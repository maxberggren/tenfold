// Arp: running notes. The sequencer-arpeggiator at the heart of trance,
// progressive house and synthwave, played with two hands.
//
// The left hand says WHAT: it holds notes on a grid of the scale (two rows,
// an octave apart), and the first note grows into a chord, so one finger is
// already a running triad. Above the grid sit the eight patterns, below it
// the fifteen sounds in two rows (the lower a window onto the ten past the
// dance floor's) and the follow and latch switches.
//
// The right hand says HOW, on six pads: rate and gate (twist for swing),
// euclidean rhythm (press for rolls), range, tone (twist for resonance,
// press for bite), groove (accents and chaos) and echo (press to throw it).
//
// The pattern runs on the studio's clock to the sample. Clicking the pad
// latches what the fingers hold; Space loops the last bars into a clip from
// the bar line, and the loop carries on exactly where the live pattern was.
// With no notes held, the right hand shapes the loop that is playing, and
// lets go of it when it lifts.
//
// The keys are the ones every part page shares (← → sound, ↑ ↓ register,
// Shift+← → rate, - + tone, Return latch, Shift+Return overdub, the key
// left of 1 follow, Delete let go), with the arp's own letters beside them. The band under the pad
// has a control for each, so the mouse, which is one more finger on the
// pad, can do everything the keys do.

#include "arp.h"

#include <QPainter>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QJsonObject>
#include <algorithm>
#include <climits>
#include <cmath>

#include "console.h"
#include "parts.h"
#include "songfile.h"

namespace tf::arp {

// --- settings -----------------------------------------------------------------

namespace {
int q10(float v) { return std::clamp(int(std::lround(v * 1023.0f)), 0, 1023); }
float u10(int v) { return float(v & 1023) / 1023.0f; }
}

void Settings::pack(Command &c) const {
    // The sound had three bits when there were five; the fourth sits in the
    // spare bit 30, so loops saved before there were ten unpack unchanged.
    c.i[0] = (pattern & 15) | (rate & 7) << 4 | ((octaves - 1) & 3) << 7 | (reg & 3) << 9 | (sound & 7) << 11 |
             (echoTime & 7) << 14 | (accent & 15) << 17 | (stack & 7) << 21 | int(follow) << 24 | (rotate & 31) << 25 |
             (sound >> 3 & 1) << 30;
    c.i[1] = int(seed & 0xFFFFFF) | int(alt) << 24;
    c.i[2] = q10(accentDepth) | q10(ratchet) << 10 | q10(envBoost) << 20;
    c.i[3] = (c.i[3] & ~1023) | q10(throwFb);
    c.f[0] = gate;
    c.f[1] = swing;
    c.f[2] = density;
    c.f[3] = chaos;
    c.f[4] = cutoff;
    c.f[5] = res;
    c.f[6] = color;
    c.f[7] = echo;
}

void Settings::unpack(const Command &c) {
    const int a = c.i[0];
    pattern = std::clamp(a & 15, 0, PatternCount - 1);
    rate = std::clamp(a >> 4 & 7, 0, RateCount - 1);
    octaves = (a >> 7 & 3) + 1;
    reg = a >> 9 & 3;
    sound = std::clamp((a >> 11 & 7) | (a >> 30 & 1) << 3, 0, SoundCount - 1);
    echoTime = std::clamp(a >> 14 & 7, 0, kEchoTimes - 1);
    accent = std::clamp(a >> 17 & 15, 0, kAccentChoices - 1);
    stack = std::clamp(a >> 21 & 7, 0, kStacks - 1);
    follow = a >> 24 & 1;
    rotate = a >> 25 & 31;
    seed = uint32_t(c.i[1]) & 0xFFFFFF;
    alt = c.i[1] >> 24 & 1;
    accentDepth = u10(c.i[2]);
    ratchet = u10(c.i[2] >> 10);
    envBoost = u10(c.i[2] >> 20);
    throwFb = u10(c.i[3]);
    gate = clampf(c.f[0], 0.04f, 1.0f);
    swing = clampf(c.f[1], 0, 1);
    density = clampf(c.f[2], 0, 1);
    chaos = clampf(c.f[3], 0, 1);
    cutoff = clampf(c.f[4], 0, 1);
    res = clampf(c.f[5], 0, 1);
    color = clampf(c.f[6], 0, 1);
    echo = clampf(c.f[7], 0, 1);
}

bool Settings::near(const Settings &o) const {
    // Choices must match; knobs within half a percent count as unmoved.
    Command a, b;
    pack(a);
    o.pack(b);
    if (a.i[0] != b.i[0] || a.i[1] != b.i[1])
        return false;
    for (int k = 0; k < 3; ++k)
        if (std::abs((a.i[2] >> (10 * k) & 1023) - (b.i[2] >> (10 * k) & 1023)) > 5)
            return false;
    if (std::abs((a.i[3] & 1023) - (b.i[3] & 1023)) > 5)
        return false;
    for (int i = 0; i < 8; ++i)
        if (std::fabs(a.f[i] - b.f[i]) > 0.005f)
            return false;
    return true;
}

bool Settings::operator==(const Settings &o) const {
    Command a, b;
    pack(a);
    o.pack(b);
    for (int i = 0; i < 4; ++i)
        if (a.i[i] != b.i[i])
            return false;
    for (int i = 0; i < 8; ++i)
        if (std::fabs(a.f[i] - b.f[i]) > 1e-6f)
            return false;
    return true;
}

// --- patterns -----------------------------------------------------------------

void buildPool(const int *deg, const float *vel, int count, int octaves, int perOctave, PoolView &out) {
    out = PoolView{};
    count = std::min(count, 16);
    octaves = std::clamp(octaves, 1, kMaxOctaves);
    // The base: unique degrees, low to high, keeping the first velocity.
    for (int i = 0; i < count; ++i) {
        bool dup = false;
        for (int j = 0; j < out.baseCount; ++j)
            dup |= out.base[j] == deg[i];
        if (dup)
            continue;
        out.base[out.baseCount] = deg[i];
        out.baseVel[out.baseCount] = vel[i];
        ++out.baseCount;
    }
    for (int i = 1; i < out.baseCount; ++i)
        for (int j = i; j > 0 && out.base[j - 1] > out.base[j]; --j) {
            std::swap(out.base[j - 1], out.base[j]);
            std::swap(out.baseVel[j - 1], out.baseVel[j]);
        }
    // Through the octaves, low to high, each pitch once.
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < out.baseCount && out.sortedCount < 64; ++i) {
            const int d = out.base[i] + o * perOctave;
            bool dup = false;
            for (int j = 0; j < out.sortedCount; ++j)
                dup |= out.sorted[j] == d;
            if (dup)
                continue;
            out.sorted[out.sortedCount] = d;
            out.sortedVel[out.sortedCount] = out.baseVel[i];
            ++out.sortedCount;
        }
    for (int i = 1; i < out.sortedCount; ++i)
        for (int j = i; j > 0 && out.sorted[j - 1] > out.sorted[j]; --j) {
            std::swap(out.sorted[j - 1], out.sorted[j]);
            std::swap(out.sortedVel[j - 1], out.sortedVel[j]);
        }
    // As played, octave by octave.
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < count && out.orderCount < 64; ++i) {
            bool dup = false;
            for (int j = 0; j < i; ++j)
                dup |= deg[j] == deg[i];
            if (dup)
                continue;
            out.order[out.orderCount] = deg[i] + o * perOctave;
            out.orderVel[out.orderCount] = vel[i];
            ++out.orderCount;
        }
}

int patternNotes(int pattern, const PoolView &pool, uint64_t counter, uint32_t seed, int octaves, int perOctave,
                 int *deg, float *vel, int max) {
    const int L = pool.sortedCount;
    if (L <= 0 || max <= 0)
        return 0;
    auto one = [&](int idx) {
        idx = std::clamp(idx, 0, L - 1);
        deg[0] = pool.sorted[idx];
        vel[0] = pool.sortedVel[idx];
        return 1;
    };
    switch (pattern) {
    case Up:
        return one(int(counter % uint64_t(L)));
    case Down:
        return one(L - 1 - int(counter % uint64_t(L)));
    case UpDown: {
        if (L < 2)
            return one(0);
        const uint64_t cyc = uint64_t(2 * L - 2);
        const int p = int(counter % cyc);
        return one(p < L ? p : int(cyc) - p);
    }
    case Converge: {
        const int p = int(counter % uint64_t(L));
        return one(p % 2 == 0 ? p / 2 : L - 1 - p / 2);
    }
    case Order: {
        const int n = pool.orderCount;
        if (n <= 0)
            return one(0);
        const int p = int(counter % uint64_t(n));
        deg[0] = pool.order[p];
        vel[0] = pool.orderVel[p];
        return 1;
    }
    case Random: {
        if (L < 2)
            return one(0);
        // A shuffled deck, dealt again every time round: every note comes
        // up as often as the others, never twice running, and the same dice
        // deal the same hands, so a random arp repeats like a phrase.
        auto deal = [&](uint64_t round, int *deck) {
            for (int i = 0; i < L; ++i)
                deck[i] = i;
            for (int i = L - 1; i > 0; --i)
                std::swap(deck[i], deck[hash(seed * 31u + 7u, uint32_t(round * 64 + uint64_t(i))) % uint32_t(i + 1)]);
        };
        int deck[64], prev[64];
        const uint64_t round = counter / uint64_t(L);
        const int p = int(counter % uint64_t(L));
        deal(round, deck);
        if (round > 0) {
            deal(round - 1, prev);
            if (deck[0] == prev[L - 1])
                std::swap(deck[0], deck[L / 2]);
        }
        return one(deck[p]);
    }
    case ChordStab: {
        const int o = int(counter % uint64_t(std::clamp(octaves, 1, kMaxOctaves)));
        const int n = std::min(pool.baseCount, max);
        for (int i = 0; i < n; ++i) {
            deg[i] = pool.base[i] + o * perOctave;
            vel[i] = pool.baseVel[i];
        }
        return n;
    }
    case Pedal: {
        if (L < 2)
            return one(0);
        const uint64_t cyc = uint64_t(2 * (L - 1));
        const int p = int(counter % cyc);
        return one(p % 2 == 0 ? 0 : 1 + p / 2);
    }
    default:
        return one(0);
    }
}

// --- the voice ----------------------------------------------------------------

