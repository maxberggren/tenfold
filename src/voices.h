#pragma once

// The two kinds of sound a finger makes: the drone it blooms into when held,
// built partial by partial, and the struck body it sounds when it lands.

#include "dsp.h"

namespace tf {

// Knob values the engine hands every drone each block, already smoothed.
struct ToneParams {
    float bright = 0.45f;  // spectral tilt
    float hollow = 0.5f;   // < 0.5 drops even partials (hollow), > 0.5 odd ones (organ-like)
    float width = 0.3f;    // stereo spread of partials and their detune
    float shadow = 0.2f;   // sub-octave and fifth
    float voice = 0.0f;    // breath and formants: from pure tone to a choir
    float vowel = 0.5f;    // u o a e i
    float stretch = 0.0f;  // inharmonicity: string → bell → metal
    float life = 0.3f;     // slow random motion of every partial
};

// Formants for u, o, a, e, i (F1–F3 in Hz), a dark-to-bright walk so the
// twist sweeps the vowel without jumps.
inline void vowelFormants(float v, float f[3]) {
    static const float table[5][3] = {
        {300, 870, 2240}, {570, 840, 2410}, {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}};
    const float p = clampf(v, 0.0f, 1.0f) * 4.0f;
    const int i = std::min(3, int(p));
    const float t = p - float(i);
    for (int k = 0; k < 3; ++k)
        f[k] = lerpf(table[i][k], table[i + 1][k], t);
}

// A held finger's voice: 64 partials, each a pair of quadrature oscillators
// (one per ear) rotated by a complex multiply per sample. Every partial has
// its own detune, pan, slow drift and level, recomputed per block from the
// knobs, so the timbre is shaped directly in the spectrum: brightness is a
// tilt, the vowel is an envelope across the partials, inharmonicity bends
// their spacing. Nothing is filtered after the fact, so nothing rings or
// aliases; partials past 17 kHz simply fade out.
struct DroneVoice {
    static constexpr int K = 96;

    bool active = false, frozen = false, releasing = false;
    int finger = -1, role = 0, group = 0;
    // A frozen voice keeps the timbre it was frozen with, its scale degree
    // (so it follows key changes and can be transposed), and how far its
    // pitch sat from that degree (a glide frozen mid-way stays mid-way).
    ToneParams own;
    int degree = 0, transpose = 0;
    float midiOffset = 0;
    bool pinned = false, held = false;
    Smooth swell;
    float swellTarget = 1;
    uint64_t born = 0;
    float x = 0, y = 0;
    float midiTarget = 48, pressure = 0, size = 0;
    Smooth midi, press, sizeS;
    float env = 0, fade = 1, frozenDecay = 1, fadeRate = 0;
    float level = 0;

    alignas(16) float reL[K], imL[K], cL[K], sL[K], aL[K], daL[K];
    alignas(16) float reR[K], imR[K], cR[K], sR[K], aR[K], daR[K];
    float harm[K], logHarm[K], detune[K], panSeed[K], driftF[K], driftA[K];
    // Sympathetic ring: how much each partial has been set ringing by a
    // strike whose modes sit on it, decaying on its own.
    float freq[K], sym[K];
    // Faster shimmer per partial, opened up by leaning into the pad.
    float flick[K];
    float age = 0, breathPhase = 0;
    float driftPitch = 0;

    Delay comb;
    OnePoleLp combLp;
    Svf f1, f2;
    Rng rng;

    void init(float rate, uint32_t seed) {
        comb.init(size_t(rate / 14.0f));
        rng = Rng(seed);
        int k = 0;
        harm[k++] = 0.5f; // sub-octave
        harm[k++] = 1.0f;
        harm[k++] = 1.5f; // fifth
        for (int h = 2; k < K; ++h)
            harm[k++] = float(h);
        for (int i = 0; i < K; ++i) {
            logHarm[i] = std::log(harm[i]);
            detune[i] = rng.bipolar();
            panSeed[i] = rng.bipolar();
        }
    }

