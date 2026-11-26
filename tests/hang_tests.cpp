// The hang kit: an instrument of steel and air (hang.h), checked as the
// acoustics papers measure real ones (docs/hang.md): its tuning, its
// partials, where and how hard it is struck, the gu's air and what it does
// to the shell, sympathy between fields, its decay, and what it costs.

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
    // D minor: the classic hang, D | A Bb C D E F G A.
    Ctx() { h.tuning.set(2, 0, 4, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 1.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// One strike on a fresh hang, its output (both ears summed) for `seconds`.
// u, w: where on the field, in units of its rim (u along its long axis).
std::vector<float> strike(int field, float vel, float u, float w, double seconds, drums::Engine *keep = nullptr,
                          float tone = 0) {
    Ctx ctx;
    drums::Engine local;
    drums::Engine &e = keep ? *keep : local;
    if (!keep)
        e.init(kRate);
    drums::Strike s;
    s.kit = drums::Hang;
    s.pad = drums::hangPad(field);
    s.vel = vel;
    s.sx = 0.5f + 0.5f * u / hang::kSpotReach;
    s.sy = 0.5f + 0.5f * w / hang::kSpotReach;
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

// A partial's level: the strongest component within ±cents of where it
// should be (a real partial sits a few cents off, or splits into a pair
// with another field's).
double levelNear(const std::vector<float> &x, double from, double len, double hz, double cents = 25) {
    double top = -1e9;
    const double step = std::max(0.2, 0.5 / len);
    for (double f = hz * std::exp2(-cents / 1200); f <= hz * std::exp2(cents / 1200); f += step)
        top = std::max(top, levelAt(x, from, len, f));
    return top;
}

// The strongest peak within ±cents of hz over a window: coarse, then fine.
double peakHz(const std::vector<float> &x, double from, double len, double hz, double cents = 60) {
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

// The page on the hang: Right from house as many times as the hang is kits
// along (so kits added after it don't move it).
std::string toHang() {
    std::string s = "page 0 1\n";
    for (int i = 0; i < drums::Hang; ++i)
        s += fmt("key %.3f Right\n", 0.1 + 0.004 * i);
    return s;
}

// The pitch heard near hz: the power-weighted mean frequency within ±cents
// over a window. A partial coupled to another field's splits into a pair
// a few cents either side, which beat; the ear hears their middle.
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
double tauOf(const std::vector<float> &x, double hz, double t0, double t1) {
    const double a = levelAt(x, t0, 0.2, hz), b = levelAt(x, t1, 0.2, hz);
    return (t1 - t0) * 20 / std::log(10.0) / (a - b);
}

}

class HangTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenFingersOnTheHangStayInsideBudget")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "aRollNeverPumpsEnergyIn aLoopOfTheHangKeepsTheHang")
private slots:
    // --- tuning ---------------------------------------------------------------

    void inDMinorItIsTheClassicHang() {
        // D3 | A3 Bb3 C4 D4 E4 F4 G4 A4, the Integral Hang's own notes.
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        const int midi[hang::kFields] = {50, 57, 58, 60, 62, 64, 65, 67, 69};
        for (int f = 0; f < hang::kFields; ++f)
            QVERIFY2(std::fabs(cents(hz[f], midiToHz(midi[f]))) < 0.5, qPrintable(QString::number(f)));
        // In another key the ding moves to its root, near D3 still.
        ctx.h.tuning.set(9, 0, 4, false);
        drums::hangTuning(ctx.h, hz);
        QVERIFY(std::fabs(cents(hz[0], midiToHz(45))) < 0.5 || std::fabs(cents(hz[0], midiToHz(57))) < 0.5);
        QVERIFY(hz[0] > 105 && hz[0] < 215);
        for (int f = 1; f < hang::kFields; ++f)
            QVERIFY(hz[f] > hz[f - 1]);
    }

    void everyFieldSoundsInTune() {
        // Struck in the middle, each field's fundamental (heard, the air and
        // the shell's couplings and all) is within 7 cents of its note, a
        // maker's tolerance (all real ones within 13): the model
        // pre-compensates the pull of the gu's air, as a maker tunes the
        // assembled instrument. (A field whose fundamental another field's
        // overtone shares, as A4 does A3's octave, is heard with the partner
        // it beats with, a few cents off its own.)
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        for (int f = 0; f < hang::kFields; ++f) {
            const auto x = strike(f, 0.35f, 0.0f, 0.0f, 1.6);
            const double got = pitchNear(x, 0.4, 1.2, hz[f]);
            QVERIFY2(std::fabs(cents(got, hz[f])) < 7.0,
                     qPrintable(QStringLiteral("field %1: %2 Hz for %3").arg(f).arg(got).arg(hz[f])));
        }
    }

    void thePartialsAreOneTwoThree() {
        // Every field and the ding: the octave and compound fifth within
        // the 13 cents every measured real hang is within (most within 5).
        for (int f = 0; f < hang::kFields; ++f) {
            const auto x = strike(f, 0.8f, 0.4f, 0.4f, 1.6);
            Ctx ctx;
            float hz[hang::kFields];
            drums::hangTuning(ctx.h, hz);
            const double f1 = pitchNear(x, 0.4, 1.2, hz[f]);
            const double f2 = pitchNear(x, 0.4, 1.2, 2 * hz[f], 30);
            const double f3 = pitchNear(x, 0.4, 1.2, 3 * hz[f], 30);
            // (against the note's own octave and twelfth: a fundamental
            // heard with a partner is pulled a few cents itself)
            QVERIFY2(std::fabs(cents(f2, 2 * hz[f])) < 13.0, qPrintable(QStringLiteral("field %1 octave %2 c").arg(f).arg(cents(f2, 2 * hz[f]))));
            QVERIFY2(std::fabs(cents(f3, 3 * hz[f])) < 13.0, qPrintable(QStringLiteral("field %1 fifth %2 c").arg(f).arg(cents(f3, 3 * hz[f]))));
            // And all three speak, within the spread measured on real
            // hangs (octave −24…+21 dB, fifth −24…+13 dB of the fundamental).
            const double l1 = levelNear(x, 0.1, 0.2, f1);
            QVERIFY2(levelNear(x, 0.1, 0.2, f2) > l1 - 25, qPrintable(QString::number(levelNear(x, 0.1, 0.2, f2) - l1)));
            QVERIFY2(levelNear(x, 0.1, 0.2, f3) > l1 - 30, qPrintable(QString::number(levelNear(x, 0.1, 0.2, f3) - l1)));
        }
        // The body's modes say the same: 1 : 2 : 3, then nothing harmonic
        // until near the third octave (7.5–9×).
        hang::Body b;
        b.init(kRate);
        for (int f = 0; f < hang::kFields; ++f) {
            const float f0 = b.modeHz(f, 0);
            QVERIFY(std::fabs(cents(b.modeHz(f, 1), 2 * f0)) < 13);
            QVERIFY(std::fabs(cents(b.modeHz(f, 2), 3 * f0)) < 13);
            for (int k = 3; k < 3 + hang::Body::kUpper; ++k)
                QVERIFY(b.modeHz(f, k) > 7.4f * f0 && b.modeHz(f, k) < 9.3f * f0);
        }
    }

    // --- the strike -----------------------------------------------------------

    void whereTheFingerLandsChoosesThePartials() {
        // Against a strike in the middle of a field: off along its long axis
        // (the octave's antinodes) the octave is louder; off across it (the
        // fifth's), the fifth. On real hangs by about +5 and +7 dB: here, over
        // the eight fields, by a median of 2–12 and 3–14 dB at the better of
        // the two spots, and the octave never by more than 20 (a fingertip's patch is
        // wide against a lobe). (The two ends of an axis differ: the
        // curvature's drive adds to the strike's own push at one and works
        // against it at the other.)
        std::vector<double> octave, fifth;
        for (int f = 1; f < hang::kFields; ++f) {
            Ctx ctx;
            float hz[hang::kFields];
            drums::hangTuning(ctx.h, hz);
            const auto mid = strike(f, 0.6f, 0, 0, 0.6), up = strike(f, 0.6f, 0.5f, 0, 0.6),
                       down = strike(f, 0.6f, -0.5f, 0, 0.6), left = strike(f, 0.6f, 0, 0.5f, 0.6),
                       right = strike(f, 0.6f, 0, -0.5f, 0.6);
            auto rel = [&](const std::vector<float> &x, int partial) {
                // (over half a second: a shared partial trades its energy
                // with its partner a few times a second)
                return levelNear(x, 0.02, 0.5, hz[f] * partial) - levelNear(x, 0.02, 0.5, hz[f]);
            };
            octave.push_back(std::max(rel(up, 2), rel(down, 2)) - rel(mid, 2));
            fifth.push_back(std::max(rel(left, 3), rel(right, 3)) - rel(mid, 3));
        }
        auto all = [](const std::vector<double> &v) {
            QStringList s;
            for (double x : v)
                s << QString::number(x, 'f', 1);
            return s.join(' ');
        };
        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
        };
        QVERIFY2(median(octave) > 2 && median(octave) < 12, qPrintable(all(octave)));
        QVERIFY2(median(fifth) > 3 && median(fifth) < 14, qPrintable(all(fifth)));
        QVERIFY2(*std::max_element(octave.begin(), octave.end()) < 20, qPrintable(all(octave)));
        // (A low field's fifth is weakest struck in the middle, so its spot
        // gains most.)
        QVERIFY2(*std::max_element(fifth.begin(), fifth.end()) < 30, qPrintable(all(fifth)));
    }

    void harderIsLouderBrighterAndBlooms() {
        // A soft stroke is the fundamental and its octave, the fifth far
        // below; a hard one is louder by 15–30 dB, and the curvature of the
        // dimple pumps energy up into the octave and fifth, which peak
        // after the fundamental does (on real hangs: ~25 ms, ~90 ms, 0.2–0.5 s).
        const int f = 4;
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        const auto soft = strike(f, 0.15f, 0, 0, 1.0), hard = strike(f, 1.0f, 0, 0, 1.0);
        const double sf = levelAt(soft, 0.02, 0.1, hz[f]), hf = levelAt(hard, 0.02, 0.1, hz[f]);
        QVERIFY2(hf - sf > 15 && hf - sf < 32, qPrintable(QString::number(hf - sf)));
        const double soft5 = levelNear(soft, 0.1, 0.2, 3 * hz[f]) - levelNear(soft, 0.1, 0.2, hz[f]);
        const double hard5 = levelNear(hard, 0.1, 0.2, 3 * hz[f]) - levelNear(hard, 0.1, 0.2, hz[f]);
        QVERIFY2(soft5 < -20, qPrintable(QString::number(soft5)));
        QVERIFY2(hard5 > soft5 + 12, qPrintable(QStringLiteral("%1 %2").arg(hard5).arg(soft5)));
        // The bloom: where each partial of the hard strike is loudest.
        auto peakTime = [&](int partial) {
            double best = 0, top = -1e9;
            for (double t = 0.0; t < 0.8; t += 0.01) {
                const double l = levelAt(hard, t, 0.04, hz[f] * partial);
                if (l > top)
                    top = l, best = t;
            }
            return best;
        };
        const double t1 = peakTime(1), t2 = peakTime(2), t3 = peakTime(3);
        QVERIFY2(t1 < 0.04, qPrintable(QString::number(t1)));
        QVERIFY2(t2 > t1 && t3 > t1, qPrintable(QStringLiteral("%1 %2 %3").arg(t1).arg(t2).arg(t3)));
    }

    void aHarderStrikeBloomsMoreNotLess() {
        // A fingertip sags a hang's pitch only a little (a stick far more):
        // struck harder, a field blooms into at least as much octave, and
        // starts no more than a quarter-tone flat. (Its octave may sit a few
        // dB lower against a fundamental that grew faster, never collapse.)
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        for (int f : {1, 4, 8}) {
            auto octave = [&](const std::vector<float> &x) {
                return levelNear(x, 0.0, 0.5, 2 * hz[f]) - levelNear(x, 0.0, 0.5, hz[f]);
            };
            const auto mf = strike(f, 0.6f, 0, 0, 0.6), ff = strike(f, 0.9f, 0, 0, 0.6);
            QVERIFY2(octave(ff) > octave(mf) - 4,
                     qPrintable(QStringLiteral("field %1: %2 %3").arg(f).arg(octave(mf)).arg(octave(ff))));
            const auto full = strike(f, 1.0f, 0, 0, 0.3);
            const double start = cents(pitchNear(full, 0.01, 0.08, hz[f], 80), hz[f]);
            QVERIFY2(start > -30, qPrintable(QStringLiteral("field %1 starts %2 c").arg(f).arg(start)));
        }
    }

    void theFingerTouchesForAMillisecondOrTwo() {
        // Solved against the field's own motion: shorter the harder it is
        // struck, within what fingers and sticks are measured at (1–4 ms).
        hang::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock];
        auto contact = [&](float vel) {
            b.silence();
            b.strike(4, vel, 0, 0, 0, 0);
            for (int i = 0; i < 100; ++i)
                b.render(l, r, kBlock);
            return b.lastContact();
        };
        const float soft = contact(0.1f), hard = contact(1.0f);
        QVERIFY2(soft > hard, qPrintable(QStringLiteral("%1 %2").arg(soft).arg(hard)));
        QVERIFY(hard > 0.0008f && soft < 0.004f);
    }

    // --- the gu ---------------------------------------------------------------

    void theGuIsAHelmholtzResonator() {
        // Helmholtz's formula for a handpan's 82 mm, 19 mm-necked port in
        // 25 litres gives the measured 85 Hz; the maker sizes the gu to the
        // ding (85 Hz under a 148 Hz ding).
        QVERIFY(std::fabs(hang::Body::helmholtzHz() - 85.0f) < 2.0f);
        hang::Body b;
        b.init(kRate);
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        b.tune(hz);
        QVERIFY2(std::fabs(b.guHeardHz() - 85.0f * hz[0] / 148.0f) < 0.5f, qPrintable(QString::number(b.guHeardHz())));
        QVERIFY(b.guRadius() > 0.035f && b.guRadius() < 0.05f);
        // A slap on the gu booms there, and briefly (a Q of about twelve, so
        // tens of milliseconds): below the rigid-walled formula's frequency,
        // as the shell gives under the pressure.
        const auto slap = strike(hang::kGu, 0.8f, 0, 0, 0.6);
        const double boom = peakHz(slap, 0.0, 0.25, b.guHeardHz(), 400);
        QVERIFY2(std::fabs(boom - b.guHeardHz()) < 1.5, qPrintable(QStringLiteral("%1 %2").arg(boom).arg(b.guHeardHz())));
        QVERIFY2(boom < b.guHz() - 0.5, qPrintable(QStringLiteral("%1 %2").arg(boom).arg(b.guHz())));
        QVERIFY(levelAt(slap, 0.0, 0.08, boom) > levelAt(slap, 0.3, 0.08, boom) + 15);
    }

    void theDingAndTheGuTradeEnergy() {
        // The cavity's pressure couples them both ways. With the air in
        // place a ding strike sets the gu's air moving (its boom is in the
        // sound) and a gu slap sets the ding ringing; open the cavity (no
        // air spring) and neither happens.
        drums::Engine withAir, without;
        withAir.init(kRate);
        without.init(kRate);
        without.hangBody().setAir(false);
        const auto a = strike(0, 0.8f, 0.2f, 0, 0.5, &withAir);
        const auto n = strike(0, 0.8f, 0.2f, 0, 0.5, &without);
        const double gu = withAir.hangBody().guHz();
        QVERIFY2(levelAt(a, 0.0, 0.1, gu) > levelAt(n, 0.0, 0.1, gu) + 15,
                 qPrintable(QStringLiteral("%1 %2").arg(levelAt(a, 0.0, 0.1, gu)).arg(levelAt(n, 0.0, 0.1, gu))));
        // The ding is pumped much harder than a small high field, whose
        // dimple moves little air at a frequency far from the gu's.
        hang::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock];
        auto guAfter = [&](int field) {
            b.silence();
            b.strike(field, 0.8f, 0, 0, 0, 0);
            float top = 0;
            for (int i = 0; i < 150; ++i) {
                b.render(l, r, kBlock);
                top = std::max(top, b.guSwing() / std::max(b.swing(field, 0), 1e-12f));
            }
            return top;
        };
        QVERIFY2(guAfter(0) > 4 * guAfter(8), qPrintable(QStringLiteral("%1 %2").arg(guAfter(0)).arg(guAfter(8))));
        // And back: a slap on the gu rings the ding, mostly through the air
        // (the palm on the bottom shell reaches it a little directly).
        auto dingAfterSlap = [&] {
            b.silence();
            b.strike(hang::kGu, 0.8f, 0, 0, 0, 0);
            float top = 0;
            for (int i = 0; i < 300; ++i) {
                b.render(l, r, kBlock);
                top = std::max(top, b.swing(0, 0));
            }
            return top;
        };
        const float through = dingAfterSlap();
        b.setAir(false);
        const float direct = dingAfterSlap();
        QVERIFY2(through > 3 * direct, qPrintable(QStringLiteral("%1 %2").arg(through).arg(direct)));
    }

    void theAirStiffensTheDingAndTheModelAllowsForIt() {
        // The air couples the ding and the gu by several percent of their
        // frequencies (it would split them that much if they met), and
        // the ding still sounds where it is tuned, with or without the air.
        hang::Body b;
        b.init(kRate);
        QVERIFY2(b.airCoupling(0) > 0.02f && b.airCoupling(0) < 0.3f, qPrintable(QString::number(b.airCoupling(0))));
        QVERIFY(b.airCoupling(0) > 3 * b.airCoupling(8));
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        drums::Engine without;
        without.init(kRate);
        without.hangBody().setAir(false);
        const auto a = strike(0, 0.3f, 0, 0, 1.6), n = strike(0, 0.3f, 0, 0, 1.6, &without);
        const double withAir = cents(pitchNear(a, 0.4, 1.2, hz[0]), hz[0]);
        const double open = cents(pitchNear(n, 0.4, 1.2, hz[0]), hz[0]);
        QVERIFY2(std::fabs(withAir) < 4 && std::fabs(open) < 4, qPrintable(QStringLiteral("%1 %2").arg(withAir).arg(open)));
    }

    // --- the shell ------------------------------------------------------------

    void aFieldRingsItsPartnersInSympathy() {
        // A3's octave is A4's fundamental: striking A3 sets A4 ringing far
        // more than a field with nothing in common (F4).
        hang::Body b;
        b.init(kRate);
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        b.tune(hz);
        b.strike(1, 0.7f, 0.3f, 0.1f, 0, 0);
        float l[kBlock], r[kBlock];
        float partner = 0, stranger = 0;
        for (int i = 0; i < int(0.6f * kRate) / kBlock; ++i) {
            b.render(l, r, kBlock);
            partner = std::max(partner, b.swing(8, 0));
            stranger = std::max(stranger, b.swing(6, 0));
        }
        QVERIFY2(partner > 10 * stranger, qPrintable(QStringLiteral("%1 %2").arg(partner).arg(stranger)));
        // Within 20 dB of the struck field's own octave: heard, as on real hangs.
        QVERIFY(partner > 0.1f * b.swing(1, 1) || partner > 1e-7f);
    }

    void coupledPartialsBeatAFewTimesASecond() {
        // The pair a shared partial makes (A3's octave with A4's fundamental)
        // trades its energy back and forth: struck on A3, what is heard at
        // 440 Hz rises and falls a few times a second (3.1–8.6 Hz measured on
        // real hangs; here the partners sit 10–14 c apart, about 2.5–4 Hz).
        const auto x = strike(1, 0.6f, 0.4f, 0.1f, 2.2);
        std::vector<double> env;
        for (double t = 0.15; t < 2.1; t += 0.01)
            env.push_back(levelNear(x, t, 0.05, 440.0, 30));
        std::vector<double> peaks;
        const int gap = 8; // 1/12 s: the beats, not the frame's ripple
        for (int i = gap; i + gap < int(env.size()); ++i) {
            bool top = true;
            for (int j = i - gap; j <= i + gap && top; ++j)
                top = env[size_t(j)] <= env[size_t(i)];
            if (top && (peaks.empty() || i - peaks.back() > gap))
                peaks.push_back(i);
        }
        QVERIFY2(peaks.size() >= 3, qPrintable(QString::number(peaks.size())));
        const double rate = (peaks.size() - 1) / ((peaks.back() - peaks.front()) * 0.01);
        QVERIFY2(rate > 2.0 && rate < 10, qPrintable(QString::number(rate)));
    }

    void aStruckFieldDecaysWithoutPumping() {
        // A field whose fundamental another field shares (D4 with the ding's
        // octave, A4 with A3's) gives away only some of its energy: around
        // its steady decay, its fundamental swells and dips by no more than
        // ~10 dB in its first second, not the full exchange of two partials
        // tuned on top of each other.
        QStringList pumping;
        double worst = 0;
        for (int f = 0; f < hang::kFields; ++f) {
            hang::Body b;
            b.init(kRate);
            Ctx ctx;
            float hz[hang::kFields];
            drums::hangTuning(ctx.h, hz);
            b.tune(hz);
            b.strike(f, 0.6f, 0, 0, 0, 0);
            float l[kBlock], r[kBlock];
            std::vector<double> t, db;
            for (int i = 0; i < int(1.0f * kRate) / kBlock; ++i) {
                b.render(l, r, kBlock);
                const double at = double(i) * kBlock / kRate;
                if (at > 0.05) {
                    t.push_back(at);
                    db.push_back(20 * std::log10(b.swing(f, 0) + 1e-15));
                }
            }
            // Least squares: the steady decay; what is left, the pumping.
            double st = 0, sd = 0, stt = 0, std_ = 0;
            for (size_t i = 0; i < t.size(); ++i)
                st += t[i], sd += db[i], stt += t[i] * t[i], std_ += t[i] * db[i];
            const double n = double(t.size()), slope = (n * std_ - st * sd) / (n * stt - st * st), icpt = (sd - slope * st) / n;
            double lo = 1e9, hi = -1e9;
            for (size_t i = 0; i < t.size(); ++i) {
                const double res = db[i] - (icpt + slope * t[i]);
                lo = std::min(lo, res);
                hi = std::max(hi, res);
            }
            pumping << QString::number(hi - lo, 'f', 1);
            worst = std::max(worst, hi - lo);
        }
        QVERIFY2(worst < 10, qPrintable(pumping.join(' ')));
        // And as heard (both ears together, where two partners ringing at
        // one frequency could cancel): D4, sharing the ding's octave, struck
        // firmly and hard.
        for (float vel : {0.6f, 0.9f}) {
            const auto x = strike(4, vel, 0, 0, 1.1);
            Ctx ctx;
            float hz[hang::kFields];
            drums::hangTuning(ctx.h, hz);
            std::vector<double> t, db;
            for (double at = 0.05; at < 1.0; at += 0.01) {
                t.push_back(at);
                db.push_back(levelNear(x, at, 0.05, hz[4], 30));
            }
            double st = 0, sd = 0, stt = 0, std_ = 0;
            for (size_t i = 0; i < t.size(); ++i)
                st += t[i], sd += db[i], stt += t[i] * t[i], std_ += t[i] * db[i];
            const double n = double(t.size()), slope = (n * std_ - st * sd) / (n * stt - st * st), icpt = (sd - slope * st) / n;
            double lo = 1e9, hi = -1e9;
            for (size_t i = 0; i < t.size(); ++i) {
                lo = std::min(lo, db[i] - (icpt + slope * t[i]));
                hi = std::max(hi, db[i] - (icpt + slope * t[i]));
            }
            QVERIFY2(hi - lo < 12, qPrintable(QStringLiteral("D4 at %1: %2 dB").arg(vel).arg(hi - lo)));
        }
    }

    void aFieldSoundsItsOwnNoteNotItsNeighbours() {
        // The pull between fields an octave apart stays a sympathy: struck
        // in the middle, A3 is heard as A3 (its 440 band, its own octave and
        // A4 ringing along, no louder than its fundamental), and a hard
        // strike on D4 or A4 grows no ghost of the note an octave below.
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        for (float vel : {0.3f, 0.6f, 0.9f}) {
            const auto a3 = strike(1, vel, 0, 0, 0.6);
            const double up = levelNear(a3, 0.0, 0.5, 2 * hz[1]) - levelNear(a3, 0.0, 0.5, hz[1]);
            // (struck hard it blooms, as a hang does, but A3 stays the note)
            QVERIFY2(up < (vel > 0.8f ? 6 : 3), qPrintable(QStringLiteral("A3 at %1: 440 Hz %2 dB over its own").arg(vel).arg(up)));
        }
        for (int f : {4, 8}) {
            const int below = f == 4 ? 0 : 1;
            const auto x = strike(f, 1.0f, 0, 0, 1.5);
            double ghost = -1e9;
            for (double t = 0.1; t < 1.4; t += 0.05)
                ghost = std::max(ghost, levelNear(x, t, 0.1, hz[below], 20));
            const double own = levelNear(x, 0.0, 0.1, hz[f], 20);
            QVERIFY2(ghost < own - 30, qPrintable(QStringLiteral("field %1: %2 dB").arg(f).arg(ghost - own)));
        }
    }

    void aTapBetweenTheFieldsStillSounds() {
        // The shoulder between the fields is steel too: a tap there is a dry
        // knock and the nearest fields, quieter than a field but no hole.
        auto loudness = [](float x, float y) {
            const auto take = testkit::play(
                toHang() + QStringLiteral("tap 0.5 %1 %2 p=0.7\nend 1.5\n").arg(x).arg(y).toStdString());
            return 20 * std::log10(take.peak() + 1e-9);
        };
        const double onField = loudness(0.5f, 0.535f - 0.445f * 0.62f);
        // Between the ding and the lowest field; between two fields on the ring.
        const double gap = loudness(0.5f, 0.535f - 0.445f * 0.42f);
        const float th = -67.5f * kPi / 180.0f;
        const double ring = loudness(0.5f + 0.47f * 0.62f * std::cos(th), 0.535f + 0.445f * 0.62f * std::sin(th));
        QVERIFY2(gap > onField - 15 && ring > onField - 15, qPrintable(QStringLiteral("%1 %2 %3").arg(onField).arg(gap).arg(ring)));
    }

    void aRollNeverPumpsEnergyIn() {
        // Fingers drumming fast on one field (a 30 ms roll) add up as hits
        // would, never more: each fingertip meets the steel where it is.
        // Checked with the curvature's nonlinearity off, which would hide it.
        const float beta = hang::Knobs::beta, gamma = hang::Knobs::gamma, soft = hang::Knobs::soft;
        hang::Knobs::beta = hang::Knobs::gamma = hang::Knobs::soft = 0;
        hang::Body b;
        b.init(kRate);
        float l[kBlock], r[kBlock], first = 0, top = 0;
        bool finite = true;
        for (int i = 0; i < int(1.5f * kRate) / kBlock; ++i) {
            if (i % 45 == 0)
                b.strike(4, 1.0f, 0.3f, 0.1f, 0, 0);
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            b.render(l, r, kBlock);
            for (int j = 0; j < kBlock; ++j) {
                finite &= std::isfinite(l[j]);
                top = std::max(top, std::fabs(l[j]));
                if (i < 45)
                    first = std::max(first, std::fabs(l[j]));
            }
        }
        hang::Knobs::beta = beta, hang::Knobs::gamma = gamma, hang::Knobs::soft = soft;
        QVERIFY(finite);
        // (Hits a 30 ms apart on a note ringing ~0.45 s add up to at most
        // 1/(1 − e^(−0.03/0.45)) of one, +24 dB, if every one lands in phase;
        // energy pumped in grew a roll by 76 dB.)
        QVERIFY2(20 * std::log10(top / first) < 22, qPrintable(QString::number(20 * std::log10(top / first))));
    }

    void itRingsForSecondsTheDingLongest() {
        // T60 2–4 s for the fields (handpans: 2.5–3.2 s), longer for the ding.
        Ctx ctx;
        float hz[hang::kFields];
        drums::hangTuning(ctx.h, hz);
        const auto field = strike(5, 0.4f, 0, 0, 2.5), ding = strike(0, 0.4f, 0, 0, 2.5);
        const double tf = tauOf(field, hz[5], 0.5, 1.8), td = tauOf(ding, hz[0], 0.5, 1.8);
        QVERIFY2(tf * 6.91 > 2.0 && tf * 6.91 < 4.5, qPrintable(QString::number(tf * 6.91)));
        QVERIFY2(td > tf, qPrintable(QStringLiteral("%1 %2").arg(td).arg(tf)));
        // And it goes to sleep once rung out: nothing left to compute.
        drums::Engine e;
        e.init(kRate);
        strike(4, 1.0f, 0, 0, 14.0, &e);
        QCOMPARE(e.voices(), 0);
    }

    // --- the page -------------------------------------------------------------

    void thePadIsTheHang() {
        // The ding in the middle of the pad, A3 nearest the player, A4 at the
        // far side, the gu past the rim in a corner.
        auto struck = [](float x, float y) {
            const auto take = testkit::play(toHang() + QStringLiteral("tap 0.5 %1 %2 p=0.7\nend 0.56\n")
                                                          .arg(x)
                                                          .arg(y)
                                                          .toStdString());
            int best = -1;
            float top = 0.2f;
            for (int p = 0; p < drums::PadCount; ++p)
                if (take.view.parts[0].data[p] > top)
                    top = take.view.parts[0].data[p], best = p;
            return best < 0 ? -1 : drums::hangField(best);
        };
        QCOMPARE(struck(0.5f, 0.535f), 0);
        QCOMPARE(struck(0.5f, 0.535f - 0.445f * 0.62f), 1);
        QCOMPARE(struck(0.5f, 0.535f + 0.445f * 0.62f), 8);
        QCOMPARE(struck(0.03f, 0.95f), int(hang::kGu));
        const auto take = testkit::play(toHang() + "tap 0.5 0.5 0.535 p=0.7\nend 2\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(0.5, 1.5) > 1e-3);
        QVERIFY(take.peak() < 1.0);
    }

    void aLoopOfTheHangKeepsTheHang() {
        // Hits carry the kit: a hang loop sounds as a hang after the page
        // has gone back to house.
        const auto take = testkit::play(toHang() + "tap 0.6 0.5 0.535 p=0.7\ntap 0.9 0.5 0.26 p=0.7\n"
                                                    "key 1.2 Space\nkey 1.5 Right\nend 6\n");
        QVERIFY(take.finite());
        QVERIFY(take.rms(3, 6) > 1e-4);
    }

    // --- the cost -------------------------------------------------------------

    void theHangCostsTheSameForTenFingers() {
        // One body rings whatever strikes it: ten strikes at once cost about
        // what one does, and a hang playing costs well under a percent of
        // one core per second of sound.
        TENFOLD_TIMED();
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
                        s.kit = drums::Hang;
                        s.pad = drums::hangPad(f % hang::kFields);
                        s.vel = 0.9f;
                        s.sx = 0.6f;
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
        // Ten fields struck hard at once (the ding twice) are louder than
        // the ding alone by about what their attacks add up to, landing
        // together, and no more: nothing feeds on the crowd.
        QVERIFY2(top < 20 * oneTop, qPrintable(QStringLiteral("%1 %2").arg(oneTop).arg(top)));
        QVERIFY2(ten < 0.03, qPrintable(QString::number(ten)));
        QVERIFY2(ten < 3 * one + 0.002, qPrintable(QStringLiteral("%1 %2").arg(one).arg(ten)));
    }

    void tenFingersOnTheHangStayInsideBudget() {
        // Ten fingers drumming on the hang through the whole studio, as the
        // other pages' budgets are measured.
        TENFOLD_TIMED();
        std::string s = toHang();
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

TESTKIT_REGISTER(HangTests)
#include "hang_tests.moc"
