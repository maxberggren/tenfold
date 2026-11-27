// Export: a scripted session's song rendered into a folder of stems, voices
// and a MIDI file, and read back as another studio would read them.

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <chrono>
#include <cstring>
#include <thread>

#include "export.h"
#include "testkit.h"

using namespace tf;

namespace {

// At 120 BPM, a bar every 2 s from 2.5: a beat on the drums (kick, snare,
// kick, snare), a bass line, chords and an atmos bed, each a bar looped.
const char *kLoops = "page 0 1\n"
                     "tap 0.5 0.12 0.12\ntap 1.0 0.38 0.12\ntap 1.5 0.12 0.12\ntap 2.0 0.38 0.12\nkey 2.5 Space\n"
                     "page 3.0 2\n"
                     "tap 4.5 0.03 0.5 len=0.3\ntap 5.0 0.03 0.5 len=0.15\ntap 5.5 0.36 0.5 len=0.3\n"
                     "tap 6.0 0.3 0.5 len=0.15\nkey 6.6 Space\n"
                     "page 7.0 3\n"
                     "tap 8.5 0.03 0.6 len=0.15\ntap 9.0 0.03 0.6 len=0.15\ntap 9.5 0.36 0.6 len=0.15\n"
                     "tap 10.0 0.36 0.6 len=0.15\nkey 10.6 Space\n"
                     "page 11.0 6\n"
                     "tap 12.5 0.03 0.4 len=0.9\ntap 13.5 0.17 0.4 len=0.9\nkey 14.6 Space\n";

QJsonObject saved(const std::string &script) {
    QJsonObject doc;
    testkit::play(script, [&](Session &s) { doc = s.saveSong(); });
    return doc;
}

struct Wav {
    int rate = 0, channels = 0, bits = 0;
    std::vector<float> l, r;
    QByteArray acid, bext;
    std::vector<QByteArray> order; // the chunks' ids, in order
};

bool readWav(const QString &path, Wav *w) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray b = f.readAll();
    auto u32 = [&](qsizetype at) {
        return uint32_t(uint8_t(b[at])) | uint32_t(uint8_t(b[at + 1])) << 8 | uint32_t(uint8_t(b[at + 2])) << 16 |
               uint32_t(uint8_t(b[at + 3])) << 24;
    };
    if (b.size() < 12 || b.left(4) != "RIFF" || b.mid(8, 4) != "WAVE" || u32(4) != uint32_t(b.size() - 8))
        return false;
    qsizetype at = 12;
    while (at + 8 <= b.size()) {
        const QByteArray id = b.mid(at, 4);
        const uint32_t size = u32(at + 4);
        const QByteArray body = b.mid(at + 8, size);
        w->order.push_back(id);
        if (id == "fmt ") {
            w->channels = uint8_t(body[2]);
            w->rate = int(uint32_t(uint8_t(body[4])) | uint32_t(uint8_t(body[5])) << 8 | uint32_t(uint8_t(body[6])) << 16);
            w->bits = uint8_t(body[14]);
        } else if (id == "acid") {
            w->acid = body;
        } else if (id == "bext") {
            w->bext = body;
        } else if (id == "data") {
            const qsizetype frames = body.size() / 6;
            for (qsizetype i = 0; i < frames; ++i) {
                auto s24 = [&](qsizetype k) {
                    const int32_t v = int32_t(uint32_t(uint8_t(body[k])) << 8 | uint32_t(uint8_t(body[k + 1])) << 16 |
                                              uint32_t(uint8_t(body[k + 2])) << 24) >>
                                      8;
                    return float(v) / 8388608.0f;
                };
                w->l.push_back(s24(i * 6));
                w->r.push_back(s24(i * 6 + 3));
            }
        }
        at += 8 + size + (size & 1);
    }
    return w->rate > 0 && !w->l.empty();
}

float acidTempo(const QByteArray &a) {
    float t;
    std::memcpy(&t, a.constData() + 20, 4);
    return t;
}
uint32_t acidBeats(const QByteArray &a) {
    uint32_t v;
    std::memcpy(&v, a.constData() + 12, 4);
    return v;
}
uint32_t acidFlags(const QByteArray &a) {
    uint32_t v;
    std::memcpy(&v, a.constData(), 4);
    return v;
}

