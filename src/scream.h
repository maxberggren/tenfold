#pragma once

// A screamed voice, a few metres off: the RIOT style of the Vocal page.
// Not a chain of effects on the voice but the voice made again, the way a
// throat makes a scream, with the singer's own mouth (the vowels and the
// consonants) kept. What it is measured against is in docs/riot-scream.md.
//
//   the mouth    what you sing, analysed every 4 ms by linear prediction
//                (16 poles at 16 kHz, Levinson–Durbin on a 25 ms window,
//                after the first step of IAIF takes your glottis's tilt
//                out): the vocal tract's resonances, your vowel. The lattice
//                filter that undoes them leaves the residual, your source.
//                Its buzz is thrown away; your consonants (made in the
//                mouth, by turbulence) are noise through your own mouth at
//                your own level, so no pitch of yours gets through.
//   the throat   the source is replaced. A glottis of a given area opens
//                and closes at the sung pitch moved by octaves into a
//                screamer's register (about 540 Hz), pressed: a short open
//                phase, a tight closure, a little leak. Above it the false
//                folds, drawn in by the effort, form a second slit in
//                series. Air through two orifices in series obeys
//                Bernoulli for both, U = √(2P/ρ) · Ag·As / √(Ag² + As²):
//                when the false folds are the narrower, they cap every
//                pulse of flow, the distortion of a strained voice with no
//                curve chosen by ear. They flutter at half the glottis's
//                rate (the vibration Borch et al. filmed in rock "dist"
//                tones), so alternate pulses are capped differently: period
//                doubling, subharmonics. The air above them has inertia
//                (the epilarynx's inertance), so the flow lags the opening
//                and stops abruptly: a pressed voice's highs. The jets'
//                turbulence is the Meyer-Eppler and Flanagan noise source,
//                (Re² − Re_c²) above a critical Reynolds number, so the
//                noise rides on each pulse of flow, louder as the flow is.
//                Each cycle's length and height wander a little (jitter,
//                shimmer). How hard you sing is the lungs' pressure:
//                louder is more flow, more noise and more of the false
//                folds. The note is your tracker's, its octave checked
//                against your spectrum, one octave kept for a phrase.
//   the scream's mouth  your vowels moved as a screamer's are: the larynx
//                raised and the tract shortened (all resonances up by a
//                factor, P'(f) = P(f/α), on the spectrum before the poles
//                are found, so they stay stable), and the jaw dropped: your
//                first resonance itself carried onto the second harmonic
//                (F1 ≈ 2·f0, the belter's tuning; over 650 Hz just above
//                the first). It is as loud as you are, vowel by vowel: what
//                the throat will carry through it is computed for each 4 ms
//                and set to your level.
//   the place    the singer is heard from where they are: in a field a few
//                metres off (the reference), with the ground's reflection
//                3 ms behind, a few bodies and frames nearby and almost no
//                tail; or in a small room; or close. The highs fall away
//                as a voice heard off its axis does, and the band is a
//                phone's (its codec ends it near 16 kHz).
//
// Its voice runs at a third of the studio's rate (the reference's
// harmonics end near 5.5 kHz); the jets' noise above that band, and your
// consonants' tops, at the full rate. Sample by sample, no look-ahead:
// about 0.3 ms of filters behind the microphone (a syllable's scream
// starts with the tracker's first reading or, inside a phrase, as soon as
// the vowel is periodic), and half a percent of a core.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace tf::scream {

constexpr int kOrder = 16;  // poles of the mouth
constexpr int kN = 512;     // the analysis's FFT
constexpr int kWin = 400;   // the analysis window at the inner rate: 25 ms
constexpr int kHop = 64;    // a new mouth every 4 ms
constexpr int kBufN = 512;  // the inner rate's recent past (a power of two)
constexpr int kMaxGang = 4; // other screamers at the fingers' notes

// Where the singer is (B).
enum Place { Field, Room, Close, PlaceCount };
inline const char *placeName(int p) {
    static const char *names[] = {"FIELD", "ROOM", "CLOSE"};
    return names[std::clamp(p, 0, PlaceCount - 1)];
}
inline const char *placeKey(int p) {
    static const char *names[] = {"field", "room", "close"};
    return names[std::clamp(p, 0, PlaceCount - 1)];
}

// Butterworth low-passes in state-variable sections: the band's edge going
// down to the inner rate (twelfth order, so an /s/ at 10 kHz folds back
// 46 dB down) and coming back up (eighth).
struct Butter12 {
    Svf s[6];
    void set(float hz, float rate) {
        static const float q[6] = {0.5043f, 0.5412f, 0.6302f, 0.8213f, 1.3066f, 3.8306f};
        for (int i = 0; i < 6; ++i)
            s[i].set(hz, q[i], rate);
    }
    inline float step(float x) {
        for (auto &f : s)
            x = f.step(x).low;
        return x;
    }
    void reset() {
        for (auto &f : s)
            f.reset();
    }
};
struct Butter8 {
    Svf s[4];
    void set(float hz, float rate) {
        static const float q[4] = {0.5098f, 0.6013f, 0.9000f, 2.5629f};
        for (int i = 0; i < 4; ++i)
            s[i].set(hz, q[i], rate);
    }
    inline float step(float x) {
        for (auto &f : s)
            x = f.step(x).low;
        return x;
    }
    void reset() {
        for (auto &f : s)
            f.reset();
    }
};

// Levinson–Durbin: autocorrelation r[0..p] to reflection coefficients
// k[1..p] (A(z) = 1 + Σ a_j z^−j); returns the prediction error's power.
inline float levinson(const float *r, float *k, int p) {
    float a[kOrder + 1] = {}, t[kOrder + 1];
    a[0] = 1;
    float e = r[0];
    if (!(e > 1e-12f)) {
        for (int i = 1; i <= p; ++i)
            k[i] = 0;
        return 0;
    }
    for (int i = 1; i <= p; ++i) {
        float acc = r[i];
        for (int j = 1; j < i; ++j)
            acc += a[j] * r[i - j];
        const float ki = clampf(-acc / e, -0.995f, 0.995f);
        k[i] = ki;
        for (int j = 1; j < i; ++j)
            t[j] = a[j] + ki * a[i - j];
        for (int j = 1; j < i; ++j)
            a[j] = t[j];
        a[i] = ki;
        e *= 1.0f - ki * ki;
    }
    return std::max(e, 1e-12f);
}

// cos(π·x) for x in [0, 2], to a few parts in ten thousand: the throat
// needs two a sample, and the library's would be most of its cost.
inline float cosPi(float x) {
    // Fold onto [0, 0.5] and use the even polynomial there.
    float sign = 1;
    if (x > 1.0f)
        x = 2.0f - x;
    if (x > 0.5f) {
        x = 1.0f - x;
        sign = -1;
    }
    const float z = x * x;
    return sign * (1.0f + z * (-4.934802f + z * (4.058712f + z * (-1.335262f + z * 0.235330f))));
}