namespace {

// sin(2πx) for x in [0, 1): a parabola with one correction, a thousandth
// off at worst, at a fraction of the cost of std::sin per sample per partial.
inline float sin2pi(float x) {
    const float t = x - 0.5f;
    float y = 8.0f * t - 16.0f * t * std::fabs(t);
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}
inline float frac(float x) { return x - std::floor(x); }
inline float sawBlep(float ph, float inc) { return 2.0f * ph - 1.0f - polyBlep(ph, inc); }
inline float squareBlep(float ph, float inc) {
    float v = ph < 0.5f ? 1.0f : -1.0f;
    v += polyBlep(ph, inc);
    v -= polyBlep(frac(ph + 0.5f), inc);
    return v;
}

constexpr int kVoices = 28;
constexpr double kTopMidi = 100.0; // E7: arps climb no higher
// The log drum's glide from log to log, set by the tone pad's twist: 12 ms
// (a flick) at none, some 45 where the twist starts, over 200 ms (a slow
// slide) at most. The voice takes it, and the pad shows it.
inline float logGlideSec(float res) { return 0.012f + 0.2f * std::pow(std::clamp(res, 0.0f, 1.0f), 1.5f); }
constexpr float kOutGain = 1.6f;
constexpr int kSoftVoices = 18; // past this, the oldest notes are faded out to make room

// One arp note. Ten sounds, all through the same ladder filter so the
// tone pad means the same thing for each:
//   pluck     two saws a few cents apart (blending to square), a snappy
//             filter envelope: the trance pluck.
//   glass     two-operator FM, the index falling fast: a struck glass tine.
//   supersaw  seven saws in unison spread wide: the anthem arp.
//   pulse     two pulse waves with slow width modulation: synthwave.
//   acid      one saw (blending to square), resonance pushed, accents bite.
//   marimba   three modal partials tuned 1 : 4 : 10 like a rosewood bar,
//             the upper ones dying first, a fourth high up that is gone in
//             milliseconds (the bar's top click) and a woody knock tuned
//             to the note; colour is the mallet, soft yarn to hard rubber.
//   harp      a Karplus-Strong string: the shape of the plucked string
//             circulating in a delay one period long; colour is the pluck,
//             fingertip to nail. Strings are not damped when the arp lets
//             go: they ring on through the pattern until they fade (or the
//             voice cap takes the oldest). Low strings left, high right.
//   rhodes    a tine electric piano: a 1:1 FM pair whose index (the bark)
//             grows with how hard the key is hit, the pickup's lopsided
//             response (even harmonics, the growl), a brief, slightly
//             inharmonic tine on the attack, and the suitcase's tremolo.
//   juno      one oscillator, a width-modulated pulse (blending back to a
//             saw at low colour), a square an octave below, the Juno's
//             high-pass thinning the oscillator above the sub as colour
//             rises, a resonant four-pole sweep and the bus's BBD chorus.
//   organ     five drawbars of sines (16', 8', 4', 2 2/3', 2'), the third
//             harmonic's percussion and a key click, through the bus's
//             Leslie: a horn and a drum spinning slow or fast.
// The second five are mono at the source and panned after the filter, so
// each costs one ladder; the chorus and the Leslie are one each for the
// bus, not one per note. So are the third five, but for the gamelan:
//   steel pan the fundamental, the octave, the twelfth and the double
//             octave, each its own phase, since a pan's note is a dome
//             tuned by ear and never quite harmonic: a harder stick
//             stretches the octave and twelfth further and starts the note
//             a few cents sharp, settling in some 70 ms. The octave and
//             twelfth bloom in after the stick, a hertz-sharp copy of the
//             octave beats slowly against it (the shimmer), and four
//             neighbouring domes (the note a fifth up, its octave, the
//             note an octave down, and the note's own outer ring) are rung
//             through the skirt and ring on after it. A long note at a
//             slow rate is rolled, as a pannist does: soft restrikes some
//             twelve times a second while the gate holds. Colour is the
//             stick, rubber to bare.
//   sitar     a plucked string like the harp's, but over a jawari: while
//             the string swings up onto the bridge's curve it is a little
//             shorter, so the loop jumps between two lengths each period,
//             read through a Hermite curve so the kink stays bright. The
//             kink is the buzz, and since the contact follows the string's
//             own swing, it is fed new overtones (their peaks sweeping)
//             all the way down instead of dying with the attack. Under it
//             the bus's taraf (see Taraf): six sympathetic strings tuned to
//             the key, the top two the chikari drone, struck on accents.
//             A legato note on the beat (or on an accent) close to the
//             last is a meend, the string pulled along its fret. Colour
//             opens the jawari, closed and round to wide open and snarling.
//   log drum  amapiano's, an octave under the named note as a bass is: a
//             sine falling into the note from above, or gliding there from
//             the last log (the glide's time on the tone pad's twist), a
//             short yelp up on an accent, a phase-modulated knock that
//             clears to the pure tone, a hollow partial (the log), a nasal
//             tok swept down through the knock, and a lopsided tanh whose
//             offset is taken back out. It rings on past its gate for half
//             a second or more down low, on a bus of its own that keeps
//             its fundamental from the arp's 95 Hz high-pass.
//   chip      a Game Boy pulse channel: one duty of 12.5, 25 or 50 % (by
//             colour), made at twice the rate and brought down through a
//             half-band, pitched through the hardware's 11-bit register,
//             its volume falling in sixteen steps, a vibrato that waits for
//             long notes, accents swept down into the note x >> n at a
//             time, chords as a 60 Hz arpeggio in the one channel, and the
//             one-sided DAC's centre into a 28 Hz capacitor (as much of it
//             as colour lets through: a clean blip to the hardware's tick).
//   gamelan   a gangsa: two bronze keys a few hertz apart so together
//             they shimmer (the ombak, faster up high), each with a free
//             bar's inharmonic partials 1 : 2.76 : 5.4, and the wooden
//             mallet's ping. Two players, the polos left and the sangsih
//             right, play kotekan (see the trigger): one figure split
//             between them, meeting on shared keys. In selisir every note
//             is one of five gangsa keys. Each player damps the key they
//             struck as they strike the next, as gamelan is played.
// The gamelan runs two channels (its two keys, each player's place)
// through two ladders; the sitar's two sides are the taraf's, on its bus.
// The chip's half-band: it runs its pulse at twice the rate and comes back
// down through this, 39 taps of a Kaiser-windowed sinc (every other one
// zero), flat to 0.002 dB below a third of the doubled rate and 90 dB down
// past two thirds of it. The edges' residue that a pulse at the plain rate
// would fold back as a hash of tones between the harmonics lands above the
// ear instead, where this takes it away.
struct HalfBand {
    static constexpr int kTaps = 39, kMid = 19;
    float h[kTaps] = {};
    HalfBand() {
        auto i0 = [](double x) {
            double sum = 1, term = 1;
            for (int k = 1; k < 40; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        constexpr double beta = 10.0;
        double total = 0;
        double w[kTaps];
        for (int i = 0; i < kTaps; ++i) {
            const double n = double(i - kMid);
            const double sinc = i == kMid ? 1.0 : std::sin(3.14159265358979 * n / 2.0) / (3.14159265358979 * n / 2.0);
            const double r = n / double(kMid);
            w[i] = 0.5 * sinc * i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
            total += w[i];
        }
        for (int i = 0; i < kTaps; ++i)
            h[i] = float(w[i] / total);
    }
};
const HalfBand kHalf;

// A Game Boy's pulse channel is tuned by an 11-bit register x, its pitch
// 131072 / (2048 − x) Hz: the register for a pitch, and the pitch of one.
inline float chipRegister(float hz) { return clampf(std::round(2048.0f - 131072.0f / hz), 0.0f, 2046.0f); }
inline float chipHz(float x) { return 131072.0f / std::max(1.0f, 2048.0f - x); }

constexpr int kString = 2048; // the harp's delay: periods up to 2047 samples (23 Hz)

struct Voice {
    bool on = false, released = false, fading = false;
    bool mono = false;   // one source, panned after the filter
    // 0 dry, 1 through the chorus (juno), 2 the Leslie (organ), 3 the taraf
    // (sitar), 4 the sub bus (the log drum), which skips the 95 Hz high-pass.
    int bus = 0;
    int lane = 0, sound = Pluck;
    uint64_t born = 0;
    int gate = 0;
    float vel = 0.8f;
    int stage = 0; // 0 attack, 1 decay, 2 release
    float amp = 0, attackInc = 0, decayMul = 0, sustain = 0, releaseMul = 0, fadeMul = 0;
    float fenv = 1, fenvMul = 0, envOct = 0;
    float fmIdx = 0, fmSus = 0, fmMul = 0;
    float keyTrack = 1;
    int units = 1;
    float ph[7] = {}, inc[7] = {}, gl[7] = {}, gr[7] = {};
    float mph = 0, minc = 0;
    float blend = 0, pw = 0.5f, lfo = 0, lfoInc = 0, pwmDepth = 0;
    float gain = 0.3f, resBias = 0, cutBias = 0;
    float midiNow = 60, midiTo = 60, glide = 0; // acid slides
    Ladder fl, fr;
    int coefIn = 0;
    float level = 0;
    // How far a sound that dies away by itself (the marimba, the harp) has
    // gone: once it and the envelope together are inaudible, the note is over.
    float tail = 1, tailMul = 1;
    // Marimba: each partial a two-pole recursion, y = c·y1 − r²·y2, set
    // going so it rings as a decaying sine from zero (no click). In double:
    // a low bar's coefficient sits so close to 2 that a float's rounding
    // would pull it a few cents flat.
    static constexpr int kModes = 4;
    double my1[kModes] = {}, my2[kModes] = {}, mc[kModes] = {}, mr2[kModes] = {};
    // A burst of noise: the marimba's knock, the organ's key click.
    float burst = 0, burstMul = 0, burstLp = 0, burstA = 1;
    Rng noise;
    // Harp: the string, its length in whole samples, the loop's damping
    // (a two-point weighted average), the allpass that tunes the fraction,
    // and the loss per pass.
    float str[kString] = {};
    int sw = 0, sN = 1;
    float sS = 0.4f, sC = 0, sRho = 1, apX = 0, apY = 0;
    // Rhodes: the tine's index, the tremolo's depth. Organ: drawbars and
    // percussion.
    float tine = 0, tineMul = 0, trem = 0, growl = 0;
    // Juno: the high-pass's coefficient (0: flat) and its two poles.
    float hpA = 0, hp1 = 0, hp2 = 0;
    float draw[5] = {}, perc = 0, percMul = 0;
    // Steel pan: each partial's level, fall and ratio to the fundamental
    // (each its own phase, since none is a whole multiple), the bloom of
    // the octave and the twelfth (the difference of two falls), the
    // stroke's sharpening as it settles, and the neighbouring notes that
    // ring in sympathy.
    float pa[4] = {}, paMul[4] = {}, pr[4] = {}, prBend[4] = {}, swell = 0, swellMul = 0, swell3 = 0, swell3Mul = 0;
    float bend = 0, bendMul = 0, bendDepth = 0;
    static constexpr int kNeighbours = 4;
    double ny1[kNeighbours] = {}, ny2[kNeighbours] = {}, nc[kNeighbours] = {}, nr2[kNeighbours] = {};
    float nIn[kNeighbours] = {};
    // The roll: the stroke's own partial levels, the time to the next
    // restrike and its average spacing, how many so far, and the short
    // ramp that brings each partial up to the restrike's level.
    float paBase[4] = {}, rollTo[4] = {}, rollEvery = 0, rollIn = 0, rollHard = 0, rollMul = 1;
    int rollN = 0, rollRamp = 0, rollRampLen = 1;
    // Sitar: the share of the swing that rests on the bridge, how much
    // shorter the string is while it does (in samples, and as it is now),
    // the swing it follows, the loop's DC blocker; and for a meend, the
    // loop's length and the pitch it was tuned to, and how far a pull has
    // stretched it.
    float jaw = 0, jawCut = 0, jawNow = 0, swing = 0, swingMul = 0, dcX = 0, dcY = 0, dcR = 0.9995f;
    float sLoop = 0, sMidi = 60, sBend = 0;
    // Log drum: the bend into the note (in octaves, from above or from the
    // last log), the knock's index, the drive, and the nasal tok: a
    // resonant band sweeping down through the knock.
    float drop = 0, dropMul = 0, drive = 0, driveNorm = 1, incNote = 0, asym = 0;
    float tok = 0, tokEnv = 0, tokMul = 0, tokHz = 1000;
    // The accent's yelp: how far up (in octaves), and the two falls whose
    // difference is its rise and return.
    float yelp = 0, yelpSlow = 0, yelpFast = 0, yelpSlowMul = 0, yelpFastMul = 0;
    Svf tokF;
    // Chip: the volume's step (0..15), how many samples each lasts, the
    // count towards the next, the level smoothed a hair, the duty; the
    // vibrato's wait and depth; the frame clock (the driver's, 60 a
    // second) and the sweep's (128); the frequency register now, where
    // the sweep lands, and its shift; the notes of a chord taken in turn,
    // one a frame; the oversampled phase, the half-band's history and the
    // output capacitor.
    int chipVol = 15, chipEvery = 1, chipCount = 0;
    float chipLevel = 1, chipSmooth = 1, duty = 0.5f, vibWait = 0, vibDepth = 0, vibGrow = 0;
    float frameIn = 0, frameLen = 800, sweepIn = 0, sweepLen = 375;
    float reg = 0, regTo = 0;
    int sweepShift = 0, arpN = 0, arpAt = 0;
    float arpReg[4] = {};
    static constexpr int kHalfBand = 64;
    float hb[kHalfBand] = {};
    int hbAt = 0;
    float capX = 0, capY = 0, capR = 0.996f;
    // The DAC's centre as it reaches the capacitor: how much of it does
    // (grain), and how fast it follows the pulse's own (centreA).
    float centre = 0, centreA = 1, grain = 1;
    // Gamelan: two keys of three partials each, the same recursion as the
    // marimba's bar.
    static constexpr int kGong = 6;
    double gy1[kGong] = {}, gy2[kGong] = {}, gc[kGong] = {}, gr2[kGong] = {};
    int player = 0; // the gamelan's: 0 the polos, 1 the sangsih

    struct Start {
        int sound = Pluck;
        float midi = 60, vel = 0.8f, accent = 0, envBoost = 0, color = 0.3f, gateSec = 0.1f, stepSec = 0.12f,
              rate = 48000;
        // A log drum's last pitch in the lane, to glide from (0: none).
        float fromMidi = 0;
        // A chip's chord, taken one note a frame (more than one: an
        // arpeggio in one channel).
        int chord = 0;
        float chordMidi[4] = {};
        // A gangsa's player: the sangsih, on the right, interlocking with
        // the polos on the left.
        bool sangsih = false;
        // The tone pad's twist: the log drum's glide time (its resonance
        // stays its own).
        float res = 0.3f;
        // A level the trigger lifts a note by (the sitar's legato, one
        // string sounding where plucked ones would ring on together).
        float lift = 1;
        uint32_t seed = 1;
        double clock = 0; // seconds since the engine started: one LFO for every note
    };

    void start(const Start &s) {
        on = true;
        released = fading = false;
        sound = s.sound;
        mono = sound >= Marimba; // the gamelan sets two units, which wins
        bus = sound == Juno ? 1 : sound == Organ ? 2 : sound == LogDrum ? 4 : 0;
        player = s.sangsih ? 1 : 0;
        vel = clampf(s.vel, 0.05f, 1.3f);
        stage = 0;
        amp = 0;
        level = 0;
        gate = std::max(1, int(s.gateSec * s.rate));
        midiNow = midiTo = s.midi;
        glide = 1.0f - std::exp(-8.0f / (0.045f * s.rate)); // per eight samples
        const float hz = midiToHz(s.midi);
        Rng rng(s.seed | 1u);
        for (float &p : ph)
            p = rng.unit();
        mph = 0;
        lfo = rng.unit();
        fl.reset();
        fr.reset();
        coefIn = 0;
        fenv = 1;
        tail = 1;
        tailMul = 1;
        // Cutoff follows the note half way, so a high run is not duller
        // than a low one.
        keyTrack = std::exp2((s.midi - 60.0f) / 24.0f);
        const float g = s.gateSec, step = s.stepSec;
        auto mulFor = [&](float tau) { return std::exp(-1.0f / (std::max(tau, 0.0005f) * s.rate)); };
        // Envelopes follow the gate: short notes are plucks, long ones swell
        // and sing. The filter closes faster than the level falls, which is
        // what makes a pattern breathe.
        float attack = 0.0012f;
        float decay = clampf(0.5f * step + 1.2f * g, 0.06f, 1.6f);
        float fdecay = clampf(0.3f * step + 0.5f * g, 0.03f, 0.9f);
        float release = 0.05f;
        auto setUnits = [&](int n, const float *cents, const float *pans) {
            units = n;
            for (int i = 0; i < n; ++i) {
                inc[i] = std::min(0.45f, hz * std::exp2(cents[i] / 1200.0f) / s.rate);
                const float p = clampf(pans[i], -1, 1);
                gl[i] = std::sqrt(0.5f * (1.0f - p));
                gr[i] = std::sqrt(0.5f * (1.0f + p));
            }
        };
        resBias = 0;
        cutBias = 0;
        blend = 0;
        pwmDepth = 0;
        switch (sound) {
        case Pluck: {
            const float c[2] = {-7.0f, 7.0f}, p[2] = {-0.55f, 0.55f};
            setUnits(2, c, p);
            blend = s.color;
            attack = 0.0025f;
            sustain = 0.22f;
            envOct = 3.6f;
            gain = 0.78f;
            release = 0.05f + 0.4f * g;
            break;
        }
        case Glass: {
            const float c[2] = {-3.0f, 3.0f}, p[2] = {-0.6f, 0.6f};
            setUnits(2, c, p);
            // The modulator's ratio by colour: octave (round), twelfth
            // (hollow), 3.5 (struck glass), seven (a bright tine).
            static const float ratios[5] = {1.0f, 2.0f, 3.0f, 3.5f, 7.0f};
            const float ratio = ratios[std::clamp(int(s.color * 4.99f), 0, 4)];
            minc = std::min(0.45f, hz * ratio / s.rate);
            fmIdx = 2.4f + 1.6f * s.accent + 1.2f * s.envBoost;
            fmSus = 0.35f + 0.3f * s.color;
            fmMul = mulFor(0.035f + 0.2f * std::min(g, 0.5f));
            sustain = 0.18f;
            decay = clampf(2.0f * step + 1.5f * g, 0.15f, 1.8f);
            envOct = 1.4f;
            cutBias = 1.6f;
            gain = 0.17f;
            release = 0.14f + 0.5f * g;
            break;
        }
        case Saw: {
            const float spread = 8.0f + 42.0f * s.color;
            const float c[7] = {0, -0.11f * spread, 0.11f * spread, -0.36f * spread, 0.36f * spread, -0.78f * spread,
                                0.78f * spread};
            const float p[7] = {0, -0.4f, 0.4f, -0.7f, 0.7f, -0.95f, 0.95f};
            setUnits(7, c, p);
            cutBias = 1.6f; // the anthem arp sits bright
            sustain = 0.6f;
            envOct = 3.0f;
            attack = 0.003f;
            gain = 0.16f;
            release = 0.07f + 0.4f * g;
            break;
        }
        case Pulse: {
            const float c[2] = {-5.0f, 5.0f}, p[2] = {-0.5f, 0.5f};
            setUnits(2, c, p);
            pw = 0.5f - 0.38f * s.color;
            pwmDepth = 0.04f + 0.1f * s.color;
            lfoInc = 0.45f / s.rate;
            sustain = 0.55f;
            envOct = 2.8f;
            gain = 0.3f;
            release = 0.05f + 0.3f * g;
            break;
        }
        case Acid: {
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            blend = s.color;
            sustain = 0.8f;
            envOct = 3.4f + 1.8f * s.accent;
            resBias = 0.35f;
            cutBias = -1.0f;
            fdecay = clampf(0.06f + 0.5f * g, 0.05f, 0.35f) * (s.accent > 0 ? 0.7f : 1.0f);
            gain = 0.48f;
            release = 0.025f;
            break;
        }
        case Marimba: {
            // Low bars left, high bars right, as the player sees them.
            const float c[1] = {0}, p[1] = {clampf((s.midi - 66.0f) / 40.0f, -0.45f, 0.45f)};
            setUnits(1, c, p);
            const float hard = clampf(0.2f + 1.0f * s.color + 0.35f * s.accent + 0.25f * s.envBoost, 0.1f, 1.5f);
            // A low bar rings longer than a high one; the overtones die
            // first, which is what makes it wood and not a bell.
            const float ring = clampf(0.5f * std::sqrt(261.6f / hz), 0.18f, 1.0f);
            tailMul = mulFor(ring);
            // The fourth is the bar's top click: far up, gone within ten
            // milliseconds, louder the harder the mallet.
            static const float ratio[kModes] = {1.0f, 3.98f, 9.88f, 17.3f};
            const float amp[kModes] = {1.0f, 0.5f * hard, 0.28f * hard * hard, 1.1f * hard};
            const float tau[kModes] = {ring, ring * 0.3f, ring * 0.09f, 0.003f};
            for (int k = 0; k < kModes; ++k) {
                const double w = 2.0 * 3.14159265358979 * double(hz) * ratio[k] / double(s.rate);
                const double r = w < 2.6 ? std::exp(-1.0 / (double(tau[k]) * double(s.rate))) : 0.0;
                mc[k] = 2.0 * r * std::cos(w);
                mr2[k] = r * r;
                my2[k] = 0;
                my1[k] = double(amp[k]) * r * std::sin(w);
            }
            // The knock: the mallet's thud, its brightness following the
            // note (a few times its pitch), so a high bar clicks and a low
            // one thumps.
            noise = Rng(s.seed * 2654435761u | 1u);
            burst = 0.9f * hard;
            burstMul = mulFor(0.004f);
            burstA = 1.0f - std::exp(-kTwoPi * clampf(4.0f * hz, 1500.0f, 10000.0f) / s.rate);
            burstLp = 0;
            attack = 0.0006f;
            sustain = 1.0f;
            release = 0.35f + 0.3f * g;
            envOct = 1.0f;
            fdecay = 0.05f;
            cutBias = 2.6f;
            resBias = -0.25f;
            gain = 0.17f;
            break;
        }
        case Harp: {
            const float c[1] = {0}, p[1] = {clampf((s.midi - 66.0f) / 30.0f, -0.6f, 0.6f)};
            setUnits(1, c, p);
            const float bright = clampf(s.color + 0.3f * s.accent + 0.3f * s.envBoost, 0.0f, 1.0f);
            // The loop is N whole samples, the damping's S, and the
            // allpass's fraction d: N + S + d is the period, to the cent.
            const double period = clampf(s.rate / hz, 8.0f, float(kString - 4));
            // Strings ring four or five seconds low, a second or two high:
            // the fundamental's loss per pass, g, is what that asks for.
            const float t60 = clampf(3.6f * std::sqrt(196.0f / hz), 1.0f, 5.0f);
            tailMul = mulFor(t60 / 6.9f);
            const float g1 = std::exp(-6.9f * float(period) / (t60 * s.rate));
            // The damping average loses some of the fundamental every pass,
            // which on a high string (few samples a period, many passes a
            // second) would cut the ring to a fraction. So the damping is
            // held to what the ring allows, |H| ≥ g at the fundamental, and
            // the loss makes up the rest: the fundamental rings as long as
            // asked at every pitch, and nothing in the loop ever gains.
            const float half = std::sin(kPi / float(period));
            const float room = (1.0f - g1 * g1) / (4.0f * half * half);
            sS = 0.46f - 0.3f * bright;
            if (room < 0.25f)
                sS = std::min(sS, 0.5f * (1.0f - std::sqrt(1.0f - 4.0f * room)));
            const float H = std::sqrt(std::max(1.0f - 4.0f * sS * (1.0f - sS) * half * half, 1e-6f));
            sRho = std::min(1.0f, g1 / H);
            sN = int(std::floor(period - double(sS) - 0.1));
            const double d = period - double(sS) - double(sN);
            sC = float((1.0 - d) / (1.0 + d));
            apX = apY = 0;
            // The pluck: the string's shape at the moment it is let go, a
            // triangle peaked where the finger held it (near the middle for
            // a harp's soft tone, towards the end for a nail's twang), with a
            // little noise for the finger itself. Its mean is taken away and
            // it is scaled to the same strength every time.
            Rng rn(s.seed | 1u);
            const float at = 0.38f - 0.28f * bright;
            const float rough = 0.06f + 0.4f * bright * bright;
            const int peak = std::clamp(int(at * float(sN)), 1, sN - 1);
            const int mask = kString - 1;
            float lp = 0;
            sw = 0;
            for (int k = 0; k < sN; ++k) {
                lp += 0.5f * (rn.bipolar() - lp);
                const float shape = k < peak ? float(k) / float(peak) : float(sN - k) / float(sN - peak);
                str[(k - sN) & mask] = shape + rough * lp;
            }
            str[(-sN - 1) & mask] = 0;
            float sum = 0, sq = 0;
            for (int k = 0; k < sN; ++k) {
                sum += str[(k - sN) & mask];
                sq += str[(k - sN) & mask] * str[(k - sN) & mask];
            }
            const float mean = sum / float(sN);
            const float norm = 0.55f / std::sqrt(std::max(sq / float(sN) - mean * mean, 1e-9f));
            for (int k = 0; k < sN; ++k)
                str[(k - sN) & mask] = (str[(k - sN) & mask] - mean) * norm;
            attack = 0.0004f;
            sustain = 1.0f;
            release = 0; // none: the string rings on after the arp lets go
            envOct = 0.8f;
            fdecay = 0.08f;
            cutBias = 2.4f;
            resBias = -0.3f;
            gain = 0.15f;
            break;
        }
        case Rhodes: {
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            const float v = clampf(s.vel, 0, 1.2f);
            // Hit harder, the tine swings closer to the pickup and the note
            // barks: the index grows with velocity, more so with colour. It
            // stops at 1.3, where the fundamental still leads; past it the
            // note hollows into a DX's honk, not a Mark I's bark.
            fmIdx = std::min(1.3f, 0.3f + (0.4f + 0.9f * s.color) * v + 0.4f * s.accent + 0.3f * s.envBoost);
            fmSus = 0.15f + 0.25f * s.color;
            fmMul = mulFor(0.22f);
            // The pickup hears the tine lopsided, nearer on one swing than
            // the other: even harmonics, the growl, more the harder it is hit.
            growl = 0.08f + 0.3f * v * (0.5f + s.color) + 0.15f * s.accent;
            // The tine's ping: a little over fourteen times up (a tine is
            // not quite in tune with its own overtones), gone in a few
            // milliseconds; seven for notes where fourteen would alias.
            const float tr = hz * 14.2f < 0.42f * s.rate ? 14.13f : hz * 7.1f < 0.42f * s.rate ? 7.06f : 0.0f;
            minc = hz * tr / s.rate;
            mph = 0;
            tine = tr > 0 ? 0.9f + 0.8f * v : 0.0f;
            tineMul = mulFor(0.009f);
            // The suitcase's tremolo, one LFO for every note.
            lfoInc = 4.6f / s.rate;
            lfo = float(s.clock * 4.6 - std::floor(s.clock * 4.6));
            trem = 0.42f;
            attack = 0.0012f;
            sustain = 0.0f;
            decay = clampf(3.2f * std::pow(261.6f / hz, 0.3f), 1.2f, 4.5f);
            release = 0.09f;
            envOct = 0.6f;
            fdecay = 0.12f;
            cutBias = 2.8f;
            resBias = -0.3f;
            gain = 0.205f;
            break;
        }
        case Juno: {
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            inc[1] = 0.5f * inc[0]; // the sub, an octave down
            ph[1] = 0;
            // Colour: up to 0.6 the saw blends into the pulse; from there it
            // is all pulse, and the whole way the width LFO speeds up, from
            // a slow 0.15 Hz drift to 3.4 Hz shimmer (1.3 Hz at the start).
            blend = clampf(1.65f * s.color, 0, 1);
            pwmDepth = 0.36f;
            const double pwmHz = 0.15 * std::exp2(4.5 * double(s.color));
            lfoInc = float(pwmHz) / s.rate;
            lfo = float(s.clock * pwmHz - std::floor(s.clock * pwmHz));
            // The Juno's HPF slider, folded into the top of the colour: flat
            // up to 0.45, then rising to 700 Hz (about 270 Hz at the start),
            // the thin, glassy arp the Juno is known for. It thins the
            // oscillator only; the sub stays under it as the floor.
            const float thin = clampf((s.color - 0.45f) / 0.55f, 0, 1);
            const float hpHz = 700.0f * std::pow(thin, 1.2f);
            hpA = hpHz > 1.0f ? 1.0f - std::exp(-kTwoPi * hpHz / s.rate) : 0.0f;
            hp1 = hp2 = 0;
            attack = 0.002f;
            sustain = 0.4f;
            envOct = 3.2f;
            resBias = 0.2f;
            cutBias = -0.2f;
            gain = 0.36f;
            release = 0.06f + 0.35f * g;
            break;
        }
        case Organ: {
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            inc[1] = 0.5f * inc[0]; // 16'
            ph[1] = 0;
            ph[0] = 0;
            // Colour pulls the drawbars from a soft jazz registration to
            // nearly all out; the parts above the ear's top are left in.
            static const float soft[5] = {0.35f, 1.0f, 0.45f, 0.0f, 0.15f};
            static const float full[5] = {0.75f, 1.0f, 0.8f, 0.6f, 0.65f};
            float sum = 0;
            for (int k = 0; k < 5; ++k) {
                draw[k] = lerpf(soft[k], full[k], s.color);
                sum += draw[k];
            }
            for (float &d : draw)
                d *= 1.5f / sum;
            perc = 0.7f + 0.4f * s.accent;
            percMul = mulFor(0.16f);
            noise = Rng(s.seed * 2654435761u | 1u);
            burst = 0.12f;
            burstMul = mulFor(0.0015f);
            burstA = 1.0f - std::exp(-kTwoPi * 5000.0f / s.rate);
            burstLp = 0;
            attack = 0.0015f;
            sustain = 1.0f;
            release = 0.035f;
            envOct = 0.0f;
            cutBias = 2.3f;
            resBias = -0.3f;
            gain = 0.245f;
            break;
        }
        case Steelpan: {
            // High notes right, as a tenor pan's notes run round its bowl.
            const float c[1] = {0}, p[1] = {clampf((s.midi - 66.0f) / 40.0f, -0.4f, 0.4f)};
            setUnits(1, c, p);
            const float hard = clampf(0.15f + 0.9f * s.color + 0.35f * s.accent + 0.25f * s.envBoost, 0.05f, 1.4f);
            // A low note rings longer than a high one: the top of a tenor
            // pan is small, stiff notes.
            const float ring = clampf(std::pow(220.0f / hz, 0.35f), 0.3f, 1.5f);
            tailMul = mulFor(ring);
            // A pan's note is a hammered dome tuned by ear to its octave and
            // twelfth, never quite there, and a hard stroke stiffens the
            // metal while it swings widest: the partials stretch with the
            // stroke (the octave 2.00 to 2.01, the twelfth 2.98 to 3.02) and
            // the whole note starts a few cents sharp and settles. That
            // stretch, not the levels, is what tells a pan from a marimba's
            // clean lines.
            const float st = clampf(hard / 1.2f, 0.0f, 1.0f);
            pr[0] = 1.0f;
            pr[1] = 2.001f + 0.006f * st;
            pr[2] = 2.982f + 0.03f * st;
            pr[3] = 4.01f + 0.03f * st;
            prBend[0] = 0;
            prBend[1] = 0.004f * st;
            prBend[2] = 0.012f * st;
            prBend[3] = 0.02f * st;
            bend = 1.0f;
            bendDepth = 0.0012f + 0.0035f * st * st; // 2 to 8 cents, soft to hardest
            bendMul = mulFor(0.07f);
            // The fundamental, the octave, the twelfth and the double
            // octave, the upper ones falling first; any above the ear's top
            // are left out rather than folded back down. The twelfth is
            // strong, as on a pan and on no bar: it is the nasal ring.
            const float lv[4] = {1.0f, 0.62f + 0.18f * hard, 0.3f + 0.3f * hard, 0.1f * hard * hard};
            const float tau[4] = {ring, 0.7f * ring, 0.32f * ring, 0.1f * ring};
            for (int k = 0; k < 4; ++k) {
                pa[k] = hz * pr[k] * 1.02f < 0.45f * s.rate ? lv[k] : 0.0f;
                paMul[k] = mulFor(tau[k]);
                ph[k] = 0;
            }
            // The octave swells in: it is its own fall less a faster one,
            // so it rises from nothing to its peak some 20 ms after the
            // stick, the pan's soft bloom; the twelfth blooms a little
            // later still, so the note opens up after the stick.
            swell = 1.0f;
            swellMul = mulFor(0.011f);
            swell3 = 1.0f;
            swell3Mul = mulFor(0.024f);
            // The shimmer: a second octave a hertz sharp, beating slowly
            // against the first.
            minc = 2.0f * hz < 0.45f * s.rate ? (2.0f * hz + 1.1f) / s.rate : 0.0f;
            mph = 0.25f;
            // The neighbours: on a pan the note a fifth up sits beside this
            // one, and the note an octave down is its outer ring. Each is
            // its own dome with a fundamental and an octave, rung through the
            // skirt by the stick and by whatever of this note lands near its
            // own partials (the fifth's octave beside this twelfth, the low
            // note's octave on this fundamental), so they answer quietly,
            // beating a little against it, after the stroke has gone.
            // The stroke sets each ringing at once (the shock through the
            // skirt); the note then feeds whichever lies near its own
            // partials, so those swell on after the stroke has gone.
            static const float nRatio[kNeighbours] = {1.4983f, 2.9966f * 1.003f, 0.5f, 1.003f};
            const float nLevel[kNeighbours] = {0.05f + 0.05f * hard, 0.03f + 0.04f * hard, 0.05f + 0.04f * hard,
                                               0.04f};
            for (int k = 0; k < kNeighbours; ++k) {
                const double w = 2.0 * 3.14159265358979 * double(hz) * nRatio[k] / double(s.rate);
                const double r = w < 2.6 && w > 0.004 ? std::exp(-1.0 / (1.3 * double(ring) * double(s.rate))) : 0.0;
                nc[k] = 2.0 * r * std::cos(w);
                nr2[k] = r * r;
                ny2[k] = 0;
                ny1[k] = double(nLevel[k]) * r * std::sin(w);
                nIn[k] = float((1.0 - r) * 2.0 * std::sin(w));
            }
            // The stick: rubber, so a soft, short thud that follows the
            // note's height.
            noise = Rng(s.seed * 2654435761u | 1u);
            burst = 0.25f + 0.45f * hard;
            burstMul = mulFor(0.0025f);
            burstA = 1.0f - std::exp(-kTwoPi * clampf(3.0f * hz, 900.0f, 5000.0f) / s.rate);
            burstLp = 0;
            // The roll: a pannist holds a long note by striking it again
            // and again with both sticks, softly, some eleven to fourteen
            // times a second, and that tremolo, more than any partial, is
            // what says pan to a listener. So a note given most of a slow
            // step (a gate past 0.8 of a step of a fifth of a second or
            // more: eighths at up to 150 BPM) is rolled while its gate
            // holds; a short one is the single stroke it was.
            for (int k = 0; k < 4; ++k)
                paBase[k] = pa[k];
            rollEvery = 0;
            rollN = 0;
            rollRamp = 0;
            if (s.gateSec > 0.8f * s.stepSec && s.stepSec > 0.19f) {
                const float perSecond = 11.0f + 2.5f * rng.unit();
                rollEvery = s.rate / perSecond;
                rollIn = 1.1f * rollEvery;
                rollHard = hard;
                // Between restrikes each stroke's energy spreads into the
                // skirt and is gone within a tenth of a second: a rolled
                // note is what the sticks keep putting in, not the first
                // stroke's long ring, and that rise and fall a dozen times
                // a second is the tremolo heard.
                rollMul = mulFor(0.08f);
                // Each restrike swells in over a millisecond and a half
                // rather than stepping, which would click.
                rollRampLen = std::max(1, int(0.0015f * s.rate));
            }
            attack = 0.002f;
            sustain = 1.0f;
            release = 0.3f + 0.5f * g;
            envOct = 0.8f;
            fdecay = 0.06f;
            cutBias = 2.4f;
            resBias = -0.3f;
            // A rolled note is the restrikes' rise and fall, on average some
            // 3 dB under a single stroke's ring: lifted to sit level with it.
            gain = rollEvery > 0 ? 0.13f * 1.4f : 0.13f;
            break;
        }
        case Sitar: {
            // The main string in the middle: its sympathetic strings are
            // the bus's (see Taraf), spread either side.
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            bus = 3;
            const float open = clampf(s.color + 0.25f * s.accent + 0.3f * s.envBoost, 0.0f, 1.0f);
            const double period = clampf(s.rate / hz, 8.0f, float(kString - 4));
            const float t60 = clampf(2.6f * std::sqrt(220.0f / hz), 0.9f, 3.6f);
            tailMul = mulFor(t60 / 6.9f);
            // The damping held to what the ring allows, as for the harp.
            const float g1 = std::exp(-6.9f * float(period) / (t60 * s.rate));
            const float half = std::sin(kPi / float(period));
            const float room = (1.0f - g1 * g1) / (4.0f * half * half);
            // A steel string: brighter than the harp's gut, and the brighter
            // the shorter, since a high string passes the loop's filter
            // many more times a second.
            sS = (0.18f - 0.1f * open) * clampf(220.0f / hz, 0.15f, 1.0f);
            if (room < 0.25f)
                sS = std::min(sS, 0.5f * (1.0f - std::sqrt(1.0f - 4.0f * room)));
            const float H = std::sqrt(std::max(1.0f - 4.0f * sS * (1.0f - sS) * half * half, 1e-6f));
            sRho = std::min(1.0f, g1 / H);
            // The DC blocker in the loop (the bridge's flattening is one
            // sided, so it would otherwise pile up an offset) leads the
            // fundamental by a little phase: the loop is that much longer.
            dcR = 0.9995f;
            dcX = dcY = 0;
            const double w = 2.0 * 3.14159265358979 / period;
            const double lead = std::atan2(std::sin(w), 1.0 - std::cos(w)) -
                                std::atan2(double(dcR) * std::sin(w), 1.0 - double(dcR) * std::cos(w));
            const double loop = period + lead / w;
            sN = std::clamp(int(std::floor(loop - double(sS) - 0.1)), 2, kString - 4);
            const double d = loop - double(sS) - double(sN);
            sC = float((1.0 - d) / (1.0 + d));
            apX = apY = 0;
            // The jawari: the string rests on the bridge while it is past
            // (1 − jaw) of its swing, and is then shorter by jawCut. The
            // swing it is measured against falls at the string's own pace,
            // so the buzz lasts as long as the note does. A contact costs
            // the string a little each pass, and a high string makes many
            // more passes a second, each taking a little of the kink's
            // brightness away in the loop's filter. What is held even
            // across the range is the buzz heard per second, not the cut
            // per pass: the cut is a share of the period, and that share
            // grows up high (to over twice the A3's at A5) to make up for
            // the extra passes, so above A4 it still buzzes, as a sitar's
            // top frets do.
            const float reach = clampf(std::pow(hz / 220.0f, 0.4f), 1.0f, 2.2f);
            jaw = 0.5f + 0.35f * open;
            jawCut = (0.012f + 0.045f * open) * reach * float(period);
            jawNow = 0;
            swingMul = mulFor(t60 / 6.9f);
            // A meend: the string plucked where the last note was and
            // pulled along the fret into this one, in some 100 ms. The pull
            // is on the string's length, not an oscillator's step: the
            // loop's length and the pitch it was tuned to, and from there
            // how far the pull stretches it now. At most a fifth away.
            sLoop = float(loop);
            sMidi = s.midi;
            sBend = 0;
            glide = 1.0f - std::exp(-8.0f / (0.03f * s.rate));
            if (s.fromMidi > 0) {
                midiNow = clampf(s.fromMidi, s.midi - 7.0f, s.midi + 7.0f);
                sBend = sLoop * (std::exp2((sMidi - midiNow) / 12.0f) - 1.0f);
            }
            // The mizrab, a wire plectrum near the end of the string: a
            // sharp triangle and a scratch of noise, laid along as much of
            // the string as the first pass reads.
            const int fill = std::clamp(int(std::ceil(float(sN) + std::max(sBend, 0.0f))) + 1, sN, kString - 8);
            Rng rn(s.seed | 1u);
            const int peak = std::clamp(int(0.12f * float(fill)), 1, fill - 1);
            const int mask = kString - 1;
            float lp = 0;
            sw = 0;
            for (int k = 0; k < fill; ++k) {
                lp += 0.6f * (rn.bipolar() - lp);
                const float shape = k < peak ? float(k) / float(peak) : float(fill - k) / float(fill - peak);
                str[(k - fill) & mask] = shape + (0.12f + 0.25f * open) * lp;
            }
            str[(-fill - 1) & mask] = 0;
            str[(-fill - 2) & mask] = 0;
            float sum = 0, sq = 0;
            for (int k = 0; k < fill; ++k) {
                sum += str[(k - fill) & mask];
                sq += str[(k - fill) & mask] * str[(k - fill) & mask];
            }
            const float mean = sum / float(fill);
            const float norm = 0.55f / std::sqrt(std::max(sq / float(fill) - mean * mean, 1e-9f));
            for (int k = 0; k < fill; ++k)
                str[(k - fill) & mask] = (str[(k - fill) & mask] - mean) * norm;
            swing = 0.55f * 1.7f;
            attack = 0.0004f;
            sustain = 1.0f;
            release = 0; // the string rings on, as the harp's does
            envOct = 1.0f;
            fdecay = 0.1f;
            cutBias = 2.6f;
            resBias = -0.3f;
            // The bridge spends more of a high string's energy, so its
            // notes are lifted to sit with the low ones.
            gain = 0.225f * clampf(std::pow(hz / 220.0f, 0.5f), 1.0f, 2.1f);
            break;
        }
        case LogDrum: {
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            ph[0] = 0;
            incNote = std::min(0.45f, hz / s.rate);
            // Colour runs from a round, sung log (Kelvin Momo's) to a
            // gritty, bending one (Kabza De Small's).
            const float grit = clampf(s.color + 0.2f * s.envBoost, 0.0f, 1.0f);
            // The yelp: on an accent the log bends up a few semitones in
            // its first ten milliseconds and back, before it falls, the
            // squeal Kabza De Small's logs have. It is the difference of
            // two falls, 5 and 30 ms, which peaks at 0.583 some 11 ms in;
            // the depth is divided by that so it means what it says.
            yelp = s.accent > 0 ? (0.22f + 0.2f * s.accent) * (0.6f + 0.4f * grit) / 0.583f : 0.0f;
            yelpSlow = yelpFast = 1.0f;
            yelpSlowMul = mulFor(0.03f);
            yelpFastMul = mulFor(0.005f);
            if (s.fromMidi > 0) {
                // A log after a log glides there from the last one's pitch,
                // as a producer's portamento does, instead of thumping down
                // into every note afresh. How long it takes is the player's,
                // on the tone pad's twist (see logGlideSec); three of the
                // fall's time constants make the time.
                const float glideSec = logGlideSec(s.res);
                drop = clampf((s.fromMidi - s.midi) / 12.0f, -2.0f, 2.0f);
                dropMul = mulFor(glideSec / 3.0f);
            } else {
                // The first: a fall from above into the note (further and
                // slower the grittier, more on an accent), the log's bwomp;
                // with a yelp, only a little, so the bend up is heard.
                drop = yelp > 0 ? 0.08f : 0.6f + 0.6f * grit;
                dropMul = mulFor(0.006f + 0.014f * grit);
            }
            inc[0] = std::min(0.45f, incNote * std::exp2(drop));
            // The knock: the sine phase-modulated by itself, clearing to the
            // pure tone in a few tens of milliseconds.
            fmIdx = 0.8f + 1.4f * grit + 0.8f * s.accent + 0.6f * s.envBoost;
            fmMul = mulFor(0.018f + 0.01f * grit);
            // The hollow of the log, a little over two and a half times up.
            minc = hz * 2.63f < 0.45f * s.rate ? hz * 2.63f / s.rate : 0.0f;
            mph = 0;
            tine = minc > 0 ? 0.3f + 0.2f * grit : 0.0f;
            tineMul = mulFor(0.03f);
            // The tok: a narrow band swept from about 3 kHz down to 900 Hz
            // in the first 30 ms, picking the knock's overtones out of the
            // sine as it passes: the nasal attack that says "log" before
            // the pitch does.
            tok = 0.9f + 0.9f * grit + 0.6f * s.accent;
            tokEnv = 1.0f;
            tokMul = mulFor(0.028f);
            tokHz = 900.0f;
            tokF.reset();
            // The drive: round below half colour, then a tanh pushed harder
            // and leaning to one side (the even harmonics of a saturated
            // log), held back on high notes so it does not fold.
            drive = (0.6f + 3.4f * grit * grit) * clampf(std::sqrt(330.0f / hz), 0.35f, 1.0f);
            driveNorm = 1.0f / fastTanh(drive * 0.9f);
            asym = 0.35f * grit;
            // The tail: an amapiano log sits long and low, the bottom notes
            // (an octave under the register, 40 to 70 Hz) ringing half a
            // second to most of a second before they are 40 dB down, the
            // high ones shorter, a longer gate longer still. The gate does
            // not cut it short: the log rings on until the next one takes
            // over from it (one log at a time) or it has died away.
            const float low = clampf(std::sqrt(110.0f / hz), 0.6f, 1.35f);
            const float ringSec = clampf(0.32f + 1.6f * g, 0.32f, 0.72f) * low;
            attack = 0.0008f;
            sustain = 0.0f;
            decay = ringSec / 4.6f / 0.45f; // 4.6 time constants to −40 dB, as decayMul takes them
            release = decay;
            // A resonant low-pass opening wide on the stroke and closing
            // on the note: the filter's sweep under the tok.
            envOct = 2.2f + 0.6f * grit;
            fdecay = 0.045f + 0.03f * grit;
            cutBias = 0.5f;
            resBias = 0.25f + 0.15f * grit;
            gain = 0.39f;
            break;
        }
        case Chip: {
            const float c[1] = {0}, p[1] = {0};
            setUnits(1, c, p);
            ph[0] = 0; // the channel restarts its wave on a trigger
            static const float duties[3] = {0.125f, 0.25f, 0.5f};
            const int band = std::clamp(int(s.color * 2.99f), 0, 2);
            duty = duties[band];
            // Within each duty's third of colour, how much of the DAC's
            // centre reaches the capacitor: at the third's start none, a
            // clean blip that sits in a mix; at its end all of it, the
            // hardware's tick on every note. A press on the tone (the
            // bite) adds the grain. What does get through follows the
            // pulse's centre over up to 1.5 ms instead of stepping, so even
            // halfway the tick is a soft knock rather than a click.
            grain = clampf(s.color * 3.0f - float(band) + s.envBoost, 0.0f, 1.0f);
            centre = 0;
            centreA = grain > 0.999f ? 1.0f
                                     : 1.0f - std::exp(-1.0f / ((0.0001f + 0.0014f * (1.0f - grain)) * 2.0f * s.rate));
            // The volume envelope's pace: a step every n/64 s, n from 1 to 7,
            // longer the longer the gate, so short notes blip and long ones
            // fade the way a tracker's do.
            const int pace = std::clamp(int(1.0f + 9.0f * g), 1, 7);
            chipEvery = std::max(1, int(s.rate * float(pace) / 64.0f));
            chipCount = chipEvery;
            chipVol = 15;
            chipLevel = chipSmooth = 1.0f;
            // The pitch is the register's, rounded as the hardware's is:
            // in tune low down, and a few cents off at the top, where a step
            // of the register is a wide one, as on the real thing.
            // A chord is one channel taking its notes in turn, one each
            // frame of the driver, 60 a second: the chiptune chord.
            arpN = std::clamp(s.chord, 1, 4);
            for (int k = 0; k < arpN; ++k)
                arpReg[k] = chipRegister(midiToHz(s.chord > 1 ? s.chordMidi[k] : s.midi));
            arpAt = 0;
            regTo = arpReg[0];
            frameLen = s.rate / 59.73f;
            frameIn = frameLen;
            // The accent is channel 1's sweep: the register starts an
            // octave up and the hardware takes x >> n off it 128 times a
            // second until it lands on the note, a stepped fall heard as
            // the stairs it is. The shift is chosen for some eight steps.
            sweepLen = s.rate / 128.0f;
            sweepIn = sweepLen;
            sweepShift = 0;
            reg = regTo;
            if (s.accent > 0 || s.envBoost > 0.3f) {
                reg = std::round(2048.0f - 0.5f * (2048.0f - regTo));
                const float per = std::max(1.0f, (reg - regTo) / 8.0f);
                sweepShift = std::clamp(int(std::lround(std::log2(reg / per))), 1, 7);
            }
            incNote = std::min(0.45f, chipHz(reg) / s.rate);
            inc[0] = incNote;
            // The vibrato waits 0.12 s, then grows over a quarter second
            // to ±12 cents at 5.5 Hz: only held notes sing. The driver moves
            // it once a frame, so it steps.
            vibWait = 0.12f * s.rate;
            vibDepth = 0;
            vibGrow = 0.007f / (0.25f * 59.73f);
            lfo = 0;
            lfoInc = 5.5f / 59.73f;
            // The output's capacitor: a high-pass at some 28 Hz, as on the
            // handheld. The DAC is one-sided, so each trigger and each step
            // of the volume moves its centre, and the capacitor makes that a
            // tick: the hardware's own grain, let through by colour (above).
            capR = std::exp(-kTwoPi * 28.0f / s.rate);
            capX = capY = 0;
            std::fill(hb, hb + kHalfBand, 0.0f);
            hbAt = 0;
            attack = 0.0004f;
            sustain = 1.0f;
            release = 0.008f;
            envOct = 0.0f;
            fdecay = 0.05f;
            cutBias = 3.2f;
            resBias = -0.35f;
            gain = 0.33f;
            break;
        }
        case Gamelan: {
            // Two players, each on a pair of keys: the polos on the left,
            // the sangsih on the right, each pair spread a little.
            const float c[2] = {0, 0};
            const float p[2] = {s.sangsih ? 0.45f : -0.8f, s.sangsih ? 0.8f : -0.45f};
            setUnits(2, c, p);
            const float hard = clampf(0.25f + 0.9f * s.color + 0.35f * s.accent + 0.25f * s.envBoost, 0.1f, 1.5f);
            const float ring = clampf(1.4f * std::pow(220.0f / hz, 0.4f), 0.45f, 2.4f);
            tailMul = mulFor(ring);
            // The ombak: the second key a few hertz sharp, faster up high
            // as a gong kebyar is tuned (7.5 Hz at A3).
            const float beat = clampf(7.5f * std::pow(hz / 220.0f, 0.6f), 3.5f, 14.0f);
            static const float ratio[3] = {1.0f, 2.76f, 5.40f};
            // Bronze, not wood: the upper partials ring on well past the
            // stroke, beating against their partners all the faster.
            const float amp[3] = {1.0f, 0.1f + 0.35f * hard, 0.2f * hard * hard};
            const float tau[3] = {ring, 0.4f * ring, 0.18f * ring};
            // The sangsih plays another pair of gangsa, whose upper key
            // beats a little faster: where the two players meet on one key
            // the pairs beat against each other too, a slow swell under the
            // ombak that is heard only on the shared notes.
            const double upper = s.sangsih ? 1.35 : 1.0;
            for (int key = 0; key < 2; ++key) {
                const double f0 = key ? double(hz) + upper * double(beat) : double(hz);
                const double level = key ? 0.85 : 1.0;
                for (int k = 0; k < 3; ++k) {
                    const int i = 3 * key + k;
                    const double w = 2.0 * 3.14159265358979 * f0 * ratio[k] / double(s.rate);
                    const double r = w < 2.6 ? std::exp(-1.0 / (double(tau[k]) * double(s.rate))) : 0.0;
                    gc[i] = 2.0 * r * std::cos(w);
                    gr2[i] = r * r;
                    gy2[i] = 0;
                    gy1[i] = level * double(amp[k]) * r * std::sin(w);
                }
            }
            // The panggul, a wooden mallet on bronze: a bright, short ping.
            noise = Rng(s.seed * 2654435761u | 1u);
            burst = 0.5f * hard;
            burstMul = mulFor(0.0018f);
            burstA = 1.0f - std::exp(-kTwoPi * clampf(6.0f * hz, 3000.0f, 12000.0f) / s.rate);
            burstLp = 0;
            attack = 0.0005f;
            sustain = 1.0f;
            release = 0.25f + 0.9f * g;
            envOct = 0.6f;
            fdecay = 0.05f;
            cutBias = 2.6f;
            resBias = -0.3f;
            gain = 0.093f;
            break;
        }
        default:
            break;
        }
        gain *= s.lift;
        envOct *= 0.6f + 0.4f * clampf(s.vel, 0, 1);
        envOct += 1.4f * s.accent + 2.2f * s.envBoost;
        attackInc = 1.0f / (attack * s.rate);
        decayMul = mulFor(decay * 0.45f);
        releaseMul = release > 0 ? mulFor(release * 0.45f) : 1.0f;
        fenvMul = mulFor(fdecay * 0.45f);
    }

    // A tied acid note: the voice glides to the next pitch without a new
    // attack, the way a 303 slides.
    void slide(float midi, float gateSec, float rate) {
        midiTo = midi;
        gate = std::max(1, int(gateSec * rate));
    }

    void release() {
        if (stage != 2) {
            stage = 2;
            released = true;
        }
    }
    // Stolen to make room: out within a few milliseconds.
    void fade(float rate) {
        fading = true;
        stage = 2;
        released = true;
        fadeMul = std::exp(-1.0f / (0.003f * rate));
    }
    // A gangsa player's left hand on the key just struck as the right
    // strikes the next: gone in a few tens of milliseconds, softer than a
    // fade, so a damped key is heard stopping rather than cut.
    void damp(float rate) {
        releaseMul = std::min(releaseMul, std::exp(-1.0f / (0.012f * rate)));
        stage = 2;
        released = true;
    }

    // The marimba's knock or the organ's click: a burst of noise, darkened,
    // falling away within milliseconds, and then nothing at all.
    float knock() {
        if (burst < 1e-5f && std::fabs(burstLp) < 1e-6f)
            return burst = burstLp = 0.0f;
        burst *= burstMul;
        burstLp += burstA * (noise.bipolar() * burst - burstLp);
        return burstLp;
    }

    // Adds samples [from, to) to the dry and echo buses.
    void render(float *l, float *r, float *el, float *er, int from, int to, float cutHz, float res, float send,
                float rate) {
        // The log drum's twist is its glide, so its filter keeps a
        // resonance of its own.
        const float resonance = clampf((sound == LogDrum ? 0.3f : res) + resBias, 0.0f, 0.97f);
        const float base = cutHz * keyTrack * std::exp2(cutBias);
        const float resComp = 1.0f + 0.35f * resonance * resonance;
        float peak = 0;
        for (int i = from; i < to; ++i) {
            if (stage == 0) {
                amp += attackInc;
                if (amp >= 1.0f) {
                    amp = 1.0f;
                    stage = 1;
                }
            } else if (stage == 1) {
                amp = sustain + (amp - sustain) * decayMul;
            } else {
                amp *= fading ? fadeMul : releaseMul;
            }
            if (!released && --gate <= 0)
                release();
            // Envelopes that only fall stop at zero rather than creep on
            // into denormals while a long note holds.
            fenv = fenv > 1e-6f ? fenv * fenvMul : 0.0f;
            tail = tail > 1e-6f ? tail * tailMul : 0.0f;
            if (--coefIn <= 0) {
                if (midiNow != midiTo) {
                    midiNow += (midiTo - midiNow) * glide;
                    if (std::fabs(midiTo - midiNow) < 0.01f)
                        midiNow = midiTo;
                    inc[0] = std::min(0.45f, midiToHz(midiNow) / rate);
                    // A sitar's meend: the same pull, on the string's length.
                    if (sound == Sitar)
                        sBend = sLoop * (std::exp2((sMidi - midiNow) / 12.0f) - 1.0f);
                }
                coefIn = 8;
                const float fc = std::min(base * std::exp2(envOct * fenv), rate * (0.42f - 0.1f * resonance));
                fl.set(fc, resonance, rate);
                if (units > 1)
                    fr.set(fc, resonance, rate);
            }
            float sl = 0, sr = 0;
            float panL = gl[0], panR = gr[0]; // the mono sounds' place, the rhodes's moving
            switch (sound) {
            case Pluck:
            case Acid:
                for (int u = 0; u < units; ++u) {
                    float &p = ph[u];
                    p += inc[u];
                    if (p >= 1.0f)
                        p -= 1.0f;
                    const float v = (1.0f - blend) * sawBlep(p, inc[u]) + blend * 0.8f * squareBlep(p, inc[u]);
                    sl += v * gl[u];
                    sr += v * gr[u];
                }
                break;
            case Saw:
                for (int u = 0; u < units; ++u) {
                    float &p = ph[u];
                    p += inc[u];
                    if (p >= 1.0f)
                        p -= 1.0f;
                    const float v = sawBlep(p, inc[u]);
                    sl += v * gl[u];
                    sr += v * gr[u];
                }
                break;
            case Pulse: {
                lfo += lfoInc;
                if (lfo >= 1.0f)
                    lfo -= 1.0f;
                const float w = clampf(pw + pwmDepth * sin2pi(lfo), 0.08f, 0.92f);
                for (int u = 0; u < units; ++u) {
                    float &p = ph[u];
                    p += inc[u];
                    if (p >= 1.0f)
                        p -= 1.0f;
                    const float v = 0.5f * (sawBlep(p, inc[u]) - sawBlep(frac(p + w), inc[u]));
                    sl += v * gl[u];
                    sr += v * gr[u];
                }
                break;
            }
            case Glass: {
                mph += minc;
                if (mph >= 1.0f)
                    mph -= 1.0f;
                fmIdx = fmSus + (fmIdx - fmSus) * fmMul;
                const float m = fmIdx * 0.159155f * sin2pi(mph);
                for (int u = 0; u < units; ++u) {
                    float &p = ph[u];
                    p += inc[u];
                    if (p >= 1.0f)
                        p -= 1.0f;
                    const float v = sin2pi(frac(p + m));
                    sl += v * gl[u];
                    sr += v * gr[u];
                }
                break;
            }
            case Marimba: {
                double m = 0;
                for (int k = 0; k < kModes; ++k) {
                    const double y = mc[k] * my1[k] - mr2[k] * my2[k];
                    my2[k] = my1[k];
                    my1[k] = y;
                    m += y;
                }
                sl = float(m) + knock();
                break;
            }
            case Harp: {
                constexpr int mask = kString - 1;
                const float x0 = str[(sw - sN) & mask], x1 = str[(sw - sN - 1) & mask];
                const float lp = x0 + sS * (x1 - x0);
                const float ap = sC * (lp - apY) + apX;
                apX = lp;
                apY = ap;
                str[sw] = ap * sRho;
                sw = (sw + 1) & mask;
                sl = x0;
                break;
            }
            case Rhodes: {
                float &p = ph[0];
                p += inc[0];
                if (p >= 1.0f)
                    p -= 1.0f;
                mph += minc;
                if (mph >= 1.0f)
                    mph -= 1.0f;
                fmIdx = fmSus + (fmIdx - fmSus) * fmMul;
                tine = tine > 1e-5f ? tine * tineMul : 0.0f;
                const float m = 0.159155f * (fmIdx * sin2pi(p) + tine * sin2pi(mph));
                const float x = sin2pi(frac(p + m));
                sl = x + growl * (x * x - 0.5f); // a sine's square averages a half: no DC
                lfo += lfoInc;
                if (lfo >= 1.0f)
                    lfo -= 1.0f;
                const float pan = trem * sin2pi(lfo);
                panL = gl[0] * (1.0f - pan);
                panR = gr[0] * (1.0f + pan);
                break;
            }
            case Juno: {
                float &p = ph[0], &q = ph[1];
                p += inc[0];
                if (p >= 1.0f)
                    p -= 1.0f;
                q += inc[1];
                if (q >= 1.0f)
                    q -= 1.0f;
                lfo += lfoInc;
                if (lfo >= 1.0f)
                    lfo -= 1.0f;
                const float w = 0.5f - pwmDepth * (0.5f + 0.5f * sin2pi(lfo));
                const float saw = sawBlep(p, inc[0]);
                const float pulse = saw - sawBlep(frac(p + w), inc[0]); // ±1 swing, no DC
                float top = (1.0f - blend) * saw + blend * 0.9f * pulse;
                if (hpA > 0) {
                    hp1 += hpA * (top - hp1);
                    top -= hp1;
                    hp2 += hpA * (top - hp2);
                    top -= hp2;
                }
                sl = top + 0.5f * squareBlep(q, inc[1]);
                break;
            }
            case Organ: {
                float &p = ph[0], &q = ph[1];
                p += inc[0];
                if (p >= 1.0f)
                    p -= 1.0f;
                q += inc[1];
                if (q >= 1.0f)
                    q -= 1.0f;
                perc = perc > 1e-5f ? perc * percMul : 0.0f;
                sl = knock() + draw[0] * sin2pi(q) + draw[1] * sin2pi(p) + draw[2] * sin2pi(frac(2.0f * p)) +
                     (draw[3] + perc) * sin2pi(frac(3.0f * p)) + draw[4] * sin2pi(frac(4.0f * p));
                break;
            }
            case Steelpan: {
                // The roll's restrikes, while the gate holds: the hands
                // alternate, the leading one a little firmer, and neither
                // is a clock. A soft stick sounds the upper partials less,
                // and the note's tail starts again with each.
                const bool rolling = rollEvery > 0 && !released;
                if (rolling && (rollIn -= 1.0f) <= 0.0f) {
                    ++rollN;
                    rollIn += rollEvery * (0.92f + 0.16f * noise.unit());
                    const float v = ((rollN & 1) ? 0.9f : 0.75f) * (0.9f + 0.2f * noise.unit());
                    for (int k = 0; k < 4; ++k)
                        rollTo[k] = std::max(pa[k], paBase[k] * (k < 2 ? v : v * std::sqrt(v)));
                    rollRamp = rollRampLen;
                    burst = std::max(burst, (0.25f + 0.45f * rollHard) * 0.5f * v);
                    bend = std::max(bend, 0.3f * v);
                    tail = 1.0f;
                }
                if (rollRamp > 0) {
                    for (int k = 0; k < 4; ++k)
                        pa[k] += (rollTo[k] - pa[k]) / float(rollRamp);
                    --rollRamp;
                } else if (rolling) {
                    for (float &a : pa)
                        a *= rollMul;
                }
                bend = bend > 1e-5f ? bend * bendMul : 0.0f;
                const float f0 = inc[0] * (1.0f + bendDepth * bend);
                float x = knock();
                for (int k = 0; k < 4; ++k) {
                    pa[k] = pa[k] > 1e-6f ? pa[k] * paMul[k] : 0.0f;
                    ph[k] += f0 * (pr[k] + prBend[k] * bend);
                    if (ph[k] >= 1.0f)
                        ph[k] -= 1.0f;
                }
                mph += minc;
                if (mph >= 1.0f)
                    mph -= 1.0f;
                swell *= swellMul;
                swell3 *= swell3Mul;
                const float s1 = sin2pi(ph[0]), s2 = sin2pi(ph[1]);
                const float note = pa[0] * s1 + pa[1] * (1.0f - swell) * (0.8f * s2 + 0.35f * sin2pi(mph)) +
                                   pa[2] * (1.0f - swell3) * sin2pi(ph[2]) + pa[3] * sin2pi(ph[3]);
                // The neighbours, rung by the stick and by the note.
                const double drive = double(0.5f * note);
                double nb = 0;
                for (int k = 0; k < kNeighbours; ++k) {
                    const double y = nc[k] * ny1[k] - nr2[k] * ny2[k] + double(nIn[k]) * drive;
                    ny2[k] = ny1[k];
                    ny1[k] = y;
                    nb += y;
                }
                x += note + float(nb);
                sl = x;
                break;
            }
            case Sitar: {
                constexpr int mask = kString - 1;
                // The string's length now: shorter while it rests on the
                // bridge, read between samples by a four-point Hermite
                // curve. A straight line between two samples would darken
                // the loop wherever the bridge leaves the read between
                // them, and a high string, passing it a thousand times a
                // second, would lose its buzz to that within milliseconds.
                const float at = clampf(float(sN) + sBend - jawNow, 2.0f, float(kString - 5));
                const int i0 = int(at);
                const float fr = at - float(i0);
                const float sp = str[(sw - i0 + 1) & mask], s0 = str[(sw - i0) & mask],
                            s1 = str[(sw - i0 - 1) & mask], s2 = str[(sw - i0 - 2) & mask],
                            s3 = str[(sw - i0 - 3) & mask];
                auto hermite = [fr](float a, float b, float c, float d) {
                    const float c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.0f * c - 0.5f * d,
                                c3 = 0.5f * (d - a) + 1.5f * (b - c);
                    return ((c3 * fr + c2) * fr + c1) * fr + b;
                };
                const float x0 = hermite(sp, s0, s1, s2), x1 = hermite(s0, s1, s2, s3);
                const float lp = x0 + sS * (x1 - x0);
                const float ap = sC * (lp - apY) + apX;
                apX = lp;
                apY = ap;
                // The bridge: on it past (1 − jaw) of the swing, off it
                // below; the switch eased over a couple of samples so the
                // kink buzzes rather than clicks.
                swing = std::max(std::fabs(ap), swing * swingMul);
                jawNow += 0.5f * ((ap > (1.0f - jaw) * swing ? jawCut : 0.0f) - jawNow);
                const float dc = ap - dcX + dcR * dcY;
                dcX = ap;
                dcY = dc;
                str[sw] = dc * sRho;
                sw = (sw + 1) & mask;
                sl = x0;
                break;
            }
            case LogDrum: {
                // The fall or glide into the note, and the yelp's rise and
                // return over it; once both are done the note holds still.
                bool bending = false;
                if (std::fabs(drop) > 1e-4f) {
                    drop *= dropMul;
                    bending = true;
                } else {
                    drop = 0.0f;
                }
                if (yelp > 0.0f) {
                    yelpSlow *= yelpSlowMul;
                    yelpFast *= yelpFastMul;
                    if (yelpSlow < 1e-4f)
                        yelp = 0.0f;
                    bending = true;
                }
                if (bending)
                    inc[0] = std::min(0.45f, incNote * std::exp2(drop + yelp * (yelpSlow - yelpFast)));
                else if (inc[0] != incNote)
                    inc[0] = incNote;
                float &p = ph[0];
                p += inc[0];
                if (p >= 1.0f)
                    p -= 1.0f;
                mph += minc;
                if (mph >= 1.0f)
                    mph -= 1.0f;
                fmIdx = fmIdx > 1e-5f ? fmIdx * fmMul : 0.0f;
                tine = tine > 1e-5f ? tine * tineMul : 0.0f;
                const float x = sin2pi(frac(p + 0.159155f * fmIdx * sin2pi(p))) + tine * sin2pi(mph);
                float y = x;
                if (tok > 1e-4f) {
                    tokEnv *= tokMul;
                    tok *= 0.99995f;
                    if ((coefIn & 7) == 0) {
                        tokHz = 900.0f * std::exp2(1.7f * tokEnv);
                        tokF.set(tokHz, 5.0f, rate);
                    }
                    y += tok * tokEnv * tokF.step(x).band;
                }
                // A tanh, its gain undone, so the drive changes the grit,
                // not the level; the lean adds the even harmonics, and its
                // offset is taken back out so the log stays centred.
                const float u = drive * (y + asym * y * y) - drive * asym * 0.5f;
                sl = fastTanh(u) * driveNorm * 0.9f;
                break;
            }
            case Chip: {
                if (chipVol > 0 && --chipCount <= 0) {
                    --chipVol;
                    chipCount = chipEvery;
                    if (chipVol == 0)
                        tail = 0.0f; // silent: the note is over once the gate is
                }
                chipLevel = float(chipVol) * (1.0f / 15.0f);
                chipSmooth += 0.15f * (chipLevel - chipSmooth);
                // The driver's frame: the chord's next note, the vibrato's
                // next step.
                bool retune = false;
                if ((frameIn -= 1.0f) <= 0.0f) {
                    frameIn += frameLen;
                    if (arpN > 1) {
                        arpAt = (arpAt + 1) % arpN;
                        regTo = arpReg[arpAt];
                        if (sweepShift == 0)
                            reg = regTo;
                    }
                    if (vibWait > 0.0f) {
                        vibWait -= frameLen;
                    } else {
                        vibDepth = std::min(0.007f, vibDepth + vibGrow);
                        lfo += lfoInc;
                        if (lfo >= 1.0f)
                            lfo -= 1.0f;
                    }
                    retune = true;
                }
                // The sweep's clock: x less x >> n, until it lands.
                if (sweepShift > 0 && (sweepIn -= 1.0f) <= 0.0f) {
                    sweepIn += sweepLen;
                    reg -= std::floor(reg / float(1 << sweepShift));
                    if (reg <= regTo) {
                        reg = regTo;
                        sweepShift = 0;
                    }
                    retune = true;
                }
                if (retune) {
                    // The vibrato in register steps: df / f = dx / (2048 − x).
                    const float x = std::round(reg + (2048.0f - reg) * vibDepth * sin2pi(lfo));
                    incNote = std::min(0.45f, chipHz(std::min(x, 2046.0f)) / rate);
                    inc[0] = incNote;
                }
                // Two samples of the one-sided pulse at twice the rate, then
                // back down through the half-band.
                const float d = 0.5f * inc[0];
                float &p = ph[0];
                for (int j = 0; j < 2; ++j) {
                    p += d;
                    if (p >= 1.0f)
                        p -= 1.0f;
                    // The pulse about its centre, and the centre itself,
                    // as much of it as the grain lets through.
                    const float wave = 0.5f * (sawBlep(p, d) - sawBlep(frac(p + duty), d));
                    centre += centreA * (chipSmooth * duty - centre);
                    hb[hbAt] = chipSmooth * wave + grain * centre;
                    hbAt = (hbAt + 1) & (kHalfBand - 1);
                }
                float y = 0;
                const int newest = hbAt - 1;
                y += kHalf.h[HalfBand::kMid] * hb[(newest - HalfBand::kMid) & (kHalfBand - 1)];
                for (int k = 1; k <= HalfBand::kMid; k += 2)
                    y += kHalf.h[HalfBand::kMid + k] *
                         (hb[(newest - HalfBand::kMid - k) & (kHalfBand - 1)] +
                          hb[(newest - HalfBand::kMid + k) & (kHalfBand - 1)]);
                const float out = y - capX + capR * capY;
                capX = y;
                capY = out;
                sl = out;
                break;
            }
            case Gamelan: {
                double a = 0, b = 0;
                for (int k = 0; k < kGong; ++k) {
                    const double y = gc[k] * gy1[k] - gr2[k] * gy2[k];
                    gy2[k] = gy1[k];
                    gy1[k] = y;
                    (k < 3 ? a : b) += y;
                }
                const float ping = knock();
                // Each key where its player sits.
                sl = 1.1f * (float(a) * gl[0] + float(b) * gl[1]) + ping * gl[0];
                sr = 1.1f * (float(a) * gr[0] + float(b) * gr[1]) + ping * gr[0];
                break;
            }
            default:
                break;
            }
            float yl, yr;
            if (units > 1) {
                yl = fl.step(sl * 0.7f);
                yr = fr.step(sr * 0.7f);
            } else if (mono) {
                const float y = fl.step(sl * 0.7f);
                yl = y * panL;
                yr = y * panR;
            } else {
                yl = yr = fl.step(sl * 0.7f) * 0.707f;
            }
            const float g = amp * vel * gain * resComp;
            yl *= g;
            yr *= g;
            l[i] += yl;
            r[i] += yr;
            el[i] += yl * send;
            er[i] += yr * send;
            peak = std::max(peak, std::fabs(yl) + std::fabs(yr));
        }
        level = std::max(peak, level * 0.9f);
        if (stage == 2 && amp * tail < 2e-4f)
            on = false;
    }
};

// A tempo-synced ping-pong echo of the arp's own: the first repeat on the
// left, the next on the right, each darker and thinner than the last, the
// heads sliding like tape when the tempo or the time changes.
struct Echo {
    Delay dl, dr;
    Smooth time;
    OnePoleLp lpL, lpR;
    Svf hpL, hpR;
    float rate = 48000;
    float level = 0;
    void init(float r) {
        rate = r;
        dl.init(size_t(2.8f * r));
        dr.init(size_t(2.8f * r));
        time.setTime(0.12f, r);
        time.reset(0.375f * r);
        lpL.set(4800.0f, r);
        lpR.set(4800.0f, r);
        hpL.set(260.0f, 0.6f, r);
        hpR.set(260.0f, 0.6f, r);
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, float delaySamples, float fb,
                 float wet) {
        delaySamples = clampf(delaySamples, 32.0f, 2.7f * rate);
        float pk = 0;
        for (int i = 0; i < n; ++i) {
            const float d = time.step(delaySamples);
            const float a = dl.read(d), b = dr.read(d);
            const float toL = lpL.step(hpL.step(b).high) * fb;
            const float toR = lpR.step(hpR.step(a).high) * fb;
            dl.push(0.5f * (inL[i] + inR[i]) + fastTanh(toL));
            dr.push(fastTanh(toR));
            outL[i] += a * wet;
            outR[i] += b * wet;
            pk = std::max(pk, std::fabs(a) + std::fabs(b));
        }
        level = std::max(pk * wet, level * 0.95f);
    }
};

// The bus's chorus, after the Juno's: one bucket-brigade line read by two
// taps a few milliseconds long, swept by one triangle in opposite
// directions, one to each side, and darkened like a BBD. Chorus I sweeps at
// 0.5 Hz, II at 0.83 Hz and deeper. Only what is above 300 Hz (two poles) goes into
// the line, so the sub stays dry and in the middle rather than swinging
// from side to side and thinning in mono. One for the bus costs what one
// voice's filter does.
struct Chorus {
    static constexpr int kLen = 2048;
    float buf[kLen] = {};
    int w = 0;
    float rate = 48000, tri = 0, triInc = 0, base = 0, depth = 0, depthTo = 0, glide = 0;
    OnePoleLp lpL, lpR;
    Svf low;
    void init(float r) {
        rate = r;
        std::fill(buf, buf + kLen, 0.0f);
        base = 0.0036f * rate;
        glide = 1.0f - std::exp(-1.0f / (0.05f * rate));
        set(false);
        depth = depthTo;
        lpL.set(7500.0f, rate);
        lpR.set(7500.0f, rate);
        low.set(300.0f, 0.6f, rate);
    }
    // The mode: I (true) or II. The depth glides, so a switch mid-note
    // bends the pitch a moment rather than clicking.
    void set(bool one) {
        triInc = (one ? 0.5f : 0.83f) / rate;
        depthTo = (one ? 0.0013f : 0.0019f) * rate;
    }
    float read(float d) const {
        const int i = int(d);
        const float t = d - float(i);
        const float a = buf[(w - i) & (kLen - 1)], b = buf[(w - i - 1) & (kLen - 1)];
        return a + t * (b - a);
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        for (int i = 0; i < n; ++i) {
            const float x = 0.5f * (inL[i] + inR[i]);
            buf[w] = low.step(x).high;
            depth += glide * (depthTo - depth);
            tri += triInc;
            if (tri >= 1.0f)
                tri -= 1.0f;
            const float sweep = 4.0f * std::fabs(tri - 0.5f) - 1.0f;
            const float a = lpL.step(read(base + depth * sweep)), b = lpR.step(read(base - depth * sweep));
            w = (w + 1) & (kLen - 1);
            outL[i] += 0.82f * (inL[i] + 0.8f * a);
            outR[i] += 0.82f * (inR[i] + 0.8f * b);
        }
    }
};

// The organ's Leslie: the bus split at 800 Hz, the top into a spinning horn
// and the bottom into a spinning drum. Each rotor is a short delay swung
// ±0.25 ms (the Doppler, the pitch going round) and a level swung by a
// third a quarter turn later (the horn facing you, then away). The right
// side hears the rotor's level a hundred degrees further round than the
// left, as two microphones on a real cabinet do: wide, but not opposite. The
// horn's pitch swing is one for both sides (two delays swung apart would
// comb the top away in mono), and a quarter of it reaches both unswung, the
// room and the horn's own spill, so summed to mono the throb stays. Slow, the horn turns at 0.8 Hz and the
// drum at 0.7; fast, 6.7 and 5.9. The light horn gets there in about a
// second, the heavy drum in about four, so a switch is heard as the two
// rotors pulling apart and settling.
struct Leslie {
    static constexpr int kLen = 512;
    float horn[kLen] = {}, drum[kLen] = {};
    int w = 0;
    float rate = 48000, hornPh = 0, drumPh = 0.3f, hornHz = 0.8f, drumHz = 0.7f, hornA = 0, drumA = 0;
    float base = 0, wobble = 0;
    OnePoleLp split;
    void init(float r) {
        rate = r;
        std::fill(horn, horn + kLen, 0.0f);
        std::fill(drum, drum + kLen, 0.0f);
        hornA = 1.0f - std::exp(-1.0f / (0.33f * rate));
        drumA = 1.0f - std::exp(-1.0f / (1.3f * rate));
        base = 0.0012f * rate;
        wobble = 0.00025f * rate;
        split.set(800.0f, rate);
    }
    float read(const float *buf, float d) const {
        const int i = int(d);
        const float t = d - float(i);
        const float a = buf[(w - i) & (kLen - 1)], b = buf[(w - i - 1) & (kLen - 1)];
        return a + t * (b - a);
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, bool fast) {
        const float hornTo = fast ? 6.7f : 0.8f, drumTo = fast ? 5.9f : 0.7f;
        for (int i = 0; i < n; ++i) {
            const float x = 0.5f * (inL[i] + inR[i]);
            const float lo = split.step(x);
            horn[w] = x - lo;
            drum[w] = lo;
            hornHz += hornA * (hornTo - hornHz);
            drumHz += drumA * (drumTo - drumHz);
            hornPh += hornHz / rate;
            if (hornPh >= 1.0f)
                hornPh -= 1.0f;
            drumPh += drumHz / rate;
            if (drumPh >= 1.0f)
                drumPh -= 1.0f;
            constexpr float kApart = 100.0f / 360.0f;
            constexpr float kSpill = 0.25f;
            const float still = kSpill * read(horn, base);
            const float swung = (1.0f - kSpill) * read(horn, base + wobble * sin2pi(hornPh));
            const float hl = still + (1.0f + 0.35f * sin2pi(frac(hornPh + 0.25f))) * swung;
            const float hr = still + (1.0f + 0.35f * sin2pi(frac(hornPh + kApart + 0.25f))) * swung;
            const float dl = (1.0f + 0.3f * sin2pi(frac(drumPh + 0.25f))) * read(drum, base + wobble * sin2pi(drumPh));
            const float dr = (1.0f + 0.3f * sin2pi(frac(drumPh + kApart + 0.25f))) *
                             read(drum, base + wobble * sin2pi(frac(drumPh + kApart)));
            w = (w + 1) & (kLen - 1);
            outL[i] += hl + dl;
            outR[i] += hr + dr;
        }
    }
};

// The sitar's taraf: the sympathetic strings under its played ones, tuned
// to the key and not to the note, so they answer only when the arp lands on
// (or near the overtones of) what they are tuned to, and ring on between
// the notes after it. Six strings: the tonic and the fifth in two octaves,
// then the tonic twice more up high, the last two being the chikari, the
// drone strings the player's little finger strikes between phrases. Each
// is a string as the harp's is (a loop one period long, darkened a little
// each pass), fed by the sitar's bus and spread across the stereo field.
struct Taraf {
    static constexpr int kStrings = 6, kLen = 1024;
    float buf[kStrings][kLen] = {};
    int w = 0;
    float len[kStrings] = {}, fb[kStrings] = {}, last[kStrings] = {}, gl[kStrings] = {}, gr[kStrings] = {};
    float rate = 48000, tonic = -1, fifth = -1;
    // The chikari's stroke: where in the block, how hard, and the burst
    // of the wire plectrum as it runs.
    int pluckAt = -1;
    float pluckVel = 0, burst = 0, burstMul = 0, burstLp = 0;
    Rng noise{0x51A7u};
    float level = 0;
    void init(float r) {
        rate = r;
        for (auto &b : buf)
            std::fill(b, b + kLen, 0.0f);
        std::fill(last, last + kStrings, 0.0f);
        static const float pans[kStrings] = {-0.6f, 0.5f, -0.3f, 0.7f, -0.75f, 0.4f};
        for (int k = 0; k < kStrings; ++k) {
            gl[k] = std::sqrt(0.5f * (1.0f - pans[k]));
            gr[k] = std::sqrt(0.5f * (1.0f + pans[k]));
        }
        burstMul = std::exp(-1.0f / (0.004f * r));
        tonic = fifth = -1;
    }
    // Tuned to the studio's key: the tonic (in MIDI) and the fifth above it
    // (in semitones, the scale's own, so a just key gets a pure fifth).
    void tune(float tonicMidi, float fifthSemis) {
        if (tonicMidi == tonic && fifthSemis == fifth)
            return;
        tonic = tonicMidi;
        fifth = fifthSemis;
        const float notes[kStrings] = {tonic + 12, tonic + 12 + fifth, tonic + 24, tonic + 24 + fifth, tonic + 36,
                                       tonic + 48};
        for (int k = 0; k < kStrings; ++k) {
            const float period = rate / midiToHz(notes[k]);
            // The loop's averaging of two neighbours delays it half a
            // sample's share, taken off the read.
            len[k] = clampf(period - 0.3f, 3.0f, float(kLen - 4));
            // Two seconds to fall 60 dB, the high ones a little sooner.
            const float t60 = k < 4 ? 2.0f : 1.4f;
            fb[k] = std::pow(10.0f, -3.0f * period / (t60 * rate));
        }
    }
    void pluck(int at, float vel) {
        pluckAt = at;
        pluckVel = vel;
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        // Nothing played and nothing ringing: the strings are still.
        bool quiet = level < 1e-6f && pluckAt < 0 && burst == 0.0f;
        for (int i = 0; quiet && i < n; ++i)
            quiet = inL[i] == 0.0f && inR[i] == 0.0f;
        if (quiet)
            return;
        float pk = 0;
        for (int i = 0; i < n; ++i) {
            const float x = 0.5f * (inL[i] + inR[i]);
            if (i == pluckAt) {
                burst = pluckVel;
                pluckAt = -1;
            }
            float chik = 0;
            if (burst > 1e-5f) {
                burst *= burstMul;
                burstLp += 0.5f * (noise.bipolar() * burst - burstLp);
                chik = burstLp;
            } else if (burstLp != 0.0f) {
                burst = burstLp = 0.0f;
            }
            float sl = 0, sr = 0;
            for (int k = 0; k < kStrings; ++k) {
                // Read between samples by the same four-point Hermite curve
                // as the played string: a straight line would darken each
                // pass by where the read falls, and the top strings, passing
                // it over a thousand times a second, would lose their
                // shimmer to that within a fraction of a second.
                const float at = float(w) - len[k];
                const float fl = std::floor(at);
                const float t = at - fl;
                const int j = int(fl);
                const float p0 = buf[k][(j - 1) & (kLen - 1)], p1 = buf[k][j & (kLen - 1)],
                            p2 = buf[k][(j + 1) & (kLen - 1)], p3 = buf[k][(j + 2) & (kLen - 1)];
                const float c1 = 0.5f * (p2 - p0), c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3,
                            c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
                const float d = ((c3 * t + c2) * t + c1) * t + p1;
                // The loop's damping: this pass and the last, averaged.
                const float y = fb[k] * (0.7f * d + 0.3f * last[k]);
                last[k] = d;
                // Fed by the sitar in proportion to what each string loses a
                // pass, so a long string and a short one answer alike.
                const float in = 0.6f * (1.0f - fb[k]) * x + (k >= 4 ? 0.07f * chik : 0.0f);
                buf[k][w] = y + in;
                sl += y * gl[k];
                sr += y * gr[k];
            }
            w = (w + 1) & (kLen - 1);
            outL[i] += inL[i] + sl;
            outR[i] += inR[i] + sr;
            pk = std::max(pk, std::fabs(sl) + std::fabs(sr));
        }
        if (pluckAt >= n)
            pluckAt -= n;
        level = std::max(pk, level * 0.9f);
    }
};

struct PoolNote {
    int id = 0;
    int col = 0, row = 0, stack = 0;
    float vel = 0.8f;
    bool released = false, played = false;
};

constexpr int kPool = 24;
// The grid's columns as an engine takes them: the page sends 0 to 7, a song
// file could say anything, and a note far off the grid would be pitched
// past any sound (and folded down an octave at a time, for ever).
inline int gridColumn(int col) { return std::clamp(col, 0, 15); }

// Selisir, the five-tone pelog of a Balinese gong kebyar: ding, dong,
// deng, dung, dang, some 0, 120, 270, 670 and 790 cents above the ding
// (each orchestra's own, but always this shape: two small steps, a wide
// one, a small one, a wide one; much as E F G B C). It is not the studio's
// scale bent: a gangsa has five keys to the octave, and every note it
// sounds is one of them. A seven-note scale's degrees fall on them as its
// 1, 2, 3, 5 and 6 (which in a minor key are already close to selisir's
// shape), the 4th on the dung and the 7th on the ding above; any other
// scale is spread over the five evenly. Keys count up from the key's
// tonic in its lowest octave, five to the octave.
constexpr float kSelisirCents[5] = {0, 120, 270, 670, 790};
inline int selisirKey(int degree, int perOct) {
    const int n = std::max(1, perOct);
    const int64_t octave = floorDiv(degree, n);
    const int idx = int(floorModI(degree, n));
    static const int seven[7] = {0, 1, 2, 3, 3, 4, 5};
    const int key = n == 7 ? seven[idx] : int(std::lround(double(idx) * 5.0 / double(n)));
    return int(octave) * 5 + key;
}
// The studio's degree a key is named by on the page: of those that fall on
// it, the highest (the dung is the 5th, which the 4th joins).
inline int selisirDegree(int key, int perOct) {
    const int n = std::max(1, perOct);
    const int64_t octave = floorDiv(key, 5);
    const int k = int(floorModI(key, 5));
    for (int idx = n - 1; idx >= 0; --idx)
        if (selisirKey(idx, n) == k)
            return int(octave) * n + idx;
    return int(octave) * n + int(std::lround(double(k) * double(n) / 5.0));
}
inline double selisirMidi(int key, double tonicMidi) {
    return tonicMidi + 12.0 * double(floorDiv(key, 5)) + 0.01 * double(kSelisirCents[floorModI(key, 5)]);
}

} // namespace

// --- the engine -----------------------------------------------------------------

struct Engine::Impl {
    struct Lane {
        Settings s;
        PoolNote notes[kPool];
        int count = 0;
        bool running = false;
        int64_t step = 0;
        int sub = 0;
        double next = 0;
        int64_t lastFired = INT64_MIN;
        uint64_t counter = 0;
        StepPlan plan;
        int cur[16] = {};
        float curVel[16] = {};
        int curCount = 0;
        Smooth cut, res;
        float flash[kFlashCells] = {};
        int lastInBar = -1, lastRatchet = 1;
        bool lastAccent = false;
        // The gamelan's pokok: the pattern's note at the start of each
        // group of four steps, which the kotekan figures around.
        int pokok = 0;
        bool pokokSet = false;
        int diceBars = 0; // a loop's bars against its take's, for chaos's two-bar phrase
        double freshBeat = -1e9; // when the pool last filled from empty
        bool aligned = true;     // a loop on the take's own bars (not re-timed when it was made)
        uint64_t stepAge = 0;    // the voice count before the last step started
    };
    struct Pending {
        uint64_t at;
        Command c;
    };

