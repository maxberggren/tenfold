// The Vocal page: the microphone, heard at once in the headphones, played
// with ten fingers, and looped by Space.
//
// The voice is always on: whatever you sing comes back through the chain
// within the audio server's own buffering (the studio mixes it after the
// master limiter, so not even its look-ahead is added). The fingers play
// the voice the way the other pages play their instruments:
//
//   the middle    keys in the song's key, low to high across the pad: each
//                 finger is your voice again at that note (the vowels stay
//                 yours), its height the throat (low a giant, high an elf),
//                 pressure its level, twist a detune that widens it
//   left edge     a resonant filter: slide for a wah
//   right edge    freeze (lower half: the last sound held as a cloud you
//                 can sing over) or stutter (upper half: the last slice
//                 repeated on the grid, faster higher up)
//   top edge      throws: echo on the left, a huge reverb on the right,
//                 for as long as you hold
//   bottom edge   lo-fi: a radio on the left, a bitcrusher on the right
//   the click     tape stop: the voice winds down while the pad is pressed
//
// ← → choose what happens to the voice itself (natural, hard-tuned, only
// the notes you hold, a choir on the Chords page's chord, a vocoder, a
// robot). Space loops the singing just done as audio: a NEW loop by
// default, Shift+Space layered onto the vocal loop playing (the other pages
// have it the other way round: a voice is a take, layering is a choice).

#include "vocal.h"

#include <QPainter>
#include <QPainterPath>
#include <Qt>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "console.h"
#include "fx.h"
#include "parts.h"

namespace tf {

namespace vocal {

namespace {

enum Preset { Natural, Tune, Prism, Choir, Vocoder, Robot, PresetCount };
const char *presetName(int p) {
    static const char *names[] = {"NATURAL", "HARD TUNE", "PRISM", "CHOIR", "VOCODER", "ROBOT"};
    return names[std::clamp(p, 0, PresetCount - 1)];
}
const char *presetLine(int p) {
    static const char *lines[] = {
        "your voice as it is; fingers add copies of it at their notes",
        "snapped to the key's notes, hard; fingers add tuned copies",
        "only the notes your fingers hold sing, in your voice",
        "copies of you sing the Chords page's chord around your note",
        "a synth chord speaks your words: the chord, or your fingers' notes",
        "one note, the chord's root, flat as a machine",
    };
    return lines[std::clamp(p, 0, PresetCount - 1)];
}

// Commands. Notes are On/Move/Off (id: a voice of the page's), the rest
// Params; nothing the fingers do is recorded (a vocal loop is its audio).
enum ParamId {
    PPreset = 1,
    PFormant, // i[1]: −6..6, the voice's own throat
    PMonitor, // i[1]: 1 heard, 0 not
    PMicGain, // f[0]: dB
    PFollow,  // i[1]: hard tune snaps to the chord
    PFilter,  // f[0] height 0..1, f[1] pressure, f[2] on
    PLofi,    // f[0] across 0..1, f[1] pressure, f[2] on
    PFreeze,  // f[0] height 0..1, f[1] pressure, f[2] semitones, f[3] on
    PEcho,    // f[0] across 0..1, f[1] pressure, f[2] on
    PReverb,  // f[0] across 0..1, f[1] pressure, f[2] on, f[3] shimmer
    PTape,    // f[0] on
    PStutter, // i[1]: the rate's index
};

constexpr int kLoopIdBase = 0x100000; // a loop's sound: kLoopIdBase + its take's handle
constexpr int kMaxVoices = 10;

// Strip edges, in pad coordinates.
constexpr float kLeft = 0.085f, kRight = 0.915f, kBottom = 0.12f, kTop = 0.86f;
// The keys' lower part shows the tape: what Space would take.
constexpr float kTapeTop = 0.32f;

enum Zone { Keys, Filter, Freeze, Echo, Reverb, Radio, Crush };
Zone zoneAt(float x, float y) {
    if (y >= kTop)
        return x < 0.5f ? Echo : Reverb;
    if (y <= kBottom)
        return x < 0.5f ? Radio : Crush;
    if (x <= kLeft)
        return Filter;
    if (x >= kRight)
        return Freeze;
    return Keys;
}

// The stutter's slices, in beats.
const double kStutterBeats[] = {0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
const char *kStutterNames[] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
constexpr int kStutterCount = 5;

// Published numbers (pc.view->data).
namespace pub {
enum {
    InPeak = 0,
    InRms,
    Hz,
    Clarity,
    Gate,
    Feedback,
    Monitor,
    LivePeak,
    Tape,
    Freeze,
    Loops,
    LeadMidi,
    Lag,
    HasInput,
    Stutter,
    Voices = 16, // kMaxVoices × (active, midi, level)
    Scope = 64,  // 64 × (min, max) of the microphone, the last 43 ms
    ScopeN = 64,
};
}

// The nearest note of the key, or of the chord when follow is on, to a
// MIDI pitch.
double snapMidi(const Harmony &h, double midi, bool chord) {
    const Tuning &t = h.tuning;
    double best = midi, bestD = 1e9;
    if (chord) {
        const int n = std::clamp(h.chordCount, 1, 4);
        for (int oct = -2; oct <= 6; ++oct)
            for (int k = 0; k < n; ++k) {
                const double m = h.midi(h.chordTones[k], oct);
                if (std::fabs(m - midi) < bestD) {
                    bestD = std::fabs(m - midi);
                    best = m;
                }
            }
        return best;
    }
    const int per = std::max(1, t.degreesPerOctave());
    const int oct = int(std::floor((midi - t.baseMidi()) / 12.0));
    for (int d = (oct - 1) * per; d <= (oct + 2) * per; ++d) {
        const double m = t.baseMidi() + t.degreeSemis(d);
        if (std::fabs(m - midi) < bestD) {
            bestD = std::fabs(m - midi);
            best = m;
        }
    }
    return best;
}

// A note the page sends: a scale degree, or (follow) a chord tone counted
// up from the chord's root, resolved now, so a held note follows the
// chord as it changes.
double noteMidi(const Harmony &h, int value, int mode) {
    if (mode == 1) {
        const int n = std::clamp(h.chordCount, 1, 4);
        const int oct = int(std::floor(double(value) / n));
        const int k = value - oct * n;
        return h.midi(h.chordTones[k], oct);
    }
    return h.midi(value, 0);
}

inline float midiOfHz(float hz) { return 69.0f + 12.0f * std::log2(std::max(1.0f, hz) / 440.0f); }

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

} // namespace

// --- the engine -------------------------------------------------------------------

class Engine : public PartEngine {
public:
    explicit Engine(std::shared_ptr<Tape> tape) : m_tape(std::move(tape)) {}

    void init(float rate) override {
        m_rate = rate;
        m_tape->rate = rate;
        m_hp.set(90.0f, 0.7f, rate);
        m_pitch.init(rate);
        m_echo.init(rate);
        m_reverb.init(rate);
        m_reverb.set(0.8f, 0.3f);
        for (auto &v : m_voices) {
            v.level.setTime(0.02f, rate);
            v.level.reset(0);
            v.ratio.setTime(0.035f, rate);
            v.ratio.reset(1);
        }
        for (auto &c : m_choir) {
            c.level.setTime(0.06f, rate);
            c.level.reset(0);
            c.ratio.setTime(0.04f, rate);
            c.ratio.reset(1);
        }
        m_leadRatio.setTime(0.008f, rate);
        m_leadRatio.reset(1);
        m_leadShift.setTime(0.02f, rate);
        m_leadDry.setTime(0.03f, rate);
        m_leadDry.reset(1);
        m_vocoderMix.setTime(0.03f, rate);
        m_monGain.setTime(0.01f, rate);
        m_monGain.reset(1);
        m_filtOn.setTime(0.06f, rate);
        m_filtCut.setTime(0.02f, rate);
        m_filtCut.reset(0.7f);
        m_filtRes.setTime(0.02f, rate);
        m_lofiOn.setTime(0.03f, rate);
        m_echoSend.setTime(0.015f, rate);
        m_revSend.setTime(0.03f, rate);
        m_freezeLvl.setTime(0.03f, rate);
        m_stutterOn.setTime(0.004f, rate);
        for (int k = 0; k < kBands; ++k) {
            const float hz = 140.0f * std::pow(7200.0f / 140.0f, float(k) / float(kBands - 1));
            m_bands[k].a.set(hz, 7.0f, rate);
            m_bands[k].s.set(hz, 7.0f, rate);
        }
        m_sib.set(5000.0f, 0.7f, rate);
        m_radioHp.set(500.0f, 0.8f, rate);
        m_radioLp.set(2600.0f, 0.9f, rate);
        m_radioHp2.set(500.0f, 0.8f, rate);
        m_radioLp2.set(2600.0f, 0.9f, rate);
        m_freezeBuf.assign(size_t(rate * 0.75f), 0.0f);
        m_recent.assign(size_t(1) << 16, 0.0f);
        m_tapeL.assign(size_t(1) << 17, 0.0f);
        m_tapeR.assign(size_t(1) << 17, 0.0f);
    }

    void command(const Command &c, AudioContext &ctx) override {
        if (c.flags & Command::Sampled) {
            if (c.type == Command::On)
                startLoop(c, ctx);
            return;
        }
        if (c.type == Command::Off) {
            for (auto &l : m_loops)
                if (l.id == c.id && !l.fading)
                    l.fading = true;
            for (auto &v : m_voices)
                if (v.id == c.id)
                    v.id = -1, v.on = false;
            return;
        }
        if (c.type == Command::On || c.type == Command::Move) {
            Voice *v = nullptr;
            for (auto &x : m_voices)
                if (x.id == c.id)
                    v = &x;
            if (!v && c.type == Command::On) {
                for (auto &x : m_voices)
                    if (x.id < 0 && x.level.y < 0.001f) {
                        v = &x;
                        break;
                    }
                if (!v)
                    for (auto &x : m_voices)
                        if (x.id < 0) {
                            v = &x;
                            break;
                        }
                if (v) {
                    v->id = c.id;
                    v->shifter.reset();
                }
            }
            if (!v)
                return;
            v->on = true;
            v->value = c.i[0];
            v->mode = c.i[1];
            v->gain = clampf(c.f[0], 0, 1.5f);
            v->formant = clampf(c.f[1], 0, 1);
            v->detune = clampf(c.f[2], -1, 1);
            v->pan = clampf(c.f[3], 0, 1);
            return;
        }
        if (c.type != Command::Param)
            return;
        switch (c.i[0]) {
        case PPreset: m_preset = std::clamp(c.i[1], 0, PresetCount - 1); break;
        case PFormant: m_formant = std::clamp(c.i[1], -6, 6); break;
        case PMonitor: m_monitor = c.i[1] != 0; break;
        case PMicGain: m_micGain = dbToGain(clampf(c.f[0], -24, 24)); break;
        case PFollow: m_follow = c.i[1] != 0; break;
        case PFilter: m_filt = {c.f[0], c.f[1], c.f[2], 0}; break;
        case PLofi: m_lofi = {c.f[0], c.f[1], c.f[2], 0}; break;
        case PFreeze: {
            const bool was = m_frz.on > 0.5f;
            m_frz = {c.f[0], c.f[1], c.f[2], c.f[3]};
            if (m_frz.on > 0.5f && !was)
                startFreeze(ctx);
            break;
        }
        case PEcho: m_echoP = {c.f[0], c.f[1], c.f[2], 0}; break;
        case PReverb: m_revP = {c.f[0], c.f[1], c.f[2], c.f[3]}; break;
        case PTape: {
            const bool on = c.f[0] > 0.5f;
            if (on && !m_tapeStop) {
                m_tapeStop = true;
                m_tapeRate = 1;
                m_tapeRead = double(m_tapeW);
            } else if (!on && m_tapeStop) {
                m_tapeStop = false;
                m_tapeBack = 1; // back to the live voice, crossfaded
            }
            break;
        }
        case PStutter: m_stutter = std::clamp(c.i[1], 0, kStutterCount - 1); break;
        default: break;
        }
    }

