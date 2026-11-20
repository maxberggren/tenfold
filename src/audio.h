#pragma once

#include <atomic>
#include <string>

namespace tf {

class Studio;

// A PipeWire playback stream asking for a 128-frame quantum (2.7 ms at
// 48 kHz) and rendering on PipeWire's real-time thread.
class AudioOut {
public:
    explicit AudioOut(Studio *studio);
    ~AudioOut();

    bool start();
    void stop();
    std::string error() const { return m_error; }
    bool running() const { return m_running.load(); }
    int quantum() const { return m_quantum.load(); }
    int rate() const { return m_rate.load(); }

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

}
