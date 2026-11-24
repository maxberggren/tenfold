#pragma once

// Kargyraa, the Tuvan growl: the KARGYRAA style of the Vocal page. Not an
// effect on the voice but the voice made again, the way a kargyraa
// singer's larynx makes it, with the singer's own vowel steering the
// melody. What every constant is measured from is in docs/kargyraa.md;
// the physiology behind it in docs/kargyraa-research.md.
//
//   the larynx   two valves in series. The vocal folds open and close at
//                the sung pitch, moved by octaves into a kargyraa singer's
//                register, pressed: open under half the cycle, closing by
//                collision. Above them the ventricular (false) folds, drawn
//                in, vibrate too: a second oscillator, phase-locked to the
//                first at a half (or a third) of its rate, closing once
//                every two (or three) glottal cycles, during one of the
//                glottal pulses. Air through two slits in series obeys
//                Bernoulli for both, and the air column above has
//                inertance, so the flow is solved for the two areas each
//                sample: the pulse the false folds close on comes out
//                weaker and later (the back pressure of their closure slows
//                the folds below), the next one whole. Alternate pulses
//                differ in height and in timing: period doubling, the
//                undertone an octave under the sung note. Each cycle's
//                length and height wander (jitter, shimmer), and both jets
//                make turbulence above a critical Reynolds number, the rasp.
//                How hard you sing is the lungs' pressure.
//   the tract    a man's vocal tract, larynx lowered, as twelve resonances
//                in cascade (Klatt's), the first two narrow, as an overtone
//                singer tunes them.
//                Its first two are your vowel: found every 8 ms by linear
//                prediction on what you sing, normalised to a man's tract,
//                then each tuned onto the nearest harmonic of the
//                undertone, as a kargyraa singer places a resonance on a
//                single harmonic. Move your vowel (oo, oh, aw, ah) and the
//                first resonance steps from harmonic to harmonic, the 6th to
//                the 12th of the undertone: the melody, as a kargyraa's is
//                F1's. A finger on a lane steers it instead: the vowel
//                opened or closed until F1 sits on the harmonic nearest the
//                lane's note, F2 an octave over it.
//   your mouth's consonants  a hiss or a burst is noise, with no pitch: it
//                is let through as you made it, only while no vowel sounds.
//
// All of it at the studio's rate, sample by sample, no look-ahead and no
// resampling: what you sing is heard as soon as the tracker has its pitch,
// and it costs a fraction of a percent of a core.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

#include "dsp.h"
#include "scream.h"

namespace tf::kargyraa {

// How many glottal cycles one cycle of the false folds spans (B).
enum Ratio { Half, Third, RatioCount };
inline const char *ratioName(int r) {
    static const char *names[] = {"2:1", "3:1"};
    return names[std::clamp(r, 0, RatioCount - 1)];
}
inline const char *ratioKey(int r) {
    static const char *names[] = {"half", "third"};
    return names[std::clamp(r, 0, RatioCount - 1)];
}
inline int cyclesOf(int r) { return std::clamp(r, 0, RatioCount - 1) == Third ? 3 : 2; }

// The lungs: 1.4 kPa soft to 2.4 kPa loud; a full voice about the 21.9
// cmH₂O (2.15 kPa) Fuks measured in this mode, against 18 modal.
constexpr float kLungsSoft = 1400.0f, kLungsLoud = 2400.0f;

// sin(2π·e) for e in [−0.5, 0.5], from scream's cosine.
inline float sin2Pi(float e) {
    float x = 2.0f * e - 0.5f; // cos(π(2e − ½)) = sin(2πe)
    if (x < 0)
        x += 2.0f;
    return scream::cosPi(x);
}

// A two-pole resonance, Klatt's (1980): unity gain at DC, so a cascade of
// them is a vocal tract whose formants' levels come out right by
// themselves.
struct Resonator {
    float a = 1, b = 0, c = 0, y1 = 0, y2 = 0;
    void set(float hz, float bw, float rate) {
        const float r = std::exp(-kPi * bw / rate);
        c = -r * r;
        b = 2.0f * r * std::cos(kTwoPi * hz / rate);
        a = 1.0f - b - c;
    }
    inline float step(float x) {
        const float y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
    void reset() { y1 = y2 = 0; }
};

// The larynx: the vocal folds and the ventricular folds above them, in SI
// units (but the noise's scale), stepped at the studio's rate. Its output
// is what the lips radiate: the time derivative of the volume velocity,
// the jets' turbulence in it.
struct Larynx {
    // Air.
    static constexpr float kRho = 1.14f;  // kg/m³, warm and humid
    static constexpr float kC = 350.0f;   // m/s
    static constexpr float kNu = 1.6e-5f; // m²/s
    // The vocal folds: a man's, 1.4 cm long (Sakakibara 2002's l_g), their
    // widest opening set so a strong pulse at the measured 2.2 kPa peaks
    // near the measured 460 mL/s (Fuks 1998), with a posterior chink of
    // 0.005 cm² (pressed: hardly any).
    static constexpr float kFoldLength = 0.014f;
    static constexpr float kAmax = 1.0e-5f;
    static constexpr float kLeak = 5.0e-7f;
    // The open quotient of a pressed voice, and how much of the open phase
    // is opening (Rosenberg's skew), varying a hair cycle to cycle. (A
    // rigid pulse would put the same spectral zeros on the same harmonics
    // at every pitch; the weak cycle's open phase, half again as long under
    // the false folds' push, puts its zeros elsewhere, and the pair's
    // spectrum has none deep. Left to wander slowly, the zeros would sit
    // on whichever harmonic a stretch of the walk took them to: a melody
    // harmonic sunk for a second at a time, differently in every run.)
    // And the return phase after closure (the LF model's Ta:
    // the last of the glottis shutting over 0.05 ms, short as a pressed
    // voice's is).
    static constexpr float kOpen = 0.45f;
    static constexpr float kRiseShare = 0.7f; // (closing quicker than modal's 0.62: Fuks 1998)
    static constexpr float kShapeVary = 0.005f; // cycle to cycle
    static constexpr float kReturn = 0.00005f;  // s: a pressed voice's short return
    static constexpr float kReturnArea = 0.05f; // of the widest, where the return takes over
    // The ventricular folds: a slit a little longer than the vocal folds'
    // (1.5 cm, Bailly 2010's depth W), wide open at rest. Drawn in and
    // vibrating they open to 1.5–2 × the glottis (Bailly 2014's case 4:
    // h_venf/h_vof 0.9–2), and their motion has two parts: an approach on
    // every glottal cycle (one cosine per glottal period) and, at the
    // undertone, a deeper one on every second (or third). The deeper
    // closes them, tight (Lindestad 2001: "complete but short" closures):
    // contact from the weak cycle's open phase until its glottal closure,
    // where they start to open again (Bailly 2010, p. 3217). The other
    // approach leaves a gap.
    static constexpr float kVenLength = 0.015f;
    static constexpr float kVenRest = 5.0e-5f;
    static constexpr float kVenPeak = 1.5e-5f;
    static constexpr float kVenMin = 2.0e-7f;
    static constexpr float kVenUnder = 0.85f; // the undertone's share of their motion
    static constexpr float kVenContact = 0.22f; // of their cycle, at most (cut at glottal closure)
    static constexpr float kVenVary = 0.04f;    // their cycle-to-cycle variation
    // Where in the weak glottal cycle the contact would be centred (a
    // fraction of that cycle from its opening; it starts in the open phase
    // and is cut at the glottal closure), how strongly the false folds
    // lock to the folds below (Adler's lock range, as a share of their
    // rate), how fast their own tension follows a new pitch, and how much
    // their contact slows the folds' open phase, varying from closure to
    // closure, slowly: with the ventricle's pressure up to the lungs', the
    // glottis loses its drop and closes late (Bailly 2010: that cycle 19 %
    // the longer, its open phase 1.5 ms so; four consecutive cycles apart
    // by a steady 39–42 Hz). Varied more, or faster, the half-harmonics'
    // timing scatters and the top turns to noise.
    static constexpr float kClosureAt = 0.42f;
    static constexpr float kLock = 0.25f;
    static constexpr float kTensionTime = 0.3f; // s
    static constexpr float kBackPressure = 0.47f;
    static constexpr float kBackVary = 0.05f;
    static constexpr float kDriftTime = 0.3f; // s: how slowly the contact's push and the pulse's shape wander
    // The air above the folds: the epilarynx and the ventricle, about 3 cm
    // of 0.7 cm² with the larynx lowered, its inertance ρ·l/A; the tract's
    // area just above, for the noise's impedance.
    static constexpr float kInertance = kRho * 0.03f / 0.7e-4f;
    static constexpr float kTract = 3.0e-4f;
    // Turbulence after Meyer-Eppler (1953) and Flanagan & Cherry (1969),
    // p ∝ (Re² − Re_c²) above a critical Reynolds number. Nothing measures
    // a kargyraa's noise (it is heard "hoarse": Castellengo & Henrich
    // 2014, though "clear harmonic partials up to about 4 kHz": Fuks
    // 1998); the scale is three quarters the RIOT scream's, the partials
    // standing 30, 12 and 9 dB over the noise between them at 1–2, 2–3
    // and 3–4 kHz.
    static constexpr float kNoiseK = 1.5e-6f;
    static constexpr float kReCrit = 2700.0f;