    void render(float *outL, float *outR, int n, AudioContext &ctx) override {
        const Transport &tr = ctx.transport();
        const Harmony &h = ctx.harmony();
        const float *in = ctx.input();
        m_hasInput = in != nullptr;
        Tape &tape = *m_tape;

        // 1. The microphone, cleaned up: rumble off, the room's hiss gated
        //    (gently: a soft consonant still gets through), levelled.
        float dry[kBlock];
        for (int i = 0; i < n; ++i) {
            const float raw = (in ? in[i] : 0.0f) * m_micGain;
            const float a = std::fabs(raw);
            m_inPeak = std::max(m_inPeak, a);
            m_inSq += raw * raw;
            ++m_inCount;
            m_scope[m_scopeW++ & (kScopeLen - 1)] = raw;
            const float x = m_hp.step(raw).high;
            m_envAcc += double(x) * x;
            const int64_t sample = int64_t(tr.sample) + i;
            if (((sample + 1) & (kHop - 1)) == 0) {
                tape.env[size_t((sample / kHop) & (kEnvRing - 1))] = float(std::sqrt(m_envAcc / kHop));
                m_envAcc = 0;
            }
            // The gate follows the room's floor: it opens 10 dB over it.
            const float ax = std::fabs(x);
            m_gateEnv += (ax - m_gateEnv) * (ax > m_gateEnv ? 0.02f : 0.0015f);
            m_noise = m_gateEnv < m_noise ? m_noise + (m_gateEnv - m_noise) * 0.001f : m_noise * 1.000004f + 1e-9f;
            m_noise = clampf(m_noise, 1e-5f, 0.05f);
            const bool open = m_gateEnv > m_noise * 3.2f || m_gateEnv > 0.02f;
            m_gate += ((open ? 1.0f : 0.05f) - m_gate) * (open ? 0.02f : 0.0004f);
            float y = x * m_gate;
            // A compressor, 3:1 from −20 dBFS, quick: an even voice.
            const float ay = std::fabs(y);
            m_compEnv += (ay - m_compEnv) * (ay > m_compEnv ? 0.01f : 0.0003f);
            const float comp = m_compEnv > 0.1f ? std::pow(0.1f / m_compEnv, 0.667f) : 1.0f;
            y *= comp * 1.8f;
            dry[i] = y;
            m_src.push(y);
            if (m_pitch.push(y))
                track();
        }

        // 2. The voice itself, by the preset.
        float leadL[kBlock] = {}, leadR[kBlock] = {}, shifted[kBlock] = {};
        const float hz = m_pitch.hz();
        const bool voiced = hz > 0 && m_gate > 0.3f;
        const float sungMidi = voiced ? midiOfHz(hz) : -1.0f;
        bool anyFinger = false;
        for (const auto &v : m_voices)
            anyFinger |= v.on;
        float leadTarget = 1.0f; // pitch ratio of the shifted lead
        bool shift = m_formant != 0;
        if (m_preset == Tune || m_preset == Robot) {
            shift = true;
            if (voiced) {
                double target;
                if (m_preset == Tune) {
                    target = snapMidi(h, sungMidi, m_follow);
                } else {
                    // The chord's root, in the octave nearest the voice.
                    const double root = h.midi(h.chordRoot, 0);
                    target = root + 12.0 * std::round((sungMidi - root) / 12.0);
                }
                m_leadMidi = float(target);
                leadTarget = std::exp2(float(target - sungMidi) / 12.0f);
            }
        } else {
            m_leadMidi = sungMidi;
        }
        const float formant = std::exp2(float(m_formant) / 12.0f * 0.75f);
        const float wantShift = shift ? 1.0f : 0.0f;
        const float wantDry = (m_preset == Prism && anyFinger) || m_preset == Vocoder ? 0.0f : 1.0f;
        const float ratioNow = m_leadRatio.y;
        if (m_leadShift.y > 0.001f || wantShift > 0) {
            // The ratio moves fast (hard tune) but not in one step.
            for (int i = 0; i < n; ++i)
                m_leadRatio.step(leadTarget);
            m_lead.process(m_src, n, voiced ? m_pitch.period() : 0.0f, 0.5f * (ratioNow + m_leadRatio.y), formant, 1.0f,
                           shifted);
        } else {
            m_lead.reset();
        }
        for (int i = 0; i < n; ++i) {
            const float s = m_leadShift.step(wantShift), dl = m_leadDry.step(wantDry);
            const float v = (dry[i] * (1 - s) + shifted[i] * s) * dl;
            leadL[i] = v;
            leadR[i] = v;
        }

        // The vocoder: a chord of saws, shaped band by band by the voice.
        const float vmix = m_vocoderMix.y;
        if (m_preset == Vocoder || vmix > 0.001f) {
            float carriers[8];
            int nc = 0;
            for (const auto &v : m_voices)
                if (v.on && nc < 8)
                    carriers[nc++] = float(noteMidi(h, v.value, v.mode));
            if (nc == 0) {
                const int cn = std::clamp(h.chordCount, 1, 4);
                for (int k = 0; k < cn; ++k)
                    carriers[nc++] = float(h.midi(h.chordTones[k], 1));
                carriers[nc++] = float(h.midi(h.chordRoot, 0));
            }
            for (int i = 0; i < n; ++i) {
                const float mix = m_vocoderMix.step(m_preset == Vocoder ? 1.0f : 0.0f);
                float car = 0;
                for (int k = 0; k < nc; ++k)
                    for (int d = 0; d < 2; ++d) {
                        float &ph = m_carPhase[k][d];
                        const float dt = midiToHz(carriers[k] + (d ? 0.08f : -0.08f)) / m_rate;
                        ph += dt;
                        if (ph >= 1)
                            ph -= 1;
                        car += 2 * ph - 1 - polyBlep(ph, dt);
                    }
                car *= 0.35f / std::sqrt(float(nc));
                float out = 0;
                for (int b = 0; b < kBands; ++b) {
                    Band &bd = m_bands[b];
                    const float a = std::fabs(bd.a.step(dry[i]).band);
                    bd.env += (a - bd.env) * (a > bd.env ? 0.004f : 0.0007f);
                    out += bd.s.step(car).band * bd.env;
                }
                const float sib = m_sib.step(dry[i]).high;
                const float v = (out * 14.0f + sib * 0.7f + dry[i] * 0.12f) * mix;
                leadL[i] += v;
                leadR[i] += v;
            }
        }

        // 3. The fingers: your voice again at each one's note.
        float bus[2][kBlock] = {};
        for (int i = 0; i < n; ++i) {
            bus[0][i] = leadL[i];
            bus[1][i] = leadR[i];
        }
        m_activeVoices = 0;
        for (auto &v : m_voices) {
            if (!v.on && v.level.y < 0.0005f)
                continue;
            ++m_activeVoices;
            const float target = float(noteMidi(h, v.value, v.mode)) + v.detune * 0.3f;
            v.midi = target;
            const float r = voiced ? std::exp2((target - sungMidi) / 12.0f) : 1.0f;
            const float r0 = v.ratio.y;
            for (int i = 0; i < n; ++i)
                v.ratio.step(r);
            const float lvl0 = v.level.y;
            float lvl1 = lvl0;
            for (int i = 0; i < n; ++i)
                lvl1 = v.level.step(v.on && m_preset != Vocoder ? v.gain : 0.0f);
            if (lvl0 < 0.0005f && lvl1 < 0.0005f)
                continue;
            float tmp[kBlock] = {};
            const float vf = std::exp2((v.formant - 0.5f) * 1.4f);
            v.shifter.process(m_src, n, voiced ? m_pitch.period() : 0.0f, 0.5f * (r0 + v.ratio.y), vf, 1.0f, tmp);
            const float width = 0.6f + 0.4f * std::fabs(v.detune);
            const float pan = 0.5f + (v.pan - 0.5f) * width;
            const float gl = std::sqrt(1 - pan) * 1.2f, gr = std::sqrt(pan) * 1.2f;
            for (int i = 0; i < n; ++i) {
                const float g = lvl0 + (lvl1 - lvl0) * float(i + 1) / float(n);
                bus[0][i] += tmp[i] * g * gl;
                bus[1][i] += tmp[i] * g * gr;
            }
        }

        // The choir: the chord's tones nearest the note sung, one above,
        // one below, and the root under them.
        {
            const bool want = m_preset == Choir;
            if (want && voiced && (std::fabs(sungMidi - m_choirAt) > 0.7f || h.chordSeq != m_choirSeq)) {
                m_choirAt = sungMidi;
                m_choirSeq = h.chordSeq;
                double above = 1e9, below = -1e9;
                const int cn = std::clamp(h.chordCount, 1, 4);
                for (int oct = -2; oct <= 6; ++oct)
                    for (int k = 0; k < cn; ++k) {
                        const double m = h.midi(h.chordTones[k], oct);
                        if (m > sungMidi + 1.5 && m < above)
                            above = m;
                        if (m < sungMidi - 1.5 && m > below)
                            below = m;
                    }
                const double root = h.midi(h.chordRoot, 0);
                m_choirMidi[0] = float(above);
                m_choirMidi[1] = float(below);
                m_choirMidi[2] = float(root + 12.0 * std::floor((sungMidi - 7 - root) / 12.0));
            }
            static const float pans[3] = {0.15f, 0.85f, 0.5f};
            static const float throats[3] = {1.06f, 0.94f, 0.88f};
            for (int k = 0; k < 3; ++k) {
                ChoirVoice &c = m_choir[k];
                const float lvl0 = c.level.y;
                float lvl1 = lvl0;
                for (int i = 0; i < n; ++i)
                    lvl1 = c.level.step(want ? (k == 2 ? 0.5f : 0.7f) : 0.0f);
                if (lvl0 < 0.0005f && lvl1 < 0.0005f) {
                    c.shifter.reset();
                    continue;
                }
                const float r = voiced ? std::exp2((m_choirMidi[k] - sungMidi) / 12.0f) : 1.0f;
                const float r0 = c.ratio.y;
                for (int i = 0; i < n; ++i)
                    c.ratio.step(r);
                float tmp[kBlock] = {};
                c.shifter.process(m_src, n, voiced ? m_pitch.period() : 0.0f, 0.5f * (r0 + c.ratio.y), throats[k], 1.0f,
                                  tmp);
                const float gl = std::sqrt(1 - pans[k]), gr = std::sqrt(pans[k]);
                for (int i = 0; i < n; ++i) {
                    const float g = lvl0 + (lvl1 - lvl0) * float(i + 1) / float(n);
                    bus[0][i] += tmp[i] * g * gl;
                    bus[1][i] += tmp[i] * g * gr;
                }
            }
        }

        // 4. The strips, in order: filter, lo-fi, freeze or stutter, tape
        //    stop, then the throws.
        const double spb = tr.secondsPerBeat();
        {
            const float on = m_filt.on > 0.5f ? 1.0f : 0.0f;
            if (on > 0 || m_filtOn.y > 0.001f) {
                const float cut = m_filtCut.step(m_filt.on > 0.5f ? m_filt.x : m_filtCut.y);
                const float res = m_filtRes.step(m_filt.on > 0.5f ? m_filt.p : 0.0f);
                const float hzc = 160.0f * std::exp2(cut * 5.6f);
                const float q = 0.8f + res * 7.0f;
                m_filtL.set(hzc, q, m_rate);
                m_filtR.set(hzc, q, m_rate);
                for (int i = 0; i < n; ++i) {
                    const float w = m_filtOn.step(on);
                    const auto a = m_filtL.step(bus[0][i]), b = m_filtR.step(bus[1][i]);
                    // Pressure turns the low-pass into a wah: more band.
                    const float l = a.low * (1 - res) + a.band * res * 1.6f, r = b.low * (1 - res) + b.band * res * 1.6f;
                    bus[0][i] += (l - bus[0][i]) * w;
                    bus[1][i] += (r - bus[1][i]) * w;
                }
            }
        }
        {
            const float on = m_lofi.on > 0.5f ? clampf(0.45f + m_lofi.p, 0, 1) : 0.0f;
            if (on > 0 || m_lofiOn.y > 0.001f) {
                const bool radio = m_lofi.x < 0.5f;
                const float u = radio ? m_lofi.x * 2 : (m_lofi.x - 0.5f) * 2;
                if (radio) {
                    m_radioHp.set(350.0f + 600.0f * u, 0.9f, m_rate);
                    m_radioHp2.set(350.0f + 600.0f * u, 0.9f, m_rate);
                    m_radioLp.set(3200.0f - 1500.0f * u, 1.1f, m_rate);
                    m_radioLp2.set(3200.0f - 1500.0f * u, 1.1f, m_rate);
                }
                const float bits = std::exp2(11.0f - 8.0f * u);
                const int hold = 1 + int(u * 14.0f);
                for (int i = 0; i < n; ++i) {
                    const float w = m_lofiOn.step(on);
                    float l = bus[0][i], r = bus[1][i];
                    if (radio) {
                        l = fastTanh(m_radioLp.step(m_radioHp.step(l).high).low * (2.5f + 6.0f * u)) * 0.5f;
                        r = fastTanh(m_radioLp2.step(m_radioHp2.step(r).high).low * (2.5f + 6.0f * u)) * 0.5f;
                    } else {
                        if (++m_holdN >= hold) {
                            m_holdN = 0;
                            m_holdL = std::round(l * bits) / bits;
                            m_holdR = std::round(r * bits) / bits;
                        }
                        l = m_holdL;
                        r = m_holdR;
                    }
                    bus[0][i] += (l - bus[0][i]) * w;
                    bus[1][i] += (r - bus[1][i]) * w;
                }
            }
        }
        // What the freeze can take hold of: the voice so far, mono.
        for (int i = 0; i < n; ++i)
            m_recent[size_t(m_recentW++) & (m_recent.size() - 1)] = 0.5f * (bus[0][i] + bus[1][i]);
        {
            const bool stutter = m_frz.on > 0.5f && m_frz.x >= 0.5f;
            const bool cloud = m_frz.on > 0.5f && m_frz.x < 0.5f;
            const float lvl = clampf(0.5f + m_frz.p * 0.8f, 0, 1.2f);
            if (stutter || cloud || m_freezeLvl.y > 0.001f || m_stutterOn.y > 0.001f) {
                const double sliceBeats = kStutterBeats[std::clamp(
                    m_stutter + (m_frz.x > 0.83f ? 2 : m_frz.x > 0.66f ? 1 : 0), 0, kStutterCount - 1)];
                const int slice = std::clamp(int(sliceBeats * spb * m_rate), 64, int(m_freezeBuf.size()) - 1);
                const float pitch = std::exp2(clampf(m_frz.semis, -24, 24) / 12.0f);
                for (int i = 0; i < n; ++i) {
                    const float fl = m_freezeLvl.step(cloud ? lvl : 0.0f);
                    const float st = m_stutterOn.step(stutter ? 1.0f : 0.0f);
                    float add = 0;
                    if (fl > 0.0005f)
                        add += freezeCloud(pitch) * fl;
                    if (st > 0.0005f) {
                        // On the grid: each slice starts where the beat
                        // divides, playing the last slice heard before the
                        // finger came down.
                        const double beat = tr.beatAt(i);
                        const double k = std::floor(beat / sliceBeats);
                        if (k != m_stutterK) {
                            m_stutterK = k;
                            m_stutterPos = 0;
                        }
                        const int len = slice;
                        const int from = int(m_freezeBuf.size()) - len;
                        float s = 0;
                        if (m_stutterPos < len) {
                            const float fade = std::min(1.0f, std::min(float(m_stutterPos), float(len - m_stutterPos)) / 96.0f);
                            s = m_freezeBuf[size_t(from + int(m_stutterPos))] * fade;
                        }
                        m_stutterPos += 1;
                        bus[0][i] *= 1 - st;
                        bus[1][i] *= 1 - st;
                        add += s * st * lvl;
                    }
                    bus[0][i] += add;
                    bus[1][i] += add;
                }
            }
        }
        {
            // The tape: stopping winds it down to nothing over half a
            // second; letting go crossfades back to the voice as it is.
            const uint32_t mask = uint32_t(m_tapeL.size() - 1);
            for (int i = 0; i < n; ++i) {
                m_tapeL[m_tapeW & mask] = bus[0][i];
                m_tapeR[m_tapeW & mask] = bus[1][i];
                ++m_tapeW;
                if (m_tapeStop || m_tapeBack > 0) {
                    if (m_tapeStop)
                        m_tapeRate = std::max(0.0f, m_tapeRate - 1.0f / (0.55f * m_rate));
                    const double f = std::floor(m_tapeRead);
                    const uint32_t k = uint32_t(int64_t(f)) & mask;
                    const float u = float(m_tapeRead - f);
                    const float tl = m_tapeL[k] + (m_tapeL[(k + 1) & mask] - m_tapeL[k]) * u;
                    const float trr = m_tapeR[k] + (m_tapeR[(k + 1) & mask] - m_tapeR[k]) * u;
                    m_tapeRead += m_tapeRate;
                    const float g = std::min(1.0f, m_tapeRate * 6.0f);
                    if (m_tapeStop) {
                        bus[0][i] = tl * g;
                        bus[1][i] = trr * g;
                    } else {
                        m_tapeBack = std::max(0.0f, m_tapeBack - 1.0f / (0.03f * m_rate));
                        bus[0][i] = bus[0][i] * (1 - m_tapeBack) + tl * g * m_tapeBack;
                        bus[1][i] = bus[1][i] * (1 - m_tapeBack) + trr * g * m_tapeBack;
                    }
                }
            }
        }
        {
            const int div = std::clamp(int(m_echoP.x * 3.0f), 0, 2);
            static const float echoBeats[3] = {0.5f, 0.75f, 1.0f};
            m_echo.set(0.45f + 0.4f * m_echoP.p, float(spb) * echoBeats[div]);
            m_reverb.set(0.72f + 0.25f * m_revP.x, clampf(m_revP.semis, 0, 1));
            const float es = m_echoP.on > 0.5f ? 0.45f + 0.55f * m_echoP.p : 0.0f;
            const float rs = m_revP.on > 0.5f ? 0.45f + 0.55f * m_revP.p : 0.0f;
            const bool echoLive = es > 0 || m_echoSend.y > 0.0005f || m_echoTail > 0;
            const bool revLive = rs > 0 || m_revSend.y > 0.0005f || m_revTail > 0;
            for (int i = 0; i < n; ++i) {
                const float e = m_echoSend.step(es), r = m_revSend.step(rs);
                float el = 0, er = 0, rl = 0, rr = 0;
                if (echoLive)
                    m_echo.process(bus[0][i] * e, bus[1][i] * e, el, er);
                if (revLive)
                    m_reverb.process(bus[0][i] * r, bus[1][i] * r, rl, rr);
                bus[0][i] += el + rl * 0.8f;
                bus[1][i] += er + rr * 0.8f;
            }
            // The tails ring on for a while after the finger lifts.
            m_echoTail = es > 0 ? int(m_rate * 8) : std::max(0, m_echoTail - n);
            m_revTail = rs > 0 ? int(m_rate * 12) : std::max(0, m_revTail - n);
        }

        // 5. Onto the tape for Space (always, heard or not), into the ears
        //    (when monitored), and the loops on top.
        float livePeak = 0;
        const float mon = m_monitor && m_feedback <= 0 ? 1.0f : m_monitor ? 0.06f : 0.0f;
        for (int i = 0; i < n; ++i) {
            const float l = fastTanh(bus[0][i] * 0.9f) / 0.9f, r = fastTanh(bus[1][i] * 0.9f) / 0.9f;
            const int64_t s = int64_t(tr.sample) + i;
            tape.wetL[size_t(s & kRingMask)] = l;
            tape.wetR[size_t(s & kRingMask)] = r;
            const float g = m_monGain.step(mon);
            outL[i] += l * g;
            outR[i] += r * g;
            livePeak = std::max(livePeak, std::max(std::fabs(l), std::fabs(r)));
        }
        m_livePeak = std::max(livePeak, m_livePeak * 0.97f);
        tape.written.store(int64_t(tr.sample) + n, std::memory_order_release);
        if (m_feedback > 0)
            m_feedback -= n;

        renderLoops(outL, outR, n, ctx);
    }

