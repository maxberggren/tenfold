// Atmos: pads and air. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeAtmos() {
    PartSpec s;
    s.name = QStringLiteral("Atmos");
    s.blurb = QStringLiteral("pads and air");
    s.color = QColor("#A98BFF");
    starter::NotesConfig c;
    c.patch = Patch::Chord;
    c.octave = 1;
    c.span = 2;
    c.cutoff = 0.4f;
    c.envAmt = 0.2f;
    c.envDecay = 0.9f;
    c.width = 0.8f;
    c.chordTones = 4;
    s.engine = std::make_unique<starter::NotesEngine>(c);
    s.control = std::make_unique<starter::NotesControl>(c, s.name);
    const float channel[ChannelCount] = {0.65f, 0.5f, 0.5f, 0.6f, 0.2f, 0.5f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
