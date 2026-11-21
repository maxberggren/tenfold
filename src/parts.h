#pragma once

// The pages, in the order the number keys reach them. Each part lives in its
// own file and is built by its make function; the arrange page comes last.

#include <QColor>
#include <QStringList>
#include <vector>

#include "part.h"

namespace tf {

PartSpec makeDrums();  // 1
PartSpec makeBass();   // 2
PartSpec makeChords(); // 3
PartSpec makeLead();   // 4
PartSpec makeArp();    // 5
PartSpec makeAtmos();  // 6
PartSpec makeRisers(); // 7, the FX page
PartSpec makeVocal();  // 8, the microphone

std::vector<PartSpec> makeParts();
// The arrange page knows the parts by name and colour.
PartSpec makeArrange(const QStringList &names, const std::vector<QColor> &colors);

}
