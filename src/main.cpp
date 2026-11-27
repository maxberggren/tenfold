// tenfold — an instrument played with all ten fingers on the trackpad.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QFileOpenEvent>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "backend.h"
#include "console.h"
#include "platform.h"
#include "offline.h"
#include "session.h"
#include "songfile.h"
#include "studio.h"
#include "surface.h"
#include "theme.h"
#include "translator.h"
#include "visualizer.h"

// A song file opened from the Finder (or dropped on the Dock icon) comes
// as an event, not an argument.
class SongOpener : public QObject {
public:
    explicit SongOpener(Backend *backend) : m_backend(backend) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            m_backend->openSongFile(static_cast<QFileOpenEvent *>(event)->file());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    Backend *m_backend;
};

static void usage(FILE *out) {
    std::fputs("Usage:\n"
               "  tenfold [open]                          play (its button takes the trackpad)\n"
               "  tenfold render <script.tf> <out.wav> [--json]\n"
               "                                          render a scripted performance\n"
               "  tenfold bench [--json]                  time a dense case\n"
               "  tenfold latency [--json]                measure the microphone's trip to the ears\n"
               "  tenfold screenshot <out.png> [script.tf] [--at SECONDS] [--size WxH] [--page N] [--help-page] [--level 0-2] [--welcome] [--sounds N] [--notice TEXT]\n"
               "                     [--tutorial STEP] [--gpu] [--window] [--pad standby|waiting|paused|fault]\n"
               "                     [--export]\n"
               "                                          draw the window mid-performance (page 1-8, 0 arrange, 9 visual, sampler),\n"
               "                                          with the tutorial on that step (from 1) if asked; --gpu\n"
               "                                          on the desktop's GPU (the Visual page's picture needs\n"
               "                                          it), --window with the visualizer's window too,\n"
               "                                          --pad with the trackpad not taken,\n"
               "                                          --export with the export dialog open\n"
               "  tenfold check <song.tenfold>            say whether a song file opens, and what is in it\n"
               "  tenfold visualize <script.tf> <out.mp4|out.png> [--at SECONDS] [--from S] [--to S]\n"
               "                    [--fps N] [--size WxH] [--quality 0-3]\n"
               "                                          the visualizer drawn as the script plays: a video with\n"
               "                                          its sound (needs ffmpeg), or the frame at --at (needs a\n"
               "                                          display with OpenGL, Vulkan or Metal)\n",
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
    // --gpu: on the desktop's own platform, with its GPU, for the Visual
    // page's picture (the offscreen platform has no GPU: the picture stays
    // black there). The window shows for the moment it takes.
    const bool gpu = hasFlag(argc, argv, "--gpu");
    if (!gpu)
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    tf::console::loadFonts();
    app.setApplicationName("tenfold");
    QQuickStyle::setStyle("Material");
    Translator translator;
    app.installTranslator(&translator);
    qmlRegisterType<Surface>("Tenfold", 1, 0, "Surface");
    qmlRegisterUncreatableType<Backend>("Tenfold", 1, 0, "Backend", "provided by the app");
    qmlRegisterType<VisualData>("Tenfold", 1, 0, "VisualData");
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
    // The visual page's picture needs a GPU, which the offscreen platform
    // hasn't: it stays black here (tenfold visualize draws it).
    Visualizer visual(backend.session());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("visual", &visual);
    backend.setVisualizer(&visual, &engine);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    int w = 1320, h = 900;
    if (const char *size = option(argc, argv, "--size"))
        std::sscanf(size, "%dx%d", &w, &h);
    window->resize(w, h);
    if (const char *step = option(argc, argv, "--tutorial")) {
        tf::Session &s = *backend.session();
        std::lock_guard lock(s.lock());
        s.tutorial().jump(std::atoi(step) - 1, s);
    }
    if (const char *page = option(argc, argv, "--page")) {
        // By its key (1–8, 0, 9) or its name (sampler).
        tf::Session &s = *backend.session();
        if (const int n = s.pageNamed(QString::fromLocal8Bit(page)); n >= 0)
            backend.setPage(n);
    }
    // --pad: the window as it is with the pad not taken (a screenshot plays
    // as if it were).
    if (const char *pad = option(argc, argv, "--pad")) {
        const std::string p = pad;
        backend.simulatePad(p == "waiting" ? Backend::Starting : p == "paused" ? Backend::Ready
                            : p == "fault" ? Backend::Failed : p == "playing" ? Backend::Playing : Backend::Idle);
    }
    // --level 0|1|2: the window as a newcomer (0), shaping (1) or the whole
    // studio (2, as before levels); --welcome: the first start's styles.
    if (const char *level = option(argc, argv, "--level"))
        backend.setUiLevel(std::atoi(level));
    if (hasFlag(argc, argv, "--welcome"))
        backend.simulateWelcome(true);
    if (const char *sounds = option(argc, argv, "--sounds")) {
        // The sound browser open, the n-th sound tried.
        window->setProperty("soundsVisible", true);
        backend.chooseSound(std::atoi(sounds), false);
    }
    // --notice TEXT: that notice over the pad, held up for the picture.
    if (const char *notice = option(argc, argv, "--notice")) {
        backend.session()->say(QString::fromUtf8(notice));
        window->setProperty("pinNotice", true);
    }
    if (hasFlag(argc, argv, "--file-menu"))
        window->setProperty("showFileMenu", true);
    if (hasFlag(argc, argv, "--help-page"))
        window->setProperty("helpVisible", true);
    if (hasFlag(argc, argv, "--export"))
        window->setProperty("exportVisible", true);
    // --window: the visualizer's own window too, saved beside as …-window.png.
    const bool second = hasFlag(argc, argv, "--window");
    if (second)
        emit visual.windowAsked();
    // Long enough for the picture's echoes to build up with --gpu.
    for (int i = 0; i < (gpu ? 120 : 20); ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    const QImage image = window->grabWindow();
    if (!image.save(QString::fromLocal8Bit(argv[2]))) {
        std::fprintf(stderr, "tenfold: cannot write %s\n", argv[2]);
        return 1;
    }
    if (second) {
        for (QWindow *w : QGuiApplication::allWindows())
            if (w != window && w->isVisible())
                if (auto *q = qobject_cast<QQuickWindow *>(w)) {
                    QString path = QString::fromLocal8Bit(argv[2]);
                    path.insert(path.lastIndexOf(QLatin1Char('.')), QStringLiteral("-window"));
                    q->grabWindow().save(path);
                }
    }
    return 0;
}

// Opens a song file the way Ctrl+O does, into a studio with no sound card,
// and says what is in it or why it won't open.
static int check(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QString path = QString::fromLocal8Bit(argv[2]);
    QJsonObject doc;
    QString error;
    bool ok = tf::readSongFile(path, &doc, &error);
    if (ok) {
        tf::Studio studio(48000.0f);
        tf::Session session(&studio);
        ok = session.openSong(doc, &error);
    }
    if (!ok) {
        std::fprintf(stderr, "%s: %s\n", argv[2], qPrintable(error));
        return 1;
    }
    std::printf("%s: %s\n", argv[2], qPrintable(tf::songSummary(doc)));
    if (!error.isEmpty())
        std::printf("  (%s)\n", qPrintable(error));
    return 0;
}

int main(int argc, char *argv[]) {
    // The system's own commands (the Linux trackpad helper, run as root)
    // come first: they must not start any of Qt.
    if (const int code = tf::platform::runSystemCommand(argc, argv); code >= 0)
        return code;

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
    if (argc == 3 && std::strcmp(argv[1], "check") == 0)
        return check(argc, argv);
    if (argc >= 4 && std::strcmp(argv[1], "visualize") == 0) {
        QGuiApplication app(argc, argv);
        app.setApplicationName("tenfold");
        qmlRegisterType<VisualData>("Tenfold", 1, 0, "VisualData");
        tf::VisualizeOptions o;
        o.script = argv[2];
        o.out = argv[3];
        if (const char *v = option(argc, argv, "--at"))
            o.at = std::atof(v);
        if (const char *v = option(argc, argv, "--from"))
            o.from = std::atof(v);
        if (const char *v = option(argc, argv, "--to"))
            o.to = std::atof(v);
        if (const char *v = option(argc, argv, "--fps"))
            o.fps = std::clamp(std::atoi(v), 1, 120);
        if (const char *v = option(argc, argv, "--size"))
            std::sscanf(v, "%dx%d", &o.width, &o.height);
        if (const char *v = option(argc, argv, "--quality"))
            o.quality = std::atoi(v);
        return tf::visualize(o);
    }
    if (argc >= 2 && std::strcmp(argv[1], "latency") == 0) {
        QCoreApplication app(argc, argv);
        return tf::platform::measureLatency(json);
    }
    if (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    // Launched from the Finder, older macOS adds -psn_… for the process.
    if (argc >= 2 && std::strcmp(argv[1], "open") != 0 && std::strncmp(argv[1], "-psn_", 5) != 0) {
        usage(stderr);
        return 1;
    }

    QGuiApplication app(argc, argv);
    tf::console::loadFonts();
    app.setApplicationName("tenfold");
    // Settings (Play or Studio, the first start seen) in
    // ~/.config/tenfold/tenfold.conf, not under "Unknown Organization".
    app.setOrganizationName("tenfold");
    app.setApplicationVersion("0.5.0");
    app.setDesktopFileName("tenfold");
    app.setWindowIcon(QIcon::fromTheme("tenfold"));
    QQuickStyle::setStyle("Material");

    Translator translator;
    app.installTranslator(&translator);

    qmlRegisterType<Surface>("Tenfold", 1, 0, "Surface");
    qmlRegisterUncreatableType<Backend>("Tenfold", 1, 0, "Backend", "provided by the app");
    qmlRegisterType<VisualData>("Tenfold", 1, 0, "VisualData");

    Theme theme;
    Backend backend;
    Visualizer visual(backend.session());
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, &Backend::shutdown);
    SongOpener opener(&backend);
    app.installEventFilter(&opener);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("visual", &visual);
    backend.setVisualizer(&visual, &engine);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
