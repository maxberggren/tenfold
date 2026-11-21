#pragma once

// The Chords part's engine: the harmony (which notes a degree and a colour
// make in the current key, and how to voice them so one chord flows into
// the next), the ten sounds, and the tempo-locked gates that give a held
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

// New sounds go on the end: songs, clips and the tutorial store indices.
enum Style : int { Pad, Trance, Organ, Piano, Pluck, Pump, Keys, Strings, Dub, Guitar, StyleCount };

enum Pattern : int { Hold, Offbeat, Eighths, Tresillo, Charleston, TranceGate, Sixteenths, Wub, PatternCount };

// How a chord sounds. Saw styles are unison sawtooths through a ladder;
// the organ is a drawbar wavetable with percussion and a key click; the
// piano is two-operator FM with a hammer; the electric piano is FM with a
// tine's bell on the attack and a pickup's bark; the string machine is
// phase-locked octave saws (a divide-down organ's) through one low-pass; the
// guitar is a plucked string (Karplus-Strong) per note.
struct StyleDef {
    const char *name;
    const char *blurb;
    enum Osc : uint8_t { Saw, OrganOsc, PianoOsc, TineOsc, StringOsc, PluckedOsc } osc;
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
    // An effect the style's voices share, one stereo unit on its own bus,
    // so it costs the same for one chord as for ten: the string machine's
    // ensemble chorus, the dub stab's echo.
    enum Bus : uint8_t { Dry, Ensemble, Echo, Comp } bus = Dry;
};

// The ten sounds. Levels are matched by ear-proxy (RMS of a held
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
        // An electric piano: FM tine and bark that mellows as it rings, a
        // bell on the attack, a slow stereo tremolo; held, low and warm, for
        // neo-soul, lo-fi and deep house.
        {.name = "KEYS", .blurb = "electric piano, tremolo", .osc = StyleDef::TineOsc, .unison = 2, .detune = 3.0f,
         .a = 0.002f, .d = 3.2f, .s = 0.12f, .r = 0.3f, .cutoff = 0.66f, .envAmt = 0.08f, .envDecay = 0.5f,
         .res = 0.05f, .retrig = true, .glide = 0.02f, .centre = 60.0f, .gain = 0.21f, .width = 0.35f,
         .pattern = Hold},
        // A string machine: octave saws locked in phase, swelling slowly
        // through a string section's EQ (thin underneath, a bite around
        // 1.6 kHz) and an ensemble chorus; close voicings up high that
        // glide. Synthwave, the seventies and eighties, ambient.
        {.name = "STRINGS", .blurb = "string machine, ensemble", .osc = StyleDef::StringOsc, .unison = 1,
         .detune = 0.0f, .a = 0.45f, .d = 1.5f, .s = 0.9f, .r = 0.8f, .cutoff = 0.85f, .envAmt = 0.03f,
         .envDecay = 0.8f, .res = 0.0f, .legato = true, .glide = 0.06f, .centre = 67.0f, .gain = 0.155f,
         .width = 1.0f, .pattern = Hold, .bus = StyleDef::Ensemble},
        // A dub-techno stab: two saws through a resonant ladder that snaps
        // shut, open voicings with the root under them, into a dotted-eighth
        // echo whose repeats grow darker and thinner and smear into space.
        {.name = "DUB", .blurb = "chord stab into echo", .osc = StyleDef::Saw, .unison = 2, .detune = 7.0f,
         .a = 0.002f, .d = 0.24f, .s = 0.0f, .r = 0.12f, .cutoff = 0.4f, .envAmt = 0.34f, .envDecay = 0.11f,
         .res = 0.4f, .retrig = true, .spread = true, .glide = 0.02f, .centre = 61.0f, .gain = 0.56f,
         .width = 0.4f, .pattern = Offbeat, .bus = StyleDef::Echo},
        // A clean funk guitar: a plucked string per note through a single
        // coil's pickup, strummed down and up a few milliseconds apart,
        // palm-muted as the gate shortens and dead (a pitchless chk) on
        // about half the ghost strokes between the beats, squashed by a
        // compressor and rounded by a cabinet.
        {.name = "GUITAR", .blurb = "funk guitar, strummed", .osc = StyleDef::PluckedOsc, .unison = 1,
         .detune = 0.0f, .a = 0.0008f, .d = 1.0f, .s = 1.0f, .r = 0.035f, .cutoff = 0.95f, .envAmt = 0.0f,
         .envDecay = 0.2f, .res = 0.05f, .retrig = true, .glide = 0.01f, .centre = 66.0f, .gain = 0.67f,
         .strum = 0.0045f, .width = 0.8f, .pattern = Sixteenths, .bus = StyleDef::Comp},
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
constexpr int Rate = 46, Pump = 47;
constexpr int Columns = 48;  // per lane 8 apart: how loud each degree is sounding
constexpr int Pattern = 64;  // per style: the pattern sounding (override included)
static_assert(Gate + StyleCount <= Rate && Pattern + StyleCount <= kPartData);
}

// Lanes of play: the live one, then one per layer of the playing loop (each
// layer's chords are held and sound on their own, so a chord layered over
// the loop never cuts the loop's off). The page is shown two: the live
// lane, and the loop's chord of the moment as kReplayLane.
constexpr int kLiveLane = 0, kReplayLane = 1, kLanes = 1 + kClipLayers, kPubLanes = 2;
// The id of the page's carry-on markers: never a chord's (those count up
// from 1 and wrap below it).
constexpr int kCarryOnId = 0xFFFFF0;
inline int laneOf(int id) { return isReplay(id) ? kReplayLane + replayLayer(id) : kLiveLane; }

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
    uint8_t hitOn[kBlock] = {}, hitOff[kBlock] = {}; // hitOn: 2 for an accented hit, 1 for the others
};

