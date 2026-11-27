// The studio and the session: clock, clips, mixer, pages and keys.

#include <QDir>
#include <QJsonArray>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QPainter>

#include "console.h"
#include "testkit.h"
#ifdef __linux__
#include "touchsource.h"
#endif

using namespace tf;

namespace {

// Kick, snare, kick, snare half a second apart on the drums page, then
// Space where the next kick would land: a one-bar loop at 120 BPM.
struct NoFrames : FrameSink {
    void frame(const Frame &) override {}
};

const char *kBeat = "page 0 1\n"
                    "tap 0.5 0.05 0.5\ntap 1.0 0.25 0.5\ntap 1.5 0.05 0.5\ntap 2.0 0.25 0.5\n"
                    "key 2.5 Space\n";

}

class FrameworkTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "theBenchRunsWellInsideRealTime")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev",
                "escPausesAndPlaysWithThePadNotTaken escGivesTheTakenPadBackBeforeItPauses spaceLoopsWhatWasJustPlayedAndSetsTheTempo "
                "overAPlayingLoopSpaceTakesTheBarsJustPlayed pauseStopsEveryLoopAndPlayBringsThemBackOnTheOne "
                "playWithNothingToPlaySaysSo aNewLoopPlaysItsFirstBeatOnTheFirstPass aLateSpaceAddsNoEmptyBars "
                "ctrlZTakesBackTheNewestLoopAndBringsBackTheOneBefore ctrlZTakesBackThisPagesOwnLoop "
                "ctrlShiftZBringsBackWhatCtrlZTookBack arrowsJustAfterSpaceSetTheLoopsLength "
                "theLastLoopStaysResizableUntilAnotherIsMade startingOverSetsTheMixBack ctrlNTwiceStartsOver "
                "aMelodyThatStartsMidBarLoopsWithoutAGap clipsCanBeCopiedLayeredAndChained "
                "spaceLayersOntoThePlayingLoop aLayerOnAMonoPartPlaysOnAVoiceOfItsOwn "
                "aLayerLandsWithoutCuttingTheLoop shiftSpaceMakesANewLoopInstead "
                "spaceWithNothingPlayedMakesNoLoop returnKeysDoWhatThePagesSay aLoopMadeWhilePausedKeepsThePausedLoops aSongSavedPausedOpensReadyToPlay aNewLoopTakesOnlyThePlayingSinceTheLoop numberKeysChangePages ctrlKeysMoveTempoAndKey "
                "leavingAPageLetsGoOfHeldNotes stoppingWaitsForTheBar muteSilencesAChannel "
                "armedRecordingMakesABarLongClip everyPageDrawsWithoutTouches "
                "anUnreadablePadWithoutPkexecSaysWhatIsMissing keysAreNamedAsTheSystemNamesThem "
                "quantiseIsOnUntilThePlayerTurnsItOffAndKeptWithTheSong "
                "theMetronomeClicksWithNothingPlaying newLoopStopsThePagesLoopAndTheNextSpaceMakesAnother "
                "aLateFirstHitLeavesTheLoopOnTheBeat")
