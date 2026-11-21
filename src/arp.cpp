// Arp: running notes. The sequencer-arpeggiator at the heart of trance,
// progressive house and synthwave, played with two hands.
//
// The left hand says WHAT: it holds notes on a grid of the scale (two rows,
// an octave apart), and the first note grows into a chord, so one finger is
// already a running triad. Above the grid sit the eight patterns, below it
// the five sounds and the follow and latch switches.
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
#include <algorithm>
#include <climits>
#include <cmath>

#include "console.h"
#include "parts.h"

namespace tf::arp {

// --- settings -----------------------------------------------------------------

namespace {
int q10(float v) { return std::clamp(int(std::lround(v * 1023.0f)), 0, 1023); }
float u10(int v) { return float(v & 1023) / 1023.0f; }
}

void Settings::pack(Command &c) const {
    c.i[0] = (pattern & 15) | (rate & 7) << 4 | ((octaves - 1) & 3) << 7 | (reg & 3) << 9 | (sound & 7) << 11 |
             (echoTime & 7) << 14 | (accent & 15) << 17 | (stack & 7) << 21 | int(follow) << 24 | (rotate & 31) << 25;
    c.i[1] = int(seed & 0xFFFFFF);
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
    sound = std::clamp(a >> 11 & 7, 0, SoundCount - 1);
    echoTime = std::clamp(a >> 14 & 7, 0, kEchoTimes - 1);
    accent = std::clamp(a >> 17 & 15, 0, kAccentChoices - 1);
    stack = std::clamp(a >> 21 & 7, 0, kStacks - 1);
    follow = a >> 24 & 1;
    rotate = a >> 25 & 31;
    seed = uint32_t(c.i[1]) & 0xFFFFFF;
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
constexpr float kOutGain = 1.6f;
constexpr int kSoftVoices = 18; // past this, the oldest notes are faded out to make room

// One arp note. Five sounds, all through the same ladder filter so the
// tone pad means the same thing for each:
//   pluck     two saws a few cents apart (blending to square), a snappy
//             filter envelope: the trance pluck.
//   glass     two-operator FM, the index falling fast: a struck glass tine.
//   supersaw  seven saws in unison spread wide: the anthem arp.
//   pulse     two pulse waves with slow width modulation: synthwave.
//   acid      one saw (blending to square), resonance pushed, accents bite.
struct Voice {
    bool on = false, released = false, fading = false;
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

    struct Start {
        int sound = Pluck;
        float midi = 60, vel = 0.8f, accent = 0, envBoost = 0, color = 0.3f, gateSec = 0.1f, stepSec = 0.12f,
              rate = 48000;
        uint32_t seed = 1;
    };

    void start(const Start &s) {
        on = true;
        released = fading = false;
        sound = s.sound;
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
        default:
            break;
        }
        envOct *= 0.6f + 0.4f * clampf(s.vel, 0, 1);
        envOct += 1.4f * s.accent + 2.2f * s.envBoost;
        attackInc = 1.0f / (attack * s.rate);
        decayMul = mulFor(decay * 0.45f);
        releaseMul = mulFor(release * 0.45f);
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

    // Adds samples [from, to) to the dry and echo buses.
    void render(float *l, float *r, float *el, float *er, int from, int to, float cutHz, float res, float send,
                float rate) {
        const float resonance = clampf(res + resBias, 0.0f, 0.97f);
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
            fenv *= fenvMul;
            if (--coefIn <= 0) {
                if (midiNow != midiTo) {
                    midiNow += (midiTo - midiNow) * glide;
                    if (std::fabs(midiTo - midiNow) < 0.01f)
                        midiNow = midiTo;
                    inc[0] = std::min(0.45f, midiToHz(midiNow) / rate);
                }
                coefIn = 8;
                const float fc = std::min(base * std::exp2(envOct * fenv), rate * (0.42f - 0.1f * resonance));
                fl.set(fc, resonance, rate);
                if (units > 1)
                    fr.set(fc, resonance, rate);
            }
            float sl = 0, sr = 0;
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
            default:
                break;
            }
            float yl, yr;
            if (units > 1) {
                yl = fl.step(sl * 0.7f);
                yr = fr.step(sr * 0.7f);
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
        if (stage == 2 && amp < 2e-4f)
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

struct PoolNote {
    int id = 0;
    int col = 0, row = 0, stack = 0;
    float vel = 0.8f;
    bool released = false, played = false;
};

constexpr int kPool = 24;

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
    Lane lanes[2];
    Settings overlay;
    int overlayMask = 0;
    Settings eff[2];
    Voice voices[kVoices];
    uint64_t age = 0;
    Echo echo;
    Svf hpL, hpR;
    Pending pending[256];
    int pendingCount = 0;
    int64_t barIndex = -1;
    uint64_t barCount = 0;
    float level = 0;
    std::vector<Fired> *log = nullptr; // a ring, sized at init
    uint64_t logCount = 0;
    double expectBeat = -1;
    uint64_t jumpedAt = 0;
    int states = 0;
    Fired recent[kRecent];
    int recentHead = 0;
    uint32_t noteSeed = 0x2545F491u;

    static bool clipId(int id) { return (id & (1 << 30)) != 0; }

    void refreshEff(int lane) {
        eff[lane] = lanes[lane].s;
        if (lane == 1 && overlayMask) {
            Settings &e = eff[1];
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
        const int col = c.i[0], row = c.i[1] >> 4 & 15, stack = c.i[1] & 15, flags = c.i[1] >> 8;
        if (lane == 1 && c.f[1] >= 1.0f) {
            const int parity = int(c.f[1]) - 1;
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
        if (lane == 1 && (flags & kFresh)) {
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
        const bool late = lane == 1 && inBar - double(c.f[2]) > 0.02 && inBar - double(c.f[2]) < kBeatsPerBar;
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
                (phase >= 0 ? uint64_t(phase) : 0) + (lane == 1 ? stepsBefore(L, e, b, double(c.f[2]), !handPlayed) : 0);
            L.freshBeat = b;
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
        n.col = c.i[0];
        n.row = c.i[1] >> 4 & 15;
        n.stack = c.i[1] & 15;
        if (c.f[0] > 0)
            n.vel = clampf(c.f[0], 0.05f, 1.0f);
    }

    void apply(const Command &c, double b) {
        const int lane = clipId(c.id) ? 1 : 0;
        switch (c.type) {
        case kState: {
            ++states;
            Settings s;
            s.unpack(c);
            setSettings(lane, s, b);
            break;
        }
        case kOverlay: {
            const Settings before = eff[1];
            overlay.unpack(c);
            overlayMask = (c.i[3] >> 10) & 63;
            refreshEff(1);
            retime(1, before, b);
            break;
        }
        case kSync:
            // A take just landed: its loop is on the take's own bars unless
            // the clock was reset to make it (then the bars are new ones).
            lanes[1].aligned = rendered - jumpedAt >= uint64_t(rate * 0.25);
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
        const float rollGain = L.sub > 0 ? std::pow(0.8f, float(L.sub)) : 1.0f;
        const int cyc = accentCycle(s.accent);
        const float accentGain =
            L.plan.accent ? 1.0f + 0.4f * s.accentDepth : (cyc > 0 ? 1.0f - 0.22f * s.accentDepth : 1.0f);
        // A stab's notes start together: their peaks add, so each is quieter
        // than its share of the power alone would say.
        const float stack = L.curCount > 1 ? 1.1f * std::pow(float(L.curCount), -0.7f) : 1.0f;
        const int perOct = h.tuning.degreesPerOctave();
        for (int k = 0; k < L.curCount; ++k) {
            double midi = h.midi(L.cur[k], s.reg);
            // Only notes held above the range itself fold down.
            while (midi > kTopMidi + 12.0)
                midi -= 12.0;
            if (tie && s.sound == Acid && L.curCount == 1) {
                Voice *held = nullptr;
                for (Voice &w : voices)
                    if (w.on && !w.released && w.lane == lane && w.sound == Acid)
                        held = &w;
                if (held) {
                    held->slide(float(midi), gateSec, rate);
                    logNote(lane, b, float(midi), held->vel, L.cur[k], L.plan.accent, L, perOct);
                    continue;
                }
            }
            Voice &v = alloc();
            Voice::Start st;
            st.sound = s.sound;
            st.midi = float(midi);
            st.vel = L.curVel[k] * L.plan.gain * accentGain * rollGain * stack;
            st.accent = L.plan.accent ? s.accentDepth : 0.0f;
            st.envBoost = s.envBoost;
            st.color = s.color;
            st.gateSec = gateSec;
            st.stepSec = stepSec;
            st.rate = rate;
            noteSeed = hash(noteSeed, uint32_t(age));
            st.seed = noteSeed;
            v.start(st);
            v.lane = lane;
            v.born = ++age;
            logNote(lane, b, float(midi), st.vel, L.cur[k], L.plan.accent, L, perOct);
        }
    }
};

Engine::Engine() : d(new Impl) {}
Engine::~Engine() { delete d; }

void Engine::init(float rate) {
    d->rate = rate;
    d->echo.init(rate);
    d->hpL.set(95.0f, 0.7f, rate);
    d->hpR.set(95.0f, 0.7f, rate);
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
    const Transport &tr = ctx.transport();
    const Harmony &h = ctx.harmony();
    const double b0 = tr.beat, bps = tr.beatsPerSample;
    if (s.expectBeat >= 0 && std::fabs(b0 - s.expectBeat) > 1e-6)
        s.jumpedAt = s.rendered;
    s.expectBeat = b0 + double(n) * bps;
    // A clock that jumped (the first loop setting the tempo) leaves no
    // backlog of steps behind it.
    for (int k = 0; k < 2; ++k) {
        auto &L = s.lanes[k];
        if (L.running && L.next < b0 - stepBeats(s.eff[k].rate))
            s.schedule(L, s.eff[k], b0, false);
    }
    // Filter settings glide a little, so sweeps are smooth.
    float cutHz[2], res[2], send[2];
    for (int k = 0; k < 2; ++k) {
        auto &L = s.lanes[k];
        const float c = L.cut.step(s.eff[k].cutoff);
        res[k] = L.res.step(s.eff[k].res) * 0.92f;
        cutHz[k] = 55.0f * std::exp2(c * 7.9f);
        send[k] = 0.9f * s.eff[k].echo;
    }
    float dryL[kBlock] = {}, dryR[kBlock] = {}, sendL[kBlock] = {}, sendR[kBlock] = {};
    auto offsetOfBeat = [&](double beat) {
        const double o = std::ceil((beat - b0) / bps - 1e-7);
        return o <= 0 ? 0 : o >= double(n) ? n : int(o);
    };
    auto renderVoices = [&](int from, int to) {
        if (to <= from)
            return;
        for (Voice &v : s.voices)
            if (v.on)
                v.render(dryL, dryR, sendL, sendR, from, to, cutHz[v.lane], res[v.lane], send[v.lane], s.rate);
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
        for (int k = 0; k < 2; ++k) {
            auto &L = s.lanes[k];
            int fired = 0;
            while (L.running && offsetOfBeat(L.next) <= pos && fired++ < 8)
                s.fire(k, std::max(L.next, beatNow), tr, h);
        }
    }
    renderVoices(pos, n);
    // The echo, from whichever lane is playing (the live one first).
    const int lead = s.lanes[0].running || !s.lanes[1].running ? 0 : 1;
    const Settings &e = s.eff[lead];
    const float fb = std::min(0.93f, 0.2f + 0.42f * e.echo + 0.4f * e.throwFb);
    s.echo.process(sendL, sendR, dryL, dryR, n, float(echoBeats(e.echoTime) * tr.samplesPerBeat()), fb,
                   0.8f + 0.12f * e.throwFb);
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        // A soft ceiling: the attack of a stab, or a resonant peak thrown
        // through the echo, rounds off instead of spiking the bus.
        const float L = fastTanh(s.hpL.step(dryL[i]).high * kOutGain),
                    R = fastTanh(s.hpR.step(dryR[i]).high * kOutGain);
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
    auto live = [&](int k) { return s.lanes[k].running ? float(s.lanes[k].count) : 0.0f; };
    data[kDataLivePool] = live(0);
    data[kDataClipPool] = live(1);
    data[kDataLiveStep] = float(s.lanes[0].lastInBar);
    data[kDataClipStep] = float(s.lanes[1].lastInBar);
    data[kDataBar] = float(s.barIndex);
    data[kDataBarCount] = float(s.barCount);
    data[kDataLiveRunning] = s.lanes[0].running ? 1.0f : 0.0f;
    data[kDataClipRunning] = s.lanes[1].running ? 1.0f : 0.0f;
    for (int i = 0; i < kFlashCells; ++i) {
        data[kDataFlash + i] = s.lanes[0].flash[i];
        data[kDataClipFlash + i] = s.lanes[1].flash[i];
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
        o[3] = f.midi > 0 ? float(f.lane + 1) : 0.0f;
    }
}

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

const Settings &Engine::laneSettings(int lane) const { return d->eff[std::clamp(lane, 0, 1)]; }
int Engine::statesReceived() const { return d->states; }
uint64_t Engine::laneCounter(int lane) const { return d->lanes[std::clamp(lane, 0, 1)].counter; }

// --- the page -------------------------------------------------------------------

namespace {

// Where things are on the pad, in pad units (0..1, y up).
constexpr float kSplit = 0.58f;      // the notes left of this, the six pads right of it
constexpr float kChipsTop = 0.865f;  // pattern chips above
constexpr float kChipsLow = 0.135f;  // sound and switch chips below
constexpr int kColumns = 8;
constexpr int kBottomChips = 7;      // five sounds, follow, latch
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
        static const int sounds[] = {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B};
        for (int i = 0; i < SoundCount; ++i)
            if (k.key == sounds[i]) {
                setSound(i, ctx);
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

    void paint(QPainter &p, const PaintContext &pc) override {
        namespace c = console;
        p.setRenderHint(QPainter::Antialiasing, true);
        m_spots.clear();
        c::pad(p, pc, QStringLiteral("Arp"));
        paintGrid(p, pc);
        paintChips(p, pc);
        paintPads(p, pc);
        paintFingers(p, pc);
        c::beats(p, pc, pc.color);
        // Space's take, while it waits for its bar line, on the pad's top edge.
        if (pc.view->recording >= 0)
            c::tag(p, QPointF(pc.pad.center().x(), pc.pad.top()),
                   QStringLiteral("LOOP TAKE · %1 · SPACE CALLS IT OFF").arg(takeText(*pc.view, pc.beat)),
                   std::fmod(pc.time, 0.5) < 0.25 ? c::red() : c::alpha(c::red(), 0.7f), 12, true);
        paintStrip(p, pc);
    }

    QString help() const override {
        return QStringLiteral(
            "← →\tthe sound: pluck, glass, supersaw, pulse, acid (Z X C V B pick one)\n"
            "↑ ↓ · Shift+↑ ↓\tregister: where the grid starts · range: how many octaves the pattern climbs\n"
            "Shift+← → (or [ ])\tthe rate: 1/8 to 1/32, with triplets\n"
            "- +\ttone: darker, brighter\n"
            "Return · Shift+Return\tlatch · overdub on or off (what you play joins the loop)\n"
            "§ (the key left of 1; or \\ or M)\tfollow the chords page\n"
            "Delete\tlet go of what is latched\n"
            "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot · clear it · stop the loop at the bar\n"
            "under the pad, with the mouse\tthe clips (click: launch or stop), sound, rate, first note, register, "
            "range, tone, dice, overdub, let go; each chip shows its key\n"
            "touch the grid\ta note of the scale, the upper row an octave up; press harder for louder\n"
            "first finger\tgrows into a chord; twist it: single, fifth, triad, seventh, ninth\n"
            "more fingers\tadd their notes; slide one sideways to walk it along the scale\n"
            "top chips\tpattern: up, down, up·down, converge, as played, random, chord stabs, pedal\n"
            "bottom chips\tsound: pluck, glass, supersaw, pulse, acid · FOLLOW the chords page · LATCH\n"
            "RATE · GATE\tup: 1/8 to 1/32 with triplets · across: short to legato · twist: swing\n"
            "RHYTHM\tacross: euclidean hits per bar · up: turn the rhythm · press hard: rolls\n"
            "RANGE\tacross: register · up: one to four octaves\n"
            "TONE\tup: cutoff · across: the sound's colour · twist: resonance · press: bite\n"
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
        m_s.pack(c);
        ctx.send(c);
        m_sent = m_s;
        m_sentAt = t;
    }

    // The settings go to the engine whenever they change, at most sixty
    // times a second while a finger slides, at once when a choice is made.
    void flush(double t, ControlContext &ctx, bool now) {
        if (!m_s.near(m_sent) && (now || t - m_sentAt >= 1.0 / 60.0))
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
        m_s.sound = s;
        // Each sound starts from the colour it sounds best in.
        static const float colors[] = {0.3f, 0.45f, 0.55f, 0.35f, 0.15f};
        m_s.color = colors[s];
        ctx.notice(QStringLiteral("Sound: %1").arg(QString::fromUtf8(soundName(s))));
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
                if (chip < SoundCount) {
                    setSound(chip, ctx);
                } else if (chip == SoundCount) {
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
            both([&](Settings &s) {
                s.cutoff = 0.04f + 0.96f * v;
                s.color = u;
                s.res = res;
                s.envBoost = press;
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
    double m_sentAt = -1;
    Settings m_ov, m_ovSent;
    int m_overlayMask = 0, m_overlaySentMask = 0;
    double m_ovAt = -1;
    std::array<Note, 24> m_notes{};
    std::array<int, 16> m_spentTouches{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<Knob, 10> m_knobs{};
    std::array<int, 10> m_chipTouches{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    bool m_latch = false;
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
        const QRectF r = pc.rect(x0, kChipsTop, x1, 1.0f).adjusted(4, 12, -4, -5);
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
    for (int i = 0; i < kBottomChips; ++i) {
        const float x0 = kSplit * float(i) / kBottomChips, x1 = kSplit * float(i + 1) / kBottomChips;
        const QRectF r = pc.rect(x0, 0.0f, x1, kChipsLow).adjusted(4, 5, -4, -7);
        bool lit;
        QString label;
        QColor tint = col;
        if (i < SoundCount) {
            lit = m_s.sound == i;
            label = QString::fromUtf8(soundShort(i));
        } else if (i == SoundCount) {
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
        c::text(p, r, label, lit ? QColor::fromRgb(c::kInk) : tint, 11, true, Qt::AlignCenter);
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
        const QRectF r = pc.rect(float(a.left()), float(a.top()), float(a.right()), float(a.bottom())).adjusted(6, 12, -6, -6);
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
            c::text(p, QRectF(r.left(), r.top() + 1, r.width() - 6, 20),
                    QStringLiteral("%1 · RES %2").arg(QString::fromUtf8(soundShort(s.sound))).arg(int(s.res * 100)),
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
        c::chip(p, pc, QRectF(r.left() + w, r.top(), w - 4, r.height()), QStringLiteral("OVERDUB  ⇧⏎"), m_overdub,
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