    float phG = 0, phV = 0, fv = 0, flow = 0, prev = 0;
    float jit = 1, shim = 1, venDepth = 1, venGap = 0, venJit = 1, comp = 1;
    float oq = kOpen, rise = kRiseShare * kOpen, back = kBackPressure, ret = 0, retFall = 0.9f;
    float detune = 1, rough = 0; // the singer's tension against the lock; a rough onset's extra irregularity
    float driftBack = 0; // the push's slow random walk, unit variance
    int cycle = 0, pairLen = 0;
    float lastAg = 0, lastAs = kVenRest;
    Rng rng{0x4B47u};

    void reset() {
        phG = phV = 0;
        fv = 0;
        flow = prev = 0;
        jit = shim = venDepth = venJit = comp = 1;
        venGap = 0;
        oq = kOpen;
        rise = kRiseShare * kOpen;
        back = kBackPressure;
        ret = 0;
        driftBack = 0;
        detune = 1;
        rough = 0;
        cycle = pairLen = 0;
        lastAg = 0;
        lastAs = kVenRest;
    }
    // (Each draw its own statement: the order of calls within one
    // expression is the compiler's to choose, and every build should hear
    // the same noise.)
    static float gauss(Rng &r) {
        float s = r.unit();
        s += r.unit();
        s += r.unit();
        s += r.unit();
        return (s - 2.0f) * 1.732f;
    }
    // A new cycle of the false folds: each closure a little different, in
    // how wide they open, how tightly they meet, how hard they push back.
    // (Their push on the glottis wanders slowly, as a singer's effort does,
    // not independently from closure to closure: Bailly's consecutive
    // cycles differ by a steady 39–42 Hz, and the voice is "sonorous".)
    void newVenCycle(float jitter, float period) {
        venDepth = clampf(1.0f + kVenVary * gauss(rng), 0.7f, 1.3f);
        venGap = kVenMin * std::fabs(gauss(rng));
        venJit = 1.0f + 0.5f * jitter * gauss(rng);
        const float rho = std::exp(-period / kDriftTime), nu = std::sqrt(1.0f - rho * rho);
        driftBack = rho * driftBack + nu * gauss(rng);
        back = kBackPressure * clampf(1.0f + kBackVary * driftBack, 0.3f, 1.6f);
    }

    // How far open the false folds are, 0..1 of their widest, at their
    // phase (0: the middle of their contact), with `under` of their motion
    // at the undertone (0: only the approach on every glottal cycle) and
    // the contact's depth.
    static float venOpening(float ph, int cycles, float under) {
        const float c1 = 0.5f - 0.5f * scream::cosPi(2.0f * ph);                 // once a cycle of theirs
        const float x = float(cycles) * ph;                                      // once a glottal cycle
        const float cR = 0.5f - 0.5f * scream::cosPi(2.0f * (x - std::floor(x)));
        const float s = (1.0f - under) * cR + under * c1;
        // (Contact while s is under its value at the contact's edge.)
        const float edge = (1.0f - under) * (0.5f - 0.5f * scream::cosPi(float(cycles) * kVenContact)) +
                           under * (0.5f - 0.5f * scream::cosPi(kVenContact));
        // (No contact at all until the undertone has its full share.)
        const float floor = edge * under * (1.0f / kVenUnder);
        return clampf((s - floor) / (1.0f - floor), 0.0f, 1.0f);
    }

