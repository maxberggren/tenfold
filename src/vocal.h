#pragma once

// The Vocal page's machinery, apart from its page and engine (vocal.cpp):
//
//   Tape         what the microphone and the voice's effects made, kept for
//                Space: the engine writes it, the page reads it to choose a
//                loop and copies the loop's audio out of it.
//   PitchTracker which note is being sung (YIN, on a quarter-rate copy).
//   Shifter      a copy of the voice at another pitch and formant, made of
//                pitch-synchronous grains (TD-PSOLA): the voice's own
//                vowels at a new note, a few milliseconds behind it.
//   pick()       where a loop of the singing should start and end: on the
//                bars, cut in the silence between words.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace tf::vocal {

// --- the tape ------------------------------------------------------------------

constexpr int kRingBits = 21;                 // 2^21 samples: 43.7 s at 48 kHz
constexpr int64_t kRing = int64_t(1) << kRingBits;
constexpr int64_t kRingMask = kRing - 1;
constexpr int kHop = 256;                     // the envelope's step: 5.3 ms
constexpr int64_t kEnvRing = kRing / kHop;
constexpr int kMaxTakes = 256;

// Shared by the page and the engine. The engine writes the ring and the
// envelope; the page reads them behind `written`, and fills in takes.
struct Tape {
    float rate = 48000;
    // The voice as heard (every effect, no loops), stereo, indexed by the
    // studio's sample count.
    std::vector<float> wetL, wetR;
    // The microphone's level, before any effect: an RMS per kHop samples,
    // indexed by sample / kHop. Space reads where the words are from it.
    std::vector<float> env;
    std::atomic<int64_t> written{0}; // every sample before this is in the ring

    // A loop's audio. Until the page has copied it out of the ring (once the
    // ring holds all of it), the engine plays it from the ring; after, from
    // `data`, interleaved stereo, `length` frames. Handle 0 is never used.
    struct Take {
        std::atomic<const float *> data{nullptr};
        int64_t start = 0;
        int length = 0;
    };
    Take takes[kMaxTakes];

    Tape() {
        wetL.assign(size_t(kRing), 0.0f);
        wetR.assign(size_t(kRing), 0.0f);
        env.assign(size_t(kEnvRing), 0.0f);
    }
};

// --- pitch ----------------------------------------------------------------------

// YIN on the voice at a quarter of the rate (12 kHz): 70 Hz to 1 kHz, a new
// estimate every 128 samples from the last 30 ms. Cheap enough to run all
// the time, steady enough to tune by.
class PitchTracker {
public:
    static constexpr int kDecim = 4, kSize = 1024, kWindow = 200, kMinTau = 12, kMaxTau = 172;

    void init(float rate) {
        m_rate = rate;
        m_lp.set(2800.0f, 0.7f, rate);
        std::fill(std::begin(m_buf), std::end(m_buf), 0.0f);
    }
    // One sample in; true when a new estimate is ready. Not listening (the
    // gate shut: the room, not a voice), it says unvoiced without the work.
    bool push(float x, bool listen = true) {
        const float y = m_lp.step(x).low;
        m_acc += y;
        if (++m_phase < kDecim)
            return false;
        m_phase = 0;
        m_buf[m_w++ & (kSize - 1)] = m_acc * (1.0f / kDecim);
        m_acc = 0;
        if (++m_since < 32)
            return false;
        m_since = 0;
        if (listen)
            analyse();
        else
            setUnvoiced();
        return true;
    }
    // The period in samples at the full rate (0: unvoiced), and how sure.
    float period() const { return m_period; }
    float hz() const { return m_period > 0 ? m_rate / m_period : 0; }
    float clarity() const { return m_clarity; }

private:
    void analyse() {
        // d(τ) over the newest window, then the cumulative mean normalised
        // difference; the first dip under the threshold is the period.
        const uint32_t end = m_w;
        const uint32_t start = end - uint32_t(kWindow + kMaxTau);
        float x[kWindow + kMaxTau];
        float energy = 0;
        for (int i = 0; i < kWindow + kMaxTau; ++i) {
            x[i] = m_buf[(start + uint32_t(i)) & (kSize - 1)];
            energy += x[i] * x[i];
        }
        if (energy < 1e-7f) {
            setUnvoiced();
            return;
        }
        float d[kMaxTau + 1];
        d[0] = 1;
        float running = 0;
        int best = -1;
        float bestV = 1e9f;
        for (int tau = 1; tau <= kMaxTau; ++tau) {
            float s = 0;
            for (int j = 0; j < kWindow; ++j) {
                const float u = x[j] - x[j + tau];
                s += u * u;
            }
            running += s;
            d[tau] = running > 0 ? s * float(tau) / running : 1;
        }
        for (int tau = kMinTau; tau <= kMaxTau; ++tau) {
            if (d[tau] < 0.15f) {
                while (tau + 1 <= kMaxTau && d[tau + 1] < d[tau])
                    ++tau;
                best = tau;
                bestV = d[tau];
                break;
            }
            if (d[tau] < bestV) {
                bestV = d[tau];
                best = tau;
            }
        }
        if (best < 0 || bestV > 0.35f) {
            setUnvoiced();
            return;
        }
        float tau = float(best);
        if (best > kMinTau && best < kMaxTau) {
            const float a = d[best - 1], b = d[best], c = d[best + 1];
            const float den = a - 2 * b + c;
            if (std::fabs(den) > 1e-9f)
                tau += clampf(0.5f * (a - c) / den, -0.5f, 0.5f);
        }
        const float p = tau * kDecim;
        // A median of three: one octave slip doesn't move the voice.
        m_hist[m_hn++ % 3] = p;
        const float a = m_hist[0], b = m_hist[1], c = m_hist[2];
        const float med = std::max(std::min(a, b), std::min(std::max(a, b), c));
        m_period = m_hn >= 3 && med > 0 ? med : p;
        m_clarity = 1.0f - bestV;
    }
    void setUnvoiced() {
        m_period = 0;
        m_clarity = 0;
        m_hn = 0;
        m_hist[0] = m_hist[1] = m_hist[2] = 0;
    }

