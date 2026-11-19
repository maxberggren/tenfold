#include "offline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <sstream>
#include <vector>

#include "engine.h"
#include "enginesink.h"
#include "gesture.h"
#include "wav.h"

namespace tf {

namespace {

constexpr float kRate = 48000.0f;
constexpr double kFrameRate = 125.0; // what an Apple trackpad reports at

struct Finger {
    bool down = false;
    float x = 0, y = 0, p = 0.6f, size = 1.0f, twist = 0;
};

struct Event {
    double t = 0, t2 = 0;
    enum Kind { Down, Move, Glide, Up, Click, Key, End } kind = End;
    int id = 0;
    float x = 0, y = 0;
    float p = NAN, size = NAN, twist = NAN;
    std::string key;
};

struct GlideMove {
    double t1, t2;
    int id;
    Finger from, to;
};

bool parseScript(const std::string &text, std::vector<Event> &events, std::string &error) {
    std::istringstream in(text);
    std::string line;
    int lineNo = 0, autoId = 1000;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.resize(hash);
        std::istringstream ls(line);
        std::string word;
        if (!(ls >> word))
            continue;
        auto fail = [&](const char *what) {
            error = "line " + std::to_string(lineNo) + ": " + what;
            return false;
        };
        auto options = [&](Event &e, float *len) {
            std::string opt;
            while (ls >> opt) {
                const auto eq = opt.find('=');
                if (eq == std::string::npos)
                    return false;
                const std::string k = opt.substr(0, eq);
                const float v = std::stof(opt.substr(eq + 1));
                if (k == "p")
                    e.p = v;
                else if (k == "size")
                    e.size = v;
                else if (k == "twist")
                    e.twist = v;
                else if (k == "len" && len)
                    *len = v;
                else
                    return false;
            }
            return true;
        };
        Event e;
        try {
            if (word == "down" || word == "move") {
                e.kind = word == "down" ? Event::Down : Event::Move;
                if (!(ls >> e.t >> e.id >> e.x >> e.y))
                    return fail("expected T ID X Y");
                if (!options(e, nullptr))
                    return fail("bad option");
                events.push_back(e);
            } else if (word == "glide") {
                e.kind = Event::Glide;
                if (!(ls >> e.t >> e.t2 >> e.id >> e.x >> e.y))
                    return fail("expected T1 T2 ID X Y");
                if (!options(e, nullptr))
                    return fail("bad option");
                events.push_back(e);
            } else if (word == "up") {
                e.kind = Event::Up;
                if (!(ls >> e.t >> e.id))
                    return fail("expected T ID");
                events.push_back(e);
            } else if (word == "tap") {
                float len = 0.06f;
                e.kind = Event::Down;
                e.id = autoId++;
                if (!(ls >> e.t >> e.x >> e.y))
                    return fail("expected T X Y");
                if (!options(e, &len))
                    return fail("bad option");
                if (std::isnan(e.p))
                    e.p = 0.7f;
                events.push_back(e);
                Event u;
                u.kind = Event::Up;
                u.t = e.t + len;
                u.id = e.id;
                events.push_back(u);
            } else if (word == "click") {
                e.kind = Event::Click;
                if (!(ls >> e.t))
                    return fail("expected T");
                events.push_back(e);
            } else if (word == "key") {
                e.kind = Event::Key;
                if (!(ls >> e.t >> e.key))
                    return fail("expected T NAME");
                events.push_back(e);
            } else if (word == "end") {
                e.kind = Event::End;
                if (!(ls >> e.t))
                    return fail("expected T");
                events.push_back(e);
            } else {
                return fail("unknown event");
            }
        } catch (...) {
            return fail("not a number");
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) { return a.t < b.t; });
    return true;
}

double toDb(double v) { return v > 1e-9 ? 20.0 * std::log10(v) : -180.0; }

}

struct ScriptPlayer::Impl {
    std::vector<Event> events;
    double end = 0;
    std::map<int, Finger> fingers;
    std::vector<GlideMove> glides;
    size_t next = 0;
    int clickFrames = 0;
};

ScriptPlayer::ScriptPlayer() : d(new Impl) {}
ScriptPlayer::~ScriptPlayer() { delete d; }
double ScriptPlayer::end() const { return d->end; }

bool ScriptPlayer::load(const std::string &script, std::string *error) {
    *d = Impl{};
    std::string err;
    if (!parseScript(script, d->events, err)) {
        if (error)
            *error = err;
        return false;
    }
    for (const Event &e : d->events)
        d->end = std::max(d->end, std::max(e.t, e.t2));
    for (const Event &e : d->events)
        if (e.kind == Event::End)
            d->end = e.t;
    return true;
}

