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
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "touch.h"
#include "tuning.h"

class QPainter;

namespace tf {

constexpr int kMaxParts = 9;
constexpr int kClipSlots = 8;
constexpr int kPartData = 256;
constexpr int kBlock = 32; // the studio renders in blocks of at most this many frames
constexpr int kBeatsPerBar = 4;
// The mouse, held down on the pad, arrives in frames as one more touch with
// this id (pressure 0.5, a normal fingertip), whether or not the trackpad is
// taken. A page can treat it as a finger, or as a pointer that clicks.
constexpr int kMouseId = 0x7FFF;

// A replayed sound's id: bit 30 set, the slot it plays from in bits 24–26,
// the layer of its clip in bits 27–28 (0 for what was taken first, 1… for
// what Space layered on top), the id it was played with in bits 0–23. A
// mono part plays each layer on a voice of its own, so layers never cut
// each other off.
constexpr int kClipLayers = 4;
inline bool isReplay(int id) { return (id & (1 << 30)) != 0; }
inline int replaySlot(int id) { return (id >> 24) & 7; }
inline int replayLayer(int id) { return isReplay(id) ? (id >> 27) & 3 : 0; }

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
        Quantize = 1, // a User command that is an onset: Space finds the phrase from them, and
                      // Quantise (unless the player turns it off) pulls them to the grid
        NoRecord = 2, // never goes into a clip (a page's own preview, a metronome)
        Continue = 4, // an On that carries on a sound already heard (a voice handed over at a
                      // bar line, a latched layer re-asserted): recorded, but not an onset, so
                      // Space's phrase finder doesn't mistake it for playing
        Sampled = 8,  // an On that starts recorded audio (a Vocal loop): when a loop joins part
                      // way in, the studio sets its t to when it should have begun, in the past,
                      // so the engine starts the audio that far in and it stays in place
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
    // The microphone for this block: n mono samples at the studio's rate, as
    // they arrived (nothing added on the way), or null when there is no
    // microphone at all.
    virtual const float *input() const { return nullptr; }
    // A part that monitors live input (PartSpec::monitors): where to add,
    // in place of l and r, what must be heard the moment it arrives (the
    // live voice), n frames per channel (0 left, 1 right), zeroed. It goes
    // through the part's channel but round the master's look-ahead limiter.
    // What can wait (its loops) goes into l and r and the mix as usual.
    // Null for any other part.
    virtual float *monitor(int channel) { (void)channel; return nullptr; }
    // What the engine sounds, as notes, for the MIDI file an export writes
    // (export.h); nothing listens otherwise, and the call costs nothing.
    // key is the engine's own name for a note sounding (a lane, a voice): a
    // key holds one note at a time, so a new note on it ends the last, and
    // midi < 0 ends it. offset: the sample of the coming block it starts on
    // (past the block for a hit held back). length > 0: a note of so many
    // samples, which ends by itself (a drum hit, an arp's step).
    virtual void note(int key, float midi, float velocity, int offset, int length = 0) {
        (void)key, (void)midi, (void)velocity, (void)offset, (void)length;
    }
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
    // Space layering onto a loop: true if a layer's hit on the same sound as
    // one of the loop's, within a thirty-second of it, is the hand playing
    // along with the loop (a doubled hit, heard as a flam) and is left out.
    virtual bool layersDropDoubles() const { return false; }
    // A slot's clip was given back as it was before a change, or the change
    // again (Ctrl+Z, Ctrl+Shift+Z): an engine that keeps settings per layer
    // of a loop lets go of what a layer no longer there had set.
    virtual void clipChanged(int slot) { (void)slot; }
    // A second engine like this one, as it was made (sharing whatever the
    // page publishes to it, such as the Sampler's songs), for an export
    // that renders one voice of the part apart from the rest
    // (Studio::addVoice). Null when the part can't be split.
    virtual std::unique_ptr<PartEngine> twin() const { return nullptr; }
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
    uint32_t takes = 0;  // counts up each time a take lands (Space, take(), record):
                         // a take can land within one frame of being asked for
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
    bool quantise = true; // Space and overdubs tighten onsets to the sixteenths; off, loops keep them as heard
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
    // The microphone, as the audio server gives it.
    enum Input : int { NoInput = 0, InputConnecting, InputLive, InputFailed };
    int input = NoInput;
    // Milliseconds from a sound at the microphone to the same sound leaving
    // the speakers: the server's capture and playback latency (the devices'
    // buffers included, as it reports them), and the studio's own: the
    // quantum it renders plus what waits in its input ring.
    float inputMs = 0, outputMs = 0, bufferMs = 0;
    // What the sound comes out of, as far as the system says (best effort:
    // the audio server's port or data source, see the platform's backend).
    // On speakers the microphone hears the monitored voice again, so the
    // Vocal page keeps its monitor muted until asked; Unknown is anything
    // it can't tell (a line out, HDMI, an interface) and warns of nothing.
    enum Output : int { OutputUnknown = 0, OutputSpeakers, OutputHeadphones };
    int output = OutputUnknown;
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

