// The Vocal page: the microphone, heard at once in the headphones, played
// with ten fingers, and looped by Space.
//
// The voice is always on: whatever you sing comes back through the chain
// within the audio server's own buffering (the studio mixes it after the
// master limiter, so not even its look-ahead is added). The fingers play
// the voice the way the other pages play their instruments:
//
//   the middle    keys in the song's key, low to high across the pad: each
//                 finger is your voice again at that note (the vowels stay
//                 yours), its height the throat (darker low, brighter high,
//                 your own in the middle),
//                 pressure its level, twist a detune that widens it
//   left edge     a resonant filter: slide for a wah
//   right edge    freeze (lower half: the last sound held as a cloud you
//                 can sing over) or stutter (upper half: the last slice
//                 repeated on the grid, faster higher up)
//   top edge      four effects for as long as you hold, left to right:
//                 echo, a huge reverb, a radio, a bitcrusher
//   bottom edge   nothing: where a thumb rests, and where the pad is
//                 clicked
//   the click     tape stop: the voice winds down while the pad is pressed
//
// ← → choose what happens to the voice itself (natural, hard-tuned, only
// the notes you hold, a choir on the Chords page's chord, a vocoder, a
// robot, a talk box, throat singing, a sitar, a gamelan, slowed-and-
// chopped tape; B picks a style's own option where it has one). Space
// loops the singing just done as audio: a NEW loop by default, Shift+Space layered onto the vocal loop playing (the other pages
// have it the other way round: a voice is a take, layering is a choice).

#include "vocal.h"

#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <Qt>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>

#include "console.h"
#include "fx.h"
#include "parts.h"
#include "scream.h"

namespace tf {

namespace vocal {

namespace {

// The first six are the page's own; the five after them reach into
// music it had no voice for: a funk talk box, Tuvan throat singing, a
// sitar played by the voice, a Balinese gamelan, and Houston's slowed and
// chopped tape. Songs keep a style by name, so the order may grow but
// never change.
enum Preset { Natural, Tune, Prism, Choir, Vocoder, Robot, Talk, Throat, Sitar, Gamelan, Screwed, Riot, PresetCount };
const char *presetName(int p) {
    static const char *names[] = {"NATURAL", "HARD TUNE", "PRISM", "CHOIR", "VOCODER", "ROBOT",
                                  "TALK BOX", "THROAT", "SITAR", "GAMELAN", "SCREWED", "RIOT"};
    return names[std::clamp(p, 0, PresetCount - 1)];
}
const char *presetLine(int p) {
    static const char *lines[] = {
        "your voice as it is; fingers add copies of it at their notes",
        "snapped to the key's notes, hard; fingers add tuned copies",
        "only the notes your fingers hold sing, in your voice",
        "copies of you sing the Chords page's chord around your note",
        "a synth chord speaks your words: the chord, or your fingers' notes",
        "one note, the chord's root, flat as a machine",
        // (The five after robot kept to 50 characters: the status line
        // adds the style's option and Space's two ways after them.)
        "a funk synth up a tube, your mouth shapes it",
        "you are the drone; your overtones sing over it",
        "your voice plays a sitar: syllables pluck it",
        "syllables strike bronze, gongs keep the cycle",
        "you, slowed on tape; fingers chop and spin it",
        "you, screamed from across a field; fingers join in",
    };
    return lines[std::clamp(p, 0, PresetCount - 1)];
}

// Commands. Notes are On/Move/Off (id: a voice of the page's), the rest
// Params; nothing the fingers do is recorded (a vocal loop is its audio).
enum ParamId {
    PPreset = 1,
    PFormant, // i[1]: −6..6, the voice's own throat
    PMonitor, // i[1]: 1 heard, 0 not
    PMicGain, // f[0]: dB
    PFollow,  // i[1]: hard tune snaps to the chord
    PFilter,  // f[0] height 0..1, f[1] pressure, f[2] on
    PLofi,    // f[0] across 0..1, f[1] pressure, f[2] on
    PFreeze,  // f[0] height 0..1, f[1] pressure, f[2] semitones, f[3] on
    PEcho,    // f[0] across 0..1, f[1] pressure, f[2] on
    PReverb,  // f[0] across 0..1, f[1] pressure, f[2] on, f[3] shimmer
    PTape,    // f[0] on
    PStutter, // i[1]: the rate's index
    PChop,    // i[1]: which (0..3), i[2], i[3]: the word's first sample (high, low 32 bits);
              // f[0] on, f[1] its length in samples, f[2] level, f[3] semitones
    PDemo,    // i[1]: a demo singer in place of the microphone
    PStyleOpt, // i[1]: the gamelan's tuning (GamTuning), i[2]: the talk box's sub octave (1 on),
               // i[3]: the talk box played from the lanes (1) or by the note sung (0),
               // f[0]: the screwed tape's speed (ScrewSpeed), f[1]: throat singing's style (ThroatStyle),
               // f[2]: where the riot's screamer is (scream::Place)
};

// The gamelan's tunings: its own two, rooted on the song's key note, or the
// song's key itself (to play along with the other pages in tune).
enum GamTuning { Pelog, Slendro, GamKey, GamTuningCount };
const char *gamTuningName(int t) {
    static const char *names[] = {"PELOG", "SLENDRO", "KEY"};
    return names[std::clamp(t, 0, GamTuningCount - 1)];
}
// The kotekan's figures, by lane, low to high in pairs (the second of a
// pair an octave up): norot, nyog cag, telu, and empat on the top lane.
enum KotFigure { Norot, NyogCag, Telu, Empat };
KotFigure kotFigure(int lane) {
    return lane <= -4 ? Norot : lane <= 2 ? NyogCag : lane <= 5 ? Telu : Empat;
}
bool kotHigh(int lane) { return lane == -4 || lane == 2 || lane == 5; }
const char *kotName(int lane) {
    static const char *names[] = {"NOROT", "NYOG CAG", "TELU", "EMPAT"};
    return names[kotFigure(lane)];
}

// The screwed tape's speeds: a fourth down (most of a voice in the key
// stays in it), a fifth down, and the classic 0.8 of a pitched-down deck.
enum ScrewSpeed { ScrewFourth, ScrewFifth, ScrewClassic, ScrewSpeedCount };
const char *screwSpeedName(int s) {
    static const char *names[] = {"4TH DOWN", "5TH DOWN", "0.8×"};
    return names[std::clamp(s, 0, ScrewSpeedCount - 1)];
}
const char *screwSpeedKey(int s) {
    static const char *names[] = {"fourth", "fifth", "classic"};
    return names[std::clamp(s, 0, ScrewSpeedCount - 1)];
}
float screwSpeed(int s) {
    static const float rates[] = {0.74915f, 0.66742f, 0.8f}; // 2^(−5/12), 2^(−7/12), 0.8
    return rates[std::clamp(s, 0, ScrewSpeedCount - 1)];
}
constexpr int kChops = 4;
// Throat singing's three styles (B): sygyt's whistle, kargyraa's growl
// an octave down, khoomei's softer overtones between them.
enum ThroatStyle { Sygyt, Kargyraa, Khoomei, ThroatStyleCount };
const char *throatStyleName(int s) {
    static const char *names[] = {"SYGYT", "KARGYRAA", "KHOOMEI"};
    return names[std::clamp(s, 0, ThroatStyleCount - 1)];
}
// What each style does with the voice: the band its melody walks (Hz), how
// narrow the filters that find it are, how far it is lifted (over the
// drone's mean level), how much of it is a steady support and how much
// the voice's own harmonic, how deep the gap round it is cut, the growl
// (how much, how weak the weak cycle, how much of the voice moved down
// half its pitch), how much of the drone's chest is pressed out, and
// whether the melody walks the drone's harmonics or the subharmonic's;
// and its level (measured: each as loud as the natural voice).
struct ThroatShape {
    float lo, hi, q, lift, support, gap, growl, weak, ring, press;
    bool sub;
    float level;
};
const ThroatShape &throatShape(int s) {
    static const ThroatShape shapes[ThroatStyleCount] = {
        {1000.0f, 3400.0f, 12.0f, 1.0f, 0.3f, 1.0f, 0.0f, 1.0f, 0.0f, 0.45f, false, 0.59f},
        {400.0f, 1500.0f, 5.0f, 0.32f, 0.0f, 0.0f, 1.0f, 0.12f, 1.2f, 0.0f, true, 1.1f},
        {650.0f, 2000.0f, 9.0f, 0.35f, 0.15f, 0.45f, 0.0f, 1.0f, 0.0f, 0.15f, false, 0.74f},
    };
    return shapes[std::clamp(s, 0, ThroatStyleCount - 1)];
}
// The harmonic of `base` a finger's note asks for: the note folded up (or
// down) by octaves into the band lo..hi, and of the folds that fit, the one
// whose nearest harmonic is truest to the note, the lower if it is within
// ten cents a fold of being as true (low overtones are the easier sung).
// Rounding the note's own ratio to the drone first and doubling that
// would put every note between one and a half and two and a half times
// the drone on one harmonic. The harmonic sung already (`now`) is kept
// unless another is ten cents truer: two folds about as true would
// otherwise take turns at every waver of the drone.
int throatHarmonic(float noteHz, float base, float lo, float hi, int now = 0) {
    if (base <= 0 || noteHz <= 0)
        return 0;
    float f = noteHz;
    while (f < lo)
        f *= 2;
    while (f * 0.5f >= lo)
        f *= 0.5f;
    int best = std::max(2, int(std::lround(f / base)));
    float bestScore = 1e9f;
    for (int m = 0; f <= hi; ++m, f *= 2) {
        const int k = std::max(2, int(std::lround(f / base)));
        const float score = 1200.0f * std::fabs(std::log2(float(k) * base / f)) + 10.0f * float(m) - (k == now ? 10.0f : 0.0f);
        if (score < bestScore) {
            bestScore = score;
            best = k;
        }
    }
    return std::min(best, 48);
}

constexpr int kLoopIdBase = 0x100000; // a loop's sound: kLoopIdBase + its take's handle
constexpr int kMaxVoices = 10;

// Strip edges, in pad coordinates.
constexpr float kLeft = 0.085f, kRight = 0.915f, kBottom = 0.12f, kTop = 0.86f;
// The keys' lower part shows the tape: what Space would take.
constexpr float kTapeTop = 0.32f;
// The lanes' middle: a finger here keeps your own throat (the engine's
// formant is neutral half way up the lanes).
constexpr float kThroatMid = 0.5f * (kTapeTop + kTop);

// The right edge is two effects on one control (PFreeze): freeze below its
// middle, stutter above, as the engine reads the height.
enum Zone { Keys, Filter, Freeze, Echo, Reverb, Radio, Crush, Chop, Stutter, Rest };
constexpr float kRightMid = 0.5f * (kBottom + kTop);
Zone zoneAt(float x, float y) {
    // The top edge in quarters. The bottom edge plays nothing: thumbs rest
    // there, and the click (tape stop) is pressed there, so an effect
    // under them would sound by accident.
    if (y >= kTop)
        return x < 0.25f ? Echo : x < 0.5f ? Reverb : x < 0.75f ? Radio : Crush;
    if (y <= kBottom)
        return Rest;
    if (x <= kLeft)
        return Filter;
    if (x >= kRight)
        return y >= kRightMid ? Stutter : Freeze;
    if (y < kTapeTop)
        return Chop;
    return Keys;
}

// The harmony lanes, low to high: degrees from the note sung.
const int kIntervals[] = {-7, -4, -2, 2, 4, 5, 7};
const char *kIntervalNames[] = {"−8VE", "−5TH", "−3RD", "+3RD", "+5TH", "+6TH", "+8VE"};
constexpr int kIntervalCount = 7;
// Whether a preset's keys are harmonies of what you sing (the voice leads)
// or notes of their own (the fingers lead: prism, the vocoder).
bool harmonyKeys(int preset) { return preset != Prism && preset != Vocoder; }
// Whether a finger adds a copy of your voice. In the vocoder and the five
// styles after robot a finger plays the style's own part instead (a
// carrier, a synth note, the overtone, a second string, the interlocking
// figure, a repeated word).
bool fingerCopies(int preset) { return preset != Vocoder && preset < Talk; }
// Whether the voice heard is moved to a note of its own, which the page
// rings on the pitch trail.
bool retuned(int preset) { return preset == Tune || preset == Robot || preset == Talk || preset == Screwed; }

// The stutter's slices, in beats.
const double kStutterBeats[] = {0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
const char *kStutterNames[] = {"1/8", "1/8T", "1/16", "1/16T", "1/32"};
constexpr int kStutterCount = 5;

// Published numbers (pc.view->data).
namespace pub {
enum {
    InPeak = 0,
    InRms,
    Hz,
    Clarity,
    Gate,
    Feedback,
    Monitor,
    LivePeak,
    Tape,
    Freeze,
    Loops,
    LeadMidi,
    Lag,
    HasInput,
    Stutter,
    Lift, // the lift on a soft voice, in dB
    Voices = 16, // kMaxVoices × (active, midi, level)
    Overtone = 48, // throat singing: the whistle's harmonic of the drone (0: none), fractional as it sweeps
    OvertoneMidi,  // and its pitch
    Scope = 64,  // 64 × (min, max) of the microphone, the last 43 ms
    ScopeN = 64,
    // The styles' own pictures, after the scope.
    TalkMidi = 192, // the talk box's synth: its pitch now (glide and vibrato in it)
    TalkTarget,     // the note it is heading for
    TalkGate,       // how open it is (KEYS: a lane held)
    SitarBuzz,      // how far onto the jawari the main string reaches
    SitarLevel,     // the main string's level
    Taraf,          // 13 × the sympathetic strings' levels
    GamCycle = Taraf + 13, // the beat within the gong's 16-beat cycle
    GamStruck,      // the last bar struck (its MIDI note), and
    GamStruckAge,   // how long ago, in seconds
    ScrewLag,       // seconds the tape head is behind the live voice
    ScrewLimit,     // the lag at which it jumps back
    ScrewSpin,      // 1 while the tape spins back or stops
    ScrewSemis,     // how far down the tape plays
    ScrewDrop,      // seconds until the head drops onto the grid (−1: none arranged)
    ScrewDropBar,   // 1 if that drop is at a bar line (two bars behind), 0 on a beat
    ScrewDeckB,     // how far the crossfader is over to deck B
    TalkOpen,       // KEYS: how far the synth's filter is open, 0..1
    RiotOctave,     // the octaves the riot's phrase is screamed up (−1..2)
    RiotStrain,     // how hard its throat works, 0..1: the false folds' share
};
static_assert(RiotStrain < kPartData, "fits the page's data");
}

// The nearest note of the key, or of the chord when follow is on, to a
// MIDI pitch.
double snapMidi(const tf::Harmony &h, double midi, bool chord) {
    const Tuning &t = h.tuning;
    double best = midi, bestD = 1e9;
    if (chord) {
        const int n = std::clamp(h.chordCount, 1, 4);
        for (int oct = -2; oct <= 6; ++oct)
            for (int k = 0; k < n; ++k) {
                const double m = h.midi(h.chordTones[k], oct);
                if (std::fabs(m - midi) < bestD) {
                    bestD = std::fabs(m - midi);
                    best = m;
                }
            }
        return best;
    }
    const int per = std::max(1, t.degreesPerOctave());
    const int oct = int(std::floor((midi - t.baseMidi()) / 12.0));
    for (int d = (oct - 1) * per; d <= (oct + 2) * per; ++d) {
        const double m = t.baseMidi() + t.degreeSemis(d);
        if (std::fabs(m - midi) < bestD) {
            bestD = std::fabs(m - midi);
            best = m;
        }
    }
    return best;
}

// The scale degree whose note is nearest a MIDI pitch.
int degreeNear(const Harmony &h, double midi) {
    const Tuning &t = h.tuning;
    const int per = std::max(1, t.degreesPerOctave());
    const int oct = int(std::floor((midi - t.baseMidi()) / 12.0));
    int best = 0;
    double bestD = 1e9;
    for (int d = (oct - 1) * per; d <= (oct + 2) * per; ++d) {
        const double dd = std::fabs(t.baseMidi() + t.degreeSemis(d) - midi);
        if (dd < bestD) {
            bestD = dd;
            best = d;
        }
    }
    return best;
}

// How a finger's note is meant: a degree of the key, a chord tone, or a
// harmony: so many degrees above or below the note being sung.
enum NoteMode { ScaleNote = 0, ChordNote = 1, HarmonyNote = 2 };

// A note the page sends: a scale degree, or (follow) a chord tone counted
// up from the chord's root, resolved now, so a held note follows the
// chord as it changes; or a harmony, `value` degrees from `ref` (the note
// sung), in the key or (chord) on the chord's nearest tone.
double noteMidi(const tf::Harmony &h, int value, int mode, double ref = 57, bool chord = false) {
    if (mode == HarmonyNote) {
        const double m = h.midi(degreeNear(h, ref) + value, 0);
        return chord ? snapMidi(h, m, true) : m;
    }
    if (mode == ChordNote) {
        const int n = std::clamp(h.chordCount, 1, 4);
        const int oct = int(std::floor(double(value) / n));
        const int k = value - oct * n;
        return h.midi(h.chordTones[k], oct);
    }
    return h.midi(value, 0);
}

inline float midiOfHz(float hz) { return 69.0f + 12.0f * std::log2(std::max(1.0f, hz) / 440.0f); }

// A gamelan's scale: pelog's seven tones and slendro's five, as cents over
// the key's root (one set of Javanese measurements each; no two gamelans
// agree, and none is the piano's), or the song's own key. Degree 0 is the
// key's root in octave two, as Harmony::midi counts.
double gamMidi(const Harmony &h, int tuning, int degree) {
    static const double pelog[7] = {0, 120, 270, 540, 670, 785, 950};
    static const double slendro[5] = {0, 231, 474, 717, 955};
    if (tuning == GamKey)
        return h.midi(degree, 0);
    const int n = tuning == Pelog ? 7 : 5;
    const double *cents = tuning == Pelog ? pelog : slendro;
    const int oct = int(std::floor(double(degree) / n));
    return h.tuning.baseMidi() + 12.0 * oct + cents[degree - oct * n] / 100.0;
}
int gamDegreeNear(const Harmony &h, int tuning, double midi) {
    const int n = tuning == Pelog ? 7 : tuning == Slendro ? 5 : std::max(1, h.tuning.degreesPerOctave());
    const int oct = int(std::floor((midi - h.tuning.baseMidi()) / 12.0));
    int best = 0;
    double bestD = 1e9;
    for (int d = (oct - 1) * n; d <= (oct + 2) * n; ++d) {
        const double dd = std::fabs(gamMidi(h, tuning, d) - midi);
        if (dd < bestD) {
            bestD = dd;
            best = d;
        }
    }
    return best;
}

inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

} // namespace

// --- the engine -------------------------------------------------------------------

// A voice for when there is no microphone: a short phrase, sung over and
// over in the song's key (a glottal buzz through vowel formants, the way
// the script player's `voice` sings), so the page can be learned, and its
// loops tried, before a mic is plugged in.
class DemoSinger {
public:
    void render(float *out, int n, const tf::Harmony &h, float rate) {
        // Four words and a breath: degrees and lengths (seconds).
        static const int degrees[] = {4, 5, 7, 5, 2, 4};
        static const float lengths[] = {0.32f, 0.28f, 0.55f, 0.3f, 0.3f, 0.7f};
        static const char vowels[] = {'a', 'e', 'o', 'i', 'a', 'o'};
        constexpr int words = 6;
        for (int i = 0; i < n; ++i) {
            if (m_left <= 0) {
                if (m_gap > 0) {
                    --m_gap;
                    out[i] = 0;
                    continue;
                }
                m_word = (m_word + 1) % words;
                m_len = m_left = int(lengths[m_word] * rate);
                m_gap = int((m_word == words - 1 ? 0.9f : 0.12f) * rate);
                m_hz = midiToHz(h.midi(degrees[m_word], 1));
                setVowel(vowels[m_word], rate);
            }
            const float secs = float(m_len - m_left) / rate;
            const float vib = secs > 0.15f ? std::exp2(0.2f / 12.0f * std::sin(kTwoPi * 5.5f * secs)) : 1.0f;
            m_phase += m_hz * vib / rate;
            m_phase -= std::floor(m_phase);
            const float buzz = m_tilt.step(2.0f * m_phase - 1.0f) + 0.02f * m_rng.bipolar();
            float v = m_f1.step(buzz).band + 0.5f * m_f2.step(buzz).band + 0.25f * m_f3.step(buzz).band;
            const float env = std::min(1.0f, secs / 0.025f) * std::min(1.0f, float(m_left) / (0.06f * rate));
            out[i] = v * env * 0.35f;
            --m_left;
        }
    }

private:
    void setVowel(char v, float rate) {
        float a = 800, b = 1150, c = 2900;
        if (v == 'e')
            a = 400, b = 2000, c = 2550;
        else if (v == 'i')
            a = 280, b = 2250, c = 2900;
        else if (v == 'o')
            a = 450, b = 800, c = 2830;
        m_f1.set(a, 6, rate);
        m_f2.set(b, 8, rate);
        m_f3.set(c, 10, rate);
        m_tilt.set(1400, rate);
    }
    Svf m_f1, m_f2, m_f3;
    OnePoleLp m_tilt;
    Rng m_rng{0xDE70};
    float m_phase = 0, m_hz = 220;
    int m_word = -1, m_left = 0, m_len = 1, m_gap = 0;
};

class Engine : public PartEngine {
public:
    explicit Engine(std::shared_ptr<Tape> tape) : m_tape(std::move(tape)) {}

    void init(float rate) override {
        m_rate = rate;
        m_tape->rate = rate;
        m_hp.set(90.0f, 0.7f, rate);
        m_outDcL.r = m_outDcR.r = 1.0f - kTwoPi * 4.0f / rate;
        m_pitch.init(rate);
        m_echo.init(rate);
        m_reverb.init(rate);
        m_reverb.set(0.8f, 0.3f);
        for (auto &v : m_voices) {
            v.level.setTime(0.02f, rate);
            v.level.reset(0);
            v.ratio.setTime(0.015f, rate);
            v.ratio.reset(1);
        }
        for (auto &c : m_choir) {
            c.level.setTime(0.06f, rate);
            c.level.reset(0);
            c.ratio.setTime(0.04f, rate);
            c.ratio.reset(1);
        }
        m_leadRatio.setTime(0.008f, rate);
        m_leadRatio.reset(1);
        m_leadShift.setTime(0.02f, rate);
        m_leadDry.setTime(0.03f, rate);
        m_leadDry.reset(1);
        m_vocoderMix.setTime(0.03f, rate);
        initStyles(rate);
        m_monGain.setTime(0.01f, rate);
        m_monGain.reset(1);
        m_filtOn.setTime(0.06f, rate);
        m_filtCut.setTime(0.02f, rate);
        m_filtCut.reset(0.7f);
        m_filtRes.setTime(0.02f, rate);
        m_lofiOn.setTime(0.03f, rate);
        m_echoSend.setTime(0.015f, rate);
        m_revSend.setTime(0.03f, rate);
        m_freezeLvl.setTime(0.03f, rate);
        m_stutterOn.setTime(0.004f, rate);
        for (int k = 0; k < kBands; ++k) {
            const float hz = 140.0f * std::pow(7200.0f / 140.0f, float(k) / float(kBands - 1));
            m_bands[k].a.set(hz, 7.0f, rate);
            m_bands[k].s.set(hz, 7.0f, rate);
        }
        m_sib.set(5000.0f, 0.7f, rate);
        m_radioHp.set(500.0f, 0.8f, rate);
        m_radioLp.set(2600.0f, 0.9f, rate);
        m_radioHp2.set(500.0f, 0.8f, rate);
        m_radioLp2.set(2600.0f, 0.9f, rate);
        m_freezeBuf.assign(size_t(rate * 0.75f), 0.0f);
        m_recent.assign(size_t(1) << 17, 0.0f);
        m_howlHp.set(800.0f, 0.7f, rate);
        m_tapeL.assign(size_t(1) << 17, 0.0f);
        m_tapeR.assign(size_t(1) << 17, 0.0f);
    }

    void command(const Command &c, AudioContext &ctx) override {
        if (c.flags & Command::Sampled) {
            if (c.type == Command::On)
                startLoop(c, ctx);
            return;
        }
        if (c.type == Command::Off) {
            for (auto &l : m_loops)
                if (l.id == c.id && !l.fading)
                    l.fading = true;
            for (auto &v : m_voices)
                if (v.id == c.id)
                    v.id = -1, v.on = false;
            return;
        }
        if (c.type == Command::On || c.type == Command::Move) {
            Voice *v = nullptr;
            for (auto &x : m_voices)
                if (x.id == c.id)
                    v = &x;
            if (!v && c.type == Command::On) {
                for (auto &x : m_voices)
                    if (x.id < 0 && x.level.y < 0.001f) {
                        v = &x;
                        break;
                    }
                if (!v)
                    for (auto &x : m_voices)
                        if (x.id < 0) {
                            v = &x;
                            break;
                        }
                if (v) {
                    v->id = c.id;
                    v->shifter.reset();
                    v->fresh = true;
                    v->midi = -1;
                    v->order = ++m_voiceOrder;
                }
            }
            if (!v)
                return;
            v->on = true;
            v->value = c.i[0];
            v->mode = c.i[1];
            v->gain = clampf(c.f[0], 0, 1.5f);
            v->formant = clampf(c.f[1], 0, 1);
            v->detune = clampf(c.f[2], -1, 1);
            v->pan = clampf(c.f[3], 0, 1);
            return;
        }
        if (c.type != Command::Param)
            return;
        switch (c.i[0]) {
        case PPreset: m_preset = std::clamp(c.i[1], 0, PresetCount - 1); break;
        case PFormant: m_formant = std::clamp(c.i[1], -6, 6); break;
        case PMonitor: m_monitor = c.i[1] != 0; break;
        case PMicGain: m_micGain = dbToGain(clampf(c.f[0], -24, 24)); break;
        case PFollow: m_follow = c.i[1] != 0; break;
        case PFilter: m_filt = {c.f[0], c.f[1], c.f[2], 0}; break;
        case PLofi: m_lofi = {c.f[0], c.f[1], c.f[2], 0}; break;
        case PFreeze: {
            const bool was = m_frz.on > 0.5f;
            m_frz = {c.f[0], c.f[1], c.f[2], c.f[3]};
            if (m_frz.on > 0.5f && !was)
                startFreeze(ctx);
            break;
        }
        case PEcho: m_echoP = {c.f[0], c.f[1], c.f[2], 0}; break;
        case PReverb: m_revP = {c.f[0], c.f[1], c.f[2], c.f[3]}; break;
        case PTape: {
            const bool on = c.f[0] > 0.5f;
            if (on && !m_tapeStop) {
                m_tapeStop = true;
                m_tapeRate = 1;
                m_tapeRead = double(m_tapeW);
            } else if (!on && m_tapeStop) {
                m_tapeStop = false;
                m_tapeBack = 1; // back to the live voice, crossfaded
            }
            break;
        }
        case PStutter: m_stutter = std::clamp(c.i[1], 0, kStutterCount - 1); break;
        case PChop: {
            ChopVoice &v = m_chops[std::clamp(c.i[1], 0, kChops - 1)];
            const int64_t start = int64_t(uint64_t(uint32_t(c.i[2])) << 32 | uint64_t(uint32_t(c.i[3])));
            const bool on = c.f[0] > 0.5f;
            if (on && (!v.on || start != v.start)) {
                v.start = start;
                v.pos = double(start);
                v.fade = 0;
            }
            v.on = on;
            v.length = std::max(256.0f, c.f[1]);
            v.level = clampf(c.f[2], 0, 1.5f);
            v.rate = std::exp2(clampf(c.f[3], -24, 24) / 12.0f);
            break;
        }
        case PDemo: m_demo = c.i[1] != 0; break;
        case PStyleOpt:
            m_gamTuning = std::clamp(c.i[1], 0, GamTuningCount - 1);
            m_talkSub = c.i[2] != 0;
            m_talkKeys = c.i[3] != 0;
            m_screwSpeed = std::clamp(int(std::lround(c.f[0])), 0, ScrewSpeedCount - 1);
            m_throatStyle = std::clamp(int(std::lround(c.f[1])), 0, ThroatStyleCount - 1);
            m_scream.setPlace(int(std::lround(c.f[2])));
            break;
        default: break;
        }
    }