    void silence(bool hard) override {
        for (auto &v : m_voices) {
            v.on = false;
            v.id = -1;
            if (hard)
                v.level.reset(0);
        }
        for (auto &l : m_loops) {
            if (hard)
                l.id = -1;
            else
                l.fading = true;
        }
        m_frz.on = 0;
        m_filt.on = 0;
        m_lofi.on = 0;
        m_echoP.on = 0;
        m_revP.on = 0;
        if (m_tapeStop) {
            m_tapeStop = false;
            m_tapeBack = 1;
        }
    }

    void publish(float *d) override {
        d[pub::InPeak] = m_inPeak;
        d[pub::InRms] = m_inCount ? std::sqrt(m_inSq / float(m_inCount)) : 0;
        m_inPeakHeld = std::max(m_inPeak, m_inPeakHeld * 0.9f);
        m_inPeak = 0;
        m_inSq = 0;
        m_inCount = 0;
        d[pub::Hz] = m_gate > 0.3f ? m_pitch.hz() : 0;
        d[pub::Clarity] = m_pitch.clarity();
        d[pub::Gate] = m_gate;
        d[pub::Feedback] = m_feedback > 0 ? 1 : 0;
        d[pub::Monitor] = m_monitor ? 1 : 0;
        d[pub::LivePeak] = m_livePeak;
        d[pub::Tape] = m_tapeStop ? m_tapeRate : 1;
        d[pub::Freeze] = std::max(m_freezeLvl.y, m_stutterOn.y);
        int loops = 0;
        for (const auto &l : m_loops)
            loops += l.id >= 0;
        d[pub::Loops] = float(loops);
        d[pub::LeadMidi] = m_leadMidi;
        d[pub::HasInput] = m_hasInput ? 1 : 0;
        d[pub::Stutter] = float(m_stutter);
        // The lag of a shifted copy: about half a period, plus what the
        // formant asks.
        d[pub::Lag] = m_pitch.period() > 0 ? m_pitch.period() * 0.5f / m_rate * 1000.0f : 3.0f;
        for (int k = 0; k < kMaxVoices; ++k) {
            const Voice &v = m_voices[k];
            d[pub::Voices + 3 * k] = v.on ? 1 : 0;
            d[pub::Voices + 3 * k + 1] = v.midi;
            d[pub::Voices + 3 * k + 2] = v.level.y;
        }
        const int per = kScopeLen / pub::ScopeN;
        for (int k = 0; k < pub::ScopeN; ++k) {
            float lo = 0, hi = 0;
            for (int j = 0; j < per; ++j) {
                const float v = m_scope[(m_scopeW + uint32_t(k * per + j)) & (kScopeLen - 1)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            d[pub::Scope + 2 * k] = lo;
            d[pub::Scope + 2 * k + 1] = hi;
        }
    }

    int voices() const override { return m_activeVoices; }

private:
    void track() {
        // Feedback: a pure tone, loud, that doesn't waver for half a
        // second is the speakers singing into the microphone, not a voice
        // (which always wavers a little). The voice ducks for two seconds.
        const float hz = m_pitch.hz();
        if (hz > 0 && m_pitch.clarity() > 0.985f && m_gateEnv > 0.12f && m_howlHz > 0 &&
            std::fabs(hz - m_howlHz) < 0.0025f * hz)
            m_howl += 128;
        else
            m_howl = 0;
        m_howlHz = hz;
        if (m_howl > int(m_rate * 0.5f) && m_monitor) {
            m_feedback = int(m_rate * 2);
            m_howl = 0;
        }
    }

    // --- freeze -----------------------------------------------------------------
    void startFreeze(AudioContext &) {
        // The last three quarters of a second, held.
        const size_t len = m_freezeBuf.size();
        for (size_t i = 0; i < len; ++i)
            m_freezeBuf[i] = m_recent[size_t(m_recentW - int64_t(len) + int64_t(i)) & (m_recent.size() - 1)];
        m_stutterK = -1;
        for (auto &g : m_cloud)
            g.left = 0;
    }
    float freezeCloud(float pitch) {
        // Overlapping 90 ms grains from anywhere in the last half second
        // held: the sound stands still, breathing.
        const int glen = int(0.09f * m_rate);
        if (--m_cloudNext <= 0) {
            m_cloudNext = int(0.018f * m_rate);
            for (auto &g : m_cloud)
                if (g.left <= 0) {
                    const int span = int(m_freezeBuf.size()) - glen * 2 - 8;
                    g.pos = double(glen) + double(m_rng.unit()) * std::max(1, span) * 0.7 + span * 0.3;
                    g.left = glen;
                    g.len = glen;
                    break;
                }
        }
        float out = 0;
        for (auto &g : m_cloud) {
            if (g.left <= 0)
                continue;
            const float u = 1.0f - float(g.left) / float(g.len);
            const double f = std::floor(g.pos);
            const size_t k = std::min(size_t(f), m_freezeBuf.size() - 2);
            const float a = m_freezeBuf[k], b = m_freezeBuf[k + 1];
            out += (a + (b - a) * float(g.pos - f)) * hann().at(u);
            g.pos += pitch;
            if (g.pos >= double(m_freezeBuf.size() - 2))
                g.pos -= double(glen);
            --g.left;
        }
        return out * 0.4f;
    }

    // --- loops ------------------------------------------------------------------
    struct Loop {
        int id = -1;
        int handle = 0;
        double pos = 0;   // frames into the take's audio
        int length = 0;
        int startAt = 0;  // offset in the block where it begins
        float gain = 0;
        bool fading = false;
    };
    void startLoop(const Command &c, AudioContext &ctx) {
        const int handle = c.i[0];
        if (handle <= 0 || handle >= kMaxTakes)
            return;
        const Tape::Take &take = m_tape->takes[handle];
        const int len = take.length;
        if (len <= 0)
            return;
        // Where in the audio it starts: the clip's start falls i[3] frames
        // in; a loop joined late (its t in the past) that much further.
        int offset = 0;
        double late = 0;
        if (c.t > 0 && c.t < ctx.now())
            late = (ctx.now() - c.t) * double(m_rate);
        else if (c.t > 0)
            offset = ctx.offsetOf(c.t, kBlock);
        const double pos = std::fmod(double(c.i[3]) + late, double(len));
        // The same sound coming round again where it already is: it plays
        // straight on (each pass retriggers it, sample for sample).
        for (auto &l : m_loops)
            if (l.id == c.id && l.handle == handle) {
                const double expect = std::fmod(l.pos + double(offset), double(len));
                const double diff = std::fabs(expect - pos);
                if (std::min(diff, double(len) - diff) < 64) {
                    l.fading = false;
                    return;
                }
                l.fading = true;
                l.id = -2; // the old pass fades; the new one starts
            }
        Loop *slot = nullptr;
        for (auto &l : m_loops)
            if (l.id == -1) {
                slot = &l;
                break;
            }
        if (!slot)
            return;
        slot->id = c.id;
        slot->handle = handle;
        slot->pos = pos;
        slot->length = len;
        slot->startAt = offset;
        slot->gain = 0;
        slot->fading = false;
    }
    void renderLoops(float *outL, float *outR, int n, AudioContext &) {
        const Tape &tape = *m_tape;
        const int64_t written = tape.written.load(std::memory_order_relaxed);
        const float fadeIn = 1.0f / (0.004f * m_rate), fadeOut = 1.0f / (0.008f * m_rate);
        for (auto &l : m_loops) {
            if (l.id == -1)
                continue;
            const Tape::Take &take = tape.takes[l.handle];
            const float *data = take.data.load(std::memory_order_acquire);
            for (int i = l.startAt; i < n; ++i) {
                if (l.fading) {
                    l.gain -= fadeOut;
                    if (l.gain <= 0) {
                        l.id = -1;
                        break;
                    }
                } else if (l.gain < 1) {
                    l.gain = std::min(1.0f, l.gain + fadeIn);
                }
                const int k = int(l.pos);
                float a = 0, b = 0;
                if (data) {
                    a = data[2 * k];
                    b = data[2 * k + 1];
                } else {
                    // Not copied out yet: straight from the ring, which
                    // still holds it.
                    const int64_t s = take.start + k;
                    if (s < written && written - s < kRing) {
                        a = tape.wetL[size_t(s & kRingMask)];
                        b = tape.wetR[size_t(s & kRingMask)];
                    }
                }
                outL[i] += a * l.gain;
                outR[i] += b * l.gain;
                l.pos += 1;
                if (l.pos >= l.length)
                    l.pos -= l.length;
            }
            l.startAt = 0;
        }
    }

    std::shared_ptr<Tape> m_tape;
    float m_rate = 48000;

    // The input.
    Svf m_hp;
    float m_micGain = 1, m_gateEnv = 0, m_noise = 0.001f, m_gate = 0, m_compEnv = 0;
    double m_envAcc = 0;
    float m_inPeak = 0, m_inPeakHeld = 0, m_inSq = 0, m_livePeak = 0;
    int m_inCount = 0;
    bool m_hasInput = false;
    static constexpr int kScopeLen = 2048;
    float m_scope[kScopeLen] = {};
    uint32_t m_scopeW = 0;
    PitchTracker m_pitch;
    Source m_src;
    int m_howl = 0, m_feedback = 0;
    float m_howlHz = 0;

    // The voice.
    int m_preset = Natural, m_formant = 0;
    bool m_monitor = true, m_follow = false;
    Shifter m_lead;
    Smooth m_leadRatio, m_leadShift, m_leadDry, m_vocoderMix, m_monGain;
    float m_leadMidi = -1;
    struct Voice {
        int id = -1;
        bool on = false;
        int value = 0, mode = 0;
        float gain = 0, formant = 0.5f, detune = 0, pan = 0.5f, midi = 0;
        Smooth level, ratio;
        Shifter shifter;
    };
    Voice m_voices[kMaxVoices];
    int m_activeVoices = 0;
    struct ChoirVoice {
        Smooth level, ratio;
        Shifter shifter;
    };
    ChoirVoice m_choir[3];
    float m_choirMidi[3] = {60, 55, 48};
    float m_choirAt = -100;
    uint32_t m_choirSeq = ~0u;
    static constexpr int kBands = 16;
    struct Band {
        Svf a, s;
        float env = 0;
    };
    Band m_bands[kBands];
    Svf m_sib;
    float m_carPhase[8][2] = {};

    // The strips: where the finger is (x / height), pressure, a third value, on.
    struct Strip {
        float x = 0, p = 0, semis = 0, on = 0;
    };
    Strip m_filt, m_lofi, m_frz, m_echoP, m_revP;
    Svf m_filtL, m_filtR;
    Smooth m_filtOn, m_filtCut, m_filtRes, m_lofiOn;
    Svf m_radioHp, m_radioLp, m_radioHp2, m_radioLp2;
    float m_holdL = 0, m_holdR = 0;
    int m_holdN = 0;
    std::vector<float> m_recent, m_freezeBuf;
    int64_t m_recentW = 0;
    Smooth m_freezeLvl, m_stutterOn;
    struct CloudGrain {
        double pos = 0;
        int left = 0, len = 1;
    };
    CloudGrain m_cloud[8];
    int m_cloudNext = 0;
    Rng m_rng{0x70CA1};
    int m_stutter = 2;
    double m_stutterK = -1;
    float m_stutterPos = 0;
    std::vector<float> m_tapeL, m_tapeR;
    uint32_t m_tapeW = 0;
    bool m_tapeStop = false;
    float m_tapeRate = 1, m_tapeBack = 0;
    double m_tapeRead = 0;
    TapeEcho m_echo;
    tf::Reverb m_reverb;
    Smooth m_echoSend, m_revSend;
    int m_echoTail = 0, m_revTail = 0;

    Loop m_loops[16];
};

// --- the page -------------------------------------------------------------------

class Page : public PartControl {
public:
    explicit Page(std::shared_ptr<Tape> tape) : m_tape(std::move(tape)) {}

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        // The click: tape stop while pressed. A finger that landed just
        // before is the one doing the clicking, not a note.
        if (f.button != m_button) {
            m_button = f.button;
            if (f.button)
                for (Finger &h : m_fingers)
                    if (h.id >= 0 && f.t - h.t0 < 0.25 && h.zone == Keys && !h.clicker) {
                        h.clicker = true;
                        release(h, ctx);
                    }
            sendTape(ctx);
        }
        for (Finger &h : m_fingers) {
            if (h.id < 0 || h.key)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still)
                lift(h, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Finger *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (!h)
                    continue;
                land(*h, t, f.t, ctx);
            } else {
                move(*h, t, f.t, ctx);
            }
        }
    }

