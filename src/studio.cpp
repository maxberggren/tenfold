#include "studio.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include "fx.h"

namespace tf {

namespace {

double floorMod(double a, double m) {
    const double r = std::fmod(a, m);
    const double w = r < 0 ? r + m : r;
    // A hair below zero wraps to m itself in floating point; that position
    // would never play, so it is the start.
    return w >= m - 1e-9 ? 0.0 : w;
}

constexpr int kHistory = 4096;     // live commands remembered per part, for captures
constexpr int kClipEvents = 2048;  // events a clip can hold
constexpr int kReplayHeld = 96;    // replayed sounds a part can have on at once
constexpr int kReplayBit = 1 << 30;

int replayId(int id, int slot) { return kReplayBit | (slot << 24) | (id & 0xFFFFFF); }

// The lengths a clip snaps to, in beats.
double snapLength(double beats) {
    static const double lengths[] = {1, 2, 4, 8, 16, 32};
    double best = 4, bestErr = 1e9;
    for (double l : lengths) {
        const double err = std::fabs(std::log2(beats / l));
        if (err < bestErr) {
            bestErr = err;
            best = l;
        }
    }
    return best;
}

// Channel defaults for parts that don't say: a little room, a little pump.
const float kDefaultChannel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.15f, 0.0f, 0.6f, 0.0f, 0.0f};
const float kDefaultMaster[MasterCount] = {0.35f, 0.1f, 0.5f, 0.35f, 0.5f, 0.0f, 0.8f};

// The echo's divisions of the beat, picked by EchoTime.
const double kEchoBeats[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0};

}

struct Studio::PartState {
    std::unique_ptr<PartEngine> engine;
    float channelTarget[ChannelCount] = {};
    float channel[ChannelCount] = {};
    float muteGain = 1;
    Svf filtL, filtR;
    float filtSet = -1;
    float peak = 0, rms = 0;
    float bufL[kBlock] = {}, bufR[kBlock] = {};

    struct Logged {
        double beat;
        Command c;
    };
    std::vector<Logged> history;
    size_t historyHead = 0; // total ever written

    struct Event {
        double pos;
        Command c;
        double after = -1e18; // an overdub: not replayed before this beat (it was just heard live)
    };
    struct Clip {
        std::vector<Event> events;
        double length = 0; // beats; 0 = empty
        double anchor = 0; // a beat where the clip's position is 0
    };
    Clip clips[kClipSlots];
    int playing = -1;
    int queued = -1;
    bool catchUp = false; // a new clip's first block also plays what it just passed
    double queuedAt = 0;
    bool stopQueued = false;
    double stopAt = 0;
    bool overdub = false;
    int recording = -1;
    double recStart = 0, recEnd = -1;
    int recMaxBars = 8;
    int held[kReplayHeld] = {};
    int heldCount = 0;
};

struct Studio::Impl : AudioContext {
    float rate = 48000;
    Transport transportState;
    Harmony harmonyState;
    double blockTime = 0; // touch clock at the block's first sample

    const Transport &transport() const override { return transportState; }
    const Harmony &harmony() const override { return harmonyState; }
    double now() const override { return blockTime; }
    int offsetOf(double t, int n) const override {
        if (t <= 0)
            return 0;
        const double o = (t - blockTime) * double(rate);
        return o <= 0 ? 0 : o >= double(n) ? n - 1 : int(o);
    }
    void pump(int offset, float strength) override {
        if (pumpCount < 8)
            pumps[pumpCount++] = {std::clamp(offset, 0, kBlock - 1), clampf(strength, 0, 1)};
    }
    void setChord(int root, const int *tones, int count) override {
        Harmony &h = harmonyState;
        count = std::clamp(count, 1, 4);
        bool same = h.chordRoot == root && h.chordCount == count;
        for (int i = 0; i < count; ++i)
            same = same && h.chordTones[i] == tones[i];
        if (same)
            return;
        h.chordRoot = root;
        h.chordCount = count;
        for (int i = 0; i < count; ++i)
            h.chordTones[i] = tones[i];
        ++h.chordSeq;
    }
    const float *duck() const override { return duckEnv; }

    struct Pump {
        int offset;
        float strength;
    };
    Pump pumps[8];
    int pumpCount = 0;
    float duckEnv[kBlock] = {};
    float duckTarget = 0, duckLevel = 0;

    // Messages timed later than the coming block (a part scheduling ahead, a
    // mixer move set for the bar line), held until their block.
    static constexpr int kPending = 1024;
    StudioMsg pending[kPending];
    int pendingCount = 0;
    bool dispatching = false;