    void render(float *outL, float *outR, int n, AudioContext &ctx) override {
        const Transport &tr = ctx.transport();
        const Harmony &h = ctx.harmony();
        const float *in = ctx.input();
        m_hasInput = in != nullptr;
        // No microphone (or a wish to hear how it goes): a demo singer,
        // in the song's key, sings into the chain instead.
        float demo[kBlock];
        if (m_demo) {
            m_singer.render(demo, n, ctx.harmony(), m_rate);
            in = demo;
        }
        Tape &tape = *m_tape;
        // No microphone and nothing still sounding from it (a tail, a
        // freeze, the tape): only the loops play.
        bool chopping = false;
        for (const auto &v : m_chops)
            chopping |= v.on || v.fade > 0;
        if (!in && !chopping && m_echoTail == 0 && m_revTail == 0 && m_frz.on < 0.5f && m_freezeLvl.y < 0.001f &&
            m_stutterOn.y < 0.001f && !m_tapeStop && m_tapeBack <= 0) {
            m_activeVoices = 0;
            tape.written.store(int64_t(tr.sample) + n, std::memory_order_release);
            renderLoops(outL, outR, n, ctx);
            return;
        }

        // 1. The microphone, cleaned up: rumble off, the room's hiss gated
        //    (gently: a soft consonant still gets through), levelled.
        float dry[kBlock];
        for (int i = 0; i < n; ++i) {
            const float raw = (in ? in[i] : 0.0f) * m_micGain;
            const float a = std::fabs(raw);
            m_inPeak = std::max(m_inPeak, a);
            m_inSq += raw * raw;
            ++m_inCount;
            m_scope[m_scopeW++ & (kScopeLen - 1)] = raw;
            const float x = m_hp.step(raw).high;
            m_envAcc += double(x) * x;
            const int64_t sample = int64_t(tr.sample) + i;
            if (((sample + 1) & (kHop - 1)) == 0) {
                tape.env[size_t((sample / kHop) & (kEnvRing - 1))] = float(std::sqrt(m_envAcc / kHop));
                m_envAcc = 0;
            }
            // The gate follows the room's floor: it opens 10 dB over it.
            const float ax = std::fabs(x);
            m_gateEnv += (ax - m_gateEnv) * (ax > m_gateEnv ? 0.02f : 0.0015f);
            m_noise = m_gateEnv < m_noise ? m_noise + (m_gateEnv - m_noise) * 0.001f : m_noise * 1.000004f + 1e-9f;
            m_noise = clampf(m_noise, 1e-5f, 0.05f);
            const bool open = m_gateEnv > m_noise * 3.2f || m_gateEnv > 0.02f;
            m_gate += ((open ? 1.0f : 0.05f) - m_gate) * (open ? 0.02f : 0.0004f);
            float y = x * m_gate;
            // The lift: a soft voice (sung quietly, or a weak microphone)
            // brought up to where a full one sits, by up to 30 dB. It
            // listens only while the gate is open, rises over a second
            // and falls back within a few milliseconds when the voice gets
            // louder, and lifts only what the gate lets through, so the
            // room's hiss between phrases stays where it was.
            {
                const float ay0 = std::fabs(y);
                if (m_gate > 0.5f)
                    m_liftEnv += (ay0 - m_liftEnv) * (ay0 > m_liftEnv ? 0.002f : 0.00005f);
                const float want = clampf(kLiftTarget / std::max(m_liftEnv, 1e-5f), 1.0f, kLiftMax);
                m_lift += (want - m_lift) * (want > m_lift ? 2e-5f : 4e-3f);
                y *= 1.0f + (m_lift - 1.0f) * m_gate * m_gate; // shut (0.05), it adds under 1 dB
            }
            // A compressor, 3:1 from −20 dBFS, quick: an even voice.
            const float ay = std::fabs(y);
            m_compEnv += (ay - m_compEnv) * (ay > m_compEnv ? 0.01f : 0.0003f);
            const float comp = m_compEnv > 0.1f ? std::pow(0.1f / m_compEnv, 0.667f) : 1.0f;
            y *= comp * 1.25f;
            dry[i] = y;
            tape.dry[size_t(sample & kRingMask)] = y;
            m_src.push(y);
            if (m_pitch.push(y, m_gate > 0.2f))
                track();
            // Feedback: a tone the speakers sing back is one frequency,
            // steady; above 1 kHz the pitch tracker can't follow it, so its
            // zero crossings are counted too (a voice's never hold still).
            const float hi = m_howlHp.step(y).high;
            if ((hi >= 0) != (m_howlSign >= 0))
                ++m_howlCross;
            m_howlSign = hi;
            m_howlLevel = std::max(std::fabs(hi), m_howlLevel * 0.9995f);
            if (++m_howlN >= 2048) {
                const float freq = float(m_howlCross) * m_rate / (2.0f * 2048.0f);
                const bool steady = freq > 900 && std::fabs(freq - m_howlFreq) < 0.015f * freq && m_howlLevel > 0.25f;
                m_howlHi = steady ? m_howlHi + 2048 : 0;
                m_howlFreq = freq;
                m_howlCross = 0;
                m_howlN = 0;
                if (m_howlHi > int(m_rate * 0.6f) && m_monitor) {
                    m_feedback = int(m_rate * 2);
                    m_howlHi = 0;
                }
            }
            // The voice's last sound, for the freeze to hold.
            if (m_gate > 0.5f && m_gateEnv > m_noise * 6.0f)
                m_lastVoiced = m_recentW + i + 1;
        }

        // 2. The voice itself, by the preset.
        float leadL[kBlock] = {}, leadR[kBlock] = {}, shifted[kBlock] = {};
        const float hzNow = m_pitch.hz();
        const bool voicedNow = hzNow > 0 && m_gate > 0.3f;
        // Through a consonant or a breath between syllables the pitch is
        // held, so a harmony doesn't drop to the voice's own note and scoop
        // back up on every word; after a quarter second it lets go. The
        // first voiced estimate after a pause lands at once (onset).
        bool onset = false;
        if (voicedNow) {
            onset = m_unvoiced > int(m_rate * 0.25f);
            m_heldHz = hzNow;
            m_heldPeriod = m_pitch.period();
            m_unvoiced = 0;
        } else {
            m_unvoiced += n;
        }
        const bool voiced = voicedNow || (m_heldHz > 0 && m_unvoiced < int(m_rate * 0.25f) && m_gate > 0.1f);
        const float hz = voiced ? m_heldHz : 0.0f;
        const float period = voiced ? m_heldPeriod : 0.0f;
        const float sungMidi = voiced ? midiOfHz(hz) : -1.0f;
        // The note sung, as the five styles after robot play an instrument
        // by it: kept from the tracker's octave slips. On a low "o" (a
        // strong second harmonic) YIN hears the voice an octave up every
        // few estimates; a string or a synth that followed would leap an
        // octave and back many times a second. A reading an octave over
        // the note held is folded back down unless it lasts a sixth of a
        // second (a real leap); one an octave under is taken at once (the
        // slips are upward, so it is the held note that was the slip).
        float styleMidi = sungMidi;
        if (sungMidi > 0) {
            const float diff = sungMidi - m_octRef;
            const float oct = std::round(diff / 12.0f);
            if (m_octRef > 0 && oct > 0 && std::fabs(diff - 12.0f * oct) < 0.8f) {
                m_octAway += n;
                if (m_octAway > int(m_rate / 6))
                    m_octRef = sungMidi, m_octAway = 0;
                else
                    styleMidi = sungMidi - 12.0f * oct;
            } else {
                m_octRef = sungMidi;
                m_octAway = 0;
            }
        } else if (!voiced) {
            m_octRef = -1;
        }
        bool anyFinger = false;
        for (const auto &v : m_voices)
            anyFinger |= v.on;
        float leadTarget = 1.0f; // pitch ratio of the shifted lead
        bool shift = m_formant != 0;
        if (m_preset == Tune || m_preset == Robot) {
            // Only what has a pitch is tuned: consonants and breath go
            // straight through, with none of the shifter's lag.
            shift = voicedNow || m_formant != 0;
            if (voiced) {
                double target;
                if (m_preset == Tune) {
                    target = snapMidi(h, sungMidi, m_follow);
                } else {
                    // The chord's root, in the octave nearest the voice.
                    const double root = h.midi(h.chordRoot, 0);
                    target = root + 12.0 * std::round((sungMidi - root) / 12.0);
                }
                m_leadMidi = float(target);
                leadTarget = std::exp2(float(target - sungMidi) / 12.0f);
            }
        } else if (m_preset == Gamelan && m_gamTuning != GamKey) {
            // In pelog and slendro the voice that stays under the bronze is
            // drawn into the gamelan's tuning, as a pesindhen sings in it:
            // gently (the note sung is followed through 80 ms, and only that
            // slow pitch is moved onto the degree), so the voice's own
            // vibrato and slides stay in it, round the gamelan's tone
            // rather than the piano's 30 or 40 cents away.
            shift = voicedNow || m_formant != 0;
            if (voiced) {
                const float a = 1.0f - std::exp(-float(n) / (0.08f * m_rate));
                m_gamSlow = m_gamSlow > 0 ? m_gamSlow + (styleMidi - m_gamSlow) * a : styleMidi;
                const double target = gamMidi(h, m_gamTuning, gamDegreeNear(h, m_gamTuning, m_gamSlow));
                m_leadMidi = float(target);
                leadTarget = std::exp2(float(target - m_gamSlow) / 12.0f);
            } else {
                m_gamSlow = -1;
            }
        } else {
            m_leadMidi = sungMidi;
        }
        const float formant = std::exp2(float(m_formant) / 12.0f * 0.75f);
        const float wantShift = shift ? 1.0f : 0.0f;
        // The talk box, throat singing and the tape replace the voice with
        // their own; under the sitar it is a breath of the singer, under
        // the gamelan's bronze it steps back, so the instrument carries
        // the tune.
        const float wantDry = (m_preset == Prism && anyFinger) || m_preset == Vocoder || m_preset == Talk ||
                                      m_preset == Throat || m_preset == Screwed || m_preset == Riot
                                  ? 0.0f
                              : m_preset == Sitar   ? 0.0f
                              : m_preset == Gamelan ? 0.45f
                                                    : 1.0f;
        const float ratioNow = m_leadRatio.y;
        if (m_leadShift.y > 0.001f || wantShift > 0) {
            // The ratio moves fast (hard tune) but not in one step.
            for (int i = 0; i < n; ++i)
                m_leadRatio.step(leadTarget);
            m_lead.process(m_src, n, period, 0.5f * (ratioNow + m_leadRatio.y), formant, 1.0f, shifted);
        } else {
            m_lead.reset();
        }
        for (int i = 0; i < n; ++i) {
            const float s = m_leadShift.step(wantShift), dl = m_leadDry.step(wantDry);
            const float v = (dry[i] * (1 - s) + shifted[i] * s) * dl;
            leadL[i] = v;
            leadR[i] = v;
        }

        // The vocoder: a chord of saws, shaped band by band by the voice.
        const float vmix = m_vocoderMix.y;
        if (m_preset == Vocoder || vmix > 0.001f) {
            float carriers[8];
            int nc = 0;
            for (const auto &v : m_voices)
                if (v.on && nc < 8)
                    carriers[nc++] = float(noteMidi(h, v.value, v.mode));
            if (nc == 0) {
                const int cn = std::clamp(h.chordCount, 1, 4);
                for (int k = 0; k < cn; ++k)
                    carriers[nc++] = float(h.midi(h.chordTones[k], 1));
                carriers[nc++] = float(h.midi(h.chordRoot, 0));
            }
            for (int i = 0; i < n; ++i) {
                const float mix = m_vocoderMix.step(m_preset == Vocoder ? 1.0f : 0.0f);
                float car = 0;
                for (int k = 0; k < nc; ++k)
                    for (int d = 0; d < 2; ++d) {
                        float &ph = m_carPhase[k][d];
                        const float dt = midiToHz(carriers[k] + (d ? 0.08f : -0.08f)) / m_rate;
                        ph += dt;
                        if (ph >= 1)
                            ph -= 1;
                        car += 2 * ph - 1 - polyBlep(ph, dt);
                    }
                car *= 0.35f / std::sqrt(float(nc));
                float out = 0;
                for (int b = 0; b < kBands; ++b) {
                    Band &bd = m_bands[b];
                    const float a = std::fabs(bd.a.step(dry[i]).band);
                    bd.env += (a - bd.env) * (a > bd.env ? 0.004f : 0.0007f);
                    out += bd.s.step(car).band * bd.env;
                }
                const float sib = m_sib.step(dry[i]).high;
                // As loud as the voice it speaks for: the bands' sum is
                // levelled against the dry voice, slowly.
                const float raw = out * 2.0f;
                m_vocIn += (dry[i] * dry[i] - m_vocIn) * 0.0005f;
                m_vocOut += (raw * raw - m_vocOut) * 0.0005f;
                const float match = clampf(std::sqrt((m_vocIn + 1e-9f) / (m_vocOut + 1e-9f)), 0.01f, 8.0f);
                m_vocGain += (match - m_vocGain) * 0.0005f;
                const float v = (raw * m_vocGain + sib * 0.5f + dry[i] * 0.12f) * mix;
                leadL[i] += v;
                leadR[i] += v;
            }
        }

        renderStyles(dry, leadL, leadR, n, h, tr, voiced, voicedNow, styleMidi, hz);

        // 3. The fingers: your voice again at each one's note.
        float bus[2][kBlock] = {};
        for (int i = 0; i < n; ++i) {
            bus[0][i] = leadL[i];
            bus[1][i] = leadR[i];
        }
        m_activeVoices = 0;
        int sounding = 0;
        for (const auto &v : m_voices)
            sounding += v.on;
        // More voices, each a little quieter: a chord of ten isn't ten
        // times as loud.
        const float spread = 1.0f / std::sqrt(float(std::max(1, sounding)));
        const double ref = m_leadMidi > 0 ? m_leadMidi : sungMidi;
        for (auto &v : m_voices) {
            if (!v.on && v.level.y < 0.0005f)
                continue;
            ++m_activeVoices;
            if (v.mode == HarmonyNote && sungMidi <= 0 && v.midi > 0) {
                // Nothing sung: a harmony keeps its last note.
            } else {
                v.midi = float(noteMidi(h, v.value, v.mode, ref, m_follow));
            }
            const float target = v.midi + v.detune * 0.3f;
            const float r = voiced ? std::exp2((target - sungMidi) / 12.0f) : 1.0f;
            if (onset || v.fresh)
                v.ratio.reset(r);
            v.fresh = false;
            const float r0 = v.ratio.y;
            for (int i = 0; i < n; ++i)
                v.ratio.step(r);
            const float lvl0 = v.level.y;
            float lvl1 = lvl0;
            for (int i = 0; i < n; ++i)
                lvl1 = v.level.step(v.on && fingerCopies(m_preset) ? v.gain * spread : 0.0f);
            if (lvl0 < 0.0005f && lvl1 < 0.0005f)
                continue;
            float tmp[kBlock] = {};
            const float vf = std::exp2((v.formant - 0.5f) * 1.4f);
            v.shifter.process(m_src, n, period, 0.5f * (r0 + v.ratio.y), vf, 1.0f, tmp);
            const float width = 0.6f + 0.4f * std::fabs(v.detune);
            const float pan = 0.5f + (v.pan - 0.5f) * width;
            const float gl = std::sqrt(1 - pan), gr = std::sqrt(pan);
            for (int i = 0; i < n; ++i) {
                const float g = lvl0 + (lvl1 - lvl0) * float(i + 1) / float(n);
                bus[0][i] += tmp[i] * g * gl;
                bus[1][i] += tmp[i] * g * gr;
            }
        }

        // The choir: the chord's tones nearest the note sung, one above,
        // one below, and the root under them.
        {
            const bool want = m_preset == Choir;
            if (want && voiced && (std::fabs(sungMidi - m_choirAt) > 0.7f || h.chordSeq != m_choirSeq)) {
                m_choirAt = sungMidi;
                m_choirSeq = h.chordSeq;
                double above = 1e9, below = -1e9;
                const int cn = std::clamp(h.chordCount, 1, 4);
                for (int oct = -2; oct <= 6; ++oct)
                    for (int k = 0; k < cn; ++k) {
                        const double m = h.midi(h.chordTones[k], oct);
                        if (m > sungMidi + 1.5 && m < above)
                            above = m;
                        if (m < sungMidi - 1.5 && m > below)
                            below = m;
                    }
                const double root = h.midi(h.chordRoot, 0);
                m_choirMidi[0] = float(above);
                m_choirMidi[1] = float(below);
                m_choirMidi[2] = float(root + 12.0 * std::floor((sungMidi - 7 - root) / 12.0));
            }
            static const float pans[3] = {0.15f, 0.85f, 0.5f};
            static const float throats[3] = {1.06f, 0.94f, 0.88f};
            for (int k = 0; k < 3; ++k) {
                ChoirVoice &c = m_choir[k];
                const float lvl0 = c.level.y;
                float lvl1 = lvl0;
                for (int i = 0; i < n; ++i)
                    lvl1 = c.level.step(want ? (k == 2 ? 0.5f : 0.7f) : 0.0f);
                if (lvl0 < 0.0005f && lvl1 < 0.0005f) {
                    c.shifter.reset();
                    continue;
                }
                const float r = voiced ? std::exp2((m_choirMidi[k] - sungMidi) / 12.0f) : 1.0f;
                const float r0 = c.ratio.y;
                for (int i = 0; i < n; ++i)
                    c.ratio.step(r);
                float tmp[kBlock] = {};
                c.shifter.process(m_src, n, period, 0.5f * (r0 + c.ratio.y), throats[k], 1.0f, tmp);
                const float gl = std::sqrt(1 - pans[k]), gr = std::sqrt(pans[k]);
                for (int i = 0; i < n; ++i) {
                    const float g = lvl0 + (lvl1 - lvl0) * float(i + 1) / float(n);
                    bus[0][i] += tmp[i] * g * gl;
                    bus[1][i] += tmp[i] * g * gr;
                }
            }
        }

        // 4. The strips, in order: filter, lo-fi, freeze or stutter, tape
        //    stop, then the throws.
        const double spb = tr.secondsPerBeat();
        {
            const float on = m_filt.on > 0.5f ? 1.0f : 0.0f;
            if (on > 0 || m_filtOn.y > 0.001f) {
                const float cut = m_filtCut.step(m_filt.on > 0.5f ? m_filt.x : m_filtCut.y);
                const float res = m_filtRes.step(m_filt.on > 0.5f ? m_filt.p : 0.0f);
                const float hzc = 160.0f * std::exp2(cut * 5.6f);
                const float q = 0.8f + res * 7.0f;
                m_filtL.set(hzc, q, m_rate);
                m_filtR.set(hzc, q, m_rate);
                for (int i = 0; i < n; ++i) {
                    const float w = m_filtOn.step(on);
                    const auto a = m_filtL.step(bus[0][i]), b = m_filtR.step(bus[1][i]);
                    // Pressure turns the low-pass into a wah: more band.
                    const float l = a.low * (1 - res) + a.band * res * 1.6f, r = b.low * (1 - res) + b.band * res * 1.6f;
                    bus[0][i] += (l - bus[0][i]) * w;
                    bus[1][i] += (r - bus[1][i]) * w;
                }
            }
        }
        {
            const float on = m_lofi.on > 0.5f ? clampf(0.45f + m_lofi.p, 0, 1) : 0.0f;
            if (on > 0 || m_lofiOn.y > 0.001f) {
                const bool radio = m_lofi.x < 0.5f;
                const float u = radio ? m_lofi.x * 2 : (m_lofi.x - 0.5f) * 2;
                if (radio) {
                    m_radioHp.set(350.0f + 600.0f * u, 0.9f, m_rate);
                    m_radioHp2.set(350.0f + 600.0f * u, 0.9f, m_rate);
                    m_radioLp.set(3200.0f - 1500.0f * u, 1.1f, m_rate);
                    m_radioLp2.set(3200.0f - 1500.0f * u, 1.1f, m_rate);
                }
                const float bits = std::exp2(11.0f - 8.0f * u);
                const int hold = 1 + int(u * 14.0f);
                for (int i = 0; i < n; ++i) {
                    const float w = m_lofiOn.step(on);
                    float l = bus[0][i], r = bus[1][i];
                    if (radio) {
                        l = fastTanh(m_radioLp.step(m_radioHp.step(l).high).low * (2.5f + 6.0f * u)) * 0.5f;
                        r = fastTanh(m_radioLp2.step(m_radioHp2.step(r).high).low * (2.5f + 6.0f * u)) * 0.5f;
                    } else {
                        if (++m_holdN >= hold) {
                            m_holdN = 0;
                            m_holdL = std::round(l * bits) / bits;
                            m_holdR = std::round(r * bits) / bits;
                        }
                        l = m_holdL;
                        r = m_holdR;
                    }
                    bus[0][i] += (l - bus[0][i]) * w;
                    bus[1][i] += (r - bus[1][i]) * w;
                }
            }
        }
        // What the freeze can take hold of: the voice so far, mono.
        for (int i = 0; i < n; ++i)
            m_recent[size_t(m_recentW++) & (m_recent.size() - 1)] = 0.5f * (bus[0][i] + bus[1][i]);
        {
            const bool stutter = m_frz.on > 0.5f && m_frz.x >= 0.5f;
            const bool cloud = m_frz.on > 0.5f && m_frz.x < 0.5f;
            const float lvl = clampf(0.5f + m_frz.p * 0.8f, 0, 1.2f);
            if (stutter || cloud || m_freezeLvl.y > 0.001f || m_stutterOn.y > 0.001f) {
                const double sliceBeats = kStutterBeats[std::clamp(
                    m_stutter + (m_frz.x > 0.83f ? 2 : m_frz.x > 0.66f ? 1 : 0), 0, kStutterCount - 1)];
                const int slice = std::clamp(int(sliceBeats * spb * m_rate), 64, int(m_freezeBuf.size()) - 1);
                const float pitch = std::exp2(clampf(m_frz.semis, -24, 24) / 12.0f);
                for (int i = 0; i < n; ++i) {
                    const float fl = m_freezeLvl.step(cloud ? lvl : 0.0f);
                    const float st = m_stutterOn.step(stutter ? 1.0f : 0.0f);
                    float add = 0;
                    if (fl > 0.0005f)
                        add += freezeCloud(pitch) * fl;
                    if (st > 0.0005f) {
                        // On the grid: each slice starts where the beat
                        // divides, playing the last slice heard before the
                        // finger came down.
                        const double beat = tr.beatAt(i);
                        const double k = std::floor(beat / sliceBeats);
                        if (k != m_stutterK) {
                            m_stutterK = k;
                            m_stutterPos = 0;
                        }
                        const int len = slice;
                        const int from = int(m_freezeBuf.size()) - len;
                        float s = 0;
                        if (m_stutterPos < len) {
                            const float fade = std::min(1.0f, std::min(float(m_stutterPos), float(len - m_stutterPos)) / 96.0f);
                            s = m_freezeBuf[size_t(from + int(m_stutterPos))] * fade;
                        }
                        m_stutterPos += 1;
                        bus[0][i] *= 1 - st;
                        bus[1][i] *= 1 - st;
                        add += s * st * lvl;
                    }
                    bus[0][i] += add;
                    bus[1][i] += add;
                }
            }
        }
        {
            // The tape: stopping winds it down to nothing over half a
            // second; letting go crossfades back to the voice as it is.
            const uint32_t mask = uint32_t(m_tapeL.size() - 1);
            for (int i = 0; i < n; ++i) {
                m_tapeL[m_tapeW & mask] = bus[0][i];
                m_tapeR[m_tapeW & mask] = bus[1][i];
                ++m_tapeW;
                if (m_tapeStop || m_tapeBack > 0) {
                    if (m_tapeStop)
                        m_tapeRate = std::max(0.0f, m_tapeRate - 1.0f / (0.55f * m_rate));
                    const double f = std::floor(m_tapeRead);
                    const uint32_t k = uint32_t(int64_t(f)) & mask;
                    const float u = float(m_tapeRead - f);
                    const float tl = m_tapeL[k] + (m_tapeL[(k + 1) & mask] - m_tapeL[k]) * u;
                    const float trr = m_tapeR[k] + (m_tapeR[(k + 1) & mask] - m_tapeR[k]) * u;
                    m_tapeRead += m_tapeRate;
                    const float g = std::min(1.0f, m_tapeRate * 6.0f);
                    if (m_tapeStop) {
                        bus[0][i] = tl * g;
                        bus[1][i] = trr * g;
                    } else {
                        m_tapeBack = std::max(0.0f, m_tapeBack - 1.0f / (0.03f * m_rate));
                        bus[0][i] = bus[0][i] * (1 - m_tapeBack) + tl * g * m_tapeBack;
                        bus[1][i] = bus[1][i] * (1 - m_tapeBack) + trr * g * m_tapeBack;
                    }
                }
            }
        }
        {
            const int div = std::clamp(int(m_echoP.x * 3.0f), 0, 2);
            static const float echoBeats[3] = {0.5f, 0.75f, 1.0f};
            m_echo.set(0.45f + 0.4f * m_echoP.p, float(spb) * echoBeats[div]);
            m_reverb.set(0.72f + 0.25f * m_revP.x, clampf(m_revP.semis, 0, 1));
            const float es = m_echoP.on > 0.5f ? 0.45f + 0.55f * m_echoP.p : 0.0f;
            const float rs = m_revP.on > 0.5f ? 0.45f + 0.55f * m_revP.p : 0.0f;
            const bool echoLive = es > 0 || m_echoSend.y > 0.0005f || m_echoTail > 0;
            const bool revLive = rs > 0 || m_revSend.y > 0.0005f || m_revTail > 0;
            for (int i = 0; i < n; ++i) {
                const float e = m_echoSend.step(es), r = m_revSend.step(rs);
                float el = 0, er = 0, rl = 0, rr = 0;
                if (echoLive)
                    m_echo.process(bus[0][i] * e, bus[1][i] * e, el, er);
                if (revLive)
                    m_reverb.process(bus[0][i] * r, bus[1][i] * r, rl, rr);
                bus[0][i] += el + rl * 0.8f;
                bus[1][i] += er + rr * 0.8f;
            }
            // The tails ring on for a while after the finger lifts.
            m_echoTail = es > 0 ? int(m_rate * 8) : std::max(0, m_echoTail - n);
            m_revTail = rs > 0 ? int(m_rate * 12) : std::max(0, m_revTail - n);
        }

        // Words played back off the tape by the fingers on it: after the
        // effects (they are on the tape already), before the tape, so a
        // loop can take a chopped performance.
        {
            const int64_t written = tape.written.load(std::memory_order_relaxed);
            for (auto &v : m_chops) {
                if (!v.on && v.fade <= 0)
                    continue;
                for (int i = 0; i < n; ++i) {
                    v.fade = v.on ? std::min(1.0f, v.fade + 0.004f) : v.fade - 0.002f;
                    if (v.fade <= 0) {
                        v.fade = 0;
                        break;
                    }
                    const int64_t k = int64_t(v.pos);
                    float a = 0, b = 0;
                    if (k < written && written - k < kRing - 4096) {
                        a = tape.wetL[size_t(k & kRingMask)];
                        b = tape.wetR[size_t(k & kRingMask)];
                    }
                    // Each pass through the word is faded in and out, so a
                    // held word repeats without a click.
                    const double into = v.pos - double(v.start);
                    const float edge = float(std::min(into, double(v.length) - into)) / 240.0f;
                    const float g = v.level * v.fade * clampf(edge, 0, 1);
                    bus[0][i] += a * g;
                    bus[1][i] += b * g;
                    v.pos += v.rate;
                    if (v.pos >= double(v.start) + v.length)
                        v.pos = double(v.start);
                }
            }
        }

        // 5. Onto the tape for Space (always, heard or not), into the ears
        //    (when monitored), and the loops on top.
        float livePeak = 0;
        const float mon = m_monitor && m_feedback <= 0 ? 1.0f : m_monitor ? 0.06f : 0.0f;
        // The live voice goes to the studio's monitor bus, round the
        // limiter's look-ahead; the loops below go into the mix.
        float *monL = ctx.monitor(0), *monR = ctx.monitor(1);
        if (!monL || !monR) {
            monL = outL;
            monR = outR;
        }
        for (int i = 0; i < n; ++i) {
            // (The DC blockers: a voice levelled by a quick compressor, or
            // a lopsided pluck, carries an offset of a few 10⁻⁴, which no
            // loop or ear wants, and the monitor bus has no blocker of its
            // own after this.)
            const float l = m_outDcL.step(fastTanh(bus[0][i] * 0.9f) / 0.9f),
                        r = m_outDcR.step(fastTanh(bus[1][i] * 0.9f) / 0.9f);
            const int64_t s = int64_t(tr.sample) + i;
            tape.wetL[size_t(s & kRingMask)] = l;
            tape.wetR[size_t(s & kRingMask)] = r;
            const float g = m_monGain.step(mon);
            monL[i] += l * g;
            monR[i] += r * g;
            livePeak = std::max(livePeak, std::max(std::fabs(l), std::fabs(r)));
        }
        m_livePeak = std::max(livePeak, m_livePeak * 0.97f);
        tape.written.store(int64_t(tr.sample) + n, std::memory_order_release);
        if (m_feedback > 0)
            m_feedback -= n;

        renderLoops(outL, outR, n, ctx);
    }

    void silence(bool hard) override {
        for (auto &v : m_voices) {
            v.on = false;
            v.id = -1;
            if (hard)
                v.level.reset(0);
        }
        for (auto &l : m_loops) {
            if (hard)
                l.id = -1;
            else
                l.fading = true;
        }
        m_frz.on = 0;
        m_filt.on = 0;
        m_lofi.on = 0;
        m_echoP.on = 0;
        m_revP.on = 0;
        if (m_tapeStop) {
            m_tapeStop = false;
            m_tapeBack = 1;
        }
    }

