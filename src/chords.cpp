// Chords: stabs and progressions. A starter page until it has a design of its own.

#include "parts.h"
#include "starter.h"

namespace tf {

PartSpec makeChords() {
    PartSpec s;
    s.name = QStringLiteral("Chords");
    s.blurb = QStringLiteral("stabs and progressions");
    s.color = QColor("#2EE6FF");
    starter::NotesConfig c;
    c.patch = Patch::Chord;
    c.octave = 1;
    c.span = 2;
    c.chordTones = 4;
    s.engine = std::make_unique<starter::NotesEngine>(c);
    s.control = std::make_unique<starter::NotesControl>(c, s.name);
    const float channel[ChannelCount] = {0.7f, 0.5f, 0.5f, 0.35f, 0.1f, 0.6f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
