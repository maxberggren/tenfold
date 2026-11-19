#include "engine.h"

#include <chrono>
#include <cstring>

#include "electro.h"
#include "fx.h"

namespace tf {

namespace {

struct RoleInfo {
    const char *name, *slide, *twist;
    float slideDefault, twistDefault;
};

// The order fingers take roles in is the order a producer reaches for them:
// the filter first, then the shape of the sound, its envelope, the room, the
// pump.
const RoleInfo roles[RoleCount] = {
    {"Filter", "cutoff", "resonance", 0.62f, 0.22f},
    {"Env", "amount", "decay", 0.45f, 0.35f},
    {"Width", "unison", "sub", 0.4f, 0.55f},
    {"Drive", "drive", "fold", 0.12f, 0.0f},
    {"Pump", "sidechain", "arp rate", 0.5f, 0.7f},
    {"Echo", "echo", "time", 0.15f, 0.62f},
    {"Space", "room", "shimmer", 0.3f, 0.0f},
    {"Glide", "glide", "swing", 0.1f, 0.15f},
    {"Wobble", "depth", "rate", 0.0f, 0.5f},
    {"Gravity", "pull", "mode", 1.0f, 0.5f},
};

const char *rows[RowCount] = {"drums", "bass", "chords", "lead", "arp"};

constexpr int kSynths = 40;
constexpr int kDrums = 48;
constexpr int kPending = 512;
constexpr int kRolls = 8;

}

// Filter, Env, Width and Wobble belong to a row: the lead's filter is not
// the bass's. The rest act on the whole mix.
bool rowRole(int role) { return role == Filter || role == Env || role == Width || role == Wobble; }

float rowDefault(int row, int role, int axis) {
    static const float d[RowCount][4][2] = {
        // Filter      Env           Width         Wobble
        {{1.0f, 0.1f}, {0.5f, 0.5f}, {0.5f, 0.0f}, {0.0f, 0.5f}},     // drums: the filter is a DJ filter
        {{0.42f, 0.35f}, {0.55f, 0.28f}, {0.25f, 0.7f}, {0.0f, 0.5f}}, // bass
        {{0.55f, 0.12f}, {0.25f, 0.65f}, {0.65f, 0.0f}, {0.0f, 0.5f}}, // chords
        {{0.62f, 0.2f}, {0.45f, 0.4f}, {0.6f, 0.15f}, {0.0f, 0.5f}},   // lead
        {{0.58f, 0.25f}, {0.5f, 0.22f}, {0.45f, 0.0f}, {0.0f, 0.5f}},  // arp
    };
    if (!rowRole(role))
        return roleDefault(role, axis);
    const int k = role == Filter ? 0 : role == Env ? 1 : role == Width ? 2 : 3;
    return d[std::clamp(row, 0, RowCount - 1)][k][axis];
}

const char *roleName(int r) { return roles[std::clamp(r, 0, RoleCount - 1)].name; }
const char *roleSlide(int r) { return roles[std::clamp(r, 0, RoleCount - 1)].slide; }
const char *roleTwist(int r) { return roles[std::clamp(r, 0, RoleCount - 1)].twist; }
float roleDefault(int r, int axis) {
    const RoleInfo &i = roles[std::clamp(r, 0, RoleCount - 1)];
    return axis == 0 ? i.slideDefault : i.twistDefault;
}
const char *rowName(int r) { return rows[std::clamp(r, 0, RowCount - 1)]; }

struct Hit {
    double offset; // samples from the loop's one
    int row, value; // value: scale degree, or drum piece on the drum row
    float vel, x;
    double length; // samples the note was held
};

struct Loop {
    bool active = false, ready = false, held = false, pinned = false;
    int id = -1;
    double length = 0;  // samples
    double anchor = 0;  // absolute sample of the one
    Hit hits[kMaxLoopHits];
    int count = 0;
    int transpose = 0;
    float shift = 0, density = 0, intensity = 1;
    float level = 1, fade = 1, fadeRate = 0;
    int cycle = 0;
    float homeX = 0.5f, homeY = 0.5f, flash = 0;
    uint64_t created = 0;
    uint32_t seed = 1;
    bool fired[kMaxLoopHits] = {};
    struct Lane {
        bool used = false;
        int role = -1, row = LeadRow;
        float midi[kLaneBins], press[kLaneBins], ka[kLaneBins], kb[kLaneBins], x[kLaneBins];
        int deg[kLaneBins];
        uint8_t on[kLaneBins];
        uint64_t recordingUntil = 0;
        int lastIdx = -1;
    } lanes[kLanes];
};

// A note or hit due at an exact sample: loop hits, rolls, ratchets, and the
// note-offs of loop notes.
struct Pending {
    double at;
    int row, value;
    float vel, x, gain, wet;
    double length;
    int finger; // > 0: a note-off for that synthetic finger
};

struct Roll {
    bool on = false;
    int finger = -1, piece = 0;
    float pressure = 0.5f, x = 0.5f;
    double next = 0;
};

struct Engine::Impl {
    float rate;
    Tuning tuning;
    SynthVoice synths[kSynths];
    DrumVoice drums[kDrums];
    Loop loops[kMaxLoops];
    Pending pending[kPending];
    int pendingCount = 0;
    Roll rolls[kRolls];
    int nextSynthetic = 1 << 20; // fingers for loop notes, far above real ids

    // Knobs per row; the mix-wide ones hold the same value on every row.
    float knobTarget[RowCount][RoleCount][2];
    float knob[RowCount][RoleCount][2];
    float knobSmooth = 0;
    uint64_t lastKnobCmd[RowCount][RoleCount] = {};
    float override_[RowCount][RoleCount][2] = {};
    float overrideWeight[RowCount][RoleCount] = {};

    double bar = 0;
    double barAnchor = 0;
    bool tempoSet = false;
    float lastChordCenter = 60.0f;

    // Sidechain: the envelope a kick sets, ramped per sample.
    float duck = 0, duckTarget = 0;
    int duckAt = -1; // sample in the coming block where a kick lands
    Svf drumFilterL, drumFilterR;
    float damperTarget = 0, damper = 0;

    Reverb reverb;
    TapeEcho echo;
    GrainCloud swarm;
    Limiter limiter;
    Saturator gritL, gritR;
    Svf damperL, damperR;
    Svf sideLp, lowBand;
    float lowEnv = 0;
    float glueEnv = 0;
    DcBlock dcL, dcR;
    int hitsFired = 0;

    float bufDrumL[64], bufDrumR[64], bufMusicL[64], bufMusicR[64], bufSendL[64], bufSendR[64];

