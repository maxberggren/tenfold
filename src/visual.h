#pragma once

// The Visualizer: a page beside the Song page (key 9) that turns everything
// playing into a real-time picture, and whose fingers reshape that picture.
//
// This file is the part of it that is plain logic, kept apart from Qt Quick
// so the tests can drive it:
//
//   SignalTracker  reads the studio's view sixty times a second and turns
//                  each part's published numbers into a signal the shaders
//                  can show: a kick as a punch, a chord change as a new
//                  palette, an arp note as a burst of sparks…
//   HandMorph      turns touch frames into a Look: the latched shape of the
//                  world (which world, how folded, how twisted, how fast…).
//                  Gestures are relative, so a hand landing never jumps the
//                  picture, and what it set stays when it lifts.
//   VisualPage     the page itself: the hand, its keys, what is saved with
//                  a song, and a thin overlay drawn over the picture.
//
// The picture itself (Visualizer.qml and the shaders in src/visual/) and
// the second window and the video recorder live with the app
// (visualizer.h, recorder.h).

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <array>
#include <cstdint>
#include <vector>

#include "console.h"
#include "part.h"

namespace tf::visual {

// The parts in the studio's order (parts.cpp).
enum PartIndex : int { Drums = 0, Bass, Chords, Lead, Arp, Atmos, Fx, Vocal, Sampler, kParts };

// A note the shaders draw as it happens: arp notes burst into sparks, lead
// notes leave a streak.
struct NoteEvent {
    double t = -1e9;   // when (the studio's seconds)
    float pitch = 0.5f; // 0 low .. 1 high
    float vel = 0;
    int kind = 0;       // 0 arp, 1 lead
};
constexpr int kNotes = 4;

// Everything the shaders hear, each 0..1 unless noted.
struct Signals {
    double time = 0;     // the studio's seconds
    double beat = 0;
    float beatPhase = 0, barPhase = 0;
    float bpm = 124;
    float master = 0;    // the mix's level
    // Drums: each an envelope that jumps on a hit and decays, and a count of
    // hits that only goes up (the shaders cut on it).
    float kick = 0, snare = 0, hat = 0, perc = 0, sub = 0;
    int kicks = 0, snares = 0, hats = 0;
    // Bass: how loud, its pitch, its filter, its amp envelope.
    float bass = 0, bassPitch = 0.3f, bassFilter = 0.5f, bassEnv = 0;
    // Chords: the chord's colour (its root round the circle of fifths), the
    // last one's, the crossfade between them, a flash on each change.
    float chordHue = 0, chordPrevHue = 0, chordBlend = 1, chordFlash = 0, chords = 0;
    int chordTones = 3;
    uint32_t chordSeq = 0;
    int chordChanges = 0;
    // Lead: how loud, where it sings, the finger's height (brightness), the bend.
    float lead = 0, leadPitch = 0.5f, leadBright = 0, leadBend = 0;
    // Arp: its envelope, the last note's pitch, a flash per note, notes a second.
    float arp = 0, arpPitch = 0.5f, arpFlash = 0, arpRate = 0;
    // Atmosphere: level, a swell, the halo, its tone (0 dark .. 1 bright).
    float atmos = 0, swell = 0, halo = 0, atmosTone = 0.5f;
    // Effects: a build's intensity, the drop's flash, the sweep (−1 low-pass
    // .. 1 high-pass), the wash and echo throws.
    float build = 0, drop = 0, sweep = 0, wash = 0;
    int drops = 0;
    // Vocal: level (the microphone or its loops), pitch, how clearly pitched.
    float vocal = 0, vocalPitch = 0.5f, clarity = 0, vocalLoops = 0;
    // Each part's level, scaled by its own recent loudest so a quiet part
    // still reads.
    float level[kParts] = {};
    NoteEvent notes[kNotes]; // newest first
};

class SignalTracker {
public:
    // Hears one view; dt is the time since the last (the screen's frame).
    void update(const StudioView &v, double dt);
    const Signals &now() const { return m_s; }
    void reset() { *this = SignalTracker(); }

private:
    void note(double t, float pitch, float vel, int kind);
    static float decay(float v, double dt, double seconds);

