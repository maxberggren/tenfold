// Lead: the melody, played the way a lead player plays it.
//
// The right hand plays a neck of the scale: one finger is one singing note,
// and a second finger landing while the first is held is legato, a hammer-on
// that glides there without a new attack; lifting it pulls back off to the
// note still held. So two fingers trill, three or four run. Under each
// finger: slide to glide, wiggle for vibrato, height for brightness (or the
// vowel), pressure to swell, a flatter finger for a fatter sound, a twist to
// bend up or down a step, and a click of the pad to leap an octave.
//
// The left hand works the column beside the neck: fingers on the harmony
// rows add voices a third, fifth, sixth or octave away that move with the
// melody in the key (or on the chord's own notes when following it), a
// finger on BEND is a pitch wheel that rests on whole steps, and a finger on
// THROW throws the line into its echo.
//
// The engine plays one mono line per source (the live player and each clip),
// so a looped melody and a live one never steal each other's legato.

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <array>
#include <cmath>

#include "console.h"
#include "electro.h"
#include "lead.h"
#include "parts.h"
#include "phrase.h"

namespace tf {
namespace lead {

namespace {

// A sine table: FM wants several sines per sample, and a table read costs a
// fraction of std::sin. 2048 points with linear interpolation is about
// −90 dB of error, far under anything the oscillators make.
struct SineTable {
    static constexpr int N = 2048;
    float v[N + 1];
    SineTable() {
        for (int i = 0; i <= N; ++i)
            v[i] = float(std::sin(2.0 * 3.14159265358979323846 * i / N));
    }
    // phase in cycles, any value.
    inline float operator()(float phase) const {
        phase -= std::floor(phase);
        const float x = phase * float(N);
        const int i = std::min(int(x), N - 1);
        const float f = x - float(i);
        return v[i] + (v[i + 1] - v[i]) * f;
    }
};
const SineTable &sine() {
    static const SineTable t;
    return t;
}

// A PolyBLEP pulse of any width, its DC taken out so a narrow chip pulse
// doesn't push the filter off centre.
struct PulseOsc {
    float phase = 0;
    inline float step(float inc, float pw) {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        float v = phase < pw ? 1.0f : -1.0f;
        v += polyBlep(phase, inc);
        float t2 = phase - pw;
        if (t2 < 0)
            t2 += 1.0f;
        v -= polyBlep(t2, inc);
        return v - (2.0f * pw - 1.0f);
    }
};

// Vowel formants (Hz) from dark to bright: oo, oh, ah, eh, ee.
constexpr float kVowels[5][3] = {
    {320, 800, 2300}, {450, 830, 2600}, {760, 1150, 2500}, {550, 1800, 2550}, {290, 2250, 3000}};

// An attack, decay, sustain, release envelope with the same shape as the
// shared Adsr, but with its coefficients worked out when it is struck rather
// than an exp() every sample: a lead keeps two of these running per voice.
struct Env {
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float v = 0, s = 0.8f, ka = 1, kd = 1, kr = 1;
    void set(float a, float d, float sustain, float r, float rate) {
        ka = 1.0f - std::exp(-1.0f / (std::max(a, 0.0005f) * rate));
        kd = 1.0f - std::exp(-1.0f / (std::max(d, 0.001f) * rate * 0.3f));
        kr = 1.0f - std::exp(-1.0f / (std::max(r, 0.001f) * rate * 0.3f));
        s = sustain;
    }
    void gate(bool on) { stage = on ? Attack : (stage == Idle ? Idle : Release); }
    inline float step() {
        switch (stage) {
        case Attack:
            // Toward a little over the top, so the attack ends on a slope, not a crawl.
            v += (1.2f - v) * ka;
            if (v >= 1.0f) {
                v = 1.0f;
                stage = Decay;
            }
            break;
        case Decay:
            v += (s - v) * kd;
            if (std::fabs(v - s) < 1e-4f)
                stage = Sustain;
            break;
        case Sustain:
            v = s;
            break;
        case Release:
            v -= v * kr;
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

} // namespace

// --- engine -------------------------------------------------------------------

// One held note of a line, as its commands describe it.
struct NoteIn {
    int id = -1;
    int degree = 0, octave = 0;
    float vel = 0.8f, y = 0.5f, off = 0, motion = 0, fat = 0, twist = 0, press = 0.5f;
    bool follow = false;
};

// A line of play: the live player, or one clip. Mono, last note priority.
struct Line {
    NoteIn notes[10];
    int count = 0;
    NoteIn last; // the note that sounded last, for its release
    int voice = -1;
    int sound = 0;
    float harmRow[kHarmSlots] = {}, harmLevel[kHarmSlots] = {};
    float bend = 0, throwAmt = 0, octave = 0, glideScale = 1;
    int gate = 0;
    const NoteIn *top() const { return count > 0 ? &notes[count - 1] : nullptr; }
};

struct Bank {
    static constexpr int kUni = 7;
    float pitch = 60, target = 60; // MIDI, gliding toward the target
    float level = 0, want = 0;     // gain, fading toward want (harmony voices come and go)
    SawOsc saw[kUni];
    PulseOsc pulse[2];
    float car[2] = {}, mod[2] = {};
    SawOsc layer;
    Oversampler2x osL, osR;
};

struct Voice {
    static constexpr int kBanks = 1 + kHarmSlots;
    bool active = false, releasing = false;
    int owner = -1; // the line driving it, or −1 once it's been left to ring out
    int from = 0;   // the line it was started for: a tail keeps that line's gate and throw
    int sound = 0;
    uint64_t born = 0;
    Bank banks[kBanks];
    Env amp, filt;
    float vel = 0.8f;
    float tau = 0.07f; // the glide toward the current note
    float y = 0.5f, press = 0.5f, fat = 0, motion = 0; // smoothed expression
    float ySet = 0.5f, pressSet = 0.5f, fatSet = 0, motionSet = 0;
    float scoop = 0, fmEnv = 1;
    float vibPhase = 0, held = 0; // seconds since the note began, for the vibrato's delay
    float pwmPhase = 0;
    float gateEnv = 1;
    float cut = 4;                // smoothed cutoff, in octaves above 60 Hz
    Ladder fL, fR;
    Svf form[2][3];
    float level = 0;      // for the page
    float lastL = 0, lastR = 0; // the last sample out, for a stolen voice's declick
    Rng rng;
};

class LeadEngine : public PartEngine {
public:
    void init(float rate) override {
        m_rate = rate;
        (void)sine();
        for (size_t i = 0; i < m_voices.size(); ++i)
            m_voices[i].rng = Rng(0x1EADu + uint32_t(i) * 7919u);
        m_echoL.init(size_t(3.2f * rate));
        m_echoR.init(size_t(3.2f * rate));
        m_echoTime.setTime(0.25f, rate);
        m_echoTime.reset(0.36f * rate);
        m_echoHp.set(280.0f, 0.6f, rate);
        m_echoHp2.set(280.0f, 0.6f, rate);
        m_echoLpL.set(4800.0f, rate);
        m_echoLpR.set(4800.0f, rate);
        m_declickFade = std::exp(-1.0f / (0.003f * rate));
        m_hpL.set(120.0f, 0.6f, rate);
        m_hpR.set(120.0f, 0.6f, rate);
    }

    void command(const Command &c, AudioContext &ctx) override {
        // Clip events land on their exact sample: hold them for their offset
        // in the coming block. Live ones are late and play at once.
        const int off = ctx.offsetOf(c.t, kBlock);
        if (off > 0 && m_pendingCount < int(m_pending.size())) {
            m_pending[size_t(m_pendingCount++)] = {c, off};
            return;
        }
        apply(c, ctx);
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        // Pending commands, in time order, split the block.
        std::stable_sort(m_pending.begin(), m_pending.begin() + m_pendingCount,
                         [](const Pending &a, const Pending &b) { return a.offset < b.offset; });
        int at = 0, k = 0;
        while (at < n) {
            while (k < m_pendingCount && m_pending[size_t(k)].offset <= at)
                apply(m_pending[size_t(k++)].c, ctx);
            const int until = k < m_pendingCount ? std::min(n, m_pending[size_t(k)].offset) : n;
            if (until > at)
                renderSegment(l, r, at, until - at, ctx);
            at = until;
        }
        for (; k < m_pendingCount; ++k)
            apply(m_pending[size_t(k)].c, ctx);
        m_pendingCount = 0;
        renderEcho(l, r, n, ctx);
    }

    void silence(bool hard) override {
        for (auto &line : m_lines) {
            line.count = 0;
            line.bend = line.throwAmt = line.octave = 0;
        }
        for (auto &v : m_voices) {
            if (hard)
                v.active = false;
            else if (v.active)
                release(v);
        }
        if (hard) {
            m_echoL.clear();
            m_echoR.clear();
        }
    }

    void publish(float *data) override {
        // Sixteen numbers per line (the page's paint reads them), then the echo.
        for (int li = 0; li < kLines; ++li) {
            float *d = data + 16 * li;
            const Line &line = m_lines[size_t(li)];
            const Voice *v = line.voice >= 0 ? &m_voices[size_t(line.voice)] : nullptr;
            const bool on = v && v->active && v->owner == li;
            d[0] = on ? (line.count > 0 ? 1.0f : 0.5f) : 0.0f;
            d[1] = m_pubPos[li];
            d[2] = on ? v->level : 0.0f;
            d[3] = on ? v->y : 0.0f;
            for (int s = 0; s < kHarmSlots; ++s) {
                d[4 + s] = m_pubHarm[li][s];
                d[7 + s] = on ? v->banks[1 + s].level : 0.0f;
            }
            d[10] = line.bend;
            d[11] = line.throwAmt;
            d[12] = float(line.sound);
            d[13] = line.top() && line.top()->follow ? 1.0f : 0.0f;
            d[14] = on ? m_pubVib[li] : 0.0f;
            d[15] = line.octave;
        }
        data[160] = m_echoLevel;
        data[161] = float(voices());
    }

    int voices() const override {
        int n = 0;
        for (auto &v : m_voices)
            n += v.active;
        return n;
    }

private:
    struct Pending {
        Command c;
        int offset = 0;
    };

    // --- commands ---

    void apply(const Command &c, AudioContext &ctx) {
        const int li = lineOf(c.id);
        Line &line = m_lines[size_t(li)];
        switch (c.type) {
        case Command::On: {
            NoteIn n = noteFrom(c);
            n.vel = clampf(c.f[0], 0.1f, 1.0f);
            n.follow = (c.i[3] & kFollow) != 0;
            line.gate = (c.i[3] >> kGateShift) & 3;
            // The harmony held when the note landed, so a clip taken in the
            // middle of a held harmony plays it too.
            for (int s = 0; s < kHarmSlots; ++s) {
                const int row = unpackRow(c.i[3], s);
                if (row >= 0) {
                    if (line.harmLevel[s] <= 0)
                        line.harmRow[s] = float(row);
                    line.harmLevel[s] = std::max(line.harmLevel[s], 0.75f);
                } else if (li != 0) {
                    line.harmLevel[s] = 0;
                }
            }
            noteOn(li, n, std::clamp(c.i[2], 0, kSounds - 1), ctx);
            break;
        }
        case Command::Move:
            for (int i = 0; i < line.count; ++i)
                if (line.notes[i].id == c.id) {
                    const float vel = line.notes[i].vel;
                    const bool follow = line.notes[i].follow;
                    line.notes[i] = noteFrom(c);
                    line.notes[i].vel = vel;
                    line.notes[i].follow = follow;
                }
            break;
        case Command::Off:
            noteOff(li, c.id, ctx);
            break;
        case Command::Param:
            switch (c.i[0]) {
            case PHarmony: {
                const int s = std::clamp(c.i[1], 0, kHarmSlots - 1);
                line.harmRow[s] = clampf(c.f[0], 0.0f, float(kRows - 1));
                line.harmLevel[s] = clampf(c.f[1], 0.0f, 1.0f);
                break;
            }
            case PBend:
                line.bend = clampf(c.f[0], -4.0f, 4.0f);
                break;
            case PThrow:
                line.throwAmt = clampf(c.f[0], 0.0f, 1.0f);
                break;
            case POctave:
                line.octave = c.f[0] > 0.5f ? 1.0f : 0.0f;
                break;
            case PGate:
                line.gate = std::clamp(c.i[1], 0, kGates - 1);
                break;
            case PGlide:
                line.glideScale = clampf(c.f[0], 0.1f, 6.0f);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    static NoteIn noteFrom(const Command &c) {
        NoteIn n;
        n.id = c.id;
        n.degree = c.i[0];
        n.octave = c.i[1];
        n.y = clampf(c.f[1], 0.0f, 1.0f);
        n.off = clampf(c.f[2], -1.5f, 1.5f);
        n.motion = clampf(c.f[3], 0.0f, 1.0f);
        n.fat = clampf(c.f[4], 0.0f, 1.0f);
        n.twist = clampf(c.f[5], -1.5f, 1.5f);
        n.press = clampf(c.f[6], 0.0f, 1.0f);
        return n;
    }

    void noteOn(int li, const NoteIn &n, int snd, AudioContext &ctx) {
        Line &line = m_lines[size_t(li)];
        // A note already held under this id (a replayed clip coming round)
        // is replaced, not stacked.
        removeNote(line, n.id);
        if (line.count == int(std::size(line.notes)))
            removeNote(line, line.notes[0].id);
        const bool legato = line.count > 0;
        line.notes[line.count++] = n;
        Voice *v = line.voice >= 0 ? &m_voices[size_t(line.voice)] : nullptr;
        const bool mine = v && v->active && v->owner == li;
        if (legato && mine) {
            // A hammer-on: the voice glides on; plucks and bells strike again.
            const Sound &s = sound(v->sound);
            v->tau = glideTime(*v, line, n, s.glide, ctx);
            v->held = 0;
            if (s.retrig)
                strike(*v, n.vel, false);
            return;
        }
        line.sound = snd;
        const Sound &s = sound(snd);
        if (mine && s.porta > 0 && v->sound == snd) {
            // A mono synth with portamento: the voice still ringing glides
            // from where it was into the new note and starts again from its
            // own level, with no click.
            v->releasing = false;
            v->tau = glideTime(*v, line, n, s.porta, ctx);
            v->held = 0;
            strike(*v, n.vel, true);
            return;
        }
        if (mine)
            v->owner = -1; // the old note rings out on its own
        const int vi = allocate();
        line.voice = vi;
        start(m_voices[size_t(vi)], li, snd, n, ctx);
    }

    void noteOff(int li, int id, AudioContext &ctx) {
        Line &line = m_lines[size_t(li)];
        const NoteIn *was = line.top();
        const bool wasTop = was && was->id == id;
        if (wasTop)
            line.last = *was;
        if (!removeNote(line, id))
            return;
        Voice *v = line.voice >= 0 ? &m_voices[size_t(line.voice)] : nullptr;
        if (!v || !v->active || v->owner != li)
            return;
        if (line.count == 0) {
            release(*v);
        } else if (wasTop) {
            // A pull-off: back to the note still held, gliding, no new attack.
            v->tau = glideTime(*v, line, *line.top(), sound(v->sound).glide, ctx);
        }
    }

    // A player's glide: quick across a step, so trills and runs stay
    // crisp, and slower across a leap, so a jump of a fifth or an octave
    // is heard as a swoop.
    static float glideTime(const Voice &v, const Line &line, const NoteIn &to, float base, AudioContext &ctx) {
        float pitch[Voice::kBanks], level[Voice::kBanks];
        targetsFor(line, to, ctx.harmony(), pitch, level);
        const float interval = std::fabs(pitch[0] - v.banks[0].pitch);
        return base * line.glideScale * clampf(std::sqrt(interval / 5.0f), 0.4f, 1.6f);
    }

    static bool removeNote(Line &line, int id) {
        for (int i = 0; i < line.count; ++i)
            if (line.notes[i].id == id) {
                for (int j = i; j + 1 < line.count; ++j)
                    line.notes[j] = line.notes[j + 1];
                --line.count;
                return true;
            }
        return false;
    }

    int allocate() {
        for (size_t i = 0; i < m_voices.size(); ++i)
            if (!m_voices[i].active)
                return int(i);
        // Steal the quietest voice nobody holds, else the oldest.
        int best = -1;
        float quiet = 1e9f;
        for (size_t i = 0; i < m_voices.size(); ++i)
            if ((m_voices[i].owner < 0 || m_voices[i].releasing) && m_voices[i].level < quiet) {
                quiet = m_voices[i].level;
                best = int(i);
            }
        if (best >= 0)
            return best;
        uint64_t oldest = ~0ull;
        for (size_t i = 0; i < m_voices.size(); ++i)
            if (m_voices[i].born < oldest) {
                oldest = m_voices[i].born;
                best = int(i);
            }
        return std::max(best, 0);
    }

    void start(Voice &v, int li, int snd, const NoteIn &n, AudioContext &ctx) {
        const Sound &s = sound(snd);
        if (v.active) {
            // A stolen voice: its line lets go of it, and where its wave
            // stood is eased back to zero over a few milliseconds instead of
            // stepping there.
            if (v.owner >= 0 && v.owner != li)
                m_lines[size_t(v.owner)].voice = -1;
            m_declickL += v.lastL;
            m_declickR += v.lastR;
        }
        v.lastL = v.lastR = 0;
        v.active = true;
        v.releasing = false;
        v.owner = li;
        v.from = li;
        v.sound = snd;
        v.born = ++m_age;
        v.vel = n.vel;
        v.tau = s.glide;
        v.y = v.ySet = n.y;
        v.press = v.pressSet = n.press;
        v.fat = v.fatSet = n.fat;
        v.motion = v.motionSet = n.motion;
        v.held = 0;
        v.vibPhase = 0;
        v.pwmPhase = v.rng.unit();
        v.fL.reset();
        v.fR.reset();
        for (auto &ch : v.form)
            for (auto &f : ch)
                f.reset();
        v.amp.v = v.filt.v = 0;
        v.amp.stage = v.filt.stage = Env::Idle;
        v.level = 0;
        // Every bank starts on its own pitch: no glide into a first note.
        const Line &line = m_lines[size_t(li)];
        float targets[Voice::kBanks], levels[Voice::kBanks];
        targetsFor(line, n, ctx.harmony(), targets, levels);
        for (int b = 0; b < Voice::kBanks; ++b) {
            Bank &k = v.banks[b];
            k.pitch = k.target = targets[b];
            k.level = k.want = levels[b];
            for (auto &o : k.saw)
                o.phase = v.rng.unit();
            k.pulse[0].phase = v.rng.unit();
            k.pulse[1].phase = v.rng.unit();
            k.car[0] = k.car[1] = k.mod[0] = k.mod[1] = 0;
            k.layer.phase = v.rng.unit();
        }
        v.cut = cutoffFor(v, s, 1.0f);
        strike(v, n.vel, true);
    }

    // Starts the envelopes from wherever they are, so a struck legato note
    // or a portamento restart never clicks.
    void strike(Voice &v, float vel, bool scoop) {
        const Sound &s = sound(v.sound);
        v.amp.set(s.a, s.d, s.s, s.r, m_rate);
        v.filt.set(0.001f, s.envDecay, 0.0f, 0.3f, m_rate);
        v.amp.gate(true);
        v.filt.gate(true);
        v.fmEnv = 1.0f;
        if (scoop)
            v.scoop = -s.scoop * (0.7f + 0.6f * vel);
        v.vel = vel;
    }

    void release(Voice &v) {
        v.releasing = true;
        v.amp.gate(false);
        v.filt.gate(false);
    }

    // --- pitch ---

    static bool chordTone(int degree, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        for (int k = 0; k < h.chordCount; ++k)
            if (((degree - h.chordTones[k]) % per + per) % per == 0)
                return true;
        return false;
    }

    // The chord tone nearest a degree, by ear (in semitones); ties go down.
    static int nearestChordTone(int degree, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        int best = degree;
        double bestDist = 1e9;
        for (int d = degree - per; d <= degree + per; ++d) {
            if (!chordTone(d, h))
                continue;
            const double dist =
                std::fabs(h.tuning.degreeSemis(d) - h.tuning.degreeSemis(degree)) + (d > degree ? 0.01 : 0.0);
            if (dist < bestDist) {
                bestDist = dist;
                best = d;
            }
        }
        return best;
    }

    // The chord tone `rank` places above a degree (rank < 0: below).
    static int chordToneFrom(int degree, int rank, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        int d = degree, found = 0;
        const int step = rank > 0 ? 1 : -1;
        for (int guard = 0; guard < 4 * per && found < std::abs(rank); ++guard) {
            d += step;
            found += chordTone(d, h);
        }
        return found == std::abs(rank) ? d : degree + 2 * rank;
    }

    // Where a harmony row puts its voice, in degrees, for a lead degree.
    static float harmonyDegree(int lead, float row, bool follow, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        auto at = [&](int r) -> float {
            if (!follow || r == 0 || r == kRows - 1)
                return float(lead) + rowDegrees(r, per);
            // On the chord: the next chord tone up, the one after, or the
            // one just below put up an octave (a sixth over a chord tone).
            if (r == 1)
                return float(chordToneFrom(lead, 1, h));
            if (r == 2)
                return float(chordToneFrom(lead, 2, h));
            return float(chordToneFrom(lead, -1, h) + per);
        };
        const int r0 = std::clamp(int(std::floor(row)), 0, kRows - 1);
        const int r1 = std::min(r0 + 1, kRows - 1);
        const float f = row - float(r0);
        return at(r0) + (at(r1) - at(r0)) * f;
    }

    // Each bank's target pitch (MIDI) and level for a line's note, and
    // where the line and its harmony sing in degrees, for the page.
    struct Targets {
        float leadDegree = 0; // where the line sings, in degrees, for the page
        float harmDegree[kHarmSlots] = {};
    };
    static Targets targetsFor(const Line &line, const NoteIn &n, const Harmony &h, float *pitch, float *level) {
        Targets t;
        const int per = h.tuning.degreesPerOctave();
        const int abs = n.octave * per + n.degree;
        const int tgt = n.follow ? nearestChordTone(abs, h) : abs;
        // Resting, the note sits on its degree (or the chord tone); moving,
        // the pitch follows the finger. The twist and the bend wheel add steps.
        const float cont = (float(abs) + n.off - float(tgt)) * n.motion + n.twist + line.bend;
        const float base = float(h.tuning.baseMidi()) + 12.0f * line.octave;
        pitch[0] = base + float(h.tuning.continuousSemis(double(tgt) + double(cont)));
        level[0] = 1.0f;
        t.leadDegree = float(tgt) + cont + float(per) * line.octave;
        for (int s = 0; s < kHarmSlots; ++s) {
            const float hd = harmonyDegree(tgt, line.harmRow[s], n.follow, h) + cont;
            pitch[1 + s] = base + float(h.tuning.continuousSemis(double(hd)));
            level[1 + s] = line.harmLevel[s];
            t.harmDegree[s] = hd + float(per) * line.octave;
        }
        return t;
    }

    static float cutoffFor(const Voice &v, const Sound &s, float fe) {
        const float note = v.banks[0].pitch;
        float c = s.cutoff + s.cutoffY * (2.0f * v.y - 1.0f) + 0.16f * (v.press - 0.5f) +
                  s.envAmt * fe * (0.6f + 0.5f * v.vel);
        c = c * 8.1f + s.keyTrack * (note - 72.0f) / 12.0f;
        return clampf(c, 0.5f, 8.6f);
    }

    // --- sound ---

    void renderSegment(float *l, float *r, int at, int n, AudioContext &ctx) {
        const Harmony &h = ctx.harmony();
        const float dt = float(n) / m_rate;
        // Lines drive their voices.
        for (int li = 0; li < kLines; ++li) {
            Line &line = m_lines[size_t(li)];
            if (line.voice < 0)
                continue;
            Voice &v = m_voices[size_t(line.voice)];
            if (!v.active || v.owner != li)
                continue;
            const NoteIn *top = line.top();
            if (top)
                line.last = *top;
            const NoteIn &note = line.last;
            float targets[Voice::kBanks], levels[Voice::kBanks];
            const Targets t = targetsFor(line, note, h, targets, levels);
            for (int b = 0; b < Voice::kBanks; ++b) {
                v.banks[b].target = targets[b];
                v.banks[b].want = levels[b];
            }
            v.ySet = note.y;
            v.pressSet = note.press;
            v.fatSet = note.fat;
            v.motionSet = note.motion;
            m_pubPos[li] = t.leadDegree;
            for (int s = 0; s < kHarmSlots; ++s)
                m_pubHarm[li][s] = t.harmDegree[s];
        }
        m_throw = 0;
        for (auto &line : m_lines)
            m_throw = std::max(m_throw, line.throwAmt);
        const Transport &tr = ctx.transport();
        for (auto &v : m_voices)
            if (v.active) {
                // The gate runs on the studio's beat, so it lands with the drums.
                const int steps = gateSteps(m_lines[size_t(v.from)].gate);
                const double phase = tr.beatAt(at) * steps;
                renderVoice(v, l + at, r + at, m_sendL + at, m_sendR + at, n, dt, float(phase - std::floor(phase)),
                            float(tr.beatsPerSample * steps), steps > 0);
            }
    }

    void renderVoice(Voice &v, float *outL, float *outR, float *sendL, float *sendR, int n, float dt, float gatePhase,
                     float gateInc, bool gated) {
        const Sound &s = sound(v.sound);
        const float rate = m_rate;
        // Expression follows the finger closely but never steps.
        const float ex = 1.0f - std::exp(-dt / 0.012f);
        v.y += (v.ySet - v.y) * ex;
        v.press += (v.pressSet - v.press) * ex;
        v.fat += (v.fatSet - v.fat) * (1.0f - std::exp(-dt / 0.05f));
        v.motion += (v.motionSet - v.motion) * ex;
        v.held += dt;

        // Pitch: a glide that tightens while the finger moves, so slides
        // and wiggles are heard as played and a new note still glides in.
        const float tau = lerpf(std::max(v.tau, 0.004f), 0.008f, v.motion);
        const float g = 1.0f - std::exp(-dt / tau);
        // Vibrato fades in on a held note, deeper as the finger leans in,
        // and gives way to the player's own wiggle.
        const float fade = clampf((v.held - s.vibDelay) / 0.5f, 0.0f, 1.0f);
        const float lean = clampf((v.press - 0.3f) / 0.5f, 0.0f, 1.4f);
        const float vibDepth = s.vibDepth * fade * (0.3f + 0.7f * lean) * (1.0f - v.motion);
        const float vib0 = vibDepth * sine()(v.vibPhase);
        v.vibPhase += s.vibRate * dt;
        v.vibPhase -= std::floor(v.vibPhase);
        const float vib1 = vibDepth * sine()(v.vibPhase);
        const float scoop0 = v.scoop;
        v.scoop *= std::exp(-dt / std::max(s.scoopTime, 0.005f));
        if (v.owner >= 0)
            m_pubVib[v.owner] = vibDepth;

        float inc0[Voice::kBanks], inc1[Voice::kBanks], gain0[Voice::kBanks], gain1[Voice::kBanks];
        bool live[Voice::kBanks];
        for (int b = 0; b < Voice::kBanks; ++b) {
            Bank &k = v.banks[b];
            const float p0 = k.pitch + vib0 + scoop0;
            k.pitch += (k.target - k.pitch) * g;
            const float p1 = k.pitch + vib1 + v.scoop;
            inc0[b] = midiToHz(p0) / rate;
            inc1[b] = midiToHz(p1) / rate;
            const float l0 = k.level;
            k.level += (k.want - k.level) * (1.0f - std::exp(-dt / 0.035f));
            if (k.want <= 0 && k.level < 1e-3f)
                k.level = 0;
            // Harmony voices sit just under the melody.
            const float hg = b == 0 ? 1.0f : 0.62f;
            gain0[b] = l0 * hg;
            gain1[b] = k.level * hg;
            live[b] = b == 0 || l0 > 0 || k.level > 0;
        }
        // Harmony makes the line bigger, not three times as loud.
        {
            float h0 = 0, h1 = 0;
            for (int b = 1; b < Voice::kBanks; ++b) {
                h0 += gain0[b];
                h1 += gain1[b];
            }
            const float n0 = 1.0f / (1.0f + 0.55f * h0), n1 = 1.0f / (1.0f + 0.55f * h1);
            for (int b = 0; b < Voice::kBanks; ++b) {
                gain0[b] *= n0;
                gain1[b] *= n1;
            }
        }

        // Unison: the outer saws furthest out in pitch and in the field. A
        // flatter finger detunes and spreads them further.
        static const float det7[Bank::kUni] = {0.0f, -1.0f, 1.0f, -0.46f, 0.46f, -0.15f, 0.15f};
        static const float det2[2] = {-0.5f, 0.5f};
        const int U = std::clamp(s.unison, 1, Bank::kUni);
        const float cents = s.detune * (0.55f + 1.0f * v.fat);
        const float spread = clampf(s.spread * (0.75f + 0.4f * v.fat), 0.0f, 1.0f);
        float uniRatio[Bank::kUni], uniL[Bank::kUni], uniR[Bank::kUni];
        for (int u = 0; u < U; ++u) {
            const float d = U == 2 ? det2[u] : det7[u];
            uniRatio[u] = std::exp2(d * cents / 1200.0f);
            const float pan = clampf(d * spread, -1.0f, 1.0f);
            uniL[u] = std::sqrt(0.5f * (1.0f - pan));
            uniR[u] = std::sqrt(0.5f * (1.0f + pan));
        }
        const float uniGain = s.saw * 1.2f / std::sqrt(float(U));
        const float pulseDet = std::exp2((2.0f + 6.0f * v.fat) / 1200.0f);
        const float fmDet = std::exp2((s.detune * (0.25f + 0.75f * v.fat)) / 1200.0f);
        v.pwmPhase += 0.37f * dt;
        v.pwmPhase -= std::floor(v.pwmPhase);
        const float pw = clampf(s.pw + s.pwm * sine()(v.pwmPhase), 0.06f, 0.94f);
        // The FM strike falls to its floor; a high note gets less index so
        // its sidebands stay under Nyquist.
        v.fmEnv = s.fmFloor + (v.fmEnv - s.fmFloor) * std::exp(-dt / std::max(s.fmDecay, 0.01f));
        const float hz0 = inc1[0] * rate;
        const float fmIndexMax = std::max(0.0f, 0.42f * rate / (hz0 * (s.fmRatio + 1.0f)) - 1.0f);
        const float fmIndex = std::min(s.fmIndex * v.fmEnv * (0.7f + 0.5f * v.vel + 0.25f * v.y), fmIndexMax);

        // The filter, set once a segment from smoothed numbers.
        const float fe = v.filt.v;
        v.cut += (cutoffFor(v, s, fe) - v.cut) * (1.0f - std::exp(-dt / 0.004f));
        const float fc = std::min(60.0f * std::exp2(v.cut), rate * (0.45f - 0.12f * s.res));
        v.fL.set(fc, s.res, rate);
        v.fR.set(fc, s.res, rate);
        float formGain[3] = {};
        if (s.formant) {
            // The height walks through the vowels.
            const float pos = clampf(v.y, 0.0f, 1.0f) * 3.999f;
            const int a = int(pos);
            const float f = pos - float(a);
            static const float q[3] = {7.0f, 9.0f, 11.0f};
            static const float gk[3] = {1.0f, 0.7f, 0.42f};
            for (int k = 0; k < 3; ++k) {
                const float hz = std::exp(lerpf(std::log(kVowels[a][k]), std::log(kVowels[a + 1][k]), f));
                formGain[k] = gk[k];
                v.form[0][k].set(hz, q[k], rate);
                v.form[1][k].set(hz, q[k], rate);
            }
        }
        const float drive = s.drive;
        const float driveGain = 1.0f + 6.0f * drive;
        const float driveNorm = 1.0f / (1.0f + 1.6f * drive);
        const float resComp = 1.0f + 0.8f * s.res * s.res;
        const float swell = 0.5f + 0.75f * v.press;
        const float outGain = s.gain * 0.3f * swell * (0.45f + 0.55f * v.vel);
        const float send = s.echo + 0.9f * m_lines[size_t(v.from)].throwAmt;
        const float inv = 1.0f / float(n);
        const float gateAttack = 1.0f - std::exp(-1.0f / (0.0015f * rate));
        const float gateRelease = 1.0f - std::exp(-1.0f / (0.004f * rate));

        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const float t = (float(i) + 0.5f) * inv;
            const float a = v.amp.step();
            v.filt.step();
            float sl = 0, sr = 0;
            for (int b = 0; b < Voice::kBanks; ++b) {
                if (!live[b])
                    continue;
                Bank &k = v.banks[b];
                const float inc = inc0[b] + (inc1[b] - inc0[b]) * t;
                const float gb = gain0[b] + (gain1[b] - gain0[b]) * t;
                float bl = 0, br = 0;
                if (s.saw > 0) {
                    for (int u = 0; u < U; ++u) {
                        const float o = k.saw[u].step(std::min(inc * uniRatio[u], 0.45f));
                        bl += o * uniL[u];
                        br += o * uniR[u];
                    }
                    bl *= uniGain;
                    br *= uniGain;
                }
                if (s.pulse > 0) {
                    const float p0 = k.pulse[0].step(std::min(inc, 0.45f), pw);
                    const float p1 = k.pulse[1].step(std::min(inc * pulseDet, 0.45f), 1.0f - pw);
                    // A second pulse, a few cents off and leaning right, comes
                    // up as the finger flattens: a chorus.
                    const float w = 0.2f + 0.6f * v.fat;
                    bl += s.pulse * 0.6f * (p0 + 0.6f * w * p1);
                    br += s.pulse * 0.6f * (0.8f * p0 + w * p1);
                }
                if (s.fm > 0) {
                    float fmo[2];
                    for (int c = 0; c < 2; ++c) {
                        const float ci = std::min(inc * (c ? fmDet : 1.0f / fmDet), 0.45f);
                        k.car[c] += ci;
                        k.mod[c] += ci * s.fmRatio;
                        k.car[c] -= std::floor(k.car[c]);
                        k.mod[c] -= std::floor(k.mod[c]);
                        fmo[c] = sine()(k.car[c] + fmIndex * 0.15915494f * sine()(k.mod[c]));
                    }
                    // Each side mostly its own carrier: wide, but whole in mono.
                    bl += s.fm * 0.8f * (0.8f * fmo[0] + 0.2f * fmo[1]);
                    br += s.fm * 0.8f * (0.8f * fmo[1] + 0.2f * fmo[0]);
                }
                if (s.layer > 0) {
                    const float o = k.layer.step(std::min(inc * 2.0f, 0.45f)) * s.layer;
                    bl += o;
                    br += o;
                }
                // Each voice is driven on its own: driven together, a third
                // and its melody would grind out difference tones down in
                // the bass's range. Hard drive runs at twice the rate so its
                // harmonics don't fold.
                if (drive > 0.3f) {
                    bl = k.osL.process(bl, [&](float x) { return fastTanh(x * driveGain); }) * driveNorm;
                    br = k.osR.process(br, [&](float x) { return fastTanh(x * driveGain); }) * driveNorm;
                } else if (drive > 0) {
                    bl = fastTanh(bl * driveGain) * driveNorm;
                    br = fastTanh(br * driveGain) * driveNorm;
                }
                // Harmony voices stand a little to either side of the melody.
                static const float bankPan[Voice::kBanks][2] = {{1.0f, 1.0f}, {1.15f, 0.82f}, {0.82f, 1.15f}, {1.0f, 1.0f}};
                sl += bl * gb * bankPan[b][0];
                sr += br * gb * bankPan[b][1];
            }
            if (s.formant) {
                float fl = 0, fr = 0;
                for (int k = 0; k < 3; ++k) {
                    fl += v.form[0][k].step(sl).band * formGain[k];
                    fr += v.form[1][k].step(sr).band * formGain[k];
                }
                sl = fl;
                sr = fr;
            }
            // Into the ladder at half level: its own soft clip stays a
            // colour, not a distortion that would smear a bell's partials.
            sl = v.fL.step(sl * 0.5f) * 2.0f * resComp;
            sr = v.fR.step(sr * 0.5f) * 2.0f * resComp;
            // The gate: open for a little over half of each step, with
            // edges a few milliseconds long so it pulses without clicking.
            float gate = 1.0f;
            if (gated) {
                const float want = gatePhase < 0.56f ? 1.0f : 0.0f;
                v.gateEnv += (want - v.gateEnv) * (want > v.gateEnv ? gateAttack : gateRelease);
                gate = v.gateEnv;
                gatePhase += gateInc;
                if (gatePhase >= 1.0f)
                    gatePhase -= 1.0f;
            } else {
                v.gateEnv += (1.0f - v.gateEnv) * gateRelease;
                gate = v.gateEnv;
            }
            const float g2 = a * outGain * gate;
            sl *= g2;
            sr *= g2;
            outL[i] += sl;
            outR[i] += sr;
            v.lastL = sl;
            v.lastR = sr;
            sendL[i] += sl * send;
            sendR[i] += sr * send;
            peak = std::max(peak, std::fabs(sl) + std::fabs(sr));
        }
        v.level = std::max(peak * 2.0f, v.level * std::exp(-dt / 0.08f));
        // A released voice is done once it's 70 dB down; the envelope's own
        // idea of silence is far below anything audible, and every voice
        // still ringing costs its full price.
        if (v.releasing && (v.amp.stage == Env::Idle || v.amp.v < 3e-4f))
            v.active = false;
    }

    // The part's own echo: tempo-synced ping-pong, darker and thinner with
    // every repeat. A throw pushes the line into it and lets the feedback
    // climb; letting go leaves the repeats to fade on their own.
    void renderEcho(float *l, float *r, int n, AudioContext &ctx) {
        const Transport &tr = ctx.transport();
        float beats = 0.75f, fbBase = 0.25f;
        for (const auto &line : m_lines) {
            if (line.voice < 0)
                continue;
            const Voice &v = m_voices[size_t(line.voice)];
            if (!v.active)
                continue;
            beats = sound(v.sound).echoBeats;
            fbBase = std::max(fbBase, sound(v.sound).echoFb);
        }
        const float target = std::min(beats * float(tr.secondsPerBeat()) * m_rate, 3.0f * m_rate);
        const float fbTarget = fbBase + (0.8f - fbBase) * m_throw;
        const float dt = float(n) / m_rate;
        m_echoFb += (fbTarget - m_echoFb) * (1.0f - std::exp(-dt / (fbTarget > m_echoFb ? 0.05f : 2.5f)));
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const float d = m_echoTime.step(target);
            const float dl = m_echoL.read(d), dr = m_echoR.read(d);
            // Ping-pong: what goes in starts on the left, every repeat crosses.
            const float fl = m_echoLpL.step(m_echoHp.step(dr).high) * m_echoFb;
            const float fr = m_echoLpR.step(m_echoHp2.step(dl).high) * m_echoFb;
            m_echoL.push(fastTanh(0.7f * (m_sendL[i] + m_sendR[i]) + fl));
            m_echoR.push(fastTanh(fr));
            m_declickL *= m_declickFade;
            m_declickR *= m_declickFade;
            const float ol = l[i] + dl * 0.7f + m_declickL, orr = r[i] + dr * 0.7f + m_declickR;
            // Nothing under the lead's own range: the −OCT voice keeps its
            // body, the rumble goes.
            l[i] = m_dcL.step(m_hpL.step(ol).high);
            r[i] = m_dcR.step(m_hpR.step(orr).high);
            peak = std::max(peak, std::fabs(dl) + std::fabs(dr));
            m_sendL[i] = m_sendR[i] = 0;
        }
        m_echoLevel = std::max(peak, m_echoLevel * 0.97f);
    }

    float m_rate = 48000;
    uint64_t m_age = 0;
    std::array<Voice, 8> m_voices;
    std::array<Line, kLines> m_lines;
    std::array<Pending, 64> m_pending;
    int m_pendingCount = 0;
    float m_pubPos[kLines] = {}, m_pubHarm[kLines][kHarmSlots] = {}, m_pubVib[kLines] = {};

    float m_sendL[kBlock] = {}, m_sendR[kBlock] = {};
    Delay m_echoL, m_echoR;
    Smooth m_echoTime;
    Svf m_echoHp, m_echoHp2;
    OnePoleLp m_echoLpL, m_echoLpR;
    float m_echoFb = 0.3f, m_echoLevel = 0, m_throw = 0;
    float m_declickL = 0, m_declickR = 0, m_declickFade = 0.99f;
    Svf m_hpL, m_hpR;
    DcBlock m_dcL, m_dcR;
};

// --- control ------------------------------------------------------------------

namespace {

// How much a finger is moving, 0 resting .. 1 sliding or wiggling: quick to
// rise, slower to fall, so a vibrato's turning points don't count as rest.
struct Motion {
    float last = 0, speed = 0, m = 0;
    double t = -1;
    float update(float p, double now) {
        if (t < 0) {
            t = now;
            last = p;
            return m;
        }
        const float dt = float(std::max(now - t, 0.002));
        const float v = std::fabs(p - last) / dt;
        speed += (v - speed) * std::min(1.0f, dt / 0.03f);
        const float want = clampf((speed - 0.6f) / 2.4f, 0.0f, 1.0f);
        m += (want - m) * std::min(1.0f, dt / (want > m ? 0.02f : 0.16f));
        last = p;
        t = now;
        return m;
    }
};

// Rests a continuous position on whole steps unless it is moving.
float rounded(float p, float motion) {
    const float q = std::round(p);
    return q + (p - q) * motion;
}

QString noteName(double midi) {
    const int m = int(std::lround(midi));
    return QString::fromUtf8(Tuning::noteName(m)) + QString::number(m / 12 - 1);
}

} // namespace

class LeadControl : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        syncKey(ctx);
        if (f.button != m_click) {
            m_click = f.button;
            param(ctx, POctave, 0, m_click ? 1.0f : 0.0f, 0, f.t);
        }
        // Lifts first.
        for (auto &h : m_held) {
            if (h.id < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still)
                lift(h, f.t, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Held *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (h)
                    land(*h, t, f.t, ctx);
                continue;
            }
            follow(*h, t, f.t, ctx);
        }
        m_phrase.expire(f.t);
        m_status = describe();
    }

    void background(double t, ControlContext &ctx) override {
        // The trail: what the lines sang, sampled for the page to draw as
        // it rises away from the note names.
        if (t - m_lastSample < 0.01)
            return;
        m_lastSample = t;
        const PartView &v = ctx.studio().parts[ctx.part()];
        TrailPoint &p = m_trail[size_t(m_trailHead++ % kTrail)];
        p.t = t;
        for (int k = 0; k < 2; ++k) {
            const int line = k == 0 ? 0 : (v.playing >= 0 ? 1 + v.playing : -1);
            if (line < 0) {
                p.level[k][0] = 0;
                continue;
            }
            const float *d = v.data + 16 * line;
            p.pos[k][0] = d[1];
            p.level[k][0] = d[0] > 0 ? d[2] : 0.0f;
            for (int s = 0; s < kHarmSlots; ++s) {
                p.pos[k][1 + s] = d[4 + s];
                p.level[k][1 + s] = d[0] > 0 ? d[2] * d[7 + s] : 0.0f;
            }
        }
    }

    void leave(ControlContext &ctx) override {
        for (auto &h : m_held)
            if (h.id >= 0)
                lift(h, ctx.now(), ctx);
        for (auto &k : m_keys)
            if (k.down) {
                k.down = false;
                off(ctx, k.id, ctx.now());
            }
        if (m_click) {
            m_click = false;
            param(ctx, POctave, 0, 0, 0, ctx.now());
        }
        if (m_latch) {
            m_latch = false;
            for (int s = 0; s < kHarmSlots; ++s)
                if (m_slots[s].on && !m_slots[s].held) {
                    m_slots[s].on = false;
                    param(ctx, PHarmony, s, m_slots[s].row, 0, ctx.now());
                }
        }
        m_frame = Frame{};
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool used = handleKey(k, ctx);
        m_status = describe();
        return used;
    }

    bool handleKey(const KeyEvent &k, ControlContext &ctx) {
        const int degree = pianoDegree(k.key);
        if (degree >= 0) {
            if (k.repeat)
                return true;
            KeyHeld &kh = m_keys[size_t(std::clamp(pianoIndex(k.key), 0, int(m_keys.size()) - 1))];
            const double t = ctx.now();
            if (k.down && !kh.down) {
                syncKey(ctx);
                kh.down = true;
                kh.id = kKeyId + pianoIndex(k.key);
                const bool accent = k.modifiers & Qt::ShiftModifier;
                const int abs = (m_neckOct + m_shift) * m_per + degree;
                Command on;
                on.type = Command::On;
                on.id = kh.id;
                on.t = t;
                on.i[0] = ((abs % m_per) + m_per) % m_per;
                on.i[1] = (abs - on.i[0]) / m_per;
                on.i[2] = m_sound;
                on.i[3] = flags();
                on.f[0] = accent ? 1.0f : 0.75f;
                on.f[1] = 0.6f;
                on.f[4] = 0.25f;
                on.f[6] = accent ? 0.85f : 0.55f;
                ctx.send(on);
                kh.abs = abs;
                pushLanding(abs, t, kh.id);
            } else if (!k.down && kh.down) {
                kh.down = false;
                off(ctx, kh.id, t);
                liftLanding(kh.id, t);
            }
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (k.modifiers & Qt::ShiftModifier) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 cleared").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
            m_sound = (m_sound + (k.key == Qt::Key_Left ? kSounds - 1 : 1)) % kSounds;
            ctx.notice(QString::fromUtf8(sound(m_sound).name));
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            if (k.modifiers & Qt::ShiftModifier) {
                // Shift with ↑ ↓: the glide, from a quarter of the sound's own to four times it.
                m_glide = std::clamp(m_glide + (k.key == Qt::Key_Up ? 1 : -1), -2, 2);
                param(ctx, PGlide, 0, std::exp2(float(m_glide)), 0, ctx.now());
                ctx.notice(QStringLiteral("Glide ×%1").arg(double(std::exp2(float(m_glide)))));
                return true;
            }
            m_shift = std::clamp(m_shift + (k.key == Qt::Key_Up ? 1 : -1), -1, 1);
            ctx.notice(m_shift == 0 ? QStringLiteral("Middle octave")
                                    : QStringLiteral("Octave %1%2").arg(m_shift > 0 ? "+" : "−").arg(std::abs(m_shift)));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (k.modifiers & Qt::ShiftModifier)
                return toggleLatch(ctx);
            m_follow = !m_follow;
            ctx.notice(m_follow ? QStringLiteral("Following the chord") : QStringLiteral("Free in the scale"));
            return true;
        case Qt::Key_Backslash:
            return toggleLatch(ctx);
        case Qt::Key_QuoteLeft:
        case Qt::Key_section: // the key left of 1: ` on a US keyboard, § on a Nordic one
            m_gate = (m_gate + 1) % kGates;
            {
                Command c;
                c.type = Command::Param;
                c.t = ctx.now();
                c.i[0] = PGate;
                c.i[1] = m_gate;
                ctx.send(c);
            }
            ctx.notice(m_gate ? QStringLiteral("Gate %1").arg(QString::fromUtf8(gateName(m_gate)))
                              : QStringLiteral("Gate off"));
            return true;
        case Qt::Key_F9:
            if (m_recording) {
                ctx.stopRecording();
                ctx.notice(QStringLiteral("Recording ends at the bar"));
            } else {
                const int slot = std::max(0, ctx.freeSlot());
                ctx.record(slot, 8);
                ctx.notice(QStringLiteral("Recording into slot %1 from the next bar").arg(slot + 1));
            }
            m_recording = !m_recording;
            return true;
        case Qt::Key_F10:
            m_overdub = !m_overdub;
            ctx.overdub(m_overdub);
            ctx.notice(m_overdub ? QStringLiteral("Overdub on") : QStringLiteral("Overdub off"));
            return true;
        default:
            return false;
        }
    }

    bool toggleLatch(ControlContext &ctx) {
        m_latch = !m_latch;
        if (!m_latch)
            for (int s = 0; s < kHarmSlots; ++s)
                if (m_slots[s].on && !m_slots[s].held) {
                    m_slots[s].on = false;
                    param(ctx, PHarmony, s, m_slots[s].row, 0, ctx.now());
                }
        ctx.notice(m_latch ? QStringLiteral("Harmony holds") : QStringLiteral("Harmony follows the fingers"));
        return true;
    }

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "finger on the neck\ta note of the scale; one voice, singing\n"
            "second finger while one is held\tlegato: glides there, lift to fall back (trills, runs)\n"
            "slide\tglide through the scale; rest and it settles in tune\n"
            "wiggle\tvibrato as wide as the wiggle; hold still and it comes by itself\n"
            "higher on the pad\tbrighter (on Vox, the vowel: oo, oh, ah, eh, ee)\n"
            "press harder\tswell, and a deeper vibrato\n"
            "flatten the finger\tfatter: the unison spreads\n"
            "twist the finger\tbend up or down one step of the scale\n"
            "click the pad\tleap an octave up while held\n"
            "left column, harmony rows\teach finger adds a voice: −oct, 3rd, 5th, 6th, +oct; slide between\n"
            "BEND\ta pitch wheel: slide up or down, rests on whole steps\n"
            "THROW\tinto the echo; higher throws harder; lift and the repeats ring out\n"
            "tap a riff, then its first note again on the beat, held\tloops it into the next free slot\n"
            "Z X C … / A S D … / Q W E …\tthe scale on three rows, an octave apart; overlap keys for legato; Shift accents\n"
            "← →\tsound: Anthem, Pluck, Bell, Chip, Vox, Scream\n"
            "↑ ↓\toctave of the neck and the keys\n"
            "Shift+↑ ↓\tglide longer, shorter\n"
            "Enter\tfollow the chord: notes and harmony land on chord tones\n"
            "Shift+Enter (or \\)\tharmony holds after the fingers lift\n"
            "§ (or `)\ttrance gate: off, 1/8, 1/16, 1/8 triplets, on the studio's beat\n"
            "F1–F8, Shift+F1–F8\tlaunch a slot, clear it\n"
            "F9 / F10\trecord from the next bar / overdub\n"
            "Backspace\tstop this part at the next bar");
    }

