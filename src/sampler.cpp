// The Sampler page: a whole song on the pad, chopped on its own beat and
// played in the studio's time.
//
// Drop a sound file on the window, or paste a link to a song (Ctrl+V): the
// page reads it, finds its beat, its bar lines and its key, and lays it out
// in five rows of whole bars, cut where its sounds land. Then:
//
//   a tap         plays that chop, to its end, in time: every beat of the
//                 song lasts one beat of the studio, at the song's own pitch
//   a hold        repeats it for as long as the finger stays
//   a slide       once a finger has held a moment: left and right move
//                 where the chop starts (a tap that skids moves nothing)
//   two fingers   side by side on a chop (or a second set down right beside
//                 the one holding): sliding now moves where it ends
//
// What a slide changed is remembered for that chop (Delete puts it back).
// Space loops what was just played, as on every page.
//
// S separates the song into drums, bass, vocals and the rest (a model run
// beside tenfold, see src/stems.py), and ← → then choose which is played.
// With nothing made yet, the studio takes the song's tempo and key, so it
// plays untouched and every other page is in tune with it; into a track
// already going, the song is stretched to the tempo and moved to the key.
//
// The engine never allocates or waits: a song is published whole (Bank),
// played from memory, and stretched by the Stretcher in sampler.h.

#include "sampler.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <Qt>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "console.h"
#include "dsp.h"
#include "parts.h"
#include "recorder.h"
#include "sampleanalysis.h"
#include "wav.h"

namespace tf {

namespace sampler {

namespace {

// --- the engine -----------------------------------------------------------------

constexpr int kFadeIn = 48;     // a chop begins over a millisecond: its cut is at an attack's foot, not in it
constexpr int kFadeEnd = 192;   // and ends over four
constexpr int kFadeCut = 240;   // cut off by the next chop: five
constexpr int kFadeWrap = 96;   // going round: two
constexpr int kFadeSoft = 1440; // everything let go at once: thirty
constexpr float kVoiceGain = 0.85f;

class Engine : public PartEngine {
public:
    explicit Engine(std::shared_ptr<Bank> bank) : m_bank(std::move(bank)) {}

    void init(float rate) override {
        m_rate = rate;
        m_bank->capture.rate.store(rate, std::memory_order_relaxed);
        for (Voice &v : m_voices)
            v = Voice();
        for (Trimmed &t : m_trimmed)
            t = Trimmed();
        m_lpL = m_lpR = m_hpL = m_hpR = 0;
        m_kLow = 1.0f - std::exp(-kTwoPi * 150.0f / rate);
        m_kTop = 1.0f - std::exp(-kTwoPi * 4000.0f / rate);
        m_release = std::exp(-1.0f / (0.06f * rate));
    }

    void command(const Command &c, AudioContext &ctx) override {
        switch (c.type) {
        case Command::On: start(c, ctx); break;
        case Command::Off:
            for (Voice &v : m_voices)
                if (v.sound && v.id == c.id && v.dying < 0) {
                    // A loop's copy of a chop that was held round: it goes
                    // on round until the next chop of the loop (or this
                    // one again) cuts it, so the loop sounds as the hold
                    // did when Space was pressed, not with a gap where the
                    // finger lifted (or where the loop is longer than the
                    // playing). The page ends it when the loop stops.
                    if (isReplay(v.id) && v.rounds > 0 && c.i[0] != 1) {
                        v.filling = true;
                        continue;
                    }
                    v.held = false;
                    if (c.i[0] == 1)
                        v.dying = v.dyingLen = kFadeCut;
                }
            break;
        case Command::Move:
            for (Voice &v : m_voices)
                if (v.sound && v.id == c.id && v.dying < 0) {
                    // A loop's chop that began trimmed as the slide left
                    // it: the slide replayed is already in it (and what of
                    // it the loop replays may stop short of where it came
                    // to).
                    if (v.asTrimmed)
                        continue;
                    span(c, *v.sound, v.nextStart, v.nextEnd);
                    remember(v.id, v.chop, v.nextStart, v.nextEnd);
                    // The end at once (a pass cut short goes round at
                    // once, one made longer plays on); the start from the
                    // next time round, where it is heard.
                    v.end = std::max(v.nextEnd, v.start + kLeastBeats);
                }
            break;
        case Command::Param:
            if (c.i[0] == cmd::ParamTone)
                m_tone = clampf(c.f[0], -1.0f, 1.0f);
            if (c.i[0] == cmd::ParamLoopStopped)
                for (Voice &v : m_voices)
                    if (v.sound && v.filling && v.dying < 0)
                        v.dying = v.dyingLen = kFadeCut;
            break;
        default: break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        m_bank->epoch.fetch_add(1, std::memory_order_release);
        // RECORD: the microphone into the capture ring, as it arrived.
        Capture &cap = m_bank->capture;
        if (cap.on.load(std::memory_order_acquire)) {
            if (const float *in = ctx.input()) {
                uint64_t w = cap.written.load(std::memory_order_relaxed);
                float pk = cap.peak.load(std::memory_order_relaxed);
                for (int i = 0; i < n; ++i, ++w) {
                    cap.ring[w & (Capture::kSize - 1)] = in[i];
                    pk = std::max(pk, std::fabs(in[i]));
                }
                cap.peak.store(pk, std::memory_order_relaxed);
                cap.written.store(w, std::memory_order_release);
            }
        }
        m_clock += uint64_t(n);
        const Transport &tr = ctx.transport();
        const Harmony &h = ctx.harmony();
        bool any = false;
        for (Voice &v : m_voices)
            if (v.sound) {
                renderVoice(v, l, r, n, tr, h);
                any = true;
            }
        if (!any && m_level < 1e-5f) {
            m_level = 0;
            return;
        }
        // The tone: darker closes a low-pass from the top, brighter opens
        // a high-pass from the bottom, each a gentle 6 dB an octave.
        const float t = m_toneNow += (m_tone - m_toneNow) * 0.02f;
        const float lp = t < 0 ? 1.0f - std::exp(-kTwoPi * 20000.0f * std::exp2(5.5f * t) / m_rate) : 1.0f;
        const float hp = t > 0 ? 1.0f - std::exp(-kTwoPi * 20.0f * std::exp2(5.5f * t) / m_rate) : 0.0f;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            if (lp < 1.0f) {
                m_lpL += (l[i] - m_lpL) * lp;
                m_lpR += (r[i] - m_lpR) * lp;
                l[i] = m_lpL;
                r[i] = m_lpR;
            }
            if (hp > 0.0f) {
                m_hpL += (l[i] - m_hpL) * hp;
                m_hpR += (r[i] - m_hpR) * hp;
                l[i] -= m_hpL;
                r[i] -= m_hpR;
            }
            peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
            // The song's lows, middle and highs, for the visualizer to hear
            // its kick, snare and hats in (whatever the chops are of).
            const float x = 0.5f * (l[i] + r[i]);
            m_bandLow += (x - m_bandLow) * m_kLow;
            m_bandLow2 += (m_bandLow - m_bandLow2) * m_kLow;
            m_bandTop += (x - m_bandTop) * m_kTop;
            const float lo = std::fabs(m_bandLow2), hi = std::fabs(x - m_bandTop), mid = std::fabs(m_bandTop - m_bandLow2);
            m_envLow = std::max(lo, m_envLow * m_release);
            m_envMid = std::max(mid, m_envMid * m_release);
            m_envHigh = std::max(hi, m_envHigh * m_release);
        }
        m_level = std::max(peak, m_level * 0.97f);
    }

    void silence(bool hard) override {
        for (Voice &v : m_voices) {
            if (!v.sound)
                continue;
            if (hard) {
                free(v);
            } else if (v.dying < 0) {
                v.held = false;
                v.dying = v.dyingLen = kFadeSoft;
            }
        }
    }

    void publish(float *data) override {
        data[pub::Voices] = float(voices());
        data[pub::Level] = m_level;
        data[pub::Low] = m_envLow;
        data[pub::Mid] = m_envMid;
        data[pub::High] = m_envHigh;
        for (int i = 0; i < kVoices; ++i) {
            float *d = data + pub::First + i * pub::Per;
            const Voice &v = m_voices[i];
            const bool on = v.sound && v.dying < 0;
            d[pub::Handle] = on ? float(v.handle) : 0.0f;
            d[pub::StemOf] = float(v.stem);
            d[pub::Start] = float(v.start);
            d[pub::End] = float(v.end);
            d[pub::Pos] = float(v.pos);
            d[pub::Flags] = float((v.held ? 1 : 0) | (isReplay(v.id) ? 2 : 0));
        }
    }

    int voices() const override {
        int n = 0;
        for (const Voice &v : m_voices)
            n += v.sound != nullptr;
        return n;
    }

private:
    static constexpr double kLeastBeats = 1.0 / 64; // the shortest a chop can be trimmed to

    struct Voice {
        const Sound *sound = nullptr; // null: free
        const Pcm *pcm = nullptr;
        int id = 0, handle = 0, stem = 0;
        int group = 0;       // 0 the hands, else the loop and layer it is replayed from
        int chop = 0;        // which chop (the On's i[0])
        uint64_t since = 0;  // the engine's sample count when it began
        double start = 0, end = 0;         // the pass playing, in the song's beats
        double nextStart = 0, nextEnd = 0; // the next time round
        double pos = 0;
        double fold = 1;     // the song's beats per studio beat
        float tune = 0;      // semitones, the player's own
        bool keyMatch = false;
        bool held = false;   // it goes round at its end
        bool filling = false; // a loop's held chop, let go, going round until the loop's next chop
        bool asTrimmed = false; // a loop's chop begun as it was trimmed: it stays so (see start)
        int rounds = 0;       // times it has gone round
        float level = 1;
        int fadeIn = 0;
        int dying = -1, dyingLen = 1; // frames of a cut's fade left; −1: none
        uint32_t age = 0;
        Stretcher st;
    };

    // Where a chop starts and ends, in beats of the song's grid as it is
    // now (the command says it in samples).
    static void span(const Command &c, const Sound &s, double &start, double &end) {
        start = s.beatOf(double(c.i[2]));
        end = std::max(s.beatOf(double(c.i[3])), start + kLeastBeats);
    }

    // The span each sound (its id and chop: the hand's, and each loop's
    // copy of it) was last trimmed to, the newest few.
    struct Trimmed {
        int id = 0, chop = 0;
        double start = 0, end = 0;
        uint32_t age = 0;
    };
    static constexpr int kTrimmed = 64;
    Trimmed m_trimmed[kTrimmed];
    uint32_t m_trimmedAge = 0;

    const Trimmed *recall(int id, int chop) const {
        for (const Trimmed &t : m_trimmed)
            if (t.age && t.id == id && t.chop == chop)
                return &t;
        return nullptr;
    }
    void remember(int id, int chop, double start, double end) {
        Trimmed *to = &m_trimmed[0];
        for (Trimmed &t : m_trimmed) {
            if (t.age && t.id == id && t.chop == chop) {
                to = &t;
                break;
            }
            if (t.age < to->age)
                to = &t;
        }
        *to = {id, chop, start, end, ++m_trimmedAge};
    }

    void free(Voice &v) {
        if (v.sound)
            v.sound->uses.fetch_sub(1, std::memory_order_release);
        v.sound = nullptr;
        v.pcm = nullptr;
    }

    void start(const Command &c, AudioContext &ctx) {
        const int handle = c.i[1] & 0xff, stem = (c.i[1] >> 8) & 0xff, flags = c.i[1] >> 16;
        if (handle < 1 || handle > kSounds || stem < 0 || stem >= kStems)
            return;
        const Sound *s = m_bank->sounds[handle].load(std::memory_order_acquire);
        if (!s || s->count() < 2)
            return; // the song isn't here (yet): the chop is silent
        const Pcm *pcm = s->pcm[stem].load(std::memory_order_acquire);
        if (!pcm)
            pcm = s->pcm[Mix].load(std::memory_order_acquire);
        if (!pcm)
            return;
        if (flags & cmd::FlagRound) {
            for (Voice &v : m_voices)
                if (v.sound == s && v.id == c.id && v.dying < 0) {
                    // Already going round (it wraps by itself, to the
                    // sample): only what it plays next time may change. One
                    // let go of stays let go: a round sent just before the
                    // finger lifted doesn't hold it again.
                    if (v.held && !v.asTrimmed)
                        span(c, *s, v.nextStart, v.nextEnd);
                    v.rounds = std::max(v.rounds, 1);
                    return;
                }
            // Nothing playing it: a loop that starts in the middle of a
            // hold starts there; the hand's own chop is gone because it was
            // cut, and stays gone.
            if (!isReplay(c.id))
                return;
        }
        // The hands and each loop's layers are separate players: a chop
        // cuts off what its own player let go, not what a loop plays.
        const int group = isReplay(c.id) ? 1 + ((c.id >> 24) & 0x1f) : 0;
        // The hand and a loop on the same chop at the same moment (the hand
        // still going as Space lands its loop, or playing along with it):
        // one chop, not two a few milliseconds apart. The hand wins, as on
        // the drums: its own press is what the player hears himself play.
        const uint64_t near = uint64_t(kDoubleSecs * m_rate);
        for (Voice &v : m_voices) {
            if (!v.sound || v.dying >= 0 || v.chop != c.i[0] || (v.group == 0) == (group == 0) || m_clock - v.since > near)
                continue;
            if (group != 0)
                return; // the loop's copy, just after the hand's: left out
            v.dying = v.dyingLen = kFadeCut; // the hand's, just after the loop's: it takes over
        }
        for (Voice &v : m_voices) {
            if (!v.sound || v.dying >= 0 || v.group != group)
                continue;
            if (v.id == c.id || ((flags & cmd::FlagCut) && (!v.held || v.filling)))
                v.dying = v.dyingLen = kFadeCut;
        }
        Voice *v = nullptr;
        for (Voice &q : m_voices)
            if (!q.sound) {
                v = &q;
                break;
            }
        if (!v) {
            // None free: the one nearest its end goes (one already being
            // cut off, then one let go, then the oldest).
            auto rank = [](const Voice &q) { return (q.dying >= 0 ? 0 : q.held ? 2 : 1); };
            for (Voice &q : m_voices)
                if (!v || rank(q) < rank(*v) || (rank(q) == rank(*v) && q.age < v->age))
                    v = &q;
            free(*v);
        }
        s->uses.fetch_add(1, std::memory_order_release);
        v->sound = s;
        v->pcm = pcm;
        v->id = c.id;
        v->chop = c.i[0];
        v->since = m_clock;
        v->handle = handle;
        v->stem = stem;
        v->group = group;
        span(c, *s, v->start, v->end);
        // A loop's chop that was trimmed while it was held: the On the loop
        // took is from where the finger landed, before the slide, and the
        // slide's Moves after it would make each pass of the loop the
        // whole chop again (or the slide over again). What was looped is
        // the chop as it was trimmed, so the loop plays that, every pass,
        // from the start: as its own Moves last left it, or the first time
        // round, as the hand's (the same id) were.
        v->asTrimmed = false;
        if (isReplay(c.id) && !(flags & cmd::FlagRound)) {
            const Trimmed *t = recall(c.id, c.i[0]);
            if (!t)
                t = recall(c.id & 0xFFFFFF, c.i[0]);
            if (t) {
                const double a = t->start, b = t->end;
                v->start = a;
                v->end = b;
                v->asTrimmed = true;
                remember(c.id, c.i[0], a, b);
            }
        }
        v->nextStart = v->start;
        v->nextEnd = v->end;
        v->pos = v->start;
        // A song at twice the studio's tempo plays two of its beats to each
        // of the studio's (folded to near its own speed, not slowed to half).
        v->fold = 1.0 / foldFor(s->bpm, ctx.transport().bpm);
        v->tune = clampf(c.f[3], -24.0f, 24.0f);
        v->keyMatch = (flags & cmd::FlagKeyMatch) != 0;
        v->held = true; // until its Off: a tap's comes at once, and the chop plays out
        v->filling = false;
        v->rounds = (flags & cmd::FlagRound) ? 1 : 0;
        v->level = clampf(c.f[0], 0.0f, 1.5f) * kVoiceGain;
        v->fadeIn = 0;
        v->dying = -1;
        v->age = ++m_age;
        v->st.start(pcm, s->at(v->pos));
    }

