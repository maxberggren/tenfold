#include "export.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

#include "drums.h"
#include "dsp.h"
#include "sampler.h"
#include "session.h"
#include "songfile.h"
#include "studio.h"
#include "tuning.h"
#include "wav.h"

namespace tf {

namespace {

// The export's own clock: its session's touch clock is the samples it has
// rendered (a thread of its own, beside the window's live one).
thread_local double t_exportClock = 0;
double exportClock() { return t_exportClock; }

void put16(QByteArray &b, uint16_t v) {
    b.append(char(v & 0xff));
    b.append(char(v >> 8));
}
void put32(QByteArray &b, uint32_t v) {
    put16(b, uint16_t(v & 0xffff));
    put16(b, uint16_t(v >> 16));
}
void putText(QByteArray &b, const QByteArray &text, int size) {
    QByteArray t = text.left(size);
    t.append(QByteArray(size - t.size(), '\0'));
    b.append(t);
}
void putVar(QByteArray &b, uint32_t v) {
    char bytes[5];
    int n = 0;
    bytes[n++] = char(v & 0x7f);
    while ((v >>= 7) != 0)
        bytes[n++] = char(0x80 | (v & 0x7f));
    while (n > 0)
        b.append(bytes[--n]);
}

QString plainKey(int root, int family, int brightness) {
    QString name = QString::fromUtf8(Tuning::noteName(((root % 12) + 12) % 12));
    name.replace(QStringLiteral("♯"), QStringLiteral("#")).replace(QStringLiteral("♭"), QStringLiteral("b"));
    const ScaleFamily &fam = tf::family(std::clamp(family, 0, familyCount() - 1));
    return name + QLatin1Char(' ') +
           QString::fromUtf8(fam.modeNames[size_t(std::clamp(brightness, 0, fam.modeCount - 1))]).toLower();
}

// The parts whose notes mean something as notes, and their MIDI channels.
int midiChannel(const QString &part) {
    if (part == QLatin1String("Drums"))
        return 9;
    if (part == QLatin1String("Bass"))
        return 0;
    if (part == QLatin1String("Chords"))
        return 1;
    if (part == QLatin1String("Lead"))
        return 2;
    if (part == QLatin1String("Arp"))
        return 3;
    return -1;
}
// The parts whose loops are pitched, so a root note in the WAV means
// something to a DAW that re-keys loops.
bool tonal(const QString &part) {
    return part != QLatin1String("Drums") && part != QLatin1String("FX") && part != QLatin1String("Sampler");
}

// One voice of a part: a name, and what of the part's commands it hears.
struct VoiceSpec {
    QString name;
    std::function<bool(Command &)> keep;
};

// What a replayed command's layer is (0 live), and whether it is a sound
// starting, moving or stopping: what a voice of one layer leaves out of
// the others. Settings (knobs, a lane's state) reach every voice, as they
// shape the whole part.
bool playsSound(const Command &c) {
    return c.type == Command::On || c.type == Command::Off || c.type == Command::Move || (c.flags & Command::Quantize);
}

std::vector<VoiceSpec> voicesOf(const QString &part, const std::vector<ClipSet::Clip> &clips, const QJsonObject &settings) {
    std::vector<VoiceSpec> out;
    if (part == QLatin1String("Drums")) {
        // A pad of a kit each: what a hit names (its kit, 0 the page's).
        int pageKit = drums::House;
        const QString kitName = settings.value(QStringLiteral("kit")).toString();
        for (int k = 0; k < drums::KitCount; ++k)
            if (kitName == QLatin1String(drums::kitName(k)))
                pageKit = k;
        std::map<std::pair<int, int>, int> seen; // (kit + 1, pad) → hits
        for (const auto &clip : clips)
            for (const ClipEvent &e : clip.events)
                if (e.c.type == drums::Hit || e.c.type == Command::On)
                    ++seen[{e.c.i[2], std::clamp(e.c.i[0], 0, drums::PadCount - 1)}];
        int kits = 0;
        {
            std::map<int, int> k;
            for (auto &[key, n] : seen)
                k[key.first == 0 ? pageKit + 1 : key.first] = 1;
            kits = int(k.size());
        }
        for (auto &[key, n] : seen) {
            const int kitRaw = key.first, pad = key.second;
            const int kit = kitRaw == 0 ? pageKit : std::clamp(kitRaw - 1, 0, drums::KitCount - 1);
            QString name = QString::fromUtf8(drums::padName(pad, kit));
            if (kits > 1)
                name = QString::fromUtf8(drums::kitName(kit)) + QLatin1Char(' ') + name;
            out.push_back({name, [kitRaw, pad](Command &c) {
                               if (c.type == drums::Hit || c.type == Command::On)
                                   return c.i[0] == pad && c.i[2] == kitRaw;
                               return true;
                           }});
        }
        return out;
    }
    if (part == QLatin1String("Sampler")) {
        // A song played whole (or a stem of it): each separated stem apart.
        // Without stems separated, a stem's voice is silent and left out.
        bool any = false;
        for (const auto &clip : clips)
            for (const ClipEvent &e : clip.events)
                any |= e.c.type == Command::On;
        if (!any)
            return out;
        for (int s = sampler::Drums; s < sampler::kStems; ++s)
            out.push_back({QString::fromLatin1(sampler::stemName(s)), [s](Command &c) {
                               if (c.type != Command::On)
                                   return true;
                               const int stem = (c.i[1] >> 8) & 0xff;
                               if (stem == sampler::Mix)
                                   c.i[1] = (c.i[1] & ~0xff00) | (s << 8);
                               else if (stem != s)
                                   return false;
                               return true;
                           }});
        return out;
    }
    // The others: the layers Space put on their loops.
    int layers = 0;
    for (const auto &clip : clips)
        for (const ClipEvent &e : clip.events)
            layers = std::max(layers, e.layer + 1);
    if (layers < 2)
        return out;
    for (int l = 0; l < layers; ++l)
        out.push_back({QStringLiteral("layer %1").arg(l + 1), [l](Command &c) {
                           if (!isReplay(c.id) || replayLayer(c.id) == l)
                               return true;
                           return !playsSound(c);
                       }});
    return out;
}

// The export's ears: every tap written from the song's first downbeat, the
// notes kept for the MIDI file.
struct Writer final : StemSink {
    struct Out {
        int tap = -1; // −1 the mix
        QString name, path;
        std::unique_ptr<WavWriter> wav;
        float peak = 0;
        bool voice = false;
    };
    std::vector<Out> outs;
    uint64_t at = 0;              // the studio's sample at the coming block
    uint64_t start = UINT64_MAX;  // the first sample written
    uint64_t end = UINT64_MAX;    // the first not written (known once the end is)
    uint64_t lastLoud = 0;        // the last sample any file had sound at
    std::vector<float> buf;
    struct RawNote {
        uint64_t sample;
        int key;
        float midi, vel;
        int length;
    };
    std::vector<std::vector<RawNote>> notes;

