#pragma once

// The electronic voices: an 808 kick that becomes a bass when held, a drum
// kit, and a subtractive synth (band-limited saws in unison through a
// resonant ladder filter with its own envelope) that plays bass, leads and
// chords. Everything is built from oscillators, noise and filters at audio
// rate; nothing is sampled.

#include "dsp.h"

namespace tf {

// PolyBLEP: the step of a saw or square smoothed over one sample, which
// removes nearly all of the aliasing a naive saw would fold back.
inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

struct SawOsc {
    float phase = 0;
    inline float step(float inc) {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        return 2.0f * phase - 1.0f - polyBlep(phase, inc);
    }
};

struct SquareOsc {
    float phase = 0;
    inline float step(float inc) {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        float v = phase < 0.5f ? 1.0f : -1.0f;
        v += polyBlep(phase, inc);
        float t2 = phase + 0.5f;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        v -= polyBlep(t2, inc);
        return v;
    }
};

// A four-pole transistor-ladder lowpass, zero-delay feedback form, with the
// input driven through a soft clipper so resonance growls rather than
// screams. Resonance 1 self-oscillates.
struct Ladder {
    float s[4] = {};
    float G = 0, g = 0, k = 0;
    void set(float hz, float res, float rate) {
        hz = clampf(hz, 20.0f, rate * 0.45f);
        g = std::tan(kPi * hz / rate);
        G = g / (1.0f + g);
        k = 4.0f * clampf(res, 0.0f, 1.0f) * 0.98f;
    }
    inline float step(float x) {
        const float G2 = G * G, G3 = G2 * G, G4 = G3 * G;
        const float inv = 1.0f / (1.0f + g);
        const float S = G3 * s[0] * inv + G2 * s[1] * inv + G * s[2] * inv + s[3] * inv;
        float y4 = (G4 * x + S) / (1.0f + k * G4);
        float u = fastTanh(x * (1.0f + 0.5f * k) - k * y4);
        for (int i = 0; i < 4; ++i) {
            const float v = (u - s[i]) * G;
            const float y = v + s[i];
            s[i] = y + v;
            u = y;
        }
        return u;
    }
    void reset() { s[0] = s[1] = s[2] = s[3] = 0; }
};

// Exponential-segment envelope: attack, decay to sustain, release.
struct Adsr {
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float v = 0, a = 0.001f, d = 0.2f, s = 0.7f, r = 0.2f;
    float rate = 48000;
    void gate(bool on) { stage = on ? Attack : (stage == Idle ? Idle : Release); }
    inline float step() {
        switch (stage) {
        case Attack:
            v += (1.2f - v) * (1.0f - std::exp(-1.0f / (std::max(a, 0.0005f) * rate)));
            if (v >= 1.0f) {
                v = 1.0f;
                stage = Decay;
            }
            break;
        case Decay:
            v += (s - v) * (1.0f - std::exp(-1.0f / (std::max(d, 0.001f) * rate * 0.3f)));
            if (std::fabs(v - s) < 1e-4f)
                stage = Sustain;
            break;
        case Sustain:
            v = s;
            break;
        case Release:
            v -= v * (1.0f - std::exp(-1.0f / (std::max(r, 0.001f) * rate * 0.3f)));
            if (v < 1e-5f) {
                v = 0;
                stage = Idle;
            }
            break;
        case Idle:
            break;
        }
        return v;
    }
};

// Sound settings the knobs give a synth voice, smoothed by the engine.
struct SynthParams {
    float cutoff = 0.6f;    // 0..1 → 60 Hz .. 16 kHz
    float res = 0.25f;
    float width = 0.35f;    // unison detune and spread
    float sub = 0.5f;       // sub-octave oscillator
    float envAmt = 0.45f;   // filter envelope depth
    float envDecay = 0.35f; // filter envelope decay
    float wobbleDepth = 0;  // tempo-synced filter LFO
    float wobblePhase = 0;  // where the LFO is, 0..1, from the engine's bar clock
    float glide = 0.04f;    // seconds
    float arpPhase = -1;    // < 0: chords sustain; else the arp step clock
    int arpStep = 0;
};

enum class Patch : uint8_t { Kick808, Bass, Lead, Chord, Arp };

// One finger's synth voice. A Kick808 is a sine with a falling pitch and a
// click: a tap is a kick, a hold is an 808 bass. A Bass is a saw plus a
// square sub through the ladder, punchy filter envelope, mono-centred. A
// Lead is a seven-saw unison spread across the stereo field. A Chord plays a
// voiced triad or seventh in five-saw unison, or arpeggiates it.
struct SynthVoice {
    static constexpr int kNotes = 4, kUnison = 7;

