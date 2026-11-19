#pragma once

// Turns raw multitouch frames into music. This is the instrument's grammar:
//
//   land      every finger strikes the surface where it lands: X is the note,
//             Y the material (skin, snare, wood, glass, air), force and
//             contact size the mallet.
//   hold      a finger that stays blooms into a sustained voice and takes the
//             next free role (Tone, Shadow, Air, Space, …). Sliding it up and
//             down turns that role's first knob, twisting it the second.
//             Knobs stay where you leave them.
//   tap…hold  tap a rhythm, then land on the next "one" and stay: the taps
//             become a loop. While you hold it, slide to transpose, move up or
//             down to change material, twist to thin out or roll into
//             ratchets, press to accent. Everything other fingers do while you
//             hold it goes into it: taps become hits, held fingers become
//             recorded voice lines and knob sweeps that replay every bar.
//   tap, hold on a ring    picks a loop up again; on a frozen glow, picks up
//             that frozen chord to transpose it, brighten it, widen it or
//             swell it.
//   press     (click the pad) keeps what you are touching: held voices freeze
//             into the landscape, a held loop or layer is pinned. Fingers that
//             froze a chord keep their roles while they stay down, so the
//             next finger reaches the next knob without another note.
//   swipe     three fingers together: sideways moves the key by a fifth, up or
//             down the octave. Four fingers sideways: the scale family.
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

enum class TouchState : uint8_t { Landing, Voice, Handle, Layer, Palm, Spent, Swipe };

struct ContactView {
    int id = -1;
    TouchState state = TouchState::Landing;
    int role = -1, loop = -1;
    bool recording = false;
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
    bool phraseOpen = false;     // landing on the beat and holding would make a loop
    double phraseNext = 0;       // when the next beat of the phrase would land
    float knobs[RoleCount][2] = {}; // as they stand on focusRow
    int focusRow = LeadRow;
    float damper = 0;
    uint32_t strikeSeq = 0;
    struct Strike {
        float x, y, vel, material;
        double t;
    };
    std::array<Strike, 32> strikes{}; // ring, newest at strikeSeq - 1
    double t = 0;
    int root = 2, family = 1, brightness = 3, octave = 0;
    bool just = true;
    int loopsMade = 0, freezes = 0, swipes = 0, grabs = 0;
    // Running counts, for the tutorial to see what the player has done.
    int hits[kDrumPieces] = {};
    int notes[RowCount] = {};
    int overdubs = 0, chordMoves = 0;
    unsigned loopRows = 0;           // a bit per row that has been looped
    float rollStrong = 0;            // seconds rolled with a hard press
    float heldSeconds[RowCount] = {}; // longest finger held right now, per row
    uint32_t noticeSeq = 0;
    char notice[64] = {}; // the last thing a swipe changed, for the screen
};

// Where commands go: the live engine, or a recorder in tests.
class CommandSink {
public:
    virtual ~CommandSink() = default;
    virtual void command(const Command &c) = 0;
    // Live loops and frozen layers the gesture layer may grab again.
    virtual int loopNear(float x, float y, float radius) const = 0;
    virtual int layerNear(float x, float y, float radius) const { (void)x, (void)y, (void)radius; return -1; }
};

class Gesture {
public:
    explicit Gesture(CommandSink *sink);

    // Pad shape, width over height, so distances on the pad are round.
    void setAspect(float a) { m_aspect = a; }

    void frame(const Frame &f);
    // Lets lifts that were held back (in case the finger was only lost for a
    // frame) go through when no frames arrive; the live app calls it on a timer.
    void idle(double t);

