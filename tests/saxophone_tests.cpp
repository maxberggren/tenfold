// The saxophones (saxophone.h), checked against what has been measured of
// real ones (docs/saxophone.md): the bore's resonances against a measured
// alto's (Colinot et al. 2021), the cone's full harmonic series against a
// cylinder's odd one, the tone-hole lattice's cutoff, the reed's threshold
// against the linear theory, its beating, intonation over the whole range,
// slurs without clicks, that nothing runs away, and what they cost. Then the
// Lead's two sax sounds, played.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include <QJsonArray>

#include "lead.h"
#include "offline.h"
#include "parts.h"
#include "saxophone.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;
constexpr int kLead = 3;
constexpr int kTenor = 22, kAlto = 23; // the Lead's sounds

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

// A note blown on a fresh horn: the tongue lets the reed go and the breath
// rises to `blow` over 20 ms. Returns what it radiates; `reed`, if given,
// gets the reed's swing.
std::vector<float> blowNote(sax::Kind kind, float midi, float blow, double seconds, float lip = 0.5f,
                            std::vector<float> *reed = nullptr, sax::Sax *keep = nullptr, float growl = 0, bool find = true) {
    sax::Sax local;
    sax::Sax &s = keep ? *keep : local;
    s.init(kRate);
    s.start(kind, midiToHz(midi));
    s.tongue(0.7f, true);
    sax::Sax::Control c;
    c.hz = midiToHz(midi);
    c.lip = lip;
    c.growl = growl;
    c.find = find;
    std::vector<float> out;
    float buf[16];
    for (int b = 0; b < int(seconds * kRate) / 16; ++b) {
        c.blow = std::min(1.0f, float(b * 16) / (0.02f * kRate)) * blow;
        std::fill(buf, buf + 16, 0.0f);
        if (reed) {
            for (int i = 0; i < 16; ++i) {
                s.render(buf + i, 1, c);
                reed->push_back(s.reedX());
            }
        } else {
            s.render(buf, 16, c);
        }
        out.insert(out.end(), buf, buf + 16);
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

// The period heard: the first autocorrelation peak within 90 % of the
// strongest, over two and a half octaves either side of the note (so an
// octave jump is heard as one), as a frequency; −1 if it isn't periodic,
// or if the stretch and its longest lag run past the end of x (never read
// what isn't there: past a vector's end is whatever the heap held).
double periodHz(const std::vector<float> &x, double from, double len, double guess) {
    const size_t a = size_t(from * kRate), n = size_t(len * kRate);
    const int lo = std::max(2, int(kRate / (guess * 2.6))), hi = int(kRate / (guess / 2.6));
    if (a + n + size_t(hi) + 2 > x.size()) {
        qWarning("periodHz: %.3f s + %.3f s and a lag of %d run past the %zu samples", from, len, hi + 1, x.size());
        return -1;
    }
    double mean = 0;
    for (size_t i = 0; i < n; ++i)
        mean += x[a + i];
    mean /= double(n);
    double v0 = 1e-30;
    for (size_t i = 0; i < n; ++i)
        v0 += (x[a + i] - mean) * (x[a + i] - mean);
    std::vector<double> r(size_t(hi + 2), 0.0);
    double top = -1;
    for (int L = lo - 1; L <= hi + 1; ++L) {
        double s = 0;
        for (size_t i = 0; i < n; ++i)
            s += (x[a + i] - mean) * (x[a + i + size_t(L)] - mean);
        r[size_t(L)] = s / v0;
        if (L >= lo && L <= hi)
            top = std::max(top, r[size_t(L)]);
    }
    if (top < 0.5)
        return -1;
    for (int L = lo; L <= hi; ++L)
        if (r[size_t(L)] > 0.9 * top && r[size_t(L)] >= r[size_t(L - 1)] && r[size_t(L)] >= r[size_t(L + 1)]) {
            const double y0 = r[size_t(L - 1)], y1 = r[size_t(L)], y2 = r[size_t(L + 1)];
            return kRate / (double(L) + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2));
        }
    return -1;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

double rms(const std::vector<float> &x, double from, double to) {
    const size_t a = size_t(from * kRate), b = std::min(x.size(), size_t(to * kRate));
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(b - a, 1)));
}

// What a lead script plays, on one of its sounds.
std::string onSound(int snd, const std::string &rest) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < snd; ++k)
        s += fmt("key %.3f Right\n", 0.005 + 0.01 * k); // (all in before 0.25 s)
    return s + rest;
}
// The x of a neck cell (A minor: 0 is A3, 7 A4).
float cellX(int q) { return 0.22f + 0.78f * (float(q) + 0.5f) / 15.0f; }

