// The daxophone (daxophone.h), checked as docs/daxophone.md says it is
// built: its tongue is the Euler–Bernoulli cantilever and Saint-Venant
// twisting rod the textbooks give, the part behind the dax is followed in
// full, the dax sets the pitch along its travel and the player's ear brings
// it onto the note without swinging past it, a glide never clicks, the
// bow's force picks the regime (skating under Schelleng's minimum, a
// singing tone, broken up over his maximum), every note sings at every
// touch, nothing runs away, and it is cheap. Then the Lead's daxophone,
// played.

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "daxophone.h"
#include "lead.h"
#include "offline.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;
constexpr int kDaxSound = 19; // the Lead's

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

// The window of bow force where the tongue sings its note (at 0.2 m/s, as
// the model's own scan finds it), its middle, the force a player bows with
// at an ordinary press (dax::ordinaryForce), and the hardest press's (2.5
// times the roof).
float midForce(float hz) {
    float lo = 0, hi = 0;
    dax::window(hz, lo, hi);
    return std::sqrt(lo * hi);
}
float roofForce(float hz) {
    float lo = 0, hi = 0;
    dax::window(hz, lo, hi);
    return hi;
}
float ordinaryForce(float hz) { return dax::ordinaryForce(hz); }
// How firmly the dax is pressed at an ordinary touch (lead.cpp's kDaxPress).
constexpr float kPress = 0.35f;

struct Bowed {
    std::vector<float> mono, left, right;
};

// A tongue bowed for `seconds`: the bow landed with its weight, drawn up
// to speed over 30 ms. `play` sets the pitch (or the dax) each block.
template <typename F>
Bowed bow(double seconds, float force, float speed, float across, F &&play, float press = kPress, float along = 0,
         const dax::Design *design = nullptr) {
    dax::Tongue t;
    t.init(kRate, design);
    t.setDaxPress(press);
    Bowed out;
    float l[32], r[32], drawn = 0;
    const int blocks = int(seconds * kRate) / 32;
    for (int b = 0; b < blocks; ++b) {
        drawn += (1.0f - drawn) * (1.0f - std::exp(-32.0f / kRate / 0.03f));
        play(t, double(b) * 32.0 / kRate);
        t.setBow(force, speed * drawn, across, along);
        std::fill(l, l + 32, 0.0f);
        std::fill(r, r + 32, 0.0f);
        t.render(l, r, 32, 1.0f);
        for (int i = 0; i < 32; ++i) {
            out.left.push_back(l[i]);
            out.right.push_back(r[i]);
            out.mono.push_back(0.5f * (l[i] + r[i]));
        }
    }
    return out;
}

// The period by normalised autocorrelation over [from, from + len): the
// first peak within 0.9 of the strongest. Clarity is that peak's height.
double pitchOf(const std::vector<float> &x, double from, double len, double *clarity = nullptr) {
    const int a = int(from * kRate), n = int(len * kRate), lo = 12, hi = 1600;
    if (a + n + hi >= int(x.size()))
        return 0;
    std::vector<double> r(size_t(hi + 2), 0.0);
    double mx = 0;
    for (int L = lo; L <= hi + 1; ++L) {
        double s = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < n; ++i) {
            s += double(x[size_t(a + i)]) * x[size_t(a + i + L)];
            e0 += double(x[size_t(a + i)]) * x[size_t(a + i)];
            e1 += double(x[size_t(a + i + L)]) * x[size_t(a + i + L)];
        }
        r[size_t(L)] = s / std::sqrt(e0 * e1 + 1e-30);
        if (L <= hi)
            mx = std::max(mx, r[size_t(L)]);
    }
    for (int L = lo + 1; L <= hi; ++L)
        if (r[size_t(L)] > 0.9 * mx && r[size_t(L)] >= r[size_t(L - 1)] && r[size_t(L)] >= r[size_t(L + 1)]) {
            const double y0 = r[size_t(L - 1)], y1 = r[size_t(L)], y2 = r[size_t(L + 1)];
            if (clarity)
                *clarity = y1;
            return kRate / (L + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2));
        }
    return 0;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

