// The Drums page: its sounds, its timing, and every gesture it promises.

#include <vector>

#include "drums.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;
constexpr double kBpm = 124; // the studio's tempo until a loop sets another
constexpr double kSpb = 60.0 / kBpm;

// Pad centres on the trackpad.
std::string at(int pad) {
    const float x = (float(pad % 4) + 0.5f) / 4, y = (float(pad / 4) + 0.5f) / 4;
    return std::to_string(x) + " " + std::to_string(y);
}

// An audio context for the engine alone: a fixed clock and key, and a
// record of the pumps asked for.
struct Ctx : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    std::vector<std::pair<int, float>> pumps;
    double time = 1.0;
    Ctx() { h.tuning.set(9, 0, 4, false); }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return time; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int offset, float strength) override { pumps.push_back({offset, strength}); }
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

struct Dry {
    std::vector<float> l, r;
    float peak() const {
        float p = 0;
        for (size_t i = 0; i < l.size(); ++i)
            p = std::max({p, std::fabs(l[i]), std::fabs(r[i])});
        return p;
    }
    bool finite() const {
        for (size_t i = 0; i < l.size(); ++i)
            if (!std::isfinite(l[i]) || !std::isfinite(r[i]))
                return false;
        return true;
    }
};

// One hit, rendered dry, on a fresh engine or on one already playing.
Dry strike(int pad, int kit, float vel, double seconds, Ctx &ctx, drums::Engine *keep = nullptr) {
    drums::Engine local;
    drums::Engine &e = keep ? *keep : local;
    if (!keep)
        e.init(kRate);
    Command k;
    k.type = Command::Param;
    k.i[0] = drums::ParamKit;
    k.i[1] = kit;
    e.command(k, ctx);
    Command c;
    c.type = drums::Hit;
    c.i[0] = pad;
    c.f[0] = vel;
    c.f[1] = c.f[2] = 0.5f;
    c.f[5] = 0.4f;
    e.command(c, ctx);
    Dry d;
    float l[kBlock], r[kBlock];
    for (int b = 0; b < int(seconds * kRate) / kBlock; ++b) {
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        e.render(l, r, kBlock, ctx);
        d.l.insert(d.l.end(), l, l + kBlock);
        d.r.insert(d.r.end(), r, r + kBlock);
    }
    return d;
}

// The frequency a decaying tone settles on, from its zero crossings over a
// window well after the attack.
double settledHz(const std::vector<float> &x, double from, double to) {
    int crossings = 0;
    double first = -1, last = -1;
    for (size_t i = size_t(from * kRate) + 1; i < size_t(to * kRate) && i < x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0) {
            const double t = double(i) / kRate;
            if (first < 0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) / (last - first) : 0;
}

// Onsets in a mix: where a high-passed envelope (about two milliseconds,
// since a hat is noise rather than one spike) jumps to more than twice what
// it was three milliseconds before, loud enough to matter, and at least
// `gap` seconds after the last.
std::vector<double> onsets(const testkit::Take &take, double from, double to, double gap = 0.03) {
    std::vector<double> out;
    const size_t a = size_t(from * kRate), b = std::min(take.left.size(), size_t(to * kRate));
    float top = 0;
    std::vector<float> env(b > a ? b - a : 0);
    float prev = 0, e = 0;
    for (size_t i = a; i < b; ++i) {
        const float m = 0.5f * (take.left[i] + take.right[i]);
        const float hp = m - prev;
        prev = m;
        e += (std::fabs(hp) - e) * 0.01f;
        env[i - a] = e;
        top = std::max(top, e);
    }
    const size_t back = size_t(0.003 * kRate);
    for (size_t i = back; i < env.size(); ++i)
        if (env[i] > 0.15f * top && env[i] > 2.0f * env[i - back] &&
            (out.empty() || double(i + a) / kRate - out.back() > gap))
            out.push_back(double(i + a) / kRate);
    return out;
}

// The share of a dry sound's energy above 18 kHz: four poles of high-pass.
double fizz(const Dry &d) {
    Svf a, b;
    a.set(18000.0f, 0.7f, kRate);
    b.set(18000.0f, 0.7f, kRate);
    double hi = 0, all = 0;
    for (size_t i = 0; i < d.l.size(); ++i) {
        const float x = 0.5f * (d.l[i] + d.r[i]);
        const float y = b.step(a.step(x).high).high;
        hi += double(y) * y;
        all += double(x) * x;
    }
    return all > 0 ? hi / all : 0;
}

// The energy of a take between two times.
double energy(const testkit::Take &take, double from, double to) {
    const double r = take.rms(from, to);
    return r * r * (to - from);
}

// How far t is from the nearest step of `beats` on the studio's grid, in ms.
double offGridMs(double t, double beats) {
    const double step = beats * kSpb;
    return (t / step - std::round(t / step)) * step * 1000.0;
}

}

