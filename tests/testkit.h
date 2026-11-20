#pragma once

// Helpers for testing pages: play a script through the real session and
// studio and look at the sound and the studio's view afterwards.

#include <QObject>
#include <QtTest>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "offline.h"
#include "session.h"
#include "studio.h"

namespace testkit {

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

// Every test class registers itself; the test binary runs them all.
inline std::vector<std::function<int(int, char **)>> &registry() {
    static std::vector<std::function<int(int, char **)>> r;
    return r;
}
template <typename T>
struct Register {
    Register() {
        registry().push_back([](int argc, char **argv) {
            T t;
            return QTest::qExec(&t, argc, argv);
        });
    }
};

}

#define TESTKIT_REGISTER(Class) static testkit::Register<Class> registerTests##Class;
