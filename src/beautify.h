#pragma once

// The voice made beautiful: the BEAUTIFY style of the Vocal page. Someone who
// can't hold a note sings, and what comes back in the headphones is them,
// in tune, steady, doubled, polished and in a room: the way a record makes a
// voice sound, at once, while they sing. Why it is made this way, and what
// else was weighed, is in docs/beautify-research.md; what it measures is in
// docs/beautify.md.
//
//   the pitch    the page's tracker (YIN) says there is a pitch and about
//                where; it is read again at the full rate over the last
//                period alone, ~10 ms sooner. An octave slip is folded
//                back unless it lasts a sixth of a second.
//   the note     nothing is decided while the voice moves from note to
//                note: that step is the singer's own, heard as sung (as
//                Auto-Tune's Flex-Tune leaves it). Once it settles, the note
//                is the key's (or, with follow, the chord's) nearest to
//                where the voice has been since; halfway between two, the
//                one it was heading for (up, into a phrase: a scoop comes
//                from below). For 0.3 s it may still change its mind; after
//                that a held note is kept unless a quarter-second centre of
//                the voice moves well past the halfway mark, so a wobble
//                never flips it.
//   the tuning   the voice drawn onto the note as it settles, in 15–45 ms:
//                its slow error (sharp, flat, drifting, wobbling) taken out,
//                a little (15 %) of its quick movement kept for life. A held
//                note blooms from 0.28 s into a gentle vibrato (±16 cents,
//                5.4 Hz, never quite even); if the voice has a vibrato of
//                its own (a steady 4.5–7.5 Hz swing over any slower
//                wobble), most of that is kept instead.
//   the shifter  TD-PSOLA (vocal.h's Shifter): grains two periods long, laid
//                out at the new period. The vowels are the singer's own,
//                untouched (no formant moves: it is them, in tune), and the
//                voice comes out on average half a period late (2.3 ms at
//                220 Hz, at most one period: 9 ms at 110 Hz). What has no
//                pitch (consonants, breath) goes straight through, with no
//                delay at all.
//   the polish   breaths between words (no pitch, quiet, dark) brought
//                down as much as 12 dB; the mud at 280 Hz cut, presence at
//                3 kHz and air over 10 kHz lifted; the esses (5.5 kHz up)
//                held down when they stand out; and (B) two doubles, each
//                its own singer (10–35 ms behind and wandering, a few cents
//                off and drifting, a throat 2 % apart), 7 dB under, wide
//                left and right; or a harmony a third over the note too;
//                or neither (solo); then a vocal plate, 1.6 s, 18 ms
//                before it.
//
// Everything is at the studio's rate, sample by sample: no FFT, no
// look-ahead, no frame to wait for.

#include <algorithm>
#include <cmath>

#include "dsp.h"
#include "fx.h"
#include "vocal.h"