    float masterTarget[MasterCount] = {};
    float master[MasterCount] = {};
    Reverb reverb;
    TapeEcho echo;
    Saturator satL, satR;
    Svf masterL, masterR;
    float masterFiltSet = -1;
    Svf sideLp, lowBand;
    float lowEnv = 0, glueEnv = 0;
    DcBlock dcL, dcR;
    Limiter limiter;

    bool metronome = false;
    float clickEnv = 0, clickPhase = 0, clickHz = 1000;
    long lastBeat = -1;

    float cpu = 0, cpuPeak = 0, peak = 0, rms = 0;
    uint64_t lastPublish = 0;
};

Studio::Studio(float rate) : m_rate(rate), d(std::make_unique<Impl>()) {
    d->rate = rate;
    d->transportState.rate = rate;
    d->transportState.bpm = 124;
    d->transportState.beatsPerSample = 124.0 / 60.0 / rate;
    d->harmonyState.tuning.set(9, 0, 4, false);
    d->reverb.init(rate);
    d->echo.init(rate);
    d->limiter.init(rate);
    d->sideLp.set(120.0f, 0.7f, rate);
    d->lowBand.set(150.0f, 0.7f, rate);
    for (int i = 0; i < MasterCount; ++i)
        d->master[i] = d->masterTarget[i] = kDefaultMaster[i];
    m_record.assign(kRecordFrames * 2, 0.0f);
}

Studio::~Studio() = default;

int Studio::addPart(std::unique_ptr<PartEngine> engine, const float *channel) {
    auto p = std::make_unique<PartState>();
    engine->init(m_rate);
    p->engine = std::move(engine);
    for (int i = 0; i < ChannelCount; ++i)
        p->channel[i] = p->channelTarget[i] = channel ? channel[i] : kDefaultChannel[i];
    p->history.resize(kHistory);
    for (auto &c : p->clips)
        c.events.reserve(kClipEvents);
    m_parts.push_back(std::move(p));
    return int(m_parts.size()) - 1;
}

PartEngine *Studio::engine(int part) const {
    return part >= 0 && part < int(m_parts.size()) ? m_parts[size_t(part)]->engine.get() : nullptr;
}

bool Studio::post(const StudioMsg &m) {
    std::lock_guard lock(m_postLock);
    return m_queue.push(m);
}

int Studio::activeVoices() const {
    int n = 0;
    for (auto &p : m_parts)
        n += p->engine->voices();
    return n;
}

// --- clips ------------------------------------------------------------------

namespace {

void sortClip(std::vector<Studio::PartState::Event> &e) {
    std::stable_sort(e.begin(), e.end(), [](const auto &a, const auto &b) { return a.pos < b.pos; });
}

}

static void releaseHeld(Studio::PartState &p, Studio::Impl &s) {
    for (int i = 0; i < p.heldCount; ++i) {
        Command off;
        off.type = Command::Off;
        off.id = p.held[i];
        p.engine->command(off, s);
    }
    p.heldCount = 0;
}

static void stopClip(Studio::PartState &p, Studio::Impl &s) {
    releaseHeld(p, s);
    p.playing = -1;
    p.stopQueued = false;
}

static void startClip(Studio::PartState &p, Studio::Impl &s, int slot, double anchor) {
    if (p.playing >= 0)
        releaseHeld(p, s);
    p.playing = slot;
    p.clips[slot].anchor = anchor;
    p.queued = -1;
    p.stopQueued = false;
    p.catchUp = false;
}

