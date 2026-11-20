#pragma once

// The Lead page's shared vocabulary: the pad's layout, the commands the page
// sends its engine, and the sounds. Included only by lead.cpp.

#include <algorithm>
#include <cmath>

#include "part.h"

namespace tf::lead {

// --- the pad ------------------------------------------------------------------
//
// The right hand plays the neck; the left hand works the column beside it:
//
//   ┌──────────┬────────────────────────────────────────┐
//   │  +OCT    │                                        │
//   │  6TH     │   NECK: the scale across, two octaves  │
//   │  5TH     │   up = brighter, press = swell,        │
//   │  3RD     │   slide = glide, wiggle = vibrato,     │
//   │  −OCT    │   twist = bend a step, flat = fatter   │
//   ├────┬─────┤                                        │
//   │BEND│THROW│                                        │
//   └────┴─────┴────────────────────────────────────────┘

constexpr float kColumn = 0.22f;     // the left hand's column, as a share of the pad's width
constexpr float kHarmBottom = 0.40f; // harmony rows above this, bend and throw below
constexpr float kBendSplit = 0.11f;  // bend on the left of the bottom, throw on the right
constexpr int kRows = 5;             // harmony intervals, from the bottom: −OCT, 3RD, 5TH, 6TH, +OCT
constexpr int kHarmSlots = 3;        // harmony voices at once
constexpr int kLines = 1 + kClipSlots; // the live player, and one per clip slot
constexpr int kKeyId = 0x400000;     // ids of notes played from the keyboard
constexpr int kNeckOctaves = 2;

inline const char *rowName(int row) {
    static const char *names[kRows] = {"−OCT", "3RD", "5TH", "6TH", "+OCT"};
    return names[std::clamp(row, 0, kRows - 1)];
}

// The neck's columns: two octaves of the scale and the root on top.
inline int neckCells(int perOctave) { return kNeckOctaves * perOctave + 1; }
// A pad x on the neck as a continuous scale position, cell centres on whole degrees.
inline float neckPosition(float x, int perOctave) {
    const int cells = neckCells(perOctave);
    return std::clamp((x - kColumn) / (1.0f - kColumn), 0.0f, 1.0f) * float(cells) - 0.5f;
}
inline float neckX(float position, int perOctave) {
    return kColumn + (1.0f - kColumn) * (position + 0.5f) / float(neckCells(perOctave));
}
// The neck's lowest octave for a key: the root lands between G3 and F♯4,
// where a lead sings above the chords without crowding them.
inline int neckOctave(int root) {
    const int base = 36 + root;
    return (55 - base + 11) / 12;
}

// --- commands -----------------------------------------------------------------
//
// On and Move (a note, from a finger on the neck or a key):
//   i[0] scale degree within the octave, i[1] octave (as Harmony::midi)
//   i[2] sound (On)
//   i[3] flags (On): kFollow, and the harmony rows held when it landed
//   f[0] velocity (On), f[1] brightness 0..1 (the finger's height)
//   f[2] the finger's offset from the degree, in degrees
//   f[3] motion 0..1: how much the pitch follows the finger rather than the degree
//   f[4] fatness 0..1 (contact size), f[5] twist bend in degrees, f[6] pressure
//
// Param, i[0] which:
//   PHarmony  i[1] slot, f[0] row 0..4 (fractional while sliding), f[1] level
//   PBend     f[0] degrees
//   PThrow    f[0] amount 0..1
//   POctave   f[0] 0 or 1 (the click)
//   PGlide    f[0] glide time scale
//   PGate     i[1] gate pattern (see kGates)
enum ParamKind { PHarmony = 0, PBend, PThrow, POctave, PGlide, PGate };
constexpr int kFollow = 1;
// The trance gate: off, or chopping the line in time, so a held note
// pulses. Steps per beat; the On flags carry it in bits 1–2.
constexpr int kGates = 4;
inline int gateSteps(int g) {
    static const int steps[kGates] = {0, 2, 4, 3};
    return steps[std::clamp(g, 0, kGates - 1)];
}
inline const char *gateName(int g) {
    static const char *names[kGates] = {"off", "1/8", "1/16", "1/8T"};
    return names[std::clamp(g, 0, kGates - 1)];
}
constexpr int kGateShift = 1;
// The harmony rows a note landed with: three bits per slot from bit 8, row + 1.
inline int packRows(const int rows[kHarmSlots]) {
    int v = 0;
    for (int k = 0; k < kHarmSlots; ++k)
        v |= (std::clamp(rows[k] + 1, 0, 7)) << (8 + 3 * k);
    return v;
}
inline int unpackRow(int flags, int slot) { return ((flags >> (8 + 3 * slot)) & 7) - 1; }

// What the engine publishes for the page: sixteen numbers per line (0 live,
// 1 + slot for a clip), then the echo.
//   [0] 1 sounding and held, 0.5 ringing out, 0 silent
//   [1] where it sings, in absolute scale degrees (octave·degreesPerOctave + degree)
//   [2] its level, [3] its brightness (the finger's height)
//   [4..6] the harmony voices' degrees, [7..9] their levels
//   [10] the bend in degrees, [11] the throw, [12] the sound, [13] following the chord
//   [14] the vibrato's depth now, [15] 1 while an octave up
// kEchoLevel: how loud the echo's repeats are; kVoiceCount: voices sounding.
constexpr int kLineData = 16;
constexpr int kEchoLevel = 160;
constexpr int kVoiceCount = 161;

// Which line of play a command belongs to: 0 live, 1 + slot for a clip.
inline int lineOf(int id) { return (id & (1 << 30)) ? 1 + ((id >> 24) & 7) : 0; }

// --- sounds -------------------------------------------------------------------

struct Sound {
    const char *name = "";
    // Oscillators, mixed: a unison of saws, a pulse, a two-operator FM pair
    // and an octave-up saw layer.
    float saw = 0, pulse = 0, fm = 0, layer = 0;
    int unison = 1;
    float detune = 0;      // cents for the outermost saw at a normal touch
    float spread = 0.5f;   // how wide the unison stands, 0..1
    float pw = 0.5f, pwm = 0; // pulse width, and how far a slow sweep moves it
    float fmRatio = 1, fmIndex = 0, fmDecay = 0.3f, fmFloor = 1; // the index falls to fmIndex·fmFloor
    bool formant = false;  // through three vowel formants, the finger's height choosing the vowel
    // The ladder filter: base cutoff (0..1, about eight octaves above 60 Hz),
    // how far the finger's height moves it, resonance, the envelope's depth
    // and decay, and how far it follows the note.
    float cutoff = 0.9f, cutoffY = 0.1f, res = 0, envAmt = 0, envDecay = 0.3f, keyTrack = 0.3f;
    float drive = 0;       // above 0.3 it runs oversampled
    float a = 0.005f, d = 0.5f, s = 0.8f, r = 0.3f; // amplitude envelope
    float glide = 0.05f;   // legato glide, seconds (scaled by the interval)
    float porta = 0;       // portamento between detached notes too (0: none)
    float scoop = 0, scoopTime = 0.05f; // semitones below the note an attack starts from
    float vibRate = 5.5f, vibDepth = 0.15f, vibDelay = 0.4f; // the vibrato a held note grows
    bool retrig = false;   // legato notes strike the envelopes again (plucks, bells)
    float echo = 0.15f, echoFb = 0.3f, echoBeats = 0.75f; // its own delay: send, feedback, time
    float gain = 1;
};

constexpr int kSounds = 6;

inline const Sound &sound(int i) {
    static const Sound sounds[kSounds] = {
        // The anthem lead: seven saws wide across the field and an octave
        // above for air, a filter that opens with the finger's height.
        {.name = "Anthem", .saw = 1.0f, .layer = 0.22f, .unison = 7, .detune = 24.0f, .spread = 0.95f,
         .cutoff = 0.66f, .cutoffY = 0.30f, .res = 0.12f, .envAmt = 0.26f, .envDecay = 0.55f, .keyTrack = 0.55f,
         .drive = 0.12f, .a = 0.006f, .d = 0.5f, .s = 0.82f, .r = 0.38f, .glide = 0.05f, .vibRate = 5.3f,
         .vibDepth = 0.16f, .vibDelay = 0.42f, .echo = 0.16f, .echoFb = 0.32f, .gain = 1.05f},
        // A sharp pluck: saw and pulse snapped shut by a fast filter
        // envelope, every note struck again, into a dotted-eighth echo.
        {.name = "Pluck", .saw = 0.75f, .pulse = 0.45f, .unison = 3, .detune = 11.0f, .spread = 0.6f,
         .cutoff = 0.22f, .cutoffY = 0.32f, .res = 0.32f, .envAmt = 0.78f, .envDecay = 0.17f, .keyTrack = 0.7f,
         .drive = 0.1f, .a = 0.001f, .d = 0.38f, .s = 0.0f, .r = 0.3f, .glide = 0.025f, .vibDepth = 0.0f,
         .retrig = true, .echo = 0.34f, .echoFb = 0.42f, .gain = 1.3f},
        // An FM bell: a sine struck by a modulator at three and a half
        // times its frequency, the strike fading to a glassy sine.
        {.name = "Bell", .fm = 1.0f, .detune = 7.0f, .spread = 0.7f, .fmRatio = 3.5f, .fmIndex = 2.6f,
         .fmDecay = 0.45f, .fmFloor = 0.18f, .cutoff = 0.95f, .cutoffY = 0.08f, .a = 0.001f, .d = 1.4f, .s = 0.22f,
         .r = 0.9f, .glide = 0.035f, .vibRate = 4.8f, .vibDepth = 0.07f, .vibDelay = 0.6f, .retrig = true,
         .echo = 0.24f, .echoFb = 0.38f, .gain = 0.85f},
        // A chip lead: a narrow pulse with a slow sweep, quick portamento
        // and a fast, game-console vibrato.
        {.name = "Chip", .pulse = 1.0f, .detune = 4.0f, .spread = 0.4f, .pw = 0.25f, .pwm = 0.12f,
         .cutoff = 0.92f, .cutoffY = 0.10f, .a = 0.001f, .d = 0.25f, .s = 0.8f, .r = 0.07f, .glide = 0.03f,
         .porta = 0.04f, .vibRate = 6.4f, .vibDepth = 0.28f, .vibDelay = 0.22f, .echo = 0.2f, .echoBeats = 0.5f,
         .gain = 0.85f},
        // A vocal lead: saws through three vowel formants, the finger's
        // height going oo, oh, ah, eh, ee; notes scoop into pitch like a voice.
        {.name = "Vox", .saw = 0.85f, .pulse = 0.3f, .unison = 3, .detune = 9.0f, .spread = 0.55f,
         .formant = true, .cutoff = 0.85f, .cutoffY = 0.05f, .res = 0.05f, .envAmt = 0.08f, .envDecay = 0.5f,
         .drive = 0.18f, .a = 0.025f, .d = 0.6f, .s = 0.9f, .r = 0.28f, .glide = 0.07f, .scoop = 0.7f,
         .scoopTime = 0.045f, .vibRate = 5.4f, .vibDepth = 0.24f, .vibDelay = 0.32f, .echo = 0.14f, .gain = 0.5f},
        // A screaming saw: driven hard into a resonant filter, always
        // gliding, every attack bent up into the note.
        {.name = "Scream", .saw = 1.0f, .pulse = 0.4f, .unison = 3, .detune = 13.0f, .cutoff = 0.48f,
         .cutoffY = 0.36f, .res = 0.42f, .envAmt = 0.32f, .envDecay = 0.45f, .keyTrack = 0.6f, .drive = 0.85f,
         .a = 0.004f, .d = 0.5f, .s = 0.92f, .r = 0.22f, .glide = 0.08f, .porta = 0.08f, .scoop = 1.4f,
         .scoopTime = 0.055f, .vibRate = 5.9f, .vibDepth = 0.3f, .vibDelay = 0.3f, .echo = 0.2f, .echoFb = 0.36f,
         .gain = 0.9f},
    };
    return sounds[std::clamp(i, 0, kSounds - 1)];
}

// The interval a harmony row stands for, in scale degrees, when the harmony
// follows the scale rather than the chord.
inline float rowDegrees(int row, int perOctave) {
    const float per = float(perOctave);
    const float table[kRows] = {-per, 2.0f, 4.0f, 5.0f, per};
    return table[std::clamp(row, 0, kRows - 1)];
}

}
