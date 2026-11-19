#include "gesture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tf {

namespace {

constexpr float kHalfPi = 1.5707963f;
constexpr float kTwistRange = 3.14159265f / 3.0f; // a 60° twist sweeps a knob end to end
constexpr float kTwistDeadband = 0.06f;           // ≈3.5°: a still finger's wobble isn't a twist
constexpr float kSlideGain = 1.6f;                // 60% of the pad's height sweeps a knob
constexpr float kSlideDeadband = 0.035f;          // a resting finger's drift isn't a slide
constexpr float kPalmSize = 2.6f;                 // a palm is far bigger than a pressed fingertip
constexpr float kPalmFromVoice = 3.2f;            // and a note already held needs more to become one

// Slide distance with the dead zone taken out, so knobs only move when the
// finger means to move them.
float slideOf(float dy) {
    const float mag = std::max(0.0f, std::fabs(dy) - kSlideDeadband);
    return dy < 0 ? -mag : mag;
}
constexpr float kSwipeDistance = 0.12f;
constexpr int kMaxLanes = 4;

int defaultBrightness(int family) {
    switch (family) {
    case 0: return 4; // Aeolian: natural minor
    case 1: return 3; // minor pentatonic
    case 2: return 2; // harmonic minor
    case 3: return 3; // In
    default: return 0;
    }
}

float wrapHalfTurn(float a) {
    while (a > kHalfPi)
        a -= 2 * kHalfPi;
    while (a < -kHalfPi)
        a += 2 * kHalfPi;
    return a;
}

}

Gesture::Gesture(CommandSink *sink) : m_sink(sink) {
    for (int row = 0; row < RowCount; ++row)
        for (int r = 0; r < RoleCount; ++r)
            for (int a = 0; a < 2; ++a)
                m_knob[row][r][a] = rowDefault(row, r, a);
    // A minor, equal temperament: the key most electronic music lives in.
    m_tuning.set(9, 0, defaultBrightness(0), false);
    const int modes = family(m_tuning.familyIndex()).modeCount;
    knob(0, Gravity)[1] = float(m_tuning.brightnessIndex()) / float(std::max(1, modes - 1));
    sendTuning();
    for (int r = 0; r < RoleCount; ++r) {
        if (rowRole(r))
            for (int row = 0; row < RowCount; ++row)
                sendKnob(row, r);
        else
            sendKnob(0, r);
    }
    publish();
}

int Gesture::cells() const { return kOctaves * m_tuning.degreesPerOctave() + 1; }

float Gesture::degreeAt(float x) const {
    const float n = float(cells());
    return std::clamp(x * n - 0.5f, -0.5f, n - 0.5f);
}

int Gesture::strikeDegree(float x) const {
    return std::clamp(int(std::floor(x * float(cells()))), 0, cells() - 1) + m_octave * m_tuning.degreesPerOctave();
}

Gesture::Contact *Gesture::find(int id) {
    for (auto &c : m_contacts)
        if (c.live && c.id == id)
            return &c;
    return nullptr;
}

bool Gesture::isPalm(const Touch &t) const { return t.size > kPalmSize; }

Gesture::LayerState *Gesture::layerState(int group) {
    for (auto &l : m_layers)
        if (l.group == group)
            return &l;
    return nullptr;
}

void Gesture::sendKnob(int row, int role) {
    Command k;
    k.type = Cmd::Knob;
    k.i1 = role;
    k.i2 = rowRole(role) ? row : -1;
    k.f[0] = knob(row, role)[0];
    k.f[1] = knob(row, role)[1];
    send(k);
}

void Gesture::notice(const char *text) {
    std::snprintf(m_notice, sizeof(m_notice), "%s", text);
    ++m_noticeSeq;
}