// Makes a clip from what the part played between beats b0 and b1. With
// stretch, the take is scaled to the snapped length (a rhythm tapped in its
// own time); without, it is cut to it (a recording in the studio's time).
static void makeClip(Studio::PartState &p, Studio::Impl &s, int slot, double b0, double b1, float quantize,
                     double anchor, bool stretch) {
    const double raw = b1 - b0;
    if (slot < 0 || slot >= kClipSlots || raw < 0.2)
        return;
    const double len = snapLength(raw);
    const double k = stretch ? len / raw : 1.0;
    auto &clip = p.clips[slot];
    if (p.playing == slot)
        stopClip(p, s);
    clip.events.clear();
    clip.length = len;

    struct Open {
        int id;
        double origPos, newPos;
    };
    Open open[64];
    int openCount = 0;
    const size_t head = p.historyHead;
    const size_t from = head > size_t(kHistory) ? head - size_t(kHistory) : 0;
    constexpr double eps = 1e-6;
    constexpr double grid = 0.25;
    // A take in the studio's time reaches an eighth of a beat early at both
    // ends: a note landed a hair before the bar line belongs to that bar,
    // not to the one before.
    const double early = stretch ? 0.0 : 0.125;
    bool playedOnTheOne = false;
    // What was already going on when a take in the studio's time began: a
    // note still held (a bass root landed before the bar and held through
    // it) and the last setting of each knob. Both go in at the clip's start,
    // so the loop sounds the way the take did.
    if (!stretch) {
        Command held[32];
        int heldCount = 0;
        Command knobs[32];
        int knobCount = 0;
        for (size_t h = from; h < head; ++h) {
            const auto &lg = p.history[h % size_t(kHistory)];
            if (lg.beat >= b0 - early - eps)
                break;
            const Command &c = lg.c;
            if (c.type == Command::On && !(c.flags & Command::Quantize)) {
                if (heldCount < 32)
                    held[heldCount++] = c;
            } else if (c.type == Command::Off) {
                for (int i = 0; i < heldCount; ++i)
                    if (held[i].id == c.id) {
                        held[i] = held[--heldCount];
                        break;
                    }
            } else if (c.type == Command::Param) {
                int i = 0;
                while (i < knobCount && knobs[i].i[0] != c.i[0])
                    ++i;
                if (i < 32) {
                    knobs[i] = c;
                    knobCount = std::max(knobCount, i + 1);
                }
            }
        }
        for (int i = 0; i < knobCount; ++i)
            clip.events.push_back({0.0, knobs[i], -1e18});
        for (int i = 0; i < heldCount && openCount < 64; ++i) {
            clip.events.push_back({0.0, held[i], -1e18});
            open[openCount++] = {held[i].id, 0.0, 0.0};
        }
    }
    for (size_t h = from; h < head; ++h) {
        const auto &lg = p.history[h % size_t(kHistory)];
        if (lg.beat >= b1 - 0.12 && (lg.c.type == Command::On || (lg.c.flags & Command::Quantize)))
            playedOnTheOne = true;
        if (lg.beat < b0 - early - eps || lg.beat >= b1 - early - eps)
            continue;
        if (int(clip.events.size()) >= kClipEvents - 64)
            break;
        double pos = (lg.beat - b0) * k;
        Command c = lg.c;
        if (c.type == Command::On || (c.flags & Command::Quantize)) {
            const double target = std::round(pos / grid) * grid;
            const double q = pos + double(quantize) * (target - pos);
            if (c.type == Command::On && openCount < 64)
                open[openCount++] = {c.id, pos, q};
            pos = q;
        } else if (c.type == Command::Off) {
            bool found = false;
            for (int i = 0; i < openCount; ++i)
                if (open[i].id == c.id) {
                    pos = open[i].newPos + std::max(1.0 / 64, pos - open[i].origPos);
                    open[i] = open[--openCount];
                    found = true;
                    break;
                }
            if (!found)
                continue; // the end of something that started before the take
        }
        clip.events.push_back({floorMod(pos, len), c, -1e18});
    }
    // Sounds still held when the take ended stop just before it comes round.
    for (int i = 0; i < openCount; ++i) {
        Command off;
        off.type = Command::Off;
        off.id = open[i].id;
        clip.events.push_back({std::max(0.0, len - 1.0 / 32), off, -1e18});
    }
    sortClip(clip.events);
    if (clip.events.empty()) {
        clip.length = 0;
        return;
    }
    if (std::getenv("TENFOLD_DEBUG")) {
        std::fprintf(stderr, "tenfold: clip slot %d len %.3f anchor %.4f now %.4f:", slot, len, anchor, s.transportState.beat);
        for (auto &e : clip.events)
            std::fprintf(stderr, " %d@%.4f", int(e.c.type), e.pos);
        std::fprintf(stderr, "\n");
    }
    startClip(p, s, slot, anchor);
    // A loop closed by a key rather than a note on its ONE (Space with nothing
    // playing) starts a moment after its downbeat: play what it just passed.
    p.catchUp = stretch && !playedOnTheOne;
}

