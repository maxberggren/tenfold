// The Arrange page: scenes on the bar, the lane mixer, mutes and solos, the
// moves (build, drop, breakdown, echo-out, cut, lift), and the song, written
// or recorded, replayed from background() whatever page is showing.

#include <fstream>
#include <sstream>

#include "testkit.h"

using namespace tf;

namespace {

// Plays a script like testkit::play, and calls fn with the session after
// every tick, so a test can follow the studio bar by bar.
testkit::Take watch(const std::string &script, const std::function<void(double, Session &)> &fn) {
    testkit::Take take;
    ScriptPlayer player;
    if (!player.load(script, &take.error))
        return take;
    Studio studio(48000.0f);
    studio.setTimeOrigin(0);
    setScriptClock(0);
    Session session(&studio);
    session.setClock(&scriptClock);
    const double end = player.end() + 0.01;
    float buf[384 * 2];
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > end)
            break;
        session.refresh();
        player.tick(t, session);
        studio.render(buf, 384);
        for (int i = 0; i < 384; ++i) {
            take.left.push_back(buf[2 * i]);
            take.right.push_back(buf[2 * i + 1]);
        }
        session.refresh();
        if (fn)
            fn(t, session);
    }
    take.view = session.view();
    return take;
}

// Four one-bar loops at 120 BPM, each recorded with Space from the next bar
// and closed with Space again: drums (page 1), bass (2), chords (3), atmos
// (6). Bars fall every 2 s from the start; everything plays by 16 s, and the
// arrange page shows at 16.5.
const char *kLoops = "tempo 0 120\n"
                     "page 0 1\nkey 0.2 Space\n"
                     "tap 2.0 0.05 0.5\ntap 2.5 0.25 0.5\ntap 3.0 0.05 0.5\ntap 3.5 0.25 0.5\nkey 3.6 Space\n"
                     "page 4.1 2\nkey 4.2 Space\n"
                     "tap 6.0 0.03 0.5 len=0.3\ntap 6.5 0.03 0.5 len=0.15\ntap 7.0 0.36 0.5 len=0.3\n"
                     "tap 7.5 0.3 0.5 len=0.15\nkey 7.6 Space\n"
                     "page 8.1 3\nkey 8.2 Space\n"
                     "tap 10.0 0.03 0.6 len=0.15\ntap 10.5 0.03 0.6 len=0.15\ntap 11.0 0.36 0.6 len=0.15\n"
                     "tap 11.5 0.36 0.6 len=0.15\nkey 11.6 Space\n"
                     "page 12.1 6\nkey 12.2 Space\n"
                     "tap 14.0 0.03 0.4 len=0.9\ntap 15.0 0.17 0.4 len=0.9\nkey 15.6 Space\n"
                     "page 16.5 0\n";
constexpr double kBar0 = 0; // the studio's first bar line, in script seconds; bars are 2 s long at 120
double barAt(int n) { return kBar0 + 2.0 * n; }

std::string loops(const std::string &rest) { return std::string(kLoops) + rest; }

// The pad's lanes: seven parts under the scene strip (y 0..0.885).
float laneY(int part) { return 0.885f * (1.0f - (part + 0.5f) / 7.0f); }
float cellX(int slot) { return 0.12f + (0.73f - 0.12f) * (slot + 0.5f) / 8.0f; }

// True on the first tick at or after the script time x (ticks are 8 ms apart).
bool at(double t, double x) { return t >= x - 1e-9 && t < x + 0.008 - 1e-9; }

// How far a beat is from the nearest bar line, in beats.
double offBar(double beat) { return beat - 4.0 * std::round(beat / 4.0); }

}

class ArrangeTests : public QObject {
    Q_OBJECT
private slots:
    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 0\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void theLoopsAreThere() {
        const auto take = testkit::play(loops("end 17.5\n"));
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        for (int p : {0, 1, 2, 5})
            QVERIFY2(take.view.parts[p].playing == 0, qPrintable(QString("part %1").arg(p)));
    }

