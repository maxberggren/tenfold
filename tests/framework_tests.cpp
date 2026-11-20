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
        // A few stray pads while finding the groove, then kick-snare-kick-
        // snare twice, then Space on the next kick: the loop is the eight
        // hits that repeat (two bars at 120 BPM), not the noodling.
        const auto take = testkit::play("page 0 1\n"
                                        "tap 0.3 0.38 0.38\ntap 0.55 0.62 0.62\ntap 0.8 0.88 0.38\n"
                                        "tap 1.5 0.12 0.12\ntap 2.0 0.38 0.12\ntap 2.5 0.12 0.12\ntap 3.0 0.38 0.12\n"
                                        "tap 3.5 0.12 0.12\ntap 4.0 0.38 0.12\ntap 4.5 0.12 0.12\ntap 5.0 0.38 0.12\n"
                                        "key 5.5 Space\nend 8\n");
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(take.view.parts[0].clips[0].events, 8);
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
        // replaces the first. Ctrl+Z takes the second back and the first
        // plays again; Ctrl+Z again takes the first back too, and the beat
        // plays on.
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3.0 2\n"
                                        "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                                        "key 6.6 Space\n"
                                        "tap 6.55 0.6 0.5\ntap 7.05 0.8 0.5\ntap 7.55 0.7 0.5\ntap 8.05 0.6 0.5\n"
                                        "key 8.6 Space\n"
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
        QCOMPARE(twice.view.parts[0].playing, -1); // the second Ctrl+Z took the beat back
        QVERIFY(twice.view.parts[0].clips[0].state == ClipView::Empty);
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
