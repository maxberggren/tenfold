#pragma once

// The space the instrument plays into: tape echo, grain cloud, a sixteen-line
// feedback delay network with octave shimmer, and the master limiter.

#include "dsp.h"

namespace tf {

// Sixteen delay lines mixed through a Hadamard matrix, each line chorused by
// its own slow LFO and darkened by its own damping filter. With shimmer on,
// the tail is pitched up an octave and fed back in, so held chords bloom into
// a choir of harmonics above them.
class Reverb {
public:
    static constexpr int L = 16;

    void init(float rate) {
        m_rate = rate;
        static const float ms[L] = {43.1f, 47.9f, 53.3f, 59.7f, 67.1f, 71.3f, 79.9f, 83.7f,
                                    89.3f, 97.1f, 101.9f, 109.3f, 113.7f, 127.1f, 131.9f, 139.3f};
        Rng rng(0xC0FFEE);
        for (int i = 0; i < L; ++i) {
            m_base[i] = ms[i] * 0.001f * rate;
            m_lines[i].init(size_t(m_base[i] * 1.8f + 64));
            m_lfoPhase[i] = rng.unit();
            m_lfoRate[i] = (0.07f + 0.5f * rng.unit()) / rate;
            m_damp[i] = {};
        }
        static const int diff[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) {
            m_diffL[i].init(uint32_t(diff[i] * rate / 44100.0f), 0.7f);
            m_diffR[i].init(uint32_t(diff[i] * 1.13f * rate / 44100.0f), 0.7f);
        }
        m_pre.init(size_t(0.05f * rate));
        m_shift.init(size_t(0.2f * rate));
        m_shiftWindow = 0.07f * rate;
        m_shimLp.set(9000.0f, rate);
        for (int i = 0; i < L; ++i)
            m_lowLp[i].set(110.0f, rate);
        m_shimHp.set(320.0f, 0.7f, rate);
        m_inHpL.set(30.0f, 0.7f, rate);
        m_inHpR.set(30.0f, 0.7f, rate);
        set(0.35f, 0.0f);
    }

    // amount 0..1 sets size, decay and darkness together; shimmer 0..1.
    void set(float amount, float shimmer) {
        m_amount = amount;
        m_shimmer = shimmer;
        m_size = 0.55f + 0.95f * amount;
        // From a small room (1 s) to effectively endless (≈ 90 s).
        const float t60 = 1.0f * std::pow(90.0f, amount * amount);
        for (int i = 0; i < L; ++i) {
            const float len = m_base[i] * m_size;
            m_gain[i] = std::pow(10.0f, -3.0f * len / (t60 * m_rate));
            m_damp[i].set(lerpf(12500.0f, 5500.0f, amount), m_rate);
        }
    }

