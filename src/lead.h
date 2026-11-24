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
// The lines of play: the live player, and one per clip slot and layer (a
// loop with something layered on it plays each layer as a line of its own).
// The page is shown kShownLines of them: the live one, each slot's first
// layer, then the layers above it of the slot that is sounding.
constexpr int kLines = 1 + kClipSlots * kClipLayers;
constexpr int kShownLines = 1 + kClipSlots + (kClipLayers - 1);
// Where the page finds a playing loop's layer: 1 + slot for its first,
// after the slots for the ones layered on it.
inline int shownLine(int slot, int layer) { return layer == 0 ? 1 + slot : kClipSlots + layer; }
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
//   i[3] flags (On): kFollow, the gate, the tone, and the harmony rows held when it landed
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
//   PTone     i[1] tone step (see kToneSteps)
enum ParamKind { PHarmony = 0, PBend, PThrow, POctave, PGlide, PGate, PTone };
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
// The tone (- and =): steps darker or brighter than the sound's own, each
// moving its filter about a third of an octave. The On flags carry it in
// bits 3–6, 0 meaning the sound's own, so a clip keeps the tone it was
// played with.
constexpr int kToneSteps = 4;
constexpr float kToneOctaves = 0.3f;
constexpr int kToneShift = 3;
inline int packTone(int step) {
    step = std::clamp(step, -kToneSteps, kToneSteps);
    return (step == 0 ? 0 : step + 8) << kToneShift;
}
inline int unpackTone(int flags) {
    const int v = (flags >> kToneShift) & 15;
    return v == 0 ? 0 : std::clamp(v - 8, -kToneSteps, kToneSteps);
}
// The harmony rows a note landed with: three bits per slot from bit 8, row + 1.
inline int packRows(const int rows[kHarmSlots]) {
    int v = 0;
    for (int k = 0; k < kHarmSlots; ++k)
        v |= (std::clamp(rows[k] + 1, 0, 7)) << (8 + 3 * k);
    return v;
}
inline int unpackRow(int flags, int slot) { return ((flags >> (8 + 3 * slot)) & 7) - 1; }

// What the engine publishes for the page: sixteen numbers per shown line (see
// shownLine), then the echo.
//   [0] 1 sounding and held, 0.5 ringing out, 0 silent
//   [1] where it sings, in absolute scale degrees (octave·degreesPerOctave + degree)
//   [2] its level, [3] its brightness (the finger's height)
//   [4..6] the harmony voices' degrees, [7..9] their levels
//   [10] the bend in degrees, [11] the throw, [12] the sound, [13] following the chord
//   [14] the vibrato's depth now, [15] 1 while an octave up
// kEchoLevel: how loud the echo's repeats are; kVoiceCount: voices sounding.
constexpr int kLineData = 16;
constexpr int kEchoLevel = 240;
constexpr int kVoiceCount = 241;
static_assert(kLineData * kShownLines <= kEchoLevel && kVoiceCount < kPartData);

// Which line of play a command belongs to: 0 live, 1 + slot (+ kClipSlots
// per layer above the first) for a clip.
inline int lineOf(int id) { return isReplay(id) ? 1 + replaySlot(id) + kClipSlots * replayLayer(id) : 0; }

// --- sounds -------------------------------------------------------------------