    // Clips. Their lengths are 1, 2, 4, 8, 16 or 32 beats and they start on
    // the beat; their onsets are tightened toward the sixteenth while
    // Quantise is on (StudioView::quantise), and stay where they were heard
    // when the player turns it off.
    // Turns what this part played between t0 and t1 into a loop in a slot
    // and plays it at once, in time with what was just played (the
    // tap-then-hold gesture). quantize 0..1 pulls onsets to the straight
    // 16th grid: 0 keeps what was heard (a page's own swung groove too).
    virtual void capture(int slot, double t0, double t1, float quantize = 0.0f) = 0;
    // What this part played between beats b0 and b1 (bar lines, b1 now or
    // soon) becomes a loop at b1, exactly as Space makes one: held notes are
    // carried in, and a take that lands part way in sounds what is held.
    virtual void take(int slot, double b0, double b1) = 0;
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
    // The loop Space last made on this page, a step longer (dir +1) or
    // shorter (−1), as the Loop longer / Loop shorter buttons do; false when
    // there is no such loop to change. loopStep(dir) is the length in beats
    // that step would give, or 0.
    virtual bool resizeLoop(int dir) { (void)dir; return false; }
    virtual int loopStep(int dir) const { (void)dir; return 0; }
    // A loop of recorded audio (the Vocal page's), made as Space makes one
    // but from a window the page chose: the clip is one sound, `sound` (an
    // On flagged Command::Sampled, which the page's engine plays from audio
    // it keeps), starting at beat b0, the loop running b0..b1 and landing at
    // b1. The studio puts into sound.i[3] how many samples into the audio
    // the clip's own start falls (the window needn't start on the bar).
    // into ≥ 0: layered onto that slot's clip. retake: in place of the take
    // just made into that slot (longer or shorter). With free, nothing else
    // plays: b0 and b1 are touch-clock seconds and the loop sets the tempo,
    // as a capture does.
    virtual void takeAudio(int slot, double b0, double b1, const Command &sound, int into = -1, bool retake = false,
                           bool free = false) {
        (void)slot, (void)b0, (void)b1, (void)sound, (void)into, (void)retake, (void)free;
    }
    // Space again while a take waits: it is called off.
    virtual void cancelTake() {}
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
    // Clip edits, each undone by Ctrl+Z like a take. Copy keeps the clip's
    // place against the bars. Merge puts `other` into `into`: layered (both
    // at once, the shorter repeated) or chained (into, then other).
    virtual void copyClip(int part, int from, int to) = 0;
    virtual void mergeClips(int part, int into, int other, bool chain) = 0;
    virtual void stopPart(int part, bool now = false) = 0;
    // The window's New loop, from the page (its "+" slot): this part's loop
    // stops, kept in its slot, so the next Space makes a loop of its own.
    // False when it has no loop playing.
    virtual bool newLoop() { return false; }
    // A "+N MORE" chip touched on the pad: the window opens the sound
    // browser with every one of the page's sounds.
    virtual void browseSounds() {}
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
    // Asks the player for a sound file, in the system's open dialog. The
    // path picked comes back to this page later, as a drop (dropped());
    // nothing comes if the dialog is closed or there is no dialog to show.
    virtual void pickSound(const QString &title) { (void)title; }
    // The session is played by a script on a clock of its own (a render, a
    // screenshot, a test), faster or slower than real time: a page that
    // does long work on a thread of its own (the Sampler reading a song)
    // does it at once instead, before it returns, so the script hears it.
    virtual bool scripted() const { return false; }
    // A line in the debug log (~/.cache/tenfold/debug.log), when it is on.
    virtual void log(const QString &line) { (void)line; }
};