    float m_rate = 48000;
    Svf m_lp;
    float m_acc = 0;
    int m_phase = 0, m_since = 0;
    uint32_t m_w = 0;
    float m_buf[kSize];
    float m_period = 0, m_clarity = 0;
    float m_hist[3] = {};
    int m_hn = 0;
};

// --- a voice at another pitch -------------------------------------------------

// Hann, tabulated: grains read it at every sample.
struct Hann {
    static constexpr int N = 1024;
    float t[N + 1];
    Hann() {
        for (int i = 0; i <= N; ++i)
            t[i] = 0.5f - 0.5f * std::cos(kTwoPi * float(i) / float(N));
    }
    inline float at(float u) const { // u in 0..1
        const float x = clampf(u, 0, 1) * N;
        const int i = std::min(int(x), N - 1);
        return t[i] + (t[i + 1] - t[i]) * (x - float(i));
    }
};
inline const Hann &hann() {
    static const Hann h;
    return h;
}

// The source the shifters read: the cleaned-up microphone, at the full rate.
struct Source {
    static constexpr int kSize = 1 << 13;
    float buf[kSize] = {};
    int64_t w = 0; // samples written; the newest is w − 1
    inline void push(float x) { buf[w++ & (kSize - 1)] = x; }
    inline float at(double pos) const {
        const double f = std::floor(pos);
        const int64_t i = int64_t(f);
        const float u = float(pos - f);
        const float a = buf[i & (kSize - 1)], b = buf[(i + 1) & (kSize - 1)];
        return a + (b - a) * u;
    }
};