// A glottis and the false folds above it, in SI units (but the noise's
// scale, below), stepped at the
// inner rate. Its output is the radiated source: the time derivative of
// the volume velocity through both (the lips radiate dU/dt), with the
// turbulence of the jet in it.
struct Throat {
    // Air.
    static constexpr float kRho = 1.14f;      // kg/m³, warm and humid
    static constexpr float kC = 350.0f;       // m/s
    static constexpr float kNu = 1.6e-5f;     // m²/s, kinematic viscosity
    // The glottis: a slit of 1 cm (a woman's folds), opening to 0.14 cm²
    // at its widest, with a posterior gap of 0.01 cm² that never closes.
    static constexpr float kLength = 0.010f;
    static constexpr float kFalseLength = 0.008f; // the false folds' slit, its edges drawn in
    static constexpr float kAmax = 1.4e-5f;
    static constexpr float kLeak = 1.0e-6f;
    static constexpr float kTract = 3.0e-4f;  // the tract just above, 3 cm²
    // The turbulence source after Meyer-Eppler (1953) and Flanagan & Cherry
    // (1969): p ∝ (Re² − Re_c²) above a critical Reynolds number. Their
    // constants were in cgs units and Re_c is given anywhere from 1800 to
    // 2700; the scale here (2·10⁻⁶ "Pa" per Re², Re_c 2700) is set so the
    // scream's harmonics stand 15–18 dB over the noise between them, as the
    // reference's stand 16.6 dB over.
    static constexpr float kNoiseK = 2.0e-6f;
    static constexpr float kReCrit = 2700.0f;
    // The air above the folds: the epilarynx, a tube about 2.5 cm long of
    // 0.7 cm², its inertance ρ·l/A.
    static constexpr float kInertance = kRho * 0.025f / 0.7e-4f;

    float phase = 0, jit = 1, shim = 1, prev = 0, osc = 0, flow = 0, noiseFlow = 0;
    int parity = 0;
    Rng rng{0x5C2EA1u};

    void reset() {
        phase = 0;
        jit = shim = 1;
        prev = 0;
        osc = 0;
        flow = 0;
        parity = 0;
    }
    // One sample: f0 (Hz), the lungs' pressure P (Pa), how far the voice is
    // sounding (0..1, the folds' oscillation fading in and out), the false
    // folds' resting opening (m²) and how deep their half-rate flutter is.
    inline float step(float f0, float P, float voicing, float As0, float flutter, float jitter, float shimmer,
                      float rate) {
        phase += f0 * jit / rate;
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            parity ^= 1;
            // A new cycle: its length and its height wander (jitter and
            // shimmer, Gaussian, from the sum of four uniforms).
            const float g1 = (rng.unit() + rng.unit() + rng.unit() + rng.unit() - 2.0f) * 1.732f;
            const float g2 = (rng.unit() + rng.unit() + rng.unit() + rng.unit() - 2.0f) * 1.732f;
            jit = 1.0f + jitter * g1;
            shim = std::exp(shimmer * g2);
        }
        // The folds start at once (a screamer attacks hard: 3 ms) and stop
        // over a few cycles (16 ms).
        osc += (voicing - osc) * (voicing > osc ? 0.02f : 0.004f);
        // The open phase: a pressed voice is open under half the cycle,
        // opening slower than it closes, and it closes by collision: the
        // area reaches zero still falling (Rosenberg's quarter cosine),
        // the break in dU/dt that gives a pressed voice its highs.
        constexpr float kOpen = 0.42f, kRise = 0.62f * kOpen, kFall = kOpen - kRise;
        float pulse = 0;
        if (phase < kRise)
            pulse = 0.5f - 0.5f * cosPi(phase * (1.0f / kRise));
        else if (phase < kOpen)
            pulse = cosPi(0.5f * (phase - kRise) * (1.0f / kFall));
        const float Ag = kLeak + kAmax * shim * osc * pulse;
        // The false folds flutter at half the rate: one cosine over two
        // glottal cycles, so the pulses alternate.
        const float As = As0 * (1.0f + flutter * cosPi(float(parity) + phase));
        // The flow: the lungs' pressure spent on the jet through both slits
        // in series (½ρU²/Ag² + ½ρU²/As²) and on accelerating the air
        // column above them (its inertance L·dU/dt). Solved implicitly for
        // this sample's U (the positive root of a quadratic), so it stays
        // stable as the glottis shuts. The inertance makes the flow lag
        // the opening and stop abruptly at the closure (Rothenberg's
        // skewing): the sharp negative peak of dU/dt, a pressed voice's
        // highs.
        const float invA2 = 1.0f / (Ag * Ag) + 1.0f / (As * As);
        const float a = 0.5f * kRho * invA2, b = kInertance * rate, c = std::max(P, 0.0f) + b * flow;
        const float U = c > 0 ? 2.0f * c / (b + std::sqrt(b * b + 4.0f * a * c)) : 0.0f;
        flow = U;
        // The jets: for a slit the Reynolds number is 2U/(ν·L), whatever
        // its width. The glottis's always; the false folds' too, as far as
        // they are a constriction at all (a jet forms there only once they
        // are about as narrow as the glottis), with noise of its own: the
        // rasp of a strained voice.
        const float Re = 2.0f * U / (kNu * kLength);
        const float ReS = 2.0f * U / (kNu * kFalseLength);
        const float jetS = clampf(Ag / As, 0.0f, 1.0f);
        const float aG = std::max(0.0f, Re * Re - kReCrit * kReCrit);
        const float aS = jetS * std::max(0.0f, ReS * ReS - kReCrit * kReCrit);
        const float pn = kNoiseK * (aG * (rng.unit() - 0.5f) + aS * (rng.unit() - 0.5f));
        // A pressure source into the tract's impedance ρc/A is a flow.
        const float Un = pn * kTract / (kRho * kC);
        // How strong the jets' noise is now, as a flow (the noise's RMS:
        // a uniform's is 1/√12): what the band above the scream's own rate
        // is made from.
        noiseFlow = kNoiseK * std::sqrt(aG * aG + aS * aS) * 0.2887f * kTract / (kRho * kC);
        const float total = U + Un;
        const float out = (total - prev) * rate;
        prev = total;
        return out;
    }
};