    QString status() const override { return m_status; }

private:
    enum class Role : uint8_t { None, Note, Harmony, Bend, Throw };
    struct Held {
        int id = -1;
        Role role = Role::None;
        double t0 = 0;
        int q = 0;          // the neck cell, with hysteresis
        int abs = 0;        // its absolute degree
        int slot = -1;      // harmony slot
        float y0 = 0, angle0 = 0;
        Motion motion, twistMotion;
        bool long_ = false, looped = false;
        double lastSend = 0;
        float sent[8] = {};
        float shown = 0;    // what the finger's tag says (bend amount, row)
    };
    struct KeyHeld {
        bool down = false;
        int id = 0, abs = 0;
    };
    struct Slot {
        bool on = false, held = false;
        float row = 1, level = 0;
    };
    static constexpr int kTrail = 360;
    struct TrailPoint {
        double t = -1;
        float pos[2][1 + kHarmSlots] = {};
        float level[2][1 + kHarmSlots] = {};
    };

    Held *find(int id) {
        for (auto &h : m_held)
            if (h.id == id)
                return &h;
        return nullptr;
    }

    void syncKey(ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        m_per = family(v.family).count;
        m_neckOct = neckOctave(v.root);
    }

    int flags() const {
        int rows[kHarmSlots];
        for (int s = 0; s < kHarmSlots; ++s)
            rows[s] = m_slots[s].on ? int(std::lround(m_slots[s].row)) : -1;
        return (m_follow ? kFollow : 0) | (m_gate << kGateShift) | packRows(rows);
    }