    void publish(float *d) override {
        d[pub::InPeak] = m_inPeak;
        d[pub::InRms] = m_inCount ? std::sqrt(m_inSq / float(m_inCount)) : 0;
        m_inPeakHeld = std::max(m_inPeak, m_inPeakHeld * 0.9f);
        m_inPeak = 0;
        m_inSq = 0;
        m_inCount = 0;
        d[pub::Hz] = m_gate > 0.3f ? m_pitch.hz() : 0;
        d[pub::Clarity] = m_pitch.clarity();
        d[pub::Gate] = m_gate;
        d[pub::Lift] = 20.0f * std::log10(std::max(1.0f, m_lift));
        d[pub::Feedback] = m_feedback > 0 ? 1 : 0;
        d[pub::Monitor] = m_monitor ? 1 : 0;
        d[pub::LivePeak] = m_livePeak;
        d[pub::Tape] = m_tapeStop ? m_tapeRate : 1;
        d[pub::Freeze] = std::max(m_freezeLvl.y, m_stutterOn.y);
        int loops = 0;
        for (const auto &l : m_loops)
            loops += l.id >= 0;
        d[pub::Loops] = float(loops);
        d[pub::LeadMidi] = m_leadMidi;
        d[pub::HasInput] = m_hasInput ? 1 : 0;
        d[pub::Stutter] = float(m_stutter);
        d[pub::Overtone] = m_overtone;
        d[pub::OvertoneMidi] = m_overtoneMidi;
        d[pub::TalkMidi] = m_talkNow;
        d[pub::TalkTarget] = m_talkNote;
        d[pub::TalkGate] = m_preset == Talk ? m_talkLevel : 0;
        d[pub::TalkOpen] = m_talkOpen;
        d[pub::SitarBuzz] = m_sitar.buzz();
        d[pub::SitarLevel] = m_sitarTail > 0 ? m_sitarLvl : 0;
        m_sitarLvl = 0;
        for (int k = 0; k < Strings::kCount; ++k)
            d[pub::Taraf + k] = m_sitarTail > 0 ? m_strings.level(k) : 0;
        d[pub::GamCycle] = m_gamCycle;
        d[pub::GamStruck] = m_gamNote;
        d[pub::GamStruckAge] = float(std::min(m_gamStruckAge, 1 << 24)) / m_rate;
        d[pub::ScrewLag] = m_preset == Screwed ? m_scLag : 0;
        d[pub::ScrewLimit] = m_scLimit;
        d[pub::ScrewSpin] = m_spinHeld || m_spinLvl > 0.05f ? 1 : 0;
        d[pub::ScrewSemis] = 12.0f * std::log2(screwSpeed(m_screwSpeed));
        d[pub::ScrewDrop] = m_preset == Screwed ? m_dropIn : -1.0f;
        d[pub::ScrewDropBar] = m_dropStop ? 1.0f : 0.0f;
        d[pub::ScrewDeckB] = m_preset == Screwed ? m_repLvl.y : 0.0f;
        d[pub::RiotOctave] = float(m_scream.octave());
        d[pub::RiotStrain] = m_preset == Riot ? m_scream.strain() : 0.0f;
        // The lag of a shifted copy: about half a period, plus what the
        // formant asks.
        d[pub::Lag] = m_pitch.period() > 0 ? m_pitch.period() * 0.5f / m_rate * 1000.0f : 3.0f;
        for (int k = 0; k < kMaxVoices; ++k) {
            const Voice &v = m_voices[k];
            d[pub::Voices + 3 * k] = v.on ? 1 : 0;
            d[pub::Voices + 3 * k + 1] = v.midi;
            d[pub::Voices + 3 * k + 2] = v.level.y;
        }
        const int per = kScopeLen / pub::ScopeN;
        for (int k = 0; k < pub::ScopeN; ++k) {
            float lo = 0, hi = 0;
            for (int j = 0; j < per; ++j) {
                const float v = m_scope[(m_scopeW + uint32_t(k * per + j)) & (kScopeLen - 1)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            d[pub::Scope + 2 * k] = lo;
            d[pub::Scope + 2 * k + 1] = hi;
        }
    }

    int voices() const override { return m_activeVoices; }

private:
    void track() {
        // Feedback: a pure tone, loud, that doesn't waver for half a
        // second is the speakers singing into the microphone, not a voice
        // (which always wavers a little). The voice ducks for two seconds.
        const float hz = m_pitch.hz();
        if (hz > 0 && m_pitch.clarity() > 0.99f && m_gateEnv > 0.15f && m_howlHz > 0 &&
            std::fabs(hz - m_howlHz) < 0.0015f * hz)
            m_howl += 128;
        else
            m_howl = 0;
        m_howlHz = hz;
        if (m_howl > int(m_rate * 0.8f) && m_monitor) {
            m_feedback = int(m_rate * 2);
            m_howl = 0;
        }
    }

    // --- the five styles from further afield ------------------------------------
    void initStyles(float rate) {
        m_talk.init(rate);
        m_talkMix.setTime(0.03f, rate);
        // The G-funk portamento: a Minimoog's glide, an exponential approach
        // in pitch that lands in about a fifth of a second, so a leap is a
        // slide you hear.
        m_talkGlide.setTime(0.06f, rate);
        m_talkGlide.reset(57);
        m_talkSib.set(5200.0f, 0.7f, rate);
        for (int s = 0; s < 2; ++s) {
            m_tubeHp[s].set(150.0f, 0.7f, rate);
            m_tubePeak[s].set(1700.0f, 1.3f, rate);
        }
        m_talkSubLvl.setTime(0.05f, rate);
        m_talkAir[0].set(3500.0f, 0.7f, rate);
        m_talkAir[1].set(3500.0f, 0.7f, rate);
        m_talkTop[0].set(14000.0f, 0.6f, rate);
        m_talkTop[1].set(14000.0f, 0.6f, rate);
        m_talkSubLvl.reset(1);
        m_talkGate.setTime(0.02f, rate);
        m_talkPress.setTime(0.02f, rate);
        m_talkPress.reset(0.5f);
        m_talkGate.reset(1);
        m_throatMix.setTime(0.04f, rate);
        m_riotMix.setTime(0.03f, rate);
        m_scream.init(rate);
        m_whOn.setTime(0.06f, rate);
        m_underLvl.setTime(0.03f, rate);
        m_throatTone.set(3400.0f, 0.6f, rate);
        m_thPress.set(260.0f, 0.6f, rate);
        m_thCeil.limit = 0.36f;
        m_sitar.init(rate);
        // A string rings three seconds: long enough to sing over, short
        // enough that a rest between phrases is heard as one.
        m_sitar.setDecay(3.0f);
        m_sitar.setJawari(0.45f, 0.015f);
        for (auto &c : m_chik) {
            c.init(rate);
            c.setDecay(2.0f);
            c.setJawari(0.45f, 0.01f);
        }
        for (auto &f : m_sitFing) {
            f.init(rate);
            f.setDecay(4.0f);
            f.setJawari(0.45f, 0.015f);
        }
        m_sitarBody.set(320.0f, 1.4f, rate);
        // Under the talk box's ceiling: the voice's breath and the room
        // are added to it on the way.
        m_sitCeil.limit = 0.36f;
        // A pluck's peak is its first few milliseconds: the ceiling lets go
        // of it quickly, so the string's ring after it isn't held down.
        m_sitCeil.setRelease(0.025f, rate);
        // Butterworth fourth order: two stages, Q 0.54 and 1.31.
        for (int side = 0; side < 2; ++side)
            for (int st = 0; st < 2; ++st) {
                m_sitHp[side][st].set(55.0f, st ? 1.3066f : 0.5412f, rate);
                m_sitLp[side][st].set(9500.0f, st ? 1.3066f : 0.5412f, rate);
                m_sitHp2[side].set(45.0f, 0.7071f, rate);
            }
        m_strings.init(rate);
        m_sitarMix.setTime(0.05f, rate);
        m_bronze.init(rate);
        m_polos.init(rate, 4500.0f);
        m_sangsih.init(rate, 4500.0f);
        m_gongs.init(rate, 220.0f);
        m_gamMix.setTime(0.03f, rate);
        // (A strike's peak is gone in a moment, as a pluck's is.)
        m_gamCeil.limit = 0.36f;
        m_gamCeil.setRelease(0.03f, rate);
        m_screwMix.setTime(0.03f, rate);
        m_scIn.assign(size_t(1) << 18, 0.0f);
        m_scOut.assign(size_t(1) << 17, 0.0f);
        m_scLp.set(4600.0f, 0.6f, rate);
        m_scLp2.set(4600.0f, 0.6f, rate);
        m_scHiss.set(4800.0f, 0.5f, rate);
        // The crossfader's cut: 3 ms, a flick.
        m_repLvl.setTime(0.003f, rate);
        m_deckBTone.set(3200.0f, 0.6f, rate);
        for (auto &v : m_voices) {
            v.syn.setTime(0.012f, rate);
            v.syn.reset(0);
            // The harmonies glide as the lead does: one hand.
            v.synGlide.setTime(0.06f, rate);
            v.synGlide.reset(57);
        }
    }

    // A talk box's synth, one note, as a funk player sets a Minimoog: a
    // sawtooth, a pulse whose width sways slowly (it moves inside the sound
    // without beating), and a square an octave down for weight, all three
    // on one phase so nothing beats against anything. Band-limited.
    inline float talkOsc(float *ph, float midi, float width, float sub) {
        const float dt = midiToHz(midi) / m_rate, dt2 = dt * 0.5f;
        ph[0] += dt;
        ph[1] += dt2;
        if (ph[0] >= 1) {
            ph[0] -= 1;
            // The sub's phase is the main one's, halved: kept so exactly.
            ph[1] = (ph[1] >= 0.75f || ph[1] < 0.25f ? 0.0f : 0.5f) + ph[0] * 0.5f;
        }
        if (ph[1] >= 1)
            ph[1] -= 1;
        const float p = ph[0];
        const float saw = 2 * p - 1 - polyBlep(p, dt);
        float q = p + width;
        if (q >= 1)
            q -= 1;
        const float pulse = saw - (2 * q - 1 - polyBlep(q, dt));
        const float s = ph[1];
        float s2 = s + 0.5f;
        if (s2 >= 1)
            s2 -= 1;
        const float sq = (s < 0.5f ? 1.0f : -1.0f) + polyBlep(s, dt2) - polyBlep(s2, dt2);
        return 0.5f * saw + 0.4f * pulse + 0.45f * sub * sq;
    }
    // The talk box's driver and tube, on one side's synth: a compression
    // driver pushed a little (even harmonics: the bias off centre), its
    // lows thinned, and the horn-like peak the driver and the tube put
    // round 1.7 kHz.
    inline float talkTube(int side, float x) {
        const float driven = fastTanh(1.8f * x + 0.3f) - 0.2913f; // tanh(0.3): no offset at rest
        const float hp = m_tubeHp[side].step(driven).high;
        return hp + 0.9f * m_tubePeak[side].step(hp).band;
    }

    // Strikes a bar at `midi` for the gamelan, an octave over the voice,
    // where a gendér rings.
    void strikeBronze(float midi, float velocity) {
        static const Bronze::Spec bar = {
            {1.0f, 2.756f, 5.404f, 8.933f}, {1.0f, 0.35f, 0.15f, 0.06f}, {3.5f, 1.5f, 0.7f, 0.3f},
            6.0f, 0.7f, 6.0f, 0.12f, 0.35f, 1};
        m_bronze.strike(foldMidi(midi + 12, 62, 88), velocity, bar);
        m_gamNote = midi;
        m_gamStruckAge = 0;
        m_bronzeTail = int(m_rate * 8);
    }
    static float foldMidi(double m, double lo, double hi) {
        while (m < lo)
            m += 12;
        while (m > hi)
            m -= 12;
        return float(m);
    }
    // A colotomic stroke: 0 the gong ageng (the cycle's end and start), 1
    // the kempul (the half bar), 2 the kenong (the bar). Low, soft, long,
    // and each pair beating slowly, the big gong's beat the slowest (0.7 Hz,
    // heard in each ear) and its pitch sinking 12 cents over three seconds.
    // The kempul and kenong strike the balungan: the note being sung at
    // that beat (`balungan`, in the gamelan's tuning), the kempul an octave
    // or so under the voice, the kenong above it; with nothing sung yet,
    // the scale's fifth and its root. The gong ageng is the key's root.
    void strikeGong(const Harmony &h, int which, float velocity, double balungan) {
        static const Bronze::Spec specs[3] = {
            {{1.0f, 2.01f, 2.98f, 4.15f}, {1.0f, 0.4f, 0.2f, 0.08f}, {9.0f, 5.0f, 3.0f, 1.5f}, 0.7f, 0.7f, 10.0f,
             0.35f, 0.12f, 8, 12.0f, 3.0f, 0.9f},
            {{1.0f, 2.02f, 3.01f, 4.2f}, {1.0f, 0.35f, 0.15f, 0.06f}, {5.0f, 3.0f, 1.8f, 1.0f}, 1.3f, 0.5f, 6.0f,
             0.15f, 0.12f, 8, 5.0f, 1.5f, 0.6f},
            {{1.0f, 2.0f, 3.0f, 4.05f}, {1.0f, 0.3f, 0.12f, 0.05f}, {3.5f, 2.0f, 1.2f, 0.6f}, 2.2f, 0.4f, 4.0f,
             0.08f, 0.15f, 8, 4.0f, 1.0f, 0.5f},
        };
        const int n = m_gamTuning == Pelog ? 7 : m_gamTuning == Slendro ? 5 : std::max(1, h.tuning.degreesPerOctave());
        const int fifth = m_gamTuning == Slendro ? 3 : 4;
        double midi = h.tuning.baseMidi() - 12.0;
        if (which == 1)
            midi = balungan > 0 ? foldMidi(balungan, 45, 57) : gamMidi(h, m_gamTuning, fifth);
        else if (which == 2)
            midi = balungan > 0 ? foldMidi(balungan, 57, 69) : gamMidi(h, m_gamTuning, n);
        m_gongs.strike(float(midi), velocity, specs[which]);
        m_bronzeTail = int(m_rate * 10);
    }

    void renderStyles(const float *dry, float *leadL, float *leadR, int n, const Harmony &h, const Transport &tr,
                      bool voiced, bool voicedNow, float sungMidi, float hz) {
        // How long since the voice was last heard: the styles that keep a
        // cycle of their own (the sitar's drones, the gamelan's gongs) keep
        // it while you sing, and let it go a bar after you stop.
        m_sungAgo = voicedNow ? 0 : std::min(m_sungAgo + n, 1 << 30);
        const bool singing = m_sungAgo < int(4.0 * tr.samplesPerBeat());
        // Whether the pitch can be trusted to play an instrument by: a voice
        // sounding, the tracker sure of it, and the voice not falling away.
        // As a note dies the tracker drifts (often upwards); a string or a
        // whistle that followed it would chirp at the end of every phrase,
        // so they hold their last note instead.
        for (int i = 0; i < n; ++i) {
            const float a = std::fabs(dry[i]);
            m_stEnv += (a - m_stEnv) * (a > m_stEnv ? 0.004f : 0.0006f);
            m_stPeak = std::max(m_stEnv, m_stPeak * 0.99998f);
        }
        const bool steady = voicedNow && hz > 0 && m_pitch.clarity() > 0.75f && m_stEnv > 0.45f * m_stPeak;

        // The talk box: a funk synth up the tube, your mouth shaping it.
        //
        // SUNG: the synth plays the note you sing, snapped hard to the key
        // (or the chord, following), held through consonants and gaps as a
        // player keeps the key down, slid into from the last note with a
        // Minimoog's glide, scooped up into from a semitone under after a
        // rest, its vibrato coming in on a held note. Each finger adds a
        // synth note a harmony from it, every other one to each side; every
        // note is moved by the one hand (the same glide, scoop and vibrato),
        // so a chord moves together instead of beating against itself.
        //
        // KEYS: as a talk box is played. The lanes are a keyboard: the
        // latest finger held sets the synth's note, mono, with legato glide
        // from the finger before it as a Minimoog's; the voice only shapes
        // it, so there is no sound but the words' s and t without a key.
        if (m_preset == Talk || m_talkMix.y > 0.001f) {
            const Voice *key = nullptr;
            if (m_talkKeys)
                for (const auto &v : m_voices)
                    if (v.on && (!key || v.order > key->order))
                        key = &v;
            if (m_talkKeys) {
                if (key) {
                    const float note = float(noteMidi(h, key->value, key->mode));
                    // A key pressed after a lift starts on its note; one
                    // pressed over another (legato) glides from it. Only
                    // the first fires the filter's contour, as a Minimoog's
                    // single trigger does: a legato line stays smooth.
                    if (!m_talkKeyHeld) {
                        m_talkGlide.reset(note);
                        m_talkEnv = 1;
                    }
                    if (std::fabs(note - m_talkNote) > 0.01f)
                        m_talkHeld = 0;
                    m_talkNote = note;
                }
                m_talkKeyHeld = key != nullptr;
                m_talkRest = false;
            } else if (steady) {
                const float note = float(snapMidi(h, sungMidi, m_follow));
                if (std::fabs(note - m_talkNote) > 0.01f)
                    m_talkHeld = 0;
                m_talkNote = note;
                if (m_talkRest)
                    m_talkGlide.reset(note - 1.0f);
                m_talkRest = false;
            }
            m_talkQuiet = voicedNow ? 0 : m_talkQuiet + n;
            if (!m_talkKeys)
                m_talkRest |= m_talkQuiet > int(m_rate * 0.1f);
            m_talkHeld += float(n) / m_rate;
            if (m_preset == Talk)
                m_leadMidi = m_talkNote;
            const float depth = 0.16f * clampf((m_talkHeld - 0.3f) / 0.5f, 0, 1);
            float fingerMidi[kMaxVoices];
            int sounding = 1;
            for (int k = 0; k < kMaxVoices; ++k) {
                Voice &v = m_voices[k];
                fingerMidi[k] = float(noteMidi(h, v.value, v.mode, m_talkNote, m_follow));
                const bool plays = v.on && !m_talkKeys;
                sounding += plays;
                // A harmony that starts starts where the lead is in its
                // glide, by its own interval: the hand comes down as one.
                if (plays && v.syn.y < 0.0005f)
                    v.synGlide.reset(fingerMidi[k] + (m_talkGlide.y - m_talkNote));
            }
            const float norm = 1.0f / std::sqrt(float(sounding));
            const bool gateOpen = !m_talkKeys || key != nullptr;
            // KEYS: the synth's own filter, as a player digs into the keys.
            // The key's pressure opens it (from 350 Hz, dark and round,
            // to over 7 kHz, all buzz), and each fresh key fires a contour
            // that opens it a further octave and a half and closes over a
            // quarter of a second: the vowels come through dark or bright
            // as you press. Sung, the filter stands open.
            const float press = m_talkKeys ? (key ? clampf(key->gain / 1.2f, 0.0f, 1.0f) : m_talkPressHeld) : 1.0f;
            if (key)
                m_talkPressHeld = press;
            const float envFall = std::exp(-float(n) / (0.25f * m_rate));
            for (int i = 0; i < n; ++i) {
                const float mix = m_talkMix.step(m_preset == Talk ? 1.0f : 0.0f);
                const float gate = m_talkGate.step(gateOpen ? 1.0f : 0.0f);
                const float lead = m_talkGlide.step(m_talkNote);
                m_vibPhase += 5.3f / m_rate;
                if (m_vibPhase >= 1)
                    m_vibPhase -= 1;
                m_pwmPhase += 0.31f / m_rate;
                if (m_pwmPhase >= 1)
                    m_pwmPhase -= 1;
                const float vib = depth * std::sin(kTwoPi * m_vibPhase);
                const float width = 0.5f + 0.22f * std::sin(kTwoPi * m_pwmPhase);
                const float sub = m_talkSubLvl.step(m_talkSub ? 1.0f : 0.0f);
                m_talkNow = lead + vib;
                const float c = talkOsc(m_talkPh, m_talkNow, width, sub) * gate;
                float cl = c, cr = c;
                const float open = m_talkPress.step(press);
                int side = 0;
                for (int k = 0; k < kMaxVoices; ++k) {
                    Voice &v = m_voices[k];
                    const float g = v.syn.step(v.on && !m_talkKeys && m_preset == Talk ? clampf(v.gain, 0, 1.2f) : 0.0f);
                    const float fm = v.synGlide.step(fingerMidi[k]);
                    if (g > 0.0005f) {
                        // The lead's vibrato on every note, in step; and no
                        // sub under the harmonies (one bass under a chord,
                        // not three a few hertz apart).
                        const float o = talkOsc(v.synPh, fm + vib, width, 0.0f) * g;
                        cl += o * (side ? 0.35f : 0.95f);
                        cr += o * (side ? 0.95f : 0.35f);
                        side ^= 1;
                    }
                }
                // (The cutoff moves every 16 samples: a contour of a quarter
                // second, and a hand's pressure, move far slower than that.)
                if ((m_talkTick++ & 15) == 0) {
                    const float hz = m_talkKeys ? 350.0f * std::exp2(4.5f * open + 1.5f * m_talkEnv) : 18000.0f;
                    m_talkFilt[0].set(hz, 1.4f, m_rate);
                    m_talkFilt[1].set(hz, 1.4f, m_rate);
                }
                if (m_talkKeys) {
                    cl = m_talkFilt[0].step(cl).low;
                    cr = m_talkFilt[1].step(cr).low;
                }
                float yl, yr;
                m_talk.step(dry[i], talkTube(0, cl * norm), talkTube(1, cr * norm), yl, yr);
                // As loud as the voice it speaks for, levelled slowly as
                // the vocoder is; the s and t of the words themselves on
                // top, so they're understood.
                const float y = 0.5f * (yl + yr);
                m_talkIn += (dry[i] * dry[i] - m_talkIn) * 0.0005f;
                m_talkOut += (y * y - m_talkOut) * 0.0005f;
                // (Levelled only while it sounds: KEYS with no key held
                // would otherwise wind the gain up to the top.)
                if (gate > 0.5f) {
                    const float match = clampf(std::sqrt((m_talkIn + 1e-9f) / (m_talkOut + 1e-9f)), 0.05f, 30.0f);
                    m_talkGain += (match - m_talkGain) * 0.0005f;
                }
                const float sib = m_talkSib.step(dry[i]).high * 0.45f;
                // The horn's air (what the synth has over 3.5 kHz, lifted),
                // then the ceiling, last: a clipper before the lift, or a
                // tanh after it, made edges whose peaks fell between the
                // samples, two dB over what the samples said. Pushed into it
                // a little, to stay as loud as the voice; and nothing over
                // 14 kHz after it (a talk box's horn has none up there).
                float ol = yl * m_talkGain * 1.25f, orr = yr * m_talkGain * 1.25f;
                ol += 0.8f * m_talkAir[0].step(ol).high + sib;
                orr += 0.8f * m_talkAir[1].step(orr).high + sib;
                m_talkCeil.step(ol, orr);
                leadL[i] += m_talkTop[0].step(ol).low * mix;
                leadR[i] += m_talkTop[1].step(orr).low * mix;
            }
            m_talkLevel = m_talkGate.y;
            m_talkEnv *= envFall;
            m_talkOpen = m_talkKeys ? clampf((4.5f * m_talkPress.y + 1.5f * m_talkEnv) / 6.0f, 0.0f, 1.0f) : 1.0f;
        } else {
            m_talkNote = voiced ? float(snapMidi(h, sungMidi, m_follow)) : m_talkNote;
            m_talkGlide.reset(m_talkNote);
            m_talkNow = m_talkNote;
            m_talkKeyHeld = false;
        }

        // Throat singing, in one of three styles (B): the voice is the
        // drone, and over or in it a melody made of the drone's own
        // harmonics, moving from one to the next.
        //
        //   SYGYT     the drone pressed and thin, a whistle high over it
        //             (1 to 3.4 kHz) on one harmonic, a gap notched round it.
        //   KHOOMEI   softer: the overtone lower (650 Hz to 2 kHz), lifted
        //             less, the gap only half cut, the drone left whole.
        //   KARGYRAA  the false folds: every other glottal cycle weaker, and
        //             the voice moved down half its pitch as well, so the
        //             subharmonic an octave under the sung note leads. Its
        //             melody is a vowel's: a resonance walking the
        //             subharmonic's series between 400 Hz and 1.5 kHz.
        //
        // Alone, the melody sings a four-bar figure on the harmonics that
        // fall in the key, home on the key's root at every bar line; a
        // finger picks the harmonic nearest its note instead (the highest
        // finger, if several). The note is folded up by octaves into the
        // style's band first, and of the folds the one with the harmonic
        // truest to it is taken (the lower if about as true): rounding the
        // raw ratio and doubling it would land every lane on one harmonic.
        //
        // The whistle (and khoomei's overtone) is the voice's own harmonic,
        // pinned. It is found where the voice has it now (k times the
        // voice's pitch, followed through 15 ms), heterodyned down to
        // nothing by an oscillator there, low-passed at 35 Hz (which leaves
        // the harmonic alone, its neighbours a whole drone away), and
        // heterodyned back up at k times the drone's slow pitch (followed
        // through 150 ms): the singer's own harmonic, its breath and its
        // swells, but not k times their vibrato, which at the sixteenth
        // would be a third of a semitone's wobble. Under it a steady
        // support at the same pinned pitch and in phase with it (the phase
        // the harmonic has had over the last third of a second), so the
        // two never beat. When the harmonic changes the whistle sweeps
        // through the ones between over a tenth of a second, as the tongue
        // moves. And it listens only to a steady drone: when the voice
        // starts to fall away, or its pitch gets unsure, the drone and the
        // harmonic hold where they were and the whistle fades on its last
        // note, so the end of a phrase has no chirp.
        //
        // The whole style then goes through its own ceiling: a whistle
        // lifted 40 dB rides on the drone's peaks.
        if (m_preset == Throat || m_throatMix.y > 0.001f) {
            const ThroatShape &sh = throatShape(m_throatStyle);
            if (steady) {
                // The drone's pitch, kept from slipping an octave: a held
                // drone doesn't leap, so a reading an octave off is the
                // tracker's mistake, folded back.
                const float oct = std::log2(hz / std::max(1.0f, m_droneHz));
                const float r = std::round(oct);
                m_droneHz = m_droneHz > 0 && r != 0 && std::fabs(oct - r) < 0.06f ? hz * std::exp2(-r) : hz;
            } else if (!voiced && m_whOn.y < 0.01f) {
                m_droneHz = 0;
                m_kNow = 0;
            }
            // The slow drone, in pitch: what the whistle stands on; and the
            // quick one: where the voice's own harmonic is now.
            if (m_droneHz <= 0) {
                m_droneSlow = m_droneFast = 0;
            } else if (m_droneSlow <= 0 || (steady && m_whHold == 0 && std::fabs(std::log2(m_droneHz / m_droneSlow)) > 0.04f)) {
                // (A new drone after a rest is a new note, not a slide from
                // the last: the whistle starts on it, not sweeping to it.)
                m_droneSlow = m_droneFast = m_droneHz;
            } else {
                m_droneSlow *= std::exp2(std::log2(m_droneHz / m_droneSlow) *
                                         (1.0f - std::exp(-float(n) / (0.15f * m_rate))));
                m_droneFast *= std::exp2(std::log2(m_droneHz / m_droneFast) *
                                         (1.0f - std::exp(-float(n) / (0.015f * m_rate))));
            }
            const float drone = m_droneSlow;
            // The series the melody walks: the drone's, or in kargyraa the
            // subharmonic's.
            const float series = sh.sub ? 0.5f : 1.0f;
            const float base = drone * series, baseFast = m_droneFast * series;
            if (steady && drone > 0) {
                int k = 0;
                float best = -1;
                for (const auto &v : m_voices)
                    if (v.on) {
                        const float m = float(noteMidi(h, v.value, v.mode, sungMidi, m_follow));
                        if (m > best)
                            best = m;
                    }
                if (best > 0) {
                    k = throatHarmonic(midiToHz(best), base, sh.lo, sh.hi, m_kTarget);
                } else {
                    // The harmonics in the band that sit within 30 cents of
                    // the key: the melody's scale (the seventh, 31 cents
                    // flat, is left out). Reckoned from the key's note
                    // nearest the drone, so a wavering voice doesn't keep
                    // changing the scale.
                    const float home = midiToHz(snapMidi(h, midiOfHz(drone), false)) * series;
                    int scale[24];
                    int count = 0, root = -1;
                    const int rootPc = ((int(std::lround(h.midi(0, 0))) % 12) + 12) % 12;
                    for (int j = 2; j <= 48 && count < 24; ++j) {
                        const float f = float(j) * home;
                        if (f < sh.lo || f > sh.hi)
                            continue;
                        const float m = midiOfHz(f);
                        const float snapped = float(snapMidi(h, m, false));
                        if (std::fabs(snapped - m) < 0.3f) {
                            // The key's root, the lowest that is one: the
                            // figure's home.
                            if (root < 0 && ((int(std::lround(snapped)) % 12) + 12) % 12 == rootPc)
                                root = count;
                            scale[count++] = j;
                        }
                    }
                    if (count > 0) {
                        if (root < 0)
                            root = count / 3;
                        // Four bars, in steps of the scale from the root:
                        // the root on every bar line, and from it a turn, a
                        // climb, a leap down from the top, and a dip under
                        // the root to come home. (A step past the scale's
                        // end turns back the other way.)
                        static const int figure[16] = {0, 1, 2, 1, 0, 2, 3, 4, 0, 5, 4, 3, 0, -1, -2, -1};
                        const int beat = int(std::floor(tr.beatAt(0)));
                        const int step = figure[((beat % 16) + 16) % 16];
                        int j = root + step;
                        if (j > count - 1)
                            j = std::max(0, root - (j - (count - 1)));
                        if (j < 0)
                            j = std::min(count - 1, root - j);
                        k = scale[std::clamp(j, 0, count - 1)];
                    }
                }
                if (k > 0 && k != m_kTarget) {
                    m_kTarget = k;
                    // A whistle that was silent starts on its harmonic;
                    // one that sings sweeps there, over a tenth of a second
                    // whatever the leap.
                    if (m_whOn.y < 0.05f || m_kNow <= 0)
                        m_kNow = float(k);
                    m_kSpeed = std::max(std::fabs(float(k) - m_kNow) / (0.1f * m_rate), 1.0f / (0.06f * m_rate));
                }
            }
            // The whistle stays on through a moment's doubt (the tracker
            // unsure for an estimate or two in a vibrato): it is let go
            // only after a tenth of a second of it, or it would stutter.
            m_whHold = steady && drone > 0 ? int(m_rate * 0.1f) : std::max(0, m_whHold - n);
            const bool whistling = m_whHold > 0 && drone > 0 && voiced;
            // The growl's phase runs at the voice's own pitch, as it is now.
            const float cycleHz = m_droneHz > 0 ? m_droneHz : (hz > 0 ? hz : 110.0f);
            // (Kargyraa's half-pitch copy keeps only its lowest partials: the
            // subharmonic and the one over it, no buzz on top.)
            m_kgSubLp.set(std::max(160.0f, 1.6f * cycleHz), 0.7f, m_rate);
            const float zA = 1.0f - std::exp(-kTwoPi * 35.0f / m_rate), zS = 1.0f - std::exp(-kTwoPi * 3.0f / m_rate);
            for (int i = 0; i < n; ++i) {
                const float mix = m_throatMix.step(m_preset == Throat ? 1.0f : 0.0f);
                const float x = dry[i];
                const float a = std::fabs(x);
                m_thEnv += (a - m_thEnv) * (a > m_thEnv ? 0.004f : 0.0006f);
                const float on = m_whOn.step(whistling ? 1.0f : 0.0f);
                if (m_kNow > 0 && m_kNow != float(m_kTarget) && m_kTarget > 0) {
                    const float d = float(m_kTarget) - m_kNow;
                    m_kNow = std::fabs(d) <= m_kSpeed ? float(m_kTarget) : m_kNow + (d > 0 ? m_kSpeed : -m_kSpeed);
                }
                // The filters follow the harmonic every 16 samples.
                if ((m_thTick++ & 15) == 0 && base > 0 && m_kNow > 0) {
                    m_whBp[0].set(m_kNow * baseFast, sh.q, m_rate);
                    m_whBp[1].set(m_kNow * base, sh.q * 1.6f, m_rate);
                    static const float off[4] = {-2, -1, 1, 2};
                    for (int j = 0; j < 4; ++j)
                        m_whNotch[j].set((m_kNow + off[j]) * base, 8.0f, m_rate);
                }
                // Kargyraa: the strong cycle and the weak, by a phase at
                // half the voice's pitch (eased, square-ish, so the change
                // falls between pulses rather than on one), the weak one's
                // depth jittered a little each pair of cycles: the rattle.
                m_kgPhase += 0.5f * cycleHz / m_rate;
                if (m_kgPhase >= 1) {
                    m_kgPhase -= 1;
                    m_kgJitter = 0.08f * m_rng.bipolar();
                }
                m_kgJit += (m_kgJitter - m_kgJit) * 0.01f;
                const float sq = fastTanh(2.5f * std::cos(kTwoPi * m_kgPhase)) * (1.0f / 0.98661f); // tanh(2.5)
                const float depth = m_underLvl.step(voiced ? sh.growl : 0.0f);
                const float weak = clampf(sh.weak + m_kgJit, 0.0f, 1.0f);
                // (1 on the strong cycle, `weak` on the other.)
                const float doubled = 1.0f + depth * (1.0f - weak) * 0.5f * (sq - 1.0f);
                const float tone = m_throatTone.step(x).low;
                // The drone: sygyt's pressed (its chest thinned), the others
                // whole; the doubling in it; and kargyraa's copy of the
                // voice moved down half its pitch (the voice times the
                // half-pitch square: each partial split half a harmonic
                // either side), its lowest partials only, under it.
                float low = (tone - sh.press * m_thPress.step(tone).low) * 1.3f * doubled;
                if (depth > 0.001f)
                    low += depth * sh.ring * m_kgSubLp.step(tone * sq).low;
                // The melody.
                float melody = 0;
                if (sh.sub) {
                    // Kargyraa: a vowel's resonance on the subharmonic's
                    // series, lifted over the growl round it.
                    const float res = m_whBp[1].step(m_whBp[0].step(low).band * (1.0f / sh.q)).band * (1.0f / (sh.q * 1.6f));
                    const float ar = std::fabs(res);
                    m_bpEnv += (ar - m_bpEnv) * 0.002f;
                    const float want = clampf(sh.lift * m_thEnv / (m_bpEnv + 1e-5f), 0.0f, 20.0f);
                    m_whGain += (want - m_whGain) * 0.0008f;
                    melody = res * m_whGain * on;
                } else {
                    // The voice's own harmonic where it is now, down to
                    // nothing and up again where it is pinned.
                    const float pre = m_whBp[0].step(x).band * (1.0f / sh.q);
                    m_whPhA += m_kNow * baseFast / m_rate;
                    m_whPhA -= std::floor(m_whPhA);
                    m_whPhase += m_kNow * base / m_rate;
                    m_whPhase -= std::floor(m_whPhase);
                    const float ca = std::cos(kTwoPi * m_whPhA), sa = std::sin(kTwoPi * m_whPhA);
                    m_zr[0] += (pre * ca - m_zr[0]) * zA;
                    m_zi[0] += (-pre * sa - m_zi[0]) * zA;
                    m_zr[1] += (m_zr[0] - m_zr[1]) * zA;
                    m_zi[1] += (m_zi[0] - m_zi[1]) * zA;
                    const float cb = std::cos(kTwoPi * m_whPhase), sb = std::sin(kTwoPi * m_whPhase);
                    // Its level, and its phase over the last third of a second.
                    const float amp = 2.0f * std::sqrt(m_zr[1] * m_zr[1] + m_zi[1] * m_zi[1]);
                    m_bpEnv += (amp - m_bpEnv) * 0.002f;
                    m_zsr += (m_zr[1] - m_zsr) * zS;
                    m_zsi += (m_zi[1] - m_zsi) * zS;
                    const float zs = std::sqrt(m_zsr * m_zsr + m_zsi * m_zsi);
                    if (zs > 1e-7f) {
                        m_ur = m_zsr / zs;
                        m_ui = m_zsi / zs;
                    }
                    // The harmonic's own wavering kept at a quarter of its
                    // swing round that phase: the life of the voice in it,
                    // and the vibrato that is still left after following
                    // the voice (the tracker is a period behind it) a
                    // quarter of a few hertz, not k times it.
                    float dth = 0;
                    if (amp > 1e-7f)
                        dth = 0.25f * std::atan2(m_zi[1] * m_ur - m_zr[1] * m_ui, m_zr[1] * m_ur + m_zi[1] * m_ui);
                    const float pr = m_ur * std::cos(dth) - m_ui * std::sin(dth), pi = m_ur * std::sin(dth) + m_ui * std::cos(dth);
                    const float own = amp * (pr * cb - pi * sb);
                    // Lifted to stand at the style's height over the drone:
                    // up to 40 dB, a harmonic the voice barely has rising to
                    // a whistle.
                    const float want = clampf(sh.lift * m_thEnv / (m_bpEnv + 1e-5f), 0.0f, 100.0f);
                    m_whGain += (want - m_whGain) * 0.0008f;
                    // (The support's level followed slowly, 100 ms: it is
                    // what stays steady while the harmonic swells and dips.)
                    m_whLevel += (m_bpEnv * m_whGain * 0.7071f - m_whLevel) * 0.0002f;
                    const float support = (m_ur * cb - m_ui * sb) * m_whLevel * 1.4142f;
                    melody = ((1.0f - sh.support) * own * m_whGain + sh.support * support) * on;
                }
                // The gap cut round the whistle, as deep as the style has it.
                float gap = low;
                if (sh.gap > 0)
                    for (auto &f : m_whNotch) {
                        const auto o = f.step(gap);
                        gap = o.low + o.high;
                    }
                float l = (low + (gap - low) * on * sh.gap + melody) * sh.level;
                // Kargyraa's peaks, every other cycle's pulse on top of the
                // copy's, stand far over its loudness: rounded off, as the
                // false folds' rasp is, before the ceiling has to.
                if (sh.sub)
                    l = fastTanh(2.0f * l) * 0.5f;
                float r = l;
                m_thCeil.step(l, r);
                leadL[i] += l * mix;
                leadR[i] += r * mix;
            }
            m_overtone = m_whOn.y > 0.1f && base > 0 ? m_kNow : 0;
            m_overtoneMidi = m_overtone > 0 ? midiOfHz(m_kNow * base) : 0;
        } else {
            m_overtone = 0;
            m_droneSlow = m_droneFast = 0;
        }

        // The sitar, played by the voice. Each syllable plucks the main
        // string at the note being sung (the mizrab's stroke, once the
        // pitch is known: at most 60 ms on); while the note is held the
        // string follows the voice's pitch as it moves, so a slide sung is
        // a meend bent on the string. The jawari makes it buzz, bright at
        // the pluck and sweeping down as it dies. Two chikari, drone
        // strings on the key's root high up, are struck on every beat
        // while you sing; thirteen tarafs on the key's notes ring on under
        // all of it for seconds. A finger plucks a second string a
        // harmony from the voice, which bends with it.
        //
        // What the strings follow is split in two. The slow pitch (under
        // 3 Hz: a meend, a slide, a new note) is followed fully; what is
        // faster (a singer's vibrato) only at 30% and never faster than
        // 6 Hz, so a held note on a buzzing string is one string with a
        // slow ornament (a gamak), not a tangle of FM.
        //
        // The whole instrument then goes through one bus: a high-pass at
        // 55 Hz (24 dB an octave: half the lowest taraf, A2), since a
        // bridge that touches on one side of the swing pushes the string
        // off centre and that would be DC and a wandering rumble, and a
        // low-pass at 9.5 kHz (24 dB an
        // octave), since a switching bridge at the sample rate scatters
        // into the top octave and folds back. Every string goes through
        // both: the main one, the chikari, the fingers' and the tarafs.
        if (m_preset == Sitar || m_sitarMix.y > 0.001f || m_sitarTail > 0) {
            const bool on = m_preset == Sitar;
            for (int k = 0; k < Strings::kCount; ++k)
                m_strings.tune(k, float(h.midi(k, 1)));
            m_chik[0].setHz(midiToHz(h.midi(0, 2)));
            m_chik[1].setHz(midiToHz(h.midi(0, 3)));
            if (steady) {
                // (The slow pitch through two poles at 2.2 Hz: a slide is
                // followed within a sixth of a second, a vibrato's 5 or
                // 6 Hz kept to an eighth of itself.)
                const float aSlow = 1.0f - std::exp(-kTwoPi * 2.2f * float(n) / m_rate);
                const float aVib = 1.0f - std::exp(-kTwoPi * 6.0f * float(n) / m_rate);
                if (m_sitSlow <= 0) {
                    m_sitSlow = m_sitSlow1 = sungMidi;
                    m_sitVib = 0;
                }
                m_sitSlow1 += (sungMidi - m_sitSlow1) * aSlow;
                m_sitSlow += (m_sitSlow1 - m_sitSlow) * aSlow;
                m_sitVib += (sungMidi - m_sitSlow - m_sitVib) * aVib;
            }
            const float played0 = m_sitSlow + 0.3f * m_sitVib;
            auto stringMidi = [&]() {
                // Folded into the sitar's reach, by whole octaves of the
                // slow pitch only (a vibrato never crosses the fold).
                return played0 + (foldMidi(m_sitSlow, 50, 79) - m_sitSlow);
            };
            if (steady && m_sitSlow > 0) {
                m_sitarMidi = stringMidi();
                m_sitar.setHz(midiToHz(m_sitarMidi));
            }
            const double snapped = m_sitSlow > 0 ? snapMidi(h, m_sitSlow, m_follow) : 0;
            for (int k = 0; k < kSitFing; ++k) {
                const Voice &v = m_voices[k];
                if (v.on && steady) {
                    const double m = noteMidi(h, v.value, v.mode, m_sitSlow, m_follow) + (played0 - snapped);
                    m_sitFing[k].setHz(midiToHz(foldMidi(m, 50, 84)));
                }
                // Plucked once its note is known (a finger may land before
                // the voice does), so it starts on it rather than sliding.
                if (on && v.on && !m_sitFingOn[k] && steady) {
                    m_sitFing[k].jump();
                    m_sitFing[k].pluck(0.45f * clampf(v.gain, 0.3f, 1.0f));
                    m_sitFingOn[k] = true;
                }
                if (!v.on)
                    m_sitFingOn[k] = false;
            }
            for (int i = 0; i < n; ++i) {
                const float mix = m_sitarMix.step(on ? 1.0f : 0.0f);
                // (Only once the style has fully come in: passing through
                // it on the way to another plucks nothing.)
                if (m_sitOn.step(dry[i], m_rate) && on && mix > 0.9f) {
                    m_sitPending = int(m_rate * 0.06f);
                    m_sitVel = 0;
                }
                if (m_sitPending > 0) {
                    m_sitVel = std::max(m_sitVel, m_sitOn.fast);
                    if (steady || --m_sitPending == 0) {
                        m_sitPending = 0;
                        // A new pluck is on the note sung now: the slow
                        // pitch starts again from it.
                        if (steady) {
                            m_sitSlow = m_sitSlow1 = sungMidi;
                            m_sitVib = 0;
                            m_sitarMidi = stringMidi();
                            m_sitar.setHz(midiToHz(m_sitarMidi));
                        }
                        m_sitar.jump();
                        // Syllable by syllable, da and ra in turn: the
                        // inward stroke bright and full, the outward one
                        // softer and rounder.
                        const bool da = !m_sitRa;
                        m_sitRa = !m_sitRa;
                        m_sitar.pluck((da ? 0.7f : 0.58f) * clampf(m_sitVel / 0.22f, 0.35f, 1.0f), da ? 1.0f : 0.0f);
                    }
                }
                // The chikari, struck as a sitarist's right hand strikes
                // them between the melody's strokes: da ra dir da, over two
                // beats on the sixteenths (da on the beat, ra on its half,
                // dir the quick da-ra on the next beat, da on its half). A
                // da sweeps in across the two strings, high string first; a
                // ra sweeps back out, the other first, and lighter.
                const double s16 = std::floor(tr.beatAt(i) * 4.0);
                if (s16 != m_sitBeat) {
                    const bool next = s16 == m_sitBeat + 1;
                    m_sitBeat = s16;
                    // (Per sixteenth of the two beats: 0 none, 1 da, 2 ra.)
                    static const int strokes[8] = {1, 0, 2, 0, 1, 2, 1, 0};
                    const int st = strokes[int(((int64_t(s16) % 8) + 8) % 8)];
                    if (on && singing && next && mix > 0.9f && st) {
                        const bool da = st == 1;
                        const int first = da ? 0 : 1;
                        // (The beat's own da the strongest.)
                        const float accent = int64_t(s16) % 4 == 0 ? 1.0f : 0.8f;
                        m_chik[first].pluck((da ? 0.3f : 0.2f) * accent, da ? 1.0f : 0.0f);
                        m_chikSecond = 1 - first;
                        m_chikVel = (da ? 0.22f : 0.15f) * accent;
                        m_chikBright = da ? 1.0f : 0.0f;
                        m_chikLate = int(m_rate * 0.007f);
                    }
                }
                if (m_chikLate > 0 && --m_chikLate == 0)
                    m_chik[m_chikSecond].pluck(m_chikVel, m_chikBright);
                const float s = m_sitar.step(m_rng);
                m_sitarLvl = std::max(m_sitarLvl, std::fabs(s));
                float played = s + 0.6f * m_sitarBody.step(s).band;
                for (int k = 0; k < kSitFing; ++k)
                    played += m_sitFing[k].step(m_rng);
                const float c0 = m_chik[0].step(m_rng), c1 = m_chik[1].step(m_rng);
                float tl, trr;
                m_strings.step((played + c0 + c1) * 0.05f, tl, trr);
                // A plucked string is all attack and decay where a voice
                // holds: brought up to sit as loud as the voice it replaces,
                // the mizrab's peaks rounded off so they don't stick out.
                const float g = kSitarGain;
                const float pl = 0.75f * fastTanh(played * g * 1.35f), cl = (0.6f * c0 + 0.35f * c1) * 1.2f, cr = (0.35f * c0 + 0.6f * c1) * 1.2f;
                const float w = (0.3f + 0.7f * mix) * kSitarLevel;
                float l = (pl + cl + tl * 0.22f) * w, r = (pl + cr + trr * 0.22f) * w;
                // The bus: nothing folded at the top, no DC or rumble, and
                // then the mizrab's peaks held under the ceiling (after the
                // high-pass, which overshoots on a lopsided pluck), and the
                // rumble its gain makes on a lopsided wave taken off again.
                for (int st = 0; st < 2; ++st) {
                    l = m_sitHp[0][st].step(m_sitLp[0][st].step(l).low).high;
                    r = m_sitHp[1][st].step(m_sitLp[1][st].step(r).low).high;
                }
                m_sitCeil.step(l, r);
                l = m_sitHp2[0].step(l).high;
                r = m_sitHp2[1].step(r).high;
                leadL[i] += l;
                leadR[i] += r;
            }
            m_sitarTail = on ? int(m_rate * 8) : std::max(0, m_sitarTail - n);
        }

        // The gamelan: a syllable's attack strikes a bar at the note being
        // sung, in the gamelan's tuning (pelog or slendro on the key's
        // root, or the key itself), once its pitch is known (at most 60 ms
        // on; a consonant with none strikes the last note again), and a
        // new note sung legato strikes too, once it has held for 50 ms.
        // The gongs mark the cycle from the transport while you sing: the
        // gong ageng on the first beat of every fourth bar, the kenong on
        // every other bar line, the kempul on the half bar. A finger plays
        // kotekan: two parts on the sixteenths, polos left and sangsih
        // right, interlocking round the last note sung (the lane sets how
        // far above it), each part damping its last note as it strikes the
        // next.
        if (m_preset == Gamelan || m_gamMix.y > 0.001f || m_bronzeTail > 0) {
            const bool on = m_preset == Gamelan;
            const float note = voiced ? float(gamMidi(h, m_gamTuning, gamDegreeNear(h, m_gamTuning, sungMidi))) : -1.0f;
            // The lane of the finger that came down last: the figure.
            int lane = -100;
            uint32_t latest = 0;
            for (const auto &v : m_voices)
                if (v.on && (lane == -100 || v.order > latest)) {
                    lane = v.value;
                    latest = v.order;
                }
            // The balungan: the note sung now, or the last one struck.
            const double balungan = note > 0 ? note : m_gamNote;
            for (int i = 0; i < n; ++i) {
                const float mix = m_gamMix.step(on ? 1.0f : 0.0f);
                if (m_gamOn.step(dry[i], m_rate) && on && mix > 0.9f && m_gamPending <= 0) {
                    m_gamPending = int(m_rate * 0.06f);
                    m_gamVel = 0;
                }
                if (m_gamPending > 0) {
                    m_gamVel = std::max(m_gamVel, m_gamOn.fast);
                    const float vel = clampf(m_gamVel / 0.22f, 0.3f, 1.0f);
                    if (steady && note > 0) {
                        m_gamPending = 0;
                        strikeBronze(note, vel);
                    } else if (--m_gamPending == 0 && m_gamNote > 0) {
                        strikeBronze(m_gamNote, vel * 0.8f);
                    }
                }
                const double beat = tr.beatAt(i);
                const double b = std::floor(beat);
                if (b != m_gamBeat) {
                    // A beat crossed, not the first look at the clock (or
                    // a stale one, back in the style after a while).
                    const bool next = b == m_gamBeat + 1;
                    m_gamBeat = b;
                    const int64_t bi = int64_t(b);
                    const int inBar = int(((bi % 4) + 4) % 4), bar = int(((bi / 4) % 4 + 4) % 4);
                    if (on && singing && next && mix > 0.9f) {
                        if (inBar == 0)
                            strikeGong(h, bar == 0 ? 0 : 2, bar == 0 ? 0.9f : 0.6f, balungan);
                        else if (inBar == 2)
                            strikeGong(h, 1, 0.55f, balungan);
                    }
                }
                const double s16 = std::floor(beat * 4.0);
                if (s16 != m_kotStep) {
                    const bool next = s16 == m_kotStep + 1;
                    m_kotStep = s16;
                    if (on && next && mix > 0.9f && lane > -100)
                        kotekan(h, int64_t(s16), lane, balungan);
                }
                float l, r, pl, pr, sl, sr, gl, gr;
                m_bronze.step(m_rng, l, r);
                m_polos.step(m_rng, pl, pr);
                m_sangsih.step(m_rng, sl, sr);
                m_gongs.step(m_rng, gl, gr);
                // Everything rings out after the style is left, as bronze
                // would; only new strikes stop. While the style plays, the
                // bronze and the voice under it go through its own ceiling
                // together (a strike's first milliseconds stand far over its
                // ring, and land on the voice's peaks): crossfaded with the
                // plain sum by the style's mix, so coming in or going, the
                // ceiling's look-ahead never jumps the voice in time.
                const float bl = (l * 0.2f + (pl * 0.9f + sl * 0.3f) * 0.12f + gl * 0.07f) * kGamelanGain;
                const float br = (r * 0.2f + (pr * 0.3f + sr * 0.9f) * 0.12f + gr * 0.07f) * kGamelanGain;
                float cl = leadL[i] + bl, cr = leadR[i] + br;
                const float plainL = cl, plainR = cr;
                m_gamCeil.step(cl, cr);
                leadL[i] = plainL + (cl - plainL) * mix;
                leadR[i] = plainR + (cr - plainR) * mix;
            }
            if (on && steady && m_gamMix.y > 0.9f && note > 0 && m_gamPending <= 0 && std::fabs(note - m_gamNote) > 0.5f) {
                m_gamStable += n;
                if (m_gamStable > int(m_rate * 0.05f) && m_gamOn.fast > 0.02f) {
                    strikeBronze(note, clampf(m_gamOn.fast / 0.22f, 0.3f, 0.85f));
                    m_gamStable = 0;
                }
            } else {
                m_gamStable = 0;
            }
            if (on && note > 0)
                m_leadMidi = note;
            m_bronzeTail = std::max(0, m_bronzeTail - n);
            m_gamStruckAge += n;
            const double cyc = std::fmod(tr.beatAt(0), 16.0);
            m_gamCycle = float(cyc < 0 ? cyc + 16.0 : cyc);
        }

        // Screwed: real slowed tape. The voice is written onto a tape and
        // played back off it slower (a fourth down by default, or a fifth,
        // or the classic 0.8: B), pitch, throat and time all slowed
        // together, as a record at the wrong speed is, through a worn
        // cassette's wow, flutter, dull top and hiss, with a second copy a
        // few milliseconds behind doubling it. The playhead drags further
        // behind the live voice as you sing, and that is the style:
        // relentless slowness, over a band that keeps its tempo.
        //
        // It catches up only when it must, and then on the grid. After a
        // rest, when you start again, the head drops onto your first word
        // so that it lands on the next beat (at once, if you came in on the
        // beat); two bars behind, it drops at the next bar line, onto what
        // you sing from there, with a short tape stop of the old head. So
        // every phrase re-enters on the beat, not wherever the rest ended.
        //
        // The fingers are Screw's hands on two copies of one record, the
        // second a beat behind the first. Held, a lane cuts the crossfader
        // to the second deck: the word just played comes again off it (a
        // beat back from the middle lanes, half a beat from the two at the
        // right), with that deck's own drift and darker tone; let go, it
        // cuts back to the first, which has played on. Quick cuts are the
        // chop. The bottom lane is the backspin: held, the tape is spun
        // back through what it just played; let go, it drops onto you again.
        if (m_preset == Screwed || m_screwMix.y > 0.001f) {
            const bool on = m_preset == Screwed;
            const float speed = screwSpeed(m_screwSpeed);
            const int64_t inMask = int64_t(m_scIn.size()) - 1, outMask = int64_t(m_scOut.size()) - 1;
            if (m_screwMix.y < 0.001f) {
                m_scPos = double(m_scW) - 2;
                m_spinLvl = 0;
                m_scFade = 1;
                m_dropAt = -1;
            }
            const double spb = tr.samplesPerBeat();
            // The fingers: the deck B lanes and the backspin.
            bool spinHeld = false;
            float deckBeats = 0;
            uint32_t latest = 0;
            for (int k = 0; k < kMaxVoices; ++k) {
                const Voice &v = m_voices[k];
                const bool spinLane = v.value <= -7;
                if (on && v.on && !m_scFingOn[k] && spinLane) {
                    m_spinPos = m_scPos;
                    m_spinRate = m_scSpeed;
                    m_spinLvl = 1;
                    m_spinBack = true;
                }
                m_scFingOn[k] = v.on;
                spinHeld |= on && v.on && spinLane;
                // (The latest finger down says how far behind deck B is.)
                if (on && v.on && !spinLane && (deckBeats == 0 || v.order > latest)) {
                    deckBeats = v.value >= 5 ? 0.5f : 1.0f;
                    latest = v.order;
                }
            }
            if (deckBeats > 0)
                m_deckBeats = deckBeats;
            // Let go of the backspin: the tape drops onto the live voice.
            if (m_spinHeld && !spinHeld) {
                m_scPos = double(m_scW) - 2;
                m_scFade = 0;
                m_spinBack = false;
            }
            m_spinHeld = spinHeld;
            m_repOn = deckBeats > 0;
            // The most the tape may fall behind before it drops at the bar:
            // two bars (and the bar it waits for must still be on the reel).
            const double limit = std::min(8.0 * spb, double(m_scIn.size()) - 4.0 * spb - 0.2 * m_rate);
            m_scLimit = float(limit / m_rate);
            auto readIn = [&](double pos) {
                const double f = std::floor(pos);
                const int64_t k = int64_t(f);
                const float u = float(pos - f);
                const float a = m_scIn[size_t(k & inMask)], b = m_scIn[size_t((k + 1) & inMask)];
                return a + (b - a) * u;
            };
            auto readOut = [&](double pos) {
                const double f = std::floor(pos);
                const int64_t k = int64_t(f);
                const float u = float(pos - f);
                const float a = m_scOut[size_t(k & outMask)], b = m_scOut[size_t((k + 1) & outMask)];
                return a + (b - a) * u;
            };
            for (int i = 0; i < n; ++i) {
                const float mix = m_screwMix.step(on ? 1.0f : 0.0f);
                m_scIn[size_t(m_scW & inMask)] = dry[i];
                if ((m_scW & 255) == 0)
                    m_scPitch[size_t((m_scW >> 8) & 1023)] = voicedNow ? sungMidi : -1.0f;
                ++m_scW;
                const float a = std::fabs(dry[i]);
                m_scLive += (a - m_scLive) * (a > m_scLive ? 0.01f : 0.0005f);
                const double lag = double(m_scW) - m_scPos;
                const double beat = tr.beatAt(i);
                // A drop to arrange: after a rest, onto the first word, on
                // the next beat; two bars behind, at the next bar line.
                if (!m_spinHeld && m_dropAt < 0) {
                    if (lag > limit) {
                        m_dropAt = std::floor(beat / 4.0 + 1e-9) * 4.0 + 4.0;
                        m_dropTo = -1; // the live voice, as it is at the bar
                        m_dropStop = true;
                    } else if (m_scHeadEnv < 0.004f && m_scLive > 0.03f && lag > 0.04 * m_rate) {
                        const double frac = beat - std::floor(beat);
                        // (The word began a few milliseconds before the
                        // level showed it.)
                        m_dropTo = double(m_scW) - 0.012 * m_rate;
                        m_dropAt = frac < 0.2 ? beat : std::floor(beat) + 1.0;
                        m_dropStop = false;
                    }
                }
                if (m_dropAt >= 0 && beat >= m_dropAt - 1e-9) {
                    if (m_dropStop && m_scHeadEnv > 0.002f) {
                        m_spinPos = m_scPos;
                        m_spinRate = m_scSpeed;
                        m_spinLvl = 1;
                        m_spinBack = false;
                    }
                    m_scPos = m_dropTo >= 0 ? m_dropTo : double(m_scW) - 2;
                    m_scFade = 0;
                    m_scHeadEnv = m_scLive;
                    m_dropAt = -1;
                }
                m_dropIn = m_dropAt >= 0 ? float((m_dropAt - beat) * spb / m_rate) : -1.0f;
                m_wowPhase += 0.45f / m_rate;
                m_wowPhase -= std::floor(m_wowPhase);
                m_flutPhase += 6.5f / m_rate;
                m_flutPhase -= std::floor(m_flutPhase);
                m_scSpeed = speed * (1.0f + 0.0045f * std::sin(kTwoPi * m_wowPhase) +
                                     0.0012f * std::sin(kTwoPi * m_flutPhase));
                // (Under a held backspin the head is silent and stands
                // still: there is only the spin.)
                m_scFade = m_spinHeld ? 0.0f : std::min(1.0f, m_scFade + 1.0f / (0.012f * m_rate));
                const float main = readIn(m_scPos) * m_scFade;
                // The doubling: the same tape, 17 ms behind and drifting
                // against it, quiet: two copies of one record.
                const float twin = readIn(m_scPos - (0.017 + 0.003 * std::sin(kTwoPi * m_wowPhase * 1.3f)) * m_rate) *
                                   m_scFade;
                if (!m_spinHeld)
                    m_scPos += m_scSpeed;
                float spun = 0;
                if (m_spinLvl > 0) {
                    spun = readIn(m_spinPos) * m_spinLvl * m_spinLvl;
                    m_spinPos += m_spinRate;
                    if (m_spinBack) {
                        // A hand on the record: dragged back to three times
                        // its speed within 80 ms, fading over a second.
                        m_spinRate = std::max(-3.0f, m_spinRate - 3.75f / (0.08f * m_rate));
                        m_spinLvl -= 1.0f / (1.0f * m_rate);
                    } else {
                        // The tape stop: the old head winds down to nothing
                        // over a fifth of a second.
                        m_spinRate = std::max(0.0f, m_spinRate - speed / (0.2f * m_rate));
                        m_spinLvl = m_spinRate > 0 ? std::min(1.0f, m_spinRate / speed * 1.5f) : 0.0f;
                    }
                    // (Never back past what the reel still holds.)
                    if (double(m_scW) - m_spinPos > double(m_scIn.size()) - 4096)
                        m_spinLvl = 0;
                }
                const float am = std::fabs(main);
                m_scHeadEnv += (am - m_scHeadEnv) * (am > m_scHeadEnv ? 0.01f : 0.0005f);
                // Worn tape: the top gone dull, the loud parts rounded.
                const float tone = fastTanh(m_scLp.step(main + spun).low * 1.3f) * (1.0f / 1.3f);
                const float twinTone = m_scLp2.step(twin).low;
                // Deck A, as it plays, is what deck B will play a beat on.
                m_scOut[size_t(m_scOutW & outMask)] = tone + twinTone * 0.26f;
                ++m_scOutW;
                // Deck B: the same record a beat (or half) behind, on a
                // turntable of its own (its own slow drift, a few cents
                // either way) through a darker channel. The crossfader cuts
                // to it in 3 ms, a DJ's flick, not a fade.
                m_deckBWow += 0.31f / m_rate;
                m_deckBWow -= std::floor(m_deckBWow);
                const double behind = std::min(double(m_scOut.size()) - 8.0, double(m_deckBeats) * spb) *
                                      (1.0 + 0.004 * std::sin(kTwoPi * m_deckBWow));
                const float deckB = m_deckBTone.step(readOut(double(m_scOutW - 1) - behind)).low * 1.15f;
                const float x = m_repLvl.step(m_repOn ? 1.0f : 0.0f);
                const float hiss = m_scHiss.step(m_rng.bipolar()).band * 0.006f;
                // (Deck A and its double a little apart, deck B a little to
                // the other side: two turntables.)
                float l = (tone + twinTone * 0.22f) * (1 - x) + deckB * x * 0.92f + hiss;
                float r = (tone + twinTone * 0.3f) * (1 - x) + deckB * x * 1.0f + hiss;
                l *= kScrewLevel;
                r *= kScrewLevel;
                m_scCeil.step(l, r);
                leadL[i] += l * mix;
                leadR[i] += r * mix;
            }
            m_scLag = float((double(m_scW) - m_scPos) / m_rate);
            if (on) {
                const float p = m_scPitch[size_t((int64_t(m_scPos) >> 8) & 1023)];
                m_leadMidi = p > 0 ? p + 12.0f * std::log2(speed) : -1.0f;
            }
        }

        // Riot: the voice screamed, a few metres off (scream.h). Your
        // mouth is kept, your throat replaced: the note you sing moved by
        // octaves into a screamer's register for the phrase, pressed folds,
        // the false folds drawn in by the effort and fluttering at half the
        // pitch, and the noise of the jet on every pulse; heard across a
        // field (or a room: B). A finger is another screamer, at its
        // harmony, through the same mouth: a gang shout.
        if (m_preset == Riot || m_riotMix.y > 0.001f) {
            const bool on = m_preset == Riot;
            if (m_riotMix.y < 0.001f)
                m_scream.reset();
            // (The tracker's own reading, not the octave-guarded note the
            // instruments play by: the scream keeps its own guard, and the
            // instruments' one takes a slip downwards at once.)
            m_scream.setPitch(voiced ? hz : 0.0f, voicedNow, m_pitch.clarity());
            float gangHz[scream::kMaxGang], gangLvl[scream::kMaxGang];
            int gn = 0;
            const double ref = sungMidi > 0 ? sungMidi : 57;
            for (const auto &v : m_voices)
                if (v.on && gn < scream::kMaxGang) {
                    gangHz[gn] = midiToHz(noteMidi(h, v.value, v.mode, ref, m_follow) + v.detune * 0.3f);
                    gangLvl[gn] = clampf(v.gain, 0.0f, 1.2f) * 0.8f;
                    ++gn;
                }
            m_scream.setGang(gangHz, gangLvl, gn);
            const float m0 = m_riotMix.y;
            for (int i = 0; i < n; ++i)
                m_riotMix.step(on ? 1.0f : 0.0f);
            m_scream.process(dry, leadL, leadR, n, m0, m_riotMix.y);
            if (on && m_scream.hz() > 0 && voiced)
                m_leadMidi = midiOfHz(m_scream.hz());
        }
    }

    // The kotekan: two parts, polos (left) and sangsih (right), that
    // between them make one quick line on the sixteenths, each part only
    // half of it. The figure is the lane's (kotFigure), and it is written
    // round the balungan: the note sung now. Every figure lands on that
    // tone (norot and telu on every beat or cycle, nyog cag every second
    // beat, approached from the tone under it), so when you move to a new
    // note the next sixteenths already lead to it and the figure resolves
    // onto it, as a kotekan resolves onto the balungan's beat. The second
    // lane of each pair plays the figure an octave higher.
    void kotekan(const Harmony &h, int64_t step, int lane, double balungan) {
        static const Bronze::Spec gangsa = {
            {1.0f, 2.756f, 5.404f, 8.933f}, {1.0f, 0.3f, 0.12f, 0.05f}, {1.6f, 0.8f, 0.4f, 0.2f},
            6.0f, 0.35f, 2.0f, 0.06f, 0.25f, 0};
        constexpr int kNone = -100;
        const int s = int(((step % 16) + 16) % 16);
        const double base = balungan > 0 ? balungan : h.tuning.baseMidi() + 12;
        const int g = gamDegreeNear(h, m_gamTuning, base);
        int polos = kNone, sangsih = kNone, chord = kNone;
        switch (kotFigure(lane)) {
        case Norot:
            // The tone and the one over it, in turn: polos on the tone,
            // sangsih over it, both on the tone on the beat.
            if (s % 4 == 0)
                polos = sangsih = 0;
            else if (s % 2 == 1)
                sangsih = 1;
            else
                polos = 0;
            break;
        case NyogCag: {
            // One line, every other note each ("the jumping chicken"):
            // up from the tone, round, and back up into it from under.
            static const int line[8] = {0, 2, 1, 2, 3, 2, 1, -1};
            (s % 2 == 0 ? polos : sangsih) = line[s % 8];
            break;
        }
        case Telu: {
            // Three tones, the line in threes across the beat: polos on the
            // lower two, sangsih the upper two (both play the middle).
            static const int line[16] = {0, 2, 1, 0, 2, 1, 0, 2, 1, 0, 1, 2, 0, 1, 2, 1};
            const int t = line[s];
            if (t <= 1)
                polos = t;
            if (t >= 1)
                sangsih = t;
            break;
        }
        case Empat: {
            // Four tones: polos the lower two, sangsih the upper two, and
            // on the tone itself sangsih strikes the fourth tone with it,
            // the two together (ngempat).
            static const int line[16] = {0, 2, 1, 3, 0, 2, 1, 3, 0, 3, 1, 2, 0, 3, 2, 1};
            const int t = line[s];
            if (t <= 1)
                polos = t;
            else
                sangsih = t;
            if (t == 0)
                chord = 3;
            break;
        }
        }
        // The register: the tone two octaves over the voice, in the
        // gangsa's range (an octave higher on a pair's second lane).
        const double top = foldMidi(gamMidi(h, m_gamTuning, g) + 24, 72, 84) + (kotHigh(lane) ? 12 : 0);
        const double shift = top - gamMidi(h, m_gamTuning, g);
        auto midiOf = [&](int t) { return float(gamMidi(h, m_gamTuning, g + t) + shift); };
        const float vel = s % 4 == 0 ? 0.7f : 0.5f;
        if (polos != kNone)
            m_polos.strike(midiOf(polos), vel, gangsa);
        if (sangsih != kNone)
            m_sangsih.strike(midiOf(sangsih), vel, gangsa);
        if (chord != kNone) {
            // (Kept ringing with the note just struck, not damped by it.)
            Bronze::Spec both = gangsa;
            both.keep = 2;
            m_sangsih.strike(midiOf(chord), vel * 0.8f, both);
        }
        m_kotTone = g;
        m_bronzeTail = std::max(m_bronzeTail, int(m_rate * 3));
    }

    // --- freeze -----------------------------------------------------------------
    void startFreeze(AudioContext &) {
        // Three quarters of a second, held: up to the last moment the voice
        // sounded (a finger that comes a breath late still freezes the
        // note, not the silence after it), or simply the latest.
        const size_t len = m_freezeBuf.size();
        const int64_t since = m_recentW - m_lastVoiced;
        const int64_t end = since >= 0 && since < int64_t(m_rate * 1.5f) ? m_lastVoiced : m_recentW;
        for (size_t i = 0; i < len; ++i)
            m_freezeBuf[i] = m_recent[size_t(end - int64_t(len) + int64_t(i)) & (m_recent.size() - 1)];
        m_stutterK = -1;
        for (auto &g : m_cloud)
            g.left = 0;
    }
    float freezeCloud(float pitch) {
        // Overlapping 90 ms grains from anywhere in the last half second
        // held: the sound stands still, breathing.
        const int glen = int(0.09f * m_rate);
        if (--m_cloudNext <= 0) {
            m_cloudNext = int(0.018f * m_rate);
            for (auto &g : m_cloud)
                if (g.left <= 0) {
                    const int span = int(m_freezeBuf.size()) - glen * 2 - 8;
                    g.pos = double(glen) + double(m_rng.unit()) * std::max(1, span) * 0.7 + span * 0.3;
                    g.left = glen;
                    g.len = glen;
                    break;
                }
        }
        float out = 0;
        for (auto &g : m_cloud) {
            if (g.left <= 0)
                continue;
            const float u = 1.0f - float(g.left) / float(g.len);
            const double f = std::floor(g.pos);
            const size_t k = std::min(size_t(f), m_freezeBuf.size() - 2);
            const float a = m_freezeBuf[k], b = m_freezeBuf[k + 1];
            out += (a + (b - a) * float(g.pos - f)) * hann().at(u);
            g.pos += pitch;
            if (g.pos >= double(m_freezeBuf.size() - 2))
                g.pos -= double(glen);
            --g.left;
        }
        return out * 0.4f;
    }

    // --- loops ------------------------------------------------------------------
    struct Loop {
        int id = -1;
        int handle = 0;
        double pos = 0;   // frames into the take's audio
        int length = 0;
        int startAt = 0;  // offset in the block where it begins
        float gain = 0;
        bool fading = false;
    };
    void startLoop(const Command &c, AudioContext &ctx) {
        const int handle = c.i[0];
        if (handle <= 0 || handle >= kMaxTakes)
            return;
        const Tape::Take &take = m_tape->takes[handle];
        const int len = take.length;
        if (len <= 0)
            return;
        // Where in the audio it starts: the clip's start falls i[3] frames
        // in; a loop joined late (its t in the past) that much further.
        int offset = 0;
        double late = 0;
        if (c.t > 0 && c.t < ctx.now())
            late = (ctx.now() - c.t) * double(m_rate);
        else if (c.t > 0)
            offset = ctx.offsetOf(c.t, kBlock);
        const double pos = std::fmod(double(c.i[3]) + late, double(len));
        // The same sound coming round again where it already is: it plays
        // straight on (each pass retriggers it, sample for sample).
        for (auto &l : m_loops)
            if (l.id == c.id && l.handle == handle) {
                const double expect = std::fmod(l.pos + double(offset), double(len));
                const double diff = std::fabs(expect - pos);
                if (std::min(diff, double(len) - diff) < 64) {
                    l.fading = false;
                    return;
                }
                l.fading = true;
                l.id = -2; // the old pass fades; the new one starts
            }
        Loop *slot = nullptr;
        for (auto &l : m_loops)
            if (l.id == -1) {
                slot = &l;
                break;
            }
        if (!slot)
            return;
        slot->id = c.id;
        slot->handle = handle;
        slot->pos = pos;
        slot->length = len;
        slot->startAt = offset;
        slot->gain = 0;
        slot->fading = false;
    }
    void renderLoops(float *outL, float *outR, int n, AudioContext &) {
        const Tape &tape = *m_tape;
        const int64_t written = tape.written.load(std::memory_order_relaxed);
        const float fadeIn = 1.0f / (0.004f * m_rate), fadeOut = 1.0f / (0.008f * m_rate);
        for (auto &l : m_loops) {
            if (l.id == -1)
                continue;
            const Tape::Take &take = tape.takes[l.handle];
            const float *data = take.data.load(std::memory_order_acquire);
            for (int i = l.startAt; i < n; ++i) {
                if (l.fading) {
                    l.gain -= fadeOut;
                    if (l.gain <= 0) {
                        l.id = -1;
                        break;
                    }
                } else if (l.gain < 1) {
                    l.gain = std::min(1.0f, l.gain + fadeIn);
                }
                const int k = int(l.pos);
                float a = 0, b = 0;
                if (data) {
                    a = data[2 * k];
                    b = data[2 * k + 1];
                } else if (take.rate < 1.0f) {
                    // A slowed take not made yet: the singing, slowed, from
                    // the ring (without the tape's colour, for the moment
                    // until the page has made it).
                    const double src = double(k) * take.rate;
                    const int64_t s = take.start + int64_t(src);
                    if (src < take.source && s + 1 < written && written - s < kRing) {
                        const float u = float(src - std::floor(src));
                        const float x0 = tape.dry[size_t(s & kRingMask)], x1 = tape.dry[size_t((s + 1) & kRingMask)];
                        a = b = x0 + (x1 - x0) * u;
                    }
                } else {
                    // Not copied out yet: straight from the ring, which
                    // still holds it.
                    const int64_t s = take.start + k;
                    if (s < written && written - s < kRing) {
                        a = tape.wetL[size_t(s & kRingMask)];
                        b = tape.wetR[size_t(s & kRingMask)];
                    }
                }
                outL[i] += a * l.gain;
                outR[i] += b * l.gain;
                l.pos += 1;
                if (l.pos >= l.length)
                    l.pos -= l.length;
            }
            l.startAt = 0;
        }
    }

    std::shared_ptr<Tape> m_tape;
    float m_rate = 48000;

    // The input.
    Svf m_hp;
    DcBlock m_outDcL, m_outDcR; // and the output's
    float m_micGain = 1, m_gateEnv = 0, m_noise = 0.001f, m_gate = 0, m_compEnv = 0;
    // The lift's ear (the voice's level while the gate is open) and gain.
    static constexpr float kLiftTarget = 0.1f, kLiftMax = 31.6f; // −20 dBFS, +30 dB
    float m_liftEnv = kLiftTarget, m_lift = 1;
    double m_envAcc = 0;
    float m_inPeak = 0, m_inPeakHeld = 0, m_inSq = 0, m_livePeak = 0;
    int m_inCount = 0;
    bool m_hasInput = false;
    static constexpr int kScopeLen = 2048;
    float m_scope[kScopeLen] = {};
    uint32_t m_scopeW = 0;
    PitchTracker m_pitch;
    Source m_src;
    int m_howl = 0, m_feedback = 0;
    float m_howlHz = 0;
    Svf m_howlHp;
    float m_howlSign = 0, m_howlLevel = 0, m_howlFreq = 0;
    int m_howlCross = 0, m_howlN = 0, m_howlHi = 0;
    int64_t m_lastVoiced = -1000000;
    float m_heldHz = 0, m_heldPeriod = 0, m_octRef = -1;
    int m_octAway = 0;
    int m_unvoiced = 1 << 30;
    float m_vocIn = 0, m_vocOut = 0, m_vocGain = 0.5f;

    // The five styles from further afield.
    TalkBox m_talk;
    Smooth m_talkMix, m_talkGlide, m_talkSubLvl;
    Svf m_talkSib, m_tubeHp[2], m_tubePeak[2], m_talkAir[2], m_talkTop[2];
    float m_talkPh[2] = {}, m_talkNote = 57, m_talkHeld = 0, m_vibPhase = 0, m_pwmPhase = 0;
    float m_talkIn = 0, m_talkOut = 0, m_talkGain = 1;
    float m_talkNow = 57, m_talkLevel = 0;
    Ceiling m_talkCeil;
    Smooth m_talkGate, m_talkPress;
    // KEYS: the synth's filter, its contour, the pressure last held.
    Svf m_talkFilt[2];
    float m_talkEnv = 0, m_talkPressHeld = 0.5f, m_talkOpen = 1;
    uint32_t m_talkTick = 0;
    int m_talkQuiet = 0;
    bool m_talkSub = true, m_talkRest = true, m_talkKeys = false, m_talkKeyHeld = false;
    int m_sungAgo = 1 << 30;
    Smooth m_throatMix, m_whOn, m_underLvl;
    // Riot: the scream and how far in it is.
    scream::Scream m_scream;
    Smooth m_riotMix;
    int m_whHold = 0, m_throatStyle = Sygyt;
    float m_droneSlow = 0, m_droneFast = 0, m_whLevel = 0, m_kgPhase = 0, m_kgJitter = 0, m_kgJit = 0;
    float m_whPhA = 0, m_zr[2] = {}, m_zi[2] = {}, m_zsr = 0, m_zsi = 0, m_ur = 1, m_ui = 0;
    Svf m_whBp[2], m_whNotch[4], m_throatTone, m_thPress, m_kgSubLp;
    Ceiling m_thCeil;
    float m_stEnv = 0, m_stPeak = 0;
    float m_whPhase = 0, m_thEnv = 0, m_droneHz = 0, m_bpEnv = 0, m_whGain = 0;
    float m_kNow = 0, m_kSpeed = 0, m_overtone = 0, m_overtoneMidi = 0;
    int m_kTarget = 0;
    uint32_t m_thTick = 0;
    static constexpr int kSitFing = 3;
    // Measured: the sitar as loud as the natural voice sung in its register.
    static constexpr float kSitarGain = 2.2f, kSitarLevel = 3.2f;
    JawariString m_sitar, m_chik[2], m_sitFing[kSitFing];
    bool m_sitFingOn[kSitFing] = {};
    Svf m_sitarBody, m_sitHp[2][2], m_sitLp[2][2], m_sitHp2[2];
    float m_sitSlow = -1, m_sitSlow1 = -1, m_sitVib = 0, m_sitarLvl = 0;
    Ceiling m_sitCeil;
    Strings m_strings;
    Smooth m_sitarMix;
    Onsets m_sitOn;
    float m_sitarMidi = 57, m_sitVel = 0;
    int m_sitarTail = 0, m_sitPending = 0, m_chikLate = 0, m_chikSecond = 1;
    float m_chikVel = 0.22f, m_chikBright = 1;
    bool m_sitRa = false; // the next syllable's stroke is ra
    double m_sitBeat = -1;
    Bronze m_bronze, m_polos, m_sangsih, m_gongs;
    Smooth m_gamMix;
    Onsets m_gamOn;
    Ceiling m_gamCeil;
    int m_gamTuning = Pelog, m_kotTone = 0;
    // Measured, as the sitar's: the bronze as loud as the voice it plays.
    static constexpr float kGamelanGain = 0.5f;
    float m_gamVel = 0, m_gamNote = -1, m_gamSlow = -1, m_gamCycle = 0;
    int m_gamStruckAge = 1 << 30;
    int m_gamPending = 0, m_gamStable = 0, m_bronzeTail = 0;
    double m_gamBeat = -1, m_kotStep = -1;
    Smooth m_screwMix, m_repLvl;
    int m_screwSpeed = ScrewFourth;
    float m_scLag = 0, m_scLimit = 1;
    bool m_spinHeld = false, m_spinBack = false;
    Svf m_scLp, m_scLp2, m_scHiss;
    std::vector<float> m_scIn, m_scOut;
    float m_scPitch[1024] = {};
    int64_t m_scW = 0, m_scOutW = 0;
    double m_scPos = 0, m_spinPos = 0;
    float m_scSpeed = 0.75f, m_spinRate = 0, m_spinLvl = 0, m_scFade = 1, m_scLive = 0, m_scHeadEnv = 0;
    float m_wowPhase = 0, m_flutPhase = 0;
    bool m_repOn = false;
    // Deck B: how far behind (beats), its turntable's drift, its tone.
    float m_deckBeats = 1, m_deckBWow = 0;
    Svf m_deckBTone;
    // A drop of the head arranged for a beat (or a bar line): onto which
    // sample (−1: the live voice then), with a tape stop or not, and how
    // long until it (seconds, −1: none), for the page.
    double m_dropAt = -1, m_dropTo = -1;
    bool m_dropStop = false;
    float m_dropIn = -1;
    Ceiling m_scCeil;
    // Measured: the tape as loud as the natural voice singing the same.
    static constexpr float kScrewLevel = 1.16f;
    bool m_scFingOn[kMaxVoices] = {};

    // The voice.
    int m_preset = Natural, m_formant = 0;
    bool m_monitor = true, m_follow = false;
    Shifter m_lead;
    Smooth m_leadRatio, m_leadShift, m_leadDry, m_vocoderMix, m_monGain;
    float m_leadMidi = -1;
    struct Voice {
        int id = -1;
        bool on = false;
        int value = 0, mode = 0;
        float gain = 0, formant = 0.5f, detune = 0, pan = 0.5f, midi = 0;
        bool fresh = false; // its first block: the ratio starts where it should
        Smooth level, ratio;
        Smooth syn;           // the talk box: its synth note's level,
        Smooth synGlide;      // and its pitch, gliding as the lead's does
        float synPh[2] = {};
        uint32_t order = 0;   // when it came down: the talk box's keys play the latest
        Shifter shifter;
    };
    Voice m_voices[kMaxVoices];
    uint32_t m_voiceOrder = 0;
    int m_activeVoices = 0;
    struct ChoirVoice {
        Smooth level, ratio;
        Shifter shifter;
    };
    ChoirVoice m_choir[3];
    float m_choirMidi[3] = {60, 55, 48};
    float m_choirAt = -100;
    uint32_t m_choirSeq = ~0u;
    static constexpr int kBands = 16;
    struct Band {
        Svf a, s;
        float env = 0;
    };
    Band m_bands[kBands];
    Svf m_sib;
    float m_carPhase[8][2] = {};

    // The strips: where the finger is (x / height), pressure, a third value, on.
    struct Strip {
        float x = 0, p = 0, semis = 0, on = 0;
    };
    Strip m_filt, m_lofi, m_frz, m_echoP, m_revP;
    Svf m_filtL, m_filtR;
    Smooth m_filtOn, m_filtCut, m_filtRes, m_lofiOn;
    Svf m_radioHp, m_radioLp, m_radioHp2, m_radioLp2;
    float m_holdL = 0, m_holdR = 0;
    int m_holdN = 0;
    std::vector<float> m_recent, m_freezeBuf;
    int64_t m_recentW = 0;
    Smooth m_freezeLvl, m_stutterOn;
    struct CloudGrain {
        double pos = 0;
        int left = 0, len = 1;
    };
    CloudGrain m_cloud[8];
    int m_cloudNext = 0;
    Rng m_rng{0x70CA1};
    int m_stutter = 2;
    double m_stutterK = -1;
    float m_stutterPos = 0;
    std::vector<float> m_tapeL, m_tapeR;
    uint32_t m_tapeW = 0;
    bool m_tapeStop = false;
    float m_tapeRate = 1, m_tapeBack = 0;
    double m_tapeRead = 0;
    TapeEcho m_echo;
    tf::Reverb m_reverb;
    Smooth m_echoSend, m_revSend;
    int m_echoTail = 0, m_revTail = 0;

    Loop m_loops[16];
    struct ChopVoice {
        bool on = false;
        int64_t start = 0;
        double pos = 0;
        float length = 0, level = 1, rate = 1, fade = 0;
    };
    ChopVoice m_chops[kChops];
    bool m_demo = false;
    DemoSinger m_singer;
};

// --- the page -------------------------------------------------------------------

class Page : public PartControl {
public:
    explicit Page(std::shared_ptr<Tape> tape) : m_tape(std::move(tape)) {}

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        // The click: tape stop while pressed. A finger that landed just
        // before is the one doing the clicking, not a note.
        if (f.button != m_button) {
            m_button = f.button;
            if (f.button)
                for (Finger &h : m_fingers)
                    if (h.id >= 0 && f.t - h.t0 < 0.25 && h.zone == Keys && !h.clicker) {
                        h.clicker = true;
                        release(h, ctx);
                    }
            sendTape(ctx);
        }
        for (Finger &h : m_fingers) {
            if (h.id < 0 || h.key)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still)
                lift(h, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Finger *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (!h)
                    continue;
                land(*h, t, f.t, ctx);
            } else {
                move(*h, t, f.t, ctx);
            }
        }
    }

