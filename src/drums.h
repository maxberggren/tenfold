#pragma once

// The drum machine behind the Drums page: sixteen synthesised drums in five
// kits, a small room of their own, and the grooves a held hand plays. The
// page (drums.cpp) turns fingers into hits; this header is everything that
// makes sound or decides rhythm, so the tests can reach it directly.
//
// Every sound is built from oscillators, noise and filters at audio rate:
//
//   kick    a sine falling from several times its note onto it (the punch),
//           a short band of noise for the beater, a soft clipper for weight;
//           tuned to the key.
//   808     the kick's long cousin: held, it sustains and glides between
//           scale degrees, the trap bass.
//   snare   two tuned body modes with a pitch drop, and a band of noise
//           whose length is the snare's snap.
//   clap    four bursts of band-passed noise ten milliseconds apart, then a
//           tail that is different in each ear.
//   hats    six square waves at the classic inharmonic ratios (band-limited,
//           so nothing folds back), band- and high-passed, with some noise
//           for crispness. A closed hat chokes an open one.
//   toms    pitched sines with a drop, tuned to the root, third and fifth.
//   ride    seven inharmonic partials (more of the low ones toward the bell)
//           over a wash of high noise.
//   crash   twelve squares, two detuned clusters for width, through a
//           low-pass that closes as it decays.
//   and a rim, a shaker, a conga (a woodblock tok in techno, a tambourine
//   in breaks), a cowbell tuned to the fifth, and a laser zap.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"
#include "electro.h"
#include "part.h"