    // One sample: the folds' rate f0 (Hz), the lungs' pressure P (Pa), how
    // far the voice is sounding (0..1), how far the false folds are in
    // (0: at rest, wide; 1: vibrating fully, in contact), how many glottal
    // cycles they span, the cycle-to-cycle jitter and shimmer.
    inline float step(float f0, float P, float voicing, float engage, int cycles, float jitter, float shimmer,
                      float rate) {
        const float R = float(cycles);
        if (cycle >= cycles)
            cycle = 0;
        // As the false folds come in they first approach on every cycle;
        // then the undertone takes over and they close (as a song starts
        // on a plain pitch and switches to the doubling: Castellengo &
        // Henrich 2014).
        const float under = kVenUnder * clampf(2.0f * engage - 1.0f, 0.0f, 1.0f);
        const float open = venOpening(phV, cycles, under);
        // Their contact pushes back on the folds below: while the glottis
        // is open and the ventricle at the lungs' pressure, it closes late.
        const float contact = under > 0 && open <= 0.0f ? 1.0f : 0.0f;
        const float slow = phG < oq ? 1.0f - back * contact * engage : 1.0f;
        const float was = phG;
        phG += f0 * comp * jit * slow / rate;
        ++pairLen;
        // The glottis shuts: false folds in contact start to open (their
        // phase moved on to their contact's end).
        if (was < oq && phG >= oq) {
            if (contact > 0) {
                if (phV > 0.5f) {
                    newVenCycle(jitter, R / std::max(f0, 1.0f));
                    phV = 0.5f * kVenContact + 1e-3f;
                } else {
                    phV = std::max(phV, 0.5f * kVenContact + 1e-3f);
                }
            }
        }
        if (phG >= 1.0f) {
            phG -= std::floor(phG);
            cycle = (cycle + 1) % cycles;
            const float j = 1.0f + rough * 4.0f;
            jit = 1.0f + jitter * j * gauss(rng);
            shim = std::exp(shimmer * j * gauss(rng));
            oq = kOpen * clampf(1.0f + kShapeVary * gauss(rng), 0.8f, 1.2f);
            rise = oq * kRiseShare * clampf(1.0f + kShapeVary * gauss(rng), 0.8f, 1.2f);
            retFall = std::exp(-1.0f / (kReturn * rate));
            // The singer's pitch is the pair's: what the contact adds to
            // one cycle is taken from the pair, so the undertone is
            // exactly the sung note's half (and the music's in tune).
            if (cycle == 0) {
                const float want = R * rate / std::max(f0, 1.0f);
                // (Half the last pair's error, each pair: the push that
                // lengthens the weak cycle wanders over a third of a
                // second, so this follows it and a melody harmonic stays
                // within a few cents of its note; all of it at once, a loop
                // at a gain of one with the lock's own lag, can swing.)
                comp = clampf(comp * std::sqrt(float(pairLen) / want), 0.8f, 1.6f);
                pairLen = 0;
            }
        }
        // The false folds: an oscillator of their own, their natural rate
        // set by the singer's tension (following the folds' over 0.3 s, a
        // little off it, differently each phrase), pulled into step with
        // the folds below at 1:R (Adler's equation: dφ/dt = f + K·sin
        // 2π(φ_lock − φ)), so their phase against the glottis sits where
        // the detuning puts it, and slips at a leap. The lock puts their
        // contact's middle near kClosureAt of the weak cycle.
        const float fLock = f0 / R;
        if (fv <= 0)
            fv = fLock * detune;
        fv += (fLock * detune - fv) * (1.0f / (kTensionTime * rate));
        float e = (float(cycle) + phG - (R - 1.0f + kClosureAt)) / R - phV;
        e -= std::round(e);
        phV += (fv * venJit + kLock * fLock * sin2Pi(e)) / rate;
        if (phV >= 1.0f) {
            phV -= std::floor(phV);
            newVenCycle(jitter, R / std::max(f0, 1.0f));
        }
        if (phV < 0)
            phV += 1.0f;
        // The glottal area: a raised cosine opening, a quarter cosine
        // closing, still falling when it shuts (Rosenberg's), the collision
        // of a pressed voice; then the return phase.
        float pulse = 0;
        if (phG < rise) {
            pulse = 0.5f - 0.5f * scream::cosPi(phG / rise);
            ret = 0;
        } else if (phG < oq) {
            pulse = scream::cosPi(0.5f * (phG - rise) / (oq - rise));
        }
        // (The return: once the closing area is down to kReturnArea it
        // goes on falling exponentially, not to zero at once.)
        if (phG >= rise && pulse < kReturnArea) {
            ret = ret > 0 ? ret * retFall : kReturnArea;
            if (ret < 1e-6f)
                ret = 1e-7f; // (not 0: that would start a new return; and no denormals)
            pulse = std::max(pulse, ret);
        }
        const float Ag = kLeak + kAmax * shim * voicing * pulse;
        // The false folds' area: at rest, wide; drawn in, vibrating.
        const float vib = kVenMin + venGap + kVenPeak * venDepth * open;
        const float As = kVenRest + (vib - kVenRest) * clampf((engage * voicing - 0.05f) * (1.0f / 0.9f), 0.0f, 1.0f);
        lastAg = Ag;
        lastAs = As;
        // The flow: the lungs' pressure spent on the jets through both
        // slits in series and on accelerating the air above (L·dU/dt),
        // solved implicitly for this sample's U.
        const float invA2 = 1.0f / (Ag * Ag) + 1.0f / (As * As);
        const float a = 0.5f * kRho * invA2, b = kInertance * rate, c = std::max(P, 0.0f) + b * flow;
        const float U = c > 0 ? 2.0f * c / (b + std::sqrt(b * b + 4.0f * a * c)) : 0.0f;
        flow = U;
        // The jets: a slit's Reynolds number is 2U/(ν·L) whatever its
        // width; the false folds' jet counts as far as they are the
        // narrower.
        const float Re = 2.0f * U / (kNu * kFoldLength);
        const float ReV = 2.0f * U / (kNu * kVenLength);
        const float jetV = clampf(Ag / As, 0.0f, 1.0f);
        const float aG = std::max(0.0f, Re * Re - kReCrit * kReCrit);
        const float aV = jetV * std::max(0.0f, ReV * ReV - kReCrit * kReCrit);
        const float nG = rng.unit() - 0.5f;
        const float nV = rng.unit() - 0.5f;
        const float pn = kNoiseK * (aG * nG + aV * nV);
        const float total = U + pn * kTract / (kRho * kC);
        const float out = (total - prev) * rate;
        prev = total;
        return out;
    }
};

// The kargyraa voice itself: the larynx, the tract and a level loop, run
// sample by sample by what plays it. The Vocal page's style (Kargyraa,
// below) drives it from a sung voice: its pitch, its level, its vowel. The
// Lead page's (Played, below) drives it from a note: its pitch, the
// finger's height as the vowel, its press as the lungs.
class Voice {
public:
    void init(float rate) {
        m_rate = rate;
        m_dc.r = 1.0f - kTwoPi * 20.0f / rate;
        auto per = [&](float seconds) { return 1.0f - std::exp(-1.0f / (seconds * rate)); };
        m_tractRate = per(kGlide);
        m_rawRate = per(0.04f);
        m_gainRate = per(0.01f);
        m_settleRate = per(0.3f);
        reset();
    }
    void reset() {
        for (auto &r : m_tract)
            r.reset();
        for (int j = 2; j < 12; ++j)
            m_tract[j].set(std::min(kFHigh[j - 2], 0.45f * m_rate), kBHigh[j - 2], m_rate);
        m_dc.x1 = m_dc.y1 = 0;
        m_larynx.reset();
        m_t1 = m_want1 = 500.0f;
        m_t2 = m_want2 = 900.0f;
        m_tick = 0;
        m_rawPow = m_lvlPow = 0;
        m_gain = m_gainSettled = kGainStart;
    }
    void setRatio(int r) { m_ratio = std::clamp(r, 0, RatioCount - 1); }
    int cycles() const { return cyclesOf(m_ratio); }
    // Where the tract's first two resonances go (Hz): they glide there over
    // 15 ms, the tongue and the jaw moving; or, placeNow, are there at once.
    void setFormants(float f1, float f2) {
        m_want1 = f1;
        m_want2 = f2;
    }
    void placeNow() {
        m_t1 = m_want1;
        m_t2 = m_want2;
    }

    float f1() const { return m_t1; }
    float f2() const { return m_t2; }
    Larynx &larynx() { return m_larynx; }
    const Larynx &larynx() const { return m_larynx; }