struct Sound {
    const char *name = "";
    // The music it is for, after its name in the status line, so a
    // newcomer knows what to reach for it for.
    const char *style = "";
    // Oscillators, mixed: a unison of saws, a pulse, a two-operator FM pair,
    // an octave-up saw layer and an octave-down square (a mono synth's
    // third oscillator, switched down to 16'; it thins out up the neck so
    // a high note still sounds where it is played, and a flatter finger
    // brings it up).
    float saw = 0, pulse = 0, fm = 0, layer = 0, sub = 0;
    int unison = 1;
    float detune = 0;      // cents for the outermost saw at a normal touch
    float drift = 0;       // cents each of an old synth's oscillators wanders on its own, slowly
    float spread = 0.5f;   // how wide the unison stands, 0..1
    float pw = 0.5f, pwm = 0, pwmRate = 0.37f; // pulse width, how far a sweep moves it, how fast
    float pulse2 = 0;      // a second pulse a few cents off (a flat finger brings it up anyway)
    float fmRatio = 1, fmIndex = 0, fmDecay = 0.3f, fmFloor = 1; // the index falls to fmIndex·fmFloor
    float fmLow = 0;       // how much richer low notes are: the index grows by this much an octave under C5
    bool formant = false;  // through three vowel formants, the finger's height choosing the vowel
    // Breath: noise in a band over the note, rocked by the tone itself so
    // it blows through it rather than hissing beside it, with a puff on
    // every tongued attack.
    float breath = 0, chiff = 0;
    // The ladder filter: base cutoff (0..1, about eight octaves above 60 Hz),
    // how far the finger's height moves it, resonance, the envelope's depth
    // and decay, and how far it follows the note.
    float cutoff = 0.9f, cutoffY = 0.1f, res = 0, envAmt = 0, envDecay = 0.3f, keyTrack = 0.3f;
    float envPress = 0;    // how much the envelope waits on pressure (1: a light touch none of it, a hard one all)
    float drive = 0;       // above 0.3 it runs oversampled
    float a = 0.005f, d = 0.5f, s = 0.8f, r = 0.3f; // amplitude envelope
    float aHard = 0;       // the attack under a hard press, if it is quicker (0: always a)
    float glide = 0.05f;   // legato glide, seconds (scaled by the interval)
    float porta = 0;       // portamento between detached notes too (0: none)
    float scoop = 0, scoopTime = 0.05f; // semitones below the note an attack starts from
    float vibRate = 5.5f, vibDepth = 0.15f, vibDelay = 0.4f; // the vibrato a held note grows
    float vibAmp = 0;      // how far the vibrato swells the level too (a wind player's is mostly breath)
    bool retrig = false;   // legato notes strike the envelopes again (plucks, bells)
    bool swoop = false;    // keeps a slow slur even across a step (trills smear on purpose)
    float echo = 0.15f, echoFb = 0.3f, echoBeats = 0.75f; // its own delay: send, feedback, time
    float gain = 1;
    // What makes the tone, when it is not the oscillators above (whose
    // levels are then 0). Each is a model of an instrument the oscillators
    // can't imitate; the filter, the envelopes and the echo above still
    // shape it.
    //   String: a plucked string in a delay line (Karplus–Strong), its
    //           bridge a curved one that the string buzzes against (a
    //           sitar's jawari): the buzz is a formant that slides down
    //           through the harmonics as the swing dies. Under it, a taraf
    //           of sympathetic strings tuned to the scale, and the chikari,
    //           high drone strings a second finger strikes.
    //   Pan:    a struck steel pan: a few tuned modes, each a pair of
    //           sines a hair apart; a held note is rolled in the studio's
    //           time, every stroke striking the modes again, and a firm
    //           stroke pings.
    //   Sync:   two saws hard-synced to unheard masters, the note's and an
    //           octave above it, each swept down from high on every attack
    //           to a ratio that keeps tearing, and a resonance riding the
    //           slave's pitch that sings the formant; pressure crossfades
    //           into FM of the slave by its master. Run at four times the
    //           rate.
    //   Bow:    a bowed string: two delay lines either side of the bow and
    //           a friction junction where the hair sticks and slips (the
    //           string's own Helmholtz motion), at twice the rate, into a
    //           small snakeskin body that rolls off over the top; a held
    //           bow swells and tapers in an arc, and turns at its end.
    //   Reed:   free reeds in an accordion's registers, chosen by the
    //           finger's height: one reed (clarinet), a pair an octave
    //           apart (bandoneon), or three, the outer two a fixed number
    //           of hertz either side (musette); the bellows in the attack,
    //           turning by the phrase.
    //   Gong:   a gong as physics (gong.h): a bronze sheet's modes and the
    //           membrane stress that couples them, struck by a padded
    //           mallet; every voice of the line its own gong, tuned to its
    //           note. The kind (bossed, opera, tam-tam) is the sound's.
    //   TrombaMarina: a tromba marina as physics (trombamarina.h): one long
    //           gut string bowed above a thumb that only touches its nodes,
    //           so it sounds the harmonic the note is, on a bridge whose
    //           free foot knocks on the soundboard every period (the
    //           trumpet's buzz), into a long tapered box. The note picks the
    //           harmonic of a string tuned two octaves under the neck's root.
    //   Dax:    a daxophone as physics (daxophone.h): a wooden tongue
    //           clamped in a block, bowed on its end, a dax pressed on it
    //           sliding to set its sounding length, heard by contact
    //           microphones in the block; every voice its own tongue.
    //   Guitar: an overdriven electric guitar as physics (guitar.h): a
    //           steel string picked, read by a humbucker, through a valve
    //           amp and a 4x12; every voice of the line its own guitar.
    enum Model { Osc = 0, String, Pan, Sync, Bow, Reed, Gong, TrombaMarina, Dax, Guitar };
    int model = Osc;
    float jawari = 0;      // String: how hard the string buzzes on its bridge
    float sympathy = 0;    // String: how loud the taraf rings
    float chikari = 0;     // String: how loud the chikari rings when struck
    float sustainS = 3.0f; // String, Pan: seconds the fundamental takes to fall 60 dB
    float roll = 0;        // Pan: how strongly a held note is rolled
    float settle = 0;      // Pan: semitones a struck note starts sharp of its pitch (a low one more)
    float syncFrom = 1, syncTo = 1, syncTime = 0.3f; // Sync: the slave's ratio, from the attack to where it settles, and how fast
    float syncY = 0;       // Sync: how far the finger's height moves the ratio up
    float sync2 = 0;       // Sync: the second synced voice, an octave up
    float syncFm = 0;      // Sync: how far a hard press crossfades into FM of the slave
    float syncLfo = 0;     // Sync: how far a slow wander moves the held ratio
    float scratch = 0;     // Bow: the rosin's noise
    float beatHz = 0;      // Reed: how fast the musette's outer reeds beat against the middle one
    int gong = 0;          // Gong: which kind (gong::Kind: bossed, opera, tam-tam)
    float gongOct = 0;     // Gong: octaves the gong sounds from the note (a tam-tam's thud an octave under it)
    float hand = 1.0f;     // Gong: seconds a hand laid on it takes to still it once the finger lifts
    float rollHz = 13.0f;  // Pan, Gong: the strokes a second a held note is rolled at, near enough
    float feedback = 0;    // Guitar: how readily a held note feeds back through the amp (Gong's hand: the fretting hand's damping)
};