    void background(double t, ControlContext &ctx) override {
        const int part = ctx.part();
        if (part < 0)
            return;
        // The handles of loops that were replaced are let go a while after.
        for (size_t i = 0; i < m_graveyard.size();) {
            if (t >= m_graveyard[i].first) {
                freeHandle(m_graveyard[i].second);
                m_graveyard.erase(m_graveyard.begin() + long(i));
            } else {
                ++i;
            }
        }
        copyPending();
        // What was sung, for the trail of notes the page draws.
        const PartView &v = ctx.studio().parts[part];
        const float hz = v.data[pub::Hz];
        // For the tutorial: how long a voice has sung, and how long with a
        // harmony finger down over it.
        {
            const double dt = std::clamp(t - m_lastHeard, 0.0, 0.1);
            m_lastHeard = t;
            if (hz > 0) {
                m_sungSecs += dt;
                for (const Finger &h : m_fingers)
                    if (h.id >= 0 && !h.clicker && h.zone == Keys && h.mode == HarmonyNote) {
                        m_harmonySecs += dt;
                        break;
                    }
            }
        }
        if (t - m_lastTrail >= 1.0 / 60) {
            m_lastTrail = t;
            m_trail[m_trailW++ % kTrail] = {t, hz > 0 ? midiOfHz(hz) : -1.0f, v.data[pub::InRms]};
            m_ovTrail[m_ovW++ % kTrail] = {t, v.data[pub::Overtone], 0};
            m_tkTrail[m_tkW++ % kTrail] = {t, v.data[pub::TalkGate] > 0.05f ? v.data[pub::TalkMidi] : -1.0f,
                                           v.data[pub::TalkGate]};
        }
        // The keys sit where the voice is: an octave around what has been
        // sung lately, moved only while no finger is on them.
        bool keysHeld = false;
        for (const Finger &h : m_fingers)
            keysHeld |= h.id >= 0 && h.zone == Keys && !h.clicker;
        if (!keysHeld && t - m_lastRegister > 0.5) {
            m_lastRegister = t;
            std::vector<float> sung;
            for (int k = 0; k < kTrail; ++k)
                if (m_trail[k].midi > 0 && t - m_trail[k].t < 10)
                    sung.push_back(m_trail[k].midi);
            if (sung.size() > 20) {
                std::nth_element(sung.begin(), sung.begin() + long(sung.size() / 2), sung.end());
                m_sungMedian = sung[sung.size() / 2];
            }
        }
        // The monitor follows the page unless asked otherwise.
        const bool want = m_monitorMode == 1 || (m_monitorMode == 0 && m_shown);
        if (want != m_monitorSent) {
            m_monitorSent = want;
            param(ctx, PMonitor, want ? 1 : 0);
        }
        // Auto level: the loudest of three seconds of singing lands at
        // −10 dBFS (headroom for the shout).
        if (m_autoUntil > 0) {
            m_autoPeak = std::max(m_autoPeak, v.data[pub::InPeak]);
            if (t >= m_autoUntil) {
                m_autoUntil = -1;
                if (m_autoPeak > 1e-4f) {
                    const float peakDb = 20.0f * std::log10(m_autoPeak);
                    setMicGain(m_micDb + (-10.0f - peakDb), ctx);
                } else {
                    ctx.notice(QStringLiteral("Heard nothing: is the microphone muted?"));
                }
            }
        }
        // A microphone still connecting after two seconds isn't coming.
        if (ctx.studio().input == StudioView::InputConnecting) {
            if (m_connectingSince < 0)
                m_connectingSince = t;
        } else {
            m_connectingSince = -1;
        }
        // Takes landing in the studio: the loop has changed since.
        if (v.takes != m_takesSeen || v.playing != m_playingSeen) {
            m_takesSeen = v.takes;
            m_playingSeen = v.playing;
            m_loopSince = ctx.beatAt(t);
        }
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (k.key == Qt::Key_Space) {
            if (k.down && !k.repeat)
                space(ctx, shift);
            return true;
        }
        // Held keys: notes on the home row, throws, freeze, stutter and the
        // tape on the top row.
        const int lane = homeLane(k.key);
        const Zone held = k.key == Qt::Key_Q   ? Echo
                          : k.key == Qt::Key_W ? Reverb
                          : k.key == Qt::Key_E ? Freeze
                          : k.key == Qt::Key_R ? Stutter
                                               : Keys;
        if ((lane >= 0 || held != Keys || k.key == Qt::Key_T) && !(k.modifiers & Qt::ControlModifier)) {
            if (k.repeat)
                return true;
            const int id = kKeyFinger + k.key;
            if (!k.down) {
                if (k.key == Qt::Key_T) {
                    m_tapeKey = false;
                    sendTape(ctx);
                } else if (Finger *h = find(id)) {
                    lift(*h, ctx);
                }
                return true;
            }
            if (k.key == Qt::Key_T) {
                m_tapeKey = true;
                sendTape(ctx);
                return true;
            }
            Finger *h = find(-1);
            if (!h || find(id))
                return true;
            Touch t;
            t.id = id;
            t.pressure = 0.6f;
            t.size = 1;
            if (lane >= 0) {
                const int lanes = laneCount(ctx.studio());
                t.x = kLeft + (kRight - kLeft) * (float(lane) + 0.5f) / float(lanes);
                t.y = kThroatMid;
            } else if (held == Echo) {
                t.x = 0.15f, t.y = 0.93f; // the dotted 1/8
            } else if (held == Reverb) {
                t.x = 0.375f, t.y = 0.93f; // a hall
            } else {
                t.x = 0.955f;
                t.y = k.key == Qt::Key_E ? 0.3f : 0.75f;
            }
            land(*h, t, ctx.now(), ctx);
            h->key = true;
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 emptied").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        switch (k.key) {
        case Qt::Key_Backspace: ctx.stop(); return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight: {
            const int dir = (k.key == Qt::Key_Right || k.key == Qt::Key_BracketRight) ? 1 : -1;
            if (shift || k.key == Qt::Key_BracketLeft || k.key == Qt::Key_BracketRight)
                setStutter(m_stutter + dir, ctx);
            else
                setPreset((m_preset + dir + PresetCount) % PresetCount, ctx);
            return true;
        }
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const int dir = k.key == Qt::Key_Up ? 1 : -1;
            // Just after Space they make the loop longer or shorter.
            if (m_last.active && ctx.now() - m_last.at <= kRetakeSecs) {
                resizeLoop(dir, ctx);
                return true;
            }
            m_octave = std::clamp(m_octave + dir, -2, 2);
            ctx.notice(QStringLiteral("Keys %1").arg(m_octave == 0 ? QStringLiteral("around your voice")
                                                                    : QStringLiteral("%1%2 octave%3")
                                                                          .arg(m_octave > 0 ? "+" : "")
                                                                          .arg(m_octave)
                                                                          .arg(std::abs(m_octave) > 1 ? "s" : "")));
            return true;
        }
        case Qt::Key_Minus:
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            setFormant(m_formant + (k.key == Qt::Key_Minus ? -1 : 1), ctx);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift) {
                ctx.notice(QStringLiteral("On the Vocal page, Shift+Space layers onto the loop"));
                return true;
            }
            setLatch(!m_latch, ctx);
            return true;
        case Qt::Key_section:
        case Qt::Key_QuoteLeft:
        case Qt::Key_Backslash:
            setFollow(!m_follow, ctx);
            return true;
        case Qt::Key_Delete: letGo(ctx); return true;
        case Qt::Key_M: cycleMonitor(ctx); return true;
        case Qt::Key_B: styleOption(ctx, shift); return true;
        case Qt::Key_V: setDemo(!m_demo, ctx); return true;
        case Qt::Key_Z: autoLevel(ctx); return true;
        case Qt::Key_Comma: setMicGain(m_micDb - 3, ctx); return true;
        case Qt::Key_Period: setMicGain(m_micDb + 3, ctx); return true;
        default: return false;
        }
    }