namespace tf::beautify {

// What sings with the voice (B).
enum Layers { Doubled, Harmony, Solo, LayersCount };
inline const char *layersName(int l) {
    static const char *names[] = {"DOUBLED", "HARMONY", "SOLO"};
    return names[std::clamp(l, 0, LayersCount - 1)];
}
inline const char *layersKey(int l) {
    static const char *names[] = {"doubled", "harmony", "solo"};
    return names[std::clamp(l, 0, LayersCount - 1)];
}

// How much of the voice's quick movement (between ~3 and ~10 Hz) is kept,
// unless it is a vibrato: a little life, hardly any of the wobble (a voice
// that can't hold still is all wobble; its life comes back as the held
// note's vibrato).
constexpr float kKeep = 0.15f;
// Faster than this (semitones a second, over 20 ms) the voice is moving to
// a note, not wobbling round one.
constexpr float kMoving = 10.0f;
// The vibrato a held note blooms into: ±16 cents at 5.4 Hz, from 0.28 s on,
// full by 0.8 s (a pop singer's, lighter than an opera singer's half tone).
constexpr float kVibDepth = 0.16f, kVibRate = 5.4f, kVibFrom = 0.28f, kVibRise = 0.5f;
// The doubles: each singer a few cents off and a little late, quieter.
constexpr float kDouble = 0.42f, kHarmony = 0.6f;
// The plate's return.
constexpr float kPlate = 0.45f;

inline float midiOf(float hz) { return 69.0f + 12.0f * std::log2(std::max(1.0f, hz) / 440.0f); }
// A one-pole's step for dt seconds at time constant tau.
inline float pole(float dt, float tau) { return 1.0f - std::exp(-dt / tau); }

class Beautifier {
public:
    void init(float rate) {
        m_rate = rate;
        m_plate.init(rate);
        m_plate.set(0.33f, 0.0f);
        m_sendHp.set(220.0f, 0.7f, rate);
        for (int c = 0; c < 2; ++c) {
            m_mud[c].set(280.0f, 1.0f, rate);
            m_presence[c].set(3000.0f, 0.8f, rate);
            m_air[c].set(8000.0f, 0.6f, rate);
            m_split[c].set(5500.0f, 0.7f, rate);
        }
        m_sibDetect.set(5500.0f, 0.7f, rate);
        m_breathHp.set(4000.0f, 0.7f, rate);
        m_dblLp[0].set(7000.0f, rate);
        m_dblLp[1].set(7000.0f, rate);
        m_wet.setTime(0.006f, rate);
        m_layer.setTime(0.02f, rate);
        m_breath.setTime(0.015f, rate);
        m_harmLvl.setTime(0.04f, rate);
        reset();
    }
    void reset() {
        m_lead.reset();
        for (auto &d : m_dbl)
            d.reset();
        m_harm.reset();
        m_dbl[0].setDelay(0.012f * m_rate);
        m_dbl[1].setDelay(0.019f * m_rate);
        m_harm.setDelay(0.008f * m_rate);
        m_note = -1;
        m_silent = 1;
        m_ratio = 1;
        m_out = -1;
        m_wet.reset(0);
        m_layer.reset(0);
        m_breath.reset(1);
        m_harmLvl.reset(0);
        m_vibDepth = 0;
        m_sibEnv = m_allEnv = 0;
        m_vEnv = 0.02f;
        m_uEnv = m_hEnv = 0;
    }
    void setLayers(int l) { m_layers = std::clamp(l, 0, LayersCount - 1); }
    int layers() const { return m_layers; }

    // The note being sung onto (MIDI; −1: none), the voice's own pitch as
    // heard, and the pitch it comes out at.
    float note() const { return m_note; }
    float sung() const { return m_silent > 0 ? -1.0f : m_p; }
    float heard() const { return m_silent > 0 ? -1.0f : m_out; }
    // How far behind the singer the lead voice is now (seconds), while it
    // is being moved (0 while it goes straight through).
    float delay() const { return m_wet.y > 0.5f ? m_lead.lag() / m_rate : 0.0f; }