namespace tf::drums {

// The pads, as laid out on the trackpad: four rows from the player (low
// sounds nearest) to the far edge (cymbals), four across. pad = row * 4 + col.
enum Pad : int {
    Kick, Snare, Clap, Hat,         // row 0: the beat
    Sub, Rim, Shaker, Open,         // row 1: 808, rim, shaker, open hat
    TomLo, TomMid, TomHi, Perc,     // row 2: toms and a hand drum
    Crash, Ride, Bell, Zap,         // row 3: cymbals, cowbell, zap
    PadCount
};

enum Kit : int { House, Techno, Trap, Breaks, Garage, KitCount };

inline const char *padName(int pad) {
    static const char *names[PadCount] = {"kick", "snare", "clap", "hat", "808",   "rim",  "shaker", "open",
                                          "tom lo", "tom mid", "tom hi", "conga", "crash", "ride", "bell", "zap"};
    return names[std::clamp(pad, 0, PadCount - 1)];
}

// The hand drum changes with the kit.
inline const char *padName(int pad, int kit) {
    if (pad == Perc)
        return kit == Techno ? "tok" : kit == Breaks ? "tamb" : kit == Trap ? "bongo" : "conga";
    return padName(pad);
}

inline const char *kitName(int kit) {
    static const char *names[KitCount] = {"house", "techno", "trap", "breaks", "garage"};
    return names[std::clamp(kit, 0, KitCount - 1)];
}

// Each kit's feel, in MPC terms: 50% is straight, 66% a full triplet shuffle.
inline float kitSwing(int kit) {
    static const float swing[KitCount] = {0.54f, 0.5f, 0.5f, 0.56f, 0.62f};
    return swing[std::clamp(kit, 0, KitCount - 1)];
}

// Roughly how long each drum rings in each kit (its main decay's time
// constant, in seconds), for drawing.
inline float displayDecay(int pad, int kit) {
    kit = std::clamp(kit, 0, KitCount - 1);
    static const float table[PadCount][KitCount] = {
        {0.15f, 0.21f, 0.16f, 0.09f, 0.11f},    // kick
        {0.085f, 0.06f, 0.07f, 0.13f, 0.07f},   // snare
        {0.11f, 0.08f, 0.09f, 0.14f, 0.1f},     // clap
        {0.034f, 0.028f, 0.018f, 0.045f, 0.03f}, // hat
        {1.0f, 1.2f, 2.6f, 0.8f, 1.1f},         // 808
        {0.011f, 0.011f, 0.011f, 0.011f, 0.011f}, // rim
        {0.045f, 0.045f, 0.03f, 0.045f, 0.045f}, // shaker
        {0.24f, 0.2f, 0.17f, 0.34f, 0.22f},     // open
        {0.23f, 0.23f, 0.35f, 0.17f, 0.23f},    // tom lo
        {0.2f, 0.2f, 0.3f, 0.15f, 0.2f},        // tom mid
        {0.2f, 0.2f, 0.3f, 0.15f, 0.2f},        // tom hi
        {0.13f, 0.055f, 0.07f, 0.11f, 0.13f},   // hand drum
        {1.1f, 1.1f, 0.8f, 1.3f, 1.1f},         // crash
        {0.8f, 0.8f, 0.56f, 0.8f, 0.8f},        // ride
        {0.15f, 0.09f, 0.15f, 0.15f, 0.15f},    // bell
        {0.13f, 0.13f, 0.13f, 0.13f, 0.13f},    // zap
    };
    return table[std::clamp(pad, 0, PadCount - 1)][kit];
}

// Pads whose pitch is a scale degree, so they follow the key.
inline bool tonal(int pad) {
    return pad == Kick || pad == Sub || pad == TomLo || pad == TomMid || pad == TomHi || pad == Perc || pad == Bell ||
           pad == Zap;
}

// --- the command protocol ---------------------------------------------------
//
// A hit: type Hit, flagged Quantize when a hand played it (so a captured
// loop pulls it to the grid) and not when the page generated it on the grid
// already (a roll, a groove), NoRecord for an audition. Every hit carries
// its whole sound, the kit and the pad's shaping included, so a loop sounds
// the way it was played whatever the page is set to later.
//   i[0] pad   i[1] scale degrees (tonal pads, the pad's tuning included)
//   i[2] kit + 1 (0: the engine's current kit)
//   f[0] velocity 0..1   f[1], f[2] where on the pad it was struck (0..1)
//   f[3] semitones (the other pads, tuning included)
//   f[4] decay, as a power of two   f[5] for the 808: seconds to hold before
//   it lets go (0: a hit)   f[6] tone −1..1
// The 808 played by a finger is held instead: On (the same fields), Move
// (i[1] the degree it glides to), Off.
// Params never go into clips:
//   ParamKit: i[1] the page's kit (the room's size, and hits that name none).
//   ParamEdit: i[1] pad, f[0..2] tune, decay, tone, mirrored for the view.
//   ParamMute: i[1] clip slot, i[2] pad, i[3] 1 to silence that pad in that
//   slot's loop, 0 to bring it back.
constexpr uint16_t Hit = Command::User;
enum ParamKind : int { ParamKit = 0, ParamEdit = 1, ParamMute = 2 };

struct Edit {
    float tune = 0, decay = 0, tone = 0;
    bool any() const { return std::fabs(tune) > 0.01f || std::fabs(decay) > 0.01f || std::fabs(tone) > 0.01f; }
};

// What one hit asks of a voice.
struct Strike {
    int pad = Kick;
    float vel = 0.8f;
    float sx = 0.5f, sy = 0.5f;
    int degrees = 0;
    float semis = 0;
    float decay = 0;
    float tone = 0;
    int kit = -1;   // −1: the engine's
    float hold = 0; // 808 only
};

// The octave (as Harmony::midi counts them) that puts the key's root
// nearest a target frequency: the kick sits near 52 Hz whatever the key.
inline int octaveNear(const Harmony &h, float hz) {
    int best = 0;
    float err = 1e9f;
    for (int o = -2; o <= 5; ++o) {
        const float e = std::fabs(std::log2(midiToHz(h.midi(0, o)) / hz));
        if (e < err) {
            err = e;
            best = o;
        }
    }
    return best;
}

inline float degreeHz(const Harmony &h, int degree, float nearHz) {
    return midiToHz(h.midi(degree, octaveNear(h, nearHz)));
}

// The kick sits on the key's root when the root falls within a few
// semitones of where a kick lives, else on its fifth: every key gets a kick
// between about 43 and 62 Hz that still belongs to it.
inline float kickHz(const Harmony &h, int degrees, float nearHz) {
    const float root = degreeHz(h, 0, nearHz);
    const bool fits = root >= nearHz * 0.83f && root <= nearHz * 1.2f;
    const int base = fits ? 0 : h.tuning.degreesPerOctave() == 7 ? 4 : 3;
    const float hz = midiToHz(h.midi(base, octaveNear(h, nearHz)));
    const float near = hz > nearHz * 1.41f ? hz * 0.5f : hz < nearHz * 0.71f ? hz * 2.0f : hz;
    return near * std::exp2(float(h.tuning.degreeSemis(base + degrees) - h.tuning.degreeSemis(base)) / 12.0f);
}

// --- grooves ----------------------------------------------------------------
//
// A held pad repeats. One finger alone rolls at a rate; fingers that land
// together become a groove, each pad playing its part in the kit's style
// (four on the floor, a backbeat, offbeat hats, euclidean percussion). A
// bar is 96 ticks, so sixteenths (6), triplets (8, 4) and thirty-seconds
// (3) all fall on it.

constexpr int kTicks = 96;

struct Step {
    uint8_t tick = 0;
    float vel = 1;
    int8_t degree = 0; // tuned percussion walks the scale
};

struct Pattern {
    Step steps[96];
    int count = 0;
    void add(int tick, float vel, int degree = 0) {
        if (count < 96)
            steps[count++] = {uint8_t(((tick % kTicks) + kTicks) % kTicks), vel, int8_t(degree)};
    }
    void sort() {
        std::sort(steps, steps + count, [](const Step &a, const Step &b) { return a.tick < b.tick; });
    }
};

// The roll rates, slowest first, in ticks per step.
constexpr int kRates = 7;
inline int rateTicks(int r) {
    static const int ticks[kRates] = {24, 12, 8, 6, 4, 3, 2};
    return ticks[std::clamp(r, 0, kRates - 1)];
}
inline const char *rateName(int r) {
    static const char *names[kRates] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
    return names[std::clamp(r, 0, kRates - 1)];
}
// Where a held pad's roll starts: hats and shakers run in sixteenths, the
// kick in quarters, most things in eighths.
inline int defaultRate(int pad) {
    switch (pad) {
    case Kick:
    case Crash: return 0;
    case Hat:
    case Shaker:
    case Perc:
    case Zap: return 3;
    default: return 1;
    }
}

// Even spread of k onsets over n steps (Bjorklund's result, by the bucket
// method), rotated r steps later.
inline bool euclid(int k, int n, int r, int step) {
    if (n <= 0 || k <= 0)
        return false;
    k = std::min(k, n);
    const int s = (((step - r) % n) + n) % n;
    return ((s * k) % n) < k;
}

// A roll: every step at the rate, the beat a touch stronger than the rest,
// and on average as loud as the hit that began it.
inline Pattern rollPattern(int rate) {
    Pattern p;
    const int step = rateTicks(rate);
    for (int t = 0; t < kTicks; t += step)
        p.add(t, t % 24 == 0 ? 1.04f : t % 12 == 0 ? 1.0f : 0.96f);
    return p;
}

constexpr int kDensities = 5;
inline const char *densityName(int d) {
    static const char *names[kDensities] = {"sparse", "groove", "busy", "roll", "roll x2"};
    return names[std::clamp(d, 0, kDensities - 1)];
}

namespace detail {

// Sixteen steps: X accent, x normal, o ghost, . rest.
inline void addSixteenths(Pattern &p, const char *s, int shift) {
    for (int i = 0; i < 16 && s[i]; ++i) {
        const char c = s[i];
        if (c == '.')
            continue;
        p.add((i + shift) * 6, c == 'X' ? 1.0f : c == 'x' ? 0.72f : 0.42f);
    }
}

// The kit's parts for the pads that have one, at densities 0..2.
inline const char *part(int pad, int kit, int density) {
    const int d = std::clamp(density, 0, 2);
    struct Row {
        const char *p[3];
    };
    // Kick.
    static const Row kick[KitCount] = {
        {{"X.......X.......", "X...X...X...X...", "X...X...X...X..x"}},
        {{"X...X...X...X...", "X...X...X...X...", "X...X...X...X.x."}},
        {{"X...............", "X......X..X.....", "X..x...X..X...x."}},
        {{"X.........X.....", "X.X.......XX....", "X.X.......XX..x."}},
        {{"X...............", "X.........X.....", "X......x..X....."}},
    };
    static const Row snare[KitCount] = {
        {{"............X...", "....X.......X...", "....X.......X.oo"}},
        {{"............X...", "....X.......X...", "....X..o....X.o."}},
        {{"........X.......", "........X.......", "........X.....o."}},
        {{"....X.......X...", "....X..o.o..X..o", "o...X..o.o.oX..o"}},
        {{"............X...", "....X.......X...", "....X.......X..o"}},
    };
    static const Row clap[KitCount] = {
        {{"............X...", "....X.......X...", "....X..o....X..o"}},
        {{"............X...", "....X.......X...", "....X.......X.o."}},
        {{"........X.......", "........X.......", "........X.....x."}},
        {{"....X...........", "....X.......X...", "....X.......X.o."}},
        {{"............X...", "....X.......X...", "....X.......X.x."}},
    };
    static const Row hat[KitCount] = {
        {{"..X...X...X...X.", "o.X.o.X.o.X.o.X.", "oxXooxXooxXooxXx"}},
        {{"..X...X...X...X.", "xoXoxoXoxoXoxoXo", "xxXxxxXxxxXxxxXx"}},
        {{"X.x.X.x.X.x.X.x.", "", ""}}, // trap hats are built below
        {{"X.x.X.x.X.x.X.x.", "X.xoX.x.X.xoX.x.", "XoxoXoxoXoxoXoxo"}},
        {{"..X...X...X...X.", "o.Xoo.X.o.Xoo.X.", "oxXooxXxoxXooxXx"}},
    };
    static const Row open[KitCount] = {
        {{"..............X.", "..X...X...X...X.", "..X...X...X...XX"}},
        {{"..............X.", "..X...X...X...X.", "..X...X...X.x.X."}},
        {{"..............X.", "......X.......X.", "......X...X...X."}},
        {{"..........X.....", "..........X.....", "..X.......X...X."}},
        {{"..X.............", "..X.......X.....", "..X...X...X...X."}},
    };
    static const Row shaker = {{"..X...X...X...X.", "oxXxoxXxoxXxoxXx", "xxXxxxXxxxXxxXXX"}};
    static const Row ride = {{"X...X...X...X...", "X.x.X.x.X.x.X.x.", "X.xxX.xxX.xxX.xx"}};
    static const Row rideTechno = {{"..X...X...X...X.", "x.X.x.X.x.X.x.X.", "xxXxxxXxxxXxxxXx"}};
    static const Row crash = {{"X...............", "X...............", "X.......X......."}};
    static const Row sub[KitCount] = {
        {{"X...............", "..X...X...X...X.", "..X.x.X...X.x.X."}},
        {{"..X...X...X...X.", ".xX..xX..xX..xX.", ".xxx.xxx.xxx.xxx"}},
        {{"X...............", "X......X..X.....", "X..x...X..X...x."}},
        {{"X.........X.....", "X.X.......XX....", "X.X.......XX..x."}},
        {{"X...............", "X.........X..x..", "X......x..X..x.."}},
    };
    switch (pad) {
    case Kick: return kick[kit].p[d];
    case Snare: return snare[kit].p[d];
    case Clap: return clap[kit].p[d];
    case Hat: return hat[kit].p[d];
    case Open: return open[kit].p[d];
    case Shaker: return shaker.p[d];
    case Ride: return kit == Techno ? rideTechno.p[d] : ride.p[d];
    case Crash: return crash.p[d];
    case Sub: return sub[kit].p[d];
    default: return nullptr;
    }
}

}

// The part a pad plays in a groove: its kit's pattern at a density (0
// sparse, 1 the groove, 2 busy, 3 a sixteenth roll, 4 thirty-seconds),
// shifted by a number of sixteenths. Pads without a written part play
// euclidean rhythms, each starting somewhere else so they interlock, and
// the tuned ones walk the chord tones of the scale.
inline Pattern groovePattern(int pad, int kit, int density, int shift) {
    Pattern p;
    kit = std::clamp(kit, 0, KitCount - 1);
    density = std::clamp(density, 0, kDensities - 1);
    if (density >= 3) {
        // Rolls: sixteenths, or thirty-seconds; trap hats roll in triplets.
        const bool trapHat = kit == Trap && (pad == Hat || pad == Shaker);
        const int rate = density == 3 ? (trapHat ? 4 : 3) : 5;
        return rollPattern(rate);
    }
    if (pad == Hat && kit == Trap) {
        // Eighths, with the trap programmer's signature: a triplet burst at
        // the end of the bar, and at the busiest a thirty-second stutter
        // before the snare too.
        for (int t = 0; t < kTicks; t += 12)
            if (density > 0 || t < 72)
                p.add(t + shift * 6, t % 24 == 0 ? 1.0f : 0.7f);
        if (density == 0) {
            for (int t = 72; t < kTicks; t += 12)
                p.add(t + shift * 6, 0.7f);
        } else {
            for (int t = 80; t < kTicks; t += 4)
                if (t % 12 != 0)
                    p.add(t + shift * 6, 0.55f + 0.1f * float(t - 80) / 4.0f);
            if (density == 2)
                for (int t = 39; t < 48; t += 3)
                    p.add(t + shift * 6, 0.5f);
        }
        p.sort();
        return p;
    }
    if (const char *s = detail::part(pad, kit, density)) {
        detail::addSixteenths(p, s, shift);
        p.sort();
        return p;
    }
    // Euclidean: three, five or seven onsets in sixteen.
    static const int pulses[3] = {3, 5, 7};
    int rot = 0;
    switch (pad) {
    case Rim: rot = 4; break;
    case TomLo: rot = 0; break;
    case TomMid: rot = 3; break;
    case TomHi: rot = 6; break;
    case Perc: rot = 2; break;
    case Bell: rot = 5; break;
    case Zap: rot = 7; break;
    default: break;
    }
    // A melody of chord tones for the tuned ones: root, fifth, third, octave.
    static const int walk[4] = {0, 4, 2, 7};
    int n = 0;
    for (int s = 0; s < 16; ++s)
        if (euclid(pulses[density], 16, rot + shift, s)) {
            const bool tuned = pad == Perc || pad == Bell || pad == Zap;
            p.add(s * 6, n == 0 ? 1.0f : 0.75f, tuned ? walk[n % 4] : 0);
            ++n;
        }
    return p;
}

// --- the voices -------------------------------------------------------------

// A sine by rotation: two multiplies a sample, for the ride's partials.
struct Partial {
    float y1 = 0, y2 = 0, c = 0, amp = 0, env = 0, mul = 1;
    void set(float hz, float rate, float a, float tau) {
        const float w = kTwoPi * std::min(hz, rate * 0.45f) / rate;
        c = 2.0f * std::cos(w);
        y1 = std::sin(w);
        y2 = 0;
        amp = a;
        env = 1;
        mul = std::exp(-1.0f / (tau * rate));
    }
    inline float step() {
        const float y = c * y1 - y2;
        y2 = y1;
        y1 = y;
        env *= mul;
        return y2 * amp * env;
    }
};

inline float decayMul(float seconds, float rate) { return std::exp(-1.0f / (std::max(seconds, 0.0005f) * rate)); }

// Everything a voice needs from the engine when it starts.
struct Setup {
    float rate = 48000;
    int kit = House; // when the strike names none
    const Harmony *harmony = nullptr;
};

struct Voice {
    bool active = false;
    int pad = Kick;
    int kit = House;
    int delay = 0;   // samples into the block before it starts
    uint32_t serial = 0;
    float t = 0;     // samples since it started
    float gain = 0, gl = 0, gr = 0, send = 0;
    float fade = 1, fadeMul = 1;
    float level = 0;
    float rate = 48000;