// A Standard MIDI File read back: its tempo and every track's notes.
struct Smf {
    int format = -1, ppq = 0;
    uint32_t tempo = 0;
    int sharps = 99, minor = -1;
    struct Note {
        int64_t tick, length;
        int key, vel, channel;
    };
    struct Track {
        QString name;
        std::vector<Note> notes;
    };
    std::vector<Track> tracks;
};

bool readMidi(const QString &path, Smf *m) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray b = f.readAll();
    auto be = [&](qsizetype at, int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v = v << 8 | uint8_t(b[at + i]);
        return v;
    };
    if (b.left(4) != "MThd" || be(4, 4) != 6)
        return false;
    m->format = int(be(8, 2));
    const int count = int(be(10, 2));
    m->ppq = int(be(12, 2));
    qsizetype at = 14;
    for (int t = 0; t < count; ++t) {
        if (b.mid(at, 4) != "MTrk")
            return false;
        const qsizetype end = at + 8 + qsizetype(be(at + 4, 4));
        at += 8;
        Smf::Track track;
        int64_t tick = 0;
        std::map<int, std::pair<int64_t, int>> open;
        uint8_t status = 0;
        while (at < end) {
            uint32_t delta = 0;
            uint8_t c;
            do {
                c = uint8_t(b[at++]);
                delta = delta << 7 | (c & 0x7f);
            } while (c & 0x80);
            tick += delta;
            uint8_t s = uint8_t(b[at]);
            if (s & 0x80)
                ++at;
            else
                s = status;
            if (s == 0xFF) {
                const int type = uint8_t(b[at++]);
                uint32_t len = 0;
                do {
                    c = uint8_t(b[at++]);
                    len = len << 7 | (c & 0x7f);
                } while (c & 0x80);
                const QByteArray data = b.mid(at, len);
                at += len;
                if (type == 0x51)
                    m->tempo = uint32_t(uint8_t(data[0])) << 16 | uint32_t(uint8_t(data[1])) << 8 | uint8_t(data[2]);
                if (type == 0x59) {
                    m->sharps = int8_t(data[0]);
                    m->minor = data[1];
                }
                if (type == 0x03)
                    track.name = QString::fromUtf8(data);
                continue;
            }
            status = s;
            const int kind = s & 0xF0, ch = s & 0x0F;
            const int key = uint8_t(b[at++]), vel = uint8_t(b[at++]);
            if (kind == 0x90 && vel > 0) {
                open[key] = {tick, vel};
            } else if (kind == 0x80 || kind == 0x90) {
                if (auto it = open.find(key); it != open.end()) {
                    track.notes.push_back({it->second.first, tick - it->second.first, key, it->second.second, ch});
                    open.erase(it);
                }
            }
        }
        std::sort(track.notes.begin(), track.notes.end(), [](auto &x, auto &y) { return x.tick < y.tick; });
        m->tracks.push_back(track);
        at = end;
    }
    return true;
}

QString find(const QStringList &files, const QString &part) {
    for (const QString &f : files)
        if (f.contains(QStringLiteral(" ") + part + QStringLiteral(" - ")) && f.endsWith(QStringLiteral(".wav")))
            return f;
    return {};
}

double rms(const std::vector<float> &x) {
    double s = 0;
    for (float v : x)
        s += double(v) * v;
    return std::sqrt(s / double(std::max<size_t>(1, x.size())));
}

// The first sample louder than −40 dBFS.
size_t onset(const Wav &w) {
    for (size_t i = 0; i < w.l.size(); ++i)
        if (std::fabs(w.l[i]) > 0.01f || std::fabs(w.r[i]) > 0.01f)
            return i;
    return w.l.size();
}

}