double levelAt(const std::vector<float> &x, double from, double len, double hz);
// Where the strongest peak within 80 cents of a note lies,
// searched on a fine grid (a tone whose second harmonic is as loud as its
// fundamental fools an autocorrelation into its octave; this doesn't).
double peakNear(const std::vector<float> &x, double from, double len, double hz, double span = 80) {
    double best = hz, top = -1e9;
    for (double c = -span; c <= span; c += 1) {
        const double f = hz * std::exp2(c / 1200), l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    return best;
}

double rms(const std::vector<float> &x, double from, double to) {
    const size_t a = size_t(from * kRate), b = std::min(x.size(), size_t(to * kRate));
    double e = 1e-30;
    for (size_t i = a; i < b; ++i)
        e += double(x[i]) * x[i];
    return std::sqrt(e / double(std::max<size_t>(b - a, 1)));
}
double db(double x) { return 20 * std::log10(x + 1e-30); }

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

// How noisy a tone is: the power between its first twelve harmonics (at
// the half harmonics) over its loudest harmonic's, in dB.
double noiseRatio(const std::vector<float> &x, double from, double len, double f0) {
    double top = -1e9, p = 0;
    for (int h = 1; h <= 12; ++h) {
        top = std::max(top, levelAt(x, from, len, h * f0));
        p += std::pow(10.0, levelAt(x, from, len, (h + 0.5) * f0) / 10) / 12;
    }
    return 10 * std::log10(p) - top;
}
// How rich a tone is: its second to tenth harmonics' power over its
// fundamental's, in dB.
double richness(const std::vector<float> &x, double from, double len, double f0) {
    const double l0 = levelAt(x, from, len, f0);
    double p = 0;
    for (int h = 2; h <= 10; ++h)
        p += std::pow(10.0, (levelAt(x, from, len, h * f0) - l0) / 10);
    return 10 * std::log10(p);
}
// Its harmonics' levels against its fundamental's, second to twelfth.
std::vector<double> profile(const std::vector<float> &x, double from, double len, double f0) {
    const double l0 = levelAt(x, from, len, f0);
    std::vector<double> p;
    for (int h = 2; h <= 12; ++h)
        p.push_back(std::max(levelAt(x, from, len, h * f0) - l0, -70.0));
    return p;
}

// What a click is: a burst at the top of the band (the fourth difference,
// over 2 ms frames) standing out of its neighbourhood. The largest frame
// over the median of the eight around it, in dB: a tone whose brightness
// changes as it glides moves its neighbours with it; a click does not.
double clickiness(const std::vector<float> &x, double from, double to) {
    const int a = int(from * kRate), b = int(to * kRate), fr = int(0.002 * kRate);
    std::vector<double> e;
    for (int s = a; s + fr < b; s += fr) {
        double v = 1e-30;
        for (int i = s; i < s + fr; ++i) {
            const double d = x[size_t(i)] - 4.0 * x[size_t(i - 1)] + 6.0 * x[size_t(i - 2)] - 4.0 * x[size_t(i - 3)] + x[size_t(i - 4)];
            v += d * d;
        }
        e.push_back(v);
    }
    double worst = 0;
    for (size_t i = 4; i + 4 < e.size(); ++i) {
        std::vector<double> n;
        for (size_t j = i - 4; j <= i + 4; ++j)
            if (j != i)
                n.push_back(e[j]);
        std::sort(n.begin(), n.end());
        worst = std::max(worst, 10 * std::log10(e[i] / (0.5 * (n[3] + n[4]))));
    }
    return worst;
}

// The Lead on its daxophone.
std::string onDax(const std::string &rest) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < kDaxSound; ++k)
        s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
    return s + rest;
}
// The x of a neck cell (A minor: 0 is A3, 7 A4).
float cellX(double q) { return float(0.22 + 0.78 * (q + 0.5) / 15.0); }
std::vector<float> mono(const testkit::Take &t) {
    std::vector<float> m(t.left.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (t.left[i] + t.right[i]);
    return m;
}

dax::Spec uniform() {
    dax::Spec sp;
    for (int i = 0; i < dax::Spec::kPts; ++i) {
        sp.ws[i] = 1.0;
        sp.cs[i] = 0.0;
    }
    return sp;
}

}

class DaxophoneTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs them with the machine to itself.
    Q_CLASSINFO("alone", "aTongueStaysInsideBudget aDaxophoneLineStaysInsideBudget")
