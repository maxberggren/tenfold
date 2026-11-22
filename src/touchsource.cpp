#include "touchsource.h"

#include <algorithm>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmath>
#include <ctime>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tf {

namespace {

bool onPath(const char *program) {
    const char *path = std::getenv("PATH");
    std::string dirs = path ? path : "/usr/bin:/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        const size_t end = std::min(dirs.find(':', start), dirs.size());
        const std::string dir = dirs.substr(start, end - start);
        if (!dir.empty() && access((dir + "/" + program).c_str(), X_OK) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

}

TouchSource::TouchSource(FrameSink *sink) : m_gesture(sink) {}

TouchSource::~TouchSource() { stop(); }

std::string TouchSource::error() const {
    std::lock_guard lock(m_lock);
    return m_error;
}

std::string TouchSource::padName() const {
    std::lock_guard lock(m_lock);
    return m_name;
}

void TouchSource::start(const std::string &devicePath) {
    if (m_thread.joinable())
        return;
    char self[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (len <= 0) {
        std::lock_guard lock(m_lock);
        m_error = "cannot find my own binary";
        m_status = Failed;
        return;
    }
    // Readable already (the input group, or the optional udev rule): no
    // password. Otherwise the helper runs as root through pkexec, which asks
    // for the password once per run of the app.
    const bool direct = access(devicePath.c_str(), R_OK) == 0;
    m_viaPassword = !direct;
    if (!direct && !onPath("pkexec")) {
        std::lock_guard lock(m_lock);
        m_error = "Reading the trackpad needs your password, and pkexec (polkit) isn't installed. "
                  "Install polkit, or let your user read touchpads (the README says how).";
        m_status = Failed;
        return;
    }

    int toChild[2], fromChild[2];
    if (pipe2(toChild, O_CLOEXEC) != 0 || pipe2(fromChild, O_CLOEXEC) != 0) {
        std::lock_guard lock(m_lock);
        m_error = std::strerror(errno);
        m_status = Failed;
        return;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        if (direct)
            execl(self, self, "helper", devicePath.c_str(), (char *)nullptr);
        else
            execlp("pkexec", "pkexec", self, "helper", devicePath.c_str(), (char *)nullptr);
        _exit(127);
    }
    close(toChild[0]);
    close(fromChild[1]);
    if (pid < 0) {
        close(toChild[1]);
        close(fromChild[0]);
        std::lock_guard lock(m_lock);
        m_error = std::strerror(errno);
        m_status = Failed;
        return;
    }
    m_pid = pid;
    m_in = toChild[1];
    m_out = fromChild[0];
    {
        std::lock_guard lock(m_lock);
        m_error.clear();
    }
    m_status = Starting;
    m_thread = std::thread([this] { run(); });
}

void TouchSource::stop() {
    if (m_in >= 0) {
        // Closing stdin is how the helper knows to let go of the pad; it runs
        // as root, so it can't simply be killed from here.
        close(m_in);
        m_in = -1;
    }
    if (m_thread.joinable())
        m_thread.join();
    if (m_pid > 0) {
        int status = 0;
        for (int i = 0; i < 30 && waitpid(m_pid, &status, WNOHANG) == 0; ++i)
            usleep(50000);
        m_pid = -1;
    }
    if (m_status != Failed)
        m_status = Idle;
}

void TouchSource::send(const char *line) {
    if (m_in < 0)
        return;
    const std::string s = std::string(line) + "\n";
    if (write(m_in, s.data(), s.size()) < 0) {
        // The helper is gone; the reader thread reports why.
    }
}

void TouchSource::grab(bool on) {
    m_wantGrab = on;
    if (m_status == Ready || m_status == Grabbed)
        send(on ? "grab" : "ungrab");
}

void TouchSource::run() {
    FILE *in = fdopen(m_out, "r");
    char *line = nullptr;
    size_t cap = 0;
    while (in && getline(&line, &cap, in) > 0)
        handle(line);
    std::free(line);
    if (in)
        std::fclose(in);
    m_out = -1;
    int status = 0;
    if (m_pid > 0 && waitpid(m_pid, &status, 0) == m_pid) {
        m_pid = -1;
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        std::lock_guard lock(m_lock);
        if (m_status == Starting) {
            // pkexec: 126 when the password dialog is dismissed, 127 when not
            // allowed, which is also what it says when no polkit agent is
            // running to show the dialog.
            if (code == 126)
                m_error = "The password prompt was closed.";
            else if (code == 127)
                m_error = m_viaPassword ? "Not allowed to read the trackpad, or no polkit agent is running to ask for the password."
                                        : "Not allowed to read the trackpad.";
            else if (m_error.empty())
                m_error = "Could not read the trackpad.";
            m_status = Failed;
        } else if (m_in >= 0) {
            if (m_error.empty())
                m_error = "Stopped reading the trackpad.";
            m_status = Failed;
        }
    }
    // Lift every finger that was down, so no voice hangs.
    Frame empty;
    empty.t = 0;
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    empty.t = double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
    m_gesture->frame(empty);
}

void TouchSource::handle(char *line) {
    if (line[0] == 'f' && line[1] == ' ') {
        char *p = line + 2;
        Frame f;
        f.t = double(std::strtoll(p, &p, 10)) * 1e-6;
        f.button = std::strtol(p, &p, 10) != 0;
        const float xr = float(std::max(1, m_xmax - m_xmin)), yr = float(std::max(1, m_ymax - m_ymin));
        while (*p == ' ' && f.count < int(f.touches.size())) {
            ++p;
            int v[7];
            for (int i = 0; i < 7; ++i) {
                v[i] = int(std::strtol(p, &p, 10));
                if (*p == ',')
                    ++p;
            }
            Touch &t = f.touches[f.count++];
            t.id = v[0];
            t.x = std::clamp((float(v[1]) - float(m_xmin)) / xr, 0.0f, 1.0f);
            t.y = 1.0f - std::clamp((float(v[2]) - float(m_ymin)) / yr, 0.0f, 1.0f);
            const float major = float(std::max(v[3], v[4])), minor = float(std::min(v[3], v[4]));
            // Contact size relative to this player's own fingertip, learned as
            // they play, so a palm is "much bigger than your finger" on any pad.
            // A contact is smallest on its first frames, while the finger is
            // still arriving, so only settled contacts (their fourth frame
            // on) teach the fingertip size, averaged over many touches; until
            // a few have, nothing counts as big.
            int &age = m_contactAge[t.id & 255];
            age = m_lastSeen[t.id & 255] + 1 == m_frames.load() ? age + 1 : 1;
            m_lastSeen[t.id & 255] = m_frames.load();
            if (major > 0 && age == 4) {
                if (m_learned == 0)
                    m_fingerSize = major;
                else if (major < m_fingerSize * 2.0f)
                    m_fingerSize += (major - m_fingerSize) * (m_learned < 10 ? 0.3f : 0.08f);
                ++m_learned;
                m_fingerSize = std::max(m_fingerSize, float(std::max(1, m_majorMax)) * 0.04f);
            }
            t.size = major > 0 && m_learned >= 3 ? major / m_fingerSize : 1.0f;
            t.eccentricity = major > 0 && minor > 0 ? 1.0f - minor / major : 0.0f;
            if (m_omax > m_omin) {
                const float mid = 0.5f * float(m_omin + m_omax), half = 0.5f * float(m_omax - m_omin);
                t.angle = (float(v[5]) - mid) / half * 1.5707963f;
            }
            if (m_pmax > m_pmin) {
                const float raw = float(v[6] - m_pmin);
                // Pressure relative to how this player usually touches, so an
                // ordinary tap reads about a half whatever the hardware's
                // scale. Pads report ranges far beyond what fingers reach
                // (and one hard click must not make every later tap read as
                // nothing), so the reference is learned from contacts a few
                // frames in, like the fingertip size.
                if (age == 6 && raw > 0) {
                    m_pressureTypical = m_pressureTypical <= 0
                                            ? raw
                                            : m_pressureTypical + (raw - m_pressureTypical) * 0.1f;
                    if (std::getenv("TENFOLD_DEBUG") && m_learned % 20 == 0)
                        std::fprintf(stderr, "tenfold: pressure %.0f of %d, typical %.0f\n", raw,
                                     m_pmax - m_pmin, m_pressureTypical);
                }
                const float ref = std::max(2.0f * m_pressureTypical, 0.002f * float(m_pmax - m_pmin) + 1.0f);
                t.pressure = m_pressureTypical > 0 ? std::clamp(raw / ref, 0.0f, 1.0f) : 0.5f;
            } else {
                t.pressure = std::clamp((t.size - 0.6f) / 0.8f, 0.0f, 1.0f);
            }
        }
        ++m_frames;
        // Only a grabbed pad is an instrument. Given back, it's a pointer
        // again, and pointing must not play.
        if (m_status == Grabbed)
            m_gesture->frame(f);
        return;
    }
    if (std::strncmp(line, "info ", 5) == 0) {
        int v[12] = {};
        char *p = line + 5;
        for (int i = 0; i < 12; ++i)
            v[i] = int(std::strtol(p, &p, 10));
        while (*p == ' ')
            ++p;
        std::string name(p);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            name.pop_back();
        m_xmin = v[0];
        m_xmax = std::max(v[0] + 1, v[1]);
        m_ymin = v[2];
        m_ymax = std::max(v[2] + 1, v[3]);
        m_majorMax = v[4];
        m_omin = v[7];
        m_omax = v[8];
        m_pmin = v[9];
        m_pmax = v[10];
        const float w = float(m_xmax - m_xmin) / float(v[5] > 0 ? v[5] : 1);
        const float h = float(m_ymax - m_ymin) / float(v[6] > 0 ? v[6] : 1);
        const float aspect = h > 0 ? w / h : 1.6f;
        m_aspect = aspect;
        m_gesture->setAspect(aspect);
        {
            std::lock_guard lock(m_lock);
            m_name = name;
        }
        m_status = Ready;
        if (m_wantGrab)
            send("grab");
        return;
    }
    if (std::strncmp(line, "ok grab", 7) == 0) {
        m_status = Grabbed;
        return;
    }
    if (std::strncmp(line, "ok ungrab", 9) == 0) {
        m_status = Ready;
        // Lift every finger that was down when the pad was given back.
        Frame empty;
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        empty.t = double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
        m_gesture->frame(empty);
        return;
    }
    if (std::strncmp(line, "err ", 4) == 0) {
        std::lock_guard lock(m_lock);
        m_error = std::string(line + 4);
        while (!m_error.empty() && m_error.back() == '\n')
            m_error.pop_back();
    }
}

}