    bool active = false, releasing = false, latched = false, held = false, pinned = false;
    Patch patch = Patch::Lead;
    int finger = -1, role = 0, group = 0, degree = 0, transpose = 0;
    uint64_t born = 0;
    float x = 0.5f, y = 0.5f, vel = 0.8f, pressure = 0.6f;
    float midiTarget = 48;
    Smooth midi, press;
    float notes[kNotes] = {}; // semitone offsets of chord tones from midi
    int noteCount = 1;
    SawOsc saws[kNotes][kUnison];
    SquareOsc sub;
    float sinePhase = 0;
    float detune[kUnison], pan[kUnison];
    Ladder fL, fR;
    Adsr amp, filt;
    float click = 0, pitchEnv = 0;
    float level = 0, fade = 1, fadeRate = 0, latchDecay = 1;
    float swell = 1, swellTarget = 1;
    SynthParams own; // a latched voice keeps the sound it was latched with
    float midiOffset = 0;
    int lastArp = -1;
    Rng rng;

    void init(uint32_t seed) {
        rng = Rng(seed);
        // Unison detune spread evenly, the outer voices a little further out,
        // like the classic supersaw.
        static const float spread[kUnison] = {0.0f, -0.11f, 0.11f, -0.36f, 0.36f, -0.78f, 0.78f};
        for (int i = 0; i < kUnison; ++i) {
            detune[i] = spread[i];
            pan[i] = i == 0 ? 0.0f : (i & 1 ? -1.0f : 1.0f) * (0.35f + 0.2f * float(i / 2));
        }
    }

    void start(Patch p, int fingerId, float midiPitch, const float *chord, int chordCount, float velocity,
               float px, float py, float rate, uint64_t now) {
        patch = p;
        active = true;
        releasing = latched = held = pinned = false;
        finger = fingerId;
        born = now;
        x = px;
        y = py;
        vel = clampf(velocity, 0.05f, 1.0f);
        pressure = vel;
        midiTarget = midiPitch;
        midi.setTime(0.03f, rate / 64.0f);
        midi.reset(midiPitch);
        press.setTime(0.03f, rate / 64.0f);
        press.reset(vel);
        noteCount = std::clamp(chordCount, 1, kNotes);
        for (int i = 0; i < kNotes; ++i)
            notes[i] = i < noteCount && chord ? chord[i] : 0.0f;
        for (auto &row : saws)
            for (auto &o : row)
                o.phase = rng.unit();
        sub.phase = 0;
        sinePhase = 0;
        fL.reset();
        fR.reset();
        amp.rate = filt.rate = rate;
        switch (p) {
        case Patch::Kick808:
            amp.a = 0.0005f;
            amp.d = 0.45f;
            amp.s = 0.0f;
            amp.r = 0.12f;
            break;
        case Patch::Bass:
            amp.a = 0.002f;
            amp.d = 0.3f;
            amp.s = 0.85f;
            amp.r = 0.08f;
            break;
        case Patch::Lead:
            amp.a = 0.004f;
            amp.d = 0.35f;
            amp.s = 0.75f;
            amp.r = 0.15f;
            break;
        case Patch::Arp:
            amp.a = 0.002f;
            amp.d = 0.18f;
            amp.s = 0.45f;
            amp.r = 0.15f;
            break;
        case Patch::Chord:
            amp.a = 0.012f;
            amp.d = 0.6f;
            amp.s = 0.7f;
            amp.r = 0.3f;
            break;
        }
        filt.a = 0.001f;
        filt.d = 0.25f;
        filt.s = 0.0f;
        filt.r = 0.2f;
        amp.v = filt.v = 0;
        amp.gate(true);
        filt.gate(true);
        click = p == Patch::Kick808 ? 1.0f : 0.0f;
        pitchEnv = p == Patch::Kick808 ? 1.0f : 0.0f;
        level = 0;
        fade = 1;
        fadeRate = 0;
        latchDecay = 1;
        swell = swellTarget = 1;
        transpose = 0;
        midiOffset = 0;
        lastArp = -1;
    }

    bool chordish() const { return patch == Patch::Chord || patch == Patch::Arp; }

    void noteOff() {
        releasing = true;
        amp.gate(false);
        filt.gate(false);
    }