void ScriptPlayer::tick(double t, Gesture &gesture) {
    auto &events = d->events;
    auto &fingers = d->fingers;
    auto &glides = d->glides;
    while (d->next < events.size() && events[d->next].t <= t + 1e-9) {
        const Event &e = events[d->next++];
        auto apply = [&](Finger &f) {
            f.x = e.x;
            f.y = e.y;
            if (!std::isnan(e.p))
                f.p = e.p;
            if (!std::isnan(e.size))
                f.size = e.size;
            if (!std::isnan(e.twist))
                f.twist = e.twist;
        };
        switch (e.kind) {
        case Event::Down: {
            Finger f;
            f.down = true;
            apply(f);
            fingers[e.id] = f;
            break;
        }
        case Event::Move:
            if (fingers.count(e.id))
                apply(fingers[e.id]);
            break;
        case Event::Glide:
            if (fingers.count(e.id)) {
                Finger to = fingers[e.id];
                apply(to);
                glides.push_back({e.t, e.t2, e.id, fingers[e.id], to});
            }
            break;
        case Event::Up:
            fingers.erase(e.id);
            break;
        case Event::Click:
            d->clickFrames = 3;
            break;
        case Event::Key:
            if (e.key == "dissolve")
                gesture.dissolve();
            else if (e.key == "dissolve-all")
                gesture.dissolveAll();
            else if (e.key == "root+")
                gesture.shiftRoot(1);
            else if (e.key == "root-")
                gesture.shiftRoot(-1);
            else if (e.key == "family+")
                gesture.shiftFamily(1);
            else if (e.key == "family-")
                gesture.shiftFamily(-1);
            else if (e.key == "just")
                gesture.toggleJust();
            else if (e.key == "reset")
                gesture.resetKnobs();
            break;
        case Event::End:
            break;
        }
    }
    for (auto it = glides.begin(); it != glides.end();) {
        if (!fingers.count(it->id)) {
            it = glides.erase(it);
            continue;
        }
        const double u = std::clamp((t - it->t1) / std::max(1e-6, it->t2 - it->t1), 0.0, 1.0);
        if (t < it->t1) {
            ++it;
            continue;
        }
        // Smoothstep, the way a hand eases in and out of a move.
        const float s = float(u * u * (3 - 2 * u));
        Finger &f = fingers[it->id];
        f.x = lerpf(it->from.x, it->to.x, s);
        f.y = lerpf(it->from.y, it->to.y, s);
        f.p = lerpf(it->from.p, it->to.p, s);
        f.size = lerpf(it->from.size, it->to.size, s);
        f.twist = lerpf(it->from.twist, it->to.twist, s);
        it = u >= 1.0 ? glides.erase(it) : it + 1;
    }

    Frame fr;
    fr.t = t;
    fr.button = d->clickFrames > 0;
    if (d->clickFrames > 0)
        --d->clickFrames;
    for (auto &[id, f] : fingers) {
        if (fr.count >= int(fr.touches.size()))
            break;
        Touch &tc = fr.touches[fr.count++];
        tc.id = id;
        tc.x = std::clamp(f.x, 0.0f, 1.0f);
        tc.y = std::clamp(f.y, 0.0f, 1.0f);
        tc.size = f.size;
        tc.pressure = std::clamp(f.p, 0.0f, 1.0f);
        tc.angle = f.twist * 3.14159265f / 180.0f;
        tc.eccentricity = 0.5f;
    }
    gesture.frame(fr);
}

RenderStats renderScript(const std::string &script, const std::string &wavPath) {
    RenderStats st;
    ScriptPlayer player;
    if (!player.load(script, &st.error))
        return st;
    const double end = player.end() + 0.01;

    Engine engine(kRate);
    engine.setTimeOrigin(0);
    EngineSink sink(&engine, 1.6f);
    Gesture gesture(&sink);
    gesture.setAspect(1.6f);

    WavWriter wav;
    if (!wavPath.empty() && !wav.open(wavPath, int(kRate))) {
        st.error = "cannot write " + wavPath;
        return st;
    }

    std::vector<double> loads;
    double peak = 0, sumSq = 0;
    size_t total = 0;
    const auto wall0 = std::chrono::steady_clock::now();
    const int framesPerTick = int(std::lround(kRate / kFrameRate));
    // The engine is fed the way the audio server feeds it live: 128 frames
    // at a time, so the load figures mean what they'd mean on the desk.
    constexpr int kQuantum = 128;
    float quantum[kQuantum * 2];
    int quantumFill = 0;

    for (int tick = 0;; ++tick) {
        const double t = tick / kFrameRate;
        if (t > end)
            break;
        player.tick(t, gesture);
        int todo = framesPerTick;
        while (todo > 0) {
            const int n = std::min(todo, kQuantum - quantumFill);
            engine.render(quantum + quantumFill * 2, n);
            if (n == kQuantum)
                loads.push_back(engine.lastBlockLoad());
            quantumFill += n;
            todo -= n;
            if (quantumFill == kQuantum) {
                if (!wavPath.empty())
                    wav.write(quantum, kQuantum);
                for (int i = 0; i < kQuantum * 2; ++i) {
                    peak = std::max(peak, double(std::fabs(quantum[i])));
                    sumSq += double(quantum[i]) * quantum[i];
                }
                total += kQuantum;
                quantumFill = 0;
            }
        }
        st.maxVoices = std::max(st.maxVoices, engine.activeVoices());
        st.maxLoops = std::max(st.maxLoops, engine.activeLoops());
    }
    wav.close();
    st.renderSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    st.seconds = double(total) / kRate;
    st.peakDb = toDb(peak);
    st.rmsDb = toDb(std::sqrt(sumSq / std::max<size_t>(1, total * 2)));
    std::sort(loads.begin(), loads.end());
    if (!loads.empty()) {
        double sum = 0;
        for (double l : loads)
            sum += l;
        st.meanLoad = sum / double(loads.size());
        st.maxLoad = loads.back();
        st.p99Load = loads[size_t(double(loads.size() - 1) * 0.99)];
    }
    engine.fetchView();
    st.strikes = engine.view().strikes;
    const GestureView gv = gesture.view();
    st.loopsMade = gv.loopsMade;
    st.freezes = gv.freezes;
    return st;
}

