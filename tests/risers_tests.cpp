// The FX page: risers that peak on the bar line, drops that fire on the
// sample, and a mix that is always handed back.

#include "testkit.h"

#include "../src/risers.h"

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

}

class RisersTests : public QObject {
    Q_OBJECT
private slots:
    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 7\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    // A two-bar noise riser landed just after a beat: it aims at the bar
    // line nearest two bars on, grows into it, and is gone just after it,
    // when the drop has fired on that beat.
    void aRiserPeaksOnTheBarLine() {
        // Landing at 0.5 s is beat 1.03; two bars on is 9.03, nearest bar 8.
        const double drop = 8.0, tDrop = timeOf(drop);
        std::vector<VoiceInfo> during;
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(0), rowY(1), tDrop - 0.5),
                      [&](Session &s) { during = voicesOf(s.view()); });
        QCOMPARE(int(during.size()), 1);
        QCOMPARE(during[0].kind, int(risers::Noise));
        QCOMPARE(during[0].end, drop);

        StudioView after;
        const auto take = testkit::play(
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(0), rowY(1), tDrop + 0.4),
            [&](Session &s) { after = s.view(); });
        QVERIFY(take.finite());
        // It grows: the last sixteenth before the line is far louder than a
        // bar before, and louder than any moment before it.
        const double last = rmsDb(take, tDrop - 0.06, tDrop - 0.003);
        QVERIFY2(last > rmsDb(take, tDrop - 2.0, tDrop - 1.94) + 10, qPrintable(QString::number(last)));
        for (double t = tDrop - 1.9; t < tDrop - 0.12; t += 0.06)
            QVERIFY2(rmsDb(take, t, t + 0.06) < last + 0.5, qPrintable(QString("louder at %1").arg(t)));
        // The drop fired on the bar line and the riser is gone.
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), drop);
        for (const VoiceInfo &v : voicesOf(after))
            QVERIFY(!risers::isRiser(v.kind));
    }

    // The impact starts on the drop's very sample: nothing of it before.
    void theImpactLandsOnTheSample() {
        const double tDrop = timeOf(8.0);
        // A two-bar swell: it is quiet long before the drop, so the impact
        // is easy to see.
        const auto take =
            testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f\nend %.3f\n", columnX(4), rowY(1), tDrop + 0.5));
        const size_t at = size_t(tDrop * 48000);
        double before = 0, after = 0;
        for (size_t i = at - 480; i < at; ++i)
            before = std::max(before, double(std::fabs(take.left[i])));
        for (size_t i = at; i < at + 480; ++i)
            after = std::max(after, double(std::fabs(take.left[i])));
        // The swell peaks into the line; the boom on it is bigger still.
        QVERIFY2(after > before * 1.2, qPrintable(QString("%1 %2").arg(before).arg(after)));
        QVERIFY(take.peak() < 1.0);
    }

    // During a build the page owns the master filter and drive; on the ONE
    // they are back where they were, exactly.
    void theMasterIsHandedBack() {
        const double tDrop = timeOf(8.0);
        const std::string build =
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 0.6 3.0 1 %.3f 0.75\n", columnX(0), rowY(1), columnX(0));
        StudioView mid, after;
        testkit::play(build + fmt("end %.3f\n", tDrop - 0.3), [&](Session &s) { mid = s.view(); });
        QVERIFY2(mid.master[MasterFilter] > 0.6f, qPrintable(QString::number(mid.master[MasterFilter])));
        QVERIFY(mid.master[MasterDrive] > 0.02f);
        QVERIFY(mid.master[RoomSize] > 0.4f);
        testkit::play(build + fmt("up 3.5 1\nend %.3f\n", tDrop + 0.5), [&](Session &s) { after = s.view(); });
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[MasterDrive], 0.0f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        QCOMPARE(after.master[MasterGain], 0.8f);
    }

    // The strip style takes drums and bass out for the build's last bar and
    // brings them back on the ONE; the gap style silences everything for
    // the last half beat.
    void dropStylesMuteAndHandBack() {
        const double tDrop = timeOf(8.0);
        const std::string strip =
            fmt("page 0 7\nkey 0.1 Right\nkey 0.2 Right\ndown 0.5 1 %.3f %.3f\nup 1.0 1\n", columnX(1), rowY(1));
        StudioView last, after;
        testkit::play(strip + fmt("end %.3f\n", tDrop - 0.4), [&](Session &s) { last = s.view(); });
        QCOMPARE(last.parts[0].channel.value[Mute], 1.0f);
        QCOMPARE(last.parts[1].channel.value[Mute], 1.0f);
        QCOMPARE(last.parts[2].channel.value[Mute], 0.0f);
        testkit::play(strip + fmt("end %.3f\n", tDrop + 0.2), [&](Session &s) { after = s.view(); });
        for (int p = 0; p < after.partCount; ++p)
            QCOMPARE(after.parts[p].channel.value[Mute], 0.0f);

        const std::string gap = fmt("page 0 7\nkey 0.1 Right\ndown 0.5 1 %.3f %.3f\nup 1.0 1\n", columnX(1), rowY(1));
        const auto take = testkit::play(gap + fmt("end %.3f\n", tDrop + 0.3), [&](Session &s) { after = s.view(); });
        // The half beat before the drop is quiet but for the room's tail.
        QVERIFY2(rmsDb(take, tDrop - 0.2, tDrop - 0.02) < rmsDb(take, tDrop - 0.5, tDrop - 0.3) - 12,
                 qPrintable(QString::number(rmsDb(take, tDrop - 0.2, tDrop - 0.02))));
        QVERIFY(rmsDb(take, tDrop, tDrop + 0.2) > -30);
        for (int p = 0; p < after.partCount; ++p)
            QCOMPARE(after.parts[p].channel.value[Mute], 0.0f);
    }

    // A second finger joins the drop the first one set, and its row says
    // when it comes in: a one-bar layer enters one bar before.
    void layersJoinTheSameDrop() {
        std::vector<VoiceInfo> v;
        // Four bars from beat 1.03: the drop is 16. The roll lands at beat
        // 4.1 in the 1-bar row: it waits until 12.
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\ndown 2.0 2 %.3f %.3f\nend 2.6\n", columnX(0), rowY(2),
                          columnX(3), rowY(0)),
                      [&](Session &s) { v = voicesOf(s.view()); });
        QCOMPARE(int(v.size()), 2);
        QCOMPARE(v[0].end, 16.0);
        QCOMPARE(v[1].end, 16.0);
        QCOMPARE(v[1].start, 12.0);
        QCOMPARE(v[1].state, int(risers::Waiting));
    }

    // A click pulls a far drop in to the next bar line, and everything
    // rising gets there in time.
    void aClickDropsOnTheNextBar() {
        StudioView after;
        // An 8-bar riser from beat 1 aims at 32; the click at beat 5.1 means
        // the bar line at 8.
        const auto take = testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nup 1.0 1\nclick %.3f\nend %.3f\n",
                                            columnX(2), rowY(3), timeOf(5.1), timeOf(8.0) + 0.3),
                                        [&](Session &s) { after = s.view(); });
        QCOMPARE(double(after.parts[kFx].data[risers::pub::LastDrop]), 8.0);
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        // It rushed: the last beat before the drop is loud.
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

    // A finger flicked down as it leaves pulls its riser out: no drop, and
    // the mix comes home.
    void aFlickCallsTheDropOff() {
        StudioView after;
        const auto take = testkit::play(
            fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 2.0 2.06 1 %.3f %.3f\nup 2.07 1\n", columnX(0), rowY(1), columnX(0),
                rowY(1) - 0.15) +
                fmt("end %.3f\n", timeOf(8.0) + 0.5),
            [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(after.parts[kFx].data[risers::pub::Drop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QVERIFY(rmsDb(take, timeOf(8.0) - 0.2, timeOf(8.0) + 0.4) < -60);
    }

    // Q: every riser falls away at once, the drop is called off and the
    // mix comes home.
    void theWholeBuildFallsAway() {
        StudioView after;
        const auto take = testkit::play(fmt("page 0 7\ntap 0.5 %.3f %.3f len=0.2\ntap 0.7 %.3f %.3f len=0.2\nkey 2.5 Q\n", columnX(0),
                                            rowY(1), columnX(2), rowY(1)) +
                                            fmt("end %.3f\n", timeOf(8.0) + 0.5),
                                        [&](Session &s) { after = s.view(); });
        QVERIFY(after.parts[kFx].data[risers::pub::LastDrop] < 0);
        QVERIFY(voicesOf(after).empty());
        QCOMPARE(after.master[MasterFilter], 0.5f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        // It falls rather than stops: still sounding just after, quiet soon.
        QVERIFY(rmsDb(take, 2.52, 2.62) > -60);
        QVERIFY(rmsDb(take, timeOf(8.0) - 0.2, timeOf(8.0) + 0.4) < -60);
    }

    // The pitch riser lands on the key's tonic two octaves up, and follows a
    // key change.
    void pitchedRisersLandInKey() {
        const double tDrop = timeOf(8.0);
        for (int fifths : {0, 1}) {
            std::vector<VoiceInfo> v;
            std::string s = "page 0 7\n";
            if (fifths)
                s += "key 0.1 Ctrl+Right\n";
            s += fmt("down 0.5 1 %.3f %.3f\nup 1.0 1\nend %.3f\n", columnX(1), rowY(1), tDrop - 0.04);
            int root = 0;
            testkit::play(s, [&](Session &se) {
                v = voicesOf(se.view());
                root = se.view().root;
            });
            QCOMPARE(int(v.size()), 1);
            const double target = 36 + root + 24;
            QVERIFY2(std::fabs(v[0].note - target) < 0.8, qPrintable(QString("%1 vs %2").arg(v[0].note).arg(target)));
        }
        // Slid sideways, it lands on another note of the scale.
        std::vector<VoiceInfo> v;
        testkit::play(fmt("page 0 7\ndown 0.5 1 %.3f %.3f\nglide 0.6 0.9 1 %.3f %.3f\n", columnX(1), rowY(1),
                          columnX(1) + 0.15, rowY(1)) +
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

    // The keyboard: A to G start risers of the keys' length, Up makes it
    // longer, Z fires the impact, Return drops.
    void keysPlayRisersAndHits() {
        std::vector<VoiceInfo> v;
        testkit::play("page 0 7\nkey 0.3 Up\nkey 0.5 A\nkey 0.6 Z\nend 0.8\n",
                      [&](Session &s) { v = voicesOf(s.view()); });
        bool riser = false, impact = false;
        for (const VoiceInfo &x : v) {
            if (x.kind == risers::Noise) {
                riser = true;
                // Eight bars from beat 1.03: 33, nearest bar line 32.
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
        StudioView held, after;
        const std::string wash = fmt("page 0 7\ndown 0.5 1 %.3f 0.12\n", hitX(6));
        testkit::play(wash + "end 1.5\n", [&](Session &s) { held = s.view(); });
        QVERIFY(held.parts[0].channel.value[ReverbSend] > 0.6f);
        QVERIFY(held.master[RoomSize] > 0.8f);
        // Leaving the page lets go of the wash.
        testkit::play(wash + "page 1.5 1\nup 4.0 1\nend 4.0\n", [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[0].channel.value[ReverbSend], 0.12f);
        QCOMPARE(after.parts[kFx].channel.value[ReverbSend], 0.32f);
        QCOMPARE(after.master[RoomSize], 0.35f);
        QCOMPARE(after.master[RoomShimmer], 0.1f);

        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.12\nend 0.8\n", hitX(5)), [&](Session &s) { held = s.view(); });
        QVERIFY(held.parts[1].channel.value[EchoSend] > 0.6f);
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.12\nend 3.5\n", hitX(5)), [&](Session &s) { after = s.view(); });
        StudioView fresh;
        testkit::play("end 0.1\n", [&](Session &s) { fresh = s.view(); });
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

    // Tapping a rhythm on the hit row and holding the next beat loops it.
    void hitRhythmsLoop() {
        StudioView after;
        testkit::play(fmt("page 0 7\ntap 0.5 %.3f 0.1\ntap 0.75 %.3f 0.1\ntap 1.0 %.3f 0.1\ntap 1.25 %.3f 0.1\n", hitX(3),
                          hitX(3), hitX(0), hitX(3)) +
                          fmt("down 1.5 9 %.3f 0.1\nup 1.9 9\nend 3.0\n", hitX(3)),
                      [&](Session &s) { after = s.view(); });
        QCOMPARE(after.parts[kFx].playing, 0);
        QVERIFY(after.parts[kFx].clips[0].events >= 4);
    }

    // A recorded build replays its drop: the clip's riser aims at its own
    // bar line and moves the master as the live one did.
    void aRecordedBuildReplaysItsDrop() {
        StudioView mid;
        // Record from bar 1 (beat 4) for two bars: a one-bar noise riser
        // landed at beat 4.1 aims at beat 8, and the clip loops it.
        const std::string rec = fmt("page 0 7\nkey 0.2 R\ndown %.3f 1 %.3f %.3f\nup %.3f 1\n", timeOf(4.1), columnX(0),
                                    rowY(0), timeOf(4.5)) +
                                fmt("key %.3f R\n", timeOf(9.0));
        testkit::play(rec + fmt("end %.3f\n", timeOf(15.5)), [&](Session &s) { mid = s.view(); });
        QCOMPARE(mid.parts[kFx].playing, 0);
        QCOMPARE(int(mid.parts[kFx].clips[0].lengthBeats), 8);
        // In the clip's second pass the riser (now a clip's) builds toward 16.
        QCOMPARE(double(mid.parts[kFx].data[risers::pub::Drop]), 16.0);
        QVERIFY(mid.master[MasterFilter] > 0.55f);
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
};

TESTKIT_REGISTER(RisersTests)
#include "risers_tests.moc"