    inline void process(float inL, float inR, float &outL, float &outR) {
        // Nothing below 30 Hz goes in: with a tail this long, rumble would
        // only pile up where no speaker can play it.
        inL = m_inHpL.step(inL).high;
        inR = m_inHpR.step(inR).high;
        m_pre.push(0.5f * (inL + inR));
        const float pre = m_pre.tap(uint32_t(0.018f * m_rate));
        float l = pre + 0.3f * (inL - inR), r = pre - 0.3f * (inL - inR);

        // Octave-up shimmer: two read heads sweeping through a delay line
        // at double speed, crossfaded so the jump back is never heard.
        if (m_shimmer > 0.001f) {
            m_shiftPhase += 1.0f / m_shiftWindow;
            if (m_shiftPhase >= 1.0f)
                m_shiftPhase -= 1.0f;
            const float p1 = m_shiftPhase, p2 = p1 + 0.5f >= 1.0f ? p1 - 0.5f : p1 + 0.5f;
            const float d1 = (1.0f - p1) * m_shiftWindow + 4.0f, d2 = (1.0f - p2) * m_shiftWindow + 4.0f;
            const float w1 = std::sin(kPi * p1), w2 = std::sin(kPi * p2);
            const float up = m_shift.read(d1) * w1 * w1 + m_shift.read(d2) * w2 * w2;
            // Only what sits well above the bass climbs: a pitch shifter keeps
            // DC and rumble where they are, and in a near-endless tail that
            // would feed on itself.
            const float sh = fastTanh(m_shimLp.step(m_shimHp.step(up).high) * m_shimmer * 0.9f);
            l += sh;
            r += sh;
        }

        for (int i = 0; i < 4; ++i) {
            l = m_diffL[i].step(l);
            r = m_diffR[i].step(r);
        }

        float x[L];
        for (int i = 0; i < L; ++i) {
            m_lfoPhase[i] += m_lfoRate[i];
            if (m_lfoPhase[i] >= 1.0f)
                m_lfoPhase[i] -= 1.0f;
            const float mod = (0.0004f * m_rate) * std::sin(kTwoPi * m_lfoPhase[i]);
            float v = m_damp[i].step(m_lines[i].read(m_base[i] * m_size + mod));
            // Lows lose a little more on every pass than mids do, so a long
            // tail stays a shimmer above the music instead of a rumble under it.
            v -= 0.3f * m_lowLp[i].step(v);
            x[i] = v * m_gain[i];
        }
        // Fast Walsh–Hadamard transform, scaled to stay unitary.
        for (int h = 1; h < L; h <<= 1)
            for (int i = 0; i < L; i += h << 1)
                for (int j = i; j < i + h; ++j) {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        float oL = 0, oR = 0;
        for (int i = 0; i < L; ++i) {
            const float v = x[i] * 0.25f;
            m_lines[i].push(v + ((i & 1) ? r : l) * 0.35f);
            if (i & 1)
                oR += (i & 2) ? v : -v;
            else
                oL += (i & 2) ? v : -v;
        }
        outL = oL * 0.35f;
        outR = oR * 0.35f;
        m_shift.push(0.5f * (outL + outR));
    }

private:
    float m_rate = 48000, m_amount = 0, m_shimmer = 0, m_size = 1;
    Delay m_lines[L];
    float m_base[L] = {}, m_gain[L] = {}, m_lfoPhase[L] = {}, m_lfoRate[L] = {};
    OnePoleLp m_damp[L], m_lowLp[L];
    Allpass m_diffL[4], m_diffR[4];
    Delay m_pre, m_shift;
    float m_shiftPhase = 0, m_shiftWindow = 3000;
    OnePoleLp m_shimLp;
    Svf m_shimHp, m_inHpL, m_inHpR;
};

// A ping-pong tape echo. The delay time follows the tempo, and when it
// changes the heads slide to the new time the way tape does, bending pitch
// on the way rather than clicking.
class TapeEcho {
public:
    void init(float rate) {
        m_rate = rate;
        m_l.init(size_t(4.5f * rate));
        m_r.init(size_t(4.5f * rate));
        m_time.setTime(0.35f, rate);
        m_time.reset(0.375f * rate);
        m_hp.set(140.0f, 0.6f, rate);
        m_hp2.set(140.0f, 0.6f, rate);
        m_lpL.set(4200.0f, rate);
        m_lpR.set(4200.0f, rate);
    }
    void set(float amount, float seconds) {
        m_amount = amount;
        m_target = clampf(seconds, 0.02f, 4.0f) * m_rate;
        m_fb = 0.18f + 0.74f * amount;
    }
    inline void process(float inL, float inR, float &outL, float &outR) {
        if (m_amount < 0.001f && m_idle > m_rate * 6) {
            outL = outR = 0;
            return;
        }
        m_idle = m_amount < 0.001f ? m_idle + 1 : 0;
        const float t = m_time.step(m_target);
        m_wow += 0.55f / m_rate;
        m_flutter += 6.7f / m_rate;
        if (m_wow > 1)
            m_wow -= 1;
        if (m_flutter > 1)
            m_flutter -= 1;
        const float mod = m_rate * (0.0011f * std::sin(kTwoPi * m_wow) + 0.00012f * std::sin(kTwoPi * m_flutter));
        const float dl = m_l.read(t + mod), dr = m_r.read(t * 1.0f + mod * 0.8f);
        const float send = std::min(1.0f, m_amount * 1.6f);
        const float in = 0.5f * (inL + inR) * send;
        // Tape: highs and lows lost on every pass, and saturation instead of
        // runaway when the feedback is pushed past one.
        const float fl = fastTanh(1.2f * m_lpL.step(m_hp.step(dr).high) * m_fb);
        const float fr = fastTanh(1.2f * m_lpR.step(m_hp2.step(dl).high) * m_fb);
        m_l.push(in + fl * 0.95f);
        m_r.push(fr * 0.95f);
        outL = dl * 0.8f;
        outR = dr * 0.8f;
    }

private:
    float m_rate = 48000, m_amount = 0, m_target = 18000, m_fb = 0.3f;
    int m_idle = 0;
    Delay m_l, m_r;
    Smooth m_time;
    Svf m_hp, m_hp2;
    OnePoleLp m_lpL, m_lpR;
    float m_wow = 0, m_flutter = 0.3f;
};

// A grain cloud sprayed from the last few seconds of the instrument itself:
// tiny windowed slices replayed at octaves and fifths, forwards and backwards,
// scattered across the stereo field.
class GrainCloud {
public:
    static constexpr int kGrains = 64;
    void init(float rate) {
        m_rate = rate;
        m_l.init(size_t(4.2f * rate));
        m_r.init(size_t(4.2f * rate));
        for (int i = 0; i < 1024; ++i) {
            const float s = std::sin(kPi * i / 1023.0f);
            m_window[i] = s * s;
        }
    }
    void set(float density, float spread) {
        m_density = density;
        m_spread = spread;
    }
    inline void process(float inL, float inR, float &outL, float &outR) {
        m_l.push(inL);
        m_r.push(inR);
        outL = outR = 0;
        const float rate = m_density * m_density * 90.0f; // grains per second
        if (rate > 0.05f) {
            m_untilNext -= 1.0f;
            if (m_untilNext <= 0) {
                spawn();
                m_untilNext = m_rate / rate * (0.4f + 1.2f * m_rng.unit());
            }
        }
        if (!m_live)
            return;
        int live = 0;
        for (Grain &g : m_grains) {
            if (!g.on)
                continue;
            ++live;
            const float w = m_window[int(g.age * 1023.0f)];
            const float d = g.back;
            const float sl = m_l.read(d), sr = m_r.read(d);
            const float s = 0.5f * (sl + sr) * w * g.amp;
            outL += s * g.gl;
            outR += s * g.gr;
            g.back -= g.speed - 1.0f; // speed > 1 reads towards the present
            g.age += g.inc;
            if (g.age >= 1.0f || g.back < 2.0f || g.back > m_rate * 4.0f)
                g.on = false;
        }
        m_live = live;
        const float norm = 1.0f / std::sqrt(1.0f + 0.25f * float(live));
        outL *= norm;
        outR *= norm;
    }

private:
    struct Grain {
        bool on = false;
        float back = 0, speed = 1, age = 0, inc = 0, amp = 0, gl = 0, gr = 0;
    };
    void spawn() {
        for (Grain &g : m_grains) {
            if (g.on)
                continue;
            static const float ratios[8] = {1.0f, 2.0f, 0.5f, 1.5f, 0.75f, 4.0f / 3.0f, 3.0f, 0.25f};
            const int choices = 1 + int(m_spread * 7.99f);
            float speed = ratios[m_rng.next() % uint32_t(choices)];
            speed *= std::exp2(m_rng.bipolar() * m_spread * 0.012f);
            if (m_rng.unit() < m_spread * 0.3f)
                speed = -speed;
            const float len = (0.04f + 0.22f * m_rng.unit() * (1.0f - 0.5f * m_density)) * m_rate;
            const float travel = len * std::fabs(speed - 1.0f);
            g.back = std::min(m_rate * 3.8f, 0.03f * m_rate + travel + m_rng.unit() * 1.4f * m_rate);
            g.speed = speed;
            g.age = 0;
            g.inc = 1.0f / len;
            g.amp = 0.6f + 0.4f * m_rng.unit();
            const float pan = m_rng.bipolar() * 0.95f;
            g.gl = std::sqrt(0.5f * (1.0f - pan));
            g.gr = std::sqrt(0.5f * (1.0f + pan));
            g.on = true;
            ++m_live;
            return;
        }
    }
    float m_rate = 48000, m_density = 0, m_spread = 0, m_untilNext = 0;
    int m_live = 0;
    Delay m_l, m_r;
    Grain m_grains[kGrains];
    float m_window[1024];
    Rng m_rng{0xA11CE};
};

// Drive and wavefold, run at four times the sample rate and with first-order
// antiderivative anti-aliasing (the output is the slope of the shaper's
// integral between two samples, not the shaper at one point), so even a full
// fold of a high note stays harmonics rather than a fizz of aliases.
class Saturator {
public:
    float process(float x, float pre, float fold) {
        m_pre = pre;
        m_fold = fold;
        return m_outer.process(x, [&](float u) { return m_inner.process(u, [&](float v) { return adaa(v); }); });
    }

private:
    static float logCosh(float u) {
        const float a = std::fabs(u);
        return a + std::log1p(std::exp(-2.0f * a)) - 0.69314718f;
    }
    float shape(float u) const { return (1.0f - m_fold) * std::tanh(u) + m_fold * 0.8f * std::sin(1.7f * u); }
    float integral(float u) const {
        return (1.0f - m_fold) * logCosh(u) - m_fold * 0.8f * std::cos(1.7f * u) / 1.7f;
    }
    float adaa(float v) {
        const float u = v * m_pre;
        const float d = u - m_prev;
        const float y = std::fabs(d) > 1e-4f ? (integral(u) - integral(m_prev)) / d : shape(0.5f * (u + m_prev));
        m_prev = u;
        return y;
    }
    Oversampler2x m_outer, m_inner;
    float m_prev = 0, m_pre = 1, m_fold = 0;
};

// Lookahead brickwall limiter: sliding-window minimum of the gain each sample
// needs, smoothed by a boxcar as long as the lookahead, so the gain is down
// before the peak arrives and never steps.
class Limiter {
public:
    static constexpr int W = 64;
    void init(float rate) {
        m_release = 1.0f - std::exp(-1.0f / (0.12f * rate));
        std::fill(m_box, m_box + W, 1.0f);
        m_sum = W;
    }
    inline void process(float &l, float &r) {
        const float peak = std::max(std::fabs(l), std::fabs(r));
        const float need = peak > m_ceiling ? m_ceiling / peak : 1.0f;
        // Sliding minimum over the window, with a monotonic deque.
        while (m_count && m_dq[(m_head + m_count - 1) % W].v >= need)
            --m_count;
        m_dq[(m_head + m_count) % W] = {need, m_n};
        ++m_count;
        if (m_dq[m_head].n + W <= m_n) {
            m_head = (m_head + 1) % W;
            --m_count;
        }
        const float windowMin = m_dq[m_head].v;
        m_env = windowMin < m_env ? windowMin : m_env + (windowMin - m_env) * m_release;
        m_sum += m_env - m_box[m_boxPos];
        m_box[m_boxPos] = m_env;
        m_boxPos = (m_boxPos + 1) % W;
        const float g = std::min(1.0f, m_sum / W);
        m_delayL[m_dPos] = l;
        m_delayR[m_dPos] = r;
        m_dPos = (m_dPos + 1) % W;
        l = clampf(m_delayL[m_dPos] * g, -0.999f, 0.999f);
        r = clampf(m_delayR[m_dPos] * g, -0.999f, 0.999f);
        m_gain = g;
        ++m_n;
    }
    float gain() const { return m_gain; }

private:
    struct Entry {
        float v;
        uint64_t n;
    };
    Entry m_dq[W]{};
    int m_head = 0, m_count = 0;
    uint64_t m_n = 0;
    float m_env = 1, m_release = 0.001f, m_ceiling = 0.891f; // −1 dBFS
    float m_box[W] = {1}, m_sum = W;
    int m_boxPos = 0;
    float m_delayL[W] = {}, m_delayR[W] = {};
    int m_dPos = 0;
    float m_gain = 1;
};

}
