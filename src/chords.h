#pragma once

// The Chords part's engine: the harmony (which notes a degree and a colour
// make in the current key, and how to voice them so one chord flows into
// the next), the six sounds, and the tempo-locked gates that give a held
// chord its rhythm. Included only by chords.cpp.
//
// Everything a chord is travels in its Command as scale degrees and
// choices, never pitches: the engine works out the notes at the moment it
// plays, from the key of that moment and the voicing of the chord before,
// so a loop follows a key change and still voice-leads.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "dsp.h"
#include "electro.h"
#include "part.h"

namespace tf::chords {

// --- the vocabulary ---------------------------------------------------------

// What the rows of the pad and the rows of the keyboard choose: how much of
// the harmony a chord carries, from bare to lush.
enum Colour : int { Power, Sus, Triad, Add9, Seventh, ColourCount };
inline const char *colourName(int c) {
    static const char *n[] = {"5", "SUS", "TRIAD", "ADD9", "7TH"};
    return n[std::clamp(c, 0, ColourCount - 1)];
}

enum Style : int { Pad, Trance, Organ, Piano, Pluck, Pump, StyleCount };

enum Pattern : int { Hold, Offbeat, Eighths, Tresillo, Charleston, TranceGate, Sixteenths, Wub, PatternCount };

// How a chord sounds. Saw styles are unison sawtooths through a ladder;
// the organ is a drawbar wavetable with percussion and a key click; the
// piano is two-operator FM with a hammer.
struct StyleDef {
    const char *name;
    const char *blurb;
    enum Osc : uint8_t { Saw, OrganOsc, PianoOsc } osc;
    int unison;       // saws per note (the organ and the piano: two copies, one each side)
    float detune;     // cents to the outermost saw
    float a, d, s, r; // amplitude envelope, seconds (decay and release are time constants)
    float cutoff;     // 0..1, octaves above 60 Hz over 8.1
    float envAmt, envDecay, res;
    bool legato = false; // a new chord while one is held glides into it instead of striking again
    bool retrig = false; // gate hits strike the envelope (stabs) rather than chop the sound
    bool spread = false; // open (drop-2) voicings, with the root doubled underneath
    float glide;      // seconds for the voices to move to a new chord
    float centre;     // MIDI pitch the voicing gravitates to
    float gain;
    float pump = 0;   // depth of its own tempo-locked sidechain, 0 for none
    float strum = 0;  // seconds between notes of a fresh chord, low to high
    float width;      // stereo spread of the unison, 0..1
    int pattern;      // the rhythm it comes with
};

// The six sounds. Levels are matched by ear-proxy (RMS of a held
// progression through the studio), stabs a little lower for their peaks.
inline const StyleDef &styleDef(int s) {
    static const StyleDef defs[StyleCount] = {
        // Seven saws a note, slow to swell and slow to fade, open voicings
        // with the root underneath, gliding from chord to chord.
        {.name = "PAD", .blurb = "big supersaw pad", .osc = StyleDef::Saw, .unison = 7, .detune = 26.0f,
         .a = 0.28f, .d = 1.2f, .s = 0.85f, .r = 0.45f, .cutoff = 0.70f, .envAmt = 0.10f, .envDecay = 0.6f,
         .res = 0.12f, .legato = true, .spread = true, .glide = 0.045f, .centre = 62.0f, .gain = 0.36f,
         .width = 1.0f, .pattern = Hold},
        // Brighter and instant, voiced higher, made for its sixteenth gate.
        {.name = "TRANCE", .blurb = "gated supersaw", .osc = StyleDef::Saw, .unison = 7, .detune = 20.0f,
         .a = 0.004f, .d = 0.4f, .s = 0.9f, .r = 0.09f, .cutoff = 0.76f, .envAmt = 0.14f, .envDecay = 0.25f,
         .res = 0.18f, .legato = true, .glide = 0.018f, .centre = 66.0f, .gain = 0.42f, .width = 0.85f,
         .pattern = TranceGate},
        // Drawbars, percussion and click, a quick decay: struck on every hit.
        {.name = "ORGAN", .blurb = "house organ stabs", .osc = StyleDef::OrganOsc, .unison = 2, .detune = 5.0f,
         .a = 0.003f, .d = 0.5f, .s = 0.42f, .r = 0.05f, .cutoff = 0.80f, .envAmt = 0.12f, .envDecay = 0.15f,
         .res = 0.05f, .retrig = true, .glide = 0.02f, .centre = 64.0f, .gain = 0.38f, .width = 0.5f,
         .pattern = Offbeat},
        // Hammered FM that rings down, a little strum from the bottom up.
        {.name = "PIANO", .blurb = "house piano", .osc = StyleDef::PianoOsc, .unison = 2, .detune = 4.0f,
         .a = 0.0015f, .d = 2.4f, .s = 0.0f, .r = 0.22f, .cutoff = 0.88f, .envAmt = 0.10f, .envDecay = 0.45f,
         .res = 0.05f, .retrig = true, .glide = 0.02f, .centre = 63.0f, .gain = 0.25f, .strum = 0.007f,
         .width = 0.6f, .pattern = Charleston},
        // Five saws with a snapping filter and no sustain.
        {.name = "PLUCK", .blurb = "supersaw pluck", .osc = StyleDef::Saw, .unison = 5, .detune = 16.0f,
         .a = 0.001f, .d = 0.55f, .s = 0.0f, .r = 0.25f, .cutoff = 0.36f, .envAmt = 0.62f, .envDecay = 0.2f,
         .res = 0.22f, .retrig = true, .glide = 0.02f, .centre = 67.0f, .gain = 0.4f, .strum = 0.004f,
         .width = 0.8f, .pattern = Tresillo},
        // Four saws through a resonant ladder held low (the twist opens it),
        // ducking itself on every beat.
        {.name = "PUMP", .blurb = "filtered, sidechained", .osc = StyleDef::Saw, .unison = 4, .detune = 13.0f,
         .a = 0.012f, .d = 0.5f, .s = 0.92f, .r = 0.22f, .cutoff = 0.56f, .envAmt = 0.12f, .envDecay = 0.4f,
         .res = 0.42f, .legato = true, .glide = 0.025f, .centre = 60.0f, .gain = 0.44f, .pump = 0.82f,
         .width = 0.7f, .pattern = Hold},
    };
    return defs[std::clamp(s, 0, StyleCount - 1)];
}

// A rhythm for a held chord, one bar long: '.' rest, 'x' a hit, 'X' an
// accented hit. Hits last up to two steps, scaled by the gate length.
struct GateDef {
    const char *name;
    int steps;
    const char *hits;
    bool smooth; // a swell per hit (the future-bass wub) rather than a hard gate
};

inline const GateDef &gateDef(int p) {
    static const GateDef defs[PatternCount] = {
        {"HOLD", 16, "................", false},
        {"OFFBEAT", 16, "..x...x...x...x.", false},
        {"8THS", 16, "X.x.X.x.X.x.X.x.", false},
        {"3·3·2", 16, "X..x..x.X..x..x.", false},
        {"CHARLESTON", 16, "X.....x.X.....x.", false},
        {"TRANCE", 16, "X.xx.xx.X.xx.xx.", false},
        {"16THS", 16, "XxxxXxxxXxxxXxxx", false},
        {"WUB", 12, "XxxXxxXxxXxx", true},
    };
    return defs[std::clamp(p, 0, PatternCount - 1)];
}

// Command layout. On: i[0] degree, i[1] colour, i[2] style, i[3] register
// (voicing steps up or down); f[0] velocity, f[1] pressure, f[2] big (a
// flat finger: octave doublings), f[3] filter (0..1, 0.5 the sound's own).
// Move: i[1] colour, i[3] register, f[1] pressure, f[2] big, f[3] filter.
// A chord carries its sound, so a loop keeps the sound it was played with,
// and its filter, so a sweep loops too.
//
// Rhythm belongs to the sound: each style keeps its own gate (pattern,
// length, swing), so an organ loop goes on stabbing the offbeat while a pad
// is played over it, and changing the organ's gate changes the loop's too.
// These are page settings, not performance, and never go into clips.
// Param: i[0] one of these, i[1] the style, f[0] the value.
enum ParamId : int {
    ParamPattern = 0, // a style's gate pattern
    ParamOverride,    // a held rail: this pattern over every sound while it is down (−1 none)
    ParamRate,        // extra rail fingers: every gate twice or four times as fast
    ParamLength,      // a style's gate length, 0..1
    ParamSwing,       // a style's swing, 0..0.4 of a sixteenth
};

// What the engine publishes for the page.
namespace pub {
constexpr int LaneStride = 16; // live lane at 0, replayed lane at 16
constexpr int Degree = 0, Colour = 1, Style = 2, Level = 3, Notes = 4, NoteCount = 10, Sounding = 11, Register = 12,
              Filter = 13;
constexpr int Gate = 32;     // per style: the gate's level now
constexpr int Pattern = 40;  // per style: the pattern sounding (override included)
constexpr int Rate = 46, Pump = 47;
constexpr int Columns = 48;  // per lane 8 apart: how loud each degree is sounding
}

constexpr int kLiveLane = 0, kReplayLane = 1, kLanes = 2;
inline int laneOf(int id) { return (id >> 24) != 0 ? kReplayLane : kLiveLane; }

// --- harmony ----------------------------------------------------------------

// A chord as degrees of the key, root first.
struct Shape {
    int count = 0;
    int tones[4] = {};
};

inline double interval(const Tuning &t, int d, int k) { return t.degreeSemis(d + k) - t.degreeSemis(d); }

// The step above degree d whose interval comes closest to target semitones:
// the same code finds a third in a seven-note mode, a pentatonic scale or
// the overtone row.
inline int nearestStep(const Tuning &t, int d, double target) {
    int best = 1;
    double err = 1e9;
    for (int k = 1; k < 2 * t.degreesPerOctave(); ++k) {
        const double e = std::fabs(interval(t, d, k) - target);
        if (e < err - 1e-6) {
            err = e;
            best = k;
        }
    }
    return best;
}

// Insertion sort for the handful of notes in a chord (std::sort trips the
// compiler's bounds checks on arrays this small).
template <typename T>
void sortSmall(T *a, int n) {
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && a[j] < a[j - 1]; --j)
            std::swap(a[j], a[j - 1]);
}

