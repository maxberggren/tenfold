#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "touch.h"

namespace tf {

// Runs the helper (through pkexec unless the pad is already readable), reads
// its frames on a thread of its own and feeds them to the gesture layer
// directly: the touch path never waits on the screen.
class TouchSource {
public:
    enum Status { Idle, Starting, Ready, Grabbed, Failed };

    explicit TouchSource(FrameSink *sink);
    ~TouchSource();

    void start(const std::string &devicePath);
    void stop();
    void grab(bool on);

    Status status() const { return m_status.load(); }
    std::string error() const;
    std::string padName() const;
    float aspect() const { return m_aspect.load(); }
    int framesSeen() const { return m_frames.load(); }

private:
    void run();
    void handle(char *line);
    void send(const char *line);

    FrameSink *m_gesture;
    std::thread m_thread;
    int m_in = -1, m_out = -1;
    int m_pid = -1;
    std::atomic<Status> m_status{Idle};
    std::atomic<float> m_aspect{1.6f};
    std::atomic<int> m_frames{0};
    mutable std::mutex m_lock;
    std::string m_error, m_name;
    bool m_wantGrab = false;

    // Device ranges from the helper's info line.
    int m_xmin = 0, m_xmax = 1, m_ymin = 0, m_ymax = 1, m_majorMax = 0;
    int m_omin = 0, m_omax = 0, m_pmin = 0, m_pmax = 0;
    float m_fingerSize = 0, m_pressureTypical = 0;
    int m_learned = 0;
    int m_contactAge[256] = {}, m_lastSeen[256] = {};
};

}
