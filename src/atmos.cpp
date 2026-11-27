// Atmos: the mood and the background of a track. Ten fingers lay down a bed
// of sound under everything else: a deep drone, lush pads, an airy choir,
// shimmering glass and air (wind, rain, vinyl), each finger one layer.
//
// The pad is five columns, warm and low on the left to bright and airy on the
// right, so a hand laid flat across it plays a whole bed at once:
//
//   DRONE   root and fifth, a slow evolving filter
//   PAD     an ensemble pad: strings, a supersaw, a hollow pulse
//   CHOIR   voices through a vowel filter: oo, oh, ah, eh, ee
//   GLASS   pure glassy partials, and twinkles in time with the beat
//   AIR     tuned wind, rain with droplets in the key, vinyl crackle
//
// Per finger: where it lands is the layer; across its column, the colour;
// height the voicing (chord tones in a sliding window, so the bed climbs
// through its inversions without a single note gliding); pressure the swell;
// twist the motion (clockwise a slow filter sweep, anticlockwise a gate in
// time); a flat finger is wider. Every note is a tone of the chord of the
// moment (the Chords page's, or the bed's own from the bottom letter row),
// so the bed is consonant by construction and follows key and chord changes,
// with common tones held and the rest crossfaded.
//
// The sound is PADsynth: every timbre is a few seconds of spectrum, each
// harmonic smeared into a band of random-phase partials, rendered once at
// start. Playing a note is reading the table at a speed, so a layer of five
// notes is ten table reads a sample: cheap enough for ten fingers, and lush
// in a way stacked saws never are. Behind the layers sits the bed's own
// halo, a feedback delay network with octave shimmer that can freeze, and an
// optional grain cloud of the bed itself in octaves.
//
// Fifteen scenes lay whole beds at a key (Q to P, Shift+Q to T, or ← →).
// Three of the first ten reach past the
// classic bed into styles of their own: Nostalgia, lo-fi tape, felt keys
// struck on every chord and every layer on a worn cassette that wows; Neon,
// synthwave, a divide-down string machine and a pulse-width poly through
// one shared ensemble chorus; and Dub, dub techno, the pad struck as
// minor-ninth chord stabs on the offbeats into one shared tape echo with its
// hiss. The style the fingers play in is a choice of its own (L, or its
// chips in the band): laying one of those scenes sets it, fading that scene
// with its own key sets it back to classic. A style's bus is one for all its
// layers, never one per voice.
//
// The last five go further from the dance floor, each with a style whose pad
// is an instrument with a figure of its own. Four are struck note by note
// from a shared pool of strikes rather than read from a table: Gamelan,
// Balinese bronze in detuned pairs on a pelog five of the key, polos and
// sangsih interlocking in turns into each coming tone of the pokok, the
// kempli, kemong, kempur and great gong marking the gongan; Raga, a
// tanpura's four strings on the key's Sa, plucked round (Karplus-Strong,
// the string buzzing on its curved bridge, the buzz climbing as the swing
// narrows) over sympathetic strings that follow the chords; Mbira, a Shona
// thumb piano in twelve-eight round a cycle of forty-eight pulses, long
// bass tines under the left thumb, tunes that fall by step under the
// right, its caps rattling; City pop, a DX
// electric piano on rootless sevenths, ninths and elevenths, its bark
// band-limited key by key, with a fingered bass, a shaker and a rim when
// the hand comps. The fifth is a
// voice: Khoomei, Tuvan throat singing, a drone on the key's tonic with a
// breathing whistle that picks single overtones out of it as a melody.

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "console.h"
#include "dsp.h"
#include "part.h"
#include "parts.h"
#include "phrase.h"
#include "songfile.h"

namespace tf {

namespace {

inline float smoothstep(float a, float b, float x) {
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// --- the layers -------------------------------------------------------------

enum Family : int { Drone, Pad, Choir, Glass, Air };
constexpr int kFamilies = 5;

// A style is the character a scene gives its layers, and every layer laid by
// hand after it: classic (the PADsynth bed as it always was), Neon (a string
// machine and an eighties poly through a shared ensemble chorus: synthwave),
// Dub (offbeat chord stabs into a long tape echo with hiss: dub techno) and
// Tape (a warm felt-keys pad and every layer through a worn cassette: lo-fi
// hip-hop, Boards of Canada). After them, five from further away, each with
// a pad that is an instrument played rather than a tone held: Gamelan (a
// Balinese gong kebyar's bronze metallophones in pairs tuned apart, so every
// note shimmers, on a five-note pelog of the key, struck in interlocking
// figures that anticipate the core melody, over a kempli's beat, the
// smaller gongs through the cycle and a great gong closing every four
// bars), Raga (a tanpura's four strings fixed on the key's Sa, plucked
// round and round, their buzz sweeping up the harmonics as they die,
// sympathetic strings ringing to the chords), Mbira (Shona thumb piano:
// bass tines under the left thumb, high ones under the right, in a lilting
// twelve-eight round a harmonic cycle of its own, bottle caps rattling,
// inside a gourd), City pop (a DX electric piano on rootless sevenths,
// ninths and elevenths, through a stereo tremolo and a chorus, comping over
// a fingered bass, a cocktail shaker and a rim: Tokyo, 1982) and Khoomei
// (Tuvan throat singing: one voice holding the tonic, its overtones sung
// one at a time as a breathing whistle).
// A layer's style travels in its On, so a loop replays it as it was played.
enum Style : int { Classic = 0, Neon, Dub, Tape, Gamelan, Raga, Mbira, CityPop, Khoomei, kStyles };
// The styles whose pad is struck or plucked note by note, from the pool of
// strikes or a tanpura's strings, rather than read from a table.
inline bool struckPad(int style) { return style == Gamelan || style == Raga || style == Mbira || style == CityPop; }

struct FamilyShape {
    const char *name;
    const char *colours[3]; // the colour axis: left, middle, right of the column
    float low, high;        // the voicing window's bottom (MIDI) at the pad's bottom and top
    float span;             // how far the window reaches above its bottom, in semitones
    float release;          // seconds to about −40 dB after a lift
    float level;
};

// The registers keep out of the bass's way: nothing below G2, the drone a
// warm floor from there, the glass an octave and more above the pad.
const FamilyShape kShape[kFamilies] = {
    {"Drone", {"soft", "warm", "bowed"}, 43, 60, 20, 4.0f, 0.15f},
    {"Pad", {"strings", "saw", "hollow"}, 48, 72, 17, 3.2f, 0.15f},
    {"Choir", {"oo", "ah", "ee"}, 52, 74, 19, 3.6f, 0.15f},
    {"Glass", {"pure", "twinkle", "stars"}, 67, 88, 14, 5.0f, 0.12f},
    {"Air", {"wind", "rain", "vinyl"}, 62, 84, 12, 3.0f, 0.12f},
};

// Columns, and the colour inside one.
inline int familyAt(float x) { return std::clamp(int(x * kFamilies), 0, kFamilies - 1); }
inline float colourAt(int family, float x) { return clampf((x * kFamilies - float(family) - 0.08f) / 0.84f, 0.0f, 1.0f); }
inline float xOf(int family, float colour) { return (float(family) + 0.08f + 0.84f * colour) / kFamilies; }
// A style's pad has colours of its own: Neon's string machine to its poly,
// Dub's long wash to its short stab.
// The new styles' pads: Gamelan's figures from the slow core melody to
// sixteenths interlocking, the tanpura soft to its bridge buzzing bright,
// the mbira's tines from soft to rattling, the piano from a mellow tine to
// a bell, and the throat singer's three ways of singing: khoomei, soft and
// open; sygyt, the whistle; kargyraa, an octave down and growling.
const char *const kStyleName[kStyles] = {"Classic", "Neon", "Dub", "Tape", "Gamelan", "Raga", "Mbira", "City pop", "Khoomei"};
const char *const kStylePad[kStyles][3] = {
    {"strings", "saw", "hollow"}, {"strings", "ensemble", "poly"}, {"wash", "chord", "stab"}, {"felt", "keys", "bright"},
    {"pokok", "norot", "kotekan"}, {"soft", "jawari", "bright"}, {"soft", "tines", "buzz"}, {"tine", "suitcase", "bell"},
    {"khoomei", "sygyt", "kargyraa"}};
inline const char *colourAxis(int family, int style, int k) {
    return family == Pad ? kStylePad[std::clamp(style, 0, kStyles - 1)][k] : kShape[family].colours[k];
}
inline const char *colourName(int family, float c, int style = Classic) {
    if (family == Choir) {
        static const char *vowels[5] = {"oo", "oh", "ah", "eh", "ee"};
        return vowels[std::clamp(int(std::lround(c * 4.0f)), 0, 4)];
    }
    return colourAxis(family, style, c < 0.3f ? 0 : c < 0.7f ? 1 : 2);
}

// A firm landing starts at once, a light one fades in.
inline float attackFor(float pressure) { return 0.03f + 1.8f * std::pow(clampf(1.0f - pressure, 0.0f, 1.0f), 2.2f); }
inline float widthOf(float size) { return clampf(0.75f + 0.5f * (size - 1.0f), 0.35f, 1.0f); }

// --- PADsynth ---------------------------------------------------------------

// Appended only: the tables are built in this order, and nothing stores a
// timbre, but the scenes' styles name them.
enum Timbre : int {
    DroneSoft,
    DroneBowed,
    PadStrings,
    PadSaw,
    PadHollow,
    ChoirTone,
    GlassTone,
    WindTone,
    PadMachine, // Neon: a divide-down string machine
    PadPoly,    // Neon: an eighties polysynth's oscillator, read twice for its pulse
    PadDub,     // Dub: the chord a dub-techno stab is made of
    PadTape,    // Tape: warm felt keys, a little out of tune with themselves
    PadThroat,  // Khoomei: one pressed voice, its harmonics all there for the whistle to pick
    kTimbres
};

constexpr int kTableBits = 18; // 5.5 s: long enough that no repetition is heard
constexpr int kTableLen = 1 << kTableBits;
constexpr int kFracBits = 32 - kTableBits;
constexpr float kFracScale = 1.0f / float(1u << kFracBits);
constexpr float kTableRate = 48000.0f;
constexpr int kOctaves = 7; // tables centred on C2 (MIDI 36) up to C8

// Iterative radix-2 FFT, in place. Only used while the tables are built.
void fft(std::vector<std::complex<double>> &a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / double(len) * (inverse ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct TimbreDef {
    int firstOctave, lastOctave; // which tables exist
    float bwCents, bwScale;      // the first harmonic's bandwidth, and how it grows with harmonic number
};

const TimbreDef kTimbreDef[kTimbres] = {
    {1, 3, 2.0f, 1.5f},   // DroneSoft: a steady fundamental, the overtones slowly beating
    {1, 3, 4.0f, 1.3f},   // DroneBowed
    {1, 4, 22.0f, 1.0f},  // PadStrings: an ensemble
    {1, 4, 34.0f, 0.9f},  // PadSaw: a wide supersaw
    {1, 4, 14.0f, 1.0f},  // PadHollow
    {1, 4, 60.0f, 1.0f},  // ChoirTone: many voices, never quite together
    {3, 6, 3.0f, 0.6f},   // GlassTone: pure, glassy
    {2, 5, 140.0f, 0.7f}, // WindTone: so wide it is breath
    // A string machine's notes are all divided down from one master clock,
    // so each is a perfectly steady tone: almost no bandwidth, and the
    // ensemble (the Neon bus's chorus) is what makes it lush.
    {1, 4, 1.2f, 1.0f},   // PadMachine
    // PadPoly: a DCO's slight drift, the chorus does the rest; narrow, so the
    // table is near enough periodic for its pulse (see renderVoice).
    {1, 4, 2.0f, 1.0f},
    {1, 4, 3.0f, 1.0f},   // PadDub: a stable chord, for the echo to smear
    {1, 4, 9.0f, 0.8f},   // PadTape: a few cents of spread, an old instrument's
    // PadThroat: a single voice, so its harmonics are lines (a cent of
    // jitter) narrow enough that a resonance can pick out one of them alone.
    // (Kargyraa is the same voice with its false folds under it, so it
    // needs no table of its own an octave down.)
    {1, 3, 1.0f, 1.0f},
};

// Harmonic h's amplitude, at hz.
float harmonicAmp(int timbre, int h, float hz) {
    const float fh = float(h);
    switch (timbre) {
    case DroneSoft:
        return h > 14 ? 0.0f : 1.0f / std::pow(fh, 2.2f);
    case DroneBowed:
        return std::pow(fh, -1.2f) * (1.0f + 1.4f * std::exp(-std::pow((hz - 1100.0f) / 650.0f, 2.0f))) *
               std::exp(-hz / 5200.0f);
    case PadStrings:
        return std::exp(-hz / 4200.0f) / fh;
    case PadSaw:
        return std::exp(-hz / 12000.0f) / fh;
    case PadHollow:
        return ((h & 1) ? 1.0f : 0.1f) / fh * std::exp(-hz / 6500.0f);
    case ChoirTone:
        return std::pow(fh, -0.5f) * std::exp(-hz / 9000.0f);
    case GlassTone: {
        static const float partial[17] = {0, 1.0f, 0.5f, 0.16f, 0.3f, 0.04f, 0.1f, 0, 0.12f, 0, 0.03f, 0, 0.04f, 0, 0, 0, 0.03f};
        return h < 17 ? partial[h] : 0.0f;
    }
    case WindTone:
        return std::pow(fh, -0.5f) * std::exp(-hz / 7000.0f);
    case PadMachine: {
        // A string machine: the 8' and 4' saws together (every even
        // harmonic lifted by the octave above), thin at the bottom and bright
        // through a broad body around 2.5 kHz, which is what makes it read
        // as strings rather than as a synth.
        const float saw = 1.0f / fh + ((h & 1) ? 0.0f : 0.8f / (0.5f * fh));
        const float r = hz / 450.0f, thin = r * r / (1.0f + r * r);
        return saw * thin * std::exp(-hz / 20000.0f) * (0.7f + 0.8f * std::exp(-std::pow((hz - 2500.0f) / 1600.0f, 2.0f)));
    }
    case PadPoly:
        // A plain, full sawtooth, darker than the machine: the voice reads it
        // twice, a swinging fraction of a period apart, and the difference
        // of the two saws is a pulse whose width moves (a Juno's PWM).
        return std::exp(-hz / 7000.0f) / fh;
    case PadTape:
        // Felt keys held down: a strong fundamental, the next few harmonics
        // soft and the top dulled early, warm rather than bright.
        return (h == 1 ? 1.0f : 0.6f * std::pow(fh, -1.8f)) * std::exp(-hz / 3500.0f);
    case PadThroat:
        // A pressed voice, the folds closing hard: its harmonics fall slowly
        // (about 7 dB an octave), so there is still something at the
        // twelfth harmonic and beyond for the whistle to stand on.
        return std::pow(fh, -1.15f) * std::exp(-hz / 9000.0f);
    case PadDub:
        // Square-ish: strong odd harmonics, weak even ones, rolled off, so a
        // resonant filter has something woody to ring on.
        return ((h & 1) ? 1.0f : 0.3f) / fh * std::exp(-hz / 5000.0f);
    }
    return 0;
}

struct Bank {
    std::vector<float> table[kTimbres][kOctaves]; // kTableLen + 1 samples, the last a copy of the first
    static float baseMidi(int octave) { return 36.0f + 12.0f * float(octave); }

    void build() {
        Rng rng(0xA7305u);
        std::vector<double> amp(kTableLen / 2);
        std::vector<std::complex<double>> spec(kTableLen);
        const double binHz = kTableRate / kTableLen;
        for (int t = 0; t < kTimbres; ++t) {
            const TimbreDef &def = kTimbreDef[t];
            for (int o = def.firstOctave; o <= def.lastOctave; ++o) {
                const double f0 = midiToHz(baseMidi(o));
                // Read at up to 1.6 times the base speed without folding over.
                const double maxHz = std::min(15000.0, 0.46 * kTableRate / 1.6);
                std::fill(amp.begin(), amp.end(), 0.0);
                for (int h = 1;; ++h) {
                    const double fh = f0 * h;
                    if (fh > maxHz)
                        break;
                    const float a = harmonicAmp(t, h, float(fh));
                    if (a < 1e-5f)
                        continue;
                    const double bwHz = (std::exp2(def.bwCents / 1200.0) - 1.0) * f0 * std::pow(double(h), def.bwScale);
                    const double sigma = std::max(0.6, bwHz / binHz * 0.5);
                    const double centre = fh / binHz;
                    const int from = std::max(1, int(centre - 4 * sigma));
                    const int to = std::min(kTableLen / 2 - 1, int(centre + 4 * sigma) + 1);
                    // Energy per harmonic stays a², however wide it is spread.
                    const double norm = double(a) / std::sqrt(sigma);
                    for (int i = from; i <= to; ++i) {
                        const double x = (i - centre) / sigma;
                        amp[size_t(i)] += norm * std::exp(-0.5 * x * x);
                    }
                }
                spec.assign(kTableLen, 0.0);
                for (int i = 1; i < kTableLen / 2; ++i) {
                    const double ph = 2 * M_PI * rng.unit();
                    spec[size_t(i)] = std::polar(amp[size_t(i)], ph);
                    spec[size_t(kTableLen - i)] = std::conj(spec[size_t(i)]);
                }
                fft(spec, true);
                double sq = 0;
                for (const auto &v : spec)
                    sq += v.real() * v.real();
                const double gain = 0.25 / std::sqrt(std::max(1e-20, sq / kTableLen));
                auto &tab = table[t][o];
                tab.resize(kTableLen + 1);
                for (int i = 0; i < kTableLen; ++i)
                    tab[size_t(i)] = float(spec[size_t(i)].real() * gain);
                tab[kTableLen] = tab[0];
            }
        }
    }
};

// Built once, the first time an engine starts, and shared.
const Bank &bank() {
    static const Bank b = [] {
        Bank k;
        k.build();
        return k;
    }();
    return b;
}

// The table octave for a pitch: nearest centre, within what the timbre has.
inline int octaveFor(int timbre, float midi) {
    const int o = int(std::lround((midi - 36.0f) / 12.0f));
    return std::clamp(o, kTimbreDef[timbre].firstOctave, kTimbreDef[timbre].lastOctave);
}

// The two timbres a family morphs between at a colour, and their weights
// (equal power: the tables are uncorrelated). A style's pad has timbres of
// its own; its other layers keep the classic ones and take on the style
// through its bus.
void timbres(int family, float c, int style, int &a, int &b, float &wa, float &wb) {
    if (family == Pad && style == Neon) {
        a = PadMachine, b = PadPoly;
        const float u = smoothstep(0.15f, 0.85f, c);
        wa = std::cos(u * 0.5f * kPi), wb = std::sin(u * 0.5f * kPi);
        return;
    }
    if (family == Pad && style == Dub) {
        a = b = PadDub, wa = 1, wb = 0;
        return;
    }
    if (family == Pad && style == Tape) {
        a = b = PadTape, wa = 1, wb = 0;
        return;
    }
    switch (family) {
    case Drone:
        a = DroneSoft, b = DroneBowed;
        wa = std::cos(c * 0.5f * kPi), wb = std::sin(c * 0.5f * kPi);
        return;
    case Pad:
        if (c < 0.5f) {
            a = PadStrings, b = PadSaw;
            wa = std::cos(c * kPi), wb = std::sin(c * kPi);
        } else {
            a = PadSaw, b = PadHollow;
            wa = std::cos((c - 0.5f) * kPi), wb = std::sin((c - 0.5f) * kPi);
        }
        return;
    case Choir:
        a = b = ChoirTone, wa = 1, wb = 0;
        return;
    case Glass:
        a = b = GlassTone, wa = 1, wb = 0;
        return;
    default:
        a = b = WindTone, wa = 1, wb = 0;
        return;
    }
}

// --- the chord --------------------------------------------------------------

// A few scale degrees in ascending order (an insertion sort: four at most).
inline void sortTones(int *t, int n) {
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && t[j - 1] > t[j]; --j)
            std::swap(t[j - 1], t[j]);
}

// The chord the bed plays, as scale degrees in ascending order.
struct Bed {
    int root = 0, count = 3;
    int tones[4] = {0, 2, 4, 6};
    bool fifth = true; // tones[] holds a perfect fifth over the root
    int fifthDegree = 4;
};

// The bed's own chord on a degree: the third and fifth above it in the
// scale, except where that would be tense: a diminished or augmented fifth
// becomes the sixth (vii° in a major key is then a first-inversion V, the
// augmented III of harmonic minor an inverted i), and a scale with no third
// (pentatonic) leans on a second or fourth instead, as a sus chord.
Bed ownChord(const Tuning &t, int degree) {
    Bed b;
    b.root = degree;
    const int per = t.degreesPerOctave();
    const double r = t.degreeSemis(degree);
    auto find = [&](std::initializer_list<double> wants) {
        for (double want : wants)
            for (int d = 1; d < per; ++d)
                if (std::fabs(t.degreeSemis(degree + d) - r - want) < 0.35)
                    return degree + d;
        return -1;
    };
    const int third = find({4, 3, 5, 2});
    const int fifth = find({7, 9, 8});
    b.count = 0;
    b.tones[b.count++] = degree;
    if (third >= 0)
        b.tones[b.count++] = third;
    if (fifth >= 0 && fifth != third)
        b.tones[b.count++] = fifth;
    sortTones(b.tones, b.count);
    return b;
}

Bed resolveBed(const Harmony &h, int own) {
    Bed b;
    if (own >= 0) {
        b = ownChord(h.tuning, own);
    } else {
        b.root = h.chordRoot;
        b.count = std::clamp(h.chordCount, 1, 4);
        for (int i = 0; i < b.count; ++i)
            b.tones[i] = h.chordTones[i];
        sortTones(b.tones, b.count);
    }
    b.fifth = false;
    const double r = h.tuning.degreeSemis(b.root);
    for (int i = 0; i < b.count; ++i) {
        double iv = h.tuning.degreeSemis(b.tones[i]) - r;
        iv -= 12.0 * std::floor(iv / 12.0);
        if (std::fabs(iv - 7.0) < 0.3) {
            b.fifth = true;
            b.fifthDegree = b.tones[i];
        }
    }
    return b;
}

// Pitches of the bed's tones between lo and hi. The drone only takes the
// root and a perfect fifth. Extended, the chord takes its seventh and ninth
// too, the scale's own (in a minor key i is a minor ninth, VI a major
// ninth): the chord a dub-techno stab is.
int bedPitches(const Bed &b, const Harmony &h, bool drone, float lo, float hi, float *out, int max, bool extended = false) {
    const int per = h.tuning.degreesPerOctave();
    int tones[6], count = 0;
    if (drone) {
        tones[count++] = b.root;
        if (b.fifth)
            tones[count++] = b.fifthDegree;
    } else {
        for (int i = 0; i < b.count; ++i)
            tones[count++] = b.tones[i];
        if (extended)
            for (int add : {b.root + per - 1, b.root + per + 1}) {
                bool has = false;
                for (int i = 0; i < count; ++i)
                    has |= ((tones[i] - add) % per + per) % per == 0;
                if (!has)
                    tones[count++] = add;
            }
    }
    int n = 0;
    for (int k = -3; k <= 9 && n < max; ++k)
        for (int i = 0; i < count && n < max; ++i) {
            const float m = float(h.midi(tones[i] + per * k, 0));
            if (m >= lo && m <= hi)
                out[n++] = m;
        }
    return n;
}

// --- the engine ---------------------------------------------------------------

enum ParamId : int { PFreeze = 0, PCloud, PPump, PSpace, PSwell, PChord, PChordToLoop, PTone };
// The page marks the latched layers a loop is about to take, so the loop's
// first round can take their voices over (never recorded).
constexpr uint16_t kAdoptable = Command::User + 1;
// The page says a loop stopped: the layers it was carrying over its seam
// let go (never recorded; i[0] the slot).
constexpr uint16_t kSeamRelease = Command::User + 2;
enum PumpMode : int { PumpOff = 0, PumpKick, PumpBeat };

// Published numbers, read by the page.
enum Pub : int {
    PubFreeze = 0,
    PubCloud,
    PubPump,
    PubSpace,
    PubSwell,
    PubOwn,
    PubRoot,
    PubCount,
    PubTones, // 8..11
    PubHalo = 12,
    PubPumpEnv,
    PubVoices,
    PubLevel,
    PubOwnLoop,     // the own chord came from a loop
    PubOwnHand,     // the player's chord key holds it, whatever a loop says
    PubFrozen,      // voices the freeze is holding
    PubTone,        // 0 dark .. 0.5 as built .. 1 bright
    PubVoice0 = 20,  // 9 per voice
    PubGlint0 = 200, // 3 per glint
};
constexpr int kPubStride = 9;
constexpr int kPubVoices = 20, kPubGlints = 16;

constexpr int kVoices = 20;
constexpr int kSlots = 12;
constexpr int kGlints = 32;

// --- struck and plucked pads ---------------------------------------------------

// A sine of x turns, from a seventh-order polynomial on the quarter turn:
// within 2e-4 of the real thing, so its own harmonics sit 75 dB down, and
// cheap enough for an FM piano's two oscillators a sample.
inline float sinTurns(float x) {
    x -= std::floor(x + 0.5f);
    if (x > 0.25f)
        x = 0.5f - x;
    else if (x < -0.25f)
        x = -0.5f - x;
    const float t = kTwoPi * x, t2 = t * t;
    return t * (1.0f + t2 * (-1.0f / 6.0f + t2 * (1.0f / 120.0f - t2 * (1.0f / 5040.0f))));
}

// One struck note of a Gamelan, Mbira or City pop pad: a few decaying
// partials (a bronze bar's modes, a tine's), each a rotating phasor at four
// multiplies a sample. A partial's amplitude may be negative: one at the
// same frequency as another, with a shorter life, takes the start of it
// away, so the note blooms rather than starts (a great gong). All of them
// can be bent together, falling back to pitch (the gong's sag). For the
// piano, a DX electric piano instead: two detuned pairs of operators at
// 1:1, the body of the note, and a third carrier whose modulator runs
// fourteen times faster and dies in a twelfth of a second, the tine's
// metallic bark. For the mbira a rattle: the bottle caps (or shells) on its
// soundboard, loose metal struck when the tine swings up past the point
// where they lie on it, so they chatter at the tine's own rate while it
// swings wide and fall silent as it narrows. Each hit is a scrape of
// noise (metal on metal, a fraction of a millisecond) heard through six
// broad, quickly damped resonances, the caps, tuned afresh for every
// strike: no two notes rattle alike, and nothing in it stands still long
// enough to be heard as a pitch. For
// the ceng-ceng a clash and a ring of bright hiss, choked by the player's
// hands. The pool is the engine's, shared by every voice; a
// strike sounds for as long as its voice's envelope lets it, and a voice's
// next strike from the same hand damps its last, as a gangsa player's hand
// does.
struct Strike {
    static constexpr int P = 7;
    bool on = false;
    int owner = -1, ownerId = 0, style = Classic, delay = 0, hand = -1;
    int np = 0;
    float c[P] = {}, s[P] = {}, cw[P] = {}, sw[P] = {}, amp[P] = {}, dec[P] = {}, w[P] = {};
    float bend = 0, bendDec = 1; // how far above pitch, as a fraction, and how fast it falls back
    // The DX piano: the pairs' phases and steps (in turns), their index
    // and what it settles to, the bark's index and level.
    bool fm = false;
    float phA = 0, phB = 0, ph = 0, phM = 0, incA = 0, incB = 0, inc = 0, incM = 0;
    float index = 0, indexDec = 1, indexFloor = 0, bark = 0, barkDec = 1, barkAmp = 0, barkAmpDec = 1;
    float barkLp = 1, bz1 = 0, bz2 = 0; // the bark's two one-pole low-passes, and their coefficient
    float fmAmp = 0, fmDec = 1;
    // The rattle: how hard the caps are struck, how far up the tine's swing
    // they lie (on its fundamental's scale) and where the swing was a
    // sample ago, the scrape of the last hit and how fast it goes, and the
    // caps.
    static constexpr int kCaps = 6;
    float buzz = 0, capAt = 0, capWas = 0, scrape = 0, scrapeDec = 1;
    Svf cap[kCaps];
    // The ceng-ceng: the clash (a millisecond of it), the ring after it, its
    // two bands, and the samples left until the hands choke it.
    float clash = 0, clashDec = 1, hiss = 0, hissDec = 1;
    Svf hissF, hissF2;
    int choke = -1;
    float att = 0, attInc = 1, damp = 1, gain = 0, gl = 0.7f, gr = 0.7f;
    // A gong's: heard through the gong's own high-pass (low), and how far
    // it is held down (an angsel's hand on it): its gain falls from 1 to
    // duckFloor at duckDec a sample, and stays there.
    bool low = false;
    float duck = 1, duckFloor = 1, duckDec = 1;
    float x = 0, y = 0, shown = 0; // where it shows on the page, and how bright
    // Adds partial ratio × hz, amplitude a, an e-fold time of tau seconds.
    void partial(float hz, float a, float tau, float rate) {
        if (np >= P || hz >= 0.42f * rate || a == 0)
            return;
        w[np] = kTwoPi * hz / rate;
        cw[np] = std::cos(w[np]), sw[np] = std::sin(w[np]);
        c[np] = 1, s[np] = 0;
        amp[np] = a;
        dec[np] = std::exp(-1.0f / (tau * rate));
        ++np;
    }
};

// One of a tanpura's four strings: a Karplus-Strong loop (a delay of one
// period, fed back through a one-pole low-pass whose phase delay is taken
// out of the period, so the string is in tune to a cent), plucked by
// filling the next period with a soft, smooth pluck. Its jawari, the broad
// curved bridge, is what makes a tanpura a tanpura: the string lies on it,
// and on one side of its swing it presses against the curve, which gives
// it nowhere to go. In the loop that is a contact: past the bridge's height
// the string's displacement is pushed back (softly, over a little knee),
// and only one way, never the other. It only ever takes energy away, so
// the loop stays stable, but it takes it from the fundamental's peaks and
// gives it to the overtones, every period. Because the bridge is curved,
// where the string lies on it depends on how wide it swings: a fresh
// pluck wraps a long stretch of the curve for a long part of each period,
// and as the swing narrows it only grazes it, for a shorter and shorter
// moment. The bridge's height follows the string's swing (its spread about
// its own middle, over a few periods), set so the string is on the curve
// for the share of the period the swing calls for. What the bridge gives
// back each time the string lies on it is a pulse as long as the contact:
// a pulse a seventh of a period long carries the harmonics up to about the
// seventh, one a twentieth long up to the twentieth. So the buzz, that
// pulse heard through a band around the harmonics it carries, climbs as
// the string dies away, from about the fifth harmonic to past the
// twentieth: the tanpura's sweep, the thing that makes it sound alive.
// Toward the jawari end of the column it pushes back harder and its buzz
// is louder (the cotton thread under the string, moved).
struct TanString {
    float *buf = nullptr;
    uint32_t mask = 0, w = 0;
    float period = 100, readAt = 99, loss = 0.999f, b = 0.8f, z = 0, midi = 0;
    // Where the delay read is going (a retuned string slides there, over
    // about 20 ms, while its new pluck goes in), how fast, and how far the
    // jawari shortens it now (samples) and at most.
    float readTo = 99, readGlide = 0, shorten = 0, shortMax = 0;
    float since = 10;   // seconds since the pluck
    int exciteLeft = 0; // samples of the pluck still to go in
    float exAmp = 0, exLen = 1;
    Svf exF; // the pluck's low-pass
    float level = 0; // a slow follower of what it sounds, for the page
    float bridge = 1, jaw = 0; // where the string meets the jawari, and how hard it pushes back
    // The swing (the string's spread about its middle, followed over a few
    // periods: the middle, the mean square, how fast), the widest it has
    // been since the pluck, how long the contact is when it is widest (a
    // share of the period) and how long now, and how loud its buzz is.
    float mid = 0, spread = 0, follow = 0.01f, swing = 0, peak = 0, contact = 0.13f, share = 0.13f, buzz = 0;
    Svf buzzF, buzzLp, buzzLp2; // the band the contact's pulse is heard through, round the harmonics it carries, and its top dulled
    Svf dc;    // under the string's fundamental: what the one-sided bridge leaves there, out
    bool stilled = false;      // let go (its note left behind by a new key), ringing out until its pluck retunes it
    void clear() {
        if (buf)
            std::fill(buf, buf + mask + 1, 0.0f);
        z = 0;
        mid = spread = swing = peak = 0;
        buzzF.reset();
        buzzLp.reset();
        buzzLp2.reset();
        dc.reset();
    }
};

// Raga's bus: the sympathetic strings a sitar or a sarangi carries under
// its played ones. Six strings tuned to the chord's tones above middle C,
// never plucked, each a comb (a delay of one period fed back through a
// two-point average) that rings whenever anything in the style sounds near
// its pitch, and goes on ringing a moment after. What comes out is scaled
// by the loop's loss, so a partial that matches a string exactly comes back
// at about its own level (a little over) and the ringing on after it is the
// halo. On a
// chord change a string whose note leaves is damped in 40 ms, retuned and
// let ring again.
class Taraf {
public:
    static constexpr int K = 6;
    void init(float rate) {
        m_rate = rate;
        for (Str &s : m_s)
            s.line.init(size_t(rate / midiToHz(57.0)) + 8);
        m_hp.set(260.0f, 0.7f, rate);
        m_step = 1.0f / (0.04f * rate);
    }
    // The notes the strings should ring at (MIDI), at most K.
    void tune(const float *midi, int count) {
        for (int i = 0; i < K; ++i) {
            const float m = i < count ? midi[i] : 0.0f;
            if (std::fabs(m - m_s[i].want) > 0.01f)
                m_s[i].want = m;
        }
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, float t60) {
        for (int k = 0; k < K; ++k) {
            Str &s = m_s[k];
            // Retuning: down to nothing, then the new note.
            if (s.want != s.midi && s.fade <= 0) {
                s.line.clear();
                s.y1 = 0;
                s.midi = s.want;
                if (s.midi > 0) {
                    const float hz = midiToHz(s.midi);
                    s.period = m_rate / hz;
                    s.g = std::pow(10.0f, -3.0f / (t60 * hz));
                }
            }
        }
        for (int i = 0; i < n; ++i) {
            const float x = m_hp.step(0.5f * (inL[i] + inR[i])).high;
            float l = 0, r = 0;
            for (int k = 0; k < K; ++k) {
                Str &s = m_s[k];
                const bool going = s.want != s.midi;
                s.fade = clampf(s.fade + (going ? -m_step : m_step), 0.0f, 1.0f);
                if (s.midi <= 0)
                    continue;
                const float y = s.line.read(s.period - 0.5f);
                const float fb = s.g * 0.5f * (y + s.y1);
                s.y1 = y;
                s.line.push(x * s.fade + fb);
                const float o = y * (1.0f - s.g) * 1.2f * s.fade;
                if (k & 1)
                    r += o;
                else
                    l += o;
            }
            outL[i] += l + 0.3f * r;
            outR[i] += r + 0.3f * l;
        }
    }
    void clear() {
        for (Str &s : m_s) {
            s.line.clear();
            s.y1 = 0;
        }
        m_hp.reset();
    }

private:
    struct Str {
        Delay line;
        float midi = 0, want = 0, period = 100, g = 0.99f, y1 = 0, fade = 0;
    };
    float m_rate = 48000, m_step = 0.001f;
    Str m_s[K];
    Svf m_hp;
};

// Mbira's bus: the deze, the big calabash the instrument is played inside,
// which gives the tines a hollow body (two broad resonances, its air and
// its shell) and throws them a little wider.
class Gourd {
public:
    void init(float rate) {
        for (Svf *f : {&m_airL, &m_airR})
            f->set(190.0f, 2.2f, rate);
        for (Svf *f : {&m_shellL, &m_shellR})
            f->set(620.0f, 1.6f, rate);
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        for (int i = 0; i < n; ++i) {
            const float l = inL[i], r = inR[i];
            outL[i] += 0.8f * m_airL.k * m_airL.step(r).band + 0.3f * m_shellL.k * m_shellL.step(l).band;
            outR[i] += 0.8f * m_airR.k * m_airR.step(l).band + 0.3f * m_shellR.k * m_shellR.step(r).band;
        }
    }
    void clear() {
        for (Svf *f : {&m_airL, &m_airR, &m_shellL, &m_shellR})
            f->reset();
    }

private:
    Svf m_airL, m_airR, m_shellL, m_shellR;
};

// Khoomei's bus: the valley the singer faces. Throat singing is sung
// outdoors, to the hills and the river, and what comes back is one far
// echo off the other side, dark with the distance, and its own faint echo:
// a slap at about two fifths of a second on one side and half a second on
// the other, each side fed the other's return, and that only.
class Valley {
public:
    void init(float rate) {
        m_rate = rate;
        m_dL.init(size_t(0.6f * rate));
        m_dR.init(size_t(0.6f * rate));
        for (Svf *f : {&m_lpL, &m_lpR})
            f->set(2200.0f, 0.6f, rate);
        for (Svf *f : {&m_hpL, &m_hpR})
            f->set(180.0f, 0.6f, rate);
    }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        const float tL = 0.41f * m_rate, tR = 0.53f * m_rate;
        for (int i = 0; i < n; ++i) {
            const float eL = m_lpL.step(m_hpL.step(m_dL.read(tL)).high).low;
            const float eR = m_lpR.step(m_hpR.step(m_dR.read(tR)).high).low;
            m_dL.push(inL[i] + 0.3f * eR);
            m_dR.push(inR[i] + 0.3f * eL);
            outL[i] += 0.55f * eL;
            outR[i] += 0.55f * eR;
        }
    }
    void clear() {
        m_dL.clear();
        m_dR.clear();
        for (Svf *f : {&m_lpL, &m_lpR, &m_hpL, &m_hpR})
            f->reset();
    }

private:
    float m_rate = 48000;
    Delay m_dL, m_dR;
    Svf m_lpL, m_lpR, m_hpL, m_hpR;
};

struct Slot {
    bool on = false, seen = false;
    float midi = 0, gain = 0, target = 0;
    int octave = 0;
    uint32_t phase = 0, inc = 0;
};

struct Voice {
    bool active = false, releasing = false;
    bool frozen = false, holdEnv = false, pendingOff = false, adoptable = false, seamHold = false;
    float agcRms = 0.2f, agc = 1.0f;
    float lfo3 = 0, rate3 = 0.02f;
    int id = 0, family = 0, style = Classic;
    // A Dub pad's stab: its envelope, rising or falling, and the sixteenth
    // it last looked at.
    float stab = 0;
    bool stabRising = false;
    int64_t stabStep = INT64_MIN;
    uint32_t chordSeen = 0; // a Tape pad's: the chord its keys last struck
    float hammer = 0;       // and the brightness of its last strike, quicker to go
    float tgt[5] = {}, cur[5] = {}; // height, colour, pressure, motion, width
    float attack = 0.3f, ramp = 0, env = 0, relCoef = 0.9999f;
    float norm = 1, level = 0, gate = 1, gateSmooth = 1;
    float lfo1 = 0, lfo2 = 0, rate1 = 0.07f, rate2 = 0.13f, side = 1;
    float pwm = 0; // a Neon poly's pulse-width LFO
    float dropClock = 0;
    // A struck pad's (Gamelan, Raga, Mbira, City pop): its envelope where the
    // block began (the strikes ramp from it), the step of its figure it last
    // played, the last strike of each hand (the next one damps it), where its
    // tanpura's round began, and a piano's tremolo.
    float envPrev = 0;
    bool hit = false;
    int64_t figStep = INT64_MIN, cycleFrom = 0;
    int lastStrike[4] = {-1, -1, -1, -1};
    // A City pop pad's bass player's ear: the roots it has heard in order,
    // how many, the sixteenth of the last change and how long the one
    // before took, whether the next root is to be slid into, and the root
    // its last approach aimed at.
    int bassRoots[8] = {};
    int bassN = 0, bassPeriod = 0, bassAimed = -999;
    int64_t bassLast = INT64_MIN;
    bool bassSlide = false;
    // A Gamelan pad's angsel: armed, and the sixteenth its break began.
    bool angselArmed = true;
    int64_t angselFrom = INT64_MIN;
    float trem = 0, tremDepth = 0;
    TanString str[4];
    // A Khoomei pad's singer: the voice's phase in its table and its pitch
    // (MIDI, gliding), the overtone the whistle sits on (a harmonic number,
    // gliding) and the one it is going to, the step of the melody it last
    // sang, and how loud the whistle is now, for the page; the breath's
    // wavering (two slow phases and a wander), and the scoops still to
    // come up out of (the voice's, in semitones, the whistle's, in
    // overtones).
    uint32_t thPhase = 0;
    float thMidi = 0, whistle = 8, whistleTo = 8, whistleShown = 0, thSub = 0, thFlick = 0, thFlickLeft = 0;
    float thBreath = 0, thBreath2 = 0, thWander = 0, thScoop = 0, thWhistleScoop = 0;
    int64_t thStep = INT64_MIN;
    // Kargyraa's second formant (the tongue's, following the first along
    // the vowels) and the singer's high-pass, 24 dB an octave under 30 Hz.
    Svf thF2, thHp1, thHp2;
    Slot notes[kSlots];
    Svf lpL, lpR, fL[3], fR[3], bodyL, bodyR, rainL, rainR, crackle;
    OnePoleLp hissL, hissR;
    Rng rng;
};

// A short sine: a twinkle of glass, or a raindrop (a chirp up to its note).
struct Glint {
    bool on = false;
    int delay = 0;
    float c = 1, s = 0, hz = 0, chirp = 0, chirpCoef = 0;
    float amp = 0, decay = 0.999f, att = 0, attInc = 0;
    float gl = 0.7f, gr = 0.7f;
    float x = 0, y = 0; // where it shows on the page
};

inline void copyCoefficients(Svf &to, const Svf &from) {
    to.a1 = from.a1;
    to.a2 = from.a2;
    to.a3 = from.a3;
    to.k = from.k;
}

// The bed's own space: sixteen delay lines through a Hadamard matrix, lines
// chorused slowly, an octave-up shimmer fed back into the tail, and a
// freeze that turns the tail lossless and closes its input, so whatever was
// sounding hangs in the air for as long as it stays frozen.
class Halo {
public:
    static constexpr int L = 16;
    void init(float rate) {
        m_rate = rate;
        static const float ms[L] = {31.3f, 37.1f, 41.9f, 43.7f, 47.3f, 53.9f, 59.1f, 61.3f,
                                    67.9f, 71.9f, 79.3f, 83.3f, 89.9f, 97.7f, 103.1f, 113.9f};
        Rng rng(0x4A10u);
        for (int i = 0; i < L; ++i) {
            m_base[i] = ms[i] * 0.001f * rate * 1.35f;
            m_lines[i].init(size_t(m_base[i] + 0.01f * rate + 64));
            m_phase[i] = rng.unit();
            m_lfoRate[i] = 0.08f + 0.25f * rng.unit();
            m_damp[i].set(5200.0f, rate);
            m_low[i].set(140.0f, rate);
        }
        static const int diff[3] = {142, 379, 277};
        for (int i = 0; i < 3; ++i) {
            m_diffL[i].init(uint32_t(diff[i] * rate / 44100.0f), 0.65f);
            m_diffR[i].init(uint32_t(diff[i] * 1.13f * rate / 44100.0f), 0.65f);
        }
        m_shift.init(size_t(0.2f * rate));
        m_window = 0.075f * rate;
        m_shimLp.set(7000.0f, rate);
        m_shimHp.set(500.0f, 0.7f, rate);
        for (int i = 0; i < 256; ++i) {
            const float s = std::sin(kPi * float(i) / 255.0f);
            m_win[i] = s * s;
        }
        m_win[256] = 0;
    }
    // Once a block: decay, shimmer, freeze (0..1), and the chorus.
    void set(float t60, float shimmer, float freeze, int n) {
        m_shimmer = shimmer * (1.0f - freeze);
        for (int i = 0; i < L; ++i) {
            const float g = std::pow(10.0f, -3.0f * m_base[i] / (t60 * m_rate));
            m_gain[i] = lerpf(g, 1.0f, freeze);
            m_phase[i] += m_lfoRate[i] * float(n) / m_rate;
            if (m_phase[i] >= 1.0f)
                m_phase[i] -= 1.0f;
            m_mod[i] = (0.0006f * m_rate) * std::sin(kTwoPi * m_phase[i]) * (1.0f - 0.7f * freeze);
        }
        m_keep = lerpf(0.6f, 1.0f, freeze);
        m_lowLoss = lerpf(0.25f, 0.0f, freeze);
        m_in = 1.0f - freeze;
    }
    inline void process(float inL, float inR, float &outL, float &outR) {
        float l = inL * m_in, r = inR * m_in;
        if (m_shimmer > 0.001f) {
            m_shiftPhase += 1.0f / m_window;
            if (m_shiftPhase >= 1.0f)
                m_shiftPhase -= 1.0f;
            const float p1 = m_shiftPhase, p2 = p1 + 0.5f >= 1.0f ? p1 - 0.5f : p1 + 0.5f;
            const float up = m_shift.read((1.0f - p1) * m_window + 4.0f) * m_win[int(p1 * 255.0f)] +
                             m_shift.read((1.0f - p2) * m_window + 4.0f) * m_win[int(p2 * 255.0f)];
            const float sh = fastTanh(m_shimLp.step(m_shimHp.step(up).high) * m_shimmer);
            l += sh;
            r += sh;
        }
        for (int i = 0; i < 3; ++i) {
            l = m_diffL[i].step(l);
            r = m_diffR[i].step(r);
        }
        float x[L];
        for (int i = 0; i < L; ++i) {
            float v = m_lines[i].read(m_base[i] + m_mod[i]);
            const float lp = m_damp[i].step(v);
            v = lp + m_keep * (v - lp);
            v -= m_lowLoss * m_low[i].step(v);
            x[i] = v * m_gain[i];
        }
        for (int h = 1; h < L; h <<= 1)
            for (int i = 0; i < L; i += h << 1)
                for (int j = i; j < i + h; ++j) {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b;
                    x[j + h] = a - b;
                }
        constexpr float norm = 0.25f; // 1/√16: the matrix stays unitary
        float oL = 0, oR = 0;
        for (int i = 0; i < L; ++i) {
            float v = x[i] * norm;
            // A frozen tail is lossless; a soft ceiling keeps it from ever
            // running away on rounding.
            if (std::fabs(v) > 1.5f)
                v = 1.5f * fastTanh(v / 1.5f);
            m_lines[i].push(v + ((i & 1) ? r : l) * 0.35f);
            if (i & 1)
                oR += (i & 2) ? v : -v;
            else
                oL += (i & 2) ? v : -v;
        }
        outL = oL * 0.35f;
        outR = oR * 0.35f;
        m_shift.push(0.5f * (outL + outR));
    }
    void clear() {
        for (auto &d : m_lines)
            d.clear();
        m_shift.clear();
    }

private:
    float m_rate = 48000;
    Delay m_lines[L];
    float m_base[L] = {}, m_gain[L] = {}, m_phase[L] = {}, m_lfoRate[L] = {}, m_mod[L] = {};
    OnePoleLp m_damp[L], m_low[L];
    Allpass m_diffL[3], m_diffR[3];
    Delay m_shift;
    float m_shiftPhase = 0, m_window = 3600, m_shimmer = 0, m_keep = 0.6f, m_lowLoss = 0.25f, m_in = 1;
    float m_win[257] = {};
    OnePoleLp m_shimLp;
    Svf m_shimHp;
};

// A grain cloud of the bed itself: long soft grains from the last three
// seconds, at the same pitch or an octave either way, some reversed, spread
// wide. Octaves only, so the cloud is as consonant as what feeds it.
class Cloud {
public:
    static constexpr int G = 16;
    void init(float rate) {
        m_rate = rate;
        size_t n = 1;
        while (n < size_t(3.5f * rate))
            n <<= 1;
        m_buf.assign(n, 0.0f);
        m_mask = uint32_t(n - 1);
        for (int i = 0; i <= 512; ++i) {
            const float s = std::sin(kPi * float(i) / 512.0f);
            m_win[i] = s * s;
        }
        m_fadeStep = 1.0f / (0.04f * rate);
    }
    void set(float amount) { m_amount = amount; }
    bool busy() const { return m_amount > 0.001f || m_live > 0; }
    // The chord changed: grains of the old one fade within a few tens of
    // milliseconds, and new ones only read from after the change.
    void chordChanged() {
        m_fresh = 0;
        for (Grain &g : m_g)
            if (g.on)
                g.fade = std::min(g.fade, 1.0f);
    }
    // Feeds the cloud even when it is off, so it starts from what was just
    // played.
    inline void feed(float in) {
        m_buf[m_w] = in;
        m_w = (m_w + 1) & m_mask;
        m_fresh = std::min(m_fresh + 1.0f, 4.0f * m_rate);
    }
    inline void process(float &outL, float &outR) {
        outL = outR = 0;
        if (m_amount > 0.001f) {
            m_until -= 1.0f;
            if (m_until <= 0) {
                spawn();
                m_until = m_rate / (10.0f * m_amount) * (0.5f + m_rng.unit());
            }
        }
        if (!m_live)
            return;
        int live = 0;
        for (Grain &g : m_g) {
            if (!g.on)
                continue;
            ++live;
            const float pos = float(m_w) - g.back;
            const float fl = std::floor(pos);
            const uint32_t i0 = uint32_t(int32_t(fl)) & m_mask;
            const float fr = pos - fl;
            const float s0 = m_buf[i0], s1 = m_buf[(i0 + 1) & m_mask];
            float s = (s0 + fr * (s1 - s0)) * m_win[int(g.age * 512.0f)] * g.amp;
            if (g.fade < 2.0f) {
                g.fade -= m_fadeStep;
                s *= std::max(0.0f, g.fade);
                if (g.fade <= 0)
                    g.on = false;
            }
            outL += s * g.gl;
            outR += s * g.gr;
            g.back -= g.speed - 1.0f;
            g.age += g.inc;
            if (g.age >= 1.0f || g.back < 2.0f || g.back > float(m_mask) - 4.0f)
                g.on = false;
        }
        m_live = live;
        const float k = 1.0f / std::sqrt(1.0f + 0.4f * float(live));
        outL *= k;
        outR *= k;
    }

private:
    struct Grain {
        bool on = false;
        float back = 0, speed = 1, age = 0, inc = 0, amp = 0, gl = 0, gr = 0, fade = 3;
    };
    void spawn() {
        for (Grain &g : m_g) {
            if (g.on)
                continue;
            static const float speeds[6] = {1.0f, 2.0f, 1.0f, 2.0f, 0.5f, -1.0f};
            const float speed = speeds[m_rng.next() % 6u];
            const float len = (0.14f + 0.26f * m_rng.unit()) * m_rate;
            // Where it may start reading: far enough back for a fast grain to
            // stay in the past, near enough for a slow or reversed one to
            // stay after the last chord change.
            const float lo = 0.04f * m_rate + (speed > 1.0f ? len * (speed - 1.0f) : 0.0f);
            const float hi = std::min(m_rate * 3.2f, m_fresh - (speed < 1.0f ? len * (1.0f - speed) : 0.0f));
            if (hi < lo)
                return;
            g.back = lo + m_rng.unit() * std::min(hi - lo, 1.6f * m_rate);
            g.fade = 3;
            g.speed = speed;
            g.age = 0;
            g.inc = 1.0f / len;
            g.amp = (speed == 2.0f ? 0.6f : 0.85f) * (0.7f + 0.3f * m_rng.unit());
            const float pan = m_rng.bipolar() * 0.9f;
            g.gl = std::sqrt(0.5f * (1.0f - pan));
            g.gr = std::sqrt(0.5f * (1.0f + pan));
            g.on = true;
            ++m_live;
            return;
        }
    }
    float m_rate = 48000, m_amount = 0, m_until = 0, m_fresh = 0, m_fadeStep = 0.001f;
    std::vector<float> m_buf;
    uint32_t m_mask = 0, m_w = 0;
    int m_live = 0;
    Grain m_g[G];
    float m_win[513] = {};
    Rng m_rng{0xC10DDu};
};

// Neon's bus: the ensemble of a string machine, or a Juno's chorus run
// wide. One short delay line read at three taps, each swung by a slow and a
// fast sine a third of a turn apart from the others, so the copies never
// line up; the taps go left, right and both, darkened a little as a bucket
// brigade darkens them. One for the whole style, never one per voice: the
// voices are summed into it.
class Ensemble {
public:
    void init(float rate) {
        m_rate = rate;
        m_line.init(size_t(0.03f * rate));
        m_lpL.set(12000.0f, rate);
        m_lpR.set(12000.0f, rate);
        // Only what is above the low end is chorused: under it the bed is
        // mono and steady, and a moving comb there would only wobble its level.
        m_hp.set(220.0f, 0.6f, rate);
    }
    // in: the style's voices, summed; out: the chorus, added.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n) {
        const float base = 0.0085f * m_rate, slow = 0.0024f * m_rate, fast = 0.00022f * m_rate;
        float d0[3], d1[3];
        for (int k = 0; k < 3; ++k)
            d0[k] = base + slow * std::sin(kTwoPi * (m_slow + float(k) / 3.0f)) + fast * std::sin(kTwoPi * (m_fast + float(k) / 3.0f));
        m_slow += 0.63f * float(n) / m_rate;
        m_fast += 5.7f * float(n) / m_rate;
        m_slow -= std::floor(m_slow);
        m_fast -= std::floor(m_fast);
        for (int k = 0; k < 3; ++k)
            d1[k] = base + slow * std::sin(kTwoPi * (m_slow + float(k) / 3.0f)) + fast * std::sin(kTwoPi * (m_fast + float(k) / 3.0f));
        const float inv = 1.0f / float(n);
        for (int i = 0; i < n; ++i) {
            m_line.push(m_hp.step(0.5f * (inL[i] + inR[i])).high);
            const float u = float(i + 1) * inv;
            const float t0 = m_line.read(lerpf(d0[0], d1[0], u));
            const float t1 = m_line.read(lerpf(d0[1], d1[1], u));
            const float t2 = m_line.read(lerpf(d0[2], d1[2], u));
            outL[i] += m_lpL.step(0.75f * t0 + 0.35f * t2);
            outR[i] += m_lpR.step(0.75f * t1 + 0.35f * t2);
        }
    }
    void clear() {
        m_line.clear();
        m_hp.reset();
        m_lpL.y = m_lpR.y = 0;
    }

private:
    float m_rate = 48000, m_slow = 0, m_fast = 0;
    Delay m_line;
    OnePoleLp m_lpL, m_lpR;
    Svf m_hp;
};

// Dub's bus: a tape echo at a dotted eighth, bouncing left to right, each
// repeat thinner and darker than the last (a high-pass and a two-pole
// low-pass in the loop, and a soft saturation, as on a tape delay driven
// hard, which also keeps a feedback near one from running away), the tape
// wowing slowly under it, and its hiss. Like the ensemble, one for the style.
class DubEcho {
public:
    void init(float rate) {
        m_rate = rate;
        m_l.init(size_t(2.6f * rate));
        m_r.init(size_t(2.6f * rate));
        m_hpL.set(180.0f, 0.6f, rate);
        m_hpR.set(180.0f, 0.6f, rate);
        m_lpL.set(1400.0f, 0.6f, rate);
        m_lpR.set(1400.0f, 0.6f, rate);
        m_hissL.set(5000.0f, rate);
        m_hissR.set(5000.0f, rate);
        m_time = 0.75f * 0.484f * rate;
    }
    // in: the style's sends, summed; out: the echoes (and hiss), added.
    // feedback per repeat; hiss, the tape noise's level.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, double samplesPerBeat,
                 float feedback, float hiss) {
        // The time follows the tempo as a tape does, gliding (a pitch bend
        // in the echoes) rather than jumping.
        const float want = clampf(float(0.75 * samplesPerBeat), 64.0f, float(m_l.mask) - 4.0f - 0.004f * m_rate);
        const float t0 = m_time;
        m_time += (want - m_time) * (1.0f - std::exp(-float(n) / (0.25f * m_rate)));
        const float w0 = m_wow;
        m_phase += 0.35f * float(n) / m_rate;
        m_phase -= std::floor(m_phase);
        m_wow = 0.0012f * m_rate * std::sin(kTwoPi * m_phase);
        const float inv = 1.0f / float(n);
        for (int i = 0; i < n; ++i) {
            const float u = float(i + 1) * inv;
            const float d = lerpf(t0, m_time, u), wow = lerpf(w0, m_wow, u);
            const float eL = m_l.read(d + wow), eR = m_r.read(d - 0.7f * wow);
            const float fL = m_lpL.step(m_hpL.step(eL).high).low, fR = m_lpR.step(m_hpR.step(eR).high).low;
            // The throw enters on the left; each repeat crosses over.
            m_l.push(0.5f * (inL[i] + inR[i]) + feedback * sat(fR));
            m_r.push(feedback * sat(fL));
            outL[i] += kReturn * eL + hiss * m_hissL.step(m_rng.bipolar());
            outR[i] += kReturn * eR + hiss * m_hissR.step(m_rng.bipolar());
        }
    }
    void clear() {
        m_l.clear();
        m_r.clear();
        m_hpL.reset();
        m_hpR.reset();
        m_lpL.reset();
        m_lpR.reset();
    }

private:
    static inline float sat(float x) { return 0.8f * fastTanh(1.25f * x); }
    // The echoes come back a little hotter than they went in, as a dub
    // engineer rides the return: the gaps between the stabs are theirs.
    static constexpr float kReturn = 1.4f;
    float m_rate = 48000, m_time = 17000, m_wow = 0, m_phase = 0;
    Delay m_l, m_r;
    Svf m_hpL, m_hpR, m_lpL, m_lpR;
    OnePoleLp m_hissL, m_hissR;
    Rng m_rng{0xD0BEu};
};

// Tape's bus: every layer of the style played back from a worn cassette.
// One short delay line per side, its time swung slowly (wow, 7 cents, up to
// 18 with the space up) and quickly (flutter, about 2), so the pitch drifts
// the way a tape's does and stays in the key; a gentle saturation, the cassette's low-pass and a
// little hiss. The style's layers have no dry path: all of them wobble.
class TapeBus {
public:
    void init(float rate) {
        m_rate = rate;
        m_l.init(size_t(0.02f * rate));
        m_r.init(size_t(0.02f * rate));
        m_lpL.set(6500.0f, 0.6f, rate);
        m_lpR.set(6500.0f, 0.6f, rate);
        m_hissL.set(4500.0f, rate);
        m_hissR.set(4500.0f, rate);
    }
    // wowCents: how far the wow bends the pitch either way.
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, float hiss, float wowCents) {
        // A delay swung by A sin(2πft) bends the pitch by up to 2πfA: at
        // 0.55 Hz each cent is 0.167 ms of swing; 0.03 ms at 6.3 Hz is 2
        // cents of flutter.
        const float depth0 = m_depth;
        m_depth = wowCents * 1.672e-4f * m_rate;
        auto at = [&](float depth) {
            return 0.006f * m_rate + depth * std::sin(kTwoPi * m_wow) + 0.00003f * m_rate * std::sin(kTwoPi * m_flutter);
        };
        const float d0 = at(depth0);
        m_wow += 0.55f * float(n) / m_rate;
        m_flutter += 6.3f * float(n) / m_rate;
        m_wow -= std::floor(m_wow);
        m_flutter -= std::floor(m_flutter);
        const float d1 = at(m_depth), inv = 1.0f / float(n);
        for (int i = 0; i < n; ++i) {
            m_l.push(inL[i]);
            m_r.push(inR[i]);
            const float d = lerpf(d0, d1, float(i + 1) * inv);
            outL[i] += m_lpL.step(sat(m_l.read(d))).low + hiss * m_hissL.step(m_rng.bipolar());
            outR[i] += m_lpR.step(sat(m_r.read(d))).low + hiss * m_hissR.step(m_rng.bipolar());
        }
    }
    void clear() {
        m_l.clear();
        m_r.clear();
        m_lpL.reset();
        m_lpR.reset();
    }

private:
    // A touch over unity for small signals (the playback amp makes up what
    // the cassette's low-pass takes), rounding off the loud ones.
    static inline float sat(float x) { return 0.75f * fastTanh(x / 0.7f); }
    float m_rate = 48000, m_wow = 0, m_flutter = 0.3f, m_depth = 0;
    Delay m_l, m_r;
    Svf m_lpL, m_lpR;
    OnePoleLp m_hissL, m_hissR;
    Rng m_rng{0x7A9Eu};
};