    // Adds n samples to l and r; duck is the sidechain gain for this block.
    void render(float *l, float *r, int n, const SynthParams &p, float rate, float duck) {
        const float dt = float(n) / rate;
        midi.setTime(std::max(0.002f, p.glide), rate / 64.0f);
        const float m = midi.step(midiTarget);
        const float pr = press.step(pressure);
        if (fadeRate > 0)
            fade = std::max(0.0f, fade - fadeRate * dt);
        if (latched && !pinned && !held)
            latchDecay *= std::exp(-dt / 180.0f);
        swell += (swellTarget - swell) * (1.0f - std::exp(-dt / 0.1f));
        if (fade <= 0 || latchDecay < 0.01f || (amp.stage == Adsr::Idle && (releasing || patch == Patch::Kick808))) {
            active = false;
            return;
        }

        // Envelope shapes follow the knobs: a short decay is a pluck, a long
        // one a pad.
        filt.d = 0.04f + 1.2f * p.envDecay * p.envDecay;
        if (patch == Patch::Lead)
            amp.d = 0.08f + 1.5f * p.envDecay;
        if (patch == Patch::Kick808)
            amp.d = held || latched || !releasing ? (releasing ? 0.25f : 2.2f) : 0.25f;

        const float baseHz = midiToHz(m);
        // Cutoff: exponential from 60 Hz, opened by the envelope, by pressure
        // and by the wobble LFO.
        const float wob = p.wobbleDepth > 0.001f ? p.wobbleDepth * (0.5f - 0.5f * std::cos(kTwoPi * p.wobblePhase)) : 0.0f;
        const float cutBase = p.cutoff + 0.12f * (pr - 0.5f) - 0.6f * wob * (patch == Patch::Bass ? 1.0f : 0.6f);
        const float res = p.res;
        const float widthCents = (patch == Patch::Lead ? 28.0f : chordish() ? 18.0f : 10.0f) * (0.2f + p.width);
        const float gainBase = vel * swell * fade * latchDecay * duck;

        // Arpeggio: one chord tone at a time, retriggered on each step.
        int arpNote = -1;
        if (chordish() && p.arpPhase >= 0) {
            if (p.arpStep != lastArp) {
                lastArp = p.arpStep;
                filt.v = 0;
                filt.stage = Adsr::Attack;
                amp.v = std::max(amp.v * 0.3f, 0.0f);
                amp.stage = releasing ? Adsr::Release : Adsr::Attack;
            }
            const int span = noteCount * 2;
            int k = p.arpStep % (span * 2 - 2 > 0 ? span * 2 - 2 : 1);
            if (k >= span)
                k = span * 2 - 2 - k; // up, then down
            arpNote = k;
        }

        float out[64][2];
        for (int i = 0; i < n; ++i) {
            const float a = amp.step();
            const float fe = filt.step();
            float sl = 0, sr = 0;
            if (patch == Patch::Kick808) {
                // Pitch falls from three and a half times the note to the
                // note in a few tens of milliseconds: that drop is the punch.
                pitchEnv *= std::exp(-1.0f / (0.028f * rate));
                const float hz = baseHz * (1.0f + 2.5f * pitchEnv);
                sinePhase += hz / rate;
                if (sinePhase >= 1.0f)
                    sinePhase -= 1.0f;
                float s = std::sin(kTwoPi * sinePhase);
                s = fastTanh(s * (1.4f + 2.0f * p.res)) * 0.85f; // a little drive rounds it into an 808
                const float c = click * rng.bipolar() * 0.6f;
                click *= std::exp(-1.0f / (0.0025f * rate));
                sl = sr = (s * a + c) * gainBase * 0.9f;
            } else {
                const int notesNow = arpNote >= 0 ? 1 : noteCount;
                const int unison = patch == Patch::Lead ? kUnison : chordish() ? 5 : 2;
                const float uniGain = 1.0f / std::sqrt(float(unison * notesNow));
                for (int k = 0; k < notesNow; ++k) {
                    float off;
                    if (arpNote >= 0)
                        off = notes[arpNote % noteCount] + 12.0f * float(arpNote / noteCount);
                    else
                        off = notes[k];
                    const float hz = baseHz * std::exp2(off / 12.0f);
                    for (int u = 0; u < unison; ++u) {
                        const float d = std::exp2(detune[u] * widthCents / 1200.0f);
                        const float inc = std::min(hz * d / rate, 0.45f);
                        const float v = saws[k][u].step(inc) * uniGain;
                        const float pn = clampf(pan[u] * (0.45f + 0.75f * p.width), -1.0f, 1.0f);
                        sl += v * (1.0f - pn) * 0.5f;
                        sr += v * (1.0f + pn) * 0.5f;
                    }
                }
                // Sub an octave down, square, dead centre.
                const float subHz = baseHz * 0.5f * (arpNote >= 0 ? 1.0f : 1.0f);
                const float sb = sub.step(std::min(subHz / rate, 0.45f)) * p.sub * (patch == Patch::Bass ? 0.55f : 0.25f);
                sl += sb;
                sr += sb;
                // The filter: cutoff in octaves above 60 Hz.
                const float octaves = clampf(cutBase + p.envAmt * fe * 0.75f, 0.0f, 1.15f) * 8.1f;
                const float fc = 60.0f * std::exp2(octaves);
                if ((i & 15) == 0) {
                    fL.set(fc, res, rate);
                    fR.set(fc, res, rate);
                }
                sl = fL.step(sl) * a * gainBase;
                sr = fR.step(sr) * a * gainBase;
                const float g = patch == Patch::Bass ? 0.55f : patch == Patch::Lead ? 0.42f : 0.38f;
                sl *= g;
                sr *= g;
            }
            out[i][0] = sl;
            out[i][1] = sr;
        }
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            l[i] += out[i][0];
            r[i] += out[i][1];
            peak = std::max(peak, std::fabs(out[i][0]) + std::fabs(out[i][1]));
        }
        level = level * 0.9f + 0.1f * peak;
    }
};