    void start(int fingerId, int r, float midiPitch, float p, float s, float px, float py, float rate,
               uint64_t now) {
        active = true;
        frozen = releasing = false;
        finger = fingerId;
        role = r;
        born = now;
        x = px;
        y = py;
        midiTarget = midiPitch;
        midi.setTime(0.045f, rate / 64.0f);
        midi.reset(midiPitch);
        press.setTime(0.03f, rate / 64.0f);
        press.reset(0);
        sizeS.setTime(0.08f, rate / 64.0f);
        sizeS.reset(s);
        pressure = p;
        size = s;
        env = 0;
        fade = 1;
        fadeRate = 0;
        frozenDecay = 1;
        driftPitch = 0;
        pinned = held = false;
        swell.setTime(0.12f, rate / 64.0f);
        swell.reset(1);
        swellTarget = 1;
        for (int i = 0; i < K; ++i) {
            // Random starting phases: ninety-six partials starting together
            // would put a click at the front of every bloom. Both ears get
            // the same phase, so with no width the voice is truly mono and
            // folds down without beating.
            const float ph = kTwoPi * rng.unit();
            reL[i] = reR[i] = std::cos(ph);
            imL[i] = imR[i] = std::sin(ph);
            aL[i] = aR[i] = daL[i] = daR[i] = 0;
            driftF[i] = rng.bipolar();
            driftA[i] = rng.bipolar();
            freq[i] = 0;
            sym[i] = 0;
            flick[i] = 0;
        }
        age = 0;
        breathPhase = rng.unit();
        comb.clear();
        f1.reset();
        f2.reset();
    }