    void background(double t, ControlContext &ctx) override {
        const int part = ctx.part();
        if (part < 0)
            return;
        // The handles of loops that were replaced are let go a while after.
        for (size_t i = 0; i < m_graveyard.size();) {
            if (t >= m_graveyard[i].first) {
                freeHandle(m_graveyard[i].second);
                m_graveyard.erase(m_graveyard.begin() + long(i));
            } else {
                ++i;
            }
        }
        copyPending();
        // What was sung, for the trail of notes the page draws.
        const PartView &v = ctx.studio().parts[part];
        const float hz = v.data[pub::Hz];
        if (t - m_lastTrail >= 1.0 / 60) {
            m_lastTrail = t;
            m_trail[m_trailW++ % kTrail] = {t, hz > 0 ? midiOfHz(hz) : -1.0f, v.data[pub::InRms]};
        }
        // The keys sit where the voice is: an octave around what has been
        // sung lately, moved only while no finger is on them.
        bool keysHeld = false;
        for (const Finger &h : m_fingers)
            keysHeld |= h.id >= 0 && h.zone == Keys && !h.clicker;
        if (!keysHeld && t - m_lastRegister > 0.5) {
            m_lastRegister = t;
            std::vector<float> sung;
            for (int k = 0; k < kTrail; ++k)
                if (m_trail[k].midi > 0 && t - m_trail[k].t < 10)
                    sung.push_back(m_trail[k].midi);
            if (sung.size() > 20) {
                std::nth_element(sung.begin(), sung.begin() + long(sung.size() / 2), sung.end());
                m_sungMedian = sung[sung.size() / 2];
            }
        }
        // The monitor follows the page unless asked otherwise.
        const bool want = m_monitorMode == 1 || (m_monitorMode == 0 && m_shown);
        if (want != m_monitorSent) {
            m_monitorSent = want;
            param(ctx, PMonitor, want ? 1 : 0);
        }
        // Takes landing in the studio: the loop has changed since.
        if (v.takes != m_takesSeen || v.playing != m_playingSeen) {
            m_takesSeen = v.takes;
            m_playingSeen = v.playing;
            m_loopSince = ctx.beatAt(t);
        }
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (k.key == Qt::Key_Space) {
            if (k.down && !k.repeat)
                space(ctx, shift);
            return true;
        }
        // Held keys: notes on the home row, throws, freeze, stutter and the
        // tape on the top row.
        const int lane = homeLane(k.key);
        const Zone held = k.key == Qt::Key_Q   ? Echo
                          : k.key == Qt::Key_W ? Reverb
                          : k.key == Qt::Key_E ? Freeze
                          : k.key == Qt::Key_R ? Freeze
                                               : Keys;
        if ((lane >= 0 || held != Keys || k.key == Qt::Key_T) && !(k.modifiers & Qt::ControlModifier)) {
            if (k.repeat)
                return true;
            const int id = kKeyFinger + k.key;
            if (!k.down) {
                if (k.key == Qt::Key_T) {
                    m_tapeKey = false;
                    sendTape(ctx);
                } else if (Finger *h = find(id)) {
                    lift(*h, ctx);
                }
                return true;
            }
            if (k.key == Qt::Key_T) {
                m_tapeKey = true;
                sendTape(ctx);
                return true;
            }
            Finger *h = find(-1);
            if (!h || find(id))
                return true;
            Touch t;
            t.id = id;
            t.pressure = 0.6f;
            t.size = 1;
            if (lane >= 0) {
                const int lanes = laneCount(ctx.studio());
                t.x = kLeft + (kRight - kLeft) * (float(lane) + 0.5f) / float(lanes);
                t.y = 0.5f * (kBottom + kTop);
            } else if (held == Echo) {
                t.x = 0.3f, t.y = 0.93f;
            } else if (held == Reverb) {
                t.x = 0.75f, t.y = 0.93f;
            } else {
                t.x = 0.955f;
                t.y = k.key == Qt::Key_E ? 0.3f : 0.75f;
            }
            land(*h, t, ctx.now(), ctx);
            h->key = true;
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 emptied").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        switch (k.key) {
        case Qt::Key_Backspace: ctx.stop(); return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight: {
            const int dir = (k.key == Qt::Key_Right || k.key == Qt::Key_BracketRight) ? 1 : -1;
            if (shift || k.key == Qt::Key_BracketLeft || k.key == Qt::Key_BracketRight)
                setStutter(m_stutter + dir, ctx);
            else
                setPreset((m_preset + dir + PresetCount) % PresetCount, ctx);
            return true;
        }
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const int dir = k.key == Qt::Key_Up ? 1 : -1;
            // Just after Space they make the loop longer or shorter.
            if (m_last.active && ctx.now() - m_last.at <= kRetakeSecs) {
                resizeLoop(dir, ctx);
                return true;
            }
            m_octave = std::clamp(m_octave + dir, -2, 2);
            ctx.notice(QStringLiteral("Keys %1").arg(m_octave == 0 ? QStringLiteral("around your voice")
                                                                    : QStringLiteral("%1%2 octave%3")
                                                                          .arg(m_octave > 0 ? "+" : "")
                                                                          .arg(m_octave)
                                                                          .arg(std::abs(m_octave) > 1 ? "s" : "")));
            return true;
        }
        case Qt::Key_Minus:
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            setFormant(m_formant + (k.key == Qt::Key_Minus ? -1 : 1), ctx);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift) {
                ctx.notice(QStringLiteral("On the Vocal page, Shift+Space layers onto the loop"));
                return true;
            }
            setLatch(!m_latch, ctx);
            return true;
        case Qt::Key_section:
        case Qt::Key_QuoteLeft:
        case Qt::Key_Backslash:
            setFollow(!m_follow, ctx);
            return true;
        case Qt::Key_Delete: letGo(ctx); return true;
        case Qt::Key_M: cycleMonitor(ctx); return true;
        case Qt::Key_Comma: setMicGain(m_micDb - 3, ctx); return true;
        case Qt::Key_Period: setMicGain(m_micDb + 3, ctx); return true;
        default: return false;
        }
    }

    void enter(ControlContext &ctx) override {
        m_shown = true;
        if (!m_sentAll) {
            m_sentAll = true;
            param(ctx, PPreset, m_preset);
            param(ctx, PStutter, m_stutter);
        }
    }

    void leave(ControlContext &ctx) override {
        for (Finger &h : m_fingers)
            if (h.id >= 0)
                lift(h, ctx);
        if (!m_latch)
            letGoLatched(ctx);
        m_tapeKey = false;
        m_button = false;
        sendTape(ctx);
        m_frame = Frame{};
        m_shown = false;
    }

