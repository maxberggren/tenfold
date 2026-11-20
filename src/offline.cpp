#include "offline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <sstream>
#include <vector>

#include <QKeySequence>

#include "dsp.h"
#include "session.h"
#include "studio.h"
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
    enum Kind { Down, Move, Glide, Up, Click, Key, Page, Tempo, End } kind = End;
    int keyMode = 0; // 0 press, 1 down, 2 up
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
                float len = 0.08f;
                e.kind = Event::Down;
                e.id = autoId++;
                if (!(ls >> e.t >> e.x >> e.y))
                    return fail("expected T X Y");
                if (!options(e, &len))
                    return fail("bad option");
                if (std::isnan(e.p))
                    e.p = 0.5f;
                events.push_back(e);
                Event u;
                u.kind = Event::Up;
                u.t = e.t + len;
                u.id = e.id;
                events.push_back(u);
            } else if (word == "click") {
                e.kind = Event::Click;
                float len = 0.1f;
                if (!(ls >> e.t))
                    return fail("expected T");
                if (!options(e, &len))
                    return fail("bad option");
                e.t2 = e.t + len;
                events.push_back(e);
            } else if (word == "key") {
                e.kind = Event::Key;
                if (!(ls >> e.t >> e.key))
                    return fail("expected T NAME");
                std::string mode;
                if (ls >> mode) {
                    if (mode == "down")
                        e.keyMode = 1;
                    else if (mode == "up")
                        e.keyMode = 2;
                    else
                        return fail("key mode is down or up");
                }
                if (QKeySequence(QString::fromStdString(e.key)).isEmpty())
                    return fail("unknown key");
                events.push_back(e);
            } else if (word == "page") {
                e.kind = Event::Page;
                if (!(ls >> e.t >> e.id))
                    return fail("expected T N");
                events.push_back(e);
            } else if (word == "tempo") {
                e.kind = Event::Tempo;
                if (!(ls >> e.t >> e.x))
                    return fail("expected T BPM");
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
    double clickUntil = -1;
    struct PendingUp {
        double t;
        int key, mods;
    };
    std::vector<PendingUp> keyUps;
};

namespace {
double g_scriptTime = 0;
}
double scriptClock() { return g_scriptTime; }
void setScriptClock(double t) { g_scriptTime = t; }

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

void ScriptPlayer::tick(double t, Session &session) {
    auto &events = d->events;
    auto &fingers = d->fingers;
    auto &glides = d->glides;
    setScriptClock(t);
    auto sendKey = [&](int key, int mods, bool down) {
        KeyEvent k;
        k.key = key;
        k.modifiers = mods;
        k.down = down;
        if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde && !(mods & Qt::ControlModifier))
            k.text = QString(QChar(key)).toLower();
        session.key(k);
    };
    for (auto it = d->keyUps.begin(); it != d->keyUps.end();) {
        if (it->t <= t + 1e-9) {
            sendKey(it->key, it->mods, false);
            it = d->keyUps.erase(it);
        } else {
            ++it;
        }
    }
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
            f.p = 0.5f;
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
            d->clickUntil = e.t2;
            break;
        case Event::Key: {
            const QKeyCombination combo = QKeySequence(QString::fromStdString(e.key))[0];
            const int key = int(combo.key()), mods = int(combo.keyboardModifiers());
            if (e.keyMode == 0) {
                sendKey(key, mods, true);
                d->keyUps.push_back({t + 0.05, key, mods});
            } else {
                sendKey(key, mods, e.keyMode == 1);
            }
            break;
        }
        case Event::Page:
            session.setPage(e.id == 0 ? session.arrangePage() : e.id - 1);
            break;
        case Event::Tempo:
            session.shiftTempo(std::round(e.x) - std::round(session.view().bpm));
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
        if (t < it->t1) {
            ++it;
            continue;
        }
        const double u = std::clamp((t - it->t1) / std::max(1e-6, it->t2 - it->t1), 0.0, 1.0);
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
    fr.button = t < d->clickUntil;
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
    session.frame(fr);
}

