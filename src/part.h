#pragma once

// A part is one instrument of the track: drums, bass, chords, lead, arp,
// atmosphere, effects. Each has its own page, its own way of turning ten
// fingers into sound, and its own engine, while the studio around it keeps
// the things they share: one clock, one key, a mixer channel each, the room
// (reverb and echo), the sidechain pump, and eight clip slots per part.
//
// A part is two halves that never share memory:
//
//   PartControl  runs on the touch and UI threads (serialised by the session,
//                so it never needs locks of its own). It turns frames and keys
//                into Commands, keeps whatever state the page needs, and
//                paints the page.
//   PartEngine   runs on the audio thread. It hears Commands, live or
//                replayed from a clip, and renders the part's dry signal. It
//                must never allocate, lock or block.
//
// Everything a part plays goes through Commands, so the studio can record
// any performance into a clip and replay it, knob moves included, with no
// work from the part. Two rules make that work:
//
//   * Commands carry scale degrees, not pitches, wherever a pitch follows
//     the key: the engine turns degrees into pitch with ctx.harmony at the
//     moment it plays, so clips follow key changes.
//   * The first value of a held sound's id is the part's own business, but
//     ids must stay below 1 << 24: replayed clips set the bits above.

#include <QColor>
#include <QRectF>
#include <QString>
#include <cstdint>
#include <memory>

#include "touch.h"
#include "tuning.h"

class QPainter;

namespace tf {

constexpr int kMaxParts = 8;
constexpr int kClipSlots = 8;
constexpr int kPartData = 256;
constexpr int kBlock = 32; // the studio renders in blocks of at most this many frames
constexpr int kBeatsPerBar = 4;

// A message to a part's engine.
struct Command {
    // Types the studio understands when it records and replays clips. A
    // part's own types start at User.
    enum Type : uint16_t {
        On = 1,    // a sound starts: id; the rest is the part's
        Off = 2,   // the sound with this id ends
        Move = 3,  // the sound with this id changes (pitch, pressure, …)
        Param = 4, // a knob: i[0] which, f[0] value (f[1] a second axis)
        User = 16,
    };
    enum Flags : uint16_t {
        Quantize = 1, // a User command that is an onset: recorded clips pull it to the grid
        NoRecord = 2, // never goes into a clip (a page's own preview, a metronome)
        Continue = 4, // an On that carries on a sound already heard (a voice handed over at a
                      // bar line, a latched layer re-asserted): recorded, but not an onset, so
                      // Space's phrase finder doesn't mistake it for playing
    };
    uint16_t type = On;
    uint16_t flags = 0;
    int32_t id = 0;
    int32_t i[4] = {};
    double t = 0; // seconds on the touch clock when it happens; 0 = now. A time in
                  // the future is held by the studio and delivered in its block.
    float f[8] = {};
};

// The studio's clock, as a part's engine sees it for one block.
struct Transport {
    float rate = 48000;
    double bpm = 124;
    uint64_t sample = 0;   // the block's first sample since the studio started
    double beat = 0;       // beats since the studio started, at the block's first sample
    double beatsPerSample = 0;

    double beatAt(int offset) const { return beat + double(offset) * beatsPerSample; }
    double secondsPerBeat() const { return 60.0 / bpm; }
    double samplesPerBeat() const { return double(rate) * 60.0 / bpm; }
    // The offset in [0, n) at which the beat position b falls, or -1.
    int offsetOf(double b, int n) const {
        const double o = (b - beat) / beatsPerSample;
        return o >= 0 && o < double(n) ? int(o) : -1;
    }
};

// The key everything plays in, and the chord of the moment. The key is the
// studio's (keys and the arrange page change it); the chord is whatever the
// chords part last played, live or from a clip, so a bass, an arp or a lead
// can follow it.
struct Harmony {
    Tuning tuning;          // root, scale and mode: tuning.degreeSemis(degree)
    int chordRoot = 0;      // the chord's root as a scale degree, 0 = the key's tonic
    int chordTones[4] = {0, 2, 4, 6}; // its notes as scale degrees, root first
    int chordCount = 3;
    uint32_t chordSeq = 0;  // changes whenever the chord does

    // MIDI pitch of a scale degree, octave 0 being the root in octave two
    // (A at 45 in the default key).
    double midi(int degree, int octave = 0) const {
        return tuning.baseMidi() + 12.0 * octave + tuning.degreeSemis(degree);
    }
};

// What a part's engine may ask of the studio while it handles a command or
// renders a block. Audio thread only.
class AudioContext {
public:
    virtual ~AudioContext() = default;
    virtual const Transport &transport() const = 0;
    virtual const Harmony &harmony() const = 0;
    float rate() const { return transport().rate; }
    // Seconds on the touch clock at the block's first sample, and the offset
    // in the coming block (0..n-1) where a command's time t falls: late
    // commands play at once, early ones (a clip's) exactly.
    virtual double now() const = 0;
    virtual int offsetOf(double t, int n) const = 0;

