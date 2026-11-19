#pragma once

// The sound engine. It owns every voice, loop and effect, renders on the
// audio thread, and hears only commands from a lock-free queue, so the touch
// thread can never stall it and it can never stall the screen.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "dsp.h"
#include "tuning.h"

namespace tf {

// What each held finger turns, by the order it landed in: the knobs a
// producer reaches for, in the order they reach for them.
enum Role : int {
    Filter = 0, // cutoff / resonance
    Env,        // filter envelope amount / decay: pluck ↔ pad
    Width,      // unison detune and spread / sub oscillator
    Drive,      // drive / fold
    Pump,       // sidechain depth / arpeggio rate
    Echo,       // tape echo amount / time
    Space,      // reverb size / shimmer
    Glide,      // portamento / swing
    Wobble,     // tempo-synced filter wobble depth / rate
    Gravity,    // pull to the scale / mode, bright to dark
    RoleCount
};

const char *roleName(int role);
// Two short words for what slide and twist do on a role.
const char *roleSlide(int role);
const char *roleTwist(int role);
// Where the knobs start, and where they go back to on a reset.
float roleDefault(int role, int axis);

// The pad's rows, bottom to top, each one instrument: the kit, then bass,
// chords, lead and arpeggio. On the drum row the columns choose the piece;
// on the others they are the notes of the scale. A tap is a short note, a
// hold sustains it (or, on the drums, rolls).
enum Row : int { DrumRow = 0, BassRow, ChordRow, LeadRow, ArpRow, RowCount };
const char *rowName(int row);
// Semitones each row sits above the tuning's base (the root in octave 2).
inline int rowOffset(int row) {
    static const int o[RowCount] = {0, 0, 12, 12, 12};
    return o[std::clamp(row, 0, RowCount - 1)];
}
// The drum row's columns, left to right.
constexpr int kDrumPieces = 6;
inline int drumPieceAt(float x) { return std::clamp(int(x * kDrumPieces), 0, kDrumPieces - 1); }

enum class Cmd : uint8_t {
    NoteOn,      // id finger, i1 row, i2 degree, f0 midi, f1 velocity, f2 x, f3 y
    NoteMove,    // id finger, i2 degree, f0 midi, f1 pressure, f2 x, f3 y
    NoteOff,     // id finger
    Hit,         // i1 drum piece, f0 velocity, f1 x
    RollOn,      // id finger, i1 drum piece, f0 pressure, f1 x
    RollMove,    // id finger, f0 pressure
    RollOff,     // id finger
    Freeze,      // id layer: every note under a finger latches, with its own sound
    Knob,        // i1 role, f0 slide, f1 twist
    LoopBegin,   // id loop, t onset of the handle (one), f0 length s, f1 home x, f2 home y
    LoopHit,     // id loop, f0 offset s, i1 row, i2 degree or piece, f1 velocity, f2 x, f3 length s
    LoopReady,   // id loop: quantise to the tempo and start
    LoopMod,     // id loop, i1 transpose, f0 brightness/wetness shift, f1 density, f2 intensity, f3 x, f4 y
    LoopGrab,    // id loop: a finger holds it again
    LoopRelease, // id loop: the finger let go
    LoopPin,     // id loop: toggle whether it erodes
    Overdub,     // id loop, t onset, i1 row, i2 degree or piece, f0 velocity, f1 x, f2 length s
    Motion,      // id loop, i1 lane, i2 role, i3 row, t time, f0 midi, f1 pressure, f2 slide, f3 twist, f4 degree, f5 x
    LayerGrab,   // id layer
    LayerMod,    // id layer, i1 transpose, f0 brightness, f1 width, f2 swell
    LayerRelease,// id layer
    LayerPin,    // id layer: toggle whether it fades
    Dissolve,    // fade the newest layer
    DissolveAll, // fade everything that is left sounding
    Damper,      // f0 0..1, a palm on the pad
    SetTuning,   // i1 root, i2 family, i3 brightness, f0 just
    Hush,        // f0 seconds: fade out hits and notes younger than that (a swipe's own notes)
};

struct Command {
    Cmd type = Cmd::NoteOn;
    int32_t id = 0, i1 = 0, i2 = 0, i3 = 0;
    double t = 0;
    float f[6] = {};
};

constexpr int kMaxLoops = 16;
constexpr int kMaxLoopHits = 96;
constexpr int kMaxVoices = 40;
constexpr int kLanes = 4;
constexpr int kLaneBins = 256;
constexpr int kMaxLayers = 16;

struct LoopView {
    int id = -1;
    bool active = false, pinned = false, held = false, dissolving = false;
    float phase = 0, level = 0, homeX = 0, homeY = 0, flash = 0, density = 0, matShift = 0;
    int transpose = 0, hitCount = 0, cycle = 0;
    float lengthSec = 0;
    float hitPhase[kMaxLoopHits] = {};
    float hitMaterial[kMaxLoopHits] = {};
    float hitDegree[kMaxLoopHits] = {};
    // Recorded voice lines, 64 steps around the ring: −1 where the finger was
    // up, else its pitch from 0 (low) to 1 (high).
    int laneCount = 0;
    float lane[kLanes][64] = {};
    int laneRole[kLanes] = {};
};

struct LayerView {
    int group = -1;
    bool active = false, held = false, pinned = false;
    float x = 0, y = 0, level = 0;
    int voices = 0, transpose = 0;
};

struct VoiceView {
    bool active = false, frozen = false;
    int finger = -1, role = 0;
    float x = 0, y = 0, level = 0, midi = 0;
};

struct EngineView {
    LoopView loops[kMaxLoops];
    VoiceView voices[kMaxVoices];
    LayerView layers[kMaxLayers];
    float knobs[RoleCount][2] = {};
    float cpu = 0, cpuPeak = 0;
    float barSec = 0, barPhase = 0;
    bool tempoSet = false;
    float peak = 0, rms = 0;
    float damper = 0;
    int strikes = 0;
    uint64_t sample = 0;
};

// Loops the gesture thread can grab again: written by the audio thread, read
// by the touch thread to find the loop nearest a finger.
struct LoopRegistry {
    std::atomic<int> id[kMaxLoops];
    std::atomic<float> x[kMaxLoops], y[kMaxLoops];
    std::atomic<int> layer[kMaxLayers];
    std::atomic<float> layerX[kMaxLayers], layerY[kMaxLayers];
    LoopRegistry() {
        for (int i = 0; i < kMaxLoops; ++i) {
            id[i] = -1;
            x[i] = 0;
            y[i] = 0;
        }
        for (int i = 0; i < kMaxLayers; ++i) {
            layer[i] = -1;
            layerX[i] = 0;
            layerY[i] = 0;
        }
    }
};

class Engine {
public:
    explicit Engine(float sampleRate);
    ~Engine();

