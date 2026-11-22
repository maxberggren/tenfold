#pragma once

// Everything tenfold needs from the system it runs on, and nothing else: the
// trackpad read raw, the sound card, the desktop's file dialogs and the
// clock. The studio, the pages and the window only ever see these
// interfaces; each system implements them in a folder of its own (src/linux
// today, src/macos and src/windows when they come) and the project file
// compiles the one it is built on.
//
// A port is done when it implements everything below and `bin/test` passes
// on it: the tests never touch the platform, so they say nothing about it.

#include <QString>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace tf {

class FilePicker;
class FrameSink;
class Studio;

// Seconds on the system's monotonic clock. The audio engine runs on it and
// the trackpad stamps every touch with it, so loop timing comes from when
// each finger really landed. (On Linux this is CLOCK_MONOTONIC, the clock the
// kernel stamps input events with.)
inline double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The trackpad as an instrument: every finger with its size, pressure and
// angle, timestamped on monotonicSeconds(), delivered to the sink from a
// thread of the pad's own (never through the GUI). Taking it (grab) stops it
// from moving the pointer until it's given back.
class Pad {
public:
    enum Status { Idle, Starting, Ready, Grabbed, Failed };

    virtual ~Pad() = default;

    // A trackpad was found at all.
    virtual bool found() const = 0;
    // Starting will ask the person for their password (or the system's
    // equivalent permission dialog) before frames arrive.
    virtual bool needsPermission() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void grab(bool on) = 0;

    virtual Status status() const = 0;
    // Why it failed, said to the player as is.
    virtual std::string error() const = 0;
    virtual std::string name() const = 0;
    // Width over height, for drawing the pad the shape it is.
    virtual float aspect() const = 0;
};

// The sound card: renders the studio on the system's real-time audio thread
// at the smallest buffer it allows, and feeds the default microphone to the
// studio for the Vocal page.
class Audio {
public:
    virtual ~Audio() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    // Why there is no sound, or "".
    virtual std::string error() const = 0;
    // Why there is no microphone, or "".
    virtual std::string inputError() const = 0;
    // Frames per render and the sample rate the system settled on.
    virtual int quantum() const = 0;
    virtual int rate() const = 0;
};

namespace platform {

// The built-in trackpad (or the first one found), feeding frames to sink.
std::unique_ptr<Pad> openPad(FrameSink *sink);

std::unique_ptr<Audio> openAudio(Studio *studio);

// The desktop's own Open and Save dialogs. onError is told when no dialog
// can be shown, since to the player that would look like a key that does
// nothing.
std::unique_ptr<FilePicker> filePicker(std::function<void(const QString &)> onError);

// Commands only this system has, run before any of Qt starts (the Linux
// trackpad helper runs as root this way). Returns the exit code, or -1 when
// argv isn't one of them.
int runSystemCommand(int argc, char **argv);

// `tenfold latency`: what the system's audio path costs, measured, as
// text or JSON. Returns the exit code.
int measureLatency(bool json);

}

}
