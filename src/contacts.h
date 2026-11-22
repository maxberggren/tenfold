#pragma once

#include "touch.h"

namespace tf {

// Turns a trackpad's raw contacts into Touches the pages can play: size
// relative to this player's own fingertip and pressure relative to how they
// usually touch, both learned as they play, so a palm is "much bigger than
// your finger" and a normal tap reads about a half on any pad. Every
// system's trackpad feeds its raw numbers through one of these, from the
// thread that reads the pad.
class Contacts {
public:
    // majorMax: the largest contact length the pad can report (0 when it
    // doesn't say). pressureRange: the width of its pressure scale, or 0
    // when it has no pressure sensor.
    void setScale(float majorMax, float pressureRange);

    // Call once per frame, before its contacts.
    void beginFrame() { ++m_frame; }

    // One contact of this frame. id is unique to the contact while it's
    // down; x and y are 0..1 with y up; major and minor are the contact
    // ellipse's axes (0 when unknown); angle is in radians; pressure is
    // raw, from the bottom of its scale (ignored without a sensor).
    Touch touch(int id, float x, float y, float major, float minor, float angle, float pressure);

private:
    float m_majorMax = 0, m_pressureRange = 0;
    float m_fingerSize = 0, m_pressureTypical = 0;
    int m_learned = 0;
    int m_frame = 0;
    int m_contactAge[256] = {}, m_lastSeen[256] = {};
};

}
