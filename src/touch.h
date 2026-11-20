#pragma once

#include <array>

// What the trackpad reports, one frame at a time, after the touch source has
// put it in pad terms: positions 0..1 with y up, sizes relative to the
// player's own fingertip, pressure relative to their usual touch.
namespace tf {

struct Touch {
    int id = -1;
    float x = 0, y = 0;         // 0..1, y up
    float size = 0;             // contact length, relative to a normal fingertip (≈1)
    float pressure = 0;         // 0..1, a normal tap ≈ 0.5
    float angle = 0;            // radians, the contact ellipse's orientation
    float eccentricity = 0;     // 0 round .. 1 long; how much to trust the angle
};

struct Frame {
    double t = 0; // seconds on the kernel's monotonic clock (script time offline)
    bool button = false; // the pad is pressed down until it clicks
    int count = 0;
    std::array<Touch, 16> touches{};
};

// Where frames go: the session live, a part directly in tests.
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void frame(const Frame &f) = 0;
    // Called about a hundred times a second when no frames arrive, so held
    // back decisions (a lift that might be a lost frame) can go through.
    virtual void idle(double t) { (void)t; }
    // The pad's width over its height, once the touch source knows it.
    virtual void setAspect(float a) { (void)a; }
};

}