    // Adds n samples into l and r.
    void render(float *l, float *r, int n, const ToneParams &tp, float rate) {
        const float m = midi.step(midiTarget);
        const float p = press.step(pressure);
        const float sz = sizeS.step(size);

        // Envelope: a slow bloom in, a long release, a very slow decay once
        // frozen, and a fade when dissolved.
        const float dt = float(n) / rate;
        if (releasing)
            env -= env * (1.0f - std::exp(-dt / 0.9f));
        else
            env += (1.0f - env) * (1.0f - std::exp(-dt / 0.32f));
        if (frozen && !pinned && !held)
            frozenDecay *= std::exp(-dt / 220.0f);
        if (fadeRate > 0)
            fade = std::max(0.0f, fade - fadeRate * dt);
        const float pressureGain = frozen ? 0.75f : 0.22f + 0.78f * std::sqrt(clampf(p, 0, 1));
        const float gain = env * fade * frozenDecay * pressureGain * swell.step(swellTarget) * 0.16f;
        level = gain;
        if ((releasing && env < 1e-4f) || fade <= 0.0f || frozenDecay < 0.01f) {
            active = false;
            return;
        }

        // Slow life: every partial wanders in pitch and level on its own,
        // and a quicker flicker rides on the upper ones as you lean in.
        const float life = tp.life;
        const float walk = std::sqrt(dt) * 0.9f;
        const float quick = std::sqrt(dt) * 3.5f;
        for (int i = 0; i < K; ++i) {
            driftF[i] = clampf(driftF[i] * (1.0f - dt * 0.3f) + rng.bipolar() * walk, -1.0f, 1.0f);
            driftA[i] = clampf(driftA[i] * (1.0f - dt * 0.5f) + rng.bipolar() * walk, -1.0f, 1.0f);
            flick[i] = clampf(flick[i] * (1.0f - dt * 6.0f) + rng.bipolar() * quick, -1.0f, 1.0f);
        }
        age += dt;
        // A slow breath of the whole spectrum, a few seconds long.
        breathPhase += dt * (0.09f + 0.05f * life);
        if (breathPhase > 1.0f)
            breathPhase -= 1.0f;
        driftPitch = clampf(driftPitch * (1.0f - dt * 0.2f) + rng.bipolar() * walk * 0.15f, -1.0f, 1.0f);

        const float f0 = midiToHz(m + driftPitch * life * 0.12f);
        // The bloom: a new voice starts dark and opens over its first few
        // seconds, its partials arriving one after another from the bottom
        // up. Leaning in opens it further; the breath keeps it moving.
        const float bloom = 0.45f * std::exp(-age / 0.9f) - 0.12f * (1.0f - std::exp(-age / 2.5f));
        const float breath = 0.06f * (0.4f + life) * std::sin(kTwoPi * breathPhase);
        const float lean = 0.3f * p * p;
        // A flat finger (bigger than a fingertip) is warmer and rounder.
        const float flat = clampf(sz - 1.0f, 0.0f, 1.5f);
        const float alpha = lerpf(2.3f, 0.35f, clampf(tp.bright + lean - 0.2f * flat + breath, 0.0f, 1.0f)) + bloom;
        const float evenGain = clampf(2.0f * tp.hollow, 0.0f, 1.0f);
        const float oddGain = clampf(2.0f * (1.0f - tp.hollow), 0.0f, 1.0f);
        const float B = tp.stretch * tp.stretch * 0.004f;
        const float widthRatio = tp.width * 0.0105f;
        float form[3];
        vowelFormants(clampf(tp.vowel + 0.12f * life * std::sin(kTwoPi * breathPhase * 0.7f + 1.3f), 0.0f, 1.0f), form);
        const float formantAmt = clampf(tp.voice * 1.3f, 0.0f, 1.0f);
        const float voicePan = clampf((x - 0.5f) * 0.9f, -0.6f, 0.6f);

        float amps[K];
        float energy = 0;
        for (int i = 0; i < K; ++i) {
            const float h = harm[i];
            float a;
            if (h == 0.5f)
                a = 0.55f * tp.shadow + 0.3f * clampf(sz - 1.0f, 0.0f, 1.5f);
            else if (h == 1.5f)
                a = 0.3f * tp.shadow;
            else {
                a = std::exp(-alpha * logHarm[i]);
                const int hi = int(h);
                if (hi > 1)
                    a *= (hi & 1) ? oddGain : evenGain;
            }
            const float stretchMul = h > 1.0f ? std::sqrt(1.0f + B * h * h) : 1.0f;
            const float fk = f0 * h * stretchMul * (1.0f + life * 0.0035f * driftF[i]);
            freq[i] = (fk > 17000.0f || fk < 22.0f) ? 0.0f : fk;
            if (fk > 17000.0f || fk < 22.0f)
                a = 0;
            else if (fk > 12000.0f)
                a *= (17000.0f - fk) / 5000.0f;
            if (formantAmt > 0.001f && a > 0) {
                float env3 = 0.035f;
                env3 += std::exp(-sq((fk - form[0]) / (90.0f + 0.07f * form[0])));
                env3 += 0.6f * std::exp(-sq((fk - form[1]) / (110.0f + 0.07f * form[1])));
                env3 += 0.3f * std::exp(-sq((fk - form[2]) / (150.0f + 0.07f * form[2])));
                a *= lerpf(1.0f, env3 * 1.6f, formantAmt);
            }
            a *= 1.0f + life * 0.45f * driftA[i];
            if (h > 4.0f)
                a *= 1.0f + (0.15f + 0.6f * p) * life * flick[i];
            if (!frozen) {
                const float arrive = 0.035f * std::pow(h, 0.9f);
                const float u = clampf((age - arrive) / 0.45f, 0.0f, 1.0f);
                a *= u * u * (3.0f - 2.0f * u);
            }
            amps[i] = a;
            energy += a * a;
            // Each ear gets a slightly different frequency, so partials beat
            // slowly against each other across the stereo field. The sub,
            // the fundamental and the fifth stay put in the middle: that is
            // the centre the width spreads out from.
            const float spread = lowRamp(i);
            const float wL = kTwoPi * fk * (1.0f + widthRatio * detune[i] * spread) / rate;
            const float wR = kTwoPi * fk * (1.0f - widthRatio * detune[i] * spread) / rate;
            cL[i] = std::cos(wL);
            sL[i] = std::sin(wL);
            cR[i] = std::cos(wR);
            sR[i] = std::sin(wR);
        }
        // Constant loudness while the timbre moves: a darker tilt is not a
        // quieter note.
        const float norm = energy > 1e-9f ? gain / std::sqrt(energy) : 0.0f;
        const float invN = 1.0f / float(n);
        // A struck partial rings on top of the voice's own level, so a tap
        // can wake a quiet frozen chord.
        const float symGain = 0.06f * env * fade * std::sqrt(frozenDecay);
        const float symDecay = std::exp(-dt / 2.2f);
        for (int i = 0; i < K; ++i) {
            const float pan = clampf(voicePan + panSeed[i] * tp.width * 0.85f * lowRamp(i), -1.0f, 1.0f);
            const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
            const float ring = freq[i] > 0 ? sym[i] * symGain : 0.0f;
            sym[i] *= symDecay;
            daL[i] = ((amps[i] * norm + ring) * gl - aL[i]) * invN;
            daR[i] = ((amps[i] * norm + ring) * gr - aR[i]) * invN;
            // Keep each oscillator on the unit circle (float rounding would
            // otherwise grow or shrink it over minutes).
            const float gL2 = 1.5f - 0.5f * (reL[i] * reL[i] + imL[i] * imL[i]);
            const float gR2 = 1.5f - 0.5f * (reR[i] * reR[i] + imR[i] * imR[i]);
            reL[i] *= gL2;
            imL[i] *= gL2;
            reR[i] *= gR2;
            imR[i] *= gR2;
        }

        bank(l, n, reL, imL, cL, sL, aL, daL);
        bank(r, n, reR, imR, cR, sR, aR, daR);

        // Breath: noise through a comb tuned to the note, then through the
        // first two formants. At low amounts it is air around the tone; at
        // high amounts the tone sings.
        const float voiceAmt = clampf(tp.voice + 0.1f * p * p, 0.0f, 1.0f);
        if (voiceAmt > 0.002f) {
            const float breath = voiceAmt * voiceAmt * gain * 2.2f;
            const float period = clampf(rate / f0, 2.0f, float(comb.mask) - 4.0f);
            const float fb = 0.55f + 0.4f * tp.voice;
            combLp.set(2500.0f + 6000.0f * tp.bright, rate);
            f1.set(form[0], 4.0f, rate);
            f2.set(form[1], 5.0f, rate);
            const float gl = std::sqrt(0.5f * (1.0f - voicePan)), gr = std::sqrt(0.5f * (1.0f + voicePan));
            for (int i = 0; i < n; ++i) {
                const float noise = rng.bipolar();
                const float c = noise * 0.25f + fb * combLp.step(comb.read(period));
                comb.push(c);
                const float s = (c * (1.0f - formantAmt) + (f1.step(c).band + 0.7f * f2.step(c).band) * formantAmt * 1.8f) *
                                breath;
                l[i] += s * gl;
                r[i] += s * gr;
            }
        }
    }