    void renderVoice(Voice &v, float *l, float *r, int n, const Transport &tr, const Harmony &h) {
        const Sound &s = *v.sound;
        // The pitch: the player's semitones, and the way to the studio's
        // key (with it, a song tuned off concert pitch is put on it too:
        // it is being retuned anyway).
        double semis = v.tune;
        if (v.keyMatch && s.keyKnown) {
            semis += keyShift(h.tuning, s);
            if (std::fabs(s.cents) > 12)
                semis -= s.cents / 100.0;
        }
        const double rate = std::exp2(semis / 12.0) * s.rate / double(m_rate);
        int done = 0, rounds = 0;
        while (done < n && v.sound) {
            const double per = s.perBeat(v.pos);
            // The song's beats per frame: the studio's clock, or with no
            // beat found in it, the song as it is.
            const double beats = s.matched ? tr.beatsPerSample * v.fold : rate / per;
            const double left = (v.end - v.pos) / beats; // frames of this pass
            int m = n - done;
            const bool ends = left <= double(m);
            if (ends)
                m = std::clamp(int(std::ceil(left - 1e-6)), 0, m);
            if (m > 0) {
                float a[kBlock], b[kBlock];
                v.st.render(a, b, m, s.at(v.pos), per * beats, rate);
                for (int i = 0; i < m; ++i) {
                    float g = v.level;
                    if (v.fadeIn < kFadeIn)
                        g *= float(++v.fadeIn) / float(kFadeIn);
                    if (!v.held && left - i < kFadeEnd)
                        g *= float(std::max(0.0, (left - i) / kFadeEnd));
                    if (v.dying >= 0) {
                        g *= float(v.dying) / float(v.dyingLen);
                        if (v.dying > 0)
                            --v.dying;
                    }
                    l[done + i] += a[i] * g;
                    r[done + i] += b[i] * g;
                }
                v.pos += m * beats;
                done += m;
            }
            if (v.dying == 0) {
                free(v);
                return;
            }
            if (!ends)
                continue;
            ++v.rounds;
            if (!v.held || ++rounds > 4) {
                free(v);
                return;
            }
            // Round again, from wherever the start has been slid to.
            v.start = v.nextStart;
            v.end = std::max(v.nextEnd, v.start + kLeastBeats);
            v.pos = v.start;
            v.st.jump(s.at(v.pos), kFadeWrap);
        }
    }

    std::shared_ptr<Bank> m_bank;
    float m_rate = 48000;
    Voice m_voices[kVoices];
    uint32_t m_age = 0;
    uint64_t m_clock = 0; // samples rendered
    static constexpr double kDoubleSecs = 0.12;
    float m_tone = 0, m_toneNow = 0;
    float m_lpL = 0, m_lpR = 0, m_hpL = 0, m_hpR = 0;
    float m_level = 0;
    // Band followers (see render): one-pole splits at about 150 Hz and
    // 4 kHz, and envelopes that fall 60 ms.
    float m_bandLow = 0, m_bandLow2 = 0, m_bandTop = 0;
    float m_envLow = 0, m_envMid = 0, m_envHigh = 0;
    float m_kLow = 0.02f, m_kTop = 0.4f, m_release = 0.9997f;
};

// --- reading a song -------------------------------------------------------------

// What a song file remembers of a song, to put it back as it was.
struct Restore {
    bool on = false;
    int handle = 0;
    int size = Auto, page = 0, barShift = 0, stem = Mix;
    float tune = 0;
    std::vector<double> beats; // the grid its loops were made on
    int downbeat = -1;
    std::vector<std::array<double, 4>> trims; // at, length, start, end
    QString name;
};

// A job's result, handed back to the page.
struct Loaded {
    enum Kind { Song, Stems, Failed } kind = Failed;
    QString error;
    // A song:
    std::unique_ptr<Sound> sound;
    Marks marks;
    QString name, source, file, cache;
    bool truncated = false;
    Restore restore;
    // Stems, for the song with this serial:
    int serial = 0;
    std::unique_ptr<Pcm> stems[kStems];
};

QString findTool(const char *env, const QString &name) {
    const QString given = qEnvironmentVariable(env);
    if (!given.isEmpty())
        return QFileInfo(given).isExecutable() ? given : QString();
    QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty())
        return found;
    // An app started from the Finder or a launcher doesn't get the shell's PATH.
    return QStandardPaths::findExecutable(
        name, {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/opt/local/bin"),
               QDir::homePath() + QStringLiteral("/.local/bin"), QDir::homePath() + QStringLiteral("/.cargo/bin")});
}

// Where downloads, stems and the separator's script are kept.
QString cacheRoot() {
    const QString env = qEnvironmentVariable("TENFOLD_SAMPLER_CACHE");
    if (!env.isEmpty())
        return env;
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/tenfold/sampler");
}

bool isLink(const QString &what) {
    return what.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
           what.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
}

// A song's own folder in the cache: by what it is (a link; a file as it is
// now: its path, size and date), so the same song finds its download and
// its stems again, and an edited file doesn't find another's.
QString cacheFor(const QString &source) {
    QByteArray id = source.toUtf8();
    if (!isLink(source)) {
        const QFileInfo fi(source);
        id = fi.absoluteFilePath().toUtf8() + '|' + QByteArray::number(fi.size()) + '|' +
             QByteArray::number(fi.lastModified().toSecsSinceEpoch());
    }
    return cacheRoot() + QLatin1Char('/') +
           QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(16));
}

// The work that takes time, on a thread of its own: fetching a link,
// decoding, finding the beat, separating stems. One job at a time, in
// order; the page takes the results as they come.
class Importer {
public:
    ~Importer() {
        {
            std::lock_guard lock(m_mutex);
            m_quit = true;
            m_cancel = true;
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    // A file or a link, read into a song. wait: done before this returns.
    void load(const QString &what, const Restore &restore, bool wait) {
        auto job = [this, what, restore] { push(read(what, restore)); };
        run(job, wait);
    }
    // The stems of a song: taken from its cache if they are there, else
    // made by the separator from `mix` (which may be empty: then only the
    // cache is tried).
    void separate(int serial, const QString &cache, std::vector<int16_t> mix, int rate, bool wait) {
        auto data = std::make_shared<std::vector<int16_t>>(std::move(mix));
        auto job = [this, serial, cache, data, rate] { push(stems(serial, cache, *data, rate)); };
        run(job, wait);
    }
    bool take(Loaded &out) {
        std::lock_guard lock(m_mutex);
        if (m_done.empty())
            return false;
        out = std::move(m_done.front());
        m_done.pop_front();
        return true;
    }
    bool busy() const {
        std::lock_guard lock(m_mutex);
        return m_working || !m_jobs.empty();
    }
    // What it is doing, for the page to show: a few words, and how far.
    QString stage() const {
        std::lock_guard lock(m_mutex);
        return m_stage;
    }
    float progress() const { return m_progress.load(); }
    bool separating() const { return m_separating.load(); }
    void cancel() { m_cancel = true; }

private:
    void run(std::function<void()> job, bool wait) {
        if (wait) {
            m_cancel = false;
            job();
            return;
        }
        std::lock_guard lock(m_mutex);
        m_jobs.push_back(std::move(job));
        if (!m_thread.joinable())
            m_thread = std::thread([this] { loop(); });
        m_wake.notify_all();
    }
    void loop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
                if (m_quit)
                    return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
                m_working = true;
                m_cancel = false;
            }
            job();
            std::lock_guard lock(m_mutex);
            m_working = false;
            m_stage.clear();
            m_progress = 0;
            m_separating = false;
        }
    }
    void push(Loaded &&l) {
        std::lock_guard lock(m_mutex);
        m_done.push_back(std::move(l));
    }
    void say(const QString &stage, float progress = 0) {
        std::lock_guard lock(m_mutex);
        m_stage = stage;
        m_progress = progress;
    }
    static Loaded failed(const QString &why) {
        Loaded l;
        l.kind = Loaded::Failed;
        l.error = why;
        return l;
    }

