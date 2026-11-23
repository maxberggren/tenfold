// The khishba kit: the long Iraqi goblet drum as physics (khishba.h),
// checked against what is measured and calculated for it and its relatives
// (docs/khishba.md): its skin's modes and the air's load on them, the air
// column and its coupling to the skin both ways, the dum against the tek,
// the fingers, a finger left lying on it, rolls, the pad, and what it costs.

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
    Ctx() { h.tuning.set(2, 0, 4, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 1.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// The sound of a drum (both ears summed) for `seconds`, after `act` has
// played on it; a fresh one unless one is given.
std::vector<float> listen(khishba::Body &b, double seconds) {
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

// One stroke on a fresh drum: which, how hard, where (head radii).
std::vector<float> stroke(khishba::Stroke s, float vel, float x, float y, double seconds = 0.8, bool air = true) {
    static khishba::Body b;
    static bool built = false;
    if (!built) {
        b.init(kRate);
        built = true;
    }
    b.setAir(air);
    b.silence();
    b.strike(s, vel, x, y, 0, 0);
    auto out = listen(b, seconds);
    b.setAir(true);
    return out;
}

std::vector<float> dum(float vel = 0.7f, bool air = true) { return stroke(khishba::Dum, vel, 0.05f, 0.0f, 0.8, air); }
std::vector<float> tek(float vel = 0.7f) { return stroke(khishba::Tek, vel, 0.9f, -0.1f); }

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

// The strongest peak between lo and hi Hz, and its level.
std::pair<double, double> peakIn(const std::vector<float> &x, double from, double len, double lo, double hi) {
    double best = lo, top = -1e9;
    for (double f = lo; f <= hi; f *= 1.003) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    return {best, top};
}

// The spectral centroid (Hz) over a window, from 50 Hz to 12 kHz.
double centroid(const std::vector<float> &x, double from, double len) {
    double num = 0, den = 0;
    for (double f = 50; f < 12000; f *= 1.02) {
        const double p = std::pow(10.0, levelAt(x, from, len, f) / 10) * f; // per log band: × f
        num += p * f;
        den += p;
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

double db(double v) { return 20 * std::log10(v + 1e-15); }

// T60 from the energy decay curve (Schroeder), extrapolated from −5 … −25 dB.
double t60(const std::vector<float> &x) {
    double total = 0;
    for (float v : x)
        total += double(v) * v;
    double rest = total, t5 = -1, t25 = -1;
    for (size_t i = 0; i < x.size(); ++i) {
        rest -= double(x[i]) * x[i];
        const double d = 10 * std::log10(rest / total + 1e-30);
        if (t5 < 0 && d < -5)
            t5 = double(i) / kRate;
        if (t25 < 0 && d < -25) {
            t25 = double(i) / kRate;
            break;
        }
    }
    return 3 * (t25 - t5);
}

// The page on the khishba: Right from house as many times as it is kits along.
std::string toKhishba() {
    std::string s = "page 0 1\n";
    for (int i = 0; i < drums::Khishba; ++i)
        s += fmt("key %.3f Right\n", 0.1 + 0.004 * i);
    return s;
}

}

class KhishbaTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenFingersOnTheKhishbaStayInsideBudget")
private slots:

    // --- the skin ----------------------------------------------------------------

    void theSkinIsAnIdealMembraneInAVacuum() {
        // Bessel's ratios, 1 : 1.593 : 2.136 : 2.295 : 2.653 : 2.917, and a
        // (0,1) near the 600 Hz a 9.5 cm head at this tension gives.
        khishba::Body b;
        b.init(kRate);
        const float f01 = b.vacuumHz(b.find(0, 1));
        QVERIFY2(f01 > 520 && f01 < 680, qPrintable(QString::number(f01)));
        const int modes[5][2] = {{1, 1}, {2, 1}, {0, 2}, {3, 1}, {1, 2}};
        const double ideal[5] = {1.5933, 2.1355, 2.2954, 2.6531, 2.9173};
        for (int i = 0; i < 5; ++i) {
            const int k = b.find(modes[i][0], modes[i][1]);
            QVERIFY(k >= 0);
            // (a pair is split by the skin's unevenness, under a percent)
            const double ratio = b.vacuumHz(k) / f01;
            QVERIFY2(std::fabs(ratio / ideal[i] - 1) < 0.006, qPrintable(QStringLiteral("%1 %2").arg(i).arg(ratio)));
        }
        // A pair's two halves a few tenths of a percent apart: a ring that beats.
        const int k11 = b.find(1, 1);
        const double split = b.modeHz(k11 + 1) / b.modeHz(k11) - 1;
        QVERIFY2(std::fabs(split) > 0.0005 && std::fabs(split) < 0.01, qPrintable(QString::number(split)));
    }

    void theAirLoadsTheLowModesMost() {
        // On a head this small, light skin or not, the air's mass lowers
        // (1,1) by about a tenth and the higher modes less (the timpani's
        // measured loading scaled to μ = ρa/σ: ×0.87 … 0.91, ×0.91 … 0.94):
        // far from a timpano's ×0.66, so the ratios stay a membrane's.
        khishba::Body b;
        b.init(kRate);
        double last = 0;
        const int modes[4][2] = {{1, 1}, {2, 1}, {3, 1}, {4, 1}};
        for (int i = 0; i < 4; ++i) {
            const int k = b.find(modes[i][0], modes[i][1]);
            const double f = 0.5 * (b.modeHz(k) + b.modeHz(k + 1)), shift = f / b.vacuumHz(k);
            QVERIFY2(shift > 0.85 && shift < 0.99, qPrintable(QStringLiteral("%1 %2").arg(i).arg(shift)));
            QVERIFY(shift > last);
            last = shift;
        }
        const int k11 = b.find(1, 1);
        QVERIFY(b.modeHz(k11) / b.vacuumHz(k11) < 0.95);
    }

    void eachModeLosesWhatItRadiatesAndWhatTheSkinTakes() {
        // The skin's own loss (fish skin, Q ≈ 20–40) and radiation: the
        // monopole (0,1) radiates fastest (its radiation Q alone ≈ 30 for a
        // 0.17 kg/m² skin), the lobed modes barely; nothing rings a second.
        khishba::Body b;
        b.init(kRate);
        const int k01 = b.find(0, 1), k11 = b.find(1, 1), k21 = b.find(2, 1);
        auto q = [&](int k) { return kPi * b.modeHz(k) * b.modeTau(k); };
        QVERIFY2(q(k01) > 8 && q(k01) < 30, qPrintable(QString::number(q(k01))));
        QVERIFY2(q(k11) > 12 && q(k11) < 40, qPrintable(QString::number(q(k11))));
        QVERIFY(q(k21) > 10 && q(k21) < 40);
        for (int k = 0; k < b.modes(); ++k)
            QVERIFY(b.modeTau(k) < 0.15);
    }

    // --- the column --------------------------------------------------------------

    void theColumnIsAShallowCupOnALongNeck() {
        // Webster's equation for the bore, the skin held rigid: the cup's air
        // on the neck's plug low (115–180 Hz for 0.2–0.45 L on a 3.5 cm
        // bore), then the long tube's next mode near its three-quarter wave
        // (~690 Hz for 35.5 cm), each lossy at the walls and the foot.
        khishba::Body b;
        b.init(kRate);
        QVERIFY(b.airModes() >= 3);
        QVERIFY2(b.airHz(0) > 115 && b.airHz(0) < 185, qPrintable(QString::number(b.airHz(0))));
        QVERIFY2(b.airHz(1) > 560 && b.airHz(1) < 760, qPrintable(QString::number(b.airHz(1))));
        QVERIFY2(b.airHz(2) > 1000 && b.airHz(2) < 1300, qPrintable(QString::number(b.airHz(2))));
        for (int a = 0; a < b.airModes(); ++a)
            QVERIFY2(b.airQ(a) > 8 && b.airQ(a) < 120, qPrintable(QStringLiteral("%1 %2").arg(a).arg(b.airQ(a))));
    }

    void theDumsFundamentalSplitsAgainstTheTube() {
        // The skin's (0,1) sits by the tube's three-quarter-wave mode, and the
        // two repel: a dum rings two partials, ~570 and ~800 Hz (the coupled
        // model of the research gives 568 and 740 for a 600 Hz skin), not the
        // one the skin has alone.
        const auto with = dum(), without = dum(0.7f, false);
        const auto lo = peakIn(with, 0.005, 0.06, 500, 640), hi = peakIn(with, 0.005, 0.06, 680, 900);
        const auto alone = peakIn(without, 0.005, 0.06, 440, 800);
        QVERIFY2(std::fabs(lo.second - hi.second) < 15, qPrintable(QStringLiteral("%1 %2").arg(lo.second).arg(hi.second)));
        // Each a real peak: above the trough between them.
        const double trough = levelAt(with, 0.005, 0.06, std::sqrt(lo.first * hi.first));
        QVERIFY2(lo.second > trough + 3 && hi.second > trough + 3,
                 qPrintable(QStringLiteral("%1 %2 %3").arg(lo.second).arg(trough).arg(hi.second)));
        // Alone, the skin's (0,1) is one partial, between them, and nothing
        // where the upper one rings.
        QVERIFY2(alone.first > lo.first + 15 && alone.first < hi.first - 60,
                 qPrintable(QStringLiteral("%1 %2 %3").arg(lo.first).arg(alone.first).arg(hi.first)));
        QVERIFY(levelAt(without, 0.005, 0.06, hi.first) < hi.second - 10);
    }

    void theSkinAndTheColumnTradeEnergyBothWays() {
        // Strongly coupled (g/ω₁ω₂ well over a tenth); a dum sets the cup's
        // air going, which rings on, faint, after the skin has stopped: a
        // thump from the foot that isn't there without the column.
        khishba::Body b;
        b.init(kRate);
        QVERIFY2(b.airCoupling() > 0.3, qPrintable(QString::number(b.airCoupling())));
        const auto with = dum(), without = dum(0.7f, false);
        const auto boom = peakIn(with, 0.05, 0.25, 90, 170);
        const double none = levelAt(without, 0.05, 0.25, boom.first);
        QVERIFY2(boom.second > none + 25, qPrintable(QStringLiteral("%1 %2").arg(boom.second).arg(none)));
        // Pulled down from the rigid-skin cup mode by the skin's give.
        QVERIFY2(boom.first < b.airHz(0) - 10, qPrintable(QStringLiteral("%1 %2").arg(boom.first).arg(b.airHz(0))));
    }

    // --- strokes -----------------------------------------------------------------

    void theDumIsLowerAndRingsLongerThanTheTek() {
        // The khishba's "two tones a fifth apart" (Kubica): the dum's split
        // (0,1) against the tek's lobed modes, near the rim where the (0,1)
        // has almost no swing. The tek is short and dry.
        const auto d = dum(), t = tek();
        const double cd = centroid(d, 0, 0.05), ct = centroid(t, 0, 0.05);
        QVERIFY2(ct > 1.15 * cd, qPrintable(QStringLiteral("%1 %2").arg(cd).arg(ct)));
        QVERIFY2(cd > 450 && cd < 1000, qPrintable(QString::number(cd)));
        // Against the (0,1) pair, the tek's lobed modes ((1,1) and up) are
        // far stronger than the dum's.
        auto lobed = [](const std::vector<float> &x) {
            double lob = 0, pair = 0;
            for (double f = 450; f < 4000; f *= 1.005) {
                const double p = std::pow(10.0, levelAt(x, 0.002, 0.04, f) / 10);
                if (f > 500 && f < 640)
                    pair += p;
                else if (f > 940)
                    lob += p;
            }
            return 10 * std::log10(lob / pair);
        };
        QVERIFY2(lobed(t) > lobed(d) + 10, qPrintable(QStringLiteral("%1 %2").arg(lobed(t)).arg(lobed(d))));
        // The dum rings longer.
        const double td = t60(d), tt = t60(t);
        QVERIFY2(tt < 0.12 && td > tt, qPrintable(QStringLiteral("%1 %2").arg(td).arg(tt)));
        QVERIFY2(td < 0.6, qPrintable(QString::number(td)));
    }

    void theBoomIsAFaintThumpNotABass() {
        // Nobody hears a bass in a khishba ("a high, dry tone"): below
        // 300 Hz the dum's first tenth of a second is well under its head.
        const auto d = dum();
        double lo = 0, hi = 0;
        for (double f = 50; f < 10000; f *= 1.02) {
            const double p = std::pow(10.0, levelAt(d, 0, 0.1, f) / 10) * f;
            (f < 300 ? lo : hi) += p;
        }
        QVERIFY2(10 * std::log10(lo / (lo + hi)) < -2, qPrintable(QString::number(10 * std::log10(lo / (lo + hi)))));
    }

    void theFingerTouchesForAMillisecondOrTwo() {
        // Solved against the skin's own motion, the pulp's and the bone's:
        // a hard tek at the rim touches under a millisecond, a soft one a
        // few (the pulp gives, the hand drifts off); a hard dum's wrist snaps
        // its fingers back sooner than a soft one's.
        khishba::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock];
        auto contact = [&](khishba::Stroke s, float vel, float x) {
            b.silence();
            b.strike(s, vel, x, 0, 0, 0);
            for (int i = 0; i < 100; ++i)
                b.render(l, r, kBlock);
            return b.lastContact();
        };
        const float tekSoft = contact(khishba::Tek, 0.15f, 0.93f), tekHard = contact(khishba::Tek, 1.0f, 0.93f);
        const float dumSoft = contact(khishba::Dum, 0.15f, 0.0f), dumHard = contact(khishba::Dum, 1.0f, 0.0f);
        QVERIFY2(tekHard > 0.0002f && tekHard < 0.001f && tekSoft > tekHard && tekSoft < 0.0035f,
                 qPrintable(QStringLiteral("%1 %2").arg(tekSoft).arg(tekHard)));
        QVERIFY2(dumHard > tekHard && dumHard < dumSoft && dumSoft < 0.0035f,
                 qPrintable(QStringLiteral("%1 %2").arg(dumSoft).arg(dumHard)));
    }

    void harderIsLouderAndBrighter() {
        // The pulp gives way to the bone: brightness grows with velocity.
        const auto soft = tek(0.25f), hard = tek(1.0f);
        const double ls = db(rms(soft, 0, 0.05)), lh = db(rms(hard, 0, 0.05));
        QVERIFY2(lh > ls + 8, qPrintable(QStringLiteral("%1 %2").arg(ls).arg(lh)));
        QVERIFY2(centroid(hard, 0, 0.05) > centroid(soft, 0, 0.05), "brighter");
        const auto ds = dum(0.25f), dh = dum(1.0f);
        QVERIFY(db(rms(dh, 0, 0.1)) > db(rms(ds, 0, 0.1)) + 8);
    }

    void aHardDumStretchesTheSkin() {
        // Kirchhoff–Carrier: the skin's mean strain raises its tension while
        // it swings far, a few per cent on a hard dum, a tiny amount on a soft one.
        khishba::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock];
        auto stretch = [&](float vel) {
            b.silence();
            b.strike(khishba::Dum, vel, 0.0f, 0.0f, 0, 0);
            float top = 1;
            for (int i = 0; i < 20; ++i) {
                b.render(l, r, kBlock);
                top = std::max(top, b.tensionNow());
            }
            return top;
        };
        const float soft = stretch(0.15f), hard = stretch(1.0f);
        QVERIFY2(hard > 1.003f && hard < 1.3f, qPrintable(QString::number(hard)));
        QVERIFY2(soft < 1.0f + 0.2f * (hard - 1.0f), qPrintable(QStringLiteral("%1 %2").arg(soft).arg(hard)));
    }

    void aTekAtTheRimIsShortAndBright() {
        // Near the clamped edge the skin is stiff under the fingertip: the
        // finger is thrown back sooner, the stroke reaches higher modes and
        // leaves the (0,1) family alone; a third of the way in it sinks in
        // longer and the dum's partials answer.
        khishba::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock];
        auto contact = [&](float x) {
            b.silence();
            b.strike(khishba::Tek, 0.8f, x, 0, 0, 0);
            for (int i = 0; i < 100; ++i)
                b.render(l, r, kBlock);
            return b.lastContact();
        };
        QVERIFY2(contact(0.92f) < 0.6f * contact(0.6f), qPrintable(QStringLiteral("%1 %2").arg(contact(0.92f)).arg(contact(0.6f))));
        const auto rim = stroke(khishba::Tek, 0.8f, 0.92f, 0.0f), in = stroke(khishba::Tek, 0.8f, 0.6f, 0.0f);
        QVERIFY2(centroid(rim, 0, 0.05) > 1.2 * centroid(in, 0, 0.05),
                 qPrintable(QStringLiteral("%1 %2").arg(centroid(rim, 0, 0.05)).arg(centroid(in, 0, 0.05))));
        // And a knuckle on the body is the wood alone: its ring modes, no skin's.
        const auto knock = stroke(khishba::Wood, 0.8f, 1.3f, 0.0f);
        QVERIFY(rms(knock, 0, 0.05) > 1e-5);
        QVERIFY(peakIn(knock, 0.002, 0.04, 500, 760).second < peakIn(knock, 0.002, 0.04, 900, 4000).second - 20);
    }

    // --- the other hand ----------------------------------------------------------

    void aFingerLyingOnTheSkinDampsIt() {
        khishba::Body b;
        b.init(kRate);
        auto ring = [&](float rx, float press) {
            b.silence();
            b.strike(khishba::Dum, 0.8f, 0.05f, 0.0f, 0, 0);
            if (press > 0)
                b.rest(1, rx, 0.0f, press, int(0.01f * kRate));
            const auto out = listen(b, 0.3);
            b.lift(1);
            return db(rms(out, 0.08, 0.2));
        };
        const double open = ring(0, 0), middle = ring(0.0f, 0.8f), edge = ring(-0.92f, 0.8f);
        // In the middle it chokes the dum; lying at the rim it takes less.
        QVERIFY2(middle < open - 10, qPrintable(QStringLiteral("%1 %2").arg(open).arg(middle)));
        QVERIFY2(edge < open - 0.5 && edge > middle + 3, qPrintable(QStringLiteral("%1 %2 %3").arg(open).arg(edge).arg(middle)));
        // Lifted, the skin rings freely again.
        b.silence();
        b.strike(khishba::Dum, 0.8f, 0.05f, 0.0f, 0, 0);
        QVERIFY(std::fabs(db(rms(listen(b, 0.3), 0.08, 0.2)) - open) < 1.0);
        QCOMPARE(b.resting(), 0);
    }

    void aRollNeverPumpsEnergyIn() {
        // Two fingers alternating at 20 strokes a second (rash) for two
        // seconds: steady, finite, never growing.
        khishba::Body b;
        b.init(kRate);
        std::vector<float> out;
        float l[kBlock], r[kBlock];
        const int every = int(kRate / 20) / kBlock;
        for (int i = 0; i < int(2 * kRate) / kBlock; ++i) {
            if (i % every == 0)
                b.strike((i / every) % 2 ? khishba::Ka : khishba::Tek, 0.6f, (i / every) % 2 ? -0.88f : 0.88f, -0.1f, 0, 0);
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            b.render(l, r, kBlock);
            for (int j = 0; j < kBlock; ++j) {
                QVERIFY(std::isfinite(l[j]) && std::isfinite(r[j]));
                out.push_back(l[j]);
            }
        }
        const double early = rms(out, 0.2, 0.6), late = rms(out, 1.5, 1.9);
        QVERIFY2(std::fabs(db(late) - db(early)) < 3, qPrintable(QStringLiteral("%1 %2").arg(db(early)).arg(db(late))));
    }

    // --- the pad -----------------------------------------------------------------

    void thePadIsTheKhishbasHead() {
        // A dum in the middle, a tek at the right of the rim, a ka at the
        // left, the wood in a corner.
        auto struck = [](float x, float y) {
            const auto take = testkit::play(toKhishba() + fmt("tap 0.5 %.3f %.3f p=0.7\nend 0.56\n", x, y));
            int best = -1;
            float top = 0.2f;
            for (int p = 0; p < drums::PadCount; ++p)
                if (take.view.parts[0].data[p] > top)
                    top = take.view.parts[0].data[p], best = p;
            return best < 0 ? -1 : int(drums::khishbaStroke(best));
        };
        QCOMPARE(struck(0.5f, 0.535f), int(khishba::Dum));
        QCOMPARE(struck(0.86f, 0.5f), int(khishba::Tek));
        QCOMPARE(struck(0.14f, 0.5f), int(khishba::Ka));
        QCOMPARE(struck(0.03f, 0.95f), int(khishba::Wood));
        const auto take = testkit::play(toKhishba() + "tap 0.5 0.5 0.535 p=0.7\nend 1\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(0.5, 0.7) > 1e-3);
        QVERIFY(take.peak() < 1.0);
    }

    void aFingerLeftOnThePadDampsTheDrum() {
        // Tapped and lifted, the dum rings on; struck and left lying there,
        // the finger settles onto the skin and chokes it. (The drum's own
        // level, as the page draws it: the room's tail is the room's.)
        auto ringing = [](const std::string &how, double at) {
            const auto take = testkit::play(toKhishba() + how + fmt("end %.3f\n", at));
            return take.view.parts[0].data[200];
        };
        const float tapped = ringing("tap 0.5 0.5 0.535 p=0.7 len=0.05\n", 0.65);
        const float held = ringing("down 0.5 1 0.5 0.535 p=0.7\n", 0.65);
        QVERIFY2(tapped > 0.00002f, qPrintable(QString::number(tapped)));
        QVERIFY2(held < 0.3f * tapped, qPrintable(QStringLiteral("%1 %2").arg(held).arg(tapped)));
        // The stroke itself still sounds: the finger lies down only once it
        // has stayed longer than a tap.
        const float early = ringing("down 0.5 1 0.5 0.535 p=0.7\n", 0.54);
        QVERIFY(early > 0.5f * ringing("tap 0.5 0.5 0.535 p=0.7 len=0.05\n", 0.54));
    }

    void aFlatFingerPlaysAClosedStroke() {
        // A finger landing flat (a wide contact) stays on the skin as it
        // strikes: the pa, the sak, its ring choked within a few
        // milliseconds, even when it lifts at once, as a tap would.
        auto ringing = [](const char *size) {
            const auto take = testkit::play(toKhishba() + fmt("tap 0.5 0.5 0.535 p=0.7 size=%s len=0.06\nend 0.555\n", size));
            return take.view.parts[0].data[200];
        };
        const float open = ringing("1.0"), closed = ringing("2.0");
        QVERIFY2(open > 0.0005f, qPrintable(QString::number(open)));
        QVERIFY2(closed < 0.4f * open, qPrintable(QStringLiteral("%1 %2").arg(closed).arg(open)));
    }

    void fingersTogetherGrooveTheHachaa() {
        // The hacha'a, D . . . T . T . twice a bar: the dum on one and the
        // half bar, the tek's pair after each.
        const drums::Pattern d = drums::groovePattern(drums::khishbaPad(khishba::Dum), drums::Khishba, 0, 0);
        const drums::Pattern t = drums::groovePattern(drums::khishbaPad(khishba::Tek), drums::Khishba, 0, 0);
        QCOMPARE(d.count, 2);
        QCOMPARE(int(d.steps[0].tick), 0);
        QCOMPARE(int(d.steps[1].tick), 48);
        QCOMPARE(t.count, 4);
        QCOMPARE(int(t.steps[0].tick), 24);
        QCOMPARE(int(t.steps[1].tick), 36);
        // And on the pad: a dum and a tek landing together and staying groove.
        const auto take = testkit::play(toKhishba() + "down 0.5 1 0.5 0.535 p=0.6\ndown 0.5 2 0.86 0.5 p=0.6\n"
                                                      "up 3.5 1\nup 3.5 2\nend 4\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(2.0, 3.4) > 1e-3);
    }

    void aLoopOfTheKhishbaKeepsTheKhishba() {
        // Hits carry the kit: a khishba loop sounds as a khishba after the
        // page has gone on to house.
        const auto take = testkit::play(toKhishba() + "tap 0.6 0.5 0.535 p=0.7\ntap 0.9 0.86 0.5 p=0.7\n"
                                                      "key 1.2 Space\nkey 1.5 Right\nend 6\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(3, 6) > 1e-4);
    }

    // --- the cost ----------------------------------------------------------------

    void theKhishbaCostsTheSameForTenFingers() {
        // One skin rings whatever strikes it: ten strokes at once cost about
        // what one does, well under a percent and a half of one core.
        Ctx ctx;
        float top = 0;
        auto timed = [&](int fingers) {
            drums::Engine e;
            e.init(kRate);
            float l[kBlock], r[kBlock];
            top = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < int(4 * kRate) / kBlock; ++b) {
                if (b % 300 == 0)
                    for (int f = 0; f < fingers; ++f) {
                        drums::Strike s;
                        s.kit = drums::Khishba;
                        s.pad = drums::khishbaPad(f % 3);
                        s.vel = 0.9f;
                        s.sx = drums::khishbaSpot(f % 3 == 0 ? 0.0f : f % 3 == 1 ? 0.9f : -0.9f);
                        s.sy = 0.5f;
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
        QVERIFY2(top < 15 * oneTop, qPrintable(QStringLiteral("%1 %2").arg(oneTop).arg(top)));
        QVERIFY2(ten < 0.03, qPrintable(QString::number(ten)));
        QVERIFY2(ten < 3 * one + 0.002, qPrintable(QStringLiteral("%1 %2").arg(one).arg(ten)));
    }

    void tenFingersOnTheKhishbaStayInsideBudget() {
        // Ten fingers drumming on the khishba through the whole studio, as
        // the other pages' budgets are measured.
        TENFOLD_TIMED();
        std::string s = toKhishba();
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

TESTKIT_REGISTER(KhishbaTests)
#include "khishba_tests.moc"