// The choir's vowels: three formants (centre, gain) each, the body under
// them (its top and its level), dark oo to open ee. The second formant is
// narrow enough to name the vowel; the first is wide, so it colours the
// whole voicing rather than singling out the one partial under it.
const float kVowel[5][3][2] = {
    {{330, 0.75f}, {760, 0.25f}, {2600, 0.04f}},  // oo
    {{470, 1.0f}, {860, 0.55f}, {2750, 0.1f}},    // oh
    {{760, 0.55f}, {1200, 0.75f}, {2800, 0.25f}}, // ah
    {{560, 0.7f}, {1800, 1.0f}, {2650, 0.4f}},    // eh
    {{320, 0.7f}, {2300, 1.6f}, {3100, 0.6f}},    // ee
};
const float kVowelBw[3] = {360, 140, 220};
const float kBodyHz[5] = {600, 850, 1500, 2600, 4500};
const float kBodyGain[5] = {0.7f, 0.85f, 1.1f, 1.4f, 1.7f};

// How loud the vowel filter makes a note's fundamental at hz (vowel position
// vv, 0 oo .. 4 ee): the magnitude of the body and the formants, roughly.
float vowelResponse(float vv, float hz) {
    const int a = std::min(3, int(vv));
    const float f = vv - float(a);
    const float bodyHz = std::exp(lerpf(std::log(kBodyHz[a]), std::log(kBodyHz[a + 1]), f));
    const float r = hz / bodyHz, q = 0.6f;
    float m = lerpf(kBodyGain[a], kBodyGain[a + 1], f) / std::sqrt((1 - r * r) * (1 - r * r) + (r / q) * (r / q));
    for (int i = 0; i < 3; ++i) {
        const float fc = std::exp(lerpf(std::log(kVowel[a][i][0]), std::log(kVowel[a + 1][i][0]), f));
        const float g = lerpf(kVowel[a][i][1], kVowel[a + 1][i][1], f);
        const float rr = hz / fc, qq = fc / kVowelBw[i];
        m += g * (rr / qq) / std::sqrt((1 - rr * rr) * (1 - rr * rr) + (rr / qq) * (rr / qq));
    }
    return m;
}

class AtmosEngine : public PartEngine {
public:
    void init(float rate) override {
        m_rate = rate;
        m_bank = &bank();
        for (int i = 0; i < kVoices; ++i)
            m_voices[size_t(i)].rng = Rng(0x7A3Fu + uint32_t(i) * 7919u);
        m_halo.init(rate);
        m_cloud.init(rate);
        m_ensemble.init(rate);
        m_echo.init(rate);
        m_tape.init(rate);
        m_chorus.init(rate);
        m_taraf.init(rate);
        m_gourd.init(rate);
        m_valley.init(rate);
        // (The caps: inharmonic to one another and to any tine, rung briefly.)
        m_bassHp1.set(38.0f, 0.54f, rate);
        m_bassHp2.set(38.0f, 1.31f, rate);
        m_gongHp1.set(36.0f, 0.54f, rate);
        m_gongHp2.set(36.0f, 1.31f, rate);
        m_pluckLp.set(5000.0f, 0.7f, rate);
        // Every voice's four tanpura strings, room enough for the lowest a
        // tanpura is tuned to here (an octave under the pad's floor).
        size_t len = 1;
        while (len < size_t(rate / midiToHz(33.0)) + 8)
            len <<= 1;
        m_stringMem.assign(size_t(kVoices) * 4 * len, 0.0f);
        for (int i = 0; i < kVoices; ++i)
            for (int k = 0; k < 4; ++k) {
                TanString &t = m_voices[size_t(i)].str[k];
                t.buf = m_stringMem.data() + (size_t(i) * 4 + size_t(k)) * len;
                t.mask = uint32_t(len - 1);
            }
        m_hpL.set(55.0f, 0.7f, rate);
        m_hpR.set(55.0f, 0.7f, rate);
        m_sideHp1.set(280.0f, 0.54f, rate);
        m_sideHp2.set(280.0f, 1.31f, rate);
        m_sheenL.set(2400.0f, 0.6f, rate);
        m_sheenR.set(2400.0f, 0.6f, rate);
    }