RenderStats runBench(double seconds) {
    // Build the worst case by script: a dense loop per row of the pad, a
    // frozen chord, then ten held fingers with every knob turned up.
    std::ostringstream s;
    double t = 0.1;
    for (int loop = 0; loop < 12; ++loop) {
        const float y = 0.04f + 0.075f * float(loop);
        for (int i = 0; i < 16; ++i)
            s << "tap " << t + i * 0.15 << " " << 0.05f + 0.055f * float(i) << " " << y << " p=0.9\n";
        const double one = t + 16 * 0.15;
        s << "down " << one << " " << loop + 1 << " 0.5 " << y << " p=0.8\n";
        s << "glide " << one + 0.25 << " " << one + 0.6 << " " << loop + 1 << " 0.55 " << y << " twist=55\n";
        s << "up " << one + 0.7 << " " << loop + 1 << "\n";
        t = one + 1.0;
    }
    // Frozen chord of ten.
    for (int f = 0; f < 10; ++f)
        s << "down " << t << " " << 100 + f << " " << 0.05f + 0.09f * float(f) << " 0.5 p=0.8\n";
    s << "click " << t + 1.0 << "\n";
    for (int f = 0; f < 10; ++f)
        s << "up " << t + 1.2 << " " << 100 + f << "\n";
    t += 1.5;
    // Ten held fingers, each sliding its knob to the top.
    for (int f = 0; f < 10; ++f) {
        s << "down " << t + 0.05 * f << " " << 200 + f << " " << 0.06f + 0.09f * float(f) << " 0.3 p=0.8\n";
        s << "glide " << t + 1.0 << " " << t + 3.0 << " " << 200 + f << " " << 0.06f + 0.09f * float(f)
          << " 0.95 p=0.9 twist=50\n";
    }
    s << "end " << t + std::max(5.0, seconds) << "\n";
    RenderStats st = renderScript(s.str(), "");
    return st;
}

std::string statsJson(const RenderStats &s) {
    std::ostringstream o;
    o.precision(4);
    o << "{\n";
    if (!s.error.empty())
        o << "  \"error\": \"" << s.error << "\",\n";
    o << "  \"seconds\": " << s.seconds << ",\n"
      << "  \"render_seconds\": " << s.renderSeconds << ",\n"
      << "  \"realtime_factor\": " << (s.renderSeconds > 0 ? s.seconds / s.renderSeconds : 0) << ",\n"
      << "  \"peak_dbfs\": " << s.peakDb << ",\n"
      << "  \"rms_dbfs\": " << s.rmsDb << ",\n"
      << "  \"callback_load_mean\": " << s.meanLoad << ",\n"
      << "  \"callback_load_p99\": " << s.p99Load << ",\n"
      << "  \"callback_load_max\": " << s.maxLoad << ",\n"
      << "  \"strikes\": " << s.strikes << ",\n"
      << "  \"loops_made\": " << s.loopsMade << ",\n"
      << "  \"freezes\": " << s.freezes << ",\n"
      << "  \"max_voices\": " << s.maxVoices << ",\n"
      << "  \"max_loops\": " << s.maxLoops << "\n}\n";
    return o.str();
}

std::string statsText(const RenderStats &s) {
    std::ostringstream o;
    o.precision(3);
    o << std::fixed;
    if (!s.error.empty())
        o << "error: " << s.error << "\n";
    o << "length           " << s.seconds << " s (rendered in " << s.renderSeconds << " s, "
      << (s.renderSeconds > 0 ? s.seconds / s.renderSeconds : 0) << "× real time)\n"
      << "peak / rms       " << s.peakDb << " / " << s.rmsDb << " dBFS\n"
      << "callback load    mean " << s.meanLoad * 100 << "%, p99 " << s.p99Load * 100 << "%, max "
      << s.maxLoad * 100 << "% of each 128-frame callback\n"
      << "strikes          " << s.strikes << "\n"
      << "loops / freezes  " << s.loopsMade << " / " << s.freezes << "\n"
      << "most voices      " << s.maxVoices << ", most loops " << s.maxLoops << "\n";
    return o.str();
}

}
