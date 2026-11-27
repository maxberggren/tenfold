#pragma once

// Acoustic feedback on the Vocal page's monitor, caught as it starts.
//
// On speakers the microphone hears the monitored voice again, and every
// frequency where the trip (chain, speaker, room, microphone) gains more
// than it loses grows on each pass until something gives: the howl. What
// it sounds like at the microphone is what tells it from singing:
//
//   one line     most of the band's energy within 6 % of one frequency
//                (often two or three of the room's modes, a trip's worth
//                of hertz apart, taking turns): a sung note spreads over
//                its harmonics, a howl is one of them, or none, run away;
//   where the    it may warble back and forth, but it doesn't slide: the
//   room put it  room and the delay set it, not a melody;
//   growing      while it builds it rises by the same factor every trip, a
//                straight line in dB that doesn't level off (a word's
//                onset rises and levels off; a vibrato swells a harmonic a
//                few dB, slower than 200 dB/s, and moves it 2–3 %); once
//                it has saturated it holds a level no voice holds;
//   ours         the monitor's own output carries the same line (a loop
//                needs the way out as much as the way in).
//
// Every 512 samples a 1024-point FFT of the microphone (the monitor's
// output rides in the imaginary half, so one transform gives both) finds
// the strongest bin between 150 Hz and 10 kHz, its cluster's share of the
// band's energy and its frequency (interpolated between the bins). Six
// frames in a row on the same line (64 ms), growing or saturated as above,
// with the output coherent at that line, make it a suspect.
//
// A note sung nearly pure can still look like that, so a suspect is never
// trusted on its looks: the loop is tested by taking it apart. The monitor
// is dipped 30 dB (a 6 ms fade, no click) and the microphone watched:
// feedback has nothing left to feed it and falls 10 dB within the trip and
// the room's decay; a voice sings on. Then the monitor comes back, and a
// howl comes back with it, 6 dB within a few trips, where a note that
// happened to end just then stays gone. Both, and the monitor is muted
// (latched) until acknowledge(): the page asks for headphones. Either
// missing (within 100 ms plus 1.5 × the round trip, and 43 ms more for
// the return), and the monitor is simply back; that frequency is then left
// alone while the note goes on (and 4 s at least), unless it grows 6 dB
// past where it was, which a voice doesn't and a howl must. So a wrong
// suspect costs a singer one short dip, never a mute.
//
// Real-time safe: fixed arrays, no allocation after init(); one FFT of
// 1024 points per 512 samples, about 0.5 % of a core.

#include <cmath>
#include <cstdint>

#include "dsp.h"

namespace tf {

class HowlGuard {
public:
    static constexpr int kN = 1024, kHop = 512, kTrack = 6, kRefusals = 4;

    void init(float rate) {
        m_rate = rate;
        m_hp.set(150.0f, 0.7f, rate);
        for (int i = 0; i < kN; ++i)
            m_win[i] = 0.5f - 0.5f * std::cos(kTwoPi * (float(i) + 0.5f) / float(kN));
        int bits = 0;
        while ((1 << bits) < kN)
            ++bits;
        for (int i = 0; i < kN; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            m_rev[i] = r;
        }
        for (int j = 0; j < kN / 2; ++j) {
            m_twr[j] = std::cos(-kTwoPi * float(j) / float(kN));
            m_twi[j] = std::sin(-kTwoPi * float(j) / float(kN));
        }
        m_df = rate / float(kN);
        m_lo = std::max(3, int(150.0f / m_df));
        m_hi = std::min(kN / 2 - 3, int(10000.0f / m_df));
        // A full-scale sine's power in its main lobe (one side, Hann):
        // Parseval over A²/2 · Σw² = 3N/16, times N, halved.
        m_full = 3.0f * float(kN) * float(kN) / 32.0f;
        m_down = 1.0f / (0.006f * rate);
        m_up = 1.0f / (0.03f * rate);
        setLatencyMs(20);
        reset();
    }

    // The round trip from the speakers back to the microphone, as far as
    // the audio server says: how long a probe waits for the fall.
    void setLatencyMs(float ms) {
        const float secs = clampf(0.1f + 1.5f * std::max(0.0f, ms) / 1000.0f, 0.1f, 0.6f);
        m_probeLimit = std::max(2, int(secs * m_rate / float(kHop) + 0.5f));
    }

