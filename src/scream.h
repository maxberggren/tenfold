#pragma once

// A screamed voice, a few metres off: the RIOT style of the Vocal page.
// Not a chain of effects on the voice but the voice made again, the way a
// throat makes a scream, with the singer's own mouth (the vowels and the
// consonants) kept. What it is measured against is in docs/riot-scream.md.
//
//   the mouth    what you sing, analysed every 4 ms by linear prediction
//                (16 poles at 16 kHz, Levinson–Durbin on a 25 ms window):
//                the vocal tract's resonances, your vowel. The lattice
//                filter that undoes them leaves the residual, your source:
//                buzz while voiced, hiss and bursts in the consonants.
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
//                doubling, subharmonics. The jet's turbulence is Flanagan's
//                noise source, (Re² − Re_c²) above a critical Reynolds
//                number, so the noise rides on each pulse of flow, louder
//                as the flow is. Each cycle's length and height wander a
//                little (jitter, shimmer). How hard you sing is the lungs'
//                pressure: louder is more flow, more noise and more of the
//                false folds.
//   the scream's mouth  your vowels moved up as a screamer's are: the
//                larynx raised and the tract shortened (all resonances up
//                by a factor, done on the autocorrelation, r'(k) = r(αk),
//                before the poles are found), the jaw dropped so the first
//                resonance rides on the second harmonic (F1 ≈ 2·f0, the
//                belter's tuning).
//   the place    the singer is heard from where they are: in a field a few
//                metres off (the reference), with the ground's reflection
//                3 ms behind, a few bodies and frames nearby and almost no
//                tail; or in a small room; or close. The highs fall away
//                as a voice heard off its axis does, and the band is a
//                phone's.
//
// All of it runs at a third of the studio's rate (the scream's band ends
// near 6 kHz, as the reference's does), sample by sample with no look-
// ahead: under a millisecond behind the microphone, and well under one
// percent of a core.

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

// An eighth-order Butterworth low-pass, four state-variable sections: the
// band's edge going down to the inner rate and coming back up.
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

