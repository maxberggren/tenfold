// The tutorial, walked through like a player would.

#include "testkit.h"

using namespace tf;

class TutorialTests : public QObject {
    Q_OBJECT
private slots:
    void startsOnDrums() {
        testkit::play("key 0.1 Ctrl+T\nend 1\n", [](Session &s) {
            QVERIFY(s.tutorial().active());
            QCOMPARE(s.tutorial().index(), 0);
        });
    }
};

TESTKIT_REGISTER(TutorialTests)
#include "tutorial_tests.moc"