// A plucked string (Karplus-Strong): a burst of noise one period long
// circulating through a delay line tuned to the note, a one-pole lowpass in
// the loop for the string's damping (a palm on the strings closes it) and
// an allpass for the fraction of a sample the tuning needs. It is heard
// through a pickup: the string a fifth of a period earlier subtracted, the
// comb that gives a single coil its quack.
struct PluckedString {
    static constexpr int kSize = 1024; // a period of up to 1023 samples: down to about 47 Hz
    static constexpr float kDead = 1.0f; // a mute this full is a dead stroke: a pitchless chk
    float line[kSize] = {};
    int w = 0, period = 100, pickup = 20;
    float apC = 0, apX = 0, apY = 0; // the fractional-delay allpass
    float lp = 0, damp = 0.8f, loss = 0.999f;
    float excLp = 0, excLp2 = 0, excAmp = 0, excBright = 0.5f;
    int excLeft = 0, excLength = 1;
    float mute = 0;      // the palm mute of the last pluck, 0..1
    float energy = 0;    // a running level, so a string that died out costs nothing

    void clear() {
        std::fill(std::begin(line), std::end(line), 0.0f);
        apX = apY = lp = excLp = excLp2 = 0;
        excLeft = 0;
        energy = 0;
    }
    // Tunes the loop to hz for this block: the delay, the damping and how
    // fast the string dies, all from the pitch and the palm.
    void tune(float hz, float rate) {
        hz = clampf(hz, 50.0f, rate * 0.2f);
        // Open, the loop keeps nearly everything and rings for seconds; under
        // the palm it loses its highs at once and dies in a tenth of one.
        const float open = 1.0f - mute;
        const bool dead = mute >= kDead;
        // A dead stroke: the fretting hand only rests on the strings, which
        // give back a thud and no note.
        damp = dead ? 0.15f : clampf(0.28f + 0.66f * open - 0.12f * (hz / 2000.0f), 0.12f, 0.96f);
        const float t60 = dead ? 0.012f : 0.09f + 2.6f * open * open * open;
        loss = std::pow(10.0f, -3.0f / (t60 * hz));
        // The lowpass delays the loop by its phase delay at the note; the
        // integer line and the allpass make up the rest of the period.
        const float wv = kTwoPi * hz / rate, b = 1.0f - damp;
        const float lpDelay = std::atan2(b * std::sin(wv), 1.0f - b * std::cos(wv)) / wv;
        const float total = rate / hz - lpDelay;
        period = std::clamp(int(total - 0.1f), 2, kSize - 1);
        const float frac = total - float(period);
        apC = (1.0f - frac) / (1.0f + frac);
        pickup = std::max(1, int(0.2f * float(period) + 0.5f));
    }
    void pluck(float amp, float bright, float palm) {
        mute = palm;
        // A darker pick is quieter noise; made up (at most twice), so the
        // palm changes the tone more than the level.
        excAmp = amp * std::min(2.0f, 1.0f / std::sqrt(bright / (2.0f - bright)));
        excBright = bright;
        // A dead stroke is a short dark burst; a note, a period of pick.
        excLength = palm >= kDead ? 4 * period : period;
        excLeft = excLength;
    }
    inline float step(Rng &rng) {
        float in = 0;
        if (excLeft > 0) {
            // The pick: noise through two one-poles (a band-limited scrape,
            // never a white hiss), darkened by the palm and a soft touch,
            // faded over the burst so it has no edge.
            excLp += (rng.bipolar() - excLp) * excBright;
            excLp2 += (excLp - excLp2) * excBright;
            in = excLp2 * excAmp * (0.4f + 0.6f * float(excLeft) / float(excLength));
            --excLeft;
        }
        const float x = line[(w - period) & (kSize - 1)];
        const float y = apC * (x - apY) + apX;
        apX = x;
        apY = y;
        lp += (y - lp) * damp;
        const float out = lp * loss + in;
        const float before = line[(w - pickup) & (kSize - 1)];
        line[w] = out;
        w = (w + 1) & (kSize - 1);
        return 0.75f * (out - before);
    }
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
        float phase2[2] = {}; // the piano's modulators, the organ's percussion, the tine's bell
        int pluckIn = -1;     // the guitar: samples until this string is plucked (−1: not waiting)
        float pluckAmp = 1, pluckMute = 0;
        PluckedString string;
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
    bool strikeRise = false; // a ringing chord struck again: the strike climbs back in 2 ms, never jumps
    float clickEnv = 0;
    Ladder ladL, ladR;
    Svf svfL, svfR, clickBp;
    float ratio[kUnison] = {}, panL[kUnison] = {}, panR[kUnison] = {};
    int unison = 1;
    float graceEnv = 0;  // keeps a chopping gate open for a fresh chord, so a tap always speaks
    int graceHold = 0;
    int minHold = 0;     // a struck chord rings at least this long before a gate may end it
    bool lateRelease = false; // let go before it spoke: release once it has rung its shortest
    float leslie = 0;    // the organ's slow rotor, the electric piano's tremolo
    bool upStroke = false;  // the guitar: the next strum goes up the strings
    int sinceStrum = 1 << 20; // the guitar: samples since the last strum began
    Rng rng{0x1234567u};

    void init(uint32_t seed) { rng = Rng(seed); }
};

// --- the shared effects -----------------------------------------------------

// The string machine's ensemble: three taps on one short delay, each swept
// by a slow and a fast sine a third of a turn apart from the others (the
// bucket-brigade chorus of the seventies' string machines), one side of the
// stereo from the first and second, the other from the third and second.
// The whole bus goes through it, so it costs the same for any number of
// chords. Ahead of it, a string section's fixed EQ: nothing under 250 Hz,
// a broad lift around 1.6 kHz where bowed strings bite (the low-pass at
// about 7 kHz is each chord's own filter, which the knob still turns).
struct Ensemble {
    Delay line;
    Svf hp, bell;
    float rate = 48000, slow = 0, fast = 0;
    int idle = 1 << 30;
    static constexpr float kBellQ = 0.7f, kBellGain = 0.585f; // +4 dB