struct PaintContext {
    QRectF pad;           // where the trackpad is drawn, in item pixels
    const StudioView *studio = nullptr;
    const PartView *view = nullptr; // this part's slice of it
    QColor color;         // the part's phosphor
    double time = 0;      // the touch clock, for animation
    double beat = 0;      // the beat at that moment
    float scale = 1;      // device pixels per pad unit hint: pad.height() / 600
    bool hover = false;   // the mouse is over the pad (not pressed or pressed)
    float hoverX = 0, hoverY = 0; // where, in pad coordinates
    // The mouse anywhere over the page, pad or not, in item pixels: for
    // lighting a control drawn around the pad (see PartControl::click).
    bool mouseOver = false;
    QPointF mouse;
    // How much the window shows (Session::level): 0 for a newcomer, who
    // sees the pad plainly (no note letters, no strips until used); 2 all.
    int level = 2;
    // Where the pad is seen when the session zooms onto a page's play area
    // (pad is then larger, partly off it); empty: all of pad. The band under
    // the pad follows this (console::bandRow).
    QRectF visible;
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
    // A mouse click on a control the page drew outside the pad (the band
    // under it, the margin over it), at `at` in the item pixels paint()
    // drew in. Clicks on the pad itself arrive as a finger (kMouseId).
    // Every setting a key reaches should have such a control or a place
    // on the pad, so the mouse alone can play the page. True if it hit one.
    virtual bool click(QPointF at, ControlContext &ctx) { (void)at, (void)ctx; return false; }
    // A right click anywhere on the page, in item pixels: a page may open a
    // menu of its own there (drawn by paint()). True if it took the click.
    virtual bool menu(QPointF at, ControlContext &ctx) { (void)at, (void)ctx; return false; }
    // A left click while that menu is open: on an item it does it, anywhere
    // else it closes the menu. True if a menu was open (the click is used).
    virtual bool menuClick(QPointF at, ControlContext &ctx) { (void)at, (void)ctx; return false; }
    // Esc: closes the menu. True if one was open.
    virtual bool closeMenu() { return false; }
    // Space layering onto this page's loop: true if a layer always spans the
    // whole loop (a page whose playing is changes of state, like a bed's
    // chord, which a shorter layer would repeat inside the loop).
    virtual bool layersSpanTheLoop() const { return false; }
    // Space layering onto this page's loop: true if what the page's knobs
    // did over the loop (its Command::Param moves, a filter swept, grit
    // turned up) is playing Space keeps, with new notes or without: the
    // layer then spans the loop and plays the moves back as they were made.
    virtual bool layersKeepKnobMoves() const { return false; }
    // Whether sound left on with no hand on the page (a riser building, a
    // bed latched) joins playing before a pause to playing after it, when
    // Space looks for the phrase. A bed left on isn't playing: false.
    virtual bool soundLeftOnJoinsThePlaying() const { return true; }
    // Space with nothing else playing loops the pattern the player settled
    // into. False: its cycle has more than one hit, and two short cycles of
    // it are taken, for the hand's feel (a drum fill, a riff). True for a
    // page whose every hit is a phrase of its own (the Sampler's chops): a
    // single hit repeating is a pattern too, and one cycle of it is the
    // loop, so a bar-long chop held round makes a bar, not four.
    virtual bool loopsOneCycle() const { return false; }
    // Just after Space makes a loop, a finger that lands in the next moment
    // is taken for the hand still going and ignored, so it doesn't double
    // the loop's first notes. False for a page where every landing is a hit
    // to be heard (the drums, whose engine already lets a live hit win over
    // the loop's copy of it).
    virtual bool quietAfterSpace() const { return true; }
    // The page's gesture guide: one "gesture\twhat it does" per line.
    virtual QString help() const = 0;
    // One line under the pad: what the fingers are doing right now.
    virtual QString status() const { return {}; }
    // Ctrl+N twice (or the tutorial starting): the studio has been cleared,
    // its mix set back home. Drop whatever belongs to the old song.
    virtual void startOver(ControlContext &ctx) { (void)ctx; }
    // Ctrl+S: the page's own settings, the ones a player chose and would
    // want back when the song is opened again (a kit, a sound, a style, a
    // latch, a song written on the page), as JSON values the page names
    // itself. Not what a hand is doing this moment, and not the clips: the
    // studio saves those.
    virtual void save(QJsonObject &out) const { (void)out; }
    // Ctrl+O, after startOver() and once the studio has the song's clips:
    // the page takes back what save() wrote and sends its engine whatever
    // makes it sound that way. The file may be old or edited by hand, so a
    // name the page doesn't know is ignored, a missing one keeps the page's
    // default, and every value is checked and clamped to its range.
    virtual void load(const QJsonObject &in, ControlContext &ctx) { (void)in, (void)ctx; }
    // Ctrl+S, for a page whose clips play audio the page keeps itself (the
    // Vocal page's loops): whatever those clips need to sound again, which
    // the file holds as the part's "audio". `sounds` is every command in the
    // part's clips, as the file will hold them.
    virtual void saveAudio(const std::vector<const Command *> &sounds, QJsonObject &out) const {
        (void)sounds, (void)out;
    }
    // Ctrl+O, after startOver() and before the studio has the clips: the
    // page takes back what saveAudio() wrote, and may change the commands in
    // place so they name the audio as the page now keeps it. A file without
    // it (or with it broken) still opens: those clips are silent, and the
    // page says so.
    virtual void openAudio(const QJsonObject &in, const std::vector<Command *> &sounds, ControlContext &ctx) {
        (void)in, (void)sounds, (void)ctx;
    }
    // Pause (Ctrl+P): every loop stops at once. A page that plays on its own
    // (the Song page's song) stops too and remembers where it was; resume()
    // starts it again from there, from `bar` (the studio's bar where the
    // loops start again). True if the page had something to pause.
    virtual bool pause(ControlContext &ctx) { (void)ctx; return false; }
    virtual void resume(int bar, ControlContext &ctx) { (void)bar, (void)ctx; }
    // Ctrl+Z, before the studio takes back a step: a change the page has
    // promised but not yet made (a roll kept for the next bar line) is the
    // newest thing done, so taking it back is the page's. True if it did.
    virtual bool takeBackPending(ControlContext &ctx) { (void)ctx; return false; }
    // The studio has taken back this part's newest step: anything the page
    // still plays as part of it (a kept roll finishing its pass) stops.
    virtual void undone(ControlContext &ctx) { (void)ctx; }
    // A number about what the player has done on this page, for the
    // tutorial to tell when a step is done: a state ("follow": 1 or 0) or a
    // count that only goes up ("rolls": how many rolls were ever started),
    // so a quick gesture between two screen frames is never missed. NaN
    // when the page doesn't know the name; each page lists its names in its
    // own override.
    virtual float tutorialProbe(const QString &name) const { (void)name; return std::nanf(""); }
    // Something handed to the page from outside the pad: a file dragged
    // onto the window, or text pasted into it (a path, or a link to a
    // song). True if the page takes such things at all; what comes of it
    // (loading, a notice saying why not) is the page's.
    virtual bool dropped(const QString &what, ControlContext &ctx) {
        (void)what, (void)ctx;
        return false;
    }
    // A page whose loops are recorded audio, not its Commands (the Vocal
    // page), makes its own takes: it handles Space in key() and calls
    // ControlContext::takeAudio. The window's Loop shorter / longer buttons
    // then ask it too: the loop's beats one step that way (dir −1, 0, +1),
    // 0 when there is no such step, or −1 when it has no take to change.
    virtual int loopBeats(int dir) const { (void)dir; return -1; }
    virtual void resizeLoop(int dir, ControlContext &ctx) { (void)dir, (void)ctx; }
    // The Song page, for an export: the bars its song lasts (0 when there
    // is none), and the song played once from its top, starting at that
    // bar of the studio's clock.
    virtual int songBars() const { return 0; }
    virtual void playSong(int bar, ControlContext &ctx) { (void)bar, (void)ctx; }

