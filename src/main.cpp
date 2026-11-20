// tenfold — an instrument played with all ten fingers on the trackpad.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "backend.h"
#include "helper.h"
#include "offline.h"
#include "surface.h"
#include "theme.h"
#include "translator.h"

static void usage(FILE *out) {
    std::fputs("Usage:\n"
               "  tenfold [open]                          play (takes the trackpad on Space)\n"
               "  tenfold render <script.tf> <out.wav> [--json]\n"
               "                                          render a scripted performance\n"
               "  tenfold bench [--json]                  time a dense case\n"
               "  tenfold screenshot <out.png> [script.tf] [--at SECONDS] [--size WxH] [--page N] [--help-page]\n"
               "                                          draw the window mid-performance (page 1-7, 0 arrange)\n",
               out);
}

static bool hasFlag(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], flag) == 0)
            return true;
    return false;
}

static int report(const tf::RenderStats &s, bool json) {
    if (json)
        std::fputs(tf::statsJson(s).c_str(), stdout);
    else
        std::fputs(tf::statsText(s).c_str(), stdout);
    if (!s.error.empty()) {
        std::fprintf(stderr, "tenfold: %s\n", s.error.c_str());
        return 1;
    }
    return 0;
}

static const char *option(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return nullptr;
}

// Renders the real window offscreen, mid-performance: for the README, and for
// checking the screen without a trackpad or a display.
static int screenshot(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName("tenfold");
    QQuickStyle::setStyle("Material");
    Translator translator;
    app.installTranslator(&translator);
    qmlRegisterType<Surface>("Tenfold", 1, 0, "Surface");
    qmlRegisterUncreatableType<Backend>("Tenfold", 1, 0, "Backend", "provided by the app");
    // The default accent, so the picture doesn't depend on the current theme.
    Theme theme(QDir::tempPath() + "/tenfold-no-theme");
    Backend backend(false);

    std::string script = "end 0.5\n";
    if (argc >= 4 && argv[3][0] != '-') {
        std::ifstream f(argv[3]);
        std::stringstream ss;
        ss << f.rdbuf();
        script = ss.str();
    }
    const char *at = option(argc, argv, "--at");
    std::string error;
    if (!backend.simulate(script, at ? std::atof(at) : 1e9, &error)) {
        std::fprintf(stderr, "tenfold: %s\n", error.c_str());
        return 1;
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    int w = 1320, h = 900;
    if (const char *size = option(argc, argv, "--size"))
        std::sscanf(size, "%dx%d", &w, &h);
    window->resize(w, h);
    if (const char *page = option(argc, argv, "--page")) {
        const int n = std::atoi(page);
        backend.setPage(n == 0 ? backend.pageCount() - 1 : n - 1);
    }
    if (hasFlag(argc, argv, "--help-page"))
        window->setProperty("helpVisible", true);
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    const QImage image = window->grabWindow();
    if (!image.save(QString::fromLocal8Bit(argv[2]))) {
        std::fprintf(stderr, "tenfold: cannot write %s\n", argv[2]);
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // Runs as root under pkexec: no GUI, no Qt, no environment.
    if (argc == 3 && std::strcmp(argv[1], "helper") == 0)
        return runHelper(argv[2]);

    const bool json = hasFlag(argc, argv, "--json");
    if (argc >= 2 && std::strcmp(argv[1], "render") == 0) {
        if (argc < 4) {
            usage(stderr);
            return 1;
        }
        std::ifstream f(argv[2]);
        if (!f) {
            std::fprintf(stderr, "tenfold: cannot read %s\n", argv[2]);
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return report(tf::renderScript(ss.str(), argv[3]), json);
    }
    if (argc >= 2 && std::strcmp(argv[1], "bench") == 0)
        return report(tf::runBench(8.0), json);
    if (argc >= 3 && std::strcmp(argv[1], "screenshot") == 0)
        return screenshot(argc, argv);
    if (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "open") != 0) {
        usage(stderr);
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("tenfold");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("tenfold");
    app.setWindowIcon(QIcon::fromTheme("tenfold"));
    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);

    qmlRegisterType<Surface>("Tenfold", 1, 0, "Surface");
    qmlRegisterUncreatableType<Backend>("Tenfold", 1, 0, "Backend", "provided by the app");

    Theme theme;
    Backend backend;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &Backend::shutdown);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
