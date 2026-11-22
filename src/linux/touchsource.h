#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "contacts.h"
#include "platform.h"
#include "touch.h"

namespace tf {

// The Linux trackpad: the kernel's evdev device, read by `tenfold helper`
// (helper.h), which runs through pkexec unless the pad is already readable.
// Its frames are read on a thread of its own and fed to the gesture layer
// directly: the touch path never waits on the screen.
class TouchSource : public Pad {
public:
    // devicePath is /dev/input/eventN, or "" when there is no trackpad.
    TouchSource(FrameSink *sink, std::string devicePath);
    ~TouchSource() override;

    bool found() const override { return !m_path.empty(); }
    bool needsPermission() const override;
    void start() override;
    void stop() override;
    void grab(bool on) override;

    Status status() const override { return m_status.load(); }
    std::string error() const override;
    std::string name() const override;
    float aspect() const override { return m_aspect.load(); }

    // Started through pkexec because the pad isn't readable as is.
    bool viaPassword() const { return m_viaPassword.load(); }
    int framesSeen() const { return m_frames.load(); }

private:
    void run();
    void handle(char *line);
    void send(const char *line);

    FrameSink *m_gesture;
    std::string m_path;
    std::thread m_thread;
    int m_in = -1, m_out = -1;
    int m_pid = -1;
    std::atomic<Status> m_status{Idle};
    std::atomic<bool> m_viaPassword{false};
    std::atomic<float> m_aspect{1.6f};
    std::atomic<int> m_frames{0};
    mutable std::mutex m_lock;
    std::string m_error, m_name;
    bool m_wantGrab = false;

    // Device ranges from the helper's info line.
    int m_xmin = 0, m_xmax = 1, m_ymin = 0, m_ymax = 1, m_majorMax = 0;
    int m_omin = 0, m_omax = 0, m_pmin = 0, m_pmax = 0;
    Contacts m_contacts;
};

}