// The place: the ground's reflection, a few nearby surfaces, a tail; at
// the inner rate, mono in, stereo out.
class Place3 {
public:
    void init(float rate) {
        m_rate = rate;
        m_line.init(size_t(rate * 0.12f));
        // 25–46 ms at 16 kHz, mutually prime: a small room's mean free
        // path (4V/S, 2.4 m for 5 × 4 × 2.7 m) is 7 ms, so a few of them
        // between returns, and the diffusers below fill in between.
        static const int lens[4] = {397, 509, 641, 743};
        for (int i = 0; i < 2; ++i)
            m_diff[i].init(uint32_t(float(i ? 113 : 71) * rate / 16000.0f), 0.6f);
        for (int i = 0; i < 4; ++i) {
            m_fdn[i].init(size_t(float(lens[i]) * rate / 16000.0f) + 8);
            m_len[i] = float(lens[i]) * rate / 16000.0f;
            m_damp[i] = 0;
        }
        set(Field);
    }
    void set(int place) {
        m_place = std::clamp(place, 0, PlaceCount - 1);
        // Taps: delay (ms), gain, and how far left (−1) or right (+1).
        // FIELD: mouth 1.6 m up, the phone 1.2 m up, 3 m apart, on grass:
        // the ground's path is 1.07 m longer (3.1 ms), 0.74 of the direct
        // by spreading, times grass's reflection (about 0.7, less in the
        // highs, done by the tap's own low-pass). Then the players and the
        // frame round them, small and close, and the open air: almost
        // nothing comes back.
        // ROOM: 5 × 4 × 2.7 m, the singer 3 m off: the floor, the near
        // walls, the ceiling, their seconds, by image sources (each tap's
        // gain the spreading to it times the surfaces' 0.9 of the pressure
        // back: a mean absorption of 0.2, as for T60 0.5 s by Sabine).
        struct Tap {
            float ms, gain, pan;
        };
        static const Tap field[] = {{3.1f, 0.52f, 0.0f}, {7.4f, 0.16f, -0.6f}, {11.8f, 0.12f, 0.7f},
                                    {17.3f, 0.09f, -0.3f}, {26.0f, 0.07f, 0.5f}, {39.0f, 0.05f, -0.5f}};
        static const Tap room[] = {{4.4f, 0.55f, 0.0f},  {7.9f, 0.42f, -0.7f}, {9.6f, 0.38f, 0.8f},
                                   {12.5f, 0.33f, 0.1f}, {16.8f, 0.27f, -0.5f}, {21.4f, 0.22f, 0.6f},
                                   {27.0f, 0.18f, -0.2f}, {33.5f, 0.15f, 0.4f}};
        m_taps = 0;
        const Tap *t = m_place == Field ? field : room;
        const int n = m_place == Field ? 6 : m_place == Room ? 8 : 0;
        for (int i = 0; i < n; ++i) {
            m_tapD[i] = t[i].ms * 0.001f * m_rate;
            m_tapL[i] = t[i].gain * std::sqrt(0.5f * (1.0f - t[i].pan));
            m_tapR[i] = t[i].gain * std::sqrt(0.5f * (1.0f + t[i].pan));
            ++m_taps;
        }
        // The tail: what time it takes to fall 60 dB, and how loud it is
        // against the direct sound. A field has hardly any. A room's
        // critical distance, 0.057·√(V/T60), is about 0.6 m, so three
        // metres off the reflected sound outweighs the direct: here by
        // 5 dB (a little under the diffuse-field figure, as a voice is
        // louder toward its listener). Its measured T60 is 0.53 s. (The
        // room is wide stereo, its returns from all round; the field and
        // close are near mono, as the reference's phone is.)
        const float t60 = m_place == Field ? 0.22f : 0.6f;
        m_send = m_place == Field ? 0.06f : m_place == Room ? 2.2f : 0.0f;
        for (int i = 0; i < 4; ++i)
            m_fb[i] = std::pow(10.0f, -3.0f * m_len[i] / (t60 * m_rate));
        // Off its axis a voice loses its highs; the phone's microphone
        // and its own band do the rest. (Air itself takes a few tenths of
        // a dB at 8 kHz over three metres: nothing next to these.)
        m_air.set(m_place == Field ? 5000.0f : m_place == Room ? 4500.0f : 7000.0f, m_rate);
        m_wall.set(m_place == Field ? 2200.0f : 3000.0f, m_rate);
        m_tailLp.set(m_place == Field ? 2000.0f : 3200.0f, m_rate);
        // Each return a little duller (walls and air take the highs).
        m_dampA = 1.0f - std::exp(-kTwoPi * (m_place == Field ? 2500.0f : 4000.0f) / m_rate);
        m_direct = m_place == Field ? 0.9f : m_place == Room ? 0.75f : 1.0f;
    }
    int place() const { return m_place; }
    float direct() const { return m_direct; }
    void reset() {
        m_line.clear();
        for (auto &d : m_fdn)
            d.clear();
        for (auto &d : m_diff)
            d.d.clear();
        for (float &d : m_damp)
            d = 0;
        m_air.y = m_wall.y = m_tailLp.y = 0;
    }
    inline void step(float x, float &l, float &r) {
        const float dir = m_air.step(x);
        m_line.push(m_wall.step(dir));
        float el = 0, er = 0;
        for (int i = 0; i < m_taps; ++i) {
            const float v = m_line.read(m_tapD[i]);
            el += v * m_tapL[i];
            er += v * m_tapR[i];
        }
        l = dir * m_direct + el;
        r = dir * m_direct + er;
        if (m_send > 0) {
            // Four lines through a Householder matrix, each damped.
            float o[4];
            float sum = 0;
            for (int i = 0; i < 4; ++i) {
                o[i] = m_fdn[i].read(m_len[i]);
                m_damp[i] += (o[i] - m_damp[i]) * m_dampA;
                o[i] = m_damp[i] * m_fb[i];
                sum += o[i];
            }
            const float in = m_diff[1].step(m_diff[0].step(m_tailLp.step(dir) * m_send));
            for (int i = 0; i < 4; ++i)
                m_fdn[i].push(o[i] - 0.5f * sum + in);
            l += o[0] + o[2];
            r += o[1] + o[3];
        }
    }

private:
    float m_rate = 16000;
    int m_place = Field;
    Delay m_line;
    int m_taps = 0;
    float m_tapD[8] = {}, m_tapL[8] = {}, m_tapR[8] = {};
    Delay m_fdn[4];
    Allpass m_diff[2];
    float m_dampA = 0.6f;
    float m_len[4] = {}, m_fb[4] = {}, m_damp[4] = {};
    float m_send = 0, m_direct = 1;
    OnePoleLp m_air, m_wall, m_tailLp;
};

