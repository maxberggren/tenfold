// The studio and the session: clock, clips, mixer, pages and keys.

#include <QImage>
#include <QPainter>

#include "testkit.h"

using namespace tf;

namespace {

// Kick, snare, kick, snare at 120 BPM on the drums page, then the next ONE
// held: a beat that loops.
const char *kBeat = "page 0 1\n"
                    "tap 0.5 0.05 0.5\ntap 1.0 0.25 0.5\ntap 1.5 0.05 0.5\ntap 2.0 0.25 0.5\n"
                    "down 2.5 1 0.05 0.5\nup 2.9 1\n";

}

class FrameworkTests : public QObject {
    Q_OBJECT
private slots:
    void theFirstLoopSetsTheTempoAndKeepsPlaying() {
        const auto take = testkit::play(std::string(kBeat) + "end 8\n");
        QVERIFY(take.error.empty());
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(int(take.view.parts[0].clips[0].lengthBeats), 4);
        // Long after the fingers left, the beat goes on.
        QVERIFY(take.rms(5, 8) > 0.01);
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void aSecondLoopKeepsTheTempo() {
        const auto take = testkit::play(std::string(kBeat) +
                                        "page 3 2\n"
                                        "tap 3.3 0.05 0.5\ntap 3.6 0.1 0.5\ntap 3.9 0.2 0.5\n"
                                        "down 4.2 2 0.05 0.5\nup 4.6 2\nend 7\n");
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        QCOMPARE(take.view.parts[1].playing, 0);
        QVERIFY(take.view.parts[1].clips[0].lengthBeats >= 1);
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
        const auto take = testkit::play(std::string(kBeat) + "key 3.2 Backspace\nend 6\n");
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
