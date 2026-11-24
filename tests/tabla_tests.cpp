// The tabla pair: the dayan and the bayan (tabla.h), checked as the
// acoustics papers measure real ones (docs/tabla.md): the dayan's
// harmonic partials on Raman's five, each stroke's spectrum and decay, the
// resting fingers that make them, the bayan's ge and the heel's bend, the
// shells' air, the page that plays them, and what it all costs.

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
    // D minor: the dayan's Sa on D4, the bayan's ge on G2.
    Ctx() { h.tuning.set(2, 0, 4, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 1.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

constexpr double kSa = 293.66, kGe = kSa / 2.0 * 0.6674;

// Where each stroke is played, on its head (units of its radius, x to the
// player's right, y away).
const float kAt[tabla::kStrokes][2] = {{0.925f, 0.1f}, {0.66f, 0.1f}, {0.0f, 0.05f}, {0.12f, 0.38f}, {0.0f, -0.6f}};

// A stroke on a fresh pair through the drums engine, both ears summed.
std::vector<float> stroke(int st, float vel, double seconds, float x = -9, float y = -9, float tone = 0) {
    Ctx ctx;
    drums::Engine e;
    e.init(kRate);
    drums::Strike s;
    s.kit = drums::TablaPair;
    s.pad = drums::tablaPad(st);
    s.vel = vel;
    s.sx = 0.5f + 0.5f * (x > -5 ? x : kAt[st][0]);
    s.sy = 0.5f + 0.5f * (y > -5 ? y : kAt[st][1]);
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

// The pair on its own, for strokes that stay: renders `seconds`, calling
// `each(t)` before every block.
template <typename F>
std::vector<float> play(tabla::Body &b, double seconds, F each) {
    std::vector<float> out;
    float l[kBlock], r[kBlock];
    for (int k = 0; k < int(seconds * kRate) / kBlock; ++k) {
        each(double(k) * kBlock / kRate);
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        b.render(l, r, kBlock);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(0.5f * (l[i] + r[i]));
    }
    return out;
}

tabla::Body fresh() {
    tabla::Body b;
    b.init(kRate);
    b.tune(float(kSa), float(kGe));
    return b;
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

// A partial's level: the strongest component within ±cents of hz (a
// degenerate pair splits a few cents; a resting finger pulls a mode up).
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

// T60 of a partial, from its level 0.1 s apart over a stretch.
double t60(const std::vector<float> &x, double hz, double t0, double t1) {
    const double a = levelNear(x, t0, 0.08, hz, 15), b = levelNear(x, t1, 0.08, hz, 15);
    return 60.0 * (t1 - t0) / std::max(1e-6, a - b);
}

// The spectral centroid over a window (of the magnitude spectrum, 40 Hz
// to 8 kHz).
double centroid(const std::vector<float> &x, double from, double len) {
    double num = 0, den = 0;
    for (double f = 40; f < 8000; f += 2) {
        const double m = std::pow(10.0, levelAt(x, from, len, f) / 20);
        num += f * m;
        den += m;
    }
    return num / den;
}

double rms(const std::vector<float> &x, double from, double to) {
    double s = 0;
    size_t n = 0;
    for (size_t i = size_t(from * kRate); i < size_t(to * kRate) && i < x.size(); ++i, ++n)
        s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(n, 1)));
}

// How long the envelope (5 ms RMS) takes to fall to half its peak.
double halfTime(const std::vector<float> &x) {
    std::vector<double> e;
    for (size_t i = 0; i + 240 < x.size(); i += 48)
        e.push_back(rms(x, double(i) / kRate, double(i + 240) / kRate));
    const size_t pk = size_t(std::max_element(e.begin(), e.end()) - e.begin());
    for (size_t i = pk; i < e.size(); ++i)
        if (e[i] < 0.5 * e[pk])
            return double(i - pk) * 0.001;
    return 1e9;
}

bool finite(const std::vector<float> &x) {
    for (float v : x)
        if (!std::isfinite(v))
            return false;
    return true;
}

// The page's keys from house to the tabla pair: left past the end of the
// kits (wherever later kits put it).
std::string toPair() {
    std::string s = "page 0 1\n";
    for (int i = 0; i < drums::KitCount - drums::TablaPair; ++i)
        s += fmt("key %.3f Left\n", 0.1 + 0.004 * i);
    return s;
}

// The pad's coordinates of a point on a drum (drums.cpp's layout).
std::string padAt(int drum, float hx, float hy) {
    const float cx[2] = {0.775f, 0.28f}, ry[2] = {0.33f, 0.42f};
    return fmt("%.4f %.4f", cx[drum] + ry[drum] * 0.625f * hx, 0.53f + ry[drum] * hy);
}

}

class TablaTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenFingersOnThePairStayInsideBudget")
private slots:
    // --- the heads as built ---------------------------------------------------

    void theHeadsAreBuiltAsMeasured() {
        // A 14 cm dayan under a 21 cm bayan, goatskin of 0.25 kg/m², the
        // dayan's syahi over half its radius and 9–15 g of paste, the
        // bayan's a quarter, off centre by 18 %.
        const auto d = tabla::Physics::dayan(), b = tabla::Physics::bayan();
        QCOMPARE(2 * d.radius, 0.14);
        QCOMPARE(2 * b.radius, 0.21);
        QVERIFY(d.syahi >= 0.45 && d.syahi <= 0.56);
        QVERIFY(b.syahi > 0.24 && b.syahi < 0.3 && std::fabs(b.offset) > 0.17 && std::fabs(b.offset) < 0.19);
        const double md = tabla::syahiMass(d) * 1000;
        QVERIFY2(md >= 9 && md <= 15.5, qPrintable(QString::number(md))); // (15.3: at the top)
        // (the kinar a loose ring, 15 mm, hardly a load)
        QVERIFY(std::fabs((1 - d.kinarFrom) * d.radius - 0.015) < 1e-6 && d.kinar < 0.3 * d.skin);
        // Tensions a goatskin bears: the dayan's tight, the bayan's slack.
        tabla::Body p = fresh();
        QVERIFY2(p.tension(tabla::Dayan) > 3000 && p.tension(tabla::Dayan) < 12000, qPrintable(QString::number(p.tension(0))));
        QVERIFY2(p.tension(tabla::Bayan) > 300 && p.tension(tabla::Bayan) < 2000, qPrintable(QString::number(p.tension(1))));
    }

    void theModesComeFromTheLoadedHead() {
        // Solved, not set: the bare membrane's ratios (2.30, 3.60 …) are
        // pulled onto Raman's harmonics by the syahi alone; without it the
        // same solver gives the uniform membrane's.
        tabla::Design bare = tabla::Physics::dayan();
        bare.load = 0;
        bare.kinar = 0;
        bare.airMass = 0;
        const tabla::Solved s = tabla::solve(bare, 3, 3, 6);
        QVERIFY2(std::fabs(std::sqrt(s.lambda[1] / s.lambda[0]) - 1.593) < 0.005, qPrintable(QString::number(std::sqrt(s.lambda[1] / s.lambda[0]))));
        QVERIFY(std::fabs(std::sqrt(s.lambda[3] / s.lambda[0]) - 2.136) < 0.005);
        QCOMPARE(s.order[0], 0);
        QCOMPARE(s.order[1], 1);
        // Bessel's zeros, which the basis is built on.
        QVERIFY(std::fabs(tabla::besselZero(0, 1) - 2.404826) < 1e-5);
        QVERIFY(std::fabs(tabla::besselZero(1, 1) - 3.831706) < 1e-5);
        QVERIFY(std::fabs(tabla::besselZero(2, 2) - 8.417244) < 1e-5);
    }

    // --- the dayan's harmonics ------------------------------------------------

    void theDayanIsTunedToSa() {
        // Its (1,1) an octave above Sa, as a player tunes it with na; Sa on
        // the key's root near D4.
        Ctx ctx;
        float dayan, bayan;
        drums::tablaTuning(ctx.h, dayan, bayan);
        QVERIFY(std::fabs(dayan - kSa) < 0.1);
        QVERIFY(std::fabs(bayan - kGe) < 0.1);
        const auto na = stroke(tabla::Na, 0.8f, 1.2);
        const double f2 = peakHz(na, 0.3, 0.6, 2 * kSa, 30);
        QVERIFY2(std::fabs(cents(f2, 2 * kSa)) < 6, qPrintable(QString::number(f2)));
    }

    void thePartialsAreRamansHarmonics() {
        // (2,1)+(0,2) on the 3rd harmonic of Sa, (3,1)+(1,2) on the 4th,
        // (4,1)+(2,2)+(0,3) on the 5th, as Tiwari & Gupta measured (3rd
        // −10…0 c, 4th ±16 c, 5th −9…+52 c), and the (0,1) sharp of Sa by
        // 1.10–1.16, deliberately.
        tabla::Body b = fresh();
        auto find = [&](int m, int n) {
            for (int k = 0; k < b.modes(tabla::Dayan); ++k)
                if (b.modeM(tabla::Dayan, k) == m && b.modeN(tabla::Dayan, k) == n)
                    return double(b.heardHz(tabla::Dayan, k)) / kSa;
            return 0.0;
        };
        const double r01 = find(0, 1);
        QVERIFY2(r01 > 1.10 && r01 < 1.16, qPrintable(QString::number(r01)));
        QVERIFY(std::fabs(cents(find(1, 1), 2)) < 6);
        const int groups[7][3] = {{2, 1, 3}, {0, 2, 3}, {3, 1, 4}, {1, 2, 4}, {4, 1, 5}, {2, 2, 5}, {0, 3, 5}};
        const double limit[6] = {0, 0, 0, 20, 30, 55};
        for (const auto &g : groups) {
            const double c = cents(find(g[0], g[1]), g[2]);
            QVERIFY2(std::fabs(c) < limit[g[2]], qPrintable(QStringLiteral("(%1,%2): %3 c").arg(g[0]).arg(g[1]).arg(c)));
        }
        // And heard: tun's spectrum has them where the modes are.
        const auto tun = stroke(tabla::Tun, 1.0f, 1.0);
        QVERIFY(std::fabs(cents(peakHz(tun, 0.02, 0.4, r01 * kSa), r01 * kSa)) < 5);
        QVERIFY(std::fabs(cents(peakHz(tun, 0.02, 0.3, 3 * kSa, 40), 3 * kSa)) < 20);
    }

    void theFarFieldIsEachModesOwn() {
        // Each mode radiates as its shape does (Rayleigh's integral in
        // closed form, tabulated at build): the (0,1) sweeps a volume, so far
        // below its size's wavelength it is a monopole of that volume
        // (∫|F|² → 2π·vol², F → vol at broadside); the (1,1) sweeps none and
        // is a dipole, 25 dB and more under it there, and its pressure at
        // broadside is nil. (A toolchain that files the terms under the
        // wrong angular order turns the dipoles into monopoles.)
        const tabla::Built &b = tabla::built(tabla::Dayan);
        int k01 = -1, k11 = -1;
        for (int k = 0; k < b.n; ++k) {
            if (k01 < 0 && b.s.order[size_t(k)] == 0)
                k01 = k;
            if (k11 < 0 && b.s.order[size_t(k)] == 1)
                k11 = k;
        }
        QVERIFY(k01 >= 0 && k11 >= 0);
        const double vol = b.s.volume[size_t(k01)];
        const double kw = 2 * M_PI * 100.0 / tabla::Physics::kC; // well under the head's size
        const double p01 = b.lookup(b.power, k01, kw), p11 = b.lookup(b.power, k11, kw);
        QVERIFY2(std::fabs(p01 / (2 * M_PI * vol * vol) - 1) < 0.05, qPrintable(QString::number(p01 / (2 * M_PI * vol * vol))));
        QVERIFY2(10 * std::log10(p11 / p01) < -25, qPrintable(QString::number(10 * std::log10(p11 / p01))));
        double jm[12];
        for (int m = 0; m < 12; ++m)
            jm[m] = tabla::besselJ(m, 0.0);
        double re, im;
        tabla::farField(b, k01, 0.0, 0.3, jm, re, im);
        QVERIFY2(std::fabs(re / vol - 1) < 1e-6 && std::fabs(im) < 1e-9 * std::fabs(vol), qPrintable(QString::number(re / vol)));
        tabla::farField(b, k11, 0.0, 0.3, jm, re, im);
        QVERIFY2(std::fabs(re) + std::fabs(im) < 1e-9 * std::fabs(vol), qPrintable(QString::number(re)));
    }

    void theShellsAirRaisesOnlyTheAxisymmetricModes() {
        // Sealed shells: the air is a spring on the head's net volume, so
        // (0,n) go up (the dayan's (0,1) by a few per cent) and modes with
        // nodal diameters don't move.
        tabla::Body b = fresh();
        const double up = b.heardHz(tabla::Dayan, 0) / b.membraneHz(tabla::Dayan, 0);
        QVERIFY2(up > 1.02 && up < 1.12, qPrintable(QString::number(up)));
        QVERIFY(std::fabs(b.heardHz(tabla::Dayan, 1) / b.membraneHz(tabla::Dayan, 1) - 1) < 1e-4);
        QVERIFY(b.heardHz(tabla::Bayan, 0) > 1.05 * b.membraneHz(tabla::Bayan, 0));
    }

    // --- the strokes -----------------------------------------------------------

    void naRingsItsThirdWithNoFundamental() {
        // Fleischer: in na the ring finger resting on the syahi holds a
        // nodal line there; the (0,1) dies, the 3rd harmonic leads (T60 ≈
        // 0.5–0.7 s) with the 4th 8 dB under it and the 5th lower, and the
        // (1,1) lasts longest. Courtney hears "a strong second and third";
        // Patel & Iversen's dha puts the bayan 19 dB over the dayan's 2nd,
        // then 11 (Fleischer's single plot has it weaker).
        const auto na = stroke(tabla::Na, 0.8f, 2.0);
        const double h3 = levelNear(na, 0.02, 0.1, 3 * kSa);
        const double f01 = levelNear(na, 0.02, 0.1, 1.145 * kSa, 80);
        const double h2 = levelNear(na, 0.02, 0.1, 2 * kSa), h4 = levelNear(na, 0.02, 0.1, 4 * kSa),
                     h5 = levelNear(na, 0.02, 0.1, 5 * kSa, 60);
        const QString all = QStringLiteral("01 %1 h2 %2 h4 %3 h5 %4").arg(f01 - h3).arg(h2 - h3).arg(h4 - h3).arg(h5 - h3);
        QVERIFY2(f01 < h3 - 8 && h2 < h3 + 3 && h2 > h3 - 16 && h4 < h3 - 4 && h4 > h3 - 16 && h5 < h3 - 8 && h5 > h3 - 30,
                 qPrintable(all));
        // The fundamental is gone within a tenth of a second; the (1,1)
        // outlasts everything.
        QVERIFY(levelNear(na, 0.25, 0.1, 1.145 * kSa, 80) < h3 - 30);
        const double t3 = t60(na, 3 * kSa, 0.1, 0.4), t2 = t60(na, 2 * kSa, 0.3, 0.9);
        QVERIFY2(t3 > 0.4 && t3 < 0.85, qPrintable(QString::number(t3)));
        QVERIFY2(t2 > 1.5, qPrintable(QString::number(t2)));
    }

    void dhaPutsTheBayanUnderTheDayan() {
        // Patel & Iversen's dha: the bayan's fundamental 19 dB over the
        // dayan's 2nd partial in the first 0.2 s, 11 dB after, played where
        // the page plays na and ge.
        for (double sa : {246.0, 277.2, 330.0}) {
            tabla::Body b;
            b.init(kRate);
            b.tune(float(sa), float(sa / 3));
            float x, y;
            tabla::strokeHome(tabla::Na, x, y);
            b.strike(tabla::Na, 0.8f, x, y, 0, 0);
            tabla::strokeHome(tabla::Ge, x, y);
            b.strike(tabla::Ge, 0.8f, x, y, 0, 0);
            const auto d = play(b, 0.6, [](double) {});
            const double early = levelNear(d, 0, 0.2, sa / 3, 30) - levelNear(d, 0, 0.2, 2 * sa, 30);
            const double late = levelNear(d, 0.2, 0.3, sa / 3, 30) - levelNear(d, 0.2, 0.3, 2 * sa, 30);
            QVERIFY2(std::fabs(early - 19) < 3.5 && std::fabs(late - 11) < 3.5,
                     qPrintable(QStringLiteral("Sa %1: %2 %3").arg(sa).arg(early).arg(late)));
        }
    }

    void tunIsItsFundamental() {
        // Tun, the syahi struck and left: the sharp (0,1) dominant and
        // longest (T60 ≈ 1.5 s), the rest at least 15 dB down and short.
        const auto tun = stroke(tabla::Tun, 0.8f, 2.0);
        const double f01 = 1.145 * kSa, top = levelNear(tun, 0.02, 0.2, f01, 30);
        for (int h = 2; h <= 5; ++h)
            QVERIFY2(levelNear(tun, 0.02, 0.2, h * kSa) < top - 15, qPrintable(QString::number(h)));
        const double t = t60(tun, f01, 0.1, 0.8);
        QVERIFY2(t > 1.1 && t < 2.2, qPrintable(QString::number(t)));
        QVERIFY(t60(tun, 3 * kSa, 0.05, 0.3) < 0.6);
    }

    void tinIsBrighterThanTun() {
        // Patel & Iversen: tin's spectral centroid ≈ 560 Hz, tun's ≈ 295.
        // (the heads as the microphones hear them, before the page's room)
        auto dry = [](int st) {
            tabla::Body b = fresh();
            b.strike(st, 0.8f, kAt[st][0], kAt[st][1], 0, 0);
            return play(b, 0.4, [](double) {});
        };
        const double tin = centroid(dry(tabla::Tin), 0.0, 0.3), tun = centroid(dry(tabla::Tun), 0.0, 0.3);
        QVERIFY2(tin > 480 && tin < 800 && tun > 260 && tun < 450 && tin > 1.5 * tun,
                 qPrintable(QStringLiteral("%1 %2").arg(tin).arg(tun)));
    }

    void whereTheFingerLandsChoosesTheMix() {
        // Struck toward the rim the high modes speak; in the middle the (0,n).
        const auto rim = stroke(tabla::Tin, 0.8f, 0.3, 0.75f, 0.0f), mid = stroke(tabla::Tun, 0.8f, 0.3, 0.0f, 0.0f);
        const double r3 = levelNear(rim, 0.02, 0.15, 3 * kSa) - levelNear(rim, 0.02, 0.15, 1.145 * kSa, 30);
        const double m3 = levelNear(mid, 0.02, 0.15, 3 * kSa) - levelNear(mid, 0.02, 0.15, 1.145 * kSa, 30);
        QVERIFY2(r3 > m3 + 6, qPrintable(QStringLiteral("%1 %2").arg(r3).arg(m3)));
    }

    void harderIsLouderAndBrighter() {
        const auto soft = stroke(tabla::Na, 0.3f, 0.3), hard = stroke(tabla::Na, 1.0f, 0.3);
        QVERIFY(rms(hard, 0, 0.3) > 4 * rms(soft, 0, 0.3));
        QVERIFY(centroid(hard, 0, 0.05) > centroid(soft, 0, 0.05));
    }

    void theFingerTouchesForMilliseconds() {
        // A fingertip bounces off in 1–6 ms (the fleshy middle fingers of
        // ge in up to 9); the flat hand of ka stays longer.
        for (int st : {tabla::Na, tabla::Tin, tabla::Tun, tabla::Ge}) {
            tabla::Body b = fresh();
            b.strike(st, 0.8f, kAt[st][0], kAt[st][1], 0, 0);
            play(b, 0.1, [](double) {});
            QVERIFY2(b.lastContact() > 0.0008 && b.lastContact() < (st == tabla::Ge ? 0.009 : 0.006),
                     qPrintable(QStringLiteral("%1: %2").arg(st).arg(b.lastContact())));
        }
        tabla::Body b = fresh();
        b.strike(tabla::Ka, 0.8f, kAt[tabla::Ka][0], kAt[tabla::Ka][1], 0, 0);
        play(b, 0.1, [](double) {});
        QVERIFY2(b.lastContact() > 0.005 && b.lastContact() < 0.02, qPrintable(QString::number(b.lastContact())));
    }

    // --- damping -----------------------------------------------------------------

    void aRestingFingerDampsTheSkin() {
        // A finger left on the syahi after it struck (te): the head dies
        // within a fraction of a second; lifted, it would ring on.
        tabla::Body open = fresh(), held = fresh();
        open.strike(tabla::Tun, 0.8f, 0, 0.05f, 0, 0);
        const auto a = play(open, 0.6, [](double) {});
        held.strike(tabla::Tun, 0.8f, 0, 0.05f, 0, 0, 7);
        const auto b = play(held, 0.6, [&](double t) {
            if (t >= 0.08)
                held.move(7, 0, 0.05f, 0, true);
        });
        QVERIFY(rms(b, 0, 0.08) > 0.9 * rms(a, 0, 0.08));
        QVERIFY2(rms(b, 0.35, 0.6) < 0.05 * rms(a, 0.35, 0.6), qPrintable(QStringLiteral("%1 %2").arg(rms(b, 0.35, 0.6)).arg(rms(a, 0.35, 0.6))));
        // And a finger that lifts lets it ring again (what is left).
        tabla::Body lifted = fresh();
        lifted.strike(tabla::Tun, 0.8f, 0, 0.05f, 0, 0, 9);
        const auto c = play(lifted, 0.6, [&](double t) {
            if (t >= 0.08 && t < 0.1)
                lifted.move(9, 0, 0.05f, 0, true);
            if (t >= 0.1)
                lifted.lift(9);
        });
        QVERIFY(rms(c, 0.35, 0.6) > rms(b, 0.35, 0.6));
    }

    void aRestingFingerTakesOnlyEnergyOut() {
        // However hard a hand presses, the skin rings down, never up.
        tabla::Body b = fresh();
        b.strike(tabla::Ge, 1.0f, 0.1f, 0.5f, 0, 0, 3);
        b.strike(tabla::Ka, 1.0f, 0.0f, -0.6f, 0, 0, 4);
        const auto x = play(b, 1.0, [&](double t) {
            b.move(3, 0.1f, 0.5f, float(std::min(1.0, t)), true);
            b.move(4, 0.0f, -0.6f, 0, true);
        });
        QVERIFY(finite(x));
        double last = 1e9;
        for (double t = 0.1; t < 0.9; t += 0.1) {
            const double r = rms(x, t, t + 0.1);
            QVERIFY2(r < last * 1.05, qPrintable(QString::number(t)));
            last = r;
        }
    }

    void kaIsDead() {
        // The flat hand: non-resonant, half its level gone in about 8 ms
        // (Patel & Iversen), nothing left after a few tenths.
        const auto ka = stroke(tabla::Ka, 0.8f, 1.0);
        const double h = halfTime(ka);
        QVERIFY2(h < 0.02, qPrintable(QString::number(h)));
        QVERIFY(rms(ka, 0.4, 1.0) < 0.01 * rms(ka, 0, 0.05));
    }

    // --- the bayan -----------------------------------------------------------------

    void theGeIsTheBayansDeepTone() {
        // A single strong partial near 90–100 Hz that persists (T60
        // 0.5–1.3 s with the heel resting), and far weaker peaks near three
        // and five times it, as the ghe's spectrum shows.
        const auto ge = stroke(tabla::Ge, 0.8f, 2.0);
        const double f = peakHz(ge, 0.05, 0.5, kGe, 40);
        QVERIFY2(std::fabs(cents(f, kGe)) < 5, qPrintable(QString::number(f)));
        const double top = levelNear(ge, 0.05, 0.3, kGe, 20);
        for (double r = 1.5; r < 6.0; r += 0.25)
            QVERIFY2(levelNear(ge, 0.05, 0.3, r * kGe, 30) < top - 12, qPrintable(QString::number(r)));
        const double t = t60(ge, kGe, 0.2, 1.0);
        QVERIFY2(t > 0.5 && t < 1.4, qPrintable(QString::number(t)));
    }

    void aHardGeStartsSharp() {
        // The skin stretches as it swings: a hard stroke starts sharp and
        // settles (tension modulation); a soft one doesn't.
        const auto hard = stroke(tabla::Ge, 1.0f, 0.8), soft = stroke(tabla::Ge, 0.3f, 0.8);
        const double dh = cents(peakHz(hard, 0.005, 0.05, kGe), peakHz(hard, 0.5, 0.25, kGe));
        const double ds = cents(peakHz(soft, 0.005, 0.05, kGe), peakHz(soft, 0.5, 0.25, kGe));
        QVERIFY2(dh > 3 && dh < 40 && std::fabs(ds) < 3, qPrintable(QStringLiteral("%1 %2").arg(dh).arg(ds)));
    }

    void theHeelBendsTheGe() {
        // Pressed in, the heel stretches the skin: the ge glides up, an
        // octave at most, and what bend() says is what is heard.
        tabla::Body b = fresh();
        b.strike(tabla::Ge, 0.9f, 0.12f, 0.5f, 0, 0, 5);
        float said = 0;
        const auto x = play(b, 1.2, [&](double t) {
            const float press = t < 0.25 ? 0.0f : t < 0.5 ? float((t - 0.25) / 0.25) : 1.0f;
            b.move(5, 0, -0.7f, press, true);
            if (t > 1.0 && t < 1.01)
                said = b.bend();
        });
        const double before = peakHz(x, 0.05, 0.15, kGe, 30);
        const double after = peakHz(x, 0.8, 0.35, 2 * kGe, 60);
        QVERIFY2(std::fabs(cents(before, kGe)) < 5, qPrintable(QString::number(before)));
        QVERIFY2(std::fabs(cents(after, 2 * kGe)) < 25, qPrintable(QString::number(after)));
        QVERIFY2(std::fabs(said - 12.0f) < 0.3f, qPrintable(QString::number(said)));
        // Halfway through the push, partway up: a glide, not a jump.
        const double mid = 12 * std::log2(peakHz(x, 0.36, 0.06, kGe * std::exp2(0.25), 400) / kGe);
        QVERIFY2(mid > 0.5 && mid < 9, qPrintable(QString::number(mid)));
        // A heel that does it presses a believable depth (mm).
        QVERIFY2(b.octaveDepth() > 0.01 && b.octaveDepth() < 0.03, qPrintable(QString::number(b.octaveDepth())));
    }

    void dhaIsNaAndGeTogether() {
        // Two hands at once: the bayan's ge under the dayan's na, each its
        // own drum; louder in the bass than na alone.
        tabla::Body b = fresh();
        b.strike(tabla::Na, 0.8f, 0.95f, 0, 0, 0);
        b.strike(tabla::Ge, 0.8f, 0.12f, 0.5f, 0, 0);
        const auto x = play(b, 0.6, [](double) {});
        QVERIFY(levelNear(x, 0.05, 0.2, kGe, 20) > levelNear(stroke(tabla::Na, 0.8f, 0.6), 0.05, 0.2, kGe, 20) + 20);
        QVERIFY(levelNear(x, 0.05, 0.2, 3 * kSa) > -80);
    }

    void aRollNeverPumpsEnergyIn() {
        // Na struck sixteen times a second for two seconds: loud, bounded,
        // and it stops when the hand does.
        tabla::Body b = fresh();
        float top = 0;
        const auto x = play(b, 5.0, [&](double t) {
            const int k = int(t * 16);
            static int last = -1;
            if (t < 2.0 && k != last) {
                last = k;
                b.strike(k % 2 ? tabla::Na : tabla::Tin, 0.9f, k % 2 ? 0.95f : 0.66f, 0.1f, 0, 0);
            }
        });
        for (float v : x)
            top = std::max(top, std::fabs(v));
        QVERIFY(finite(x));
        QVERIFY2(top < 0.9f, qPrintable(QString::number(top)));
        QVERIFY(rms(x, 4.5, 5.0) < 0.05 * rms(x, 1.0, 2.0));
    }

    // --- the page -----------------------------------------------------------------

    void thePadIsThePair() {
        // The dayan on the right: its syahi tun, its maidan tin, its kinar
        // na; the bayan on the left: past its syahi ge, its near part ka.
        auto struck = [](const std::string &at) {
            const auto take = testkit::play(toPair() + "tap 0.5 " + at + " p=0.7\nend 0.56\n");
            int best = -1;
            float top = 0.2f;
            for (int p = 0; p < drums::PadCount; ++p)
                if (take.view.parts[0].data[p] > top)
                    top = take.view.parts[0].data[p], best = p;
            if (int(take.view.parts[0].data[34]) != int(drums::TablaPair))
                return -2;
            return best < 0 ? -1 : drums::tablaStroke(best);
        };
        QCOMPARE(struck(padAt(tabla::Dayan, 0, 0)), int(tabla::Tun));
        QCOMPARE(struck(padAt(tabla::Dayan, 0.67f, 0)), int(tabla::Tin));
        QCOMPARE(struck(padAt(tabla::Dayan, 0, -0.9f)), int(tabla::Na));
        QCOMPARE(struck(padAt(tabla::Bayan, 0, 0.5f)), int(tabla::Ge));
        QCOMPARE(struck(padAt(tabla::Bayan, 0, -0.7f)), int(tabla::Ka));
        QCOMPARE(QString::fromLatin1(drums::kitName(drums::TablaPair)), QStringLiteral("tabla pair"));
        QCOMPARE(QString::fromLatin1(drums::padName(drums::tablaPad(tabla::Ge), drums::TablaPair)), QStringLiteral("ge"));
    }

    void aFingerLeftDownDampsThroughThePage() {
        // Tapped, tun rings; pressed and left down, the finger rests on the
        // syahi and the drum goes quiet under it.
        const std::string at = padAt(tabla::Dayan, 0, 0.05f);
        const auto tap = testkit::play(toPair() + "tap 0.5 " + at + " p=0.8 len=0.05\nend 1.4\n");
        const auto hold = testkit::play(toPair() + "down 0.5 1 " + at + " p=0.8\nup 1.35 1\nend 1.4\n");
        QVERIFY(tap.finite() && hold.finite());
        QVERIFY2(hold.rms(0.9, 1.3) < 0.2 * tap.rms(0.9, 1.3),
                 qPrintable(QStringLiteral("%1 %2").arg(hold.rms(0.9, 1.3)).arg(tap.rms(0.9, 1.3))));
    }

    void slidingOnTheBayanBendsTheGe() {
        // A finger on the bayan held and slid away from the player: the
        // heel presses in and the ge glides up; the page shows the bend.
        const std::string from = padAt(tabla::Bayan, 0.1f, 0.3f);
        const float x0 = 0.28f + 0.42f * 0.625f * 0.1f, y0 = 0.53f + 0.42f * 0.3f;
        const auto take = testkit::play(toPair() + "down 0.5 1 " + from + " p=0.8\n" +
                                        fmt("glide 0.7 1.0 1 %.4f %.4f\n", x0, y0 + 0.15f) + "end 1.3\n");
        QVERIFY(take.finite());
        const float bend = take.view.parts[0].data[222];
        QVERIFY2(bend > 1.0f && bend < 12.5f, qPrintable(QString::number(bend)));
    }

    void aFastGeBoomsWhereverItLands() {
        // Played quickly, fingers land all over the bayan, not only round
        // its syahi: every ge still booms, as a real one does under fingers
        // swinging past the heel, and a quick re-stroke over a ringing ge,
        // a second finger down before the first is up, adds to it. (The
        // page once struck the head where it was touched, so a ge landing
        // out at the side or toward the heel was a click with no boom.)
        auto mono = [](const testkit::Take &t) {
            std::vector<float> m(t.left.size());
            for (size_t i = 0; i < m.size(); ++i)
                m[i] = 0.5f * (t.left[i] + t.right[i]);
            return m;
        };
        float sa, ge;
        Ctx ctx;
        drums::tablaTuning(ctx.h, sa, ge);
        const auto lone = mono(testkit::play(toPair() + "tap 0.5 " + padAt(tabla::Bayan, 0.12f, 0.38f) + " p=0.6 len=0.08\nend 1.2\n"));
        double f0 = ge, top = -1e9; // (the page's key, whatever it is)
        for (double f = 50; f < 130; f += 0.25)
            if (const double l = levelAt(lone, 0.55, 0.3, f); l > top)
                top = l, f0 = f;
        const double boom = levelAt(lone, 0.52, 0.06, f0);
        // Ten a second over the drawn bayan above the ka strip, one finger
        // landing as the last lifts, then two fingers overlapping.
        const float spots[][2] = {{0.0f, 0.4f}, {-0.6f, 0.3f}, {0.6f, 0.0f}, {0.0f, -0.35f}, {-0.4f, -0.2f},
                                  {0.4f, 0.6f}, {-0.2f, 0.8f}, {0.7f, -0.3f}, {-0.7f, 0.0f}, {0.2f, 0.1f}};
        // Each alone, against the lone stroke: none a click.
        for (const auto &sp : spots) {
            const auto one = mono(testkit::play(toPair() + "tap 0.5 " + padAt(tabla::Bayan, sp[0], sp[1]) + " p=0.6 len=0.06\nend 0.8\n"));
            const double l = levelAt(one, 0.52, 0.06, f0) - boom;
            QVERIFY2(l > -8, qPrintable(QStringLiteral("alone at (%1, %2): %3 dB").arg(sp[0]).arg(sp[1]).arg(l)));
        }
        for (int overlap = 0; overlap < 2; ++overlap) {
            std::string s = toPair();
            for (int k = 0; k < 10; ++k)
                s += fmt("down %.3f %d ", 0.5 + 0.1 * k, overlap ? 10 + k % 2 : 10 + k) +
                     padAt(tabla::Bayan, spots[k][0], spots[k][1]) + fmt(" p=0.6\nup %.3f %d\n", 0.5 + 0.1 * k + (overlap ? 0.15 : 0.06), overlap ? 10 + k % 2 : 10 + k);
            const auto take = testkit::play(s + "end 2\n");
            QVERIFY(take.finite());
            const auto m = mono(take);
            for (int k = 0; k < 10; ++k) {
                const double l = levelAt(m, 0.52 + 0.1 * k, 0.06, f0) - boom;
                QVERIFY2(l > -8, qPrintable(QStringLiteral("overlap %1, stroke %2 at (%3, %4): %5 dB")
                                                .arg(overlap).arg(k).arg(spots[k][0]).arg(spots[k][1]).arg(l)));
            }
        }
    }

    void aLoopOfThePairKeepsThePair() {
        // Strokes carry the kit: a loop of the pair still sounds after the
        // page has gone back to house.
        const auto take = testkit::play(toPair() + "tap 0.6 " + padAt(tabla::Dayan, 0.95f, 0) +
                                        " p=0.7\ntap 0.9 " + padAt(tabla::Bayan, 0.1f, 0.5f) +
                                        " p=0.7\nkey 1.2 Space\nkey 1.5 Right\nend 6\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(3, 6) > 1e-4);
    }

    // --- the cost -----------------------------------------------------------------

    void thePairCostsLittle() {
        // Both heads ringing and a hand on each: well under a percent of a
        // core per second of sound.
        TENFOLD_TIMED();
        tabla::Body b = fresh();
        const auto t0 = std::chrono::steady_clock::now();
        const auto x = play(b, 4.0, [&](double t) {
            const int k = int(t * 8);
            static int last = -1;
            if (k != last) {
                last = k;
                b.strike(k % 5, 0.8f, kAt[k % 5][0], kAt[k % 5][1], 0, 0, k % 3 == 0 ? 50 + k : 0);
                b.move(50 + k - 3, 0, 0, 0.3f, true);
                b.lift(50 + k - 6);
            }
        });
        const double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        QVERIFY(finite(x));
        QVERIFY2(share < 0.02, qPrintable(QString::number(share)));
    }

    void tenFingersOnThePairStayInsideBudget() {
        // Ten fingers drumming on the pair through the whole studio, as the
        // other pages' budgets are measured.
        TENFOLD_TIMED();
        std::string s = toPair() + "";
        for (int f = 0; f < 10; ++f) {
            const float x = 0.08f + 0.09f * float(f), y = 0.2f + 0.06f * float(f % 6);
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

TESTKIT_REGISTER(TablaTests)
#include "tabla_tests.moc"
