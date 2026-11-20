// FX: risers, impacts, sweeps. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeRisers() {
    PartSpec s;
    s.name = QStringLiteral("FX");
    s.blurb = QStringLiteral("risers, impacts, sweeps");
    s.color = QColor("#FF4136");
    starter::NotesConfig c;
    c.patch = Patch::Lead;
    c.octave = 2;
    c.span = 3;
    c.res = 0.6f;
    s.engine = std::make_unique<starter::NotesEngine>(c);
    s.control = std::make_unique<starter::NotesControl>(c, s.name);
    const float channel[ChannelCount] = {0.7f, 0.5f, 0.5f, 0.45f, 0.2f, 0.2f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