    float rate = 48000;
    uint64_t rendered = 0;
    int cursor = 0; // the sample within the block being worked on
    // The live lane, then one per layer of the playing loop: each layer
    // keeps its own notes, settings and place in its pattern, so a pattern
    // layered on the loop plays beside the loop's instead of replacing it.
    static constexpr int kLanes = 1 + kClipLayers;
    Lane lanes[kLanes];
    Settings overlay;
    int overlayMask = 0;
    Settings eff[kLanes];
    Voice voices[kVoices];
    uint64_t age = 0;
    Echo echo;
    Chorus chorus;
    Leslie leslie;
    Taraf taraf;
    bool chorusOne = false, leslieFast = false; // the switches, from the lane that last played each sound
    // The bus's high-pass at 95 Hz, which keeps the low notes' mud out of
    // the mix; and the log drum's own at 28 Hz, since its fundamental is
    // what the log is and sits under 95 Hz at the bottom of the range.
    Svf hpL, hpR, subHpL, subHpR;
    Pending pending[256];
    int pendingCount = 0;
    int64_t barIndex = -1;
    uint64_t barCount = 0;
    float level = 0;
    std::vector<Fired> *log = nullptr; // a ring, sized at init
    // The block's context while it renders, and the length of the step
    // being played: each note started goes to an export's MIDI as well.
    AudioContext *noteCtx = nullptr;
    int noteLength = 0;
    uint64_t logCount = 0;
    double expectBeat = -1;
    uint64_t jumpedAt = 0;
    int states = 0;
    Fired recent[kRecent];
    int recentHead = 0;
    uint32_t noteSeed = 0x2545F491u;

