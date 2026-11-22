#include "contacts.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace tf {

void Contacts::setScale(float majorMax, float pressureRange) {
    m_majorMax = majorMax;
    m_pressureRange = pressureRange;
}

Touch Contacts::touch(int id, float x, float y, float major, float minor, float angle, float pressure) {
    Touch t;
    t.id = id;
    t.x = std::clamp(x, 0.0f, 1.0f);
    t.y = std::clamp(y, 0.0f, 1.0f);
    if (minor > major)
        std::swap(major, minor);
    // Contact size relative to this player's own fingertip, learned as
    // they play, so a palm is "much bigger than your finger" on any pad.
    // A contact is smallest on its first frames, while the finger is
    // still arriving, so only settled contacts (their fourth frame on)
    // teach the fingertip size, averaged over many touches; until a few
    // have, nothing counts as big.
    int &age = m_contactAge[id & 255];
    age = m_lastSeen[id & 255] + 1 == m_frame ? age + 1 : 1;
    m_lastSeen[id & 255] = m_frame;
    if (major > 0 && age == 4) {
        if (m_learned == 0)
            m_fingerSize = major;
        else if (major < m_fingerSize * 2.0f)
            m_fingerSize += (major - m_fingerSize) * (m_learned < 10 ? 0.3f : 0.08f);
        ++m_learned;
        m_fingerSize = std::max(m_fingerSize, std::max(1.0f, m_majorMax) * 0.04f);
    }
    t.size = major > 0 && m_learned >= 3 ? major / m_fingerSize : 1.0f;
    t.eccentricity = major > 0 && minor > 0 ? 1.0f - minor / major : 0.0f;
    t.angle = angle;
    if (m_pressureRange > 0) {
        const float raw = pressure;
        // Pressure relative to how this player usually touches, so an
        // ordinary tap reads about a half whatever the hardware's scale.
        // Pads report ranges far beyond what fingers reach (and one hard
        // click must not make every later tap read as nothing), so the
        // reference is learned from contacts a few frames in, like the
        // fingertip size.
        if (age == 6 && raw > 0) {
            m_pressureTypical = m_pressureTypical <= 0 ? raw : m_pressureTypical + (raw - m_pressureTypical) * 0.1f;
            if (std::getenv("TENFOLD_DEBUG") && m_learned % 20 == 0)
                std::fprintf(stderr, "tenfold: pressure %.0f of %.0f, typical %.0f\n", raw, m_pressureRange,
                             m_pressureTypical);
        }
        const float ref = std::max(2.0f * m_pressureTypical, 0.002f * m_pressureRange + 1.0f);
        t.pressure = m_pressureTypical > 0 ? std::clamp(raw / ref, 0.0f, 1.0f) : 0.5f;
    } else {
        t.pressure = std::clamp((t.size - 0.6f) / 0.8f, 0.0f, 1.0f);
    }
    return t;
}

}
