// The udu kit: a clay pot drum of air and clay (udu.h), checked against
// what the acoustics papers measure on real ones (docs/udu.md):
//   - its Helmholtz bass and how its two openings share it;
//   - the octave glide as the palm leaves the hole, and the bend as a
//     finger covers the hole or the mouth;
//   - its damping;
//   - its clay's modes and where a finger finds them;
//   - that nothing feeds on itself;
//   - what it costs.

#include <chrono>
#include <vector>

#include "drums.h"
#include "offline.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[512];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

struct Ctx : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    // D minor: the udu's open bass on D3.
    Ctx() { h.tuning.set(2, 0, 4, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 1.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// A pot on its own (the reference, 10.2 litres), and what it gives over
// `seconds`, both ears summed; `each` runs before every block.
std::vector<float> run(udu::Pot &p, double seconds, const std::function<void(int)> &each = {}) {
    std::vector<float> out;
    float l[kBlock], r[kBlock];
    for (int b = 0; b < int(seconds * kRate) / kBlock; ++b) {
        if (each)
            each(b);
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        p.render(l, r, kBlock);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(0.5f * (l[i] + r[i]));
    }
    return out;
}

// A hit on the udu kit through the drums' engine (place, where on the pot).
std::vector<float> hit(int place, float X, float Y, float vel, double seconds, drums::Engine *keep = nullptr) {
    Ctx ctx;
    drums::Engine local;
    drums::Engine &e = keep ? *keep : local;
    if (!keep)
        e.init(kRate);
    drums::Strike s;
    s.kit = drums::Udu;
    s.pad = drums::uduPad(place);
    s.vel = vel;
    s.sx = drums::uduSpotX(X);
    s.sy = drums::uduSpotY(Y);
    e.strike(s, ctx.h);
    std::vector<float> out;
    float l[kBlock], r[kBlock];
    for (int b = 0; b < int(seconds * kRate) / kBlock; ++b) {
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

// The strongest frequency between lo and hi over a window: coarse, then fine.
double peakHz(const std::vector<float> &x, double from, double len, double lo, double hi) {
    double best = lo, top = -1e9;
    for (double f = lo; f <= hi; f += 0.5) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    const double c = best;
    for (double f = c - 0.5; f <= c + 0.5; f += 0.02) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    return best;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

// The bass's pitch, period by period: upward zero crossings of the signal
// low-passed below the clay (two poles at 300 Hz), as (time, Hz).
std::vector<std::pair<double, double>> pitchTrack(const std::vector<float> &x) {
    std::vector<std::pair<double, double>> out;
    const double c = 1 - std::exp(-2 * M_PI * 300 / kRate);
    double a = 0, b = 0, prev = 0, last = -1;
    for (size_t i = 0; i < x.size(); ++i) {
        a += c * (x[i] - a);
        b += c * (a - b);
        if (prev < 0 && b >= 0) {
            const double t = (double(i) - 1 + (-prev) / (b - prev)) / kRate;
            if (last > 0)
                out.push_back({t, 1 / (t - last)});
            last = t;
        }
        prev = b;
    }
    return out;
}

// The pitch the track gives around time t (the median of the periods
// within ±w).
double pitchAt(const std::vector<std::pair<double, double>> &track, double t, double w = 0.02) {
    std::vector<double> v;
    for (const auto &p : track)
        if (std::fabs(p.first - t) <= w)
            v.push_back(p.second);
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// The time constant of a frequency's decay between two times, from its level.
double tauOf(const std::vector<float> &x, double hz, double t0, double t1, double len = 0.1) {
    const double a = levelAt(x, t0, len, hz), b = levelAt(x, t1, len, hz);
    return (t1 - t0) * 20 / std::log(10.0) / (a - b);
}

// The page on the udu: Left from house (round to the last kit) as many
// times as it is from the end of the list.
std::string toUdu() {
    std::string s = "page 0 1\n";
    for (int i = 0; i < drums::KitCount - drums::Udu; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "key %.3f Left\n", 0.1 + 0.004 * i);
        s += buf;
    }
    return s;
}

// The pad's point for a point of the pot (drums.cpp draws it there).
float padX(float X) { return 0.5f + 0.30f * X; }
float padY(float Y) { return 0.40f + 0.36f * Y; }

}

class UduTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenFingersOnTheUduStayInsideBudget")
private slots:
    // --- the air ----------------------------------------------------------------

    void itIsTheMeasuredUdu() {
        // Vieira & Aguiar's udu: 10.2 litres, a neck 4.4 cm across and 6.3 cm
        // long, a 6 cm hole in a 12 mm wall. Measured: 68 Hz with the hole
        // covered, 116 Hz with the mouth covered, 136 Hz open. Helmholtz's
        // formula with the measured end corrections, the model's, within a
        // few percent of each.
        udu::Pot p;
        p.init(kRate);
        QVERIFY(std::fabs(p.volume() - 0.0102f) < 1e-5f);
        const double mouth = p.heardHz(0, 1), hole = p.heardHz(1, 0), both = p.heardHz(1, 1);
        QVERIFY2(std::fabs(cents(mouth, 68)) < 50, qPrintable(QString::number(mouth)));
        QVERIFY2(std::fabs(cents(hole, 116)) < 50, qPrintable(QString::number(hole)));
        QVERIFY2(std::fabs(cents(both, 136)) < 50, qPrintable(QString::number(both)));
    }

    void theOpeningsConductanceAdds() {
        // Two necks on one cavity: conductances add, so f² = f₁² + f₂², as
        // Helmholtz and Rayleigh knew and both measured udus show (within 2%).
        udu::Pot p;
        p.init(kRate);
        const double m = p.helmholtzHz(0, 1), h = p.helmholtzHz(1, 0), b = p.helmholtzHz(1, 1);
        QVERIFY(std::fabs(b * b - (m * m + h * h)) < 1e-3 * b * b);
        // The bass follows the formula: c/2π·√(ΣS/L′/V), each L′ the neck plus
        // its end corrections (0.82a inside, 0.61a at a free lip, 0.85a at a
        // flanged one).
        double k = 0;
        for (int i = 0; i < 2; ++i)
            k += M_PI * p.radius(i) * p.radius(i) / p.neckLength(i);
        QVERIFY(std::fabs(b - 343.0 / (2 * M_PI) * std::sqrt(k / p.volume())) < 0.01 * b);
        QVERIFY(std::fabs(p.neckLength(udu::Mouth) - (0.063 + 1.43 * 0.022)) < 1e-4);
        QVERIFY(std::fabs(p.neckLength(udu::Hole) - (0.012 + 1.67 * 0.030)) < 1e-4);
    }

    void aSlapHeldOnTheHoleSoundsTheMouthAlone() {
        // The palm stays on the hole: the air rings through the mouth alone,
        // at the formula's frequency (the model as built, the cavity's
        // standing waves and the shell's give and all).
        udu::Pot p;
        p.init(kRate);
        p.cover(1, 1, 0, 0.8f, 0);
        p.slap(udu::Hole, 0.6f, 0, 1, 0);
        const auto x = run(p, 0.8);
        const double got = peakHz(x, 0.15, 0.5, 40, 200);
        QVERIFY2(std::fabs(cents(got, p.heardHz(0, 1))) < 15, qPrintable(QStringLiteral("%1 %2").arg(got).arg(p.heardHz(0, 1))));
        // And it is a bass: everything above the clay's lowest mode far below it.
        QVERIFY(levelAt(x, 0.15, 0.3, got) > levelAt(x, 0.15, 0.3, 556) + 30);
    }

    void releasingThePalmGlidesUpAnOctave() {
        // The udu's whoop: slapped and let go, the bass starts at the mouth's
        // note and rises nearly an octave to the open note as the palm lifts
        // (measured: 68 → 136 Hz, 73 → 137 Hz), most of it in the first few
        // tens of milliseconds, always up.
        // (a moderate slap: a hard one's deep phase is bent out of shape by
        // the jet at the mouth, and its pulse fills a good part of a period)
        udu::Pot p;
        p.init(kRate);
        p.slap(udu::Hole, 0.25f, 0, 1, 0);
        const auto x = run(p, 0.6);
        const auto track = pitchTrack(x);
        const double start = pitchAt(track, 0.035, 0.01), end = pitchAt(track, 0.35, 0.05);
        // (a little sharp while the palm is still settling into its flesh)
        QVERIFY2(std::fabs(cents(start, p.heardHz(0, 1))) < 250, qPrintable(QStringLiteral("%1 Hz").arg(start)));
        QVERIFY2(std::fabs(cents(end, p.heardHz(1, 1))) < 15, qPrintable(QString::number(end)));
        const double range = cents(end, p.heardHz(0, 1));
        QVERIFY2(range > 1000 && range < 1250, qPrintable(QString::number(range)));
        // It rises (never falls more than a hair) once the palm leaves, and
        // two thirds of the way within 50 ms of the release.
        const double release = udu::Knobs::dwell;
        double prev = 0, twoThirds = -1;
        for (const auto &q : track) {
            if (q.first < release + 0.005 || q.first > 0.35)
                continue;
            QVERIFY2(q.second > prev - 4, qPrintable(QStringLiteral("%1 s: %2 after %3").arg(q.first).arg(q.second).arg(prev)));
            prev = std::max(prev, q.second);
            if (twoThirds < 0 && cents(q.second, start) > 2.0 / 3.0 * cents(end, start))
                twoThirds = q.first;
        }
        QVERIFY2(twoThirds > 0 && twoThirds - release < 0.05, qPrintable(QString::number(twoThirds - release)));
    }

    void thePalmStaysOnThroughTheDwell() {
        // The forearm follows through: however hard the slap, the palm stays
        // pressed on the lip, sealed, until it lifts, so the held note is the
        // mouth's and the whoop rises its full octave at every strength.
        for (float vel : {0.3f, 0.5f, 0.7f, 1.0f}) {
            udu::Pot p;
            p.init(kRate);
            p.slap(udu::Hole, vel, 0, 1, 0);
            float highest = -1;
            const auto x = run(p, 0.6, [&](int b) {
                const double t = double(b * kBlock) / kRate;
                if (t > 0.012 && t < udu::Knobs::dwell - 0.002)
                    highest = std::max(highest, p.palmHeight(udu::Hole));
            });
            QVERIFY2(highest < 0, qPrintable(QStringLiteral("%1: %2 mm").arg(vel).arg(highest * 1000)));
            const auto track = pitchTrack(x);
            const double held = pitchAt(track, 0.035, 0.01), open = pitchAt(track, 0.35, 0.05);
            QVERIFY2(cents(open, held) > 1100, qPrintable(QStringLiteral("%1: %2 → %3 Hz").arg(vel).arg(held).arg(open)));
        }
    }

    void aSlapOnTheMouthOpensToTheBothOpenNote() {
        // Slapped on the mouth with the hole open: the hole's note while the
        // palm stays, the open note once it is let go (+275…333 cents
        // measured between them).
        udu::Pot held;
        held.init(kRate);
        held.cover(1, 0, 1, 0.8f, 0);
        held.slap(udu::Mouth, 0.7f, 0, 1, 0);
        const double under = peakHz(run(held, 0.6), 0.1, 0.45, 40, 250);
        QVERIFY2(std::fabs(cents(under, held.heardHz(1, 0))) < 20, qPrintable(QString::number(under)));
        udu::Pot p;
        p.init(kRate);
        p.slap(udu::Mouth, 0.7f, 0, 1, 0);
        const auto track = pitchTrack(run(p, 0.5));
        const double open = pitchAt(track, 0.3, 0.05);
        QVERIFY2(std::fabs(cents(open, p.heardHz(1, 1))) < 15, qPrintable(QString::number(open)));
    }

    void coveringTheMouthLowersTheBass() {
        // A hand resting over the mouth: a slap on the hole (let go) sounds
        // the hole's note, a major third under the open one.
        udu::Pot p;
        p.init(kRate);
        p.cover(2, 0, 1, 0.8f, 0);
        run(p, 0.2); // the hand settles
        p.slap(udu::Hole, 0.6f, 0, 1, 0);
        const auto x = run(p, 0.8);
        const double got = peakHz(x, 0.25, 0.5, 40, 200);
        QVERIFY2(std::fabs(cents(got, p.heardHz(1, 0))) < 20, qPrintable(QStringLiteral("%1 %2").arg(got).arg(p.heardHz(1, 0))));
    }

    void slidingAFingerOffTheHoleBendsTheBassUp() {
        // A finger resting over the whole hole, then sliding off it over
        // 0.4 s: the bass glides up from the mouth's note to the open one,
        // continuously, never stepping down. And back on: down again.
        udu::Pot p;
        p.init(kRate);
        p.cover(3, 1, 0, 0.8f, 0);
        p.slap(udu::Hole, 0.8f, 0, 1, 0);
        const int start = int(0.15 * kRate) / kBlock, end = int(0.55 * kRate) / kBlock;
        const auto x = run(p, 1.2, [&](int b) {
            if (b >= start && b <= end && (b - start) % 8 == 0) {
                const float share = 1.0f - float(b - start) / float(end - start);
                p.cover(3, share, 0, 0.8f, 0);
            }
            if (b == int(0.8 * kRate) / kBlock)
                p.cover(3, 1, 0, 0.8f, 0);
        });
        const auto track = pitchTrack(x);
        const double low = pitchAt(track, 0.1), high = pitchAt(track, 0.65), again = pitchAt(track, 1.0, 0.05);
        QVERIFY2(std::fabs(cents(low, p.heardHz(0, 1))) < 40, qPrintable(QString::number(low)));
        QVERIFY2(std::fabs(cents(high, p.heardHz(1, 1))) < 40, qPrintable(QString::number(high)));
        QVERIFY2(std::fabs(cents(again, p.heardHz(0, 1))) < 60, qPrintable(QString::number(again)));
        // Through the slide the pitch climbs, and passes through the middle
        // of the octave: a glide, not a jump.
        double prev = 0;
        int between = 0;
        for (const auto &q : track) {
            if (q.first < 0.17 || q.first > 0.6)
                continue;
            QVERIFY2(q.second > prev * 0.97, qPrintable(QStringLiteral("%1 s: %2 after %3").arg(q.first).arg(q.second).arg(prev)));
            prev = std::max(prev, q.second);
            between += cents(q.second, low) > 250 && cents(high, q.second) > 250;
        }
        QVERIFY2(between >= 3, qPrintable(QString::number(between)));
    }

    void partCoveringIsBetween() {
        // A finger over half the hole: the bass is between the mouth's note
        // and the open one, as the crescent left conducts (half^0.6 of it).
        udu::Pot p;
        p.init(kRate);
        p.cover(4, 0.5f, 0, 0.8f, 0);
        run(p, 0.1);
        p.slap(udu::Mouth, 0.7f, 0, 1, 0);
        const auto x = run(p, 0.8);
        const double got = peakHz(x, 0.25, 0.5, 40, 200);
        QVERIFY2(got > p.heardHz(0, 1) * 1.15 && got < p.heardHz(1, 1) * 0.97,
                 qPrintable(QStringLiteral("%1 between %2 and %3").arg(got).arg(p.heardHz(0, 1)).arg(p.heardHz(1, 1))));
        QVERIFY2(std::fabs(cents(got, p.heardHz(0.5f, 1))) < 60, qPrintable(QStringLiteral("%1 %2").arg(got).arg(p.heardHz(0.5f, 1))));
    }

    void theKitSizesThePotToTheKey() {
        // In D minor the open bass is D3, and the hole held an octave below,
        // whatever the pot's size: a smaller pot, as a player would pick.
        Ctx ctx;
        QVERIFY(std::fabs(cents(drums::uduHz(ctx.h), midiToHz(50))) < 0.5);
        drums::Engine e;
        e.init(kRate);
        const auto x = hit(udu::Mouth, 0.0f, udu::kMouthY, 0.6f, 0.8, &e);
        const double got = peakHz(x, 0.25, 0.5, 60, 300);
        QVERIFY2(std::fabs(cents(got, midiToHz(50))) < 15, qPrintable(QString::number(got)));
        QVERIFY(e.uduPot().volume() < 0.0102f && e.uduPot().volume() > 0.006f);
        // In another key the root moves, between 99 and 198 Hz.
        ctx.h.tuning.set(9, 0, 4, false);
        const float a = drums::uduHz(ctx.h);
        QVERIFY(a > 98 && a < 199);
    }

    void theBassDecaysAsTheLossesSay() {
        // A soft slap, let go: the open bass rings with the Q its losses
        // give (radiation, the openings' layers, the walls: about 45 open, a
        // moringa measured 57), its T60 under a second. Measured, not just
        // computed: the level's slope between 0.2 and 0.6 s.
        udu::Pot p;
        p.init(kRate);
        const double q = p.bassQ();
        QVERIFY2(q > 35 && q < 60, qPrintable(QString::number(q)));
        p.slap(udu::Hole, 0.15f, 0, 1, 0);
        const auto x = run(p, 1.0);
        const double hz = p.heardHz(), tau = tauOf(x, hz, 0.2, 0.6, 0.15);
        const double measured = M_PI * hz * tau;
        QVERIFY2(std::fabs(measured / q - 1) < 0.25, qPrintable(QStringLiteral("%1 vs %2").arg(measured).arg(q)));
        QVERIFY2(tau * 6.91 > 0.5 && tau * 6.91 < 1.2, qPrintable(QString::number(tau * 6.91)));
    }

    void aHardSlapDiesFasterAtFirst() {
        // The jet that separates at the mouth's lip when the air moves fast:
        // held on the hole, a hard slap's bass falls faster at first than a
        // soft one's (both then ring out alike).
        auto fall = [](float vel) {
            udu::Pot p;
            p.init(kRate);
            p.cover(1, 1, 0, 0.8f, 0);
            p.slap(udu::Hole, vel, 0, 1, 0);
            const auto x = run(p, 0.6);
            const double hz = p.heardHz(0, 1);
            return levelAt(x, 0.02, 0.05, hz) - levelAt(x, 0.1, 0.05, hz);
        };
        const double soft = fall(0.1f), hard = fall(1.0f);
        QVERIFY2(hard > soft + 1.5, qPrintable(QStringLiteral("soft %1 dB, hard %2 dB").arg(soft).arg(hard)));
    }

    void theSlapsPulseIsAFewHundredPascals() {
        // The palm's pad driven into the hole: the pot squeezed by tens of
        // pascals for a soft slap to several hundred for a hard one (the
        // survey: 40–200 Pa, a hard one more, as a clap's cavity), more than
        // 20 dB of range, rising over several milliseconds.
        auto peak = [](float vel) {
            udu::Pot p;
            p.init(kRate);
            p.slap(udu::Hole, vel, 0, 1, 0);
            float top = 0;
            run(p, 0.05, [&](int) { top = std::max(top, std::fabs(p.pressure())); });
            return top;
        };
        const float firm = peak(0.6f), hard = peak(1.0f), soft = peak(0.15f), faint = peak(0.05f);
        QVERIFY2(firm > 120 && firm < 700, qPrintable(QString::number(firm)));
        QVERIFY2(hard > 300 && hard < 1300, qPrintable(QString::number(hard)));
        QVERIFY2(soft > 20 && soft < 150, qPrintable(QString::number(soft)));
        QVERIFY2(20 * std::log10(hard / faint) > 20, qPrintable(QStringLiteral("%1 %2").arg(faint).arg(hard)));
    }

    // --- the clay ---------------------------------------------------------------

    void theClaysModesAreTheMeasuredOnes() {
        // Measured on an udu (Anderson, Hilton & Giorgini): 556, 1108, 1644 Hz
        // at the lip (1 : 1.99 : 2.96), 970 and 1898 Hz at the hole, 1378 and
        // 1470 Hz; the rest of the belly above them, below its ring frequency.
        udu::Pot p;
        p.init(kRate);
        const float want[] = {556, 1108, 1644, 970, 1898, 1378, 1470};
        for (float hz : want) {
            bool found = false;
            for (int k = 0; k < p.shellModes(); ++k)
                found |= std::fabs(cents(p.shellHz(k), hz)) < 1;
            QVERIFY2(found, qPrintable(QString::number(hz)));
        }
        for (int k = 0; k < p.shellModes(); ++k) {
            // (the belly's dense tail runs on above its ring frequency, where
            // bending takes over, to about 6 kHz)
            QVERIFY(p.shellHz(k) > 500 && p.shellHz(k) < 2.6f * p.ringHz());
            // The clay's loss factor (1–2 %): T60 6.91/(πfη).
            const double t60 = 6.91 * p.shellTau(k);
            const double eta = 6.91 / (M_PI * p.shellHz(k) * t60);
            QVERIFY2(eta > 0.01 && eta < 0.035, qPrintable(QStringLiteral("%1 Hz: η %2").arg(p.shellHz(k)).arg(eta)));
        }
        // The lowest rings longest, a few tenths of a second.
        QVERIFY(6.91 * p.shellTau(0) > 0.2 && 6.91 * p.shellTau(0) < 0.45);
    }

    void whereTheFingerLandsChoosesTheClay() {
        // A tap on the neck's lip rings its cowbell (556 Hz on the measured
        // pot); a tap by the hole rings the hole's 970 Hz louder than the
        // neck does. (The kit's pot is sized to the key: its modes with it.)
        drums::Engine e;
        e.init(kRate);
        const auto neck = hit(udu::Neck, 0.2f, 1.3f, 0.7f, 0.4, &e);
        const auto byHole = hit(udu::Body, udu::kHoleX + 0.05f, udu::kHoleY - 0.5f, 0.7f, 0.4, &e);
        const double lip = e.uduPot().shellHz(0), hole = e.uduPot().shellHz(3);
        QVERIFY(std::fabs(cents(lip * e.uduPot().scale(), 556)) < 1 && std::fabs(cents(hole * e.uduPot().scale(), 970)) < 1);
        const double n556 = levelAt(neck, 0.01, 0.1, lip), n970 = levelAt(neck, 0.01, 0.1, hole);
        const double h556 = levelAt(byHole, 0.01, 0.1, lip), h970 = levelAt(byHole, 0.01, 0.1, hole);
        QVERIFY2(n556 > n970 + 10, qPrintable(QStringLiteral("neck: 556 %1, 970 %2").arg(n556).arg(n970)));
        QVERIFY2(h970 > h556 + 10, qPrintable(QStringLiteral("by the hole: 556 %1, 970 %2").arg(h556).arg(h970)));
        QVERIFY2(h970 > n970 + 10, qPrintable(QStringLiteral("970: hole %1, neck %2").arg(h970).arg(n970)));
    }

    void theFingerTouchesForAMillisecondOrTwo() {
        // A fingertip on stiff clay: 0.5–3 ms, shorter when harder.
        drums::Engine e;
        e.init(kRate);
        hit(udu::Body, 0.3f, -0.4f, 0.3f, 0.1, &e);
        const float soft = e.uduPot().lastContact();
        hit(udu::Body, 0.3f, -0.4f, 1.0f, 0.1, &e);
        const float hard = e.uduPot().lastContact();
        QVERIFY2(soft > 0.0005f && soft < 0.003f, qPrintable(QString::number(soft)));
        QVERIFY2(hard < soft, qPrintable(QStringLiteral("%1 %2").arg(hard).arg(soft)));
    }

    void aBodyTapIsClayNotBass() {
        // The fingers on the body sound the clay, a few hundred Hz up, and
        // hardly squeeze the air: the bass stays far below.
        const auto x = hit(udu::Body, 0.3f, -0.4f, 0.8f, 0.3);
        udu::Pot ref;
        ref.init(kRate);
        double clay = -1e9;
        for (double f = 500; f < 3000; f += 5)
            clay = std::max(clay, levelAt(x, 0.0, 0.05, f));
        QVERIFY2(clay > levelAt(x, 0.0, 0.05, 146.8) + 20, qPrintable(QString::number(clay)));
        float top = 0;
        for (float v : x)
            top = std::max(top, std::fabs(v));
        QVERIFY(top > 1e-3f);
    }

    void aSlapIsBassNotClank() {
        // A firm slap on the hole is the bass: the clay, the cavity's standing
        // waves and the harmonics the jet at the mouth makes stay well under
        // it in the first 40 ms, in each ear
        // (a palm presses all round the lip, so the ring modes round the hole
        // feel almost none of it, and its flesh damps what is under it), and
        // the two ears hear the clay alike (each mode's own pattern toward
        // each microphone), so mono loses little.
        udu::Pot p;
        p.init(kRate);
        p.slap(udu::Hole, 0.7f, 0, 1, 0);
        std::vector<float> L, R;
        float l[kBlock], r[kBlock];
        for (int b = 0; b < int(0.3 * kRate) / kBlock; ++b) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            p.render(l, r, kBlock);
            L.insert(L.end(), l, l + kBlock);
            R.insert(R.end(), r, r + kBlock);
        }
        auto band = [](const std::vector<float> &x, float lo, float hi) {
            Svf a, b, c, d;
            a.set(lo, 0.7f, kRate), b.set(lo, 0.7f, kRate), c.set(hi, 0.7f, kRate), d.set(hi, 0.7f, kRate);
            std::vector<float> y(x.size());
            for (size_t i = 0; i < x.size(); ++i)
                y[i] = d.step(c.step(b.step(a.step(x[i]).high).high).low).low;
            return y;
        };
        auto db = [](const std::vector<float> &x, double t0, double t1) {
            double s = 0;
            for (size_t i = size_t(t0 * kRate); i < size_t(t1 * kRate); ++i)
                s += double(x[i]) * x[i];
            return 10 * std::log10(s / ((t1 - t0) * kRate) + 1e-30);
        };
        for (const auto *x : {&L, &R}) {
            const double clay = db(band(*x, 400, 3000), 0, 0.04), bass = db(band(*x, 30, 250), 0, 0.04);
            QVERIFY2(clay < bass - 10, qPrintable(QStringLiteral("clay %1, bass %2").arg(clay).arg(bass)));
        }
        const auto cl = band(L, 400, 3000), cr = band(R, 400, 3000);
        double ab = 0, aa = 0, bb = 0;
        std::vector<float> mono(cl.size());
        for (size_t i = 0; i < cl.size(); ++i) {
            ab += double(cl[i]) * cr[i], aa += double(cl[i]) * cl[i], bb += double(cr[i]) * cr[i];
            mono[i] = 0.5f * (cl[i] + cr[i]);
        }
        QVERIFY2(ab / std::sqrt(aa * bb) > 0.4, qPrintable(QString::number(ab / std::sqrt(aa * bb))));
        QVERIFY2(db(mono, 0, 0.3) > db(cl, 0, 0.3) - 3, qPrintable(QStringLiteral("%1 %2").arg(db(mono, 0, 0.3)).arg(db(cl, 0, 0.3))));
    }

    void twoFingersOnTheHoleAreOneSlap() {
        // A second slap on a palm still down (a flam, two fingers landing a
        // few milliseconds apart) drives it on from where it is: no louder a
        // crack than a slap, and no less bass.
        auto take = [](double dt) {
            udu::Pot p;
            p.init(kRate);
            p.slap(udu::Hole, 1.0f, 0, 1, 0);
            const int at = int(dt * kRate);
            return run(p, 0.6, [&](int b) {
                if (dt > 0 && b == at / kBlock)
                    p.slap(udu::Hole, 1.0f, 0, 1, at % kBlock);
            });
        };
        auto peak = [](const std::vector<float> &x) {
            float t = 0;
            for (float v : x)
                t = std::max(t, std::fabs(v));
            return 20 * std::log10(t);
        };
        auto bass = [](const std::vector<float> &x) {
            Svf a, b;
            a.set(250, 0.7f, kRate), b.set(250, 0.7f, kRate);
            double s = 0;
            for (float v : x) {
                const float y = b.step(a.step(v).low).low;
                s += double(y) * y;
            }
            return 10 * std::log10(s + 1e-30);
        };
        const auto one = take(0);
        for (double dt : {0.002, 0.0033, 0.0067, 0.008}) {
            const auto two = take(dt);
            QVERIFY2(peak(two) < peak(one) + 3, qPrintable(QStringLiteral("%1 ms: %2 vs %3").arg(dt * 1000).arg(peak(two)).arg(peak(one))));
            QVERIFY2(std::fabs(bass(two) - bass(one)) < 3, qPrintable(QStringLiteral("%1 ms: %2 vs %3").arg(dt * 1000).arg(bass(two)).arg(bass(one))));
        }
    }

    void aHeldNoteRingsAsTheLossesSay() {
        // With a hand left on the hole, the mouth's note rings with the Q its
        // losses give (the moringa, a hand on its hole, measured 57): the
        // hand on the hole is nearly airtight and takes little.
        udu::Pot p;
        p.init(kRate);
        p.cover(3, 1, 0, 1, 0);
        p.slap(udu::Hole, 0.2f, 0, 1, 0);
        const auto x = run(p, 1.2);
        const double hz = p.heardHz(0, 1), tau = tauOf(x, hz, 0.3, 0.8, 0.15);
        const double q = M_PI * hz * tau;
        QVERIFY2(q > 45 && std::fabs(q / p.bassQ(false, true) - 1) < 0.2, qPrintable(QStringLiteral("%1 vs %2").arg(q).arg(p.bassQ(false, true))));
    }

    void aHalfCoveredHoleDiesSooner() {
        // A finger over most of the hole leaves a thin crescent whose walls
        // and edge drink the flow: the tone rings for less ("cover it
        // completely, or the sound will be short").
        auto q = [](float cover) {
            udu::Pot p;
            p.init(kRate);
            p.cover(3, cover, 0, 0.8f, 0);
            run(p, 0.1);
            p.slap(udu::Mouth, 0.3f, 0, 1, 0);
            const auto x = run(p, 1.0);
            const double hz = peakHz(x, 0.25, 0.5, 40, 200);
            return M_PI * hz * tauOf(x, hz, 0.25, 0.6, 0.12);
        };
        const double open = q(0), most = q(0.9f);
        QVERIFY2(most < 0.8 * open, qPrintable(QStringLiteral("%1 vs %2").arg(most).arg(open)));
    }

    void tapsAreAVoice() {
        // Fingertips on the clay are a voice of their own, not a whisper:
        // a firm tap within about 15 dB of a slap played as hard, its level
        // growing smoothly with how hard it is played, and from a firm one up
        // a fingertip's "tk" above 3 kHz (a soft pad touches too long for it).
        auto rms = [](const std::vector<float> &x) {
            double s = 0;
            for (size_t i = 0; i < size_t(0.15 * kRate); ++i)
                s += double(x[i]) * x[i];
            return 10 * std::log10(s / (0.15 * kRate) + 1e-30);
        };
        udu::Pot a;
        a.init(kRate);
        a.slap(udu::Hole, 0.6f, 0, 1, 0);
        const double slap = rms(run(a, 0.3));
        double prev = -1e9;
        for (float v : {0.05f, 0.2f, 0.4f, 0.6f, 0.9f}) {
            udu::Pot p;
            p.init(kRate);
            p.strike(udu::Body, 0.3f, -0.4f, v, 0, 0);
            const auto x = run(p, 0.3);
            const double t = rms(x);
            QVERIFY2(prev < -1e8 || (t > prev + 3 && t < prev + 20), qPrintable(QStringLiteral("%1: %2 after %3").arg(v).arg(t).arg(prev)));
            prev = t;
            if (v == 0.6f)
                QVERIFY2(t > slap - 15, qPrintable(QStringLiteral("tap %1, slap %2").arg(t).arg(slap)));
            // (band energies: the "tk" is a short burst of noise, spread
            // over its band)
            auto bandDb = [&](float lo, float hi) {
                Svf a, b, c, d;
                a.set(lo, 0.7f, kRate), b.set(lo, 0.7f, kRate), c.set(hi, 0.7f, kRate), d.set(hi, 0.7f, kRate);
                double e = 0;
                for (size_t i = 0; i < size_t(0.05 * kRate); ++i) {
                    const float y = d.step(c.step(b.step(a.step(x[i]).high).high).low).low;
                    e += double(y) * y;
                }
                return 10 * std::log10(e + 1e-30);
            };
            const double hf = bandDb(3000, 8000), mid = bandDb(400, 3000);
            QVERIFY2(v < 0.4f || hf > mid - 25, qPrintable(QStringLiteral("%1: 3–8 kHz %2, below %3").arg(v).arg(hf).arg(mid)));
        }
    }

    // --- staying sane -----------------------------------------------------------

    void energyNeverRunsAway() {
        // Slaps, strokes and covers in every order, hard and fast, for six
        // seconds: the output stays finite and bounded, and once the hands
        // stop the pot rings down and sleeps.
        udu::Pot p;
        p.init(kRate);
        Rng rng(0x5eedu);
        float top = 0, first = 0;
        bool finite = true;
        const int stop = int(6 * kRate) / kBlock;
        const auto x = run(p, 9.0, [&](int b) {
            if (b >= stop) {
                if (b == stop)
                    for (int id = 0; id < 4; ++id)
                        p.uncover(id, 0);
                return;
            }
            const float r = rng.unit();
            if (r < 0.05f)
                p.slap(rng.unit() < 0.7f ? udu::Hole : udu::Mouth, 0.3f + 0.7f * rng.unit(), rng.bipolar(), 0.3f + 0.7f * rng.unit(), int(rng.unit() * kBlock));
            else if (r < 0.1f)
                p.strike(rng.unit() < 0.5f ? udu::Body : udu::Neck, rng.bipolar(), rng.bipolar(), rng.unit(), rng.bipolar(), 0);
            else if (r < 0.16f)
                p.cover(int(rng.unit() * 4), rng.unit() < 0.5f ? rng.unit() : 0.0f, rng.unit() < 0.3f ? rng.unit() : 0.0f, rng.unit(), 0);
        });
        for (size_t i = 0; i < x.size(); ++i) {
            finite &= std::isfinite(x[i]);
            top = std::max(top, std::fabs(x[i]));
            if (i < size_t(0.5 * kRate))
                first = std::max(first, std::fabs(x[i]));
        }
        QVERIFY(finite);
        // (bounded: a few of the hardest hits landing together, at most)
        QVERIFY2(top < 4.0f, qPrintable(QString::number(top)));
        QVERIFY(!p.active());
        // A slap repeated every 30 ms on a ringing pot adds up as slaps would
        // (at most +20 dB, all in phase), never more.
        udu::Pot q;
        q.init(kRate);
        float one = 0, roll = 0;
        const auto y = run(q, 1.5, [&](int b) {
            if (b % 45 == 0)
                q.slap(udu::Hole, 1.0f, 0, 1, 0);
        });
        for (size_t i = 0; i < y.size(); ++i) {
            roll = std::max(roll, std::fabs(y[i]));
            if (i < 45 * kBlock)
                one = std::max(one, std::fabs(y[i]));
        }
        QVERIFY2(20 * std::log10(roll / one) < 20, qPrintable(QString::number(20 * std::log10(roll / one))));
        // And the energy only ever falls once the hands are off it.
        udu::Pot e;
        e.init(kRate);
        e.slap(udu::Hole, 1.0f, 0, 1, 0);
        e.strike(udu::Body, 0.3f, -0.3f, 1.0f, 0, 0);
        run(e, 0.15);
        float prev = e.energy();
        bool falls = true;
        run(e, 1.0, [&](int b) {
            if (b % 16)
                return;
            const float now = e.energy();
            falls &= now <= prev * 1.02f + 1e-12f;
            prev = now;
        });
        QVERIFY(falls);
    }

    // --- the page ---------------------------------------------------------------

    void thePadIsTheUdu() {
        // The kit to the left of house is the udu. A tap on the hole slaps
        // it, at the top the mouth, low on the belly the clay, on the neck
        // the neck.
        auto struck = [](float X, float Y) {
            const auto take = testkit::play(toUdu() + QStringLiteral("tap 0.5 %1 %2 p=0.7\nend 0.56\n")
                                                .arg(padX(X))
                                                .arg(padY(Y))
                                                .toStdString());
            int best = -1;
            float top = 0.2f;
            for (int p = 0; p < drums::PadCount; ++p)
                if (take.view.parts[0].data[p] > top)
                    top = take.view.parts[0].data[p], best = p;
            return best < 0 ? -1 : drums::uduPlace(best);
        };
        QCOMPARE(struck(udu::kHoleX, udu::kHoleY), int(udu::Hole));
        QCOMPARE(struck(0.0f, udu::kMouthY), int(udu::Mouth));
        QCOMPARE(struck(0.4f, -0.5f), int(udu::Body));
        QCOMPARE(struck(0.05f, 1.05f), int(udu::Neck));
    }

    void aTapOnTheHoleWhoopsThroughTheStudio() {
        // A tap on the hole through the whole studio: low while the finger is
        // on it, the open note after it lifts, up an octave.
        const auto take = testkit::play(toUdu() + fmt("tap 0.5 %.4f %.4f p=0.8 len=0.15\nend 1.4\n",
                                            padX(udu::kHoleX), padY(udu::kHoleY)));
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
        std::vector<float> mono(take.left.size());
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (take.left[i] + take.right[i]);
        const auto track = pitchTrack(mono);
        // The open note on the studio's root; held, the mouth's about an
        // octave below.
        Harmony h;
        h.tuning.set(take.view.root, take.view.family, take.view.brightness, take.view.just);
        const double held = pitchAt(track, 0.6, 0.03), open = pitchAt(track, 0.9, 0.08);
        QVERIFY2(std::fabs(cents(open, drums::uduHz(h))) < 30, qPrintable(QStringLiteral("%1 %2").arg(open).arg(drums::uduHz(h))));
        QVERIFY2(cents(open, held) > 1000 && cents(open, held) < 1250, qPrintable(QStringLiteral("%1 → %2").arg(held).arg(open)));
    }

    void aFingerOnTheMouthBendsTheBassDown() {
        // A finger slides onto the mouth while the bass rings: the pitch
        // falls, the liquid udu bend.
        const float mx = padX(0.0f), my = padY(udu::kMouthY);
        const auto take = testkit::play(toUdu() + fmt("tap 0.5 %.4f %.4f p=0.8 len=0.03\n"
                                            "down 0.7 50 %.4f %.4f p=0.6\nglide 0.72 0.95 50 %.4f %.4f\nup 1.6 50\nend 1.6\n",
                                            padX(udu::kHoleX), padY(udu::kHoleY), mx + 0.2f, my, mx, my));
        std::vector<float> mono(take.left.size());
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (take.left[i] + take.right[i]);
        const auto track = pitchTrack(mono);
        const double before = pitchAt(track, 0.66, 0.03), after = pitchAt(track, 1.15, 0.08);
        QVERIFY2(cents(before, after) > 200, qPrintable(QStringLiteral("%1 → %2").arg(before).arg(after)));
    }

    void aLoopOfTheUduKeepsTheUdu() {
        // Hits and covers carry the kit: an udu loop sounds as an udu after
        // the page has gone back to house.
        const auto take = testkit::play(toUdu() + fmt("tap 0.6 %.4f %.4f p=0.7\ntap 0.9 %.4f %.4f p=0.7\n"
                                            "key 1.2 Space\nkey 1.5 Right\nend 6\n",
                                            padX(udu::kHoleX), padY(udu::kHoleY), padX(0.4f), padY(-0.5f)));
        QVERIFY(take.finite());
        QVERIFY(take.rms(3, 6) > 1e-4);
    }

    // --- the cost ---------------------------------------------------------------

    void theUduCostsLittle() {
        // One pot rings whatever strikes it: ten hands at once cost about what
        // one does, well under a percent of a core.
        Ctx ctx;
        auto timed = [&](int fingers) {
            drums::Engine e;
            e.init(kRate);
            float l[kBlock], r[kBlock];
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < int(4 * kRate) / kBlock; ++b) {
                if (b % 150 == 0)
                    for (int f = 0; f < fingers; ++f) {
                        drums::Strike s;
                        s.kit = drums::Udu;
                        s.pad = drums::uduPad(f % udu::kPlaces);
                        s.vel = 0.9f;
                        s.sx = drums::uduSpotX(0.3f - 0.1f * float(f));
                        s.sy = drums::uduSpotY(-0.4f + 0.05f * float(f));
                        e.strike(s, ctx.h);
                    }
                std::fill(l, l + kBlock, 0.0f);
                std::fill(r, r + kBlock, 0.0f);
                e.render(l, r, kBlock, ctx);
                for (int i = 0; i < kBlock; ++i)
                    if (!std::isfinite(l[i]))
                        return -1.0;
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        };
        const double one = timed(1), ten = timed(10);
        QVERIFY(one >= 0 && ten >= 0);
        QVERIFY2(ten < 0.02, qPrintable(QString::number(ten)));
        QVERIFY2(ten < 3 * one + 0.002, qPrintable(QStringLiteral("%1 %2").arg(one).arg(ten)));
    }

    void tenFingersOnTheUduStayInsideBudget() {
        // Ten fingers drumming on the udu, some resting and sliding over its
        // openings, through the whole studio, as the other pages' budgets
        // are measured.
        std::string s = toUdu();
        for (int f = 0; f < 8; ++f) {
            const float x = 0.3f + 0.05f * float(f), y = 0.15f + 0.08f * float(f % 6);
            for (int k = 0; k < 12; ++k)
                s += fmt("tap %.3f %.3f %.3f p=0.8 len=0.05\n", 0.3 + 0.25 * k + 0.02 * f, x, y);
        }
        s += fmt("down 3.4 100 %.3f %.3f p=0.7\nglide 3.5 5.5 100 %.3f %.3f\n", padX(udu::kHoleX), padY(udu::kHoleY),
                 padX(0.3f), padY(udu::kHoleY));
        s += fmt("down 3.6 101 %.3f %.3f p=0.7\nglide 3.7 5.7 101 %.3f %.3f\n", padX(0.4f), padY(udu::kMouthY), padX(0.0f),
                 padY(udu::kMouthY));
        s += "end 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(UduTests)
#include "udu_tests.moc"
