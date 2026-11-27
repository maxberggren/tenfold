// The newcomer's tenfold: the sound browser, the LOOP button, the styles a
// first start offers, and the levels the window unfolds through.

#include <QtTest>

#include "console.h"
#include "session.h"
#include "testkit.h"

using namespace tf;

namespace {

const char *kSoundPages[] = {"drums", "bass", "chords", "lead", "arp", "atmos", "fx"};

}

class NewcomerTests : public QObject {
    Q_OBJECT
    Q_CLASSINFO("dev",
                "everyInstrumentPageListsItsSoundsAndChoosesThem aSoundTriedIsHeardAndLeftOutOfTheNextLoop "
                "theLoopButtonSaysWhatSpaceWouldDo pressingLoopIsSpace everyStyleNamesRealSounds "
                "aStyleSetsTheSoundsAndTheTempo theNextLoopGoesInTheFirstEmptySlot "
                "levelZeroFoldsTheSecondRowOnInstrumentPagesOnly theNextStepFollowsThePlayer "
                "coveredControlsIgnoreFingersAndTheMouseOpensStudio")
private slots:
    // Each instrument page lists its sounds, says which is chosen, and
    // chooses one as ← → would.
    void everyInstrumentPageListsItsSoundsAndChoosesThem() {
        testkit::play("end 0.1\n", [](Session &s) {
            for (const char *name : kSoundPages) {
                const int page = s.pageNamed(QString::fromLatin1(name));
                QVERIFY2(page >= 0, name);
                s.setPage(page);
                const auto choices = s.soundChoices();
                QVERIFY2(choices.size() >= 10, name);
                for (const auto &c : choices) {
                    QVERIFY2(!c.name.isEmpty(), name);
                    QVERIFY2(!c.what.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(name, c.name)));
                }
                const int last = int(choices.size()) - 1;
                s.chooseSound(last, false);
                QCOMPARE(s.soundChoice(), last);
                s.chooseSound(2, false);
                QCOMPARE(s.soundChoice(), 2);
                // Out of range changes nothing.
                s.chooseSound(int(choices.size()), false);
                QCOMPARE(s.soundChoice(), 2);
            }
            // The Song page and the picture have no sounds to browse.
            s.setPage(s.arrangePage());
            QVERIFY(s.soundChoices().empty());
            QCOMPARE(s.soundChoice(), -1);
        });
    }

    // A sound tried in the browser plays a short phrase at once; when it is
    // over nothing is left sounding, and Space then has nothing to loop.
    void aSoundTriedIsHeardAndLeftOutOfTheNextLoop() {
        for (const char *name : {"drums", "bass", "lead", "arp", "chords"}) {
            bool tried = false, heard = false, during = false;
            Session::LoopState after = Session::LoopState::None;
            const std::string script = std::string("page 0 ") + name + "\nkey 6.0 Space\nend 7\n";
            const auto take = testkit::watch(script, [&](double t, Session &s) {
                s.idle(t);
                if (!tried && t >= 0.5) {
                    tried = true;
                    s.chooseSound(1, true);
                }
                if (tried && t < 4.0) {
                    heard |= s.view().parts[s.page()].voices > 0;
                    during |= s.auditioning();
                }
                if (t >= 5.5 && t < 5.6)
                    after = s.loopState();
            });
            QVERIFY2(take.error.empty(), take.error.c_str());
            QVERIFY2(during, name);
            QVERIFY2(heard, name);
            QVERIFY2(take.rms(0.5, 3.0) > 0.001, name);
            QVERIFY2(after == Session::LoopState::Empty, name);
            QVERIFY2(take.view.parts[0].playing < 0 && take.view.parts[take.view.parts[1].playing >= 0 ? 1 : 0].playing < 0,
                     name);
            QVERIFY(take.finite());
        }
    }

    // Before anything is played, LOOP waits for playing; after a beat, it
    // loops it; with the loop going, it layers.
    void theLoopButtonSaysWhatSpaceWouldDo() {
        std::vector<std::pair<double, Session::LoopState>> seen;
        testkit::watch("page 0 1\n"
                       "tap 1.0 0.05 0.5\ntap 1.5 0.25 0.5\ntap 2.0 0.05 0.5\ntap 2.5 0.25 0.5\n"
                       "key 3.0 Space\nend 7\n",
                       [&](double t, Session &s) {
                           for (double at : {0.5, 2.8, 6.5})
                               if (t >= at && t < at + 0.01)
                                   seen.push_back({at, s.loopState()});
                       });
        QCOMPARE(int(seen.size()), 3);
        QVERIFY(seen[0].second == Session::LoopState::Empty);
        QVERIFY(seen[1].second == Session::LoopState::Make);
        QVERIFY(seen[2].second == Session::LoopState::Layer);
        testkit::play("end 0.1\n", [](Session &s) {
            s.setPage(s.arrangePage());
            QVERIFY(s.loopState() == Session::LoopState::None);
        });
    }

    // The button is Space: the same loop comes of it.
    void pressingLoopIsSpace() {
        bool pressed = false;
        const auto take = testkit::watch("page 0 1\n"
                                         "tap 0.5 0.05 0.5\ntap 1.0 0.25 0.5\ntap 1.5 0.05 0.5\ntap 2.0 0.25 0.5\n"
                                         "end 8\n",
                                         [&](double t, Session &s) {
                                             if (!pressed && t >= 2.5) {
                                                 pressed = true;
                                                 s.pressLoop();
                                             }
                                         });
        QCOMPARE(take.view.parts[0].playing, 0);
        QCOMPARE(int(take.view.parts[0].clips[0].lengthBeats), 4);
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
    }

    // A style names a sound for each page; every one must be there, or the
    // page would keep whatever it had.
    void everyStyleNamesRealSounds() {
        testkit::play("end 0.1\n", [](Session &s) {
            QVERIFY(Session::styles().size() >= 6);
            for (const auto &style : Session::styles()) {
                for (int i = 0; i < 7; ++i) {
                    s.setPage(s.pageNamed(QString::fromLatin1(kSoundPages[i])));
                    bool found = false;
                    for (const auto &c : s.soundChoices())
                        found |= c.name.compare(QString::fromUtf8(style.sounds[i]), Qt::CaseInsensitive) == 0;
                    QVERIFY2(found, qPrintable(QStringLiteral("%1 has no %2 on %3")
                                                   .arg(QString::fromUtf8(style.name), QString::fromUtf8(style.sounds[i]),
                                                        QString::fromLatin1(kSoundPages[i]))));
                }
                QVERIFY(style.bpm >= 60 && style.bpm <= 200);
            }
        });
    }

    void aStyleSetsTheSoundsAndTheTempo() {
        testkit::watch("end 1\n", [](double t, Session &s) {
            if (t < 0.2 || t > 0.21)
                return;
            int trap = -1;
            for (int i = 0; i < int(Session::styles().size()); ++i)
                if (QLatin1String(Session::styles()[size_t(i)].name) == QLatin1String("Trap"))
                    trap = i;
            QVERIFY(trap >= 0);
            QVERIFY(s.applyStyle(trap));
            const int page = s.page();
            s.setPage(s.pageNamed(QStringLiteral("drums")));
            QCOMPARE(s.soundChoices()[size_t(s.soundChoice())].name, QStringLiteral("trap"));
            s.setPage(s.pageNamed(QStringLiteral("bass")));
            QCOMPARE(s.soundChoices()[size_t(s.soundChoice())].name, QStringLiteral("808"));
            s.setPage(page);
            QVERIFY(!s.applyStyle(-1));
        });
        const auto take = testkit::watch("end 1\n", [](double t, Session &s) {
            if (t >= 0.2 && t < 0.21)
                s.applyStyle(1); // Trap, 140
        });
        QCOMPARE(int(std::lround(take.view.bpm)), 140);
    }

    // The clip strip's "+": the first empty slot.
    void theNextLoopGoesInTheFirstEmptySlot() {
        PartView v;
        QCOMPARE(console::firstEmptySlot(v), 0);
        v.clips[0].state = ClipView::Playing;
        v.clips[1].state = ClipView::Stopped;
        QCOMPARE(console::firstEmptySlot(v), 2);
        for (auto &c : v.clips)
            c.state = ClipView::Stopped;
        QCOMPARE(console::firstEmptySlot(v), -1);
    }

    void levelZeroFoldsTheSecondRowOnInstrumentPagesOnly() {
        testkit::play("end 0.1\n", [](Session &s) {
            const QRectF pad(30, 40, 800, 500);
            s.setLevel(0);
            for (const char *name : kSoundPages) {
                s.setPage(s.pageNamed(QString::fromLatin1(name)));
                const QRectF fold = s.foldedRow(pad);
                QVERIFY2(!fold.isEmpty(), name);
                QVERIFY(fold.top() > pad.bottom());
            }
            s.setPage(s.arrangePage());
            QVERIFY(s.foldedRow(pad).isEmpty());
            s.setPage(0);
            s.setLevel(1);
            QVERIFY(s.foldedRow(pad).isEmpty());
            s.setLevel(7);
            QCOMPARE(s.level(), 2);
        });
    }

    void theNextStepFollowsThePlayer() {
        QString before, playing, looped;
        testkit::watch("page 0 1\n"
                       "tap 1.0 0.05 0.5\ntap 1.5 0.25 0.5\ntap 2.0 0.05 0.5\ntap 2.5 0.25 0.5\n"
                       "key 3.0 Space\nend 7\n",
                       [&](double t, Session &s) {
                           if (t >= 0.5 && t < 0.51)
                               before = s.nextStep();
                           if (t >= 2.8 && t < 2.81)
                               playing = s.nextStep();
                           if (t >= 6.5 && t < 6.51)
                               looped = s.nextStep();
                       });
        QVERIFY(before.contains(QLatin1String("Z X C V")) && before.startsWith(QLatin1String("Your turn")));
        QVERIFY(playing.contains(QLatin1String("LOOP")));
        QVERIFY(looped.contains(QLatin1String("BASS")));
    }

    // Below Studio a page with fine-tuning on its pad (the bass's rail) is
    // zoomed onto its play area: a finger near the top plays a note there,
    // where at Studio it would land on the rail.
    void coveredControlsIgnoreFingersAndTheMouseOpensStudio() {
        testkit::play("end 0.1\n", [](Session &s) {
            for (const char *name : {"bass", "chords", "arp"}) {
                s.setPage(s.pageNamed(QString::fromLatin1(name)));
                s.setLevel(0);
                QVERIFY2(s.zoomArea().zooms(), name);
                s.setLevel(1);
                QVERIFY2(s.zoomArea().zooms(), name);
                s.setLevel(2);
                QVERIFY2(!s.zoomArea().zooms(), name);
            }
            s.setPage(s.pageNamed(QStringLiteral("drums")));
            s.setLevel(0);
            QVERIFY(!s.zoomArea().zooms());
        });
        for (int level : {0, 2}) {
            int voices = -1;
            testkit::watch("page 0 bass\ndown 0.5 1 0.5 0.93\nup 1.2 1\nend 1.5\n", [&](double t, Session &s) {
                if (t < 0.2)
                    s.setLevel(level);
                if (t >= 0.9 && voices < 0)
                    voices = s.view().parts[1].voices;
            });
            if (level == 0)
                QVERIFY2(voices > 0, "zoomed: the top of the pad plays");
            else
                QVERIFY2(voices == 0, "Studio: the top of the pad is the rail");
        }
    }
};

TESTKIT_REGISTER(NewcomerTests)
#include "newcomer_tests.moc"
