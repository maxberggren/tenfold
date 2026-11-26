#pragma once

// The studio: every part's engine, the clock they share, the clips they
// loop, the mixer channel each plays through, the room and the master bus.
// It runs on the audio thread and hears only messages from a lock-free
// queue; the session (touch and UI threads) is its only producer.

#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "dsp.h"
#include "part.h"

namespace tf {

// A message to the studio itself, or through it to a part.
// How many changes to its loops each part can take back (Ctrl+Z).
constexpr int kUndoSteps = 16;

// The most events a clip holds.
constexpr int kClipEvents = 2048;

// One event of a clip: a command at a position in beats from the clip's
// start, as the studio replays it and a song file keeps it.
struct ClipEvent {
    double pos = 0;
    Command c;
    double after = -1e18; // an overdub: not replayed before this beat (it was just heard live)
    int layer = 0;        // 0 the first take, 1… what was layered on top (see replayLayer)
};

// Every part's clips at once, copied out of the studio to save a song and
// handed to it to open one (Studio::readClips, Studio::writeClips).
struct ClipSet {
    struct Clip {
        std::vector<ClipEvent> events;
        double length = 0; // beats; 0 = empty
        double anchor = 0; // a beat where the clip's position is 0
    };
    int parts = 0;
    Clip clips[kMaxParts][kClipSlots];
    int playing[kMaxParts];
    ClipSet() { std::fill(std::begin(playing), std::end(playing), -1); }
    bool tooSmall = false; // reading: a clip had more events than room was made for
};

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
        UndoTake,    // part: take back that part's latest take or edit, and play what played before
        RedoTake,    // part: bring back the last step Ctrl+Z took back
        Retake,      // as TakeBars, in place of the take just made into that slot (a longer or shorter window)
        Recapture,   // as Capture, in place of the capture just made into that slot
        CopyClip,    // part, slot = from, value = to
        MergeClips,  // part, slot = into, value = the other, f = 0 layer / 1 chain
        ClearAll,    // every clip of every part goes, every sound stops: start over
        ReadClips,   // ptr = a ClipSet, value = ticket: every clip copied into it
        WriteClips,  // ptr = a ClipSet, value = ticket: every clip swapped for its own,
                     // nothing playing, nothing to take back
        Resume,      // d0 = for each part four bits, the slot it plays again + 1 (0: none), as a
                     // whole number: a pause ends, from the top of a bar, at once if nothing plays
    };
    Kind kind = ToPart;
    int8_t part = 0;
    int8_t slot = 0;
    int32_t value = 0;
    double d0 = 0, d1 = 0;
    double d2 = -1e18; // TakeBars layered: nothing played before this beat goes in (it is in the loop already)
    float f = 0;
    // TakeBars, Retake, Capture, Recapture: the clip is recorded audio, the
    // one sound cmd (see ControlContext::takeAudio), not what was played.
    bool audio = false;
    Command cmd;
    void *ptr = nullptr;
};

class Studio {
public:
    explicit Studio(float rate = 48000.0f);
    ~Studio();

    // Before audio starts.
    // monitors: the part plays live input, mixed after the limiter (PartSpec::monitors).
    int addPart(std::unique_ptr<PartEngine> engine, const float *channel, bool monitors = false);
    int partCount() const { return int(m_parts.size()); }
    PartEngine *engine(int part) const;

    float sampleRate() const { return m_rate; }
    // Touch times are seconds on the same clock as this origin: the kernel's
    // monotonic clock live, script time offline.
    void setTimeOrigin(double seconds) { m_time0 = seconds; }
    double timeOrigin() const { return m_time0; }

    // Producer side; safe from any thread (one at a time is enforced here).
    bool post(const StudioMsg &m);

    // Saving and opening a song. Both wait for the audio thread to do the
    // work between two blocks (after every message posted before them), or
    // do it at once on the thread that renders (offline, the tests) or when
    // nothing renders yet. False if the audio thread didn't answer in time.
    // readClips makes its own room in `out`; writeClips takes `in`'s clips
    // and leaves the studio's old ones in it, to be freed off the audio thread.
    bool readClips(ClipSet &out);
    bool writeClips(ClipSet &in);