    void param(ControlContext &ctx, int which, int slot, float a, float b, double t) {
        Command c;
        c.type = Command::Param;
        c.t = t;
        c.i[0] = which;
        c.i[1] = slot;
        c.f[0] = a;
        c.f[1] = b;
        ctx.send(c);
    }

    void off(ControlContext &ctx, int id, double t) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        c.t = t;
        ctx.send(c);
    }

    // The loop gesture listens for a riff: short, detached notes. A note
    // goes into the phrase when the next one lands (so the note being held
    // is never part of the phrase it might close), and a long or legato
    // note means the player is singing a line, not tapping a riff: the
    // phrase starts over. That keeps a melody that happens to come back to
    // its first note on the beat from turning into a loop.
    void pushLanding(int abs, double t, int id) {
        int held = 0;
        for (const auto &h : m_held)
            held += h.id >= 0 && h.id != id && h.role == Role::Note && !h.looped;
        for (const auto &k : m_keys)
            held += k.down && k.id != id;
        if (m_pendingTap.id >= 0) {
            const double len = m_pendingTap.lifted - m_pendingTap.t;
            if (m_pendingTap.lifted >= 0 && len < 0.45 && !m_pendingTap.legato && held == 0)
                m_phrase.tap(m_pendingTap.t, std::max(0.05, len), m_pendingTap.abs);
            else
                m_phrase.clear();
        }
        m_pendingTap = {id, abs, t, -1, held > 0};
    }
    void liftLanding(int id, double t) {
        if (m_pendingTap.id == id && m_pendingTap.lifted < 0)
            m_pendingTap.lifted = t;
    }