    static bool clipId(int id) { return (id & (1 << 30)) != 0; }
    // The loop's lane the page shows: the first layer playing (else the first).
    int clipShown() const {
        for (int k = 1; k < kLanes; ++k)
            if (lanes[k].running)
                return k;
        return 1;
    }

    void refreshEff(int lane) {
        eff[lane] = lanes[lane].s;
        if (lane >= 1 && overlayMask) {
            Settings &e = eff[lane];
            const Settings &o = overlay;
            if (overlayMask & kPadRate) {
                e.rate = o.rate;
                e.gate = o.gate;
                e.swing = o.swing;
            }
            if (overlayMask & kPadRhythm) {
                e.density = o.density;
                e.rotate = o.rotate;
                e.ratchet = o.ratchet;
            }
            if (overlayMask & kPadRange) {
                e.octaves = o.octaves;
                e.reg = o.reg;
            }
            if (overlayMask & kPadTone) {
                e.alt = o.alt;
                e.cutoff = o.cutoff;
                e.res = o.res;
                e.color = o.color;
                e.envBoost = o.envBoost;
            }
            if (overlayMask & kPadGroove) {
                e.accent = o.accent;
                e.chaos = o.chaos;
                e.accentDepth = o.accentDepth;
            }
            if (overlayMask & kPadEcho) {
                e.echoTime = o.echoTime;
                e.echo = o.echo;
                e.throwFb = o.throwFb;
            }
        }
    }

    // Finds where the next step falls from beat b. A lane that starts just
    // after a step line (a finger a little late) plays that step at once
    // rather than a whole step late: the arp catches the player.
    void schedule(Lane &L, const Settings &s, double b, bool catchLate) {
        const double sb = stepBeats(s.rate);
        const int64_t k = stepAt(b, s.rate, s.swing);
        const double into = b - stepStart(k, s.rate, s.swing);
        L.sub = 0;
        if (k > L.lastFired && (into < 1e-7 || (catchLate && into < 0.35 * sb))) {
            L.step = k;
            L.next = b;
        } else {
            L.step = k + 1;
            L.next = stepStart(L.step, s.rate, s.swing);
        }
    }

    void setSettings(int lane, const Settings &ns, double b) {
        const Settings before = eff[lane];
        lanes[lane].s = ns;
        refreshEff(lane);
        retime(lane, before, b);
    }
    void retime(int lane, const Settings &before, double b) {
        Lane &L = lanes[lane];
        const Settings &now = eff[lane];
        if (!L.running)
            return;
        if (before.rate != now.rate) {
            // A new rate starts on its own grid; a fill does not lose the bar.
            L.lastFired = std::min<int64_t>(L.lastFired, INT64_MIN / 2);
            schedule(L, now, b, false);
        } else if (std::fabs(before.swing - now.swing) > 1e-4f && L.sub == 0) {
            L.next = std::max(b, stepStart(L.step, now.rate, now.swing));
        }
    }

    int find(const Lane &L, int id) const {
        for (int i = 0; i < L.count; ++i)
            if (L.notes[i].id == id)
                return i;
        return -1;
    }
    void erase(Lane &L, int i) {
        for (int j = i; j + 1 < L.count; ++j)
            L.notes[j] = L.notes[j + 1];
        --L.count;
        if (L.count == 0)
            L.running = false;
    }

    void noteOn(int lane, const Command &c, double b) {
        Lane &L = lanes[lane];
        const int col = gridColumn(c.i[0]), row = c.i[1] >> 4 & 15, stack = c.i[1] & 15, flags = c.i[1] >> 8;
        if (lane >= 1 && c.f[1] >= 1.0f) {
            // 1 or 2; anything larger (a file's) is kept within an int.
            const int parity = int(std::min(c.f[1], 2.0f)) - 1;
            const int bar = int(floorDiv(int64_t(std::floor((b + 0.1) / kBeatsPerBar)), 1));
            L.diceBars = (parity - bar) & 1;
        }
        const float vel = clampf(c.f[0] > 0 ? c.f[0] : 0.8f, 0.05f, 1.0f);
        auto fill = [&](PoolNote &n) {
            n.col = col;
            n.row = row;
            n.stack = stack;
            n.vel = vel;
        };
        if (flags & kRename) {
            const int old = lane ? ((c.id & ~0xFFFFFF) | (c.i[2] & 0xFFFFFF)) : c.i[2];
            const int at = find(L, old);
            if (at >= 0) {
                PoolNote &n = L.notes[at];
                n.id = c.id;
                fill(n);
                n.released = false;
                // A loop starting: the note it carries may have come in a
                // moment ago (held into the take); the pattern still starts
                // where this bar's hand-over says it was.
                if (c.i[3] >= 0 && L.lastFired < L.step && std::fabs(b - L.freshBeat) < 1e-6)
                    L.counter = uint64_t(c.i[3]) + stepsBefore(L, eff[lane], b, double(c.f[2]), true);
                return;
            }
        }
        const int at = find(L, c.id);
        if (at >= 0) {
            fill(L.notes[at]);
            L.notes[at].released = false;
            return;
        }
        // A note that landed on an empty hand starts the pattern over, even
        // if a loop's quantising brought it a hair before the Offs of the
        // notes it followed.
        if (lane >= 1 && (flags & kFresh)) {
            L.count = 0;
            L.running = false;
        }
        bool held = false;
        for (int i = 0; i < L.count; ++i)
            held |= !L.notes[i].released;
        const int phase = c.i[3];
        // A loop taking over from the hand comes in late (its note replayed
        // after the place it first sounded): it doesn't catch the step the
        // hand has just played, and counts it as played.
        const Settings &e = eff[lane];
        const double inBar = b - std::floor(b / kBeatsPerBar) * kBeatsPerBar;
        const bool late = lane >= 1 && inBar - double(c.f[2]) > 0.02 && inBar - double(c.f[2]) < kBeatsPerBar;
        const bool handPlayed = late && lanes[0].lastFired == stepAt(b, e.rate, e.swing);
        if (!held && phase >= 0) {
            // A loop coming round: start the pattern where the live one was
            // when this bar began, so the loop is what was heard.
            L.count = 0;
            L.running = false;
        }
        const bool fresh = L.count == 0;
        if (L.count >= kPool)
            erase(L, 0);
        PoolNote &n = L.notes[L.count++];
        n = PoolNote{};
        n.id = c.id;
        fill(n);
        if (fresh) {
            L.counter =
                (phase >= 0 ? uint64_t(phase) : 0) + (lane >= 1 ? stepsBefore(L, e, b, double(c.f[2]), !handPlayed) : 0);
            L.freshBeat = b;
            L.pokokSet = false; // a new hand: the gamelan's figure starts from its notes
        }
        if (!L.running) {
            L.running = true;
            // A new chord landed a hair after a step the old one played (the
            // fingers changed a moment late): the new chord takes the step.
            const int64_t k = stepAt(b, e.rate, e.swing);
            if ((flags & kFresh) && !handPlayed && L.lastFired == k &&
                b - stepStart(k, e.rate, e.swing) < 0.35 * stepBeats(e.rate)) {
                for (Voice &v : voices)
                    if (v.on && v.lane == lane && v.born > L.stepAge)
                        v.fade(rate);
                L.lastFired = k - 1;
            }
            schedule(L, e, b, !handPlayed);
        }
    }

    // A loop that starts part way into a bar (Space just after the line)
    // hears a bar's hand-over late: the pattern has moved on by the notes
    // that bar would already have played. Counts them, from the bar line to
    // b, leaving out the step the lane is about to catch.
    // A loop replays a note late when it joins part way in: a note held
    // into the take comes at the join, a bar's hand-over after its bar line.
    // The pattern has moved on by the notes played since the note first
    // sounded (from its own place in the bar, `from`). Counts them, leaving
    // out the step the lane is about to catch. Not for a loop whose clock
    // was reset when it was made: its bars are not the take's.
    uint64_t stepsBefore(const Lane &L, const Settings &s, double b, double from, bool catching) const {
        if (!L.aligned || rendered - jumpedAt < uint64_t(rate * 0.25))
            return 0;
        const double barStart = std::floor((b + 0.01) / kBeatsPerBar) * kBeatsPerBar;
        const double bar = barStart + std::clamp(from, 0.0, double(kBeatsPerBar));
        if (b - bar < 0.01)
            return 0;
        const double sb = stepBeats(s.rate);
        const int64_t now = stepAt(b, s.rate, s.swing);
        const bool caught = catching && b - stepStart(now, s.rate, s.swing) < 0.35 * sb;
        const int64_t last = caught ? now - 1 : now;
        uint64_t n = 0;
        // (A note that landed a hair after its step counts that step.)
        for (int64_t k = int64_t(std::ceil((bar - 0.02) / sb)); k <= last; ++k) {
            const StepPlan plan = planStep(s, k, n, k + int64_t(L.diceBars) * stepsPerBar(s.rate));
            if (plan.fire)
                n += 1 + uint64_t(plan.skip);
        }
        return n;
    }

    void noteOff(int lane, int id) {
        Lane &L = lanes[lane];
        const int at = find(L, id);
        if (at < 0)
            return;
        // A note let go before it was ever heard stays until its turn: every
        // tap sounds, however short.
        if (L.notes[at].played)
            erase(L, at);
        else
            L.notes[at].released = true;
    }

    void noteMove(int lane, const Command &c) {
        Lane &L = lanes[lane];
        const int at = find(L, c.id);
        if (at < 0)
            return;
        PoolNote &n = L.notes[at];
        n.col = gridColumn(c.i[0]);
        n.row = c.i[1] >> 4 & 15;
        n.stack = c.i[1] & 15;
        if (c.f[0] > 0)
            n.vel = clampf(c.f[0], 0.05f, 1.0f);
    }

    void apply(const Command &c, double b) {
        const int lane = clipId(c.id) ? 1 + replayLayer(c.id) : 0;
        switch (c.type) {
        case kState: {
            ++states;
            Settings s;
            s.unpack(c);
            setSettings(lane, s, b);
            break;
        }
        case kOverlay: {
            // The hand's overlay reaches every layer of the loop.
            overlay.unpack(c);
            overlayMask = (c.i[3] >> 10) & 63;
            for (int k = 1; k < kLanes; ++k) {
                const Settings before = eff[k];
                refreshEff(k);
                retime(k, before, b);
            }
            break;
        }
        case kSync:
            // A take just landed: its loop is on the take's own bars unless
            // the clock was reset to make it (then the bars are new ones).
            for (int k = 1; k < kLanes; ++k)
                lanes[k].aligned = rendered - jumpedAt >= uint64_t(rate * 0.25);
            break;
        case kCut:
            for (Voice &v : voices)
                if (v.on && v.lane == 0)
                    v.fade(rate);
            break;
        case Command::On:
            noteOn(lane, c, b);
            break;
        case Command::Off:
            noteOff(lane, c.id);
            break;
        case Command::Move:
            noteMove(lane, c);
            break;
        default:
            break;
        }
    }

    // The pool's degrees as the pattern sees them: the notes held (the first
    // grown into its chord), or, following the chords page, the chord's
    // tones between the lowest and highest note held.
    int poolDegrees(const Lane &L, const Settings &s, const Harmony &h, int perOct, int *deg, float *vel) const {
        int n = 0;
        if (L.count == 0)
            return 0;
        if (s.follow) {
            int lo = INT_MAX, hi = INT_MIN;
            float v = 0;
            for (int i = 0; i < L.count; ++i) {
                const int d = L.notes[i].col + L.notes[i].row * perOct;
                lo = std::min(lo, d);
                hi = std::max(hi, d);
                v += L.notes[i].vel;
            }
            v /= float(L.count);
            if (hi - lo < perOct)
                hi = lo + perOct;
            // Tones may come as degrees or as steps above the root: the root
            // first either way.
            int tones[4];
            const int count = std::clamp(h.chordCount, 1, 4);
            const bool relative = h.chordTones[0] != h.chordRoot;
            for (int k = 0; k < count; ++k)
                tones[k] = h.chordTones[k] + (relative ? h.chordRoot : 0);
            for (int d = lo; d <= hi && n < 16; ++d)
                for (int k = 0; k < count; ++k)
                    if (floorModI(d - tones[k], perOct) == 0) {
                        deg[n] = d;
                        vel[n] = v;
                        ++n;
                        break;
                    }
            return n;
        }
        for (int i = 0; i < L.count && n < 16; ++i) {
            const PoolNote &p = L.notes[i];
            const int d0 = p.col + p.row * perOct;
            int st[5] = {0};
            const int m = i == 0 ? stackNotes(p.stack, st) : 1;
            for (int k = 0; k < m && n < 16; ++k) {
                deg[n] = d0 + st[k];
                vel[n] = p.vel;
                ++n;
            }
        }
        return n;
    }

    Voice &alloc() {
        int active = 0;
        for (const Voice &v : voices)
            active += v.on && !v.fading;
        if (active >= kSoftVoices) {
            Voice *oldest = nullptr;
            for (Voice &v : voices)
                if (v.on && !v.fading && (!oldest || v.born < oldest->born))
                    oldest = &v;
            if (oldest)
                oldest->fade(rate);
        }
        for (Voice &v : voices)
            if (!v.on)
                return v;
        Voice *quiet = &voices[0];
        for (Voice &v : voices)
            if (v.level < quiet->level)
                quiet = &v;
        return *quiet;
    }