    void stems(const float *mixL, const float *mixR, const float *const *l, const float *const *r, int taps,
               int n) override {
        const uint64_t a = std::max(at, start), b = std::min(at + uint64_t(n), end);
        if (a < b) {
            const int i0 = int(a - at), count = int(b - a);
            for (Out &o : outs) {
                const float *L = o.tap < 0 ? mixL : o.tap < taps ? l[o.tap] : nullptr;
                const float *R = o.tap < 0 ? mixR : o.tap < taps ? r[o.tap] : nullptr;
                if (!L)
                    continue;
                buf.resize(size_t(2 * count));
                float pk = 0;
                for (int i = 0; i < count; ++i) {
                    buf[size_t(2 * i)] = L[i0 + i];
                    buf[size_t(2 * i + 1)] = R[i0 + i];
                    pk = std::max(pk, std::max(std::fabs(L[i0 + i]), std::fabs(R[i0 + i])));
                }
                o.wav->write(buf.data(), size_t(count));
                o.peak = std::max(o.peak, pk);
                if (pk > 1e-4f) // −80 dBFS
                    lastLoud = b;
            }
        }
        at += uint64_t(n);
    }
    void note(int part, uint64_t sample, int key, float midi, float velocity, int length) override {
        if (part >= 0 && part < int(notes.size()))
            notes[size_t(part)].push_back({sample, key, midi, velocity, length});
    }
};

// A part's notes as the MIDI file's: paired up by key, from the song's
// first downbeat (tick 0) to its end.
std::vector<MidiNote> midiNotes(std::vector<Writer::RawNote> raw, uint64_t zero, uint64_t end, double samplesPerBeat,
                                int ppq) {
    std::stable_sort(raw.begin(), raw.end(), [](const auto &a, const auto &b) { return a.sample < b.sample; });
    std::vector<MidiNote> out;
    auto tick = [&](uint64_t s) {
        return int64_t(std::llround((double(s) - double(zero)) / samplesPerBeat * ppq));
    };
    auto add = [&](uint64_t from, uint64_t to, float midi, float vel) {
        if (to <= zero || from >= end)
            return;
        from = std::max(from, zero);
        to = std::min(to, end);
        MidiNote n;
        n.tick = tick(from);
        n.length = std::max<int64_t>(1, tick(to) - n.tick);
        n.key = std::clamp(int(std::lround(midi)), 0, 127);
        n.velocity = std::clamp(int(std::lround(vel * 127.0f)), 1, 127);
        out.push_back(n);
    };
    struct Open {
        uint64_t from;
        float midi, vel;
    };
    std::map<int, Open> open;
    for (const auto &e : raw) {
        if (e.length > 0) {
            if (e.midi >= 0)
                add(e.sample, e.sample + uint64_t(e.length), e.midi, e.vel);
            continue;
        }
        if (auto it = open.find(e.key); it != open.end()) {
            add(it->second.from, e.sample, it->second.midi, it->second.vel);
            open.erase(it);
        }
        if (e.midi >= 0)
            open[e.key] = {e.sample, e.midi, e.vel};
    }
    for (auto &[key, o] : open)
        add(o.from, end, o.midi, o.vel);
    std::stable_sort(out.begin(), out.end(), [](const MidiNote &a, const MidiNote &b) { return a.tick < b.tick; });
    return out;
}

}

QByteArray acidChunk(double bpm, int beats, int rootNote) {
    QByteArray b("acid", 4);
    put32(b, 24);
    // Flags: 0x01 one-shot (off: a loop), 0x02 the root note is set, 0x04
    // stretch (it follows the tempo), 0x08 disk-based.
    put32(b, 0x04u | (rootNote >= 0 ? 0x02u : 0u));
    put16(b, uint16_t(rootNote >= 0 ? rootNote : 60));
    put16(b, 0x8000); // as ACID writes it
    put32(b, 0);
    put32(b, uint32_t(std::max(0, beats)));
    put16(b, 4); // the meter: 4/4
    put16(b, 4);
    const float tempo = float(bpm);
    uint32_t bits;
    std::memcpy(&bits, &tempo, 4);
    put32(b, bits);
    return b;
}

QByteArray bextChunk(const QString &description, const QDateTime &when, int rate) {
    QByteArray body;
    putText(body, description.toUtf8(), 256);
    putText(body, QByteArrayLiteral("tenfold"), 32);
    putText(body, ("tenfold" + when.toString(QStringLiteral("yyyyMMddHHmmss"))).toLatin1(), 32);
    putText(body, when.toString(QStringLiteral("yyyy-MM-dd")).toLatin1(), 10);
    putText(body, when.toString(QStringLiteral("HH:mm:ss")).toLatin1(), 8);
    put32(body, 0); // TimeReference: the first sample is at 0 (bar 1)
    put32(body, 0);
    put16(body, 1); // version 1
    body.append(QByteArray(64, '\0')); // UMID: none
    body.append(QByteArray(190, '\0'));
    body.append(QStringLiteral("A=PCM,F=%1,W=24,M=stereo,T=tenfold\r\n").arg(rate).toLatin1());
    if (body.size() & 1)
        body.append('\0');
    QByteArray b("bext", 4);
    put32(b, uint32_t(body.size()));
    b.append(body);
    return b;
}

bool keySignature(int root, int family, int brightness, int *sharps, bool *minor) {
    Tuning t;
    t.set(((root % 12) + 12) % 12, family, brightness, false);
    if (t.degreesPerOctave() != 7)
        return false;
    int semis[7];
    for (int d = 0; d < 7; ++d)
        semis[d] = int(std::lround(t.degreeSemis(d))) % 12;
    static const int major[7] = {0, 2, 4, 5, 7, 9, 11};
    for (int r = 0; r < 7; ++r) {
        bool is = true;
        for (int k = 0; k < 7; ++k)
            is &= ((semis[(r + k) % 7] - semis[r] + 12) % 12) == major[k];
        if (!is)
            continue;
        // The major key the mode is drawn from, and its sharps or flats.
        static const int fifths[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
        *sharps = fifths[(t.root() + semis[r]) % 12];
        *minor = semis[r] == 3; // the tonic a minor third under it: aeolian
        return true;
    }
    return false;
}

QByteArray midiFile(const QString &title, double bpm, int ppq, int sharps, bool minor, const std::vector<MidiTrack> &tracks) {
    auto track = [](const QByteArray &events) {
        QByteArray t("MTrk", 4);
        const uint32_t n = uint32_t(events.size());
        t.append(char(n >> 24));
        t.append(char(n >> 16));
        t.append(char(n >> 8));
        t.append(char(n));
        t.append(events);
        return t;
    };
    auto meta = [](QByteArray &e, int type, const QByteArray &data) {
        putVar(e, 0);
        e.append(char(0xFF));
        e.append(char(type));
        putVar(e, uint32_t(data.size()));
        e.append(data);
    };
    QByteArray file("MThd", 4);
    for (char c : {0, 0, 0, 6, 0, 1})
        file.append(c);
    const int count = int(tracks.size()) + 1;
    file.append(char(count >> 8));
    file.append(char(count));
    file.append(char(ppq >> 8));
    file.append(char(ppq));

    QByteArray head;
    meta(head, 0x03, title.toUtf8());
    const uint32_t us = uint32_t(std::lround(60000000.0 / bpm));
    auto bytes = [](std::initializer_list<int> v) {
        QByteArray b;
        for (int x : v)
            b.append(char(x));
        return b;
    };
    meta(head, 0x51, bytes({int(us >> 16) & 0xff, int(us >> 8) & 0xff, int(us) & 0xff}));
    meta(head, 0x58, bytes({4, 2, 24, 8}));
    if (sharps >= -7 && sharps <= 7)
        meta(head, 0x59, bytes({sharps & 0xff, minor ? 1 : 0}));
    meta(head, 0x2F, {});
    file.append(track(head));

    for (const MidiTrack &t : tracks) {
        QByteArray e;
        meta(e, 0x03, t.name.toUtf8());
        // Note-offs before note-ons at the same tick, so a note struck again
        // is not cut by its own end.
        struct Ev {
            int64_t tick;
            bool on;
            int key, vel;
        };
        std::vector<Ev> evs;
        for (const MidiNote &n : t.notes) {
            evs.push_back({n.tick, true, n.key, n.velocity});
            evs.push_back({n.tick + std::max<int64_t>(1, n.length), false, n.key, 0});
        }
        std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) {
            return a.tick != b.tick ? a.tick < b.tick : (!a.on && b.on);
        });
        int64_t last = 0;
        // A key struck while it already sounds: the old note ends there.
        int sounding[128] = {};
        for (const Ev &v : evs) {
            if (!v.on && sounding[v.key] == 0)
                continue;
            putVar(e, uint32_t(std::max<int64_t>(0, v.tick - last)));
            last = std::max(last, v.tick);
            const int ch = std::clamp(t.channel, 0, 15);
            if (v.on) {
                if (sounding[v.key] > 0) {
                    e.append(char(0x80 | ch));
                    e.append(char(v.key));
                    e.append(char(64));
                    putVar(e, 0);
                    --sounding[v.key];
                }
                e.append(char(0x90 | ch));
                e.append(char(v.key));
                e.append(char(std::clamp(v.vel, 1, 127)));
                ++sounding[v.key];
            } else {
                e.append(char(0x80 | ch));
                e.append(char(v.key));
                e.append(char(64));
                --sounding[v.key];
            }
        }
        meta(e, 0x2F, {});
        file.append(track(e));
    }
    return file;
}