    // A strike near this voice: every partial within about a third of a
    // semitone of one of its modes is set ringing, more the closer it is.
    void excite(const float *modeHz, const float *modeAmp, int modes, float vel) {
        const float f0 = freq[1];
        if (f0 <= 0)
            return;
        // Only the few partials that can sit near each mode are checked: the
        // nearest harmonics, plus the sub and the fifth.
        // Nothing rings below about 120 Hz: a kick that set the drone's
        // fundamental booming would only pile up sub. Each partial has a cap
        // and fills towards it more slowly the fuller it is, so a run of
        // kicks can't stack.
        constexpr float cap = 1.2f;
        auto ring = [&](int i, float hz, float amp) {
            if (i < 0 || i >= K || freq[i] <= 0)
                return;
            const float off = std::fabs(freq[i] / hz - 1.0f);
            if (off >= 0.02f)
                return;
            const float lowCut = clampf((freq[i] - 90.0f) / 130.0f, 0.0f, 1.0f);
            const float add = amp * (1.0f - off / 0.02f) * vel * 1.4f * lowCut * lowCut;
            sym[i] = std::min(cap, sym[i] + add * (1.0f - sym[i] / cap));
        };
        for (int m = 0; m < modes; ++m) {
            if (modeAmp[m] <= 0)
                continue;
            const float h = modeHz[m] / f0;
            if (h < 0.4f || h > float(K))
                continue;
            ring(0, modeHz[m], modeAmp[m]);
            ring(2, modeHz[m], modeAmp[m]);
            const int near = int(std::lround(h));
            for (int k = near - 1; k <= near + 1; ++k)
                ring(k <= 1 ? k : k + 1, modeHz[m], modeAmp[m]);
        }
    }