// The accordion's registers, by the finger's height: low on the pad the
// clarinet (one reed, dry, tango's), in the middle the bandoneon (a reed and
// one an octave above it), high the musette (three reeds beating, cumbia's and
// the bal's). Each is a weight, so a finger sliding up pulls the next stop
// in over a short stretch instead of clicking it.
constexpr int kRegisters = 3;
inline void registerWeights(float y, float w[kRegisters]) {
    auto smooth = [](float a, float b, float x) {
        const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float up1 = smooth(0.30f, 0.40f, y), up2 = smooth(0.62f, 0.72f, y);
    w[0] = 1.0f - up1;
    w[2] = up2;
    w[1] = up1 - up2;
}
inline const char *registerName(float y) {
    float w[kRegisters];
    registerWeights(y, w);
    static const char *names[kRegisters] = {"clarinet", "bandoneon", "musette"};
    return names[w[2] >= 0.5f ? 2 : w[0] >= 0.5f ? 0 : 1];
}

// The chikari: a second finger tapped this high on the pad, while a Sitar
// note is held, strikes the high drone strings instead of a hammer-on.
constexpr float kChikariY = 0.8f;

constexpr int kSounds = 21;

inline const Sound &sound(int i) {
    static const Sound sounds[kSounds] = {
        // The anthem lead: seven saws wide across the field and an octave
        // above for air, a filter that opens with the finger's height.
        {.name = "Anthem", .style = "trance", .saw = 1.0f, .layer = 0.22f, .unison = 7, .detune = 24.0f, .spread = 0.95f,
         .cutoff = 0.66f, .cutoffY = 0.30f, .res = 0.12f, .envAmt = 0.26f, .envDecay = 0.55f, .keyTrack = 0.55f,
         .drive = 0.12f, .a = 0.006f, .d = 0.5f, .s = 0.82f, .r = 0.38f, .glide = 0.05f, .vibRate = 5.3f,
         .vibDepth = 0.16f, .vibDelay = 0.42f, .echo = 0.16f, .echoFb = 0.32f, .gain = 1.05f},
        // A sharp pluck: saw and pulse snapped shut by a fast filter
        // envelope, every note struck again, into a dotted-eighth echo.
        {.name = "Pluck", .style = "house", .saw = 0.75f, .pulse = 0.45f, .unison = 3, .detune = 11.0f, .spread = 0.6f,
         .cutoff = 0.22f, .cutoffY = 0.32f, .res = 0.32f, .envAmt = 0.78f, .envDecay = 0.17f, .keyTrack = 0.7f,
         .drive = 0.1f, .a = 0.001f, .d = 0.38f, .s = 0.0f, .r = 0.3f, .glide = 0.025f, .vibDepth = 0.0f,
         .retrig = true, .echo = 0.34f, .echoFb = 0.42f, .gain = 1.73f},
        // An FM bell: a sine struck by a modulator at three times its
        // frequency (every partial on a harmonic, so it sits in the key and
        // its strike is heard at its own pitch), fading to a glassy sine.
        {.name = "Bell", .style = "chillout", .fm = 1.0f, .detune = 7.0f, .spread = 0.7f, .fmRatio = 3.0f, .fmIndex = 2.3f,
         .fmDecay = 0.45f, .fmFloor = 0.18f, .cutoff = 0.95f, .cutoffY = 0.08f, .a = 0.001f, .d = 1.4f, .s = 0.22f,
         .r = 0.9f, .glide = 0.035f, .vibRate = 4.8f, .vibDepth = 0.07f, .vibDelay = 0.6f, .retrig = true,
         .echo = 0.24f, .echoFb = 0.38f, .gain = 0.85f},
        // A chip lead: a narrow pulse with a slow sweep, quick portamento
        // and a fast, game-console vibrato.
        {.name = "Chip", .style = "chiptune", .pulse = 1.0f, .detune = 4.0f, .spread = 0.4f, .pw = 0.25f, .pwm = 0.12f,
         .cutoff = 0.92f, .cutoffY = 0.10f, .a = 0.001f, .d = 0.25f, .s = 0.8f, .r = 0.07f, .glide = 0.03f,
         .porta = 0.04f, .vibRate = 6.4f, .vibDepth = 0.28f, .vibDelay = 0.22f, .echo = 0.2f, .echoBeats = 0.5f,
         .gain = 0.92f},
        // A vocal lead: saws through three vowel formants, the finger's
        // height going oo, oh, ah, eh, ee; notes scoop into pitch like a voice.
        {.name = "Vox", .style = "eurodance", .saw = 0.85f, .pulse = 0.3f, .unison = 3, .detune = 9.0f, .spread = 0.55f,
         .formant = true, .cutoff = 0.85f, .cutoffY = 0.05f, .res = 0.05f, .envAmt = 0.08f, .envDecay = 0.5f,
         .drive = 0.18f, .a = 0.025f, .d = 0.6f, .s = 0.9f, .r = 0.28f, .glide = 0.07f, .scoop = 0.7f,
         .scoopTime = 0.045f, .vibRate = 5.4f, .vibDepth = 0.24f, .vibDelay = 0.32f, .echo = 0.14f, .gain = 0.76f},
        // A screaming saw: driven hard into a resonant filter, always
        // gliding, every attack bent up into the note.
        {.name = "Scream", .style = "electro", .saw = 1.0f, .pulse = 0.4f, .unison = 3, .detune = 13.0f, .cutoff = 0.48f,
         .cutoffY = 0.36f, .res = 0.42f, .envAmt = 0.32f, .envDecay = 0.45f, .keyTrack = 0.6f, .drive = 0.85f,
         .a = 0.004f, .d = 0.5f, .s = 0.92f, .r = 0.22f, .glide = 0.08f, .porta = 0.08f, .scoop = 1.4f,
         .scoopTime = 0.055f, .vibRate = 5.9f, .vibDepth = 0.3f, .vibDelay = 0.3f, .swoop = true, .echo = 0.2f, .echoFb = 0.36f,
         .gain = 1.17f},
        // The hoover: saws and a fast-swept pulse in a wide, wobbling
        // chorus, driven, bending up into every note from well below.
        {.name = "Hoover", .style = "rave", .saw = 0.8f, .pulse = 0.7f, .unison = 5, .detune = 30.0f, .spread = 0.9f,
         .pwm = 0.38f, .pwmRate = 4.6f, .cutoff = 0.62f, .cutoffY = 0.3f, .res = 0.2f, .envAmt = 0.2f,
         .envDecay = 0.4f, .keyTrack = 0.5f, .drive = 0.4f, .a = 0.01f, .d = 0.6f, .s = 0.9f, .r = 0.3f,
         .glide = 0.1f, .porta = 0.1f, .scoop = 2.5f, .scoopTime = 0.09f, .vibDepth = 0.0f, .swoop = true,
         .echo = 0.15f, .gain = 1.1f},
        // The big-room lead: two squares a few cents apart and an octave
        // above, bright and short, made to cut through a festival mix.
        {.name = "Big room", .style = "festival", .pulse = 1.0f, .layer = 0.35f, .detune = 8.0f, .spread = 0.8f, .pulse2 = 0.9f,
         .cutoff = 0.7f, .cutoffY = 0.25f, .res = 0.1f, .envAmt = 0.35f, .envDecay = 0.25f, .keyTrack = 0.5f,
         .drive = 0.15f, .a = 0.002f, .d = 0.3f, .s = 0.55f, .r = 0.15f, .glide = 0.03f, .vibDepth = 0.06f,
         .echo = 0.22f, .gain = 1.09f},
        // A vintage mono synth, the solo lead of G-funk, prog and
        // synthwave: two saws a few cents apart over a square an octave
        // down, each wandering in tune on its own like an old synth's,
        // into a resonant ladder that follows the note. Touch decides its
        // character: a light finger low on the pad is the smooth, near
        // sine whistle of a G-funk lead, a hard press quacks the filter
        // open on every attack. Close to mono, a hint of drive, and it
        // always glides.
        {.name = "Mini", .style = "G-funk", .saw = 0.85f, .sub = 0.1f, .unison = 2, .detune = 7.0f, .drift = 6.0f, .spread = 0.15f,
         .cutoff = 0.6f, .cutoffY = 0.2f, .res = 0.36f, .envAmt = 0.55f, .envDecay = 0.24f, .keyTrack = 0.9f,
         .envPress = 1.0f, .drive = 0.25f, .a = 0.003f, .d = 0.6f, .s = 0.85f, .r = 0.16f, .glide = 0.09f,
         .porta = 0.08f, .vibRate = 5.6f, .vibDepth = 0.17f, .vibDelay = 0.5f, .echo = 0.12f, .echoFb = 0.3f,
         .gain = 1.0f},
        // A breathy wooden flute, for ambient, lo-fi, world and dub: a
        // near sine whose few harmonics fill in as the note is blown, air
        // through it and a puff on each tongued note, a soft attack, and
        // a slow vibrato that comes late and is mostly breath. Low notes
        // are reedier and airier, as a real flute's are; a hard press
        // tongues a quicker attack for fast runs. One pipe, in the middle;
        // a flatter finger blows more air through it.
        {.name = "Flute", .style = "lo-fi", .fm = 1.0f, .fmRatio = 1.0f, .fmIndex = 0.45f, .fmDecay = 0.15f, .fmFloor = 1.4f,
         .fmLow = 0.5f, .breath = 0.3f, .chiff = 1.4f, .cutoff = 0.7f, .cutoffY = 0.22f, .envAmt = 0.06f,
         .envDecay = 0.2f, .keyTrack = 0.6f, .a = 0.07f, .d = 0.5f, .s = 0.88f, .r = 0.2f, .aHard = 0.018f, .glide = 0.05f,
         .scoop = 0.3f, .scoopTime = 0.05f, .vibRate = 4.9f, .vibDepth = 0.1f, .vibDelay = 0.38f, .vibAmp = 0.16f,
         .echo = 0.18f, .echoFb = 0.38f, .gain = 0.92f},
        // A sitar, for raga, raga rock and psychedelia: a plucked wire
        // string buzzing on its curved bridge (the jawari), whose buzz is
        // a bright formant on the stroke that sweeps down through the
        // harmonics, a stripe across them, over the first half second or
        // so and on as the swing dies (the sitar's "jiiing"). Under it a
        // taraf of eleven sympathetic strings tuned to the scale shimmers
        // with whatever is played and takes up what the string still
        // swings once it is let go, ringing on after it; and a second
        // finger tapped high on the pad strikes the chikari, the high
        // drone strings, as hard as the melody string still swings. The
        // wire rings long, as a sitar's does, so an alap's held note lives
        // through its meend: legato, a hammer-on or a slide, bends the
        // ringing string slowly to the next note. A flatter finger buzzes
        // more, a higher one plucks brighter.
        {.name = "Sitar", .style = "raga", .cutoff = 0.98f, .cutoffY = 0.06f, .keyTrack = 0.2f, .a = 0.0005f,
         .d = 1.0f, .s = 1.0f, .r = 0.22f, .glide = 0.12f, .vibRate = 5.0f, .vibDepth = 0.06f, .vibDelay = 0.7f,
         .swoop = true, .echo = 0.12f, .echoFb = 0.3f, .gain = 0.74f, .model = Sound::String, .jawari = 1.0f,
         .sympathy = 0.3f, .chikari = 0.22f, .sustainS = 16.0f},
        // A steel pan, for calypso, soca and island pop: tuned modes
        // struck with a rubber-tipped stick, the octave blooming just
        // after the fundamental, every mode a pair of sines a hair apart
        // that shimmer against each other, each struck note settling down
        // onto its pitch. A note of an eighth is one stroke; a longer one
        // is rolled from the eighth on, as panists sustain a note, its
        // strokes on the beat's subdivision nearest thirteen a second, the
        // roll harder the harder the press, swelling into it over a few
        // tenths of a second and leaning on the strokes that land on the
        // studio's beat. Each stroke sets the note's neighbours on the pan
        // ringing, its octave and the fifth over it, a little under it and
        // a few cents off its partials: the halo of a real pan. A firm
        // stroke high on the note pings off the stick, bright enough to
        // cut through horns. Low notes are a cello pan's, their octave
        // strong and defined.
        {.name = "Steel pan", .style = "calypso", .cutoff = 0.95f, .cutoffY = 0.12f, .keyTrack = 0.2f, .a = 0.0008f,
         .d = 1.0f, .s = 1.0f, .r = 0.5f, .glide = 0.03f, .vibDepth = 0.0f, .retrig = true, .echo = 0.2f,
         .echoFb = 0.34f, .gain = 1.17f, .model = Sound::Pan, .sustainS = 2.2f, .roll = 1.0f, .settle = 0.14f},
        // The sync lead of Italo disco and synth-pop, two oscillators deep:
        // a saw hard-synced to the note and a second synced an octave
        // above, each swept down from high on every attack ("yow") to a
        // ratio where it keeps tearing, wandering slowly and moved by the
        // vibrato and the finger's wiggle, and a resonance riding the
        // slave's pitch sings the formant it sweeps (a "yow" as hard as the
        // note is struck), with a second formant over it whose vowel the
        // finger's height moves from "ow" to "ee". Height also holds the
        // sweep up; a hard press crossfades into FM of the slave by its
        // master, a metallic snarl none of the other leads have. Undriven,
        // its oscillators run at four times the rate and its ladder at
        // twice, so neither the snarl under a vibrato nor the ladder's
        // soft clip folds anything back.
        {.name = "Sync", .style = "Italo disco", .cutoff = 0.88f, .cutoffY = 0.1f, .res = 0.12f, .envAmt = 0.16f,
         .envDecay = 0.35f, .keyTrack = 0.5f, .drive = 0.0f, .a = 0.002f, .d = 0.5f, .s = 0.85f, .r = 0.2f,
         .glide = 0.04f, .vibRate = 5.5f, .vibDepth = 0.1f, .vibDelay = 0.5f, .echo = 0.18f, .echoFb = 0.4f,
         .gain = 0.44f, .model = Sound::Sync, .syncFrom = 6.0f, .syncTo = 2.4f, .syncTime = 0.16f, .syncY = 2.0f,
         .sync2 = 0.5f, .syncFm = 0.7f, .syncLfo = 1.0f},
        // An erhu, the two-string fiddle of Chinese opera, film and
        // C-pop, as a bowed string: the hair sticks to the string and
        // slips off it every period, so the tone is the string's own
        // Helmholtz motion, and the bow decides it: a light press is a
        // breathy flautando that whistles or breaks, a hard one grinds
        // toward sul ponticello; higher on the pad bows nearer the bridge.
        // The rosin rasps in time with each slip, the python-skin body is
        // nasal around a kilohertz, and a note played again soon after
        // changes the bow, with the catch of the hair turning round. A held
        // bow sings in an arc, swelling into the middle of the stroke and
        // tapering to its end, where it turns by itself. The small body
        // speaks little over seven kilohertz, so a hard press grinds in its
        // middle. It slides into notes, and across a leap of more than a
        // tone the finger travels the string (a huayin), a little past the
        // note and back, the line lightening as it goes. Its vibrato, from
        // a hand pressing a string that has no fingerboard, is deep, and a
        // player's: late and narrow at first, then wider and quicker into
        // the bow's arc (five cycles a second to six and a half), no two
        // cycles alike.
        {.name = "Erhu", .style = "C-pop", .cutoff = 0.9f, .cutoffY = 0.1f, .keyTrack = 0.4f, .a = 0.06f, .d = 0.6f,
         .s = 0.9f, .r = 0.12f, .aHard = 0.025f, .glide = 0.12f, .porta = 0.07f, .scoop = 1.0f, .scoopTime = 0.07f,
         .vibRate = 5.0f, .vibDepth = 0.5f, .vibDelay = 0.28f, .vibAmp = 0.05f, .swoop = true, .echo = 0.14f,
         .echoFb = 0.34f, .gain = 0.36f, .model = Sound::Bow, .scratch = 1.0f},
        // An accordion, for cumbia, vallenato, tango and the French bal,
        // with its registers under the finger's height: low the clarinet,
        // one dry reed (tango); the middle the bandoneon, a reed and one an
        // octave above it; high the musette, three reeds, the outer two a
        // five to seven hertz either side of the middle one, a wet tuning,
        // so the note shimmers at the same speed all the way up (cumbia,
        // the bal). Every note speaks with a key's click and the reed's sag
        // onto pitch. The bellows turn by the phrase, as a player's do:
        // once they have drawn their air, or unheard after a rest, never on
        // every note of a run, and the two ways are voiced apart, as no two
        // reed sets are: the pull's reeds edgier and its chamber's bite
        // higher, the push's rounder. Their pressure is its swell. No
        // vibrato but the reeds' own.
        {.name = "Accordion", .style = "cumbia, tango", .cutoff = 0.8f, .cutoffY = 0.06f, .res = 0.04f,
         .keyTrack = 0.3f, .a = 0.025f, .d = 0.4f, .s = 0.92f, .r = 0.08f, .aHard = 0.01f, .glide = 0.02f,
         .scoop = -0.16f, .scoopTime = 0.05f, .vibDepth = 0.0f, .echo = 0.1f, .echoFb = 0.25f, .gain = 2.2f,
         .model = Sound::Reed, .beatHz = 6.0f},
        // A bossed gong, the kempul and bonang of the gamelan and the
        // kulintang's row of them: a thick bronze face with a raised boss,
        // which the smith has tuned so its two ringing axisymmetric modes
        // stand an octave apart, less a few hertz. Struck on the boss with
        // a padded mallet it hums its note for seconds; the boss's own
        // stretching sounds the note's octave too, which beats against the
        // smith's octave a few times a second (the ombak), and its twelfth
        // beats with the sum of the two. Higher on the pad strikes off the
        // boss, onto the face: a clang with less of the note. Lifting the
        // finger lays a hand on it, as a bonang player stills each kettle.
        {.name = "Gong", .style = "gamelan", .cutoff = 0.99f, .cutoffY = 0.0f, .keyTrack = 0.0f, .a = 0.0005f,
         .d = 1.0f, .s = 1.0f, .r = 4.0f, .glide = 0.03f, .vibDepth = 0.0f, .retrig = true, .echo = 0.12f,
         .echoFb = 0.3f, .gain = 1.0f, .model = Sound::Gong, .gong = 0, .hand = 1.2f},
        // A Chinese opera gong: a thin bronze disc in a stiff conical
        // flange, struck hard with a padded stick. Its pitch moves as it
        // rings, by how far the sheet swings against its thickness: the
        // big gongs low on the neck are flat in the middle, stretch as
        // they swing and start up to a minor third sharp, gliding down as
        // they die (the daluo); the small ones high on the neck are domed
        // by a millimetre, give as they swing and start flat, gliding up
        // (the xiaoluo, the opera's "jjang"). The harder it is struck the
        // further it glides; a light touch is nearly pure. Higher on the pad
        // strikes off the middle, brighter.
        {.name = "Opera gong", .style = "Chinese opera", .cutoff = 0.99f, .cutoffY = 0.0f, .keyTrack = 0.0f,
         .a = 0.0005f, .d = 1.0f, .s = 1.0f, .r = 4.0f, .glide = 0.03f, .vibDepth = 0.0f, .retrig = true,
         .echo = 0.1f, .echoFb = 0.28f, .gain = 1.0f, .model = Sound::Gong, .gong = 1, .hand = 0.9f},
        // A tam-tam, the orchestra's great flat gong: a thin hammered
        // sheet struck in its middle with a heavy soft beater, sounding an
        // octave under the note. Struck softly it is a deep thud; struck
        // hard, the sheet swings further than its thickness and the
        // stretching passes the energy up out of the thud into ever higher
        // modes, so over a second the sound blossoms into a shimmer that
        // outlasts the thud by seconds. A note held past a beat is rolled
        // with soft beaters, swelling as the press grows: the tam-tam's
        // crescendo. Higher on the pad strikes further out, less thud and
        // more shimmer. Lifting the finger lets it ring on, ten seconds and
        // more, a hand only resting on it.
        {.name = "Tam-tam", .style = "film score", .cutoff = 0.99f, .cutoffY = 0.0f, .keyTrack = 0.0f, .a = 0.0005f,
         .d = 1.0f, .s = 1.0f, .r = 4.0f, .glide = 0.05f, .vibDepth = 0.0f, .retrig = true, .echo = 0.08f,
         .echoFb = 0.25f, .gain = 1.0f, .model = Sound::Gong, .roll = 1.0f, .gong = 2, .gongOct = -1.0f, .hand = 12.0f,
         .rollHz = 9.0f},
        // A tromba marina, the "trumpet marine" of baroque and convent
        // music: one long gut string, bowed near the top while the thumb
        // only touches it at a node, so every note is a natural harmonic
        // of the open string, tuned two octaves under the key's root. Its
        // bridge has one foot free a hair over the soundboard, and every
        // period it knocks there: bowed firmly, the string brays like a
        // trumpet; lightly, the knocking stops and it is a soft, nasal
        // flageolet. Pressure is the bow's weight (and its speed, as a
        // player leans in): past the bow's limit it scratches. Higher on
        // the pad bows nearer the nut, brighter and needing more weight.
        // Moving to another note slides the thumb to another node. No
        // vibrato: a harmonic's pitch is the string's.
        {.name = "Tromba marina", .style = "baroque", .cutoff = 0.99f, .cutoffY = 0.0f, .keyTrack = 0.0f, .a = 0.08f,
         .d = 0.6f, .s = 1.0f, .r = 0.15f, .aHard = 0.04f, .glide = 0.05f, .vibDepth = 0.0f, .echo = 0.1f,
         .echoFb = 0.28f, .gain = 1.0f, .model = Sound::TrombaMarina},
        // A daxophone, Hans Reichel's: a thin maple tongue clamped in a
        // block, bowed on its end with a double-bass bow, a wooden dax
        // pressed on it and slid along to set how much of it sounds, heard
        // through contact microphones in the block. The finger is the dax:
        // it glides wherever the finger goes, a voice's glide, snapping to
        // the key only when the line follows the chord. The press is the bow:
        // the lightest skates and barely speaks, an ordinary one sings,
        // harder is rougher, the hardest breaks the note: rough, or falling
        // to a clean tone a few semitones under it.
        // Higher on the pad bows on the very end, roughest; lower, further
        // in along the edge, rounder; a flatter finger presses the dax
        // lighter. A held
        // note's bow turns at the end of its stroke; let go, the tongue
        // rings out briefly on the dax.
        {.name = "Daxophone", .style = "free improv", .cutoff = 0.99f, .cutoffY = 0.0f, .keyTrack = 0.0f, .a = 0.01f,
         .d = 1.0f, .s = 1.0f, .r = 0.3f, .glide = 0.09f, .porta = 0.04f, .vibRate = 5.2f, .vibDepth = 0.22f,
         .vibDelay = 0.35f, .swoop = true, .echo = 0.12f, .echoFb = 0.3f, .gain = 1.0f, .model = Sound::Dax},
        // An overdriven electric guitar, the rock lead: a Les Paul's steel
        // string, picked, through its bridge humbucker into a cranked
        // Marshall and a 4x12. The pick's stroke is the press as the finger
        // lands; higher on the pad picks nearer the bridge, brighter and
        // thinner. A second finger hammers on (no new pick, the string
        // ringing on at its new fret), lifting it pulls off; a slide steps
        // fret by fret; the twist and the bend wheel bend the string up,
        // and a held note's vibrato is a bend's, above the note. A flat
        // finger is a palm on the strings by the bridge: a muted chug.
        // Held, the amp's sustain carries it, and pressing harder stands
        // nearer the amp, until it feeds back. Lifting the finger mutes the
        // string with the fretting hand.
        {.name = "Guitar", .style = "rock", .cutoff = 0.99f, .cutoffY = 0.0f, .keyTrack = 0.0f, .a = 0.0005f,
         .d = 1.0f, .s = 1.0f, .r = 0.3f, .glide = 0.004f, .vibRate = 5.8f, .vibDepth = 0.35f, .vibDelay = 0.6f,
         .echo = 0.16f, .echoFb = 0.32f, .echoBeats = 0.75f, .gain = 1.0f, .model = Sound::Guitar, .hand = 0.12f,
         .feedback = 1.0f},
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