    // Oscillators, envelopes and filters, used as each drum needs them.
    float ph[4] = {};
    float hz[4] = {};
    float env[4] = {}, mul[4] = {};
    float k[8] = {};
    float holdSamples = 0;
    SquareOsc sq[12];
    float sqInc[12] = {};
    Partial partial[7];
    Svf f1, f2, f3, f4, f5, f6;
    Rng rng, rng2;

    // Fades the voice out over a few milliseconds: a choke, or a steal.
    void release(float seconds) {
        fadeMul = std::min(fadeMul, decayMul(seconds, rate));
    }

    void start(const Strike &s, const Setup &su, float rate_, int delaySamples, uint32_t seed) {
        rate = rate_;
        active = true;
        pad = s.pad;
        kit = std::clamp(s.kit >= 0 ? s.kit : su.kit, 0, KitCount - 1);
        delay = delaySamples;
        t = 0;
        fade = 1;
        fadeMul = 1;
        level = 0;
        rng = Rng(seed * 2654435761u + 1u);
        rng2 = Rng(seed * 40503u + 7u);
        for (float &p : ph)
            p = 0;
        for (int i = 0; i < 4; ++i)
            env[i] = 0, mul[i] = 1;
        f1.reset();
        f2.reset();
        f3.reset();
        f4.reset();
        f5.reset();
        f6.reset();
        const Harmony &h = *su.harmony;
        const float vel = clampf(s.vel, 0.02f, 1.0f);
        // Louder hits are louder by more than their velocity, the way a
        // drum responds, but a ghost note still speaks: about 24 dB from
        // the lightest touch to the hardest.
        const float dyn = 0.05f + 0.95f * std::pow(vel, 1.7f);
        const float decayScale = std::exp2(clampf(s.decay, -3.0f, 3.0f));
        const float tone = clampf(s.tone, -1.0f, 1.0f);
        const float ratio = std::exp2(s.semis / 12.0f);
        const int degrees = s.degrees;
        const float edge = clampf((s.sy - 0.5f) * 2.0f, -1.0f, 1.0f); // −1 near edge … +1 far edge
        float pan = 0;
        send = 0;
        auto dm = [&](float seconds) { return decayMul(seconds * decayScale, rate); };

        switch (pad) {
        case Kick: {
            // k: 0 pitch amount, 1 fast pitch amount, 2 drive, 3 click, 4 drive norm
            static const float amt[KitCount] = {5.5f, 7.0f, 4.0f, 4.5f, 5.0f};
            static const float tauP[KitCount] = {0.030f, 0.034f, 0.028f, 0.022f, 0.025f};
            static const float hold[KitCount] = {0.025f, 0.04f, 0.02f, 0.01f, 0.015f};
            static const float tauA[KitCount] = {0.15f, 0.21f, 0.16f, 0.09f, 0.11f};
            static const float drive[KitCount] = {1.8f, 3.6f, 1.5f, 2.4f, 2.0f};
            static const float click[KitCount] = {0.45f, 0.4f, 0.5f, 0.7f, 0.5f};
            static const float clickHz[KitCount] = {3500.0f, 2600.0f, 4200.0f, 2200.0f, 3200.0f};
            hz[0] = kickHz(h, degrees, kit == Trap ? 47.0f : 52.0f);
            k[0] = amt[kit];
            k[1] = 8.0f;
            mul[0] = decayMul(tauP[kit], rate);
            mul[1] = decayMul(0.0035f, rate);
            env[0] = env[1] = 1;
            holdSamples = hold[kit] * decayScale * rate;
            env[2] = 1;
            mul[2] = dm(tauA[kit]);
            k[2] = drive[kit] * std::exp2(tone * 1.2f) * (0.8f + 0.3f * vel);
            k[4] = 1.0f / fastTanh(k[2]);
            k[3] = click[kit] * (1.0f + 0.6f * tone) * (1.0f + 0.4f * std::max(0.0f, edge)) * (0.5f + 0.6f * vel);
            env[3] = 1;
            mul[3] = decayMul(0.0025f, rate);
            f1.set(clickHz[kit], 0.8f, rate);
            // Every kit's kick peaks alike, whatever its drive and sweep.
            static const float level[KitCount] = {1.0f, 1.15f, 0.92f, 1.08f, 1.0f};
            gain = 0.48f * level[kit];
            send = 0.02f;
            break;
        }
        case Sub:
            // The 808 has a voice of its own (Sub808): it glides between notes.
            active = false;
            return;
        case Snare: {
            // k: 0 body level, 1 noise level, 2 click level
            static const float base[KitCount] = {190.0f, 200.0f, 225.0f, 172.0f, 212.0f};
            static const float tauB[KitCount] = {0.05f, 0.04f, 0.035f, 0.07f, 0.045f};
            static const float tauN[KitCount] = {0.085f, 0.06f, 0.07f, 0.13f, 0.07f};
            static const float hp[KitCount] = {1300.0f, 2200.0f, 2800.0f, 900.0f, 1900.0f};
            static const float bp[KitCount] = {4800.0f, 5200.0f, 6500.0f, 3600.0f, 5600.0f};
            static const float noise[KitCount] = {0.85f, 0.75f, 0.95f, 0.8f, 0.9f};
            static const float body[KitCount] = {0.65f, 0.55f, 0.45f, 0.85f, 0.5f};
            hz[0] = base[kit] * ratio * (1.0f + 0.04f * (s.sx - 0.5f)) * (1.0f + 0.06f * std::max(0.0f, edge));
            hz[1] = hz[0] * 1.62f;
            env[0] = 1; // body
            mul[0] = dm(tauB[kit]);
            env[1] = 1; // pitch drop
            mul[1] = decayMul(0.012f, rate);
            env[2] = 1; // noise
            mul[2] = dm(tauN[kit] * (0.85f + 0.3f * vel));
            env[3] = 1; // the crack
            mul[3] = decayMul(0.0012f, rate);
            k[0] = body[kit] * std::exp2(-tone * 0.6f) * (1.0f - 0.35f * std::max(0.0f, edge));
            k[1] = noise[kit] * std::exp2(tone * 0.7f) * (1.0f + 0.25f * std::max(0.0f, edge));
            k[2] = (kit == Trap ? 0.35f : 0.2f) * (0.6f + 0.6f * vel);
            f1.set(hp[kit] * ratio * (1.0f + 0.5f * std::max(0.0f, edge)), 0.7f, rate);
            f2.set(std::min(bp[kit] * ratio, rate * 0.4f), 1.1f, rate);
            f3.set(11000.0f, 0.7f, rate);
            gain = 0.45f;
            send = kit == Breaks ? 0.5f : 0.3f;
            break;
        }
        case Clap: {
            static const float centre[KitCount] = {1200.0f, 1000.0f, 1500.0f, 1100.0f, 1300.0f};
            static const float tail[KitCount] = {0.11f, 0.08f, 0.09f, 0.14f, 0.1f};
            static const float spread[KitCount] = {1.0f, 0.9f, 0.8f, 1.15f, 1.0f};
            // Burst times in samples.
            k[0] = 0;
            k[1] = 0.0085f * spread[kit] * rate;
            k[2] = k[1] + 0.0095f * spread[kit] * rate;
            k[3] = k[2] + 0.011f * spread[kit] * rate;
            k[4] = decayMul(0.0021f, rate); // one burst's fall, per sample
            env[0] = 0;                     // the burst envelope
            env[1] = 1;                     // the tail
            mul[1] = dm(tail[kit] * (1.0f + 0.25f * tone));
            const float c = centre[kit] * ratio * std::exp2(0.35f * edge);
            f1.set(c, 1.4f, rate);
            f2.set(c * 1.04f, 1.4f, rate);
            f3.set(2500.0f * ratio, 0.7f, rate);
            f4.set(2500.0f * ratio, 0.7f, rate);
            f5.set(std::min(12000.0f, rate * 0.4f), 0.6f, rate);
            f6.set(std::min(12000.0f, rate * 0.4f), 0.6f, rate);
            k[5] = 0.35f + 0.25f * tone; // sizzle
            gain = 0.7f;
            send = 0.45f;
            break;
        }
        case Rim: {
            hz[0] = 480.0f * ratio;
            hz[1] = 1720.0f * ratio * (1.0f + 0.05f * edge);
            env[0] = env[1] = env[2] = 1;
            mul[0] = dm(0.011f);
            mul[1] = dm(0.007f);
            mul[2] = decayMul(0.0012f, rate);
            k[0] = 0.55f - 0.2f * tone;
            k[1] = 0.8f + 0.3f * tone;
            f1.set(320.0f, 0.7f, rate);
            f2.set(3500.0f, 1.2f, rate);
            gain = 0.25f;
            pan = -0.2f;
            send = 0.35f;
            break;
        }
        case Shaker: {
            const float attack = (0.004f + 0.012f * (0.5f + 0.5f * edge)) * rate;
            k[0] = attack;
            env[0] = 1;
            mul[0] = dm((kit == Trap ? 0.03f : 0.045f) * (1.0f + 0.3f * tone));
            f1.set(7200.0f * ratio * std::exp2(0.4f * tone), 0.9f, rate);
            f2.set(4500.0f * ratio, 0.7f, rate);
            f3.set(std::min(13000.0f, rate * 0.4f), 0.6f, rate);
            gain = 0.17f;
            pan = 0.3f + 0.3f * (s.sx - 0.5f);
            send = 0.15f;
            break;
        }
        case Hat:
        case Open: {
            static const float ratios[6] = {1.0f, 1.483f, 1.800f, 2.546f, 2.630f, 3.897f};
            static const float tune[KitCount] = {1.0f, 1.12f, 1.25f, 0.92f, 1.05f};
            static const float noise[KitCount] = {0.45f, 0.3f, 0.75f, 0.55f, 0.5f};
            static const float closedTau[KitCount] = {0.034f, 0.028f, 0.018f, 0.045f, 0.03f};
            static const float openTau[KitCount] = {0.24f, 0.2f, 0.17f, 0.34f, 0.22f};
            const float base = 205.3f * 1.7f * tune[kit] * ratio * std::exp2(0.25f * tone);
            for (int i = 0; i < 6; ++i) {
                sq[i].phase = rng.unit();
                sqInc[i] = std::min(base * ratios[i] / rate, 0.45f);
            }
            k[0] = noise[kit] * (1.0f + 0.4f * tone);
            const bool open = pad == Open;
            // Struck toward the far edge, a closed hat loosens and an open one
            // washes longer.
            const float tau = open ? openTau[kit] * (1.0f + 0.4f * std::max(0.0f, edge))
                                   : closedTau[kit] * (0.8f + 0.4f * vel) * (1.0f + 0.9f * std::max(0.0f, edge));
            env[0] = 1;
            mul[0] = dm(tau);
            f1.set(std::min(9000.0f * std::exp2(0.3f * tone), rate * 0.42f), 1.1f, rate);
            f2.set(6800.0f * std::exp2(0.3f * tone), 0.7f, rate);
            f4.set(6000.0f * std::exp2(0.3f * tone), 0.7f, rate);
            f3.set(std::min(13500.0f, rate * 0.42f), 0.7f, rate);
            // Brighter and looser kits are trimmed to sit as far under the kick.
            static const float level[KitCount] = {1.0f, 1.0f, 0.7f, 0.8f, 1.0f};
            gain = (open ? 0.19f : 0.25f) * level[kit];
            pan = (open ? 0.22f : 0.15f) + 0.25f * (s.sx - 0.5f);
            send = open ? 0.12f : 0.08f;
            break;
        }
        case TomLo:
        case TomMid:
        case TomHi: {
            const int deg = pad == TomLo ? 0 : pad == TomMid ? 2 : 4;
            hz[0] = degreeHz(h, deg + degrees, 125.0f);
            if (pad != TomLo && hz[0] < degreeHz(h, degrees, 125.0f))
                hz[0] *= 2.0f;
            env[0] = 1; // amp
            mul[0] = dm((kit == Trap ? 0.3f : kit == Breaks ? 0.15f : 0.2f) * (pad == TomLo ? 1.15f : 1.0f));
            env[1] = 1; // pitch
            mul[1] = decayMul(0.045f, rate);
            k[0] = (kit == Breaks ? 0.55f : 0.75f) * (1.0f + 0.3f * tone) * (1.0f + 0.3f * vel);
            env[2] = 1; // skin noise
            mul[2] = decayMul(kit == Breaks ? 0.03f : 0.008f, rate);
            k[1] = (kit == Breaks ? 0.35f : 0.12f) * (1.0f + 0.5f * std::max(0.0f, edge));
            k[2] = 1.6f + 1.2f * std::max(0.0f, tone);
            k[3] = 1.0f / fastTanh(k[2]);
            f1.set(hz[0] * 6.0f, 0.9f, rate);
            gain = 0.26f;
            pan = pad == TomLo ? -0.3f : pad == TomMid ? 0.0f : 0.3f;
            send = 0.25f;
            break;
        }
        case Perc: {
            hz[0] = degreeHz(h, 4 + degrees, kit == Trap ? 520.0f : 360.0f);
            const float slap = clampf((s.sy - 0.45f) * 2.5f, 0.0f, 1.0f);
            env[0] = 1;
            env[1] = 1;
            env[2] = 1;
            if (kit == Techno) {
                // A woodblock tok: frequency modulation that dies in a few
                // milliseconds, leaving a short hollow tone.
                mul[0] = dm(0.055f);
                mul[1] = decayMul(0.008f, rate);
                k[0] = 2.6f + 1.5f * tone;
                k[1] = 1.41f;
            } else if (kit == Breaks) {
                // A tambourine: jingles over a short wash.
                static const float ratios[6] = {1.0f, 1.37f, 1.73f, 2.21f, 2.67f, 3.11f};
                for (int i = 0; i < 6; ++i) {
                    sq[i].phase = rng.unit();
                    sqInc[i] = std::min(1450.0f * ratios[i] * ratio / rate, 0.45f);
                }
                mul[0] = dm(0.11f * (1.0f + 0.4f * tone));
                f1.set(9500.0f, 1.0f, rate);
                f2.set(5500.0f, 0.7f, rate);
            } else {
                // Conga or bongo: open tone at the centre, a slap at the edge.
                mul[0] = dm((kit == Trap ? 0.07f : 0.13f) * (1.0f - 0.6f * slap) * (1.0f + 0.3f * tone));
                mul[1] = decayMul(0.006f, rate);
                k[0] = 0.3f;
                mul[2] = decayMul(0.012f, rate);
                k[2] = 0.15f + 0.7f * slap;
                f1.set(2400.0f, 1.3f, rate);
            }
            gain = 0.21f;
            pan = -0.35f + 0.3f * (s.sx - 0.5f);
            send = 0.35f;
            break;
        }
        case Crash: {
            static const float ratios[6] = {1.0f, 1.342f, 1.693f, 2.131f, 2.788f, 3.414f};
            const float base = 410.0f * ratio * std::exp2(0.2f * tone);
            for (int i = 0; i < 6; ++i) {
                sq[i].phase = rng.unit();
                sq[i + 6].phase = rng.unit();
                sqInc[i] = std::min(base * ratios[i] / rate, 0.45f);
                sqInc[i + 6] = std::min(base * 1.013f * ratios[i] / rate, 0.45f);
            }
            env[0] = 1; // the hit
            mul[0] = decayMul(0.05f, rate);
            env[1] = 1; // the wash
            mul[1] = dm(kit == Trap ? 0.8f : kit == Breaks ? 1.3f : 1.1f);
            env[2] = 1; // brightness
            mul[2] = dm(0.45f);
            k[0] = 1.0f;
            f1.set(4200.0f, 0.7f, rate);
            f2.set(4200.0f, 0.7f, rate);
            f5.set(3600.0f, 0.7f, rate);
            f6.set(3600.0f, 0.7f, rate);
            gain = 0.36f;
            pan = -0.25f;
            send = 0.12f;
            break;
        }
        case Ride: {
            // The bow: a ping of inharmonic partials up where a ride's
            // stick sound lives, over a wash. Toward the far edge, the bell:
            // the partials drop and ring longer and purer.
            static const float ratios[7] = {1.0f, 1.34f, 1.71f, 2.15f, 2.62f, 3.27f, 4.08f};
            static const float amps[7] = {0.7f, 1.0f, 0.85f, 0.7f, 0.55f, 0.45f, 0.3f};
            static const float taus[7] = {1.1f, 0.95f, 0.8f, 0.65f, 0.55f, 0.45f, 0.35f};
            const float bell = clampf(edge, 0.0f, 1.0f);
            const float base = (kit == Techno ? 1500.0f : 1250.0f) * ratio * std::exp2(0.25f * tone) * (1.0f - 0.5f * bell);
            // No two strokes of a real ride ring quite alike: each hit's
            // partials wander a little, so a run of eighths shimmers
            // instead of droning on one chord.
            for (int i = 0; i < 7; ++i) {
                const float shape = i < 3 ? 1.0f + 1.2f * bell : 1.0f - 0.5f * bell;
                const float wander = 1.0f + 0.018f * rng.bipolar();
                partial[i].set(base * ratios[i] * wander, rate, amps[i] * shape / 6.0f,
                               taus[i] * (0.65f + 1.0f * bell) * decayScale * (kit == Trap ? 0.7f : 1.0f));
            }
            env[0] = 1; // wash
            mul[0] = dm(0.5f);
            env[1] = 1; // stick
            mul[1] = decayMul(0.0015f, rate);
            k[0] = 0.55f * (1.0f - 0.6f * bell) * (1.0f + 0.5f * tone);
            f1.set(6500.0f, 0.7f, rate);
            f2.set(5000.0f, 1.2f, rate);
            f3.set(std::min(14500.0f, rate * 0.4f), 0.6f, rate); // air in the wash, not fizz
            gain = 0.17f;
            pan = 0.3f;
            send = 0.1f;
            break;
        }
        case Bell: {
            // The 808's two squares, here a perfect fifth apart so the bell
            // rings in key.
            hz[0] = degreeHz(h, 4 + degrees, 680.0f);
            sq[0].phase = 0;
            sq[1].phase = 0.25f;
            sqInc[0] = std::min(hz[0] / rate, 0.45f);
            sqInc[1] = std::min(hz[0] * 1.4983f / rate, 0.45f);
            env[0] = 1;
            mul[0] = decayMul(0.01f, rate);
            env[1] = 1;
            mul[1] = dm((kit == Techno ? 0.09f : 0.15f) * (1.0f + 0.3f * edge));
            f1.set(hz[0] * 2.2f * std::exp2(0.4f * tone), 1.4f, rate);
            f2.set(380.0f, 0.7f, rate);
            gain = 0.09f;
            pan = 0.25f + 0.3f * (s.sx - 0.5f);
            send = 0.3f;
            break;
        }
        case Zap: {
            hz[0] = degreeHz(h, degrees, 220.0f);
            k[0] = 10.0f + 16.0f * (0.5f + 0.5f * edge); // sweep depth
            env[0] = 1;
            mul[0] = decayMul(0.03f * (1.0f + 0.4f * tone), rate);
            env[1] = 1;
            mul[1] = dm(0.13f);
            k[1] = 0.6f + 0.4f * tone;
            gain = 0.2f;
            pan = -0.15f + 0.4f * (s.sx - 0.5f);
            send = 0.3f;
            break;
        }
        default:
            active = false;
            return;
        }
        gain *= dyn;
        pan = clampf(pan, -0.9f, 0.9f);
        gl = std::sqrt(0.5f * (1.0f - pan));
        gr = std::sqrt(0.5f * (1.0f + pan));
    }