// The scream: the studio's rate in and out, the rest at a third of it.
class Scream {
public:
    void init(float rate) {
        m_rate = rate;
        m_decim = std::max(1, int(std::lround(rate / 16000.0f)));
        m_inner = rate / float(m_decim);
        m_down.set(0.40f * m_inner, rate);
        for (auto &u : m_up)
            u.set(0.40f * m_inner, rate);
        m_place.init(m_inner);
        // The phone's low cut (fourth-order Butterworth): with no lips
        // near the microphone there is no proximity bass either.
        m_hp.set(250.0f, 0.5412f, m_inner);
        m_hp2.set(250.0f, 1.3066f, m_inner);
        m_consHp.set(600.0f, 0.7f, m_inner);
        m_consInHp.set(600.0f, 0.7f, m_inner);
        m_lowBand.set(900.0f, m_inner);
        // The band over the inner rate's: from its edge up, as air off a
        // voice falls (−6 dB/oct past 8 kHz, off its axis), at the full rate.
        m_airHp[0].set(0.40f * m_inner, 0.5412f, rate);
        m_airHp[1].set(0.40f * m_inner, 1.3066f, rate);
        m_airLp.set(8000.0f, rate);
        m_codec[0].set(std::min(13000.0f, 0.45f * rate), 0.5412f, rate);
        m_codec[1].set(std::min(13000.0f, 0.45f * rate), 1.3066f, rate);
        tables();
        reset();
    }
    void reset() {
        m_down.reset();
        for (auto &u : m_up)
            u.reset();
        std::fill(std::begin(m_buf), std::end(m_buf), 0.0f);
        m_w = 0;
        m_phase = 0;
        m_hopAt = 0;
        for (int i = 0; i <= kOrder; ++i)
            m_kA[i] = m_kA0[i] = m_kS[i] = m_kS0[i] = 0;
        std::fill(std::begin(m_bA), std::end(m_bA), 0.0f);
        std::fill(std::begin(m_bS), std::end(m_bS), 0.0f);
        std::fill(std::begin(m_bC), std::end(m_bC), 0.0f);
        m_eA = m_eA0 = m_eS = m_eS0 = 0;
        m_throat.reset();
        for (auto &g : m_gangThroat)
            g.reset();
        m_place.reset();
        m_hp.reset();
        m_hp2.reset();
        m_consHp.reset();
        m_consInHp.reset();
        m_consIn = m_consOut = 0;
        m_lowBand.y = 0;
        for (auto &f : m_airHp)
            f.reset();
        for (auto &f : m_codec)
            f.reset();
        m_airLp.y = 0;
        m_env = m_envFast = m_envLow = m_envPeak = m_consMs = 0;
        m_powIn = m_powOut = m_powRef = 0;
        m_guard = 1;
        m_lvlIn = m_lvlOut = 0;
        m_match = 1;
        m_settled = 0;
        m_voicing = 0;
        m_f0 = 0;
        m_oct = 0;
        m_unvoiced = 1 << 30;
        m_sung = 0;
        m_away = 0;
        m_deemph = 0;
        m_preX = 0;
        m_burst = 0.5f;
        m_f1 = 0;
        m_hopGain = m_hopGain0 = 1;
        m_fastPow = m_winPow = m_winPow0 = 0;
        m_hgSmooth = 1;
        m_airAmp = m_airAmp0 = m_consGate = m_consGate0 = 0;
    }
    void setPlace(int p) { m_place.set(p); }
    int place() const { return m_place.place(); }
    // The note sung now, for this block: its pitch (0: none), whether a
    // voice is sounding, and how sure the tracker is of the pitch.
    void setPitch(float hz, bool voiced, float clarity = 1.0f) {
        m_inHz = hz;
        m_inVoiced = voiced && hz > 0;
        m_inSure = clarity >= 0.75f;
        m_inClarity = clarity;
    }
    // The other screamers: each finger a note (Hz as sung, before the
    // octave the scream moves everything by) and a level.
    void setGang(const float *hz, const float *level, int n) {
        m_gangN = std::clamp(n, 0, kMaxGang);
        for (int i = 0; i < m_gangN; ++i) {
            m_gangHz[i] = hz[i];
            m_gangLvl[i] = level[i];
        }
    }
    // The octaves the tracker's reading is moved by, and the scream's
    // pitch now.
    int octave() const { return m_oct; }
    float hz() const { return m_f0; }
    // How hard the throat is working, 0..1: the false folds' share.
    float strain() const { return m_strain; }
    // Where the scream's first resonance is (Hz), and the singer's.
    float f1() const { return m_f1Out; }
    float sungF1() const { return m_f1; }

    // n samples of the voice in; the scream out, added to l and r.
    void process(const float *in, float *l, float *r, int n, float mix0 = 1.0f, float mix1 = 1.0f) {
        follow(n);
        // Before the tracker has a new syllable's note, the throat starts on
        // the pitch the spectrum shows (the best harmonic comb within a
        // fifth of the phrase's note, to a quarter tone), not on the last.
        const bool early = !m_inVoiced && nearNote() && m_unvoiced < int(0.25f * m_rate);
        float target = early ? m_harmHz * std::exp2(float(m_oct)) : m_sung > 0 ? m_sung * std::exp2(float(m_oct)) : m_f0;
        // No throat screams over about C6 or under 150 Hz: a phrase that
        // runs off the register folds back by an octave there.
        while (target > 1300.0f)
            target *= 0.5f;
        while (target > 0 && target < 150.0f)
            target *= 2.0f;
        for (int i = 0; i < n; ++i) {
            const float x = m_down.step(in[i]);
            float yl = 0, yr = 0;
            if (++m_phase >= m_decim) {
                m_phase = 0;
                m_airAmp0 = m_airAmp;
                m_consGate0 = m_consGate;
                innerStep(x, target, yl, yr);
                yl *= float(m_decim);
                yr *= float(m_decim);
            }
            // Above the inner band: the jets' noise goes on up (the same
            // jets, at the full rate), and a consonant's top is the
            // singer's own (what the mouth made there is noise, with no
            // pitch in it).
            const float t = float(m_phase + 1) / float(m_decim);
            const float amp = m_airAmp0 + (m_airAmp - m_airAmp0) * t;
            const float gate = m_consGate0 + (m_consGate - m_consGate0) * t;
            // (Both off the voice's axis, and the phone's codec ends the
            // band: AAC at this rate stops near 16 kHz, and the reference
            // is 40 dB down there.)
            float hi = m_airHp[1].step(m_airHp[0].step(m_airRng.bipolar() * amp + in[i] * gate).high).high;
            hi = m_codec[1].step(m_codec[0].step(m_airLp.step(hi) * m_guard).low).low;
            const float mix = mix0 + (mix1 - mix0) * float(i + 1) / float(n);
            l[i] += (m_up[0].step(yl) + hi) * mix;
            r[i] += (m_up[1].step(yr) + hi) * mix;
        }
    }

private:
    // The note sung, followed. A phrase (after a quarter second without a
    // note) takes the octave that brings its first note nearest a
    // screamer's, and keeps it, so a melody keeps its shape.
    //
    // The tracker is followed only where it is sure, and its octave is
    // checked against the singer's spectrum (octaveFix): it slips an
    // octave up at the end of a syllable and down at the start of one, as
    // sure of itself either way. A reading still an octave or two off the
    // note held is taken as that note (folded back onto it; a singer's own
    // octave leap is lost, to a scream that moves everything by octaves
    // anyway), and the phrase's octave never changes. Any other leap of
    // more than a fourth counts at once if the tracker is all but certain
    // of it, and otherwise once it has lasted 15 ms (the edge of a
    // syllable, where the first guesses wander; until then the throat is
    // quiet, and the consonant and the breath go on).
    // The octave that brings the phrase's note nearest a screamer's, and
    // how far up the tract is drawn with it: the larynx raised and a
    // shorter, more open mouth (a man's moved further, as his voice is).
    void choose() {
        m_oct = std::clamp(int(std::lround(std::log2(kCentreHz / m_sung))), -1, 2);
        m_alpha = std::clamp(1.08f + 0.06f * float(m_oct), 1.0f, 1.24f);
    }

    void follow(int n) {
        m_phraseAge = std::min(m_phraseAge + n, 1 << 30);
        // The reading, put in its octave by the singer's own spectrum.
        const float reading = m_inVoiced ? m_inHz * octaveFix(m_inHz) : 0.0f;
        if (!m_inVoiced) {
            m_unvoiced = std::min(m_unvoiced + n, 1 << 30);
            return;
        }
        const bool fresh = m_unvoiced > int(0.25f * m_rate) || m_sung <= 0;
        m_unvoiced = 0;
        if (fresh) {
            if (m_inClarity < 0.7f)
                return;
            m_sung = reading;
            m_away = 0;
            m_phraseAge = 0;
            choose();
            if (m_f0 <= 0)
                m_f0 = m_sung * std::exp2(float(m_oct));
            return;
        }
        if (!m_inSure)
            return;
        const float semis = 12.0f * std::log2(reading / m_sung);
        const float octs = std::round(semis / 12.0f);
        if (std::fabs(semis) <= 5.0f) {
            m_sung = reading;
            m_away = 0;
        } else if (octs != 0 && std::fabs(octs) <= 2.0f && std::fabs(semis - 12.0f * octs) < 0.8f) {
            m_sung = reading * std::exp2(-octs);
            m_away = 0;
        } else if ((m_inClarity >= 0.93f && m_harmonic > kHarmonic) || (m_away += n) > int(kLeapWait * m_rate)) {
            m_sung = reading;
            m_away = 0;
            // In a phrase's first moments a new note means the first
            // reading was the tracker's guess, not the singer's: the
            // octave is chosen again, from this one.
            if (m_phraseAge < int(0.15f * m_rate))
                choose();
        }
    }

