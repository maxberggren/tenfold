#include "engine.h"

#include <chrono>
#include <cstring>

#include "fx.h"
#include "voices.h"

namespace tf {

namespace {

struct RoleInfo {
    const char *name, *slide, *twist;
    float slideDefault, twistDefault;
};

// The order fingers take roles in is the order a player reaches for them:
// tone first, then width, then voice, then the room.
const RoleInfo roles[RoleCount] = {
    {"Tone", "brightness", "hollow ↔ full", 0.55f, 0.5f},
    {"Shadow", "width", "sub & fifth", 0.3f, 0.25f},
    {"Air", "voice", "vowel", 0.12f, 0.4f},
    {"Space", "room", "shimmer", 0.35f, 0.0f},
    {"Pulse", "depth", "rate", 0.0f, 0.4f},
    {"Echo", "echo", "time", 0.0f, 0.55f},
    {"Grit", "drive", "fold", 0.0f, 0.0f},
    {"Drift", "stretch", "life", 0.0f, 0.45f},
    {"Swarm", "grains", "spread", 0.0f, 0.3f},
    {"Gravity", "pull", "mode", 0.7f, 0.5f},
};

constexpr int kStrikes = 64;
constexpr int kPending = 256;

}

const char *roleName(int r) { return roles[std::clamp(r, 0, RoleCount - 1)].name; }
const char *roleSlide(int r) { return roles[std::clamp(r, 0, RoleCount - 1)].slide; }
const char *roleTwist(int r) { return roles[std::clamp(r, 0, RoleCount - 1)].twist; }
float roleDefault(int r, int axis) {
    const RoleInfo &i = roles[std::clamp(r, 0, RoleCount - 1)];
    return axis == 0 ? i.slideDefault : i.twistDefault;
}

struct Hit {
    double offset; // samples from the loop's one
    int degree;
    float material, vel, x;
};

struct Loop {
    bool active = false, ready = false, held = false, pinned = false;
    int id = -1;
    double length = 0;  // samples
    double anchor = 0;  // absolute sample of the one
    float rawLengthSec = 0;
    Hit hits[kMaxLoopHits];
    int count = 0;
    int transpose = 0;
    float matShift = 0, density = 0, intensity = 1;
    float level = 1, fade = 1, fadeRate = 0;
    int cycle = 0;
    float homeX = 0.5f, homeY = 0.5f, flash = 0;
    uint64_t created = 0;
    uint32_t seed = 1;
    bool fired[kMaxLoopHits] = {};
    // Voice lines recorded while the loop was held: what a held finger did
    // (pitch, pressure, and the knob its role turns), one bin per 1/256 of
    // the loop.
    struct Lane {
        bool used = false, voice = true;
        int role = -1;
        float midi[kLaneBins], press[kLaneBins], ka[kLaneBins], kb[kLaneBins], x[kLaneBins];
        int deg[kLaneBins];
        uint8_t on[kLaneBins];
        uint64_t recordingUntil = 0;
        int lastIdx = -1;
    } lanes[kLanes];
};

struct Pending {
    double at; // absolute sample
    int degree;
    float material, vel, x, gain, wet;
};

struct Engine::Impl {
    float rate;
    Tuning tuning;
    DroneVoice voices[kMaxVoices];
    StrikeVoice strikes[kStrikes];
    Loop loops[kMaxLoops];
    Pending pending[kPending];
    int pendingCount = 0;

    float knobTarget[RoleCount][2];
    float knob[RoleCount][2];
    float knobSmooth = 0;

    double bar = 0;        // samples per bar; the first loop sets it
    double barAnchor = 0;  // a sample that falls on a downbeat
    bool tempoSet = false;

    int freezeGroup = 0;
    float damperTarget = 0, damper = 0;

    Reverb reverb;
    TapeEcho echo;
    GrainCloud swarm;
    Limiter limiter;
    Saturator gritL, gritR;
    StrikeVoice ghosts[8];
    int ghostNext = 0;
    uint64_t lastKnobCmd[RoleCount] = {};
    float override_[RoleCount][2] = {};
    float overrideWeight[RoleCount] = {};
    Svf damperL, damperR;
    Svf mudL, mudR, airL, airR;
    DcBlock dcL, dcR;
    Rng rng{0x5EED};
    int strikesFired = 0;

