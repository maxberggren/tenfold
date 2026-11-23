// The Drums page: its sounds, its timing, and every gesture it promises.

#include <QJsonArray>
#include <climits>
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

// A dry sound's brightness: the RMS frequency of its spectrum, from how fast
// it moves against how big it is (sqrt of the derivative's power over the
// signal's), in Hz.
double rmsHz(const Dry &d) {
    double dx = 0, x = 0;
    for (size_t i = 1; i < d.l.size(); ++i) {
        const double a = 0.5 * (d.l[i] + d.r[i]), b = 0.5 * (d.l[i - 1] + d.r[i - 1]);
        dx += (a - b) * (a - b);
        x += a * a;
    }
    return x > 0 ? std::sqrt(dx / x) * kRate / (2 * M_PI) : 0;
}

// How long a dry sound takes to fall 30 dB below its peak, in seconds.
double fallTime(const Dry &d) {
    const float top = d.peak();
    size_t last = 0;
    for (size_t i = 0; i < d.l.size(); ++i)
        if (std::max(std::fabs(d.l[i]), std::fabs(d.r[i])) > top * 0.0316f)
            last = i;
    return double(last) / kRate;
}

// Where a dry sound is loudest, in seconds.
double peakTime(const Dry &d) {
    size_t at = 0;
    float top = 0;
    for (size_t i = 0; i < d.l.size(); ++i)
        if (std::fabs(d.l[i]) > top) {
            top = std::fabs(d.l[i]);
            at = i;
        }
    return double(at) / kRate;
}

// The RMS of one channel of a dry sound between two times.
double dryRms(const Dry &d, double from, double to) {
    double s = 0;
    int n = 0;
    for (size_t i = size_t(from * kRate); i < size_t(to * kRate) && i < d.l.size(); ++i, ++n)
        s += double(d.l[i]) * d.l[i];
    return n ? std::sqrt(s / n) : 0;
}

// A kit's groove (kick, snare, clap, hat and its bass at density 1, swung
// as the page swings it) played straight into an engine for four bars at
// 120 BPM, in the key of ctx. The engine's output, both ears.
Dry grooveTake(int kit, Ctx &ctx) {
    drums::Engine e;
    e.init(kRate);
    Command k;
    k.type = Command::Param;
    k.i[0] = drums::ParamKit;
    k.i[1] = kit;
    e.command(k, ctx);
    struct Ev {
        long at;
        int pad, degree;
        float vel;
    };
    std::vector<Ev> evs;
    const double spb = 0.5, swing = drums::kitSwing(kit);
    for (int pad : {drums::Kick, drums::Snare, drums::Clap, drums::Hat, drums::Sub}) {
        const drums::Pattern p = drums::groovePattern(pad, kit, 1, 0);
        bool sixteenths = true;
        for (int i = 0; i < p.count; ++i)
            sixteenths &= p.steps[i].tick % 6 == 0;
        for (int bar = 0; bar < 4; ++bar)
            for (int i = 0; i < p.count; ++i) {
                const drums::Step &s = p.steps[i];
                double beat = bar * 4 + s.tick / 24.0;
                if (p.triplet && s.tick % 24 == 12)
                    beat += swing - 0.5;
                else if (!p.triplet && sixteenths && s.tick % 12 == 6)
                    beat += (swing - 0.5) * 0.5;
                evs.push_back({long(beat * spb * kRate), pad, s.degree, s.vel});
            }
    }
    Dry d;
    float l[kBlock], r[kBlock];
    for (long b = 0; b < long(16 * spb * kRate) / kBlock; ++b) {
        for (const Ev &ev : evs)
            if (ev.at >= b * kBlock && ev.at < (b + 1) * kBlock) {
                Command c;
                c.type = drums::Hit;
                c.i[0] = ev.pad;
                c.i[1] = ev.degree;
                c.f[0] = ev.vel;
                c.f[1] = c.f[2] = 0.5f;
                c.f[5] = float((kit == drums::Brushes || kit == drums::Cr78 ? 0.95 : 0.4) * spb); // as the page holds it
                c.t = ctx.time + double(ev.at - b * kBlock) / kRate;
                e.command(c, ctx);
            }
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        e.render(l, r, kBlock, ctx);
        d.l.insert(d.l.end(), l, l + kBlock);
        d.r.insert(d.r.end(), r, r + kBlock);
    }
    return d;
}

