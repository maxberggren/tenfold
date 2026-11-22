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
// whose top is a plucked string (Karplus–Strong) in a wooden body. Five
// more reach past the dance floor: an amapiano log drum (a struck bar's
// inharmonic modes over a falling sine, gliding from note to note), a
// Balkan brass tuba (a saw through lopsided lips, flaring bright only when
// played hard), the NES's bass (its four-bit triangle and a pulse, moving
// once a frame as the console did), a tabla's bayan (a skin whose pitch the
// wrist pushes up, closed by a light hand) and a didgeridoo (a lip buzz
// down a bore that rings on odd harmonics, its one mouth formant on the
// finger).
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

// Saved songs name their sound, so the order may grow but never change.
enum Sound : int {
    Sub, Pluck, Reese, Acid, Wobble, Growl, Boom, Bounce, Moog, Upright,
    Log, Tuba, Chip, Tabla, Didge,
    SoundCount
};
// The first sounds have a letter each (Q to P); the rest are reached by
// stepping with ← and →, and by the mouse.
constexpr int kLetteredSounds = 10;

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
    // Where the lane's soft knee begins (its output never passes 1.45
    // times this): lower, a sound whose strike stands far over its body is
    // rounded there, as a drum is clipped on a mix bus, rather than left
    // for the master limiter to swallow.
    float knee = 0.55f;
    // The knee ducks with the kick (kneeDucks): a strike or a swell landing
    // on one is rounded lower, as if clipped before the pump, so the two
    // together stay off the bus's ceiling.
    bool kneeDucks = false;
    // Every finger strikes: a note landing while another is held is struck
    // anew (a slide onto a drum that has died away would be silence), its
    // pitch sliding in from the held one, as an amapiano log glides. The
    // slide takes longer the further it goes, by slideSpan of the glide for
    // every fifth (a player draws a wide slide out); lifting it slides back
    // to the held note and strikes that again at restrike of the velocity.
    bool struck = false;
    float slideSpan = 0, restrike = 0;
    // Semitones the sound sits above the pad: a drum whose voice lives
    // higher than a sub's carries its own register.
    float octave = 0;
    // The twist scales the drop (a negative drop is a scoop up into the
    // note) by adding this many semitones at full twist.
    float mDrop = 0;
    // Pressing harder after the strike bends the note up by as many as
    // this many semitones: the bayan player's wrist on the skin.
    float gumki = 0;
    // Up to three inharmonic partials of a struck bar or skin, as ratios of
    // the note, each falling away on its own at its own level; height
    // brings them up by modeTone. The twist that sets the ring (mRing)
    // stretches them by half as much.
    float modes = 0, modeTone = 0;
    float modeRatio[3] = {2.756f, 5.404f, 8.933f}, modeDecay[3] = {0.08f, 0.03f, 0.02f};
    float modeLevel[3] = {1, 0.55f, 0};
    // The hand's slap on a skin as it strikes: noise of slapTime seconds
    // through a band-pass at slapHz, louder accented.
    float slap = 0, slapHz = 600, slapTime = 0.02f;
    // A light press damps the skin (pressed under about 0.55 velocity): the
    // closed "ke", all slap and no ring.
    bool damp = false;
    // Brass: a cutoff that follows the breath, opening by cutAmp at full
    // amplitude, so a note brightens as it swells and darkens as it dies.
    // Brassiness (brassy) adds almost nothing up to mezzo-forte and then
    // flares: past 0.7 velocity the drive and the envelope's opening climb
    // steeply, the upper harmonics bursting in as a brass player's do at
    // fortissimo. lipBuzz is the lips catching on a note, harder the harder
    // it is blown: for its first 40 ms the pitch wanders (by up to half a
    // semitone at 1) and the buzz flutters.
    float cutAmp = 0, brassy = 0, lipBuzz = 0;
    // Breath noise through a band-pass at breathHz, with the amp envelope
    // and a chiff on the attack.
    float breath = 0, breathHz = 1200;
    // The NES's triangle: sixteen levels up and sixteen down, the steps
    // band-limited, and a pulse over it (pulse, plus pulseTone with height).
    // With nes, everything moves as the console did, once a frame (60 Hz):
    // the pitch (from the triangle's 11-bit period table, so upper notes
    // sit a few cents off), the pulse's volume (sixteen steps falling to a
    // floor) and its duty (12.5 %, then 25 %, then 50 %), the twist's
    // arpeggio over the chord, and an accent's kick blip, the triangle
    // sweeping down two octaves onto the note, with the noise channel's
    // short burst (nesNoise, its level) over the sweep's first frames: the
    // two halves of the bitpop kick.
    float chipTri = 0, pulse = 0, pulseTone = 0, nesNoise = 0;
    bool nes = false;
    // A didgeridoo: lips buzzing (an asymmetric pulse, open longer than
    // shut) into a bore that rings on the note's odd harmonics, through one
    // strong mouth formant the finger's height sweeps from 800 Hz to 2 kHz
    // (pressure pushes it further). The twist is the voice, sung into the
    // drone a little off its fifth, so the two beat and growl; an accent
    // overblows a toot, a tenth (toot semitones) above. Rolled, the breath
    // pumps on the pattern's steps instead of the note being struck again.
    float didge = 0, toot = 0;
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
        {
            // The amapiano log drum: a hollow wooden bar struck with a
            // mallet. A sine that falls a fourth onto its note in a few
            // milliseconds (shorter and smaller than the 808's drop, so it
            // knocks rather than booms), and the bar's own upper modes (a
            // free bar's 2.76, 5.40 and 8.93 times the note) over it: the
            // last two are the "tok" and gone in tens of milliseconds, the
            // first rings on under the note for a third of a second, the
            // hollow, rubbery voice that keeps it a log and not an 808.
            // Held, it keeps a little of its boom.
            // Driven only gently and lopsidedly (a tube's even harmonics,
            // little third, which would beat against the 2.76 mode). Every
            // finger strikes, sliding in from the note held, slower the
            // further it goes; lifting slides back and strikes the held
            // note again, softly. The twist is how long it rings, from a
            // dry knock to a long, gliding boom. It sits an octave over the
            // pad, where an amapiano line lives (about 70 to 140 Hz across
            // the field's middle), and its knee is low, so a strike landing
            // on a kick is rounded before the bus has to.
            Recipe &s = r[Log];
            s.name = "LOG";
            s.what = "struck log, amapiano";
            s.sine = 1;
            s.sub = 1.1f;
            s.octave = 12;
            s.modes = 1.1f, s.modeTone = 1.0f;
            s.modeRatio[0] = 2.756f, s.modeRatio[1] = 5.404f, s.modeRatio[2] = 8.933f;
            s.modeDecay[0] = 0.35f, s.modeDecay[1] = 0.03f, s.modeDecay[2] = 0.05f;
            s.modeLevel[0] = 0.8f, s.modeLevel[1] = 0.5f, s.modeLevel[2] = 0.35f;
            s.cutLo = 0.4f, s.cutHi = 0.85f, s.res = 0.1f;
            s.envAmt = 0.25f, s.envDecay = 0.05f;
            s.attack = 0.001f, s.decay = 0.3f, s.sustain = 0.3f, s.release = 0.08f;
            s.drive = 0.0f, s.character = 0.5f, s.toneDrive = 0.15f, s.driveFirst = true;
            s.drop = 5, s.dropTime = 0.016f;
            s.mRing = 2.0f;
            s.glide = 0.055f, s.slideSpan = 0.9f, s.restrike = 0.6f;
            s.struck = true;
            s.accent = 0.35f;
            s.level = 0.6f;
            s.pump = 0.5f, s.subPump = 0.45f;
            s.knee = 0.25f;
            s.kneeDucks = true;
        }
        {
            // A tuba in a Balkan brass band (or a sousaphone on a second
            // line): lips buzzing into a long brass tube. A saw, and a little
            // of one an octave up, so every harmonic is there and the second
            // and fourth are as strong as brass has them, shaped by a
            // lopsided drive, as lips are. That drive thins the odd ones, so
            // a little square puts back the third and fifth (more would make
            // it a clarinet). Brass gets brighter as it gets louder, but
            // not evenly: the cutoff follows the breath as it swells, and
            // past mezzo-forte it flares, the drive and the attack's opening
            // climbing steeply, the blat of a fortissimo. A slurred note flares from its own
            // breath as a tongued one does, and a held note stays as bright
            // as it is blown (its level and the finger's pressure), not only
            // through the attack. The sub is thin: in a low brass note the
            // second to fourth harmonics stand as tall as the fundamental,
            // which a full sub under it would bury. A hard note's lips
            // catch for a few milliseconds (the pitch wanders) and it
            // scoops up into pitch, by up to two semitones at full twist,
            // settling in about 50 ms. Breath in the tone, the bell in the
            // low mids.
            // Height is the embouchure; the twist is the blare.
            Recipe &s = r[Tuba];
            s.name = "TUBA";
            s.what = "brass oompah, Balkan, second line";
            s.saw = 1, s.octSaw = 0.18f, s.square = 0.12f;
            // The mouthpiece's cup and the bell: a broad "o" in the tone.
            s.formant = 0.6f, s.vowel = 0.6f, s.vowelTone = 0.5f;
            s.sub = 0.22f;
            s.lowThrough = 0.12f;
            s.cutLo = 0.08f, s.cutHi = 0.3f, s.res = 0.05f, s.cutAmp = 0.12f;
            s.envAmt = 0.12f, s.envDecay = 0.07f, s.mEnv = 0.2f;
            s.brassy = 0.75f, s.lipBuzz = 0.8f;
            s.attack = 0.03f, s.decay = 0.7f, s.sustain = 0.82f, s.release = 0.07f;
            s.drive = 0.1f, s.character = 0.5f;
            s.drop = -1.1f, s.dropTime = 0.02f, s.mDrop = -1.2f;
            s.breath = 0.14f, s.breathHz = 950;
            s.lowMid = 0.4f;
            s.glide = 0.04f;
            s.width = 0.12f;
            s.accent = 0.5f;
            s.level = 0.9f;
            s.pump = 0.3f, s.subPump = 0.2f;
            s.knee = 0.24f;
            s.kneeDucks = true;
        }
        {
            // The NES's bass, moving at the console's frame rate. Its
            // triangle channel, a ramp of sixteen steps up and sixteen
            // down with no volume control (the steps band-limited here,
            // so no note aliases), pitched from its 11-bit period table;
            // and over it a pulse whose volume steps down from full to a
            // floor and whose duty steps from 12.5 % to 25 % to 50 % over
            // the first frames, the chirp at the front of every note.
            // Height brings the pulse up; the twist is the arpeggio, the
            // chord's notes once every two frames and then every frame,
            // the octave on top at full twist; an accent is the bitpop
            // kick blip, the triangle's sweep and a burst on the noise
            // channel. Arpeggiated, the triangle and pulse play the chord
            // while the sub holds the root, and under A1 the arpeggio goes
            // up an octave: a frame there is less than a cycle of the note,
            // and a chord of single cycles is a smear, not a sparkle.
            // No filter, no drive: chiptune, 8-bit, bitpop.
            Recipe &s = r[Chip];
            s.name = "CHIP";
            s.what = "NES triangle, frame arpeggios";
            s.chipTri = 1, s.nes = true, s.nesNoise = 0.3f;
            s.pulse = 0.3f, s.pulseTone = 0.6f;
            s.sub = 0.85f;
            s.cutLo = 0.86f, s.cutHi = 1.0f, s.res = 0;
            s.envAmt = 0, s.envDecay = 0.1f;
            s.attack = 0.001f, s.decay = 0.3f, s.sustain = 1, s.release = 0.017f;
            s.drive = 0.02f, s.character = 0;
            s.glide = 0.03f;
            s.accent = 0.3f;
            s.level = 0.85f;
            s.pump = 0.35f, s.subPump = 0.25f;
            s.knee = 0.42f;
            s.kneeDucks = true;
        }
        {
            // The bayan, the tabla's big drum: a skin loaded with paste,
            // struck by the flat of the hand. It sits an octave over the
            // pad, where a bayan speaks (80 to 150 Hz across the field's
            // middle). A sine that swoops up into its note, the "ghe",
            // from a few semitones under it (the twist is how far), the
            // skin's own two lower modes over it (1.59 and 2.14 times the
            // note, a membrane's), and the slap of the palm, a burst of
            // noise around 600 Hz. Pressing harder after the strike bends
            // it up, as the wrist pushes into the skin: the gumki. Pressed
            // lightly it is the "ke", the flat hand laid on the skin, a
            // closed slap with no ring, so ghe and ke alternate from one
            // finger. Every finger strikes. Hindustani, Asian underground.
            Recipe &s = r[Tabla];
            s.name = "TABLA";
            s.what = "bayan: ghe, ke, press to bend";
            s.sine = 1;
            s.sub = 1;
            s.octave = 12;
            s.modes = 0.45f, s.modeTone = 1.2f;
            s.modeRatio[0] = 1.59f, s.modeRatio[1] = 2.14f;
            s.modeDecay[0] = 0.16f, s.modeDecay[1] = 0.08f;
            s.modeLevel[0] = 1, s.modeLevel[1] = 0.55f, s.modeLevel[2] = 0;
            s.slap = 0.55f, s.slapHz = 650, s.slapTime = 0.02f;
            s.damp = true;
            s.cutLo = 0.32f, s.cutHi = 0.8f, s.res = 0.12f;
            s.envAmt = 0.35f, s.envDecay = 0.04f;
            s.attack = 0.001f, s.decay = 0.8f, s.sustain = 0, s.release = 0.15f;
            s.drive = 0.3f, s.character = 0.4f, s.toneDrive = 0.35f, s.driveFirst = true;
            s.drop = -2.0f, s.dropTime = 0.07f, s.mDrop = -4.0f;
            s.gumki = 4;
            s.glide = 0.05f, s.slideSpan = 0.5f, s.restrike = 0.8f;
            s.struck = true;
            s.width = 0.15f;
            s.accent = 0.6f;
            s.level = 1.2f;
            s.pump = 0.5f, s.subPump = 0.45f;
            s.knee = 0.21f;
            s.kneeDucks = true;
        }
        {
            // A didgeridoo: lips buzzing into a eucalyptus tube a termite
            // hollowed out. The lips open slowly and shut fast, a lopsided
            // pulse; the bore, stopped at the mouth and open at the end,
            // rings on the note's odd harmonics. Over that one formant, the
            // tongue and cheeks, swept by the finger's height from about
            // 800 Hz to 2 kHz and pushed on by pressing harder: "wah",
            // "yoi", wherever the finger goes, not on any clock. The twist
            // sings into it, a voice a hair off the drone's fifth, which
            // beats and growls against it, its rattle carried by the throat
            // straight to the mouth (between the bore's peaks, where the tube
            // alone would swallow it); an accent overblows a toot a
            // tenth up. With the roll, the breath pumps on its steps.
            // Australian drone, tribal, psy-dub.
            Recipe &s = r[Didge];
            s.name = "DIDGE";
            s.what = "lip drone, mouth on the finger";
            s.didge = 1, s.toot = 16;
            s.sub = 0.34f;
            s.cutLo = 0.75f, s.cutHi = 0.95f, s.res = 0;
            s.envAmt = 0.05f, s.envDecay = 0.15f;
            s.attack = 0.035f, s.decay = 0.5f, s.sustain = 0.95f, s.release = 0.15f;
            s.drive = 0.05f, s.character = 0.4f;
            s.breath = 0.04f, s.breathHz = 1400;
            s.glide = 0.09f;
            s.width = 0.3f;
            s.accent = 0.2f;
            s.level = 1.85f;
            s.pump = 0.35f;
            s.knee = 0.22f;
            s.kneeDucks = true;
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
        m_lipC = 1.0f - std::exp(-kTwoPi * 50.0f / rate);
        m_lipLpC = 1.0f - std::exp(-kTwoPi * 6000.0f / rate);
        m_growlC = 1.0f - std::exp(-kTwoPi * 1200.0f / rate);
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
                            recipe(n.sound).string <= 0 && !recipe(n.sound).struck;
        if (m_count == kStack)
            remove(m_notes[0].id);
        m_notes[m_count++] = n;
        m_cur = n;
        m_sinceOn = 0;
        m_target = pitchOf(n, h) + 12.0 * double(sh.oct);
        m_chordSeq = h.chordSeq;
        if (legato) {
            m_glideTau = glideTime(n) / 3.0f;
            // A slurred brass note is still blown: its own breath sets how
            // loud it is and how far it flares, so a slurred crescendo
            // blares by its top as a tongued one does.
            if (recipe(n.sound).brassy > 0) {
                m_vel = clampf(n.vel, 0.05f, 1.0f);
                m_accent = n.vel > 0.9f ? 1.0f : 0.0f;
                m_fenv = std::max(m_fenv, brassiness(m_vel));
            }
            return;
        }
        // A drum struck while another note is held slides in from it, the
        // further the longer.
        const bool slideIn = m_count > 1 && recipe(n.sound).struck && m_amp >= 0.02f;
        m_glideTau = slideIn ? struckSlide(n, m_target - m_midi) : 0.0f;
        if (!slideIn)
            m_midi = m_target;
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
            // A string pulls off: the held note at once, no slide. A drum
            // slides back to the held note (render times the slide, once it
            // knows the pitch it goes to) and strikes it again, softly: a
            // slide back onto a note that has died away would be silence.
            const Recipe &R = recipe(m_cur.sound);
            m_glideTau = R.string > 0 ? 0.0f : glideTime(m_cur) / 3.0f;
            if (R.struck) {
                m_slideBack = true;
                // The slide is the stroke's movement: no drop or swoop.
                if (R.restrike > 0 && !m_rolling) {
                    trigger(R.restrike, false);
                    m_drop = 0;
                }
            }
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
            for (Mode &md : m_modes)
                md.reset();
            m_modeHit = 0;
            m_blep.reset();
            m_noiseA = m_noiseWas = 0;
            std::fill(std::begin(m_bore), std::end(m_bore), 0.0f);
            m_boreLp = m_toot = m_tootA = m_pump = m_growl = 0;
            m_tootLeft = m_lipLeft = 0;
            m_slapT = 1e9f;
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
        if (m_slideBack) {
            m_slideBack = false;
            m_glideTau = struckSlide(m_cur, m_target - m_midi);
        }

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
        // The gumki: pressing past how hard the note was struck bends it up.
        if (R.gumki > 0)
            bendTarget += R.gumki * clampf((press - m_strikePress - 0.04f) / 0.3f, 0.0f, 1.0f);
        // A string's twist is how long it rings; muted by the palm, or
        // played as a ghost, it is a dead thud of 40 ms.
        float ampDecay = R.mRing != 0 ? R.decay * std::exp2(R.mRing * (morph - 0.3f)) : R.decay;
        const float dead = R.string > 0 ? deadness() : 0.0f;
        ampDecay = lerpf(ampDecay, 0.04f, dead);
        // A damped skin (the "ke") is over in 30 ms.
        ampDecay = lerpf(ampDecay, 0.03f, m_damp);
        // A bar's modes ring longer with the note, by half as much.
        const float modeRing = R.mRing != 0 ? std::exp2(0.5f * R.mRing * (morph - 0.3f)) : 1.0f;
        // Brass past mezzo-forte: how far into the flare this note was played.
        const float brass = R.brassy > 0 ? R.brassy * brassiness(m_vel) : 0.0f;
        const float decayCoef = std::exp(-1.0f / (std::max(0.01f, ampDecay) * rate));
        const float releaseCoef = std::exp(-1.0f / (std::max(0.004f, R.release) * rate));
        // A hard brass note is tongued: it speaks sooner.
        const float attackInc = 1.0f / (std::max(0.0005f, R.attack * (1.0f - 0.6f * brass)) * rate);
        const float envDecay = R.envDecay * (1.0f + morph * R.mDecay) * (1.0f - 0.35f * m_accent) *
                               (R.contour != 0 ? std::exp2(R.contour * (morph - 0.3f)) : 1.0f) *
                               (1.0f - R.toneDecay * tone);
        const float fenvCoef = std::exp(-1.0f / (std::max(0.01f, envDecay) * rate));
        const float dropCoef = std::exp(-1.0f / (std::max(0.005f, R.dropTime) * rate));
        const float pumpCoef = std::exp(-1.0f / (0.11f * rate));
        const float tootCoef = 1.0f - std::exp(-1.0f / (0.0035f * rate));
        const float drop = (R.drop + morph * R.mDrop) * (1.0f - m_damp);
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
        // The chip's pulse: how much of it height brings in.
        const float pulseL = R.pulse + R.pulseTone * std::max(0.0f, tone - 0.1f);
        // A struck bar's or skin's modes, louder the higher the finger, and
        // none on a damped skin.
        const float modeL = R.modes * std::max(0.0f, 1.0f + R.modeTone * (tone - 0.5f)) * (1.0f - m_damp);
        // The chip's arpeggio, once its twist is past 0.4: the chord's notes
        // (or, not following, the key's triad on the note), every other
        // frame, then every frame, the octave on top past two thirds.
        int arpN = 0, arpFrames = 2;
        float arpSemis[5] = {};
        if (R.nes && morph > 0.4f) {
            const float a = (morph - 0.4f) / 0.6f;
            const int d = m_cur.degree + ((m_cur.flags & Follow) ? chordShift(h) : 0);
            const double base = h.midi(d, m_cur.octave);
            if (m_cur.flags & Follow) {
                const int count = std::clamp(h.chordCount, 1, 4);
                for (int k = 0; k < count; ++k)
                    arpSemis[arpN++] = float(h.midi(d + h.chordTones[k] - h.chordTones[0], m_cur.octave) - base);
            } else {
                for (int k : {0, 2, 4})
                    arpSemis[arpN++] = float(h.midi(d + k, m_cur.octave) - base);
            }
            if (a > 0.66f)
                arpSemis[arpN++] = 12;
            arpFrames = a < 0.33f ? 2 : 1;
        }
        // The didgeridoo's voice, sung into the drone as the twist turns up.
        const float voice = R.didge > 0 ? clampf((morph - 0.3f) / 0.7f, 0.0f, 1.0f) : 0.0f;
        const bool drone = R.didge > 0;
        if (R.breath > 0)
            m_breathBp.set(R.breathHz, 1.1f, rate);
        // A held note sits about 14 dB under full scale, the sub's peak near
        // −10 dB: room for the kick it pumps with, and for the bus.
        // A ghost note is mostly the finger: the note itself 15 dB down.
        const float ghost = R.string > 0 ? ghostness() : 0.0f;
        const float fingerGain = R.level * (0.5f + 0.5f * m_vel) * 0.42f;
        const float topGain = fingerGain * (1.0f - 0.82f * ghost);
        // A damped skin is mostly the hand: its sub 8 dB down.
        const float subGain = R.sub * (0.86f + 0.14f * m_vel) * (1.0f + 0.25f * fat) * 0.235f * (1.0f - 0.82f * ghost) *
                              (1.0f - 0.6f * m_damp);
        const float topT = topGain * (1.0f + 0.8f * m_accent * R.accent);
        const float gainC = 1.0f - std::exp(-1.0f / (0.0015f * rate));
        const float cutGlideC = 1.0f - std::exp(-8.0f / (0.0015f * rate));
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
                        // A drone isn't struck again: its breath pumps.
                        if (drone && m_gate)
                            m_pump = lean ? 1.0f : 0.8f;
                        else
                            trigger(lean ? 1.0f : 0.86f, false);
                        m_gateEnd = (double(step) + gateLen * double(untilNext(pattern, within))) / double(steps);
                    }
                }
                if (m_gate && beat >= m_gateEnd && gateLen < 0.999f && !drone)
                    closeGate();
            }

            // Every eight samples: the filter, the vowel, the drive.
            if ((i & 7) == 0) {
                // The chip's pitch is the frame's (its arpeggio and blip
                // included), and a toot's the note it overblows, so the
                // crossover follows the note that sounds.
                const float hz0 = R.nes ? m_nesRootHz : midiToHz(m_midi + double(m_bend + R.toot * m_toot));
                if (wobDepth > 0.001f) {
                    const double ph = beat / kRateBeats[wobRate];
                    lfo = 0.5f - 0.5f * std::cos(kTwoPi * float(ph - std::floor(ph)));
                } else {
                    lfo = 0;
                }
                // A brass note held loud stays loud in the bell: the flare
                // rides the breath still going in (the note's level and the
                // finger's pressure), not only the attack's opening.
                const float flareHeld = m_amp * (0.3f + 0.6f * press);
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
                envAmt = (R.envAmt + morph * R.mEnv) * (1.0f + 1.4f * m_accent * R.accent) + 0.9f * brass;
                cut += envAmt * m_fenv * 0.6f + R.cutAmp * m_amp * (0.3f + 0.7f * m_vel) +
                       brass * m_amp * (0.1f + 0.2f * press);
                // Struck again while it rings, the filter opens over a
                // couple of milliseconds: its stages hold the last note, and
                // a cutoff that leaps at once leaps the waveform with it.
                if (m_cutGlide > 0) {
                    cutNow += (clampf(cut, 0.0f, 1.0f) - cutNow) * cutGlideC;
                    m_cutGlide -= 8;
                } else {
                    cutNow = clampf(cut, 0.0f, 1.0f);
                }
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
                                  m_accent * 0.15f * R.accent + std::max(0.0f, press - 0.55f) * 0.5f +
                                  brass * (0.05f + 0.8f * std::max(m_fenv, 0.4f * flareHeld));
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
                    tuneString(midiToHz(m_midi + double(m_bend + drop * m_drop)), tone, R.ring, dead);
                if (R.modes > 0)
                    for (int k = 0; k < 3; ++k)
                        m_modes[k].set(hz0 * R.modeRatio[k], R.modeDecay[k] * modeRing, rate);
                if (drone) {
                    // The mouth: height sweeps it from 800 Hz to 2 kHz,
                    // pressing harder, a breath's pump and a toot push it on.
                    const float open = tone + 0.6f * std::max(0.0f, press - 0.5f) + 0.25f * m_pump + 0.1f * m_toot;
                    const float fm = std::min(800.0f * std::pow(2.5f, open), 2400.0f);
                    m_mouth.set(fm, 4.5f, rate);
                    // The lips' buzz falls away up high; the mouth opening
                    // there is louder for it, so the higher it goes the
                    // brighter it sounds.
                    m_mouthGain = std::min(2.4f * std::pow(fm / 800.0f, 2.3f), 20.0f);
                }
            }

            // The levels and the drive a strike sets glide there over about
            // 2 ms. Struck again while the last note rings, a harder or
            // accented note (or a lighter, damped one) would otherwise step
            // the waveform within a sample: a click on every re-strike that
            // a first strike, rising from silence, never has.
            if (m_gainSnap) {
                m_gainSnap = false;
                m_gTop = topT, m_gSub = subGain, m_gMode = modeL, m_gDrive = g, m_gComp = comp;
            } else {
                m_gTop += (topT - m_gTop) * gainC;
                m_gSub += (subGain - m_gSub) * gainC;
                m_gMode += (modeL - m_gMode) * gainC;
                m_gDrive += (g - m_gDrive) * gainC;
                m_gComp += (comp - m_gComp) * gainC;
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
            m_pump *= pumpCoef;
            m_midi += (m_target - m_midi) * double(glideCoef);
            m_bend += (bendTarget - m_bend) * bendCoef;

            // Oscillators. A hard brass note's lips catch for its first
            // 40 ms: the pitch wanders by up to half a semitone and the
            // buzz flutters, less and less.
            float wander = 0, flutter = 1;
            if (m_lipLeft > 0) {
                const float k = float(m_lipLeft--) / m_lipLen;
                m_lipN += m_lipC * (m_rng.bipolar() - m_lipN);
                wander = R.lipBuzz * (0.3f + brass) * k * k * 16.0f * m_lipN;
                flutter = 1.0f + 1.5f * wander;
            }
            // A toot: up a tenth for 140 ms, there and back in a few, on
            // an S-curve (two smoothings), so the overblow leaves the drone
            // and lands on the tenth without a corner in the pitch or level.
            if (R.toot > 0) {
                m_tootA += ((m_tootLeft > 0 ? 1.0f : 0.0f) - m_tootA) * tootCoef;
                m_toot += (m_tootA - m_toot) * tootCoef;
                if (m_tootLeft > 0)
                    --m_tootLeft;
            }
            float hz;
            if (R.nes) {
                // Once a frame, as the console's sound driver ran: the pitch
                // latched from the period table, the pulse's volume and duty.
                if (m_frameLeft <= 0) {
                    m_frameLeft += rate / 60.0f;
                    nesFrame(arpSemis, arpN, arpFrames);
                }
                m_frameLeft -= 1;
                hz = m_nesHz;
            } else {
                hz = midiToHz(m_midi + double(m_bend + drop * m_drop + wander + R.toot * m_toot));
            }
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
            // The chip's sub keeps to the root while its arpeggio runs.
            float sSub = s1;
            if (R.nes) {
                m_subPh += std::min(m_nesRootHz / rate, 0.45f);
                if (m_subPh >= 1.0f)
                    m_subPh -= 1.0f;
                // Stepping back as the arpeggio starts, over 2 ms, not at once.
                m_arpMix += ((arpN > 0 ? 1.0f : 0.0f) - m_arpMix) * gainC;
                sSub = std::sin(kTwoPi * m_subPh) * (1.0f - 0.3f * m_arpMix);
            }
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
            if (R.chipTri > 0 || pulseL > 0) {
                // Arpeggiated, the channels play a chord over the sub, which
                // holds the note, and the chord's frames come through the
                // crossover too. Both step back (here and at the sub), so the
                // chord and the note together peak no higher than the note.
                const float c = chipOsc(ph, inc, R.chipTri, pulseL * (R.nes ? m_nesVol : 1.0f),
                                        R.nes ? m_nesDuty : 0.25f, R.nesNoise);
                osc += (1.0f - 0.4f * m_arpMix) * c;
            }
            if (R.didge > 0) {
                osc += R.didge * didgeridoo(ph, inc, voice, tone);
                // The voice's own note, a fifth over the drone, sits under
                // the crossover at low notes: it is sung into the sub too, so
                // the growl's lowest beat is there.
                if (voice > 0)
                    sSub += kVoiceSub * voice * std::sin(kTwoPi * m_voicePh);
            }
            if (R.modes > 0) {
                // Struck: one sample's impulse into each resonator, which
                // then rings as a sine starting from zero, so it clicks no
                // more than a mallet does.
                const float hit = m_modeHit;
                m_modeHit = 0;
                float m = 0;
                for (int k = 0; k < 3; ++k)
                    if (R.modeLevel[k] > 0)
                        m += R.modeLevel[k] * m_modes[k].step(hit);
                osc += m_gMode * m;
            }
            if (R.breath > 0) {
                // Breath: steady with the note, and a chiff as it starts.
                const float b = m_breathBp.k * m_breathBp.step(m_rng.bipolar()).band;
                osc += R.breath * 3.0f * (m_amp + 1.5f * m_fenv) * b;
            }

            osc *= flutter;

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
                y = ladder(m_os.process(x * m_gDrive, inner) * m_gComp, R.heat);
            }
            else if (character > 0.5f) // folding: four times the rate here too
                y = m_os.process(ladder(x, R.heat) * m_gDrive, [&](float v) { return m_os4.process(v, shapeFn); }) * m_gComp;
            else
                y = m_os.process(ladder(x, R.heat) * m_gDrive, shapeFn) * m_gComp;
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

            // A drone rolled breathes: down between the pattern's pumps. A
            // toot, an octave and a third up, is as loud with less.
            float amp = drone && m_rolling ? m_amp * (0.5f + 0.6f * m_pump) : m_amp;
            if (drone)
                amp *= 1.0f - 0.5f * m_toot;
            const float mute = m_mute.step(muteTarget);
            const float dk = duck[std::min(off0 + i, kBlock - 1)];
            const float pumped = 1.0f - R.pump * dk;
            const float topScale = amp * m_gTop * pumped;
            const float top = y * topScale;
            // The sub ducks under the kick harder than the top does.
            const float sub = sSub * amp * m_gSub * pumped * (1.0f - R.subPump * dk);
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
            const float knee = R.kneeDucks ? R.knee * pumped : R.knee;
            const float mag = std::fabs(mono) + std::fabs(side);
            if (mag > knee) {
                const float k = (knee + 0.45f * knee * fastTanh((mag - knee) / (0.45f * knee))) / mag;
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
        return h.midi(d, n.octave) + rootShift(h.tuning) + double(recipe(n.sound).octave);
    }
    // A struck note's slide from a held one: the sound's glide for a short
    // step, drawn out by slideSpan of it for every fifth further.
    float struckSlide(const Note &n, double semitones) const {
        const Recipe &R = recipe(n.sound);
        const float span = R.slideSpan > 0 ? 0.45f + R.slideSpan * float(std::fabs(semitones)) / 7.0f : 1.0f;
        return glideTime(n) / 3.0f * span;
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
            // The triangle starts at its middle step, as the phase does.
            m_triU = 7.5f;
            m_triLvl = 8;
            m_subPh = 0;
            m_voicePh = 0;
            // And from the height and twist it was played with: smoothed
            // from the last note's, its first moments (a scoop, a swoop)
            // would be the last note's.
            m_tone.reset(m_cur.tone);
            m_morph.reset(m_cur.morph);
            m_gainSnap = true;
            m_cutGlide = 0;
        } else {
            m_cutGlide = int(0.01f * m_rate);
        }
        m_vel = clampf(m_cur.vel * velScale, 0.05f, 1.0f);
        m_accent = accent || m_cur.vel > 0.9f ? 1.0f : 0.0f;
        m_stage = Attack;
        m_fenv = 1;
        m_drop = 1;
        m_strikePress = m_cur.pressure;
        const Recipe &R = recipe(m_cur.sound);
        if (R.string > 0)
            pluckString();
        // Pressed lightly, a skin that damps is the closed "ke".
        m_damp = R.damp ? clampf((0.56f - m_vel) / 0.14f, 0.0f, 1.0f) : 0.0f;
        if (R.modes > 0)
            m_modeHit = 1.0f + 0.5f * m_accent;
        if (R.slap > 0) {
            // The palm on the skin, harder accented, and most of a ke.
            m_slapT = 0;
            m_slapLen = R.slapTime * m_rate;
            m_slapAmp = R.slap * (0.5f + 0.5f * m_vel) * (1.0f + 0.8f * m_accent) * (1.0f + 3.0f * m_damp);
            m_slapBp.set(R.slapHz, 1.1f, m_rate);
            m_slapBp2.set(R.slapHz, 0.9f, m_rate);
            m_slapLp.set(3500.0f, 0.6f, m_rate);
            m_noiseLeft = std::max(m_noiseLeft, int((R.slapTime + 0.02f) * m_rate));
        }
        if (R.lipBuzz > 0) {
            m_lipLen = 0.04f * m_rate;
            m_lipLeft = int(m_lipLen);
        }
        // The chip starts its frames again, and an accent is its kick blip.
        m_frame = 0;
        m_frameLeft = 0;
        m_blip = R.nes && m_accent > 0;
        if (R.toot > 0 && m_accent > 0)
            m_tootLeft = int(0.14f * m_rate);
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
        // A drone sounds at once and keeps sounding; the roll only pumps it.
        if (recipe(m_cur.sound).didge > 0) {
            if (!m_gate || m_amp < 0.02f)
                trigger(1, m_cur.flags & Accent);
            m_pump = kPatterns[pattern].steps[within] == 'x' ? 1.0f : 0.0f;
            return;
        }
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
        // The palm's slap on a skin: brighter than the finger's sounds, so
        // not under their low-pass, but a palm on a loaded skin, not a
        // hiss: through two band-passes (a 4-pole skirt) and its own
        // low-pass near 3.5 kHz, so nothing of it reaches the top octaves.
        float sl = 0;
        if (m_slapT < m_slapLen) {
            const float w = std::sin(kPi * m_slapT / m_slapLen);
            sl = m_slapAmp * w * w * m_rng.bipolar();
            m_slapT += 1;
        }
        --m_noiseLeft;
        // A gentle low-pass after them both: a finger is soft, and noise
        // would otherwise leave a hiss on the band-passes' skirts.
        return m_fingerLp.step(m_thumpBp.k * 3.0f * m_thumpBp.step(t).band + m_knockBp.k * 3.0f * m_knockBp.step(k).band) +
               m_slapLp.step(m_slapBp2.k * m_slapBp2.step(m_slapBp.k * 3.5f * m_slapBp.step(sl).band).band).low;
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

    // --- the chip's oscillators ----------------------------------------------
    //
    // The NES's triangle channel, a ramp through sixteen levels up and down
    // in phase with the sub, and its pulse. Every step and edge is a jump, and a jump has harmonics past
    // any rate. With no filter after them to hide what folds back, each is
    // band-limited with a four-point BLEP (the integral of a cubic
    // B-spline), cleaner than the two-point one the saws use: its residual
    // starts two samples before the jump, so these come out two samples
    // late (about a degree at the bass's highest notes).
    struct Blep4 {
        float q[4] = {}; // the samples n − 2 … n + 1, n the newest
        // The next sample: the naive value of n is added to q[2] after this.
        void shift() {
            q[0] = q[1];
            q[1] = q[2];
            q[2] = q[3];
            q[3] = 0;
        }
        // A jump of j, d samples before sample n (0 ≤ d < 1).
        void add(float j, float d) {
            const float x = d - 1.0f; // sample n − 1, before the jump
            const float d2 = d * d, x2 = x * x;
            q[0] += j * d2 * d2 * (1.0f / 24.0f);
            q[1] += j * (0.5f + (4.0f * x - 2.0f * x2 * x - 0.75f * x2 * x2) * (1.0f / 6.0f));
            q[2] -= j * (0.5f + (-4.0f * d + 2.0f * d2 * d - 0.75f * d2 * d2) * (1.0f / 6.0f));
            const float e = 1.0f - d;
            q[3] -= j * e * e * e * e * (1.0f / 24.0f);
        }
        void reset() { std::fill(std::begin(q), std::end(q), 0.0f); }
    };
    inline float chipOsc(float ph, float inc, float triL, float pulseL, float pw, float noiseL) {
        m_blep.shift();
        float naive = 0;
        if (triL > 0) {
            const float tri = ph < 0.25f ? 4.0f * ph : ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f;
            const float u = (tri + 1.0f) * 7.5f;
            const int lvl = std::clamp(int(u + 0.5f), 0, 15);
            naive += triL * (float(lvl) / 7.5f - 1.0f);
            if (lvl != m_triLvl) {
                // Where between the two samples it crossed the step's edge.
                const float edge = 0.5f * float(lvl + m_triLvl);
                const float du = u - m_triU;
                const float t = std::fabs(du) > 1e-6f ? clampf((edge - m_triU) / du, 0.0f, 1.0f) : 0.5f;
                m_blep.add(triL * float(lvl - m_triLvl) / 7.5f, std::min(1.0f - t, 0.9999f));
            }
            m_triLvl = lvl;
            m_triU = u;
        }
        if (pulseL > 0) {
            // Less its mean: a narrow pulse is mostly low, and that offset,
            // arriving all at once with the note, would thump through the
            // filters.
            const float a = pulseL * 0.8f;
            naive += a * ((ph < pw ? 1.0f : -1.0f) + 1.0f - 2.0f * pw);
            const float prev = ph - inc;
            if (prev < 0) {
                m_blep.add(2.0f * a, std::min(ph / inc, 0.9999f)); // it rose as the cycle began
                if (pw <= ph)
                    m_blep.add(-2.0f * a, (ph - pw) / inc);
            } else if (prev < pw && pw <= ph) {
                m_blep.add(-2.0f * a, (ph - pw) / inc);
            }
        }
        if (noiseL > 0 && (m_noiseA > 0 || m_noiseWas > 0)) {
            // The noise channel: a 15-bit shift register clocked from its
            // period table, sounding its low bit; every flip a jump, and a
            // frame's new volume a jump at this sample.
            const float a = noiseL * m_noiseA, sign = (m_lfsr & 1) ? -1.0f : 1.0f;
            if (a != m_noiseWas)
                m_blep.add((a - m_noiseWas) * sign, 0.0f);
            m_noiseWas = a;
            const float nInc = m_noiseClock / m_rate;
            m_noisePh += nInc;
            float level = sign;
            while (m_noisePh >= 1.0f) {
                m_noisePh -= 1.0f;
                const int was = m_lfsr & 1;
                m_lfsr = (m_lfsr >> 1) | (((m_lfsr ^ (m_lfsr >> 1)) & 1) << 14);
                if ((m_lfsr & 1) != was) {
                    level = -level;
                    m_blep.add(2.0f * a * level, std::min(m_noisePh / nInc, 0.9999f));
                }
            }
            naive += a * level;
        }
        m_blep.q[2] += naive;
        return m_blep.q[0];
    }

    // One frame of the console's sound driver: the pitch it plays now (the
    // arpeggio's note, the blip's sweep), as the triangle's period register
    // can hold it (the CPU clock over 32 times eleven bits plus one, so
    // high notes land a few cents off, as they do on the console); the
    // pulse's volume, from full down two steps of sixteen a frame to a
    // floor of six; and its duty, 12.5 % for two frames, 25 % for two, then
    // 50 %.
    void nesFrame(const float *arpSemis, int arpN, int arpFrames) {
        static constexpr float kBlip[] = {24, 17, 11, 6, 2};
        // The noise's burst: volume down from full in four frames, its
        // period lengthening, a hiss falling into a thud.
        static constexpr float kNoiseVol[] = {15, 10, 6, 3};
        static constexpr float kNoisePeriod[] = {202, 254, 380, 508};
        const int f = m_frame++;
        const double note = m_midi + double(m_bend) + ((m_blip && f < 5) ? kBlip[f] : 0.0f);
        // Under A1 the arpeggio climbs an octave, the root's frames too.
        const float lift = arpN > 0 && m_midi + double(m_bend) < 33.5 ? 12.0f : 0.0f;
        const float semis = arpN > 0 ? arpSemis[(f / arpFrames) % arpN] + lift : 0.0f;
        auto latch = [](double midi) {
            const float period = clampf(std::round(1789773.0f / (32.0f * midiToHz(midi)) - 1.0f), 0.0f, 2047.0f);
            return 1789773.0f / (32.0f * (period + 1.0f));
        };
        m_nesHz = latch(note + double(semis));
        m_nesRootHz = latch(note);
        m_noiseA = m_blip && f < 4 ? kNoiseVol[f] / 15.0f : 0.0f;
        m_noiseClock = 1789773.0f / kNoisePeriod[std::min(f, 3)];
        m_nesVol = float(std::max(6, 15 - 2 * f)) / 15.0f;
        m_nesDuty = f < 2 ? 0.125f : f < 4 ? 0.25f : 0.5f;
    }

    // Brass's flare: almost nothing up to mezzo-forte (0.7), then steeply
    // on to fortissimo.
    static float brassiness(float vel) {
        const float b = clampf((vel - 0.7f) / 0.3f, 0.0f, 1.0f);
        return b * b * (3.0f - 2.0f * b);
    }

    // --- the didgeridoo ------------------------------------------------------
    //
    // The lips: a Rosenberg pulse, opening over 45 % of the cycle as a
    // half cosine and snapping shut over 22 % (5 % with the lips
    // tightened, the finger high), then closed; what sounds is
    // how fast the flow changes (its slope, as a mouth radiates it), so
    // the snap is a sharp kick each cycle, rich up to where the mouth's
    // formant sits. It shuts with a corner, not a step, so the flow's
    // harmonics fall 12 dB an octave (the slope's 6) and nothing that
    // matters folds back at these pitches. The voice sung into it (a fifth up, a
    // little sharp) rattles the lips (it modulates their flow, which puts
    // sidebands half a note under and over the drone's harmonics, the
    // growl) and is heard itself, beating against the drone's third
    // harmonic. The bore is a delay of half a period fed back inverted,
    // through a gentle low-pass: its peaks are the note's odd harmonics, a
    // tube stopped at one end. Then the mouth's one formant, strong over a
    // little of the bore straight through.
    static constexpr float kGrowl = 2.6f, kVoiceSub = 0.12f;
    inline float didgeridoo(float ph, float inc, float voice, float tone) {
        // Tighter lips (the finger higher) snap shut faster: brighter.
        constexpr float tp = 0.45f;
        const float tn = 0.22f - 0.17f * tone;
        const float flow = ph < tp ? 0.5f - 0.5f * std::cos(kPi * ph / tp)
                                   : ph < tp + tn ? std::cos(0.5f * kPi * (ph - tp) / tn) : 0.0f;
        // Its slope per cycle, scaled so the snap's peak is about 1.
        float g = (flow - m_lipFlow) / std::max(inc, 1e-4f) * (tn / 1.5708f);
        m_lipFlow = flow;
        // The snap is a step in that slope: softened by flesh (two poles
        // near 6 kHz), it kicks without the sharpest spikes.
        m_lipLp[0] += m_lipLpC * (g - m_lipLp[0]);
        m_lipLp[1] += m_lipLpC * (m_lipLp[0] - m_lipLp[1]);
        g = m_lipLp[1];
        float sung = 0, growl = 0;
        if (voice > 0) {
            m_voicePh += inc * 1.5f * 1.006f;
            if (m_voicePh >= 1.0f)
                m_voicePh -= 1.0f;
            const float v = std::sin(kTwoPi * m_voicePh);
            // The rattle's sidebands fall between the bore's peaks, where
            // the tube would swallow them; the throat's own buzz carries
            // them straight to the mouth, so the growl is heard.
            // Throat, not lips: soft above 1.2 kHz, so the growl rumbles
            // and does not spit.
            m_growl += m_growlC * (kGrowl * voice * g * v - m_growl);
            growl = m_growl;
            g *= 1.0f + 0.8f * voice * v;
            sung = 0.3f * voice * (v + 0.35f * std::sin(2.0f * kTwoPi * m_voicePh));
        }
        const float d = clampf(0.5f / std::max(inc, 1e-4f), 2.0f, float(kBore - 4));
        const float pos = float(m_boreW + kBore) - d;
        const int i0 = int(pos);
        const float f = pos - float(i0);
        const float back = lerpf(m_bore[i0 & (kBore - 1)], m_bore[(i0 + 1) & (kBore - 1)], f);
        m_boreLp += 0.55f * (back - m_boreLp);
        const float y = 1.6f * (g + sung) - 0.74f * m_boreLp;
        m_bore[m_boreW] = y;
        m_boreW = (m_boreW + 1) & (kBore - 1);
        // A tenth of it into the drive, which would otherwise flatten the
        // mouth's peak as it rises.
        const float out = y + growl;
        return 0.1f * (0.35f * out + m_mouthGain * m_mouth.k * m_mouth.step(out).band);
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
    // How hard the sounding note was struck: the gumki bends from there.
    float m_strikePress = 0.5f;
    // A struck mode: a two-pole resonator, an impulse in and a decaying
    // sine out, its peak the impulse's size whatever the pitch.
    struct Mode {
        float b1 = 0, b2 = 0, gain = 0, y1 = 0, y2 = 0;
        void set(float hz, float decay, float rate) {
            const float w = kTwoPi * clampf(hz, 20.0f, 0.45f * rate) / rate;
            const float r = std::exp(-1.0f / (std::max(0.005f, decay) * rate));
            b1 = 2.0f * r * std::cos(w);
            b2 = -r * r;
            gain = std::sin(w);
        }
        inline float step(float x) {
            const float y = b1 * y1 + b2 * y2 + gain * x;
            y2 = y1;
            y1 = y;
            return y;
        }
        void reset() { y1 = y2 = 0; }
    };
    Mode m_modes[3];
    float m_modeHit = 0; // the strike, for the next sample
    Svf m_breathBp;
    // How damped the skin was struck (the ke), and the palm's slap.
    float m_damp = 0, m_slapT = 1e9f, m_slapLen = 1, m_slapAmp = 0;
    Svf m_slapBp, m_slapBp2, m_slapLp;
    // A brass note's lips catching: samples left, of how many, and the
    // wander (noise low-passed near 50 Hz).
    int m_lipLeft = 0;
    float m_lipLen = 1, m_lipN = 0, m_lipC = 0.015f;
    // A drum lifted off slides back to the held note, timed once render
    // knows how far.
    bool m_slideBack = false;
    // The chip's frames: samples to the next, which one, and what it set.
    float m_frameLeft = 0;
    int m_frame = 0;
    float m_nesHz = 55, m_nesVol = 1, m_nesDuty = 0.125f;
    // The sub under it holds the note (with the blip, not the arpeggio),
    // on its own phase.
    float m_nesRootHz = 55, m_subPh = 0, m_arpMix = 0;
    // The noise channel: its register, the clock's phase and rate, the
    // volume the frame set and the one last sounded.
    int m_lfsr = 1;
    float m_noisePh = 0, m_noiseClock = 8860, m_noiseA = 0, m_noiseWas = 0;
    bool m_blip = false;
    // The didgeridoo: its bore, its mouth, the voice sung into it, the
    // toot's samples left and how far up it is, and the breath's pump.
    static constexpr int kBore = 2048;
    float m_bore[kBore] = {};
    int m_boreW = 0;
    float m_boreLp = 0, m_voicePh = 0, m_toot = 0, m_tootA = 0, m_pump = 0, m_lipFlow = 0, m_mouthGain = 2.4f;
    float m_lipLp[2] = {}, m_lipLpC = 0.3f, m_growl = 0, m_growlC = 0.15f;
    int m_tootLeft = 0;
    Svf m_mouth;
    // The levels and drive a strike set, gliding there (snapped from silence).
    float m_gTop = 0, m_gSub = 0, m_gMode = 0, m_gDrive = 1, m_gComp = 1;
    bool m_gainSnap = true;
    int m_cutGlide = 0; // samples the filter still glides open after a re-strike
    // The chip's triangle: the step it is on and where it was; and what
    // its band-limiting holds of the samples to come.
    float m_triU = 7.5f;
    int m_triLvl = 8;
    Blep4 m_blep;
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