    // The player has heard the warning: the monitor may come back.
    void acknowledge() {
        m_latched = false;
        m_probe = false;
        m_verify = false;
        m_n = 0;
        for (auto &r : m_refused)
            r.until = 0;
    }

    void reset() {
        acknowledge();
        m_gain = 1;
        m_ramp = 1;
        m_count = 0;
        for (auto &x : m_mic)
            x = 0;
        for (auto &x : m_out)
            x = 0;
    }

    bool latched() const { return m_latched; }
    bool probing() const { return m_probe || m_verify; }
    float hz() const { return m_latched || probing() ? m_probeHz : 0.0f; }
    int probes() const { return m_probes; }
    int catches() const { return m_catches; }

    // One sample: the microphone as it arrived, the monitored voice as it
    // leaves for the speakers (mono), and whether a loop is possible at
    // all (the monitor is on and the microphone is what it hears). The
    // gain the monitor gets at this sample.
    inline float step(float mic, float out, bool armed) {
        m_mic[m_w] = m_hp.step(mic).high;
        m_out[m_w] = out;
        m_w = (m_w + 1) & (kN - 1);
        if (++m_count >= kHop) {
            m_count = 0;
            analyse(armed);
        }
        // A probe dips the monitor 30 dB (enough to break the loop, and a
        // dip, not a gap, if it was a voice); a howl caught mutes it.
        const float target = m_latched ? 0.0f : m_probe ? 0.1f : 1.0f;
        if (m_ramp > target)
            m_ramp = std::max(target, m_ramp - m_down);
        else if (m_ramp < target)
            m_ramp = std::min(target, m_ramp + m_up);
        m_gain = m_ramp * m_ramp * (3.0f - 2.0f * m_ramp);
        return m_gain;
    }

private:
    void analyse(bool armed) {
        ++m_hop;
        float *re = m_re, *im = m_im;
        for (int i = 0; i < kN; ++i) {
            const int k = (m_w + i) & (kN - 1);
            re[i] = m_mic[k] * m_win[i];
            im[i] = m_out[k] * m_win[i];
        }
        fft(re, im);
        // The two spectra apart: the microphone's X[k] = (Z[k] + Z*[N−k])/2,
        // the output's Y[k] = (Z[k] − Z*[N−k])/2i; their powers.
        float *mic = m_pm, *out = m_po;
        float E = 0, Eout = 0, best = -1;
        int kb = m_lo;
        for (int k = m_lo - 2; k <= m_hi + 2; ++k) {
            const float ar = re[k], ai = im[k], br = re[kN - k], bi = -im[kN - k];
            const float xr = 0.5f * (ar + br), xi = 0.5f * (ai + bi);
            const float yr = 0.5f * (ai - bi), yi = -0.5f * (ar - br);
            mic[k] = xr * xr + xi * xi;
            out[k] = yr * yr + yi * yi;
            if (k < m_lo || k > m_hi)
                continue;
            E += mic[k];
            Eout += out[k];
            if (mic[k] > best) {
                best = mic[k];
                kb = k;
            }
        }
        // A line's power: its bins within 6 % either way (at least the
        // window's main lobe). A howl is often two or three of the room's
        // modes, a trip's worth of hertz apart, taking turns: one cluster.
        auto cluster = [&](const float *P, int k) {
            const int w = std::max(2, int(float(k) * 0.06f + 0.5f));
            float p = 0;
            for (int j = std::max(m_lo - 2, k - w); j <= std::min(m_hi + 2, k + w); ++j)
                p += P[j];
            return p;
        };
        auto db = [&](float p) { return 10.0f * std::log10(p / m_full + 1e-30f); };
        auto near = [&]() { return db(cluster(mic, m_probeBin)); };
        // A probe: the monitor is dipped, so a howl must fall...
        if (m_probe) {
            const float now = near();
            if (now <= m_probeLevel - kFallDb) {
                m_probe = false;
                m_verify = true;
                m_fallLevel = now;
                m_probeHops = 0;
            } else if (++m_probeHops >= m_probeLimit) {
                m_probe = false;
                refuse(m_probeHz, m_probeLevel);
            }
            return;
        }
        // ...and, given the monitor back, come back: a note that happened
        // to end just then doesn't.
        if (m_verify) {
            const float now = near();
            m_fallLevel = std::min(m_fallLevel, now);
            if (now >= m_fallLevel + kReturnDb) {
                m_verify = false;
                m_latched = true;
                ++m_catches;
            } else if (++m_probeHops >= m_probeLimit + kVerifyHops) {
                m_verify = false;
                refuse(m_probeHz, m_probeLevel);
            }
            return;
        }
        if (m_latched)
            return;
        if (E <= m_full * 1e-9f) { // under −90 dBFS: nothing to follow
            m_n = 0;
            return;
        }
        const float line = cluster(mic, kb), share = std::min(1.0f, line / E), level = db(line);
        // Between the bins: a parabola through the log powers.
        const float a = std::log(mic[kb - 1] + 1e-30f), b = std::log(mic[kb] + 1e-30f),
                    c = std::log(mic[kb + 1] + 1e-30f);
        const float den = a - 2.0f * b + c;
        const float hz = (float(kb) + (den < 0 ? clampf(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f)) * m_df;

        // The same line as the frame before, or a new one.
        const float tol = std::max(1.5f * m_df, 0.07f * m_trackHz);
        if (m_n == 0 || std::fabs(hz - m_trackHz) > tol)
            m_n = 0;
        const int slot = m_n % kTrack;
        m_hzs[slot] = hz;
        m_levels[slot] = level;
        m_shares[slot] = share;
        ++m_n;
        m_trackHz = hz;
        if (m_n < kTrack || !armed || level < kFloorDb)
            return;

        // Over the last six frames: how far it moved, how it grew.
        float lo = 1e9f, hi = 0, minShare = 1, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int j = 0; j < kTrack; ++j) {
            const int s = (m_n - kTrack + j) % kTrack;
            lo = std::min(lo, m_hzs[s]);
            hi = std::max(hi, m_hzs[s]);
            minShare = std::min(minShare, m_shares[s]);
            const float x = float(j), y = m_levels[s];
            sx += x, sy += y, sxx += x * x, sxy += x * y;
        }
        const float n = float(kTrack);
        const float slope = (n * sxy - sx * sy) / (n * sxx - sx * sx); // dB per frame
        const float icpt = (sy - slope * sx) / n;
        float resid = 0;
        int falls = 0; // frames quieter than the one before
        for (int j = 0; j < kTrack; ++j) {
            const float y = m_levels[(m_n - kTrack + j) % kTrack];
            const float e = y - (icpt + slope * float(j));
            resid += e * e;
            if (j > 0 && y < m_levels[(m_n - kTrack + j - 1) % kTrack] - 0.3f)
                ++falls;
        }
        resid = std::sqrt(resid / n);
        const float perSec = slope * m_rate / float(kHop);
        // Still rising as fast at the end as at the start: a howl grows
        // by the same factor every trip (a straight line in dB), where a
        // word's onset rises and levels off.
        auto at = [&](int j) { return m_levels[(m_n - kTrack + j) % kTrack]; };
        const float early = (at(2) - at(0)) * 0.5f, late = (at(kTrack - 1) - at(kTrack - 3)) * 0.5f;
        const bool keepsRising = late >= 0.6f * early && late * m_rate / float(kHop) >= 20.0f;
        // It may warble (the room's modes taking turns, the chain's limiter
        // riding it): within 6 or 7 %. Growing, it needn't be all that is
        // heard yet; saturated, it is nearly all of it.
        const float spread = (hi - lo) / hz;
        // (Slowly, under 200 dB/s, it must hold still, under 1 %: a held
        // note's vibrato swells its harmonics by a few dB at a time, moving
        // them 2–3 %. Faster than any vibrato swells, it may warble.)
        const bool growing = keepsRising && resid <= 1.5f && falls == 0 && minShare >= 0.35f &&
                             ((perSec >= 200.0f && spread <= 0.07f) || (perSec >= 30.0f && spread <= 0.01f));
        // (Neither while it fades: a note let go isn't a suspect.)
        // (Nor while it slides: a howl may warble, back and forth, but it
        // stays where the room put it.)
        const bool sliding = std::fabs(m_hzs[(m_n - 1) % kTrack] - m_hzs[(m_n - kTrack) % kTrack]) > 0.015f * hz;
        const bool saturated = spread <= 0.06f && !sliding && resid <= 2.0f && perSec > -12.0f && falls <= 1 &&
                               minShare >= 0.75f && level >= kSteadyDb;
        if (!growing && !saturated)
            return;
        // Ours: the monitor carries the same line, a fair share of what it
        // sends (a loop needs the way out).
        if (Eout <= m_full * 1e-9f || cluster(out, kb) < 0.25f * Eout)
            return;
        if (refused(hz, level))
            return;
        m_probe = true;
        m_probeHz = hz;
        m_probeBin = kb;
        m_probeLevel = level;
        m_probeHops = 0;
        ++m_probes;
    }