    float bufDroneL[64], bufDroneR[64], bufHitL[64], bufHitR[64], bufSendL[64], bufSendR[64];
    // Low end: summed to mono under 120 Hz and held in check by a
    // compressor on the band below 150 Hz.
    Svf sideLp, lowBand;
    float lowEnv = 0;


    float cpu = 0, cpuPeak = 0;
    float peakBlock = 0, rmsBlock = 0;
    uint64_t lastPublish = 0;
};

static void sympathetic(Engine::Impl &s, const StrikeVoice &strike, float vel) {
    float hz[StrikeVoice::M], amp[StrikeVoice::M];
    const int n = strike.modes(hz, amp);
    for (auto &v : s.voices)
        if (v.active && !v.releasing)
            v.excite(hz, amp, n, vel);
}

Engine::Engine(float sampleRate) : m_rate(sampleRate), d(std::make_unique<Impl>()) {
    d->rate = sampleRate;
    for (int i = 0; i < kMaxVoices; ++i)
        d->voices[i].init(sampleRate, 0x1234567u + uint32_t(i) * 7919u);
    for (int i = 0; i < kStrikes; ++i)
        d->strikes[i].rng = Rng(0xBEEF + uint32_t(i) * 104729u);
    for (int i = 0; i < 8; ++i)
        d->ghosts[i].rng = Rng(0xFACE + uint32_t(i) * 7907u);
    for (int r = 0; r < RoleCount; ++r)
        for (int a = 0; a < 2; ++a)
            d->knob[r][a] = d->knobTarget[r][a] = roleDefault(r, a);
    d->knobSmooth = 1.0f - std::exp(-64.0f / (0.06f * sampleRate));
    d->reverb.init(sampleRate);
    d->echo.init(sampleRate);
    d->swarm.init(sampleRate);
    d->limiter.init(sampleRate);
    d->sideLp.set(120.0f, 0.7f, sampleRate);
    d->lowBand.set(150.0f, 0.7f, sampleRate);
    d->mudL.set(300.0f, 0.8f, sampleRate);
    d->mudR.set(300.0f, 0.8f, sampleRate);
    d->airL.set(6500.0f, 0.7f, sampleRate);
    d->airR.set(6500.0f, 0.7f, sampleRate);
    d->bar = 2.4 * sampleRate;
    m_record.assign(kRecordFrames * 2, 0.0f);
}

Engine::~Engine() = default;

int Engine::activeVoices() const {
    int n = 0;
    for (const auto &v : d->voices)
        n += v.active;
    return n;
}
int Engine::activeStrikes() const {
    int n = 0;
    for (const auto &s : d->strikes)
        n += s.active;
    return n;
}
int Engine::activeLoops() const {
    int n = 0;
    for (const auto &l : d->loops)
        n += l.active && l.ready;
    return n;
}
float Engine::barSeconds() const { return float(d->bar / m_rate); }

static double floorMod(double a, double m) {
    const double r = std::fmod(a, m);
    return r < 0 ? r + m : r;
}

// A strike rings whatever is sounding near its modes: frozen chords, held
// voices and loop voices are one resonating body with the drums.
static void sympathetic(Engine::Impl &s, const StrikeVoice &strike, float vel);

static StrikeVoice &takeStrike(Engine::Impl &s) {
    StrikeVoice *slot = nullptr;
    float quietest = 1e9f;
    for (auto &v : s.strikes) {
        if (!v.active)
            return v;
        if (v.level < quietest) {
            quietest = v.level;
            slot = &v;
        }
    }
    StrikeVoice &ghost = s.ghosts[s.ghostNext];
    s.ghostNext = (s.ghostNext + 1) % 8;
    ghost = *slot;
    ghost.fadeStep = 1.0f / (0.008f * s.rate);
    return *slot;
}

// The timbre the knobs describe right now, for live voices and for a layer
// frozen this instant.
static ToneParams currentTone(const float knob[RoleCount][2]) {
    ToneParams tp;
    tp.bright = knob[Tone][0];
    tp.hollow = knob[Tone][1];
    tp.width = knob[Shadow][0];
    tp.shadow = knob[Shadow][1];
    tp.voice = knob[Air][0];
    tp.vowel = knob[Air][1];
    tp.stretch = knob[Drift][0];
    tp.life = knob[Drift][1];
    return tp;
}

// A free strike voice, or the quietest one, faded out over a few
// milliseconds on the side rather than cut off mid-ring.
static StrikeVoice &takeStrike(Engine::Impl &s);

void Engine::apply(const Command &c) {
    Impl &s = *d;
    auto findLoop = [&](int id) -> Loop * {
        for (auto &l : s.loops)
            if (l.active && l.id == id)
                return &l;
        return nullptr;
    };
    auto fireStrike = [&](int degree, float mat, float vel, float x, int delay, float gain, float contact) {
        StrikeVoice &slot = takeStrike(s);
        const double midi = s.tuning.baseMidi() + s.tuning.degreeSemis(degree);
        slot.start(float(midi), mat, vel, clampf((x - 0.5f) * 1.3f, -0.8f, 0.8f), gain, delay, s.rate, contact);
        sympathetic(s, slot, vel * gain * 2.0f);
        ++s.strikesFired;
    };

    switch (c.type) {
    case Cmd::Strike:
        fireStrike(c.i1, c.f[0], c.f[1], c.f[2], 0, 0.5f, c.f[4] > 0 ? c.f[4] : 1.0f);
        break;
    case Cmd::VoiceOn: {
        DroneVoice *slot = nullptr;
        // A free slot, else the quietest frozen voice, else the oldest one.
        for (auto &v : s.voices)
            if (!v.active) {
                slot = &v;
                break;
            }
        if (!slot) {
            float quietest = 1e9f;
            for (auto &v : s.voices)
                if (v.frozen && v.level < quietest) {
                    quietest = v.level;
                    slot = &v;
                }
        }
        if (!slot)
            slot = &s.voices[0];
        slot->start(c.id, c.i1, c.f[0], c.f[1], c.f[2], c.f[3], c.f[4], s.rate, m_sample);
        slot->degree = int(std::lround(c.f[5]));
        slot->transpose = 0;
        break;
    }
    case Cmd::VoiceMove:
        for (auto &v : s.voices)
            if (v.active && !v.frozen && v.finger == c.id) {
                v.midiTarget = c.f[0];
                v.pressure = c.f[1];
                v.size = c.f[2];
                v.x = c.f[3];
                v.y = c.f[4];
                v.degree = int(std::lround(c.f[5]));
            }
        break;
    case Cmd::VoiceOff:
        for (auto &v : s.voices)
            if (v.active && !v.frozen && v.finger == c.id)
                v.releasing = true;
        break;
    case Cmd::Freeze: {
        s.freezeGroup = c.id;
        int frozenCount = 0;
        const ToneParams now = currentTone(s.knob);
        for (auto &v : s.voices)
            if (v.active && !v.frozen && !v.releasing && v.finger >= 0) {
                v.frozen = true;
                v.finger = -1;
                v.group = c.id;
                v.born = m_sample;
                // The layer keeps the timbre it was made with; turning the
                // knobs from here on shapes what comes next, not this.
                v.own = now;
                v.transpose = 0;
                v.midiOffset = clampf(float(v.midi.y - (s.tuning.baseMidi() + s.tuning.degreeSemis(v.degree))), -1.5f, 1.5f);
                ++frozenCount;
            }
        // Too many frozen layers: the oldest give way, slowly.
        int frozen = 0;
        for (auto &v : s.voices)
            frozen += v.active && v.frozen;
        while (frozen > 30) {
            DroneVoice *oldest = nullptr;
            for (auto &v : s.voices)
                if (v.active && v.frozen && v.fadeRate == 0 && (!oldest || v.born < oldest->born))
                    oldest = &v;
            if (!oldest)
                break;
            oldest->fadeRate = 1.0f / 6.0f;
            --frozen;
        }
        (void)frozenCount;
        break;
    }
    case Cmd::Knob:
        if (c.i1 >= 0 && c.i1 < RoleCount) {
            s.knobTarget[c.i1][0] = clampf(c.f[0], 0, 1);
            s.knobTarget[c.i1][1] = clampf(c.f[1], 0, 1);
            s.lastKnobCmd[c.i1] = m_sample + 1;
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
            lane.voice = c.i3 != 0;
            const double at = (c.t - m_time0) * s.rate;
            const int idx = int(floorMod(at - l->anchor, l->length) / l->length * kLaneBins) % kLaneBins;
            // Fill the bins since the last sample too, so a moving finger
            // leaves a line rather than dots.
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
                lane.on[k] = 1;
                if (k == idx)
                    break;
            }
            lane.lastIdx = idx;
            // Not replayed while it is being played: the finger is the sound.
            lane.recordingUntil = m_sample + uint64_t(0.12 * s.rate);
        }
        break;
    case Cmd::LayerGrab:
        for (auto &v : s.voices)
            if (v.active && v.frozen && v.group == c.id) {
                v.held = true;
                v.fadeRate = 0;
                v.fade = 1;
                v.frozenDecay = std::max(v.frozenDecay, 0.6f);
            }
        break;
    case Cmd::LayerMod:
        for (auto &v : s.voices)
            if (v.active && v.frozen && v.group == c.id) {
                v.transpose = c.i1;
                v.own.bright = clampf(c.f[0], 0, 1);
                v.own.width = clampf(c.f[1], 0, 1);
                v.swellTarget = clampf(c.f[2], 0, 2);
            }
        break;
    case Cmd::LayerRelease:
        for (auto &v : s.voices)
            if (v.active && v.frozen && v.group == c.id) {
                v.held = false;
                v.swellTarget = 1;
            }
        break;
    case Cmd::LayerPin:
        for (auto &v : s.voices)
            if (v.active && v.frozen && v.group == c.id)
                v.pinned = !v.pinned;
        break;
    case Cmd::LoopBegin: {
        Loop *slot = nullptr;
        for (auto &l : s.loops)
            if (!l.active) {
                slot = &l;
                break;
            }
        if (!slot) {
            // Full: the quietest unpinned loop makes room.
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
        slot->rawLengthSec = c.f[0];
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
            l->hits[l->count++] = {double(c.f[0]) * s.rate, c.i1, c.f[1], c.f[2], c.f[3]};
        break;
    case Cmd::LoopReady:
        if (Loop *l = findLoop(c.id)) {
            double len = l->length;
            if (len < 0.05 * s.rate)
                len = 0.05 * s.rate;
            if (!s.tempoSet) {
                // The first loop is the clock. A short pattern is a fraction of
                // a bar: double it until the bar is a comfortable length.
                double bar = len;
                while (bar < 1.4 * s.rate)
                    bar *= 2;
                while (bar > 4.8 * s.rate)
                    bar *= 0.5;
                s.bar = bar;
                s.barAnchor = l->anchor;
                s.tempoSet = true;
            } else {
                // Later loops snap to a power-of-two fraction or multiple of
                // the bar, and their one moves to the nearest beat. The taps
                // inside keep their own timing: the groove is the player's.
                const double q = std::exp2(std::round(std::log2(len / s.bar)));
                const double target = s.bar * std::clamp(q, 1.0 / 16, 4.0);
                const double k = target / len;
                for (int i = 0; i < l->count; ++i)
                    l->hits[i].offset *= k;
                len = target;
                const double grid = std::min(len, s.bar / 4);
                const double off = floorMod(l->anchor - s.barAnchor, grid);
                l->anchor += off < grid / 2 ? -off : grid - off;
            }
            l->length = len;
            // Anything already past in this first cycle waits for the next.
            const double pos = floorMod(double(m_sample) - l->anchor, len);
            for (int i = 0; i < l->count; ++i)
                l->fired[i] = l->hits[i].offset < pos;
            l->ready = true;
        }
        break;
    case Cmd::LoopMod:
        if (Loop *l = findLoop(c.id)) {
            l->transpose = c.i1;
            l->matShift = c.f[0];
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
            const double off = floorMod(at - l->anchor, l->length);
            // Stored without the loop's current transpose and material, so the
            // new tap moves with the others from here on.
            l->hits[l->count] = {off, c.i1 - l->transpose, clampf(c.f[0] - l->matShift, 0, 4), c.f[1], c.f[2]};
            l->fired[l->count] = true;
            ++l->count;
        }
        break;
    case Cmd::Dissolve: {
        // The newest layer: a loop or a frozen chord, whichever came last.
        Loop *newestLoop = nullptr;
        for (auto &l : s.loops)
            if (l.active && l.fadeRate == 0 && (!newestLoop || l.created > newestLoop->created))
                newestLoop = &l;
        int group = -1;
        uint64_t groupBorn = 0;
        for (auto &v : s.voices)
            if (v.active && v.frozen && v.fadeRate == 0 && v.born >= groupBorn) {
                group = v.group;
                groupBorn = v.born;
            }
        if (newestLoop && (group < 0 || newestLoop->created >= groupBorn))
            newestLoop->fadeRate = 1.0f / 2.5f;
        else if (group >= 0)
            for (auto &v : s.voices)
                if (v.active && v.frozen && v.group == group)
                    v.fadeRate = 1.0f / 2.5f;
        break;
    }
    case Cmd::DissolveAll:
        for (auto &l : s.loops)
            if (l.active)
                l.fadeRate = 1.0f / 3.0f;
        for (auto &v : s.voices)
            if (v.active && v.frozen)
                v.fadeRate = 1.0f / 3.0f;
        s.tempoSet = false;
        break;
    case Cmd::Damper:
        s.damperTarget = clampf(c.f[0], 0, 1);
        break;
    case Cmd::Hush:
        for (auto &v : s.strikes)
            if (v.active && float(v.age) < c.f[0] * s.rate)
                v.fadeStep = 1.0f / (0.03f * s.rate);
        break;
    case Cmd::SetTuning:
        s.tuning.set(c.i1, c.i2, c.i3, c.f[0] > 0.5f);
        break;
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
    // The screen redraws at most every few milliseconds; publishing more often
    // would only copy views nobody reads.
    if (m_sample - d->lastPublish >= uint64_t(m_rate / 200)) {
        d->lastPublish = m_sample;
        publish();
    }
}

void Engine::renderBlock(float *outL, float *outR, int n) {
    Impl &s = *d;
    const float dt = float(n) / s.rate;

    // Knobs: where the fingers set them, unless a loop is replaying a
    // recorded sweep of that knob and no finger has touched it just now.
    for (int r = 0; r < RoleCount; ++r) {
        const bool live = s.lastKnobCmd[r] && m_sample < s.lastKnobCmd[r] + uint64_t(0.3 * s.rate);
        const float w = live ? 0.0f : s.overrideWeight[r];
        for (int a = 0; a < 2; ++a) {
            const float target = lerpf(s.knobTarget[r][a], s.override_[r][a], w);
            s.knob[r][a] += (target - s.knob[r][a]) * s.knobSmooth;
        }
        s.overrideWeight[r] = 0;
    }
    const auto K = [&](int role, int axis) { return s.knob[role][axis]; };
    const ToneParams tp = currentTone(s.knob);

    float *dl = s.bufDroneL, *dr = s.bufDroneR, *hl = s.bufHitL, *hr = s.bufHitR;
    std::memset(dl, 0, sizeof(float) * n);
    std::memset(dr, 0, sizeof(float) * n);
    std::memset(hl, 0, sizeof(float) * n);
    std::memset(hr, 0, sizeof(float) * n);
    float *sl = s.bufSendL, *sr = s.bufSendR;
    std::memset(sl, 0, sizeof(float) * n);
    std::memset(sr, 0, sizeof(float) * n);

    // Loop lanes: replay what held fingers did while the loop was held.
    {
        int loopIndex = 0;
        for (auto &lp : s.loops) {
            const int li = loopIndex++;
            if (!lp.active || !lp.ready)
                continue;
            const int idx = int(floorMod(double(m_sample) - lp.anchor, lp.length) / lp.length * kLaneBins) % kLaneBins;
            const float w = lp.level * lp.fade;
            for (int k = 0; k < kLanes; ++k) {
                auto &lane = lp.lanes[k];
                if (!lane.used)
                    continue;
                const int finger = -1000 - li * kLanes - k;
                DroneVoice *voice = nullptr;
                for (auto &v : s.voices)
                    if (v.active && !v.frozen && v.finger == finger && !v.releasing)
                        voice = &v;
                const bool play = lane.on[idx] && m_sample >= lane.recordingUntil && w > 0.02f;
                if (!play || !lane.voice) {
                    if (voice)
                        voice->releasing = true;
                }
                if (!play)
                    continue;
                if (lane.role >= 0 && lane.role < RoleCount) {
                    s.override_[lane.role][0] = lane.ka[idx];
                    s.override_[lane.role][1] = lane.kb[idx];
                    s.overrideWeight[lane.role] = std::max(s.overrideWeight[lane.role], w);
                }
                if (!lane.voice)
                    continue;
                const int deg = lane.deg[idx];
                const float midi = lane.midi[idx] + float(s.tuning.degreeSemis(deg + lp.transpose) - s.tuning.degreeSemis(deg));
                if (!voice) {
                    for (auto &v : s.voices)
                        if (!v.active) {
                            voice = &v;
                            break;
                        }
                    if (!voice)
                        continue;
                    voice->start(finger, std::max(0, lane.role), midi, 0, 1.0f, lane.x[idx], lp.homeY, s.rate, m_sample);
                }
                voice->midiTarget = midi;
                voice->pressure = lane.press[idx] * w;
                voice->x = lane.x[idx];
                if (lane.role >= 0 && lane.role < RoleCount) {
                    s.override_[lane.role][0] = lane.ka[idx];
                    s.override_[lane.role][1] = lane.kb[idx];
                    s.overrideWeight[lane.role] = std::max(s.overrideWeight[lane.role], w);
                }
            }
        }
    }

    for (auto &v : s.voices) {
        if (!v.active)
            continue;
        if (v.frozen) {
            // A frozen chord lives in scale degrees: it follows the key and
            // mode, and its own transpose when it's picked up.
            v.midiTarget = float(s.tuning.baseMidi() + s.tuning.degreeSemis(v.degree + v.transpose)) + v.midiOffset;
            v.render(dl, dr, n, v.own, s.rate);
        } else {
            v.render(dl, dr, n, tp, s.rate);
        }
    }

    // Loops: find every hit that falls inside this block and start it at its
    // exact sample.
    const double blockStart = double(m_sample);
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
        Rng rng(lp.seed ^ uint32_t(lp.cycle * 2654435761u));
        auto fire = [&](int i, double at) {
            const Hit &h = lp.hits[i];
            rng.s = lp.seed ^ uint32_t(lp.cycle * 2654435761u) ^ uint32_t(i * 40503u);
            rng.next();
            // Twisting left thins the pattern out; the one always stays.
            if (lp.density < 0 && i != 0 && rng.unit() < -lp.density)
                return;
            const float erosion = 1.0f - lp.level;
            const float vel = clampf(h.vel * lp.intensity * (0.92f + 0.16f * rng.unit()) * (1.0f - 0.5f * erosion), 0.02f, 1.0f);
            const float mat = clampf(h.material + lp.matShift, 0.0f, 4.0f);
            const int deg = h.degree + lp.transpose;
            // An eroding loop drifts out of time as tape would.
            const double smear = double(erosion * erosion) * 0.012 * s.rate * rng.bipolar();
            const double when = std::max(blockStart, at + smear);
            const float gain = 0.5f * lp.level * lp.fade;
            // A loop's height is how wet it is: a kick left low stays dry, a
            // bell pattern left high drowns in the room.
            const float wet = 0.15f + 1.7f * std::pow(clampf(lp.homeY, 0, 1), 1.3f);
            if (s.pendingCount < kPending)
                s.pending[s.pendingCount++] = {when, deg, mat, vel, h.x, gain, wet};
            // Twisting right rolls each hit into ratchets and, further, adds
            // ghost notes between them; pressing hard doubles the rolls.
            int rolls = 0;
            if (lp.density > 0)
                rolls = int(lp.density * 3.0f * rng.unit() + lp.density * 0.6f);
            if (lp.intensity > 1.3f)
                rolls = rolls * 2 + 1;
            const double step = s.bar / (lp.intensity > 1.3f ? 32.0 : 16.0);
            for (int k = 1; k <= rolls && s.pendingCount < kPending; ++k)
                s.pending[s.pendingCount++] = {when + step * k, deg, mat, vel * std::pow(0.72f, float(k)), h.x, gain, wet};
            if (lp.density > 0.5f && rng.unit() < (lp.density - 0.5f) * 1.2f && s.pendingCount < kPending) {
                const int hop = int(rng.next() % 3u) - 1;
                s.pending[s.pendingCount++] = {when + step * (1 + int(rng.next() % 3u)), deg + hop, mat, vel * 0.35f, h.x, gain, wet};
            }
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
            // A new cycle: erode unless held or pinned, then play anything at
            // the very start of it.
            ++lp.cycle;
            if (lp.held)
                lp.level = 1.0f;
            else if (lp.pinned)
                lp.level = std::min(1.0f, lp.level + 0.08f);
            else
                lp.level *= 0.972f;
            if (lp.level < 0.04f) {
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

    // Lane voices of loops that are gone are let go of.
    for (auto &v : s.voices)
        if (v.active && !v.frozen && !v.releasing && v.finger <= -1000) {
            const int li = (-1000 - v.finger) / kLanes;
            if (li >= kMaxLoops || !s.loops[li].active)
                v.releasing = true;
        }

    // Due hits, at their exact sample.
    const double blockEnd = blockStart + n;
    for (int i = 0; i < s.pendingCount;) {
        Pending &p = s.pending[i];
        if (p.at < blockEnd) {
            StrikeVoice &slot = takeStrike(s);
            const double midi = s.tuning.baseMidi() + s.tuning.degreeSemis(p.degree);
            slot.start(float(midi), p.material, p.vel, clampf((p.x - 0.5f) * 1.3f, -0.8f, 0.8f), p.gain,
                       int(std::max(0.0, p.at - blockStart)), s.rate);
            slot.wet = p.wet;
            sympathetic(s, slot, p.vel * p.gain * 2.0f);
            ++s.strikesFired;
            p = s.pending[--s.pendingCount];
        } else {
            ++i;
        }
    }

    for (auto &v : s.strikes)
        if (v.active)
            v.render(hl, hr, n, sl, sr);
    for (auto &v : s.ghosts)
        if (v.active)
            v.render(hl, hr, n, sl, sr);

    // Pulse: a tempo-locked swell and duck on the drones only, so taps and
    // loops cut through it.
    const float pulseDepth = K(Pulse, 0);
    static const double divisions[7] = {1, 2, 3, 4, 6, 8, 16};
    const double pulsesPerBar = divisions[std::clamp(int(K(Pulse, 1) * 6.99f), 0, 6)];
    const double barPos = floorMod(blockStart - s.barAnchor, s.bar) / s.bar;

    // Space, echo, grit and swarm are set once a block from the smoothed knobs.
    s.reverb.set(K(Space, 0), K(Space, 1));
    static const double echoDiv[7] = {1.0 / 16, 1.0 / 12, 1.0 / 8, 3.0 / 16, 1.0 / 4, 3.0 / 8, 1.0 / 2};
    s.echo.set(K(Echo, 0), float(s.bar / s.rate * echoDiv[std::clamp(int(K(Echo, 1) * 6.99f), 0, 6)]));
    s.swarm.set(K(Swarm, 0), K(Swarm, 1));
    const float drive = K(Grit, 0), fold = K(Grit, 1);
    const float pre = 1.0f + drive * drive * 30.0f;
    const float gritMix = clampf(drive * 5.0f, 0.0f, 1.0f);
    const float gritComp = 0.35f / std::max(0.35f, fastTanh(0.35f * pre));
    const float space = K(Space, 0);
    const float send = 0.25f + 0.75f * space;
    const float wet = 0.35f + 0.9f * space;

    s.damper += (s.damperTarget - s.damper) * (1.0f - std::exp(-dt / 0.05f));
    const float dampHz = 18000.0f * std::exp(-s.damper * 4.4f);
    s.damperL.set(dampHz, 0.6f, s.rate);
    s.damperR.set(dampHz, 0.6f, s.rate);
    const float damperGain = 1.0f - 0.55f * s.damper;

    float peak = 0;
    double sumSq = 0;
    const double barStep = 1.0 / s.bar;
    for (int i = 0; i < n; ++i) {
        float gate = 1.0f;
        if (pulseDepth > 0.001f) {
            const double ph = (barPos + barStep * i) * pulsesPerBar;
            const float frac = float(ph - std::floor(ph));
            // A soft-edged pulse: swells in, holds, ducks; sharper with depth.
            const float shape = std::pow(0.5f + 0.5f * std::cos(kTwoPi * frac), 1.0f + 3.0f * pulseDepth);
            gate = 1.0f - pulseDepth * (1.0f - shape);
        }
        float L = dl[i] * gate + hl[i];
        float R = dr[i] * gate + hr[i];

        if (gritMix > 0.0f) {
            const float gl = s.gritL.process(L, pre, fold) * gritComp;
            const float gr = s.gritR.process(R, pre, fold) * gritComp;
            L = lerpf(L, gl, gritMix);
            R = lerpf(R, gr, gritMix);
        }

        // What goes to the echo and the room: everything as it is, except
        // that loop hits go at their own loop's wetness.
        const float sendL = L + (sl[i] - hl[i]), sendR = R + (sr[i] - hr[i]);

        float eL, eR;
        s.echo.process(sendL, sendR, eL, eR);
        L += eL;
        R += eR;

        float gL, gR;
        s.swarm.process(L, R, gL, gR);
        const float swarmLevel = 0.9f;
        L += gL * swarmLevel;
        R += gR * swarmLevel;

        float rL, rR;
        s.reverb.process((sendL + eL) * send + gL * 0.6f, (sendR + eR) * send + gR * 0.6f, rL, rR);
        L += rL * wet;
        R += rR * wet;

        // Low end: one bass in the middle, not two drifting apart, and a
        // compressor on everything under 150 Hz so a dense landscape stays
        // clear instead of filling up from the floor.
        {
            const float mid = 0.5f * (L + R);
            float side = 0.5f * (L - R);
            side -= s.sideLp.step(side).low;
            const float low = s.lowBand.step(mid).low;
            const float a = std::fabs(low);
            s.lowEnv += (a - s.lowEnv) * (a > s.lowEnv ? 0.004f : 0.0001f);
            constexpr float threshold = 0.1f; // about −20 dBFS
            const float g = s.lowEnv > threshold ? std::pow(threshold / s.lowEnv, 0.66f) : 1.0f;
            const float m2 = mid - (1.0f - g) * low;
            L = m2 + side;
            R = m2 - side;
        }

        if (s.damper > 0.002f) {
            L = s.damperL.step(L).low * damperGain;
            R = s.damperR.step(R).low * damperGain;
        }
        // A fixed master voicing: a little less boxiness around 300 Hz, a
        // little more air above 6 kHz.
        L += -0.28f * s.mudL.step(L).band + 0.45f * s.airL.step(L).high;
        R += -0.28f * s.mudR.step(R).band + 0.45f * s.airR.step(R).high;
        L = s.dcL.step(L);
        R = s.dcR.step(R);
        s.limiter.process(L, R);
        outL[i] = L;
        outR[i] = R;
        peak = std::max(peak, std::max(std::fabs(L), std::fabs(R)));
        sumSq += double(L) * L + double(R) * R;
    }

    // Scope and recording taps.
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
        o.matShift = l.matShift;
        o.transpose = l.transpose;
        o.cycle = l.cycle;
        o.lengthSec = float(l.length / s.rate);
        o.hitCount = l.count;
        for (int h = 0; h < l.count; ++h) {
            o.hitPhase[h] = float(l.hits[h].offset / l.length);
            o.hitMaterial[h] = clampf(l.hits[h].material + l.matShift, 0, 4);
            o.hitDegree[h] = float(l.hits[h].degree + l.transpose);
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
    // Frozen layers: where each chord's light sits, for the screen and for
    // the gesture layer to find when a finger reaches for it.
    for (auto &lv : v.layers)
        lv = LayerView{};
    int layerCount = 0;
    for (const DroneVoice &dv : s.voices) {
        if (!dv.active || !dv.frozen)
            continue;
        LayerView *slot = nullptr;
        for (int i = 0; i < layerCount; ++i)
            if (v.layers[i].group == dv.group)
                slot = &v.layers[i];
        if (!slot) {
            if (layerCount >= kMaxLayers)
                continue;
            slot = &v.layers[layerCount++];
            slot->group = dv.group;
            slot->active = true;
        }
        slot->x += dv.x;
        slot->y += dv.y;
        slot->level += dv.level;
        slot->held |= dv.held;
        slot->pinned |= dv.pinned;
        slot->transpose = dv.transpose;
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
        const DroneVoice &dv = s.voices[i];
        VoiceView &o = v.voices[i];
        o.active = dv.active;
        o.frozen = dv.frozen;
        o.finger = dv.finger;
        o.role = dv.role;
        o.x = dv.x;
        o.y = dv.y;
        o.level = dv.level;
        o.midi = dv.midi.y;
    }
    for (int r = 0; r < RoleCount; ++r) {
        v.knobs[r][0] = s.knob[r][0];
        v.knobs[r][1] = s.knob[r][1];
    }
    v.cpu = s.cpu;
    v.cpuPeak = s.cpuPeak;
    v.barSec = float(s.bar / s.rate);
    v.barPhase = float(floorMod(double(m_sample) - s.barAnchor, s.bar) / s.bar);
    v.tempoSet = s.tempoSet;
    v.peak = s.peakBlock;
    v.rms = s.rmsBlock;
    v.damper = s.damper;
    v.strikes = s.strikesFired;
    v.sample = m_sample;
    m_views.publish();
}

}