    Signals m_s;
    float m_peak[kParts] = {};
    float m_prevDrum[3] = {};
    // The Sampler's song: its lows, middle and highs against their own
    // loudest lately, and the last frame's, to hear its hits in.
    float m_samplePeak[3] = {}, m_prevSample[3] = {};
    float m_prevLeadOn = 0;
    float m_prevLeadPos = -1e9f;
    float m_prevDrop = 0;
    float m_chordTarget = -1;
    float m_sweepPeak = 0;
    double m_dropHold = 0;
    float m_lastDrop = -1;
    float m_rate = 0;
    // The arp's newest published note, to tell the new ones from those seen.
    float m_arpBeat = -1, m_arpDegree = -1e9f;
    bool m_seen = false;
};

// Pure helpers the tracker and the tests share.
// A pitch class's place on the circle of fifths, as a hue 0..1.
float fifthsHue(int pitchClass);
// Whether a part can be heard (not muted, not soloed away, its fader up).
bool audible(const StudioView &v, int part);

// The shape of the world, as the hand leaves it. Saved with the song.
struct Look {
    float world = 0;    // which world, continuous: 0 city, 1 tunnel, 2 crystal, 3 sponge, 4 lattice (wraps)
    float fold = 0.5f;  // how folded and detailed, 0..1
    float symmetry = 0; // the kaleidoscope: 0 off .. 1 twelve-fold
    float warp = 0.25f; // domain warp of the feedback, 0..1
    float twist = 0;    // the roll, radians, any value
    float zoom = 0;     // −1 wide .. 1 close
    float yaw = 0, pitch = 0; // where the camera looks, −1..1
    float speed = 0.5f; // how fast it flies, 0..1
    float trails = 0.5f; // how long the feedback remembers, 0..1
    float hue = 0;      // a turn of the palette, 0..1 (wraps)
    float seed = 0;     // dials the folds' constants: every value another world
    bool operator==(const Look &) const = default;
};
constexpr float kWorlds = 5;

// The hand right now, for the shaders: what is down and how it moves.
struct Hand {
    int count = 0;
    float cx = 0.5f, cy = 0.5f; // centroid, 0..1, y up
    float spread = 0;           // mean distance from the centroid (pad heights)
    float pressure = 0;         // mean, 0..1
    float motion = 0;           // how fast it moves, smoothed (pads a second)
    float chaos = 0;            // a glitch that follows quick motion and decays
    float extreme = 0;          // 0..1, nine or ten fingers down (eased)
    struct Finger {
        float x = 0, y = 0, pressure = 0, alive = 0; // alive fades 1 → 0 after a lift
        int id = -1;
        // Which finger down it is, 0 the first: the lowest place no other
        // finger down holds when it lands. Kept while it is down and as it
        // fades, so its colour (Settings::fingerHues) stays with it.
        int order = -1;
    };
    std::array<Finger, 10> fingers{};
};

// Fingers in, a Look out. Relative gestures: each frame moves the look by
// how the same fingers moved since the last; a finger landing or lifting
// only sets a new reference, so the picture never jumps.
//
//   1 finger    steer: x turns the camera, y tilts it; pressing harder flies faster
//   2 fingers   twist them to roll the world; spread or pinch to zoom
//   3 fingers   slide sideways to travel between worlds (they morph into
//               each other on the way); up and down folds them; twist for
//               the kaleidoscope
//   4 fingers   spread to warp space; twist to turn the palette
//   5 fingers   twist the whole hand to dial the seed (endless variations);
//               spread for longer trails; up and down for speed
//   6–10        two hands: the left steers the camera, the right travels
//               and folds; both spread to zoom, both twist to roll
//
// Every finger is also a vortex in the feedback, its pull its pressure, and
// fast motion of any hand glitches the picture (chaos).
class HandMorph {
public:
    void frame(const Frame &f, float aspect);
    // Between frames: fades lifted fingers, eases chaos, and drifts the look
    // when no hand has touched it for a while (autopilot).
    void tick(double dt, bool autopilot);
    Look look;
    const Hand &hand() const { return m_hand; }
    double idleSeconds() const { return m_idle; }

private:
    struct Ref {
        int count = 0;
        int ids[16] = {};
        float x[16] = {}, y[16] = {};
        double t = 0;
    } m_ref;
    Hand m_hand;
    double m_idle = 1e9;
    float m_aspect = 1.6f;
    float m_speedTarget = -1;
};

// Wraps a Look's world to 0..kWorlds and clamps the rest into range.
Look tidy(Look l);
QJsonObject toJson(const Look &l);
// Missing or broken values keep l's.
Look fromJson(const QJsonObject &o, Look l);

// The fingers whose colour the player sets: the first seven down. The rest
// keep the chord's.
constexpr int kColourFingers = 7;

// The visual page's settings beyond the look, saved with it.
struct Settings {
    Look look;
    // Each early finger's colour, as a turn round the colour wheel from the
    // one every finger has by default (the chord's, turned by the look's
    // hue), 0..1: 0 the chord's own, 0.5 its complement. It still follows
    // the chords, at its own distance.
    std::array<float, kColourFingers> fingerHues{};
    bool autopilot = true;
    int quality = 1;      // 0 draft, 1 normal, 2 fine, 3 full: the world's resolution
    bool markers = true;  // the fingers drawn over the picture
    bool crt = false;     // scanlines and a curved glass (C)
};

// What the picture integrates between frames, so it moves smoothly: how far
// the camera has flown, the roll of the echoes, and the turn a chord change
// gives the crystals.
struct Drive {
    double travel = 0;
    float roll = 0;          // radians a frame the feedback turns
    float seedShift = 0, seedShiftTo = 0;
    float pace = 0;          // how fast the camera flies, units a second
    int drops = -1, chordChanges = -1;
    float lastTwist = 0;
    // True when the music has just asked to move the world on (a drop),
    // and follow lets it: the page's autopilot. The app then calls
    // VisualPage::moveOn().
    bool step(const Signals &s, const Look &l, double dt, bool follow = true);
};

// A finger's turn of the colour wheel from the chord's: its own setting if
// it is one of the first kColourFingers down, else none.
float fingerHue(const Settings &set, const Hand::Finger &f);
// The shaders' cosine palette (feedback.frag's palette()) at t, the chord's
// hue and the look's turn added in by the caller: what a finger's ring is
// drawn in is palette(chordHue + look.hue + kFingerTone + fingerHue).
QColor palette(float t);
constexpr float kFingerTone = 0.6f;

// The march steps and the world's share of the picture's resolution, per
// quality (draft, normal, fine, full).
int marchSteps(int quality);
float sceneScale(int quality);

// One uniform of the shaders: a name the shaders declare, four floats.
struct Uniform {
    const char *name;
    float v[4];
};
// Every uniform the shaders read, for a picture of width × height pixels.
// crt: how much of the CRT the glass shows (the second window and video a
// full one; the main window, which has its own scanlines, half).
std::vector<Uniform> uniforms(const Signals &s, const Settings &set, const Hand &h, const Drive &d, float width,
                              float height, float crt);

// The page. Its frame(), key() and paint() run under the session's lock, as
// every page's do; the app reads state() under the same lock each frame.
class VisualPage : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override;
    bool key(const KeyEvent &k, ControlContext &ctx) override;
    void leave(ControlContext &ctx) override;
    void paint(QPainter &p, const PaintContext &pc) override;
    bool click(QPointF at, ControlContext &ctx) override;
    QString help() const override;
    QString status() const override;
    void startOver(ControlContext &ctx) override;
    void save(QJsonObject &out) const override;
    void load(const QJsonObject &in, ControlContext &ctx) override;
    float tutorialProbe(const QString &name) const override;

    // What the app reads each frame.
    struct State {
        Settings settings;
        Hand hand;
        // Counters that go up once per ask: the app opens or closes the
        // second window, or flips it to full screen, when one changes.
        int windowAsks = 0, fullScreenAsks = 0;
    };
    const State &state() const { return m_state; }
    // The app steps the hand's easing at its own frame rate (also offline).
    void tick(double dt);
    // The chord's colour as the picture has it now, so the fingers drawn
    // over it are in their rings' colours.
    void hear(float chordHue) { m_chordHue = chordHue; }
    // The early finger the keys colour (0 the first), and its colour now.
    int picked() const { return m_pick; }
    QColor fingerColour(int order) const;
    // A drop with the autopilot on: a hard cut to the next world, in new
    // colours.
    void moveOn();

    // Keys that set a world directly: A S D F G.
    static int worldKey(int key);

private:
    void ask(ControlContext &ctx, const QString &what);
    HandMorph m_morph;
    State m_state;
    void turnFinger(ControlContext &ctx, int finger, float by);
    console::Hotspots m_spots;
    int m_jumps = 0;
    int m_pick = 0;
    float m_chordHue = 0;
};

PartSpec makeVisual();

}