    // One block. src: the cleaned microphone the shifters read (its newest
    // sample dry[n − 1]); hz and period: the tracker's (0: no pitch);
    // voicedNow: the tracker hears a pitch this block; formant: the page's
    // throat (1: the voice's own). key.snap(midi) is the nearest note of
    // the key (or chord), key.above(midi) the harmony over a note. The
    // result is added into out (×mix, moving from mix0 to mix1).
    template <class Key>
    void process(const vocal::Source &src, const float *dry, int n, float hz, float period, bool voicedNow,
                 float formant, const Key &key, float *outL, float *outR, float mix0, float mix1) {
        const float dt = float(n) / m_rate;
        // An octave slip of the tracker's (a low "o" heard an octave up for
        // a moment) is folded back unless it lasts a sixth of a second: a
        // real leap.
        if (voicedNow && period > 0 && m_gap < 0.03f && m_period > 0) {
            const float r = period / m_period;
            const float oct = r > 1.8f && r < 2.2f ? 0.5f : r > 0.45f && r < 0.556f ? 2.0f : 1.0f;
            if (oct != 1.0f && (m_octT += dt) < 0.16f) {
                period *= oct;
                hz /= oct;
            } else if (oct == 1.0f) {
                m_octT = 0;
            }
        } else {
            m_octT = 0;
        }
        // The pitch, read again at the full rate over the last period
        // alone: the tracker's own estimate is ~15 ms behind the voice.
        if (voicedNow && period > 0) {
            if ((++m_tick & 1) == 0 || m_fine <= 0)
                m_fine = refine(src, period);
            if (m_fine > 0)
                period = m_fine, hz = m_rate / m_fine;
        } else {
            m_fine = 0;
        }
        control(voicedNow ? hz : 0.0f, dt, key);
        // The shifted voice sounds while there is a pitch, and through a
        // gap of under 40 ms in it (a tracker's dropout inside a vowel):
        // a consonant or a breath goes straight through.
        const bool shifted = m_note > 0 && (voicedNow || m_gap < 0.04f);
        // (The grains' period moves at most 3 % a block: a tracker's jump
        // never makes one grain twice as long as the last.)
        float P = period > 0 ? period : m_period;
        if (m_gap < 0.03f && m_lastP > 0)
            P = clampf(P, m_lastP * 0.97f, m_lastP * 1.03f);
        m_lastP = P;
        if (period > 0)
            m_period = period;
        float lead[kMaxBlock] = {}, dbl[2][kMaxBlock] = {}, harm[kMaxBlock] = {};
        const bool layered = m_layers != Solo;
        if (shifted || m_wet.y > 0.001f) {
            m_lead.process(src, n, P, m_ratio, formant, 1.0f, lead);
            if (!layered && m_layer.y <= 0.001f)
                for (auto &d : m_dbl)
                    d.reset();
            if (layered || m_layer.y > 0.001f)
                for (int k = 0; k < 2; ++k) {
                    // Each its own singer: a few cents off, drifting, and
                    // its own way behind (10–35 ms, wandering slowly).
                    const float cents = (k ? -0.08f : 0.09f) + 0.05f * std::sin(m_dPh[k]) +
                                        0.04f * std::sin(2.7f * m_dPh[k] + 1.0f);
                    m_dPh[k] += kTwoPi * (k ? 0.19f : 0.23f) * dt;
                    m_dDel[k] += kTwoPi * (k ? 0.07f : 0.11f) * dt;
                    m_dbl[k].setDelay(((k ? 0.026f : 0.018f) + (k ? 0.008f : 0.007f) * std::sin(m_dDel[k])) * m_rate);
                    // (Their pitch is their own: the note, their own
                    // vibrato, not the lead's in step with it, which beats.)
                    const float r = std::exp2((m_dblOut[k] + cents - m_p) / 12.0f);
                    m_dbl[k].process(src, n, P, m_out > 0 ? clampf(r, 0.5f, 2.0f) : 1.0f, formant * (k ? 1.02f : 0.98f),
                                     1.0f, dbl[k]);
                }
            if (m_layers == Harmony || m_harmLvl.y > 0.001f) {
                const float r = m_out > 0 ? std::exp2((m_harmOut - m_p) / 12.0f) : 1.0f;
                m_harm.process(src, n, P, r, formant, 1.0f, harm);
            }
        } else {
            m_lead.reset();
            for (auto &d : m_dbl)
                d.reset();
            m_harm.reset();
        }
        for (int i = 0; i < n; ++i) {
            const float mix = mix0 + (mix1 - mix0) * float(i + 1) / float(n);
            const float x = dry[i];
            const float w = m_wet.step(shifted ? 1.0f : 0.0f);
            const float lay = m_layer.step(shifted && layered ? 1.0f : 0.0f);
            const float hl = m_harmLvl.step(shifted && m_layers == Harmony ? 1.0f : 0.0f);
            // Breath: between words, what has no pitch, is under the
            // singing (by its peaks, 4 dB and more) and darker than a
            // consonant (an s, a sh, a t is nearly all over 4 kHz) is
            // brought down 9 dB.
            const float ax = std::fabs(x), ah = std::fabs(m_breathHp.step(x).high);
            m_uEnv += (ax - m_uEnv) * (ax > m_uEnv ? 0.02f : 0.002f);
            m_hEnv += (ah - m_hEnv) * (ah > m_hEnv ? 0.02f : 0.002f);
            // (The singing's level: up over 100 ms, down only over seconds,
            // so a note's own fading end doesn't count.)
            if (shifted)
                m_vEnv += (m_uEnv - m_vEnv) * (m_uEnv > m_vEnv ? 0.0002f : 0.00001f);
            const bool breath = !shifted && m_uEnv < 0.6f * m_vEnv && m_hEnv < 0.75f * m_uEnv;
            const float br = m_breath.step(breath ? 0.25f : 1.0f);
            const float v = x * (1.0f - w) * br + lead[i] * w;
            const float d0 = m_dblLp[0].step(dbl[0][i]) * lay * kDouble, d1 = m_dblLp[1].step(dbl[1][i]) * lay * kDouble;
            const float hv = harm[i] * hl * kHarmony;
            // The doubles wide, the harmony a little right of centre.
            float ch[2] = {v + d0 * 0.99f + d1 * 0.15f + hv * 0.6f, v + d0 * 0.15f + d1 * 0.99f + hv * 0.8f};
            // The esses: the band over 5.5 kHz, when it stands out of the
            // whole by more than it does in a vowel, brought down by as much
            // (to −12 dB), at once, and let back over 40 ms.
            const float sib = m_sibDetect.step(v).high, as = std::fabs(sib), av = std::fabs(v);
            m_sibEnv += (as - m_sibEnv) * (as > m_sibEnv ? 0.05f : 0.0006f);
            m_allEnv += (av - m_allEnv) * (av > m_allEnv ? 0.05f : 0.0006f);
            // (Against the singing's own level too, not only the sound's: an
            // s alone is all esses.)
            const float over = m_sibEnv / std::max(1e-6f, std::min(0.45f * m_allEnv, 0.2f * m_vEnv));
            const float ess = over > 1.0f ? clampf(std::pow(over, -0.8f), 0.25f, 1.0f) : 1.0f;
            for (int c = 0; c < 2; ++c) {
                float y = ch[c];
                y += -0.25f * m_mud[c].k * m_mud[c].step(y).band;      // −2.5 dB at 280 Hz
                y += 0.19f * m_presence[c].k * m_presence[c].step(y).band; // +1.5 dB at 3 kHz
                y += 0.75f * m_air[c].step(y).high;                    // +5 dB over 8 kHz
                const float lo = m_split[c].step(y).low;
                ch[c] = lo + (y - lo) * ess;
            }
            float rl = 0, rr = 0;
            const float send = m_sendHp.step(0.5f * (ch[0] + ch[1])).high;
            m_plate.process(send, send, rl, rr);
            // Its peaks kept under the studio's own limiter (whose instant
            // grip on a peak ticks): a gain down within 0.2 ms, up over
            // 60 ms, no look-ahead (no delay).
            float l = (ch[0] + rl * kPlate) * kGain, r = (ch[1] + rr * kPlate) * kGain;
            const float pk = std::max(std::fabs(l), std::fabs(r));
            const float want = pk > kPeak ? kPeak / pk : 1.0f;
            m_pkGain += (want - m_pkGain) * (want < m_pkGain ? 0.1f : 0.0004f);
            const float gg = std::min(m_pkGain, pk > 0.6f ? 0.6f / pk : 1.0f) * mix; // (and never over 0.6)
            outL[i] += l * gg;
            outR[i] += r * gg;
        }
    }