    void command(const Command &c, AudioContext &ctx) override {
        (void)ctx;
        switch (c.type) {
        case Command::On: {
            Voice *v = find(c.id, true);
            if (!v && (c.id & (1 << 30))) {
                // A loop's first round reaching a latched layer it was made
                // from: the live voice becomes the loop's, so the bed neither
                // doubles nor drops when the hand lets it go.
                for (Voice &w : m_voices)
                    if (w.active && !w.releasing && w.adoptable && w.id == (c.id & 0xFFFFFF)) {
                        w.id = c.id;
                        w.adoptable = false;
                        setTargets(w, c);
                        v = &w;
                        break;
                    }
                if (v)
                    break;
            }
            if (v) {
                // Already sounding (a loop coming round, a recording taking
                // over what is held): take the new shape, and if it was
                // fading, swell back from where it is.
                setTargets(*v, c);
                v->seamHold = false;
                if (v->releasing) {
                    v->releasing = false;
                    v->ramp = clampf(0.5f - std::sin(std::asin(clampf(1.0f - 2.0f * v->env, -1.0f, 1.0f)) / 3.0f), 0.0f, 1.0f);
                    v->attack = std::max(0.02f, c.f[5]);
                }
                break;
            }
            v = allocate();
            start(*v, c);
            break;
        }
        case Command::Move:
            if (Voice *v = find(c.id, false))
                setTargets(*v, c);
            break;
        case Command::Off:
            if (Voice *v = find(c.id, false)) {
                // A frozen layer keeps sounding until the thaw. A loop's
                // layer that was still sounding when the take ended carries
                // on over the seam to its next start, the way the bed was
                // sounding when it was taken; the page lets it go if the
                // loop stops.
                if (v->frozen)
                    v->pendingOff = true;
                else if ((c.id & (1 << 30)) && c.f[7] < 0.5f)
                    v->seamHold = true;
                else
                    v->releasing = true;
            }
            break;
        case kSeamRelease:
            for (Voice &v : m_voices)
                if (v.active && v.seamHold && replaySlot(v.id) == (c.i[0] & 7)) {
                    v.seamHold = false;
                    if (v.frozen)
                        v.pendingOff = true;
                    else
                        v.releasing = true;
                }
            break;
        case kAdoptable:
            if (Voice *v = find(c.id, false))
                v->adoptable = true;
            break;
        case Command::Param:
            switch (c.i[0]) {
            case PFreeze:
                setFreeze(c.f[0] > 0.5f);
                break;
            case PCloud: m_cloudOn = c.f[0] > 0.5f; break;
            case PPump: m_pump = std::clamp(int(std::lround(c.f[0])), 0, 2); break;
            case PSpace: m_space = clampf(c.f[0], 0.0f, 1.0f); break;
            case PSwell: m_swellOn = c.f[0] > 0.5f; break;
            case PTone: m_tone = clampf(c.f[0], 0.0f, 1.0f); break;
            case PChordToLoop:
                m_ownHand = false;
                break;
            case PChord: {
                // A loop can carry the bed's own progression; a chord key
                // pressed by hand takes over from it until comma.
                const int own = std::clamp(int(std::lround(c.f[0])), -1, 15);
                if (c.id & (1 << 30)) {
                    if (!m_ownHand) {
                        m_own = own;
                        m_ownLoop = own >= 0;
                    }
                } else {
                    m_own = own;
                    m_ownHand = own >= 0;
                    m_ownLoop = false;
                }
                break;
            }
            default: break;
            }
            break;
        default:
            break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        const Harmony &h = ctx.harmony();
        const Transport &tr = ctx.transport();
        const float dt = float(n) / m_rate;
        m_bed = resolveBed(h, m_own);

        // Slow globals.
        const float spb = float(tr.secondsPerBeat());
        // The swell rises for as long as S is held, up to eight bars, and
        // falls back over a bar.
        if (m_swellOn)
            m_swell = std::min(1.0f, m_swell + dt / (32.0f * spb));
        else
            m_swell = std::max(0.0f, m_swell - dt / (4.0f * spb));
        m_swellE = std::pow(m_swell, 0.6f);
        const float k = 1.0f - std::exp(-dt / 0.15f);
        // When the chord changes the halo lets go of the old one quickly, so
        // a long tail never sours the next chord; a freeze melts for a bar
        // and catches the new chord from whatever is sounding.
        uint32_t mask = 0;
        for (int i = 0; i < m_bed.count; ++i)
            mask |= 1u << (((int(std::lround(h.midi(m_bed.tones[i], 0))) % 12) + 12) % 12);
        if (mask != m_mask) {
            ++m_chordSerial;
            if (m_mask) {
                m_clear = 1;
                m_sinceChange = 0;
                m_cloud.chordChanged();
                if (m_freeze)
                    m_melt = kBeatsPerBar;
            }
            m_mask = mask;
        }
        m_sinceChange = std::min(64.0, m_sinceChange + double(n) * tr.beatsPerSample);
        m_clear *= std::exp(-dt / (0.5f * spb));
        m_melt = std::max(0.0, m_melt - double(n) * tr.beatsPerSample);
        const bool frozen = m_freeze && m_melt <= 0;
        m_freezeAmt += ((frozen ? 1.0f : 0.0f) - m_freezeAmt) * (1.0f - std::exp(-dt / 0.05f));
        m_spaceS += (m_space - m_spaceS) * k;
        m_toneS += (m_tone - m_toneS) * k;
        m_cloudAmt += ((m_cloudOn ? 1.0f : 0.0f) - m_cloudAmt) * (1.0f - std::exp(-dt / 0.6f));

        float dryL[kBlock] = {}, dryR[kBlock] = {};
        // Each style's bus (the classic one, unused, keeps the indexing plain).
        float busL[kStyles][kBlock] = {}, busR[kStyles][kBlock] = {};
        float weight = 0, dub = 0, neon = 0, tape = 0;
        bool live[kStyles] = {};
        m_breakWant = 1; // (a Gamelan pad's angsel lowers it while its silent beat lasts)
        if (m_tarafSerial != m_chordSerial) {
            // The sympathetic strings follow the chord: its tones from
            // middle C up, as many as there are strings.
            m_tarafSerial = m_chordSerial;
            float notes[Taraf::K];
            const int count = bedPitches(m_bed, h, false, 60.0f, 84.0f, notes, Taraf::K);
            m_taraf.tune(notes, count);
        }
        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            renderVoice(v, dryL, dryR, busL[v.style], busR[v.style], n, h, tr);
            weight += v.env;
            live[v.style] = true;
            if (v.style == Dub)
                dub += v.env;
            else if (v.style == Neon)
                neon += v.env;
            else if (v.style == Tape)
                tape += v.env;
        }
        renderStrikes(dryL, dryR, busL, busR, n);
        // Neon wants its chorus in front, not washed into the halo: the more
        // of the bed it is, the less of the room. Dub's echoes darken as
        // they go: the halo's octave shimmer and its long tail would outlast
        // them and brighten the end, so the room gives way to them. (With
        // nothing sounding the shares hold, so a tail keeps its room.)
        if (weight > 1e-3f) {
            const float k = 1.0f - std::exp(-dt / 0.5f);
            m_neonShare += (neon / weight - m_neonShare) * k;
            m_dubShare += (dub / weight - m_dubShare) * k;
        }
        // More layers are fuller, not much louder.
        const float normT = 0.6f / std::sqrt(1.0f + 0.5f * std::max(0.0f, weight - 1.0f));
        const float g0 = m_norm;
        m_norm += (normT - m_norm) * (1.0f - std::exp(-dt / 0.3f));
        const float swellGain = 1.0f + 0.4f * m_swellE;
        for (int i = 0; i < n; ++i) {
            const float g = lerpf(g0, m_norm, float(i + 1) / float(n)) * swellGain;
            dryL[i] *= g;
            dryR[i] *= g;
            for (int st = 1; st < kStyles; ++st) {
                busL[st][i] *= g;
                busR[st][i] *= g;
            }
        }
        // The styles' buses run while their layers sound and for as long as
        // their tails last, and are cleared once those have gone, so a style
        // never played costs nothing and never wakes with an old echo.
        // (Gamelan has no bus: its bronze rings in the halo and nothing else.)
        static const float kTail[kStyles] = {0.0f, 0.1f, 14.0f, 0.1f, 0.0f, 4.0f, 0.3f, 0.1f, 3.0f};
        for (int st = 1; st < kStyles; ++st) {
            if (live[st])
                m_busHold[st] = kTail[st];
            else if (m_busHold[st] > 0 && (m_busHold[st] -= dt) <= 0) {
                switch (st) {
                case Neon: m_ensemble.clear(); break;
                case Dub: m_echo.clear(); break;
                case Tape: m_tape.clear(); break;
                case Raga: m_taraf.clear(); break;
                case Mbira: m_gourd.clear(); break;
                case CityPop: m_chorus.clear(); break;
                case Khoomei: m_valley.clear(); break;
                default: break;
                }
            }
        }
        if (m_busHold[Raga] > 0)
            m_taraf.process(busL[Raga], busR[Raga], dryL, dryR, n, 1.6f + 2.4f * m_spaceS);
        if (m_busHold[Mbira] > 0)
            m_gourd.process(busL[Mbira], busR[Mbira], dryL, dryR, n);
        if (m_busHold[CityPop] > 0)
            m_chorus.process(busL[CityPop], busR[CityPop], dryL, dryR, n);
        if (m_busHold[Khoomei] > 0)
            m_valley.process(busL[Khoomei], busR[Khoomei], dryL, dryR, n);
        if (m_busHold[Tape] > 0) {
            m_tapeHiss += (0.006f * std::min(1.0f, tape) - m_tapeHiss) * (1.0f - std::exp(-dt / 0.8f));
            // ↑ (space) makes the tape more seasick: the wow 7 cents at the
            // middle, 18 at the top, under 3 at the bottom.
            m_tape.process(busL[Tape], busR[Tape], dryL, dryR, n, m_tapeHiss, 7.0f * std::exp2(2.72f * (m_spaceS - 0.5f)));
        }
        if (m_busHold[Neon] > 0)
            m_ensemble.process(busL[Neon], busR[Neon], dryL, dryR, n);
        if (m_busHold[Dub] > 0) {
            // The echo's tail is six to eight seconds; ↑ (space) pushes it
            // toward a throw that hangs for half a minute, ↓ shortens it.
            // It lets the old chord go quicker on a change, as the halo
            // does; the tape hiss is there while the style plays.
            const float push = m_spaceS > 0.5f ? 0.4f * (m_spaceS - 0.5f) : -0.3f * (0.5f - m_spaceS);
            const float fb = (0.65f + push + 0.05f * m_swellE) * (1.0f - 0.55f * m_clear);
            m_hiss += (0.01f * std::min(1.0f, dub) - m_hiss) * (1.0f - std::exp(-dt / 0.8f));
            m_echo.process(busL[Dub], busR[Dub], dryL, dryR, n, tr.samplesPerBeat(), fb, m_hiss);
        }
        renderGlints(dryL, dryR, n);
        // Tone, the player's - and =: below the middle a low-pass closes over
        // the bed (and so over its halo), from about 16 kHz down to about
        // 1.1; above it the sheen lifts the top. In the middle the bed is as
        // it was built, the filter out of the way.
        const float darker = clampf((0.5f - m_toneS) * 2.0f, 0.0f, 1.0f);
        const float brighter = clampf((m_toneS - 0.5f) * 2.0f, 0.0f, 1.0f);
        if (darker > 0.002f) {
            const float hz = 16000.0f * std::exp2(-3.8f * darker);
            if (std::fabs(hz - m_toneSet) > 0.002f * hz) {
                m_toneSet = hz;
                m_toneL.set(hz, 0.6f, m_rate);
                copyCoefficients(m_toneR, m_toneL);
            }
            for (int i = 0; i < n; ++i) {
                dryL[i] = m_toneL.step(dryL[i]).low;
                dryR[i] = m_toneR.step(dryR[i]).low;
            }
        } else {
            m_toneL.reset();
            m_toneR.reset();
        }
        // Sheen: the top of the bed lifted a touch always (more with the
        // tone up, none with it down), and a lot as it swells.
        const float sheen = (0.25f + 1.2f * brighter) * (1.0f - darker) + 4.5f * m_swellE;
        for (int i = 0; i < n; ++i) {
            dryL[i] += sheen * m_sheenL.step(dryL[i]).high;
            dryR[i] += sheen * m_sheenR.step(dryR[i]).high;
        }

        // The room the bed lives in.
        float t60 = (3.0f + 9.0f * m_spaceS + 6.0f * m_swellE) * (1.0f - 0.85f * m_clear) * (1.0f - 0.6f * m_dubShare);
        if (m_melt > 0)
            t60 = std::min(t60, 4.0f);
        const float shimmer = (0.12f + 0.35f * m_spaceS + 0.65f * m_swellE) * (1.0f - 0.9f * m_dubShare);
        m_halo.set(t60, shimmer, m_freezeAmt, n);
        m_cloud.set(m_cloudAmt);
        const float send = (0.45f + 0.35f * m_spaceS + 0.35f * m_swellE) * (1.0f - 0.45f * m_neonShare);
        const float wet = (0.35f + 0.8f * m_spaceS) * (1.0f + 0.1f * m_freezeAmt) * (1.0f - 0.3f * m_neonShare);
        // The swell thins the bottom as it rises, the way a build-up does: the
        // bed's low cut climbs from 55 Hz to about 400, leaving the low mids their warmth.
        const float lowCut = 55.0f * std::exp2(2.9f * m_swellE);
        if (std::fabs(lowCut - m_lowCutSet) > 0.5f) {
            m_lowCutSet = lowCut;
            m_hpL.set(lowCut, 0.7f, m_rate);
            copyCoefficients(m_hpR, m_hpL);
        }
        const float *duck = ctx.duck();
        const float depth = m_pump == PumpKick ? 0.6f : m_pump == PumpBeat ? 0.65f : 0.0f;
        const float pumpAtt = 1.0f - std::exp(-1.0f / (0.004f * m_rate));
        const float breakIn = 1.0f - std::exp(-1.0f / (0.008f * m_rate)), breakOut = 1.0f - std::exp(-1.0f / (0.06f * m_rate));
        const bool cloud = m_cloud.busy();
        float sq = 0, wetSq = 0;
        for (int i = 0; i < n; ++i) {
            float cL = 0, cR = 0;
            m_cloud.feed(0.5f * (dryL[i] + dryR[i]));
            if (cloud)
                m_cloud.process(cL, cR);
            float hL, hR;
            m_halo.process((dryL[i] + cL) * send, (dryR[i] + cR) * send, hL, hR);
            // The pump: the kick's own envelope, or one of our own on every
            // beat (down at once, back over half a beat), so a bed can
            // breathe in time even before the drums come in.
            float e = 0;
            if (m_pump == PumpKick) {
                e = duck[i];
            } else if (m_pump == PumpBeat) {
                const double b = tr.beatAt(i);
                const float ph = float(b - std::floor(b));
                const float u = ph < 0.55f ? 1.0f - ph / 0.55f : 0.0f;
                e = u * u;
            }
            m_pumpEnv += (e - m_pumpEnv) * (e > m_pumpEnv ? pumpAtt : 1.0f);
            // An angsel's silence is the room's too: the halo is held down
            // while it lasts (within 25 ms) and let back over a sixth of a
            // second as the ensemble comes in again.
            m_break += (m_breakWant - m_break) * (m_breakWant < m_break ? breakIn : breakOut);
            const float gd = 1.0f - depth * m_pumpEnv, gw = (1.0f - 0.55f * depth * m_pumpEnv) * m_break;
            float oL = (dryL[i] + 0.7f * cL) * gd + hL * wet * gw;
            float oR = (dryR[i] + 0.7f * cR) * gd + hR * wet * gw;
            // Mono under about 250 Hz: all the width stays above it, none
            // of it fights the low end.
            const float mid = 0.5f * (oL + oR);
            const float side = m_sideHp2.step(m_sideHp1.step(0.5f * (oL - oR)).high).high;
            oL = m_hpL.step(mid + side).high;
            oR = m_hpR.step(mid - side).high;
            l[i] += oL;
            r[i] += oR;
            sq += oL * oL + oR * oR;
            wetSq += hL * hL + hR * hR;
        }
        m_levelOut += (std::sqrt(sq / float(2 * n)) - m_levelOut) * 0.05f;
        m_haloOut += (std::sqrt(wetSq / float(2 * n)) * wet - m_haloOut) * 0.05f;
    }

    void silence(bool hard) override {
        for (Voice &v : m_voices) {
            if (hard)
                v.active = false;
            else
                v.releasing = true;
        }
        for (Glint &g : m_glints)
            if (hard)
                g.on = false;
        setFreeze(false);
        m_swellOn = false;
        if (hard) {
            m_halo.clear();
            m_ensemble.clear();
            m_echo.clear();
            m_tape.clear();
            m_chorus.clear();
            m_taraf.clear();
            m_gourd.clear();
            m_valley.clear();
            for (Strike &st : m_strikes)
                st.on = false;
            for (float &b : m_busHold)
                b = 0;
            m_freezeAmt = 0;
        }
    }

    void publish(float *d) override {
        d[PubFreeze] = m_freeze ? 1.0f : 0.0f;
        d[PubCloud] = m_cloudOn ? 1.0f : 0.0f;
        d[PubPump] = float(m_pump);
        d[PubSpace] = m_space;
        d[PubSwell] = m_swell;
        d[PubOwn] = float(m_own);
        d[PubRoot] = float(m_bed.root);
        d[PubCount] = float(m_bed.count);
        for (int i = 0; i < 4; ++i)
            d[PubTones + i] = float(m_bed.tones[i]);
        d[PubHalo] = m_haloOut;
        d[PubPumpEnv] = m_pumpEnv * (m_pump ? 1.0f : 0.0f);
        d[PubVoices] = float(voices());
        d[PubLevel] = m_levelOut;
        d[PubOwnLoop] = m_ownLoop ? 1.0f : 0.0f;
        d[PubOwnHand] = m_ownHand ? 1.0f : 0.0f;
        d[PubTone] = m_tone;
        int frozen = 0, k = 0;
        for (const Voice &v : m_voices) {
            frozen += v.active && v.frozen;
            if (!v.active || k >= kPubVoices)
                continue;
            float *o = d + PubVoice0 + kPubStride * k++;
            o[0] = float(v.family + 1);
            o[1] = v.cur[0];
            o[2] = v.cur[1];
            o[3] = v.level;
            o[4] = ((v.id & (1 << 30)) ? 2.0f : 1.0f) + (v.frozen ? 4.0f : 0.0f) + (v.releasing ? 8.0f : 0.0f) +
                   16.0f * float(v.style);
            o[5] = v.cur[3];
            o[6] = v.cur[4];
            o[7] = v.gate;
            o[8] = float(v.id & 0xFFFFFF);
        }
        for (; k < kPubVoices; ++k)
            d[PubVoice0 + kPubStride * k] = 0;
        d[PubFrozen] = float(frozen);
        int g = 0;
        for (const Glint &gl : m_glints) {
            if (!gl.on || gl.delay > 0 || g >= kPubGlints)
                continue;
            d[PubGlint0 + 3 * g] = gl.x;
            d[PubGlint0 + 3 * g + 1] = gl.y;
            d[PubGlint0 + 3 * g + 2] = std::min(1.0f, gl.amp * 12.0f);
            ++g;
        }
        // A throat singer's whistle shows as a glint at its overtone, high
        // in the column for a high one.
        for (const Voice &v : m_voices) {
            if (!v.active || v.family != Pad || v.style != Khoomei || v.whistleShown < 0.02f || g >= kPubGlints)
                continue;
            d[PubGlint0 + 3 * g] = xOf(Pad, v.cur[1]);
            d[PubGlint0 + 3 * g + 1] = clampf((v.whistle - 4.0f) / 15.0f, 0.02f, 0.98f);
            d[PubGlint0 + 3 * g + 2] = std::min(1.0f, v.whistleShown);
            ++g;
        }
        // The strikes of a struck pad show as glints too, where they sound.
        for (const Strike &st : m_strikes) {
            if (!st.on || st.delay > 0 || st.shown < 0.02f || g >= kPubGlints)
                continue;
            d[PubGlint0 + 3 * g] = st.x;
            d[PubGlint0 + 3 * g + 1] = st.y;
            d[PubGlint0 + 3 * g + 2] = std::min(1.0f, st.shown);
            ++g;
        }
        for (; g < kPubGlints; ++g)
            d[PubGlint0 + 3 * g + 2] = 0;
    }

    std::unique_ptr<PartEngine> twin() const override { return std::make_unique<AtmosEngine>(); }