void Studio::apply(const StudioMsg &m) {
    Impl &s = *d;
    Transport &tr = s.transportState;
    const double nowBeat = tr.beat;
    auto beatOf = [&](double t) {
        if (t <= 0)
            return nowBeat;
        return nowBeat + (t - s.blockTime) * tr.bpm / 60.0;
    };
    auto nextBar = [&] { return std::ceil(nowBeat / kBeatsPerBar - 1e-9) * kBeatsPerBar; };
    const bool hasPart = m.part >= 0 && m.part < int(m_parts.size());
    PartState *p = hasPart ? m_parts[size_t(m.part)].get() : nullptr;

    switch (m.kind) {
    case StudioMsg::ToPart: {
        if (!p)
            return;
        const Command &c = m.cmd;
        const double b = beatOf(c.t);
        if (!(c.flags & Command::NoRecord)) {
            p->history[p->historyHead % size_t(kHistory)] = {b, c};
            ++p->historyHead;
            if (p->overdub && p->playing >= 0) {
                auto &clip = p->clips[p->playing];
                if (int(clip.events.size()) < kClipEvents) {
                    double pos = floorMod(b - clip.anchor, clip.length);
                    if (c.type == Command::On || (c.flags & Command::Quantize)) {
                        const double target = std::round(pos / 0.25) * 0.25;
                        pos = floorMod(pos + 0.85 * (target - pos), clip.length);
                    }
                    // Kept sorted; the capacity is reserved, so this never allocates.
                    auto at = std::upper_bound(clip.events.begin(), clip.events.end(), pos,
                                               [](double v, const PartState::Event &e) { return v < e.pos; });
                    clip.events.insert(at, {pos, c, b + 0.3});
                }
            }
        }
        if (!s.dispatching && c.t > s.blockTime + double(kBlock) / double(s.rate) &&
            s.pendingCount < Impl::kPending) {
            s.pending[s.pendingCount++] = m;
            break;
        }
        p->engine->command(c, s);
        break;
    }
    case StudioMsg::Capture: {
        if (!p)
            return;
        const double b0 = beatOf(m.d0), b1 = beatOf(m.d1);
        bool anyPlaying = false;
        for (auto &q : m_parts)
            anyPlaying |= q->playing >= 0 || q->queued >= 0;
        const double secs = m.d1 - m.d0;
        if (!anyPlaying && !s.metronome && secs > 0.15) {
            // Nothing else is playing: this loop is the clock. Its tempo
            // becomes the studio's (a whole BPM, between 88 and 176), and the
            // bar starts where the loop closed.
            double len = 1;
            while (len * 60.0 / secs < 88.0 && len < 32)
                len *= 2;
            const double bpm = std::round(len * 60.0 / secs);
            const double oldBpm = tr.bpm, oldBeat = nowBeat;
            const double bar = nextBar() + kBeatsPerBar;
            tr.bpm = bpm;
            tr.beatsPerSample = bpm / 60.0 / double(s.rate);
            tr.beat = bar + (s.blockTime - m.d1) * bpm / 60.0;
            // Everything remembered was timed on the old clock: move it onto
            // the new one, so later captures line up too.
            for (auto &q : m_parts)
                for (auto &lg : q->history)
                    lg.beat = tr.beat + (lg.beat - oldBeat) * bpm / oldBpm;
            makeClip(*p, s, m.slot, bar - secs * bpm / 60.0, bar, m.f, bar, true);
            break;
        }
        const double anchor = std::round(b1);
        makeClip(*p, s, m.slot, b0, b1, m.f, anchor, true);
        break;
    }
    case StudioMsg::Record:
        if (!p || m.slot < 0 || m.slot >= kClipSlots)
            return;
        p->recording = m.slot;
        p->recStart = nextBar();
        p->recEnd = -1;
        p->recMaxBars = std::clamp(m.value, 1, 8);
        break;
    case StudioMsg::StopRecord:
        if (!p || p->recording < 0)
            return;
        {
            // A stop just after a bar line means that bar line.
            const double into = floorMod(nowBeat, kBeatsPerBar);
            double end = into < 0.5 ? nowBeat - into : nextBar();
            p->recEnd = std::max(end, p->recStart + 1.0);
        }
        break;
    case StudioMsg::TakeBars:
        if (!p || m.slot < 0 || m.slot >= kClipSlots || m.d1 <= m.d0)
            return;
        // The same path as a recording that closes at d1: when the clock
        // reaches it, the history between d0 and d1 becomes the clip, its
        // start on d0's bar, so it carries on from d1 without a seam.
        p->recording = m.slot;
        p->recStart = m.d0;
        p->recEnd = m.d1;
        p->recMaxBars = 8;
        break;
    case StudioMsg::CancelRecord:
        if (p)
            p->recording = -1;
        break;
    case StudioMsg::Overdub:
        if (p)
            p->overdub = m.value != 0;
        break;
    case StudioMsg::Launch:
        if (!p || m.slot < 0 || m.slot >= kClipSlots || p->clips[m.slot].length <= 0)
            return;
        if (m.value) {
            startClip(*p, s, m.slot, nowBeat);
        } else {
            p->queued = m.slot;
            p->queuedAt = nextBar();
            p->stopQueued = false;
        }
        break;
    case StudioMsg::Stop:
        if (!p)
            return;
        p->queued = -1;
        if (m.value || p->playing < 0) {
            stopClip(*p, s);
        } else {
            p->stopQueued = true;
            p->stopAt = nextBar();
        }
        break;
    case StudioMsg::Clear:
        if (!p || m.slot < 0 || m.slot >= kClipSlots)
            return;
        if (p->playing == m.slot)
            stopClip(*p, s);
        if (p->queued == m.slot)
            p->queued = -1;
        p->clips[m.slot].events.clear();
        p->clips[m.slot].length = 0;
        break;
    case StudioMsg::SetChannel:
    case StudioMsg::SetMaster:
        if (!s.dispatching && m.d0 > s.blockTime + double(kBlock) / double(s.rate) &&
            s.pendingCount < Impl::kPending) {
            s.pending[s.pendingCount++] = m;
            break;
        }
        if (m.kind == StudioMsg::SetMaster) {
            if (m.slot >= 0 && m.slot < MasterCount)
                s.masterTarget[m.slot] = clampf(m.f, 0, 1);
            break;
        }
        if (p && m.slot >= 0 && m.slot < ChannelCount)
            p->channelTarget[m.slot] = clampf(m.f, 0, 1);
        break;
    case StudioMsg::Tempo:
        tr.bpm = std::clamp(m.d0, 40.0, 240.0);
        tr.beatsPerSample = tr.bpm / 60.0 / double(s.rate);
        break;
    case StudioMsg::Key:
        s.harmonyState.tuning.set(std::clamp(int(m.part), 0, 11), m.slot, m.value, false);
        ++s.harmonyState.chordSeq;
        break;
    case StudioMsg::Metronome:
        s.metronome = m.value != 0;
        break;
    case StudioMsg::Silence:
        for (int i = 0; i < s.pendingCount;)
            if (s.pending[i].kind == StudioMsg::ToPart && (m.part < 0 || s.pending[i].part == m.part))
                s.pending[i] = s.pending[--s.pendingCount];
            else
                ++i;
        for (int i = 0; i < int(m_parts.size()); ++i)
            if (m.part < 0 || m.part == i) {
                m_parts[size_t(i)]->heldCount = 0;
                m_parts[size_t(i)]->engine->silence(m.value != 0);
            }
        break;
    }
}

