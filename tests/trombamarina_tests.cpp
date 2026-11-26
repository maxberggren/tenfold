// The tromba marina (trombamarina.h), checked as the acoustics describe a
// real one (docs/tromba-marina.md): the thumb that picks a harmonic of the
// open string, the bow's Helmholtz motion between Schelleng's limits and
// its scratch past them, the bridge whose free foot starts knocking on the
// board once the bow is firm enough (Padilla & Gibiat 2005), the long
// board's modes, that nothing runs away, and what it costs. Then the
// Lead's Tromba marina, played.

#include <chrono>
#include <cstdio>
#include <vector>

#include <QJsonArray>

#include "lead.h"
#include "offline.h"
#include "parts.h"
#include "testkit.h"
#include "trombamarina.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;
constexpr int kLead = 3;
constexpr int kTromba = 18; // the Lead's sound

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

// A string tuned to sound hz on its harmonic n, bowed steadily for
// `seconds`, the mono sum of what it sounds. Counts are reset at `count`.
std::vector<float> bowed(tromba::TrombaMarina &t, float hz, int n, float force, float speed, float beta, double seconds,
                         double count = 0.5) {
    t.init(kRate);
    t.setNote(hz, n, true);
    t.setBow(force, speed, beta);
    std::vector<float> out;
    float l[32], r[32];
    for (int b = 0; b < int(seconds * kRate) / 32; ++b) {
        if (b == int(count * kRate) / 32)
            t.resetCounts();
        std::fill(l, l + 32, 0.0f);
        std::fill(r, r + 32, 0.0f);
        t.render(l, r, 32, 1.0f);
        for (int i = 0; i < 32; ++i)
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

// The strongest peak within ±cents of hz.
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

// The power (dB) of the note's harmonics from lo to hi Hz, against its first.
double harmonicsOver(const std::vector<float> &x, double from, double len, double hz, double lo, double hi) {
    double p = 1e-30;
    for (int k = 1; k * hz < hi; ++k)
        if (k * hz >= lo)
            p += std::pow(10.0, levelAt(x, from, len, k * hz) / 10);
    return 10 * std::log10(p) - levelAt(x, from, len, hz);
}

// The power (dB) of a band, every 10 Hz from lo to hi, over a window: the
// knocks' energy wherever it lies (a bridge knocking twice a period, as
// they do, puts it between the note's harmonics too).
double band(const std::vector<float> &x, double from, double len, double lo, double hi) {
    double p = 1e-30;
    for (double f = lo; f < hi; f += 10)
        p += std::pow(10.0, levelAt(x, from, len, f) / 10);
    return 10 * std::log10(p);
}

std::string onSound(int snd, const std::string &rest) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < snd; ++k)
        s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
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

} // namespace

class TrombaMarinaTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs them with the machine to itself.
    Q_CLASSINFO("alone", "aStringStaysInsideBudget aLineWithHarmonyStaysInsideBudget")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "nothingRunsAway theLeadPlaysItsTrombaMarina")