    int voices() const override {
        int n = 0;
        for (const Voice &v : m_voices)
            n += v.active;
        return n;
    }

private:
    // Freeze holds the bed itself: every layer sounding now keeps sounding,
    // at the level it has, after its finger lifts, and keeps following the
    // chord; the halo turns lossless behind it. A thaw lets go of whatever
    // was lifted meanwhile.
    void setFreeze(bool on) {
        if (on == m_freeze)
            return;
        m_freeze = on;
        // Right on a chord change the halo waits for the new chord.
        m_melt = on ? std::max(0.0, 1.5 - m_sinceChange) : 0.0;
        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            if (on) {
                if (!v.releasing) {
                    v.frozen = true;
                } else if (v.env > 0.02f) {
                    v.frozen = v.holdEnv = v.pendingOff = true;
                    v.releasing = false;
                }
            } else if (v.frozen) {
                v.frozen = v.holdEnv = false;
                if (v.pendingOff) {
                    v.pendingOff = false;
                    v.releasing = true;
                    v.ramp = 1;
                }
            }
        }
    }

    Voice *find(int id, bool evenReleasing) {
        for (Voice &v : m_voices)
            if (v.active && v.id == id && (evenReleasing || !v.releasing))
                return &v;
        return nullptr;
    }

    Voice *allocate() {
        for (Voice &v : m_voices)
            if (!v.active)
                return &v;
        Voice *best = nullptr;
        for (Voice &v : m_voices)
            if (v.releasing && (!best || v.env < best->env))
                best = &v;
        if (best)
            return best;
        best = &m_voices[0];
        for (Voice &v : m_voices)
            if (v.env * v.cur[2] < best->env * best->cur[2])
                best = &v;
        return best;
    }

    static void setTargets(Voice &v, const Command &c) {
        v.tgt[0] = clampf(c.f[0], 0.0f, 1.0f);
        v.tgt[1] = clampf(c.f[1], 0.0f, 1.0f);
        v.tgt[2] = clampf(c.f[2], 0.0f, 1.0f);
        v.tgt[3] = clampf(c.f[3], -1.0f, 1.0f);
        v.tgt[4] = clampf(c.f[4], 0.0f, 1.0f);
    }

    void start(Voice &v, const Command &c) {
        v.active = true;
        v.releasing = false;
        v.frozen = v.holdEnv = v.pendingOff = v.adoptable = v.seamHold = false;
        v.agcRms = 0.3f;
        v.agc = 1.0f / 0.3f;
        v.lfo3 = v.rng.unit();
        v.rate3 = 0.025f + 0.02f * v.rng.unit();
        v.id = c.id;
        v.family = std::clamp(c.i[0], 0, kFamilies - 1);
        v.style = std::clamp(c.i[1], 0, kStyles - 1);
        v.stab = 0;
        v.stabRising = false;
        v.stabStep = INT64_MIN;
        // Tape's keys are struck as they land.
        v.stabRising = v.style == Tape && v.family == Pad;
        v.chordSeen = m_chordSerial;
        v.hammer = v.stabRising ? 1.0f : 0.0f;
        // A struck pad strikes as it lands (the piano its chord, the others
        // their figure from the next step); a tanpura starts its round.
        v.hit = v.family == Pad && struckPad(v.style);
        v.figStep = INT64_MIN;
        v.cycleFrom = INT64_MIN;
        v.angselArmed = false; // (a pad laid down already twisted plays on; the break is for the twist)
        v.angselFrom = INT64_MIN;
        v.bassN = v.bassPeriod = 0;
        v.bassAimed = -999;
        v.bassLast = INT64_MIN;
        v.bassSlide = false;
        for (int &k : v.lastStrike)
            k = -1;
        v.trem = 0;
        v.tremDepth = 0;
        v.envPrev = 0;
        if (v.family == Pad && v.style == Raga)
            for (TanString &t : v.str) {
                t.clear();
                t.midi = 0;
                t.stilled = false;
                t.since = 10;
                t.level = 0;
            }
        // (Not draws of their own: the classic layers' random sequence
        // stays as it was.)
        v.thPhase = 0;
        v.thMidi = 0;
        v.thF2.reset();
        v.thHp1.reset();
        v.thHp2.reset();
        v.whistle = v.whistleTo = 8;
        v.whistleShown = v.thSub = v.thFlick = v.thFlickLeft = 0;
        v.thStep = INT64_MIN;
        // The singer comes up into the first note from under it.
        v.thBreath = 0, v.thBreath2 = 0.37f, v.thWander = 0;
        v.thScoop = 1, v.thWhistleScoop = 0;
        setTargets(v, c);
        for (int j = 0; j < 5; ++j)
            v.cur[j] = v.tgt[j];
        v.attack = std::max(0.02f, c.f[5] > 0 ? c.f[5] : 0.4f);
        // Bronze, tines and a piano's keys are struck, however lightly the
        // finger lands: each strike brings its own attack, and the layer is
        // there at once to carry it (pressure is how hard, not how slowly).
        if (v.family == Pad && struckPad(v.style) && v.style != Raga)
            v.attack = 0.005f;
        v.ramp = 0;
        v.env = 0;
        v.relCoef = std::exp(-1.0f / (kShape[v.family].release / 4.6f * m_rate));
        v.norm = 1;
        v.level = 0;
        v.gate = v.gateSmooth = 1;
        v.lfo1 = v.rng.unit();
        v.lfo2 = v.rng.unit();
        v.pwm = v.lfo2; // not a draw of its own: the classic layers' random sequence stays as it was
        v.rate1 = 0.05f + 0.04f * v.rng.unit();
        v.rate2 = 0.11f + 0.06f * v.rng.unit();
        v.side = (v.rng.next() & 1) ? 1.0f : -1.0f;
        v.dropClock = 0;
        for (Slot &s : v.notes)
            s = Slot{};
        v.lpL.reset();
        v.lpR.reset();
        for (int i = 0; i < 3; ++i) {
            v.fL[i].reset();
            v.fR[i].reset();
        }
        v.rainL.reset();
        v.rainR.reset();
        v.crackle.reset();
        v.crackle.set(2600.0f, 0.8f, m_rate);
        v.hissL.set(6500.0f, m_rate);
        v.hissR.set(6500.0f, m_rate);
    }

    // Spawns a glint delay samples into the coming block.
    void spawnGlint(float hz, float amp, float decaySeconds, float chirp, float pan, int delay, float x, float y) {
        Glint *g = nullptr;
        for (Glint &q : m_glints)
            if (!q.on) {
                g = &q;
                break;
            }
        if (!g) {
            g = &m_glints[0];
            for (Glint &q : m_glints)
                if (q.amp < g->amp)
                    g = &q;
        }
        g->on = true;
        g->delay = delay;
        g->hz = std::min(hz, 0.4f * m_rate);
        g->chirp = chirp;
        g->chirpCoef = std::exp(-1.0f / (0.012f * m_rate));
        g->c = 1;
        g->s = 0;
        g->amp = amp;
        g->decay = std::exp(-1.0f / (decaySeconds * m_rate));
        g->att = 0;
        g->attInc = 1.0f / (0.002f * m_rate);
        g->gl = std::sqrt(0.5f * (1.0f - pan));
        g->gr = std::sqrt(0.5f * (1.0f + pan));
        g->x = x;
        g->y = y;
    }

    void renderGlints(float *l, float *r, int n) {
        for (Glint &g : m_glints) {
            if (!g.on)
                continue;
            if (g.delay >= n) {
                g.delay -= n;
                continue;
            }
            const int from = g.delay;
            g.delay = 0;
            // Frequency for this block (a raindrop rises to its note), as a
            // rotation: four multiplies a sample.
            const float hz = g.hz * (1.0f - g.chirp);
            g.chirp *= std::pow(g.chirpCoef, float(n));
            const float w = kTwoPi * hz / m_rate;
            const float cw = std::cos(w), sw = std::sin(w);
            for (int i = from; i < n; ++i) {
                const float c = g.c * cw - g.s * sw;
                const float s = g.c * sw + g.s * cw;
                g.c = c;
                g.s = s;
                g.att = std::min(1.0f, g.att + g.attInc);
                const float v = s * g.amp * g.att;
                g.amp *= g.decay;
                l[i] += v * g.gl;
                r[i] += v * g.gr;
            }
            const float m = 1.5f - 0.5f * (g.c * g.c + g.s * g.s);
            g.c *= m;
            g.s *= m;
            if (g.amp < 1e-4f)
                g.on = false;
        }
    }

    // A voice into the dry bed, and into its style's bus (busL, busR) by the
    // style's send.
    void renderVoice(Voice &v, float *outL, float *outR, float *busL, float *busR, int n, const Harmony &h,
                     const Transport &tr) {
        const FamilyShape &fs = kShape[v.family];
        const float dt = float(n) / m_rate;
        const float ks = 1.0f - std::exp(-dt / 0.08f);
        for (int j = 0; j < 5; ++j)
            v.cur[j] += (v.tgt[j] - v.cur[j]) * ks;
        // A third, slower drift moves the colour and the filter over half a
        // minute or so, so even one finger held still keeps evolving.
        v.lfo3 += v.rate3 * dt;
        v.lfo3 -= std::floor(v.lfo3);
        const float drift = std::sin(kTwoPi * v.lfo3);
        const float height = v.cur[0], press = v.cur[2], motion = v.cur[3], width = v.cur[4];
        const float colour = clampf(v.cur[1] + (v.family == Choir ? 0.05f : 0.15f) * drift, 0.0f, 1.0f);
        const float P = fs.low + height * (fs.high - fs.low);

        // The voicing window: every chord tone between P and P + span, the
        // ones near either edge faded, so climbing the pad brings tones in at
        // the top and lets them go at the bottom. Slots hold their pitch for
        // life; a chord change fades the old tones out and the new ones in,
        // while the tones the chords share keep sounding.
        float cand[16];
        // (Dub's stabs and City pop's piano take the chord with its seventh
        // and ninth.)
        const int nc = bedPitches(m_bed, h, v.family == Drone, P, P + fs.span, cand, 16,
                                  v.family == Pad && (v.style == Dub || v.style == CityPop));
        if (v.family == Pad && struckPad(v.style)) {
            renderStruck(v, cand, nc, P, colour, outL, outR, busL, busR, n, h, tr);
            return;
        }
        if (v.family == Pad && v.style == Khoomei) {
            renderThroat(v, colour, outL, outR, busL, busR, n, h, tr);
            return;
        }
        for (Slot &s : v.notes)
            s.seen = false;
        int ta, tb;
        float wa, wb;
        timbres(v.family, colour, v.style, ta, tb, wa, wb);
        const float rootMidi = float(h.midi(m_bed.root, 0));
        float resp[16] = {}, respMean = 1;
        if (v.family == Choir && nc > 0) {
            respMean = 0;
            for (int k = 0; k < nc; ++k)
                respMean += resp[k] = vowelResponse(colour * 4.0f, midiToHz(cand[k]));
            respMean /= float(nc);
        }
        float w2 = 0;
        constexpr float edge = 2.5f;
        for (int c = 0; c < nc; ++c) {
            const float m = cand[c];
            float w = smoothstep(0.0f, edge, m - P) * smoothstep(0.0f, edge, P + fs.span - m);
            if (v.family == Drone) {
                const float iv = std::fmod(m - rootMidi + 1200.0f, 12.0f);
                if (iv > 0.5f && iv < 11.5f)
                    w *= 0.7f; // the fifth sits under the root
            }
            if (w < 0.002f)
                continue;
            w2 += w * w;
            Slot *slot = nullptr;
            for (Slot &s : v.notes)
                if (s.on && std::fabs(s.midi - m) < 0.05f) {
                    slot = &s;
                    break;
                }
            if (!slot) {
                for (Slot &s : v.notes)
                    if (!s.on) {
                        slot = &s;
                        break;
                    }
                if (!slot) {
                    for (Slot &s : v.notes)
                        if (s.target == 0 && (!slot || s.gain < slot->gain))
                            slot = &s;
                    if (!slot || slot->gain > 0.05f)
                        continue;
                }
                slot->on = true;
                slot->midi = m;
                slot->gain = 0;
                slot->octave = octaveFor(ta, m);
                const double speed = double(midiToHz(m)) / double(midiToHz(Bank::baseMidi(slot->octave))) *
                                     double(kTableRate) / double(m_rate);
                slot->inc = uint32_t(std::llround(speed * double(1u << kFracBits)));
                slot->phase = v.rng.next();
            }
            // The choir: each note leveled against what the vowel does to
            // it, so the voices sing the chord, not the note under the
            // formant.
            if (v.family == Choir)
                w *= clampf(respMean / std::max(1e-3f, resp[c]), 0.35f, 3.0f);
            slot->target = w;
            slot->seen = true;
        }
        for (Slot &s : v.notes)
            if (s.on && !s.seen)
                s.target = 0;
        v.norm += (1.0f / std::sqrt(std::max(1.0f, w2)) - v.norm) * ks;

        const bool morph = tb != ta && wb > 0.02f && wa > 0.02f;
        if (!morph) {
            if (wb > wa)
                ta = tb;
            wa = 1;
        }

        // Neon's poly is a pulse of swinging width: its saw table read a
        // second time, w of a period ahead, and taken away, since
        // saw(φ) − saw(φ + w) is a pulse of width w. The width swings
        // slowly between thin and square, the moving hollowness of a Juno's
        // PWM; one more table read per note.
        const bool pwm = v.style == Neon && v.family == Pad && (morph ? tb == PadPoly : ta == PadPoly);
        float wm = 0, wp = 0, w0 = 0, w1 = 0;
        if (pwm) {
            wm = morph ? wa : 0.0f;
            wp = morph ? wb : 1.0f;
            w0 = 0.5f + 0.36f * std::sin(kTwoPi * v.pwm);
            v.pwm += 0.45f * dt;
            v.pwm -= std::floor(v.pwm);
            w1 = 0.5f + 0.36f * std::sin(kTwoPi * v.pwm);
        }

        // The tones.
        float sL[kBlock] = {}, sR[kBlock] = {};
        // (Tape's keys change chord as a hand does, at once, so the strike
        // lands on the new notes rather than on a crossfade.)
        const bool struck = v.style == Tape && v.family == Pad;
        const float aIn = 1.0f - std::exp(-dt / (struck ? 0.012f : 0.12f));
        const float aOut = 1.0f - std::exp(-dt / (struck ? 0.12f : v.family == Glass ? 0.6f : 0.35f));
        const uint32_t half = 1u << 31;
        for (Slot &s : v.notes) {
            if (!s.on)
                continue;
            const float g0 = s.gain;
            s.gain += (s.target - s.gain) * (s.target > s.gain ? aIn : aOut);
            if (s.target == 0 && s.gain < 1e-4f) {
                s.on = false;
                continue;
            }
            const float dg = (s.gain - g0) / float(n);
            const float *A = m_bank->table[ta][s.octave].data();
            uint32_t ph = s.phase;
            const uint32_t inc = s.inc;
            if (pwm) {
                const float *M = m_bank->table[PadMachine][s.octave].data();
                const float *Q = m_bank->table[PadPoly][s.octave].data();
                // A period of the table's own pitch, in phase units.
                const double period = double(kTableRate) / double(midiToHz(Bank::baseMidi(s.octave))) * double(1u << kFracBits);
                uint32_t off = uint32_t(std::llround(double(w0) * period));
                const int32_t dOff = int32_t(std::llround(double(w1 - w0) * period / double(n)));
                constexpr uint32_t fm = (1u << kFracBits) - 1;
                const float wq = 0.7f * wp;
                for (int i = 0; i < n; ++i) {
                    const float g = g0 + dg * float(i + 1);
                    const uint32_t i0 = ph >> kFracBits, j0 = (ph + half) >> kFracBits;
                    const float fr = float(ph & fm) * kFracScale;
                    const uint32_t q = ph + off, k0 = q >> kFracBits, l0 = (q + half) >> kFracBits;
                    const float fq = float(q & fm) * kFracScale;
                    const float m = M[i0] + fr * (M[i0 + 1] - M[i0]), mr = M[j0] + fr * (M[j0 + 1] - M[j0]);
                    const float p = Q[i0] + fr * (Q[i0 + 1] - Q[i0]) - (Q[k0] + fq * (Q[k0 + 1] - Q[k0]));
                    const float pr = Q[j0] + fr * (Q[j0 + 1] - Q[j0]) - (Q[l0] + fq * (Q[l0 + 1] - Q[l0]));
                    sL[i] += g * (wm * m + wq * p);
                    sR[i] += g * (wm * mr + wq * pr);
                    ph += s.inc;
                    off += uint32_t(dOff);
                }
            } else if (morph) {
                const float *B = m_bank->table[tb][s.octave].data();
                for (int i = 0; i < n; ++i) {
                    const float g = g0 + dg * float(i + 1);
                    const uint32_t i0 = ph >> kFracBits, j0 = (ph + half) >> kFracBits;
                    const float fr = float(ph & ((1u << kFracBits) - 1)) * kFracScale;
                    const float a = A[i0] + fr * (A[i0 + 1] - A[i0]), ar = A[j0] + fr * (A[j0 + 1] - A[j0]);
                    const float b = B[i0] + fr * (B[i0 + 1] - B[i0]), br = B[j0] + fr * (B[j0 + 1] - B[j0]);
                    sL[i] += g * (wa * a + wb * b);
                    sR[i] += g * (wa * ar + wb * br);
                    ph += inc;
                }
            } else {
                for (int i = 0; i < n; ++i) {
                    const float g = g0 + dg * float(i + 1);
                    const uint32_t i0 = ph >> kFracBits, j0 = (ph + half) >> kFracBits;
                    const float fr = float(ph & ((1u << kFracBits) - 1)) * kFracScale;
                    sL[i] += g * (A[i0] + fr * (A[i0 + 1] - A[i0]));
                    sR[i] += g * (A[j0] + fr * (A[j0 + 1] - A[j0]));
                    ph += inc;
                }
            }
            s.phase = ph;
        }

        // Breath: two slow incommensurate swells per layer, so a held bed
        // is never quite still; motion makes it deeper.
        v.lfo1 += v.rate1 * dt;
        v.lfo2 += v.rate2 * dt;
        v.lfo1 -= std::floor(v.lfo1);
        v.lfo2 -= std::floor(v.lfo2);
        const float b1 = std::sin(kTwoPi * v.lfo1), b2 = std::sin(kTwoPi * v.lfo2);
        const float evolve = std::max(0.0f, motion), pulse = std::max(0.0f, -motion);
        // Air breathes hardest: wind comes in gusts.
        const float gust = v.family == Air ? 2.5f : 1.0f;
        const float breath = 1.0f + std::sqrt(gust) * ((0.05f + 0.08f * evolve) * b1 + 0.035f * b2);
        float oct = 0.3f * gust * b2 + 0.9f * drift + 4.2f * m_swellE;
        if (evolve > 0.04f) {
            // A slow filter sweep locked to the bars: four bars round at a
            // touch of twist, one bar at full.
            const double period = lerpf(16.0f, 4.0f, evolve);
            const double ph = tr.beat / period + 0.17 * v.family;
            oct += 2.3f * evolve * std::sin(kTwoPi * float(ph - std::floor(ph)));
        }

        // Family colour and filter.
        float cutoff = 1000.0f, q = 0.75f;
        switch (v.family) {
        case Drone:
            cutoff = 260.0f * std::exp2(0.6f + 3.0f * height + 1.6f * (press - 0.5f) + 0.8f * colour + oct);
            q = 0.9f + 0.5f * evolve;
            break;
        case Pad:
            if (v.style == Dub) {
                // Dub's chord sits low and rings: a resonant low-pass, more
                // resonant toward the stab, that opens on every hit and
                // closes as it decays, and sweeps with the bars when twisted
                // clockwise, the way a dub chord's filter is ridden.
                cutoff = 190.0f * std::exp2(2.4f * height + 1.2f * (press - 0.5f) + 0.7f * oct + 2.5f * v.stab);
                q = 1.2f + 1.5f * colour;
            } else {
                // Neon's machine and poly are bright by nature, the filter
                // further open and a touch resonant, as an eighties pad is.
                const bool neon = v.style == Neon;
                cutoff = 380.0f * std::exp2((neon ? 2.5f : 1.1f) + 3.4f * height + 1.8f * (press - 0.5f) + oct);
                q = (neon ? 1.0f : 0.8f) + 0.4f * evolve;
                // Tape's keys are warm: the filter lower, and the colour
                // opens it, felt to bright.
                if (v.style == Tape)
                    cutoff = 380.0f * std::exp2(0.4f + 2.6f * height + 1.8f * (press - 0.5f) + oct + 1.6f * colour +
                                                1.5f * v.hammer);
            }
            break;
        case Choir:
            cutoff = 3400.0f * std::exp2(1.0f * height + 0.8f * (press - 0.5f) + 0.4f * oct + 1.2f * m_swellE);
            q = 0.6f;
            break;
        case Glass:
            cutoff = 2400.0f * std::exp2(0.6f + 2.0f * height + 1.2f * (press - 0.5f) + 0.6f * oct + 0.8f * m_swellE);
            q = 0.7f;
            break;
        case Air:
            cutoff = 900.0f * std::exp2(0.8f + 3.0f * height + 1.2f * (press - 0.5f) + oct -
                                        1.6f * smoothstep(0.55f, 1.0f, colour));
            q = 0.9f;
            break;
        }
        cutoff = clampf(cutoff, 80.0f, 15000.0f);
        v.lpL.set(cutoff, q, m_rate);
        copyCoefficients(v.lpR, v.lpL);

        // The choir sings through three formants, the vowel set by the
        // colour and drifting a little with the breath.
        float fg[3] = {}, body = 1.1f;
        if (v.family == Choir) {
            const float vv = clampf(colour * 4.0f + 0.25f * b1 * (0.4f + evolve), 0.0f, 4.0f);
            const int a = std::min(3, int(vv));
            const float f = vv - float(a);
            for (int i = 0; i < 3; ++i) {
                const float hz = std::exp(lerpf(std::log(kVowel[a][i][0]), std::log(kVowel[a + 1][i][0]), f));
                fg[i] = lerpf(kVowel[a][i][1], kVowel[a + 1][i][1], f);
                v.fL[i].set(hz, hz / kVowelBw[i], m_rate);
                copyCoefficients(v.fR[i], v.fL[i]);
            }
            body = lerpf(kBodyGain[a], kBodyGain[a + 1], f);
            v.bodyL.set(std::exp(lerpf(std::log(kBodyHz[a]), std::log(kBodyHz[a + 1]), f)) * std::exp2(2.5f * m_swellE), 0.6f, m_rate);
            copyCoefficients(v.bodyR, v.bodyL);
        }

        // Glass twinkles on the sixteenths, and air's raindrops at random:
        // short sines on chord tones above the layer.
        const float pressGain = 0.35f + 0.85f * press;
        if (!v.releasing || v.env > 0.05f) {
            if (v.family == Glass) {
                const float tw = smoothstep(0.2f, 0.85f, colour);
                const double s0 = tr.beat * 4.0, s1 = tr.beatAt(n) * 4.0;
                if (tw > 0.01f && std::floor(s1) > std::floor(s0)) {
                    const float prob = tw * (0.12f + 0.45f * press);
                    if (v.rng.unit() < prob) {
                        float up[16];
                        const int nu = bedPitches(m_bed, h, false, P + 12.0f, P + 12.0f + fs.span, up, 16);
                        if (nu > 0) {
                            const float m = up[v.rng.next() % uint32_t(nu)];
                            const int at = tr.offsetOf(std::floor(s1) / 4.0, n);
                            spawnGlint(midiToHz(m), 0.05f * pressGain * v.env, 0.35f + 0.6f * v.rng.unit(), 0,
                                       v.rng.bipolar() * 0.8f, std::max(0, at), xOf(Glass, colour) + 0.03f * v.rng.bipolar(),
                                       clampf(height + 0.15f + 0.2f * v.rng.unit(), 0, 1));
                        }
                    }
                }
            } else if (v.family == Air) {
                const float rain = smoothstep(0.2f, 0.45f, colour) * (1.0f - smoothstep(0.6f, 0.85f, colour));
                if (rain > 0.01f) {
                    v.dropClock += dt * rain * (2.0f + 18.0f * press);
                    while (v.dropClock > 1.0f) {
                        v.dropClock -= 0.4f + 1.2f * v.rng.unit();
                        float up[16];
                        const int nu = bedPitches(m_bed, h, false, 84.0f + 6.0f * height, 98.0f + 6.0f * height, up, 16);
                        if (nu > 0) {
                            const float m = up[v.rng.next() % uint32_t(nu)];
                            spawnGlint(midiToHz(m), 0.035f * pressGain * v.env * (0.4f + 0.6f * v.rng.unit()),
                                       0.02f + 0.04f * v.rng.unit(), 0.3f, v.rng.bipolar() * 0.9f, int(v.rng.unit() * float(n)),
                                       xOf(Air, colour) + 0.04f * v.rng.bipolar(), clampf(height + 0.3f * v.rng.bipolar(), 0, 1));
                        }
                    }
                }
            }
        }

        // Noise beds for air: rain hiss, vinyl crackle and hiss.
        const bool air = v.family == Air;
        const float rainAmt = air ? smoothstep(0.2f, 0.45f, colour) * (1.0f - smoothstep(0.6f, 0.85f, colour)) : 0.0f;
        const float vinylAmt = air ? smoothstep(0.55f, 0.9f, colour) : 0.0f;
        const float windAmt = air ? 1.0f - 0.65f * smoothstep(0.2f, 0.5f, colour) : 1.0f;
        if (rainAmt > 0.001f) {
            v.rainL.set(2200.0f * std::exp2(1.4f * height), 0.55f, m_rate);
            copyCoefficients(v.rainR, v.rainL);
        }
        const float crackleRate = vinylAmt * (6.0f + 40.0f * press) / m_rate;

        // A Dub pad is played as stabs: the chord struck on the offbeat
        // eighths (twisted anticlockwise, a 3-3-2 roll that never lands on
        // the beat),
        // each one decaying, long toward the wash and short toward the stab,
        // and the echo fills the gaps. Its level is made up for the gaps:
        // the stab's mean square over the time between hits, worked out
        // rather than measured, so it holds from the first hit, and capped,
        // so a short stab stays a stab rather than a spike.
        const bool stabbing = v.style == Dub && v.family == Pad;
        // Tape's pad is felt keys: struck as the layer lands and on every new
        // chord, a hammer (2.6 times the held level, and an octave and a half
        // brighter, the brightness gone in a sixth of a second) falling back
        // over a quarter of a second to the held note under it. Twisted
        // anticlockwise, the hand plays the held chord again on a dotted
        // figure (the first, the "and" of two, the fourth beat), lo-fi
        // chords rather than a pad.
        const bool keys = v.style == Tape && v.family == Pad;
        const bool keyRoll = keys && pulse > 0.3f;
        if (keys && v.chordSeen != m_chordSerial) {
            v.chordSeen = m_chordSerial;
            v.stabRising = true;
            v.hammer = 1;
        }
        if (keys)
            v.hammer *= std::exp(-dt / 0.15f);
        const float strikeRise = 1.0f - std::exp(-1.0f / (0.003f * m_rate));
        const float strikeFall = std::exp(-1.0f / (0.22f * m_rate));
        uint32_t pattern = 0;
        float stabDecay = 1, stabRise = 1, stabFloor = 1, stabGain = 1;
        if (stabbing) {
            const bool roll = pulse > 0.3f;
            pattern = roll ? 0xA4A4u : 0x4444u; // sixteenths 2 5 7 10 13 15, or 2 6 10 14
            const float tau = lerpf(0.25f, 0.04f, colour) * (0.75f + 0.5f * press);
            stabDecay = std::exp(-1.0f / (tau * m_rate));
            stabRise = 1.0f - std::exp(-1.0f / (0.0025f * m_rate));
            stabFloor = 0.22f * (1.0f - smoothstep(0.0f, 0.5f, colour));
            const float T = float(tr.secondsPerBeat()) * (roll ? 1.0f / 3.0f : 1.0f), x = tau / T;
            const float e1 = x * (1.0f - std::exp(-1.0f / x)), e2 = 0.5f * x * (1.0f - std::exp(-2.0f / x));
            const float f = stabFloor;
            const float ms = f * f + 2.0f * f * (1.0f - f) * e1 + (1.0f - f) * (1.0f - f) * e2;
            stabGain = std::min(2.8f, 0.9f / std::sqrt(std::max(ms, 1e-3f)));
        }
        // What the style takes from the voice: Neon nearly all of it, into
        // the ensemble, beside a little less of the dry (the drone less: its
        // few low partials would wobble in level in the moving comb); Dub a
        // send by layer, most from the stabs and none from the drone, which
        // would only muddy the echoes.
        static const float kNeonSend[kFamilies] = {0.3f, 0.85f, 0.85f, 0.85f, 0.85f};
        static const float kDubSend[kFamilies] = {0.0f, 0.75f, 0.35f, 0.5f, 0.2f};
        // Tape takes everything and leaves no dry: the whole layer is on tape.
        // City pop's chorus takes as Neon's does. Raga's sympathetic strings
        // hear every layer, the drone and the voices most. Mbira's gourd is
        // round the tines; the rest only brush it. Khoomei's valley echoes
        // the singer and the voices most, the drone hardly (its echo would
        // only blur the floor), the wind not at all.
        static const float kRagaSend[kFamilies] = {0.15f, 0.8f, 0.5f, 0.3f, 0.2f};
        static const float kValleySend[kFamilies] = {0.05f, 0.6f, 0.45f, 0.35f, 0.0f};
        float dryAmt = 1.0f, sendAmt = 0.0f;
        switch (v.style) {
        case Neon: dryAmt = v.family != Drone ? 0.9f : 1.0f, sendAmt = kNeonSend[v.family]; break;
        case Dub: sendAmt = kDubSend[v.family]; break;
        case Tape: dryAmt = 0.0f, sendAmt = 1.0f; break;
        case Raga: sendAmt = kRagaSend[v.family]; break;
        case Mbira: sendAmt = 0.12f; break;
        case CityPop: dryAmt = v.family != Drone ? 0.9f : 1.0f, sendAmt = kNeonSend[v.family]; break;
        case Khoomei: sendAmt = kValleySend[v.family]; break;
        default: break;
        }

        // Pulse: a gate in time, eighths at a little twist, sixteenths at more.
        // (A stab's pulse is its pattern instead.)
        const float gateDepth = stabbing || keyRoll ? 0.0f : smoothstep(0.12f, 0.45f, pulse);
        const double div = pulse > 0.55f ? 0.25 : 0.5;
        const float gk = 1.0f - std::exp(-1.0f / (0.004f * m_rate));
        const float attInc = 1.0f / (v.attack * m_rate);
        const float level = fs.level * pressGain * breath;
        const float agc0 = v.agc;
        float timbreSq = 0;
        (void)w2;
        const float pan = 0.12f * b1 * v.side;
        const float panL = 1.0f - pan, panR = 1.0f + pan;
        for (int i = 0; i < n; ++i) {
            // Envelope: a smooth swell to full, then a long fade once let go.
            // A finger lifted mid-swell lets the swell finish first, so a
            // tap is a whole breath.
            if (v.holdEnv) {
                // frozen mid-fade: held where it was
            } else if (!v.releasing || v.ramp < 1.0f) {
                v.ramp = std::min(1.0f, v.ramp + attInc);
                v.env = v.ramp * v.ramp * (3.0f - 2.0f * v.ramp);
            } else {
                v.env *= v.relCoef;
            }
            float gate = 1.0f;
            if (gateDepth > 0.001f) {
                const double b = tr.beatAt(i) / div;
                const float ph = float(b - std::floor(b));
                v.gateSmooth += ((ph < 0.55f ? 1.0f : 0.0f) - v.gateSmooth) * gk;
                gate = 1.0f - gateDepth * (1.0f - v.gateSmooth);
            }
            float L, R;
            const Svf::Out oL = v.lpL.step(sL[i]), oR = v.lpR.step(sR[i]);
            if (v.family == Choir) {
                // Breath noise through the same formants keeps it airy.
                const float nl = 0.02f * v.rng.bipolar(), nr = 0.02f * v.rng.bipolar();
                L = body * v.bodyL.step(oL.low).low;
                R = body * v.bodyR.step(oR.low).low;
                for (int f = 0; f < 3; ++f) {
                    L += fg[f] * v.fL[f].k * v.fL[f].step(sL[i] + nl).band;
                    R += fg[f] * v.fR[f].k * v.fR[f].step(sR[i] + nr).band;
                }
                L *= 2.2f;
                R *= 2.2f;
            } else {
                L = oL.low * windAmt;
                R = oR.low * windAmt;
            }
            if (air) {
                if (rainAmt > 0.001f) {
                    L += 0.3f * rainAmt * v.rainL.step(v.rng.bipolar()).band;
                    R += 0.3f * rainAmt * v.rainR.step(v.rng.bipolar()).band;
                }
                if (vinylAmt > 0.001f) {
                    float ck = 0, bal = 0.5f;
                    if (v.rng.unit() < crackleRate) {
                        const float u = v.rng.unit();
                        ck = (v.rng.next() & 1 ? 1.0f : -1.0f) * u * u * u * 0.9f;
                        bal = 0.5f + 0.4f * v.rng.bipolar();
                    }
                    const float c = v.crackle.step(ck).band * 1.2f + ck * 0.25f;
                    L += vinylAmt * (c * (1.0f - bal) * 2.0f + 0.03f * v.hissL.step(v.rng.bipolar()));
                    R += vinylAmt * (c * bal * 2.0f + 0.03f * v.hissR.step(v.rng.bipolar()));
                }
            }
            timbreSq += L * L + R * R;
            float sg = 1.0f;
            if (stabbing) {
                const int64_t step = int64_t(std::floor(tr.beatAt(i) * 4.0));
                if (step != v.stabStep) {
                    v.stabStep = step;
                    if ((pattern >> uint32_t(step & 15)) & 1u)
                        v.stabRising = true;
                }
                // Up in a few milliseconds from wherever it is (no click on
                // a retrigger), then down.
                if (v.stabRising) {
                    v.stab += (1.05f - v.stab) * stabRise;
                    if (v.stab >= 1.0f) {
                        v.stab = 1.0f;
                        v.stabRising = false;
                    }
                } else {
                    v.stab *= stabDecay;
                }
                sg = stabGain * (stabFloor + (1.0f - stabFloor) * v.stab);
            } else if (keys) {
                if (keyRoll) {
                    const int64_t step = int64_t(std::floor(tr.beatAt(i) * 4.0));
                    if (step != v.stabStep) {
                        v.stabStep = step;
                        if ((0x1041u >> uint32_t(step & 15)) & 1u) { // sixteenths 0, 6, 12
                            v.stabRising = true;
                            v.hammer = 1;
                        }
                    }
                }
                if (v.stabRising) {
                    v.stab += (1.05f - v.stab) * strikeRise;
                    if (v.stab >= 1.0f) {
                        v.stab = 1.0f;
                        v.stabRising = false;
                    }
                } else {
                    v.stab *= strikeFall;
                }
                sg = 2.6f * (0.38f + 0.62f * v.stab);
            }
            const float a = v.env * level * gate * agc0 * sg;
            const float mid = 0.5f * (L + R) * a, side = 0.5f * (L - R) * a * width;
            const float yl = (mid + side) * panL, yr = (mid - side) * panR;
            outL[i] += yl * dryAmt;
            outR[i] += yr * dryAmt;
            busL[i] += yl * sendAmt;
            busR[i] += yr * sendAmt;
        }
        // Loudness follows pressure, not colour or height: the layer's own
        // sound (before its envelope, pressure and gate) is held to one
        // level, slowly, so sweeps and vowels change the colour only.
        {
            // (Tape's keys learn their level from the held note only, or the
            // leveller would take each strike's brightness straight back out.)
            const float rms = std::sqrt(timbreSq / float(2 * n));
            if (!keys || v.hammer < 0.05f)
                v.agcRms += (rms - v.agcRms) * (1.0f - std::exp(-dt / 0.3f));
            const float want = clampf(1.0f / std::max(v.agcRms, 1e-3f), 0.15f, 12.0f);
            // Down at once, up slowly: a layer fading in never overshoots.
            v.agc += (want - v.agc) * (1.0f - std::exp(-dt / (want < v.agc ? 0.03f : 0.6f)));
        }
        v.gate = 1.0f - gateDepth * (1.0f - v.gateSmooth);
        v.level = v.env * pressGain * breath *
                  (stabbing ? stabFloor + (1.0f - stabFloor) * v.stab : keys ? 1.0f + 0.5f * v.stab : 1.0f);
        if (v.releasing && v.ramp >= 1.0f && v.env < 1e-4f)
            v.active = false;
    }

    // --- the struck pads ---------------------------------------------------

    // A fresh strike for voice v, delay samples into the block. The quietest
    // is taken over if the pool is full. hand >= 0 damps that hand's last
    // strike, as a gamelan player's other hand stops the bar just struck.
    Strike &newStrike(Voice &v, int hand, int delay) {
        Strike *st = nullptr;
        for (Strike &q : m_strikes)
            if (!q.on) {
                st = &q;
                break;
            }
        if (!st) {
            st = &m_strikes[0];
            for (Strike &q : m_strikes)
                if (q.gain * (std::fabs(q.amp[0]) + q.fmAmp) < st->gain * (std::fabs(st->amp[0]) + st->fmAmp))
                    st = &q;
        }
        const int owner = int(&v - m_voices.data());
        if (hand >= 0) {
            const int last = v.lastStrike[hand];
            if (last >= 0 && m_strikes[size_t(last)].on && m_strikes[size_t(last)].owner == owner &&
                m_strikes[size_t(last)].ownerId == v.id)
                damp(m_strikes[size_t(last)], 0.05f);
            v.lastStrike[hand] = int(st - m_strikes.data());
        }
        *st = Strike{};
        st->on = true;
        st->hand = hand;
        st->owner = owner;
        st->ownerId = v.id;
        st->style = v.style;
        st->delay = delay;
        return *st;
    }
    // Stops a strike within about tau seconds: every partial decays at
    // least that fast from now on.
    void damp(Strike &st, float tau) {
        const float d = std::exp(-1.0f / (tau * m_rate));
        for (int k = 0; k < st.np; ++k)
            st.dec[k] = std::min(st.dec[k], d);
        st.fmDec = std::min(st.fmDec, d);
        st.hissDec = std::min(st.hissDec, d);
        st.clashDec = std::min(st.clashDec, d);
    }
    // Damps every strike of voice v, or with hand >= 0 only that hand's.
    void dampVoice(const Voice &v, float tau, int hand = -2) {
        const int owner = int(&v - m_voices.data());
        for (Strike &st : m_strikes)
            if (st.on && st.owner == owner && st.ownerId == v.id && (hand == -2 || st.hand == hand))
                damp(st, tau);
    }
    static void pan(Strike &st, float p) {
        st.gl = std::sqrt(0.5f * (1.0f - p));
        st.gr = std::sqrt(0.5f * (1.0f + p));
    }
    // Where a strike shows: in the pad's column at its colour, at the
    // height of its note in the column's range.
    static void place(Strike &st, const Voice &v, float midi, Rng &rng) {
        const FamilyShape &fs = kShape[Pad];
        st.x = xOf(Pad, v.cur[1]) + 0.03f * rng.bipolar();
        st.y = clampf((midi - fs.low) / (fs.high - fs.low), 0.02f, 0.98f);
    }

    // The five tones of the key a gamelan plays, as scale degrees from the
    // tonic: a pelog, five notes with two half steps and two wide thirds in
    // them (selisir's shape, 1 2 4 1 4 semitones, or its mirror, 2 1 4 1 4),
    // the five of the key that hold the tonic; failing that (a pentatonic
    // key, or one stranger) a slendro-like five, the key's own pentatonic;
    // failing that, its first five. In A minor that is A B C E F, in C
    // major C E F G B: the half steps are what make it sound of Bali.
    static int gamelanDegrees(const Tuning &t, int *out) {
        const int per = t.degreesPerOctave();
        bool in[12] = {};
        int degreeAt[12] = {};
        for (int d = 0; d < per; ++d) {
            const int pc = ((int(std::lround(t.degreeSemis(d))) % 12) + 12) % 12;
            in[pc] = true;
            degreeAt[pc] = d;
        }
        static const int kShapes[3][5] = {{1, 2, 4, 1, 4}, {2, 1, 4, 1, 4}, {2, 2, 3, 2, 3}};
        for (const auto &shape : kShapes)
            for (int rot = 0; rot < 5; ++rot) {
                int pc = 0, k = 0;
                for (; k < 5 && in[pc]; ++k) {
                    out[k] = degreeAt[pc];
                    pc += shape[(rot + k) % 5];
                }
                if (k == 5)
                    return 5;
            }
        const int n = std::min(per, 5);
        for (int k = 0; k < n; ++k)
            out[k] = k;
        return n;
    }

    // City pop's chord as a keyboard player voices it: no root (the bass
    // has it), the third and the seventh at the bottom, the ninth over them
    // (a flat ninth is left out), then the eleventh on a minor chord or the
    // sharp eleventh on a major one (a plain fourth over a major third
    // rubs, so the thirteenth instead), and the fifth on top: five notes over
    // about an octave and a half, from the first third at or over P.
    int epVoicing(const Harmony &h, float P, float *out) const {
        const int per = h.tuning.degreesPerOctave(), root = m_bed.root;
        const double r = h.tuning.degreeSemis(root);
        auto find = [&](std::initializer_list<double> wants) {
            for (double want : wants)
                for (int d = 1; d < per; ++d) {
                    const double iv = h.tuning.degreeSemis(root + d) - r;
                    if (std::fabs(iv - want) < 0.35)
                        return iv;
                }
            return -1.0;
        };
        double third = find({4, 3});
        if (third < 0)
            third = find({5, 2}); // a sus chord: the fourth leans where the third would be
        if (third < 0)
            return 0;
        const bool minor = third < 3.5;
        double tones[5];
        int n = 0;
        tones[n++] = third;
        if (const double s = find({11, 10}); s >= 0)
            tones[n++] = s;
        if (const double s = find({2}); s >= 0)
            tones[n++] = 12 + s;
        if (const double s = minor ? find({5, 6}) : find({6}); s >= 0)
            tones[n++] = 12 + s;
        else if (const double s13 = find({9}); s13 >= 0)
            tones[n++] = 12 + s13;
        if (const double s = find({7, 6, 8}); s >= 0)
            tones[n++] = 12 + s;
        for (int i = 1; i < n; ++i)
            for (int j = i; j > 0 && tones[j - 1] > tones[j]; --j)
                std::swap(tones[j - 1], tones[j]);
        // The root's pitch, placed so the third is the first at or over P.
        double R = h.midi(root, 0);
        while (R + third < P)
            R += 12;
        while (R + third >= P + 12)
            R -= 12;
        for (int k = 0; k < n; ++k)
            out[k] = float(R + tones[k]);
        return n;
    }

    // A struck pad's voice: its envelope, then its figure, struck into the
    // pool (Gamelan, Mbira, City pop) or plucked on its tanpura (Raga).
    // cand holds the chord's tones in the voicing window, ascending.
    void renderStruck(Voice &v, const float *cand, int nc, float P, float colour, float *outL, float *outR, float *busL,
                      float *busR, int n, const Harmony &h, const Transport &tr) {
        const FamilyShape &fs = kShape[Pad];
        const float dt = float(n) / m_rate;
        const float press = v.cur[2], motion = v.cur[3], pulse = std::max(0.0f, -motion);
        v.envPrev = v.env;
        const float attInc = 1.0f / (v.attack * m_rate);
        for (int i = 0; i < n; ++i) {
            if (v.holdEnv) {
            } else if (!v.releasing || v.ramp < 1.0f) {
                v.ramp = std::min(1.0f, v.ramp + attInc);
                v.env = v.ramp * v.ramp * (3.0f - 2.0f * v.ramp);
            } else {
                v.env *= v.relCoef;
            }
        }
        // The notes the hands have: the chord's tones well inside the
        // window, low to high.
        float notes[16];
        int nn = 0;
        for (int c = 0; c < nc; ++c)
            if (smoothstep(0.0f, 2.5f, cand[c] - P) * smoothstep(0.0f, 2.5f, P + fs.span - cand[c]) > 0.3f)
                notes[nn++] = cand[c];
        auto note = [&](int i) { return notes[std::clamp(i, 0, nn - 1)]; };
        const float pressGain = 0.35f + 0.85f * press;
        const float level = fs.level * pressGain;
        const bool playing = !v.releasing || v.env > 0.05f;
        const bool newChord = v.chordSeen != m_chordSerial;
        v.chordSeen = m_chordSerial;
        v.hammer *= std::exp(-dt / 0.2f);
        // The step of a figure with `per` steps a beat, if one begins in
        // this block, and where in it.
        auto stepIn = [&](int per, int64_t &step, int &at) {
            const double b0 = tr.beat * per, b1 = tr.beatAt(n) * per;
            if (std::floor(b1) <= std::floor(b0))
                return false;
            step = int64_t(std::floor(b1));
            at = std::clamp(tr.offsetOf(double(step) / per, n), 0, n - 1);
            return true;
        };
        int64_t step = 0;
        int at = 0;

        switch (v.style) {
        case Gamelan: {
            // The bronze plays the key's pelog five, not the chord: the
            // chord only moves the pokok, the core melody, to the five's
            // tone nearest its root. The pokok walks a sixteen-beat
            // contour, one gongan: home on the gong, out and back by the
            // half (the kempur) and home again. Everything above it
            // elaborates where it is going, not where it is, the way
            // Balinese figuration anticipates: norot alternates the coming
            // pokok tone with the key above it in eighths, the sangsih
            // doubling the beat; kotekan, sixteenths in two parts, is a
            // turn into the coming tone (from under it as the pokok
            // rises, from over it as it falls, round it as it stays),
            // split between the parts the way kotekan is: the polos on
            // the beat and the third sixteenth, on the coming tone and
            // under it, the sangsih on the second and the fourth, on it
            // and over it. Each part strikes every other sixteenth and
            // never two in a row, so neither is a line alone and only
            // together are they unbroken. Twisted anticlockwise, a level
            // busier, and at kotekan the ceng-ceng join; twisted past
            // three quarters, the ensemble plays an angsel once: from the
            // next beat, two accents together (on it and on its last
            // sixteenth), then every hand stops its bronze, the cymbals
            // and the kempli are choked, a hand is laid on the gong, and
            // a whole beat of silence; all of them come back together on
            // the beat after. The colotomy: the kempli on every beat, the
            // kemong at the quarters, the kempur at the half, the great
            // gong at the end of every gongan.
            int per = colour < 0.33f ? 1 : colour < 0.66f ? 2 : 4;
            const bool ceng = per == 4 && pulse > 0.3f;
            if (pulse > 0.3f)
                per = std::min(4, per * 2);
            int deg[5];
            const int nd = gamelanDegrees(h.tuning, deg);
            const int perOct = h.tuning.degreesPerOctave();
            float bronze[16];
            int nb = 0;
            for (int k = -2; k <= 6 && nb < 16; ++k)
                for (int i = 0; i < nd && nb < 16; ++i) {
                    const float m = float(h.midi(deg[i] + perOct * k, 0));
                    if (m >= P - 0.01f && m <= P + fs.span)
                        bronze[nb++] = m;
                }
            // The pokok's home: the lowest bronze whose tone is nearest the
            // chord's root, low enough that the kotekan's keys round it
            // are there.
            int home = 0;
            {
                const float rootPc = float(h.midi(m_bed.root, 0));
                float best = 99;
                for (int i = 1; i < std::max(2, nb - 4); ++i) {
                    float d = std::fmod(std::fabs(bronze[i] - rootPc), 12.0f);
                    d = std::min(d, 12.0f - d);
                    if (d < best - 0.01f)
                        best = d, home = i;
                }
            }
            auto bar = [&](int i) { return bronze[std::clamp(home + i, 0, nb - 1)]; };
            // The pokok, a tone a beat over the gongan, as keys from home.
            static const int kPokok[16] = {0, 1, 2, 1, 2, 3, 2, 1, 0, 1, 0, -1, 0, 2, 1, 0};
            auto pokok = [&](int64_t beat) { return kPokok[((beat % 16) + 16) % 16]; };
            // The angsel: armed while the twist is under two thirds, set off
            // once as it passes three quarters, from the next beat.
            if (pulse < 0.66f)
                v.angselArmed = true;
            int64_t sixteenth = 0;
            int atSixteenth = 0;
            const bool onSixteenth = stepIn(4, sixteenth, atSixteenth);
            if (v.angselArmed && pulse > 0.75f && nb > 0 && playing) {
                v.angselArmed = false;
                v.angselFrom = (int64_t(std::floor(tr.beatAt(n) * 4.0)) / 4 + 1) * 4;
            }
            // Where in the angsel this block's sixteenth is (0 to 7), or -1.
            const int64_t inAngsel = onSixteenth && v.angselFrom != INT64_MIN ? sixteenth - v.angselFrom : -1;
            // (The silent beat: no figure, no colotomy, until it is over.)
            const bool silent = v.angselFrom != INT64_MIN && tr.beatAt(n) * 4.0 >= double(v.angselFrom + 4) &&
                                tr.beatAt(n) * 4.0 < double(v.angselFrom + 8) && !(inAngsel == 8);
            if (inAngsel >= 0 && inAngsel < 8) {
                if (playing && nb > 0) {
                    const int T = pokok(v.angselFrom / 4 + 2);
                    if (inAngsel == 0 || inAngsel == 3) {
                        const float vel = (0.9f + 0.2f * press) * level;
                        strikeBronze(v, bar(T), vel, 0, atSixteenth, 1.0f, -0.45f);
                        strikeBronze(v, bar(T + 3), 0.92f * vel, 1, atSixteenth, 1.0f, 0.45f);
                        strikeCeng(v, level, atSixteenth);
                    } else if (inAngsel == 4) {
                        // The break: the bronze and the kempli stopped by the
                        // hands, the cymbals choked, and the gong held down to
                        // a fifth of its ring, all within a few tens of
                        // milliseconds (a step would click).
                        const int owner = int(&v - m_voices.data());
                        for (Strike &st : m_strikes) {
                            if (!st.on || st.owner != owner || st.ownerId != v.id)
                                continue;
                            if (st.low) {
                                st.duckFloor = std::min(st.duckFloor, 0.2f);
                                st.duckDec = std::exp(-1.0f / (0.03f * m_rate));
                            } else {
                                damp(st, 0.015f);
                            }
                        }
                    }
                }
            } else if (nb > 0 && playing && !silent && stepIn(per, step, at)) {
                const int64_t beat = step / per;
                const int k = int(step % per);
                const int C = pokok(beat), T = pokok(beat + 1);
                const float accent = k == 0 ? 1.0f : 0.8f;
                // (Coming out of the angsel, together and hard.)
                const float back = inAngsel == 8 ? 1.15f : 1.0f;
                const float vel = back * accent * (0.8f + 0.4f * press) * level;
                if (per == 1) {
                    strikeBronze(v, bar(C), vel, 2, at, 2.6f, 0.0f);
                } else if (per == 2) {
                    if (k == 0) {
                        strikeBronze(v, bar(C), vel, 0, at, 1.6f, -0.45f);
                        strikeBronze(v, bar(C + 1), 0.7f * vel, 1, at, 1.6f, 0.45f);
                    } else {
                        strikeBronze(v, bar(T + 1), vel, 0, at, 1.6f, -0.45f);
                    }
                } else {
                    // The polos on the beat (the pokok arrived) and on the
                    // third sixteenth, the sangsih on the second and the
                    // fourth: into T from under as the pokok rises, from
                    // over as it falls, round it as it stays.
                    static const int kUp[4] = {0, 0, -1, 0}, kDown[4] = {0, 1, 0, 1}, kSame[4] = {0, 1, -1, 0};
                    if (k == 0) {
                        strikeBronze(v, bar(C), vel, 0, at, 1.3f, -0.45f);
                    } else {
                        const int *turn = T > C ? kUp : T < C ? kDown : kSame;
                        if (k == 2)
                            strikeBronze(v, bar(T + turn[k]), vel, 0, at, 1.3f, -0.45f);
                        else
                            strikeBronze(v, bar(T + turn[k]), 0.92f * vel, 1, at, 1.3f, 0.45f);
                    }
                    // The ceng-ceng: a pair of small cymbal sets crashed in an
                    // off-beat ostinato, the busiest the ensemble gets.
                    if (ceng && ((0xEAEAu >> uint32_t(step & 15)) & 1u))
                        strikeCeng(v, (k & 1 ? 0.8f : 1.0f) * level, at);
                }
            }
            if (inAngsel >= 8)
                v.angselFrom = INT64_MIN;
            if (silent)
                m_breakWant = 0.03f;
            // The kempli on every beat: a small damped gong, the one that
            // keeps the ensemble together; the kemong at the gongan's
            // quarters, the kempur at its half.
            {
                const double b0 = tr.beat, b1 = tr.beatAt(n);
                if (nb > 0 && playing && !silent && std::floor(b1) > std::floor(b0)) {
                    const int bat = std::clamp(tr.offsetOf(std::floor(b1), n), 0, n - 1);
                    strikeKempli(v, h, deg, nd, (per == 1 ? 0.6f : 1.0f) * level, bat);
                    const int64_t beat = int64_t(std::floor(b1)) % 16;
                    if (beat == 8)
                        strikeKempur(v, h, level, bat);
                    else if (beat == 4 || beat == 12)
                        strikeKemong(v, h, deg, nd, level, bat);
                }
            }
            // The gong opens the piece as the layer lands, unless it has
            // just sounded, and closes every four bars. A chord change never
            // stills it: it is the ground the piece stands on.
            const double c0 = tr.beat / (4.0 * kBeatsPerBar), c1 = tr.beatAt(n) / (4.0 * kBeatsPerBar);
            const int64_t cycle = int64_t(std::floor(c1));
            if (v.hit && playing && !silent && cycle != m_gongCycle) {
                m_gongCycle = cycle;
                strikeGong(v, h, level, 0);
            } else if (playing && !silent && std::floor(c1) > std::floor(c0) && cycle != m_gongCycle) {
                m_gongCycle = cycle;
                strikeGong(v, h, level, std::clamp(tr.offsetOf(std::floor(c1) * 4.0 * kBeatsPerBar, n), 0, n - 1));
            }
            v.hit = false;
            break;
        }
        case Mbira: {
            // Twelve-eight: three pulses a beat, and a cycle of forty-eight
            // (four phrases of twelve), with its own harmony as an mbira
            // piece has: twelve chords of four pulses each, so the bass's
            // changes fall across the beat (four against three, the
            // music's drive). They move by the key's steps round the
            // app's chord, Nhemamusasa's way: home twice and a third up,
            // home twice and a fifth up, the sixth twice and a third up,
            // the sixth twice and a fifth up. The kushaura, the lead part:
            // the left thumb on the beat, on the bass tines (the chord's
            // root, fifth and third from G2 up), the right thumb the pulse
            // after, high, singing a tune of its own; the third pulse
            // rests, the lilt. Twisted anticlockwise the kutsinhira joins,
            // a second mbira with a part of its own, one pulse later
            // throughout: its bass on the second pulse and its treble on
            // the third, so the two interlock into an unbroken stream, the
            // bass line between them, and the long bass tines overlap into
            // one cloud.
            if (nn > 0 && playing && stepIn(3, step, at)) {
                static const int kChord[12] = {0, 0, 2, 0, 0, 4, 5, 5, 2, 5, 5, 4};
                const int per = h.tuning.degreesPerOctave();
                // A chord of the cycle at pulse p: its root, fifth and third
                // on the bass tines.
                auto chordAt = [&](int64_t p, float *bass) {
                    const int root = m_bed.root + kChord[((p % 48) + 48) % 48 / 4];
                    const int tones[3] = {root, root + 4, root + 2};
                    for (int k = 0; k < 3; ++k) {
                        float m = float(h.midi(tones[k], 0));
                        while (m >= 55.0f)
                            m -= 12.0f;
                        while (m < 43.0f)
                            m += 12.0f;
                        bass[k] = m;
                    }
                };
                static const int kBassA[16] = {0, 1, 0, 2, 0, 1, 2, 0, 0, 1, 0, 2, 0, 2, 1, 0};
                static const int kBassB[16] = {2, 0, 1, 0, 2, 0, 1, 1, 2, 0, 1, 0, 1, 0, 2, 0};
                // The right thumbs' tunes, as steps of the key from home (7
                // is home an octave up), a note a beat: each twelve-pulse
                // phrase falls, by step and by neighbour tone, landing on
                // a tone of the chord under it as the cycle's harmony
                // moves (a third of the chord, a step over it and back, the
                // root, the fifth of the next). The kushaura's sings high
                // and comes down a sixth over the cycle; the kutsinhira's
                // sits a fourth or so under it, its own tune, never the
                // first's an octave down.
                static const int kTuneA[16] = {9, 8, 7, 6, 9, 10, 9, 8, 9, 7, 8, 6, 7, 6, 5, 4};
                static const int kTuneB[16] = {4, 5, 4, 2, 4, 3, 2, 1, 5, 6, 4, 2, 5, 4, 3, 1};
                // (The tunes sit so the kushaura's middle is a little over
                // the middle of the pad's window.)
                int oct = 0;
                {
                    const float aim = P + 0.6f * fs.span;
                    float best = 1e9f;
                    for (int o = -3; o <= 4; ++o) {
                        const float d = std::fabs(float(h.midi(m_bed.root + 7 + per * o, 0)) - aim);
                        if (d < best)
                            best = d, oct = o;
                    }
                }
                auto tune = [&](int stepOf) { return float(h.midi(m_bed.root + stepOf + per * oct, 0)); };
                const int pos = int(step % 3), beat = int((step / 3) & 15);
                float bass[3];
                chordAt(step, bass);
                // (With the kutsinhira in, two instruments' long bass tines
                // overlap: each is played softer, so the pair sits as loud
                // as one.)
                const float pv = (0.9f + 0.2f * press) * level * (1.0f - 0.27f * smoothstep(0.2f, 0.4f, pulse));
                auto jitter = [&] { return 0.9f + 0.2f * v.rng.unit(); };
                if (pos == 0)
                    strikeTine(v, bass[kBassA[beat]], jitter() * pv, colour, at, -0.3f, 0.0f);
                else if (pos == 1)
                    strikeTine(v, tune(kTuneA[beat]), 0.72f * jitter() * pv, colour, at, -0.1f, 0.0f);
                if (pulse > 0.3f) {
                    // (The second instrument is never quite in tune with the
                    // first: four cents apart, and further right.)
                    if (pos == 1)
                        strikeTine(v, bass[kBassB[beat]], 0.75f * jitter() * pv, colour, at, 0.45f, 0.04f);
                    else if (pos == 2)
                        strikeTine(v, tune(kTuneB[beat]), 0.6f * jitter() * pv, colour, at, 0.55f, 0.04f);
                }
            }
            break;
        }
        case CityPop: {
            // The chord, voiced rootless, struck as the layer lands and on
            // every change. Twisted anticlockwise the band comes in: the
            // piano comps a two-bar figure that pushes (the "and" of two
            // and of three, then the bar after on its "a" of one, the "and"
            // of two and a push on the "and" of four that ties over the bar
            // line), the hand lifting a sixteenth before each hit so the
            // chords are short. Under it a fingered bass on the root the
            // voicing leaves out: a two-bar line of sixteenths with ghost
            // notes between the notes, the fifth and the seventh, the
            // octave popped now and then, and a chromatic approach into
            // every bar line, into the coming chord when the player can
            // tell what it will be (it has heard the progression go round
            // once, and the changes come as often as they did) and into the
            // root again when not; a change it did not see coming it
            // slides into from a half step under. And the pocket: a
            // cocktail shaker on swung sixteenths, a rim on two and four.
            float voicing[5];
            const int nv = epVoicing(h, P, voicing);
            bool strike = (v.hit || newChord) && nv > 0 && playing;
            v.hit = false;
            const bool band = pulse > 0.3f;
            if (newChord) {
                // What the bass player hears: the roots in order, and how
                // long each change came after the last, in sixteenths.
                const int64_t now16 = int64_t(std::floor(tr.beat * 4.0));
                if (v.bassLast != INT64_MIN)
                    v.bassPeriod = int(std::clamp<int64_t>(now16 - v.bassLast, 0, 64));
                v.bassLast = now16;
                v.bassSlide = band && v.bassAimed != m_bed.root;
                v.bassAimed = -999;
                if (v.bassN == 0 || v.bassRoots[(v.bassN - 1) & 7] != m_bed.root)
                    v.bassRoots[v.bassN++ & 7] = m_bed.root;
            }
            if (band && nv > 0 && playing && stepIn(4, step, at)) {
                constexpr uint32_t kComp = (1u << 0) | (1u << 6) | (1u << 10) | (1u << 19) | (1u << 22) | (1u << 30);
                const int k = int(step & 31);
                // (The bar after a push is already held over: no hit on its one.)
                strike |= ((kComp >> uint32_t(k)) & 1u) && !(k == 0 && v.figStep != INT64_MIN);
                if ((kComp >> uint32_t((k + 1) & 31)) & 1u)
                    dampVoice(v, 0.04f, 0);
                v.figStep = step;
                // The bass, two bars of sixteenths: R the root, O its octave
                // popped, F the fifth, S the seventh (or the third), G a
                // ghost, A the approach, M a mute (the hand stops the
                // string), and rests.
                enum { R, O, F, S, G, A, M, _ };
                static const int kBass[32] = {R, _, G, R, _, R, O, G, R, _, F, G, S, G, F, A,
                                              R, G, _, R, M, F, O, G, R, _, S, G, F, M, O, A};
                const int code = kBass[k];
                auto bassOf = [&](int degree) {
                    float r = float(h.midi(degree, 0));
                    while (r >= 52.0f)
                        r -= 12.0f;
                    while (r < 40.0f)
                        r += 12.0f;
                    return r;
                };
                const float r = bassOf(m_bed.root);
                auto above = [&](float m) {
                    while (m < r + 0.5f)
                        m += 12.0f;
                    while (m >= r + 12.0f)
                        m -= 12.0f;
                    return m;
                };
                const float drive = (0.85f + 0.3f * press) * level;
                const float accent = k % 4 == 0 ? 1.0f : 0.8f;
                // (The sixteenths between the eighths come late: the swing
                // the bass and the shaker share.)
                const int when = at + ((k & 1) ? int(0.12 * tr.samplesPerBeat() / 4.0) : 0);
                if (code == M) {
                    dampVoice(v, 0.02f, 3);
                } else if (code == A) {
                    // The coming root, if the change is due on the next
                    // sixteenth and the progression has been heard before.
                    int target = m_bed.root;
                    const int64_t next16 = step + 1;
                    if (v.bassPeriod > 0 && v.bassLast != INT64_MIN && next16 - v.bassLast == v.bassPeriod && v.bassN >= 2) {
                        for (int back = 2; back <= std::min(8, v.bassN); ++back)
                            if (v.bassRoots[(v.bassN - back) & 7] == m_bed.root) {
                                target = v.bassRoots[(v.bassN - back + 1) & 7];
                                break;
                            }
                    }
                    v.bassAimed = target;
                    const float t = bassOf(target);
                    strikeBass(v, (k == 15 ? t - 1.0f : t + 1.0f), 0.75f * drive, when, 0);
                } else if (code != _) {
                    float m = r;
                    int kind = 0;
                    if (code == O)
                        m = r + 12.0f, kind = 2;
                    else if (code == G)
                        kind = 1;
                    else if (code == F) {
                        m = r + 7.0f;
                        if (m_bed.fifth)
                            m = above(float(h.midi(m_bed.fifthDegree, 0)));
                    } else if (code == S) {
                        m = r + 10.0f;
                        bool found = false;
                        for (int i = 0; i < m_bed.count && !found; ++i) {
                            const float iv = above(float(h.midi(m_bed.tones[i], 0))) - r;
                            if (iv > 9.5f && iv < 11.5f)
                                m = r + iv, found = true;
                        }
                        if (!found)
                            for (int i = 0; i < m_bed.count; ++i) {
                                const float iv = above(float(h.midi(m_bed.tones[i], 0))) - r;
                                if (iv > 2.5f && iv < 4.5f)
                                    m = r + iv;
                            }
                    }
                    const float vel = kind == 1 ? 0.45f : kind == 2 ? 0.95f : accent;
                    strikeBass(v, m, vel * drive, when, kind, v.bassSlide && code == R);
                    if (code == R)
                        v.bassSlide = false;
                }
                // The pocket: the shaker on every sixteenth, the ones between
                // the eighths late (swung) and soft; the rim on two and four.
                strikeShaker(v, ((k & 1) ? 0.55f : (k & 2) ? 0.8f : 1.0f) * level, when);
                if ((k & 7) == 4)
                    strikeRim(v, (0.9f + 0.2f * press) * level, at);
            } else if (strike) {
                at = 0;
            }
            if (!band)
                v.figStep = INT64_MIN;
            if (strike) {
                dampVoice(v, 0.08f, 0);
                const float hard = 0.35f + 0.65f * press;
                // (With the band the hand comps lighter: the chords come
                // often, and the bass and the pocket fill the rest.)
                const float comp = band ? 0.62f : 0.8f;
                for (int k = 0; k < nv; ++k) {
                    // A hand rolls a chord a little, low to high (and the
                    // barks don't all land on one sample).
                    const int roll = at + k * int(0.006f * m_rate);
                    strikeEP(v, voicing[k], hard, comp * (0.85f + 0.3f * press) * level / std::sqrt(float(nv)) * 1.6f, colour, roll,
                             0.5f * (float(k) / std::max(1, nv - 1) - 0.5f));
                }
            }
            // The Suitcase's stereo tremolo: deepest in the middle of the
            // column, a little left at either end.
            v.tremDepth = 0.12f + 0.6f * smoothstep(0.15f, 0.5f, colour) * (1.0f - 0.6f * smoothstep(0.7f, 1.0f, colour));
            v.trem += 4.7f * dt;
            v.trem -= std::floor(v.trem);
            break;
        }
        case Raga:
            renderTanpura(v, P, colour, pulse, outL, outR, busL, busR, n, h, tr);
            break;
        default:
            break;
        }
        (void)note;
        v.gate = 1;
        v.level = v.env * pressGain * (0.6f + 0.5f * v.hammer);
        if (v.releasing && v.ramp >= 1.0f && v.env < 1e-4f) {
            v.active = false;
            dampVoice(v, 0.01f);
        }
    }

    // A gangsa's key: a bronze bar's first three modes, each twice, the
    // second instrument of the pair tuned 6.5 Hz above the first, so the
    // note beats (ombak, the wave a Balinese ensemble is tuned for); and
    // the hard mallet's tok, the bar's fourth mode gone in 15 ms.
    void strikeBronze(Voice &v, float midi, float vel, int hand, int at, float tau, float p) {
        const float hz = midiToHz(midi);
        Strike &st = newStrike(v, hand, at);
        const float t1 = tau * std::pow(330.0f / hz, 0.3f);
        for (float f : {hz, hz + 6.5f}) {
            st.partial(f, 0.5f, t1, m_rate);
            st.partial(f * 2.76f, 0.16f, 0.35f * t1, m_rate);
            st.partial(f * 5.40f, 0.06f, 0.08f, m_rate);
        }
        // (Its seventh partial would be the twin's fourth mode: one is enough.)
        st.np = std::min(st.np, Strike::P - 1);
        st.partial(hz * 8.93f, 0.12f, 0.015f, m_rate);
        st.attInc = 1.0f / (0.0015f * m_rate);
        st.gain = vel * kStruckGain[Gamelan];
        pan(st, p);
        place(st, v, midi, v.rng);
        v.hammer = 1;
    }
    // The gong ageng, on the key's tonic in the lowest octave there is
    // (about 40 to 70 Hz): a padded beater, so the hum blooms rather than
    // starts; the hum's octave, where the gong's slow beating lives (two
    // modes eight tenths of a hertz apart: heard, but up where it is a
    // shimmer in the bronze's low end rather than a throb in the floor);
    // an inharmonic shell mode, and the pitch sagging about sixty cents
    // back down to the note over the first seconds, as a great gong's does
    // when the hammered bronze is set swinging hard. It sits under the
    // bronze, the ground of the piece, not over it: its hum is gone (sixty
    // decibels) in about eight seconds, so the floor clears between gongs,
    // and it goes through the gongs' high-pass.
    void strikeGong(Voice &v, const Harmony &h, float level, int at) {
        float m = float(h.midi(0, 0));
        m = 27.0f + std::fmod(std::fmod(m - 27.0f, 12.0f) + 12.0f, 12.0f);
        const float hz = midiToHz(m);
        Strike &st = newStrike(v, -1, at);
        st.low = true;
        st.partial(hz, 0.75f, 2.8f, m_rate);
        st.partial(hz, -0.75f, 0.45f, m_rate);
        st.partial(2.02f * hz, 0.4f, 2.6f, m_rate);
        st.partial(2.02f * hz + 0.8f, 0.3f, 2.2f, m_rate);
        st.partial(2.93f * hz, 0.14f, 1.2f, m_rate);
        st.partial(4.6f * hz, 0.08f, 0.12f, m_rate);
        st.bend = 0.035f;
        st.bendDec = std::exp(-1.0f / (1.1f * m_rate));
        st.attInc = 1.0f / (0.01f * m_rate);
        st.gain = level * kStruckGain[Gamelan] * 0.85f;
        pan(st, 0);
        place(st, v, kShape[Pad].low + 0.5f, v.rng);
    }
    // The kempur: the middle gong, an octave over the great one, its hum
    // blooming a little and sagging a little, ringing a couple of seconds.
    void strikeKempur(Voice &v, const Harmony &h, float level, int at) {
        float m = float(h.midi(0, 0));
        m = 39.0f + std::fmod(std::fmod(m - 39.0f, 12.0f) + 12.0f, 12.0f);
        const float hz = midiToHz(m);
        Strike &st = newStrike(v, -1, at);
        st.low = true;
        st.partial(hz, 0.6f, 2.4f, m_rate);
        st.partial(hz, -0.6f, 0.15f, m_rate);
        st.partial(2.0f * hz + 0.6f, 0.3f, 1.6f, m_rate);
        st.partial(2.76f * hz, 0.12f, 0.6f, m_rate);
        st.partial(5.1f * hz, 0.05f, 0.08f, m_rate);
        st.bend = 0.02f;
        st.bendDec = std::exp(-1.0f / (0.6f * m_rate));
        st.attInc = 1.0f / (0.006f * m_rate);
        st.gain = level * kStruckGain[Gamelan] * 0.55f;
        pan(st, -0.1f);
        place(st, v, kShape[Pad].low + 2.0f, v.rng);
    }
    // The kemong: a small hanging gong on the five's tone nearest E4, the
    // pair beating fast as the bronze does, gone in about a second.
    void strikeKemong(Voice &v, const Harmony &h, const int *deg, int nd, float level, int at) {
        float m = 64.0f, best = 99.0f;
        const int perOct = h.tuning.degreesPerOctave();
        for (int k = 0; k <= 4; ++k)
            for (int i = 0; i < nd; ++i) {
                const float c = float(h.midi(deg[i] + perOct * k, 0));
                if (std::fabs(c - 64.0f) < best)
                    best = std::fabs(c - 64.0f), m = c;
            }
        const float hz = midiToHz(m);
        Strike &st = newStrike(v, -1, at);
        st.partial(hz, 0.4f, 0.9f, m_rate);
        st.partial(hz + 3.5f, 0.3f, 0.8f, m_rate);
        st.partial(hz, -0.7f, 0.03f, m_rate);
        st.partial(2.32f * hz, 0.12f, 0.35f, m_rate);
        st.partial(4.1f * hz, 0.05f, 0.08f, m_rate);
        st.attInc = 1.0f / (0.003f * m_rate);
        st.gain = level * kStruckGain[Gamelan] * 0.5f;
        pan(st, 0.2f);
        place(st, v, kShape[Pad].low + 6.0f, v.rng);
    }
    // The kempli: a small kettle gong held damped, a short tuk on the
    // five's tone nearest the top of the treble staff.
    void strikeKempli(Voice &v, const Harmony &h, const int *deg, int nd, float level, int at) {
        float m = 74.0f, best = 99.0f;
        const int perOct = h.tuning.degreesPerOctave();
        for (int k = 1; k <= 4; ++k)
            for (int i = 0; i < nd; ++i) {
                const float c = float(h.midi(deg[i] + perOct * k, 0));
                if (std::fabs(c - 72.0f) < best)
                    best = std::fabs(c - 72.0f), m = c;
            }
        const float hz = midiToHz(m);
        Strike &st = newStrike(v, -1, at);
        st.partial(hz, 0.5f, 0.07f, m_rate);
        st.partial(hz * 2.32f, 0.2f, 0.035f, m_rate);
        st.partial(hz * 4.1f, 0.12f, 0.012f, m_rate);
        st.attInc = 1.0f / (0.001f * m_rate);
        st.gain = level * kStruckGain[Gamelan] * 0.55f;
        pan(st, 0.15f);
        place(st, v, kShape[Pad].high - 1.0f, v.rng);
    }
    // The ceng-ceng: small cymbals clashed together. A clash (a millisecond
    // and a half of noise through a broad band round 6 kHz), then the ring:
    // the cymbals' own modes, between 4 and 9 kHz, and a narrower hiss in the
    // same place, until the player's hands choke it a tenth of a second on.
    void strikeCeng(Voice &v, float level, int at) {
        Strike &st = newStrike(v, -1, at);
        static const float kModes[4][3] = {{4210, 0.05f, 0.09f}, {5370, 0.05f, 0.07f}, {6830, 0.04f, 0.06f}, {8090, 0.03f, 0.05f}};
        const float spread = 1.0f + 0.02f * v.rng.bipolar();
        for (const auto &md : kModes)
            st.partial(md[0] * spread, md[1], md[2], m_rate);
        st.clash = 0.9f;
        st.clashDec = std::exp(-1.0f / (0.0015f * m_rate));
        st.hiss = 0.5f;
        st.hissDec = std::exp(-1.0f / (0.06f * m_rate));
        st.hissF.set(6000.0f, 0.9f, m_rate);
        st.hissF2.set(6200.0f * spread, 1.6f, m_rate);
        st.choke = int((0.09f + 0.03f * v.rng.unit()) * m_rate);
        st.attInc = 1.0f / (0.0005f * m_rate);
        st.gain = level * kStruckGain[Gamelan] * 0.8f;
        pan(st, 0.3f * v.rng.bipolar());
        place(st, v, kShape[Pad].high - 0.5f, v.rng);
    }
    // An mbira's tine, plucked by a thumb: its fundamental, the clamped
    // bar's second mode six and a quarter times above it and a third
    // fleeting one, and the rattle. The wide bass tines bloom and sing on,
    // four and a half seconds to silence at G2, so the notes of both parts
    // overlap into a cloud; the narrow top ones are gone in about one.
    // Toward the rattling end of the column the caps lie closer to the
    // tine, so they chatter for more of its swing.
    // detune: cents the second mbira sits from the first.
    void strikeTine(Voice &v, float midi, float vel, float colour, int at, float p, float detune) {
        const float hz = midiToHz(midi + detune);
        Strike &st = newStrike(v, -1, at);
        const float t60 = clampf(4.6f * std::pow(98.0f / hz, 0.75f), 0.9f, 5.0f);
        st.partial(hz, 1.0f, t60 / 6.9f, m_rate);
        st.partial(hz * 6.27f, 0.24f + 0.14f * colour, 0.05f, m_rate);
        st.partial(hz * 17.55f, 0.07f, 0.01f, m_rate);
        const float rattle = smoothstep(0.2f, 1.0f, colour);
        st.buzz = 2.4f * rattle;
        st.capAt = 0.6f - 0.4f * rattle;
        if (rattle > 0) {
            // The caps, loose on their wire, never sit the same way twice:
            // each strike finds them tuned a little differently (up to
            // fifteen percent either way of where they usually are), and
            // broad (a Q of four to eight), so they sizzle rather than sing.
            static const float kCapHz[Strike::kCaps] = {2100, 2900, 3800, 5000, 6600, 8600};
            for (int k = 0; k < Strike::kCaps; ++k)
                st.cap[k].set(kCapHz[k] * (1.0f + 0.15f * v.rng.bipolar()), 4.0f + 4.0f * v.rng.unit(), m_rate);
            st.scrapeDec = std::exp(-1.0f / (0.00025f * m_rate));
        }
        st.attInc = 1.0f / (0.001f * m_rate);
        // A high tine dies sooner; it is played a little harder, so the
        // instrument is as loud at the top as at the bottom.
        st.gain = vel * kStruckGain[Mbira] * clampf(std::pow(hz / 262.0f, 0.15f), 0.8f, 1.3f);
        pan(st, p);
        place(st, v, midi, v.rng);
        v.hammer = 1;
    }
    // A DX electric piano's key, the way the famous patch is built: two
    // pairs of operators at 1:1 a couple of cents apart, the body (the
    // second softer than the first, so their slow beating moves the tone
    // rather than ever cancelling it), their
    // index falling in a third of a second to what is left of it (brighter
    // toward the bell); and a third carrier whose modulator runs fourteen
    // times as fast, its index high and gone in about 80 ms, the tine's
    // metallic bark on every attack. How hard the key is struck (hard,
    // from the pressure) scales both indexes: a firm finger barks.
    void strikeEP(Voice &v, float midi, float hard, float vel, float colour, int at, float p) {
        const float hz = midiToHz(midi);
        Strike &st = newStrike(v, -1, at);
        st.hand = 0; // (the piano's hand, for its lifts; a chord's notes never damp each other)
        st.fm = true;
        const float detune = std::exp2(1.3f / 1200.0f);
        st.incA = hz * detune / m_rate;
        st.incB = hz / detune / m_rate;
        st.inc = hz / m_rate;
        // (Each key's operators start where they happen to be, so a chord's
        // attacks never pile up into one spike.)
        st.phA = v.rng.unit(), st.phB = v.rng.unit(), st.ph = v.rng.unit();
        st.index = (0.5f + 1.7f * colour) * (0.5f + 0.7f * hard);
        st.indexFloor = 0.08f + 0.42f * colour;
        st.indexDec = std::exp(-1.0f / (0.3f * m_rate));
        // The bark is band-limited, key by key. Its modulator runs fourteen
        // times the note in the low keys, but never over 2.8 kHz (a higher
        // key's bark is a less harmonic ping, as a real tine's is), and its
        // index is held where the sidebands that matter, the index plus one
        // of them either side, stay under 7 kHz. Left free, a mid key's
        // bark at a firm touch reached forty kilohertz and folded back as a
        // sizzle over every chord.
        const float mod = std::min(14.0f * hz, 2800.0f);
        st.incM = mod / m_rate;
        st.phM = v.rng.unit();
        const float barkMax = std::max(0.0f, (7000.0f - hz) / mod - 1.0f);
        st.bark = std::min(barkMax, (1.0f + 1.6f * hard) * (0.7f + 0.5f * colour));
        st.barkDec = std::exp(-1.0f / (0.08f * m_rate));
        // (Toward the bell its index is high enough that it needs less
        // level to be heard, and a chord of five would spike.)
        st.barkAmp = 0.46f * (1.0f - 0.4f * colour);
        st.barkAmpDec = std::exp(-1.0f / (0.5f * m_rate));
        // (And it is heard through a tine's pickup, which is no tweeter: two
        // poles at 5.5 kHz, so what is left over 6 kHz is a glint and not a
        // spit.)
        st.barkLp = 1.0f - std::exp(-kTwoPi * 5500.0f / m_rate);
        st.fmAmp = 1.0f;
        // (A high key dies a little sooner, and is voiced a little louder
        // for it, so a chord held high is as present as one held low.)
        st.fmDec = std::exp(-1.0f / (clampf(4.0f * std::pow(262.0f / hz, 0.2f), 1.5f, 5.0f) * m_rate));
        st.attInc = 1.0f / (0.0015f * m_rate);
        st.gain = vel * kStruckGain[CityPop] * clampf(std::pow(hz / 262.0f, 0.2f), 0.85f, 1.35f);
        pan(st, p);
        place(st, v, midi, v.rng);
        v.hammer = 1;
    }
    // City pop's bass: a fingered electric bass. The string's first six
    // harmonics, the higher ones dying sooner, how many of them there are
    // following how hard it is played (a body low-pass that opens with the
    // velocity); the finger's snap off the string, a few milliseconds of
    // it through the pickup's band; and the pitch a fifth of a semitone
    // sharp at the attack, settling, as a plucked string's does. A ghost
    // note is the finger landing on a muted string: mostly the snap, a
    // breath of the note, gone at once. A popped octave is plucked hard
    // and bright. slide: come into it from a half step under. The next note
    // stops the last; all of it goes through the bass's high-pass, so the
    // attack never thumps under 35 Hz.
    void strikeBass(Voice &v, float midi, float vel, int at, int kind, bool slide = false) {
        const float hz = midiToHz(midi);
        Strike &st = newStrike(v, 3, at);
        const float hard = clampf(vel / std::max(1e-4f, kShape[Pad].level), 0.0f, 1.3f);
        const float open = kind == 2 ? 6.0f : 1.6f + 3.0f * hard; // harmonics the body passes
        const float tau = kind == 1 ? 0.035f : kind == 2 ? 0.45f : 0.9f;
        for (int k = 1; k <= 6; ++k) {
            const float a = (k == 1 ? 1.0f : 0.7f / float(k)) * std::exp(-float(k - 1) / open);
            st.partial(float(k) * hz, (kind == 1 ? 0.25f : 1.0f) * a, tau / (1.0f + 0.7f * float(k - 1)), m_rate);
        }
        st.hiss = kind == 1 ? 0.5f : kind == 2 ? 0.45f : 0.22f;
        st.hissDec = std::exp(-1.0f / (0.004f * m_rate));
        st.hissF.set(kind == 2 ? 3200.0f : 1800.0f, 1.2f, m_rate);
        st.hissF2.set(kind == 2 ? 3200.0f : 1800.0f, 0.8f, m_rate);
        st.bend = slide ? std::exp2(-1.0f / 12.0f) - 1.0f : kind == 1 ? 0.0f : 0.012f;
        st.bendDec = std::exp(-1.0f / ((slide ? 0.035f : 0.03f) * m_rate));
        st.attInc = 1.0f / (0.003f * m_rate);
        st.gain = vel * kStruckGain[CityPop] * 0.42f;
        pan(st, 0);
        place(st, v, kShape[Pad].low + 0.5f, v.rng);
    }
    // The cocktail shaker: a short swish of bright noise, rising in over a
    // few milliseconds (the beads thrown, not struck), a little right.
    void strikeShaker(Voice &v, float level, int at) {
        Strike &st = newStrike(v, -1, at);
        st.hiss = 0.5f;
        st.hissDec = std::exp(-1.0f / (0.03f * m_rate));
        st.hissF.set(6500.0f, 0.7f, m_rate);
        st.hissF2.set(8000.0f, 0.9f, m_rate);
        st.attInc = 1.0f / (0.006f * m_rate);
        st.gain = level * kStruckGain[CityPop] * 0.2f;
        pan(st, 0.35f);
        place(st, v, kShape[Pad].high - 1.0f, v.rng);
    }
    // The rim: a stick laid across the snare and clicked on its rim, the
    // woody knock of the shell and a millisecond's click, a little left.
    void strikeRim(Voice &v, float level, int at) {
        Strike &st = newStrike(v, -1, at);
        st.partial(520.0f, 0.5f, 0.035f, m_rate);
        st.partial(1630.0f, 0.3f, 0.018f, m_rate);
        st.partial(3310.0f, 0.12f, 0.008f, m_rate);
        st.clash = 0.8f;
        st.clashDec = std::exp(-1.0f / (0.001f * m_rate));
        st.hissF.set(2600.0f, 0.7f, m_rate);
        st.attInc = 1.0f / (0.0005f * m_rate);
        st.gain = level * kStruckGain[CityPop] * 0.25f;
        pan(st, -0.25f);
        place(st, v, kShape[Pad].low + 8.0f, v.rng);
    }

    // Raga's tanpura: four strings, the first on Pa (the fifth under Sa),
    // or on Ma (the fourth) in a mode without a perfect fifth, or on Ni (the
    // seventh) with neither; then Sa, Sa again (a hair sharp, as two strings
    // never quite agree) and Sa an octave down, plucked round in that order:
    // an eighth note apart then a breath, in two bars, or twisted
    // anticlockwise in one. Sa is the key's tonic and never moves with the
    // chord (a tanpura's whole purpose is the one Sa the music returns
    // to); the chords reach only the sympathetic strings. A change of
    // height or of key that moves a string leaves it ringing until its
    // pluck retunes it (a new key also starts the round over).
    void renderTanpura(Voice &v, float P, float colour, float pulse, float *outL, float *outR, float *busL, float *busR,
                       int n, const Harmony &h, const Transport &tr) {
        const int per = h.tuning.degreesPerOctave();
        float sa = float(h.midi(0, 0));
        while (sa < P)
            sa += 12.0f;
        while (sa >= P + 12.0f)
            sa -= 12.0f;
        float first = sa - 5.0f;
        {
            bool found = false;
            for (double want : {7.0, 5.0, 11.0, 10.0}) {
                for (int d = 1; d < per && !found; ++d)
                    if (std::fabs(h.tuning.degreeSemis(d) - want) < 0.35) {
                        first = sa - 12.0f + float(h.tuning.degreeSemis(d));
                        found = true;
                    }
                if (found)
                    break;
            }
        }
        const float want[4] = {first, sa, sa + 0.015f, sa - 12.0f};
        if (v.cycleFrom != INT64_MIN && v.cycleFrom != INT64_MAX) {
            // A string whose note has moved rings on where it was until its
            // own pluck in the round retunes it, so the drone never has a
            // hole in it. Moved by an octave (the height), it is the same
            // Sa or Pa, and the round goes on. Moved to another note (a new
            // key), it rings out sooner (gone in about a second and a
            // half), and the round starts over at the next eighth so the
            // new Sa comes at once; the strings still ringing the old one
            // fade one by one as the new one is plucked round, never all
            // together. (A string already let go waits for its pluck: it
            // never starts the round over again.)
            bool moved = false;
            for (int k = 0; k < 4; ++k) {
                TanString &t = v.str[k];
                if (t.midi <= 0 || t.stilled || std::fabs(t.midi - want[k]) <= 0.01f)
                    continue;
                const float d = std::fabs(t.midi - want[k]);
                if (std::fabs(d - 12.0f * std::round(d / 12.0f)) > 0.05f) {
                    t.loss = std::pow(10.0f, -3.0f / (1.5f * midiToHz(t.midi)));
                    t.stilled = true;
                    moved = true;
                }
            }
            if (moved)
                v.cycleFrom = INT64_MAX;
        }
        const bool fast = pulse > 0.3f;
        const int cycle = fast ? 8 : 16, gap = fast ? 2 : 3;
        const double e0 = tr.beat * 2.0, e1 = tr.beatAt(n) * 2.0;
        const bool playing = !v.releasing || v.env > 0.05f;
        if (playing && (v.cycleFrom == INT64_MIN || std::floor(e1) > std::floor(e0))) {
            const int64_t e = int64_t(std::floor(e1));
            if (v.cycleFrom == INT64_MIN || v.cycleFrom == INT64_MAX)
                v.cycleFrom = e;
            const int64_t k = ((e - v.cycleFrom) % cycle + cycle) % cycle;
            if (k % gap == 0 && k / gap < 4)
                pluck(v.str[k / gap], want[k / gap], colour, 0.85f + 0.3f * v.rng.unit(), k / gap == 3);
        }
        // The strings.
        // (The buzz adds more toward the jawari end than the bridge takes,
        // the top octave's strings ring shorter, and in the bottom octave
        // the kharaj's fundamental is under the ear's reach: all made up, so
        // the colour and the height change the sound and not the level; and
        // a round in one bar plucks each string again while it still rings,
        // so it is played a little softer, as loud as the round in two.)
        const float g = kShape[Pad].level * (0.35f + 0.85f * v.cur[2]) * kStruckGain[Raga] *
                        (1.0f - 0.18f * smoothstep(0.05f, 1.0f, colour)) * (1.0f + 0.9f * smoothstep(0.65f, 1.0f, v.cur[0])) *
                        (1.0f + 0.1f * smoothstep(60.0f, 54.0f, sa)) * (1.0f - 0.28f * smoothstep(0.2f, 0.4f, pulse));
        static const float kPan[4] = {-0.35f, 0.1f, -0.1f, 0.35f};
        const float dt = float(n) / m_rate;
        float sL[kBlock] = {}, sR[kBlock] = {};
        for (int k = 0; k < 4; ++k) {
            TanString &t = v.str[k];
            if (t.midi <= 0)
                continue;
            t.since += dt;
            const float gl = std::sqrt(0.5f * (1.0f - kPan[k])), gr = std::sqrt(0.5f * (1.0f + kPan[k]));
            // The curve: as the swing narrows (against the widest it was)
            // the string lies on the bridge for a shorter share of each
            // period, down to a twenty-sixth; the bridge's height is where a
            // swing that wide is on it for that share. While it lies on the
            // bridge the string is shortened (it leaves the curve further
            // along): the loop's delay read is drawn nearer with how far
            // past the bridge's height it was a sample ago, up to a sample
            // or two, a jolt of phase once a period that moves the energy
            // up into the overtones without taking it away, so the string
            // still rings as long; and a little is pushed back as well, the
            // bridge giving nothing. What the bridge gives back is heard as
            // well: the contact's pulse, through its band.
            const float rel = t.peak > 1e-6f ? clampf(t.swing / t.peak, 0.0f, 1.0f) : 1.0f;
            t.share = std::max(1.0f / 26.0f, t.contact * std::pow(rel, 1.4f));
            const float sag = 1.0f - std::cos(kPi * t.share);
            t.bridge = t.mid + std::max(1e-5f, t.swing * (1.0f - sag));
            const float knee = 0.08f * t.swing + 1e-5f;
            // (The pulse is as tall as the swing, however short, and never
            // taller: a string's swing is not always a sine's.)
            const float toPulse = 1.0f / std::max(1e-5f, sag);
            // (The band never above 4.5 kHz, and the buzz dulled steeply over
            // 5.5 kHz: it is the string's, not a hiss.)
            t.buzzF.set(std::min(4500.0f, midiToHz(t.midi) * 0.6f / t.share), 1.2f, m_rate);
            const float cap = 2.5f * t.swing + 1e-6f, invCap = 1.0f / cap;
            const float invSwing = 1.0f / (0.5f * t.swing * sag + 1e-6f);
            // (A shorter contact is a sharper one, the string snapping off
            // the curve: its pulse carries a little more than a long one did.)
            const float sharp = t.buzz * std::pow(t.contact / t.share, 0.8f);
            float sq = 0;
            for (int i = 0; i < n; ++i) {
                t.readAt += (t.readTo - t.readAt) * t.readGlide;
                // (Read from as much nearer as the string lay past the bridge
                // a sample ago.)
                const float rd = std::max(1.0f, t.readAt - t.shorten);
                const int i0 = int(rd);
                const float fr = rd - float(i0);
                const float a = t.buf[(t.w - uint32_t(i0)) & t.mask], b = t.buf[(t.w - uint32_t(i0) - 1u) & t.mask];
                const float y = a + fr * (b - a);
                t.z += t.b * (y - t.z);
                // The jawari: past the bridge, pushed back, and its pulse.
                float zc = t.z;
                t.mid += t.follow * (zc - t.mid);
                t.spread += t.follow * ((zc - t.mid) * (zc - t.mid) - t.spread);
                float pulse = 0;
                t.shorten = 0;
                if (const float over = zc - t.bridge; over > 0) {
                    pulse = std::min(over * toPulse, t.swing);
                    zc -= t.jaw * over * over / (over + knee);
                    t.shorten = t.shortMax * std::min(1.0f, over * invSwing);
                }
                float x = zc * t.loss;
                if (t.exciteLeft > 0) {
                    // A pluck: a triangle less its own mean, so it starts and
                    // ends at nothing (no step to click), through a low-pass
                    // at 5 kHz: a finger is soft, and the triangle's corners
                    // (or any noise for the finger's grain) would otherwise
                    // spray a line of harmonics to the top of the spectrum
                    // on every pluck. The jawari makes the brightness, as it
                    // should. (The filter runs on a moment after, to settle.)
                    const float u = std::min(1.0f, 1.0f - float(t.exciteLeft - kPluckTail) / t.exLen);
                    const float tri = u < 0.2f ? u / 0.2f : (1.0f - u) / 0.8f;
                    const float e = t.exAmp * (tri - 0.785f * sinTurns(0.5f * u));
                    x += t.exF.step(e).low;
                    --t.exciteLeft;
                }
                t.buf[t.w] = x;
                t.w = (t.w + 1) & t.mask;
                // (The buzz never stands much taller than the string's own
                // swing: past it, it rounds off, as a buzz does, rather than
                // spiking.)
                const float bz = sharp * t.buzzF.k * t.buzzF.step(pulse).band * invCap;
                const float o = t.dc.step(y + t.buzzLp2.step(t.buzzLp.step(cap * bz / std::sqrt(1.0f + bz * bz)).low).low).high;
                sq += o * o;
                sL[i] += o * gl;
                sR[i] += o * gr;
            }
            t.swing = std::sqrt(2.0f * t.spread);
            if (t.exciteLeft <= 0)
                t.peak = std::max(t.peak, t.swing);
            t.level += (std::sqrt(sq / float(n)) - t.level) * 0.1f;
        }
        const float a0 = v.envPrev, a1 = v.env;
        for (int i = 0; i < n; ++i) {
            const float e = g * lerpf(a0, a1, float(i + 1) / float(n));
            outL[i] += sL[i] * e;
            outR[i] += sR[i] * e;
            busL[i] += sL[i] * e * 0.8f;
            busR[i] += sR[i] * e * 0.8f;
        }
        v.hammer = std::max(v.hammer, std::min(1.0f, 3.0f * (v.str[0].level + v.str[1].level + v.str[2].level + v.str[3].level)));
    }
    // Plucks a string at midi: retuned first if its note has changed (the
    // delay read sliding to the new period, what it still rings carried
    // along, so nothing is cut), then the next period filled with the
    // pluck. Toward the jawari end of the column the
    // bridge meets more of the swing and the loop is brighter: the buzz.
    // kharaj: the string an octave under Sa.
    void pluck(TanString &t, float midi, float colour, float vel, bool kharaj) {
        if (std::fabs(t.midi - midi) > 0.01f) {
            // A string that has never sounded starts clean; one still
            // ringing slides into its new note as the pluck goes in, its old
            // ring carried along (so nothing is cut).
            const bool fresh = t.midi <= 0;
            if (fresh)
                t.clear();
            t.midi = midi;
            const float hz = midiToHz(midi);
            t.period = m_rate / hz;
            // The loop's low-pass: a steel string's, bright (the bridge
            // keeps feeding the top, so the loop needn't), brighter still
            // toward the jawari's end. Its phase delay at the fundamental
            // comes out of the delay read.
            t.b = 0.72f + 0.24f * colour;
            const float w = kTwoPi * hz / m_rate, q = 1.0f - t.b;
            const float lag = std::atan2(q * std::sin(w), 1.0f - q * std::cos(w)) / w;
            t.readTo = std::max(1.0f, t.period - lag);
            if (fresh)
                t.readAt = t.readTo;
            t.readGlide = 1.0f - std::exp(-1.0f / (0.007f * m_rate));
            // (Over four periods: steady through one, quick to follow the string.)
            t.follow = 1.0f - std::exp(-1.0f / (4.0f * t.period));
            // (The kharaj, the string an octave under Sa, keeps its
            // fundamental lower: its buzz is its voice, and the floor is the
            // drone's and the bass's, not its.)
            t.dc.set((kharaj ? 1.1f : 0.6f) * hz, 0.7f, m_rate);
            t.buzzLp.set(5500.0f, 0.54f, m_rate);
            t.buzzLp2.set(5500.0f, 1.31f, m_rate);
            if (fresh)
                t.dc.reset();
        }
        t.stilled = false;
        const float hz = midiToHz(midi);
        // A tanpura string rings on through the whole round, so the drone
        // never thins (the bridge takes its top as it buzzes): sixty decibels in about twenty-two seconds at A3.
        t.loss = std::pow(10.0f, -3.0f / (28.0f * std::pow(110.0f / hz, 0.3f) * hz));
        t.since = 0;
        t.exLen = float(int(t.period));
        t.exciteLeft = int(t.period) + kPluckTail;
        t.exAmp = 0.5f * vel;
        copyCoefficients(t.exF, m_pluckLp);
        t.exF.reset();
        // The bridge: at the soft end the string lies on it for a longer
        // share of a fresh pluck's period (its band low, the buzz soft), at
        // the bright end for a shorter one, pushed back harder and buzzing
        // louder. The swing is measured afresh from this pluck.
        const float j = smoothstep(0.05f, 1.0f, colour);
        t.contact = 0.16f - 0.02f * j;
        t.jaw = 0.25f + 0.3f * j;
        t.buzz = 0.8f + 7.2f * j;
        t.shortMax = 0.3f + 0.9f * j;
        t.peak = 0;
    }

    // Khoomei: one throat singer. The voice is a pressed drone on the key's
    // tonic (a table read, its harmonics lines); the singer's mouth is the
    // filter. A low formant gives the drone its vowel; a second resonance,
    // the tongue's, is narrowed until it lets one harmonic through nearly
    // alone, and that harmonic is heard as a whistle over the drone. The
    // melody is sung by moving it from overtone to overtone (it slides, and
    // the harmonics it passes flash on the way, the sound of it). Only
    // overtones that fall on the key's scale are sung (the seventh and the
    // fourteenth, a third of a semitone flat, are allowed: Tuvan melodies
    // use them), and on the strong steps of the melody one that is a tone
    // of the chord is preferred, so the melody follows the chords while the
    // drone stays. Across the column the three ways of singing: khoomei, the
    // soft one, the whistle broad and the drone full; sygyt, the whistle
    // narrow and loud over a pressed, thinner drone; and kargyraa, the
    // same voice on the tonic with the false folds closing under it at half
    // its rate (every other pulse of the voice weaker: a subharmonic, the
    // growl, heard an octave under the tonic and never two), its melody the
    // vowels on the low overtones of that octave down. The whistle breathes:
    // its level wavers with the singer's breath pressure, a quick tremble
    // and a slow wander, and the singer scoops up into the voice as it
    // starts and into the whistle at the top of every phrase. Height is how
    // high the melody sings. Twisted anticlockwise, the ezengileer, the
    // stirrup: the melody on triplets and the voice pulsing in a gallop;
    // clockwise, the vowel opens and closes with the bars.
    void renderThroat(Voice &v, float colour, float *outL, float *outR, float *busL, float *busR, int n,
                      const Harmony &h, const Transport &tr) {
        const float dt = float(n) / m_rate;
        const float height = v.cur[0], press = v.cur[2], motion = v.cur[3], width = v.cur[4];
        const float pulse = std::max(0.0f, -motion), evolve = std::max(0.0f, motion);
        const float sygyt = smoothstep(0.1f, 0.5f, colour) * (1.0f - smoothstep(0.62f, 0.85f, colour));
        const float growl = smoothstep(0.62f, 0.85f, colour);
        v.envPrev = v.env;

        // The singer's pitch: the tonic from F#2 up (92 to 185 Hz), come up
        // to from a semitone and a half under as the voice starts (the
        // scoop, gone in about a fifth of a second).
        float tonic = float(h.midi(0, 0));
        while (tonic >= 54.0f)
            tonic -= 12.0f;
        while (tonic < 42.0f)
            tonic += 12.0f;
        v.thScoop *= std::exp(-dt / 0.06f);
        v.thMidi = tonic - 1.5f * v.thScoop;
        const float hzUp = midiToHz(v.thMidi);
        const float f0 = hzUp * std::exp2(-growl); // the pitch the whistle counts its overtones from

        // The overtones the melody may sing.
        const int per = h.tuning.degreesPerOctave();
        int allowed[16], na = 0;
        bool chordTone[16] = {};
        for (int k = 5; k <= 18 && na < 16; ++k) {
            const double c = 12.0 * std::log2(double(k));
            bool in = false;
            for (int d = 0; d < per; ++d) {
                double diff = c - h.tuning.degreeSemis(d);
                diff -= 12.0 * std::round(diff / 12.0);
                in |= std::fabs(diff) < 0.35;
            }
            if (!in)
                continue;
            for (int i = 0; i < m_bed.count; ++i) {
                double diff = c - h.tuning.degreeSemis(m_bed.tones[i]);
                diff -= 12.0 * std::round(diff / 12.0);
                chordTone[na] |= std::fabs(diff) < 0.35;
            }
            allowed[na++] = k;
        }
        // The melody, two bars of eighths (triplets in the gallop): steps
        // from home through the overtones the key allows; 99 holds.
        const int stepsPerBeat = pulse > 0.3f ? 3 : 2;
        {
            const double s0 = tr.beat * stepsPerBeat, s1 = tr.beatAt(n) * stepsPerBeat;
            if (na > 0 && (v.thStep == INT64_MIN || std::floor(s1) > std::floor(s0))) {
                const int64_t step = int64_t(std::floor(s1));
                v.thStep = step;
                static const int kPhrase[32] = {0, 99, 99, 1, 2, 99, 1, 99, 0,  99, -1, 99, 0, 99, 99, 99,
                                                2, 99, 3,  2, 1, 99, 2, 99, 1,  99, 0,  99, -1, 0, 99, 99};
                // (A kargyraa singer answers a sygyt one: a bar and a half on.)
                const int off = kPhrase[(step + (growl > 0.5f ? 12 : 0)) & 31];
                if (off != 99) {
                    const float homeK = lerpf(7.0f, 13.0f, height);
                    int home = 0;
                    for (int i = 1; i < na; ++i)
                        if (std::fabs(float(allowed[i]) - homeK) < std::fabs(float(allowed[home]) - homeK))
                            home = i;
                    int idx = std::clamp(home + off, 0, na - 1);
                    // On the strong steps, a tone of the chord if one is a step away.
                    if (step % (2 * stepsPerBeat) == 0 && !chordTone[idx]) {
                        if (idx + 1 < na && chordTone[idx + 1])
                            ++idx;
                        else if (idx > 0 && chordTone[idx - 1])
                            --idx;
                    }
                    const float to = float(allowed[idx]);
                    // A sygyt singer flicks into a note from the overtone
                    // past it, the yodel in the whistle: for 50 ms the
                    // whistle aims one beyond.
                    v.thFlick = sygyt > 0.4f && to != v.whistleTo ? (to > v.whistleTo ? 1.0f : -1.0f) : 0.0f;
                    v.thFlickLeft = 0.05f;
                    // At the top of each phrase (and on a note sung again)
                    // the whistle is come up to from under, as the singer
                    // takes a breath and finds the note again.
                    if ((step % (8 * stepsPerBeat)) == 0 || to == v.whistleTo)
                        v.thWhistleScoop = -0.45f;
                    v.whistleTo = to;
                }
            }
        }
        v.thFlickLeft -= dt;
        v.thWhistleScoop *= std::exp(-dt / 0.05f);
        const float aim = v.whistleTo + (v.thFlickLeft > 0 ? v.thFlick : 0.0f) + v.thWhistleScoop;
        v.whistle += (aim - v.whistle) * (1.0f - std::exp(-dt / 0.022f));

        // The breath: a tremble at about five a second (two rates, so it
        // never settles into a beat) and a slow wander, the pressure behind
        // the whistle never quite steady. The whistle's level follows it
        // most, the drone's a little, the whistle's resonance a hair (more
        // pressure, a slightly higher tongue).
        auto breathAt = [&] { return 0.6f * sinTurns(v.thBreath) + 0.4f * sinTurns(v.thBreath2) + v.thWander; };
        const float breath0 = breathAt();
        v.thBreath += 5.3f * dt;
        v.thBreath -= std::floor(v.thBreath);
        v.thBreath2 += 3.7f * dt;
        v.thBreath2 -= std::floor(v.thBreath2);
        // (A random walk pulled back to nothing over half a second: it
        // strays about a third as far as the tremble swings.)
        v.thWander += 1.0f * v.rng.bipolar() * std::sqrt(dt) - v.thWander * dt / 0.5f;
        const float breath1 = breathAt();
        const float breathDepth = lerpf(lerpf(0.16f, 0.24f, sygyt), 0.12f, growl);

        // The mouth. The vowel's formant (an "o" opening toward "a" in
        // kargyraa, swept with the bars when twisted clockwise), the body
        // under it, and the whistle: two resonances in a row on the overtone,
        // narrow for sygyt, broad for kargyraa's vowels.
        float sweep = 0;
        if (evolve > 0.04f) {
            const double ph = tr.beat / 8.0;
            sweep = 0.7f * evolve * std::sin(kTwoPi * float(ph - std::floor(ph)));
        }
        const float f1 = lerpf(lerpf(480.0f, 360.0f, sygyt), 330.0f, growl) * std::exp2(sweep);
        v.fL[0].set(f1, 2.5f, m_rate);
        v.bodyL.set(lerpf(lerpf(900.0f, 650.0f, sygyt), 700.0f, growl), 0.7f, m_rate);
        const float fw = std::min(0.4f * m_rate, v.whistle * f0 * (1.0f + 0.004f * breath1));
        const float qw = lerpf(lerpf(9.0f, 34.0f, sygyt), 5.0f, growl);
        v.fL[1].set(fw, qw, m_rate);
        v.fL[2].set(fw, qw, m_rate);
        // Kargyraa's melody is vowels, and a vowel is two formants: as the
        // first climbs the low overtones (an "u" opening through "o" to
        // "a"), the second climbs with it, from about 700 Hz to past 1.3
        // kHz, along the line the back vowels lie on. Heard together they
        // say the vowel, and the melody can be followed as a line of
        // vowels rather than a colour shifting under the growl.
        const float f2 = clampf(700.0f + 1.3f * (fw - 300.0f), 600.0f, 2200.0f);
        v.thF2.set(f2, 7.0f, m_rate);
        const float f2Gain = 0.9f * growl; // (relative to the first: it goes out at the whistle's gain)
        if (v.thHp1.a1 == 0) {
            v.thHp1.set(30.0f, 0.54f, m_rate);
            v.thHp2.set(30.0f, 1.31f, m_rate);
        }
        // The whistle's gain makes up for how little of the voice there is
        // up there (its harmonics fall about 7 dB an octave), so every note
        // of the melody is as loud as the next.
        const float lift = std::pow(std::max(1.0f, v.whistle), 1.15f) * std::exp(fw / 9000.0f);
        const float wGain = lerpf(lerpf(0.35f, 1.1f, sygyt), 0.55f, growl) * lift;
        const float dGain = lerpf(lerpf(1.0f, 0.5f, sygyt), 1.0f, growl);

        const float *U = m_bank->table[PadThroat][1].data();
        const auto speed = [&](float hz, int oct) {
            const double sp = double(hz) / double(midiToHz(Bank::baseMidi(oct))) * double(kTableRate) / double(m_rate);
            return uint32_t(std::llround(sp * double(1u << kFracBits)));
        };
        const uint32_t incU = speed(hzUp, 1);
        constexpr uint32_t fm = (1u << kFracBits) - 1;
        // The false folds close at half the voice's rate.
        const float subInc = 0.5f * hzUp / m_rate;

        // The gallop: on the first and third triplet of every beat, the
        // voice pushed (the long and short of a horse's stride). Each push
        // swells in over a few milliseconds and dies away, as a singer's
        // diaphragm does: a step would click on every hoofbeat.
        const float gallop = smoothstep(0.3f, 0.7f, pulse) * 0.75f;
        const float gallopFall = std::exp(-1.0f / (0.09f * m_rate));
        const float gallopRise = 1.0f - std::exp(-1.0f / (0.0015f * m_rate));
        const float attInc = 1.0f / (v.attack * m_rate);
        const float pressGain = 0.35f + 0.85f * press;
        const float level = kShape[Pad].level * pressGain;
        const float agc0 = v.agc;
        float sq = 0, wsq = 0;
        float y[kBlock];
        uint32_t pu = v.thPhase;
        const float inv = 1.0f / float(n);
        for (int i = 0; i < n; ++i) {
            if (v.holdEnv) {
            } else if (!v.releasing || v.ramp < 1.0f) {
                v.ramp = std::min(1.0f, v.ramp + attInc);
                v.env = v.ramp * v.ramp * (3.0f - 2.0f * v.ramp);
            } else {
                v.env *= v.relCoef;
            }
            const uint32_t iu = pu >> kFracBits;
            const float fu = float(pu & fm) * kFracScale;
            const float up = U[iu] + fu * (U[iu + 1] - U[iu]);
            pu += incU;
            // The false folds: every other pulse of the voice weaker, so it
            // repeats only every second period, an octave down.
            v.thSub += subInc;
            v.thSub -= std::floor(v.thSub);
            const float x = up * (1.0f + 0.85f * growl * sinTurns(v.thSub)) + 0.004f * v.rng.bipolar();
            const float drone = 0.6f * v.bodyL.step(x).low + v.fL[0].k * v.fL[0].step(x).band;
            float w = v.fL[2].k * v.fL[2].step(v.fL[1].k * v.fL[1].step(x).band).band;
            if (f2Gain > 1e-4f)
                w += f2Gain * v.thF2.k * v.thF2.step(x).band;
            float g = 1.0f;
            if (gallop > 0.001f) {
                const double b = tr.beatAt(i) * 3.0;
                const int64_t s = int64_t(std::floor(b));
                if (s != v.stabStep) {
                    v.stabStep = s;
                    if (((s % 3) + 3) % 3 != 1)
                        v.stabRising = true;
                }
                if (v.stabRising) {
                    v.stab += (1.02f - v.stab) * gallopRise;
                    if (v.stab >= 1.0f)
                        v.stab = 1.0f, v.stabRising = false;
                } else {
                    v.stab *= gallopFall;
                }
                g = 1.0f - gallop + gallop * v.stab;
            }
            const float br = lerpf(breath0, breath1, float(i + 1) * inv);
            const float o = dGain * drone * (1.0f - 0.4f * gallop + 0.4f * gallop * g) * (1.0f + 0.25f * breathDepth * br) +
                            wGain * w * g * (1.0f + breathDepth * br);
            sq += o * o;
            wsq += wGain * w * wGain * w;
            // (Under the voice, nothing: the false folds' growl is an octave
            // under the tonic, 46 Hz at the lowest, and what is under that
            // is only rumble.)
            y[i] = v.thHp2.step(v.thHp1.step(o * v.env).high).high;
        }
        v.thPhase = pu;
        // Held to one level, slowly, as every layer is: the way of singing
        // changes the colour, the pressure the loudness.
        const float rms = std::sqrt(sq / float(n));
        v.agcRms += (rms - v.agcRms) * (1.0f - std::exp(-dt / 0.3f));
        const float wantAgc = clampf(1.0f / std::max(v.agcRms, 1e-3f), 0.15f, 12.0f);
        v.agc += (wantAgc - v.agc) * (1.0f - std::exp(-dt / (wantAgc < v.agc ? 0.03f : 0.6f)));
        // One singer, near the middle, a little wider for a flat finger.
        const float p = 0.15f * v.side * width;
        const float gl = std::sqrt(0.5f * (1.0f - p)), gr = std::sqrt(0.5f * (1.0f + p));
        // (Sygyt's thin voice comes through the room less than its rms says,
        // kargyraa's growl, its pressed harmonics in the ear's most
        // sensitive octaves, more: by ear, the tests and the meter, a dB or
        // so each.)
        const float a = level * agc0 * 1.2f * (1.0f + 0.12f * sygyt - 0.1f * growl);
        static const float kSend = 0.6f;
        for (int i = 0; i < n; ++i) {
            const float l = y[i] * a * gl, r = y[i] * a * gr;
            outL[i] += l;
            outR[i] += r;
            busL[i] += l * kSend;
            busR[i] += r * kSend;
        }
        v.whistleShown = v.env * std::min(1.0f, 1.6f * std::sqrt(wsq / std::max(sq, 1e-12f)));
        v.gate = 1;
        v.level = v.env * pressGain;
        if (v.releasing && v.ramp >= 1.0f && v.env < 1e-4f)
            v.active = false;
    }

    // Every strike sounding, into the dry bed and its style's bus, each at
    // its voice's envelope as it is now (the voice's release is theirs).
    void renderStrikes(float *dryL, float *dryR, float (*busL)[kBlock], float (*busR)[kBlock], int n) {
        // How much of a strike goes to the dry bed and how much to its bus.
        static const float kDry[kStyles] = {1, 1, 1, 1, 1.0f, 1.0f, 1.0f, 0.9f, 1.0f};
        static const float kSend[kStyles] = {0, 0, 0, 0, 0.0f, 0.8f, 1.0f, 0.85f, 0.0f};
        const float inv = 1.0f / float(n);
        // City pop's bass, gathered (it is in the middle) to go through its
        // high-pass together.
        float bass[kBlock] = {};
        bool anyBass = false;
        // And Gamelan's gongs, through theirs.
        float gong[kBlock] = {};
        bool anyGong = false;
        for (Strike &st : m_strikes) {
            if (!st.on)
                continue;
            const Voice *o = st.owner >= 0 ? &m_voices[size_t(st.owner)] : nullptr;
            if (o && (!o->active || o->id != st.ownerId))
                o = nullptr;
            if (!o) {
                st.on = false;
                continue;
            }
            const float e0 = o->envPrev, e1 = o->env;
            float gl0 = st.gl, gr0 = st.gr, gl1 = st.gl, gr1 = st.gr;
            if (st.style == CityPop && st.hand == 0) {
                // The tremolo pans the piano across, the voice's own.
                const float d = o->tremDepth;
                const float s0 = std::sin(kTwoPi * (o->trem - 4.7f * float(n) / m_rate)), s1 = std::sin(kTwoPi * o->trem);
                gl0 = st.gl * (1.0f + d * s0), gr0 = st.gr * (1.0f - d * s0);
                gl1 = st.gl * (1.0f + d * s1), gr1 = st.gr * (1.0f - d * s1);
            }
            const float dry = kDry[st.style], send = kSend[st.style];
            float *bl = busL[st.style], *br = busR[st.style];
            const bool isBass = st.style == CityPop && st.hand == 3;
            anyBass |= isBass;
            anyGong |= st.low;
            const int from = std::min(st.delay, n);
            st.delay = std::max(0, st.delay - n);
            if (from >= n)
                continue;
            // A bent strike (the gong) falls back toward its pitch, a block
            // at a time.
            if (std::fabs(st.bend) > 1e-5f) {
                for (int k = 0; k < st.np; ++k) {
                    const float w = st.w[k] * (1.0f + st.bend);
                    st.cw[k] = std::cos(w), st.sw[k] = std::sin(w);
                }
                st.bend *= std::pow(st.bendDec, float(n - from));
                if (std::fabs(st.bend) <= 1e-5f)
                    for (int k = 0; k < st.np; ++k)
                        st.cw[k] = std::cos(st.w[k]), st.sw[k] = std::sin(st.w[k]);
            }
            const float twoPiInv = 1.0f / kTwoPi;
            for (int i = from; i < n; ++i) {
                float y = 0;
                for (int k = 0; k < st.np; ++k) {
                    const float c = st.c[k] * st.cw[k] - st.s[k] * st.sw[k];
                    const float s = st.c[k] * st.sw[k] + st.s[k] * st.cw[k];
                    st.c[k] = c;
                    st.s[k] = s;
                    y += s * st.amp[k];
                    st.amp[k] *= st.dec[k];
                }
                if (st.fm) {
                    const float a = sinTurns(st.phA + st.index * twoPiInv * sinTurns(st.phA));
                    const float b = sinTurns(st.phB + st.index * twoPiInv * sinTurns(st.phB));
                    const float c = sinTurns(st.ph + st.bark * twoPiInv * sinTurns(st.phM));
                    st.bz1 += st.barkLp * (st.barkAmp * c - st.bz1);
                    st.bz2 += st.barkLp * (st.bz1 - st.bz2);
                    y += st.fmAmp * (0.62f * a + 0.38f * b + st.bz2);
                    st.fmAmp *= st.fmDec;
                    st.barkAmp *= st.barkAmpDec;
                    st.index = st.indexFloor + (st.index - st.indexFloor) * st.indexDec;
                    st.bark *= st.barkDec;
                    st.phA += st.incA;
                    st.phA -= float(st.phA >= 1.0f);
                    st.phB += st.incB;
                    st.phB -= float(st.phB >= 1.0f);
                    st.ph += st.inc;
                    st.ph -= float(st.ph >= 1.0f);
                    st.phM += st.incM;
                    st.phM -= float(st.phM >= 1.0f);
                }
                // The caps: struck as the tine swings up past them (not
                // every time: a cap sits loose, and sometimes misses), as
                // hard as the swing goes past. Each hit scrapes (noise,
                // gone in a quarter of a millisecond), and the caps ring
                // with it for a millisecond or so. (Once the swing no longer
                // reaches them and they have stopped, they are let be.)
                if (st.buzz > 0 && (std::fabs(st.amp[0]) > st.capAt || st.scrape > 1e-5f)) {
                    const float d = st.s[0] * st.amp[0];
                    if (d > st.capAt && st.capWas <= st.capAt && m_strikeRng.unit() < 0.8f)
                        st.scrape += st.buzz * (std::fabs(st.amp[0]) - st.capAt) * (0.6f + 0.4f * m_strikeRng.unit());
                    st.capWas = d;
                    const float e = st.scrape * m_strikeRng.bipolar();
                    st.scrape *= st.scrapeDec;
                    float r = 0.35f * e;
                    for (int k = 0; k < Strike::kCaps; ++k)
                        r += st.cap[k].k * st.cap[k].step(e).band;
                    y += r;
                }
                if (st.hiss + st.clash > 1e-4f) {
                    const float wide = st.hissF.k * st.hissF.step(m_strikeRng.bipolar()).band;
                    y += st.clash * wide + st.hiss * st.hissF2.k * st.hissF2.step(wide).band;
                    st.hiss *= st.hissDec;
                    st.clash *= st.clashDec;
                }
                if (st.choke > 0 && --st.choke == 0)
                    damp(st, 0.012f);
                st.att = std::min(1.0f, st.att + st.attInc);
                st.duck = st.duckFloor + (st.duck - st.duckFloor) * st.duckDec;
                const float u = float(i + 1) * inv;
                const float a = y * st.att * st.duck * st.gain * lerpf(e0, e1, u);
                if (isBass) {
                    bass[i] += a;
                    continue;
                }
                if (st.low) {
                    gong[i] += a;
                    continue;
                }
                const float l = a * lerpf(gl0, gl1, u), r = a * lerpf(gr0, gr1, u);
                dryL[i] += l * dry;
                dryR[i] += r * dry;
                bl[i] += l * send;
                br[i] += r * send;
            }
            for (int k = 0; k < st.np; ++k) {
                const float m = 1.5f - 0.5f * (st.c[k] * st.c[k] + st.s[k] * st.s[k]);
                st.c[k] *= m;
                st.s[k] *= m;
            }
            float left = st.fm ? st.fmAmp : 0.0f;
            for (int k = 0; k < st.np; ++k)
                left = std::max(left, std::fabs(st.amp[k]));
            left = std::max(left, st.hiss + st.clash);
            st.shown = std::min(1.0f, 1.5f * left * st.att);
            if (left < 2e-4f)
                st.on = false;
        }
        // (Run on a while after the last note, so its tail is filtered too.)
        m_bassHold = anyBass ? 4800 : std::max(0, m_bassHold - n);
        if (m_bassHold > 0) {
            const float g = 0.7071f;
            for (int i = 0; i < n; ++i) {
                const float x = m_bassHp2.step(m_bassHp1.step(bass[i]).high).high * g;
                dryL[i] += x * kDry[CityPop];
                dryR[i] += x * kDry[CityPop];
                busL[CityPop][i] += x * kSend[CityPop];
                busR[CityPop][i] += x * kSend[CityPop];
            }
        }
        // The gongs' high-pass: 24 dB an octave under 36 Hz, so the great
        // gong's hum (40 to 80 Hz) is all there and nothing under it
        // rumbles. (In the middle, and dry: Gamelan sends nothing.)
        m_gongHold = anyGong ? 4800 : std::max(0, m_gongHold - n);
        if (m_gongHold > 0) {
            const float g = 0.7071f;
            for (int i = 0; i < n; ++i) {
                const float x = m_gongHp2.step(m_gongHp1.step(gong[i]).high).high * g;
                dryL[i] += x;
                dryR[i] += x;
            }
        }
    }

    // What a struck pad's notes are scaled by, so each style's pad sits as
    // loud as the classic one: worked out by ear and checked by the tests.
    static constexpr float kStruckGain[kStyles] = {1, 1, 1, 1, 2.2f, 26.0f, 2.0f, 2.18f, 1};

    float m_rate = 48000;
    const Bank *m_bank = nullptr;
    std::array<Voice, kVoices> m_voices;
    std::array<Glint, kGlints> m_glints;
    Halo m_halo;
    Cloud m_cloud;
    Ensemble m_ensemble;
    DubEcho m_echo;
    TapeBus m_tape;
    Ensemble m_chorus; // City pop's
    Taraf m_taraf;
    Gourd m_gourd;
    Valley m_valley;
    std::array<Strike, 64> m_strikes;
    std::vector<float> m_stringMem;
    Svf m_bassHp1, m_bassHp2; // City pop's bass's high-pass, 24 dB an octave under 35 Hz
    int m_bassHold = 0;
    Svf m_gongHp1, m_gongHp2; // Gamelan's gongs' high-pass, 24 dB an octave under 36 Hz
    int m_gongHold = 0;
    float m_break = 1, m_breakWant = 1; // the halo's gain, held down through an angsel's silent beat
    Svf m_pluckLp;                // the tanpura's pluck's low-pass, every string's copy
    static constexpr int kPluckTail = 32; // samples its filters run on after it
    Rng m_strikeRng{0x5B1Eu};
    int64_t m_gongCycle = INT64_MIN;
    uint32_t m_tarafSerial = ~0u;
    float m_busHold[kStyles] = {}, m_hiss = 0, m_tapeHiss = 0, m_neonShare = 0, m_dubShare = 0;
    Svf m_hpL, m_hpR;
    Bed m_bed;
    bool m_freeze = false, m_cloudOn = false, m_swellOn = false;
    uint32_t m_mask = 0, m_chordSerial = 0;
    double m_melt = 0, m_sinceChange = 64;
    float m_clear = 0;
    int m_pump = PumpKick, m_own = -1;
    float m_space = 0.5f, m_spaceS = 0.5f, m_swell = 0, m_freezeAmt = 0, m_cloudAmt = 0;
    float m_norm = 0.6f, m_pumpEnv = 0, m_levelOut = 0, m_haloOut = 0, m_swellE = 0;
    bool m_ownLoop = false, m_ownHand = false;
    float m_lowCutSet = 55.0f;
    Svf m_sideHp1, m_sideHp2, m_sheenL, m_sheenR;
    float m_tone = 0.5f, m_toneS = 0.5f, m_toneSet = 0;
    Svf m_toneL, m_toneR;
};