// --- rendering ----------------------------------------------------------------

void Studio::render(float *out, int frames) {
    const auto t0 = std::chrono::steady_clock::now();
    StudioMsg m;
    d->blockTime = m_time0 + double(m_sample) / double(m_rate);
    while (m_queue.pop(m))
        apply(m);
    float l[kBlock], r[kBlock];
    int done = 0;
    while (done < frames) {
        const int n = std::min(kBlock, frames - done);
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
    if (m_sample - d->lastPublish >= uint64_t(m_rate / 120)) {
        d->lastPublish = m_sample;
        publish();
    }
}

void Studio::renderBlock(float *outL, float *outR, int n) {
    Impl &s = *d;
    Transport &tr = s.transportState;
    tr.sample = m_sample;
    s.blockTime = m_time0 + double(m_sample) / double(m_rate);
    const double b0 = tr.beat, b1 = tr.beat + double(n) * tr.beatsPerSample;
    const double spb = tr.secondsPerBeat();

    // Messages that were waiting for this block.
    if (s.pendingCount > 0) {
        const double end = s.blockTime + double(n) / double(m_rate);
        s.dispatching = true;
        for (int i = 0; i < s.pendingCount;) {
            const StudioMsg &m = s.pending[i];
            const double when = m.kind == StudioMsg::ToPart ? m.cmd.t : m.d0;
            if (when < end) {
                const StudioMsg due = m;
                s.pending[i] = s.pending[--s.pendingCount];
                if (due.kind == StudioMsg::ToPart) {
                    if (due.part >= 0 && due.part < int(m_parts.size()))
                        m_parts[size_t(due.part)]->engine->command(due.cmd, s);
                } else {
                    apply(due);
                }
            } else {
                ++i;
            }
        }
        s.dispatching = false;
    }

    // Clips: what starts, stops and records on a boundary in this block, then
    // the events that fall in it.
    for (auto &pp : m_parts) {
        PartState &p = *pp;
        if (p.stopQueued && p.stopAt < b1)
            stopClip(p, s);
        if (p.queued >= 0 && p.queuedAt < b1)
            startClip(p, s, p.queued, p.queuedAt);
        if (p.recording >= 0) {
            const double maxEnd = p.recStart + kBeatsPerBar * p.recMaxBars;
            const double end = p.recEnd >= 0 ? std::min(p.recEnd, maxEnd) : maxEnd;
            if (end < b1) {
                const int slot = p.recording;
                p.recording = -1;
                makeClip(p, s, slot, p.recStart, end, 0.6f, p.recStart, false);
            }
        }
        if (p.playing < 0)
            continue;
        auto &clip = p.clips[p.playing];
        if (clip.length <= 0)
            continue;
        const double pos0 = floorMod(b0 - clip.anchor, clip.length);
        const double span = b1 - b0;
        auto fire = [&](const PartState::Event &e, double pos) {
            if (b0 + (pos - pos0) <= e.after)
                return;
            Command c = e.c;
            c.id = replayId(c.id, p.playing);
            c.t = s.blockTime + (pos - pos0) * spb;
            if (c.type == Command::On) {
                bool already = false;
                for (int i = 0; i < p.heldCount; ++i)
                    already |= p.held[i] == c.id;
                if (already) {
                    Command off;
                    off.type = Command::Off;
                    off.id = c.id;
                    p.engine->command(off, s);
                } else if (p.heldCount < kReplayHeld) {
                    p.held[p.heldCount++] = c.id;
                }
            } else if (c.type == Command::Off) {
                for (int i = 0; i < p.heldCount; ++i)
                    if (p.held[i] == c.id) {
                        p.held[i] = p.held[--p.heldCount];
                        break;
                    }
            }
            p.engine->command(c, s);
        };
        auto fireRange = [&](double from, double to, double base) {
            auto it = std::lower_bound(clip.events.begin(), clip.events.end(), from,
                                       [](const PartState::Event &e, double v) { return e.pos < v; });
            for (; it != clip.events.end() && it->pos < to; ++it)
                fire(*it, it->pos + base);
        };
        if (p.catchUp) {
            p.catchUp = false;
            if (pos0 > 0 && pos0 < 0.25)
                fireRange(0, pos0, 0);
        }
        fireRange(pos0, std::min(pos0 + span, clip.length), 0);
        if (pos0 + span > clip.length)
            fireRange(0, pos0 + span - clip.length, clip.length);
    }

    // The pump: per-sample, a couple of milliseconds down, about a beat back.
    {
        const float release = std::exp(-1.0f / (float(0.09 + 0.25 * spb) * s.rate));
        const float attack = 1.0f - std::exp(-1.0f / (0.002f * s.rate));
        for (int i = 0; i < n; ++i) {
            for (int k = 0; k < s.pumpCount; ++k)
                if (s.pumps[k].offset == i)
                    s.duckTarget = std::max(s.duckTarget, s.pumps[k].strength);
            s.duckTarget *= release;
            s.duckLevel += s.duckTarget > s.duckLevel ? (s.duckTarget - s.duckLevel) * attack
                                                      : (s.duckTarget - s.duckLevel);
            s.duckEnv[i] = s.duckLevel;
        }
        s.pumpCount = 0;
    }

    // Every part's dry signal.
    for (auto &pp : m_parts) {
        std::fill(pp->bufL, pp->bufL + n, 0.0f);
        std::fill(pp->bufR, pp->bufR + n, 0.0f);
        pp->engine->render(pp->bufL, pp->bufR, n, s);
    }

    // Smoothed settings.
    const float smooth = 1.0f - std::exp(-float(n) / (0.03f * s.rate));
    for (int i = 0; i < MasterCount; ++i)
        s.master[i] += (s.masterTarget[i] - s.master[i]) * smooth;
    bool anySolo = false;
    for (auto &pp : m_parts) {
        for (int i = 0; i < ChannelCount; ++i)
            pp->channel[i] += (pp->channelTarget[i] - pp->channel[i]) * smooth;
        anySolo |= pp->channelTarget[Solo] > 0.5f;
    }

    // Mixer: each channel through its filter, level, pan and pump, into the
    // mix and the room's sends.
    float mixL[kBlock] = {}, mixR[kBlock] = {}, sendRL[kBlock] = {}, sendRR[kBlock] = {}, sendEL[kBlock] = {},
          sendER[kBlock] = {};
    auto setFilter = [&](Svf &fl, Svf &fr, float v, float &cached) {
        if (std::fabs(v - cached) < 0.002f)
            return;
        cached = v;
        float hz;
        if (v < 0.5f)
            hz = 60.0f * std::exp2(v / 0.5f * 8.4f); // 60 Hz .. 20 kHz
        else
            hz = 20.0f * std::exp2((v - 0.5f) / 0.5f * 9.6f); // 20 Hz .. 15 kHz
        fl.set(hz, 0.95f, s.rate);
        fr.set(hz, 0.95f, s.rate);
    };
    for (auto &pp : m_parts) {
        PartState &p = *pp;
        const float *ch = p.channel;
        const bool audible = p.channelTarget[Mute] < 0.5f && (!anySolo || p.channelTarget[Solo] > 0.5f);
        const float muteTarget = audible ? 1.0f : 0.0f;
        const float g = (ch[Gain] / 0.8f) * (ch[Gain] / 0.8f);
        const float panL = std::min(1.0f, 2.0f * (1.0f - ch[Pan])), panR = std::min(1.0f, 2.0f * ch[Pan]);
        const float filt = ch[Filter];
        const bool lp = filt < 0.49f, hp = filt > 0.51f;
        if (lp || hp)
            setFilter(p.filtL, p.filtR, filt, p.filtSet);
        const float rs = ch[ReverbSend] * ch[ReverbSend], es = ch[EchoSend] * ch[EchoSend];
        float pk = 0, sq = 0;
        for (int i = 0; i < n; ++i) {
            p.muteGain += (muteTarget - p.muteGain) * 0.01f;
            float L = p.bufL[i], R = p.bufR[i];
            if (lp) {
                L = p.filtL.step(L).low;
                R = p.filtR.step(R).low;
            } else if (hp) {
                L = p.filtL.step(L).high;
                R = p.filtR.step(R).high;
            }
            const float gain = g * p.muteGain * (1.0f - ch[PumpDepth] * s.duckEnv[i]);
            L *= gain * panL;
            R *= gain * panR;
            mixL[i] += L;
            mixR[i] += R;
            sendRL[i] += L * rs;
            sendRR[i] += R * rs;
            sendEL[i] += L * es;
            sendER[i] += R * es;
            pk = std::max(pk, std::max(std::fabs(L), std::fabs(R)));
            sq += L * L + R * R;
        }
        p.peak = std::max(pk, p.peak * 0.97f);
        p.rms += (std::sqrt(sq / float(2 * n)) - p.rms) * 0.1f;
    }

    // The room: echo into reverb, both pumping with the kick a little.
    const float *M = s.master;
    s.reverb.set(M[RoomSize], M[RoomShimmer]);
    s.echo.set(M[EchoFeedback], float(spb * kEchoBeats[std::clamp(int(M[EchoTime] * 5.99f), 0, 5)]));
    const float wet = 0.6f + 0.8f * M[RoomSize];
    const float mf = M[MasterFilter];
    const bool mlp = mf < 0.49f, mhp = mf > 0.51f;
    if (mlp || mhp)
        setFilter(s.masterL, s.masterR, mf, s.masterFiltSet);
    const float drive = M[MasterDrive];
    const float pre = 1.0f + drive * drive * 12.0f;
    const float satComp = 0.5f / std::max(0.5f, fastTanh(0.5f * pre));
    const float mgain = (M[MasterGain] / 0.8f) * (M[MasterGain] / 0.8f);

    float peak = 0;
    double sumSq = 0;
    const double spbSamples = tr.samplesPerBeat();
    for (int i = 0; i < n; ++i) {
        float eL, eR, rL, rR;
        s.echo.process(sendEL[i], sendER[i], eL, eR);
        s.reverb.process(sendRL[i] + 0.5f * eL, sendRR[i] + 0.5f * eR, rL, rR);
        const float roomDuck = 1.0f - 0.35f * s.duckEnv[i];
        float L = mixL[i] + (eL + rL * wet) * roomDuck, R = mixR[i] + (eR + rR * wet) * roomDuck;

        if (mlp) {
            L = s.masterL.step(L).low;
            R = s.masterR.step(R).low;
        } else if (mhp) {
            L = s.masterL.step(L).high;
            R = s.masterR.step(R).high;
        }
        if (drive > 0.01f) {
            L = lerpf(L, s.satL.process(L, pre, 0.0f) * satComp, std::min(1.0f, drive * 3.0f));
            R = lerpf(R, s.satR.process(R, pre, 0.0f) * satComp, std::min(1.0f, drive * 3.0f));
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
        // Glue: a gentle 2:1 bus compressor with a slow release, then make-up.
        {
            const float lvl = std::max(std::fabs(L), std::fabs(R));
            s.glueEnv += (lvl - s.glueEnv) * (lvl > s.glueEnv ? 0.003f : 0.00008f);
            constexpr float thr = 0.3f;
            const float g = s.glueEnv > thr ? std::pow(thr / s.glueEnv, 0.5f) : 1.0f;
            L *= g * 1.8f * mgain;
            R *= g * 1.8f * mgain;
        }
        // The metronome: a short blip on every beat, higher on the one.
        if (s.metronome) {
            const double beat = b0 + double(i) * tr.beatsPerSample;
            const long whole = long(std::floor(beat));
            if (whole != s.lastBeat) {
                s.lastBeat = whole;
                s.clickEnv = 1.0f;
                s.clickHz = whole % kBeatsPerBar == 0 ? 1760.0f : 1320.0f;
            }
            if (s.clickEnv > 1e-4f) {
                s.clickPhase += s.clickHz / s.rate;
                s.clickPhase -= std::floor(s.clickPhase);
                const float c = std::sin(kTwoPi * s.clickPhase) * s.clickEnv * 0.12f;
                s.clickEnv *= std::exp(-1.0f / (0.012f * s.rate));
                L += c;
                R += c;
            }
        }
        (void)spbSamples;
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
        m_scope[(w + uint32_t(i)) & (kScopeSize - 1)] = 0.5f * (outL[i] + outR[i]);
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
    s.peak = std::max(peak, s.peak * 0.97f);
    s.rms += (float(std::sqrt(sumSq / (2.0 * n))) - s.rms) * 0.1f;

    m_sample += uint64_t(n);
    tr.beat = b1;
}

size_t Studio::drainRecording(float *out, size_t maxFrames) {
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

void Studio::copyScope(float *out, int n) const {
    const uint32_t w = m_scopeWrite.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        out[i] = m_scope[(w - uint32_t(n) + uint32_t(i)) & (kScopeSize - 1)];
}

void Studio::publish() {
    Impl &s = *d;
    StudioView &v = m_views.back();
    const Transport &tr = s.transportState;
    v.bpm = tr.bpm;
    v.beat = tr.beat;
    v.sample = m_sample;
    v.time = m_time0 + double(m_sample) / double(m_rate);
    v.metronome = s.metronome;
    const Harmony &h = s.harmonyState;
    v.root = h.tuning.root();
    v.family = h.tuning.familyIndex();
    v.brightness = h.tuning.brightnessIndex();
    v.just = h.tuning.just();
    v.chordRoot = h.chordRoot;
    v.chordCount = h.chordCount;
    for (int i = 0; i < 4; ++i)
        v.chordTones[i] = h.chordTones[i];
    v.chordSeq = h.chordSeq;
    v.partCount = int(m_parts.size());
    for (int pi = 0; pi < int(m_parts.size()) && pi < kMaxParts; ++pi) {
        PartState &p = *m_parts[size_t(pi)];
        PartView &o = v.parts[pi];
        for (int c = 0; c < kClipSlots; ++c) {
            const auto &clip = p.clips[c];
            ClipView &cv = o.clips[c];
            cv.lengthBeats = float(clip.length);
            cv.events = int(clip.events.size());
            cv.phase = clip.length > 0 ? float(floorMod(tr.beat - clip.anchor, clip.length) / clip.length) : 0;
            if (p.recording == c)
                cv.state = ClipView::Recording;
            else if (clip.length <= 0)
                cv.state = ClipView::Empty;
            else if (p.playing == c)
                cv.state = p.stopQueued || (p.queued >= 0 && p.queued != c) ? ClipView::Stopping : ClipView::Playing;
            else if (p.queued == c)
                cv.state = ClipView::Queued;
            else
                cv.state = ClipView::Stopped;
        }
        o.playing = p.playing;
        o.queued = p.queued;
        o.overdub = p.overdub;
        o.recording = p.recording;
        for (int c = 0; c < ChannelCount; ++c)
            o.channel.value[c] = p.channelTarget[c];
        o.channel.peak = p.peak;
        o.channel.rms = p.rms;
        o.voices = p.engine->voices();
        p.engine->publish(o.data);
    }
    for (int i = 0; i < MasterCount; ++i)
        v.master[i] = s.masterTarget[i];
    v.peak = s.peak;
    v.rms = s.rms;
    v.cpu = s.cpu;
    v.cpuPeak = s.cpuPeak;
    const uint32_t w = m_scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < 256; ++i)
        v.scope[i] = m_scope[(w - 1024u + uint32_t(i) * 4u) & (kScopeSize - 1)];
    m_views.publish();
}

}