class DrumsTests : public QObject {
    Q_OBJECT
private slots:
    void kicksPlayedAlongArentLayeredTwice() {
        // Four kicks looped; then the kick played along with the loop and a
        // snare between, layered with Space: the layer brings the snare, and
        // the kicks that only doubled the loop's (a flam) are left out.
        const std::string base = "page 0.05 1\nkey 0.1 Ctrl+M\n"
                                 "key 1.9355 Z\nkey 2.4194 Z\nkey 2.9032 Z\nkey 3.3871 Z\nkey 3.8952 Space\n";
        const auto looped = testkit::play(base + "end 5.0\n");
        const int alone = looped.view.parts[0].clips[0].events;
        const auto take = testkit::play(base +
                                        "key 5.8105 Z\nkey 6.0484 V\nkey 6.2943 Z\nkey 6.5323 V\n"
                                        "key 6.7782 Z\nkey 7.0161 V\nkey 7.2621 Z\nkey 7.5000 V\n"
                                        "key 7.7661 Space\nend 9.0\n");
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.parts[0].playing, 0);
        const int layered = take.view.parts[0].clips[0].events;
        // The snares, as many events each as a kick has, and nothing more.
        QCOMPARE(layered, alone * 2);
    }

    void spaceJustAfterTheOneKeepsItsKick() {
        // Kicks on four beats, Space a twentieth of a beat after the next
        // ONE: the loop's first kick plays at once, a hair late, instead of
        // being skipped until the loop comes round.
        const std::string script = "page 0.05 1\nkey 0.1 Ctrl+M\n"
                                   "key 1.9355 Z\nkey 2.4194 Z\nkey 2.9032 Z\nkey 3.3871 Z\nkey 3.8952 Space\nend 5.0\n";
        const auto take = testkit::play(script);
        QVERIFY(take.error.empty());
        const double spb = 60.0 / 124.0;
        const double first = take.rms(8 * spb, 8.45 * spb), next = take.rms(9 * spb, 9.45 * spb);
        QVERIFY2(first > next * 0.5, qPrintable(QStringLiteral("the ONE %1, the next beat %2").arg(first).arg(next)));
    }

    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 1\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    // --- the sounds ----------------------------------------------------------

    void everyDrumInEveryKitSpeaksAndDies() {
        for (int kit = 0; kit < drums::KitCount; ++kit)
            for (int pad = 0; pad < drums::PadCount; ++pad) {
                Ctx ctx;
                drums::Engine e;
                e.init(kRate);
                const Dry d = strike(pad, kit, 1.0f, 9.0, ctx, &e);
                QVERIFY2(d.finite(), drums::padName(pad));
                QVERIFY2(d.peak() > 0.01f, drums::padName(pad));
                QVERIFY2(d.peak() < 0.6f, drums::padName(pad));
                // Every voice ends by itself, the crash last.
                QVERIFY2(e.voices() == 0, drums::padName(pad));
            }
    }

    void theKickLeadsAndTheHatsSitWellBelow() {
        for (int kit = 0; kit < drums::KitCount; ++kit) {
            Ctx ctx;
            const float kick = strike(drums::Kick, kit, 1.0f, 1.0, ctx).peak();
            for (int pad = 0; pad < drums::PadCount; ++pad) {
                if (pad == drums::Kick || pad == drums::Sub)
                    continue;
                const float other = strike(pad, kit, 1.0f, 1.0, ctx).peak();
                QVERIFY2(other < kick, drums::padName(pad, kit));
            }
            for (int pad : {drums::Hat, drums::Shaker, drums::Ride, drums::Crash})
                QVERIFY2(strike(pad, kit, 1.0f, 1.0, ctx).peak() < kick * 0.4f,
                         qPrintable(QStringLiteral("%1 in %2").arg(drums::padName(pad), drums::kitName(kit))));
        }
    }

    void velocityMatters() {
        Ctx ctx;
        for (int pad : {drums::Kick, drums::Snare, drums::Hat, drums::Clap}) {
            const float soft = strike(pad, drums::House, 0.25f, 0.5, ctx).peak();
            const float hard = strike(pad, drums::House, 1.0f, 0.5, ctx).peak();
            QVERIFY2(hard > soft * 2.5f, drums::padName(pad));
        }
    }

    void theKickIsTunedToTheKey() {
        // In A it sits on A1 (55 Hz); in C on C2, the C nearest the usual
        // kick; and the toms play the root, third and fifth an octave up.
        Ctx ctx;
        const Dry a = strike(drums::Kick, drums::House, 1.0f, 0.5, ctx);
        QVERIFY(std::fabs(settledHz(a.l, 0.12, 0.3) - 55.0) < 2.0);
        // In C the root is out of a kick's range either way: it takes the
        // fifth, G1.
        ctx.h.tuning.set(0, 0, 4, false);
        const Dry c = strike(drums::Kick, drums::House, 1.0f, 0.5, ctx);
        QVERIFY(std::fabs(settledHz(c.l, 0.12, 0.3) - 49.0) < 2.0);
        ctx.h.tuning.set(9, 0, 4, false);
        const Dry tom = strike(drums::TomHi, drums::House, 1.0f, 0.6, ctx);
        // E3 is the fifth of A minor.
        QVERIFY(std::fabs(settledHz(tom.l, 0.15, 0.4) - 164.8) < 4.0);
    }

    void theKickPumpsTheStudioWhereItLands() {
        Ctx ctx;
        drums::Engine e;
        e.init(kRate);
        Command c;
        c.type = drums::Hit;
        c.i[0] = drums::Kick;
        c.f[0] = 1.0f;
        c.t = ctx.time + 20.0 / kRate; // twenty samples into the block
        e.command(c, ctx);
        QCOMPARE(int(ctx.pumps.size()), 1);
        QCOMPARE(ctx.pumps[0].first, 20);
        QVERIFY(ctx.pumps[0].second > 0.9f);
        // A kick scheduled further ahead pumps in the block before it lands.
        ctx.pumps.clear();
        c.t = ctx.time + 100.0 / kRate;
        e.command(c, ctx);
        QVERIFY(ctx.pumps.empty());
        float l[kBlock] = {}, r[kBlock] = {};
        for (int b = 0; b < 3; ++b)
            e.render(l, r, kBlock, ctx);
        QCOMPARE(int(ctx.pumps.size()), 1);
        QCOMPARE(ctx.pumps[0].first, 100 - 3 * kBlock);
        // A hat doesn't.
        ctx.pumps.clear();
        c.i[0] = drums::Hat;
        c.t = 0;
        e.command(c, ctx);
        QVERIFY(ctx.pumps.empty());
    }

    void aClosedHatChokesTheOpenOne() {
        Ctx ctx;
        drums::Engine e;
        e.init(kRate);
        const Dry open = strike(drums::Open, drums::House, 1.0f, 0.3, ctx, &e);
        drums::Engine f;
        f.init(kRate);
        strike(drums::Open, drums::House, 1.0f, 0.1, ctx, &f);
        const Dry choked = strike(drums::Hat, drums::House, 0.3f, 0.2, ctx, &f);
        // 100 ms on, the open hat alone still rings; choked, it's gone.
        auto energy = [](const Dry &d, size_t a, size_t b) {
            double s = 0;
            for (size_t i = a; i < b; ++i)
                s += double(d.l[i]) * d.l[i];
            return s;
        };
        QVERIFY(energy(choked, size_t(0.08 * kRate), size_t(0.18 * kRate)) <
                0.05 * energy(open, size_t(0.18 * kRate), size_t(0.28 * kRate)));
    }

    // --- timing --------------------------------------------------------------

    void aHeldPadRollsLockedToTheGrid() {
        // The techno kit is straight, so its sixteenths land on the grid to
        // the sample.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\n"
                                        "down 1.0 1 " + at(drums::Hat) + "\nup 3.0 1\nend 3.3\n");
        const auto on = onsets(take, 1.3, 2.9);
        QVERIFY(on.size() >= 12);
        // The detector sees each hit a moment into its attack, the same
        // moment every time.
        const double lag = offGridMs(on[0], 0.25);
        QVERIFY(lag > -0.5 && lag < 3.0);
        for (double t : on)
            QVERIFY2(std::fabs(offGridMs(t, 0.25) - lag) < 0.8, qPrintable(QString::number(offGridMs(t, 0.25))));
        // One every sixteenth: nothing missed, nothing doubled.
        for (size_t i = 1; i < on.size(); ++i)
            QVERIFY(std::fabs(on[i] - on[i - 1] - 0.25 * kSpb) < 0.002);
    }

    void aTapIsOneHitNotARoll() {
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\ntap 1.0 " + at(drums::Hat) + " len=0.15\nend 2\n");
        QCOMPARE(int(onsets(take, 0.9, 2.0).size()), 1);
    }

    void slidingUpRollsFaster() {
        // Techno is straight: a hat roll at 1/16, then pushed up two rungs
        // to 1/32.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\n"
                                        "down 1.0 1 0.88 0.12\nglide 2.0 2.1 1 0.88 0.27\nup 3.5 1\nend 3.6\n");
        const auto slow = onsets(take, 1.3, 1.95, 0.02), fast = onsets(take, 2.4, 3.4, 0.02);
        QVERIFY(slow.size() > 2 && fast.size() > 2);
        const double slowGap = (slow.back() - slow.front()) / double(slow.size() - 1);
        const double fastGap = (fast.back() - fast.front()) / double(fast.size() - 1);
        QVERIFY(std::fabs(slowGap - 0.25 * kSpb) < 0.003);
        QVERIFY(std::fabs(fastGap - 0.125 * kSpb) < 0.003);
    }

    void swingDelaysTheOffbeatSixteenths() {
        // House swings 54%: every other sixteenth is a fiftieth of a beat late.
        const auto take = testkit::play("page 0 1\ndown 1.0 1 " + at(drums::Hat) + "\nup 3.0 1\nend 3.1\n");
        const auto on = onsets(take, 1.3, 2.9);
        int late = 0;
        for (double t : on)
            late += offGridMs(t, 0.25) > 8.0;
        QVERIFY(on.size() >= 12);
        QVERIFY(late >= int(on.size()) / 2 - 1 && late <= int(on.size()) / 2 + 1);
    }

    // --- gestures ------------------------------------------------------------

    void fingersLandingTogetherGroove() {
        // Kick and hat held together: four on the floor and offbeat hats.
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.00 1 " + at(drums::Kick) + "\ndown 1.03 2 " + at(drums::Hat) +
                                        "\nup 5.0 1\nup 5.0 2\nend 5.2\n");
        int beats = 0;
        for (int b = 3; b <= 9; ++b) {
            const double t = b * kSpb;
            if (take.rms(t, t + 0.05) > 3 * take.rms(t - 0.06, t - 0.01))
                ++beats;
        }
        QVERIFY(beats >= 6);
    }

    void aClickKeepsTheGrooveAsALoop() {
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Clap) +
                                        "\ndown 1.04 3 " + at(drums::Hat) +
                                        "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\nup 2.4 3\nend 9\n");
        const auto &part = take.view.parts[0];
        QCOMPARE(part.playing, 0);
        QCOMPARE(int(part.clips[0].lengthBeats), 4);
        // Kick 4, clap 2, hats 8.
        QCOMPARE(part.clips[0].events, 14);
        QCOMPARE(int(std::lround(take.view.bpm)), 124);
        // Long after the fingers left, the groove goes on.
        QVERIFY(take.rms(7, 9) > 0.02);
    }

    // Taps from a real session: a few rounds of a pattern, a pause, then
    // five rounds of A A B about every 0.94 s, and Space just after the
    // last B (before the next round would start). The loop is the pattern
    // as last played, two rounds of it, and the tempo comes from it.
    void spaceFindsTheTappedPatternWhereverItComes() {
        const std::pair<double, bool> taps[] = {
            {1.000, true},
            {1.483, true},
            {1.499, false},
            {1.939, true},
            {2.175, true},
            {2.412, false},
            {2.860, true},
            {3.080, true},
            {3.340, true},
            {3.365, false},
            {6.098, true},
            {6.329, true},
            {6.549, false},
            {6.997, true},
            {7.257, true},
            {7.469, false},
            {7.942, true},
            {8.162, true},
            {8.398, false},
            {8.858, true},
            {9.086, true},
            {9.322, false},
            {9.795, true},
            {9.999, true},
            {10.235, false},
        };
        std::string script = "page 0 1\n";
        for (const auto &[t, a] : taps)
            script += "tap " + std::to_string(t) + " " + at(a ? drums::Kick : drums::Clap) + "\n";
        script += "key " + std::to_string(taps[std::size(taps) - 1].first + 0.34) + " Space\nend 16\n";
        const auto take = testkit::play(script);
        const auto &part = take.view.parts[0];
        QCOMPARE(part.playing, 0);
        QCOMPARE(int(part.clips[0].lengthBeats), 4);
        QCOMPARE(part.clips[0].events, 6);
        // Two rounds of about 0.93 s make a bar at about 129 BPM.
        QVERIFY2(std::fabs(take.view.bpm - 129) <= 3, qPrintable(QString::number(take.view.bpm)));
    }
    void aTempoLoopGrowsAndShrinksByRoundsAtItsTempo() {
        const std::pair<double, bool> taps[] = {
            {1.000, true},
            {1.483, true},
            {1.499, false},
            {1.939, true},
            {2.175, true},
            {2.412, false},
            {2.860, true},
            {3.080, true},
            {3.340, true},
            {3.365, false},
            {6.098, true},
            {6.329, true},
            {6.549, false},
            {6.997, true},
            {7.257, true},
            {7.469, false},
            {7.942, true},
            {8.162, true},
            {8.398, false},
            {8.858, true},
            {9.086, true},
            {9.322, false},
            {9.795, true},
            {9.999, true},
            {10.235, false},
        };
        std::string script = "page 0 1\n";
        for (const auto &[t, a] : taps)
            script += "tap " + std::to_string(t) + " " + at(a ? drums::Kick : drums::Clap) + "\n";
        const double space = taps[std::size(taps) - 1].first + 0.34;
        script += "key " + std::to_string(space) + " Space\n";
        // Longer: four rounds, two bars, the same tempo. Shorter twice: one
        // round, two beats. Long after, the buttons still have it.
        const auto longer = testkit::play(script + "key " + std::to_string(space + 1) + " Up\nend 16\n");
        QCOMPARE(int(longer.view.parts[0].clips[0].lengthBeats), 8);
        QCOMPARE(longer.view.parts[0].clips[0].events, 12);
        QVERIFY2(std::fabs(longer.view.bpm - 129) <= 3, qPrintable(QString::number(longer.view.bpm)));
        const auto shorter = testkit::play(script + "key " + std::to_string(space + 1) + " Down\nend 16\n",
                                           [](Session &ses) {
                                               QCOMPARE(ses.loopBeats(0), 2);
                                               QCOMPARE(ses.loopBeats(1), 4);
                                           });
        QCOMPARE(int(shorter.view.parts[0].clips[0].lengthBeats), 2);
        QCOMPARE(shorter.view.parts[0].clips[0].events, 3);
        QVERIFY2(std::fabs(shorter.view.bpm - 129) <= 3, qPrintable(QString::number(shorter.view.bpm)));
        // One take still: one Ctrl+Z, no drums.
        const auto undone = testkit::play(script + "key " + std::to_string(space + 1) + " Up\nkey " +
                                          std::to_string(space + 2) + " Ctrl+Z\nend 16\n");
        QCOMPARE(undone.view.parts[0].playing, -1);
    }

    // Space while a groove plays under the fingers keeps it the way a click
    // does: whole bars of the groove on the clock, the tempo unchanged, and
    // none of the taps and rolls tried before it.
    void spaceKeepsTheHeldGrooveWithoutWhatCameBefore() {
        const auto take = testkit::play("page 0 1\n"
                                        "tap 0.3 " + at(drums::Rim) + "\ntap 0.8 " + at(drums::Crash) +
                                        "\ndown 1.2 7 " + at(drums::Shaker) + "\nup 2.6 7\n" // a roll tried
                                        "down 3.00 1 " + at(drums::Kick) + "\ndown 3.02 2 " + at(drums::Clap) +
                                        "\ndown 3.04 3 " + at(drums::Hat) +
                                        "\nkey 6.2 Space\nup 7.0 1\nup 7.0 2\nup 7.0 3\nend 12\n");
        const auto &part = take.view.parts[0];
        QCOMPARE(part.playing, 0);
        // Held two bars and more: two bars of it.
        QCOMPARE(int(part.clips[0].lengthBeats), 8);
        // Kick 4, clap 2, hats 8 a bar: nothing of the rim, crash or shaker.
        QCOMPARE(part.clips[0].events, 28);
        QCOMPARE(int(std::lround(take.view.bpm)), 124);
        QVERIFY(take.rms(10, 12) > 0.02);
    }

    // A roll kept over the playing loop goes into it for a pass; Ctrl+Z
    // takes it back out, and the loop plays as it was.
    void ctrlZTakesBackARollKeptIntoTheLoop() {
        const std::string base = "page 0 1\n"
                                 "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Clap) +
                                 "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\n"
                                 "down 4.2 4 " + at(drums::Shaker) + "\nclick 4.4 len=0.15\nup 4.6 4\n";
        const auto kept = testkit::play(base + "end 8\n");
        const int before = 6, with = kept.view.parts[0].clips[0].events;
        QVERIFY2(with > before, qPrintable(QString::number(with)));
        const auto undone = testkit::play(base + "key 8.0 Ctrl+Z\nend 9\n");
        QCOMPARE(undone.view.parts[0].playing, 0);
        QCOMPARE(undone.view.parts[0].clips[0].events, before);
        // Before its pass begins (the click waits for the bar line), Ctrl+Z
        // takes back the kept roll, not the loop under it; mid-pass it takes
        // the pass back and the roll stops with it. Either way one Ctrl+Z is
        // one step: the loop plays on as it was.
        for (const char *when : {"4.5", "6.5"}) {
            const auto one = testkit::play(base + "key " + when + " Ctrl+Z\nend 10\n");
            QCOMPARE(one.view.parts[0].playing, 0);
            QCOMPARE(one.view.parts[0].clips[0].events, before);
            // Nothing of the shaker after the roll was taken back.
            QVERIFY2(one.rms(8.5, 10) < 1.5 * testkit::play("page 0 1\n"
                                                            "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " +
                                                            at(drums::Clap) + "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\nend 10\n")
                                                     .rms(8.5, 10),
                     when);
        }
    }

    // The mouse reaches what the keys do: a click on TECHNO under the pad
    // changes the kit, SWING + adds swing, a filled clip slot launches.
    void theBandUnderThePadIsClickable() {
        // On the scripts' window the pad is 562.5 px high: row 0 of the band
        // is 0.048 of it under the pad, row 1 0.119. The kits fill row 0
        // from 0.57 of the width, five across.
        testkit::play("page 0 1\nbutton 0.2 0.70 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("techno"), Qt::CaseInsensitive), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 1\nbutton 0.2 0.35 -0.119\nbutton 0.3 0.35 -0.119\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("58%")), qPrintable(s.pageStatus()));
        });
    }

    void aThumbThatComesDownToClickOnlyClicks() {
        // Two fingers groove; a thumb lands and clicks: the groove is kept,
        // nothing is grabbed for shaping.
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Hat) +
                                        "\ndown 2.0 9 0.5 0.05 size=1.8\nclick 2.05 len=0.15\n"
                                        "up 2.4 1\nup 2.4 2\nup 2.4 9\nend 8\n");
        QCOMPARE(take.view.parts[0].playing, 0);
        for (int p = 0; p < drums::PadCount; ++p)
            QCOMPARE(take.view.parts[0].data[40 + 3 * p], 0.0f);
    }

    void aClickWithALoopPlayingOverdubsOnePass() {
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Clap) +
                                        "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\n"
                                        "down 6.0 4 " + at(drums::Shaker) + "\ndown 6.02 5 " + at(drums::Open) +
                                        "\nclick 6.6 len=0.15\nup 6.9 4\nup 6.9 5\nend 14\n");
        const auto &part = take.view.parts[0];
        QCOMPARE(part.playing, 0);
        // Kick 4 and clap 2, then shaker 16 and open hats 4 in one pass.
        QCOMPARE(part.clips[0].events, 26);
        QVERIFY(part.clips[1].state == ClipView::Empty);
    }

    void aClickDragShapesTheDrum() {
        // Click on a fresh kick, drag right two degrees and up: tuned up and
        // longer, and the engine has it.
        const std::string drag = "page 0 1\n"
                                 "down 1.0 1 " + at(drums::Kick) + "\nclick 1.05 len=0.6\n"
                                 "glide 1.2 1.5 1 0.225 0.225\nup 1.8 1\n";
        const auto take = testkit::play(drag + "end 2\n");
        const float *data = take.view.parts[0].data;
        QCOMPARE(int(std::lround(data[40 + 3 * drums::Kick])), 2);
        QVERIFY(data[41 + 3 * drums::Kick] > 0.4f);
        // Delete puts it back.
        const auto reset = testkit::play(drag + "key 1.9 Delete\nend 2\n");
        QCOMPARE(reset.view.parts[0].data[40 + 3 * drums::Kick], 0.0f);
    }

    void deleteWithAPadHeldAndNoLoopResetsOnlyThatDrum() {
        // The kick and the snare are both shaped; Delete with the snare
        // held (and no loop to take it out of) puts back only the snare.
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.0 1 " + at(drums::Kick) + "\nclick 1.05 len=0.6\n"
                                        "glide 1.2 1.5 1 0.225 0.225\nup 1.8 1\n"
                                        "down 2.0 2 " + at(drums::Snare) + "\nclick 2.05 len=0.6\n"
                                        "glide 2.2 2.5 2 0.475 0.225\nup 2.8 2\n"
                                        "down 3.0 3 " + at(drums::Snare) + "\nkey 3.2 Delete\nup 3.4 3\nend 3.6\n");
        const float *data = take.view.parts[0].data;
        QCOMPARE(int(std::lround(data[40 + 3 * drums::Kick])), 2);
        QCOMPARE(data[40 + 3 * drums::Snare], 0.0f);
    }

    void the808HoldsAndGlidesThroughTheScale() {
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.0 1 " + at(drums::Sub) + "\nglide 1.5 1.7 1 0.225 0.375\nend 2.5\n");
        QCOMPARE(int(std::lround(take.view.parts[0].data[33])), 2);
        QVERIFY(take.view.parts[0].data[32] > 0.01f);
        QVERIFY(take.rms(2.2, 2.5) > 0.01);
        // Let go, and it stops.
        const auto gone = testkit::play("page 0 1\ndown 1.0 1 " + at(drums::Sub) + "\nup 1.5 1\nend 2.5\n");
        QVERIFY(gone.rms(2.0, 2.5) < 0.002);
    }

    void aFastRunAcrossPadsPlaysEachOne() {
        const auto take = testkit::play("page 0 1\n"
                                        "down 1.0 1 0.1 0.62\nglide 1.02 1.12 1 0.9 0.62\nup 1.15 1\nend 1.2\n");
        const float *data = take.view.parts[0].data;
        for (int pad : {drums::TomMid, drums::TomHi, drums::Perc})
            QVERIFY2(data[pad] > 0.2f, drums::padName(pad));
    }

    void keysArePads() {
        // Z is the kick; V held rolls the hat.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\nkey 1.0 Z\nkey 2.0 V down\nkey 3.0 V up\nend 3.2\n");
        QVERIFY(take.rms(1.0, 1.2) > 0.05);
        QVERIFY(onsets(take, 2.3, 3.0).size() >= 4);
    }

    void arrowsChangeTheKit() {
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\nkey 0.2 Right\nend 0.5\n");
        QCOMPARE(int(take.view.parts[0].data[34]), int(drums::Trap));
        const auto back = testkit::play("page 0 1\nkey 0.1 Left\nend 0.5\n");
        QCOMPARE(int(back.view.parts[0].data[34]), int(drums::Garage));
    }

    void leavingThePageStopsWhatFingersHold() {
        const auto take = testkit::play("page 0 1\ndown 0.5 1 " + at(drums::Hat) + "\nkey 1.5 2\nup 3.0 1\nend 3.0\n");
        QVERIFY(take.rms(0.9, 1.4) > 0.003);
        QVERIFY(take.rms(2.2, 3.0) < 0.0005);
    }

    // --- loops come only from an explicit act --------------------------------

    void holdingAfterHatsRollsAndNeverLoops() {
        const auto take = testkit::play("page 0 1\n"
                                        "tap 1.000 0.88 0.12 p=0.5 len=0.07\ntap 1.242 0.88 0.12 p=0.5 len=0.07\n"
                                        "tap 1.484 0.88 0.12 p=0.5 len=0.07\ntap 1.726 0.88 0.12 p=0.5 len=0.07\n"
                                        "down 1.968 1 0.88 0.12 p=0.5\nup 3.5 1\nend 5\n");
        QCOMPARE(take.view.parts[0].playing, -1);
        QVERIFY(take.view.parts[0].clips[0].state == ClipView::Empty);
        QVERIFY(onsets(take, 2.05, 3.5).size() >= 10);
    }

    void holdingTheKickAfterABeatRollsAndNeverLoops() {
        const auto take = testkit::play("page 0 1\n"
                                        "tap 1.000 0.12 0.12 p=0.6 len=0.07\ntap 1.484 0.38 0.12 p=0.6 len=0.07\n"
                                        "tap 1.968 0.12 0.12 p=0.6 len=0.07\ntap 2.452 0.38 0.12 p=0.6 len=0.07\n"
                                        "down 2.936 1 0.12 0.12 p=0.6\nup 4.5 1\nend 6\n");
        QCOMPARE(take.view.parts[0].playing, -1);
        // The kick rolls in quarters while it's held.
        QVERIFY(onsets(take, 3.1, 4.5, 0.2).size() >= 2);
    }

    void holdingTheSnareEarlyKeepsTheTempo() {
        const auto take = testkit::play("page 0 1\n"
                                        "tap 1.000 0.38 0.12 p=0.6 len=0.07\ntap 1.968 0.38 0.12 p=0.6 len=0.07\n"
                                        "down 2.80 1 0.38 0.12 p=0.6\nglide 3.0 5.0 1 0.38 0.5\nup 5.0 1\nend 6\n");
        QCOMPARE(take.view.parts[0].playing, -1);
        QCOMPARE(int(std::lround(take.view.bpm)), 124);
    }

    void ghostNotesOverALoopDontReplaceIt() {
        const auto take = testkit::play("page 0 1\n"
                                        "down 0.484 1 0.12 0.12 p=0.65\ndown 0.503 2 0.62 0.12 p=0.6\n"
                                        "down 0.523 3 0.88 0.12 p=0.55\nclick 1.645 len=0.12\n"
                                        "up 2.032 1\nup 2.032 2\nup 2.032 3\n"
                                        "tap 5.081 0.38 0.12 p=0.3 len=0.07\ntap 5.323 0.38 0.12 p=0.3 len=0.07\n"
                                        "tap 5.565 0.38 0.12 p=0.3 len=0.07\ndown 5.807 4 0.38 0.12 p=0.6\n"
                                        "up 7.3 4\nend 9\n");
        const auto &part = take.view.parts[0];
        QCOMPARE(part.playing, 0);
        QCOMPARE(int(part.clips[0].lengthBeats), 4);
        QVERIFY(part.clips[1].state == ClipView::Empty);
    }

    void spaceLoopsTheDrums() {
        // The session's Space: kick, snare, kick, snare, then Space on the
        // next one makes a bar at 120 BPM.
        const auto take = testkit::play("page 0 1\n"
                                        "tap 0.5 " + at(drums::Kick) + "\ntap 1.0 " + at(drums::Snare) + "\ntap 1.5 " +
                                        at(drums::Kick) + "\ntap 2.0 " + at(drums::Snare) + "\nkey 2.5 Space\nend 6\n");
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[0].clips[0].events, 4);
    }

    // --- rolls start where they can ------------------------------------------

    void aRollStartsOnTheFirstGridStepItCan() {
        // Straight sixteenths (techno) at 124: landing at 1.009 s, the first
        // step at least 90 ms later is the one at 1.2097 s, and nothing is
        // left out after it.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\n"
                                        "down 1.009 1 " + at(drums::Hat) + " p=0.6\nup 2.0 1\nend 2.2\n");
        const auto on = onsets(take, 0.95, 2.0);
        QVERIFY(on.size() >= 8);
        QVERIFY(std::fabs(on[0] - 1.009) < 0.012);
        const double firstStep = 10 * 0.25 * kSpb;
        QVERIFY2(std::fabs(on[1] - firstStep) < 0.004, qPrintable(QString::number(on[1])));
        for (size_t i = 2; i < on.size(); ++i)
            QVERIFY(std::fabs(on[i] - on[i - 1] - 0.25 * kSpb) < 0.003);
    }

    void aLongTapStaysOneHit() {
        // Down for 120 ms: past the hold, but lifted before the first step.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\ntap 1.009 " + at(drums::Hat) + " len=0.12\nend 2\n");
        QCOMPARE(int(onsets(take, 0.9, 2.0).size()), 1);
    }

    void longTapsDontSproutASecondHit() {
        // 24 taps spread across a sixteenth, at each length, on the hat
        // (1/16) and the rim (1/8), at 124 BPM in the house kit: a tap of
        // 110 ms never sprouts a step, one of 130 ms almost never.
        auto strays = [&](int pad, double len) {
            std::string s = "page 0 1\n";
            for (int i = 0; i < 24; ++i) {
                const double t = 1.0 + i * 1.0 + i * (0.25 * kSpb / 24.0);
                s += "tap " + std::to_string(t) + " " + at(pad) + " p=0.6 len=" + std::to_string(len) + "\n";
            }
            s += "end 26\n";
            const auto take = testkit::play(s);
            int extra = 0;
            for (int i = 0; i < 24; ++i) {
                const double t = 1.0 + i * 1.0 + i * (0.25 * kSpb / 24.0);
                extra += std::max(0, int(onsets(take, t - 0.02, t + 0.6).size()) - 1);
            }
            return extra;
        };
        QCOMPARE(strays(drums::Hat, 0.09), 0);
        QCOMPARE(strays(drums::Hat, 0.11), 0);
        QCOMPARE(strays(drums::Hat, 0.13), 0);
        QCOMPARE(strays(drums::Rim, 0.11), 0);
        QCOMPARE(strays(drums::Rim, 0.13), 0);
    }

    void aRollStartsAsLoudAsItsHitAndPressureSwellsIt() {
        // Straight techno snare held at three pressures: the first roll
        // steps sit within 1.5 dB of the hit that began them.
        auto peakAt = [](const testkit::Take &take, double t) {
            double p = 0;
            for (size_t i = size_t(t * kRate); i < size_t((t + 0.02) * kRate) && i < take.left.size(); ++i)
                p = std::max({p, double(std::fabs(take.left[i])), double(std::fabs(take.right[i]))});
            return 20 * std::log10(p);
        };
        for (const char *p : {"0.3", "0.5", "0.8"}) {
            const auto take = testkit::play(std::string("page 0 1\nkey 0.1 Right\ndown 1.0 1 ") + at(drums::Snare) +
                                            " p=" + p + "\nup 2.5 1\nend 2.6\n");
            const auto on = onsets(take, 0.95, 2.4);
            QVERIFY(on.size() >= 4);
            const double hit = peakAt(take, on[0]);
            for (size_t i = 1; i < 4; ++i)
                QVERIFY2(std::fabs(peakAt(take, on[i]) - hit) < 1.5,
                         qPrintable(QStringLiteral("p=%1 step %2: %3 dB against %4").arg(p).arg(i).arg(peakAt(take, on[i])).arg(hit)));
        }
        // Landing light and pressing hard swells the roll by 10 dB or more.
        const auto swell = testkit::play(std::string("page 0 1\nkey 0.1 Right\ndown 1.0 1 ") + at(drums::Snare) +
                                         " p=0.3\nglide 1.5 2.5 1 0.375 0.125 p=1.0\nup 3.5 1\nend 3.6\n");
        const auto on = onsets(swell, 0.95, 3.4);
        QVERIFY(on.size() >= 8);
        QVERIFY(peakAt(swell, on.back()) - peakAt(swell, on[1]) > 10.0);
    }

    void aThumbOnTheRepeatStripRollsFromTheLanding() {
        // Straight techno at 124: a thumb rests on the strip at 1/16, and
        // the hat lands on beat 4 (1.9355 s): every sixteenth from there,
        // with no hole after the landing.
        const double beat4 = 4 * kSpb;
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\ndown 1.0 9 0.5 0.02\n"
                                        "down " + std::to_string(beat4) + " 1 " + at(drums::Hat) +
                                        "\nup 3.0 1\nup 3.1 9\nend 3.2\n");
        const auto on = onsets(take, 1.5, 3.0);
        QVERIFY(on.size() >= 9);
        QVERIFY2(std::fabs(on[0] - beat4) < 0.004, qPrintable(QString::number(on[0])));
        for (size_t i = 1; i < on.size(); ++i)
            QVERIFY2(std::fabs(on[i] - on[i - 1] - 0.25 * kSpb) < 0.003, qPrintable(QString::number(on[i] - on[i - 1])));
        // Nothing on the strip sounds by itself.
        QVERIFY(take.rms(1.0, 1.9) < 0.0005);
    }

    void theStripPicksTheRate() {
        // The thumb further right, at 1/32: a roll every 60.5 ms from landing.
        const double beat4 = 4 * kSpb;
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\ndown 1.0 9 0.79 0.02\n"
                                        "down " + std::to_string(beat4) + " 1 " + at(drums::Hat) +
                                        "\nup 3.0 1\nup 3.1 9\nend 3.2\n");
        const auto on = onsets(take, 1.5, 3.0, 0.02);
        QVERIFY(on.size() >= 16);
        QVERIFY(std::fabs(on[0] - beat4) < 0.004);
        for (size_t i = 1; i < on.size(); ++i)
            QVERIFY2(std::fabs(on[i] - on[i - 1] - 0.125 * kSpb) < 0.006, qPrintable(QString::number(on[i] - on[i - 1])));
    }

    void heldBRepeatsKeysAndALandingJustEarlySnapsToTheStep() {
        // B held: the hat key pressed 20 ms before beat 4 plays on beat 4,
        // then every sixteenth.
        const double beat4 = 4 * kSpb;
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\nkey 1.0 B down\n"
                                        "key " + std::to_string(beat4 - 0.02) + " V down\nkey 3.0 V up\nkey 3.1 B up\nend 3.2\n");
        const auto on = onsets(take, 1.5, 3.0);
        QVERIFY(on.size() >= 9);
        QVERIFY2(std::fabs(on[0] - beat4) < 0.004, qPrintable(QString::number(on[0])));
        for (size_t i = 1; i < on.size(); ++i)
            QVERIFY(std::fabs(on[i] - on[i - 1] - 0.25 * kSpb) < 0.003);
    }

    void aQuickTapWithRepeatIsOneHit() {
        // REPEAT at 1/16 (house, swung): 20 taps of 60 ms across a step,
        // each a single hit; at 1/32 never more than two.
        auto hits = [&](float stripX) {
            std::string s = "page 0 1\ndown 0.5 99 " + std::to_string(stripX) + " 0.02\n";
            std::vector<double> at;
            for (int i = 0; i < 20; ++i) {
                const double t = 1.0 + i * 0.9 + i * (0.25 * kSpb / 20.0);
                at.push_back(t);
                s += "tap " + std::to_string(t) + " 0.38 0.375 p=0.6 len=0.06\n";
            }
            s += "end 20\n";
            const auto take = testkit::play(s);
            std::vector<int> n;
            for (double t : at)
                n.push_back(int(onsets(take, t - 0.02, t + 0.5, 0.02).size()));
            return n;
        };
        for (int n : hits(0.5f))
            QCOMPARE(n, 1);
        for (int n : hits(0.79f))
            QVERIFY(n >= 1 && n <= 2);
    }

    void aLiftStopsARollAtOnce() {
        // A build pushed to 1/32 and let go 40 ms before the drop: nothing
        // after the lift, so the drop lands in a gap. (A hat, whose hits a
        // detector can tell apart at that rate; a snare's run together.)
        const double drop = 12 * kSpb;
        const auto take = testkit::play("page 0 1\ndown " + std::to_string(4 * kSpb) +
                                        " 1 0.88 0.08 p=0.6\nglide 2.4194 2.9032 1 0.88 0.30 p=0.9\nup " +
                                        std::to_string(drop - 0.04) + " 1\nend " + std::to_string(drop + 0.3) + "\n");
        // (One window over the roll and the drop, so the tail after the
        // lift is judged against the roll, not against itself.)
        const auto on = onsets(take, drop - 0.5, drop + 0.3, 0.02);
        QVERIFY(on.size() >= 6);
        QVERIFY2(on.back() < drop - 0.04 + 0.015, qPrintable(QString::number(on.back() - (drop - 0.04))));
    }

    void anEarlyRepeatLandingWaitsForItsStep() {
        // REPEAT at 1/16, hat landing 40 and 50 ms before a step: it lands on
        // the step, and no two hits come closer than half a step.
        for (double early : {0.04, 0.05}) {
            const double step = 4 * kSpb;
            const auto take = testkit::play("page 0 1\nkey 0.1 Right\ndown 0.5 99 0.5 0.02\ndown " +
                                            std::to_string(step - early) + " 1 " + at(drums::Hat) + "\nup " +
                                            std::to_string(step + 0.5) + " 1\nend " + std::to_string(step + 0.6) + "\n");
            const auto on = onsets(take, step - 0.1, step + 0.5);
            QVERIFY(on.size() >= 4);
            QVERIFY2(std::fabs(on[0] - step) < 0.004, qPrintable(QString::number(on[0] - step)));
            for (size_t i = 1; i < on.size(); ++i)
                QVERIFY(on[i] - on[i - 1] > 0.125 * kSpb);
        }
    }

    void aHeldKeyRollsOnTimeWithOnlyASlowTick() {
        // No frames at all, a tick every 16 ms (a slow UI timer), and the
        // hat key held in the straight techno kit: every step still lands
        // on the grid, none late.
        Studio studio(kRate);
        studio.setTimeOrigin(0);
        setScriptClock(0);
        Session session(&studio);
        session.setClock(&scriptClock);
        session.setPage(0);
        testkit::Take take;
        float buf[768 * 2];
        auto press = [&](int key, bool down) {
            KeyEvent k;
            k.key = key;
            k.down = down;
            session.key(k);
        };
        for (int tick = 0; tick * 0.016 < 3.0; ++tick) {
            const double t = tick * 0.016;
            setScriptClock(t);
            session.refresh();
            if (tick == 6)
                press(Qt::Key_Right, true);
            if (tick == 62)
                press(Qt::Key_V, true);
            if (tick == 175)
                press(Qt::Key_V, false);
            session.idle(t);
            studio.render(buf, 768);
            for (int i = 0; i < 768; ++i) {
                take.left.push_back(buf[2 * i]);
                take.right.push_back(buf[2 * i + 1]);
            }
        }
        const auto on = onsets(take, 1.3, 2.75);
        QVERIFY(on.size() >= 10);
        const double lag = offGridMs(on[0], 0.25);
        QVERIFY(lag > -0.5 && lag < 3.0);
        for (double t : on)
            QVERIFY2(std::fabs(offGridMs(t, 0.25) - lag) < 0.8, qPrintable(QString::number(offGridMs(t, 0.25))));
    }

    void aFatFirmHitLowOnTheKickStillPlays() {
        // A wide contact near the edge is a resting thumb only when it's
        // light; pressed firmly it's a kick.
        const auto kick = testkit::play("page 0 1\ndown 1.0 1 0.12 0.10 p=0.7 size=1.5\nup 1.1 1\nend 1.5\n");
        QVERIFY(kick.rms(1.0, 1.3) > 0.01);
        const auto thumb = testkit::play("page 0 1\ndown 1.0 1 0.12 0.10 p=0.2 size=1.5\nup 1.6 1\nend 1.7\n");
        QVERIFY(thumb.rms(0.9, 1.7) < 0.0005);
    }

    void aFastRepeatsFirstStepIsNotRushed() {
        // REPEAT at 1/32: landing 20 ms after a step (past a quarter step)
        // doesn't count as it; no two hits come closer than 3/4 of a step.
        const double step = 4 * kSpb;
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\ndown 0.5 99 0.79 0.02\ndown " +
                                        std::to_string(step + 0.02) + " 1 " + at(drums::Hat) + "\nup " +
                                        std::to_string(step + 0.6) + " 1\nend " + std::to_string(step + 0.7) + "\n");
        const auto on = onsets(take, step - 0.05, step + 0.55, 0.02);
        QVERIFY(on.size() >= 6);
        for (size_t i = 1; i < on.size(); ++i)
            QVERIFY2(on[i] - on[i - 1] > 0.75 * 0.125 * kSpb, qPrintable(QString::number(on[i] - on[i - 1])));
    }

    void swingLeavesThirtySecondsEven() {
        // Garage swings 62%, but a rim rolling in thirty-seconds stays even.
        const auto take = testkit::play("page 0 1\nkey 0.1 Left\n"
                                        "down 1.0 1 0.38 0.27 p=0.6\nglide 1.3 1.4 1 0.38 0.56\nup 4.0 1\nend 4.5\n");
        const auto on = onsets(take, 2.0, 3.9, 0.02);
        QVERIFY(on.size() >= 25);
        for (size_t i = 1; i < on.size(); ++i)
            QVERIFY2(std::fabs(on[i] - on[i - 1] - 0.125 * kSpb) < 0.006, qPrintable(QString::number(on[i] - on[i - 1])));
    }

    void theTopRowClimbsThreeRungs() {
        // The cowbell held at the far edge still climbs from 1/8 to 1/16T.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\n"
                                        "down 1.0 1 0.62 0.86 p=0.6\nglide 1.3 1.4 1 0.62 0.985\nup 3.5 1\nend 3.6\n");
        const auto on = onsets(take, 2.0, 3.4, 0.02);
        QVERIFY(on.size() >= 6);
        const double gap = (on.back() - on.front()) / double(on.size() - 1);
        QVERIFY2(std::fabs(gap - kSpb / 6) < 0.003, qPrintable(QString::number(gap)));
    }

    // --- loops keep their sound ----------------------------------------------

    void aLoopKeepsItsKit() {
        // A trap groove kept as a loop, then the page turns to breaks: the
        // loop's bars still sound the same.
        const std::string keep = "page 0 1\nkey 0.1 Right\nkey 0.15 Right\n"
                                 "down 1.00 1 " + at(drums::Snare) + "\ndown 1.02 2 " + at(drums::Hat) +
                                 "\ndown 1.04 3 " + at(drums::Kick) + "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\nup 2.4 3\n";
        const auto same = testkit::play(keep + "end 14\n");
        const auto turned = testkit::play(keep + "key 8.0 Right\nend 14\n");
        // Bars 6 and 7 (beats 24..32) come round after the kit changed.
        const double a = energy(same, 24 * kSpb, 32 * kSpb), b = energy(turned, 24 * kSpb, 32 * kSpb);
        QVERIFY2(std::fabs(10 * std::log10(b / a)) < 0.3, qPrintable(QString::number(10 * std::log10(b / a))));
        QCOMPARE(int(turned.view.parts[0].data[34]), int(drums::Breaks));
        // And a hit that names its kit sounds as that kit whatever the engine's.
        Ctx ctx;
        drums::Engine e;
        e.init(kRate);
        Command c;
        c.type = drums::Hit;
        c.i[0] = drums::Snare;
        c.i[2] = drums::Trap + 1;
        c.f[0] = 1.0f;
        e.command(c, ctx);
        float l[kBlock], r[kBlock];
        double named = 0;
        for (int i = 0; i < 300; ++i) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
            for (float v : l)
                named += double(v) * v;
        }
        const Dry trap = strike(drums::Snare, drums::Trap, 1.0f, 300.0 * kBlock / kRate, ctx);
        double direct = 0;
        for (float v : trap.l)
            direct += double(v) * v;
        QVERIFY(std::fabs(named / direct - 1.0) < 0.02);
    }

    void aLoopKeepsItsShaping() {
        // A kick tuned up and kept, then the edits are cleared: the loop
        // still plays the tuned kick (its pitch is in every hit).
        const std::string keep = "page 0 1\n"
                                 "down 1.0 1 " + at(drums::Kick) + "\nclick 1.05 len=0.6\n"
                                 "glide 1.2 1.5 1 0.275 0.125\nup 1.8 1\n"
                                 "down 2.0 2 " + at(drums::Kick) + "\ndown 2.02 3 " + at(drums::Hat) +
                                 "\nclick 3.0 len=0.15\nup 3.4 2\nup 3.4 3\n";
        const auto kept = testkit::play(keep + "end 12\n");
        const auto cleared = testkit::play(keep + "key 7.0 Delete\nend 12\n");
        QCOMPARE(cleared.view.parts[0].data[40 + 3 * drums::Kick], 0.0f);
        const double a = energy(kept, 9.0, 12.0), b = energy(cleared, 9.0, 12.0);
        QVERIFY(std::fabs(10 * std::log10(b / a)) < 0.3);
    }

    // --- taking a drum out of a loop -----------------------------------------

    void holdingAPadAndDeleteTakesItOutOfTheLoop() {
        // Kick, clap and hat kept from beat 8 (3.87 s); the clap lands on beats
        // 1 and 3 of every bar. Hold the clap and press Delete: from then on
        // the loop plays no clap; again, and it's back.
        const std::string keep = "page 0 1\n"
                                 "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Clap) +
                                 "\ndown 1.04 3 " + at(drums::Hat) +
                                 "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\nup 2.4 3\n"
                                 "down 6.0 4 " + at(drums::Clap) + "\nkey 6.3 Delete\nup 6.6 4\n";
        const double clapAt = 25 * kSpb + 0.03;
        const auto out = testkit::play(keep + "end " + std::to_string(clapAt) + "\n");
        QCOMPARE(out.view.parts[0].playing, 0);
        QVERIFY(out.view.parts[0].data[16 + drums::Clap] < 0.05f);
        QVERIFY(out.view.parts[0].data[16 + drums::Kick] > 0.3f);
        const auto back = testkit::play(keep + "down 8.0 5 " + at(drums::Clap) + "\nkey 8.3 Delete\nup 8.6 5\nend " +
                                        std::to_string(clapAt) + "\n");
        QVERIFY(back.view.parts[0].data[16 + drums::Clap] > 0.3f);
    }

    // --- loop lengths ----------------------------------------------------------

    void aGrooveHeldLongerKeepsMoreBars() {
        auto held = [&](double clickAt) {
            const auto take = testkit::play("page 0 1\n"
                                            "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Hat) +
                                            "\nclick " + std::to_string(clickAt) + " len=0.12\nup " +
                                            std::to_string(clickAt + 0.3) + " 1\nup " + std::to_string(clickAt + 0.3) +
                                            " 2\nend " + std::to_string(clickAt + 6) + "\n");
            return int(take.view.parts[0].clips[0].lengthBeats);
        };
        QCOMPARE(held(2.0), 4);
        QCOMPARE(held(5.5), 8);
        QCOMPARE(held(9.5), 16);
    }

    // --- sound -----------------------------------------------------------------

    void nothingFizzesAboveEighteenKilohertz() {
        for (int kit = 0; kit < drums::KitCount; ++kit)
            for (int pad = 0; pad < drums::PadCount; ++pad) {
                Ctx ctx;
                const double f = fizz(strike(pad, kit, 1.0f, 1.0, ctx));
                QVERIFY2(f < 0.02, qPrintable(QStringLiteral("%1 in %2: %3%").arg(drums::padName(pad, kit),
                                                                                    drums::kitName(kit)).arg(f * 100)));
            }
    }

    void aGhostIsFarQuieterThanAnAccent() {
        Ctx ctx;
        for (int pad : {drums::Snare, drums::Hat, drums::Kick}) {
            const float ghost = strike(pad, drums::House, 0.17f, 0.5, ctx).peak();
            const float accent = strike(pad, drums::House, 1.0f, 0.5, ctx).peak();
            QVERIFY2(20 * std::log10(accent / ghost) > 18.0, drums::padName(pad));
        }
    }

    void clipsDontDoubleWhatWasJustPlayedLive() {
        // A replayed hit right after a live one on the same pad is dropped.
        Ctx ctx;
        drums::Engine e;
        e.init(kRate);
        Command live;
        live.type = drums::Hit;
        live.i[0] = drums::Snare;
        live.f[0] = 1.0f;
        live.t = ctx.time + 0.01;
        e.command(live, ctx);
        Command replay = live;
        replay.id = (1 << 30) | 5;
        e.command(replay, ctx);
        float l[kBlock], r[kBlock];
        for (int b = 0; b < 40; ++b) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
        }
        QCOMPARE(e.voices(), 1);
    }

    void patternsAreWhatTheKitsPromise() {
        auto ticks = [](const drums::Pattern &p) {
            std::vector<int> t;
            for (int i = 0; i < p.count; ++i)
                t.push_back(p.steps[i].tick);
            return t;
        };
        // House: four on the floor, a backbeat clap, offbeat open hats.
        QCOMPARE(ticks(drums::groovePattern(drums::Kick, drums::House, 1, 0)), (std::vector<int>{0, 24, 48, 72}));
        QCOMPARE(ticks(drums::groovePattern(drums::Clap, drums::House, 1, 0)), (std::vector<int>{24, 72}));
        QCOMPARE(ticks(drums::groovePattern(drums::Open, drums::House, 1, 0)), (std::vector<int>{12, 36, 60, 84}));
        // Trap: the snare on three, hats rolling in triplets at the end.
        QCOMPARE(ticks(drums::groovePattern(drums::Snare, drums::Trap, 1, 0)), (std::vector<int>{48}));
        const auto hats = ticks(drums::groovePattern(drums::Hat, drums::Trap, 1, 0));
        QVERIFY(std::find(hats.begin(), hats.end(), 88) != hats.end());
        // Euclid: five in sixteen.
        int n = 0;
        for (int s = 0; s < 16; ++s)
            n += drums::euclid(5, 16, 0, s);
        QCOMPARE(n, 5);
        // Shifting moves every step a sixteenth.
        QCOMPARE(ticks(drums::groovePattern(drums::Clap, drums::House, 1, 1)), (std::vector<int>{30, 78}));
    }
};

TESTKIT_REGISTER(DrumsTests)
#include "drums_tests.moc"