    void paint(QPainter &p, const PaintContext &pc) override;
    enum Spot { SpotPreset = 100, SpotMonitor = 200, SpotLatch, SpotFollow, SpotGainDown, SpotGainUp, SpotNew, SpotLayer };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, *&ctx.studio().parts[ctx.part()], ctx))
            return true;
        if (id >= int(SpotPreset) && id < int(SpotPreset) + int(PresetCount)) {
            setPreset(id - SpotPreset, ctx);
            return true;
        }
        switch (id) {
        case SpotMonitor: cycleMonitor(ctx); return true;
        case SpotLatch: setLatch(!m_latch, ctx); return true;
        case SpotFollow: setFollow(!m_follow, ctx); return true;
        case SpotGainDown: setMicGain(m_micDb - 3, ctx); return true;
        case SpotGainUp: setMicGain(m_micDb + 3, ctx); return true;
        case SpotNew: space(ctx, false); return true;
        case SpotLayer: space(ctx, true); return true;
        default: return false;
        }
    }

    QString help() const override {
        return QStringLiteral(
            "sing\tyou hear yourself at once, through everything below; the page shows the note you sing and the "
            "words as they go by\n"
            "Space\ta NEW vocal loop of what you just sang: the last phrase, in whole bars, its cuts moved up to a "
            "beat off the bar lines so they fall in a breath, never in a word (the bracket on the tape shows what "
            "Space would take, before you press it)\n"
            "Shift+Space\tLAYER what you sang over the vocal loop onto it (on the other pages it is the other way "
            "round: on this one a take is a take, and layering is the choice)\n"
            "Up / Down just after Space\tthe loop longer or shorter (1, 2, 4, 8, 16 bars; with nothing playing, a "
            "phrase more or less), its cuts found again in the silences\n"
            "Space again while a take waits\tcall it off\n"
            "with nothing else playing\tsing a phrase and press Space where it would start again: the loop sets the "
            "tempo\n"
            "a finger in the middle\tyour voice again, at that note of the key (the vowels stay yours): low to high "
            "across the pad, an octave either side of where you sing\n"
            "its height · pressure · twist\tthe throat, giant low to elf high · its level · a detune that widens it\n"
            "more fingers\ta harmony, a chord, a ten-voice choir of you\n"
            "left edge\ta resonant filter: slide up and down for a wah, press for more\n"
            "right edge, lower half\tFREEZE: the last sound held as a cloud you can sing over; twist to pitch it\n"
            "right edge, upper half\tSTUTTER: the last slice repeated on the grid, faster higher up; the voice "
            "steps aside\n"
            "top edge\tthrows: ECHO on the left (across: 1/8, dotted 1/8, 1/4), REVERB on the right; the tails ring "
            "on after you lift\n"
            "bottom edge\tRADIO on the left, CRUSH on the right: further across is rougher, pressure is more\n"
            "click the pad\tTAPE STOP: the voice winds down while you hold it\n"
            "Left / Right\tthe voice: natural, hard tune, prism (only the notes you hold), choir on the Chords "
            "page's chord, vocoder, robot\n"
            "Shift+Left / Right (or [ ])\tthe stutter's rate: 1/8, 1/8T, 1/16, 1/16T, 1/32\n"
            "Up / Down\tthe keys an octave up or down\n"
            "- and +\tthe voice's own throat: darker (giant), brighter (elf)\n"
            "Return\tlatch: notes stay when you lift (sing over a held chord)\n"
            "the key left of 1 (§)\tfollow the Chords page: the keys are the chord's notes, hard tune snaps to "
            "them\n"
            "Delete\tlet go: latched notes, freeze, every strip\n"
            "A S D F G H J K L\tnotes from the keyboard, held as long as the key\n"
            "Q · W · E · R · T\techo throw · reverb throw · freeze · stutter · tape stop, held\n"
            "M\tmonitor: on this page, always, or off (the loops still record what you sing)\n"
            ", and .\tthe microphone's level down, up (3 dB)\n"
            "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot · empty it · stop the vocal loop at the bar\n"
            "headphones\tthe voice comes back at once: on speakers it would feed back (the page ducks the voice if "
            "it hears that happen)");
    }

    QString status() const override {
        QStringList parts;
        int notes = 0;
        for (const Finger &h : m_fingers)
            if (h.id >= 0 && !h.clicker) {
                if (h.zone == Keys)
                    ++notes;
                else
                    parts << QString::fromLatin1(zoneName(h.zone));
            }
        notes += int(m_latched.size());
        if (notes)
            parts.prepend(notes == 1 ? QStringLiteral("1 voice") : QStringLiteral("%1 voices").arg(notes));
        if (m_button || m_tapeKey)
            parts << QStringLiteral("TAPE STOP");
        QString s = parts.join(QStringLiteral(" + "));
        if (s.isEmpty())
            s = QStringLiteral("sing; fingers add voices and effects");
        s += QStringLiteral("  ·  %1").arg(QString::fromLatin1(presetName(m_preset)));
        s += QStringLiteral("  ·  Space new loop, Shift+Space layer");
        return s;
    }

    void startOver(ControlContext &) override {
        // Every loop is gone: so is their audio, once the engine has let go.
        for (int h = 1; h < kMaxTakes; ++h)
            if (m_used[h])
                m_graveyard.push_back({m_now + 1.0, h});
        m_pending.clear();
        m_last = {};
    }
    void undone(ControlContext &) override { m_last = {}; }

    int loopBeats(int dir) const override {
        if (!m_last.active)
            return -1;
        if (m_last.free) {
            const int n = m_last.phrases + dir;
            if (n < 1 || n > int(m_last.starts.size()))
                return 0;
            const double secs = m_last.end - (m_last.starts[m_last.starts.size() - size_t(n)] - 0.03);
            return int(freeBeats(secs));
        }
        const auto steps = stepsFor(m_last);
        const auto it = std::find(steps.begin(), steps.end(), m_last.bars);
        const long at = long(it - steps.begin()) + dir;
        if (it == steps.end() || at < 0 || at >= long(steps.size()))
            return 0;
        return steps[size_t(at)] * 4;
    }

    void resizeLoop(int dir, ControlContext &ctx) override {
        if (!m_last.active)
            return;
        m_last.at = ctx.now();
        if (m_last.free) {
            const int n = m_last.phrases + dir;
            if (n < 1 || n > int(m_last.starts.size())) {
                ctx.notice(dir > 0 ? QStringLiteral("Loop: every phrase you sang") : QStringLiteral("Loop: one phrase"));
                return;
            }
            Pick pk;
            pk.ok = true;
            pk.b0 = m_last.starts[m_last.starts.size() - size_t(n)] - 0.03;
            pk.b1 = m_last.end;
            pk.phrases = n;
            makeTake(ctx, pk, m_last.slot, -1, true, true);
            return;
        }
        const auto steps = stepsFor(m_last);
        auto it = std::find(steps.begin(), steps.end(), m_last.bars);
        if (it == steps.end())
            return;
        // To the next length whose cuts both fall in silence, if there is one.
        PickRequest req = m_last.req;
        const Sung sung = snapshot(ctx);
        Pick best;
        for (long at = long(it - steps.begin()) + dir; at >= 0 && at < long(steps.size()); at += dir) {
            req.bars = steps[size_t(at)];
            const Pick pk = pick(sung, req);
            if (!pk.ok)
                break;
            if (!best.ok)
                best = pk;
            if (pk.cutDb <= 3.0f) {
                best = pk;
                break;
            }
            if (dir < 0)
                break; // shorter: the next one down, wherever it cuts
        }
        if (!best.ok) {
            ctx.notice(dir > 0 ? QStringLiteral("Loop: as long as it goes") : QStringLiteral("Loop: as short as it goes"));
            return;
        }
        m_last.req.bars = best.bars;
        makeTake(ctx, best, m_last.slot, m_last.into, true, false);
    }

    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("loops"))
            return float(m_loopsMade);
        return PartControl::tutorialProbe(name);
    }

    // For the tests: the last take's choice.
    Pick lastPick() const { return m_last.active ? m_last.pick : Pick{}; }

