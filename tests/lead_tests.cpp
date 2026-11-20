// The Lead page.

#include "testkit.h"

using namespace tf;

class LeadTests : public QObject {
    Q_OBJECT
private slots:
    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 4\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }
};

TESTKIT_REGISTER(LeadTests)
#include "lead_tests.moc"