QString exportFileName(const QString &prefix, const QString &index, const QString &name, double bpm, const QString &key) {
    QString n = QStringLiteral("%1%2 %3 - %4 bpm %5").arg(prefix, index, name).arg(int(std::lround(bpm))).arg(key);
    // Nothing a file system would refuse.
    for (QChar &c : n)
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':') || c == QLatin1Char('*') ||
            c == QLatin1Char('?') || c == QLatin1Char('"') || c == QLatin1Char('<') || c == QLatin1Char('>') ||
            c == QLatin1Char('|'))
            c = QLatin1Char('-');
    return n;
}

ExportResult exportSong(const QJsonObject &songIn, const ExportOptions &o, const std::atomic<bool> *cancel,
                        const ExportProgress &progress) {
    flushDenormals();
    ExportResult res;
    res.folder = o.folder;
    auto say = [&](double f, const QString &what) {
        if (progress)
            progress(std::clamp(f, 0.0, 1.0), what);
    };
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (o.folder.isEmpty() || !QDir().mkpath(o.folder)) {
        res.error = QStringLiteral("Can't write to %1").arg(o.folder.isEmpty() ? QStringLiteral("no folder") : o.folder);
        return res;
    }
    say(0, QStringLiteral("Getting ready"));

    // The song as it will be played: no click, and nothing launched by the
    // file itself (the export starts what plays, at bar 1).
    QJsonObject doc = songIn;
    doc[QStringLiteral("metronome")] = false;
    int playing[kMaxParts];
    std::fill(std::begin(playing), std::end(playing), -1);
    QJsonArray partsJson = doc.value(QStringLiteral("parts")).toArray();
    struct PartIn {
        QString name;
        QJsonObject settings;
        std::vector<ClipSet::Clip> clips;
        int playing = -1;
    };
    std::vector<PartIn> partsIn;
    for (qsizetype i = 0; i < partsJson.size(); ++i) {
        QJsonObject po = partsJson[i].toObject();
        PartIn in;
        in.name = po.value(QStringLiteral("name")).toString();
        in.settings = po.value(QStringLiteral("settings")).toObject();
        in.playing = po.value(QStringLiteral("playing")).toInt(-1);
        for (const QJsonValue &cv : po.value(QStringLiteral("clips")).toArray()) {
            int slot = -1;
            ClipSet::Clip clip;
            QString why;
            if (clipFromJson(cv, &slot, &clip, &why))
                in.clips.push_back(std::move(clip));
        }
        partsIn.push_back(std::move(in));
        po[QStringLiteral("playing")] = -1;
        partsJson[i] = po;
    }
    doc[QStringLiteral("parts")] = partsJson;

    const float rate = o.rate > 8000 ? o.rate : 48000.0f;
    Studio studio(rate);
    studio.setTimeOrigin(0);
    t_exportClock = 0;
    Session session(&studio);
    session.setClock(&exportClock);
    session.setAspect(1.6f);

    Writer w;
    w.notes.resize(size_t(session.partCount()));
    StemOptions so;
    so.preFader = o.preFader;
    so.master = o.master;
    studio.exportStems(&w, so);

    // Which parts, and their voices.
    std::vector<int> wanted;
    for (int p = 0; p < session.partCount(); ++p) {
        const QString name = session.pageName(p);
        const PartIn *in = nullptr;
        for (const PartIn &x : partsIn)
            if (x.name == name)
                in = &x;
        if (!in || in->clips.empty()) {
            if (o.parts.contains(name))
                res.notes << QStringLiteral("%1 has no loops").arg(name);
            continue;
        }
        if (!o.parts.isEmpty() && !o.parts.contains(name))
            continue;
        wanted.push_back(p);
    }
    struct VoiceOut {
        int part, tap, index;
        QString name;
    };
    std::vector<VoiceOut> voiceOuts;
    if (o.voices)
        for (int p : wanted) {
            const QString name = session.pageName(p);
            for (const PartIn &in : partsIn) {
                if (in.name != name)
                    continue;
                const std::vector<VoiceSpec> specs = voicesOf(name, in.clips, in.settings);
                int k = 0;
                for (const VoiceSpec &v : specs) {
                    const int tap = studio.addVoice(p, v.keep);
                    if (tap < 0) {
                        res.notes << QStringLiteral("%1 can't be split into voices").arg(name);
                        break;
                    }
                    voiceOuts.push_back({p, tap, ++k, v.name});
                }
                if (specs.empty() && name == QLatin1String("Vocal"))
                    res.notes << QStringLiteral("Vocal is one stem: its takes share one tape");
            }
        }

    QString why;
    if (!session.openSong(doc, &why)) {
        res.error = why;
        return res;
    }
    session.refresh();

    // Song or loops, and how long.
    const int songBars = session.songBars();
    const bool song = o.song && songBars > 0;
    if (o.song && songBars == 0)
        res.notes << QStringLiteral("No song on the Song page: the loops playing, for %1 bars").arg(o.bars);
    int launches = 0;
    if (!song)
        for (int p = 0; p < session.partCount() && p < kMaxParts; ++p)
            for (const PartIn &in : partsIn)
                if (in.name == session.pageName(p) && in.playing >= 0) {
                    playing[p] = in.playing;
                    ++launches;
                }
    if (!song && launches == 0) {
        res.error = QStringLiteral("Nothing is playing to export: start some loops, or make a song on the Song page");
        return res;
    }
    const int bars = song ? songBars : std::clamp(o.bars, 1, 512);
    res.bars = bars;
    constexpr int kFirstBar = 1; // a bar of lead-in, for what is launched ahead of the song
    if (song)
        session.playSong(kFirstBar);

    const double startBeat = kFirstBar * kBeatsPerBar, endBeat = startBeat + double(bars) * kBeatsPerBar;
    const int framesPerTick = std::max(1, int(std::lround(rate / 125.0f)));
    std::vector<float> scratch(size_t(framesPerTick) * 2);
    bool started = false;
    int stops = 0;
    double bpm = 0, spb = 0; // samples per beat
    uint64_t zero = 0, contentEnd = 0, cap = 0;
    // The files, opened once the tempo is known (at the first ticks).
    auto openFiles = [&]() -> bool {
        const StudioView &v = session.view();
        const QString key = plainKey(v.root, v.family, v.brightness);
        const int beatsTotal = bars * kBeatsPerBar; // the tail adds to it below
        (void)beatsTotal;
        auto add = [&](int tap, const QString &index, const QString &name, bool voice, bool pitched) {
            Writer::Out out;
            out.tap = tap;
            out.voice = voice;
            out.name = exportFileName(o.prefix, index, name, bpm, key) + QStringLiteral(".wav");
            out.path = QDir(o.folder).filePath(out.name);
            out.wav = std::make_unique<WavWriter>();
            // The chunks are written again on close, with the real length.
            (void)pitched;
            w.outs.push_back(std::move(out));
        };
        if (o.mix)
            add(-1, QStringLiteral("00"), QStringLiteral("Mix"), false, true);
        for (int p : wanted) {
            const QString idx = QStringLiteral("%1").arg(p + 1, 2, 10, QLatin1Char('0'));
            add(p, idx, session.pageName(p), false, tonal(session.pageName(p)));
            for (const VoiceOut &vo : voiceOuts)
                if (vo.part == p)
                    add(vo.tap, idx + QStringLiteral(".%1").arg(vo.index, 2, 10, QLatin1Char('0')),
                        session.pageName(p) + QLatin1Char(' ') + vo.name, true, tonal(session.pageName(p)));
        }
        // Opened with chunks the right size; their contents are rewritten
        // once the length is known (see below).
        const QByteArray placeholder = bextChunk({}, QDateTime::currentDateTime(), int(rate)) + acidChunk(bpm, 0, -1);
        for (Writer::Out &out : w.outs)
            if (!out.wav->open(QFile::encodeName(out.path).toStdString(), int(rate), placeholder.toStdString())) {
                res.error = QStringLiteral("Can't write %1").arg(out.path);
                return false;
            }
        return true;
    };

    say(0.02, QStringLiteral("Rendering"));
    for (int tick = 0;; ++tick) {
        if (cancelled()) {
            res.cancelled = true;
            break;
        }
        const double t = double(studio.samples()) / rate;
        t_exportClock = t;
        session.refresh();
        session.idle(t);
        const double beat = session.view().beat;
        if (!started && (beat > 1.5 || tick > 125)) {
            // The tempo is the studio's now; the loops start on bar 1.
            started = true;
            bpm = session.view().bpm;
            spb = 60.0 / bpm * rate;
            zero = uint64_t(std::floor(startBeat * spb + 1e-7));
            w.start = zero + uint64_t(Studio::kStemLatency);
            contentEnd = uint64_t(std::floor(endBeat * spb + 1e-7));
            cap = contentEnd + uint64_t(o.tails ? o.maxTail * rate : 0);
            if (!o.tails)
                w.end = contentEnd + uint64_t(Studio::kStemLatency);
            if (!song)
                for (int p = 0; p < kMaxParts; ++p)
                    if (playing[p] >= 0) {
                        StudioMsg m;
                        m.kind = StudioMsg::Launch;
                        m.part = int8_t(p);
                        m.slot = int8_t(playing[p]);
                        studio.post(m);
                    }
            if (!openFiles())
                break;
        }
        // Half a bar before the end, everything is told to stop on it, and
        // again a moment before (in case the song launched a loop after).
        if (started && ((stops == 0 && beat >= endBeat - 2) || (stops == 1 && beat >= endBeat - 0.5))) {
            ++stops;
            for (int p = 0; p < session.partCount(); ++p) {
                StudioMsg m;
                m.kind = StudioMsg::Stop;
                m.part = int8_t(p);
                studio.post(m);
            }
        }
        studio.render(scratch.data(), framesPerTick);
        const uint64_t now = studio.samples();
        if (started && now >= w.end)
            break;
        if (started && o.tails && now > contentEnd + uint64_t(Studio::kStemLatency)) {
            // Ringing out: until every file has been quiet for half a
            // second, or the most a tail may take; then on to the bar line.
            if ((now > w.lastLoud + uint64_t(rate / 2) || now >= cap) && w.end == UINT64_MAX) {
                const double beatsSoFar = (double(now) - double(zero)) / spb;
                const double barsTotal = std::ceil(beatsSoFar / kBeatsPerBar - 1e-9);
                w.end = zero + uint64_t(std::llround(barsTotal * kBeatsPerBar * spb)) + uint64_t(Studio::kStemLatency);
            }
        }
        if (started) {
            const double total = double((o.tails ? cap : contentEnd) + Studio::kStemLatency - 0);
            say(0.02 + 0.93 * double(now) / std::max(1.0, total),
                now < contentEnd ? QStringLiteral("Rendering bar %1 of %2")
                                       .arg(std::clamp(int((double(now) - double(zero)) / spb / kBeatsPerBar) + 1, 1, bars))
                                       .arg(bars)
                                 : QStringLiteral("Letting the tails ring out"));
        }
    }

    // The length every file has, its beats and the chunks that say so.
    const int64_t frames = w.end != UINT64_MAX && w.end > w.start ? int64_t(w.end - w.start) : 0;
    res.frames = frames;
    res.bpm = bpm;
    res.seconds = double(frames) / rate;
    const StudioView &v = session.view();
    const QString keyText = plainKey(v.root, v.family, v.brightness);
    const int beats = spb > 0 ? int(std::lround(double(frames) / spb)) : 0;
    const QDateTime when = QDateTime::currentDateTime();
    for (Writer::Out &out : w.outs) {
        out.wav->close();
        if (res.cancelled || !res.error.isEmpty()) {
            QFile::remove(out.path);
            continue;
        }
        // The chunks, rewritten in place now that the length is known.
        const bool pitched = out.tap < 0 || (out.tap < session.partCount() && tonal(session.pageName(out.tap))) ||
                             (out.tap >= session.partCount() && [&] {
                                 for (const VoiceOut &vo : voiceOuts)
                                     if (vo.tap == out.tap)
                                         return tonal(session.pageName(vo.part));
                                 return false;
                             }());
        const QByteArray chunks =
            bextChunk(QStringLiteral("tenfold: %1, %2 bpm, %3, %4 beats from bar 1")
                          .arg(QFileInfo(out.name).completeBaseName())
                          .arg(bpm, 0, 'f', 3)
                          .arg(keyText)
                          .arg(beats),
                      when, int(rate)) +
            acidChunk(bpm, beats, pitched ? 60 + ((v.root % 12) + 12) % 12 : -1);
        QFile f(out.path);
        if (f.open(QIODevice::ReadWrite)) {
            f.seek(36);
            f.write(chunks);
            f.close();
        }
        // A voice that never sounded (a stem not separated) says nothing.
        if (out.voice && out.peak < 1e-4f) {
            QFile::remove(out.path);
            res.notes << QStringLiteral("%1: silent, left out").arg(QFileInfo(out.name).completeBaseName());
            continue;
        }
        res.files << out.name;
    }
    if (res.cancelled || !res.error.isEmpty())
        return res;

    if (o.midi) {
        say(0.97, QStringLiteral("Writing the MIDI file"));
        constexpr int kPpq = 480;
        std::vector<MidiTrack> tracks;
        for (int p : wanted) {
            const QString name = session.pageName(p);
            const int ch = midiChannel(name);
            if (ch < 0)
                continue;
            MidiTrack t;
            t.name = name;
            t.channel = ch;
            t.notes = midiNotes(w.notes[size_t(p)], zero, contentEnd, spb, kPpq);
            if (!t.notes.empty())
                tracks.push_back(std::move(t));
        }
        if (!tracks.empty()) {
            int sharps = 99;
            bool minor = false;
            if (!keySignature(v.root, v.family, v.brightness, &sharps, &minor))
                sharps = 99;
            const QString name = exportFileName(o.prefix, QStringLiteral("00"), QStringLiteral("MIDI"), bpm, keyText) +
                                 QStringLiteral(".mid");
            QFile f(QDir(o.folder).filePath(name));
            if (f.open(QIODevice::WriteOnly) &&
                f.write(midiFile(o.prefix.trimmed().isEmpty() ? QStringLiteral("tenfold") : o.prefix.trimmed(), bpm, kPpq,
                                 sharps, minor, tracks)) > 0) {
                res.files << name;
            } else {
                res.notes << QStringLiteral("The MIDI file couldn't be written");
            }
        }
    }
    say(1, QStringLiteral("Done"));
    res.ok = true;
    return res;
}

}