    void aSceneLaunchesOnTheBar() {
        // Everything stops, then F1 brings column 1 back: not before the
        // bar line, and every clip from the top of its loop on it.
        double startedBeat = -1, keyBeat = -1;
        int playingBefore = 0;
        watch(loops("key 16.7 Backspace\nkey 18.8 F1\nend 22.5\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (t > 18.79 && keyBeat < 0)
                keyBeat = v.beat;
            if (t > 18.5 && t < 18.8)
                for (int p = 0; p < v.partCount; ++p)
                    playingBefore += v.parts[p].playing >= 0;
            if (keyBeat >= 0 && startedBeat < 0 && v.parts[0].playing == 0) {
                startedBeat = v.beat - v.parts[0].clips[0].phase * v.parts[0].clips[0].lengthBeats;
                QCOMPARE(v.parts[1].playing, 0);
                QCOMPARE(v.parts[2].playing, 0);
                QCOMPARE(v.parts[5].playing, 0);
            }
        });
        QCOMPARE(playingBefore, 0);
        QVERIFY(startedBeat > keyBeat);
        QVERIFY2(std::fabs(offBar(startedBeat)) < 1e-3, qPrintable(QString::number(startedBeat)));
    }

    void aTapStartsAClipAndATapStopsIt() {
        // Bass playing: a tap on its cell stops it on the bar; another starts it again.
        int afterStop = 99, afterStart = -9;
        watch(loops("tap 16.8 " + std::to_string(cellX(0)) + " " + std::to_string(laneY(1)) + "\n" +
                    "tap 18.8 " + std::to_string(cellX(0)) + " " + std::to_string(laneY(1)) + "\nend 20.5\n"),
              [&](double t, Session &s) {
                  if (at(t, 18.7))
                      afterStop = s.view().parts[1].playing;
                  if (at(t, 20.4))
                      afterStart = s.view().parts[1].playing;
              });
        QCOMPARE(afterStop, -1);
        QCOMPARE(afterStart, 0);
    }

    void anEmptySlotStaysHonest() {
        // A tap on an empty cell launches nothing; I with no loops writes no song.
        const auto take = testkit::play("page 0 0\ntap 0.5 0.5 0.5\nkey 0.8 I\nkey 1.0 P\nend 4\n");
        for (int p = 0; p < take.view.partCount; ++p)
            QCOMPARE(take.view.parts[p].playing, -1);
        QVERIFY(take.rms(1, 4) < 1e-4);
    }

    void aLaneRidesLevelAndFilterAndTheFilterSpringsBack() {
        // A finger on the chords lane: slide up and right (level up, high-pass),
        // let go: the level stays, the filter comes home.
        float gainHeld = 0, filtHeld = 0, gainAfter = 0, filtAfter = 0;
        const float y = laneY(2);
        watch(loops("down 17.0 9 0.8 " + std::to_string(y) +
                    "\nglide 17.1 17.7 9 0.95 " + std::to_string(y + 0.12f) + "\nup 18.1 9\nend 19.0\n"),
              [&](double t, Session &s) {
                  const auto &ch = s.view().parts[2].channel;
                  if (at(t, 17.9)) {
                      gainHeld = ch.value[Gain];
                      filtHeld = ch.value[Filter];
                  }
                  if (at(t, 18.9)) {
                      gainAfter = ch.value[Gain];
                      filtAfter = ch.value[Filter];
                  }
              });
        QVERIFY2(gainHeld > 0.82f, qPrintable(QString::number(gainHeld)));
        QVERIFY2(filtHeld > 0.65f, qPrintable(QString::number(filtHeld)));
        QVERIFY(std::fabs(gainAfter - gainHeld) < 0.01f);
        QVERIFY2(std::fabs(filtAfter - 0.5f) < 0.01f, qPrintable(QString::number(filtAfter)));
    }

    void aClickLatchesTheRide() {
        const float y = laneY(1);
        const auto take = testkit::play(loops("down 17.0 9 0.5 " + std::to_string(y) +
                                              "\nglide 17.1 17.5 9 0.3 " + std::to_string(y) +
                                              "\nclick 17.7\nup 18.1 9\nend 19.0\n"));
        QVERIFY2(take.view.parts[1].channel.value[Filter] < 0.3f,
                 qPrintable(QString::number(take.view.parts[1].channel.value[Filter])));
    }

    void pressureThrowsALaneIntoTheEcho() {
        float echo = 0;
        const float y = laneY(2);
        watch(loops("down 17.0 9 0.5 " + std::to_string(y) + " p=0.5\nglide 17.1 17.4 9 0.5 " + std::to_string(y) +
                    " p=0.95\nup 17.9 9\nend 18.1\n"),
              [&](double t, Session &s) {
                  if (at(t, 17.7))
                      echo = s.view().parts[2].channel.value[EchoSend];
              });
        QVERIFY2(echo > 0.6f, qPrintable(QString::number(echo)));
    }