    // One sample: the folds' rate f0 (Hz), the lungs' pressure P (Pa), how
    // far the voice is sounding (0..1), how far the false folds are in
    // (0..1), and the level to hold, as this sample of it squared.
    inline float step(float f0, float P, float voicing, float engage, float levelSq) {
        m_t1 += (m_want1 - m_t1) * m_tractRate;
        m_t2 += (m_want2 - m_t2) * m_tractRate;
        if ((m_tick++ & 15) == 0) {
            m_tract[0].set(m_t1, kB1, m_rate);
            m_tract[1].set(m_t2, kB2, m_rate);
        }
        float src = 0;
        if (f0 > 0) {
            const float norm = 1.0f / (Larynx::kAmax * std::sqrt(2.0f * P / Larynx::kRho) * f0);
            src = m_larynx.step(f0, P, voicing, engage, cycles(), kJitter, kShimmer, m_rate) * norm;
        }
        float y = src;
        for (auto &res : m_tract)
            y = res.step(y);
        y = m_dc.step(y);
        if (!std::isfinite(y)) {
            reset();
            y = 0;
        }
        // The level held: the growl's own power over 40 ms held to the one
        // asked for over the same, both started when the growl starts, so
        // the vowel, the harmonic the melody is on, the doubling coming in
        // and the pressure change its colour but not its level (a loudness
        // follower, by design). A phrase starts on the gain the last one
        // settled on, and the gain never rises past twice that: the growl's
        // first milliseconds, while its own power is still building, aren't
        // lifted to meet a level they haven't reached.
        if (voicing < 0.05f) {
            m_gain = m_gainSettled;
            m_rawPow = m_lvlPow = 0;
        } else {
            m_rawPow += (y * y - m_rawPow) * m_rawRate;
            m_lvlPow += (levelSq - m_lvlPow) * m_rawRate;
        }
        const float cap = std::min(kGainCap, 2.0f * m_gainSettled);
        const float gTo = m_rawPow > 1e-12f ? std::min(std::sqrt(m_lvlPow / m_rawPow), cap) : m_gainSettled;
        m_gain += (gTo - m_gain) * m_gainRate;
        // (Settled: a doubled, fully sounding voice.)
        if (voicing > 0.99f && engage > 0.95f)
            m_gainSettled += (m_gain - m_gainSettled) * m_settleRate;
        return y * m_gain;
    }

    // Cycle to cycle: jitter 0.2 %, shimmer 0.025 neper (0.2 dB). Nothing
    // measures a kargyraa's; it is "sonorous" (Lindestad 2001), its cycles
    // "more uniform" than the pulse register's (Grawunder 1999), and its
    // partials clear to 4 kHz (Fuks 1998), so a steady healthy voice's;
    // with the false folds' own variation the undertone's period jitter
    // comes to about 0.4 %.
    static constexpr float kJitter = 0.002f, kShimmer = 0.025f;

private:
    static constexpr float kGlide = 0.015f; // s
    // Bandwidths: narrow. Nothing measures a kargyraa's; a long, firm
    // glottal closure damps the tract little, and an overtone singer's
    // tuned formant is about 20 Hz wide (Klingholz 1993). 25 Hz puts the
    // tuned harmonic 5–16 dB over its neighbours across H6–H11.
    static constexpr float kB1 = 25.0f, kB2 = 45.0f;
    // F3–F5 a man's; F6–F12 over them, about a kilohertz apart as an 18 cm
    // tube's are. A cascade cut off after five poles loses what the poles
    // above add to the top (Klatt's cascade at 10 kHz stood in for them
    // with a higher-pole correction; at the studio's rate they are poles).
    static constexpr float kFHigh[10] = {2350.0f, 3150.0f, 3750.0f, 4500.0f, 5500.0f,
                                         6500.0f, 7500.0f, 8500.0f, 9500.0f, 10500.0f};
    static constexpr float kBHigh[10] = {110.0f, 160.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f, 900.0f};
    // The level loop's gain: never over this (a voice needs 0.01–0.05: its
    // raw RMS is 10–30), and where a first phrase starts.
    static constexpr float kGainCap = 0.15f;
    static constexpr float kGainStart = 0.03f;

    float m_rate = 48000;
    int m_ratio = Half;
    Larynx m_larynx;
    Resonator m_tract[12];
    DcBlock m_dc;
    float m_t1 = 500, m_t2 = 900, m_want1 = 500, m_want2 = 900;
    uint32_t m_tick = 0;
    float m_tractRate = 0, m_rawRate = 0, m_gainRate = 0, m_settleRate = 0;
    float m_rawPow = 0, m_lvlPow = 0, m_gain = 0, m_gainSettled = 0;
};

// The voice played as an instrument (the Lead page's KARGYRAA), the way a
// throat singer sings a melody: the note played is the melody, a harmonic
// of the growl's undertone that the vowel's first resonance picks out, and
// the undertone holds under it in kargyraa's own register (45–105 Hz),
// the singer changing which harmonic the vowel picks out, not the drone.
// It holds while each new note is within 6 cents of one of its even
// harmonics (the 4th to the 16th: its octaves and fifths, the folds' own
// harmonics, steady); otherwise it moves, to the key's root if that fits
// under the note, or else to the note's own name an octave or more down.
// So the growl is always in the key, and the melody's harmonics are the
// tempered notes within a few cents. The folds sound an octave over the
// undertone and the false folds close on every other pulse. F1 is on the
// note, narrow, so the note is what is heard; the finger's height moves F2
// from an octave over F1 (the open vowels' "formant octave") up to a
// bright 1.7 kHz (/ɛ ə a/), the two ways Castellengo & Henrich found it
// sung. A note from silence is a phrase: a short rough start, the false
// folds a little off the lock. Pressure is the lungs.
class Played {
public:
    void init(float rate) {
        m_rate = rate;
        m_voice.init(rate);
        auto per = [&](float seconds) { return 1.0f - std::exp(-1.0f / (seconds * rate)); };
        m_up = per(0.008f);
        m_down = per(0.025f);
        m_engRate = per(0.04f);
        reset();
    }
    void reset() {
        m_voice.reset();
        m_voicing = m_engage = 0;
        m_age = 1 << 30;
        m_rough = 0;
        m_k = 0;
        m_under = m_noteWas = 0;
        m_tick = 0;
    }
    // A note begins. From silence it is a new phrase: a rough start of 20
    // to 60 ms (a singer's runs 50 to 300, which would put a line's first
    // note's growl an octave up, the folds' own pitch, off the beat; an
    // accent doubles at once), the false folds a little off the lock.
    void start(bool accent, Rng &rng) {
        if (m_voicing > 0.05f)
            return;
        m_age = 0;
        m_k = 0;
        m_under = 0;
        m_rough = accent ? 0 : int((0.02f + 0.04f * rng.unit()) * m_rate);
        m_voice.larynx().detune = 1.0f + 0.03f * Larynx::gauss(rng);
    }
    bool sounding() const { return m_voicing > 0.001f; }
    // Which harmonic of the undertone the note is (0: silent), and the
    // undertone (Hz).
    int harmonic() const { return m_voicing > 0.1f ? m_k : 0; }
    float under() const { return m_under; }
    const Voice &voice() const { return m_voice; }

