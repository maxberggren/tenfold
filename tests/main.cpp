// Runs every registered test class (the framework's and each part's).

#include <QGuiApplication>

#include "testkit.h"

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    int failed = 0;
    for (auto &run : testkit::registry())
        failed += run(argc, argv) != 0;
    return failed;
}
