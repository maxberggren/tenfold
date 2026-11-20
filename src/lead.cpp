// Lead: melody. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeLead() {
    PartSpec s;
    s.name = QStringLiteral("Lead");
    s.blurb = QStringLiteral("melody");
    s.color = QColor("#FFB000");
    starter::NotesConfig c;
    c.patch = Patch::Lead;
    c.octave = 2;
    c.span = 2;
    s.engine = std::make_unique<starter::NotesEngine>(c);
    s.control = std::make_unique<starter::NotesControl>(c, s.name);
    const float channel[ChannelCount] = {0.75f, 0.5f, 0.5f, 0.3f, 0.3f, 0.4f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