    // Adds n samples into l, r and the room's send.
    void render(float *l, float *r, float *room, int n) {
        int from = 0;
        if (delay > 0) {
            if (delay >= n) {
                delay -= n;
                return;
            }
            from = delay;
            delay = 0;
        }
        const float inv = 1.0f / rate;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            float sl = 0, sr = 0; // stereo where the drum is wide, else sl only
            bool stereo = false;
            switch (pad) {
            case Kick: {
                env[0] *= mul[0];
                env[1] *= mul[1];
                const float f = hz[0] * (1.0f + k[0] * env[0] + k[1] * env[1]);
                ph[0] += f * inv;
                ph[0] -= std::floor(ph[0]);
                if (t >= holdSamples)
                    env[2] *= mul[2];
                const float body = fastTanh(k[2] * std::sin(kTwoPi * ph[0]) * env[2]) * k[4];
                env[3] *= mul[3];
                const float click = f1.step(rng.bipolar()).band * env[3] * k[3] * 2.5f;
                sl = body + click;
                break;
            }
            case Snare: {
                env[1] *= mul[1];
                const float pitch = 1.0f + 0.5f * env[1];
                ph[0] += hz[0] * pitch * inv;
                ph[1] += hz[1] * pitch * inv;
                ph[0] -= std::floor(ph[0]);
                ph[1] -= std::floor(ph[1]);
                env[0] *= mul[0];
                const float body = (std::sin(kTwoPi * ph[0]) + 0.55f * std::sin(kTwoPi * ph[1])) * env[0] * k[0];
                const float n0 = rng.bipolar();
                const float nz = f3.step(f1.step(n0).high * 0.7f + f2.step(n0).band * 1.3f).low;
                env[2] *= mul[2];
                const float attack = std::min(1.0f, t / (0.0004f * rate));
                env[3] *= mul[3];
                // Tape-like rounding of the noise's tallest peaks: the snare
                // stays as loud but no longer spikes past the kick.
                sl = fastTanh(1.2f * (body + nz * env[2] * attack * k[1] + n0 * env[3] * k[2])) * (1.0f / 1.2f);
                break;
            }
            case Clap: {
                float burst = 0;
                for (int b = 0; b < 4; ++b) {
                    const float since = t - k[b];
                    if (since >= 0 && since < 0.009f * rate)
                        burst = std::max(burst, std::pow(k[4], since));
                }
                const bool tailing = t > k[3];
                if (tailing)
                    env[1] *= mul[1];
                const float nl = rng.bipolar(), nr = rng2.bipolar();
                const float bl = f1.step(nl).band * 2.2f + f3.step(nl).high * k[5];
                const float br = f2.step(nr).band * 2.2f + f4.step(nr).high * k[5];
                const float tail = tailing ? env[1] * 0.55f : 0.0f;
                // A clap is a crowd of transients: rounding the tallest
                // keeps it loud without spiking past the kick.
                // Then a low-pass at 12 kHz: the saturated noise would
                // otherwise fizz up to Nyquist.
                sl = f5.step(fastTanh(1.6f * (bl * burst + bl * tail)) * 0.8f).low;
                sr = f6.step(fastTanh(1.6f * (bl * burst + br * tail)) * 0.8f).low;
                stereo = true;
                break;
            }
            case Rim: {
                ph[0] += hz[0] * inv;
                ph[1] += hz[1] * inv;
                ph[0] -= std::floor(ph[0]);
                ph[1] -= std::floor(ph[1]);
                env[0] *= mul[0];
                env[1] *= mul[1];
                env[2] *= mul[2];
                float v = std::sin(kTwoPi * ph[0]) * env[0] * k[0] + std::sin(kTwoPi * ph[1]) * env[1] * k[1];
                v = f1.step(v).high + f2.step(rng.bipolar()).band * env[2] * 1.5f;
                sl = fastTanh(1.6f * v);
                break;
            }
            case Shaker: {
                const float a = t < k[0] ? (t / k[0]) * (t / k[0]) : 1.0f;
                if (t >= k[0])
                    env[0] *= mul[0];
                const float n0 = rng.bipolar();
                sl = f3.step(f2.step(f1.step(n0).band).high).low * a * env[0] * 2.0f;
                break;
            }
            case Hat:
            case Open: {
                float m = 0;
                for (int q = 0; q < 6; ++q)
                    m += sq[q].step(sqInc[q]);
                m = m * (1.0f / 6.0f) + rng.bipolar() * k[0];
                // Two high-pass stages: the cluster's low partials would ring
                // as a clang, and a hat is all shimmer.
                const float v = f3.step(f4.step(f2.step(f1.step(m).band + 0.4f * m).high).high).low;
                env[0] *= mul[0];
                const float attack = std::min(1.0f, t / (0.0006f * rate));
                sl = v * env[0] * attack * 2.2f;
                break;
            }
            case TomLo:
            case TomMid:
            case TomHi: {
                env[1] *= mul[1];
                ph[0] += hz[0] * (1.0f + k[0] * env[1]) * inv;
                ph[0] -= std::floor(ph[0]);
                env[0] *= mul[0];
                env[2] *= mul[2];
                const float body = fastTanh(k[2] * std::sin(kTwoPi * ph[0]) * env[0]) * k[3];
                sl = body + f1.step(rng.bipolar()).band * env[2] * k[1] * 2.0f;
                break;
            }
            case Perc: {
                env[0] *= mul[0];
                if (kit == Techno) {
                    env[1] *= mul[1];
                    ph[1] += hz[0] * k[1] * inv;
                    ph[1] -= std::floor(ph[1]);
                    ph[0] += hz[0] * inv;
                    ph[0] -= std::floor(ph[0]);
                    sl = std::sin(kTwoPi * ph[0] + k[0] * env[1] * std::sin(kTwoPi * ph[1])) * env[0];
                } else if (kit == Breaks) {
                    float m = 0;
                    for (int q = 0; q < 6; ++q)
                        m += sq[q].step(sqInc[q]);
                    m = m * (1.0f / 6.0f) + 0.5f * rng.bipolar();
                    // Jingles: the wash flutters as the discs settle.
                    const float flutter = 0.65f + 0.35f * std::sin(kTwoPi * 31.0f * t * inv);
                    sl = f2.step(f1.step(m).band).high * env[0] * flutter * 2.4f;
                } else {
                    env[1] *= mul[1];
                    env[2] *= mul[2];
                    ph[0] += hz[0] * (1.0f + k[0] * env[1]) * inv;
                    ph[0] -= std::floor(ph[0]);
                    sl = std::sin(kTwoPi * ph[0]) * env[0] + f1.step(rng.bipolar()).band * env[2] * k[2] * 2.0f;
                }
                break;
            }
            case Crash: {
                float ml = 0, mr = 0;
                for (int q = 0; q < 6; ++q) {
                    ml += sq[q].step(sqInc[q]);
                    mr += sq[q + 6].step(sqInc[q + 6]);
                }
                env[0] *= mul[0];
                env[1] *= mul[1];
                env[2] *= mul[2];
                if ((int(t) & 15) == 0) {
                    const float hz2 = std::min(6000.0f + 7000.0f * env[2], rate * 0.4f);
                    f3.set(hz2, 0.6f, rate);
                    f4.set(hz2, 0.6f, rate);
                }
                const float e = (0.55f * env[0] + 0.45f * env[1]) * std::min(1.0f, t / (0.0015f * rate));
                const float nl = rng.bipolar(), nr = rng2.bipolar();
                sl = f3.step(f5.step(f1.step(ml * 0.12f + nl * 0.55f).high).high).low * e;
                sr = f4.step(f6.step(f2.step(mr * 0.12f + nr * 0.55f).high).high).low * e;
                stereo = true;
                break;
            }
            case Ride: {
                float v = 0;
                for (auto &p : partial)
                    v += p.step();
                env[0] *= mul[0];
                env[1] *= mul[1];
                const float n0 = rng.bipolar();
                v += f3.step(f1.step(n0).high).low * env[0] * k[0] + f2.step(n0).band * env[1] * 0.6f;
                sl = v;
                break;
            }
            case Bell: {
                const float m = sq[0].step(sqInc[0]) + sq[1].step(sqInc[1]);
                env[0] *= mul[0];
                env[1] *= mul[1];
                const float e = 0.6f * env[0] + 0.4f * env[1];
                sl = f2.step(f1.step(m).band).high * e * 1.4f;
                break;
            }
            case Zap: {
                env[0] *= mul[0];
                env[1] *= mul[1];
                ph[0] += hz[0] * (1.0f + k[0] * env[0]) * inv;
                ph[0] -= std::floor(ph[0]);
                sl = std::sin(kTwoPi * ph[0] + k[1] * env[0] * std::sin(kTwoPi * 2.0f * ph[0])) * env[1];
                break;
            }
            default:
                break;
            }
            fade *= fadeMul;
            const float g = gain * fade;
            sl *= g;
            sr = stereo ? sr * g : sl;
            l[i] += sl * gl * 1.4142f;
            r[i] += sr * gr * 1.4142f;
            room[i] += (sl + sr) * 0.5f * send;
            peak = std::max(peak, std::fabs(sl));
            t += 1.0f;
        }
        level = std::max(level * 0.8f, peak);
        if (fade < 1e-3f || (t > 0.03f * rate && level < 1e-4f))
            active = false;
    }
};