// A glottis and the false folds above it, in SI units, stepped at the
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
    // Flanagan's turbulence source: p = 2·10⁻⁶·(Re² − Re_c²) Pa above the
    // critical Reynolds number.
    static constexpr float kNoiseK = 2.0e-6f;
    static constexpr float kReCrit = 2700.0f;
    // The air above the folds: the epilarynx, a tube about 2.5 cm long of
    // 0.7 cm², its inertance ρ·l/A.
    static constexpr float kInertance = kRho * 0.025f / 0.7e-4f;

    float phase = 0, jit = 1, shim = 1, prev = 0, osc = 0, flow = 0;
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
        osc += (voicing - osc) * 0.004f; // ~16 ms at 16 kHz: the folds start and stop over a few cycles
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
        const float pn = kNoiseK * (std::max(0.0f, Re * Re - kReCrit * kReCrit) * (rng.unit() - 0.5f) +
                                    jetS * std::max(0.0f, ReS * ReS - kReCrit * kReCrit) * (rng.unit() - 0.5f));
        // A pressure source into the tract's impedance ρc/A is a flow.
        const float Un = pn * kTract / (kRho * kC);
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
        // walls, the ceiling, their seconds, by image sources, painted
        // walls (0.8 of the pressure back).
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
        // against the direct sound. A field has hardly any; a room's is
        // as loud as the singer three metres off (the critical distance
        // of a small furnished room is about that).
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
        m_down.set(0.42f * m_inner, rate);
        for (auto &u : m_up)
            u.set(0.40f * m_inner, rate);
        m_place.init(m_inner);
        // The phone's low cut (fourth-order Butterworth): with no lips
        // near the microphone there is no proximity bass either.
        m_hp.set(250.0f, 0.5412f, m_inner);
        m_hp2.set(250.0f, 1.3066f, m_inner);
        m_residHp.set(1200.0f, 0.7f, m_inner);
        m_belt.set(1000.0f, 4.0f, m_inner);
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
        m_eA = m_eA0 = m_eS = m_eS0 = 0;
        m_throat.reset();
        for (auto &g : m_gangThroat)
            g.reset();
        m_place.reset();
        m_belt.reset();
        m_hp.reset();
        m_hp2.reset();
        m_residHp.reset();
        m_env = 0;
        m_powIn = m_powOut = 0;
        m_guard = 1;
        m_voicing = 0;
        m_f0 = 0;
        m_oct = 0;
        m_unvoiced = 1 << 30;
        m_sung = 0;
        m_away = 0;
        m_slip = 0;
        m_deemph = 0;
        m_preX = 0;
        m_burst = 0.5f;
    }
    void setPlace(int p) { m_place.set(p); }
    int place() const { return m_place.place(); }
    // The note sung now, for this block: its pitch (0: none), whether a
    // voice is sounding, and how sure the tracker is of the pitch.
    void setPitch(float hz, bool voiced, float clarity = 1.0f) {
        m_inHz = hz;
        m_inVoiced = voiced && hz > 0;
        m_inSure = clarity >= 0.75f;
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
    // The octave the phrase is moved by, and the scream's pitch now.
    int octave() const { return m_oct; }
    float hz() const { return m_f0; }
    // How hard the throat is working, 0..1: the false folds' share.
    float strain() const { return m_strain; }

    // n samples of the voice in; the scream out, added to l and r.
    void process(const float *in, float *l, float *r, int n, float mix0 = 1.0f, float mix1 = 1.0f) {
        // A new phrase (a quarter second without a note) takes the octave
        // that puts its first note nearest a screamer's: it is kept until
        // the phrase ends, so a melody keeps its shape.
        if (m_inVoiced) {
            if (m_unvoiced > int(0.25f * m_rate)) {
                m_sung = m_inHz;
                m_away = m_slip = 0;
                m_phraseAge = 0;
                choose();
                if (m_f0 <= 0)
                    m_f0 = m_inHz * std::exp2(float(m_oct));
            }
            m_unvoiced = 0;
        } else {
            m_unvoiced = std::min(m_unvoiced + n, 1 << 30);
        }
        m_phraseAge = std::min(m_phraseAge + n, 1 << 30);
        // The note is followed only where the tracker is sure, and a leap
        // of more than a fourth only once it has lasted 30 ms: at the
        // edge of a syllable the tracker's first guesses wander, and a
        // throat that followed them would shriek a sweep at every word.
        // Until a leap is sure the throat is quiet (breath and the
        // consonant go on), then it starts on the new note.
        // A reading an octave or two off the note held is the tracker
        // slipping (a low voice's strong second harmonic): it is folded
        // back onto the note, with no gap, unless it lasts a fifth of a
        // second.
        if (m_inVoiced && m_inSure && m_sung > 0) {
            const float semis = 12.0f * std::log2(m_inHz / m_sung);
            const float octs = std::round(semis / 12.0f);
            if (std::fabs(semis) <= 5.0f) {
                m_sung = m_inHz;
                m_away = 0;
                m_slip = 0;
            } else if (octs != 0 && std::fabs(semis - 12.0f * octs) < 0.8f && std::fabs(octs) <= 2.0f &&
                       (m_slip += n) < int(0.2f * m_rate)) {
                m_sung = m_inHz * std::exp2(-octs);
                m_away = 0;
            } else if ((m_away += n) > int(0.03f * m_rate)) {
                m_sung = m_inHz;
                m_away = 0;
                m_slip = 0;
                // In a phrase's first moments a new note means the first
                // reading was the tracker's slip, not the singer's: the
                // octave is chosen again, from this one.
                if (m_phraseAge < int(0.15f * m_rate))
                    choose();
            }
        }
        float target = m_inVoiced && m_sung > 0 ? m_sung * std::exp2(float(m_oct)) : m_f0;
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
                innerStep(x, target, yl, yr);
                yl *= float(m_decim);
                yr *= float(m_decim);
            }
            const float mix = mix0 + (mix1 - mix0) * float(i + 1) / float(n);
            l[i] += m_up[0].step(yl) * mix;
            r[i] += m_up[1].step(yr) * mix;
        }
    }

