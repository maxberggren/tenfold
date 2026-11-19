#include "tuning.h"

#include <cmath>

namespace tf {

namespace {

// Five-limit just intonation for each equal-tempered semitone above the
// tonic. Held chords in these ratios don't beat, which is most of why a
// drone of stacked fingers sounds still rather than restless.
constexpr double justRatio[12] = {1.0,       16.0 / 15, 9.0 / 8, 6.0 / 5, 5.0 / 4,  4.0 / 3,
                                  45.0 / 32, 3.0 / 2,   8.0 / 5, 5.0 / 3, 9.0 / 5, 15.0 / 8};

const ScaleFamily families[] = {
    {"Diatonic", 7, {0, 200, 400, 500, 700, 900, 1100}, 7,
     // Lydian, Ionian, Mixolydian, Dorian, Aeolian, Phrygian, Locrian.
     {3, 0, 4, 1, 5, 2, 6},
     {"Lydian", "Ionian", "Mixolydian", "Dorian", "Aeolian", "Phrygian", "Locrian"}},
    {"Pentatonic", 5, {0, 200, 400, 700, 900}, 5,
     // Major, Yo, Suspended, Minor, Man Gong.
     {0, 3, 1, 4, 2},
     {"Major", "Yo", "Suspended", "Minor", "Man Gong"}},
    {"Harmonic minor", 7, {0, 200, 300, 500, 700, 800, 1100}, 7,
     {5, 2, 0, 3, 6, 1, 4},
     {"Ionian #5", "Phrygian dominant", "Harmonic minor", "Dorian #4", "Altered bb7", "Locrian #6",
      "Lydian #2"}},
    {"In", 5, {0, 100, 500, 700, 800}, 5,
     {3, 4, 2, 0, 1},
     {"Yo-in", "Kumoi", "Hon-kumoi", "In", "Iwato"}},
    // The 8th to 15th partials of the root: the scale the drones are made of.
    {"Overtones", 8,
     {0, 203.91, 386.31, 551.32, 701.96, 840.53, 968.83, 1088.27}, 8,
     {0, 2, 4, 6, 1, 3, 5, 7},
     {"Otonal", "Tenth", "Twelfth", "Fourteenth", "Ninth", "Eleventh", "Thirteenth", "Fifteenth"}},
};

}

const ScaleFamily &family(int index) {
    const int n = familyCount();
    return families[((index % n) + n) % n];
}

int familyCount() { return int(sizeof(families) / sizeof(families[0])); }

Tuning::Tuning() { rebuild(); }

void Tuning::set(int rootPc, int familyIndex, int brightnessIndex, bool just) {
    m_root = ((rootPc % 12) + 12) % 12;
    m_family = ((familyIndex % familyCount()) + familyCount()) % familyCount();
    const ScaleFamily &f = family(m_family);
    m_bright = brightnessIndex < 0 ? 0 : brightnessIndex >= f.modeCount ? f.modeCount - 1 : brightnessIndex;
    m_just = just;
    rebuild();
}

const char *Tuning::modeName() const { return family(m_family).modeNames[m_bright]; }
const char *Tuning::familyName() const { return family(m_family).name; }

void Tuning::rebuild() {
    const ScaleFamily &f = family(m_family);
    m_count = f.count;
    const int rotation = f.brightness[m_bright];
    const double start = f.cents[rotation];
    for (int i = 0; i < m_count; ++i) {
        double c = f.cents[(rotation + i) % m_count] - start;
        if (c < 0)
            c += 1200.0;
        // Families built on equal semitones take the just ratio of their
        // interval above the mode's own tonic; the overtone family is just by
        // construction and is left alone.
        const double nearest = std::round(c / 100.0);
        if (m_just && std::fabs(c - nearest * 100.0) < 1e-6) {
            const int semis = int(nearest) % 12;
            c = 1200.0 * std::log2(justRatio[semis]);
        }
        m_semis[i] = c / 100.0;
    }
}

double Tuning::degreeSemis(int degree) const {
    const int octave = degree >= 0 ? degree / m_count : -((-degree + m_count - 1) / m_count);
    const int index = degree - octave * m_count;
    return octave * 12.0 + m_semis[index];
}

double Tuning::continuousSemis(double degree) const {
    const double lower = std::floor(degree);
    const double frac = degree - lower;
    const double a = degreeSemis(int(lower));
    const double b = degreeSemis(int(lower) + 1);
    return a + (b - a) * frac;
}

double Tuning::semisPerDegree(double degree) const {
    const int lower = int(std::floor(degree));
    return degreeSemis(lower + 1) - degreeSemis(lower);
}

const char *Tuning::noteName(int pc) {
    static const char *names[12] = {"C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"};
    return names[((pc % 12) + 12) % 12];
}

}