    // One sample: the note (Hz, the melody), the finger's height (F2,
    // 0..1), the lungs (0..1), whether the note is held, the level to hold
    // its voice at (RMS), the key's root (Hz, any octave; 0: none) and an
    // undertone to share if the note fits it (a harmony's, the lead
    // voice's; 0: none).
    inline float step(float hz, float height, float effort, bool on, float level, float rootHz = 0,
                      float shareUnder = 0) {
        m_voicing += ((on ? 1.0f : 0.0f) - m_voicing) * (on ? m_up : m_down);
        const bool rough = on && m_age < m_rough;
        if (m_age < (1 << 30))
            ++m_age;
        m_engage += ((on ? (rough ? 0.5f : 1.0f) : 0.0f) - m_engage) * m_engRate;
        Larynx &lx = m_voice.larynx();
        lx.rough += ((rough ? 1.0f : 0.0f) - lx.rough) * m_engRate;
        const float note = std::max(hz, 1.0f);
        if (std::fabs(note - m_noteWas) > 1e-3f * note || m_k <= 0) {
            m_noteWas = note;
            choose(note, rootHz, shareUnder);
        }
        if ((m_tick++ & 15) == 0) {
            const float f1 = note;
            const float w = clampf(height, 0.0f, 1.0f);
            const float f2want = std::min(lerpf(2.0f * f1, std::max(1700.0f, f1 + 300.0f), w), kF2Top);
            // F2 on the harmonic nearest that which shares no factor with
            // the note's (two strong partials with a common period would be
            // heard at theirs, an octave or more over the note), never over
            // its ceiling.
            const int top = std::max(m_k + 1, int(std::floor(kF2Top / m_under)));
            const int near = std::clamp(int(std::lround(f2want / m_under)), m_k + 1, top);
            int k2 = near;
            for (int d = 0; d <= 3; ++d) {
                if (near - d > m_k && std::gcd(m_k, near - d) == 1) {
                    k2 = near - d;
                    break;
                }
                if (near + d <= top && std::gcd(m_k, near + d) == 1) {
                    k2 = near + d;
                    break;
                }
            }
            m_voice.setFormants(f1, float(k2) * m_under);
            if (m_voicing < 0.02f)
                m_voice.placeNow();
        }
        const float P = kLungsSoft + (kLungsLoud - kLungsSoft) * clampf(effort, 0.0f, 1.0f);
        return m_voice.step(m_under * float(m_voice.cycles()), P, m_voicing, m_engage, level * level);
    }

private:
    // The undertone for a new note: the one held, a shared one, or a new
    // one, and which harmonic of it the note is.
    void choose(float note, float rootHz, float shareUnder) {
        // The even harmonic of u the note is within 6 cents of (0: none).
        auto fits = [&](float u) {
            if (u < kUnderLo || u > kUnderHi)
                return 0;
            const float q = note / u;
            const int k = 2 * int(std::lround(0.5f * q));
            if (k < kKLo || k > kKHi)
                return 0;
            return std::fabs(1200.0f * std::log2(q / float(k))) < 6.0f ? k : 0;
        };
        int k = 0;
        if (shareUnder > 0 && (k = fits(shareUnder)) > 0) {
            m_under = shareUnder;
        } else if (m_under > 0 && (k = fits(m_under)) > 0) {
            // (Held: the drone stays, the vowel picks another harmonic.)
        } else {
            // A new one: the key's root under the note if it fits, or the
            // note's own name an octave or more down (its 4th, 8th or 16th
            // harmonic), or a fifth under that (the 6th, 12th), nearest
            // 70 Hz; the 10th and 14th, a pure third and seventh, would put
            // the growl out of key.
            float best = 1e9f;
            for (int kk : {4, 6, 8, 12, 16}) {
                const float u = note / float(kk);
                if (u < kUnderLo || u > kUnderHi)
                    continue;
                float score = std::fabs(std::log2(u / kUnderAt)) + (kk % 4 ? 0.3f : 0.0f);
                if (rootHz > 0) {
                    const float c = 12.0f * std::log2(u / rootHz);
                    if (std::fabs(c - std::round(c / 12.0f) * 12.0f) < 0.1f)
                        score -= 2.0f;
                }
                if (score < best)
                    best = score, k = kk;
            }
            if (k == 0)
                k = std::clamp(2 * int(std::lround(note / (2.0f * kUnderAt))), kKLo, kKHi);
            m_under = note / float(k);
        }
        m_k = k;
    }

    // The undertone's register (Lindestad: 70 Hz; eleven recordings 61–92;
    // Fuks from 49), and which harmonics a melody note may be of it
    // (Castellengo & Henrich's 6th to 12th, down to the 4th for the neck's
    // lowest notes, up to the 16th for its highest); F2's ceiling.
    static constexpr float kUnderAt = 70.0f, kUnderLo = 45.0f, kUnderHi = 105.0f;
    static constexpr int kKLo = 4, kKHi = 16;
    static constexpr float kF2Top = 2300.0f;

    float m_rate = 48000;
    Voice m_voice;
    float m_voicing = 0, m_engage = 0, m_up = 0, m_down = 0, m_engRate = 0, m_under = 0, m_noteWas = 0;
    int m_age = 1 << 30, m_rough = 0, m_k = 0;
    uint32_t m_tick = 0;
};

// The Vocal page's style: a sung voice in, the kargyraa out, at the
// studio's rate.
class Kargyraa {
public:
    void init(float rate) {
        m_rate = rate;
        m_decim = std::max(1, int(std::lround(rate / 8000.0f)));
        m_inner = rate / float(m_decim);
        for (int i = 0; i < 3; ++i)
            m_down[i].set(0.4f * m_inner, kButter6[i], rate);
        m_consHp[0].set(1800.0f, 0.5412f, rate);
        m_consHp[1].set(1800.0f, 1.3066f, rate);
        m_hiBand.set(2000.0f, 0.7071f, rate);
        m_voice.init(rate);
        auto per = [&](float seconds) { return 1.0f - std::exp(-1.0f / (seconds * rate)); };
        m_envUp = per(0.004f);
        m_envDown = per(0.03f);
        m_vUp = per(0.008f);
        m_vDown = per(0.025f);
        m_engRate = per(0.04f);
        m_glide = per(0.005f);
        m_onsetSamples = int(0.15f * rate);
        m_guardRate = per(0.02f);
        m_refFall = std::exp(-std::log(10.0f) * 6.0f / rate);
        // The analysis: Hann window, and |1/A|'s grid, 150 Hz to 3.2 kHz.
        for (int i = 0; i < kWin; ++i)
            m_hann[i] = 0.5f - 0.5f * std::cos(kTwoPi * (float(i) + 0.5f) / float(kWin));
        for (int g = 0; g < kGrid; ++g) {
            const float w = kTwoPi * gridHz(g) / m_inner;
            for (int k = 0; k <= kOrder; ++k) {
                m_gc[g][k] = std::cos(w * float(k));
                m_gs[g][k] = std::sin(w * float(k));
            }
        }
        reset();
    }
    void reset() {
        for (auto &f : m_down)
            f.reset();
        for (auto &f : m_consHp)
            f.reset();
        m_hiBand.reset();
        m_voice.reset();
        std::fill(std::begin(m_buf), std::end(m_buf), 0.0f);
        m_w = 0;
        m_phase = 0;
        m_hopAt = 0;
        m_preX = 0;
        m_env = m_pow = m_powHi = m_voicing = m_engage = 0;
        m_powIn = m_powRef = m_powOut = 0;
        m_guard = 1;
        m_f0 = 0;
        m_sung = 0;
        m_oct = 0;
        m_unvoiced = 1 << 30;
        m_away = 0;
        m_agree = 0;
        m_phraseAge = 0;
        m_startHz = 0;
        m_f1 = 500.0f;
        m_f2 = 900.0f;
        m_haveVowel = false;
        m_k1 = m_k2 = 0;
        m_want1 = 500.0f;
        m_want2 = 900.0f;
        m_tick = 0;
        m_consGate = 0;

        m_roughFor = 0;
        m_reading = false;
    }
    // The note sung now, for this block: its pitch (0: none), whether a
    // voice is sounding, how sure the tracker is.
    void setPitch(float hz, bool voiced, float clarity = 1.0f) {
        m_inHz = hz;
        m_inVoiced = voiced && hz > 0;
        m_inClarity = clarity;
        m_reading = true;
    }
    // A finger's note (Hz; 0: none): the resonance steered onto the
    // undertone's harmonic nearest it. Without one, your vowel steers it.
    void setSteer(float hz) { m_steerHz = hz; }
    void setRatio(int r) {
        m_ratio = std::clamp(r, 0, RatioCount - 1);
        m_voice.setRatio(m_ratio);
    }
    int ratio() const { return m_ratio; }

