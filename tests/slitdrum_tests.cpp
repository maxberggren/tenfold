// The slit drum kit: a wooden tongue drum (slitdrum.h), checked as the
// acoustics papers measure real ones (docs/slit-drum.md): its tuning, its
// tongues as cantilevers of anisotropic wood, where and with what they are
// struck, the box's air and what it does to the tongues, sympathy between
// them, its decay, and what it costs.

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
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

struct Ctx : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    // G major: the box as cut, G3 A3 B3 C4 D4 E4 F#4 G4.
    Ctx() { h.tuning.set(7, 0, 1, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 1.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// One strike on a fresh drum through the drums engine, its output (both
// ears summed) for `seconds`. xi along the tongue from its root (0) to its
// tip (1), eta across it (±1 its edges); tone the mallet.
std::vector<float> strike(int tongue, float vel, float xi, float eta, double seconds, float tone = 0,
                          drums::Engine *keep = nullptr) {
    Ctx ctx;
    drums::Engine local;
    drums::Engine &e = keep ? *keep : local;
    if (!keep)
        e.init(kRate);
    drums::Strike s;
    s.kit = drums::SlitDrum;
    s.pad = drums::slitPad(tongue);
    s.vel = vel;
    s.sx = xi;
    s.sy = 0.5f + 0.5f * eta;
    s.tone = tone;
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

// The same straight on a body (for what the engine doesn't reach).
std::vector<float> ring(slit::Body &b, double seconds) {
    std::vector<float> out;
    float l[kBlock], r[kBlock];
    for (int i = 0; i < int(seconds * kRate) / kBlock; ++i) {
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        b.render(l, r, kBlock);
        for (int j = 0; j < kBlock; ++j)
            out.push_back(0.5f * (l[j] + r[j]));
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

// A partial's level: the strongest component within ±cents of hz.
double levelNear(const std::vector<float> &x, double from, double len, double hz, double cents = 30) {
    double top = -1e9;
    const double step = std::max(0.2, 0.5 / len);
    for (double f = hz * std::exp2(-cents / 1200); f <= hz * std::exp2(cents / 1200); f += step)
        top = std::max(top, levelAt(x, from, len, f));
    return top;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

// The pitch heard near hz: the power-weighted mean frequency within ±cents.
double pitchNear(const std::vector<float> &x, double from, double len, double hz, double cents = 40) {
    double sum = 0, weight = 0;
    const double lo = hz * std::exp2(-cents / 1200), hi = hz * std::exp2(cents / 1200);
    for (double f = lo; f <= hi; f += 0.05) {
        const double p = std::pow(10.0, levelAt(x, from, len, f) / 10);
        sum += p * f;
        weight += p;
    }
    return weight > 0 ? sum / weight : 0;
}

// The time constant of a partial's decay between two times, from its level.
double tauOf(const std::vector<float> &x, double hz, double t0, double t1, double win = 0.1) {
    const double a = levelAt(x, t0, win, hz), b = levelAt(x, t1, win, hz);
    return (t1 - t0) * 20 / std::log(10.0) / (a - b);
}

// How bright [from, from+len) is: the share of its power above 800 Hz, in
// dB (50 Hz to 8 kHz, on a log grid).
double brightness(const std::vector<float> &x, double from, double len) {
    double high = 0, all = 0;
    for (double f = 50; f < 8000; f *= 1.03) {
        const double p = std::pow(10.0, levelAt(x, from, len, f) / 10);
        all += p;
        if (f > 800)
            high += p;
    }
    return 10 * std::log10(high / all);
}

double peakOf(const std::vector<float> &x) {
    float top = 0;
    for (float v : x)
        top = std::max(top, std::fabs(v));
    return top;
}

}

class SlitDrumTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenMalletsOnTheSlitDrumStayInsideBudget")
private slots:
    // --- tuning ---------------------------------------------------------------

    void itIsTunedToTheKey() {
        // In G major, the box as cut: G3 A3 B3 C4 D4 E4 F#4 G4.
        Ctx ctx;
        float hz[slit::kTongues];
        drums::slitTuning(ctx.h, hz);
        const int midi[slit::kTongues] = {55, 57, 59, 60, 62, 64, 66, 67};
        for (int t = 0; t < slit::kTongues; ++t)
            QVERIFY2(std::fabs(cents(hz[t], midiToHz(midi[t]))) < 0.5, qPrintable(QString::number(t)));
        // In D minor the root moves to D, within a few semitones of G3, and
        // the tongues go up its scale.
        ctx.h.tuning.set(2, 0, 4, false);
        drums::slitTuning(ctx.h, hz);
        QVERIFY(std::fabs(cents(hz[0], midiToHz(50))) < 0.5 || std::fabs(cents(hz[0], midiToHz(62))) < 0.5);
        for (int t = 1; t < slit::kTongues; ++t)
            QVERIFY(hz[t] > hz[t - 1]);
        QVERIFY(std::fabs(cents(hz[7], 2 * hz[0])) < 0.5);
    }

    void everyTongueSoundsInTune() {
        // Struck, each tongue's first mode as heard (the box's air, the
        // slits, the panels and the other tongues all pulling on it) is
        // within 5 cents of its note: the model tunes each tongue on the
        // assembled box, as a maker does. In another key too.
        for (int key : {7, 2}) {
            Ctx ctx;
            ctx.h.tuning.set(key, 0, key == 2 ? 4 : 1, false);
            float hz[slit::kTongues];
            drums::slitTuning(ctx.h, hz);
            for (int t = 0; t < slit::kTongues; ++t) {
                drums::Engine e;
                e.init(kRate);
                drums::Strike s;
                s.kit = drums::SlitDrum;
                s.pad = drums::slitPad(t);
                s.vel = 0.4f;
                s.sx = 0.9f;
                e.strike(s, ctx.h);
                std::vector<float> x;
                float l[kBlock], r[kBlock];
                for (int b = 0; b < int(0.8 * kRate) / kBlock; ++b) {
                    std::fill(l, l + kBlock, 0.0f);
                    std::fill(r, r + kBlock, 0.0f);
                    e.render(l, r, kBlock, ctx);
                    for (int i = 0; i < kBlock; ++i)
                        x.push_back(0.5f * (l[i] + r[i]));
                }
                const double got = pitchNear(x, 0.05, 0.6, hz[t], 30);
                QVERIFY2(std::fabs(cents(got, hz[t])) < 5.0,
                         qPrintable(QStringLiteral("key %1 tongue %2: %3 Hz for %4").arg(key).arg(t).arg(got).arg(hz[t])));
            }
        }
    }

    // --- the tongues ----------------------------------------------------------

    void aTongueIsACantilever() {
        // The finite elements against the textbook: a uniform strip clamped
        // at one end (here an almost rigid land a tenth of a millimetre long) bends at
        // Euler–Bernoulli's 1.8751²/2π·√(EI/ρA)/L², and twists at
        // (1/4L)·√(GJ/ρI_p), within a couple of percent; its bending modes
        // above at the clamped–free 6.267, 17.55, 34.39, less Timoshenko's
        // shear correction, which wood (E/G ≈ 15) needs and which grows with
        // the mode.
        const float L = 0.15f;
        const slit::fe::Modes md = slit::fe::solve(L, 0.0001f);
        const double E = slit::Physics::kEL, G = slit::Physics::kG, rho = slit::Physics::kWood;
        const double h = slit::Physics::kTop, b = slit::Physics::kWidth;
        // (The same elements without shear; with it, Timoshenko's, the first
        // mode a couple of percent lower.)
        const double eb = 1.8751 * 1.8751 / (2 * M_PI) * std::sqrt(E * h * h / (12 * rho)) / (L * L);
        QVERIFY2(std::fabs(md.eulerHz[0] / eb - 1) < 0.01, qPrintable(QStringLiteral("%1 %2").arg(md.eulerHz[0]).arg(eb)));
        QVERIFY2(md.hz[0] < md.eulerHz[0] && md.hz[0] > 0.96 * md.eulerHz[0], qPrintable(QStringLiteral("%1 %2").arg(md.hz[0]).arg(md.eulerHz[0])));
        const double tw = 1 / (4 * L) * std::sqrt(G * slit::fe::torsionConstant(b, h) / (rho * b * h * (b * b + h * h) / 12));
        QVERIFY2(std::fabs(md.hz[4] / tw - 1) < 0.03, qPrintable(QStringLiteral("%1 %2").arg(md.hz[4]).arg(tw)));
        const double euler[4] = {1, 6.267, 17.55, 34.39};
        double last = 1.0;
        for (int k = 1; k < 4; ++k) {
            const double got = md.hz[k] / md.hz[0], short_ = got / euler[k];
            QVERIFY2(short_ < last && short_ > 0.55, qPrintable(QStringLiteral("mode %1: %2 (%3 of Euler)").arg(k).arg(got).arg(short_)));
            last = short_;
        }
        // Wood's low shear modulus puts the first twist between the first
        // two bending modes (in steel, G/E ≈ 0.38, it would be above the
        // second for this strip).
        QVERIFY(md.hz[4] > 1.5 * md.hz[0] && md.hz[4] < md.hz[1]);
        const double steel = tw * std::sqrt(0.385 * E / G);
        QVERIFY(steel > md.hz[1]);
    }

    void theTonguesAsBuiltAreInharmonic() {
        // On the drum, roots that give a little and all: the second bending
        // mode 5–7× the first (kalimba tines measure 5.6–5.9), the third
        // 12–18×, the first twist where a strip's torsion puts it against
        // its bending, 3.10·(L/w)·√(G/E), within a quarter (the land moves
        // both).
        // The one heard most, the second bending mode, nowhere near a
        // harmonic: a slit drum's woody, unpitched overtones. (The others
        // fall where the wood puts them, an integer now and then by chance.)
        slit::Body b;
        b.init(kRate);
        for (int t = 0; t < slit::kTongues; ++t) {
            const double f1 = b.modeHz(t, 0);
            const double r2 = b.modeHz(t, 1) / f1, r3 = b.modeHz(t, 2) / f1, rt = b.modeHz(t, 4) / f1;
            QVERIFY2(r2 > 5.0 && r2 < 7.0, qPrintable(QStringLiteral("tongue %1: %2").arg(t).arg(r2)));
            QVERIFY2(r3 > 12.0 && r3 < 18.0, qPrintable(QStringLiteral("tongue %1: %2").arg(t).arg(r3)));
            const double strip = 3.10 * slit::tongue(t).length / slit::Physics::kWidth *
                                 std::sqrt(slit::Physics::kG / slit::Physics::kEL);
            QVERIFY2(std::fabs(rt / strip - 1) < 0.25, qPrintable(QStringLiteral("tongue %1: %2 for %3").arg(t).arg(rt).arg(strip)));
            for (int k : {1}) {
                const double r = b.modeHz(t, k) / f1;
                QVERIFY2(std::fabs(r - std::round(r)) > 0.06,
                         qPrintable(QStringLiteral("tongue %1 mode %2 at %3").arg(t).arg(k).arg(r)));
            }
            // As cut, each tongue lands on its note without a maker's trim.
            QVERIFY2(std::fabs(b.trim(t) - 1) < 0.02, qPrintable(QString::number(b.trim(t))));
        }
        // And the sound has them: the second bending mode is heard where the
        // body says, not at a harmonic.
        const auto x = strike(2, 0.9f, 0.6f, 0.0f, 0.3, 1.0f);
        const double f1 = b.modeHz(2, 0), f2 = b.modeHz(2, 1);
        QVERIFY2(levelNear(x, 0.0, 0.05, f2, 20) > levelNear(x, 0.0, 0.05, 5 * f1, 15) + 10,
                 qPrintable(QStringLiteral("%1 %2").arg(levelNear(x, 0.0, 0.05, f2, 20)).arg(levelNear(x, 0.0, 0.05, 5 * f1, 15))));
    }

    void woodRingsItsFirstModeLongestAndItsTwistsShortest() {
        // Wood's loss factor: the first bending mode's T60 is 2.2/(f·η), so
        // a low tongue rings longer than a high one; each mode above dies
        // faster (higher, and more of it shear); a twist, all shear, faster
        // than the bending mode below it.
        slit::Body b;
        b.init(kRate);
        for (int t = 0; t < slit::kTongues; ++t) {
            const double eta = 1 / (M_PI * b.modeHz(t, 0) * b.modeTau(t, 0));
            QVERIFY2(eta > 0.006 && eta < 0.02, qPrintable(QStringLiteral("tongue %1: η %2").arg(t).arg(eta)));
            for (int k = 1; k < 4; ++k)
                QVERIFY(b.modeTau(t, k) < b.modeTau(t, k - 1));
            const double etaTwist = 1 / (M_PI * b.modeHz(t, 4) * b.modeTau(t, 4));
            QVERIFY(etaTwist > 1.5 * eta);
            if (t)
                QVERIFY(b.modeTau(t, 0) < b.modeTau(t - 1, 0));
        }
    }

    void theLowestTongueRingsUnderASecond() {
        // As heard: the lowest tongue's T60 0.5–1.5 s, the highest shorter;
        // and it goes to sleep once rung out.
        Ctx ctx;
        float hz[slit::kTongues];
        drums::slitTuning(ctx.h, hz);
        const auto low = strike(0, 0.5f, 0.9f, 0, 1.2), high = strike(7, 0.5f, 0.9f, 0, 1.2);
        const double tl = 6.91 * tauOf(low, hz[0], 0.1, 0.5), th = 6.91 * tauOf(high, hz[7], 0.1, 0.4);
        QVERIFY2(tl > 0.5 && tl < 1.5, qPrintable(QString::number(tl)));
        QVERIFY2(th < tl && th > 0.2, qPrintable(QStringLiteral("%1 %2").arg(th).arg(tl)));
        drums::Engine e;
        e.init(kRate);
        strike(0, 1.0f, 0.9f, 0, 8.0, 0, &e);
        QCOMPARE(e.voices(), 0);
    }

    // --- the mallet -----------------------------------------------------------

    void whereTheMalletLandsChoosesThePartials() {
        // Along the tongue: at the tip every bending mode moves; three
        // quarters of the way out is the second mode's node, its purest
        // note; near the root the first mode hardly moves and the stiff
        // upper ones speak. Across it: off the centre line it twists.
        slit::Body b;
        b.init(kRate);
        const int t = 1;
        const double f1 = b.modeHz(t, 0), f2 = b.modeHz(t, 1), ft = b.modeHz(t, 4);
        double twistSwing = 0;
        auto at = [&](float xi, float eta, double &one, double &two, double &twist) {
            b.silence();
            b.strike(t, 0.7f, xi, eta, 1.0f, 0);
            const auto x = ring(b, 0.15);
            twistSwing = b.swing(t, 4);
            one = levelNear(x, 0.0, 0.1, f1, 20);
            two = levelNear(x, 0.0, 0.1, f2, 25);
            twist = levelNear(x, 0.0, 0.1, ft, 25);
        };
        double tip1, tip2, tipT, node1, node2, nodeT, root1, root2, rootT, edge1, edge2, edgeT;
        at(1.0f, 0.0f, tip1, tip2, tipT);
        const double tipSwing = twistSwing;
        at(0.78f, 0.0f, node1, node2, nodeT);
        at(0.25f, 0.0f, root1, root2, rootT);
        at(1.0f, 0.9f, edge1, edge2, edgeT);
        const double edgeSwing = twistSwing;
        const QString all = QStringLiteral("tip %1/%2/%3 node %4/%5/%6 root %7/%8/%9 edge %10/%11/%12")
                                .arg(tip1).arg(tip2).arg(tipT).arg(node1).arg(node2).arg(nodeT)
                                .arg(root1).arg(root2).arg(rootT).arg(edge1).arg(edge2).arg(edgeT);
        // The node: the second mode down 15 dB or more against the tip.
        QVERIFY2(node2 - node1 < tip2 - tip1 - 15, qPrintable(all));
        // The root: quieter, the second mode up against the first.
        QVERIFY2(root1 < tip1 - 8 && root2 - root1 > tip2 - tip1 + 6, qPrintable(all));
        // The edge: the tongue twists, where on the centre line it hardly
        // does at all (the strips and the air leak a little twist in), and
        // the twist is heard.
        QVERIFY2(edgeSwing > 30 * tipSwing, qPrintable(all + QStringLiteral(" swing %1 %2").arg(edgeSwing).arg(tipSwing)));
        QVERIFY2(edgeT > tipT + 8, qPrintable(all));
    }

    void harderIsLouderBrighterAndShorter() {
        // A faster mallet presses its Hertzian ball deeper and leaves
        // sooner: louder, a brighter attack, a shorter contact.
        slit::Body b;
        b.init(kRate);
        auto play = [&](float vel, float tone, double &peak, double &bright, double &contact) {
            b.silence();
            b.strike(3, vel, 0.9f, 0.1f, tone, 0);
            const auto x = ring(b, 0.3);
            peak = peakOf(x);
            bright = brightness(x, 0.0, 0.03);
            contact = b.lastContact();
        };
        double p1, b1, c1, p2, b2, c2;
        play(0.25f, 0.0f, p1, b1, c1);
        play(1.0f, 0.0f, p2, b2, c2);
        QVERIFY2(p2 > 2.5 * p1 && b2 > b1 + 3 && c2 < c1,
                 qPrintable(QStringLiteral("%1/%2/%3 %4/%5/%6").arg(p1).arg(b1).arg(c1).arg(p2).arg(b2).arg(c2)));
        // A rubber ball touches for a millisecond or three, a hard mallet a
        // fraction of one, a fingertip's pulp longest; the harder the
        // mallet, the brighter.
        double pf, bf, cf, pr, br, cr, ph, bh, ch;
        play(0.7f, -1.0f, pf, bf, cf);
        play(0.7f, 0.0f, pr, br, cr);
        play(0.7f, 1.0f, ph, bh, ch);
        const QString all = QStringLiteral("finger %1 ms %2 dB, rubber %3 ms %4 dB, hard %5 ms %6 dB")
                                .arg(1000 * cf).arg(bf).arg(1000 * cr).arg(br).arg(1000 * ch).arg(bh);
        // (A superball against a rigid target 1.5–3.4 ms; a tongue that gives
        // under it keeps it longer.)
        QVERIFY2(cr > 0.0012 && cr < 0.0045, qPrintable(all));
        QVERIFY2(ch > 0.0002 && ch < 0.0007, qPrintable(all));
        QVERIFY2(cf > 0.001 && cf < 0.006, qPrintable(all));
        QVERIFY2(bh > br + 6 && br > bf, qPrintable(all));
    }

    void aRollNeverPumpsEnergyIn() {
        // A mallet rolling fast on one tongue (30 ms) adds up as hits would,
        // never more: each meets the wood where it is.
        slit::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock], first = 0, top = 0;
        bool finite = true;
        for (int i = 0; i < int(1.5f * kRate) / kBlock; ++i) {
            if (i % 11 == 0)
                b.strike(0, 1.0f, 0.9f, 0.0f, 0, 0);
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            b.render(l, r, kBlock);
            for (int j = 0; j < kBlock; ++j) {
                finite &= std::isfinite(l[j]);
                top = std::max(top, std::fabs(l[j]));
                if (i < 11)
                    first = std::max(first, std::fabs(l[j]));
            }
        }
        QVERIFY(finite);
        // (Hits 30 ms apart on a note ringing ~0.15 s add up to at most
        // 1/(1 − e^(−0.03/0.15)) of one, +15 dB, in phase.)
        QVERIFY2(20 * std::log10(top / first) < 15, qPrintable(QString::number(20 * std::log10(top / first))));
    }

    // --- the box --------------------------------------------------------------

    void theBoxIsAHelmholtzResonator() {
        // Its volume on the plugs of air in its slits: Helmholtz's
        // c/2π·√(S/VL), in the tongues' range, broad (a long slit radiates
        // well); a knock on the box sounds it, and without the air it's gone.
        slit::Body b;
        b.init(kRate);
        const double S = b.portArea(), V = slit::volume();
        const double neck = slit::Physics::kTop + 2 * slit::Physics::kKerf / M_PI *
                                                      (1.5 - 0.5772 - std::log(2 * M_PI * 220 / 343.0 * slit::Physics::kKerf / 4));
        const double f = 343 / (2 * M_PI) * std::sqrt(S / (V * neck));
        QVERIFY2(std::fabs(b.helmholtzHz() / f - 1) < 0.01, qPrintable(QStringLiteral("%1 %2").arg(b.helmholtzHz()).arg(f)));
        QVERIFY2(b.helmholtzHz() > 150 && b.helmholtzHz() < 400, qPrintable(QString::number(b.helmholtzHz())));
        QVERIFY2(b.helmholtzQ() > 1.5 && b.helmholtzQ() < 15, qPrintable(QString::number(b.helmholtzQ())));
        auto knock = [&](bool air) {
            b.setAir(air);
            b.silence();
            b.strike(slit::kFrame, 0.8f, 0.0f, -0.9f, 0.0f, 0);
            return ring(b, 0.3);
        };
        const auto with = knock(true), without = knock(false);
        b.setAir(true);
        const double fH = b.helmholtzHz();
        // Within an octave either side: the panels' give pulls it down.
        double best = 0, top = -1e9;
        for (double hz = 0.5 * fH; hz < 1.4 * fH; hz *= 1.01) {
            const double l = levelAt(with, 0.0, 0.1, hz) - levelAt(without, 0.0, 0.1, hz);
            if (l > top)
                top = l, best = hz;
        }
        QVERIFY2(top > 10, qPrintable(QStringLiteral("%1 dB at %2 Hz (fH %3)").arg(top).arg(best).arg(fH)));
    }

    void theBoxsAirCouplesToEveryTongue() {
        // Every tongue's first mode pushes on the cavity: coupled to the
        // Helmholtz air by a few percent of its frequency. A struck tongue
        // breathes through every slit, so the box makes it louder at the
        // box's resonance, and it costs the tongue some of its ring.
        slit::Body b;
        b.init(kRate);
        for (int t = 0; t < slit::kTongues; ++t)
            QVERIFY2(b.airCoupling(t) > 0.01 && b.airCoupling(t) < 0.2, qPrintable(QString::number(b.airCoupling(t))));
        auto play = [&](bool air) {
            b.setAir(air);
            b.silence();
            b.strike(2, 0.6f, 0.9f, 0.0f, 0.0f, 0);
            return ring(b, 0.8);
        };
        const auto with = play(true), without = play(false);
        b.setAir(true);
        const double f1 = b.modeHz(2, 0);
        // Louder with the box.
        QVERIFY2(levelNear(with, 0.05, 0.2, f1) > levelNear(without, 0.05, 0.2, f1) + 3,
                 qPrintable(QStringLiteral("%1 %2").arg(levelNear(with, 0.05, 0.2, f1)).arg(levelNear(without, 0.05, 0.2, f1))));
        // And it rings a little shorter for what it radiates.
        QVERIFY2(tauOf(with, f1, 0.1, 0.5) < tauOf(without, f1, 0.1, 0.5),
                 qPrintable(QStringLiteral("%1 %2").arg(tauOf(with, f1, 0.1, 0.5)).arg(tauOf(without, f1, 0.1, 0.5))));
    }

    void aStruckTongueStirsTheOthersThroughTheAirAndTheWood() {
        // Sympathy: through the box's air and through the top at the roots
        // (the land behind each root is one plate with the strips of top
        // beside it), a struck tongue sets the others swinging; take the air
        // away and they move less, but still move.
        auto others = [&](bool air) {
            slit::Body b;
            b.init(kRate);
            b.setAir(air);
            b.strike(0, 1.0f, 0.9f, 0.0f, 0.0f, 0);
            float l[kBlock], r[kBlock];
            double sum = 0;
            for (int i = 0; i < int(0.2 * kRate) / kBlock; ++i) {
                b.render(l, r, kBlock);
                for (int t = 1; t < slit::kTongues; ++t)
                    sum += b.swing(t, 0);
            }
            return sum;
        };
        const double with = others(true), without = others(false);
        QVERIFY2(with > 3 * without && without > 0, qPrintable(QStringLiteral("%1 %2").arg(with).arg(without)));
        // Across the grain, a land and the strip beside it pull on each other
        // by a percent or two of their stiffness (a plate spring of
        // 12·D_y/d³ along the land).
        slit::Body b;
        b.init(kRate);
        for (int t = 0; t < slit::kTongues; ++t)
            QVERIFY2(b.rootCoupling(t) > 0.005 && b.rootCoupling(t) < 0.05, qPrintable(QString::number(b.rootCoupling(t))));
    }

    void theTopBetweenTheCutsRingsAsStripsOfWood() {
        // The H's leave the top between them as strips running wall to wall,
        // each a clamped–clamped beam (4.730²/2π·√(EI/ρA)/L², its modes at
        // 2.76× and 5.40×, a little under for shear), every strip its own
        // piece of wood. A hard knock on one sounds its first mode loudest,
        // and the box's air.
        slit::Body b;
        b.init(kRate);
        const double L = slit::innerLength(), h = slit::Physics::kTop;
        const double f1 = 4.73004 * 4.73004 / (2 * M_PI) * std::sqrt(slit::Physics::kEL * h * h / (12 * slit::Physics::kWood)) / (L * L);
        for (int s = 0; s < slit::kStrips; ++s) {
            const double got = b.panelHz(s * slit::kStripModes) / slit::panel(s * slit::kStripModes).scatter;
            QVERIFY2(got < f1 && got > 0.97 * f1, qPrintable(QStringLiteral("%1 %2").arg(got).arg(f1)));
            const double r2 = b.panelHz(s * slit::kStripModes + 1) / b.panelHz(s * slit::kStripModes) *
                              slit::panel(s * slit::kStripModes).scatter / slit::panel(s * slit::kStripModes + 1).scatter;
            QVERIFY2(r2 < 2.757 && r2 > 2.6, qPrintable(QString::number(r2)));
        }
        b.strike(slit::kFrame, 0.9f, 0.35f, slit::stripY(1) / (0.5f * slit::Physics::innerDepth()), 1.0f, 0);
        const auto x = ring(b, 0.3);
        double top = -1e9;
        std::vector<std::pair<double, double>> peaks;
        double prev2 = -1e9, prev = -1e9, pf = 0;
        for (double f = 150; f < 2500; f *= 1.004) {
            const double l = levelAt(x, 0, 0.08, f);
            if (prev > prev2 && prev > l)
                peaks.push_back({pf, prev});
            prev2 = prev, prev = l, pf = f;
            top = std::max(top, l);
        }
        QString all;
        double loudest = 0, best = -1e9;
        for (auto &p : peaks) {
            if (p.second > top - 25)
                all += QStringLiteral(" %1 Hz %2 dB").arg(p.first, 0, 'f', 0).arg(p.second - top, 0, 'f', 1);
            if (p.second > best)
                best = p.second, loudest = p.first;
        }
        // (Its first mode pumps air, the others mostly rock it, and the glued
        // ends take more of theirs: the first leads.)
        const double first = b.panelHz(slit::kStripModes);
        QVERIFY2(std::fabs(loudest / first - 1) < 0.03, qPrintable(QStringLiteral("loudest %1 Hz, strip %2 Hz:").arg(loudest).arg(first) + all));
        double air = -1e9;
        for (double f = 170; f < 280; f *= 1.01)
            air = std::max(air, levelAt(x, 0, 0.08, f));
        QVERIFY2(air > top - 35, qPrintable(QStringLiteral("air %1 dB:").arg(air - top) + all));
    }

    void aTongueOnAStripsModeBeatsButStillRings() {
        // The key retunes the tongues, not the box: whichever lands on a
        // strip's first mode (350–370 Hz) trades energy with it, a wolf, as
        // on any instrument with a coupled resonance. In every key, major
        // and minor, every tongue still rings (T60 over 0.2 s) and none
        // loses more than 10 dB between its first and second 25 ms.
        for (int root = 0; root < 12; ++root)
            for (int bright : {1, 4}) {
                Ctx ctx;
                ctx.h.tuning.set(root, 0, bright, false);
                float hz[slit::kTongues];
                drums::slitTuning(ctx.h, hz);
                slit::Body b;
                b.init(kRate);
                b.tune(hz);
                for (int t = 0; t < slit::kTongues; ++t) {
                    b.silence();
                    b.strike(t, 0.6f, 0.85f, 0.0f, 0.0f, 0);
                    const auto x = ring(b, 0.45);
                    const double drop = levelAt(x, 0.0, 0.025, hz[t]) - levelAt(x, 0.025, 0.025, hz[t]);
                    const double t60 = 6.91 * tauOf(x, hz[t], 0.05, 0.3);
                    QVERIFY2(drop < 10 && t60 > 0.2, qPrintable(QStringLiteral("key %1/%2 tongue %3 (%4 Hz): drop %5 dB, T60 %6 s")
                                                                  .arg(root).arg(bright).arg(t).arg(hz[t]).arg(drop).arg(t60)));
                }
            }
    }

    // --- the page -------------------------------------------------------------

    void thePadIsTheSlitDrum() {
        // The box seen from above: the lowest tongue nearest the player on
        // the left, the highest on the right, the box round the edges.
        auto struck = [](float x, float y) {
            const auto take = testkit::play(QStringLiteral("page 0 1\nkey 0.1 Left\ntap 0.5 %1 %2 p=0.7\nend 0.56\n")
                                                .arg(x)
                                                .arg(y)
                                                .toStdString());
            int best = -1;
            float top = 0.2f;
            for (int p = 0; p < drums::PadCount; ++p)
                if (take.view.parts[0].data[p] > top)
                    top = take.view.parts[0].data[p], best = p;
            return best < 0 ? -1 : drums::slitTongue(best);
        };
        auto padX = [](float X) { return 0.5f + X / slit::boxLength() * 0.95f; };
        auto padY = [](float Y) { return 0.5325f + Y / slit::Physics::kDepth * 0.885f; };
        for (int t : {0, 3, 4, 7}) {
            const slit::Tongue tg = slit::tongue(t);
            const float X = tg.rootX + float(tg.dir) * 0.7f * tg.length;
            QCOMPARE(struck(padX(X), padY(tg.y)), t);
        }
        QCOMPARE(struck(0.5f, padY(-0.48f * slit::Physics::kDepth)), int(slit::kFrame));
        QVERIFY(slit::tongue(0).rootX < 0 && slit::tongue(0).y < 0);
        QVERIFY(slit::tongue(7).rootX > 0 && slit::tongue(7).y < 0);
        const auto take = testkit::play(QStringLiteral("page 0 1\nkey 0.1 Left\ntap 0.5 %1 %2 p=0.7\nend 2\n")
                                            .arg(padX(slit::tongue(2).rootX + 0.8f * slit::tongue(2).length))
                                            .arg(padY(slit::tongue(2).y))
                                            .toStdString());
        QVERIFY(take.finite());
        QVERIFY(take.rms(0.5, 1.0) > 1e-4);
        QVERIFY(take.peak() < 1.0);
    }

    void aLoopOfTheSlitDrumKeepsTheSlitDrum() {
        // Hits carry the kit: a slit drum loop sounds as one after the page
        // has gone back to house.
        const auto take = testkit::play("page 0 1\nkey 0.1 Left\ntap 0.6 0.2 0.25 p=0.7\ntap 0.9 0.8 0.25 p=0.7\n"
                                        "key 1.2 Space\nkey 1.5 Right\nend 6\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(3, 6) > 1e-5);
    }

    // --- the cost -------------------------------------------------------------

    void theSlitDrumCostsTheSameForTenMallets() {
        // One box rings whatever strikes it: ten strikes at once cost about
        // what one does, and a slit drum playing costs well under a percent
        // of one core per second of sound.
        Ctx ctx;
        float top = 0;
        auto timed = [&](int mallets) {
            drums::Engine e;
            e.init(kRate);
            float l[kBlock], r[kBlock];
            top = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < int(4 * kRate) / kBlock; ++b) {
                if (b % 300 == 0)
                    for (int f = 0; f < mallets; ++f) {
                        drums::Strike s;
                        s.kit = drums::SlitDrum;
                        s.pad = drums::slitPad(f % slit::kTongues);
                        s.vel = 0.9f;
                        s.sx = 0.85f;
                        s.sy = 0.55f;
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
        QVERIFY2(top < 20 * oneTop, qPrintable(QStringLiteral("%1 %2").arg(oneTop).arg(top)));
        QVERIFY2(ten < 0.03, qPrintable(QString::number(ten)));
        QVERIFY2(ten < 3 * one + 0.002, qPrintable(QStringLiteral("%1 %2").arg(one).arg(ten)));
    }

    void tenMalletsOnTheSlitDrumStayInsideBudget() {
        // Ten fingers drumming on the slit drum through the whole studio.
        std::string s = "page 0 1\nkey 0.1 Left\n";
        for (int f = 0; f < 10; ++f) {
            const float x = 0.12f + 0.08f * float(f), y = 0.2f + 0.06f * float(f % 6);
            for (int k = 0; k < 12; ++k)
                s += fmt("tap %.3f %.3f %.3f p=0.8 len=0.05\n", 0.3 + 0.25 * k + 0.02 * f, x, y);
        }
        for (int f = 0; f < 10; ++f)
            s += fmt("down %.3f %d %.3f %.3f p=0.7\n", 3.5 + 0.01 * f, 100 + f, 0.1 + 0.08 * f, 0.3 + 0.04 * f);
        s += "end 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(SlitDrumTests)
#include "slitdrum_tests.moc"