    float sampleRate() const { return m_rate; }

    // Gesture times are seconds on the same clock as setTimeOrigin(): the
    // kernel's monotonic clock live, script time offline.
    void setTimeOrigin(double seconds) { m_time0 = seconds; }
    double timeOrigin() const { return m_time0; }
    double now() const { return m_time0 + double(m_sample) / m_rate; }

    bool send(const Command &c) { return m_queue.push(c); }

    // Renders interleaved stereo. Audio thread only.
    void render(float *interleaved, int frames);

    // The screen's copy, at most a frame old.
    bool fetchView() { return m_views.fetch(); }
    const EngineView &view() const { return m_views.front(); }

    const LoopRegistry &registry() const { return m_registry; }

    // Mono output history for the screen's spectrum, written without locks.
    static constexpr int kScopeSize = 8192;
    void copyScope(float *out, int n) const;

    // Recording tap: a ring of interleaved stereo the recorder thread drains.
    static constexpr size_t kRecordFrames = 1 << 19;
    void setRecording(bool on) { m_recording.store(on, std::memory_order_release); }
    bool recording() const { return m_recording.load(std::memory_order_acquire); }
    size_t drainRecording(float *out, size_t maxFrames);

    // For tests and the offline renderer.
    int activeVoices() const;
    int activeHits() const;
    int activeLoops() const;
    float barSeconds() const;
    double lastBlockLoad() const { return m_lastLoad; }

    struct Impl;

private:
    void apply(const Command &c);
    void renderBlock(float *l, float *r, int n);
    void publish();

    float m_rate;
    double m_time0 = 0;
    uint64_t m_sample = 0;
    double m_lastLoad = 0;
    std::unique_ptr<Impl> d;
    SpscQueue<Command, 8192> m_queue;
    TripleBuffer<EngineView> m_views;
    LoopRegistry m_registry;

    float m_scope[kScopeSize] = {};
    std::atomic<uint32_t> m_scopeWrite{0};

    std::atomic<bool> m_recording{false};
    std::vector<float> m_record;
    std::atomic<size_t> m_recordHead{0}, m_recordTail{0};
};

}
