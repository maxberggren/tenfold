#pragma once

// The Chords part's engine: the harmony (which notes a degree and a colour
// make in the current key, and how to voice them so one chord flows into
// the next), the sounds, and the tempo-locked gates that give a held
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
enum Style : int {
    Pad,
    Trance,
    Organ,
    Piano,
    Pluck,
    Pump,
    Keys,
    Strings,
    Dub,
    Guitar,
    Accordion,
    Kalimba,
    Gamelan,
    Steel,
    Sitar,
    StyleCount
};

enum Pattern : int { Hold, Offbeat, Eighths, Tresillo, Charleston, TranceGate, Sixteenths, Wub, PatternCount };

// How a chord sounds. Saw styles are unison sawtooths through a ladder;
// the organ is a drawbar wavetable with percussion and a key click; the
// piano is two-operator FM with a hammer; the electric piano is FM with a
// tine's bell on the attack and a pickup's bark; the string machine is
// phase-locked octave saws (a divide-down organ's) through one low-pass; the
// guitar is a plucked string (Karplus-Strong) per note. Beyond the dance
// floor: the accordion is free reeds a note, beating against each other and
// breathing with the bellows; the kalimba and the gamelan are struck tines
// and bars, their partials out of the harmonic series as metal's are, and
// they play the chord one tine or bar at a time, interlocking, as their
// players do; the pedal steel is strings that swell in under a volume pedal,
// slide on a bar and bend with the pedals; the sitar is a string buzzing on
// its curved bridge, bent by pressing, with strings tuned to the key ringing
// in sympathy.
struct StyleDef {
    const char *name;
    const char *blurb;
    enum Osc : uint8_t {
        Saw,
        OrganOsc,
        PianoOsc,
        TineOsc,
        StringOsc,
        PluckedOsc,
        ReedOsc,
        KalimbaOsc,
        GamelanOsc,
        SteelOsc,
        SitarOsc
    } osc;
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
    // ensemble chorus, the dub stab's echo, the sitar's sympathetic strings.
    enum Bus : uint8_t { Dry, Ensemble, Echo, Comp, Tarab } bus = Dry;
};
constexpr int kBuses = StyleDef::Tarab + 1;

// The sounds. Levels are matched by ear-proxy (RMS of a held
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
        // A musette accordion (Paris waltzes, cumbia, tango): free reeds a
        // note, in the musette three of them, one in tune, one a little
        // sharp and one as flat, so each note shimmers at a few beats a
        // second. The voicing keys are its register switches: below the
        // middle, bassoon and clarinet (a reed an octave down, no beating);
        // above it, the musette with a piccolo reed an octave up. Each reed
        // speaks a moment late and a little flat, with air and the valve's
        // knock; the bellows are the pressure, louder and brighter. On the
        // offbeat, the 3·3·2, the Charleston and the triplet wub its left
        // hand plays the Stradella buttons, the bass (root and fifth by
        // turns) on the accents and the chord between: polka, cumbia and
        // tango, and a waltz a beat; on the other gates the bellows shake.
        {.name = "ACCORDION", .blurb = "musette, Stradella bass", .osc = StyleDef::ReedOsc, .unison = 1,
         .detune = 0.0f, .a = 0.05f, .d = 1.5f, .s = 0.9f, .r = 0.07f, .cutoff = 0.68f, .envAmt = 0.05f,
         .envDecay = 0.3f, .res = 0.0f, .legato = true, .glide = 0.006f, .centre = 63.0f, .gain = 0.31f,
         .width = 0.8f, .pattern = Hold},
        // A kalimba, the thumb piano of the mbira's family: each tine a sine
        // with its two bending modes far above (6.3 and 17.6 times, gone in
        // a few milliseconds, the lower one as loud as the tine for the
        // first three), struck 13 cents sharp, with the nail's tick (a flat
        // finger plays with the thumb's soft pad instead), in a small box
        // that sings at 250 Hz and 1.1 kHz, with bottle caps that rattle at
        // the crest of each swing of a tine just plucked hard enough to
        // lift them (pressing harder loosens them). Played as it is played: on each
        // hit of the gate one thumb plucks one tine, the thumbs taking turns,
        // the left walking up its side and the right down its own, so a held
        // chord becomes an interlocking figure.
        {.name = "KALIMBA", .blurb = "thumb piano, two thumbs", .osc = StyleDef::KalimbaOsc, .unison = 1,
         .detune = 0.0f, .a = 0.0008f, .d = 1.0f, .s = 1.0f, .r = 0.9f, .cutoff = 0.98f, .envAmt = 0.0f,
         .envDecay = 0.2f, .res = 0.0f, .retrig = true, .glide = 0.01f, .centre = 70.0f, .gain = 0.33f,
         .strum = 0.03f, .width = 0.7f, .pattern = Eighths},
        // A Balinese gamelan: every note two bronze bars, tuned a few hertz
        // apart (the ombak, the beating that makes a gamelan shimmer); each
        // bar with its partials at 2.76, 5.4, 8.9 and 13 times, the high
        // ones dying first, over a resonator tube that swells the
        // fundamental in. It plays kotekan, two players interlocking, polos
        // on the left and sangsih on the right, each damping the bar it
        // struck last as it strikes the next: on its sixteenths nyog cag
        // (the sangsih anticipating the polos a step higher, one line
        // moving every sixteenth), on the triplet gate norot (each tone of
        // the chord alternated with the note above it).
        {.name = "GAMELAN", .blurb = "bronze, kotekan", .osc = StyleDef::GamelanOsc, .unison = 1,
         .detune = 0.0f, .a = 0.0006f, .d = 1.0f, .s = 1.0f, .r = 0.6f, .cutoff = 0.96f, .envAmt = 0.0f,
         .envDecay = 0.2f, .res = 0.0f, .retrig = true, .glide = 0.01f, .centre = 67.0f, .gain = 0.52f,
         .strum = 0.028f, .width = 0.75f, .pattern = Sixteenths},
        // A pedal steel guitar (country ballads, Hawaiian, slowcore): steel
        // strings picked dark with the volume pedal down and swelled in
        // (slowly under a light touch, quickly under a firm one), so a chord
        // blooms without a pick's edge, and sustaining for many seconds
        // through a gentle sustainer; the bar slides from the last chord's
        // notes to the new ones; pressing harder pushes the B pedal (the
        // third up to the fourth) and then the A pedal (the fifth up to the
        // sixth), easing off resolves them (the steel's cry); a flat finger
        // pulls the knee lever that lowers the root to the note under it; a
        // held chord grows the bar's vibrato as soon as it has swelled in.
        {.name = "STEEL", .blurb = "pedal steel, swells, bends", .osc = StyleDef::SteelOsc, .unison = 1,
         .detune = 0.0f, .a = 0.16f, .d = 1.0f, .s = 1.0f, .r = 0.6f, .cutoff = 0.92f, .envAmt = 0.0f,
         .envDecay = 0.2f, .res = 0.0f, .legato = true, .glide = 0.075f, .centre = 64.0f, .gain = 0.8f,
         .strum = 0.012f, .width = 1.0f, .pattern = Hold},
        // A sitar: a steel string per note buzzing on the jawari, the wide
        // curved bridge that shortens the string as it swings onto it, so
        // the note blooms with a buzz that sweeps down through its overtones
        // over two or three seconds and keeps buzzing for many more; raked
        // from the bottom with the mizrab, the chikari
        // drone strings struck after it; pressing harder pulls the top
        // string up to the next note of the key (the meend); and under it a
        // row of sympathetic strings tuned to the key's scale, ringing on.
        {.name = "SITAR", .blurb = "jawari, meend, sympathetic", .osc = StyleDef::SitarOsc, .unison = 1,
         .detune = 0.0f, .a = 0.0008f, .d = 1.0f, .s = 1.0f, .r = 0.5f, .cutoff = 0.88f, .envAmt = 0.0f,
         .envDecay = 0.2f, .res = 0.0f, .retrig = true, .glide = 0.01f, .centre = 64.0f, .gain = 0.3f,
         .strum = 0.024f, .width = 0.6f, .pattern = Hold, .bus = StyleDef::Tarab},
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
constexpr int Rate = 46, Pump = 47;
constexpr int Columns = 48;  // per lane 8 apart: how loud each degree is sounding
constexpr int Pattern = 64;  // per style: the pattern sounding (override included)
constexpr int Gate = 96;     // per style: the gate's level now (moved past the patterns when the sounds outgrew 32..45)
static_assert(Pattern + StyleCount <= Gate && Gate + StyleCount <= kPartData);
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
    // For the sounds that bend (see Engine::bends): how far up each note's
    // next note of the key lies, in semitones (the sitar's meend, the
    // gamelan's norot); how far the pedal steel's pedals raise it (the third
    // to the fourth, the fifth to the sixth; nothing for the others) and
    // which pedal does (1 the B pedal on the third, 2 the A pedal on the
    // fifth); and how far its knee lever lowers the root, to the note of the
    // key under it (negative; nothing for the others).
    float lift[4] = {};
    float pedal[4] = {};
    uint8_t pedalOf[4] = {};
    float lower[4] = {};
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
    // Where each of the pattern's four beats begins, hit or rest, and where
    // it has lasted as long as a hit would.
    uint8_t beatOn[kBlock] = {}, beatOff[kBlock] = {};
};

// The gates on which the accordion's left hand plays its Stradella buttons
// (the bass on the accents and on the beats, the chord on the other hits):
// the offbeat (a polka's oom-pa), the 3·3·2 (cumbia, tango's habanera), the
// Charleston, and the triplet wub (oom-pa-pa, a waltz a beat). On the
// others its bellows shake.
inline bool stradella(int pattern) {
    return pattern == Offbeat || pattern == Tresillo || pattern == Charleston || pattern == Wub;
}

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

