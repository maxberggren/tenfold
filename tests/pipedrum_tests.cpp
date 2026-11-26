// The pipes kit: a rack of PVC pipes slapped with a flip-flop (pipedrum.h),
// checked as the acoustics textbooks and measurements have pipes
// (docs/pipe-drum.md): cut to length with their end corrections, every
// harmonic open and the odd ones closed, the paddle closing the mouth while
// it is held, how hard and where it lands, how long they ring, and what they
// cost.

#include <chrono>
#include <vector>

#include "drums.h"
#include "offline.h"
#include "testkit.h"

using namespace tf;

namespace {

// The page on the pipes: Left from house (round to the last kit) as many times as it is
// kits from the end (so kits added after it don't move it).
std::string toPipes() {
    std::string s = "page 0 1\n";
    for (int i = 0; i < drums::KitCount - drums::Pipes; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "key %.3f Left\n", 0.1 + 0.004 * i);
        s += buf;
    }
    return s;
}

constexpr float kRate = 48000;

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

struct Ctx : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    // D minor: the pipes D3 E3 F3 G3 A3 Bb3 C4 D4 E4 F4.
    Ctx() { h.tuning.set(2, 0, 4, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 1.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// What a pipe does, sample by sample: a slap at the start, and (if hold >
// 0) the paddle put back on at `hold` seconds and lifted at `lift`.
struct Play {
    int pipe = 0;
    float vel = 0.8f;
    float u = 0, w = 0; // where on the mouth, in its radii
    float tone = 0;
    double hold = -1, lift = -1, open = -1;
    float gap = 0;
    double seconds = 2;
};

std::vector<float> play(const Play &pl, drums::Engine *keep = nullptr) {
    Ctx ctx;
    drums::Engine local;
    drums::Engine &e = keep ? *keep : local;
    if (!keep)
        e.init(kRate);
    drums::Strike s;
    s.kit = drums::Pipes;
    s.pad = drums::pipePad(pl.pipe);
    s.vel = pl.vel;
    s.sx = 0.5f + 0.5f * pl.u / pipedrum::kSpotReach;
    s.sy = 0.5f + 0.5f * pl.w / pipedrum::kSpotReach;
    s.tone = pl.tone;
    if (pl.vel > 0)
        e.strike(s, ctx.h);
    std::vector<float> out;
    float l[kBlock], r[kBlock];
    bool held = false, lifted = false, opened = false;
    for (int b = 0; b < int(pl.seconds * kRate) / kBlock; ++b) {
        const double t = double(b * kBlock) / kRate;
        if (!held && pl.hold >= 0 && t >= pl.hold) {
            e.paddle(s.pad, 0, drums::PaddleHold, 0.6f, ctx.h);
            held = true;
        }
        if (!opened && pl.open >= 0 && t >= pl.open) {
            e.paddle(s.pad, 0, drums::PaddleOpen, pl.gap, ctx.h);
            opened = true;
        }
        if (!lifted && pl.lift >= 0 && t >= pl.lift) {
            e.paddle(s.pad, 0, drums::PaddleLift, 0, ctx.h);
            lifted = true;
        }
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        e.render(l, r, kBlock, ctx);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(0.5f * (l[i] + r[i]));
    }
    return out;
}

// The level (dB) of a Hann-windowed single frequency over [from, from+len).
double levelAt(const std::vector<float> &x, double from, double len, double hz) {
    const size_t a = size_t(from * kRate), n = size_t(len * kRate);
    double re = 0, im = 0;
    const double step = 2 * M_PI * hz / kRate;
    for (size_t i = 0; i < n && a + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n - 1));
        re += w * x[a + i] * std::cos(step * double(i));
        im -= w * x[a + i] * std::sin(step * double(i));
    }
    return 20 * std::log10(std::sqrt(re * re + im * im) / (0.25 * double(n)) + 1e-15);
}

// The strongest component within ±cents of hz.
double levelNear(const std::vector<float> &x, double from, double len, double hz, double cents = 40) {
    double top = -1e9;
    const double step = std::max(0.2, 0.5 / len);
    for (double f = hz * std::exp2(-cents / 1200); f <= hz * std::exp2(cents / 1200); f += step)
        top = std::max(top, levelAt(x, from, len, f));
    return top;
}