    // The period (in samples, fractional) of the newest stretch of src, one
    // period long (at least 4 ms), as the lag within three semitones of the
    // tracker's `coarse` where it best matches itself (its normalised cross-
    // correlation, every other or fourth sample of it; between the lags, a
    // parabola).
    // 0: no clear match there (the tracker's then).
    static float refine(const vocal::Source &src, float coarse) {
        const int W = std::max(192, int(coarse));
        const int lo = std::max(16, int(coarse / 1.19f)), hi = std::min(int(coarse * 1.19f) + 1, 1400);
        const int64_t end = src.w;
        if (end < int64_t(W + hi + 2) || W + hi + 2 >= vocal::Source::kSize)
            return 0;
        float r[1404];
        // (Every other sample; every fourth of a long period, where there
        // are plenty: a low voice costs no more than a high one.)
        const int step = W > 384 ? 4 : 2;
        int best = -1;
        float bestR = 0;
        for (int L = lo - 1; L <= hi + 1; ++L) {
            float xy = 0, xx = 0, yy = 0;
            for (int j = 0; j < W; j += step) {
                const float a = src.buf[(end - W + j) & (vocal::Source::kSize - 1)];
                const float b = src.buf[(end - W - L + j) & (vocal::Source::kSize - 1)];
                xy += a * b;
                xx += a * a;
                yy += b * b;
            }
            r[L - lo + 1] = xy / std::sqrt(xx * yy + 1e-20f);
            if (L >= lo && L <= hi && r[L - lo + 1] > bestR) {
                bestR = r[L - lo + 1];
                best = L;
            }
        }
        if (best <= lo || best >= hi || bestR < 0.8f)
            return 0;
        const float y0 = r[best - lo], y1 = r[best - lo + 1], y2 = r[best - lo + 2];
        const float den = y0 - 2 * y1 + y2;
        return float(best) + (std::fabs(den) > 1e-9f ? clampf(0.5f * (y0 - y2) / den, -0.5f, 0.5f) : 0.0f);
    }

private:
    static constexpr int kMaxBlock = 64;
    // Doubles and a harmony make the voice fuller, not louder.
    static constexpr float kGain = 1.0f, kPeak = 0.42f;