class ExportTests : public QObject {
    Q_OBJECT
    // The quick checks: the chunks and key signatures, and a cancel.
    Q_CLASSINFO("dev", "theChunksSayTheTempoAndTheBeats keySignaturesFollowTheMode aCancelledExportLeavesNothing anExportThatWritesNothingFails")
private slots:
    // Parts asked for by names none has, with the mix and MIDI off: nothing
    // written is a failed export, not an empty success. A song saved paused
    // exports the loops the pause stopped.
    void anExportThatWritesNothingFails() {
        const QJsonObject doc = saved(std::string(kLoops) + "end 16\n");
        QTemporaryDir dir;
        ExportOptions o;
        o.folder = dir.filePath(QStringLiteral("none"));
        o.song = false;
        o.bars = 1;
        o.mix = o.midi = false;
        o.parts = {QStringLiteral("Nope")};
        const ExportResult r = exportSong(doc, o);
        QVERIFY(!r.ok);
        QVERIFY(r.files.isEmpty());
        QVERIFY(r.error.startsWith(QStringLiteral("Nothing was written")));
        QVERIFY(!QDir(o.folder).exists());
        const QJsonObject paused = saved(std::string(kLoops) + "key 15.5 Ctrl+P\nend 16\n");
        ExportOptions p;
        p.folder = dir.filePath(QStringLiteral("paused"));
        p.song = false;
        p.bars = 1;
        p.midi = false;
        const ExportResult pr = exportSong(paused, p);
        QVERIFY2(pr.ok, qPrintable(pr.error));
        QVERIFY(!pr.files.isEmpty());
    }
    void theChunksSayTheTempoAndTheBeats() {
        const QByteArray a = acidChunk(123.5, 64, 69);
        QCOMPARE(a.left(4), QByteArray("acid"));
        QCOMPARE(a.size(), 8 + 24);
        const QByteArray body = a.mid(8);
        QCOMPARE(acidTempo(body), 123.5f);
        QCOMPARE(acidBeats(body), 64u);
        QVERIFY(!(acidFlags(body) & 1)); // a loop, not a one-shot
        QVERIFY(acidFlags(body) & 2);
        QCOMPARE(int(uint8_t(body[4])), 69);
        QVERIFY(!(acidFlags(acidChunk(120, 4, -1).mid(8)) & 2));
        const QByteArray b = bextChunk(QStringLiteral("x"), QDateTime(QDate(2026, 11, 27), QTime(9, 5)), 48000);
        QCOMPARE(b.left(4), QByteArray("bext"));
        QVERIFY(b.size() % 2 == 0);
        QCOMPARE(b.mid(8 + 256 + 64, 10), QByteArray("2026-11-27"));
    }

    void keySignaturesFollowTheMode() {
        int sharps = 0;
        bool minor = false;
        // A aeolian: no sharps, minor. D dorian: none (C major's). E major
        // (ionian, brightness 1 is ionian in the diatonic family? found by name).
        QVERIFY(keySignature(9, 0, 4, &sharps, &minor));
        QCOMPARE(sharps, 0);
        QVERIFY(minor);
        const ScaleFamily &fam = family(0);
        for (int b = 0; b < fam.modeCount; ++b) {
            if (QString::fromUtf8(fam.modeNames[size_t(b)]) == QLatin1String("Ionian")) {
                QVERIFY(keySignature(4, 0, b, &sharps, &minor)); // E major: four sharps
                QCOMPARE(sharps, 4);
                QVERIFY(!minor);
                QVERIFY(keySignature(5, 0, b, &sharps, &minor)); // F major: one flat
                QCOMPARE(sharps, -1);
            }
            if (QString::fromUtf8(fam.modeNames[size_t(b)]) == QLatin1String("Dorian")) {
                QVERIFY(keySignature(2, 0, b, &sharps, &minor));
                QCOMPARE(sharps, 0);
            }
        }
    }

    void theLoopsExportAsAlignedStemsThatAddUpToTheMix() {
        const QJsonObject doc = saved(std::string(kLoops) + "end 16\n");
        QVERIFY(!doc.isEmpty());
        QTemporaryDir dir;
        ExportOptions o;
        o.folder = dir.path();
        o.prefix = QStringLiteral("test ");
        o.song = false;
        o.bars = 4;
        o.voices = true;
        o.tails = false;
        std::vector<double> seen;
        const ExportResult r = exportSong(doc, o, nullptr, [&](double f, const QString &) { seen.push_back(f); });
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(std::lround(r.bpm)), 120);
        QVERIFY(!seen.empty() && seen.back() == 1.0);
        // Four bars at 120 BPM and 48 kHz: 16 beats of 24000 samples.
        QCOMPARE(r.frames, int64_t(16 * 24000));
        for (const char *part : {"Mix", "Drums", "Bass", "Chords", "Atmos"})
            QVERIFY2(!find(r.files, QLatin1String(part)).isEmpty(), part);
        QVERIFY2(find(r.files, QStringLiteral("Lead")).isEmpty(), "a part with no loops has no file");