    void fire(int lane, double b, const Transport &tr, const Harmony &h) {
        Lane &L = lanes[lane];
        const Settings &s = eff[lane];
        const int perOct = h.tuning.degreesPerOctave();
        if (L.sub == 0) {
            L.plan = planStep(s, L.step, L.counter, L.step + int64_t(L.diceBars) * stepsPerBar(s.rate));
            L.curCount = 0;
            if (L.plan.fire && L.count > 0) {
                int deg[16];
                float vel[16];
                const int n = poolDegrees(L, s, h, perOct, deg, vel);
                if (n > 0) {
                    PoolView pv;
                    // The climb stops at the octave that would pass the top
                    // of the range (E7), rather than folding back down and
                    // breaking the pattern's shape.
                    int top = deg[0];
                    for (int i = 1; i < n; ++i)
                        top = std::max(top, deg[i]);
                    int octaves = s.octaves;
                    while (octaves > 1 && h.midi(top + (octaves - 1) * perOct, s.reg) > kTopMidi)
                        --octaves;
                    buildPool(deg, vel, n, octaves, perOct, pv);
                    L.curCount = patternNotes(s.pattern, pv, L.counter + uint64_t(L.plan.skip), s.seed, octaves,
                                              perOct, L.cur, L.curVel, 16);
                    L.counter += 1 + uint64_t(L.plan.skip);
                    if (L.plan.leap) {
                        bool room = true;
                        for (int i = 0; i < L.curCount; ++i)
                            room &= h.midi(L.cur[i] + perOct, s.reg) <= kTopMidi;
                        if (room)
                            for (int i = 0; i < L.curCount; ++i)
                                L.cur[i] += perOct;
                        else
                            L.plan.leap = 0;
                    }
                    // Which held notes have now been heard.
                    for (int i = 0; i < L.count; ++i) {
                        PoolNote &p = L.notes[i];
                        if (p.played)
                            continue;
                        const int d0 = p.col + p.row * perOct;
                        for (int k = 0; k < L.curCount && !p.played; ++k)
                            p.played = s.follow || floorModI(L.cur[k] - d0, perOct) == 0 || (i == 0 && p.stack > 0);
                    }
                    L.lastFired = L.step;
                    L.stepAge = age;
                    L.lastInBar = int(floorModI(L.step, stepsPerBar(s.rate)));
                    L.lastRatchet = L.plan.ratchet;
                    L.lastAccent = L.plan.accent;
                }
            }
        }
        if (L.curCount > 0)
            trigger(lane, b, tr, h);
        const int r = L.curCount > 0 ? std::max(1, L.plan.ratchet) : 1;
        if (++L.sub >= r) {
            L.sub = 0;
            ++L.step;
        }
        const double sb = stepBeats(s.rate);
        L.next = stepStart(L.step, s.rate, s.swing) + double(L.sub) * sb / double(r);
        // Notes let go before they were heard leave once they have been.
        for (int i = L.count - 1; i >= 0; --i)
            if (L.notes[i].released && L.notes[i].played)
                erase(L, i);
    }

    void logNote(int lane, double b, float midi, float vel, int degree, bool accent, Lane &L, int perOct) {
        const int cell = degree - (L.plan.leap ? perOct : 0);
        if (cell >= 0 && cell < kFlashCells)
            L.flash[cell] = 1.0f;
        Fired f;
        f.sample = rendered + uint64_t(cursor);
        f.beat = b;
        f.midi = midi;
        f.vel = vel;
        f.degree = degree;
        f.lane = lane;
        f.accent = accent;
        recent[recentHead] = f;
        recentHead = (recentHead + 1) % kRecent;
        if (noteCtx)
            noteCtx->note(-1, midi, vel, cursor, std::max(1, noteLength));
        if (log) {
            (*log)[logCount % log->size()] = f;
            ++logCount;
        }
    }

    void trigger(int lane, double b, const Transport &tr, const Harmony &h) {
        Lane &L = lanes[lane];
        const Settings &s = eff[lane];
        const int r = std::max(1, L.plan.ratchet);
        const float stepSec = float(stepBeats(s.rate) * tr.secondsPerBeat());
        const float subSec = stepSec / float(r);
        // A gate pushed all the way ties each note into the next: legato,
        // and on the acid a slide.
        const bool tie = s.gate > 0.97f && r == 1;
        const float gateSec =
            tie ? subSec * 1.08f : std::max(0.012f, (r > 1 ? std::min(s.gate, 0.7f) : s.gate) * subSec);
        noteLength = int(std::min(gateSec, subSec) * rate);
        const float rollGain = L.sub > 0 ? std::pow(0.8f, float(L.sub)) : 1.0f;
        const int cyc = accentCycle(s.accent);
        const float accentGain =
            L.plan.accent ? 1.0f + 0.4f * s.accentDepth : (cyc > 0 ? 1.0f - 0.22f * s.accentDepth : 1.0f);
        // A stab's notes start together: their peaks add, so each is quieter
        // than its share of the power alone would say.
        const float stack = L.curCount > 1 ? 1.1f * std::pow(float(L.curCount), -0.7f) : 1.0f;
        const int perOct = h.tuning.degreesPerOctave();
        // A Game Boy channel plays one note at a time: a new step cuts what
        // the lane's chip still holds. A log drum is one log: the next
        // glides on from where it is, yelp and all.
        float logFrom = 0;
        if (s.sound == Chip || s.sound == LogDrum)
            for (Voice &w : voices)
                if (w.on && !w.fading && w.lane == lane && w.sound == s.sound) {
                    if (s.sound == LogDrum && L.curCount == 1)
                        logFrom = w.midiTo + 12.0f * (w.drop + w.yelp * (w.yelpSlow - w.yelpFast));
                    w.fade(rate);
                }
        auto pitch = [&](int k) {
            double midi = h.midi(L.cur[k], s.reg);
            // Only notes held above the range itself fold down.
            while (midi > kTopMidi + 12.0)
                midi -= 12.0;
            return midi;
        };
        // A sitar's legato is one string: each note stops what rang of the
        // last, and is plucked a little harder for it, since one string
        // sounding alone must carry what several ringing on together would
        // (about 3 dB, by ear and meter). Of those notes, the ones chosen
        // (an accent, or with none set, the beat) that lie a 2nd to a 4th
        // from the last are a meend: plucked at the last note's pitch and
        // pulled along the fret into their own. The rest are fresh plucks.
        // A meend is an ornament a player chooses; on every note it turns
        // a run into a slide whistle.
        float meendFrom = 0, lift = 1;
        if (s.sound == Sitar && tie && L.curCount == 1) {
            lift = 1.4f;
            Voice *last = nullptr;
            for (Voice &w : voices)
                if (w.on && !w.fading && w.lane == lane && w.sound == Sitar && (!last || w.born > last->born))
                    last = &w;
            if (last) {
                const int perBeat = std::max(1, stepsPerBar(s.rate) / 4);
                const bool chosen = cyc > 0 ? L.plan.accent : floorModI(L.step, perBeat) == 0;
                const float apart = std::fabs(float(pitch(0)) - last->midiNow);
                if (chosen && apart > 0.5f && apart < 5.5f)
                    meendFrom = last->midiNow;
                last->fade(rate);
            }
        }
        auto setUp = [&](Voice::Start &st, float midi, float vel) {
            st.sound = s.sound;
            st.midi = midi;
            st.vel = vel;
            st.accent = L.plan.accent ? s.accentDepth : 0.0f;
            st.envBoost = s.envBoost;
            st.color = s.color;
            st.res = s.res;
            st.lift = lift;
            st.gateSec = gateSec;
            st.stepSec = stepSec;
            st.rate = rate;
            noteSeed = hash(noteSeed, uint32_t(age));
            st.seed = noteSeed;
            st.clock = double(rendered + uint64_t(cursor)) / double(rate);
        };
        // The chikari: on an accent (or, with none set, on the bar's first
        // step) the sitarist's little finger strikes the high tonic drone
        // strings, which are the taraf's top two.
        if (s.sound == Sitar && L.sub == 0 &&
            (L.plan.accent || (cyc == 0 && floorModI(L.step, stepsPerBar(s.rate)) == 0)))
            taraf.pluck(cursor, L.plan.accent ? 0.5f + 0.3f * s.accentDepth : 0.45f);
        // A chip's chord is not three channels but one, its notes taken in
        // turn a frame each, sixty times a second: the chiptune chord.
        if (s.sound == Chip && L.curCount > 1) {
            Voice &v = alloc();
            Voice::Start st;
            float top = 0;
            const int n = std::min(L.curCount, 4);
            st.chord = n;
            for (int k = 0; k < n; ++k) {
                st.chordMidi[k] = float(pitch(k));
                top = std::max(top, L.curVel[k]);
            }
            setUp(st, st.chordMidi[0], top * L.plan.gain * accentGain * rollGain);
            v.start(st);
            v.lane = lane;
            v.born = ++age;
            for (int k = 0; k < L.curCount; ++k)
                logNote(lane, b, float(pitch(k)), st.vel, L.cur[k], L.plan.accent, L, perOct);
            return;
        }
        if (s.sound == Gamelan) {
            gangsa(lane, b, h, perOct, accentGain * rollGain, stack, setUp);
            return;
        }
        for (int k = 0; k < L.curCount; ++k) {
            const double midi = pitch(k);
            // A tied acid note glides the way a 303 slides.
            if (tie && s.sound == Acid && L.curCount == 1) {
                Voice *held = nullptr;
                for (Voice &w : voices)
                    if (w.on && !w.released && w.lane == lane && w.sound == s.sound)
                        held = &w;
                if (held) {
                    held->slide(float(midi), gateSec, rate);
                    logNote(lane, b, float(midi), held->vel, L.cur[k], L.plan.accent, L, perOct);
                    continue;
                }
            }
            // The log drum sounds an octave under the note it is named by,
            // as a bass does: the grid's lowest register then puts its
            // fundamentals at 40 to 80 Hz, where amapiano's logs sit.
            const float sounding = float(midi) - (s.sound == LogDrum ? 12.0f : 0.0f);
            // A harp string or a marimba bar struck again is the same
            // string or bar: what still rings of the last stroke is damped
            // in a few milliseconds as the new one starts, instead of a
            // second voice piling up on the same note (up and down patterns
            // hit each note twice), which would crowd the low strings out of
            // the voice cap long before they had finished ringing.
            // The same holds for a pan's note and a sitar's string.
            if (s.sound == Harp || s.sound == Marimba || s.sound == Steelpan || s.sound == Sitar)
                for (Voice &w : voices)
                    if (w.on && !w.fading && w.lane == lane && w.sound == s.sound &&
                        std::fabs(w.midiNow - sounding) < 0.01f)
                        w.fade(rate);
            Voice &v = alloc();
            Voice::Start st;
            setUp(st, sounding, L.curVel[k] * L.plan.gain * accentGain * rollGain * stack);
            st.fromMidi = s.sound == Sitar ? meendFrom : logFrom;
            v.start(st);
            v.lane = lane;
            v.born = ++age;
            logNote(lane, b, float(midi), st.vel, L.cur[k], L.plan.accent, L, perOct);
        }
    }

    // Kotekan, the gangsa's interlocking: one figure split between two
    // players, the polos (left) and the sangsih (right), which neither
    // plays whole. This is norot: the pattern's note at the start of each
    // group of four steps is the pokok, the melody's tone, which the group
    // holds while the arp moves on; around it, the main tone M and the key
    // above it N alternate, M N M N, the players taking them as
    //     step     1  2  3  4
    //     polos    M  .  M  N
    //     sangsih  M  N  .  N
    // so each part is full of holes the other fills, and the two meet on
    // the same key on the group's first step and its last, where the two
    // pairs of gangsa beat against each other (the ombak between players).
    // In selisir M and N are two of the five keys, counted in those five;
    // even, two of the studio's degrees. A chord (the stab pattern, or
    // several notes at once) is the polos's alone.
    template <class SetUp>
    void gangsa(int lane, double b, const Harmony &h, int perOct, float gain, float stack, SetUp &setUp) {
        Lane &L = lanes[lane];
        const Settings &s = eff[lane];
        const bool selisir = !s.alt;
        const double tonic = h.midi(0, s.reg);
        const float base = L.plan.gain * gain;
        // A key as a degree the page names, and the pitch it sounds at. In
        // selisir the orchestra's octaves are stretched a little, some five
        // cents an octave, as its tuners do; even, it is the studio's own.
        auto strike = [&](int player, int key, float vel, bool damp) {
            int degree = selisir ? selisirDegree(key, perOct) : key;
            while (h.midi(degree, s.reg) > kTopMidi + 12.0) {
                key -= selisir ? 5 : perOct;
                degree -= perOct;
            }
            const double named = h.midi(degree, s.reg);
            const double sounding =
                selisir ? selisirMidi(key, tonic) + 0.05 * (selisirMidi(key, tonic) - 57.0) / 12.0 : named;
            // The player's other hand stops the key they struck last.
            if (damp)
                for (Voice &w : voices)
                    if (w.on && !w.fading && w.lane == lane && w.sound == Gamelan && w.player == player)
                        w.damp(rate);
            Voice &v = alloc();
            Voice::Start st;
            setUp(st, float(sounding), vel);
            st.sangsih = player == 1;
            v.start(st);
            v.lane = lane;
            v.born = ++age;
            logNote(lane, b, float(named), st.vel, degree, L.plan.accent, L, perOct);
        };
        auto keyOf = [&](int degree) { return selisir ? selisirKey(degree, perOct) : degree; };
        if (L.curCount > 1) {
            for (int k = 0; k < L.curCount; ++k)
                strike(0, keyOf(L.cur[k]), L.curVel[k] * base * stack, k == 0);
            return;
        }
        const int group = int(floorModI(L.step, 4));
        if (group == 0 || !L.pokokSet) {
            L.pokok = L.cur[0];
            L.pokokSet = true;
        }
        const int m = keyOf(L.pokok);
        // Two players on one key are louder than one: each a little softer
        // there, so the meetings shimmer rather than thump.
        const float v = L.curVel[0] * base, both = 0.78f * v;
        switch (group) {
        case 0:
            strike(0, m, both, true);
            strike(1, m, both, true);
            break;
        case 1:
            strike(1, m + 1, v, true);
            break;
        case 2:
            strike(0, m, v, true);
            break;
        default:
            strike(0, m + 1, both, true);
            strike(1, m + 1, both, true);
            break;
        }
    }
};

Engine::Engine() : d(new Impl) {}
Engine::~Engine() { delete d; }

void Engine::init(float rate) {
    d->rate = rate;
    d->echo.init(rate);
    d->chorus.init(rate);
    d->leslie.init(rate);
    d->taraf.init(rate);
    d->hpL.set(95.0f, 0.7f, rate);
    d->hpR.set(95.0f, 0.7f, rate);
    d->subHpL.set(28.0f, 0.7f, rate);
    d->subHpR.set(28.0f, 0.7f, rate);
    for (auto &L : d->lanes) {
        L.cut.setTime(0.02f, rate / float(kBlock));
        L.res.setTime(0.02f, rate / float(kBlock));
        L.cut.reset(L.s.cutoff);
        L.res.reset(L.s.res);
    }
    d->refreshEff(0);
    d->refreshEff(1);
    m_log.assign(1 << 13, Fired{});
    d->log = &m_log;
}

void Engine::command(const Command &c, AudioContext &ctx) {
    // Everything happens at its own time: a replayed clip's events within
    // the block, a loop's hand-over a little ahead, live touches at once.
    uint64_t at = d->rendered;
    if (c.t > 0) {
        const double ahead = (c.t - ctx.now()) * double(d->rate);
        if (ahead > 0)
            at += uint64_t(std::llround(ahead));
    }
    if (d->pendingCount >= 256) {
        d->apply(c, ctx.transport().beat);
        return;
    }
    int i = d->pendingCount++;
    while (i > 0 && d->pending[i - 1].at > at) {
        d->pending[i] = d->pending[i - 1];
        --i;
    }
    d->pending[i] = {at, c};
}

void Engine::render(float *l, float *r, int n, AudioContext &ctx) {
    Impl &s = *d;
    s.noteCtx = &ctx;
    const Transport &tr = ctx.transport();
    const Harmony &h = ctx.harmony();
    const double b0 = tr.beat, bps = tr.beatsPerSample;
    if (s.expectBeat >= 0 && std::fabs(b0 - s.expectBeat) > 1e-6)
        s.jumpedAt = s.rendered;
    s.expectBeat = b0 + double(n) * bps;
    // A clock that jumped (the first loop setting the tempo) leaves no
    // backlog of steps behind it.
    for (int k = 0; k < Impl::kLanes; ++k) {
        auto &L = s.lanes[k];
        if (L.running && L.next < b0 - stepBeats(s.eff[k].rate))
            s.schedule(L, s.eff[k], b0, false);
    }
    // Filter settings glide a little, so sweeps are smooth.
    float cutHz[Impl::kLanes], res[Impl::kLanes], send[Impl::kLanes];
    for (int k = 0; k < Impl::kLanes; ++k) {
        auto &L = s.lanes[k];
        const float c = L.cut.step(s.eff[k].cutoff);
        res[k] = L.res.step(s.eff[k].res) * 0.92f;
        cutHz[k] = 55.0f * std::exp2(c * 7.9f);
        send[k] = 0.9f * s.eff[k].echo;
    }
    float dryL[kBlock] = {}, dryR[kBlock] = {}, sendL[kBlock] = {}, sendR[kBlock] = {};
    float chL[kBlock] = {}, chR[kBlock] = {}; // the voices that go through the chorus
    float rotL[kBlock] = {}, rotR[kBlock] = {}; // and through the Leslie
    float tarL[kBlock] = {}, tarR[kBlock] = {}; // and over the taraf (the sitar)
    float subL[kBlock] = {}, subR[kBlock] = {}; // and the log drum's, past the 95 Hz high-pass
    float *busL[5] = {dryL, chL, rotL, tarL, subL}, *busR[5] = {dryR, chR, rotR, tarR, subR};
    auto offsetOfBeat = [&](double beat) {
        const double o = std::ceil((beat - b0) / bps - 1e-7);
        return o <= 0 ? 0 : o >= double(n) ? n : int(o);
    };
    auto renderVoices = [&](int from, int to) {
        if (to <= from)
            return;
        for (Voice &v : s.voices)
            if (v.on)
                v.render(busL[v.bus], busR[v.bus], sendL, sendR, from, to, cutHz[v.lane], res[v.lane],
                         send[v.lane], s.rate);
    };
    int pos = 0;
    double barLine = std::ceil(b0 / kBeatsPerBar - 1e-9) * kBeatsPerBar;
    for (int guard = 0; guard < 512; ++guard) {
        int next = n;
        if (s.pendingCount > 0) {
            const uint64_t at = s.pending[0].at;
            next = std::min(next, at <= s.rendered ? 0 : int(std::min<uint64_t>(at - s.rendered, uint64_t(n))));
        }
        const int barOff = offsetOfBeat(barLine);
        next = std::min(next, barOff);
        for (auto &L : s.lanes)
            if (L.running)
                next = std::min(next, offsetOfBeat(L.next));
        next = std::max(next, pos);
        if (next >= n)
            break;
        renderVoices(pos, next);
        pos = next;
        s.cursor = pos;
        const double beatNow = b0 + double(pos) * bps;
        // Commands first, then the bar line, then the steps.
        while (s.pendingCount > 0 && s.pending[0].at <= s.rendered + uint64_t(pos)) {
            const Command c = s.pending[0].c;
            for (int i = 1; i < s.pendingCount; ++i)
                s.pending[i - 1] = s.pending[i];
            --s.pendingCount;
            s.apply(c, beatNow);
        }
        if (barOff <= pos) {
            s.barIndex = int64_t(std::llround(barLine / kBeatsPerBar));
            s.barCount = s.lanes[0].counter;
            barLine += kBeatsPerBar;
        }
        for (int k = 0; k < Impl::kLanes; ++k) {
            auto &L = s.lanes[k];
            int fired = 0;
            while (L.running && offsetOfBeat(L.next) <= pos && fired++ < 8)
                s.fire(k, std::max(L.next, beatNow), tr, h);
        }
    }
    renderVoices(pos, n);
    // The chorus's mode and the Leslie's speed follow the switch of a lane
    // playing that sound (the live one first); with none, they stay put, so
    // a Leslie spun up keeps spinning down after the notes stop.
    for (int k = Impl::kLanes - 1; k >= 0; --k) {
        const auto &L = s.lanes[k];
        if (!(k == 0 ? L.running || L.count > 0 : L.running))
            continue;
        if (s.eff[k].sound == Juno)
            s.chorusOne = s.eff[k].alt;
        if (s.eff[k].sound == Organ)
            s.leslieFast = s.eff[k].alt;
    }
    s.chorus.set(s.chorusOne);
    s.chorus.process(chL, chR, dryL, dryR, n);
    s.leslie.process(rotL, rotR, dryL, dryR, n, s.leslieFast);
    {
        // The taraf follows the key: the tonic, and the scale's own fifth.
        const Tuning &tu = h.tuning;
        float fifth = 7.0f, best = 1.0f;
        for (int d = 1; d < tu.degreesPerOctave(); ++d) {
            const float semis = float(tu.degreeSemis(d));
            if (std::fabs(semis - 7.0f) < best) {
                best = std::fabs(semis - 7.0f);
                fifth = semis;
            }
        }
        s.taraf.tune(float(tu.baseMidi()), fifth);
        s.taraf.process(tarL, tarR, dryL, dryR, n);
    }
    // The echo, from whichever lane is playing (the live one first).
    const int lead = s.lanes[0].running ? 0 : s.clipShown();
    const Settings &e = s.eff[lead];
    const float fb = std::min(0.93f, 0.2f + 0.42f * e.echo + 0.4f * e.throwFb);
    s.echo.process(sendL, sendR, dryL, dryR, n, float(echoBeats(e.echoTime) * tr.samplesPerBeat()), fb,
                   0.8f + 0.12f * e.throwFb);
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        // A soft ceiling: the attack of a stab, or a resonant peak thrown
        // through the echo, rounds off instead of spiking the bus.
        const float L = fastTanh((s.hpL.step(dryL[i]).high + s.subHpL.step(subL[i]).high) * kOutGain),
                    R = fastTanh((s.hpR.step(dryR[i]).high + s.subHpR.step(subR[i]).high) * kOutGain);
        l[i] += L;
        r[i] += R;
        pk = std::max(pk, std::max(std::fabs(L), std::fabs(R)));
    }
    s.level = std::max(pk, s.level * 0.97f);
    const float decay = std::exp(-float(n) / (0.18f * s.rate));
    for (auto &L : s.lanes)
        for (float &f : L.flash)
            f *= decay;
    s.rendered += uint64_t(n);
}

void Engine::silence(bool hard) {
    for (auto &L : d->lanes) {
        L.count = 0;
        L.running = false;
    }
    d->pendingCount = 0;
    for (Voice &v : d->voices) {
        if (hard)
            v.on = false;
        else if (v.on)
            v.release();
    }
}

void Engine::publish(float *data) {
    const Impl &s = *d;
    // The loop's layers show together: their notes, the steps they flash;
    // its step and settings are the first layer playing's.
    const int clip = s.clipShown();
    float clipPool = 0;
    bool clipRunning = false;
    for (int k = 1; k < Impl::kLanes; ++k)
        if (s.lanes[k].running) {
            clipPool += float(s.lanes[k].count);
            clipRunning = true;
        }
    data[kDataLivePool] = s.lanes[0].running ? float(s.lanes[0].count) : 0.0f;
    data[kDataClipPool] = clipPool;
    data[kDataLiveStep] = float(s.lanes[0].lastInBar);
    data[kDataClipStep] = float(s.lanes[clip].lastInBar);
    data[kDataBar] = float(s.barIndex);
    data[kDataBarCount] = float(s.barCount);
    data[kDataLiveRunning] = s.lanes[0].running ? 1.0f : 0.0f;
    data[kDataClipRunning] = clipRunning ? 1.0f : 0.0f;
    for (int i = 0; i < kFlashCells; ++i) {
        data[kDataFlash + i] = s.lanes[0].flash[i];
        float f = 0;
        for (int k = 1; k < Impl::kLanes; ++k)
            f = std::max(f, s.lanes[k].flash[i]);
        data[kDataClipFlash + i] = f;
    }
    data[kDataEcho] = s.echo.level;
    data[kDataLevel] = s.level;
    float env = 0;
    for (const Voice &v : s.voices)
        if (v.on && !v.released)
            env = std::max(env, v.fenv * v.amp);
    data[kDataEnv] = env;
    data[kDataOverlay] = float(s.overlayMask);
    data[kDataLiveRatchet] = float(s.lanes[0].lastRatchet);
    data[kDataLiveAccent] = s.lanes[0].lastAccent ? 1.0f : 0.0f;
    for (int i = 0; i < kRecent; ++i) {
        const Fired &f = s.recent[(s.recentHead + i) % kRecent];
        float *o = data + kDataRecent + 4 * i;
        o[0] = float(std::fmod(f.beat, 64.0));
        o[1] = float(f.degree);
        o[2] = f.accent ? -f.vel : f.vel;
        o[3] = f.midi > 0 ? float(std::min(f.lane, 1) + 1) : 0.0f; // 1 live, 2 the loop (any layer)
    }
}

std::unique_ptr<PartEngine> Engine::twin() const { return std::make_unique<Engine>(); }

int Engine::voices() const {
    int n = 0;
    for (const Voice &v : d->voices)
        n += v.on;
    return n;
}

std::vector<Engine::Fired> Engine::log() const {
    std::vector<Fired> out;
    const uint64_t n = std::min<uint64_t>(d->logCount, m_log.size());
    for (uint64_t i = d->logCount - n; i < d->logCount; ++i)
        out.push_back(m_log[i % m_log.size()]);
    return out;
}

void Engine::clearLog() { d->logCount = 0; }

const Settings &Engine::laneSettings(int lane) const { return d->eff[lane <= 0 ? 0 : d->clipShown()]; }
int Engine::statesReceived() const { return d->states; }
uint64_t Engine::laneCounter(int lane) const { return d->lanes[lane <= 0 ? 0 : d->clipShown()].counter; }

// --- the page -------------------------------------------------------------------

namespace {

// Where things are on the pad, in pad units (0..1, y up).
constexpr float kSplit = 0.58f;      // the notes left of this, the six pads right of it
constexpr float kChipsTop = 0.865f;  // pattern chips above
constexpr float kChipsLow = 0.15f;   // sound and switch chips below
constexpr int kColumns = 8;
// Below the grid, seven columns: the sounds two rows deep in the first five
// (the dance floor's on top, the wider world's under them, as Z X C V B and
// Shift+Z X C V B), then follow and latch the height of both. Ten in one row
// would be too thin to read or hit, and a third row too low to hit, so the
// lower row is a window of five onto all the sounds past the dance floor's:
// it follows the lit one, and the chip at an end where more wait beyond is
// faded (as console::chips() does for the band's rows); touching it picks
// that sound, which moves the window along.
constexpr int kBottomChips = 7;
constexpr int kSoundCols = 5;
constexpr int kWorld = SoundCount - kSoundCols; // the sounds the lower row looks onto
static_assert(kWorld >= kSoundCols && kWorld <= 2 * kSoundCols);
// The first of the lower row's sounds (counted from the first past the top
// row) with `sound` lit, the window having stood at `rest`. It moves only
// as far as it must for the lit chip to sit clear of a faded end, so a
// chip picked in the middle of the row stays under the finger (the lit
// organ or juno tapped again is its switch), and one picked at a faded end
// draws the row along by one.
int worldFirst(int sound, int rest) {
    rest = std::clamp(rest, 0, kWorld - kSoundCols);
    if (sound < kSoundCols)
        return rest;
    const int at = sound - kSoundCols;
    const int lo = rest + (rest > 0 ? 1 : 0);
    const int hi = rest + kSoundCols - 1 - (rest + kSoundCols < kWorld ? 1 : 0);
    if (at < lo)
        return std::max(0, at - 1);
    if (at > hi)
        return std::min(kWorld - kSoundCols, at - kSoundCols + 2);
    return rest;
}
// The sound chip under a point of the band, or −1 (follow, latch).
int soundChip(float u, float y, int first) {
    const int col = std::clamp(int(u * kBottomChips), 0, kBottomChips - 1);
    if (col >= kSoundCols)
        return -1;
    return y > 0.5f * kChipsLow ? col : kSoundCols + first + col;
}
// The letter that picks a sound, and how many presses of it with Shift
// (0 for none: the top row's sounds need no Shift).
char soundKey(int sound) { return "ZXCVB"[sound % kSoundCols]; }
int soundShifts(int sound) { return sound / kSoundCols; }
constexpr float kPadW = (1.0f - kSplit) / 2.0f;

enum PadId { PadRate, PadRhythm, PadRange, PadTone, PadGroove, PadEcho, PadCount };

const char *padTitle(int p) {
    static const char *t[] = {"RATE · GATE", "RHYTHM", "RANGE", "TONE", "GROOVE", "ECHO"};
    return t[p];
}

QRectF padArea(int p) { // in pad units, y up: top() is the low edge
    const int col = p % 2, row = p / 2;
    const float x0 = kSplit + float(col) * kPadW;
    const float y1 = 1.0f - float(row) / 3.0f;
    return QRectF(QPointF(x0, y1 - 1.0f / 3.0f), QPointF(x0 + kPadW, y1));
}

// A finger's place within a pad, 0..1 each way, the title band left out.
QPointF padLocal(int p, float x, float y) {
    const QRectF a = padArea(p);
    const float u = (x - float(a.left())) / float(a.width());
    const float v = (y - float(a.top())) / float(a.height());
    return {clampf((u - 0.07f) / 0.86f, 0, 1), clampf((v - 0.06f) / 0.76f, 0, 1)};
}

// A zone of count along v, with a little give at the edges so a finger
// resting on a line does not flicker between two.
int zone(float v, int count, int current) {
    const float raw = v * float(count);
    if (current >= 0 && current < count && raw > float(current) - 0.18f && raw < float(current + 1) + 0.18f)
        return current;
    return std::clamp(int(raw), 0, count - 1);
}

float wrapAngle(float a) {
    while (a > kPi * 0.5f)
        a -= kPi;
    while (a < -kPi * 0.5f)
        a += kPi;
    return a;
}

int perOctaveOf(const StudioView &v) {
    Tuning t;
    t.set(v.root, v.family, v.brightness, v.just);
    return t.degreesPerOctave();
}

QString noteLabel(const Tuning &t, int reg, int row, int col) {
    const int midi = int(std::lround(t.baseMidi() + 12.0 * (reg + row) + t.degreeSemis(col)));
    return QStringLiteral("%1%2").arg(QString::fromUtf8(Tuning::noteName(((midi % 12) + 12) % 12))).arg(midi / 12 - 1);
}

// A tiny drawing of a pattern's shape: eight steps of it over a triad
// through two octaves.
QPainterPath patternGlyph(int pattern, const QRectF &r) {
    PoolView pv;
    const int deg[3] = {0, 2, 4};
    const float vel[3] = {1, 1, 1};
    buildPool(deg, vel, 3, 2, 7, pv);
    QPainterPath path;
    const int steps = 8;
    for (int s = 0; s < steps; ++s) {
        int out[16];
        float ov[16];
        const int n = patternNotes(pattern, pv, uint64_t(s), 3, 2, 7, out, ov, 16);
        const qreal x = r.left() + r.width() * (s + 0.5) / steps;
        for (int k = 0; k < n; ++k) {
            const qreal y = r.bottom() - r.height() * out[k] / 11.0;
            if (pattern == ChordStab)
                path.addRect(QRectF(x - 2, y - 1.5, 4, 3));
            else if (s == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }
    }
    return path;
}

class ArpControl : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        m_perOct = perOctaveOf(ctx.studio());
        // Lifts.
        for (auto &n : m_notes)
            if (n.used && !n.key && n.touch >= 0 && !present(f, n.touch))
                liftNote(n, f.t, ctx);
        for (auto &k : m_knobs)
            if (k.touch >= 0 && !present(f, k.touch))
                liftKnob(k);
        for (int &c : m_chipTouches)
            if (c >= 0 && !present(f, c))
                c = -1;
        for (int &s : m_spentTouches)
            if (s >= 0 && !present(f, s))
                s = -1;
        // Landings and moves.
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            if (spentTouch(t.id) || chipHeld(t.id))
                continue;
            if (Note *n = noteFor(t.id))
                moveNote(*n, t, f.t, ctx);
            else if (Knob *k = knobFor(t.id))
                moveKnob(*k, t, f.t);
            else
                land(t, f.t, ctx);
        }
        // The click latches. A hard press on a pad can register as a click
        // on a force-sensing trackpad, so a click that comes while a pad is
        // pressed, or just after, is not one.
        if (f.button && !m_button) {
            m_button = true;
            m_buttonIgnored = pressing() || f.t - m_lastPress < 0.3;
        } else if (f.button && pressing()) {
            m_buttonIgnored = true;
        } else if (!f.button && m_button) {
            m_button = false;
            if (!m_buttonIgnored)
                setLatch(!m_latch, f.t, ctx);
        }
        flush(f.t, ctx, false);
    }

