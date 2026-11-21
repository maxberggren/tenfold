#pragma once

// The FX page's sound: everything a build-up and a drop are made of,
// synthesised. Five risers that peak exactly on a downbeat (a noise sweep, a
// pitch riser, a supersaw stab build, a snare roll and a reverse swell), the
// hits that land on the ONE (impact, sub drop), the moves that come after it
// (downlifter, laser zap, power-down) and three mix moves that have no sound
// of their own (echo throw, reverb wash, the DJ sweep), which the page plays
// on the studio's master bus and the other parts' channels.
//
// The engine keeps the build's one fact: the downbeat it is aiming at, the
// drop. Every riser knows its drop, and when the transport reaches it the
// engine fires the impact and the sub on that exact sample. The page reads
// the drop back from what the engine publishes and moves the mix around it,
// so a build replayed from a clip moves the mix exactly as the live one did.
//
// All of it comes in ten sets, one sound per genre (see SetId below): EDM,
// analog, dub, lo-fi, trap, drum and bass, cinema, synthwave, techno and
// ambient.

#include <algorithm>
#include <cmath>

#include "electro.h"
#include "fx.h"
#include "part.h"

namespace tf::risers {

// What a finger or key plays. The first five are the build field's columns,
// the rest the hit row's cells, left to right.
enum Kind : int {
    Noise = 0, // white-noise sweep with a comb that whistles up to the tonic
    Pitch,     // a saw riser gliding two octaves up onto a scale note
    Saw,       // the chord of the moment as supersaw stabs that speed up
    Roll,      // a snare roll: quarters, eighths, sixteenths, thirty-seconds
    Swell,     // a reverse cymbal and reverse chord, sucked into the ONE
    Impact,    // boom, crash and a falling tail: the drop itself
    Sub,       // an 808 sub that dives an octave onto the key's root
    Down,      // a downlifter: noise and pitch falling away
    Zap,       // a laser: a pitch dive onto a chord tone; held, it fires sixteenths
    Stop,      // power-down: the mix slows into darkness until the next ONE
    Throw,     // echo throw: every part into the tape echo, held or for a beat
    Wash,      // reverb wash: every part into a huge shimmering room
    Sweep,     // the DJ filter on the whole mix, left low-pass, right high-pass
    KindCount
};
constexpr int kRisers = 5;
constexpr int kHits = KindCount - kRisers;
inline bool isRiser(int k) { return k >= 0 && k < kRisers; }

inline const char *kindName(int k) {
    static const char *names[KindCount] = {"noise", "pitch", "saw",   "roll",  "swell", "impact", "sub",
                                           "down",  "zap",   "stop", "throw", "wash",  "sweep"};
    return k >= 0 && k < KindCount ? names[k] : "";
}

// --- sets ----------------------------------------------------------------------
//
// A set recolours every riser and hit for a genre: the same thirteen kinds,
// played the same way, with another sound. Each voice keeps the set it was
// started in (On's i[3]), so a loop plays in the set it was recorded in
// whatever the hand has chosen since, and a drop's impact is in the set of
// the build that aims at it.
//
// A set is numbers the renderers read, not an engine of its own: filter
// shapes and resonance, oscillator shapes, envelopes, saturation, and sends
// to an echo and a spring the engine shares. EDM's are the defaults, the
// sound the page always had.
enum SetId : int {
    SetEdm = 0,
    SetAnalog,    // square waves and resonance, Juno chorus, spring-reverb crash
    SetDub,       // siren, organ skank, spring splash, tape echo throws that run away
    SetLofi,      // dusty: crackle, wow, a sampler's grain behind its output filter, a turntable brake
    SetTrap,      // a whistle, FM bells, hi-hats changing rate, 808 dive, reversed vocal chops
    SetDnb,       // a chopped break into a fill, reese riser, stabs and down-sweep, a rewind
    SetCinema,    // braams, taiko, string swells
    SetSynthwave, // gated-reverb snare, laser toms, chorused brass
    SetTechno,    // ring-modulated metal, acid riser, dub-techno chord, gated swell, clang
    SetAmbient,   // granular shimmer, soft swells, a bloom instead of an impact
    SetCount
};

struct NoiseLook {
    float lp = 1, hp = 1;  // the sweep's low- and high-pass, scaled
    float q = 0;           // resonance added to the low-pass as it opens
    float comb = 1, combFb = 1, combOct = 0; // the whistle: mix, feedback, octaves up
    float air = 1;
    float grain = 0;       // broken into grains, 0..1
    float gate = 0;        // a stutter on the riser's grid, 0..1
    float gain = 1;
    float ring = 0;        // ring-modulated by two squares at an unrelated ratio: metal, 0..1
};
struct PitchLook {
    int shape = 0;         // 0 saw, 1 square, 2 sine
    float oct = 0, glide = 24, detune = 1, fifth = 1, vib = 1;
    float siren = 0;       // a square LFO between the note and its fifth, 0..1
    float sirenRate = 2;   // its cycles per beat at the start (it quickens)
    float cut = 1, res = 0, gain = 1;
    float acid = 0;        // its filter struck again on every sixteenth, 0..1
};
struct StabLook {
    int shape = 0;         // 0 saw, 1 square, 2 electric piano, 3 bell (FM at 1:3.5)
    float oct = 0, detune = 1, cut = 1, res = 0;
    float decay = 1, depth = 1, sub = 1;
    float offbeat = 0;     // 1: the stabs fall between the grid's steps
    float gain = 1;
};
struct RollLook {
    float body = 1, oct = 0, drop = 0.6f, ring = 1; // the drum's tone: level, octave, pitch drop, length
    float wires = 1, wireHz = 1, decay = 1, clap = 1;
    float hat = 0;         // 0..1 from snare wires to a closed hi-hat
    float gate = 0;        // seconds a hit holds before it is cut (gated reverb)
    float triplet = 0, ghost = 0, gain = 1;
    float room = 1;        // its send to the room, against the set's
    float climb = 1;       // how far up the scale the body climbs as it speeds up
    float hp = 1, hpRise = 1; // the high-pass under it, and how far it climbs
    int pattern = 0;       // 0 a roll; 1 a chopped break, then a fill; 2 hi-hats changing rate every beat
};
struct SwellLook {
    float cym = 1, pad = 1, oct = 0, metal = 1, cut = 1, gain = 1;
    float noise = 1;       // the cymbal's noise against its square-wave metal
    float gate = 0;        // chopped in sixteenths, 0..1
};
struct ImpactLook {
    float boom = 1, boomDecay = 1;
    float body = 1, bodyHz = 1, bodyDecay = 1, snap = 1;
    float crash = 1, crashDecay = 1, top = 1, metal = 1, noise = 1, tail = 1;
    float gate = 0;        // the crash cut after this many seconds (gated reverb)
    float spring = 0;      // send to the spring
    // A chord of detuned saws through a ladder that opens and closes with
    // its envelope: a braam when quick and low, a bloom when slow and high.
    float pad = 0, padOct = 0, padChord = 0, padAttack = 0.01f, padDecay = 1.5f, padOpen = 2500;
    float gain = 1;
    float padDrive = 1.8f;
    float padBlare = 0;    // a band around 1.4 kHz pushed into the drive as it swells: the brass edge
};
struct SubLook {
    float dive = 12, diveTime = 1, drive = 0, decay = 1, reese = 0, gain = 1;
    float swell = 0;       // seconds it takes to swell in (0: struck)
    float body = 0;        // a tom's skin an octave up, struck and gone in a quarter second
};
struct DownLook {
    float noise = 1, tone = 0.25f, detune = 0.1f, q = 1.6f, range = 36, len = 1, gain = 1;
};
struct ZapLook {
    int mode = 0;          // 0 a laser (or tom), 1 a reversed vocal chop
    float fall = 40, fallTime = 1, decay = 1, fm = 1, square = 0.25f, oct = 0, click = 0, gain = 1;
};
struct StopLook {
    float range = 36, cut = 1, sub = 0.7f, gain = 1;
    float brake = 0;       // seconds to stop dead, as a turntable does (0: it winds down to the bar line)
    float square = 0, res = 0.7f; // a square instead of a saw, and its filter's resonance
    float rewind = 0;      // 1: over the brake's time it spins back up instead, a DJ's rewind
};
struct MoveLook {
    float throwFb = 0, throwTail = 1, shimmer = 0, sweepDrive = 0;
};

struct Palette {
    const char *name;  // for notices and help
    const char *label; // its chip
    const char *names[KindCount] = {}; // what the pad calls a kind in this set, where it differs
    // The voice's colour, after its own sound.
    float drive = 0;   // saturation
    float lowpass = 0; // a one-pole low-pass, Hz (0: open)
    float crush = 0;   // sample-and-hold rate, Hz (0: none)
    float wow = 0;     // tape wow, semitones
    float chorus = 0;  // a Juno-style chorus, its mix
    float crackle = 0; // vinyl
    float room = 1;    // send to the FX room
    float echo = 0;    // send to the tape echo (dotted eighths)
    float echoFb = 0.5f;
    float spring = 0;  // send to the spring
    NoiseLook noise;
    PitchLook pitch;
    StabLook stab;
    RollLook roll;
    SwellLook swell;
    ImpactLook impact;
    SubLook sub;
    DownLook down;
    ZapLook zap;
    StopLook stop;
    MoveLook moves;
};

// Each set names only what it changes; the rest keeps EDM's defaults.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
// clang-format off
inline const Palette kPalettes[SetCount] = {
    {.name = "EDM", .label = "EDM"},
    {.name = "analog", .label = "ANLG",
     .names = {nullptr, "SQUARE", "POLY", "CR-78", nullptr, "SPRING"},
     .drive = 0.25f, .lowpass = 6000, .wow = 0.05f, .chorus = 0.45f, .room = 0.8f, .spring = 0.12f,
     .noise = {.lp = 0.8f, .q = 5.0f, .comb = 0.25f, .air = 0.4f, .gain = 0.9f},
     .pitch = {.shape = 1, .detune = 0.6f, .fifth = 0.6f, .vib = 0.5f, .cut = 0.6f, .res = 0.8f, .gain = 0.8f},
     .stab = {.shape = 1, .detune = 0.5f, .cut = 0.8f, .res = 0.25f, .decay = 1.6f, .depth = 0.85f, .gain = 0.85f},
     .roll = {.body = 1.4f, .drop = 0.3f, .wires = 0.55f, .wireHz = 1.4f, .decay = 0.6f, .clap = 0},
     .swell = {.cym = 0.6f, .pad = 1.3f, .cut = 0.8f},
     .impact = {.body = 1.1f, .crash = 0.55f, .crashDecay = 0.7f, .top = 0.8f, .tail = 0.3f, .spring = 1.0f},
     .sub = {.drive = 0.25f, .decay = 1.2f},
     .down = {.noise = 0.5f, .tone = 0.6f, .q = 8.0f, .gain = 0.8f},
     .zap = {.fall = 30, .fallTime = 2, .decay = 1.2f, .fm = 0, .square = 1, .gain = 0.8f},
     .stop = {.cut = 0.8f, .square = 1, .res = 4}},
    {.name = "dub", .label = "DUB",
     .names = {nullptr, "SIREN", "SKANK", "RIMS", nullptr, "SPLASH"},
     .drive = 0.15f, .lowpass = 7000, .room = 0.5f, .echo = 0.55f, .echoFb = 0.6f, .spring = 0.25f,
     .noise = {.lp = 0.75f, .q = 1.5f, .comb = 0.2f, .air = 0.6f, .gain = 0.9f},
     .pitch = {.shape = 2, .glide = 12, .detune = 0.2f, .fifth = 0, .vib = 0, .siren = 1, .sirenRate = 2, .cut = 2, .gain = 0.95f},
     .stab = {.shape = 1, .detune = 0.3f, .cut = 1.6f, .decay = 0.45f, .sub = 0, .offbeat = 1, .gain = 1.1f},
     .roll = {.body = 0.8f, .oct = 1, .drop = 0.2f, .wires = 0.4f, .wireHz = 1.6f, .decay = 0.35f, .clap = 0},
     .swell = {.cym = 0.7f, .pad = 0.8f, .cut = 0.8f, .gain = 1.3f},
     .impact = {.boom = 1.2f, .body = 0.8f, .crash = 0.4f, .crashDecay = 0.5f, .tail = 0, .spring = 1.6f},
     .sub = {.dive = 5, .drive = 0.1f, .decay = 1.5f, .swell = 0.6f},
     .down = {.tone = 0.4f, .q = 2.5f, .len = 1.5f, .gain = 0.7f},
     .zap = {.fall = 24, .fallTime = 1.8f, .fm = 0.1f, .square = 0},
     .stop = {.range = 24, .cut = 0.6f},
     .moves = {.throwFb = 0.5f, .throwTail = 2, .shimmer = -0.3f}},
    {.name = "lo-fi", .label = "LOFI",
     .names = {"DUST", nullptr, "KEYS", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "BRAKE"},
     .drive = 0.2f, .lowpass = 4200, .crush = 12000, .wow = 0.15f, .crackle = 0.6f, .room = 0.6f,
     .noise = {.lp = 0.6f, .q = 0.5f, .comb = 0.4f, .air = 0.3f, .gain = 1.15f},
     .pitch = {.shape = 2, .glide = 12, .detune = 0.3f, .fifth = 0.4f, .vib = 0.3f, .cut = 0.6f, .gain = 0.65f},
     .stab = {.shape = 2, .detune = 0, .cut = 0.5f, .decay = 2.0f, .depth = 0.7f, .sub = 0.3f, .gain = 0.8f},
     .roll = {.body = 0.9f, .wires = 0.8f, .wireHz = 0.7f, .decay = 1.2f, .clap = 0.5f},
     .swell = {.cym = 0.4f, .pad = 1.3f, .cut = 0.6f, .gain = 1.3f},
     .impact = {.boom = 1.1f, .snap = 0.4f, .crash = 0.35f, .top = 0.5f, .tail = 0.2f},
     .sub = {.dive = 7, .drive = 0.3f},
     .down = {.noise = 0.6f, .tone = 0.5f, .q = 1.0f, .len = 1.2f},
     .zap = {.fall = 12, .fallTime = 0.6f, .decay = 0.8f, .fm = 0, .square = 0, .gain = 1.3f},
     .stop = {.cut = 0.6f, .sub = 0.9f, .gain = 1.2f, .brake = 0.8f},
     .moves = {.shimmer = -0.5f}},
    {.name = "trap", .label = "TRAP",
     .names = {nullptr, "WHISTLE", "BELLS", "HATS", nullptr, nullptr, "808", nullptr, "CHOP"},
     .drive = 0.1f, .room = 0.7f,
     .noise = {.hp = 1.4f, .comb = 0.5f, .air = 1.2f, .gate = 0.75f, .gain = 1.2f},
     .pitch = {.shape = 2, .oct = 2, .detune = 0, .fifth = 0, .vib = 1.5f, .cut = 3, .gain = 0.8f},
     .stab = {.shape = 3, .oct = 1, .cut = 3, .decay = 1.6f, .sub = 0, .gain = 0.9f},
     .roll = {.body = 0, .wires = 0, .decay = 0.45f, .clap = 0, .hat = 1, .gain = 2.4f, .pattern = 2},
     .swell = {.cym = 1.6f, .pad = 0, .metal = 1.7f, .gate = 0.6f},
     .impact = {.boom = 1.4f, .boomDecay = 2.2f, .body = 0.8f, .snap = 1.6f, .crash = 0.5f, .tail = 0.3f},
     .sub = {.dive = 19, .diveTime = 2.0f, .drive = 0.6f, .decay = 2.5f, .gain = 0.7f},
     .down = {.noise = 0.3f, .tone = 0.9f, .range = 24},
     .zap = {.mode = 1, .gain = 1.3f},
     .stop = {.sub = 1.2f, .gain = 2.0f, .brake = 0.35f}},
    {.name = "drum and bass", .label = "DNB",
     .names = {nullptr, "REESE", nullptr, "AMEN", nullptr, nullptr, nullptr, "SINK", "SCREAM", "REWIND"},
     .drive = 0.2f, .room = 0.8f,
     .noise = {.hp = 1.6f, .q = 3.0f, .comb = 0.9f, .combFb = 1.1f, .combOct = 1},
     .pitch = {.oct = -1, .detune = 0.8f, .fifth = 0, .cut = 0.7f, .res = 0.4f, .gain = 1.1f},
     .stab = {.oct = -1, .detune = 2.5f, .cut = 0.6f, .res = 0.3f, .decay = 1.5f, .depth = 0.6f, .sub = 1.2f, .gain = 0.8f},
     .roll = {.body = 1.1f, .oct = 1, .drop = 0.8f, .wires = 1.1f, .wireHz = 1.2f, .decay = 0.6f, .clap = 0, .ghost = 0.55f,
              .pattern = 1},
     .swell = {.cym = 0.5f, .pad = 1.6f, .oct = -1, .cut = 0.5f},
     .impact = {.boom = 1.1f, .body = 1.3f, .bodyHz = 2.0f, .bodyDecay = 0.35f, .snap = 2.0f, .crash = 0.2f, .crashDecay = 0.25f,
                .top = 1.4f, .tail = 0},
     .sub = {.decay = 1.4f, .reese = 1},
     .down = {.noise = 0.2f, .tone = 1.2f, .detune = 0.3f, .q = 2.5f, .gain = 0.8f},
     .zap = {.fall = 60, .fallTime = 0.5f, .decay = 0.6f, .fm = 3, .square = 0.9f, .oct = 1, .gain = 0.8f},
     .stop = {.sub = 1.0f, .gain = 1.8f, .brake = 0.6f, .rewind = 1}},
    {.name = "cinema", .label = "CINE",
     .names = {"WIND", "STRINGS", "PULSE", "TAIKO", nullptr, "BRAAM"},
     .drive = 0.1f, .chorus = 0.2f, .room = 1.6f,
     .noise = {.lp = 0.7f, .q = 1.0f, .comb = 0.5f, .combOct = -1, .air = 0.5f},
     .pitch = {.oct = -1, .glide = 12, .detune = 1.5f, .fifth = 1.2f, .vib = 1.5f, .cut = 0.5f},
     .stab = {.oct = -1, .detune = 1.5f, .cut = 0.6f, .decay = 2.5f, .depth = 0.5f, .sub = 1.2f, .gain = 0.8f},
     .roll = {.body = 1.8f, .oct = -2, .drop = 1.0f, .ring = 3.5f, .wires = 0.15f, .decay = 1.5f, .clap = 0,
              .room = 0.3f, .climb = 0, .hp = 0.35f, .hpRise = 0},
     .swell = {.cym = 0.5f, .pad = 1.6f, .oct = -1, .cut = 0.6f},
     .impact = {.boom = 0.6f, .boomDecay = 1.6f, .body = 0.9f, .bodyHz = 0.5f, .bodyDecay = 2, .crash = 0.5f,
                .pad = 0.5f, .padOct = 0, .padAttack = 0.08f, .padDecay = 3.0f, .padOpen = 5000, .padDrive = 4.0f, .padBlare = 3.0f},
     .sub = {.dive = 24, .diveTime = 2.5f, .decay = 2.0f, .swell = 0.8f},
     .down = {.noise = 0.8f, .tone = 0.6f, .q = 1.2f, .len = 1.5f},
     .zap = {.fall = 12, .fallTime = 3, .decay = 2, .fm = 0.6f, .square = 0},
     .stop = {.range = 48, .cut = 0.4f, .sub = 1.2f},
     .moves = {.shimmer = 0.3f}},
    {.name = "synthwave", .label = "80S",
     .names = {nullptr, nullptr, "BRASS", "GATED", nullptr, nullptr, nullptr, nullptr, "TOMS"},
     .drive = 0.1f, .chorus = 0.55f, .room = 1.4f, .echo = 0.2f, .echoFb = 0.3f,
     .noise = {.hp = 0.8f, .q = 2.5f, .comb = 0.5f, .air = 0.8f, .gate = 0.4f},
     .pitch = {.shape = 1, .detune = 1.3f, .vib = 0.6f, .cut = 1.2f, .gain = 0.85f},
     .stab = {.detune = 1.2f, .cut = 1.4f, .res = 0.1f, .decay = 2.0f, .depth = 0.5f, .gain = 0.85f},
     .roll = {.wires = 1.2f, .decay = 0.3f, .clap = 1.2f, .gate = 0.12f, .gain = 0.75f},
     .swell = {.cym = 1.4f, .pad = 0.6f, .oct = 1},
     .impact = {.body = 1.2f, .bodyHz = 0.8f, .crash = 1.1f, .tail = 0, .gate = 0.35f},
     .sub = {.dive = 24, .diveTime = 2.0f, .drive = 0.2f, .decay = 0.9f, .body = 1.0f},
     .down = {.noise = 0.2f, .tone = 1.0f, .detune = 0.05f, .range = 48},
     .zap = {.fall = 12, .fallTime = 1.5f, .decay = 2.0f, .fm = 0, .square = 0, .oct = -2, .click = 1.0f, .gain = 1.5f},
     .stop = {.cut = 2.0f, .sub = 0, .brake = 1.5f, .square = 1}},
    {.name = "techno", .label = "TECH",
     .names = {"METAL", "ACID", "CHORD", "CLAPS", nullptr, "CLANG"},
     .drive = 0.35f, .lowpass = 8000, .echo = 0.25f, .echoFb = 0.45f,
     .noise = {.q = 1.0f, .comb = 1.6f, .combFb = 1.3f, .combOct = 2, .gate = 0.5f, .ring = 0.8f},
     .pitch = {.detune = 0.2f, .fifth = 0, .cut = 0.6f, .res = 1.6f, .gain = 0.75f, .acid = 1},
     .stab = {.cut = 0.5f, .res = 0.3f, .decay = 0.6f, .offbeat = 1},
     .roll = {.body = 0.6f, .oct = 1, .wires = 0.8f, .decay = 0.5f, .clap = 2, .hat = 0.3f},
     .swell = {.cym = 1.5f, .pad = 0.2f, .metal = 2.3f, .cut = 0.5f, .gain = 2.6f, .noise = 0.2f, .gate = 0.85f},
     .impact = {.boom = 1.1f, .body = 1.3f, .snap = 1.5f, .crash = 1.1f, .crashDecay = 0.6f, .metal = 0.6f, .noise = 0.2f},
     .sub = {.dive = 7, .drive = 0.5f, .decay = 0.9f},
     .down = {.noise = 0.4f, .tone = 0.8f, .detune = 0.5f, .q = 6, .gain = 0.6f},
     .zap = {.fall = 48, .fallTime = 0.4f, .decay = 0.4f, .fm = 4, .square = 1, .oct = 1, .click = 0.4f, .gain = 1.2f},
     .stop = {.range = 48, .cut = 1.6f, .gain = 0.7f, .square = 1, .res = 6},
     .moves = {.sweepDrive = 0.3f}},
    {.name = "ambient", .label = "AMB",
     .names = {"GRAIN", nullptr, "PAD", "SHAKER", nullptr, "BLOOM"},
     .lowpass = 7000, .chorus = 0.4f, .room = 2.0f, .echo = 0.15f, .echoFb = 0.4f,
     .noise = {.lp = 0.6f, .q = 0.3f, .comb = 1.2f, .combFb = 1.1f, .combOct = 1, .air = 0.6f, .grain = 0.8f, .gain = 1.6f},
     .pitch = {.shape = 2, .glide = 12, .detune = 0.5f, .fifth = 1.5f, .vib = 0.4f, .cut = 0.8f, .gain = 0.9f},
     .stab = {.cut = 0.4f, .depth = 0.15f},
     .roll = {.body = 0.1f, .wires = 0.7f, .wireHz = 1.6f, .decay = 0.5f, .clap = 0, .gain = 2.6f},
     .swell = {.cym = 0.3f, .pad = 1.6f, .oct = 1},
     .impact = {.boom = 0.6f, .body = 0.3f, .snap = 0, .crash = 0, .tail = 0.25f,
                .pad = 0.55f, .padChord = 1, .padAttack = 0.25f, .padDecay = 2.5f, .padOpen = 1800},
     .sub = {.dive = 3, .decay = 1.5f},
     .down = {.noise = 0.7f, .tone = 0.4f, .q = 0.8f, .len = 1.5f},
     .zap = {.fall = 7, .fallTime = 2, .decay = 2, .fm = 0.1f, .square = 0},
     .stop = {.cut = 0.3f, .gain = 1.2f, .brake = 3.0f},
     .moves = {.shimmer = 0.5f}},
};
// clang-format on
#pragma GCC diagnostic pop

inline const Palette &palette(int set) { return kPalettes[std::clamp(set, 0, int(SetCount) - 1)]; }
// What the pad calls a kind in a set: its own name where it has one.
inline const char *kindLabel(int set, int k) {
    if (k < 0 || k >= KindCount)
        return "";
    const char *own = palette(set).names[k];
    return own ? own : kindName(k);
}

// Commands beyond On, Off and Move.
//
//   On    i[0] kind, i[1] bars (a riser's length), i[2] flags, i[3] the set, f[0] pressure,
//         f[1] tone (a slide sideways, −1..1), f[2] intensity 0..1, f[3]
//         motion (twist) 0..1, f[4] grit (a flat finger) 0..1, f[5] beats from
//         the onset to the drop (risers).
//   Move  f[0]..f[4] as in On.
//   Drop  the drop comes at the next bar line: risers rush into it. i[0] the
//         set its impact (and a swell into it) plays in, when no build is
//         coming to give it one.
//   Mark  nothing to play: the page marks the end of a build's take with it
//         (see risers.cpp), and clips replay it as silence.
//   Fall  the riser with this id (or every one, kEveryone) falls away, and a drop
//         no riser aims at any more is called off.
enum : uint16_t { CmdDrop = Command::User, CmdFall = Command::User + 1, CmdMark = Command::User + 2 };
enum : int32_t { FlagEnterLate = 1 }; // a riser joining a drop enters its length before it
// Param i[0]: the drop style, f[0] 0 clean, 1 gap, 2 strip. In a gap (and a
// strip, which ends in one) the risers stop half a beat before the ONE.
enum : int32_t { ParamStyle = 1 };
constexpr double kGapBeats = 0.5;
// Fall's id for every riser at once: the low 24 bits survive a clip's
// rewriting of ids, so a replayed "everything falls away" still means it.
constexpr int32_t kEveryone = 0xFFFFFF;

// The numbers the engine publishes for the page.
namespace pub {
constexpr int Drop = 0;       // the pending drop's beat, or −1
constexpr int Start = 1;      // where its build began
constexpr int Intensity = 2;  // the build's intensity
constexpr int Flash = 3;      // 1 on a drop, fading
constexpr int Layers = 4;     // risers aiming at the drop
constexpr int Throw = 5;      // echo throw: send amount 0..1
constexpr int ThrowTail = 6;  // feedback held up while the echoes ring out
constexpr int ThrowFeedback = 7;
constexpr int Wash = 8;
constexpr int WashShimmer = 9;
constexpr int Sweep = 10;     // −1 low-pass … 0 open … 1 high-pass
constexpr int SweepDrive = 11;
constexpr int StopStart = 12; // power-down from this beat …
constexpr int StopEnd = 13;   // … to this one (−1: none)
constexpr int LastDrop = 14;  // the beat of the last drop that fired
constexpr int SubArmed = 15;  // a sub waits for the drop
constexpr int Hits = 16;      // a flash per hit cell, kHits of them
constexpr int StopRamp = 24;  // a brake: beats until the power-down is all the way down (0: its whole length)
constexpr int Voices = 32;    // then per voice:
constexpr int Stride = 12;
constexpr int MaxVoices = 18;
// Per voice: kind, start, end, progress, level, intensity, tone, who (1 a
// finger, 0 let go, 2 a clip), bars, state, note (MIDI), motion.
enum Field { VKind, VStart, VEnd, VProgress, VLevel, VIntensity, VTone, VWho, VBars, VState, VNote, VMotion };
static_assert(Voices + Stride * MaxVoices <= kPartData, "fits the page's data");
}

enum State : int { Waiting = 0, Running, Falling, Releasing };

// sin(2π·ph) for ph in [0, 1), a parabola refined once: harmonics below
// −60 dB, which no sub or boom will ever show.
inline float sin01(float ph) {
    const float x = 2.0f * ph - 1.0f;
    float y = 4.0f * x * (1.0f - std::fabs(x));
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}

inline float smoothstep(float u) {
    u = clampf(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// The bar line nearest a beat.
inline double nearestBar(double beat) { return std::round(beat / kBeatsPerBar) * kBeatsPerBar; }

// A riser speeds up the way a build does: quarters for its first half, then
// eighths, sixteenths, and thirty-seconds in the last eighth. Long builds
// begin in half notes.
inline double gridFor(float p, int bars) {
    if (bars >= 8 && p < 0.25f)
        return 2.0;
    return p < 0.5f ? 1.0 : p < 0.75f ? 0.5 : p < 0.875f ? 0.25 : 0.125;
}

struct Voice {
    bool active = false;
    int kind = 0, id = -1, bars = 1;
    bool held = false, replay = false;
    int state = Running;
    double start = 0, end = 0; // beats: a riser's run, a hit's onset (and its stop's end)
    double fallAt = 0;
    float fallFrom = 0, fallBeats = 1.5f;
    float p = 0;        // progress, last block
    int delay = 0;      // samples until a hit begins
    float t = 0;        // seconds since a hit began
    double releasedAt = -1;

    // The finger: targets, and the same smoothed per block.
    float press = 0.5f, pressPeak = 0.5f, tone = 0, intensity = 0.6f, motion = 0, grit = 0;
    float sPress = 0.5f, sTone = 0, sInt = 0.6f, sMotion = 0, sGrit = 0;

    float vel = 0.8f; // how hard it began
    float gain = 0;   // the last block's gain, ramped from
    float cut = 1;    // after a riser's peak: a few milliseconds down to nothing
    float level = 0;  // for the page
    float note = 0;   // the pitch it sounds, MIDI, for the page and the tests
    float amount = 0; // a mix move's amount, smoothed

    Rng rng;
    Svf aL, aR, bL, bR, cL, cR;
    OnePoleLp oL, oR;
    bool quiet = false; // an impact whose low end an armed sub plays instead
    Ladder ladL, ladR;
    SawOsc saw[12];
    float ph[8] = {};
    float env = 0, env2 = 0, env3 = 0, env4 = 0, pitchEnv = 0;
    double lastStep = -1;
    int stepCount = 0;
    Delay combL, combR;
    Allpass apL[3], apR[3];
    float glideNote = 0;

    // The set it plays in, and what that set adds to every voice.
    int set = 0;
    SquareOsc sq[10];
    float bend = 0;              // tape wow this block, semitones
    float wowPh = 0, flutterPh = 0, sirenPh = 0;
    float padEnv = 0;            // an impact's braam or bloom
    Svf blareL, blareR;
    bool padRising = false;
    float since = 0;             // seconds since a roll's or a chop's last hit
    Delay chL, chR;              // the chorus
    float chPh = 0;
    Svf postL, postR, post2L, post2R; // the set's low-pass, the last thing it goes through
    DcBlock dcL, dcR;
    float crushPh = 0, holdL = 0, holdR = 0, crack = 0;
};

// A spring tank, roughly: two springs, delays of 33 and 41 ms, each in a
// loop with a chain of stretched first-order allpasses, which hold the
// highs back longer than the lows, so every trip round the loop chirps the
// way a spring's boing does. Mono in, one spring to each side. It rings
// for two to three seconds, and runs only while something goes in or its
// tail is still above −100 dB.
class Spring {
public:
    static constexpr int kStages = 16, kStretch = 4;
    void init(float rate) {
        m_rate = rate;
        static const float lens[2] = {0.033f, 0.041f};
        for (int s = 0; s < 2; ++s) {
            m_loop[s].init(size_t(0.05f * rate));
            m_len[s] = uint32_t(lens[s] * rate);
            m_lp[s].set(6000.0f, rate);
        }
        m_hp.set(160.0f, 0.7f, rate);
        m_tailFall = std::exp(-1.0f / (0.05f * rate));
    }
    // Whether it has anything to do: its tail is still ringing.
    bool busy() const { return m_tail > 1e-5f; }
    inline void process(float in, float &l, float &r) {
        if (in != 0)
            m_tail = 1.0f;
        const float x0 = m_hp.step(in).high;
        float out[2];
        for (int s = 0; s < 2; ++s) {
            float x = x0 + 0.94f * m_lp[s].step(m_loop[s].tap(m_len[s]));
            for (int k = 0; k < kStages; ++k) {
                // y[n] = a·x[n] + x[n−K] − a·y[n−K]
                float *xs = m_x[s][k], *ys = m_y[s][k];
                const float y = m_a * x + xs[m_i] - m_a * ys[m_i];
                xs[m_i] = x;
                ys[m_i] = y;
                x = y;
            }
            x = fastTanh(x);
            m_loop[s].push(x);
            out[s] = x;
        }
        m_i = (m_i + 1) % kStretch;
        l = out[0];
        r = out[1];
        m_tail = std::max(std::max(std::fabs(l), std::fabs(r)), m_tail * m_tailFall);
    }

private:
    float m_rate = 48000, m_a = 0.62f;
    uint32_t m_len[2] = {1600, 2000};
    int m_i = 0;
    float m_tail = 0, m_tailFall = 0.999f;
    Delay m_loop[2];
    float m_x[2][kStages][kStretch] = {}, m_y[2][kStages][kStretch] = {};
    OnePoleLp m_lp[2];
    Svf m_hp;
};

class Engine : public PartEngine {
public:
    static constexpr int kVoices = 18;

    void init(float rate) override {
        m_rate = rate;
        // A riser is gone by the ONE: it lets go 3 ms before the line, in a
        // millisecond and a half, so nothing of it stacks on the kick's peak
        // (which sits at the limiter's ceiling, and would lose what the riser
        // added). Heard, it still peaks on the downbeat.
        m_cutDecay = std::exp(-1.0f / (0.0015f * rate));
        m_busAttack = 1.0f - std::exp(-1.0f / (0.004f * rate));
        m_busRelease = 1.0f - std::exp(-1.0f / (0.18f * rate));
        m_room.init(rate);
        m_room.set(0.5f, 0.0f);
        m_roomHpL.set(220.0f, 0.7f, rate);
        m_roomHpR.set(220.0f, 0.7f, rate);
        m_roomIdle = rate * 10.0f;
        m_echo.init(rate);
        m_spring.init(rate);
        for (int i = 0; i < kVoices; ++i) {
            Voice &v = m_voices[i];
            v.rng = Rng(0x7EA5u + uint32_t(i) * 2654435761u);
            v.combL.init(size_t(rate / 30.0f));
            v.combR.init(size_t(rate / 30.0f));
            v.chL.init(size_t(0.008f * rate));
            v.chR.init(size_t(0.008f * rate));
            static const int ap[3] = {347, 113, 37};
            for (int k = 0; k < 3; ++k) {
                v.apL[k].init(uint32_t(ap[k] * rate / 44100.0f), 0.62f);
                v.apR[k].init(uint32_t(ap[k] * 1.17f * rate / 44100.0f), 0.62f);
            }
        }
    }

    void command(const Command &c, AudioContext &ctx) override {
        const Transport &tr = ctx.transport();
        // The beat the command belongs to: a live one arrives a little late
        // and is placed where the finger actually landed; a clip's arrives
        // early and exactly.
        double onset = tr.beat;
        int offset = 0;
        if (c.t > 0) {
            onset = tr.beat + (c.t - ctx.now()) * tr.bpm / 60.0;
            offset = ctx.offsetOf(c.t, kBlock);
        }
        const bool replay = c.id >= (1 << 24);
        switch (c.type) {
        case Command::On: {
            const int kind = std::clamp(c.i[0], 0, int(KindCount) - 1);
            Voice &v = take(c.id);
            begin(v, c, kind, replay);
            if (isRiser(kind)) {
                v.bars = std::clamp(c.i[1], 1, 16);
                // The drop is always a bar line: the command says roughly how
                // far away, and a clip's onsets, pulled to the grid, land on
                // the same one.
                const double drop = nearestBar(onset + std::max(0.5f, c.f[5]));
                v.end = drop;
                v.start = (c.i[2] & FlagEnterLate) ? std::max(onset, drop - 4.0 * v.bars) : onset;
                if (v.end - v.start < 0.25)
                    v.start = v.end - 0.25;
                v.state = v.start > tr.beat ? Waiting : Running;
                addDrop(drop, v.start, v.intensity, false, v.set);
            } else {
                v.start = onset;
                v.delay = offset;
                v.state = Running;
                v.end = -1;
                if (kind == Stop)
                    v.end = stopEnd(onset);
                m_hitFlash[kind - kRisers] = 1.0f;
                // A sub tapped while a drop is coming is armed onto it: subs
                // belong on the ONE.
                if (kind == Sub)
                    for (const DropPoint &d : m_drops)
                        if (d.active && d.beat > onset + 1.0) {
                            v.state = Waiting;
                            v.start = d.beat;
                            v.held = false;
                            v.releasedAt = d.beat;
                        }
            }
            break;
        }
        case Command::Move:
            for (Voice &v : m_voices)
                if (v.active && v.id == c.id && v.held) {
                    // Clamped as On clamps them: a clip from a file may carry
                    // any number, and the tone picks chord tones by index.
                    v.press = clampf(c.f[0], 0, 1);
                    v.pressPeak = std::max(v.pressPeak * 0.9f, v.press);
                    v.tone = clampf(c.f[1], -1, 1);
                    v.intensity = clampf(c.f[2], 0, 1);
                    v.motion = clampf(c.f[3], 0, 1);
                    v.grit = clampf(c.f[4], 0, 1);
                }
            break;
        case Command::Off:
            for (Voice &v : m_voices)
                if (v.active && v.id == c.id && v.held) {
                    v.held = false;
                    // A lifting finger's pressure falls away in its last
                    // frames; a riser keeps what the finger was doing.
                    v.press = std::max(v.press, v.pressPeak * 0.85f);
                    v.releasedAt = onset;
                }
            break;
        case CmdDrop:
            drop(onset, tr.beat, std::clamp(c.i[0], 0, int(SetCount) - 1));
            break;
        case Command::Param:
            if (c.i[0] == ParamStyle)
                m_gap = c.f[0] >= 0.5f; // gap, and strip, which ends in one
            break;
        case CmdFall: {
            // "Every one" from the hand drops every riser; from a loop, only
            // the risers of that loop's slot and layer, so a Fall in one
            // layer doesn't cut the hand's riser or another layer's.
            const bool everyone = (c.id & kEveryone) == kEveryone;
            for (Voice &v : m_voices)
                if (v.active && isRiser(v.kind) && v.state <= Running &&
                    (everyone ? !isReplay(c.id) || (v.id & ~kEveryone) == (c.id & ~kEveryone) : v.id == c.id))
                    fall(v, tr.beat);
            break;
        }
        default:
            break;
        }
    }

    void render(float *outL, float *outR, int n, AudioContext &ctx) override {
        float *l = outL, *r = outR;
        const Transport &tr = ctx.transport();
        const Harmony &h = ctx.harmony();
        const double b0 = tr.beat, bps = tr.beatsPerSample, b1 = b0 + bps * n;
        m_beat = b0;
        m_bpm = tr.bpm;
        m_cutLead = 0.003 * tr.bpm / 60.0;
        const float smooth = 1.0f - std::exp(-float(n) / (0.04f * m_rate));

        // A drop that falls in this block: the impact and the sub, on its
        // sample.
        for (DropPoint &d : m_drops) {
            if (!d.active)
                continue;
            bool aimed = d.explicit_;
            for (const Voice &v : m_voices)
                aimed |= v.active && isRiser(v.kind) && v.state <= Running && std::fabs(v.end - d.beat) < 0.01;
            if (!aimed) {
                d.active = false;
                for (Voice &v : m_voices)
                    if (v.active && v.kind == Sub && v.state == Waiting && std::fabs(v.start - d.beat) < 0.01)
                        v.active = false;
                continue;
            }
            if (d.beat < b1) {
                const int off = std::clamp(int((d.beat - b0) / bps), 0, n - 1);
                fireDrop(d, off);
                d.active = false;
            }
        }

        // Three buses. Risers breathe with the kick on their own (the
        // impact must never duck itself); risers and hits share a room of
        // their own; the boom and the sub stay dry and mono, out of every
        // room, so the low end of a drop is the kick's and theirs alone.
        // Each voice renders on its own first, so its set can colour it and
        // send it to the room, the echo and the spring as that set likes.
        float rl[kBlock] = {}, rr[kBlock] = {}, hl[kBlock] = {}, hr[kBlock] = {}, dl[kBlock] = {};
        float srl[kBlock] = {}, srr[kBlock] = {}, shl[kBlock] = {}, shr[kBlock] = {};
        float echoIn[kBlock] = {}, springIn[kBlock] = {};
        float echoWant = 0;
        const float *duck = ctx.duck();
        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            const bool riser = isRiser(v.kind);
            if (riser && v.state == Waiting && v.held) {
                float tkl[kBlock] = {}, tkr[kBlock] = {};
                tick(v, tkl, tkr, n, b0, bps);
                for (int i = 0; i < n; ++i) {
                    hl[i] += tkl[i];
                    hr[i] += tkr[i];
                    shl[i] += tkl[i];
                    shr[i] += tkr[i];
                }
            }
            v.sPress += (v.press - v.sPress) * smooth;
            v.sTone += (v.tone - v.sTone) * smooth;
            v.sInt += (v.intensity - v.sInt) * smooth;
            v.sMotion += (v.motion - v.sMotion) * smooth;
            v.sGrit += (v.grit - v.sGrit) * smooth;
            if (!v.held)
                v.pressPeak *= 0.999f;
            const Palette &s = palette(v.set);
            // Tape wow: a slow wander and a faster flutter, a block at a time.
            if (s.wow > 0) {
                v.wowPh += float(n) * 0.55f / m_rate;
                v.flutterPh += float(n) * 5.3f / m_rate;
                v.wowPh -= std::floor(v.wowPh);
                v.flutterPh -= std::floor(v.flutterPh);
                v.bend = s.wow * (sin01(v.wowPh) + 0.25f * sin01(v.flutterPh));
            }
            float vl[kBlock] = {}, vr[kBlock] = {};
            l = vl;
            r = vr;
            bool sounds = true;
            switch (v.kind) {
            case Noise: renderNoise(v, l, r, n, b0, bps, h); break;
            case Pitch: renderPitch(v, l, r, n, b0, bps, h); break;
            case Saw: renderSaw(v, l, r, n, b0, bps, h); break;
            case Roll: renderRoll(v, l, r, n, b0, bps, h); break;
            case Swell: renderSwell(v, l, r, n, b0, bps, h); break;
            case Impact: renderImpact(v, l, r, dl, n, h, duck); break;
            case Sub: renderSub(v, l, r, dl, n, b0, bps, h, duck); break;
            case Down: renderDown(v, l, r, n, b0, bps, h); break;
            case Zap: renderZap(v, l, r, n, b0, bps, h); break;
            case Stop: renderStop(v, l, r, n, b0, bps, h); break;
            case Throw:
            case Wash:
            case Sweep:
                renderMove(v, n, b0);
                sounds = false;
                break;
            default:
                v.active = false;
                sounds = false;
                break;
            }
            if (!sounds)
                continue;
            colour(v, s, vl, vr, n);
            float *dryL = riser ? rl : hl, *dryR = riser ? rr : hr;
            float *sendL = riser ? srl : shl, *sendR = riser ? srr : shr;
            const float spring = v.kind == Impact ? s.impact.spring : s.spring;
            // A gated hit is its own room, held and then cut dead: none of
            // it goes where a tail would ring on past the gate. A roll may
            // want less of the room than the rest of its set (a taiko's
            // weight is its own).
            const bool gated = (v.kind == Roll && s.roll.gate > 0) || (v.kind == Impact && s.impact.gate > 0);
            const float room = gated ? 0.0f : s.room * (v.kind == Roll ? s.roll.room : 1.0f);
            const float echo = gated ? 0.0f : s.echo;
            for (int i = 0; i < n; ++i) {
                dryL[i] += vl[i];
                dryR[i] += vr[i];
                sendL[i] += room * vl[i];
                sendR[i] += room * vr[i];
            }
            if (echo > 0) {
                echoWant = std::max(echoWant, s.echoFb);
                for (int i = 0; i < n; ++i)
                    echoIn[i] += echo * (vl[i] + vr[i]);
            }
            if (spring > 0)
                for (int i = 0; i < n; ++i)
                    springIn[i] += spring * (vl[i] + vr[i]);
        }
        // The echo keeps its feedback while anything sends to it and for
        // eight seconds after, so the repeats ring out rather than stop.
        if (echoWant > 0) {
            m_echoAmount = echoWant;
            m_echoIdle = 0;
        } else if (m_echoAmount > 0 && (m_echoIdle += n) > int(8 * m_rate)) {
            m_echoAmount = 0;
        }
        if (m_echoAmount > 0)
            m_echo.set(m_echoAmount, float(0.75 * 60.0 / m_bpm));
        l = outL;
        r = outR;
        for (int i = 0; i < n; ++i) {
            const float g = 1.0f - 0.32f * duck[i];
            const float L = rl[i] * g + hl[i], R = rr[i] * g + hr[i];
            const float SL = srl[i] * g + shl[i], SR = srr[i] * g + shr[i];
            float wl = 0, wr = 0;
            if (m_roomIdle < m_rate * 8.0f || SL != 0 || SR != 0) {
                m_roomIdle = (SL == 0 && SR == 0) ? m_roomIdle + 1 : 0;
                m_room.process(m_roomHpL.step(SL).high, m_roomHpR.step(SR).high, wl, wr);
            }
            // The room's return ducks under the kick too, so a riser's tail
            // never sits on the ONE; so do the echo's and the spring's.
            const float wet = 0.55f * (1.0f - duck[i]);
            float el = 0, er = 0;
            if (m_echoAmount > 0)
                m_echo.process(echoIn[i], echoIn[i], el, er);
            if (springIn[i] != 0 || m_spring.busy()) {
                float pl, pr;
                m_spring.process(springIn[i], pl, pr);
                el += 0.6f * pl;
                er += 0.6f * pr;
            }
            l[i] = L + wet * wl + (1.0f - duck[i]) * el;
            r[i] = R + wet * wr + (1.0f - duck[i]) * er;
        }
        // The FX bus: a soft compressor, so that five risers stacked at full
        // pressure are a wall, not five times the level. The dry lows join
        // after it, untouched.
        for (int i = 0; i < n; ++i) {
            const float a = std::max(std::fabs(l[i]), std::fabs(r[i]));
            m_busEnv += (a - m_busEnv) * (a > m_busEnv ? m_busAttack : m_busRelease);
            // Held under the studio's glue: a build that already squeezes the
            // master bus leaves the drop nothing to hit with.
            constexpr float threshold = 0.2f;
            const float g = m_busEnv > threshold ? std::pow(threshold / m_busEnv, 0.75f) : 1.0f;
            l[i] = l[i] * g + dl[i];
            r[i] = r[i] * g + dl[i];
        }
        const float fade = std::exp(-float(n) / (0.25f * m_rate));
        m_flash *= fade;
        for (float &f : m_hitFlash)
            f *= fade;
    }

    void silence(bool hard) override {
        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            if (hard) {
                v.active = false;
            } else if (isRiser(v.kind)) {
                fall(v, m_beat);
            } else {
                v.held = false;
                v.releasedAt = m_beat;
            }
        }
        for (DropPoint &d : m_drops)
            d.active = false;
    }

    void publish(float *d) override {
        std::fill(d, d + kPartData, 0.0f);
        const DropPoint *next = nullptr;
        for (const DropPoint &p : m_drops)
            if (p.active && (!next || p.beat < next->beat))
                next = &p;
        d[pub::Drop] = next ? float(next->beat) : -1.0f;
        d[pub::Start] = next ? float(next->start) : 0.0f;
        float intensity = next ? next->intensity : 0.0f;
        int layers = 0;
        d[pub::StopEnd] = -1;
        d[pub::Flash] = m_flash;
        d[pub::LastDrop] = float(m_lastDrop);
        for (const Voice &v : m_voices)
            if (v.active && v.kind == Sub && v.state == Waiting)
                d[pub::SubArmed] = 1;
        for (int k = 0; k < kHits; ++k)
            d[pub::Hits + k] = m_hitFlash[k];
        int w = 0;
        for (const Voice &v : m_voices) {
            if (!v.active)
                continue;
            if (next && isRiser(v.kind) && v.state <= Running && std::fabs(v.end - next->beat) < 0.01) {
                ++layers;
                intensity = std::max(intensity, v.sInt);
            }
            switch (v.kind) {
            // A set leans on the mix moves too: dub's echoes run away, an
            // ambient wash shimmers more, techno's sweep bites.
            case Throw:
                d[pub::Throw] = std::max(d[pub::Throw], v.amount);
                d[pub::ThrowTail] = std::max(d[pub::ThrowTail], v.env2);
                d[pub::ThrowFeedback] = std::max(
                    d[pub::ThrowFeedback], clampf(0.5f + 0.5f * v.sInt + 0.3f * v.sTone + palette(v.set).moves.throwFb, 0, 1));
                break;
            case Wash:
                d[pub::Wash] = std::max(d[pub::Wash], v.amount);
                d[pub::WashShimmer] = std::max(
                    d[pub::WashShimmer], clampf(0.25f + 0.6f * v.sInt + 0.3f * v.sTone + palette(v.set).moves.shimmer, 0, 1));
                break;
            case Sweep:
                if (v.amount > 0.001f) {
                    d[pub::Sweep] = clampf(v.sTone * 1.6f, -1, 1) * v.amount;
                    d[pub::SweepDrive] = std::max(
                        d[pub::SweepDrive],
                        clampf(clampf(v.sPress - 0.6f, 0, 0.4f) * 2.5f + palette(v.set).moves.sweepDrive, 0, 1) * v.amount);
                }
                break;
            case Stop:
                if (v.state != Releasing) {
                    d[pub::StopStart] = float(v.start);
                    d[pub::StopEnd] = float(v.end);
                    d[pub::StopRamp] = palette(v.set).stop.brake * float(m_bpm / 60.0);
                }
                break;
            default:
                break;
            }
            if (w >= pub::MaxVoices)
                continue;
            float *o = d + pub::Voices + pub::Stride * w++;
            o[pub::VKind] = float(v.kind);
            o[pub::VStart] = float(v.start);
            o[pub::VEnd] = float(v.end);
            o[pub::VProgress] = v.p;
            o[pub::VLevel] = v.level;
            o[pub::VIntensity] = v.sInt;
            o[pub::VTone] = v.sTone;
            o[pub::VWho] = v.replay ? 2.0f : v.held ? 1.0f : 0.0f;
            o[pub::VBars] = float(v.bars);
            o[pub::VState] = float(v.state);
            o[pub::VNote] = v.note;
            o[pub::VMotion] = v.sMotion;
        }
        for (; w < pub::MaxVoices; ++w)
            d[pub::Voices + pub::Stride * w + pub::VKind] = -1;
        d[pub::Intensity] = intensity;
        d[pub::Layers] = float(layers);
    }

    // What is sounding, as the studio's Space hears it: an impact counts for
    // its first three beats only, so a take of a build and its drop ends on
    // the bar after the drop, not wherever the crash's tail fades out.
    int voices() const override {
        const float bar = float(3.0 * 60.0 / m_bpm);
        int n = 0;
        for (const Voice &v : m_voices)
            n += v.active && !(v.kind == Impact && v.t > bar);
        return n;
    }

private:
    struct DropPoint {
        bool active = false, explicit_ = false;
        double beat = 0, start = 0;
        float intensity = 0;
        float climb = 1; // how far a rushed build had got: its impact is that big
        int set = 0;     // the set of the build aiming at it: its impact's
    };

    Voice &take(int id) {
        for (Voice &v : m_voices)
            if (v.active && v.id == id && v.held)
                return v;
        for (Voice &v : m_voices)
            if (!v.active)
                return v;
        // Every voice busy: the quietest hit gives way, else the oldest riser.
        Voice *best = nullptr;
        for (Voice &v : m_voices)
            if (!isRiser(v.kind) && (!best || v.level < best->level))
                best = &v;
        if (!best)
            for (Voice &v : m_voices)
                if (!best || v.start < best->start)
                    best = &v;
        return *best;
    }

    void begin(Voice &v, const Command &c, int kind, bool replay) {
        v.active = true;
        v.kind = kind;
        v.id = c.id;
        v.held = true;
        v.replay = replay;
        v.bars = 1;
        v.press = v.sPress = clampf(c.f[0], 0, 1);
        v.pressPeak = v.press;
        v.tone = v.sTone = clampf(c.f[1], -1, 1);
        v.intensity = v.sInt = clampf(c.f[2], 0, 1);
        v.motion = v.sMotion = clampf(c.f[3], 0, 1);
        v.grit = v.sGrit = clampf(c.f[4], 0, 1);
        v.p = 0;
        v.delay = 0;
        v.t = 0;
        v.releasedAt = -1;
        v.gain = 0;
        v.cut = 1;
        v.level = 0;
        v.note = 0;
        v.amount = 0;
        v.env = v.env2 = v.env3 = v.env4 = v.pitchEnv = 0;
        v.lastStep = -1;
        v.stepCount = 0;
        v.glideNote = 0;
        v.aL.reset();
        v.aR.reset();
        v.bL.reset();
        v.bR.reset();
        v.cL.reset();
        v.cR.reset();
        v.ladL.reset();
        v.ladR.reset();
        for (auto &s : v.saw)
            s.phase = v.rng.unit();
        // Oscillators that are a hit's body start at phase zero, so two drops
        // sound the same; risers' start anywhere, as unison should.
        for (float &p : v.ph)
            p = isRiser(kind) ? v.rng.unit() : 0.0f;
        v.quiet = false;
        v.combL.clear();
        v.combR.clear();
        v.vel = 0.55f + 0.9f * v.press;
        v.set = std::clamp(c.i[3], 0, int(SetCount) - 1);
        v.bend = 0;
        v.sirenPh = 0;
        v.padEnv = 0;
        v.padRising = true;
        v.since = 0;
        v.crushPh = v.holdL = v.holdR = v.crack = 0;
        v.postL.reset();
        v.postR.reset();
        v.post2L.reset();
        v.post2R.reset();
        v.dcL = v.dcR = DcBlock{};
        const Palette &s = palette(v.set);
        if (s.lowpass > 0) {
            v.postL.set(s.lowpass, 0.7f, m_rate);
            v.postR.set(s.lowpass, 0.7f, m_rate);
            v.post2L.set(s.lowpass, 0.7f, m_rate);
            v.post2R.set(s.lowpass, 0.7f, m_rate);
        }
        if (s.chorus > 0) {
            v.chL.clear();
            v.chR.clear();
        }
        // Squares share their saws' phases, so a set that swaps one for the
        // other starts the same way.
        for (int k = 0; k < 10; ++k)
            v.sq[k].phase = v.saw[k].phase;
    }

    // A riser or a drop joining a drop that is already coming gives it its
    // set: the last layer of a build says what its impact sounds like.
    void addDrop(double beat, double start, float intensity, bool explicit_, int set) {
        for (DropPoint &d : m_drops)
            if (d.active && std::fabs(d.beat - beat) < 0.01) {
                d.start = std::min(d.start, start);
                d.intensity = std::max(d.intensity, intensity);
                d.explicit_ |= explicit_;
                d.set = set;
                return;
            }
        DropPoint *slot = &m_drops[0];
        for (DropPoint &d : m_drops)
            if (!d.active) {
                slot = &d;
                break;
            }
        *slot = {true, explicit_, beat, start, intensity, 1.0f, set};
    }

    // The drop moves to the next bar line (a click a sixteenth late still
    // means the bar line just gone, and drops at once). Risers on their way
    // to a later drop rush into this one from wherever they are.
    void drop(double onset, double now, int set) {
        const double target = std::ceil((onset - 0.25) / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        DropPoint *pending = nullptr;
        for (DropPoint &d : m_drops)
            if (d.active && (!pending || d.beat < pending->beat))
                pending = &d;
        if (pending && pending->beat <= target + 0.01) {
            pending->explicit_ = true;
            return;
        }
        // A build keeps its own set for its impact; a drop out of nothing
        // takes the hand's.
        if (pending)
            set = pending->set;
        const double from = pending ? pending->beat : -1;
        float intensity = pending ? pending->intensity : 0.7f;
        float climb = pending ? 0.0f : 1.0f;
        double start = std::min(onset, now);
        for (Voice &v : m_voices) {
            if (!v.active || !isRiser(v.kind) || v.state > Running || std::fabs(v.end - from) > 0.01)
                continue;
            const double span = std::max(1e-3, target - now);
            if (v.state == Waiting || now <= v.start) {
                v.start = now;
            } else {
                // Keep the progress it has reached and finish the rest in
                // the time that is left.
                const double p0 = std::clamp((now - v.start) / (v.end - v.start), 0.0, 0.97);
                climb = std::max(climb, float(p0));
                v.start = target - span / (1.0 - p0);
            }
            v.end = target;
            v.state = Running;
            start = std::min(start, v.start);
            intensity = std::max(intensity, v.intensity);
        }
        for (Voice &v : m_voices)
            if (v.active && v.kind == Sub && v.state == Waiting && std::fabs(v.start - from) < 0.01)
                v.start = target;
        if (pending)
            pending->active = false;
        // A drop out of nothing gets a reverse swell into it when there is
        // room for one.
        if (!pending && target - now >= 0.75) {
            Voice &v = take(-1);
            Command c;
            c.id = -1;
            c.i[3] = set;
            c.f[0] = 0.55f;
            c.f[2] = 0.75f;
            begin(v, c, Swell, false);
            v.held = false;
            v.bars = 1;
            v.start = now;
            v.end = target;
            v.state = Running;
        }
        addDrop(target, start, intensity, true, set);
        for (DropPoint &d : m_drops)
            if (d.active && std::fabs(d.beat - target) < 0.01)
                d.climb = pending ? clampf(0.35f + climb, 0.35f, 1.0f) : 1.0f;
    }

    void fall(Voice &v, double now) {
        v.fallFrom = v.state == Waiting ? 0.0f : v.p;
        v.fallAt = now;
        v.fallBeats = v.kind == Roll ? 1.0f : 2.0f;
        v.state = v.state == Waiting ? Releasing : Falling;
        if (v.state == Releasing)
            v.active = false;
    }

    // A power-down ends on the next bar line at least two beats away, or on
    // the drop if one is coming sooner.
    double stopEnd(double onset) const {
        double end = std::ceil((onset + 2.0) / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        for (const DropPoint &d : m_drops)
            if (d.active && d.beat > onset + 1.0 && d.beat < end)
                end = d.beat;
        return end;
    }

    // The drop: the impact on its sample. Its boom gives way to a sub if
    // one was armed onto this drop (they would only fight in one octave).
    // Neither pumps the mix: the kick on the ONE does that, and the lows
    // here duck under it.
    void fireDrop(const DropPoint &d, int offset) {
        m_flash = 1.0f;
        m_lastDrop = d.beat;
        const float intensity = clampf(0.55f + 0.45f * d.intensity, 0.4f, 1.0f);
        const float size = clampf(float(d.beat - d.start) / 16.0f, 0.3f, 1.0f);
        bool armedSub = false;
        for (const Voice &v : m_voices)
            armedSub |= v.active && v.kind == Sub && v.state == Waiting && std::fabs(v.start - d.beat) < 0.01;
        Command c;
        c.id = -2;
        c.i[3] = d.set;
        c.f[0] = 0.5f;
        c.f[2] = intensity;
        Voice &boom = take(-2);
        begin(boom, c, Impact, false);
        boom.held = false;
        boom.delay = offset;
        boom.start = d.beat;
        boom.vel = (0.75f + 0.25f * size) * (0.6f + 0.4f * d.climb);
        boom.quiet = armedSub;
        m_hitFlash[Impact - kRisers] = 1.0f;
    }

    // --- shared pieces -------------------------------------------------------

    // Progress through a riser at beat b, and what a fall has left of it.
    float progressAt(const Voice &v, double b) const {
        if (v.state == Falling) {
            const float u = clampf(float((b - v.fallAt) / v.fallBeats), 0, 1);
            return v.fallFrom * (1.0f - u) * (1.0f - 0.5f * u);
        }
        if (b <= v.start)
            return 0;
        return clampf(float((b - v.start) / std::max(1e-6, v.end - v.start)), 0, 1);
    }
    float fallFade(const Voice &v, double b) const {
        if (v.state != Falling)
            return 1;
        const float u = clampf(float((b - v.fallAt) / v.fallBeats), 0, 1);
        return (1.0f - u) * (1.0f - u);
    }
    // Whether a riser has anything to play in this block; a waiting one
    // starts when the transport reaches its entry.
    bool riserLive(Voice &v, double b0, double b1) {
        if (v.state == Waiting) {
            if (b1 <= v.start)
                return false;
            v.state = Running;
            v.env = 0;
            v.lastStep = -1; // the tick's count is not the riser's grid
        }
        if (v.state == Falling && b0 - v.fallAt >= v.fallBeats) {
            v.active = false;
            return false;
        }
        return true;
    }
    // Per sample: after the drop's sample, a riser goes down in a few
    // milliseconds, so its peak is the ONE.
    inline float cutAt(Voice &v, double b) {
        if (v.state == Running && b >= v.end - m_cutLead - (m_gap ? kGapBeats : 0.0)) {
            v.cut *= m_cutDecay;
            if (v.cut < 1e-4f) {
                v.active = false;
                // Gone into a gap before its line: the drop is still coming.
                for (DropPoint &d : m_drops)
                    if (d.active && std::fabs(d.beat - v.end) < 0.01)
                        d.explicit_ = true;
            }
        }
        return v.cut;
    }
    float pressGain(const Voice &v) const { return 0.55f + 0.9f * v.sPress; }
    static inline float drive(float x, float grit) {
        if (grit < 0.01f)
            return x;
        const float g = 1.0f + 5.0f * grit;
        return fastTanh(x * g) / std::min(g, fastTanh(g) * 1.6f + 0.4f) * (1.0f + 0.3f * grit);
    }
    // The riser's level: −dB·(1−p)^k, so it grows slowly, then all at once.
    static float riseGain(float p, float db, float k) { return dbToGain(-db * std::pow(1.0f - p, k)); }

    // A landing that waits bars to come in ticks softly on each beat while
    // its finger is down, so the player hears it is armed.
    void tick(Voice &v, float *l, float *r, int n, double b0, double bps) {
        const float decay = std::exp(-1.0f / (0.006f * m_rate));
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const double beat = std::floor(b);
            if (beat != v.lastStep) {
                if (v.lastStep >= 0)
                    v.env = 1.0f;
                v.lastStep = beat;
            }
            if (v.env < 1e-4f)
                continue;
            v.ph[7] += 2400.0f / m_rate;
            v.ph[7] -= std::floor(v.ph[7]);
            const float o = sin01(v.ph[7]) * v.env * 0.025f;
            v.env *= decay;
            l[i] += o;
            r[i] += o;
        }
    }

    // The set's colour on one voice's block, in place: a chorus, a low-pass,
    // a sampler's grain, saturation and vinyl crackle, each only where the
    // set asks for it.
    void colour(Voice &v, const Palette &s, float *l, float *r, int n) {
        // EDM's sound is as it always was; the others' shorter stabs and
        // drive can leave an offset, which goes first.
        if (v.set != SetEdm)
            for (int i = 0; i < n; ++i) {
                l[i] = v.dcL.step(l[i]);
                r[i] = v.dcR.step(r[i]);
            }
        if (s.chorus > 0) {
            // Two short delays swung in opposite directions by a slow
            // triangle, each mixed with what went in: the Juno's chorus.
            // Short enough (2–5 ms) that a riser cut on the line is gone by
            // the kick's peak.
            const float base = 0.0035f * m_rate, depth = 0.0015f * m_rate, inc = 0.5f / m_rate;
            const float norm = 1.0f / (1.0f + 0.4f * s.chorus);
            for (int i = 0; i < n; ++i) {
                v.chPh += inc;
                if (v.chPh >= 1.0f)
                    v.chPh -= 1.0f;
                const float tri = 4.0f * std::fabs(v.chPh - 0.5f) - 1.0f;
                v.chL.push(l[i]);
                v.chR.push(r[i]);
                // Linear reads: a chorus's slow sweep needs no more.
                const float dl = base + depth * tri, dr = base - depth * tri;
                const uint32_t il = uint32_t(dl), ir = uint32_t(dr);
                const float fl = dl - float(il), fr = dr - float(ir);
                const float wl = v.chL.tap(il) + fl * (v.chL.tap(il + 1) - v.chL.tap(il));
                const float wr = v.chR.tap(ir) + fr * (v.chR.tap(ir + 1) - v.chR.tap(ir));
                l[i] = (l[i] + s.chorus * wl) * norm;
                r[i] = (r[i] + s.chorus * wr) * norm;
            }
        }
        // An old sampler's low rate: what is above half of it folds back as
        // grit. Its output filter (the set's low-pass, last) takes away the
        // images the steps leave up high.
        if (s.crush > 0) {
            const float inc = s.crush / m_rate;
            for (int i = 0; i < n; ++i) {
                v.crushPh += inc;
                if (v.crushPh >= 1.0f) {
                    v.crushPh -= 1.0f;
                    v.holdL = l[i];
                    v.holdR = r[i];
                }
                l[i] = v.holdL;
                r[i] = v.holdR;
            }
        }
        if (s.drive > 0) {
            // drive(), with what doesn't change hoisted out of the loop.
            const float g = 1.0f + 5.0f * s.drive;
            const float out = (1.0f + 0.3f * s.drive) / std::min(g, fastTanh(g) * 1.6f + 0.4f);
            for (int i = 0; i < n; ++i) {
                l[i] = fastTanh(l[i] * g) * out;
                r[i] = fastTanh(r[i] * g) * out;
            }
        }
        if (s.crackle > 0) {
            // Crackle while the voice sounds: a dozen ticks a second, of
            // random size and mostly small, under the sound rather than on it.
            const float present = clampf(v.level * 6.0f, 0, 1) * s.crackle;
            if (present > 0.001f) {
                const uint32_t chance = uint32_t(12.0 / double(m_rate) * 4294967295.0);
                const float decay = std::exp(-1.0f / (0.0003f * m_rate));
                for (int i = 0; i < n; ++i) {
                    if (v.rng.next() < chance) {
                        const float a = v.rng.bipolar();
                        v.crack = a * a * a * 0.05f * present;
                    }
                    l[i] += v.crack;
                    r[i] += v.crack * 0.8f;
                    v.crack *= decay;
                }
            }
        }
        // Four poles behind a sampler's steps, two otherwise.
        if (s.lowpass > 0 && s.crush > 0)
            for (int i = 0; i < n; ++i) {
                l[i] = v.post2L.step(v.postL.step(l[i]).low).low;
                r[i] = v.post2R.step(v.postR.step(r[i]).low).low;
            }
        else if (s.lowpass > 0)
            for (int i = 0; i < n; ++i) {
                l[i] = v.postL.step(l[i]).low;
                r[i] = v.postR.step(r[i]).low;
            }
    }

    // --- risers --------------------------------------------------------------

    void renderNoise(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm), pe = progressAt(v, b0 + bps * n);
        v.p = p;
        const float c = std::pow(p, 1.4f);
        const float I = v.sInt;
        const NoiseLook &k = palette(v.set).noise;
        // From a breath at 1.4 kHz to the full hiss: never so dark that it
        // can't be heard over a beat, with air above it from the start. A
        // set with resonance makes it sing as it opens, as a synth's filter
        // sweep does.
        const float lpHz = std::min(11000.0f, 1400.0f * k.lp * std::exp2(c * (2.4f + 0.6f * I) + 0.6f * v.sTone));
        const float hpHz = 55.0f * k.hp * std::exp2(c * (4.6f + 0.8f * I) + 0.5f * v.sTone);
        const float q = 0.75f + 1.8f * I * c + k.q * (0.3f + 0.7f * c);
        v.aL.set(lpHz, q, m_rate);
        v.aR.set(lpHz * 1.02f, q, m_rate);
        v.bL.set(hpHz, 0.7f, m_rate);
        v.bR.set(hpHz, 0.7f, m_rate);
        v.cL.set(7500.0f, 0.7f, m_rate);
        v.cR.set(7900.0f, 0.7f, m_rate);
        const float air = (0.25f + 0.35f * c) * k.air;
        // The comb whistles: its delay is the period of a note gliding three
        // octaves up onto the tonic, which it reaches on the ONE.
        const float target = float(h.midi(0, 3)) + 12.0f * std::round(v.sTone * 0.6f) + 12.0f * k.combOct;
        const float midi = target - 36.0f * (1.0f - c) + v.bend;
        v.note = midi;
        const float period = m_rate / midiToHz(std::max(midi, 24.0f));
        const float fb = clampf((0.3f + 0.45f * I + 0.2f * v.sMotion) * k.combFb, 0, k.combFb > 1 ? 0.96f : 0.93f) *
                         (0.5f + 0.5f * c);
        const float combMix = clampf((0.35f + 0.4f * I) * k.comb, 0, 1);
        // A stutter on the riser's grid, quickening with it; and grains: a
        // random scatter of short swells, so the hiss shimmers.
        const double gateGrid = 0.5 * gridFor(p, v.bars);
        const float gateSmooth = 1.0f - std::exp(-1.0f / (0.002f * m_rate));
        const float grainRise = 1.0f - std::exp(-1.0f / (0.006f * m_rate));
        // Metal: the hiss ring-modulated by two sines at an unrelated ratio
        // (√2) under the whistle, so it clangs rather than sings.
        const float ringInc0 = std::min(midiToHz(midi - 12.0f) / m_rate, 0.45f), ringInc1 = ringInc0 * 1.4142f;
        const float grainFall = std::exp(-1.0f / (0.05f * m_rate));
        const uint32_t grainChance = uint32_t(30.0 / double(m_rate) * 4294967295.0);
        const float width = 0.2f + 0.8f * c;
        const float wa = std::sqrt(1.0f - 0.5f * width), wb = std::sqrt(0.5f * width);
        // Twist: an autopan that speeds up with the build, on the grid.
        const double rate = std::exp2(std::floor(c * 3.99f)) * 0.5;
        const float g0 = v.gain;
        const float g1 =
            riseGain(pe, 17.0f, 1.4f) * pressGain(v) * fallFade(v, b0 + bps * n) * (0.9f + 0.4f * I) * 0.25f * k.gain;
        v.gain = g1;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            float g = (g0 + (g1 - g0) * float(i) / float(n)) * cutAt(v, b);
            if (k.gate > 0) {
                const double u = b / gateGrid - std::floor(b / gateGrid);
                v.env4 += ((u < 0.55 ? 1.0f : 1.0f - k.gate) - v.env4) * gateSmooth;
                g *= v.env4;
            }
            if (k.grain > 0) {
                if (v.rng.next() < grainChance)
                    v.env3 = 0.4f + 0.6f * v.rng.unit();
                v.env2 += (v.env3 - v.env2) * grainRise;
                v.env3 *= grainFall;
                g *= 1.0f - k.grain + k.grain * 1.8f * v.env2;
            }
            const float nc = v.rng.bipolar(), n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            float L = nc * wa + n1 * wb, R = nc * wa + n2 * wb;
            const float airL = v.cL.step(L).high * air, airR = v.cR.step(R).high * air;
            L = v.bL.step(v.aL.step(L).low).high + airL;
            R = v.bR.step(v.aR.step(R).low).high + airR;
            const float cl = L + fb * v.combL.read(period);
            const float cr = R + fb * v.combR.read(period * 1.003f);
            v.combL.push(fastTanh(cl));
            v.combR.push(fastTanh(cr));
            L = L + combMix * (cl * (1.0f - fb) * 1.6f - L);
            R = R + combMix * (cr * (1.0f - fb) * 1.6f - R);
            if (k.ring > 0) {
                v.ph[0] += ringInc0;
                v.ph[0] -= std::floor(v.ph[0]);
                v.ph[1] += ringInc1;
                v.ph[1] -= std::floor(v.ph[1]);
                const float rm = 1.0f - k.ring + k.ring * 1.4f * (sin01(v.ph[0]) + sin01(v.ph[1]));
                L *= rm;
                R *= rm;
            }
            if (v.sMotion > 0.02f) {
                const float tri = 2.0f * std::fabs(2.0f * float(b * rate - std::floor(b * rate)) - 1.0f) - 1.0f;
                const float pan = tri * v.sMotion;
                L *= 1.0f - std::max(0.0f, pan);
                R *= 1.0f + std::min(0.0f, pan);
            }
            L = drive(L * g, v.sGrit);
            R = drive(R * g, v.sGrit);
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    void renderPitch(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm), pe = progressAt(v, b0 + bps * n);
        v.p = p;
        const float c = std::pow(p, 1.5f);
        const float I = v.sInt;
        const PitchLook &k = palette(v.set).pitch;
        // It lands on a scale note: the tonic, or the degree the finger slid
        // to, two octaves above where it began.
        const int degree = int(std::lround(v.sTone * 7.0f));
        const float target = float(h.midi(degree, 2)) + 12.0f * k.oct;
        if (v.glideNote == 0)
            v.glideNote = target;
        v.glideNote += (target - v.glideNote) * 0.25f;
        const float fifth = float(h.tuning.degreeSemis(degree + 4) - h.tuning.degreeSemis(degree));
        // A vibrato that speeds up with the build, on the grid: the siren.
        const double vibRate = std::min(12.0, std::exp2(1.0 + 3.0 * c));
        const float vibDepth = (0.06f + 0.5f * v.sMotion) * c * k.vib;
        const float vib = vibDepth * sin01(float(bm * vibRate - std::floor(bm * vibRate)));
        // The dub siren: a squared LFO throws the note up to its fifth and
        // back, faster and faster; both ends are in the key.
        float siren = 0;
        if (k.siren > 0) {
            v.sirenPh += float(k.sirenRate * (1.0 + 3.0 * c) * bps * n);
            v.sirenPh -= std::floor(v.sirenPh);
            // Squared hard, so it is on one note or the other, and let go of
            // in the last stretch so the riser lands on its note. A twist
            // turns it into the classic swept siren: an octave climbed over
            // and over.
            const float c4 = c * c * c * c;
            const float swept = clampf(v.sMotion * 1.5f, 0, 1);
            const float twoNote = fifth * 0.5f * (1.0f + fastTanh(10.0f * sin01(v.sirenPh)));
            const float sweep = 12.0f * std::sqrt(v.sirenPh);
            siren = k.siren * (1.0f - c4 * c4) * (twoNote + swept * (sweep - twoNote));
        }
        const float midi = v.glideNote - k.glide * (1.0f - c) + vib + siren + v.bend;
        v.note = midi;
        const float hz = midiToHz(midi), hz5 = midiToHz(midi + fifth - 12.0f);
        const float cents = (5.0f + 18.0f * c) * k.detune;
        const float d = std::exp2(cents / 1200.0f);
        const float inc[5] = {std::min(hz / m_rate, 0.45f), std::min(hz * d / m_rate, 0.45f),
                              std::min(hz / d / m_rate, 0.45f), std::min(hz5 * d / m_rate, 0.45f),
                              std::min(hz5 / d / m_rate, 0.45f)};
        const float cutoff = std::min(16000.0f, hz * (1.5f + (3.0f + 9.0f * I) * c) * k.cut);
        const float q = 0.8f + 1.4f * I + k.res * (1.0f + 3.0f * c);
        v.aL.set(cutoff, q, m_rate);
        v.aR.set(cutoff, q, m_rate);
        const float g0 = v.gain;
        const float g1 =
            riseGain(pe, 19.0f, 1.3f) * pressGain(v) * fallFade(v, b0 + bps * n) * (0.85f + 0.3f * I) * 0.16f * k.gain;
        v.gain = g1;
        const float f3 = 0.45f * k.fifth, f4 = 0.15f * k.fifth;
        // Acid: the filter struck on every sixteenth and closing again, an
        // accent on each beat, over a darker resting cutoff.
        const float acidDecay = std::exp(-1.0f / (0.07f * m_rate));
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * cutAt(v, b);
            if (k.acid > 0) {
                const double step = std::floor(b * 4.0 + 1e-7);
                if (step != v.lastStep) {
                    v.lastStep = step;
                    v.env = std::fabs(b - std::round(b)) < 0.01 ? 1.3f : 1.0f;
                }
                v.env *= acidDecay;
                if ((i & 7) == 0) {
                    const float fc = std::min(16000.0f, cutoff * (1.0f - 0.7f * k.acid + 2.5f * k.acid * v.env));
                    v.aL.set(fc, q, m_rate);
                    v.aR.set(fc, q, m_rate);
                }
            }
            float o[5];
            if (k.shape == 0) {
                for (int j = 0; j < 5; ++j)
                    o[j] = v.saw[j].step(inc[j]);
            } else if (k.shape == 1) {
                for (int j = 0; j < 5; ++j)
                    o[j] = 0.8f * v.sq[j].step(inc[j]);
            } else {
                // Sines, with a touch of their octave so they speak.
                for (int j = 0; j < 5; ++j) {
                    v.ph[j] += inc[j];
                    v.ph[j] -= std::floor(v.ph[j]);
                    float p2 = 2.0f * v.ph[j];
                    p2 -= std::floor(p2);
                    o[j] = 1.4f * (sin01(v.ph[j]) + 0.2f * sin01(p2));
                }
            }
            float L = o[0] + 1.2f * o[1] + f3 * o[3] + f4 * o[4];
            float R = o[0] + 1.2f * o[2] + f4 * o[3] + f3 * o[4];
            L = drive(v.aL.step(L).low * g, v.sGrit);
            R = drive(v.aR.step(R).low * g, v.sGrit);
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    void renderSaw(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm), pe = progressAt(v, b0 + bps * n);
        v.p = p;
        const float c = std::pow(p, 1.4f);
        const float I = v.sInt;
        const StabLook &k = palette(v.set).stab;
        // The chord of the moment, turned over by the finger's slide.
        const int count = std::clamp(h.chordCount, 1, 3);
        const int inv = int(std::lround(v.sTone * 3.0f));
        float midis[3];
        for (int t = 0; t < count; ++t) {
            const int j = t + inv;
            const int lift = j >= 0 ? j / count : -((count - 1 - j) / count);
            const int idx = j - lift * count;
            midis[t] = float(h.midi(h.chordTones[idx], 1)) + 12.0f * float(lift);
        }
        for (float &m : midis)
            m += 12.0f * k.oct + v.bend;
        // The last beat rises an octave, landing on the chord again.
        const double riseBeats = v.bars >= 2 ? 1.0 : 0.5;
        const float u = v.state == Running ? clampf(float(1.0 - (v.end - bm) / riseBeats), 0, 1) : 0.0f;
        const float lift = 12.0f * u * u;
        v.note = midis[0] + lift;
        const float cents = (6.0f + 22.0f * c) * k.detune;
        static const float spread[3] = {0.0f, -1.0f, 1.0f};
        float inc[9];
        for (int t = 0; t < count; ++t)
            for (int u3 = 0; u3 < 3; ++u3)
                inc[3 * t + u3] = std::min(midiToHz(midis[t] + lift + spread[u3] * cents * 0.01f) / m_rate, 0.45f);
        const float subInc = std::min(midiToHz(midis[0] + lift - 12.0f) / m_rate, 0.45f);
        const float baseCut = 160.0f * k.cut * std::exp2(c * (5.2f + 1.2f * I));
        const float res = clampf(0.2f + 0.35f * I + k.res, 0, 0.95f);
        const double grid = gridFor(p, v.bars);
        // Off the beat, a set's stabs fall half a step late: the skank.
        const double shift = 0.5 * k.offbeat;
        const float stabTau = float(0.5 * grid * 60.0 / m_bpm) * k.decay;
        const float stabDecay = std::exp(-1.0f / (std::max(0.01f, stabTau) * m_rate));
        const float depth = clampf(0.85f - 0.7f * v.sMotion, 0, 1) * k.depth;
        const float g0 = v.gain;
        const float g1 = riseGain(pe, 17.0f, 1.2f) * pressGain(v) * fallFade(v, b0 + bps * n) * (0.85f + 0.3f * I) * 0.3f /
                         std::sqrt(float(count)) * k.gain;
        v.gain = g1;
        float peak = 0;
        const float panW = 0.3f + 0.6f * c;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const double step = std::floor(b / grid - shift);
            if (step != v.lastStep && v.state == Running && b < v.end) {
                v.lastStep = step;
                v.env = 1.0f;
            }
            v.env *= stabDecay;
            if ((i & 7) == 0) {
                const float fc = std::min(15000.0f, baseCut * (1.0f + 2.0f * v.env));
                v.ladL.set(fc, res, m_rate);
                v.ladR.set(fc * 1.01f, res, m_rate);
            }
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * cutAt(v, b) * (1.0f - depth + depth * v.env);
            float L = 0, R = 0;
            if (k.shape == 3) {
                // A bell: each note's sine bent by another at three and a
                // half times its pitch, harder while the stab is fresh, so
                // its partials are not harmonics.
                const float index = 0.05f + 0.45f * v.env;
                static const float pan[3] = {0.0f, -0.5f, 0.5f};
                for (int t = 0; t < count; ++t) {
                    float &ph = v.ph[t], &mph = v.ph[3 + t];
                    ph += inc[3 * t];
                    ph -= std::floor(ph);
                    mph += std::min(3.5f * inc[3 * t], 0.45f);
                    mph -= std::floor(mph);
                    float q = ph + index * sin01(mph);
                    q -= std::floor(q);
                    const float o = 2.0f * sin01(q);
                    L += o * (1.0f - pan[t]);
                    R += o * (1.0f + pan[t]);
                }
            } else if (k.shape == 2) {
                // An electric piano: one sine per note, its own phase bent by
                // itself while the stab is fresh, the bark of a struck tine.
                const float bark = 0.04f + 0.3f * v.env;
                static const float pan[3] = {0.0f, -0.5f, 0.5f};
                for (int t = 0; t < count; ++t) {
                    float &ph = v.ph[t];
                    ph += inc[3 * t];
                    ph -= std::floor(ph);
                    float q = ph + bark * sin01(ph);
                    q -= std::floor(q);
                    const float o = 2.2f * sin01(q);
                    L += o * (1.0f - pan[t]);
                    R += o * (1.0f + pan[t]);
                }
            } else if (k.shape == 1) {
                for (int t = 0; t < count; ++t) {
                    const float a = v.sq[3 * t].step(inc[3 * t]);
                    const float bL = v.sq[3 * t + 1].step(inc[3 * t + 1]);
                    const float bR = v.sq[3 * t + 2].step(inc[3 * t + 2]);
                    L += 0.7f * (a + bL * (1.0f + panW) + bR * (1.0f - panW));
                    R += 0.7f * (a + bR * (1.0f + panW) + bL * (1.0f - panW));
                }
            } else {
                for (int t = 0; t < count; ++t) {
                    const float a = v.saw[3 * t].step(inc[3 * t]);
                    const float bL = v.saw[3 * t + 1].step(inc[3 * t + 1]);
                    const float bR = v.saw[3 * t + 2].step(inc[3 * t + 2]);
                    L += a + bL * (1.0f + panW) + bR * (1.0f - panW);
                    R += a + bR * (1.0f + panW) + bL * (1.0f - panW);
                }
            }
            const float sb = k.sub > 0 ? v.saw[9].step(subInc) * 0.8f * k.sub : 0.0f;
            L = drive(v.ladL.step((L + sb) * 0.5f) * g * 2.0f, v.sGrit);
            R = drive(v.ladR.step((R + sb) * 0.5f) * g * 2.0f, v.sGrit);
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    void renderRoll(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm);
        v.p = p;
        const float c = std::pow(p, 1.3f);
        const float I = v.sInt;
        const RollLook &k = palette(v.set).roll;
        // The drum's body climbs the scale as the roll speeds up: five steps
        // up from the degree the finger slid to (a taiko stays down low).
        const int degree = int(std::floor(p * 5.0f * k.climb)) + int(std::lround(v.sTone * 4.0f));
        const float midi = float(h.midi(degree, 1)) + 12.0f * k.oct + v.bend;
        v.note = midi;
        const float hz = midiToHz(midi);
        const float bodyInc = hz / m_rate;
        v.aL.set(3200.0f * k.wireHz * (1.0f + 0.8f * c), 0.8f, m_rate); // the wires
        v.bL.set(1100.0f * k.wireHz, 0.7f, m_rate);
        // A rising high-pass thins it out.
        v.cL.set(90.0f * k.hp * std::exp2(2.6f * c * k.hpRise), 0.7f, m_rate);
        v.cR.set(90.0f * k.hp * std::exp2(2.6f * c * k.hpRise), 0.7f, m_rate);
        v.bR.set(1250.0f, 1.4f, m_rate); // the clap layer's band
        const float noiseDecay = std::exp(-1.0f / ((0.07f - 0.02f * c) * k.decay * m_rate));
        const float toneDecay = std::exp(-1.0f / (0.05f * k.ring * m_rate));
        const float clapDecay = std::exp(-1.0f / (0.04f * m_rate));
        const float pitchDecay = std::exp(-1.0f / (0.012f * k.ring * m_rate));
        // A gated hit holds, then is cut in a few milliseconds: the 80s snare.
        const float gateCut = std::exp(-1.0f / (0.0025f * m_rate));
        // A roll never starts slower than quarters; a triplet set rolls in
        // threes, which still meet the bar line.
        const double grid = gridFor(p, 1) * (k.triplet > 0 ? 2.0 / 3.0 : 1.0);
        const float fade = fallFade(v, bm);
        const float master = dbToGain(-8.0f * (1.0f - c)) * (0.7f + 0.5f * I) * fade * 0.55f * pressGain(v) * k.gain;
        const float width = 0.15f + 0.6f * v.sMotion;
        // A closed hi-hat: six square waves at the 808's unrelated pitches
        // and noise, through a high-pass made from a low-pass taken away.
        static const float hat[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        if (k.hat > 0)
            v.oL.set(6500.0f, m_rate);
        const float clapBase = k.clap > 1 ? (k.clap - 1.0f) * 0.6f : 0.0f;
        float peak = 0;
        // A chopped break, in sixteenths: 0 a kick, 1 the snare, 2 a ghost
        // note, −1 nothing.
        static const int8_t amen[16] = {0, -1, 0, -1, 1, -1, -1, 2, -1, 2, 0, 0, 1, -1, -1, 2};
        // Hats that change rate on every beat: sixteenths, triplets, thirty-
        // seconds, and in the build's second half sixty-fourth stutters.
        static const double slow[4] = {0.25, 0.25, 1.0 / 6.0, 0.125}, fast[4] = {0.125, 1.0 / 12.0, 0.0625, 1.0 / 6.0};
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            double step;
            int hit = 1;
            if (k.pattern == 1 && p < 0.75f) {
                step = std::floor(b * 4.0 + 1e-7);
                hit = amen[int(step - 16.0 * std::floor(step / 16.0))];
            } else if (k.pattern == 2) {
                const double beatNo = std::floor(b + 1e-7);
                const int pick = int(beatNo * 3.0 + std::floor(beatNo / 4.0)) & 3;
                const double g = (p < 0.5f ? slow : fast)[pick];
                step = beatNo * 64.0 + std::floor((b - beatNo) / g + 1e-7);
            } else {
                step = std::floor(b / grid + 1e-7);
            }
            const bool fresh =
                step != v.lastStep && (v.state == Running || v.state == Falling) && b < v.end - 1e-9 && b >= v.start - 1e-9;
            if (fresh)
                v.lastStep = step;
            if (fresh && hit >= 0) {
                ++v.stepCount;
                // Crescendo, the beat a little louder than the notes between;
                // a set with ghost notes plays every other one between softly.
                const bool beat = std::fabs(b - std::round(b)) < 0.01;
                const double onBeat = beat ? 1.0 : 0.82;
                float vel = float(onBeat) * (0.55f + 0.45f * std::pow(p, 0.8f));
                if (!beat && (v.stepCount & 1))
                    vel *= 1.0f - k.ghost;
                if (hit == 2)
                    vel *= 0.35f;
                v.env = hit == 0 ? 0.0f : vel;
                v.env2 = hit == 0 ? 1.4f * vel : vel;
                v.glideNote = hit == 0 ? 0.45f : 1.0f; // a kick is the body, lower and falling further
                v.pitchEnv = 1.0f;
                v.since = 0;
                if (I > 0.45f && (v.stepCount & 1))
                    v.env3 = vel * (I - 0.45f) * 1.6f * std::min(k.clap, 1.0f);
                if (clapBase > 0 && (v.stepCount & 1))
                    v.env3 += vel * clapBase;
                v.env4 = (v.stepCount & 1) ? 1.0f : -1.0f;
            }
            const float g = master * cutAt(v, b);
            v.pitchEnv *= pitchDecay;
            v.ph[0] += bodyInc * v.glideNote * (1.0f + (v.glideNote < 1.0f ? 1.5f : k.drop) * v.pitchEnv);
            v.ph[0] -= std::floor(v.ph[0]);
            v.ph[1] += bodyInc * 1.52f;
            v.ph[1] -= std::floor(v.ph[1]);
            const float tone = (sin01(v.ph[0]) + 0.45f * sin01(v.ph[1])) * v.env2 * k.body;
            const float nz = v.rng.bipolar();
            float wires = (v.aL.step(nz).band * 1.3f + v.bL.step(nz).high * 0.5f) * v.env * k.wires;
            if (k.hat > 0) {
                float m = 0;
                for (int j = 0; j < 6; ++j) {
                    float &ph = v.ph[2 + j];
                    ph += hat[j] * 1.7f / m_rate;
                    ph -= std::floor(ph);
                    m += ph < 0.5f ? 1.0f : -1.0f;
                }
                const float x = 0.12f * m + 0.5f * nz;
                wires += (x - v.oL.step(x)) * 1.6f * v.env * k.hat;
            }
            const float clap = v.bR.step(nz).band * 2.2f * v.env3;
            if (k.gate > 0) {
                v.since += 1.0f / m_rate;
                if (v.since > k.gate)
                    v.env *= gateCut;
            } else {
                v.env *= noiseDecay;
            }
            v.env2 *= toneDecay;
            v.env3 *= clapDecay;
            const float s = 0.6f * tone + 0.9f * wires;
            const float pan = v.env4 * width;
            float L = (s * (1.0f - pan) + clap) * g;
            float R = (s * (1.0f + pan) + clap) * g;
            L = v.cL.step(L).high;
            R = v.cR.step(R).high;
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.state == Running && b0 >= v.end + 0.5)
            v.active = false;
    }

    void renderSwell(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n, be = b0 + bps * n;
        v.p = progressAt(v, bm);
        const float I = v.sInt;
        // Reversed decay: the closer the ONE, the louder, as a cymbal or a
        // chord played backwards.
        const float len = float(std::max(1.0, v.end - v.start));
        // Two reversed tails: one as long as the swell, so it is heard from
        // its first bar, and a short one, the suck into the ONE.
        const float tauLong = 0.5f * len, tauShort = 0.6f + 0.05f * len;
        auto curve = [&](double left) {
            const float r = float(std::max(0.0, left));
            return 0.75f * std::exp(-r / tauLong) + 0.25f * std::exp(-r / tauShort);
        };
        auto envAt = [&](double b) {
            if (v.state == Falling)
                return curve(v.end - v.fallAt) * fallFade(v, b);
            return curve(v.end - b);
        };
        const SwellLook &look = palette(v.set).swell;
        const float e0 = v.gain, e1 = envAt(be) * pressGain(v) * 0.24f * look.gain;
        v.gain = e1;
        const int count = std::clamp(h.chordCount, 1, 3);
        const int shift = int(std::lround(v.sTone * 3.0f));
        float inc[6];
        for (int k = 0; k < count; ++k) {
            const float m = float(h.midi(h.chordTones[k] + 0, 1)) + 12.0f * float(shift > k) - 12.0f * float(-shift > k) +
                            12.0f * look.oct + v.bend;
            if (k == 0)
                v.note = m;
            inc[2 * k] = midiToHz(m + 0.07f) / m_rate;
            inc[2 * k + 1] = midiToHz(m - 0.07f) / m_rate;
        }
        const float cut = 900.0f * look.cut * std::exp2(3.8f * envAt(bm));
        v.aL.set(cut, 0.9f, m_rate);
        v.aR.set(cut * 1.05f, 0.9f, m_rate);
        v.bL.set(2600.0f, 0.7f, m_rate);
        v.bR.set(2900.0f, 0.7f, m_rate);
        v.cL.set(8500.0f, 0.9f, m_rate);
        const float bright = 5000.0f * std::exp2(1.6f * envAt(bm));
        v.oL.set(bright, m_rate);
        v.oR.set(bright * 1.1f, m_rate);
        // The cluster of a cymbal: six square waves at unrelated pitches,
        // band-passed high so they shimmer rather than ring.
        static const float metal[6] = {593.0f, 879.8f, 1068.1f, 1510.6f, 1560.6f, 2312.0f};
        const float cym = (0.8f + 0.6f * I) * look.cym, pad = (0.65f - 0.25f * I) * look.pad;
        const float metalInc = look.metal / m_rate;
        const float gateSmooth = 1.0f - std::exp(-1.0f / (0.002f * m_rate));
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const float g = e0 + (e1 - e0) * float(i) / float(n);
            float m = 0;
            for (int k = 0; k < 6; ++k) {
                v.ph[k] += metal[k] * metalInc;
                v.ph[k] -= std::floor(v.ph[k]);
                m += v.ph[k] < 0.5f ? 1.0f : -1.0f;
            }
            const float n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            const float metalBand = v.cL.step(m * 0.07f).band;
            // Reversed, a cymbal goes from dark to bright.
            const float cL = v.oL.step(v.bL.step(n1 * look.noise).high + metalBand) * cym;
            const float cR = v.oR.step(v.bR.step(n2 * look.noise).high + metalBand) * cym;
            float sl = 0, sr = 0;
            for (int k = 0; k < count; ++k) {
                sl += v.saw[2 * k].step(inc[2 * k]);
                sr += v.saw[2 * k + 1].step(inc[2 * k + 1]);
            }
            sl = v.aL.step(sl * pad * 0.5f).low;
            sr = v.aR.step(sr * pad * 0.5f).low;
            float L = (cL + sl) * g, R = (cR + sr) * g;
            // A smear of diffusion: it sounds like a reverb run backwards.
            for (int k = 0; k < 3; ++k) {
                L = v.apL[k].step(L);
                R = v.apR[k].step(R);
            }
            // Cut after the smear, so none of it runs past the line; a gated
            // set chops it in sixteenths there too.
            float cut = cutAt(v, b);
            if (look.gate > 0) {
                const double u = b * 4.0 - std::floor(b * 4.0);
                v.env4 += ((u < 0.5 ? 1.0f : 1.0f - look.gate) - v.env4) * gateSmooth;
                cut *= v.env4;
            }
            L *= cut;
            R *= cut;
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    // --- hits ----------------------------------------------------------------

    // The key's root as a low boom, never under 40 Hz.
    static float rootHz(const Harmony &h, float atLeast) {
        float hz = midiToHz(h.midi(0, -1));
        while (hz < atLeast)
            hz *= 2.0f;
        return hz;
    }

    // Splits a block around a hit's start: returns the first sample to play.
    static int startOf(Voice &v, int n) {
        if (v.delay <= 0)
            return 0;
        if (v.delay >= n) {
            v.delay -= n;
            return n;
        }
        const int from = v.delay;
        v.delay = 0;
        return from;
    }

    // The impact, in three layers:
    //   body   a tuned thump between 100 and 400 Hz and a knock of band-passed
    //          noise, on the sample: the weight you feel on the ONE;
    //   boom   the key's root, but only after the first 40 ms, faded in from
    //          phase zero and ducked under the kick: the kick owns the low end
    //          of the downbeat, the boom is its tail;
    //   crash  six square waves at unrelated pitches with noise, high-passed
    //          and closing from bright to warm as it decays, so it slopes down
    //          like a real cymbal instead of hissing; then a band of noise
    //          sweeping down under it.
    void renderImpact(Voice &v, float *l, float *r, float *low, int n, const Harmony &h, const float *duck) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const float I = v.sInt;
        const ImpactLook &k = palette(v.set).impact;
        const float boomHz = rootHz(h, 40.0f);
        float bodyHz = boomHz * 4.0f;
        while (bodyHz > 320.0f)
            bodyHz *= 0.5f;
        bodyHz *= k.bodyHz;
        v.note = float(69.0 + 12.0 * std::log2(boomHz / 440.0));
        if (v.t == 0) {
            v.env = 1;    // boom
            v.env2 = 1;   // body
            v.env3 = 1;   // crash
            v.env4 = 1;   // tail
            v.pitchEnv = 1;
            v.glideNote = 1; // the body's own pitch drop
            v.aL.set(240.0f, 1.4f, m_rate);
            v.aR.set(100.0f * std::min(1.0f, k.bodyHz), 0.7f, m_rate);
            v.pressPeak = 1; // the snap's envelope (a hit has no use for the pressure peak)
            v.bL.set(2400.0f, 0.7f, m_rate);
            v.bR.set(2600.0f, 0.7f, m_rate);
            v.padEnv = k.pad > 0 ? 1e-3f : 0.0f;
            v.padRising = true;
            v.blareL.reset();
            v.blareR.reset();
            v.blareL.set(1400.0f, 1.1f, m_rate);
            v.blareR.set(1500.0f, 1.1f, m_rate);
        }
        constexpr float kickWindow = 0.04f, fadeIn = 0.03f, kHitDelay = 0.02f;
        const float dt = 1.0f / m_rate;
        const float boomDecay = std::exp(-dt / ((0.28f + 0.3f * I) * k.boomDecay));
        const float bodyDecay = std::exp(-dt / (0.14f * k.bodyDecay)), snapDecay = std::exp(-dt / 0.025f);
        const float crashDecay = std::exp(-dt / ((0.8f + 0.9f * I) * k.crashDecay));
        const float tailDecay = std::exp(-dt / 0.9f);
        const float pitchDecay = std::exp(-dt / 0.06f), bodyPitchDecay = std::exp(-dt / 0.018f);
        // A gated set cuts the crash and its tail dead after a moment, the
        // way an 80s gated room does.
        const float gateCut = std::exp(-dt / 0.0025f);
        // The crash's top closes from 12 kHz to 4.5 kHz as it rings down.
        const float top = (3000.0f + 6000.0f * v.env3 * v.env3) * k.top;
        v.oL.set(top, m_rate);
        v.oR.set(top * 1.06f, m_rate);
        const float tailHz = 7000.0f * std::exp2(-v.t * 2.2f * float(m_bpm) / 120.0f);
        v.cL.set(std::max(250.0f, tailHz), 2.2f, m_rate);
        if (v.t == 0) {
            v.cR.set(650.0f, 1.2f, m_rate); // the knock's upper band, filling the mids
            v.bR.set(1700.0f, 0.7f, m_rate);
            v.bL.set(1500.0f, 0.7f, m_rate);
        }
        static const float metal[6] = {1373.0f, 2035.0f, 2471.0f, 3496.0f, 3611.0f, 5355.0f};
        const float metalInc = k.metal / m_rate;
        // The braam or the bloom: a chord of saws, two to a note, whose
        // ladder opens with its swell and closes as it dies.
        float padInc[6] = {};
        float padRise = 0, padFall = 1;
        if (k.pad > 0) {
            float m[3];
            if (k.padChord > 0) {
                for (int j = 0; j < 3; ++j)
                    m[j] = float(h.midi(h.chordTones[j % std::max(1, h.chordCount)], 2)) + 12.0f * k.padOct;
            } else {
                const float root = v.note + 12.0f + 12.0f * k.padOct;
                m[0] = root;
                m[1] = root + float(h.tuning.degreeSemis(4) - h.tuning.degreeSemis(0));
                m[2] = root + 12.0f;
            }
            for (int j = 0; j < 3; ++j) {
                padInc[2 * j] = std::min(midiToHz(m[j] + 0.08f) / m_rate, 0.45f);
                padInc[2 * j + 1] = std::min(midiToHz(m[j] - 0.08f) / m_rate, 0.45f);
            }
            padRise = 1.0f - std::exp(-dt / std::max(0.002f, k.padAttack));
            padFall = std::exp(-dt / k.padDecay);
        }
        // A layer under the kick, not over it: the drop's weight is the
        // kick's, the impact is its size.
        const float vel = v.vel * (0.75f + 0.35f * I) * pressGain(v) * 0.42f * k.gain;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            // Nothing for the kick's first 20 ms: its peak sits at the
            // limiter's ceiling, and anything laid on it only takes level
            // away from it. The body, snap and crash begin just after.
            if (v.t < kHitDelay) {
                v.t += 1.0f / m_rate;
                continue;
            }
            // Body.
            v.glideNote *= bodyPitchDecay;
            v.ph[1] += bodyHz * (1.0f + 1.2f * v.glideNote) / m_rate;
            v.ph[1] -= std::floor(v.ph[1]);
            const float n0 = v.rng.bipolar(), n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            const float knock = v.aL.step(n0).band * 2.5f + v.cR.step(n0).band * 2.6f;
            const float body = v.aR.step((sin01(v.ph[1]) + knock) * v.env2).high * k.body;
            v.env2 *= bodyDecay;
            // The snap: 25 ms of noise, the edge of the hit.
            const float snap = n2 * 0.35f * v.pressPeak * k.snap;
            v.pressPeak *= snapDecay;
            // Boom: after the kick's window, from phase zero.
            float boom = 0;
            if (v.t >= kickWindow && !v.quiet) {
                v.pitchEnv *= pitchDecay;
                v.ph[0] += boomHz * (1.0f + 0.5f * v.pitchEnv) / m_rate;
                v.ph[0] -= std::floor(v.ph[0]);
                const float in = std::min(1.0f, (v.t - kickWindow) / fadeIn);
                boom = fastTanh((1.0f + 0.8f * v.env) * sin01(v.ph[0])) * v.env * in * (1.0f - 0.85f * duck[i]) * k.boom;
                // A gate takes the boom's tail too.
                v.env *= (k.gate > 0 && v.t > k.gate) ? gateCut : boomDecay;
            }
            // Crash.
            float m = 0;
            for (int j = 0; j < 6; ++j) {
                float &ph = v.ph[2 + j];
                ph += metal[j] * metalInc;
                ph -= std::floor(ph);
                m += ph < 0.5f ? 1.0f : -1.0f;
            }
            const float cym = 0.09f * m;
            const float nz = 0.7f * k.noise;
            const float cl = v.oL.step(v.bL.step(n1 * nz + cym).high) * v.env3 * k.crash;
            const float cr = v.oR.step(v.bR.step(n2 * nz + cym).high) * v.env3 * k.crash;
            const float tl = v.cL.step(n1).band * v.env4 * k.tail, tr = tl;
            const bool gated = k.gate > 0 && v.t > k.gate;
            v.env3 *= gated ? gateCut : crashDecay;
            v.env4 *= gated ? gateCut : tailDecay;
            // The pad.
            float pl = 0, pr = 0;
            if (v.padEnv > 0) {
                if (v.padRising) {
                    v.padEnv += (1.0f - v.padEnv) * padRise;
                    v.padRising = v.padEnv < 0.98f;
                } else {
                    v.padEnv *= padFall;
                    if (v.padEnv < 1e-4f)
                        v.padEnv = 0;
                }
                if ((i & 7) == 0) {
                    const float fc = 90.0f + k.padOpen * v.padEnv * std::sqrt(v.padEnv);
                    v.ladL.set(fc, 0.2f, m_rate);
                    v.ladR.set(fc * 1.02f, 0.2f, m_rate);
                }
                const float a0 = v.saw[0].step(padInc[0]), a1 = v.saw[1].step(padInc[1]);
                const float a2 = v.saw[2].step(padInc[2]), a3 = v.saw[3].step(padInc[3]);
                const float a4 = v.saw[4].step(padInc[4]), a5 = v.saw[5].step(padInc[5]);
                const float g = 0.45f * k.pad * v.padEnv;
                float xl = v.ladL.step(0.5f * (a0 + a3 + a4 + 0.6f * (a1 + a2)));
                float xr = v.ladR.step(0.5f * (a1 + a2 + a5 + 0.6f * (a0 + a3)));
                if (k.padBlare > 0) {
                    const float blare = k.padBlare * v.padEnv;
                    xl += blare * v.blareL.step(xl).band;
                    xr += blare * v.blareR.step(xr).band;
                }
                pl = fastTanh(k.padDrive * xl) * g;
                pr = fastTanh(k.padDrive * xr) * g;
            }
            const float attack = clampf((v.t - kHitDelay) * 1000.0f, 0, 1);
            const float L = (0.55f * body + 0.2f * snap + 0.3f * cl + 0.12f * tl + pl) * vel * attack;
            const float R = (0.55f * body - 0.2f * snap + 0.3f * cr + 0.12f * tr + pr) * vel * attack;
            l[i] += L;
            r[i] += R;
            low[i] += 0.22f * boom * vel;
            peak = std::max(peak, std::fabs(L) + std::fabs(R) + std::fabs(boom));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.t > 0.2f && v.env < 1e-4f && v.env3 < 1e-4f && v.env4 < 1e-4f && v.padEnv <= 0)
            v.active = false;
    }