enum class Drum : uint8_t { Kick, Snare, Clap, HatClosed, HatOpen, Perc, Count };

inline const char *drumName(Drum d) {
    static const char *names[] = {"kick", "snare", "clap", "hat", "open", "perc"};
    return names[int(d)];
}

// One drum hit, synthesised: snare = two tuned sines and a band of noise;
// clap = three quick noise bursts and a tail; hats and ride = six detuned
// square waves (the classic metallic cluster) through band and high passes;
// perc = a pitched tom with a rimshot click.
struct DrumVoice {
    bool active = false;
    Drum kind = Drum::Snare;
    int delay = 0;
    float vel = 0.8f, tune = 1.0f, pan = 0, gain = 0.5f, wet = 1.0f;
    float t = 0, phaseA = 0, phaseB = 0;
    float metal[6] = {};
    float envTone = 0, envNoise = 0, decayTone = 0, decayNoise = 0;
    float pitchEnv = 0, pitchDecay = 0;
    float level = 0;
    Svf bp, hp, bp2;
    Rng rng;

    void start(Drum d, float velocity, float tuning, float panning, float g, int startDelay, float rate) {
        kind = d;
        active = true;
        delay = startDelay;
        vel = clampf(velocity, 0.05f, 1.0f);
        tune = tuning;
        pan = panning;
        gain = g;
        wet = 1.0f;
        t = 0;
        phaseA = phaseB = 0;
        for (float &m : metal)
            m = rng.unit();
        bp.reset();
        hp.reset();
        bp2.reset();
        envTone = envNoise = 1.0f;
        auto decay = [&](float s) { return std::exp(-1.0f / (s * rate)); };
        switch (d) {
        case Drum::Snare:
            decayTone = decay(0.07f);
            decayNoise = decay(0.16f + 0.08f * vel);
            bp.set(4200.0f * tune, 0.6f, rate);
            hp.set(1000.0f, 0.7f, rate);
            break;
        case Drum::Clap:
            decayTone = 0;
            decayNoise = decay(0.22f);
            bp.set(1150.0f * tune, 1.3f, rate);
            hp.set(600.0f, 0.7f, rate);
            break;
        case Drum::HatClosed:
            decayNoise = decay(0.035f + 0.04f * vel);
            bp.set(9500.0f, 0.9f, rate);
            hp.set(7000.0f * std::min(tune, 1.2f), 0.7f, rate);
            break;
        case Drum::HatOpen:
            decayNoise = decay(0.38f);
            bp.set(9000.0f, 0.8f, rate);
            hp.set(6500.0f * std::min(tune, 1.2f), 0.7f, rate);
            break;
        case Drum::Perc:
            decayTone = decay(0.18f);
            decayNoise = decay(0.012f);
            bp.set(1700.0f, 1.5f, rate);
            hp.set(800.0f, 0.7f, rate);
            break;
        case Drum::Kick:
            // tune is the kick's own pitch in Hz, set from the key.
            decayTone = decay(0.075f + 0.06f * vel); // about −40 dB by half a second
            decayNoise = decay(0.0018f);
            pitchEnv = 1.0f;
            pitchDecay = decay(0.034f);
            hp.set(3000.0f, 0.7f, rate);
            break;
        case Drum::Count:
            break;
        }
    }