    void idle(double t, ControlContext &ctx) override { flush(t, ctx, false); }

    void background(double t, ControlContext &ctx) override {
        const double beat = ctx.beatAt(t);
        // Space's take (the session's). The live notes end exactly where the
        // loop takes over, so the two never play the same notes side by
        // side: a scheduled Off a moment before the take's beat (late enough
        // that Space can still call it off), or at once when a take lands
        // without having waited (below).
        const StudioView &sv = ctx.studio();
        const PartView &pv = sv.parts[ctx.part()];
        m_running = pv.data[kDataLiveRunning] > 0.5f || pv.data[kDataClipRunning] > 0.5f;
        if (pv.recording >= 0) {
            m_take = pv.recording;
            m_takeView = pv;
            m_takeBeat = beat;
            if (!m_takeHanded && anyNotes()) {
                const double lead = 0.06 * sv.bpm / 60.0;
                if (pv.takeAt > 0 && beat >= pv.takeAt - lead)
                    handToTake(std::max(ctx.timeAt(pv.takeAt), t), ctx);
            }
        } else {
            m_take = -1;
            m_takeHanded = false;
        }
        // A take that closed on a bar line already passed (Space just after
        // the line), or one that sets the clock, lands in the studio's next
        // block without ever showing as waiting: the count of takes going up
        // is the only sign. The loop is playing now, so the live notes hand
        // over at once.
        if (m_takesSeen != pv.takes) {
            const bool landed = m_takesSeen != UINT32_MAX && pv.playing >= 0;
            m_takesSeen = pv.takes;
            if (landed && !m_takeHanded) {
                // Tell the loop whether it is on the take's own bars.
                Command sync;
                sync.type = kSync;
                sync.flags = Command::NoRecord;
                sync.id = kStateId;
                ctx.send(sync);
                if (anyNotes()) {
                    m_take = pv.playing;
                    handToTake(t, ctx, true);
                    m_take = -1;
                }
            }
            m_takeHanded = false;
        }
        // At every bar line, the notes held hand over to fresh ids, so any
        // window that starts on a bar holds them from its first moment, and
        // knows where the pattern was.
        const int64_t bar = int64_t(std::floor(beat / kBeatsPerBar));
        if (bar > m_handedBar) {
            if (!anyNotes()) {
                m_handedBar = bar;
            } else {
                const float *data = ctx.studio().parts[ctx.part()].data;
                const bool published = int64_t(std::llround(data[kDataBar])) == bar;
                if (published || beat - double(bar) * kBeatsPerBar > 0.3) {
                    handOver(bar, published ? int(data[kDataBarCount]) : -1, ctx);
                    m_handedBar = bar;
                }
            }
        }
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const double t = ctx.now();
        m_perOct = perOctaveOf(ctx.studio());
        // The piano row: notes, held like fingers.
        static const int piano[] = {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H,
                                    Qt::Key_J, Qt::Key_K, Qt::Key_L, Qt::Key_Semicolon, Qt::Key_Apostrophe};
        for (int i = 0; i < 11; ++i)
            if (k.key == piano[i]) {
                if (k.repeat)
                    return true;
                if (k.down) {
                    if (!keyNote(k.key))
                        addNote(-1, k.key, i % m_perOct, i / m_perOct, 0.78f, t, ctx);
                } else if (Note *n = keyNote(k.key)) {
                    liftNote(*n, t, ctx);
                }
                flush(t, ctx, false);
                return true;
            }
        if (!k.down)
            return false;
        static const int patterns[] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R,
                                       Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I};
        for (int i = 0; i < PatternCount; ++i)
            if (k.key == patterns[i]) {
                m_s.pattern = i;
                ctx.notice(QStringLiteral("Pattern: %1").arg(QString::fromUtf8(patternName(i))));
                flush(t, ctx, true);
                return true;
            }
        // Z X C V B the first five sounds, with Shift the second five: the
        // same keys as the two rows of chips. The letters ran out at ten, so
        // Shift and a letter pressed again on its own lit sound goes on to
        // the third five, and once more back to the second (the lower row's
        // chips wear ⇧ or ⇧⇧ to say which).
        // A second-row sound with a third behind its letter is held back
        // from the engine a moment (see holdSound()), so the double press
        // goes from what was playing straight to the third.
        static const int sounds[] = {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B};
        for (int i = 0; i < kSoundCols; ++i)
            if (k.key == sounds[i]) {
                int to = i;
                bool hold = false;
                if (k.modifiers & Qt::ShiftModifier) {
                    to = i + kSoundCols;
                    if (m_s.sound == to && to + kSoundCols < SoundCount)
                        to += kSoundCols;
                    else
                        hold = to + kSoundCols < SoundCount;
                }
                setSound(to, ctx);
                if (hold)
                    holdSound(t);
                flush(t, ctx, true);
                return true;
            }
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift)
                ctx.clear(slot);
            else
                ctx.launch(slot);
            return true;
        }
        // The keys every part page shares come first (← → sound, ↑ ↓
        // register, Shift+← → rate, - + tone, Return latch, Shift+Return
        // overdub, the key left of 1 follow, Delete let go), chosen so none
        // needs AltGr on a Nordic keyboard; [ ] = \ and M stay as second
        // keys, and the letters are the arp's own.
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift)
                toggleOverdub(ctx);
            else
                setLatch(!m_latch, t, ctx);
            return true;
        case Qt::Key_Delete:
            letGo(t, ctx);
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (shift)
                stepRate(k.key == Qt::Key_Right ? 1 : -1, ctx);
            else
                stepSound(k.key == Qt::Key_Right ? 1 : -1, ctx);
            break;
        case Qt::Key_Up:
        case Qt::Key_Down:
            if (shift)
                stepOctaves(k.key == Qt::Key_Up ? 1 : -1, ctx);
            else
                stepRegister(k.key == Qt::Key_Up ? 1 : -1, ctx);
            break;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            stepRate(k.key == Qt::Key_BracketRight ? 1 : -1, ctx);
            break;
        case Qt::Key_Minus:
        case Qt::Key_Equal:
        case Qt::Key_Plus:
            stepTone(k.key == Qt::Key_Minus ? -1 : 1, ctx);
            break;
        case Qt::Key_section:
        case Qt::Key_QuoteLeft:
        case Qt::Key_Backslash:
        case Qt::Key_M:
            toggleFollow(ctx);
            break;
        case Qt::Key_Comma:
        case Qt::Key_Period:
            stepStack(k.key == Qt::Key_Period ? 1 : -1, t, ctx);
            break;
        case Qt::Key_N:
            newDice(t, ctx);
            break;
        case Qt::Key_Slash:
            flipSwitch(ctx);
            break;
        case Qt::Key_O:
        case Qt::Key_P: {
            const int n = stepsPerBar(m_s.rate);
            const int hits = std::clamp(hitsFor(m_s.density, n) + (k.key == Qt::Key_P ? 1 : -1), 1, n);
            m_s.density = float(hits) / float(n);
            ctx.notice(QStringLiteral("%1 of %2 steps").arg(hits).arg(n));
            break;
        }
        default:
            return false;
        }
        flush(t, ctx, true);
        return true;
    }

    // The mouse on the band under the pad: the clips, and a control for
    // everything a key does that the pad's own chips and pads don't.
    enum Spot {
        SpotSoundDown = 100, SpotSoundUp, SpotRateDown, SpotRateUp, SpotStackDown, SpotStackUp,
        SpotRegDown, SpotRegUp, SpotOctDown, SpotOctUp, SpotToneDown, SpotToneUp,
        SpotDice, SpotOverdub, SpotLetGo
    };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
            return true;
        const double t = ctx.now();
        m_perOct = perOctaveOf(ctx.studio());
        switch (id) {
        case SpotSoundDown: stepSound(-1, ctx); break;
        case SpotSoundUp: stepSound(1, ctx); break;
        case SpotRateDown: stepRate(-1, ctx); break;
        case SpotRateUp: stepRate(1, ctx); break;
        case SpotStackDown: stepStack(-1, t, ctx); break;
        case SpotStackUp: stepStack(1, t, ctx); break;
        case SpotRegDown: stepRegister(-1, ctx); break;
        case SpotRegUp: stepRegister(1, ctx); break;
        case SpotOctDown: stepOctaves(-1, ctx); break;
        case SpotOctUp: stepOctaves(1, ctx); break;
        case SpotToneDown: stepTone(-1, ctx); break;
        case SpotToneUp: stepTone(1, ctx); break;
        case SpotDice: newDice(t, ctx); break;
        case SpotOverdub: toggleOverdub(ctx); return true;
        case SpotLetGo: letGo(t, ctx); return true;
        default: return false;
        }
        flush(t, ctx, true);
        return true;
    }

    void leave(ControlContext &ctx) override {
        const double t = ctx.now();
        // Fingers and keys let go; a latched pattern keeps running, the way
        // a loop does, until it is unlatched here.
        for (auto &n : m_notes)
            if (n.used && (n.touch >= 0 || n.key))
                liftNote(n, t, ctx, true);
        for (auto &k : m_knobs)
            if (k.touch >= 0)
                liftKnob(k);
        m_chipTouches.fill(-1);
        m_spentTouches.fill(-1);
        m_button = false;
        flush(t, ctx, true);
        m_frame = Frame{};
    }

    // Ctrl+N or a song opened: the studio has silenced the engine and
    // emptied its pools, so the notes the latch held belong to the old
    // song. Kept, they would come back at the next bar line's hand-over.
    // The studio turns overdub off, and so does the page; the settings stay,
    // as they always have after Ctrl+N (a song opened sets its own).
    void startOver(ControlContext &ctx) override {
        (void)ctx;
        for (auto &n : m_notes)
            n = Note{};
        m_overdub = false;
        m_take = -1;
        m_takeHanded = false;
        m_latchToLoopAt = -1e9;
        m_latchToLoopSlot = -1;
    }

    // Ctrl+S: the arp as the player set it up, pattern, sound, rhythm,
    // range, tone, groove, echo and latch. A choice from a list goes by its
    // name, so a list that grows or is reordered still finds it. The
    // presses (rolls, bite, harder accents, a thrown echo) end when the
    // finger lifts, and the notes a latch holds are a hand's, so neither is
    // kept; nor is overdub, which only means something while a take is
    // being made.
    void save(QJsonObject &out) const override {
        const Settings &s = m_s;
        out[QStringLiteral("pattern")] = QString::fromUtf8(patternName(s.pattern));
        out[QStringLiteral("sound")] = QString::fromUtf8(soundName(s.sound));
        out[QStringLiteral("color")] = tidyFloat(s.color);
        out[QStringLiteral("rate")] = QString::fromUtf8(rateName(s.rate));
        out[QStringLiteral("gate")] = tidyFloat(s.gate);
        out[QStringLiteral("swing")] = tidyFloat(s.swing);
        out[QStringLiteral("density")] = tidyFloat(s.density);
        out[QStringLiteral("rotate")] = s.rotate;
        out[QStringLiteral("register")] = s.reg;
        out[QStringLiteral("octaves")] = s.octaves;
        out[QStringLiteral("firstNote")] = QString::fromUtf8(stackName(s.stack));
        out[QStringLiteral("follow")] = s.follow;
        // The sound's own switch: the organ's Leslie fast, the juno's chorus I.
        out[QStringLiteral("switch")] = s.alt;
        out[QStringLiteral("tone")] = tidyFloat(s.cutoff);
        out[QStringLiteral("resonance")] = tidyFloat(s.res);
        out[QStringLiteral("accentEvery")] = accentCycle(s.accent);
        out[QStringLiteral("chaos")] = tidyFloat(s.chaos);
        out[QStringLiteral("dice")] = qint64(s.seed);
        out[QStringLiteral("echoTime")] = QString::fromUtf8(echoName(s.echoTime));
        out[QStringLiteral("echo")] = tidyFloat(s.echo);
        out[QStringLiteral("latch")] = m_latch;
    }

    // Ctrl+O, after startOver(): what save() wrote, checked value by value
    // (the file may be old or edited by hand), over the page's defaults
    // rather than the song before's. The engine then hears the settings the
    // way a key sends them.
    void load(const QJsonObject &in, ControlContext &ctx) override {
        Settings s;
        auto choice = [&](const char *key, int count, const char *(*name)(int), int &to) {
            const QString v = in.value(QLatin1String(key)).toString();
            for (int i = 0; i < count; ++i)
                if (v == QString::fromUtf8(name(i)))
                    to = i;
        };
        auto number = [&](const char *key, double lo, double hi, float &to) {
            const QJsonValue v = in.value(QLatin1String(key));
            if (v.isDouble() && std::isfinite(v.toDouble()))
                to = float(std::clamp(v.toDouble(), lo, hi));
        };
        auto whole = [&](const char *key, int lo, int hi, int &to) {
            const QJsonValue v = in.value(QLatin1String(key));
            if (v.isDouble() && std::isfinite(v.toDouble()))
                to = int(std::lround(std::clamp(v.toDouble(), double(lo), double(hi))));
        };
        auto flag = [&](const char *key, bool &to) {
            const QJsonValue v = in.value(QLatin1String(key));
            if (v.isBool())
                to = v.toBool();
        };
        choice("pattern", PatternCount, patternName, s.pattern);
        choice("sound", SoundCount, soundName, s.sound);
        // A sound without its colour starts from the one it sounds best in,
        // as picking it does.
        s.color = soundColor(s.sound);
        number("color", 0, 1, s.color);
        choice("rate", RateCount, rateName, s.rate);
        number("gate", 0.04, 1, s.gate);
        number("swing", 0, 1, s.swing);
        number("density", 0, 1, s.density);
        whole("rotate", 0, 31, s.rotate);
        whole("register", 0, kRegisters - 1, s.reg);
        whole("octaves", 1, kMaxOctaves, s.octaves);
        choice("firstNote", kStacks, stackName, s.stack);
        flag("follow", s.follow);
        flag("switch", s.alt);
        number("tone", 0.04, 1, s.cutoff);
        number("resonance", 0, 1, s.res);
        int every = -1;
        whole("accentEvery", 0, 64, every);
        for (int i = 0; i < kAccentChoices; ++i)
            if (accentCycle(i) == every)
                s.accent = i;
        number("chaos", 0, 1, s.chaos);
        int dice = int(s.seed);
        whole("dice", 0, 0xFFFFFF, dice);
        s.seed = uint32_t(dice);
        choice("echoTime", kEchoTimes, echoName, s.echoTime);
        number("echo", 0, 1, s.echo);
        bool latch = false;
        flag("latch", latch);
        // No notes are held after startOver(), so the latch needs no
        // letting go, only setting.
        m_latch = latch;
        m_s = s;
        m_holdUntil = -1;
        // Sent whole even when within flush()'s half a percent of what the
        // engine has, so the arp plays exactly what the file says.
        const double t = ctx.now();
        if (m_s != m_sent)
            sendState(t, ctx, false);
        flush(t, ctx, true);
    }

    void paint(QPainter &p, const PaintContext &pc) override {
        namespace c = console;
        p.setRenderHint(QPainter::Antialiasing, true);
        m_spots.clear();
        c::pad(p, pc, QStringLiteral("Arp"));
        c::variation(p, pc, QString::fromUtf8(soundName(m_s.sound)));
        paintGrid(p, pc);
        paintChips(p, pc);
        paintPads(p, pc);
        paintFingers(p, pc);
        c::beats(p, pc, pc.color);
        // Space's take, while it waits for its bar line, on the pad's top edge.
        if (pc.view->recording >= 0)
            c::tag(p, QPointF(pc.pad.center().x(), c::tabY(pc)),
                   QStringLiteral("LOOP TAKE · %1 · SPACE CALLS IT OFF").arg(takeText(*pc.view, pc.beat)),
                   std::fmod(pc.time, 0.5) < 0.25 ? c::red() : c::alpha(c::red(), 0.7f), 12, true);
        paintStrip(p, pc);
    }

    QString help() const override {
        return QStringLiteral(
            "← →\tthe sound: pluck, glass, supersaw, pulse, acid, marimba, harp, rhodes, juno, organ, steel pan, "
            "sitar, log drum, chip, gamelan (Z X C V B pick one of the first five, Shift+Z X C V B of the second, "
            "and Shift+the same letter again within a moment of the third: ⇧⇧ on its chip; the second is held "
            "back that moment, so the double press goes straight to the third without sounding the second)\n"
            "↑ ↓ · Shift+↑ ↓\tregister: where the grid starts · range: how many octaves the pattern climbs\n"
            "Shift+← → (or [ ])\tthe rate: 1/8 to 1/32, with triplets\n"
            "- +\ttone: darker, brighter\n"
            "Shift+Return · F10\tlatch · overdub on or off (what you play joins the loop)\n"
            "§ (the key left of 1; or \\ or M)\tfollow the chords page\n"
            "Delete\tlet go of what is latched\n"
            "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot · clear it · stop the loop at the bar\n"
            "under the pad, with the mouse\tthe clips (click: launch or stop), sound, rate, first note, register, "
            "range, tone, dice, overdub, let go; each chip shows its key\n"
            "touch the grid\ta note of the scale, the upper row an octave up; press harder for louder\n"
            "first finger\tgrows into a chord; twist it: single, fifth, triad, seventh, ninth\n"
            "more fingers\tadd their notes; slide one sideways to walk it along the scale\n"
            "top chips\tpattern: up, down, up·down, converge, as played, random, chord stabs, pedal\n"
            "bottom chips\tsound, two rows: pluck, glass, supersaw, pulse, acid · marimba, harp, rhodes, juno, organ, "
            "steel pan, sitar, log drum, chip, gamelan (five at a time, following the lit one; a faded chip at an "
            "end has more beyond it) · FOLLOW the chords page · LATCH\n"
            "RATE · GATE\tup: 1/8 to 1/32 with triplets · across: short to legato · twist: swing\n"
            "RHYTHM\tacross: euclidean hits per bar · up: turn the rhythm · press hard: rolls\n"
            "RANGE\tacross: register · up: one to four octaves\n"
            "TONE\tup: cutoff · across: the sound's colour · twist: resonance (on the log drum: its glide) · press: "
            "bite (on the organ: the Leslie, slow or fast; on the juno: chorus II or I; on the gamelan: selisir or "
            "even; the cutoff and colour stay put unless the finger slides)\n"
            "/ (or the lit organ, juno or gamelan chip)\tthe same switch: the Leslie slow or fast, chorus II or I, "
            "the gamelan on the five keys of a gong kebyar's selisir or even on the studio's scale\n"
            "the third five\tsteel pan (colour: rubber to bare stick; its neighbouring notes ring along; a gate past "
            "0.8 at 1/8 or slower rolls each note, as a pannist holds one) · sitar (colour: the jawari closed to "
            "open; the sympathetic strings are tuned to the key; the chikari drone strikes on accents, and with no "
            "accent set on each bar's first step; with the gate all the way, a note on the beat, or on an accent "
            "when accents are set, a 2nd to a 4th from the last is a meend, pulled along the fret, and the rest "
            "fresh plucks) · log drum (sounds an octave under the named notes; colour: round to gritty; each log "
            "glides on from the last, as slowly as the tone's twist says; accents yelp up before they fall) · chip "
            "(colour: the duty, 12.5, 25 or 50 %, each from a clean blip to the hardware's tick; accents are the "
            "sweep down into the note, the chord pattern an arpeggio at 60 a second in one channel) · gamelan "
            "(kotekan: the polos and the sangsih split a norot figure around each four steps' first note, the "
            "note and the key above in turn, and meet on shared keys; in selisir every note is one of its five "
            "keys)\n"
            "GROOVE\tacross: accent every 2, 3, 4, 5 or 7 notes · up: chaos · press: harder accents\n"
            "ECHO\tacross: 1/16 to dotted 1/4 · up: amount · press: throw the feedback\n"
            "right hand, no notes held\tshapes the playing loop, and lets go of it on lifting\n"
            "click\tlatch: the pattern keeps running when the fingers lift (not while a pad is pressed)\n"
            "the mouse on the pad\ta finger: the grid, the chips, the pads; with LATCH on, each click adds a note "
            "to the chord and a click on a latched note takes it away\n"
            "Space\tloops what you just played, from the bar line (a latch lets go: the loop plays it on); Space again calls it off\n"
            "A S D F G H J K L ; '\tnotes, held like fingers\n"
            "Q W E R T Y U I\tthe eight patterns\n"
            ", . · O P · N\twhat the first note grows into · fewer or more hits · new dice for random and chaos");
    }

    // What the tutorial asks: whether the arp follows the chords page, is
    // latched, and runs (live or from its loop); and for the tests, the tone.
    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("follow"))
            return m_s.follow ? 1.0f : 0.0f;
        if (name == QLatin1String("latched"))
            return m_latch ? 1.0f : 0.0f;
        if (name == QLatin1String("running"))
            return m_running ? 1.0f : 0.0f;
        if (name == QLatin1String("tone"))
            return m_s.cutoff;
        return PartControl::tutorialProbe(name);
    }

    QString status() const override {
        int held = 0;
        for (const auto &n : m_notes)
            held += n.used;
        QString s = held == 0 ? QStringLiteral("No notes: touch the grid")
                              : QStringLiteral("%1 note%2").arg(held).arg(held > 1 ? "s" : "");
        s += QStringLiteral(" · %1 · %2 · %3 oct · %4")
                 .arg(QString::fromUtf8(patternName(m_s.pattern)), QString::fromUtf8(rateName(m_s.rate)))
                 .arg(m_s.octaves)
                 .arg(QString::fromUtf8(soundName(m_s.sound)));
        if (m_s.sound == Organ)
            s += m_s.alt ? QStringLiteral(" (Leslie fast)") : QStringLiteral(" (Leslie slow)");
        else if (m_s.sound == Juno)
            s += m_s.alt ? QStringLiteral(" (chorus I)") : QStringLiteral(" (chorus II)");
        else if (m_s.sound == Gamelan)
            s += m_s.alt ? QStringLiteral(" (even)") : QStringLiteral(" (selisir)");
        if (m_s.follow)
            s += QStringLiteral(" · following the chords");
        if (m_latch)
            s += QStringLiteral(" · latched");
        if (m_overdub)
            s += QStringLiteral(" · overdub");
        if (m_take >= 0)
            s += QStringLiteral(" · loop take: %1 (Space calls it off)").arg(takeText(m_takeView, m_takeBeat).toLower());
        else if (held > 0)
            s += QStringLiteral(" · Space loops what you just played");
        return s;
    }