    void enter(ControlContext &ctx) override {
        m_shown = true;
        if (!m_sentAll) {
            m_sentAll = true;
            param(ctx, PPreset, m_preset);
            param(ctx, PStutter, m_stutter);
            sendStyleOpt(ctx);
        }
    }

    void leave(ControlContext &ctx) override {
        for (Finger &h : m_fingers)
            if (h.id >= 0)
                lift(h, ctx);
        if (!m_latch)
            letGoLatched(ctx);
        m_tapeKey = false;
        m_button = false;
        sendTape(ctx);
        m_frame = Frame{};
        m_shown = false;
    }

    void paint(QPainter &p, const PaintContext &pc) override;
    enum Spot {
        SpotPreset = 100,
        SpotMonitor = 200,
        SpotLatch,
        SpotFollow,
        SpotGainDown,
        SpotGainUp,
        SpotNew,
        SpotLayer,
        SpotDemo,
        SpotAuto,
        SpotStyleOpt
    };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, *&ctx.studio().parts[ctx.part()], ctx))
            return true;
        if (id >= int(SpotPreset) && id < int(SpotPreset) + int(PresetCount)) {
            setPreset(id - SpotPreset, ctx);
            return true;
        }
        switch (id) {
        case SpotMonitor: cycleMonitor(ctx); return true;
        case SpotLatch: setLatch(!m_latch, ctx); return true;
        case SpotFollow: setFollow(!m_follow, ctx); return true;
        case SpotGainDown: setMicGain(m_micDb - 3, ctx); return true;
        case SpotGainUp: setMicGain(m_micDb + 3, ctx); return true;
        case SpotNew: space(ctx, false); return true;
        case SpotLayer: space(ctx, true); return true;
        case SpotDemo: setDemo(!m_demo, ctx); return true;
        case SpotAuto: autoLevel(ctx); return true;
        case SpotStyleOpt: styleOption(ctx, false); return true;
        default: return false;
        }
    }

    QString help() const override {
        return QStringLiteral(
            "sing\tyou hear yourself at once, through everything below; the page shows the note you sing and the "
            "words as they go by\n"
            "Space\ta NEW vocal loop of what you just sang: the last phrase, in whole bars, its cuts moved up to a "
            "beat off the bar lines so they fall in a breath, never in a word (the bracket on the tape shows what "
            "Space would take, before you press it)\n"
            "Shift+Space\tLAYER what you sang over the vocal loop onto it (on the other pages it is the other way "
            "round: on this one a take is a take, and layering is the choice)\n"
            "Up / Down just after Space\tthe loop longer or shorter (1, 2, 4, 8, 16 bars; with nothing playing, a "
            "phrase more or less), its cuts found again in the silences\n"
            "Space again while a take waits\tcall it off\n"
            "with nothing else playing\tsing a phrase and press Space where it would start again: the loop sets the "
            "tempo\n"
            "a finger in the middle\tyour voice again, a harmony of what you sing, in the key: the lanes are −8ve, "
            "−5th, −3rd, +3rd, +5th, +6th, +8ve, your own note on the line between the thirds (it follows your "
            "melody, so it never clashes)\n"
            "in PRISM, VOCODER and the TALK BOX's KEYS\tthe lanes are notes of the key instead, two octaves around your voice: your "
            "voice (or the vocoder, or the talk box's synth) sings exactly the notes you hold\n"
            "its height · pressure · twist\tthe throat (your own on the middle line, darker below, brighter above) · its level · a detune that widens it\n"
            "more fingers\ta harmony, a chord, a ten-voice choir of you\n"
            "the tape (the strip of waveform under the lanes)\tTOUCH A WORD and it plays again, over and over while "
            "you hold; slide to another word; twist to pitch it; a loop can take what you chop\n"
            "left edge\ta resonant filter: slide up and down for a wah, press for more\n"
            "right edge, lower half\tFREEZE: the last sound held as a cloud you can sing over; twist to pitch it\n"
            "right edge, upper half\tSTUTTER: the last slice repeated on the grid, faster higher up; the voice "
            "steps aside\n"
            "top edge\tfour throws, left to right: ECHO (across: 1/8, dotted 1/8, 1/4; press for more repeats), "
            "REVERB (across: room to cathedral; twist for shimmer), RADIO and CRUSH (further across is rougher); "
            "the echo and reverb ring on after you lift\n"
            "bottom edge\tplays nothing: rest a thumb there, and click the pad there\n"
            "click the pad\tTAPE STOP: the voice winds down while you hold it\n"
            "Left / Right\tthe voice: natural, hard tune, prism (only the notes you hold), choir on the Chords "
            "page's chord, vocoder, robot, then five from further afield (the status line says what each does). "
            "TALK BOX: a funk synth up a tube, your mouth shaping it into words. SUNG (B), it plays the note you "
            "sing, snapped to the key, slides between notes with a Minimoog's glide and scoops into a phrase; "
            "fingers add synth harmonies, spread left and right, moving with it as one hand. KEYS (B again), the "
            "lanes are a keyboard in the key, as a talk box is played: the latest lane held is the note, mono, "
            "gliding from the one before when you play legato; you only mouth the words (no key, no note); press "
            "harder and the synth's filter opens, dark to bright, and each fresh key opens it a moment more (the "
            "PRESS bar). Shift+B: its sub octave on or off. The page draws the synth's pitch sliding to its note. "
            "THROAT: Tuvan throat singing: your voice is the drone, and a melody of its own overtones walks a "
            "four-bar figure on the ones in the key, home on the root at every bar line (the page shows the "
            "ladder of overtones); a finger picks the overtone nearest its note. B: SYGYT, a whistle high over a "
            "pressed drone, your own harmonic pinned, a gap cut round it; KARGYRAA, the growl, every other cycle "
            "weaker and the subharmonic an octave under you leading, a vowel walking its overtones low down; "
            "KHOOMEI, softer, the overtones lower and the drone whole. SITAR: your voice plays a sitar: each "
            "syllable plucks the string at your note, da and ra in turn (the stroke in bright, the stroke out "
            "softer), a slide you sing bends it (a little of your vibrato with it), the bridge makes it buzz, "
            "drone strings strike da ra dir da across every two beats and thirteen sympathetic strings ring on "
            "(the page lights the bridge and each string as it rings); a finger plucks a second string a "
            "harmony away. GAMELAN: each syllable strikes a pair of bronze bars, beating, over their tubes, in "
            "pelog (or slendro, or the song's key: B), your voice drawn into that tuning under them; while you "
            "sing, the kenong on the bar and the kempul on the half bar strike the note you are singing, and "
            "every fourth bar the gong ageng booms, throbbing and sinking as it dies (the page shows the gong's "
            "cycle and the beats to the next gong); a finger plays kotekan, two parts interlocking on the "
            "sixteenths and landing on your note, each lane its own figure: NOROT, NYOG CAG, TELU (each pair's "
            "second lane an octave up) and EMPAT on the top lane. SCREWED: you on slowed tape (a fourth down; "
            "B: a fifth, or 0.8), falling further behind as you sing (the page shows how far against the beats), "
            "and caught up on the grid: after a rest your next phrase drops onto the next beat, and two bars "
            "behind the tape stops and drops at the bar line; a finger is Screw's hand on the crossfader, cutting "
            "to a second copy of the record a beat behind (half a beat on the two lanes at the right), so the "
            "word comes again off the other deck; the lowest lane, held, spins it back; Space makes a loop of "
            "the phrase slowed whole. RIOT: you, screamed, as a riot-grrrl singer sounds from across a field: your "
            "vowels and consonants kept, but the throat made again (pressed folds, the false folds drawn in and "
            "fluttering at half the pitch, the rush of air on every pulse), moved by octaves into a screamer's "
            "register for each phrase and heard three metres off (B: in a FIELD, a small ROOM, or CLOSE); sing "
            "louder and it strains more; a finger adds another screamer at its harmony, as a gang shout. The row of styles under the pad scrolls with the lit one; click any to "
            "pick it\n"
            "B\tthe style's own option, also the chip at the end of the row of styles, and named straight after "
            "the style in the status line: the TALK BOX sung or played from the lanes (KEYS), THROAT's style "
            "(sygyt, kargyraa, khoomei), the GAMELAN's tuning (pelog, slendro, the song's key), the SCREWED "
            "tape's speed (a fourth down, a fifth, 0.8), where the RIOT screams (field, room, close)\n"
            "Shift+B\tthe TALK BOX's sub octave (on, off)\n"
            "Shift+Left / Right (or [ ])\tthe stutter's rate: 1/8, 1/8T, 1/16, 1/16T, 1/32\n"
            "Up / Down\tthe keys an octave up or down\n"
            "- and +\tthe voice's own throat: darker (giant), brighter (elf)\n"
            "Return\tlatch: notes stay when you lift (sing over a held chord)\n"
            "the key left of 1 (§)\tfollow the Chords page: the keys are the chord's notes, hard tune snaps to "
            "them\n"
            "Delete\tlet go: latched notes, freeze, every strip\n"
            "A S D F G H J (K L)\tthe harmony lanes (in PRISM, VOCODER and KEYS, notes) from the keyboard, held\n"
            "V\ta demo singer in place of the microphone, to learn the page and loop it with no mic\n"
            "Z (AUTO)\tsing for three seconds: the microphone's level sets itself\n"
            "Q · W · E · R · T\techo throw · reverb throw · freeze · stutter · tape stop, held\n"
            "M\tmonitor: on this page, always, or off (the loops still record what you sing)\n"
            ", and .\tthe microphone's level down, up (3 dB)\n"
            "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot · empty it · stop the vocal loop at the bar\n"
            "a soft voice\tis lifted by itself while you sing, up to 30 dB (LIFT by the meter says how much), so "
            "you can sing quietly; . and , set the microphone's own gain, Z sets it from three seconds of singing\n"
            "headphones\tthe voice comes back at once: on speakers it would feed back (the page ducks the voice if "
            "it hears that happen)");
    }

    QString status() const override {
        QStringList parts;
        int notes = 0;
        for (const Finger &h : m_fingers)
            if (h.id >= 0 && !h.clicker && h.zone != Rest) {
                if (h.zone == Keys)
                    ++notes;
                else
                    parts << QString::fromLatin1(zoneName(h.zone));
            }
        notes += int(m_latched.size());
        if (notes)
            parts.prepend(notes == 1 ? QStringLiteral("1 voice") : QStringLiteral("%1 voices").arg(notes));
        if (m_button || m_tapeKey)
            parts << QStringLiteral("TAPE STOP");
        QString s = parts.join(QStringLiteral(" + "));
        // The style, and what it does: some of the names mean little to
        // someone who hasn't heard the music they come from. (Its line
        // says what the fingers do, so the general hint gives way to it.)
        // (The style's option comes straight after its name: the line
        // after it may run off the end of the status line, the option
        // mustn't.)
        const QString option = hasOption(m_preset) ? QStringLiteral(" (%1, B)").arg(optionWords()) : QString();
        const QString style = QStringLiteral("%1%2: %3")
                                  .arg(QString::fromLatin1(presetName(m_preset)), option,
                                       QString::fromLatin1(presetLine(m_preset)));
        if (s.isEmpty())
            s = m_preset >= Talk ? style : QStringLiteral("sing; fingers add voices and effects  ·  ") + style;
        else
            s += QStringLiteral("  ·  ") + style;
        // Space's two ways first: what the style does can run off the end
        // of the line (its name is lit under the pad anyway); how to loop
        // must not.
        return QStringLiteral("Space new loop, Shift+Space layer  ·  ") + s;
    }

    void startOver(ControlContext &ctx) override {
        // Every loop is gone: so is their audio, once the engine has let go
        // (a second on, long after the studio has stopped every clip).
        for (int h = 1; h < kMaxTakes; ++h)
            if (m_used[h])
                m_graveyard.push_back({ctx.now() + 1.0, h});
        m_pending.clear();
        m_last = {};
    }
    void undone(ControlContext &) override { m_last = {}; }

    // --- songs ------------------------------------------------------------------
    // The page's own choices: the style, the throat, the stutter, where the
    // keys sit and whether they follow the chords. Not the latch: that is
    // what a hand is doing.
    // The styles by the names a file gives them (the page's are for show).
    static constexpr const char *kStyleNames[PresetCount] = {"natural", "tune", "prism", "choir", "vocoder", "robot",
                                                                 "talkbox", "throat", "sitar", "gamelan", "screwed", "riot"};
    void save(QJsonObject &out) const override {
        out[QStringLiteral("style")] = QLatin1String(kStyleNames[std::clamp(m_preset, 0, PresetCount - 1)]);
        out[QStringLiteral("formant")] = m_formant;
        out[QStringLiteral("stutter")] = m_stutter;
        out[QStringLiteral("octave")] = m_octave;
        out[QStringLiteral("follow")] = m_follow;
        out[QStringLiteral("gamelanTuning")] = QString::fromLatin1(gamTuningName(m_gamTuning)).toLower();
        out[QStringLiteral("talkboxSub")] = m_talkSub;
        out[QStringLiteral("talkboxKeys")] = m_talkKeys;
        out[QStringLiteral("screwedSpeed")] = QLatin1String(screwSpeedKey(m_screwSpeed));
        out[QStringLiteral("throatStyle")] = QString::fromLatin1(throatStyleName(m_throatStyle)).toLower();
        out[QStringLiteral("riotPlace")] = QLatin1String(scream::placeKey(m_riotPlace));
    }
    void load(const QJsonObject &in, ControlContext &ctx) override {
        m_preset = Natural;
        const QString style = in.value(QStringLiteral("style")).toString();
        for (int p = 0; p < PresetCount; ++p)
            if (style == QLatin1String(kStyleNames[p]))
                m_preset = p;
        m_formant = std::clamp(in.value(QStringLiteral("formant")).toInt(0), -6, 6);
        m_stutter = std::clamp(in.value(QStringLiteral("stutter")).toInt(2), 0, kStutterCount - 1);
        m_octave = std::clamp(in.value(QStringLiteral("octave")).toInt(0), -2, 2);
        m_follow = in.value(QStringLiteral("follow")).toBool(false);
        m_gamTuning = Pelog;
        const QString tuning = in.value(QStringLiteral("gamelanTuning")).toString();
        for (int t = 0; t < GamTuningCount; ++t)
            if (tuning == QString::fromLatin1(gamTuningName(t)).toLower())
                m_gamTuning = t;
        m_riotPlace = scream::Field;
        const QString place = in.value(QStringLiteral("riotPlace")).toString();
        for (int k = 0; k < scream::PlaceCount; ++k)
            if (place == QLatin1String(scream::placeKey(k)))
                m_riotPlace = k;
        m_talkSub = in.value(QStringLiteral("talkboxSub")).toBool(true);
        m_talkKeys = in.value(QStringLiteral("talkboxKeys")).toBool(false);
        m_screwSpeed = ScrewFourth;
        const QString speed = in.value(QStringLiteral("screwedSpeed")).toString();
        for (int k = 0; k < ScrewSpeedCount; ++k)
            if (speed == QLatin1String(screwSpeedKey(k)))
                m_screwSpeed = k;
        m_throatStyle = Sygyt;
        const QString throat = in.value(QStringLiteral("throatStyle")).toString();
        for (int k = 0; k < ThroatStyleCount; ++k)
            if (throat == QString::fromLatin1(throatStyleName(k)).toLower())
                m_throatStyle = k;
        // To the engine at once, with no notice for each.
        param(ctx, PPreset, m_preset);
        param(ctx, PStutter, m_stutter);
        param(ctx, PFormant, m_formant);
        param(ctx, PFollow, m_follow ? 1 : 0);
        sendStyleOpt(ctx);
        m_sentAll = true;
    }

    // The loops' audio: every take a clip plays, as 16-bit stereo PCM. A
    // take still being recorded (a layer whose bar hasn't come) has no
    // audio yet and is left out; its clip opens silent.
    void saveAudio(const std::vector<const Command *> &sounds, QJsonObject &out) const override {
        QJsonArray takes;
        bool done[kMaxTakes] = {};
        for (const Command *c : sounds) {
            if (c->type != Command::On || !(c->flags & Command::Sampled))
                continue;
            const int h = c->i[0];
            if (h <= 0 || h >= kMaxTakes || done[h] || !m_used[h])
                continue;
            done[h] = true;
            const std::vector<float> &buf = m_audio[h];
            const int frames = int(buf.size() / 2);
            if (frames <= 0)
                continue;
            QByteArray pcm(qsizetype(frames) * 4, Qt::Uninitialized);
            auto *p = reinterpret_cast<unsigned char *>(pcm.data());
            for (size_t k = 0; k < size_t(2 * frames); ++k) {
                const int v = int(std::lround(std::clamp(buf[k], -1.0f, 1.0f) * 32767.0f));
                p[2 * k] = uint8_t(v & 0xff);
                p[2 * k + 1] = uint8_t((v >> 8) & 0xff);
            }
            QJsonObject take;
            take[QStringLiteral("handle")] = h;
            take[QStringLiteral("rate")] = int(std::lround(m_tape->rate));
            take[QStringLiteral("frames")] = frames;
            take[QStringLiteral("pcm")] = QString::fromLatin1(pcm.toBase64());
            takes.append(take);
        }
        if (!takes.isEmpty())
            out[QStringLiteral("takes")] = takes;
    }

    // Each take gets a handle of its own here (the old song's are still let
    // go of), and the clips are pointed at it. Audio at another rate is
    // resampled to the tape's.
    void openAudio(const QJsonObject &in, const std::vector<Command *> &sounds, ControlContext &ctx) override {
        int fresh[kMaxTakes] = {}; // the file's handle → this one's
        bool known[kMaxTakes] = {};
        for (const QJsonValue &tv : in.value(QStringLiteral("takes")).toArray()) {
            const QJsonObject t = tv.toObject();
            const int h = t.value(QStringLiteral("handle")).toInt(0);
            const int frames = t.value(QStringLiteral("frames")).toInt(0);
            const double rate = t.value(QStringLiteral("rate")).toDouble(0);
            if (h <= 0 || h >= kMaxTakes || known[h] || frames <= 0 || frames > kRing || !(rate >= 8000 && rate <= 384000))
                continue;
            const QByteArray pcm = QByteArray::fromBase64(t.value(QStringLiteral("pcm")).toString().toLatin1());
            if (pcm.size() != qsizetype(frames) * 4)
                continue;
            const int nh = allocHandle();
            if (nh <= 0)
                break;
            known[h] = true;
            fresh[h] = nh;
            const auto *p = reinterpret_cast<const unsigned char *>(pcm.constData());
            auto sample = [&](int k, int ch) {
                const int i = 2 * k + ch;
                return float(int16_t(uint16_t(p[2 * i] | (p[2 * i + 1] << 8)))) / 32767.0f;
            };
            std::vector<float> &buf = m_audio[nh];
            const double step = rate / double(m_tape->rate);
            const int len = std::max(1, int(std::lround(frames / step)));
            buf.assign(size_t(2 * len), 0.0f);
            for (int k = 0; k < len; ++k) {
                const double x = k * step;
                const int a = std::min(int(x), frames - 1), b = std::min(a + 1, frames - 1);
                const float w = float(x - a);
                for (int ch = 0; ch < 2; ++ch)
                    buf[size_t(2 * k + ch)] = sample(a, ch) * (1 - w) + sample(b, ch) * w;
            }
            Tape::Take &take = m_tape->takes[nh];
            take.start = 0;
            take.length = len;
            m_zeroBefore[nh] = INT64_MIN;
            take.data.store(buf.data(), std::memory_order_release);
        }
        // The clips name the new handles; a take the file had no audio for
        // names none, and its clip is silent.
        constexpr int kIdBits = 0xFFFFFF; // a replayed id's own (see replayId)
        bool played[kMaxTakes] = {};
        for (const Command *c : sounds)
            if (c->type == Command::On && (c->flags & Command::Sampled) && c->i[0] > 0 && c->i[0] < kMaxTakes)
                played[c->i[0]] = true;
        int missing = 0;
        for (int h = 1; h < kMaxTakes; ++h)
            missing += played[h] && !known[h];
        for (Command *c : sounds) {
            const int h = (c->id & kIdBits) - kLoopIdBase;
            if (h <= 0 || h >= kMaxTakes || !played[h])
                continue;
            c->id = (c->id & ~kIdBits) | (kLoopIdBase + fresh[h]);
            if (c->flags & Command::Sampled)
                c->i[0] = fresh[h];
        }
        if (missing > 0)
            ctx.notice(missing == 1 ? QStringLiteral("A vocal loop came without its audio: it is silent")
                                    : QStringLiteral("%1 vocal loops came without their audio: they are silent").arg(missing));
    }

    int loopBeats(int dir) const override {
        if (!m_last.active)
            return -1;
        if (m_last.free) {
            const int n = m_last.phrases + dir;
            if (n < 1 || n > int(m_last.starts.size()))
                return 0;
            const double secs = m_last.end - (m_last.starts[m_last.starts.size() - size_t(n)] - 0.03);
            return int(freeBeats(secs));
        }
        const auto steps = stepsFor(m_last);
        const auto it = std::find(steps.begin(), steps.end(), m_last.bars);
        const long at = long(it - steps.begin()) + dir;
        if (it == steps.end() || at < 0 || at >= long(steps.size()))
            return 0;
        return steps[size_t(at)] * 4;
    }

    void resizeLoop(int dir, ControlContext &ctx) override {
        if (!m_last.active)
            return;
        m_last.at = ctx.now();
        if (m_last.free) {
            const int n = m_last.phrases + dir;
            if (n < 1 || n > int(m_last.starts.size())) {
                ctx.notice(dir > 0 ? QStringLiteral("Loop: every phrase you sang") : QStringLiteral("Loop: one phrase"));
                return;
            }
            Pick pk;
            pk.ok = true;
            pk.b0 = m_last.starts[m_last.starts.size() - size_t(n)] - 0.03;
            pk.b1 = m_last.end;
            pk.phrases = n;
            makeTake(ctx, pk, m_last.slot, -1, true, true);
            return;
        }
        const auto steps = stepsFor(m_last);
        auto it = std::find(steps.begin(), steps.end(), m_last.bars);
        if (it == steps.end())
            return;
        // To the next length whose cuts both fall in silence, if there is one.
        PickRequest req = m_last.req;
        const Sung sung = snapshot(ctx);
        Pick best;
        for (long at = long(it - steps.begin()) + dir; at >= 0 && at < long(steps.size()); at += dir) {
            req.bars = steps[size_t(at)];
            const Pick pk = pick(sung, req);
            if (!pk.ok)
                break;
            if (!best.ok)
                best = pk;
            if (pk.cutDb <= 3.0f) {
                best = pk;
                break;
            }
            if (dir < 0)
                break; // shorter: the next one down, wherever it cuts
        }
        if (!best.ok) {
            ctx.notice(dir > 0 ? QStringLiteral("Loop: as long as it goes") : QStringLiteral("Loop: as short as it goes"));
            return;
        }
        m_last.req.bars = best.bars;
        makeTake(ctx, best, m_last.slot, m_last.into, true, false);
    }

    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("loops"))
            return float(m_loopsMade);
        if (name == QLatin1String("sung"))
            return float(m_sungSecs);
        if (name == QLatin1String("harmonized"))
            return float(m_harmonySecs);
        if (name == QLatin1String("demo"))
            return m_demo ? 1.0f : 0.0f;
        return PartControl::tutorialProbe(name);
    }

    // For the tests: the last take's choice.
    Pick lastPick() const { return m_last.active ? m_last.pick : Pick{}; }