// --- scenes -------------------------------------------------------------------

struct SceneLayer {
    int family;
    float colour, height, press, motion, width;
};
struct Scene {
    const char *name;
    float attack;
    int count;
    SceneLayer layers[4];
    int style = Classic; // its layers' style, which laying it also makes the fingers'
};

// Whole beds on one key each, laid on the pad as latched layers that can be
// grabbed and reshaped like any other. Appended only: the tutorial and the
// tests name them by place.
constexpr int kSceneCount = 15;
const Scene kScenes[kSceneCount] = {
    {"Dawn", 2.5f, 3, {{Pad, 0.1f, 0.35f, 0.5f, 0.25f, 0.85f}, {Choir, 0.08f, 0.45f, 0.45f, 0.15f, 0.9f}, {Air, 0.05f, 0.55f, 0.35f, 0.3f, 1.0f}}},
    {"Ocean", 3.0f, 3, {{Drone, 0.3f, 0.2f, 0.45f, 0.2f, 0.7f}, {Pad, 0.85f, 0.3f, 0.45f, 0.35f, 0.9f}, {Air, 0.4f, 0.4f, 0.5f, 0.6f, 1.0f}}},
    {"Cathedral", 3.0f, 4, {{Drone, 0.7f, 0.1f, 0.45f, 0.1f, 0.6f}, {Choir, 0.5f, 0.35f, 0.55f, 0.2f, 0.9f}, {Choir, 0.9f, 0.8f, 0.4f, 0.1f, 1.0f}, {Glass, 0.1f, 0.3f, 0.35f, 0.0f, 1.0f}}},
    {"Stardust", 2.0f, 3, {{Glass, 0.65f, 0.5f, 0.55f, 0.0f, 1.0f}, {Pad, 0.55f, 0.6f, 0.4f, 0.4f, 0.9f}, {Air, 0.1f, 0.8f, 0.3f, 0.3f, 1.0f}}},
    // Lo-fi tape: vinyl, felt keys and a drone, all on a worn cassette. Its
    // loops from before it had a style still carry style 0, and play classic.
    {"Nostalgia", 2.0f, 3, {{Air, 0.95f, 0.5f, 0.45f, 0.0f, 1.0f}, {Pad, 0.8f, 0.3f, 0.5f, 0.2f, 0.8f}, {Drone, 0.55f, 0.25f, 0.4f, 0.0f, 0.6f}}, Tape},
    {"Euphoria", 2.5f, 4, {{Pad, 0.5f, 0.45f, 0.7f, 0.5f, 1.0f}, {Choir, 0.45f, 0.55f, 0.5f, 0.2f, 1.0f}, {Glass, 0.45f, 0.45f, 0.45f, 0.0f, 1.0f}, {Drone, 0.4f, 0.3f, 0.4f, 0.0f, 0.7f}}},
    {"Night rain", 2.5f, 3, {{Air, 0.4f, 0.45f, 0.6f, 0.0f, 1.0f}, {Pad, 0.15f, 0.2f, 0.45f, 0.2f, 0.8f}, {Glass, 0.2f, 0.25f, 0.35f, 0.0f, 1.0f}}},
    {"Pulse", 0.4f, 3, {{Pad, 0.5f, 0.5f, 0.65f, -0.8f, 1.0f}, {Drone, 0.35f, 0.2f, 0.45f, 0.0f, 0.7f}, {Choir, 0.4f, 0.6f, 0.45f, -0.4f, 0.9f}}},
    // Synthwave: the pulse-width poly low and full with a slow sweep, the
    // string machine bright and thin above it, all through the ensemble and
    // in front of a smaller room, and only a breath of drone under them.
    {"Neon", 1.6f, 2, {{Pad, 0.95f, 0.38f, 0.6f, 0.3f, 1.0f}, {Pad, 0.05f, 0.7f, 0.65f, 0.0f, 0.9f}}, Neon},
    // Dub techno: the minor-ninth chord stabbed on the offbeats into the
    // echo, its filter ridden slowly, and the hiss and crackle of the record
    // under it. No drone: the gaps between the stabs are the echo's.
    {"Dub", 0.6f, 2, {{Pad, 0.72f, 0.15f, 0.65f, 0.3f, 1.0f}, {Air, 0.95f, 0.5f, 0.0f, 0.0f, 1.0f}}, Dub},
    // The five past the letters, on Shift+Q to T.
    // Balinese gamelan: kotekan interlocking up high, the pokok a beat at a
    // time under it, the kempli's beat and the great gong, a soft drone
    // where the gong's hum lives and wind through the pavilion.
    {"Gamelan", 0.3f, 4, {{Pad, 0.85f, 0.5f, 0.6f, 0.0f, 1.0f}, {Pad, 0.1f, 0.12f, 0.5f, 0.0f, 0.9f}, {Drone, 0.15f, 0.15f, 0.35f, 0.0f, 0.6f}, {Air, 0.05f, 0.6f, 0.25f, 0.3f, 1.0f}}, Gamelan},
    // An alap's ground: the tanpura's round, a bowed drone under it and a
    // low hum of voices, all ringing the sympathetic strings.
    {"Raga", 1.5f, 3, {{Pad, 0.6f, 0.3f, 0.38f, 0.0f, 1.0f}, {Drone, 0.8f, 0.1f, 0.35f, 0.1f, 0.6f}, {Choir, 0.05f, 0.4f, 0.3f, 0.15f, 0.9f}}, Raga},
    // Two Shona mbiras on a wet night: the kushaura's lilt with its caps
    // rattling, a second player low and soft, twisted into the kutsinhira
    // so the two interlock, and a little rain, low and far, under them.
    {"Mbira", 0.3f, 3, {{Pad, 0.62f, 0.4f, 0.4f, 0.0f, 1.0f}, {Pad, 0.15f, 0.05f, 0.3f, -0.6f, 0.8f}, {Air, 0.3f, 0.1f, 0.08f, 0.0f, 1.0f}}, Mbira},
    // City pop off the record: the electric piano comping its push over the
    // octave bass, through its tremolo, glass twinkling over it, and the
    // vinyl's crackle under it all.
    {"City pop", 0.5f, 3, {{Pad, 0.45f, 0.3f, 0.6f, -0.6f, 1.0f}, {Glass, 0.55f, 0.45f, 0.45f, 0.0f, 1.0f}, {Air, 0.9f, 0.5f, 0.35f, 0.0f, 1.0f}}, CityPop},
    // Two throat singers on the steppe: one in sygyt, whistling the melody,
    // one in kargyraa, growling an octave under, a soft drone, and the wind.
    {"Khoomei", 1.5f, 4, {{Pad, 0.4f, 0.45f, 0.6f, 0.0f, 0.9f}, {Pad, 0.92f, 0.3f, 0.45f, 0.0f, 0.8f}, {Drone, 0.2f, 0.15f, 0.35f, 0.1f, 0.6f}, {Air, 0.05f, 0.5f, 0.3f, 0.3f, 1.0f}}, Khoomei},
};
// The first ten on the letter row; the five after them on Shift and its
// first five letters.
const int kSceneKeys[kSceneCount] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I,
                                     Qt::Key_O, Qt::Key_P, Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T};