    // The kick: every other part ducks around this moment (offset into the
    // coming block), by each channel's pump depth, scaled by strength.
    virtual void pump(int offset, float strength) = 0;
    // The chords part says what is playing now; the others read harmony().
    virtual void setChord(int root, const int *tones, int count) = 0;
    // The sidechain envelope for this block, 0 (no duck) .. 1 (fully down),
    // for a part that wants to pump its own way (the studio already ducks
    // every channel by its pump depth).
    virtual const float *duck() const = 0;
};

class PartEngine {
public:
    virtual ~PartEngine() = default;
    // Before audio starts; may allocate.
    virtual void init(float rate) = 0;
    // A command, live or from a clip. Replayed commands carry ids with bits
    // above 1 << 24 set, and their own t.
    virtual void command(const Command &c, AudioContext &ctx) = 0;
    // Adds the part's dry stereo signal for n ≤ kBlock frames into l and r,
    // which start at zero. The studio applies the channel (level, pan,
    // filter, pump, sends) afterwards.
    virtual void render(float *l, float *r, int n, AudioContext &ctx) = 0;
    // Everything stops: a release (hard = false) or an instant cut.
    virtual void silence(bool hard) = 0;
    // Numbers for the part's page, at most kPartData of them, published about
    // sixty times a second: what's sounding, envelopes, step positions…
    virtual void publish(float *data) { (void)data; }
    virtual int voices() const { return 0; }
};

// A clip slot as the pages see it.
struct ClipView {
    enum State : uint8_t { Empty, Stopped, Queued, Playing, Stopping, Recording };
    uint8_t state = Empty;
    float lengthBeats = 0;
    float phase = 0; // 0..1 through the clip while it plays
    int events = 0;
};

// A mixer channel's settings. Every value is 0..1 except where noted.
enum Channel : int {
    Gain = 0,  // 0..1, 0.8 is unity
    Pan,       // 0 left .. 0.5 centre .. 1 right
    Filter,    // 0 low-pass closed .. 0.5 open .. 1 high-pass closed (a DJ filter)
    ReverbSend, // send
    EchoSend,   // send
    PumpDepth, // how far the kick ducks this channel
    Mute,      // 0 or 1
    Solo,      // 0 or 1
    ChannelCount
};

struct ChannelView {
    float value[ChannelCount] = {};
    float peak = 0, rms = 0;
};

struct PartView {
    ClipView clips[kClipSlots];
    int playing = -1;    // the slot playing, or -1
    int queued = -1;     // the slot about to start at the next boundary, or -1
    bool overdub = false;
    int recording = -1;  // the slot being recorded, or a take waiting for it, or -1
    float takeBeats = 0; // a waiting take's length in beats (0 while open-ended)
    double takeAt = 0;   // the beat it starts looping at
    ChannelView channel;
    int voices = 0;
    float data[kPartData] = {};
};

// The room and the master bus.
enum Master : int {
    RoomSize = 0,   // reverb size and decay
    RoomShimmer,    // octave-up shimmer in the tail
    EchoTime,       // 0..1 → 1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2 of a beat… (see studio.cpp)
    EchoFeedback,
    MasterFilter,   // 0 low-pass .. 0.5 open .. 1 high-pass, the whole mix (build-ups)
    MasterDrive,    // tape saturation on the bus
    MasterGain,     // 0.8 is unity
    MasterCount
};

struct StudioView {
    double bpm = 124;
    double beat = 0;        // at the published sample
    double time = 0;        // touch-clock seconds at the published sample
    uint64_t sample = 0;
    bool metronome = false;
    int root = 9, family = 0, brightness = 4;
    bool just = false;
    int chordRoot = 0, chordCount = 3;
    int chordTones[4] = {0, 2, 4, 6};
    uint32_t chordSeq = 0;
    int partCount = 0;
    PartView parts[kMaxParts];
    float master[MasterCount] = {};
    float peak = 0, rms = 0, cpu = 0, cpuPeak = 0;
    float scope[256] = {}; // the last few milliseconds of the mix, for drawing
};

// A key press or release, for parts that play from the keyboard too. The
// session keeps the digits, Space, Escape, Tab, ? and every Ctrl
// combination for itself; a page gets everything else.
struct KeyEvent {
    int key = 0;        // Qt::Key
    int modifiers = 0;  // Qt::KeyboardModifiers
    bool down = true;
    bool repeat = false;
    QString text;
};

// What a part's controls may ask of the studio. Touch and UI threads, under
// the session's lock.
class ControlContext {
public:
    virtual ~ControlContext() = default;

    // This part's index, and the part's engine.
    virtual int part() const = 0;
    virtual void send(const Command &c) = 0;