    // Closed hats choke an open one, as on the real machine.
    void choke(float rate) { decayNoise = std::exp(-1.0f / (0.015f * rate)); }

    void render(float *l, float *r, int n, float rate, float *sendL = nullptr, float *sendR = nullptr) {
        int from = 0;
        if (delay > 0) {
            if (delay >= n) {
                delay -= n;
                return;
            }
            from = delay;
            delay = 0;
        }
        static const float metalHz[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        const float gl = std::sqrt(0.5f * (1.0f - pan)), gr = std::sqrt(0.5f * (1.0f + pan));
        float peak = 0;
        for (int i = from; i < n; ++i) {
            float s = 0;
            const float noise = rng.bipolar();
            switch (kind) {
            case Drum::Snare: {
                const float f1 = 185.0f * tune * (1.0f + 0.4f * envTone * envTone);
                phaseA += f1 / rate;
                phaseB += f1 * 1.78f / rate;
                phaseA -= std::floor(phaseA);
                phaseB -= std::floor(phaseB);
                const float tone = (std::sin(kTwoPi * phaseA) + 0.6f * std::sin(kTwoPi * phaseB)) * envTone;
                const float nz = (bp.step(noise).band * 1.2f + hp.step(noise).high * 0.6f) * envNoise;
                s = 0.55f * tone + 0.9f * nz;
                envTone *= decayTone;
                envNoise *= decayNoise;
                break;
            }
            case Drum::Clap: {
                // Three bursts ten milliseconds apart, then the room.
                const float ms = t * 1000.0f / rate;
                float burst = 0;
                for (int b = 0; b < 3; ++b) {
                    const float since = ms - 10.0f * float(b);
                    if (since >= 0 && since < 8)
                        burst = std::max(burst, std::exp(-since / 2.2f));
                }
                const float env = std::max(burst, ms > 22 ? envNoise : 0.0f);
                if (ms > 22)
                    envNoise *= decayNoise;
                s = bp.step(noise).band * 2.4f * env + hp.step(noise).high * 0.3f * env;
                break;
            }
            case Drum::HatClosed:
            case Drum::HatOpen: {
                // Mostly bright noise with the metallic cluster underneath:
                // crisp rather than pitched.
                float m = 0;
                for (int k = 0; k < 6; ++k) {
                    metal[k] += metalHz[k] * 1.7f * tune / rate;
                    metal[k] -= std::floor(metal[k]);
                    m += metal[k] < 0.5f ? 1.0f : -1.0f;
                }
                m = m / 6.0f * 0.35f + noise * 0.8f;
                const float v = hp.step(bp.step(m).band + 0.5f * m).high;
                s = v * envNoise * (kind == Drum::HatClosed ? 1.8f : 1.3f);
                envNoise *= decayNoise;
                break;
            }
            case Drum::Kick: {
                // A sine that falls about four to one onto its note in a few
                // tens of milliseconds, a two-millisecond click on top, and
                // a little drive to round it.
                const float f = tune * (1.0f + 3.0f * pitchEnv);
                pitchEnv *= pitchDecay;
                phaseA += f / rate;
                phaseA -= std::floor(phaseA);
                const float body = fastTanh(1.6f * std::sin(kTwoPi * phaseA)) * envTone;
                const float clk = hp.step(noise).high * envNoise * 0.5f;
                s = body + clk;
                envTone *= decayTone;
                envNoise *= decayNoise;
                break;
            }
            case Drum::Perc: {
                const float f = 150.0f * tune * (1.0f + 0.8f * envTone * envTone);
                phaseA += f / rate;
                phaseA -= std::floor(phaseA);
                s = std::sin(kTwoPi * phaseA) * envTone + bp.step(noise).band * envNoise * 1.5f;
                envTone *= decayTone;
                envNoise *= decayNoise;
                break;
            }
            case Drum::Count:
                break;
            }
            s = (kind == Drum::Kick ? s : fastTanh(s * (0.8f + vel))) * gain * vel;
            l[i] += s * gl;
            r[i] += s * gr;
            if (sendL) {
                sendL[i] += s * gl * wet;
                sendR[i] += s * gr * wet;
            }
            peak = std::max(peak, std::fabs(s));
            ++t;
        }
        level = peak;
        if (envNoise < 1e-4f && (envTone < 1e-4f || decayTone == 0) && t > rate * 0.03f)
            active = false;
    }
};

}