private:
    // The octave that brings the phrase's note nearest a screamer's, and
    // how far up the tract is drawn with it: the larynx raised and a
    // shorter, more open mouth (a man's moved further, as his voice is).
    void choose() {
        m_oct = std::clamp(int(std::lround(std::log2(kCentreHz / m_sung))), -1, 2);
        m_alpha = std::clamp(1.08f + 0.06f * float(m_oct), 1.0f, 1.24f);
    }

    static constexpr float kCentreHz = 540.0f; // the reference's wail: 506–558 Hz
    static constexpr float kPre = 0.92f;       // pre-emphasis at the inner rate
    static constexpr float kResidLift = 1.6f;    // the high-passed residual back to about its power
    static constexpr float kDeemphNorm = 0.392f; // √(1 − kPre²): white noise through 1/(1 − kPre·z⁻¹) at unit power

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
        const float P = 700.0f + 2300.0f * loud;                       // Pa: 0.7 kPa soft to 3 kPa
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

        // Voicing follows the tracker; the pitch glides to the note in
        // 2.5 ms (no step when the tracker steps).
        m_voicing += ((m_inVoiced && m_away == 0 ? 1.0f : 0.0f) - m_voicing) * 0.006f;
        if (target > 0)
            m_f0 = m_f0 > 0 ? m_f0 * std::exp2(std::log2(target / m_f0) * 0.025f) : target;

        // The singer's residual, normalised: what the mouth was given.
        float e = xp;
        const float *kA = m_kA, *kA0 = m_kA0;
        {
            float f = xp, bPrev = xp;
            for (int i = 1; i <= kOrder; ++i) {
                const float k = kA0[i] + (kA[i] - kA0[i]) * t;
                const float bi = m_bA[i - 1];
                const float fi = f + k * bi;
                m_bA[i - 1] = bPrev;
                bPrev = k * f + bi;
                f = fi;
            }
            m_bA[kOrder] = bPrev;
            e = f;
        }
        const float eA = m_eA0 + (m_eA - m_eA0) * t;
        const float eS = m_eS0 + (m_eS - m_eS0) * t;
        // The singer's own sound keeps its own tilt: the residual is taken
        // back from the pre-emphasis before the mouth (the filter is
        // linear, so before is as good as after), at the same power.
        // Only its noise is let through (above 1.2 kHz): the consonants,
        // which are made in the mouth, not the throat. Its buzz would be
        // your own voice at your own pitch at the start of every vowel,
        // before the tracker has the note.
        // It is scaled by how much louder the moved mouth is than yours
        // (the ratio of their errors, which is there even out of silence),
        // so a consonant comes through at once, at its own level.
        const float ratio = clampf((eS + 1e-14f) / (eA + 1e-14f), 0.01f, 100.0f);
        m_deemph = e * std::sqrt(ratio) + kPre * m_deemph;
        const float resid = m_residHp.step(m_deemph * kDeemphNorm).high * kResidLift;

        // The new source, normalised by what a full cycle of it carries
        // (so the singer's level, not the model's, sets the loudness, and
        // the pressure only the timbre).
        const float pressureFlow = Throat::kAmax * std::sqrt(2.0f * P / Throat::kRho);
        const float norm = 1.0f / (pressureFlow * std::max(m_f0, 60.0f) * kSrcScale);
        float src = m_throat.step(m_f0, P, m_voicing, As0, flutter, 0.0035f, 0.06f, m_inner) * norm;
        for (int g = 0; g < m_gangN; ++g) {
            const float gHz = m_gangHz[g] * std::exp2(float(m_oct));
            src += m_gangThroat[g].step(gHz, P, m_voicing * (m_gangLvl[g] > 0.001f ? 1.0f : 0.0f), As0, flutter,
                                        0.005f, 0.07f, m_inner) /
                   (pressureFlow * std::max(gHz, 60.0f) * kSrcScale) * m_gangLvl[g];
        }
        const float v = m_voicing;
        const float exc = std::sqrt(eS) * v * src * kSrcGain + (1.0f - v) * resid;

        // The scream's mouth: an all-pole lattice on the moved poles.
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
        // The jaw: F1 onto the second harmonic. (No de-emphasis: the
        // mouth was found on the pre-emphasised voice, which is the tract
        // alone, and the new source is already what the lips radiate.)
        const float beltHz = clampf(2.0f * m_f0, 650.0f, 1300.0f);
        m_belt.set(beltHz, 3.5f, m_inner);
        y += kBeltGain * v * m_belt.k * m_belt.step(y).band;
        y = m_hp2.step(m_hp.step(y).high).high;
        // A guard, not a sound: a voice's mouth is broad, but a pure tone's
        // is one sharp pole, and a harmonic of the scream landing on it
        // would ring far louder than what was sung. Over 20 ms the scream
        // may be up to 30 dB over the singer's (pre-emphasised) level; past
        // that it is held there. (A sung or spoken voice measures at most
        // 28 dB over, a pure tone sweeping through 74.)
        m_powIn += (xp * xp - m_powIn) * 0.003f;
        m_powOut += (y * y - m_powOut) * 0.003f;
        const float room = 1000.0f * m_powIn + 1e-12f;
        const float g = m_powOut > room ? std::sqrt(room / m_powOut) : 1.0f;
        m_guard += (g - m_guard) * (g < m_guard ? 0.05f : 0.002f);
        y *= kLevel * m_guard;
        m_place.step(y, outL, outR);
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
        // The singer's own glottis out of it, as IAIF does (Alku 1992): a
        // first-order fit of the spectrum is the source's tilt, divided
        // out, so the mouth found below is the tract alone and a breathy
        // or a soft singer doesn't make the scream dull.
        float Pt[kN / 2 + 1];
        {
            float r0 = 0, r1 = 0;
            for (int j = 0; j <= kN / 2; ++j) {
                r0 += P[j] * m_cos[0][j];
                r1 += P[j] * m_cos[1][j];
            }
            const float a1 = r0 > 1e-20f ? clampf(-r1 / r0, -0.98f, 0.98f) : 0.0f;
            for (int j = 0; j <= kN / 2; ++j)
                Pt[j] = P[j] * (1.0f + a1 * a1 + 2.0f * a1 * m_cosW[j]);
        }
        // The moved mouth: every resonance up by α, P'(f) = P(f/α). Moved
        // in the spectrum, it stays a spectrum (nothing negative), so the
        // poles found from it are always stable.
        float Ps[kN / 2 + 1];
        const float inv = 1.0f / m_alpha;
        for (int j = 0; j <= kN / 2; ++j) {
            const float pos = float(j) * inv;
            const int i = std::min(int(pos), kN / 2 - 1);
            const float fr = pos - float(i);
            Ps[j] = Pt[i] + (Pt[i + 1] - Pt[i]) * fr;
        }
        // Both back to lags 0..16 (cosine sums), per sample (the window's
        // own energy is 3/8 of its length), bandwidths widened by 60 Hz (a
        // Gaussian lag window) and a floor 40 dB down, so a pure tone
        // can't make a pole on the circle.
        float ra[kOrder + 1], rs[kOrder + 1];
        for (int k = 0; k <= kOrder; ++k) {
            const float *c = m_cos[k];
            float sa = 0, ss = 0;
            for (int j = 0; j <= kN / 2; ++j) {
                sa += P[j] * c[j];
                ss += Ps[j] * c[j];
            }
            ra[k] = sa * m_lagWin[k];
            rs[k] = ss * m_lagWin[k];
        }
        ra[0] *= 1.0001f;
        rs[0] *= 1.0001f;
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
        // Out of silence the level starts where it is, not glided up from
        // nothing (the residual is divided by it: from nothing it would
        // be huge for a hop).
        if (m_eA0 < 1e-3f * m_eA) {
            m_eA0 = m_eA;
            m_eS0 = m_eS;
        }
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
        for (int j = 0; j <= kN / 2; ++j)
            m_cosW[j] = std::cos(kTwoPi * float(j) / float(kN));
        const float bw = kTwoPi * 60.0f / m_inner;
        for (int k = 0; k <= kOrder; ++k)
            m_lagWin[k] = std::exp(-0.5f * bw * bw * float(k * k));
    }

    // Calibration, measured on the test singer (docs/riot-scream.md):
    // what a full source cycle carries, the source against the residual,
    // the jaw's lift, and the level to sit as loud as the natural voice.
    static constexpr float kSrcScale = 0.9f;
    static constexpr float kSrcGain = 1.0f;
    static constexpr float kBeltGain = 1.5f;
    static constexpr float kLevel = 0.62f;
    static constexpr float kEnvRef = 0.12f;

    float m_rate = 48000, m_inner = 16000;
    int m_decim = 3, m_phase = 0;
    Butter8 m_down, m_up[2];
    float m_preX = 0, m_deemph = 0;
    float m_buf[kBufN] = {};
    float m_hann[kWin] = {}, m_twr[kN / 2] = {}, m_twi[kN / 2] = {}, m_cos[kOrder + 1][kN / 2 + 1] = {};
    float m_lagWin[kOrder + 1] = {}, m_cosW[kN / 2 + 1] = {};
    int m_rev[kN] = {};
    uint32_t m_w = 0;
    int m_hopAt = 0;
    float m_kA[kOrder + 1] = {}, m_kA0[kOrder + 1] = {}, m_kS[kOrder + 1] = {}, m_kS0[kOrder + 1] = {};
    float m_bA[kOrder + 1] = {}, m_bS[kOrder + 1] = {};
    float m_eA = 0, m_eA0 = 0, m_eS = 0, m_eS0 = 0;
    float m_alpha = 1.14f;
    Throat m_throat, m_gangThroat[kMaxGang];
    float m_gangHz[kMaxGang] = {}, m_gangLvl[kMaxGang] = {};
    int m_gangN = 0;
    Place3 m_place;
    Svf m_hp, m_hp2, m_belt, m_residHp;
    float m_powIn = 0, m_powOut = 0, m_guard = 1;
    float m_env = 0, m_voicing = 0, m_f0 = 0, m_strain = 0, m_burst = 0.5f, m_burstTarget = 0.5f;
    int m_oct = 0, m_unvoiced = 1 << 30;
    float m_inHz = 0, m_sung = 0;
    int m_away = 0, m_slip = 0, m_phraseAge = 0;
    bool m_inVoiced = false, m_inSure = false;
    Rng m_rng{0xB1C1u};
};

}