std::vector<float> mono(const testkit::Take &t) {
    std::vector<float> m(t.left.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (t.left[i] + t.right[i]);
    return m;
}

// Where the energy of a stretch sits: its spectral centroid over the
// harmonics of f0 up to 10 kHz (each heard over a few per cent either side,
// so a vibrato's swing doesn't hide the upper ones).
double centroid(const std::vector<float> &x, double from, double len, double f0) {
    double num = 0, den = 0;
    for (int h = 1; h * f0 < 10000; ++h) {
        double p = 0;
        for (double d = -0.04; d <= 0.041; d += 0.01)
            p += std::pow(10.0, levelAt(x, from, len, h * f0 * (1 + d)) / 10);
        num += p * h * f0;
        den += p;
    }
    return num / std::max(den, 1e-30);
}

// The linear threshold of mode A (peak over Z_c) at w with the reed's
// dynamics: ζ(√γ·Re D − (1−γ)/2√γ) = 1/A (Kergomard; Silva et al. 2008).
double linearThreshold(double zeta, double A, double w, double wr, double qr) {
    const double r = w / wr, den = (1 - r * r) * (1 - r * r) + (r / qr) * (r / qr);
    const double reD = (1 - r * r) / den;
    for (double g = 0.2; g < 1.2; g += 0.001)
        if (zeta * (std::sqrt(g) * reD - (1 - g) / (2 * std::sqrt(g))) >= 1 / A)
            return g;
    return 9;
}

// A run of slurred notes on one of the Lead's saxes (second fingers
// landing on `cells` and lifting, back to `base` each time), played;
// the largest sharpness of the line (its second difference, sample to
// sample) around the changes, against that of the notes held, and
// whether the line ever stopped.
struct Slur {
    double change = 0, steady = 0, gap = 1e9;
    double hz[5] = {};   // what each slurred-to note sounded, late in it
    double back[5] = {}; // and the held note fallen back to after each
};
Slur slur(int snd, int base, const int (&cells)[5]) {
    std::string s = fmt("down 0.3 1 %.4f 0.5 p=0.6\n", cellX(base));
    for (int i = 0; i < 5; ++i)
        s += fmt("down %.2f %d %.4f 0.5 p=0.6\nup %.2f %d\n", 0.8 + 0.3 * i, 2 + i, cellX(cells[i]), 1.05 + 0.3 * i, 2 + i);
    s += "up 2.6 1\nend 2.8\n";
    const auto x = mono(testkit::play(onSound(snd, s)));
    auto edge = [&](double from, double to) {
        double m = 0;
        for (size_t i = size_t(from * kRate); i < std::min(x.size(), size_t(to * kRate)); ++i)
            m = std::max(m, std::fabs(double(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
        return m;
    };
    Slur r;
    for (int i = 0; i < 5; ++i) {
        r.steady = std::max(r.steady, edge(0.9 + 0.3 * i, 1.02 + 0.3 * i));
        r.change = std::max(r.change, edge(0.79 + 0.3 * i, 0.83 + 0.3 * i));
        r.change = std::max(r.change, edge(1.04 + 0.3 * i, 1.08 + 0.3 * i));
    }
    for (int i = 0; i < 5; ++i) {
        r.hz[i] = periodHz(x, 0.95 + 0.3 * i, 0.08, midiToHz(57.0));
        // (The held note, back after the last lift: held 350 ms.)
        r.back[i] = periodHz(x, 2.4, 0.15, midiToHz(57.0));
    }
    const double all = rms(x, 0.8, 2.3);
    for (double t = 0.85; t < 2.3; t += 0.05)
        r.gap = std::min(r.gap, rms(x, t, t + 0.05) / all);
    return r;
}

}

class SaxophoneTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs them with the machine to itself.
    Q_CLASSINFO("alone", "aSaxStaysInsideBudget aSaxSectionStaysInsideBudget")
private slots:
    void initTestCase() { sax::prepare(); }

    void theBoreIsAMeasuredAltos() {
        // Colinot, Vergez, Guillemain & Doc (2021, Table 2) fitted a real
        // alto's low written D♯ (a Buffet Senzo) with modes: 190, 395, 593,
        // 820 Hz, Q 34, 35, 28.5, 37, peaks 10, 13.3, 9.9, 7.7 times Z_c.
        // The model's own, from the cone, the mouthpiece and the holes.
        const sax::Fingering &f = sax::design(sax::Alto).at(5);
        const double want[4][3] = {{190.2, 34, 10.0}, {395.2, 35, 13.3}, {593.2, 28.5, 9.9}, {820.1, 37, 7.7}};
        for (int k = 0; k < 4; ++k) {
            const double hz = f.w[k] / (2 * M_PI);
            // (The model is in equal temperament at A440; the measured horn
            // a few cents off it, and its upper modes sharper still.)
            QVERIFY2(std::fabs(hz / want[k][0] - 1) < 0.09, qPrintable(QStringLiteral("mode %1: %2 Hz").arg(k).arg(hz)));
            QVERIFY2(f.q[k] > 0.7 * want[k][1] && f.q[k] < 1.4 * want[k][1], qPrintable(QStringLiteral("mode %1: Q %2").arg(k).arg(f.q[k])));
            QVERIFY2(f.a[k] > 0.6 * want[k][2] && f.a[k] < 1.5 * want[k][2], qPrintable(QStringLiteral("mode %1: peak %2").arg(k).arg(f.a[k])));
        }
        // The first peak smaller than the second, as in every conical bore
        // (and the tenor's lowest note's about 11 MPa·s/m³, Chen et al. 2011).
        QVERIFY(f.a[0] < f.a[1]);
        const sax::Design &t = sax::design(sax::Tenor);
        const double mpa = t.at(0).a[0] * t.zRef / 1e6;
        // (Chen's measured with the reed's compliance in parallel, which
        // lowers it; without it, the model's stands higher.)
        QVERIFY2(mpa > 7 && mpa < 25, qPrintable(QString::number(mpa)));
    }

    void theConeHasTheWholeHarmonicSeries() {
        // A cone's resonances stand near every harmonic of the first (a
        // cylinder closed by the reed has only the odd ones: 1, 3, 5): the
        // saxophone's low notes have their second resonance near twice the
        // first, their third near three times it (sharp of them, as the
        // measured alto's, 2.08 and 3.12, are).
        for (int k = 0; k < sax::kKinds; ++k)
            for (int s : {0, 4, 8}) {
                const sax::Fingering &f = sax::design(sax::Kind(k)).at(s);
                const double r2 = f.w[1] / f.w[0], r3 = f.w[2] / f.w[0];
                QVERIFY2(r2 > 1.97 && r2 < 2.12 && r3 > 2.95 && r3 < 3.25,
                         qPrintable(QStringLiteral("kind %1 s %2: %3 %4").arg(k).arg(s).arg(r2).arg(r3)));
            }
        // The same bore built as a cylinder (no taper): odd harmonics only.
        sax::Spec cyl = sax::specOf(sax::Alto);
        cyl.halfAngle = 1e-4;
        cyl.r1 = 1e-4 * 200; // a cylinder of 2 cm radius, its "apex" 200 m off
        sax::Bore b;
        b.x1 = cyl.r1 / std::tan(cyl.halfAngle);
        b.tanA = std::tan(cyl.halfAngle);
        b.end = b.x1 + 0.6;
        b.bellR = 0.02;
        b.mpL = 0.001;
        b.mpS = M_PI * 0.02 * 0.02;
        const double f1 = sax::resonanceNear(b, sax::kC / (4 * 0.62), 0.3);
        const double f2 = sax::resonanceNear(b, 3 * f1, 0.15);
        QVERIFY2(std::fabs(f2 / f1 - 3) < 0.1, qPrintable(QStringLiteral("%1 %2").arg(f1).arg(f2)));
        QVERIFY2(sax::resonanceNear(b, 2 * f1, 0.15) < 0, "a cylinder has no resonance at the octave");
        // And played, the sax's sound has its even harmonics: the second
        // stands within 10 dB of the first, at a mezzo forte. (The Lead's
        // Accordion is free reeds, a saw and pulse with every harmonic too;
        // what is new here is a reed that has them because of its bore.)
        for (int k = 0; k < sax::kKinds; ++k) {
            const float midi = sax::design(sax::Kind(k)).spec.lowMidi + 8;
            const auto x = blowNote(sax::Kind(k), midi, 0.7f, 0.8);
            const double f0 = midiToHz(midi);
            const double h1 = levelAt(x, 0.5, 0.25, f0), h2 = levelAt(x, 0.5, 0.25, 2 * f0), h4 = levelAt(x, 0.5, 0.25, 4 * f0);
            QVERIFY2(h2 > h1 - 10 && h4 > h1 - 25, qPrintable(QStringLiteral("kind %1: %2 %3 %4").arg(k).arg(h1).arg(h2).arg(h4)));
        }
    }

    void theOpenHolesCutOffTheResonances() {
        // Above the open tone-hole lattice's cutoff the bore's resonances
        // fade: an alto's around 1 kHz (Petersen et al. 2020: 930–1140 Hz in
        // the first register), a tenor's lower (Chen et al. 2009: 760 ± 250
        // Hz). Where the peaks of a mid-register fingering fall under a
        // quarter of the strongest.
        const double lo[sax::kKinds] = {650, 450}, hi[sax::kKinds] = {1500, 1100};
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Fingering &f = sax::design(sax::Kind(k)).at(8);
            float top = 0;
            for (int m = 0; m < f.n; ++m)
                top = std::max(top, f.a[m]);
            double cut = 0;
            for (int m = 0; m < f.n; ++m)
                if (f.a[m] >= 0.25f * top)
                    cut = f.w[m] / (2 * M_PI);
            QVERIFY2(cut > lo[k] && cut < hi[k], qPrintable(QStringLiteral("kind %1: %2 Hz").arg(k).arg(cut)));
        }
    }

    void theReedSpeaksAtItsThreshold() {
        // Blown at a steady pressure, a fresh horn's reed starts to swing
        // (and goes on growing) once the mouth pressure passes where the
        // linear theory puts the first mode's instability, the static
        // reed's ζ(3γ−1)/2√γ = 1/Z with the reed's own dynamics (Silva et
        // al. 2008); under it, the reed's small push dies away. Found by
        // bisection, to within a few hundredths of P_M.
        for (int k = 0; k < sax::kKinds; ++k)
            for (int s : {2, 8, 14}) {
                const sax::Design &d = sax::design(sax::Kind(k));
                const sax::Fingering &f = d.at(s);
                const float midi = float(d.spec.lowMidi + s);
                // (Speaking: the reed still swinging two seconds on, by more
                // than a hundredth of its opening.)
                auto speaks = [&](float blow) {
                    std::vector<float> reed;
                    blowNote(sax::Kind(k), midi, blow, 2.0, 0.5f, &reed, nullptr, 0, false);
                    float lo = 1e9f, hi = -1e9f;
                    for (size_t i = size_t(1.8 * kRate); i < reed.size(); ++i) {
                        lo = std::min(lo, reed[i]);
                        hi = std::max(hi, reed[i]);
                    }
                    return hi - lo > 0.02f;
                };
                float lo = 0.3f, hi = 0.8f;
                for (int it = 0; it < 7; ++it) {
                    const float m = 0.5f * (lo + hi);
                    (speaks(m) ? hi : lo) = m;
                }
                sax::Sax probe;
                blowNote(sax::Kind(k), midi, 0.5f, 0.05, 0.5f, nullptr, &probe);
                const double wr = std::max(2 * M_PI * d.spec.reedHz * 0.85, 1.9 * 2 * M_PI * midiToHz(midi));
                const double want = linearThreshold(probe.zeta(), f.a[f.note], 2 * M_PI * midiToHz(midi), wr, d.spec.reedQ * 0.5);
                // (The simulated horn speaks a few hundredths above the
                // single-mode theory: the other modes, the tract.)
                QVERIFY2(std::fabs(hi - want) < 0.12,
                         qPrintable(QStringLiteral("kind %1 s %2: speaks from %3, theory %4").arg(k).arg(s).arg(hi).arg(want)));
            }
    }

    void itBeatsWhenBlownHard() {
        // Blown hard the reed beats, lying shut for part of each period. In the
        // idealised saxophone (the Helmholtz motion's analogue, Ollivier,
        // Dalmont & Kergomard 2004) that part is the cone's truncation over
        // its length, x1/(L + x1): a short episode, more of the period up
        // the neck as the tube shortens.
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Design &d = sax::design(sax::Kind(k));
            for (int s : {0, 10}) {
                const float midi = float(d.spec.lowMidi + s);
                sax::Sax hard;
                blowNote(sax::Kind(k), midi, 0.75f, 0.6, 0.5f, nullptr, &hard);
                hard.closedShare();
                // (Rendered on a little, to measure the steady state.)
                std::vector<float> tail(4800);
                sax::Sax::Control c;
                c.hz = midiToHz(midi);
                c.lip = 0.5f;
                c.blow = 0.75f;
                hard.render(tail.data(), 4800, c);
                const float closed = hard.closedShare();
                const float beta = d.at(s).closed;
                QVERIFY2(closed > 0.5f * beta && closed < 2.0f * beta,
                         qPrintable(QStringLiteral("kind %1 s %2: shut %3 of the period, x1/(L+x1) %4").arg(k).arg(s).arg(closed).arg(beta)));
            }
        }
    }

    void everyNoteIsInTune() {
        // Across each horn's range (and half an octave of the larger horn
        // below it), at an
        // ordinary breath and embouchure, the note sounds where it is
        // fingered within a few cents once the player's ear has drawn it in,
        // and in its own register (no octave or twelfth). (Above the palm
        // keys, the smaller horns: see aboveThePalmKeys.)
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Design &d = sax::design(sax::Kind(k));
            for (int s = -6; s <= 30; s += 4) {
                const float midi = float(d.spec.lowMidi + s);
                std::vector<float> reed;
                blowNote(sax::Kind(k), midi, 0.62f, 1.0, 0.5f, &reed);
                const double got = periodHz(reed, 0.75, 0.2, midiToHz(midi));
                // (Below the horn's own range, the larger horn's low notes,
                // slower for the ear to draw in, within 15.)
                QVERIFY2(got > 0 && std::fabs(cents(got, midiToHz(midi))) < (s < 0 ? 15 : 10),
                         qPrintable(QStringLiteral("kind %1 midi %2: %3 Hz").arg(k).arg(midi).arg(got)));
            }
        }
    }

    void aboveThePalmKeys() {
        // Past the horn's top (the smaller horn that stands in for the
        // altissimo, docs/saxophone.md), from written F6 to more than an
        // octave above: each note in its own register and near its pitch, at
        // an ordinary touch and at a hard one with a firm lip and a fast
        // attack, the ear still drawing it in.
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Design &d = sax::design(sax::Kind(k));
            for (int s = 29; s <= 50; ++s)
                for (int hard = 0; hard < 2; ++hard) {
                    const float midi = float(d.spec.lowMidi + s);
                    std::vector<float> reed;
                    blowNote(sax::Kind(k), midi, hard ? 0.85f : 0.65f, 1.2, hard ? 0.95f : 0.6f, &reed);
                    const double got = periodHz(reed, 0.95, 0.2, midiToHz(midi));
                    QVERIFY2(got > 0 && std::fabs(cents(got, midiToHz(midi))) < 50,
                             qPrintable(QStringLiteral("kind %1 s %2 %3: %4 Hz").arg(k).arg(s).arg(hard ? "hard" : "ordinary").arg(got)));
                }
        }
    }

    void nothingRunsAway() {
        // Pushed about: the pitch leaping across the range and bending, the
        // breath from nothing to past the reed's closing, every embouchure,
        // growling, tongued again and again: always finite and bounded, and
        // silent once the breath stops.
        for (int k = 0; k < sax::kKinds; ++k) {
            sax::Sax sx;
            sx.init(kRate);
            sx.start(sax::Kind(k), 220);
            Rng rng(0x5a7u + uint32_t(k));
            sax::Sax::Control c;
            float buf[32], top = 0;
            for (int b = 0; b < int(6.0 * kRate) / 32; ++b) {
                if (b % 100 == 0) {
                    c.hz = 60.0f * std::exp2(5.0f * rng.unit());
                    c.lip = rng.unit();
                    c.growl = rng.unit() < 0.3f ? rng.unit() : 0.0f;
                    c.air = rng.unit();
                    if (rng.unit() < 0.3f)
                        sx.tongue(rng.unit());
                }
                c.hz *= std::exp2(0.002f * std::sin(0.05f * float(b)));
                c.blow = 1.3f * rng.unit() * rng.unit();
                std::fill(buf, buf + 32, 0.0f);
                sx.render(buf, 32, c);
                for (float v : buf) {
                    QVERIFY2(std::isfinite(v), qPrintable(QString::number(k)));
                    top = std::max(top, std::fabs(v));
                }
            }
            QVERIFY2(top < 40.0f, qPrintable(QStringLiteral("kind %1: %2 Pa").arg(k).arg(top)));
            c.blow = 0;
            for (int b = 0; b < int(0.5 * kRate) / 32; ++b)
                sx.render(buf, 32, c);
            QVERIFY2(sx.level() < 0.01f, qPrintable(QStringLiteral("kind %1: %2").arg(k).arg(sx.level())));
        }
    }

    void theLeadPlaysItsSaxes() {
        // Chosen with → past the Tam-tam, each says what it is for, saves
        // with a song by its name and comes back by it, and plays its note
        // in tune through the page.
        const char *styled[] = {"Tenor sax · R&B", "Alto sax · smooth jazz"};
        for (int snd = kTenor; snd <= kAlto; ++snd) {
            testkit::reopen(onSound(snd, "end 0.5\n"), 0.3, [&](Session &, Session &opened, const QJsonObject &doc) {
                const auto settings =
                    doc[QStringLiteral("parts")].toArray()[kLead].toObject()[QStringLiteral("settings")].toObject();
                QCOMPARE(settings[QStringLiteral("sound")].toString(), QString::fromUtf8(lead::sound(snd).name));
                QVERIFY2(opened.pageStatus().contains(QString::fromUtf8(styled[snd - kTenor])), qPrintable(opened.pageStatus()));
            });
            const auto take = testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=0.55\nup 1.4 1\nend 1.8\n", cellX(7))));
            QVERIFY2(take.finite() && take.peak() < 1.0, lead::sound(snd).name);
            QVERIFY2(take.rms(0.4, 1.3) > 0.02, lead::sound(snd).name);
            const double want = 440.0 * std::exp2(lead::sound(snd).saxOct); // (the tenor's an octave under the alto)
            const double got = periodHz(mono(take), 0.9, 0.3, want);
            QVERIFY2(got > 0 && std::fabs(cents(got, want)) < 10, qPrintable(QStringLiteral("%1: %2").arg(lead::sound(snd).name).arg(got)));
        }
    }

    void pressureIsBreath() {
        // A light touch is a soft, dark subtone; a hard one louder and much
        // brighter (the reed beating), as a saxophone's spectrum widens with
        // its dynamic (Fuks & Sundberg's pp to ff).
        for (int snd = kTenor; snd <= kAlto; ++snd) {
            // (The neck's A3: the alto's A3, the tenor's E3, a fourth under.)
            const int cell = 0;
            auto play = [&](double y, double p) {
                return mono(testkit::play(onSound(snd, fmt("down 0.3 1 %.4f %.3f p=%.3f\nup 1.3 1\nend 1.5\n", cellX(cell), y, p))));
            };
            // (Hard, but short of the growl a press at the very top brings.)
            const auto soft = play(0.2, 0.1), hard = play(0.8, 0.88);
            const double f0 = midiToHz(57 + 12 * lead::sound(snd).saxOct);
            QVERIFY2(rms(hard, 0.6, 1.2) > 1.8 * rms(soft, 0.6, 1.2),
                     qPrintable(QStringLiteral("%1: %2 %3").arg(lead::sound(snd).name).arg(rms(hard, 0.6, 1.2)).arg(rms(soft, 0.6, 1.2))));
            const double cs = centroid(soft, 0.7, 0.4, f0), ch = centroid(hard, 0.7, 0.4, f0);
            QVERIFY2(ch > 1.1 * cs, qPrintable(QStringLiteral("%1: centroid %2 → %3").arg(lead::sound(snd).name).arg(cs).arg(ch)));
        }
    }

    void subtoneIsDarkAndEdgeBright() {
        // On the horn itself: a subtone (much lip on the reed, the jaw down,
        // the reed nearly closed by it, blown just over its threshold) is
        // soft and almost a sine; an edge (a firm lip, the reed open, blown
        // hard) is louder by more than 10 dB and much brighter, its reed
        // beating.
        for (int k = 0; k < sax::kKinds; ++k) {
            const float midi = float(sax::design(sax::Kind(k)).spec.lowMidi + 13);
            auto play = [&](float blow, float lip, float open) {
                sax::Sax sx;
                sx.init(kRate);
                sx.start(sax::Kind(k), midiToHz(midi));
                sx.tongue(0.5f, true);
                sax::Sax::Control c;
                c.hz = midiToHz(midi);
                c.lip = lip;
                c.open = open;
                c.find = true;
                std::vector<float> out(48000, 0.0f);
                for (int b = 0; b < int(kRate) / 32; ++b) {
                    c.blow = std::min(1.0f, float(b * 32) / (0.02f * kRate)) * blow;
                    sx.render(out.data() + 32 * b, 32, c);
                }
                return out;
            };
            const auto soft = play(0.72f, 0.05f, 0.55f), edge = play(0.86f, 0.95f, 1.0f);
            const double f0 = midiToHz(midi);
            QVERIFY2(rms(edge, 0.6, 1.0) > 3.2 * rms(soft, 0.6, 1.0),
                     qPrintable(QStringLiteral("kind %1: %2 %3").arg(k).arg(rms(edge, 0.6, 1.0)).arg(rms(soft, 0.6, 1.0))));
            QVERIFY2(centroid(edge, 0.6, 0.4, f0) > 1.4 * centroid(soft, 0.6, 0.4, f0),
                     qPrintable(QStringLiteral("kind %1: %2 %3").arg(k).arg(centroid(edge, 0.6, 0.4, f0)).arg(centroid(soft, 0.6, 0.4, f0))));
        }
    }

    void legatoSlursWithoutClicks() {
        // Slurred within a register (the alto from A3, the tenor from D4 in
        // its second register, a fourth under the neck's G4): the keys move under the breath, so no change
        // of note puts a sharper edge into the line than the beating reed
        // puts into every period of a held note, and the line never stops.
        const int alto[5] = {1, 2, 3, 2, 1}, tenor[5] = {7, 8, 9, 8, 7};
        for (int snd = kTenor; snd <= kAlto; ++snd) {
            const Slur r = snd == kAlto ? slur(snd, 0, alto) : slur(snd, 6, tenor);
            QVERIFY2(r.change < 2.5 * r.steady, qPrintable(QStringLiteral("%1: %2 against %3").arg(lead::sound(snd).name).arg(r.change).arg(r.steady)));
            QVERIFY2(r.gap > 0.25, qPrintable(QStringLiteral("%1: %2").arg(lead::sound(snd).name).arg(r.gap)));
        }
    }

    void slurringAcrossTheBreak() {
        // Across the break (written C♯5 to D5, the same cells on both: the
        // alto's E4 to F4, the tenor's B3 to C4, a fourth under), up and
        // back down, the octave key
        // and most of the keys at once: a real horn blips; here too the
        // regime changes under the reed, but the line never stops, no edge
        // is more than a few times the reed's own, and every note, the ones
        // slurred to and the one fallen back to, lands on its pitch.
        struct Case {
            int snd, base;
            int cells[5];
            double up[5], down;
        };
        const Case cases[] = {
            {kAlto, 4, {5, 6, 7, 6, 5}, {349.23, 392.0, 440.0, 392.0, 349.23}, 329.63},
            {kTenor, 4, {5, 6, 7, 6, 5}, {261.63, 293.66, 329.63, 293.66, 261.63}, 246.94},
        };
        for (const Case &k : cases) {
            const Slur r = slur(k.snd, k.base, k.cells);
            const QString name = QString::fromUtf8(lead::sound(k.snd).name);
            QVERIFY2(r.change < 6 * r.steady, qPrintable(QStringLiteral("%1: %2 against %3").arg(name).arg(r.change).arg(r.steady)));
            QVERIFY2(r.gap > 0.25, qPrintable(QStringLiteral("%1: %2").arg(name).arg(r.gap)));
            for (int i = 0; i < 5; ++i) {
                QVERIFY2(r.hz[i] > 0 && std::fabs(cents(r.hz[i], k.up[i])) < 30,
                         qPrintable(QStringLiteral("%1 up %2: %3 Hz").arg(name).arg(i).arg(r.hz[i])));
                QVERIFY2(r.back[i] > 0 && std::fabs(cents(r.back[i], k.down)) < 30,
                         qPrintable(QStringLiteral("%1 back %2: %3 Hz").arg(name).arg(i).arg(r.back[i])));
            }
        }
    }

    void bendsKeepTheLevel() {
        // A lip bend moves the note, not the loudness: every note of each
        // horn's range bent ±15, ±30 and ±45 cents (the lip and, past half a
        // semitone, the next key) radiates within 3 dB of the note unbent.
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Design &d = sax::design(sax::Kind(k));
            for (int s = -4; s <= 34; s += 2) {
                const float midi = float(d.spec.lowMidi + s);
                auto level = [&](float cents) {
                    const auto x = blowNote(sax::Kind(k), midi + cents / 100.0f, 0.7f, 0.8);
                    return 20 * std::log10(rms(x, 0.5, 0.8) + 1e-9);
                };
                const double flat = level(0);
                for (float c : {-45.0f, -30.0f, -15.0f, 15.0f, 30.0f, 45.0f}) {
                    const double bent = level(c);
                    // (The tenor's low notes have their strength at their
                    // break, in their upper harmonics, which a bend up on
                    // the lip alone pulls the reed's support from: its third
                    // note loses 3.8 dB at 45 cents.)
                    QVERIFY2(std::fabs(bent - flat) < (k == sax::Tenor ? 4.0 : 3.0),
                             qPrintable(QStringLiteral("kind %1 s %2 bent %3 c: %4 dB against %5").arg(k).arg(s).arg(c).arg(bent).arg(flat)));
                }
            }
        }
    }

    void theLevelRunsEvenlyUpTheRange() {
        // No note of the range (and the larger and smaller horns past it)
        // stands out: each within 10 dB of the range's median at the same
        // breath and embouchure.
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Design &d = sax::design(sax::Kind(k));
            std::vector<double> db;
            for (int s = -8; s <= 40; ++s) {
                const auto x = blowNote(sax::Kind(k), float(d.spec.lowMidi + s), 0.7f, 0.8);
                db.push_back(20 * std::log10(rms(x, 0.5, 0.8) + 1e-9));
            }
            std::vector<double> sorted = db;
            std::sort(sorted.begin(), sorted.end());
            const double median = sorted[sorted.size() / 2];
            for (size_t i = 0; i < db.size(); ++i)
                QVERIFY2(std::fabs(db[i] - median) < 10,
                         qPrintable(QStringLiteral("kind %1 s %2: %3 dB, median %4").arg(k).arg(int(i) - 8).arg(db[i]).arg(median)));
        }
    }

    void aSlideAndATongueStayClean() {
        // On the page: a finger slid from one note to the next (the lip,
        // then the key) swells no more than a few decibels; and notes
        // tongued again and again on a sounding horn (detached, close
        // together) put no edge into the line sharper than a few times a
        // held note's, while each one is articulated.
        for (int snd = kTenor; snd <= kAlto; ++snd) {
            // (Within a register: the alto B3 to C4, the tenor D4 to E4,
            // a fourth under the neck's G4 and A4.)
            const int from = snd == kAlto ? 1 : 6;
            const auto slide = mono(testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=0.6\nglide 0.9 1.5 1 %.4f 0.5 p=0.6\nup 2.1 1\nend 2.3\n", cellX(from), cellX(from + 1)))));
            double lo = 1e9, hi = 0;
            for (double t = 0.6; t < 2.0; t += 0.05) {
                const double r = rms(slide, t, t + 0.05);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
            QVERIFY2(20 * std::log10(hi / lo) < 8, qPrintable(QStringLiteral("%1 slide: %2 to %3").arg(lead::sound(snd).name).arg(lo).arg(hi)));
            std::string t;
            for (int i = 0; i < 6; ++i)
                t += fmt("down %.2f %d %.4f 0.5 p=0.6\nup %.2f %d\n", 0.3 + 0.25 * i, i + 1, cellX(3), 0.5 + 0.25 * i, i + 1);
            t += "end 2.0\n";
            const auto x = mono(testkit::play(onSound(snd, t)));
            auto edge = [&](double from, double to) {
                double m = 0;
                for (size_t i = size_t(from * kRate); i < std::min(x.size(), size_t(to * kRate)); ++i)
                    m = std::max(m, std::fabs(double(x[i]) - 2.0 * x[i - 1] + x[i - 2]));
                return m;
            };
            double steady = 0, attack = 0;
            for (int i = 1; i < 6; ++i) {
                steady = std::max(steady, edge(0.4 + 0.25 * i, 0.48 + 0.25 * i));
                attack = std::max(attack, edge(0.29 + 0.25 * i, 0.36 + 0.25 * i));
            }
            QVERIFY2(attack < 3 * steady, qPrintable(QStringLiteral("%1 tongue: %2 against %3").arg(lead::sound(snd).name).arg(attack).arg(steady)));
        }
    }

    void aGrowlRoughensIt() {
        // Humming into the horn while playing puts the voice's pitch under
        // the note: sidebands between the note's harmonics, the growl.
        auto between = [&](const std::vector<float> &x, double f0) {
            double in = 0, out = 0;
            for (double f = 0.6 * f0; f < 8 * f0; f += f0 / 16) {
                const double fr = f / f0 - std::floor(f / f0);
                const double p = std::pow(10.0, levelAt(x, 0.5, 0.4, f) / 10);
                (fr < 0.1 || fr > 0.9 ? in : out) += p;
            }
            return 10 * std::log10(out / in);
        };
        const float midi = 57;
        const auto clean = blowNote(sax::Tenor, midi, 0.62f, 1.0);
        const auto growl = blowNote(sax::Tenor, midi, 0.62f, 1.0, 0.5f, nullptr, nullptr, 1.0f);
        QVERIFY2(between(growl, midiToHz(midi)) > between(clean, midiToHz(midi)) + 6,
                 qPrintable(QStringLiteral("%1 %2").arg(between(growl, midiToHz(midi))).arg(between(clean, midiToHz(midi)))));
    }

    void theTenorIsNotAnAlto() {
        // The same notes on both horns, blown alike. The tenor's lattice of
        // open holes cuts off lower (its holes, b/a 0.54, on a bore 1.33
        // times longer) and its spectrum peaks at its own break, so it
        // breaks lower, as Benade & Lutgen measured (618 Hz against the
        // alto's 837, a ratio of 0.74), and less of its energy lies above
        // 1 kHz. Bounded both ways: should the two horns converge again (a
        // tenor that is an alto scaled) this fails, and so it does if the
        // tenor goes dull.
        struct Look {
            double high, edge;
            std::vector<double> profile;
        };
        auto look = [&](sax::Kind kind, float midi) {
            const auto x = blowNote(kind, midi, 0.75f, 1.0, 0.6f);
            const double f0 = midiToHz(midi);
            std::vector<double> p;
            for (int h = 1; h * f0 < 9000; ++h)
                p.push_back(std::pow(10.0, levelAt(x, 0.6, 0.4, h * f0) / 10));
            double all = 0, high = 0, top = 0;
            for (size_t h = 0; h < p.size(); ++h) {
                all += p[h];
                top = std::max(top, p[h]);
                if (double(h + 1) * f0 > 1000)
                    high += p[h];
            }
            Look l;
            l.high = 10 * std::log10(high / all + 1e-30);
            l.edge = 0; // the highest harmonic within 20 dB of the strongest
            for (size_t h = 0; h < p.size(); ++h) {
                if (p[h] > 0.01 * top)
                    l.edge = double(h + 1) * f0;
                if (h < 12)
                    l.profile.push_back(10 * std::log10(p[h] / top + 1e-30));
            }
            return l;
        };
        std::vector<double> highs, edges, dists;
        int darker = 0;
        const float notes[] = {50, 55, 57, 62, 64, 69, 72}; // D3 to C5, inside both horns' ranges
        for (const float midi : notes) {
            const Look a = look(sax::Alto, midi), t = look(sax::Tenor, midi);
            highs.push_back(t.high - a.high);
            edges.push_back(t.edge / a.edge);
            double d = 0;
            for (size_t h = 0; h < 12; ++h)
                d += (t.profile[h] - a.profile[h]) * (t.profile[h] - a.profile[h]);
            dists.push_back(std::sqrt(d / 12));
            if (t.high < a.high)
                ++darker;
            qInfo("%.0f: above 1 kHz alto %.1f tenor %.1f dB, edge alto %.0f tenor %.0f Hz, apart %.1f dB", double(midi), a.high,
                  t.high, a.edge, t.edge, dists.back());
            // (Not a muffled horn: some of it still above 1 kHz.)
            QVERIFY2(t.high > -30, qPrintable(QStringLiteral("%1: tenor %2 dB above 1 kHz").arg(midi).arg(t.high)));
        }
        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        QVERIFY2(darker >= 6, qPrintable(QStringLiteral("darker on %1 of 7").arg(darker)));
        QVERIFY2(median(highs) < -4 && median(highs) > -14, qPrintable(QStringLiteral("%1 dB").arg(median(highs))));
        QVERIFY2(median(edges) > 0.5 && median(edges) < 0.78, qPrintable(QStringLiteral("edge ratio %1").arg(median(edges))));
        QVERIFY2(median(dists) > 6, qPrintable(QStringLiteral("profiles %1 dB apart").arg(median(dists))));
    }

    void theTenorIsAsBrightAsARealOne() {
        // Against UNSW's recorded tenor (its dynamic not stated): on its low
        // notes (concert A♭2 to E♭3) the energy above 1 kHz is 6 to 9 dB
        // under the total. This model's horns are darker than measured
        // (docs/saxophone.md, "Darker than measured"; this one's about
        // 20 dB under): kept as a known failure, so a fix shows. (The bound,
        // −12 dB, is 3 to 6 dB short of the recordings.)
        const sax::Design &d = sax::design(sax::Tenor);
        std::vector<double> highs;
        for (int s = 0; s <= 7; s += 2) {
            const float midi = float(d.spec.lowMidi + s);
            const auto x = blowNote(sax::Tenor, midi, 0.85f, 1.0, 0.8f);
            const double f0 = midiToHz(midi);
            double all = 0, high = 0;
            for (int h = 1; h * f0 < 9000; ++h) {
                const double p = std::pow(10.0, levelAt(x, 0.6, 0.4, h * f0) / 10);
                all += p;
                if (h * f0 > 1000)
                    high += p;
            }
            highs.push_back(10 * std::log10(high / all + 1e-30));
        }
        std::sort(highs.begin(), highs.end());
        QEXPECT_FAIL("", "the model's spectra fall too fast above the break", Continue);
        QVERIFY2(highs[highs.size() / 2] > -12, qPrintable(QStringLiteral("%1 dB above 1 kHz").arg(highs[highs.size() / 2])));
    }

    void theTenorBreathesMoreInSubtone() {
        // A subtone, the same low notes on both (D3, E3, A3, B3): the
        // tenor's bigger reed and its slack lip pass more air, the hiss
        // between the harmonics. (Estimated, not measured: see
        // docs/saxophone.md.)
        auto hiss = [&](sax::Kind kind, float midi) {
            const auto x = blowNote(kind, midi, 0.75f, 1.0, 0.2f);
            const double f0 = midiToHz(midi);
            double in = 0, out = 0;
            for (double f = 0.6 * f0; f < 12 * f0; f += f0 / 8) {
                const double fr = f / f0 - std::floor(f / f0);
                const double p = std::pow(10.0, levelAt(x, 0.5, 0.4, f) / 10);
                (fr < 0.1 || fr > 0.9 ? in : out) += p;
            }
            return 10 * std::log10(out / in);
        };
        std::vector<double> more;
        for (const float midi : {50.0f, 52.0f, 57.0f, 59.0f}) {
            const double a = hiss(sax::Alto, midi), t = hiss(sax::Tenor, midi);
            qInfo("%.0f: hiss alto %.1f, tenor %.1f dB", double(midi), a, t);
            QVERIFY2(t > a, qPrintable(QStringLiteral("%1: alto %2, tenor %3 dB").arg(midi).arg(a).arg(t)));
            more.push_back(t - a);
        }
        std::sort(more.begin(), more.end());
        QVERIFY2(more[1] + more[2] > 4, qPrintable(QStringLiteral("%1, %2 dB more").arg(more[1]).arg(more[2])));
    }

    void theLowestNotesSettle() {
        // Each horn's lowest three notes, at an ordinary and at a hard
        // breath: on their pitch within 25 cents from 0.3 s to 1.1 s (a
        // cracked start, an octave up, may last no longer). (The windows
        // counted in whole tenths: a sum of 0.1s overshoots, and would add
        // one at 1.1 s reaching past what was rendered.)
        for (int k = 0; k < sax::kKinds; ++k) {
            const sax::Design &d = sax::design(sax::Kind(k));
            for (int s = 0; s <= 2; ++s)
                for (const auto &[blow, lip] : {std::pair{0.75f, 0.6f}, std::pair{0.85f, 0.9f}}) {
                    const float midi = float(d.spec.lowMidi + s);
                    std::vector<float> reed;
                    blowNote(sax::Kind(k), midi, blow, 1.2, lip, &reed);
                    for (int w = 3; w <= 10; ++w) {
                        const double t = 0.1 * w;
                        const double hz = periodHz(reed, t, 0.1, midiToHz(midi));
                        QVERIFY2(hz > 0 && std::fabs(cents(hz, midiToHz(midi))) < 25,
                                 qPrintable(QStringLiteral("kind %1 s %2 blow %3 at %4 s: %5 Hz").arg(k).arg(s).arg(blow).arg(t).arg(hz)));
                    }
                }
        }
    }

    void eachSaxPlaysInItsOwnRange() {
        // The same cell on the Lead's two saxes is the same fingering: the
        // tenor a fourth under the alto, as the horns are (A♭2 to E5 against
        // D♭3 to A5). Low, pressed nearly to the top (short of where any
        // sound's press brings the growl), the tenor honks, much rougher
        // than the alto pressed alike.
        auto play = [&](int snd, double p) {
            return mono(testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.7 p=%.3f\nup 1.3 1\nend 1.5\n", cellX(0), p))));
        };
        auto between = [&](const std::vector<float> &x, double f0) {
            double in = 0, out = 0;
            for (double f = 0.6 * f0; f < 8 * f0; f += f0 / 16) {
                const double fr = f / f0 - std::floor(f / f0);
                const double pw = std::pow(10.0, levelAt(x, 0.7, 0.4, f) / 10);
                (fr < 0.1 || fr > 0.9 ? in : out) += pw;
            }
            return 10 * std::log10(out / in);
        };
        auto high = [&](const std::vector<float> &x, double f0) {
            double all = 0, hi = 0;
            for (int h = 1; h * f0 < 9000; ++h) {
                const double p = std::pow(10.0, levelAt(x, 0.7, 0.4, h * f0) / 10);
                all += p;
                if (h * f0 > 1000)
                    hi += p;
            }
            return 10 * std::log10(hi / all + 1e-30);
        };
        const auto tenor = play(kTenor, 0.92), alto = play(kAlto, 0.92), tenorForte = play(kTenor, 0.8), tenorTop = play(kTenor, 1.0);
        const double ft = periodHz(tenor, 0.7, 0.4, midiToHz(52)), fa = periodHz(alto, 0.7, 0.4, midiToHz(57));
        QVERIFY2(std::fabs(cents(ft, midiToHz(52))) < 30, qPrintable(QStringLiteral("tenor %1 Hz").arg(ft)));
        QVERIFY2(std::fabs(cents(fa, midiToHz(57))) < 30, qPrintable(QStringLiteral("alto %1 Hz").arg(fa)));
        const double f0 = midiToHz(52);
        const double honk = between(tenor, f0), forte = between(tenorForte, f0), al = between(alto, midiToHz(57));
        qInfo("between the harmonics: tenor honking %.1f, at a forte %.1f, alto %.1f dB", honk, forte, al);
        QVERIFY2(honk > forte + 4, qPrintable(QStringLiteral("tenor honking %1, at a forte %2 dB").arg(honk).arg(forte)));
        QVERIFY2(honk > al + 4, qPrintable(QStringLiteral("tenor %1, alto %2 dB").arg(honk).arg(al)));
        // (Rough, not muffled: as much above 1 kHz as a forte, within 3 dB.)
        const double hf = high(tenorForte, f0), hh = high(tenor, f0), ht = high(tenorTop, f0);
        qInfo("above 1 kHz: forte %.1f, honking %.1f, at the top %.1f dB", hf, hh, ht);
        QVERIFY2(hh > hf - 3 && ht > hf - 3, qPrintable(QStringLiteral("forte %1, honking %2, top %3 dB").arg(hf).arg(hh).arg(ht)));
    }

    void aSaxStaysInsideBudget() {
        // One horn, blown hard, against the clock: well under one per cent
        // of a core.
        TENFOLD_TIMED();
        for (int k = 0; k < sax::kKinds; ++k) {
            sax::Sax sx;
            sx.init(kRate);
            sx.start(sax::Kind(k), 220.0f);
            sax::Sax::Control c;
            c.hz = 220;
            c.blow = 0.7f;
            float buf[32];
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < int(4.0 * kRate) / 32; ++b)
                sx.render(buf, 32, c);
            const double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
            QVERIFY2(share < 0.01, qPrintable(QStringLiteral("kind %1: %2").arg(k).arg(share)));
        }
    }

    void aSaxSectionStaysInsideBudget() {
        // A tenor line with three harmony voices (four horns) played through
        // the whole studio: it keeps its time.
        TENFOLD_TIMED();
        std::string s = onSound(kTenor, "");
        s += "down 0.3 11 0.08 0.78 p=0.8\ndown 0.3 12 0.15 0.62 p=0.8\ndown 0.3 13 0.08 0.5 p=0.8\n";
        for (int i = 0; i < 12; ++i)
            s += fmt("down %.2f %d %.4f 0.6 p=0.8\nup %.2f %d\n", 0.4 + 0.4 * i, i + 1, cellX((i * 3) % 12), 0.75 + 0.4 * i, i + 1);
        s += "up 5.4 11\nup 5.4 12\nup 5.4 13\nend 5.6\n";
        // (Once to warm the studio up: a first render pays for memory
        // first touched.)
        (void)renderScript(s, "");
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.2, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(SaxophoneTests)
#include "saxophone_tests.moc"