inline bool near(double a, double b) { return std::fabs(a - b) < 0.3; }

// The degrees a colour makes on a degree of the current key. Everything
// stays in the key; the only choices are the ones a producer would make
// anyway: no diminished fifth in a plain chord (it becomes the seventh,
// the soft shell of a half-diminished chord), no minor ninth over the root
// (it becomes the eleventh), and a sus that clashes swaps sides.
inline Shape shapeOf(const Tuning &t, int degree, int colour) {
    const int n = t.degreesPerOctave();
    const int third = nearestStep(t, degree, 3.5), fifth = nearestStep(t, degree, 7.0);
    const int seventh = nearestStep(t, degree, 10.5), second = nearestStep(t, degree, 2.0);
    const int fourth = nearestStep(t, degree, 5.0);
    const bool dimFifth = near(interval(t, degree, fifth), 6.0);
    const int top = dimFifth ? seventh : fifth; // a plain chord's upper tone
    // The colour tone above the octave: the ninth unless it is a minor
    // ninth, then the eleventh.
    const int ninth = (near(interval(t, degree, second), 1.0) ? fourth : second) + n;
    Shape s;
    switch (colour) {
    case Power: {
        // A fifth and the octave; a degree without a perfect fifth takes its
        // fourth, which still rings like a power chord.
        const int f = dimFifth ? fourth : fifth;
        s.count = 3;
        s.tones[0] = degree, s.tones[1] = degree + f, s.tones[2] = degree + n;
        return s;
    }
    case Sus:
        if (near(interval(t, degree, fourth), 5.0) && !dimFifth)
            s.count = 3, s.tones[0] = degree, s.tones[1] = degree + fourth, s.tones[2] = degree + fifth;
        else if (near(interval(t, degree, fourth), 5.0))
            s.count = 3, s.tones[0] = degree, s.tones[1] = degree + fourth, s.tones[2] = degree + seventh;
        else if (near(interval(t, degree, second), 2.0))
            s.count = 3, s.tones[0] = degree, s.tones[1] = degree + second, s.tones[2] = degree + top;
        else
            s.count = 3, s.tones[0] = degree, s.tones[1] = degree + third, s.tones[2] = degree + top;
        return s;
    case Add9:
        s.count = 4, s.tones[0] = degree, s.tones[1] = degree + third, s.tones[2] = degree + top,
        s.tones[3] = degree + ninth;
        return s;
    case Seventh:
        s.count = 4, s.tones[0] = degree, s.tones[1] = degree + third, s.tones[2] = degree + fifth,
        s.tones[3] = degree + seventh;
        return s;
    case Triad:
    default:
        s.count = 3, s.tones[0] = degree, s.tones[1] = degree + third, s.tones[2] = degree + top;
        return s;
    }
}

// The notes of a chord as MIDI pitches, lowest first.
struct Voicing {
    int count = 0;
    float notes[4] = {};
};