constexpr int kShiftScenes = 10; // the first scene played with Shift
const char kSceneKeyNames[] = "QWERTYUIOPQWERT";
inline QString sceneKey(int s) {
    return (s >= kShiftScenes ? QStringLiteral("⇧") : QString()) + QChar(kSceneKeyNames[s]);
}
const int kChordKeys[7] = {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_N, Qt::Key_M};

// --- the page -----------------------------------------------------------------

QColor familyColor(QColor base, int family) {
    static const char *tint[kFamilies] = {"#7B6CFF", "#A98BFF", "#D59BFF", "#9FD8FF", "#C9C3FF"};
    QColor t(tint[std::clamp(family, 0, kFamilies - 1)]);
    // Lean toward the page's own phosphor so the page stays one colour.
    return QColor::fromRgbF(0.5f * (t.redF() + base.redF()), 0.5f * (t.greenF() + base.greenF()),
                            0.5f * (t.blueF() + base.blueF()));
}

class AtmosControl : public PartControl {
public:
    // A bed's playing is changes of state (a chord clicked, a layer
    // latched): a layer spans the whole loop, so a change made once isn't
    // made again inside it.
    bool layersSpanTheLoop() const override { return true; }
    // A bed latched and left isn't playing: it doesn't join a chord clicked
    // now to playing from before it was left (the loop would reach back).
    bool soundLeftOnJoinsThePlaying() const override { return false; }
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;