    // A frequency where a probe found a voice, not a howl, left alone while
    // the note goes on.
    struct Refusal {
        float hz = 0, level = 0;
        int64_t since = 0, until = 0;
    };
    void refuse(float hz, float level) {
        Refusal *r = &m_refused[0];
        for (auto &x : m_refused)
            if (x.until < r->until)
                r = &x;
        const int64_t hops = int64_t(m_rate / float(kHop));
        *r = {hz, level, m_hop, m_hop + 4 * hops};
    }
    bool refused(float hz, float level) {
        const int64_t hops = int64_t(m_rate / float(kHop));
        for (auto &r : m_refused)
            if (m_hop < r.until && std::fabs(hz - r.hz) < 0.06f * r.hz) {
                if (level > r.level + 6.0f)
                    return false;
                r.until = std::max(r.until, m_hop + hops); // still the note: a second more
                return true;
            }
        return false;
    }

    void fft(float *re, float *im) const {
        for (int i = 0; i < kN; ++i) {
            const int j = m_rev[i];
            if (j > i) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int len = 2; len <= kN; len <<= 1) {
            const int half = len >> 1, step = kN / len;
            for (int i = 0; i < kN; i += len)
                for (int j = 0; j < half; ++j) {
                    const float wr = m_twr[j * step], wi = m_twi[j * step];
                    const int a = i + j, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr;
                    im[b] = im[a] - xi;
                    re[a] += xr;
                    im[a] += xi;
                }
        }
    }

