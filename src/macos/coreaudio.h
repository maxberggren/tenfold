#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "platform.h"

namespace tf {

class Studio;

// The Mac's default output through CoreAudio, rendering on its real-time
// thread at a 128-frame buffer, and beside it the default microphone, mono,
// handed straight to the studio (the Vocal page monitors it). The studio
// runs at its own rate; the output unit converts to the device's, and the
// microphone is converted on the way in. macOS asks the player once whether
// tenfold may use the microphone; without it the studio plays on and the
// Vocal page says so.
class CoreAudioOut : public Audio {
public:
    explicit CoreAudioOut(Studio *studio);
    ~CoreAudioOut() override;

    bool start() override;
    void stop() override;
    bool running() const override { return m_running.load(); }
    std::string error() const override { return m_error; }
    std::string inputError() const override;
    int quantum() const override { return m_quantum.load(); }
    int rate() const override { return int(m_rate); }

    // The render and capture callbacks.
    void render(float *interleaved, int frames);
    void capture(void *flags, const void *timestamp, unsigned frames);

private:
    bool startInput();

    Studio *m_studio;
    float m_rate;
    void *m_output = nullptr, *m_input = nullptr;
    std::string m_error;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_quantum{0};
    bool m_clockSet = false, m_flushed = false;

    // The microphone.
    mutable std::mutex m_errorLock;
    std::string m_inputError;
    std::atomic<bool> m_inputFailed{false};
    std::atomic<uint64_t> m_captured{0};
    uint64_t m_seen = 0, m_quiet = 0;
    float m_inMs = 0, m_outMs = 0;
    double m_inRate = 0;      // the microphone's own rate
    double m_phase = 0;       // where the resampler is between its samples
    float m_last = 0;         // the sample before this buffer, for the resampler
    std::vector<float> m_in;  // what the device delivered
    std::vector<float> m_out; // at the studio's rate
};

// `tenfold latency` on a Mac: what the default devices and the buffer cost,
// as CoreAudio reports them.
int reportCoreAudioLatency(bool json);

}
