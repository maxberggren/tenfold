#pragma once

// Helpers for testing pages: play a script through the real session and
// studio and look at the sound and the studio's view afterwards.

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QObject>
#include <QtTest>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "offline.h"
#include "session.h"
#include "songfile.h"
#include "studio.h"

namespace testkit {

// The tests that time the studio against the clock measure the machine as
// much as the code. On a shared virtual machine (the macOS CI runner, three
// cores among other jobs) a scheduling hiccup reads as a slow render, so
// TENFOLD_SKIP_TIMING=1 there skips them, visibly, with the reason. The
// job runs `tenfold bench` instead and prints what it measures.
#define TENFOLD_TIMED()                                                                                     \
    do {                                                                                                    \
        if (qEnvironmentVariableIsSet("TENFOLD_SKIP_TIMING"))                                               \
            QSKIP("timing on a shared virtual machine (TENFOLD_SKIP_TIMING); see the job's bench instead"); \
    } while (0)

struct Take {
    std::vector<float> left, right; // the mix, 48 kHz
    tf::StudioView view;            // the studio at the end
    std::string error;
    double rms(double from = 0, double to = 1e9) const {
        const size_t a = size_t(std::max(0.0, from) * 48000), b = std::min(left.size(), size_t(to * 48000));
        double s = 0;
        for (size_t i = a; i < b; ++i)
            s += double(left[i]) * left[i] + double(right[i]) * right[i];
        return b > a ? std::sqrt(s / double(2 * (b - a))) : 0;
    }
    double peak() const {
        double p = 0;
        for (size_t i = 0; i < left.size(); ++i)
            p = std::max({p, double(std::fabs(left[i])), double(std::fabs(right[i]))});
        return p;
    }
    bool finite() const {
        for (size_t i = 0; i < left.size(); ++i)
            if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
                return false;
        return true;
    }
};

// Plays a script (see offline.h) and returns what came out. inspect, if
// given, is called with the session at the end, before it is destroyed.
inline Take play(const std::string &script, const std::function<void(tf::Session &)> &inspect = {}) {
    Take take;
    tf::ScriptPlayer player;
    if (!player.load(script, &take.error))
        return take;
    tf::Studio studio(48000.0f);
    studio.setTimeOrigin(0);
    tf::setScriptClock(0);
    tf::Session session(&studio);
    session.setClock(&tf::scriptClock);
    const double end = player.end() + 0.01;
    float buf[384 * 2];
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > end)
            break;
        session.refresh();
        player.tick(t, session);
        studio.render(buf, 384);
        for (int i = 0; i < 384; ++i) {
            take.left.push_back(buf[2 * i]);
            take.right.push_back(buf[2 * i + 1]);
        }
    }
    session.refresh();
    take.view = session.view();
    if (inspect)
        inspect(session);
    return take;
}

// Plays a script like testkit::play, and calls fn with the session after
// every tick, so a test can follow the studio bar by bar.
inline Take watch(const std::string &script, const std::function<void(double, tf::Session &)> &fn) {
    Take take;
    tf::ScriptPlayer player;
    if (!player.load(script, &take.error))
        return take;
    tf::Studio studio(48000.0f);
    studio.setTimeOrigin(0);
    tf::setScriptClock(0);
    tf::Session session(&studio);
    session.setClock(&tf::scriptClock);
    const double end = player.end() + 0.01;
    float buf[384 * 2];
    uint64_t lastSample = ~0ull;
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > end)
            break;
        session.refresh();
        player.tick(t, session);
        studio.render(buf, 384);
        for (int i = 0; i < 384; ++i) {
            take.left.push_back(buf[2 * i]);
            take.right.push_back(buf[2 * i + 1]);
        }
        session.refresh();
        // Only a view the studio has just published: a page's own changes
        // show in the session's view at once, before the studio applies them
        // (a mute timed for the bar line shows a sixteenth early).
        if (fn && session.view().sample != lastSample) {
            lastSample = session.view().sample;
            fn(t, session);
        }
    }
    take.view = session.view();
    return take;
}