    // Whether what is sounding is periodic, at a pitch within a tone of
    // the phrase's note: a vowel the throat can start on before the tracker
    // has it (a leap waits for the tracker, so it starts on its note).
    bool nearNote() const {
        return m_sung > 0 && m_harmHz > 0 && m_harmonic > kHarmonic &&
               std::fabs(12.0f * std::log2(m_harmHz / m_sung)) <= 2.5f;
    }

    // The tracker's octave, checked against the spectrum it came from (the
    // last 25 ms, at most 4 ms old), by subharmonic summation, as a
    // listener hears a pitch: if the odd multiples of half the reading are
    // there, it read the voice's second harmonic; if the reading's own odd
    // harmonics are missing, it read half the voice.
    float octaveFix(float r) const {
        const float hzPerBin = m_inner / float(kN);
        auto at = [&](float f) {
            const int b = int(f / hzPerBin + 0.5f);
            if (b < 1 || b >= kN / 2)
                return 0.0f;
            return std::max({m_lastP[b - 1], m_lastP[b], m_lastP[b + 1]});
        };
        auto series = [&](float f0, int first, int step) {
            float sum = 0;
            for (int k = first; float(k) * f0 < 4000.0f && k <= 15; k += step)
                sum += at(float(k) * f0);
            return sum;
        };
        if (r * 0.5f >= 70.0f && series(r * 0.5f, 1, 2) > 0.5f * series(r, 1, 1))
            return 0.5f;
        if (r * 2.0f <= 1000.0f && series(r, 1, 2) < 0.1f * series(r, 2, 2))
            return 2.0f;
        return 1.0f;
    }

    static constexpr float kCentreHz = 540.0f; // the reference's wail: 506–558 Hz
    static constexpr float kLeapWait = 0.015f; // how long a leap must last to be one
    static constexpr float kPre = 0.92f;       // pre-emphasis at the inner rate