    // The studio as of the last screen frame, and the clock in between:
    // beatAt() and timeAt() extrapolate from it, good to a millisecond.
    virtual const StudioView &studio() const = 0;
    virtual double beatAt(double t) const = 0;
    virtual double timeAt(double beat) const = 0;
    virtual double now() const = 0; // the touch clock

    // Clips. All of them are quantised by the studio: lengths to 1, 2, 4, 8,
    // 16 or 32 beats, starts to the beat, onsets toward the sixteenth.
    // Turns what this part played between t0 and t1 into a loop in a slot
    // and plays it at once, in time with what was just played (the
    // tap-then-hold gesture). quantize 0..1 pulls onsets to the 16th grid.
    virtual void capture(int slot, double t0, double t1, float quantize = 0.85f) = 0;
    // Armed recording: starts at the next bar, stops (and loops) at the bar
    // after stopRecording(), or after maxBars.
    virtual void record(int slot, int maxBars = 8) = 0;
    virtual void stopRecording() = 0;
    // While on, what this part plays is added to the playing clip.
    virtual void overdub(bool on) = 0;
    // Start a slot at the next bar (or at once, from its start), stop this
    // part's clip at the next bar, empty a slot.
    virtual void launch(int slot, bool now = false) = 0;
    virtual void stop(bool now = false) = 0;
    virtual void clear(int slot) = 0;
    int freeSlot() const {
        if (part() < 0)
            return -1;
        for (int s = 0; s < kClipSlots; ++s)
            if (studio().parts[part()].clips[s].state == ClipView::Empty)
                return s;
        return -1;
    }

    // The same, for any part: the arrange page's tools.
    virtual void launchPart(int part, int slot, bool now = false) = 0;
    virtual void stopPart(int part, bool now = false) = 0;
    virtual void clearPart(int part, int slot) = 0;
    // A time t (the touch clock, as ctx.timeAt(beat) gives it) schedules the
    // change for that moment, to the sample block; 0 is now.
    virtual void setChannel(int part, int channel, float value, double t = 0) = 0;
    virtual void setMaster(int master, float value, double t = 0) = 0;
    virtual void setTempo(double bpm) = 0;
    virtual void setKey(int root, int family, int brightness) = 0;

    // The pad's width over its height, so distances drawn and felt are round.
    virtual float aspect() const = 0;
    // A short message the window shows for a moment.
    virtual void notice(const QString &text) = 0;
};

struct PaintContext {
    QRectF pad;           // where the trackpad is drawn, in item pixels
    const StudioView *studio = nullptr;
    const PartView *view = nullptr; // this part's slice of it
    QColor color;         // the part's phosphor
    double time = 0;      // the touch clock, for animation
    double beat = 0;      // the beat at that moment
    float scale = 1;      // device pixels per pad unit hint: pad.height() / 600
    // Pad coordinates (0..1, y up) to item pixels and back.
    QPointF at(float x, float y) const {
        return {pad.left() + double(x) * pad.width(), pad.bottom() - double(y) * pad.height()};
    }
    QRectF rect(float x0, float y0, float x1, float y1) const {
        return QRectF(at(x0, y1), at(x1, y0)).normalized();
    }
};

class PartControl {
public:
    virtual ~PartControl() = default;
    // Touch frames while this part's page is showing.
    virtual void frame(const Frame &f, ControlContext &ctx) = 0;
    virtual void idle(double t, ControlContext &ctx) { (void)t, (void)ctx; }
    // Every page, showing or not, about a hundred times a second (with every
    // frame and idle tick): for work that must go on in the background, such
    // as an arrangement that launches clips as the bars go by. Keep it cheap.
    virtual void background(double t, ControlContext &ctx) { (void)t, (void)ctx; }
    // Return true when the key was used.
    virtual bool key(const KeyEvent &k, ControlContext &ctx) { (void)k, (void)ctx; return false; }
    // The page is shown, or left. Leaving must end every sound a finger or
    // key is holding (the session sends no more frames until the page
    // returns); clips keep playing.
    virtual void enter(ControlContext &ctx) { (void)ctx; }
    virtual void leave(ControlContext &ctx) = 0;
    // Draws the page inside pc.pad (and may use the margin around it). UI
    // thread, under the session's lock: keep it to drawing.
    virtual void paint(QPainter &p, const PaintContext &pc) = 0;
    // The page's gesture guide: one "gesture\twhat it does" per line.
    virtual QString help() const = 0;
    // One line under the pad: what the fingers are doing right now.
    virtual QString status() const { return {}; }
};

// One part, as the studio and the session know it.
struct PartSpec {
    QString name;      // "Drums"
    QString blurb;     // a few words for the page tab: "kick, snare, hats"
    QColor color;
    std::unique_ptr<PartControl> control;
    std::unique_ptr<PartEngine> engine;
    float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.15f, 0.0f, 0.6f, 0.0f, 0.0f};
};

}