// The 808: one voice, a sine with a little punch and drive, that sustains
// while held and glides to each new degree, the way a trap bass slides.
struct Sub808 {
    bool active = false, held = false, attacking = false;
    int id = -1;
    int delay = 0;
    float degree = 0, targetDegree = 0;
    float hz = 55, phase = 0;
    float env = 0, punch = 0, click = 0;
    float vel = 0.8f, autoOff = -1; // samples until a groove's 808 lets go
    float glide = 0;
    float level = 0;
    float rate = 48000;
    int kit = House;
    Edit edit;
    Svf clickBp;
    Rng rng{0x808u};

    // holdSeconds > 0: a hit that lets go by itself; 0: held until off().
    void start(int fingerId, int deg, float velocity, float holdSeconds, int kit_, const Edit &e, int delaySamples,
               float rate_) {
        rate = rate_;
        // A new note while the last still sounds slides into it.
        const bool slide = active && env > 0.05f;
        id = fingerId;
        held = true;
        autoOff = holdSeconds > 0 ? holdSeconds * rate : -1;
        delay = delaySamples;
        targetDegree = float(deg);
        if (!slide)
            degree = targetDegree;
        glide = slide ? 0.06f : 0.0f;
        vel = clampf(velocity, 0.05f, 1.0f);
        kit = kit_;
        edit = e;
        if (!active)
            phase = 0;
        active = true;
        attacking = true;
        punch = slide ? 0.25f : 1.0f;
        click = slide ? 0.0f : 1.0f;
        clickBp.set(2000.0f, 0.8f, rate);
    }
    void off(int fingerId) {
        if (fingerId == id)
            held = false;
    }

