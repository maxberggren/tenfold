#include "trackpad.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace tf {

namespace {

// MultitouchSupport's contact record, as every program that reads the Mac
// trackpad has declared it (the framework ships no headers). 96 bytes.
struct MTPoint {
    float x, y;
};
struct MTVector {
    MTPoint position, velocity;
};
struct MTTouch {
    int32_t frame;
    double timestamp;
    int32_t pathIndex; // which contact, while it's down
    int32_t state;     // see below
    int32_t fingerId;
    int32_t handId;
    MTVector normalized; // 0..1 across the pad, y up
    float zTotal;        // how much finger is on the glass: size and pressure
    int32_t field9;
    float angle; // radians, the ellipse's long axis
    float majorAxis;
    float minorAxis;
    MTVector millimeters;
    int32_t field14;
    int32_t field15;
    float zDensity;
};
static_assert(sizeof(MTTouch) == 96, "MultitouchSupport's contact record is 96 bytes");

// Hovering and leaving contacts are reported too; only these two are a
// finger on the glass.
constexpr int32_t kMakeTouch = 3, kTouching = 4;

using MTDeviceRef = void *;
using MTFrameCallback = int (*)(MTDeviceRef, const MTTouch *, int32_t, double, int32_t);

struct Framework {
    MTDeviceRef (*createDefault)() = nullptr;
    void (*registerCallback)(MTDeviceRef, MTFrameCallback) = nullptr;
    void (*unregisterCallback)(MTDeviceRef, MTFrameCallback) = nullptr;
    void (*start)(MTDeviceRef, int32_t) = nullptr;
    void (*stop)(MTDeviceRef) = nullptr;
    bool (*isBuiltIn)(MTDeviceRef) = nullptr;
    int32_t (*dimensions)(MTDeviceRef, int32_t *, int32_t *) = nullptr;

    bool load() {
        void *lib = dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport", RTLD_LAZY);
        if (!lib)
            return false;
        createDefault = reinterpret_cast<decltype(createDefault)>(dlsym(lib, "MTDeviceCreateDefault"));
        registerCallback = reinterpret_cast<decltype(registerCallback)>(dlsym(lib, "MTRegisterContactFrameCallback"));
        unregisterCallback =
            reinterpret_cast<decltype(unregisterCallback)>(dlsym(lib, "MTUnregisterContactFrameCallback"));
        start = reinterpret_cast<decltype(start)>(dlsym(lib, "MTDeviceStart"));
        stop = reinterpret_cast<decltype(stop)>(dlsym(lib, "MTDeviceStop"));
        isBuiltIn = reinterpret_cast<decltype(isBuiltIn)>(dlsym(lib, "MTDeviceIsBuiltIn"));
        dimensions = reinterpret_cast<decltype(dimensions)>(dlsym(lib, "MTDeviceGetSensorSurfaceDimensions"));
        return createDefault && registerCallback && start && stop;
    }
};

Framework &framework() {
    static Framework f;
    static const bool loaded = f.load();
    (void)loaded;
    return f;
}

// The framework calls back without a context pointer: there is one pad.
std::atomic<Trackpad *> g_pad{nullptr};

int onFrame(MTDeviceRef, const MTTouch *touches, int32_t count, double timestamp, int32_t) {
    if (Trackpad *pad = g_pad.load(std::memory_order_acquire))
        pad->contactFrame(touches, count, timestamp);
    return 0;
}

// The system's gestures, held back while the pad is taken (only when
// tenfold is allowed to watch input: Privacy & Security > Accessibility).
// Rotate, begin and end gesture, gesture, magnify, swipe, smart magnify.
constexpr int kGestureTypes[] = {18, 19, 20, 29, 30, 31, 32};

CGEventRef onGestureEvent(CGEventTapProxy, CGEventType type, CGEventRef event, void *data) {
    auto *tap = static_cast<CFMachPortRef *>(data);
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (tap && *tap)
            CGEventTapEnable(*tap, true);
        return event;
    }
    return nullptr;
}

}

Trackpad::Trackpad(FrameSink *sink) : m_sink(sink) {
    Framework &mt = framework();
    if (mt.createDefault)
        m_device = mt.createDefault();
    if (m_device) {
        if (mt.isBuiltIn)
            m_name = mt.isBuiltIn(m_device) ? "Built-in trackpad" : "Magic Trackpad";
        int32_t w = 0, h = 0;
        // Hundredths of a millimetre.
        if (mt.dimensions && mt.dimensions(m_device, &w, &h) == 0 && w > 0 && h > 0)
            m_aspect = float(w) / float(h);
    }
    // MultitouchSupport's axes run to about the width of a finger in its
    // own units, and its pressure has no fixed scale: a nominal one.
    m_contacts.setScale(0, 1000);
    QCoreApplication::instance()->installEventFilter(this);
}

Trackpad::~Trackpad() {
    stop();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
}

std::string Trackpad::error() const {
    std::lock_guard lock(m_lock);
    return m_error;
}