    static constexpr float kFloorDb = -55.0f;  // a line quieter than this is no howl yet
    static constexpr float kSteadyDb = -45.0f; // nor, not growing, is one under this
    static constexpr float kFallDb = 10.0f;    // what a probe's dip must take off it
    static constexpr float kReturnDb = 6.0f;   // and what the monitor back must put on again
    static constexpr int kVerifyHops = 4;      // (and 43 ms more for the fade back in)

    float m_rate = 48000, m_df = 46.875f, m_full = 1;
    int m_lo = 3, m_hi = 200;
    Svf m_hp;
    float m_win[kN] = {}, m_twr[kN / 2] = {}, m_twi[kN / 2] = {};
    int m_rev[kN] = {};
    float m_mic[kN] = {}, m_out[kN] = {};
    float m_re[kN] = {}, m_im[kN] = {}, m_pm[kN / 2] = {}, m_po[kN / 2] = {}; // a frame's working
    int m_w = 0, m_count = 0;
    int64_t m_hop = 0;
    // The line followed: its last frames.
    float m_hzs[kTrack] = {}, m_levels[kTrack] = {}, m_shares[kTrack] = {};
    int m_n = 0;
    float m_trackHz = 0;
    bool m_probe = false, m_verify = false, m_latched = false;
    float m_probeHz = 0, m_probeLevel = 0, m_fallLevel = 0;
    int m_probeBin = 0, m_probeHops = 0, m_probeLimit = 10;
    int m_probes = 0, m_catches = 0;
    Refusal m_refused[kRefusals];
    float m_ramp = 1, m_gain = 1, m_down = 0.004f, m_up = 0.0007f;
};

}