// TD-PSOLA without analysis marks: each grain is two periods of the voice,
// Hann-windowed, its centre on a grid one period apart (so consecutive
// grains are in phase with each other), placed at the new pitch's period
// in the output. Reading a grain faster or slower moves the formants: a
// giant, an elf, the same vowel. A grain is laid out as late as it can be
// and never reads a sample not yet heard: a voice a period lower comes
// about half a period late (2–5 ms for a singing voice).
class Shifter {
public:
    void reset() {
        for (auto &g : m_grains)
            g.left = 0;
        m_next = -1;
        m_mark = -1e18;
    }
    // Adds n samples into out (×gain), the source's newest sample being
    // src.w − n + i at output sample i. period: the source's (0: unvoiced,
    // a fixed 6 ms grain); ratio: the new pitch over the old; formant: how
    // much faster a grain is read (1: the voice's own vowels).
    void process(const Source &src, int n, float period, float ratio, float formant, float gain, float *out) {
        const float P = period > 0 ? period : 288.0f;
        ratio = clampf(ratio, 0.25f, 4.0f);
        formant = clampf(formant, 0.5f, 2.0f);
        const float half = P / formant;               // the grain's half length, in output samples
        const float hop = P / ratio;                  // output samples between grain centres
        const float lag = P * std::fabs(1.0f - 1.0f / formant) + 2.0f; // the least a grain may trail
        const float norm = 1.0f / std::max(1.0f, ratio / formant);
        const int64_t now0 = src.w - n;
        for (int i = 0; i < n; ++i) {
            const double now = double(now0 + i);
            if (m_next < 0)
                m_next = now + half;
            // A grain whose window opens now: its centre is half a window on.
            while (now >= m_next - half) {
                const double centre = m_next;
                // The latest grid point this grain can be read from without
                // reading ahead of what has been heard.
                const double target = centre - lag;
                double mark;
                if (std::fabs(target - m_mark) > 4.0 * P) {
                    mark = target; // a fresh start (after silence, a leap)
                } else {
                    const double k = std::floor((target - m_mark) / P);
                    mark = m_mark + std::max(0.0, k) * P;
                }
                m_mark = mark;
                spawn(centre, mark, half, formant, P);
                m_next += hop;
            }
            float acc = 0;
            for (auto &g : m_grains) {
                if (g.left <= 0)
                    continue;
                const float u = float((now - (g.centre - g.half)) / (2.0 * g.half));
                acc += src.at(g.mark + double(g.speed) * (now - g.centre)) * hann().at(u);
                --g.left;
            }
            out[i] += acc * gain * norm;
        }
    }

private:
    struct Grain {
        double centre = 0, mark = 0;
        float half = 0, speed = 1;
        int left = 0;
    };
    void spawn(double centre, double mark, float half, float speed, float P) {
        Grain *g = nullptr;
        for (auto &c : m_grains)
            if (c.left <= 0) {
                g = &c;
                break;
            }
        if (!g) {
            // Every slot busy (a very high voice): the oldest gives way.
            g = &m_grains[0];
            for (auto &c : m_grains)
                if (c.left < g->left)
                    g = &c;
        }
        (void)P;
        g->centre = centre;
        g->mark = mark;
        g->half = half;
        g->speed = speed;
        g->left = int(std::ceil(2.0f * half));
    }

    Grain m_grains[16];
    double m_next = -1, m_mark = -1e18;
};

// --- a talk box --------------------------------------------------------------------

// A talk box, as a funk player wires one: a synth's sound goes up a tube
// into the mouth, and the mouth shapes it into words. Here the mouth is
// measured instead. Linear prediction (order 24, the autocorrelation
// method, on the voice tilted up 6 dB an octave so the upper formants
// count) finds the vocal tract every 8 ms from the last 16 ms, and an
// all-pole lattice filter with that tract shapes the synth. The lattice's
// reflection coefficients are moved sample by sample from one frame's to
// the next: each stays inside ±1 all the way, so the filter is stable at
// every step, and the formants glide as a mouth's do instead of stepping
// at the frame rate.
class TalkBox {
public:
    static constexpr int kOrder = 24, kWin = 768, kHopN = 384;

    void init(float rate) {
        m_rate = rate;
        for (int i = 0; i < kWin; ++i)
            m_win[i] = 0.5f - 0.5f * std::cos(kTwoPi * (float(i) + 0.5f) / float(kWin));
        m_winSq = 0;
        for (float w : m_win)
            m_winSq += double(w) * w;
        reset();
    }
    void reset() {
        std::fill(std::begin(m_buf), std::end(m_buf), 0.0f);
        std::fill(std::begin(m_k0), std::end(m_k0), 0.0f);
        std::fill(std::begin(m_k1), std::end(m_k1), 0.0f);
        std::fill(std::begin(m_b), std::end(m_b), 0.0f);
        m_g0 = m_g1 = 0;
        m_pre = 0;
        m_n = 0;
        m_w = 0;
    }
    // One sample of the voice in (what the mouth is doing), one of the
    // synth (what goes up the tube); the synth, spoken, out. The synth is
    // best bright and steady: the filter puts back every formant, so a
    // plain sawtooth comes out as a vowel.
    inline float step(float voice, float synth) {
        m_buf[m_w] = voice - 0.95f * m_pre;
        m_pre = voice;
        m_w = (m_w + 1) % kWin;
        if (++m_n >= kHopN) {
            m_n = 0;
            analyse();
        }
        const float t = float(m_n) * (1.0f / kHopN);
        float f = (m_g0 + (m_g1 - m_g0) * t) * synth;
        for (int i = kOrder; i >= 1; --i) {
            const float k = m_k0[i] + (m_k1[i] - m_k0[i]) * t;
            f += k * m_b[i - 1];
            m_b[i] = m_b[i - 1] - k * f;
        }
        // A silent voice has no tract to measure: the state can't grow,
        // but a denormal tail shouldn't linger either.
        m_b[0] = std::fabs(f) < 1e-20f ? 0.0f : f;
        return m_b[0];
    }