    void init(float r) {
        rate = r;
        line.init(size_t(0.03f * r));
        hp.set(250.0f, 0.707f, r);
        bell.set(1600.0f, kBellQ, r);
    }
    // Adds the chorused bus to out.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        float in = 0;
        for (int i = 0; i < n; ++i)
            in += std::fabs(inL[i]) + std::fabs(inR[i]);
        idle = in > 1e-7f ? 0 : idle + n;
        if (idle > int(0.05f * rate)) // the line's 30 ms have passed through
            return;
        const Tables &tb = tables();
        const float base = 0.0068f * rate, dSlow = 0.0015f * rate, dFast = 0.00012f * rate;
        const float incS = 0.63f / rate, incF = 5.7f / rate;
        for (int i = 0; i < n; ++i) {
            const float x = hp.step(0.5f * (inL[i] + inR[i])).high;
            const float m = x + kBellGain / kBellQ * bell.step(x).band;
            line.push(m);
            slow = wrap01(slow + incS);
            fast = wrap01(fast + incF);
            float t[3];
            for (int k = 0; k < 3; ++k) {
                const float off = float(k) * (1.0f / 3.0f);
                const float d = base + dSlow * lut(tb.sine, wrap01(slow + off)) + dFast * lut(tb.sine, wrap01(fast + off));
                t[k] = line.read(d);
            }
            outL[i] += 0.15f * m + 0.62f * t[0] + 0.45f * t[1];
            outR[i] += 0.15f * m + 0.62f * t[2] + 0.45f * t[1];
        }
    }
};

// The dub stab's echo: a ping-pong on the dotted eighth of the tempo, each
// pass through a high-pass, a low-pass, a soft clip and an allpass, so the
// repeats grow thinner, darker and more diffuse as they trail off (the
// first is nearly the stab, the tenth a wash), and one more allpass a side
// on the way out. Pressing harder throws it: the feedback climbs. The dry
// bus passes straight through.
struct DubEcho {
    Delay l, r;
    Smooth time, feedback;
    Svf hpL, hpR, lpL, lpR;
    Allpass apL1, apL2, apR1, apR2;
    float rate = 48000, wow = 0, throwTo = 0.66f;
    int idle = 1 << 30;
    static constexpr float kMaxSeconds = 1.5f, kFeedback = 0.66f, kThrown = 0.82f, kWet = 0.6f;

    void init(float rt) {
        rate = rt;
        l.init(size_t(kMaxSeconds * rt) + 64);
        r.init(size_t(kMaxSeconds * rt) + 64);
        time.setTime(0.3f, rt);
        time.reset(0.36f * rt);
        feedback.setTime(0.15f, rt);
        feedback.reset(kFeedback);
        for (Svf *f : {&hpL, &hpR})
            f->set(330.0f, 0.6f, rt);
        for (Svf *f : {&lpL, &lpR})
            f->set(2100.0f, 0.65f, rt);
        apL1.init(uint32_t(0.0071f * rt), 0.55f);
        apL2.init(uint32_t(0.0113f * rt), 0.5f);
        apR1.init(uint32_t(0.0083f * rt), 0.55f);
        apR2.init(uint32_t(0.0127f * rt), 0.5f);
    }
    // Adds the dry bus and its echoes to out.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, double secondsPerBeat) {
        float in = 0;
        for (int i = 0; i < n; ++i)
            in += std::fabs(inL[i]) + std::fabs(inR[i]);
        idle = in > 1e-7f ? 0 : idle + n;
        // Silent for twenty seconds: every repeat has died away.
        if (idle > int(20.0f * rate))
            return;
        const float target = clampf(float(0.75 * secondsPerBeat), 0.05f, kMaxSeconds - 0.01f) * rate;
        for (int i = 0; i < n; ++i) {
            const float t = time.step(target);
            wow = wrap01(wow + 0.4f / rate);
            const float mod = 0.0009f * rate * lut(tables().sine, wow);
            const float dl = l.read(t + mod), dr = r.read(t + 0.8f * mod);
            const float fb = feedback.step(throwTo);
            const float fl = apL1.step(fastTanh(1.15f * lpL.step(hpL.step(dr).high).low)) * fb;
            const float fr = apR1.step(fastTanh(1.15f * lpR.step(hpR.step(dl).high).low)) * fb;
            // The stab goes left first and every repeat crosses over, so
            // the left side always holds the louder repeat of each pair:
            // taking the left back by √fb and the right up by as much (a
            // little more, for what the loop's filters take on each pass)
            // makes each pair even, and the echo leans to neither side.
            l.push(0.5f * (inL[i] + inR[i]) + fl);
            r.push(fr);
            const float side = 0.85f * std::sqrt(fb);
            outL[i] += inL[i] + kWet * side * apL2.step(dl);
            outR[i] += inR[i] + kWet / side * apR2.step(dr);
        }
    }
};

// The guitar's bus: a compressor, as funk players keep on the floor (fast
// to catch the pick, a little slower to let go, four to one, the level
// made up), the round-off of a clean amp pushed a little (the pick's
// sharpest edges, which no compressor is fast enough for), then a speaker
// cabinet: two two-pole low-passes at 4.5 kHz, where a guitar's sound
// ends. The compressor hears the strings a
// millisecond and a half before they pass, so even a pick's first edge is
// caught.
struct GuitarAmp {
    static constexpr int kLookSize = 128;
    Svf cabL1, cabL2, cabR1, cabR2;
    float lookL[kLookSize] = {}, lookR[kLookSize] = {};
    int look = 72, pos = 0;
    float rate = 48000, env = 0, atk = 0.02f, rel = 0.0003f;
    int idle = 1 << 30;
    static constexpr float kThreshold = 0.1f, kSlope = 1.0f - 1.0f / 4.0f, kMakeup = 2.6f, kClip = 0.3f;