// The strongest peak within ±cents of hz over a window: coarse, then fine.
double peakHz(const std::vector<float> &x, double from, double len, double hz, double cents = 80) {
    double best = hz, top = -1e9;
    const double lo = hz * std::exp2(-cents / 1200), hi = hz * std::exp2(cents / 1200);
    for (double f = lo; f <= hi; f += 0.25) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    const double c = best;
    for (double f = c - 0.3; f <= c + 0.3; f += 0.01) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    return best;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

// The time constant of a partial's decay between two times, from its level.
double tauOf(const std::vector<float> &x, double hz, double t0, double t1) {
    const double a = levelAt(x, t0, 0.2, hz), b = levelAt(x, t1, 0.2, hz);
    return (t1 - t0) * 20 / std::log(10.0) / (a - b);
}

// The pipes as the engine has them tuned in D minor.
const pipedrum::Body &tuned(drums::Engine &e) {
    Ctx ctx;
    e.init(kRate);
    float hz[pipedrum::kPipes];
    drums::pipeTuning(ctx.h, hz);
    e.pipeBody().tune(hz);
    return e.pipeBody();
}

}

class PipeDrumTests : public QObject {
    Q_OBJECT
    // Time the model and the studio against the clock: bin/test runs them with the machine to itself.
    Q_CLASSINFO("alone", "tenPipesCostLittle tenFingersOnThePipesStayInsideBudget")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "aRollNeverRunsAway aLoopOfThePipesKeepsThePipes")