    // Runs a command to its end, handing each line it prints to `line`.
    // False if it couldn't start, failed, or was called off.
    bool process(const QString &program, const QStringList &args, const std::function<void(const QString &)> &line,
                 QString *errors, QByteArray *raw = nullptr, qint64 rawMost = 0) {
        QProcess p;
        p.setProcessChannelMode(QProcess::SeparateChannels);
        p.start(program, args);
        if (!p.waitForStarted(10000))
            return false;
        p.closeWriteChannel();
        QByteArray pending;
        auto drain = [&] {
            if (raw) {
                // A decoder's samples: kept as they are, up to the most
                // the page takes (the rest is read and dropped, so the
                // decoder ends by itself).
                const QByteArray got = p.readAllStandardOutput();
                if (raw->size() < rawMost)
                    raw->append(got.left(rawMost - raw->size()));
            } else {
                pending += p.readAllStandardOutput();
                // Progress comes with carriage returns as often as new lines.
                pending.replace('\r', '\n');
                for (qsizetype nl; (nl = pending.indexOf('\n')) >= 0; pending.remove(0, nl + 1))
                    if (line && nl > 0)
                        line(QString::fromUtf8(pending.left(nl)).trimmed());
            }
            if (errors)
                *errors += QString::fromUtf8(p.readAllStandardError());
        };
        while (p.state() != QProcess::NotRunning) {
            p.waitForReadyRead(100);
            drain();
            if (m_cancel) {
                p.terminate();
                if (!p.waitForFinished(3000))
                    p.kill();
                p.waitForFinished(1000);
                return false;
            }
        }
        drain();
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    }
    // The last thing a tool said that reads as a reason.
    static QString lastLine(const QString &errors) {
        const QStringList lines = errors.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            QString l = it->trimmed();
            if (l.startsWith(QLatin1String("ERROR:")))
                l = l.mid(6).trimmed();
            if (!l.isEmpty())
                return l.left(160);
        }
        return {};
    }

    // A link: the song's audio into its cache folder, with yt-dlp. Its
    // title comes back in `name`.
    bool fetch(const QString &link, const QString &cache, QString &file, QString &name, QString &why) {
        QDir().mkpath(cache);
        const QDir dir(cache);
        const QStringList have = dir.entryList({QStringLiteral("source.*")}, QDir::Files);
        if (!have.isEmpty() && QFile::exists(cache + QStringLiteral("/title.txt"))) {
            // Fetched before.
            file = dir.filePath(have.first());
            QFile t(cache + QStringLiteral("/title.txt"));
            if (t.open(QIODevice::ReadOnly))
                name = QString::fromUtf8(t.readAll()).trimmed();
            return true;
        }
        const QString tool = findTool("TENFOLD_YTDLP", QStringLiteral("yt-dlp"));
        if (tool.isEmpty()) {
            why = QStringLiteral("A link needs yt-dlp to fetch it, and there is none here (TENFOLD_YTDLP names one)");
            return false;
        }
        say(QStringLiteral("FETCHING"));
        QString errors, title, path;
        const bool ok = process(
            tool,
            {QStringLiteral("--no-playlist"), QStringLiteral("--no-quiet"), QStringLiteral("--newline"),
             QStringLiteral("--no-warnings"), QStringLiteral("-f"), QStringLiteral("bestaudio/best"),
             QStringLiteral("--print"), QStringLiteral("before_dl:TENFOLD-TITLE %(title)s"), QStringLiteral("--print"),
             QStringLiteral("after_move:TENFOLD-FILE %(filepath)s"), QStringLiteral("-o"),
             cache + QStringLiteral("/source.%(ext)s"), QStringLiteral("--"), link},
            [&](const QString &l) {
                if (l.startsWith(QLatin1String("TENFOLD-TITLE "))) {
                    title = l.mid(14).trimmed();
                } else if (l.startsWith(QLatin1String("TENFOLD-FILE "))) {
                    path = l.mid(13).trimmed();
                } else if (l.startsWith(QLatin1String("[download]"))) {
                    const qsizetype pct = l.indexOf(QLatin1Char('%'));
                    if (pct > 10)
                        say(QStringLiteral("FETCHING"), float(l.mid(10, pct - 10).trimmed().toDouble() / 100.0));
                }
            },
            &errors);
        if (!ok || path.isEmpty() || !QFile::exists(path)) {
            const QString said = lastLine(errors);
            why = m_cancel ? QStringLiteral("Called off")
                           : said.isEmpty() ? QStringLiteral("The link gave no sound") : QStringLiteral("The link gave no sound: ") + said;
            return false;
        }
        file = path;
        name = title;
        QFile t(cache + QStringLiteral("/title.txt"));
        if (t.open(QIODevice::WriteOnly))
            t.write(title.toUtf8());
        return true;
    }

    // Any sound file as 48 kHz 16-bit stereo: a WAV at that rate read as
    // it is, anything else through ffmpeg.
    bool decode(const QString &file, std::vector<int16_t> &pcm, bool &truncated, QString &why) {
        say(QStringLiteral("READING"));
        constexpr int kRate = 48000;
        const size_t most = size_t(kMaxSeconds * kRate) * 2;
        int rate = 0;
        std::vector<int16_t> wav;
        const bool isWav = readWavStereo(QFile::encodeName(file).toStdString(), wav, rate, kMaxSeconds + 1);
        if (isWav && rate == kRate) {
            pcm = std::move(wav);
        } else {
            const QString ffmpeg = video::findFfmpeg();
            if (ffmpeg.isEmpty()) {
                if (!isWav) {
                    why = QStringLiteral("Reading that needs ffmpeg, and there is none here (a WAV file opens without)");
                    return false;
                }
                pcm = resampleStereo(wav, rate, kRate);
            } else {
                QByteArray raw;
                QString errors;
                const bool ok = process(ffmpeg,
                                        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                                         QStringLiteral("-i"), file, QStringLiteral("-vn"), QStringLiteral("-ac"),
                                         QStringLiteral("2"), QStringLiteral("-ar"), QString::number(kRate),
                                         QStringLiteral("-f"), QStringLiteral("s16le"), QStringLiteral("-")},
                                        {}, &errors, &raw, qint64(most + 2) * 2);
                if (!ok && raw.size() < 4 * kRate / 10) {
                    const QString said = lastLine(errors);
                    why = m_cancel ? QStringLiteral("Called off")
                                   : said.isEmpty() ? QStringLiteral("That isn't a sound file tenfold can read")
                                                    : QStringLiteral("Not read: ") + said;
                    return false;
                }
                pcm.resize(size_t(raw.size()) / 2);
                std::memcpy(pcm.data(), raw.constData(), pcm.size() * 2);
                pcm.resize(pcm.size() / 2 * 2);
            }
        }
        truncated = pcm.size() > most;
        if (truncated)
            pcm.resize(most);
        if (pcm.size() < size_t(kRate / 10) * 2) {
            why = QStringLiteral("There is no sound in that file");
            return false;
        }
        return true;
    }

    Loaded read(const QString &what, const Restore &restore) {
        Loaded out;
        out.restore = restore;
        out.source = what;
        out.cache = cacheFor(what);
        QString why;
        if (isLink(what)) {
            if (!fetch(what, out.cache, out.file, out.name, why))
                return failed(why);
        } else {
            out.file = what;
            out.name = QFileInfo(what).completeBaseName();
            if (!QFileInfo(what).isFile())
                return failed(QStringLiteral("There is no file at %1").arg(what));
        }
        if (out.name.isEmpty())
            out.name = QStringLiteral("song");
        auto sound = std::make_unique<Sound>();
        auto pcm = std::make_unique<Pcm>();
        if (!decode(out.file, pcm->data, out.truncated, why))
            return failed(why);
        if (m_cancel)
            return failed(QStringLiteral("Called off"));
        say(QStringLiteral("FINDING THE BEAT"));
        pcm->prepare();
        sound->rate = 48000;
        sound->frames = pcm->frames;
        // The analysis hears the song in mono.
        std::vector<float> mono(size_t(pcm->frames));
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = (float(pcm->data[2 * i]) + float(pcm->data[2 * i + 1])) * (0.5f / 32768.0f);
        const Analysis a = analyze(mono.data(), mono.size(), sound->rate,
                                   [this](float p) { say(QStringLiteral("FINDING THE BEAT"), p); }, &m_cancel);
        if (m_cancel)
            return failed(QStringLiteral("Called off"));
        sound->beats = a.beats;
        if (sound->beats.size() < 2) {
            // Nothing to go by: half-second beats from the top.
            sound->beats.clear();
            for (double at = 0; at <= double(sound->frames) + 24000; at += 24000)
                sound->beats.push_back(at);
            if (sound->beats.size() < 2)
                sound->beats.push_back(sound->beats.back() + 24000);
        }
        sound->matched = a.ok;
        sound->bpm = a.ok && a.bpm > 0 ? a.bpm : 120;
        sound->keyRoot = ((a.keyRoot % 12) + 12) % 12;
        sound->keyMinor = a.keyMinor;
        sound->keyKnown = a.ok && a.keyConfidence >= 0.3f;
        sound->cents = std::clamp(a.tuningCents, -50.0, 50.0);
        out.marks.downbeat = std::clamp(a.downbeat, 0, 3);
        out.marks.strength = a.strength;
        out.marks.attack = a.attack;
        out.marks.change = a.change;
        sound->own[Mix] = std::move(pcm);
        sound->pcm[Mix].store(sound->own[Mix].get(), std::memory_order_release);
        out.marks.sub = a.sub;
        out.sound = std::move(sound);
        out.kind = Loaded::Song;
        return out;
    }

    Loaded stems(int serial, const QString &cache, const std::vector<int16_t> &mix, int rate) {
        Loaded out;
        out.serial = serial;
        const QString dir = cache + QStringLiteral("/stems");
        auto there = [&] {
            for (int s = Drums; s < kStems; ++s)
                if (!QFile::exists(dir + QLatin1Char('/') + QLatin1String(stemFile(s)) + QStringLiteral(".wav")))
                    return false;
            return true;
        };
        if (!there()) {
            if (mix.empty())
                return failed(QStringLiteral("The stems aren't made yet"));
            const QString uv = findTool("TENFOLD_UV", QStringLiteral("uv"));
            if (uv.isEmpty())
                return failed(QStringLiteral("Stems need uv to run the separator (astral.sh/uv), and there is none here"));
            m_separating = true;
            say(QStringLiteral("STEMS: GETTING READY"));
            QDir().mkpath(dir);
            // The separator's script travels inside tenfold; it runs from
            // the cache, in an environment uv makes for it the first time.
            const QString script = cacheRoot() + QStringLiteral("/stems.py");
            QFile::remove(script);
            if (!QFile::copy(QStringLiteral(":/stems.py"), script))
                return failed(QStringLiteral("The separator's script isn't in this tenfold"));
            QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner);
            const QString mixPath = cache + QStringLiteral("/mix.wav");
            if (!writeWavStereo(QFile::encodeName(mixPath).toStdString(), mix.data(), mix.size() / 2, rate))
                return failed(QStringLiteral("No room to write the song for the separator in %1").arg(cache));
            QString errors;
            const bool ok = process(
                uv,
                {QStringLiteral("run"), QStringLiteral("--script"), script, QStringLiteral("--in"), mixPath,
                 QStringLiteral("--out"), dir},
                [&](const QString &l) {
                    if (l.startsWith(QLatin1String("STAGE ")))
                        say(QStringLiteral("STEMS: ") + l.mid(6).trimmed().toUpper(), m_progress.load());
                    else if (l.startsWith(QLatin1String("PROGRESS ")))
                        say(stageNow(), float(std::clamp(l.mid(9).trimmed().toDouble(), 0.0, 1.0)));
                },
                &errors);
            QFile::remove(mixPath);
            if (!ok || !there()) {
                const QString said = lastLine(errors);
                return failed(m_cancel ? QStringLiteral("Stems called off")
                                       : said.isEmpty() ? QStringLiteral("The stems couldn't be made")
                                                        : QStringLiteral("No stems: ") + said);
            }
        }
        say(QStringLiteral("STEMS: READING"), 1.0f);
        for (int s = Drums; s < kStems; ++s) {
            auto pcm = std::make_unique<Pcm>();
            int r = 0;
            const QString path = dir + QLatin1Char('/') + QLatin1String(stemFile(s)) + QStringLiteral(".wav");
            if (!readWavStereo(QFile::encodeName(path).toStdString(), pcm->data, r, kMaxSeconds + 1) || r != rate)
                return failed(QStringLiteral("The %1 stem can't be read").arg(QLatin1String(stemFile(s))));
            pcm->prepare();
            out.stems[s] = std::move(pcm);
        }
        out.kind = Loaded::Stems;
        return out;
    }
    QString stageNow() const {
        std::lock_guard lock(m_mutex);
        return m_stage;
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    std::deque<std::function<void()>> m_jobs;
    std::deque<Loaded> m_done;
    bool m_quit = false, m_working = false;
    QString m_stage;
    std::atomic<float> m_progress{0};
    std::atomic<bool> m_cancel{false}, m_separating{false};
};

// --- the page -------------------------------------------------------------------

constexpr float kTrimBeats = 2.0f;   // a slide across the whole pad moves a chop's edge this many beats
// Moving a chop's start or end is a deliberate act, never a tap gone
// astray: the finger must have rested on the chop this long before its
// sliding counts, and then move this far (about 4 mm) from where it rested.
// A finger that drums and skids does neither.
constexpr double kTrimHold = 0.4;
constexpr double kSecondHold = 0.2;  // the second finger, beside one already holding
constexpr float kTrimDead = 0.03f;
// The second finger of a two-finger hold lands right beside the first, as
// two fingers of one hand do (about 2 cm), either with it or once it has
// held long enough to slide. Any other finger is its own chop: a hand
// playing chops around one that is held is never taken for its partner.
constexpr double kTogether = 0.08;
constexpr float kSecondX = 0.16f, kSecondY = 0.22f;
constexpr float kToneStep = 0.125f;

