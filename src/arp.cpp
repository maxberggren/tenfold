// Arp: running notes. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeArp() {
    PartSpec s;
    s.name = QStringLiteral("Arp");
    s.blurb = QStringLiteral("running notes");
    s.color = QColor("#39FF88");
    starter::NotesConfig c;
    c.patch = Patch::Arp;
    c.octave = 2;
    c.span = 2;
    c.envDecay = 0.15f;
    s.engine = std::make_unique<starter::NotesEngine>(c);
    s.control = std::make_unique<starter::NotesControl>(c, s.name);
    const float channel[ChannelCount] = {0.7f, 0.5f, 0.5f, 0.3f, 0.35f, 0.5f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