    void init(float r) {
        rate = r;
        atk = 1.0f - std::exp(-1.0f / (0.001f * r));
        rel = 1.0f - std::exp(-1.0f / (0.06f * r));
        look = std::min(kLookSize - 1, int(0.0015f * r));
        for (Svf *f : {&cabL1, &cabR1})
            f->set(4500.0f, 0.54f, r);
        for (Svf *f : {&cabL2, &cabR2})
            f->set(4500.0f, 1.31f, r);
    }
    // Adds the bus, squashed and through the cabinet, to out.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        float in = 0;
        for (int i = 0; i < n; ++i)
            in += std::fabs(inL[i]) + std::fabs(inR[i]);
        idle = in > 1e-7f ? 0 : idle + n;
        if (idle > int(0.05f * rate))
            return;
        for (int i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(inL[i]), std::fabs(inR[i]));
            env += (x - env) * (x > env ? atk : rel);
            const float g = (env > kThreshold ? std::pow(kThreshold / env, kSlope) : 1.0f) * kMakeup;
            lookL[pos] = inL[i];
            lookR[pos] = inR[i];
            const int at = (pos - look + kLookSize) % kLookSize;
            pos = (pos + 1) % kLookSize;
            const float yl = kClip * fastTanh(lookL[at] * g * (1.0f / kClip));
            const float yr = kClip * fastTanh(lookR[at] * g * (1.0f / kClip));
            outL[i] += cabL2.step(cabL1.step(yl).low).low;
            outR[i] += cabR2.step(cabR1.step(yr).low).low;
        }
    }
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
        m_ensemble.init(rate);
        m_echo.init(rate);
        m_amp.init(rate);
    }

    void command(const Command &c, AudioContext &ctx) override {
        // The page's beat markers for a held chord (see kCarryOn in
        // chords.cpp) tell Space the playing goes on; they carry no sound.
        if ((c.id & 0xFFFFFF) == kCarryOnId)
            return;
        const int lane = laneOf(c.id);
        Lane &L = m_lanes[lane];
        checkKey(ctx);
        switch (c.type) {
        case Command::On: {
            Entry e;
            e.id = c.id;
            // The page plays degrees 0 to 7; a song's clip comes from a file
            // that may say anything, and a degree far out of the keyboard
            // would overflow the harmony's sums and spin the doublings'
            // octave search, so it is kept to a few octaves of the key.
            e.degree = std::clamp(c.i[0], 0, 31);
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
            // A new chord is the lane's from now, even if a struck sound waits
            // for its gate's next hit to speak (the same chord struck again
            // is not a new one).
            if (column(e.degree) != column(L.shape.tones[0]) || L.shape.count == 0)
                m_onsetAt[lane] = ctx.transport().beatAt(offset);
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
                // The chord of the moment falls back to the newest still
                // held: the loop's under a live one, a layer's first layer
                // under the chord layered on it.
                if (!current(m_lanes[kLiveLane]) && loopLane() >= 0)
                    announce(loopLane(), ctx);
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
        // (−18 dB in about 30 ms) until the finger lifts. The same chord over
        // the loop leaves it whole. When the live chord is the newcomer (the
        // player reharmonising the loop) the loop steps back after 30 ms,
        // long enough for a finger landing a hair before the loop reaches
        // the same chord. When the loop moved on under a chord already held,
        // it waits half a beat (an eighth note): a player layering the loop's
        // chords a little late is still on the loop's last chord for a moment.
        bool loopSounding = false;
        for (const auto &v : m_voices)
            loopSounding |= v.active && v.lane >= kReplayLane;
        const ChordVoice *live = current(m_lanes[kLiveLane]);
        const int ll = loopLane();
        const Shape &loopShape = m_lanes[ll >= 0 ? ll : kReplayLane].shape;
        const bool differ = live && loopSounding && loopShape.count > 0 &&
                            column(live->degree) != column(loopShape.tones[0]);
        m_differFor = differ ? m_differFor + double(n) * tr.beatsPerSample : 0.0;
        // Landing with the loop's chord, to a tenth of a beat, counts as new.
        const bool newcomer = m_onsetAt[kLiveLane] >= m_onsetAt[ll >= 0 ? ll : kReplayLane] - 0.1;
        const double wait = newcomer ? 0.03 / tr.secondsPerBeat() : 0.5;
        const bool clash = m_differFor > wait;
        const float k = 1.0f - std::exp(-float(n) / (0.01f * m_rate));
        const float loop0 = m_loopGain;
        m_loopGain += ((clash ? 0.125f : 1.0f) - m_loopGain) * k;
        // Layers of the loop holding a different chord from its newest step
        // back the same way: one harmony leads, the other stays under it.
        float layer0[kLanes];
        for (int i = kReplayLane; i < kLanes; ++i) {
            const Lane &L = m_lanes[i];
            const bool under = ll >= 0 && i != ll && current(L) && L.shape.count > 0 &&
                               column(L.shape.tones[0]) != column(m_lanes[ll].shape.tones[0]);
            layer0[i] = m_layerGain[i];
            m_layerGain[i] += ((under ? 0.125f : 1.0f) - m_layerGain[i]) * k;
        }
        // Voices of a style with an effect of its own render to that
        // effect's bus; the effect then adds its bus to the mix.
        float mixL[kBlock] = {}, mixR[kBlock] = {};
        float busL[StyleDef::Comp + 1][kBlock] = {}, busR[StyleDef::Comp + 1][kBlock] = {};
        float dubPressure = -1;
        for (auto &v : m_voices)
            if (v.active) {
                const bool loop = v.lane >= kReplayLane;
                const int lane = std::clamp(int(v.lane), 0, kLanes - 1);
                const StyleDef::Bus bus = styleDef(v.style).bus;
                float *oL = bus == StyleDef::Dry ? mixL : busL[bus];
                float *oR = bus == StyleDef::Dry ? mixR : busR[bus];
                renderVoice(v, oL, oR, n, loop ? loop0 * layer0[lane] : 1.0f,
                            loop ? m_loopGain * m_layerGain[lane] : 1.0f);
                if (bus == StyleDef::Echo && !v.releasing)
                    dubPressure = std::max(dubPressure, v.pressureSm);
            }
        // The hardest-pressed dub chord sets how far its echo is thrown; with
        // none held, the echo keeps the last throw as it trails off.
        if (dubPressure >= 0)
            m_echo.throwTo = DubEcho::kFeedback + (DubEcho::kThrown - DubEcho::kFeedback) *
                                                      clampf((dubPressure - 0.55f) / 0.4f, 0.0f, 1.0f);
        m_ensemble.process(busL[StyleDef::Ensemble], busR[StyleDef::Ensemble], mixL, mixR, n);
        m_echo.process(busL[StyleDef::Echo], busR[StyleDef::Echo], mixL, mixR, n, tr.secondsPerBeat());
        m_amp.process(busL[StyleDef::Comp], busR[StyleDef::Comp], mixL, mixR, n);
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
        // The loop is shown as its chord of the moment: the newest layer
        // holding one, else the newest ringing out.
        int shownLoop = loopLane();
        if (shownLoop < 0)
            for (int i = kReplayLane; i < kLanes; ++i) {
                const Lane &L = m_lanes[i];
                if (L.shown >= 0 && m_voices[size_t(L.shown)].active && m_voices[size_t(L.shown)].lane == i &&
                    (shownLoop < 0 || m_onsetAt[i] > m_onsetAt[shownLoop]))
                    shownLoop = i;
            }
        if (shownLoop < 0)
            shownLoop = kReplayLane;
        for (int pk = 0; pk < kPubLanes; ++pk) {
            const int k = pk == 0 ? kLiveLane : shownLoop;
            float *d = data + pk * pub::LaneStride;
            const Lane &L = m_lanes[k];
            const ChordVoice *v = L.shown >= 0 ? &m_voices[size_t(L.shown)] : nullptr;
            if (v && (!v->active || v->lane != k))
                v = nullptr;
            // A live chord let go is no longer the chord of the moment: the
            // page falls back to the loop's as soon as the finger lifts,
            // while its tail rings out.
            if (v && k == kLiveLane && v->releasing)
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
                if (w.active && (pk == 0 ? w.lane == kLiveLane : w.lane >= kReplayLane)) {
                    const int c = ((w.degree % m_count) + m_count) % m_count;
                    if (c < 8)
                        cols[c] = std::max(cols[c], std::min(1.0f, w.level * 3.0f));
                }
            for (int c = 0; c < 8; ++c)
                data[pub::Columns + 8 * pk + c] = cols[c];
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
    const ChordVoice *current(const Lane &L) const {
        if (L.voice < 0)
            return nullptr;
        const ChordVoice &v = m_voices[size_t(L.voice)];
        return v.active && !v.releasing ? &v : nullptr;
    }

    // How long a chord takes to fall away when a new one takes its place.
    static constexpr float kDamp = 0.02f;

    // Lets a voice go: its own release, or a faster one (seconds) when given.
    void release(ChordVoice &v, float fast = 0) {
        if (fast > 0)
            v.amp.cr = std::max(v.amp.cr, 1.0f - std::exp(-1.0f / (fast * m_rate)));
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

    // The loop's lane whose chord is the loop's now: the newest held, or -1.
    int loopLane() const {
        int best = -1;
        for (int i = kReplayLane; i < kLanes; ++i)
            if (current(m_lanes[i]) && (best < 0 || m_onsetAt[i] > m_onsetAt[best]))
                best = i;
        return best;
    }

    // A degree as its column on the pad: the same chord in any octave.
    int column(int degree) const { return ((degree % m_count) + m_count) % m_count; }

    int effectivePattern(int style) const { return m_override >= 0 ? m_override : m_rhythm[style].pattern; }

    // A struck sound under a gate speaks on the gate's hits: a chord landing
    // between them waits (in samples, returned) for the next one, so an
    // organ on the offbeat never stabs the downbeat because a finger moved
    // there. A chord up to about 100 ms late for a hit strikes at once:
    // that is a player a little behind, not one aiming between the hits.
    int waitForHit(int style, const Transport &tr, int offset) const {
        const int pattern = effectivePattern(style);
        if (!styleDef(style).retrig || pattern == Hold)
            return 0;
        const GateDef &g = gateDef(pattern);
        const double stepsPerBeat = double(g.steps * m_globalRate) / kBeatsPerBar;
        const double pos = tr.beatAt(offset) * stepsPerBeat;
        const long k = long(std::floor(pos));
        long last = k;
        while (g.hits[((last % g.steps) + g.steps) % g.steps] == '.' && last > k - g.steps)
            --last;
        const double lateSeconds = (pos - double(last)) / stepsPerBeat * tr.secondsPerBeat();
        if (g.hits[((last % g.steps) + g.steps) % g.steps] != '.' && lateSeconds < 0.105)
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
                    cur->strikeRise = true;
                }
                cur->filt.on();
                cur->graceEnv = 1;
                cur->graceHold = int(0.06f * m_rate);
            }
        } else {
            // The chord it replaces is damped, not left to ring under the new
            // one: two harmonies would rub for the length of a release.
            if (cur)
                release(*cur, kDamp);
            ChordVoice &v = allocate();
            start(v, e, lane, vc, rootMidi, offset);
            L.voice = int(&v - m_voices.data());
        }
        L.shown = L.voice;
        L.last = vc;
        L.hasLast = true;
        L.lastReg = e.reg;
        // When the lane's chord last became a different one, for the loop's
        // step back (an On has set it already).
        if (!fresh && column(shape.tones[0]) != column(L.shape.tones[0]))
            m_onsetAt[lane] = ctx.transport().beatAt(offset);
        L.shape = shape;
        if (tell)
            announce(lane, ctx);
    }

    // The chord of the moment for Bass, Arp and Lead. A chord held live wins
    // over a loop's while the finger is down.
    void announce(int lane, AudioContext &ctx) {
        if (lane >= kReplayLane && (current(m_lanes[kLiveLane]) || lane != loopLane()))
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
        v.strikeRise = false;
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
        v.clickBp.set(e.style == Piano ? 2600.0f : e.style == Keys ? 1300.0f : 3400.0f, 1.2f, m_rate);
        v.upStroke = false;
        v.sinceStrum = -1; // the guitar strums its first chord as it starts
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
        // The guitar's strings all start at once and its strum plucks them
        // in turn (see strum), the same way on every hit of its gate.
        const float noteStrum = sd.osc == StyleDef::PluckedOsc ? 0.0f : sd.strum;
        for (int k = 0; k < ChordVoice::kNotes; ++k) {
            auto &nt = v.notes[k];
            nt.delay = 0;
            nt.onset = 1;
            nt.exists = true;
            nt.ampPrev = nt.amp = nt.ampTarget = 0;
            for (float &p : nt.phase)
                p = v.rng.unit();
            nt.phase2[0] = nt.phase2[1] = 0;
            nt.pluckIn = -1;
            if (sd.osc == StyleDef::PluckedOsc)
                nt.string.clear();
            if (k < vc.count) {
                nt.role = ChordVoice::Core;
                nt.midi = nt.target = vc.notes[k];
                nt.amp = nt.ampPrev = nt.ampTarget = 1;
                nt.delay = int(noteStrum * float(k) * m_rate);
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
                nt.delay = int(noteStrum * float(vc.count) * m_rate);
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
                nt.pluckIn = 0; // a string fading in is plucked (the other sounds ignore it)
                nt.pluckAmp = 0.7f * v.vel;
                nt.pluckMute = 0;
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
            G.hitOn[i] = on && hitAbs != R.lastHit ? (g.hits[h] == 'X' ? 2 : 1) : 0;
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

    // The guitar's right hand: on each hit of its gate (and as a chord
    // starts) a strum across the sounding strings, a few milliseconds a
    // string, down from the low strings on the accents and alternating
    // down and up between them. The palm mutes it more the shorter the
    // gate, and more again on the unaccented strokes, the ghost scratches
    // of funk; a Hold gate lets the chord ring open.
    void strumOnHits(ChordVoice &v, const GateBlock &G, int from, int n) {
        const StyleDef &sd = styleDef(v.style);
        const bool gated = G.active && !v.releasing;
        const float len = m_rhythm[v.style].length;
        const float palm = gated ? clampf((0.7f - len) / 0.6f, 0.0f, 0.75f) : 0.0f;
        int since = v.sinceStrum;
        for (int i = from; i < n; ++i) {
            const bool first = since < 0;
            ++since;
            if (!first && !(gated && G.hitOn[i] && since > int(0.03f * m_rate)))
                continue;
            const bool accent = first || G.hitOn[i] == 2;
            v.upStroke = accent ? false : !v.upStroke;
            since = 0;
            // The strings sounding, lowest first.
            int order[ChordVoice::kNotes], count = 0;
            for (int k = 0; k < ChordVoice::kNotes; ++k)
                if (v.notes[k].ampTarget > 0.01f && v.notes[k].exists)
                    order[count++] = k;
            for (int a = 1; a < count; ++a)
                for (int b = a; b > 0 && v.notes[order[b]].target < v.notes[order[b - 1]].target; --b)
                    std::swap(order[b], order[b - 1]);
            // A hand, not a sequencer: each stroke lands up to 5 ms behind the
            // hit (never ahead of it, so a loop's first stroke is never
            // early) and sweeps the strings a little faster or slower.
            const float per = (v.upStroke ? 0.75f : 1.0f) * sd.strum * m_rate * (0.8f + 0.4f * v.rng.unit());
            const int late = int(0.005f * m_rate * v.rng.unit());
            // A ghost stroke muted past about half is a dead one: the chk
            // between the chords. A little scatter, as a hand has, so some
            // ghosts still sound a note, and pressing hard (past three
            // quarters, so a light or medium touch keeps the chord-chk
            // alternation) lets more of them ring.
            float mute = clampf(palm + (accent ? -0.15f : 0.2f + 0.3f * (v.rng.unit() - 0.5f)), 0.0f, 0.92f);
            const float ring = 0.08f + 0.6f * clampf((v.pressureSm - 0.75f) / 0.25f, 0.0f, 1.0f);
            if (!accent && mute > 0.45f && v.rng.unit() >= ring)
                mute = PluckedString::kDead;
            for (int r = 0; r < count; ++r) {
                auto &nt = v.notes[order[r]];
                const int rank = v.upStroke ? count - 1 - r : r;
                nt.pluckIn = i - from + late + int(per * float(rank));
                // An upstroke catches the top strings hardest.
                const float weight = v.upStroke ? 0.6f + 0.4f * float(r) / float(std::max(1, count - 1)) : 1.0f;
                nt.pluckAmp = (accent ? 1.0f : 0.72f) * weight * (0.92f + 0.16f * v.rng.unit());
                nt.pluckMute = mute;
            }
        }
        v.sinceStrum = since;
    }

    // The fretting hand lifts: the strings stop ringing.
    static void choke(ChordVoice &v) {
        for (auto &nt : v.notes)
            nt.string.mute = std::max(nt.string.mute, 0.95f);
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

        // The strike, sample by sample: falling away as the chord rings,
        // climbing back over 2 ms when it is struck again (a jump would
        // click through the FM index and the percussion).
        float strikeAt[kBlock] = {};
        const bool struck = sd.osc == StyleDef::OrganOsc || sd.osc == StyleDef::PianoOsc || sd.osc == StyleDef::TineOsc;
        if (struck) {
            const float fall =
                std::exp(-1.0f / ((v.style == Piano ? 0.32f : sd.osc == StyleDef::TineOsc ? 0.5f : 0.11f) * m_rate));
            const float rise = 1.0f / (0.002f * m_rate);
            float st = v.strike;
            const bool strikes = G.active && sd.retrig && !v.releasing;
            for (int i = from; i < n; ++i) {
                // A gate hit strikes in its own block, from its own sample.
                if (strikes && G.hitOn[i])
                    v.strikeRise = true;
                if (v.strikeRise) {
                    st += rise;
                    if (st >= 1.0f) {
                        st = 1.0f;
                        v.strikeRise = false;
                    }
                } else {
                    st *= fall;
                }
                strikeAt[i] = st;
            }
            v.strike = st;
        }
        if (sd.osc == StyleDef::PluckedOsc)
            strumOnHits(v, G, from, n);

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
                const float side = kSide * sd.width;
                for (int i = start; i < n; ++i) {
                    pl = wrap01(pl + incL);
                    pr = wrap01(pr + incR);
                    pp = wrap01(pp + incP);
                    const float mid = (lut(tb.organ, pl) + lut(tb.sine, pp) * 0.38f * strikeAt[i]) * kMid;
                    const float sd2 = lut(tb.organ, pr) * side;
                    nl[i] += mid + sd2;
                    nr[i] += mid - sd2;
                }
                nt.phase[0] = pl;
                nt.phase[1] = pr;
                nt.phase2[0] = pp;
            } else if (sd.osc == StyleDef::TineOsc) {
                // The electric piano: FM at 1:1 for the tine's bark, which
                // softens as the note rings, and a modulator fourteen times
                // the pitch whose bell rings only on the attack (it falls as
                // the strike to the sixth power). A carrier a few cents off
                // is the stereo side, as on the piano.
                const float incL = hz * v.ratio[0] * invRate, incR = hz * v.ratio[1] * invRate;
                const float incB = std::min(14.0f * hz * invRate, 0.45f);
                float pl = nt.phase[0], pr = nt.phase[1], pb = nt.phase2[0];
                const float bright = clampf(1.25f - (nt.midi - 60.0f) / 36.0f, 0.35f, 1.3f);
                const float idx0 = (0.25f + 1.4f * v.vel * v.vel) * bright / kTwoPi;
                // The bell thins out up the keyboard, where 14x would alias.
                const float bell0 =
                    (0.35f + 0.8f * v.vel) * bright * clampf((96.0f - nt.midi) / 24.0f, 0.0f, 1.0f) / kTwoPi;
                const float side = kSide * sd.width;
                // The pickup, driven: a hard hit pushes it into an odd-harmonic
                // edge (tanh, normalised so the level holds) and an
                // even-harmonic growl (x + k·x²) that stays through the
                // sustain; a soft touch reaches neither and stays round, a
                // medium one already growls.
                const float push = clampf((v.vel - 0.55f) / 0.4f, 0.0f, 1.0f), hit = push * push;
                const float bark0 = 1.8f * hit, drive = 1.0f + hit;
                const float driveNorm = 1.0f / fastTanh(drive);
                // The mean of the driven x², taken back off so the growl adds
                // no DC (fitted over drives 1..2).
                const float meanSq = 0.5f + 0.31f * (drive - 1.0f) - 0.12f * (drive - 1.0f) * (drive - 1.0f);
                for (int i = start; i < n; ++i) {
                    pl = wrap01(pl + incL);
                    pr = wrap01(pr + incR);
                    pb = wrap01(pb + incB);
                    const float st = strikeAt[i], s2 = st * st;
                    const float idx = idx0 * (0.3f + 0.7f * st);
                    const float bell = lut(tb.sine, pb) * bell0 * s2 * s2 * s2;
                    const float ml = lut(tb.sine, pl) * idx + bell, mr = lut(tb.sine, pr) * idx + bell;
                    const float k = bark0 * (0.6f + 0.4f * st);
                    const float cm = fastTanh(drive * lut(tb.sine, wrap01(pl + ml))) * driveNorm;
                    const float cs = fastTanh(drive * lut(tb.sine, wrap01(pr + mr))) * driveNorm;
                    const float mid = (cm + k * (cm * cm - meanSq)) * kMid, sd2 = (cs + k * (cs * cs - meanSq)) * side;
                    nl[i] += mid + sd2;
                    nr[i] += mid - sd2;
                }
                nt.phase[0] = pl;
                nt.phase[1] = pr;
                nt.phase2[0] = pb;
            } else if (sd.osc == StyleDef::StringOsc) {
                // The string machine: a saw and the saw an octave up, divided
                // from one phase as the old machines' octave dividers did, so
                // the two never beat; the ensemble on the bus gives the
                // movement and the width.
                const float inc = std::min(hz * invRate, 0.22f), inc2 = 2.0f * inc;
                float ph = nt.phase[0];
                for (int i = start; i < n; ++i) {
                    ph += inc;
                    if (ph >= 1.0f)
                        ph -= 1.0f;
                    const float p2 = ph < 0.5f ? 2.0f * ph : 2.0f * ph - 1.0f;
                    const float x = 0.6f * (2.0f * ph - 1.0f - polyBlep(ph, inc)) +
                                    0.6f * (2.0f * p2 - 1.0f - polyBlep(p2, inc2));
                    nl[i] += x;
                    nr[i] += x;
                }
                nt.phase[0] = ph;
            } else if (sd.osc == StyleDef::PluckedOsc) {
                // The guitar: a string per note, plucked when its strum
                // reaches it, low strings a little left and high ones right.
                auto &str = nt.string;
                const int k = int(&nt - v.notes);
                // (Low strings carry more energy, so the spread sits a
                // little right of centre to balance.)
                const float pan = (nt.role == ChordVoice::Low   ? -0.45f
                                   : nt.role == ChordVoice::Top ? 0.75f
                                                                : -0.35f + 0.33f * float(k)) *
                                  sd.width;
                const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
                const bool waiting = nt.pluckIn >= 0 && nt.pluckIn < n - from;
                // A string that has died away costs nothing until it is
                // plucked again.
                if (str.energy > 1e-6f || str.excLeft > 0 || waiting) {
                    str.tune(hz, m_rate);
                    float e = 0;
                    for (int i = start; i < n; ++i) {
                        if (i - from == nt.pluckIn)
                            str.pluck(nt.pluckAmp,
                                      clampf(0.12f + 0.45f * v.vel * (1.0f - 0.7f * nt.pluckMute), 0.06f, 0.8f),
                                      nt.pluckMute);
                        const float x = str.step(v.rng);
                        e = std::max(e, std::fabs(x));
                        nl[i] += x * gl;
                        nr[i] += x * gr;
                    }
                    str.energy = e;
                }
                if (nt.pluckIn >= 0)
                    nt.pluckIn = nt.pluckIn >= n - from ? nt.pluckIn - (n - from) : -1;
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
                const float idx0 = (0.7f + 2.3f * v.vel) * bright / kTwoPi;
                const float side = kSide * sd.width;
                for (int i = start; i < n; ++i) {
                    pl = wrap01(pl + incL);
                    pr = wrap01(pr + incR);
                    pm = wrap01(pm + incM);
                    const float idx = idx0 * (0.18f + strikeAt[i]);
                    const float mod = lut(tb.sine, pm) * 0.35f * strikeAt[i] / kTwoPi;
                    const float ml = lut(tb.sine, pl) * idx + mod, mr = lut(tb.sine, pr) * idx + mod;
                    const float mid = lut(tb.sine, wrap01(pl + ml)) * kMid, sd2 = lut(tb.sine, wrap01(pr + mr)) * side;
                    nl[i] += mid + sd2;
                    nr[i] += mid - sd2;
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
        const bool mono = sd.osc == StyleDef::StringOsc; // one filter does for both sides
        const bool click = struck;
        const float clickLevel = v.style == Piano ? 0.16f : v.style == Keys ? 0.035f : 0.1f;
        const bool guitar = sd.osc == StyleDef::PluckedOsc;
        const float graceDecay = std::exp(-1.0f / (0.008f * m_rate));
        const float clickDecay = std::exp(-1.0f / (0.0035f * m_rate));
        const float gain = sd.gain * (0.5f + 0.5f * v.vel) * (0.82f + 0.36f * v.pressureSm);
        // The organ's rotor, slow; the electric piano's tremolo, a stereo
        // pan at a few hertz (it keeps the mono sum whole). Each moves
        // smoothly across the block.
        const float rotDepth = v.style == Organ ? 0.13f : v.style == Keys ? 0.24f : 0.0f;
        const float rot0 = rotDepth * lut(tb.sine, v.leslie);
        v.leslie = wrap01(v.leslie + float(n) * (v.style == Keys ? 4.4f : 0.85f) * invRate);
        const float rot1 = rotDepth * lut(tb.sine, v.leslie);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            if (v.minHold > 0 && --v.minHold == 0 &&
                (v.lateRelease || (strikeGate && !v.releasing && G.g[i] < 0.3f))) {
                v.amp.off();
                v.filt.off();
                if (guitar)
                    choke(v);
            }
            if (strikeGate && !v.releasing) {
                if (G.hitOn[i]) {
                    v.amp.on();
                    v.filt.on();
                    v.clickEnv = 1; // (the strike already began climbing at this sample)
                }
                if (v.minHold == 0 && G.hitOff[i]) {
                    v.amp.off();
                    v.filt.off();
                    if (guitar)
                        choke(v);
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
                    if (!mono)
                        v.svfR.set(hz, 0.75f, m_rate);
                }
            }
            float xl = bL[i], xr = bR[i];
            if (click && v.clickEnv > 1e-4f) {
                // The hammer, or the organ's key click.
                const float c = v.clickBp.step(v.rng.bipolar()).band * v.clickEnv * clickLevel;
                xl += c;
                xr += c;
                v.clickEnv *= clickDecay;
            }
            if (ladder) {
                xl = v.ladL.step(xl);
                xr = v.ladR.step(xr);
            } else if (mono) {
                xl = xr = v.svfL.step(xl).low;
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
            const float rot = rot0 + (rot1 - rot0) * float(i) * invN;
            xl *= g * (1.0f + rot);
            xr *= g * (1.0f - rot);
            outL[i] += xl;
            outR[i] += xr;
            peak = std::max(peak, std::fabs(xl) + std::fabs(xr));
        }
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
    double m_differFor = 0; // beats the live chord has differed from the loop's
    double m_onsetAt[kLanes] = {-1e9, -1e9, -1e9, -1e9, -1e9}; // the beat each lane's chord began
    static_assert(kLanes == 5);
    float m_layerGain[kLanes] = {1, 1, 1, 1, 1}; // each loop layer's step back under another
    Svf m_hp1L, m_hp1R, m_hp2L, m_hp2R;
    Ensemble m_ensemble;
    DubEcho m_echo;
    GuitarAmp m_amp;
};

}