    void render(float *l, float *r, int n, const Harmony &h) {
        if (!active)
            return;
        int from = 0;
        if (delay > 0) {
            if (delay >= n) {
                delay -= n;
                return;
            }
            from = delay;
            delay = 0;
        }
        // The pitch is worked out every block from the key, so a held 808
        // follows a key change.
        if (glide > 0)
            degree += (targetDegree - degree) * (1.0f - std::exp(-float(n - from) / (glide * rate)));
        else
            degree = targetDegree;
        const float lo = std::floor(degree);
        const float frac = degree - lo;
        const int deg = int(lo) + int(std::lround(edit.tune));
        const int oct = octaveNear(h, kit == Trap ? 46.0f : 50.0f);
        const float m0 = float(h.midi(deg, oct)), m1 = float(h.midi(deg + 1, oct));
        hz = midiToHz(m0 + (m1 - m0) * frac);
        static const float sustainTau[KitCount] = {1.0f, 1.2f, 2.6f, 0.8f, 1.1f};
        static const float drives[KitCount] = {1.6f, 2.4f, 2.2f, 1.8f, 1.7f};
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float sustain = decayMul(sustainTau[kit] * decayScale, rate);
        const float release = decayMul(0.07f, rate);
        const float drive = drives[kit] * std::exp2(edit.tone * 1.2f);
        const float norm = 1.0f / fastTanh(drive);
        const float punchMul = decayMul(0.04f, rate), clickMul = decayMul(0.003f, rate);
        const float attack = 1.0f - decayMul(0.0015f, rate);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            if (autoOff > 0) {
                autoOff -= 1.0f;
                if (autoOff <= 0)
                    held = false;
            }
            if (attacking) {
                env += (1.0f - env) * attack;
                attacking = env < 0.995f;
            } else {
                env *= held ? sustain : release;
            }
            punch *= punchMul;
            click *= clickMul;
            phase += hz * (1.0f + 1.4f * punch) / rate;
            phase -= std::floor(phase);
            const float s = fastTanh(drive * std::sin(kTwoPi * phase) * env) * norm;
            const float c = clickBp.step(rng.bipolar()).band * click * 0.25f;
            const float y = (s + c) * vel * 0.47f;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!held && !attacking && env < 1e-4f)
            active = false;
    }
};