const char *noteName(int pc) {
    static const char *names[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return names[((pc % 12) + 12) % 12];
}

class Page : public PartControl {
public:
    explicit Page(std::shared_ptr<Bank> bank) : m_bank(std::move(bank)) {}
    ~Page() override {
        // The engine may outlive the page: it must find no song.
        for (int h = 1; h <= kSounds; ++h)
            m_bank->sounds[h].store(nullptr, std::memory_order_release);
    }

    // --- touch ---------------------------------------------------------------

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        // Lifts first.
        m_spent.erase(std::remove_if(m_spent.begin(), m_spent.end(), [&](int id) { return !touching(f, id); }), m_spent.end());
        for (Holder &h : m_holders) {
            if (!h.used)
                continue;
            if (h.second >= 0 && !touching(f, h.second)) {
                // Back to one finger: the slide is the start's again,
                // from where the finger is now.
                h.second = -1;
                h.moved = false;
                h.x0 = h.x;
                h.base = trimOf(h).start;
            }
            if (!touching(f, h.src))
                release(h, f.t, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            if (Holder *h = holderOf(t.id)) {
                h->x = t.x;
                h->y = t.y;
                slide(*h, f, ctx);
            } else if (!secondOf(t.id) && std::find(m_spent.begin(), m_spent.end(), t.id) == m_spent.end()) {
                land(t, f.t, ctx);
                // A finger that landed on nothing has done what it does
                // until it lifts.
                if (!holderOf(t.id) && !secondOf(t.id))
                    m_spent.push_back(t.id);
            }
        }
        for (Holder &h : m_holders)
            if (h.used && h.second >= 0)
                slide(h, f, ctx);
    }

    void background(double t, ControlContext &ctx) override {
        sendRounds(t, ctx);
        // The loop stopped, changed or was taken back: its held chops
        // going round on their own stop too.
        const PartView &pv = ctx.studio().parts[ctx.part()];
        if (pv.playing != m_loopPlaying || pv.takes != m_loopTakes) {
            if (m_loopPlaying >= 0) {
                Command c;
                c.type = Command::Param;
                c.flags = Command::NoRecord;
                c.i[0] = cmd::ParamLoopStopped;
                ctx.send(c);
            }
            m_loopPlaying = pv.playing;
            m_loopTakes = pv.takes;
        }
        if (m_recording) {
            drainTake();
            if (m_take.size() >= size_t(kMaxSeconds * m_bank->capture.rate.load(std::memory_order_relaxed)))
                finishTake(ctx); // as long as a song may be
        }
        Loaded done;
        while (m_importer.take(done))
            arrived(std::move(done), ctx);
        // Songs taken away are freed once the engine has let go of them:
        // no voice plays one, and it has rendered since (or isn't running).
        const uint32_t epoch = m_bank->epoch.load(std::memory_order_acquire);
        for (auto it = m_retired.begin(); it != m_retired.end();) {
            const bool rendered = epoch - it->epoch >= 2 || t - it->at > 2.0;
            if (rendered && it->sound->uses.load(std::memory_order_acquire) == 0)
                it = m_retired.erase(it);
            else
                ++it;
        }
    }

    // A chop is a phrase: one held round is a pattern, and one cycle of
    // what was played is the loop (see PartControl::loopsOneCycle).
    bool loopsOneCycle() const override { return true; }

    bool dropped(const QString &what, ControlContext &ctx) override {
        const QString given = what.trimmed();
        if (!isLink(given) && !QFileInfo(given).isFile()) {
            ctx.notice(QStringLiteral("Not a sound file or a link to a song: %1").arg(given.left(60)));
            return true;
        }
        // A song already here is shown, not read twice.
        for (int h = 1; h <= kSounds; ++h)
            if (m_songs[h].sound && m_songs[h].source == given) {
                show(h, ctx);
                return true;
            }
        // A song this page is missing (a song file opened without it)
        // comes back to its place, with its loops and trims.
        Restore restore;
        for (int h = 1; h <= kSounds; ++h)
            if (m_songs[h].used && !m_songs[h].sound &&
                (m_songs[h].source == given || m_songs[h].name == QFileInfo(given).completeBaseName())) {
                restore = m_songs[h].missing;
                restore.on = true;
                restore.handle = h;
            }
        m_importer.load(given, restore, ctx.scripted());
        if (!ctx.scripted())
            ctx.notice(isLink(given) ? QStringLiteral("Fetching the song…") : QStringLiteral("Reading %1…").arg(QFileInfo(given).fileName()));
        background(ctx.now(), ctx);
        return true;
    }

    // --- keys ----------------------------------------------------------------

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (k.modifiers & (Qt::AltModifier | Qt::MetaModifier))
            return false;
        switch (k.key) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Minus:
        case Qt::Key_Plus:
        case Qt::Key_Equal:
        case Qt::Key_Comma:
        case Qt::Key_Period:
            break; // these repeat while held
        default:
            if (k.repeat)
                return ownKey(k.key);
        }
        if (!k.down)
            return ownKey(k.key);
        switch (k.key) {
        case Qt::Key_Left:
            if (shift)
                resize(-1, ctx);
            else
                stem(-1, ctx);
            return true;
        case Qt::Key_Right:
            if (shift)
                resize(1, ctx);
            else
                stem(1, ctx);
            return true;
        case Qt::Key_BracketLeft: resize(-1, ctx); return true;
        case Qt::Key_BracketRight: resize(1, ctx); return true;
        case Qt::Key_Up:
        case Qt::Key_PageUp: turn(-1, ctx); return true;
        case Qt::Key_Down:
        case Qt::Key_PageDown: turn(1, ctx); return true;
        case Qt::Key_Minus: tone(-1, ctx); return true;
        case Qt::Key_Plus:
        case Qt::Key_Equal: tone(1, ctx); return true;
        case Qt::Key_Comma: tune(-1, ctx); return true;
        case Qt::Key_Period: tune(1, ctx); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Only Shift+Return: a chop goes on playing by itself only once
            // Space has made it a loop, which Ctrl+Z can take back.
            if (!shift)
                return false;
            toggleOverdub(ctx);
            return true;
        case Qt::Key_section:
        case Qt::Key_QuoteLeft:
        case Qt::Key_Backslash:
        case Qt::Key_AsciiTilde:
        case Qt::Key_onehalf: toggleKeyMatch(ctx); return true;
        case Qt::Key_Delete:
            if (shift)
                resetAll(ctx);
            else
                reset(ctx);
            return true;
        case Qt::Key_S: separate(ctx); return true;
        case Qt::Key_T: giveTempo(ctx, true); return true;
        case Qt::Key_H: regrid(0.5, ctx); return true;
        case Qt::Key_D: regrid(2.0, ctx); return true;
        case Qt::Key_B: shiftBar(ctx); return true;
        case Qt::Key_Q: show(1, ctx); return true;
        case Qt::Key_W: show(2, ctx); return true;
        case Qt::Key_E: show(3, ctx); return true;
        case Qt::Key_R: show(4, ctx); return true;
        default: return false;
        }
    }

    void enter(ControlContext &ctx) override { background(ctx.now(), ctx); }

    void leave(ControlContext &ctx) override {
        for (Holder &h : m_holders)
            if (h.used)
                release(h, ctx.now(), ctx);
        m_frame = Frame();
        m_spent.clear();
    }

    void startOver(ControlContext &ctx) override {
        m_importer.cancel();
        for (Holder &h : m_holders)
            if (h.used)
                off(h.id, true, ctx.now(), ctx);
        for (Holder &h : m_holders)
            h = Holder();
        for (int h = 1; h <= kSounds; ++h)
            unload(h, ctx);
        m_current = 0;
        m_keyMatch = true;
        m_tone = 0;
        sendTone(ctx);
        m_overdub = false;
    }

    // --- songs ---------------------------------------------------------------

    void save(QJsonObject &out) const override {
        out[QStringLiteral("keyMatch")] = m_keyMatch;
        out[QStringLiteral("tone")] = double(m_tone);
        out[QStringLiteral("showing")] = m_current;
        QJsonArray songs;
        for (int h = 1; h <= kSounds; ++h) {
            const Song &s = m_songs[h];
            if (!s.used)
                continue;
            QJsonObject o;
            o[QStringLiteral("handle")] = h;
            o[QStringLiteral("name")] = s.name;
            o[QStringLiteral("source")] = s.source;
            const Restore now = remember(s);
            o[QStringLiteral("chop")] = QString::fromLatin1(sizeName(now.size)).toLower();
            o[QStringLiteral("page")] = now.page;
            o[QStringLiteral("barShift")] = now.barShift;
            o[QStringLiteral("stem")] = QString::fromLatin1(stemName(now.stem)).toLower();
            o[QStringLiteral("tune")] = double(now.tune);
            // The grid the loops were made on, so they stay where they
            // were whatever a later tenfold makes of the song's beat: a
            // steady one as where it starts and how long a beat is.
            QJsonObject grid;
            const std::vector<double> &b = now.beats;
            bool even = b.size() >= 2;
            const double step = even ? (b.back() - b.front()) / double(b.size() - 1) : 0;
            for (size_t i = 0; even && i < b.size(); ++i)
                even = std::fabs(b[i] - (b.front() + step * double(i))) < 0.01;
            if (even) {
                grid[QStringLiteral("first")] = b.front();
                grid[QStringLiteral("beat")] = step;
                grid[QStringLiteral("count")] = int(b.size());
            } else {
                QJsonArray beats;
                for (double v : b)
                    beats.append(std::round(v * 100.0) / 100.0);
                grid[QStringLiteral("beats")] = beats;
            }
            o[QStringLiteral("grid")] = grid;
            o[QStringLiteral("downbeat")] = now.downbeat;
            QJsonArray trims;
            for (const auto &t : now.trims)
                trims.append(QJsonArray{t[0], t[1], std::round(t[2] * 1e4) / 1e4, std::round(t[3] * 1e4) / 1e4});
            if (!trims.isEmpty())
                o[QStringLiteral("trims")] = trims;
            songs.append(o);
        }
        if (!songs.isEmpty())
            out[QStringLiteral("songs")] = songs;
    }

    void load(const QJsonObject &in, ControlContext &ctx) override {
        m_keyMatch = in.value(QStringLiteral("keyMatch")).toBool(true);
        const double tone = in.value(QStringLiteral("tone")).toDouble(0);
        m_tone = std::isfinite(tone) ? float(std::clamp(tone, -1.0, 1.0)) : 0.0f;
        sendTone(ctx);
        const int showing = in.value(QStringLiteral("showing")).toInt(0);
        for (const QJsonValue &v : in.value(QStringLiteral("songs")).toArray()) {
            const QJsonObject o = v.toObject();
            Restore r;
            r.on = true;
            r.handle = o.value(QStringLiteral("handle")).toInt(0);
            const QString source = o.value(QStringLiteral("source")).toString();
            if (r.handle < 1 || r.handle > kSounds || source.isEmpty() || m_songs[r.handle].used)
                continue;
            r.name = o.value(QStringLiteral("name")).toString().left(200);
            const QString chop = o.value(QStringLiteral("chop")).toString();
            for (int s = 0; s < kSizes; ++s)
                if (chop.compare(QLatin1String(sizeName(s)), Qt::CaseInsensitive) == 0)
                    r.size = s;
            const QString stem = o.value(QStringLiteral("stem")).toString();
            for (int s = 0; s < kStems; ++s)
                if (stem.compare(QLatin1String(stemName(s)), Qt::CaseInsensitive) == 0)
                    r.stem = s;
            r.page = std::clamp(o.value(QStringLiteral("page")).toInt(0), 0, 100000);
            r.barShift = ((o.value(QStringLiteral("barShift")).toInt(0) % 4) + 4) % 4;
            const double tuned = o.value(QStringLiteral("tune")).toDouble(0);
            r.tune = std::isfinite(tuned) ? float(std::clamp(std::round(tuned), -12.0, 12.0)) : 0.0f;
            r.downbeat = std::clamp(o.value(QStringLiteral("downbeat")).toInt(-1), -1, 3);
            const QJsonObject grid = o.value(QStringLiteral("grid")).toObject();
            const QJsonArray beats = grid.value(QStringLiteral("beats")).toArray();
            const double first = grid.value(QStringLiteral("first")).toDouble(0), step = grid.value(QStringLiteral("beat")).toDouble(0);
            const int count = grid.value(QStringLiteral("count")).toInt(0);
            if (beats.size() >= 2 && beats.size() <= 100000) {
                for (const QJsonValue &b : beats)
                    r.beats.push_back(b.toDouble());
            } else if (std::isfinite(first) && step > 100 && step < 1e6 && count >= 2 && count <= 100000) {
                for (int i = 0; i < count; ++i)
                    r.beats.push_back(first + step * i);
            }
            // A grid must go forward, or it is no grid.
            for (size_t i = 1; i < r.beats.size(); ++i)
                if (!(r.beats[i] > r.beats[i - 1] + 1.0) || !std::isfinite(r.beats[i])) {
                    r.beats.clear();
                    break;
                }
            for (const QJsonValue &tv : o.value(QStringLiteral("trims")).toArray()) {
                const QJsonArray t = tv.toArray();
                if (t.size() != 4)
                    continue;
                const std::array<double, 4> trim{t[0].toDouble(), t[1].toDouble(), t[2].toDouble(), t[3].toDouble()};
                if (std::isfinite(trim[0]) && std::isfinite(trim[1]) && std::isfinite(trim[2]) && std::isfinite(trim[3]) &&
                    r.trims.size() < 4096)
                    r.trims.push_back(trim);
            }
            // Its place is kept from now, so its loops find it there
            // when it has been read (or the player drops it in again).
            Song &s = m_songs[r.handle];
            s = Song();
            s.used = true;
            s.name = r.name;
            s.source = source;
            s.missing = r;
            if (isLink(source) || QFileInfo(source).isFile()) {
                m_importer.load(source, r, ctx.scripted());
            } else {
                ctx.notice(QStringLiteral("The Sampler's song isn't where it was (%1): drop it in again and its loops play")
                               .arg(QFileInfo(source).fileName()));
            }
        }
        if (showing >= 1 && showing <= kSounds && m_songs[showing].used)
            m_current = showing;
        m_wanted = m_current;
        background(ctx.now(), ctx);
    }

    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("songs")) {
            int n = 0;
            for (int h = 1; h <= kSounds; ++h)
                n += m_songs[h].sound != nullptr;
            return float(n);
        }
        if (name == QLatin1String("plays"))
            return float(m_plays);
        if (name == QLatin1String("trims"))
            return float(m_trimsMade);
        if (name == QLatin1String("stems"))
            return current() && current()->stems ? 1.0f : 0.0f;
        if (name == QLatin1String("busy"))
            return m_importer.busy() ? 1.0f : 0.0f;
        if (name == QLatin1String("bpm"))
            return current() && current()->sound ? float(current()->sound->bpm) : 0.0f;
        if (name == QLatin1String("chops"))
            return current() ? float(current()->layout.chops.size()) : 0.0f;
        if (name == QLatin1String("page"))
            return current() ? float(current()->page) : 0.0f;
        // The layout, for a script to find a chop by its beat.
        if (const Song *s = current(); s && s->sound) {
            if (name == QLatin1String("origin"))
                return float(s->layout.origin);
            if (name == QLatin1String("rowBeats"))
                return float(s->layout.rowBeats);
            if (name == QLatin1String("rows"))
                return float(s->layout.rows);
            if (name == QLatin1String("downbeat"))
                return float((((s->marks.downbeat + s->barShift) % 4) + 4) % 4);
            if (name == QLatin1String("gridFirst"))
                return float(s->sound->beats.front());
            if (name == QLatin1String("gridBeat"))
                return float((s->sound->beats.back() - s->sound->beats.front()) / double(s->sound->beats.size() - 1));
            if (name == QLatin1String("stem"))
                return float(s->stem);
            if (name == QLatin1String("size"))
                return float(s->size);
            if (name == QLatin1String("tune"))
                return s->tune;
            if (name == QLatin1String("matched"))
                return s->sound->matched ? 1.0f : 0.0f;
        }
        return PartControl::tutorialProbe(name);
    }

    void paint(QPainter &p, const PaintContext &pc) override;
    bool click(QPointF at, ControlContext &ctx) override;

    QString help() const override {
        return QStringLiteral(
            "drop a sound file on the window\tthe song is read, its beat, bar lines and key are found, and it is laid "
            "out here in five rows of whole bars, cut where its sounds land (any format ffmpeg reads; a WAV needs "
            "nothing)\n"
            "Ctrl+V\tpaste a link to a song (YouTube, or anywhere yt-dlp fetches from), or a file's path\n"
            "a tap\tplays that chop to its end, in time with the studio: each beat of the song lasts one beat of "
            "the track, at the song's own pitch; the next tap cuts it off\n"
            "a hold\trepeats the chop for as long as the finger stays; more fingers, more chops at once\n"
            "hold, then slide left or right\tmoves where the chop STARTS, earlier or later (heard the next time "
            "round); the pad's whole width is two beats. Only a finger that has rested on its chop a moment "
            "slides (it says START ↔ when it can): a tap that skids changes nothing\n"
            "two fingers side by side on a chop (or a second set down right beside the one holding), then slide\t"
            "moves where the chop ENDS. A finger further off is its own chop, as always\n"
            "Space\tloop what you just played, as on every page (Shift+Space: a new loop instead of a layer): "
            "that is how a chop goes on playing by itself, and Ctrl+Z takes it back. Pressing the pad down does "
            "nothing here\n"
            "Delete\tthe chop you last touched gets its own start and end back. Shift+Delete: every chop of the "
            "song does\n"
            "Left / Right\twhich stem the pad plays: the mix, or once S has separated them, the drums, bass, "
            "vocals or the rest\n"
            "S\tseparate the stems: the song goes through a model on this computer (the first time it downloads "
            "itself; it needs uv). S again calls it off\n"
            "Up / Down\tthe page of the song: five rows before, five rows on\n"
            "Shift+Left / Shift+Right\thow the song is cut: AUTO (where sounds land), half a beat, a beat, two, a "
            "bar, two bars\n"
            ", and .\ttune the song down or up a semitone\n"
            "the key left of 1 (or \\)\tKEY MATCH: the song is moved to the studio's key, by the shortest way "
            "(on by default)\n"
            "T\tgive the studio the song's tempo and key, so it plays untouched (it happens by itself when "
            "nothing is made yet)\n"
            "B\tmove the bar lines a beat, when the song's ONE was heard in the wrong place\n"
            "H and D\tthe song's tempo was heard at double or half speed (it happens around 85 and 170 BPM: hip-hop "
            "against drum and bass): H halves it, D doubles it, and its bars are laid out again\n"
            "- and +\ttone: darker, thinner\n"
            "Q W E R\tthe songs loaded, up to four; their loops keep playing whichever is shown\n"
            "F10\toverdub: play into the playing loop\n"
            "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot · clear it · stop the loop at the bar");
    }

    QString status() const override {
        if (m_importer.busy() && !current())
            return m_importer.stage();
        const Song *s = current();
        if (!s || !s->sound)
            return QStringLiteral("DROP A SOUND FILE ON THE WINDOW, OR PASTE A LINK (Ctrl+V)");
        QString line = s->name.left(28).toUpper();
        if (m_touched >= 0 && m_touched < int(s->layout.chops.size())) {
            const Chop &c = s->layout.chops[size_t(m_touched)];
            const auto it = s->trims.find(c.key());
            line += QStringLiteral(" · BAR %1").arg(barOf(*s, c) + 1);
            if (it != s->trims.end())
                line += QStringLiteral(" · START %1 END %2 BEATS")
                            .arg(double(it->second.start), 0, 'f', 2)
                            .arg(double(it->second.end), 0, 'f', 2);
        }
        return line;
    }