    void land(Held &h, const Touch &t, double now, ControlContext &ctx) {
        h = Held{};
        h.id = t.id;
        h.t0 = now;
        h.y0 = t.y;
        h.angle0 = t.angle;
        h.lastSend = now;
        if (t.x >= kColumn) {
            h.role = Role::Note;
            const float pos = neckPosition(t.x, m_per);
            h.q = std::clamp(int(std::lround(pos)), 0, neckCells(m_per) - 1);
            h.motion.update(pos, now);
            h.abs = (m_neckOct + m_shift) * m_per + h.q;
            Command on;
            on.type = Command::On;
            on.id = t.id;
            on.t = now;
            on.i[0] = ((h.abs % m_per) + m_per) % m_per;
            on.i[1] = (h.abs - on.i[0]) / m_per;
            on.i[2] = m_sound;
            on.i[3] = flags();
            on.f[0] = clampf(0.6f + 0.5f * t.pressure, 0.3f, 1.0f);
            fillNote(on, h, t, pos, 0, 0);
            ctx.send(on);
            std::copy(on.f + 1, on.f + 7, h.sent + 1);
            pushLanding(h.abs, now, t.id);
            return;
        }
        if (t.y >= kHarmBottom) {
            // A free voice first; with the harmony held, a held-over one next.
            int slot = -1;
            for (int s = 0; s < kHarmSlots && slot < 0; ++s)
                if (!m_slots[s].on)
                    slot = s;
            for (int s = 0; s < kHarmSlots && slot < 0; ++s)
                if (!m_slots[s].held)
                    slot = s;
            if (slot < 0)
                return; // three voices of harmony is all there is; this finger rests
            h.role = Role::Harmony;
            h.slot = slot;
            const float row = harmonyRow(t.y);
            h.motion.update(row, now);
            Slot &s = m_slots[slot];
            s.held = s.on = true;
            s.row = std::round(row);
            s.level = clampf(0.55f + 0.6f * t.pressure, 0.4f, 1.0f);
            h.sent[0] = s.row;
            h.sent[1] = s.level;
            param(ctx, PHarmony, slot, s.row, s.level, now);
            return;
        }
        h.role = t.x < kBendSplit ? Role::Bend : Role::Throw;
        if (h.role == Role::Throw) {
            const float a = throwAmount(t);
            h.sent[0] = a;
            param(ctx, PThrow, 0, a, 0, now);
        }
    }

