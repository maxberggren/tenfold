#pragma once

// Small building blocks for the engine. Everything here is allocation-free
// after construction and safe to run on the audio thread.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace tf {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float midiToHz(double midi) { return float(440.0 * std::exp2((midi - 69.0) / 12.0)); }
inline float dbToGain(float db) { return std::exp(db * 0.11512925464970229f); }

// Denormals turn a decaying reverb tail into a CPU spike. Flush them to zero
// on the thread that renders audio.
inline void flushDenormals() {
#if defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ull << 24); // FZ
    asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
    unsigned csr;
    asm volatile("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040; // FTZ | DAZ
    asm volatile("ldmxcsr %0" ::"m"(csr));
#endif
}

// A Padé tanh, accurate to a few parts in a thousand over the range that
// matters and smooth everywhere, with a hard bound past it.
inline float fastTanh(float x) {
    x = clampf(x, -4.97f, 4.97f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// xorshift: deterministic, so a performance script renders identically every
// time, and cheap enough to run per sample per partial.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    inline uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // [-1, 1)
    inline float bipolar() { return float(int32_t(next())) * (1.0f / 2147483648.0f); }
    // [0, 1)
    inline float unit() { return float(next() >> 8) * (1.0f / 16777216.0f); }
};

// One-pole smoother set by a time constant in seconds.
struct Smooth {
    float y = 0, a = 0;
    void setTime(float seconds, float rate) { a = seconds <= 0 ? 1.0f : 1.0f - std::exp(-1.0f / (seconds * rate)); }
    inline float step(float target) { return y += a * (target - y); }
    void reset(float v) { y = v; }
};

// Zero-delay-feedback state variable filter (Zavalishin/Simper). It stays
// stable and in tune under fast modulation, which the vowel and damper sweeps
// need.
struct Svf {
    float ic1 = 0, ic2 = 0;
    float a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set(float hz, float q, float rate) {
        hz = clampf(hz, 5.0f, rate * 0.49f);
        const float g = std::tan(kPi * hz / rate);
        k = 1.0f / std::max(q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    struct Out {
        float low, band, high;
    };
    inline Out step(float v0) {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return {v2, v1, v0 - k * v1 - v2};
    }
    void reset() { ic1 = ic2 = 0; }
};

struct OnePoleLp {
    float y = 0, a = 1;
    void set(float hz, float rate) { a = 1.0f - std::exp(-kTwoPi * clampf(hz, 1.0f, rate * 0.49f) / rate); }
    inline float step(float x) { return y += a * (x - y); }
};

struct DcBlock {
    float x1 = 0, y1 = 0, r = 0.9995f;
    inline float step(float x) {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

// Power-of-two delay line with cubic Hermite reads, so modulated delays (the
// tape echo, the reverb's chorused lines, the shimmer) glide without zipper.
struct Delay {
    std::vector<float> buf;
    uint32_t mask = 0, w = 0;
    void init(size_t minSize) {
        size_t n = 1;
        while (n < minSize + 4)
            n <<= 1;
        buf.assign(n, 0.0f);
        mask = uint32_t(n - 1);
        w = 0;
    }
    inline void push(float x) {
        buf[w] = x;
        w = (w + 1) & mask;
    }
    // Integer delay of d samples (d >= 1 reads the sample written d pushes ago).
    inline float tap(uint32_t d) const { return buf[(w - d) & mask]; }
    inline float read(float d) const {
        d = std::max(d, 1.0f);
        const float fl = std::floor(d);
        const float t = d - fl;
        const uint32_t i = uint32_t(fl);
        const float y0 = tap(i - 1), y1 = tap(i), y2 = tap(i + 1), y3 = tap(i + 2);
        const float c0 = y1, c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
};

struct Allpass {
    Delay d;
    uint32_t len = 1;
    float g = 0.6f;
    void init(uint32_t n, float gain) {
        d.init(n + 2);
        len = n;
        g = gain;
    }
    inline float step(float x) {
        const float delayed = d.tap(len);
        const float v = x + g * delayed;
        d.push(v);
        return delayed - g * v;
    }
};

// 2x oversampling around a nonlinearity: a 31-tap windowed-sinc halfband up
// and down, so the grit stage doesn't fold harmonics back as alias whine.
struct Oversampler2x {
    static constexpr int N = 31;
    float h[N];
    float up[N] = {}, down[N] = {};
    int upPos = 0, downPos = 0;
    Oversampler2x() {
        const int c = N / 2;
        float sum = 0;
        for (int i = 0; i < N; ++i) {
            const int n = i - c;
            const float sinc = n == 0 ? 0.5f : std::sin(kPi * 0.5f * n) / (kPi * n);
            const float w = 0.42f - 0.5f * std::cos(kTwoPi * i / (N - 1)) + 0.08f * std::cos(2 * kTwoPi * i / (N - 1));
            h[i] = sinc * w;
            sum += h[i];
        }
        for (float &v : h)
            v /= sum;
    }
    inline float fir(float *line, int &pos, float x) {
        line[pos] = x;
        float acc = 0;
        int j = pos;
        for (int i = 0; i < N; ++i) {
            acc += h[i] * line[j];
            j = j == 0 ? N - 1 : j - 1;
        }
        pos = pos + 1 == N ? 0 : pos + 1;
        return acc;
    }
    template <typename F>
    inline float process(float x, F &&shape) {
        const float a = shape(2.0f * fir(up, upPos, x));
        const float b = shape(2.0f * fir(up, upPos, 0.0f));
        fir(down, downPos, a);
        return fir(down, downPos, b);
    }
};

// Single-producer, single-consumer ring for commands from the touch thread
// to the audio thread. Never blocks, never allocates after construction.
template <typename T, size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "power of two");

public:
    bool push(const T &v) {
        const size_t h = m_head.load(std::memory_order_relaxed);
        const size_t t = m_tail.load(std::memory_order_acquire);
        if (h - t >= N)
            return false;
        m_items[h & (N - 1)] = v;
        m_head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T &v) {
        const size_t t = m_tail.load(std::memory_order_relaxed);
        const size_t h = m_head.load(std::memory_order_acquire);
        if (t == h)
            return false;
        v = m_items[t & (N - 1)];
        m_tail.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> m_items{};
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
};

// Lock-free triple buffer: the audio thread publishes a view for the screen
// without ever waiting on it.
template <typename T>
class TripleBuffer {
public:
    T &back() { return m_slots[m_back]; }
    void publish() {
        const int old = m_middle.exchange(m_back | 4, std::memory_order_acq_rel);
        m_back = old & 3;
    }
    // Returns true when a newer value was taken.
    bool fetch() {
        if (!(m_middle.load(std::memory_order_acquire) & 4))
            return false;
        const int old = m_middle.exchange(m_front, std::memory_order_acq_rel);
        m_front = old & 3;
        return true;
    }
    const T &front() const { return m_slots[m_front]; }

private:
    T m_slots[3]{};
    int m_back = 0, m_front = 1;
    std::atomic<int> m_middle{2};
};

}