private:
    struct Trim {
        float start = 0, end = 0; // beats added to the chop's own start and end
    };
    struct Song {
        bool used = false;
        std::unique_ptr<Sound> sound; // null while it is missing or being read
        Marks marks;
        Layout layout;
        QString name, source, cache;
        int serial = 0;
        int size = Auto, page = 0, barShift = 0, stem = Mix;
        float tune = 0;
        bool stems = false;
        std::map<int64_t, Trim> trims;
        Restore missing; // what a song file said of it, until it is read
    };
    struct Holder {
        bool used = false;
        int src = -1;    // the finger
        int second = -1; // the finger beside it, sliding the end
        int id = 0;      // its voice
        int handle = 0, chop = -1;
        int64_t key = 0;
        float x = 0, y = 0;
        float x0 = 0;    // where the slide is measured from
        float sx0 = 0;   // the same for the second finger
        float base = 0;  // the trim when the slide began
        bool moved = false;
        double t0 = 0, t2 = 0; // when it landed, and its second finger
        double sent = 0;
        double roundBeat = 0;  // the studio's beat its current round began on
        bool rounding = false; // its rounds are being sent (a song with a beat)
    };
    struct Retired {
        std::unique_ptr<Sound> sound;
        uint32_t epoch = 0;
        double at = 0;
    };

    static bool touching(const Frame &f, int id) {
        for (int i = 0; i < f.count; ++i)
            if (f.touches[i].id == id)
                return true;
        return false;
    }
    static const Touch *touch(const Frame &f, int id) {
        for (int i = 0; i < f.count; ++i)
            if (f.touches[i].id == id)
                return &f.touches[i];
        return nullptr;
    }
    Holder *holderOf(int touchId) {
        for (Holder &h : m_holders)
            if (h.used && h.src == touchId)
                return &h;
        return nullptr;
    }
    Holder *secondOf(int touchId) {
        for (Holder &h : m_holders)
            if (h.used && h.second == touchId)
                return &h;
        return nullptr;
    }
    Song *current() { return m_current >= 1 && m_songs[m_current].used ? &m_songs[m_current] : nullptr; }
    const Song *current() const { return m_current >= 1 && m_songs[m_current].used ? &m_songs[m_current] : nullptr; }
    static bool ownKey(int key) {
        switch (key) {
        case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_PageUp:
        case Qt::Key_PageDown: case Qt::Key_BracketLeft: case Qt::Key_BracketRight: case Qt::Key_Minus:
        case Qt::Key_Plus: case Qt::Key_Equal: case Qt::Key_Comma: case Qt::Key_Period: case Qt::Key_Return:
        case Qt::Key_Enter: case Qt::Key_section: case Qt::Key_QuoteLeft: case Qt::Key_Backslash:
        case Qt::Key_AsciiTilde: case Qt::Key_onehalf: case Qt::Key_Delete: case Qt::Key_S: case Qt::Key_T:
        case Qt::Key_B: case Qt::Key_Q: case Qt::Key_W: case Qt::Key_E: case Qt::Key_R: case Qt::Key_H:
        case Qt::Key_D:
            return true;
        default: return false;
        }
    }
    // The bar a chop begins in, counted from the song's first.
    static int barOf(const Song &s, const Chop &c) {
        const int down = (((s.marks.downbeat + s.barShift) % 4) + 4) % 4;
        return int(std::floor((c.at / 2.0 - down) / 4.0)) + (down > 0 ? 1 : 0); // a pickup is the first bar
    }
    Trim trimOf(const Holder &h) const {
        const Song &s = m_songs[h.handle];
        const auto it = s.trims.find(h.key);
        return it == s.trims.end() ? Trim() : it->second;
    }
    // A chop's start and end as played: its own, moved by what was slid,
    // never shorter than a sixty-fourth of a beat nor reaching back more
    // than a bar before it.
    static void played(const Chop &c, Trim t, float &start, float &end) {
        const double s = std::clamp(c.start + double(t.start), c.start - 4.0, c.end + double(t.end) - 1.0 / 32);
        const double e = std::max(c.end + double(t.end), s + 1.0 / 32);
        start = float(s);
        end = float(e);
    }
    // The same as the samples a command carries.
    static void playedAt(const Sound &snd, const Chop &c, Trim t, int32_t &start, int32_t &end) {
        float s, e;
        played(c, t, s, e);
        start = int32_t(std::llround(snd.at(s)));
        end = int32_t(std::llround(snd.at(e)));
    }

    // --- playing -------------------------------------------------------------

    void land(const Touch &t, double now, ControlContext &ctx) {
        // Right beside a finger that landed with it, or has been holding:
        // its second, sliding the end.
        Holder *beside = nullptr;
        for (Holder &h : m_holders)
            if (h.used && h.second < 0 && (now - h.t0 <= kTogether || now - h.t0 >= kTrimHold) && std::fabs(t.x - h.x) < kSecondX &&
                std::fabs(t.y - h.y) < kSecondY && (!beside || std::fabs(t.x - h.x) < std::fabs(t.x - beside->x)))
                beside = &h;
        if (beside) {
            beside->second = t.id;
            beside->t2 = now;
            beside->sx0 = t.x;
            beside->x0 = beside->x;
            beside->base = trimOf(*beside).end;
            beside->moved = false;
            return;
        }
        Song *s = current();
        if (!s || !s->sound) {
            emptyTap(t, ctx); // OPEN FILE and RECORD, under DROP A SONG HERE
            return;
        }
        const int row = s->page * kRows + std::clamp(int((1.0f - t.y) * kRows), 0, kRows - 1);
        const int chop = s->layout.find(row, std::clamp(t.x, 0.0f, 0.9999f));
        if (chop < 0)
            return;
        const Chop &c = s->layout.chops[size_t(chop)];
        m_touched = chop;
        Holder *h = nullptr;
        for (Holder &q : m_holders)
            if (!q.used) {
                h = &q;
                break;
            }
        if (!h)
            return;
        *h = Holder();
        h->used = true;
        h->src = t.id;
        h->id = m_nextId = m_nextId % 0xFFFFF + 1;
        h->handle = m_current;
        h->chop = chop;
        h->key = c.key();
        h->x = h->x0 = t.x;
        h->y = t.y;
        h->t0 = now;
        h->base = trimOf(*h).start;
        Command on;
        on.type = Command::On;
        on.id = h->id;
        on.t = now;
        // What it is, for Space to tell a repeated pattern from new
        // playing: this cut of this song.
        on.i[0] = int(c.key() & 0x1FFFFFFF) | (m_current - 1) << 29;
        on.i[1] = m_current | s->stem << 8 | (cmd::FlagCut | (m_keyMatch ? cmd::FlagKeyMatch : 0)) << 16;
        on.f[0] = 1.0f;
        playedAt(*s->sound, c, trimOf(*h), on.i[2], on.i[3]);
        on.f[3] = s->tune;
        ctx.send(on);
        ++m_plays;
        h->roundBeat = ctx.beatAt(now);
        h->rounding = s->sound->matched;
    }

    // A held chop's next round, sent once it has come (a few milliseconds
    // late at most, stamped with when it came), and only while the finger
    // is still down: one sent ahead of a lift would be recorded into a loop
    // as a chop the hand never played. The engine wraps the chop itself, to
    // the sample; this only tells Space and the loop that it went round.
    void sendRounds(double now, ControlContext &ctx) {
        for (Holder &h : m_holders) {
            if (!h.used || !h.rounding)
                continue;
            const Song &s = m_songs[h.handle];
            if (!s.sound || h.chop < 0 || h.chop >= int(s.layout.chops.size()) || s.layout.chops[size_t(h.chop)].key() != h.key)
                continue;
            const Chop &c = s.layout.chops[size_t(h.chop)];
            float a, b;
            played(c, trimOf(h), a, b);
            // The chop in the studio's beats: the song's beats over how many
            // go by in one of the studio's.
            const double beats = double(b - a) * foldFor(s.sound->bpm, std::max(1.0, ctx.studio().bpm));
            if (beats < 0.05)
                continue;
            const double next = h.roundBeat + beats;
            const double at = ctx.timeAt(next);
            if (at > now)
                continue;
            Command on;
            on.type = Command::On;
            on.id = h.id;
            on.t = at;
            on.i[0] = int(h.key & 0x1FFFFFFF) | (h.handle - 1) << 29;
            on.i[1] = h.handle | s.stem << 8 | (cmd::FlagRound | (m_keyMatch ? cmd::FlagKeyMatch : 0)) << 16;
            on.f[0] = 1.0f;
            playedAt(*s.sound, c, trimOf(h), on.i[2], on.i[3]);
            on.f[3] = s.tune;
            ctx.send(on);
            h.roundBeat = next;
        }
    }

    void off(int id, bool atOnce, double now, ControlContext &ctx) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        c.t = now;
        c.i[0] = atOnce ? 1 : 0;
        ctx.send(c);
    }

    void release(Holder &h, double now, ControlContext &ctx) {
        off(h.id, false, now, ctx);
        h = Holder();
    }

    // A held finger slid: the chop's start (one finger) or end (two).
    void slide(Holder &h, const Frame &f, ControlContext &ctx) {
        Song &s = m_songs[h.handle];
        if (!s.sound || h.chop < 0 || h.chop >= int(s.layout.chops.size()) || s.layout.chops[size_t(h.chop)].key() != h.key)
            return;
        const Touch *t2 = h.second >= 0 ? touch(f, h.second) : nullptr;
        if (h.second >= 0 && !t2)
            return;
        // How far the slide has gone: one finger's way, or the two's together.
        const float dx = t2 ? 0.5f * ((h.x - h.x0) + (t2->x - h.sx0)) : h.x - h.x0;
        if (!h.moved) {
            // Not held long enough yet: wherever the finger wanders is
            // where a slide will be measured from.
            if (t2 ? f.t - h.t2 < kSecondHold : f.t - h.t0 < kTrimHold) {
                h.x0 = h.x;
                if (t2)
                    h.sx0 = t2->x;
                return;
            }
            if (std::fabs(dx) < kTrimDead)
                return;
            // A slide, then, and it counts from here: no jump as it begins.
            h.moved = true;
            h.x0 = h.x;
            if (t2)
                h.sx0 = t2->x;
            ++m_trimsMade;
            return;
        }
        const Chop &c = s.layout.chops[size_t(h.chop)];
        Trim t = trimOf(h);
        const float len = float(c.end - c.start);
        if (h.second >= 0)
            t.end = std::clamp(h.base + dx * kTrimBeats, t.start - len + 1.0f / 32, 8.0f);
        else
            t.start = std::clamp(h.base + dx * kTrimBeats, -4.0f, len + t.end - 1.0f / 32);
        const Trim was = trimOf(h);
        if (std::fabs(t.start - was.start) < 0.002f && std::fabs(t.end - was.end) < 0.002f)
            return;
        if (f.t - h.sent < 1.0 / 60)
            return;
        h.sent = f.t;
        if (std::fabs(t.start) < 1e-4f && std::fabs(t.end) < 1e-4f)
            s.trims.erase(h.key);
        else
            s.trims[h.key] = t;
        Command m;
        m.type = Command::Move;
        m.id = h.id;
        m.t = f.t;
        playedAt(*s.sound, c, t, m.i[2], m.i[3]);
        ctx.send(m);
    }

    // --- settings ------------------------------------------------------------

    bool need(ControlContext &ctx) {
        if (current() && current()->sound)
            return true;
        ctx.notice(m_importer.busy() ? QStringLiteral("The song is still being read")
                                     : QStringLiteral("No song yet: drop a sound file on the window, or paste a link (Ctrl+V)"));
        return false;
    }

    void relay(Song &s) {
        if (!s.sound)
            return;
        s.layout = layOut(*s.sound, s.marks, s.size, s.barShift);
        s.page = std::clamp(s.page, 0, s.layout.pages() - 1);
        // The fingers down belong to the old cut.
        for (Holder &h : m_holders)
            if (h.used && &m_songs[h.handle] == &s)
                h.chop = -1;
        m_touched = -1;
    }

    void stem(int dir, ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        if (!s.stems) {
            ctx.notice(m_importer.separating() ? QStringLiteral("The stems are on their way")
                                               : QStringLiteral("S separates the stems first: drums, bass, vocals and the rest"));
            return;
        }
        setStem((s.stem + dir + kStems) % kStems, ctx);
    }
    void setStem(int stem, ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        if (stem != Mix && !s.stems) {
            ctx.notice(QStringLiteral("S separates the stems first: drums, bass, vocals and the rest"));
            return;
        }
        s.stem = std::clamp(stem, 0, int(kStems) - 1);
        ctx.notice(QString::fromLatin1(stemName(s.stem)));
    }

    void resize(int dir, ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        const int to = std::clamp(s.size + dir, 0, int(kSizes) - 1);
        if (to == s.size)
            return;
        // The page that shows the same part of the song.
        const double beat = s.layout.origin + double(s.page) * kRows * s.layout.rowBeats;
        s.size = to;
        relay(s);
        s.page = std::clamp(int(std::floor((beat - s.layout.origin) / (kRows * s.layout.rowBeats) + 1e-6)), 0,
                            s.layout.pages() - 1);
        ctx.notice(QStringLiteral("CHOPS: %1").arg(QLatin1String(sizeName(s.size))));
    }

    void turn(int dir, ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        const int to = std::clamp(s.page + dir, 0, s.layout.pages() - 1);
        if (to == s.page) {
            ctx.notice(dir < 0 ? QStringLiteral("The top of the song") : QStringLiteral("The end of the song"));
            return;
        }
        s.page = to;
        m_touched = -1;
    }

    void tone(int dir, ControlContext &ctx) {
        m_tone = std::clamp(m_tone + dir * kToneStep, -1.0f, 1.0f);
        if (std::fabs(m_tone) < 1e-4f)
            m_tone = 0;
        sendTone(ctx);
        ctx.notice(m_tone == 0 ? QStringLiteral("TONE: AS IT IS")
                               : m_tone < 0 ? QStringLiteral("TONE: DARKER %1").arg(int(std::lround(-m_tone / kToneStep)))
                                            : QStringLiteral("TONE: THINNER %1").arg(int(std::lround(m_tone / kToneStep))));
    }
    void sendTone(ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.i[0] = cmd::ParamTone;
        c.f[0] = m_tone;
        c.flags = Command::NoRecord;
        ctx.send(c);
    }

    void tune(int dir, ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        s.tune = std::clamp(s.tune + float(dir), -12.0f, 12.0f);
        ctx.notice(QStringLiteral("TUNE %1%2").arg(s.tune > 0 ? QStringLiteral("+") : QString()).arg(int(s.tune)));
    }

    void toggleKeyMatch(ControlContext &ctx) {
        m_keyMatch = !m_keyMatch;
        ctx.notice(m_keyMatch ? QStringLiteral("KEY MATCH: the song is moved to the studio's key")
                              : QStringLiteral("KEY MATCH OFF: the song at its own pitch"));
    }

    void toggleOverdub(ControlContext &ctx) {
        m_overdub = !m_overdub;
        ctx.overdub(m_overdub);
        ctx.notice(m_overdub ? QStringLiteral("OVERDUB: playing goes into the loop") : QStringLiteral("OVERDUB OFF"));
    }

    // The tempo was heard at half or double what it is: the song gets a
    // new grid, a beat between every two (×2) or every other beat (÷2),
    // and its bars are laid out again. It is a new Sound over the same
    // audio, published in the old one's place; voices playing the old one
    // finish on it. Loops name their chops in samples, so they sound as
    // they did. Chops slid on the old grid are let go of: their places
    // aren't chops any more.
    void regrid(double by, ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        const Sound &old = *s.sound;
        if (!old.matched) {
            ctx.notice(QStringLiteral("No steady beat was found in this one: there is no tempo to double or halve"));
            return;
        }
        if (old.bpm * by < 40 || old.bpm * by > 320) {
            ctx.notice(QStringLiteral("%1 BPM is as far as it goes").arg(old.bpm, 0, 'f', 1));
            return;
        }
        auto fresh = std::make_unique<Sound>();
        fresh->rate = old.rate;
        fresh->frames = old.frames;
        for (int st = 0; st < kStems; ++st) {
            fresh->own[st] = old.own[st];
            fresh->pcm[st].store(fresh->own[st].get(), std::memory_order_release);
        }
        fresh->matched = old.matched;
        fresh->bpm = old.bpm * by;
        fresh->keyRoot = old.keyRoot;
        fresh->keyMinor = old.keyMinor;
        fresh->keyKnown = old.keyKnown;
        fresh->cents = old.cents;
        const Marks &m = s.marks;
        Marks n;
        const int count = old.count();
        auto strengthAt = [&](int i) { return i >= 0 && i < int(m.strength.size()) ? m.strength[size_t(i)] : 0.0f; };
        auto attackAt = [&](int i) { return i >= 0 && i < int(m.attack.size()) ? m.attack[size_t(i)] : old.beats[size_t(std::clamp(i, 0, count - 1))]; };
        const int down = (((m.downbeat + s.barShift) % 4) + 4) % 4;
        if (by > 1) {
            // A beat between every two: what landed on the eighth between
            // them (the analysis heard it as the beat's third sixteenth).
            for (int i = 0; i < count; ++i) {
                fresh->beats.push_back(old.beats[size_t(i)]);
                n.strength.push_back(strengthAt(i));
                n.attack.push_back(attackAt(i));
                if (i + 1 == count)
                    break;
                const double mid = 0.5 * (old.beats[size_t(i)] + old.beats[size_t(i + 1)]);
                fresh->beats.push_back(mid);
                const size_t k = size_t(4 * i + 2);
                n.strength.push_back(k < m.sub.size() ? m.sub[k] : 0.0f);
                n.attack.push_back(mid);
            }
            // A bar was four beats and is now eight: two bars.
            n.downbeat = (2 * down) % 4;
            for (float c : m.change) {
                n.change.push_back(c);
                n.change.push_back(0.0f);
            }
        } else {
            // Every other beat, keeping the bar lines.
            const int phase = down % 2;
            for (int i = phase; i < count; i += 2) {
                fresh->beats.push_back(old.beats[size_t(i)]);
                n.strength.push_back(strengthAt(i));
                n.attack.push_back(attackAt(i));
            }
            if (fresh->beats.size() < 2) {
                ctx.notice(QStringLiteral("Too short to halve"));
                return;
            }
            // A bar is now eight of the old beats: two old bars, the one
            // that changes more first.
            const size_t pairs = (m.change.size() + 1) / 2;
            float even = 0, odd = 0;
            for (size_t k = 0; k < m.change.size(); ++k)
                (k % 2 ? odd : even) += m.change[k];
            const int first = odd > even ? 1 : 0;
            n.downbeat = ((down - phase) / 2 + 2 * first) % 4;
            for (size_t k = 0; k < pairs; ++k) {
                const size_t a = 2 * k + size_t(first);
                n.change.push_back(a < m.change.size() ? m.change[a] : 0.0f);
            }
        }
        s.marks = std::move(n);
        s.barShift = 0;
        const size_t trims = s.trims.size();
        s.trims.clear();
        for (Holder &h : m_holders)
            if (h.used && h.handle == m_current)
                h.chop = -1;
        // The page that shows the same part of the song.
        const double sample = old.at(s.layout.origin + double(s.page) * kRows * s.layout.rowBeats);
        const int handle = m_current;
        m_bank->sounds[handle].store(nullptr, std::memory_order_release);
        m_retired.push_back({std::move(s.sound), m_bank->epoch.load(std::memory_order_acquire), ctx.now()});
        s.sound = std::move(fresh);
        m_bank->sounds[handle].store(s.sound.get(), std::memory_order_release);
        relay(s);
        s.page = std::clamp(int(std::floor((s.sound->beatOf(sample) - s.layout.origin) / (kRows * s.layout.rowBeats) + 1e-6)), 0,
                            s.layout.pages() - 1);
        QString said = QStringLiteral("TEMPO %1: %2 BPM").arg(by > 1 ? QStringLiteral("×2") : QStringLiteral("÷2")).arg(s.sound->bpm, 0, 'f', 1);
        if (trims)
            said += QStringLiteral(" · the chops' slid starts and ends are let go");
        ctx.notice(said);
    }

    void shiftBar(ControlContext &ctx) {
        if (!need(ctx))
            return;
        Song &s = *current();
        s.barShift = (s.barShift + 1) % 4;
        relay(s);
        ctx.notice(s.barShift == 0 ? QStringLiteral("BAR LINES: where they were heard")
                                   : QStringLiteral("BAR LINES: %1 BEAT%2 LATER").arg(s.barShift).arg(s.barShift > 1 ? "S" : ""));
    }

    void reset(ControlContext &ctx) {
        Song *s = current();
        if (!s || m_touched < 0 || m_touched >= int(s->layout.chops.size())) {
            ctx.notice(QStringLiteral("Touch a chop, then Delete puts its start and end back"));
            return;
        }
        const bool had = s->trims.erase(s->layout.chops[size_t(m_touched)].key()) > 0;
        resend(ctx);
        ctx.notice(had ? QStringLiteral("The chop's own start and end") : QStringLiteral("That chop is as it was cut"));
    }
    void resetAll(ControlContext &ctx) {
        if (Song *s = current()) {
            const size_t n = s->trims.size();
            s->trims.clear();
            resend(ctx);
            ctx.notice(n ? QStringLiteral("Every chop's own start and end (%1 put back)").arg(n)
                         : QStringLiteral("Every chop is as it was cut"));
        }
    }
    // A reset is heard on what is held, too.
    void resend(ControlContext &ctx) {
        for (Holder &h : m_holders) {
            const Song &s = m_songs[h.handle];
            if (!h.used || !s.sound || h.chop < 0 || h.chop >= int(s.layout.chops.size()))
                continue;
            Command m;
            m.type = Command::Move;
            m.id = h.id;
            m.t = ctx.now();
            playedAt(*s.sound, s.layout.chops[size_t(h.chop)], trimOf(h), m.i[2], m.i[3]);
            ctx.send(m);
            h.moved = false;
            h.x0 = h.x;
            h.base = h.second >= 0 ? trimOf(h).end : trimOf(h).start;
        }
    }

    void show(int which, ControlContext &ctx) {
        if (which < 1 || which > kSounds || !m_songs[which].used) {
            ctx.notice(QStringLiteral("No song %1: drop another on the window").arg(which));
            return;
        }
        m_current = which;
        m_touched = -1;
        ctx.notice(m_songs[which].name.left(40).toUpper());
    }

    // The studio takes the song's tempo and key: it then plays as it is.
    // asked: the player pressed T (else it is a first song into silence).
    void giveTempo(ControlContext &ctx, bool asked) {
        if (!need(ctx))
            return;
        const Sound &s = *current()->sound;
        if (!s.matched) {
            if (asked)
                ctx.notice(QStringLiteral("No steady beat was found in this one: it plays as it is"));
            return;
        }
        // Into the tempo range a track lives in.
        double bpm = s.bpm;
        while (bpm < 70)
            bpm *= 2;
        while (bpm > 180)
            bpm *= 0.5;
        ctx.setTempo(bpm);
        QString said = QStringLiteral("%1 BPM").arg(bpm, 0, 'f', 1);
        if (s.keyKnown) {
            // The diatonic family: Ionian is its second mode from the
            // bright end, Aeolian its fifth.
            ctx.setKey(s.keyRoot, 0, s.keyMinor ? 4 : 1);
            said += QStringLiteral(" · %1 %2").arg(QLatin1String(noteName(s.keyRoot)), s.keyMinor ? QStringLiteral("MINOR") : QStringLiteral("MAJOR"));
        }
        ctx.notice(QStringLiteral("THE STUDIO TAKES THE SONG'S: %1").arg(said));
    }

    void separate(ControlContext &ctx) {
        if (m_importer.separating()) {
            m_importer.cancel();
            ctx.notice(QStringLiteral("Stems called off"));
            return;
        }
        if (!need(ctx))
            return;
        Song &s = *current();
        if (s.stems) {
            ctx.notice(QStringLiteral("The stems are here: ← → choose one"));
            return;
        }
        if (m_importer.busy()) {
            ctx.notice(QStringLiteral("One thing at a time: the song is still being read"));
            return;
        }
        const Pcm *mix = s.sound->pcm[Mix].load(std::memory_order_acquire);
        // A script takes stems already made and doesn't wait a minute for
        // a model, unless it is told to (TENFOLD_STEMS_IN_SCRIPTS: a
        // render that wants the real thing).
        const bool model = mix && (!ctx.scripted() || qEnvironmentVariableIsSet("TENFOLD_STEMS_IN_SCRIPTS"));
        m_importer.separate(s.serial, s.cache, model ? mix->data : std::vector<int16_t>(), int(s.sound->rate), ctx.scripted());
        if (!ctx.scripted())
            ctx.notice(QStringLiteral("Separating the stems: this takes a minute or so (S calls it off)"));
        background(ctx.now(), ctx);
    }

    // --- songs coming and going ----------------------------------------------

    void unload(int handle, ControlContext &ctx) {
        Song &s = m_songs[handle];
        if (s.sound) {
            m_bank->sounds[handle].store(nullptr, std::memory_order_release);
            m_retired.push_back({std::move(s.sound), m_bank->epoch.load(std::memory_order_acquire), ctx.now()});
        }
        for (Holder &h : m_holders)
            if (h.used && h.handle == handle)
                h = Holder();
        s = Song();
    }

    // What a song file keeps of a song (and a song being read again takes back).
    Restore remember(const Song &s) const {
        if (!s.sound)
            return s.missing;
        Restore r;
        r.on = true;
        r.size = s.size;
        r.page = s.page;
        r.barShift = s.barShift;
        r.stem = s.stem;
        r.tune = s.tune;
        r.beats = s.sound->beats;
        r.downbeat = s.marks.downbeat;
        r.name = s.name;
        for (const auto &[key, t] : s.trims)
            r.trims.push_back({double((key >> 8) - (1 << 20)), double(key & 0xff), double(t.start), double(t.end)});
        return r;
    }

    void arrived(Loaded &&l, ControlContext &ctx) {
        if (l.kind == Loaded::Failed) {
            ctx.notice(l.error);
            return;
        }
        if (l.kind == Loaded::Stems) {
            for (int h = 1; h <= kSounds; ++h) {
                Song &s = m_songs[h];
                if (!s.sound || s.serial != l.serial)
                    continue;
                for (int st = Drums; st < kStems; ++st) {
                    if (!l.stems[st] || l.stems[st]->frames < s.sound->frames - 4800)
                        continue; // not this song's
                    s.sound->own[st] = std::move(l.stems[st]);
                    s.sound->pcm[st].store(s.sound->own[st].get(), std::memory_order_release);
                }
                s.stems = true;
                for (int st = Drums; st < kStems; ++st)
                    s.stems &= s.sound->own[st] != nullptr;
                // A song file's choice of stem, now that it can be played.
                if (s.stems && m_stemWanted[h] > 0)
                    s.stem = std::clamp(m_stemWanted[h], 0, int(kStems) - 1);
                m_stemWanted[h] = 0;
                ctx.notice(s.stems ? QStringLiteral("STEMS READY: ← → choose drums, bass, vocals or the rest")
                                   : QStringLiteral("The stems don't match the song"));
            }
            return;
        }
        // A song. Its place: the one a song file kept for it, else a free
        // one, else the place of the song loaded longest ago.
        int handle = l.restore.on ? l.restore.handle : 0;
        if (handle < 1 || handle > kSounds) {
            for (int h = 1; h <= kSounds && handle == 0; ++h)
                if (!m_songs[h].used)
                    handle = h;
            if (handle == 0) {
                handle = 1;
                for (int h = 2; h <= kSounds; ++h)
                    if (m_songs[h].serial < m_songs[handle].serial)
                        handle = h;
                ctx.notice(QStringLiteral("Four songs at most: %1 makes room").arg(m_songs[handle].name.left(30)));
            }
        }
        unload(handle, ctx);
        Song &s = m_songs[handle];
        s.used = true;
        s.name = l.restore.on && !l.restore.name.isEmpty() ? l.restore.name : l.name;
        s.source = l.source;
        s.cache = l.cache;
        s.serial = ++m_serial;
        s.marks = std::move(l.marks);
        if (l.restore.on)
            restore(*l.sound, s, l.restore);
        s.sound = std::move(l.sound);
        relay(s);
        s.page = std::clamp(l.restore.on ? l.restore.page : 0, 0, s.layout.pages() - 1);
        m_bank->sounds[handle].store(s.sound.get(), std::memory_order_release);
        if (!l.restore.on || m_wanted == handle || m_current == 0)
            m_current = handle;
        const Sound &snd = *s.sound;
        if (!l.restore.on) {
            // Into silence, the studio is the song's: its tempo and key.
            bool made = false;
            const StudioView &v = ctx.studio();
            for (int p = 0; p < v.partCount && p < kMaxParts; ++p)
                for (const ClipView &c : v.parts[p].clips)
                    made |= c.state != ClipView::Empty;
            QString said = s.name.left(36).toUpper();
            if (snd.matched) {
                said += QStringLiteral(" · %1 BPM").arg(snd.bpm, 0, 'f', 1);
                if (snd.keyKnown)
                    said += QStringLiteral(" · %1 %2").arg(QLatin1String(noteName(snd.keyRoot)), snd.keyMinor ? QStringLiteral("MINOR") : QStringLiteral("MAJOR"));
            } else {
                said += QStringLiteral(" · no steady beat: it plays as it is");
            }
            if (l.truncated)
                said += QStringLiteral(" · its first 15 minutes");
            if (!made && snd.matched) {
                giveTempo(ctx, false);
                said += QStringLiteral(" · the studio takes its tempo%1").arg(snd.keyKnown ? QStringLiteral(" and key") : QString());
            }
            ctx.notice(said);
        }
        // Stems made for it before are a moment's reading away.
        if (QFile::exists(s.cache + QStringLiteral("/stems/other.wav")))
            m_importer.separate(s.serial, s.cache, {}, int(snd.rate), ctx.scripted());
    }

    // A song read again for a song file: the grid its loops were made on,
    // its cut, its trims. The analysis just made still says where sounds
    // land, moved onto the kept grid where the two differ.
    void restore(Sound &sound, Song &s, const Restore &r) {
        s.size = std::clamp(r.size, 0, int(kSizes) - 1);
        s.barShift = r.barShift;
        s.stem = Mix; // until its stems are read
        s.missing = Restore();
        m_stemWanted[r.handle >= 1 && r.handle <= kSounds ? r.handle : 0] = r.stem;
        s.tune = r.tune;
        if (r.beats.size() >= 2) {
            bool same = r.beats.size() == sound.beats.size();
            for (size_t i = 0; same && i < r.beats.size(); ++i)
                same = std::fabs(r.beats[i] - sound.beats[i]) < 48.0;
            if (!same) {
                Marks m;
                m.downbeat = r.downbeat >= 0 ? r.downbeat : s.marks.downbeat;
                m.change = s.marks.change;
                const std::vector<double> &fresh = sound.beats;
                for (double at : r.beats) {
                    const size_t j = std::min(fresh.size() - 1, size_t(std::lower_bound(fresh.begin(), fresh.end(), at) - fresh.begin()));
                    const size_t k = j > 0 && at - fresh[j - 1] < fresh[j] - at ? j - 1 : j;
                    const bool near = std::fabs(fresh[k] - at) < 0.06 * sound.rate;
                    m.strength.push_back(near && k < s.marks.strength.size() ? s.marks.strength[k] : 0.0f);
                    m.attack.push_back(near && k < s.marks.attack.size() && std::fabs(s.marks.attack[k] - at) < 0.06 * sound.rate
                                           ? s.marks.attack[k]
                                           : at);
                }
                s.marks = std::move(m);
            } else if (r.downbeat >= 0) {
                s.marks.downbeat = r.downbeat;
            }
            sound.beats = r.beats;
            const double per = (r.beats.back() - r.beats.front()) / double(r.beats.size() - 1);
            if (per > 0 && sound.matched)
                sound.bpm = 60.0 * sound.rate / per;
        }
        for (const auto &t : r.trims) {
            const int64_t key = (int64_t(std::llround(t[0])) + (1 << 20)) * 256 + int64_t(std::clamp(std::llround(t[1]), 0LL, 255LL));
            s.trims[key] = {float(std::clamp(t[2], -4.0, 64.0)), float(std::clamp(t[3], -64.0, 8.0))};
        }
    }

    enum Spot : int {
        SpotStem = 100, // + stem
        SpotSong = 120, // + handle
        SpotStems = 140,
        SpotPageUp,
        SpotPageDown,
        SpotSmaller,
        SpotLarger,
        SpotTuneDown,
        SpotTuneUp,
        SpotKeyMatch,
        SpotTempo,
        SpotHalf,
        SpotDouble,
        SpotBar,
        SpotReset,
        SpotOverdub,
        SpotDarker,
        SpotBrighter,
    };

    void paintRows(QPainter &p, const PaintContext &pc, const Song &s);
    void paintEmpty(QPainter &p, const PaintContext &pc);

    // OPEN FILE and RECORD, under DROP A SONG HERE (where they are on the
    // pad, 0..1 with y up), and the take while the microphone is recorded.
    static constexpr float kButtonY0 = 0.27f, kButtonY1 = 0.36f;
    static constexpr float kOpenX0 = 0.29f, kOpenX1 = 0.49f, kRecX0 = 0.51f, kRecX1 = 0.71f;
    bool m_recording = false;
    uint64_t m_recRead = 0;  // how far into the capture ring the take has been read
    float m_recLevel = 0;    // the take's level, for the meter under RECORD
    std::vector<float> m_take; // mono, at the capture's rate

    void emptyTap(const Touch &t, ControlContext &ctx) {
        if (m_importer.busy() || t.y < kButtonY0 || t.y > kButtonY1)
            return;
        if (t.x >= kOpenX0 && t.x <= kOpenX1)
            ctx.pickSound(QStringLiteral("Open a sound file"));
        else if (t.x >= kRecX0 && t.x <= kRecX1)
            toggleRecord(ctx);
    }

    void toggleRecord(ControlContext &ctx) {
        if (m_recording) {
            finishTake(ctx);
            return;
        }
        const int in = ctx.studio().input;
        if (in != StudioView::InputLive) {
            ctx.notice(in == StudioView::InputConnecting
                           ? QStringLiteral("The microphone is still connecting: RECORD again in a moment")
                           : QStringLiteral("No microphone: plug one in (or let tenfold use it), then RECORD"));
            return;
        }
        Capture &cap = m_bank->capture;
        m_take.clear();
        m_take.reserve(size_t(cap.rate.load(std::memory_order_relaxed) * 60));
        m_recRead = cap.written.load(std::memory_order_acquire);
        cap.peak.store(0, std::memory_order_relaxed);
        cap.on.store(true, std::memory_order_release);
        m_recording = true;
        m_recLevel = 0;
        ctx.notice(QStringLiteral("Recording the microphone · RECORD again to stop"));
    }

    // What the engine has written since the last look, into the take.
    void drainTake() {
        Capture &cap = m_bank->capture;
        const uint64_t w = cap.written.load(std::memory_order_acquire);
        uint64_t r = m_recRead;
        if (w - r > Capture::kSize) // fallen a whole ring behind: the oldest is gone
            r = w - Capture::kSize;
        const size_t limit = size_t(kMaxSeconds * cap.rate.load(std::memory_order_relaxed));
        for (; r < w && m_take.size() < limit; ++r)
            m_take.push_back(cap.ring[r & (Capture::kSize - 1)]);
        m_recRead = w;
        m_recLevel = std::max(m_recLevel * 0.8f, cap.peak.exchange(0, std::memory_order_relaxed));
    }

    // RECORD again: the take is written as a WAV beside tenfold's other
    // recordings and read in as if it had been dropped on the window.
    void finishTake(ControlContext &ctx) {
        Capture &cap = m_bank->capture;
        cap.on.store(false, std::memory_order_release);
        drainTake();
        m_recording = false;
        const float rate = cap.rate.load(std::memory_order_relaxed);
        if (m_take.size() < size_t(0.3 * rate)) {
            m_take.clear();
            ctx.notice(QStringLiteral("Nothing recorded: RECORD, play or sing, then RECORD again"));
            return;
        }
        // Beside tenfold's recordings (TENFOLD_SAMPLES_DIR elsewhere, for the tests).
        QString dir = qEnvironmentVariable("TENFOLD_SAMPLES_DIR");
        if (dir.isEmpty()) {
            dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
            if (dir.isEmpty())
                dir = QDir::homePath();
            dir += QStringLiteral("/tenfold/samples");
        }
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/sample-") +
                             QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".wav");
        {
            WavWriter wav;
            if (!wav.open(path.toStdString(), int(std::lround(rate)))) {
                m_take.clear();
                ctx.notice(QStringLiteral("Can't write %1").arg(path));
                return;
            }
            std::vector<float> both(m_take.size() * 2);
            for (size_t i = 0; i < m_take.size(); ++i)
                both[2 * i] = both[2 * i + 1] = m_take[i];
            wav.write(both.data(), m_take.size());
            wav.close();
        }
        m_take.clear();
        m_take.shrink_to_fit();
        dropped(path, ctx);
    }

    std::shared_ptr<Bank> m_bank;
    Importer m_importer;
    Song m_songs[kSounds + 1];
    int m_stemWanted[kSounds + 1] = {};
    int m_current = 0, m_wanted = 0;
    int m_loopPlaying = -1;
    uint32_t m_loopTakes = 0;
    int m_serial = 0;
    std::vector<Retired> m_retired;
    std::array<Holder, 12> m_holders{};
    std::vector<int> m_spent; // fingers down that have done their one thing
    Frame m_frame;
    bool m_keyMatch = true, m_overdub = false;
    float m_tone = 0;
    int m_nextId = 0;
    int m_touched = -1; // the chop last touched, of the song showing
    int m_plays = 0, m_trimsMade = 0;
    console::Hotspots m_spots;
};

