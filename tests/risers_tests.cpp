// The FX page: risers that peak on the bar line, drops that fire on the
// sample, and a mix that is always handed back.

#include "testkit.h"

#include <QJsonArray>

#include "../src/risers.h"
#include "parts.h"

using namespace tf;

namespace {

constexpr int kFx = 6; // the FX page's part
constexpr double kBpm = 124.0;
double timeOf(double beat) { return beat * 60.0 / kBpm; }

// Where the five rows of the build field are (1, 2, 4, 8, 16 bars) and the
// columns (noise, pitch, saw, roll, swell), in pad terms.
float rowY(int row) { return 0.2f + 0.16f * (float(row) + 0.5f); }
float columnX(int kind) { return (float(kind) + 0.5f) / 5.0f; }
float hitX(int hit) { return (float(hit) + 0.5f) / 8.0f; }

std::string fmt(const char *f, double a, double b = 0, double c = 0, double d = 0, double e = 0) {
    char buf[512];
    std::snprintf(buf, sizeof buf, f, a, b, c, d, e);
    return buf;
}

struct VoiceInfo {
    int kind;
    double start, end;
    int state;
    float note;
};

std::vector<VoiceInfo> voicesOf(const StudioView &v) {
    std::vector<VoiceInfo> out;
    const float *d = v.parts[kFx].data;
    for (int k = 0; k < risers::pub::MaxVoices; ++k) {
        const float *o = d + risers::pub::Voices + risers::pub::Stride * k;
        if (o[risers::pub::VKind] < 0)
            continue;
        out.push_back({int(o[risers::pub::VKind]), o[risers::pub::VStart], o[risers::pub::VEnd],
                       int(o[risers::pub::VState]), o[risers::pub::VNote]});
    }
    return out;
}

double rmsDb(const testkit::Take &take, double from, double to) { return 20 * std::log10(take.rms(from, to) + 1e-9); }

// Kick, snare, kick, snare half a second apart on the drums page (its near
// row of pads: kick, snare, clap, hat), then Space where the next kick would
// land: a one-bar loop at 120 BPM.
const char *kBeat = "page 0 1\n"
                    "tap 0.5 0.12 0.12 p=0.7\ntap 1.0 0.38 0.12 p=0.6\ntap 1.5 0.12 0.12 p=0.7\ntap 2.0 0.38 0.12 p=0.6\n"
                    "key 2.5 Space\n";

// The peak of the mix below 150 Hz between two times: the kick's weight.
double lowPeakDb(const testkit::Take &take, double from, double to) {
    Svf a, b;
    a.set(150, 0.7f, 48000);
    b.set(150, 0.7f, 48000);
    double peak = 0;
    const size_t start = size_t(std::max(0.0, from - 0.05) * 48000), end = size_t(to * 48000);
    for (size_t i = start; i < end && i < take.left.size(); ++i) {
        const float y = b.step(a.step(0.5f * (take.left[i] + take.right[i])).low).low;
        if (i >= size_t(from * 48000))
            peak = std::max(peak, double(std::fabs(y)));
    }
    return 20 * std::log10(peak + 1e-9);
}

// The touch-clock time of a beat, from a view.
double timeOfBeat(const StudioView &v, double beat) { return v.time + (beat - v.beat) * 60.0 / v.bpm; }

}

// The FX engine on its own at 124 BPM, for what the page's commands do.
struct FxAudio : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return double(tr.sample) / double(tr.rate); }
    int offsetOf(double t, int n) const override {
        const int o = int((t - now()) * double(tr.rate));
        return o < 0 ? 0 : std::min(o, n - 1);
    }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

