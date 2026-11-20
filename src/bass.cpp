// Bass: sub and growl. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeBass() {
    PartSpec s;
    s.name = QStringLiteral("Bass");
    s.blurb = QStringLiteral("sub and growl");
    s.color = QColor("#FF4FD8");
    starter::NotesConfig c;
    c.patch = Patch::Bass;
    c.octave = 0;
    c.span = 2;
    c.cutoff = 0.45f;
    c.res = 0.35f;
    c.sub = 0.6f;
    s.engine = std::make_unique<starter::NotesEngine>(c);
    s.control = std::make_unique<starter::NotesControl>(c, s.name);
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