private slots:
    // Esc with the trackpad not taken (a script never has it) pauses and
    // plays, as Ctrl+P does: every loop stops at once, and comes back.
    void escPausesAndPlaysWithThePadNotTaken() {
        const std::string script = std::string(kBeat) + "key 3.3 Esc\nkey 4.9 Esc\nend 6\n";
        bool pausedSeen = false, stoppedAtOnce = true;
        int afterPlay = -2;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            if (t > 3.32 && t < 4.85) {
                pausedSeen |= s.paused();
                stoppedAtOnce &= s.view().parts[0].playing < 0;
            }
            if (t > 4.93 && t < 4.95)
                afterPlay = s.view().parts[0].playing;
        });
        QVERIFY(take.error.empty());
        QVERIFY(pausedSeen);
        QVERIFY(stoppedAtOnce);
        QVERIFY2(take.rms(3.8, 4.85) < 0.002, qPrintable(QString::number(take.rms(3.8, 4.85))));
        QCOMPARE(afterPlay, 0);
        QVERIFY(take.rms(5, 6) > 0.01);
    }

    // With the pad taken (or being taken) Esc gives it back and nothing
    // more: the loops play on. Only the next Esc pauses. And before the pad,
    // Esc leaves the tutorial.
    void escGivesTheTakenPadBackBeforeItPauses() {
        Session::Escaped first = Session::Escaped::Menu, second = Session::Escaped::Menu;
        bool playedOnAfterPad = true, pausedAfterSecond = false;
        const auto take = testkit::watch(std::string(kBeat) + "end 5\n", [&](double t, Session &s) {
            if (t > 3.0 && first == Session::Escaped::Menu)
                first = s.escape(true);
            if (t > 3.1 && t < 3.9)
                playedOnAfterPad &= s.view().parts[0].playing == 0 && !s.paused();
            if (t > 4.0 && second == Session::Escaped::Menu)
                second = s.escape(false);
            if (t > 4.1)
                pausedAfterSecond |= s.paused();
        });
        QVERIFY(take.error.empty());
        QVERIFY(first == Session::Escaped::Pad);
        QVERIFY(playedOnAfterPad);
        QVERIFY(second == Session::Escaped::Pause);
        QVERIFY(pausedAfterSecond);

        testkit::play("key 0.1 Ctrl+T\nend 0.5\n", [](Session &s) {
            QVERIFY(s.tutorial().active());
            QVERIFY(s.escape(true) == Session::Escaped::Tutorial);
            QVERIFY(!s.tutorial().active());
            QVERIFY(s.escape(true) == Session::Escaped::Pad);
        });
        // A script's Esc leaves the tutorial too, and doesn't pause.
        testkit::play("key 0.1 Ctrl+T\nkey 0.3 Esc\nend 0.5\n", [](Session &s) { QVERIFY(!s.tutorial().active()); });
    }

    // Quantise off, a first loop keeps every hit as played, but its bar sits
    // where the playing's own beat is, not on its first hit (a shadow
    // quantise finds it). A bar at 100 BPM, K . H . S . H . K . K . S . H in
    // eighths, three times round, the kicks 25 ms behind the beat: the
    // snares land on their beats, and the kicks stay late.
    void aLateFirstHitLeavesTheLoopOnTheBeat() {
        const double beat = 0.6;
        const int pat[8] = {0, 2, 1, 2, 0, 0, 1, 2};
        const double xs[3] = {0.05, 0.25, 0.45}, late[3] = {0.025, 0.0, -0.01};
        for (const bool quantise : {false, true}) {
            std::string script = quantise ? "page 0 1\n" : "page 0 1\nkey 0.1 Ctrl+G\n";
            unsigned r = 12345; // the hand's own scatter, ±10 ms
            auto scatter = [&]() {
                r = r * 1103515245u + 12345u;
                return 0.02 * (double((r >> 8) & 0xffff) / 65535.0 - 0.5);
            };
            char line[96];
            for (int i = 0; i < 24; ++i) {
                const int k = pat[i % 8];
                std::snprintf(line, sizeof line, "tap %.4f %.2f 0.5\n", 0.5 + i * beat / 2 + late[k] + scatter(), xs[k]);
                script += line;
            }
            std::snprintf(line, sizeof line, "key %.4f Space\nend %.4f\n", 0.5 + 12 * beat + 0.03, 0.5 + 12 * beat + 4);
            script += line;
            testkit::play(script, [&](Session &s) {
                QCOMPARE(int(std::lround(s.view().bpm)), 100);
                ClipSet c;
                QVERIFY(s.studio()->readClips(c));
                const auto &clip = c.clips[0][0];
                QCOMPARE(clip.length, 8.0);
                double kicks = 0, snares = 0;
                int nk = 0, ns = 0;
                for (const auto &e : clip.events) {
                    if (!(e.c.flags & Command::Quantize) && e.c.type != Command::On)
                        continue;
                    const double eighth = std::round(e.pos * 2) / 2;
                    const double ms = (e.pos - eighth) * beat * 1000;
                    const int k = pat[int(std::fmod(eighth * 2, 8.0))];
                    if (k == 1) {
                        snares += ms;
                        ++ns;
                    } else if (k == 0) {
                        kicks += ms;
                        ++nk;
                    }
                }
                QCOMPARE(ns, 4);
                QCOMPARE(nk, 6);
                // The snares on their beats, the scatter aside (on the first
                // hit's bar they were 28 ms early).
                QVERIFY2(std::fabs(snares / ns) < (quantise ? 3 : 6), qPrintable(QString::number(snares / ns)));
                // Quantise off, the kicks are as late as they were played (25
                // ms, less what the snares and hats pull the beat); on, pulled
                // most of the way to it.
                if (quantise)
                    QVERIFY2(std::fabs(kicks / nk) < 6, qPrintable(QString::number(kicks / nk)));
                else
                    QVERIFY2(kicks / nk > 12 && kicks / nk < 30, qPrintable(QString::number(kicks / nk)));
            });
        }
    }

    void spaceLoopsWhatWasJustPlayedAndSetsTheTempo() {
        const auto take = testkit::play(std::string(kBeat) + "end 8\n");
        QVERIFY(take.error.empty());
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(int(take.view.parts[0].clips[0].lengthBeats), 4);
        QCOMPARE(take.view.parts[0].clips[0].events, 4);
        // Long after the fingers left, the beat goes on.
        QVERIFY(take.rms(5, 8) > 0.01);
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void overAPlayingLoopSpaceTakesTheBarsJustPlayed() {
        // The beat plays; four bass notes go into the bar from 4.5 s (beat
        // 8 at 120 BPM, counted from the loop's bar), and Space a little
        // after the next bar line takes that bar.
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3.0 2\n"
                                        "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                        "key 6.7 Space\nend 9\n");
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 4);
        QCOMPARE(take.view.parts[1].clips[0].events, 8); // four notes, on and off
        QCOMPARE(take.view.parts[0].playing, 0);         // the beat is untouched
    }

    // Pause (Ctrl+P) stops every loop at once; play brings the same loops
    // back together, at once, on a downbeat (the clock moves on to the bar
    // line), not up to a bar later.
    void pauseStopsEveryLoopAndPlayBringsThemBackOnTheOne() {
        const std::string script = std::string(kBeat) +
                                   "page 3.0 2\n"
                                   "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                   "key 6.7 Space\nkey 7.3 Ctrl+P\nkey 8.9 Ctrl+P\nend 12\n";
        bool pausedSeen = false, stoppedAtOnce = true;
        int afterPlay[2] = {-2, -2};
        double beatAtPlay = -1;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            if (t > 7.32 && t < 8.85) {
                pausedSeen |= s.paused();
                stoppedAtOnce &= s.view().parts[0].playing < 0 && s.view().parts[1].playing < 0;
            }
            if (t > 8.93 && t < 8.95) {
                afterPlay[0] = s.view().parts[0].playing;
                afterPlay[1] = s.view().parts[1].playing;
                beatAtPlay = s.view().beat;
            }
        });
        QVERIFY(take.error.empty());
        QVERIFY(pausedSeen);
        QVERIFY(stoppedAtOnce);
        // Silent while paused (once the last hits have rung away).
        QVERIFY2(take.rms(7.8, 8.85) < 0.002, qPrintable(QString::number(take.rms(7.8, 8.85))));
        // Both loops again, at once, the beat's first kick with them.
        QCOMPARE(afterPlay[0], 0);
        QCOMPARE(afterPlay[1], 0);
        QVERIFY(take.rms(8.9, 9.0) > 0.02);
        const double into = beatAtPlay - 4.0 * std::floor(beatAtPlay / 4.0);
        QVERIFY2(into < 0.2, qPrintable(QString("%1 beats into the bar").arg(into)));
        // And they go on together.
        QVERIFY(take.rms(10, 12) > 0.01);
        QVERIFY(take.finite());
    }

    // The metronome left on is silent through a pause and clicks again on
    // play: it is still on (and shown on) throughout.
    void theMetronomeRestsWhileTheSongIsPaused() {
        const std::string head = std::string(kBeat) + "key 3.0 Ctrl+M\n";
        const std::string tail = "key 5.3 Ctrl+P\nkey 7.9 Ctrl+P\nend 9.5\n";
        bool onWhilePaused = true;
        const auto take = testkit::watch(head + tail, [&](double t, Session &s) {
            if (t > 5.4 && t < 7.85)
                onWhilePaused &= s.view().metronome;
        });
        QVERIFY(take.error.empty());
        QVERIFY(onWhilePaused);
        // Paused: nothing at all, not a click (once the last hits have rung away).
        QVERIFY2(take.rms(5.8, 7.85) < 0.002, qPrintable(QString::number(take.rms(5.8, 7.85))));
        // Playing again, the clicks are back over the loop: the same song
        // without the metronome differs from it by the clicks alone.
        const auto bare = testkit::play(std::string(kBeat) + tail);
        QVERIFY(bare.error.empty());
        const size_t a = size_t(8.0 * 48000), b = std::min({size_t(9.5 * 48000), take.left.size(), bare.left.size()});
        double d = 0;
        for (size_t i = a; i < b; ++i)
            d += std::pow(double(take.left[i]) - bare.left[i], 2);
        const double clicks = b > a ? std::sqrt(d / double(b - a)) : 0;
        QVERIFY2(clicks > 0.002, qPrintable(QString::number(clicks)));
        QVERIFY(take.finite());
    }

    // With nothing played yet nothing is paused: the metronome counts in.
    void theMetronomeClicksWithNothingPlaying() {
        const auto take = testkit::play("page 0 1\nkey 0.2 Ctrl+M\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY2(take.rms(0.5, 2.5) > 0.002, qPrintable(QString::number(take.rms(0.5, 2.5))));
    }

    // Turned off while paused, it stays off when play comes.
    void theMetronomeTurnedOffInAPauseStaysOff() {
        bool on = true;
        testkit::play(std::string(kBeat) + "key 3.0 Ctrl+M\nkey 5.3 Ctrl+P\nkey 6.0 Ctrl+M\nkey 7.9 Ctrl+P\nend 9\n",
                      [&](Session &s) { on = s.view().metronome; });
        QVERIFY(!on);
    }

    // Quantise is on until the player turns it off (Ctrl+G, or the button),
    // says so, and is kept with the song; a song saved before there was a
    // choice opens with it on, as it was quantised.
    void quantiseIsOnUntilThePlayerTurnsItOffAndKeptWithTheSong() {
        bool on = false;
        testkit::play("page 0 1\nend 0.2\n", [&](Session &s) { on = s.view().quantise; });
        QVERIFY(on);
        QString notice;
        testkit::play("page 0 1\nkey 0.1 Ctrl+G\nend 0.3\n", [&](Session &s) { notice = s.notice(nullptr); });
        QVERIFY2(notice.startsWith(QStringLiteral("Quantise off")), qPrintable(notice));
        bool saved = false, kept = false;
        testkit::reopen("page 0 1\nkey 0.1 Ctrl+G\nend 0.3\n", 0.2,
                        [&](Session &before, Session &opened, const QJsonObject &doc) {
                            saved = !before.view().quantise && doc.contains(QStringLiteral("quantise")) &&
                                    !doc.value(QStringLiteral("quantise")).toBool(true);
                            kept = !opened.view().quantise;
                        });
        QVERIFY(saved);
        QVERIFY(kept);
        // An older song, without the key: quantised, as it always was.
        bool old = false;
        testkit::play("page 0 1\nkey 0.1 Ctrl+G\nend 0.3\n", [&](Session &before) {
            QString error;
            QJsonObject doc = before.saveSong(&error);
            doc.remove(QStringLiteral("quantise"));
            Studio studio(48000.0f);
            Session opened(&studio);
            opened.setClock(&scriptClock);
            QVERIFY2(opened.openSong(doc, &error), qPrintable(error));
            opened.refresh();
            old = opened.view().quantise;
        });
        QVERIFY(old);
        bool onAgain = false;
        testkit::play("page 0 1\nkey 0.1 Ctrl+G\nkey 0.2 Ctrl+G\nend 0.4\n",
                      [&](Session &s) { onAgain = s.view().quantise; });
        QVERIFY(onAgain);
    }

    void playWithNothingToPlaySaysSo() {
        QString notice;
        testkit::play("page 0 1\nkey 0.5 Ctrl+P\nend 1\n", [&](Session &s) { notice = s.notice(nullptr); });
        QVERIFY2(notice.contains(QStringLiteral("Nothing to play")), qPrintable(notice));
    }

    void aNewLoopPlaysItsFirstBeatOnTheFirstPass() {
        // Space closes the phrase where the next kick would land: that kick
        // comes from the loop at once, not a whole bar later.
        const auto take = testkit::play(std::string(kBeat) + "end 3.5\n");
        QVERIFY(take.rms(2.5, 2.62) > 0.02);
    }

    void aRootHeldAcrossTheBarIsInTheLoop() {
        // Over the beat (bars at 2.5, 4.5, 6.5 s), a bass root lands a little
        // before the bar at 4.5 and is held through it, then one more note;
        // Space after the next bar loops that bar with the root on its one.
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3.0 2\n"
                                        "down 4.425 1 0.05 0.5\nup 5.4 1\ntap 5.5 0.3 0.5\n"
                                        "key 6.6 Space\nend 9\n");
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 4);
        QCOMPARE(take.view.parts[1].clips[0].events, 4);
    }

    void aLateSpaceAddsNoEmptyBars() {
        // A bar of bass over the beat (bars at 4.5 and 6.5 s), then Space
        // two beats into the following bar: still that one bar, no silence.
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3.0 2\n"
                                        "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                        "key 7.5 Space\nend 10\n");
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 4);
        QCOMPARE(take.view.parts[1].clips[0].events, 8);
    }

    void spaceLeavesOutTheTinkeringBeforeARepeatedBeat() {
        // A few stray pads while finding the groove, then kick-snare four
        // times round, then Space on the next kick: the loop is the groove
        // (two rounds of it, a bar at 120 BPM), not the noodling.
        const auto take = testkit::play("page 0 1\n"
                                        "tap 0.3 0.38 0.38\ntap 0.55 0.62 0.62\ntap 0.8 0.88 0.38\n"
                                        "tap 1.5 0.12 0.12\ntap 2.0 0.38 0.12\ntap 2.5 0.12 0.12\ntap 3.0 0.38 0.12\n"
                                        "tap 3.5 0.12 0.12\ntap 4.0 0.38 0.12\ntap 4.5 0.12 0.12\ntap 5.0 0.38 0.12\n"
                                        "key 5.5 Space\nend 8\n");
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(take.view.parts[0].clips[0].events, 4);
    }

    void overABeatSpaceLoopsTheBarsThatRepeat() {
        // Over the beat (bars at 4.5, 6.5, 8.5 s): a bar of trying things,
        // then the same bass bar twice, then Space after the bar line. The
        // loop is the bar that repeated, without the bar before it.
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3.0 2\n"
                                        "tap 4.6 0.7 0.5\ntap 5.3 0.9 0.5\ntap 6.1 0.5 0.5\n"
                                        "tap 6.55 0.1 0.5\ntap 7.05 0.3 0.5\ntap 7.55 0.2 0.5\ntap 8.05 0.1 0.5\n"
                                        "tap 8.55 0.1 0.5\ntap 9.05 0.3 0.5\ntap 9.55 0.2 0.5\ntap 10.05 0.1 0.5\n"
                                        "key 10.7 Space\nend 13\n");
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 4);
        QCOMPARE(take.view.parts[1].clips[0].events, 8);
    }

    void ctrlZTakesBackTheNewestLoopAndBringsBackTheOneBefore() {
        // The beat, then two bass loops one after the other: the second
        // (Shift+Space) replaces the first. Ctrl+Z takes the second back and
        // the first plays again; Ctrl+Z again takes the first back too, and
        // on the bass page that is all: the beat plays on.
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3.0 2\n"
                                        "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                        "key 6.6 Space\n"
                                        "tap 6.55 0.6 0.5\ntap 7.05 0.8 0.5\ntap 7.55 0.7 0.5\ntap 8.05 0.6 0.5\n"
                                        "key 8.6 Shift+Space\n"
                                        "key 9.5 Ctrl+Z\nend 10.5\n");
        QCOMPARE(take.view.parts[1].playing, 0);
        QVERIFY(take.view.parts[1].clips[1].state == ClipView::Empty);
        QCOMPARE(take.view.parts[0].playing, 0);
        const auto twice = testkit::play(std::string(kBeat) +
                                         "page 3.0 2\n"
                                         "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                         "key 6.6 Space\nkey 7.5 Ctrl+Z\nkey 7.8 Ctrl+Z\nend 9\n");
        QCOMPARE(twice.view.parts[1].playing, -1);
        QVERIFY(twice.view.parts[1].clips[0].state == ClipView::Empty);
        QCOMPARE(twice.view.parts[0].playing, 0); // the drums are the drums page's to take back
    }

    void ctrlZTakesBackThisPagesOwnLoop() {
        // Drums looped, then a bass loop: on the drums page Ctrl+Z takes
        // back the drums, not the bass that came later.
        const std::string base = std::string(kBeat) +
                                 "page 3.0 2\n"
                                 "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                 "key 6.6 Space\n";
        const auto drums = testkit::play(base + "page 7.0 1\nkey 7.2 Ctrl+Z\nend 8\n");
        QCOMPARE(drums.view.parts[0].playing, -1);
        QCOMPARE(drums.view.parts[1].playing, 0);
        // On the bass page with nothing of its own to take back, nothing
        // else goes.
        const auto none = testkit::play(std::string(kBeat) + "page 3.0 2\nkey 3.2 Ctrl+Z\nend 4\n");
        QCOMPARE(none.view.parts[0].playing, 0);
    }

    void ctrlShiftZBringsBackWhatCtrlZTookBack() {
        // A bass loop, then a second line layered on: Ctrl+Z takes the layer
        // off, Ctrl+Shift+Z brings it back, Ctrl+Z takes it off again. A new
        // loop after a Ctrl+Z forgets what could have come back.
        const std::string base = std::string(kBeat) +
                                 "page 3.0 2\n"
                                 "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                 "key 6.6 Space\n"
                                 "tap 6.8 0.6 0.5\ntap 7.8 0.7 0.5\ntap 8.8 0.6 0.5\ntap 9.8 0.7 0.5\n"
                                 "key 10.6 Space\n";
        const auto redone = testkit::play(base + "key 11.0 Ctrl+Z\nkey 11.5 Ctrl+Shift+Z\nend 12.5\n");
        QCOMPARE(redone.view.parts[1].playing, 0);
        QCOMPARE(redone.view.parts[1].clips[0].events, 12);
        const auto again = testkit::play(base + "key 11.0 Ctrl+Z\nkey 11.5 Ctrl+Shift+Z\nkey 12.0 Ctrl+Z\nend 12.5\n");
        QCOMPARE(again.view.parts[1].clips[0].events, 8);
        // Twice back, twice forward: both steps return, in order.
        const auto both = testkit::play(base + "key 11.0 Ctrl+Z\nkey 11.3 Ctrl+Z\nkey 11.6 Ctrl+Shift+Z\n"
                                               "key 11.9 Ctrl+Shift+Z\nend 12.5\n");
        QCOMPARE(both.view.parts[1].playing, 0);
        QCOMPARE(both.view.parts[1].clips[0].events, 12);
        // Nothing to bring back on a page where nothing was taken back.
        const auto other = testkit::play(base + "key 11.0 Ctrl+Z\npage 11.2 1\nkey 11.5 Ctrl+Shift+Z\nend 12.5\n");
        QCOMPARE(other.view.parts[1].clips[0].events, 8);
    }

    void aLineSlidWithOneFingerLoopsWhole() {
        // Over the beat (120 BPM, bars every 2 s from 4.5 s), one bass finger
        // lands and slides through four columns, a bar on each, without
        // lifting; Space just after: all four bars loop, not the bar after
        // the one note that landed.
        std::string s = std::string(kBeat) + "page 3.0 2\ndown 4.55 1 0.21 0.4\n";
        const double cols[4] = {0.21, 0.37, 0.29, 0.46};
        for (int b = 1; b < 4; ++b)
            s += "move " + std::to_string(4.55 + 2.0 * b) + " 1 " + std::to_string(cols[b]) + " 0.4\n";
        s += "key 12.6 Space\nup 12.7 1\nend 14\n";
        const auto take = testkit::play(s);
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 16);
    }

    void arrowsJustAfterSpaceSetTheLoopsLength() {
        // Over the beat (bars every 2 s from 4.5 s), six bars of bass that
        // never repeat, then Space: the last four loop. ↑ makes it the last
        // eight, ↓ ↓ ↓ two bars, then one; once the arrows rest it stays,
        // and one Ctrl+Z takes the whole take back.
        std::string s = std::string(kBeat) + "page 3.0 2\n";
        const double xs[6] = {0.21, 0.37, 0.29, 0.46, 0.54, 0.62};
        for (int b = 0; b < 6; ++b)
            for (int k = 0; k < 4; ++k)
                s += "tap " + std::to_string(4.55 + 2.0 * b + 0.5 * k) + " " + std::to_string(xs[(b + k) % 6]) + " 0.4\n";
        s += "key 16.6 Space\n";
        QCOMPARE(int(testkit::play(s + "end 17.5\n").view.parts[1].clips[0].lengthBeats), 16);
        QCOMPARE(int(testkit::play(s + "key 17.0 Up\nend 18\n").view.parts[1].clips[0].lengthBeats), 32);
        const std::string down = s + "key 17.0 Up\nkey 17.4 Down\nkey 17.8 Down\nkey 18.2 Down\n";
        const auto shortest = testkit::play(down + "end 19\n");
        QCOMPARE(shortest.view.parts[1].playing, 0);
        QCOMPARE(int(shortest.view.parts[1].clips[0].lengthBeats), 4);
        // Long after, the arrows are the page's again: the loop stays.
        QCOMPARE(int(testkit::play(down + "key 23.0 Up\nend 24\n").view.parts[1].clips[0].lengthBeats), 4);
        // One take, one step: Ctrl+Z leaves no bass, and the drums play on.
        const auto undone = testkit::play(down + "key 19.0 Ctrl+Z\nkey 19.5 Ctrl+Z\nend 20\n");
        QCOMPARE(undone.view.parts[1].playing, -1);
        QCOMPARE(undone.view.parts[0].playing, 0);
    }

    void theLastLoopStaysResizableUntilAnotherIsMade() {
        // A bass loop over the beat: long after the arrows' three seconds,
        // the window's Shorter / Longer still have it, on its own page only;
        // Ctrl+Z leaves nothing to resize.
        const std::string s = std::string(kBeat) + "page 3.0 2\n"
                              "tap 4.55 0.21 0.4\ntap 5.05 0.37 0.4\ntap 5.55 0.29 0.4\ntap 6.05 0.46 0.4\n"
                              "tap 6.55 0.54 0.4\ntap 7.05 0.62 0.4\nkey 7.6 Space\n";
        testkit::play(s + "end 15\n", [](Session &ses) {
            QCOMPARE(ses.loopBeats(0), 8);
            QCOMPARE(ses.loopBeats(-1), 4);
            QCOMPARE(ses.loopBeats(1), 16);
        });
        testkit::play(s + "page 9 1\nend 10\n", [](Session &ses) { QCOMPARE(ses.loopBeats(0), 0); });
        testkit::play(s + "key 9 Ctrl+Z\nend 10\n", [](Session &ses) { QCOMPARE(ses.loopBeats(0), 0); });
    }

    void theHandStepsAsideWhenSpaceMakesALoop() {
        // A bass bar over the beat, Space, and a tap a tenth of a second
        // later: the hand was still going, so it isn't heard over the loop
        // that just started. A tap half a second later is new playing.
        const std::string s = std::string(kBeat) + "page 3.0 2\n"
                              "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                              "key 6.6 Space\n";
        const auto without = testkit::play(s + "end 8\n");
        const auto early = testkit::play(s + "tap 6.7 0.9 0.5\nend 8\n");
        const auto later = testkit::play(s + "tap 7.1 0.9 0.5\nend 8\n");
        QVERIFY2(std::fabs(early.rms(6.7, 6.95) - without.rms(6.7, 6.95)) < 0.002,
                 qPrintable(QString("%1 vs %2").arg(early.rms(6.7, 6.95)).arg(without.rms(6.7, 6.95))));
        QVERIFY(later.rms(7.1, 7.35) > without.rms(7.1, 7.35) + 0.005);
    }

    void aWaitBeforeSpaceKeepsTheTempo() {
        // Nothing playing: a few chords tried at no steady pace, then a long
        // wait, then Space. No pattern, not in the flow: the tempo stays at
        // 124 and the playing loops in whole bars of it, four at most.
        const auto take = testkit::play("page 0 3\n"
                                        "tap 0.5 0.1 0.4 len=0.6\ntap 1.7 0.4 0.4 len=0.4\ntap 2.3 0.7 0.4 len=0.9\n"
                                        "tap 4.1 0.25 0.4 len=0.5\n"
                                        "key 9.0 Space\nend 11\n");
        QCOMPARE(int(std::lround(take.view.bpm)), 124);
        QCOMPARE(take.view.parts[2].playing, 0);
        QVERIFY(take.view.parts[2].clips[0].lengthBeats <= 16);
    }

    void startingOverSetsTheMixBack() {
        // Drums looped, then muted on the Song page with Q: Ctrl+N twice
        // clears the loop and unmutes them, so the next beat is heard.
        const auto take = testkit::play(std::string(kBeat) + "page 3.0 0\nkey 3.2 Q\nend 4.0\n");
        QVERIFY(take.view.parts[0].channel.value[Mute] > 0.5f);
        const auto over = testkit::play(std::string(kBeat) + "page 3.0 0\nkey 3.2 Q\n"
                                        "key 4.0 Ctrl+N\nkey 4.5 Ctrl+N\nend 6\n");
        QVERIFY(over.view.parts[0].channel.value[Mute] < 0.5f);
        QCOMPARE(over.view.parts[0].playing, -1);
    }

    void ctrlNTwiceStartsOver() {
        // Once only asks; twice clears every loop, and the next loop sets a
        // new tempo of its own.
        const auto once = testkit::play(std::string(kBeat) + "key 4.0 Ctrl+N\nend 5\n");
        QCOMPARE(once.view.parts[0].playing, 0);
        const auto take = testkit::play(std::string(kBeat) +
                                        "key 4.0 Ctrl+N\nkey 4.5 Ctrl+N\n"
                                        "tap 6.0 0.12 0.12\ntap 6.4 0.38 0.12\ntap 6.8 0.12 0.12\ntap 7.2 0.38 0.12\n"
                                        "key 7.6 Space\nend 9\n");
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(take.view.parts[0].clips[0].events, 4);
        QCOMPARE(int(std::lround(take.view.bpm)), 150);
        QVERIFY(take.rms(5.0, 5.9) < 0.002); // silence between clearing and the new loop
    }

    void aMelodyThatStartsMidBarLoopsWithoutAGap() {
        // Over the beat (bars every 2 s from 2.5 s), some noodling, then a
        // four-note riff that starts on beat 3 of a bar, played twice, then
        // Space. The loop is that riff, one bar long, cut where it repeats:
        // no empty beats in front, none of the noodling.
        std::string script = std::string(kBeat) + "page 3.0 2\n"
                             "tap 3.3 0.8 0.5\ntap 3.9 0.6 0.5\ntap 4.7 0.9 0.5\n";
        for (double cycle : {5.5, 7.5})
            for (double o : {0.0, 0.5, 1.0, 1.25})
                script += "tap " + std::to_string(cycle + o) + " " + (o == 0.0 ? "0.1" : o == 0.5 ? "0.3" : o == 1.0 ? "0.2" : "0.4") + " 0.5\n";
        script += "key 9.6 Space\nend 12\n";
        const auto take = testkit::play(script);
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 4);
        QCOMPARE(take.view.parts[1].clips[0].events, 8);
        QCOMPARE(take.view.parts[0].playing, 0);
    }

    void aTwoBarMelodyPlayedTwiceStaysTwoBars() {
        // Two bars alike in rhythm but not in their last note, played twice
        // over the beat: the loop is both bars, not the first one alone.
        std::string script = std::string(kBeat) + "page 3.0 2\n";
        const double offs[8] = {0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5};
        const char *xs[8] = {"0.1", "0.3", "0.2", "0.4", "0.1", "0.3", "0.2", "0.6"};
        for (double cycle : {4.5, 8.5})
            for (int i = 0; i < 8; ++i)
                script += "tap " + std::to_string(cycle + offs[i]) + " " + xs[i] + " 0.5\n";
        script += "key 12.6 Space\nend 15\n";
        const auto take = testkit::play(script);
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 8);
        QCOMPARE(take.view.parts[1].clips[0].events, 16);
    }

    void clipsCanBeCopiedLayeredAndChained() {
        // Two drum clips made straight in the studio: one bar of four hits,
        // two bars of two. Copy, layer, chain, and take the merge back.
        Studio studio;
        Session session(&studio);
        std::vector<float> buf(240 * 2);
        double t = 0;
        auto runTo = [&](double until) {
            while (t < until) {
                studio.render(buf.data(), 240);
                t += 240.0 / 48000.0;
            }
        };
        auto hit = [&](int pad) {
            StudioMsg m;
            m.kind = StudioMsg::ToPart;
            m.part = 0;
            m.cmd.type = Command::User;
            m.cmd.flags = Command::Quantize;
            m.cmd.i[0] = pad;
            m.cmd.t = t;
            studio.post(m);
        };
        auto post = [&](StudioMsg::Kind k, int slot, int value, float f, double d0 = 0, double d1 = 0) {
            StudioMsg m;
            m.kind = k;
            m.part = 0;
            m.slot = int8_t(slot);
            m.value = value;
            m.f = f;
            m.d0 = d0;
            m.d1 = d1;
            studio.post(m);
        };
        const double spb = 60.0 / 124.0;
        // Bar 1 (beats 4–8): four hits; bars 2–3 (beats 8–16): two hits.
        for (int i = 0; i < 4; ++i) {
            runTo((4 + i) * spb);
            hit(1);
        }
        runTo(9 * spb);
        hit(2);
        runTo(13 * spb);
        hit(3);
        runTo(16.2 * spb);
        post(StudioMsg::TakeBars, 0, 0, 0, 4, 8);
        runTo(16.4 * spb);
        post(StudioMsg::TakeBars, 1, 0, 0, 8, 16);
        runTo(17 * spb);
        studio.fetchView();
        QCOMPARE(studio.view().parts[0].clips[0].events, 4);
        QCOMPARE(studio.view().parts[0].clips[1].events, 2);
        post(StudioMsg::CopyClip, 0, 2, 0);
        post(StudioMsg::MergeClips, 2, 1, 0); // layer: 2 bars, 4 hits twice + 2
        post(StudioMsg::CopyClip, 0, 3, 0);
        post(StudioMsg::MergeClips, 3, 1, 1); // chain: 3 bars, 4 + 2
        runTo(17.2 * spb);
        studio.fetchView();
        const auto &c = studio.view().parts[0].clips;
        QCOMPARE(int(c[2].lengthBeats), 8);
        QCOMPARE(c[2].events, 10);
        QCOMPARE(int(c[3].lengthBeats), 12);
        QCOMPARE(c[3].events, 6);
        post(StudioMsg::UndoTake, 0, 0, 0); // takes back the chain: slot 3 is the copy again
        runTo(17.4 * spb);
        studio.fetchView();
        QCOMPARE(int(studio.view().parts[0].clips[3].lengthBeats), 4);
        QCOMPARE(studio.view().parts[0].clips[3].events, 4);
    }

    void spaceLayersOntoThePlayingLoop() {
        // A bar of bass looped, then a second line played over it twice
        // and Space: one loop with both lines, no new slot used. Ctrl+Z
        // takes the layer back off.
        const std::string base = std::string(kBeat) +
                                  "page 3.0 2\n"
                                  "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                  "key 6.6 Space\n"
                                  "tap 6.8 0.6 0.5\ntap 7.8 0.7 0.5\ntap 8.8 0.6 0.5\ntap 9.8 0.7 0.5\n";
        const auto take = testkit::play(base + "key 10.6 Space\nend 12\n");
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(take.view.parts[1].clips[0].events, 12);
        QVERIFY(take.view.parts[1].clips[1].state == ClipView::Empty);
        const auto undone = testkit::play(base + "key 10.6 Space\nkey 11.0 Ctrl+Z\nend 12\n");
        QCOMPARE(undone.view.parts[1].playing, 0);
        QCOMPARE(undone.view.parts[1].clips[0].events, 8);
    }

    // (Lead counts a note left ringing out as a voice, so this can't tell
    // it apart there: lead_tests checks its layers' lines directly.)
    void aLayerOnAMonoPartPlaysOnAVoiceOfItsOwn_data() {
        QTest::addColumn<int>("page");
        QTest::newRow("bass") << 2;
    }
    void aLayerOnAMonoPartPlaysOnAVoiceOfItsOwn() {
        // A bar that is one long note, then short notes layered over it: bass
        // and lead are mono, but each layer is a line of its own, so the long
        // note keeps sounding under the new ones (two voices at once) instead
        // of being cut off by them.
        QFETCH(int, page);
        const int part = page - 1;
        const std::string script = std::string(kBeat) +
                                   "page 3.0 " + std::to_string(page) + "\n"
                                   "tap 4.52 0.3 0.5 len=1.85\n"
                                   "key 6.6 Space\n"
                                   "tap 7.0 0.6 0.5 len=0.3\ntap 7.5 0.7 0.5 len=0.3\n"
                                   "tap 9.0 0.6 0.5 len=0.3\ntap 9.5 0.7 0.5 len=0.3\n"
                                   "key 10.0 Space\nend 15\n";
        int most = 0;
        const auto take = testkit::play(script, [&](Session &s) {
            if (s.view().time > 11.0)
                most = std::max(most, int(s.view().parts[part].voices));
        });
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.parts[part].playing, 0);
        QVERIFY(take.view.parts[part].clips[1].state == ClipView::Empty);
        QVERIFY2(most >= 2, qPrintable(QStringLiteral("at most %1 voice(s) at once").arg(most)));
    }

    void aLayerLandsWithoutCuttingTheLoop() {
        // A bar of lead that holds one long note, then a short note played
        // over it and Space part-way through the bar: the layer goes into
        // the playing loop without stopping it, so the long note is held on
        // to the end of the bar instead of being let go when the layer lands.
        // (The lead publishes each line's state: [0] is 1 while it is held.)
        const std::string script = std::string(kBeat) +
                                   "page 3.0 4\n"
                                   "tap 4.52 0.3 0.5 len=2.3\n"
                                   "key 6.6 Space\n"
                                   "tap 9.0 0.6 0.5 len=0.15\n"
                                   "key 9.4 Space\nend 10.6\n";
        bool heldBefore = true, heldAfter = true;
        int layered = -1;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            const float held = s.view().parts[3].data[16 * 1 + 0];
            if (t > 8.7 && t < 9.3)
                heldBefore = heldBefore && held == 1.0f;
            if (t > 9.6 && t < 10.3)
                heldAfter = heldAfter && held == 1.0f;
            if (t > 10.4)
                layered = s.view().parts[3].clips[0].events;
        });
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.parts[3].playing, 0);
        QVERIFY2(layered > 2, qPrintable(QStringLiteral("%1 events").arg(layered)));
        QVERIFY(heldBefore);
        QVERIFY2(heldAfter, "the loop's held note was let go when the layer landed");
    }

    void aLayerFitsTheLoopItGoesOnto() {
        // A four-bar bass loop (one note a bar, played once), then three
        // notes over three bars layered on it: the layer is taken as four
        // bars, so it plays once per loop. Taken as three, it would repeat
        // within the loop, out of step with it, and be cut where it wraps.
        const std::string script = std::string(kBeat) +
                                   "page 3.0 2\n"
                                   "tap 4.55 0.1 0.5\ntap 6.55 0.3 0.5\ntap 8.55 0.2 0.5\ntap 10.55 0.4 0.5\n"
                                   "key 12.4 Space\n"
                                   "tap 12.8 0.6 0.5\ntap 14.8 0.7 0.5\ntap 16.8 0.6 0.5\n"
                                   "key 17.4 Space\nend 19\n";
        const auto take = testkit::play(script);
        QVERIFY(take.error.empty());
        const auto &c = take.view.parts[1].clips[0];
        QCOMPARE(int(c.lengthBeats), 16);
        QCOMPARE(c.events, 8 + 6);
    }

    void shiftSpaceMakesANewLoopInstead() {
        // The same, with Shift+Space: the second line alone becomes a new
        // loop in the next slot, in the first one's place.
        const std::string base = std::string(kBeat) +
                                  "page 3.0 2\n"
                                  "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                  "key 6.6 Space\n"
                                  "tap 6.8 0.6 0.5\ntap 7.8 0.7 0.5\ntap 8.8 0.6 0.5\ntap 9.8 0.7 0.5\n";
        const auto take = testkit::play(base + "key 10.6 Shift+Space\nend 12\n");
        QCOMPARE(take.view.parts[1].playing, 1);
        QCOMPARE(take.view.parts[1].clips[0].events, 8);
        QVERIFY(take.view.parts[1].clips[1].state == ClipView::Playing);
    }

    void spaceWithNothingPlayedMakesNoLoop() {
        const auto take = testkit::play("page 0 2\nkey 0.5 Space\nend 2\n");
        QCOMPARE(take.view.parts[1].playing, -1);
        QVERIFY(take.view.parts[1].clips[0].state == ClipView::Empty);
    }

    void aCapturedFirstLoopSetsTheTempo() {
        // Four hits half a second apart, captured from the first to the
        // fifth beat's moment: a 4-beat loop at 120 BPM.
        Studio studio;
        Session session(&studio);
        std::vector<float> buf(240 * 2);
        double t = 0;
        auto runTo = [&](double until) {
            while (t < until) {
                studio.render(buf.data(), 240);
                t += 240.0 / 48000.0;
            }
        };
        for (int i = 0; i < 4; ++i) {
            runTo(0.5 + 0.5 * i);
            StudioMsg m;
            m.kind = StudioMsg::ToPart;
            m.part = 0;
            m.cmd.type = Command::User;
            m.cmd.flags = Command::Quantize;
            m.cmd.t = t;
            studio.post(m);
        }
        runTo(2.8);
        StudioMsg c;
        c.kind = StudioMsg::Capture;
        c.part = 0;
        c.slot = 0;
        c.d0 = 0.5;
        c.d1 = 2.5;
        c.f = 0.85f;
        studio.post(c);
        runTo(3.2);
        studio.fetchView();
        QCOMPARE(int(std::lround(studio.view().bpm)), 120);
        QCOMPARE(studio.view().parts[0].playing, 0);
        QCOMPARE(int(studio.view().parts[0].clips[0].lengthBeats), 4);
    }

    // Enter pauses on every page; what Return was on a page is Shift+Enter,
    // and overdub is F10. The pages' chips say so: F10 turns overdub on on
    // Bass, Atmos and Arp (their chips once said ⇧⏎), and no chip or help
    // line anywhere shows a bare ⏎ or ↵, which would be the pause.
    void returnKeysDoWhatThePagesSay() {
        for (int page : {2, 5, 6}) {
            const auto f10 = testkit::play(std::string(kBeat) + "page 3 " + std::to_string(page) + "\nkey 3.4 F10\nend 4\n",
                                          [](Session &s) { QVERIFY(!s.paused()); });
            QVERIFY2(f10.view.parts[page - 1].overdub, qPrintable(QString::number(page)));
            testkit::play(std::string(kBeat) + "page 3 " + std::to_string(page) + "\nkey 3.4 Return\nend 4\n",
                          [](Session &s) { QVERIFY(s.paused()); });
        }
        testkit::play(std::string(kBeat) + "page 3 6\nkey 3.4 Shift+Return\nend 4\n", [](Session &s) {
            QVERIFY(!s.paused());
            QVERIFY(!s.view().parts[5].overdub);
            QVERIFY(s.notice(nullptr).startsWith(QStringLiteral("Hold")));
        });
        const QDir src(QStringLiteral(TENFOLD_SRC));
        const QRegularExpression bare(QStringLiteral("(?<!⇧)(?<!⇧ )[⏎↵]"));
        for (const QString &name : src.entryList({QStringLiteral("*.cpp")}, QDir::Files)) {
            if (name == QLatin1String("console.cpp")) // draws the glyphs themselves
                continue;
            QFile f(src.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i)
                QVERIFY2(!bare.match(lines[i]).hasMatch(), qPrintable(QString("%1:%2: a bare Return pauses: %3").arg(name).arg(i + 1).arg(lines[i].trimmed())));
        }
    }

    // A loop made while paused plays alone; pausing it and playing again
    // brings back the loops paused before it too, not only the new one.
    void aLoopMadeWhilePausedKeepsThePausedLoops() {
        const auto take = testkit::play(std::string(kBeat) +
                                        "key 3 Ctrl+P\npage 3.1 2\n"
                                        "tap 3.3 0.1 0.5\ntap 3.8 0.3 0.5\ntap 4.3 0.2 0.5\ntap 4.8 0.1 0.5\n"
                                        "key 5.3 Space\nkey 7 Ctrl+P\nkey 8 Ctrl+P\nend 10\n");
        QCOMPARE(take.view.parts[1].playing, 0);
        QCOMPARE(take.view.parts[0].playing, 0);
    }

    // A song saved while paused (or after Ctrl+Backspace) opens paused:
    // Ctrl+P plays the loops that were stopped. A slot launched while
    // paused takes down the "Paused" notice.
    void aSongSavedPausedOpensReadyToPlay() {
        for (const char *stop : {"key 3 Ctrl+P\n", "key 3 Ctrl+Backspace\n"}) {
            testkit::reopen(std::string(kBeat) + stop + "end 6\n", 1, [](Session &, Session &o, const QJsonObject &doc) {
                QCOMPARE(doc.value("parts").toArray().at(0).toObject().value("paused").toInt(-1), 0);
                QVERIFY(o.paused());
                o.key(KeyEvent{Qt::Key_P, Qt::ControlModifier, true});
                QCOMPARE(o.notice(nullptr), QStringLiteral("Playing"));
                const double t0 = scriptClock();
                for (int i = 0; i < 400 && o.view().parts[0].playing < 0; ++i) {
                    setScriptClock(t0 + i / 125.0);
                    o.refresh();
                    float buf[384 * 2];
                    o.studio()->render(buf, 384);
                }
                QCOMPARE(o.view().parts[0].playing, 0);
            });
        }
        testkit::play(std::string(kBeat) + "key 3 Ctrl+P\nkey 3.2 F1\nend 5\n", [](Session &s) {
            QCOMPARE(s.view().parts[0].playing, 0);
            QCOMPARE(s.notice(nullptr), QStringLiteral("Playing"));
        });
        // With loops in their slots but none paused, Ctrl+P doesn't say
        // there is nothing.
        testkit::play(std::string(kBeat) + "key 3 Ctrl+P\nkey 3.5 Ctrl+P\nkey 5 Backspace\nkey 7 Ctrl+P\nkey 7.2 Ctrl+P\nend 8\n", [](Session &s) {
            QVERIFY2(!s.notice(nullptr).startsWith(QStringLiteral("Nothing to play yet")), qPrintable(s.notice(nullptr)));
        });
    }

    // Shift+Space just after a loop takes only what was played since it
    // landed (the hits before are in that loop), and a loop is 1, 2, 4, 8
    // or 16 bars, never three.
    void aNewLoopTakesOnlyThePlayingSinceTheLoop() {
        const char *s = "page 0 1\ntap 0.5 0.05 0.5\ntap 1.0 0.25 0.5\ntap 1.5 0.05 0.5\ntap 2.0 0.25 0.5\nkey 2.52 Space\n"
                        "tap 3.1 0.55 0.5\ntap 3.6 0.75 0.5\ntap 4.1 0.55 0.5\ntap 4.6 0.75 0.5\nkey 5.12 Shift+Space\nend 8\n";
        const auto take = testkit::play(s);
        QCOMPARE(take.view.parts[0].playing, 1);
        QCOMPARE(take.view.parts[0].clips[1].events, 4);
        QCOMPARE(int(take.view.parts[0].clips[1].lengthBeats), 4);
        // Ten loops in a row, each Shift+Space over the last: every one a
        // power of two bars.
        std::string ten = "page 0 1\n";
        double t = 0.5;
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 4; ++k)
                ten += "tap " + std::to_string(t + 0.5 * k) + " " + std::to_string(0.05 + 0.2 * (k % 2)) + " 0.5\n";
            t += 2.0;
            ten += "key " + std::to_string(t + 0.02) + (i == 0 ? " Space\n" : " Shift+Space\n");
            t += 0.6;
        }
        const auto all = testkit::play(ten + "end " + std::to_string(t + 2) + "\n");
        for (const ClipView &c : all.view.parts[0].clips) {
            if (c.state == ClipView::Empty)
                continue;
            const int bars = int(std::lround(c.lengthBeats / 4));
            QVERIFY2(bars == 1 || bars == 2 || bars == 4 || bars == 8 || bars == 16, qPrintable(QString::number(c.lengthBeats)));
        }
    }

    void numberKeysChangePages() {
        int page = -1, afterZero = -1, afterTab = -1;
        testkit::play("key 0.1 3\nend 0.2\n", [&](Session &s) { page = s.page(); });
        QCOMPARE(page, 2);
        testkit::play("key 0.1 0\nend 0.2\n", [&](Session &s) { afterZero = s.page(); });
        testkit::play("key 0.1 7\nkey 0.2 Tab\nend 0.3\n", [&](Session &s) {
            afterTab = s.page();
            QCOMPARE(afterZero, s.arrangePage());
        });
        QCOMPARE(afterTab, 7);
    }

    void ctrlKeysMoveTempoAndKey() {
        const auto take = testkit::play("key 0.1 Ctrl+Up\nkey 0.2 Ctrl+Up\nkey 0.3 Ctrl+Right\nend 0.5\n");
        QCOMPARE(int(std::lround(take.view.bpm)), 126);
        QCOMPARE(take.view.root, (9 + 7) % 12);
    }

    void leavingAPageLetsGoOfHeldNotes() {
        // A bass note held while the page changes stops sounding.
        const auto take = testkit::play("page 0 2\ndown 0.2 1 0.3 0.5\nkey 0.8 3\nup 2.5 1\nend 2.5\n");
        QVERIFY(take.rms(0.4, 0.7) > 0.01);
        QVERIFY(take.rms(1.8, 2.4) < 0.002);
    }

    void stoppingWaitsForTheBar() {
        // F1/Backspace on the starter pages: stop at the next bar.
        const auto take = testkit::play(std::string(kBeat) + "key 4.2 Backspace\nend 7\n");
        QCOMPARE(take.view.parts[0].playing, -1);
        QVERIFY(take.view.parts[0].clips[0].state == ClipView::Stopped);
    }

    void muteSilencesAChannel() {
        Studio studio;
        Session session(&studio);
        StudioMsg m;
        m.kind = StudioMsg::SetChannel;
        m.part = 1;
        m.slot = Mute;
        m.f = 1;
        studio.post(m);
        StudioMsg on;
        on.kind = StudioMsg::ToPart;
        on.part = 1;
        on.cmd.type = Command::On;
        on.cmd.id = 1;
        studio.post(on);
        std::vector<float> buf(48000 * 2);
        studio.render(buf.data(), 48000);
        double s = 0;
        for (size_t i = 24000; i < buf.size(); ++i)
            s += double(buf[i]) * buf[i];
        QVERIFY(std::sqrt(s / double(buf.size() - 24000)) < 1e-4);
    }

    void armedRecordingMakesABarLongClip() {
        Studio studio;
        Session session(&studio);
        auto post = [&](StudioMsg::Kind k, int value = 0) {
            StudioMsg m;
            m.kind = k;
            m.part = 1;
            m.slot = 2;
            m.value = value;
            studio.post(m);
        };
        post(StudioMsg::Record, 4);
        std::vector<float> buf(512 * 2);
        double t = 0;
        auto run = [&](double seconds) {
            for (int i = 0; i < int(seconds * 48000 / 512); ++i) {
                studio.render(buf.data(), 512);
                t += 512.0 / 48000.0;
            }
        };
        run(0.1);
        for (int n = 0; n < 6; ++n) { // notes through the first bar
            StudioMsg on;
            on.kind = StudioMsg::ToPart;
            on.part = 1;
            on.cmd.type = Command::On;
            on.cmd.id = 10 + n;
            on.cmd.i[0] = n;
            studio.post(on);
            run(0.15);
            on.cmd.type = Command::Off;
            studio.post(on);
            run(0.2);
        }
        post(StudioMsg::StopRecord);
        run(2.5);
        studio.fetchView();
        const ClipView &c = studio.view().parts[1].clips[2];
        QCOMPARE(int(c.lengthBeats), 4);
        QVERIFY(c.events >= 2);
        QCOMPARE(studio.view().parts[1].playing, 2);
    }

    // Enter plays and pauses on every page; Shift+Enter is the page's own
    // (here the Drums' keep), and never pauses.
    void enterPlaysAndPausesOnEveryPage() {
        const std::string taps = "tap 0.5 0.12 0.12\ntap 1.0 0.12 0.12\ntap 1.5 0.12 0.12\ntap 2.0 0.12 0.12\nkey 2.5 Space\n";
        for (const char *page : {"1", "3", "6", "7", "0", "sampler"}) {
            const auto paused = testkit::play("page 0.05 1\n" + taps + "page 4.0 " + page + "\nkey 4.5 Return\nend 5.5\n");
            QVERIFY2(paused.view.parts[0].playing < 0, page);
            const auto again = testkit::play("page 0.05 1\n" + taps + "page 4.0 " + page + "\nkey 4.5 Return\nkey 5.0 Enter\nend 7\n");
            QVERIFY2(again.view.parts[0].playing >= 0, page);
            const auto shifted = testkit::play("page 0.05 1\n" + taps + "page 4.0 " + page + "\nkey 4.5 Shift+Return\nend 5.5\n");
            QVERIFY2(shifted.view.parts[0].playing >= 0, page);
        }
    }

    // New loop: the page's loop stops at once and stays in its slot, and
    // the next Space makes a loop of its own in another slot, not a layer.
    void newLoopStopsThePagesLoopAndTheNextSpaceMakesAnother() {
        const std::string taps = "tap 0.5 0.12 0.12\ntap 1.0 0.12 0.12\ntap 1.5 0.12 0.12\ntap 2.0 0.12 0.12\nkey 2.5 Space\n";
        testkit::play("page 0.05 1\n" + taps + "end 6\n", [](Session &s) {
            QVERIFY(s.canNewLoop());
            const int slot = s.view().parts[0].playing;
            QVERIFY(slot >= 0);
            QVERIFY(s.newLoop());
            QVERIFY(!s.canNewLoop());
            QCOMPARE(s.view().parts[0].playing, -1);
            QCOMPARE(int(s.view().parts[0].clips[slot].state) != int(ClipView::Empty), true);
            QVERIFY(!s.newLoop()); // nothing playing now
        });
        // Played on: the second Space is a loop in a slot of its own.
        int first = -1;
        bool asked = false;
        const auto take = testkit::watch("page 0.05 1\n" + taps +
                                             "tap 5.5 0.37 0.12\ntap 6.0 0.37 0.12\ntap 6.5 0.37 0.12\ntap 7.0 0.37 0.12\n"
                                             "key 7.5 Space\nend 11\n",
                                         [&](double t, Session &s) {
                                             if (t >= 5.0 && !asked) {
                                                 asked = true;
                                                 first = s.view().parts[0].playing;
                                                 QVERIFY(s.newLoop());
                                             }
                                         });
        QVERIFY(first >= 0);
        const PartView &pv = take.view.parts[0];
        QVERIFY(pv.playing >= 0);
        QVERIFY(pv.playing != first);
        QVERIFY(pv.clips[first].state != ClipView::Empty);
    }

    void everyPageDrawsWithoutTouches() {
        testkit::play("end 0.2\n", [](Session &s) {
            QImage img(1200, 800, QImage::Format_ARGB32);
            for (int p = 0; p < s.pageCount(); ++p) {
                s.setPage(p);
                QPainter painter(&img);
                s.paint(painter, QRectF(30, 14, 1100, 690), 0.2);
                QVERIFY(!s.pageHelp(p).isEmpty());
            }
        });
    }

    // A pad this user can't read is read through pkexec, which asks for the
    // password. Without pkexec, taking the pad says what's missing instead of
    // failing with a bare "not allowed". (A test everywhere, so the dev list
    // names a test on macOS too; there it has nothing to check.)
    void anUnreadablePadWithoutPkexecSaysWhatIsMissing() {
#ifndef __linux__
        QSKIP("the Linux trackpad's");
#else
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        NoFrames sink;
        TouchSource source(&sink, "/dev/input/event999");
        QVERIFY(source.needsPermission());
        source.start();
        qputenv("PATH", path);
        QCOMPARE(int(source.status()), int(Pad::Failed));
        QVERIFY(source.viaPassword());
        QVERIFY2(source.error().find("pkexec") != std::string::npos, source.error().c_str());
#endif
    }

    // Ctrl shortcuts are the Command key on macOS, and every label says so.
    void keysAreNamedAsTheSystemNamesThem() {
#ifdef Q_OS_MACOS
        QCOMPARE(console::keyNames(QStringLiteral("Ctrl M")), QStringLiteral("\u2318 M"));
        QCOMPARE(console::keyNames(QStringLiteral("Ctrl+Z takes back, CTRL N twice")),
                 QStringLiteral("\u2318Z takes back, \u2318 N twice"));
        QCOMPARE(console::keyNames(QStringLiteral("Control room")), QStringLiteral("Control room"));
#else
        QCOMPARE(console::keyNames(QStringLiteral("Ctrl+Z, Ctrl M")), QStringLiteral("Ctrl+Z, Ctrl M"));
#endif
    }

    void theBenchRunsWellInsideRealTime() {
        TENFOLD_TIMED();
        const RenderStats st = runBench(4.0);
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.5, qPrintable(QString::number(st.p99Load)));
    }
};

TESTKIT_REGISTER(FrameworkTests)
#include "framework_tests.moc"