        Wav mix, parts[4];
        QVERIFY(readWav(dir.filePath(find(r.files, QStringLiteral("Mix"))), &mix));
        const char *names[4] = {"Drums", "Bass", "Chords", "Atmos"};
        for (int i = 0; i < 4; ++i) {
            QVERIFY(readWav(dir.filePath(find(r.files, QLatin1String(names[i]))), &parts[i]));
            QCOMPARE(parts[i].l.size(), mix.l.size());
            QCOMPARE(parts[i].rate, 48000);
            QCOMPARE(parts[i].bits, 24);
            QCOMPARE(parts[i].channels, 2);
            QCOMPARE(acidTempo(parts[i].acid), 120.0f);
            QCOMPARE(acidBeats(parts[i].acid), 16u);
            QVERIFY(!parts[i].bext.isEmpty());
            QVERIFY(rms(parts[i].l) > 1e-3);
        }
        QCOMPARE(mix.l.size(), size_t(r.frames));
        // The drums' bar 1 starts on the file's first sample: the kick on
        // the ONE, within a couple of milliseconds (its own attack).
        QVERIFY2(onset(parts[0]) < 96, qPrintable(QString::number(onset(parts[0]))));
        QVERIFY2(onset(mix) < 96, qPrintable(QString::number(onset(mix))));
        // No drum key; the tonal ones say the key's root (A: 69).
        QVERIFY(!(acidFlags(parts[0].acid) & 2));
        QVERIFY(acidFlags(parts[1].acid) & 2);
        QCOMPARE(int(uint8_t(parts[1].acid[4])), 69);

        // With the master's gains on every stem, the stems are the mix
        // (to within the dither, and the echo's and shimmer's soft clipping).
        std::vector<float> diff(mix.l.size() * 2);
        for (size_t i = 0; i < mix.l.size(); ++i) {
            float l = 0, rr = 0;
            for (auto &p : parts) {
                l += p.l[i];
                rr += p.r[i];
            }
            diff[2 * i] = l - mix.l[i];
            diff[2 * i + 1] = rr - mix.r[i];
        }
        const double ratio = rms(diff) / rms(mix.l);
        QVERIFY2(ratio < 0.001, qPrintable(QStringLiteral("stems − mix at %1 dB").arg(20 * std::log10(ratio))));

        // The drums' voices: a pad each.
        QStringList drumVoices;
        for (const QString &f : r.files)
            if (f.startsWith(QStringLiteral("test 01.")))
                drumVoices << f;
        QVERIFY2(drumVoices.size() >= 2, qPrintable(r.files.join(", ")));
        QVERIFY(!find(r.files, QStringLiteral("Drums kick")).isEmpty());
        std::vector<float> sum(mix.l.size(), 0.0f);
        for (const QString &f : drumVoices) {
            Wav v;
            QVERIFY(readWav(dir.filePath(f), &v));
            QCOMPARE(v.l.size(), mix.l.size());
            for (size_t i = 0; i < v.l.size(); ++i)
                sum[i] += v.l[i];
        }
        // The same drums, pad by pad: as loud together as the drums' stem
        // (sample for sample they differ where a drum is noise: each voice
        // draws its own).
        const double together = rms(sum), whole = rms(parts[0].l);
        QVERIFY2(std::fabs(together / whole - 1) < 0.05, qPrintable(QStringLiteral("%1 / %2").arg(together).arg(whole)));

