// The FX page: risers that peak on the bar line, drops that fire on the
// sample, and a mix that is always handed back.

#include "testkit.h"

#include <QJsonArray>
#include <functional>

#include "../src/risers.h"
#include "parts.h"

using namespace tf;

namespace {

constexpr int kFx = 6; // the FX page's part
constexpr double kBpm = 124.0;
double timeOf(double beat) { return beat * 60.0 / kBpm; }

// Where the five rows of the build field are (1, 2, 4, 8, 16 bars) and the
// columns (noise, pitch, saw, roll, swell), in pad terms.
float rowY(int row) { return 0.2f + 0.16f * (float(row) + 0.5f); }
float columnX(int kind) { return (float(kind) + 0.5f) / 5.0f; }
float hitX(int hit) { return (float(hit) + 0.5f) / 8.0f; }

std::string fmt(const char *f, double a, double b = 0, double c = 0, double d = 0, double e = 0) {
    char buf[512];
    std::snprintf(buf, sizeof buf, f, a, b, c, d, e);
    return buf;
}

struct VoiceInfo {
    int kind;
    double start, end;
    int state;
    float note;
};

std::vector<VoiceInfo> voicesOf(const StudioView &v) {
    std::vector<VoiceInfo> out;
    const float *d = v.parts[kFx].data;
    for (int k = 0; k < risers::pub::MaxVoices; ++k) {
        const float *o = d + risers::pub::Voices + risers::pub::Stride * k;
        if (o[risers::pub::VKind] < 0)
            continue;
        out.push_back({int(o[risers::pub::VKind]), o[risers::pub::VStart], o[risers::pub::VEnd],
                       int(o[risers::pub::VState]), o[risers::pub::VNote]});
    }
    return out;
}

double rmsDb(const testkit::Take &take, double from, double to) { return 20 * std::log10(take.rms(from, to) + 1e-9); }

// Kick, snare, kick, snare half a second apart on the drums page (its near
// row of pads: kick, snare, clap, hat), then Space where the next kick would
// land: a one-bar loop at 120 BPM.
const char *kBeat = "page 0 1\n"
                    "tap 0.5 0.12 0.12 p=0.7\ntap 1.0 0.38 0.12 p=0.6\ntap 1.5 0.12 0.12 p=0.7\ntap 2.0 0.38 0.12 p=0.6\n"
                    "key 2.5 Space\n";

// The peak of the mix below 150 Hz between two times: the kick's weight.
double lowPeakDb(const testkit::Take &take, double from, double to) {
    Svf a, b;
    a.set(150, 0.7f, 48000);
    b.set(150, 0.7f, 48000);
    double peak = 0;
    const size_t start = size_t(std::max(0.0, from - 0.05) * 48000), end = size_t(to * 48000);
    for (size_t i = start; i < end && i < take.left.size(); ++i) {
        const float y = b.step(a.step(0.5f * (take.left[i] + take.right[i])).low).low;
        if (i >= size_t(from * 48000))
            peak = std::max(peak, double(std::fabs(y)));
    }
    return 20 * std::log10(peak + 1e-9);
}

// The touch-clock time of a beat, from a view.
double timeOfBeat(const StudioView &v, double beat) { return v.time + (beat - v.beat) * 60.0 / v.bpm; }

}

// The FX engine on its own at 124 BPM, for what the page's commands do.
struct FxAudio : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return double(tr.sample) / double(tr.rate); }
    int offsetOf(double t, int n) const override {
        const int o = int((t - now()) * double(tr.rate));
        return o < 0 ? 0 : std::min(o, n - 1);
    }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// One sound of one set, played on the engine alone at 124 BPM: a riser of
// one bar aimed at beat 4 (heard up to its line), or a hit held for 0.3 s
// (heard for 2.5 s). The note it sounded at the end of the riser, too.
struct Heard {
    std::vector<float> l, r;
    float note = 0;
};
Heard playSolo(int set, int kind) {
    PartSpec spec = makeRisers();
    auto &e = *spec.engine;
    FxAudio ctx;
    ctx.tr.rate = 48000;
    ctx.tr.bpm = 124;
    ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
    e.init(48000);
    Command c;
    c.type = Command::On;
    c.id = 5;
    c.i[0] = kind;
    c.i[1] = 1;
    c.i[3] = set;
    c.f[0] = 0.6f;
    c.f[2] = 0.7f;
    c.f[5] = 4.0f;
    e.command(c, ctx);
    const bool riser = risers::isRiser(kind);
    const int blocks = int((riser ? 4.0 * 60.0 / 124.0 - 0.01 : 2.5) * 48000 / kBlock);
    Heard out;
    float data[kPartData];
    for (int b = 0; b < blocks; ++b) {
        ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
        if (b == int(0.3 * 48000 / kBlock)) {
            Command off;
            off.type = Command::Off;
            off.id = 5;
            e.command(off, ctx);
        }
        float l[kBlock] = {}, r[kBlock] = {};
        e.render(l, r, kBlock, ctx);
        out.l.insert(out.l.end(), l, l + kBlock);
        out.r.insert(out.r.end(), r, r + kBlock);
        ctx.tr.sample += kBlock;
        e.publish(data);
        if (data[risers::pub::Voices + risers::pub::VKind] == float(kind))
            out.note = data[risers::pub::Voices + risers::pub::VNote];
    }
    return out;
}

// A hit held for a while in a set, with the notes it played on the way,
// one per block where it changed.
std::vector<float> notesHeld(int set, int kind, double seconds) {
    PartSpec spec = makeRisers();
    auto &e = *spec.engine;
    FxAudio ctx;
    ctx.tr.rate = 48000;
    ctx.tr.bpm = 124;
    ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
    e.init(48000);
    Command c;
    c.type = Command::On;
    c.id = 5;
    c.i[0] = kind;
    c.i[3] = set;
    c.f[0] = 0.6f;
    c.f[2] = 0.7f;
    e.command(c, ctx);
    std::vector<float> notes;
    float data[kPartData];
    for (int b = 0; b < int(seconds * 48000 / kBlock); ++b) {
        ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
        float l[kBlock] = {}, r[kBlock] = {};
        e.render(l, r, kBlock, ctx);
        ctx.tr.sample += kBlock;
        e.publish(data);
        const float note = data[risers::pub::Voices + risers::pub::VNote];
        if (data[risers::pub::Voices + risers::pub::VKind] == float(kind) && (notes.empty() || notes.back() != note))
            notes.push_back(note);
    }
    return notes;
}

// The FX engine alone at 124 BPM, driven by hand: commands sent when a
// test says, and every block what it played and what it published.
struct Rig {
    PartSpec spec = makeRisers();
    FxAudio ctx;
    float data[kPartData] = {};
    std::vector<float> l, r;
    Rig() {
        ctx.tr.rate = 48000;
        ctx.tr.bpm = 124;
        ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
        spec.engine->init(48000);
    }
    void send(Command c) { spec.engine->command(c, ctx); }
    // A finger landing: a riser of `bars` aimed four beats on, or a hit.
    void on(int id, int kind, int set, float tone = 0, float intensity = 0.7f, float motion = 0) {
        Command c;
        c.type = Command::On;
        c.id = id;
        c.i[0] = kind;
        c.i[1] = 1;
        c.i[3] = set;
        c.f[0] = 0.6f;
        c.f[1] = tone;
        c.f[2] = intensity;
        c.f[3] = motion;
        c.f[5] = 4.0f;
        send(c);
    }
    void move(int id, float tone, float intensity = 0.7f, float motion = 0) {
        Command c;
        c.type = Command::Move;
        c.id = id;
        c.f[0] = 0.6f;
        c.f[1] = tone;
        c.f[2] = intensity;
        c.f[3] = motion;
        send(c);
    }
    void off(int id) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        send(c);
    }
    double seconds() const { return double(ctx.tr.sample) / 48000.0; }
    // Plays on for a while; `each` sees what was published after every block.
    void run(double secs, const std::function<void(const float *)> &each = {}) {
        for (int b = 0; b < int(secs * 48000 / kBlock); ++b) {
            ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
            float bl[kBlock] = {}, br[kBlock] = {};
            spec.engine->render(bl, br, kBlock, ctx);
            l.insert(l.end(), bl, bl + kBlock);
            r.insert(r.end(), br, br + kBlock);
            ctx.tr.sample += kBlock;
            spec.engine->publish(data);
            if (each)
                each(data);
        }
    }
    // The published note of the first voice of a kind (−1 if none sounds).
    float note(int kind) const {
        for (int w = 0; w < risers::pub::MaxVoices; ++w) {
            const float *o = data + risers::pub::Voices + risers::pub::Stride * w;
            if (o[risers::pub::VKind] == float(kind))
                return o[risers::pub::VNote];
        }
        return -1;
    }
    int voices(int kind) const {
        int count = 0;
        for (int w = 0; w < risers::pub::MaxVoices; ++w)
            count += data[risers::pub::Voices + risers::pub::Stride * w + risers::pub::VKind] == float(kind);
        return count;
    }
};

// The notes a kind played while it ran, one each time it changed.
std::vector<float> notesOf(Rig &rig, int kind, double secs) {
    std::vector<float> notes;
    rig.run(secs, [&](const float *) {
        const float n = rig.note(kind);
        if (n > 0 && (notes.empty() || notes.back() != n))
            notes.push_back(n);
    });
    return notes;
}

bool inKeyNote(const Harmony &h, float note, float within = 0.5f) {
    for (int d = -21; d < 42; ++d)
        if (std::fabs(note - float(h.midi(d, 0))) < within)
            return true;
    return false;
}
bool inSlendro(const Harmony &h, float note, float within = 0.02f) {
    for (int k = -15; k < 30; ++k)
        if (std::fabs(note - risers::slendro(h, k, 0)) < within)
            return true;
    return false;
}
// Whether a note is in a set's own scale, the key's or slendro's, give or
// take what a glide still has to go at the line.
bool inScaleOf(int set, const Harmony &h, float note) {
    return risers::palette(set).tuning == 1 ? inSlendro(h, note, 0.3f) : inKeyNote(h, note);
}