    // The analysis, exposed for the tests: the reflection coefficients
    // the lattice is heading for, and the residual's level.
    const float *target() const { return m_k1; }
    float gain() const { return m_g1; }

private:
    void analyse() {
        // Where the frame now ends is where it was interpolating to: start
        // the next glide from there.
        std::copy(std::begin(m_k1), std::end(m_k1), std::begin(m_k0));
        m_g0 = m_g1;
        float x[kWin];
        for (int i = 0; i < kWin; ++i)
            x[i] = m_buf[(m_w + i) % kWin] * m_win[i];
        double r[kOrder + 1];
        for (int lag = 0; lag <= kOrder; ++lag) {
            double s = 0;
            for (int i = lag; i < kWin; ++i)
                s += double(x[i]) * x[i - lag];
            // A lag window (a 40 Hz Gaussian) widens the formants a
            // little, so a high voice's harmonics aren't taken for them.
            const double w = kTwoPi * 40.0 / m_rate * lag;
            r[lag] = s * std::exp(-0.5 * w * w);
        }
        if (r[0] < 1e-9 * kWin) {
            std::fill(std::begin(m_k1), std::end(m_k1), 0.0f);
            m_g1 = 0;
            return;
        }
        r[0] *= 1.0001; // a hair of white noise: never a pole on the circle
        // Levinson–Durbin: the prediction x̂[n] = Σ a[j] x[n−j], built up an
        // order at a time, each order's reflection coefficient kept.
        double a[kOrder + 1] = {}, na[kOrder + 1];
        double e = r[0];
        for (int i = 1; i <= kOrder; ++i) {
            double acc = r[i];
            for (int j = 1; j < i; ++j)
                acc -= a[j] * r[i - j];
            const double k = std::clamp(acc / e, -0.995, 0.995);
            for (int j = 1; j < i; ++j)
                na[j] = a[j] - k * a[i - j];
            for (int j = 1; j < i; ++j)
                a[j] = na[j];
            a[i] = k;
            m_k1[i] = float(k);
            e *= 1.0 - k * k;
        }
        // The residual's level per sample: a unit synth through the filter
        // comes out as loud as the voice was.
        m_g1 = float(std::sqrt(std::max(0.0, e) / m_winSq));
    }

    float m_win[kWin];
    double m_winSq = 1;
    float m_buf[kWin];
    float m_k0[kOrder + 1] = {}, m_k1[kOrder + 1] = {}, m_b[kOrder + 1] = {};
    float m_g0 = 0, m_g1 = 0, m_pre = 0, m_rate = 48000;
    int m_n = 0, m_w = 0;
};

// --- sympathetic strings ---------------------------------------------------------

// A sitar's tarafs: strings no one plucks, tuned to the raga's notes,
// that ring when the playing (here, the singing) has their note in it. Each
// is a delay line a period long (its fraction an allpass, which loses
// nothing: an interpolated read would dull the string within a second)
// fed back through a three-tap loop filter.
// The filter is symmetric, so its delay is one sample whatever its
// brightness, and the string stays in tune as the brightness moves. And it
// moves: the louder a string rings, the brighter its loop, the way a
// sitar's flat bridge (the jawari) makes a string buzz brighter the harder
// it swings, so each note blooms into a zing as it builds and darkens as
// it dies.
class Strings {
public:
    static constexpr int kCount = 13, kSize = 4096;