// Plays a script, saves the song at its end (as Ctrl+S would), opens it in a
// fresh studio and session (as Ctrl+O would, in a new tenfold), plays that
// on for `after` seconds, and hands both sessions to `check`: how a page
// tests that what it saves comes back. The saved JSON is in `doc`.
inline Take reopen(const std::string &script, double after,
                   const std::function<void(tf::Session &before, tf::Session &opened, const QJsonObject &doc)> &check) {
    return play(script, [&](tf::Session &before) {
        QString error;
        const QJsonObject doc = before.saveSong(&error);
        if (doc.isEmpty()) {
            QFAIL(qPrintable(QStringLiteral("saveSong: ") + error));
            return;
        }
        // Through the file's own bytes, so what is checked is what a file holds.
        QJsonParseError pe;
        const QJsonObject read = QJsonDocument::fromJson(tf::songJson(doc), &pe).object();
        QVERIFY2(pe.error == QJsonParseError::NoError, qPrintable(pe.errorString()));
        const double t0 = tf::scriptClock();
        tf::Studio studio(48000.0f);
        studio.setTimeOrigin(t0);
        tf::Session opened(&studio);
        opened.setClock(&tf::scriptClock);
        if (!opened.openSong(read, &error) || !error.isEmpty()) {
            QFAIL(qPrintable(QStringLiteral("openSong: ") + error));
            return;
        }
        float buf[384 * 2];
        for (int tick = 0; tick <= int(after * 125); ++tick) {
            tf::setScriptClock(t0 + tick / 125.0);
            opened.refresh();
            opened.idle(tf::scriptClock());
            studio.render(buf, 384);
        }
        opened.refresh();
        check(before, opened, read);
    });
}

// Every test class registers itself; the test binary runs them all.
struct Suite {
    const QMetaObject *meta;
    std::function<int(int, char **)> run;
};
inline std::vector<Suite> &registry() {
    static std::vector<Suite> r;
    return r;
}
template <typename T>
struct Register {
    Register() {
        auto run = [](int argc, char **argv) {
            // TENFOLD_TESTS=Name,Other runs only those classes, for a quick
            // look at one page's tests.
            const QByteArray only = qgetenv("TENFOLD_TESTS");
            if (!only.isEmpty() && !only.split(',').contains(T::staticMetaObject.className()))
                return 0;
            T t;
            return QTest::qExec(&t, argc, argv);
        };
        registry().push_back({&T::staticMetaObject, run});
    }
};

// A class's test functions, the way QtTest finds them: its private slots
// that take nothing, without the set-up and tear-down ones and the data
// tables.
inline QStringList testFunctions(const QMetaObject &mo) {
    static const QStringList special = {"initTestCase", "cleanupTestCase", "init", "cleanup"};
    QStringList r;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod m = mo.method(i);
        const QString name = QString::fromLatin1(m.name());
        if (m.methodType() == QMetaMethod::Slot && m.access() == QMetaMethod::Private && m.parameterCount() == 0 &&
            !special.contains(name) && !name.endsWith(QLatin1String("_data")))
            r << name;
    }
    return r;
}

// The test functions a class names in a Q_CLASSINFO, space-separated.
inline QStringList classInfo(const QMetaObject &mo, const char *key) {
    const int i = mo.indexOfClassInfo(key);
    return i < 0 ? QStringList() : QString::fromLatin1(mo.classInfo(i).value()).split(' ', Qt::SkipEmptyParts);
}

// Prints every test for bin/test, a line for each that needs a process of
// its own: "parallel Class fn" for one that runs beside the others, or
// "alone Class fn" for one that measures the studio against the clock and
// so runs after them, with the machine to itself. A class lists those in
// Q_CLASSINFO("alone", "fn other"), and tests that share work (a long take
// rendered once) in Q_CLASSINFO("together", "fn other"): they share a line,
// "parallel Class fn other", so the work is done once.
inline void list() {
    for (const Suite &s : registry()) {
        const QStringList alone = classInfo(*s.meta, "alone"), shared = classInfo(*s.meta, "together");
        const QString cls = QString::fromLatin1(s.meta->className());
        bool sharedDone = false;
        for (const QString &fn : testFunctions(*s.meta)) {
            QString line;
            if (alone.contains(fn)) {
                line = "alone " + cls + ' ' + fn;
            } else if (shared.contains(fn)) {
                if (sharedDone)
                    continue;
                sharedDone = true;
                line = "parallel " + cls + ' ' + shared.join(' ');
            } else {
                line = "parallel " + cls + ' ' + fn;
            }
            std::printf("%s\n", qPrintable(line));
        }
    }
}

}

#define TESTKIT_REGISTER(Class) static testkit::Register<Class> registerTests##Class;