    float cpu = 0, cpuPeak = 0;
    float peakBlock = 0, rmsBlock = 0;
    uint64_t lastPublish = 0;
};

static double floorMod(double a, double m) {
    const double r = std::fmod(a, m);
    return r < 0 ? r + m : r;
}

static SynthParams currentParams(const float knob[RoleCount][2]) {
    SynthParams p;
    p.cutoff = knob[Filter][0];
    p.res = knob[Filter][1] * 0.95f;
    p.envAmt = knob[Env][0];
    p.envDecay = knob[Env][1];
    p.width = knob[Width][0];
    p.sub = knob[Width][1];
    p.wobbleDepth = knob[Wobble][0];
    p.glide = 0.004f + 0.3f * knob[Glide][0] * knob[Glide][0];
    return p;
}

static Patch patchFor(int row) {
    switch (row) {
    case BassRow: return Patch::Bass;
    case ChordRow: return Patch::Chord;
    case ArpRow: return Patch::Arp;
    default: return Patch::Lead;
    }
}

Engine::Engine(float sampleRate) : m_rate(sampleRate), d(std::make_unique<Impl>()) {
    d->rate = sampleRate;
    for (int i = 0; i < kSynths; ++i)
        d->synths[i].init(0x1234567u + uint32_t(i) * 7919u);
    for (int i = 0; i < kDrums; ++i)
        d->drums[i].rng = Rng(0xBEEF + uint32_t(i) * 104729u);
    for (int row = 0; row < RowCount; ++row)
        for (int r = 0; r < RoleCount; ++r)
            for (int a = 0; a < 2; ++a)
                d->knob[row][r][a] = d->knobTarget[row][r][a] = rowDefault(row, r, a);
    d->knobSmooth = 1.0f - std::exp(-64.0f / (0.05f * sampleRate));
    d->reverb.init(sampleRate);
    d->echo.init(sampleRate);
    d->swarm.init(sampleRate);
    d->limiter.init(sampleRate);
    d->sideLp.set(120.0f, 0.7f, sampleRate);
    d->lowBand.set(150.0f, 0.7f, sampleRate);
    d->bar = 2.0 * sampleRate; // 120 BPM until the first loop says otherwise
    m_record.assign(kRecordFrames * 2, 0.0f);
}

Engine::~Engine() = default;

int Engine::activeVoices() const {
    int n = 0;
    for (const auto &v : d->synths)
        n += v.active;
    return n;
}
int Engine::activeHits() const {
    int n = 0;
    for (const auto &v : d->drums)
        n += v.active;
    return n;
}
int Engine::activeLoops() const {
    int n = 0;
    for (const auto &l : d->loops)
        n += l.active && l.ready;
    return n;
}
float Engine::barSeconds() const { return float(d->bar / m_rate); }

// A seventh chord on a scale degree, voiced as close as it can be to the
// last chord played, so a progression moves by small steps instead of
// jumping. Fills tones[] with semitone offsets from the returned bass note.
static float buildChord(Engine::Impl &s, int degree, float tones[4]) {
    const double base = s.tuning.baseMidi() + rowOffset(ChordRow);
    double notes[4];
    for (int i = 0; i < 4; ++i)
        notes[i] = base + s.tuning.degreeSemis(degree + 2 * i);
    double best[4] = {};
    double bestDist = 1e9;
    for (int inv = 0; inv < 4; ++inv) {
        double v[4];
        for (int i = 0; i < 4; ++i)
            v[i] = notes[(i + inv) % 4] + (i + inv >= 4 ? 12.0 : 0.0);
        for (int oct = -2; oct <= 1; ++oct) {
            double mean = 0;
            for (int i = 0; i < 4; ++i)
                mean += v[i] + 12.0 * oct;
            mean /= 4;
            const double dist = std::fabs(mean - s.lastChordCenter);
            if (dist < bestDist) {
                bestDist = dist;
                for (int i = 0; i < 4; ++i)
                    best[i] = v[i] + 12.0 * oct;
            }
        }
    }
    double mean = 0;
    for (int i = 0; i < 4; ++i)
        mean += best[i];
    // Drift back towards the middle of the keyboard, so a long progression
    // doesn't wander off the top or bottom.
    s.lastChordCenter = float(0.8 * (mean / 4) + 0.2 * 62.0);
    for (int i = 0; i < 4; ++i)
        tones[i] = float(best[i] - best[0]);
    return float(best[0]);
}

static SynthVoice &takeSynth(Engine::Impl &s) {
    SynthVoice *slot = nullptr;
    for (auto &v : s.synths)
        if (!v.active)
            return v;
    // Steal the quietest releasing or latched voice, else the quietest.
    float quietest = 1e9f;
    for (auto &v : s.synths)
        if ((v.releasing || v.latched) && v.level < quietest) {
            quietest = v.level;
            slot = &v;
        }
    if (slot)
        return *slot;
    for (auto &v : s.synths)
        if (v.level < quietest) {
            quietest = v.level;
            slot = &v;
        }
    return *slot;
}

static DrumVoice &takeDrum(Engine::Impl &s) {
    DrumVoice *slot = nullptr;
    float quietest = 1e9f;
    for (auto &v : s.drums) {
        if (!v.active)
            return v;
        if (v.level < quietest) {
            quietest = v.level;
            slot = &v;
        }
    }
    return *slot;
}

static void startNote(Engine::Impl &s, int finger, int row, int degree, float midi, float vel, float x, float y,
                      uint64_t now) {
    // The bass is mono and legato: a new note while one still sounds takes
    // it over and slides there, without retriggering — the acid slide.
    if (row == BassRow)
        for (auto &v : s.synths)
            if (v.active && !v.latched && !v.releasing && v.patch == Patch::Bass) {
                v.finger = finger;
                v.midiTarget = midi;
                v.degree = degree;
                v.vel = clampf(vel, 0.05f, 1.0f);
                v.slide = 0.06f;
                v.x = x;
                return;
            }
    SynthVoice &v = takeSynth(s);
    float tones[4] = {0, 0, 0, 0};
    int count = 1;
    if (row == ChordRow || row == ArpRow) {
        midi = buildChord(s, degree, tones);
        count = 4;
    }
    v.start(patchFor(row), finger, midi, tones, count, vel, x, y, s.rate, now);
    v.degree = degree;
    v.role = row;
}

static void startDrum(Engine::Impl &s, int piece, float vel, float x, float gain, float wet, int delay) {
    const Drum kind = Drum(std::clamp(piece, 0, int(Drum::Count) - 1));
    if (kind == Drum::HatClosed)
        for (auto &d : s.drums)
            if (d.active && d.kind == Drum::HatOpen)
                d.choke(s.rate);
    DrumVoice &v = takeDrum(s);
    // Within a piece's columns, the position tunes it a little. The kick is
    // tuned to the key: the root, in whichever octave lands at 45–65 Hz.
    const float within = clampf(x * kDrumPieces - float(piece), 0, 1);
    float tune = 0.9f + 0.25f * within;
    float pan = clampf((x - 0.5f) * 1.2f, -0.7f, 0.7f);
    if (kind == Drum::Kick) {
        float hz = midiToHz(24.0 + s.tuning.root());
        while (hz < 45.0f)
            hz *= 2.0f;
        tune = hz * (1.0f + 0.06f * (within - 0.5f));
        pan = 0;
        s.duckAt = delay; // every kick pumps the rest of the mix
    }
    // High percussion spreads out: hats alternate a little left and right,
    // the open hat leans right, perc bounces wider. Kick, snare and clap
    // stay in the middle.
    static int alternate = 0;
    alternate ^= 1;
    if (kind == Drum::HatClosed)
        pan = alternate ? -0.3f : 0.3f;
    else if (kind == Drum::HatOpen)
        pan = 0.35f;
    else if (kind == Drum::Perc)
        pan = alternate ? -0.55f : 0.55f;
    else if (kind != Drum::Kick)
        pan *= 0.2f;
    v.start(kind, vel, tune, pan, gain, delay, s.rate);
    // Each piece has its own send: the kick stays out of the room.
    static const float pieceWet[int(Drum::Count)] = {0.0f, 0.45f, 0.6f, 0.15f, 0.25f, 0.35f};
    v.wet = wet * pieceWet[int(kind)];
    ++s.hitsFired;
}

void Engine::apply(const Command &c) {
    Impl &s = *d;
    auto findLoop = [&](int id) -> Loop * {
        for (auto &l : s.loops)
            if (l.active && l.id == id)
                return &l;
        return nullptr;
    };

    switch (c.type) {
    case Cmd::NoteOn:
        startNote(s, c.id, c.i1, c.i2, c.f[0], c.f[1], c.f[2], c.f[3], m_sample);
        ++s.hitsFired;
        break;
    case Cmd::NoteMove:
        for (auto &v : s.synths)
            if (v.active && !v.latched && v.finger == c.id && !v.releasing) {
                if (v.chordish() && c.i2 != v.degree) {
                    // Sliding a chord finger moves through the progression.
                    float tones[4];
                    v.midiTarget = buildChord(s, c.i2, tones);
                    for (int i = 0; i < 4; ++i)
                        v.notes[i] = tones[i];
                } else if (!v.chordish()) {
                    v.midiTarget = c.f[0];
                }
                v.degree = c.i2;
                v.pressure = c.f[1];
                v.x = c.f[2];
                v.y = c.f[3];
            }
        break;
    case Cmd::NoteOff:
        for (auto &v : s.synths)
            if (v.active && !v.latched && v.finger == c.id)
                v.noteOff();
        break;
    case Cmd::Hit:
        startDrum(s, c.i1, c.f[0], c.f[1], 0.55f, 1.0f, 0);
        break;
    case Cmd::RollOn:
        for (auto &r : s.rolls)
            if (!r.on) {
                r = Roll{true, c.id, c.i1, c.f[0], c.f[1], 0};
                // The roll locks to the sixteenth grid.
                const double step = s.bar / 16.0;
                r.next = s.barAnchor + std::ceil((double(m_sample) - s.barAnchor) / step) * step;
                break;
            }
        break;
    case Cmd::RollMove:
        for (auto &r : s.rolls)
            if (r.on && r.finger == c.id)
                r.pressure = c.f[0];
        break;
    case Cmd::RollOff:
        for (auto &r : s.rolls)
            if (r.on && r.finger == c.id)
                r.on = false;
        break;
    case Cmd::Freeze: {
        for (auto &v : s.synths)
            if (v.active && !v.latched && !v.releasing && v.finger >= 0 && v.finger < (1 << 20)) {
                v.latched = true;
                v.finger = -1;
                v.group = c.id;
                v.born = m_sample;
                v.own = currentParams(s.knob[std::clamp(v.role, 0, RowCount - 1)]);
                v.transpose = 0;
                v.midiOffset = v.chordish()
                                   ? 0.0f
                                   : clampf(float(v.midi.y - (s.tuning.baseMidi() + rowOffset(v.role) +
                                                              s.tuning.degreeSemis(v.degree))),
                                            -1.5f, 1.5f);
            }
        int latched = 0;
        for (auto &v : s.synths)
            latched += v.active && v.latched;
        while (latched > 24) {
            SynthVoice *oldest = nullptr;
            for (auto &v : s.synths)
                if (v.active && v.latched && v.fadeRate == 0 && (!oldest || v.born < oldest->born))
                    oldest = &v;
            if (!oldest)
                break;
            oldest->fadeRate = 1.0f / 4.0f;
            --latched;
        }
        break;
    }
    case Cmd::Knob:
        if (c.i1 >= 0 && c.i1 < RoleCount) {
            // A row's own knob, or (row −1, or a mix-wide role) every row.
            for (int row = 0; row < RowCount; ++row) {
                if (rowRole(c.i1) && c.i2 >= 0 && c.i2 != row)
                    continue;
                s.knobTarget[row][c.i1][0] = clampf(c.f[0], 0, 1);
                s.knobTarget[row][c.i1][1] = clampf(c.f[1], 0, 1);
                s.lastKnobCmd[row][c.i1] = m_sample + 1;
            }
        }
        break;
    case Cmd::LoopBegin: {
        Loop *slot = nullptr;
        for (auto &l : s.loops)
            if (!l.active) {
                slot = &l;
                break;
            }
        if (!slot) {
            float quietest = 1e9f;
            for (auto &l : s.loops)
                if (!l.pinned && l.level * l.fade < quietest) {
                    quietest = l.level * l.fade;
                    slot = &l;
                }
            if (!slot)
                slot = &s.loops[0];
        }
        *slot = Loop{};
        slot->active = true;
        slot->id = c.id;
        slot->length = double(c.f[0]) * s.rate;
        slot->anchor = (c.t - m_time0) * s.rate;
        slot->homeX = c.f[1];
        slot->homeY = c.f[2];
        slot->held = true;
        slot->created = m_sample;
        slot->seed = 0x9E37u * uint32_t(c.id + 1);
        break;
    }
    case Cmd::LoopHit:
        if (Loop *l = findLoop(c.id); l && l->count < kMaxLoopHits)
            l->hits[l->count++] = {double(c.f[0]) * s.rate, c.i1, c.i2, c.f[1], c.f[2],
                                   double(std::max(0.03f, c.f[3])) * s.rate};
        break;
    case Cmd::LoopReady:
        if (Loop *l = findLoop(c.id)) {
            double len = std::max(l->length, 0.05 * s.rate);
            if (!s.tempoSet) {
                // The first loop is the clock: a bar of a comfortable length,
                // rounded to a whole number of beats per minute.
                double bar = len;
                while (bar < 1.4 * s.rate)
                    bar *= 2;
                while (bar > 4.8 * s.rate)
                    bar *= 0.5;
                const double bpm = std::round(240.0 * s.rate / bar);
                const double snapped = 240.0 * s.rate / bpm;
                const double k = snapped / bar;
                len *= k;
                for (int i = 0; i < l->count; ++i) {
                    l->hits[i].offset *= k;
                    l->hits[i].length *= k;
                }
                s.bar = snapped;
                s.barAnchor = l->anchor;
                s.tempoSet = true;
            } else {
                const double q = std::exp2(std::round(std::log2(len / s.bar)));
                const double target = s.bar * std::clamp(q, 1.0 / 16, 4.0);
                const double k = target / len;
                for (int i = 0; i < l->count; ++i) {
                    l->hits[i].offset *= k;
                    l->hits[i].length *= k;
                }
                len = target;
                const double grid = std::min(len, s.bar / 16);
                const double off = floorMod(l->anchor - s.barAnchor, grid);
                l->anchor += off < grid / 2 ? -off : grid - off;
            }
            // Tighten the groove: hits move most of the way to the sixteenth
            // grid, and every second sixteenth swings a little late.
            const double sixteenth = s.bar / 16.0;
            for (int i = 0; i < l->count; ++i) {
                Hit &h = l->hits[i];
                const double slot = std::round(h.offset / sixteenth);
                const double swing = (int(slot) & 1) ? 0.3 * s.knob[0][Glide][1] * sixteenth : 0.0;
                const double target = slot * sixteenth + swing;
                h.offset = floorMod(h.offset + 0.85 * (target - h.offset), len);
            }
            l->length = len;
            const double pos = floorMod(double(m_sample) - l->anchor, len);
            for (int i = 0; i < l->count; ++i)
                l->fired[i] = l->hits[i].offset < pos;
            l->ready = true;
        }
        break;
    case Cmd::LoopMod:
        if (Loop *l = findLoop(c.id)) {
            l->transpose = c.i1;
            l->shift = c.f[0];
            l->density = c.f[1];
            l->intensity = c.f[2];
            l->homeX = c.f[3];
            l->homeY = c.f[4];
        }
        break;
    case Cmd::LoopGrab:
        if (Loop *l = findLoop(c.id)) {
            l->held = true;
            l->fadeRate = 0;
            l->fade = 1;
        }
        break;
    case Cmd::LoopRelease:
        if (Loop *l = findLoop(c.id)) {
            l->held = false;
            l->intensity = 1;
        }
        break;
    case Cmd::LoopPin:
        if (Loop *l = findLoop(c.id)) {
            l->pinned = !l->pinned;
            l->flash = 1;
        }
        break;
    case Cmd::Overdub:
        if (Loop *l = findLoop(c.id); l && l->ready && l->count < kMaxLoopHits) {
            const double at = (c.t - m_time0) * s.rate;
            double off = floorMod(at - l->anchor, l->length);
            const double sixteenth = s.bar / 16.0;
            off = floorMod(off + 0.85 * (std::round(off / sixteenth) * sixteenth - off), l->length);
            const int value = c.i1 == DrumRow ? c.i2 : c.i2 - l->transpose;
            l->hits[l->count] = {off, c.i1, value, c.f[0], c.f[1], double(std::max(0.03f, c.f[2])) * s.rate};
            l->fired[l->count] = true;
            ++l->count;
        }
        break;
    case Cmd::Motion:
        if (Loop *l = findLoop(c.id); l && l->ready && c.i1 >= 0 && c.i1 < kLanes) {
            auto &lane = l->lanes[c.i1];
            if (!lane.used) {
                lane.used = true;
                std::memset(lane.on, 0, sizeof(lane.on));
                lane.lastIdx = -1;
            }
            lane.role = c.i2;
            lane.row = c.i3;
            const double at = (c.t - m_time0) * s.rate;
            const int idx = int(floorMod(at - l->anchor, l->length) / l->length * kLaneBins) % kLaneBins;
            int from = idx;
            if (lane.lastIdx >= 0) {
                const int gap = (idx - lane.lastIdx + kLaneBins) % kLaneBins;
                if (gap > 0 && gap <= 8)
                    from = (lane.lastIdx + 1) % kLaneBins;
            }
            for (int k = from;; k = (k + 1) % kLaneBins) {
                lane.midi[k] = c.f[0];
                lane.press[k] = c.f[1];
                lane.ka[k] = c.f[2];
                lane.kb[k] = c.f[3];
                lane.deg[k] = int(std::lround(c.f[4]));
                lane.x[k] = c.f[5];
                lane.on[k] = c.i3 >= 0 ? 1 : 2; // 2: a knob sweep without a note
                if (k == idx)
                    break;
            }
            lane.lastIdx = idx;
            lane.recordingUntil = m_sample + uint64_t(0.12 * s.rate);
        }
        break;
    case Cmd::LayerGrab:
        for (auto &v : s.synths)
            if (v.active && v.latched && v.group == c.id) {
                v.held = true;
                v.fadeRate = 0;
                v.fade = 1;
                v.latchDecay = std::max(v.latchDecay, 0.6f);
            }
        break;
    case Cmd::LayerMod:
        for (auto &v : s.synths)
            if (v.active && v.latched && v.group == c.id) {
                if (v.transpose != c.i1 && v.chordish()) {
                    float tones[4];
                    v.midiTarget = buildChord(s, v.degree + c.i1, tones);
                    for (int i = 0; i < 4; ++i)
                        v.notes[i] = tones[i];
                }
                v.transpose = c.i1;
                v.own.cutoff = clampf(c.f[0], 0, 1);
                v.own.width = clampf(c.f[1], 0, 1);
                v.swellTarget = clampf(c.f[2], 0, 2);
            }
        break;
    case Cmd::LayerRelease:
        for (auto &v : s.synths)
            if (v.active && v.latched && v.group == c.id) {
                v.held = false;
                v.swellTarget = 1;
            }
        break;
    case Cmd::LayerPin:
        for (auto &v : s.synths)
            if (v.active && v.latched && v.group == c.id)
                v.pinned = !v.pinned;
        break;
    case Cmd::Dissolve: {
        Loop *newestLoop = nullptr;
        for (auto &l : s.loops)
            if (l.active && l.fadeRate == 0 && (!newestLoop || l.created > newestLoop->created))
                newestLoop = &l;
        int group = -1;
        uint64_t groupBorn = 0;
        for (auto &v : s.synths)
            if (v.active && v.latched && v.fadeRate == 0 && v.born >= groupBorn) {
                group = v.group;
                groupBorn = v.born;
            }
        if (newestLoop && (group < 0 || newestLoop->created >= groupBorn))
            newestLoop->fadeRate = 1.0f / 2.0f;
        else if (group >= 0)
            for (auto &v : s.synths)
                if (v.active && v.latched && v.group == group)
                    v.fadeRate = 1.0f / 2.0f;
        break;
    }
    case Cmd::DissolveAll:
        for (auto &l : s.loops)
            if (l.active)
                l.fadeRate = 1.0f / 2.5f;
        for (auto &v : s.synths)
            if (v.active && v.latched)
                v.fadeRate = 1.0f / 2.5f;
        for (auto &r : s.rolls)
            r.on = false;
        s.tempoSet = false;
        break;
    case Cmd::Damper:
        s.damperTarget = clampf(c.f[0], 0, 1);
        break;
    case Cmd::SetTuning:
        s.tuning.set(c.i1, c.i2, c.i3, c.f[0] > 0.5f);
        break;
    case Cmd::Hush: {
        const float window = c.f[0] * s.rate;
        for (auto &v : s.drums)
            if (v.active && v.t < window)
                v.active = false;
        for (auto &v : s.synths)
            if (v.active && !v.latched && float(m_sample - v.born) < window)
                v.fadeRate = 1.0f / 0.03f;
        break;
    }
    }
}

void Engine::render(float *out, int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    Command c;
    while (m_queue.pop(c))
        apply(c);
    float l[64], r[64];
    int done = 0;
    while (done < frames) {
        const int n = std::min(64, frames - done);
        renderBlock(l, r, n);
        for (int i = 0; i < n; ++i) {
            out[2 * (done + i)] = l[i];
            out[2 * (done + i) + 1] = r[i];
        }
        done += n;
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double load = took / (double(frames) / m_rate);
    m_lastLoad = load;
    d->cpu += (float(load) - d->cpu) * 0.05f;
    d->cpuPeak = std::max(float(load), d->cpuPeak * 0.995f);
    if (m_sample - d->lastPublish >= uint64_t(m_rate / 200)) {
        d->lastPublish = m_sample;
        publish();
    }
}

void Engine::renderBlock(float *outL, float *outR, int n) {
    Impl &s = *d;
    const float dt = float(n) / s.rate;
    const double blockStart = double(m_sample);
    const double blockEnd = blockStart + n;

    for (int row = 0; row < RowCount; ++row)
        for (int r = 0; r < RoleCount; ++r) {
            const bool live = s.lastKnobCmd[row][r] && m_sample < s.lastKnobCmd[row][r] + uint64_t(0.3 * s.rate);
            const float w = live ? 0.0f : s.overrideWeight[row][r];
            for (int a = 0; a < 2; ++a) {
                const float target = lerpf(s.knobTarget[row][r][a], s.override_[row][r][a], w);
                s.knob[row][r][a] += (target - s.knob[row][r][a]) * s.knobSmooth;
            }
            s.overrideWeight[row][r] = 0;
        }
    // Mix-wide knobs are read from any row; they're the same on all.
    const auto K = [&](int role, int axis) { return s.knob[0][role][axis]; };
    SynthParams rowParams[RowCount];

    // Tempo-locked clocks for the wobble and the arpeggio.
    const double barPos = floorMod(blockStart - s.barAnchor, s.bar) / s.bar;
    static const double wobbleDiv[6] = {1, 2, 4, 8, 6, 12};
    for (int row = 0; row < RowCount; ++row) {
        rowParams[row] = currentParams(s.knob[row]);
        const double perBar = wobbleDiv[std::clamp(int(s.knob[row][Wobble][1] * 5.99f), 0, 5)];
        rowParams[row].wobblePhase = float(barPos * perBar - std::floor(barPos * perBar));
    }
    static const double arpDiv[5] = {0, 4, 8, 16, 32};
    // The arp row arpeggiates, sixteenths unless the knob says otherwise;
    // the chord row always sustains.
    {
        const double knobRate = arpDiv[std::clamp(int(K(Pump, 1) * 4.99f), 0, 4)];
        const double perBar = knobRate > 0 ? knobRate : 16.0;
        const double steps = barPos * perBar;
        rowParams[ArpRow].arpPhase = float(steps - std::floor(steps));
        rowParams[ArpRow].arpStep = int(std::floor(floorMod(blockStart - s.barAnchor, s.bar * 16) / s.bar * perBar));
    }

    float *dl = s.bufDrumL, *dr = s.bufDrumR, *ml = s.bufMusicL, *mr = s.bufMusicR, *sl = s.bufSendL,
          *sr = s.bufSendR;
    std::memset(dl, 0, sizeof(float) * n);
    std::memset(dr, 0, sizeof(float) * n);
    std::memset(ml, 0, sizeof(float) * n);
    std::memset(mr, 0, sizeof(float) * n);
    std::memset(sl, 0, sizeof(float) * n);
    std::memset(sr, 0, sizeof(float) * n);

    // Loops: hits due in this block become pending notes at their exact sample.
    for (auto &lp : s.loops) {
        if (!lp.active || !lp.ready)
            continue;
        if (lp.fadeRate > 0) {
            lp.fade -= lp.fadeRate * dt;
            if (lp.fade <= 0) {
                lp.active = false;
                continue;
            }
        }
        lp.flash *= std::exp(-dt / 0.12f);
        const double posStart = floorMod(blockStart - lp.anchor, lp.length);
        const double posEnd = posStart + n;
        const bool wraps = posEnd >= lp.length;
        auto fire = [&](int i, double at) {
            const Hit &h = lp.hits[i];
            Rng rng(lp.seed ^ uint32_t(lp.cycle * 2654435761u) ^ uint32_t(i * 40503u));
            rng.next();
            if (lp.density < 0 && i != 0 && rng.unit() < -lp.density)
                return;
            const float erosion = 1.0f - lp.level;
            const float vel = clampf(h.vel * lp.intensity * (0.95f + 0.1f * rng.unit()) * (1.0f - 0.4f * erosion), 0.05f, 1.0f);
            const int value = h.row == DrumRow ? h.value : h.value + lp.transpose;
            const float gain = lp.level * lp.fade;
            // Height is wetness: a loop left low stays dry, one left high
            // drowns in the room.
            const float wet = 0.2f + 1.6f * std::pow(clampf(lp.homeY, 0, 1), 1.3f);
            if (s.pendingCount < kPending)
                s.pending[s.pendingCount++] = {at, h.row, value, vel, h.x, gain, wet, h.length, 0};
            int rolls = 0;
            if (lp.density > 0)
                rolls = int(lp.density * 3.0f * rng.unit() + lp.density * 0.6f);
            if (lp.intensity > 1.3f)
                rolls = rolls * 2 + 1;
            const double step = s.bar / (lp.intensity > 1.3f ? 32.0 : 16.0);
            for (int k = 1; k <= rolls && s.pendingCount < kPending; ++k)
                s.pending[s.pendingCount++] = {at + step * k, h.row, value, vel * std::pow(0.75f, float(k)), h.x, gain,
                                               wet, std::min(h.length, step * 0.8), 0};
            lp.flash = 1.0f;
        };
        for (int i = 0; i < lp.count; ++i) {
            const double o = lp.hits[i].offset;
            if (!lp.fired[i] && o >= posStart && o < std::min(posEnd, lp.length)) {
                fire(i, blockStart + (o - posStart));
                lp.fired[i] = true;
            }
        }
        if (wraps) {
            ++lp.cycle;
            if (lp.held)
                lp.level = 1.0f;
            else if (lp.pinned)
                lp.level = std::min(1.0f, lp.level + 0.08f);
            // Loops hold their level: they repeat until dissolved.
            if (lp.level < 0.05f) {
                lp.active = false;
                continue;
            }
            for (int i = 0; i < lp.count; ++i)
                lp.fired[i] = false;
            const double rest = posEnd - lp.length;
            for (int i = 0; i < lp.count; ++i) {
                const double o = lp.hits[i].offset;
                if (o < rest) {
                    fire(i, blockStart + (lp.length - posStart) + o);
                    lp.fired[i] = true;
                }
            }
        }
    }

    // Rolls: a held finger on the drum row repeats its piece on the
    // sixteenths, on the thirty-seconds when pressed hard, louder with
    // pressure — the build-up before a drop.
    for (auto &r : s.rolls) {
        if (!r.on)
            continue;
        while (r.next < blockEnd && s.pendingCount < kPending) {
            s.pending[s.pendingCount++] = {std::max(r.next, blockStart), DrumRow, r.piece,
                                           clampf(0.25f + 0.8f * r.pressure, 0.1f, 1.0f), r.x, 1.0f, 1.0f, 0, 0};
            r.next += s.bar / (r.pressure > 0.75f ? 32.0 : 16.0);
        }
    }

    // Due notes and hits, at their exact sample.
    for (int i = 0; i < s.pendingCount;) {
        Pending &p = s.pending[i];
        if (p.at >= blockEnd) {
            ++i;
            continue;
        }
        const int delay = int(std::max(0.0, p.at - blockStart));
        if (p.finger > 0) {
            for (auto &v : s.synths)
                if (v.active && v.finger == p.finger)
                    v.noteOff();
        } else if (p.row == DrumRow) {
            startDrum(s, p.value, p.vel, p.x, 0.55f * p.gain, p.wet, delay);
        } else {
            // A loop note: on now, off after the length it was held.
            const int finger = s.nextSynthetic++;
            if (s.nextSynthetic > (1 << 30))
                s.nextSynthetic = 1 << 20;
            const float midi = float(s.tuning.baseMidi() + rowOffset(p.row) + s.tuning.degreeSemis(p.value));
            startNote(s, finger, p.row, p.value, midi, p.vel * (0.6f + 0.4f * p.gain), p.x, 0.5f, m_sample);
            ++s.hitsFired;
            if (s.pendingCount < kPending)
                s.pending[s.pendingCount++] = {p.at + std::max(p.length, 0.03 * s.rate), p.row, 0, 0, 0, 0, 0, 0,
                                               finger};
        }
        p = s.pending[--s.pendingCount];
    }

    // Loop lanes: replay what held fingers did while the loop was held.
    {
        int li = 0;
        for (auto &lp : s.loops) {
            const int index = li++;
            if (!lp.active || !lp.ready)
                continue;
            const int idx = int(floorMod(blockStart - lp.anchor, lp.length) / lp.length * kLaneBins) % kLaneBins;
            const float w = lp.level * lp.fade;
            for (int k = 0; k < kLanes; ++k) {
                auto &lane = lp.lanes[k];
                if (!lane.used)
                    continue;
                const int finger = -1000 - index * kLanes - k;
                SynthVoice *voice = nullptr;
                for (auto &v : s.synths)
                    if (v.active && !v.latched && v.finger == finger && !v.releasing)
                        voice = &v;
                const bool play = lane.on[idx] && m_sample >= lane.recordingUntil && w > 0.02f;
                const bool sounds = play && lane.on[idx] == 1;
                if (!sounds && voice)
                    voice->noteOff();
                if (play && lane.role >= 0 && lane.role < RoleCount)
                    for (int row = 0; row < RowCount; ++row) {
                        if (rowRole(lane.role) && row != std::clamp(lane.row, 0, RowCount - 1))
                            continue;
                        s.override_[row][lane.role][0] = lane.ka[idx];
                        s.override_[row][lane.role][1] = lane.kb[idx];
                        s.overrideWeight[row][lane.role] = std::max(s.overrideWeight[row][lane.role], w);
                    }
                if (!sounds)
                    continue;
                const int deg = lane.deg[idx] + lp.transpose;
                const float midi = lane.midi[idx] + float(s.tuning.degreeSemis(deg) - s.tuning.degreeSemis(lane.deg[idx]));
                if (!voice) {
                    startNote(s, finger, lane.row, deg, midi, 0.7f * w, lane.x[idx], lp.homeY, m_sample);
                    for (auto &v : s.synths)
                        if (v.active && v.finger == finger)
                            voice = &v;
                }
                if (voice) {
                    if (!voice->chordish())
                        voice->midiTarget = midi;
                    voice->pressure = lane.press[idx];
                    voice->x = lane.x[idx];
                }
            }
        }
    }
    for (auto &v : s.synths)
        if (v.active && !v.latched && !v.releasing && v.finger <= -1000 && v.finger > -1000 - kMaxLoops * kLanes) {
            const int li = (-1000 - v.finger) / kLanes;
            if (li >= kMaxLoops || !s.loops[li].active)
                v.noteOff();
        }

    float bl[64] = {}, br[64] = {};
    for (auto &v : s.synths) {
        if (!v.active)
            continue;
        const int row = std::clamp(v.role, 0, RowCount - 1);
        float *outL = v.patch == Patch::Bass ? bl : ml, *outR = v.patch == Patch::Bass ? br : mr;
        if (v.latched) {
            if (!v.chordish())
                v.midiTarget = float(s.tuning.baseMidi() + rowOffset(v.role) + s.tuning.degreeSemis(v.degree + v.transpose)) +
                               v.midiOffset;
            SynthParams own = v.own;
            own.wobblePhase = rowParams[row].wobblePhase;
            own.arpPhase = rowParams[row].arpPhase;
            own.arpStep = rowParams[row].arpStep;
            v.render(outL, outR, n, own, s.rate, 1.0f);
        } else {
            v.render(outL, outR, n, rowParams[row], s.rate, 1.0f);
        }
    }

    // Sidechain gains, per sample: every kick ducks the bass, the music and
    // the room within a couple of milliseconds, and they swell back over a
    // beat — the pump.
    const float pumpDepth = K(Pump, 0);
    float duckMusic[64], duckBass[64];
    {
        const float release = std::exp(-1.0f / (float(0.09 + 0.05 * s.bar / s.rate) * s.rate));
        const float attack = 1.0f - std::exp(-1.0f / (0.002f * s.rate));
        for (int i = 0; i < n; ++i) {
            if (i == s.duckAt)
                s.duckTarget = 1.0f;
            s.duckTarget *= release;
            s.duck += s.duckTarget > s.duck ? (s.duckTarget - s.duck) * attack : (s.duckTarget - s.duck);
            duckMusic[i] = 1.0f - pumpDepth * 0.85f * s.duck;
            duckBass[i] = 1.0f - pumpDepth * 0.7f * s.duck;
        }
        s.duckAt = -1;
    }
    // The drum row's filter is a DJ filter over the whole kit.
    const float drumCut = s.knob[DrumRow][Filter][0];
    const bool drumFilter = drumCut < 0.97f;
    if (drumFilter) {
        const float hz = 60.0f * std::exp2(drumCut * 8.3f);
        s.drumFilterL.set(hz, 0.6f + 2.5f * s.knob[DrumRow][Filter][1], s.rate);
        s.drumFilterR.set(hz, 0.6f + 2.5f * s.knob[DrumRow][Filter][1], s.rate);
    }
    for (auto &v : s.drums)
        if (v.active)
            v.render(dl, dr, n, s.rate, sl, sr);

    // Effects, set once a block from the smoothed knobs.
    s.reverb.set(K(Space, 0), K(Space, 1));
    static const double echoDiv[7] = {1.0 / 16, 1.0 / 12, 1.0 / 8, 3.0 / 16, 1.0 / 4, 3.0 / 8, 1.0 / 2};
    s.echo.set(K(Echo, 0), float(s.bar / s.rate * echoDiv[std::clamp(int(K(Echo, 1) * 6.99f), 0, 6)]));
    const float drive = K(Drive, 0), fold = K(Drive, 1);
    const float pre = 1.0f + drive * drive * 24.0f;
    const float gritMix = clampf(drive * 4.0f, 0.0f, 1.0f);
    const float gritComp = 0.35f / std::max(0.35f, fastTanh(0.35f * pre));
    const float space = K(Space, 0);
    const float send = 0.12f + 0.75f * space;
    const float wet = 0.25f + 0.9f * space;

    s.damper += (s.damperTarget - s.damper) * (1.0f - std::exp(-dt / 0.05f));
    const float dampHz = 18000.0f * std::exp(-s.damper * 4.4f);
    s.damperL.set(dampHz, 0.6f, s.rate);
    s.damperR.set(dampHz, 0.6f, s.rate);
    const float damperGain = 1.0f - 0.55f * s.damper;

    float peak = 0;
    double sumSq = 0;
    for (int i = 0; i < n; ++i) {
        // Music and bass through the drive; drums stay clean and punchy.
        float L = ml[i] * duckMusic[i] + bl[i] * duckBass[i], R = mr[i] * duckMusic[i] + br[i] * duckBass[i];
        if (drumFilter) {
            dl[i] = s.drumFilterL.step(dl[i]).low;
            dr[i] = s.drumFilterR.step(dr[i]).low;
        }
        if (gritMix > 0.0f) {
            const float gl = s.gritL.process(L, pre, fold) * gritComp;
            const float gr = s.gritR.process(R, pre, fold) * gritComp;
            L = lerpf(L, gl, gritMix);
            R = lerpf(R, gr, gritMix);
        }
        L += dl[i];
        R += dr[i];
        // Sends: chords, leads and arps as they are, drums at each piece's
        // own wetness. The bass stays dry: a bass in the room is mud.
        const float sendL = ml[i] * duckMusic[i] + sl[i], sendR = mr[i] * duckMusic[i] + sr[i];

        float eL, eR;
        s.echo.process(sendL, sendR, eL, eR);
        float rL, rR;
        s.reverb.process((sendL + eL) * send, (sendR + eR) * send, rL, rR);
        // The returns pump with the kick too.
        L += (eL + rL * wet) * duckMusic[i];
        R += (eR + rR * wet) * duckMusic[i];

        if (s.damper > 0.002f) {
            L = s.damperL.step(L).low * damperGain;
            R = s.damperR.step(R).low * damperGain;
        }
        // Low end: mono under 120 Hz, held in check under 150 Hz.
        {
            const float mid = 0.5f * (L + R);
            float side = 0.5f * (L - R);
            side -= s.sideLp.step(side).low;
            const float low = s.lowBand.step(mid).low;
            const float a = std::fabs(low);
            s.lowEnv += (a - s.lowEnv) * (a > s.lowEnv ? 0.004f : 0.0001f);
            constexpr float threshold = 0.22f;
            const float g = s.lowEnv > threshold ? std::pow(threshold / s.lowEnv, 0.5f) : 1.0f;
            const float m2 = mid - (1.0f - g) * low;
            L = m2 + side;
            R = m2 - side;
        }
        // Glue: a gentle 2:1 bus compressor with a slow release, then make-up,
        // so a dense track sits together and loud.
        {
            const float lvl = std::max(std::fabs(L), std::fabs(R));
            s.glueEnv += (lvl - s.glueEnv) * (lvl > s.glueEnv ? 0.003f : 0.00008f);
            constexpr float thr = 0.3f;
            const float g = s.glueEnv > thr ? std::pow(thr / s.glueEnv, 0.5f) : 1.0f;
            L *= g * 2.0f;
            R *= g * 2.0f;
        }
        L = s.dcL.step(L);
        R = s.dcR.step(R);
        s.limiter.process(L, R);
        outL[i] = L;
        outR[i] = R;
        peak = std::max(peak, std::max(std::fabs(L), std::fabs(R)));
        sumSq += double(L) * L + double(R) * R;
    }

    uint32_t w = m_scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        m_scope[(w + i) & (kScopeSize - 1)] = 0.5f * (outL[i] + outR[i]);
    m_scopeWrite.store(w + uint32_t(n), std::memory_order_release);
    if (m_recording.load(std::memory_order_relaxed)) {
        size_t head = m_recordHead.load(std::memory_order_relaxed);
        const size_t tail = m_recordTail.load(std::memory_order_acquire);
        for (int i = 0; i < n && head - tail < kRecordFrames; ++i, ++head) {
            m_record[(head % kRecordFrames) * 2] = outL[i];
            m_record[(head % kRecordFrames) * 2 + 1] = outR[i];
        }
        m_recordHead.store(head, std::memory_order_release);
    }

    s.peakBlock = std::max(peak, s.peakBlock * 0.97f);
    s.rmsBlock += (float(std::sqrt(sumSq / (2.0 * n))) - s.rmsBlock) * 0.1f;
    m_sample += uint64_t(n);
}

size_t Engine::drainRecording(float *out, size_t maxFrames) {
    const size_t head = m_recordHead.load(std::memory_order_acquire);
    size_t tail = m_recordTail.load(std::memory_order_relaxed);
    size_t n = 0;
    while (tail < head && n < maxFrames) {
        out[2 * n] = m_record[(tail % kRecordFrames) * 2];
        out[2 * n + 1] = m_record[(tail % kRecordFrames) * 2 + 1];
        ++tail;
        ++n;
    }
    m_recordTail.store(tail, std::memory_order_release);
    return n;
}

void Engine::copyScope(float *out, int n) const {
    const uint32_t w = m_scopeWrite.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        out[i] = m_scope[(w - uint32_t(n) + uint32_t(i)) & (kScopeSize - 1)];
}

void Engine::publish() {
    Impl &s = *d;
    EngineView &v = m_views.back();
    for (int i = 0; i < kMaxLoops; ++i) {
        const Loop &l = s.loops[i];
        LoopView &o = v.loops[i];
        o.active = l.active && l.ready;
        o.id = l.id;
        m_registry.id[i].store(o.active ? l.id : -1, std::memory_order_relaxed);
        m_registry.x[i].store(l.homeX, std::memory_order_relaxed);
        m_registry.y[i].store(l.homeY, std::memory_order_relaxed);
        if (!o.active)
            continue;
        o.pinned = l.pinned;
        o.held = l.held;
        o.dissolving = l.fadeRate > 0;
        o.phase = float(floorMod(double(m_sample) - l.anchor, l.length) / l.length);
        o.level = l.level * l.fade;
        o.homeX = l.homeX;
        o.homeY = l.homeY;
        o.flash = l.flash;
        o.density = l.density;
        o.matShift = l.shift;
        o.transpose = l.transpose;
        o.cycle = l.cycle;
        o.lengthSec = float(l.length / s.rate);
        o.hitCount = l.count;
        for (int h = 0; h < l.count; ++h) {
            o.hitPhase[h] = float(l.hits[h].offset / l.length);
            o.hitMaterial[h] = float(l.hits[h].row);
            o.hitDegree[h] = float(l.hits[h].value);
        }
        o.laneCount = 0;
        for (int k = 0; k < kLanes; ++k) {
            const auto &lane = l.lanes[k];
            if (!lane.used)
                continue;
            float *dst = o.lane[o.laneCount];
            o.laneRole[o.laneCount] = lane.role;
            for (int b = 0; b < 64; ++b) {
                const int src = b * (kLaneBins / 64);
                dst[b] = lane.on[src] ? clampf((lane.midi[src] - 30.0f) / 60.0f, 0, 1) : -1.0f;
            }
            ++o.laneCount;
        }
    }
    for (auto &lv : v.layers)
        lv = LayerView{};
    int layerCount = 0;
    for (const SynthVoice &sv : s.synths) {
        if (!sv.active || !sv.latched)
            continue;
        LayerView *slot = nullptr;
        for (int i = 0; i < layerCount; ++i)
            if (v.layers[i].group == sv.group)
                slot = &v.layers[i];
        if (!slot) {
            if (layerCount >= kMaxLayers)
                continue;
            slot = &v.layers[layerCount++];
            slot->group = sv.group;
            slot->active = true;
        }
        slot->x += sv.x;
        slot->y += sv.y;
        slot->level += sv.level;
        slot->held |= sv.held;
        slot->pinned |= sv.pinned;
        slot->transpose = sv.transpose;
        ++slot->voices;
    }
    for (int i = 0; i < kMaxLayers; ++i) {
        LayerView &lv = v.layers[i];
        if (i < layerCount && lv.voices > 0) {
            lv.x /= float(lv.voices);
            lv.y /= float(lv.voices);
        }
        m_registry.layer[i].store(i < layerCount ? lv.group : -1, std::memory_order_relaxed);
        m_registry.layerX[i].store(lv.x, std::memory_order_relaxed);
        m_registry.layerY[i].store(lv.y, std::memory_order_relaxed);
    }
    for (int i = 0; i < kMaxVoices; ++i) {
        VoiceView &o = v.voices[i];
        if (i >= kSynths) {
            o.active = false;
            continue;
        }
        const SynthVoice &sv = s.synths[i];
        o.active = sv.active;
        o.frozen = sv.latched;
        o.finger = sv.finger;
        o.role = sv.role; // the row, for colour
        o.x = sv.x;
        o.y = sv.y;
        o.level = sv.level;
        o.midi = sv.midi.y;
    }
    for (int r = 0; r < RoleCount; ++r) {
        v.knobs[r][0] = s.knob[LeadRow][r][0];
        v.knobs[r][1] = s.knob[LeadRow][r][1];
    }
    v.cpu = s.cpu;
    v.cpuPeak = s.cpuPeak;
    v.barSec = float(s.bar / s.rate);
    v.barPhase = float(floorMod(double(m_sample) - s.barAnchor, s.bar) / s.bar);
    v.tempoSet = s.tempoSet;
    v.peak = s.peakBlock;
    v.rms = s.rmsBlock;
    v.damper = s.damper;
    v.strikes = s.hitsFired;
    v.sample = m_sample;
    m_views.publish();
}

}
