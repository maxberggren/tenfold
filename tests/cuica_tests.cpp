// The cuíca kit: a friction drum (cuica.h), checked as the measurements of
// real ones go (docs/cuica.md): the open stroke's pitch near the shell's
// air, the thumb bending it up an octave and more, nearer the knot and
// harder higher, the cloth's stick–slip at the pitch it sounds, a spectrum
// led by its fundamental, nothing sounding that isn't rubbed, nothing
// running away, and what it costs.

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

// The cuíca by its place among the kits: Right from house, as many times as
// its index.
std::string cuicaKeys() {
    std::string s;
    for (int i = 0; i < drums::Cuica; ++i)
        s += fmt("key %.3f Right\n", 0.1 + 0.002 * i);
    return s;
}

// Mono output of a body driven by `play`, called once per block with the
// block's time.
template <typename F>
std::vector<float> run(cuica::Body &b, double seconds, F play) {
    std::vector<float> out;
    float l[kBlock], r[kBlock];
    for (int k = 0; k < int(seconds * kRate) / kBlock; ++k) {
        play(double(k) * kBlock / kRate);
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        b.render(l, r, kBlock);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(0.5f * (l[i] + r[i]));
    }
    return out;
}

// Strokes back and forth (each `len` m in `secs`, a pause between), with a
// thumb at r pressing from th0 to th1 during each.
std::vector<float> strokes(int count, float len, float secs, float grip, float r, float th0, float th1,
                           double seconds, cuica::Body *keep = nullptr) {
    cuica::Body local;
    cuica::Body &b = keep ? *keep : local;
    if (!keep)
        b.init(kRate);
    int done = 0;
    double next = 0.05;
    return run(b, seconds, [&](double t) {
        if (done < count && t >= next) {
            cuica::Stroke s;
            const bool pull = done % 2 == 0;
            s.length = pull ? len : -len;
            s.from = pull ? 0.25f : 0.25f + len / (0.8f * cuica::Physics::kStickLength);
            s.seconds = secs;
            s.grip = grip;
            s.thumbR = r;
            s.thumb0 = th0;
            s.thumb1 = th1;
            b.stroke(s, 0);
            ++done;
            next = t + secs + 0.06;
        }
    });
}

// The pitch in a window by autocorrelation (0: none clear).
double pitchAt(const std::vector<float> &x, double t, double len = 0.03, double lo = 90, double hi = 1200) {
    const size_t a = size_t(t * kRate), n = size_t(len * kRate);
    const int lmax = int(kRate / lo), lmin = int(kRate / hi);
    if (a + n + size_t(lmax) + 2 >= x.size())
        return 0;
    double e0 = 0;
    for (size_t i = 0; i < n; ++i)
        e0 += double(x[a + i]) * x[a + i];
    if (e0 < 1e-9 * double(n))
        return 0;
    std::vector<double> c(size_t(lmax) + 2);
    for (int L = lmin; L <= lmax + 1; ++L) {
        double s = 0, e1 = 0;
        for (size_t i = 0; i < n; ++i) {
            s += double(x[a + i]) * x[a + i + size_t(L)];
            e1 += double(x[a + i + size_t(L)]) * x[a + i + size_t(L)];
        }
        c[size_t(L)] = s / std::sqrt(e0 * e1 + 1e-30);
    }
    double top = 0;
    for (int L = lmin; L <= lmax; ++L)
        top = std::max(top, c[size_t(L)]);
    if (top < 0.6)
        return 0;
    for (int L = lmin + 1; L <= lmax; ++L)
        if (c[size_t(L)] > c[size_t(L) - 1] && c[size_t(L)] >= c[size_t(L) + 1] && c[size_t(L)] > 0.85 * top) {
            const double y0 = c[size_t(L) - 1], y1 = c[size_t(L)], y2 = c[size_t(L) + 1];
            return kRate / (L + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2));
        }
    return 0;
}

// The median pitch over [a, b).
double medianPitch(const std::vector<float> &x, double a, double b) {
    std::vector<double> p;
    for (double t = a; t < b; t += 0.01)
        if (const double f = pitchAt(x, t); f > 0)
            p.push_back(f);
    if (p.empty())
        return 0;
    std::sort(p.begin(), p.end());
    return p[p.size() / 2];
}

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

double rms(const std::vector<float> &x, double a, double b) {
    double s = 0;
    const size_t i0 = size_t(a * kRate), i1 = std::min(x.size(), size_t(b * kRate));
    for (size_t i = i0; i < i1; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(1, i1 - i0)));
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

}

class CuicaTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenFingersOnTheCuicaStayInsideBudget theCuicaCostsLittleAndBarelyMoreForMoreFingers")
private slots:
    // --- the instrument as built ---------------------------------------------------

    void itIsBuiltAsACuica() {
        // A 9" goatskin at a samba tension, a bamboo stick of a few grams
        // (0.2–0.8 of the skin's own mass), a shell whose quarter wave is
        // in the open tone's range, and the stick's compression modes far
        // above the note (it moves as a rigid mass at the pitch).
        cuica::Body b;
        b.init(kRate);
        const double skinMass = cuica::Physics::kSkin * M_PI * std::pow(cuica::Physics::kRadius, 2.0);
        QVERIFY(b.stickMass() > 0.2 * skinMass && b.stickMass() < 0.8 * skinMass);
        QVERIFY(b.stickMass() > 0.003f && b.stickMass() < 0.008f);
        QVERIFY(b.tension() > 3000 && b.tension() < 1.0e4f);
        QVERIFY(cuica::Body::shellHz() > 220 && cuica::Body::shellHz() < 310);
        QVERIFY(b.stickHz(1) > 5000);
    }

    // --- pitch ----------------------------------------------------------------------

    void theOpenStrokeSpeaksLowNearTheShellsAir() {
        // No thumb: the stick's mass on the skin and the shell's air pull
        // the open tone far under the skin's own (0,1), near the shell's
        // quarter wave (measured open tones 105–300 Hz).
        cuica::Body b;
        b.init(kRate);
        const auto x = strokes(4, 0.06f, 0.12f, 0.6f, 0, 0, 0, 0.8, &b);
        // (each stroke's held second half: it glides up into it)
        std::vector<double> held;
        for (int k = 0; k < 4; ++k)
            for (double t = 0.05 + 0.18 * k + 0.06; t < 0.05 + 0.18 * k + 0.11; t += 0.005)
                if (const double p = pitchAt(x, t); p > 0)
                    held.push_back(p);
        QVERIFY(held.size() > 10);
        std::sort(held.begin(), held.end());
        const double f = held[held.size() / 2];
        QVERIFY2(f > 160 && f < 300, qPrintable(QString::number(f)));
        QVERIFY2(f < 0.7 * b.skinHz(), qPrintable(QStringLiteral("%1 %2").arg(f).arg(b.skinHz())));
        QVERIFY2(std::fabs(cents(f, cuica::Body::shellHz())) < 600, qPrintable(QString::number(f)));
        // And linear acoustics agrees on where the head resonates (the
        // played note a little under it: stick–slip holds the stick a
        // moment each cycle, longer the firmer the pinch).
        const double lin = b.resonance(b.tension(), 0, 0);
        QVERIFY2(cents(f, lin) < 50 && cents(f, lin) > -250, qPrintable(QStringLiteral("%1 %2").arg(f).arg(lin)));
    }

    void theThumbBendsItUpAnOctave() {
        // Measured: pressed passages sit 9–10 semitones over the open tone,
        // peaks 17 over it (players: an octave or more). A thumb pressed
        // hard beside the knot takes a medium stroke up about an octave.
        const double open = medianPitch(strokes(2, 0.06f, 0.12f, 0.6f, 0, 0, 0, 0.5), 0.05, 0.4);
        const double top = medianPitch(strokes(2, 0.06f, 0.12f, 0.6f, 0, 1, 1, 0.5), 0.08, 0.4);
        const double octaves = std::log2(top / open);
        QVERIFY2(octaves > 0.9 && octaves < 2.2, qPrintable(QStringLiteral("%1 → %2").arg(open).arg(top)));
    }

    void harderAndNearerTheKnotIsHigher() {
        auto pitch = [](float r, float force) {
            return medianPitch(strokes(2, 0.06f, 0.12f, 0.6f, r, force, force, 0.5), 0.08, 0.4);
        };
        // Harder, on the knot.
        const double p1 = pitch(0, 0.1f), p3 = pitch(0, 0.3f), p10 = pitch(0, 1.0f);
        QVERIFY2(p1 < p3 && p3 < p10, qPrintable(QStringLiteral("%1 %2 %3").arg(p1).arg(p3).arg(p10)));
        // Nearer, pressed hard.
        const double far = pitch(0.3f, 1.0f), mid = pitch(0.15f, 1.0f);
        QVERIFY2(far < mid && mid < p10, qPrintable(QStringLiteral("%1 %2 %3").arg(far).arg(mid).arg(p10)));
        // Out near the rim the thumb hardly moves it.
        const double open = pitch(0, 0), rim = pitch(0.7f, 1.0f);
        QVERIFY2(std::fabs(cents(rim, open)) < 150, qPrintable(QStringLiteral("%1 %2").arg(rim).arg(open)));
    }

    void theLaughGlidesWithoutGaps() {
        // The thumb pressing in through a long stroke: the pitch climbs the
        // whole way, mostly in small steps, and sounds throughout.
        const auto x = strokes(1, 0.12f, 0.4f, 0.7f, 0, 0, 1, 0.6);
        std::vector<double> p;
        for (double t = 0.09; t < 0.42; t += 0.015)
            p.push_back(pitchAt(x, t));
        int voiced = 0, rising = 0;
        for (size_t i = 0; i < p.size(); ++i) {
            voiced += p[i] > 0;
            if (i > 0 && p[i] > 0 && p[i - 1] > 0 && p[i] >= p[i - 1] * 0.99)
                ++rising;
        }
        QVERIFY2(voiced >= int(p.size()) * 3 / 4, qPrintable(QString::number(voiced)));
        QVERIFY2(rising >= voiced * 2 / 3, qPrintable(QString::number(rising)));
        double lo = 1e9, hi = 0;
        for (double f : p)
            if (f > 0)
                lo = std::min(lo, f), hi = std::max(hi, f);
        QVERIFY2(hi / lo > 1.35, qPrintable(QStringLiteral("%1 %2").arg(lo).arg(hi)));
    }

    void aStrokeGlidesUpThenHolds() {
        // As recorded (+2 to +3.6 semitones on the Pearl clip, +1–2 on
        // another), rising and then level: a played stroke starts squeezed
        // and eases into the glide, the note climbing from the flat side
        // onto the head's.
        const auto x = strokes(1, 0.08f, 0.2f, 0.8f, 0, 0, 0, 0.35);
        const double first = medianPitch(x, 0.06, 0.09), later = medianPitch(x, 0.16, 0.22);
        QVERIFY2(first > 0 && later > 0, qPrintable(QStringLiteral("%1 %2").arg(first).arg(later)));
        const double rise = cents(later, first);
        QVERIFY2(rise > 150 && rise < 500, qPrintable(QStringLiteral("%1 → %2").arg(first).arg(later)));
        // ... and doesn't sag before the turn.
        const double end = medianPitch(x, 0.21, 0.235);
        QVERIFY2(cents(end, later) > -80, qPrintable(QStringLiteral("%1 → %2").arg(later).arg(end)));
    }

    void thePressedNoteIsTheHeadsNotTheHands() {
        // A thumb pressed hard beside the knot: the note stays within two
        // semitones whether the cloth is drawn at 0.2 or 0.8 m/s (a little
        // higher the faster, as a stick–slip note is).
        std::vector<double> f;
        for (float v : {0.2f, 0.4f, 0.6f, 0.8f}) {
            cuica::Body b;
            b.init(kRate);
            b.press(2, 0.1f, 1.0f, 0);
            float along = 0.02f;
            double next = 0.05;
            bool gripped = false;
            const double dur = 0.75 * 0.8 * cuica::Physics::kStickLength / v;
            const auto x = run(b, 0.05 + dur, [&](double t) {
                if (!gripped && t >= 0.05) {
                    b.grip(1, along, 0.6f, 0);
                    gripped = true;
                } else if (gripped && t >= next) {
                    along += v / (0.8f * cuica::Physics::kStickLength) * 0.005f;
                    b.slide(1, along, 0.6f);
                    next = t + 0.005;
                }
            });
            f.push_back(medianPitch(x, 0.05 + 0.3 * dur, 0.05 + dur - 0.02));
        }
        const double lo = *std::min_element(f.begin(), f.end()), hi = *std::max_element(f.begin(), f.end());
        QVERIFY2(lo > 0 && cents(hi, lo) < 200, qPrintable(QStringLiteral("%1 … %2").arg(lo).arg(hi)));
    }

    void fastStrokesAreLoudNotMute() {
        // The same 10 cm stroke drawn in 0.3, 0.15 and 0.08 s (out of the
        // turn up to about 0.5, 1 and 1.9 m/s): faster is louder.
        auto level = [](float secs) {
            const auto x = strokes(1, 0.1f, secs, 0.7f, 0, 0, 0, 0.5);
            return 20 * std::log10(rms(x, 0.05 + 0.3 * secs, 0.05 + 0.9 * secs) + 1e-9);
        };
        const double slow = level(0.3f), mid = level(0.15f), fast = level(0.08f);
        QVERIFY2(mid > slow && fast > mid - 2, qPrintable(QStringLiteral("%1 %2 %3").arg(slow).arg(mid).arg(fast)));
    }

    void theFirstStrokeSpeaksLikeTheRest() {
        // The pinch is set before the hand moves: a stroke from rest is as
        // loud as the one after it (within 4 dB).
        const auto x = strokes(2, 0.05f, 0.1f, 0.6f, 0, 0, 0, 0.4);
        const double a = rms(x, 0.06, 0.15), b = rms(x, 0.22, 0.31);
        QVERIFY2(std::fabs(20 * std::log10(a / b)) < 4, qPrintable(QStringLiteral("%1 %2").arg(a).arg(b)));
    }

    // --- the cloth ------------------------------------------------------------------

    void theClothSticksAndSlipsOncePerCycle() {
        // Stick–slip: in a steady stroke the cloth grips and lets go of
        // the stick once per period of the note it sounds.
        cuica::Body b;
        b.init(kRate);
        b.grip(1, 0.2f, 0.6f, 0);
        // (a stick of a few samples is the wet fibres catching mid-slip:
        // counted are the grips that hold, the short stick of each period)
        int slips = 0, held = 0;
        float along = 0.2f;
        std::vector<float> out;
        float l[kBlock], r[kBlock];
        for (int k = 0; k < int(0.5 * kRate) / kBlock; ++k) {
            along += 0.6f * kBlock / kRate; // ~0.12 m/s
            b.slide(1, along, 0.6f);
            for (int i = 0; i < kBlock; ++i) {
                l[0] = r[0] = 0;
                b.render(l, r, 1);
                out.push_back(l[0]);
                if (k * kBlock + i > int(0.2 * kRate)) {
                    if (b.stuckNow()) {
                        ++held;
                    } else {
                        if (held >= 12)
                            ++slips;
                        held = 0;
                    }
                }
            }
        }
        const double f = medianPitch(out, 0.2, 0.45);
        const double rate = slips / 0.3;
        QVERIFY2(f > 0 && std::fabs(rate / f - 1) < 0.15, qPrintable(QStringLiteral("%1 slips/s at %2 Hz").arg(rate).arg(f)));
    }

    void fasterIsLouder() {
        // The squeeze keeps pace with the draw, so the pinch leans on the
        // note's colour more than its level; speed is the level.
        const double slow = rms(strokes(2, 0.03f, 0.14f, 0.6f, 0, 0, 0, 0.5), 0.05, 0.4);
        const double fast = rms(strokes(2, 0.09f, 0.14f, 0.6f, 0, 0, 0, 0.5), 0.05, 0.4);
        const double firm = rms(strokes(2, 0.09f, 0.14f, 0.9f, 0, 0, 0, 0.5), 0.05, 0.4);
        QVERIFY2(20 * std::log10(fast / slow) > 4, qPrintable(QStringLiteral("%1 %2").arg(slow).arg(fast)));
        QVERIFY2(std::fabs(20 * std::log10(firm / fast)) < 3, qPrintable(QStringLiteral("%1 %2").arg(fast).arg(firm)));
    }

    void theFundamentalLeads() {
        // As measured: the fundamental strongest, the second harmonic
        // 4–23 dB under it, the third further (16–36 dB under).
        const auto x = strokes(2, 0.06f, 0.12f, 0.6f, 0, 0, 0, 0.4);
        // (in the stroke's held part, past its glide)
        const double f = medianPitch(x, 0.11, 0.17);
        QVERIFY(f > 0);
        const double l1 = levelAt(x, 0.11, 0.06, f), l2 = levelAt(x, 0.11, 0.06, 2 * f), l3 = levelAt(x, 0.11, 0.06, 3 * f);
        QVERIFY2(l2 < l1 - 2 && l2 > l1 - 30, qPrintable(QStringLiteral("%1 %2").arg(l1).arg(l2)));
        QVERIFY2(l3 < l1 - 4 && l3 > l1 - 45, qPrintable(QStringLiteral("%1 %2").arg(l1).arg(l3)));
    }

    void theThumbBendsItSmoothlyBothWays() {
        // A steady draw while the thumb presses in on the knot and lets up,
        // then slides in from out on the head onto the knot: the
        // note follows it the whole way, never stepping more than a
        // semitone and a half between frames 20 ms apart (the press is
        // steep near the knot), and is back on the open note within 50
        // cents once the thumb is off.
        auto draw = [](auto thumb) {
            cuica::Body b;
            b.init(kRate);
            float along = 0.02f;
            double next = 0.05;
            bool gripped = false;
            return run(b, 0.62, [&](double t) {
                thumb(b, t);
                if (!gripped && t >= 0.05) {
                    b.grip(1, along, 0.5f, 0);
                    gripped = true;
                } else if (gripped && t >= next) {
                    along += 0.35f / (0.8f * cuica::Physics::kStickLength) * 0.005f; // 0.35 m/s
                    b.slide(1, along, 0.5f);
                    next = t + 0.005;
                }
            });
        };
        auto smooth = [](const std::vector<float> &x, double a, double b, const char *what) {
            double last = 0, worst = 0;
            for (double t = a; t < b; t += 0.02) {
                const double f = pitchAt(x, t);
                if (f > 0 && last > 0)
                    worst = std::max(worst, std::fabs(cents(f, last)));
                last = f;
            }
            QVERIFY2(worst < 150, qPrintable(QStringLiteral("%1: a %2 cent step").arg(what).arg(worst)));
        };
        const auto press = draw([](cuica::Body &b, double t) {
            const float p = float((t - 0.1) / 0.45);
            const float force = clampf(0.1f + 0.9f * (p < 0.5f ? 2 * p : 2 - 2 * p), 0.1f, 1);
            if (t < 0.001)
                b.press(2, 0.0f, 0.1f, 0);
            b.movePress(2, 0.0f, force);
        });
        smooth(press, 0.16, 0.55, "pressing"); // (after the stroke's own onset)
        const auto slide = draw([](cuica::Body &b, double t) {
            const float p = float((t - 0.1) / 0.45);
            const float r = clampf(0.3f - 0.3f * p, 0, 0.3f);
            if (t < 0.001)
                b.press(2, 0.3f, 0.8f, 0);
            b.movePress(2, r, 0.8f);
        });
        smooth(slide, 0.12, 0.55, "sliding");
        const auto open = draw([](cuica::Body &, double) {});
        const auto lifted = draw([](cuica::Body &b, double t) {
            if (t < 0.001)
                b.press(2, 0.05f, 0.9f, 0);
            if (t > 0.25)
                b.lift(2);
        });
        const double before = medianPitch(open, 0.45, 0.58), after = medianPitch(lifted, 0.45, 0.58);
        QVERIFY2(std::fabs(cents(after, before)) < 50, qPrintable(QStringLiteral("%1 %2").arg(before).arg(after)));
    }

    void itSpeaksAcrossTheWayItIsPlayed() {
        // Drawn at 0.1–0.9 m/s with the page's grip anywhere from light to
        // firm, the head's own note (within 1.5 semitones of where a medium
        // stroke puts it) speaks in most of the map, open and pressed: the
        // player's hand grips as the speed needs, as on any friction
        // instrument.
        auto played = [](float r, float force, float v, float g) {
            cuica::Body b;
            b.init(kRate);
            if (force > 0)
                b.press(2, r, force, 0);
            const double dur = std::min(0.4, 0.75 * 0.8 * cuica::Physics::kStickLength / v);
            float along = 0.02f;
            double next = 0.05;
            bool gripped = false;
            const auto x = run(b, 0.05 + dur, [&](double t) {
                if (!gripped && t >= 0.05) {
                    b.grip(1, along, g, 0);
                    gripped = true;
                } else if (gripped && t >= next) {
                    along += v / (0.8f * cuica::Physics::kStickLength) * 0.005f;
                    b.slide(1, along, g);
                    next = t + 0.005;
                }
            });
            return medianPitch(x, 0.05 + std::min(0.1, 0.3 * dur), 0.05 + dur - 0.03);
        };
        auto share = [&](float r, float force) {
            const double ref = played(r, force, 0.4f, 0.5f);
            int good = 0, all = 0;
            for (float v : {0.1f, 0.15f, 0.2f, 0.3f, 0.4f, 0.5f, 0.7f, 0.9f})
                for (float g : {0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f}) {
                    const double f = played(r, force, v, g);
                    good += f > 0 && std::fabs(cents(f, ref)) < 150;
                    ++all;
                }
            return double(good) / all;
        };
        const double open = share(0, 0), pressed = share(0.0f, 0.7f), mid = share(0.1f, 0.6f);
        // (the rest is the band's edges: too light for the speed and it
        // slides silently or rattles; too firm for it and it creaks low)
        QVERIFY2(open > 0.45 && pressed > 0.45 && mid > 0.45,
                 qPrintable(QStringLiteral("%1 %2 %3").arg(open).arg(pressed).arg(mid)));
    }

    void nothingFizzesUpAtTheTop() {
        // No energy near the step's own limit: above 16 kHz stays 50 dB
        // under the whole, open and pressed.
        for (float force : {0.0f, 1.0f}) {
            const auto x = strokes(4, 0.06f, 0.12f, 0.6f, 0, force, force, 0.8);
            Svf a, b;
            a.set(16000.0f, 0.7f, kRate);
            b.set(16000.0f, 0.7f, kRate);
            double hi = 0, all = 0;
            for (float v : x) {
                const float y = b.step(a.step(v).high).high;
                hi += double(y) * y;
                all += double(v) * v;
            }
            QVERIFY2(10 * std::log10(hi / all) < -50, qPrintable(QString::number(10 * std::log10(hi / all))));
        }
    }

    // --- nothing it shouldn't -------------------------------------------------------

    void nothingSoundsUntilTheClothMoves() {
        // A hand holding the stick still, and a thumb pressing the head:
        // no ghost of a note. Then the hand lets go and it sleeps.
        cuica::Body b;
        b.init(kRate);
        b.grip(1, 0.4f, 0.8f, 0);
        b.press(2, 0.1f, 0.8f, 0);
        const auto x = run(b, 0.6, [&](double t) {
            if (t > 0.4) {
                b.letGo(1);
                b.lift(2);
            }
        });
        // (the thumb coming down makes a soft thud, and then nothing)
        QVERIFY2(rms(x, 0.2, 0.4) < 1e-4, qPrintable(QString::number(rms(x, 0.2, 0.4))));
        QVERIFY2(rms(x, 0.0, 0.2) < 0.01, qPrintable(QString::number(rms(x, 0.0, 0.2))));
        run(b, 2.0, [](double) {});
        QVERIFY(!b.active());
    }

    void aStrokeEndsAndTheHeadFallsQuiet() {
        // Once the cloth stops the head rings on only for a moment (the
        // skin and the shell's air damp it within a few tenths).
        cuica::Body b;
        b.init(kRate);
        const auto x = strokes(1, 0.06f, 0.12f, 0.7f, 0, 0, 0, 1.5, &b);
        const double during = rms(x, 0.08, 0.15), after = rms(x, 0.45, 0.5);
        QVERIFY2(20 * std::log10(after / during) < -40, qPrintable(QStringLiteral("%1 %2").arg(during).arg(after)));
        run(b, 1.0, [](double) {});
        QVERIFY(!b.active());
    }

    void handsAndThumbsNeverRunAway() {
        // Hands and thumbs everywhere at once, jerking about for seconds:
        // finite, bounded, quiet once they leave.
        cuica::Body b;
        b.init(kRate);
        Rng rng(7);
        for (int h = 0; h < 6; ++h)
            b.grip(10 + h, rng.unit(), rng.unit(), 0);
        for (int t = 0; t < 4; ++t)
            b.press(20 + t, rng.unit(), rng.unit(), 0);
        float top = 0;
        bool finite = true;
        const auto x = run(b, 5.0, [&](double t) {
            if (t < 4.0) {
                for (int h = 0; h < 6; ++h)
                    b.slide(10 + h, rng.unit(), rng.unit());
                for (int k = 0; k < 4; ++k)
                    b.movePress(20 + k, rng.unit(), rng.unit());
            } else {
                for (int h = 0; h < 6; ++h)
                    b.letGo(10 + h);
                for (int k = 0; k < 4; ++k)
                    b.lift(20 + k);
            }
        });
        for (float v : x) {
            finite &= std::isfinite(v);
            top = std::max(top, std::fabs(v));
        }
        QVERIFY(finite);
        // (hands flung about at 3 m/s and gripping as hard as a hand can:
        // louder than anyone plays it, under 0 dBFS once the drums' trim of
        // 0.45 is on it)
        QVERIFY2(top < 2.5f, qPrintable(QString::number(top)));
        QVERIFY(rms(x, 4.7, 5.0) < 1e-3);
    }

    // --- the kit ---------------------------------------------------------------------

    void everyPadIsAStroke() {
        // Keys and grooves play strokes: each sounds, none clips, each ends.
        struct Ctx : AudioContext {
            Transport tr;
            Harmony h;
            float duckBuf[kBlock] = {};
            const Transport &transport() const override { return tr; }
            const Harmony &harmony() const override { return h; }
            double now() const override { return 1.0; }
            int offsetOf(double, int) const override { return 0; }
            void pump(int, float) override {}
            void setChord(int, const int *, int) override {}
            const float *duck() const override { return duckBuf; }
        } ctx;
        for (int pad = 0; pad < drums::PadCount; ++pad) {
            drums::Engine e;
            e.init(kRate);
            drums::Strike s;
            s.kit = drums::Cuica;
            s.pad = pad;
            s.vel = 0.9f;
            e.strike(s, ctx.h);
            float l[kBlock], r[kBlock], top = 0;
            for (int b = 0; b < int(3 * kRate) / kBlock; ++b) {
                std::fill(l, l + kBlock, 0.0f);
                std::fill(r, r + kBlock, 0.0f);
                e.render(l, r, kBlock, ctx);
                for (int i = 0; i < kBlock; ++i)
                    top = std::max(top, std::fabs(l[i]));
            }
            QVERIFY2(top > 0.01f && top < 0.6f, qPrintable(QStringLiteral("%1 %2").arg(drums::padName(pad, drums::Cuica)).arg(top)));
            QVERIFY2(e.voices() == 0, drums::padName(pad, drums::Cuica));
        }
        // The top stroke reaches the cuíca's high register (recorded pressed
        // passages 9–10 semitones over the open tone), and starts there: the
        // thumb is down first.
        drums::Engine e;
        e.init(kRate);
        drums::Strike s;
        s.kit = drums::Cuica;
        s.pad = drums::Bell;
        s.vel = 1.0f;
        e.strike(s, ctx.h);
        std::vector<float> x;
        float l[kBlock], r[kBlock];
        for (int b = 0; b < int(0.4 * kRate) / kBlock; ++b) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
            x.insert(x.end(), l, l + kBlock);
        }
        // (its first 30 ms of sound against the whole)
        double start = 0.02;
        while (start < 0.15 && pitchAt(x, start) <= 0)
            start += 0.005;
        const double first = medianPitch(x, start, start + 0.03);
        const double top = medianPitch(x, 0.03, 0.14);
        QVERIFY2(top > 190 * std::exp2(9.0 / 12), qPrintable(QString::number(top)));
        QVERIFY2(first > 0 && std::fabs(cents(first, top)) < 200, qPrintable(QStringLiteral("%1 %2").arg(first).arg(top)));
    }

    void thePadIsTheCuica() {
        // A finger drawn back and forth along the stick sounds; the same
        // finger resting doesn't; a second finger on the head raises it.
        const std::string keys = cuicaKeys();
        auto take = [&](const std::string &body) { return testkit::play("page 0 1\n" + keys + body); };
        std::string rub = "down 0.5 1 0.5 0.53 p=0.7\n";
        for (int k = 0; k < 6; ++k)
            rub += fmt("glide %.2f %.2f 1 %.2f 0.53 p=0.7\n", 0.5 + 0.2 * k, 0.7 + 0.2 * k, k % 2 ? 0.5 : 0.25);
        const auto rubbed = take(rub + "up 1.7 1\nend 1.8\n");
        QVERIFY(rubbed.finite());
        QVERIFY2(rubbed.rms(0.6, 1.6) > 3e-3, qPrintable(QString::number(rubbed.rms(0.6, 1.6))));
        QVERIFY(rubbed.peak() < 1.0);
        const auto still = take("down 0.5 1 0.5 0.53 p=0.7\nup 1.7 1\nend 1.8\n");
        QVERIFY2(still.rms(0.6, 1.6) < 3e-4, qPrintable(QString::number(still.rms(0.6, 1.6))));
        // The thumb on the knot, pressing.
        const auto thumbed = take(rub + "down 0.45 2 0.86 0.53 p=0.9\nup 1.7 1\nup 1.7 2\nend 1.8\n");
        std::vector<float> a(rubbed.left.begin(), rubbed.left.end()), b(thumbed.left.begin(), thumbed.left.end());
        const double fa = medianPitch(a, 0.6, 1.6), fb = medianPitch(b, 0.6, 1.6);
        QVERIFY2(fb > fa * 1.25, qPrintable(QStringLiteral("%1 %2").arg(fa).arg(fb)));
    }

    void livePushAndPullHoldTheNote() {
        // The page's own gesture: a finger flicked back and forth along the
        // stick in 55 ms eased glides, pairs four times a second, through
        // the whole studio. Each stroke sounds the head's open note (within
        // 1.5 semitones) and holds it, within half a semitone, rather than
        // sagging into its turn.
        std::string s = "page 0 1\n" + cuicaKeys() + "down 0.5 1 0.300 0.53 p=0.6\n";
        std::vector<double> starts;
        for (int k = 0; k < 4; ++k) {
            const double a = 0.55 + 0.25 * k, b = a + 0.06;
            s += fmt("glide %.3f %.3f 1 0.420 0.53 p=0.6\nglide %.3f %.3f 1 0.300 0.53 p=0.6\n", a, a + 0.055, b, b + 0.055);
            starts.push_back(a);
            starts.push_back(b);
        }
        s += "up 1.55 1\nend 1.8\n";
        const auto take = testkit::play(s);
        QVERIFY(take.finite());
        std::vector<float> x(take.left.size());
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.5f * (take.left[i] + take.right[i]);
        std::vector<double> all;
        double rise = 0;
        int rises = 0;
        for (double a : starts) {
            const double f = medianPitch(x, a, a + 0.055);
            if (f > 0)
                all.push_back(f);
            const double head = medianPitch(x, a, a + 0.02), tail = medianPitch(x, a + 0.025, a + 0.045);
            if (head > 0 && tail > 0) {
                rise += cents(tail, head);
                ++rises;
            }
        }
        QVERIFY2(all.size() >= 6, qPrintable(QString::number(all.size())));
        std::sort(all.begin(), all.end());
        const double f = all[all.size() / 2];
        QVERIFY2(std::fabs(cents(f, 190)) < 150, qPrintable(QString::number(f)));
        QVERIFY2(rises >= 4 && rise / rises > -60, qPrintable(QStringLiteral("%1 c over %2").arg(rise / std::max(1, rises)).arg(rises)));
    }

    void aLoopOfTheCuicaKeepsTheCuica() {
        // What the fingers played goes into a loop (Space) and plays back
        // as the cuíca after the page has gone to another kit.
        std::string s = "page 0 1\n" + cuicaKeys() + "down 0.5 1 0.5 0.53 p=0.8\n";
        for (int k = 0; k < 8; ++k)
            s += fmt("glide %.2f %.2f 1 %.2f 0.53 p=0.8\n", 0.5 + 0.2 * k, 0.7 + 0.2 * k, k % 2 ? 0.5 : 0.25);
        s += "up 2.1 1\nkey 2.2 Space\nkey 2.4 Right\nend 9\n";
        const auto take = testkit::play(s);
        QVERIFY(take.finite());
        QVERIFY2(take.rms(4, 9) > 1e-3, qPrintable(QString::number(take.rms(4, 9))));
    }

    // --- the cost -----------------------------------------------------------------

    void theCuicaCostsLittleAndBarelyMoreForMoreFingers() {
        // One body sounds whatever plays it: a hand and a thumb cost well
        // under a percent of a core, and six hands with four thumbs not
        // much more.
        auto timed = [](int hands, int thumbs) {
            cuica::Body b;
            b.init(kRate);
            for (int h = 0; h < hands; ++h)
                b.grip(h, 0.2f + 0.05f * h, 0.6f, 0);
            for (int t = 0; t < thumbs; ++t)
                b.press(100 + t, 0.1f * t, 0.5f, 0);
            float along = 0.2f, dir = 1;
            float l[kBlock], r[kBlock];
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < int(4 * kRate) / kBlock; ++k) {
                along += dir * 1.2f * kBlock / kRate;
                if (along > 0.7f || along < 0.2f)
                    dir = -dir;
                for (int h = 0; h < hands; ++h)
                    b.slide(h, along + 0.02f * h, 0.6f);
                b.render(l, r, kBlock);
            }
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        };
        const double one = timed(1, 1), many = timed(6, 4);
        QVERIFY2(one < 0.012, qPrintable(QString::number(one)));
        QVERIFY2(many < 3 * one + 0.004, qPrintable(QStringLiteral("%1 %2").arg(one).arg(many)));
    }

    void tenFingersOnTheCuicaStayInsideBudget() {
        // Ten fingers on the cuíca through the whole studio, as the other
        // pages' budgets are measured.
        std::string s = "page 0 1\n" + cuicaKeys();
        for (int f = 0; f < 10; ++f) {
            const bool thumb = f >= 7;
            const float x = thumb ? 0.8f + 0.03f * float(f - 7) : 0.1f + 0.08f * float(f);
            s += fmt("down %.3f %d %.3f 0.5 p=0.8\n", 0.3 + 0.01 * f, 100 + f, x);
            for (int k = 0; k < 10; ++k)
                s += fmt("glide %.3f %.3f %d %.3f %.3f p=0.8\n", 0.4 + 0.5 * k, 0.9 + 0.5 * k, 100 + f,
                         thumb ? x : x + (k % 2 ? 0.0f : 0.1f), thumb ? 0.5f + 0.02f * float(k % 3) : 0.5f);
        }
        s += "end 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(CuicaTests)
#include "cuica_tests.moc"
