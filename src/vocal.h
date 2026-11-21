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
    // One sample in; true when a new estimate is ready.
    bool push(float x) {
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
        analyse();
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

    Grain m_grains[10];
    double m_next = -1, m_mark = -1e18;
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
    // The noise floor: the quietest tenth of the last twenty seconds.
    std::vector<float> sorted(s.env.end() - std::min(n, int(20.0 / s.hop)), s.env.end());
    std::sort(sorted.begin(), sorted.end());
    const float noise = std::max(1e-5f, sorted[sorted.size() / 10]);
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
    const int gap = int(std::max(0.35, r.free ? 0.35 : 1.25 * spb) / s.hop);
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