    template <class Key> void control(float hz, float dt, const Key &key) {
        if (hz <= 0) {
            m_gap += dt;
            m_silent = m_gap > 0.15f ? 1 : 0;
            if (m_silent)
                m_note = -1;
            return;
        }
        const float raw = midiOf(hz);
        m_gap = 0;
        if (m_note < 0) {
            // A phrase's first pitch: the nearest note, for now (nothing is
            // corrected until the voice has settled, below).
            m_note = float(key.snap(raw));
            m_ps = m_d = m_c1 = m_c2 = m_pPrev = m_avg = raw;
            m_vel = 0;
            m_settled = 0;
            m_avgN = 0;
            m_amt = 0;
            m_g1 = m_g2 = m_note;
            m_onNote = 0;
            m_onset = 0;
            m_candT = 0;
            m_cand = -1;
            m_vibDepth = 0;
            m_f = 0;
            m_dir = 1; // (a phrase is scooped into from below)
            m_from = raw;
            m_vb.reset();
            m_lb.reset();
            m_vibRms = m_lowRms = 0;
        }
        // (A reading's own jitter, a few cents, smoothed over 4 ms.)
        m_ps += (raw - m_ps) * pole(dt, 0.004f);
        const float p = m_ps;
        m_silent = 0;
        m_p = p;
        m_onset += dt;
        // How fast the voice is going (over 12 ms, from the readings
        // themselves), and whether it is moving from note to note: over 10
        // semitones a second (4 in a phrase's first 150 ms, where a scoop is
        // slower than a step).
        m_d += (p - m_d) * pole(dt, 0.012f);
        m_vel += (std::fabs(p - m_pPrev) / dt - m_vel) * pole(dt, 0.012f);
        m_pPrev = p;
        // (Or, slower, a voice whose centre (~3 Hz) is clearly at another
        // note, under 40 % of the step from it, for 60 ms: a slow step is a
        // step too. Read any quicker, a wobble would be taken for steps.)
        const float there = m_note > 0 ? float(key.snap(m_c2)) : -1.0f;
        const bool away = there > 0 && std::fabs(there - m_note) > 0.01f &&
                          std::fabs(m_c2 - there) < 0.4f * std::fabs(there - m_note);
        m_away = away ? m_away + dt : 0.0f;
        const bool moving = m_vel > (m_onset < 0.15f ? 5.0f : kMoving) || (m_away > 0.06f && m_settled > 0.3f);
        if (moving) {
            if (m_settled > 0)
                m_from = p; // where this movement set out from
            m_settled = 0;
            m_avgN = 0;
            if (std::fabs(p - m_from) > 0.3f)
                m_dir = p > m_from ? 1.0f : -1.0f;
        } else {
            m_settled += dt;
            // Where the voice has been since it settled: its plain average
            // over the first 150 ms, then one over the last 250 (a wobble's
            // period and more, so a held note's centre doesn't wobble).
            ++m_avgN;
            const float k = m_settled < 0.15f ? 1.0f / float(m_avgN) : pole(dt, 0.25f);
            m_avg += (p - m_avg) * k;
        }
        // The note it means. Nothing is decided while the voice moves: it
        // is only being corrected once it has settled. Then, for 0.3 s, the
        // note is the nearest to where it has been since (a voice halfway
        // between two goes the way it was going: up, into a phrase); after
        // that a held note is kept against a wobble, and only a centre
        // well past the halfway mark (under 35 % of the step from the next
        // note) for 80 ms has moved on. (Only a phrase's first note leans
        // the way the voice came: a scoop. A step that lands halfway is
        // as often an overshoot.)
        if (!moving && m_settled >= 0.025f) {
            const bool young = m_settled < 0.3f;
            // (Young, where the voice is now, over 12 ms: a short note's
            // drift is not a wobble to be averaged out. Held, its centre.)
            const float at = young ? m_d : m_avg;
            float cand = float(key.snap(at));
            const float next = float(key.snap(cand + m_dir * 1.1f));
            if (young && m_onset < 0.4f && std::fabs(next - cand) > 0.01f && (next - cand) * m_dir > 0 &&
                std::fabs(at - 0.5f * (cand + next)) < 0.1f)
                cand = next;
            if (std::fabs(cand - m_note) > 0.01f) {
                const float frac = std::fabs(at - cand) / std::fabs(cand - m_note);
                if (std::fabs(cand - m_cand) > 0.01f) {
                    m_cand = cand;
                    m_candT = 0;
                }
                m_candT += dt;
                // (Young, a few ms to be sure while it is already being
                // corrected; none before.)
                // (The first decision after settling is free; a young note
                // then changes only to a neighbour the voice is clearly
                // nearer, for 40 ms; an old one as above.)
                const bool go = young ? (m_amt < 0.3f || (frac < 0.42f && m_candT > 0.04f))
                                      : (frac < 0.35f && m_candT > 0.08f);
                if (go) {
                    m_note = cand;
                    m_onNote = 0;
                    m_cand = -1;
                    m_candT = 0;
                }
                if (frac >= (young ? 0.42f : 0.35f))
                    m_candT = 0;
            } else {
                m_cand = -1;
                m_candT = 0;
            }
        }
        // The voice's own movement round its centre, between ~3 and ~10 Hz
        // (a little of it kept, for life), and whether it is a vibrato of
        // its own: a steady 4.5–7.5 Hz swing, stronger than any slower
        // wobble under it.
        const float tc = m_settled < 0.15f ? 0.02f : 0.05f;
        m_c1 += (p - m_c1) * pole(dt, tc);
        m_c2 += (m_c1 - m_c2) * pole(dt, tc);
        m_f += (clampf(p - m_c2, -0.6f, 0.6f) - m_f) * pole(dt, 0.016f);
        const float f = m_f;
        const float ctl = 1.0f / dt;
        m_vb.set(5.8f, 2.0f, ctl);
        m_lb.set(2.8f, 1.5f, ctl);
        const float vb = m_vb.step(p).band, lb = m_lb.step(p).band;
        m_vibRms += (vb * vb - m_vibRms) * pole(dt, 0.4f);
        m_lowRms += (lb * lb - m_lowRms) * pole(dt, 0.4f);
        const float vr = std::sqrt(m_vibRms), lr = std::sqrt(m_lowRms);
        const float own = clampf((vr - 0.05f) / 0.04f, 0.0f, 1.0f) * clampf((vr / std::max(lr, 1e-4f) - 1.5f), 0.0f, 1.0f);
        m_own += (own - m_own) * pole(dt, 0.1f);
        m_onNote += dt;
        // How much the voice is corrected: not while it moves from note to
        // note (the singer's own step or slide is heard as it was sung: a
        // correction worked out from a pitch heard a little late would
        // hold it back, then let it go), eased in by how fast it is still
        // moving (fully under 6 semitones a second, not at all over 18:
        // arriving, it is drawn onto the note as it slows), and only once
        // it has been settled 25 to 55 ms (in a phrase's first 150 ms,
        // fully only under 2 st/s: a scoop is heard, then lands).
        const float bySpeed = m_onset < 0.15f ? clampf((8.0f - m_vel) / 5.0f, 0.0f, 1.0f)
                                              : clampf((kMoving + 8.0f - m_vel) / 12.0f, 0.0f, 1.0f);
        // (And for a note's first 120 ms, while it is in doubt, the voice
        // near halfway to its neighbour, only as much as it is sure: a
        // voice left a
        // quarter tone out for a moment is better than one pulled onto
        // the wrong note and then the right one.)
        float sure = 1.0f;
        if (m_settled < 0.12f && m_note > 0) {
            const float up = float(key.snap(m_note + 1.1f)), down = float(key.snap(m_note - 1.1f));
            const float other = std::fabs(m_d - up) < std::fabs(m_d - down) ? up : down;
            const float half = 0.5f * (m_note + other);
            sure = clampf((std::fabs(m_d - half) - 0.03f) / 0.07f, 0.0f, 1.0f);
            if ((m_d - half) * (m_note - half) < 0)
                sure = 0; // past halfway: the other note it is, very likely
        }
        const float amt = std::min({bySpeed, clampf((m_settled - 0.025f) / 0.03f, 0.0f, 1.0f), sure});
        m_amt += (amt - m_amt) * pole(dt, amt < m_amt ? 0.006f : 0.01f);
        // The step to a new note: an S-curve, two poles of 16 ms; or, if
        // the voice is hardly being corrected yet, the note at once.
        if (m_amt < 0.5f)
            m_g1 = m_g2 = m_note;
        m_g1 += (m_note - m_g1) * pole(dt, 0.016f);
        m_g2 += (m_g1 - m_g2) * pole(dt, 0.016f);
        // A held note's vibrato: the voice's own, if it has one (most of
        // it kept), or one of its own: never quite even, as a singer's
        // isn't (its rate wanders 4 %, its depth 12 %, at 0.3 and 0.7 Hz).
        m_wander[0] += kTwoPi * 0.3f * dt;
        m_wander[1] += kTwoPi * 0.7f * dt;
        for (float &w : m_wander)
            if (w > kTwoPi)
                w -= kTwoPi;
        const float want = m_onNote > kVibFrom ? kVibDepth * std::min(1.0f, (m_onNote - kVibFrom) / kVibRise) *
                                                     (1.0f + 0.12f * std::sin(m_wander[1])) * (1.0f - m_own)
                                               : 0.0f;
        m_vibDepth += (want - m_vibDepth) * pole(dt, want > m_vibDepth ? 0.08f : 0.04f);
        m_vibPh += kTwoPi * kVibRate * (1.0f + 0.04f * std::sin(m_wander[0])) * dt;
        if (m_vibPh > kTwoPi)
            m_vibPh -= kTwoPi;
        const float life = kKeep * f * (1.0f - m_own) + 0.85f * vb * m_own;
        const float target = m_g2 + std::min(1.0f, m_onNote / 0.15f) * life + m_vibDepth * std::sin(m_vibPh);
        m_out = p + m_amt * (target - p);
        m_ratio = clampf(std::exp2((m_out - p) / 12.0f), 0.5f, 2.0f);
        // The harmony: a third over the note, moving with it.
        const float h = float(key.above(m_note));
        // (A fixed interval over the lead, whatever the lead is doing: a
        // third over its note, never the singer's own pitch.)
        m_harmOut = m_out + (h - m_note);
        // The doubles: the note, each with its own vibrato (5.0 and 5.9 Hz),
        // as drawn onto it as the lead is.
        for (int k = 0; k < 2; ++k) {
            m_dblVib[k] += kTwoPi * (k ? 5.9f : 5.0f) * dt;
            if (m_dblVib[k] > kTwoPi)
                m_dblVib[k] -= kTwoPi;
            m_dblOut[k] = p + m_amt * (m_g2 + 0.8f * m_vibDepth * std::sin(m_dblVib[k]) - p);
        }
    }