void Trackpad::start() {
    if (m_status == Ready || m_status == Grabbed)
        return;
    if (!m_device) {
        std::lock_guard lock(m_lock);
        m_error = framework().createDefault ? "No trackpad found." : "This Mac can't read its trackpad (no MultitouchSupport).";
        m_status = Failed;
        return;
    }
    g_pad.store(this, std::memory_order_release);
    Framework &mt = framework();
    mt.registerCallback(m_device, onFrame);
    mt.start(m_device, 0);
    {
        std::lock_guard lock(m_lock);
        m_error.clear();
    }
    m_status = Ready;
}

void Trackpad::stop() {
    if (m_status == Grabbed)
        grab(false);
    if (m_device && (m_status == Ready || m_status == Grabbed)) {
        Framework &mt = framework();
        if (mt.unregisterCallback)
            mt.unregisterCallback(m_device, onFrame);
        mt.stop(m_device);
    }
    g_pad.store(nullptr, std::memory_order_release);
    if (m_status != Failed)
        m_status = Idle;
}

void Trackpad::grab(bool on) {
    if (on && m_status == Ready) {
        // The pointer stays where it is and out of sight while fingers play.
        CGAssociateMouseAndMouseCursorPosition(false);
        CGDisplayHideCursor(kCGDirectMainDisplay);
        holdGestures(true);
        m_status = Grabbed;
    } else if (!on && m_status == Grabbed) {
        holdGestures(false);
        CGDisplayShowCursor(kCGDirectMainDisplay);
        CGAssociateMouseAndMouseCursorPosition(true);
        m_status = Ready;
        lift();
    }
}

void Trackpad::holdGestures(bool on) {
    if (on) {
        if (m_tap || !AXIsProcessTrusted())
            return;
        CGEventMask mask = 0;
        for (int type : kGestureTypes)
            mask |= CGEventMaskBit(type);
        auto *holder = new CFMachPortRef(nullptr);
        CFMachPortRef tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                                             mask, onGestureEvent, holder);
        if (!tap) {
            delete holder;
            return;
        }
        *holder = tap;
        CFRunLoopSourceRef source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
        CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CGEventTapEnable(tap, true);
        m_tap = tap;
        m_tapSource = source;
    } else if (m_tap) {
        auto tap = static_cast<CFMachPortRef>(m_tap);
        auto source = static_cast<CFRunLoopSourceRef>(m_tapSource);
        CGEventTapEnable(tap, false);
        CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CFMachPortInvalidate(tap);
        CFRelease(source);
        CFRelease(tap);
        // The holder the callback reads is left: a late event may still be
        // on its way to it.
        m_tap = m_tapSource = nullptr;
    }
}

void Trackpad::lift() {
    Frame empty;
    empty.t = monotonicSeconds();
    m_sink->frame(empty);
}

bool Trackpad::eventFilter(QObject *watched, QEvent *event) {
    if (m_status != Grabbed)
        return QObject::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        // A click on the glass is the pad's button: the pages hear it
        // through the frames, the window doesn't.
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
            m_button = event->type() == QEvent::MouseButtonPress;
        return true;
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::HoverMove:
    case QEvent::Wheel:
    case QEvent::NativeGesture:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        return true;
    default:
        return QObject::eventFilter(watched, event);
    }
}

void Trackpad::contactFrame(const void *data, int count, double timestamp) {
    const auto *touches = static_cast<const MTTouch *>(data);
    // The framework stamps frames on its own clock; the studio runs on
    // monotonicSeconds(). The offset between them is the smallest seen (the
    // frame that arrived quickest), kept current as the clocks drift.
    const double now = monotonicSeconds();
    const double offset = now - timestamp;
    if (!m_offsetSet || std::fabs(offset - m_offset) > 0.05)
        m_offset = offset;
    else
        m_offset = std::min(offset, m_offset + 1e-4);
    m_offsetSet = true;

    Frame f;
    f.t = timestamp + m_offset;
    f.button = m_button.load(std::memory_order_relaxed);
    bool down[64] = {};
    m_contacts.beginFrame();
    for (int i = 0; i < count && f.count < int(f.touches.size()); ++i) {
        const MTTouch &c = touches[i];
        if (c.state != kMakeTouch && c.state != kTouching)
            continue;
        const int slot = c.pathIndex & 63;
        // A fresh id for every new contact, as the kernel gives on Linux:
        // the framework reuses its path numbers.
        if (!m_down[slot])
            m_idFor[slot] = m_nextId = m_nextId % 100000 + 1;
        down[slot] = true;
        // The ellipse's angle from the long axis lying across the pad, as
        // radians either side of upright.
        const float angle = c.angle - 1.5707963f;
        f.touches[f.count++] = m_contacts.touch(m_idFor[slot], c.normalized.position.x, c.normalized.position.y,
                                                c.majorAxis, c.minorAxis, angle, c.zTotal * 1000.0f);
    }
    std::copy(std::begin(down), std::end(down), std::begin(m_down));
    // Only a taken pad is an instrument. Given back, it's a pointer again,
    // and pointing must not play.
    if (m_status == Grabbed)
        m_sink->frame(f);
}

}