        // The MIDI file: the tempo, and the beat's hits on the beats.
        QString midiName;
        for (const QString &f : r.files)
            if (f.endsWith(QStringLiteral(".mid")))
                midiName = f;
        QVERIFY(!midiName.isEmpty());
        Smf m;
        QVERIFY(readMidi(dir.filePath(midiName), &m));
        QCOMPARE(m.format, 1);
        QCOMPARE(m.tempo, 500000u);
        QCOMPARE(m.sharps, 0);
        QCOMPARE(m.minor, 1);
        const Smf::Track *drums = nullptr, *bass = nullptr, *chords = nullptr;
        for (const auto &t : m.tracks) {
            if (t.name == QLatin1String("Drums"))
                drums = &t;
            if (t.name == QLatin1String("Bass"))
                bass = &t;
            if (t.name == QLatin1String("Chords"))
                chords = &t;
            QVERIFY2(t.name != QLatin1String("Atmos"), "no MIDI for a bed");
        }
        QVERIFY(drums && bass && chords);
        QCOMPARE(int(drums->notes.size()), 16);
        for (size_t i = 0; i < drums->notes.size(); ++i) {
            const auto &n = drums->notes[i];
            QCOMPARE(n.channel, 9);
            QVERIFY2(std::llabs(n.tick - int64_t(i) * m.ppq) <= 24, qPrintable(QString("hit %1 at %2").arg(i).arg(n.tick)));
            QCOMPARE(n.key, i % 2 == 0 ? 36 : 38);
        }
        QVERIFY(bass->notes.size() >= 4);
        QVERIFY(bass->notes.front().tick <= 24);
        for (const auto &n : bass->notes)
            QVERIFY(n.key >= 24 && n.key <= 72);
        // A chord is three notes or more, struck together.
        QVERIFY(chords->notes.size() >= 3);
        QCOMPARE(chords->notes[0].tick, chords->notes[2].tick);
    }

    void theSongExportsWholeWithItsTail() {
        // The Song page writes a track from the loops (I, twice).
        const QJsonObject doc = saved(std::string(kLoops) + "page 15.0 0\nkey 15.2 I\nkey 15.4 I\nend 16\n");
        QTemporaryDir dir;
        ExportOptions o;
        o.folder = dir.path();
        o.tails = true;
        const ExportResult r = exportSong(doc, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.bars >= 8, qPrintable(QString::number(r.bars)));
        QCOMPARE(r.files.size(), 6); // the mix, drums, bass, chords, atmos, MIDI
        // Whole bars, the song's and the tail's: past the song, not far past.
        const int64_t bar = 4 * 24000;
        QCOMPARE(r.frames % bar, int64_t(0));
        QVERIFY(r.frames > r.bars * bar);
        QVERIFY(r.frames <= r.bars * bar + int64_t(20 * 48000) + bar);
        Wav mix;
        QVERIFY(readWav(dir.filePath(find(r.files, QStringLiteral("Mix"))), &mix));
        QCOMPARE(int64_t(mix.l.size()), r.frames);
        QCOMPARE(acidBeats(mix.acid), uint32_t(r.frames / 24000));
        // The tail dies away to nothing.
        const std::vector<float> last(mix.l.end() - 4800, mix.l.end());
        QVERIFY(rms(last) < 1e-3);

        // The stems add up to the mix in every bar: the builds too, where
        // the echo rings at full feedback, the drive is turned up and the
        // master sweeps.
        std::vector<Wav> stems(4);
        const char *names[4] = {"Drums", "Bass", "Chords", "Atmos"};
        for (int k = 0; k < 4; ++k)
            QVERIFY(readWav(dir.filePath(find(r.files, QLatin1String(names[k]))), &stems[size_t(k)]));
        double worst = -200;
        int worstBar = -1;
        QStringList bars;
        for (int64_t b0 = 0; b0 + bar <= r.frames; b0 += bar) {
            double m = 0, d = 0;
            for (int64_t i = b0; i < b0 + bar; ++i)
                for (int c = 0; c < 2; ++c) {
                    const float x = c ? mix.r[size_t(i)] : mix.l[size_t(i)];
                    float sum = 0;
                    for (const Wav &w : stems)
                        sum += c ? w.r[size_t(i)] : w.l[size_t(i)];
                    m += double(x) * x;
                    d += double(sum - x) * (sum - x);
                }
            if (m < 1e-8 * double(bar)) // silent: nothing to compare
                continue;
            const double db = 10 * std::log10(d / m + 1e-30);
            bars << QString::number(db, 'f', 0);
            if (db > worst) {
                worst = db;
                worstBar = int(b0 / bar) + 1;
            }
        }
        qInfo().noquote() << "stems - mix by bar (dB):" << bars.join(QLatin1Char(' '));
        QVERIFY2(worst < -50, qPrintable(QStringLiteral("bar %1: stems - mix at %2 dB").arg(worstBar).arg(worst)));

        // The MIDI has what the mix plays: no bass note in a bar where the
        // Song page has the bass muted (its stem there is only the room's
        // tail, far under its usual level), and notes where it plays.
        QString midiName;
        for (const QString &f : r.files)
            if (f.endsWith(QStringLiteral(".mid")))
                midiName = f;
        Smf m;
        QVERIFY(readMidi(dir.filePath(midiName), &m));
        const Wav &bass = stems[1];
        std::vector<double> barLevel;
        for (int64_t b0 = 0; b0 + bar <= int64_t(bass.l.size()); b0 += bar)
            barLevel.push_back(rms(std::vector<float>(bass.l.begin() + b0, bass.l.begin() + b0 + bar)));
        std::vector<double> sorted = barLevel;
        std::sort(sorted.begin(), sorted.end());
        const double usual = sorted[sorted.size() * 3 / 4];
        int heard = 0;
        for (const auto &t : m.tracks) {
            if (t.name != QLatin1String("Bass"))
                continue;
            for (const auto &n : t.notes) {
                const size_t b = size_t(n.tick / (4 * m.ppq));
                QVERIFY2(b < barLevel.size() && barLevel[b] > 0.1 * usual,
                         qPrintable(QStringLiteral("a bass note in bar %1, where the mix has the bass muted").arg(b + 1)));
                ++heard;
            }
        }
        QVERIFY(heard > 8);

        // And the other way: every hit the drums and the bass play on a beat
        // is in the MIDI, the first of each drop (the ONE a muted part comes
        // back on) included. A hit is a beat where the stem jumps up out of
        // what was sounding just before it.
        auto level = [](const Wav &w, int64_t from, int64_t to) {
            double e = 0;
            for (int64_t i = std::max<int64_t>(0, from); i < std::min<int64_t>(to, int64_t(w.l.size())); ++i)
                e += double(w.l[size_t(i)]) * w.l[size_t(i)] + double(w.r[size_t(i)]) * w.r[size_t(i)];
            return std::sqrt(e / double(std::max<int64_t>(1, to - from)));
        };
        for (int k = 0; k < 2; ++k) {
            const Wav &w = stems[size_t(k)];
            const QString name = QLatin1String(names[k]);
            const Smf::Track *track = nullptr;
            for (const auto &t : m.tracks)
                if (t.name == name)
                    track = &t;
            QVERIFY(track);
            const double loud = level(w, 0, int64_t(w.l.size()));
            int hits = 0;
            QStringList missing;
            for (int64_t beat = 0; beat * 24000 < int64_t(w.l.size()) && beat < r.bars * 4; ++beat) {
                const int64_t at = beat * 24000;
                const double before = level(w, at - 2400, at), after = level(w, at, at + 2400);
                if (!(after > 3 * before && after > 0.2 * loud))
                    continue;
                ++hits;
                bool found = false;
                for (const auto &n : track->notes)
                    found |= std::llabs(n.tick - beat * m.ppq) <= 24;
                if (!found)
                    missing << QStringLiteral("bar %1 beat %2").arg(beat / 4 + 1).arg(beat % 4 + 1);
            }
            qInfo().noquote() << name << "hits heard on the beat:" << hits;
            QVERIFY2(hits > r.bars, qPrintable(name));
            QVERIFY2(missing.isEmpty(), qPrintable(name + QStringLiteral(" plays, the MIDI hasn't: ") + missing.join(", ")));
        }
    }

    void aCancelledExportLeavesNothing() {
        const QJsonObject doc = saved(std::string(kLoops) + "end 16\n");
        QTemporaryDir dir;
        ExportOptions o;
        o.folder = dir.path();
        o.song = false;
        o.bars = 64;
        // On a thread of its own, as the window runs it, cancelled from this
        // one once it is a fifth of the way.
        std::atomic<bool> cancel{false};
        std::atomic<double> fraction{0};
        ExportResult r;
        std::thread worker([&] { r = exportSong(doc, o, &cancel, [&](double f, const QString &) { fraction = f; }); });
        while (fraction.load() < 0.2 && worker.joinable()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (fraction.load() >= 1.0)
                break;
        }
        cancel = true;
        worker.join();
        QVERIFY(r.cancelled);
        QVERIFY(!r.ok);
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }
};

TESTKIT_REGISTER(ExportTests)
#include "export_tests.moc"