private slots:

    // --- tuning by length -------------------------------------------------------

    void inDMinorThePipesRunUpTheScale() {
        // Ten degrees from D3: D3 E3 F3 G3 A3 Bb3 C4 D4 E4 F4, each pipe
        // shorter than the last.
        Ctx ctx;
        float hz[pipedrum::kPipes];
        drums::pipeTuning(ctx.h, hz);
        const int midi[pipedrum::kPipes] = {50, 52, 53, 55, 57, 58, 60, 62, 64, 65};
        for (int n = 0; n < pipedrum::kPipes; ++n)
            QVERIFY2(std::fabs(cents(hz[n], midiToHz(midi[n]))) < 0.5, qPrintable(QString::number(n)));
        drums::Engine e;
        const pipedrum::Body &b = tuned(e);
        for (int n = 1; n < pipedrum::kPipes; ++n)
            QVERIFY(b.length(n) < b.length(n - 1));
        // D3 is a pipe of about 1.13 m, F4 of about 0.45 m: a tubulum's.
        QVERIFY2(b.length(0) > 1.08f && b.length(0) < 1.18f, qPrintable(QString::number(b.length(0))));
        QVERIFY2(b.length(9) > 0.42f && b.length(9) < 0.48f, qPrintable(QString::number(b.length(9))));
    }

    void eachPipeIsCutWithItsEndCorrections() {
        // The textbook cuts an open pipe to c/2f less two end corrections of
        // 0.6133 a (Levine & Schwinger). The model's own pipe is a few
        // millimetres shorter: the boundary layer slows the wave by the
        // same α that wears it away (a few tenths of a percent), and the
        // end correction falls a little with frequency.
        drums::Engine e;
        const pipedrum::Body &b = tuned(e);
        const float a = pipedrum::Physics::kRadius;
        QVERIFY2(std::fabs(b.endCorrection(30.0f) / a - 0.6133f) < 0.005f,
                 qPrintable(QString::number(b.endCorrection(30.0f) / a)));
        QVERIFY(b.endCorrection(1500.0f) < b.endCorrection(150.0f));
        for (int n = 0; n < pipedrum::kPipes; ++n) {
            const float f = b.openHz(n);
            const float textbook = pipedrum::Physics::kC / (2.0f * f) - 2.0f * pipedrum::Physics::kEnd * a;
            const float short_ = textbook - b.length(n);
            QVERIFY2(short_ > 0.0f && short_ < 0.012f * textbook,
                     qPrintable(QStringLiteral("%1: %2 vs %3").arg(n).arg(b.length(n)).arg(textbook)));
            // And the textbook's pitch for the model's length is within the
            // boundary layer's few cents of the note.
            const double c = cents(pipedrum::Body::textbookOpenHz(b.length(n)), f);
            QVERIFY2(c > 0 && c < 16, qPrintable(QStringLiteral("%1: %2 c").arg(n).arg(c)));
        }
    }

    void everyPipeSoundsItsNote() {
        // Slapped and let go, each pipe sounds its note open at both ends,
        // within a couple of cents once the paddle is clear.
        Ctx ctx;
        float hz[pipedrum::kPipes];
        drums::pipeTuning(ctx.h, hz);
        for (int n = 0; n < pipedrum::kPipes; ++n) {
            Play pl;
            pl.pipe = n;
            pl.seconds = 0.8;
            const auto x = play(pl);
            const double f = peakHz(x, 0.1, 0.5, hz[n]);
            QVERIFY2(std::fabs(cents(f, hz[n])) < 3, qPrintable(QStringLiteral("%1: %2 Hz for %3").arg(n).arg(f).arg(hz[n])));
        }
    }

    // --- the harmonic series ----------------------------------------------------

    void anOpenPipeSoundsEveryHarmonic() {
        // Open at both ends, its resonances are 1, 2, 3, 4 × the note (a
        // hair stretched by the end correction's fall with frequency): a
        // slap's first hundred milliseconds have a strong second harmonic
        // (within 10 dB of the note, as the robotic tubulum's recordings
        // show), and the third and fourth well above the floor; a closed
        // pipe could have no second at all. (Heard from overhead, about as
        // far from both ends, the even harmonics, which the two ends sound
        // in opposite phase, partly cancel: the fourth sits lowest.)
        Play pl;
        pl.pipe = 2;
        pl.vel = 0.9f;
        pl.seconds = 0.6;
        const auto x = play(pl);
        drums::Engine e;
        const float f1 = tuned(e).openHz(2);
        const double l1 = levelNear(x, 0.0, 0.12, f1);
        for (int k = 2; k <= 4; ++k) {
            const double f = peakHz(x, 0.0, 0.12, k * f1, 60);
            QVERIFY2(std::fabs(cents(f, k * f1)) < 15, qPrintable(QStringLiteral("%1: %2 Hz").arg(k).arg(f)));
            const double lk = levelNear(x, 0.0, 0.12, k * f1);
            QVERIFY2(lk > l1 - (k == 2 ? 10 : 50), qPrintable(QStringLiteral("%1: %2 vs %3 dB").arg(k).arg(lk).arg(l1)));
        }
        // And the higher harmonics die faster: the open end lets them out.
        QVERIFY(tauOf(x, 2 * f1, 0.1, 0.35) < tauOf(x, f1, 0.1, 0.35));
    }

    void aHeldPaddleClosesThePipe() {
        // The paddle put back on the mouth and held: a pipe closed at one
        // end, sounding c/4(L + 0.6133a), near an octave below its open
        // note, with only odd harmonics (1, 3, 5 × the closed note; the
        // open note, 2 × it, all but gone).
        Play pl;
        pl.pipe = 3;
        pl.hold = 0.2;
        pl.seconds = 1.2;
        drums::Engine e;
        e.init(kRate);
        const auto x = play(pl, &e);
        QVERIFY(e.pipeBody().paddleHeld(3));
        const float L = e.pipeBody().length(3);
        const double closed = pipedrum::Body::textbookClosedHz(L);
        const double open = e.pipeBody().openHz(3);
        QVERIFY(closed < 0.56 * open && closed > 0.5 * open);
        const double f = peakHz(x, 0.4, 0.6, closed, 120);
        QVERIFY2(std::fabs(cents(f, closed)) < 30, qPrintable(QStringLiteral("%1 Hz for %2").arg(f).arg(closed)));
        const double l1 = levelNear(x, 0.4, 0.6, closed, 60), l2 = levelNear(x, 0.4, 0.6, 2 * closed, 60);
        const double l3 = levelNear(x, 0.4, 0.6, 3 * closed, 60);
        QVERIFY2(l1 > l2 + 20 && l3 > l2 + 8, qPrintable(QStringLiteral("%1 %2 %3 dB").arg(l1).arg(l2).arg(l3)));
        // Before it went on, the open note.
        QVERIFY(levelNear(x, 0.02, 0.15, open, 30) > levelNear(x, 0.02, 0.15, closed, 30) + 10);
    }

    void liftedThePipeOpensAgain() {
        // Lifted, the paddle's suction pops the open note back, and the
        // closed one is gone.
        Play pl;
        pl.pipe = 3;
        pl.hold = 0.2;
        pl.lift = 0.8;
        pl.seconds = 1.5;
        drums::Engine e;
        e.init(kRate);
        const auto x = play(pl, &e);
        QVERIFY(!e.pipeBody().paddleHeld(3));
        const double open = e.pipeBody().openHz(3), closed = pipedrum::Body::textbookClosedHz(e.pipeBody().length(3));
        const double f = peakHz(x, 1.0, 0.4, open, 40);
        QVERIFY2(std::fabs(cents(f, open)) < 5, qPrintable(QString::number(f)));
        QVERIFY(levelNear(x, 1.0, 0.4, open) > levelNear(x, 1.0, 0.4, closed, 60) + 15);
    }

    void tiltingThePaddleBendsTheNoteUp() {
        // Held, then tilted up off the rim: the gap's air lets the mouth
        // breathe, and the closed note rises toward the open one, as a key
        // opening over a tone hole.
        Play shut, tilted;
        shut.pipe = tilted.pipe = 4;
        shut.hold = tilted.hold = 0.2;
        tilted.open = 0.4;
        tilted.gap = 0.4f; // 1.8 mm off the rim
        shut.seconds = tilted.seconds = 1.0;
        drums::Engine e;
        e.init(kRate);
        const auto a = play(shut), b = play(tilted, &e);
        const double closed = pipedrum::Body::textbookClosedHz(e.pipeBody().length(4));
        const double open = e.pipeBody().openHz(4);
        const double fa = peakHz(a, 0.6, 0.35, closed, 200), fb = peakHz(b, 0.6, 0.35, std::sqrt(closed * open), 600);
        QVERIFY2(std::fabs(cents(fa, closed)) < 40, qPrintable(QStringLiteral("%1 Hz shut").arg(fa)));
        QVERIFY2(fb > 1.5 * fa && fb < open, qPrintable(QStringLiteral("%1 → %2 Hz (open %3)").arg(fa).arg(fb).arg(open)));
    }

    // --- the slap -----------------------------------------------------------------

    void harderIsLouderBrighterAndShorter() {
        Play soft, hard;
        soft.pipe = hard.pipe = 1;
        soft.vel = 0.25f;
        hard.vel = 1.0f;
        soft.seconds = hard.seconds = 0.5;
        drums::Engine es, eh;
        es.init(kRate);
        eh.init(kRate);
        const auto a = play(soft, &es);
        const float softContact = es.pipeBody().lastContact();
        const auto b = play(hard, &eh);
        const float hardContact = eh.pipeBody().lastContact();
        const double f1 = es.pipeBody().openHz(1);
        const double la = levelNear(a, 0, 0.1, f1), lb = levelNear(b, 0, 0.1, f1);
        QVERIFY2(lb > la + 8, qPrintable(QStringLiteral("%1 %2").arg(la).arg(lb)));
        // Brighter: the 4th harmonic over the note.
        const double ba = levelNear(a, 0, 0.1, 4 * f1) - la, bb = levelNear(b, 0, 0.1, 4 * f1) - lb;
        QVERIFY2(bb > ba + 3, qPrintable(QStringLiteral("%1 %2").arg(ba).arg(bb)));
        // The foam springs back sooner from a harder slap; a slap's contact
        // is a few milliseconds.
        QVERIFY2(hardContact < softContact, qPrintable(QStringLiteral("%1 %2").arg(softContact).arg(hardContact)));
        QVERIFY(hardContact > 0.0008f && softContact < 0.008f);
    }

    void theSlapPushesAPulseIntoThePipe() {
        // A sole arriving at v pushes the column like a piston: ρcv inside
        // the mouth, some kilopascals for a hard slap (slap tubes are loud).
        drums::Engine e;
        e.init(kRate);
        Play pl;
        pl.pipe = 5;
        pl.vel = 1.0f;
        pl.seconds = 0.05;
        play(pl, &e);
        const float p = e.pipeBody().peakPressure();
        QVERIFY2(p > 1000 && p < 6000, qPrintable(QString::number(p)));
    }

    void aGlancingBlowIsMorePlasticAndLessAir() {
        // Struck past its middle, the sole lands on one side of the rim: it
        // seals less of the mouth and pushes less air in, and more of its
        // force goes across the pipe into the plastic.
        Play mid, edge;
        mid.pipe = edge.pipe = 2;
        edge.u = 1.3f;
        mid.seconds = edge.seconds = 0.4;
        drums::Engine e;
        e.init(kRate);
        const auto a = play(mid), b = play(edge, &e);
        const double f1 = e.pipeBody().openHz(2);
        QVERIFY2(levelNear(b, 0.05, 0.2, f1) < levelNear(a, 0.05, 0.2, f1) - 4, "less air");
        const double bend = e.pipeBody().wallHz(2, 3);
        QVERIFY2(levelNear(b, 0, 0.05, bend, 30) - levelNear(b, 0.05, 0.2, f1) >
                     levelNear(a, 0, 0.05, bend, 30) - levelNear(a, 0.05, 0.2, f1),
                 "more plastic");
    }

    void noGestureClicks() {
        // A paddle is a body moving through air: nothing it does may step
        // the sound from one sample to the next. A slap, a re-slap on a
        // ringing pipe, a glancing blow, the paddle put down, tilted and
        // lifted: the second difference of the output stays within the
        // window's own level (a 3 kHz tone's is 0.15 of it; a click's is
        // many times it).
        auto worst = [](const std::vector<float> &x) {
            double top = 0;
            for (size_t c = 0; c + 240 < x.size(); c += 60) {
                double e = 0, d = 0;
                for (size_t i = c + 2; i < c + 240; ++i) {
                    e += double(x[i]) * x[i];
                    d = std::max(d, std::fabs(double(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
                }
                const double rms = std::sqrt(e / 238);
                if (rms > 1e-4)
                    top = std::max(top, d / rms);
            }
            return top;
        };
        struct Case {
            const char *what;
            Play pl;
        };
        std::vector<Case> cases;
        for (float v : {0.2f, 0.6f, 1.0f}) {
            Play p;
            p.pipe = 2;
            p.vel = v;
            p.seconds = 0.5;
            cases.push_back({"slap", p});
        }
        for (float u : {0.7f, 1.0f, 1.3f, 1.8f}) {
            Play p;
            p.pipe = 0;
            p.u = u;
            p.seconds = 0.5;
            cases.push_back({"glancing", p});
        }
        {
            Play p;
            p.pipe = 5;
            p.hold = 0.2;
            p.open = 0.5;
            p.gap = 0.5f;
            p.lift = 0.8;
            p.seconds = 1.2;
            cases.push_back({"held, tilted, lifted", p});
        }
        for (int pipe : {1, 9}) {
            // slapped, then slapped back on and held while it still rings
            Play p;
            p.pipe = pipe;
            p.hold = 0.2;
            p.seconds = 0.6;
            cases.push_back({"slapped back on", p});
        }
        for (const Case &c : cases) {
            const double w = worst(play(c.pl));
            QVERIFY2(w < 1.5, qPrintable(QStringLiteral("%1: %2").arg(c.what).arg(w)));
        }
        // A soft re-slap on a ringing pipe.
        drums::Engine e;
        e.init(kRate);
        Play first;
        first.pipe = 2;
        first.seconds = 0.15;
        std::vector<float> x = play(first, &e);
        Play again = first;
        again.vel = 0.2f;
        again.seconds = 0.4;
        const std::vector<float> y = play(again, &e);
        x.insert(x.end(), y.begin(), y.end());
        QVERIFY2(worst(x) < 1.5, qPrintable(QStringLiteral("re-slap: %1").arg(worst(x))));
    }

    void thePlasticRingsBriefly() {
        // PVC's loss factor of a couple of hundredths: its modes die in tens
        // of milliseconds, under the air's second.
        drums::Engine e;
        const pipedrum::Body &b = tuned(e);
        for (int k = 0; k < pipedrum::Body::kWallModes; ++k) {
            QVERIFY(b.wallTau(0, k) < 0.1f);
            QVERIFY(b.wallHz(0, k) > 50.0f);
        }
        // Ovalling: the same in every pipe (a ring's, set by wall and radius);
        // the axial modes fall with length.
        QVERIFY(std::fabs(b.wallHz(0, 5) - b.wallHz(9, 5)) < 1.0f);
        QVERIFY(b.wallHz(0, 0) < b.wallHz(9, 0));
    }

    // --- decay ----------------------------------------------------------------------

    void itRingsForAboutASecond() {
        // The boundary layer and the open ends take a low pipe's note down
        // 60 dB in one to two seconds; a short pipe's sooner.
        Play low, high;
        low.pipe = 0;
        high.pipe = 9;
        low.seconds = high.seconds = 1.2;
        drums::Engine e;
        const pipedrum::Body &b = tuned(e);
        const auto x = play(low), y = play(high);
        const double t60 = 6.91 * tauOf(x, b.openHz(0), 0.2, 0.8), t60h = 6.91 * tauOf(y, b.openHz(9), 0.2, 0.8);
        QVERIFY2(t60 > 0.9 && t60 < 2.2, qPrintable(QString::number(t60)));
        QVERIFY2(t60h > 0.5 && t60h < t60, qPrintable(QStringLiteral("%1 %2").arg(t60h).arg(t60)));
    }

    void itFallsSilentAndSleeps() {
        drums::Engine e;
        e.init(kRate);
        Play pl;
        pl.seconds = 4;
        play(pl, &e);
        QVERIFY(!e.pipeBody().active());
        QCOMPARE(e.voices(), 0);
    }

    void aRollNeverRunsAway() {
        // Slapped thirty times a second for two seconds, the pipe stays
        // finite, and no louder than a few slaps landing in phase.
        Ctx ctx;
        Play one;
        one.vel = 1.0f;
        one.seconds = 0.5;
        float single = 0;
        for (float v : play(one))
            single = std::max(single, std::fabs(v));
        drums::Engine e;
        e.init(kRate);
        float l[kBlock], r[kBlock], top = 0;
        for (int b = 0; b < int(2 * kRate) / kBlock; ++b) {
            if (b % 50 == 0) {
                drums::Strike s;
                s.kit = drums::Pipes;
                s.pad = drums::pipePad(0);
                s.vel = 1.0f;
                e.strike(s, ctx.h);
            }
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
            for (int i = 0; i < kBlock; ++i) {
                QVERIFY(std::isfinite(l[i]));
                top = std::max(top, std::fabs(0.5f * (l[i] + r[i])));
            }
        }
        QVERIFY2(top < 3.0f * single, qPrintable(QStringLiteral("%1 against %2").arg(top).arg(single)));
    }

    // --- the page -------------------------------------------------------------

    void thePadIsTheRack() {
        // The lowest pipe at the left of the near row, the highest at the
        // right of the far one; anywhere plays the nearest mouth.
        auto struck = [](float x, float y) {
            const auto take = testkit::play(toPipes() + QStringLiteral("tap 0.5 %1 %2 p=0.7 len=0.05\nend 0.56\n")
                                                .arg(x)
                                                .arg(y)
                                                .toStdString());
            int best = -1;
            float top = 0.2f;
            for (int p = 0; p < drums::PadCount; ++p)
                if (take.view.parts[0].data[p] > top)
                    top = take.view.parts[0].data[p], best = p;
            return best < 0 ? -1 : drums::pipeOf(best);
        };
        const pipedrum::Mouth lo = pipedrum::mouth(0), hi = pipedrum::mouth(9), mid = pipedrum::mouth(5);
        QCOMPARE(struck(lo.x, lo.y), 0);
        QCOMPARE(struck(hi.x, hi.y), 9);
        QCOMPARE(struck(mid.x, mid.y), 5);
        QCOMPARE(struck(0.02f, 0.1f), 0);
        const auto take = testkit::play(toPipes() + "tap 0.5 0.095 0.36 p=0.7 len=0.05\nend 2\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(0.5, 1.5) > 1e-3);
        QVERIFY(take.peak() < 1.0);
    }

    void aHeldFingerKeepsThePaddleOn() {
        // A finger held on a mouth: after a moment the paddle goes back on
        // and the pipe sounds closed, a little more than an octave below
        // its open note; a quick tap stays open throughout. (The studio's
        // own key: the open note is read off the tap.)
        const pipedrum::Mouth m = pipedrum::mouth(2);
        auto mono = [](const testkit::Take &t) {
            std::vector<float> x(t.left.size());
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 0.5f * (t.left[i] + t.right[i]);
            return x;
        };
        const auto tap = testkit::play(fmt((toPipes() + "tap 0.5 %.3f %.3f p=0.7 len=0.14\nend 1.4\n").c_str(), m.x, m.y));
        const auto y = mono(tap);
        double open = 0, top = -1e9;
        for (double f = 80; f < 400; f += 0.5)
            if (const double l = levelAt(y, 0.6, 0.4, f); l > top)
                top = l, open = f;
        open = peakHz(y, 0.6, 0.4, open, 10);
        const auto held = testkit::play(fmt((toPipes() + "down 0.5 1 %.3f %.3f p=0.7\nup 1.6 1\nend 2.4\n").c_str(), m.x, m.y));
        QVERIFY(held.finite());
        const auto x = mono(held);
        const double closed = peakHz(x, 1.0, 0.5, 0.53 * open, 120);
        QVERIFY2(closed > 0.5 * open && closed < 0.56 * open, qPrintable(QStringLiteral("%1 Hz closed, %2 open").arg(closed).arg(open)));
        QVERIFY2(levelNear(x, 1.0, 0.5, closed, 20) > levelNear(x, 1.0, 0.5, open, 30) + 10, "closed while held");
        QVERIFY2(levelNear(y, 0.7, 0.5, open, 20) > levelNear(y, 0.7, 0.5, 0.53 * open, 120) + 10, "open after a tap");
        // Let go, it opens again.
        QVERIFY2(levelNear(x, 1.8, 0.5, open, 20) > levelNear(x, 1.8, 0.5, closed, 20) + 10, "open once lifted");
    }

    void aLoopOfThePipesKeepsThePipes() {
        // Hits carry the kit: a loop of the pipes sounds as pipes after the
        // page has gone back to house.
        const auto take = testkit::play(toPipes() + "tap 0.6 0.095 0.36 p=0.7 len=0.05\n"
                                        "tap 0.9 0.545 0.74 p=0.7 len=0.05\nkey 1.2 Space\nkey 1.5 Right\nend 6\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(3, 6) > 1e-4);
    }

    // --- the cost -------------------------------------------------------------

    void tenPipesCostLittle() {
        // Each ringing pipe is a waveguide and a handful of modes: all ten
        // slapped at once cost well under a percent of a core per second of
        // sound, and nothing feeds on the crowd.
        TENFOLD_TIMED();
        Ctx ctx;
        float top = 0;
        auto timed = [&](int pipes) {
            drums::Engine e;
            e.init(kRate);
            float l[kBlock], r[kBlock];
            top = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < int(4 * kRate) / kBlock; ++b) {
                if (b % 300 == 0)
                    for (int f = 0; f < pipes; ++f) {
                        drums::Strike s;
                        s.kit = drums::Pipes;
                        s.pad = drums::pipePad(f);
                        s.vel = 0.9f;
                        e.strike(s, ctx.h);
                    }
                std::fill(l, l + kBlock, 0.0f);
                std::fill(r, r + kBlock, 0.0f);
                e.render(l, r, kBlock, ctx);
                for (int i = 0; i < kBlock; ++i) {
                    if (!std::isfinite(l[i]))
                        return -1.0;
                    top = std::max(top, std::fabs(l[i]));
                }
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        };
        const double one = timed(1);
        const float oneTop = top;
        const double ten = timed(10);
        QVERIFY(one >= 0 && ten >= 0);
        QVERIFY2(top < 10 * oneTop, qPrintable(QStringLiteral("%1 %2").arg(oneTop).arg(top)));
        QVERIFY2(ten < 0.03, qPrintable(QString::number(ten)));
        QVERIFY2(one < 0.005, qPrintable(QString::number(one)));
    }

    void tenFingersOnThePipesStayInsideBudget() {
        // Ten fingers drumming on the pipes through the whole studio, as the
        // other pages' budgets are measured, then all ten held down.
        TENFOLD_TIMED();
        std::string s = toPipes();
        for (int f = 0; f < 10; ++f) {
            const pipedrum::Mouth m = pipedrum::mouth(f);
            for (int k = 0; k < 12; ++k)
                s += fmt("tap %.3f %.3f %.3f p=0.8 len=0.05\n", 0.3 + 0.25 * k + 0.02 * f, m.x, m.y);
        }
        for (int f = 0; f < 10; ++f) {
            const pipedrum::Mouth m = pipedrum::mouth(f);
            s += fmt("down %.3f %d %.3f %.3f p=0.7\n", 3.5 + 0.01 * f, 100 + f, m.x, m.y);
        }
        s += "end 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(PipeDrumTests)
#include "pipedrum_tests.moc"