    void innerStep(float x, float target, float &outL, float &outR) {
        // The mouth's analysis runs on the pre-emphasised voice.
        const float xp = x - kPre * m_preX;
        m_preX = x;
        m_buf[m_w++ & (kBufN - 1)] = xp;
        if (++m_hopAt >= kHop) {
            m_hopAt = 0;
            analyse();
        }
        const float t = float(m_hopAt) / float(kHop);
        // The lungs: the voice's level (fast up, slower down) as pressure.
        const float ax = std::fabs(x);
        m_env += (ax - m_env) * (ax > m_env ? 0.02f : 0.002f);
        const float loud = clampf(m_env / kEnvRef, 0.0f, 1.3f);
        const float P = 700.0f + 2300.0f * loud; // Pa: 0.7 kPa soft to 3 kPa
        m_strain = clampf((P - 1400.0f) / 1400.0f, 0.0f, 1.0f);
        m_strain = m_strain * m_strain * (3.0f - 2.0f * m_strain);
        // The false folds: well open in a soft voice, drawn in to under
        // the glottis's widest as the effort grows, and fluttering in
        // bursts that come and go.
        if ((m_rng.next() & 1023u) == 0)
            m_burstTarget = m_rng.unit();
        m_burst += (m_burstTarget - m_burst) * 0.0008f;
        const float As0 = Throat::kAmax * (2.4f - 1.65f * m_strain);
        const float flutter = 0.03f + 0.14f * m_strain * m_burst;

        // Is a vowel sounding? The tracker says so 20 ms into it; inside a
        // phrase (a note held) the throat doesn't wait for it, but starts
        // as soon as the voice is up and its energy is low (a vowel, a
        // nasal, a liquid; not a hiss or a burst): a vowel after a
        // consonant is screamed from its start.
        m_envFast += (ax - m_envFast) * (ax > m_envFast ? 0.15f : 0.01f);
        const float low = m_lowBand.step(x);
        m_envLow += (std::fabs(low) - m_envLow) * (std::fabs(low) > m_envLow ? 0.15f : 0.01f);
        m_envPeak = std::max(m_envFast, m_envPeak * 0.99995f);
        const bool inPhrase = m_sung > 0 && m_unvoiced < int(0.25f * m_rate);
        const bool vowel = inPhrase && m_envFast > 0.2f * m_envPeak && m_envLow > 0.6f * m_envFast && nearNote();
        // (And never on a hiss or a burst the tracker calls voiced: what
        // sounds must be periodic.)
        const bool sounding = ((m_inVoiced && m_sung > 0) || vowel) && m_away == 0 && m_harmonic > kPeriodic;
        // A screamer's attack: up in 2 ms, down over 10.
        m_voicing += ((sounding ? 1.0f : 0.0f) - m_voicing) * (sounding ? 0.03f : 0.006f);
        // The pitch glides to the note in 2.5 ms (no step when the tracker
        // steps).
        if (target > 0)
            m_f0 = m_f0 > 0 ? m_f0 * std::exp2(std::log2(target / m_f0) * 0.025f) : target;

        // The singer's residual: what the mouth was given.
        float e;
        {
            float f = xp, bPrev = xp;
            for (int i = 1; i <= kOrder; ++i) {
                const float k = m_kA0[i] + (m_kA[i] - m_kA0[i]) * t;
                const float bi = m_bA[i - 1];
                const float fi = f + k * bi;
                m_bA[i - 1] = bPrev;
                bPrev = k * f + bi;
                f = fi;
            }
            m_bA[kOrder] = bPrev;
            e = f;
        }
        const float eS = m_eS0 + (m_eS - m_eS0) * t;
        // The consonants are made in the singer's own mouth, by turbulence:
        // noise. The residual's level is followed (its mean square over
        // 2 ms) and
        // given to fresh noise, which goes through the singer's own mouth
        // (the analysis's poles, not the scream's: a hiss is shaped where
        // it is made) and back from the pre-emphasis. So a hiss or a burst
        // comes through at its own level and colour, and the singer's buzz,
        // at the singer's pitch, never does. Only over 600 Hz, where a
        // consonant's noise is.
        const float e2 = e * e;
        m_consMs += (e2 - m_consMs) * 0.03f;
        float cons;
        {
            float f = m_rng.bipolar() * 1.732f * std::sqrt(m_consMs);
            for (int i = kOrder; i >= 1; --i) {
                const float k = m_kA0[i] + (m_kA[i] - m_kA0[i]) * t;
                f -= k * m_bC[i - 1];
                m_bC[i] = k * f + m_bC[i - 1];
            }
            m_bC[0] = f;
            if (!std::isfinite(f)) {
                std::fill(std::begin(m_bC), std::end(m_bC), 0.0f);
                f = 0;
            }
            m_deemph = f + kPre * m_deemph;
            cons = m_consHp.step(m_deemph).high;
            // At the singer's own level over 600 Hz, held to it every 2 ms
            // (the noise through a mouth found 4 ms ago, a stop's closure's
            // mouth at a burst, would land somewhere else).
            const float hx = m_consInHp.step(x).high;
            m_consIn += (hx * hx - m_consIn) * 0.03f;
            m_consOut += (cons * cons - m_consOut) * 0.03f;
            cons *= std::min(std::sqrt(m_consIn / (m_consOut + 1e-14f)), 30.0f) * kConsLift;
        }

        // The new source, normalised by what a full cycle of it carries
        // (so the singer's level, not the model's, sets the loudness, and
        // the pressure only the timbre).
        const float pressureFlow = Throat::kAmax * std::sqrt(2.0f * P / Throat::kRho);
        const float norm = 1.0f / (pressureFlow * std::max(m_f0, 60.0f) * kSrcScale);
        float src = m_throat.step(m_f0, P, m_voicing, As0, flutter, 0.0035f, 0.06f, m_inner) * norm;
        float noiseFlow = m_throat.noiseFlow * m_throat.osc * norm;
        for (int g = 0; g < m_gangN; ++g) {
            float gHz = m_gangHz[g] * std::exp2(float(m_oct));
            while (gHz > 1300.0f)
                gHz *= 0.5f;
            while (gHz > 0 && gHz < 150.0f)
                gHz *= 2.0f;
            const float gn = m_gangLvl[g] / (pressureFlow * std::max(gHz, 60.0f) * kSrcScale);
            src += m_gangThroat[g].step(gHz, P, m_voicing * (m_gangLvl[g] > 0.001f ? 1.0f : 0.0f), As0, flutter,
                                        0.005f, 0.07f, m_inner) * gn;
            noiseFlow += m_gangThroat[g].noiseFlow * m_gangThroat[g].osc * gn;
        }
        const float v = m_voicing;
        // The gain is the window's; the voice's level over the last 8 ms
        // corrects it by up to 12 dB either way, so a vowel comes in as
        // fast as it is sung and a closure falls as fast.
        m_fastPow += (x * x - m_fastPow) * 0.008f;
        const float winPow = m_winPow0 + (m_winPow - m_winPow0) * t;
        const float fast = clampf(std::sqrt((m_fastPow + 1e-14f) / (winPow + 1e-14f)), 0.25f, 4.0f);
        // (Followed through 8 ms, not stepped hop by hop: a gain that moved
        // every 4 ms would ring sidebands 250 Hz either side of every
        // harmonic.)
        m_hgSmooth += (m_hopGain * fast - m_hgSmooth) * 0.008f;
        const float hg = m_hgSmooth;
        const float exc = std::sqrt(eS) * hg * src;

        // The scream's mouth: an all-pole lattice on the moved poles. (No
        // de-emphasis: the mouth was found on the pre-emphasised voice,
        // which is the tract alone, and the new source is already what the
        // lips radiate.)
        float f = exc;
        for (int i = kOrder; i >= 1; --i) {
            const float k = m_kS0[i] + (m_kS[i] - m_kS0[i]) * t;
            f -= k * m_bS[i - 1];
            m_bS[i] = k * f + m_bS[i - 1];
        }
        m_bS[0] = f;
        float y = f;
        if (!std::isfinite(y)) {
            std::fill(std::begin(m_bS), std::end(m_bS), 0.0f);
            y = 0;
        }
        y = m_hp2.step(m_hp.step(y).high).high;
        // What is left of a difference in level, over a second of settled
        // voice (30 ms into a vowel, when the mouth is the vowel's), is
        // taken out slowly: the scream as loud as the singer.
        m_settled = v > 0.9f ? std::min(m_settled + 1, 1 << 30) : 0;
        if (m_settled > int(0.03f * m_inner)) {
            m_lvlIn += (x * x - m_lvlIn) * kLvlRate;
            m_lvlOut += (y * y - m_lvlOut) * kLvlRate;
            if (m_lvlOut > 1e-12f && m_lvlIn > 1e-12f) {
                const float want = clampf(std::sqrt(m_lvlIn / m_lvlOut) * kLevel, 0.25f, 4.0f);
                m_match += (want - m_match) * kSlowRate;
            }
        }
        y = y * m_match + (1.0f - v) * cons;
        m_place.step(y, outL, outR);
        // A guard, not a sound. A voice's mouth is broad, but a pure tone's
        // is one sharp pole, and a harmonic of the scream on it, or the
        // tone on one of the room's modes, would ring far louder than what
        // went in. Over 20 ms what comes out may be up to 18 dB over the
        // singer's level; past that it is held there. (Sung and spoken
        // voices measure at most 11 dB over, and a room adds 5.)
        // (The singer's level held, falling no faster than 60 dB a second,
        // so a room's tail after the voice stops is never taken for one.)
        m_powIn += (x * x - m_powIn) * 0.003f;
        m_powRef = std::max(m_powIn, m_powRef * kRefFall);
        m_powOut += (0.5f * (outL * outL + outR * outR) - m_powOut) * 0.003f;
        const float room = 63.0f * m_powRef + 1e-12f;
        const float g = m_powOut > room ? std::sqrt(room / m_powOut) : 1.0f;
        m_guard += (g - m_guard) * (g < m_guard ? 0.05f : 0.002f);
        outL *= m_guard;
        outR *= m_guard;
        // The jets' noise above the band, at the level it has in the band:
        // a flow's derivative, through the mouth's gain at its top (its
        // error, as for the source), at the same match.
        m_airAmp = noiseFlow * m_inner * std::sqrt(eS) * hg * kAir * m_match * m_place.direct();
        // The singer's own consonant top, as their consonant is let through.
        m_consGate = (1.0f - v) * kMouthTop;
    }

