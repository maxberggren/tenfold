#pragma once

// Turns raw multitouch frames into music. This is the instrument's grammar:
//
//   land      every finger strikes the surface where it lands: X is the note,
//             Y the material (skin, wood, glass, air), pressure the force.
//   hold      a finger that stays blooms into a sustained voice, and takes
//             the next free role (Tone, Shadow, Air, Space, …). Sliding it up
//             and down turns that role's first knob, twisting it the second.
//             Knobs stay where you leave them.
//   tap…hold  tap a rhythm, then land on the next "one" and stay: the taps
//             become a loop. While you hold, slide to transpose it, move up
//             or down to change its material, twist to thin it out or roll it
//             into ratchets, press to accent. Taps with other fingers while
//             you hold are added to it. Let go and it keeps playing, eroding
//             slowly like old tape.
//   tap, hold on a loop   grabs that loop again.
//   press     (click the pad) holds on to what you are touching: held voices
//             freeze into the landscape, a held loop is pinned and stops
//             eroding.
//   palm      a resting palm damps everything, like a hand on strings. Palm
//             and press clears the landscape.

#include <array>
#include <mutex>
#include <vector>

#include "engine.h"
#include "tuning.h"

namespace tf {

struct Touch {
    int id = -1;
    float x = 0, y = 0;         // 0..1, y up
    float size = 0;             // contact length, relative to a normal fingertip (≈1)
    float pressure = 0;         // 0..1
    float angle = 0;            // radians, the contact ellipse's orientation
    float eccentricity = 0;     // 0 round .. 1 long; how much to trust the angle
};

struct Frame {
    double t = 0; // seconds
    bool button = false;
    int count = 0;
    std::array<Touch, 16> touches{};
};

enum class TouchState : uint8_t { Landing, Voice, Handle, Palm, Spent };

struct ContactView {
    int id = -1;
    TouchState state = TouchState::Landing;
    int role = -1, loop = -1;
    float x = 0, y = 0, size = 0, pressure = 0, angle = 0, twist = 0;
    float age = 0;
};

struct GestureView {
    std::array<ContactView, 16> contacts{};
    int count = 0;
    struct Tap {
        float x, y;
        double t;
    };
    std::array<Tap, 64> phrase{};
    int phraseCount = 0;
    bool phraseOpen = false; // landing now and holding would make a loop
    float knobs[RoleCount][2] = {};
    float damper = 0;
    uint32_t strikeSeq = 0;
    struct Strike {
        float x, y, vel, material;
        double t;
    };
    std::array<Strike, 32> strikes{}; // ring, newest at strikeSeq - 1
    double t = 0;
    int root = 2, family = 1, brightness = 3;
    bool just = true;
    int loopsMade = 0, freezes = 0;
};

// Where commands go: the live engine, or a recorder in tests.
class CommandSink {
public:
    virtual ~CommandSink() = default;
    virtual void command(const Command &c) = 0;
    // Live loops the gesture layer may grab again.
    virtual int loopNear(float x, float y, float radius) const = 0;
};

class Gesture {
public:
    explicit Gesture(CommandSink *sink);

    // Pad shape, width over height, so distances on the pad are round.
    void setAspect(float a) { m_aspect = a; }

    void frame(const Frame &f);

    // Keyboard and menu actions.
    void shiftRoot(int fifths);
    void shiftFamily(int step);
    void toggleJust();
    void resetKnobs();
    void dissolve();
    void dissolveAll();

    GestureView view() const;

    // Layout: how many scale degrees the pad spans, and helpers to map.
    int cells() const;
    float degreeAt(float x) const; // continuous, cell centres on integers
    int strikeDegree(float x) const;
    const Tuning &tuning() const { return m_tuning; }

    static constexpr double kHoldTime = 0.19;
    static constexpr int kOctaves = 3;

private:
    struct Contact {
        bool live = false;
        int id = -1;
        TouchState state = TouchState::Landing;
        double t0 = 0, last = 0;
        float x0 = 0, y0 = 0, x = 0, y = 0;
        float size = 0, pressure = 0, peakPressure = 0;
        float angle = 0, twist = 0, travel = 0;
        int frames = 0;
        bool struck = false;
        int degree0 = 0;
        float material0 = 0, vel = 0;
        int role = -1;
        float refY = 0, refTwist = 0, refKnob[2] = {};
        int loop = -1;
        float refX = 0, refDensity = 0;
        float slowDeg = 0, slowSpeed = 0, motion = 0;
        float loopDensity = 0;
    };
    struct Tap {
        double t;
        float x, y;
        int degree;
        float material, vel;
    };

    Contact *find(int id);
    void land(Contact &c, const Touch &t, double now);
    void move(Contact &c, const Touch &t, double now, double dt);
    void lift(Contact &c, double now);
    void hold(Contact &c, double now);
    void strike(Contact &c);
    void click(double now);
    void sendTuning();
    float voicePitch(Contact &c, double dt);
    void send(const Command &c) { m_sink->command(c); }
    bool isPalm(const Touch &t) const;

    CommandSink *m_sink;
    float m_aspect = 1.6f;
    Tuning m_tuning;
    float m_knob[RoleCount][2];
    std::array<Contact, 16> m_contacts{};
    std::vector<Tap> m_phrase;
    bool m_button = false;
    int m_nextLoop = 1;
    float m_damper = 0;
    double m_now = 0, m_lastFrame = 0;
    int m_activeHandle = -1; // the loop overdubs go into
    int m_loopsMade = 0, m_freezes = 0;
    bool m_palmClicked = false;

    mutable std::mutex m_viewLock;
    GestureView m_view;
    uint32_t m_strikeSeq = 0;
    std::array<GestureView::Strike, 32> m_strikes{};
    void publish();
};

}