private:
    struct Finger {
        int id = -1;
        Zone zone = Keys;
        double t0 = 0, lastSend = 0;
        float x = 0, y = 0, p = 0.5f, angle0 = 0, twist = 0;
        int voice = -1; // the engine's voice id (keys)
        int value = 0, mode = 0;
        int chop = -1;  // the engine's chop voice (the tape)
        int64_t word = INT64_MIN;
        int wordLength = 0;
        bool key = false, clicker = false;
        float sent[4] = {-9, -9, -9, -9};
    };
    static constexpr int kKeyFinger = 1 << 20;
    static constexpr double kRetakeSecs = 3.0;
    static constexpr int kTrail = 600;

    Finger *find(int id) {
        for (Finger &h : m_fingers)
            if (h.id == id)
                return &h;
        return nullptr;
    }
    static const char *zoneName(Zone z) {
        switch (z) {
        case Keys: return "VOICE";
        case Chop: return "WORD";
        case Filter: return "FILTER";
        case Freeze: return "FREEZE";
        case Stutter: return "STUTTER";
        case Rest: return "";
        case Echo: return "ECHO";
        case Reverb: return "REVERB";
        case Radio: return "RADIO";
        case Crush: return "CRUSH";
        }
        return "";
    }
    // The home row: the harmony lanes (A to J, low to high), or the keys'
    // middle notes (A to L).
    int homeLane(int key) const {
        static const int keys[] = {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
                                   Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L};
        for (int i = 0; i < 9; ++i)
            if (keys[i] == key)
                return harmonyLanes() ? (i < kIntervalCount ? i : -1) : i + 3;
        return -1;
    }

    // --- the keys -------------------------------------------------------------------
    Tuning tuningOf(const StudioView &v) const {
        Tuning t;
        t.set(v.root, v.family, v.brightness, v.just);
        return t;
    }
    // A lane's name where it isn't a harmony. The gamelan's: the kotekan
    // figure it plays (the second of a pair an octave up). The screwed
    // tape's: what the hand does.
    QString laneName(int value) const {
        if (m_preset == Screwed)
            return value <= -7 ? QStringLiteral("SPIN")
                   : value >= 5 ? QStringLiteral("CHOP ½")
                                : QStringLiteral("CHOP 1");
        return QString::fromLatin1(kotName(value)) + (kotHigh(value) ? QStringLiteral(" ↑") : QString());
    }
    // A tone of the gamelan's scale as Javanese musicians number them
    // (pelog 1 to 7, slendro 1 2 3 5 6; the song's key 1 up from its root).
    QString gamToneName(const Harmony &h, double midi) const {
        const int n = m_gamTuning == Pelog ? 7 : m_gamTuning == Slendro ? 5 : std::max(1, h.tuning.degreesPerOctave());
        static const int slendro[5] = {1, 2, 3, 5, 6};
        const int k = ((gamDegreeNear(h, m_gamTuning, midi) % n) + n) % n;
        return QString::number(m_gamTuning == Slendro ? slendro[k] : k + 1);
    }
    // (The talk box's KEYS make the lanes a keyboard, as PRISM's are.)
    bool harmonyLanes() const { return harmonyKeys(m_preset) && !(m_preset == Talk && m_talkKeys); }
    int laneCount(const StudioView &v) const {
        if (harmonyLanes())
            return kIntervalCount;
        if (m_follow)
            return std::clamp(v.chordCount, 1, 4) * 2 + 1;
        return tuningOf(v).degreesPerOctave() * 2 + 1;
    }
    // The lowest lane's value: a degree (or chord tone) an octave under the
    // voice, moved by ↑↓.
    int laneBase(const StudioView &v) const {
        const Tuning t = tuningOf(v);
        const int per = m_follow ? std::clamp(v.chordCount, 1, 4) : t.degreesPerOctave();
        const int oct = int(std::lround((m_sungMedian - t.baseMidi() - 12.0) / 12.0)) + m_octave;
        return oct * per;
    }
    Harmony harmonyOf(const StudioView &v) const {
        Harmony h;
        h.tuning = tuningOf(v);
        h.chordRoot = v.chordRoot;
        h.chordCount = v.chordCount;
        for (int i = 0; i < 4; ++i)
            h.chordTones[i] = v.chordTones[i];
        return h;
    }
    double laneMidi(const StudioView &v, int lane) const {
        return noteMidi(harmonyOf(v), laneBase(v) + lane, m_follow ? 1 : 0);
    }
    // What a finger at `lane` sends: an interval (harmony lanes) or a note.
    void laneNote(const StudioView &v, int lane, int &value, int &mode) const {
        if (harmonyLanes()) {
            value = kIntervals[std::clamp(lane, 0, kIntervalCount - 1)];
            mode = HarmonyNote;
        } else {
            value = laneBase(v) + lane;
            mode = m_follow ? ChordNote : ScaleNote;
        }
    }
    int laneAt(const StudioView &v, float x) const {
        const int n = laneCount(v);
        return std::clamp(int((x - kLeft) / (kRight - kLeft) * float(n)), 0, n - 1);
    }

    void land(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        h = Finger{};
        h.id = t.id;
        h.t0 = time;
        h.zone = zoneAt(t.x, t.y);
        h.x = t.x;
        h.y = t.y;
        h.p = t.pressure;
        h.angle0 = t.angle;
        if (h.zone == Keys) {
            h.voice = m_nextVoice++;
            if (m_nextVoice > 0xFFFF)
                m_nextVoice = 1;
            const StudioView &v = ctx.studio();
            laneNote(v, laneAt(v, t.x), h.value, h.mode);
            sendNote(h, Command::On, ctx);
            ++m_notesPlayed;
        } else if (h.zone == Chop) {
            h.chop = -1;
            for (int k = 0; k < kChops; ++k) {
                bool used = false;
                for (const Finger &o : m_fingers)
                    used |= &o != &h && o.id >= 0 && o.zone == Chop && o.chop == k;
                if (!used) {
                    h.chop = k;
                    break;
                }
            }
            sendChop(h, true, ctx);
        } else {
            sendStrip(h, true, ctx);
        }
    }
    void move(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        h.x = t.x;
        h.y = t.y;
        h.p = t.pressure;
        float tw = t.angle - h.angle0;
        while (tw > float(kPi / 2))
            tw -= float(kPi);
        while (tw < -float(kPi / 2))
            tw += float(kPi);
        h.twist = tw;
        if (h.clicker || time - h.lastSend < 1.0 / 60)
            return;
        if (h.zone == Keys) {
            const StudioView &v = ctx.studio();
            int value = 0, mode = 0;
            laneNote(v, laneAt(v, t.x), value, mode);
            if (value != h.value || changed(h)) {
                h.value = value;
                h.mode = mode;
                h.lastSend = time;
                sendNote(h, Command::Move, ctx);
            }
        } else if (h.zone == Chop) {
            if (changed(h)) {
                h.lastSend = time;
                sendChop(h, true, ctx);
            }
        } else if (changed(h)) {
            // Along the right edge the engine hears freeze or stutter by
            // height, so the finger's name follows it across the middle.
            if (h.zone == Freeze || h.zone == Stutter)
                h.zone = h.y >= kRightMid ? Stutter : Freeze;
            h.lastSend = time;
            sendStrip(h, true, ctx);
        }
    }
    bool changed(Finger &h) const {
        const float now[4] = {h.x, h.y, h.p, h.twist};
        for (int i = 0; i < 4; ++i)
            if (std::fabs(now[i] - h.sent[i]) > 0.005f)
                return true;
        return false;
    }
    void lift(Finger &h, ControlContext &ctx) {
        if (h.clicker) {
            h.id = -1;
            return;
        }
        if (h.zone == Keys) {
            if (m_latch && h.voice > 0)
                m_latched.push_back(h.voice);
            else
                release(h, ctx);
        } else if (h.zone == Chop) {
            sendChop(h, false, ctx);
        } else {
            sendStrip(h, false, ctx);
        }
        h.id = -1;
    }
    void release(Finger &h, ControlContext &ctx) {
        if (h.voice > 0) {
            Command c;
            c.type = Command::Off;
            c.flags = Command::NoRecord;
            c.id = h.voice;
            ctx.send(c);
            h.voice = -1;
        }
    }
    void sendNote(Finger &h, uint16_t type, ControlContext &ctx) {
        Command c;
        c.type = type;
        c.flags = Command::NoRecord;
        c.id = h.voice;
        c.i[0] = h.value;
        c.i[1] = h.mode;
        const float u = clampf((h.y - kTapeTop) / (kTop - kTapeTop), 0, 1);
        c.f[0] = 0.3f + 0.9f * clampf(h.p, 0, 1);
        c.f[1] = u;
        c.f[2] = clampf(h.twist / 0.8f, -1, 1);
        c.f[3] = clampf((h.x - kLeft) / (kRight - kLeft), 0, 1);
        h.sent[0] = h.x, h.sent[1] = h.y, h.sent[2] = h.p, h.sent[3] = h.twist;
        ctx.send(c);
    }
    void sendStrip(Finger &h, bool on, ControlContext &ctx) {
        h.sent[0] = h.x, h.sent[1] = h.y, h.sent[2] = h.p, h.sent[3] = h.twist;
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        const float p = clampf(h.p, 0, 1);
        const float yIn = clampf((h.y - kBottom) / (kTop - kBottom), 0, 1);
        switch (h.zone) {
        case Filter:
            c.i[0] = PFilter;
            c.f[0] = yIn;
            c.f[1] = p;
            break;
        case Freeze:
        case Stutter:
            c.i[0] = PFreeze;
            c.f[0] = yIn;
            c.f[1] = p;
            c.f[2] = std::round(h.twist / 0.8f * 12.0f);
            c.f[3] = on ? 1 : 0;
            break;
        case Echo:
            c.i[0] = PEcho;
            c.f[0] = clampf(h.x * 4, 0, 0.999f);
            c.f[1] = p;
            break;
        case Reverb:
            c.i[0] = PReverb;
            c.f[0] = clampf((h.x - 0.25f) * 4, 0, 1);
            c.f[1] = p;
            c.f[3] = clampf(0.25f + h.twist / 0.8f, 0, 1);
            break;
        // One lo-fi engine, its first half the radio and its second the
        // crush, each across its own quarter.
        case Radio:
            c.i[0] = PLofi;
            c.f[0] = clampf((h.x - 0.5f) * 2, 0, 0.499f);
            c.f[1] = p;
            break;
        case Crush:
            c.i[0] = PLofi;
            c.f[0] = clampf(0.5f + (h.x - 0.75f) * 2, 0.5f, 1);
            c.f[1] = p;
            break;
        case Keys:
        case Chop:
        case Rest: return;
        }
        if (h.zone != Freeze && h.zone != Stutter)
            c.f[2] = on ? 1 : 0;
        // A strip a second finger still holds stays on.
        if (!on)
            for (Finger &o : m_fingers)
                if (&o != &h && o.id >= 0 && !o.clicker && sameStrip(o.zone, h.zone)) {
                    sendStrip(o, true, ctx);
                    return;
                }
        ctx.send(c);
        if (on)
            ++m_effectsUsed;
    }
    // A finger on the tape plays the word under it, from its first sound,
    // over and over while held; sliding finds the next word; twist
    // pitches it.
    void sendChop(Finger &h, bool on, ControlContext &ctx) {
        if (h.chop < 0)
            return;
        h.sent[0] = h.x, h.sent[1] = h.y, h.sent[2] = h.p, h.sent[3] = h.twist;
        if (on) {
            const StudioView &v = ctx.studio();
            const Tape &tape = *m_tape;
            const double rate = tape.rate;
            const int64_t written = tape.written.load(std::memory_order_acquire);
            const double nowT = v.time + (double(written) - double(v.sample)) / rate;
            const double span = tapeSpan(v);
            const double t = nowT - span * (1.0 - double((h.x - kLeft) / (kRight - kLeft)));
            // The word nearest the finger (the envelope's words, without
            // the latency: the tape is drawn as it was recorded).
            Sung s;
            s.hop = kHop / rate;
            const int64_t last = written / kHop, first = std::max<int64_t>(0, last - int64_t(span / s.hop) - 64);
            s.env.resize(size_t(std::max<int64_t>(0, last - first)));
            for (int64_t k = first; k < last; ++k)
                s.env[size_t(k - first)] = tape.env[size_t(k & (kEnvRing - 1))];
            s.t0 = v.time + (double(first * kHop + kHop / 2) - double(v.sample)) / rate;
            const auto words = findWords(s);
            double bestD = 0.35, start = t, end = t + 0.25;
            for (const Word &w : words) {
                const double a = s.time(w.start), b = s.time(w.end);
                const double d = t < a ? a - t : t > b ? t - b : 0;
                if (d < bestD) {
                    bestD = d;
                    start = a - 0.01;
                    end = b + 0.03;
                }
            }
            h.word = int64_t(std::llround(double(v.sample) + (start - v.time) * rate));
            h.wordLength = int(std::max(0.05, end - start) * rate);
        }
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = PChop;
        c.i[1] = h.chop;
        c.i[2] = int32_t(uint32_t(uint64_t(h.word) >> 32));
        c.i[3] = int32_t(uint32_t(uint64_t(h.word) & 0xFFFFFFFFu));
        c.f[0] = on ? 1 : 0;
        c.f[1] = float(h.wordLength);
        c.f[2] = 0.4f + 0.9f * clampf(h.p, 0, 1);
        c.f[3] = std::round(h.twist / 0.8f * 12.0f);
        ctx.send(c);
        if (on)
            ++m_chopsPlayed;
    }
    // How much of the past the tape shows: eight bars with a clock, eight
    // seconds without.
    static double tapeSpan(const StudioView &sv) {
        bool anyPlaying = sv.metronome;
        for (int q = 0; q < sv.partCount; ++q)
            anyPlaying |= sv.parts[q].playing >= 0 || sv.parts[q].queued >= 0;
        return anyPlaying ? 8 * 4 * 60.0 / std::max(1.0, sv.bpm) : 8.0;
    }
    static bool sameStrip(Zone a, Zone b) {
        auto pair = [](Zone z, Zone u, Zone v) { return z == u || z == v; };
        return a == b || (pair(a, Radio, Crush) && pair(b, Radio, Crush)) ||
               (pair(a, Freeze, Stutter) && pair(b, Freeze, Stutter));
    }
    void sendTape(ControlContext &ctx) {
        const bool on = m_button || m_tapeKey;
        if (on == m_tapeSent)
            return;
        m_tapeSent = on;
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = PTape;
        c.f[0] = on ? 1 : 0;
        ctx.send(c);
    }
    void param(ControlContext &ctx, int which, int value, float f = 0) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = which;
        c.i[1] = value;
        c.f[0] = f;
        ctx.send(c);
    }
    void letGoLatched(ControlContext &ctx) {
        for (int v : m_latched) {
            Command c;
            c.type = Command::Off;
            c.flags = Command::NoRecord;
            c.id = v;
            ctx.send(c);
        }
        m_latched.clear();
    }
    void letGo(ControlContext &ctx) {
        letGoLatched(ctx);
        for (Finger &h : m_fingers)
            if (h.id >= 0 && h.zone != Keys)
                sendStrip(h, false, ctx);
        ctx.notice(QStringLiteral("Let go: latched notes, freeze and every strip"));
    }
    void setPreset(int p, ControlContext &ctx) {
        m_preset = p;
        param(ctx, PPreset, p);
        ctx.notice(QStringLiteral("%1: %2").arg(QString::fromLatin1(presetName(p)), QString::fromLatin1(presetLine(p))));
    }
    // B, or the chip beside the styles: the style's own option. The
    // gamelan's tuning (pelog, slendro, or the song's key, to play in tune
    // with the other pages); the talk box played by the voice or from the
    // lanes (and with Shift, its sub octave); throat singing's style; the
    // screwed tape's speed.
    bool hasOption(int preset) const {
        return preset == Gamelan || preset == Talk || preset == Screwed || preset == Throat || preset == Riot;
    }
    QString optionLabel() const {
        if (m_preset == Throat)
            return QStringLiteral("%1  B").arg(QString::fromLatin1(throatStyleName(m_throatStyle)));
        if (m_preset == Gamelan)
            return QStringLiteral("%1  B").arg(QString::fromLatin1(gamTuningName(m_gamTuning)));
        if (m_preset == Screwed)
            return QStringLiteral("%1  B").arg(QString::fromUtf8(screwSpeedName(m_screwSpeed)));
        if (m_preset == Riot)
            return QStringLiteral("%1  B").arg(QString::fromLatin1(scream::placeName(m_riotPlace)));
        // (The sub, on by default, is named only when it is off.)
        return QStringLiteral("%1%2  B")
            .arg(m_talkKeys ? QStringLiteral("KEYS") : QStringLiteral("SUNG"))
            .arg(m_talkSub ? QString() : QStringLiteral(" −SUB"));
    }
    // The option as the status line says it.
    QString optionWords() const {
        if (m_preset == Throat)
            return QString::fromLatin1(throatStyleName(m_throatStyle)).toLower();
        if (m_preset == Gamelan)
            return QString::fromLatin1(gamTuningName(m_gamTuning)).toLower();
        if (m_preset == Screwed)
            return QString::fromUtf8(screwSpeedName(m_screwSpeed)).toLower();
        if (m_preset == Riot)
            return QString::fromLatin1(scream::placeKey(m_riotPlace));
        return m_talkKeys ? QStringLiteral("keys") : QStringLiteral("sung");
    }
    void styleOption(ControlContext &ctx, bool shift) {
        if (m_preset == Throat) {
            m_throatStyle = (m_throatStyle + 1) % ThroatStyleCount;
            static const char *why[] = {
                "sygyt, a whistle high over a pressed drone, a gap cut round it",
                "kargyraa, the growl: the subharmonic an octave under you leads, a vowel walks its overtones",
                "khoomei, softer: the overtones lower and less lifted, the drone whole"};
            ctx.notice(QStringLiteral("Throat singing: %1").arg(QString::fromLatin1(why[m_throatStyle])));
        } else if (m_preset == Gamelan) {
            m_gamTuning = (m_gamTuning + 1) % GamTuningCount;
            static const char *why[] = {"pelog, seven tones on the key's root", "slendro, five tones on the key's root",
                                        "the song's key, in tune with the other pages"};
            ctx.notice(QStringLiteral("Gamelan tuning: %1").arg(QString::fromLatin1(why[m_gamTuning])));
        } else if (m_preset == Talk && shift) {
            m_talkSub = !m_talkSub;
            ctx.notice(m_talkSub ? QStringLiteral("Talk box: a square an octave under the synth")
                                 : QStringLiteral("Talk box: no sub octave"));
        } else if (m_preset == Talk) {
            // The fingers held mean something else now: let them go.
            for (Finger &h : m_fingers)
                if (h.id >= 0 && h.zone == Keys)
                    lift(h, ctx);
            letGoLatched(ctx);
            m_talkKeys = !m_talkKeys;
            ctx.notice(m_talkKeys ? QStringLiteral("Talk box KEYS: the lanes play the synth, your mouth shapes it")
                                  : QStringLiteral("Talk box SUNG: the synth plays the note you sing"));
        } else if (m_preset == Screwed) {
            m_screwSpeed = (m_screwSpeed + 1) % ScrewSpeedCount;
            static const char *why[] = {"a fourth down, three quarters the speed", "a fifth down, two thirds the speed",
                                        "0.8 of the speed, the classic pitched-down deck"};
            ctx.notice(QStringLiteral("Screwed tape: %1").arg(QString::fromLatin1(why[m_screwSpeed])));
        } else if (m_preset == Riot) {
            m_riotPlace = (m_riotPlace + 1) % scream::PlaceCount;
            static const char *why[] = {"in a field three metres off, the ground's echo and open air",
                                        "three metres off in a small room, its walls and a tail",
                                        "close: the scream with no place round it"};
            ctx.notice(QStringLiteral("Riot: %1").arg(QString::fromLatin1(why[m_riotPlace])));
        } else {
            ctx.notice(QStringLiteral("%1 has no option of its own (B: the TALK BOX's keys, THROAT's style, the "
                                      "GAMELAN's tuning, the SCREWED tape's speed, where the RIOT screams)")
                           .arg(QString::fromLatin1(presetName(m_preset))));
            return;
        }
        sendStyleOpt(ctx);
    }
    void sendStyleOpt(ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = PStyleOpt;
        c.i[1] = m_gamTuning;
        c.i[2] = m_talkSub ? 1 : 0;
        c.i[3] = m_talkKeys ? 1 : 0;
        c.f[0] = float(m_screwSpeed);
        c.f[1] = float(m_throatStyle);
        c.f[2] = float(m_riotPlace);
        ctx.send(c);
    }
    void setStutter(int s, ControlContext &ctx) {
        m_stutter = std::clamp(s, 0, kStutterCount - 1);
        param(ctx, PStutter, m_stutter);
        ctx.notice(QStringLiteral("Stutter from %1 (faster higher up the edge)").arg(QString::fromLatin1(kStutterNames[m_stutter])));
    }
    void setFormant(int f, ControlContext &ctx) {
        m_formant = std::clamp(f, -6, 6);
        param(ctx, PFormant, m_formant);
        ctx.notice(m_formant == 0 ? QStringLiteral("Your own throat")
                                  : QStringLiteral("Throat %1%2: %3")
                                        .arg(m_formant > 0 ? "+" : "")
                                        .arg(m_formant)
                                        .arg(m_formant < 0 ? QStringLiteral("darker, bigger") : QStringLiteral("brighter, smaller")));
    }
    void setLatch(bool on, ControlContext &ctx) {
        m_latch = on;
        if (!on)
            letGoLatched(ctx);
        ctx.notice(on ? QStringLiteral("Latch: notes stay when you lift") : QStringLiteral("Latch off"));
    }
    void setFollow(bool on, ControlContext &ctx) {
        m_follow = on;
        param(ctx, PFollow, on ? 1 : 0);
        ctx.notice(on ? QStringLiteral("Following the Chords page: the keys are the chord's notes")
                      : QStringLiteral("The keys are the key's notes"));
    }
    void setDemo(bool on, ControlContext &ctx) {
        m_demo = on;
        param(ctx, PDemo, on ? 1 : 0);
        ctx.notice(on ? QStringLiteral("A demo singer in place of the microphone: play over it, loop it (V again: off)")
                      : QStringLiteral("Demo singer off"));
    }
    void autoLevel(ControlContext &ctx) {
        m_autoUntil = ctx.now() + 3.0;
        m_autoPeak = 0;
        ctx.notice(QStringLiteral("Sing as loud as you will for three seconds: the level follows"));
    }
    void cycleMonitor(ControlContext &ctx) {
        m_monitorMode = (m_monitorMode + 1) % 3;
        static const char *what[] = {"on this page", "on every page", "off (loops still record)"};
        ctx.notice(QStringLiteral("Monitor %1").arg(QString::fromLatin1(what[m_monitorMode])));
    }
    void setMicGain(float db, ControlContext &ctx) {
        m_micDb = clampf(db, -24, 24);
        param(ctx, PMicGain, 0, m_micDb);
        ctx.notice(QStringLiteral("Microphone %1%2 dB").arg(m_micDb > 0 ? "+" : "").arg(double(m_micDb), 0, 'f', 0));
    }

    // --- Space ----------------------------------------------------------------------
    struct LastTake {
        bool active = false, free = false;
        double at = 0;      // when it was made or last changed
        int slot = -1, into = -1, handle = 0, bars = 0, phrases = 0;
        double end = 0;     // free: Space's moment
        std::vector<double> starts; // free: the phrases' starts, oldest first
        PickRequest req;
        Pick pick;
        double base = 0;
    };

    // What was sung, as pick() reads it: the envelope of the last forty
    // seconds, each hop at the moment it was sung (the trip through the
    // audio server taken off).
    Sung snapshot(ControlContext &ctx) const {
        Sung s;
        const StudioView &v = ctx.studio();
        const Tape &tape = *m_tape;
        const double rate = tape.rate;
        s.hop = kHop / rate;
        const int64_t written = tape.written.load(std::memory_order_acquire);
        const int64_t last = written / kHop; // hops before this are complete
        const int64_t first = std::max<int64_t>(0, last - (kEnvRing - 64));
        s.env.resize(size_t(last - first));
        for (int64_t h = first; h < last; ++h)
            s.env[size_t(h - first)] = tape.env[size_t(h & (kEnvRing - 1))];
        const double sampleTime = v.time + (double(first * kHop + kHop / 2) - double(v.sample)) / rate;
        s.t0 = sampleTime - latency(v);
        return s;
    }
    // Seconds from a sound sung to its place on the tape: the server's
    // trip both ways (the singer sings to what they hear) and the studio's
    // own. Offline there is none.
    static double latency(const StudioView &v) {
        if (v.inputMs + v.outputMs <= 0)
            return 0;
        return double(v.inputMs + v.outputMs + v.bufferMs) / 1000.0;
    }
    int64_t sampleAt(const StudioView &v, double t) const {
        return int64_t(std::llround(double(v.sample) + (t + latency(v) - v.time) * m_tape->rate));
    }
    static double freeBeats(double secs) {
        double len = 1;
        while (len * 60.0 / secs < 88.0 && len < 32)
            len *= 2;
        return len;
    }
    static std::vector<int> stepsFor(const LastTake &l) {
        if (l.into >= 0 && l.base > 0) {
            std::vector<int> out;
            for (int b : barSteps())
                if (b <= int(l.base) && int(l.base) % b == 0)
                    out.push_back(b);
            return out;
        }
        return barSteps();
    }

    void space(ControlContext &ctx, bool layer) {
        const int part = ctx.part();
        const StudioView &v = ctx.studio();
        const PartView &pv = v.parts[part];
        if (pv.recording >= 0) {
            ctx.cancelTake();
            m_last = {};
            ctx.notice(QStringLiteral("Loop called off"));
            return;
        }
        if (v.input == StudioView::NoInput && !m_demo) {
            ctx.notice(QStringLiteral("No microphone: nothing to loop (V: a demo singer)"));
            return;
        }
        const int into = layer ? pv.playing : -1;
        if (layer && into < 0)
            ctx.notice(QStringLiteral("No vocal loop playing to layer onto: a new loop instead"));
        const int slot = ctx.freeSlot();
        if (slot < 0) {
            ctx.notice(QStringLiteral("Every slot is full: clear one first (Shift+F1–F8)"));
            return;
        }
        bool anyPlaying = v.metronome;
        for (int q = 0; q < v.partCount; ++q)
            anyPlaying |= v.parts[q].playing >= 0 || v.parts[q].queued >= 0;
        const double now = ctx.now();
        PickRequest req;
        req.free = !anyPlaying;
        req.now = now;
        req.beat = ctx.beatAt(now);
        req.bpm = std::max(1.0, v.bpm);
        // Over the Chords page's loop, the take may grow to cover its
        // progression: the line meets the same chords every time round.
        const PartView &chords = v.parts[kChordsPart];
        if (kChordsPart != part && chords.playing >= 0)
            req.fit = chords.clips[chords.playing].lengthBeats / 4.0;
        if (into >= 0) {
            req.layerBase = pv.clips[into].lengthBeats / 4.0;
            req.since = m_loopSince;
        }
        const Sung sung = snapshot(ctx);
        Pick pk = pick(sung, req);
        if (!pk.ok) {
            ctx.notice(req.free ? QStringLiteral("Sing a phrase first, then Space where it would start again")
                                : QStringLiteral("Sing first, then Space: nothing sung lately"));
            return;
        }
        m_last = {};
        m_last.active = true;
        m_last.req = req;
        m_last.free = req.free;
        m_last.end = now;
        m_last.base = req.layerBase;
        if (req.free) {
            float thr = 0;
            const auto words = findWords(sung, &thr);
            std::vector<Word> heard;
            for (const Word &w : words)
                if (sung.time(w.start) <= now + 0.02)
                    heard.push_back(w);
            for (const Word &w : phrasesOf(heard, int(0.8 / sung.hop)))
                m_last.starts.push_back(sung.time(w.start));
        }
        ++m_loopsMade;
        makeTake(ctx, pk, slot, into, false, req.free);
    }

    // The take's audio gets a handle, the studio a clip of it.
    void makeTake(ControlContext &ctx, const Pick &pk, int slot, int into, bool retake, bool free) {
        const StudioView &v = ctx.studio();
        const int handle = allocHandle();
        if (handle <= 0) {
            ctx.notice(QStringLiteral("The vocal tape is full: Ctrl+N clears it"));
            return;
        }
        Tape::Take &take = m_tape->takes[handle];
        double b0 = pk.b0, b1 = pk.b1;
        int64_t s0, len;
        if (free) {
            // As the studio sizes a loop that sets the tempo: whole BPM.
            const double secs = b1 - b0;
            const double beats = freeBeats(secs);
            const double bpm = std::round(beats * 60.0 / secs);
            const double clip = beats * 60.0 / bpm;
            b0 = b1 - clip;
            const double in = v.inputMs + v.outputMs > 0 ? double(v.inputMs + v.bufferMs) / 1000.0 : 0;
            const int64_t end = int64_t(std::llround(double(v.sample) + (b1 + in - v.time) * m_tape->rate));
            len = int64_t(std::llround(clip * m_tape->rate));
            s0 = end - len;
        } else {
            s0 = sampleAt(v, ctx.timeAt(b0));
            len = int64_t(std::llround((b1 - b0) * 60.0 / std::max(1.0, v.bpm) * m_tape->rate));
        }
        // SCREWED: a new loop is the phrase slowed honestly, as live singing
        // can't be: all of it, at the tape's speed, so it fills more bars
        // than it was sung in (the fewest whole steps that hold it, the
        // rest a breath of tape), ending where Space's loop would have.
        float rate = 1;
        int source = 0, sungBars = pk.bars;
        if (m_preset == Screwed && into < 0 && !free) {
            rate = screwSpeed(m_screwSpeed);
            int bars = barSteps().back();
            for (int b : barSteps())
                if (double(b) * rate >= double(pk.bars) - 1e-9) {
                    bars = b;
                    break;
                }
            const double spbS = 60.0 / std::max(1.0, v.bpm) * m_tape->rate;
            len = int64_t(std::llround(4.0 * bars * spbS));
            source = int(std::min<int64_t>(int64_t(std::llround((b1 - b0) * spbS)), int64_t(double(len) * rate)));
            b0 = b1 - 4.0 * bars;
            sungBars = bars;
        }
        take.data.store(nullptr, std::memory_order_release);
        take.start = s0;
        take.length = int(len);
        take.rate = rate;
        take.source = source;
        m_zeroBefore[handle] = into >= 0 ? sampleAt(v, ctx.timeAt(m_loopSince)) : INT64_MIN;
        m_pending.push_back(handle);
        copyPending();
        Command sound;
        sound.type = Command::On;
        sound.flags = Command::Sampled;
        sound.id = kLoopIdBase + handle;
        sound.i[0] = handle;
        if (retake && m_last.handle > 0)
            m_graveyard.push_back({ctx.now() + 6.0, m_last.handle});
        ctx.takeAudio(slot, b0, b1, sound, into, retake, free);
        m_last.at = ctx.now();
        m_last.slot = slot;
        m_last.into = into;
        m_last.handle = handle;
        m_last.pick = pk;
        m_last.bars = pk.bars;
        m_last.phrases = pk.phrases;
        // Say what was taken, and how it was cut.
        QString what;
        if (free) {
            const int beats = int(freeBeats(b1 - b0));
            what = QStringLiteral("%1 (%2 phrase%3), setting the tempo")
                       .arg(beats < 4 ? QStringLiteral("%1 beats").arg(beats)
                                      : beats == 4 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(beats / 4))
                       .arg(pk.phrases)
                       .arg(pk.phrases == 1 ? "" : "s");
        } else {
            what = pk.bars == 1 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(pk.bars);
            if (rate < 1)
                what += QStringLiteral(" slowed to %1 (%2×, nothing lost)").arg(sungBars).arg(double(rate), 0, 'f', 2);
            if (std::fabs(pk.delta) > 1e-6)
                what += QStringLiteral(", cut %1 beat %2 the bar")
                            .arg(std::fabs(pk.delta), 0, 'g', 3)
                            .arg(pk.delta < 0 ? QStringLiteral("before") : QStringLiteral("after"));
            what += pk.cutDb <= 3 ? QStringLiteral(" in a breath") : QStringLiteral(" (no silence there: at the quietest)");
        }
        if (into >= 0)
            ctx.notice(QStringLiteral("Layered %1 onto vocal slot %2 · ↑↓ longer, shorter").arg(what).arg(into + 1));
        else
            ctx.notice(QStringLiteral("Vocal loop: %1 in slot %2 · ↑↓ longer, shorter").arg(what).arg(slot + 1));
    }

    int allocHandle() {
        for (int h = 1; h < kMaxTakes; ++h) {
            const int k = (m_nextHandle + h - 1) % (kMaxTakes - 1) + 1;
            if (!m_used[k]) {
                m_used[k] = true;
                m_nextHandle = k + 1;
                return k;
            }
        }
        return -1;
    }
    void freeHandle(int h) {
        if (h <= 0 || h >= kMaxTakes || !m_used[h])
            return;
        m_tape->takes[h].data.store(nullptr, std::memory_order_release);
        m_tape->takes[h].length = 0;
        m_tape->takes[h].rate = 1;
        m_tape->takes[h].source = 0;
        m_audio[h].clear();
        m_audio[h].shrink_to_fit();
        m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), h), m_pending.end());
        m_used[h] = false;
    }
    // A take whose audio is all on the tape now is copied off it: faded
    // in and out at the cuts (they are in silence; the fades only keep a
    // breath from clicking) and, for a layer, silent before the loop
    // began, so the phrase it was made from isn't doubled.
    void copyPending() {
        const Tape &tape = *m_tape;
        const int64_t written = tape.written.load(std::memory_order_acquire);
        for (size_t i = 0; i < m_pending.size();) {
            const int h = m_pending[i];
            Tape::Take &take = m_tape->takes[h];
            const bool slowed = take.rate < 1.0f;
            if (take.start + (slowed ? take.source : take.length) > written) {
                ++i;
                continue;
            }
            std::vector<float> &buf = m_audio[h];
            const int len = take.length;
            buf.assign(size_t(2 * len), 0.0f);
            const int fade = std::max(1, std::min(len / 4, int(0.005 * tape.rate)));
            if (slowed) {
                slowTake(take, buf, written);
                take.data.store(buf.data(), std::memory_order_release);
                m_pending.erase(m_pending.begin() + long(i));
                continue;
            }
            const int ramp = int(0.02 * tape.rate);
            const int64_t zero = m_zeroBefore[h];
            for (int k = 0; k < len; ++k) {
                const int64_t s = take.start + k;
                if (written - s >= kRing)
                    continue; // gone from the ring already (a take far back)
                float g = 1;
                if (k < fade)
                    g = hann().at(0.5f * float(k) / float(fade));
                else if (len - 1 - k < fade)
                    g = hann().at(0.5f * float(len - 1 - k) / float(fade));
                if (s < zero)
                    g *= clampf(1.0f - float(zero - s) / float(ramp), 0, 1);
                buf[size_t(2 * k)] = tape.wetL[size_t(s & kRingMask)] * g;
                buf[size_t(2 * k + 1)] = tape.wetR[size_t(s & kRingMask)] * g;
            }
            take.data.store(buf.data(), std::memory_order_release);
            m_pending.erase(m_pending.begin() + long(i));
        }
    }

    // A slowed take, made from the singing as the SCREWED tape plays it
    // live: read at the tape's speed with its wow and flutter, the top
    // dulled and the loud parts rounded, the second deck 17 ms behind,
    // hiss; faded in and out at its cuts and where the singing ends.
    void slowTake(const Tape::Take &take, std::vector<float> &buf, int64_t written) const {
        const Tape &tape = *m_tape;
        const float sr = tape.rate;
        Svf lp, lp2, hissF;
        lp.set(4600.0f, 0.6f, sr);
        lp2.set(4600.0f, 0.6f, sr);
        hissF.set(4800.0f, 0.5f, sr);
        Rng rng{0x5C2E};
        auto at = [&](double p) {
            if (p < 0 || p >= take.source - 1)
                return 0.0f;
            const int64_t s = take.start + int64_t(p);
            if (written - s >= kRing)
                return 0.0f;
            const float u = float(p - std::floor(p));
            const float a = tape.dry[size_t(s & kRingMask)], b = tape.dry[size_t((s + 1) & kRingMask)];
            return a + (b - a) * u;
        };
        const int len = take.length;
        const int fade = std::max(1, std::min(len / 4, int(0.005 * sr)));
        const double end = double(take.source) / take.rate; // where the singing runs out
        double pos = 0;
        float wow = 0, flut = 0;
        for (int k = 0; k < len; ++k) {
            wow += 0.45f / sr;
            wow -= std::floor(wow);
            flut += 6.5f / sr;
            flut -= std::floor(flut);
            const float main = at(pos);
            const float deck = at(pos - (0.017 + 0.003 * std::sin(kTwoPi * wow * 1.3f)) * sr);
            pos += take.rate * (1.0f + 0.0045f * std::sin(kTwoPi * wow) + 0.0012f * std::sin(kTwoPi * flut));
            const float tone = fastTanh(lp.step(main).low * 1.3f) * (1.0f / 1.3f);
            const float deckTone = lp2.step(deck).low;
            const float hiss = hissF.step(rng.bipolar()).band * 0.006f;
            float g = 1;
            if (k < fade)
                g = hann().at(0.5f * float(k) / float(fade));
            else if (len - 1 - k < fade)
                g = hann().at(0.5f * float(len - 1 - k) / float(fade));
            const float tail = clampf(float((end - double(k)) / double(fade)), 0.0f, 1.0f);
            buf[size_t(2 * k)] = ((tone + deckTone * 0.22f) * tail + hiss) * g;
            buf[size_t(2 * k + 1)] = ((tone + deckTone * 0.3f) * tail + hiss) * g;
        }
    }

    std::shared_ptr<Tape> m_tape;
    Finger m_fingers[16];
    Frame m_frame;
    bool m_button = false, m_tapeKey = false, m_tapeSent = false;
    int m_nextVoice = 1;
    std::vector<int> m_latched;
    int m_preset = Natural, m_octave = 0, m_formant = 0, m_stutter = 2, m_monitorMode = 0;
    bool m_latch = false, m_follow = false, m_shown = false, m_monitorSent = true, m_sentAll = false;
    float m_micDb = 0;
    float m_sungMedian = 57;
    double m_lastRegister = 0, m_lastTrail = 0, m_now = 0, m_connectingSince = -1;
    struct TrailPoint {
        double t = -1e9;
        float midi = -1, level = 0;
    };
    TrailPoint m_trail[kTrail];
    int m_trailW = 0;
    // Throat singing's overtones as they went by (the harmonic number in
    // `midi`), for the ladder the page draws.
    TrailPoint m_ovTrail[kTrail];
    int m_ovW = 0;
    // The talk box's synth, its pitch as it went (glides and all).
    TrailPoint m_tkTrail[kTrail];
    int m_tkW = 0;
    // The styles' own options: the gamelan's tuning, the tape's speed,
    // throat singing's style, the talk box's sub and keys.
    int m_gamTuning = Pelog, m_screwSpeed = ScrewFourth, m_throatStyle = Sygyt, m_riotPlace = scream::Field;
    bool m_talkSub = true, m_talkKeys = false;
    double m_loopSince = -1e18;
    uint32_t m_takesSeen = 0;
    int m_playingSeen = -1;
    int m_loopsMade = 0, m_notesPlayed = 0, m_effectsUsed = 0, m_chopsPlayed = 0;
    double m_sungSecs = 0, m_harmonySecs = 0, m_lastHeard = 0;
    bool m_demo = false;
    double m_autoUntil = -1;
    float m_autoPeak = 0;
    static constexpr int kChordsPart = 2; // the Chords page, third in makeParts()

    LastTake m_last;
    bool m_used[kMaxTakes] = {};
    int m_nextHandle = 1;
    std::vector<float> m_audio[kMaxTakes];
    int64_t m_zeroBefore[kMaxTakes] = {};
    std::vector<int> m_pending;
    std::vector<std::pair<double, int>> m_graveyard;
    console::Hotspots m_spots;
    Pick m_preview, m_previewLayer;
    float m_meterHold = -90;
    double m_meterHoldAt = 0;
    bool m_previewFree = false;
    double m_previewAt = -1;
    friend void paintTape(Page &, QPainter &, const PaintContext &);
};