    // Keyboard and menu actions.
    void shiftRoot(int fifths);
    void shiftFamily(int step);
    void shiftOctave(int step);
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
    static constexpr double kLiftGrace = 0.035;
    static constexpr int kOctaves = 3;

private:
    struct Contact {
        bool live = false;
        int id = -1;
        TouchState state = TouchState::Landing;
        double t0 = 0, last = 0, missingSince = -1, swipeStart = 0;
        float x0 = 0, y0 = 0, x = 0, y = 0;
        float size = 0, pressure = 0, peakPressure = 0, peakSize = 0;
        // Orientation as a doubled-angle vector, averaged with each frame
        // weighted by how elongated the contact is: no accumulation, so
        // nothing to drift.
        float vx = 1, vy = 0, smoothAngle = 0, refAngle = 0;
        float firstSize = 0;
        float travel = 0;
        int frames = 0, bigFrames = 0;
        bool struck = false;
        int row = 0, value = 0; // value: the scale degree, or the drum piece
        float vel = 0;
        bool sounding = false, rolling = false;
        int role = -1;
        float refY = 0, refKnob[2] = {};
        int loop = -1, layer = -1;
        float refX = 0, refDensity = 0, refBright = 0, refWidth = 0;
        float slowDeg = 0, slowSpeed = 0, motion = 0;
        float loopDensity = 0;
        int lane = -1, laneLoop = -1;
        int lastDegree = -1;
        float midi = 0;
    };
    struct Tap {
        double t;
        float x, y;
        int row, value;
        float vel, length;
    };
    struct SwipeGroup {
        bool active = false;
        double start = 0;
        int members = 0, lifted = 0;
        float sx = 0, sy = 0;
    };
    struct LayerState {
        int group = -1, transpose = 0;
        float bright = 0.5f, width = 0.3f;
    };

    Contact *find(int id);
    void land(Contact &c, const Touch &t, double now);
    void move(Contact &c, const Touch &t, double now, double dt);
    void lift(Contact &c, double now);
    void hold(Contact &c, double now);
    void becomeVoice(Contact &c);
    void strike(Contact &c);
    void stopSound(Contact &c);
    void click(double now);
    void finishSwipe(Contact &c, double now);
    void recordMotion(Contact &c, double now, bool voice);
    void processLifts(double now);
    void sendTuning();
    void sendKnob(int row, int role);
    void doShiftRoot(int fifths);
    void doShiftFamily(int step);
    void doShiftOctave(int step);
    void notice(const char *text);
    float twistOf(const Contact &c) const;
    float voicePitch(Contact &c, double dt);
    int voiceDegree(const Contact &c) const;
    void send(const Command &c) { m_sink->command(c); }
    bool isPalm(const Touch &t) const;
    LayerState *layerState(int group);

    CommandSink *m_sink;
    float m_aspect = 1.6f;
    Tuning m_tuning;
    int m_octave = 0;
    // Knob values per row; mix-wide roles live in row 0.
    float m_knob[RowCount][RoleCount][2];
    float *knob(int row, int role) { return m_knob[rowRole(role) ? std::clamp(row, 0, RowCount - 1) : 0][role]; }
    int m_focusRow = LeadRow; // the row the knob strip shows
    std::array<Contact, 16> m_contacts{};
    std::vector<Tap> m_phrase;
    std::vector<LayerState> m_layers;
    int m_lanesUsed[256] = {};
    bool m_button = false;
    int m_nextLoop = 1, m_nextGroup = 1;
    float m_damper = 0;
    double m_now = 0, m_lastFrame = 0;
    int m_activeHandle = -1; // the loop overdubs go into
    int m_loopsMade = 0, m_freezes = 0, m_swipes = 0, m_grabs = 0;
    bool m_palmClicked = false;
    int m_hits[kDrumPieces] = {}, m_notes[RowCount] = {};
    int m_overdubs = 0, m_chordMoves = 0;
    unsigned m_loopRows = 0;
    float m_rollStrong = 0;
    SwipeGroup m_swipe;
    uint32_t m_noticeSeq = 0;
    char m_notice[64] = {};

    mutable std::mutex m_viewLock;
    GestureView m_view;
    uint32_t m_strikeSeq = 0;
    std::array<GestureView::Strike, 32> m_strikes{};
    void publish();
};

}
