// Drums: kick, snare, hats. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeDrums() {
    PartSpec s;
    s.name = QStringLiteral("Drums");
    s.blurb = QStringLiteral("kick, snare, hats");
    s.color = QColor("#FF8A1F");
    s.engine = std::make_unique<starter::DrumsEngine>();
    s.control = std::make_unique<starter::DrumsControl>();
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.12f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