    // The sound browser (the window's SOUND button): the page's sounds in
    // the order ← → steps through them, each with a few words saying what
    // it is for; the one chosen now (−1: none); and choosing one, as ← →
    // would. A page without sounds to choose returns none.
    struct SoundChoice {
        QString name, what;
    };
    virtual std::vector<SoundChoice> soundChoices() const { return {}; }
    virtual int soundChoice() const { return -1; }
    virtual void chooseSound(int index, ControlContext &ctx) { (void)index, (void)ctx; }
    // A sound tried in the browser is heard at once: a short phrase the
    // page plays from its own keys, as a hand on the keyboard would. Each
    // step is a key going down or up, `beat` beats after the sound was
    // chosen. None: choosing it is heard by itself (a scene laid).
    struct AuditionStep {
        double beat;
        int key;
        bool down;
    };
    virtual std::vector<AuditionStep> audition() const { return {}; }
    // The part of the pad that plays, where the rest fine-tunes (a rail of
    // rhythms, a bank of shapers), in pad units (y up), with a few words on
    // what the rest holds. Below the Studio level the session zooms the pad
    // onto it: it fills the pad, touches land in it, and the fine-tuning is
    // one line on the pad's edge. The whole pad (the default): no zoom.
    struct PlayArea {
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
        QString what;
        bool zooms() const { return x0 > 0 || y0 > 0 || x1 < 1 || y1 < 1; }
    };
    virtual PlayArea playArea() const { return {}; }
    // The page draws its own sound chips on the pad (the Arp): the session's
    // level-0 SOUND chip under the pad would show them twice.
    virtual bool soundsOnPad() const { return false; }
};

// A phrase for audition(): each key held for `len` beats from `at`.
inline void auditionNote(std::vector<PartControl::AuditionStep> &out, double at, int key, double len) {
    out.push_back({at, key, true});
    out.push_back({at + len, key, false});
}

// One part, as the studio and the session know it.
struct PartSpec {
    QString name;      // "Drums"
    QString blurb;     // a few words for the page tab: "kick, snare, hats"
    QColor color;
    std::unique_ptr<PartControl> control;
    std::unique_ptr<PartEngine> engine;
    float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.15f, 0.0f, 0.6f, 0.0f, 0.0f};
    // A part that monitors live input (the microphone) gets a second bus
    // (AudioContext::monitor) mixed after the master bus's look-ahead
    // limiter, so the voice reaches the headphones without the limiter's
    // delay; it still has its channel and the room.
    bool monitors = false;
    // Whether what it plays depends on the chord of the moment (bass, arp,
    // pads…): Space then hears the same notes over a new chord as new
    // playing, so a line that follows a progression loops the progression.
    bool followsChords = true;
};

}