    static inline float sq(float v) { return v * v; }
    // 0 for the sub, fundamental and fifth, rising to 1 by the 8th partial.
    static inline float lowRamp(int i) { return clampf(float(i - 3) / 6.0f, 0.0f, 1.0f); }

    // The hot loop: rotate every partial one sample, sum, repeat. Laid out
    // so the compiler vectorises across partials.
    static void bank(float *out, int n, float *__restrict re, float *__restrict im, const float *__restrict c,
                     const float *__restrict s, float *__restrict a, const float *__restrict da) {
        for (int t = 0; t < n; ++t) {
            float sum = 0;
            for (int i = 0; i < K; ++i) {
                const float nr = re[i] * c[i] - im[i] * s[i];
                const float ni = re[i] * s[i] + im[i] * c[i];
                re[i] = nr;
                im[i] = ni;
                a[i] += da[i];
                sum += ni * a[i];
            }
            out[t] += sum;
        }
    }
};

// Materials a tap can strike, from the bottom of the pad to the top. Each is
// a set of modes (ratios to the note, levels, how long they ring) plus the
// noise of the impact itself. Between two materials the ratios glide
// geometrically, so between two rows the pad sounds like something that
// doesn't exist: half wooden bar, half bell.
struct Material {
    float ratio[16];
    float amp[16];
    float t60;        // seconds for the fundamental
    float t60Slope;   // how much faster higher modes die
    float pitchDepth; // a drum's pitch drop as the skin relaxes
    float pitchTau;
    float noise, noiseRatio, noiseQ, noiseDecay;
    float noiseHz;    // when set, the noise sits at a fixed band (snare wires)
    float hardness;   // how much a soft hit loses its upper modes
    float octave;     // semitones from the pad's pitch
};

constexpr int kMaterials = 5;

inline const Material &material(int i) {
    static const Material m[kMaterials] = {
        // Skin: circular membrane modes, a pitch drop on the fundamental.
        {{1, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f, 3.60f, 3.65f, 4.06f, 4.15f, 4.23f, 4.60f, 4.83f, 5.13f},
         {1, .45f, .32f, .28f, .2f, .15f, .12f, .1f, .08f, .07f, .05f, .04f, .035f, .03f, .025f, .02f},
         0.55f, 0.9f, 1.4f, 0.032f, 0.35f, 5.0f, 0.9f, 0.012f, 0, 1.4f, -12},
        // Snare: a higher, tighter skin with wires buzzing against it.
        {{1, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f, 3.60f, 3.65f, 4.06f, 4.15f, 4.23f, 4.60f, 4.83f, 5.13f},
         {1, .6f, .45f, .4f, .3f, .25f, .2f, .17f, .14f, .12f, .1f, .08f, .07f, .06f, .05f, .04f},
         0.28f, 1.1f, 0.35f, 0.02f, 1.1f, 0, 0.7f, 0.11f, 4200.0f, 1.0f, 0},
        // Wood: a tuned bar, upper modes dying much faster than the note.
        {{1, 3.98f, 9.38f, 15.6f, 2.76f, 5.40f, 8.93f, 13.3f, 18.6f, 1.0f, 2.0f, 4.0f, 23.1f, 27.9f, 6.3f, 11.7f},
         {1, .7f, .42f, .22f, .14f, .1f, .08f, .06f, .04f, .0f, .08f, .07f, .03f, .02f, .05f, .04f},
         1.3f, 1.6f, 0.015f, 0.01f, 0.18f, 6.0f, 1.2f, 0.005f, 0, 1.6f, 0},
        // Glass: church-bell partials, the hum below the note, a long ring.
        {{0.5f, 1, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f, 6.796f, 8.215f, 9.67f, 11.2f, 12.8f, 14.4f},
         {.55f, 1, .7f, .5f, .6f, .35f, .3f, .25f, .2f, .15f, .1f, .08f, .06f, .05f, .04f, .03f},
         5.0f, 0.35f, 0.0f, 0.01f, 0.06f, 9.0f, 2.0f, 0.003f, 0, 1.0f, 12},
        // Air: a cluster of inharmonic metal and a burst of bright noise.
        {{1, 1.483f, 1.932f, 2.546f, 2.630f, 3.897f, 4.6f, 5.3f, 6.1f, 7.2f, 8.4f, 9.9f, 11.3f, 12.9f, 14.7f, 16.8f},
         {.5f, .5f, .45f, .45f, .4f, .4f, .35f, .3f, .3f, .25f, .2f, .2f, .18f, .16f, .14f, .12f},
         0.22f, 0.25f, 0.0f, 0.01f, 0.9f, 7.0f, 0.8f, 0.05f, 0, 0.8f, 24},
    };
    return m[std::clamp(i, 0, kMaterials - 1)];
}

// A struck body: sixteen modes, each a complex one-pole resonator (a
// decaying phasor), driven by a mallet: a half-sine pulse whose length is the
// contact time. A hard, small contact is a short pulse that rings every mode;
// a soft, broad one is a long pulse that only reaches the low ones, which is
// why soft hits are darker as well as quieter. Started at an exact sample
// inside the block, so loops land where they were tapped.
struct StrikeVoice {
    static constexpr int M = 16;
    bool active = false;
    int delay = 0;
    uint32_t age = 0, life = 0;
    float level = 0;
    float re[M], im[M], w0[M], rad[M], gL[M], gR[M], drive[M], hz[M];
    float pitchEnv = 0, pitchDecay = 0;
    float noiseAmp = 0, noiseDecay = 0;
    float clickAmp = 0, clickDecay = 0;
    float noisePanL = 0.7f, noisePanR = 0.7f;
    int pulseLen = 1, pulsePos = 0;
    float pulseNorm = 1;
    float fade = 1, fadeStep = 0; // a stolen voice fades instead of cutting
    float wet = 1;                // how much goes to the echo and the room
    Svf noiseF, clickF;
    Rng rng;