    void follow(Held &h, const Touch &t, double now, ControlContext &ctx) {
        const bool due = now - h.lastSend >= 0.0155;
        switch (h.role) {
        case Role::Note: {
            if (h.looped)
                return;
            const float pos = neckPosition(t.x, m_per);
            const int cells = neckCells(m_per);
            if (std::fabs(pos - float(h.q)) > 0.62f)
                h.q = std::clamp(int(std::lround(pos)), 0, cells - 1);
            const float m = h.motion.update(pos, now);
            // A twist is measured from how the finger landed; the contact's
            // angle only means anything for a finger that isn't round.
            float twist = 0;
            if (t.eccentricity > 0.2f) {
                float d = t.angle - h.angle0;
                while (d > kPi / 2)
                    d -= kPi;
                while (d < -kPi / 2)
                    d += kPi;
                const float mag = clampf((std::fabs(d) - 0.2f) / 0.45f, 0.0f, 1.0f);
                // Steep in the middle, so a twist sits on the step or off it.
                const float s = mag * mag * (3.0f - 2.0f * mag);
                twist = d < 0 ? s : -s; // clockwise bends up
            }
            // And like the slide, a twist held still rests on a whole step:
            // a finger that happens to sit a little turned stays in tune.
            twist = rounded(twist, h.twistMotion.update(twist * 4.0f, now));
            const int abs = (m_neckOct + m_shift) * m_per + h.q;
            Command mv;
            mv.type = Command::Move;
            mv.id = t.id;
            mv.t = now;
            mv.i[0] = ((abs % m_per) + m_per) % m_per;
            mv.i[1] = (abs - mv.i[0]) / m_per;
            fillNote(mv, h, t, pos, m, twist);
            float change = 0;
            for (int k = 1; k < 7; ++k)
                change = std::max(change, std::fabs(mv.f[k] - h.sent[k]));
            if (abs != h.abs || (due && change > 0.005f)) {
                h.abs = abs;
                h.lastSend = now;
                std::copy(mv.f + 1, mv.f + 7, h.sent + 1);
                ctx.send(mv);
            }
            // Tap a phrase, then land on its first note again on the beat
            // and stay: the phrase becomes a loop.
            if (!h.long_ && now - h.t0 >= 0.3) {
                h.long_ = true;
                double start = 0;
                int notesHeld = 0;
                for (auto &o : m_held)
                    notesHeld += o.id >= 0 && o.role == Role::Note;
                const auto &taps = m_phrase.taps();
                if (notesHeld == 1 && m_pendingTap.id == h.id && !m_pendingTap.legato && taps.size() >= 3 && taps.front().group == h.abs &&
                    m_phrase.closes(h.t0, h.abs, &start)) {
                    const int slot = std::max(0, ctx.freeSlot());
                    ctx.capture(slot, start, h.t0);
                    ctx.notice(QStringLiteral("Melody loops in slot %1").arg(slot + 1));
                    // The loop plays this note now; the finger lets go of it.
                    off(ctx, h.id, now);
                    h.looped = true;
                    m_phrase.clear();
                    m_pendingTap = {};
                }
            }
            return;
        }
        case Role::Harmony: {
            const float row = harmonyRow(t.y);
            const float m = h.motion.update(row, now);
            Slot &s = m_slots[h.slot];
            s.row = rounded(row, m);
            s.level = clampf(0.55f + 0.6f * t.pressure, 0.4f, 1.0f);
            if (due && (std::fabs(s.row - h.sent[0]) > 0.005f || std::fabs(s.level - h.sent[1]) > 0.01f)) {
                h.sent[0] = s.row;
                h.sent[1] = s.level;
                h.lastSend = now;
                param(ctx, PHarmony, h.slot, s.row, s.level, now);
            }
            return;
        }
        case Role::Bend: {
            // About eight millimetres a step on a real pad.
            const float raw = clampf((t.y - h.y0) / 0.075f, -3.0f, 3.0f);
            const float m = h.motion.update(raw, now);
            const float bend = rounded(raw, m);
            h.shown = bend;
            if (due && std::fabs(bend - h.sent[0]) > 0.005f) {
                h.sent[0] = bend;
                h.lastSend = now;
                param(ctx, PBend, 0, bend, 0, now);
            }
            return;
        }
        case Role::Throw: {
            const float a = throwAmount(t);
            if (due && std::fabs(a - h.sent[0]) > 0.01f) {
                h.sent[0] = a;
                h.lastSend = now;
                param(ctx, PThrow, 0, a, 0, now);
            }
            return;
        }
        case Role::None:
            return;
        }
    }