private:
    struct Finger {
        int id = -1;
        Zone zone = Keys;
        double t0 = 0, lastSend = 0;
        float x = 0, y = 0, p = 0.5f, angle0 = 0, twist = 0;
        int voice = -1; // the engine's voice id (keys)
        int value = 0, mode = 0;
        bool key = false, clicker = false;
        float sent[4] = {-9, -9, -9, -9};
    };
    static constexpr int kKeyFinger = 1 << 20;
    static constexpr double kRetakeSecs = 3.0;
    static constexpr int kTrail = 600;

    Finger *find(int id) {
        for (Finger &h : m_fingers)
            if (h.id == id)
                return &h;
        return nullptr;
    }
    static const char *zoneName(Zone z) {
        switch (z) {
        case Keys: return "VOICE";
        case Filter: return "FILTER";
        case Freeze: return "FREEZE";
        case Echo: return "ECHO";
        case Reverb: return "REVERB";
        case Radio: return "RADIO";
        case Crush: return "CRUSH";
        }
        return "";
    }
    static int homeLane(int key) {
        static const int keys[] = {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
                                   Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L};
        for (int i = 0; i < 9; ++i)
            if (keys[i] == key)
                return i + 3;
        return -1;
    }

    // --- the keys -------------------------------------------------------------------
    Tuning tuningOf(const StudioView &v) const {
        Tuning t;
        t.set(v.root, v.family, v.brightness, v.just);
        return t;
    }
    int laneCount(const StudioView &v) const {
        if (m_follow)
            return std::clamp(v.chordCount, 1, 4) * 2 + 1;
        return tuningOf(v).degreesPerOctave() * 2 + 1;
    }
    // The lowest lane's value: a degree (or chord tone) an octave under the
    // voice, moved by ↑↓.
    int laneBase(const StudioView &v) const {
        const Tuning t = tuningOf(v);
        const int per = m_follow ? std::clamp(v.chordCount, 1, 4) : t.degreesPerOctave();
        const int oct = int(std::lround((m_sungMedian - t.baseMidi() - 12.0) / 12.0)) + m_octave;
        return oct * per;
    }
    Harmony harmonyOf(const StudioView &v) const {
        Harmony h;
        h.tuning = tuningOf(v);
        h.chordRoot = v.chordRoot;
        h.chordCount = v.chordCount;
        for (int i = 0; i < 4; ++i)
            h.chordTones[i] = v.chordTones[i];
        return h;
    }
    double laneMidi(const StudioView &v, int lane) const {
        return noteMidi(harmonyOf(v), laneBase(v) + lane, m_follow ? 1 : 0);
    }
    int laneAt(const StudioView &v, float x) const {
        const int n = laneCount(v);
        return std::clamp(int((x - kLeft) / (kRight - kLeft) * float(n)), 0, n - 1);
    }

    void land(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        h = Finger{};
        h.id = t.id;
        h.t0 = time;
        h.zone = zoneAt(t.x, t.y);
        h.x = t.x;
        h.y = t.y;
        h.p = t.pressure;
        h.angle0 = t.angle;
        if (h.zone == Keys) {
            h.voice = m_nextVoice++;
            if (m_nextVoice > 0xFFFF)
                m_nextVoice = 1;
            const StudioView &v = ctx.studio();
            h.value = laneBase(v) + laneAt(v, t.x);
            h.mode = m_follow ? 1 : 0;
            sendNote(h, Command::On, ctx);
            ++m_notesPlayed;
        } else {
            sendStrip(h, true, ctx);
        }
    }
    void move(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        h.x = t.x;
        h.y = t.y;
        h.p = t.pressure;
        float tw = t.angle - h.angle0;
        while (tw > float(kPi / 2))
            tw -= float(kPi);
        while (tw < -float(kPi / 2))
            tw += float(kPi);
        h.twist = tw;
        if (h.clicker || time - h.lastSend < 1.0 / 60)
            return;
        if (h.zone == Keys) {
            const StudioView &v = ctx.studio();
            const int value = laneBase(v) + laneAt(v, t.x);
            if (value != h.value || changed(h)) {
                h.value = value;
                h.lastSend = time;
                sendNote(h, Command::Move, ctx);
            }
        } else if (changed(h)) {
            h.lastSend = time;
            sendStrip(h, true, ctx);
        }
    }
    bool changed(Finger &h) const {
        const float now[4] = {h.x, h.y, h.p, h.twist};
        for (int i = 0; i < 4; ++i)
            if (std::fabs(now[i] - h.sent[i]) > 0.005f)
                return true;
        return false;
    }
    void lift(Finger &h, ControlContext &ctx) {
        if (h.clicker) {
            h.id = -1;
            return;
        }
        if (h.zone == Keys) {
            if (m_latch && h.voice > 0)
                m_latched.push_back(h.voice);
            else
                release(h, ctx);
        } else {
            sendStrip(h, false, ctx);
        }
        h.id = -1;
    }
    void release(Finger &h, ControlContext &ctx) {
        if (h.voice > 0) {
            Command c;
            c.type = Command::Off;
            c.flags = Command::NoRecord;
            c.id = h.voice;
            ctx.send(c);
            h.voice = -1;
        }
    }
    void sendNote(Finger &h, uint16_t type, ControlContext &ctx) {
        Command c;
        c.type = type;
        c.flags = Command::NoRecord;
        c.id = h.voice;
        c.i[0] = h.value;
        c.i[1] = h.mode;
        const float u = clampf((h.y - kBottom) / (kTop - kBottom), 0, 1);
        c.f[0] = 0.3f + 0.9f * clampf(h.p, 0, 1);
        c.f[1] = u;
        c.f[2] = clampf(h.twist / 0.8f, -1, 1);
        c.f[3] = clampf((h.x - kLeft) / (kRight - kLeft), 0, 1);
        h.sent[0] = h.x, h.sent[1] = h.y, h.sent[2] = h.p, h.sent[3] = h.twist;
        ctx.send(c);
    }
    void sendStrip(Finger &h, bool on, ControlContext &ctx) {
        h.sent[0] = h.x, h.sent[1] = h.y, h.sent[2] = h.p, h.sent[3] = h.twist;
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        const float p = clampf(h.p, 0, 1);
        const float yIn = clampf((h.y - kBottom) / (kTop - kBottom), 0, 1);
        switch (h.zone) {
        case Filter:
            c.i[0] = PFilter;
            c.f[0] = yIn;
            c.f[1] = p;
            break;
        case Freeze:
            c.i[0] = PFreeze;
            c.f[0] = yIn;
            c.f[1] = p;
            c.f[2] = std::round(h.twist / 0.8f * 12.0f);
            c.f[3] = on ? 1 : 0;
            break;
        case Echo:
            c.i[0] = PEcho;
            c.f[0] = clampf(h.x * 2, 0, 0.999f);
            c.f[1] = p;
            break;
        case Reverb:
            c.i[0] = PReverb;
            c.f[0] = clampf((h.x - 0.5f) * 2, 0, 1);
            c.f[1] = p;
            c.f[3] = clampf(0.25f + h.twist / 0.8f, 0, 1);
            break;
        case Radio:
        case Crush:
            c.i[0] = PLofi;
            c.f[0] = clampf(h.x, 0, 1);
            c.f[1] = p;
            break;
        case Keys: return;
        }
        if (h.zone != Freeze)
            c.f[2] = on ? 1 : 0;
        // A strip a second finger still holds stays on.
        if (!on)
            for (Finger &o : m_fingers)
                if (&o != &h && o.id >= 0 && !o.clicker && sameStrip(o.zone, h.zone)) {
                    sendStrip(o, true, ctx);
                    return;
                }
        ctx.send(c);
        if (on)
            ++m_effectsUsed;
    }
    static bool sameStrip(Zone a, Zone b) { return a == b || ((a == Radio || a == Crush) && (b == Radio || b == Crush)); }
    void sendTape(ControlContext &ctx) {
        const bool on = m_button || m_tapeKey;
        if (on == m_tapeSent)
            return;
        m_tapeSent = on;
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = PTape;
        c.f[0] = on ? 1 : 0;
        ctx.send(c);
    }
    void param(ControlContext &ctx, int which, int value, float f = 0) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = which;
        c.i[1] = value;
        c.f[0] = f;
        ctx.send(c);
    }
    void letGoLatched(ControlContext &ctx) {
        for (int v : m_latched) {
            Command c;
            c.type = Command::Off;
            c.flags = Command::NoRecord;
            c.id = v;
            ctx.send(c);
        }
        m_latched.clear();
    }
    void letGo(ControlContext &ctx) {
        letGoLatched(ctx);
        for (Finger &h : m_fingers)
            if (h.id >= 0 && h.zone != Keys)
                sendStrip(h, false, ctx);
        ctx.notice(QStringLiteral("Let go: latched notes, freeze and every strip"));
    }
    void setPreset(int p, ControlContext &ctx) {
        m_preset = p;
        param(ctx, PPreset, p);
        ctx.notice(QStringLiteral("%1: %2").arg(QString::fromLatin1(presetName(p)), QString::fromLatin1(presetLine(p))));
    }
    void setStutter(int s, ControlContext &ctx) {
        m_stutter = std::clamp(s, 0, kStutterCount - 1);
        param(ctx, PStutter, m_stutter);
        ctx.notice(QStringLiteral("Stutter from %1 (faster higher up the edge)").arg(QString::fromLatin1(kStutterNames[m_stutter])));
    }
    void setFormant(int f, ControlContext &ctx) {
        m_formant = std::clamp(f, -6, 6);
        param(ctx, PFormant, m_formant);
        ctx.notice(m_formant == 0 ? QStringLiteral("Your own throat")
                                  : QStringLiteral("Throat %1%2: %3")
                                        .arg(m_formant > 0 ? "+" : "")
                                        .arg(m_formant)
                                        .arg(m_formant < 0 ? QStringLiteral("darker, bigger") : QStringLiteral("brighter, smaller")));
    }
    void setLatch(bool on, ControlContext &ctx) {
        m_latch = on;
        if (!on)
            letGoLatched(ctx);
        ctx.notice(on ? QStringLiteral("Latch: notes stay when you lift") : QStringLiteral("Latch off"));
    }
    void setFollow(bool on, ControlContext &ctx) {
        m_follow = on;
        param(ctx, PFollow, on ? 1 : 0);
        ctx.notice(on ? QStringLiteral("Following the Chords page: the keys are the chord's notes")
                      : QStringLiteral("The keys are the key's notes"));
    }
    void cycleMonitor(ControlContext &ctx) {
        m_monitorMode = (m_monitorMode + 1) % 3;
        static const char *what[] = {"on this page", "on every page", "off (loops still record)"};
        ctx.notice(QStringLiteral("Monitor %1").arg(QString::fromLatin1(what[m_monitorMode])));
    }
    void setMicGain(float db, ControlContext &ctx) {
        m_micDb = clampf(db, -24, 24);
        param(ctx, PMicGain, 0, m_micDb);
        ctx.notice(QStringLiteral("Microphone %1%2 dB").arg(m_micDb > 0 ? "+" : "").arg(double(m_micDb), 0, 'f', 0));
    }

    // --- Space ----------------------------------------------------------------------
    struct LastTake {
        bool active = false, free = false;
        double at = 0;      // when it was made or last changed
        int slot = -1, into = -1, handle = 0, bars = 0, phrases = 0;
        double end = 0;     // free: Space's moment
        std::vector<double> starts; // free: the phrases' starts, oldest first
        PickRequest req;
        Pick pick;
        double base = 0;
    };

    // What was sung, as pick() reads it: the envelope of the last forty
    // seconds, each hop at the moment it was sung (the trip through the
    // audio server taken off).
    Sung snapshot(ControlContext &ctx) const {
        Sung s;
        const StudioView &v = ctx.studio();
        const Tape &tape = *m_tape;
        const double rate = tape.rate;
        s.hop = kHop / rate;
        const int64_t written = tape.written.load(std::memory_order_acquire);
        const int64_t last = written / kHop; // hops before this are complete
        const int64_t first = std::max<int64_t>(0, last - (kEnvRing - 64));
        s.env.resize(size_t(last - first));
        for (int64_t h = first; h < last; ++h)
            s.env[size_t(h - first)] = tape.env[size_t(h & (kEnvRing - 1))];
        const double sampleTime = v.time + (double(first * kHop + kHop / 2) - double(v.sample)) / rate;
        s.t0 = sampleTime - latency(v);
        return s;
    }
    // Seconds from a sound sung to its place on the tape: the server's
    // trip both ways (the singer sings to what they hear) and the studio's
    // own. Offline there is none.
    static double latency(const StudioView &v) {
        if (v.inputMs + v.outputMs <= 0)
            return 0;
        return double(v.inputMs + v.outputMs + v.bufferMs) / 1000.0;
    }
    int64_t sampleAt(const StudioView &v, double t) const {
        return int64_t(std::llround(double(v.sample) + (t + latency(v) - v.time) * m_tape->rate));
    }
    static double freeBeats(double secs) {
        double len = 1;
        while (len * 60.0 / secs < 88.0 && len < 32)
            len *= 2;
        return len;
    }
    static std::vector<int> stepsFor(const LastTake &l) {
        if (l.into >= 0 && l.base > 0) {
            std::vector<int> out;
            for (int b : barSteps())
                if (b <= int(l.base) && int(l.base) % b == 0)
                    out.push_back(b);
            return out;
        }
        return barSteps();
    }

    void space(ControlContext &ctx, bool layer) {
        const int part = ctx.part();
        const StudioView &v = ctx.studio();
        const PartView &pv = v.parts[part];
        if (pv.recording >= 0) {
            ctx.cancelTake();
            m_last = {};
            ctx.notice(QStringLiteral("Loop called off"));
            return;
        }
        if (v.input == StudioView::NoInput) {
            ctx.notice(QStringLiteral("No microphone: nothing to loop"));
            return;
        }
        const int into = layer ? pv.playing : -1;
        if (layer && into < 0)
            ctx.notice(QStringLiteral("No vocal loop playing to layer onto: a new loop instead"));
        const int slot = ctx.freeSlot();
        if (slot < 0) {
            ctx.notice(QStringLiteral("Every slot is full: clear one first (Shift+F1–F8)"));
            return;
        }
        bool anyPlaying = v.metronome;
        for (int q = 0; q < v.partCount; ++q)
            anyPlaying |= v.parts[q].playing >= 0 || v.parts[q].queued >= 0;
        const double now = ctx.now();
        PickRequest req;
        req.free = !anyPlaying;
        req.now = now;
        req.beat = ctx.beatAt(now);
        req.bpm = std::max(1.0, v.bpm);
        // Over the Chords page's loop, the take may grow to cover its
        // progression: the line meets the same chords every time round.
        const PartView &chords = v.parts[kChordsPart];
        if (kChordsPart != part && chords.playing >= 0)
            req.fit = chords.clips[chords.playing].lengthBeats / 4.0;
        if (into >= 0) {
            req.layerBase = pv.clips[into].lengthBeats / 4.0;
            req.since = m_loopSince;
        }
        const Sung sung = snapshot(ctx);
        Pick pk = pick(sung, req);
        if (!pk.ok) {
            ctx.notice(req.free ? QStringLiteral("Sing a phrase first, then Space where it would start again")
                                : QStringLiteral("Sing first, then Space: nothing sung lately"));
            return;
        }
        m_last = {};
        m_last.active = true;
        m_last.req = req;
        m_last.free = req.free;
        m_last.end = now;
        m_last.base = req.layerBase;
        if (req.free) {
            float thr = 0;
            const auto words = findWords(sung, &thr);
            std::vector<Word> heard;
            for (const Word &w : words)
                if (sung.time(w.start) <= now + 0.02)
                    heard.push_back(w);
            for (const Word &w : phrasesOf(heard, int(0.35 / sung.hop)))
                m_last.starts.push_back(sung.time(w.start));
        }
        ++m_loopsMade;
        makeTake(ctx, pk, slot, into, false, req.free);
    }

    // The take's audio gets a handle, the studio a clip of it.
    void makeTake(ControlContext &ctx, const Pick &pk, int slot, int into, bool retake, bool free) {
        const StudioView &v = ctx.studio();
        const int handle = allocHandle();
        if (handle <= 0) {
            ctx.notice(QStringLiteral("The vocal tape is full: Ctrl+N clears it"));
            return;
        }
        Tape::Take &take = m_tape->takes[handle];
        double b0 = pk.b0, b1 = pk.b1;
        int64_t s0, len;
        if (free) {
            // As the studio sizes a loop that sets the tempo: whole BPM.
            const double secs = b1 - b0;
            const double beats = freeBeats(secs);
            const double bpm = std::round(beats * 60.0 / secs);
            const double clip = beats * 60.0 / bpm;
            b0 = b1 - clip;
            const double in = v.inputMs + v.outputMs > 0 ? double(v.inputMs + v.bufferMs) / 1000.0 : 0;
            const int64_t end = int64_t(std::llround(double(v.sample) + (b1 + in - v.time) * m_tape->rate));
            len = int64_t(std::llround(clip * m_tape->rate));
            s0 = end - len;
        } else {
            s0 = sampleAt(v, ctx.timeAt(b0));
            len = int64_t(std::llround((b1 - b0) * 60.0 / std::max(1.0, v.bpm) * m_tape->rate));
        }
        take.data.store(nullptr, std::memory_order_release);
        take.start = s0;
        take.length = int(len);
        m_zeroBefore[handle] = into >= 0 ? sampleAt(v, ctx.timeAt(m_loopSince)) : INT64_MIN;
        m_pending.push_back(handle);
        copyPending();
        Command sound;
        sound.type = Command::On;
        sound.flags = Command::Sampled;
        sound.id = kLoopIdBase + handle;
        sound.i[0] = handle;
        if (retake && m_last.handle > 0)
            m_graveyard.push_back({ctx.now() + 6.0, m_last.handle});
        ctx.takeAudio(slot, b0, b1, sound, into, retake, free);
        m_last.at = ctx.now();
        m_last.slot = slot;
        m_last.into = into;
        m_last.handle = handle;
        m_last.pick = pk;
        m_last.bars = pk.bars;
        m_last.phrases = pk.phrases;
        // Say what was taken, and how it was cut.
        QString what;
        if (free) {
            const int beats = int(freeBeats(b1 - b0));
            what = QStringLiteral("%1 (%2 phrase%3), setting the tempo")
                       .arg(beats < 4 ? QStringLiteral("%1 beats").arg(beats)
                                      : beats == 4 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(beats / 4))
                       .arg(pk.phrases)
                       .arg(pk.phrases == 1 ? "" : "s");
        } else {
            what = pk.bars == 1 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(pk.bars);
            if (std::fabs(pk.delta) > 1e-6)
                what += QStringLiteral(", cut %1 beat %2 the bar")
                            .arg(std::fabs(pk.delta), 0, 'g', 3)
                            .arg(pk.delta < 0 ? QStringLiteral("before") : QStringLiteral("after"));
            what += pk.cutDb <= 3 ? QStringLiteral(" in a breath") : QStringLiteral(" (no silence there: at the quietest)");
        }
        if (into >= 0)
            ctx.notice(QStringLiteral("Layered %1 onto vocal slot %2 · ↑↓ longer, shorter").arg(what).arg(into + 1));
        else
            ctx.notice(QStringLiteral("Vocal loop: %1 in slot %2 · ↑↓ longer, shorter").arg(what).arg(slot + 1));
    }

    int allocHandle() {
        for (int h = 1; h < kMaxTakes; ++h) {
            const int k = (m_nextHandle + h - 1) % (kMaxTakes - 1) + 1;
            if (!m_used[k]) {
                m_used[k] = true;
                m_nextHandle = k + 1;
                return k;
            }
        }
        return -1;
    }
    void freeHandle(int h) {
        if (h <= 0 || h >= kMaxTakes || !m_used[h])
            return;
        m_tape->takes[h].data.store(nullptr, std::memory_order_release);
        m_tape->takes[h].length = 0;
        m_audio[h].clear();
        m_audio[h].shrink_to_fit();
        m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), h), m_pending.end());
        m_used[h] = false;
    }
    // A take whose audio is all on the tape now is copied off it: faded
    // in and out at the cuts (they are in silence; the fades only keep a
    // breath from clicking) and, for a layer, silent before the loop
    // began, so the phrase it was made from isn't doubled.
    void copyPending() {
        const Tape &tape = *m_tape;
        const int64_t written = tape.written.load(std::memory_order_acquire);
        for (size_t i = 0; i < m_pending.size();) {
            const int h = m_pending[i];
            Tape::Take &take = m_tape->takes[h];
            if (take.start + take.length > written) {
                ++i;
                continue;
            }
            std::vector<float> &buf = m_audio[h];
            const int len = take.length;
            buf.assign(size_t(2 * len), 0.0f);
            const int fade = std::max(1, std::min(len / 4, int(0.005 * tape.rate)));
            const int ramp = int(0.02 * tape.rate);
            const int64_t zero = m_zeroBefore[h];
            for (int k = 0; k < len; ++k) {
                const int64_t s = take.start + k;
                if (written - s >= kRing)
                    continue; // gone from the ring already (a take far back)
                float g = 1;
                if (k < fade)
                    g = hann().at(0.5f * float(k) / float(fade));
                else if (len - 1 - k < fade)
                    g = hann().at(0.5f * float(len - 1 - k) / float(fade));
                if (s < zero)
                    g *= clampf(1.0f - float(zero - s) / float(ramp), 0, 1);
                buf[size_t(2 * k)] = tape.wetL[size_t(s & kRingMask)] * g;
                buf[size_t(2 * k + 1)] = tape.wetR[size_t(s & kRingMask)] * g;
            }
            take.data.store(buf.data(), std::memory_order_release);
            m_pending.erase(m_pending.begin() + long(i));
        }
    }

    std::shared_ptr<Tape> m_tape;
    Finger m_fingers[16];
    Frame m_frame;
    bool m_button = false, m_tapeKey = false, m_tapeSent = false;
    int m_nextVoice = 1;
    std::vector<int> m_latched;
    int m_preset = Natural, m_octave = 0, m_formant = 0, m_stutter = 2, m_monitorMode = 0;
    bool m_latch = false, m_follow = false, m_shown = false, m_monitorSent = true, m_sentAll = false;
    float m_micDb = 0;
    float m_sungMedian = 57;
    double m_lastRegister = 0, m_lastTrail = 0, m_now = 0;
    struct TrailPoint {
        double t = -1e9;
        float midi = -1, level = 0;
    };
    TrailPoint m_trail[kTrail];
    int m_trailW = 0;
    double m_loopSince = -1e18;
    uint32_t m_takesSeen = 0;
    int m_playingSeen = -1;
    int m_loopsMade = 0, m_notesPlayed = 0, m_effectsUsed = 0;
    static constexpr int kChordsPart = 2; // the Chords page, third in makeParts()

    LastTake m_last;
    bool m_used[kMaxTakes] = {};
    int m_nextHandle = 1;
    std::vector<float> m_audio[kMaxTakes];
    int64_t m_zeroBefore[kMaxTakes] = {};
    std::vector<int> m_pending;
    std::vector<std::pair<double, int>> m_graveyard;
    console::Hotspots m_spots;
    Pick m_preview;
    bool m_previewFree = false;
    double m_previewAt = -1;
    friend void paintTape(Page &, QPainter &, const PaintContext &);
};

