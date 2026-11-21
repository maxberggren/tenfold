#pragma once

// The Arp page's musical core: the settings a pattern runs with, the
// patterns themselves, the rhythm a step follows, and the engine that plays
// them on the studio's clock. Everything here is a pure function of its
// inputs or lives on the audio thread, so the page can draw what the engine
// will play and the tests can check it against the beat grid.
//
// The engine keeps two lanes: the live one (what the fingers and keys hold
// now) and the clip lane (what a replayed loop holds). Each has its own pool
// of notes, its own settings and its own place in the pattern, so a loop and
// a live arp can run side by side.

#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

#include "dsp.h"
#include "electro.h"
#include "part.h"

namespace tf::arp {

enum Pattern : int { Up, Down, UpDown, Converge, Order, Random, ChordStab, Pedal };
enum Rate : int { Eighth, EighthTriplet, Sixteenth, SixteenthTriplet, ThirtySecond };
// The first five are the dance floor's; the second five reach past it: a
// marimba for ambient and afro house, a plucked harp for film and lo-fi, an
// electric piano for neo-soul, an eighties poly through its chorus for
// synthwave and Italo, and a drawbar organ for house and gospel. New sounds
// go on the end, so songs and loops keep their index.
enum Sound : int { Pluck, Glass, Saw, Pulse, Acid, Marimba, Harp, Rhodes, Juno, Organ };
constexpr int PatternCount = 8, RateCount = 5, SoundCount = 10;
constexpr int kEchoTimes = 5;
constexpr int kAccentChoices = 6;
constexpr int kStacks = 5;
constexpr int kMaxOctaves = 4;
constexpr int kRegisters = 4;

inline const char *patternName(int p) {
    static const char *n[] = {"up", "down", "up·down", "converge", "as played", "random", "chord", "pedal"};
    return n[std::clamp(p, 0, PatternCount - 1)];
}
inline const char *patternShort(int p) {
    static const char *n[] = {"UP", "DOWN", "UP·DN", "CONV", "ORDER", "RAND", "CHORD", "PEDAL"};
    return n[std::clamp(p, 0, PatternCount - 1)];
}
inline const char *rateName(int r) {
    static const char *n[] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
    return n[std::clamp(r, 0, RateCount - 1)];
}
inline const char *soundName(int s) {
    static const char *n[] = {"pluck", "glass", "supersaw", "pulse", "acid",
                              "marimba", "harp", "rhodes", "juno", "organ"};
    return n[std::clamp(s, 0, SoundCount - 1)];
}
inline const char *soundShort(int s) {
    static const char *n[] = {"PLUCK", "GLASS", "SAW", "PULSE", "ACID", "MARIMBA", "HARP", "RHODES", "JUNO", "ORGAN"};
    return n[std::clamp(s, 0, SoundCount - 1)];
}
// The colour each sound starts from, the one it sounds best in.
inline float soundColor(int s) {
    static const float c[] = {0.3f, 0.45f, 0.55f, 0.35f, 0.15f, 0.4f, 0.55f, 0.4f, 0.7f, 0.45f};
    return c[std::clamp(s, 0, SoundCount - 1)];
}

// The sounds a press on the tone pad switches rather than bites.
inline bool hasSwitch(int s) { return s == Juno || s == Organ; }
inline const char *switchName(int s, bool alt) {
    if (s == Organ)
        return alt ? "FAST" : "SLOW";
    if (s == Juno)
        return alt ? "CH I" : "CH II";
    return "";
}

inline const char *echoName(int e) {
    static const char *n[] = {"1/16", "1/8", "1/8.", "1/4", "1/4."};
    return n[std::clamp(e, 0, kEchoTimes - 1)];
}
inline const char *stackName(int s) {
    static const char *n[] = {"single", "fifth", "triad", "seventh", "ninth"};
    return n[std::clamp(s, 0, kStacks - 1)];
}

// A step's length in beats, and how many steps make a bar.
inline double stepBeats(int rate) {
    static const double b[] = {0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
    return b[std::clamp(rate, 0, RateCount - 1)];
}
inline int stepsPerBar(int rate) {
    static const int n[] = {8, 12, 16, 24, 32};
    return n[std::clamp(rate, 0, RateCount - 1)];
}
inline bool triplet(int rate) { return rate == EighthTriplet || rate == SixteenthTriplet; }
// The echo's time in beats: 1/16, 1/8, dotted 1/8 (the trance echo), 1/4,
// dotted 1/4.
inline double echoBeats(int e) {
    static const double b[] = {0.25, 0.5, 0.75, 1.0, 1.5};
    return b[std::clamp(e, 0, kEchoTimes - 1)];
}
// Accent every n notes; 0 is none. Three against the sixteenths is the
// trance lilt, five and seven roll slowly against the bar.
inline int accentCycle(int choice) {
    static const int n[] = {0, 2, 3, 4, 5, 7};
    return n[std::clamp(choice, 0, kAccentChoices - 1)];
}
// The chord a single held note grows into, in scale degrees above it.
inline int stackNotes(int stack, int *out) {
    static const int notes[kStacks][5] = {{0}, {0, 4}, {0, 2, 4}, {0, 2, 4, 6}, {0, 2, 4, 6, 8}};
    static const int count[kStacks] = {1, 2, 3, 4, 5};
    const int s = std::clamp(stack, 0, kStacks - 1);
    for (int i = 0; i < count[s]; ++i)
        out[i] = notes[s][i];
    return count[s];
}

// A lane's settings: everything a pattern needs besides its notes. The page
// sends them whole (packed into one Command) whenever they change, so a clip
// replays a pattern exactly as it was played, knob moves included.
struct Settings {
    int pattern = Up;
    int rate = Sixteenth;
    int octaves = 2;     // 1..4, the pattern climbs through this many octaves
    int reg = 1;         // 0..3, the octave of the pad's lower row (1: A3 in A)
    int sound = Pluck;
    int echoTime = 2;    // dotted eighth
    int accent = 0;      // accentCycle() choice
    int stack = 2;       // what the first note grows into: a triad
    bool follow = false; // play the Chords page's chord within the held range
    int rotate = 0;      // euclidean rotation, in steps
    uint32_t seed = 1;   // the dice for random and chaos
    // The sound's own switch, flipped by a press on the tone pad: the
    // organ's Leslie fast (off: slow), the juno's chorus I (off: II).
    bool alt = false;
    float gate = 0.45f;  // a step's sounding share, 0.06..1
    float swing = 0;     // 0..1: the off steps late by up to half a step
    float density = 1;   // euclidean hits per bar, as a share of its steps
    float chaos = 0;     // skips, octave leaps, rolls
    float cutoff = 0.55f;
    float res = 0.3f;
    float color = 0.35f; // each sound's own colour (wave blend, detune, FM ratio, pulse width)
    float echo = 0.3f;
    float accentDepth = 0.5f;
    float ratchet = 0;   // rolls, from pressing hard on the rhythm pad
    float envBoost = 0;  // extra filter envelope, from pressing on the tone pad
    float throwFb = 0;   // echo feedback thrown up, from pressing on the echo pad

    // A Command (type kState) carries the whole of it.
    void pack(Command &c) const;
    void unpack(const Command &c);
    bool operator==(const Settings &o) const;
    // The same to the ear: every choice equal, every knob within 0.5%.
    bool near(const Settings &o) const;
    bool operator!=(const Settings &o) const { return !(*this == o); }
};

// The page's command types.
enum : uint16_t {
    kState = Command::User,       // a lane's settings
    kOverlay = Command::User + 1, // the right hand shaping a playing loop (never recorded)
    kCut = Command::User + 2,     // the live lane's sounding notes fade at once (never recorded)
    kSync = Command::User + 3,    // a take just landed (never recorded)
};
// On: i[0] column (degree within the octave row), i[1] stack | row << 4 |
// flags << 8, i[2] the id this note takes over from (a bar's handover), i[3]
// the pattern position at that bar or −1; f[0] velocity.
// Move: the same i[0], i[1] and f[0].
enum : int { kRename = 1, kKey = 2, kFresh = 4 }; // kFresh: landed on an empty hand, the pattern starts over
// Overlay masks: which of the right hand's pads are shaping the loop.
enum : int { kPadRate = 1, kPadRhythm = 2, kPadRange = 4, kPadTone = 8, kPadGroove = 16, kPadEcho = 32 };
constexpr int kStateId = 0xFFFF00;
constexpr int kOverlayId = 0xFFFF01;

// Ids a published frame keeps where (PartView::data).
enum : int {
    kDataLivePool = 0,
    kDataClipPool = 1,
    kDataLiveStep = 2, // the live lane's last step, within its bar
    kDataClipStep = 3,
    kDataBar = 4,      // the last bar line the live lane passed…
    kDataBarCount = 5, // …and how many notes it had played by then
    kDataLiveRunning = 6,
    kDataClipRunning = 7,
    kDataFlash = 8,      // 24 cells: degrees above the register, lit when played
    kDataClipFlash = 32, // the same for the loop
    kDataEcho = 56,
    kDataLevel = 57,
    kDataEnv = 58,
    kDataOverlay = 59,
    kDataLiveRatchet = 60, // the last step's roll count
    kDataLiveAccent = 61,
    kDataRecent = 64, // kRecent × (beat mod 64, degree, velocity (negative: accent), lane + 1)
};
constexpr int kFlashCells = 24;
constexpr int kRecent = 40;

// Cheap, well mixed hash for the dice: the same inputs always give the same
// roll, so a random pattern repeats as a phrase instead of wandering.
inline uint32_t hash(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
inline float unit(uint32_t h) { return float(h >> 8) * (1.0f / 16777216.0f); }

inline int64_t floorDiv(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
inline int64_t floorModI(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

// Euclidean rhythm: hits spread as evenly as they go over n steps, the first
// on the step, turned by rot steps (the Bresenham form of Bjorklund).
inline bool euclid(int64_t step, int hits, int n, int rot) {
    if (n <= 0 || hits >= n)
        return true;
    if (hits <= 0)
        return false;
    const int64_t s = floorModI(step - rot, n);
    return (s * hits) % n < hits;
}
inline int hitsFor(float density, int n) { return std::clamp(int(std::lround(density * float(n))), 1, n); }

// Where step k starts, in beats, with swing pushing the off steps late.
inline double stepStart(int64_t k, int rate, float swing) {
    const double sb = stepBeats(rate);
    double b = double(k) * sb;
    if (!triplet(rate) && (k & 1))
        b += 0.5 * sb * double(std::clamp(swing, 0.0f, 1.0f));
    return b;
}
// The step under a beat position: the last one that started at or before it.
inline int64_t stepAt(double beat, int rate, float swing) {
    int64_t k = int64_t(std::floor(beat / stepBeats(rate)));
    if (stepStart(k, rate, swing) > beat + 1e-9)
        --k;
    return k;
}

// What a step does besides its note: whether it plays, how many times
// (ratchets), whether it is accented, an octave leap from chaos, and how
// loud it sits in the bar.
struct StepPlan {
    bool fire = true;
    int ratchet = 1;
    bool accent = false;
    int leap = 0;    // octaves
    int skip = 0;    // pattern notes skipped over
    float gain = 1;  // the bar's own dynamics
};

// dice is the step the dice are rolled for: the step itself, or for a loop
// the step its take had at that place.
inline StepPlan planStep(const Settings &s, int64_t step, uint64_t counter, int64_t dice) {
    StepPlan p;
    const int n = stepsPerBar(s.rate);
    const int perBeat = n / 4;
    const int64_t inBar = floorModI(step, n);
    p.fire = euclid(step, hitsFor(s.density, n), n, s.rotate);
    // Dice keyed to the step's place in a two-bar phrase: chaos repeats as
    // a phrase, so what you hear twice is what a loop will hold.
    const uint32_t key = uint32_t(floorModI(dice, 2 * n));
    const bool onBeat = inBar % perBeat == 0;
    if (s.chaos > 0.001f) {
        const float c = s.chaos;
        if (unit(hash(s.seed, key * 8 + 1)) < c * (onBeat ? 0.12f : 0.4f))
            p.fire = false;
        if (unit(hash(s.seed, key * 8 + 2)) < c * 0.28f)
            p.leap = 1;
        if (unit(hash(s.seed, key * 8 + 3)) < c * 0.16f)
            p.ratchet = 2;
        if (unit(hash(s.seed, key * 8 + 4)) < c * 0.22f)
            p.skip = 1;
    }
    if (s.ratchet > 0.001f && unit(hash(s.seed ^ 0x5A5A, key * 8 + 5)) < s.ratchet)
        p.ratchet = std::max(p.ratchet, 2 + int(s.ratchet > 0.45f) + int(s.ratchet > 0.8f));
    const int cyc = accentCycle(s.accent);
    p.accent = cyc > 0 && counter % uint64_t(cyc) == 0;
    // The beat a little louder than the steps between: a groove before any
    // accent is set.
    p.gain = onBeat ? 1.0f : (inBar % (perBeat / std::max(1, perBeat / 2)) == 0 ? 0.9f : 0.82f);
    return p;
}

// A lane's notes as the pattern sees them.
struct PoolView {
    int sorted[64];  // every note of the pool through its octaves, low to high, once each
    float sortedVel[64];
    int sortedCount = 0;
    int order[64];   // the same in the order they were played, octave by octave
    float orderVel[64];
    int orderCount = 0;
    int base[16];    // the pool in its lowest octave, low to high, for chord stabs
    float baseVel[16];
    int baseCount = 0;
};

// Builds the view from the pool's degrees (in played order) for the given
// octave span.
void buildPool(const int *deg, const float *vel, int count, int octaves, int perOctave, PoolView &out);

// The notes the pattern plays at position counter. Returns how many.
int patternNotes(int pattern, const PoolView &pool, uint64_t counter, uint32_t seed, int octaves, int perOctave,
                 int *deg, float *vel, int max);

// The engine. Public so the tests can drive it on a clock of their own.
class Engine : public PartEngine {
public:
    Engine();
    ~Engine() override;
    void init(float rate) override;
    void command(const Command &c, AudioContext &ctx) override;
    void render(float *l, float *r, int n, AudioContext &ctx) override;
    void silence(bool hard) override;
    void publish(float *data) override;
    int voices() const override;

    // Every note the engine started, newest last, for the page and the
    // tests: the sample it started on, the beat, its pitch, velocity, lane.
    struct Fired {
        uint64_t sample = 0;
        double beat = 0;
        float midi = 0, vel = 0;
        int degree = 0; // above the register's tonic
        int lane = 0;
        bool accent = false;
    };
    // A copy, oldest first (not for the audio thread).
    std::vector<Fired> log() const;
    void clearLog();
    const Settings &laneSettings(int lane) const;
    uint64_t laneCounter(int lane) const;
    int statesReceived() const; // settings commands heard, for the tests

    struct Impl;

private:
    Impl *d;
    std::vector<Fired> m_log; // a ring of the last few thousand, sized at init
};

}