    void init(float rate) {
        m_rate = rate;
        for (auto &s : m_s) {
            std::fill(std::begin(s.buf), std::end(s.buf), 0.0f);
            s.w = 0;
            s.env = 0;
            s.z1 = s.z2 = 0;
            s.apX = s.apY = 0;
            s.midi = -1;
        }
        m_sizzleHp.set(1500.0f, 0.7f, rate);
        m_sizzleHp2.set(1500.0f, 0.7f, rate);
        m_level = m_pre = 0;
    }
    // Tunes string k (a MIDI note); only a change does any work.
    void tune(int k, float midi) {
        S &s = m_s[k];
        if (std::fabs(s.midi - midi) < 1e-3f)
            return;
        s.midi = midi;
        const float hz = midiToHz(midi);
        // A period, less the loop filter's one sample: whole samples, and a
        // fraction between a half and one and a half for the allpass,
        // where its delay is flattest.
        const float len = clampf(m_rate / hz - 1.0f, 8.0f, float(kSize - 4));
        s.n = int(std::floor(len - 0.5f));
        const float d = len - float(s.n);
        s.eta = (1.0f - d) / (1.0f + d);
        // The lower strings ring longer, as long strings do: a T60 of 3.5 s
        // at 110 Hz, 2 s by 880.
        const float t60 = clampf(3.5f - 0.5f * std::log2(hz / 110.0f), 1.6f, 4.0f);
        s.fb = std::pow(10.0f, -3.0f / (t60 * hz));
    }
    // One sample in (the voice); the strings, left and right, out.
    inline void step(float in, float &outL, float &outR) {
        // The voice tilted up 6 dB an octave: a voice's upper harmonics are
        // weak, a plucked string's aren't, and these strings should ring
        // with the whole series.
        const float x0 = in - 0.85f * m_pre;
        m_pre = in;
        in = x0 * 2.5f;
        float l = 0, r = 0;
        for (int k = 0; k < kCount; ++k) {
            S &s = m_s[k];
            const float a = s.buf[(s.w - s.n) & (kSize - 1)];
            const float x = s.eta * (a - s.apY) + s.apX;
            s.apX = a;
            s.apY = x;
            s.env += (std::fabs(x) - s.env) * (std::fabs(x) > s.env ? 0.01f : 0.0004f);
            // The jawari: a dark loop (the side taps at 0.25: the top
            // octave of the string gone within a few passes) opening to an
            // almost lossless one (0.02) as the string's level climbs.
            const float side = 0.25f - 0.23f * clampf(s.env * 8.0f, 0.0f, 1.0f);
            const float y = (side * (x + s.z2) + (1.0f - 2.0f * side) * s.z1) * s.fb;
            s.z2 = s.z1;
            s.z1 = x;
            s.buf[s.w] = in + y;
            s.w = (s.w + 1) & (kSize - 1);
            (k & 1 ? r : l) += x;
            (k & 1 ? l : r) += x * 0.35f;
        }
        // A held note in tune with several strings (its octaves, its
        // fifth) rings them far louder than a passing one rings one: a
        // slow leveller keeps them all under the voice, quick to duck and
        // slow to let go, so a tail still dies away as it would.
        const float peak = std::max(std::fabs(l), std::fabs(r));
        m_level += (peak - m_level) * (peak > m_level ? 0.002f : 0.00005f);
        const float g = m_level > 0.3f ? 0.3f / m_level : 1.0f;
        l *= g;
        r *= g;
        // The buzz itself: the strings' sum squared, sign kept, over its
        // level (odd partials, as loud against the ring whatever its level
        // once it rings, fading out as it dies), its low half taken off so
        // only the sizzle on top is left.
        const float norm = 1.0f / (m_level + 0.03f);
        outL = l + 0.5f * m_sizzleHp.step(l * std::fabs(l) * norm).high;
        outR = r + 0.5f * m_sizzleHp2.step(r * std::fabs(r) * norm).high;
    }

private:
    struct S {
        float buf[kSize];
        int w = 0;
        int n = 100;
        float eta = 0, apX = 0, apY = 0, fb = 0.99f, env = 0, z1 = 0, z2 = 0, midi = -1;
    };
    S m_s[kCount];
    float m_rate = 48000, m_level = 0, m_pre = 0;
    Svf m_sizzleHp, m_sizzleHp2;
};

// --- bronze ----------------------------------------------------------------------

// A gamelan's bronze bars, by their modes: each bar rings at its own four
// partials (a free bar's 1, 2.76, 5.40, 8.93, the upper ones dying
// sooner), every mode a decaying phasor. Every note is struck on two
// instruments at once, one tuned a few hertz under the note and one over,
// left and right, the way a Balinese pair is tuned to beat (ombak): the
// note shimmers. As a player does with the off hand, a new strike damps
// all but the last note before it, so a melody rings without smearing.
class Bronze {
public:
    static constexpr int kStrikes = 8, kModes = 4;