    // The folds' pitch now, the undertone's (the false folds'), the octaves
    // the sung note is moved by, and the melody: the harmonic of the
    // undertone the first resonance sits on (a kargyraa's melody is F1's:
    // Castellengo & Henrich 2014), and where.
    float hz() const { return m_f0; }
    float under() const { return m_f0 > 0 ? m_f0 / float(cyclesOf(m_ratio)) : 0.0f; }
    int octave() const { return m_oct; }
    int harmonic() const { return m_voicing > 0.1f ? m_k1 : 0; }
    float focusHz() const { return m_want1; }
    float f1() const { return m_voice.f1(); }
    float f2() const { return m_voice.f2(); }
    // The singer's own first two resonances, as found.
    float sungF1() const { return m_f1; }
    float sungF2() const { return m_f2; }
    float engaged() const { return m_engage; }
    // The two valves' areas at the last sample (m²), for the tests.
    const Larynx &larynx() const { return m_voice.larynx(); }

    // n samples of the voice in; the kargyraa out, added to l and r.
    void process(const float *in, float *l, float *r, int n, float mix0 = 1.0f, float mix1 = 1.0f) {
        follow(n);
        const bool sounding = m_inVoiced && m_sung > 0;
        const float target = m_sung > 0 ? m_sung * std::exp2(float(m_oct)) : m_f0;
        for (int i = 0; i < n; ++i) {
            const float x = in[i];
            // The analysis, at an eighth of a millisecond.
            const float d = m_down[2].step(m_down[1].step(m_down[0].step(x).low).low).low;
            if (++m_phase >= m_decim) {
                m_phase = 0;
                const float xp = d - kPre * m_preX;
                m_preX = d;
                m_buf[m_w++ & (kBufN - 1)] = xp;
                if (++m_hopAt >= kHop) {
                    m_hopAt = 0;
                    analyse();
                }
            }
            // The lungs: your level (its power, up over 4 ms, down over 30,
            // so no ripple at your own pitch rides on it) as pressure.
            const float x2 = x * x;
            m_pow += (x2 - m_pow) * (x2 > m_pow ? m_envUp : m_envDown);
            m_env = std::sqrt(m_pow);
            const float hb = m_hiBand.step(x).high;
            m_powHi += (hb * hb - m_powHi) * (hb * hb > m_powHi ? m_envUp : m_envDown);
            const float loud = clampf(m_env / kEnvRef, 0.0f, 1.3f);
            const float P = kLungsSoft + (kLungsLoud - kLungsSoft) * std::min(loud, 1.0f);
            // The voice: in over 8 ms, out over 25.
            m_voicing += ((sounding ? 1.0f : 0.0f) - m_voicing) * (sounding ? m_vUp : m_vDown);
            // The false folds come in with the voice: a plain pitch first,
            // rough, the false folds at the glottis's own period (for 50 to
            // 300 ms, a different time each phrase), then the doubling.
            const bool rough = sounding && m_phraseAge < m_roughFor;
            const float engageTo = sounding ? (rough ? 0.5f : 1.0f) : 0.0f;
            m_engage += (engageTo - m_engage) * m_engRate;
            Larynx &lx = m_voice.larynx();
            lx.rough += ((rough ? 1.0f : 0.0f) - lx.rough) * m_engRate;
            if (target > 0)
                m_f0 = m_f0 > 0 && m_voicing > 0.05f ? m_f0 + (target - m_f0) * m_glide : target;
            // Your vowel onto the undertone's harmonics, every 16 samples (a
            // phrase's first vowel placed at once, not glided to); then the
            // voice, as loud as you: its level held to yours.
            if ((m_tick++ & 15) == 0) {
                place();
                m_voice.setFormants(m_want1, m_want2);
            }
            if (m_voicing < 0.02f)
                m_voice.placeNow();
            float y = m_voice.step(m_f0, P, m_voicing, m_engage, x * x);
            // A guard, not a sound: what comes out over 20 ms is held to at
            // most 6 dB over your level (held as it falls, 60 dB a second),
            // so a tone or a buzz shouted in can't make it ring far louder.
            m_powIn += (x * x - m_powIn) * m_guardRate;
            m_powRef = std::max(m_powIn, m_powRef * m_refFall);
            m_powOut += (y * y - m_powOut) * m_guardRate;
            // (In a phrase's first 150 ms, while your vowel is still being
            // found and the growl's level loop is catching up with it, held
            // to 2 dB over: no burst at the attack.)
            const float room = (m_phraseAge < m_onsetSamples ? 1.6f : 4.0f) * m_powRef + 1e-12f;
            const float g = m_powOut > room ? std::sqrt(room / m_powOut) : 1.0f;
            m_guard += (g - m_guard) * (g < m_guard ? 0.05f : m_guardRate);
            y *= m_guard;
            // Your consonants, while no vowel sounds: a hiss (most of its
            // energy over 2 kHz) through as you made it.
            const float hiShare = std::sqrt(m_powHi / (m_pow + 1e-12f));
            const float gateTo = (1.0f - m_voicing) * clampf((hiShare - 0.25f) * 3.0f, 0.0f, 1.0f);
            m_consGate += (gateTo - m_consGate) * m_vUp;
            const float cons = m_consHp[1].step(m_consHp[0].step(x).high).high * m_consGate;
            const float mix = mix0 + (mix1 - mix0) * float(i + 1) / float(n);
            const float o = (y + cons) * mix;
            l[i] += o;
            r[i] += o;
        }
    }

private:
    // --- the note ---------------------------------------------------------
    // A phrase (after a quarter second without a note) takes the octave that
    // brings its first note nearest a kargyraa singer's folds, and keeps it.
    // A kargyraa holds its note: a reading an octave off it is the
    // tracker's slip, folded back; any other leap past a fourth counts once
    // it has lasted 15 ms.
    void follow(int n) {
        m_phraseAge = std::min(m_phraseAge + n, 1 << 30);
        if (!m_inVoiced) {
            m_unvoiced = std::min(m_unvoiced + n, 1 << 30);
            m_agree = 0;
            return;
        }
        const bool fresh = m_unvoiced > int(0.25f * m_rate) || m_sung <= 0;
        float reading = m_inHz;
        if (fresh) {
            // A phrase starts on three readings that agree (the first at a
            // syllable's onset is often a guess).
            if (m_inClarity < 0.6f) {
                m_agree = 0;
                return;
            }
            // (Counted by the tracker's readings, whatever the blocks.)
            if (!m_reading)
                return;
            m_reading = false;
            if (m_agree > 0 && std::fabs(std::log2(reading / m_startHz)) > 0.1f)
                m_agree = 0;
            m_startHz = reading;
            if (++m_agree < 3)
                return;
            m_agree = 0;
            m_unvoiced = 0;
            m_sung = reading;
            m_away = 0;
            m_phraseAge = 0;
            choose();
            // How long this phrase's rough start lasts, and how far the
            // singer's false folds are set off the lock.
            m_roughFor = int((0.05f + 0.25f * m_rng.unit()) * m_rate);
            m_voice.larynx().detune = 1.0f + 0.03f * Larynx::gauss(m_rng);
            return;
        }
        m_unvoiced = 0;
        const float octs = std::log2(reading / m_sung);
        const float rnd = std::round(octs);
        if (rnd != 0 && std::fabs(octs - rnd) < 0.06f)
            reading *= std::exp2(-rnd);
        const float semis = 12.0f * std::fabs(std::log2(reading / m_sung));
        if (semis <= 5.0f) {
            m_sung = reading;
            m_away = 0;
        } else if ((m_away += n) > int(0.015f * m_rate)) {
            m_sung = reading;
            m_away = 0;
            // (In a phrase's first moments a new note means its first
            // reading was off: the octave is chosen again.)
            if (m_phraseAge < int(0.15f * m_rate))
                choose();
        }
        // Never far out of the register: the phrase's octave moves only
        // when its note leaves a band an octave and a half wide (80–230 Hz),
        // so a note near either edge, or a vibrato across it, never flips.
        while (m_oct > -4 && m_sung * std::exp2(float(m_oct)) > kFoldTop)
            --m_oct;
        while (m_oct < 2 && m_sung * std::exp2(float(m_oct)) < kFoldBottom)
            ++m_oct;
    }
    void choose() { m_oct = std::clamp(int(std::lround(std::log2(kCentreHz / m_sung))), -4, 1); }