        // The click latches every layer held, a finger that has only just
        // landed included. Holding the click down for most of a second lets
        // every latched layer go (Delete does too).
        if (f.button && !m_button) {
            m_buttonAt = f.t;
            m_longDone = false;
            int latched = 0;
            for (Held &h : m_held)
                if (h.touch >= 0 && !h.grabbed && !h.cancelled) {
                    h.latch = true;
                    ++latched;
                }
            if (latched)
                ctx.notice(QStringLiteral("Latched: it stays when you lift. Hold the click to let everything go"));
        }
        if (f.button && m_button && !m_longDone && f.t - m_buttonAt >= 0.8) {
            m_longDone = true;
            for (Held &h : m_held)
                if (h.touch >= 0 && !h.cancelled) {
                    if (h.grabbed) {
                        if (Latched *l = findLatched(h.layer.id))
                            l->grabbed = false;
                    } else {
                        sendOff(h.layer.id, ctx, f.t);
                    }
                    h.cancelled = true;
                }
            releaseAll(ctx, QStringLiteral("Let go: everything latched fades"));
        }
        m_button = f.button;

        for (Held &h : m_held) {
            if (h.touch < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.touch;
            if (!still)
                lift(h, f.t, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            if (Held *h = findTouch(t.id))
                follow(*h, t, f.t, ctx);
            else
                land(t, f.t, ctx);
        }
    }

    void background(double t, ControlContext &ctx) override {
        const PartView &pv = ctx.studio().parts[ctx.part()];
        m_cue = loopCue(ctx.beatAt(t), pv);

        // The bed's settings as the engine has them, for save(): a thaw or
        // a loop taking the chords changes them without a key. Only a view
        // published after the page's last setting went out, so a save just
        // after a key never reads the setting from before it.
        const StudioView &sv = ctx.studio();
        if (sv.sample > 0 && sv.time > m_settingsSentAt) {
            m_settings.freeze = pv.data[PubFreeze] > 0.5f;
            m_settings.cloud = pv.data[PubCloud] > 0.5f;
            m_settings.pump = std::clamp(int(std::lround(pv.data[PubPump])), 0, 2);
            m_settings.space = clampf(pv.data[PubSpace], 0.0f, 1.0f);
            m_settings.tone = clampf(pv.data[PubTone], 0.0f, 1.0f);
            m_settings.chord = pv.data[PubOwnHand] > 0.5f ? std::clamp(int(std::lround(pv.data[PubOwn])), -1, 6) : -1;
        }

        // A loop that stopped (Backspace, another slot, Ctrl+Z) lets go of
        // the layers it was carrying over its seam.
        if (m_lastPlaying >= 0 && pv.playing != m_lastPlaying) {
            Command c;
            c.type = kSeamRelease;
            c.flags = Command::NoRecord;
            c.i[0] = m_lastPlaying;
            c.t = t;
            ctx.send(c);
        }
        m_lastPlaying = pv.playing;

        // A Space take lands (the studio counts it): its loop's chords drive
        // the bed from now on, until a chord key takes them back, and the
        // latched layers leave the page as soon as the loop has taken their
        // voices over. A take called off or refused leaves them the hand's.
        if (m_takeSlot >= 0 && pv.recording < 0) {
            if (pv.takes != m_takesAtSpace) {
                if (pv.playing >= 0) {
                    Command c;
                    c.type = Command::Param;
                    c.flags = Command::NoRecord;
                    c.i[0] = PChordToLoop;
                    c.t = t;
                    ctx.send(c);
                    const double len = std::max(1.0f, pv.clips[pv.playing].lengthBeats);
                    m_handUntil = t + (len + 4.0) * 60.0 / std::max(1.0, ctx.studio().bpm);
                }
                m_takeSlot = -1;
            } else if (t - m_spaceAt > 0.3) {
                for (Latched &l : m_latched)
                    l.handed = false;
                m_takeSlot = -1;
            }
        }
        if (m_handUntil > 0) {
            const float *d = pv.data;
            for (auto it = m_latched.begin(); it != m_latched.end();) {
                bool live = false;
                for (int k = 0; k < kPubVoices; ++k) {
                    const float *v = d + PubVoice0 + kPubStride * k;
                    live |= v[0] > 0 && (int(v[4]) & 3) == 1 && int(v[8]) == it->layer.id;
                }
                if (it->handed && !live)
                    it = m_latched.erase(it);
                else
                    ++it;
            }
            if (t > m_handUntil) {
                // Whatever the loop didn't take is the hand's again.
                for (Latched &l : m_latched)
                    l.handed = false;
                m_handUntil = 0;
            }
            if (m_scene >= 0 && std::none_of(m_latched.begin(), m_latched.end(), [](const Latched &l) { return l.scene >= 0; }))
                m_scene = -1;
        }

        // A swell the mouse started ends by itself once it has risen all
        // the way.
        if (m_swellClicked && t >= m_swellUntil) {
            m_swellClicked = false;
            swell(ctx);
        }

        // A loop holding the bed, for the status line.
        m_playing.clear();
        if (pv.playing >= 0)
            m_playing = QStringLiteral("loop %1 holds the bed%2").arg(pv.playing + 1)
                            .arg(pv.data[PubOwnLoop] > 0.5f ? QStringLiteral(" and its chords") : QString());

        // What the freeze is holding, for the status line.
        m_frozen.clear();
        if (pv.data[PubFreeze] > 0.5f) {
            QStringList names;
            for (int k = 0; k < kPubVoices; ++k) {
                const float *v = pv.data + PubVoice0 + kPubStride * k;
                const int f = int(v[0]) - 1;
                if (f < 0 || !(int(v[4]) & 4))
                    continue;
                const QString n = QStringLiteral("%1 %2").arg(QString::fromUtf8(kShape[f].name).toLower(),
                                                             QString::fromUtf8(colourName(f, v[2], int(v[4]) >> 4)));
                if (!names.contains(n))
                    names << n;
            }
            m_frozen = names.isEmpty() ? QStringLiteral("frozen: the halo hangs, F thaws")
                                       : QStringLiteral("frozen: %1 hanging, F thaws").arg(names.join(QStringLiteral(", ")));
        }
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const PartView &pv = ctx.studio().parts[ctx.part()];
        if (k.key == Qt::Key_Space) {
            // Space is the session's: it loops what was just played. Seeing
            // it first, the page offers its latched layers to the loop, which
            // takes their voices over as it starts, so the bed neither
            // doubles nor drops.
            if (k.down && !k.repeat && pv.recording < 0) {
                m_spaceAt = ctx.now();
                m_takesAtSpace = pv.takes;
                m_takeSlot = ctx.freeSlot();
                handOver(ctx);
            }
            return false;
        }
        if (k.key == Qt::Key_S) {
            if (k.repeat)
                return true;
            m_swellHeld = k.down;
            m_swells += k.down;
            swell(ctx);
            return true;
        }
        if (!k.down)
            return false;
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 cleared").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        if (k.key == Qt::Key_Backspace) {
            ctx.stop();
            return true;
        }
        // The steppers repeat while held; everything else is one press.
        switch (k.key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
            stepSpace(k.key == Qt::Key_Up ? 1 : -1, ctx);
            return true;
        case Qt::Key_Minus:
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            stepTone(k.key == Qt::Key_Minus ? -1 : 1, ctx);
            return true;
        default:
            break;
        }
        if (k.repeat)
            return true;
        for (int s = 0; s < kSceneCount; ++s)
            if (k.key == kSceneKeys[s] && shift == (s >= kShiftScenes)) {
                toggleScene(s, ctx);
                return true;
            }
        for (int c = 0; c < 7; ++c)
            if (k.key == kChordKeys[c]) {
                ownChord(c, ctx);
                return true;
            }
        // The shared keys first (the same on every part page, and reachable
        // without AltGr on a Nordic keyboard), then the keys they replaced,
        // which stay as second keys.
        switch (k.key) {
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (shift)
                stepPump(k.key == Qt::Key_Right ? 1 : -1, ctx);
            else
                stepScene(k.key == Qt::Key_Right ? 1 : -1, ctx);
            return true;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            stepPump(k.key == Qt::Key_BracketRight ? 1 : -1, ctx);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift)
                toggleOverdub(ctx);
            else if (!latchHeld(ctx))
                toggleHold(ctx);
            return true;
        case Qt::Key_section:   // left of 1 on Nordic and Mac layouts
        case Qt::Key_QuoteLeft: // and on US ones
        case Qt::Key_Backslash:
        case Qt::Key_Comma:
            follow(ctx);
            return true;
        case Qt::Key_Delete:
            releaseAll(ctx, QStringLiteral("Let go: everything latched fades"));
            return true;
        case Qt::Key_H:
            toggleHold(ctx);
            return true;
        case Qt::Key_F:
            toggleFreeze(ctx);
            return true;
        case Qt::Key_G:
            toggleCloud(ctx);
            return true;
        case Qt::Key_L:
            setStyle((m_style + (shift ? int(kStyles) - 1 : 1)) % int(kStyles), ctx);
            return true;
        default:
            return false;
        }
    }

    // The mouse on the band under the pad: the clips, the scenes and the
    // chords in it, and every setting a key reaches.
    enum Spot {
        SpotScene = 100,
        SpotChord = 120,
        SpotHold = 200,
        SpotFreeze,
        SpotCloud,
        SpotSwell,
        SpotPumpDown,
        SpotPumpUp,
        SpotSpaceDown,
        SpotSpaceUp,
        SpotToneDown,
        SpotToneUp,
        SpotFollow,
        SpotOverdub,
        SpotLetGo,
        SpotStyleNext,
        SpotStyle, // + the style
    };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
            return true;
        if (id >= SpotScene && id < SpotScene + kSceneCount) {
            toggleScene(id - SpotScene, ctx);
            return true;
        }
        if (id >= SpotChord && id < SpotChord + 7) {
            ownChord(id - SpotChord, ctx);
            return true;
        }
        switch (id) {
        case SpotHold: toggleHold(ctx); return true;
        case SpotFreeze: toggleFreeze(ctx); return true;
        case SpotCloud: toggleCloud(ctx); return true;
        case SpotSwell:
            // A click has no release to end a held swell, so the chip is a
            // toggle: on, the swell rises; a second click, or eight bars
            // (when it has risen all the way), lets it fall back.
            if (m_swellClicked) {
                m_swellClicked = false;
            } else {
                m_swellClicked = true;
                m_swellUntil = ctx.timeAt(ctx.beatAt(ctx.now()) + 8.0 * kBeatsPerBar);
                ++m_swells;
            }
            swell(ctx);
            ctx.notice(m_swellClicked ? QStringLiteral("Swelling: click again to let it fall back")
                                      : QStringLiteral("The swell falls back"));
            return true;
        case SpotPumpDown: stepPump(-1, ctx); return true;
        case SpotPumpUp: stepPump(1, ctx); return true;
        case SpotSpaceDown: stepSpace(-1, ctx); return true;
        case SpotSpaceUp: stepSpace(1, ctx); return true;
        case SpotToneDown: stepTone(-1, ctx); return true;
        case SpotToneUp: stepTone(1, ctx); return true;
        case SpotFollow: follow(ctx); return true;
        case SpotOverdub: toggleOverdub(ctx); return true;
        case SpotLetGo: releaseAll(ctx, QStringLiteral("Let go: everything latched fades")); return true;
        case SpotStyleNext: setStyle((m_style + 1) % int(kStyles), ctx); return true;
        default:
            if (id >= SpotStyle && id < SpotStyle + int(kStyles)) {
                setStyle(id - SpotStyle, ctx);
                return true;
            }
            return false;
        }
    }

    void leave(ControlContext &ctx) override {
        // Fingers let go; what was latched (by hand, click or scene) stays,
        // like a clip, until Delete or a long click lets it go.
        for (Held &h : m_held) {
            if (h.touch < 0)
                continue;
            if (h.cancelled) {
                h.touch = -1;
                continue;
            }
            if (h.grabbed) {
                if (Latched *l = findLatched(h.layer.id)) {
                    l->grabbed = false;
                    l->layer = h.layer;
                }
            } else if (h.latch) {
                m_latched.push_back({h.layer, h.x, h.y, -1, false, false});
            } else {
                sendOff(h.layer.id, ctx);
            }
            h.touch = -1;
        }
        if (m_swellHeld) {
            m_swellHeld = false;
            swell(ctx);
        }
        m_frame = Frame{};
    }

    // Ctrl+N, or a song opening: the studio lets every voice go, the
    // latched bed's among them, so the page forgets the old song's bed, its
    // scene and a take it was waiting for. The settings (space, tone, pump,
    // cloud, hold, the bed's own chord) stay as the player left them, as on
    // every page; only the freeze thaws, as the engine does.
    void startOver(ControlContext &ctx) override {
        (void)ctx;
        m_latched.clear();
        m_scene = -1;
        m_takeSlot = -1;
        m_handUntil = 0;
        m_lastPlaying = -1;
        m_swellClicked = false;
        m_settings.freeze = false;
    }

    // The bed as it was left: its settings, and the layers latched on it.
    // A latched bed is sound the player chose to leave on with no hand on
    // the page, like a held drone under the song, so it comes back latched:
    // a song saved over a bed would otherwise open without its floor. A
    // layer a loop is taking over is the loop's (its clip brings it back),
    // and what fingers hold now is the hand's moment, so neither is saved.
    // A layer only the freeze holds isn't on the page's books, so only the
    // freeze itself comes back. Choices from lists go by name (the layer's
    // sound, the scene, the pump), so a list reordered later still opens.
    void save(QJsonObject &out) const override {
        static const char *pumps[3] = {"off", "kick", "beat"};
        out[QStringLiteral("space")] = tidyFloat(m_settings.space);
        out[QStringLiteral("tone")] = tidyFloat(m_settings.tone);
        out[QStringLiteral("pump")] = QLatin1String(pumps[std::clamp(m_settings.pump, 0, 2)]);
        out[QStringLiteral("cloud")] = m_settings.cloud;
        out[QStringLiteral("freeze")] = m_settings.freeze;
        out[QStringLiteral("hold")] = m_hold;
        // The degree of the bed's own chord (0 is I), or -1 to follow the
        // Chords page.
        out[QStringLiteral("chord")] = m_settings.chord;
        if (m_scene >= 0)
            out[QStringLiteral("scene")] = QString::fromUtf8(kScenes[m_scene].name);
        // The style hand-laid layers take next (L), and each layer's own, so
        // a dub stab or a tape key comes back as one.
        out[QStringLiteral("style")] = QString::fromUtf8(kStyleName[m_style]);
        QJsonArray bed;
        for (const Latched &l : m_latched) {
            if (l.handed)
                continue;
            // An orb being reshaped is saved as it sounds now.
            Layer L = l.layer;
            for (const Held &h : m_held)
                if (l.grabbed && h.touch >= 0 && h.grabbed && h.layer.id == L.id)
                    L = h.layer;
            QJsonObject o;
            o[QStringLiteral("sound")] = QString::fromUtf8(kShape[std::clamp(L.family, 0, kFamilies - 1)].name);
            o[QStringLiteral("colour")] = tidyFloat(L.colour);
            o[QStringLiteral("height")] = tidyFloat(L.height);
            o[QStringLiteral("press")] = tidyFloat(L.press);
            o[QStringLiteral("motion")] = tidyFloat(L.motion);
            o[QStringLiteral("width")] = tidyFloat(L.width);
            if (L.style != Classic)
                o[QStringLiteral("style")] = QString::fromUtf8(kStyleName[std::clamp(L.style, 0, kStyles - 1)]);
            if (l.scene >= 0)
                o[QStringLiteral("scene")] = true;
            bed.append(o);
        }
        out[QStringLiteral("bed")] = bed;
    }

    // After startOver(): every setting is sent through the page's own
    // setter, the file's or the default, since the engine still has the
    // old song's. The latched layers sound again as Ons that carry on a
    // sound already heard, so Space never takes the opening for playing,
    // and the freeze goes out a moment after them, to hold them as it did.
    void load(const QJsonObject &in, ControlContext &ctx) override {
        auto number = [](const QJsonObject &o, const char *key, float lo, float hi, float home) {
            const QJsonValue v = o.value(QLatin1String(key));
            if (!v.isDouble() || !std::isfinite(v.toDouble()))
                return home;
            return float(std::clamp(v.toDouble(), double(lo), double(hi)));
        };
        auto flag = [](const QJsonObject &o, const char *key, bool home) {
            const QJsonValue v = o.value(QLatin1String(key));
            return v.isBool() ? v.toBool() : home;
        };
        auto same = [](const QJsonValue &v, const char *name) {
            return v.isString() && v.toString().compare(QString::fromUtf8(name), Qt::CaseInsensitive) == 0;
        };

        static const char *pumps[3] = {"off", "kick", "beat"};
        int pump = PumpKick;
        for (int i = 0; i < 3; ++i)
            if (same(in.value(QStringLiteral("pump")), pumps[i]))
                pump = i;
        param(PSpace, number(in, "space", 0, 1, 0.5f), ctx);
        param(PTone, number(in, "tone", 0, 1, 0.5f), ctx);
        param(PPump, float(pump), ctx);
        param(PCloud, flag(in, "cloud", false) ? 1.0f : 0.0f, ctx);
        // Unrecorded, unlike a chord key: opening a song isn't playing.
        param(PChord, float(std::lround(number(in, "chord", -1, 6, -1))), ctx);
        m_hold = flag(in, "hold", false);

        // A style this tenfold doesn't know is the classic one.
        auto styleOf = [&](const QJsonValue &v) {
            for (int st = 0; st < kStyles; ++st)
                if (same(v, kStyleName[st]))
                    return st;
            return int(Classic);
        };
        m_style = styleOf(in.value(QStringLiteral("style")));
        int scene = -1;
        for (int s = 0; s < kSceneCount; ++s)
            if (same(in.value(QStringLiteral("scene")), kScenes[s].name))
                scene = s;
        const double now = ctx.now();
        int count = 0;
        for (const QJsonValue &v : in.value(QStringLiteral("bed")).toArray()) {
            // No more layers than the engine has voices.
            if (!v.isObject() || count >= kVoices)
                continue;
            const QJsonObject o = v.toObject();
            int family = -1;
            for (int f = 0; f < kFamilies; ++f)
                if (same(o.value(QStringLiteral("sound")), kShape[f].name))
                    family = f;
            if (family < 0)
                continue; // a sound this tenfold doesn't have
            Layer L;
            L.id = nextId();
            L.family = family;
            L.colour = number(o, "colour", 0, 1, L.colour);
            L.height = number(o, "height", 0, 1, L.height);
            L.press = number(o, "press", 0, 1, L.press);
            L.motion = number(o, "motion", -1, 1, L.motion);
            L.width = number(o, "width", 0.35f, 1, L.width);
            L.style = styleOf(o.value(QStringLiteral("style")));
            const int from = scene >= 0 && flag(o, "scene", false) ? scene : -1;
            sendOn(L, ctx, now, Command::Continue);
            m_latched.push_back({L, xOf(L.family, L.colour), L.height, from, false, false});
            ++count;
        }
        // The scene shows lit only if some of its layers came back, so its
        // key fades them as it would have.
        if (std::any_of(m_latched.begin(), m_latched.end(), [](const Latched &l) { return l.scene >= 0; }))
            m_scene = m_lastScene = scene;
        param(PFreeze, flag(in, "freeze", false) ? 1.0f : 0.0f, ctx, now + 0.05);
    }

    void paint(QPainter &p, const PaintContext &pc) override;
    QString help() const override;
    QString status() const override;
    // For the tutorial: layers latched now (by click, hold or a scene), the
    // scene, and swells ever started with S.
    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("latched"))
            return float(m_latched.size());
        if (name == QLatin1String("scene"))
            return float(m_scene);
        if (name == QLatin1String("swells"))
            return float(m_swells);
        if (name == QLatin1String("style"))
            return float(m_style);
        return PartControl::tutorialProbe(name);
    }

private:
    struct Layer {
        int id = 0, family = 0, style = Classic;
        float colour = 0.5f, height = 0.5f, press = 0.5f, motion = 0, width = 0.75f;
        float attack = 0.4f;
        double since = 0; // when its On went out
    };
    struct Held {
        int touch = -1;
        Layer layer;
        double t0 = 0, sentT = 0;
        float x0 = 0, y0 = 0, a0 = 0;
        float ox = 0, oy = 0, om = 0, op = 0; // offsets for a grabbed orb
        float x = 0, y = 0, travel = 0;
        bool grabbed = false, latch = false, cancelled = false;
        Layer sent;
    };
    struct Latched {
        Layer layer;
        float x = 0, y = 0;
        int scene = -1;
        bool grabbed = false;
        bool handed = false; // a loop is taking it over
    };

    static QString roman(int degree) {
        static const char *r[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        return QString::fromUtf8(r[((degree % 7) + 7) % 7]);
    }

    // The loop on its way, from the studio's own view: a take waits for the
    // bar, and Space again calls it off.
    static QString loopCue(double beat, const PartView &pv) {
        if (pv.recording < 0)
            return {};
        const double next = std::ceil(beat / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        const int beats = std::max(0, int(std::ceil(next - beat - 1e-3)));
        const QString when = beats == 0 ? QStringLiteral("NOW")
                                        : beats == 1 ? QStringLiteral("IN 1 BEAT") : QStringLiteral("IN %1 BEATS").arg(beats);
        return QStringLiteral("LOOP INTO SLOT %1 · %2 · SPACE CALLS IT OFF").arg(pv.recording + 1).arg(when);
    }

    int nextId() {
        m_nextId = m_nextId >= 0xFFFFF ? 1 : m_nextId + 1;
        return m_nextId;
    }
    Held *findTouch(int touch) {
        for (Held &h : m_held)
            if (h.touch == touch)
                return &h;
        return nullptr;
    }
    Latched *findLatched(int id) {
        for (Latched &l : m_latched)
            if (l.layer.id == id)
                return &l;
        return nullptr;
    }

    // Global settings are the hand's, never a loop's: they go unrecorded.
    // The page keeps what it sent, for save() (t: when, if not now).
    void param(int which, float value, ControlContext &ctx, double t = 0) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = which;
        c.f[0] = value;
        c.t = t > 0 ? t : ctx.now();
        ctx.send(c);
        remember(which, value, c.t);
    }
    void remember(int which, float value, double t) {
        switch (which) {
        case PFreeze: m_settings.freeze = value > 0.5f; break;
        case PCloud: m_settings.cloud = value > 0.5f; break;
        case PPump: m_settings.pump = std::clamp(int(std::lround(value)), 0, 2); break;
        case PSpace: m_settings.space = clampf(value, 0.0f, 1.0f); break;
        case PTone: m_settings.tone = clampf(value, 0.0f, 1.0f); break;
        case PChord: m_settings.chord = std::clamp(int(std::lround(value)), -1, 6); break;
        case PChordToLoop: m_settings.chord = -1; break;
        default: return;
        }
        m_settingsSentAt = std::max(m_settingsSentAt, t);
    }
    // The bed's own chord is recorded, so a loop can carry a progression.
    void chord(int degree, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::Quantize; // a chord change is played, and Space hears it
        c.i[0] = PChord;
        c.f[0] = float(degree);
        c.t = ctx.now();
        ctx.send(c);
        remember(PChord, c.f[0], c.t);
        m_onsets[m_onsetHead++ % m_onsets.size()] = {c.t, true};
    }
    void sendOn(Layer &l, ControlContext &ctx, double t, uint16_t flags = 0) {
        Command c;
        c.type = Command::On;
        c.flags = flags;
        c.id = l.id;
        c.t = t;
        c.i[0] = l.family;
        c.i[1] = l.style;
        c.f[0] = l.height;
        c.f[1] = l.colour;
        c.f[2] = l.press;
        c.f[3] = l.motion;
        c.f[4] = l.width;
        c.f[5] = flags & Command::Continue ? 0.3f : l.attack;
        ctx.send(c);
        if (!(flags & Command::Continue))
            l.since = t;
        m_onsets[m_onsetHead++ % m_onsets.size()] = {t, !(flags & Command::Continue)};
    }
    void sendMove(const Layer &l, ControlContext &ctx, double t) {
        Command c;
        c.type = Command::Move;
        c.id = l.id;
        c.t = t;
        c.f[0] = l.height;
        c.f[1] = l.colour;
        c.f[2] = l.press;
        c.f[3] = l.motion;
        c.f[4] = l.width;
        ctx.send(c);
    }
    void sendOff(int id, ControlContext &ctx, double t = 0) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        c.f[7] = 1; // a lift, not the end of a take
        c.t = t > 0 ? t : ctx.now();
        ctx.send(c);
    }

    // Latched layers are marked for the loop to take over.
    void handOver(ControlContext &ctx) {
        for (Latched &l : m_latched)
            if (!l.grabbed) {
                Command c;
                c.type = kAdoptable;
                c.flags = Command::NoRecord;
                c.id = l.layer.id;
                ctx.send(c);
                l.handed = true;
            }
    }

    int hitOrb(float x, float y, float aspect) const {
        int best = -1;
        float bestD = 0.06f;
        for (size_t i = 0; i < m_latched.size(); ++i) {
            const Latched &l = m_latched[i];
            if (l.grabbed || l.handed)
                continue;
            const float dx = (x - l.x) * aspect, dy = y - l.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < bestD) {
                bestD = d;
                best = int(i);
            }
        }
        return best;
    }

    void land(const Touch &t, double now, ControlContext &ctx) {
        Held *h = findTouch(-1);
        if (!h)
            return;
        *h = Held{};
        h->touch = t.id;
        h->t0 = now;
        h->x0 = h->x = t.x;
        h->y0 = h->y = t.y;
        h->a0 = t.angle;
        const int orb = hitOrb(t.x, t.y, std::max(0.5f, ctx.aspect()));
        if (orb >= 0) {
            // A latched layer, picked up where it lies.
            Latched &l = m_latched[size_t(orb)];
            l.grabbed = true;
            h->grabbed = true;
            h->layer = l.layer;
            h->ox = l.x - t.x;
            h->oy = l.y - t.y;
            h->om = l.layer.motion;
            h->op = l.layer.press - t.pressure;
            h->x = l.x;
            h->y = l.y;
            h->sent = h->layer;
            h->sentT = now;
            return;
        }
        Layer &L = h->layer;
        L.id = nextId();
        L.family = familyAt(t.x);
        L.style = style();
        L.colour = colourAt(L.family, t.x);
        L.height = clampf(t.y, 0, 1);
        L.press = clampf(t.pressure, 0, 1);
        L.motion = 0;
        L.width = widthOf(t.size);
        L.attack = attackFor(t.pressure);
        sendOn(L, ctx, now);
        h->sent = L;
        h->sentT = now;
    }

    void follow(Held &h, const Touch &t, double now, ControlContext &ctx) {
        if (h.cancelled)
            return;
        Layer &L = h.layer;
        const float colLo = float(L.family) / kFamilies, colHi = float(L.family + 1) / kFamilies;
        h.x = clampf(t.x + h.ox, colLo, colHi);
        h.y = clampf(t.y + h.oy, 0, 1);
        L.colour = colourAt(L.family, h.x);
        L.height = h.y;
        L.press = clampf(t.pressure + h.op, 0, 1);
        // Twist from where the finger landed; the ellipse's angle is only
        // defined up to a half turn.
        float da = t.angle - h.a0;
        while (da > kPi * 0.5f)
            da -= kPi;
        while (da < -kPi * 0.5f)
            da += kPi;
        // Clockwise is positive; full motion at about thirty degrees, and up
        // to twice that from a grabbed layer's own, so a pulsing orb can be
        // turned all the way round to a sweep.
        const float deg = -da * 180.0f / kPi;
        const float tw = (deg > 0 ? 1.0f : -1.0f) * clampf((std::fabs(deg) - 4.0f) / 26.0f, 0.0f, 2.0f);
        L.motion = clampf(h.om + tw, -1, 1);
        L.width = widthOf(t.size);
        h.travel = std::max(h.travel, std::hypot(t.x - h.x0, t.y - h.y0));
        if (m_hold && !h.grabbed && now - h.t0 >= 0.3)
            h.latch = true;

        const Layer &s = h.sent;
        const bool changed = std::fabs(L.colour - s.colour) > 0.005f || std::fabs(L.height - s.height) > 0.005f ||
                             std::fabs(L.press - s.press) > 0.008f || std::fabs(L.motion - s.motion) > 0.01f ||
                             std::fabs(L.width - s.width) > 0.01f;
        if (changed && now - h.sentT >= 1.0 / 60.0) {
            sendMove(L, ctx, now);
            h.sent = L;
            h.sentT = now;
        }
    }

    void lift(Held &h, double now, ControlContext &ctx) {
        if (h.cancelled) {
            h.touch = -1;
            return;
        }
        const Layer &L = h.layer;
        const Layer &s = h.sent;
        if (s.height != L.height || s.colour != L.colour || s.press != L.press || s.motion != L.motion || s.width != L.width)
            sendMove(L, ctx, now);
        if (h.grabbed) {
            Latched *l = findLatched(L.id);
            if (now - h.t0 < 0.25 && h.travel < 0.03f) {
                // A tap on an orb lets that layer go.
                sendOff(L.id, ctx, now);
                if (l)
                    m_latched.erase(m_latched.begin() + (l - m_latched.data()));
            } else if (l) {
                l->grabbed = false;
                l->layer = L;
                l->x = h.x;
                l->y = h.y;
            }
        } else if (h.latch) {
            m_latched.push_back({L, h.x, h.y, -1, false, false});
        } else {
            sendOff(L.id, ctx, now);
        }
        h.touch = -1;
    }

    // Lets every latched layer go, except what a loop is taking over.
    void releaseAll(ControlContext &ctx, const QString &message) {
        for (const Latched &l : m_latched)
            if (!l.grabbed && !l.handed)
                sendOff(l.layer.id, ctx);
        m_latched.erase(std::remove_if(m_latched.begin(), m_latched.end(),
                                       [](const Latched &l) { return !l.grabbed && !l.handed; }),
                        m_latched.end());
        m_scene = -1;
        ctx.notice(message);
    }

    void toggleScene(int s, ControlContext &ctx) {
        const bool same = m_scene == s;
        for (const Latched &l : m_latched)
            if (l.scene >= 0 && !l.grabbed && !l.handed)
                sendOff(l.layer.id, ctx);
        m_latched.erase(std::remove_if(m_latched.begin(), m_latched.end(),
                                       [](const Latched &l) { return l.scene >= 0 && !l.grabbed && !l.handed; }),
                        m_latched.end());
        m_scene = -1;
        if (same) {
            // A styled scene faded with its own key takes its style with it.
            if (kScenes[s].style != Classic && m_style == kScenes[s].style) {
                m_style = Classic;
                ctx.notice(QStringLiteral("%1 fades; fingers play classic layers again").arg(QString::fromUtf8(kScenes[s].name)));
            } else {
                ctx.notice(QStringLiteral("%1 fades").arg(QString::fromUtf8(kScenes[s].name)));
            }
            return;
        }
        const Scene &sc = kScenes[s];
        for (int i = 0; i < sc.count; ++i) {
            const SceneLayer &sl = sc.layers[i];
            Layer L;
            L.id = nextId();
            L.family = sl.family;
            L.style = sc.style;
            L.colour = sl.colour;
            L.height = sl.height;
            L.press = sl.press;
            L.motion = sl.motion;
            L.width = sl.width;
            L.attack = sc.attack;
            sendOn(L, ctx, ctx.now());
            m_latched.push_back({L, xOf(L.family, L.colour), L.height, s, false, false});
        }
        m_scene = s;
        m_lastScene = s;
        // A styled scene sets the style the fingers play in too.
        if (sc.style != Classic) {
            m_style = sc.style;
            ctx.notice(QStringLiteral("%1: fingers play %2 layers too, L or the chips change it")
                           .arg(QString::fromUtf8(sc.name), QString::fromUtf8(kStyleName[sc.style]).toLower()));
        } else {
            ctx.notice(QString::fromUtf8(sc.name));
        }
    }

    // The style a finger's layer takes, shown lit in the band: set by L
    // (Shift+L back) or its chips, by laying a styled scene, and back to
    // classic when that scene is faded with its own key.
    int style() const { return m_style; }
    void setStyle(int st, ControlContext &ctx) {
        m_style = std::clamp(st, 0, kStyles - 1);
        static const char *what[kStyles] = {"the PADsynth bed as built", "synthwave, through the ensemble chorus",
                                            "dub techno, chord stabs into the tape echo",
                                            "lo-fi, felt keys and every layer on a worn cassette",
                                            "Balinese bronze in shimmering pairs on a pelog five, interlocking, the kempli and the gong",
                                            "a tanpura's buzzing round on the key's Sa, and sympathetic strings to the chords",
                                            "a Shona thumb piano in its gourd, bass tines and the bottle caps rattling",
                                            "a DX electric piano on rootless elevenths, tremolo and chorus; twist it for the band",
                                            "Tuvan throat singing: a drone on the tonic, a whistle singing its overtones"};
        ctx.notice(QStringLiteral("%1 layers: %2").arg(QString::fromUtf8(kStyleName[m_style]), QString::fromUtf8(what[m_style])));
    }

    // ← →: the next or previous scene, from the one laid (or last laid).
    void stepScene(int dir, ControlContext &ctx) {
        const int from = m_scene >= 0 ? m_scene : m_lastScene;
        toggleScene(from < 0 ? (dir > 0 ? 0 : kSceneCount - 1) : (from + dir + kSceneCount) % kSceneCount, ctx);
    }

    // What the engine says it is doing now.
    static const float *pub(ControlContext &ctx) { return ctx.studio().parts[ctx.part()].data; }

    // The swell rises while S is held or the mouse's toggle is on.
    void swell(ControlContext &ctx) { param(PSwell, m_swellHeld || m_swellClicked ? 1.0f : 0.0f, ctx); }

    void stepSpace(int dir, ControlContext &ctx) {
        const float s = clampf(std::round(pub(ctx)[PubSpace] * 4.0f) / 4.0f + 0.25f * float(dir), 0.0f, 1.0f);
        param(PSpace, s, ctx);
        ctx.notice(QStringLiteral("Space %1").arg(int(std::lround(s * 4))));
    }

    void stepTone(int dir, ControlContext &ctx) {
        const float s = clampf(std::round(pub(ctx)[PubTone] * 8.0f) / 8.0f + 0.125f * float(dir), 0.0f, 1.0f);
        param(PTone, s, ctx);
        const int step = int(std::lround(s * 8)) - 4;
        ctx.notice(step == 0 ? QStringLiteral("Tone as built")
                             : QStringLiteral("Tone %1%2: %3").arg(step > 0 ? "+" : "").arg(step)
                                   .arg(step > 0 ? QStringLiteral("brighter") : QStringLiteral("darker")));
    }

    void stepPump(int dir, ControlContext &ctx) {
        const int mode = (int(std::lround(pub(ctx)[PubPump])) + dir + 3) % 3;
        param(PPump, float(mode), ctx);
        static const char *names[3] = {"Pump off", "Pump with the kick", "Pump on every beat"};
        ctx.notice(QString::fromUtf8(names[mode]));
    }

    void ownChord(int c, ControlContext &ctx) {
        const bool loopHad = pub(ctx)[PubOwnLoop] > 0.5f;
        chord(c, ctx);
        ctx.notice(QStringLiteral("The bed's own chord %1: only Atmos plays it%2. § follows the Chords page")
                       .arg(roman(c), loopHad ? QStringLiteral(", over the loop's chords") : QString()));
    }

    void follow(ControlContext &ctx) {
        chord(-1, ctx);
        ctx.notice(QStringLiteral("The bed follows the chords"));
    }

    void toggleHold(ControlContext &ctx) {
        m_hold = !m_hold;
        ctx.notice(m_hold ? QStringLiteral("Hold: layers held a moment stay when lifted") : QStringLiteral("Hold off"));
    }

    // Return with fingers down latches them, as the click does. False if
    // there was nothing to latch.
    bool latchHeld(ControlContext &ctx) {
        int latched = 0;
        for (Held &h : m_held)
            if (h.touch >= 0 && !h.grabbed && !h.cancelled && !h.latch) {
                h.latch = true;
                ++latched;
            }
        if (latched)
            ctx.notice(QStringLiteral("Latched: it stays when you lift. Delete lets everything go"));
        return latched > 0;
    }

    void toggleFreeze(ControlContext &ctx) {
        const bool on = pub(ctx)[PubFreeze] < 0.5f;
        param(PFreeze, on ? 1.0f : 0.0f, ctx);
        ctx.notice(on ? QStringLiteral("Frozen: the bed holds, lifted or not, and follows the chords")
                      : QStringLiteral("Thawed"));
    }

    void toggleCloud(ControlContext &ctx) {
        const bool on = pub(ctx)[PubCloud] < 0.5f;
        param(PCloud, on ? 1.0f : 0.0f, ctx);
        ctx.notice(on ? QStringLiteral("Grain cloud") : QStringLiteral("Cloud off"));
    }

    void toggleOverdub(ControlContext &ctx) {
        const bool on = !ctx.studio().parts[ctx.part()].overdub;
        ctx.overdub(on);
        ctx.notice(on ? QStringLiteral("Overdub: what you play joins the loop") : QStringLiteral("Overdub off"));
    }

    std::array<Held, 16> m_held{};
    std::vector<Latched> m_latched;
    struct Onset {
        double t = 0;
        bool real = true;
    };
    std::array<Onset, 256> m_onsets{};
    uint32_t m_takesAtSpace = 0;
    size_t m_onsetHead = 0;
    Frame m_frame;
    QString m_cue, m_frozen, m_playing;
    double m_buttonAt = 0, m_handUntil = 0;
    int m_nextId = 0, m_scene = -1, m_lastScene = -1, m_takeSlot = -1, m_lastPlaying = -1, m_style = Classic;
    double m_spaceAt = 0, m_swellUntil = 0;
    bool m_button = false, m_longDone = false, m_hold = false, m_swellHeld = false, m_swellClicked = false;
    console::Hotspots m_spots; // what paint() drew to be clicked
    uint32_t m_swells = 0; // for the tutorial
    // The bed's settings as last sent or seen in the engine, for save().
    struct Settings {
        bool freeze = false, cloud = false;
        int pump = PumpKick;
        float space = 0.5f, tone = 0.5f;
        int chord = -1; // the bed's own chord by hand, or -1 to follow
    } m_settings;
    double m_settingsSentAt = 0;
};