private:
    struct Note {
        bool used = false;
        int touch = -1;  // the finger, or −1 for a key or a latched note
        bool key = false;
        int keyCode = 0;
        int id = 0;      // the engine's id, renewed at every bar line
        int col = 0, row = 0, stack = 0;
        float vel = 0.8f;
        double t0 = 0;
        bool first = false;
        bool fresh = false; // the On about to go out lands on an empty hand
        float angle0 = 0;
        int stack0 = 0;
        double lastMove = 0;
    };
    struct Knob {
        int touch = -1;
        int pad = 0;
        float angle0 = 0;
        float base = 0; // the twisted value when the finger landed
        bool overlay = false;
        bool pressing = false;
        // On a sound with a switch, a finger landing on the tone pad leaves
        // the cutoff and colour where they were until it slides, so the
        // Leslie can be thrown mid-groove without moving the drawbars.
        bool keep = false;
        float u0 = 0, v0 = 0;
    };

    static bool present(const Frame &f, int id) {
        for (int i = 0; i < f.count; ++i)
            if (f.touches[i].id == id)
                return true;
        return false;
    }
    Note *noteFor(int touch) {
        for (auto &n : m_notes)
            if (n.used && n.touch == touch && !n.key)
                return &n;
        return nullptr;
    }
    Note *keyNote(int code) {
        for (auto &n : m_notes)
            if (n.used && n.key && n.keyCode == code)
                return &n;
        return nullptr;
    }
    Knob *knobFor(int touch) {
        for (auto &k : m_knobs)
            if (k.touch == touch)
                return &k;
        return nullptr;
    }
    bool chipHeld(int touch) const {
        for (int c : m_chipTouches)
            if (c == touch)
                return true;
        return false;
    }
    bool spentTouch(int touch) const {
        for (int s : m_spentTouches)
            if (s == touch)
                return true;
        return false;
    }
    bool anyNotes() const {
        for (const auto &n : m_notes)
            if (n.used)
                return true;
        return false;
    }
    int nextId() {
        m_id = m_id % 0xFFF000 + 1;
        return m_id;
    }

    void sendNote(uint16_t type, const Note &n, double t, ControlContext &ctx, int renameFrom = 0, int phase = -1) {
        Command c;
        c.type = type;
        c.id = n.id;
        c.t = t;
        c.i[0] = n.col;
        c.i[1] = (n.first ? n.stack : 0) | n.row << 4 |
                 ((renameFrom ? kRename : 0) | (n.key ? kKey : 0) | (type == Command::On && n.fresh ? kFresh : 0)) << 8;
        c.i[2] = renameFrom;
        c.i[3] = phase;
        c.f[0] = n.vel;
        // A hand-over carries on a sound already heard: recorded, but not
        // new playing for Space to find.
        if (renameFrom)
            c.flags = Command::Continue;
        // Which of a two-bar phrase this bar was, so a loop rolls chaos's
        // dice the way the take did, pass after pass.
        if (type == Command::On)
        {
            const double beat = ctx.beatAt(t);
            c.f[1] = 1.0f + float(int(std::floor((beat + 0.1) / kBeatsPerBar)) & 1);
            // And where in its bar it sounded, for a loop that replays it late.
            const double in = beat + 1e-6 - std::floor((beat + 1e-6) / kBeatsPerBar) * kBeatsPerBar;
            // A note landed just before a bar line belongs to that bar.
            c.f[2] = float(in > kBeatsPerBar - 0.125 ? in - kBeatsPerBar : in);
        }
        ctx.send(c);
    }

    void sendState(double t, ControlContext &ctx, bool onset) {
        Command c;
        c.type = kState;
        c.id = kStateId;
        // Settings are not onsets: Space looks for those.
        // With a note's landing it is flagged to be pulled to the grid with
        // the note (so a loop never starts the note with the settings from
        // before) and as carrying on, not new playing, so Space ignores it.
        c.t = t;
        c.flags = onset ? Command::Quantize | Command::Continue : 0;
        const Settings out = wanted();
        out.pack(c);
        ctx.send(c);
        m_sent = out;
        m_sentAt = t;
    }

    // What the engine should have: the settings, but while a sound is held
    // back, the sound (and its colour) the engine already plays.
    Settings wanted() const {
        Settings out = m_s;
        if (m_holdUntil >= 0) {
            out.sound = m_heldSound;
            out.color = m_heldColor;
        }
        return out;
    }
    // Shift and a letter lands on the second row's sound, and the same
    // again within 0.3 s goes on to the third's: the page shows the second
    // at once, but the engine keeps what it played until the moment has
    // passed (or a note lands), so a double press is never heard passing
    // through the second.
    void holdSound(double t) {
        if (m_holdUntil < 0) {
            m_heldSound = m_sent.sound;
            m_heldColor = m_sent.color;
        }
        m_holdUntil = t + 0.3;
    }

    // The settings go to the engine whenever they change, at most sixty
    // times a second while a finger slides, at once when a choice is made.
    void flush(double t, ControlContext &ctx, bool now) {
        if (m_holdUntil >= 0 && t >= m_holdUntil) {
            m_holdUntil = -1;
            now = true;
        }
        if (!wanted().near(m_sent) && (now || t - m_sentAt >= 1.0 / 60.0))
            sendState(t, ctx, false);
        const bool maskChanged = m_overlayMask != m_overlaySentMask;
        if (maskChanged || (m_overlayMask && !m_ov.near(m_ovSent) && (now || t - m_ovAt >= 1.0 / 60.0))) {
            Command c;
            c.type = kOverlay;
            c.id = kOverlayId;
            c.t = t;
            c.flags = Command::NoRecord;
            m_ov.pack(c);
            c.i[3] = (c.i[3] & 1023) | m_overlayMask << 10;
            ctx.send(c);
            m_ovSent = m_ov;
            m_overlaySentMask = m_overlayMask;
            m_ovAt = t;
        }
    }

    void setSound(int s, ControlContext &ctx) {
        m_holdUntil = -1;
        m_s.sound = s;
        m_world = worldFirst(s, m_world);
        // Each sound starts from the colour it sounds best in.
        m_s.color = soundColor(s);
        ctx.notice(QStringLiteral("Sound: %1").arg(QString::fromUtf8(soundName(s))));
    }

    // The organ's Leslie, slow or fast; the juno's chorus, II or I; the
    // gamelan's tuning, selisir or even.
    void flipSwitch(ControlContext &ctx) {
        if (!hasSwitch(m_s.sound))
            return;
        m_s.alt = !m_s.alt;
        ctx.notice(m_s.sound == Organ  ? (m_s.alt ? QStringLiteral("Leslie fast") : QStringLiteral("Leslie slow"))
                   : m_s.sound == Juno ? (m_s.alt ? QStringLiteral("Chorus I") : QStringLiteral("Chorus II"))
                                       : (m_s.alt ? QStringLiteral("Gamelan even, on the studio's scale")
                                                  : QStringLiteral("Gamelan in selisir, five keys")));
    }

    // What the keys and the band's chips both do, one step at a time.
    void stepSound(int dir, ControlContext &ctx) { setSound((m_s.sound + dir + SoundCount) % SoundCount, ctx); }

    void stepRate(int dir, ControlContext &ctx) {
        m_s.rate = std::clamp(m_s.rate + dir, 0, RateCount - 1);
        ctx.notice(QStringLiteral("Rate %1").arg(QString::fromUtf8(rateName(m_s.rate))));
    }

    void stepRegister(int dir, ControlContext &ctx) {
        m_s.reg = std::clamp(m_s.reg + dir, 0, kRegisters - 1);
        const StudioView &sv = ctx.studio();
        Tuning tuning;
        tuning.set(sv.root, sv.family, sv.brightness, sv.just);
        ctx.notice(QStringLiteral("Register: the grid from %1").arg(noteLabel(tuning, m_s.reg, 0, 0)));
    }

    void stepOctaves(int dir, ControlContext &ctx) {
        m_s.octaves = std::clamp(m_s.octaves + dir, 1, kMaxOctaves);
        ctx.notice(QStringLiteral("%1 octave%2").arg(m_s.octaves).arg(m_s.octaves > 1 ? "s" : ""));
    }

    void stepTone(int dir, ControlContext &ctx) {
        m_s.cutoff = clampf(m_s.cutoff + 0.05f * float(dir), 0.04f, 1.0f);
        ctx.notice(QStringLiteral("Tone %1%").arg(int(std::lround(m_s.cutoff * 100))));
    }

    void stepStack(int dir, double t, ControlContext &ctx) {
        m_s.stack = std::clamp(m_s.stack + dir, 0, kStacks - 1);
        restack(t, ctx);
        ctx.notice(QStringLiteral("First note: %1").arg(QString::fromUtf8(stackName(m_s.stack))));
    }

    void newDice(double t, ControlContext &ctx) {
        m_s.seed = (hash(m_s.seed, uint32_t(t * 1000.0)) & 0xFFFFFF) | 1u;
        ctx.notice(QStringLiteral("New dice"));
    }

    void toggleFollow(ControlContext &ctx) {
        m_s.follow = !m_s.follow;
        ctx.notice(m_s.follow ? QStringLiteral("Following the chords") : QStringLiteral("Playing the notes held"));
    }

    void toggleOverdub(ControlContext &ctx) {
        m_overdub = !m_overdub;
        ctx.overdub(m_overdub);
        ctx.notice(m_overdub ? QStringLiteral("Overdub: what you play joins the loop") : QStringLiteral("Overdub off"));
    }

    // Delete: what is latched lets go, and the latch with it. Notes held
    // by fingers or keys stay; they let go when they lift.
    void letGo(double t, ControlContext &ctx) {
        bool any = false;
        for (const auto &n : m_notes)
            any |= n.used && n.touch < 0 && !n.key;
        if (!m_latch && !any) {
            ctx.notice(QStringLiteral("Nothing latched"));
            return;
        }
        setLatch(false, t, ctx);
        flush(t, ctx, true);
    }

    void setLatch(bool on, double t, ControlContext &ctx) {
        m_latch = on;
        if (!on)
            for (auto &n : m_notes)
                if (n.used && n.touch < 0 && !n.key)
                    release(n, t, ctx);
        ctx.notice(on ? QStringLiteral("Latched: the pattern runs on when you lift") : QStringLiteral("Unlatched"));
    }

    void release(Note &n, double t, ControlContext &ctx) {
        sendNote(Command::Off, n, t, ctx);
        n = Note{};
        renumberFirst(t, ctx);
    }

    // The earliest note held is the one that grows into a chord.
    void renumberFirst(double t, ControlContext &ctx) {
        Note *first = nullptr;
        for (auto &n : m_notes)
            if (n.used && (!first || n.t0 < first->t0))
                first = &n;
        for (auto &n : m_notes) {
            const bool was = n.first;
            n.first = &n == first;
            if (n.used && n.first && !was) {
                // The chord moves to the note that is now first.
                n.stack = m_s.stack;
                n.stack0 = m_s.stack;
                sendNote(Command::Move, n, t, ctx);
            }
        }
    }

    void restack(double t, ControlContext &ctx) {
        for (auto &n : m_notes)
            if (n.used && n.first) {
                n.stack = m_s.stack;
                n.stack0 = m_s.stack;
                sendNote(Command::Move, n, t, ctx);
            }
    }

    void addNote(int touch, int keyCode, int col, int row, float vel, double t, ControlContext &ctx) {
        // Latched notes give way to a fresh hand: landing when nothing is
        // physically held starts a new chord.
        bool physical = false;
        for (auto &n : m_notes)
            physical |= n.used && (n.touch >= 0 || n.key);
        if (m_latch && !physical && touch != kMouseId)
            for (auto &n : m_notes)
                if (n.used)
                    release(n, t, ctx);
        Note *slot = nullptr;
        for (auto &n : m_notes)
            if (!n.used) {
                slot = &n;
                break;
            }
        if (!slot)
            return;
        const bool first = !anyNotes();
        Note &n = *slot;
        n = Note{};
        n.used = true;
        n.touch = touch;
        n.key = keyCode != 0;
        n.keyCode = keyCode;
        n.id = nextId();
        n.col = col;
        n.row = row;
        n.vel = vel;
        n.t0 = t;
        n.stack = m_s.stack;
        n.stack0 = m_s.stack;
        n.first = first;
        n.fresh = first;
        m_holdUntil = -1; // a note played wants the sound picked
        sendState(t, ctx, true);
        sendNote(Command::On, n, t, ctx);
        n.fresh = false;
    }

    void liftNote(Note &n, double t, ControlContext &ctx, bool leaving = false) {
        (void)leaving;
        if (m_latch) {
            n.touch = -1;
            n.key = false;
            n.keyCode = 0;
            return;
        }
        release(n, t, ctx);
    }

    void land(const Touch &t, double time, ControlContext &ctx) {
        if (t.x < kSplit) {
            const float u = t.x / kSplit;
            if (t.y >= kChipsTop) {
                m_s.pattern = std::clamp(int(u * PatternCount), 0, PatternCount - 1);
                holdChip(t.id);
                ctx.notice(QStringLiteral("Pattern: %1").arg(QString::fromUtf8(patternName(m_s.pattern))));
                flush(time, ctx, true);
                return;
            }
            if (t.y <= kChipsLow) {
                const int chip = std::clamp(int(u * kBottomChips), 0, kBottomChips - 1);
                if (chip < kSoundCols) {
                    // The lit organ or juno chip tapped again is its switch.
                    const int sound = soundChip(u, t.y, worldFirst(m_s.sound, m_world));
                    if (sound == m_s.sound && hasSwitch(sound))
                        flipSwitch(ctx);
                    else
                        setSound(sound, ctx);
                } else if (chip == kSoundCols) {
                    toggleFollow(ctx);
                } else {
                    setLatch(!m_latch, time, ctx);
                }
                holdChip(t.id);
                flush(time, ctx, true);
                return;
            }
            const int col = std::clamp(int(u * kColumns), 0, kColumns - 1);
            const int row = gridRow(t.y, -1);
            // The mouse is one finger, so with the latch on it builds the
            // chord a click at a time: each click adds its note to what is
            // latched, and a click on a latched note takes it away (Delete
            // lets go of them all).
            if (t.id == kMouseId && m_latch)
                for (auto &n : m_notes)
                    if (n.used && n.touch < 0 && !n.key && n.col == col && n.row == row) {
                        release(n, time, ctx);
                        holdChip(t.id);
                        flush(time, ctx, true);
                        return;
                    }
            addNote(t.id, 0, col, row, velocityOf(t), time, ctx);
            if (Note *n = noteFor(t.id))
                n->angle0 = t.angle;
            return;
        }
        // The right hand.
        Knob *k = nullptr;
        for (auto &kk : m_knobs)
            if (kk.touch < 0) {
                k = &kk;
                break;
            }
        if (!k)
            return;
        const int col = t.x < kSplit + kPadW ? 0 : 1;
        const int row = std::clamp(int((1.0f - t.y) * 3.0f), 0, 2);
        *k = Knob{};
        k->touch = t.id;
        k->pad = row * 2 + col;
        k->angle0 = t.angle;
        k->base = k->pad == PadRate ? m_s.swing : k->pad == PadTone ? m_s.res : 0;
        const QPointF landed = padLocal(k->pad, t.x, t.y);
        k->u0 = float(landed.x());
        k->v0 = float(landed.y());
        k->keep = k->pad == PadTone && hasSwitch(m_s.sound);
        // No notes under the left hand and a loop playing: this hand plays
        // the loop.
        k->overlay = !anyNotes() && ctx.studio().parts[ctx.part()].playing >= 0;
        if (k->overlay) {
            if (!m_overlayMask)
                m_ov = m_s;
            m_overlayMask |= 1 << k->pad;
        }
        moveKnob(*k, t, time);
    }

    void holdChip(int id) {
        for (int &c : m_chipTouches)
            if (c < 0) {
                c = id;
                return;
            }
    }

    static int gridRow(float y, int current) {
        const float v = (y - kChipsLow) / (kChipsTop - kChipsLow);
        if (current == 0 && v < 0.58f)
            return 0;
        if (current == 1 && v > 0.42f)
            return 1;
        return v < 0.5f ? 0 : 1;
    }

    static float velocityOf(const Touch &t) { return std::clamp(0.45f + 0.75f * t.pressure, 0.3f, 1.0f); }

    void moveNote(Note &n, const Touch &t, double time, ControlContext &ctx) {
        bool changed = false;
        // Sliding sideways walks the note along the scale; a finger must go
        // well into the next column before it changes.
        const float u = t.x / kSplit * kColumns;
        if (u < float(n.col) - 0.2f || u > float(n.col + 1) + 0.2f) {
            const int col = std::clamp(int(u), 0, kColumns - 1);
            if (col != n.col) {
                n.col = col;
                changed = true;
            }
        }
        const int row = gridRow(std::clamp(t.y, kChipsLow, kChipsTop), n.row);
        if (row != n.row) {
            n.row = row;
            changed = true;
        }
        // Twisting the first finger clockwise grows its chord.
        if (n.first && !m_s.follow && t.eccentricity > 0.2f) {
            const float turn = wrapAngle(t.angle - n.angle0);
            const int stack = std::clamp(n.stack0 - int(std::lround(turn / (25.0f * kPi / 180.0f))), 0, kStacks - 1);
            if (stack != n.stack) {
                n.stack = stack;
                changed = true;
            }
        }
        const float vel = velocityOf(t);
        if (std::fabs(vel - n.vel) > 0.04f && time - n.lastMove > 1.0 / 30.0) {
            n.vel = vel;
            changed = true;
        }
        if (changed) {
            n.lastMove = time;
            sendNote(Command::Move, n, time, ctx);
        }
    }

    void spend(Note &n, double t, ControlContext &ctx) {
        if (n.touch >= 0)
            for (int &s : m_spentTouches)
                if (s < 0) {
                    s = n.touch;
                    break;
                }
        n = Note{};
        renumberFirst(t, ctx);
    }

    bool pressing() const {
        for (const auto &k : m_knobs)
            if (k.touch >= 0 && k.pressing)
                return true;
        return false;
    }

    void moveKnob(Knob &k, const Touch &t, double time) {
        const QPointF uv = padLocal(k.pad, t.x, t.y);
        const float u = float(uv.x()), v = float(uv.y());
        const float turn = t.eccentricity > 0.2f ? wrapAngle(t.angle - k.angle0) : 0.0f;
        // Pressing is well above a normal touch (about 0.5), and once in it
        // stays until the finger eases clearly off: rolls, bite and throws
        // are things you mean.
        const bool wasPressing = k.pressing;
        if (!k.pressing && t.pressure > 0.78f)
            k.pressing = true;
        else if (k.pressing && t.pressure < 0.7f)
            k.pressing = false;
        const float press = k.pressing ? clampf((t.pressure - 0.7f) / 0.25f, 0, 1) : 0.0f;
        if (k.pressing)
            m_lastPress = time;
        auto both = [&](auto &&fn) {
            fn(m_s);
            if (k.overlay)
                fn(m_ov);
        };
        switch (k.pad) {
        case PadRate: {
            const int rate = zone(v, RateCount, m_s.rate);
            const float gate = 0.06f + 0.94f * u;
            const float swing = clampf(k.base - turn / (45.0f * kPi / 180.0f), 0, 1);
            both([&](Settings &s) {
                s.rate = rate;
                s.gate = gate;
                s.swing = swing;
            });
            break;
        }
        case PadRhythm: {
            const int n = stepsPerBar(m_s.rate);
            const int hits = 1 + int(std::lround(u * float(n - 1)));
            const int rot = std::clamp(int(v * float(n)), 0, n - 1);
            both([&](Settings &s) {
                s.density = float(hits) / float(n);
                s.rotate = rot;
                s.ratchet = press;
            });
            break;
        }
        case PadRange: {
            const int reg = zone(u, kRegisters, m_s.reg);
            const int oct = zone(v, kMaxOctaves, m_s.octaves - 1) + 1;
            both([&](Settings &s) {
                s.reg = reg;
                s.octaves = oct;
            });
            break;
        }
        case PadTone: {
            const float res = clampf(k.base - turn / (50.0f * kPi / 180.0f), 0, 1);
            // On the organ and the juno a press is a switch, not a bite:
            // pressing in flips the Leslie between slow and fast (the
            // half-moon lever under an organist's hand) or the chorus
            // between II and I, and lifting leaves it where it is.
            const bool flip = hasSwitch(m_s.sound) && k.pressing && !wasPressing;
            if (k.keep && std::hypot(u - k.u0, v - k.v0) > 0.05f)
                k.keep = false;
            both([&](Settings &s) {
                if (!k.keep) {
                    s.cutoff = 0.04f + 0.96f * v;
                    s.color = u;
                }
                s.res = res;
                s.envBoost = hasSwitch(s.sound) ? 0.0f : press;
                if (flip)
                    s.alt = !s.alt;
            });
            break;
        }
        case PadGroove: {
            const int acc = zone(u, kAccentChoices, m_s.accent);
            both([&](Settings &s) {
                s.accent = acc;
                s.chaos = v;
                s.accentDepth = 0.5f + 0.5f * press;
            });
            break;
        }
        case PadEcho: {
            const int time = zone(u, kEchoTimes, m_s.echoTime);
            both([&](Settings &s) {
                s.echoTime = time;
                s.echo = v;
                s.throwFb = press;
            });
            break;
        }
        default:
            break;
        }
    }

    void liftKnob(Knob &k) {
        // Presses are momentary: the rolls, the bite and the throw end when
        // the finger lifts. Everything else stays where it was left.
        switch (k.pad) {
        case PadRhythm:
            m_s.ratchet = 0;
            break;
        case PadTone:
            m_s.envBoost = 0;
            break;
        case PadGroove:
            m_s.accentDepth = 0.5f;
            break;
        case PadEcho:
            m_s.throwFb = 0;
            break;
        default:
            break;
        }
        const bool overlay = k.overlay;
        k = Knob{};
        if (overlay) {
            bool still = false;
            for (auto &o : m_knobs)
                still |= o.touch >= 0 && o.overlay;
            if (!still)
                m_overlayMask = 0;
        }
    }

    QString knobLabel(int pad) const {
        switch (pad) {
        case PadRate:
            return QStringLiteral("%1 · %2%").arg(QString::fromUtf8(rateName(m_s.rate))).arg(int(m_s.gate * 100));
        case PadRhythm:
            return QStringLiteral("%1/%2").arg(hitsFor(m_s.density, stepsPerBar(m_s.rate))).arg(stepsPerBar(m_s.rate));
        case PadRange:
            return QStringLiteral("%1 OCT").arg(m_s.octaves);
        case PadTone:
            return QStringLiteral("CUT %1").arg(int(m_s.cutoff * 100));
        case PadGroove:
            return accentCycle(m_s.accent) ? QStringLiteral("ACC %1").arg(accentCycle(m_s.accent))
                                           : QStringLiteral("CHAOS %1").arg(int(m_s.chaos * 100));
        case PadEcho:
            return QStringLiteral("%1 · %2%").arg(QString::fromUtf8(echoName(m_s.echoTime))).arg(int(m_s.echo * 100));
        default:
            return {};
        }
    }

    // How a waiting take reads: its length and how soon it starts.
    static QString takeText(const PartView &v, double beat) {
        const int slot = v.recording + 1;
        if (v.takeBeats <= 0)
            return QStringLiteral("INTO SLOT %1 · LENGTH OPEN UNTIL IT CLOSES").arg(slot);
        const int bars = int(std::lround(v.takeBeats / kBeatsPerBar));
        const QString len = bars >= 1 ? QStringLiteral("%1 BAR%2").arg(bars).arg(bars > 1 ? "S" : "")
                                      : QStringLiteral("%1 BEATS").arg(double(v.takeBeats));
        const double left = v.takeAt - beat;
        return left > 0.05 ? QStringLiteral("%1 INTO SLOT %2 · IN %3 BEATS").arg(len).arg(slot).arg(left, 0, 'f', 1)
                           : QStringLiteral("%1 INTO SLOT %2 · NOW").arg(len).arg(slot);
    }

    // A take begins: every live note ends at its first beat, fingers still
    // down step aside until they lift (a new landing starts a fresh layer),
    // and a latch lets go.
    void handToTake(double t, ControlContext &ctx, bool atOnce = false) {
        m_takeHanded = true;
        // A loop that sets the clock starts its ONE where Space was pressed,
        // which can sit just after a live step on the old grid: that step
        // fades at once rather than ring against the loop's first note.
        if (atOnce) {
            Command cut;
            cut.type = kCut;
            cut.flags = Command::NoRecord;
            cut.id = kStateId;
            ctx.send(cut);
        }
        bool any = false;
        for (auto &n : m_notes)
            if (n.used) {
                sendNote(Command::Off, n, t, ctx);
                spend(n, t, ctx);
                any = true;
            }
        // The loop plays what the latch held, so the latch lets go: said
        // here, and on the LATCH chip for a few seconds, so the pattern that
        // runs on is plainly the loop's and a click isn't needed to stop it.
        if (m_latch) {
            const int slot = m_take >= 0 ? m_take : ctx.studio().parts[ctx.part()].playing;
            ctx.notice(any ? QStringLiteral("The loop in slot %1 plays what was latched; the latch let go").arg(slot + 1)
                           : QStringLiteral("Unlatched: the loop in slot %1 plays on").arg(slot + 1));
            m_latchToLoopAt = t;
            m_latchToLoopSlot = slot;
        }
        (void)any;
        m_latch = false;
    }

    // A bar line passed with notes held: each takes a fresh id from that
    // line, with the settings of the moment and where the pattern was.
    void handOver(int64_t bar, int phase, ControlContext &ctx) {
        const double t = ctx.timeAt(double(bar) * kBeatsPerBar);
        sendState(t, ctx, false);
        // In the order they were played, so a loop keeps that order.
        Note *order[24];
        int count = 0;
        for (auto &n : m_notes)
            if (n.used && n.t0 < t)
                order[count++] = &n;
        std::sort(order, order + count, [](const Note *a, const Note *b) { return a->t0 < b->t0; });
        for (int i = 0; i < count; ++i) {
            Note &n = *order[i];
            const int old = n.id;
            n.id = nextId();
            sendNote(Command::On, n, t, ctx, old, phase);
            // The old id ends here, so the studio knows it is not held into
            // a take beside its new one (the engine already moved it over).
            Command off;
            off.type = Command::Off;
            off.flags = Command::Continue;
            off.id = old;
            off.t = t;
            ctx.send(off);
        }
    }

    void paintGrid(QPainter &p, const PaintContext &pc);
    void paintChips(QPainter &p, const PaintContext &pc);
    void paintPads(QPainter &p, const PaintContext &pc);
    void paintFingers(QPainter &p, const PaintContext &pc);
    void paintStrip(QPainter &p, const PaintContext &pc);

    Settings m_s, m_sent;
    double m_holdUntil = -1;          // a second-row sound held back from the engine until then
    int m_heldSound = Pluck;          // what the engine plays meanwhile
    float m_heldColor = 0.3f;
    double m_sentAt = -1;
    Settings m_ov, m_ovSent;
    int m_overlayMask = 0, m_overlaySentMask = 0;
    double m_ovAt = -1;
    std::array<Note, 24> m_notes{};
    std::array<int, 16> m_spentTouches{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<Knob, 10> m_knobs{};
    std::array<int, 10> m_chipTouches{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    bool m_latch = false;
    int m_world = 0;                  // where the lower row of sounds stood when last a sound there was lit
    bool m_overdub = false;           // Shift+Return or the OVERDUB chip: what is played joins the loop
    console::Hotspots m_spots;        // what paint() drew to be clicked
    bool m_running = false;           // the arp plays, live or from its loop
    double m_latchToLoopAt = -1e9;    // when a landing loop took the latch over
    int m_latchToLoopSlot = -1;
    bool m_button = false, m_buttonIgnored = false;
    double m_lastPress = -1;
    int m_take = -1;
    bool m_takeHanded = false;
    uint32_t m_takesSeen = UINT32_MAX;
    PartView m_takeView;
    double m_takeBeat = 0;
    int64_t m_handedBar = INT64_MIN;
    int m_id = 0;
    int m_perOct = 7;
    Frame m_frame;
};

// --- painting -------------------------------------------------------------------

void ArpControl::paintGrid(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const StudioView &sv = *pc.studio;
    Tuning tuning;
    tuning.set(sv.root, sv.family, sv.brightness, sv.just);
    const int perOct = tuning.degreesPerOctave();
    const float *data = pc.view->data;
    const float rowH = (kChipsTop - kChipsLow) / 2.0f;
    // Following the chords, the chord's own notes are tinted.
    bool inChord[32] = {};
    if (m_s.follow) {
        const bool relative = sv.chordTones[0] != sv.chordRoot;
        for (int d = 0; d < 32; ++d)
            for (int k = 0; k < std::clamp(sv.chordCount, 1, 4); ++k)
                if (floorModI(d - (sv.chordTones[k] + (relative ? sv.chordRoot : 0)), perOct) == 0)
                    inChord[d] = true;
    }
    for (int row = 0; row < 2; ++row) {
        for (int k = 0; k < kColumns; ++k) {
            const float x0 = kSplit * float(k) / kColumns, x1 = kSplit * float(k + 1) / kColumns;
            const float y0 = kChipsLow + rowH * float(row), y1 = y0 + rowH;
            const QRectF cell = pc.rect(x0, y0, x1, y1).adjusted(1, 1, -1, -1);
            const int degree = k + row * perOct;
            if (floorModI(k, perOct) == 0)
                p.fillRect(cell, c::alpha(col, 0.07f));
            if (m_s.follow && inChord[std::clamp(degree, 0, 31)])
                p.fillRect(cell, c::alpha(c::cyan(), 0.1f));
            for (const auto &n : m_notes)
                if (n.used && n.col == k && n.row == row) {
                    p.fillRect(cell, c::alpha(col, n.touch >= 0 || n.key ? 0.32f : 0.2f));
                    if (n.first && !m_s.follow)
                        c::text(p, QRectF(cell.left() + 5, cell.top() + 5, cell.width() - 8, 14),
                                QString::fromUtf8(stackName(n.stack)), col, 10, true, Qt::AlignLeft | Qt::AlignTop);
                    if (n.touch < 0 && !n.key)
                        c::text(p, QRectF(cell.left() + 5, cell.top() + 19, cell.width() - 8, 14),
                                QStringLiteral("LATCHED"), c::amber(), 9, true, Qt::AlignLeft | Qt::AlignTop);
                }
            // The arp lighting the notes as it plays them; the loop in amber.
            const float flash = degree < kFlashCells ? data[kDataFlash + degree] : 0.0f;
            const float clip = degree < kFlashCells ? data[kDataClipFlash + degree] : 0.0f;
            if (flash > 0.02f)
                p.fillRect(cell, c::alpha(col, 0.5f * flash));
            if (clip > 0.02f)
                p.fillRect(QRectF(cell.left(), cell.bottom() - 26, cell.width(), 4), c::alpha(c::amber(), 0.9f * clip));
            c::text(p, QRectF(cell.left(), cell.bottom() - 20, cell.width(), 16), noteLabel(tuning, m_s.reg, row, k),
                    c::alpha(col, 0.65f), 11, true, Qt::AlignCenter);
        }
    }
    p.setPen(QPen(c::alpha(col, 0.18f), 1));
    for (int k = 1; k < kColumns; ++k)
        p.drawLine(pc.at(kSplit * float(k) / kColumns, kChipsLow), pc.at(kSplit * float(k) / kColumns, kChipsTop));
    p.drawLine(pc.at(0, kChipsLow + rowH), pc.at(kSplit, kChipsLow + rowH));
    p.setPen(QPen(c::alpha(col, 0.5f), 1.5));
    p.drawLine(pc.at(0, kChipsLow), pc.at(kSplit, kChipsLow));
    p.drawLine(pc.at(0, kChipsTop), pc.at(kSplit, kChipsTop));
    p.setPen(QPen(col, 2));
    p.drawLine(pc.at(kSplit, 0), pc.at(kSplit, 1));

    // The notes just played, drawn where they sit on the grid and joined in
    // the order they came: the pattern's path through the chord, fading
    // over a beat. Notes above the grid ride its upper edge; the loop's are
    // amber.
    const double now = std::fmod(pc.beat, 64.0);
    const qreal cw = pc.pad.width() * kSplit / kColumns, ch = pc.pad.height() * rowH;
    QPointF last[2];
    bool have[2] = {false, false};
    for (int i = 0; i < kRecent; ++i) {
        const float *o = data + kDataRecent + 4 * i;
        if (o[3] <= 0)
            continue;
        double age = now - double(o[0]);
        if (age < 0)
            age += 64.0;
        if (age > 1.5)
            continue;
        const int lane = o[3] > 1.5f ? 1 : 0;
        const int d = int(std::lround(o[1]));
        int row = d < perOct ? 0 : 1;
        int colIdx = d - row * perOct, above = 0;
        while (colIdx >= kColumns) {
            colIdx -= perOct;
            ++above;
        }
        if (colIdx < 0)
            continue;
        const QPointF centre = pc.at(kSplit * (float(colIdx) + 0.5f) / kColumns, kChipsLow + rowH * (float(row) + 0.5f));
        const QPointF pt = centre + QPointF(0, -std::min(0.42, 0.14 * above) * ch - (lane ? cw * 0.18 : 0));
        const QColor dot = lane ? c::amber() : col;
        const float fade = float(1.0 - age / 1.5);
        if (have[lane]) {
            p.setPen(QPen(c::alpha(dot, 0.55f * fade), 1.5));
            p.drawLine(last[lane], pt);
        }
        const qreal rad = 2.5 + 4.0 * std::fabs(o[2]) + (o[2] < 0 ? 3.0 : 0);
        p.setPen(Qt::NoPen);
        p.setBrush(c::alpha(dot, 0.15f + 0.85f * fade));
        p.drawEllipse(pt, rad, rad);
        if (above > 0) {
            p.setPen(QPen(c::alpha(dot, 0.8f * fade), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(pt, rad + 3.0 * above, rad + 3.0 * above);
        }
        last[lane] = pt;
        have[lane] = true;
    }
}

void ArpControl::paintChips(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    for (int i = 0; i < PatternCount; ++i) {
        const float x0 = kSplit * float(i) / PatternCount, x1 = kSplit * float(i + 1) / PatternCount;
        const QRectF r = pc.rect(x0, kChipsTop, x1, 1.0f).adjusted(4, 4, -4, -5);
        const bool lit = m_s.pattern == i;
        if (lit) {
            p.fillRect(r, col);
        } else {
            p.setPen(QPen(c::alpha(col, 0.55f), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        }
        const QColor ink = lit ? QColor::fromRgb(c::kInk) : col;
        c::text(p, QRectF(r.left(), r.top() + 2, r.width(), 14), QString::fromUtf8(patternShort(i)), ink, 10, true,
                Qt::AlignCenter);
        const QRectF g = r.adjusted(8, 19, -8, -6);
        const QPainterPath path = patternGlyph(i, g);
        if (i == ChordStab) {
            p.setPen(Qt::NoPen);
            p.setBrush(lit ? ink : c::alpha(col, 0.8f));
        } else {
            p.setPen(QPen(lit ? ink : c::alpha(col, 0.8f), 1.4));
            p.setBrush(Qt::NoBrush);
        }
        p.drawPath(path);
    }
    const StudioView &sv = *pc.studio;
    const int first = worldFirst(m_s.sound, m_world);
    // Slots: the top row's five sounds, the lower row's window of five, then
    // follow and latch.
    constexpr int kSlots = 2 * kSoundCols;
    for (int i = 0; i < kSlots + 2; ++i) {
        const int column = i < kSlots ? i % kSoundCols : i - kSoundCols;
        const float x0 = kSplit * float(column) / kBottomChips, x1 = kSplit * float(column + 1) / kBottomChips;
        const float half = 0.5f * kChipsLow;
        const QRectF r = i < kSoundCols ? pc.rect(x0, half, x1, kChipsLow).adjusted(4, 5, -4, -2)
                         : i < kSlots  ? pc.rect(x0, 0.0f, x1, half).adjusted(4, 2, -4, -7)
                                       : pc.rect(x0, 0.0f, x1, kChipsLow).adjusted(4, 5, -4, -7);
        const int sound = i < kSoundCols ? i : i < kSlots ? kSoundCols + first + column : -1;
        // A lower chip at an end of the window with more sounds beyond it.
        const bool more = i >= kSoundCols && i < kSlots &&
                          ((column == 0 && first > 0) || (column == kSoundCols - 1 && first + kSoundCols < kWorld));
        if (more) {
            p.save();
            p.setOpacity(0.3);
        }
        bool lit;
        QString label;
        QColor tint = col;
        if (sound >= 0) {
            lit = m_s.sound == sound;
            label = QString::fromUtf8(soundShort(sound));
        } else if (i == kSlots) {
            lit = m_s.follow;
            tint = c::cyan();
            Tuning tuning;
            tuning.set(sv.root, sv.family, sv.brightness, sv.just);
            const int pcRoot = int(std::lround(tuning.baseMidi() + tuning.degreeSemis(sv.chordRoot))) % 12;
            label = m_s.follow ? QStringLiteral("FOLLOW %1").arg(QString::fromUtf8(Tuning::noteName(pcRoot)))
                               : QStringLiteral("FOLLOW");
        } else {
            lit = m_latch;
            tint = c::amber();
            label = QStringLiteral("LATCH");
            // A loop just took the latched notes over: the chip says where
            // they went, blinking, for a few seconds.
            if (!m_latch && pc.time - m_latchToLoopAt < 4.0 && m_latchToLoopSlot >= 0) {
                label = QStringLiteral("→ LOOP %1").arg(m_latchToLoopSlot + 1);
                lit = std::fmod(pc.time - m_latchToLoopAt, 0.6) < 0.3;
            }
        }
        if (lit) {
            p.fillRect(r, tint);
        } else {
            p.setPen(QPen(c::alpha(tint, 0.55f), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
        }
        const QColor ink = lit ? QColor::fromRgb(c::kInk) : tint;
        if (sound >= 0) {
            // Each sound chip wears its key small in the top corner, so the
            // lower row teaches that Shift reaches it (twice for the third
            // five).
            const QString key = QString(soundShifts(sound), QChar(0x21E7)) + QChar::fromLatin1(soundKey(sound));
            if (lit && hasSwitch(sound)) {
                // The lit organ, juno or gamelan chip is also its switch: it says
                // where it stands and the key that flips it (in place of
                // the key that picks it, which has done its job).
                c::text(p, QRectF(r.left(), r.top() + 2, r.width(), 15), label, ink, 11, true, Qt::AlignCenter);
                c::text(p, QRectF(r.left(), r.bottom() - 12, r.width(), 11),
                        QStringLiteral("%1  /").arg(QString::fromUtf8(switchName(sound, m_s.alt))),
                        c::alpha(ink, 0.75f), 8, true, Qt::AlignCenter);
            } else {
                c::text(p, r.adjusted(0, 4, 0, 0), label, ink, 11, true, Qt::AlignCenter);
                // The lower row's keys (⇧ or ⇧⇧ and the letter) are a size
                // up and stronger than the top row's single letters: they
                // are what teaches the double press.
                const bool lower = soundShifts(sound) > 0;
                c::text(p, QRectF(r.right() - 40, r.top() + 1, 37, lower ? 13 : 11), key,
                        c::alpha(ink, lower ? 0.85f : 0.6f), lower ? 10 : 8, true, Qt::AlignRight | Qt::AlignVCenter);
            }
        } else {
            c::text(p, r, label, ink, 11, true, Qt::AlignCenter);
        }
        if (more)
            p.restore();
    }
}

void ArpControl::paintPads(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const float *data = pc.view->data;
    const Settings &s = m_s;
    Tuning tuning;
    tuning.set(pc.studio->root, pc.studio->family, pc.studio->brightness, pc.studio->just);
    for (int pad = 0; pad < PadCount; ++pad) {
        const QRectF a = padArea(pad);
        const QRectF r = pc.rect(float(a.left()), float(a.top()), float(a.right()), float(a.bottom())).adjusted(6, 6, -6, -6);
        bool touched = false, shaping = false;
        for (const auto &k : m_knobs)
            if (k.touch >= 0 && k.pad == pad) {
                touched = true;
                shaping |= k.overlay;
            }
        const QColor tint = shaping ? c::amber() : col;
        c::button(p, r, QString::fromUtf8(padTitle(pad)), tint, touched);
        const QRectF in = r.adjusted(9, 28, -9, -9);
        // Where a finger would land to keep the value as it is.
        auto marker = [&](float u, float v) {
            const QPointF m(in.left() + in.width() * u, in.bottom() - in.height() * v);
            p.setPen(QPen(tint, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(m, 6, 6);
            p.drawLine(m + QPointF(-11, 0), m + QPointF(-7, 0));
            p.drawLine(m + QPointF(11, 0), m + QPointF(7, 0));
        };
        switch (pad) {
        case PadRate: {
            const qreal h = in.height() / RateCount;
            for (int i = 0; i < RateCount; ++i) {
                const QRectF band(in.left(), in.bottom() - h * (i + 1), in.width(), h - 2);
                if (i == s.rate)
                    p.fillRect(QRectF(band.left(), band.top(), band.width() * s.gate, band.height()),
                               c::alpha(tint, 0.32f));
                c::text(p, band.adjusted(4, 0, 0, 0), QString::fromUtf8(rateName(i)),
                        i == s.rate ? tint : c::alpha(tint, 0.4f), 11, i == s.rate);
            }
            c::text(p, QRectF(r.left(), r.top() + 1, r.width() - 6, 20), QStringLiteral("SWING %1").arg(int(s.swing * 100)),
                    touched ? QColor::fromRgb(c::kInk) : c::alpha(tint, s.swing > 0.01f ? 0.95f : 0.4f), 10, true,
                    Qt::AlignRight | Qt::AlignVCenter);
            marker(s.gate, (float(s.rate) + 0.5f) / RateCount);
            break;
        }
        case PadRhythm: {
            const int n = stepsPerBar(s.rate);
            const int hits = hitsFor(s.density, n);
            const QPointF centre = in.center();
            const qreal rad = std::min(in.width(), in.height()) * 0.44;
            // The live lane's step, or the loop's (in amber) when only it runs.
            const bool live = data[kDataLiveRunning] > 0.5f;
            const bool running = live || data[kDataClipRunning] > 0.5f;
            const int now = int(data[live ? kDataLiveStep : kDataClipStep]);
            const QColor stepInk = live ? tint : c::amber();
            for (int i = 0; i < n; ++i) {
                const double ang = -kPi / 2 + 2 * kPi * i / n;
                const QPointF pt = centre + QPointF(std::cos(ang) * rad, std::sin(ang) * rad);
                const bool hit = euclid(i, hits, n, s.rotate);
                const bool cur = running && i == now;
                const qreal dot = (n > 16 ? 2.2 : 3.4) * (cur ? 1.7 : 1.0);
                const QColor ink = cur ? stepInk : tint;
                p.setPen(QPen(c::alpha(ink, hit || cur ? 1.0f : 0.35f), 1.2));
                p.setBrush(hit || cur ? QBrush(c::alpha(ink, cur ? 1.0f : 0.7f)) : Qt::NoBrush);
                p.drawEllipse(pt, dot, dot);
                if (cur && data[kDataLiveRatchet] > 1.5f) {
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(pt, dot + 4, dot + 4);
                }
            }
            c::text(p, QRectF(centre.x() - 40, centre.y() - 11, 80, 20), QStringLiteral("%1/%2").arg(hits).arg(n), tint,
                    14, true, Qt::AlignCenter);
            c::text(p, QRectF(centre.x() - 40, centre.y() + 7, 80, 14),
                    s.ratchet > 0.01f ? QStringLiteral("ROLLS") : QStringLiteral("ROT %1").arg(s.rotate % n),
                    c::alpha(tint, 0.7f), 10, true, Qt::AlignCenter);
            marker(float(hits - 1) / float(std::max(1, n - 1)), (float(s.rotate % n) + 0.5f) / float(n));
            break;
        }
        case PadRange: {
            const qreal w = in.width() / kRegisters, h = in.height() / kMaxOctaves;
            for (int reg = 0; reg < kRegisters; ++reg)
                for (int o = 0; o < kMaxOctaves; ++o) {
                    const QRectF cell(in.left() + w * reg + 1, in.bottom() - h * (o + 1) + 1, w - 2, h - 2);
                    const bool covered = reg == s.reg && o < s.octaves;
                    const bool reach = reg > s.reg && reg < s.reg + s.octaves && o == 0;
                    p.fillRect(cell, c::alpha(tint, covered ? 0.42f : reach ? 0.16f : 0.06f));
                }
            // The octaves that fit under the top of the range (E7) with the
            // notes held now: the engine stops the climb there.
            int fit = s.octaves;
            {
                const int perOct = tuning.degreesPerOctave();
                int top = INT_MIN;
                for (const auto &n : m_notes)
                    if (n.used) {
                        int st[5] = {0};
                        const int m = n.first && !s.follow ? stackNotes(n.stack, st) : 1;
                        top = std::max(top, n.col + n.row * perOct + st[m - 1]);
                    }
                while (top > INT_MIN && fit > 1 &&
                       tuning.baseMidi() + 12.0 * s.reg + tuning.degreeSemis(top + (fit - 1) * perOct) > kTopMidi)
                    --fit;
            }
            c::text(p, QRectF(r.left(), r.top() + 1, r.width() - 6, 20),
                    fit < s.octaves ? QStringLiteral("%1 OF %2 OCT FIT FROM %3").arg(fit).arg(s.octaves).arg(noteLabel(tuning, s.reg, 0, 0))
                                    : QStringLiteral("%1 OCT FROM %2").arg(s.octaves).arg(noteLabel(tuning, s.reg, 0, 0)),
                    touched ? QColor::fromRgb(c::kInk) : tint, 10, true, Qt::AlignRight | Qt::AlignVCenter);
            marker((float(s.reg) + 0.5f) / kRegisters, (float(s.octaves) - 0.5f) / kMaxOctaves);
            break;
        }
        case PadTone: {
            // A low-pass curve with its resonant bump at the cutoff, the
            // filter envelope breathing under it.
            QPainterPath curve;
            const float fc = s.cutoff, q = s.res;
            for (int i = 0; i <= 48; ++i) {
                const float x = float(i) / 48.0f;
                const float dd = x - fc;
                float y = dd < 0 ? 0.62f : 0.62f - 2.4f * dd;
                y += q * 0.4f * std::exp(-dd * dd / 0.0025f);
                const QPointF pt(in.left() + in.width() * x, in.bottom() - in.height() * clampf(y, 0, 1));
                if (i == 0)
                    curve.moveTo(pt);
                else
                    curve.lineTo(pt);
            }
            p.setPen(QPen(c::alpha(tint, 0.85f), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(curve);
            const float env = clampf(data[kDataEnv], 0, 1);
            if (env > 0.01f)
                p.fillRect(QRectF(in.left(), in.bottom() - 3, in.width() * env, 3), c::alpha(tint, 0.7f));
            // The log drum's twist is its glide, in milliseconds, as the
            // voice works it out.
            QString title =
                s.sound == LogDrum
                    ? QStringLiteral("LOGDRUM · GLIDE %1 MS").arg(int(std::lround(1000.0f * logGlideSec(s.res))))
                : hasSwitch(s.sound)
                    ? QStringLiteral("%1 · %2 · RES %3")
                          .arg(QString::fromUtf8(soundShort(s.sound)), QString::fromUtf8(switchName(s.sound, s.alt)))
                          .arg(int(s.res * 100))
                    : QStringLiteral("%1 · RES %2").arg(QString::fromUtf8(soundShort(s.sound))).arg(int(s.res * 100));
            // Too long to sit beside the pad's name (the gamelan in selisir):
            // the switch alone says enough, the lit chip names the sound.
            if (title.size() > 22 && hasSwitch(s.sound))
                title = QStringLiteral("%1 · RES %2").arg(QString::fromUtf8(switchName(s.sound, s.alt))).arg(int(s.res * 100));
            c::text(p, QRectF(r.left(), r.top() + 1, r.width() - 6, 20), title,
                    touched ? QColor::fromRgb(c::kInk) : c::alpha(tint, 0.85f), 10, true,
                    Qt::AlignRight | Qt::AlignVCenter);
            marker(s.color, (s.cutoff - 0.04f) / 0.96f);
            break;
        }
        case PadGroove: {
            const qreal w = in.width() / kAccentChoices;
            for (int i = 0; i < kAccentChoices; ++i) {
                const QRectF cell(in.left() + w * i + 1, in.bottom() - 15, w - 2, 15);
                const bool on = i == s.accent;
                if (on)
                    p.fillRect(cell, tint);
                c::text(p, cell, i == 0 ? QStringLiteral("–") : QString::number(accentCycle(i)),
                        on ? QColor::fromRgb(c::kInk) : c::alpha(tint, 0.55f), 10, true, Qt::AlignCenter);
            }
            // The bar to come, step by step, as the groove will play it:
            // the euclidean hits, accents tall, rolls split, chaos's skips
            // missing and its leaps lifted. The dice repeat every two bars,
            // so this is what will sound.
            {
                const int n = stepsPerBar(s.rate);
                const int64_t bar = int64_t(std::floor(pc.beat / kBeatsPerBar));
                const QRectF lane(in.left(), in.top() + 6, in.width(), in.height() - 30);
                const qreal w2 = lane.width() / n;
                uint64_t counter = 0;
                for (int i = 0; i < n; ++i) {
                    const int64_t step = bar * n + i;
                    const StepPlan plan = planStep(s, step, counter, step);
                    const qreal x = lane.left() + w2 * i;
                    const bool now = int64_t(std::floor(pc.beat / stepBeats(s.rate))) == step;
                    if (!plan.fire) {
                        p.fillRect(QRectF(x + w2 * 0.2, lane.bottom() - 2, w2 * 0.6, 2), c::alpha(tint, 0.25f));
                        continue;
                    }
                    ++counter;
                    const qreal h = lane.height() * (plan.accent ? 0.9 : 0.35 + 0.3 * (plan.gain - 0.8) / 0.2);
                    const int r = std::max(1, plan.ratchet);
                    const qreal seg = h / r;
                    for (int k = 0; k < r; ++k)
                        p.fillRect(QRectF(x + w2 * 0.15, lane.bottom() - seg * (k + 1) + (k ? 1.5 : 0), w2 * 0.7,
                                          seg - (k ? 1.5 : 0)),
                                   c::alpha(tint, now ? 1.0f : plan.accent ? 0.85f : 0.55f));
                    if (plan.leap) {
                        p.setPen(Qt::NoPen);
                        p.setBrush(c::alpha(tint, 0.9f));
                        p.drawEllipse(QPointF(x + w2 * 0.5, lane.bottom() - h - 5), 2, 2);
                    }
                }
            }
            c::text(p, QRectF(r.left(), r.top() + 1, r.width() - 6, 20), QStringLiteral("CHAOS %1").arg(int(s.chaos * 100)),
                    touched ? QColor::fromRgb(c::kInk) : c::alpha(tint, s.chaos > 0.01f ? 0.95f : 0.4f), 10, true,
                    Qt::AlignRight | Qt::AlignVCenter);
            marker((float(s.accent) + 0.5f) / kAccentChoices, s.chaos);
            break;
        }
        case PadEcho: {
            // The repeats over the next bar, spaced by the echo's time and
            // fading by its feedback, left and right in turn like the
            // ping-pong itself; the beats ticked along the bottom.
            const float fb = std::min(0.93f, 0.2f + 0.42f * s.echo + 0.4f * s.throwFb);
            const QRectF lane(in.left() + 4, in.top() + 4, in.width() - 8, in.height() - 12);
            p.setPen(QPen(c::alpha(tint, 0.3f), 1));
            for (int b = 0; b <= 4; ++b) {
                const qreal x = lane.left() + lane.width() * b / 4.0;
                p.drawLine(QPointF(x, lane.bottom()), QPointF(x, lane.bottom() + 4));
            }
            p.drawLine(QPointF(lane.left(), lane.center().y()), QPointF(lane.right(), lane.center().y()));
            p.fillRect(QRectF(lane.left() - 1, lane.top(), 3, lane.height()), c::alpha(tint, 0.8f));
            float lvl = 0.9f * s.echo;
            for (int i = 1; i < 24 && lvl > 0.02f; ++i) {
                const qreal x = lane.left() + lane.width() * echoBeats(s.echoTime) * i / 4.0;
                if (x > lane.right())
                    break;
                const qreal h = lane.height() * 0.48 * lvl;
                const qreal y0 = lane.center().y();
                p.fillRect(i % 2 ? QRectF(x - 2, y0 - h, 4, h) : QRectF(x - 2, y0, 4, h), c::alpha(tint, 0.35f + 0.6f * lvl));
                lvl *= fb;
            }
            c::text(p, QRectF(r.left(), r.top() + 1, r.width() - 6, 20),
                    QStringLiteral("%1 · %2").arg(QString::fromUtf8(echoName(s.echoTime))).arg(int(s.echo * 100)),
                    touched ? QColor::fromRgb(c::kInk) : tint, 10, true, Qt::AlignRight | Qt::AlignVCenter);
            marker((float(s.echoTime) + 0.5f) / kEchoTimes, s.echo);
            break;
        }
        default:
            break;
        }
    }
}

void ArpControl::paintFingers(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const StudioView &sv = *pc.studio;
    Tuning tuning;
    tuning.set(sv.root, sv.family, sv.brightness, sv.just);
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        QString label;
        QColor color = pc.color;
        float glow = 0;
        for (const auto &n : m_notes)
            if (n.used && n.touch == t.id) {
                label = noteLabel(tuning, m_s.reg, n.row, n.col);
                glow = 0.6f;
            }
        for (const auto &k : m_knobs)
            if (k.touch == t.id) {
                label = knobLabel(k.pad);
                if (k.overlay)
                    color = c::amber();
            }
        // Near the right edge the tag goes on the finger's left, inside the pad.
        if (t.x > 0.8f && !label.isEmpty()) {
            c::finger(p, pc, t, color, QString(), glow);
            const qreal r = pc.pad.height() / 10.0 * 0.32 * std::clamp(t.size, 0.6f, 4.0f);
            const qreal w = console::textWidth(c::font(12, true), label) + 12;
            const QPointF at = pc.at(t.x, t.y);
            c::tag(p, QPointF(at.x() - r - 6 - w, at.y() - r * 0.6), label, color, 12);
        } else {
            c::finger(p, pc, t, color, label, glow);
        }
    }
}

void ArpControl::paintStrip(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const Settings &s = m_s;
    // The band under the pad. Row 0: the clips, then the sound, the rate
    // and the first note's chord; row 1, in five columns: the register, the
    // octaves, the tone, then dice, overdub and let go. A stepper is its
    // two keys, drawn as keycaps, either side of what they set: the mouse
    // reaches everything a key does and learns the key while it's at it.
    auto column = [&](int row, int i, int span = 1) {
        constexpr double w = 0.19, step = 0.2025;
        return c::bandRow(pc, row, i * step, i * step + w + (span - 1) * step);
    };
    constexpr double kClipsEnd = 0.45, kGap = 0.012, kTopW = (1.0 - kClipsEnd - 3 * kGap) / 3;
    auto top = [&](int i) {
        const double x0 = kClipsEnd + kGap + i * (kTopW + kGap);
        return c::bandRow(pc, 0, x0, x0 + kTopW);
    };
    auto keycap = [&](const QRectF &r, const QString &key, int id) {
        c::chip(p, pc, r, QString(), false, &m_spots, id);
        c::text(p, r, key, col, 15, true, Qt::AlignCenter);
    };
    auto stepper = [&](const QRectF &r, const QString &title, const QString &value, const QString &down,
                       const QString &up, int idDown, float gauge = -1, bool dim = false) {
        const qreal kw = std::clamp<qreal>(r.width() * 0.22, 26, 40);
        keycap(QRectF(r.left(), r.top(), kw, r.height()), down, idDown);
        keycap(QRectF(r.right() - kw, r.top(), kw, r.height()), up, idDown + 1);
        const QRectF mid(r.left() + kw + 2, r.top(), r.width() - 2 * kw - 4, r.height());
        c::text(p, QRectF(mid.left(), mid.top(), mid.width(), 12), title, c::alpha(col, 0.6f), 9, true,
                Qt::AlignCenter);
        c::text(p, QRectF(mid.left(), mid.top() + 10, mid.width(), gauge >= 0 ? 14 : 20), value,
                dim ? c::alpha(col, 0.45f) : col, 12, true, Qt::AlignCenter);
        if (gauge >= 0)
            c::gauge(p, QRectF(mid.left() + 4, mid.bottom() - 6, mid.width() - 8, 5), gauge, col, 10);
    };
    c::clips(p, c::bandRow(pc, 0, 0, kClipsEnd), *pc.view, col, pc.time, &m_spots);
    stepper(top(0), QStringLiteral("SOUND"), QString::fromUtf8(soundShort(s.sound)), QStringLiteral("←"),
            QStringLiteral("→"), SpotSoundDown);
    stepper(top(1), QStringLiteral("RATE"), QString::fromUtf8(rateName(s.rate)), QStringLiteral("⇧←"),
            QStringLiteral("⇧→"), SpotRateDown);
    // Following the chords, the first note plays the chord's notes, not
    // its own stack: the setting waits, dimmed.
    stepper(top(2), QStringLiteral("FIRST NOTE"), QString::fromUtf8(stackName(s.stack)).toUpper(),
            QStringLiteral(","), QStringLiteral("."), SpotStackDown, -1, s.follow);
    Tuning tuning;
    tuning.set(pc.studio->root, pc.studio->family, pc.studio->brightness, pc.studio->just);
    stepper(column(1, 0), QStringLiteral("REGISTER"), QStringLiteral("FROM %1").arg(noteLabel(tuning, s.reg, 0, 0)),
            QStringLiteral("↓"), QStringLiteral("↑"), SpotRegDown);
    stepper(column(1, 1), QStringLiteral("RANGE"), QStringLiteral("%1 OCT").arg(s.octaves), QStringLiteral("⇧↓"),
            QStringLiteral("⇧↑"), SpotOctDown);
    stepper(column(1, 2), QStringLiteral("TONE"), QStringLiteral("%1").arg(int(std::lround(s.cutoff * 100))),
            QStringLiteral("-"), QStringLiteral("+"), SpotToneDown, (s.cutoff - 0.04f) / 0.96f);
    {
        bool latched = m_latch;
        for (const auto &n : m_notes)
            latched |= n.used && n.touch < 0 && !n.key;
        const QRectF r = column(1, 3, 2);
        const qreal w = (r.width() + 4) / 3;
        c::chip(p, pc, QRectF(r.left(), r.top(), w - 4, r.height()), QStringLiteral("DICE  N"), false, &m_spots,
                SpotDice);
        c::chip(p, pc, QRectF(r.left() + w, r.top(), w - 4, r.height()), QStringLiteral("OVERDUB  F10"), m_overdub,
                &m_spots, SpotOverdub);
        // Let go is only worth a lit outline while something is latched.
        if (latched)
            c::chip(p, pc, QRectF(r.left() + 2 * w, r.top(), w - 4, r.height()), QStringLiteral("LET GO  DEL"), false,
                    &m_spots, SpotLetGo);
        else {
            const QRectF cell(r.left() + 2 * w, r.top(), w - 4, r.height());
            p.setPen(QPen(c::alpha(col, 0.25f), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cell);
            c::text(p, cell, QStringLiteral("LET GO  DEL"), c::alpha(col, 0.35f), 12, true, Qt::AlignCenter);
            m_spots.add(cell, SpotLetGo);
        }
    }
}

} // namespace

}

namespace tf {

PartSpec makeArp() {
    PartSpec s;
    s.name = QStringLiteral("Arp");
    s.blurb = QStringLiteral("running notes");
    s.color = QColor("#39FF88");
    s.engine = std::make_unique<arp::Engine>();
    s.control = std::make_unique<arp::ArpControl>();
    // The arp has its own synced echo; the room's echo stays a touch.
    const float channel[ChannelCount] = {0.72f, 0.5f, 0.5f, 0.28f, 0.08f, 0.55f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