    // --- the tract ---------------------------------------------------------
    // Your vowel (or a finger) to the two resonances, each on a harmonic of
    // the undertone.
    void place() {
        const float fu = under();
        // Your resonances, normalised to a man's tract with the larynx
        // lowered: shorter tracts' formants scaled down with the octaves
        // the voice is moved down by.
        const float alpha = clampf(kAlpha0 + kAlphaOct * float(m_oct), 0.75f, 1.0f);
        float w1 = clampf(m_f1 * alpha, kF1Lo, kF1Hi);
        float w2 = clampf(m_f2 * alpha, w1 + kGap, kF2Hi);
        if (m_steerHz > 0 && fu > 0) {
            // A finger: its note folded by octaves into the melody's band,
            // the 6th to the 12th harmonic of the undertone (Castellengo &
            // Henrich 2014), and the vowel opened or closed along the
            // rounded back vowels (the jaw and the lips) until F1 is on the
            // harmonic nearest it, F2 an octave over it (their singers'
            // "formant octave" on /o ɔ ɑ a/).
            // (Folded into an octave from 5.7 to 11.4 times the undertone,
            // so a note's harmonic is never at the band's edge, where the
            // undertone's vibrato would toss it an octave each way.)
            float f = m_steerHz;
            const float lo = kMelLo * fu, hi = 2.0f * lo;
            while (f < lo)
                f *= 2.0f;
            while (f > hi)
                f *= 0.5f;
            const int k = std::max(2, int(std::lround(f / fu)));
            w1 = float(k) * fu;
            w2 = 2.0f * w1;
            m_k1 = k;
            m_k2 = 2 * k;
        }
        if (fu <= 0) {
            m_want1 = w1;
            m_want2 = w2;
            return;
        }
        // Each onto its nearest harmonic, the one it is on kept until
        // another is clearly nearer (two harmonics about as near would
        // otherwise take turns at every waver).
        // (F2, not the melody, is held the longer: its estimate wanders a
        // few tens of hertz on a steady vowel.)
        auto snap = [&](float w, int &k, float hold) {
            const float q = w / fu;
            if (k <= 0 || std::fabs(q - float(k)) > 0.5f + hold)
                k = std::max(1, int(std::lround(q)));
            return float(k) * fu;
        };
        m_want1 = snap(w1, m_k1, kHysteresis);
        if (m_k2 <= m_k1)
            m_k2 = 0;
        m_want2 = snap(std::max(w2, m_want1 + 2.0f * fu), m_k2, 2.0f * kHysteresis);
        if (m_k2 <= m_k1) {
            m_k2 = m_k1 + 2;
            m_want2 = float(m_k2) * fu;
        }
    }
    // --- the analysis ------------------------------------------------------
    // Every 8 ms, the last 32 ms of what you sing at 8 kHz: windowed,
    // pre-emphasised, twelve poles by Levinson–Durbin, the lag window widening
    // every resonance by a third of your pitch (so a high voice's
    // harmonics are never taken for formants), and the envelope's first
    // two peaks between 150 Hz and 3.2 kHz: your F1 and F2.
    void analyse() {
        if (!m_inVoiced)
            return;
        float x[kWin];
        const uint32_t start = m_w - uint32_t(kWin);
        float e0 = 0;
        for (int i = 0; i < kWin; ++i) {
            x[i] = m_buf[(start + uint32_t(i)) & (kBufN - 1)] * m_hann[i];
            e0 += x[i] * x[i];
        }
        if (e0 < 1e-9f)
            return;
        float r[kOrder + 1];
        const float bw = kTwoPi * std::max(60.0f, 0.33f * m_inHz) / m_inner;
        for (int k = 0; k <= kOrder; ++k) {
            float s = 0;
            for (int i = k; i < kWin; ++i)
                s += x[i] * x[i - k];
            r[k] = s * std::exp(-0.5f * bw * bw * float(k * k));
        }
        r[0] *= 1.0001f;
        float kr[scream::kOrder + 1], a[kOrder + 1];
        scream::levinson(r, kr, kOrder);
        // Reflection to direct form.
        {
            float t[kOrder + 1];
            a[0] = 1;
            for (int i = 1; i <= kOrder; ++i) {
                for (int j = 1; j < i; ++j)
                    t[j] = a[j] + kr[i] * a[i - j];
                for (int j = 1; j < i; ++j)
                    a[j] = t[j];
                a[i] = kr[i];
            }
        }
        float env[kGrid];
        for (int g = 0; g < kGrid; ++g) {
            float re = 0, im = 0;
            for (int k = 0; k <= kOrder; ++k) {
                re += a[k] * m_gc[g][k];
                im -= a[k] * m_gs[g][k];
            }
            env[g] = 1.0f / (re * re + im * im + 1e-12f);
        }
        // The resonances: the envelope's peaks, and its shoulders (where a
        // resonance merges into its neighbour's skirt, as an /a/'s F2 into
        // its F1, the envelope bends without peaking: a local minimum of its
        // curvature).
        float le[kGrid];
        for (int g = 0; g < kGrid; ++g)
            le[g] = std::log(env[g]);
        float cand[12];
        bool isPeak[12];
        int nc = 0;
        for (int g = 2; g + 2 < kGrid && nc < 12; ++g) {
            const float d2 = le[g - 1] - 2.0f * le[g] + le[g + 1];
            const bool peak = le[g] > le[g - 1] && le[g] >= le[g + 1];
            const bool shoulder = d2 < -0.01f && d2 < le[g - 2] - 2.0f * le[g - 1] + le[g] &&
                                  d2 <= le[g] - 2.0f * le[g + 1] + le[g + 2];
            if (!peak && !shoulder)
                continue;
            // (A parabola through the three for a peak's frequency.)
            const float den = le[g - 1] - 2.0f * le[g] + le[g + 1];
            const float off = peak && den < 0 ? clampf(0.5f * (le[g - 1] - le[g + 1]) / den, -0.5f, 0.5f) : 0.0f;
            const float hz = gridHz(g) + off * kGridStep;
            // (A shoulder right beside a peak is that peak's own top.)
            if (nc > 0 && hz - cand[nc - 1] < 100.0f) {
                if (peak && !isPeak[nc - 1])
                    cand[nc - 1] = hz, isPeak[nc - 1] = true;
                continue;
            }
            cand[nc] = hz;
            isPeak[nc++] = peak;
        }
        // F1: the first peak under 1.1 kHz (none: a front vowel's F1 lost in
        // the low end, kept as it was).
        float f1 = 0;
        for (int c = 0; c < nc && f1 <= 0; ++c)
            if (isPeak[c] && cand[c] < 1100.0f)
                f1 = cand[c];
        const bool haveF1 = f1 > 0;
        if (!haveF1)
            f1 = m_f1;
        // F2: of the peaks and shoulders 150 Hz to 2.8 kHz over it, the one
        // nearest the F2 followed (a new vowel's, the lowest), so F3 taken
        // for F2 in one window is not followed there.
        float f2 = 0, best = 1e9f;
        for (int c = 0; c < nc; ++c) {
            if (cand[c] < f1 + 150.0f || cand[c] > 2800.0f)
                continue;
            const float cost = m_haveVowel ? std::fabs(std::log2(cand[c] / m_f2)) + (isPeak[c] ? 0.0f : 0.1f) : cand[c];
            if (cost < best)
                best = cost, f2 = cand[c];
        }
        if (!haveF1 && f2 <= 0)
            return;
        if (f2 <= 0)
            f2 = std::max(m_f2, f1 * 1.7f);
        // The median of the last three readings (one hop's slip, F3 taken
        // for F2, is dropped), then followed over two hops in pitch; a
        // fresh phrase's vowel at once.
        m_r1[m_ri % 3] = f1;
        m_r2[m_ri % 3] = f2;
        ++m_ri;
        auto median = [](const float *v) { return std::max(std::min(v[0], v[1]), std::min(std::max(v[0], v[1]), v[2])); };
        if (!m_haveVowel) {
            for (int j = 0; j < 3; ++j)
                m_r1[j] = f1, m_r2[j] = f2;
            m_f1 = f1;
            m_f2 = f2;
            m_haveVowel = true;
        } else {
            m_f1 *= std::exp2(0.5f * std::log2(median(m_r1) / m_f1));
            m_f2 *= std::exp2(0.5f * std::log2(median(m_r2) / m_f2));
        }
    }
    static float gridHz(int g) { return 150.0f + kGridStep * float(g); }

