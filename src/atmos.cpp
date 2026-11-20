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

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include "console.h"
#include "dsp.h"
#include "part.h"
#include "parts.h"
#include "phrase.h"

namespace tf {

namespace {

inline float smoothstep(float a, float b, float x) {
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// --- the layers -------------------------------------------------------------

enum Family : int { Drone, Pad, Choir, Glass, Air };
constexpr int kFamilies = 5;

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
inline const char *colourName(int family, float c) {
    if (family == Choir) {
        static const char *vowels[5] = {"oo", "oh", "ah", "eh", "ee"};
        return vowels[std::clamp(int(std::lround(c * 4.0f)), 0, 4)];
    }
    return kShape[family].colours[c < 0.3f ? 0 : c < 0.7f ? 1 : 2];
}

// A firm landing starts at once, a light one fades in.
inline float attackFor(float pressure) { return 0.03f + 1.8f * std::pow(clampf(1.0f - pressure, 0.0f, 1.0f), 2.2f); }
inline float widthOf(float size) { return clampf(0.75f + 0.5f * (size - 1.0f), 0.35f, 1.0f); }

// --- PADsynth ---------------------------------------------------------------

enum Timbre : int { DroneSoft, DroneBowed, PadStrings, PadSaw, PadHollow, ChoirTone, GlassTone, WindTone, kTimbres };

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
// (equal power: the tables are uncorrelated).
void timbres(int family, float c, int &a, int &b, float &wa, float &wb) {
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
// root and a perfect fifth.
int bedPitches(const Bed &b, const Harmony &h, bool drone, float lo, float hi, float *out, int max) {
    const int per = h.tuning.degreesPerOctave();
    int tones[4], count = 0;
    if (drone) {
        tones[count++] = b.root;
        if (b.fifth)
            tones[count++] = b.fifthDegree;
    } else {
        for (int i = 0; i < b.count; ++i)
            tones[count++] = b.tones[i];
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

enum ParamId : int { PFreeze = 0, PCloud, PPump, PSpace, PSwell, PChord, PChordToLoop };
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
    PubVoice0 = 20,  // 9 per voice
    PubGlint0 = 200, // 3 per glint
};
constexpr int kPubStride = 9;
constexpr int kPubVoices = 20, kPubGlints = 16;

constexpr int kVoices = 20;
constexpr int kSlots = 12;
constexpr int kGlints = 32;

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
    int id = 0, family = 0;
    float tgt[5] = {}, cur[5] = {}; // height, colour, pressure, motion, width
    float attack = 0.3f, ramp = 0, env = 0, relCoef = 0.9999f;
    float norm = 1, level = 0, gate = 1, gateSmooth = 1;
    float lfo1 = 0, lfo2 = 0, rate1 = 0.07f, rate2 = 0.13f, side = 1;
    float dropClock = 0;
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
                if (v.active && v.seamHold && ((v.id >> 24) & 63) == (c.i[0] & 63)) {
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
        m_cloudAmt += ((m_cloudOn ? 1.0f : 0.0f) - m_cloudAmt) * (1.0f - std::exp(-dt / 0.6f));

        float dryL[kBlock] = {}, dryR[kBlock] = {};
        float weight = 0;
        for (Voice &v : m_voices) {
            if (!v.active)
                continue;
            renderVoice(v, dryL, dryR, n, h, tr);
            weight += v.env;
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
        }
        renderGlints(dryL, dryR, n);
        // Sheen: the top of the bed lifted a touch always, and a lot as it
        // swells.
        const float sheen = 0.25f + 4.5f * m_swellE;
        for (int i = 0; i < n; ++i) {
            dryL[i] += sheen * m_sheenL.step(dryL[i]).high;
            dryR[i] += sheen * m_sheenR.step(dryR[i]).high;
        }

        // The room the bed lives in.
        float t60 = (3.0f + 9.0f * m_spaceS + 6.0f * m_swellE) * (1.0f - 0.85f * m_clear);
        if (m_melt > 0)
            t60 = std::min(t60, 4.0f);
        const float shimmer = 0.12f + 0.35f * m_spaceS + 0.65f * m_swellE;
        m_halo.set(t60, shimmer, m_freezeAmt, n);
        m_cloud.set(m_cloudAmt);
        const float send = 0.45f + 0.35f * m_spaceS + 0.35f * m_swellE;
        const float wet = (0.35f + 0.8f * m_spaceS) * (1.0f + 0.1f * m_freezeAmt);
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
            const float gd = 1.0f - depth * m_pumpEnv, gw = 1.0f - 0.55f * depth * m_pumpEnv;
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
            o[4] = ((v.id & (1 << 30)) ? 2.0f : 1.0f) + (v.frozen ? 4.0f : 0.0f) + (v.releasing ? 8.0f : 0.0f);
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
        for (; g < kPubGlints; ++g)
            d[PubGlint0 + 3 * g + 2] = 0;
    }

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
        setTargets(v, c);
        for (int j = 0; j < 5; ++j)
            v.cur[j] = v.tgt[j];
        v.attack = std::max(0.02f, c.f[5] > 0 ? c.f[5] : 0.4f);
        v.ramp = 0;
        v.env = 0;
        v.relCoef = std::exp(-1.0f / (kShape[v.family].release / 4.6f * m_rate));
        v.norm = 1;
        v.level = 0;
        v.gate = v.gateSmooth = 1;
        v.lfo1 = v.rng.unit();
        v.lfo2 = v.rng.unit();
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

    void renderVoice(Voice &v, float *outL, float *outR, int n, const Harmony &h, const Transport &tr) {
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
        const int nc = bedPitches(m_bed, h, v.family == Drone, P, P + fs.span, cand, 16);
        for (Slot &s : v.notes)
            s.seen = false;
        int ta, tb;
        float wa, wb;
        timbres(v.family, colour, ta, tb, wa, wb);
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

        // The tones.
        float sL[kBlock] = {}, sR[kBlock] = {};
        const float aIn = 1.0f - std::exp(-dt / 0.12f);
        const float aOut = 1.0f - std::exp(-dt / (v.family == Glass ? 0.6f : 0.35f));
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
            if (morph) {
                const float *B = m_bank->table[tb][s.octave].data();
                for (int i = 0; i < n; ++i) {
                    const float g = g0 + dg * float(i + 1);
                    const uint32_t i0 = ph >> kFracBits, j0 = (ph + half) >> kFracBits;
                    const float fr = float(ph & ((1u << kFracBits) - 1)) * kFracScale;
                    const float a = A[i0] + fr * (A[i0 + 1] - A[i0]), ar = A[j0] + fr * (A[j0 + 1] - A[j0]);
                    const float b = B[i0] + fr * (B[i0 + 1] - B[i0]), br = B[j0] + fr * (B[j0 + 1] - B[j0]);
                    sL[i] += g * (wa * a + wb * b);
                    sR[i] += g * (wa * ar + wb * br);
                    ph += s.inc;
                }
            } else {
                for (int i = 0; i < n; ++i) {
                    const float g = g0 + dg * float(i + 1);
                    const uint32_t i0 = ph >> kFracBits, j0 = (ph + half) >> kFracBits;
                    const float fr = float(ph & ((1u << kFracBits) - 1)) * kFracScale;
                    sL[i] += g * (A[i0] + fr * (A[i0 + 1] - A[i0]));
                    sR[i] += g * (A[j0] + fr * (A[j0 + 1] - A[j0]));
                    ph += s.inc;
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
            cutoff = 380.0f * std::exp2(1.1f + 3.4f * height + 1.8f * (press - 0.5f) + oct);
            q = 0.8f + 0.4f * evolve;
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

        // Pulse: a gate in time, eighths at a little twist, sixteenths at more.
        const float gateDepth = smoothstep(0.12f, 0.45f, pulse);
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
            const float a = v.env * level * gate * agc0;
            const float mid = 0.5f * (L + R) * a, side = 0.5f * (L - R) * a * width;
            outL[i] += (mid + side) * panL;
            outR[i] += (mid - side) * panR;
        }
        // Loudness follows pressure, not colour or height: the layer's own
        // sound (before its envelope, pressure and gate) is held to one
        // level, slowly, so sweeps and vowels change the colour only.
        {
            const float rms = std::sqrt(timbreSq / float(2 * n));
            v.agcRms += (rms - v.agcRms) * (1.0f - std::exp(-dt / 0.3f));
            const float want = clampf(1.0f / std::max(v.agcRms, 1e-3f), 0.15f, 12.0f);
            // Down at once, up slowly: a layer fading in never overshoots.
            v.agc += (want - v.agc) * (1.0f - std::exp(-dt / (want < v.agc ? 0.03f : 0.6f)));
        }
        v.gate = 1.0f - gateDepth * (1.0f - v.gateSmooth);
        v.level = v.env * pressGain * breath;
        if (v.releasing && v.ramp >= 1.0f && v.env < 1e-4f)
            v.active = false;
    }

    float m_rate = 48000;
    const Bank *m_bank = nullptr;
    std::array<Voice, kVoices> m_voices;
    std::array<Glint, kGlints> m_glints;
    Halo m_halo;
    Cloud m_cloud;
    Svf m_hpL, m_hpR;
    Bed m_bed;
    bool m_freeze = false, m_cloudOn = false, m_swellOn = false;
    uint32_t m_mask = 0;
    double m_melt = 0, m_sinceChange = 64;
    float m_clear = 0;
    int m_pump = PumpKick, m_own = -1;
    float m_space = 0.5f, m_spaceS = 0.5f, m_swell = 0, m_freezeAmt = 0, m_cloudAmt = 0;
    float m_norm = 0.6f, m_pumpEnv = 0, m_levelOut = 0, m_haloOut = 0, m_swellE = 0;
    bool m_ownLoop = false, m_ownHand = false;
    float m_lowCutSet = 55.0f;
    Svf m_sideHp1, m_sideHp2, m_sheenL, m_sheenR;
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
};

// Whole beds on one key each, laid on the pad as latched layers that can be
// grabbed and reshaped like any other.
const Scene kScenes[8] = {
    {"Dawn", 2.5f, 3, {{Pad, 0.1f, 0.35f, 0.5f, 0.25f, 0.85f}, {Choir, 0.08f, 0.45f, 0.45f, 0.15f, 0.9f}, {Air, 0.05f, 0.55f, 0.35f, 0.3f, 1.0f}}},
    {"Ocean", 3.0f, 3, {{Drone, 0.3f, 0.2f, 0.45f, 0.2f, 0.7f}, {Pad, 0.85f, 0.3f, 0.45f, 0.35f, 0.9f}, {Air, 0.4f, 0.4f, 0.5f, 0.6f, 1.0f}}},
    {"Cathedral", 3.0f, 4, {{Drone, 0.7f, 0.1f, 0.45f, 0.1f, 0.6f}, {Choir, 0.5f, 0.35f, 0.55f, 0.2f, 0.9f}, {Choir, 0.9f, 0.8f, 0.4f, 0.1f, 1.0f}, {Glass, 0.1f, 0.3f, 0.35f, 0.0f, 1.0f}}},
    {"Stardust", 2.0f, 3, {{Glass, 0.65f, 0.5f, 0.55f, 0.0f, 1.0f}, {Pad, 0.55f, 0.6f, 0.4f, 0.4f, 0.9f}, {Air, 0.1f, 0.8f, 0.3f, 0.3f, 1.0f}}},
    {"Nostalgia", 2.0f, 3, {{Air, 0.95f, 0.5f, 0.45f, 0.0f, 1.0f}, {Pad, 0.8f, 0.3f, 0.5f, 0.2f, 0.8f}, {Drone, 0.55f, 0.25f, 0.4f, 0.0f, 0.6f}}},
    {"Euphoria", 2.5f, 4, {{Pad, 0.5f, 0.45f, 0.7f, 0.5f, 1.0f}, {Choir, 0.45f, 0.55f, 0.5f, 0.2f, 1.0f}, {Glass, 0.45f, 0.45f, 0.45f, 0.0f, 1.0f}, {Drone, 0.4f, 0.3f, 0.4f, 0.0f, 0.7f}}},
    {"Night rain", 2.5f, 3, {{Air, 0.4f, 0.45f, 0.6f, 0.0f, 1.0f}, {Pad, 0.15f, 0.2f, 0.45f, 0.2f, 0.8f}, {Glass, 0.2f, 0.25f, 0.35f, 0.0f, 1.0f}}},
    {"Pulse", 0.4f, 3, {{Pad, 0.5f, 0.5f, 0.65f, -0.8f, 1.0f}, {Drone, 0.35f, 0.2f, 0.45f, 0.0f, 0.7f}, {Choir, 0.4f, 0.6f, 0.45f, -0.4f, 0.9f}}},
};
const int kSceneKeys[8] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I};
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
                                                             QString::fromUtf8(colourName(f, v[2])));
                if (!names.contains(n))
                    names << n;
            }
            m_frozen = names.isEmpty() ? QStringLiteral("frozen: the halo hangs, F thaws")
                                       : QStringLiteral("frozen: %1 hanging, F thaws").arg(names.join(QStringLiteral(", ")));
        }
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const PartView &pv = ctx.studio().parts[ctx.part()];
        const float *d = pv.data;
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
            param(PSwell, k.down ? 1.0f : 0.0f, ctx);
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            ctx.launch(k.key - Qt::Key_F1);
            return true;
        }
        if (k.key == Qt::Key_Backspace) {
            ctx.stop();
            return true;
        }
        if (k.repeat)
            return true;
        for (int s = 0; s < 8; ++s)
            if (k.key == kSceneKeys[s]) {
                toggleScene(s, ctx);
                return true;
            }
        for (int c = 0; c < 7; ++c)
            if (k.key == kChordKeys[c]) {
                const bool loopHad = d[PubOwnLoop] > 0.5f;
                chord(c, ctx);
                ctx.notice(QStringLiteral("The bed's own chord %1: only Atmos plays it%2. Comma follows the Chords page")
                               .arg(roman(c), loopHad ? QStringLiteral(", over the loop's chords") : QString()));
                return true;
            }
        switch (k.key) {
        case Qt::Key_Comma:
            chord(-1, ctx);
            ctx.notice(QStringLiteral("The bed follows the chords"));
            return true;
        case Qt::Key_H:
            m_hold = !m_hold;
            ctx.notice(m_hold ? QStringLiteral("Hold: layers stay when lifted") : QStringLiteral("Hold off"));
            return true;
        case Qt::Key_F: {
            const bool on = d[PubFreeze] < 0.5f;
            param(PFreeze, on ? 1.0f : 0.0f, ctx);
            ctx.notice(on ? QStringLiteral("Frozen: the bed holds, lifted or not, and follows the chords")
                          : QStringLiteral("Thawed"));
            return true;
        }
        case Qt::Key_G: {
            const bool on = d[PubCloud] < 0.5f;
            param(PCloud, on ? 1.0f : 0.0f, ctx);
            ctx.notice(on ? QStringLiteral("Grain cloud") : QStringLiteral("Cloud off"));
            return true;
        }
        case Qt::Key_P: {
            const int mode = (int(std::lround(d[PubPump])) + 1) % 3;
            param(PPump, float(mode), ctx);
            static const char *names[3] = {"Pump off", "Pump with the kick", "Pump on every beat"};
            ctx.notice(QString::fromUtf8(names[mode]));
            return true;
        }
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const float s = clampf(std::round(d[PubSpace] * 4.0f) / 4.0f + (k.key == Qt::Key_Up ? 0.25f : -0.25f), 0.0f, 1.0f);
            param(PSpace, s, ctx);
            ctx.notice(QStringLiteral("Space %1").arg(int(std::lround(s * 4))));
            return true;
        }
        case Qt::Key_O:
            ctx.overdub(!pv.overdub);
            ctx.notice(pv.overdub ? QStringLiteral("Overdub off") : QStringLiteral("Overdub"));
            return true;
        case Qt::Key_Delete:
            releaseAll(ctx, QStringLiteral("Let go: everything latched fades"));
            return true;
        default:
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
            param(PSwell, 0, ctx);
        }
        m_frame = Frame{};
    }

    void paint(QPainter &p, const PaintContext &pc) override;
    QString help() const override;
    QString status() const override;