bool Page::click(QPointF at, ControlContext &ctx) {
    const int id = m_spots.at(at);
    if (id < 0)
        return false;
    if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
        return true;
    if (id >= SpotStem && id < SpotStem + int(kStems)) {
        setStem(id - SpotStem, ctx);
        return true;
    }
    if (id > SpotSong && id <= SpotSong + kSounds) {
        show(id - SpotSong, ctx);
        return true;
    }
    switch (id) {
    case SpotStems: separate(ctx); return true;
    case SpotPageUp: turn(-1, ctx); return true;
    case SpotPageDown: turn(1, ctx); return true;
    case SpotSmaller: resize(-1, ctx); return true;
    case SpotLarger: resize(1, ctx); return true;
    case SpotTuneDown: tune(-1, ctx); return true;
    case SpotTuneUp: tune(1, ctx); return true;
    case SpotKeyMatch: toggleKeyMatch(ctx); return true;
    case SpotTempo: giveTempo(ctx, true); return true;
    case SpotHalf: regrid(0.5, ctx); return true;
    case SpotDouble: regrid(2.0, ctx); return true;
    case SpotBar: shiftBar(ctx); return true;
    case SpotReset: reset(ctx); return true;
    case SpotOverdub: toggleOverdub(ctx); return true;
    case SpotDarker: tone(-1, ctx); return true;
    case SpotBrighter: tone(1, ctx); return true;
    default: return false;
    }
}