// A string whose length can change while it rings: the sitar's and the
// pedal steel's. A plucked string like the guitar's (Karplus-Strong), but
// read at a fractional delay through a cubic interpolator that moves a
// little every sample, so a bend, a meend or a bar's slide retunes it
// without a step and without plucking it again. The pick is noise through
// a third-order low-pass (a state-variable filter, whose zero at the top of
// the band one-poles lack, and a one-pole), so it carries nothing of the
// pick's scrape above a few kilohertz into a loop that would keep it
// ringing.
// The sitar's string is heard through its bridge, the jawari: a wide,
// gently curved bridge the string grazes on one side of each swing. The
// graze is a narrow one-sided pulse a period (every harmonic of the note,
// the buzz). The bridge feeds the swing's energy back into the overtones,
// so the buzz outlasts the note under it: its level falls at half the
// rate (in decibels) of the string's own. The pulse is worked out at twice
// the rate and brought back down through a half-band filter, so its sharp
// edges fold no tones above 10 kHz back into the sound. And the point
// where the string leaves the curve walks along the bridge as the swing
// narrows, which acts on the sound as a comb (on its treble only, so the
// note keeps its body) whose teeth sit far apart at the pluck and close up
// over the next two or three seconds: its first peak sweeps from about
// 5 kHz down to 500 Hz, the sitar's "yowww", bright and nasal at the
// pluck, purer as it rings. (A length that follows the swing inside the
// loop would do the same in principle, but every fraction of a sample it
// moves costs the interpolator treble, and the string came out darker,
// not buzzing.)
struct BentString {
    static constexpr int kSize = 2048; // a period of up to about 2040 samples: down to about 24 Hz
    static constexpr int kComb = 128;  // the jawari's comb, up to 2.6 ms
    static constexpr int kHalf = 11;   // the half-band filter's taps, at twice the rate
    float line[kSize] = {};
    int w = 0;
    float delay = 100, delayStep = 0; // the loop's length now, and its move a sample
    bool fresh = true;
    float lp = 0, damp = 0.9f, loss = 0.999f;
    float excAmp = 0;
    Svf exc;
    OnePoleLp exc2;
    int excLeft = 0, excLength = 1;
    float energy = 0;
    // The jawari (the sitar only): the swing's level, the widest since the
    // pluck, how fast the level lets go; the comb, how far its sweep has
    // gone since the pluck and the treble it acts on; the string a sample
    // ago and the graze at twice the rate, for the half-band filter.
    float env = 0, widest = 1e-3f, envRel = 0.999f;
    DcBlock dc;
    float comb[kComb] = {};
    int combW = 0;
    float sweep = 0, sweepCoef = 0, combLow = 0, combLowCoef = 0.03f;
    int rewind = 0;
    float buzzLevel = 0, levelCoef = 0.007f;
    static constexpr float kRewind = 0.012f; // a sample, while rewinding: 1 % left after 8 ms
    float before = 0, half[kHalf] = {};
    float rate = 48000;