// RMS in dB of the mono mix over [t0, t1) seconds, after a low-pass (0: none).
double windowDb(const std::vector<float> &l, const std::vector<float> &r, double t0, double t1, float lowpass = 0) {
    Svf a, b;
    if (lowpass > 0) {
        a.set(lowpass, 0.7f, 48000);
        b.set(lowpass, 0.7f, 48000);
    }
    double e = 0;
    int count = 0;
    for (size_t i = 0; i < l.size() && double(i) < t1 * 48000; ++i) {
        float m = 0.5f * (l[i] + r[i]);
        if (lowpass > 0)
            m = b.step(a.step(m).low).low;
        if (double(i) >= t0 * 48000) {
            e += double(m) * m;
            ++count;
        }
    }
    return 10 * std::log10(e / std::max(1, count) + 1e-20);
}

// The level of one frequency in a window (both sides summed), in dB: a
// single bin of a Fourier transform, Hann-windowed, so a tone's own line is
// told from its neighbours a few tens of hertz away.
double toneDb(const std::vector<float> &l, const std::vector<float> &r, double hz, double t0, double t1) {
    const size_t a = size_t(t0 * 48000), b = std::min(l.size(), size_t(t1 * 48000));
    double re = 0, im = 0, wsum = 0;
    for (size_t i = a; i < b; ++i) {
        const double u = double(i - a) / double(b - a);
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * u);
        const double x = 0.5 * (l[i] + r[i]) * w;
        const double ph = 2 * M_PI * hz * double(i) / 48000.0;
        re += x * std::cos(ph);
        im += x * std::sin(ph);
        wsum += w;
    }
    return 20 * std::log10(2 * std::sqrt(re * re + im * im) / std::max(wsum, 1.0) + 1e-12);
}

// What a sound is like: its level; its colour, as the energy in eight
// octave bands (63 Hz to 8 kHz) against its whole, so a sound only louder
// or softer has the same colour; and its envelope in quarters, against its
// loudest quarter. All in dB.
struct Features {
    double level = -200, bands[8] = {}, env[4] = {};
};
double bandsOf(const Heard &s, double *out) {
    double total = 0;
    for (size_t i = 0; i < s.l.size(); ++i)
        total += 0.25 * double(s.l[i] + s.r[i]) * (s.l[i] + s.r[i]);
    for (int k = 0; k < 8; ++k) {
        Svf a, b;
        const float hz = 62.5f * float(1 << k);
        a.set(hz, 1.4f, 48000);
        b.set(hz, 1.4f, 48000);
        double e = 0;
        for (size_t i = 0; i < s.l.size(); ++i) {
            const double y = b.step(a.step(0.5f * (s.l[i] + s.r[i])).band).band;
            e += y * y;
        }
        out[k] = 10 * std::log10((e + 1e-18) / (total + 1e-18));
    }
    return total;
}
Features featuresOf(const Heard &s) {
    Features f;
    const size_t n = s.l.size();
    f.level = 10 * std::log10(bandsOf(s, f.bands) / double(n) + 1e-18);
    double loudest = -200;
    for (int q = 0; q < 4; ++q) {
        double e = 0;
        for (size_t i = q * n / 4; i < (q + 1) * n / 4; ++i)
            e += 0.25 * (s.l[i] + s.r[i]) * (s.l[i] + s.r[i]);
        f.env[q] = 10 * std::log10(e / double(n / 4) + 1e-18);
        loudest = std::max(loudest, f.env[q]);
    }
    for (double &q : f.env)
        q -= loudest;
    return f;
}
// Whether two sounds would be told apart by more than how loud they are:
// their colour 3.5 dB apart across the bands that matter (within 40 dB of
// the strongest), or their shape 4 dB apart in some quarter.
bool differ(const Features &a, const Features &b) {
    double top = -200, sum = 0;
    int count = 0;
    for (int k = 0; k < 8; ++k)
        top = std::max({top, a.bands[k], b.bands[k]});
    for (int k = 0; k < 8; ++k)
        if (std::max(a.bands[k], b.bands[k]) > top - 40) {
            sum += std::fabs(std::max(a.bands[k], top - 40) - std::max(b.bands[k], top - 40));
            ++count;
        }
    bool env = false;
    for (int q = 0; q < 4; ++q)
        env |= std::fabs(std::max(a.env[q], -40.0) - std::max(b.env[q], -40.0)) > 4;
    return (count && sum / count > 3.5) || env;
}
// Energy above 10 kHz against the whole, in dB: two high-passes in a row.
double airOf(const Heard &s) {
    Svf a, b;
    a.set(10000, 0.7f, 48000);
    b.set(10000, 0.7f, 48000);
    double e = 0, total = 0;
    for (size_t i = 0; i < s.l.size(); ++i) {
        const float m = 0.5f * (s.l[i] + s.r[i]);
        const double y = b.step(a.step(m).high).high;
        e += y * y;
        total += double(m) * m;
    }
    return 10 * std::log10((e + 1e-18) / (total + 1e-18));
}
// The kinds that make a sound of their own.
const int kSounding[] = {risers::Noise,  risers::Pitch, risers::Saw,  risers::Roll, risers::Swell,
                         risers::Impact, risers::Sub,   risers::Down, risers::Zap,  risers::Stop};

class RisersTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenFingersStayInsideBudget newSetsStayInsideBudget")
private slots:
    void aLoopsFallDropsOnlyItsOwnLayersRisers() {
        // A riser from the hand and one replayed by a loop's second layer;
        // the loop's "every riser falls" drops its own, not the hand's. The
        // hand's falls everything.
        PartSpec spec = makeRisers();
        auto &e = *spec.engine;
        FxAudio ctx;
        ctx.tr.rate = 48000;
        ctx.tr.bpm = 124;
        ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
        e.init(48000);
        auto run = [&](double seconds) {
            for (int b = 0; b < int(seconds * 48000 / kBlock); ++b) {
                ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
                float l[kBlock] = {}, r[kBlock] = {};
                e.render(l, r, kBlock, ctx);
                ctx.tr.sample += kBlock;
            }
        };
        auto riser = [&](int id, int kind) {
            Command c;
            c.type = Command::On;
            c.id = id;
            c.i[0] = kind;
            c.i[1] = 2;
            c.f[0] = 0.6f;
            c.f[2] = 0.7f;
            c.f[5] = 8;
            e.command(c, ctx);
        };
        const int layer1 = (1 << 30) | (1 << 27);
        riser(5, risers::Noise);
        riser(layer1 | 7, risers::Pitch);
        run(0.3);
        Command fall;
        fall.type = risers::CmdFall;
        fall.id = layer1 | risers::kEveryone;
        e.command(fall, ctx);
        run(0.1);
        float data[kPartData] = {};
        e.publish(data);
        StudioView v;
        std::copy(std::begin(data), std::end(data), v.parts[kFx].data);
        int hand = -1, loop = -1;
        for (const auto &vi : voicesOf(v)) {
            if (vi.kind == risers::Noise)
                hand = vi.state;
            if (vi.kind == risers::Pitch)
                loop = vi.state;
        }
        QCOMPARE(hand, int(risers::Running));
        QVERIFY(loop >= int(risers::Falling));
        fall.id = risers::kEveryone;
        e.command(fall, ctx);
        run(0.1);
        e.publish(data);
        std::copy(std::begin(data), std::end(data), v.parts[kFx].data);
        for (const auto &vi : voicesOf(v))
            if (vi.kind == risers::Noise)
                QVERIFY(vi.state >= int(risers::Falling));
    }

    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 7\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    // A build lands on the phrase grid: from beat 1, a two-bar noise riser
    // aims at the 8-bar line (beat 32), always forward, and waits to come in
    // two bars before it. It grows into the line, the drop fires on it, and
    // the riser is gone after.
    void aRiserPeaksOnThePhraseLine() {
        const double drop = 32.0, tDrop = timeOf(drop);
        const std::string land = fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\n", columnX(0), rowY(1));
        std::vector<VoiceInfo> during;
        testkit::play(land + "end 3\n", [&](Session &s) { during = voicesOf(s.view()); });
        QCOMPARE(int(during.size()), 1);
        QCOMPARE(during[0].kind, int(risers::Noise));
        QCOMPARE(during[0].end, drop);
        QCOMPARE(during[0].start, drop - 8);
        QCOMPARE(during[0].state, int(risers::Waiting));

        StudioView after;
        const auto take = testkit::play(land + fmt("end %.3f\n", tDrop + 0.4), [&](Session &s) { after = s.view(); });
        QVERIFY(take.finite());
        const double last = rmsDb(take, tDrop - 0.06, tDrop - 0.003);
        QVERIFY2(last > rmsDb(take, tDrop - 3.8, tDrop - 3.74) + 6, qPrintable(QString::number(last)));
        for (double t = tDrop - 3.8; t < tDrop - 0.12; t += 0.06)
            QVERIFY2(rmsDb(take, t, t + 0.06) < last + 0.5, qPrintable(QString("louder at %1").arg(t)));
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), drop);
        for (const VoiceInfo &v : voicesOf(after))
            QVERIFY(!risers::isRiser(v.kind));
    }

    // Long risers are heard from their first bar: every 8- and 16-bar riser
    // is within 20 dB of its last beat a quarter of the way in.
    void longRisersAreAudibleEarly() {
        for (int row : {3, 4})
            for (int kind = 0; kind < risers::kRisers; ++kind) {
                if (row == 4 && kind != risers::Noise && kind != risers::Swell)
                    continue;
                // From beat 1, the 8-bar row aims at beat 32, the 16-bar row
                // at beat 64; both start at once.
                const double drop = row == 3 ? 32.0 : 64.0, start = timeOf(1.03), tDrop = timeOf(drop);
                const auto take = testkit::play(
                    fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(kind), rowY(row), tDrop - 0.01));
                const double quarter = start + 0.25 * (tDrop - start);
                const double early = rmsDb(take, quarter - 0.5, quarter + 0.5);
                const double peak = rmsDb(take, tDrop - timeOf(1.0), tDrop - 0.01);
                QVERIFY2(early > peak - 20, qPrintable(QString("%1 row %2: %3 vs %4").arg(risers::kindName(kind)).arg(row).arg(early).arg(peak)));
            }
    }

    // The ONE with the FX on it is louder than the beat's own ONE, in any
    // drop style, and the kick keeps its weight: the risers are gone 3 ms
    // before the line, the impact waits 20 ms and its boom 40 ms (ducked
    // under the kick), nothing low goes in a room, and the drop lifts the
    // master.
    void theDropAddsToTheKick() {
        for (int style = 0; style < 3; ++style) {
            std::string fx = std::string(kBeat) + "page 3.0 7\n";
            for (int k = 0; k < style; ++k)
                fx += fmt("key %.3f Shift+Right\n", 3.02 + 0.03 * k);
            fx += fmt("down 3.1 1 %.3f %.3f p=0.6\nup 3.6 1\n", columnX(0), rowY(2));
            std::vector<VoiceInfo> v;
            StudioView view;
            testkit::play(fx + "end 4\n", [&](Session &s) {
                v = voicesOf(s.view());
                view = s.view();
            });
            QCOMPARE(int(v.size()), 1);
            const double tDrop = timeOfBeat(view, v[0].end);
            const auto with = testkit::play(fx + fmt("end %.3f\n", tDrop + 1.0));
            const auto without = testkit::play(std::string(kBeat) + fmt("end %.3f\n", tDrop + 1.0));
            const double a = rmsDb(with, tDrop, tDrop + 0.04), b = rmsDb(without, tDrop, tDrop + 0.04);
            QVERIFY2(a > b, qPrintable(QString("style %1: with %2, beat alone %3").arg(style).arg(a).arg(b)));
            // The kick keeps its weight: its low peak within half a dB.
            const double la = lowPeakDb(with, tDrop, tDrop + 0.04), lb = lowPeakDb(without, tDrop, tDrop + 0.04);
            QVERIFY2(la > lb - 0.5, qPrintable(QString("style %1: low peak %2 against %3").arg(style).arg(la).arg(lb)));
            QVERIFY2(rmsDb(with, tDrop + 0.3, tDrop + 0.45) > rmsDb(without, tDrop + 0.3, tDrop + 0.45) - 1.0,
                     qPrintable(QString::number(rmsDb(with, tDrop + 0.3, tDrop + 0.45))));
            QVERIFY(with.peak() < 1.0);
        }
    }

    // The riser is gone by the ONE (only its room rings on, and that ducks
    // under a kick) and the impact comes 20 ms after it, leaving the kick's
    // peak alone at the limiter's ceiling.
    void theImpactWaitsOutTheKicksPeak() {
        // A one-bar swell landed at beat 25.1 enters at 28 and drops at 32.
        const double tDrop = timeOf(32.0);
        const auto take = testkit::play(
            fmt("page 0 7\ntap %.3f %.3f %.3f\nend %.3f\n", timeOf(25.1), columnX(4), rowY(0), tDrop + 0.5));
        auto peak = [&](double from, double to) {
            double p = 0;
            for (size_t i = size_t(from * 48000); i < size_t(to * 48000); ++i)
                p = std::max(p, double(std::fabs(take.left[i])));
            return p;
        };
        const double before = peak(tDrop - 0.02, tDrop - 0.004);
        QVERIFY2(peak(tDrop + 0.002, tDrop + 0.018) < 0.25 * before, qPrintable(QString::number(peak(tDrop + 0.002, tDrop + 0.018))));
        QVERIFY2(peak(tDrop + 0.021, tDrop + 0.04) > 0.3 * before, qPrintable(QString::number(peak(tDrop + 0.021, tDrop + 0.04))));
        QVERIFY(take.peak() < 1.0);
    }

    // During a build the page owns the master filter, drive and room; they
    // are still held a moment before the ONE and back where they were, exactly,
    // just after it. The lift on the ONE is back by the end of its bar.
    void theMasterIsHandedBack() {
        const double tDrop = timeOf(32.0);
        const std::string build =
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 0.6 3.0 1 %.3f %.3f\n", columnX(0), rowY(3), columnX(0), rowY(3) + 0.12);
        StudioView mid, late, after;
        testkit::play(build + fmt("end %.3f\n", tDrop - 1.0), [&](Session &s) { mid = s.view(); });
        QVERIFY2(mid.master[MasterFilter] > 0.6f, qPrintable(QString::number(mid.master[MasterFilter])));
        QVERIFY(mid.master[MasterDrive] > 0.02f);
        QVERIFY(mid.master[RoomSize] > 0.4f);
        testkit::play(build + fmt("end %.3f\n", tDrop - 0.2), [&](Session &s) { late = s.view(); });
        QVERIFY(late.master[MasterFilter] > 0.6f);
        testkit::play(build + fmt("up 3.5 1\nend %.3f\n", tDrop + 0.3), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[MasterDrive], 0.0f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        // The drop comes in a notch up, and eases back over its bar.
        QVERIFY(after.master[MasterGain] > 0.85f);
        testkit::play(build + fmt("up 3.5 1\nend %.3f\n", tDrop + timeOf(4.0) + 0.2), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterGain], 0.8f);
    }

    // Strip leaves the risers alone for the build's last bar; gap mutes
    // everything else for the last half beat, and the risers stop too. Both
    // bring the mix back on the ONE: the kick is there within 3 ms of the
    // line.
    void dropStylesMuteAndHandBack() {
        for (int style : {1, 2}) {
            std::string fx = std::string(kBeat) + "page 3.0 7\n";
            for (int k = 0; k < style; ++k)
                fx += fmt("key %.3f Shift+Right\n", 3.05 + 0.05 * k);
            fx += fmt("down 3.3 1 %.3f %.3f\nup 3.8 1\n", columnX(1), rowY(3));
            std::vector<VoiceInfo> v;
            StudioView view, last, after;
            testkit::play(fx + "end 4\n", [&](Session &s) {
                v = voicesOf(s.view());
                view = s.view();
            });
            const double tDrop = timeOfBeat(view, v[0].end);
            testkit::play(fx + fmt("end %.3f\n", tDrop - 0.15), [&](Session &s) { last = s.view(); });
            QCOMPARE(last.parts[0].channel.value[Mute], 1.0f);
            QCOMPARE(last.parts[1].channel.value[Mute], 1.0f);
            QCOMPARE(last.parts[2].channel.value[Mute], 1.0f);
            QCOMPARE(last.parts[kFx].channel.value[Mute], 0.0f);
            const auto take = testkit::play(fx + fmt("end %.3f\n", tDrop + 0.3), [&](Session &s) { after = s.view(); });
            for (int p = 0; p < after.partCount; ++p)
                QCOMPARE(after.parts[p].channel.value[Mute], 0.0f);
            QVERIFY2(rmsDb(take, tDrop + 0.001, tDrop + 0.004) > -30,
                     qPrintable(QString::number(rmsDb(take, tDrop + 0.001, tDrop + 0.004))));
        }
    }

    // The styles sound different: a gap is a breath, at least 8 dB down on
    // the clean build in its half beat; a strip is the risers alone, down on
    // the clean build over its last bar even over this sparse beat.
    void dropStylesAreHeard() {
        double gapWindow[3] = {}, lastBar[3] = {};
        double tDrop = 0;
        for (int style = 0; style < 3; ++style) {
            std::string fx = std::string(kBeat) + "page 3.0 7\n";
            for (int k = 0; k < style; ++k)
                fx += fmt("key %.3f Shift+Right\n", 3.02 + 0.03 * k);
            fx += fmt("down 3.1 1 %.3f %.3f p=0.6\nup 3.6 1\n", columnX(0), rowY(2));
            if (style == 0) {
                std::vector<VoiceInfo> v;
                StudioView view;
                testkit::play(fx + "end 4\n", [&](Session &s) {
                    v = voicesOf(s.view());
                    view = s.view();
                });
                tDrop = timeOfBeat(view, v[0].end);
            }
            const auto take = testkit::play(fx + fmt("end %.3f\n", tDrop + 0.2));
            gapWindow[style] = rmsDb(take, tDrop - 0.22, tDrop - 0.01);
            lastBar[style] = rmsDb(take, tDrop - 2.0, tDrop - 0.01);
        }
        QVERIFY2(gapWindow[1] < gapWindow[0] - 8, qPrintable(QString("gap %1 clean %2").arg(gapWindow[1]).arg(gapWindow[0])));
        QVERIFY2(lastBar[2] < lastBar[0] - 1, qPrintable(QString("strip %1 clean %2").arg(lastBar[2]).arg(lastBar[0])));
    }

    // A second finger joins the drop the first one set, and its row says
    // when it comes in: a one-bar layer enters one bar before.
    void layersJoinTheSameDrop() {
        std::vector<VoiceInfo> v;
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\ndown 2.0 2 %.3f %.3f\nend 2.6\n", columnX(0), rowY(3),
                          columnX(3), rowY(0)),
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 2);
        QCOMPARE(v[0].end, 32.0);
        QCOMPARE(v[1].end, 32.0);
        QCOMPARE(v[1].start, 28.0);
        QCOMPARE(v[1].state, int(risers::Waiting));
    }

    // A click pulls a far drop in to the next bar line, and everything
    // rising gets there in time.
    void aClickDropsOnTheNextBar() {
        StudioView after;
        const auto take = testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nclick %.3f\nend %.3f\n",
                                            columnX(2), rowY(3), timeOf(5.1), timeOf(8.0) + 0.3),
                                        [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 8.0);
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(rmsDb(take, timeOf(7.6), timeOf(7.98)) > rmsDb(take, timeOf(4.5), timeOf(5.0)) + 6);
    }

    // A click with nothing building is a drop out of nothing: a reverse
    // swell into the next bar line, and the impact on it.
    void aClickAloneMakesADrop() {
        StudioView after;
        testkit::play(fmt("page 0 7\nclick %.3f\nend %.3f\n", timeOf(1.5), timeOf(4.0) + 0.2),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 4.0);
    }

    // The drop's sub is a choice: none unless SUB DROP is tapped during the
    // build, which arms one onto the ONE.
    void aSubIsArmedOntoTheDrop() {
        const double tDrop = timeOf(32.0);
        const std::string build = fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\n", columnX(0), rowY(3));
        for (bool armed : {false, true}) {
            std::string s = build;
            if (armed)
                s += fmt("tap 2.0 %.3f 0.1\n", hitX(1));
            StudioView before, after;
            testkit::play(s + fmt("end %.3f\n", tDrop - 0.5), [&](Session &se) { before = se.view(); });
            QCOMPARE(before.parts[kFx].data[risers::pub::SubArmed] > 0, armed);
            std::vector<VoiceInfo> v;
            testkit::play(s + fmt("end %.3f\n", tDrop + 0.1), [&](Session &se) { v = voicesOf(se.view()); });
            bool sub = false;
            for (const VoiceInfo &x : v)
                sub |= x.kind == risers::Sub;
            QCOMPARE(sub, armed);
        }
    }

    // A finger flicked down as it leaves pulls its riser out: no drop, and
    // the mix comes home.
    void aFlickCallsTheDropOff() {
        StudioView after;
        const auto take = testkit::play(
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 4.0 4.06 1 %.3f %.3f\nup 4.07 1\n", columnX(0), rowY(3), columnX(0),
                rowY(3) - 0.15) +
                fmt("end %.3f\n", timeOf(32.0) + 0.5),
            [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QVERIFY(rmsDb(take, timeOf(32.0) - 0.2, timeOf(32.0) + 0.4) < -60);
    }

    // Q: every riser falls away at once, the drop is called off and the
    // mix comes home.
    void theWholeBuildFallsAway() {
        StudioView after;
        const auto take = testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\ntap 0.7 %.3f %.3f len=0.2\nkey 2.5 Q\n",
                                            columnX(0), rowY(3), columnX(2), rowY(3)) +
                                            fmt("end %.3f\n", timeOf(32.0) + 0.5),
                                        [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        // It falls rather than stops: still sounding just after, quiet soon.
        QVERIFY(rmsDb(take, 2.52, 2.62) > -60);
        QVERIFY(rmsDb(take, timeOf(32.0) - 0.2, timeOf(32.0) + 0.4) < -60);
    }

    // Delete is the shared key for letting go: here the whole build falls
    // away, as Q (its first key) still does.
    void deleteDropsTheBuildAway() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\nkey 2.5 Delete\n", columnX(0), rowY(3)) +
                          fmt("end %.3f\n", timeOf(32.0) + 0.5),
                      [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
    }

    // The band under the pad does what the keys do. On the scripts' window
    // row 0 is 0.048 of the pad under it, row 1 0.119. Row 0: the clips to
    // 0.26, then the sets from 0.31, more than fit, so a window onto the
    // first nine while EDM is lit; row 1: the three drop styles from
    // 0.07 to 0.33, then shorter, the length and longer to 0.58, then DROP,
    // FALL AWAY and MOVE MIX.
    void theBandUnderThePadIsClickable() {
        // DROP GAP.
        testkit::play("page 0 7\nbutton 0.2 0.2 -0.119\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("gap")), qPrintable(s.pageStatus()));
        });
        // DUB, the third set.
        testkit::play("page 0 7\nbutton 0.2 0.50 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("dub")), qPrintable(s.pageStatus()));
        });
        // DROP with nothing building: the next bar line.
        StudioView after;
        testkit::play(fmt("page 0 7\nbutton %.3f 0.658 -0.119\nend %.3f\n", timeOf(1.5), timeOf(4.0) + 0.2),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 4.0);
        // LONGER: the keys' noise riser is 8 bars instead of 4.
        std::vector<VoiceInfo> v;
        testkit::play("page 0 7\nbutton 0.2 0.561 -0.119\nkey 0.5 A\nend 0.8\n",
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 1);
        QCOMPARE(v[0].end, 32.0);
        QVERIFY2(v[0].end - v[0].start > 20, qPrintable(QString::number(v[0].start)));
        // FALL AWAY.
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\nbutton 1.5 0.795 -0.119\nend 3.0\n", columnX(0), rowY(3)),
                      [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
    }

    // A clip slot clicked stops the loop playing in it; Shift+F1 empties it.
    void clipsAreClickedAndCleared() {
        const std::string loop = fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\n"
                                     "key 2.5 Space\n",
                                     hitX(3), hitX(3), hitX(0), hitX(3));
        StudioView after;
        testkit::play(loop + "button 4.0 0.015 -0.048\nend 6.5\n", [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, -1);
        QVERIFY(after.parts[kFx].clips[0].state != ClipView::Empty);
        testkit::play(loop + "key 4.0 Backspace\nkey 6.0 Shift+F1\nend 6.5\n", [&](Session &s) { after = s.view(); });
        QCOMPARE(int(after.parts[kFx].clips[0].state), int(ClipView::Empty));
    }

    // The pitch riser lands on the key's tonic two octaves up, and follows a
    // key change.
    void pitchedRisersLandInKey() {
        const double tDrop = timeOf(32.0);
        for (int fifths : {0, 1}) {
            std::vector<VoiceInfo> v;
            std::string s = "page 0 7\n";
            if (fifths)
                s += "key 0.1 Ctrl+Right\n";
            s += fmt("down 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(1), rowY(3), tDrop - 0.04);
            int root = 0;
            testkit::play(s, [&](Session &se) {
                v = voicesOf(se.view());
                root = se.view().root;
            });
            QCOMPARE(int(v.size()), 1);
            const double target = 36 + root + 24;
            QVERIFY2(std::fabs(v[0].note - target) < 0.8, qPrintable(QString("%1 vs %2").arg(v[0].note).arg(target)));
        }
        std::vector<VoiceInfo> v;
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 0.6 0.9 1 %.3f %.3f\n", columnX(1), rowY(3),
                          columnX(1) + 0.15, rowY(3)) +
                          fmt("end %.3f\n", tDrop - 0.04),
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 1);
        Tuning tuning;
        tuning.set(9, 0, 4, false);
        bool inKey = false;
        for (int d = -14; d < 21; ++d)
            inKey |= std::fabs(v[0].note - (tuning.baseMidi() + tuning.degreeSemis(d))) < 0.8;
        QVERIFY2(inKey && std::fabs(v[0].note - 69) > 1, qPrintable(QString::number(v[0].note)));
    }

    // The keyboard: A to G start risers of the keys' length on the phrase
    // grid, Up makes it longer, Z fires the impact, Shift+Return drops.
    void keysPlayRisersAndHits() {
        std::vector<VoiceInfo> v;
        testkit::play("page 0 7\nkey 0.3 Up\nkey 0.5 A\nkey 0.6 Z\nend 0.8\n",
                      [&](Session &s) { v = voicesOf(s.view()); });
        bool riser = false, impact = false;
        for (const VoiceInfo &x : v) {
            if (x.kind == risers::Noise) {
                riser = true;
                QCOMPARE(x.end, 32.0);
            }
            impact |= x.kind == risers::Impact;
        }
        QVERIFY(riser && impact);
        StudioView after;
        testkit::play(fmt("page 0 7\nkey 0.5 S\nkey %.3f Shift+Return\nend %.3f\n", timeOf(5.5), timeOf(8.0) + 0.2),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 8.0);
    }

    // Throw and wash take every channel's sends and hand them back when
    // the finger leaves, or the page does.
    void sendsAreHandedBack() {
        StudioView fresh, held, after;
        testkit::play("end 0.1\n", [&](Session &s) { fresh = s.view(); });
        const std::string wash = fmt("page 0 7\ndown 0.5 1 %.3f 0.12\n", hitX(6));
        testkit::play(wash + "end 1.5\n", [&](Session &s) { held = s.view(); });
        QVERIFY(held.parts[0].channel.value[ReverbSend] > 0.6f);
        QVERIFY(held.master[RoomSize] > 0.8f);
        testkit::play(wash + "page 1.5 1\nup 4.0 1\nend 4.0\n", [&](Session &s) { after = s.view(); });
        for (int p = 0; p < after.partCount; ++p)
            QCOMPARE(after.parts[p].channel.value[ReverbSend], fresh.parts[p].channel.value[ReverbSend]);
        QCOMPARE(after.master[RoomSize], 0.35f);
        QCOMPARE(after.master[RoomShimmer], 0.1f);

        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.12\nend 0.8\n", hitX(5)), [&](Session &s) { held = s.view(); });
        QVERIFY(held.parts[1].channel.value[EchoSend] > 0.6f);
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.12\nend 3.5\n", hitX(5)), [&](Session &s) { after = s.view(); });
        for (int p = 0; p < after.partCount; ++p)
            QCOMPARE(after.parts[p].channel.value[EchoSend], fresh.parts[p].channel.value[EchoSend]);
        QCOMPARE(after.master[EchoFeedback], 0.35f);
    }

    // The power-down darkens the mix into the next bar line at least two
    // beats away (from beat 4.5, the line at 8) and gives it back there.
    void aPowerDownComesBackOnTheOne() {
        StudioView mid, after;
        const std::string stop = fmt("page 0 7\ntap %.3f %.3f 0.12\n", timeOf(4.5), hitX(4));
        testkit::play(stop + fmt("end %.3f\n", timeOf(7.5)), [&](Session &s) { mid = s.view(); });
        QVERIFY(mid.master[MasterFilter] < 0.2f);
        QVERIFY(mid.master[MasterGain] < 0.5f);
        testkit::play(stop + fmt("end %.3f\n", timeOf(8.2)), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[MasterGain], 0.8f);
    }

    // Holding a hit only holds it: taps and a hold in rhythm make no loop.
    void holdingAHitNeverLoops() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ndown 1.5 9 %.3f 0.1\nup 2.4 9\nend 3.0\n", hitX(0),
                          hitX(1), hitX(5)),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, -1);
        for (const ClipView &c : after.parts[kFx].clips)
            QCOMPARE(int(c.state), int(ClipView::Empty));
    }

    // Space loops what was just played on the hit row, as on every page.
    void spaceLoopsTheHitRow() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\n", hitX(3), hitX(3),
                          hitX(0), hitX(3)) +
                          "key 2.5 Space\nend 4.0\n",
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, 0);
        QVERIFY(after.parts[kFx].clips[0].events >= 4);
    }

    // A riser looped by Space replays from the clip and still aims at a
    // drop the page can build the mix around.
    void aLoopedRiserStillAimsAtADrop() {
        StudioView after;
        testkit::play(std::string(kBeat) + fmt("page 3.0 7\ntap 3.1 %.3f %.3f\nkey 4.2 Space\nend 9\n", columnX(0), rowY(3)),
                      [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].playing >= 0);
        bool replayed = false;
        const float *d = after.parts[kFx].data;
        for (int k = 0; k < risers::pub::MaxVoices; ++k) {
            const float *o = d + risers::pub::Voices + risers::pub::Stride * k;
            replayed |= int(o[risers::pub::VKind]) == int(risers::Noise) && o[risers::pub::VWho] > 1.5f;
        }
        QVERIFY(replayed);
        QVERIFY(d[risers::pub::Drop] > 0);
    }

    // Recording a build and its drop: play the build over a loop, let the
    // drop land, press Space in the bar after it. The loop runs from the bar
    // the build began in to the bar after the drop, and every pass drops on
    // the same bar of it, moving the mix as it did live.
    void spaceRecordsABuildAndItsDrop() {
        for (bool rush : {false, true}) {
            std::string fx = std::string(kBeat) + fmt("page 3.0 7\ndown 3.2 1 %.3f %.3f\nup 3.5 1\n", columnX(0), rowY(2));
            std::vector<VoiceInfo> v;
            StudioView view;
            testkit::play(fx + "end 4\n", [&](Session &s) {
                v = voicesOf(s.view());
                view = s.view();
            });
            QCOMPARE(int(v.size()), 1);
            const double landing = view.beat + (3.2 - view.time) * view.bpm / 60.0;
            double drop = v[0].end;
            if (rush) {
                // A click two bars before the drop pulls it to the next bar line.
                const double click = drop - 8.5;
                fx += fmt("click %.3f\n", timeOfBeat(view, click));
                drop = std::ceil((click - 0.25) / 4.0) * 4.0;
            }
            const double space = timeOfBeat(view, drop + 4.5);
            fx += fmt("key %.3f Space\n", space);
            const double startBar = std::floor(landing / 4.0) * 4.0;
            const double length = drop + 4.0 - startBar;
            // Long enough for the loop to come round twice more.
            const double end = timeOfBeat(view, drop + 4.0 + 2 * length + 1.0);
            StudioView after;
            float drops = 0;
            testkit::play(fx + fmt("end %.3f\n", end), [&](Session &s) {
                after = s.view();
                drops = s.probe(kFx, QStringLiteral("drops"));
            });
            // The tutorial's count: the live drop and two passes of the loop.
            QVERIFY2(drops >= 3, qPrintable(QString::number(drops)));
            const PartView &fxv = after.parts[kFx];
            QVERIFY2(fxv.playing >= 0, rush ? "rush" : "build");
            QCOMPARE(double(fxv.clips[fxv.playing].lengthBeats), length);
            // The drop of the latest pass sits on the same bar as the live one.
            const double last = fxv.data[risers::pub::LastDrop];
            QVERIFY2(last > drop + length - 0.01, qPrintable(QString::number(last)));
            const double k = (last - drop) / length;
            QVERIFY2(std::fabs(k - std::round(k)) < 1e-3, qPrintable(QString("%1 %2 %3").arg(last).arg(drop).arg(length)));
            // And the mix it moved is home again after it.
            QCOMPARE(after.master[MasterFilter], 0.5f);
        }
    }

    // Every set's every sound is clean (finite, under the peak, no offset)
    // and sits near EDM's level for the same sound. And every set is its
    // own: against any other set, most of its ten sounds would be told
    // apart.
    void everySetIsCleanLevelAndItsOwn() {
        constexpr int kinds = int(std::size(kSounding));
        static Features f[risers::SetCount][kinds];
        // Every problem at once, so one run shows all a change has moved.
        QStringList wrong;
        for (int s = 0; s < risers::SetCount; ++s)
            for (int k = 0; k < kinds; ++k) {
                const Heard solo = playSolo(s, kSounding[k]);
                double peak = 0, mean = 0, power = 0;
                bool finite = true;
                for (size_t i = 0; i < solo.l.size(); ++i) {
                    power += double(solo.l[i]) * solo.l[i] + double(solo.r[i]) * solo.r[i];
                    finite &= std::isfinite(solo.l[i]) && std::isfinite(solo.r[i]);
                    peak = std::max({peak, double(std::fabs(solo.l[i])), double(std::fabs(solo.r[i]))});
                    mean += solo.l[i] + solo.r[i];
                }
                mean /= 2.0 * double(solo.l.size());
                const double rms = std::sqrt(power / (2.0 * double(solo.l.size())));
                const QString what = QStringLiteral("%1 %2").arg(risers::palette(s).name, risers::kindName(kSounding[k]));
                if (!finite)
                    wrong << what + QStringLiteral(": not finite");
                if (peak >= 0.9)
                    wrong << QStringLiteral("%1: peak %2").arg(what).arg(peak);
                // A decaying sine leaves a little offset of its own (EDM's sub
                // does); nothing more than that.
                if (std::fabs(mean) >= 0.04 * rms + 1e-4)
                    wrong << QStringLiteral("%1: offset %2 at %3 rms").arg(what).arg(mean).arg(rms);
                f[s][k] = featuresOf(solo);
                if (std::fabs(f[s][k].level - f[0][k].level) >= 4.5)
                    wrong << QStringLiteral("%1: %2 dB against EDM's %3").arg(what).arg(f[s][k].level).arg(f[0][k].level);
            }
        // Against any other set, most of a set's ten sounds would be told
        // apart; a set added after the first ten, eight of them from the
        // older set it is nearest (phonk from trap, shoegaze from EDM and
        // analog), and seven from any other.
        auto near = [](int a, int b) {
            return (a == risers::SetTrap && b == risers::SetPhonk) ||
                   (b == risers::SetShoegaze && (a == risers::SetEdm || a == risers::SetAnalog));
        };
        for (int a = 0; a < risers::SetCount; ++a)
            for (int b = a + 1; b < risers::SetCount; ++b) {
                int told = 0;
                QStringList same;
                for (int k = 0; k < kinds; ++k) {
                    const bool d = differ(f[a][k], f[b][k]);
                    told += d;
                    if (!d)
                        same << QString::fromUtf8(risers::kindName(kSounding[k]));
                }
                const int need = near(a, b) ? 8 : b >= risers::SetAmapiano ? 7 : 6;
                if (told < need)
                    wrong << QStringLiteral("%1 and %2: %3 of %4 sounds differ (alike: %5)")
                                 .arg(risers::palette(a).name, risers::palette(b).name)
                                 .arg(told)
                                 .arg(kinds)
                                 .arg(same.join(QStringLiteral(", ")));
            }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("\n"))));
    }

    // Lo-fi's sampler grit stays under its output filter: nothing of it
    // above 10 kHz, where the sample-and-hold's images would whine.
    void loFiHasNoAir() {
        for (int k : kSounding) {
            const double air = airOf(playSolo(risers::SetLofi, k));
            QVERIFY2(air < -35, qPrintable(QStringLiteral("%1: %2 dB above 10 kHz").arg(risers::kindName(k)).arg(air)));
        }
    }

    // The pitch riser lands on a scale note in every set: a dub siren stops
    // throwing it about before the line, and tape wow is a few cents.
    void everySetsPitchRiserLandsInKey() {
        Harmony h;
        for (int s = 0; s < risers::SetCount; ++s) {
            const Heard solo = playSolo(s, risers::Pitch);
            bool inKey = false;
            for (int d = -21; d < 42; ++d)
                inKey |= std::fabs(solo.note - float(h.midi(d, 0))) < 0.5f;
            QVERIFY2(inKey, qPrintable(QStringLiteral("%1: %2").arg(risers::palette(s).name).arg(solo.note)));
        }
    }

    // Left and Right step through the ten sets and wrap round; Shift with
    // them (or [ ]) is the drop style, and leaves the set alone.
    void arrowsStepTheSets() {
        testkit::play("page 0 7\nkey 0.1 Right\nkey 0.2 Right\nkey 0.3 Right\nkey 0.4 Shift+Right\nend 0.6\n",
                      [](Session &s) {
                          QCOMPARE(s.probe(kFx, QStringLiteral("set")), 3.0f);
                          QVERIFY2(s.pageStatus().contains(QStringLiteral("gap")), qPrintable(s.pageStatus()));
                          QVERIFY2(s.pageStatus().contains(QStringLiteral("lo-fi")), qPrintable(s.pageStatus()));
                      });
        testkit::play("page 0 7\nkey 0.1 Left\nkey 0.2 ]\nkey 0.3 ]\nend 0.6\n", [](Session &s) {
            QCOMPARE(s.probe(kFx, QStringLiteral("set")), float(risers::SetCount - 1));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("strip")), qPrintable(s.pageStatus()));
        });
    }

    // The five sets added after the first ten: every sound they make is in
    // the set's scale wherever it has a pitch (the key's, or gamelan's
    // slendro), under 0 dBFS, and each is reached from the keys (Left from
    // EDM wraps to the last) and the row of chips, named in the page's
    // status line, and saved and reopened by name.
    void theNewSetsPlayInKeyAndComeBackByName() {
        Harmony h;
        for (int set = risers::SetAmapiano; set < risers::SetCount; ++set) {
            const QString name = QString::fromUtf8(risers::palette(set).name);
            for (int kind : kSounding) {
                const Heard solo = playSolo(set, kind);
                double peak = 0, power = 0;
                for (size_t i = 0; i < solo.l.size(); ++i) {
                    peak = std::max({peak, double(std::fabs(solo.l[i])), double(std::fabs(solo.r[i]))});
                    power += double(solo.l[i]) * solo.l[i];
                }
                const QString what = QStringLiteral("%1 %2").arg(name, risers::kindName(kind));
                QVERIFY2(power > 1e-3, qPrintable(what + QStringLiteral(": silent")));
                QVERIFY2(20 * std::log10(peak) < -1.0, qPrintable(QStringLiteral("%1: peak %2").arg(what).arg(peak)));
                // Noise sweeps whistle onto the tonic and rolls climb the
                // scale; the rest are struck or slid onto scale notes. A
                // power-down's note is a slide, a brake's a turntable's, and
                // the stabs' last beat is an octave's glide onto the chord.
                if (kind != risers::Stop && kind != risers::Down && kind != risers::Swell && kind != risers::Impact &&
                    kind != risers::Saw)
                    QVERIFY2(inScaleOf(set, h, solo.note), qPrintable(QStringLiteral("%1: note %2").arg(what).arg(solo.note)));
            }
            // Its chip on the row under the pad and its name in the status.
            const int steps = int(risers::SetCount) - set;
            std::string keys = "page 0 7\n";
            for (int k = 0; k < steps; ++k)
                keys += fmt("key %.2f Left\n", 0.1 + 0.05 * k);
            testkit::play(keys + "end 1.2\n", [&](Session &s) {
                QCOMPARE(s.probe(kFx, QStringLiteral("set")), float(set));
                QVERIFY2(s.pageStatus().contains(name), qPrintable(s.pageStatus()));
            });
            bool checked = false;
            testkit::reopen(keys + fmt("tap 1.0 %.3f 0.1\nend 1.6\n", hitX(0)), 1.3,
                            [&](Session &, Session &opened, const QJsonObject &doc) {
                                const QJsonObject fx = doc[QStringLiteral("parts")].toArray()[kFx].toObject();
                                QCOMPARE(fx[QStringLiteral("settings")].toObject()[QStringLiteral("set")].toString(), name);
                                QCOMPARE(opened.probe(kFx, QStringLiteral("set")), float(set));
                                checked = true;
                            });
            QVERIFY(checked);
        }
    }

    // Amapiano's log drum is a bass line: across its cell it plays the
    // scale's notes, one at a time (a new one chokes the last within a few
    // milliseconds), and a held finger sliding sideways glides to the next.
    // Tapped, it is down 20 dB in 0.35 to 0.45 s. Its register is one
    // octave whatever the key (62 to 124 Hz for the root), its weight in
    // the octaves at 125 and 250 Hz. Chiptune's triangle is played the
    // same way, and phonk's 808 slides from the last note into the next.
    void theLogDrumIsALine() {
        Harmony h;
        for (int set : {int(risers::SetAmapiano), int(risers::SetChiptune), int(risers::SetPhonk)}) {
            const QString name = QString::fromUtf8(risers::palette(set).name);
            std::vector<float> struck;
            for (float tone : {-0.4f, 0.0f, 0.4f}) {
                Rig rig;
                rig.on(5, risers::Sub, set, tone);
                rig.run(0.15);
                struck.push_back(rig.note(risers::Sub));
                QVERIFY2(inKeyNote(h, struck.back(), 0.05f), qPrintable(name + QString::number(struck.back())));
            }
            QVERIFY2(struck[0] < struck[1] && struck[1] < struck[2],
                     qPrintable(QStringLiteral("%1: %2 %3 %4").arg(name).arg(struck[0]).arg(struck[1]).arg(struck[2])));
            // One at a time.
            Rig rig;
            rig.on(5, risers::Sub, set, 0);
            rig.run(0.2);
            rig.on(6, risers::Sub, set, 0.4f);
            rig.run(0.02);
            QCOMPARE(rig.voices(risers::Sub), 1);
            // Held and slid: through the notes between, onto the new one.
            Rig slide;
            slide.on(5, risers::Sub, set, 0);
            slide.run(0.2);
            const float from = slide.note(risers::Sub);
            slide.move(5, 0.43f);
            const std::vector<float> path = notesOf(slide, risers::Sub, 0.6);
            const float to = path.back();
            QVERIFY2(to > from + 2 && inKeyNote(h, to, 0.05f), qPrintable(QStringLiteral("%1 → %2").arg(from).arg(to)));
            int between = 0;
            for (float n : path)
                between += n > from + 0.3f && n < to - 0.3f;
            QVERIFY2(between >= 3, qPrintable(name + QStringLiteral(": glides through %1 blocks").arg(between)));
        }
        // An 808 struck while the last still rings slides in from it.
        {
            Rig rig;
            rig.on(5, risers::Sub, risers::SetPhonk, 0);
            rig.run(0.3);
            const float before = rig.note(risers::Sub);
            rig.on(6, risers::Sub, risers::SetPhonk, 0.43f);
            rig.run(0.004);
            QVERIFY2(std::fabs(rig.note(risers::Sub) - before) < 1.5f, qPrintable(QString::number(rig.note(risers::Sub))));
        }
        // Tapped, the log drum is a pluck.
        {
            Rig rig;
            rig.on(5, risers::Sub, risers::SetAmapiano, 0);
            rig.run(0.01);
            rig.off(5);
            rig.run(1.2);
            double loudest = -200, at20 = -1;
            for (double t = 0; t < 1.15; t += 0.01)
                loudest = std::max(loudest, windowDb(rig.l, rig.r, t, t + 0.02));
            for (double t = 0; t < 1.15 && at20 < 0; t += 0.01)
                if (windowDb(rig.l, rig.r, t, t + 0.02) < loudest - 20 && t > 0.05)
                    at20 = t;
            QVERIFY2(at20 > 0.33 && at20 < 0.47, qPrintable(QString::number(at20)));
        }
        // One octave for the root in every key.
        for (int root = 0; root < 12; ++root) {
            Rig rig;
            rig.ctx.h.tuning.set(root, rig.ctx.h.tuning.familyIndex(), rig.ctx.h.tuning.brightnessIndex(), false);
            rig.on(5, risers::Sub, risers::SetAmapiano, 0);
            rig.run(0.2);
            const float hz = midiToHz(rig.note(risers::Sub));
            QVERIFY2(hz >= 62 && hz < 124, qPrintable(QStringLiteral("root %1: %2 Hz").arg(root).arg(hz)));
        }
        const Heard log = playSolo(risers::SetAmapiano, risers::Sub);
        double bands[8];
        bandsOf(log, bands);
        // Bands at 62.5 Hz and up: the log drum's are 125 and 250 Hz.
        const double body = std::max(bands[1], bands[2]);
        QVERIFY2(body > bands[0] + 3 && body > bands[5] + 6,
                 qPrintable(QStringLiteral("%1 %2 %3 %4").arg(bands[0]).arg(bands[1]).arg(bands[2]).arg(bands[5])));
    }

    // Amapiano's drop is the log drum alone on the ONE: the drums and bass
    // sit out its first beat and come back on the second, exactly as they
    // were.
    void anAmapianoDropLeavesTheOneToTheLogDrum() {
        std::string fx = std::string(kBeat) + "page 3.0 7\n";
        for (int k = 0; k < 5; ++k)
            fx += fmt("key %.3f Left\n", 3.05 + 0.04 * k);
        fx += fmt("down 3.3 1 %.3f %.3f\nup 3.8 1\n", columnX(1), rowY(0));
        std::vector<VoiceInfo> v;
        StudioView view, first, second;
        testkit::play(fx + "end 4\n", [&](Session &s) {
            v = voicesOf(s.view());
            view = s.view();
        });
        QVERIFY(!v.empty());
        const double tDrop = timeOfBeat(view, v[0].end), beat = 60.0 / view.bpm;
        testkit::play(fx + fmt("end %.3f\n", tDrop + 0.4 * beat), [&](Session &s) { first = s.view(); });
        QCOMPARE(first.parts[0].channel.value[Mute], 1.0f);
        QCOMPARE(first.parts[1].channel.value[Mute], 1.0f);
        QCOMPARE(first.parts[2].channel.value[Mute], 0.0f);
        testkit::play(fx + fmt("end %.3f\n", tDrop + 1.5 * beat), [&](Session &s) { second = s.view(); });
        for (int p = 0; p < second.partCount; ++p)
            QCOMPARE(second.parts[p].channel.value[Mute], 0.0f);
    }

    // Gamelan is in slendro, not the key: its saron, gangsa, suling,
    // bonang and ceng-ceng all play slendro's tones, some of them well off
    // any note of the key. The gangsa's kotekan is two parts locked into
    // one line: in both its figures every stroke is someone's, each part
    // rests, they share tones, and neither simply takes every other stroke.
    // Played, polos is on the left and sangsih on the right. And the gong
    // leaves the kick's band alone: below 120 Hz it hums by under 6 dB
    // through its first bar, and is 30 dB down by its third.
    void gamelanIsInSlendro() {
        Harmony h;
        bool offKey = false;
        for (int kind : {int(risers::Saw), int(risers::Roll), int(risers::Pitch), int(risers::Zap), int(risers::Noise)}) {
            Rig rig;
            rig.on(5, kind, risers::SetGamelan, 0.3f);
            const std::vector<float> notes = notesOf(rig, kind, kind == risers::Zap ? 0.3 : 1.9);
            QVERIFY(!notes.empty());
            for (float n : notes) {
                QVERIFY2(inSlendro(h, n), qPrintable(QStringLiteral("%1: %2").arg(risers::kindName(kind)).arg(n)));
                offKey |= !inKeyNote(h, n, 0.3f);
            }
        }
        QVERIFY(offKey);
        {
            Rig rig;
            Command c;
            c.type = Command::On;
            c.id = 5;
            c.i[0] = risers::Roll;
            c.i[1] = 4;
            c.i[3] = risers::SetGamelan;
            c.f[0] = 0.6f;
            c.f[2] = 0.7f;
            c.f[5] = 16.0f;
            rig.send(c);
            const std::vector<float> notes = notesOf(rig, risers::Roll, timeOf(7.9));
            QVERIFY2(notes.size() >= 10, qPrintable(QString::number(notes.size())));
            // In the first half (norot, in eighths), a stroke polos plays
            // alone is louder on the left, one sangsih plays alone on the
            // right.
            double polosL = 0, polosR = 0, sangsihL = 0, sangsihR = 0;
            for (int e = 2; e < 15; ++e) {
                int strike[2];
                risers::kotekanStroke(false, e % 8, strike);
                if ((strike[0] >= 0) == (strike[1] >= 0))
                    continue;
                const size_t i0 = size_t(timeOf(0.5 * e) * 48000), i1 = size_t(timeOf(0.5 * e + 0.25) * 48000);
                for (size_t i = i0; i < i1; ++i) {
                    (strike[0] >= 0 ? polosL : sangsihL) += double(rig.l[i]) * rig.l[i];
                    (strike[0] >= 0 ? polosR : sangsihR) += double(rig.r[i]) * rig.r[i];
                }
            }
            QVERIFY2(polosL > 1.5 * polosR && sangsihR > 1.5 * sangsihL,
                     qPrintable(QStringLiteral("%1 %2 %3 %4").arg(polosL).arg(polosR).arg(sangsihL).arg(sangsihR)));
        }
        for (bool telu : {false, true}) {
            int rests[2] = {}, shared = 0, alternate = 0;
            for (int j = 0; j < 8; ++j) {
                int strike[2];
                risers::kotekanStroke(telu, j, strike);
                QVERIFY(strike[0] >= 0 || strike[1] >= 0);
                rests[0] += strike[0] < 0;
                rests[1] += strike[1] < 0;
                shared += strike[0] >= 0 && strike[0] == strike[1];
                alternate += (strike[0] >= 0) == (j % 2 == 0) && (strike[1] >= 0) == (j % 2 == 1);
            }
            QVERIFY2(rests[0] >= 2 && rests[1] >= 2 && shared >= 1 && alternate < 8,
                     qPrintable(QStringLiteral("%1 %2 %3 %4").arg(rests[0]).arg(rests[1]).arg(shared).arg(alternate)));
        }
        Rig gong;
        gong.on(5, risers::Impact, risers::SetGamelan);
        gong.off(5);
        gong.run(6.0);
        const double bar = 4 * 60.0 / 124;
        double hi = -200, lo = 200;
        for (double t = 0.3; t < bar; t += 0.1) {
            const double db = windowDb(gong.l, gong.r, t, t + 0.1, 120);
            hi = std::max(hi, db);
            lo = std::min(lo, db);
        }
        QVERIFY2(hi - lo < 6, qPrintable(QStringLiteral("%1 dB of hum").arg(hi - lo)));
        const double later = windowDb(gong.l, gong.r, 2 * bar, 2 * bar + 0.3, 120);
        QVERIFY2(later < hi - 30, qPrintable(QStringLiteral("%1 against %2").arg(later).arg(hi)));
    }

    // A console's arpeggio is notes of the key and nothing between: each
    // held for whole frames (a sixtieth of a second at least), and many of
    // them, the climb a step at a time. Its triangle kick falls onto its
    // note a frame at a time.
    void chiptuneSteps() {
        Harmony h;
        Rig rig;
        rig.on(5, risers::Pitch, risers::SetChiptune);
        std::vector<std::pair<float, int>> runs;
        rig.run(1.9, [&](const float *) {
            const float n = rig.note(risers::Pitch);
            if (n <= 0)
                return;
            if (runs.empty() || runs.back().first != n)
                runs.push_back({n, 0});
            ++runs.back().second;
        });
        QVERIFY2(runs.size() > 40, qPrintable(QString::number(runs.size())));
        std::vector<float> distinct;
        for (size_t k = 0; k < runs.size(); ++k) {
            QVERIFY2(inKeyNote(h, runs[k].first, 0.001f), qPrintable(QString::number(runs[k].first)));
            // Whole frames: 25 blocks of 32 samples, the first and last cut short.
            if (k > 0 && k + 1 < runs.size())
                QVERIFY2(runs[k].second >= 24, qPrintable(QString::number(runs[k].second)));
            if (std::find(distinct.begin(), distinct.end(), runs[k].first) == distinct.end())
                distinct.push_back(runs[k].first);
        }
        QVERIFY2(distinct.size() >= 10, qPrintable(QString::number(distinct.size())));
        Rig kick;
        kick.on(5, risers::Sub, risers::SetChiptune);
        std::vector<std::pair<float, int>> fall;
        kick.run(0.12, [&](const float *) {
            const float n = kick.note(risers::Sub);
            if (fall.empty() || fall.back().first != n)
                fall.push_back({n, 0});
            ++fall.back().second;
        });
        QVERIFY2(fall.size() >= 4, qPrintable(QString::number(fall.size())));
        for (size_t k = 1; k < fall.size(); ++k)
            QVERIFY2(fall[k].first < fall[k - 1].first, qPrintable(QString::number(fall[k].first)));
    }

    // Amapiano's "ayy" held chops a new one each eighth without cutting the
    // last one off: no sample jumps from its neighbours' line by more than
    // a voice's own curve does (a click is a step the size of the voice).
    void heldAyyDoesNotClick() {
        Rig rig;
        rig.on(5, risers::Zap, risers::SetAmapiano, 0, 0.6f);
        rig.run(2.0);
        rig.off(5);
        rig.run(0.6);
        double worst = 0, peak = 0;
        for (const std::vector<float> *ch : {&rig.l, &rig.r})
            for (size_t i = 2; i < ch->size(); ++i) {
                const std::vector<float> &x = *ch;
                worst = std::max(worst, double(std::fabs(x[i] - 2 * x[i - 1] + x[i - 2])));
                peak = std::max(peak, double(std::fabs(x[i])));
            }
        QVERIFY(peak > 0.05);
        QVERIFY2(worst < 0.01, qPrintable(QStringLiteral("%1 of %2").arg(worst).arg(peak)));
    }

    // Phonk's cowbell held plays a figure with rests in it, not a note
    // every sixteenth, all of it in the key; higher in the cell, another
    // figure. Its vocal and its 808 are covered by the sets' own checks.
    void phonkFiguresHaveRests() {
        Harmony h;
        std::vector<float> figures[2];
        int k = 0;
        for (float intensity : {0.4f, 0.98f}) {
            Rig rig;
            rig.on(5, risers::Zap, risers::SetPhonk, 0, intensity);
            figures[k] = notesOf(rig, risers::Zap, 0.2 + 2 * timeOf(4));
            for (float n : figures[k])
                QVERIFY2(inKeyNote(h, n, 0.05f), qPrintable(QString::number(n)));
            ++k;
        }
        // Two bars of sixteenths would be 32; the 3-3-2 strikes 12.
        QVERIFY2(figures[0].size() >= 8 && figures[0].size() <= 16, qPrintable(QString::number(figures[0].size())));
        QVERIFY(figures[0] != figures[1]);
    }

    // Shoegaze: the feedback grows into a harmonic of the note, its octave
    // and then its twelfth; the glide guitar's whammy bar wanders either
    // side of the glide by between a third of a semitone and a semitone,
    // both ways; and the fuzz is a fuzz: brighter at 2 and 4 kHz than EDM's
    // saws, and scooped, its 1 kHz under both 250 Hz and 2 kHz.
    void shoegazeSingsAndWanders() {
        Harmony h;
        {
            Rig rig;
            rig.on(5, risers::Noise, risers::SetShoegaze);
            const std::vector<float> notes = notesOf(rig, risers::Noise, 1.9);
            QVERIFY(!notes.empty());
            QCOMPARE(notes.back(), float(h.midi(0, 1)) + 19.0f);
            QVERIFY(std::find(notes.begin(), notes.end(), float(h.midi(0, 1)) + 12.0f) != notes.end());
        }
        {
            Rig rig;
            rig.on(5, risers::Pitch, risers::SetShoegaze, 0, 0.7f, 0.5f);
            double up = 0, down = 0;
            const float target = float(h.midi(0, 2)) - 12.0f;
            rig.run(1.9 * 0.7, [&](const float *) {
                const double p = rig.seconds() / (4 * 60.0 / 124);
                const double glide = target - risers::palette(risers::SetShoegaze).pitch.glide * (1 - std::pow(p, 1.5));
                const double dev = rig.note(risers::Pitch) - glide;
                up = std::max(up, dev);
                down = std::min(down, dev);
            });
            QVERIFY2(up > 0.33 && down < -0.33 && up < 1.1 && down > -1.1, qPrintable(QStringLiteral("%1 %2").arg(up).arg(down)));
        }
        double fuzz[8], edm[8];
        bandsOf(playSolo(risers::SetShoegaze, risers::Saw), fuzz);
        bandsOf(playSolo(risers::SetEdm, risers::Saw), edm);
        QString all;
        for (int k = 0; k < 8; ++k)
            all += QStringLiteral("%1/%2 ").arg(fuzz[k], 0, 'f', 1).arg(edm[k], 0, 'f', 1);
        QVERIFY2(fuzz[5] > edm[5] + 3 && fuzz[6] > edm[6] + 3 && fuzz[4] < fuzz[2] && fuzz[4] < fuzz[5], qPrintable(all));
    }

    // Shoegaze's wall on the drop: a palm-muted chug on the ONE that has
    // died away before the wall blooms; its feedback (the root two octaves
    // up, at 880 Hz in A) singing within the first beat, well over the
    // wall's own harmonic beside it, by the end of the beat; and the chord a power chord, its
    // root, fifth and octave lines standing clear of the gaps between them.
    void shoegazeWallChugsAndSings() {
        Rig rig;
        rig.on(5, risers::Impact, risers::SetShoegaze);
        rig.run(0.3);
        rig.off(5);
        rig.run(1.5);
        const double beat = 60.0 / 124;
        const double chug = windowDb(rig.l, rig.r, 0.025, 0.06), after = windowDb(rig.l, rig.r, 0.12, 0.16);
        QVERIFY2(chug > after + 2, qPrintable(QStringLiteral("%1 then %2").arg(chug).arg(after)));
        const double sing = toneDb(rig.l, rig.r, 880, 0.7 * beat, beat), beside = toneDb(rig.l, rig.r, 990, 0.7 * beat, beat);
        QVERIFY2(sing > beside + 10, qPrintable(QStringLiteral("%1 beside %2").arg(sing).arg(beside)));
        for (double hz : {110.0, 164.8, 220.0}) {
            const double line = toneDb(rig.l, rig.r, hz, 0.6, 1.4);
            const double gap = std::max(toneDb(rig.l, rig.r, hz + 27, 0.6, 1.4), toneDb(rig.l, rig.r, hz - 27, 0.6, 1.4));
            QVERIFY2(line > gap + 20, qPrintable(QStringLiteral("%1 Hz: %2 against %3").arg(hz).arg(line).arg(gap)));
        }
    }

    // Phonk's hiss has the horror chord printed through it: a beat before
    // the bar its root (220 Hz in A) comes up out of the hiss, far over
    // where it was a beat earlier; and the tape is rewound into the line,
    // the chord racing up, so the last tenth of the beat is brighter than
    // the hiss had ever been.
    void phonkHissPrintsThroughAndRewinds() {
        // A riser of two bars, so the bar's print-through comes a bar
        // before the rewind.
        Rig rig;
        Command c;
        c.type = Command::On;
        c.id = 5;
        c.i[0] = risers::Noise;
        c.i[1] = 2;
        c.i[3] = risers::SetPhonk;
        c.f[0] = 0.6f;
        c.f[2] = 0.7f;
        c.f[5] = 8.0f;
        rig.send(c);
        const double beat = 60.0 / 124;
        rig.run(8 * beat - 0.01);
        const double ghost = toneDb(rig.l, rig.r, 220, 3 * beat + 0.05, 3 * beat + 0.3);
        const double earlier = toneDb(rig.l, rig.r, 220, 2 * beat + 0.05, 2 * beat + 0.3);
        QVERIFY2(ghost > earlier + 10, qPrintable(QStringLiteral("%1 against %2").arg(ghost).arg(earlier)));
        const double late = windowDb(rig.l, rig.r, 7.85 * beat, 7.97 * beat) - windowDb(rig.l, rig.r, 7.85 * beat, 7.97 * beat, 2000);
        const double mid = windowDb(rig.l, rig.r, 6.5 * beat, 6.9 * beat) - windowDb(rig.l, rig.r, 6.5 * beat, 6.9 * beat, 2000);
        QVERIFY2(late > mid, qPrintable(QStringLiteral("%1 against %2").arg(late).arg(mid)));
    }

    // A loop keeps the set it was played in: zaps looped in the trap set
    // (reversed vocal chops) sound exactly the same whether the hand then
    // turns to EDM or not, and nothing like zaps looped in EDM.
    void aLoopKeepsItsSet() {
        const std::string zaps = fmt("tap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\n"
                                     "key 2.5 Space\n",
                                     hitX(3), hitX(3), hitX(3), hitX(3));
        const std::string trap = "page 0 7\nkey 0.1 Right\nkey 0.15 Right\nkey 0.2 Right\nkey 0.25 Right\n";
        const auto kept = testkit::play(trap + zaps + "end 8\n");
        const auto turned = testkit::play(trap + zaps + "key 3.0 Left\nkey 3.1 Left\nkey 3.2 Left\nkey 3.3 Left\nend 8\n");
        const auto edm = testkit::play("page 0 7\n" + zaps + "end 8\n");
        QVERIFY(kept.rms(4, 8) > 1e-3);
        double same = 0, other = 0;
        for (size_t i = size_t(4 * 48000); i < kept.left.size(); ++i) {
            same = std::max(same, double(std::fabs(kept.left[i] - turned.left[i])));
            other += std::pow(double(kept.left[i] - edm.left[i]), 2);
        }
        QVERIFY2(same < 1e-6, qPrintable(QString::number(same)));
        QVERIFY2(std::sqrt(other / (4 * 48000)) > 0.3 * kept.rms(4, 8), qPrintable(QString::number(other)));
    }

    // Ten fingers on the busiest risers stay well inside the page's share of
    // the audio callback.
    void tenFingersStayInsideBudget() {
        TENFOLD_TIMED();
        std::string s = "page 0 7\n";
        for (int f = 0; f < 10; ++f)
            s += fmt("down %.3f %.0f %.3f %.3f p=0.8\n", 0.2 + 0.05 * f, 100 + f, 0.05 + 0.1 * f, 0.7);
        s += "click 4.0\nend 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }

    // The same in each set added after the first ten, with three of the
    // fingers on the hit row's busiest (a line bass, the zap held, the
    // downlifter), so a kotekan, a fuzz, a reversed reverb and the rest all
    // play at once.
    void newSetsStayInsideBudget() {
        TENFOLD_TIMED();
        for (int set = risers::SetAmapiano; set < risers::SetCount; ++set) {
            std::string s = "page 0 7\n";
            for (int k = 0; k < int(risers::SetCount) - set; ++k)
                s += fmt("key %.2f Left\n", 0.05 + 0.02 * k);
            for (int f = 0; f < 7; ++f)
                s += fmt("down %.3f %.0f %.3f %.3f p=0.8\n", 0.3 + 0.05 * f, 100 + f, 0.07 + 0.14 * f, 0.7);
            for (int f = 0; f < 3; ++f)
                s += fmt("down %.3f %.0f %.3f 0.1 p=0.8\n", 0.7 + 0.05 * f, 110 + f, hitX(1 + f));
            s += "click 4.0\nend 6\n";
            const RenderStats st = renderScript(s, "");
            const QString name = QString::fromUtf8(risers::palette(set).name);
            QVERIFY(st.error.empty());
            QVERIFY2(st.p99Load < 0.12, qPrintable(name + QStringLiteral(": ") + QString::number(st.p99Load)));
            QVERIFY2(st.peakDb < 0, qPrintable(name + QStringLiteral(": ") + QString::number(st.peakDb)));
        }
    }

    // A song keeps the page's settings: the drop style, the keys' riser
    // length and whether builds move the mix come back with the loop.
    void theSongKeepsTheFxSettings() {
        const std::string script =
            fmt("page 0 7\nkey 0.15 Right\nkey 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Up\nkey 0.35 W\n"
                "tap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\nkey 2.5 Space\nend 3.5\n",
                hitX(3), hitX(3), hitX(0), hitX(3));
        bool checked = false;
        testkit::reopen(script, 2.5, [&](Session &, Session &opened, const QJsonObject &doc) {
            const QJsonObject part = doc[QStringLiteral("parts")].toArray()[kFx].toObject();
            QCOMPARE(part[QStringLiteral("name")].toString(), opened.pageName(kFx)); // scripts count pages from 1
            const QJsonObject settings = part[QStringLiteral("settings")].toObject();
            QCOMPARE(settings[QStringLiteral("set")].toString(), QStringLiteral("analog"));
            QCOMPARE(settings[QStringLiteral("dropStyle")].toString(), QStringLiteral("strip"));
            QCOMPARE(settings[QStringLiteral("riserBars")].toInt(), 8);
            QCOMPARE(settings[QStringLiteral("buildMix")].toBool(true), false);
            const QJsonObject again = opened.saveSong()[QStringLiteral("parts")].toArray()[kFx].toObject();
            QCOMPARE(again[QStringLiteral("settings")].toObject(), settings);
            // The page shows the style, the loop plays again, and the keys
            // play 8-bar risers.
            QCOMPARE(opened.page(), kFx);
            QVERIFY2(opened.pageStatus().contains(QStringLiteral("strip")), qPrintable(opened.pageStatus()));
            QCOMPARE(opened.view().parts[kFx].playing, 0);
            KeyEvent a;
            a.key = Qt::Key_A;
            QVERIFY(opened.key(a));
            float buf[384 * 2];
            for (int tick = 0; tick < 20; ++tick) {
                setScriptClock(scriptClock() + 1 / 125.0);
                opened.refresh();
                opened.idle(scriptClock());
                opened.studio()->render(buf, 384);
            }
            opened.refresh();
            bool eightBars = false;
            for (const VoiceInfo &v : voicesOf(opened.view()))
                eightBars |= v.kind == risers::Noise && v.end - v.start > 20;
            QVERIFY(eightBars);
            checked = true;
        });
        QVERIFY(checked);
    }

    // A file edited by hand: wrong types, numbers out of range and names
    // the page doesn't know leave it on sane settings.
    void garbageSettingsOpenSanely() {
        testkit::play("page 0 7\nkey 0.2 Right\nkey 0.3 Down\nend 0.5\n", [](Session &s) {
            auto settingsAfter = [&](const QJsonObject &garbage) {
                QJsonObject doc = s.saveSong();
                QJsonArray parts = doc[QStringLiteral("parts")].toArray();
                QJsonObject fx = parts[kFx].toObject();
                fx[QStringLiteral("settings")] = garbage;
                parts[kFx] = fx;
                doc[QStringLiteral("parts")] = parts;
                QString error;
                if (!s.openSong(doc, &error))
                    return QJsonObject{{QStringLiteral("error"), error}};
                return s.saveSong()[QStringLiteral("parts")].toArray()[kFx].toObject()[QStringLiteral("settings")].toObject();
            };
            const QJsonObject defaults{{QStringLiteral("set"), QStringLiteral("EDM")},
                                       {QStringLiteral("dropStyle"), QStringLiteral("clean")},
                                       {QStringLiteral("riserBars"), 4},
                                       {QStringLiteral("buildMix"), true}};
            QCOMPARE(settingsAfter({}), defaults);
            QCOMPARE(settingsAfter({{QStringLiteral("dropStyle"), 7},
                                    {QStringLiteral("riserBars"), QStringLiteral("8")},
                                    {QStringLiteral("buildMix"), QStringLiteral("no")},
                                    {QStringLiteral("extra"), QJsonArray{1, 2}}}),
                     defaults);
            QCOMPARE(settingsAfter({{QStringLiteral("set"), QStringLiteral("KAZOO")}})[QStringLiteral("set")].toString(),
                     QStringLiteral("EDM"));
            QCOMPARE(settingsAfter({{QStringLiteral("set"), QStringLiteral("Dub")}})[QStringLiteral("set")].toString(),
                     QStringLiteral("dub"));
            QJsonObject s1 = settingsAfter({{QStringLiteral("dropStyle"), QStringLiteral("boom")},
                                            {QStringLiteral("riserBars"), 1e300}});
            QCOMPARE(s1[QStringLiteral("dropStyle")].toString(), QStringLiteral("clean"));
            QCOMPARE(s1[QStringLiteral("riserBars")].toInt(), 16);
            QCOMPARE(settingsAfter({{QStringLiteral("riserBars"), -1e300}})[QStringLiteral("riserBars")].toInt(), 1);
            // A length the field has no row for is the nearest one it has.
            QCOMPARE(settingsAfter({{QStringLiteral("riserBars"), 6}})[QStringLiteral("riserBars")].toInt(), 8);
            QJsonObject gap = settingsAfter({{QStringLiteral("dropStyle"), QStringLiteral("gap")}});
            QCOMPARE(gap[QStringLiteral("dropStyle")].toString(), QStringLiteral("gap"));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("gap")), qPrintable(s.pageStatus()));
        });
    }

    // A clip from a file may hold any number in a command's values: the
    // engine takes them all without reading out of bounds, and plays on.
    void anyCommandValuesAreSafe() {
        PartSpec spec = makeRisers();
        auto &e = *spec.engine;
        FxAudio ctx;
        ctx.tr.rate = 48000;
        ctx.tr.bpm = 124;
        ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
        e.init(48000);
        auto run = [&](double seconds) {
            for (int b = 0; b < int(seconds * 48000 / kBlock); ++b) {
                ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
                float l[kBlock] = {}, r[kBlock] = {};
                e.render(l, r, kBlock, ctx);
                ctx.tr.sample += kBlock;
            }
        };
        const int32_t ints[] = {INT32_MIN, -1, 0, 7, 12, INT32_MAX};
        const float floats[] = {-3e38f, -1e6f, 0.0f, 1e6f, 3e38f};
        const uint16_t types[] = {Command::On, Command::Move, Command::Param, Command::Off, risers::CmdDrop,
                                  risers::CmdFall, risers::CmdMark};
        int n = 0;
        for (uint16_t type : types)
            for (int32_t i : ints)
                for (float f : floats) {
                    Command c;
                    c.type = type;
                    c.id = (n++ % 3) + (i & 1 ? (1 << 30) : 0);
                    for (int32_t &v : c.i)
                        v = i;
                    for (float &v : c.f)
                        v = f;
                    e.command(c, ctx);
                    run(0.002);
                }
        run(0.5);
        float data[kPartData];
        e.publish(data);
        QVERIFY(e.voices() <= risers::Engine::kVoices);
    }
};

TESTKIT_REGISTER(RisersTests)
#include "risers_tests.moc"