    void analyse() {
        // The power spectrum of the last 25 ms through a Hann window (zero-
        // padded to 512, so the lags wanted don't wrap round).
        float re[kN], im[kN];
        const uint32_t start = m_w - uint32_t(kWin);
        for (int i = 0; i < kN; ++i) {
            re[i] = i < kWin ? m_buf[(start + uint32_t(i)) & (kBufN - 1)] * m_hann[i] : 0.0f;
            im[i] = 0;
        }
        fft(re, im);
        float P[kN / 2 + 1];
        for (int j = 0; j <= kN / 2; ++j)
            P[j] = re[j] * re[j] + im[j] * im[j];
        std::copy(P, P + kN / 2 + 1, m_lastP);
        // Is what is sounding a vowel: periodic? (The share of its energy,
        // up to 4 kHz, on the harmonics of the best pitch within a fifth of
        // the phrase's note: a vowel's is most of it, a hiss's or an /h/'s
        // a fraction.)
        m_harmonic = 0;
        if (m_sung > 0) {
            const float hzPerBin = m_inner / float(kN);
            const int top = int(4000.0f / hzPerBin);
            float total = 0;
            for (int j = 2; j <= top; ++j)
                total += P[j];
            if (total > 1e-20f) {
                for (int q = -14; q <= 14; ++q) {
                    const float f0 = m_sung * std::exp2(float(q) / 24.0f);
                    float on = 0;
                    int last = -10;
                    for (int k = 1; float(k) * f0 < 4000.0f; ++k) {
                        const int b = int(float(k) * f0 / hzPerBin + 0.5f);
                        for (int q = std::max(b - 1, last + 1); q <= b + 1; ++q)
                            if (q >= 2 && q <= top)
                                on += P[q];
                        last = b + 1;
                    }
                    if (on / total > m_harmonic) {
                        m_harmonic = on / total;
                        m_harmHz = f0;
                    }
                }
            }
        }
        // The singer's glottis out of it, as IAIF's first step does (Alku
        // 1992): a first-order fit of the spectrum is the source's tilt,
        // divided out, so the mouth found below is the tract and a breathy
        // or a soft singer doesn't make the scream dull.
        float Pt[kN / 2 + 1];
        {
            float r0 = 0, r1 = 0;
            for (int j = 0; j <= kN / 2; ++j) {
                r0 += P[j] * m_cos[0][j];
                r1 += P[j] * m_cos[1][j];
            }
            const float a1 = r0 > 1e-20f ? clampf(-r1 / r0, -kTiltMax, kTiltMax) : 0.0f;
            for (int j = 0; j <= kN / 2; ++j)
                Pt[j] = P[j] * (1.0f + a1 * a1 + 2.0f * a1 * m_cosW[j]);
        }
        // How smooth the mouth is taken: a resonance is widened by at least
        // 60 Hz, and by a third of the sung pitch, so the poles find the
        // tract and never the singer's harmonics (a high voice's are as far
        // apart as its formants; 16 poles would sit on them).
        const float bwHz = std::max(60.0f, m_inVoiced ? 0.33f * m_inHz : 0.0f);
        float lag[kOrder + 1];
        {
            const float bw = kTwoPi * bwHz / m_inner;
            for (int k = 0; k <= kOrder; ++k)
                lag[k] = std::exp(-0.5f * bw * bw * float(k * k));
        }
        auto toLags = [&](const float *S, float *r, int order) {
            for (int k = 0; k <= order; ++k) {
                const float *c = m_cos[k];
                float sum = 0;
                for (int j = 0; j <= kN / 2; ++j)
                    sum += S[j] * c[j];
                r[k] = sum * lag[k];
            }
            r[0] *= 1.0001f;
        };
        // The singer's first resonance: the lowest peak of the tract
        // (ten poles), between 200 Hz and 1.4 kHz and within 20 dB of its
        // highest.
        {
            float rt[11], kt[11];
            toLags(P, rt, 10);
            if (rt[0] > 1e-10f) {
                levinson(rt, kt, 10);
                float a[11];
                reflToPoly(kt, a, 10);
                float best = 0, top = 0, env[kF1Grid];
                for (int g = 0; g < kF1Grid; ++g) {
                    env[g] = envelopeAt(a, 10, 200.0f + 25.0f * float(g));
                    top = std::max(top, env[g]);
                }
                for (int g = 1; g + 1 < kF1Grid; ++g)
                    if (env[g] >= env[g - 1] && env[g] >= env[g + 1] && env[g] > 0.01f * top &&
                        200.0f + 25.0f * float(g) <= 1400.0f) {
                        best = 200.0f + 25.0f * float(g);
                        break;
                    }
                if (best > 0)
                    m_f1 = m_f1 > 0 ? m_f1 * std::exp2(0.3f * std::log2(best / m_f1)) : best;
            }
        }
        // The scream's mouth. Every resonance up by α (the larynx raised,
        // the tract shorter), and the jaw dropped: the first resonance
        // carried onto the second harmonic (the belter's tuning; over
        // 650 Hz, onto just above the first), as a block of ±δ round it,
        // so its width grows by α only; between it and the knee (where the
        // plain α takes over) the spectrum is stretched to fit. Moved in
        // the spectrum, it stays a spectrum (nothing negative), so the
        // poles found from it are always stable.
        float Ps[kN / 2 + 1];
        {
            const float hzPerBin = m_inner / float(kN);
            const float a = m_alpha;
            float x[5] = {}, y[5] = {};
            int np = 0;
            const float f0 = std::max(m_f0, 100.0f);
            const float f1t = m_f1 > 0 && m_voicing > 0.05f ? (2.0f * f0 <= 1300.0f ? 2.0f * f0 : 1.1f * f0) : 0.0f;
            if (f1t > 0) {
                const float f1 = m_f1, d = std::min(120.0f, 0.4f * f1);
                const float kneeOut = std::max(2.0f * (f1t + d * a), f1t + d * a + 900.0f);
                const float kneeIn = std::max(kneeOut / a, f1 + d + 200.0f);
                x[0] = 0, y[0] = 0;
                x[1] = f1 - d, y[1] = std::max(f1t - d * a, 1.0f);
                x[2] = f1 + d, y[2] = f1t + d * a;
                x[3] = kneeIn, y[3] = std::max(kneeIn * a, y[2] + 1.0f);
                np = 4;
            }
            for (int j = 0; j <= kN / 2; ++j) {
                const float fo = float(j) * hzPerBin;
                float fi = fo / a;
                if (np > 0 && fo < y[np - 1]) {
                    int s = 0;
                    while (s + 1 < np - 1 && fo >= y[s + 1])
                        ++s;
                    fi = x[s] + (fo - y[s]) * (x[s + 1] - x[s]) / (y[s + 1] - y[s]);
                } else if (np > 0) {
                    fi = x[np - 1] + (fo - y[np - 1]) / a;
                }
                const float pos = std::min(fi / hzPerBin, float(kN / 2) - 1.001f);
                const int i = int(pos);
                const float fr = pos - float(i);
                Ps[j] = Pt[i] + (Pt[i + 1] - Pt[i]) * fr;
            }
            m_f1Out = f1t > 0 ? f1t : m_f1 * a;
        }
        // Both back to lags 0..16 (cosine sums), per sample (the window's
        // own energy is 3/8 of its length), widened, and with a floor 40 dB
        // down, so a pure tone can't make a pole on the circle.
        float ra[kOrder + 1], rs[kOrder + 1];
        toLags(P, ra, kOrder);
        toLags(Ps, rs, kOrder);
        rs[0] *= 1.0f;
        for (int i = 0; i <= kOrder; ++i) {
            m_kA0[i] = m_kA[i];
            m_kS0[i] = m_kS[i];
        }
        m_eA0 = m_eA;
        m_eS0 = m_eS;
        if (ra[0] < 1e-10f) {
            // Silence: the mouth stays as it was, the level goes.
            m_eA = std::max(m_eA * 0.5f, 1e-12f);
            m_eS = m_eS * 0.5f;
            return;
        }
        m_eA = levinson(ra, m_kA, kOrder);
        m_eS = levinson(rs, m_kS, kOrder);
        // The error of the moved mouth at the singer's level: its filter
        // brings white noise of power eS up to r'(0), which should be r(0).
        m_eS *= ra[0] / std::max(rs[0], 1e-12f);
        // As loud as the singer, vowel by vowel. What the scream will
        // carry through this mouth is known: its harmonics (the throat's
        // own, as measured) through |1/A'|
        // at each. It is set to the singer's level as heard. (The mouth's
        // own level, eS, is the pre-emphasised voice's, which counts a
        // back vowel's low resonances for little: an /u/ would be 10 dB
        // quieter than the /u/ sung.)
        m_hopGain0 = m_hopGain;
        if (m_f0 > 0 && m_eS > 0) {
            float a[kOrder + 1];
            reflToPoly(m_kS, a, kOrder);
            float pred = 0, wsum = 0;
            // The throat's harmonics as measured on it (pressed, at 3 kPa):
            // H1 −1, H2 0, H3 −6, H4 −9 dB … then about 7.5 dB an octave.
            static const float kSrcDb[16] = {-1, 0, -6, -9, -7, -12, -11, -13, -15, -15, -18, -18, -20, -20, -21, -22};
            for (int h = 1; h <= 16 && float(h) * m_f0 < 0.45f * m_inner; ++h) {
                const float w = dbToGain(kSrcDb[h - 1] * 0.5f) * dbToGain(kSrcDb[h - 1] * 0.5f);
                pred += w * envelopeAt(a, kOrder, float(h) * m_f0);
                wsum += w;
            }
            pred = m_eS * pred / std::max(wsum, 1e-12f);
            // (The singer's level from the same window the mouth is from,
            // its spectrum taken back from the pre-emphasis, so the two
            // agree at every edge of a vowel.)
            float plain = 0;
            for (int j = 0; j <= kN / 2; ++j)
                plain += P[j] * m_deemphW[j] * m_cos[0][j];
            m_hopGain = clampf(std::sqrt(plain / std::max(pred, 1e-20f)), 0.1f, 6.0f);
            m_winPow0 = m_winPow;
            m_winPow = plain;
        }
        // Out of silence the level starts where it is, not glided up from
        // nothing.
        if (m_eA0 < 1e-3f * m_eA) {
            m_eA0 = m_eA;
            m_eS0 = m_eS;
        }
    }

