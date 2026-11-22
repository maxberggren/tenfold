#pragma once

#include <QObject>
#include <atomic>
#include <mutex>
#include <string>

#include "contacts.h"
#include "platform.h"
#include "touch.h"

namespace tf {

// The Mac trackpad, read through MultitouchSupport, the private framework
// the system's own gestures are built on: every finger with its ellipse and
// pressure, from the framework's thread, which feeds the gesture layer
// directly. It needs no permission. The framework is loaded at run time, so
// a Mac without it (or a future macOS that drops it) says so instead of
// failing to start.
//
// Taking the pad freezes the pointer and keeps the trackpad's clicks,
// scrolls and pinches from reaching the window, so fingers only play. The
// system's own swipes (Mission Control, spaces) still go through unless
// tenfold is allowed under Privacy & Security > Accessibility, which lets it
// hold those back too while the pad is taken.
class Trackpad : public QObject, public Pad {
    Q_OBJECT
public:
    explicit Trackpad(FrameSink *sink);
    ~Trackpad() override;

    bool found() const override { return m_device != nullptr; }
    bool needsPermission() const override { return false; }
    void start() override;
    void stop() override;
    void grab(bool on) override;

    Status status() const override { return m_status.load(); }
    std::string error() const override;
    std::string name() const override { return m_name; }
    float aspect() const override { return m_aspect; }

    // From the framework's thread.
    void contactFrame(const void *touches, int count, double timestamp);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void lift();
    void holdGestures(bool on);

    FrameSink *m_sink;
    void *m_device = nullptr;
    std::string m_name = "Trackpad";
    float m_aspect = 1.6f;
    std::atomic<Status> m_status{Idle};
    mutable std::mutex m_lock;
    std::string m_error;
    std::atomic<bool> m_button{false};
    void *m_tap = nullptr, *m_tapSource = nullptr;

    // The framework's thread only.
    Contacts m_contacts;
    double m_offset = 0;
    bool m_offsetSet = false;
    int m_nextId = 1;
    int m_idFor[64] = {};
    bool m_down[64] = {};
};

}