private slots:
    void initTestCase() { dax::prepare(); }

    void theTongueIsACantilever() {
        // A straight tongue of the reference board (330 × 30 × 5 mm of
        // maple), the dax at the clamp: its bending modes are the
        // clamped-free beam's, 1 : 6.267 : 17.55 : 34.39 (βL = 1.875,
        // 4.694, 7.855, 10.996), the fundamental 0.1615·h/ℓ²·√(E/ρ); its
        // twisting ones Saint-Venant's, (2n−1)/4ℓ · √(GJ/ρI_p).
        const dax::Spec sp = uniform();
        const dax::Design d = dax::build(sp);
        const dax::Entry &e = d.e[0];
        const double ell = (1.0 - e.u) * sp.L;
        std::vector<double> bend, twist;
        for (int k = 0; k < dax::kFront; ++k)
            (std::fabs(e.tipW[k]) > 0.3f ? bend : twist).push_back(e.hz[k]);
        std::sort(bend.begin(), bend.end());
        std::sort(twist.begin(), twist.end());
        const double f1 = 0.1615 * sp.h / (ell * ell) * std::sqrt(sp.E / sp.rho);
        QVERIFY2(std::fabs(bend[0] / f1 - 1) < 0.03, qPrintable(QStringLiteral("%1 vs %2").arg(bend[0]).arg(f1)));
        const double want[] = {6.267, 17.55, 34.39};
        for (int k = 0; k < 3; ++k)
            QVERIFY2(std::fabs(bend[size_t(k + 1)] / bend[0] / want[k] - 1) < 0.025,
                     qPrintable(QStringLiteral("mode %1: %2").arg(k + 2).arg(bend[size_t(k + 1)] / bend[0])));
        const double J = sp.w0 * std::pow(sp.h, 3) / 3 * (1 - 0.63 * sp.h / sp.w0), Ip = std::pow(sp.w0, 3) * sp.h / 12;
        const double t1 = std::sqrt(sp.G * J / (sp.rho * Ip)) / (4 * ell);
        QVERIFY2(std::fabs(twist[0] / t1 - 1) < 0.03, qPrintable(QStringLiteral("%1 vs %2").arg(twist[0]).arg(t1)));
        QVERIFY2(std::fabs(twist[1] / twist[0] / 3 - 1) < 0.03, qPrintable(QString::number(twist[1] / twist[0])));
        // The dax half way along leaves half the tongue sounding: four
        // times the pitch, near enough (the part behind the dax lets it
        // rock a little, a few per cent flat of a rigid clamp).
        int half = 0;
        while (half + 1 < dax::kU && d.e[half].u < 0.5f)
            ++half;
        const double ratio = d.e[half].hz[0] / e.hz[0] * std::pow((1.0 - d.e[half].u) / (1.0 - e.u), 2.0);
        QVERIFY2(ratio > 0.93 && ratio < 1.01, qPrintable(QString::number(ratio)));
    }

    void thePartBehindTheDaxIsFollowedInFull() {
        // Every mode of the part behind the dax under 4 kHz, solved afresh
        // at each of the 64 dax positions, is one the tongue follows.
        const dax::Design &d = dax::design();
        const dax::Spec &sp = d.spec;
        for (int j = 0; j < dax::kU; ++j) {
            const double u = dax::Design::uAt(j), wd = sp.width(u) * sp.w0 / sp.L;
            dax::detail::End clamp, held;
            clamp.cl[0] = clamp.cl[1] = clamp.cl[2] = true;
            held.k[0] = sp.Kc, held.k[1] = sp.Kr, held.k[2] = sp.Kc * wd * wd / 12.0;
            const auto m = dax::detail::solvePart(sp, 0.0, u, 14, clamp, held, 40);
            for (int k = 0; k < m.n; ++k) {
                const double hz = std::sqrt(m.lam[size_t(k)]) * d.omega / (2 * M_PI);
                if (hz > 4000)
                    continue;
                bool found = false;
                for (int f = 0; f < dax::kBack; ++f)
                    found |= std::fabs(d.e[j].bhz[f] / hz - 1) < 0.005;
                QVERIFY2(found, qPrintable(QStringLiteral("u %1: %2 Hz").arg(u).arg(hz)));
            }
        }
    }

    void theOutlineVoicesTheTongue() {
        // Reichel's tongues are sawn to an outline, not straight: the
        // reference outline's waist and bulges move its upper modes off
        // the straight board's ratios, and its off-centre mass couples
        // twisting into the swing (a twisting family moves the tip).
        const dax::Design &d = dax::design();
        const dax::Design straight = dax::build(uniform());
        int moved = 0;
        for (int k = 1; k < 6; ++k)
            moved += std::fabs(d.e[20].hz[k] / d.e[20].hz[0] / (straight.e[20].hz[k] / straight.e[20].hz[0]) - 1) > 0.03f;
        QVERIFY2(moved >= 3, qPrintable(QString::number(moved)));
        float coupled = 0;
        for (int k = 0; k < dax::kFront; ++k)
            if (std::fabs(d.e[20].tipP[k]) > 20)
                coupled = std::max(coupled, std::fabs(d.e[20].tipW[k]));
        QVERIFY2(coupled > 0.3f, qPrintable(QString::number(coupled)));
    }

    void theDaxSetsThePitch() {
        // The dax slid out along the tongue shortens what sounds: the bowed
        // pitch is the bowed part's fundamental where the dax stands (as
        // the design tabulates it), rising all the way, from a growl under
        // 40 Hz to past 1.5 kHz over the dax's travel.
        dax::Tongue probe;
        probe.init(kRate);
        QVERIFY(probe.hzAt(float(dax::kUMin)) < 40.0f);
        QVERIFY(probe.hzAt(0.89f) > 1500.0f);
        for (int j = 1; j < dax::kU; ++j)
            QVERIFY(dax::design().e[j].hz[0] > dax::design().e[j - 1].hz[0]);
        double last = 0;
        for (float u : {0.45f, 0.55f, 0.62f, 0.7f, 0.76f, 0.82f}) {
            const float hz = probe.hzAt(u);
            // (A firm dax, which hardly rocks: the pitch is the mode's.)
            const auto x = bow(0.7, midForce(hz), 0.2f, 0.2f, [&](dax::Tongue &t, double) { t.setDax(u); }, 1.0f);
            double clarity = 0;
            const double got = pitchOf(x.mono, 0.45, 0.2, &clarity);
            // (Bowed, a tone sits under the mode it is drawn from, by up to
            // most of a semitone; no ear here to bring it in.)
            QVERIFY2(std::fabs(cents(got, hz)) < 100 && clarity > 0.9,
                     qPrintable(QStringLiteral("u %1: %2 Hz for %3 (%4)").arg(u).arg(got).arg(hz).arg(clarity)));
            QVERIFY(got > last);
            last = got;
        }
    }

    void theEarBringsTheNoteIn() {
        // Asked for a pitch, the dax goes there and the player's ear
        // corrects the bowed tone's flattening: on the note within a few
        // cents, across the Lead's range.
        for (float hz : {110.0f, 220.0f, 330.0f, 440.0f, 660.0f, 880.0f, 1320.0f}) {
            // (Bowed 0.3 across, 0.3 along: inside the window's four touches.)
            const auto x = bow(0.9, ordinaryForce(hz), 0.2f, 0.3f, [&](dax::Tongue &t, double) { t.setPitch(hz); }, kPress, 0.3f);
            const double got = pitchOf(x.mono, 0.6, 0.25);
            QVERIFY2(std::fabs(cents(got, hz)) < 8, qPrintable(QStringLiteral("%1: %2").arg(hz).arg(got)));
        }
    }

    void theEarDoesNotSwingPast() {
        // Notes played one after another, each from a fresh bow. The first
        // time at a place on the tongue, the ear has to find it: within 40
        // cents from 160 ms, 15 from 320 ms, never swinging past. Played
        // again, the place is known: within 25 cents from 160 ms (for its
        // first tenth of a second or so a note stands sharp of where it
        // settles, by up to 30 cents, before the dax's rocking has built up
        // the flattening the ear corrects for).
        dax::Tongue t;
        t.init(kRate);
        t.setDaxPress(kPress);
        const float notes[] = {220.0f, 330.0f, 659.0f, 440.0f, 220.0f, 330.0f, 659.0f};
        for (int i = 0; i < 7; ++i) {
            const float hz = notes[i];
            const bool known = i >= 4;
            t.silence();
            std::vector<float> x;
            float l[32], r[32], drawn = 0, land = 0;
            for (int b = 0; b < int(0.6 * kRate) / 32; ++b) {
                drawn += (1.0f - drawn) * (1.0f - std::exp(-32.0f / kRate / 0.03f));
                land += (1.0f - land) * (1.0f - std::exp(-32.0f / kRate / 0.002f));
                t.setPitch(hz);
                t.setBow(ordinaryForce(hz) * land, 0.2f * drawn, 0.3f, 0.5f);
                std::fill(l, l + 32, 0.0f);
                t.render(l, r, 32, 1.0f);
                x.insert(x.end(), l, l + 32);
            }
            for (double at = 0.16; at < 0.56; at += 0.04) {
                const double c = cents(peakNear(x, at, 0.04, hz, 150), hz);
                const double allowed = known ? 25 : at >= 0.32 ? 15 : 40;
                QVERIFY2(std::fabs(c) < allowed, qPrintable(QStringLiteral("%1 (%2) at %3 s: %4 c").arg(hz).arg(known).arg(at).arg(c)));
            }
        }
    }

    void everyNoteSingsAtEveryTouch() {
        // Every semitone from A2 to E6, bowed with the ordinary force, at
        // each of the touches the Lead gives (the dax pressed ordinarily
        // and as lightly as a flat finger presses it; bowed on the very end
        // and an eighth in): its own note, clear, never dead.
        for (int midi = 45; midi <= 88; ++midi) {
            const float hz = float(440.0 * std::exp2((midi - 69) / 12.0));
            for (int touch = 0; touch < 4; ++touch) {
                const float press = touch < 2 ? kPress : 0.0f, along = touch % 2 ? 1.0f : 0.0f;
                const auto x = bow(0.75, ordinaryForce(hz), 0.2f, 0.3f, [&](dax::Tongue &t, double) { t.setPitch(hz); }, press, along);
                double c = 0;
                const double p = pitchOf(x.mono, 0.5, 0.2, &c);
                const bool own = std::fabs(cents(p, hz)) < 15 || (std::fabs(cents(p, 2 * hz)) < 15 && std::fabs(cents(peakNear(x.mono, 0.5, 0.25, hz), hz)) < 15);
                QVERIFY2(own && c > 0.9 && db(rms(x.mono, 0.5, 0.75)) > -75,
                         qPrintable(QStringLiteral("MIDI %1, touch %2: %3 Hz (%4), %5 dB").arg(midi).arg(touch).arg(p).arg(c).arg(db(rms(x.mono, 0.5, 0.75)))));
            }
        }
    }

    void aGlideNeverClicks() {
        // Slid an octave up in a third of a second and back, then rocked
        // a semitone either way six times a second: the pitch follows the
        // dax all the way, and the top of the band never jumps out of its
        // run (the modes are retuned sample by sample, their swing carried
        // over), no more than on a note held still.
        const auto glide = bow(2.0, ordinaryForce(330.0f), 0.2f, 0.2f, [](dax::Tongue &t, double s) {
            double semis = 0;
            if (s > 0.5 && s < 0.83)
                semis = 12 * (s - 0.5) / 0.33;
            else if (s >= 0.83 && s < 1.0)
                semis = 12;
            else if (s >= 1.0 && s < 1.33)
                semis = 12 * (1 - (s - 1.0) / 0.33);
            else if (s >= 1.4)
                semis = std::sin(2 * M_PI * 6 * (s - 1.4));
            t.setPitch(float(220.0 * std::exp2(semis / 12)));
        });
        const auto held = bow(2.0, ordinaryForce(330.0f), 0.2f, 0.2f, [](dax::Tongue &t, double) { t.setPitch(220.0f); });
        const double still = clickiness(held.mono, 0.3, 2.0), moving = clickiness(glide.mono, 0.3, 2.0);
        QVERIFY2(moving < still + 4.0, qPrintable(QStringLiteral("%1 %2").arg(moving).arg(still)));
        // (And what is measured would hear a click: the same glide with a
        // step of a tenth of its swing in it stands right out.)
        auto stepped = glide.mono;
        for (size_t i = size_t(0.7 * kRate); i < stepped.size(); ++i)
            stepped[i] += 0.3f * float(rms(glide.mono, 0.6, 0.7)) * 1.41f;
        QVERIFY2(clickiness(stepped, 0.3, 2.0) > moving + 8, qPrintable(QStringLiteral("%1 %2").arg(clickiness(stepped, 0.3, 2.0)).arg(moving)));
        // On its way: past the fifth half way up, at the octave on top.
        const double mid = pitchOf(glide.mono, 0.655, 0.03), top = pitchOf(glide.mono, 0.88, 0.1);
        QVERIFY2(cents(mid, 220) > 450 && cents(mid, 220) < 750, qPrintable(QString::number(mid)));
        QVERIFY2(std::fabs(cents(top, 440)) < 20, qPrintable(QString::number(top)));
    }

    void theBowsForceIsTheRegime() {
        // Schelleng's window, as on a bowed string. Under its floor the bow
        // can't hold the tongue: it skates over it and the tongue barely
        // speaks. Inside it the note sings, rougher the harder it is
        // pressed. Over its roof the hair holds on too long: the tone
        // croaks, its period doubling, or falls flat and raucous.
        auto play = [&](float hz, float force) {
            return bow(0.9, force, 0.2f, 0.3f, [&](dax::Tongue &t, double) { t.setPitch(hz); }, kPress, 0.3f);
        };
        for (float hz : {220.0f, 440.0f, 660.0f}) {
            const auto ordinary = play(hz, ordinaryForce(hz)), skating = play(hz, ordinaryForce(hz) / 20);
            double c = 0;
            const double sung = pitchOf(ordinary.mono, 0.55, 0.25, &c);
            QVERIFY2(std::fabs(cents(sung, hz)) < 10 && c > 0.9, qPrintable(QStringLiteral("%1: %2 (%3)").arg(hz).arg(sung).arg(c)));
            QVERIFY2(noiseRatio(ordinary.mono, 0.55, 0.3, hz) < -40, qPrintable(QString::number(noiseRatio(ordinary.mono, 0.55, 0.3, hz))));
            QVERIFY2(db(rms(skating.mono, 0.55, 0.85)) < db(rms(ordinary.mono, 0.55, 0.85)) - 25,
                     qPrintable(QStringLiteral("%1: %2 %3").arg(hz).arg(db(rms(skating.mono, 0.55, 0.85))).arg(db(rms(ordinary.mono, 0.55, 0.85)))));
        }
        // Rougher as it presses: from a light bow (half the window's
        // middle) to one just under its roof, still inside it, the
        // harmonics come up, on average over the neck by more than 6 dB.
        double gain = 0;
        for (float hz : {110.0f, 220.0f, 440.0f, 660.0f}) {
            const double r0 = richness(play(hz, 0.5f * midForce(hz)).mono, 0.55, 0.3, hz),
                         r2 = richness(play(hz, 0.95f * roofForce(hz)).mono, 0.55, 0.3, hz);
            gain += (r2 - r0) / 4;
        }
        QVERIFY2(gain > 6, qPrintable(QString::number(gain)));
        // Over the roof, at the hardest press the Lead gives (2.5 times the
        // roof), from a fresh attack, at A3, E4 and E5: the note is lost,
        // the tone falling from 2.5 to 6 semitones under it (whether what is
        // left is rough or clean turns on the attack's last detail, and
        // differs from one compiler's rounding to another's; where it lands
        // doesn't).
        for (float hz : {220.0f, 330.0f, 660.0f}) {
            const auto croak = play(hz, 2.5f * roofForce(hz));
            const double own = pitchOf(croak.mono, 0.55, 0.25);
            QVERIFY2(cents(own, hz) < -200, qPrintable(QStringLiteral("%1: %2 Hz").arg(hz).arg(own)));
        }
        // And the shorter, stiffer tongue of a high note needs a firmer
        // bow before it sings at all: the window's floor climbs with pitch.
        auto floorOf = [&](float note) {
            const double full = rms(play(note, ordinaryForce(note)).mono, 0.5, 0.65);
            for (float f = 0.005f; f < 5.0f; f *= 1.25f) {
                const auto x = bow(0.65, f, 0.2f, 0.3f, [&](dax::Tongue &t, double) { t.setPitch(note); }, kPress, 0.3f);
                double cl = 0;
                const double q = pitchOf(x.mono, 0.45, 0.15, &cl);
                if (std::fabs(cents(q, note)) < 20 && cl > 0.95 && rms(x.mono, 0.5, 0.65) > 0.3 * full)
                    return f;
            }
            return 99.0f;
        };
        const float low = floorOf(220.0f), high = floorOf(880.0f);
        QVERIFY2(high > 2.0f * low, qPrintable(QStringLiteral("%1 %2").arg(low).arg(high)));
    }

    void theDaxRocks() {
        // The dax holds only what its press can: it rocks on its curved
        // face under an ordinary bow, softening the tongue's support at the
        // peak of each swing. Without the ear the note falls flat and its
        // harmonics come up, against a dax no bow could rock: pressed as
        // lightly as a flat finger presses it, 66–149 c and 6–22 dB
        // (measured over E3–A5); at an ordinary press, 28–89 c and 1–10 dB.
        dax::Spec rigid;
        rigid.pressLight = rigid.pressFirm = 1e7;
        const dax::Design still = dax::build(rigid);
        for (float press : {0.0f, kPress}) {
            double richer = 0;
            for (float hz : {165.0f, 220.0f, 330.0f, 440.0f, 660.0f, 880.0f}) {
                auto play = [&](const dax::Design *d) {
                    return bow(0.9, ordinaryForce(hz), 0.2f, 0.3f, [&](dax::Tongue &t, double) { t.setDax(t.daxFor(hz)); }, press, 0.5f, d);
                };
                const auto rocking = play(nullptr), held = play(&still);
                const double a = pitchOf(rocking.mono, 0.55, 0.25), b = pitchOf(held.mono, 0.55, 0.25);
                QVERIFY2(cents(a, b) < (press > 0 ? -20 : -50), qPrintable(QStringLiteral("%1 %2: %3 c").arg(press).arg(hz).arg(cents(a, b))));
                richer += (richness(rocking.mono, 0.55, 0.3, a) - richness(held.mono, 0.55, 0.3, b)) / 6;
            }
            QVERIFY2(richer > (press > 0 ? 3 : 10), qPrintable(QStringLiteral("%1: %2 dB").arg(press).arg(richer)));
        }
    }

    void theDaxDampsTheTongue() {
        // Let go, the tongue rings out on the dax, briefly: down 40 dB
        // within a second. A lighter press lets the cardboard lose more,
        // so it dies sooner.
        auto ring = [&](float press) {
            dax::Tongue t;
            t.init(kRate);
            t.setDaxPress(press);
            std::vector<float> x;
            float l[32], r[32];
            for (int b = 0; b < int(2.0 * kRate) / 32; ++b) {
                const bool bowing = b < int(0.5 * kRate) / 32;
                t.setPitch(330.0f);
                t.setBow(bowing ? ordinaryForce(330.0f) : 0.0f, bowing ? 0.2f : 0.0f, 0.2f);
                std::fill(l, l + 32, 0.0f);
                std::fill(r, r + 32, 0.0f);
                t.render(l, r, 32, 1.0f);
                for (int i = 0; i < 32; ++i)
                    x.push_back(l[i] + r[i]);
            }
            return x;
        };
        const auto firm = ring(1.0f), light = ring(0.0f);
        const double at = db(rms(firm, 0.4, 0.5));
        QVERIFY2(db(rms(firm, 1.45, 1.5)) < at - 40, qPrintable(QString::number(db(rms(firm, 1.45, 1.5)) - at)));
        QVERIFY2(db(rms(firm, 0.53, 0.56)) > at - 30, "it rings a little before it dies");
        QVERIFY2(db(rms(light, 0.8, 0.9)) < db(rms(firm, 0.8, 0.9)) - 3,
                 qPrintable(QStringLiteral("%1 %2").arg(db(rms(light, 0.8, 0.9))).arg(db(rms(firm, 0.8, 0.9)))));
    }

    void whereItIsBowedVoicesIt() {
        // Bowed on the very end every mode swings most under the hair; an
        // eighth of the bowed part in along the edge the upper modes swing
        // less, and the tone is rounder: a few decibels, most low on the
        // tongue (5 dB at A2), and not everywhere (at E5 the edge is the rougher).
        double sum = 0;
        int n = 0;
        for (float hz : {82.0f, 110.0f, 165.0f, 220.0f, 330.0f, 440.0f}) {
            auto at = [&](float along) {
                return bow(0.9, ordinaryForce(hz), 0.2f, 0.3f, [&](dax::Tongue &t, double) { t.setPitch(hz); }, kPress, along);
            };
            const double d = richness(at(0.0f).mono, 0.55, 0.3, hz) - richness(at(1.0f).mono, 0.55, 0.3, hz);
            if (hz == 110.0f)
                QVERIFY2(d > 3, qPrintable(QString::number(d)));
            sum += d;
            ++n;
        }
        QVERIFY2(sum / n > 1.5, qPrintable(QString::number(sum / n)));
    }

    void nothingRunsAway() {
        // Ten seconds of abuse: the dax thrown about, the bow slammed on and
        // off at any force and speed, either way, anywhere across the end,
        // the dax pressed anyhow. Every sample finite; the tongue never
        // holds more energy than the bow could have done work on it (μs·F·v
        // over the time), each block's worth carried up as the dax retunes
        // the swing (its action, energy over frequency, is kept); and quiet
        // once the bow lifts.
        dax::Tongue t;
        t.init(kRate);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        float l[32], r[32], force = 0, speed = 0;
        double bound = 1e-6, worst = 0, hzWas = 0;
        bool finite = true;
        for (int b = 0; b < int(10.0 * kRate) / 32; ++b) {
            if (b % 20 == 0) {
                t.setPitch(20.0f * std::pow(200.0f, u(rng)));
                force = 8.0f * u(rng) * u(rng);
                speed = 1.5f * (u(rng) - 0.5f);
                t.setBow(force, speed, 2 * u(rng) - 1);
                t.setDaxPress(u(rng));
            }
            if (b % 37 == 0)
                t.setDax(u(rng));
            std::fill(l, l + 32, 0.0f);
            std::fill(r, r + 32, 0.0f);
            t.render(l, r, 32, 1.0f);
            for (int i = 0; i < 32; ++i)
                finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
            // What the bow could have done, carried as the swing is when the
            // dax retunes it (energy over frequency), every block.
            const double hz = t.hzAt(t.dax());
            if (hzWas > 0)
                bound *= hz / hzWas;
            hzWas = hz;
            bound += dax::Tongue::kMuS * force * std::fabs(speed) * 32.0 / kRate;
            worst = std::max(worst, double(t.energy()) / bound);
        }
        QVERIFY(finite);
        QVERIFY2(worst < 1.0, qPrintable(QString::number(worst)));
        t.setBow(0, 0, 0);
        for (int b = 0; b < int(3.0 * kRate) / 32; ++b) {
            std::fill(l, l + 32, 0.0f);
            std::fill(r, r + 32, 0.0f);
            t.render(l, r, 32, 1.0f);
        }
        QVERIFY2(t.energy() < 1e-9f, qPrintable(QString::number(double(t.energy()))));
        QVERIFY(std::isfinite(l[31]) && std::fabs(l[31]) < 1e-4f);
    }

    // --- the Lead's daxophone ---

    void theFingerIsTheDax() {
        // A finger resting between two cells sings between them (the dax
        // glides wherever it is put), and follows a slide all the way;
        // with the line following the chord (§) it rests on the degree,
        // as every other lead does.
        const std::string between = fmt("down 0.3 1 %.4f 0.5 p=0.5\nup 1.3 1\nend 1.5\n", cellX(7.4));
        const auto free = mono(testkit::play(onDax(between)));
        const auto snapped = mono(testkit::play(onDax("key 0.29 §\n" + between)));
        const double f = pitchOf(free, 0.8, 0.3), s = pitchOf(snapped, 0.8, 0.3);
        // A4 to B4 is two semitones in A minor: 0.4 of a cell is 80 cents.
        QVERIFY2(cents(f, 440) > 60 && cents(f, 440) < 100, qPrintable(QString::number(f)));
        QVERIFY2(std::fabs(cents(s, 440)) < 15, qPrintable(QString::number(s)));
        // A slide from A3 up to A4: the pitch rises all the way, no steps.
        const auto slide = testkit::play(onDax(fmt("down 0.3 1 %.4f 0.5 p=0.5\nglide 0.7 1.3 1 %.4f 0.5\nup 1.8 1\nend 2.0\n", cellX(0), cellX(7))));
        const auto m = mono(slide);
        double last = 0;
        for (double t = 0.72; t < 1.28; t += 0.07) {
            const double p = pitchOf(m, t, 0.03);
            QVERIFY2(p > last, qPrintable(QStringLiteral("%1: %2 after %3").arg(t).arg(p).arg(last)));
            last = p;
        }
        QVERIFY(slide.finite());
        QVERIFY2(clickiness(m, 0.4, 1.7) < 14, qPrintable(QString::number(clickiness(m, 0.4, 1.7))));
    }

    void thePressIsTheBow() {
        // On the Lead, a light touch is a quieter, purer tone, a heavy one
        // louder and rougher; both sing their note.
        auto at = [&](double p) {
            return mono(testkit::play(onDax(fmt("down 0.3 1 %.4f 0.5 p=%.2f\nup 1.3 1\nend 1.5\n", cellX(0), p))));
        };
        const auto soft = at(0.3), hard = at(0.75);
        for (const auto *x : {&soft, &hard}) {
            const double f = peakNear(*x, 0.8, 0.3, 220);
            QVERIFY2(std::fabs(cents(f, 220)) < 15, qPrintable(QString::number(f)));
            // (Its second harmonic may stand over it, as a voice's does;
            // the fundamental is there.)
            QVERIFY2(levelAt(*x, 0.8, 0.3, f) > levelAt(*x, 0.8, 0.3, 440) - 25, "the note's own, not an octave's");
        }
        QVERIFY2(richness(hard, 0.8, 0.3, 220) > richness(soft, 0.8, 0.3, 220) + 6,
                 qPrintable(QStringLiteral("%1 %2").arg(richness(hard, 0.8, 0.3, 220)).arg(richness(soft, 0.8, 0.3, 220))));
        QVERIFY(rms(hard, 0.8, 1.2) > rms(soft, 0.8, 1.2));
    }

    void aTongueStaysInsideBudget() {
        // One tongue, bowed and gliding, against the clock: under 1 % of
        // real time (about 0.47 % on a quiet core; twice that leaves room
        // for a busy machine).
        TENFOLD_TIMED();
        dax::Tongue t;
        t.init(kRate);
        float l[32], r[32];
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < int(4.0 * kRate) / 32; ++b) {
            t.setPitch(220.0f * std::exp2(std::sin(float(b) * 0.003f)));
            t.setBow(0.6f, 0.2f, 0.2f);
            t.render(l, r, 32, 1.0f);
        }
        const double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        QVERIFY2(share < 0.01, qPrintable(QString::number(share)));
    }

    void aDaxophoneLineStaysInsideBudget() {
        // A daxophone line through the whole studio with three harmony
        // voices (four tongues bowed at once) and a run of notes left to
        // ring: the studio keeps its time.
        TENFOLD_TIMED();
        std::string s = onDax("");
        s += "down 0.3 11 0.08 0.95 p=0.8\ndown 0.3 12 0.15 0.83 p=0.8\ndown 0.3 13 0.08 0.71 p=0.8\n";
        for (int i = 0; i < 12; ++i)
            s += fmt("down %.2f %d %.4f 0.6 p=0.7\nup %.2f %d\n", 0.5 + 0.3 * i, i + 1, cellX((i * 3) % 15), 0.75 + 0.3 * i, i + 1);
        s += "up 4.5 11\nup 4.5 12\nup 4.5 13\nend 5\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.2, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(DaxophoneTests)
#include "daxophone_tests.moc"
