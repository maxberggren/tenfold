#include "touchsource.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tf {

TouchSource::TouchSource(Gesture *gesture) : m_gesture(gesture) {}

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
    // Readable already (the input group, or a udev rule): no password.
    const bool direct = access(devicePath.c_str(), R_OK) == 0;

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
            // pkexec: 126 when the password dialog is dismissed, 127 when not allowed.
            if (code == 126)
                m_error = "The password prompt was closed.";
            else if (code == 127)
                m_error = "Not allowed to read the trackpad.";
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
            if (major > 0) {
                if (m_fingerSize <= 0)
                    m_fingerSize = major;
                else if (major < m_fingerSize * 1.8f)
                    m_fingerSize += (major - m_fingerSize) * 0.002f;
                m_fingerSize = std::max(m_fingerSize, float(std::max(1, m_majorMax)) * 0.02f);
            }
            t.size = major > 0 && m_fingerSize > 0 ? major / m_fingerSize : 1.0f;
            t.eccentricity = major > 0 && minor > 0 ? 1.0f - minor / major : 0.0f;
            if (m_omax > m_omin) {
                const float mid = 0.5f * float(m_omin + m_omax), half = 0.5f * float(m_omax - m_omin);
                t.angle = (float(v[5]) - mid) / half * 1.5707963f;
            }
            if (m_pmax > m_pmin) {
                const float raw = float(v[6] - m_pmin);
                // Pressure relative to the hardest the player has pressed
                // lately, never less than a fifth of the hardware's range.
                m_pressureRef = std::max({m_pressureRef * 0.99998f, raw, 0.2f * float(m_pmax - m_pmin)});
                t.pressure = std::clamp(raw / m_pressureRef, 0.0f, 1.0f);
            } else {
                t.pressure = std::clamp((t.size - 0.6f) / 0.8f, 0.0f, 1.0f);
            }
        }
        ++m_frames;
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