// --- drawing -------------------------------------------------------------------

void Page::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    m_now = pc.time;
    const QColor col = pc.color;
    const StudioView &sv = *pc.studio;
    const float *d = pc.view->data;
    c::pad(p, pc, QStringLiteral("VOCAL"));
    c::variation(p, pc, QString::fromLatin1(presetName(m_preset)));
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal H = pc.pad.height();

    // No microphone: say so first, big, and why.
    const bool noMic = sv.input == StudioView::NoInput || sv.input == StudioView::InputFailed ||
                       (sv.input == StudioView::InputConnecting && m_connectingSince >= 0 &&
                        pc.time - m_connectingSince > 2.0);

    // The strips round the edge: each names itself in plain words, says
    // what its finger does along it, and while held shows where it is set.
    // Every name reads level: a strip on the side is too narrow to turn.
    const int stripPx = int(std::clamp(H * 0.028, 10.0, 14.0));
    const int notePx = std::max(8, stripPx - 3);
    // The finger on a strip, if any (the latest one, as the engine hears it).
    auto on = [&](Zone z) -> const Finger * {
        const Finger *f = nullptr;
        for (const Finger &h : m_fingers)
            if (h.id >= 0 && !h.clicker && h.zone == z)
                f = &h;
        return f;
    };
    auto box = [&](const QRectF &r, bool lit, QColor tint) {
        p.fillRect(r, c::alpha(tint, lit ? 0.2f : 0.06f));
        p.setPen(QPen(c::alpha(tint, lit ? 0.85f : 0.3f), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(1, 1, -1, -1));
    };
    const QColor fx = c::cyan(), warm = c::amber(), cold = c::violet();
    // A top or bottom strip: its name on the first line, then its cells
    // (labels spread at the places they hold), the held one lit, and a
    // marker where the finger is.
    auto band = [&](float x0, float y0, float x1, float y1, const QString &title, const QString &hint,
                    const QStringList &cells, Zone z, QColor tint) {
        const QRectF r = pc.rect(x0, y0, x1, y1);
        const Finger *f = on(z);
        box(r, f, tint);
        const qreal line = stripPx + 6;
        const QRectF head(r.left() + 8, r.top() + 4, r.width() - 16, line);
        c::text(p, head, title, c::alpha(tint, f ? 1.0f : 0.85f), stripPx, true, Qt::AlignLeft | Qt::AlignVCenter);
        c::text(p, head, hint, c::alpha(tint, f ? 0.85f : 0.55f), notePx, false, Qt::AlignRight | Qt::AlignVCenter);
        const QRectF body(r.left() + 4, head.bottom() + 2, r.width() - 8, r.bottom() - head.bottom() - 6);
        const float u = f ? clampf((f->x - x0) / (x1 - x0), 0, 0.999f) : -1;
        const int n = int(cells.size());
        for (int k = 0; k < n; ++k) {
            const QRectF cell(body.left() + body.width() * k / n, body.top(), body.width() / n, body.height());
            const bool here = u >= 0 && int(u * float(n)) == k;
            if (here)
                p.fillRect(cell.adjusted(1, 1, -1, -1), c::alpha(tint, 0.35f));
            if (k > 0) {
                p.setPen(QPen(c::alpha(tint, 0.2f), 1, Qt::DotLine));
                p.drawLine(cell.topLeft(), cell.bottomLeft());
            }
            c::text(p, cell, cells[k], here ? QColor(Qt::white) : c::alpha(tint, 0.6f), notePx, here, Qt::AlignCenter);
        }
        if (f) {
            const qreal x = body.left() + body.width() * qreal(u);
            p.setPen(QPen(Qt::white, 2));
            p.drawLine(QPointF(x, body.top()), QPointF(x, body.bottom()));
        }
    };
    band(0, kTop, 0.25f, 1, QStringLiteral("ECHO"), QStringLiteral("press: more"),
         {QStringLiteral("1/8"), QStringLiteral("1/8."), QStringLiteral("1/4")}, Echo, fx);
    band(0.25f, kTop, 0.5f, 1, QStringLiteral("REVERB"), QStringLiteral("twist: shimmer"),
         {QStringLiteral("room"), QStringLiteral("hall"), QStringLiteral("vast")}, Reverb, fx);
    band(0.5f, kTop, 0.75f, 1, QStringLiteral("RADIO"), QStringLiteral("tiny speaker"),
         {QStringLiteral("radio"), QStringLiteral("phone"), QStringLiteral("blown")}, Radio, warm);
    band(0.75f, kTop, 1, 1, QStringLiteral("CRUSH"), QStringLiteral("fewer bits"),
         {QStringLiteral("lo-fi"), QStringLiteral("grit"), QStringLiteral("8-bit")}, Crush, warm);
    // The bottom edge: a place to rest, said plainly so it isn't mistaken
    // for a control.
    {
        const QRectF r = pc.rect(0, 0, 1, kBottom);
        p.setPen(QPen(c::alpha(col, 0.18f), 1, Qt::DashLine));
        p.drawLine(r.topLeft(), r.topRight());
        c::text(p, r, QStringLiteral("REST YOUR THUMB HERE  ·  CLICK THE PAD: TAPE STOP"),
                c::alpha(col, m_button || m_tapeKey ? 1.0f : 0.45f), notePx + 1, true, Qt::AlignCenter);
    }

    // A side strip: its name at the top, then labels at the heights they
    // hold, the finger's height marked across it.
    auto column = [&](float x0, float y0, float x1, float y1, const QString &title, const QString &hint, Zone z,
                      QColor tint, const std::function<void(const QRectF &, const Finger *)> &inside) {
        const QRectF r = pc.rect(x0, y0, x1, y1);
        const Finger *f = on(z);
        box(r, f, tint);
        const QRectF head(r.left(), r.top() + 4, r.width(), stripPx + 4);
        c::text(p, head, title, c::alpha(tint, f ? 1.0f : 0.85f), stripPx, true, Qt::AlignCenter);
        const QRectF sub(r.left(), head.bottom(), r.width(), notePx + 4);
        c::text(p, sub, hint, c::alpha(tint, f ? 0.85f : 0.55f), notePx, false, Qt::AlignCenter);
        inside(QRectF(r.left(), sub.bottom() + 4, r.width(), r.bottom() - sub.bottom() - 8), f);
        if (f) {
            const qreal y = std::clamp(pc.at(0, f->y).y(), r.top() + 2, r.bottom() - 2);
            p.setPen(QPen(Qt::white, 2));
            p.drawLine(QPointF(r.left() + 3, y), QPointF(r.right() - 3, y));
        }
    };
    // Labels down a side strip at the pad heights they belong to.
    auto mark = [&](const QRectF &area, float y, const QString &label, bool lit, QColor tint) {
        const qreal py = pc.at(0, y).y();
        if (py < area.top() || py > area.bottom())
            return;
        c::text(p, QRectF(area.left(), py - notePx, area.width(), 2 * notePx), label,
                lit ? QColor(Qt::white) : c::alpha(tint, 0.55f), notePx, lit, Qt::AlignCenter);
    };
    // Filter: the cutoff, 160 Hz at the foot to 7.8 kHz at the top, as the
    // engine sets it; pressure adds the wah's resonance.
    column(0, kBottom, kLeft, kTop, QStringLiteral("FILTER"), QStringLiteral("press: wah"), Filter, cold,
           [&](const QRectF &area, const Finger *f) {
               auto hz = [](float u) { return 160.0f * std::exp2(clampf(u, 0, 1) * 5.6f); };
               auto name = [](float v) {
                   return v >= 1000 ? QStringLiteral("%1k").arg(double(v) / 1000, 0, 'f', v >= 9950 ? 0 : 1)
                                    : QString::number(int(std::lround(v / 10) * 10));
               };
               if (f) {
                   const float u = (f->y - kBottom) / (kTop - kBottom);
                   mark(area, std::clamp(f->y + 0.035f, kBottom + 0.03f, kTop - 0.12f), name(hz(u)), true, cold);
               } else {
                   mark(area, kBottom + 0.06f, QStringLiteral("dark"), false, cold);
                   mark(area, 0.5f * (kBottom + kTop), QStringLiteral("↕"), false, cold);
                   mark(area, kTop - 0.14f, QStringLiteral("bright"), false, cold);
               }
           });
    // Freeze: holds the last sound as a cloud you can sing over.
    column(kRight, kBottom, 1, kRightMid, QStringLiteral("FREEZE"), QStringLiteral("hold it"), Freeze, cold,
           [&](const QRectF &area, const Finger *f) {
               if (f && std::fabs(f->twist) > 0.05f) {
                   const int st = int(std::round(f->twist / 0.8f * 12.0f));
                   mark(area, 0.5f * (kBottom + kRightMid), QStringLiteral("%1%2 st").arg(st > 0 ? "+" : "").arg(st),
                        true, cold);
               } else {
                   mark(area, 0.5f * (kBottom + kRightMid) + 0.02f, QStringLiteral("twist:"), f, cold);
                   mark(area, 0.5f * (kBottom + kRightMid) - 0.03f, QStringLiteral("pitch"), f, cold);
               }
           });
    // Stutter: the last slice over and over on the grid, faster up the
    // strip in the three steps the engine takes.
    column(kRight, kRightMid, 1, kTop, QStringLiteral("STUTTER"), QStringLiteral("repeat"), Stutter, cold,
           [&](const QRectF &area, const Finger *f) {
               int held = -1;
               if (f) {
                   const float u = (f->y - kBottom) / (kTop - kBottom);
                   held = u > 0.83f ? 2 : u > 0.66f ? 1 : 0;
               }
               const float edges[4] = {kRightMid, kBottom + 0.66f * (kTop - kBottom), kBottom + 0.83f * (kTop - kBottom),
                                       kTop};
               for (int k = 0; k < 3; ++k) {
                   const int rate = std::clamp(m_stutter + k, 0, kStutterCount - 1);
                   const float mid = 0.5f * (edges[k] + edges[k + 1]);
                   if (k > 0) {
                       const qreal y = pc.at(0, edges[k]).y();
                       if (y > area.top()) {
                           p.setPen(QPen(c::alpha(cold, 0.25f), 1, Qt::DotLine));
                           p.drawLine(QPointF(area.left() + 3, y), QPointF(area.right() - 3, y));
                       }
                   }
                   mark(area, std::min(mid, kTop - 0.1f), QString::fromLatin1(kStutterNames[rate]),
                        held == k, cold);
               }
           });

    // The keys: a lane per harmony (what it adds to your note), or per
    // note of the key, its name at the foot, the root's lit.
    const int lanes = laneCount(sv);
    const Tuning tuning = tuningOf(sv);
    const float laneW = (kRight - kLeft) / float(lanes);
    const float sungHz = d[pub::Hz];
    const float sung = sungHz > 0 ? midiOfHz(sungHz) : -1.0f;
    const bool harmony = harmonyLanes();
    const int labelPx = int(std::clamp(H * 0.026, 9.0, 14.0));
    // In two styles a lane isn't a harmony, and is named for what it plays.
    const Harmony hl = harmonyOf(sv);
    const bool ownLanes = m_preset == Gamelan || m_preset == Screwed;
    if (harmony) {
        for (int k = 0; k < lanes; ++k) {
            const float x0 = kLeft + laneW * float(k);
            const QRectF r = pc.rect(x0, kTapeTop, x0 + laneW, kTop);
            if (k % 2 == 0)
                p.fillRect(r, c::alpha(col, 0.035f));
            p.setPen(QPen(c::alpha(col, 0.16f), 1));
            p.drawLine(pc.at(x0, kTapeTop), pc.at(x0, kTop));
            c::text(p, pc.rect(x0, kTapeTop, x0 + laneW, kTapeTop + 0.05f),
                    ownLanes ? laneName(kIntervals[k]) : QString::fromUtf8(kIntervalNames[k]),
                    c::alpha(col, 0.75f), labelPx, true, Qt::AlignCenter);
        }
        if (m_preset == Gamelan)
            c::text(p, pc.rect(kLeft, kTapeTop + 0.05f, kRight, kTapeTop + 0.09f),
                    d[pub::GamStruck] > 0
                        ? QStringLiteral("KOTEKAN RESOLVES ONTO %1 TONE %2: THE NOTE YOU SING")
                              .arg(QString::fromLatin1(gamTuningName(m_gamTuning)), gamToneName(hl, d[pub::GamStruck]))
                        : QStringLiteral("KOTEKAN: EACH LANE A FIGURE, LANDING ON THE NOTE YOU SING"),
                    c::alpha(col, 0.6f), labelPx - 1, true, Qt::AlignCenter);
        if (m_preset == Screwed)
            c::text(p, pc.rect(kLeft, kTapeTop + 0.05f, kRight, kTapeTop + 0.09f),
                    QStringLiteral("HOLD: CUT TO THE SECOND DECK, A BEAT (½ ON THE RIGHT) BEHIND"),
                    c::alpha(col, 0.6f), labelPx - 1, true, Qt::AlignCenter);
        if (m_preset == Riot) {
            // Where the scream is pitched and where it is heard from.
            const int oct = int(std::lround(d[pub::RiotOctave]));
            const QString up = oct == 0   ? QStringLiteral("AT YOUR PITCH")
                               : oct > 0  ? QStringLiteral("%1 OCTAVE%2 UP").arg(oct).arg(oct > 1 ? "S" : "")
                                          : QStringLiteral("AN OCTAVE DOWN");
            static const char *where[] = {"ACROSS A FIELD, 3 M OFF", "IN A ROOM, 3 M OFF", "CLOSE"};
            c::text(p, pc.rect(kLeft, kTapeTop + 0.05f, kRight, kTapeTop + 0.09f),
                    QStringLiteral("SCREAMED %1 · %2 · HOLD A LANE: A GANG SHOUT AT ITS HARMONY")
                        .arg(up, QString::fromLatin1(where[std::clamp(m_riotPlace, 0, 2)])),
                    c::alpha(col, 0.6f), labelPx - 1, true, Qt::AlignCenter);
        }
        // Your own note sits between the thirds.
        const float ux = kLeft + laneW * 3.0f;
        p.setPen(QPen(c::alpha(col, 0.5f), 1, Qt::DashLine));
        p.drawLine(pc.at(ux, kTapeTop + 0.05f), pc.at(ux, kTop));
        // Until you sing; then your note is named at the line's foot.
        if (sung <= 0)
            c::tag(p, QPointF(pc.at(ux, kTop).x(), pc.at(0, kTop - 0.035f).y()), QStringLiteral("YOU"), col, 10, true);
    }
    for (int k = 0; k < lanes && !harmony; ++k) {
        const double midi = laneMidi(sv, k);
        const int pc12 = ((int(std::lround(midi)) % 12) + 12) % 12;
        const bool root = pc12 == tuning.root();
        const float x0 = kLeft + laneW * float(k);
        const QRectF r = pc.rect(x0, kTapeTop, x0 + laneW, kTop);
        if (root)
            p.fillRect(r, c::alpha(col, 0.07f));
        if (sung > 0 && std::fabs(sung - float(midi)) < 0.5f)
            p.fillRect(r, c::alpha(col, 0.16f));
        p.setPen(QPen(c::alpha(col, 0.14f), 1));
        p.drawLine(pc.at(x0, kTapeTop), pc.at(x0, kTop));
        const QString name = QString::fromUtf8(Tuning::noteName(pc12));
        c::text(p, pc.rect(x0, kTapeTop, x0 + laneW, kTapeTop + 0.045f), name,
                c::alpha(col, root ? 0.9f : 0.5f), labelPx, root, Qt::AlignCenter);
    }
    // The throat, up the keys: your own on the middle line, darker and
    // bigger below it, brighter and smaller above.
    {
        const qreal y = pc.at(0, kThroatMid).y();
        p.setPen(QPen(c::alpha(col, 0.22f), 1, Qt::DotLine));
        p.drawLine(QPointF(pc.at(kLeft, 0).x(), y), QPointF(pc.at(kRight, 0).x(), y));
        c::text(p, pc.rect(kLeft + 0.008f, kThroatMid, kLeft + 0.3f, kThroatMid + 0.045f),
                QStringLiteral("YOUR OWN THROAT"), c::alpha(col, 0.5f), labelPx - 2, true);
        c::text(p, pc.rect(kLeft + 0.008f, kTop - 0.055f, kLeft + 0.3f, kTop - 0.01f), QStringLiteral("↑ BRIGHTER"),
                c::alpha(col, 0.55f), labelPx - 1, true);
        c::text(p, pc.rect(kLeft + 0.008f, kTapeTop + 0.12f, kLeft + 0.3f, kTapeTop + 0.165f),
                QStringLiteral("↓ DARKER"), c::alpha(col, 0.55f), labelPx - 1, true);
    }

    // Throat singing: which overtone of the drone is singing, and the
    // ones before it, as a ladder (the drone's harmonics 6 to 18, up) with
    // the last four seconds across it, so the melody the overtones make
    // can be seen as it is heard.
    if (m_preset == Throat) {
        constexpr double kSpan = 4.0;
        const QRectF box = pc.rect(kRight - 0.22f, kTapeTop + 0.08f, kRight - 0.01f, kTop - 0.08f);
        p.fillRect(box, QColor(0, 0, 0, 120));
        const float now = d[pub::Overtone];
        // The ladder spans the overtones lately sung, a few either side.
        float lo = now > 0 ? now : 12, hi = lo;
        for (const TrailPoint &tp : m_ovTrail)
            if (pc.time - tp.t <= kSpan && tp.midi > 0) {
                lo = std::min(lo, tp.midi);
                hi = std::max(hi, tp.midi);
            }
        const int kLo = std::max(2, int(std::floor(lo)) - 2);
        const int kHi = std::max(kLo + 10, int(std::ceil(hi)) + 2);
        auto yOf = [&](float k) { return box.bottom() - (k - kLo + 0.5) / (kHi - kLo + 1) * box.height(); };
        for (int k = kLo; k <= kHi; ++k) {
            const bool lit = now > 0 && std::lround(now) == k;
            p.setPen(QPen(c::alpha(col, lit ? 0.6f : 0.12f), 1));
            p.drawLine(QPointF(box.left() + 20, yOf(float(k))), QPointF(box.right() - 2, yOf(float(k))));
            if (k % 2 == 0 || lit)
                c::text(p, QRectF(box.left() + 2, yOf(float(k)) - 6, 18, 12), QString::number(k),
                        c::alpha(col, lit ? 1.0f : 0.45f), 8, lit);
        }
        p.setPen(Qt::NoPen);
        for (int j = 0; j < kTrail; ++j) {
            const TrailPoint &tp = m_ovTrail[j];
            const double age = pc.time - tp.t;
            if (age < 0 || age > kSpan || tp.midi <= 0)
                continue;
            const double x = box.right() - 4 - age / kSpan * (box.width() - 26);
            p.setBrush(c::alpha(col, float(0.85 - 0.6 * age / kSpan)));
            p.drawEllipse(QPointF(x, yOf(tp.midi)), 2.2, 2.2);
        }
        const QString what = now > 0 ? QStringLiteral("%3 · OVERTONE %1 · %2")
                                           .arg(std::lround(now))
                                           .arg(QString::fromUtf8(Tuning::noteName(
                                               ((int(std::lround(d[pub::OvertoneMidi])) % 12) + 12) % 12)) +
                                                QString::number(int(std::lround(d[pub::OvertoneMidi])) / 12 - 1),
                                                QString::fromLatin1(throatStyleName(m_throatStyle)))
                                     : QStringLiteral("%1 · HOLD A DRONE").arg(QString::fromLatin1(throatStyleName(m_throatStyle)));
        c::text(p, QRectF(box.left(), box.top() - 16, box.width(), 14), what,
                c::alpha(col, now > 0 ? 1.0f : 0.5f), 10, true, Qt::AlignRight);
    }

    // The other four styles' pictures, in the same place as the ladder: what
    // each one is doing that the ear alone may not tell a newcomer.
    auto midiName = [](float midi) {
        const int m = int(std::lround(midi));
        return QString::fromUtf8(Tuning::noteName(((m % 12) + 12) % 12)) + QString::number(m / 12 - 1);
    };
    const QRectF styleBox = pc.rect(kRight - 0.22f, kTapeTop + 0.08f, kRight - 0.01f, kTop - 0.08f);
    // The talk box: the synth's pitch over the last three seconds, the
    // glides and scoops drawn as it slid, against the key's notes, with the
    // note it is heading for lit.
    if (m_preset == Talk) {
        constexpr double kSpan = 3.0;
        const QRectF box = styleBox;
        p.fillRect(box, QColor(0, 0, 0, 120));
        const float target = d[pub::TalkTarget], now = d[pub::TalkMidi];
        const bool sounding = d[pub::TalkGate] > 0.05f;
        const float mid = sounding ? target : 57.0f;
        const float lo = mid - 7.5f, hi = mid + 7.5f;
        auto yOf = [&](float m) { return box.bottom() - (m - lo) / (hi - lo) * box.height(); };
        const Harmony hk = harmonyOf(sv);
        for (int m = int(std::ceil(lo)); m <= int(std::floor(hi)); ++m) {
            if (std::fabs(snapMidi(hk, m, false) - m) > 0.01)
                continue;
            const bool lit = sounding && std::lround(target) == m;
            p.setPen(QPen(c::alpha(col, lit ? 0.7f : 0.12f), lit ? 2 : 1));
            p.drawLine(QPointF(box.left() + 26, yOf(float(m))), QPointF(box.right() - 2, yOf(float(m))));
            c::text(p, QRectF(box.left() + 2, yOf(float(m)) - 6, 24, 12), midiName(float(m)),
                    c::alpha(col, lit ? 1.0f : 0.4f), 8, lit);
        }
        QPainterPath path;
        bool started = false;
        for (int k = 0; k < kTrail; ++k) {
            const TrailPoint &tp = m_tkTrail[(m_tkW + k) % kTrail];
            const double age = pc.time - tp.t;
            if (age < 0 || age > kSpan || tp.midi <= 0 || tp.midi < lo - 3 || tp.midi > hi + 3) {
                started = false;
                continue;
            }
            const QPointF at(box.right() - 4 - age / kSpan * (box.width() - 30), yOf(clampf(tp.midi, lo, hi)));
            if (!started)
                path.moveTo(at);
            else
                path.lineTo(at);
            started = true;
        }
        p.setPen(QPen(c::alpha(c::green(), 0.85f), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        if (sounding) {
            p.setPen(Qt::NoPen);
            p.setBrush(c::green());
            p.drawEllipse(QPointF(box.right() - 4, yOf(clampf(now, lo, hi))), 4, 4);
        }
        const QString what = !sounding ? (m_talkKeys ? QStringLiteral("KEYS · HOLD A LANE") : QStringLiteral("SYNTH · SING"))
                             : std::fabs(now - target) > 0.25f
                                 ? QStringLiteral("GLIDE %1 → %2").arg(midiName(now), midiName(target))
                                 : QStringLiteral("%1 %2").arg(m_talkKeys ? QStringLiteral("KEYS") : QStringLiteral("SYNTH"),
                                                                midiName(target));
        c::text(p, QRectF(box.left(), box.top() - 16, box.width(), 14), what, c::alpha(col, sounding ? 1.0f : 0.5f),
                10, true, Qt::AlignRight);        // KEYS: how open the synth's filter is (the key's pressure and the
        // contour of a fresh key), a bar along the bottom.
        if (m_talkKeys) {
            const QRectF bar(box.left() + 30, box.bottom() - 10, box.width() - 36, 5);
            p.fillRect(bar, c::alpha(col, 0.15f));
            p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * clampf(d[pub::TalkOpen], 0, 1), bar.height()),
                       c::alpha(c::green(), 0.85f));
            c::text(p, QRectF(box.left() + 2, bar.top() - 4, 28, 12), QStringLiteral("PRESS"), c::alpha(col, 0.6f), 8,
                    true);
        }
    }
    // The sitar: the thirteen sympathetic strings, each lit as it rings (so
    // the raga's notes the playing wakes can be seen), and the main string
    // over the bridge, glowing as far as it buzzes on the jawari.
    if (m_preset == Sitar) {
        const QRectF box = styleBox;
        p.fillRect(box, QColor(0, 0, 0, 120));
        const Harmony hk = harmonyOf(sv);
        const QRectF strings(box.left() + 6, box.top() + 6, box.width() - 12, box.height() * 0.68);
        for (int k = 0; k < 13; ++k) {
            const float lvl = clampf(d[pub::Taraf + k] * 12.0f, 0, 1);
            const qreal x = strings.left() + strings.width() * (k + 0.5) / 13.0;
            p.setPen(QPen(c::alpha(c::amber(), 0.15f + 0.85f * lvl), 1.0 + 2.5 * lvl));
            p.drawLine(QPointF(x, strings.top()), QPointF(x, strings.bottom()));
            if (k % 2 == 0 || lvl > 0.5f) {
                const int m = int(std::lround(hk.midi(k, 1)));
                c::text(p, QRectF(x - 9, strings.bottom() + 1, 18, 11),
                        QString::fromUtf8(Tuning::noteName(((m % 12) + 12) % 12)), c::alpha(col, 0.35f + 0.65f * lvl),
                        8, lvl > 0.5f, Qt::AlignCenter);
            }
        }
        // The main string across the bottom, swinging by its level, and the
        // bridge under its left end, lit by the buzz.
        const float buzz = clampf(d[pub::SitarBuzz] / 0.45f, 0, 1), lvl = clampf(d[pub::SitarLevel] * 4.0f, 0, 1);
        const qreal y = box.bottom() - box.height() * 0.12;
        QPainterPath str;
        for (int i = 0; i <= 40; ++i) {
            const qreal u = i / 40.0;
            const qreal x = box.left() + 8 + u * (box.width() - 16);
            const qreal amp = 7.0 * lvl * std::sin(kPi * u) * std::sin(pc.time * 40.0 + u * 3.0);
            if (i == 0)
                str.moveTo(x, y + amp);
            else
                str.lineTo(x, y + amp);
        }
        p.setPen(QPen(c::alpha(col, 0.4f + 0.6f * lvl), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(str);
        p.fillRect(QRectF(box.left() + 4, y - 3, 10, 6), c::alpha(c::amber(), 0.25f + 0.75f * buzz));
        c::text(p, QRectF(box.left(), box.top() - 16, box.width(), 14),
                buzz > 0.05f ? QStringLiteral("JAWARI %1% · TARAFS").arg(int(std::lround(buzz * 100)))
                             : QStringLiteral("TARAFS · SING TO PLUCK"),
                c::alpha(col, buzz > 0.05f ? 1.0f : 0.5f), 10, true, Qt::AlignRight);
    }
    // The gamelan: the gong's cycle of sixteen beats as a wheel, the gong
    // ageng at the top, the kenong on the bars and the kempul on the half
    // bars, the hand at the beat now, and how many beats until the gong.
    if (m_preset == Gamelan) {
        const QRectF box = styleBox;
        p.fillRect(box, QColor(0, 0, 0, 120));
        const qreal r = std::min(box.width(), box.height()) * 0.36;
        const QPointF o(box.center().x(), box.center().y() + 4);
        const float cyc = d[pub::GamCycle];
        for (int b = 0; b < 16; ++b) {
            const qreal a = kTwoPi * b / 16.0 - kPi / 2;
            const QPointF at(o.x() + r * std::cos(a), o.y() + r * std::sin(a));
            const bool gong = b == 0, kenong = b % 4 == 0, kempul = b % 4 == 2;
            const bool lit = int(std::floor(cyc)) == b;
            const qreal size = gong ? 9 : kenong ? 6 : kempul ? 4.5 : 2;
            p.setPen(Qt::NoPen);
            p.setBrush(lit ? QColor(Qt::white) : c::alpha(gong ? c::amber() : col, gong ? 0.8f : kenong || kempul ? 0.5f : 0.25f));
            p.drawEllipse(at, size, size);
            if (gong || kenong || kempul) {
                const QPointF lab(o.x() + (r + 15) * std::cos(a), o.y() + (r + 15) * std::sin(a));
                c::text(p, QRectF(lab.x() - 14, lab.y() - 6, 28, 12),
                        gong ? QStringLiteral("GONG") : kenong ? QStringLiteral("N") : QStringLiteral("P"),
                        c::alpha(col, 0.6f), 8, gong, Qt::AlignCenter);
            }
        }
        const qreal a = kTwoPi * cyc / 16.0 - kPi / 2;
        p.setPen(QPen(c::alpha(Qt::white, 0.8f), 2));
        p.drawLine(o, QPointF(o.x() + r * 0.8 * std::cos(a), o.y() + r * 0.8 * std::sin(a)));
        // The last bar struck, named by its degree, fading as it rings.
        const float age = d[pub::GamStruckAge];
        if (d[pub::GamStruck] > 0 && age < 2.0f) {
            const QString deg = gamToneName(harmonyOf(sv), d[pub::GamStruck]);
            c::text(p, QRectF(o.x() - 20, o.y() - 12, 40, 24), deg, c::alpha(c::amber(), 1.0f - age / 2.0f), 18,
                    true, Qt::AlignCenter);
        }
        const int toGong = 16 - int(std::floor(cyc));
        c::text(p, QRectF(box.left(), box.top() - 16, box.width(), 14),
                toGong >= 16 ? QStringLiteral("GONG AGENG") : QStringLiteral("GONG IN %1 BEAT%2").arg(toGong).arg(toGong == 1 ? "" : "S"),
                c::alpha(col, toGong >= 16 || toGong <= 2 ? 1.0f : 0.7f), 10, true, Qt::AlignRight);
    }
    // Screwed: the tape. The live voice at the right, the tape head behind
    // it by how far it has fallen, against the beats and bar lines behind
    // you (the track is the two bars it may fall), filling towards the
    // mark where it drops at the bar. A drop arranged is shown where it
    // will land: on the beat (after a rest) or the bar line, counting down.
    // Deck B, when the crossfader is over to it, lights the track's lower
    // half, a beat (or half) behind the head.
    if (m_preset == Screwed) {
        const QRectF box = styleBox;
        p.fillRect(box, QColor(0, 0, 0, 120));
        const float lag = d[pub::ScrewLag], limit = std::max(0.1f, d[pub::ScrewLimit]);
        const bool spin = d[pub::ScrewSpin] > 0.5f;
        const float drop = d[pub::ScrewDrop], deckB = d[pub::ScrewDeckB];
        const int px = int(std::clamp(box.height() * 0.06, 11.0, 14.0));
        const QRectF track(box.left() + 10, box.center().y() - 18, box.width() - 20, 36);
        p.setPen(QPen(c::alpha(col, 0.45f), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(track);
        // The beats behind you: the limit is two bars, eight beats.
        for (int b = 1; b < 8; ++b) {
            const qreal x = track.right() - track.width() * b / 8.0;
            p.setPen(QPen(c::alpha(col, b % 4 == 0 ? 0.6f : 0.22f), b % 4 == 0 ? 2 : 1));
            p.drawLine(QPointF(x, track.top() + 2), QPointF(x, track.bottom() - 2));
        }
        const float u = clampf(lag / limit, 0, 1);
        const qreal live = track.right(), head = track.right() - u * track.width();
        p.fillRect(QRectF(head, track.top() + 1, live - head, track.height() * 0.5 - 1),
                   c::alpha(u > 0.8f ? c::red() : c::violet(), 0.3f + 0.4f * u));
        if (deckB > 0.05f) {
            // (Deck B a beat behind the head: an eighth of the track.)
            const qreal bx = std::max(track.left(), head - track.width() / 8.0);
            p.fillRect(QRectF(bx, track.center().y(), head - bx + 2, track.height() * 0.5 - 1),
                       c::alpha(c::amber(), 0.35f + 0.55f * deckB));
        }
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(live, track.top() - 8), QPointF(live, track.bottom() + 8));
        p.setPen(QPen(c::violet(), 3));
        p.drawLine(QPointF(head, track.top() - 8), QPointF(head, track.bottom() + 8));
        c::text(p, QRectF(live - 60, track.bottom() + 8, 60, px + 2), QStringLiteral("YOU"), c::alpha(col, 0.95f), px,
                true, Qt::AlignRight);
        c::text(p, QRectF(head - 30, track.top() - px - 10, 60, px + 2), QStringLiteral("TAPE"), c::violet(), px, true,
                Qt::AlignCenter);
        p.setPen(QPen(c::alpha(c::red(), 0.85f), 2, Qt::DashLine));
        p.drawLine(QPointF(track.left(), track.top() - 4), QPointF(track.left(), track.bottom() + 4));
        c::text(p, QRectF(track.left(), track.bottom() + 8, track.width() * 0.6, px + 2), QStringLiteral("2 BARS: DROP"),
                c::alpha(c::red(), 0.95f), px, true, Qt::AlignLeft);
        // The drop arranged, and how soon.
        QString status;
        QColor statusCol = c::alpha(col, 0.95f);
        if (spin) {
            status = QStringLiteral("SPINNING BACK");
            statusCol = c::amber();
        } else if (drop >= 0) {
            status = QStringLiteral("DROP ON THE %1 IN %2 s")
                         .arg(d[pub::ScrewDropBar] > 0.5f ? QStringLiteral("BAR") : QStringLiteral("BEAT"))
                         .arg(double(drop), 0, 'f', 1);
            statusCol = c::green();
        } else if (deckB > 0.5f) {
            status = QStringLiteral("DECK B");
            statusCol = c::amber();
        } else {
            status = QStringLiteral("TAPE %1 s BEHIND").arg(double(lag), 0, 'f', 1);
        }
        c::text(p, QRectF(box.left() + 6, box.top() + 6, box.width() - 12, px + 4), status, statusCol, px + 1, true,
                Qt::AlignCenter);
        c::text(p, QRectF(box.left() + 6, box.bottom() - px - 10, box.width() - 12, px + 4),
                QStringLiteral("%1 SEMITONES DOWN").arg(double(-d[pub::ScrewSemis]), 0, 'f', 1),
                c::alpha(col, 0.9f), px, true, Qt::AlignCenter);
        c::text(p, QRectF(box.left(), box.top() - 16, box.width(), 14),
                spin ? QStringLiteral("SPINNING BACK") : QStringLiteral("SCREWED TAPE"),
                spin ? c::amber() : c::alpha(col, 0.8f), 10, true, Qt::AlignRight);
    }

    // The note sung: a trail rising up the keys, at its pitch across them.
    const Harmony hv = harmonyOf(sv);
    auto xOfMidi = [&](float midi) -> float {
        // Harmony lanes: your note on the line between the thirds, off it
        // by how far from the key's note you are (a lane is a semitone).
        if (harmony) {
            const double near = hv.midi(degreeNear(hv, midi), 0);
            return kLeft + laneW * (3.0f + clampf(float(midi - near), -0.5f, 0.5f));
        }
        // Between lanes, by pitch: the lanes' own midi values interpolated.
        double lo = laneMidi(sv, 0), hi = laneMidi(sv, lanes - 1);
        if (midi <= lo)
            return kLeft + laneW * 0.5f - float(lo - midi) * laneW * 0.5f;
        for (int k = 0; k + 1 < lanes; ++k) {
            const double a = laneMidi(sv, k), b = laneMidi(sv, k + 1);
            if (midi >= a && midi <= b && b > a)
                return kLeft + laneW * (float(k) + 0.5f + float((midi - a) / (b - a)));
        }
        return kLeft + laneW * (float(lanes) - 0.5f) + float(midi - hi) * laneW * 0.5f;
    };
    // In harmony lanes the trails start above the lanes' note names.
    const float trailFoot = kTapeTop + (harmony ? 0.12f : 0.06f);
    {
        const double span = 2.5; // seconds up the keys
        QPainterPath path;
        bool started = false;
        for (int k = 0; k < kTrail; ++k) {
            const TrailPoint &tp = m_trail[(m_trailW + k) % kTrail];
            const double age = pc.time - tp.t;
            if (age < 0 || age > span || tp.midi <= 0) {
                started = false;
                continue;
            }
            const float x = clampf(xOfMidi(tp.midi), kLeft, kRight);
            const float y = trailFoot + float(age / span) * (kTop - trailFoot);
            const QPointF at = pc.at(x, y);
            if (!started)
                path.moveTo(at);
            else
                path.lineTo(at);
            started = true;
        }
        p.setPen(QPen(c::alpha(col, 0.55f), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        if (sung > 0) {
            const QPointF at = pc.at(clampf(xOfMidi(sung), kLeft, kRight), trailFoot);
            p.setPen(Qt::NoPen);
            p.setBrush(c::alpha(col, 0.9f));
            p.drawEllipse(at, 6, 6);
            const double target = d[pub::LeadMidi];
            if (retuned(m_preset) && target > 0) {
                const QPointF to = pc.at(clampf(xOfMidi(float(target)), kLeft, kRight), trailFoot);
                p.setPen(QPen(c::alpha(c::green(), 0.9f), 2));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(to, 9, 9);
            }
        }
    }
    // Harmony lanes: the choir you're making, seen. Each lane says the
    // note it would sing over yours right now; a held one lights, and
    // its note rises up the lane beside your own trail, named where it
    // changes, so a third above a tune reads as the tune it is.
    if (harmony && !ownLanes) {
        auto noteName = [](double midi) {
            const int m = int(std::lround(midi));
            return QString::fromUtf8(Tuning::noteName(((m % 12) + 12) % 12));
        };
        const double ref = d[pub::LeadMidi] > 0 ? double(d[pub::LeadMidi]) : double(sung);
        bool held[kIntervalCount] = {};
        for (const Finger &h : m_fingers)
            if (h.id >= 0 && !h.clicker && h.zone == Keys && h.mode == HarmonyNote)
                for (int k = 0; k < kIntervalCount; ++k)
                    held[k] |= kIntervals[k] == h.value;
        const double span = 2.5;
        for (int k = 0; k < kIntervalCount; ++k) {
            const float cx = kLeft + laneW * (float(k) + 0.5f);
            const float x0 = kLeft + laneW * float(k);
            if (ref > 0) {
                const double m = noteMidi(hv, kIntervals[k], HarmonyNote, ref, m_follow);
                // Latched voices hold their lanes too: a voice sounding at
                // this lane's note.
                for (int v = 0; v < kMaxVoices && !held[k]; ++v)
                    held[k] = d[pub::Voices + 3 * v] > 0.5f && std::fabs(d[pub::Voices + 3 * v + 1] - m) < 0.5;
                c::text(p, pc.rect(x0, kTapeTop + 0.05f, x0 + laneW, kTapeTop + 0.11f), noteName(m),
                        held[k] ? QColor(Qt::white) : c::alpha(col, 0.45f), held[k] ? labelPx + 4 : labelPx,
                        held[k], Qt::AlignCenter);
            }
            if (!held[k])
                continue;
            p.fillRect(pc.rect(x0, kTapeTop, x0 + laneW, kTop), c::alpha(col, 0.09f));
            // Its trail: the note it sang at each moment of yours.
            QPainterPath path;
            bool started = false;
            int last = INT_MIN;
            qreal labelled = -1e9; // where the last name went: names keep their distance
            int named = INT_MIN;   // and which: a note flickering back isn't named again
            for (int t = 0; t < kTrail; ++t) {
                const TrailPoint &tp = m_trail[(m_trailW + t) % kTrail];
                const double age = pc.time - tp.t;
                if (age < 0 || age > span || tp.midi <= 0) {
                    started = false;
                    continue;
                }
                const double m = noteMidi(hv, kIntervals[k], HarmonyNote, tp.midi, m_follow);
                const float y = trailFoot + float(age / span) * (kTop - trailFoot);
                const QPointF at = pc.at(cx, y);
                if (!started)
                    path.moveTo(at);
                else
                    path.lineTo(at);
                started = true;
                const int note = int(std::lround(m));
                if (last == INT_MIN)
                    named = note;
                if (note != last && note != named && age > 0.15 && std::fabs(at.y() - labelled) > 3.0 * notePx &&
                    (labelled = at.y(), named = note, true))
                    c::text(p, QRectF(at.x() + 6, at.y() - notePx, laneW * pc.pad.width() / 2, 2 * notePx),
                            noteName(m), c::alpha(col, float(0.85 * (1 - age / span))), notePx, true,
                            Qt::AlignLeft | Qt::AlignVCenter);
                last = note;
            }
            p.setPen(QPen(c::alpha(col, 0.8f), 3, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        // Your own note, named on its line.
        if (sung > 0)
            c::tag(p, pc.at(kLeft + laneW * 3.0f, kTapeTop + 0.08f), QStringLiteral("YOU ") + noteName(sung), col,
                   notePx + 1, true);
    }

    // The voices the fingers hold (in harmony lanes, the fingers show them).
    for (int k = 0; k < kMaxVoices && !harmony; ++k) {
        if (d[pub::Voices + 3 * k] < 0.5f)
            continue;
        const float midi = d[pub::Voices + 3 * k + 1];
        const float x = clampf(xOfMidi(midi), kLeft, kRight);
        p.setPen(QPen(c::alpha(col, 0.35f + 0.6f * clampf(d[pub::Voices + 3 * k + 2], 0, 1)), 3));
        p.drawLine(pc.at(x, kTapeTop + 0.05f), pc.at(x, kTop));
    }

    paintTape(*this, p, pc);

    // The fingers.
    for (const Finger &h : m_fingers) {
        if (h.id < 0 || h.key)
            continue;
        Touch t;
        t.id = h.id;
        t.x = h.x;
        t.y = h.y;
        t.pressure = h.p;
        t.size = 1;
        QString label;
        if (h.clicker)
            label = QStringLiteral("TAPE");
        else if (h.zone == Keys && h.mode == HarmonyNote) {
            for (int k = 0; k < kIntervalCount; ++k)
                if (kIntervals[k] == h.value)
                    label = QString::fromUtf8(kIntervalNames[k]);
            const double ref = d[pub::LeadMidi] > 0 ? double(d[pub::LeadMidi]) : double(sung);
            if (ref > 0) {
                const int m = int(std::lround(noteMidi(hv, h.value, HarmonyNote, ref, m_follow)));
                label += QStringLiteral(" ") + QString::fromUtf8(Tuning::noteName(((m % 12) + 12) % 12));
            }
            // In two styles a finger isn't a harmony: say what it does.
            if (m_preset == Gamelan)
                label = laneName(h.value);
            else if (m_preset == Screwed)
                label = h.value <= -7 ? QStringLiteral("BACKSPIN")
                        : h.value >= 5 ? QStringLiteral("DECK B · ½ BEAT BEHIND")
                                       : QStringLiteral("DECK B · A BEAT BEHIND");
        } else if (h.zone == Keys) {
            const int m = int(std::lround(noteMidi(hv, h.value, h.mode)));
            label = QString::fromUtf8(Tuning::noteName(((m % 12) + 12) % 12));
        }
        // A finger on a strip goes unnamed: the strip it lit says it.
        // A resting thumb is drawn faint: it plays nothing.
        if (h.zone == Rest)
            c::finger(p, pc, t, c::alpha(col, 0.3f), {}, 0);
        else
            c::finger(p, pc, t, h.zone == Keys ? col : c::cyan(), label, clampf(h.p, 0, 1));
    }

    // Tape stop: the pad itself slows.
    if (d[pub::Tape] < 0.999f) {
        p.fillRect(pc.pad, c::alpha(QColor(0, 0, 0), 0.4f * (1 - d[pub::Tape])));
        c::text(p, pc.rect(0, 0.42f, 1, 0.58f), QStringLiteral("TAPE STOP"), c::alpha(c::red(), 0.9f), int(H * 0.08), true,
                Qt::AlignCenter);
    }
    if (d[pub::Feedback] > 0.5f)
        c::tag(p, QPointF(pc.pad.center().x(), pc.at(0, 0.8f).y()),
               QStringLiteral("FEEDBACK HEARD: VOICE DUCKED · USE HEADPHONES"), c::red(), 13, true);
    if (noMic && !m_demo) {
        const QRectF box = pc.rect(0.2f, 0.45f, 0.8f, 0.72f);
        p.fillRect(box, QColor(0, 0, 0, 220));
        p.setPen(QPen(c::red(), 2));
        p.drawRect(box);
        c::text(p, QRectF(box.left(), box.top() + 8, box.width(), box.height() * 0.4), QStringLiteral("NO MICROPHONE"),
                c::red(), int(H * 0.05), true, Qt::AlignCenter);
        const QString why = sv.input == StudioView::InputFailed
                                ? QStringLiteral("PipeWire refused the microphone stream: check the input device")
                            : sv.input == StudioView::InputConnecting
                                ? QStringLiteral("Waiting for an input: plug a mic in, or pick one in the sound settings")
                                : QStringLiteral("This session has no input (offline, or started without one)");
        c::text(p, QRectF(box.left() + 10, box.center().y() - 6, box.width() - 20, box.height() * 0.25), why,
                c::alpha(QColor(255, 255, 255), 0.85f), 12, false, Qt::AlignCenter);
        c::text(p, QRectF(box.left() + 10, box.center().y() + box.height() * 0.2, box.width() - 20, box.height() * 0.25),
                QStringLiteral("V: A DEMO SINGER, TO PLAY AND LOOP MEANWHILE"), c::amber(), 12, true, Qt::AlignCenter);
    } else if (m_demo) {
        c::tag(p, QPointF(pc.pad.center().x(), pc.at(0, kTop - 0.035f).y() + 22), QStringLiteral("DEMO SINGER · V"),
               c::amber(), 10, true);
    }
    if (pc.view->recording >= 0)
        c::tag(p, QPointF(pc.pad.left() + 12, pc.at(0, kTop).y() + 14),
               QStringLiteral("VOCAL LOOP INTO SLOT %1 AT THE BAR · SPACE CALLS IT OFF").arg(pc.view->recording + 1),
               c::red(), 11);
    c::beats(p, pc, col);

    // Under the pad: the clips and the voice; then Space's two ways, the
    // monitor, latch, follow, the microphone's level and the latency.
    m_spots.clear();
    c::clips(p, c::bandRow(pc, 0, 0, 0.42), *pc.view, col, pc.time, &m_spots);
    QStringList names;
    for (int k = 0; k < PresetCount; ++k)
        names << QString::fromLatin1(presetName(k));
    // A style with an option of its own has its chip at the row's end.
    const bool opt = hasOption(m_preset);
    c::chips(p, pc, c::bandRow(pc, 0, 0.44, opt ? 0.885 : 1), names, m_preset, &m_spots, SpotPreset);
    if (opt)
        c::chip(p, pc, c::bandRow(pc, 0, 0.89, 1).adjusted(0, 0, -4, 0), optionLabel(), true, &m_spots, SpotStyleOpt);
    const QRectF row = c::bandRow(pc, 1, 0, 1);
    const double w = row.width();
    auto at = [&](double x0, double x1) { return QRectF(row.left() + w * x0, row.top(), w * (x1 - x0) - 4, row.height()); };
    c::chip(p, pc, at(0, 0.11), QStringLiteral("NEW  SPACE"), false, &m_spots, SpotNew);
    c::chip(p, pc, at(0.11, 0.23), QStringLiteral("LAYER  ⇧SPACE"), false, &m_spots, SpotLayer);
    static const char *mon[] = {"MONITOR PAGE  M", "MONITOR ALL  M", "MONITOR OFF  M"};
    c::chip(p, pc, at(0.23, 0.36), QString::fromLatin1(mon[m_monitorMode]), m_monitorMode != 2, &m_spots, SpotMonitor);
    c::chip(p, pc, at(0.36, 0.44), QStringLiteral("LATCH  ↵"), m_latch, &m_spots, SpotLatch);
    c::chip(p, pc, at(0.44, 0.53), QStringLiteral("FOLLOW  §"), m_follow, &m_spots, SpotFollow);
    c::chip(p, pc, at(0.53, 0.60), QStringLiteral("DEMO  V"), m_demo, &m_spots, SpotDemo);
    c::chip(p, pc, at(0.60, 0.635), QStringLiteral("−"), false, &m_spots, SpotGainDown);
    {
        // The microphone's level: a meter, its peak held a moment, red
        // only near the top; AUTO sets it from three seconds of singing.
        const QRectF m = at(0.635, 0.76);
        const float pk = d[pub::InPeak];
        const float db = pk > 1e-6f ? 20.0f * std::log10(pk) : -90.0f;
        if (db > m_meterHold || pc.time - m_meterHoldAt > 1.5) {
            m_meterHold = db;
            m_meterHoldAt = pc.time;
        }
        const QString mic = m_micDb > 0 ? QStringLiteral("+%1").arg(double(m_micDb), 0, 'f', 0)
                                         : QString::number(double(m_micDb), 'f', 0);
        // A soft voice: how much the lift is adding, in place of the peak
        // (the gauge below still shows that).
        const float lift = d[pub::Lift];
        c::text(p, QRectF(m.left(), m.top(), m.width(), 12),
                lift >= 1.0f ? QStringLiteral("MIC %1 dB · LIFT +%2").arg(mic).arg(int(std::lround(lift)))
                             : QStringLiteral("MIC %1 dB · PEAK %2")
                                   .arg(mic)
                                   .arg(m_meterHold > -60 ? QString::number(double(m_meterHold), 'f', 0)
                                                          : QStringLiteral("—")),
                m_meterHold > -1 ? c::red() : c::alpha(col, 0.8f), 9, true);
        c::gauge(p, QRectF(m.left(), m.bottom() - 12, m.width(), 9), clampf((db + 48.0f) / 48.0f, 0, 1),
                 db > -1 ? c::red() : db > -9 ? c::amber() : col, 16);
    }
    c::chip(p, pc, at(0.76, 0.795), QStringLiteral("+"), false, &m_spots, SpotGainUp);
    c::chip(p, pc, at(0.795, 0.865), m_autoUntil > 0 ? QStringLiteral("LISTENING") : QStringLiteral("AUTO  Z"),
            m_autoUntil > 0, &m_spots, SpotAuto);
    {
        // Latency: the trip from the microphone to the ears, as measured
        // by the server, and the studio's own part of it.
        const QRectF m = at(0.865, 1.0);
        const float total = sv.inputMs + sv.outputMs + sv.bufferMs;
        const QString line = sv.input == StudioView::InputLive && sv.inputMs + sv.outputMs > 0
                                 ? QStringLiteral("%1 ms").arg(double(total), 0, 'f', 1)
                                 : sv.input == StudioView::InputLive ? QStringLiteral("LIVE") : QStringLiteral("—");
        c::text(p, QRectF(m.left(), m.top(), m.width(), 14), QStringLiteral("MIC → EAR"), c::alpha(col, 0.6f), 9, true);
        c::text(p, QRectF(m.left(), m.top() + 12, m.width(), 18), line,
                total > 15 ? c::amber() : col, 13, true);
        // A retuned or shifted voice comes a little after: say how much.
        if (m_preset == Tune || m_preset == Robot || m_formant != 0)
            c::text(p, QRectF(m.left() + m.width() * 0.55, m.top() + 12, m.width() * 0.45, 18),
                    QStringLiteral("+%1").arg(double(d[pub::Lag]), 0, 'f', 1), c::alpha(col, 0.6f), 9, true);
    }
}

// The tape: the last bars of singing as a waveform under the keys, the bar
// lines over it, and the bracket of what Space would take now.
void paintTape(Page &pg, QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const StudioView &sv = *pc.studio;
    const QRectF area = pc.rect(kLeft, kBottom, kRight, kTapeTop);
    p.fillRect(area, QColor(0, 0, 0, 120));
    p.setPen(QPen(c::alpha(col, 0.25f), 1));
    p.drawLine(area.topLeft(), area.topRight());
    bool chopping = false;
    for (const auto &h : pg.m_fingers)
        chopping |= h.id >= 0 && h.zone == Chop;
    c::text(p, area.adjusted(6, 2, -6, 0).translated(0, area.height() - 16).adjusted(0, 0, 0, -area.height() + 16),
            QStringLiteral("THE TAPE · TOUCH A WORD TO PLAY IT"), c::alpha(col, chopping ? 0.9f : 0.45f), 9, true);
    const Tape &tape = *pg.m_tape;
    const int64_t written = tape.written.load(std::memory_order_acquire);
    if (written <= 0 || (sv.input == StudioView::NoInput && !pg.m_demo))
        return;
    // Eight bars back with a clock, eight seconds without.
    bool anyPlaying = sv.metronome;
    for (int q = 0; q < sv.partCount; ++q)
        anyPlaying |= sv.parts[q].playing >= 0 || sv.parts[q].queued >= 0;
    const double spb = 60.0 / std::max(1.0, sv.bpm);
    const double span = Page::tapeSpan(sv);
    const double rate = tape.rate;
    const double nowT = sv.time + (double(written) - double(sv.sample)) / rate;
    const double lat = Page::latency(sv);
    auto xOf = [&](double t) { return area.left() + area.width() * (1.0 - (nowT - t) / span); };
    // The envelope.
    const int cols = std::max(8, int(area.width() / 2));
    const float mid = float(area.center().y()), half = float(area.height() * 0.44);
    p.setPen(QPen(c::alpha(col, 0.75f), 1.4));
    for (int k = 0; k < cols; ++k) {
        const double t1 = nowT - span * (1.0 - double(k + 1) / cols), t0 = nowT - span * (1.0 - double(k) / cols);
        const int64_t h0 = int64_t((t0 - sv.time) * rate + double(sv.sample)) / kHop,
                      h1 = int64_t((t1 - sv.time) * rate + double(sv.sample)) / kHop;
        float v = 0;
        for (int64_t h = h0; h <= h1; ++h)
            if (h >= 0 && h < written / kHop && written / kHop - h < kEnvRing - 8)
                v = std::max(v, tape.env[size_t(h & (kEnvRing - 1))]);
        const float a = clampf(std::sqrt(v * 6.0f), 0, 1) * half;
        const double x = area.left() + area.width() * (double(k) + 0.5) / cols;
        p.drawLine(QPointF(x, mid - a), QPointF(x, mid + a));
    }
    // Bar lines.
    if (anyPlaying) {
        const double beatNow = sv.beat + (nowT - sv.time) / spb;
        for (double b = std::floor(beatNow / 4) * 4; b > beatNow - 8 * 4 - 4; b -= 4) {
            const double x = xOf(sv.time + (b - sv.beat) * spb + lat);
            if (x < area.left() || x > area.right())
                continue;
            p.setPen(QPen(c::alpha(col, 0.35f), 1, Qt::DashLine));
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
    }
    // What Space would take now (or, just after it, what it took), and
    // over a vocal loop, what Shift+Space would layer onto it.
    const PartView &me = *pc.view;
    const bool canLayer = me.playing >= 0 && me.clips[me.playing].lengthBeats > 0;
    Pick pk, layer;
    bool taken = false;
    if (pg.m_last.active && pc.time - pg.m_last.at < 4.0) {
        pk = pg.m_last.pick;
        taken = true;
        if (pg.m_last.free) {
            pk.b0 = pg.m_last.pick.b0;
            pk.b1 = pg.m_last.end;
        }
    } else if (pc.time - pg.m_previewAt > 0.12 || !pg.m_preview.ok) {
        // Cheap enough several times a second: the tape shown, and a little before.
        Sung s;
        s.hop = kHop / rate;
        const int64_t last = written / kHop, first = std::max<int64_t>(0, last - int64_t(span / s.hop) - 400);
        s.env.resize(size_t(last - first));
        for (int64_t h = first; h < last; ++h)
            s.env[size_t(h - first)] = tape.env[size_t(h & (kEnvRing - 1))];
        s.t0 = sv.time + (double(first * kHop + kHop / 2) - double(sv.sample)) / rate - lat;
        PickRequest req;
        req.free = !anyPlaying;
        req.now = pc.time;
        req.beat = pc.beat;
        req.bpm = std::max(1.0, sv.bpm);
        const PartView &chords = sv.parts[Page::kChordsPart];
        if (chords.playing >= 0)
            req.fit = chords.clips[chords.playing].lengthBeats / 4.0;
        pg.m_preview = pick(s, req);
        pg.m_previewFree = req.free;
        if (canLayer && !req.free) {
            req.layerBase = me.clips[me.playing].lengthBeats / 4.0;
            req.since = pg.m_loopSince;
            pg.m_previewLayer = pick(s, req);
        } else {
            pg.m_previewLayer = Pick{};
        }
        pg.m_previewAt = pc.time;
    }
    if (!taken) {
        pk = pg.m_preview;
        layer = pg.m_previewLayer;
    }
    const bool free = taken ? pg.m_last.free : pg.m_previewFree;
    auto bracket = [&](const Pick &b, QColor bc, float fill, const QString &label, float tagRow) {
        if (!b.ok)
            return;
        const double t0 = free ? b.b0 : sv.time + (b.b0 - sv.beat) * spb;
        const double t1 = free ? b.b1 : sv.time + (b.b1 - sv.beat) * spb;
        const double x0 = std::max(area.left(), xOf(t0 + lat)), x1 = std::min(area.right(), xOf(t1 + lat));
        if (x1 <= x0)
            return;
        p.fillRect(QRectF(x0, area.top(), x1 - x0, area.height()), c::alpha(bc, fill));
        p.setPen(QPen(bc, 2));
        p.drawLine(QPointF(x0, area.top()), QPointF(x0, area.bottom()));
        p.drawLine(QPointF(x1, area.top()), QPointF(x1, area.bottom()));
        p.drawLine(QPointF(x0, area.top()), QPointF(x0 + 8, area.top()));
        p.drawLine(QPointF(x0, area.bottom()), QPointF(x0 + 8, area.bottom()));
        p.drawLine(QPointF(x1, area.top()), QPointF(x1 - 8, area.top()));
        p.drawLine(QPointF(x1, area.bottom()), QPointF(x1 - 8, area.bottom()));
        // The tag stays on the tape, inside the pad.
        const QFont f = c::font(10, true);
        const double tw = c::textWidth(f, label) + 14;
        const double tx = std::clamp(x0 + 4, area.left() + 4, std::max(area.left() + 4, area.right() - tw - 4));
        c::tag(p, QPointF(tx, area.top() + 10 + tagRow * 18), label, bc, 10);
    };
    auto describe = [&](const Pick &b) {
        if (free)
            return QStringLiteral("%1 PHRASE%2 · SETS THE TEMPO").arg(std::max(1, b.phrases)).arg(b.phrases > 1 ? "S" : "");
        return (b.bars == 1 ? QStringLiteral("1 BAR") : QStringLiteral("%1 BARS").arg(b.bars)) +
               (std::fabs(b.delta) > 1e-6 ? QStringLiteral(" · CUT %1%2 BEAT")
                                                .arg(b.delta > 0 ? "+" : "−")
                                                .arg(std::fabs(b.delta), 0, 'g', 3)
                                          : QString()) +
               (b.cutDb <= 3 ? QStringLiteral(" · IN THE GAPS") : QStringLiteral(" · NO GAP: QUIETEST"));
    };
    if (!taken && layer.ok)
        bracket(layer, c::alpha(col, 0.9f), 0.05f, QStringLiteral("⇧SPACE LAYERS: ") + describe(layer), 1);
    if (pk.ok)
        bracket(pk, taken ? c::red() : c::alpha(QColor(255, 255, 255), 0.85f), taken ? 0.12f : 0.06f,
                (taken ? QStringLiteral("TAKEN: ") : QStringLiteral("SPACE: ")) + describe(pk), 0);
}

}

PartSpec makeVocal() {
    PartSpec s;
    s.name = QStringLiteral("Vocal");
    s.blurb = QStringLiteral("your voice, live");
    s.color = QColor("#FF7AB8");
    auto tape = std::make_shared<vocal::Tape>();
    s.engine = std::make_unique<vocal::Engine>(tape);
    s.control = std::make_unique<vocal::Page>(tape);
    s.monitors = true;
    s.followsChords = false;
    // A dry voice up front: a touch of the room, no pump (the singer is
    // the one thing the kick shouldn't push around).
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.12f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
