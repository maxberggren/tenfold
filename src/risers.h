#pragma once

// The FX page's sound: everything a build-up and a drop are made of,
// synthesised. Five risers that peak exactly on a downbeat (a noise sweep, a
// pitch riser, a supersaw stab build, a snare roll and a reverse swell), the
// hits that land on the ONE (impact, sub drop), the moves that come after it
// (downlifter, laser zap, power-down) and three mix moves that have no sound
// of their own (echo throw, reverb wash, the DJ sweep), which the page plays
// on the studio's master bus and the other parts' channels.
//
// The engine keeps the build's one fact: the downbeat it is aiming at, the
// drop. Every riser knows its drop, and when the transport reaches it the
// engine fires the impact and the sub on that exact sample. The page reads
// the drop back from what the engine publishes and moves the mix around it,
// so a build replayed from a clip moves the mix exactly as the live one did.

#include <algorithm>
#include <cmath>

#include "electro.h"
#include "part.h"

namespace tf::risers {

// What a finger or key plays. The first five are the build field's columns,
// the rest the hit row's cells, left to right.
enum Kind : int {
    Noise = 0, // white-noise sweep with a comb that whistles up to the tonic
    Pitch,     // a saw riser gliding two octaves up onto a scale note
    Saw,       // the chord of the moment as supersaw stabs that speed up
    Roll,      // a snare roll: quarters, eighths, sixteenths, thirty-seconds
    Swell,     // a reverse cymbal and reverse chord, sucked into the ONE
    Impact,    // boom, crash and a falling tail: the drop itself
    Sub,       // an 808 sub that dives an octave onto the key's root
    Down,      // a downlifter: noise and pitch falling away
    Zap,       // a laser: a pitch dive onto a chord tone; held, it fires sixteenths
    Stop,      // power-down: the mix slows into darkness until the next ONE
    Throw,     // echo throw: every part into the tape echo, held or for a beat
    Wash,      // reverb wash: every part into a huge shimmering room
    Sweep,     // the DJ filter on the whole mix, left low-pass, right high-pass
    KindCount
};
constexpr int kRisers = 5;
constexpr int kHits = KindCount - kRisers;
inline bool isRiser(int k) { return k >= 0 && k < kRisers; }

inline const char *kindName(int k) {
    static const char *names[KindCount] = {"noise", "pitch", "saw",   "roll",  "swell", "impact", "sub",
                                           "down",  "zap",   "stop", "throw", "wash",  "sweep"};
    return k >= 0 && k < KindCount ? names[k] : "";
}

// Commands beyond On, Off and Move.
//
//   On    i[0] kind, i[1] bars (a riser's length), i[2] flags, f[0] pressure,
//         f[1] tone (a slide sideways, −1..1), f[2] intensity 0..1, f[3]
//         motion (twist) 0..1, f[4] grit (a flat finger) 0..1, f[5] beats from
//         the onset to the drop (risers).
//   Move  f[0]..f[4] as in On.
//   Drop  the drop comes at the next bar line: risers rush into it.
//   Fall  the riser with this id (or every one, id −1) falls away, and a drop
//         no riser aims at any more is called off.
enum : uint16_t { CmdDrop = Command::User, CmdFall = Command::User + 1 };
enum : int32_t { FlagEnterLate = 1 }; // a riser joining a drop enters its length before it

// The numbers the engine publishes for the page.
namespace pub {
constexpr int Drop = 0;       // the pending drop's beat, or −1
constexpr int Start = 1;      // where its build began
constexpr int Intensity = 2;  // the build's intensity
constexpr int Flash = 3;      // 1 on a drop, fading
constexpr int Layers = 4;     // risers aiming at the drop
constexpr int Throw = 5;      // echo throw: send amount 0..1
constexpr int ThrowTail = 6;  // feedback held up while the echoes ring out
constexpr int ThrowFeedback = 7;
constexpr int Wash = 8;
constexpr int WashShimmer = 9;
constexpr int Sweep = 10;     // −1 low-pass … 0 open … 1 high-pass
constexpr int SweepDrive = 11;
constexpr int StopStart = 12; // power-down from this beat …
constexpr int StopEnd = 13;   // … to this one (−1: none)
constexpr int LastDrop = 14;  // the beat of the last drop that fired
constexpr int Hits = 16;      // a flash per hit cell, kHits of them
constexpr int Voices = 32;    // then per voice:
constexpr int Stride = 12;
constexpr int MaxVoices = 18;
// Per voice: kind, start, end, progress, level, intensity, tone, who (1 a
// finger, 0 let go, 2 a clip), bars, state, note (MIDI), motion.
enum Field { VKind, VStart, VEnd, VProgress, VLevel, VIntensity, VTone, VWho, VBars, VState, VNote, VMotion };
static_assert(Voices + Stride * MaxVoices <= kPartData, "fits the page's data");
}

enum State : int { Waiting = 0, Running, Falling, Releasing };

// sin(2π·ph) for ph in [0, 1), a parabola refined once: harmonics below
// −60 dB, which no sub or boom will ever show.
inline float sin01(float ph) {
    const float x = 2.0f * ph - 1.0f;
    float y = 4.0f * x * (1.0f - std::fabs(x));
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}

inline float smoothstep(float u) {
    u = clampf(u, 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}

// The bar line nearest a beat.
inline double nearestBar(double beat) { return std::round(beat / kBeatsPerBar) * kBeatsPerBar; }

// A riser speeds up the way a build does: quarters for its first half, then
// eighths, sixteenths, and thirty-seconds in the last eighth. Long builds
// begin in half notes.
inline double gridFor(float p, int bars) {
    if (bars >= 8 && p < 0.25f)
        return 2.0;
    return p < 0.5f ? 1.0 : p < 0.75f ? 0.5 : p < 0.875f ? 0.25 : 0.125;
}

struct Voice {
    bool active = false;
    int kind = 0, id = -1, bars = 1;
    bool held = false, replay = false;
    int state = Running;
    double start = 0, end = 0; // beats: a riser's run, a hit's onset (and its stop's end)
    double fallAt = 0;
    float fallFrom = 0, fallBeats = 1.5f;
    float p = 0;        // progress, last block
    int delay = 0;      // samples until a hit begins
    float t = 0;        // seconds since a hit began
    double releasedAt = -1;

    // The finger: targets, and the same smoothed per block.
    float press = 0.5f, pressPeak = 0.5f, tone = 0, intensity = 0.6f, motion = 0, grit = 0;
    float sPress = 0.5f, sTone = 0, sInt = 0.6f, sMotion = 0, sGrit = 0;

    float vel = 0.8f; // how hard it began
    float gain = 0;   // the last block's gain, ramped from
    float cut = 1;    // after a riser's peak: a few milliseconds down to nothing
    float level = 0;  // for the page
    float note = 0;   // the pitch it sounds, MIDI, for the page and the tests
    float amount = 0; // a mix move's amount, smoothed

    Rng rng;
    Svf aL, aR, bL, bR, cL, cR;
    Ladder ladL, ladR;
    SawOsc saw[12];
    float ph[8] = {};
    float env = 0, env2 = 0, env3 = 0, env4 = 0, pitchEnv = 0;
    double lastStep = -1;
    int stepCount = 0;
    Delay combL, combR;
    Allpass apL[3], apR[3];
    float glideNote = 0;
};

class Engine : public PartEngine {
public:
    static constexpr int kVoices = 18;

    void init(float rate) override {
        m_rate = rate;
        m_cutDecay = std::exp(-1.0f / (0.004f * rate));
        m_busAttack = 1.0f - std::exp(-1.0f / (0.004f * rate));
        m_busRelease = 1.0f - std::exp(-1.0f / (0.18f * rate));
        for (int i = 0; i < kVoices; ++i) {
            Voice &v = m_voices[i];
            v.rng = Rng(0x7EA5u + uint32_t(i) * 2654435761u);
            v.combL.init(size_t(rate / 30.0f));
            v.combR.init(size_t(rate / 30.0f));
            static const int ap[3] = {347, 113, 37};
            for (int k = 0; k < 3; ++k) {
                v.apL[k].init(uint32_t(ap[k] * rate / 44100.0f), 0.62f);
                v.apR[k].init(uint32_t(ap[k] * 1.17f * rate / 44100.0f), 0.62f);
            }
        }
    }

    void command(const Command &c, AudioContext &ctx) override {
        const Transport &tr = ctx.transport();
        // The beat the command belongs to: a live one arrives a little late
        // and is placed where the finger actually landed; a clip's arrives
        // early and exactly.
        double onset = tr.beat;
        int offset = 0;
        if (c.t > 0) {
            onset = tr.beat + (c.t - ctx.now()) * tr.bpm / 60.0;
            offset = ctx.offsetOf(c.t, kBlock);
        }
        const bool replay = c.id >= (1 << 24);
        switch (c.type) {
        case Command::On: {
            const int kind = std::clamp(c.i[0], 0, int(KindCount) - 1);
            Voice &v = take(c.id);
            begin(v, c, kind, replay);
            if (isRiser(kind)) {
                v.bars = std::clamp(c.i[1], 1, 16);
                // The drop is always a bar line: the command says roughly how
                // far away, and a clip's onsets, pulled to the grid, land on
                // the same one.
                const double drop = nearestBar(onset + std::max(0.5f, c.f[5]));
                v.end = drop;
                v.start = (c.i[2] & FlagEnterLate) ? std::max(onset, drop - 4.0 * v.bars) : onset;
                if (v.end - v.start < 0.25)
                    v.start = v.end - 0.25;
                v.state = v.start > tr.beat ? Waiting : Running;
                addDrop(drop, v.start, v.intensity, false);
            } else {
                v.start = onset;
                v.delay = offset;
                v.state = Running;
                v.end = -1;
                if (kind == Stop)
                    v.end = stopEnd(onset);
                m_hitFlash[kind - kRisers] = 1.0f;
                if (kind == Impact)
                    ctx.pump(offset, 0.9f * v.vel);
            }
            break;
        }
        case Command::Move:
            for (Voice &v : m_voices)
                if (v.active && v.id == c.id && v.held) {
                    v.press = c.f[0];
                    v.pressPeak = std::max(v.pressPeak * 0.9f, c.f[0]);
                    v.tone = c.f[1];
                    v.intensity = clampf(c.f[2], 0, 1);
                    v.motion = clampf(c.f[3], 0, 1);
                    v.grit = clampf(c.f[4], 0, 1);
                }
            break;
        case Command::Off:
            for (Voice &v : m_voices)
                if (v.active && v.id == c.id && v.held) {
                    v.held = false;
                    // A lifting finger's pressure falls away in its last
                    // frames; a riser keeps what the finger was doing.
                    v.press = std::max(v.press, v.pressPeak * 0.85f);
                    v.releasedAt = onset;
                }
            break;
        case CmdDrop:
            drop(onset, tr.beat);
            break;
        case CmdFall:
            for (Voice &v : m_voices)
                if (v.active && isRiser(v.kind) && (c.id < 0 || v.id == c.id) && v.state <= Running)
                    fall(v, tr.beat);
            break;
        default:
            break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        const Transport &tr = ctx.transport();
        const Harmony &h = ctx.harmony();
        const double b0 = tr.beat, bps = tr.beatsPerSample, b1 = b0 + bps * n;
        m_beat = b0;
        m_bpm = tr.bpm;
        const float smooth = 1.0f - std::exp(-float(n) / (0.04f * m_rate));

        // A drop that falls in this block: the impact and the sub, on its
        // sample.
        for (DropPoint &d : m_drops) {
            if (!d.active)
                continue;
            bool aimed = d.explicit_;
            for (const Voice &v : m_voices)
                aimed |= v.active && isRiser(v.kind) && v.state <= Running && std::fabs(v.end - d.beat) < 0.01;
            if (!aimed) {
                d.active = false;
                continue;
            }
            if (d.beat < b1) {
                const int off = std::clamp(int((d.beat - b0) / bps), 0, n - 1);
                fireDrop(d, off, ctx);
                d.active = false;
            }
        }

        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            v.sPress += (v.press - v.sPress) * smooth;
            v.sTone += (v.tone - v.sTone) * smooth;
            v.sInt += (v.intensity - v.sInt) * smooth;
            v.sMotion += (v.motion - v.sMotion) * smooth;
            v.sGrit += (v.grit - v.sGrit) * smooth;
            if (!v.held)
                v.pressPeak *= 0.999f;
            switch (v.kind) {
            case Noise: renderNoise(v, l, r, n, b0, bps, h); break;
            case Pitch: renderPitch(v, l, r, n, b0, bps, h); break;
            case Saw: renderSaw(v, l, r, n, b0, bps, h); break;
            case Roll: renderRoll(v, l, r, n, b0, bps, h); break;
            case Swell: renderSwell(v, l, r, n, b0, bps, h); break;
            case Impact: renderImpact(v, l, r, n, h); break;
            case Sub: renderSub(v, l, r, n, b0, bps, h); break;
            case Down: renderDown(v, l, r, n, b0, bps, h); break;
            case Zap: renderZap(v, l, r, n, b0, bps, h); break;
            case Stop: renderStop(v, l, r, n, b0, bps, h); break;
            case Throw:
            case Wash:
            case Sweep: renderMove(v, n, b0); break;
            default: v.active = false; break;
            }
        }
        // The FX bus: a soft compressor, so that five risers stacked at full
        // pressure are a wall, not five times the level.
        for (int i = 0; i < n; ++i) {
            const float a = std::max(std::fabs(l[i]), std::fabs(r[i]));
            m_busEnv += (a - m_busEnv) * (a > m_busEnv ? m_busAttack : m_busRelease);
            constexpr float threshold = 0.22f;
            const float g = m_busEnv > threshold ? std::pow(threshold / m_busEnv, 0.6f) : 1.0f;
            l[i] *= g;
            r[i] *= g;
        }
        const float fade = std::exp(-float(n) / (0.25f * m_rate));
        m_flash *= fade;
        for (float &f : m_hitFlash)
            f *= fade;
    }

    void silence(bool hard) override {
        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            if (hard) {
                v.active = false;
            } else if (isRiser(v.kind)) {
                fall(v, m_beat);
            } else {
                v.held = false;
                v.releasedAt = m_beat;
            }
        }
        for (DropPoint &d : m_drops)
            d.active = false;
    }

    void publish(float *d) override {
        std::fill(d, d + kPartData, 0.0f);
        const DropPoint *next = nullptr;
        for (const DropPoint &p : m_drops)
            if (p.active && (!next || p.beat < next->beat))
                next = &p;
        d[pub::Drop] = next ? float(next->beat) : -1.0f;
        d[pub::Start] = next ? float(next->start) : 0.0f;
        float intensity = next ? next->intensity : 0.0f;
        int layers = 0;
        d[pub::StopEnd] = -1;
        d[pub::Flash] = m_flash;
        d[pub::LastDrop] = float(m_lastDrop);
        for (int k = 0; k < kHits; ++k)
            d[pub::Hits + k] = m_hitFlash[k];
        int w = 0;
        for (const Voice &v : m_voices) {
            if (!v.active)
                continue;
            if (next && isRiser(v.kind) && v.state <= Running && std::fabs(v.end - next->beat) < 0.01) {
                ++layers;
                intensity = std::max(intensity, v.sInt);
            }
            switch (v.kind) {
            case Throw:
                d[pub::Throw] = std::max(d[pub::Throw], v.amount);
                d[pub::ThrowTail] = std::max(d[pub::ThrowTail], v.env2);
                d[pub::ThrowFeedback] = std::max(d[pub::ThrowFeedback], clampf(0.5f + 0.5f * v.sInt + 0.3f * v.sTone, 0, 1));
                break;
            case Wash:
                d[pub::Wash] = std::max(d[pub::Wash], v.amount);
                d[pub::WashShimmer] = std::max(d[pub::WashShimmer], clampf(0.25f + 0.6f * v.sInt + 0.3f * v.sTone, 0, 1));
                break;
            case Sweep:
                if (v.amount > 0.001f) {
                    d[pub::Sweep] = clampf(v.sTone * 1.6f, -1, 1) * v.amount;
                    d[pub::SweepDrive] = std::max(d[pub::SweepDrive], clampf(v.sInt - 0.5f, 0, 0.5f) * 2.0f * v.amount);
                }
                break;
            case Stop:
                if (v.state != Releasing) {
                    d[pub::StopStart] = float(v.start);
                    d[pub::StopEnd] = float(v.end);
                }
                break;
            default:
                break;
            }
            if (w >= pub::MaxVoices)
                continue;
            float *o = d + pub::Voices + pub::Stride * w++;
            o[pub::VKind] = float(v.kind);
            o[pub::VStart] = float(v.start);
            o[pub::VEnd] = float(v.end);
            o[pub::VProgress] = v.p;
            o[pub::VLevel] = v.level;
            o[pub::VIntensity] = v.sInt;
            o[pub::VTone] = v.sTone;
            o[pub::VWho] = v.replay ? 2.0f : v.held ? 1.0f : 0.0f;
            o[pub::VBars] = float(v.bars);
            o[pub::VState] = float(v.state);
            o[pub::VNote] = v.note;
            o[pub::VMotion] = v.sMotion;
        }
        for (; w < pub::MaxVoices; ++w)
            d[pub::Voices + pub::Stride * w + pub::VKind] = -1;
        d[pub::Intensity] = intensity;
        d[pub::Layers] = float(layers);
    }

    int voices() const override {
        int n = 0;
        for (const Voice &v : m_voices)
            n += v.active;
        return n;
    }

private:
    struct DropPoint {
        bool active = false, explicit_ = false;
        double beat = 0, start = 0;
        float intensity = 0;
    };

    Voice &take(int id) {
        for (Voice &v : m_voices)
            if (v.active && v.id == id && v.held)
                return v;
        for (Voice &v : m_voices)
            if (!v.active)
                return v;
        // Every voice busy: the quietest hit gives way, else the oldest riser.
        Voice *best = nullptr;
        for (Voice &v : m_voices)
            if (!isRiser(v.kind) && (!best || v.level < best->level))
                best = &v;
        if (!best)
            for (Voice &v : m_voices)
                if (!best || v.start < best->start)
                    best = &v;
        return *best;
    }

    void begin(Voice &v, const Command &c, int kind, bool replay) {
        v.active = true;
        v.kind = kind;
        v.id = c.id;
        v.held = true;
        v.replay = replay;
        v.bars = 1;
        v.press = v.sPress = clampf(c.f[0], 0, 1);
        v.pressPeak = v.press;
        v.tone = v.sTone = clampf(c.f[1], -1, 1);
        v.intensity = v.sInt = clampf(c.f[2], 0, 1);
        v.motion = v.sMotion = clampf(c.f[3], 0, 1);
        v.grit = v.sGrit = clampf(c.f[4], 0, 1);
        v.p = 0;
        v.delay = 0;
        v.t = 0;
        v.releasedAt = -1;
        v.gain = 0;
        v.cut = 1;
        v.level = 0;
        v.note = 0;
        v.amount = 0;
        v.env = v.env2 = v.env3 = v.env4 = v.pitchEnv = 0;
        v.lastStep = -1;
        v.stepCount = 0;
        v.glideNote = 0;
        v.aL.reset();
        v.aR.reset();
        v.bL.reset();
        v.bR.reset();
        v.cL.reset();
        v.cR.reset();
        v.ladL.reset();
        v.ladR.reset();
        for (auto &s : v.saw)
            s.phase = v.rng.unit();
        for (float &p : v.ph)
            p = v.rng.unit();
        v.combL.clear();
        v.combR.clear();
        v.vel = 0.55f + 0.9f * v.press;
    }

    void addDrop(double beat, double start, float intensity, bool explicit_) {
        for (DropPoint &d : m_drops)
            if (d.active && std::fabs(d.beat - beat) < 0.01) {
                d.start = std::min(d.start, start);
                d.intensity = std::max(d.intensity, intensity);
                d.explicit_ |= explicit_;
                return;
            }
        DropPoint *slot = &m_drops[0];
        for (DropPoint &d : m_drops)
            if (!d.active) {
                slot = &d;
                break;
            }
        *slot = {true, explicit_, beat, start, intensity};
    }

    // The drop moves to the next bar line (a click a sixteenth late still
    // means the bar line just gone, and drops at once). Risers on their way
    // to a later drop rush into this one from wherever they are.
    void drop(double onset, double now) {
        const double target = std::ceil((onset - 0.25) / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        DropPoint *pending = nullptr;
        for (DropPoint &d : m_drops)
            if (d.active && (!pending || d.beat < pending->beat))
                pending = &d;
        if (pending && pending->beat <= target + 0.01) {
            pending->explicit_ = true;
            return;
        }
        const double from = pending ? pending->beat : -1;
        float intensity = pending ? pending->intensity : 0.7f;
        double start = std::min(onset, now);
        for (Voice &v : m_voices) {
            if (!v.active || !isRiser(v.kind) || v.state > Running || std::fabs(v.end - from) > 0.01)
                continue;
            const double span = std::max(1e-3, target - now);
            if (v.state == Waiting || now <= v.start) {
                v.start = now;
            } else {
                // Keep the progress it has reached and finish the rest in
                // the time that is left.
                const double p0 = std::clamp((now - v.start) / (v.end - v.start), 0.0, 0.97);
                v.start = target - span / (1.0 - p0);
            }
            v.end = target;
            v.state = Running;
            start = std::min(start, v.start);
            intensity = std::max(intensity, v.intensity);
        }
        if (pending)
            pending->active = false;
        // A drop out of nothing gets a reverse swell into it when there is
        // room for one.
        if (!pending && target - now >= 0.75) {
            Voice &v = take(-1);
            Command c;
            c.id = -1;
            c.f[0] = 0.55f;
            c.f[2] = 0.75f;
            begin(v, c, Swell, false);
            v.held = false;
            v.bars = 1;
            v.start = now;
            v.end = target;
            v.state = Running;
        }
        addDrop(target, start, intensity, true);
    }

    void fall(Voice &v, double now) {
        v.fallFrom = v.state == Waiting ? 0.0f : v.p;
        v.fallAt = now;
        v.fallBeats = v.kind == Roll ? 1.0f : 2.0f;
        v.state = v.state == Waiting ? Releasing : Falling;
        if (v.state == Releasing)
            v.active = false;
    }

    // A power-down ends on the next bar line at least two beats away, or on
    // the drop if one is coming sooner.
    double stopEnd(double onset) const {
        double end = std::ceil((onset + 2.0) / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        for (const DropPoint &d : m_drops)
            if (d.active && d.beat > onset + 1.0 && d.beat < end)
                end = d.beat;
        return end;
    }

    void fireDrop(const DropPoint &d, int offset, AudioContext &ctx) {
        m_flash = 1.0f;
        m_lastDrop = d.beat;
        const float intensity = clampf(0.55f + 0.45f * d.intensity, 0.4f, 1.0f);
        const float size = clampf(float(d.beat - d.start) / 16.0f, 0.3f, 1.0f);
        Command c;
        c.id = -2;
        c.f[0] = 0.5f;
        c.f[2] = intensity;
        Voice &boom = take(-2);
        begin(boom, c, Impact, false);
        boom.held = false;
        boom.delay = offset;
        boom.start = d.beat;
        boom.vel = 0.75f + 0.25f * size;
        Voice &sub = take(-3);
        c.id = -3;
        begin(sub, c, Sub, false);
        sub.held = false;
        sub.delay = offset;
        sub.start = d.beat;
        sub.vel = 0.7f + 0.3f * size;
        sub.releasedAt = d.beat;
        m_hitFlash[Impact - kRisers] = m_hitFlash[Sub - kRisers] = 1.0f;
        ctx.pump(offset, 1.0f);
    }

    // --- shared pieces -------------------------------------------------------

    // Progress through a riser at beat b, and what a fall has left of it.
    float progressAt(const Voice &v, double b) const {
        if (v.state == Falling) {
            const float u = clampf(float((b - v.fallAt) / v.fallBeats), 0, 1);
            return v.fallFrom * (1.0f - u) * (1.0f - 0.5f * u);
        }
        if (b <= v.start)
            return 0;
        return clampf(float((b - v.start) / std::max(1e-6, v.end - v.start)), 0, 1);
    }
    float fallFade(const Voice &v, double b) const {
        if (v.state != Falling)
            return 1;
        const float u = clampf(float((b - v.fallAt) / v.fallBeats), 0, 1);
        return (1.0f - u) * (1.0f - u);
    }
    // Whether a riser has anything to play in this block; a waiting one
    // starts when the transport reaches its entry.
    bool riserLive(Voice &v, double b0, double b1) {
        if (v.state == Waiting) {
            if (b1 <= v.start)
                return false;
            v.state = Running;
        }
        if (v.state == Falling && b0 - v.fallAt >= v.fallBeats) {
            v.active = false;
            return false;
        }
        return true;
    }
    // Per sample: after the drop's sample, a riser goes down in a few
    // milliseconds, so its peak is the ONE.
    inline float cutAt(Voice &v, double b) {
        if (v.state == Running && b >= v.end) {
            v.cut *= m_cutDecay;
            if (v.cut < 1e-4f)
                v.active = false;
        }
        return v.cut;
    }
    float pressGain(const Voice &v) const { return 0.55f + 0.9f * v.sPress; }
    static inline float drive(float x, float grit) {
        if (grit < 0.01f)
            return x;
        const float g = 1.0f + 5.0f * grit;
        return fastTanh(x * g) / std::min(g, fastTanh(g) * 1.6f + 0.4f) * (1.0f + 0.3f * grit);
    }
    // The riser's level: −dB·(1−p)^k, so it grows slowly, then all at once.
    static float riseGain(float p, float db, float k) { return dbToGain(-db * std::pow(1.0f - p, k)); }

    // --- risers --------------------------------------------------------------

    void renderNoise(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm), pe = progressAt(v, b0 + bps * n);
        v.p = p;
        const float c = std::pow(p, 1.7f);
        const float I = v.sInt;
        const float lpHz = std::min(10000.0f, 260.0f * std::exp2(c * (5.4f + 1.2f * I) + 0.8f * v.sTone));
        const float hpHz = 55.0f * std::exp2(c * (4.6f + 0.8f * I) + 0.5f * v.sTone);
        const float q = 0.75f + 1.8f * I * c;
        v.aL.set(lpHz, q, m_rate);
        v.aR.set(lpHz * 1.02f, q, m_rate);
        v.bL.set(hpHz, 0.7f, m_rate);
        v.bR.set(hpHz, 0.7f, m_rate);
        // The comb whistles: its delay is the period of a note gliding three
        // octaves up onto the tonic, which it reaches on the ONE.
        const float target = float(h.midi(0, 3)) + 12.0f * std::round(v.sTone * 0.6f);
        const float midi = target - 36.0f * (1.0f - c);
        v.note = midi;
        const float period = m_rate / midiToHz(midi);
        const float fb = clampf(0.3f + 0.45f * I + 0.2f * v.sMotion, 0, 0.93f) * (0.5f + 0.5f * c);
        const float combMix = 0.35f + 0.4f * I;
        const float width = 0.2f + 0.8f * c;
        const float wa = std::sqrt(1.0f - 0.5f * width), wb = std::sqrt(0.5f * width);
        // Twist: an autopan that speeds up with the build, on the grid.
        const double rate = std::exp2(std::floor(c * 3.99f)) * 0.5;
        const float g0 = v.gain;
        const float g1 = riseGain(pe, 34.0f, 1.5f) * pressGain(v) * fallFade(v, b0 + bps * n) * (0.9f + 0.4f * I) * 0.27f;
        v.gain = g1;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * cutAt(v, b);
            const float nc = v.rng.bipolar(), n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            float L = nc * wa + n1 * wb, R = nc * wa + n2 * wb;
            L = v.bL.step(v.aL.step(L).low).high;
            R = v.bR.step(v.aR.step(R).low).high;
            const float cl = L + fb * v.combL.read(period);
            const float cr = R + fb * v.combR.read(period * 1.003f);
            v.combL.push(fastTanh(cl));
            v.combR.push(fastTanh(cr));
            L = L + combMix * (cl * (1.0f - fb) * 1.6f - L);
            R = R + combMix * (cr * (1.0f - fb) * 1.6f - R);
            if (v.sMotion > 0.02f) {
                const float tri = 2.0f * std::fabs(2.0f * float(b * rate - std::floor(b * rate)) - 1.0f) - 1.0f;
                const float pan = tri * v.sMotion;
                L *= 1.0f - std::max(0.0f, pan);
                R *= 1.0f + std::min(0.0f, pan);
            }
            L = drive(L * g, v.sGrit);
            R = drive(R * g, v.sGrit);
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    void renderPitch(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm), pe = progressAt(v, b0 + bps * n);
        v.p = p;
        const float c = std::pow(p, 1.5f);
        const float I = v.sInt;
        // It lands on a scale note: the tonic, or the degree the finger slid
        // to, two octaves above where it began.
        const int degree = int(std::lround(v.sTone * 7.0f));
        const float target = float(h.midi(degree, 2));
        if (v.glideNote == 0)
            v.glideNote = target;
        v.glideNote += (target - v.glideNote) * 0.25f;
        const float fifth = float(h.tuning.degreeSemis(degree + 4) - h.tuning.degreeSemis(degree));
        // A vibrato that speeds up with the build, on the grid: the siren.
        const double vibRate = std::min(12.0, std::exp2(1.0 + 3.0 * c));
        const float vibDepth = (0.06f + 0.5f * v.sMotion) * c;
        const float vib = vibDepth * sin01(float(bm * vibRate - std::floor(bm * vibRate)));
        const float midi = v.glideNote - 24.0f * (1.0f - c) + vib;
        v.note = midi;
        const float hz = midiToHz(midi), hz5 = midiToHz(midi + fifth - 12.0f);
        const float cents = 5.0f + 18.0f * c;
        const float d = std::exp2(cents / 1200.0f);
        const float inc[5] = {std::min(hz / m_rate, 0.45f), std::min(hz * d / m_rate, 0.45f),
                              std::min(hz / d / m_rate, 0.45f), std::min(hz5 * d / m_rate, 0.45f),
                              std::min(hz5 / d / m_rate, 0.45f)};
        const float cutoff = std::min(16000.0f, hz * (1.5f + (3.0f + 9.0f * I) * c));
        v.aL.set(cutoff, 0.8f + 1.4f * I, m_rate);
        v.aR.set(cutoff, 0.8f + 1.4f * I, m_rate);
        const float g0 = v.gain;
        const float g1 = riseGain(pe, 30.0f, 1.4f) * pressGain(v) * fallFade(v, b0 + bps * n) * (0.85f + 0.3f * I) * 0.16f;
        v.gain = g1;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * cutAt(v, b);
            const float s0 = v.saw[0].step(inc[0]), s1 = v.saw[1].step(inc[1]), s2 = v.saw[2].step(inc[2]);
            const float s3 = v.saw[3].step(inc[3]), s4 = v.saw[4].step(inc[4]);
            float L = s0 + 1.2f * s1 + 0.45f * s3 + 0.15f * s4;
            float R = s0 + 1.2f * s2 + 0.15f * s3 + 0.45f * s4;
            L = drive(v.aL.step(L).low * g, v.sGrit);
            R = drive(v.aR.step(R).low * g, v.sGrit);
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    void renderSaw(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm), pe = progressAt(v, b0 + bps * n);
        v.p = p;
        const float c = std::pow(p, 1.4f);
        const float I = v.sInt;
        // The chord of the moment, turned over by the finger's slide.
        const int count = std::clamp(h.chordCount, 1, 3);
        const int inv = int(std::lround(v.sTone * 3.0f));
        float midis[3];
        for (int k = 0; k < count; ++k) {
            const int j = k + inv;
            const int lift = j >= 0 ? j / count : -((count - 1 - j) / count);
            const int idx = j - lift * count;
            midis[k] = float(h.midi(h.chordTones[idx], 1)) + 12.0f * float(lift);
        }
        // The last beat rises an octave, landing on the chord again.
        const double riseBeats = v.bars >= 2 ? 1.0 : 0.5;
        const float u = v.state == Running ? clampf(float(1.0 - (v.end - bm) / riseBeats), 0, 1) : 0.0f;
        const float lift = 12.0f * u * u;
        v.note = midis[0] + lift;
        const float cents = 6.0f + 22.0f * c;
        static const float spread[3] = {0.0f, -1.0f, 1.0f};
        float inc[9];
        for (int k = 0; k < count; ++k)
            for (int u3 = 0; u3 < 3; ++u3)
                inc[3 * k + u3] = std::min(midiToHz(midis[k] + lift + spread[u3] * cents * 0.01f) / m_rate, 0.45f);
        const float subInc = std::min(midiToHz(midis[0] + lift - 12.0f) / m_rate, 0.45f);
        const float baseCut = 160.0f * std::exp2(c * (5.2f + 1.2f * I));
        const float res = 0.2f + 0.35f * I;
        const double grid = gridFor(p, v.bars);
        const float stabTau = float(0.5 * grid * 60.0 / m_bpm);
        const float stabDecay = std::exp(-1.0f / (std::max(0.01f, stabTau) * m_rate));
        const float depth = clampf(0.85f - 0.7f * v.sMotion, 0, 1);
        const float g0 = v.gain;
        const float g1 = riseGain(pe, 26.0f, 1.3f) * pressGain(v) * fallFade(v, b0 + bps * n) * (0.85f + 0.3f * I) * 0.3f /
                         std::sqrt(float(count));
        v.gain = g1;
        float peak = 0;
        const float panW = 0.3f + 0.6f * c;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const double step = std::floor(b / grid);
            if (step != v.lastStep && v.state == Running && b < v.end) {
                v.lastStep = step;
                v.env = 1.0f;
            }
            v.env *= stabDecay;
            if ((i & 7) == 0) {
                const float fc = std::min(15000.0f, baseCut * (1.0f + 2.0f * v.env));
                v.ladL.set(fc, res, m_rate);
                v.ladR.set(fc * 1.01f, res, m_rate);
            }
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * cutAt(v, b) * (1.0f - depth + depth * v.env);
            float L = 0, R = 0;
            for (int k = 0; k < count; ++k) {
                const float a = v.saw[3 * k].step(inc[3 * k]);
                const float bL = v.saw[3 * k + 1].step(inc[3 * k + 1]);
                const float bR = v.saw[3 * k + 2].step(inc[3 * k + 2]);
                L += a + bL * (1.0f + panW) + bR * (1.0f - panW);
                R += a + bR * (1.0f + panW) + bL * (1.0f - panW);
            }
            const float sb = v.saw[9].step(subInc) * 0.8f;
            L = drive(v.ladL.step((L + sb) * 0.5f) * g * 2.0f, v.sGrit);
            R = drive(v.ladR.step((R + sb) * 0.5f) * g * 2.0f, v.sGrit);
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    void renderRoll(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n;
        const float p = progressAt(v, bm);
        v.p = p;
        const float c = std::pow(p, 1.3f);
        const float I = v.sInt;
        // The drum's body climbs the scale as the roll speeds up: five steps
        // up from the degree the finger slid to.
        const int degree = int(std::floor(p * 5.0f)) + int(std::lround(v.sTone * 4.0f));
        const float midi = float(h.midi(degree, 1));
        v.note = midi;
        const float hz = midiToHz(midi);
        const float bodyInc = hz / m_rate;
        v.aL.set(3200.0f * (1.0f + 0.8f * c), 0.8f, m_rate); // the wires
        v.bL.set(1100.0f, 0.7f, m_rate);
        v.cL.set(90.0f * std::exp2(2.6f * c), 0.7f, m_rate); // a rising high-pass thins it out
        v.cR.set(90.0f * std::exp2(2.6f * c), 0.7f, m_rate);
        v.bR.set(1250.0f, 1.4f, m_rate); // the clap layer's band
        const float noiseDecay = std::exp(-1.0f / ((0.07f - 0.02f * c) * m_rate));
        const float toneDecay = std::exp(-1.0f / (0.05f * m_rate));
        const float clapDecay = std::exp(-1.0f / (0.04f * m_rate));
        const float pitchDecay = std::exp(-1.0f / (0.012f * m_rate));
        const double grid = gridFor(p, v.bars);
        const float fade = fallFade(v, bm);
        const float master = dbToGain(-14.0f * (1.0f - c)) * (0.7f + 0.5f * I) * fade * 0.55f * pressGain(v);
        const float width = 0.15f + 0.6f * v.sMotion;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const double step = std::floor(b / grid + 1e-9);
            if (step != v.lastStep && (v.state == Running || v.state == Falling) && b < v.end - 1e-9 && b >= v.start - 1e-9) {
                v.lastStep = step;
                ++v.stepCount;
                // Crescendo, the beat a little louder than the notes between.
                const double onBeat = std::fabs(b - std::round(b)) < 0.01 ? 1.0 : 0.82;
                const float vel = float(onBeat) * (0.35f + 0.65f * std::pow(p, 0.8f));
                v.env = vel;
                v.env2 = vel;
                v.pitchEnv = 1.0f;
                if (I > 0.45f && (v.stepCount & 1))
                    v.env3 = vel * (I - 0.45f) * 1.6f;
                v.env4 = (v.stepCount & 1) ? 1.0f : -1.0f;
            }
            const float g = master * cutAt(v, b);
            v.pitchEnv *= pitchDecay;
            v.ph[0] += bodyInc * (1.0f + 0.6f * v.pitchEnv);
            v.ph[0] -= std::floor(v.ph[0]);
            v.ph[1] += bodyInc * 1.52f;
            v.ph[1] -= std::floor(v.ph[1]);
            const float tone = (sin01(v.ph[0]) + 0.45f * sin01(v.ph[1])) * v.env2;
            const float nz = v.rng.bipolar();
            const float wires = (v.aL.step(nz).band * 1.3f + v.bL.step(nz).high * 0.5f) * v.env;
            const float clap = v.bR.step(nz).band * 2.2f * v.env3;
            v.env *= noiseDecay;
            v.env2 *= toneDecay;
            v.env3 *= clapDecay;
            const float s = 0.6f * tone + 0.9f * wires;
            const float pan = v.env4 * width;
            float L = (s * (1.0f - pan) + clap) * g;
            float R = (s * (1.0f + pan) + clap) * g;
            L = v.cL.step(L).high;
            R = v.cR.step(R).high;
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.state == Running && b0 >= v.end + 0.5)
            v.active = false;
    }

    void renderSwell(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        if (!riserLive(v, b0, b0 + bps * n))
            return;
        const double bm = b0 + 0.5 * bps * n, be = b0 + bps * n;
        v.p = progressAt(v, bm);
        const float I = v.sInt;
        // Reversed decay: the closer the ONE, the louder, as a cymbal or a
        // chord played backwards.
        const float len = float(std::max(1.0, v.end - v.start));
        const float tau = 0.5f + 0.09f * len;
        auto envAt = [&](double b) {
            if (v.state == Falling)
                return std::exp(-float(v.end - v.fallAt) / tau) * fallFade(v, b);
            return std::exp(-float(std::max(0.0, v.end - b)) / tau);
        };
        const float e0 = v.gain, e1 = envAt(be) * pressGain(v) * 0.24f;
        v.gain = e1;
        const int count = std::clamp(h.chordCount, 1, 3);
        const int shift = int(std::lround(v.sTone * 3.0f));
        float inc[6];
        for (int k = 0; k < count; ++k) {
            const float m = float(h.midi(h.chordTones[k] + 0, 1)) + 12.0f * float(shift > k) - 12.0f * float(-shift > k);
            if (k == 0)
                v.note = m;
            inc[2 * k] = midiToHz(m + 0.07f) / m_rate;
            inc[2 * k + 1] = midiToHz(m - 0.07f) / m_rate;
        }
        const float cut = 500.0f * std::exp2(4.6f * envAt(bm));
        v.aL.set(cut, 0.9f, m_rate);
        v.aR.set(cut * 1.05f, 0.9f, m_rate);
        v.bL.set(4200.0f, 0.7f, m_rate);
        v.bR.set(4600.0f, 0.7f, m_rate);
        v.cL.set(7500.0f, 1.2f, m_rate);
        static const float metal[6] = {349.0f, 517.5f, 628.3f, 888.6f, 918.0f, 1360.0f};
        const float cym = 0.45f + 0.55f * I, pad = 0.9f - 0.4f * I;
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const double b = b0 + bps * i;
            const float g = (e0 + (e1 - e0) * float(i) / float(n)) * cutAt(v, b);
            float m = 0;
            for (int k = 0; k < 6; ++k) {
                v.ph[k] += metal[k] / m_rate;
                v.ph[k] -= std::floor(v.ph[k]);
                m += v.ph[k] < 0.5f ? 1.0f : -1.0f;
            }
            const float n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            const float metalBand = v.cL.step(m * 0.12f).band;
            const float cL = (v.bL.step(n1).high + metalBand) * cym;
            const float cR = (v.bR.step(n2).high + metalBand) * cym;
            float sl = 0, sr = 0;
            for (int k = 0; k < count; ++k) {
                sl += v.saw[2 * k].step(inc[2 * k]);
                sr += v.saw[2 * k + 1].step(inc[2 * k + 1]);
            }
            sl = v.aL.step(sl * pad * 0.5f).low;
            sr = v.aR.step(sr * pad * 0.5f).low;
            float L = (cL + sl) * g, R = (cR + sr) * g;
            // A smear of diffusion: it sounds like a reverb run backwards.
            for (int k = 0; k < 3; ++k) {
                L = v.apL[k].step(L);
                R = v.apR[k].step(R);
            }
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
        }
        v.level = v.level * 0.8f + 0.2f * peak;
    }

    // --- hits ----------------------------------------------------------------

    // The key's root as a low boom, never under 40 Hz.
    static float rootHz(const Harmony &h, float atLeast) {
        float hz = midiToHz(h.midi(0, -1));
        while (hz < atLeast)
            hz *= 2.0f;
        return hz;
    }

    // Splits a block around a hit's start: returns the first sample to play.
    static int startOf(Voice &v, int n) {
        if (v.delay <= 0)
            return 0;
        if (v.delay >= n) {
            v.delay -= n;
            return n;
        }
        const int from = v.delay;
        v.delay = 0;
        return from;
    }

    void renderImpact(Voice &v, float *l, float *r, int n, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const float I = v.sInt;
        const float boomHz = rootHz(h, 40.0f);
        v.note = float(69.0 + 12.0 * std::log2(boomHz / 440.0));
        if (v.t == 0) {
            v.env = 1;    // boom
            v.env2 = 1;   // thud
            v.env3 = 1;   // crash
            v.env4 = 1;   // tail
            v.pitchEnv = 1;
            v.aL.set(1800.0f, 0.7f, m_rate);
            v.bL.set(3600.0f, 0.7f, m_rate);
            v.bR.set(3900.0f, 0.7f, m_rate);
        }
        const float dt = 1.0f / m_rate;
        const float boomDecay = std::exp(-dt / (0.45f + 0.7f * I));
        const float thudDecay = std::exp(-dt / 0.07f);
        const float crashDecay = std::exp(-dt / (0.9f + 1.4f * I));
        const float tailDecay = std::exp(-dt / 1.1f);
        const float pitchDecay = std::exp(-dt / 0.035f);
        // The tail sweeps down from bright to dark over a couple of beats.
        const float tailHz = 9000.0f * std::exp2(-v.t * 2.2f * float(m_bpm) / 120.0f);
        v.cL.set(std::max(250.0f, tailHz), 2.2f, m_rate);
        v.cR.set(std::max(250.0f, tailHz * 1.1f), 2.2f, m_rate);
        const float vel = v.vel * (0.75f + 0.35f * I) * pressGain(v);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            v.pitchEnv *= pitchDecay;
            v.ph[0] += boomHz * (1.0f + 3.0f * v.pitchEnv) / m_rate;
            v.ph[0] -= std::floor(v.ph[0]);
            const float attack = std::min(1.0f, v.t * 1500.0f);
            const float boom = fastTanh(1.8f * sin01(v.ph[0])) * v.env * attack;
            const float n0 = v.rng.bipolar(), n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            const float thud = v.aL.step(n0).low * v.env2 * 1.6f;
            const float cl = v.bL.step(n1).high * v.env3, cr = v.bR.step(n2).high * v.env3;
            const float tl = v.cL.step(n1).band * v.env4, tr = v.cR.step(n2).band * v.env4;
            v.env *= boomDecay;
            v.env2 *= thudDecay;
            v.env3 *= crashDecay;
            v.env4 *= tailDecay;
            const float mono = 0.5f * boom + 0.3f * thud;
            const float L = (mono + 0.28f * cl + 0.3f * tl) * vel;
            const float R = (mono + 0.28f * cr + 0.3f * tr) * vel;
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.env < 1e-4f && v.env3 < 1e-4f && v.env4 < 1e-4f)
            v.active = false;
    }