    void clear() {
        std::fill(std::begin(line), std::end(line), 0.0f);
        std::fill(std::begin(comb), std::end(comb), 0.0f);
        std::fill(std::begin(half), std::end(half), 0.0f);
        lp = 0;
        exc.reset();
        exc2.y = 0;
        excLeft = 0;
        energy = 0;
        env = 0;
        widest = 1e-3f;
        sweep = combLow = before = buzzLevel = 0;
        rewind = 0;
        dc = DcBlock();
        fresh = true;
    }
    // Tunes the loop to hz over the next n samples. ring is the time to
    // die by 60 dB, bright the loop's lowpass (1 lets everything through).
    void tune(float hz, float r, float ring, float bright, int n) {
        rate = r;
        hz = clampf(hz, 30.0f, r * 0.2f);
        damp = bright;
        loss = std::pow(10.0f, -3.0f / (ring * hz));
        const float wv = kTwoPi * hz / r, b = 1.0f - damp;
        const float lpDelay = std::atan2(b * std::sin(wv), 1.0f - b * std::cos(wv)) / wv;
        const float to = clampf(r / hz - lpDelay, 3.0f, float(kSize - 8));
        if (fresh) {
            delay = to;
            fresh = false;
        }
        delayStep = (to - delay) / float(std::max(1, n));
        // The level follower holds over a period and a little more.
        envRel = std::exp(-1.0f / (std::max(0.02f, 2.5f / hz) * r));
        // The comb's sweep: most of the way in a second, all of it in three.
        sweepCoef = 1.0f - std::exp(-1.0f / (0.9f * r));
        combLowCoef = 1.0f - std::exp(-kTwoPi * 350.0f / r);
        levelCoef = 1.0f - std::exp(-1.0f / (0.003f * r));
    }
    inline float tap(int i) const { return line[(w - i) & (kSize - 1)]; }
    // The string d samples back, between samples (Catmull-Rom, as Delay::read).
    inline float read(float d) const {
        d = std::max(d, 2.0f);
        const int i = int(d);
        const float t = d - float(i);
        const float y0 = tap(i - 1), y1 = tap(i), y2 = tap(i + 1), y3 = tap(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
    // A pick: a burst of noise share of a period long, low-passed at hz,
    // faded over the burst so it has no edge.
    void pluck(float amp, float share, float hz) {
        excAmp = amp;
        exc.set(hz, 0.707f, rate);
        exc2.set(hz, rate);
        excLength = std::max(8, int(delay * share));
        excLeft = excLength;
        widest = 1e-3f;
        rewind = int(0.008f * rate);
    }
    inline float step(Rng &rng) {
        float in = 0;
        if (excLeft > 0) {
            in = exc2.step(exc.step(rng.bipolar()).low) * excAmp * float(excLeft) / float(excLength);
            --excLeft;
        }
        delay += delayStep;
        const float x = read(delay);
        lp += (x - lp) * damp;
        const float out = lp * loss + in;
        line[w] = out;
        w = (w + 1) & (kSize - 1);
        return out;
    }
    // The graze on the bridge: a soft one-sided pulse at the crest of the
    // swing (a tanh, not a step).
    inline float graze(float x) const { return fastTanh(7.0f * (x / (env + 1e-5f) - 0.55f)); }
    // The string heard through the jawari (see above).
    inline float jawari(float x) {
        env = std::max(std::fabs(x), env * envRel);
        widest = std::max(widest, env);
        const float wide = env / widest;
        // The graze at twice the rate (the string halfway between this
        // sample and the last, then this one), through the half-band filter.
        std::copy(half + 2, half + kHalf, half);
        half[kHalf - 2] = graze(0.5f * (before + x));
        half[kHalf - 1] = graze(x);
        before = x;
        float g = kHalfCentre * half[kHalf / 2];
        for (int k = 0; k < kHalf / 2; k += 2)
            g += kHalfTaps[k / 2] * (half[k] + half[kHalf - 1 - k]);
        const float buzz = dc.step(g);
        // (The buzz's level glides over a few milliseconds: a fresh pluck
        // resets the widest swing, and a step in the level mid-pulse would
        // tick.)
        const float root = std::sqrt(wide);
        buzzLevel += (kBuzz * widest * root * (0.5f + 0.5f * root) - buzzLevel) * levelCoef;
        const float s = x + buzzLevel * buzz;
        comb[combW] = s;
        combW = (combW + 1) & (kComb - 1);
        // The comb's delay: 0.1 ms at the pluck (its first peak near 5 kHz),
        // 1 ms two or three seconds on (near 500 Hz). A fresh pluck sends
        // it back to the start over 8 ms, never in a jump (a jump of its tap
        // would click across the band).
        if (rewind > 0) {
            --rewind;
            sweep -= sweep * kRewind;
        } else {
            sweep += (1.0f - sweep) * sweepCoef;
        }
        const float d = (0.0001f + 0.0009f * sweep) * rate;
        const int i = std::clamp(int(d), 1, kComb - 3);
        const float t = d - float(i);
        const float a0 = comb[(combW - i) & (kComb - 1)], a1 = comb[(combW - i - 1) & (kComb - 1)];
        const float back = a0 + t * (a1 - a0);
        combLow += (back - combLow) * combLowCoef;
        return s - kTeeth * (back - combLow);
    }
    static constexpr float kBuzz = 1.0f, kTeeth = 0.85f;
    // An 11-tap half-band low-pass (every other tap zero), its centre tap
    // and the outer ones from the edge in.
    static constexpr float kHalfCentre = 0.5f;
    static constexpr float kHalfTaps[3] = {0.0488f, -0.0832f, 0.2844f};
};

// The sitar's sympathetic strings (the tarab): a row of strings under the
// played ones, tuned to the notes of the key, never plucked, set ringing by
// whatever the played strings sound that they share a pitch or an overtone
// with, and ringing on after the chord is let go: the halo around a sitar.
// Each is tuned by an allpass for the fraction of a sample (a linear read
// would dull the upper strings and pull them flat) and loses little in its
// loop, so the halo shimmers. With them, the two chikari: high drone
// strings tuned to the key's tonic and its octave, struck after a rake and
// on the gate's off-hits, the sitar's rhythm strings. One set for the whole
// bus, so it costs the same for any number of chords.
struct Tarab {
    static constexpr int kStrings = 9, kSize = 1024;
    struct Str {
        float line[kSize] = {};
        int w = 0, whole = 100;
        float apC = 0, apX = 0, apY = 0;
        float lp = 0, damp = 0.9f, loss = 0.999f, pan = 0;
    };
    Str str[kStrings];
    BentString chik[2];
    float chikHz[2] = {523.0f, 1046.0f};
    int chikIn = -1;
    float chikAmp = 0;
    float limEnv = 0, limAtk = 0.02f, limRel = 0.9997f; // the peaks' easing (see process)
    Svf droneLp, droneLp2;
    Rng rng{0x7ABAB5u};
    float rate = 48000;
    int idle = 1 << 30;
    static constexpr float kCouple = 0.0026f, kWet = 0.5f, kChikari = 0.5f, kChikPick = 1.6f;

    void init(float r) {
        rate = r;
        for (BentString &c : chik)
            c.clear();
        limAtk = 1.0f - std::exp(-1.0f / (0.001f * r));
        droneLp.set(6000.0f, 0.541f, r);
        droneLp2.set(6000.0f, 1.307f, r);
        limRel = std::exp(-1.0f / (0.08f * r));
    }
    // Tunes string k to midi (each to its own note of the scale).
    void tune(int k, float midi) {
        Str &s = str[k];
        const float hz = clampf(midiToHz(midi), 60.0f, rate * 0.2f);
        s.damp = 0.93f;
        s.loss = std::pow(10.0f, -3.0f / (4.0f * hz));
        const float wv = kTwoPi * hz / rate, b = 1.0f - s.damp;
        const float lpDelay = std::atan2(b * std::sin(wv), 1.0f - b * std::cos(wv)) / wv;
        const float total = std::min(float(kSize - 4), rate / hz - lpDelay);
        s.whole = std::max(2, int(total - 0.1f));
        const float frac = total - float(s.whole);
        s.apC = (1.0f - frac) / (1.0f + frac);
        s.pan = -0.7f + 1.4f * float(k) / float(kStrings - 1);
    }
    // The chikari on the key's tonic at midi and an octave over it.
    void tuneChikari(float midi) {
        chikHz[0] = midiToHz(midi);
        chikHz[1] = midiToHz(midi + 12.0f);
    }
    // Strikes the chikari at sample at of the coming blocks.
    void strikeChikari(int at, float amp) {
        chikIn = at;
        chikAmp = amp;
    }
    // Adds the dry bus, the chikari and the strings' ringing to out.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        float in = 0;
        for (int i = 0; i < n; ++i)
            in += std::fabs(inL[i]) + std::fabs(inR[i]);
        idle = in > 1e-7f ? 0 : idle + n;
        if (idle > int(12.0f * rate)) // every string has died away
            return;
        // The chikari, a little right of centre (they sit beside the
        // played strings on the neck), feeding the sympathetic strings as
        // the played ones do.
        float drone[kBlock] = {};
        for (int c = 0; c < 2; ++c) {
            BentString &s = chik[c];
            const bool due = chikIn >= 0 && chikIn < n;
            if (s.energy < 1e-6f && s.excLeft == 0 && !due)
                continue;
            s.tune(chikHz[c], rate, 3.0f, 0.82f, n);
            float e = 0;
            for (int i = 0; i < n; ++i) {
                // A short, dark stroke (low-passed at 5 kHz): the
                // mizrab's flick on steel, never a click across the band.
                if (i == chikIn)
                    s.pluck(kChikPick * chikAmp * (c == 0 ? 1.0f : 0.7f), 0.25f, 5000.0f);
                const float x = s.step(rng);
                e = std::max(e, std::fabs(x));
                drone[i] += x;
            }
            s.energy = std::max(e, 0.9f * s.energy);
        }
        if (chikIn >= 0)
            chikIn = chikIn >= n ? chikIn - n : -1;
        // The chikari heard through the sitar's gourd and its own bridge:
        // nothing of them above about 6 kHz, where their stroke would read
        // as a tick across the band.
        for (int i = 0; i < n; ++i)
            drone[i] = droneLp2.step(droneLp.step(drone[i]).low).low;
        float wetL[kBlock] = {}, wetR[kBlock] = {};
        for (Str &s : str) {
            const float gl = std::sqrt(0.5f * (1.0f - s.pan)), gr = std::sqrt(0.5f * (1.0f + s.pan));
            for (int i = 0; i < n; ++i) {
                const float x = s.line[(s.w - s.whole) & (kSize - 1)];
                const float y = s.apC * (x - s.apY) + s.apX;
                s.apX = x;
                s.apY = y;
                s.lp += (y - s.lp) * s.damp;
                const float out = s.lp * s.loss + kCouple * (inL[i] + inR[i] + kChikari * drone[i]);
                s.line[s.w] = out;
                s.w = (s.w + 1) & (kSize - 1);
                wetL[i] += out * gl;
                wetR[i] += out * gr;
            }
        }
        // A raked chord's strings, each buzzing in a narrow pulse at its
        // crest, stack into peaks far over the sound's body; past kKnee
        // they are eased down, so the sitar sits as loud as the other
        // sounds without its peaks reaching the studio's ceiling. The
        // easing is a gain that follows the peaks (in a millisecond, back
        // over 80 ms), not a curve bent into the wave, which would add
        // overtones of its own up to the top of the band.
        for (int i = 0; i < n; ++i) {
            const float d = kChikari * drone[i];
            const float l = inL[i] + 0.8f * d, r = inR[i] + 1.1f * d;
            const float a = std::max(std::fabs(l), std::fabs(r));
            limEnv = a > limEnv ? limEnv + (a - limEnv) * limAtk : limEnv * limRel;
            const float g = limEnv > kKnee ? knee(limEnv) / limEnv : 1.0f;
            outL[i] += g * l + kWet * wetL[i];
            outR[i] += g * r + kWet * wetR[i];
        }
    }
    // The level a peak of x is eased to: itself up to kKnee, then less and
    // less of what is over it.
    static float knee(float x) {
        const float a = std::fabs(x);
        if (a <= kKnee)
            return x;
        const float over = a - kKnee;
        return std::copysign(kKnee + over / (1.0f + over / kSoft), x);
    }
    static constexpr float kKnee = 0.2f, kSoft = 0.15f;
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
        BentString wire;      // the sitar's and the pedal steel's string
        // The kalimba's tines and the gamelan's bars ring each on its own,
        // struck one at a time: the level, what a strike climbs to (over
        // half a millisecond, never a jump) and its fall a sample (faster
        // once a gamelan player damps it); the upper modes' levels, the
        // tine's pitch drop, the gamelan's resonator swelling the
        // fundamental in, and the side it was struck from.
        float ring = 0, ringPeak = 0, ringDecay = 1;
        int ringRise = 0;
        float m2 = 0, m3 = 0, m4 = 0, m5 = 0, drop = 0, bloom = 1, side = 0, sideTo = 0;
        float osc[10][2] = {}; // the gamelan's partials, each a rotating pair (cos, sin)
        // The kalimba's caps: how high this tine's swing still lifts them
        // (it lets go faster than the tine rings, so the rattle dies with
        // each pluck), and the tine's phase a sample ago, to find its crest.
        float capLift = 0, lastPhase = 0;
        float speak = 1;       // the accordion: how far the reed has started to speak
        int speakIn = 0;       // samples before it starts
        float lift = 0, pedal = 0, lower = 0; // see Voicing
        uint8_t pedalOf = 0;
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
    // The kalimba's thumbs and the gamelan's players: where each has got to
    // in its walk through the chord, the bar each struck last, and the hits
    // since the chord began.
    int walk[2] = {}, lastStruck[2] = {-1, -1}, hits = 0;
    // The accordion's bellows (the pressure, smoothed per sample, so a
    // sudden push never ticks) and its shake on the gate, with the flutter
    // of each turn; the valve's knock. Its left hand on a Stradella gate:
    // the bass button's and the chord buttons' levels and where each is
    // heading, the bass note (the root, MIDI) and the fifth over it
    // (semitones), the basses played since the chord began (they alternate
    // root and fifth), and the bass reeds' phases.
    float bellows = 0.5f, shake = 1, flutter = 0, knock = 0;
    float lhBass = 0, lhChord = 0, lhBassTo = 0, lhChordTo = 0;
    float bassMidi = 48, fifthSemis = 7;
    int basses = 0;
    bool bassOnFifth = false;
    float lhPhase[3] = {};
    // The bend of the sitar's meend and of the steel's B pedal (0..1), the
    // steel's A pedal and knee lever, the length of its swell (seconds),
    // and its bar: seconds held, for its vibrato, and the vibrato's phase.
    float bend = 0, bendA = 0, lever = 0, swell = 0.3f, held = 0, vib = 0;
    float sustRef = 0, sustGain = 1; // the steel's sustainer (see renderVoice)
    // The kalimba's caps: the tick of the last lift, falling in a couple of
    // milliseconds, its second low-pass, and the nail's tick.
    float capTick = 0, nail = 0;
    Svf bodyA, bodyB, buzzBp, buzzLp, capLp2, nailBp;
    OnePoleLp clickLp;
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
        m_tarab.init(rate);
        auto fall = [rate](float seconds) { return std::exp(-1.0f / (seconds * rate)); };
        m_fall2ms = fall(0.002f);
        m_fall3ms = fall(0.003f);
        m_fall25ms = fall(0.025f);
        m_capFall = fall(0.0007f);
        m_nailFall = fall(0.0005f);
        m_fall8ms = fall(0.008f);
        m_fall20ms = fall(0.02f);
        m_fall40ms = fall(0.04f);
        m_fall400ms = fall(0.4f);
        m_fall1600ms = fall(1.6f);
        m_fall120ms = fall(0.12f);
        m_fall50ms = fall(0.05f);
        m_tetekep = fall(0.05f);
        m_airDecay = fall(0.05f);
        m_bloom = 1.0f - fall(0.06f);
        m_bellowsCoef = 1.0f - fall(0.06f);
        m_shakeCoef = 1.0f - fall(0.03f);
        m_buttonOn = 1.0f - fall(0.012f);
        m_buttonOff = 1.0f - fall(0.025f);
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
        // (−9 dB in about 30 ms) until the finger lifts, far enough for the
        // new chord to lead, not so far that the loop drops out while a
        // player tries out the next layer over it. The same chord over
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
        m_loopGain += ((clash ? kLiveDuck : 1.0f) - m_loopGain) * k;
        // The loop's layers never step back for each other: each is a voice
        // of its own, held at its own level whatever the others play, so a
        // layer recorded over the loop adds a part instead of taking turns
        // with it.
        // Voices of a style with an effect of its own render to that
        // effect's bus; the effect then adds its bus to the mix.
        float mixL[kBlock] = {}, mixR[kBlock] = {};
        float busL[kBuses][kBlock] = {}, busR[kBuses][kBlock] = {};
        float dubPressure = -1;
        for (auto &v : m_voices)
            if (v.active) {
                const bool loop = v.lane >= kReplayLane;
                const StyleDef::Bus bus = styleDef(v.style).bus;
                float *oL = bus == StyleDef::Dry ? mixL : busL[bus];
                float *oR = bus == StyleDef::Dry ? mixR : busR[bus];
                renderVoice(v, oL, oR, n, loop ? loop0 : 1.0f, loop ? m_loopGain : 1.0f);
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
        m_tarab.process(busL[StyleDef::Tarab], busR[StyleDef::Tarab], mixL, mixR, n);
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
            // (Not the gamelan's spare bar: it plays the figure's passing
            // notes, not the chord.)
            const bool spare = v && styleDef(v->style).osc == StyleDef::GamelanOsc;
            if (v)
                for (const auto &nt : v->notes)
                    if (nt.ampTarget > 0.05f && count < ChordVoice::kNotes && !(spare && nt.role == ChordVoice::Low))
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
        long lastBeat = -1;
        bool beatWasOn = false;
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
    // The loop's level under a different chord held live (−9 dB).
    static constexpr float kLiveDuck = 0.355f;

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
        // The sitar's sympathetic strings retune to the key: its notes
        // upwards from the first at or above E4, as many as there are strings.
        double low = t.baseMidi();
        while (low < 64.0)
            low += 12.0;
        while (low >= 76.0)
            low -= 12.0;
        for (int k = 0; k < Tarab::kStrings; ++k)
            m_tarab.tune(k, float(low + t.degreeSemis(k) - t.degreeSemis(0)));
        // The chikari: the tonic from C5 up, and its octave.
        m_tarab.tuneChikari(float(low < 72.0 ? low + 12.0 : low));
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
        Voicing vc = voiceChord(t, shape, centre, sd.spread, prev);
        const double rootMidi = t.baseMidi() + t.degreeSemis(e.degree);
        bends(t, rootMidi, vc);
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
                // The pedal steel picks the strings again as the bar slides,
                // the volume pedal pulled back and swelled in once more.
                if (sd.osc == StyleDef::SteelOsc) {
                    cur->sinceStrum = -1;
                    cur->amp.v *= 0.3f;
                    swellIn(*cur, e.pressure);
                    cur->amp.on();
                    cur->held = 0;
                }
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
        // The accordion's left hand: its bass button on the root, in the
        // octave from G2, and the fifth of the key over it for the
        // alternating bass.
        if (L.voice >= 0) {
            ChordVoice &now = m_voices[size_t(L.voice)];
            double bass = rootMidi;
            while (bass >= 55.0)
                bass -= 12.0;
            while (bass < 43.0)
                bass += 12.0;
            now.bassMidi = float(bass);
            double fifth = interval(t, e.degree, 1);
            for (int k = 2; k < m_count; ++k)
                if (std::fabs(interval(t, e.degree, k) - 7.0) < std::fabs(fifth - 7.0))
                    fifth = interval(t, e.degree, k);
            now.fifthSemis = float(fifth);
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

    // How far each note of a voicing may bend: up to the next note of the
    // key (the sitar's meend, at most two semitones), and, for the pedal
    // steel's pedals, the third up to the fourth and the fifth up to the
    // sixth (the next note of the key above each), the others not at all.
    static void bends(const Tuning &t, double rootMidi, Voicing &vc) {
        const int count = t.degreesPerOctave();
        for (int k = 0; k < vc.count; ++k) {
            const double m = vc.notes[k];
            double next = m + 2.0;
            for (int d = 0; d < 10 * count; ++d) {
                const double p = t.baseMidi() + t.degreeSemis(d);
                if (p > m + 0.5) {
                    next = std::min(next, p);
                    break;
                }
            }
            vc.lift[k] = float(next - m);
            const double above = std::fmod(m - rootMidi + 1200.0, 12.0);
            const bool third = above > 2.5 && above < 4.5, fifth = above > 6.5 && above < 7.5;
            vc.pedal[k] = third || fifth ? vc.lift[k] : 0.0f;
            vc.pedalOf[k] = third ? 1 : fifth ? 2 : 0;
            // The root's note of the key below: the knee lever's E-lower,
            // which turns the chord's root into its seventh.
            vc.lower[k] = 0;
            if (above < 0.5 || above > 11.5) {
                double prevNote = m - 2.0;
                for (int d = 10 * count; d > -10 * count; --d) {
                    const double p = t.baseMidi() + t.degreeSemis(d);
                    if (p < m - 0.5) {
                        prevNote = std::max(prevNote, p);
                        break;
                    }
                }
                vc.lower[k] = float(prevNote - m);
            }
        }
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
        // All busy: take the quietest one ringing out, else the oldest.
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
        // The hammer's, the key click's, the thumb's and the mallet's
        // noise; the accordion's, the air through an opening valve.
        v.clickBp.set(e.style == Piano       ? 2600.0f
                      : e.style == Keys      ? 1300.0f
                      : e.style == Kalimba   ? 1200.0f
                      : e.style == Gamelan   ? 1600.0f
                      : e.style == Accordion ? 2200.0f
                                             : 3400.0f,
                      e.style == Kalimba || e.style == Gamelan || e.style == Accordion ? 0.8f : 1.2f, m_rate);
        // (The thumb and the mallet are soft: nothing of them above about
        // 6 kHz.)
        v.clickLp.y = 0;
        v.clickLp.set(6000.0f, m_rate);
        // The kalimba's box (its air at 250 Hz, its wood at 1.1 kHz), its
        // bottle caps' rattle (a ringing band at 3.6 kHz, then a fourth-order
        // low-pass at 6 kHz) and its nail's tick (a broader band around
        // 3 kHz, through the same low-pass); the accordion's valve knock; the
        // gamelan's mallet, second-order low-passed at 5 kHz after its band.
        for (Svf *f : {&v.bodyA, &v.bodyB, &v.buzzBp, &v.buzzLp, &v.capLp2, &v.nailBp})
            f->reset();
        v.bodyA.set(e.style == Accordion ? 350.0f : 250.0f, e.style == Accordion ? 1.5f : 3.0f, m_rate);
        v.bodyB.set(1100.0f, 2.5f, m_rate);
        v.buzzBp.set(3600.0f, 8.0f, m_rate);
        v.buzzLp.set(e.style == Gamelan ? 5000.0f : 6000.0f, e.style == Gamelan ? 0.707f : 0.541f, m_rate);
        v.capLp2.set(6000.0f, 1.307f, m_rate);
        v.nailBp.set(3000.0f, 1.2f, m_rate);
        v.capTick = 0;
        v.nail = 0;
        v.walk[0] = v.walk[1] = 0;
        v.lastStruck[0] = v.lastStruck[1] = -1;
        v.hits = 0;
        v.bellows = e.pressure;
        v.shake = 1;
        v.flutter = 0;
        v.knock = 1;
        // On a Stradella gate the left hand presses its bass button as the
        // chord starts, so even a tap between the gate's hits speaks; on any
        // other the right hand's chord sounds at once.
        const bool buttons = sd.osc == StyleDef::ReedOsc && stradella(effectivePattern(e.style));
        v.lhBass = 0;
        v.lhBassTo = buttons ? 1.0f : 0.0f;
        v.lhChord = v.lhChordTo = buttons ? 0.0f : 1.0f;
        v.basses = buttons ? 1 : 0;
        v.bassOnFifth = false;
        v.bend = v.bendA = v.lever = 0;
        v.sustRef = 0;
        v.sustGain = 1;
        v.held = 0;
        v.vib = 0;
        if (sd.osc == StyleDef::SteelOsc)
            swellIn(v, e.pressure);
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
        // in turn (see strum), the same way on every hit of its gate; the
        // kalimba's tines and the gamelan's bars are struck one by one (see
        // strikeTines).
        const bool strings = sd.osc == StyleDef::PluckedOsc || sd.osc == StyleDef::SitarOsc ||
                             sd.osc == StyleDef::SteelOsc || sd.osc == StyleDef::KalimbaOsc ||
                             sd.osc == StyleDef::GamelanOsc;
        const float noteStrum = strings ? 0.0f : sd.strum;
        // A gamelan player damps the bars of the last chord as the new one is
        // struck (tetekep), and the pedal steel's bar lifts off the last
        // chord's strings: neither is left ringing under the new chord.
        if (sd.osc == StyleDef::GamelanOsc || sd.osc == StyleDef::SteelOsc)
            for (auto &o : m_voices)
                if (&o != &v && o.active && o.releasing && o.lane == lane && o.style == e.style)
                    release(o, sd.osc == StyleDef::SteelOsc ? 0.03f : 0.06f);
        // The bar slides into the chord from where it was: each string from
        // the last chord's nearest note, if it is within a fourth.
        const Lane &L = m_lanes[lane];
        const bool slide = sd.osc == StyleDef::SteelOsc && L.hasLast;
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
            if (sd.osc == StyleDef::SitarOsc || sd.osc == StyleDef::SteelOsc)
                nt.wire.clear();
            nt.ring = nt.ringPeak = 0;
            nt.ringRise = 0;
            nt.ringDecay = 1;
            nt.m2 = nt.m3 = nt.m4 = nt.m5 = nt.drop = 0;
            nt.bloom = 1;
            nt.side = nt.sideTo = 0;
            if (sd.osc == StyleDef::GamelanOsc)
                for (int j = 0; j < 10; ++j) {
                    const float ph = v.rng.unit();
                    nt.osc[j][0] = std::cos(kTwoPi * ph);
                    nt.osc[j][1] = std::sin(kTwoPi * ph);
                }
            // Each of the accordion's reeds speaks 8 to 22 ms after its valve
            // opens.
            nt.speak = sd.osc == StyleDef::ReedOsc ? 0.0f : 1.0f;
            nt.speakIn = sd.osc == StyleDef::ReedOsc ? int((0.008f + 0.014f * v.rng.unit()) * m_rate) : 0;
            nt.lift = nt.pedal = nt.lower = 0;
            nt.pedalOf = 0;
            nt.capLift = nt.lastPhase = 0;
            if (k < vc.count) {
                nt.role = ChordVoice::Core;
                nt.midi = nt.target = vc.notes[k];
                nt.lift = vc.lift[k];
                nt.pedal = vc.pedal[k];
                nt.pedalOf = vc.pedalOf[k];
                nt.lower = vc.lower[k];
                if (slide) {
                    float from = nt.target, best = 1e9f;
                    for (int j = 0; j < L.last.count; ++j)
                        if (std::fabs(L.last.notes[j] - nt.target) < best) {
                            best = std::fabs(L.last.notes[j] - nt.target);
                            from = L.last.notes[j];
                        }
                    if (best <= 5.0f)
                        nt.midi = from;
                }
                nt.amp = nt.ampPrev = nt.ampTarget = 1;
                nt.delay = int(noteStrum * float(k) * m_rate);
                nt.onset = nt.delay > 0 ? 0.0f : 1.0f;
            } else if (k == 4) {
                nt.role = ChordVoice::Low;
                // (The gamelan's is its spare bar, tuned as it is struck:
                // see strikeTines.)
                nt.exists = low > 0 || sd.osc == StyleDef::GamelanOsc;
                nt.midi = nt.target = low > 0 ? low : vc.notes[0];
                nt.amp = nt.ampPrev = nt.ampTarget = lowAmp(sd, v.big, nt.exists);
            } else if (k == 5) {
                nt.role = ChordVoice::Top;
                nt.exists = top > 0;
                nt.midi = nt.target = top > 0 ? top : vc.notes[vc.count - 1];
                nt.amp = nt.ampPrev = nt.ampTarget = topAmp(sd, v.big, nt.exists);
                nt.delay = int(noteStrum * float(vc.count) * m_rate);
                nt.onset = nt.delay > 0 ? 0.0f : 1.0f;
            } else {
                nt.role = ChordVoice::Core;
                nt.midi = nt.target = vc.notes[0];
            }
        }
    }

    // The doublings' levels. The steel has none: its flat finger is the
    // knee lever.
    // The gamelan's spare bar is always open: it sounds only when struck.
    static float lowAmp(const StyleDef &sd, float big, bool has) {
        if (sd.osc == StyleDef::SteelOsc)
            return 0.0f;
        if (sd.osc == StyleDef::GamelanOsc)
            return has ? 1.0f : 0.0f;
        return has ? (sd.spread ? 0.5f : 0.0f) + 0.45f * big : 0.0f;
    }
    static float topAmp(const StyleDef &sd, float big, bool has) {
        return has && sd.osc != StyleDef::SteelOsc ? 0.55f * big : 0.0f;
    }

    // The steel's volume pedal: a light touch swells the chord in slowly
    // (0.8 s), a firm one quickly (0.25 s). The envelope is squared as it
    // plays (see renderVoice), so the swell starts gently and arrives
    // smoothly, a foot easing the pedal down, not a soft attack.
    void swellIn(ChordVoice &v, float pressure) {
        const StyleDef &sd = styleDef(Steel);
        const float u = clampf((pressure - 0.2f) / 0.6f, 0.0f, 1.0f);
        v.swell = 0.8f - 0.55f * u;
        v.amp.set(v.swell / 1.61f, sd.d, sd.s, sd.r, m_rate);
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
                v.notes[owned[best]].lift = vc.lift[i];
                v.notes[owned[best]].pedal = vc.pedal[i];
                v.notes[owned[best]].pedalOf = vc.pedalOf[i];
                v.notes[owned[best]].lower = vc.lower[i];
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
                nt.lift = vc.lift[i];
                nt.pedal = vc.pedal[i];
                nt.pedalOf = vc.pedalOf[i];
                nt.lower = vc.lower[i];
                nt.amp = nt.ampPrev = 0;
                nt.ampTarget = 1;
                nt.delay = 0;
                nt.pluckIn = 0; // a string fading in is plucked, a tine or bar struck (the others ignore it)
                nt.pluckAmp = 0.7f * v.vel;
                nt.pluckMute = 0;
                // An accordion's new reed speaks late, with its valve's knock
                // and a breath of air.
                nt.speak = 0;
                nt.speakIn = int((0.008f + 0.014f * v.rng.unit()) * m_rate);
                if (v.style == Accordion) {
                    v.knock = 1;
                    v.clickEnv = std::max(v.clickEnv, 0.6f);
                }
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
        const bool spare = styleDef(v.style).osc == StyleDef::GamelanOsc;
        lo.exists = low > 0 || spare;
        if (low > 0 && !spare)
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
            R.lastBeat = -1;
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
            const long beat = long(std::floor(pos * 4.0 / double(S)));
            G.beatOn[i] = beat != R.lastBeat && R.lastBeat >= 0;
            R.lastBeat = beat;
            const bool beatOn = pos - double(beat) * double(S) / 4.0 < 2.0 * double(R.length);
            G.beatOff[i] = !beatOn && R.beatWasOn;
            R.beatWasOn = beatOn;
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
        // The sitar and the steel are struck open every time: no palm, no
        // dead strokes.
        const bool guitar = sd.osc == StyleDef::PluckedOsc, sitar = sd.osc == StyleDef::SitarOsc;
        const float palm = gated && guitar ? clampf((0.7f - len) / 0.6f, 0.0f, 0.75f) : 0.0f;
        int since = v.sinceStrum;
        for (int i = from; i < n; ++i) {
            const bool first = since < 0;
            ++since;
            if (!first && !(gated && G.hitOn[i] && since > int(0.03f * m_rate)))
                continue;
            const bool accent = first || G.hitOn[i] == 2;
            since = 0;
            // The pedal steel is picked as the chord starts (and as the bar
            // slides to a new one), never on a gate.
            if (sd.osc == StyleDef::SteelOsc && !first)
                continue;
            // The sitar's gate is its right hand's rhythm: the accents rake
            // the chord, the hits between them strike the chikari alone.
            if (sitar && !accent) {
                m_tarab.strikeChikari(i + int(0.004f * m_rate * v.rng.unit()), 0.5f * (0.6f + 0.5f * v.vel));
                continue;
            }
            v.upStroke = accent ? false : !v.upStroke;
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
            if (!guitar)
                mute = 0;
            for (int r = 0; r < count; ++r) {
                auto &nt = v.notes[order[r]];
                const int rank = v.upStroke ? count - 1 - r : r;
                nt.pluckIn = i - from + late + int(per * float(rank));
                // An upstroke catches the top strings hardest.
                const float weight = v.upStroke ? 0.6f + 0.4f * float(r) / float(std::max(1, count - 1)) : 1.0f;
                nt.pluckAmp = (accent ? 1.0f : 0.72f) * weight * (0.92f + 0.16f * v.rng.unit());
                nt.pluckMute = mute;
            }
            // After a rake, the little finger catches the chikari.
            if (sitar)
                m_tarab.strikeChikari(i + late + int(per * float(count)) + int(0.07f * m_rate),
                                      0.35f * (0.6f + 0.5f * v.vel));
        }
        v.sinceStrum = since;
    }

    // The kalimba's thumbs and the gamelan's players: on each hit of the
    // gate (and as a chord starts) one tine or bar is struck, not the chord.
    // Without a gate, the chord is struck once, rolled up from the bottom.
    // The kalimba's tines lie alternately left and right of the middle, so
    // the chord's lowest tone, third and so on are the left thumb's and the
    // others the right's: the thumbs take turns, hit by hit, the left on the
    // chord's first hit (the beat, on its own gate) and walking up its tines,
    // the right walking down its own. The
    // gamelan plays kotekan: one figure of eight sixteenths through the
    // chord, its even steps struck by the polos (left), its odd ones by the
    // sangsih (right), each player damping the bar it struck before.
    void strikeTines(ChordVoice &v, const GateBlock &G, int from, int n) {
        const StyleDef &sd = styleDef(v.style);
        const bool gated = G.active && !v.releasing;
        const bool gamelan = sd.osc == StyleDef::GamelanOsc;
        int since = v.sinceStrum;
        for (int i = from; i < n; ++i) {
            const bool first = since < 0;
            ++since;
            if (!first && !(gated && G.hitOn[i]))
                continue;
            since = 0;
            int order[4], count = 0;
            for (int k = 0; k < 4; ++k)
                if (v.notes[k].ampTarget > 0.01f)
                    order[count++] = k;
            for (int a = 1; a < count; ++a)
                for (int b = a; b > 0 && v.notes[order[b]].target < v.notes[order[b - 1]].target; --b)
                    std::swap(order[b], order[b - 1]);
            if (count == 0)
                continue;
            const int at = i - from;
            auto strike = [&](int k, int when, float amp, float side) {
                auto &nt = v.notes[k];
                nt.pluckIn = when;
                nt.pluckAmp = amp * (0.9f + 0.2f * v.rng.unit());
                nt.sideTo = side;
            };
            if (first && !gated) {
                for (int r = 0; r < count; ++r)
                    strike(order[r], at + int(sd.strum * m_rate * float(r)), 0.85f, (r & 1) ? 1.0f : -1.0f);
                continue;
            }
            const bool accent = first || G.hitOn[i] == 2;
            const float amp = accent ? 1.0f : 0.72f;
            if (gamelan) {
                // A tone of the figure, counted up the chord from its lowest
                // (past the top: the note of the key above it), or the note
                // of the key above one; both of those on the spare bar
                // (slot 4), tuned as it is struck.
                auto bar = [&](int tone, bool neighbour) {
                    tone = std::clamp(tone, 0, count);
                    if (tone < count && !neighbour)
                        return order[tone];
                    const auto &under = v.notes[order[std::min(tone, count - 1)]];
                    auto &spare = v.notes[4];
                    spare.midi = spare.target = under.target + under.lift;
                    return 4;
                };
                const int step = v.hits++;
                int player, k;
                if (effectivePattern(v.style) == Wub) {
                    // Norot, on the triplet gate: each tone of the chord in
                    // turn (up and back down) alternated with the note of the
                    // key above it, the polos on the tone and the sangsih on
                    // its neighbour, for two beats; the last two strokes
                    // already on the next tone, leading into it.
                    const int span = std::max(1, 2 * count - 2);
                    auto toneAt = [&](int g) {
                        const int w = g % span;
                        return w < count ? w : span - w;
                    };
                    const int g = step / 6, h = step % 6;
                    player = h & 1;
                    k = bar(toneAt(h < 4 ? g : g + 1), player == 1);
                } else {
                    // Nyog cag: the polos on the beat-side sixteenths walks a
                    // figure (low, middle, high, low); the sangsih, on the
                    // sixteenths between, anticipates the polos's next tone a
                    // step higher, so the two parts are one line moving every
                    // sixteenth (low, high, middle, above the top, high,
                    // middle, low, high), twice as fast as either.
                    static const int polos[4] = {0, 1, 2, 0};
                    player = step & 1;
                    k = player == 0 ? bar(polos[(step / 2) & 3], false) : bar(polos[((step + 1) / 2) & 3] + 1, false);
                }
                // Tetekep: the bar this player struck last is damped, unless
                // the other player has just struck it too.
                const int last = v.lastStruck[player];
                if (last >= 0 && last != k && last != v.lastStruck[1 - player])
                    v.notes[last].ringDecay = m_tetekep;
                v.lastStruck[player] = k;
                strike(k, at, amp, player ? 1.0f : -1.0f);
            } else {
                auto next = [&](int thumb) {
                    int side[4], m = 0;
                    for (int r = 0; r < count; ++r)
                        if ((r & 1) == thumb)
                            side[m++] = order[r];
                    if (m == 0)
                        return order[0];
                    const int w = v.walk[thumb]++ % m;
                    return thumb == 0 ? side[w] : side[m - 1 - w];
                };
                const int thumb = v.hits++ & 1;
                strike(next(thumb), at, amp, thumb ? 1.0f : -1.0f);
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
                nt.ampTarget = topAmp(sd, v.bigSm, nt.exists);
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
        const bool struck =
            sd.osc == StyleDef::OrganOsc || sd.osc == StyleDef::PianoOsc || sd.osc == StyleDef::TineOsc;
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
        if (sd.osc == StyleDef::PluckedOsc || sd.osc == StyleDef::SitarOsc || sd.osc == StyleDef::SteelOsc)
            strumOnHits(v, G, from, n);
        const bool tines = sd.osc == StyleDef::KalimbaOsc || sd.osc == StyleDef::GamelanOsc;
        if (tines)
            strikeTines(v, G, from, n);
        // The sitar's meend: pressing past 0.6 bends, all the way by 0.9, the
        // string moving over about 70 ms (a hand pulling the string along
        // the fret). The steel's pedals, one after the other as the press
        // deepens: the B pedal (the third up to the fourth) from 0.55 to
        // 0.72, the A pedal (the fifth up to the sixth) from 0.72 to 0.9;
        // and its knee lever, the E-lower, with a flat finger (the root down
        // to the note of the key under it). A bend is taken from where the
        // pressure stands, not from where it landed, so a chord landed hard
        // sounds bent at once and resolves as the finger eases (the steel's
        // cry, played backwards).
        const bool steel = sd.osc == StyleDef::SteelOsc;
        if (sd.osc == StyleDef::SitarOsc || steel) {
            auto smooth = [](float x, float a, float b) {
                const float u = clampf((x - a) / (b - a), 0.0f, 1.0f);
                return u * u * (3.0f - 2.0f * u);
            };
            const float k = 1.0f - std::exp(-float(n) / (0.07f * m_rate));
            v.bend += ((steel ? smooth(v.pressureSm, 0.55f, 0.72f) : smooth(v.pressureSm, 0.6f, 0.9f)) - v.bend) * k;
            v.bendA += (smooth(v.pressureSm, 0.72f, 0.9f) - v.bendA) * k;
            v.lever += (smooth(v.bigSm, 0.35f, 0.75f) - v.lever) * k;
        }
        // The steel's bar: a vibrato that grows in as soon as the swell has
        // arrived, over half a second, a tenth of a semitone each way at
        // 5 Hz.
        float vibrato = 0;
        if (steel) {
            v.held += float(n) * invRate;
            v.vib = wrap01(v.vib + 5.0f * float(n) * invRate);
            vibrato = 0.1f * clampf((v.held - v.swell) / 0.5f, 0.0f, 1.0f) * lut(tables().sine, v.vib);
        }
        // The highest note of the chord sounding: the sitar's main string,
        // the one the meend pulls.
        int topNote = -1;
        for (int k = 0; k < 4; ++k)
            if (v.notes[k].ampTarget > 0.01f && (topNote < 0 || v.notes[k].target > v.notes[topNote].target))
                topNote = k;

        // The accordion's bellows and left hand, sample by sample: the
        // pressure smoothed over 60 ms (louder, and brighter, the harder the
        // push). On a Stradella gate (see stradella) the left hand plays
        // the buttons: each accent, and on the offbeat each beat, presses
        // the bass button (the root and the fifth by turns), each other hit
        // the chord button, every press speaking in about 12 ms and let go
        // in 25; the right hand rests. On any other gate the bellows shake
        // in time: the sound swells by about 4 dB on each hit (never a
        // chop), and at each turn of the bellows the reeds sag a few cents
        // flat and a little darker for a moment, with a breath of air.
        const bool accordion = sd.osc == StyleDef::ReedOsc;
        bool buttons = false;
        float rhAt[kBlock], bassAt[kBlock], belAt[kBlock], flutAt[kBlock];
        if (accordion) {
            const int pat = effectivePattern(v.style);
            buttons = G.active && stradella(pat);
            const bool play = G.active && !v.releasing;
            for (int i = from; i < n; ++i) {
                v.bellows += (v.pressure - v.bellows) * m_bellowsCoef;
                if (buttons) {
                    if (play && ((pat == Offbeat && G.beatOn[i]) || G.hitOn[i] == 2)) {
                        v.lhBassTo = 1;
                        v.lhChordTo = 0;
                        v.bassOnFifth = (v.basses++ & 1) != 0;
                        v.knock = std::max(v.knock, 0.5f);
                    } else if (play && G.hitOn[i]) {
                        v.lhChordTo = 1;
                        v.lhBassTo = 0;
                        v.knock = std::max(v.knock, 0.4f);
                    }
                    if (play && G.hitOff[i])
                        v.lhBassTo = v.lhChordTo = 0;
                    if (play && pat == Offbeat && G.beatOff[i])
                        v.lhBassTo = 0;
                    v.shake += (1.0f - v.shake) * m_shakeCoef;
                } else {
                    v.lhBassTo = 0;
                    v.lhChordTo = 1;
                    if (play) {
                        v.shake += (0.63f + 0.37f * std::min(1.0f, G.g[i]) - v.shake) * m_shakeCoef;
                        if (G.hitOn[i]) {
                            v.flutter = 1;
                            v.clickEnv = std::max(v.clickEnv, 0.4f);
                        }
                    } else {
                        v.shake += (1.0f - v.shake) * m_shakeCoef;
                    }
                }
                v.lhBass += (v.lhBassTo - v.lhBass) * (v.lhBassTo > v.lhBass ? m_buttonOn : m_buttonOff);
                v.lhChord += (v.lhChordTo - v.lhChord) * (v.lhChordTo > v.lhChord ? m_buttonOn : m_buttonOff);
                v.flutter *= m_fall40ms;
                rhAt[i] = v.lhChord;
                bassAt[i] = v.lhBass;
                belAt[i] = (0.4f + 0.9f * v.bellows) * v.shake;
                flutAt[i] = v.flutter;
            }
        }

        // Oscillators, before the envelope and the filter.
        float bL[kBlock] = {}, bR[kBlock] = {};
        // The kalimba: the caps' ticks as the tines lift them, and the
        // nail's, sample by sample.
        float lifted[kBlock] = {}, nailed[kBlock] = {};
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
            } else if (sd.osc == StyleDef::ReedOsc) {
                // The accordion: free reeds, each a narrow pulse (hollow and
                // nasal, a notch every fifth harmonic) with some saw for body.
                // The register switches: the musette's three 8' reeds, the
                // middle in tune and the other two a few hertz sharp and as
                // flat (at most 13 cents), so the note beats; below the
                // middle voicing, bassoon and clarinet, a 16' reed and one 8'
                // (no beating, dry and dark); above it the musette and a 4'
                // piccolo reed.
                const float c = std::min(0.0075f, 3.0f / hz);
                const float inc0 = std::min(hz * invRate, 0.3f);
                const float incs[5] = {inc0, inc0 * (1.0f + c), inc0 * (1.0f - c), 0.5f * inc0,
                                       std::min(2.0f * inc0, 0.3f)};
                const float musette = v.reg >= 0 ? 0.62f : 0.0f;
                const float levels[5] = {v.reg < 0 ? 0.8f : v.reg > 0 ? 0.56f : 0.62f, musette, musette,
                                         v.reg < 0 ? 0.7f : 0.0f, v.reg > 0 ? 0.8f : 0.0f};
                static const float duties[5] = {0.22f, 0.22f, 0.22f, 0.34f, 0.22f};
                const float pan = 0.6f * sd.width;
                const float sideL = std::sqrt(0.5f * (1.0f + pan)), sideR = std::sqrt(0.5f * (1.0f - pan));
                const float pans[5][2] = {{0.707f, 0.707f}, {sideL, sideR}, {sideR, sideL}, {0.8f, 0.6f}, {0.6f, 0.8f}};
                // A reed speaks a moment after its valve opens, swelling in
                // over about 15 ms and sagging a sixth of a semitone flat
                // until it does (the pitch a free reed has before it is fully
                // swinging).
                float spk[kBlock];
                const float speakCoef = 1.0f - std::exp(-1.0f / (0.015f * m_rate));
                for (int i = start; i < n; ++i) {
                    if (nt.speakIn > 0)
                        --nt.speakIn;
                    else
                        nt.speak += (1.0f - nt.speak) * speakCoef;
                    spk[i] = nt.speak;
                }
                // (On a Stradella gate the chord sounds only under its
                // button, and a little fuller: the left hand's chord reeds
                // are several to a button.)
                float lvl[kBlock];
                const float press = buttons ? kChordButton : 1.0f;
                for (int i = start; i < n; ++i)
                    lvl[i] = spk[i] * rhAt[i] * press;
                for (int u = 0; u < 5; ++u) {
                    if (levels[u] <= 0)
                        continue;
                    float ph = nt.phase[u];
                    const float duty = duties[u];
                    const float gl = pans[u][0] * levels[u], gr = pans[u][1] * levels[u];
                    for (int i = start; i < n; ++i) {
                        const float inc = incs[u] * (1.0f - 0.01f * (1.0f - spk[i]) - kFlutter * flutAt[i]);
                        ph += inc;
                        if (ph >= 1.0f)
                            ph -= 1.0f;
                        float p2 = ph + duty;
                        if (p2 >= 1.0f)
                            p2 -= 1.0f;
                        const float saw = 2.0f * ph - 1.0f - polyBlep(ph, inc);
                        const float saw2 = 2.0f * p2 - 1.0f - polyBlep(p2, inc);
                        const float x = (0.5f * (saw - saw2) + 0.3f * saw) * lvl[i];
                        nl[i] += x * gl;
                        nr[i] += x * gr;
                    }
                    nt.phase[u] = ph;
                }
            } else if (sd.osc == StyleDef::KalimbaOsc) {
                // The kalimba: a tine is a bar clamped at one end, its modes
                // at 1, 6.27 and 17.55 times the note. A pluck rings the
                // fundamental for a second or two (less up high); the
                // 6.27 mode speaks nearly as loud as it for the first 3 ms
                // (the metallic "t" of the attack) and leaves a faint tail
                // for 25 ms, and the 17.55 mode is gone in 2. The tine's
                // swing is a little lopsided (a touch of the second
                // harmonic, its mean taken back off), and it is struck about
                // 13 cents sharp, settling as the bend of the strike relaxes.
                // At the crest of each upward swing a tine that still swings
                // wide enough lifts the bottle caps, and they drop back with
                // a tick: a tick a period, at the tine's own pitch, the
                // rattle's buzz. How wide is enough is the caps' weight
                // (pressing harder loosens them); how long a pluck keeps
                // lifting them falls faster than the tine rings, so the
                // rattle dies with each pluck and leaves the tine singing.
                const bool due = nt.pluckIn >= 0 && nt.pluckIn < n - from;
                if (nt.ring > 1e-4f || nt.ringRise > 0 || due) {
                    const float inc = hz * invRate;
                    const float inc2 = std::min(6.27f * inc, 0.45f), inc3 = std::min(17.55f * inc, 0.45f);
                    const bool has2 = 6.27f * hz < 12000.0f, has3 = 17.55f * hz < 12000.0f;
                    const float ringFall =
                        std::exp(-1.0f / (clampf(1.8f - (nt.midi - 60.0f) / 30.0f, 0.5f, 1.8f) * m_rate));
                    nt.side += (nt.sideTo - nt.side) * 0.3f;
                    const float pan = 0.5f * nt.side * sd.width;
                    const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
                    const float weight = 0.55f - 0.35f * v.pressureSm;
                    const float liftFall =
                        std::exp(-1.0f / ((0.04f + 0.36f * v.pressureSm * v.pressureSm) * m_rate));
                    // The nail: the tip of the thumb, unless the finger is
                    // laid flat (the thumb's soft pad).
                    const float nailTip = 1.0f - clampf((v.bigSm - 0.2f) / 0.5f, 0.0f, 1.0f);
                    float p1 = nt.phase[0], p2 = nt.phase[1], p3 = nt.phase[2];
                    for (int i = start; i < n; ++i) {
                        if (i - from == nt.pluckIn) {
                            nt.ringPeak = std::max(nt.ring, nt.pluckAmp);
                            nt.ringRise = 24;
                            nt.ringDecay = ringFall;
                            nt.m2 = nt.m3 = nt.m4 = nt.pluckAmp;
                            nt.drop = 1;
                            nt.capLift = 1;
                            v.clickEnv = 1;
                            nailed[i] = std::max(nailed[i], nailTip * nt.pluckAmp);
                        }
                        if (nt.ringRise > 0) {
                            nt.ring += (nt.ringPeak - nt.ring) / float(nt.ringRise);
                            --nt.ringRise;
                        } else {
                            nt.ring *= nt.ringDecay;
                        }
                        nt.m2 *= m_fall3ms;
                        nt.m4 *= m_fall25ms;
                        nt.m3 *= m_fall2ms;
                        nt.drop *= m_fall40ms;
                        nt.capLift *= liftFall;
                        p1 = wrap01(p1 + inc * (1.0f + 0.0075f * nt.drop));
                        p2 = wrap01(p2 + inc2);
                        p3 = wrap01(p3 + inc3);
                        const float s1 = lut(tb.sine, p1);
                        const float s2 = lut(tb.sine, p2);
                        // (The upper modes climb in with the strike's half
                        // millisecond, as the fundamental does: a mode that
                        // started at its peak would click across the band.)
                        const float in = 1.0f - float(nt.ringRise) / 24.0f;
                        const float x = nt.ring * (s1 + 0.14f * (s1 * s1 - 0.5f)) +
                                        in * ((has2 ? (0.6f * nt.m2 + 0.07f * nt.m4) * s2 : 0.0f) +
                                              (has3 ? 0.04f * nt.m3 * lut(tb.sine, p3) : 0.0f));
                        // The crest of the swing (a quarter of the way round).
                        if (nt.lastPhase < 0.25f && p1 >= 0.25f) {
                            const float over = nt.ring * nt.capLift - weight;
                            if (over > 0)
                                lifted[i] = std::max(lifted[i], over * nt.amp);
                        }
                        nt.lastPhase = p1;
                        nl[i] += x * gl;
                        nr[i] += x * gr;
                    }
                    nt.phase[0] = p1;
                    nt.phase[1] = p2;
                    nt.phase[2] = p3;
                }
                if (nt.pluckIn >= 0)
                    nt.pluckIn = nt.pluckIn >= n - from ? nt.pluckIn - (n - from) : -1;
            } else if (sd.osc == StyleDef::GamelanOsc) {
                // The gamelan: two bars a note, the pengumbang a little low
                // and the pengisep as high, so the pair beats at a steady
                // six or so hertz across the range (the ombak; at most 11
                // cents each way); each a free bar's modes at 1, 2.76, 5.4,
                // 8.9 and 13 times, the upper ones dying first: 5.4 in
                // 400 ms, 8.9 in 120 and 13 in 50, the clang of the strike
                // that makes a gangsa shimmer and then sing. The resonator
                // tube under the bar swells the fundamental in over the
                // first 100 ms or so. Each partial is a rotating pair, four
                // multiplies a sample, and a bar that has died away (or
                // been damped) costs nothing.
                const bool due = nt.pluckIn >= 0 && nt.pluckIn < n - from;
                if (nt.ring > 1e-4f || nt.ringRise > 0 || due) {
                    constexpr int M = 5;
                    const float half = std::min(3.1f, hz * 0.0064f);
                    const float ratios[M] = {1.0f, 2.76f, 5.4f, 8.9f, 13.0f};
                    float rc[2 * M], rs[2 * M];
                    for (int j = 0; j < M; ++j)
                        for (int b = 0; b < 2; ++b) {
                            const float w = kTwoPi * std::min((hz + (b ? half : -half)) * ratios[j] * invRate, 0.45f);
                            rc[M * b + j] = std::cos(w);
                            rs[M * b + j] = std::sin(w);
                        }
                    // (A mode past 14 kHz is left out rather than folded.)
                    float level[M] = {0, 0.42f, 0.22f, 0.13f, 0.08f};
                    for (int j = 2; j < M; ++j)
                        if (ratios[j] * hz > 14000.0f)
                            level[j] = 0;
                    const float ringFall =
                        std::exp(-1.0f / (clampf(3.2f - (nt.midi - 60.0f) / 20.0f, 1.0f, 3.2f) * m_rate));
                    // The polos left, the sangsih right; within a note, the
                    // pair of bars a little apart.
                    nt.side += (nt.sideTo - nt.side) * 0.3f;
                    const float pan = 0.55f * nt.side * sd.width;
                    const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
                    const float spread = 0.25f * sd.width;
                    const float gU = std::sqrt(0.5f * (1.0f + spread)), gS = std::sqrt(0.5f * (1.0f - spread));
                    float (*o)[2] = nt.osc;
                    for (int i = start; i < n; ++i) {
                        if (i - from == nt.pluckIn) {
                            nt.ringPeak = std::max(nt.ring, nt.pluckAmp);
                            nt.ringRise = 24;
                            nt.ringDecay = ringFall;
                            v.clickEnv = std::max(v.clickEnv, nt.pluckAmp);
                        }
                        // A strike climbs over its half millisecond: the
                        // ring to its peak, the upper modes back to full and
                        // the resonator's share down to where it swells from
                        // (a bar struck while it rings would click if any of
                        // them jumped).
                        if (nt.ringRise > 0) {
                            const float r = 1.0f / float(nt.ringRise);
                            nt.ring += (nt.ringPeak - nt.ring) * r;
                            nt.m2 += (1.0f - nt.m2) * r;
                            nt.m3 += (1.0f - nt.m3) * r;
                            nt.m4 += (1.0f - nt.m4) * r;
                            nt.m5 += (1.0f - nt.m5) * r;
                            nt.bloom += (0.3f - nt.bloom) * r;
                            --nt.ringRise;
                        } else {
                            nt.ring *= nt.ringDecay;
                            nt.m2 *= m_fall1600ms;
                            nt.m3 *= m_fall400ms;
                            nt.m4 *= m_fall120ms;
                            nt.m5 *= m_fall50ms;
                            nt.bloom += (1.0f - nt.bloom) * m_bloom;
                        }
                        for (int j = 0; j < 2 * M; ++j) {
                            const float c = o[j][0], sn = o[j][1];
                            o[j][0] = c * rc[j] - sn * rs[j];
                            o[j][1] = c * rs[j] + sn * rc[j];
                        }
                        const float a[M] = {nt.bloom, level[1] * nt.m2, level[2] * nt.m3, level[3] * nt.m4,
                                            level[4] * nt.m5};
                        float u = 0, s2 = 0;
                        for (int j = 0; j < M; ++j) {
                            u += a[j] * o[j][1];
                            s2 += a[j] * o[M + j][1];
                        }
                        const float x = 0.72f * nt.ring;
                        nl[i] += x * gl * (u * gU + s2 * gS);
                        nr[i] += x * gr * (u * gS + s2 * gU);
                    }
                    // (Rounding lets a rotating pair drift from the unit
                    // circle: put it back once a block.)
                    for (int j = 0; j < 2 * M; ++j) {
                        const float g = 1.5f - 0.5f * (o[j][0] * o[j][0] + o[j][1] * o[j][1]);
                        o[j][0] *= g;
                        o[j][1] *= g;
                    }
                }
                if (nt.pluckIn >= 0)
                    nt.pluckIn = nt.pluckIn >= n - from ? nt.pluckIn - (n - from) : -1;
            } else if (sd.osc == StyleDef::SteelOsc || sd.osc == StyleDef::SitarOsc) {
                // A string that bends: the sitar's, buzzing on the jawari,
                // its top string pulled up by the meend; the pedal steel's,
                // through a pickup near the bridge (the string a ninth of a
                // period earlier subtracted, a bright, glassy comb), its
                // third and fifth raised by the pedals and all of them moved
                // by the bar's vibrato. Low strings left, high ones right.
                auto &str = nt.wire;
                const int k = int(&nt - v.notes);
                const float pan = (nt.role == ChordVoice::Low   ? -0.6f
                                   : nt.role == ChordVoice::Top ? 0.7f
                                                                : -0.4f + 0.3f * float(k)) *
                                  sd.width;
                const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
                const bool waiting = nt.pluckIn >= 0 && nt.pluckIn < n - from;
                if (str.energy > 1e-6f || str.excLeft > 0 || waiting) {
                    const float pedals = nt.pedalOf == 1 ? v.bend : nt.pedalOf == 2 ? v.bendA : 0.0f;
                    const float bent = steel ? pedals * nt.pedal + v.lever * nt.lower + vibrato
                                             : (k == topNote ? v.bend * std::min(nt.lift, 2.0f) : 0.0f);
                    const float bhz = clampf(midiToHz(nt.midi + bent), 30.0f, m_rate * 0.2f);
                    // Steel strings sustain: the steel's ring for half a
                    // minute or more, the sitar's for twenty seconds, both
                    // through loops that lose little of their treble a pass
                    // (the jawari keeps the sitar's overtones fed).
                    const float ring = steel ? kSteelRing : kSitarRing;
                    const float bright = steel ? 0.995f
                                               : clampf(0.97f - 0.08f * (bhz / 1500.0f), 0.75f, 0.97f);
                    str.tune(bhz, m_rate, ring, bright, n - start);
                    const float pick = 0.11f * str.delay;
                    float e = 0;
                    for (int i = start; i < n; ++i) {
                        // The steel's pick is dark (a fingerpick under the
                        // volume pedal, nothing of it above about 3 kHz),
                        // the sitar's mizrab brighter (5 kHz).
                        if (i - from == nt.pluckIn) {
                            if (steel)
                                str.pluck(kSteelPick * nt.pluckAmp * (0.6f + 0.5f * v.vel), 0.5f, 3000.0f);
                            else
                                str.pluck(kSitarPick * nt.pluckAmp * (0.6f + 0.5f * v.vel), 1.0f, 5000.0f);
                        }
                        float x = str.step(v.rng);
                        e = std::max(e, std::fabs(x));
                        x = steel ? 0.8f * (x - str.read(pick)) : str.jawari(x);
                        nl[i] += x * gl;
                        nr[i] += x * gr;
                    }
                    // (A pick shorter than the period leaves blocks of the
                    // line quiet while the wave goes round: the level is held
                    // over them, so a string is never taken for dead.)
                    str.energy = std::max(e, 0.9f * str.energy);
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

        // The accordion's bass button: the bass reed on the note and a reed
        // an octave over it (the low reeds alone would sit under the page's
        // high-pass), the root and the fifth of the key by turns.
        if (buttons) {
            float bassMidi = v.bassMidi + (v.bassOnFifth ? v.fifthSemis : 0.0f);
            if (bassMidi > 56.0f)
                bassMidi -= 12.0f;
            const float inc0 = midiToHz(bassMidi) * invRate;
            const float incs[2] = {inc0, 2.0f * inc0};
            static const float duties[2] = {0.34f, 0.22f}, levels[2] = {0.75f, 0.5f};
            for (int u = 0; u < 2; ++u) {
                float ph = v.lhPhase[u];
                for (int i = from; i < n; ++i) {
                    const float inc = incs[u] * (1.0f - kFlutter * flutAt[i]);
                    ph += inc;
                    if (ph >= 1.0f)
                        ph -= 1.0f;
                    float p2 = ph + duties[u];
                    if (p2 >= 1.0f)
                        p2 -= 1.0f;
                    const float saw = 2.0f * ph - 1.0f - polyBlep(ph, inc);
                    const float saw2 = 2.0f * p2 - 1.0f - polyBlep(p2, inc);
                    const float x = (0.5f * (saw - saw2) + 0.3f * saw) * bassAt[i] * levels[u] * kBass;
                    bL[i] += x;
                    bR[i] += x;
                }
                v.lhPhase[u] = ph;
            }
        }

        // Envelopes, gate, filter, pump.
        const float knob = (v.filterSm - 0.5f) * 0.9f;
        const float build = 0.07f * float(m_globalRate - 1);
        const float keyTrack = topMidi > 0 ? 0.12f * (topMidi - 64.0f) / 24.0f : 0.0f;
        // (The accordion's bellows brighten it sample by sample, below.)
        const float base = sd.cutoff + knob + (accordion ? 0.0f : 0.16f * (v.pressureSm - 0.5f)) + build + keyTrack;
        const float accent = 0.6f + 0.6f * v.vel;
        // The accordion's gate shakes the bellows rather than cutting the
        // sound (below).
        const bool chop = G.active && !sd.retrig && !accordion;
        // The tines, the bars and the sitar ring on between the gate's hits:
        // the hits strike them (see strikeTines, strumOnHits), they never
        // stop them.
        const bool strikeGate = G.active && sd.retrig && !tines && sd.osc != StyleDef::SitarOsc;
        const bool ladder = sd.osc == StyleDef::Saw;
        const bool mono = sd.osc == StyleDef::StringOsc; // one filter does for both sides
        const float clickLevel = v.style == Piano     ? 0.16f
                                 : v.style == Keys    ? 0.035f
                                 : v.style == Kalimba ? 0.03f
                                 : v.style == Gamelan ? 0.05f
                                                      : 0.1f;
        const bool click = struck;
        const bool kalimba = sd.osc == StyleDef::KalimbaOsc, gamelan = sd.osc == StyleDef::GamelanOsc;
        // The kalimba's bottle caps: how hard they rattle (pressing harder
        // loosens them; how far a tine must swing to lift them is set in its
        // oscillator).
        const float rattle = kRattle * (0.5f + v.pressureSm);
        const bool guitar = sd.osc == StyleDef::PluckedOsc;
        const float graceDecay = std::exp(-1.0f / (0.008f * m_rate));
        const float clickDecay = std::exp(-1.0f / (0.0035f * m_rate));
        // (The steel's pressure is its pedals, the accordion's its bellows:
        // neither is simply louder.)
        const float pressed = accordion || steel ? 1.0f : 0.82f + 0.36f * v.pressureSm;
        const float gain = sd.gain * (0.5f + 0.5f * v.vel) * pressed;
        // The organ's rotor, slow; the electric piano's tremolo, a stereo
        // pan at a few hertz (it keeps the mono sum whole). Each moves
        // smoothly across the block.
        const float rotDepth = v.style == Organ ? 0.13f : v.style == Keys ? 0.24f : 0.0f;
        const float rot0 = rotDepth * lut(tb.sine, v.leslie);
        v.leslie = wrap01(v.leslie + float(n) * (v.style == Keys ? 4.4f : 0.85f) * invRate);
        const float rot1 = rotDepth * lut(tb.sine, v.leslie);
        float peak = 0;
        const float sustain = steel ? v.sustGain : 1.0f;
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
            float a = v.amp.step();
            if (steel)
                a *= a; // the volume pedal's swell (see swellIn)
            const float fe = v.filt.step();
            // The accordion's bellows (worked out above).
            const float bellows = accordion ? belAt[i] : 1.0f;
            if (((i - from) & 7) == 0) {
                // A wub opens the filter as it swells: the chord talks.
                const float wub = chop && G.smooth ? 0.32f * (G.g[i] - 0.55f) : 0.0f;
                const float bel = accordion ? 0.24f * (v.bellows - 0.5f) + 0.3f * (v.shake - 1.0f) - 0.08f * flutAt[i]
                                            : 0.0f;
                const float c = clampf(base + sd.envAmt * fe * accent + wub + bel, 0.0f, 1.12f);
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
            if (kalimba) {
                // The box: its air and its wood, a little of each, rung by the
                // tines.
                const float m = 0.5f * (xl + xr);
                const float body = 0.2f * v.bodyA.step(m).band + 0.12f * v.bodyB.step(m).band;
                // The thumb's soft thump (through the low-pass below too);
                // the caps, a tick each time a tine drops them (the knock of
                // the metal landing, the same each time, so the ticks a
                // period apart buzz at the tine's pitch, and a scrape of
                // noise falling away in under a millisecond), ringing in
                // their own narrow band at 3.6 kHz; and the nail's tick,
                // brighter and broader. Caps and nail come after the box (it
                // does not ring with them) and through a fourth-order
                // low-pass at 6 kHz: a rattle, never a hiss across the band.
                const float nz = v.rng.bipolar();
                const float thumb = v.clickBp.step(nz).band * v.clickEnv * clickLevel;
                v.capTick = std::max(v.capTick * m_capFall, lifted[i]);
                v.nail = std::max(v.nail * m_nailFall, nailed[i]);
                const float drop = kKnock * lifted[i] + nz * v.capTick;
                const float ticks = v.buzzBp.step(drop * rattle).band + v.nailBp.step(nz * v.nail * kNail).band;
                const float caps = v.capLp2.step(v.buzzLp.step(ticks + thumb).low).low;
                xl += body + caps;
                xr += body + caps;
                v.clickEnv *= clickDecay;
            } else if (gamelan) {
                // The mallet: a wooden thock, low-passed at 5 kHz (second
                // order, after its band), nothing of it reaching the top of
                // the band.
                if (v.clickEnv > 1e-4f) {
                    const float c =
                        v.buzzLp.step(v.clickBp.step(v.rng.bipolar()).band).low * v.clickEnv * clickLevel;
                    xl += c;
                    xr += c;
                    v.clickEnv *= clickDecay;
                }
            } else if (accordion) {
                // Air through the valves as they open (and as the bellows
                // turn), a faint breath under the reeds as long as they
                // blow, and each valve's knock as it opens.
                const float nz = v.rng.bipolar();
                const float air = v.clickBp.step(nz).band * (0.05f * v.clickEnv + 0.008f * v.bellows);
                const float thud = v.bodyA.step(nz * v.knock).band * 0.05f;
                xl += air + thud;
                xr += air + thud;
                v.clickEnv *= m_airDecay;
                v.knock *= clickDecay;
            } else if (click && v.clickEnv > 1e-4f) {
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
            float g = a * gain * bellows * sustain * (g0 + (g1 - g0) * float(i) * invN);
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
        // The steel's sustainer, a slow compressor on the strings' level:
        // the level the swell arrived at is the reference, let go over
        // about eight seconds; while the chord is held, half of whatever
        // the strings have fallen under it (in decibels) is made up, at
        // most 4 dB, moving over a few hundred milliseconds, so a long
        // chord holds its bloom as a steel through a sustain pedal does.
        if (steel) {
            const float raw = peak / std::max(1e-6f, v.sustGain);
            const bool swelling = v.amp.stage == Env::Attack;
            v.sustRef = swelling ? raw : std::max(raw, v.sustRef * std::exp(-float(n) / (8.0f * m_rate)));
            const float want = swelling || v.releasing ? 1.0f : clampf(std::sqrt(v.sustRef / std::max(1e-6f, raw)), 1.0f, 1.58f);
            v.sustGain += (want - v.sustGain) * (1.0f - std::exp(-float(n) / (0.3f * m_rate)));
        }
        if (v.releasing && v.amp.stage == Env::Idle)
            v.active = false;
    }

    float m_rate = 48000;
    uint64_t m_age = 0;
    // Room for every layer of a loop and the live lane to hold a chord with
    // the last ones ringing out, so a layer never takes another's voice.
    std::array<ChordVoice, 16> m_voices;
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
    Svf m_hp1L, m_hp1R, m_hp2L, m_hp2R;
    Ensemble m_ensemble;
    DubEcho m_echo;
    GuitarAmp m_amp;
    Tarab m_tarab;
    // Falls a sample, worked out once: the kalimba's upper modes and its
    // strike's pitch drop, the gamelan's upper modes, a damped bar, the
    // accordion's air; and the gamelan's resonator bloom, the bellows' and
    // their shake's smoothing.
    float m_fall8ms = 0, m_fall20ms = 0, m_fall40ms = 0, m_fall400ms = 0, m_fall1600ms = 0, m_tetekep = 0;
    float m_airDecay = 0, m_bloom = 0, m_bellowsCoef = 0, m_shakeCoef = 0, m_buttonOn = 0, m_buttonOff = 0;
    float m_fall2ms = 0, m_fall3ms = 0, m_fall25ms = 0, m_capFall = 0, m_nailFall = 0, m_fall120ms = 0, m_fall50ms = 0;
    // The kalimba's caps' and nail's levels.
    static constexpr float kRattle = 0.05f, kNail = 0.05f, kKnock = 6.0f;
    // The accordion: how far a turn of the bellows sags its reeds (a share
    // of the pitch, about 7 cents) and its bass button's level.
    static constexpr float kFlutter = 0.004f, kBass = 1.4f, kChordButton = 1.35f;
    // The bending strings: how long they ring (seconds to fall 60 dB) and
    // how hard they are picked, set so each sits at the piano's loudness.
    static constexpr float kSteelRing = 60.0f, kSteelPick = 2.75f, kSitarRing = 22.0f, kSitarPick = 1.5f;
};

}