    void init(float rate) {
        m_rate = rate;
        for (auto &s : m_s)
            s.live = false;
        m_click.set(3200.0f, 1.2f, rate);
    }
    void strike(float midi, float velocity, float ombakHz = 3.2f) {
        // The voice to use: a dead one, or else the quietest.
        Strike *s = nullptr;
        for (auto &c : m_s)
            if (!c.live) {
                s = &c;
                break;
            }
        if (!s) {
            s = &m_s[0];
            for (auto &c : m_s)
                if (c.level < s->level)
                    s = &c;
        }
        // Damp what came before but the newest: a player's other hand.
        for (auto &c : m_s)
            if (c.live && c.order < m_order - 1)
                c.damp = true;
        static const float ratio[kModes] = {1.0f, 2.756f, 5.404f, 8.933f};
        static const float amp[kModes] = {1.0f, 0.42f, 0.2f, 0.09f};
        static const float t60[kModes] = {4.0f, 1.7f, 0.75f, 0.35f};
        const float hz = midiToHz(midi);
        const float v = clampf(velocity, 0.05f, 1.0f);
        for (int side = 0; side < 2; ++side) {
            const float f0 = hz + (side ? ombakHz : -ombakHz) * 0.5f;
            for (int m = 0; m < kModes; ++m) {
                Mode &md = s->mode[side][m];
                const float f = f0 * ratio[m];
                // A mode past the top of the band would only alias.
                const float a = f < m_rate * 0.45f ? amp[m] * std::pow(v, 0.5f * float(m)) : 0.0f;
                const float r = std::pow(10.0f, -3.0f / (t60[m] * m_rate));
                const float w = kTwoPi * f / m_rate;
                md.cr = r * std::cos(w);
                md.ci = r * std::sin(w);
                // Starting on the sine's zero: the bar's own swing, no step.
                md.re = 0;
                md.im = -a * v;
            }
        }
        s->live = true;
        s->damp = false;
        s->level = v;
        s->order = m_order++;
        m_tock += 0.35f * v; // the mallet's knock on the bar
    }
    inline void step(Rng &rng, float &outL, float &outR) {
        float l = 0, r = 0;
        static const float dampR = 0.99952f; // a 0.3 s T60 at 48 kHz, on top of the mode's own
        for (auto &s : m_s) {
            if (!s.live)
                continue;
            for (int side = 0; side < 2; ++side) {
                float sum = 0;
                for (auto &md : s.mode[side]) {
                    const float re = md.re * md.cr - md.im * md.ci;
                    const float im = md.re * md.ci + md.im * md.cr;
                    md.re = re;
                    md.im = im;
                    sum += re;
                }
                (side ? r : l) += sum;
            }
            // The fundamental is the longest mode: when it has died, so
            // has the bar.
            s.level *= s.damp ? dampR * 0.99998f : 0.99998f;
            if (s.damp)
                for (auto &side : s.mode)
                    for (auto &md : side) {
                        md.re *= dampR;
                        md.im *= dampR;
                    }
            const float e = std::fabs(s.mode[0][0].re) + std::fabs(s.mode[0][0].im);
            if (e < 1e-5f && s.level < 0.02f)
                s.live = false;
        }
        // The knock: a few milliseconds of band-passed noise.
        m_tock *= 0.992f;
        if (m_tock < 1e-5f)
            m_tock = 0;
        const float tock = m_tock > 0 ? m_click.step(rng.bipolar() * m_tock).band : 0.0f;
        outL = l + tock;
        outR = r + tock;
    }

private:
    struct Mode {
        float re = 0, im = 0, cr = 1, ci = 0;
    };
    struct Strike {
        Mode mode[2][kModes];
        bool live = false, damp = false;
        float level = 0;
        int order = 0;
    };
    Strike m_s[kStrikes];
    int m_order = 0;
    float m_rate = 48000, m_tock = 0;
    Svf m_click;
};

// --- Space: where the words are ---------------------------------------------

// The envelope as the page hands it to pick(): oldest first, every hop's
// middle at a known time (the latency already taken off).
struct Sung {
    std::vector<float> env; // RMS per hop
    double t0 = 0;          // touch-clock seconds of env[0]
    double hop = kHop / 48000.0;
    double time(double h) const { return t0 + h * hop; }
    double hopAt(double t) const { return (t - t0) / hop; }
};

struct Word {
    int start = 0, end = 0; // hops, end exclusive
};

