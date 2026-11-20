#include "parts.h"

namespace tf {

std::vector<PartSpec> makeParts() {
    std::vector<PartSpec> parts;
    parts.push_back(makeDrums());
    parts.push_back(makeBass());
    parts.push_back(makeChords());
    parts.push_back(makeLead());
    parts.push_back(makeArp());
    parts.push_back(makeAtmos());
    parts.push_back(makeRisers());
    return parts;
}

}