    void muteKeysAndHeldSolos() {
        float drumsMuted = 0, bassSolo = 0, bassSoloAfter = 1;
        watch(loops("key 17.0 Q\nkey 17.5 S down\nkey 18.5 S up\nend 19.0\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (at(t, 17.3))
                drumsMuted = v.parts[0].channel.value[Mute];
            if (at(t, 18.0))
                bassSolo = v.parts[1].channel.value[Solo];
            if (at(t, 18.9))
                bassSoloAfter = v.parts[1].channel.value[Solo];
        });
        QCOMPARE(drumsMuted, 1.0f);
        QCOMPARE(bassSolo, 1.0f);
        QCOMPARE(bassSoloAfter, 0.0f);
    }

    void aBuildThinsTheMixTakesABreathAndDropsOnTheOne() {
        // B at bar 6.5 or so: a build to the end of the 4-bar phrase.
        double gapBeat = -1, dropBeat = -1;
        float topFilter = 0.5f, topReverb = 0;
        bool silentGap = true;
        int dropAt = -1;
        const auto take = watch(loops("key 16.8 B\nend 33.5\n"), [&](double, Session &s) {
            const StudioView &v = s.view();
            topFilter = std::max(topFilter, v.master[MasterFilter]);
            topReverb = std::max(topReverb, v.parts[2].channel.value[ReverbSend]);
            bool allMuted = true;
            for (int p = 0; p < v.partCount; ++p)
                allMuted = allMuted && v.parts[p].channel.value[Mute] > 0.5f;
            if (allMuted && gapBeat < 0)
                gapBeat = v.beat;
            if (gapBeat >= 0 && !allMuted && dropBeat < 0) {
                dropBeat = v.beat;
                dropAt = int(std::lround(v.beat / 4.0));
            }
            if (gapBeat >= 0 && dropBeat < 0)
                silentGap = silentGap && v.master[MasterFilter] < 0.95f;
        });
        QVERIFY2(topFilter > 0.65f, qPrintable(QString::number(topFilter)));
        QVERIFY(topReverb > 0.45f);
        QVERIFY(gapBeat > 0 && dropBeat > gapBeat);
        // The gap is the last half beat; the drop lands on a phrase's bar line
        // (a tick early at most, never late).
        QVERIFY2(std::fabs(dropBeat - gapBeat - 0.5) < 0.06, qPrintable(QString("%1 %2").arg(gapBeat).arg(dropBeat)));
        QVERIFY2(offBar(dropBeat) > -0.06 && offBar(dropBeat) <= 0.0001, qPrintable(QString::number(dropBeat)));
        QCOMPARE(dropAt % 4, 0);
        QVERIFY(silentGap);
        // After the drop the bus is open again and it is loud again.
        QVERIFY(std::fabs(take.view.master[MasterFilter] - 0.5f) < 0.01f);
    }

    void theDropBringsEverythingBackFromTheTop() {
        // Stop everything, then Return: on the next bar every part with a clip
        // starts from the top of its loop together.
        double anchors[4] = {-1, -1, -1, -1};
        watch(loops("key 16.7 Backspace\nkey 18.8 Return\nend 21.5\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (t < 18.8)
                return;
            const int parts[4] = {0, 1, 2, 5};
            for (int k = 0; k < 4; ++k) {
                const auto &pv = v.parts[parts[k]];
                if (anchors[k] < 0 && pv.playing >= 0 && v.beat > 4)
                    anchors[k] = v.beat - pv.clips[pv.playing].phase * pv.clips[pv.playing].lengthBeats;
            }
        });
        for (double a : anchors) {
            QVERIFY(a > 0);
            QVERIFY(std::fabs(offBar(a)) < 1e-3);
            QVERIFY(std::fabs(a - anchors[0]) < 1e-3);
        }
    }