// How loud a take sounds, roughly as an ear (and a laptop) hears it: the
// lows below 100 Hz rolled off, the presence above 1.5 kHz lifted, its RMS
// over the last three bars in dB.
double loudness(const Dry &d) {
    Svf hp, hp2, shelf;
    hp.set(100.0f, 0.7f, kRate);
    hp2.set(100.0f, 0.7f, kRate);
    shelf.set(1500.0f, 0.7f, kRate);
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < d.l.size(); ++i) {
        const float x = hp2.step(hp.step(0.5f * (d.l[i] + d.r[i])).high).high;
        const float y = x + 0.6f * shelf.step(x).high;
        if (i >= d.l.size() / 4) {
            sum += double(y) * y;
            ++n;
        }
    }
    return 10 * std::log10(sum / double(std::max<size_t>(n, 1)) + 1e-20);
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
                // The gong ageng, the slenthem, the kenong and the tanpura
                // ring longest.
                const bool longest = (kit == drums::Tabla && pad == drums::Crash) ||
                                     (kit == drums::Gamelan &&
                                      (pad == drums::Crash || pad == drums::Shaker || pad == drums::Open));
                const Dry d = strike(pad, kit, 1.0f, longest ? 16.0 : 9.0, ctx, &e);
                QVERIFY2(d.finite(), drums::padName(pad));
                QVERIFY2(d.peak() > 0.01f, drums::padName(pad));
                QVERIFY2(d.peak() < 0.6f, drums::padName(pad));
                // Every voice ends by itself, the crash last.
                QVERIFY2(e.voices() == 0, qPrintable(QStringLiteral("%1 in %2").arg(drums::padName(pad, kit),
                                                                                     drums::kitName(kit))));
            }
    }

    void theKickLeadsAndTheHatsSitWellBelow() {
        // (The hang has no kick: it is one instrument.)
        for (int kit = 0; kit < drums::Hang; ++kit) {
            Ctx ctx;
            const float kick = strike(drums::Kick, kit, 1.0f, 1.0, ctx).peak();
            for (int pad = 0; pad < drums::PadCount; ++pad) {
                if (pad == drums::Kick || pad == drums::Sub)
                    continue;
                const float other = strike(pad, kit, 1.0f, 1.0, ctx).peak();
                QVERIFY2(other < kick, qPrintable(QStringLiteral("%1 in %2: %3 over the kick's %4")
                                                      .arg(drums::padName(pad, kit), drums::kitName(kit))
                                                      .arg(other)
                                                      .arg(kick)));
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

    // A tap just after Space is the next hit, not the hand still going: on
    // the drums it sounds (other pages let a finger landing then pass).
    void aTapJustAfterSpaceSounds() {
        const std::string s = "page 0 1\ntap 0.5 " + at(drums::Kick) + "\ntap 1.0 " + at(drums::Kick) + "\ntap 1.5 " +
                              at(drums::Kick) + "\ntap 2.0 " + at(drums::Kick) + "\nkey 2.45 Space\n";
        const std::string clap = "tap 2.57 " + at(drums::Clap) + " p=0.6 len=0.06\n";
        const auto without = testkit::play(s + "end 3.2\n");
        const auto with = testkit::play(s + clap + "end 3.2\n");
        const auto alone = testkit::play("page 0 1\n" + clap + "end 3.2\n");
        QVERIFY(with.error.empty());
        // What the clap added over the loop, against the clap on its own.
        const double a = with.rms(2.57, 2.67), b = without.rms(2.57, 2.67);
        const double added = std::sqrt(std::max(0.0, a * a - b * b));
        QVERIFY2(added > 0.5 * alone.rms(2.57, 2.67),
                 qPrintable(QString("added %1, the clap alone %2").arg(added).arg(alone.rms(2.57, 2.67))));
    }

    // A finger that comes down softly reads light on its first frame; its
    // hit waits for the pad's next readings and is as loud as it pressed,
    // even when the app's clock (its ticker, every few milliseconds) passes
    // the wait before the next frame arrives.
    void aSoftLandingIsAsLoudAsItPressed() {
        auto level = [](const std::string &script) {
            const auto take = testkit::watch(script, [](double t, Session &s) { s.idle(t + 0.007); });
            double p = 0;
            for (size_t i = size_t(1.0 * kRate); i < size_t(1.1 * kRate) && i < take.left.size(); ++i)
                p = std::max({p, double(std::fabs(take.left[i])), double(std::fabs(take.right[i]))});
            return p;
        };
        const std::string pad = at(drums::Snare);
        const double firm = level("page 0 1\ndown 1.0 1 " + pad + " p=0.5\nup 1.08 1\nend 1.5\n");
        const double soft = level("page 0 1\ndown 1.0 1 " + pad + " p=0.02\nmove 1.008 1 " + pad + " p=0.3\nmove 1.016 1 " +
                                  pad + " p=0.5\nmove 1.024 1 " + pad + " p=0.45\nup 1.08 1\nend 1.5\n");
        QVERIFY2(soft > 0.7 * firm, qPrintable(QString("soft %1, firm %2").arg(soft).arg(firm)));
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

    // The drums have no octave: ↑ and ↓ make the loop longer or shorter at
    // any time, as the Loop longer / Loop shorter buttons do, not only in
    // the moments after Space.
    void arrowsResizeTheLoopLongAfterSpace() {
        std::string script = "page 0 1\n";
        const double kicks[] = {1.0, 1.94, 2.86, 3.8, 4.73, 5.66};
        for (double t : kicks)
            script += "tap " + std::to_string(t) + " " + at(drums::Kick) + "\n";
        const double space = 6.6;
        script += "key " + std::to_string(space) + " Space\n";
        const auto made = testkit::play(script + "end 14\n");
        const int beats = int(made.view.parts[0].clips[0].lengthBeats);
        QVERIFY(beats > 0);
        const double bpm = made.view.bpm;
        // Five seconds on, well past the moment after Space.
        const auto longer = testkit::play(script + "key " + std::to_string(space + 5) + " Up\nend 14\n");
        QCOMPARE(int(longer.view.parts[0].clips[0].lengthBeats), 2 * beats);
        QVERIFY2(std::fabs(longer.view.bpm - bpm) <= 3, qPrintable(QString("%1 then %2").arg(bpm).arg(longer.view.bpm)));
        const auto shorter = testkit::play(script + "key " + std::to_string(space + 5) + " Up\nkey " +
                                           std::to_string(space + 6) + " Down\nkey " + std::to_string(space + 7) +
                                           " Down\nend 14\n");
        QCOMPARE(int(shorter.view.parts[0].clips[0].lengthBeats), beats / 2);
        // The chips under the pad do the same: LOOP + is the fifth of the
        // seven in row 1 of the band, LOOP − the one before it.
        const auto clicked = testkit::play(script + "button " + std::to_string(space + 5) + " 0.30 -0.119\nend 14\n");
        QCOMPARE(int(clicked.view.parts[0].clips[0].lengthBeats), 2 * beats);
        const auto clickedShorter =
            testkit::play(script + "button " + std::to_string(space + 5) + " 0.225 -0.119\nend 14\n");
        QCOMPARE(int(clickedShorter.view.parts[0].clips[0].lengthBeats), beats / 2);
    }

    // Held keys that roll still take the arrows for their rate.
    void arrowsSpeedUpAHeldKeyRoll() {
        const std::string held = "page 0 1\nkey 0.1 Right\nkey 1.0 V down\n";
        const auto plain = testkit::play(held + "key 3.0 V up\nend 3.2\n");
        const auto faster = testkit::play(held + "key 1.5 Up\nkey 3.0 V up\nend 3.2\n");
        QVERIFY(onsets(faster, 1.7, 2.9, 0.02).size() > onsets(plain, 1.7, 2.9, 0.02).size());
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

    // The mouse reaches what the keys do: a click on TECHNO or TABLA under
    // the pad changes the kit, the faded chip at the drum kits' end leads
    // on to the ones past it, swing's + adds swing, a filled clip slot
    // launches.
    void theBandUnderThePadIsClickable() {
        // On the scripts' window the pad is 562.5 px high: row 0 of the band
        // is 0.048 of it under the pad, row 1 0.119. The kits fill both rows
        // from 0.57 of the width: row 0 a window onto the ten drum kits
        // (the console's chips, faded at the ends), row 1 the five from
        // the world's music;
        // row 1's left 0.55 holds swing −, the swing, swing + (0.14..0.19),
        // loop −, loop +, overdub and back.
        testkit::play("page 0 1\nbutton 0.2 0.72 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("techno"), Qt::CaseInsensitive), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 1\nbutton 0.2 0.70 -0.119\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("tabla"), Qt::CaseInsensitive), qPrintable(s.pageStatus()));
        });
        // The faded chip at the row's right end is the next kit past the
        // window: clicking it chooses that kit and the window follows, so
        // clicking there again and again walks on to the last drum kit,
        // and the first click already leaves house behind.
        testkit::play("page 0 1\nbutton 0.2 0.985 -0.048\nend 0.4\n", [](Session &s) {
            QVERIFY2(!s.pageStatus().contains(QStringLiteral("house"), Qt::CaseInsensitive), qPrintable(s.pageStatus()));
        });
        std::string walk = "page 0 1\n";
        for (int i = 0; i < 10; ++i)
            walk += "button " + std::to_string(0.2 + 0.1 * i) + " 0.985 -0.048\n";
        testkit::play(walk + "end 1.4\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("brushes"), Qt::CaseInsensitive), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 1\nbutton 0.2 0.162 -0.119\nbutton 0.3 0.162 -0.119\nend 0.5\n", [](Session &s) {
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
        // Left from the first kit wraps round to the last.
        const auto back = testkit::play("page 0 1\nkey 0.1 Left\nend 0.5\n");
        QCOMPARE(int(back.view.parts[0].data[34]), int(drums::KitCount - 1));
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
        // Straight sixteenths (techno) at 124: landing at 1.0 s, the first
        // step at least 200 ms later is the one at 1.2097 s, and nothing is
        // left out after it.
        const auto take = testkit::play("page 0 1\nkey 0.1 Right\n"
                                        "down 1.0 1 " + at(drums::Hat) + " p=0.6\nup 2.0 1\nend 2.2\n");
        const auto on = onsets(take, 0.95, 2.0);
        QVERIFY(on.size() >= 8);
        QVERIFY(std::fabs(on[0] - 1.0) < 0.012);
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
        // (1/16) and the rim (1/8), at 124 BPM in the house kit: a tap of up
        // to 170 ms never sprouts a step, the app's ticker running between
        // the pad's frames or not.
        auto strays = [&](int pad, double len, bool ticker = false) {
            std::string s = "page 0 1\n";
            for (int i = 0; i < 24; ++i) {
                const double t = 1.0 + i * 1.0 + i * (0.25 * kSpb / 24.0);
                s += "tap " + std::to_string(t) + " " + at(pad) + " p=0.6 len=" + std::to_string(len) + "\n";
            }
            s += "end 26\n";
            const auto take = ticker ? testkit::watch(s, [](double t, Session &ses) { ses.idle(t + 0.004); })
                                     : testkit::play(s);
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
        QCOMPARE(strays(drums::Hat, 0.15), 0);
        QCOMPARE(strays(drums::Hat, 0.17), 0);
        QCOMPARE(strays(drums::Hat, 0.17, true), 0);
        QCOMPARE(strays(drums::Rim, 0.11), 0);
        QCOMPARE(strays(drums::Rim, 0.13), 0);
        QCOMPARE(strays(drums::Rim, 0.17), 0);
        QCOMPARE(strays(drums::Rim, 0.17, true), 0);
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
        // The brushes swing a full triplet (66%), but a cross-stick rolling
        // in thirty-seconds stays even.
        // Brushes is the tenth kit: eight to the left of house.
        const auto take = testkit::play("page 0 1\nkey 0.1 Left\nkey 0.11 Left\nkey 0.12 Left\nkey 0.13 Left\n"
                                        "key 0.14 Left\nkey 0.15 Left\nkey 0.16 Left\nkey 0.17 Left\n"
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

    // --- the kits beyond the dance floor ----------------------------------------

    void everyNewKitSoundsItsOwn() {
        // Heard blind, each of the five new kits differs from every other
        // kit in at least three of its main drums, in brightness or in how
        // long they ring.
        static const int pads[] = {drums::Kick,  drums::Snare, drums::Clap, drums::Hat,
                                   drums::Perc, drums::Ride,  drums::Zap};
        struct Look {
            double hz, fall;
        };
        std::vector<std::vector<Look>> looks(drums::KitCount);
        for (int kit = 0; kit < drums::KitCount; ++kit)
            for (int pad : pads) {
                Ctx ctx;
                const Dry d = strike(pad, kit, 1.0f, 1.5, ctx);
                looks[size_t(kit)].push_back({rmsHz(d), fallTime(d)});
            }
        for (int a = drums::BoomBap; a < drums::KitCount; ++a)
            for (int b = 0; b < drums::KitCount; ++b) {
                if (a == b)
                    continue;
                int differ = 0;
                for (size_t i = 0; i < std::size(pads); ++i) {
                    const Look &x = looks[size_t(a)][i], &y = looks[size_t(b)][i];
                    differ += std::fabs(std::log2(x.hz / y.hz)) > 0.2 || std::fabs(std::log2(x.fall / y.fall)) > 0.3;
                }
                QVERIFY2(differ >= 3, qPrintable(QStringLiteral("%1 and %2 alike in all but %3")
                                                     .arg(drums::kitName(a), drums::kitName(b))
                                                     .arg(differ)));
            }
    }

    void theLinnSnareIsGated() {
        // Its room fills the first tenth of a second and more, then a gate
        // shuts it: the Linn's snare is louder than a house snare there,
        // and a few hundredths of a second after the gate it is all but
        // gone, where the house snare only fades.
        Ctx ctx;
        const Dry linn = strike(drums::Snare, drums::Linn, 1.0f, 0.5, ctx);
        const Dry house = strike(drums::Snare, drums::House, 1.0f, 0.5, ctx);
        const double held = dryRms(linn, 0.1, 0.16), after = dryRms(linn, 0.2, 0.26);
        QVERIFY2(held > 1.4 * dryRms(house, 0.1, 0.16), qPrintable(QString::number(held)));
        QVERIFY2(after < 0.15 * held, qPrintable(QString::number(after / held)));
        QVERIFY(dryRms(house, 0.2, 0.26) > 0.15 * dryRms(house, 0.1, 0.16));
        // The gate shuts on the snare's own tail too: a chop to silence,
        // 50 dB down within a tenth of a second.
        const double gone = dryRms(linn, 0.27, 0.5);
        QVERIFY2(gone < 0.003 * held, qPrintable(QString::number(20 * std::log10(gone / held))));
    }

    void theBrushSlapsAndSwishes() {
        // The slap speaks at once; the swish swells for most of a tenth of
        // a second; both are darker than a house snare.
        Ctx ctx;
        const Dry slap = strike(drums::Snare, drums::Brushes, 1.0f, 0.5, ctx);
        const Dry swish = strike(drums::Clap, drums::Brushes, 1.0f, 0.8, ctx);
        QVERIFY2(peakTime(slap) < 0.03, qPrintable(QString::number(peakTime(slap))));
        QVERIFY2(peakTime(swish) > 0.05, qPrintable(QString::number(peakTime(swish))));
        QVERIFY(rmsHz(swish) < rmsHz(strike(drums::Snare, drums::House, 1.0f, 0.5, ctx)));
    }

    void theBoomBapKitIsDusty() {
        // Through the sampler's dark filter: every drum that has a house
        // twin is duller than it.
        for (int pad : {drums::Snare, drums::Hat, drums::Ride, drums::Shaker}) {
            Ctx ctx;
            const double dusty = rmsHz(strike(pad, drums::BoomBap, 1.0f, 0.6, ctx));
            const double clean = rmsHz(strike(pad, drums::House, 1.0f, 0.6, ctx));
            QVERIFY2(dusty < clean * 0.9, drums::padName(pad));
        }
    }

    void theNewTunedPercussionIsInKey() {
        // In A minor the vibes play the fifth and the clave the root, each
        // where the kit puts it; the Simmons falls all the way down onto
        // its root as it fades, never settling into a hum above it.
        Ctx ctx;
        const Dry vibes = strike(drums::Perc, drums::Brushes, 1.0f, 0.8, ctx);
        const double v = drums::degreeHz(ctx.h, 4, 520.0f);
        QVERIFY2(std::fabs(settledHz(vibes.l, 0.3, 0.7) / v - 1.0) < 0.02, qPrintable(QString::number(settledHz(vibes.l, 0.3, 0.7))));
        const Dry simmons = strike(drums::Zap, drums::Linn, 1.0f, 1.2, ctx);
        const double s = drums::degreeHz(ctx.h, 0, 150.0f);
        const double early = settledHz(simmons.l, 0.01, 0.08), mid = settledHz(simmons.l, 0.25, 0.45),
                     late = settledHz(simmons.l, 0.6, 1.0);
        QVERIFY2(early > 2.5 * s && mid > late * 1.08 && late > s * 0.99 && late < s * 1.3,
                 qPrintable(QStringLiteral("%1 %2 %3 / %4").arg(early).arg(mid).arg(late).arg(s)));
        const Dry clave = strike(drums::Zap, drums::Cr78, 1.0f, 0.2, ctx);
        const double c = drums::degreeHz(ctx.h, 0, 2100.0f);
        QVERIFY2(std::fabs(settledHz(clave.l, 0.005, 0.04) / c - 1.0) < 0.03, qPrintable(QString::number(settledHz(clave.l, 0.005, 0.04))));
    }

    void everyKitGroovesAsLoudAsHouse() {
        // Changing kit never jumps the mix: each kit's groove is within
        // 2 dB of house's, as an ear hears loudness.
        Ctx ctx;
        const double house = loudness(grooveTake(drums::House, ctx));
        QString all;
        for (int kit = 0; kit < drums::KitCount; ++kit)
            all += QStringLiteral(" %1 %2").arg(drums::kitName(kit)).arg(loudness(grooveTake(kit, ctx)) - house, 0, 'f', 1);
        for (int kit = 0; kit < drums::KitCount; ++kit) {
            const double d = loudness(grooveTake(kit, ctx)) - house;
            QVERIFY2(std::fabs(d) < 2.0, qPrintable(QStringLiteral("%1:%2").arg(drums::kitName(kit), all)));
        }
    }

    void theLowEndReachesALaptop() {
        // A laptop's speakers start near 150 Hz: above it, the CR-78's
        // kick and the brushes' upright carry nearly as much as house's
        // kick and 808.
        auto above = [](const Dry &d) {
            Svf a, b;
            a.set(150.0f, 0.7f, kRate);
            b.set(150.0f, 0.7f, kRate);
            double e = 0;
            for (size_t i = 0; i < d.l.size(); ++i) {
                const float y = b.step(a.step(0.5f * (d.l[i] + d.r[i])).high).high;
                e += double(y) * y;
            }
            return 10 * std::log10(e + 1e-20);
        };
        Ctx ctx;
        const double kick = above(strike(drums::Kick, drums::House, 1.0f, 0.6, ctx));
        const double cr78 = above(strike(drums::Kick, drums::Cr78, 1.0f, 0.6, ctx));
        QVERIFY2(cr78 > kick - 6.0, qPrintable(QString::number(cr78 - kick)));
        const double sub = above(strike(drums::Sub, drums::House, 1.0f, 0.6, ctx));
        const double upright = above(strike(drums::Sub, drums::Brushes, 1.0f, 0.6, ctx));
        QVERIFY2(upright > sub - 3.0, qPrintable(QString::number(upright - sub)));
    }

    void theUprightWalksTheChord() {
        // On the chord of the fourth (D minor in A minor) the upright's
        // root is D3, an octave up from where an 808 would sit; its fifth
        // is the chord's fifth, A3.
        Ctx ctx;
        ctx.h.chordRoot = 3;
        ctx.h.chordTones[0] = 3;
        ctx.h.chordTones[1] = 5;
        ctx.h.chordTones[2] = 7;
        ctx.h.chordCount = 3;
        auto played = [&](int degree, double expect) {
            drums::Engine e;
            e.init(kRate);
            drums::Strike s;
            s.pad = drums::Sub;
            s.kit = drums::Brushes;
            s.degrees = degree;
            s.hold = 0.4f;
            e.strike(s, ctx.h);
            Dry d;
            float l[kBlock], r[kBlock];
            for (int b = 0; b < int(0.4 * kRate) / kBlock; ++b) {
                std::fill(l, l + kBlock, 0.0f);
                std::fill(r, r + kBlock, 0.0f);
                e.render(l, r, kBlock, ctx);
                d.l.insert(d.l.end(), l, l + kBlock);
            }
            // The fundamental alone, below the harmonics.
            Svf lp, lp2;
            lp.set(float(expect * 1.2), 0.7f, kRate);
            lp2.set(float(expect * 1.2), 0.7f, kRate);
            for (float &x : d.l)
                x = lp2.step(lp.step(x).low).low;
            return settledHz(d.l, 0.1, 0.35);
        };
        const double root = played(0, 146.83), fifth = played(4, 220.0);
        QVERIFY2(std::fabs(root / 146.83 - 1.0) < 0.03, qPrintable(QString::number(root)));
        QVERIFY2(std::fabs(fifth / 220.0 - 1.0) < 0.03, qPrintable(QString::number(fifth)));
    }

    void theBrushesSwingMovesTheTripletEighth() {
        // Block and hat held together in the brushes kit: the block's
        // euclidean part, on the triplet grid, has an "a" that lands two
        // thirds through its beat at the kit's 66%, and on the straight
        // eighth once the swing is taken down to 50%.
        auto where = [](const std::string &extra) {
            // Brushes is the tenth kit: eight to the left of house.
            const auto take = testkit::play("page 0 1\nkey 0.1 Left\nkey 0.11 Left\nkey 0.12 Left\nkey 0.13 Left\n"
                                            "key 0.14 Left\nkey 0.15 Left\nkey 0.16 Left\nkey 0.17 Left\n" + extra +
                                            "key 1.0 P down\nkey 1.0 V down\nkey 6.0 P up\nkey 6.0 V up\nend 6.2\n");
            double sum = 0;
            int n = 0;
            for (double t : onsets(take, 2.5, 5.8, 0.05)) {
                const double f = t / kSpb - std::floor(t / kSpb);
                if (f > 0.42 && f < 0.85) {
                    sum += f;
                    ++n;
                }
            }
            return n ? sum / n : -1.0;
        };
        const double swung = where(""), straight = where("key 0.2 Shift+Left\nkey 0.25 Shift+Left\nkey 0.3 Shift+Left\n"
                                                          "key 0.35 Shift+Left\nkey 0.4 Shift+Left\nkey 0.45 Shift+Left\n"
                                                          "key 0.5 Shift+Left\nkey 0.55 Shift+Left\n");
        QVERIFY2(std::fabs(swung - 2.0 / 3.0) < 0.04, qPrintable(QString::number(swung)));
        QVERIFY2(std::fabs(straight - 0.5) < 0.04, qPrintable(QString::number(straight)));
    }

    void theNewKitsGrooveTheirOwnWay() {
        auto ticks = [](const drums::Pattern &p) {
            std::vector<int> t;
            for (int i = 0; i < p.count; ++i)
                t.push_back(p.steps[i].tick);
            return t;
        };
        // Brushes: the ride's spang-a-lang in triplets (each beat's last
        // triplet written on the eighth, for the swing to place), the hat's
        // foot on two and four, the bass walking the chord with an approach
        // into the next bar; the euclidean parts on the triplet grid too.
        const auto ride = drums::groovePattern(drums::Ride, drums::Brushes, 1, 0);
        QVERIFY(ride.triplet);
        QCOMPARE(ticks(ride), (std::vector<int>{0, 24, 36, 48, 72, 84}));
        QCOMPARE(ticks(drums::groovePattern(drums::Hat, drums::Brushes, 1, 0)), (std::vector<int>{24, 72}));
        const auto walk = drums::groovePattern(drums::Sub, drums::Brushes, 1, 0);
        QCOMPARE(walk.count, 4);
        for (int i = 0; i < 4; ++i)
            QCOMPARE(int(walk.steps[i].degree), (std::vector<int>{0, 2, 4, -1})[size_t(i)]);
        const auto vibes = drums::groovePattern(drums::Perc, drums::Brushes, 1, 0);
        QVERIFY(vibes.triplet);
        for (int i = 0; i < vibes.count; ++i)
            QVERIFY(vibes.steps[i].tick % 24 == 0 || vibes.steps[i].tick % 24 == 8 || vibes.steps[i].tick % 24 == 12);
        // The Linn's tambourine has a part of its own: the backbeat.
        QCOMPARE(ticks(drums::groovePattern(drums::Perc, drums::Linn, 0, 0)), (std::vector<int>{24, 72}));
        // The CR-78's rim plays the son clave.
        QCOMPARE(ticks(drums::groovePattern(drums::Rim, drums::Cr78, 1, 0)), (std::vector<int>{0, 18, 36, 60, 72}));
        // Jungle's busiest snare rushes into the bar in thirty-seconds.
        const auto rush = ticks(drums::groovePattern(drums::Snare, drums::Jungle, 2, 0));
        QCOMPARE((std::vector<int>(rush.end() - 3, rush.end())), (std::vector<int>{87, 90, 93}));
        // Every kit's swing is one the page can set.
        for (int kit = 0; kit < drums::KitCount; ++kit)
            QVERIFY(drums::kitSwing(kit) >= 0.5f && drums::kitSwing(kit) <= 0.7f);
    }

    // --- the kits from the world's music ----------------------------------------

    void theWorldKitsPlayInKey() {
        // In A minor, each kit's tuned instruments land on the notes the kit
        // puts them on, measured below their overtones once the strike has
        // passed: the log drum on A1 (after dropping from an octave up), the
        // tabla tarang's low drum on A3 (its na rings the harmonics, the
        // fundamental under them), the saron and the kenong on A4 (each
        // ombak twin only a few hertz off), the chip's blip on E5, the
        // marimba on E5, the sitar on A4, the krin on E4, the kora on A4
        // and the djembe's bass on E2 (the fifth, where A is too far from
        // where a bass lives).
        struct Case {
            int pad, kit;
            double hz, from, to;
        };
        const Case cases[] = {
            {drums::Sub, drums::Amapiano, 55.0, 0.12, 0.3},   {drums::TomLo, drums::Tabla, 220.0, 0.05, 0.3},
            {drums::TomLo, drums::Gamelan, 440.0, 0.05, 0.4}, {drums::Open, drums::Gamelan, 440.0, 0.1, 0.6},
            {drums::Perc, drums::Chip, 659.26, 0.02, 0.08},   {drums::Bell, drums::Amapiano, 659.26, 0.02, 0.3},
            {drums::Zap, drums::Tabla, 440.0, 0.1, 0.4},      {drums::Bell, drums::Djembe, 329.63, 0.01, 0.15},
            {drums::Zap, drums::Djembe, 440.0, 0.05, 0.3},    {drums::Kick, drums::Djembe, 82.41, 0.04, 0.2},
        };
        for (const Case &c : cases) {
            Ctx ctx;
            Dry d = strike(c.pad, c.kit, 1.0f, c.to + 0.05, ctx);
            Svf lp, lp2;
            lp.set(float(c.hz * 1.25), 0.7f, kRate);
            lp2.set(float(c.hz * 1.25), 0.7f, kRate);
            for (float &x : d.l)
                x = lp2.step(lp.step(x).low).low;
            const double got = settledHz(d.l, c.from, c.to);
            QVERIFY2(std::fabs(got / c.hz - 1.0) < 0.02,
                     qPrintable(QStringLiteral("%1 in %2: %3 Hz, not %4")
                                    .arg(drums::padName(c.pad, c.kit), drums::kitName(c.kit))
                                    .arg(got)
                                    .arg(c.hz)));
        }
        // The bayan's ge rises onto its note as the heel presses, where a
        // kick falls onto it; struck further up the pad, it rises further.
        Ctx ctx;
        Dry ge = strike(drums::Kick, drums::Tabla, 1.0f, 0.4, ctx);
        const double early = settledHz(ge.l, 0.01, 0.06), late = settledHz(ge.l, 0.2, 0.35);
        QVERIFY2(early < late * 0.95, qPrintable(QStringLiteral("%1 then %2").arg(early).arg(late)));
        drums::Engine e;
        e.init(kRate);
        drums::Strike far;
        far.pad = drums::Kick;
        far.kit = drums::Tabla;
        far.sy = 0.95f;
        e.strike(far, ctx.h);
        Dry high;
        float l[kBlock], r[kBlock];
        for (int b = 0; b < int(0.4 * kRate) / kBlock; ++b) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
            high.l.insert(high.l.end(), l, l + kBlock);
        }
        const double rise = std::log2(settledHz(high.l, 0.2, 0.35) / late) * 12.0;
        QVERIFY2(rise > 1.0, qPrintable(QStringLiteral("%1 semitones further").arg(rise)));
    }

    void theGamelanPlaysInItsOwnTuning() {
        // The saron's middle bar in A minor is pelog's third note, 270 cents
        // over A (514 Hz), and in A major slendro's, 474 cents over it (579
        // Hz): neither is a note of the piano.
        for (const bool major : {false, true}) {
            Ctx ctx;
            ctx.h.tuning.set(9, 0, major ? 1 : 4, false);
            QCOMPARE(drums::gamelanPelog(ctx.h.tuning), !major);
            Dry d = strike(drums::TomMid, drums::Gamelan, 1.0f, 0.45, ctx);
            Svf lp, lp2;
            lp.set(700.0f, 0.7f, kRate);
            lp2.set(700.0f, 0.7f, kRate);
            for (float &x : d.l)
                x = lp2.step(lp.step(x).low).low;
            const double want = 440.0 * std::exp2((major ? 474.0 : 270.0) / 1200.0);
            const double got = settledHz(d.l, 0.05, 0.4);
            QVERIFY2(std::fabs(got / want - 1.0) < 0.01, qPrintable(QStringLiteral("%1 Hz, not %2").arg(got).arg(want)));
            const double semis = 12.0 * std::log2(got / 440.0);
            QVERIFY2(std::fabs(semis - std::round(semis)) > 0.2, qPrintable(QStringLiteral("%1 semitones").arg(semis)));
        }
        // The great gong sits on the root near 50 Hz (A1), and rings: ten
        // seconds on, it is still there.
        {
            Ctx ctx;
            Dry d = strike(drums::Crash, drums::Gamelan, 1.0f, 11.0, ctx);
            Svf lp[4];
            for (Svf &f : lp)
                f.set(62.0f, 0.6f, kRate);
            std::vector<float> low = d.l;
            for (float &x : low)
                for (Svf &f : lp)
                    x = f.step(x).low;
            const double hz = settledHz(low, 1.0, 4.0);
            QVERIFY2(std::fabs(hz / 55.0 - 1.0) < 0.03, qPrintable(QStringLiteral("%1 Hz").arg(hz)));
            float tail = 0;
            for (size_t i = size_t(10.0 * kRate); i < d.l.size(); ++i)
                tail = std::max(tail, std::fabs(d.l[i]));
            QVERIFY2(tail > d.peak() * 0.005f, qPrintable(QStringLiteral("%1 of %2").arg(tail).arg(d.peak())));
        }
        // Octaves are counted in the gamelan's own notes, negative ones too.
        Ctx ctx;
        QVERIFY(std::fabs(drums::gamelanHz(ctx.h, 7, 440.0f) / drums::gamelanHz(ctx.h, 0, 440.0f) -
                          std::exp2(1212.0 / 1200.0)) < 1e-3);
        QVERIFY(std::fabs(drums::gamelanHz(ctx.h, -1, 440.0f) / drums::gamelanHz(ctx.h, 6, 440.0f) -
                          std::exp2(-1212.0 / 1200.0)) < 1e-3);
    }

    void theWorldKitsSustainWhatIsHeld() {
        // A roll on the angklung keeps the one sounding shaking, and a roll
        // on the tanpura its strings plucked, rather than starting more:
        // after two seconds of steps, still one voice, still sounding.
        for (const int pad : {drums::Zap, drums::Crash}) {
            const int kit = pad == drums::Zap ? drums::Gamelan : drums::Tabla;
            Ctx ctx;
            drums::Engine e;
            e.init(kRate);
            Command k;
            k.type = Command::Param;
            k.i[0] = drums::ParamKit;
            k.i[1] = kit;
            e.command(k, ctx);
            float l[kBlock], r[kBlock];
            double late = 0;
            int most = 0;
            const int blocks = int(4.0 * kRate) / kBlock;
            for (int b = 0; b < blocks; ++b) {
                const double t = double(b * kBlock) / kRate;
                if (b % int(0.25 * kRate / kBlock) == 0 && t < 2.0) {
                    Command c;
                    c.type = drums::Hit;
                    c.i[0] = pad;
                    c.f[0] = 0.8f;
                    c.f[1] = c.f[2] = 0.5f;
                    e.command(c, ctx);
                }
                std::fill(l, l + kBlock, 0.0f);
                std::fill(r, r + kBlock, 0.0f);
                e.render(l, r, kBlock, ctx);
                most = std::max(most, e.voices());
                if (t > 2.2 && t < 2.5)
                    for (float x : l)
                        late = std::max(late, double(std::fabs(x)));
            }
            QVERIFY2(most == 1, qPrintable(QStringLiteral("%1: %2 voices").arg(drums::padName(pad, kit)).arg(most)));
            QVERIFY2(late > 0.005, qPrintable(QStringLiteral("%1: %2").arg(drums::padName(pad, kit)).arg(late)));
        }
        // A single tanpura pluck cycle drones on for seconds.
        Ctx ctx;
        const Dry d = strike(drums::Crash, drums::Tabla, 0.8f, 7.0, ctx);
        float tail = 0;
        for (size_t i = size_t(6.0 * kRate); i < d.l.size(); ++i)
            tail = std::max(tail, std::fabs(d.l[i]));
        QVERIFY2(tail > d.peak() * 0.03f, qPrintable(QStringLiteral("%1 of %2").arg(tail).arg(d.peak())));
    }

    void theCengCengIsWide() {
        // Two players, one in each ear: what differs between the ears is
        // as loud as what they share.
        Ctx ctx;
        const Dry d = strike(drums::Clap, drums::Gamelan, 1.0f, 0.5, ctx);
        double side = 0, mid = 0;
        for (size_t i = 0; i < d.l.size(); ++i) {
            side += double(d.l[i] - d.r[i]) * double(d.l[i] - d.r[i]);
            mid += double(d.l[i] + d.r[i]) * double(d.l[i] + d.r[i]);
        }
        QVERIFY2(side > 0.4 * mid, qPrintable(QStringLiteral("side/mid %1").arg(side / mid)));
    }

    void theLogDrumSlidesFromNoteToNote() {
        // A note while the last still sounds slides into it: between the
        // two, the log drum passes through the pitches in between.
        Ctx ctx;
        drums::Engine e;
        e.init(kRate);
        drums::Strike s;
        s.pad = drums::Sub;
        s.kit = drums::Amapiano;
        s.hold = 0.6f;
        e.strike(s, ctx.h);
        Dry d;
        float l[kBlock], r[kBlock];
        for (int b = 0; b < int(0.7 * kRate) / kBlock; ++b) {
            if (b == int(0.25 * kRate) / kBlock) {
                s.degrees = 4;
                e.strike(s, ctx.h);
            }
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
            d.l.insert(d.l.end(), l, l + kBlock);
        }
        Svf lp, lp2;
        lp.set(100.0f, 0.7f, kRate);
        lp2.set(100.0f, 0.7f, kRate);
        for (float &x : d.l)
            x = lp2.step(lp.step(x).low).low;
        const double before = settledHz(d.l, 0.12, 0.24), during = settledHz(d.l, 0.26, 0.3),
                     after = settledHz(d.l, 0.5, 0.68);
        QVERIFY2(std::fabs(before / 55.0 - 1.0) < 0.02 && std::fabs(after / 82.41 - 1.0) < 0.02,
                 qPrintable(QStringLiteral("%1 then %2").arg(before).arg(after)));
        QVERIFY2(during > before * 1.03 && during < after * 0.97,
                 qPrintable(QStringLiteral("%1, %2, %3").arg(before).arg(during).arg(after)));
    }

    void theChipsNoiseClocksAsTheChipDoes() {
        // The noise channel's clock: 524288 Hz over the divisor (half for a
        // divisor of 0), over two to the shift plus one.
        QCOMPARE(drums::ChipVoice::noiseClock(0, 0), 524288.0f);
        QCOMPARE(drums::ChipVoice::noiseClock(1, 0), 262144.0f);
        QCOMPARE(drums::ChipVoice::noiseClock(4, 6), 1024.0f);
        // At a low clock the noise holds each bit for many samples: it
        // steps.
        drums::ChipVoice v;
        v.reset(kRate);
        v.noiseVol = 1;
        v.setNoise(7, 9);
        int changes = 0;
        float last = 0;
        for (int i = 0; i < int(kRate); ++i) {
            const float x = v.step();
            if (i > 100 && std::fabs(x - last) > 0.05f)
                ++changes;
            last = x;
        }
        QVERIFY2(changes < 200, qPrintable(QString::number(changes)));
    }

    void theWorldKitsStayUnderFullScale() {
        // Each of the five, a groove of four held and slid up to busier,
        // then every pad struck hard, played through the whole studio:
        // nothing reaches 0 dBFS and nothing goes wrong.
        for (int kit = drums::Amapiano; kit < drums::KitCount; ++kit) {
            std::string script = "page 0 1\n";
            for (int i = 0; i < kit; ++i)
                script += "key " + std::to_string(0.1 + 0.02 * i) + " Right\n";
            script += "down 1.00 1 " + at(drums::Kick) + " p=0.9\ndown 1.02 2 " + at(drums::Snare) +
                      " p=0.9\ndown 1.04 3 " + at(drums::Hat) + " p=0.9\ndown 1.06 4 " + at(drums::Sub) +
                      " p=0.9\nglide 1.3 1.5 1 0.125 0.25\nup 4.0 1\nup 4.0 2\nup 4.0 3\nup 4.0 4\n";
            for (int pad = 0; pad < drums::PadCount; ++pad)
                script += "tap " + std::to_string(4.2 + 0.12 * pad) + " " + at(pad) + " p=1.0 len=0.06\n";
            script += "end 8\n";
            const auto take = testkit::play(script);
            QVERIFY(take.error.empty());
            QCOMPARE(int(take.view.parts[0].data[34]), kit);
            QVERIFY2(take.finite(), drums::kitName(kit));
            QVERIFY2(take.peak() < 1.0, qPrintable(QStringLiteral("%1: %2").arg(drums::kitName(kit)).arg(take.peak())));
            QVERIFY2(take.rms(1.5, 4.0) > 0.01, drums::kitName(kit));
        }
    }

    void aSongKeepsAWorldKitByName() {
        // Each new kit is saved by its name and comes back as itself.
        for (int kit = drums::Amapiano; kit < drums::KitCount; ++kit) {
            std::string script = "page 0 1\n";
            for (int i = 0; i < kit; ++i)
                script += "key " + std::to_string(0.1 + 0.02 * i) + " Right\n";
            script += "end 1\n";
            bool checked = false;
            testkit::reopen(script, 0.5, [&](Session &, Session &opened, const QJsonObject &doc) {
                const QJsonObject saved =
                    doc[QStringLiteral("parts")].toArray()[0].toObject()[QStringLiteral("settings")].toObject();
                QCOMPARE(saved[QStringLiteral("kit")].toString(), QString::fromLatin1(drums::kitName(kit)));
                QCOMPARE(int(opened.view().parts[0].data[34]), kit);
                QVERIFY(opened.pageStatus(0).contains(QString::fromLatin1(drums::kitName(kit)), Qt::CaseInsensitive));
                // The status says what the kit is: the gamelan's tuning in
                // the song's A minor, pelog.
                QVERIFY2(opened.pageStatus(0).contains(QString::fromLatin1(drums::kitCharacter(kit, true)),
                                                       Qt::CaseInsensitive),
                         qPrintable(opened.pageStatus(0)));
                checked = true;
            });
            QVERIFY2(checked, drums::kitName(kit));
        }
    }

    void theWorldKitsGrooveTheirOwnWay() {
        auto ticks = [](const drums::Pattern &p) {
            std::vector<int> t;
            for (int i = 0; i < p.count; ++i)
                t.push_back(p.steps[i].tick);
            return t;
        };
        // Amapiano's log drum plays a riff in three, three and two that
        // leaps as the log drum does: root, the octave, the fifth above it.
        const auto log = drums::groovePattern(drums::Sub, drums::Amapiano, 1, 0);
        QCOMPARE(ticks(log), (std::vector<int>{0, 18, 36, 48, 66, 84}));
        QCOMPARE(int(log.steps[0].degree), 0);
        QCOMPARE(int(log.steps[1].degree), 7);
        QCOMPARE(int(log.steps[2].degree), 11);
        // The gamelan's bonang and saron play a kotekan: two parts that
        // never strike together and together strike every sixteenth.
        for (int density = 0; density < 3; ++density) {
            const auto polos = ticks(drums::groovePattern(drums::Perc, drums::Gamelan, density, 0));
            const auto sangsih = ticks(drums::groovePattern(drums::TomMid, drums::Gamelan, density, 0));
            std::vector<int> both = polos;
            both.insert(both.end(), sangsih.begin(), sangsih.end());
            std::sort(both.begin(), both.end());
            QVERIFY2(std::adjacent_find(both.begin(), both.end()) == both.end(), qPrintable(QString::number(density)));
            QCOMPARE(int(both.size()), density == 0 ? 8 : 16);
        }
        // The djembe's shekere leaves room for the bells: no more than an
        // eighth note's worth of strokes in its groove.
        QVERIFY(drums::groovePattern(drums::Shaker, drums::Djembe, 1, 0).count <= 8);
        // And there is no backbeat in it: nothing on two and four from the
        // woodrim or the vox at its sparest.
        for (const int pad : {drums::Snare, drums::Clap})
            for (int t : ticks(drums::groovePattern(pad, drums::Amapiano, 0, 0)))
                QVERIFY2(t != 24 && t != 72, drums::padName(pad, drums::Amapiano));
        // The tabla plays kaharwa: ge on dha, ge and dhi, ka on its own.
        QCOMPARE(ticks(drums::groovePattern(drums::Kick, drums::Tabla, 1, 0)), (std::vector<int>{0, 12, 72}));
        QCOMPARE(ticks(drums::groovePattern(drums::Clap, drums::Tabla, 1, 0)), (std::vector<int>{60}));
        // The chiptune bass jumps between root and octave on the eighths.
        const auto chip = drums::groovePattern(drums::Sub, drums::Chip, 1, 0);
        QCOMPARE(chip.count, 8);
        QCOMPARE(int(chip.steps[1].degree), 7);
        // The kenkeni plays the offbeat eighths, the dununba answers itself.
        QCOMPARE(ticks(drums::groovePattern(drums::TomHi, drums::Djembe, 0, 0)), (std::vector<int>{12, 36, 60, 84}));
        QCOMPARE(ticks(drums::groovePattern(drums::TomLo, drums::Djembe, 0, 0)), (std::vector<int>{0, 48}));
        // Shifted, a riff keeps its notes and moves a sixteenth.
        const auto moved = drums::groovePattern(drums::Sub, drums::Gamelan, 1, 1);
        QCOMPARE(ticks(moved), (std::vector<int>{6, 54}));
        QCOMPARE(int(moved.steps[1].degree), 4);
    }

    void theWorldKitsAnswerTheCritic() {
        // Amapiano's kick leaves the bottom octave to the log drum: under
        // 80 Hz it carries far less than house's kick does.
        auto below = [](const Dry &d, float hz) {
            Svf a, b;
            a.set(hz, 0.7f, kRate);
            b.set(hz, 0.7f, kRate);
            double e = 0;
            for (size_t i = 0; i < d.l.size(); ++i) {
                const float y = b.step(a.step(0.5f * (d.l[i] + d.r[i])).low).low;
                e += double(y) * y;
            }
            return 10 * std::log10(e + 1e-20);
        };
        auto sideOverMid = [](const Dry &d) {
            double s = 0, m = 0;
            for (size_t i = 0; i < d.l.size(); ++i) {
                s += double(d.l[i] - d.r[i]) * (d.l[i] - d.r[i]);
                m += double(d.l[i] + d.r[i]) * (d.l[i] + d.r[i]);
            }
            return s / (m + 1e-20);
        };
        auto correlation = [](const Dry &a, const Dry &b) {
            double ab = 0, aa = 0, bb = 0;
            for (size_t i = 0; i < std::min(a.l.size(), b.l.size()); ++i) {
                ab += double(a.l[i]) * b.l[i];
                aa += double(a.l[i]) * a.l[i];
                bb += double(b.l[i]) * b.l[i];
            }
            return ab / std::sqrt(aa * bb + 1e-30);
        };
        Ctx ctx;
        {
            const double house = below(strike(drums::Kick, drums::House, 1.0f, 0.5, ctx), 80.0f);
            const double amapiano = below(strike(drums::Kick, drums::Amapiano, 1.0f, 0.5, ctx), 80.0f);
            QVERIFY2(amapiano < house - 10.0, qPrintable(QStringLiteral("%1 against %2").arg(amapiano).arg(house)));
        }
        // Its two shakers are one in each ear: far wider than house's.
        {
            const double house = sideOverMid(strike(drums::Shaker, drums::House, 1.0f, 0.3, ctx));
            const double amapiano = sideOverMid(strike(drums::Shaker, drums::Amapiano, 1.0f, 0.3, ctx));
            QVERIFY2(amapiano > 0.3 && amapiano > house * 4.0,
                     qPrintable(QStringLiteral("%1 against %2").arg(amapiano).arg(house)));
        }
        // The kecrek's loose plates never land twice alike, and neither
        // does a hand on the tabla's dayan: two strokes of the same pad,
        // one after the other, are two different sounds (a drum machine's
        // would be the same samples, correlating perfectly). The plates
        // differ by percents, the dayan's strokes by cents and touch.
        for (const auto &[pad, kit] : {std::pair{drums::Hat, drums::Gamelan}, std::pair{drums::Snare, drums::Tabla},
                                       std::pair{drums::Perc, drums::Tabla}}) {
            drums::Engine e;
            e.init(kRate);
            const Dry a = strike(pad, kit, 1.0f, 0.25, ctx, &e);
            const Dry b = strike(pad, kit, 1.0f, 0.25, ctx, &e);
            const double c = correlation(a, b);
            QVERIFY2(c < (pad == drums::Hat ? 0.9 : 0.97),
                     qPrintable(QStringLiteral("%1: %2").arg(drums::padName(pad, kit)).arg(c)));
        }
        // The gamelan's notes go by its own names: pelog's fourth is pat,
        // slendro's fourth (which skips 4) is 5, ma.
        QCOMPARE(QString::fromUtf8(drums::gamelanNoteName(3, true)), QStringLiteral("4 pat"));
        QCOMPARE(QString::fromUtf8(drums::gamelanNoteName(3, false)), QStringLiteral("5 ma"));
        QCOMPARE(QString::fromUtf8(drums::gamelanNoteName(-1, true)), QStringLiteral("7 pi"));
        QCOMPARE(drums::gamelanPadDegree(drums::TomHi), 4);
        QCOMPARE(drums::gamelanPadDegree(drums::Kick), -1);
    }

    void theWorldKitsAnswerTheCriticAgain() {
        auto band = [](const Dry &d, float hz, bool above, bool side) {
            Svf a, b;
            a.set(hz, 0.7f, kRate);
            b.set(hz, 0.7f, kRate);
            double e = 0;
            for (size_t i = 0; i < d.l.size(); ++i) {
                const float x = 0.5f * (side ? d.l[i] - d.r[i] : d.l[i] + d.r[i]);
                const float y = above ? b.step(a.step(x).high).high : b.step(a.step(x).low).low;
                e += double(y) * y;
            }
            return 10 * std::log10(e + 1e-20);
        };
        auto correlation = [](const Dry &a, const Dry &b) {
            double ab = 0, aa = 0, bb = 0;
            for (size_t i = 0; i < std::min(a.l.size(), b.l.size()); ++i) {
                ab += double(a.l[i]) * b.l[i];
                aa += double(a.l[i]) * a.l[i];
                bb += double(b.l[i]) * b.l[i];
            }
            return ab / std::sqrt(aa * bb + 1e-30);
        };
        Ctx ctx;
        // The log drum is the loudest low end in an amapiano record: its
        // groove carries as much under 80 Hz as house's kick and 808 do.
        {
            const double house = band(grooveTake(drums::House, ctx), 80.0f, false, false);
            const double amapiano = band(grooveTake(drums::Amapiano, ctx), 80.0f, false, false);
            QVERIFY2(std::fabs(amapiano - house) < 1.0, qPrintable(QStringLiteral("%1 against %2").arg(amapiano).arg(house)));
        }
        // The djembe's slap cracks: a good share of it is above 6 kHz,
        // where its tone has next to nothing.
        {
            const Dry slap = strike(drums::Snare, drums::Djembe, 1.0f, 0.3, ctx);
            const Dry tone = strike(drums::Clap, drums::Djembe, 1.0f, 0.3, ctx);
            const double crack = band(slap, 6000.0f, true, false) - band(slap, 20.0f, true, false);
            const double round = band(tone, 6000.0f, true, false) - band(tone, 20.0f, true, false);
            QVERIFY2(crack > -16.0 && crack > round + 12.0, qPrintable(QStringLiteral("%1, the tone %2").arg(crack).arg(round)));
        }
        // No hand or stick strikes twice alike: two strokes of the slap,
        // the bass, a dunun or either iron bell, one after the other, are
        // two different sounds. A few cents hardly move a low drum's phase
        // in a quarter of a second, so the bass and the dunun differ least;
        // the slap's crack and the bells' contact differ most.
        for (const int pad : {drums::Snare, drums::Kick, drums::TomMid, drums::Rim, drums::Ride}) {
            drums::Engine e;
            e.init(kRate);
            const Dry a = strike(pad, drums::Djembe, 1.0f, 0.25, ctx, &e);
            const Dry b = strike(pad, drums::Djembe, 1.0f, 0.25, ctx, &e);
            const double c = correlation(a, b);
            const bool low = pad == drums::Kick || pad == drums::TomMid;
            QVERIFY2(c < (low ? 0.99 : 0.9), qPrintable(QStringLiteral("%1: %2").arg(drums::padName(pad, drums::Djembe)).arg(c)));
        }
        // The ensemble stands spread out: above 3 kHz its groove is far
        // wider than house's, no more than 10 dB of side under its middle.
        {
            const Dry djembe = grooveTake(drums::Djembe, ctx);
            const double width = band(djembe, 3000.0f, true, true) - band(djembe, 3000.0f, true, false);
            QVERIFY2(width > -10.0, qPrintable(QString::number(width)));
        }
        // Busy, the slap pad is the lead djembe's call: flams (two strokes
        // a tick apart) and triplets of slaps (four ticks apart).
        {
            const drums::Pattern lead = drums::groovePattern(drums::Snare, drums::Djembe, 2, 0);
            int flams = 0, triplets = 0;
            for (int i = 1; i < lead.count; ++i) {
                flams += lead.steps[i].tick - lead.steps[i - 1].tick == 1;
                triplets += lead.steps[i].tick - lead.steps[i - 1].tick == 4;
            }
            QVERIFY2(flams >= 1 && triplets >= 4, qPrintable(QStringLiteral("%1 flams, %2").arg(flams).arg(triplets)));
        }
        // The gamelan's kotekan can be found without the help: grooving
        // the bonang without the saron, the status line asks for the saron,
        // and with both it names the kotekan.
        std::string gamelan = "page 0 1\n";
        for (int i = 0; i < drums::Gamelan; ++i)
            gamelan += "key " + std::to_string(0.1 + 0.02 * i) + " Right\n";
        auto statusOf = [&](const std::string &fingers) {
            QString status;
            testkit::play(gamelan + fingers + "end 2.5\n",
                          [&](Session &s) { status = s.pageStatus(0); });
            return status;
        };
        const QString half = statusOf("down 1.0 1 " + at(drums::Kick) + "\ndown 1.01 2 " + at(drums::Perc) + "\n");
        QVERIFY2(half.contains(QStringLiteral("SARON")) && half.contains(QStringLiteral("KOTEKAN")), qPrintable(half));
        const QString both = statusOf("down 1.0 1 " + at(drums::Kick) + "\ndown 1.01 2 " + at(drums::Perc) +
                                      "\ndown 1.02 3 " + at(drums::TomMid) + "\n");
        QVERIFY2(both.contains(QStringLiteral("KOTEKAN: bonang and saron")), qPrintable(both));
    }

    void theChipHasTheGameBoysOutputStage() {
        // The DMG's output capacitor, at 0.999958 a cycle of its 4 MHz
        // clock: about 0.9963 a sample at 48 kHz, a high-pass near 30 Hz.
        QVERIFY(std::fabs(drums::ChipVoice::chargeFactor(48000.0f) - 0.99634f) < 0.0001f);
        // So a held, low pulse droops between its edges: the bass's high
        // half starts well above where it ends.
        Ctx ctx;
        drums::Sub808 b;
        drums::Edit e;
        b.start(1, 0, 1.0f, 0, drums::Chip, e, 0, float(kRate));
        std::vector<float> x;
        float l[kBlock], r[kBlock];
        for (int i = 0; i < int(0.5 * kRate) / kBlock; ++i) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            b.render(l, r, kBlock, ctx.h);
            x.insert(x.end(), l, l + kBlock);
        }
        // Find a rising edge after the attack, then compare the half
        // cycle's start with its end.
        size_t at = size_t(0.3 * kRate);
        while (at + 1 < x.size() && !(x[at] <= 0.0f && x[at + 1] > 0.0f))
            ++at;
        float top = 0;
        size_t peakAt = at;
        for (size_t i = at; i < at + 120; ++i)
            if (x[i] > top)
                top = x[i], peakAt = i;
        size_t end = peakAt;
        while (end + 1 < x.size() && x[end + 1] > x[end] * 0.3f && x[end + 1] > 0.0f)
            ++end;
        QVERIFY2(x[end] < top * 0.75f, qPrintable(QStringLiteral("%1 then %2").arg(top).arg(x[end])));
        // Every chip drum still ends with nothing left over, and the hat is
        // the noise channel's metallic seven-bit mode.
        for (int pad = 0; pad < drums::PadCount; ++pad) {
            if (pad == drums::Sub)
                continue;
            const Dry d = strike(pad, drums::Chip, 1.0f, 1.0, ctx);
            double mean = 0;
            for (float v : d.l)
                mean += v;
            QVERIFY2(std::fabs(mean / double(d.l.size())) < 0.002, drums::padName(pad, drums::Chip));
        }
        drums::Voice v;
        drums::Setup su{float(kRate), drums::Chip, &ctx.h};
        drums::Strike s;
        s.pad = drums::Hat;
        s.kit = drums::Chip;
        v.start(s, su, float(kRate), 0, 1);
        QVERIFY(v.chip.shortNoise);
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

    // --- songs -----------------------------------------------------------------

    void aSongKeepsTheDrumsSettings() {
        // Trap, swing up two steps, held keys a step faster (↑ while a key
        // rolls: alone, ↑ is the loop's length), the kick tuned,
        // and a groove kept as a loop with its clap taken out.
        const std::string script = "page 0 1\nkey 0.1 Right\nkey 0.15 Right\n"
                                   "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n"
                                   "down 1.0 1 " + at(drums::Kick) + "\nclick 1.05 len=0.6\n"
                                   "glide 1.2 1.5 1 0.275 0.125\nup 1.8 1\n"
                                   "down 2.00 2 " + at(drums::Kick) + "\ndown 2.02 3 " + at(drums::Clap) +
                                   "\ndown 2.04 4 " + at(drums::Hat) +
                                   "\nclick 3.0 len=0.15\nup 3.4 2\nup 3.4 3\nup 3.4 4\n"
                                   "key 4.0 V down\nkey 4.5 Up\nkey 4.6 V up\n"
                                   "down 8.0 5 " + at(drums::Clap) + "\nkey 8.3 Delete\nup 8.6 5\nend 9\n";
        bool checked = false;
        testkit::reopen(script, 3.0, [&](Session &before, Session &opened, const QJsonObject &doc) {
            const QJsonObject saved = doc[QStringLiteral("parts")].toArray()[0].toObject()[QStringLiteral("settings")].toObject();
            QCOMPARE(saved[QStringLiteral("kit")].toString(), QStringLiteral("trap"));
            QCOMPARE(saved[QStringLiteral("swing")].toDouble(), 0.54);
            QCOMPARE(saved[QStringLiteral("keyRate")].toInt(), 1);
            QVERIFY(saved[QStringLiteral("sounds")].toObject().contains(QStringLiteral("kick")));
            const QJsonArray muted = saved[QStringLiteral("muted")].toArray();
            QCOMPARE(muted.size(), 1);
            QCOMPARE(muted[0].toObject()[QStringLiteral("slot")].toInt(), 0);
            QCOMPARE(muted[0].toObject()[QStringLiteral("pads")].toArray(), QJsonArray{QStringLiteral("clap")});
            // What the opened song saves is what the file said.
            const QJsonObject again = opened.saveSong()[QStringLiteral("parts")].toArray()[0].toObject()[QStringLiteral("settings")].toObject();
            QCOMPARE(again, saved);
            // The engine has the kit and the kick's shaping, and the loop
            // plays on without its clap.
            const PartView &a = before.view().parts[0], &b = opened.view().parts[0];
            QCOMPARE(int(b.data[34]), int(drums::Trap));
            QVERIFY(b.data[40 + 3 * drums::Kick] != 0.0f);
            QCOMPARE(b.data[40 + 3 * drums::Kick], a.data[40 + 3 * drums::Kick]);
            QCOMPARE(b.playing, 0);
            const QString status = opened.pageStatus(0);
            QVERIFY2(status.contains(QStringLiteral("CLAP out of loop 1")), qPrintable(status));
            QVERIFY2(status.contains(QStringLiteral("trap kit, swing 54%")), qPrintable(status));
            checked = true;
        });
        QVERIFY(checked);
    }

    void aSongFileOfNonsenseLeavesTheDrumsSane() {
        // A loop with a drum out of it, then the same song opened over it
        // with settings no tenfold wrote: every value is checked or clamped,
        // and the old song's mute doesn't linger.
        const std::string script = "page 0 1\nkey 0.1 Right\n"
                                   "down 1.00 1 " + at(drums::Kick) + "\ndown 1.02 2 " + at(drums::Clap) +
                                   "\ndown 1.04 3 " + at(drums::Hat) +
                                   "\nclick 2.0 len=0.15\nup 2.4 1\nup 2.4 2\nup 2.4 3\n"
                                   "down 6.0 4 " + at(drums::Clap) + "\nkey 6.3 Delete\nup 6.6 4\nend 7\n";
        bool checked = false;
        testkit::reopen(script, 1.0, [&](Session &, Session &opened, const QJsonObject &doc) {
            const QJsonObject nonsense = QJsonDocument::fromJson(R"({
                "kit": 7, "swing": 1e300, "keyRate": "fast", "extra": true,
                "sounds": {"kick": {"tune": 1e9, "decay": -1e9, "tone": "loud"}, "snare": [1, 2],
                           "rim": {"tune": 0.5}, "nope": {"tune": 3}},
                "muted": [{"slot": 99, "pads": ["kick"]}, {"slot": 3, "pads": ["kick"]},
                          {"slot": 0, "pads": [3, "nope", "hat"]}, "x", {"slot": 0.5, "pads": ["snare"]}]
            })").object();
            QVERIFY(!nonsense.isEmpty());
            QJsonObject edited = doc;
            QJsonArray parts = edited[QStringLiteral("parts")].toArray();
            QJsonObject drums = parts[0].toObject();
            drums[QStringLiteral("settings")] = nonsense;
            parts[0] = drums;
            edited[QStringLiteral("parts")] = parts;
            QString error;
            QVERIFY2(opened.openSong(edited, &error), qPrintable(error));
            float buf[384 * 2];
            for (int i = 0; i < 4; ++i)
                opened.studio()->render(buf, 384);
            opened.refresh();
            const QJsonObject got = opened.saveSong()[QStringLiteral("parts")].toArray()[0].toObject()[QStringLiteral("settings")].toObject();
            QCOMPARE(got[QStringLiteral("kit")].toString(), QStringLiteral("house"));
            QCOMPARE(got[QStringLiteral("swing")].toDouble(), 0.7);
            QCOMPARE(got[QStringLiteral("keyRate")].toInt(), 0);
            const QJsonObject sounds = got[QStringLiteral("sounds")].toObject();
            QCOMPARE(sounds.keys(), (QStringList{QStringLiteral("kick"), QStringLiteral("rim")}));
            const QJsonObject kick = sounds[QStringLiteral("kick")].toObject();
            QCOMPARE(kick[QStringLiteral("tune")].toDouble(), 7.0);
            QCOMPARE(kick[QStringLiteral("decay")].toDouble(), -2.0);
            QCOMPARE(kick[QStringLiteral("tone")].toDouble(), 0.0);
            // Only the hat is out, and only of the loop the song has.
            const QJsonArray muted = got[QStringLiteral("muted")].toArray();
            QCOMPARE(muted.size(), 1);
            QCOMPARE(muted[0].toObject()[QStringLiteral("slot")].toInt(), 0);
            QCOMPARE(muted[0].toObject()[QStringLiteral("pads")].toArray(), QJsonArray{QStringLiteral("hat")});
            QCOMPARE(int(opened.view().parts[0].data[34]), int(drums::House));
            QCOMPARE(opened.view().parts[0].data[40 + 3 * drums::Kick], 7.0f);
            checked = true;
        });
        QVERIFY(checked);
    }

    void anyDegreeAClipSaysIsSafe() {
        // A song file's clip may hold any int: the engine keeps the scale's
        // arithmetic in range, and the sound finite.
        Ctx ctx;
        drums::Engine e;
        e.init(kRate);
        int id = 1;
        for (int degree : {INT_MIN, INT_MAX, -100000, 100000})
            for (int pad : {int(drums::Kick), int(drums::TomHi), int(drums::Bell), int(drums::Sub)}) {
                Command c;
                c.type = pad == drums::Sub ? uint16_t(Command::On) : drums::Hit;
                c.id = id++;
                c.i[0] = pad;
                c.i[1] = degree;
                c.i[2] = INT_MAX;
                c.f[0] = 0.8f;
                e.command(c, ctx);
                if (pad == drums::Sub) {
                    Command m;
                    m.type = Command::Move;
                    m.id = c.id;
                    m.i[1] = degree == INT_MIN ? INT_MAX : -degree;
                    e.command(m, ctx);
                }
            }
        float l[kBlock], r[kBlock];
        for (int b = 0; b < 200; ++b) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            e.render(l, r, kBlock, ctx);
            for (int i = 0; i < kBlock; ++i)
                QVERIFY(std::isfinite(l[i]) && std::isfinite(r[i]));
        }
    }
};

TESTKIT_REGISTER(DrumsTests)
#include "drums_tests.moc"