void Gesture::frame(const Frame &f) {
    std::lock_guard lock(m_viewLock);
    const double dt = m_lastFrame > 0 ? std::clamp(f.t - m_lastFrame, 0.001, 0.05) : 0.008;
    m_lastFrame = f.t;
    m_now = f.t;

    bool seen[16] = {};
    for (int i = 0; i < f.count; ++i) {
        const Touch &t = f.touches[i];
        Contact *c = find(t.id);
        if (!c) {
            // A finger the pad lost for a frame comes back with a new id;
            // pick up where it was rather than starting over.
            float best = 0.05f;
            for (auto &o : m_contacts) {
                if (!o.live || o.missingSince < 0)
                    continue;
                // Only if it carries on as the same finger would: a fresh tap
                // on the same spot lands light and small, a finger the pad
                // merely lost comes back as it was.
                if (std::fabs(t.pressure - o.pressure) > 0.3f || std::fabs(t.size - o.size) > 0.4f * std::max(o.size, 0.3f))
                    continue;
                // And only within the lift grace: a pad can lose a held finger
                // for a few frames. A fresh tap gives itself away by landing
                // light and small, which the check above already catches.
                if (f.t - o.missingSince > kLiftGrace)
                    continue;
                const float dx = (o.x - t.x) * m_aspect, dy = o.y - t.y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < best) {
                    best = d;
                    c = &o;
                }
            }
            if (c)
                c->id = t.id;
        }
        if (c) {
            seen[c - m_contacts.data()] = true;
            c->missingSince = -1;
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
    for (auto &c : m_contacts) {
        if (!c.live || seen[&c - m_contacts.data()])
            continue;
        // A tap's lift is immediate (its timing is the rhythm); anything held
        // gets a moment's grace, so a dropped frame doesn't cut a voice.
        if (c.state == TouchState::Landing || c.state == TouchState::Palm || c.state == TouchState::Swipe)
            lift(c, f.t);
        else if (c.missingSince < 0)
            c.missingSince = f.t;
    }
    processLifts(f.t);

    for (auto &c : m_contacts) {
        if (!c.live)
            continue;
        if (c.state == TouchState::Swipe) {
            if (f.t - c.swipeStart > 0.5) {
                // Still down: not a flick but a chord being placed and moved.
                m_swipe.active = false;
                becomeVoice(c);
            }
            continue;
        }
        if (c.state != TouchState::Landing)
            continue;
        // Strike on the second frame, when the pressure has had a moment to
        // say how hard the finger came down. A contact that is still growing
        // may be a palm arriving: give it a few more milliseconds to decide.
        const double age = f.t - c.t0;
        if (!c.struck) {
            const bool growing = c.size > 1.5f || c.size - c.firstSize > 0.25f;
            if ((!growing && (c.frames >= 2 || age >= 0.012)) || age >= 0.036) {
                if (c.size > kPalmSize)
                    c.state = TouchState::Palm;
                else
                    strike(c);
            }
        }
        if (c.state == TouchState::Landing && age >= kHoldTime)
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
            maxGap = std::max(maxGap, float(m_phrase[i].t - m_phrase[i - 1].t) * 2.3f);
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

void Gesture::idle(double t) {
    std::lock_guard lock(m_viewLock);
    if (t <= m_now)
        return;
    bool any = false;
    for (auto &c : m_contacts)
        any |= c.live && c.missingSince >= 0;
    if (!any)
        return;
    processLifts(t);
    publish();
}

void Gesture::processLifts(double now) {
    for (auto &c : m_contacts)
        if (c.live && c.missingSince >= 0 && now - c.missingSince >= kLiftGrace)
            lift(c, c.missingSince);
}

void Gesture::land(Contact &c, const Touch &t, double now) {
    c = Contact{};
    c.live = true;
    c.id = t.id;
    c.t0 = c.last = now;
    c.x0 = c.x = t.x;
    c.y0 = c.y = t.y;
    c.size = c.peakSize = t.size;
    c.pressure = c.peakPressure = t.pressure;
    c.firstSize = t.size;
    c.vx = std::cos(2 * t.angle);
    c.vy = std::sin(2 * t.angle);
    c.smoothAngle = c.refAngle = t.angle;
    c.frames = 1;
    c.row = std::clamp(int(t.y * float(RowCount)), 0, RowCount - 1);
    m_focusRow = c.row;
    c.value = c.row == DrumRow ? drumPieceAt(t.x) : strikeDegree(t.x);
    c.slowDeg = degreeAt(t.x);
    c.state = TouchState::Landing;
}

float Gesture::twistOf(const Contact &c) const {
    const float d = wrapHalfTurn(c.smoothAngle - c.refAngle);
    const float mag = std::max(0.0f, std::fabs(d) - kTwistDeadband);
    return d < 0 ? -mag : mag;
}

void Gesture::move(Contact &c, const Touch &t, double now, double dt) {
    const float dx = (t.x - c.x) * m_aspect, dy = t.y - c.y;
    c.travel += std::sqrt(dx * dx + dy * dy);
    c.x = t.x;
    c.y = t.y;
    c.size = t.size;
    c.peakSize = std::max(c.peakSize, t.size);
    c.pressure = t.pressure;
    c.last = now;
    ++c.frames;
    if (!c.struck)
        c.peakPressure = std::max(c.peakPressure, t.pressure);

    // Twist: the contact ellipse's angle, which only means anything while the
    // contact is long enough to have a direction. Each frame nudges a running
    // average of the angle (as a vector at twice the angle, since an ellipse
    // turned half a turn is the same ellipse) by as much as it can be
    // trusted. The twist is that average measured from where it was when the
    // knob was picked up, with a small deadband. Nothing is summed, so noise
    // can't walk it anywhere.
    const float trust = std::clamp((t.eccentricity - 0.1f) / 0.3f, 0.0f, 1.0f);
    const float k = float(1.0 - std::exp(-dt / 0.06)) * trust;
    c.vx += (std::cos(2 * t.angle) - c.vx) * k;
    c.vy += (std::sin(2 * t.angle) - c.vy) * k;
    if (c.vx * c.vx + c.vy * c.vy > 1e-4f)
        c.smoothAngle = 0.5f * std::atan2(c.vy, c.vx);

    // A palm can arrive slowly; catch it whenever it becomes one.
    c.bigFrames = t.size > (c.state == TouchState::Voice ? kPalmFromVoice : kPalmSize) ? c.bigFrames + 1 : 0;
    if (c.bigFrames >= 5 && (c.state == TouchState::Landing || c.state == TouchState::Voice)) {
        stopSound(c);
        c.role = -1;
        c.state = TouchState::Palm;
        c.struck = true;
    }

    const float twist = twistOf(c);
    if ((c.state == TouchState::Voice || c.state == TouchState::Spent) && c.role >= 0) {
        const float a = std::clamp(c.refKnob[0] + slideOf(c.y - c.refY) * kSlideGain, 0.0f, 1.0f);
        const float b = std::clamp(c.refKnob[1] + twist / kTwistRange, 0.0f, 1.0f);
        float *k = knob(c.row, c.role);
        if (std::fabs(a - k[0]) > 0.001f || std::fabs(b - k[1]) > 0.001f) {
            k[0] = a;
            k[1] = b;
            sendKnob(c.row, c.role);
            m_focusRow = c.row;
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
    if (c.state == TouchState::Spent && c.role >= 0)
        recordMotion(c, now, false);
    if (c.state == TouchState::Voice || (c.state == TouchState::Landing && c.sounding)) {
        if (c.row == DrumRow) {
            if (c.rolling) {
                if (c.pressure > 0.75f)
                    m_rollStrong += float(dt);
                Command v;
                v.type = Cmd::RollMove;
                v.id = c.id;
                v.f[0] = c.pressure;
                send(v);
            }
        } else {
            c.midi = voicePitch(c, float(dt));
            const int degree = voiceDegree(c);
            if (c.row == ChordRow && c.state == TouchState::Voice && c.lastDegree >= 0 && degree != c.lastDegree)
                ++m_chordMoves;
            c.lastDegree = degree;
            Command v;
            v.type = Cmd::NoteMove;
            v.id = c.id;
            v.i2 = degree;
            v.f[0] = c.midi;
            v.f[1] = c.pressure;
            v.f[2] = c.x;
            v.f[3] = c.y;
            send(v);
            if (c.state == TouchState::Voice)
                recordMotion(c, now, true);
        }
    } else if (c.state == TouchState::Handle) {
        Command m;
        m.type = Cmd::LoopMod;
        m.id = c.loop;
        m.i1 = int(std::lround((c.x - c.refX) * float(cells())));
        m.f[0] = (c.y - c.refY) * 4.0f;
        c.loopDensity = std::clamp(c.refDensity + twist / kTwistRange, -1.0f, 1.0f);
        m.f[1] = c.loopDensity;
        m.f[2] = 0.55f + 0.95f * c.pressure;
        m.f[3] = c.x;
        m.f[4] = c.y;
        send(m);
    } else if (c.state == TouchState::Layer) {
        if (LayerState *l = layerState(c.layer)) {
            l->transpose = int(std::lround((c.x - c.refX) * float(cells())));
            l->bright = std::clamp(c.refBright + slideOf(c.y - c.refY) * kSlideGain, 0.0f, 1.0f);
            l->width = std::clamp(c.refWidth + twist / kTwistRange, 0.0f, 1.0f);
            Command m;
            m.type = Cmd::LayerMod;
            m.id = c.layer;
            m.i1 = l->transpose;
            m.f[0] = l->bright;
            m.f[1] = l->width;
            m.f[2] = 0.35f + 1.3f * c.pressure; // press to swell it
            m.f[3] = c.x;
            m.f[4] = c.y;
            send(m);
        }
    }
}

void Gesture::recordMotion(Contact &c, double now, bool voice) {
    // Held while a loop is held: this finger goes into the loop, as a voice
    // line, or (for a finger whose chord froze) as its knob's sweep alone.
    bool handleHeld = false;
    for (auto &o : m_contacts)
        if (o.live && o.state == TouchState::Handle && o.loop == m_activeHandle)
            handleHeld = true;
    if (!handleHeld || m_activeHandle <= 0)
        return;
    if (c.laneLoop != m_activeHandle) {
        int &used = m_lanesUsed[m_activeHandle & 255];
        c.lane = used < kMaxLanes ? used++ : -1;
        c.laneLoop = m_activeHandle;
    }
    if (c.lane < 0)
        return;
    Command m;
    m.type = Cmd::Motion;
    m.id = m_activeHandle;
    m.i1 = c.lane;
    m.i2 = c.role;
    m.i3 = voice ? c.row : -1;
    m.t = now;
    m.f[0] = c.midi;
    m.f[1] = c.pressure;
    m.f[2] = c.role >= 0 ? knob(c.row, c.role)[0] : 0;
    m.f[3] = c.role >= 0 ? knob(c.row, c.role)[1] : 0;
    m.f[4] = float(voiceDegree(c));
    m.f[5] = c.x;
    send(m);
}

void Gesture::finishSwipe(Contact &c, double now) {
    // A swipe is a flick: three or more fingers that landed together, moved
    // together, and all left the pad within half a second. Decided when the
    // last of them lifts.
    (void)now;
    if (!m_swipe.active || std::fabs(c.swipeStart - m_swipe.start) > 1e-9)
        return;
    m_swipe.sx += (c.x - c.x0) * m_aspect;
    m_swipe.sy += c.y - c.y0;
    if (++m_swipe.lifted < m_swipe.members)
        return;
    m_swipe.active = false;
    const float sx = m_swipe.sx / float(m_swipe.members), sy = m_swipe.sy / float(m_swipe.members);
    if (std::sqrt(sx * sx + sy * sy) < kSwipeDistance)
        return;
    char text[64];
    if (std::fabs(sx) > std::fabs(sy)) {
        if (m_swipe.members >= 4) {
            doShiftFamily(sx > 0 ? 1 : -1);
            std::snprintf(text, sizeof(text), "%s", m_tuning.familyName());
        } else {
            doShiftRoot(sx > 0 ? 1 : -1);
            std::snprintf(text, sizeof(text), "%s", Tuning::noteName(m_tuning.root()));
        }
    } else {
        doShiftOctave(sy > 0 ? 1 : -1);
        std::snprintf(text, sizeof(text), "octave %+d", m_octave);
    }
    notice(text);
    ++m_swipes;
    // The fingers struck as they landed; a gesture shouldn't leave notes.
    Command h;
    h.type = Cmd::Hush;
    h.f[0] = 0.7f;
    send(h);
}

int Gesture::voiceDegree(const Contact &c) const {
    return int(std::lround(c.slowDeg)) + m_octave * m_tuning.degreesPerOctave();
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
    const int shift = m_octave * m_tuning.degreesPerOctave();
    const double snapped = m_tuning.degreeSemis(int(std::lround(c.slowDeg)) + shift) +
                           (xd - c.slowDeg) * m_tuning.semisPerDegree(c.slowDeg + shift);
    const double cont = m_tuning.continuousSemis(xd + shift);
    const double w = knob(0, Gravity)[0] * (1.0f - c.motion);
    return float(m_tuning.baseMidi() + rowOffset(c.row) + cont + (snapped - cont) * w);
}

void Gesture::strike(Contact &c) {
    // The finger's sound starts the moment it lands: a drum hit on the drum
    // row, a note on the others (a tap is just a short note).
    c.struck = true;
    if (c.state == TouchState::Palm)
        return;
    c.vel = std::clamp(0.25f + 0.85f * c.peakPressure, 0.15f, 1.0f);
    if (c.row == DrumRow)
        ++m_hits[std::clamp(c.value, 0, kDrumPieces - 1)];
    else
        ++m_notes[c.row];
    if (c.row == DrumRow) {
        Command h;
        h.type = Cmd::Hit;
        h.i1 = c.value;
        h.f[0] = c.vel;
        h.f[1] = c.x0;
        send(h);
    } else {
        Command n;
        n.type = Cmd::NoteOn;
        n.id = c.id;
        n.i1 = c.row;
        n.i2 = c.value;
        n.f[0] = float(m_tuning.baseMidi() + rowOffset(c.row) + m_tuning.degreeSemis(c.value));
        n.f[1] = c.vel;
        n.f[2] = c.x0;
        n.f[3] = c.y0;
        send(n);
        c.sounding = true;
    }
    m_strikes[m_strikeSeq % m_strikes.size()] = {c.x0, c.y0, c.vel, float(c.row), c.t0};
    ++m_strikeSeq;
}

void Gesture::stopSound(Contact &c) {
    if (c.rolling) {
        Command o;
        o.type = Cmd::RollOff;
        o.id = c.id;
        send(o);
        c.rolling = false;
    }
    if (c.sounding) {
        Command o;
        o.type = Cmd::NoteOff;
        o.id = c.id;
        send(o);
        c.sounding = false;
    }
}

void Gesture::hold(Contact &c, double now) {
    // Three or more fingers that landed together and are already moving:
    // perhaps a swipe. Wait a little longer before deciding.
    if (c.state == TouchState::Landing) {
        int together = 0;
        for (auto &o : m_contacts)
            if (o.live && (o.state == TouchState::Landing || o.state == TouchState::Swipe) &&
                std::fabs(o.t0 - c.t0) < 0.09)
                ++together;
        const float mx = (c.x - c.x0) * m_aspect, my = c.y - c.y0;
        if (together >= 3 && std::sqrt(mx * mx + my * my) > 0.04f && !m_swipe.active) {
            m_swipe = {true, c.t0, 0, 0, 0, 0};
            for (auto &o : m_contacts)
                if (o.live && (o.state == TouchState::Landing || o.state == TouchState::Swipe) &&
                    std::fabs(o.t0 - c.t0) < 0.09) {
                    o.state = TouchState::Swipe;
                    o.swipeStart = c.t0;
                    ++m_swipe.members;
                }
            return;
        }
    }

    if (!m_phrase.empty()) {
        const Tap &last = m_phrase.back();
        const Tap &first = m_phrase.front();
        const double gap = c.t0 - last.t;
        const float lx = (c.x0 - last.x) * m_aspect, ly = c.y0 - last.y;
        const float fx = (c.x0 - first.x) * m_aspect, fy = c.y0 - first.y;
        const float nearLast = std::sqrt(lx * lx + ly * ly), nearFirst = std::sqrt(fx * fx + fy * fy);

        // A single quick tap and hold on top of a loop or a frozen chord
        // picks it up again.
        if (m_phrase.size() == 1 && gap > 0 && gap < 0.45 && nearLast < 0.08f) {
            const int id = m_sink->loopNear(c.x, c.y, 0.14f);
            if (id >= 0) {
                stopSound(c);
                c.state = TouchState::Handle;
                c.loop = id;
                c.refX = c.x;
                c.refY = c.y;
                c.refAngle = c.smoothAngle;
                c.refDensity = 0;
                Command g;
                g.type = Cmd::LoopGrab;
                g.id = id;
                send(g);
                m_activeHandle = id;
                m_phrase.clear();
                ++m_grabs;
                return;
            }
            const int group = m_sink->layerNear(c.x, c.y, 0.14f);
            if (group >= 0) {
                stopSound(c);
                LayerState *l = layerState(group);
                if (!l) {
                    m_layers.push_back({group, 0, knob(ChordRow, Filter)[0], knob(ChordRow, Width)[0]});
                    l = &m_layers.back();
                }
                c.state = TouchState::Layer;
                c.layer = group;
                c.refX = c.x - float(l->transpose) / float(cells());
                c.refY = c.y;
                c.refAngle = c.smoothAngle;
                c.refBright = l->bright;
                c.refWidth = l->width;
                Command g;
                g.type = Cmd::LayerGrab;
                g.id = group;
                send(g);
                m_phrase.clear();
                ++m_grabs;
                return;
            }
        }

        // Does this landing close the phrase? It has to sound like the next
        // beat: as far after the last tap as some gap inside the phrase (or
        // twice that), or land back where the phrase began. A single tap
        // only loops with a hold right on top of it (a steady pulse).
        // A finger laid down flat or barely pressing is reaching for a
        // drone, never closing a rhythm.
        bool sameRow = false;
        for (const Tap &t : m_phrase)
            sameRow |= t.row == c.row;
        // ...and only a finger on the phrase's own row closes it: a chord
        // landing on the beat after a bass line is a chord.
        const bool voiceFirst = c.peakSize > 1.5f || c.peakPressure < 0.18f || !sameRow;
        bool closes = false;
        if (gap > 0 && gap <= 2.2 && !voiceFirst) {
            if (m_phrase.size() == 1) {
                closes = nearLast < 0.08f && gap >= 0.12;
            } else {
                // Twice an interval only counts for an even pulse.
                double mean = 0, var = 0;
                const int n = int(m_phrase.size()) - 1;
                for (int i = 1; i <= n; ++i)
                    mean += m_phrase[i].t - m_phrase[i - 1].t;
                mean /= n;
                for (int i = 1; i <= n; ++i)
                    var += std::pow(m_phrase[i].t - m_phrase[i - 1].t - mean, 2);
                const bool even = n >= 2 && std::sqrt(var / n) < 0.12 * mean;
                for (int i = 1; i <= n && !closes; ++i) {
                    const double ioi = m_phrase[i].t - m_phrase[i - 1].t;
                    closes = std::fabs(gap - ioi) < 0.15 * ioi || (even && std::fabs(gap - 2 * ioi) < 0.15 * ioi);
                }
                closes = closes || (nearFirst < 0.07f && gap <= 2.0 * mean + 0.2);
            }
        }
        const double length = c.t0 - first.t;
        if (closes && length >= 0.12) {
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
                h.f[0] = float(t.t - first.t);
                h.i1 = t.row;
                h.i2 = t.value;
                h.f[1] = t.vel;
                h.f[2] = t.x;
                h.f[3] = t.length;
                send(h);
            }
            Command r;
            r.type = Cmd::LoopReady;
            r.id = id;
            send(r);
            // The finger that closed the phrase is the loop's handle now, not
            // a note.
            stopSound(c);
            c.state = TouchState::Handle;
            c.loop = id;
            c.refX = c.x;
            c.refY = c.y;
            c.refAngle = c.smoothAngle;
            c.refDensity = 0;
            m_activeHandle = id;
            for (const Tap &t : m_phrase)
                m_loopRows |= 1u << t.row;
            m_phrase.clear();
            ++m_loopsMade;
            return;
        }
    }
    becomeVoice(c);
}

void Gesture::becomeVoice(Contact &c) {
    // A held finger keeps its note sounding (or, on the drum row, rolls) and
    // takes the lowest role no other finger on the pad holds, including
    // fingers that latched a chord and stayed down.
    bool taken[RoleCount] = {};
    for (auto &o : m_contacts)
        if (o.live && (o.state == TouchState::Voice || o.state == TouchState::Spent) && o.role >= 0)
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
    c.refAngle = c.smoothAngle;
    if (role >= 0) {
        c.refKnob[0] = knob(c.row, role)[0];
        c.refKnob[1] = knob(c.row, role)[1];
    }
    m_focusRow = c.row;
    if (!c.struck)
        strike(c);
    if (c.row == DrumRow) {
        Command r;
        r.type = Cmd::RollOn;
        r.id = c.id;
        r.i1 = c.value;
        r.f[0] = c.pressure;
        r.f[1] = c.x;
        send(r);
        c.rolling = true;
    }
}

void Gesture::lift(Contact &c, double now) {
    switch (c.state) {
    case TouchState::Landing: {
        if (!c.struck)
            strike(c);
        stopSound(c);
        const float length = float(std::max(0.03, now - c.t0));
        bool handleHeld = false;
        for (auto &o : m_contacts)
            if (o.live && o.state == TouchState::Handle && o.loop == m_activeHandle)
                handleHeld = true;
        if (handleHeld) {
            Command o;
            o.type = Cmd::Overdub;
            o.id = m_activeHandle;
            o.t = c.t0;
            o.i1 = c.row;
            o.i2 = c.value;
            o.f[0] = c.vel;
            o.f[1] = c.x0;
            o.f[2] = length;
            send(o);
            ++m_overdubs;
        } else {
            m_phrase.push_back({c.t0, c.x0, c.y0, c.row, c.value, c.vel, length});
            while (m_phrase.size() > 64 || (m_phrase.size() > 1 && c.t0 - m_phrase.front().t > 8.0))
                m_phrase.erase(m_phrase.begin());
        }
        break;
    }
    case TouchState::Voice:
        stopSound(c);
        break;
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
    case TouchState::Layer: {
        Command r;
        r.type = Cmd::LayerRelease;
        r.id = c.layer;
        send(r);
        break;
    }
    case TouchState::Swipe:
        finishSwipe(c, now);
        break;
    case TouchState::Palm:
    case TouchState::Spent:
        break;
    }
    c.live = false;
}

void Gesture::click(double now) {
    (void)now;
    bool palm = false, handle = false, voice = false, layer = false;
    for (auto &c : m_contacts) {
        if (!c.live)
            continue;
        palm |= c.state == TouchState::Palm;
        handle |= c.state == TouchState::Handle;
        layer |= c.state == TouchState::Layer;
        voice |= c.state == TouchState::Voice;
    }
    if (palm) {
        if (!m_palmClicked) {
            Command d;
            d.type = Cmd::DissolveAll;
            send(d);
            m_layers.clear();
            m_palmClicked = true;
        }
        return;
    }
    if (handle || layer) {
        for (auto &c : m_contacts) {
            if (!c.live)
                continue;
            Command p;
            if (c.state == TouchState::Handle) {
                p.type = Cmd::LoopPin;
                p.id = c.loop;
                send(p);
            } else if (c.state == TouchState::Layer) {
                p.type = Cmd::LayerPin;
                p.id = c.layer;
                send(p);
            }
        }
        return;
    }
    if (voice) {
        const int group = m_nextGroup++;
        Command f;
        f.type = Cmd::Freeze;
        f.id = group;
        send(f);
        m_layers.push_back({group, 0, knob(ChordRow, Filter)[0], knob(ChordRow, Width)[0]});
        if (m_layers.size() > 64)
            m_layers.erase(m_layers.begin());
        // The fingers go quiet but keep their roles (and keep turning their
        // knobs) until they lift.
        for (auto &c : m_contacts)
            if (c.live && c.state == TouchState::Voice) {
                // Latched notes ring on in the engine; a drum roll stops.
                if (c.rolling)
                    stopSound(c);
                c.sounding = false;
                c.state = TouchState::Spent;
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

void Gesture::doShiftRoot(int fifths) {
    m_tuning.set(m_tuning.root() + 7 * fifths, m_tuning.familyIndex(), m_tuning.brightnessIndex(), m_tuning.just());
    sendTuning();
}

void Gesture::doShiftFamily(int step) {
    const int fam = (m_tuning.familyIndex() + step + familyCount()) % familyCount();
    m_tuning.set(m_tuning.root(), fam, defaultBrightness(fam), m_tuning.just());
    const int modes = family(fam).modeCount;
    knob(0, Gravity)[1] = float(m_tuning.brightnessIndex()) / float(std::max(1, modes - 1));
    sendKnob(0, Gravity);
    sendTuning();
}

void Gesture::doShiftOctave(int step) { m_octave = std::clamp(m_octave + step, -2, 2); }

void Gesture::shiftRoot(int fifths) {
    std::lock_guard lock(m_viewLock);
    doShiftRoot(fifths);
    publish();
}

void Gesture::shiftFamily(int step) {
    std::lock_guard lock(m_viewLock);
    doShiftFamily(step);
    publish();
}

void Gesture::shiftOctave(int step) {
    std::lock_guard lock(m_viewLock);
    doShiftOctave(step);
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
    for (int row = 0; row < RowCount; ++row)
        for (int r = 0; r < RoleCount; ++r)
            for (int a = 0; a < 2; ++a)
                m_knob[row][r][a] = rowDefault(row, r, a);
    knob(0, Gravity)[1] = float(m_tuning.brightnessIndex()) /
                          float(std::max(1, family(m_tuning.familyIndex()).modeCount - 1));
    for (int r = 0; r < RoleCount; ++r) {
        if (rowRole(r))
            for (int row = 0; row < RowCount; ++row)
                sendKnob(row, r);
        else
            sendKnob(0, r);
    }
    // Fingers still holding a role pick up from the new values.
    for (auto &c : m_contacts)
        if (c.live && (c.state == TouchState::Voice || c.state == TouchState::Spent) && c.role >= 0) {
            c.refY = c.y;
            c.refAngle = c.smoothAngle;
            c.refKnob[0] = knob(c.row, c.role)[0];
            c.refKnob[1] = knob(c.row, c.role)[1];
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
    m_layers.clear();
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
        o.loop = c.state == TouchState::Layer ? c.layer : c.loop;
        o.recording = c.state == TouchState::Voice && c.lane >= 0 && c.laneLoop == m_activeHandle;
        o.x = c.x;
        o.y = c.y;
        o.size = c.size;
        o.pressure = c.pressure;
        o.angle = c.smoothAngle;
        o.twist = c.state == TouchState::Handle ? c.loopDensity : twistOf(c);
        o.age = float(m_now - c.t0);
    }
    v.phraseCount = 0;
    for (const Tap &t : m_phrase)
        if (v.phraseCount < int(v.phrase.size()))
            v.phrase[v.phraseCount++] = {t.x, t.y, t.t};
    v.phraseOpen = !m_phrase.empty();
    v.phraseNext = 0;
    if (m_phrase.size() >= 2)
        v.phraseNext = m_phrase.back().t + (m_phrase.back().t - m_phrase[m_phrase.size() - 2].t);
    for (int r = 0; r < RoleCount; ++r) {
        v.knobs[r][0] = knob(m_focusRow, r)[0];
        v.knobs[r][1] = knob(m_focusRow, r)[1];
    }
    v.focusRow = m_focusRow;
    v.damper = m_damper;
    v.strikeSeq = m_strikeSeq;
    v.strikes = m_strikes;
    v.t = m_now;
    v.root = m_tuning.root();
    v.family = m_tuning.familyIndex();
    v.brightness = m_tuning.brightnessIndex();
    v.octave = m_octave;
    v.just = m_tuning.just();
    v.loopsMade = m_loopsMade;
    v.freezes = m_freezes;
    v.swipes = m_swipes;
    v.grabs = m_grabs;
    for (int i = 0; i < kDrumPieces; ++i)
        v.hits[i] = m_hits[i];
    for (int r = 0; r < RowCount; ++r) {
        v.notes[r] = m_notes[r];
        v.heldSeconds[r] = 0;
    }
    for (auto &c : m_contacts)
        if (c.live && c.state == TouchState::Voice)
            v.heldSeconds[c.row] = std::max(v.heldSeconds[c.row], float(m_now - c.t0));
    v.overdubs = m_overdubs;
    v.chordMoves = m_chordMoves;
    v.loopRows = m_loopRows;
    v.rollStrong = m_rollStrong;
    v.noticeSeq = m_noticeSeq;
    std::memcpy(v.notice, m_notice, sizeof(v.notice));
}

GestureView Gesture::view() const {
    std::lock_guard lock(m_viewLock);
    return m_view;
}

}