private slots:
    void initTestCase() { tromba::prepare(); }

    void theNoteChoosesTheNearestHarmonic() {
        // On a string tuned to A1 (55 Hz) the neck's notes fall on its
        // harmonics, the nearest in pitch: A3 the 4th, E4 the 6th, A4 the
        // 8th, C♯5 the 10th, E5 the 12th; between two, the nearer.
        QCOMPARE(tromba::harmonicFor(220.0f, 55.0f), 4);
        QCOMPARE(tromba::harmonicFor(329.6f, 55.0f), 6);
        QCOMPARE(tromba::harmonicFor(440.0f, 55.0f), 8);
        QCOMPARE(tromba::harmonicFor(554.4f, 55.0f), 10);
        // (The 13th and up are beyond the thumb: their neighbours have
        // nearly a node there too. A5 is played on the 12th of a higher string.)
        QCOMPARE(tromba::harmonicFor(660.0f, 55.0f), 12);
        QCOMPARE(tromba::harmonicFor(880.0f, 55.0f), tromba::kMaxHarmonic);
        // (B3 is 4.49 × 55: 186 cents under the 5th, 200 over the 4th.)
        QCOMPARE(tromba::harmonicFor(246.9f, 55.0f), 5);
        QCOMPARE(tromba::harmonicFor(240.0f, 55.0f), 4);
        QCOMPARE(tromba::harmonicFor(60.0f, 55.0f), tromba::kMinHarmonic);
        QCOMPARE(tromba::harmonicFor(9000.0f, 55.0f), tromba::kMaxHarmonic);
    }

    void theThumbLeavesOnlyItsHarmonic() {
        // Bowed on the open string's 3rd, 4th, 6th, 8th and 12th harmonic:
        // the note sounds at n·f0, in tune, and the open string's other
        // modes (every k·f0 that is not a multiple of n), which the thumb
        // damps, stand at least 35 dB under it.
        for (int n : {3, 4, 6, 8, 12}) {
            tromba::TrombaMarina t;
            const float hz = 55.0f * float(n);
            const auto bw = tromba::bowFor(0.6f, 0.5f, 0.7f, n, 55.0f);
            const auto x = bowed(t, hz, n, bw.force, -bw.speed, bw.beta, 1.6);
            const double got = peakHz(x, 0.6, 1.0, hz, 40);
            QVERIFY2(std::fabs(cents(got, hz)) < 8, qPrintable(QStringLiteral("n=%1: %2 Hz").arg(n).arg(got)));
            const double note = levelAt(x, 0.6, 1.0, hz);
            double worst = -200;
            for (int k = 1; k < 3 * n; ++k)
                if (k % n)
                    worst = std::max(worst, levelAt(x, 0.6, 1.0, 55.0 * k) - note);
            QVERIFY2(worst < -35, qPrintable(QStringLiteral("n=%1: %2 dB").arg(n).arg(worst)));
            QCOMPARE(t.harmonic(), n);
            QVERIFY(std::fabs(t.openHz() - 55.0f) < 0.01f);
        }
    }

    void theBowSticksAndSlipsOncePerPeriod() {
        // Between Schelleng's limits the string is in its Helmholtz motion:
        // the hair lets go of it once a period of the note, and slides for
        // the share of the period that the bow's place is of the loop it
        // bows (β), sticking the rest.
        for (float beta : {0.12f, 0.2f, 0.3f}) {
            tromba::TrombaMarina t;
            const float F = 0.75f * tromba::TrombaMarina::maxForce(55.0f, 0.2f, beta);
            (void)bowed(t, 440.0f, 8, F, 0.2f, beta, 1.2);
            QVERIFY2(std::fabs(t.slipsPerSecond() / 440.0f - 1.0f) < 0.03f,
                     qPrintable(QStringLiteral("β %1: %2 slips a period").arg(beta).arg(t.slipsPerSecond() / 440.0f)));
            QVERIFY2(std::fabs(t.slipShare() - beta) < 0.35f * beta,
                     qPrintable(QStringLiteral("β %1: slides %2 of the period").arg(beta).arg(t.slipShare())));
        }
    }

    void tooMuchWeightScratches() {
        // Past Schelleng's most force the hair holds the string too long:
        // it no longer lets go once a period, and the note breaks into a
        // scratch, its harmonics no longer standing over the noise between.
        tromba::TrombaMarina good, bad;
        const auto bw = tromba::bowFor(0.6f, 0.5f, 0.7f, 6, 55.0f);
        const float fmax = tromba::TrombaMarina::maxForce(55.0f, bw.speed, bw.beta);
        const auto g = bowed(good, 330.0f, 6, bw.force, -bw.speed, bw.beta, 1.4);
        const auto b = bowed(bad, 330.0f, 6, 2.2f * fmax, -bw.speed, bw.beta, 1.4);
        QVERIFY2(std::fabs(good.slipsPerSecond() / 330.0f - 1.0f) < 0.03f, qPrintable(QString::number(good.slipsPerSecond())));
        QVERIFY2(std::fabs(bad.slipsPerSecond() / 330.0f - 1.0f) > 0.1f, qPrintable(QString::number(bad.slipsPerSecond())));
        auto between = [&](const std::vector<float> &x) {
            double h = 0, m = 0;
            for (int k = 1; k <= 8; ++k) {
                h += std::pow(10.0, levelAt(x, 0.6, 0.8, 330.0 * k) / 10);
                m += std::pow(10.0, levelAt(x, 0.6, 0.8, 330.0 * (k + 0.5)) / 10);
            }
            return 10 * std::log10(h / m);
        };
        QVERIFY2(between(g) > between(b) + 10, qPrintable(QStringLiteral("%1 %2").arg(between(g)).arg(between(b))));
    }

    void theBridgeKnocksOnceTheBowIsFirm() {
        // A light bow, under Schelleng's least force, breaks the Helmholtz
        // motion into slips of its own, or leaves its corner rounded, and
        // rocks the bridge too gently to close its 20 µm gap: no knock.
        // Firmer, past a threshold, the free foot comes down on the board
        // every period.
        // (At β 0.3 on a down-bow, the motion stays a clean Helmholtz
        // motion either side of the threshold: it is the force, not a
        // change of regime, that closes the gap.)
        const float fmax = tromba::TrombaMarina::maxForce(55.0f, 0.25f, 0.3f);
        std::vector<float> knocks, slips;
        for (float share : {0.08f, 0.15f, 0.5f, 0.85f}) {
            tromba::TrombaMarina t;
            (void)bowed(t, 440.0f, 8, share * fmax, -0.25f, 0.3f, 1.2);
            knocks.push_back(t.knocksPerSecond() / 440.0f);
            slips.push_back(t.slipsPerSecond() / 440.0f);
            QVERIFY(t.footLowest() > -1e-4f);
        }
        for (float s : slips)
            QVERIFY2(std::fabs(s - 1.0f) < 0.1f, qPrintable(QString::number(s)));
        QVERIFY2(knocks[0] < 0.05f && knocks[1] < 0.05f, qPrintable(QStringLiteral("%1 %2").arg(knocks[0]).arg(knocks[1])));
        QVERIFY2(knocks[2] > 0.9f && knocks[3] > 0.9f, qPrintable(QStringLiteral("%1 %2").arg(knocks[2]).arg(knocks[3])));
        // Both ways of the bow knock once it is firm (the shoe is
        // asymmetric, so not alike, but neither is mute).
        for (float dir : {1.0f, -1.0f}) {
            tromba::TrombaMarina t;
            (void)bowed(t, 440.0f, 8, 0.6f * tromba::TrombaMarina::maxForce(55.0f, 0.25f, 0.2f), dir * 0.25f, 0.2f, 1.2);
            QVERIFY2(t.knocksPerSecond() > 0.9f * 440.0f, qPrintable(QStringLiteral("%1: %2").arg(dir).arg(t.knocksPerSecond())));
        }
        // And through the player, over the middle of the neck, where the
        // trumpet parts were written: a light touch is a soft flageolet
        // with no knock, a firm one knocks every period and is brighter by
        // far over two kilohertz.
        for (float hz : {440.0f, 523.25f, 587.33f, 660.0f}) {
            const int n = tromba::harmonicFor(hz, 55.0f);
            auto play = [&](float press, tromba::TrombaMarina &t) {
                const auto bw = tromba::bowFor(press, 0.5f, 0.8f, n, hz / float(n));
                return bowed(t, hz, n, bw.force, -bw.speed, bw.beta, 1.2);
            };
            tromba::TrombaMarina soft, firm;
            const auto xs = play(0.1f, soft), xf = play(0.6f, firm);
            QVERIFY2(soft.knocksPerSecond() < 0.05f * hz, qPrintable(QStringLiteral("%1 Hz: %2").arg(hz).arg(soft.knocksPerSecond())));
            QVERIFY2(firm.knocksPerSecond() > 0.9f * hz, qPrintable(QStringLiteral("%1 Hz: %2").arg(hz).arg(firm.knocksPerSecond())));
            const double bs = band(xs, 0.6, 0.5, 2000.0, 8000.0), bf = band(xf, 0.6, 0.5, 2000.0, 8000.0);
            QVERIFY2(bf > bs + 10, qPrintable(QStringLiteral("%1 Hz: %2 %3").arg(hz).arg(bf).arg(bs)));
        }
    }

    void aSlideLandsOnTheNewHarmonic() {
        // The thumb moved to the next node under a steady bow: the mode it
        // left dies under it and the string takes up the new harmonic,
        // within 10 cents a few tenths of a second on, both ways of the bow
        // and up or down the series.
        // (Down to a divisor too, the 12th to the 6th and the 8th to the
        // 4th, where the thumb lands on one of the old note's nodes and
        // only the bow, eased as the thumb moves, lets it go.)
        const float pairs[][2] = {{392.0f, 440.0f}, {523.25f, 587.33f}, {587.33f, 659.26f}, {440.0f, 392.0f},
                                  {659.26f, 523.25f}, {659.26f, 329.63f}, {440.0f, 220.0f}};
        for (const auto &pr : pairs)
            for (float dir : {1.0f, -1.0f})
                for (float press : {0.25f, 0.55f}) {
                tromba::TrombaMarina t;
                t.init(kRate);
                const int n0 = tromba::harmonicFor(pr[0], 55.0f), n1 = tromba::harmonicFor(pr[1], 55.0f);
                t.setNote(pr[0], n0, true);
                std::vector<float> x;
                float l[32], r[32];
                for (int b = 0; b < int(1.3 * kRate) / 32; ++b) {
                    const bool after = b >= int(0.6 * kRate) / 32;
                    const float hz = after ? pr[1] : pr[0];
                    const int n = after ? n1 : n0;
                    t.setNote(hz, n);
                    const auto bw = tromba::bowFor(press, 0.5f, 0.7f, n, hz / float(n));
                    t.setBow(bw.force, dir * bw.speed, bw.beta);
                    std::fill(l, l + 32, 0.0f);
                    std::fill(r, r + 32, 0.0f);
                    t.render(l, r, 32, 1.0f);
                    for (int i = 0; i < 32; ++i)
                        x.push_back(l[i]);
                }
                const double got = peakHz(x, 0.9, 0.4, pr[1], 150);
                QVERIFY2(std::fabs(cents(got, pr[1])) < 10,
                         qPrintable(QStringLiteral("%1 → %2 (%3, %4): %5").arg(pr[0]).arg(pr[1]).arg(dir).arg(press).arg(got)));
                // And the old note has gone, not merely been joined.
                if (std::fmod(pr[0], pr[1]) > 1.0f) // (a divisor's harmonics include the old note)
                    QVERIFY2(levelAt(x, 0.9, 0.4, pr[0]) < levelAt(x, 0.9, 0.4, pr[1]) - 20,
                             qPrintable(QStringLiteral("%1 → %2 (%3, %4)").arg(pr[0]).arg(pr[1]).arg(dir).arg(press)));
                else
                    QVERIFY2(levelAt(x, 0.9, 0.4, pr[0]) < levelAt(x, 0.9, 0.4, pr[1]) + 3,
                             qPrintable(QStringLiteral("%1 → %2 (%3, %4)").arg(pr[0]).arg(pr[1]).arg(dir).arg(press)));
            }
    }

    void theLeadsAttackAndTurnLeaveItSpeaking() {
        // Bowed as the Lead bows it: its envelope (rising toward 1.2 with a
        // time constant of 80 ms, the attack below a press of 0.45), the
        // weight on first (2.5 times the envelope, to full), and at the end
        // of the stroke the bow turning round with a 10 ms time constant.
        // Over the neck (G3 to G5 in A minor), light to firm, both ways of
        // the bow, 0.3 s after the attack and after the turn: in its
        // Helmholtz motion (0.9 to 1.15 slips a period) in at least nine
        // cells of ten. (The rest catch in several slips a period, a known
        // weakness: docs/tromba-marina.md.)
        int off = 0, cells = 0;
        QStringList where;
        for (float hz : {196.0f, 261.63f, 329.63f, 392.0f, 440.0f, 493.88f, 523.25f, 587.33f, 659.26f, 783.99f})
            for (float press : {0.1f, 0.3f, 0.5f, 0.7f})
                for (float dir0 : {1.0f, -1.0f}) {
                    const int n = tromba::harmonicFor(hz, 55.0f);
                    tromba::TrombaMarina t;
                    t.init(kRate);
                    t.setNote(hz, n, true);
                    float l[32], r[32], dirS = dir0;
                    float after[2] = {};
                    for (int b = 0; b < int(1.7 * kRate) / 32; ++b) {
                        const float time = float(b) * 32.0f / kRate;
                        const float drawn = std::min(1.0f, 1.2f * (1.0f - std::exp(-time / 0.08f)));
                        const float weight = std::min(1.0f, 2.5f * drawn);
                        dirS += ((time < 1.0f ? dir0 : -dir0) - dirS) * (1.0f - std::exp(-32.0f / kRate / 0.01f));
                        const auto bw = tromba::bowFor(press, 0.5f, 0.7f, n, hz / float(n));
                        t.setBow(bw.force * weight, bw.speed * drawn * dirS, bw.beta);
                        if (b == int(0.4 * kRate) / 32 || b == int(1.35 * kRate) / 32)
                            t.resetCounts();
                        if (b == int(0.7 * kRate) / 32)
                            after[0] = t.slipsPerSecond() / hz;
                        if (b == int(1.65 * kRate) / 32)
                            after[1] = t.slipsPerSecond() / hz;
                        t.render(l, r, 32, 1.0f);
                    }
                    for (int k = 0; k < 2; ++k) {
                        ++cells;
                        if (after[k] > 1.15f || after[k] < 0.9f) {
                            ++off;
                            where << QStringLiteral("%1 Hz p%2 %3 %4: %5").arg(hz).arg(press).arg(dir0 > 0 ? "+" : "−").arg(k ? "turn" : "attack").arg(after[k]);
                        }
                    }
                }
        qInfo("%d of %d cells off the Helmholtz motion: %s", off, cells, qPrintable(where.join(QStringLiteral(", "))));
        QVERIFY2(off * 10 <= cells, qPrintable(QStringLiteral("%1 of %2").arg(off).arg(cells)));
    }

    void theBoardIsLongAndNarrow() {
        // The tapered board's modes start low (its wide end's first cross
        // mode, a couple of hundred hertz) and crowd closely, a few tens of
        // hertz apart at first, as a long strip's do.
        const auto &b = tromba::board();
        QVERIFY2(b.n >= 6, qPrintable(QString::number(b.n)));
        QVERIFY2(b.hz[0] > 100 && b.hz[0] < 250, qPrintable(QString::number(b.hz[0])));
        QVERIFY2(b.hz[1] - b.hz[0] < 80, qPrintable(QString::number(b.hz[1] - b.hz[0])));
        for (int k = 1; k < b.n; ++k)
            QVERIFY(b.hz[k] > b.hz[k - 1]);
    }

    void nothingRunsAway() {
        // Long and loud: the heaviest bow at its fastest, turning every
        // half second, while the note leaps about the harmonics and bends;
        // then nonsense (NaN, infinities) for every number it is given.
        // Everything it sounds stays finite and bounded, and once the bow
        // leaves it rings out and sleeps.
        tromba::TrombaMarina t;
        t.init(kRate);
        t.setNote(110.0f, 2, true);
        float l[32], r[32];
        float peak = 0;
        bool finite = true;
        const int blocks = int(20.0f * kRate) / 32;
        for (int b = 0; b < blocks; ++b) {
            const float time = float(b) * 32.0f / kRate;
            const int n = 2 + (b / 700) % 23;
            t.setNote(55.0f * float(n) * std::exp2(0.1f * std::sin(time)), n);
            t.setBow(20.0f, (b / 750) % 2 ? 2.0f : -2.0f, 0.03f + 0.4f * float((b / 300) % 5) / 4.0f);
            if (b % 997 == 0) {
                t.setNote(std::nanf(""), 3);
                t.setBow(std::nanf(""), INFINITY, -INFINITY);
            }
            std::fill(l, l + 32, 0.0f);
            std::fill(r, r + 32, 0.0f);
            t.render(l, r, 32, 1.0f);
            for (int i = 0; i < 32; ++i) {
                finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
                peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
            }
        }
        QVERIFY(finite);
        QVERIFY2(peak < 200.0f, qPrintable(QString::number(peak))); // pascals at a metre: loud, never runaway
        t.setBow(0, 0, 0.15f);
        for (int b = 0; b < int(4.0f * kRate) / 32 && t.active(); ++b)
            t.render(l, r, 32, 1.0f);
        QVERIFY(!t.active());
    }

    void theLeadPlaysItsTrombaMarina() {
        // Chosen with → past the Tam-tam, it says what it is for, saves with
        // a song by its name and comes back by it.
        testkit::reopen(onSound(kTromba, "end 0.5\n"), 0.3, [&](Session &, Session &opened, const QJsonObject &doc) {
            const auto settings =
                doc[QStringLiteral("parts")].toArray()[kLead].toObject()[QStringLiteral("settings")].toObject();
            QCOMPARE(settings[QStringLiteral("sound")].toString(), QStringLiteral("Tromba marina"));
            QVERIFY2(opened.pageStatus().contains(QStringLiteral("Tromba marina · baroque")), qPrintable(opened.pageStatus()));
        });
        // A held note on A3, A4 and E5: heard, finite, under full scale and
        // in tune.
        for (int cell : {0, 7, 11}) {
            const double want = cell == 0 ? 220.0 : cell == 7 ? 440.0 : 659.26;
            const auto take =
                testkit::play(onSound(kTromba, fmt("down 0.4 1 %.4f 0.5 p=0.6\nup 1.9 1\nend 2.3\n", cellX(cell))));
            QVERIFY2(take.finite() && take.peak() < 1.0, qPrintable(QString::number(take.peak())));
            QVERIFY2(take.rms(0.7, 1.8) > 0.02, qPrintable(QString::number(take.rms(0.7, 1.8))));
            const double got = peakHz(mono(take), 0.9, 0.9, want, 50);
            QVERIFY2(std::fabs(cents(got, want)) < 6, qPrintable(QStringLiteral("%1: %2").arg(want).arg(got)));
            // Let go, the bow leaves and the string rings out.
            QVERIFY2(take.rms(2.15, 2.3) < 0.1 * take.rms(1.2, 1.8), qPrintable(QString::number(take.rms(2.15, 2.3))));
        }
    }

    void thePressIsTheBowsWeight() {
        // On the page: a light finger is a soft flageolet, a firm one sets
        // the bridge knocking, brighter by far over two kilohertz.
        auto play = [&](double p) {
            return mono(testkit::play(onSound(kTromba, fmt("down 0.4 1 %.4f 0.5 p=%.2f\nup 1.9 1\nend 2.0\n", cellX(7), p))));
        };
        const auto soft = play(0.12), firm = play(0.7);
        const double s = band(soft, 0.9, 0.5, 2000.0, 8000.0) - band(soft, 0.9, 0.5, 100.0, 2000.0);
        const double f = band(firm, 0.9, 0.5, 2000.0, 8000.0) - band(firm, 0.9, 0.5, 100.0, 2000.0);
        QVERIFY2(f > s + 8, qPrintable(QStringLiteral("%1 %2").arg(f).arg(s)));
    }

    void aLegatoNoteSlidesTheThumb() {
        // A second finger on another note while the first is held: the
        // thumb slides to its node and the string sounds the new harmonic,
        // in tune, without a new bow.
        const auto take = testkit::play(onSound(
            kTromba, fmt("down 0.4 1 %.4f 0.5 p=0.6\ndown 1.2 2 %.4f 0.5 p=0.6\nup 2.4 2\nup 2.4 1\nend 2.6\n", cellX(7), cellX(9))));
        const auto x = mono(take);
        // (C5 in A minor: the open string's nearest harmonic is the 10th.)
        const double want = 523.25;
        const double got = peakHz(x, 1.6, 0.7, want, 50);
        QVERIFY2(std::fabs(cents(got, want)) < 6, qPrintable(QString::number(got)));
        QVERIFY(take.finite() && take.peak() < 1.0);
    }

    void aStringStaysInsideBudget() {
        // A string bowed firmly enough to knock, rendered against the clock:
        // under one and a half per cent of real time.
        TENFOLD_TIMED();
        tromba::TrombaMarina t;
        t.init(kRate);
        t.setNote(440.0f, 8, true);
        const auto bw = tromba::bowFor(0.7f, 0.5f, 0.8f, 8, 55.0f);
        t.setBow(bw.force, -bw.speed, bw.beta);
        float l[32], r[32];
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < int(4.0 * kRate) / 32; ++b)
            t.render(l, r, 32, 1.0f);
        const double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        QVERIFY2(share < 0.015, qPrintable(QString::number(share)));
    }

    void aLineWithHarmonyStaysInsideBudget() {
        // A run on the Lead with three harmony voices held (four strings at
        // once) through the whole studio: it keeps its time.
        TENFOLD_TIMED();
        std::string s = onSound(kTromba, "");
        s += "down 0.4 11 0.08 0.85 p=0.8\ndown 0.4 12 0.15 0.70 p=0.8\ndown 0.4 13 0.08 0.55 p=0.8\n";
        for (int i = 0; i < 10; ++i)
            s += fmt("down %.2f %d %.4f 0.6 p=0.8\nup %.2f %d\n", 0.5 + 0.5 * i, i + 1, cellX((i * 3) % 15), 0.95 + 0.5 * i,
                     i + 1);
        s += "up 5.5 11\nup 5.5 12\nup 5.5 13\nend 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.2, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(TrombaMarinaTests)
#include "trombamarina_tests.moc"
