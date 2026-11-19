#include "helper.h"

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kSlots = 16;

struct Slot {
    int id = -1, x = 0, y = 0, major = 0, minor = 0, orientation = 0, pressure = 0;
};

bool hasBit(const unsigned long *bits, int bit) {
    return bits[bit / (8 * sizeof(long))] & (1UL << (bit % (8 * sizeof(long))));
}

input_absinfo absInfo(int fd, int code) {
    input_absinfo info{};
    ioctl(fd, EVIOCGABS(code), &info);
    return info;
}

bool validPath(const char *path) {
    // Only /dev/input/eventN: this runs as root and must not open anything else.
    const char *prefix = "/dev/input/event";
    const size_t n = std::strlen(prefix);
    if (std::strncmp(path, prefix, n) != 0)
        return false;
    const char *digits = path + n;
    const size_t len = std::strlen(digits);
    if (len < 1 || len > 3)
        return false;
    return std::all_of(digits, digits + len, [](char c) { return c >= '0' && c <= '9'; });
}

}

int runHelper(const char *devicePath) {
    std::setvbuf(stdout, nullptr, _IOFBF, 1 << 14);
    if (!validPath(devicePath)) {
        std::fprintf(stderr, "tenfold helper: not an input event device: %s\n", devicePath);
        return 2;
    }
    const int fd = open(devicePath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "tenfold helper: cannot open %s: %s\n", devicePath, std::strerror(errno));
        return 1;
    }
    unsigned long absBits[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
    if (!hasBit(absBits, ABS_MT_SLOT) || !hasBit(absBits, ABS_MT_TRACKING_ID) || !hasBit(absBits, ABS_MT_POSITION_X)) {
        std::fprintf(stderr, "tenfold helper: %s is not a multitouch pad\n", devicePath);
        return 1;
    }
    // Timestamps on the monotonic clock, the one the app's audio clock uses,
    // so loop timing comes straight from the kernel's own record of each tap.
    int clock = CLOCK_MONOTONIC;
    ioctl(fd, EVIOCSCLOCKID, &clock);

    char name[256] = {};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    const auto x = absInfo(fd, ABS_MT_POSITION_X), y = absInfo(fd, ABS_MT_POSITION_Y);
    const auto major = absInfo(fd, ABS_MT_TOUCH_MAJOR), orient = absInfo(fd, ABS_MT_ORIENTATION),
               pressure = absInfo(fd, ABS_MT_PRESSURE);
    const bool hasPressure = hasBit(absBits, ABS_MT_PRESSURE);
    const bool hasOrient = hasBit(absBits, ABS_MT_ORIENTATION);
    const bool hasMajor = hasBit(absBits, ABS_MT_TOUCH_MAJOR);
    std::printf("info %d %d %d %d %d %d %d %d %d %d %d %d %s\n", x.minimum, x.maximum, y.minimum, y.maximum,
                hasMajor ? major.maximum : 0, x.resolution, y.resolution, hasOrient ? orient.minimum : 0,
                hasOrient ? orient.maximum : 0, hasPressure ? pressure.minimum : 0, hasPressure ? pressure.maximum : 0,
                0, name);
    std::fflush(stdout);

    Slot slots[kSlots];
    int slot = std::max(0, absInfo(fd, ABS_MT_SLOT).value);
    bool button = false;
    std::string pending;

    pollfd fds[2] = {{fd, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
    for (;;) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::printf("err device gone\n");
            std::fflush(stdout);
            break;
        }
        if (fds[0].revents & POLLIN) {
            input_event events[64];
            ssize_t n;
            bool wrote = false;
            while ((n = read(fd, events, sizeof(events))) > 0) {
                for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
                    const input_event &e = events[i];
                    if (e.type == EV_KEY && e.code == BTN_LEFT) {
                        button = e.value != 0;
                    } else if (e.type == EV_ABS) {
                        if (e.code == ABS_MT_SLOT) {
                            slot = e.value;
                            continue;
                        }
                        if (slot < 0 || slot >= kSlots)
                            continue;
                        Slot &s = slots[slot];
                        switch (e.code) {
                        case ABS_MT_TRACKING_ID:
                            if (e.value < 0)
                                s = Slot{};
                            else
                                s.id = e.value;
                            break;
                        case ABS_MT_POSITION_X: s.x = e.value; break;
                        case ABS_MT_POSITION_Y: s.y = e.value; break;
                        case ABS_MT_TOUCH_MAJOR: s.major = e.value; break;
                        case ABS_MT_TOUCH_MINOR: s.minor = e.value; break;
                        case ABS_MT_ORIENTATION: s.orientation = e.value; break;
                        case ABS_MT_PRESSURE: s.pressure = e.value; break;
                        }
                    } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
                        const long long us = (long long)e.input_event_sec * 1000000 + e.input_event_usec;
                        std::printf("f %lld %d", us, button ? 1 : 0);
                        for (const Slot &s : slots)
                            if (s.id >= 0)
                                std::printf(" %d,%d,%d,%d,%d,%d,%d", s.id, s.x, s.y, s.major, s.minor, s.orientation,
                                            s.pressure);
                        std::putchar('\n');
                        wrote = true;
                    }
                }
            }
            if (wrote)
                std::fflush(stdout);
            if (n < 0 && errno != EAGAIN && errno != EINTR) {
                std::printf("err device %s\n", std::strerror(errno));
                std::fflush(stdout);
                break;
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            char buffer[512];
            const ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (n <= 0)
                break; // The app closed stdin or died: let go of the pad and exit.
            pending.append(buffer, size_t(n));
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                const std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (line == "grab" || line == "ungrab") {
                    const bool on = line == "grab";
                    if (ioctl(fd, EVIOCGRAB, on ? 1 : 0) == 0 || (!on && errno == EINVAL))
                        std::printf("ok %s\n", line.c_str());
                    else
                        std::printf("err %s %s\n", line.c_str(), std::strerror(errno));
                    std::fflush(stdout);
                }
            }
            if (pending.size() > 4096)
                break;
        }
    }
    ioctl(fd, EVIOCGRAB, 0);
    close(fd);
    return 0;
}