    void renderSub(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const float I = v.sInt;
        // Down an octave, onto the root (or the degree slid to), in about a
        // beat; the deeper the finger sits in the cell, the longer.
        const int degree = int(std::lround(v.sTone * 4.0f));
        float endHz = midiToHz(h.midi(degree, -1));
        while (endHz < 36.0f)
            endHz *= 2.0f;
        const float diveSec = float((0.6 + 0.8 * I) * 60.0 / m_bpm);
        const float u = clampf(v.t / diveSec, 0, 1);
        const float semis = 12.0f * (1.0f - u) * (1.0f - u);
        const float hz = endHz * std::exp2(semis / 12.0f);
        v.note = float(69.0 + 12.0 * std::log2(hz / 440.0));
        const bool sustaining = v.held || (v.releasedAt >= 0 && b0 < v.releasedAt) || v.t < diveSec;
        const float decay = std::exp(-1.0f / ((sustaining ? 6.0f : 0.5f + 1.2f * I) * m_rate));
        if (v.t == 0)
            v.env = 1.0f;
        const float vel = v.vel * pressGain(v) * 0.4f;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            v.ph[0] += hz / m_rate;
            v.ph[0] -= std::floor(v.ph[0]);
            const float attack = std::min(1.0f, v.t * 400.0f);
            const float s = sin01(v.ph[0]);
            const float o = fastTanh(1.6f * s + 0.25f * s * s) * v.env * attack * vel;
            v.env *= decay;
            l[i] += o;
            r[i] += o;
            peak = std::max(peak, 2.0f * std::fabs(o));
            v.t += 1.0f / m_rate;
        }
        (void)bps;
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.env < 2e-4f)
            v.active = false;
    }

    void renderDown(Voice &v, float *l, float *r, int n, double, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        // Four beats from bright to dark; held, it sinks most of the way and
        // waits there for the finger to let go.
        const float len = 4.0f + 2.0f * v.sInt;
        v.env3 += float(bps * n) / len;
        if (v.held)
            v.env3 = std::min(v.env3, 0.85f);
        const float u = clampf(v.env3, 0, 1);
        v.p = 1.0f - u;
        const bool done = !v.held && v.env3 > 1.0f + 0.5f / len;
        const float c = 1.0f - std::pow(1.0f - u, 1.6f);
        const float lp = std::max(180.0f, 15000.0f * std::exp2(-c * 6.0f));
        v.aL.set(lp, 1.6f, m_rate);
        v.aR.set(lp * 1.03f, 1.6f, m_rate);
        v.bL.set(140.0f, 0.7f, m_rate);
        v.bR.set(140.0f, 0.7f, m_rate);
        const float target = float(h.midi(int(std::lround(v.sTone * 4.0f)), 0));
        const float midi = target + 36.0f * (1.0f - c);
        v.note = midi;
        const float inc = std::min(midiToHz(midi) / m_rate, 0.45f), inc2 = std::min(midiToHz(midi + 0.1f) / m_rate, 0.45f);
        const float gEnd = dbToGain(-30.0f * c) * (u >= 0.999f && !v.held ? 0.0f : 1.0f);
        const float g0 = v.gain, g1 = gEnd * pressGain(v) * (0.6f + 0.5f * v.sInt) * 0.3f;
        v.gain = g1;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const float attack = std::min(1.0f, v.t * 200.0f);
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * attack;
            const float n1 = v.rng.bipolar(), n2 = v.rng.bipolar();
            const float s1 = v.saw[0].step(inc), s2 = v.saw[1].step(inc2);
            const float L = v.bL.step(v.aL.step(n1 + 0.25f * s1).low).high * g;
            const float R = v.bR.step(v.aR.step(n2 + 0.25f * s2).low).high * g;
            l[i] += L;
            r[i] += R;
            peak = std::max(peak, std::fabs(L) + std::fabs(R));
            v.t += 1.0f / m_rate;
        }
        (void)bps;
        v.level = v.level * 0.8f + 0.2f * peak;
        if (done || (u >= 0.999f && !v.held && v.gain < 1e-4f))
            v.active = false;
    }

    void renderZap(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        const float I = v.sInt;
        const int count = std::clamp(h.chordCount, 1, 4);
        const int base = int(std::lround(v.sTone * 7.0f));
        const float fall = 0.025f + 0.11f * I; // seconds to fall onto the note
        const float decay = std::exp(-1.0f / ((0.07f + 0.22f * I) * m_rate));
        const float pitchDecay = std::exp(-1.0f / (fall * m_rate));
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const double b = b0 + bps * i;
            // Held, it fires again on every sixteenth, walking up the chord.
            const double step = std::floor(b * 4.0 + 1e-9);
            if (v.t == 0 || (v.held && step != v.lastStep && v.t > 0.05f)) {
                v.lastStep = step;
                const int k = v.stepCount++;
                const int tone = h.chordTones[k % count] + base;
                v.glideNote = float(h.midi(tone, 2 + (k / count) % 2));
                v.env = 1.0f;
                v.pitchEnv = 1.0f;
                v.env4 = (k & 1) ? 0.5f : -0.5f;
            }
            v.pitchEnv *= pitchDecay;
            const float midi = v.glideNote + 40.0f * v.pitchEnv * v.pitchEnv;
            v.note = midi;
            const float hz = midiToHz(midi);
            v.ph[0] += hz / m_rate;
            v.ph[0] -= std::floor(v.ph[0]);
            v.ph[1] += 2.0f * hz / m_rate;
            v.ph[1] -= std::floor(v.ph[1]);
            const float mod = sin01(v.ph[1]) * (0.8f * v.pitchEnv + 0.15f);
            float ph = v.ph[0] + 0.25f * mod;
            ph -= std::floor(ph);
            const float s = sin01(ph) + 0.25f * (v.ph[0] < 0.5f ? 1.0f : -1.0f) * v.pitchEnv;
            const float o = s * v.env * v.vel * 0.15f * (0.8f + 0.4f * I);
            v.env *= decay;
            const float pan = v.env4;
            l[i] += o * (1.0f - pan);
            r[i] += o * (1.0f + pan);
            peak = std::max(peak, 2.0f * std::fabs(o));
            v.t += 1.0f / m_rate;
        }
        v.level = v.level * 0.8f + 0.2f * peak;
        if (!v.held && v.env < 1e-4f)
            v.active = false;
    }

    void renderStop(Voice &v, float *l, float *r, int n, double b0, double bps, const Harmony &h) {
        const int from = startOf(v, n);
        if (from >= n)
            return;
        // Held past its bar line, the power-down waits for the next one.
        if (v.held && b0 >= v.end - 0.5)
            v.end += kBeatsPerBar;
        if (v.state != Releasing && b0 >= v.end) {
            v.state = Releasing;
        }
        const float u = clampf(float((b0 - v.start) / std::max(0.25, v.end - v.start)), 0, 1);
        v.p = u;
        // The sound of a deck winding down: the key's root sliding down
        // three octaves, darkening as it goes.
        const float midi = float(h.midi(int(std::lround(v.sTone * 4.0f)), 1)) - 36.0f * u * u;
        v.note = midi;
        const float hz = midiToHz(midi);
        v.aL.set(std::max(80.0f, 5000.0f * std::exp2(-5.0f * u)), 0.9f, m_rate);
        const float g0 = v.gain;
        const float g1 = v.state == Releasing ? 0.0f : (1.0f - u * u) * 0.12f * pressGain(v) * (0.7f + 0.5f * v.sInt);
        v.gain = g1;
        const float inc = std::min(hz / m_rate, 0.45f), inc2 = std::min(hz * 0.5f / m_rate, 0.45f);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const float g = (g0 + (g1 - g0) * float(i) / float(n)) * std::min(1.0f, v.t * 300.0f);
            const float s = v.aL.step(v.saw[0].step(inc) + 0.7f * v.saw[1].step(inc2)).low * g;
            l[i] += s;
            r[i] += s;
            peak = std::max(peak, 2.0f * std::fabs(s));
            v.t += 1.0f / m_rate;
        }
        (void)bps;
        v.level = v.level * 0.8f + 0.2f * peak;
        if (v.state == Releasing && v.gain <= 0 && b0 > v.end + 0.25)
            v.active = false;
    }

    // Throw, wash and sweep make no sound: they move the mix, which the page
    // does from the amounts published here.
    void renderMove(Voice &v, int n, double b0) {
        const float dt = float(n) / m_rate;
        const float spb = float(60.0 / m_bpm);
        if (v.t == 0)
            v.env2 = 0;
        v.t += dt;
        const float since = float(b0 - v.start);
        float target = 0;
        switch (v.kind) {
        case Throw:
            // A tap throws one beat into the echo; a hold, as long as it is
            // held. The feedback stays up for two beats after, so the echoes
            // ring out.
            target = (v.held || since < 1.0f) ? 1.0f : 0.0f;
            v.amount = target > 0 ? 1.0f : std::max(0.0f, v.amount - dt / (0.0625f * spb));
            v.env2 = target > 0 ? 1.0f : std::max(0.0f, v.env2 - dt / (3.0f * spb));
            if (!v.held && v.amount <= 0 && v.env2 <= 0)
                v.active = false;
            break;
        case Wash: {
            target = (v.held || since < 2.0f) ? 1.0f : 0.0f;
            const float rise = dt / (0.25f * spb), fallRate = dt / (2.0f * spb);
            v.amount = target > v.amount ? std::min(1.0f, v.amount + rise) : std::max(0.0f, v.amount - fallRate);
            if (!v.held && since >= 2.0f && v.amount <= 0)
                v.active = false;
            break;
        }
        case Sweep:
            target = v.held ? 1.0f : 0.0f;
            v.amount = v.held ? 1.0f : std::max(0.0f, v.amount - dt / (0.25f * spb));
            if (!v.held && v.amount <= 0)
                v.active = false;
            break;
        default:
            break;
        }
        v.p = v.amount;
        v.level = v.amount * 0.5f;
    }

    float m_rate = 48000;
    float m_cutDecay = 0.99f;
    float m_busEnv = 0, m_busAttack = 0.005f, m_busRelease = 0.0001f;
    double m_beat = 0, m_bpm = 124, m_lastDrop = -1;
    float m_flash = 0;
    float m_hitFlash[kHits] = {};
    Voice m_voices[kVoices];
    DropPoint m_drops[4];
};

}