// The words: where the level stands clear of the room's own noise, with a
// little hangover so a plosive's gap doesn't split a word, and nothing
// shorter than a breath's click. Also the threshold it used (RMS).
inline std::vector<Word> findWords(const Sung &s, float *threshold = nullptr) {
    std::vector<Word> words;
    const int n = int(s.env.size());
    if (n == 0)
        return words;
    // The noise floor: the quietest moment of each second of the last
    // twenty, and of those a quiet one: a dense topline with hardly a gap
    // still has its breaths, a second apart.
    const int per = std::max(1, int(1.0 / s.hop));
    std::vector<float> minima;
    for (int a = std::max(0, n - 20 * per); a < n; a += per) {
        float lo = 1e9f;
        for (int h = a; h < std::min(n, a + per); ++h)
            lo = std::min(lo, s.env[size_t(h)]);
        minima.push_back(lo);
    }
    std::sort(minima.begin(), minima.end());
    const float noise = std::max(1e-5f, minima[minima.size() / 4]);
    const float on = std::max(noise * 5.0f, 0.004f), off = on * 0.5f;
    if (threshold)
        *threshold = off;
    const int hang = std::max(1, int(0.08 / s.hop)), shortest = std::max(1, int(0.04 / s.hop));
    bool in = false;
    int start = 0, quiet = 0;
    for (int h = 0; h < n; ++h) {
        const float v = s.env[size_t(h)];
        if (!in) {
            if (v > on) {
                in = true;
                start = h;
                quiet = 0;
            }
        } else if (v < off) {
            if (++quiet > hang) {
                const int end = h - quiet + 1;
                if (end - start >= shortest)
                    words.push_back({start, end});
                in = false;
            }
        } else {
            quiet = 0;
        }
    }
    if (in && n - start >= shortest)
        words.push_back({start, n - quiet});
    return words;
}

// How loud it is around a moment, over the threshold, in dB (0: silence):
// what a cut there would cut into.
inline float cutCost(const Sung &s, double t, float threshold) {
    const double h = s.hopAt(t);
    const int r = std::max(1, int(0.03 / s.hop));
    float peak = 0;
    for (int i = int(std::floor(h)) - r; i <= int(std::ceil(h)) + r; ++i)
        if (i >= 0 && i < int(s.env.size()))
            peak = std::max(peak, s.env[size_t(i)]);
    return peak > threshold ? 20.0f * std::log10(peak / threshold) : 0.0f;
}

struct PickRequest {
    // The clock: free (nothing plays yet: the loop will set the tempo), or
    // beats at a tempo.
    bool free = false;
    double now = 0;    // seconds, the moment of Space
    double beat = 0;   // the beat at `now`
    double bpm = 120;
    int bars = 0;       // 0: choose; else this many (↑↓)
    int phrases = 0;    // free: 0 choose, else this many phrases back (↑↓)
    double fit = 0;     // bars of a playing loop the take may grow to cover (the chords' progression)
    double layerBase = 0; // a layer: the bars of the loop it goes onto (its length divides it)
    double since = -1e18; // a layer: nothing sung before this beat belongs to it
};

struct Pick {
    bool ok = false;
    // Bars: beats b0..b1 (the loop lands at b1). Free: seconds b0..b1.
    double b0 = 0, b1 = 0;
    int bars = 0, phrases = 0;
    double delta = 0;          // how far off the bar line the cuts sit, in beats
    float cutDb = 0;           // the louder of the two cuts, over the silence
    double phraseFrom = 0, phraseTo = 0; // the singing it was chosen for (beats, or seconds free)
    int words = 0;
};

inline double beatOf(const PickRequest &r, double t) { return r.beat + (t - r.now) * r.bpm / 60.0; }
inline double timeOf(const PickRequest &r, double b) { return r.now + (b - r.beat) * 60.0 / r.bpm; }

// The phrases: words with less than `gap` seconds between them.
inline std::vector<Word> phrasesOf(const std::vector<Word> &words, int gapHops) {
    std::vector<Word> out;
    for (const Word &w : words) {
        if (!out.empty() && w.start - out.back().end < gapHops)
            out.back().end = w.end;
        else
            out.push_back(w);
    }
    return out;
}

// Loop lengths in bars, as Space offers them: doubling, as the other
// loops are made.
inline const std::vector<int> &barSteps() {
    static const std::vector<int> s{1, 2, 4, 8, 16};
    return s;
}