    void lift(Held &h, double now, ControlContext &ctx) {
        switch (h.role) {
        case Role::Note:
            if (!h.looped)
                off(ctx, h.id, now);
            liftLanding(h.id, now);
            break;
        case Role::Harmony: {
            Slot &s = m_slots[h.slot];
            s.held = false;
            if (!m_latch) {
                s.on = false;
                param(ctx, PHarmony, h.slot, s.row, 0, now);
            } else {
                s.row = std::round(s.row);
                param(ctx, PHarmony, h.slot, s.row, s.level, now);
            }
            break;
        }
        case Role::Bend:
            if (h.sent[0] != 0)
                param(ctx, PBend, 0, 0, 0, now);
            break;
        case Role::Throw:
            param(ctx, PThrow, 0, 0, 0, now);
            break;
        case Role::None:
            break;
        }
        h.id = -1;
    }

    void fillNote(Command &c, const Held &h, const Touch &t, float pos, float motion, float twist) const {
        c.f[1] = t.y;
        c.f[2] = pos - float(h.q);
        c.f[3] = motion;
        c.f[4] = clampf((t.size - 1.0f) / 0.8f, 0.0f, 1.0f);
        c.f[5] = twist;
        c.f[6] = t.pressure;
    }

    static float harmonyRow(float y) {
        return clampf((y - kHarmBottom) / (1.0f - kHarmBottom) * float(kRows) - 0.5f, 0.0f, float(kRows - 1));
    }
    static float throwAmount(const Touch &t) {
        return clampf((0.45f + 0.55f * t.y / kHarmBottom) * (0.75f + 0.5f * t.pressure), 0.0f, 1.0f);
    }