private:
    struct Layer {
        int id = 0, family = 0;
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
    void param(int which, float value, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = which;
        c.f[0] = value;
        c.t = ctx.now();
        ctx.send(c);
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
        m_onsets[m_onsetHead++ % m_onsets.size()] = {c.t, true};
    }
    void sendOn(Layer &l, ControlContext &ctx, double t, uint16_t flags = 0) {
        Command c;
        c.type = Command::On;
        c.flags = flags;
        c.id = l.id;
        c.t = t;
        c.i[0] = l.family;
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
            ctx.notice(QStringLiteral("%1 fades").arg(QString::fromUtf8(kScenes[s].name)));
            return;
        }
        const Scene &sc = kScenes[s];
        for (int i = 0; i < sc.count; ++i) {
            const SceneLayer &sl = sc.layers[i];
            Layer L;
            L.id = nextId();
            L.family = sl.family;
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
        ctx.notice(QString::fromUtf8(sc.name));
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
    int m_nextId = 0, m_scene = -1, m_takeSlot = -1, m_lastPlaying = -1;
    double m_spaceAt = 0;
    bool m_button = false, m_longDone = false, m_hold = false, m_swellHeld = false;
};

QString AtmosControl::help() const {
    return QStringLiteral(
        "touch\ta layer of the bed, in the chord: drone, pad, choir, glass, air from left to right\n"
        "slide up or down\tthe voicing climbs or falls through the chord, brighter as it rises\n"
        "slide across a column\tthe colour: soft to bowed, strings to saw to hollow, oo to ee, pure glass to twinkles to stars, wind to rain to vinyl\n"
        "press\tswells the layer; a firm landing sounds at once, a light one fades in\n"
        "twist\tclockwise, a slow filter sweep with the bars; anticlockwise, a gate in eighths, then sixteenths\n"
        "flat finger\twider\n"
        "tap\ta whole breath: the swell finishes, then fades\n"
        "click\tlatch every layer held, a new one too: it stays when lifted\n"
        "hold the click down a second\tlet every latched layer go (so does Delete)\n"
        "touch an orb\tpick a latched layer up and reshape it; a quick tap lets it go\n"
        "Space\tloop what was just played, latched bed and all; the pad shows the loop waiting for its bar; Space again calls it off\n"
        "Q W E R T Y U I\tscenes: dawn, ocean, cathedral, stardust, nostalgia, euphoria, night rain, pulse\n"
        "Z X C V B N M\tthe bed's own chord on degrees I to VII (Atmos only); Space loops a progression with the bed, and a key pressed over the loop takes over until comma\n"
        "comma\tfollow the Chords page again\n"
        "H\thold: layers held a moment stay when lifted\n"
        "F\tfreeze: every layer sounding holds, lifted or not, and follows the chords; again to thaw\n"
        "G\tgrain cloud of the bed, in octaves\n"
        "S, held\tswell: louder, brighter, more shimmer, rising for as long as it is held (up to eight bars)\n"
        "P\tpump: with the kick, on every beat, off\n"
        "Up, Down\tspace: the halo's size and shimmer\n"
        "O\toverdub\n"
        "Delete\tlet every latched layer go\n"
        "F1–F8\tlaunch a slot at the next bar\n"
        "Backspace\tstop the clip at the next bar");
}

QString AtmosControl::status() const {
    QStringList parts;
    for (const Held &h : m_held)
        if (h.touch >= 0 && !h.cancelled)
            parts << QStringLiteral("%1 %2").arg(QString::fromUtf8(kShape[h.layer.family].name),
                                                QString::fromUtf8(colourName(h.layer.family, h.layer.colour)));
    QString s = parts.join(QStringLiteral(", "));
    auto add = [&](const QString &t) { s += (s.isEmpty() ? QString() : QStringLiteral(" · ")) + t; };
    if (!m_latched.empty())
        add(QStringLiteral("%1 latched").arg(m_latched.size()));
    if (m_scene >= 0)
        add(QString::fromUtf8(kScenes[m_scene].name));
    if (m_hold)
        add(QStringLiteral("hold"));
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
        const QString axis = QStringLiteral("%1 ‹ › %2").arg(QString::fromUtf8(fs.colours[0]), QString::fromUtf8(fs.colours[2]));
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
                QString::fromUtf8(colourName(l.layer.family, l.layer.colour)) + (l.handed ? QStringLiteral(" · loop") : QString()),
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
        QString label = QString::fromUtf8(colourName(h.layer.family, h.layer.colour));
        // Motion shows in the aurora itself (a sweep sways it, a gate
        // flickers it); the tag says only what the layer is.
        if (h.latch || h.grabbed)
            label += QStringLiteral(" ●");
        // The finger, its tag on whichever side has room.
        c::finger(p, pc, shown, fc, QString(), 0.4f + 0.6f * h.layer.press);
        const QPointF at = pc.at(h.x, h.y);
        const qreal r = pc.pad.height() / 10.0 * 0.32 * std::clamp(t->size, 0.6f, 4.0f);
        const int tpx = 12;
        const qreal w = QFontMetricsF(c::font(tpx, true)).horizontalAdvance(label) + 12;
        const bool left = at.x() + r + 6 + w > pc.pad.right() - 4;
        c::tag(p, QPointF(left ? at.x() - r - 6 - w : at.x() + r + 6, at.y() - r * 0.6), label, fc, tpx);
    }
    p.restore();

    // The panel's readouts along the top edge, inside the pad.
    {
        const int px = std::max(10, int(12 * pc.scale));
        const QFont fnt = c::font(px, true);
        qreal x = pc.pad.left() + 12, y = pc.pad.top() + 16;
        // Readouts run left to right and wrap onto a second row on a narrow pad.
        auto room = [&](qreal w) {
            if (x + w > pc.pad.right() - 10) {
                x = pc.pad.left() + 12;
                y += 22;
            }
        };
        auto lit = [&](const QString &s, bool on, QColor cc) {
            const qreal w = QFontMetricsF(fnt).horizontalAdvance(s) + 12;
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
        const int own = int(std::lround(d[PubOwn]));
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
        lit(QStringLiteral("HOLD"), m_hold, col);
        lit(QStringLiteral("FREEZE"), d[PubFreeze] > 0.5f, QColor("#BFE6FF"));
        lit(QStringLiteral("CLOUD"), d[PubCloud] > 0.5f, col);
        static const char *pump[3] = {"PUMP OFF", "PUMP KICK", "PUMP BEAT"};
        lit(QString::fromUtf8(pump[std::clamp(int(std::lround(d[PubPump])), 0, 2)]), d[PubPump] > 0.5f, col);
        meter(QStringLiteral("SPACE"), d[PubSpace], 5);
        meter(QStringLiteral("SWELL"), d[PubSwell], 6);
        if (m_scene >= 0)
            lit(QString::fromUtf8(kScenes[m_scene].name), true, familyColor(col, Choir));
        const QString cue = loopCue(pc.beat, *pc.view);
        if (!cue.isEmpty())
            lit(cue, true, c::red());

        // The keys, as a row of chips: the scenes, then the chords, lit
        // where they are.
        x = pc.pad.left() + 12;
        y += 24;
        const int kpx = std::max(9, int(10 * pc.scale));
        const QFont kf = c::font(kpx, true);
        auto chip = [&](const QString &s, bool on, QColor cc) {
            const qreal w = QFontMetricsF(kf).horizontalAdvance(s) + 10;
            if (x + w > pc.pad.right() - 10)
                return;
            if (on)
                c::tag(p, QPointF(x, y), s, cc, kpx);
            else
                c::text(p, QRectF(x, y - 8, w, 16), s, c::alpha(cc, 0.4f), kpx, true, Qt::AlignCenter);
            x += w + 3;
        };
        static const char *sceneKeys = "QWERTYUI", *chordKeys = "ZXCVBNM";
        for (int i = 0; i < 8; ++i)
            chip(QStringLiteral("%1 %2").arg(QChar(sceneKeys[i])).arg(QString::fromUtf8(kScenes[i].name).toUpper()),
                 m_scene == i, familyColor(col, Choir));
        x += 8;
        for (int i = 0; i < 7; ++i)
            chip(QStringLiteral("%1 %2").arg(QChar(chordKeys[i])).arg(roman(i)), own == i, col);
        chip(QStringLiteral(", FOLLOW"), own < 0, col);
    }

    c::beats(p, pc, col);
    c::clips(p, QRectF(pc.pad.left(), pc.pad.bottom() + 14, pc.pad.width(), 30), *pc.view, col, pc.time);
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
