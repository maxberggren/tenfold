#pragma once

#include <atomic>
#include <string>

#include "platform.h"

namespace tf {

class Studio;

// A PipeWire playback stream asking for a 128-frame quantum (2.7 ms at
// 48 kHz) and rendering on PipeWire's real-time thread, and beside it a
// capture stream from the default microphone at the same quantum, which
// hands what it hears straight to the studio for the next render (the
// Vocal page monitors it, so every sample of buffering is heard).
class PipeWireAudio : public Audio {
public:
    explicit PipeWireAudio(Studio *studio);
    ~PipeWireAudio() override;

    bool start() override;
    void stop() override;
    std::string error() const override { return m_error; }
    bool running() const override { return m_running.load(); }
    int quantum() const override { return m_quantum.load(); }
    int rate() const override { return m_rate.load(); }
    std::string inputError() const override;

    struct Impl;

private:
    Studio *m_engine;
    Impl *d = nullptr;
    std::string m_error;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_quantum{0}, m_rate{48000};
    friend struct Impl;
    friend void onProcess(void *);
};

// `tenfold latency` on PipeWire: how long the voice takes from the
// microphone to the ears, measured. The real studio and its two streams run
// against a pair of private virtual devices (a source that sends a burst of
// noise, a sink that records), through the real PipeWire graph at the real
// quantum, with the Vocal page monitoring, so nothing is heard; then the
// devices this machine would use report their own latency. The report says
// both.
int measurePipeWireLatency(bool json);

// `tenfold output`: what the default sink plays through, speakers or
// headphones, as the Vocal page's monitor sees it, and every sink's.
int describePipeWireOutput();

}
