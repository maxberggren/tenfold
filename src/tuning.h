#pragma once

#include <array>

// Pitch for tenfold. The pad is laid out in scale degrees, not semitones:
// every note of the current scale gets the same width under the finger, so a
// hand falls on music wherever it lands. Loops store degrees rather than
// pitches, which is what lets a change of mode or root reharmonise everything
// that is already playing.
namespace tf {

struct ScaleFamily {
    const char *name;
    int count;                 // degrees per octave
    std::array<double, 12> cents; // the parent scale, ascending, from 0
    // Mode rotations ordered from brightest to darkest, so a twist that turns
    // the scale turns it from light to shadow.
    int modeCount;
    std::array<int, 12> brightness;
    std::array<const char *, 12> modeNames;
};

const ScaleFamily &family(int index);
int familyCount();

class Tuning {
public:
    Tuning();

    void set(int rootPc, int familyIndex, int brightnessIndex, bool just);
    int root() const { return m_root; }
    int familyIndex() const { return m_family; }
    int brightnessIndex() const { return m_bright; }
    bool just() const { return m_just; }
    int degreesPerOctave() const { return m_count; }
    const char *modeName() const;
    const char *familyName() const;

    // MIDI pitch of degree 0, the root in the lowest playing octave.
    double baseMidi() const { return 36.0 + m_root; }

    // Semitones above baseMidi() for any integer degree, negative included.
    double degreeSemis(int degree) const;
    // A position between two degrees glides linearly between their pitches.
    double continuousSemis(double degree) const;
    // Local semitones per degree around a position, for scaling vibrato.
    double semisPerDegree(double degree) const;

    static const char *noteName(int pc);

private:
    void rebuild();

    // A natural minor, equal temperament.
    int m_root = 9;
    int m_family = 0;
    int m_bright = 4;
    bool m_just = false;
    int m_count = 7;
    std::array<double, 12> m_semis{}; // one octave of degrees, from 0
};

}