    // The piano rows: each a run of the scale from a root, an octave apart.
    // The keys past L and P differ between layouts, so both the US ones and
    // the Nordic ones (this is often played on a Swedish Mac keyboard) play
    // the same notes.
    static int pianoIndex(int key) {
        static const int rows[3][12] = {
            {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_N, Qt::Key_M, Qt::Key_Comma,
             Qt::Key_Period, Qt::Key_Slash, 0, 0},
            {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L,
             Qt::Key_Semicolon, Qt::Key_Apostrophe, 0},
            {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O,
             Qt::Key_P, Qt::Key_BracketLeft, Qt::Key_BracketRight}};
        static const int nordic[3][12] = {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, Qt::Key_Minus, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, Qt::Key_Odiaeresis, Qt::Key_Adiaeresis, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, Qt::Key_Aring, 0}};
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 12; ++i)
                if ((rows[r][i] && rows[r][i] == key) || (nordic[r][i] && nordic[r][i] == key))
                    return r * 12 + i;
        return -1;
    }
    int pianoDegree(int key) const {
        const int idx = pianoIndex(key);
        if (idx < 0)
            return -1;
        return (idx / 12) * m_per + idx % 12;
    }

    QString describe() const;

    std::array<Held, 16> m_held{};
    std::array<KeyHeld, 36> m_keys{};
    Slot m_slots[kHarmSlots];
    struct {
        int id = -1, abs = 0;
        double t = 0, lifted = -1;
        bool legato = false;
    } m_pendingTap;
    Phrase m_phrase;
    Frame m_frame;
    int m_per = 7, m_neckOct = 1, m_shift = 0, m_sound = 0, m_glide = 0, m_gate = 0;
    bool m_follow = false, m_latch = false, m_click = false, m_recording = false, m_overdub = false;
    QString m_status;
    std::array<TrailPoint, kTrail> m_trail{};
    long m_trailHead = 0;
    double m_lastSample = 0;
};