RenderStats renderScript(const std::string &script, const std::string &wavPath) {
    RenderStats st;
    ScriptPlayer player;
    if (!player.load(script, &st.error))
        return st;
    const double end = player.end() + 0.01;

    Studio studio(kRate);
    studio.setTimeOrigin(0);
    setScriptClock(0);
    Session session(&studio);
    session.setClock(&scriptClock);
    session.setAspect(1.6f);

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
    // The studio is fed the way the audio server feeds it live: 128 frames
    // at a time, so the load figures mean what they'd mean on the desk.
    constexpr int kQuantum = 128;
    float quantum[kQuantum * 2];
    int quantumFill = 0;

    for (int tick = 0;; ++tick) {
        const double t = tick / kFrameRate;
        if (t > end)
            break;
        session.refresh();
        player.tick(t, session);
        int todo = framesPerTick;
        while (todo > 0) {
            const int n = std::min(todo, kQuantum - quantumFill);
            studio.render(quantum + quantumFill * 2, n);
            if (n == kQuantum)
                loads.push_back(studio.lastBlockLoad());
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
        st.maxVoices = std::max(st.maxVoices, studio.activeVoices());
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
    session.refresh();
    const StudioView &v = session.view();
    st.bpm = v.bpm;
    std::ostringstream clips;
    for (int p = 0; p < v.partCount; ++p) {
        std::ostringstream line;
        int n = 0;
        for (int c = 0; c < kClipSlots; ++c) {
            const ClipView &cv = v.parts[p].clips[c];
            if (cv.state == ClipView::Empty)
                continue;
            ++n;
            line << " " << c + 1 << ":" << cv.lengthBeats << "b/" << cv.events << "ev"
                 << (cv.state == ClipView::Playing ? "*" : "");
        }
        st.clips += n;
        if (n)
            clips << session.pageName(p).toStdString() << line.str() << "; ";
    }
    st.clipsText = clips.str();
    return st;
}

RenderStats runBench(double seconds) {
    // A loop on every part, then ten fingers held on the lead page.
    std::ostringstream s;
    double t = 0.1;
    for (int page = 1; page <= 7; ++page) {
        s << "page " << t << " " << page << "\n";
        for (int i = 0; i < 8; ++i)
            s << "tap " << t + 0.05 + i * 0.25 << " " << 0.08f + 0.1f * float(i % 8) << " 0.5 p=0.8\n";
        const double one = t + 0.05 + 8 * 0.25;
        s << "down " << one << " " << page << " 0.08 0.5 p=0.8\n";
        s << "up " << one + 0.6 << " " << page << "\n";
        t = one + 0.8;
    }
    s << "page " << t << " 4\n";
    for (int f = 0; f < 10; ++f) {
        s << "down " << t + 0.05 * f << " " << 200 + f << " " << 0.06f + 0.09f * float(f) << " 0.3 p=0.8\n";
        s << "glide " << t + 1.0 << " " << t + 3.0 << " " << 200 + f << " " << 0.06f + 0.09f * float(f)
          << " 0.95 p=0.9 twist=50\n";
    }
    s << "end " << t + std::max(5.0, seconds) << "\n";
    return renderScript(s.str(), "");
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
      << "  \"bpm\": " << s.bpm << ",\n"
      << "  \"max_voices\": " << s.maxVoices << ",\n"
      << "  \"clips\": " << s.clips << ",\n"
      << "  \"clips_by_part\": \"" << s.clipsText << "\"\n}\n";
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
      << "tempo            " << s.bpm << " BPM\n"
      << "most voices      " << s.maxVoices << "\n"
      << "clips            " << s.clips << (s.clipsText.empty() ? "" : "  (" + s.clipsText + ")") << "\n";
    return o.str();
}

}