    float m_rate = 48000;
    int m_layers = Doubled;
    vocal::Shifter m_lead, m_dbl[2], m_harm;
    Reverb m_plate;
    Svf m_sendHp, m_mud[2], m_presence[2], m_air[2], m_split[2], m_sibDetect, m_breathHp;
    OnePoleLp m_dblLp[2];
    Smooth m_wet, m_layer, m_breath, m_harmLvl;
    // The pitch: the latest heard (MIDI), the decision's average, the
    // centre's two poles, the glide's two, and the output.
    float m_p = 0, m_ps = 0, m_d = 0, m_c1 = 0, m_c2 = 0, m_g1 = 0, m_g2 = 0, m_out = -1, m_harmOut = 0;
    float m_away = 0, m_vel = 0, m_settled = 0, m_pPrev = 0, m_amt = 1, m_f = 0, m_avg = 0, m_dir = 1, m_from = 0;
    int m_avgN = 0;
    float m_note = -1, m_cand = -1, m_candT = 0, m_onNote = 0, m_onset = 0;
    // The voice's own vibrato: its band (4.5–7.5 Hz) and a slower wobble's,
    // their strengths, and how much it is a vibrato (0..1).
    Svf m_vb, m_lb;
    float m_vibRms = 0, m_lowRms = 0, m_own = 0;
    float m_wander[2] = {0.0f, 1.3f}, m_vibDepth = 0, m_vibPh = 0, m_dPh[2] = {0.0f, 2.1f}, m_dDel[2] = {0.0f, 1.7f};
    float m_ratio = 1, m_period = 288, m_gap = 1, m_fine = 0, m_octT = 0, m_lastP = 0;
    unsigned m_tick = 0;
    int m_silent = 1;
    float m_pkGain = 1, m_dblOut[2] = {0, 0}, m_dblVib[2] = {0.7f, 3.9f};
    float m_sibEnv = 0, m_allEnv = 0, m_vEnv = 0.02f, m_uEnv = 0, m_hEnv = 0;
};

} // namespace tf::beautify