void LeadControl::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const StudioView &sv = *pc.studio;
    const PartView &pv = *pc.view;
    const QColor col = pc.color;
    Tuning tuning;
    tuning.set(sv.root, sv.family, sv.brightness, sv.just);
    const int per = tuning.degreesPerOctave();
    const int cells = neckCells(per);
    const int neckBase = (neckOctave(sv.root) + m_shift) * per;
    const qreal unit = pc.pad.height() / 600.0;
    auto midiOf = [&](double degree) { return tuning.baseMidi() + tuning.continuousSemis(degree); };
    auto xOf = [&](float absDegree) { return neckX(absDegree - float(neckBase), per); };
    // A line sung above or below the neck (an octave leap, a high harmony)
    // is drawn folded back onto it by octaves, fainter, so it never just
    // disappears.
    auto fold = [&](float absDegree, int *octaves) {
        float q = absDegree - float(neckBase);
        int o = 0;
        while (q > float(cells) - 0.5f && o < 4) {
            q -= float(per);
            ++o;
        }
        while (q < -0.5f && o > -4) {
            q += float(per);
            --o;
        }
        *octaves = o;
        return q + float(neckBase);
    };

    c::pad(p, pc, QStringLiteral("Lead"));
    p.setRenderHint(QPainter::Antialiasing, true);

    // --- the neck ---
    const QRectF neck = pc.rect(kColumn, 0, 1, 1);
    const bool follow = m_follow;
    auto isChordTone = [&](int degree) {
        for (int k = 0; k < sv.chordCount; ++k)
            if (((degree - sv.chordTones[k]) % per + per) % per == 0)
                return true;
        return false;
    };
    // Columns first, unsmoothed: they sit on whole pixels anyway, and the
    // rasteriser's antialiased path is several times slower on big fills.
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int k = 0; k < cells; ++k) {
        const int abs = neckBase + k;
        const QRectF cell(neck.left() + neck.width() * k / cells, neck.top(), neck.width() / cells, neck.height());
        if (((abs % per) + per) % per == 0)
            p.fillRect(cell, c::alpha(col, 0.09f));
        if (follow && isChordTone(abs))
            p.fillRect(cell, c::alpha(col, 0.07f));
    }
    p.setPen(QPen(c::alpha(col, 0.15f), 1));
    for (int k = 1; k < cells; ++k) {
        const qreal x = std::round(neck.left() + neck.width() * k / cells) + 0.5;
        p.drawLine(QPointF(x, neck.top()), QPointF(x, neck.bottom()));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    // The note names, and over them a mark on each chord tone: the safe
    // places to land.
    p.setFont(c::font(int(11 * unit + 1), true));
    for (int k = 0; k < cells; ++k) {
        const int abs = neckBase + k;
        const QRectF cell(neck.left() + neck.width() * k / cells, neck.top(), neck.width() / cells, neck.height());
        const bool tone = isChordTone(abs);
        if (tone) {
            const QRectF mark(cell.center().x() - 3 * unit - 1, cell.bottom() - 36 * unit, 6 * unit + 2, 6 * unit + 2);
            p.setPen(Qt::NoPen);
            p.setBrush(c::alpha(col, follow ? 0.9f : 0.45f));
            p.drawEllipse(mark);
        }
        p.setPen(c::alpha(col, follow && !tone ? 0.3f : 0.75f));
        p.drawText(QRectF(cell.left(), cell.bottom() - 24 * unit, cell.width(), 20 * unit), Qt::AlignCenter,
                   QString::fromUtf8(Tuning::noteName(int(std::lround(midiOf(abs))))));
    }
    // The height's meaning, on the right edge.
    {
        const bool vox = sound(m_sound).formant;
        const QRectF r(neck.right() - 70 * unit, neck.top() + 6 * unit, 64 * unit, 16 * unit);
        c::text(p, r, vox ? QStringLiteral("ee") : QStringLiteral("bright"), c::alpha(col, 0.4f), int(10 * unit + 1),
                false, Qt::AlignRight | Qt::AlignVCenter);
        c::text(p, r.translated(0, neck.height() * 0.5 - 14 * unit), vox ? QStringLiteral("ah") : QString(),
                c::alpha(col, 0.4f), int(10 * unit + 1), false, Qt::AlignRight | Qt::AlignVCenter);
        c::text(p, r.translated(0, neck.height() - 64 * unit), vox ? QStringLiteral("oo") : QStringLiteral("dark"),
                c::alpha(col, 0.4f), int(10 * unit + 1), false, Qt::AlignRight | Qt::AlignVCenter);
    }

    // The trail: the last few seconds of what each line sang, rising from
    // the note names as it ages. Glides are curves, vibrato is a ripple. It
    // is drawn as a few polylines, one per stretch of a tenth of the span,
    // each as bright and thick as it was loud: thousands of separate strokes
    // would hold the session's lock for milliseconds every frame.
    {
        p.save();
        p.setClipRect(neck.adjusted(1, 1, -1, -1));
        const double span = 3.2;
        const int buckets = 10;
        const double now = pc.time;
        const qreal floorY = neck.bottom() - 40 * unit, rise = neck.height() - 40 * unit;
        auto xAt = [&](float pos) { return neck.left() + (xOf(pos) - kColumn) / (1 - kColumn) * neck.width(); };
        for (int k = 1; k >= 0; --k) {
            const QColor lc = k == 0 ? col : c::alpha(col, 0.55f);
            for (int b = 0; b < 1 + kHarmSlots; ++b) {
                QPolygonF poly;
                float levelSum = 0;
                int bucket = -1, octaves = 0;
                auto flush = [&] {
                    if (poly.size() >= 2) {
                        const float la = levelSum / float(poly.size());
                        const float fade = float(1.0 - (bucket + 0.5) / buckets) * (octaves ? 0.45f : 1.0f);
                        const float w = (b == 0 ? 2.0f : 1.2f) + (b == 0 ? 7.0f : 3.0f) * std::min(1.0f, la);
                        p.setPen(QPen(c::alpha(lc, fade * (b == 0 ? 0.85f : 0.5f) * std::min(1.0f, 0.4f + la)),
                                      w * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                        p.drawPolyline(poly);
                    }
                    poly.clear();
                    levelSum = 0;
                };
                for (int i = 1; i < kTrail; ++i) {
                    const TrailPoint &a = m_trail[size_t((m_trailHead - i + kTrail) % kTrail)];
                    if (a.t < 0 || a.t > now + 0.05)
                        break;
                    const double age = std::max(0.0, now - a.t);
                    if (age > span)
                        break;
                    const float level = a.level[k][b];
                    int oct = 0;
                    const float pos = fold(a.pos[k][b], &oct);
                    const int bk = std::min(buckets - 1, int(age / span * buckets));
                    const QPointF pt(xAt(pos), floorY - rise * age / span);
                    // A silence, a jump, a new octave or a new stretch ends a stroke;
                    // a new stretch starts where the last one ended, so it stays whole.
                    const bool gap = level < 0.01f || (!poly.isEmpty() && (oct != octaves ||
                                                                          std::fabs(pt.x() - poly.last().x()) > neck.width() * 0.2));
                    if (gap) {
                        flush();
                        if (level < 0.01f)
                            continue;
                    } else if (bk != bucket && !poly.isEmpty()) {
                        const QPointF joint = poly.last();
                        flush();
                        poly << joint;
                    }
                    bucket = bk;
                    octaves = oct;
                    poly << pt;
                    levelSum += level;
                }
                flush();
            }
        }
        p.restore();
    }

    // Where each line sings right now: a bright string across the neck.
    for (int k = 1; k >= 0; --k) {
        const int line = k == 0 ? 0 : (pv.playing >= 0 ? 1 + pv.playing : -1);
        if (line < 0)
            continue;
        const float *d = pv.data + 16 * line;
        if (d[0] <= 0 || d[2] < 0.005f)
            continue;
        auto string = [&](float pos, float level, float width) {
            int octaves = 0;
            pos = fold(pos, &octaves);
            if (octaves)
                level *= 0.5f;
            const qreal x = neck.left() + (xOf(pos) - kColumn) / (1 - kColumn) * neck.width();
            QLinearGradient g(QPointF(x - 14 * unit, 0), QPointF(x + 14 * unit, 0));
            g.setColorAt(0, c::alpha(col, 0));
            g.setColorAt(0.5, c::alpha(col, (k == 0 ? 0.35f : 0.2f) * std::min(1.0f, level * 1.5f)));
            g.setColorAt(1, c::alpha(col, 0));
            p.setRenderHint(QPainter::Antialiasing, false);
            p.fillRect(QRectF(x - 14 * unit, neck.top(), 28 * unit, neck.height()), g);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(c::alpha(col, std::min(1.0f, 0.3f + level)), width * unit,
                          octaves ? Qt::DashLine : Qt::SolidLine));
            p.drawLine(QPointF(x, neck.top()), QPointF(x, neck.bottom() - 40 * unit));
        };
        string(d[1], d[2], k == 0 ? 2.5f : 1.5f);
        for (int s = 0; s < kHarmSlots; ++s)
            if (d[7 + s] > 0.02f)
                string(d[4 + s], d[2] * d[7 + s], 1.2f);
    }

    // --- the left hand's column ---
    const QRectF column = pc.rect(0, 0, kColumn, 1);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(column, QColor(10, 7, 0));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c::alpha(col, 0.7f), 1.5));
    p.drawLine(QPointF(column.right(), column.top()), QPointF(column.right(), column.bottom()));
    const float *live = pv.data;
    for (int row = 0; row < kRows; ++row) {
        const float y0 = kHarmBottom + (1 - kHarmBottom) * float(row) / kRows;
        const float y1 = kHarmBottom + (1 - kHarmBottom) * float(row + 1) / kRows;
        const QRectF r = pc.rect(0, y0, kColumn, y1).adjusted(6 * unit, 4 * unit, -6 * unit, -4 * unit);
        bool lit = false;
        float level = 0;
        for (int s = 0; s < kHarmSlots; ++s) {
            if (m_slots[s].on && int(std::lround(m_slots[s].row)) == row) {
                lit = true;
                level = std::max(level, live[7 + s]);
            }
        }
        // A lit row glows as brightly as its voice sounds, with a solid bar
        // at its edge; the finger on it stays visible.
        p.setPen(QPen(c::alpha(col, lit ? 1.0f : 0.35f), 1.5, lit && m_latch ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(lit ? QBrush(c::alpha(col, 0.18f + 0.3f * std::min(1.0f, level * 1.5f))) : Qt::NoBrush);
        p.drawRect(r);
        if (lit)
            p.fillRect(QRectF(r.left(), r.top(), 6 * unit, r.height()), col);
        c::text(p, r.adjusted(14 * unit, 0, 0, 0), QString::fromUtf8(rowName(row)), lit ? col : c::alpha(col, 0.7f),
                int(15 * unit + 1), true, Qt::AlignLeft | Qt::AlignVCenter);

    }
    // BEND: a wheel's slot with the bend drawn from its centre.
    {
        const QRectF r = pc.rect(0, 0, kBendSplit, kHarmBottom).adjusted(6 * unit, 6 * unit, -3 * unit, -6 * unit);
        p.setPen(QPen(c::alpha(col, 0.6f), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        const float bend = live[10];
        const qreal mid = r.center().y();
        const qreal step = (r.height() - 30 * unit) / 6.0;
        p.setPen(QPen(c::alpha(col, 0.3f), 1));
        for (int s = -3; s <= 3; ++s)
            p.drawLine(QPointF(r.left() + r.width() * 0.3, mid - s * step), QPointF(r.right() - r.width() * 0.3, mid - s * step));
        if (std::fabs(bend) > 0.01f) {
            const QRectF bar(r.left() + r.width() * 0.25, std::min(mid, mid - bend * step), r.width() * 0.5,
                             std::fabs(bend * step));
            p.fillRect(bar, c::alpha(col, 0.8f));
        }
        p.setPen(QPen(col, 2));
        p.drawLine(QPointF(r.left() + 4 * unit, mid - bend * step), QPointF(r.right() - 4 * unit, mid - bend * step));
        c::text(p, QRectF(r.left(), r.bottom() - 20 * unit, r.width(), 18 * unit), QStringLiteral("bend"),
                c::alpha(col, 0.8f), int(12 * unit + 1), true, Qt::AlignCenter);
    }
    // THROW: how hard, and the echo it leaves.
    {
        const QRectF r = pc.rect(kBendSplit, 0, kColumn, kHarmBottom).adjusted(3 * unit, 6 * unit, -6 * unit, -6 * unit);
        const float amt = live[11];
        const float echo = std::min(1.0f, pv.data[160] * 2.0f);
        p.setPen(QPen(c::alpha(col, amt > 0 ? 1.0f : 0.6f), 1.5));
        p.setBrush(amt > 0 ? QBrush(c::alpha(col, 0.15f + 0.4f * amt)) : Qt::NoBrush);
        p.drawRect(r);
        const QRectF meter(r.left() + r.width() * 0.35, r.top() + 8 * unit, r.width() * 0.3, r.height() - 36 * unit);
        const int segs = 12;
        for (int s = 0; s < segs; ++s) {
            const qreal h = meter.height() / segs;
            const QRectF seg(meter.left(), meter.bottom() - (s + 1) * h + 1, meter.width(), h - 2);
            p.fillRect(seg, float(s) / segs < echo ? col : c::alpha(col, 0.12f));
        }
        c::text(p, QRectF(r.left(), r.bottom() - 20 * unit, r.width(), 18 * unit), QStringLiteral("throw"),
                c::alpha(col, 0.8f), int(12 * unit + 1), true, Qt::AlignCenter);
    }

    // Modes, at the neck's top left.
    {
        qreal x = neck.left() + 10 * unit;
        const qreal y = neck.top() + 16 * unit;
        auto mode = [&](const QString &s, bool on) {
            if (on) {
                const QRectF r = c::tag(p, QPointF(x, y), s, col, int(11 * unit + 1));
                x = r.right() + 6 * unit;
            }
        };
        mode(QStringLiteral("follow chord"), m_follow);
        mode(m_shift > 0 ? QStringLiteral("oct +1") : QStringLiteral("oct −1"), m_shift != 0);
        mode(QStringLiteral("glide ×%1").arg(double(std::exp2(float(m_glide)))), m_glide != 0);
        mode(QStringLiteral("gate %1").arg(QString::fromUtf8(gateName(m_gate))), m_gate != 0);
        mode(QStringLiteral("8va"), m_click);
        mode(QStringLiteral("harmony held"), m_latch);
        if (live[14] > 0.02f)
            mode(QStringLiteral("vibrato"), true);
    }

    // Fingers, each saying what it does.
    int topNote = -1;
    double topT = -1;
    for (const auto &h : m_held)
        if (h.id >= 0 && h.role == Role::Note && !h.looped && h.t0 > topT) {
            topT = h.t0;
            topNote = h.id;
        }
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        const Held *h = nullptr;
        for (const auto &o : m_held)
            if (o.id == t.id)
                h = &o;
        QString label;
        float glow = 0;
        if (h) {
            switch (h->role) {
            case Role::Note:
                if (h->looped) {
                    label = QStringLiteral("loop");
                } else if (t.id == topNote) {
                    label = live[0] > 0 ? noteName(midiOf(live[1])) : QString();
                    glow = std::min(1.0f, 0.3f + live[2]);
                } else {
                    label = noteName(midiOf(h->abs));
                }
                break;
            case Role::Harmony:
                // The row says the interval; the finger says the note it makes.
                label = live[0] > 0 && live[7 + h->slot] > 0.05f ? noteName(midiOf(live[4 + h->slot]))
                                                                  : QString::fromUtf8(rowName(int(std::lround(m_slots[h->slot].row))));
                break;
            case Role::Bend:
                label = QStringLiteral("%1%2").arg(h->shown >= 0 ? "+" : "").arg(double(h->shown), 0, 'f', 1);
                break;
            case Role::Throw:
                label = QStringLiteral("throw");
                glow = 0.6f;
                break;
            case Role::None:
                break;
            }
        }
        c::finger(p, pc, t, h && h->role == Role::Note && t.id != topNote && !h->looped ? c::alpha(col, 0.6f) : col,
                  label, glow);
    }
    c::beats(p, pc, col);
    c::clips(p, QRectF(pc.pad.left(), pc.pad.bottom() + 14, pc.pad.width(), 30), pv, col, pc.time);
    // The legend again, over the column, and the sounds cut into the top
    // edge after it.
    {
        const QFont legend = c::font(13, true);
        const qreal lw = QFontMetricsF(legend).horizontalAdvance(QStringLiteral("Lead")) + 16;
        const QRectF lr(pc.pad.left() + 16, pc.pad.top() - 9, lw, 18);
        p.fillRect(lr, QColor(0, 0, 0));
        c::text(p, lr, QStringLiteral("Lead"), col, 13, true, Qt::AlignCenter);
        qreal x = lr.right() + 12;
        for (int s = 0; s < kSounds; ++s) {
            const QString name = QString::fromUtf8(sound(s).name);
            if (s == m_sound) {
                const QRectF r = c::tag(p, QPointF(x, pc.pad.top()), name, col, 12);
                x = r.right() + 8;
            } else {
                const QFont f = c::font(12, true);
                const qreal w = QFontMetricsF(f).horizontalAdvance(name) + 12;
                p.fillRect(QRectF(x, pc.pad.top() - 9, w, 18), QColor(0, 0, 0));
                c::text(p, QRectF(x, pc.pad.top() - 9, w, 18), name, c::alpha(col, 0.55f), 12, true, Qt::AlignCenter);
                x += w + 8;
            }
        }
    }

}

QString LeadControl::describe() const {
    QStringList parts;
    parts << QString::fromUtf8(sound(m_sound).name);
    int notes = 0;
    for (const auto &h : m_held)
        notes += h.id >= 0 && h.role == Role::Note && !h.looped;
    for (const auto &k : m_keys)
        notes += k.down;
    if (notes == 1)
        parts << QStringLiteral("singing");
    else if (notes > 1)
        parts << QStringLiteral("legato, %1 held").arg(notes);
    QStringList harm;
    for (const auto &s : m_slots)
        if (s.on)
            harm << QString::fromUtf8(rowName(int(std::lround(s.row))));
    if (!harm.isEmpty())
        parts << QStringLiteral("harmony ") + harm.join(QStringLiteral(" "));
    for (const auto &h : m_held) {
        if (h.id < 0)
            continue;
        if (h.role == Role::Bend)
            parts << QStringLiteral("bend %1").arg(double(h.shown), 0, 'f', 1);
        if (h.role == Role::Throw)
            parts << QStringLiteral("throwing");
    }
    if (m_click)
        parts << QStringLiteral("an octave up");
    if (m_follow)
        parts << QStringLiteral("on the chord");
    if (m_gate)
        parts << QStringLiteral("gated ") + QString::fromUtf8(gateName(m_gate));
    return parts.join(QStringLiteral(" · "));
}

} // namespace lead

PartSpec makeLead() {
    PartSpec s;
    s.name = QStringLiteral("Lead");
    s.blurb = QStringLiteral("melody, harmony, glide");
    s.color = QColor("#FFB000");
    s.engine = std::make_unique<lead::LeadEngine>();
    s.control = std::make_unique<lead::LeadControl>();
    // The lead has its own echo; the room's tape echo only adds a little air.
    const float channel[ChannelCount] = {0.75f, 0.5f, 0.5f, 0.28f, 0.08f, 0.35f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