    // Interleaved stereo. Audio thread only.
    void render(float *interleaved, int frames);

    // The microphone, mono at the studio's rate: the capture stream (or a
    // script) pushes what arrives, and each render takes as many samples as
    // it renders, keeping as little waiting as it can. One pushing thread.
    static constexpr size_t kInputRing = 1 << 15;
    void pushInput(const float *mono, int frames);
    // What the audio server says about the microphone and the latencies
    // (milliseconds each way, the devices' included); see StudioView.
    void setInput(int state, float inputMs = 0, float outputMs = 0);
    // Samples waiting in the input ring after the last render took its share.
    int inputWaiting() const { return m_inReserve.load(std::memory_order_relaxed); }

    bool fetchView() { return m_views.fetch(); }
    const StudioView &view() const { return m_views.front(); }

    // Mono output history for the screen, written without locks.
    static constexpr int kScopeSize = 8192;
    void copyScope(float *out, int n) const;

    // Recording tap: a ring of interleaved stereo a recorder thread drains.
    static constexpr size_t kRecordFrames = 1 << 19;
    void setRecording(bool on) {
        // Nothing is written while it is off, so the head stands still here.
        if (on)
            m_recordBase = m_recordHead.load(std::memory_order_acquire);
        m_recording.store(on, std::memory_order_release);
    }
    // Frames recorded since the last setRecording(true): the recording's
    // length so far, which the video recorder keeps its frames in step with.
    uint64_t recordedFrames() const { return m_recordHead.load(std::memory_order_acquire) - m_recordBase; }
    bool recording() const { return m_recording.load(std::memory_order_acquire); }
    size_t drainRecording(float *out, size_t maxFrames);

    // Debugging: every On a part hears (live or replayed from a slot) and
    // every take made, in a ring the session drains into its log.
    struct DebugEvent {
        int kind = 0;  // 0 an On heard, 1 a take made, 2 one of its events, 3 a pass into a loop, 4 an undo
        int part = -1;
        int src = -1;  // kind 0: −1 live, else the slot it replayed from; kinds 1/2: the slot
        double t = 0, heard = 0, beat = 0; // kind 0
        double a = 0, b = 0, len = 0, anchor = 0; // kind 1: the window; kind 2: a = pos
        int id = 0, i0 = 0, flags = 0, type = 0, events = 0;
        float v = 0;
    };
    bool popDebug(DebugEvent &e);

    double lastBlockLoad() const { return m_lastLoad; }
    uint64_t samples() const { return m_sample; }
    int activeVoices() const;

    struct Impl;
    struct PartState;

private:
    void apply(const StudioMsg &m);
    void drain();
    bool exchange(StudioMsg::Kind kind, ClipSet &set);
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
    // The thread that last rendered, and the last ticket readClips or
    // writeClips handed it, done.
    std::atomic<std::thread::id> m_renderer{};
    std::atomic<int32_t> m_exchanged{0};
    int32_t m_ticket = 0;
    TripleBuffer<StudioView> m_views;

    float m_scope[kScopeSize] = {};
    std::atomic<uint32_t> m_scopeWrite{0};

    std::vector<float> m_input;
    std::atomic<uint64_t> m_inHead{0}, m_inTail{0};
    std::atomic<int> m_inState{0}, m_inReserve{0};
    std::atomic<float> m_inMs{0}, m_outMs{0};
    int m_inSlack = 0;        // samples held back after an underrun, so the next doesn't come
    int m_inLow = 1 << 30;    // the least left waiting over the last second
    uint64_t m_inCalm = 0;    // samples rendered since the last underrun
    float m_inLast = 0;
    uint64_t m_inLowSince = 0;
    int m_quantum = 128;
    void pullInput(float *dst, int n);

    std::atomic<bool> m_recording{false};
    std::vector<float> m_record;
    std::atomic<size_t> m_recordHead{0}, m_recordTail{0};
    size_t m_recordBase = 0;
};

}
