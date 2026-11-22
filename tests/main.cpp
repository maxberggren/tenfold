// Runs every registered test class (the framework's and each part's).

#include <QGuiApplication>
#include <cstring>

#include "console.h"
#include "dsp.h"
#include "testkit.h"

int main(int argc, char **argv) {
    // bin/test asks for the list of tests and runs them in parallel.
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        testkit::list();
        return 0;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    tf::console::loadFonts();
    // The tests render on this thread, which flushes denormals as the audio
    // thread does (GCC's fast maths does it for the whole program, Clang's
    // doesn't).
    tf::flushDenormals();
    int failed = 0;
    for (auto &suite : testkit::registry())
        failed += suite.run(argc, argv) != 0;
    return failed;
}