// A small room of the drums' own, so a snare or a clap gets air without the
// kick and the 808 smearing the studio's big reverb: four delay lines mixed
// by a Householder matrix, damped, after two allpasses that diffuse the
// input. Nothing below 200 Hz goes in.
struct Room {
    Delay lines[4];
    uint32_t len[4] = {};
    float g = 0.5f, mix = 0.25f;
    OnePoleLp damp[4];
    Allpass diff[2];
    Svf hp;
    float rate = 48000;

    void init(float r) {
        rate = r;
        static const float ms[4] = {23.3f, 31.7f, 41.1f, 53.9f};
        for (int i = 0; i < 4; ++i) {
            len[i] = uint32_t(ms[i] * 0.001f * r);
            lines[i].init(size_t(len[i]) + 8);
            damp[i].set(6000.0f, r);
        }
        diff[0].init(uint32_t(0.0047f * r), 0.6f);
        diff[1].init(uint32_t(0.0113f * r), 0.6f);
        hp.set(200.0f, 0.7f, r);
        set(0.45f, 0.25f);
    }
    // seconds of decay to −60 dB; mix is the wet level.
    void set(float t60, float wet) {
        const float meanLen = 0.0375f * rate;
        g = std::pow(10.0f, -3.0f * meanLen / (t60 * rate));
        mix = wet;
    }
    inline void process(float in, float &outL, float &outR) {
        float x = diff[1].step(diff[0].step(hp.step(in).high));
        float y[4];
        for (int i = 0; i < 4; ++i)
            y[i] = damp[i].step(lines[i].tap(len[i]));
        const float sum = 0.5f * (y[0] + y[1] + y[2] + y[3]);
        for (int i = 0; i < 4; ++i)
            lines[i].push((y[i] - sum) * g + x * (i & 1 ? -0.5f : 0.5f));
        outL = (y[0] + y[2]) * mix;
        outR = (y[1] + y[3]) * mix;
    }
};

// --- the engine -------------------------------------------------------------

class Engine : public PartEngine {
public:
    static constexpr int kVoices = 48;
    static constexpr int kPending = 256;

    void init(float rate) override {
        m_rate = rate;
        m_room.init(rate);
        applyKit();
    }