    // material 0..4 (skin, snare, wood, glass, air), velocity 0..1, pan −1..1,
    // contact size relative to a fingertip.
    void start(float midiNote, float mat, float vel, float pan, float gain, int startDelay, float rate,
               float contact = 1.0f) {
        const float mc = clampf(mat, 0.0f, float(kMaterials - 1));
        const int lo = std::min(kMaterials - 2, int(mc));
        const float t = mc - float(lo);
        const Material &A = material(lo), &B = material(lo + 1);
        const float octave = (t < 0.5f ? A.octave : B.octave);
        const float f0 = midiToHz(midiNote + octave);
        vel = clampf(vel, 0.0f, 1.0f);
        const float soft = 1.0f - vel;
        const float hard = lerpf(A.hardness, B.hardness, t);
        float total = 0;
        for (int i = 0; i < M; ++i) {
            const float ratio = std::exp(lerpf(std::log(std::max(A.ratio[i], 0.01f)), std::log(std::max(B.ratio[i], 0.01f)), t));
            const float amp = lerpf(A.amp[i], B.amp[i], t);
            const float f = f0 * ratio;
            const float t60base = std::exp(lerpf(std::log(A.t60), std::log(B.t60), t));
            const float slope = lerpf(A.t60Slope, B.t60Slope, t);
            // Higher modes and higher notes ring shorter, as in any real body;
            // a hard hit drives the upper modes harder, so they also last
            // longer above the noise.
            const float force = std::pow(vel, 1.2f); // close to linear: a medium tap sounds medium
            float t60 = t60base / (1.0f + slope * float(i) * ratio * 0.25f / (1.0f + 3.0f * force));
            t60 *= clampf(std::pow(220.0f / std::max(f, 20.0f), 0.3f), 0.25f, 2.5f);
            w0[i] = kTwoPi * f / rate;
            hz[i] = f;
            rad[i] = (f > rate * 0.45f || f < 18.0f) ? 0.0f : std::exp(-6.9078f / (t60 * rate));
            // Soft hits lose their upper modes; hard ones with a small contact
            // lift them, for the whole ring, not just the click.
            const float lift = 1.0f + 4.0f * force * clampf((ratio - 1.0f) / 3.0f, 0.0f, 1.0f) / clampf(contact, 0.8f, 2.0f);
            float a = amp * lift * std::pow(std::max(ratio, 0.5f), -soft * soft * hard);
            if (rad[i] == 0.0f)
                a = 0;
            re[i] = 0;
            im[i] = 0;
            drive[i] = a;
            total += a;
            const float mp = clampf(pan + ((i & 1) ? 0.22f : -0.22f) * (i > 0 ? 1.0f : 0.2f), -1.0f, 1.0f);
            gL[i] = std::sqrt(0.5f * (1.0f - mp));
            gR[i] = std::sqrt(0.5f * (1.0f + mp));
        }
        const float g = gain * std::pow(std::max(vel, 0.02f), 1.3f) / std::max(total, 0.5f);
        for (int i = 0; i < M; ++i)
            drive[i] *= g;
        // Contact time: 5 ms for a soft, flat finger down to a third of a
        // millisecond for a hard fingertip.
        // On a log scale, so every step of force is a step of brightness.
        const float contactMs = 4.0f * std::pow(0.33f / 4.0f, vel) * clampf(0.55f + 0.45f * contact, 0.6f, 2.2f);
        pulseLen = std::max(1, int(contactMs * 0.001f * rate));
        pulsePos = 0;
        pulseNorm = 0;
        for (int k = 0; k < pulseLen; ++k)
            pulseNorm += std::sin(kPi * (k + 0.5f) / pulseLen);
        pulseNorm = 1.0f / std::max(pulseNorm, 1e-3f);
        pitchEnv = lerpf(A.pitchDepth, B.pitchDepth, t) * (0.6f + 0.4f * vel);
        pitchDecay = std::exp(-1.0f / (lerpf(A.pitchTau, B.pitchTau, t) * rate));
        noiseAmp = lerpf(A.noise, B.noise, t) * g * (0.4f + 0.8f * vel) * 0.8f;
        noiseDecay = std::exp(-1.0f / (lerpf(A.noiseDecay, B.noiseDecay, t) * rate));
        noiseF.reset();
        const float hzA = A.noiseHz > 0 ? A.noiseHz : f0 * A.noiseRatio;
        const float hzB = B.noiseHz > 0 ? B.noiseHz : f0 * B.noiseRatio;
        noiseF.set(std::min(lerpf(hzA, hzB, t), rate * 0.42f), lerpf(A.noiseQ, B.noiseQ, t), rate);
        // The click of the contact itself, bright and short, only for hits
        // that mean it.
        clickAmp = g * vel * vel * 0.9f / clampf(contact, 0.7f, 2.0f);
        clickDecay = std::exp(-1.0f / (0.0012f * rate));
        clickF.reset();
        clickF.set(std::min(2200.0f + 3500.0f * vel, rate * 0.42f), 1.2f, rate);
        noisePanL = std::sqrt(0.5f * (1.0f - pan));
        noisePanR = std::sqrt(0.5f * (1.0f + pan));
        delay = startDelay;
        age = 0;
        life = uint32_t(rate * 12.0f);
        level = g;
        fade = 1;
        fadeStep = 0;
        wet = 1;
        active = true;
    }

