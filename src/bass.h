#pragma once

// The bass page's sound, and the words its controls and engine share.
//
// One mono bass, built the way a producer builds one in a session: a clean
// sine sub under a crossover that follows the note, and above it the part
// with character (saws, squares, FM, a ladder and a vowel filter, drive
// that is oversampled and level-matched), so the weight under 100 Hz is
// always one sine in phase with the note, however hard the top is pushed.
// Ten sounds share that one voice and differ only in their recipe: a
// clean sub, a rolling pluck, a reese, an acid line, a wobble, a growl, an
// 808, a bouncing house bass, a Minimoog-style ladder bass and an upright,
// whose top is a plucked string (Karplus–Strong) in a wooden body.
//
// There are two lanes, each a mono synth with its own note stack: the
// player's fingers and keys, and the clip that is looping. While the
// player holds a note the loop ducks to half, so the hand leads but what
// Space would layer onto is still heard; the shapers the player holds
// (octave, roll, wobble, grit) work on whatever is sounding, the loop
// included.

#include <array>
#include <cmath>

#include "electro.h"
#include "part.h"

namespace tf::bass {

// --- the vocabulary -----------------------------------------------------------

enum Sound : int { Sub, Pluck, Reese, Acid, Wobble, Growl, Boom, Bounce, Moog, Upright, SoundCount };

// Commands, besides the studio's own:
//   On     id; i[0] degree, i[1] octave, i[2] NoteFlags, i[3] Sound;
//          f[0] velocity, f[1] tone 0..1, f[2] morph 0..1, f[3] fat 0..1, f[4] glide 0..1,
//          f[5] the wobble rate a sound that wobbles on its own uses (the WOB finger's last)
//   Move   id; i[0] degree, i[1] octave; f[0] pressure, f[1] tone, f[2] morph, f[3] fat
//   Off    id
//   Param  i[0] Shaper; f[0] amount (0 = off), f[1] its second axis (ShapeBend: f[0]
//          semitones, −24..12, f[1] 1 once it has settled; ShapeFilter: f[0] cutoff
//          offset −0.7..0.7, f[1] morph offset −1..1);
//          or i[0] ResetSlot, f[0] the slot (never recorded)
//   Strike id: the note this id plays is struck again with an accent (the click)
constexpr uint16_t kStrike = Command::User;
enum NoteFlag : int { Follow = 1, Accent = 2, Square = 4 }; // Square: the saw becomes a square
enum Shaper : int { ShapeOct, ShapeRoll, ShapeWob, ShapeGrit, ShapeBend, ShapeFilter, ShaperCount, ResetSlot = 15 };

// The roll's rhythms, one beat each: a held note is struck on the lit steps.
// The kick owns the downbeat, so most of them leave it free.
struct Pattern {
    const char *name;
    const char *steps; // 'x' struck, '-' rest
};
constexpr Pattern kPatterns[] = {
    {"OFFBEAT", "--x-"}, {"SKIP", "-x-x"}, {"GALLOP", "x-xx"}, {"PSY", "-xxx"},
    {"DRIVE", "xxxx"},   {"TRIPLET", "-xx"}, {"STUTTER", "xxxxxxxx"}, {"8THS", "x-x-"},
};
constexpr int kPatternCount = int(sizeof(kPatterns) / sizeof(kPatterns[0]));
inline int patternSteps(int p) {
    int n = 0;
    while (kPatterns[p].steps[n])
        ++n;
    return n;
}

// Wobble rates, in beats per cycle.
constexpr double kRateBeats[] = {2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
constexpr const char *kRateNames[] = {"1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
constexpr int kRateCount = 8;

inline const char *gritName(float character) {
    return character < 0.34f ? "WARM" : character < 0.67f ? "TUBE" : "FOLD";
}

// What the engine publishes for the page, sixty times a second.
enum Pub : int {
    PubLane = 0,      // two lanes of 8: live, then clip
    PubOn = 0,        // sounding (1), or releasing (0.5)
    PubMidi = 1,      // the pitch now
    PubLevel = 2,
    PubDegree = 3,    // the pad's degree played (degree + octave × degrees per octave)
    PubSound = 4,
    PubCut = 5,       // the filter now, 0..1
    PubEnv = 6,       // the amp envelope
    PubGate = 7,      // 1 while a roll step is open
    PubLfo = 16,      // the wobble's position 0..1, and its depth
    PubWobDepth = 17,
    PubWobRate = 18,
    PubRoll = 19,     // the roll's pattern + 1, or 0
    PubRollStep = 20, // the step it is on
    PubGrit = 21,
    PubGritChar = 22,
    PubOct = 23,
    PubChordShift = 24, // degrees the chord moves a following line
    PubClipMute = 25,   // how far the loop has ducked under the hand (0..1 − kLoopUnderHand)
    PubScopeCycles = 26,
    PubFilter = 27,      // the FILTER finger's cutoff offset, and its morph offset
    PubFilterMorph = 28,
    PubLayerLanes = 32, // the loop's other layers, three lanes of 8 laid out as PubLane's
    PubScope = 128,     // 128 points: two cycles of the waveform, phase-locked
    PubScopeSize = 128,
};

// How loud the loop plays while the player holds a note over it.
constexpr float kLoopUnderHand = 0.5f;

// A loop's commands may come from a song file, so nothing in one is
// trusted. A degree is kept within a few octaves of the pad's (wider would
// only be out of hearing, and near the int's ends its octave overflows); a
// float that picks from a list is clamped before it becomes an int, since
// converting one too big for an int is undefined.
inline int clampDegree(int degree) { return std::clamp(degree, -32, 64); }
inline int indexOf(float v, int count) { return int(clampf(v, 0, float(count - 1))); }

// --- the sounds -----------------------------------------------------------------

struct Recipe {
    const char *name = "";
    const char *what = "";
    // Oscillators, all locked in phase to the sub: a sine, a unison of
    // saws, a square, a phase-modulated sine and a saw an octave up.
    float sine = 0, saw = 0, square = 0, fm = 0, octSaw = 0;
    float subSquare = 0;     // a square an octave down, under the top only
    int unison = 1;          // saws, 1–3
    float detune = 0;        // cents the outer saws sit from the centre
    float fmRatio = 1, fmIndex = 0, fmTone = 0; // fmTone: the height adds this much index
    float foldPre = 0;       // wavefolding of the sine and FM before the filter
    float sub = 1;           // the clean sine under the crossover
    // The finger's height (tone) runs the cutoff between these; 0..1 is
    // 50 Hz .. 16 kHz.
    float cutLo = 0.1f, cutHi = 0.7f, res = 0.2f;
    float envAmt = 0.3f, envDecay = 0.25f; // filter envelope, seconds
    float formant = 0;       // 0 all ladder .. 1 all vowel filter
    float vowel = 0, vowelTone = 0, vowelEnv = 0; // 0..4 = u o a e i
    float attack = 0.002f, decay = 0.4f, sustain = 0.8f, release = 0.08f;
    float drive = 0.2f, character = 0.2f, toneDrive = 0;
    bool driveFirst = false; // drive, then filter: the tone shapes the harmonics the drive made
    float drop = 0, dropTime = 0.04f; // semitones the note falls from, and how fast
    float glide = 0.05f;     // legato slide, seconds
    float width = 0;         // stereo above 300 Hz
    float accent = 0.6f;
    float wobble = 0;        // its own wobble depth when no finger asks for one
    float wobCut = 1, wobVowel = 0, wobFm = 0;
    // What morph (the twist, [ and ]) does.
    float mSquare = 0, mDetune = 0, mFm = 0, mVowel = 0, mDrive = 0;
    float mRes = 0, mEnv = 0, mDecay = 0; // the squelch: resonance, envelope depth, its decay (× 1 + this)
    // How hot the ladder is fed. At 1 it is fed gently, so a resonant
    // squelch keeps its peak; hotter, its own input stage saturates and
    // rounds every note into the thick, compressed body of a Minimoog.
    float heat = 1;
    // A plucked string in place of (or beside) the oscillators: its level,
    // how long the string itself rings (seconds to fall 60 dB), and how much
    // of a wooden body it sounds through. The amp envelope's decay is then
    // how long the note lasts, and morph scales it by 2^(mRing × (morph − 0.3)).
    float string = 0, ring = 6, body = 0, mRing = 0;
    // How much of the top's own fundamental comes through the crossover,
    // as a sine in phase with the sub at the level the top has it: a ladder
    // driven hard thickens its fundamental, and this lets that be heard
    // without the low end ever leaving the sub's phase.
    float lowThrough = 0;
    // A broad lift in the low mids after the filter (+6 dB at 1, at 240 Hz): the
    // throaty voice a Minimoog keeps while a note is held.
    float lowMid = 0;
    // A contour: the twist scales the filter envelope's decay by
    // 2^(contour × (morph − 0.3)), so the usual twist is envDecay itself,
    // and the finger's height shortens it by up to toneDecay, so higher
    // notes snap.
    float contour = 0, toneDecay = 0;
    float level = 1;
    // How far the kick ducks it, on top of the channel's light pump: the
    // whole sound, and the sub a little more. A house bass breathes with the
    // kick; an 808 plays with it.
    float pump = 0.4f, subPump = 0.3f;
};

inline const Recipe &recipe(int sound) {
    static const std::array<Recipe, SoundCount> recipes = [] {
        std::array<Recipe, SoundCount> r{};
        {
            // A sine you feel more than hear; the tone axis drives it into a
            // soft, slightly lopsided clip, which adds the second and third
            // harmonics that let a sub speak on small speakers.
            Recipe &s = r[Sub];
            s.name = "SUB";
            s.what = "clean sine, warmth on top";
            s.sine = 1;
            s.sub = 1;
            s.cutLo = 0.2f, s.cutHi = 0.62f, s.res = 0;
            s.envAmt = 0.08f, s.envDecay = 0.2f;
            s.attack = 0.004f, s.decay = 0.5f, s.sustain = 0.92f, s.release = 0.09f;
            s.drive = 0.3f, s.character = 0.5f, s.toneDrive = 0.6f, s.driveFirst = true;
            s.glide = 0.04f;
            s.accent = 0.4f;
            s.mDrive = 0.8f;
            s.level = 0.8f;
            s.pump = 0.5f, s.subPump = 0.3f;
        }
        {
            // The workhorse: a saw with some square through the ladder, a
            // short filter envelope, so every note has a pluck at the front
            // and a round body behind it. Rolling 16ths in trance, offbeats
            // in house.
            Recipe &s = r[Pluck];
            s.name = "PLUCK";
            s.what = "rolling saw, techno, trance";
            s.saw = 0.8f, s.square = 0.3f;
            s.sub = 0.85f;
            s.cutLo = 0.12f, s.cutHi = 0.72f, s.res = 0.3f;
            s.envAmt = 0.5f, s.envDecay = 0.16f;
            s.attack = 0.001f, s.decay = 0.22f, s.sustain = 0.5f, s.release = 0.06f;
            s.drive = 0.25f, s.character = 0.15f;
            s.glide = 0.045f;
            s.accent = 0.8f;
            s.mSquare = 1;
            s.level = 1.25f;
            s.pump = 0.45f;
        }
        {
            // Three saws a few cents apart beat against each other, so the
            // note phases and tears on its own; wide above 300 Hz, one sine
            // below.
            Recipe &s = r[Reese];
            s.name = "REESE";
            s.what = "detuned saws, drum & bass";
            s.saw = 1, s.unison = 3, s.detune = 11;
            s.sub = 1;
            s.cutLo = 0.2f, s.cutHi = 0.7f, s.res = 0.18f;
            s.envAmt = 0.1f, s.envDecay = 0.4f;
            s.attack = 0.006f, s.decay = 0.6f, s.sustain = 0.9f, s.release = 0.14f;
            s.drive = 0.4f, s.character = 0.25f;
            s.glide = 0.09f;
            s.width = 0.75f;
            s.accent = 0.4f;
            s.mDetune = 18;
            s.level = 1.0f;
            s.pump = 0.4f;
        }
        {
            // One saw, a ladder near self-oscillation and an envelope that
            // snaps it shut: accents bite, slides glide, and a distortion
            // after the filter, as the real thing went into a pedal. Morph
            // (the twist) is the squelch, the 303's resonance, env mod and
            // decay knobs turned together; the key \ swaps saw for square.
            Recipe &s = r[Acid];
            s.name = "ACID";
            s.what = "twist for squelch, \\ saw or square";
            s.saw = 1;
            s.sub = 0.75f;
            s.cutLo = 0.14f, s.cutHi = 0.6f, s.res = 0.6f;
            s.envAmt = 0.45f, s.envDecay = 0.11f;
            s.mRes = 0.36f, s.mEnv = 0.25f, s.mDecay = 1.6f;
            s.attack = 0.001f, s.decay = 0.35f, s.sustain = 0.7f, s.release = 0.03f;
            s.drive = 0.42f, s.character = 0.1f;
            s.glide = 0.065f;
            s.accent = 1;
            s.level = 1.3f;
            s.pump = 0.3f, s.subPump = 0.25f;
        }
        {
            // Saws and a square with a little FM grit, a resonant filter the
            // tempo-synced LFO sweeps open and shut: the wub. It wobbles at
            // an eighth on its own; the WOB finger plays the rate.
            Recipe &s = r[Wobble];
            s.name = "WOBBLE";
            s.what = "synced filter, dubstep";
            s.saw = 0.7f, s.square = 0.45f, s.fm = 0.3f, s.unison = 2, s.detune = 7;
            s.fmIndex = 1.2f;
            s.sub = 0.9f;
            s.cutLo = 0.1f, s.cutHi = 0.72f, s.res = 0.45f;
            s.envAmt = 0.1f, s.envDecay = 0.3f;
            s.formant = 0.25f, s.vowel = 1, s.vowelTone = 1.2f;
            s.attack = 0.004f, s.decay = 0.5f, s.sustain = 1, s.release = 0.1f;
            s.drive = 0.45f, s.character = 0.4f;
            s.glide = 0.06f;
            s.width = 0.45f;
            s.accent = 0.6f;
            s.wobble = 0.55f, s.wobCut = 1, s.wobVowel = 0.6f, s.wobFm = 1.6f;
            s.mFm = 3;
            s.level = 1.05f;
            s.pump = 0.35f;
        }
        {
            // Phase modulation folded and pushed through a vowel filter: the
            // finger's height and twist move the mouth, so it says "yoy" and
            // "wow" as it is played.
            Recipe &s = r[Growl];
            s.name = "GROWL";
            s.what = "FM and vowels that talk";
            s.saw = 0.2f, s.fm = 1, s.fmIndex = 1.0f, s.fmTone = 2.6f, s.foldPre = 0.35f;
            s.sub = 0.9f;
            s.cutLo = 0.15f, s.cutHi = 0.95f, s.res = 0.25f;
            s.envAmt = 0.1f, s.envDecay = 0.3f;
            // Height opens the mouth from u to a, the brightest vowel; the
            // twist and the wobble move it on through e and i.
            s.formant = 0.85f, s.vowel = 0.2f, s.vowelTone = 1.75f;
            s.attack = 0.004f, s.decay = 0.5f, s.sustain = 1, s.release = 0.1f;
            s.drive = 0.5f, s.character = 0.75f, s.driveFirst = true;
            s.glide = 0.06f;
            s.width = 0.55f;
            s.accent = 0.6f;
            s.wobCut = 0.3f, s.wobVowel = 1, s.wobFm = 2;
            s.mVowel = 2.0f, s.mFm = 1.2f;
            s.level = 0.65f;
            s.pump = 0.35f;
        }
        {
            // The trap 808: a sine that falls an octave onto its note in a
            // few tens of milliseconds, rings for seconds, glides when notes
            // overlap, and is driven so a phone can hear it. The drive's
            // harmonics sit above the crossover; the sine under it stays clean.
            Recipe &s = r[Boom];
            s.name = "808";
            s.what = "falling sine, long, glides";
            s.sine = 1;
            s.sub = 1.1f;
            s.cutLo = 0.3f, s.cutHi = 0.75f, s.res = 0;
            s.envAmt = 0, s.envDecay = 0.2f;
            s.attack = 0.001f, s.decay = 1.6f, s.sustain = 0.3f, s.release = 0.25f;
            s.drive = 0.5f, s.character = 0.45f, s.toneDrive = 0.7f, s.driveFirst = true;
            s.drop = 12, s.dropTime = 0.035f;
            s.glide = 0.11f;
            s.accent = 0.5f;
            s.mDrive = 1.2f;
            s.level = 0.9f;
            s.pump = 0, s.subPump = 0;
        }
        {
            // A square and a saw an octave up through a vowel filter that the
            // envelope opens from "o" to "a" on every note: the bouncy,
            // talking organ bass of bass house.
            Recipe &s = r[Bounce];
            s.name = "BOUNCE";
            s.what = "organ wah, bass house";
            s.saw = 0.3f, s.square = 0.8f, s.octSaw = 0.45f;
            s.sub = 0.85f;
            s.cutLo = 0.25f, s.cutHi = 0.7f, s.res = 0.2f;
            s.envAmt = 0.35f, s.envDecay = 0.14f;
            s.formant = 0.7f, s.vowel = 0.5f, s.vowelTone = 1.4f, s.vowelEnv = 2.2f;
            s.attack = 0.001f, s.decay = 0.2f, s.sustain = 0.3f, s.release = 0.05f;
            s.drive = 0.3f, s.character = 0.4f;
            s.glide = 0.035f;
            s.width = 0.25f;
            s.accent = 0.6f;
            s.mVowel = 1.5f;
            s.level = 1.75f;
            s.pump = 0.5f;
        }
        {
            // The Minimoog's bass: a saw and a square in tune and a third
            // oscillator an octave down, fed hot into a 24 dB ladder with
            // some emphasis, its envelope opening it wide on every note and
            // closing it in a tenth of a second (less higher up): "bwow",
            // and a staccato sixteenth still says "pew". Where ACID
            // squelches, this is throaty in the low mids even held, and some
            // of the driven ladder's own fundamental comes through under the
            // sub: funk, disco, synthwave. Mono, as the instrument was; the
            // twist is the contour, from a snap to a slow wah.
            Recipe &s = r[Moog];
            s.name = "MOOG";
            s.what = "ladder bwow, funk, synthwave";
            s.saw = 0.8f, s.square = 0.55f, s.subSquare = 0.4f;
            s.sub = 0.65f;
            s.cutLo = 0.06f, s.cutHi = 0.46f, s.res = 0.45f, s.lowMid = 2;
            s.envAmt = 0.5f, s.envDecay = 0.1f, s.contour = 1.6f, s.toneDecay = 0.35f;
            s.mEnv = 0.15f;
            s.heat = 2.5f;
            s.lowThrough = 0.32f;
            s.attack = 0.002f, s.decay = 0.7f, s.sustain = 0.85f, s.release = 0.1f;
            s.drive = 0.22f, s.character = 0.1f;
            s.glide = 0.075f;
            s.accent = 0.55f;
            s.level = 0.42f;
            s.pump = 0.3f, s.subPump = 0.2f;
        }
        {
            // A double bass played pizzicato: a string plucked by a soft
            // finger, ringing through a wooden body, dying away on its own
            // while held and stopped by the hand when let go. Its pitch
            // starts a hair sharp and settles, as a plucked string's does.
            // Height plucks nearer the bridge with a harder finger (from a
            // dub thud to a jazz twang); the twist is how long it rings.
            // Pressed lightly it is a ghost note; Shift+P palm-mutes it to a
            // dead thud. Jazz, lo-fi hip-hop, dub.
            Recipe &s = r[Upright];
            s.name = "UPRIGHT";
            s.what = "plucked string, jazz, lo-fi, dub";
            s.string = 1, s.ring = 7, s.body = 1, s.mRing = 2.2f;
            s.lowThrough = 0.32f;
            s.sub = 0.72f;
            s.cutLo = 0.5f, s.cutHi = 0.85f, s.res = 0;
            s.envAmt = 0.15f, s.envDecay = 0.12f;
            s.attack = 0.002f, s.decay = 0.5f, s.sustain = 0, s.release = 0.07f;
            s.drive = 0.15f, s.character = 0.35f;
            s.drop = 0.22f, s.dropTime = 0.06f;
            s.glide = 0.06f;
            s.accent = 0.5f;
            s.level = 1.7f;
            s.pump = 0.25f, s.subPump = 0.2f;
        }
        return r;
    }();
    return recipes[size_t(std::clamp(sound, 0, SoundCount - 1))];
}

// The key's root as the lowest note a bass wants: between E1 and D#2
// (41–78 Hz), the octave every producer puts a sub in.
inline double rootShift(const Tuning &t) { return t.root() >= 4 ? -12.0 : 0.0; }

// The nearest note of the key to a pitch: where a bend that has settled
// lands, so it ends in tune.
inline double nearestInKey(const Harmony &h, double midi) {
    const double base = h.tuning.baseMidi() + rootShift(h.tuning);
    const int n = h.tuning.degreesPerOctave();
    const int oct = int(std::floor((midi - base) / 12.0));
    double best = midi, bestDistance = 1e9;
    for (int d = (oct - 1) * n; d <= (oct + 2) * n; ++d) {
        const double m = base + h.tuning.degreeSemis(d);
        if (std::fabs(m - midi) < bestDistance) {
            bestDistance = std::fabs(m - midi);
            best = m;
        }
    }
    return best;
}

// How far a chord moves a line that follows it, in degrees: its root, folded
// to the nearer octave so the bass walks to the closest root and stays low.
inline int chordShift(const Harmony &h) {
    const int n = h.tuning.degreesPerOctave();
    int r = ((h.chordRoot % n) + n) % n;
    if (r > n / 2)
        r -= n;
    return r;
}

// --- DSP pieces -------------------------------------------------------------------

// 2× oversampling with a polyphase halfband. Of a 31-tap halfband only the
// 16 even-indexed side taps and the centre are not zero, so the upsampler's
// two output phases are a 16-tap filter and a plain delay, and the
// decimator the same in reverse: 32 multiplies a sample instead of 124.
struct Halfband2x {
    static constexpr int H = 16, D = 7; // side taps; the centre tap's delay in input samples
    float c[H] = {};
    float in[H] = {}, odd[H] = {}, even[H] = {};
    int pos = 0;
    Halfband2x() {
        constexpr int N = 31, centre = N / 2;
        float sum = 0;
        for (int k = 0; k < H; ++k) {
            const int i = 2 * k, n = i - centre;
            const float sinc = std::sin(kPi * 0.5f * float(n)) / (kPi * float(n));
            const float w = 0.42f - 0.5f * std::cos(kTwoPi * float(i) / (N - 1)) +
                            0.08f * std::cos(2.0f * kTwoPi * float(i) / (N - 1));
            c[k] = sinc * w;
            sum += c[k];
        }
        // The side taps carry half of DC, the centre tap (0.5) the other half.
        for (float &v : c)
            v *= 0.5f / sum;
    }
    void reset() {
        std::fill(std::begin(in), std::end(in), 0.0f);
        std::fill(std::begin(odd), std::end(odd), 0.0f);
        std::fill(std::begin(even), std::end(even), 0.0f);
    }
    template <typename F>
    inline float process(float x, F &&shape) {
        in[pos] = x;
        float a = 0;
        for (int k = 0, j = pos; k < H; ++k, j = (j + H - 1) & (H - 1))
            a += c[k] * in[j];
        // The two samples at twice the rate: the side taps (doubled, for the
        // zeros between), then the centre tap's delay of the input.
        even[pos] = shape(2.0f * a);
        odd[pos] = shape(in[(pos + H - D) & (H - 1)]);
        float y = 0.5f * even[(pos + H - D) & (H - 1)];
        for (int k = 0, j = pos; k < H; ++k, j = (j + H - 1) & (H - 1))
            y += c[k] * odd[j];
        pos = (pos + 1) & (H - 1);
        return y;
    }
};

// --- the engine -------------------------------------------------------------------

// What the shaper fingers ask for. A lane plays its own (a loop keeps the
// moves it was played with) under the player's live ones.
struct Shape {
    float oct = 0;   // octaves up, 0..2
    float roll = 0;  // gate length while rolling, 0 = not rolling
    int pattern = 3;
    float wob = 0;   // wobble depth while a finger wobbles, 0 = none
    int rate = 3;
    float grit = 0, gritChar = 0.3f;
    float bend = 0;  // semitones
    bool bendSettled = false; // a bend at rest lands on a note of the key
    float cut = 0, morph = 0; // the FILTER finger: cutoff and morph offsets

    // The live shapers over a loop's own: the player's hand wins.
    static Shape over(const Shape &own, const Shape &live) {
        Shape s = own;
        s.bend = own.bend + live.bend;
        s.bendSettled = live.bend != 0 ? live.bendSettled : own.bendSettled;
        s.cut = clampf(own.cut + live.cut, -0.9f, 0.9f);
        s.morph = clampf(own.morph + live.morph, -1.0f, 1.0f);
        s.oct = std::clamp(own.oct + live.oct, 0.0f, 2.0f);
        if (live.roll > 0) {
            s.roll = live.roll;
            s.pattern = live.pattern;
        }
        if (live.wob > 0) {
            s.wob = live.wob;
            s.rate = live.rate;
        }
        if (live.grit > own.grit) {
            s.grit = live.grit;
            s.gritChar = live.gritChar;
        }
        return s;
    }
};

struct Note {
    int id = -1;
    int degree = 0, octave = 0, flags = 0, sound = Pluck;
    float vel = 0.8f, tone = 0.5f, morph = 0.3f, fat = 0, glide = 0.5f, pressure = 0.5f;
    int rate = 3; // its own wobble's rate
};

// One mono synth with a note stack: a note that lands while another is held
// slides to it without a new attack (legato, the 303's slide), and lifting
// it slides back. A plucked string (the upright) is never slid onto: every
// finger plucks its note, and lifting one pulls off to the note still held.
class Lane {
public:
    static constexpr int kStack = 16;

    void init(float rate, uint32_t seed) {
        m_rate = rate;
        m_rng = Rng(seed);
        m_chorus.init(size_t(0.03f * rate));
        m_post.reset();
        m_sideHp.set(300.0f, 0.7f, rate);
        m_preLp.set(4500.0f, rate);
        m_preLp2.set(4500.0f, rate);
        // The upright's body: a broad wooden resonance where the box is
        // loudest and a narrower one higher up, the hollow knock in a
        // plucked note.
        m_body[0].set(190.0f, 1.4f, rate);
        m_body[1].set(520.0f, 2.6f, rate);
        // A stopped string is gone in about 20 ms.
        m_stopCoef = std::exp(-1.0f / (0.003f * rate));
        // The finger's thump on the string, and its knock on the
        // fingerboard as it stops a note.
        m_thumpBp.set(230.0f, 1.2f, rate);
        m_knockBp.set(320.0f, 1.4f, rate);
        m_fingerLp.set(800.0f, rate);
        m_lowMid.set(240.0f, 1.0f, rate);
        // The top's fundamental, read against the sub's own phase, settles
        // in about 15 ms.
        m_demod = 1.0f - std::exp(-kTwoPi * 11.0f / rate);
        m_tone.reset(0.5f);
        m_morph.reset(0.3f);
        m_fat.reset(0);
        m_press.reset(0.5f);
        m_mute.reset(1);
    }

    bool holding() const { return m_count > 0; }
    bool active() const { return m_active; }
    const Note &current() const { return m_cur; }

    // A note starts. beat is where in the bar it lands, for the roll.
    void noteOn(const Note &n, const Harmony &h, const Shape &sh, double beat) {
        remove(n.id);
        // A different note while one is held slides to it; the same note
        // again is struck again, so two fingers drumming one note, which
        // always overlap a little, still articulate. A string is plucked
        // each time: a slide onto a string that has rung away is silence.
        const bool legato = m_count > 0 && (n.degree != m_cur.degree || n.octave != m_cur.octave) &&
                            recipe(n.sound).string <= 0;
        if (m_count == kStack)
            remove(m_notes[0].id);
        m_notes[m_count++] = n;
        m_cur = n;
        m_sinceOn = 0;
        m_target = pitchOf(n, h) + 12.0 * double(sh.oct);
        m_chordSeq = h.chordSeq;
        if (legato) {
            m_glideTau = glideTime(n) / 3.0f;
            return;
        }
        m_midi = m_target;
        m_glideTau = 0;
        // Held, the lane runs, even while a roll waits for its next step.
        m_active = true;
        if (sh.roll > 0)
            enterRoll(sh, beat);
        else
            trigger(1, n.flags & Accent);
    }

    void noteOff(int id) {
        const bool top = m_count > 0 && m_notes[m_count - 1].id == id;
        remove(id);
        if (!top)
            return;
        if (m_count > 0) {
            m_cur = m_notes[m_count - 1];
            // A string pulls off: the held note at once, no slide.
            m_glideTau = recipe(m_cur.sound).string > 0 ? 0.0f : glideTime(m_cur) / 3.0f;
        } else {
            release();
        }
    }

    void move(const Command &c) {
        for (int k = 0; k < m_count; ++k) {
            Note &n = m_notes[k];
            if (n.id != c.id)
                continue;
            // Clamped as a note's start is: a loop's moves may come from a
            // song file.
            const int degree = clampDegree(c.i[0]), octave = std::clamp(c.i[1], -1, 3);
            const bool slide = n.degree != degree || n.octave != octave;
            n.degree = degree;
            n.octave = octave;
            n.pressure = clampf(c.f[0], 0, 1);
            n.tone = clampf(c.f[1], 0, 1);
            n.morph = clampf(c.f[2], 0, 1);
            n.fat = clampf(c.f[3], 0, 1);
            if (k == m_count - 1) {
                m_cur = n;
                // A move that comes with its note (a loop carrying in a note
                // held across its start, where it had slid to) is where the
                // note begins, not a slide.
                if (slide)
                    m_glideTau = m_sinceOn < int(0.005f * m_rate) ? 0.0f : glideTime(n) / 3.0f;
            }
        }
    }

    // The click: the sounding note again, accented.
    void strike() {
        if (m_count > 0)
            trigger(1, true);
    }

    void silence(bool hard) {
        m_count = 0;
        if (hard) {
            m_active = false;
            m_amp = 0;
            m_stage = Idle;
            m_ladder.reset();
            m_os.reset();
            m_os4.reset();
            stopStrings();
            m_noiseLeft = 0;
            m_fundI = m_fundQ = m_fundI2 = m_fundQ2 = 0;
        } else {
            release();
        }
    }

    // Renders n samples into l and r (added), off0 samples into the block.
    // wraps, if given, is set where the oscillator starts a cycle, for the
    // page's phase-locked scope.
    void render(float *l, float *r, int n, int off0, AudioContext &ctx, const Shape &sh, float muteTarget,
                uint8_t *wraps) {
        m_mute.setTime(0.012f, m_rate);
        if (!m_active) {
            m_mute.reset(muteTarget);
            m_level *= 0.9f;
            return;
        }
        const Recipe &R = recipe(m_cur.sound);
        const Harmony &h = ctx.harmony();
        const Transport &tr = ctx.transport();
        const float *duck = ctx.duck();
        const float rate = m_rate;

        // The chord moved under a line that follows it: walk to the new root
        // and play it again, as a bass player would.
        if (h.chordSeq != m_chordSeq) {
            m_chordSeq = h.chordSeq;
            if (m_count > 0 && (m_cur.flags & Follow)) {
                m_glideTau = 0.003f;
                if (sh.roll <= 0)
                    trigger(0.92f, false);
            }
        }
        if (sh.oct != m_oct) {
            // An octave finger is a jump, not a slide.
            m_oct = sh.oct;
            m_glideTau = 0.0015f;
        }
        m_target = pitchOf(m_cur, h) + 12.0 * double(m_oct);
        m_sinceOn = std::min(m_sinceOn + n, 1 << 30);

        // The roll starting or stopping under a held note.
        const bool rolling = sh.roll > 0 && m_count > 0;
        if (rolling && !m_rolling)
            enterRoll(sh, tr.beatAt(off0));
        else if (!rolling && m_rolling && m_count > 0 && !m_gate)
            trigger(1, false);
        m_rolling = rolling;

        const float blockSmooth = 1.0f - std::exp(-float(n) / (0.006f * rate));
        const float tone = m_tone.y += (m_cur.tone - m_tone.y) * blockSmooth;
        // The FILTER finger's offsets, smoothed: it arrives sixty times a second.
        const float cutOff = m_cutOff += (sh.cut - m_cutOff) * blockSmooth;
        m_morphOff += (sh.morph - m_morphOff) * blockSmooth;
        m_morph.y += (m_cur.morph - m_morph.y) * blockSmooth;
        const float morph = clampf(m_morph.y + m_morphOff, 0.0f, 1.0f);
        const float fat = m_fat.y += (m_cur.fat - m_fat.y) * blockSmooth;
        const float press = m_press.y += (m_cur.pressure - m_press.y) * blockSmooth;

        const float glideCoef = m_glideTau <= 0 ? 1.0f : 1.0f - std::exp(-1.0f / (m_glideTau * rate));
        // A bend arrives sixty times a second; smoothed over 15 ms it is a
        // continuous dive, not steps.
        const float bendCoef = 1.0f - std::exp(-1.0f / (0.015f * rate));
        // Moving, a bend goes where the finger is; settled, it lands on the
        // nearest note of the key.
        float bendTarget = sh.bend;
        if (sh.bendSettled && sh.bend != 0)
            bendTarget = float(nearestInKey(h, m_target + double(sh.bend)) - m_target);
        // A string's twist is how long it rings; muted by the palm, or
        // played as a ghost, it is a dead thud of 40 ms.
        float ampDecay = R.mRing != 0 ? R.decay * std::exp2(R.mRing * (morph - 0.3f)) : R.decay;
        const float dead = R.string > 0 ? deadness() : 0.0f;
        ampDecay = lerpf(ampDecay, 0.04f, dead);
        const float decayCoef = std::exp(-1.0f / (std::max(0.01f, ampDecay) * rate));
        const float releaseCoef = std::exp(-1.0f / (std::max(0.004f, R.release) * rate));
        const float attackInc = 1.0f / (std::max(0.0005f, R.attack) * rate);
        const float envDecay = R.envDecay * (1.0f + morph * R.mDecay) * (1.0f - 0.35f * m_accent) *
                               (R.contour != 0 ? std::exp2(R.contour * (morph - 0.3f)) : 1.0f) *
                               (1.0f - R.toneDecay * tone);
        const float fenvCoef = std::exp(-1.0f / (std::max(0.01f, envDecay) * rate));
        const float dropCoef = std::exp(-1.0f / (std::max(0.005f, R.dropTime) * rate));
        const float sustain = R.sustain;

        const float wobDepth = sh.wob > 0 ? sh.wob : R.wobble;
        const int wobRate = std::clamp(sh.wob > 0 ? sh.rate : m_cur.rate, 0, kRateCount - 1);
        const float grit = sh.grit;
        const float character =
            grit > 0 ? lerpf(R.character, sh.gritChar, std::min(1.0f, 2.0f * grit)) : R.character;
        const int pattern = std::clamp(sh.pattern, 0, kPatternCount - 1);
        const int steps = patternSteps(pattern);
        const float gateLen = std::clamp(sh.roll, 0.12f, 1.0f);

        // Morph: what it does depends on the sound.
        const float sqMix = (m_cur.flags & Square) ? 1.0f : std::min(1.0f, morph * R.mSquare);
        const float sawL = R.saw * (1.0f - 0.85f * sqMix);
        const float sqL = R.square + 0.8f * sqMix * (R.saw > 0 ? 1.0f : 0.0f);
        const float fmL = R.fm + grit * sh.gritChar * 0.5f;
        // A held note sits about 14 dB under full scale, the sub's peak near
        // −10 dB: room for the kick it pumps with, and for the bus.
        // A ghost note is mostly the finger: the note itself 15 dB down.
        const float ghost = R.string > 0 ? ghostness() : 0.0f;
        const float fingerGain = R.level * (0.5f + 0.5f * m_vel) * 0.42f;
        const float topGain = fingerGain * (1.0f - 0.82f * ghost);
        const float subGain = R.sub * (0.86f + 0.14f * m_vel) * (1.0f + 0.25f * fat) * 0.235f * (1.0f - 0.82f * ghost);
        const float width = std::min(1.0f, R.width + 0.4f * fat);
        const int unison = R.unison;
        const float uniNorm = 1.0f / std::sqrt(float(unison));

        float cutNow = m_cutNow;
        float lfo = m_lfo;
        float g = 1, comp = 1, fmIndex = 0, d2 = 1, d3 = 1, envAmt = 0, hiScale = 1;
        float peak = 0;
        float foldMax = 4.0f; // radians the fold may turn through, fewer up high
        auto shapeFn = [&character, &foldMax](float v) {
            // Warm (tanh), tube (lopsided: even harmonics), fold (sine
            // folding: the growl), blended continuously.
            const float t = fastTanh(v);
            const float a = fastTanh(v + 0.35f) - 0.33638f;
            if (character < 0.5f)
                return lerpf(t, a, character * 2.0f);
            // The fold turns at most about one and a half times: pushed
            // harder it only saturates, so its harmonics stay inside what
            // twice the rate can hold.
            const float f = std::sin(foldMax * fastTanh(v / foldMax));
            return lerpf(a, f, (character - 0.5f) * 2.0f);
        };

        for (int i = 0; i < n; ++i) {
            const double beat = tr.beat + double(off0 + i) * tr.beatsPerSample;

            // The roll: strike on the pattern's steps, close after the gate.
            if (m_rolling) {
                const double sp = beat * double(steps);
                const long step = long(std::floor(sp));
                if (step != m_step) {
                    m_step = step;
                    const int within = int(((step % steps) + steps) % steps);
                    if (kPatterns[pattern].steps[within] == 'x') {
                        // The step after the beat's middle lifts a little: a
                        // roll leans into the offbeat, which is where it grooves.
                        const bool lean = within * 2 == steps || (steps == 3 && within == 1);
                        trigger(lean ? 1.0f : 0.86f, false);
                        m_gateEnd = (double(step) + gateLen * double(untilNext(pattern, within))) / double(steps);
                    }
                }
                if (m_gate && beat >= m_gateEnd && gateLen < 0.999f)
                    closeGate();
            }

            // Every eight samples: the filter, the vowel, the drive.
            if ((i & 7) == 0) {
                const float hz0 = midiToHz(m_midi + double(m_bend));
                if (wobDepth > 0.001f) {
                    const double ph = beat / kRateBeats[wobRate];
                    lfo = 0.5f - 0.5f * std::cos(kTwoPi * float(ph - std::floor(ph)));
                } else {
                    lfo = 0;
                }
                const float keytrack = 0.4f * std::log2(std::max(hz0, 20.0f) / 55.0f) / 8.3f;
                // The FILTER finger: above its middle it opens the synth's own
                // filter; below, a DJ low-pass closes over the whole top, the
                // envelope and accents included, from 16 kHz down to 150 Hz.
                float cut = lerpf(R.cutLo, R.cutHi, tone) + keytrack + 0.1f * (press - 0.5f) + std::max(0.0f, cutOff);
                m_djOn = cutOff < -0.005f;
                if (m_djOn)
                    m_dj.set(16000.0f * std::exp2(cutOff / 0.7f * 6.7f), 0.7f, rate);
                if (wobDepth > 0.001f)
                    cut = lerpf(cut, lerpf(R.cutLo * 0.6f, cut + 0.22f, lfo), wobDepth * R.wobCut);
                envAmt = (R.envAmt + morph * R.mEnv) * (1.0f + 1.4f * m_accent * R.accent);
                cut += envAmt * m_fenv * 0.6f;
                cutNow = clampf(cut, 0.0f, 1.0f);
                const float fc = std::min(50.0f * std::exp2(cutNow * 8.3f), rate * (0.42f - 0.1f * R.res));
                m_ladder.set(fc, std::min(0.97f, R.res + morph * R.mRes + 0.12f * m_accent * R.accent), rate);
                if (R.formant > 0) {
                    const float v = clampf(R.vowel + tone * R.vowelTone + morph * R.mVowel + m_fenv * R.vowelEnv +
                                               wobDepth * lfo * R.wobVowel * 3.0f,
                                           0.0f, 3.999f);
                    setVowel(v);
                }
                // FM and folding run at the base rate, so they too thin out
                // as the note climbs.
                hiScale = clampf(320.0f / hz0, 0.2f, 1.0f);
                foldMax = 1.6f + 2.4f * hiScale * hiScale;
                fmIndex = (R.fmIndex + tone * R.fmTone + morph * R.mFm + wobDepth * lfo * R.wobFm + grit * sh.gritChar * 3.0f) * hiScale;
                const float drv = R.drive + tone * R.toneDrive + morph * R.mDrive + grit * 1.1f +
                                  m_accent * 0.15f * R.accent + std::max(0.0f, press - 0.55f) * 0.5f;
                // High up the drive eases off: there its harmonics would
                // reach past what even twice the rate can hold.
                g = 1.0f + 14.0f * drv * drv * hiScale * hiScale;
                comp = 1.0f / std::sqrt(1.0f + 0.5f * (g - 1.0f));
                // The crossover follows the note: everything at the note's
                // own pitch is the sub's, everything from its second harmonic
                // up is the top's.
                const float xo = clampf(hz0 * 1.45f, 80.0f, 320.0f);
                m_hp1.set(xo, 0.7071f, rate);
                m_hp2.set(xo, 0.7071f, rate);
                m_post.set(9000.0f, 0.6f, rate);
                if (unison > 1) {
                    m_drift += float(8 * 0.13 / double(rate));
                    if (m_drift >= 1.0f)
                        m_drift -= 1.0f;
                    const float cents = (R.detune + morph * R.mDetune + fat * 9.0f) *
                                        (1.0f + 0.22f * std::sin(kTwoPi * m_drift));
                    d2 = std::exp2(cents / 1200.0f);
                    d3 = std::exp2(-cents * 1.07f / 1200.0f);
                }
                if (R.string > 0)
                    tuneString(midiToHz(m_midi + double(m_bend + R.drop * m_drop)), tone, R.ring, dead);
            }

            // Envelopes.
            switch (m_stage) {
            case Attack:
                m_amp += attackInc;
                if (m_amp >= 1.0f) {
                    m_amp = 1.0f;
                    m_stage = Decay;
                }
                break;
            case Decay:
                m_amp = sustain + (m_amp - sustain) * decayCoef;
                break;
            case Release:
                m_amp *= releaseCoef;
                if (m_amp < 1e-4f) {
                    m_amp = 0;
                    m_stage = Idle;
                }
                break;
            case Idle:
                break;
            }
            m_fenv *= fenvCoef;
            m_drop *= dropCoef;
            m_midi += (m_target - m_midi) * double(glideCoef);
            m_bend += (bendTarget - m_bend) * bendCoef;

            // Oscillators.
            const float hz = midiToHz(m_midi + double(m_bend + R.drop * m_drop));
            const float inc = std::min(hz / rate, 0.45f);
            m_phase += inc;
            if (m_phase >= 1.0f) {
                m_phase -= 1.0f;
                m_halfCycle = !m_halfCycle;
                if (wraps)
                    wraps[i] = 1;
            }
            const float ph = m_phase;
            const float s1 = std::sin(kTwoPi * ph);
            float osc = R.sine * s1;
            if (sawL > 0) {
                // Falling saws, so their fundamental is in phase with the sine.
                float s = 1.0f - 2.0f * ph + polyBlep(ph, inc);
                if (unison > 1) {
                    const float i2 = std::min(inc * d2, 0.45f);
                    m_saw2 += i2;
                    if (m_saw2 >= 1.0f)
                        m_saw2 -= 1.0f;
                    s += 1.0f - 2.0f * m_saw2 + polyBlep(m_saw2, i2);
                }
                if (unison > 2) {
                    const float i3 = std::min(inc * d3, 0.45f);
                    m_saw3 += i3;
                    if (m_saw3 >= 1.0f)
                        m_saw3 -= 1.0f;
                    s += 1.0f - 2.0f * m_saw3 + polyBlep(m_saw3, i3);
                }
                osc += sawL * s * uniNorm;
            }
            if (sqL > 0) {
                float q = ph < 0.5f ? 1.0f : -1.0f;
                q += polyBlep(ph, inc);
                float t2 = ph + 0.5f;
                if (t2 >= 1.0f)
                    t2 -= 1.0f;
                q -= polyBlep(t2, inc);
                osc += sqL * 0.8f * q;
            }
            if (R.subSquare > 0) {
                // An octave down: high for one cycle of the note, low for
                // the next, its edges where the note's phase wraps. The
                // crossover keeps its own fundamental out of the low end,
                // so what it adds is the growl of its odd harmonics.
                const float hp = 0.5f * (ph + (m_halfCycle ? 1.0f : 0.0f));
                float q = m_halfCycle ? -1.0f : 1.0f;
                q += polyBlep(hp, 0.5f * inc);
                float t2 = hp + 0.5f;
                if (t2 >= 1.0f)
                    t2 -= 1.0f;
                q -= polyBlep(t2, 0.5f * inc);
                osc += R.subSquare * 0.8f * q;
            }
            if (R.octSaw > 0) {
                float p2 = ph * 2.0f;
                if (p2 >= 1.0f)
                    p2 -= 1.0f;
                const float i2 = std::min(inc * 2.0f, 0.45f);
                osc += R.octSaw * (1.0f - 2.0f * p2 + polyBlep(p2, i2));
            }
            if (fmL > 0) {
                m_fmPhase += inc * R.fmRatio;
                if (m_fmPhase >= 1.0f)
                    m_fmPhase -= 1.0f;
                float c = std::sin(kTwoPi * ph + fmIndex * std::sin(kTwoPi * m_fmPhase));
                if (R.foldPre > 0)
                    c = std::sin(c * (1.0f + 3.0f * R.foldPre * hiScale) * 1.5707963f);
                osc += fmL * c;
            }
            if (R.string > 0)
                osc += R.string * pluckedString(R.body);

            // Filters (the ladder, and the vowel bank beside it) and drive,
            // the drive at twice the rate so its harmonics don't fold back.
            float y;
            // The vowel bank shapes the oscillators first: the mouth gives
            // the character. Then the ladder and the drive, in the order the
            // recipe asks; either way the finger's height opens the sound.
            float x = osc * 0.7f;
            if (R.formant > 0) {
                const float v = m_vf[0].step(x).band * m_vk[0] + 0.7f * m_vf[1].step(x).band * m_vk[1] +
                                0.45f * m_vf[2].step(x).band * m_vk[2];
                // High up the mouth opens all the way: less vowel, more of
                // the raw tone through an open filter.
                x = lerpf(x, 1.8f * v, R.formant * (1.0f - 0.45f * tone * tone));
            }
            if (R.driveFirst) {
                // Nothing high goes into a drive with no filter before it:
                // what it would fold back is gone first.
                x = m_preLp2.step(m_preLp.step(x));
                // Four times the rate: a drive this hard folds and clips into
                // corners whose harmonics would pass twice the rate.
                auto inner = [&](float v) { return m_os4.process(v, shapeFn); };
                y = ladder(m_os.process(x * g, inner) * comp, R.heat);
            }
            else if (character > 0.5f) // folding: four times the rate here too
                y = m_os.process(ladder(x, R.heat) * g, [&](float v) { return m_os4.process(v, shapeFn); }) * comp;
            else
                y = m_os.process(ladder(x, R.heat) * g, shapeFn) * comp;
            y = m_post.step(y).low;
            if (R.lowMid > 0)
                y += R.lowMid * m_lowMid.k * m_lowMid.step(y).band;
            if (m_djOn)
                y = m_dj.step(y).low;
            // The top's own fundamental, before the crossover takes it: its
            // level, read against the sub's phase (both quadratures, twice
            // smoothed), comes back as a sine in that phase, so the drive's
            // weight is heard and the low end stays one signal.
            float fund = 0;
            if (R.lowThrough > 0) {
                const float c1 = std::cos(kTwoPi * ph);
                m_fundI += m_demod * (y * s1 - m_fundI);
                m_fundQ += m_demod * (y * c1 - m_fundQ);
                m_fundI2 += m_demod * (m_fundI - m_fundI2);
                m_fundQ2 += m_demod * (m_fundQ - m_fundQ2);
                fund = 2.0f * R.lowThrough * std::sqrt(m_fundI2 * m_fundI2 + m_fundQ2 * m_fundQ2) * s1;
            }
            // The crossover: two 2-pole high-passes, a 24 dB slope.
            y = m_hp2.step(m_hp1.step(y).high).high;

            const float amp = m_amp;
            const float mute = m_mute.step(muteTarget);
            const float dk = duck[std::min(off0 + i, kBlock - 1)];
            const float pumped = 1.0f - R.pump * dk;
            const float topScale = amp * topGain * (1.0f + 0.8f * m_accent * R.accent) * pumped;
            const float top = y * topScale;
            // The sub ducks under the kick harder than the top does.
            const float sub = s1 * amp * subGain * pumped * (1.0f - R.subPump * dk);
            float mono = (top + fund * topScale + sub) * mute;
            // The finger's own sounds, which don't follow the note's
            // envelope: the thump of a pluck and the knock of a stop.
            if (m_noiseLeft > 0)
                mono += fingerNoise() * fingerGain * pumped * mute;
            float side = 0;
            if (width > 0.01f) {
                m_chorus.push(m_sideHp.step(top * mute).high);
                m_chorusPhase += 0.37f / rate;
                if (m_chorusPhase >= 1.0f)
                    m_chorusPhase -= 1.0f;
                const float lfoC = std::sin(kTwoPi * m_chorusPhase);
                const float a = m_chorus.read((0.0075f + 0.0016f * lfoC) * rate);
                const float b = m_chorus.read((0.0113f - 0.0021f * lfoC) * rate);
                side = (a - b) * 0.55f * width;
            }
            // A soft knee near the top, the same for both sides, so an
            // accented, fat, resonant attack rounds off instead of hitting
            // the bus limiter, and the low end stays one signal.
            const float mag = std::fabs(mono) + std::fabs(side);
            if (mag > 0.55f) {
                const float k = (0.55f + 0.25f * fastTanh((mag - 0.55f) / 0.25f)) / mag;
                mono *= k;
                side *= k;
            }
            l[i] += mono + side;
            r[i] += mono - side;
            peak = std::max(peak, std::fabs(mono) + std::fabs(side));
        }
        m_cutNow = cutNow;
        m_lfo = wobDepth > 0.001f ? lfo : -1.0f;
        m_lfoDepth = wobDepth;
        m_level = std::max(peak, m_level * 0.97f);
        if (m_stage == Idle && m_count == 0 && m_noiseLeft <= 0)
            m_active = false;
    }

    // For the page.
    float level() const { return m_level; }
    float midi() const { return float(m_midi); }
    float cut() const { return m_cutNow; }
    float amp() const { return m_amp; }
    float lfo() const { return m_lfo; }
    float lfoDepth() const { return m_lfoDepth; }
    bool gate() const { return m_gate; }
    long step() const { return m_step; }
    float mute() const { return m_mute.y; }

private:
    enum Stage { Idle, Attack, Decay, Release };

    double pitchOf(const Note &n, const Harmony &h) const {
        const int d = n.degree + ((n.flags & Follow) ? chordShift(h) : 0);
        return h.midi(d, n.octave) + rootShift(h.tuning);
    }
    float glideTime(const Note &n) const {
        // The glide setting scales the sound's own slide from a quarter to
        // four times.
        return recipe(n.sound).glide * std::exp2((n.glide - 0.5f) * 4.0f);
    }
    void remove(int id) {
        for (int k = 0; k < m_count; ++k)
            if (m_notes[k].id == id) {
                for (int j = k; j + 1 < m_count; ++j)
                    m_notes[j] = m_notes[j + 1];
                --m_count;
                return;
            }
    }

    void trigger(float velScale, bool accent) {
        if (m_amp < 0.02f) {
            // From silence, start every cycle at zero: each note's first
            // swing of the sub is the same, so it punches the same.
            m_phase = m_fmPhase = 0;
            m_halfCycle = false;
            m_saw2 = m_rng.unit();
            m_saw3 = m_rng.unit();
            m_fundI = m_fundQ = m_fundI2 = m_fundQ2 = 0;
        }
        m_vel = clampf(m_cur.vel * velScale, 0.05f, 1.0f);
        m_accent = accent || m_cur.vel > 0.9f ? 1.0f : 0.0f;
        m_stage = Attack;
        m_fenv = 1;
        m_drop = 1;
        if (recipe(m_cur.sound).string > 0)
            pluckString();
        m_gate = true;
        m_active = true;
    }
    void closeGate() {
        m_gate = false;
        if (m_stage != Idle)
            m_stage = Release;
    }
    void release() {
        // The hand that lets a string go stops it: a soft knock, as loud
        // as what is still ringing.
        if (m_active && m_stage != Idle && recipe(m_cur.sound).string > 0) {
            m_knockT = 0;
            m_knockAmp = 0.0175f * (0.6f + 0.4f * m_vel) * m_amp;
            m_noiseLeft = int(0.03f * m_rate);
        }
        closeGate();
        m_rolling = false;
    }
    // A note lands, or the roll begins, while rolling: if this step of the
    // pattern is lit and still young, it sounds now; otherwise it waits for
    // the next lit step, so a roll is always on the grid.
    void enterRoll(const Shape &sh, double beat) {
        const int pattern = std::clamp(sh.pattern, 0, kPatternCount - 1);
        const int steps = patternSteps(pattern);
        const double sp = beat * double(steps);
        const long step = long(std::floor(sp));
        m_step = step;
        m_rolling = true;
        const int within = int(((step % steps) + steps) % steps);
        if (kPatterns[pattern].steps[within] == 'x' && sp - double(step) < 0.5) {
            trigger(1, m_cur.flags & Accent);
            m_gateEnd = (double(step) + std::clamp(sh.roll, 0.12f, 1.0f) * double(untilNext(pattern, within))) /
                        double(steps);
        } else if (m_gate) {
            closeGate();
        }
    }
    // The ladder fed gently: its input clips ahead of the resonance, so a
    // hot signal flattens the very peak a squelch is made of. In at a level
    // that falls as the resonance rises, out at the level it came in.
    // Fed hot (heat above 1), the input clips harder and the ladder
    // compresses: the Minimoog's roundness.
    float ladder(float x, float heat) {
        const float trim = heat * 0.45f / (1.0f + 0.5f * m_ladder.k);
        return m_ladder.step(x * trim) / trim;
    }

    // --- the plucked string ------------------------------------------------
    //
    // Karplus–Strong: a delay line one period long, fed back through a loss
    // filter, so every harmonic rings and the high ones die first, as on a
    // real string. The delay is read between samples (four-point Lagrange)
    // and the loss filter's own delay at the note is taken off it, so the
    // string is in tune with the sub and slides with it, a fretless glide.

    // Every eight samples: the delay for the pitch now, and the loss. The
    // loss is set by how fast a kilohertz dies (an eighth of a second low
    // on the field, a dark gut thud, to half a second high, a brighter
    // ring), whatever the note: a low string passes its loss filter fewer
    // times a second, so it gets a stronger one, and every note has the
    // same thump and bloom.
    void tuneString(float hz, float tone, float ring, float dead) {
        hz = clampf(hz, m_rate / float(kString - 8), m_rate / 16.0f);
        const float w = kTwoPi * hz / m_rate;
        {
            const float t60 = lerpf(0.12f, 0.5f, tone) * (1.0f - 0.6f * dead);
            // The gain each pass at 1 kHz for that, and the one-pole with
            // that gain there: (1 − a)² = g²(1 − 2a cos ω + a²), its smaller root.
            const float g = std::exp2(-9.966f / (hz * t60));
            const float c = std::cos(kTwoPi * 1000.0f / m_rate);
            const float u = 1.0f - g * g, b = 1.0f - g * g * c;
            m_strA = u > 1e-6f ? clampf((b - std::sqrt(std::max(0.0f, b * b - u * u))) / u, 0.0f, 0.95f) : 0.0f;
        }
        // The one-pole's phase delay at the fundamental, in samples.
        const float pd = std::atan2(m_strA * std::sin(w), 1.0f - m_strA * std::cos(w)) / w;
        m_strD = m_rate / hz - pd;
        // The loop loses 60 dB at the fundamental over ring seconds; the
        // amp envelope decides how long the note is heard.
        m_strG = std::exp2(-9.966f / (hz * ring));
    }

    // A pluck: a soft finger's bump of displacement, and the same bump
    // inverted a little later, its reflection from the near end; where the
    // finger is along the string sets the gap, so the harmonics with a node
    // there go missing. Height plucks harder and nearer the bridge.
    //
    // A string still ringing is stopped by the finger that plucks it again:
    // it fades out over a few milliseconds while the pluck starts on the
    // other of the lane's two strings, so a repeated note never clicks.
    void pluckString() {
        // Not two plucks alike: a little louder or softer, the finger a
        // little wider or nearer the bridge.
        const float vary = std::exp2(0.25f * m_rng.bipolar());
        const float wider = 1.0f + 0.1f * m_rng.bipolar(), nearer = 1.0f + 0.1f * m_rng.bipolar();
        if (m_amp >= 0.02f) {
            String &was = m_strings[m_string];
            was.level = 1;
            was.d = m_strD;
            m_string ^= 1;
        }
        String &s = m_strings[m_string];
        std::fill(std::begin(s.buf), std::end(s.buf), 0.0f);
        s.lp = 0;
        s.level = 0;
        const float period = m_rate / std::max(20.0f, midiToHz(m_target));
        const float tone = clampf(m_cur.tone + 0.2f * m_accent - 0.3f * deadness(), 0.0f, 1.0f);
        m_excW = std::min(m_rate / lerpf(330.0f, 2600.0f, tone * tone) * wider, 0.6f * period);
        m_excGap = lerpf(0.25f, 0.13f, tone) * period * nearer;
        m_excT = 0;
        // A narrower bump carries less of the low harmonics; a harder,
        // sharper pluck keeps most of the body and adds the snap on top.
        m_excAmp = 0.55f * std::pow(m_rate / 330.0f / m_excW, 0.6f) * vary;
        // An accent plucks hard enough that the string slaps the
        // fingerboard: its swing is clipped there until it has settled.
        m_buzz = m_accent > 0 ? 0.55f * m_excAmp : 0.0f;
        // The finger on the string: a short thump, louder for a ghost.
        m_thumpT = 0;
        m_thumpAmp = (0.06f + 0.06f * m_accent + 0.1f * ghostness()) * vary;
        m_noiseLeft = int(0.03f * m_rate);
    }
    // How much a note is a ghost (pressed lightly), and how dead it is
    // (a ghost, or palm-muted with Shift and its letter).
    float ghostness() const { return clampf((0.6f - m_cur.vel) / 0.15f, 0.0f, 1.0f); }
    float deadness() const { return (m_cur.flags & Square) ? 1.0f : ghostness(); }

    // The thump and the knock: noise through a Hann window, each through
    // its own band-pass, so they start and end without a click.
    float fingerNoise() {
        float t = 0, k = 0;
        const float thumpLen = 0.007f * m_rate, knockLen = 0.005f * m_rate;
        if (m_thumpT < thumpLen) {
            const float w = std::sin(kPi * m_thumpT / thumpLen);
            t = m_thumpAmp * w * w * m_rng.bipolar();
            m_thumpT += 1;
        }
        if (m_knockT < knockLen) {
            const float w = std::sin(kPi * m_knockT / knockLen);
            k = m_knockAmp * w * w * m_rng.bipolar();
            m_knockT += 1;
        }
        --m_noiseLeft;
        // A gentle low-pass after them both: a finger is soft, and noise
        // would otherwise leave a hiss on the band-passes' skirts.
        return m_fingerLp.step(m_thumpBp.k * 3.0f * m_thumpBp.step(t).band + m_knockBp.k * 3.0f * m_knockBp.step(k).band);
    }
    void stopStrings() {
        for (String &s : m_strings) {
            std::fill(std::begin(s.buf), std::end(s.buf), 0.0f);
            s.lp = s.level = 0;
        }
    }
    inline float pluckedString(float body) {
        float e = 0;
        if (m_excT < m_excW + m_excGap) {
            auto bump = [this](float t) {
                return t >= 0 && t < m_excW ? 0.5f - 0.5f * std::cos(kTwoPi * t / m_excW) : 0.0f;
            };
            e = m_excAmp * (bump(m_excT) - bump(m_excT - m_excGap));
            m_excT += 1;
        }
        float v = m_strings[m_string].step(m_strD, m_strA, m_strG, e, m_buzz);
        String &was = m_strings[m_string ^ 1];
        if (was.level > 1e-4f) {
            was.level *= m_stopCoef;
            v += was.level * was.step(was.d, m_strA, m_strG, 0, 0);
        }
        // Through the body: the string's own sound and the box's.
        return v + body * (0.9f * m_body[0].k * m_body[0].step(v).band + 0.6f * m_body[1].k * m_body[1].step(v).band);
    }

    // Steps from a lit step to the next: the gate is a share of that, so an
    // offbeat note can last an eighth and a sixteenth roll stays tight.
    static int untilNext(int pattern, int within) {
        const int steps = patternSteps(pattern);
        for (int k = 1; k <= steps; ++k)
            if (kPatterns[pattern].steps[(within + k) % steps] == 'x')
                return k;
        return steps;
    }
    void setVowel(float v) {
        // Formants of u, o, a, e, i (a man's voice, a little low).
        static const float F[5][3] = {
            {325, 700, 2530}, {450, 800, 2830}, {800, 1150, 2900}, {400, 1900, 2550}, {290, 2250, 3000}};
        static const float Q[3] = {5.0f, 7.0f, 9.0f};
        const int a = int(v);
        const float t = v - float(a);
        for (int k = 0; k < 3; ++k) {
            const float hz = F[a][k] * std::pow(F[a + 1][k] / F[a][k], t);
            m_vf[k].set(hz, Q[k], m_rate);
            m_vk[k] = 1.0f / Q[k] * 2.0f;
        }
    }

    float m_rate = 48000;
    Rng m_rng;
    Note m_notes[kStack];
    int m_count = 0;
    Note m_cur;
    bool m_active = false, m_gate = false, m_rolling = false;
    Stage m_stage = Idle;
    float m_amp = 0, m_fenv = 0, m_drop = 0, m_vel = 0.8f, m_accent = 0;
    double m_midi = 33, m_target = 33;
    float m_glideTau = 0;
    float m_oct = 0, m_bend = 0, m_cutOff = 0, m_morphOff = 0;
    int m_sinceOn = 0; // samples since the sounding note began
    uint32_t m_chordSeq = 0;
    long m_step = 0;
    double m_gateEnd = 0;
    bool m_halfCycle = false; // which half of the octave-down square
    float m_phase = 0, m_saw2 = 0, m_saw3 = 0, m_fmPhase = 0, m_drift = 0, m_chorusPhase = 0;
    Smooth m_tone, m_morph, m_fat, m_press, m_mute;
    Ladder m_ladder;
    Svf m_vf[3];
    float m_vk[3] = {1, 1, 1};
    Halfband2x m_os, m_os4;
    Svf m_post, m_hp1, m_hp2, m_sideHp;
    OnePoleLp m_preLp, m_preLp2;
    Svf m_dj;            // the FILTER finger's low-pass, below its middle
    bool m_djOn = false;
    Delay m_chorus;
    float m_cutNow = 0, m_lfo = -1, m_lfoDepth = 0, m_level = 0;
    // The plucked string, two of them so one can be stopped while the next
    // pluck starts: each a delay line long enough for a note bent two
    // octaves under the bottom of the pad, read d samples back through a
    // one-pole loss.
    static constexpr int kString = 4096;
    struct String {
        float buf[kString] = {};
        int w = 0;
        float d = 100, lp = 0, level = 0; // level: how far a stopped string has faded
        inline float step(float delay, float a, float g, float in, float buzz) {
            const float pos = float(w + kString) - delay;
            const int i0 = int(pos);
            const float f = pos - float(i0);
            const float p = f + 1, q = f - 1, r = f - 2;
            const float z = -f * q * r * (1.0f / 6.0f) * buf[(i0 - 1) & (kString - 1)] +
                            p * q * r * 0.5f * buf[i0 & (kString - 1)] -
                            p * f * r * 0.5f * buf[(i0 + 1) & (kString - 1)] +
                            p * f * q * (1.0f / 6.0f) * buf[(i0 + 2) & (kString - 1)];
            lp += (1.0f - a) * (z - lp);
            float v = g * lp + in;
            // Against the fingerboard: past it, the swing gives way.
            if (buzz > 0 && std::fabs(v) > buzz)
                v = std::copysign(buzz + 0.3f * (std::fabs(v) - buzz), v);
            buf[w] = v;
            w = (w + 1) & (kString - 1);
            return v;
        }
    };
    String m_strings[2];
    int m_string = 0; // the one plucked last
    float m_strD = 100, m_strA = 0.5f, m_strG = 0.99f, m_stopCoef = 0.99f;
    float m_excT = 1e9f, m_excW = 1, m_excGap = 0, m_excAmp = 0, m_buzz = 0;
    Svf m_body[2];
    // The finger's thump and knock, and how long their filters still ring.
    float m_thumpT = 1e9f, m_thumpAmp = 0, m_knockT = 1e9f, m_knockAmp = 0;
    int m_noiseLeft = 0;
    Svf m_thumpBp, m_knockBp;
    OnePoleLp m_fingerLp;
    Svf m_lowMid;
    // The top's fundamental against the sub's phase, in quadrature.
    float m_fundI = 0, m_fundQ = 0, m_fundI2 = 0, m_fundQ2 = 0, m_demod = 0.001f;
};

class Engine : public PartEngine {
public:
    void init(float rate) override {
        m_rate = rate;
        m_live.init(rate, 0xB455u);
        for (int k = 0; k < kClipLayers; ++k)
            m_clip[size_t(k)].init(rate, 0x5EB0u + uint32_t(k) * 0x101u);
    }

    void command(const Command &c, AudioContext &ctx) override {
        // Clip events land inside the coming block: keep them for their
        // sample, so a looped line is as tight as the grid.
        const int off = c.t > 0 ? ctx.offsetOf(c.t, kBlock) : 0;
        if (off > 0 && m_pendingCount < kPending) {
            m_pending[m_pendingCount++] = {c, off};
            return;
        }
        apply(c, ctx, 0);
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        // Pending commands in time order (a handful at most).
        for (int a = 1; a < m_pendingCount; ++a)
            for (int b = a; b > 0 && m_pending[b].off < m_pending[b - 1].off; --b)
                std::swap(m_pending[b], m_pending[b - 1]);
        int done = 0, k = 0;
        while (done < n) {
            while (k < m_pendingCount && m_pending[k].off <= done)
                apply(m_pending[k++].c, ctx, done);
            const int next = k < m_pendingCount ? std::min(n, m_pending[k].off) : n;
            segment(l + done, r + done, next - done, done, ctx);
            done = next;
        }
        while (k < m_pendingCount)
            apply(m_pending[k++].c, ctx, n);
        m_pendingCount = 0;
        m_harmony = ctx.harmony();
        m_beat = ctx.transport().beatAt(n);
    }

    void silence(bool hard) override {
        m_live.silence(hard);
        for (Lane &ln : m_clip)
            ln.silence(hard);
        m_pendingCount = 0;
        // The live shapers are the page's to set and clear; the loops' go.
        if (hard)
            for (Shape &sh : m_shapeSlot)
                sh = Shape{};
    }

    void publish(float *data) override {
        // Live, the loop's lane shown, then its other layers.
        const Lane *lanes[2 + kClipLayers - 1] = {&m_live, &clipShown()};
        int count = 2;
        for (const Lane &ln : m_clip)
            if (&ln != lanes[1])
                lanes[count++] = &ln;
        for (int k = 0; k < count; ++k) {
            const Lane &ln = *lanes[k];
            float *d = data + (k < 2 ? PubLane + 8 * k : PubLayerLanes + 8 * (k - 2));
            d[PubOn] = ln.active() ? (ln.holding() ? 1.0f : 0.5f) : 0.0f;
            d[PubMidi] = ln.midi();
            d[PubLevel] = ln.level();
            d[PubDegree] = float(ln.current().degree + ln.current().octave * m_harmony.tuning.degreesPerOctave());
            d[PubSound] = float(ln.current().sound);
            d[PubCut] = ln.cut();
            d[PubEnv] = ln.amp();
            d[PubGate] = ln.gate() ? 1.0f : 0.0f;
        }
        const Lane &shown = m_live.active() || !clipShown().active() ? m_live : clipShown();
        const Shape sh = &shown == &m_live ? m_shapeLive : Shape::over(clipShape(), m_shapeLive);
        data[PubLfo] = shown.lfo();
        data[PubWobDepth] = shown.lfoDepth();
        data[PubWobRate] = float(sh.wob > 0 ? sh.rate : shown.current().rate);
        data[PubRoll] = sh.roll > 0 ? float(sh.pattern + 1) : 0.0f;
        {
            const int steps = patternSteps(std::clamp(sh.pattern, 0, kPatternCount - 1));
            const double sp = m_beat * double(steps);
            data[PubRollStep] = float(((long(std::floor(sp)) % steps) + steps) % steps);
        }
        data[PubGrit] = sh.grit;
        data[PubGritChar] = sh.gritChar;
        data[PubOct] = sh.oct;
        data[PubFilter] = sh.cut;
        data[PubFilterMorph] = sh.morph;
        data[PubChordShift] = float(chordShift(m_harmony));
        data[PubClipMute] = 1.0f - m_clip[0].mute();
        scope(data + PubScope, data[PubScopeCycles]);
    }

    int voices() const override {
        int n = int(m_live.active());
        for (const Lane &ln : m_clip)
            n += int(ln.active());
        return n;
    }

private:
    static constexpr int kPending = 64, kScope = 4096;
    struct Pending {
        Command c;
        int off;
    };

    void apply(const Command &c, AudioContext &ctx, int off) {
        const bool replay = (c.id >> 24) != 0;
        // Each layer of a loop is a mono line of its own, on its own lane.
        Lane &lane = replay ? m_clip[size_t(replayLayer(c.id))] : m_live;
        // A loop's shapers belong to its slot, so one loop's wobble never
        // leaks into the next.
        Shape &own = replay ? m_shapeSlot[slotOf(c.id)] : m_shapeLive;
        const Shape eff = replay ? Shape::over(own, m_shapeLive) : m_shapeLive;
        switch (c.type) {
        case Command::On: {
            Note n;
            n.id = c.id;
            n.degree = clampDegree(c.i[0]);
            n.octave = std::clamp(c.i[1], -1, 3);
            n.flags = c.i[2];
            n.sound = std::clamp(c.i[3], 0, SoundCount - 1);
            n.vel = clampf(c.f[0], 0.05f, 1.0f);
            n.tone = clampf(c.f[1], 0, 1);
            n.morph = clampf(c.f[2], 0, 1);
            n.fat = clampf(c.f[3], 0, 1);
            n.glide = clampf(c.f[4], 0, 1);
            n.pressure = n.vel;
            n.rate = indexOf(c.f[5], kRateCount);
            lane.noteOn(n, ctx.harmony(), eff, ctx.transport().beatAt(off));
            break;
        }
        case Command::Off:
            lane.noteOff(c.id);
            break;
        case Command::Move:
            lane.move(c);
            break;
        case Command::Param:
            switch (c.i[0]) {
            case ShapeOct:
                own.oct = clampf(std::round(c.f[0]), 0, 2);
                break;
            case ShapeRoll:
                own.roll = clampf(c.f[0], 0, 1);
                own.pattern = indexOf(c.f[1], kPatternCount);
                break;
            case ShapeWob:
                own.wob = clampf(c.f[0], 0, 1);
                own.rate = indexOf(c.f[1], kRateCount);
                break;
            case ShapeGrit:
                own.grit = clampf(c.f[0], 0, 1);
                own.gritChar = clampf(c.f[1], 0, 1);
                break;
            case ShapeBend:
                own.bend = clampf(c.f[0], -24, 12);
                own.bendSettled = c.f[1] > 0.5f;
                break;
            case ShapeFilter:
                own.cut = clampf(c.f[0], -0.7f, 0.7f);
                own.morph = clampf(c.f[1], -1.0f, 1.0f);
                break;
            case ResetSlot:
                // The page is about to fill a slot: what an old loop there
                // left set goes with it.
                m_shapeSlot[indexOf(c.f[0], kClipSlots)] = Shape{};
                break;
            default:
                break;
            }
            break;
        case kStrike:
            if (lane.holding())
                lane.strike();
            break;
        default:
            break;
        }
    }

    static int slotOf(int id) { return replaySlot(id); }
    // The clip lane shown and heard as "the loop": the first layer sounding.
    const Lane &clipShown() const {
        for (const Lane &ln : m_clip)
            if (ln.active())
                return ln;
        return m_clip[0];
    }
    const Shape &clipShape() const { return m_shapeSlot[slotOf(clipShown().current().id)]; }

    void segment(float *l, float *r, int n, int off0, AudioContext &ctx) {
        uint8_t wraps[kBlock] = {};
        // The shown lane marks its cycles for the scope.
        const Lane &shownClip = clipShown();
        const bool liveShown = m_live.active() || !shownClip.active();
        float mixL[kBlock] = {}, mixR[kBlock] = {};
        m_live.render(mixL, mixR, n, off0, ctx, m_shapeLive, 1.0f, liveShown ? wraps : nullptr);
        // The loop ducks to half while the player holds a note: the hand
        // leads, and the loop it would layer onto is still there to play
        // along with. Its layers all share the loop's shapers.
        const Shape clipSh = Shape::over(clipShape(), m_shapeLive);
        for (Lane &ln : m_clip)
            ln.render(mixL, mixR, n, off0, ctx, clipSh, m_live.holding() ? kLoopUnderHand : 1.0f,
                          liveShown || &ln != &shownClip ? nullptr : wraps);
        for (int i = 0; i < n; ++i) {
            l[i] += mixL[i];
            r[i] += mixR[i];
            m_scopeBuf[m_scopeW & (kScope - 1)] = 0.5f * (mixL[i] + mixR[i]);
            if (wraps[i])
                m_wraps[m_wrapW++ & 7] = m_scopeW;
            ++m_scopeW;
        }
    }

    // Two cycles of what is sounding, from one of the oscillator's own
    // zero phases to the next but one, resampled to the page's points.
    void scope(float *out, float &cycles) {
        cycles = 0;
        if (m_wrapW < 3) {
            std::fill(out, out + PubScopeSize, 0.0f);
            return;
        }
        const uint32_t a = m_wraps[(m_wrapW - 3) & 7], b = m_wraps[(m_wrapW - 1) & 7];
        if (b <= a || b - a >= kScope || m_scopeW - a >= kScope) {
            std::fill(out, out + PubScopeSize, 0.0f);
            return;
        }
        cycles = 2;
        const float span = float(b - a);
        for (int k = 0; k < PubScopeSize; ++k) {
            const float pos = float(a) + span * float(k) / float(PubScopeSize - 1);
            const uint32_t i0 = uint32_t(pos);
            const float t = pos - float(i0);
            out[k] = lerpf(m_scopeBuf[i0 & (kScope - 1)], m_scopeBuf[(i0 + 1) & (kScope - 1)], t);
        }
    }

    float m_rate = 48000;
    Lane m_live;
    std::array<Lane, kClipLayers> m_clip; // a loop's layers, each its own mono line
    Shape m_shapeLive, m_shapeSlot[kClipSlots];
    Pending m_pending[kPending];
    int m_pendingCount = 0;
    Harmony m_harmony;
    double m_beat = 0;
    float m_scopeBuf[kScope] = {};
    uint32_t m_scopeW = 0;
    uint32_t m_wraps[8] = {};
    uint32_t m_wrapW = 0;
};

}