// Picks the voicing of a chord: every inversion in every octave, close or
// open, scored by how far the voices move from the last chord (the smaller,
// the smoother), how far the top voice jumps, how far the whole drifts from
// the register it should live in, and whether it stays above the bass's
// territory. Without a last chord it prefers root position at the centre.
inline Voicing voiceChord(const Tuning &t, const Shape &shape, double centre, bool spread, const Voicing *prev) {
    const double base = t.baseMidi();
    // Pitch classes, as semitones 0..12 above the key's base, root first.
    double pcs[4];
    int m = 0;
    const double rootPc = std::fmod(t.degreeSemis(shape.tones[0]) + 1200.0, 12.0);
    for (int i = 0; i < shape.count; ++i) {
        const double pc = std::fmod(t.degreeSemis(shape.tones[i]) + 1200.0, 12.0);
        bool dup = false;
        for (int j = 0; j < m; ++j)
            dup |= std::fabs(pcs[j] - pc) < 0.01;
        if (!dup)
            pcs[m++] = pc;
    }
    sortSmall(pcs, m);
    const bool power = m == 2; // root and fifth (and the octave)
    const double lo = 48.0, hi = 86.0;
    Voicing best;
    double bestCost = 1e18;
    auto consider = [&](double *notes, int count, bool rootBottom, double bias) {
        sortSmall(notes, count);
        double cost = bias;
        // Neighbouring voices a semitone apart rub; low in the chord they
        // turn to mud, so there they are as good as forbidden (an add9 puts
        // its ninth on top, a major seventh keeps its root below it).
        for (int i = 0; i + 1 < count; ++i)
            if (notes[i + 1] - notes[i] < 1.5)
                cost += notes[i] < 72.0 ? 40.0 : 4.0;
        if (notes[0] < lo)
            cost += 4.0 * (lo - notes[0]);
        if (notes[count - 1] > hi)
            cost += 4.0 * (notes[count - 1] - hi);
        double mean = 0;
        for (int i = 0; i < count; ++i)
            mean += notes[i];
        mean /= count;
        if (prev && prev->count > 0) {
            // Each voice to its nearest neighbour in the last chord, both ways
            // round, with leaps beyond a fourth costing double.
            auto move = [](double d) { return d <= 5.0 ? d : 5.0 + 2.0 * (d - 5.0); };
            for (int i = 0; i < count; ++i) {
                double d = 1e9;
                for (int j = 0; j < prev->count; ++j)
                    d = std::min(d, std::fabs(notes[i] - double(prev->notes[j])));
                cost += move(d);
            }
            for (int j = 0; j < prev->count; ++j) {
                double d = 1e9;
                for (int i = 0; i < count; ++i)
                    d = std::min(d, std::fabs(notes[i] - double(prev->notes[j])));
                cost += move(d);
            }
            cost += 0.6 * move(std::fabs(notes[count - 1] - double(prev->notes[prev->count - 1])));
            cost += 0.45 * std::fabs(mean - centre);
        } else {
            cost += 0.8 * std::fabs(mean - centre) + (rootBottom ? 0.0 : 2.5);
        }
        if (cost < bestCost) {
            bestCost = cost;
            best.count = count;
            for (int i = 0; i < count; ++i)
                best.notes[i] = float(notes[i]);
        }
    };
    for (int oct = 0; oct < 6; ++oct) {
        if (power) {
            // Root, fifth, octave; or the fifth underneath, which keeps a
            // run of power chords from leaping.
            const double r = base + rootPc + 12.0 * oct;
            const double f = base + (pcs[0] == rootPc ? pcs[1] : pcs[0]) + 12.0 * oct;
            const double fifthUp = f > r ? f : f + 12.0;
            double a[3] = {r, fifthUp, r + 12.0};
            consider(a, 3, true, 0.0);
            double b[3] = {fifthUp - 12.0, r, fifthUp};
            consider(b, 3, false, 0.0);
            continue;
        }
        for (int inv = 0; inv < m; ++inv) {
            double notes[4];
            double last = -1e9;
            for (int i = 0; i < m; ++i) {
                double p = base + pcs[(inv + i) % m] + 12.0 * oct;
                while (p <= last)
                    p += 12.0;
                notes[i] = last = p;
            }
            const bool rootBottom = std::fabs(std::fmod(notes[0] - base + 1200.0, 12.0) - rootPc) < 0.01;
            // Pads prefer open (drop-2) voicings, the second voice from the
            // top an octave down, but take a close one when it moves less.
            double close[4];
            std::copy(notes, notes + m, close);
            consider(close, m, rootBottom, spread ? 1.5 : 0.0);
            if (spread && m >= 3) {
                double open[4];
                std::copy(notes, notes + m, open);
                open[m - 2] -= 12.0;
                const bool openRoot = m - 2 == 0 ? std::fabs(std::fmod(open[0] - base + 1200.0, 12.0) - rootPc) < 0.01
                                                 : rootBottom;
                consider(open, m, openRoot, 0.0);
            }
        }
    }
    return best;
}

// --- sound ------------------------------------------------------------------

constexpr int kTable = 2048;

// A sine and the organ's drawbar wave, one cycle each, read with linear
// interpolation.
struct Tables {
    float sine[kTable + 1];
    float organ[kTable + 1];
    Tables() {
        // Harmonics one to eight as drawbars 8', 4', 2 2/3', 2', 1 3/5',
        // 1 1/3' and 1' (there is none for the seventh): the bright, slightly
        // hollow mix of the house organ.
        static const float bars[] = {1.0f, 0.75f, 0.55f, 0.45f, 0.18f, 0.22f, 0.0f, 0.3f};
        float peak = 0;
        for (int i = 0; i <= kTable; ++i) {
            const double ph = double(i) / kTable;
            sine[i] = float(std::sin(2.0 * M_PI * ph));
            double o = 0;
            for (int h = 0; h < 8; ++h)
                o += bars[h] * std::sin(2.0 * M_PI * ph * (h + 1));
            organ[i] = float(o);
            peak = std::max(peak, std::fabs(organ[i]));
        }
        for (float &v : organ)
            v /= peak;
    }
};

inline const Tables &tables() {
    static const Tables t;
    return t;
}

inline float lut(const float *t, float ph) {
    const float x = ph * float(kTable);
    const int i = int(x);
    const float f = x - float(i);
    return t[i] + f * (t[i + 1] - t[i]);
}

inline float wrap01(float x) { return x - std::floor(x); }

// An envelope with its coefficients worked out once, not per sample.
struct Env {
    enum Stage : uint8_t { Idle, Attack, Decay, Release } stage = Idle;
    float v = 0, s = 0.7f, ca = 0.1f, cd = 0.001f, cr = 0.001f;
    void set(float a, float d, float sus, float r, float rate) {
        ca = 1.0f - std::exp(-1.0f / (std::max(a, 0.0004f) * rate));
        cd = 1.0f - std::exp(-1.0f / (std::max(d, 0.001f) * rate));
        cr = 1.0f - std::exp(-1.0f / (std::max(r, 0.001f) * rate));
        s = sus;
    }
    void on() { stage = Attack; }
    void off() {
        if (stage != Idle)
            stage = Release;
    }
    inline float step() {
        switch (stage) {
        case Attack:
            v += (1.25f - v) * ca;
            if (v >= 1.0f) {
                v = 1.0f;
                stage = Decay;
            }
            break;
        case Decay:
            v += (s - v) * cd;
            break;
        case Release:
            v -= v * cr;
            if (v < 1e-5f) {
                v = 0;
                stage = Idle;
            }
            break;
        case Idle:
            break;
        }
        return v;
    }
};

// A style's rhythm for one block: the gate's level per sample, and where
// hits start and end (for the styles whose gate strikes).
struct GateBlock {
    bool active = false;
    bool smooth = false;
    float g[kBlock] = {};
    uint8_t hitOn[kBlock] = {}, hitOff[kBlock] = {};
};

// One sounding chord: up to four voiced notes plus two doublings (the root
// an octave under, the top an octave over), each a stack of detuned
// oscillators, through one stereo filter and one pair of envelopes.
struct ChordVoice {
    static constexpr int kNotes = 6, kUnison = 7;
    enum Role : uint8_t { Core, Low, Top };

    struct Note {
        float midi = 60, target = 60;
        float amp = 0, ampTarget = 0, ampPrev = 0;
        Role role = Core;
        bool exists = true; // a doubling the voicing has room for
        int delay = 0;      // a strum: samples before this note speaks
        float onset = 1;    // a strummed note fades in over a couple of milliseconds once it speaks
        float phase[kUnison] = {};
        float phase2[2] = {}; // the piano's modulators, the organ's percussion
    };

    bool active = false, releasing = false;
    int id = -1, lane = 0, style = 0, degree = 0, colour = 0, reg = 0;
    uint64_t born = 0;
    int delay = 0;      // samples before the chord starts (a replayed onset inside the block)
    float vel = 0.8f;
    float pressure = 0.5f, pressureSm = 0.5f, big = 0, bigSm = 0, filter = 0.5f, filterSm = 0.5f;
    float level = 0;
    float glideCoef = 0.1f;
    Note notes[kNotes];
    Env amp, filt;
    float strike = 0;   // the piano's hammer and FM brightness, the organ's percussion and click
    float clickEnv = 0;
    Ladder ladL, ladR;
    Svf svfL, svfR, clickBp;
    float ratio[kUnison] = {}, panL[kUnison] = {}, panR[kUnison] = {};
    int unison = 1;
    float graceEnv = 0;  // keeps a chopping gate open for a fresh chord, so a tap always speaks
    int graceHold = 0;
    int minHold = 0;     // a struck chord rings at least this long before a gate may end it
    bool lateRelease = false; // let go before it spoke: release once it has rung its shortest
    float leslie = 0;    // the organ's slow rotor
    Rng rng{0x1234567u};

    void init(uint32_t seed) { rng = Rng(seed); }
};