    // An 808 sub that dives an octave onto the root (or the degree slid to)
    // in half a beat and is gone within about a beat, ducked under the
    // kick. Armed onto a drop, it waits for the ONE and leaves the kick its
    // first 40 ms.
    void renderSub(Voice &v, float *l, float *r, float *low, int n, double b0, double bps, const Harmony &h,
                   const float *duck) {
        if (v.state == Waiting) {
            const double b1 = b0 + bps * n;
            if (v.start >= b1)
                return;
            v.delay = std::clamp(int((v.start - b0) / bps), 0, n - 1);
            v.state = Running;
            v.quiet = true; // started on a drop: mind the kick's window
        }
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const float I = v.sInt;
        const SubLook &k = palette(v.set).sub;
        const int degree = int(std::lround(v.sTone * 4.0f));
        float endHz = midiToHz(h.midi(degree, -1));
        while (endHz < 36.0f)
            endHz *= 2.0f;
        const float spb = float(60.0 / m_bpm);
        const float diveSec = (0.35f + 0.3f * I) * spb * k.diveTime;
        const float u = clampf(v.t / diveSec, 0, 1);
        const float semis = k.dive * (1.0f - u) * (1.0f - u);
        const float hz = endHz * std::exp2(semis / 12.0f);
        v.note = float(69.0 + 12.0 * std::log2(hz / 440.0));
        const bool sustaining = v.held || v.t < std::max(diveSec, k.swell);
        const float decay =
            std::exp(-1.0f / ((sustaining ? 4.0f : (0.18f + 0.25f * I) * spb / 0.47f * k.decay) * m_rate));
        if (v.t == 0) {
            v.env = 1.0f;
            v.env2 = 1.0f; // the tom's skin
            v.env3 = 1.0f; // its stick
        }
        const float vel = v.vel * pressGain(v) * 0.3f * k.gain;
        const float skinDecay = std::exp(-1.0f / (0.25f * m_rate)), stickDecay = std::exp(-1.0f / (0.004f * m_rate));
        const float skinInc = std::min(2.0f * hz / m_rate, 0.45f);
        const float window = v.quiet ? 0.04f : 0.0f;
        // An 808 pushed into distortion: more drive, the same loudness.
        const float push = 1.6f + 4.0f * k.drive, norm = 1.0f / (1.0f + 0.35f * k.drive);
        // The reese: two saws a few cents apart under a low-pass, beating
        // against each other an octave over the sub.
        const float rInc1 = std::min(hz * 2.0f * 1.004f / m_rate, 0.45f), rInc2 = std::min(hz * 2.0f / 1.004f / m_rate, 0.45f);
        if (k.reese > 0)
            v.aL.set(380.0f + 500.0f * v.env, 0.9f, m_rate);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const float attack = clampf((v.t - window) * (v.quiet ? 40.0f : 400.0f), 0, 1);
            if (attack > 0) {
                v.ph[0] += hz / m_rate;
                v.ph[0] -= std::floor(v.ph[0]);
            }
            const float s = sin01(v.ph[0]);
            // A touch of second harmonic so small speakers hear it, centred
            // so it adds no offset.
            float gain = v.env * attack * vel * (1.0f - 0.85f * duck[i]);
            // A slow swell: the sub breathes in rather than being struck.
            if (k.swell > 0)
                gain *= smoothstep(v.t / k.swell);
            const float o = fastTanh(push * s + 0.25f * (s * s - 0.5f)) * norm * gain;
            if (k.body > 0 && attack > 0) {
                v.ph[1] += skinInc;
                v.ph[1] -= std::floor(v.ph[1]);
                const float skin = (sin01(v.ph[1]) * v.env2 + 0.6f * v.rng.bipolar() * v.env3) * k.body * vel * 0.8f;
                v.env2 *= skinDecay;
                v.env3 *= stickDecay;
                l[i] += skin;
                r[i] += skin;
            }
            if (k.reese > 0 && attack > 0) {
                const float rs = v.aL.step(v.saw[0].step(rInc1) + v.saw[1].step(rInc2)).low * 0.35f * k.reese * gain;
                l[i] += rs;
                r[i] += rs;
            }
            v.env *= decay;
            low[i] += o;
            peak = std::max(peak, 2.0f * std::fabs(o));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.env < 2e-4f)
            v.active = false;
    }

    void renderDown(Voice &v, float *l, float *r, int n, double, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const DownLook &k = palette(v.set).down;
        // Four beats from bright to dark; held, it sinks most of the way and
        // waits there for the finger to let go.
        const float len = (4.0f + 2.0f * v.sInt) * k.len;
        v.env3 += float(bps * n) / len;
        if (v.held)
            v.env3 = std::min(v.env3, 0.85f);
        const float u = clampf(v.env3, 0, 1);
        v.p = 1.0f - u;
        const bool done = !v.held && v.env3 > 1.0f + 0.5f / len;
        const float c = 1.0f - std::pow(1.0f - u, 1.6f);
        const float lp = std::max(180.0f, 15000.0f * std::exp2(-c * 6.0f));
        v.aL.set(lp, k.q, m_rate);
        v.aR.set(lp * 1.03f, k.q, m_rate);
        v.bL.set(140.0f, 0.7f, m_rate);
        v.bR.set(140.0f, 0.7f, m_rate);
        const float target = float(h.midi(int(std::lround(v.sTone * 4.0f)), 0));
        const float midi = target + k.range * (1.0f - c) + v.bend;
        v.note = midi;
        const float inc = std::min(midiToHz(midi) / m_rate, 0.45f), inc2 = std::min(midiToHz(midi + k.detune) / m_rate, 0.45f);
        // The last tenth fades to nothing rather than stopping.
        const float gEnd = dbToGain(-24.0f * c) * clampf((1.0f - u) / 0.12f, 0, 1);
        const float g0 = v.gain, g1 = gEnd * pressGain(v) * (0.6f + 0.5f * v.sInt) * 0.3f * k.gain;
        v.gain = g1;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const float attack = std::min(1.0f, v.t * 200.0f);
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * attack;
            const float n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            const float s1 = v.saw[0].step(inc), s2 = v.saw[1].step(inc2);
            const float L = v.bL.step(v.aL.step(k.noise * n1 + k.tone * s1).low).high * g;
            const float R = v.bR.step(v.aR.step(k.noise * n2 + k.tone * s2).low).high * g;
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (done || (u >= 0.999f && !v.held && v.gain < 1e-4f))
            v.active = false;
    }

    void renderZap(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const ZapLook &k = palette(v.set).zap;
        if (k.mode == 1) {
            renderChop(v, l, r, from, n, b0, bps, h);
            return;
        }
        const float I = v.sInt;
        const int count = std::clamp(h.chordCount, 1, 4);
        const int base = int(std::lround(v.sTone * 7.0f));
        const float fall = (0.025f + 0.11f * I) * k.fallTime; // seconds to fall onto the note
        const float decay = std::exp(-1.0f / ((0.07f + 0.22f * I) * k.decay * m_rate));
        const float pitchDecay = std::exp(-1.0f / (fall * m_rate));
        const float clickDecay = std::exp(-1.0f / (0.004f * m_rate));
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const double b = b0 + bps * i;
            // Held, it fires again on every sixteenth, walking up the chord.
            const double step = std::floor(b * 4.0 + 1e-9);
            if (v.t == 0 || (v.held && step != v.lastStep && v.t > 0.2f)) {
                v.lastStep = step;
                const int j = v.stepCount++;
                const int tone = h.chordTones[j % count] + base;
                v.glideNote = float(h.midi(tone, 2 + (j / count) % 2)) + 12.0f * k.oct;
                v.env = 1.0f;
                v.env2 = 1.0f;
                v.pitchEnv = 1.0f;
                v.env4 = (j & 1) ? 0.5f : -0.5f;
            }
            v.pitchEnv *= pitchDecay;
            const float midi = v.glideNote + k.fall * v.pitchEnv * v.pitchEnv + v.bend;
            v.note = midi;
            const float hz = midiToHz(midi);
            v.ph[0] += hz / m_rate;
            v.ph[0] -= std::floor(v.ph[0]);
            v.ph[1] += 2.0f * hz / m_rate;
            v.ph[1] -= std::floor(v.ph[1]);
            const float mod = sin01(v.ph[1]) * (0.8f * v.pitchEnv + 0.15f) * k.fm;
            float ph = v.ph[0] + 0.25f * mod;
            ph -= std::floor(ph);
            float s = sin01(ph) + k.square * (v.ph[0] < 0.5f ? 1.0f : -1.0f) * v.pitchEnv;
            // A tom's stick: a few milliseconds of noise on the strike.
            if (k.click > 0) {
                s += k.click * v.rng.bipolar() * v.env2;
                v.env2 *= clickDecay;
            }
            const float attack = std::min(1.0f, v.t * 1000.0f);
            const float o = s * v.env * v.vel * 0.15f * (0.8f + 0.4f * I) * attack * k.gain;
            v.env *= decay;
            const float pan = v.env4;
            l[i] += o * (1.0f - pan);
            r[i] += o * (1.0f + pan);
            peak = std::max(peak, 2.0f * std::fabs(o));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (!v.held && v.env < 1e-4f)
            v.active = false;
    }

    // A reversed vocal chop: a buzz at a chord tone through two formant
    // bands, a vowel per chop, swelling backwards into a sudden stop. Held,
    // it chops sixteenths up the chord with the vowels turning over.
    void renderChop(Voice &v, float *l, float *r, int from, int n, double b0, double bps, const Harmony &h) {
        const float I = v.sInt;
        const ZapLook &k = palette(v.set).zap;
        const int count = std::clamp(h.chordCount, 1, 4);
        const int base = int(std::lround(v.sTone * 7.0f));
        // "ah", "oh", "eh", "ee": the first two formants of each.
        static const float vowels[4][2] = {{760, 1180}, {520, 900}, {480, 1850}, {320, 2250}};
        const float sixteenth = float(15.0 / m_bpm);
        const float len = v.held ? 0.92f * sixteenth : (0.16f + 0.14f * I);
        const float tau = 0.3f * len;
        const float stop = std::exp(-1.0f / (0.003f * m_rate));
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const double b = b0 + bps * i;
            const double step = std::floor(b * 4.0 + 1e-9);
            if (v.t == 0 || (v.held && step != v.lastStep && v.t > 0.2f)) {
                v.lastStep = step;
                const int j = v.stepCount++;
                v.glideNote = float(h.midi(h.chordTones[j % count] + base, 1)) + 12.0f * k.oct;
                v.note = v.glideNote;
                const float *f = vowels[j % 4];
                v.aL.set(f[0], 5.0f, m_rate);
                v.bL.set(f[1], 7.0f, m_rate);
                v.since = 0;
                v.env = 1e-3f;
                v.env4 = (j & 1) ? 0.35f : -0.35f;
            }
            v.since += 1.0f / m_rate;
            // Backwards: rising to its loudest at the end, then gone.
            v.env = v.since < len ? std::exp((v.since - len) / tau) : v.env * stop;
            const float hz = midiToHz(v.glideNote + v.bend + 0.12f * sin01(v.ph[2]));
            v.ph[2] += 5.5f / m_rate;
            v.ph[2] -= std::floor(v.ph[2]);
            const float buzz = v.saw[0].step(std::min(hz / m_rate, 0.45f)) + 0.15f * v.rng.bipolar();
            const float s = v.aL.step(buzz).band * 1.0f + v.bL.step(buzz).band * 0.7f;
            const float attack = std::min(1.0f, v.t * 1000.0f);
            const float o = s * v.env * v.vel * 0.3f * (0.8f + 0.4f * I) * attack * k.gain;
            l[i] += o * (1.0f - v.env4);
            r[i] += o * (1.0f + v.env4);
            peak = std::max(peak, 2.0f * std::fabs(o));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (!v.held && v.since > len && v.env < 1e-4f)
            v.active = false;
    }

    void renderStop(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const StopLook &k = palette(v.set).stop;
        // Held past its bar line, the power-down waits for the next one.
        if (v.held && b0 >= v.end - 0.5)
            v.end += kBeatsPerBar;
        if (v.state != Releasing && b0 >= v.end) {
            v.state = Releasing;
        }
        float u = clampf(float((b0 - v.start) / std::max(0.25, v.end - v.start)), 0, 1);
        v.p = u;
        // The sound of a deck winding down: the key's root sliding down
        // three octaves, darkening as it goes. A brake stops a turntable
        // dead instead, its speed falling straight to nothing in under a
        // second, and the rest of the bar is silence.
        float midi = float(h.midi(int(std::lround(v.sTone * 4.0f)), 1)) - k.range * u * u + v.bend;
        float fade = 1.0f - u * u;
        if (k.brake > 0) {
            u = clampf(v.t / k.brake, 0, 1);
            const float speed = k.rewind > 0 ? 1.0f + 3.0f * u * u : std::max(1.0f - u, 0.02f);
            midi = float(h.midi(int(std::lround(v.sTone * 4.0f)), 1)) + 12.0f * std::log2(speed) + v.bend;
            fade = std::sqrt(1.0f - u);
        }
        v.note = midi;
        const float hz = midiToHz(midi);
        v.aL.set(std::max(80.0f, 2500.0f * k.cut * std::exp2(-4.0f * u)), k.res, m_rate);
        const float g0 = v.gain;
        const float g1 = v.state == Releasing ? 0.0f : fade * 0.12f * pressGain(v) * (0.7f + 0.5f * v.sInt) * k.gain;
        v.gain = g1;
        const float inc = std::min(hz / m_rate, 0.45f), inc2 = std::min(hz * 0.5f / m_rate, 0.45f);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            // Faded in over 20 ms: a saw switched on is a click.
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * std::min(1.0f, v.t * 50.0f);
            const float osc = k.square > 0 ? 0.7f * (v.sq[0].step(inc) + k.sub * v.sq[1].step(inc2))
                                           : v.saw[0].step(inc) + k.sub * v.saw[1].step(inc2);
            const float s = v.aL.step(osc).low * g;
            l[i] += s;
            r[i] += s;
            peak = std::max(peak, 2.0f * std::fabs(s));
            v.t += 1.0f / m_rate;
        }
        (void)bps;
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.state == Releasing && v.gain <= 0 && b0 > v.end + 0.25)
            v.active = false;
    }

    // Throw, wash and sweep make no sound: they move the mix, which the page
    // does from the amounts published here.
    void renderMove(Voice &v, int n, double b0) {
        const float dt = float(n) / m_rate;
        const float spb = float(60.0 / m_bpm);
        if (v.t == 0)
            v.env2 = 0;
        v.t += dt;
        const float since = float(b0 - v.start);
        float target = 0;
        switch (v.kind) {
        case Throw:
            // A tap throws one beat into the echo; a hold, as long as it is
            // held. The feedback stays up for two beats after, so the echoes
            // ring out.
            target = (v.held || since < 1.0f) ? 1.0f : 0.0f;
            v.amount = target > 0 ? 1.0f : std::max(0.0f, v.amount - dt / (0.0625f * spb));
            v.env2 = target > 0 ? 1.0f : std::max(0.0f, v.env2 - dt / (3.0f * spb * palette(v.set).moves.throwTail));
            if (!v.held && v.amount <= 0 && v.env2 <= 0)
                v.active = false;
            break;
        case Wash: {
            target = (v.held || since < 2.0f) ? 1.0f : 0.0f;
            const float rise = dt / (0.25f * spb), fallRate = dt / (2.0f * spb);
            v.amount = target > v.amount ? std::min(1.0f, v.amount + rise) : std::max(0.0f, v.amount - fallRate);
            if (!v.held && since >= 2.0f && v.amount <= 0)
                v.active = false;
            break;
        }
        case Sweep:
            target = v.held ? 1.0f : 0.0f;
            v.amount = v.held ? 1.0f : std::max(0.0f, v.amount - dt / (0.25f * spb));
            if (!v.held && v.amount <= 0)
                v.active = false;
            break;
        default:
            break;
        }
        v.p = v.amount;
        v.level = v.amount * 0.5f;
    }

    float m_rate = 48000;
    float m_cutDecay = 0.99f;
    double m_cutLead = 0.006;
    bool m_gap = false;
    float m_busEnv = 0, m_busAttack = 0.005f, m_busRelease = 0.0001f;
    Reverb m_room;
    Svf m_roomHpL, m_roomHpR;
    TapeEcho m_echo;
    float m_echoAmount = 0;
    int m_echoIdle = 0;
    Spring m_spring;
    float m_roomIdle = 0;
    double m_beat = 0, m_bpm = 124, m_lastDrop = -1;
    float m_flash = 0;
    float m_hitFlash[kHits] = {};
    Voice m_voices[kVoices];
    DropPoint m_drops[4];
};

}
