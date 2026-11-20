#pragma once

// The studio: every part's engine, the clock they share, the clips they
// loop, the mixer channel each plays through, the room and the master bus.
// It runs on the audio thread and hears only messages from a lock-free
// queue; the session (touch and UI threads) is its only producer.

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "dsp.h"
#include "part.h"

namespace tf {

// A message to the studio itself, or through it to a part.
struct StudioMsg {
    enum Kind : uint8_t {
        ToPart,      // part, cmd
        Capture,     // part, slot, d0 = t0, d1 = t1, f = quantize
        Record,      // part, slot, value = max bars
        StopRecord,  // part
        TakeBars,    // part, slot, d0 = first beat, d1 = last beat (a bar line, now or soon):
                     // what the part played between them becomes a loop at d1
        CancelRecord,// part: a recording or a waiting take is called off
        Overdub,     // part, value = on
        Launch,      // part, slot, value = now
        Stop,        // part, value = now
        Clear,       // part, slot
        SetChannel,  // part, slot = channel, f = value, d0 = when (touch clock; 0 = now)
        SetMaster,   // slot = master param, f = value, d0 = when
        Tempo,       // d0 = bpm
        Key,         // part = root, slot = family, value = brightness
        Metronome,   // value = on
        Silence,     // part (−1: all), value = hard
        UndoTake,    // part: take back that part's latest take, and play what played before
        ClearAll,    // every clip of every part goes, every sound stops: start over
    };
    Kind kind = ToPart;
    int8_t part = 0;
    int8_t slot = 0;
    int32_t value = 0;
    double d0 = 0, d1 = 0;
    float f = 0;
    Command cmd;
};

class Studio {
public:
    explicit Studio(float rate = 48000.0f);
    ~Studio();

    // Before audio starts.
    int addPart(std::unique_ptr<PartEngine> engine, const float *channel);
    int partCount() const { return int(m_parts.size()); }
    PartEngine *engine(int part) const;

    float sampleRate() const { return m_rate; }
    // Touch times are seconds on the same clock as this origin: the kernel's
    // monotonic clock live, script time offline.
    void setTimeOrigin(double seconds) { m_time0 = seconds; }
    double timeOrigin() const { return m_time0; }

    // Producer side; safe from any thread (one at a time is enforced here).
    bool post(const StudioMsg &m);

    // Interleaved stereo. Audio thread only.
    void render(float *interleaved, int frames);

    bool fetchView() { return m_views.fetch(); }
    const StudioView &view() const { return m_views.front(); }

    // Mono output history for the screen, written without locks.
    static constexpr int kScopeSize = 8192;
    void copyScope(float *out, int n) const;

    // Recording tap: a ring of interleaved stereo a recorder thread drains.
    static constexpr size_t kRecordFrames = 1 << 19;
    void setRecording(bool on) { m_recording.store(on, std::memory_order_release); }
    bool recording() const { return m_recording.load(std::memory_order_acquire); }
    size_t drainRecording(float *out, size_t maxFrames);

    double lastBlockLoad() const { return m_lastLoad; }
    uint64_t samples() const { return m_sample; }
    int activeVoices() const;

    struct Impl;
    struct PartState;

private:
    void apply(const StudioMsg &m);
    void renderBlock(float *l, float *r, int n);
    void publish();

    float m_rate;
    double m_time0 = 0;
    uint64_t m_sample = 0;
    double m_lastLoad = 0;
    std::unique_ptr<Impl> d;
    std::vector<std::unique_ptr<PartState>> m_parts;
    std::mutex m_postLock;
    SpscQueue<StudioMsg, 8192> m_queue;
    TripleBuffer<StudioView> m_views;

    float m_scope[kScopeSize] = {};
    std::atomic<uint32_t> m_scopeWrite{0};

    std::atomic<bool> m_recording{false};
    std::vector<float> m_record;
    std::atomic<size_t> m_recordHead{0}, m_recordTail{0};
};

}