class RisersTests : public QObject {
    Q_OBJECT
private slots:
    void aLoopsFallDropsOnlyItsOwnLayersRisers() {
        // A riser from the hand and one replayed by a loop's second layer;
        // the loop's "every riser falls" drops its own, not the hand's. The
        // hand's falls everything.
        PartSpec spec = makeRisers();
        auto &e = *spec.engine;
        FxAudio ctx;
        ctx.tr.rate = 48000;
        ctx.tr.bpm = 124;
        ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
        e.init(48000);
        auto run = [&](double seconds) {
            for (int b = 0; b < int(seconds * 48000 / kBlock); ++b) {
                ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
                float l[kBlock] = {}, r[kBlock] = {};
                e.render(l, r, kBlock, ctx);
                ctx.tr.sample += kBlock;
            }
        };
        auto riser = [&](int id, int kind) {
            Command c;
            c.type = Command::On;
            c.id = id;
            c.i[0] = kind;
            c.i[1] = 2;
            c.f[0] = 0.6f;
            c.f[2] = 0.7f;
            c.f[5] = 8;
            e.command(c, ctx);
        };
        const int layer1 = (1 << 30) | (1 << 27);
        riser(5, risers::Noise);
        riser(layer1 | 7, risers::Pitch);
        run(0.3);
        Command fall;
        fall.type = risers::CmdFall;
        fall.id = layer1 | risers::kEveryone;
        e.command(fall, ctx);
        run(0.1);
        float data[kPartData] = {};
        e.publish(data);
        StudioView v;
        std::copy(std::begin(data), std::end(data), v.parts[kFx].data);
        int hand = -1, loop = -1;
        for (const auto &vi : voicesOf(v)) {
            if (vi.kind == risers::Noise)
                hand = vi.state;
            if (vi.kind == risers::Pitch)
                loop = vi.state;
        }
        QCOMPARE(hand, int(risers::Running));
        QVERIFY(loop >= int(risers::Falling));
        fall.id = risers::kEveryone;
        e.command(fall, ctx);
        run(0.1);
        e.publish(data);
        std::copy(std::begin(data), std::end(data), v.parts[kFx].data);
        for (const auto &vi : voicesOf(v))
            if (vi.kind == risers::Noise)
                QVERIFY(vi.state >= int(risers::Falling));
    }

    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 7\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    // A build lands on the phrase grid: from beat 1, a two-bar noise riser
    // aims at the 8-bar line (beat 32), always forward, and waits to come in
    // two bars before it. It grows into the line, the drop fires on it, and
    // the riser is gone after.
    void aRiserPeaksOnThePhraseLine() {
        const double drop = 32.0, tDrop = timeOf(drop);
        const std::string land = fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\n", columnX(0), rowY(1));
        std::vector<VoiceInfo> during;
        testkit::play(land + "end 3\n", [&](Session &s) { during = voicesOf(s.view()); });
        QCOMPARE(int(during.size()), 1);
        QCOMPARE(during[0].kind, int(risers::Noise));
        QCOMPARE(during[0].end, drop);
        QCOMPARE(during[0].start, drop - 8);
        QCOMPARE(during[0].state, int(risers::Waiting));

        StudioView after;
        const auto take = testkit::play(land + fmt("end %.3f\n", tDrop + 0.4), [&](Session &s) { after = s.view(); });
        QVERIFY(take.finite());
        const double last = rmsDb(take, tDrop - 0.06, tDrop - 0.003);
        QVERIFY2(last > rmsDb(take, tDrop - 3.8, tDrop - 3.74) + 6, qPrintable(QString::number(last)));
        for (double t = tDrop - 3.8; t < tDrop - 0.12; t += 0.06)
            QVERIFY2(rmsDb(take, t, t + 0.06) < last + 0.5, qPrintable(QString("louder at %1").arg(t)));
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), drop);
        for (const VoiceInfo &v : voicesOf(after))
            QVERIFY(!risers::isRiser(v.kind));
    }

    // Long risers are heard from their first bar: every 8- and 16-bar riser
    // is within 20 dB of its last beat a quarter of the way in.
    void longRisersAreAudibleEarly() {
        for (int row : {3, 4})
            for (int kind = 0; kind < risers::kRisers; ++kind) {
                if (row == 4 && kind != risers::Noise && kind != risers::Swell)
                    continue;
                // From beat 1, the 8-bar row aims at beat 32, the 16-bar row
                // at beat 64; both start at once.
                const double drop = row == 3 ? 32.0 : 64.0, start = timeOf(1.03), tDrop = timeOf(drop);
                const auto take = testkit::play(
                    fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(kind), rowY(row), tDrop - 0.01));
                const double quarter = start + 0.25 * (tDrop - start);
                const double early = rmsDb(take, quarter - 0.5, quarter + 0.5);
                const double peak = rmsDb(take, tDrop - timeOf(1.0), tDrop - 0.01);
                QVERIFY2(early > peak - 20, qPrintable(QString("%1 row %2: %3 vs %4").arg(risers::kindName(kind)).arg(row).arg(early).arg(peak)));
            }
    }

    // The ONE with the FX on it is louder than the beat's own ONE, in any
    // drop style, and the kick keeps its weight: the risers are gone 3 ms
    // before the line, the impact waits 20 ms and its boom 40 ms (ducked
    // under the kick), nothing low goes in a room, and the drop lifts the
    // master.
    void theDropAddsToTheKick() {
        for (int style = 0; style < 3; ++style) {
            std::string fx = std::string(kBeat) + "page 3.0 7\n";
            for (int k = 0; k < style; ++k)
                fx += fmt("key %.3f Right\n", 3.02 + 0.03 * k);
            fx += fmt("down 3.1 1 %.3f %.3f p=0.6\nup 3.6 1\n", columnX(0), rowY(2));
            std::vector<VoiceInfo> v;
            StudioView view;
            testkit::play(fx + "end 4\n", [&](Session &s) {
                v = voicesOf(s.view());
                view = s.view();
            });
            QCOMPARE(int(v.size()), 1);
            const double tDrop = timeOfBeat(view, v[0].end);
            const auto with = testkit::play(fx + fmt("end %.3f\n", tDrop + 1.0));
            const auto without = testkit::play(std::string(kBeat) + fmt("end %.3f\n", tDrop + 1.0));
            const double a = rmsDb(with, tDrop, tDrop + 0.04), b = rmsDb(without, tDrop, tDrop + 0.04);
            QVERIFY2(a > b, qPrintable(QString("style %1: with %2, beat alone %3").arg(style).arg(a).arg(b)));
            // The kick keeps its weight: its low peak within half a dB.
            const double la = lowPeakDb(with, tDrop, tDrop + 0.04), lb = lowPeakDb(without, tDrop, tDrop + 0.04);
            QVERIFY2(la > lb - 0.5, qPrintable(QString("style %1: low peak %2 against %3").arg(style).arg(la).arg(lb)));
            QVERIFY2(rmsDb(with, tDrop + 0.3, tDrop + 0.45) > rmsDb(without, tDrop + 0.3, tDrop + 0.45) - 1.0,
                     qPrintable(QString::number(rmsDb(with, tDrop + 0.3, tDrop + 0.45))));
            QVERIFY(with.peak() < 1.0);
        }
    }

    // The riser is gone by the ONE (only its room rings on, and that ducks
    // under a kick) and the impact comes 20 ms after it, leaving the kick's
    // peak alone at the limiter's ceiling.
    void theImpactWaitsOutTheKicksPeak() {
        // A one-bar swell landed at beat 25.1 enters at 28 and drops at 32.
        const double tDrop = timeOf(32.0);
        const auto take = testkit::play(
            fmt("page 0 7\ntap %.3f %.3f %.3f\nend %.3f\n", timeOf(25.1), columnX(4), rowY(0), tDrop + 0.5));
        auto peak = [&](double from, double to) {
            double p = 0;
            for (size_t i = size_t(from * 48000); i < size_t(to * 48000); ++i)
                p = std::max(p, double(std::fabs(take.left[i])));
            return p;
        };
        const double before = peak(tDrop - 0.02, tDrop - 0.004);
        QVERIFY2(peak(tDrop + 0.002, tDrop + 0.018) < 0.25 * before, qPrintable(QString::number(peak(tDrop + 0.002, tDrop + 0.018))));
        QVERIFY2(peak(tDrop + 0.021, tDrop + 0.04) > 0.3 * before, qPrintable(QString::number(peak(tDrop + 0.021, tDrop + 0.04))));
        QVERIFY(take.peak() < 1.0);
    }

    // During a build the page owns the master filter, drive and room; they
    // are still held a moment before the ONE and back where they were, exactly,
    // just after it. The lift on the ONE is back by the end of its bar.
    void theMasterIsHandedBack() {
        const double tDrop = timeOf(32.0);
        const std::string build =
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 0.6 3.0 1 %.3f %.3f\n", columnX(0), rowY(3), columnX(0), rowY(3) + 0.12);
        StudioView mid, late, after;
        testkit::play(build + fmt("end %.3f\n", tDrop - 1.0), [&](Session &s) { mid = s.view(); });
        QVERIFY2(mid.master[MasterFilter] > 0.6f, qPrintable(QString::number(mid.master[MasterFilter])));
        QVERIFY(mid.master[MasterDrive] > 0.02f);
        QVERIFY(mid.master[RoomSize] > 0.4f);
        testkit::play(build + fmt("end %.3f\n", tDrop - 0.2), [&](Session &s) { late = s.view(); });
        QVERIFY(late.master[MasterFilter] > 0.6f);
        testkit::play(build + fmt("up 3.5 1\nend %.3f\n", tDrop + 0.3), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[MasterDrive], 0.0f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        // The drop comes in a notch up, and eases back over its bar.
        QVERIFY(after.master[MasterGain] > 0.85f);
        testkit::play(build + fmt("up 3.5 1\nend %.3f\n", tDrop + timeOf(4.0) + 0.2), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterGain], 0.8f);
    }

    // Strip leaves the risers alone for the build's last bar; gap mutes
    // everything else for the last half beat, and the risers stop too. Both
    // bring the mix back on the ONE: the kick is there within 3 ms of the
    // line.
    void dropStylesMuteAndHandBack() {
        for (int style : {1, 2}) {
            std::string fx = std::string(kBeat) + "page 3.0 7\n";
            for (int k = 0; k < style; ++k)
                fx += fmt("key %.3f Right\n", 3.05 + 0.05 * k);
            fx += fmt("down 3.3 1 %.3f %.3f\nup 3.8 1\n", columnX(1), rowY(3));
            std::vector<VoiceInfo> v;
            StudioView view, last, after;
            testkit::play(fx + "end 4\n", [&](Session &s) {
                v = voicesOf(s.view());
                view = s.view();
            });
            const double tDrop = timeOfBeat(view, v[0].end);
            testkit::play(fx + fmt("end %.3f\n", tDrop - 0.15), [&](Session &s) { last = s.view(); });
            QCOMPARE(last.parts[0].channel.value[Mute], 1.0f);
            QCOMPARE(last.parts[1].channel.value[Mute], 1.0f);
            QCOMPARE(last.parts[2].channel.value[Mute], 1.0f);
            QCOMPARE(last.parts[kFx].channel.value[Mute], 0.0f);
            const auto take = testkit::play(fx + fmt("end %.3f\n", tDrop + 0.3), [&](Session &s) { after = s.view(); });
            for (int p = 0; p < after.partCount; ++p)
                QCOMPARE(after.parts[p].channel.value[Mute], 0.0f);
            QVERIFY2(rmsDb(take, tDrop + 0.001, tDrop + 0.004) > -30,
                     qPrintable(QString::number(rmsDb(take, tDrop + 0.001, tDrop + 0.004))));
        }
    }

    // The styles sound different: a gap is a breath, at least 8 dB down on
    // the clean build in its half beat; a strip is the risers alone, down on
    // the clean build over its last bar even over this sparse beat.
    void dropStylesAreHeard() {
        double gapWindow[3] = {}, lastBar[3] = {};
        double tDrop = 0;
        for (int style = 0; style < 3; ++style) {
            std::string fx = std::string(kBeat) + "page 3.0 7\n";
            for (int k = 0; k < style; ++k)
                fx += fmt("key %.3f Right\n", 3.02 + 0.03 * k);
            fx += fmt("down 3.1 1 %.3f %.3f p=0.6\nup 3.6 1\n", columnX(0), rowY(2));
            if (style == 0) {
                std::vector<VoiceInfo> v;
                StudioView view;
                testkit::play(fx + "end 4\n", [&](Session &s) {
                    v = voicesOf(s.view());
                    view = s.view();
                });
                tDrop = timeOfBeat(view, v[0].end);
            }
            const auto take = testkit::play(fx + fmt("end %.3f\n", tDrop + 0.2));
            gapWindow[style] = rmsDb(take, tDrop - 0.22, tDrop - 0.01);
            lastBar[style] = rmsDb(take, tDrop - 2.0, tDrop - 0.01);
        }
        QVERIFY2(gapWindow[1] < gapWindow[0] - 8, qPrintable(QString("gap %1 clean %2").arg(gapWindow[1]).arg(gapWindow[0])));
        QVERIFY2(lastBar[2] < lastBar[0] - 1, qPrintable(QString("strip %1 clean %2").arg(lastBar[2]).arg(lastBar[0])));
    }

    // A second finger joins the drop the first one set, and its row says
    // when it comes in: a one-bar layer enters one bar before.
    void layersJoinTheSameDrop() {
        std::vector<VoiceInfo> v;
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\ndown 2.0 2 %.3f %.3f\nend 2.6\n", columnX(0), rowY(3),
                          columnX(3), rowY(0)),
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 2);
        QCOMPARE(v[0].end, 32.0);
        QCOMPARE(v[1].end, 32.0);
        QCOMPARE(v[1].start, 28.0);
        QCOMPARE(v[1].state, int(risers::Waiting));
    }

    // A click pulls a far drop in to the next bar line, and everything
    // rising gets there in time.
    void aClickDropsOnTheNextBar() {
        StudioView after;
        const auto take = testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nclick %.3f\nend %.3f\n",
                                            columnX(2), rowY(3), timeOf(5.1), timeOf(8.0) + 0.3),
                                        [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 8.0);
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(rmsDb(take, timeOf(7.6), timeOf(7.98)) > rmsDb(take, timeOf(4.5), timeOf(5.0)) + 6);
    }

    // A click with nothing building is a drop out of nothing: a reverse
    // swell into the next bar line, and the impact on it.
    void aClickAloneMakesADrop() {
        StudioView after;
        testkit::play(fmt("page 0 7\nclick %.3f\nend %.3f\n", timeOf(1.5), timeOf(4.0) + 0.2),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 4.0);
    }

    // The drop's sub is a choice: none unless SUB DROP is tapped during the
    // build, which arms one onto the ONE.
    void aSubIsArmedOntoTheDrop() {
        const double tDrop = timeOf(32.0);
        const std::string build = fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\n", columnX(0), rowY(3));
        for (bool armed : {false, true}) {
            std::string s = build;
            if (armed)
                s += fmt("tap 2.0 %.3f 0.1\n", hitX(1));
            StudioView before, after;
            testkit::play(s + fmt("end %.3f\n", tDrop - 0.5), [&](Session &se) { before = se.view(); });
            QCOMPARE(before.parts[kFx].data[risers::pub::SubArmed] > 0, armed);
            std::vector<VoiceInfo> v;
            testkit::play(s + fmt("end %.3f\n", tDrop + 0.1), [&](Session &se) { v = voicesOf(se.view()); });
            bool sub = false;
            for (const VoiceInfo &x : v)
                sub |= x.kind == risers::Sub;
            QCOMPARE(sub, armed);
        }
    }

    // A finger flicked down as it leaves pulls its riser out: no drop, and
    // the mix comes home.
    void aFlickCallsTheDropOff() {
        StudioView after;
        const auto take = testkit::play(
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 4.0 4.06 1 %.3f %.3f\nup 4.07 1\n", columnX(0), rowY(3), columnX(0),
                rowY(3) - 0.15) +
                fmt("end %.3f\n", timeOf(32.0) + 0.5),
            [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QVERIFY(rmsDb(take, timeOf(32.0) - 0.2, timeOf(32.0) + 0.4) < -60);
    }

    // Q: every riser falls away at once, the drop is called off and the
    // mix comes home.
    void theWholeBuildFallsAway() {
        StudioView after;
        const auto take = testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\ntap 0.7 %.3f %.3f len=0.2\nkey 2.5 Q\n",
                                            columnX(0), rowY(3), columnX(2), rowY(3)) +
                                            fmt("end %.3f\n", timeOf(32.0) + 0.5),
                                        [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        // It falls rather than stops: still sounding just after, quiet soon.
        QVERIFY(rmsDb(take, 2.52, 2.62) > -60);
        QVERIFY(rmsDb(take, timeOf(32.0) - 0.2, timeOf(32.0) + 0.4) < -60);
    }

    // Delete is the shared key for letting go: here the whole build falls
    // away, as Q (its first key) still does.
    void deleteDropsTheBuildAway() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\nkey 2.5 Delete\n", columnX(0), rowY(3)) +
                          fmt("end %.3f\n", timeOf(32.0) + 0.5),
                      [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
    }

    // The band under the pad does what the keys do. On the scripts' window
    // row 0 is 0.048 of the pad under it, row 1 0.119. Row 0: the clips to
    // 0.55, then the three drop styles; row 1: shorter, the length, longer
    // to 0.42, then DROP, FALL AWAY and MOVE MIX.
    void theBandUnderThePadIsClickable() {
        // DROP GAP.
        testkit::play("page 0 7\nbutton 0.2 0.785 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("gap")), qPrintable(s.pageStatus()));
        });
        // DROP with nothing building: the next bar line.
        StudioView after;
        testkit::play(fmt("page 0 7\nbutton %.3f 0.53 -0.119\nend %.3f\n", timeOf(1.5), timeOf(4.0) + 0.2),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 4.0);
        // LONGER: the keys' noise riser is 8 bars instead of 4.
        std::vector<VoiceInfo> v;
        testkit::play("page 0 7\nbutton 0.2 0.35 -0.119\nkey 0.5 A\nend 0.8\n",
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 1);
        QCOMPARE(v[0].end, 32.0);
        QVERIFY2(v[0].end - v[0].start > 20, qPrintable(QString::number(v[0].start)));
        // FALL AWAY.
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\nbutton 1.5 0.72 -0.119\nend 3.0\n", columnX(0), rowY(3)),
                      [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
    }

    // A clip slot clicked stops the loop playing in it; Shift+F1 empties it.
    void clipsAreClickedAndCleared() {
        const std::string loop = fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\n"
                                     "key 2.5 Space\n",
                                     hitX(3), hitX(3), hitX(0), hitX(3));
        StudioView after;
        testkit::play(loop + "button 4.0 0.03 -0.048\nend 6.5\n", [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, -1);
        QVERIFY(after.parts[kFx].clips[0].state != ClipView::Empty);
        testkit::play(loop + "key 4.0 Backspace\nkey 6.0 Shift+F1\nend 6.5\n", [&](Session &s) { after = s.view(); });
        QCOMPARE(int(after.parts[kFx].clips[0].state), int(ClipView::Empty));
    }

    // The pitch riser lands on the key's tonic two octaves up, and follows a
    // key change.
    void pitchedRisersLandInKey() {
        const double tDrop = timeOf(32.0);
        for (int fifths : {0, 1}) {
            std::vector<VoiceInfo> v;
            std::string s = "page 0 7\n";
            if (fifths)
                s += "key 0.1 Ctrl+Right\n";
            s += fmt("down 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(1), rowY(3), tDrop - 0.04);
            int root = 0;
            testkit::play(s, [&](Session &se) {
                v = voicesOf(se.view());
                root = se.view().root;
            });
            QCOMPARE(int(v.size()), 1);
            const double target = 36 + root + 24;
            QVERIFY2(std::fabs(v[0].note - target) < 0.8, qPrintable(QString("%1 vs %2").arg(v[0].note).arg(target)));
        }
        std::vector<VoiceInfo> v;
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 0.6 0.9 1 %.3f %.3f\n", columnX(1), rowY(3),
                          columnX(1) + 0.15, rowY(3)) +
                          fmt("end %.3f\n", tDrop - 0.04),
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 1);
        Tuning tuning;
        tuning.set(9, 0, 4, false);
        bool inKey = false;
        for (int d = -14; d < 21; ++d)
            inKey |= std::fabs(v[0].note - (tuning.baseMidi() + tuning.degreeSemis(d))) < 0.8;
        QVERIFY2(inKey && std::fabs(v[0].note - 69) > 1, qPrintable(QString::number(v[0].note)));
    }

    // The keyboard: A to G start risers of the keys' length on the phrase
    // grid, Up makes it longer, Z fires the impact, Return drops.
    void keysPlayRisersAndHits() {
        std::vector<VoiceInfo> v;
        testkit::play("page 0 7\nkey 0.3 Up\nkey 0.5 A\nkey 0.6 Z\nend 0.8\n",
                      [&](Session &s) { v = voicesOf(s.view()); });
        bool riser = false, impact = false;
        for (const VoiceInfo &x : v) {
            if (x.kind == risers::Noise) {
                riser = true;
                QCOMPARE(x.end, 32.0);
            }
            impact |= x.kind == risers::Impact;
        }
        QVERIFY(riser && impact);
        StudioView after;
        testkit::play(fmt("page 0 7\nkey 0.5 S\nkey %.3f Return\nend %.3f\n", timeOf(5.5), timeOf(8.0) + 0.2),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 8.0);
    }

    // Throw and wash take every channel's sends and hand them back when
    // the finger leaves, or the page does.
    void sendsAreHandedBack() {
        StudioView fresh, held, after;
        testkit::play("end 0.1\n", [&](Session &s) { fresh = s.view(); });
        const std::string wash = fmt("page 0 7\ndown 0.5 1 %.3f 0.12\n", hitX(6));
        testkit::play(wash + "end 1.5\n", [&](Session &s) { held = s.view(); });
        QVERIFY(held.parts[0].channel.value[ReverbSend] > 0.6f);
        QVERIFY(held.master[RoomSize] > 0.8f);
        testkit::play(wash + "page 1.5 1\nup 4.0 1\nend 4.0\n", [&](Session &s) { after = s.view(); });
        for (int p = 0; p < after.partCount; ++p)
            QCOMPARE(after.parts[p].channel.value[ReverbSend], fresh.parts[p].channel.value[ReverbSend]);
        QCOMPARE(after.master[RoomSize], 0.35f);
        QCOMPARE(after.master[RoomShimmer], 0.1f);

        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.12\nend 0.8\n", hitX(5)), [&](Session &s) { held = s.view(); });
        QVERIFY(held.parts[1].channel.value[EchoSend] > 0.6f);
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.12\nend 3.5\n", hitX(5)), [&](Session &s) { after = s.view(); });
        for (int p = 0; p < after.partCount; ++p)
            QCOMPARE(after.parts[p].channel.value[EchoSend], fresh.parts[p].channel.value[EchoSend]);
        QCOMPARE(after.master[EchoFeedback], 0.35f);
    }

    // The power-down darkens the mix into the next bar line at least two
    // beats away (from beat 4.5, the line at 8) and gives it back there.
    void aPowerDownComesBackOnTheOne() {
        StudioView mid, after;
        const std::string stop = fmt("page 0 7\ntap %.3f %.3f 0.12\n", timeOf(4.5), hitX(4));
        testkit::play(stop + fmt("end %.3f\n", timeOf(7.5)), [&](Session &s) { mid = s.view(); });
        QVERIFY(mid.master[MasterFilter] < 0.2f);
        QVERIFY(mid.master[MasterGain] < 0.5f);
        testkit::play(stop + fmt("end %.3f\n", timeOf(8.2)), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[MasterGain], 0.8f);
    }

    // Holding a hit only holds it: taps and a hold in rhythm make no loop.
    void holdingAHitNeverLoops() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ndown 1.5 9 %.3f 0.1\nup 2.4 9\nend 3.0\n", hitX(0),
                          hitX(1), hitX(5)),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, -1);
        for (const ClipView &c : after.parts[kFx].clips)
            QCOMPARE(int(c.state), int(ClipView::Empty));
    }

    // Space loops what was just played on the hit row, as on every page.
    void spaceLoopsTheHitRow() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\n", hitX(3), hitX(3),
                          hitX(0), hitX(3)) +
                          "key 2.5 Space\nend 4.0\n",
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, 0);
        QVERIFY(after.parts[kFx].clips[0].events >= 4);
    }

    // A riser looped by Space replays from the clip and still aims at a
    // drop the page can build the mix around.
    void aLoopedRiserStillAimsAtADrop() {
        StudioView after;
        testkit::play(std::string(kBeat) + fmt("page 3.0 7\ntap 3.1 %.3f %.3f\nkey 4.2 Space\nend 9\n", columnX(0), rowY(3)),
                      [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].playing >= 0);
        bool replayed = false;
        const float *d = after.parts[kFx].data;
        for (int k = 0; k < risers::pub::MaxVoices; ++k) {
            const float *o = d + risers::pub::Voices + risers::pub::Stride * k;
            replayed |= o[risers::pub::VKind] == risers::Noise && o[risers::pub::VWho] > 1.5f;
        }
        QVERIFY(replayed);
        QVERIFY(d[risers::pub::Drop] > 0);
    }

    // Recording a build and its drop: play the build over a loop, let the
    // drop land, press Space in the bar after it. The loop runs from the bar
    // the build began in to the bar after the drop, and every pass drops on
    // the same bar of it, moving the mix as it did live.
    void spaceRecordsABuildAndItsDrop() {
        for (bool rush : {false, true}) {
            std::string fx = std::string(kBeat) + fmt("page 3.0 7\ndown 3.2 1 %.3f %.3f\nup 3.5 1\n", columnX(0), rowY(2));
            std::vector<VoiceInfo> v;
            StudioView view;
            testkit::play(fx + "end 4\n", [&](Session &s) {
                v = voicesOf(s.view());
                view = s.view();
            });
            QCOMPARE(int(v.size()), 1);
            const double landing = view.beat + (3.2 - view.time) * view.bpm / 60.0;
            double drop = v[0].end;
            if (rush) {
                // A click two bars before the drop pulls it to the next bar line.
                const double click = drop - 8.5;
                fx += fmt("click %.3f\n", timeOfBeat(view, click));
                drop = std::ceil((click - 0.25) / 4.0) * 4.0;
            }
            const double space = timeOfBeat(view, drop + 4.5);
            fx += fmt("key %.3f Space\n", space);
            const double startBar = std::floor(landing / 4.0) * 4.0;
            const double length = drop + 4.0 - startBar;
            // Long enough for the loop to come round twice more.
            const double end = timeOfBeat(view, drop + 4.0 + 2 * length + 1.0);
            StudioView after;
            float drops = 0;
            testkit::play(fx + fmt("end %.3f\n", end), [&](Session &s) {
                after = s.view();
                drops = s.probe(kFx, QStringLiteral("drops"));
            });
            // The tutorial's count: the live drop and two passes of the loop.
            QVERIFY2(drops >= 3, qPrintable(QString::number(drops)));
            const PartView &fxv = after.parts[kFx];
            QVERIFY2(fxv.playing >= 0, rush ? "rush" : "build");
            QCOMPARE(double(fxv.clips[fxv.playing].lengthBeats), length);
            // The drop of the latest pass sits on the same bar as the live one.
            const double last = fxv.data[risers::pub::LastDrop];
            QVERIFY2(last > drop + length - 0.01, qPrintable(QString::number(last)));
            const double k = (last - drop) / length;
            QVERIFY2(std::fabs(k - std::round(k)) < 1e-3, qPrintable(QString("%1 %2 %3").arg(last).arg(drop).arg(length)));
            // And the mix it moved is home again after it.
            QCOMPARE(after.master[MasterFilter], 0.5f);
        }
    }

    // Ten fingers on the busiest risers stay well inside the page's share of
    // the audio callback.
    void tenFingersStayInsideBudget() {
        std::string s = "page 0 7\n";
        for (int f = 0; f < 10; ++f)
            s += fmt("down %.3f %.0f %.3f %.3f p=0.8\n", 0.2 + 0.05 * f, 100 + f, 0.05 + 0.1 * f, 0.7);
        s += "click 4.0\nend 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }

    // A song keeps the page's settings: the drop style, the keys' riser
    // length and whether builds move the mix come back with the loop.
    void theSongKeepsTheFxSettings() {
        const std::string script =
            fmt("page 0 7\nkey 0.2 Right\nkey 0.25 Right\nkey 0.3 Up\nkey 0.35 W\n"
                "tap 0.5 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.5 %.3f 0.1\ntap 2.0 %.3f 0.1\nkey 2.5 Space\nend 3.5\n",
                hitX(3), hitX(3), hitX(0), hitX(3));
        bool checked = false;
        testkit::reopen(script, 2.5, [&](Session &, Session &opened, const QJsonObject &doc) {
            const QJsonObject part = doc[QStringLiteral("parts")].toArray()[kFx].toObject();
            QCOMPARE(part[QStringLiteral("name")].toString(), opened.pageName(kFx)); // scripts count pages from 1
            const QJsonObject settings = part[QStringLiteral("settings")].toObject();
            QCOMPARE(settings[QStringLiteral("dropStyle")].toString(), QStringLiteral("strip"));
            QCOMPARE(settings[QStringLiteral("riserBars")].toInt(), 8);
            QCOMPARE(settings[QStringLiteral("buildMix")].toBool(true), false);
            const QJsonObject again = opened.saveSong()[QStringLiteral("parts")].toArray()[kFx].toObject();
            QCOMPARE(again[QStringLiteral("settings")].toObject(), settings);
            // The page shows the style, the loop plays again, and the keys
            // play 8-bar risers.
            QCOMPARE(opened.page(), kFx);
            QVERIFY2(opened.pageStatus().contains(QStringLiteral("strip")), qPrintable(opened.pageStatus()));
            QCOMPARE(opened.view().parts[kFx].playing, 0);
            KeyEvent a;
            a.key = Qt::Key_A;
            QVERIFY(opened.key(a));
            float buf[384 * 2];
            for (int tick = 0; tick < 20; ++tick) {
                setScriptClock(scriptClock() + 1 / 125.0);
                opened.refresh();
                opened.idle(scriptClock());
                opened.studio()->render(buf, 384);
            }
            opened.refresh();
            bool eightBars = false;
            for (const VoiceInfo &v : voicesOf(opened.view()))
                eightBars |= v.kind == risers::Noise && v.end - v.start > 20;
            QVERIFY(eightBars);
            checked = true;
        });
        QVERIFY(checked);
    }

    // A file edited by hand: wrong types, numbers out of range and names
    // the page doesn't know leave it on sane settings.
    void garbageSettingsOpenSanely() {
        testkit::play("page 0 7\nkey 0.2 Right\nkey 0.3 Down\nend 0.5\n", [](Session &s) {
            auto settingsAfter = [&](const QJsonObject &garbage) {
                QJsonObject doc = s.saveSong();
                QJsonArray parts = doc[QStringLiteral("parts")].toArray();
                QJsonObject fx = parts[kFx].toObject();
                fx[QStringLiteral("settings")] = garbage;
                parts[kFx] = fx;
                doc[QStringLiteral("parts")] = parts;
                QString error;
                if (!s.openSong(doc, &error))
                    return QJsonObject{{QStringLiteral("error"), error}};
                return s.saveSong()[QStringLiteral("parts")].toArray()[kFx].toObject()[QStringLiteral("settings")].toObject();
            };
            const QJsonObject defaults{{QStringLiteral("dropStyle"), QStringLiteral("clean")},
                                       {QStringLiteral("riserBars"), 4},
                                       {QStringLiteral("buildMix"), true}};
            QCOMPARE(settingsAfter({}), defaults);
            QCOMPARE(settingsAfter({{QStringLiteral("dropStyle"), 7},
                                    {QStringLiteral("riserBars"), QStringLiteral("8")},
                                    {QStringLiteral("buildMix"), QStringLiteral("no")},
                                    {QStringLiteral("extra"), QJsonArray{1, 2}}}),
                     defaults);
            QJsonObject s1 = settingsAfter({{QStringLiteral("dropStyle"), QStringLiteral("boom")},
                                            {QStringLiteral("riserBars"), 1e300}});
            QCOMPARE(s1[QStringLiteral("dropStyle")].toString(), QStringLiteral("clean"));
            QCOMPARE(s1[QStringLiteral("riserBars")].toInt(), 16);
            QCOMPARE(settingsAfter({{QStringLiteral("riserBars"), -1e300}})[QStringLiteral("riserBars")].toInt(), 1);
            // A length the field has no row for is the nearest one it has.
            QCOMPARE(settingsAfter({{QStringLiteral("riserBars"), 6}})[QStringLiteral("riserBars")].toInt(), 8);
            QJsonObject gap = settingsAfter({{QStringLiteral("dropStyle"), QStringLiteral("gap")}});
            QCOMPARE(gap[QStringLiteral("dropStyle")].toString(), QStringLiteral("gap"));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("gap")), qPrintable(s.pageStatus()));
        });
    }

    // A clip from a file may hold any number in a command's values: the
    // engine takes them all without reading out of bounds, and plays on.
    void anyCommandValuesAreSafe() {
        PartSpec spec = makeRisers();
        auto &e = *spec.engine;
        FxAudio ctx;
        ctx.tr.rate = 48000;
        ctx.tr.bpm = 124;
        ctx.tr.beatsPerSample = 124 / 60.0 / 48000.0;
        e.init(48000);
        auto run = [&](double seconds) {
            for (int b = 0; b < int(seconds * 48000 / kBlock); ++b) {
                ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
                float l[kBlock] = {}, r[kBlock] = {};
                e.render(l, r, kBlock, ctx);
                ctx.tr.sample += kBlock;
            }
        };
        const int32_t ints[] = {INT32_MIN, -1, 0, 7, 12, INT32_MAX};
        const float floats[] = {-3e38f, -1e6f, 0.0f, 1e6f, 3e38f};
        const uint16_t types[] = {Command::On, Command::Move, Command::Param, Command::Off, risers::CmdDrop,
                                  risers::CmdFall, risers::CmdMark};
        int n = 0;
        for (uint16_t type : types)
            for (int32_t i : ints)
                for (float f : floats) {
                    Command c;
                    c.type = type;
                    c.id = (n++ % 3) + (i & 1 ? (1 << 30) : 0);
                    for (int32_t &v : c.i)
                        v = i;
                    for (float &v : c.f)
                        v = f;
                    e.command(c, ctx);
                    run(0.002);
                }
        run(0.5);
        float data[kPartData];
        e.publish(data);
        QVERIFY(e.voices() <= risers::Engine::kVoices);
    }
};

TESTKIT_REGISTER(RisersTests)
#include "risers_tests.moc"