// What Space takes. Bars: the latest phrase, in whole bars (1, 2, 4, 8,
// 16), with both cuts moved together up to a beat off the bar lines so they
// fall where nothing is sung: the loop keeps the singing where it was
// against the beat, and its seam is a breath, never a word. Free: the
// latest phrase, from its first word to Space.
inline Pick pick(const Sung &s, const PickRequest &r) {
    Pick out;
    float thr = 0;
    const std::vector<Word> words = findWords(s, &thr);
    // Only words already sung (Space's moment), and for a layer only those
    // sung over the loop.
    std::vector<Word> heard;
    for (const Word &w : words) {
        if (s.time(w.start) > r.now + 0.02)
            continue;
        if (!r.free && beatOf(r, s.time(w.end)) < r.since + 0.05)
            continue;
        heard.push_back(w);
    }
    if (heard.empty())
        return out;
    const double spb = 60.0 / r.bpm;
    // A breath inside a line is shorter than this; between lines, longer.
    const int gap = int(std::max(0.8, r.free ? 0.8 : 1.25 * spb) / s.hop);
    const std::vector<Word> phrases = phrasesOf(heard, gap);
    const Word &last = phrases.back();
    out.words = int(heard.size());

    if (r.free) {
        // From the first word of the phrase (or of `phrases` phrases back),
        // a breath before it, to Space.
        const int back = std::clamp(r.phrases > 0 ? r.phrases : 1, 1, int(phrases.size()));
        const Word &first = phrases[phrases.size() - size_t(back)];
        const double t0 = s.time(first.start) - 0.03, t1 = r.now;
        if (t1 - t0 < 0.3)
            return out;
        out.ok = true;
        out.b0 = t0;
        out.b1 = t1;
        out.phrases = back;
        out.phraseFrom = s.time(first.start);
        out.phraseTo = s.time(last.end);
        out.cutDb = std::max(cutCost(s, t0, thr), cutCost(s, t1, thr));
        return out;
    }

    // Bars. The phrase sung last, and every phrase before it that ran into
    // it within the loop.
    const double from = beatOf(r, s.time(last.start)), to = beatOf(r, s.time(last.end));
    out.phraseFrom = from;
    out.phraseTo = to;
    // The loop ends on the bar line the phrase's tail reaches (a word may
    // run up to a beat past it), and not before Space: still singing, it
    // ends on the next.
    const double nowBar = std::floor(r.beat / 4.0 + 1e-9) * 4.0;
    double end = std::ceil((to - 1.0) / 4.0 - 1e-9) * 4.0;
    if (end < to - 1.0)
        end += 4.0;
    if (r.now - s.time(last.end) < 0.12)
        end = std::max(end, nowBar + 4.0);
    // Its length: as few bars as hold the phrase from its first word (a
    // pickup may start a beat before the bar), more to cover a progression
    // that plays, the loop's own for a layer, or what ↑↓ asked for.
    // (Both cuts move by the same δ, at most a beat: the loop holds the
    // phrase when some δ puts the end after its last word and the start
    // before its first.)
    int bars = 16;
    for (int b : barSteps())
        if (std::max(to - end, -1.0) <= std::min(from - end + 4.0 * b, 1.0) + 1e-6) {
            bars = b;
            break;
        }
    if (r.fit > bars && r.fit <= 16 && std::fabs(std::log2(r.fit) - std::round(std::log2(r.fit))) < 1e-6)
        bars = int(r.fit);
    if (r.layerBase > 0)
        bars = int(r.layerBase);
    if (r.bars > 0)
        bars = r.bars;
    // Where to cut: both cuts move together, a sixteenth at a time, up to a
    // beat either way. Cutting into a word costs most, then cutting the
    // phrase off, then leaving the bar line.
    double bestCost = 1e18, bestDelta = 0;
    float bestCut = 0;
    const double len = 4.0 * bars;
    for (int k = -16; k <= 16; ++k) {
        const double d = k / 16.0;
        const double b0 = end - len + d, b1 = end + d;
        const float c0 = cutCost(s, timeOf(r, b0), thr), c1 = cutCost(s, timeOf(r, b1), thr);
        double cost = 3.0 * (c0 + c1) + 4.0 * std::fabs(d);
        if (b1 < to - 0.02)
            cost += 30.0 + 10.0 * (to - b1); // the phrase's tail left out
        if (b0 > from + 0.02 && r.bars == 0)
            cost += 30.0 + 10.0 * (b0 - from); // its start
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            bestDelta = d;
            bestCut = std::max(c0, c1);
        }
    }
    out.ok = true;
    out.bars = bars;
    out.delta = bestDelta;
    out.b0 = end - len + bestDelta;
    out.b1 = end + bestDelta;
    out.cutDb = bestCut;
    return out;
}

}