// --- the engine -------------------------------------------------------------

// The organ's and the piano's balance of the copy in the middle and the
// detuned copy at the sides (scaled by the style's width).
constexpr float kMid = 0.74f, kSide = 0.5f;

class Engine : public PartEngine {
public:
    Engine() {
        for (int k = 0; k < StyleCount; ++k)
            m_rhythm[k].pattern = styleDef(k).pattern;
    }

    void init(float rate) override {
        m_rate = rate;
        (void)tables();
        for (size_t i = 0; i < m_voices.size(); ++i)
            m_voices[i].init(0xC40D5u + uint32_t(i) * 2654435761u);
        for (Svf *f : {&m_hp1L, &m_hp1R})
            f->set(140.0f, 0.54f, rate);
        for (Svf *f : {&m_hp2L, &m_hp2R})
            f->set(140.0f, 1.31f, rate);
    }

    void command(const Command &c, AudioContext &ctx) override {
        const int lane = laneOf(c.id);
        Lane &L = m_lanes[lane];
        checkKey(ctx);
        switch (c.type) {
        case Command::On: {
            Entry e;
            e.id = c.id;
            e.degree = c.i[0];
            e.colour = std::clamp(c.i[1], 0, ColourCount - 1);
            e.style = std::clamp(c.i[2], 0, StyleCount - 1);
            e.reg = std::clamp(c.i[3], -4, 4);
            e.vel = clampf(c.f[0], 0.05f, 1.0f);
            e.pressure = clampf(c.f[1], 0.0f, 1.0f);
            e.big = clampf(c.f[2], 0.0f, 1.0f);
            e.filter = clampf(c.f[3], 0.0f, 1.0f);
            remove(L, c.id);
            if (L.depth < int(std::size(L.stack)))
                L.stack[L.depth++] = e;
            else
                L.stack[L.depth - 1] = e;
            int offset = ctx.offsetOf(c.t, kBlock);
            offset += waitForHit(e.style, ctx.transport(), offset);
            sound(lane, e, offset, true, ctx);
            break;
        }
        case Command::Off: {
            const int at = find(L, c.id);
            if (at < 0) {
                // Not held (a clip stopping, a page leaving twice): just make sure
                // nothing with this id keeps sounding.
                for (auto &v : m_voices)
                    if (v.active && v.id == c.id && !v.releasing)
                        release(v);
                break;
            }
            const bool front = at == L.depth - 1;
            remove(L, c.id);
            if (!front)
                break;
            ChordVoice *cur = current(L);
            if (L.depth > 0 && cur && styleDef(cur->style).legato && L.stack[L.depth - 1].style == cur->style) {
                // A legato sound glides back to the chord still held.
                sound(lane, L.stack[L.depth - 1], 0, false, ctx);
            } else {
                if (cur)
                    release(*cur);
                L.voice = -1;
                if (lane == kLiveLane && m_lanes[kReplayLane].voice >= 0)
                    announce(kReplayLane, ctx);
            }
            break;
        }
        case Command::Move: {
            const int at = find(L, c.id);
            if (at < 0)
                break;
            Entry &e = L.stack[at];
            const int colour = std::clamp(c.i[1], 0, ColourCount - 1), reg = std::clamp(c.i[3], -4, 4);
            const bool revoice = colour != e.colour || reg != e.reg;
            e.colour = colour;
            e.reg = reg;
            e.pressure = clampf(c.f[1], 0.0f, 1.0f);
            e.big = clampf(c.f[2], 0.0f, 1.0f);
            e.filter = clampf(c.f[3], 0.0f, 1.0f);
            if (at != L.depth - 1)
                break;
            if (revoice)
                sound(lane, e, 0, false, ctx);
            if (ChordVoice *v = current(L)) {
                v->pressure = e.pressure;
                v->big = e.big;
                v->filter = e.filter;
            }
            break;
        }
        case Command::Param: {
            const float v = c.f[0];
            Rhythm &R = m_rhythm[std::clamp(c.i[1], 0, StyleCount - 1)];
            switch (c.i[0]) {
            case ParamPattern: R.pattern = std::clamp(int(std::lround(v)), 0, PatternCount - 1); break;
            case ParamOverride: m_override = std::clamp(int(std::lround(v)), -1, PatternCount - 1); break;
            case ParamRate: m_globalRate = std::clamp(int(std::lround(v)), 1, 4); break;
            case ParamLength: R.length = clampf(v, 0.1f, 1.0f); break;
            case ParamSwing: R.swing = clampf(v, 0.0f, 0.4f); break;
            default: break;
            }
            break;
        }
        default:
            break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        checkKey(ctx);
        const Transport &tr = ctx.transport();
        bool used[StyleCount] = {};
        for (auto &v : m_voices)
            if (v.active)
                used[v.style] = true;
        for (int k = 0; k < StyleCount; ++k)
            computeGate(k, tr, n, used[k]);
        if (used[Pump])
            computePump(tr, n);
        // A different chord held live over a loop would sound two harmonies
        // at once while the studio announces one: the loop steps back
        // (−18 dB in about 30 ms) until the finger lifts. The same chord
        // over the loop leaves it whole.
        bool loopSounding = false;
        for (const auto &v : m_voices)
            loopSounding |= v.active && v.lane == kReplayLane;
        const ChordVoice *live = current(m_lanes[kLiveLane]);
        const Shape &loopShape = m_lanes[kReplayLane].shape;
        auto column = [&](int d) { return ((d % m_count) + m_count) % m_count; };
        const bool clash = live && loopSounding && loopShape.count > 0 &&
                           column(live->degree) != column(loopShape.tones[0]);
        const float loop0 = m_loopGain;
        m_loopGain += ((clash ? 0.125f : 1.0f) - m_loopGain) * (1.0f - std::exp(-float(n) / (0.01f * m_rate)));
        float mixL[kBlock] = {}, mixR[kBlock] = {};
        for (auto &v : m_voices)
            if (v.active) {
                const bool loop = v.lane == kReplayLane;
                renderVoice(v, mixL, mixR, n, loop ? loop0 : 1.0f, loop ? m_loopGain : 1.0f);
            }
        // Chords leave the low end to the bass: a fourth-order high-pass
        // under 140 Hz.
        for (int i = 0; i < n; ++i) {
            l[i] += m_hp2L.step(m_hp1L.step(mixL[i]).high).high;
            r[i] += m_hp2R.step(m_hp1R.step(mixR[i]).high).high;
        }
    }

    void silence(bool hard) override {
        for (auto &L : m_lanes) {
            L.depth = 0;
            L.voice = -1;
        }
        m_override = -1;
        m_globalRate = 1;
        for (auto &v : m_voices) {
            if (!v.active)
                continue;
            if (hard)
                v.active = false;
            else
                release(v);
        }
    }

    void publish(float *data) override {
        for (int k = 0; k < kLanes; ++k) {
            float *d = data + k * pub::LaneStride;
            const Lane &L = m_lanes[k];
            const ChordVoice *v = L.shown >= 0 ? &m_voices[size_t(L.shown)] : nullptr;
            if (v && (!v->active || v->lane != k))
                v = nullptr;
            d[pub::Degree] = v ? float(v->degree) : -1.0f;
            d[pub::Colour] = v ? float(v->colour) : 0.0f;
            d[pub::Style] = v ? float(v->style) : 0.0f;
            d[pub::Level] = v ? std::min(1.0f, v->level * 3.0f) : 0.0f;
            d[pub::Register] = v ? float(v->reg) : 0.0f;
            d[pub::Filter] = v ? v->filter : 0.5f;
            int count = 0;
            float notes[ChordVoice::kNotes];
            if (v)
                for (const auto &nt : v->notes)
                    if (nt.ampTarget > 0.05f && count < ChordVoice::kNotes)
                        notes[count++] = nt.target;
            sortSmall(notes, count);
            for (int i = 0; i < 6; ++i)
                d[pub::Notes + i] = i < count ? notes[i] : 0.0f;
            d[pub::NoteCount] = float(count);
            d[pub::Sounding] = L.voice >= 0 ? 1.0f : 0.0f;
            float cols[8] = {};
            for (const auto &w : m_voices)
                if (w.active && w.lane == k) {
                    const int c = ((w.degree % m_count) + m_count) % m_count;
                    if (c < 8)
                        cols[c] = std::max(cols[c], std::min(1.0f, w.level * 3.0f));
                }
            for (int c = 0; c < 8; ++c)
                data[pub::Columns + 8 * k + c] = cols[c];
        }
        for (int s = 0; s < StyleCount; ++s) {
            data[pub::Gate + s] = m_rhythm[s].gateView;
            data[pub::Pattern + s] = float(m_override >= 0 ? m_override : m_rhythm[s].pattern);
        }
        data[pub::Rate] = float(m_globalRate);
        data[pub::Pump] = m_pumpView;
    }

    int voices() const override {
        int n = 0;
        for (const auto &v : m_voices)
            n += v.active;
        return n;
    }

private:
    struct Entry {
        int id = -1, degree = 0, colour = Triad, style = Pad, reg = 0;
        float vel = 0.8f, pressure = 0.5f, big = 0, filter = 0.5f;
    };
    // The chords held in one lane (live, or a loop's), newest last: the
    // newest sounds, and lifting it returns to the one under it.
    struct Lane {
        Entry stack[12];
        int depth = 0;
        int voice = -1; // the voice sounding the front chord while it is held
        int shown = -1; // the voice to show: the front, or the last one ringing out
        Shape shape;
        Voicing last;
        bool hasLast = false;
        int lastReg = 0;
    };
    // A style's gate, and where it is in the bar.
    struct Rhythm {
        int pattern = Hold;
        float length = 0.55f, swing = 0;
        long lastHit = -1;
        float gateSm = 1;
        bool wasOn = false;
        float gateView = 0;
        GateBlock gate;
    };

    static int find(const Lane &L, int id) {
        for (int i = 0; i < L.depth; ++i)
            if (L.stack[i].id == id)
                return i;
        return -1;
    }
    static void remove(Lane &L, int id) {
        const int at = find(L, id);
        if (at < 0)
            return;
        for (int i = at; i + 1 < L.depth; ++i)
            L.stack[i] = L.stack[i + 1];
        --L.depth;
    }
    ChordVoice *current(Lane &L) {
        if (L.voice < 0)
            return nullptr;
        ChordVoice &v = m_voices[size_t(L.voice)];
        return v.active && !v.releasing ? &v : nullptr;
    }

    static void release(ChordVoice &v) {
        v.releasing = true;
        // A stab still waiting for its hit plays it anyway, for its shortest
        // ring, and then lets go: a tap between hits is heard on the next.
        if (v.delay > 0) {
            v.lateRelease = true;
            return;
        }
        v.amp.off();
        v.filt.off();
    }

    // The studio's key changed under held chords: they move to the new key
    // the way they would move to a new chord.
    void checkKey(AudioContext &ctx) {
        const Tuning &t = ctx.harmony().tuning;
        if (t.root() == m_keyRoot && t.familyIndex() == m_keyFamily && t.brightnessIndex() == m_keyBright &&
            t.just() == m_keyJust)
            return;
        const bool first = m_keyRoot < 0;
        m_keyRoot = t.root();
        m_keyFamily = t.familyIndex();
        m_keyBright = t.brightnessIndex();
        m_keyJust = t.just();
        m_count = t.degreesPerOctave();
        if (first)
            return;
        for (int k = 0; k < kLanes; ++k) {
            Lane &L = m_lanes[k];
            if (L.depth > 0 && current(L))
                sound(k, L.stack[L.depth - 1], 0, false, ctx, false);
        }
    }

    int effectivePattern(int style) const { return m_override >= 0 ? m_override : m_rhythm[style].pattern; }

    // A struck sound under a gate speaks on the gate's hits: a chord landing
    // between them waits (in samples, returned) for the next one, so an
    // organ on the offbeat never stabs the downbeat because a finger moved
    // there. Landing in the first part of a hit's step strikes at once.
    int waitForHit(int style, const Transport &tr, int offset) const {
        const int pattern = effectivePattern(style);
        if (!styleDef(style).retrig || pattern == Hold)
            return 0;
        const GateDef &g = gateDef(pattern);
        const double stepsPerBeat = double(g.steps * m_globalRate) / kBeatsPerBar;
        const double pos = tr.beatAt(offset) * stepsPerBeat;
        const long k = long(std::floor(pos));
        if (g.hits[k % g.steps] != '.' && pos - double(k) < 0.4)
            return 0;
        long next = k + 1;
        while (g.hits[next % g.steps] == '.' && next < k + g.steps)
            ++next;
        return int((double(next) - pos) / stepsPerBeat / tr.beatsPerSample);
    }

    // Plays the lane's front chord: a fresh onset (a finger landing) or a
    // change to the chord already sounding (a slide to another colour, a
    // register step, a held finger returning to the front, a key change).
    void sound(int lane, const Entry &e, int offset, bool fresh, AudioContext &ctx, bool tell = true) {
        Lane &L = m_lanes[lane];
        const Tuning &t = ctx.harmony().tuning;
        m_count = t.degreesPerOctave();
        const StyleDef &sd = styleDef(e.style);
        const Shape shape = shapeOf(t, e.degree, e.colour);
        const double centre = sd.centre + 3.5 * e.reg;
        const Voicing *prev = L.hasLast && L.lastReg == e.reg ? &L.last : nullptr;
        const Voicing vc = voiceChord(t, shape, centre, sd.spread, prev);
        const double rootMidi = t.baseMidi() + t.degreeSemis(e.degree);
        ChordVoice *cur = current(L);
        const bool same = cur && cur->degree == e.degree && cur->colour == e.colour && cur->reg == e.reg;
        if (cur && cur->style == e.style && (sd.legato || !fresh)) {
            morph(*cur, vc, rootMidi);
            cur->id = e.id;
            cur->degree = e.degree;
            cur->colour = e.colour;
            cur->reg = e.reg;
            cur->pressure = e.pressure;
            cur->big = e.big;
            cur->filter = e.filter;
            if (fresh) {
                // Landing on the chord already sounding strikes it again;
                // landing on a new one blooms the filter as the voices glide.
                if (same) {
                    cur->amp.on();
                    cur->vel = e.vel;
                    cur->strike = 1;
                }
                cur->filt.on();
                cur->graceEnv = 1;
                cur->graceHold = int(0.06f * m_rate);
            }
        } else {
            if (cur)
                release(*cur);
            ChordVoice &v = allocate();
            start(v, e, lane, vc, rootMidi, offset);
            L.voice = int(&v - m_voices.data());
        }
        L.shown = L.voice;
        L.last = vc;
        L.hasLast = true;
        L.lastReg = e.reg;
        L.shape = shape;
        if (tell)
            announce(lane, ctx);
    }

    // The chord of the moment for Bass, Arp and Lead. A chord held live wins
    // over a loop's while the finger is down.
    void announce(int lane, AudioContext &ctx) {
        if (lane == kReplayLane && current(m_lanes[kLiveLane]))
            return;
        const Shape &s = m_lanes[lane].shape;
        if (s.count > 0)
            ctx.setChord(s.tones[0], s.tones, s.count);
    }

    ChordVoice &allocate() {
        for (auto &v : m_voices)
            if (!v.active)
                return v;
        // All ten busy: take the quietest one ringing out, else the oldest.
        ChordVoice *best = nullptr;
        for (auto &v : m_voices)
            if (v.releasing && (!best || v.level < best->level))
                best = &v;
        if (!best)
            for (auto &v : m_voices)
                if (!best || v.born < best->born)
                    best = &v;
        for (auto &L : m_lanes)
            if (L.voice == int(best - m_voices.data()))
                L.voice = -1;
        return *best;
    }

    // Where the doublings go: the root an octave under the lowest note (pads,
    // and any chord played with a flat finger), the top note an octave over
    // (a flat finger). 0 when there is no room.
    static void doublings(const Voicing &vc, double rootMidi, float &low, float &top) {
        double r = rootMidi;
        while (r > vc.notes[0] - 0.5)
            r -= 12.0;
        while (r < 48.0)
            r += 12.0;
        low = r < vc.notes[0] - 0.5 ? float(r) : 0.0f;
        top = vc.notes[vc.count - 1] + 12.0f <= 98.0f ? vc.notes[vc.count - 1] + 12.0f : 0.0f;
    }

    void start(ChordVoice &v, const Entry &e, int lane, const Voicing &vc, double rootMidi, int offset) {
        const StyleDef &sd = styleDef(e.style);
        v.active = true;
        v.releasing = false;
        v.id = e.id;
        v.lane = lane;
        v.style = e.style;
        v.degree = e.degree;
        v.colour = e.colour;
        v.reg = e.reg;
        v.born = ++m_age;
        v.delay = offset;
        v.vel = e.vel;
        v.pressure = v.pressureSm = e.pressure;
        v.big = v.bigSm = e.big;
        v.filter = v.filterSm = e.filter;
        v.level = 0;
        v.strike = 1;
        v.clickEnv = 1;
        v.graceEnv = 1;
        v.graceHold = int(0.06f * m_rate);
        v.minHold = int(0.11f * m_rate);
        v.lateRelease = false;
        v.glideCoef = 1.0f - std::exp(-float(kBlock) / (std::max(sd.glide, 0.005f) * m_rate));
        v.amp.set(sd.a, sd.d, sd.s, sd.r, m_rate);
        v.filt.set(0.001f, sd.envDecay, 0.0f, 0.3f, m_rate);
        v.amp.v = 0;
        v.filt.v = 0;
        v.amp.on();
        v.filt.on();
        v.ladL.reset();
        v.ladR.reset();
        v.svfL.reset();
        v.svfR.reset();
        v.clickBp.reset();
        v.clickBp.set(e.style == Piano ? 2600.0f : 3400.0f, 1.2f, m_rate);
        // The unison: detunes spread symmetrically with the outer pair
        // furthest, each saw panned its own way. The organ and the piano use
        // two copies a few cents apart, one each side.
        static const float spread7[] = {0.0f, -0.29f, 0.31f, -0.62f, 0.6f, -1.0f, 0.97f};
        static const float spread5[] = {0.0f, -0.42f, 0.4f, -1.0f, 0.95f};
        static const float spread4[] = {-0.22f, 0.24f, -1.0f, 0.93f};
        const bool saw = sd.osc == StyleDef::Saw;
        const float *sp = sd.unison >= 7 ? spread7 : sd.unison == 5 ? spread5 : spread4;
        v.unison = saw ? std::min(sd.unison, ChordVoice::kUnison) : 2;
        for (int u = 0; u < ChordVoice::kUnison; ++u) {
            // The organ and the piano: a centre copy in tune and a side copy
            // detuned (see renderVoice), so they widen without thinning in mono.
            const float d = saw ? (u < v.unison ? sp[u] : 0.0f) : (u == 0 ? 0.0f : 1.0f);
            v.ratio[u] = std::exp2(d * sd.detune / 1200.0f);
            const float pan = saw ? clampf(-d * sd.width * 1.05f, -1.0f, 1.0f) : (u == 0 ? -sd.width : sd.width);
            v.panL[u] = std::sqrt(0.5f * (1.0f - pan));
            v.panR[u] = std::sqrt(0.5f * (1.0f + pan));
        }
        float low = 0, top = 0;
        doublings(vc, rootMidi, low, top);
        for (int k = 0; k < ChordVoice::kNotes; ++k) {
            auto &nt = v.notes[k];
            nt.delay = 0;
            nt.onset = 1;
            nt.exists = true;
            nt.ampPrev = nt.amp = nt.ampTarget = 0;
            for (float &p : nt.phase)
                p = v.rng.unit();
            nt.phase2[0] = nt.phase2[1] = 0;
            if (k < vc.count) {
                nt.role = ChordVoice::Core;
                nt.midi = nt.target = vc.notes[k];
                nt.amp = nt.ampPrev = nt.ampTarget = 1;
                nt.delay = int(sd.strum * float(k) * m_rate);
                nt.onset = nt.delay > 0 ? 0.0f : 1.0f;
            } else if (k == 4) {
                nt.role = ChordVoice::Low;
                nt.exists = low > 0;
                nt.midi = nt.target = low > 0 ? low : vc.notes[0];
                nt.amp = nt.ampPrev = nt.ampTarget = lowAmp(sd, v.big, nt.exists);
            } else if (k == 5) {
                nt.role = ChordVoice::Top;
                nt.exists = top > 0;
                nt.midi = nt.target = top > 0 ? top : vc.notes[vc.count - 1];
                nt.amp = nt.ampPrev = nt.ampTarget = nt.exists ? 0.55f * v.big : 0.0f;
                nt.delay = int(sd.strum * float(vc.count) * m_rate);
                nt.onset = nt.delay > 0 ? 0.0f : 1.0f;
            } else {
                nt.role = ChordVoice::Core;
                nt.midi = nt.target = vc.notes[0];
            }
        }
    }

    static float lowAmp(const StyleDef &sd, float big, bool has) {
        return has ? (sd.spread ? 0.5f : 0.0f) + 0.45f * big : 0.0f;
    }

    // The voices of the sounding chord walk to the notes of the next one,
    // each to the nearest new note; a voice with nowhere to go fades out as it
    // glides, and a new note fades in where it belongs.
    void morph(ChordVoice &v, const Voicing &vc, double rootMidi) {
        int owned[4], used = 0;
        bool taken[4] = {};
        for (int k = 0; k < 4; ++k)
            if (v.notes[k].ampTarget > 0.01f)
                owned[used++] = k;
        bool assigned[4] = {};
        for (int i = 0; i < vc.count; ++i) {
            int best = -1;
            float dist = 1e9f;
            for (int j = 0; j < used; ++j)
                if (!taken[j] && std::fabs(v.notes[owned[j]].target - vc.notes[i]) < dist) {
                    dist = std::fabs(v.notes[owned[j]].target - vc.notes[i]);
                    best = j;
                }
            if (best >= 0) {
                taken[best] = true;
                v.notes[owned[best]].target = vc.notes[i];
                assigned[i] = true;
            }
        }
        for (int i = 0; i < vc.count; ++i) {
            if (assigned[i])
                continue;
            for (int k = 0; k < 4; ++k) {
                auto &nt = v.notes[k];
                if (nt.ampTarget > 0.01f || nt.amp > 0.02f)
                    continue;
                nt.midi = nt.target = vc.notes[i];
                nt.amp = nt.ampPrev = 0;
                nt.ampTarget = 1;
                nt.delay = 0;
                break;
            }
        }
        for (int j = 0; j < used; ++j)
            if (!taken[j]) {
                auto &nt = v.notes[owned[j]];
                float closest = vc.notes[0];
                for (int i = 0; i < vc.count; ++i)
                    if (std::fabs(vc.notes[i] - nt.target) < std::fabs(closest - nt.target))
                        closest = vc.notes[i];
                nt.target = closest;
                nt.ampTarget = 0;
            }
        float low = 0, top = 0;
        doublings(vc, rootMidi, low, top);
        auto &lo = v.notes[4], &hi = v.notes[5];
        lo.exists = low > 0;
        if (lo.exists)
            lo.target = low;
        hi.exists = top > 0;
        if (hi.exists)
            hi.target = top;
    }

    void computeGate(int style, const Transport &tr, int n, bool used) {
        Rhythm &R = m_rhythm[style];
        GateBlock &G = R.gate;
        const int pattern = effectivePattern(style);
        G.active = pattern != Hold;
        if (!G.active || !used) {
            R.gateSm = G.active ? 0.0f : 1.0f;
            R.gateView = 0;
            R.lastHit = -1;
            R.wasOn = false;
            return;
        }
        const GateDef &g = gateDef(pattern);
        G.smooth = g.smooth;
        const int S = g.steps;
        const double stepsPerBeat = double(S * m_globalRate) / kBeatsPerBar;
        const float atk = 1.0f - std::exp(-1.0f / (0.0012f * m_rate));
        const float rel = 1.0f - std::exp(-1.0f / (0.005f * m_rate));
        const double sw = S == 16 ? double(R.swing) : 0.0;
        for (int i = 0; i < n; ++i) {
            double pos = tr.beatAt(i) * stepsPerBeat;
            if (sw > 0) {
                // Swing: every second sixteenth lands late.
                const double pair = std::floor(pos / 2.0) * 2.0, x = pos - pair;
                pos = pair + (x < 1.0 + sw ? x / (1.0 + sw) : 1.0 + (x - 1.0 - sw) / (1.0 - sw));
            }
            const long k = long(std::floor(pos));
            const int kp = int(((k % S) + S) % S);
            // The hit this moment belongs to: the last one at or before it.
            int back = 0;
            while (back < S && g.hits[(kp - back + S) % S] == '.')
                ++back;
            const long hitAbs = k - back;
            const int h = (kp - back + S) % S;
            int gap = 1;
            while (gap < S && g.hits[(h + gap) % S] == '.')
                ++gap;
            const double dur = std::min(2, gap) * double(R.length);
            const double elapsed = pos - double(hitAbs);
            const float accent = g.hits[h] == 'X' ? 1.0f : 0.8f;
            const bool on = elapsed < dur;
            G.hitOn[i] = on && hitAbs != R.lastHit;
            G.hitOff[i] = !on && R.wasOn;
            if (G.hitOn[i])
                R.lastHit = hitAbs;
            R.wasOn = on;
            if (g.smooth) {
                // A swell and fall over each hit: the future-bass wub.
                const float u = clampf(float(elapsed / std::max(0.05, double(gap) * R.length)), 0.0f, 1.0f);
                const float s = lut(tables().sine, 0.5f * u);
                R.gateSm += (accent * (0.12f + 0.88f * s * s) - R.gateSm) * atk;
            } else {
                const float target = on ? accent : 0.0f;
                R.gateSm += (target - R.gateSm) * (target > R.gateSm ? atk : rel);
            }
            G.g[i] = R.gateSm;
        }
        R.gateView = R.gateSm;
    }

    // A sidechain of its own, locked to the beat, for the pumping style: down
    // on every beat in five milliseconds, back up over most of the beat.
    void computePump(const Transport &tr, int n) {
        const double spb = tr.secondsPerBeat();
        for (int i = 0; i < n; ++i) {
            const double b = tr.beatAt(i);
            const float ph = float(b - std::floor(b));
            const float atk = std::min(1.0f, float(ph * spb) / 0.005f);
            float u = clampf((ph - 0.04f) / 0.6f, 0.0f, 1.0f);
            u = u * u * (3.0f - 2.0f * u);
            m_pump[i] = 1.0f - atk * (1.0f - u);
        }
        m_pumpView = 1.0f - m_pump[n - 1];
    }

    // g0 and g1: a gain ramp over the block (the loop stepping back).
    void renderVoice(ChordVoice &v, float *outL, float *outR, int n, float g0, float g1) {
        int from = 0;
        if (v.delay > 0) {
            if (v.delay >= n) {
                v.delay -= n;
                return;
            }
            from = v.delay;
            v.delay = 0;
        }
        const StyleDef &sd = styleDef(v.style);
        const GateBlock &G = m_rhythm[v.style].gate;
        const float invRate = 1.0f / m_rate;
        v.pressureSm += (v.pressure - v.pressureSm) * 0.06f;
        v.bigSm += (v.big - v.bigSm) * 0.06f;
        v.filterSm += (v.filter - v.filterSm) * 0.15f;
        const float fadeCoef = 1.0f - std::exp(-float(n) / (0.03f * m_rate));
        float topMidi = 0, cores = 0;
        for (auto &nt : v.notes) {
            if (nt.role == ChordVoice::Low)
                nt.ampTarget = lowAmp(sd, v.bigSm, nt.exists);
            else if (nt.role == ChordVoice::Top)
                nt.ampTarget = nt.exists ? 0.55f * v.bigSm : 0.0f;
            // A silent voice jumps; a sounding one glides.
            if (nt.amp < 1e-3f && nt.ampPrev < 1e-3f)
                nt.midi = nt.target;
            else
                nt.midi += (nt.target - nt.midi) * v.glideCoef;
            nt.ampPrev = nt.amp;
            nt.amp += (nt.ampTarget - nt.amp) * fadeCoef;
            if (nt.role == ChordVoice::Core) {
                cores += nt.ampTarget;
                if (nt.ampTarget > 0)
                    topMidi = std::max(topMidi, nt.midi);
            }
        }

        // Oscillators, before the envelope and the filter.
        float bL[kBlock] = {}, bR[kBlock] = {};
        const float invN = 1.0f / float(n);
        const Tables &tb = tables();
        const float noteNorm = 1.0f / std::sqrt(std::max(2.0f, cores));
        for (auto &nt : v.notes) {
            if (nt.amp < 1e-4f && nt.ampPrev < 1e-4f)
                continue;
            int start = from;
            if (nt.delay > 0) {
                start = std::min(n, from + nt.delay);
                nt.delay = std::max(0, nt.delay - (n - from));
            }
            const float hz = midiToHz(nt.midi);
            const float a0 = nt.ampPrev * noteNorm, da = (nt.amp - nt.ampPrev) * noteNorm * invN;
            float nl[kBlock] = {}, nr[kBlock] = {};
            if (sd.osc == StyleDef::Saw) {
                const float uni = 1.0f / std::sqrt(float(v.unison));
                for (int u = 0; u < v.unison; ++u) {
                    float ph = nt.phase[u];
                    const float inc = std::min(hz * v.ratio[u] * invRate, 0.45f);
                    const float invInc = 1.0f / inc;
                    const float gl = v.panL[u] * uni, gr = v.panR[u] * uni;
                    for (int i = start; i < n; ++i) {
                        ph += inc;
                        if (ph >= 1.0f)
                            ph -= 1.0f;
                        float s = 2.0f * ph - 1.0f;
                        if (ph < inc) {
                            const float t = ph * invInc;
                            s -= t + t - t * t - 1.0f;
                        } else if (ph > 1.0f - inc) {
                            const float t = (ph - 1.0f) * invInc;
                            s -= t * t + t + t + 1.0f;
                        }
                        nl[i] += s * gl;
                        nr[i] += s * gr;
                    }
                    nt.phase[u] = ph;
                }
            } else if (sd.osc == StyleDef::OrganOsc) {
                // The drawbar wave in the middle, a copy a few cents off as the
                // stereo side (it widens the stab and leaves mono whole), and
                // the percussion: a decaying third harmonic that gives each
                // stab its bite.
                const float incL = hz * v.ratio[0] * invRate, incR = hz * v.ratio[1] * invRate;
                const float incP = std::min(3.0f * hz * invRate, 0.45f);
                float pl = nt.phase[0], pr = nt.phase[1], pp = nt.phase2[0];
                float perc = v.strike * 0.38f;
                const float percDecay = std::exp(-1.0f / (0.11f * m_rate));
                const float side = kSide * sd.width;
                for (int i = start; i < n; ++i) {
                    pl = wrap01(pl + incL);
                    pr = wrap01(pr + incR);
                    pp = wrap01(pp + incP);
                    const float mid = (lut(tb.organ, pl) + lut(tb.sine, pp) * perc) * kMid;
                    const float sd2 = lut(tb.organ, pr) * side;
                    perc *= percDecay;
                    nl[i] += mid + sd2;
                    nr[i] += mid - sd2;
                }
                nt.phase[0] = pl;
                nt.phase[1] = pr;
                nt.phase2[0] = pp;
            } else {
                // Two-operator FM at 1:1, its index falling with the strike, so
                // a chord starts bright and hammered and mellows as it rings,
                // with an octave modulator for the attack's clang; a second
                // carrier a few cents off as the stereo side.
                const float incL = hz * v.ratio[0] * invRate, incR = hz * v.ratio[1] * invRate;
                const float incM = std::min(hz * 2.0f * invRate, 0.45f);
                float pl = nt.phase[0], pr = nt.phase[1], pm = nt.phase2[0];
                // High notes are less bright, as on the real thing.
                const float bright = clampf(1.4f - (nt.midi - 60.0f) / 40.0f, 0.4f, 1.4f);
                float strike = v.strike;
                const float sDecay = std::exp(-1.0f / (0.32f * m_rate));
                const float idx0 = (0.7f + 2.3f * v.vel) * bright / kTwoPi;
                const float side = kSide * sd.width;
                for (int i = start; i < n; ++i) {
                    pl = wrap01(pl + incL);
                    pr = wrap01(pr + incR);
                    pm = wrap01(pm + incM);
                    const float idx = idx0 * (0.18f + strike);
                    const float mod = lut(tb.sine, pm) * 0.35f * strike / kTwoPi;
                    const float ml = lut(tb.sine, pl) * idx + mod, mr = lut(tb.sine, pr) * idx + mod;
                    const float mid = lut(tb.sine, wrap01(pl + ml)) * kMid, sd2 = lut(tb.sine, wrap01(pr + mr)) * side;
                    nl[i] += mid + sd2;
                    nr[i] += mid - sd2;
                    strike *= sDecay;
                }
                nt.phase[0] = pl;
                nt.phase[1] = pr;
                nt.phase2[0] = pm;
            }
            const float onsetStep = 1.0f / (0.002f * m_rate);
            for (int i = start; i < n; ++i) {
                const float a = (a0 + da * float(i)) * nt.onset;
                if (nt.onset < 1.0f)
                    nt.onset = std::min(1.0f, nt.onset + onsetStep);
                bL[i] += nl[i] * a;
                bR[i] += nr[i] * a;
            }
        }

        // Envelopes, gate, filter, pump.
        const float knob = (v.filterSm - 0.5f) * 0.9f;
        const float build = 0.07f * float(m_globalRate - 1);
        const float keyTrack = topMidi > 0 ? 0.12f * (topMidi - 64.0f) / 24.0f : 0.0f;
        const float base = sd.cutoff + knob + 0.16f * (v.pressureSm - 0.5f) + build + keyTrack;
        const float accent = 0.6f + 0.6f * v.vel;
        const bool chop = G.active && !sd.retrig;
        const bool strikeGate = G.active && sd.retrig;
        const bool ladder = sd.osc == StyleDef::Saw;
        const bool click = sd.osc != StyleDef::Saw;
        const float graceDecay = std::exp(-1.0f / (0.008f * m_rate));
        const float strikeDecay = std::exp(-1.0f / ((v.style == Piano ? 0.32f : 0.11f) * m_rate));
        const float clickDecay = std::exp(-1.0f / (0.0035f * m_rate));
        const float gain = sd.gain * (0.5f + 0.5f * v.vel) * (0.82f + 0.36f * v.pressureSm);
        v.leslie = wrap01(v.leslie + float(n) * 0.85f * invRate);
        const float rot = v.style == Organ ? 0.13f * lut(tb.sine, v.leslie) : 0.0f;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            if (v.minHold > 0 && --v.minHold == 0 &&
                (v.lateRelease || (strikeGate && !v.releasing && G.g[i] < 0.3f))) {
                v.amp.off();
                v.filt.off();
            }
            if (strikeGate && !v.releasing) {
                if (G.hitOn[i]) {
                    v.amp.on();
                    v.filt.on();
                    v.strike = 1;
                    v.clickEnv = 1;
                }
                if (v.minHold == 0 && G.hitOff[i]) {
                    v.amp.off();
                    v.filt.off();
                }
            }
            const float a = v.amp.step();
            const float fe = v.filt.step();
            if (((i - from) & 7) == 0) {
                // A wub opens the filter as it swells: the chord talks.
                const float wub = chop && G.smooth ? 0.32f * (G.g[i] - 0.55f) : 0.0f;
                const float c = clampf(base + sd.envAmt * fe * accent + wub, 0.0f, 1.12f);
                const float hz = std::min(60.0f * std::exp2(c * 8.1f), m_rate * 0.42f);
                if (ladder) {
                    v.ladL.set(hz, sd.res, m_rate);
                    v.ladR.set(hz, sd.res, m_rate);
                } else {
                    v.svfL.set(hz, 0.75f, m_rate);
                    v.svfR.set(hz, 0.75f, m_rate);
                }
            }
            float xl = bL[i], xr = bR[i];
            if (click && v.clickEnv > 1e-4f) {
                // The hammer, or the organ's key click.
                const float c = v.clickBp.step(v.rng.bipolar()).band * v.clickEnv * (v.style == Piano ? 0.16f : 0.1f);
                xl += c;
                xr += c;
                v.clickEnv *= clickDecay;
            }
            if (ladder) {
                xl = v.ladL.step(xl);
                xr = v.ladR.step(xr);
            } else {
                xl = v.svfL.step(xl).low;
                xr = v.svfR.step(xr).low;
            }
            float g = a * gain * (g0 + (g1 - g0) * float(i) * invN);
            if (chop) {
                if (v.graceHold > 0)
                    --v.graceHold;
                else
                    v.graceEnv *= graceDecay;
                g *= std::max(G.g[i], v.graceEnv);
            }
            if (sd.pump > 0)
                g *= 1.0f - sd.pump * (1.0f - m_pump[i]);
            xl *= g * (1.0f + rot);
            xr *= g * (1.0f - rot);
            outL[i] += xl;
            outR[i] += xr;
            peak = std::max(peak, std::fabs(xl) + std::fabs(xr));
        }
        v.strike *= std::pow(strikeDecay, float(n - from));
        v.level = v.level * 0.9f + 0.1f * peak;
        if (v.releasing && v.amp.stage == Env::Idle)
            v.active = false;
    }

    float m_rate = 48000;
    uint64_t m_age = 0;
    std::array<ChordVoice, 10> m_voices;
    Lane m_lanes[kLanes];
    Rhythm m_rhythm[StyleCount];
    int m_override = -1, m_globalRate = 1;
    int m_keyRoot = -1, m_keyFamily = 0, m_keyBright = 0;
    bool m_keyJust = false;
    int m_count = 7;
    float m_pump[kBlock] = {};
    float m_pumpView = 0;
    float m_loopGain = 1;
    Svf m_hp1L, m_hp1R, m_hp2L, m_hp2R;
};

}
