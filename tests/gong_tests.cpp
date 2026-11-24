// The gongs (gong.h), checked as the acoustics papers measure real ones
// (docs/gong.md): the plate they are built from, their tuning, the opera
// gongs' glides (Rossing & Fletcher 1983, Fletcher 1985), the bossed gong's
// ombak (Krueger, Gee & Grimshaw 2010), the tam-tam's cascade into a
// shimmer (Legge & Fletcher 1989), that the stretching only moves energy
// about, that nothing runs away, and what they cost. Then the Lead's three
// gong sounds, played.

#include <chrono>
#include <cstdio>
#include <vector>

#include <QJsonArray>

#include "gong.h"
#include "lead.h"
#include "offline.h"
#include "parts.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;
constexpr int kLead = 3;
constexpr int kGong = 15, kOpera = 16, kTamTam = 17; // the Lead's sounds

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

// One strike on a fresh gong of this kind tuned to hz, the mono sum of what
// it sounds for `seconds`.
std::vector<float> strike(gong::Kind kind, float hz, float vel, double seconds, float where = 0.03f,
                          gong::Gong *keep = nullptr) {
    gong::Gong local;
    gong::Gong &g = keep ? *keep : local;
    g.init(kRate);
    g.start(kind, hz);
    g.strike(vel, where, 0.7f);
    std::vector<float> out;
    float l[64], r[64];
    for (int b = 0; b < int(seconds * kRate) / 64; ++b) {
        std::fill(l, l + 64, 0.0f);
        std::fill(r, r + 64, 0.0f);
        g.render(l, r, 64, 1.0f);
        for (int i = 0; i < 64; ++i)
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

// The strongest peak within ±cents of hz over a window: coarse, then fine.
double peakHz(const std::vector<float> &x, double from, double len, double hz, double cents = 60) {
    double best = hz, top = -1e9;
    const double lo = hz * std::exp2(-cents / 1200), hi = hz * std::exp2(cents / 1200);
    const double step = std::max(0.05, 0.25 / len);
    for (double f = lo; f <= hi; f += step) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    const double c = best;
    for (double f = c - step; f <= c + step; f += step / 20) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    return best;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

// The power (dB) of a band, every 25 Hz from lo to hi, over a window.
double band(const std::vector<float> &x, double from, double len, double lo, double hi) {
    double p = 1e-30;
    for (double f = lo; f < hi; f += 25)
        p += std::pow(10.0, levelAt(x, from, len, f) / 10);
    return 10 * std::log10(p);
}

// What a lead script plays, on one of its sounds.
std::string onSound(int snd, const std::string &rest) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < snd; ++k)
        s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
    return s + rest;
}
// The x of a neck cell (A minor: 0 is A3, 7 A4).
float cellX(int q) { return 0.22f + 0.78f * (float(q) + 0.5f) / 15.0f; }

double levelOf(const testkit::Take &take, double from, double to) {
    const size_t a = size_t(from * kRate), b = std::min(take.left.size(), size_t(to * kRate));
    double e = 1e-30;
    for (size_t i = a; i < b; ++i) {
        const double x = 0.5 * (take.left[i] + take.right[i]);
        e += x * x;
    }
    return 10 * std::log10(2.0 * e / double(std::max<size_t>(b - a, 1)));
}

std::vector<float> mono(const testkit::Take &t) {
    std::vector<float> m(t.left.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (t.left[i] + t.right[i]);
    return m;
}

}

class GongTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs them with the machine to itself.
    Q_CLASSINFO("alone", "aGongStaysInsideBudget fourTamTamsStayInsideBudget")
private slots:
    void initTestCase() { gong::prepare(); }

    void thePlateIsTheClampedPlate() {
        // The roots of the clamped circular plate's frequency equation, as
        // Leissa tabulates them (λ = 3.1962, 4.6109, 5.9057, 6.3064, 7.7993
        // …): the plate every gong is built from.
        const struct {
            int m;
            int n;
            double l;
        } want[] = {{0, 1, 3.1962}, {0, 2, 6.3064}, {0, 3, 9.4395}, {1, 1, 4.6109}, {1, 2, 7.7993}, {2, 1, 5.9057},
                    {3, 1, 7.1435}};
        for (const auto &w : want) {
            const double got = gong::plateRoots(w.m)[size_t(w.n - 1)];
            QVERIFY2(std::fabs(got / w.l - 1) < 3e-4, qPrintable(QStringLiteral("(%1,%2): %3").arg(w.m).arg(w.n).arg(got)));
        }
        // Each mode's shape is the clamped plate's: nothing at the rim, and
        // the axisymmetric fundamental's widest swing in the middle.
        const auto &d = gong::design(gong::Opera, 0);
        for (int k = 0; k < d.n; ++k)
            QVERIFY(std::fabs(d.shape[size_t(k) * gong::Design::kGrid + gong::Design::kGrid - 1]) < 0.02f);
        QVERIFY(std::fabs(d.shape[size_t(d.axis[0]) * gong::Design::kGrid] - 1.0f) < 0.01f);
        // The opera gong's flat face: the plate's own spectrum, (1,1) at
        // 2.08, (2,1) 3.41, (0,2) 3.89 times the fundamental.
        auto ratioOf = [&](int m, int n) {
            for (int k = 0; k < d.n; ++k)
                if (d.m[k] == m && d.nr[k] == n && d.orient[k] == 0)
                    return double(d.lambda2[k]) / d.lambda2[size_t(d.axis[0])];
            return 0.0;
        };
        QVERIFY(std::fabs(ratioOf(2, 1) / 3.414 - 1) < 0.02);
        QVERIFY(std::fabs(ratioOf(0, 2) / 3.893 - 1) < 0.002);
    }

    void eachGongSoundsItsNote() {
        // Struck softly, each gong's fundamental (a tam-tam's thud) is on
        // its note within a couple of cents, low and high.
        for (int k = 0; k < gong::kKinds; ++k)
            for (float hz : {110.0f, 220.0f, 440.0f, 880.0f}) {
                if (k == gong::TamTam && hz > 440.0f)
                    continue;
                const auto x = strike(gong::Kind(k), hz, 0.08f, 1.6);
                const double got = peakHz(x, 0.3, 1.2, hz);
                // (A tam-tam's thud a little looser: its hammered texture
                // couples it to every other mode.)
                QVERIFY2(std::fabs(cents(got, hz)) < (k == gong::TamTam ? 8.0 : 2.5), qPrintable(QStringLiteral("kind %1 at %2: %3 Hz").arg(k).arg(hz).arg(got)));
            }
    }

    void theBossedGongIsTheSmiths() {
        // A Balinese gong's modes, by Krueger et al.'s vibrometry: the
        // axisymmetric pair an octave apart less the ombak, the single-
        // diameter pair at 1.490 and 1.648, the pairs near 2.43–2.48 and
        // 2.59–2.63, the next at 3.82.
        gong::Gong g;
        g.init(kRate);
        g.start(gong::Bossed, 200.0f);
        const auto &d = *g.built();
        auto hzOf = [&](int m, int n, int o) {
            for (int k = 0; k < d.n; ++k)
                if (d.m[k] == m && d.nr[k] == n && d.orient[k] == o)
                    return g.modeHz(k);
            return 0.0f;
        };
        QVERIFY(std::fabs(hzOf(1, 1, 0) / 200.0f - 1.490f) < 1e-3f);
        QVERIFY(std::fabs(hzOf(1, 1, 1) / 200.0f - 1.648f) < 1e-3f);
        QVERIFY(std::fabs(hzOf(2, 1, 0) / 200.0f - 2.433f) < 1e-3f);
        QVERIFY(std::fabs(hzOf(3, 1, 1) / 200.0f - 2.632f) < 1e-3f);
        QVERIFY(std::fabs(hzOf(1, 2, 0) / 200.0f - 3.822f) < 1e-3f);
        const float ombak = gong::Gong::ombakHz(200.0f);
        QVERIFY(ombak > 2.0f && ombak < 6.0f);
        // And it sounds them: struck softly on the boss, its second
        // axisymmetric mode rings where the smith put it, shell and all.
        const auto x = strike(gong::Bossed, 200.0f, 0.08f, 3.0);
        const double f2 = peakHz(x, 0.3, 2.5, 400.0 - ombak, 15);
        QVERIFY2(std::fabs(f2 - (400.0 - ombak)) < 0.4, qPrintable(QString::number(f2)));
    }

    void theBossedGongBeatsItsOmbak() {
        // Struck as a gamelan player strikes it, the boss's own stretching
        // sounds its fundamental's octave, which beats with the smith's
        // octave a few hertz under it: the ombak. Krueger et al. measured
        // the two of about equal strength; and 3f1 beside f1 + f2.
        const float hz = 200.0f, ombak = gong::Gong::ombakHz(hz);
        gong::Gong g;
        const auto x = strike(gong::Bossed, hz, 0.7f, 4.0, 0.03f, &g);
        const double oct = levelAt(x, 0.3, 3.0, 2.0 * hz), smith = levelAt(x, 0.3, 3.0, 2.0 * hz - ombak);
        const double f1 = levelAt(x, 0.3, 3.0, hz);
        QVERIFY2(oct > smith - 15 && oct < smith + 3, qPrintable(QStringLiteral("2f1 %1 f2 %2").arg(oct).arg(smith)));
        QVERIFY2(oct < f1 - 6, qPrintable(QStringLiteral("2f1 %1 f1 %2").arg(oct).arg(f1)));
        const double three = levelAt(x, 0.3, 3.0, 3.0 * hz), sum = levelAt(x, 0.3, 3.0, 3.0 * hz - ombak);
        const double floor = levelAt(x, 0.3, 3.0, 2.75 * hz);
        QVERIFY2(three > floor + 20 && sum > floor + 20, qPrintable(QStringLiteral("%1 %2 %3").arg(three).arg(sum).arg(floor)));
        // The beat: the octave band's level rises and falls at the ombak's
        // rate, a steady wave, not a drift.
        std::vector<double> env;
        const double hop = 0.02;
        for (double t = 0.5; t < 3.5; t += hop) {
            double re = 0, im = 0;
            for (int i = 0; i < int(0.08 * kRate); ++i) {
                const size_t j = size_t((t + i / kRate) * kRate);
                const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (0.08 * kRate));
                re += w * x[j] * std::cos(2 * M_PI * 2.0 * hz * (t + i / kRate));
                im += w * x[j] * std::sin(2 * M_PI * 2.0 * hz * (t + i / kRate));
            }
            env.push_back(std::hypot(re, im));
        }
        // (In decibels, the decay taken out by a straight line through them.)
        const double n = double(env.size());
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (size_t i = 0; i < env.size(); ++i) {
            env[i] = 20 * std::log10(env[i] + 1e-12);
            sx += double(i), sy += env[i], sxx += double(i) * double(i), sxy += double(i) * env[i];
        }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), cut = (sy - slope * sx) / n;
        for (size_t i = 0; i < env.size(); ++i)
            env[i] -= cut + slope * double(i);
        double best = 0, bestHz = 0;
        for (double f = 1.5; f < 8.0; f += 0.05) {
            double re = 0, im = 0;
            for (size_t i = 0; i < env.size(); ++i) {
                re += env[i] * std::cos(2 * M_PI * f * hop * double(i));
                im += env[i] * std::sin(2 * M_PI * f * hop * double(i));
            }
            if (std::hypot(re, im) > best)
                best = std::hypot(re, im), bestHz = f;
        }
        QVERIFY2(std::fabs(bestHz - ombak) < 0.3, qPrintable(QStringLiteral("beats at %1, ombak %2").arg(bestHz).arg(ombak)));
        // And no glide: a bossed gong's pitch doesn't move with how hard it
        // is struck (Krueger et al.).
        QVERIFY(std::fabs(cents(peakHz(x, 0.05, 0.4, hz), hz)) < 6);
    }

    void theFlatOperaGongGlidesDown() {
        // The large opera gong, flat in the middle: struck hard it swings
        // about its thickness (Rossing & Fletcher: 0.8–1.0 h), starts sharp
        // by what their Table I gives (176 → 212 Hz, 180 → 212: +287…+316
        // cents) and glides down onto its note as it dies, over about a
        // second (Fletcher 1999).
        gong::Gong g;
        const auto x = strike(gong::Opera, 200.0f, 1.0f, 2.5, 0.03f, &g);
        auto pitchAt = [&](double t) { return cents(peakHz(x, t, 0.08, 200.0 * std::exp2(250 / 1200.0), 420), 200.0); };
        const double p0 = pitchAt(0.04), p1 = pitchAt(0.25), p2 = pitchAt(0.6), p3 = pitchAt(1.6);
        QVERIFY2(p0 > 220 && p0 < 400, qPrintable(QString::number(p0)));
        QVERIFY2(p0 > p1 && p1 > p2 && p2 > p3, qPrintable(QStringLiteral("%1 %2 %3 %4").arg(p0).arg(p1).arg(p2).arg(p3)));
        QVERIFY2(p3 < 25, qPrintable(QString::number(p3)));
        // How far it swung, just after the mallet left.
        gong::Gong h;
        h.init(kRate);
        h.start(gong::Opera, 200.0f);
        h.strike(1.0f, 0.03f, 0.7f);
        float l[64], r[64];
        for (int b = 0; b < int(0.03 * kRate) / 64; ++b)
            h.render(l, r, 64, 1.0f);
        const float swing = h.swing(h.built()->axis[0]);
        QVERIFY2(swing > 0.7f && swing < 1.5f, qPrintable(QString::number(swing)));
    }

    void theDomedOperaGongGlidesUp() {
        // The small opera gong, domed by a millimetre over 0.7 mm of bronze:
        // struck hard it starts flat (Table I: 472 → 390, 472 → 430, 700 →
        // 640 Hz, −155…−323 cents) and glides up onto its note.
        const auto x = strike(gong::Opera, 600.0f, 1.0f, 2.0);
        auto pitchAt = [&](double t) { return cents(peakHz(x, t, 0.06, 600.0 * std::exp2(-150 / 1200.0), 300), 600.0); };
        const double p0 = pitchAt(0.04), p1 = pitchAt(0.2), p2 = pitchAt(0.45), p3 = pitchAt(1.2);
        QVERIFY2(p0 < -80 && p0 > -350, qPrintable(QString::number(p0)));
        // (Struck hardest it can swing past where the shell gives most, and
        // dip a little further first: Fletcher's reversing glide.)
        QVERIFY2(p1 < p2 && p2 <= p3 + 2 && p0 < p3 - 80, qPrintable(QStringLiteral("%1 %2 %3 %4").arg(p0).arg(p1).arg(p2).arg(p3)));
        QVERIFY2(std::fabs(p3) < 25, qPrintable(QString::number(p3)));
        // Struck gently, both gongs barely move: nearly a sine at the note
        // (Fletcher 1999: "a simple and nearly sinusoidal tone").
        for (float hz : {200.0f, 600.0f}) {
            const auto soft = strike(gong::Opera, hz, 0.15f, 0.6);
            const double c = cents(peakHz(soft, 0.04, 0.1, hz, 200), hz);
            QVERIFY2(std::fabs(c) < 25, qPrintable(QStringLiteral("%1: %2").arg(hz).arg(c)));
        }
    }

    void theTamTamBlossomsIntoAShimmer() {
        // Struck hard in the middle, a tam-tam starts as a deep thud, and
        // over about a second the high modes, which the soft beater barely
        // reached, fill with the energy the stretching passes up to them,
        // into a bright wash that outlasts the thud (Legge & Fletcher 1989:
        // "over a time of about 1 s, blossoms"; Fletcher 1999: 2–3 s).
        const float hz = 110.0f;
        const auto x = strike(gong::TamTam, hz, 0.7f, 3.5);
        const double hiLo = 30 * hz, hiHi = 90 * hz;
        auto high = [&](double t) { return band(x, t, 0.05, hiLo, hiHi); };
        auto thud = [&](double t) { return band(x, t, 0.05, 0.75 * hz, 1.3 * hz); };
        const double start = high(0.02);
        double peak = -1e9, when = 0;
        for (double t = 0.05; t < 2.5; t += 0.05)
            if (high(t) > peak)
                peak = high(t), when = t;
        QVERIFY2(peak > start + 12, qPrintable(QStringLiteral("%1 → %2").arg(start).arg(peak)));
        QVERIFY2(when > 0.15 && when < 2.0, qPrintable(QString::number(when)));
        // The thud leads at first and the wash after: the balance turns.
        const double early = thud(0.03) - high(0.03), late = thud(2.0) - high(2.0);
        QVERIFY2(early > late + 15, qPrintable(QStringLiteral("%1 %2").arg(early).arg(late)));
        // Struck softly it stays a thud: nothing to pass up.
        const auto soft = strike(gong::TamTam, hz, 0.15f, 2.0);
        double softPeak = -1e9;
        for (double t = 0.05; t < 1.8; t += 0.1)
            softPeak = std::max(softPeak, band(soft, t, 0.05, hiLo, hiHi) - band(soft, t, 0.05, 0.75 * hz, 1.3 * hz));
        QVERIFY2(softPeak < -20, qPrintable(QString::number(softPeak)));
    }

    void theTamTamKeepsItsTuningAsItRingsDown() {
        // Rung down, a tam-tam's stretching fades out (to save the work);
        // the sheet's own modes carry its stiffness, so nothing is retuned
        // when it does: once the shimmer has died down every partial sounds
        // where a barely touched tam-tam's does. (While it shimmers, the
        // partials stand a little sharp: the sheet stiffened by its swing.)
        const float hz = 110.0f;
        gong::Gong g;
        const auto x = strike(gong::TamTam, hz, 0.4f, 9.0, 0.03f, &g);
        for (double at : {330.0, 700.0, 1500.0, 3000.0}) {
            const double late = peakHz(x, 8.0, 0.6, at, 250);
            double nearest = 1e9;
            for (int k = 0; k < g.modes(); ++k)
                if (std::fabs(cents(late, g.modeHz(k))) < std::fabs(nearest))
                    nearest = cents(late, g.modeHz(k));
            QVERIFY2(std::fabs(nearest) < 3, qPrintable(QStringLiteral("%1 Hz, %2 c from a mode").arg(late).arg(nearest)));
        }
    }

    void theStretchingOnlyMovesEnergyAbout() {
        // With every loss taken away, a hard strike's energy, once the
        // mallet has gone, stays what it was through all the trading
        // between modes (the shell's potential is a sum of squares): a
        // tam-tam's cascade, an opera gong's glide, a bossed gong's ombak.
        for (int k = 0; k < gong::kKinds; ++k) {
            gong::Gong g;
            g.init(kRate);
            g.start(gong::Kind(k), k == gong::TamTam ? 110.0f : 300.0f);
            g.setLossless(true);
            // (A tam-tam struck as hard as its shimmer wants, not past it:
            // swung two thicknesses, its explicit step drifts a few per cent.)
            g.strike(k == gong::TamTam ? 0.6f : 1.0f, 0.05f, 0.7f);
            float l[64], r[64];
            for (int b = 0; b < int(0.05 * kRate) / 64; ++b)
                g.render(l, r, 64, 1.0f);
            const double e0 = g.energy();
            double lo = e0, hi = e0;
            for (int b = 0; b < int(1.0 * kRate) / 64; ++b) {
                g.render(l, r, 64, 1.0f);
                lo = std::min(lo, g.energy());
                hi = std::max(hi, g.energy());
            }
            QVERIFY2(hi < 1.03 * e0 && lo > 0.97 * e0, qPrintable(QStringLiteral("kind %1: %2 %3 %4").arg(k).arg(e0).arg(lo).arg(hi)));
        }
    }

    void nothingRunsAway() {
        // Hammered: strikes as hard as they come, anywhere, soft and hard
        // beaters, while the pitch is bent about: every gong stays finite
        // and bounded, and rings down to silence once left alone.
        for (int k = 0; k < gong::kKinds; ++k) {
            float one = 0;
            for (float v : strike(gong::Kind(k), 220.0f, 1.0f, 0.5))
                one = std::max(one, std::fabs(v));
            gong::Gong g;
            g.init(kRate);
            g.start(gong::Kind(k), 220.0f);
            Rng rng(0x5eedu + uint32_t(k));
            float l[64], r[64], top = 0;
            for (int b = 0; b < int(4.0 * kRate) / 64; ++b) {
                if (b % 75 == 0) // ten strokes a second
                    g.strike(1.0f, 0.9f * rng.unit(), 6.28f * rng.unit(), 0, rng.unit());
                g.setPitch(220.0f * std::exp2(2.0f * std::sin(0.01f * float(b)) / 12.0f));
                std::fill(l, l + 64, 0.0f);
                std::fill(r, r + 64, 0.0f);
                g.render(l, r, 64, 1.0f);
                for (int i = 0; i < 64; ++i) {
                    QVERIFY2(std::isfinite(l[i]) && std::isfinite(r[i]), qPrintable(QString::number(k)));
                    top = std::max(top, std::fabs(l[i]));
                }
            }
            // (Strokes landing on a gong already swinging add up, a little.)
            QVERIFY2(top < 12.0f * one, qPrintable(QStringLiteral("kind %1: %2 against one strike's %3").arg(k).arg(top).arg(one)));
            g.mute(0.5f);
            for (int b = 0; b < int(3.0 * kRate) / 64; ++b)
                g.render(l, r, 64, 1.0f);
            QVERIFY2(!g.active(), qPrintable(QString::number(k)));
        }
    }

    void theLeadPlaysItsGongs() {
        // Chosen with → past the Accordion, each says what it is for, saves
        // with a song by its name and comes back by it.
        const char *styled[] = {"Gong · gamelan", "Opera gong · Chinese opera", "Tam-tam · film score"};
        for (int snd = kGong; snd <= kTamTam; ++snd) {
            testkit::reopen(onSound(snd, "end 0.5\n"), 0.3, [&](Session &, Session &opened, const QJsonObject &doc) {
                const auto settings =
                    doc[QStringLiteral("parts")].toArray()[kLead].toObject()[QStringLiteral("settings")].toObject();
                QCOMPARE(settings[QStringLiteral("sound")].toString(), QString::fromUtf8(lead::sound(snd).name));
                QVERIFY2(opened.pageStatus().contains(QString::fromUtf8(styled[snd - kGong])), qPrintable(opened.pageStatus()));
            });
            // A note: heard, finite, under full scale, in the key (A4 on the
            // neck; a tam-tam's thud an octave under it).
            // (Lifted before a tam-tam's roll begins; it rings on under the hand.)
            const auto take = testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.4 p=0.5\nup 0.45 1\nend 2.8\n", cellX(7))));
            QVERIFY2(take.finite() && take.peak() < 1.0, lead::sound(snd).name);
            QVERIFY2(take.rms(0.35, 1.2) > 0.01, lead::sound(snd).name);
            const auto x = mono(take);
            const double want = snd == kTamTam ? 220.0 : 440.0;
            const double got = peakHz(x, 1.3, 1.0, want, 50);
            // (A tam-tam's thud still a few cents sharp a second on: the sheet
            // still swings, and stiffens with it.)
            QVERIFY2(std::fabs(cents(got, want)) < (snd == kTamTam ? 12 : 8),
                     qPrintable(QStringLiteral("%1: %2").arg(lead::sound(snd).name).arg(got)));
        }
    }

    void theStrikeIsTheTouch() {
        // Harder is louder and brighter; higher on the pad strikes further
        // out, off the boss, where the note is weaker against the rest.
        auto play = [&](int snd, double y, double p) {
            return testkit::play(onSound(snd, fmt("down 0.3 1 %.4f %.3f p=%.3f\nup 1.2 1\nend 1.4\n", cellX(7), y, p)));
        };
        const auto soft = play(kGong, 0.4, 0.25), hard = play(kGong, 0.4, 0.95);
        QVERIFY2(levelOf(hard, 0.32, 0.6) > levelOf(soft, 0.32, 0.6) + 4,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(hard, 0.32, 0.6)).arg(levelOf(soft, 0.32, 0.6))));
        // (On the boss the note and its octave pair; off it, on the face,
        // the clang of the single-diameter mode at 1.49 times the note.)
        const auto boss = mono(play(kGong, 0.4, 0.7)), face = mono(play(kGong, 0.95, 0.7));
        auto clang = [&](const std::vector<float> &x) {
            return levelAt(x, 0.35, 0.5, 1.490 * 440.0) - levelAt(x, 0.35, 0.5, 440.0);
        };
        QVERIFY2(clang(face) > clang(boss) + 10, qPrintable(QStringLiteral("%1 %2").arg(clang(face)).arg(clang(boss))));
    }

    void aHeldTamTamIsRolledAndSwells() {
        // Held, the tam-tam is rolled with soft beaters, swelling over a
        // second or two, harder the harder the press; let go, a hand stills
        // it over a few seconds rather than at once. (A medium press, so the
        // swell has room under the studio's ceiling.)
        const auto held = testkit::play(onSound(kTamTam, fmt("down 0.3 1 %.4f 0.4 p=0.6\nup 4.0 1\nend 8.5\n", cellX(7))));
        const auto tap = testkit::play(onSound(kTamTam, fmt("down 0.3 1 %.4f 0.4 p=0.6\nup 0.45 1\nend 4.5\n", cellX(7))));
        QVERIFY2(levelOf(held, 2.8, 3.8) > levelOf(tap, 2.8, 3.8) + 6,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(held, 2.8, 3.8)).arg(levelOf(tap, 2.8, 3.8))));
        QVERIFY2(levelOf(held, 2.8, 3.8) > levelOf(held, 0.6, 1.0),
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(held, 2.8, 3.8)).arg(levelOf(held, 0.6, 1.0))));
        QVERIFY(held.finite() && held.peak() < 1.0);
        // After the lift it rings on, a hand only resting on it: still
        // strong half a second on, and four seconds on fading but heard
        // (Legge & Fletcher: dying away over as long as 10 s).
        QVERIFY2(levelOf(held, 4.4, 4.6) > levelOf(held, 3.6, 3.9) - 12, "a hand, not a gate");
        const double late = levelOf(held, 7.8, 8.4) - levelOf(held, 3.6, 3.9);
        QVERIFY2(late < -8 && late > -45, qPrintable(QString::number(late)));
    }

    void liftingTheFingerStillsTheGong() {
        // Held, an opera gong rings on its own; lifted, a hand lies on it
        // and it dies within about a second.
        const auto held = testkit::play(onSound(kOpera, fmt("down 0.3 1 %.4f 0.4 p=0.7\nup 3.0 1\nend 3.2\n", cellX(4))));
        const auto lifted = testkit::play(onSound(kOpera, fmt("down 0.3 1 %.4f 0.4 p=0.7\nup 0.8 1\nend 3.2\n", cellX(4))));
        QVERIFY2(levelOf(lifted, 2.0, 2.4) < levelOf(held, 2.0, 2.4) - 18,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(lifted, 2.0, 2.4)).arg(levelOf(held, 2.0, 2.4))));
    }

    void aGongStaysInsideBudget() {
        // A hard strike on each kind, rendered against the clock: a bossed
        // or opera gong costs a fraction of a percent of real time, a
        // tam-tam (116 sheet modes and the stretching of eight patches) about one.
        TENFOLD_TIMED();
        for (int k = 0; k < gong::kKinds; ++k) {
            gong::Gong g;
            g.init(kRate);
            g.start(gong::Kind(k), k == gong::TamTam ? 110.0f : 300.0f);
            g.strike(1.0f, 0.05f, 0.7f);
            float l[32], r[32];
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < int(4.0 * kRate) / 32; ++b)
                g.render(l, r, 32, 1.0f);
            const double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
            QVERIFY2(share < (k == gong::TamTam ? 0.04 : 0.012), qPrintable(QStringLiteral("kind %1: %2").arg(k).arg(share)));
        }
    }

    void fourTamTamsStayInsideBudget() {
        // A run of hard tam-tam strokes through the whole studio, each left
        // to ring: four ring at most, and the studio keeps its time.
        TENFOLD_TIMED();
        std::string s = onSound(kTamTam, "");
        for (int i = 0; i < 10; ++i)
            s += fmt("down %.2f %d %.4f 0.6 p=0.95\nup %.2f %d\n", 0.5 + 0.35 * i, i + 1, cellX((i * 3) % 15), 0.8 + 0.35 * i,
                     i + 1);
        s += "end 8\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.maxVoices <= 4, qPrintable(QString::number(st.maxVoices)));
        QVERIFY2(st.p99Load < 0.2, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(GongTests)
#include "gong_tests.moc"
