#include "gesture.h"

#include <algorithm>
#include <cmath>

namespace tf {

namespace {

constexpr float kTwistRange = 3.14159265f / 3.0f; // a 60° twist sweeps a knob end to end
constexpr float kSlideGain = 1.6f;                // 60% of the pad's height sweeps a knob
constexpr float kPalmSize = 2.3f;

int defaultBrightness(int family) {
    switch (family) {
    case 0: return 3; // Dorian
    case 1: return 3; // minor pentatonic
    case 2: return 2; // harmonic minor
    case 3: return 3; // In
    default: return 0;
    }
}

}

Gesture::Gesture(CommandSink *sink) : m_sink(sink) {
    for (int r = 0; r < RoleCount; ++r)
        for (int a = 0; a < 2; ++a)
            m_knob[r][a] = roleDefault(r, a);
    m_tuning.set(2, 1, defaultBrightness(1), true);
    const int modes = family(m_tuning.familyIndex()).modeCount;
    m_knob[Gravity][1] = float(m_tuning.brightnessIndex()) / float(std::max(1, modes - 1));
    sendTuning();
    for (int r = 0; r < RoleCount; ++r) {
        Command c;
        c.type = Cmd::Knob;
        c.i1 = r;
        c.f[0] = m_knob[r][0];
        c.f[1] = m_knob[r][1];
        send(c);
    }
    publish();
}

int Gesture::cells() const { return kOctaves * m_tuning.degreesPerOctave() + 1; }

float Gesture::degreeAt(float x) const {
    const float n = float(cells());
    return std::clamp(x * n - 0.5f, -0.5f, n - 0.5f);
}

int Gesture::strikeDegree(float x) const { return std::clamp(int(std::floor(x * float(cells()))), 0, cells() - 1); }

Gesture::Contact *Gesture::find(int id) {
    for (auto &c : m_contacts)
        if (c.live && c.id == id)
            return &c;
    return nullptr;
}

bool Gesture::isPalm(const Touch &t) const { return t.size > kPalmSize; }

void Gesture::frame(const Frame &f) {
    std::lock_guard lock(m_viewLock);
    const double dt = m_lastFrame > 0 ? std::clamp(f.t - m_lastFrame, 0.001, 0.05) : 0.008;
    m_lastFrame = f.t;
    m_now = f.t;

    bool seen[16] = {};
    for (int i = 0; i < f.count; ++i) {
        const Touch &t = f.touches[i];
        if (Contact *c = find(t.id)) {
            seen[c - m_contacts.data()] = true;
            move(*c, t, f.t, dt);
        } else {
            for (auto &slot : m_contacts)
                if (!slot.live) {
                    land(slot, t, f.t);
                    seen[&slot - m_contacts.data()] = true;
                    break;
                }
        }
    }
    for (auto &c : m_contacts)
        if (c.live && !seen[&c - m_contacts.data()])
            lift(c, f.t);

    for (auto &c : m_contacts) {
        if (!c.live || c.state != TouchState::Landing)
            continue;
        // Strike on the second frame, when the pressure has had a moment to
        // say how hard the finger came down; a few milliseconds, not a delay
        // anyone can feel.
        if (!c.struck && (c.frames >= 2 || f.t - c.t0 >= 0.012))
            strike(c);
        if (f.t - c.t0 >= kHoldTime)
            hold(c, f.t);
    }

    if (f.button && !m_button)
        click(f.t);
    m_button = f.button;

    // An unfinished phrase is forgotten after a pause long enough that no
    // one would hear the next tap as part of it.
    if (!m_phrase.empty()) {
        float maxGap = 1.3f;
        for (size_t i = 1; i < m_phrase.size(); ++i)
            maxGap = std::max(maxGap, float(m_phrase[i].t - m_phrase[i - 1].t) * 1.8f);
        if (f.t - m_phrase.back().t > std::min(maxGap, 2.4f) + kHoldTime)
            m_phrase.clear();
    }

    float damper = 0;
    for (auto &c : m_contacts)
        if (c.live && c.state == TouchState::Palm)
            damper = std::max(damper, std::clamp(0.45f + (c.size - kPalmSize) * 0.3f, 0.0f, 1.0f));
    if (std::fabs(damper - m_damper) > 0.01f || (damper == 0 && m_damper != 0)) {
        m_damper = damper;
        Command c;
        c.type = Cmd::Damper;
        c.f[0] = damper;
        send(c);
    }
    if (damper == 0)
        m_palmClicked = false;

    publish();
}

void Gesture::land(Contact &c, const Touch &t, double now) {
    c = Contact{};
    c.live = true;
    c.id = t.id;
    c.t0 = c.last = now;
    c.x0 = c.x = t.x;
    c.y0 = c.y = t.y;
    c.size = t.size;
    c.pressure = c.peakPressure = t.pressure;
    c.angle = t.angle;
    c.frames = 1;
    c.degree0 = strikeDegree(t.x);
    c.material0 = std::clamp(t.y * 3.0f, 0.0f, 3.0f);
    c.slowDeg = degreeAt(t.x);
    c.state = isPalm(t) ? TouchState::Palm : TouchState::Landing;
}

void Gesture::move(Contact &c, const Touch &t, double now, double dt) {
    const float dx = (t.x - c.x) * m_aspect, dy = t.y - c.y;
    c.travel += std::sqrt(dx * dx + dy * dy);
    c.x = t.x;
    c.y = t.y;
    c.size = t.size;
    c.pressure = t.pressure;
    c.last = now;
    ++c.frames;
    if (!c.struck)
        c.peakPressure = std::max(c.peakPressure, t.pressure);

    // Twist: the contact ellipse's angle, unwrapped, trusted only as far as
    // the contact is actually elongated (a round contact has no direction).
    float da = t.angle - c.angle;
    const float half = 3.14159265f / 2.0f;
    while (da > half)
        da -= 2 * half;
    while (da < -half)
        da += 2 * half;
    const float trust = std::clamp((t.eccentricity - 0.08f) / 0.2f, 0.0f, 1.0f);
    c.twist += da * trust;
    c.angle = t.angle;

    if (c.state == TouchState::Landing && isPalm(t) && now - c.t0 < 0.15)
        c.state = TouchState::Palm;

    if (c.state == TouchState::Voice) {
        if (c.role >= 0) {
            const float a = std::clamp(c.refKnob[0] + (c.y - c.refY) * kSlideGain, 0.0f, 1.0f);
            const float b = std::clamp(c.refKnob[1] + (c.twist - c.refTwist) / kTwistRange, 0.0f, 1.0f);
            if (std::fabs(a - m_knob[c.role][0]) > 0.001f || std::fabs(b - m_knob[c.role][1]) > 0.001f) {
                m_knob[c.role][0] = a;
                m_knob[c.role][1] = b;
                Command k;
                k.type = Cmd::Knob;
                k.i1 = c.role;
                k.f[0] = a;
                k.f[1] = b;
                send(k);
                if (c.role == Gravity) {
                    const int modes = family(m_tuning.familyIndex()).modeCount;
                    const int bright = std::clamp(int(std::lround(b * float(modes - 1))), 0, modes - 1);
                    if (bright != m_tuning.brightnessIndex()) {
                        m_tuning.set(m_tuning.root(), m_tuning.familyIndex(), bright, m_tuning.just());
                        sendTuning();
                    }
                }
            }
        }
        Command v;
        v.type = Cmd::VoiceMove;
        v.id = c.id;
        v.f[0] = voicePitch(c, float(dt));
        v.f[1] = c.pressure;
        v.f[2] = c.size;
        v.f[3] = c.x;
        v.f[4] = c.y;
        send(v);
    } else if (c.state == TouchState::Handle) {
        Command m;
        m.type = Cmd::LoopMod;
        m.id = c.loop;
        m.i1 = int(std::lround((c.x - c.refX) * float(cells())));
        m.f[0] = (c.y - c.refY) * 3.0f;
        c.loopDensity = std::clamp(c.refDensity + (c.twist - c.refTwist) / kTwistRange, -1.0f, 1.0f);
        m.f[1] = c.loopDensity;
        m.f[2] = 0.55f + 0.95f * c.pressure;
        m.f[3] = c.x;
        m.f[4] = c.y;
        send(m);
    }
}

float Gesture::voicePitch(Contact &c, double dt) {
    // Pitch has gravity. A slow-following centre snaps to the nearest scale
    // note, and whatever the finger does around that centre (a wobble, a
    // rock) is vibrato on top. A deliberate slide outruns the centre and
    // glides freely through the notes in between. Gravity's first knob sets
    // how strong the pull is.
    const float xd = degreeAt(c.x);
    const float prev = c.slowDeg;
    c.slowDeg += (xd - c.slowDeg) * float(1.0 - std::exp(-dt / 0.2));
    const float speed = std::fabs(c.slowDeg - prev) / float(dt);
    c.slowSpeed += (speed - c.slowSpeed) * float(1.0 - std::exp(-dt / 0.08));
    const float target = std::clamp((c.slowSpeed - 0.5f) / 2.5f, 0.0f, 1.0f);
    c.motion += (target - c.motion) * float(1.0 - std::exp(-dt / 0.1));
    const double snapped =
        m_tuning.degreeSemis(int(std::lround(c.slowDeg))) + (xd - c.slowDeg) * m_tuning.semisPerDegree(c.slowDeg);
    const double cont = m_tuning.continuousSemis(xd);
    const double w = m_knob[Gravity][0] * (1.0f - c.motion);
    return float(m_tuning.baseMidi() + cont + (snapped - cont) * w);
}

void Gesture::strike(Contact &c) {
    c.struck = true;
    if (c.state == TouchState::Palm)
        return;
    c.vel = std::clamp(0.12f + 0.95f * c.peakPressure, 0.08f, 1.0f);
    Command s;
    s.type = Cmd::Strike;
    s.i1 = c.degree0;
    s.f[0] = c.material0;
    s.f[1] = c.vel;
    s.f[2] = c.x0;
    s.f[3] = c.y0;
    send(s);
    m_strikes[m_strikeSeq % m_strikes.size()] = {c.x0, c.y0, c.vel, c.material0, c.t0};
    ++m_strikeSeq;
}

void Gesture::hold(Contact &c, double now) {
    (void)now;
    if (!m_phrase.empty()) {
        const Tap &last = m_phrase.back();
        const double gap = c.t0 - last.t;
        double maxGap = 1.3;
        if (m_phrase.size() > 1) {
            double maxIoi = 0;
            for (size_t i = 1; i < m_phrase.size(); ++i)
                maxIoi = std::max(maxIoi, m_phrase[i].t - m_phrase[i - 1].t);
            maxGap = std::clamp(1.75 * maxIoi, 0.35, 2.2);
        }
        if (gap > 0 && gap <= maxGap) {
            const float dx = (c.x0 - last.x) * m_aspect, dy = c.y0 - last.y;
            // A single quick tap and hold on top of a loop picks it up again.
            if (m_phrase.size() == 1 && gap < 0.45 && std::sqrt(dx * dx + dy * dy) < 0.08f) {
                const int id = m_sink->loopNear(c.x, c.y, 0.14f);
                if (id >= 0) {
                    c.state = TouchState::Handle;
                    c.loop = id;
                    c.refX = c.x;
                    c.refY = c.y;
                    c.refTwist = c.twist;
                    c.refDensity = 0;
                    Command g;
                    g.type = Cmd::LoopGrab;
                    g.id = id;
                    send(g);
                    m_activeHandle = id;
                    m_phrase.clear();
                    return;
                }
            }
            const double length = c.t0 - m_phrase.front().t;
            if (length >= 0.12) {
                const int id = m_nextLoop++;
                Command b;
                b.type = Cmd::LoopBegin;
                b.id = id;
                b.t = c.t0;
                b.f[0] = float(length);
                b.f[1] = c.x;
                b.f[2] = c.y;
                send(b);
                for (const Tap &t : m_phrase) {
                    Command h;
                    h.type = Cmd::LoopHit;
                    h.id = id;
                    h.f[0] = float(t.t - m_phrase.front().t);
                    h.i1 = t.degree;
                    h.f[1] = t.material;
                    h.f[2] = t.vel;
                    h.f[3] = t.x;
                    send(h);
                }
                Command r;
                r.type = Cmd::LoopReady;
                r.id = id;
                send(r);
                c.state = TouchState::Handle;
                c.loop = id;
                c.refX = c.x;
                c.refY = c.y;
                c.refTwist = c.twist;
                c.refDensity = 0;
                m_activeHandle = id;
                m_phrase.clear();
                ++m_loopsMade;
                return;
            }
        }
    }

    // A voice. It takes the lowest role no other held finger has.
    bool taken[RoleCount] = {};
    for (auto &o : m_contacts)
        if (o.live && o.state == TouchState::Voice && o.role >= 0)
            taken[o.role] = true;
    int role = -1;
    for (int r = 0; r < RoleCount; ++r)
        if (!taken[r]) {
            role = r;
            break;
        }
    c.state = TouchState::Voice;
    c.role = role;
    c.refY = c.y;
    c.refTwist = c.twist;
    if (role >= 0) {
        c.refKnob[0] = m_knob[role][0];
        c.refKnob[1] = m_knob[role][1];
    }
    c.slowDeg = degreeAt(c.x);
    Command v;
    v.type = Cmd::VoiceOn;
    v.id = c.id;
    v.i1 = std::max(0, role);
    v.f[0] = voicePitch(c, 0.008);
    v.f[1] = c.pressure;
    v.f[2] = c.size;
    v.f[3] = c.x;
    v.f[4] = c.y;
    send(v);
}

void Gesture::lift(Contact &c, double now) {
    (void)now;
    switch (c.state) {
    case TouchState::Landing: {
        if (!c.struck)
            strike(c);
        if (c.state == TouchState::Palm)
            break;
        bool handleHeld = false;
        for (auto &o : m_contacts)
            if (o.live && o.state == TouchState::Handle && o.loop == m_activeHandle)
                handleHeld = true;
        if (handleHeld) {
            Command o;
            o.type = Cmd::Overdub;
            o.id = m_activeHandle;
            o.t = c.t0;
            o.i1 = c.degree0;
            o.f[0] = c.material0;
            o.f[1] = c.vel;
            o.f[2] = c.x0;
            send(o);
        } else {
            m_phrase.push_back({c.t0, c.x0, c.y0, c.degree0, c.material0, c.vel});
            while (m_phrase.size() > 64 || (m_phrase.size() > 1 && c.t0 - m_phrase.front().t > 8.0))
                m_phrase.erase(m_phrase.begin());
        }
        break;
    }
    case TouchState::Voice: {
        Command o;
        o.type = Cmd::VoiceOff;
        o.id = c.id;
        send(o);
        break;
    }
    case TouchState::Handle: {
        Command r;
        r.type = Cmd::LoopRelease;
        r.id = c.loop;
        send(r);
        if (m_activeHandle == c.loop) {
            m_activeHandle = -1;
            for (auto &o : m_contacts)
                if (o.live && &o != &c && o.state == TouchState::Handle)
                    m_activeHandle = o.loop;
        }
        break;
    }
    case TouchState::Palm:
    case TouchState::Spent:
        break;
    }
    c.live = false;
}

void Gesture::click(double now) {
    (void)now;
    bool palm = false, handle = false, voice = false;
    for (auto &c : m_contacts) {
        if (!c.live)
            continue;
        palm |= c.state == TouchState::Palm;
        handle |= c.state == TouchState::Handle;
        voice |= c.state == TouchState::Voice;
    }
    if (palm) {
        if (!m_palmClicked) {
            Command d;
            d.type = Cmd::DissolveAll;
            send(d);
            m_palmClicked = true;
        }
        return;
    }
    if (handle) {
        for (auto &c : m_contacts)
            if (c.live && c.state == TouchState::Handle) {
                Command p;
                p.type = Cmd::LoopPin;
                p.id = c.loop;
                send(p);
            }
        return;
    }
    if (voice) {
        Command f;
        f.type = Cmd::Freeze;
        send(f);
        for (auto &c : m_contacts)
            if (c.live && c.state == TouchState::Voice) {
                c.state = TouchState::Spent;
                c.role = -1;
            }
        ++m_freezes;
    }
}

void Gesture::sendTuning() {
    Command c;
    c.type = Cmd::SetTuning;
    c.i1 = m_tuning.root();
    c.i2 = m_tuning.familyIndex();
    c.i3 = m_tuning.brightnessIndex();
    c.f[0] = m_tuning.just() ? 1.0f : 0.0f;
    send(c);
}

void Gesture::shiftRoot(int fifths) {
    std::lock_guard lock(m_viewLock);
    m_tuning.set(m_tuning.root() + 7 * fifths, m_tuning.familyIndex(), m_tuning.brightnessIndex(), m_tuning.just());
    sendTuning();
    publish();
}

void Gesture::shiftFamily(int step) {
    std::lock_guard lock(m_viewLock);
    const int fam = (m_tuning.familyIndex() + step + familyCount()) % familyCount();
    m_tuning.set(m_tuning.root(), fam, defaultBrightness(fam), m_tuning.just());
    const int modes = family(fam).modeCount;
    m_knob[Gravity][1] = float(m_tuning.brightnessIndex()) / float(std::max(1, modes - 1));
    Command k;
    k.type = Cmd::Knob;
    k.i1 = Gravity;
    k.f[0] = m_knob[Gravity][0];
    k.f[1] = m_knob[Gravity][1];
    send(k);
    sendTuning();
    publish();
}

void Gesture::toggleJust() {
    std::lock_guard lock(m_viewLock);
    m_tuning.set(m_tuning.root(), m_tuning.familyIndex(), m_tuning.brightnessIndex(), !m_tuning.just());
    sendTuning();
    publish();
}

void Gesture::resetKnobs() {
    std::lock_guard lock(m_viewLock);
    for (int r = 0; r < RoleCount; ++r) {
        m_knob[r][0] = roleDefault(r, 0);
        m_knob[r][1] = r == Gravity ? float(m_tuning.brightnessIndex()) /
                                          float(std::max(1, family(m_tuning.familyIndex()).modeCount - 1))
                                    : roleDefault(r, 1);
        Command k;
        k.type = Cmd::Knob;
        k.i1 = r;
        k.f[0] = m_knob[r][0];
        k.f[1] = m_knob[r][1];
        send(k);
    }
    // Fingers still holding a role pick up from the new values.
    for (auto &c : m_contacts)
        if (c.live && c.state == TouchState::Voice && c.role >= 0) {
            c.refY = c.y;
            c.refTwist = c.twist;
            c.refKnob[0] = m_knob[c.role][0];
            c.refKnob[1] = m_knob[c.role][1];
        }
    publish();
}

void Gesture::dissolve() {
    std::lock_guard lock(m_viewLock);
    Command c;
    c.type = Cmd::Dissolve;
    send(c);
}

void Gesture::dissolveAll() {
    std::lock_guard lock(m_viewLock);
    Command c;
    c.type = Cmd::DissolveAll;
    send(c);
}

void Gesture::publish() {
    GestureView &v = m_view;
    v.count = 0;
    for (auto &c : m_contacts) {
        if (!c.live)
            continue;
        ContactView &o = v.contacts[v.count++];
        o.id = c.id;
        o.state = c.state;
        o.role = c.role;
        o.loop = c.loop;
        o.x = c.x;
        o.y = c.y;
        o.size = c.size;
        o.pressure = c.pressure;
        o.angle = c.angle;
        o.twist = c.state == TouchState::Handle ? c.loopDensity : c.twist - c.refTwist;
        o.age = float(m_now - c.t0);
    }
    v.phraseCount = 0;
    for (const Tap &t : m_phrase)
        if (v.phraseCount < int(v.phrase.size()))
            v.phrase[v.phraseCount++] = {t.x, t.y, t.t};
    v.phraseOpen = !m_phrase.empty();
    for (int r = 0; r < RoleCount; ++r) {
        v.knobs[r][0] = m_knob[r][0];
        v.knobs[r][1] = m_knob[r][1];
    }
    v.damper = m_damper;
    v.strikeSeq = m_strikeSeq;
    v.strikes = m_strikes;
    v.t = m_now;
    v.root = m_tuning.root();
    v.family = m_tuning.familyIndex();
    v.brightness = m_tuning.brightnessIndex();
    v.just = m_tuning.just();
    v.loopsMade = m_loopsMade;
    v.freezes = m_freezes;
}

GestureView Gesture::view() const {
    std::lock_guard lock(m_viewLock);
    return m_view;
}

}