    void aBreakdownStripsToChordsAndAtmosAndComesBack() {
        float during[7] = {}, after[7] = {};
        float pumpDuring = 1;
        watch(loops("key 16.8 N\nkey 20.1 N\nend 23.0\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (at(t, 19.5)) {
                for (int p = 0; p < 7; ++p)
                    during[p] = v.parts[p].channel.value[Mute];
                pumpDuring = v.parts[2].channel.value[PumpDepth];
            }
            if (at(t, 22.5))
                for (int p = 0; p < 7; ++p)
                    after[p] = v.parts[p].channel.value[Mute];
        });
        QCOMPARE(during[0], 1.0f); // drums
        QCOMPARE(during[1], 1.0f); // bass
        QCOMPARE(during[2], 0.0f); // chords
        QCOMPARE(during[5], 0.0f); // atmos
        QVERIFY(pumpDuring < 0.1f); // no kick, no pump
        for (float m : after)
            QCOMPARE(m, 0.0f);
    }

    void anEchoOutStopsEverythingAndLeavesATail() {
        // X just after a bar: one bar into the echo, then everything stops on
        // the next line and only the echo is left, fading.
        const auto take = watch(loops("key 18.1 X\nend 28.5\n"), {});
        for (int p = 0; p < take.view.partCount; ++p)
            QCOMPARE(take.view.parts[p].playing, -1);
        // The bar after the echo-out bar: the echo still rings.
        const double stop = barAt(9); // 18.1 → the out bar is bar 8 (20.0), the stop at bar 9 (22.0)
        QVERIFY2(take.rms(stop + 0.3, stop + 1.5) > 0.004, qPrintable(QString::number(take.rms(stop + 0.3, stop + 1.5))));
        QVERIFY(take.rms(stop + 0.3, stop + 1.5) < take.rms(stop - 2, stop));
        QVERIFY(take.rms(26, 27) < take.rms(stop + 0.3, stop + 1.5));
    }

    void aCutSilencesTheLastBeat() {
        const auto take = watch(loops("key 16.8 C\nend 20.5\n"), {});
        // The bar after 16.8 begins at 18.0: the cut is 16.0..16.5.
        QVERIFY(take.rms(17.55, 17.95) < 0.25 * take.rms(17.0, 17.45));
        QVERIFY(take.rms(18.05, 18.5) > 0.5 * take.rms(17.0, 17.45));
    }

    void aLiftChangesKeyOnTheBar() {
        int before = -1, after = -1;
        watch(loops("key 16.8 L\nend 18.5\n"), [&](double t, Session &s) {
            if (at(t, 17.8))
                before = s.view().root;
            if (at(t, 18.2))
                after = s.view().root;
        });
        QCOMPARE(before, 9);
        QCOMPARE(after, 11);
    }

    void theTensionFaderBuildsAndLetGoHighDrops() {
        // Up the fader from the middle: the bus thins; let go high: on the next
        // ONE the bus is open and every part sounds.
        float top = 0.5f;
        double gapBeat = -1;
        const auto take = watch(loops("down 16.55 9 0.94 0.4\nglide 16.6 17.0 9 0.94 0.85\nup 17.1 9\nend 21.0\n"),
                                [&](double, Session &s) {
                                    const StudioView &v = s.view();
                                    top = std::max(top, v.master[MasterFilter]);
                                    if (gapBeat < 0 && v.parts[0].channel.value[Mute] > 0.5f)
                                        gapBeat = v.beat;
                                });
        QVERIFY2(top > 0.6f, qPrintable(QString::number(top)));
        QVERIFY(gapBeat > 0);
        QVERIFY(std::fabs(offBar(gapBeat + 0.5)) < 0.06);
        QVERIFY(std::fabs(take.view.master[MasterFilter] - 0.5f) < 0.01f);
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].channel.value[Mute], 0.0f);
    }

    void theTensionFaderLetGoLowSpringsBack() {
        const auto take = watch(loops("down 16.8 9 0.94 0.4\nglide 16.9 17.3 9 0.94 0.55\nup 17.4 9\nend 19.5\n"), {});
        QVERIFY(std::fabs(take.view.master[MasterFilter] - 0.5f) < 0.01f);
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].channel.value[Mute], 0.0f);
    }

    void aWrittenTrackHasItsShape() {
        // I writes a long track and plays it from the next bar (18.0): intro
        // with drums and atmos, no bass; then the groove brings the bass; the
        // drop has everything; the breakdown has no drums.
        std::vector<std::array<int, 7>> bars;
        std::vector<std::array<int, 7>> muted;
        const auto take = watch(loops("key 16.8 I\nend 176.5\n"), [&](double t, Session &s) {
            const double bar = (t - 18.0) / 2.0 - 0.25;
            if (bar >= 0 && std::fabs(bar - std::round(bar)) < 0.003 && int(bars.size()) <= int(std::round(bar))) {
                std::array<int, 7> b{}, m{};
                for (int p = 0; p < 7; ++p) {
                    b[size_t(p)] = s.view().parts[p].playing;
                    m[size_t(p)] = s.view().parts[p].channel.value[Mute] > 0.5f;
                }
                bars.push_back(b);
                muted.push_back(m);
            }
        });
        QVERIFY(bars.size() >= 74);
        auto audible = [&](int bar, int part) { return bars[size_t(bar)][size_t(part)] >= 0 && !muted[size_t(bar)][size_t(part)]; };
        // Intro.
        QVERIFY(audible(0, 0) && audible(0, 5) && !audible(0, 1));
        // Groove.
        QVERIFY(audible(9, 0) && audible(9, 1));
        // First drop (bars 20–35): everything with a clip.
        for (int p : {0, 1, 2, 5})
            QVERIFY2(audible(21, p), qPrintable(QString("drop part %1").arg(p)));
        // Breakdown (36–43): chords and atmos, no drums or bass.
        QVERIFY(!audible(38, 0) && !audible(38, 1) && audible(38, 2) && audible(38, 5));
        // Second drop (48–63).
        for (int p : {0, 1, 2, 5})
            QVERIFY(audible(50, p));
        // After the echo-out, everything has stopped; the track ends in its echo.
        for (int p = 0; p < 7; ++p)
            QCOMPARE(bars[73][size_t(p)], -1);
        QVERIFY(take.rms(18.0 + 2 * 21, 18.0 + 2 * 23) > 1.4 * take.rms(18.0 + 2 * 37, 18.0 + 2 * 39));
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void aRecordedSongReplaysBarByBarFromAnyPage() {
        // Record: four bars of everything, F1-less: mute the drums for two
        // bars with the keys, then stop recording. Play it back from another
        // page: the mutes come back on the same bars.
        //   16.8 O → recording from 18.0 (bar 7)
        //   20.1 Q → drums muted from 20.1; 24.1 Q → back
        //   24.8 O → recording ends on 26.0: four bars (18.0–26.0)
        //   26.2 P → the song plays from 28.0; 26.5 page 2: the arrange page is hidden
        std::vector<std::pair<double, float>> mutes;
        const auto take = watch(loops("key 16.8 O\nkey 20.1 Q\nkey 24.1 Q\nkey 24.8 O\nkey 26.2 P\npage 26.5 2\nend 37.0\n"),
                                [&](double t, Session &s) {
                                    const float m = s.view().parts[0].channel.value[Mute];
                                    if (mutes.empty() || mutes.back().second != m)
                                        mutes.push_back({t, m});
                                });
        // Muted live at 20.1, back at 24.1; replayed at 30.1 (±1/16) and 32.6.
        QVERIFY2(mutes.size() >= 5, qPrintable(QString::number(mutes.size())));
        QVERIFY(std::fabs(mutes[1].first - 20.1) < 0.02);
        QVERIFY(std::fabs(mutes[2].first - 24.1) < 0.02);
        QVERIFY2(std::fabs(mutes[3].first - 30.1) < 0.15, qPrintable(QString::number(mutes[3].first)));
        QVERIFY2(std::fabs(mutes[4].first - 34.1) < 0.15, qPrintable(QString::number(mutes[4].first)));
        QCOMPARE(take.view.parts[0].channel.value[Mute], 0.0f);
    }

    void aRecordedBuildReplays() {
        // A build recorded into the song comes back with its drop on the same bar.
        //   16.8 O → take from 18; 18.2 B → a build from 20 to the end of the
        //   4-bar phrase (the studio's bar 12, at 24), where it drops;
        //   25.3 O → the take ends at 26 (four bars); 26.2 P → plays from 28:
        //   its build from 30, its drop at 34
        std::vector<double> drops;
        bool wasGap = false;
        watch(loops("key 16.8 O\nkey 18.2 B\nkey 25.3 O\nkey 26.2 P\nend 36\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            bool all = true;
            for (int p = 0; p < v.partCount; ++p)
                all = all && v.parts[p].channel.value[Mute] > 0.5f;
            if (wasGap && !all)
                drops.push_back(t);
            wasGap = all;
        });
        QCOMPARE(int(drops.size()), 2);
        QVERIFY2(std::fabs(drops[0] - 24.0) < 0.02, qPrintable(QString::number(drops[0])));
        QVERIFY2(std::fabs(drops[1] - 34.0) < 0.02, qPrintable(QString::number(drops[1])));
    }

    void theDropLaunchesNothingThePlayerHasNotPlayed() {
        // The bass is stopped (a tap on its clip) and stays stopped for more
        // than sixteen bars; the drop brings back the others, not the bass.
        const std::string tapBass = std::to_string(cellX(0)) + " " + std::to_string(laneY(1));
        const auto take = testkit::play(loops("tap 16.8 " + tapBass + "\nkey 52.8 Return\nend 56.5\n"));
        QCOMPARE(take.view.parts[1].playing, -1);
        for (int p : {0, 2, 5})
            QCOMPARE(take.view.parts[p].playing, 0);
        // And with nothing played at all, a drop does nothing.
        const auto none = testkit::play("page 0 0\nkey 0.5 Return\nkey 0.6 B\nend 6\n");
        QVERIFY(none.rms(0, 6) < 1e-4);
    }

    void aHeldClipAndAClickClearsIt() {
        // Hold the chords clip still, click: the slot is empty. Touch the
        // bass clip, press Delete: that one too.
        const std::string chords = std::to_string(cellX(0)) + " " + std::to_string(laneY(2));
        const std::string bass = std::to_string(cellX(0)) + " " + std::to_string(laneY(1));
        const auto take = testkit::play(loops("down 16.8 9 " + chords + "\nclick 17.2\nup 17.5 9\n" +
                                              "down 18.0 10 " + bass + "\nup 18.4 10\nkey 18.5 Delete\nend 19.5\n"));
        QVERIFY(take.view.parts[2].clips[0].state == ClipView::Empty);
        QVERIFY(take.view.parts[1].clips[0].state == ClipView::Empty);
        QVERIFY(take.view.parts[0].clips[0].state != ClipView::Empty);
        // Holding a clip to clear it doesn't also launch or stop anything.
        QCOMPARE(take.view.parts[0].playing, 0);
    }

    void storedScenesRecallTheMix() {
        // Shift+F3 stores what plays; Backspace stops it all; F3 brings it back.
        const auto take = testkit::play(loops("key 16.8 Shift+F3\nkey 17.0 Backspace\nkey 18.5 F3\nend 20.5\n"));
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].playing, 0);
    }

    void staysLeanOnTheQueue() {
        // A whole written track sends a modest number of mixer changes: only
        // real changes, never every tick.
        int changes = 0;
        float last[8][ChannelCount] = {};
        watch(loops("key 16.8 Shift+I\nend 61.5\n"), [&](double, Session &s) {
            for (int p = 0; p < 7; ++p)
                for (int c = 0; c < ChannelCount; ++c) {
                    const float v = s.view().parts[p].channel.value[c];
                    if (v != last[p][c])
                        ++changes;
                    last[p][c] = v;
                }
        });
        QVERIFY2(changes < 6000, qPrintable(QString::number(changes)));
    }

    void debugSong() {
        const char *path = std::getenv("ARRANGE_DEBUG");
        if (!path)
            QSKIP("set ARRANGE_DEBUG to a script to print the studio bar by bar");
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        int lastBar = -1;
        watch(ss.str(), [&](double t, Session &s) {
            const StudioView &v = s.view();
            const int bar = int(std::floor(v.beat / 4));
            if (bar == lastBar)
                return;
            lastBar = bar;
            std::string line;
            for (int p = 0; p < v.partCount; ++p) {
                const auto &pv = v.parts[p];
                line += std::to_string(pv.playing) + (pv.channel.value[Mute] > 0.5f ? "m " : "  ");
            }
            std::printf("t=%7.2f bar=%4d %s mf=%.2f | %s\n", t, bar, line.c_str(), v.master[MasterFilter],
                        s.pageStatus().toStdString().c_str());
        });
    }
};

TESTKIT_REGISTER(ArrangeTests)
#include "arrange_tests.moc"
