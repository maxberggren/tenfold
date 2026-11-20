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
enum ParamKind { PHarmony = 0, PBend, PThrow, POctave, PGlide };
constexpr int kFollow = 1;
// The harmony rows a note landed with: three bits per slot from bit 8, row + 1.
inline int packRows(const int rows[kHarmSlots]) {
    int v = 0;
    for (int k = 0; k < kHarmSlots; ++k)
        v |= (std::clamp(rows[k] + 1, 0, 7)) << (8 + 3 * k);
    return v;
}
inline int unpackRow(int flags, int slot) { return ((flags >> (8 + 3 * slot)) & 7) - 1; }

// Which line of play a command belongs to: 0 live, 1 + slot for a clip.
inline int lineOf(int id) { return (id & (1 << 30)) ? 1 + ((id >> 24) & 7) : 0; }

// --- sounds -------------------------------------------------------------------

struct Sound {
    const char *name;
    // Oscillators: a unison of saws, a pulse, a two-operator FM pair and an
    // octave-up saw layer, mixed.
    float saw, pulse, fm, layer;
    int unison;
    float detune, spread; // cents for the outermost saw at a normal touch; stereo 0..1
    float pw, pwm;        // pulse width and how far a slow sweep moves it
    float fmRatio, fmIndex, fmDecay, fmFloor; // index falls from fmIndex to fmIndex·fmFloor
    bool formant;         // through three vowel formants, the height choosing the vowel
    float cutoff, cutoffY, res, envAmt, envDecay, keyTrack;
    float drive;
    float a, d, s, r;     // amplitude envelope
    float glide, porta;   // legato glide; portamento between detached notes (0: none)
    float scoop, scoopTime; // semitones below the note an attack starts from
    float vibRate, vibDepth, vibDelay;
    bool retrig;          // legato notes restart the envelopes (plucks, bells)
    float echo, echoFb, echoBeats; // its own delay: send, feedback, time in beats
    float gain;
};

constexpr int kSounds = 6;

inline const Sound &sound(int i) {
    static const Sound sounds[kSounds] = {
        // The anthem lead: seven saws wide across the field and an octave
        // above for air, a filter that opens with the finger's height.
        {"Anthem", 1.0f, 0.0f, 0.0f, 0.22f, 7, 24.0f, 0.95f, 0.5f, 0.0f, 1.0f, 0.0f, 0.3f, 1.0f, false,
         0.66f, 0.30f, 0.12f, 0.26f, 0.55f, 0.55f, 0.12f, 0.006f, 0.5f, 0.82f, 0.38f, 0.05f, 0.0f, 0.0f, 0.06f,
         5.3f, 0.16f, 0.42f, false, 0.16f, 0.32f, 0.75f, 1.05f},
        // A sharp pluck: saw and pulse snapped shut by a fast filter
        // envelope, every note struck again, into a dotted-eighth echo.
        {"Pluck", 0.75f, 0.45f, 0.0f, 0.0f, 3, 11.0f, 0.6f, 0.5f, 0.0f, 1.0f, 0.0f, 0.3f, 1.0f, false,
         0.22f, 0.32f, 0.32f, 0.78f, 0.17f, 0.7f, 0.1f, 0.001f, 0.38f, 0.0f, 0.3f, 0.025f, 0.0f, 0.0f, 0.06f,
         5.0f, 0.0f, 1.0f, true, 0.34f, 0.42f, 0.75f, 1.5f},
        // An FM bell: a sine struck by a modulator at three and a half
        // times its frequency, the strike fading to a glassy sine.
        {"Bell", 0.0f, 0.0f, 1.0f, 0.0f, 1, 7.0f, 0.7f, 0.5f, 0.0f, 3.5f, 2.6f, 0.45f, 0.18f, false,
         0.95f, 0.08f, 0.0f, 0.0f, 0.3f, 0.3f, 0.0f, 0.001f, 1.4f, 0.22f, 0.9f, 0.035f, 0.0f, 0.0f, 0.06f,
         4.8f, 0.07f, 0.6f, true, 0.24f, 0.38f, 0.75f, 0.85f},
        // A chip lead: a narrow pulse with a slow sweep, quick portamento
        // and a fast, game-console vibrato.
        {"Chip", 0.0f, 1.0f, 0.0f, 0.0f, 1, 4.0f, 0.4f, 0.25f, 0.12f, 1.0f, 0.0f, 0.3f, 1.0f, false,
         0.92f, 0.10f, 0.0f, 0.0f, 0.3f, 0.3f, 0.0f, 0.001f, 0.25f, 0.8f, 0.07f, 0.03f, 0.04f, 0.0f, 0.06f,
         6.4f, 0.28f, 0.22f, false, 0.2f, 0.3f, 0.5f, 0.85f},
        // A vocal lead: saws through three vowel formants, the finger's
        // height going oo, oh, ah, eh, ee; notes scoop into pitch like a voice.
        {"Vox", 0.85f, 0.3f, 0.0f, 0.0f, 3, 9.0f, 0.55f, 0.5f, 0.0f, 1.0f, 0.0f, 0.3f, 1.0f, true,
         0.85f, 0.05f, 0.05f, 0.08f, 0.5f, 0.3f, 0.18f, 0.025f, 0.6f, 0.9f, 0.28f, 0.07f, 0.0f, 0.7f, 0.045f,
         5.4f, 0.24f, 0.32f, false, 0.14f, 0.3f, 0.75f, 0.5f},
        // A screaming saw: driven hard into a resonant filter, always
        // gliding, every attack bent up into the note.
        {"Scream", 1.0f, 0.4f, 0.0f, 0.0f, 3, 13.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.3f, 1.0f, false,
         0.48f, 0.36f, 0.42f, 0.32f, 0.45f, 0.6f, 0.85f, 0.004f, 0.5f, 0.92f, 0.22f, 0.08f, 0.08f, 1.4f, 0.055f,
         5.9f, 0.3f, 0.3f, false, 0.2f, 0.36f, 0.75f, 1.1f},
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