    // Reflection coefficients to A(z)'s, and |1/A| at a frequency.
    static void reflToPoly(const float *k, float *a, int p) {
        float t[kOrder + 1];
        a[0] = 1;
        for (int i = 1; i <= p; ++i) {
            for (int j = 1; j < i; ++j)
                t[j] = a[j] + k[i] * a[i - j];
            for (int j = 1; j < i; ++j)
                a[j] = t[j];
            a[i] = k[i];
        }
    }
    float envelopeAt(const float *a, int p, float hz) const {
        // e^{−jwk} by rotation: one sine and cosine, not one per pole.
        const float w = kTwoPi * hz / m_inner;
        const float c = std::cos(w), sn = std::sin(w);
        float zr = 1, zi = 0, re = 0, im = 0;
        for (int k = 0; k <= p; ++k) {
            re += a[k] * zr;
            im -= a[k] * zi;
            const float t = zr * c - zi * sn;
            zi = zr * sn + zi * c;
            zr = t;
        }
        return 1.0f / (re * re + im * im + 1e-12f);
    }

    // A radix-2 FFT, in place, of kN points.
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
    void tables() {
        for (int i = 0; i < kWin; ++i)
            m_hann[i] = 0.5f - 0.5f * std::cos(kTwoPi * (float(i) + 0.5f) / float(kWin));
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
            m_twr[j] = std::cos(kTwoPi * float(j) / float(kN));
            m_twi[j] = -std::sin(kTwoPi * float(j) / float(kN));
        }
        // r(k) = Σ P(j)·cos(2πjk/N)·(2 between the ends, 1 at them) / N,
        // per sample of the window's energy.
        const float scale = 1.0f / (float(kN) * 0.375f * float(kWin));
        for (int k = 0; k <= kOrder; ++k)
            for (int j = 0; j <= kN / 2; ++j)
                m_cos[k][j] = std::cos(kTwoPi * float(j * k) / float(kN)) * (j == 0 || j == kN / 2 ? 1.0f : 2.0f) * scale;
        for (int j = 0; j <= kN / 2; ++j) {
            m_cosW[j] = std::cos(kTwoPi * float(j) / float(kN));
            m_deemphW[j] = 1.0f / (1.0f + kPre * kPre - 2.0f * kPre * m_cosW[j]);
        }
    }

    // Calibration, measured on the test singers (docs/riot-scream.md):
    // what a full source cycle carries; the consonants' noise back to the
    // residual's power above 1.2 kHz; the level against the singer's
    // (1: as loud), and what counts as a full voice; the jets' noise
    // above the band and the singer's own consonant top there.
    static constexpr float kSrcScale = 2.4f;
    static constexpr float kTiltMax = 0.7f;
    static constexpr float kConsLift = 1.6f;
    static constexpr float kLevel = 1.0f;
    static constexpr float kEnvRef = 0.12f;
    static constexpr float kAir = 3.5f;
    static constexpr float kRefFall = 0.99914f; // −60 dB a second at 16 kHz, in power
    static constexpr float kMouthTop = 1.0f;
    static constexpr float kLvlRate = 1.0f / 1600.0f;   // 100 ms
    static constexpr float kSlowRate = 1.0f / 16000.0f; // matched over a second
    static constexpr int kF1Grid = 49;
    static constexpr float kHarmonic = 0.5f;
    static constexpr float kPeriodic = 0.4f; // and the least of it for the throat to sound at all     // a vowel's share of energy on its harmonics                  // 200 Hz to 1.4 kHz by 25

    float m_rate = 48000, m_inner = 16000;
    int m_decim = 3, m_phase = 0;
    Butter12 m_down;
    Butter8 m_up[2];
    float m_preX = 0, m_deemph = 0;
    float m_buf[kBufN] = {};
    float m_hann[kWin] = {}, m_twr[kN / 2] = {}, m_twi[kN / 2] = {}, m_cos[kOrder + 1][kN / 2 + 1] = {};
    float m_cosW[kN / 2 + 1] = {};
    int m_rev[kN] = {};
    uint32_t m_w = 0;
    int m_hopAt = 0;
    float m_kA[kOrder + 1] = {}, m_kA0[kOrder + 1] = {}, m_kS[kOrder + 1] = {}, m_kS0[kOrder + 1] = {};
    float m_bA[kOrder + 1] = {}, m_bS[kOrder + 1] = {}, m_bC[kOrder + 1] = {};
    float m_eA = 0, m_eA0 = 0, m_eS = 0, m_eS0 = 0;
    float m_alpha = 1.14f, m_f1 = 0, m_f1Out = 0, m_hopGain = 1, m_hopGain0 = 1;
    float m_deemphW[kN / 2 + 1] = {};
    float m_fastPow = 0, m_winPow = 0, m_winPow0 = 0, m_hgSmooth = 1;
    Throat m_throat, m_gangThroat[kMaxGang];
    float m_gangHz[kMaxGang] = {}, m_gangLvl[kMaxGang] = {};
    int m_gangN = 0;
    Place3 m_place;
    Svf m_hp, m_hp2, m_consHp, m_consInHp, m_airHp[2], m_codec[2];
    OnePoleLp m_lowBand, m_airLp;
    float m_powRef = 0;
    float m_powIn = 0, m_powOut = 0, m_guard = 1, m_lvlIn = 0, m_lvlOut = 0, m_match = 1;
    int m_settled = 0;
    float m_consIn = 0, m_consOut = 0;
    float m_env = 0, m_envFast = 0, m_envLow = 0, m_envPeak = 0, m_consMs = 0;
    float m_voicing = 0, m_f0 = 0, m_strain = 0, m_burst = 0.5f, m_burstTarget = 0.5f;
    float m_airAmp = 0, m_airAmp0 = 0, m_consGate = 0, m_consGate0 = 0;
    int m_oct = 0, m_unvoiced = 1 << 30;
    float m_inHz = 0, m_sung = 0, m_inClarity = 0;
    float m_lastP[kN / 2 + 1] = {}, m_harmonic = 0, m_harmHz = 0;
    int m_away = 0, m_phraseAge = 0;
    bool m_inVoiced = false, m_inSure = false;
    Rng m_rng{0xB1C1u}, m_airRng{0xA1E5u};
};

}