    void command(const Command &c, AudioContext &ctx) override {
        const bool replay = c.id >= (1 << 24) || c.id < 0;
        int delay = 0;
        if (c.t > 0)
            delay = int(std::clamp((c.t - ctx.now()) * double(m_rate), 0.0, double(m_rate)));
        switch (c.type) {
        case Hit:
        case Command::On: {
            const int pad = std::clamp(c.i[0], 0, PadCount - 1);
            const int64_t at = int64_t(m_sample) + delay;
            if (replay && (m_muted[(c.id >> 24) & 7] >> pad & 1u))
                return; // this pad's lane is silenced in the loop that sent it
            if (replay) {
                // A clip that was overdubbed while a hand (or a held
                // groove) played the same pad can bring back what was just
                // played live: the live hit wins.
                const int64_t since = at - m_lastLive[pad];
                if (since > -int64_t(0.005f * m_rate) && since < int64_t(0.06f * m_rate))
                    return;
            } else {
                m_lastLive[pad] = at;
            }
            Pending p;
            p.wait = delay;
            p.cmd = c;
            p.replay = replay;
            push(p, ctx);
            break;
        }
        case Command::Off:
        case Command::Move: {
            Pending p;
            p.wait = delay;
            p.cmd = c;
            push(p, ctx);
            break;
        }
        case Command::Param:
            if (c.i[0] == ParamKit) {
                m_kit = std::clamp(c.i[1], 0, KitCount - 1);
                applyKit();
            } else if (c.i[0] == ParamEdit) {
                const int pad = std::clamp(c.i[1], 0, PadCount - 1);
                m_edit[pad] = {c.f[0], c.f[1], c.f[2]};
            } else if (c.i[0] == ParamMute && c.i[1] >= 0 && c.i[1] < kClipSlots) {
                const uint32_t bit = 1u << std::clamp(c.i[2], 0, PadCount - 1);
                m_muted[c.i[1]] = c.i[3] ? m_muted[c.i[1]] | bit : m_muted[c.i[1]] & ~bit;
            }
            break;
        default:
            break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        // What falls due in this block starts now, at its sample.
        int keep = 0;
        for (int i = 0; i < m_pendingCount; ++i) {
            Pending &p = m_pending[i];
            if (p.wait < n)
                fire(p, p.wait, ctx);
            else {
                p.wait -= n;
                m_pending[keep++] = p;
            }
        }
        m_pendingCount = keep;

        float room[kBlock] = {};
        for (auto &v : m_voices)
            if (v.active)
                v.render(l, r, room, n);
        m_sub.render(l, r, n, ctx.harmony());
        // The trim sits the kit where the studio's master expects a part,
        // with room for the bass and the chords: a full-velocity kick peaks
        // around −10 dBFS before the bus glue.
        constexpr float trim = 0.45f;
        for (int i = 0; i < n; ++i) {
            float rl, rr;
            m_room.process(room[i], rl, rr);
            l[i] = (l[i] + rl) * trim;
            r[i] = (r[i] + rr) * trim;
        }

        // The kick ducks the studio: the studio reads pumps before it
        // renders a block, so a kick due in the next block says so now.
        for (int i = 0; i < m_pendingCount; ++i) {
            Pending &p = m_pending[i];
            if (p.pumped || p.wait >= kBlock)
                continue;
            if (const float s = pumpOf(p.cmd); s > 0) {
                ctx.pump(p.wait, s);
                p.pumped = true;
            }
        }

        const float fall = std::exp(-float(n) / (0.14f * m_rate));
        for (int p = 0; p < PadCount; ++p) {
            m_flash[p] *= fall;
            m_flashReplay[p] *= fall;
        }
        m_sample += uint64_t(n);
    }

    void silence(bool hard) override {
        for (auto &v : m_voices) {
            if (hard)
                v.active = false;
            else if (v.active)
                v.release(0.03f);
        }
        m_sub.held = false;
        if (hard)
            m_sub.active = false;
        m_pendingCount = 0;
    }

    // data[0..15] how hard each pad was just hit (live), [16..31] the same
    // for hits a clip played, [32] the 808's level, [33] its degree, [34]
    // the kit, [35] voices sounding, [40 + 3 * pad ..] each pad's edit
    // (tune, decay, tone) as the engine has it.
    void publish(float *data) override {
        for (int p = 0; p < PadCount; ++p) {
            data[p] = m_flash[p];
            data[16 + p] = m_flashReplay[p];
        }
        data[32] = m_sub.active ? m_sub.level : 0.0f;
        data[33] = m_sub.degree;
        data[34] = float(m_kit);
        data[35] = float(voices());
        for (int p = 0; p < PadCount; ++p) {
            data[40 + 3 * p] = m_edit[p].tune;
            data[41 + 3 * p] = m_edit[p].decay;
            data[42 + 3 * p] = m_edit[p].tone;
        }
    }

    int voices() const override {
        int n = m_sub.active ? 1 : 0;
        for (auto &v : m_voices)
            n += v.active;
        return n;
    }

    // For tests: start a hit at once, as if a command had come.
    void strike(const Strike &s, const Harmony &h) {
        if (s.pad == Sub) {
            m_sub.start(0, s.degrees, s.vel, std::max(0.05f, s.hold), s.kit >= 0 ? s.kit : m_kit,
                        Edit{0, s.decay, s.tone}, 0, m_rate);
            return;
        }
        Setup su{m_rate, m_kit, &h};
        startVoice(s, su, 0);
    }
    int kit() const { return m_kit; }

private:
    struct Pending {
        int wait = 0;
        Command cmd;
        bool replay = false, pumped = false;
    };

    void push(const Pending &p, AudioContext &ctx) {
        if (m_pendingCount >= kPending)
            return;
        Pending q = p;
        // Something due in this very block ducks the studio from here.
        if (q.wait < kBlock)
            if (const float s = pumpOf(q.cmd); s > 0) {
                ctx.pump(q.wait, s);
                q.pumped = true;
            }
        m_pending[m_pendingCount++] = q;
    }

    static float pumpOf(const Command &c) {
        if (c.type != Hit && c.type != Command::On)
            return 0;
        if (c.i[0] == Kick)
            return clampf(0.35f + 0.75f * c.f[0], 0.0f, 1.0f);
        if (c.i[0] == Sub)
            return clampf(0.25f + 0.5f * c.f[0], 0.0f, 0.8f);
        return 0;
    }

    void fire(const Pending &p, int offset, AudioContext &ctx) {
        const Command &c = p.cmd;
        const int pad = std::clamp(c.i[0], 0, PadCount - 1);
        switch (c.type) {
        case Hit:
        case Command::On: {
            Strike s;
            s.pad = pad;
            s.degrees = c.i[1];
            s.vel = c.f[0];
            s.sx = c.f[1];
            s.sy = c.f[2];
            s.semis = c.f[3];
            s.decay = c.f[4];
            s.tone = c.f[6];
            s.kit = c.i[2] > 0 ? std::clamp(c.i[2] - 1, 0, KitCount - 1) : m_kit;
            if (pad == Sub) {
                // A hit on the 808 (a groove's) lets go by itself; an On is
                // held until its Off.
                const float hold = c.type == Hit ? std::max(0.05f, c.f[5]) : 0.0f;
                m_sub.start(c.id, s.degrees, s.vel, hold, s.kit, Edit{0, s.decay, s.tone}, offset, m_rate);
            } else {
                Setup su{m_rate, m_kit, &ctx.harmony()};
                startVoice(s, su, offset);
            }
            float &flash = p.replay ? m_flashReplay[pad] : m_flash[pad];
            flash = std::max(flash, 0.3f + 0.7f * clampf(s.vel, 0, 1));
            break;
        }
        case Command::Move:
            if (m_sub.active && m_sub.id == c.id) {
                m_sub.targetDegree = float(c.i[1]);
                m_sub.glide = 0.05f;
            }
            break;
        case Command::Off:
            m_sub.off(c.id);
            break;
        default:
            break;
        }
    }

    // How many of one drum may ring at once: a kick or a closed hat is one
    // drum, struck again; a crash can bloom over itself a little.
    static int polyphony(int pad) {
        switch (pad) {
        case Kick:
        case Hat:
        case Open: return 1;
        case Crash:
        case Ride: return 3;
        default: return 2;
        }
    }

    void startVoice(const Strike &s, const Setup &su, int offset) {
        // A closed hat chokes the open one.
        if (s.pad == Hat)
            for (auto &v : m_voices)
                if (v.active && v.pad == Open)
                    v.release(0.012f);
        // Past the pad's polyphony, its oldest voice fades out fast.
        int count = 0;
        Voice *oldest = nullptr;
        for (auto &v : m_voices)
            if (v.active && v.pad == s.pad && v.fadeMul >= 1.0f) {
                ++count;
                if (!oldest || v.serial < oldest->serial)
                    oldest = &v;
            }
        if (oldest && count >= polyphony(s.pad))
            oldest->release(s.pad == Kick ? 0.004f : 0.008f);
        Voice *slot = nullptr;
        for (auto &v : m_voices)
            if (!v.active) {
                slot = &v;
                break;
            }
        if (!slot) {
            // Everything is busy: take the quietest.
            slot = &m_voices[0];
            for (auto &v : m_voices)
                if (v.level < slot->level)
                    slot = &v;
        }
        slot->start(s, su, m_rate, offset, ++m_serial);
        slot->serial = m_serial;
    }

    void applyKit() {
        static const float t60[KitCount] = {0.55f, 0.45f, 0.4f, 0.9f, 0.5f};
        static const float wet[KitCount] = {0.22f, 0.18f, 0.14f, 0.32f, 0.22f};
        m_room.set(t60[m_kit], wet[m_kit]);
    }

    float m_rate = 48000;
    int m_kit = House;
    Edit m_edit[PadCount];
    Voice m_voices[kVoices];
    Sub808 m_sub;
    Room m_room;
    Pending m_pending[kPending];
    int m_pendingCount = 0;
    uint64_t m_sample = 0;
    int64_t m_lastLive[PadCount] = {INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2,
                                     INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2,
                                     INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2,
                                     INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2};
    uint32_t m_serial = 0;
    uint32_t m_muted[kClipSlots] = {}; // per clip slot, the pads silenced in its loop
    float m_flash[PadCount] = {}, m_flashReplay[PadCount] = {};
};

}