    static constexpr int kOrder = 12;     // poles of your mouth, at 8 kHz
    static constexpr int kWin = 256;      // 32 ms
    static constexpr int kHop = 64;       // 8 ms
    static constexpr int kBufN = 512;     // power of two
    static constexpr float kGridStep = 20.0f;
    static constexpr int kGrid = 153;     // 150 Hz to 3.19 kHz
    static constexpr float kPre = 0.94f;
    static constexpr float kButter6[3] = {0.5176f, 0.7071f, 1.9319f};

    // The register: a kargyraa singer's folds near 140 Hz, the undertone
    // near 70 (Lindestad 2001: 70 Hz against 140 modal; eleven recordings
    // 61–92 Hz, Castellengo & Henrich 2014). A phrase starts in the octave
    // nearest it (99–198 Hz); within it, moved only outside 80–230 Hz (an
    // undertone of 40–115 Hz; Fuks 1998 sang an octave from 49 Hz).
    static constexpr float kCentreHz = 140.0f;
    static constexpr float kFoldTop = 230.0f, kFoldBottom = 80.0f;
    static constexpr float kEnvRef = 0.12f;
    // The tract. Your formants to a man's: α at your own octave, less per
    // octave the voice is moved down. The bands the two resonances keep
    // to, and the melody's band.
    static constexpr float kAlpha0 = 0.96f, kAlphaOct = 0.08f;
    // (F1 320–920 Hz across eleven recordings: Castellengo & Henrich 2014.)
    static constexpr float kF1Lo = 300.0f, kF1Hi = 950.0f, kF2Hi = 2300.0f, kGap = 180.0f;
    static constexpr float kMelLo = 5.7f; // the melody from the undertone's 6th harmonic
    static constexpr float kHysteresis = 0.25f;
    float m_rate = 48000, m_inner = 8000;
    int m_decim = 6, m_phase = 0;
    Svf m_down[3], m_consHp[2], m_hiBand;
    Voice m_voice;
    float m_buf[kBufN] = {};
    float m_hann[kWin] = {};
    float m_gc[kGrid][kOrder + 1] = {}, m_gs[kGrid][kOrder + 1] = {};
    uint32_t m_w = 0;
    int m_hopAt = 0;
    float m_preX = 0;
    float m_env = 0, m_pow = 0, m_powHi = 0, m_voicing = 0, m_engage = 0;
    float m_envUp = 0, m_envDown = 0, m_vUp = 0, m_vDown = 0, m_engRate = 0;
    float m_glide = 0;
    int m_onsetSamples = 7200;
    float m_powIn = 0, m_powRef = 0, m_powOut = 0, m_guard = 1, m_guardRate = 0, m_refFall = 1;
    float m_f0 = 0, m_sung = 0, m_inHz = 0, m_inClarity = 0, m_steerHz = 0;
    int m_oct = 0, m_unvoiced = 1 << 30, m_away = 0, m_ratio = Half, m_agree = 0, m_phraseAge = 0;
    float m_startHz = 0;
    bool m_inVoiced = false, m_haveVowel = false;
    float m_r1[3] = {500, 500, 500}, m_r2[3] = {900, 900, 900};
    int m_ri = 0;
    float m_f1 = 500, m_f2 = 900, m_want1 = 500, m_want2 = 900;
    int m_k1 = 0, m_k2 = 0;
    uint32_t m_tick = 0;
    float m_consGate = 0;

    int m_roughFor = 0;
    bool m_reading = false;
    Rng m_rng{0x7A11u};
};

}