void Page::paintEmpty(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QRectF pad = pc.pad;
    const bool busy = m_importer.busy();
    const qreal u = pad.height() / 600.0;
    if (busy) {
        const QString stage = m_importer.stage();
        c::text(p, QRectF(pad.left(), pad.center().y() - 60 * u, pad.width(), 44 * u), stage.isEmpty() ? QStringLiteral("READING") : stage,
                pc.color, int(30 * u), true, Qt::AlignCenter);
        c::gauge(p, QRectF(pad.center().x() - 180 * u, pad.center().y() + 4 * u, 360 * u, 14 * u), m_importer.progress(), pc.color, 24);
        return;
    }
    c::text(p, QRectF(pad.left(), pad.center().y() - 86 * u, pad.width(), 50 * u), QStringLiteral("DROP A SONG HERE"), pc.color,
            int(40 * u), true, Qt::AlignCenter);
    c::text(p, QRectF(pad.left(), pad.center().y() - 26 * u, pad.width(), 30 * u),
            QStringLiteral("a sound file dragged onto the window · or Ctrl+V with a link to a song copied"), c::alpha(pc.color, 0.8f),
            int(15 * u), false, Qt::AlignCenter);
    c::text(p, QRectF(pad.left(), pad.center().y() + 14 * u, pad.width(), 30 * u),
            QStringLiteral("it is cut on its own beat and plays in the studio's time: tap a chop, hold to repeat it"),
            c::alpha(pc.color, 0.55f), int(13 * u), false, Qt::AlignCenter);
    // Or a file picked, or the microphone recorded.
    const QRectF open = pc.rect(kOpenX0, kButtonY0, kOpenX1, kButtonY1);
    const QRectF rec = pc.rect(kRecX0, kButtonY0, kRecX1, kButtonY1);
    c::button(p, open, QStringLiteral("OPEN FILE"), pc.color, false);
    if (m_recording) {
        const float rate = m_bank->capture.rate.load(std::memory_order_relaxed);
        const int secs = int(double(m_take.size()) / std::max(1.0f, rate));
        c::button(p, rec, QStringLiteral("STOP  %1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')), c::red(), true);
        c::gauge(p, QRectF(rec.left(), rec.bottom() + 8 * u, rec.width(), 10 * u), std::min(1.0f, m_recLevel), c::red(), 16);
    } else {
        c::button(p, rec, QStringLiteral("RECORD"), pc.color, false);
    }
}

void Page::paintRows(QPainter &p, const PaintContext &pc, const Song &s) {
    namespace c = console;
    const Sound &snd = *s.sound;
    const Layout &lay = s.layout;
    const Pcm *pcm = snd.pcm[s.stem].load(std::memory_order_acquire);
    if (!pcm)
        pcm = snd.pcm[Mix].load(std::memory_order_acquire);
    const QColor col = pc.color;
    const int down = (((s.marks.downbeat + s.barShift) % 4) + 4) % 4;
    const double first = snd.beatOf(0), last = snd.beatOf(double(snd.frames));
    // What sounds, by the beats of the song: each voice of the engine
    // playing this song, and where it is.
    struct Sounding {
        double start, end, pos;
        bool held, loop;
    };
    std::vector<Sounding> sounding;
    for (int i = 0; i < kVoices; ++i) {
        const float *d = pc.view->data + pub::First + i * pub::Per;
        if (int(d[pub::Handle]) == m_current)
            sounding.push_back({double(d[pub::Start]), double(d[pub::End]), double(d[pub::Pos]), (int(d[pub::Flags]) & 1) != 0,
                                (int(d[pub::Flags]) & 2) != 0});
    }
    for (int r = 0; r < kRows; ++r) {
        const int row = s.page * kRows + r;
        const QRectF box = pc.rect(0, 1.0f - float(r + 1) / kRows, 1, 1.0f - float(r) / kRows).adjusted(3, 3, -3, -3);
        if (row >= lay.rows) {
            p.fillRect(box, c::alpha(col, 0.02f));
            continue;
        }
        const double b0 = lay.origin + double(row) * lay.rowBeats;
        auto xOf = [&](double beat) { return box.left() + (beat - b0) / lay.rowBeats * box.width(); };
        // The row's chops as cells: lit while they sound, brighter under
        // a finger.
        for (int i = lay.rowFirst[size_t(row)]; i < lay.rowFirst[size_t(row + 1)]; ++i) {
            const Chop &ch = lay.chops[size_t(i)];
            const QRectF cell(xOf(ch.at / 2.0), box.top(), (ch.length / 2.0) / lay.rowBeats * box.width(), box.height());
            bool fingered = false;
            for (const Holder &h : m_holders)
                fingered |= h.used && h.handle == m_current && h.key == ch.key();
            float lit = fingered ? 0.3f : 0.0f;
            if (pc.hover && !fingered) {
                const int hr = s.page * kRows + std::clamp(int((1.0f - pc.hoverY) * kRows), 0, kRows - 1);
                if (hr == row && pc.hoverX >= ch.x0 && pc.hoverX < ch.x1)
                    lit = std::max(lit, 0.1f);
            }
            p.fillRect(cell.adjusted(1, 0, -1, 0), c::alpha(col, 0.05f + lit));
            if (i == m_touched) {
                p.setPen(QPen(c::alpha(col, 0.9f), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRect(cell.adjusted(1.5, 1.5, -1.5, -1.5));
            }
            // A chop whose start or end was slid shows where they are now.
            const auto it = s.trims.find(ch.key());
            if (it != s.trims.end()) {
                float ps, pe;
                played(ch, it->second, ps, pe);
                const qreal xs = std::clamp(xOf(ps), box.left(), box.right()), xe = std::clamp(xOf(pe), box.left(), box.right());
                p.setPen(QPen(c::amber(), 2));
                p.drawLine(QPointF(xs, cell.top() + 2), QPointF(xs, cell.bottom() - 2));
                p.drawLine(QPointF(xs, cell.top() + 2), QPointF(xs + 6, cell.top() + 2));
                p.drawLine(QPointF(xe, cell.top() + 2), QPointF(xe, cell.bottom() - 2));
                p.drawLine(QPointF(xe, cell.bottom() - 2), QPointF(xe - 6, cell.bottom() - 2));
            }
        }
        // The wave, a column a pixel.
        if (pcm && !pcm->peaks.empty()) {
            const int w = std::max(1, int(box.width()));
            const qreal mid = box.center().y(), half = box.height() * 0.46;
            QVector<QLineF> lines, bright;
            lines.reserve(w);
            const int64_t hops = int64_t(pcm->peaks.size() / 2);
            for (int x = 0; x < w; ++x) {
                const double ba = b0 + double(x) / w * lay.rowBeats, bb = b0 + double(x + 1) / w * lay.rowBeats;
                if (bb <= first || ba >= last)
                    continue;
                const int64_t h0 = std::clamp(int64_t(snd.at(ba) / kPeakHop), int64_t(0), hops - 1);
                const int64_t h1 = std::clamp(int64_t(snd.at(bb) / kPeakHop), h0, hops - 1);
                int lo = 127, hi = -128;
                for (int64_t h = h0; h <= h1; ++h) {
                    lo = std::min(lo, int(pcm->peaks[size_t(2 * h)]));
                    hi = std::max(hi, int(pcm->peaks[size_t(2 * h + 1)]));
                }
                const QLineF line(box.left() + x + 0.5, mid - half * hi / 127.0, box.left() + x + 0.5, mid - half * lo / 127.0 + 1);
                bool on = false;
                for (const Sounding &v : sounding)
                    on |= ba >= v.start && ba < v.end;
                (on ? bright : lines).push_back(line);
            }
            p.setPen(QPen(c::alpha(col, 0.55f), 1));
            p.drawLines(lines);
            p.setPen(QPen(col, 1));
            p.drawLines(bright);
        }
        // The cuts: bar lines whole and bright, the chops between them
        // fainter; the bar's number at each bar line.
        for (int b = 0; b <= lay.rowBeats; ++b) {
            const int beat = int(b0) + b;
            if (((beat - down) % 4 + 4) % 4 != 0)
                continue;
            const qreal x = xOf(beat);
            p.setPen(QPen(c::alpha(col, 0.5f), 1));
            p.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
            if (b < lay.rowBeats && beat + 4 > first && beat < last)
                c::text(p, QRectF(x + 4, box.top() + 1, 60, 13), QString::number((beat - down) / 4 + (down > 0 ? 2 : 1)),
                        c::alpha(col, 0.7f), 10, true);
        }
        for (int i = lay.rowFirst[size_t(row)]; i < lay.rowFirst[size_t(row + 1)]; ++i) {
            const Chop &ch = lay.chops[size_t(i)];
            const qreal x = xOf(ch.at / 2.0);
            p.setPen(QPen(c::alpha(col, 0.3f), 1));
            p.drawLine(QPointF(x, box.top() + box.height() * 0.2), QPointF(x, box.bottom() - box.height() * 0.2));
        }
        // Where each voice is.
        for (const Sounding &v : sounding) {
            if (v.pos < b0 || v.pos >= b0 + lay.rowBeats)
                continue;
            const qreal x = xOf(v.pos);
            p.setPen(QPen(v.loop ? c::alpha(col, 0.85f) : QColor(255, 255, 255), 2));
            p.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
        }
    }
}

void Page::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    c::pad(p, pc, QStringLiteral("SAMPLER"));
    const QColor col = pc.color;
    const Song *s = current();
    m_spots.clear();
    if (!s || !s->sound) {
        paintEmpty(p, pc);
    } else {
        paintRows(p, pc, *s);
        const Sound &snd = *s->sound;
        // The song's name, and what is done to it to fit the studio.
        qreal x = c::variation(p, pc, s->name.left(34));
        if (snd.matched) {
            const double fold = foldFor(snd.bpm, pc.studio->bpm);
            const double bpm = snd.bpm * fold;
            const bool same = std::fabs(bpm - pc.studio->bpm) < 0.05;
            x = c::tag(p, QPointF(x, c::tabY(pc)),
                       same ? QStringLiteral("%1 BPM").arg(pc.studio->bpm, 0, 'f', 1)
                            : QStringLiteral("%1 → %2 BPM").arg(snd.bpm, 0, 'f', 1).arg(pc.studio->bpm, 0, 'f', 1),
                       same ? c::alpha(col, 0.8f) : col, 11)
                    .right() + 6;
        } else {
            x = c::tag(p, QPointF(x, c::tabY(pc)), QStringLiteral("NO STEADY BEAT"), c::alpha(col, 0.6f), 11).right() + 6;
        }
        if (snd.keyKnown) {
            Tuning studio;
            studio.set(pc.studio->root, pc.studio->family, pc.studio->brightness, pc.studio->just);
            const int shift = (m_keyMatch ? keyShift(studio, snd) : 0) + int(s->tune);
            QString key = QStringLiteral("%1 %2").arg(QLatin1String(noteName(snd.keyRoot)), snd.keyMinor ? QStringLiteral("MIN") : QStringLiteral("MAJ"));
            if (shift != 0)
                key += QStringLiteral(" %1%2").arg(shift > 0 ? QStringLiteral("+") : QString()).arg(shift);
            x = c::tag(p, QPointF(x, c::tabY(pc)), key, shift != 0 ? col : c::alpha(col, 0.8f), 11).right() + 6;
        } else if (s->tune != 0) {
            x = c::tag(p, QPointF(x, c::tabY(pc)), QStringLiteral("TUNE %1%2").arg(s->tune > 0 ? "+" : "").arg(int(s->tune)), col, 11).right() + 6;
        }
        if (s->stem != Mix)
            x = c::tag(p, QPointF(x, c::tabY(pc)), QString::fromLatin1(stemName(s->stem)), col, 11).right() + 6;
        if (m_importer.busy()) {
            const QString stage = m_importer.stage();
            c::tag(p, QPointF(pc.pad.center().x(), pc.pad.bottom() - 16),
                   QStringLiteral("%1 %2%").arg(stage.isEmpty() ? QStringLiteral("WORKING") : stage).arg(int(m_importer.progress() * 100)),
                   std::fmod(pc.time, 0.6) < 0.3 ? col : c::alpha(col, 0.7f), 13, true);
        }
    }
    // The fingers, each saying what its slide does.
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        QString label;
        for (const Holder &h : m_holders) {
            if (!h.used)
                continue;
            // (Held long enough to slide, it says so.)
            if (h.src == t.id && h.second < 0 && h.moved)
                label = QStringLiteral("START %1").arg(double(trimOf(h).start), 0, 'f', 2);
            else if (h.src == t.id && h.second < 0 && pc.time - h.t0 >= kTrimHold)
                label = QStringLiteral("START ↔");
            else if (h.second == t.id)
                label = h.moved ? QStringLiteral("END %1").arg(double(trimOf(h).end), 0, 'f', 2) : QStringLiteral("END ↔");
        }
        c::finger(p, pc, t, col, label, 0.4f);
    }
    if (pc.view->recording >= 0)
        c::tag(p, QPointF(pc.pad.center().x(), c::tabY(pc)),
               pc.view->takeBeats > 0
                   ? QStringLiteral("%1 BEATS INTO SLOT %2 IN %3 · SPACE CALLS IT OFF")
                         .arg(pc.view->takeBeats, 0, 'f', 0)
                         .arg(pc.view->recording + 1)
                         .arg(std::max(0.0, pc.view->takeAt - pc.beat), 0, 'f', 1)
                   : QStringLiteral("LOOPING INTO SLOT %1 · SPACE CALLS IT OFF").arg(pc.view->recording + 1),
               std::fmod(pc.time, 0.5) < 0.25 ? c::red() : c::alpha(c::red(), 0.6f), 13, true);
    c::beats(p, pc, col);

    // Under the pad. Row 0: the loops, the songs loaded, the stems.
    c::clips(p, c::bandRow(pc, 0, 0, 0.4), *pc.view, col, pc.time, &m_spots);
    int loaded = 0;
    for (int h = 1; h <= kSounds; ++h)
        loaded += m_songs[h].used;
    if (loaded > 1) {
        const QRectF songs = c::bandRow(pc, 0, 0.41, 0.53);
        static const char *keys[] = {"", "Q", "W", "E", "R"};
        const qreal w = songs.width() / kSounds;
        for (int h = 1; h <= kSounds; ++h)
            if (m_songs[h].used)
                c::chip(p, pc, QRectF(songs.left() + (h - 1) * w, songs.top(), w - 3, songs.height()),
                        QString::fromLatin1(keys[h]), h == m_current, &m_spots, SpotSong + h);
    }
    {
        // The tempo as heard, halved or doubled.
        const QRectF fix = c::bandRow(pc, 0, 0.54, 0.64);
        c::chip(p, pc, QRectF(fix.left(), fix.top(), fix.width() / 2 - 2, fix.height()), QStringLiteral("÷2 H"), false, &m_spots, SpotHalf);
        c::chip(p, pc, QRectF(fix.center().x() + 1, fix.top(), fix.width() / 2 - 2, fix.height()), QStringLiteral("×2 D"), false, &m_spots, SpotDouble);
        const QRectF stems = c::bandRow(pc, 0, 0.65, 0.88);
        const qreal w = stems.width() / int(kStems);
        for (int st = 0; st < kStems; ++st) {
            const QRectF r(stems.left() + st * w, stems.top(), w - 3, stems.height());
            const bool there = s && s->sound && (st == Mix || s->stems);
            if (there) {
                c::chip(p, pc, r, QString::fromLatin1(stemName(st)), s->stem == st, &m_spots, SpotStem + st);
            } else {
                p.setPen(QPen(c::alpha(col, 0.25f), 1));
                p.setBrush(Qt::NoBrush);
                p.drawRect(r);
                c::text(p, r, QString::fromLatin1(stemName(st)), c::alpha(col, 0.3f), 11, true, Qt::AlignCenter);
                m_spots.add(r, SpotStem + st);
            }
        }
        const bool separating = m_importer.separating();
        c::chip(p, pc, c::bandRow(pc, 0, 0.885, 1),
                separating ? QStringLiteral("%1%").arg(int(m_importer.progress() * 100)) : QStringLiteral("STEMS S"),
                separating && std::fmod(pc.time, 0.6) < 0.3, &m_spots, SpotStems);
    }
    // Row 1: the page of the song, the cut, the tuning, and the rest.
    const QRectF row = c::bandRow(pc, 1, 0, 1);
    static const double cuts[] = {0, 0.04, 0.115, 0.155, 0.2, 0.285, 0.33, 0.365, 0.42, 0.455, 0.52, 0.6, 0.665, 0.765, 0.865, 1};
    auto cell = [&](int i) {
        return QRectF(row.left() + row.width() * cuts[i], row.top(), row.width() * (cuts[i + 1] - cuts[i]) - 4, row.height());
    };
    auto label = [&](int i, const QString &text) { c::text(p, cell(i), text, col, 11, true, Qt::AlignCenter); };
    c::chip(p, pc, cell(0), QStringLiteral("↑"), false, &m_spots, SpotPageUp);
    label(1, s && s->sound ? QStringLiteral("PAGE %1/%2").arg(s->page + 1).arg(s->layout.pages()) : QStringLiteral("PAGE"));
    c::chip(p, pc, cell(2), QStringLiteral("↓"), false, &m_spots, SpotPageDown);
    c::chip(p, pc, cell(3), QStringLiteral("⇧←"), false, &m_spots, SpotSmaller);
    label(4, s ? QString::fromLatin1(sizeName(s->size)) : QStringLiteral("CHOPS"));
    c::chip(p, pc, cell(5), QStringLiteral("⇧→"), false, &m_spots, SpotLarger);
    c::chip(p, pc, cell(6), QStringLiteral(","), false, &m_spots, SpotTuneDown);
    label(7, s && s->tune != 0 ? QStringLiteral("%1%2").arg(s->tune > 0 ? "+" : "").arg(int(s->tune)) : QStringLiteral("TUNE"));
    c::chip(p, pc, cell(8), QStringLiteral("."), false, &m_spots, SpotTuneUp);
    c::chip(p, pc, cell(9), QStringLiteral("KEY §"), m_keyMatch, &m_spots, SpotKeyMatch);
    c::chip(p, pc, cell(10), QStringLiteral("TEMPO T"), false, &m_spots, SpotTempo);
    c::chip(p, pc, cell(11), QStringLiteral("BAR B"), s && s->barShift != 0, &m_spots, SpotBar);
    c::chip(p, pc, cell(12), QStringLiteral("BACK DEL"), false, &m_spots, SpotReset);
    c::chip(p, pc, cell(13), QStringLiteral("DUB F10"), m_overdub, &m_spots, SpotOverdub);
    {
        // Tone: − and + in one cell's halves.
        const QRectF r = cell(14);
        c::chip(p, pc, QRectF(r.left(), r.top(), r.width() / 2 - 2, r.height()), QStringLiteral("−"), m_tone < 0, &m_spots, SpotDarker);
        c::chip(p, pc, QRectF(r.center().x() + 2, r.top(), r.width() / 2 - 2, r.height()), QStringLiteral("+"), m_tone > 0, &m_spots, SpotBrighter);
    }
}

}

}

std::string sampler::cacheFolder(const std::string &source) {
    return sampler::cacheFor(QString::fromStdString(source)).toStdString();
}

PartSpec makeSampler() {
    PartSpec s;
    s.name = QStringLiteral("Sampler");
    s.blurb = QStringLiteral("a song, chopped");
    s.color = QColor("#5C9DFF");
    auto bank = std::make_shared<sampler::Bank>();
    s.engine = std::make_unique<sampler::Engine>(bank);
    s.control = std::make_unique<sampler::Page>(bank);
    // What it plays is a song with its own harmony: the same chops over a
    // new chord are the same playing.
    s.followsChords = false;
    // A record already has its room and its own dynamics: a little of the
    // studio's reverb, and half the pump the synths get.
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.06f, 0.0f, 0.3f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