    // The modes and their levels relative to the loudest, for sympathetic ringing.
    int modes(float *outHz, float *outAmp) const {
        float peak = 1e-9f;
        for (int i = 0; i < M; ++i)
            peak = std::max(peak, drive[i]);
        for (int i = 0; i < M; ++i) {
            outHz[i] = hz[i];
            outAmp[i] = rad[i] > 0 ? drive[i] / peak : 0.0f;
        }
        return M;
    }

    void render(float *l, float *r, int n, float *sendL = nullptr, float *sendR = nullptr) {
        int from = 0;
        if (delay > 0) {
            if (delay >= n) {
                delay -= n;
                return;
            }
            from = delay;
            delay = 0;
        }
        // Per-block coefficients; the drum's pitch drop is slow next to a block.
        float cr[M], ci[M];
        const float bend = 1.0f + pitchEnv;
        for (int i = 0; i < M; ++i) {
            const float w = w0[i] * (i == 0 ? bend : 1.0f + pitchEnv * 0.5f);
            cr[i] = rad[i] * std::cos(w);
            ci[i] = rad[i] * std::sin(w);
        }
        float energy = 0;
        for (int t = from; t < n; ++t) {
            float ex = 0;
            if (pulsePos < pulseLen) {
                ex = std::sin(kPi * (pulsePos + 0.5f) / pulseLen) * pulseNorm;
                ++pulsePos;
            }
            float sl = 0, sr = 0;
            for (int i = 0; i < M; ++i) {
                const float nr = re[i] * cr[i] - im[i] * ci[i] + ex * drive[i];
                const float ni = re[i] * ci[i] + im[i] * cr[i];
                re[i] = nr;
                im[i] = ni;
                sl += ni * gL[i];
                sr += ni * gR[i];
            }
            if (noiseAmp > 1e-6f) {
                const float nz = noiseF.step(rng.bipolar()).band * noiseAmp;
                noiseAmp *= noiseDecay;
                sl += nz * noisePanL;
                sr += nz * noisePanR;
            }
            if (clickAmp > 1e-6f) {
                const float c = clickF.step(rng.bipolar()).band * clickAmp * 1.6f;
                clickAmp *= clickDecay;
                sl += c * noisePanL;
                sr += c * noisePanR;
            }
            if (fadeStep > 0) {
                sl *= fade;
                sr *= fade;
                fade -= fadeStep;
                if (fade <= 0) {
                    active = false;
                    return;
                }
            }
            l[t] += sl;
            r[t] += sr;
            if (sendL) {
                sendL[t] += sl * wet;
                sendR[t] += sr * wet;
            }
        }
        for (int i = 0; i < M; ++i)
            energy += re[i] * re[i] + im[i] * im[i];
        pitchEnv *= std::pow(pitchDecay, float(n - from));
        age += uint32_t(n - from);
        level = std::sqrt(energy);
        if ((energy < 1e-11f && noiseAmp < 1e-6f && pulsePos >= pulseLen) || age > life)
            active = false;
    }
};

}