QString AtmosControl::help() const {
    return QStringLiteral(
        "← →\tthe scenes, one after another: dawn, ocean, cathedral, stardust, nostalgia (lo-fi tape), euphoria, night rain, pulse, "
        "neon, dub, then on past the letters to gamelan, raga, mbira, city pop, khoomei\n"
        "↑ ↓\tspace: the halo's size and shimmer\n"
        "Shift+← →\tthe pump: off, with the kick, on every beat ([ ] step it too)\n"
        "- +\ttone: darker, brighter (= too)\n"
        "Shift+Return\twith fingers down, latch them, as the click does; otherwise hold on or off: layers held a moment stay when lifted (H too)\n"
        "F10\toverdub\n"
        "§ (the key left of 1; ` on US keyboards)\tfollow the Chords page again (\\ and comma too)\n"
        "Delete\tlet every latched layer go\n"
        "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot at the next bar · clear it · stop the clip at the next bar\n"
        "the band under the pad\tthe mouse does what the keys do: click a clip to launch or stop it, a scene (Q to P on the first "
        "row, Shift+Q to T on the second), the bed's own chord, a style (L before them steps on), and every setting; a "
        "stepper's left half goes down, its right half up; the swell chip is a toggle, and it ends by itself after eight bars\n"
        "touch\ta layer of the bed, in the chord: drone, pad, choir, glass, air from left to right\n"
        "slide up or down\tthe voicing climbs or falls through the chord, brighter as it rises (khoomei: the melody sings higher)\n"
        "slide across a column\tthe colour: soft to bowed, strings to saw to hollow (neon: strings to poly; dub: a long wash "
        "to a short stab; tape: felt to bright keys; gamelan: the pokok a beat at a time, norot in eighths, kotekan in "
        "interlocking sixteenths; raga: a soft tanpura to its bridge buzzing bright; mbira: soft tines to sizzling caps; city "
        "pop: a mellow tine, the suitcase tremolo, a bell; khoomei: khoomei, soft and open, sygyt, the whistle, kargyraa, an "
        "octave down and growling), oo to ee, pure glass to twinkles to stars, wind to rain to vinyl\n"
        "press\tswells the layer; a firm landing sounds at once, a light one fades in (struck pads strike at once, harder "
        "the firmer)\n"
        "twist\tclockwise, a slow filter sweep with the bars; anticlockwise, a gate in eighths, then sixteenths (a dub "
        "chord's stabs roll 3-3-2 instead; tape keys comp; the city-pop band comes in, the piano comping a two-bar push "
        "over a fingered bass with ghost notes and approach notes, a swung shaker and a rim on two and four; the gamelan "
        "plays a level busier, at kotekan the ceng-ceng crash in, and twisted past three quarters it plays one angsel from "
        "the next beat, two accents and a whole beat of silence, the room's too; the mbira's second part joins a pulse "
        "behind with a tune of its own; the tanpura goes round in one bar; the throat singer "
        "gallops, the ezengileer)\n"
        "flat finger\twider\n"
        "tap\ta whole breath: the swell finishes, then fades\n"
        "click\tlatch every layer held, a new one too: it stays when lifted\n"
        "hold the click down a second\tlet every latched layer go (so does Delete)\n"
        "touch an orb\tpick a latched layer up and reshape it; a quick tap lets it go\n"
        "Space\tloop what was just played, latched bed and all; the pad shows the loop waiting for its bar; Space again calls it off\n"
        "Q W E R T Y U I O P\tscenes: dawn, ocean, cathedral, stardust, nostalgia (lo-fi tape: vinyl, felt keys and a "
        "drone on a worn cassette), euphoria, night rain, pulse, "
        "neon (synthwave: a string machine and a poly through an ensemble chorus), dub (dub techno: chord stabs on the "
        "offbeats into a tape echo, hiss and crackle); the same key again fades it\n"
        "Shift+Q W E R T\tfive more scenes: gamelan (Balinese bronze in pairs tuned to shimmer on a pelog five of the key, "
        "polos and sangsih interlocking in turns into each coming pokok tone, the kempli on the beat, the kemong and the "
        "kempur through the gongan, a great gong every four bars), raga (a tanpura's round on the key's Sa, its buzz "
        "climbing through the harmonics as each string dies on its curved bridge, a bowed drone, a hum of voices and "
        "sympathetic strings following the chords), mbira (two Shona thumb pianos interlocking in twelve-eight round a "
        "forty-eight-pulse cycle of their own, long bass tines under the left thumb, bottle caps chattering, in a gourd), "
        "city pop (a DX electric piano on rootless elevenths comping a push over a fingered bass, a shaker and a rim, "
        "tremolo and chorus), khoomei (two Tuvan throat singers, a breathing sygyt whistle singing the key's overtones "
        "over a kargyraa growl an octave down, an echo off the valley); the same keys again fade them\n"
        "Z X C V B N M\tthe bed's own chord on degrees I to VII (Atmos only); Space loops a progression with the bed, and a key pressed over the loop takes over until §\n"
        "F\tfreeze: every layer sounding holds, lifted or not, and follows the chords; again to thaw\n"
        "G\tgrain cloud of the bed, in octaves\n"
        "L\tthe style fingers play in (Shift+L goes back): classic, neon (a string machine and a pulse-width poly "
        "through an ensemble chorus), dub (minor-ninth chord stabs on the offbeats into a tape echo), tape (lo-fi: felt "
        "keys, and every layer on a worn cassette, wow, flutter and hiss), gamelan, raga, mbira, city pop, khoomei (the "
        "pad becomes each one's instrument, and the other layers go through its bus: the raga's sympathetic strings, the "
        "mbira's gourd, the city-pop chorus, the khoomei valley's echo); laying a styled scene sets it, fading that scene "
        "with its own key sets it back; ↑ makes the tape more seasick and the raga's strings ring longer; the style chips "
        "too, a window that follows the lit one, the L chip before it stepping on\n"
        "S, held\tswell: louder, brighter, more shimmer, rising for as long as it is held (up to eight bars)");
}

QString AtmosControl::status() const {
    QStringList parts;
    for (const Held &h : m_held)
        if (h.touch >= 0 && !h.cancelled)
            parts << QStringLiteral("%1 %2").arg(QString::fromUtf8(kShape[h.layer.family].name),
                                                QString::fromUtf8(colourName(h.layer.family, h.layer.colour, h.layer.style)));
    QString s = parts.join(QStringLiteral(", "));
    auto add = [&](const QString &t) { s += (s.isEmpty() ? QString() : QStringLiteral(" · ")) + t; };
    if (!m_latched.empty())
        add(QStringLiteral("%1 latched").arg(m_latched.size()));
    if (m_scene >= 0)
        add(QString::fromUtf8(kScenes[m_scene].name));
    else if (m_style != Classic)
        add(QStringLiteral("%1 layers").arg(QString::fromUtf8(kStyleName[m_style]).toLower()));
    if (m_hold)
        add(QStringLiteral("hold"));
    if (m_swellClicked)
        add(QStringLiteral("swelling, a click on SWELL ends it"));
    if (!m_playing.isEmpty())
        add(m_playing);
    if (!m_frozen.isEmpty())
        add(m_frozen);
    if (!m_cue.isEmpty())
        add(m_cue.toLower());
    if (s.isEmpty())
        s = QStringLiteral("Lay a finger anywhere: the bed is always in the chord · Space loops what you play");
    return s;
}

void AtmosControl::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    c::pad(p, pc, QStringLiteral("Atmos"));
    // The scene playing, or with none the style hand-laid layers play in.
    c::variation(p, pc, m_scene >= 0 ? QString::fromUtf8(kScenes[m_scene].name) : QString::fromUtf8(kStyleName[m_style]));
    const float *d = pc.view->data;
    const QColor col = pc.color;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(pc.pad.adjusted(1, 1, -1, -1));

    Tuning tn;
    tn.set(pc.studio->root, pc.studio->family, pc.studio->brightness, pc.studio->just);
    const int per = tn.degreesPerOctave();
    const int root = int(std::lround(d[PubRoot]));
    const int count = std::clamp(int(std::lround(d[PubCount])), 1, 4);
    int tones[4];
    for (int i = 0; i < 4; ++i)
        tones[i] = int(std::lround(d[PubTones + i]));
    auto midiOf = [&](int deg) { return float(tn.baseMidi() + tn.degreeSemis(deg)); };

    // Columns: a faint glow rising from the floor, the name below, the
    // colour axis above, and the voicing staircase: a tick wherever a chord
    // tone sits at the window's bottom, so the eye sees where the next
    // inversion comes in.
    const QFont tiny = c::font(std::max(9, int(10 * pc.scale)), false);
    for (int f = 0; f < kFamilies; ++f) {
        const QRectF colR = pc.rect(float(f) / kFamilies, 0, float(f + 1) / kFamilies, 1);
        QLinearGradient g(colR.bottomLeft(), colR.topLeft());
        const QColor fc = familyColor(col, f);
        g.setColorAt(0, c::alpha(fc, 0.10f));
        g.setColorAt(0.5, c::alpha(fc, 0.03f));
        g.setColorAt(1, c::alpha(fc, 0.0f));
        p.fillRect(colR, g);
        const FamilyShape &fs = kShape[f];
        c::text(p, QRectF(colR.left(), colR.bottom() - 26, colR.width(), 20), QString::fromUtf8(fs.name),
                c::alpha(fc, 0.85f), std::max(11, int(14 * pc.scale)), true, Qt::AlignCenter);
        const QString axis = QStringLiteral("%1 ‹ › %2").arg(QString::fromUtf8(colourAxis(f, style(), 0)),
                                                            QString::fromUtf8(colourAxis(f, style(), 2)));
        c::text(p, QRectF(colR.left(), colR.bottom() - 42, colR.width(), 16), axis, c::alpha(fc, 0.45f),
                std::max(9, int(10 * pc.scale)), false, Qt::AlignCenter);
        p.setFont(tiny);
        const bool drone = f == Drone;
        for (int k = -3; k <= 9; ++k)
            for (int i = 0; i < count; ++i) {
                const int deg = tones[i] + per * k;
                if (drone) {
                    double iv = tn.degreeSemis(tones[i]) - tn.degreeSemis(root);
                    iv -= 12.0 * std::floor(iv / 12.0);
                    if (tones[i] != root && std::fabs(iv - 7.0) > 0.3)
                        continue;
                }
                const float m = midiOf(deg);
                const float y = (m - fs.low) / (fs.high - fs.low);
                if (y < 0.1f || y > 0.88f)
                    continue;
                const bool isRoot = ((deg - root) % per + per) % per == 0;
                const QPointF a = pc.at(float(f) / kFamilies, y);
                p.setPen(QPen(c::alpha(fc, isRoot ? 0.6f : 0.3f), 1.2));
                p.drawLine(a, a + QPointF(isRoot ? 14 : 8, 0));
                p.setPen(c::alpha(fc, isRoot ? 0.6f : 0.35f));
                const int pc12 = ((int(std::lround(m)) % 12) + 12) % 12;
                p.drawText(QRectF(a.x() + 16, a.y() - 8, 40, 16), Qt::AlignLeft | Qt::AlignVCenter,
                           QString::fromUtf8(Tuning::noteName(pc12)));
            }
    }
    c::grid(p, pc, kFamilies, 1, col, 0.22f);

    // The halo: a sky glow over the whole pad, as bright as the bed's own
    // space is full, icy when frozen, breathing with the pump.
    const float pumpDip = 1.0f - 0.5f * d[PubPumpEnv];
    const bool frozen = d[PubFreeze] > 0.5f;
    {
        const float glow = clampf(d[PubHalo] * 9.0f, 0.0f, 1.0f) * pumpDip;
        if (glow > 0.01f) {
            const QColor sky = frozen ? QColor("#BFE6FF") : col;
            QLinearGradient g(pc.at(0.5f, 1.0f), pc.at(0.5f, 0.35f));
            g.setColorAt(0, c::alpha(sky, 0.3f * glow));
            g.setColorAt(0.5, c::alpha(sky, 0.07f * glow));
            g.setColorAt(1, c::alpha(sky, 0.0f));
            p.fillRect(pc.rect(0, 0.35f, 1, 1), g);
            // Slow light waves along the top, one per octave of shimmer.
            p.setBrush(Qt::NoBrush);
            for (int w = 0; w < 3; ++w) {
                QPainterPath wave;
                for (int s = 0; s <= 48; ++s) {
                    const float u = float(s) / 48.0f;
                    const float y = 0.86f - 0.05f * float(w) +
                                    0.025f * glow * float(std::sin(pc.time * (0.3 + 0.13 * w) + u * (5.0 + 2.0 * w) + w));
                    const QPointF pt = pc.at(u, y);
                    if (s == 0)
                        wave.moveTo(pt);
                    else
                        wave.lineTo(pt);
                }
                p.setPen(QPen(c::alpha(sky, (0.25f - 0.06f * float(w)) * glow), 1.4));
                p.drawPath(wave);
            }
        }
    }

    // Frozen: frost over everything.
    if (frozen) {
        p.fillRect(pc.pad, c::alpha(QColor("#BFE6FF"), 0.05f));
        p.setPen(QPen(c::alpha(QColor("#BFE6FF"), 0.06f), 1));
        for (int i = 1; i < 24; ++i) {
            const qreal y = pc.pad.top() + pc.pad.height() * i / 24.0;
            p.drawLine(QPointF(pc.pad.left(), y), QPointF(pc.pad.right(), y));
        }
    }

    // Every sounding layer as an aurora: a curtain of light rising from its
    // height over the span of its voicing, swaying with its breath, brighter
    // as it swells, cut by its gate when it pulses.
    p.setPen(Qt::NoPen);
    for (int k = 0; k < kPubVoices; ++k) {
        const float *v = d + PubVoice0 + kPubStride * k;
        const int f = int(v[0]) - 1;
        if (f < 0 || f >= kFamilies)
            continue;
        const float level = std::min(1.2f, v[3]) * v[7] * pumpDip;
        if (level < 0.01f)
            continue;
        const FamilyShape &fs = kShape[f];
        const float x = xOf(f, v[2]), y = v[1];
        const float top = std::min(1.0f, y + fs.span / (fs.high - fs.low) * 0.55f);
        const float halfW = (0.018f + 0.03f * v[6]) * (0.7f + 0.3f * level);
        const double t = pc.time * (0.35 + 0.25 * std::fabs(v[5])) + k * 1.7;
        QPainterPath path;
        constexpr int steps = 14;
        for (int s = 0; s <= steps; ++s) {
            const float u = float(s) / steps;
            const float yy = y - 0.03f + (top - y + 0.03f) * u;
            const float sway = 0.012f * float(std::sin(t + u * 3.1)) + 0.006f * float(std::sin(2.3 * t + u * 7.0));
            const QPointF pt = pc.at(x - halfW * (1.0f - 0.5f * u) + sway, yy);
            if (s == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        for (int s = steps; s >= 0; --s) {
            const float u = float(s) / steps;
            const float yy = y - 0.03f + (top - y + 0.03f) * u;
            const float sway = 0.012f * float(std::sin(t + u * 3.1)) + 0.006f * float(std::sin(2.3 * t + u * 7.0 + 1.0));
            path.lineTo(pc.at(x + halfW * (1.0f - 0.5f * u) + sway, yy));
        }
        path.closeSubpath();
        const QColor fc = familyColor(col, f);
        QLinearGradient g(pc.at(x, y - 0.03f), pc.at(x, top));
        g.setColorAt(0, c::alpha(fc, 0.0f));
        g.setColorAt(0.12, c::alpha(fc, 0.55f * level));
        g.setColorAt(0.5, c::alpha(fc, 0.25f * level));
        g.setColorAt(1, c::alpha(fc, 0.0f));
        p.setBrush(g);
        p.drawPath(path);
        // A bright seam at the voicing's bottom: where the layer sits.
        p.setPen(QPen(c::alpha(fc, std::min(1.0f, 1.5f * level)), 2));
        p.drawLine(pc.at(x - halfW, y), pc.at(x + halfW, y));
        p.setPen(Qt::NoPen);
    }

    // Glints: twinkles and drops.
    for (int g = 0; g < kPubGlints; ++g) {
        const float a = d[PubGlint0 + 3 * g + 2];
        if (a < 0.02f)
            continue;
        const QPointF at = pc.at(d[PubGlint0 + 3 * g], d[PubGlint0 + 3 * g + 1]);
        const qreal r = 3 + 6 * a;
        QRadialGradient rg(at, r * 2);
        rg.setColorAt(0, c::alpha(QColor("#E8F6FF"), 0.9f * a));
        rg.setColorAt(1, c::alpha(QColor("#E8F6FF"), 0));
        p.setBrush(rg);
        p.drawEllipse(at, r * 2, r * 2);
        p.setPen(QPen(c::alpha(QColor("#E8F6FF"), 0.7f * a), 1));
        p.drawLine(at - QPointF(r * 1.6, 0), at + QPointF(r * 1.6, 0));
        p.drawLine(at - QPointF(0, r * 1.6), at + QPointF(0, r * 1.6));
        p.setPen(Qt::NoPen);
    }

    // Latched layers: orbs that can be picked up.
    const qreal orbR = pc.pad.height() * 0.035;
    for (const Latched &l : m_latched) {
        if (l.grabbed)
            continue;
        const QColor fc = familyColor(col, l.layer.family);
        const QPointF at = pc.at(l.x, l.y);
        const float pulse = 0.5f + 0.5f * float(std::sin(pc.time * 1.3 + l.layer.id));
        QRadialGradient rg(at, orbR * 1.8);
        rg.setColorAt(0, c::alpha(fc, 0.35f + 0.15f * pulse));
        rg.setColorAt(1, c::alpha(fc, 0));
        p.setBrush(rg);
        p.setPen(Qt::NoPen);
        p.drawEllipse(at, orbR * 1.8, orbR * 1.8);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(fc, 1.6));
        p.drawEllipse(at, orbR, orbR);
        if (l.scene >= 0)
            p.drawEllipse(at, orbR * 0.45, orbR * 0.45);
        // A layer a loop is taking over wears the loop's square.
        if (l.handed)
            p.drawRect(QRectF(at.x() - orbR * 0.4, at.y() - orbR * 0.4, orbR * 0.8, orbR * 0.8));
        // Its name above it, clear of the seam at its height.
        c::text(p, QRectF(at.x() - 50, at.y() - orbR - 18, 100, 14),
                QString::fromUtf8(colourName(l.layer.family, l.layer.colour, l.layer.style)) + (l.handed ? QStringLiteral(" · loop") : QString()),
                c::alpha(fc, 0.85f), std::max(9, int(10 * pc.scale)), true, Qt::AlignCenter);
    }

    // Fingers.
    for (const Held &h : m_held) {
        if (h.touch < 0 || h.cancelled)
            continue;
        const Touch *t = nullptr;
        for (int i = 0; i < m_frame.count; ++i)
            if (m_frame.touches[i].id == h.touch)
                t = &m_frame.touches[i];
        if (!t)
            continue;
        Touch shown = *t;
        shown.x = h.x;
        shown.y = h.y;
        const QColor fc = familyColor(col, h.layer.family);
        QString label = QString::fromUtf8(colourName(h.layer.family, h.layer.colour, h.layer.style));
        // Motion shows in the aurora itself (a sweep sways it, a gate
        // flickers it); the tag says only what the layer is.
        if (h.latch || h.grabbed)
            label += QStringLiteral(" ●");
        // The finger, its tag on whichever side has room.
        c::finger(p, pc, shown, fc, QString(), 0.4f + 0.6f * h.layer.press);
        const QPointF at = pc.at(h.x, h.y);
        const qreal r = pc.pad.height() / 10.0 * 0.32 * std::clamp(t->size, 0.6f, 4.0f);
        const int tpx = 12;
        const qreal w = console::textWidth(c::font(tpx, true), label) + 12;
        const bool left = at.x() + r + 6 + w > pc.pad.right() - 4;
        c::tag(p, QPointF(left ? at.x() - r - 6 - w : at.x() + r + 6, at.y() - r * 0.6), label, fc, tpx);
    }
    p.restore();

    // The panel's readouts along the top edge, inside the pad.
    const int own = int(std::lround(d[PubOwn]));
    {
        const int px = std::max(10, int(12 * pc.scale));
        const QFont fnt = c::font(px, true);
        qreal x = pc.pad.left() + 12, y = pc.pad.top() + 11;
        // Readouts run left to right and wrap onto a second row on a narrow pad.
        auto room = [&](qreal w) {
            if (x + w > pc.pad.right() - 10) {
                x = pc.pad.left() + 12;
                y += 22;
            }
        };
        auto lit = [&](const QString &s, bool on, QColor cc) {
            const qreal w = console::textWidth(fnt, s) + 12;
            room(w);
            if (on)
                c::tag(p, QPointF(x, y), s, cc, px);
            else
                c::text(p, QRectF(x, y - 9, w, 18), s, c::alpha(cc, 0.35f), px, true, Qt::AlignCenter);
            x += w + 6;
        };
        auto meter = [&](const QString &s, float v, int segments) {
            room(124);
            c::text(p, QRectF(x, y - 9, 52, 18), s, c::alpha(col, 0.7f), px, true);
            c::gauge(p, QRectF(x + 54, y - 5, 60, 10), v, col, segments);
            x += 134;
        };
        // The chord.
        const float rootMidi = midiOf(root);
        const int rpc = ((int(std::lround(rootMidi)) % 12) + 12) % 12;
        QString quality;
        bool third = false;
        for (int i = 0; i < count; ++i) {
            double iv = midiOf(tones[i]) - rootMidi;
            iv -= 12.0 * std::floor(iv / 12.0);
            if (std::fabs(iv - 3.0) < 0.3) {
                quality = QStringLiteral("m");
                third = true;
            } else if (std::fabs(iv - 4.0) < 0.3) {
                third = true;
            }
        }
        if (!third)
            quality = QStringLiteral("sus");
        const QString chord = QString::fromUtf8(Tuning::noteName(rpc)) + quality;
        // Whose chord it is: the Chords page's, the player's own, or a loop's.
        const QString whose = own < 0 ? QStringLiteral("%1 · FOLLOW").arg(chord)
                              : d[PubOwnLoop] > 0.5f ? QStringLiteral("%1 · LOOP %2").arg(chord, roman(own))
                                                     : QStringLiteral("%1 · OWN %2 · ATMOS ONLY").arg(chord, roman(own));
        lit(whose, true, col);
        x += 4;
        meter(QStringLiteral("SPACE"), d[PubSpace], 4);
        meter(QStringLiteral("TONE"), d[PubTone], 8);
        meter(QStringLiteral("SWELL"), d[PubSwell], 6);
        if (m_scene >= 0)
            lit(QString::fromUtf8(kScenes[m_scene].name), true, familyColor(col, Choir));
        else if (m_style != Classic)
            lit(QStringLiteral("%1 LAYERS").arg(QString::fromUtf8(kStyleName[m_style]).toUpper()), false,
                familyColor(col, Choir));
        const QString cue = loopCue(pc.beat, *pc.view);
        if (!cue.isEmpty())
            lit(cue, true, c::red());
    }

    c::beats(p, pc, col);

    // Below the pad, every one clickable. Row 0: the clips, then the scenes
    // in two thin rows, the letter row's ten above and the five on Shift
    // below. Row 1, in two thin rows: the switches and the styles, then the
    // steppers and the bed's own chords. Each chip says its key.
    m_spots.clear();
    c::clips(p, c::bandRow(pc, 0, 0, 0.34), *pc.view, col, pc.time, &m_spots);
    const double thin = 14, gap = 3;
    constexpr int tpx = 10; // a size up from what chip() gives a box this thin
    auto half = [&](int row, int sub, double x0, double x1) {
        const QRectF r = c::bandRow(pc, row, x0, x1);
        return QRectF(r.left(), r.top() + sub * (r.height() - thin), r.width(), thin);
    };
    auto thinChip = [&](const QRectF &cell, const QString &label, bool lit, int id, int px = 0) {
        c::chip(p, pc, cell, QString(), lit, id >= 0 ? &m_spots : nullptr, id);
        c::text(p, cell.adjusted(2, 0, -2, 0), label, lit ? QColor::fromRgb(c::kInk) : col, px > 0 ? px : tpx, true,
                Qt::AlignCenter);
    };
    // A row of scenes, each chip as wide as its name: the whole row in view
    // (the keys on it are a keyboard's row, and every scene stays in
    // sight), its type a size or two smaller if that is what it takes; on a
    // pad too narrow even for that, a window that follows the lit one.
    auto sceneRow = [&](const QRectF &r, int from, int to) {
        QStringList labels;
        for (int sc = from; sc < to; ++sc)
            labels << QStringLiteral("%1 %2").arg(sceneKey(sc), QString::fromUtf8(kScenes[sc].name).toUpper());
        const int lit = m_scene >= from && m_scene < to ? m_scene - from : -1;
        for (int px = tpx; px >= 8; --px) {
            const QFont f = c::font(px, true);
            std::vector<double> w;
            double sum = 0;
            for (const QString &l : labels) {
                w.push_back(console::textWidth(f, l) + 8);
                sum += w.back();
            }
            const double spare = r.width() - sum - gap * double(labels.size() - 1);
            if (spare < 0)
                continue;
            double x = r.left();
            for (int k = 0; k < int(labels.size()); ++k) {
                const double cw = w[size_t(k)] + spare / double(labels.size());
                thinChip(QRectF(x, r.top(), cw, r.height()), labels[k], k == lit, SpotScene + from + k, px);
                x += cw + gap;
            }
            return;
        }
        c::chips(p, pc, r, labels, lit, &m_spots, SpotScene + from, gap);
    };
    sceneRow(half(0, 0, 0.35, 1), 0, kShiftScenes);
    sceneRow(half(0, 1, 0.35, 1), kShiftScenes, kSceneCount);

    // A row of chips of their labels' widths, the spare room shared out
    // evenly. A stepper is one chip, its left half one way and its right
    // half the other.
    struct Item {
        QString label;
        bool lit;
        int id, id2 = -1; // id2: the right half's, for a stepper
    };
    auto row = [&](const QRectF &r, const std::vector<Item> &items) {
        const QFontMetricsF fm(c::font(tpx, true));
        std::vector<double> w;
        double sum = 0;
        for (const Item &it : items) {
            w.push_back(console::textWidth(c::font(tpx, true), it.label) + 12);
            sum += w.back();
        }
        const double spare = r.width() - sum - gap * double(items.size() - 1);
        const double add = spare / double(items.size()), squeeze = spare < 0 ? (r.width() - gap * double(items.size() - 1)) / sum : 1;
        double x = r.left();
        for (size_t i = 0; i < items.size(); ++i) {
            const double cw = spare < 0 ? w[i] * squeeze : w[i] + add;
            const QRectF cell(x, r.top(), cw, r.height());
            const Item &it = items[i];
            thinChip(cell, it.label, it.lit, it.id2 < 0 ? it.id : -1);
            if (it.id2 >= 0) {
                m_spots.add(QRectF(cell.left(), cell.top(), cell.width() / 2, cell.height()), it.id);
                m_spots.add(QRectF(cell.center().x(), cell.top(), cell.width() / 2, cell.height()), it.id2);
            }
            x += cw + gap;
        }
    };
    static const char *pumpName[3] = {"OFF", "KICK", "BEAT"};
    const int pump = std::clamp(int(std::lround(d[PubPump])), 0, 2);
    row(half(1, 0, 0, 0.645), {
        {QStringLiteral("HOLD  ⇧⏎"), m_hold, SpotHold},
        {QStringLiteral("FREEZE  F"), d[PubFreeze] > 0.5f, SpotFreeze},
        {QStringLiteral("CLOUD  G"), d[PubCloud] > 0.5f, SpotCloud},
        {QStringLiteral("SWELL ON/OFF  S"), m_swellClicked || m_swellHeld, SpotSwell},
        {QStringLiteral("⇧←  PUMP %1  ⇧→").arg(QString::fromUtf8(pumpName[pump])), pump > 0, SpotPumpDown, SpotPumpUp},
        {QStringLiteral("OVERDUB  F10"), pc.view->overdub, SpotOverdub},
        {QStringLiteral("LET GO  DEL"), false, SpotLetGo},
    });
    // The styles: L, which steps on to the next, always in sight, then the
    // nine in a window that follows the lit one.
    {
        const QRectF r = half(1, 0, 0.65, 1);
        const double lw = console::textWidth(c::font(tpx, true), QStringLiteral("L ›")) + 12;
        thinChip(QRectF(r.left(), r.top(), lw, r.height()), QStringLiteral("L ›"), false, SpotStyleNext);
        QStringList styles;
        for (int st = 0; st < kStyles; ++st)
            styles << QString::fromUtf8(kStyleName[st]).toUpper();
        c::chips(p, pc, r.adjusted(lw + gap, 0, 0, 0), styles, m_style, &m_spots, SpotStyle, gap);
    }
    std::vector<Item> lower = {
        {QStringLiteral("↓  SPACE  ↑"), false, SpotSpaceDown, SpotSpaceUp},
        {QStringLiteral("−  TONE  +"), false, SpotToneDown, SpotToneUp},
        {QStringLiteral("FOLLOW  §"), own < 0, SpotFollow},
    };
    static const char *chordKeys = "ZXCVBNM";
    for (int i = 0; i < 7; ++i)
        lower.push_back({QStringLiteral("%1 %2").arg(QChar(chordKeys[i]), roman(i)), own == i, SpotChord + i});
    row(half(1, 1, 0, 1), lower);
}

} // namespace

PartSpec makeAtmos() {
    PartSpec s;
    s.name = QStringLiteral("Atmos");
    s.blurb = QStringLiteral("pads, choirs and air");
    s.color = QColor("#A98BFF");
    s.engine = std::make_unique<AtmosEngine>();
    s.control = std::make_unique<AtmosControl>();
    // The bed brings its own halo and its own pump, so the channel's are
    // light: a touch of the shared room to sit with the rest, no echo.
    const float channel[ChannelCount] = {0.7f, 0.5f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