// --- drawing -------------------------------------------------------------------

void Page::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    m_now = pc.time;
    const QColor col = pc.color;
    const StudioView &sv = *pc.studio;
    const float *d = pc.view->data;
    c::pad(p, pc, QStringLiteral("VOCAL"));
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal H = pc.pad.height();

    // No microphone: say so first, big, and why.
    const bool noMic = sv.input == StudioView::NoInput || sv.input == StudioView::InputFailed ||
                       (sv.input == StudioView::InputConnecting && pc.time > 2.0);

    // The strips.
    auto strip = [&](float x0, float y0, float x1, float y1, const QString &label, bool lit, QColor tint) {
        const QRectF r = pc.rect(x0, y0, x1, y1);
        p.fillRect(r, c::alpha(tint, lit ? 0.22f : 0.06f));
        p.setPen(QPen(c::alpha(tint, lit ? 0.8f : 0.25f), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(1, 1, -1, -1));
        if (r.width() > r.height()) {
            c::text(p, r, label, c::alpha(tint, lit ? 1.0f : 0.6f), int(std::clamp(H * 0.026, 9.0, 13.0)), true,
                    Qt::AlignCenter);
        } else {
            p.save();
            p.translate(r.center());
            p.rotate(-90);
            c::text(p, QRectF(-r.height() / 2, -r.width() / 2, r.height(), r.width()), label,
                    c::alpha(tint, lit ? 1.0f : 0.6f), int(std::clamp(H * 0.026, 9.0, 13.0)), true, Qt::AlignCenter);
            p.restore();
        }
    };
    bool lit[7] = {};
    for (const Finger &h : m_fingers)
        if (h.id >= 0 && !h.clicker)
            lit[h.zone] = true;
    const QColor fx = c::cyan(), warm = c::amber(), cold = c::violet();
    strip(0, kTop, 0.5f, 1, QStringLiteral("ECHO THROW   1/8 · 1/8. · 1/4"), lit[Echo], fx);
    strip(0.5f, kTop, 1, 1, QStringLiteral("REVERB THROW   twist: shimmer"), lit[Reverb], fx);
    strip(0, 0, 0.5f, kBottom, QStringLiteral("RADIO   rougher →"), lit[Radio], warm);
    strip(0.5f, 0, 1, kBottom, QStringLiteral("CRUSH   rougher →"), lit[Crush], warm);
    strip(0, kBottom, kLeft, kTop, QStringLiteral("FILTER · WAH"), lit[Filter], cold);
    strip(kRight, kBottom, 1, 0.5f * (kBottom + kTop), QStringLiteral("FREEZE"), lit[Freeze] && d[pub::Freeze] > 0, cold);
    strip(kRight, 0.5f * (kBottom + kTop), 1, kTop,
          QStringLiteral("STUTTER %1+").arg(QString::fromLatin1(kStutterNames[m_stutter])), lit[Freeze], cold);

    // The keys: a lane per note, its name at the foot, the root's lit.
    const int lanes = laneCount(sv);
    const Tuning tuning = tuningOf(sv);
    const float laneW = (kRight - kLeft) / float(lanes);
    const float sungHz = d[pub::Hz];
    const float sung = sungHz > 0 ? midiOfHz(sungHz) : -1.0f;
    for (int k = 0; k < lanes; ++k) {
        const double midi = laneMidi(sv, k);
        const int pc12 = ((int(std::lround(midi)) % 12) + 12) % 12;
        const bool root = pc12 == tuning.root();
        const float x0 = kLeft + laneW * float(k);
        const QRectF r = pc.rect(x0, kTapeTop, x0 + laneW, kTop);
        if (root)
            p.fillRect(r, c::alpha(col, 0.07f));
        if (sung > 0 && std::fabs(sung - float(midi)) < 0.5f)
            p.fillRect(r, c::alpha(col, 0.16f));
        p.setPen(QPen(c::alpha(col, 0.14f), 1));
        p.drawLine(pc.at(x0, kTapeTop), pc.at(x0, kTop));
        const QString name = QString::fromLatin1(Tuning::noteName(pc12));
        c::text(p, pc.rect(x0, kTapeTop, x0 + laneW, kTapeTop + 0.045f), name,
                c::alpha(col, root ? 0.9f : 0.45f), int(std::clamp(H * 0.022, 8.0, 12.0)), root, Qt::AlignCenter);
    }
    // The throat, up the keys: giant at the foot, elf at the top.
    c::text(p, pc.rect(kLeft + 0.005f, kTop - 0.05f, kLeft + 0.2f, kTop - 0.01f), QStringLiteral("ELF ↑"),
            c::alpha(col, 0.35f), 9);
    c::text(p, pc.rect(kLeft + 0.005f, kTapeTop + 0.05f, kLeft + 0.2f, kTapeTop + 0.09f), QStringLiteral("GIANT ↓"),
            c::alpha(col, 0.35f), 9);

    // The note sung: a trail rising up the keys, at its pitch across them.
    auto xOfMidi = [&](float midi) -> float {
        // Between lanes, by pitch: the lanes' own midi values interpolated.
        double lo = laneMidi(sv, 0), hi = laneMidi(sv, lanes - 1);
        if (midi <= lo)
            return kLeft + laneW * 0.5f - float(lo - midi) * laneW * 0.5f;
        for (int k = 0; k + 1 < lanes; ++k) {
            const double a = laneMidi(sv, k), b = laneMidi(sv, k + 1);
            if (midi >= a && midi <= b && b > a)
                return kLeft + laneW * (float(k) + 0.5f + float((midi - a) / (b - a)));
        }
        return kLeft + laneW * (float(lanes) - 0.5f) + float(midi - hi) * laneW * 0.5f;
    };
    {
        const double span = 2.5; // seconds up the keys
        QPainterPath path;
        bool started = false;
        for (int k = 0; k < kTrail; ++k) {
            const TrailPoint &tp = m_trail[(m_trailW + k) % kTrail];
            const double age = pc.time - tp.t;
            if (age < 0 || age > span || tp.midi <= 0) {
                started = false;
                continue;
            }
            const float x = clampf(xOfMidi(tp.midi), kLeft, kRight);
            const float y = kTapeTop + 0.06f + float(age / span) * (kTop - kTapeTop - 0.06f);
            const QPointF at = pc.at(x, y);
            if (!started)
                path.moveTo(at);
            else
                path.lineTo(at);
            started = true;
        }
        p.setPen(QPen(c::alpha(col, 0.55f), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        if (sung > 0) {
            const QPointF at = pc.at(clampf(xOfMidi(sung), kLeft, kRight), kTapeTop + 0.06f);
            p.setPen(Qt::NoPen);
            p.setBrush(c::alpha(col, 0.9f));
            p.drawEllipse(at, 6, 6);
            const double target = d[pub::LeadMidi];
            if ((m_preset == Tune || m_preset == Robot) && target > 0) {
                const QPointF to = pc.at(clampf(xOfMidi(float(target)), kLeft, kRight), kTapeTop + 0.06f);
                p.setPen(QPen(c::alpha(c::green(), 0.9f), 2));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(to, 9, 9);
            }
        }
    }
    // The voices the fingers hold.
    for (int k = 0; k < kMaxVoices; ++k) {
        if (d[pub::Voices + 3 * k] < 0.5f)
            continue;
        const float midi = d[pub::Voices + 3 * k + 1];
        const float x = clampf(xOfMidi(midi), kLeft, kRight);
        p.setPen(QPen(c::alpha(col, 0.35f + 0.6f * clampf(d[pub::Voices + 3 * k + 2], 0, 1)), 3));
        p.drawLine(pc.at(x, kTapeTop + 0.05f), pc.at(x, kTop));
    }

    paintTape(*this, p, pc);

    // The fingers.
    for (const Finger &h : m_fingers) {
        if (h.id < 0 || h.key)
            continue;
        Touch t;
        t.id = h.id;
        t.x = h.x;
        t.y = h.y;
        t.pressure = h.p;
        t.size = 1;
        QString label;
        if (h.clicker)
            label = QStringLiteral("TAPE");
        else if (h.zone == Keys) {
            const int m = int(std::lround(noteMidi(harmonyOf(sv), h.value, h.mode)));
            label = QString::fromLatin1(Tuning::noteName(((m % 12) + 12) % 12));
        }
        else
            label = QString::fromLatin1(zoneName(h.zone));
        c::finger(p, pc, t, h.zone == Keys ? col : c::cyan(), label, clampf(h.p, 0, 1));
    }

    // Tape stop: the pad itself slows.
    if (d[pub::Tape] < 0.999f) {
        p.fillRect(pc.pad, c::alpha(QColor(0, 0, 0), 0.4f * (1 - d[pub::Tape])));
        c::text(p, pc.rect(0, 0.42f, 1, 0.58f), QStringLiteral("TAPE STOP"), c::alpha(c::red(), 0.9f), int(H * 0.08), true,
                Qt::AlignCenter);
    }
    if (d[pub::Feedback] > 0.5f)
        c::tag(p, QPointF(pc.pad.center().x(), pc.at(0, 0.8f).y()),
               QStringLiteral("FEEDBACK HEARD: VOICE DUCKED · USE HEADPHONES"), c::red(), 13, true);
    if (noMic) {
        const QRectF box = pc.rect(0.2f, 0.45f, 0.8f, 0.7f);
        p.fillRect(box, QColor(0, 0, 0, 220));
        p.setPen(QPen(c::red(), 2));
        p.drawRect(box);
        c::text(p, QRectF(box.left(), box.top() + 8, box.width(), box.height() * 0.4), QStringLiteral("NO MICROPHONE"),
                c::red(), int(H * 0.05), true, Qt::AlignCenter);
        const QString why = sv.input == StudioView::InputFailed
                                ? QStringLiteral("PipeWire refused the microphone stream: check the input device")
                            : sv.input == StudioView::InputConnecting
                                ? QStringLiteral("Waiting for an input: plug a mic in, or pick one in the sound settings")
                                : QStringLiteral("This session has no input (offline, or started without one)");
        c::text(p, QRectF(box.left() + 10, box.center().y(), box.width() - 20, box.height() * 0.4), why,
                c::alpha(QColor(255, 255, 255), 0.85f), 12, false, Qt::AlignCenter);
    }
    if (pc.view->recording >= 0)
        c::tag(p, QPointF(pc.pad.left() + 12, pc.at(0, kTop).y() + 14),
               QStringLiteral("VOCAL LOOP INTO SLOT %1 AT THE BAR · SPACE CALLS IT OFF").arg(pc.view->recording + 1),
               c::red(), 11);
    c::beats(p, pc, col);

    // Under the pad: the clips and the voice; then Space's two ways, the
    // monitor, latch, follow, the microphone's level and the latency.
    m_spots.clear();
    c::clips(p, c::bandRow(pc, 0, 0, 0.42), *pc.view, col, pc.time, &m_spots);
    QStringList names;
    for (int k = 0; k < PresetCount; ++k)
        names << QString::fromLatin1(presetName(k));
    c::chips(p, pc, c::bandRow(pc, 0, 0.44, 1), names, m_preset, &m_spots, SpotPreset);
    const QRectF row = c::bandRow(pc, 1, 0, 1);
    const double w = row.width();
    auto at = [&](double x0, double x1) { return QRectF(row.left() + w * x0, row.top(), w * (x1 - x0) - 4, row.height()); };
    c::chip(p, pc, at(0, 0.13), QStringLiteral("NEW LOOP  SPACE"), false, &m_spots, SpotNew);
    c::chip(p, pc, at(0.13, 0.27), QStringLiteral("LAYER  ⇧SPACE"), false, &m_spots, SpotLayer);
    static const char *mon[] = {"MONITOR: PAGE  M", "MONITOR: ALL  M", "MONITOR: OFF  M"};
    c::chip(p, pc, at(0.27, 0.42), QString::fromLatin1(mon[m_monitorMode]), m_monitorMode != 2, &m_spots, SpotMonitor);
    c::chip(p, pc, at(0.42, 0.51), QStringLiteral("LATCH  ↵"), m_latch, &m_spots, SpotLatch);
    c::chip(p, pc, at(0.51, 0.61), QStringLiteral("FOLLOW  §"), m_follow, &m_spots, SpotFollow);
    c::chip(p, pc, at(0.61, 0.655), QStringLiteral("−"), false, &m_spots, SpotGainDown);
    {
        // The microphone's level: a meter, red at the top.
        const QRectF m = at(0.655, 0.81);
        const float pk = d[pub::InPeak];
        const float db = pk > 1e-6f ? 20.0f * std::log10(pk) : -90.0f;
        c::text(p, QRectF(m.left(), m.top(), m.width(), 12),
                QStringLiteral("MIC %1 dB").arg(m_micDb > 0 ? QStringLiteral("+%1").arg(double(m_micDb), 0, 'f', 0)
                                                           : QString::number(double(m_micDb), 'f', 0)),
                c::alpha(col, 0.8f), 9, true);
        c::gauge(p, QRectF(m.left(), m.bottom() - 12, m.width(), 9), clampf((db + 60.0f) / 60.0f, 0, 1),
                 db > -3 ? c::red() : col, 16);
    }
    c::chip(p, pc, at(0.81, 0.855), QStringLiteral("+"), false, &m_spots, SpotGainUp);
    {
        // Latency: the trip from the microphone to the ears, as measured
        // by the server, and the studio's own part of it.
        const QRectF m = at(0.86, 1.0);
        const float total = sv.inputMs + sv.outputMs + sv.bufferMs;
        const QString line = sv.input == StudioView::InputLive && sv.inputMs + sv.outputMs > 0
                                 ? QStringLiteral("%1 ms").arg(double(total), 0, 'f', 1)
                                 : sv.input == StudioView::InputLive ? QStringLiteral("LIVE") : QStringLiteral("—");
        c::text(p, QRectF(m.left(), m.top(), m.width(), 14), QStringLiteral("MIC → EAR"), c::alpha(col, 0.6f), 9, true);
        c::text(p, QRectF(m.left(), m.top() + 12, m.width(), 18), line,
                total > 15 ? c::amber() : col, 13, true);
    }
}

// The tape: the last bars of singing as a waveform under the keys, the bar
// lines over it, and the bracket of what Space would take now.
void paintTape(Page &pg, QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const StudioView &sv = *pc.studio;
    const QRectF area = pc.rect(kLeft, kBottom, kRight, kTapeTop);
    p.fillRect(area, QColor(0, 0, 0, 120));
    p.setPen(QPen(c::alpha(col, 0.25f), 1));
    p.drawLine(area.topLeft(), area.topRight());
    const Tape &tape = *pg.m_tape;
    const int64_t written = tape.written.load(std::memory_order_acquire);
    if (written <= 0 || sv.input == StudioView::NoInput)
        return;
    // Eight bars back with a clock, eight seconds without.
    bool anyPlaying = sv.metronome;
    for (int q = 0; q < sv.partCount; ++q)
        anyPlaying |= sv.parts[q].playing >= 0 || sv.parts[q].queued >= 0;
    const double spb = 60.0 / std::max(1.0, sv.bpm);
    const double span = anyPlaying ? 8 * 4 * spb : 8.0;
    const double rate = tape.rate;
    const double nowT = sv.time + (double(written) - double(sv.sample)) / rate;
    const double lat = Page::latency(sv);
    auto xOf = [&](double t) { return area.left() + area.width() * (1.0 - (nowT - t) / span); };
    // The envelope.
    const int cols = std::max(8, int(area.width() / 2));
    const float mid = float(area.center().y()), half = float(area.height() * 0.44);
    p.setPen(QPen(c::alpha(col, 0.75f), 1.4));
    for (int k = 0; k < cols; ++k) {
        const double t1 = nowT - span * (1.0 - double(k + 1) / cols), t0 = nowT - span * (1.0 - double(k) / cols);
        const int64_t h0 = int64_t((t0 - sv.time) * rate + double(sv.sample)) / kHop,
                      h1 = int64_t((t1 - sv.time) * rate + double(sv.sample)) / kHop;
        float v = 0;
        for (int64_t h = h0; h <= h1; ++h)
            if (h >= 0 && h < written / kHop && written / kHop - h < kEnvRing - 8)
                v = std::max(v, tape.env[size_t(h & (kEnvRing - 1))]);
        const float a = clampf(std::sqrt(v * 6.0f), 0, 1) * half;
        const double x = area.left() + area.width() * (double(k) + 0.5) / cols;
        p.drawLine(QPointF(x, mid - a), QPointF(x, mid + a));
    }
    // Bar lines.
    if (anyPlaying) {
        const double beatNow = sv.beat + (nowT - sv.time) / spb;
        for (double b = std::floor(beatNow / 4) * 4; b > beatNow - 8 * 4 - 4; b -= 4) {
            const double x = xOf(sv.time + (b - sv.beat) * spb + lat);
            if (x < area.left() || x > area.right())
                continue;
            p.setPen(QPen(c::alpha(col, 0.35f), 1, Qt::DashLine));
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
    }
    // What Space would take now (or, just after it, what it took).
    Pick pk;
    bool taken = false;
    if (pg.m_last.active && pc.time - pg.m_last.at < 4.0) {
        pk = pg.m_last.pick;
        taken = true;
        if (pg.m_last.free) {
            pk.b0 = pg.m_last.pick.b0;
            pk.b1 = pg.m_last.end;
        }
    } else if (pc.time - pg.m_previewAt > 0.12 || !pg.m_preview.ok) {
        // Cheap enough several times a second: the tape shown, and a little before.
        Sung s;
        s.hop = kHop / rate;
        const int64_t last = written / kHop, first = std::max<int64_t>(0, last - int64_t(span / s.hop) - 400);
        s.env.resize(size_t(last - first));
        for (int64_t h = first; h < last; ++h)
            s.env[size_t(h - first)] = tape.env[size_t(h & (kEnvRing - 1))];
        s.t0 = sv.time + (double(first * kHop + kHop / 2) - double(sv.sample)) / rate - lat;
        PickRequest req;
        req.free = !anyPlaying;
        req.now = pc.time;
        req.beat = pc.beat;
        req.bpm = std::max(1.0, sv.bpm);
        const PartView &chords = sv.parts[Page::kChordsPart];
        if (chords.playing >= 0)
            req.fit = chords.clips[chords.playing].lengthBeats / 4.0;
        pg.m_preview = pick(s, req);
        pg.m_previewFree = req.free;
        pg.m_previewAt = pc.time;
    }
    if (!taken)
        pk = pg.m_preview;
    if (!pk.ok)
        return;
    const bool free = taken ? pg.m_last.free : pg.m_previewFree;
    const double t0 = free ? pk.b0 : sv.time + (pk.b0 - sv.beat) * spb;
    const double t1 = free ? pk.b1 : sv.time + (pk.b1 - sv.beat) * spb;
    const double x0 = std::max(area.left(), xOf(t0 + lat)), x1 = std::min(area.right(), xOf(t1 + lat));
    if (x1 <= x0)
        return;
    const QColor bc = taken ? c::red() : c::alpha(QColor(255, 255, 255), 0.8f);
    p.fillRect(QRectF(x0, area.top(), x1 - x0, area.height()), c::alpha(bc, taken ? 0.12f : 0.06f));
    p.setPen(QPen(bc, 2));
    p.drawLine(QPointF(x0, area.top()), QPointF(x0, area.bottom()));
    p.drawLine(QPointF(x1, area.top()), QPointF(x1, area.bottom()));
    p.drawLine(QPointF(x0, area.top()), QPointF(x0 + 8, area.top()));
    p.drawLine(QPointF(x0, area.bottom()), QPointF(x0 + 8, area.bottom()));
    p.drawLine(QPointF(x1, area.top()), QPointF(x1 - 8, area.top()));
    p.drawLine(QPointF(x1, area.bottom()), QPointF(x1 - 8, area.bottom()));
    QString what;
    if (free)
        what = QStringLiteral("%1 PHRASE%2 · SETS THE TEMPO").arg(std::max(1, pk.phrases)).arg(pk.phrases > 1 ? "S" : "");
    else
        what = (pk.bars == 1 ? QStringLiteral("1 BAR") : QStringLiteral("%1 BARS").arg(pk.bars)) +
               (std::fabs(pk.delta) > 1e-6 ? QStringLiteral(" · CUT %1%2 BEAT")
                                                 .arg(pk.delta > 0 ? "+" : "−")
                                                 .arg(std::fabs(pk.delta), 0, 'g', 3)
                                           : QString()) +
               (pk.cutDb <= 3 ? QStringLiteral(" · IN THE GAPS") : QStringLiteral(" · NO GAP: QUIETEST"));
    c::tag(p, QPointF(std::max(x0 + 4, area.left() + 4), area.top() + 10),
           (taken ? QStringLiteral("TAKEN: ") : QStringLiteral("SPACE: ")) + what, bc, 10);
}

}

PartSpec makeVocal() {
    PartSpec s;
    s.name = QStringLiteral("Vocal");
    s.blurb = QStringLiteral("your voice, live");
    s.color = QColor("#FF7AB8");
    auto tape = std::make_shared<vocal::Tape>();
    s.engine = std::make_unique<vocal::Engine>(tape);
    s.control = std::make_unique<vocal::Page>(tape);
    s.monitors = true;
    s.followsChords = false;
    // A dry voice up front: a touch of the room, no pump (the singer is
    // the one thing the kick shouldn't push around).
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.12f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
