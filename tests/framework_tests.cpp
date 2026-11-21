// The studio and the session: clock, clips, mixer, pages and keys.

#include <QImage>
#include <QPainter>

#include "testkit.h"

using namespace tf;

namespace {

// Kick, snare, kick, snare half a second apart on the drums page, then
// Space where the next kick would land: a one-bar loop at 120 BPM.
const char *kBeat = "page 0 1\n"
                    "tap 0.5 0.05 0.5\ntap 1.0 0.25 0.5\ntap 1.5 0.05 0.5\ntap 2.0 0.25 0.5\n"
                    "key 2.5 Space\n";

}

class FrameworkTests : public QObject {
    Q_OBJECT
private slots:
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

    void theBenchRunsWellInsideRealTime() {
        const RenderStats st = runBench(4.0);
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.5, qPrintable(QString::number(st.p99Load)));
    }
};

TESTKIT_REGISTER(FrameworkTests)
#include "framework_tests.moc"
