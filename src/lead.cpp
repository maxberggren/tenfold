// Lead: the melody, played the way a lead player plays it.
//
// The right hand plays a neck of the scale: one finger is one singing note,
// and a second finger landing while the first is held is legato, a hammer-on
// that glides there without a new attack; lifting it pulls back off to the
// note still held. So two fingers trill, three or four run. Under each
// finger: slide to glide, wiggle for vibrato, height for brightness (or the
// vowel), pressure to swell, a flatter finger for a fatter sound, a twist to
// bend up or down a step, and a click of the pad to leap an octave.
//
// The left hand works the column beside the neck: fingers on the harmony
// rows add voices a third, fifth, sixth or octave away that move with the
// melody in the key (or on the chord's own notes when following it), a
// finger on BEND is a pitch wheel that rests on whole steps, and a finger on
// THROW throws the line into its echo.
//
// Loops are only ever asked for, with the studio's Space: the page shows
// when Space has something to take, then what the take became.
// Playing alone never makes a loop.
//
// The engine plays one mono line per source (the live player and each clip),
// so a looped melody and a live one never steal each other's legato.

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <array>
#include <cmath>
#include <vector>

#include "console.h"
#include "daxophone.h"
#include "electro.h"
#include "gong.h"
#include "guitar.h"
#include "kargyraa.h"
#include "lead.h"
#include "parts.h"
#include "trombamarina.h"
#include "saxophone.h"

namespace tf {
namespace lead {

namespace {

// A sine table: FM wants several sines per sample, and a table read costs a
// fraction of std::sin. 2048 points with linear interpolation is about
// −90 dB of error, far under anything the oscillators make.
struct SineTable {
    static constexpr int N = 2048;
    float v[N + 1];
    SineTable() {
        for (int i = 0; i <= N; ++i)
            v[i] = float(std::sin(2.0 * 3.14159265358979323846 * i / N));
    }
    // phase in cycles, any value.
    inline float operator()(float phase) const {
        phase -= std::floor(phase);
        const float x = phase * float(N);
        const int i = std::min(int(x), N - 1);
        const float f = x - float(i);
        return v[i] + (v[i + 1] - v[i]) * f;
    }
};
const SineTable &sine() {
    static const SineTable t;
    return t;
}

// A PolyBLEP pulse of any width, its DC taken out so a narrow chip pulse
// doesn't push the filter off centre.
struct PulseOsc {
    float phase = 0;
    inline float step(float inc, float pw) {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        float v = phase < pw ? 1.0f : -1.0f;
        v += polyBlep(phase, inc);
        float t2 = phase - pw;
        if (t2 < 0)
            t2 += 1.0f;
        v -= polyBlep(t2, inc);
        return v - (2.0f * pw - 1.0f);
    }
};

// A saw hard-synced to a master oscillator that is never heard: every time
// the master comes round, the saw starts its cycle again wherever it was,
// so the note stays the master's while the saw's own, higher pitch sweeps
// through its harmonics as a formant (the synth-pop and Italo disco lead).
// Both the saw's own wrap and the reset are steps; each is rounded off by a
// cubic B-spline over the four samples around it (a polyBLEP of the third
// order, whose leftovers fall away far faster above Nyquist than the usual
// two-sample one's, so a slave swept and swung by FM under a vibrato folds
// nothing audible back between the note's harmonics). The output runs two
// samples late, so a step found in this sample can still reach back into
// the two before it.
struct SyncOsc {
    float master = 0, slave = 0;
    float back2 = 0, back1 = 0, ahead = 0; // the two samples still open, and what is owed the next
    // inc: the master's (the note's) phase step; ratio: the slave's pitch
    // over it; under it, `own` of the master's sine, which keeps the note's
    // fundamental there however the slave sweeps (a synced saw's own first
    // harmonic comes and goes with its ratio, and a saw under it could
    // cancel it out). fm: how far the
    // master's sine swings the slave's speed through each cycle, so the
    // sync's formant is smeared into FM's clangorous sidebands; still
    // reset every period, it stays on the note.
    inline float step(float inc, float ratio, float own, float fm = 0) {
        const float swing = fm > 0 ? 1.0f + fm * sine()(master) : 1.0f;
        const float incS = std::min(inc * ratio * swing, 0.45f);
        float corr = ahead;
        ahead = 0;
        // The spline's integral from its start, as it rises from 0 to a
        // half: u^4 / 24 over its first sample, this over its second.
        auto rise = [](float u) {
            const float u2 = u * u;
            return (-3.0f * u2 * u2 + 16.0f * u2 * u - 24.0f * u2 + 16.0f * u - 4.0f) * (1.0f / 24.0f);
        };
        // A step of height h, d samples before this one: the spline's
        // integral less the step itself, at the two samples before it and
        // the two after.
        auto blep = [&](float h, float d) {
            const float e = 1.0f - d, d2 = d * d, e2 = e * e;
            back2 += h * d2 * d2 * (1.0f / 24.0f);
            back1 += h * rise(d + 1.0f);
            // After the step, by the spline's symmetry: less the mirror of
            // what came before it.
            corr -= h * rise(2.0f - d);
            ahead -= h * e2 * e2 * (1.0f / 24.0f);
        };
        master += inc;
        if (master >= 1.0f) {
            master -= 1.0f;
            const float d = std::clamp(master / std::max(inc, 1e-9f), 0.0f, 1.0f); // since the reset
            float at = slave + (1.0f - d) * incS;                                // the slave then
            if (at >= 1.0f) {
                at -= 1.0f;
                blep(-2.0f, std::clamp(d + at / incS, 0.0f, 1.0f));
            }
            blep(-2.0f * at, d);
            slave = d * incS;
        } else {
            slave += incS;
            if (slave >= 1.0f) {
                slave -= 1.0f;
                blep(-2.0f, std::clamp(slave / incS, 0.0f, 1.0f));
            }
        }
        const float cur = 2.0f * slave - 1.0f + own * sine()(master) + corr;
        const float out = back2;
        back2 = back1;
        back1 = cur;
        return out;
    }
};

// A free reed, as an accordion's are: a brass tongue swinging through a
// slot chops the air into something between a saw and a narrow pulse, one
// phase making both so they stay locked. The harder the bellows blow, the
// shorter the slot stays open each swing: a narrower pulse, a brighter reed.
struct ReedOsc {
    float phase = 0;
    inline float step(float inc, float pw = 0.34f) {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        const float saw = 2.0f * phase - 1.0f - polyBlep(phase, inc);
        float t2 = phase - pw;
        if (t2 < 0)
            t2 += 1.0f;
        const float pulse = (phase < pw ? 1.0f : -1.0f) + polyBlep(phase, inc) - polyBlep(t2, inc) - (2.0f * pw - 1.0f);
        return 0.55f * saw + 0.45f * pulse;
    }
};

// A steel pan note's modes: its fundamental, the octave and the twelfth a
// tuner hammers in on purpose, the double octave, and a short-lived
// metallic ring well above them from the skirt around the note. Each
// mode's share of a full strike and how long it rings against the
// fundamental; the octave and the twelfth bloom a moment after the strike,
// as the note's energy flows up into them.
constexpr int kModes = 5;
constexpr float kModeRatio[kModes] = {1.0f, 2.0f, 3.0f, 4.0f, 6.73f};
constexpr float kModeLevel[kModes] = {1.0f, 0.62f, 0.26f, 0.1f, 0.07f};
constexpr float kModeRing[kModes] = {1.0f, 0.62f, 0.32f, 0.18f, 0.025f};
constexpr float kModeBloom[kModes] = {0.0f, 1.0f, 0.6f, 0.0f, 0.0f};

// A pan's halo: a struck note's neighbours on the same pan, its octave and
// the fifth over it, ringing in sympathy. Where they sit against the note
// (each a few cents off the note's own partials, as no two notes are tuned
// to each other exactly, so they beat slowly against them), how loud a full
// stroke leaves them, how long they ring against the note, and which side
// of the pan they are on.
constexpr float kHaloRatio[2] = {2.0f * 1.0035f, 1.5f * 0.9972f};
constexpr float kHaloLevel[2] = {0.3f, 0.2f};
constexpr float kHaloRing[2] = {0.8f, 0.9f};
constexpr float kHaloPan[2] = {0.7f, -0.7f};

// A pan's roll: how long a player takes to swell into it, and how much
// harder the strokes on the beat land.
constexpr float kRollSwellS = 0.35f;
constexpr float kRollAccent = 1.5f;
// Gong: what a gong's pressure (Pa at a metre) is worth on the page, per kind
// (bossed, opera, tam-tam), so each speaks as loud as the other leads.
constexpr float kGongLevel[gong::kKinds] = {0.27f, 0.85f, 0.3f};
// Kargyraa: the level its voice is held at (RMS), before the envelope.
constexpr float kKgLevel = 0.2f;
// Gong: how many tam-tams may ring at once.
constexpr int kTamTams = 4;
// Tromba marina: what its pressure (Pa at a metre) is worth on the page, and
// how long the bow is, in seconds of an ordinary stroke.
constexpr float kTrombaLevel = 0.5f;
constexpr float kTrombaStrokeS = 2.6f;

// Daxophone: what the contact microphones' newton-metres are worth on the
// page, and how much of what reaches the clamp of the bowed part's swing
// (as the tongue hears it, Tongue::transmission) is evened out across the
// neck (it peaks wherever one of the modes behind the dax meets the note
// or a strong harmonic), so a line is one instrument with a voice that
// still swells where the tongue rings; the bow's speed (m/s) at an ordinary stroke and how much faster a
// firm landing draws it; and how long a stroke of a double-bass bow lasts
// before it turns.
constexpr float kDaxLevel = 4.0f, kDaxEven = 0.75f, kDaxEvenFloor = 0.02f;
constexpr float kDaxSpeed = 0.15f, kDaxSpeedVel = 0.1f;
constexpr float kDaxStrokeS = 4.0f;
// How firmly the dax is pressed at an ordinary touch, from lightly (0, 8 N)
// to firmly (1, 40 N): see daxophone.h's Spec.
constexpr float kDaxPress = 0.35f;

// A lighter touch than ordinary bows lighter in proportion (to the power
// kDaxSoft), down to nothing, where the bow skates and the tongue barely
// speaks; a harder one leans further, slowly at first (a firm press stays
// in the window) then steeply, the hardest to kDaxCrush times the roof,
// where the hair holds on too long and the note breaks: rough (A3–E5),
// or falling to a clean tone a few semitones under it (A4 and up).
constexpr float kDaxSoft = 1.3f, kDaxCrush = 2.5f;
// The contact microphones' colour: the block on its stand rolls the
// bottom off, the small box's top and the piezo discs ring a broad band
// near three kilohertz, and little over eight comes through.
constexpr float kDaxLowHz = 45.0f, kDaxBandHz = 2800.0f, kDaxBandQ = 1.4f, kDaxBand = 0.35f, kDaxTopHz = 8000.0f;
// Guitar: what the mic'd cab's level (a fortieth of the speaker's volts) is worth on the page.
constexpr float kGuitarLevel = 0.85f;

// Sax: what a saxophone's pressure (Pa at a metre) is worth on the page, and
// the hardest a press blows it (the mouth pressure over the reed's closing
// pressure).
constexpr float kSaxLevel = 0.9f;
constexpr float kSaxBlowTop = 0.9f;

// An accordion's bellows: how much air one way holds (seconds of sound at
// an ordinary press), and how long a rest is a breath, long enough to turn
// them unheard.
constexpr float kBellowsAir = 2.6f;
constexpr float kBreathS = 0.4f;

// How much more of a sitar string's sound the taraf takes up once the string
// is let go than while it sounds (the string dies within a fifth of a
// second of it, so this is a short, strong push).
constexpr float kTarafBloom = 32.0f;

// How narrow the sitar's jawari formant is, and how loud the mizrab's scrape.
constexpr float kJawQ = 9.0f;
constexpr float kJawSweepS = 0.7f; // how long the formant takes to slide down from the stroke
constexpr float kScrape = 0.5f;
constexpr float kSizzle = 0.8f; // how loud the wire sizzles on the bridge while it touches, in the formant's band

// How long the erhu's bow is, in seconds of an ordinary stroke.
constexpr float kBowStrokeS = 3.2f;

// The bow's arc: an erhu player leans into the middle of the stroke and
// eases off toward its end, so a held note swells a few decibels and
// tapers. Its weight a given number of seconds into the stroke.
inline float bowArcAt(float pos) {
    const float u = clampf(pos / kBowStrokeS, 0.0f, 1.0f);
    return 0.78f + 0.3f * std::sin(kPi * std::pow(u, 0.8f));
}

// An erhu's huayin, the slide between two notes a finger travels: how
// quick (its natural frequency, radians a second) and how damped, so it
// lands a few per cent of the way past the note and settles back.
constexpr float kSlideW = 2.0f * kPi * 3.0f;
constexpr float kSlideZeta = 0.62f;
// How far apart two notes are, in semitones, before the finger slides
// between them rather than stepping: more than a whole tone.
constexpr float kSlideOver = 2.2f;

// The erhu's body: the box's own low resonance, the python skin's nasal one
// around a kilohertz (the loudest), and a small one above.
constexpr float kBowBodyHz[3] = {470.0f, 1050.0f, 2700.0f};
constexpr float kBowBodyQ[3] = {2.0f, 4.5f, 4.0f};
constexpr float kBowBodyGain[3] = {0.35f, 2.0f, 0.35f};

// The sync lead's sung formant: how narrow, and how far it lifts what it rings.
constexpr float kYowQ = 3.5f;
constexpr float kYow = 2.0f;
// The sync lead's vowel: its second formant from "ow" to "ee" with the
// finger's height (Hz), how narrow, and how far it lifts what it rings.
constexpr float kSyncF2Low = 850.0f;
constexpr float kSyncF2High = 2600.0f;
constexpr float kSyncF2Q = 5.0f;
constexpr float kSyncF2 = 2.6f;
// How much of the master's sine the sync lead keeps under its slave.
constexpr float kSyncOwn = 0.6f;

// How much dry saw a vowel keeps under its formants.
constexpr float kFormDry = 0.2f;

// Vowel formants (Hz) from dark to bright: oo, oh, ah, eh, ee.
constexpr float kVowels[5][3] = {
    {320, 800, 2300}, {450, 830, 2600}, {760, 1150, 2500}, {550, 1800, 2550}, {290, 2250, 3000}};

// A sample into a half-band filter's line whose output isn't wanted this
// time: decimating by two, only every other one is.
inline void store(float *line, int &pos, float x) {
    line[pos] = x;
    pos = pos + 1 == Oversampler2x::N ? 0 : pos + 1;
}

// An attack, decay, sustain, release envelope with the same shape as the
// shared Adsr, but with its coefficients worked out when it is struck rather
// than an exp() every sample: a lead keeps two of these running per voice.
struct Env {
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float v = 0, s = 0.8f, ka = 1, kd = 1, kr = 1;
    void set(float a, float d, float sustain, float r, float rate) {
        ka = 1.0f - std::exp(-1.0f / (std::max(a, 0.0005f) * rate));
        kd = 1.0f - std::exp(-1.0f / (std::max(d, 0.001f) * rate * 0.3f));
        kr = 1.0f - std::exp(-1.0f / (std::max(r, 0.001f) * rate * 0.3f));
        s = sustain;
    }
    void gate(bool on) { stage = on ? Attack : (stage == Idle ? Idle : Release); }
    inline float step() {
        switch (stage) {
        case Attack:
            // Toward a little over the top, so the attack ends on a slope, not a crawl.
            v += (1.2f - v) * ka;
            if (v >= 1.0f) {
                v = 1.0f;
                stage = Decay;
            }
            break;
        case Decay:
            v += (s - v) * kd;
            if (std::fabs(v - s) < 1e-4f)
                stage = Sustain;
            break;
        case Sustain:
            v = s;
            break;
        case Release:
            v -= v * kr;
            if (v < 1e-5f) {
                v = 0;
                stage = Idle;
            }
            break;
        case Idle:
            break;
        }
        return v;
    }
};

} // namespace

// --- engine -------------------------------------------------------------------

// One held note of a line, as its commands describe it.
struct NoteIn {
    int id = -1;
    int degree = 0, octave = 0;
    float vel = 0.8f, y = 0.5f, off = 0, motion = 0, fat = 0, twist = 0, press = 0.5f;
    bool follow = false;
};

// A line of play: the live player, or one clip. Mono, last note priority.
struct Line {
    NoteIn notes[10];
    int count = 0;
    NoteIn last; // the note that sounded last, for its release
    int voice = -1;
    int sound = 0;
    float harmRow[kHarmSlots] = {}, harmLevel[kHarmSlots] = {};
    float bend = 0, throwAmt = 0, octave = 0, glideScale = 1;
    int gate = 0;
    int tone = 0; // steps darker or brighter (- =)
    // When the line last fell silent (in samples), which way an accordion's
    // bellows are going, and how much air they have drawn that way (in
    // seconds of sound at an ordinary press): they turn round by the
    // phrase, once spent, never note by note.
    int64_t offAt = -(int64_t(1) << 40);
    float bellows = 1, air = 0;
    const NoteIn *top() const { return count > 0 ? &notes[count - 1] : nullptr; }
};

struct Bank {
    static constexpr int kUni = 7;
    float pitch = 60, target = 60; // MIDI, gliding toward the target
    float pitchVel = 0;            // Bow: semitones a second, while a huayin's slide carries it
    float level = 0, want = 0;     // gain, fading toward want (harmony voices come and go)
    SawOsc saw[kUni];
    PulseOsc pulse[2];
    float car[2] = {}, mod[2] = {};
    SawOsc layer, layer2;
    SquareOsc sub;
    Svf breath; // the band of air over the note (and a bow's rosin)
    Oversampler2x osL, osR;
    // String: the string itself, the loop filter's state, and the pluck
    // still being fed in (samples left, of how many). The jawari's formant,
    // and the swing it follows. Bow: the string is `string` on the nut's
    // side of the bow and `bridge` on the bridge's, `strLp` its loss.
    Delay string, bridge;
    float strLp = 0;
    int pluck = 0, pluckLen = 1;
    OnePoleLp pluckLp;
    bool wasLive = false; // a harmony string coming in is plucked
    Svf jawF, jawN;
    float swing = 0, swingTop = 0; // the string's swing, and how wide it was just after the pluck
    float jawSweep = 0;            // how far the jawari's formant still has to slide down, 1 at the stroke
    // Pan: each mode's ringing energy, the struck part on top of it that a
    // roll's strokes give and that dies within a stroke, the level both
    // make (through two smoothing stages, the stick's contact) and its two
    // sines' phases; the bloom still to come up, and how sharp the struck
    // note still stands of its pitch, in semitones.
    float modeE[kModes] = {}, modeF[kModes] = {}, modeS[kModes] = {}, modeA[kModes] = {}, modePh[kModes][2] = {};
    float haloE[2] = {}, haloA[2] = {}, haloPh[2] = {}; // Pan: the neighbours a stroke sets ringing, their ring as heard, phases
    float bloom = 0, settle = 0;
    float thud = 0; // how hard the stick's last stroke landed (String: the mizrab's scrape still sounding; Reed: how loud the key's click)
    float ping = 0; // Pan: how bright the stick's first contact rings, and its few milliseconds still to come
    int pingLeft = 0, pingLen = 1;
    Svf pingF, pingLp;
    Svf thudLp;
    SyncOsc sync[2], sync2[2];
    ReedOsc reeds[4];
    // Gong: the voice's gong (each voice of a line its own, as a gong
    // chime's are), and whether it has been hung and struck for this note.
    gong::Gong gong;
    bool gongLive = false;
    // Tromba marina: the voice's instrument (each voice of a line its own),
    // whether it is sounding, and the harmonic the thumb is on and the note
    // it was chosen for.
    tromba::TrombaMarina tromba;
    bool trombaLive = false;
    float trombaFor = -100;
    // Daxophone: the voice's tongue (each voice of a line its own daxophone).
    dax::Tongue tongue;
    // Guitar: the voice's guitar (each voice of a line its own, as a
    // harmony line is played by several guitarists), whether it has been
    // picked for this note, and where the note's fret is (MIDI) and where
    // the finger rests with its bends but without its slide.
    guitar::Guitar guitar;
    bool guitarLive = false;
    float fret = 60, anchor = 60;
    float rested = 60; // where the finger last rested on the neck (with its bends), the fret a slide counts from
    // Kargyraa: the voice's throat (kargyraa.h), each voice of a line its own.
    kargyraa::Played kg;
    // Sax: the voice's horn (each voice of a line its own: a section), and
    // whether it has been put to the lips for this note.
    sax::Sax sax;
    bool saxLive = false;
};

struct Voice {
    static constexpr int kBanks = 1 + kHarmSlots;
    bool active = false, releasing = false;
    int owner = -1; // the line driving it, or −1 once it's been left to ring out
    int from = 0;   // the line it was started for: a tail keeps that line's gate and throw
    int sound = 0;
    uint64_t born = 0;
    Bank banks[kBanks];
    Env amp, filt;
    float vel = 0.8f;
    float tau = 0.07f; // the glide toward the current note
    float y = 0.5f, press = 0.5f, fat = 0, motion = 0; // smoothed expression
    float ySet = 0.5f, pressSet = 0.5f, fatSet = 0, motionSet = 0;
    float scoop = 0, fmEnv = 1;
    float chiff = 0, chiffFast = 0; // the puff of air on a tongued attack: its fall, and its rise
    // An old synth's tuning wandering, each oscillator on its own (the two
    // saws and the square under them): where each is, where it is heading,
    // and how long until it heads somewhere else.
    static constexpr int kDrifts = 3;
    float drift[kDrifts] = {}, driftTo[kDrifts] = {}, driftLeft[kDrifts] = {};
    float vibPhase = 0, held = 0; // seconds since the note began, for the vibrato's delay
    float pwmPhase = 0;
    int64_t rollSlot = -1; // Pan: the studio's subdivision the roll last struck on, −1 before it begins
    float rollLate = 0;  // and how far off the beat's grid this stroke comes (a player's hands are never a clock)
    int rollHand = 0;    // and which stick strikes next
    float rollAge = 0;   // Pan: seconds the roll has run, for the player's swell into it
    float symBoost = 1;  // how much of it the taraf takes up: more once it is let go
    float syncEnv = 0, syncEnv2 = 0; // Sync: the sweeps from the attack, falling to 0
    float syncLfo = 0;   // Sync: the slow wander's phase
    float syncWob = 0;   // Sync: how fast the pitch is swinging, smoothed
    float bowDir = 1, bowDirS = 1; // Bow: which way the bow is drawn, and its turning round
    float bowPos = 0;    // Bow: seconds into the stroke, for its arc and the bow change at its end
    float bowArc = 0;    // Bow: the arc's weight as heard, following it
    // Bow: where the last block left the friction's slope, the bow's place
    // on the string and the string's loss (and so its delay), each block
    // sliding on from there; a slope below 0 is a new note, which starts
    // where it is.
    float bowWas[6] = {-1, 0, 0, 0, 0, 0}; // and its level, how fast it is drawn, and how it grinds
    float bowTauWas[kBanks] = {};
    float vibRateJ = 1, vibDepthJ = 1; // Bow: this vibrato cycle's own speed and width, never two alike
    float vibW = 0;      // Bow: the vibrato's width as heard, following the hand
    float slide = 0;     // Bow: seconds a huayin's slide has left, 0 when none
    float slideSize = 1; // Bow: how far it slides, in semitones
    float slideDip = 1;  // Bow: how much it lightens the line
    Svf bodyLp[2][3];    // Bow: the small body's roll-off over the top
    float daxBow = 0;    // Daxophone: the bow drawn, 0 still .. 1 at its speed
    float daxLand = 0;   // Daxophone: its weight on the tongue, 0 lifted .. 1 resting
    float reg[kRegisters] = {1, 0, 0}; // Reed: the registers drawn, smoothed
    float bellows = 1;   // Reed: push or pull
    float jerk = 0;      // Reed: the bellows turning round, falling to 0
    float gateEnv = 1;
    float formNorm = 1; // the vowel's level made up
    float cut = 4;                // smoothed cutoff, in octaves above 60 Hz
    Ladder fL, fR;
    Oversampler2x ladOs[2]; // Sync: the ladder run at twice the rate, so its soft clip folds nothing back
    Svf topLp[2][2];        // Sync: and over 19 kHz, what the return to the page's rate lets through, taken off
    Svf form[2][3];
    OnePoleLp airLp; // takes the hiss off the breath
    float level = 0;      // for the page
    float lastL = 0, lastR = 0; // the last sample out, for a stolen voice's declick
    Rng rng;
};

class LeadEngine : public PartEngine {
public:
    void init(float rate) override {
        m_rate = rate;
        (void)sine();
        for (size_t i = 0; i < m_voices.size(); ++i) {
            m_voices[i].rng = Rng(0x1EADu + uint32_t(i) * 7919u);
            // A string as long as the lowest note any bank can reach: the
            // neck's bottom an octave down, its −OCT voice, bent; twice that,
            // as the bowed string runs at twice the rate.
            for (auto &k : m_voices[i].banks) {
                k.string.init(size_t(rate / 12.0f));
                k.bridge.init(size_t(rate / 12.0f));
            }
        }
        for (auto &d : m_sym)
            d.init(size_t(rate / 90.0f));
        // The gongs' designs are built once, here, never on the audio thread.
        gong::prepare();
        dax::prepare();
        for (auto &v : m_voices)
            for (auto &k : v.banks) {
                k.gong.init(rate);
                k.tongue.init(rate);
                k.tongue.shareEar(&m_daxEar); // one player, who knows the tongue
            }
        // The tromba marina's soundboard too.
        tromba::prepare();
        for (auto &v : m_voices)
            for (auto &k : v.banks)
                k.tromba.init(rate);
        // So are the guitar's (its valves' curves, its strings' stiffness).
        guitar::prepare();
        for (auto &v : m_voices)
            for (auto &k : v.banks)
                k.guitar.init(rate);
        for (auto &line : m_rig)
            for (auto &rig : line)
                rig.init(rate);
        // And each voice's throat, for Kargyraa.
        for (auto &v : m_voices)
            for (auto &k : v.banks)
                k.kg.init(rate);
        // And the saxophones' bores, each fingering's resonances.
        sax::prepare();
        for (auto &v : m_voices)
            for (auto &k : v.banks)
                k.sax.init(rate);
        for (auto &c : m_chik) {
            c.string.init(size_t(rate / 150.0f));
            c.jawF.set(3000.0f, 4.0f, rate);
        }
        m_echoL.init(size_t(3.2f * rate));
        m_echoR.init(size_t(3.2f * rate));
        m_echoTime.setTime(0.25f, rate);
        m_echoTime.reset(0.36f * rate);
        m_echoHp.set(280.0f, 0.6f, rate);
        m_echoHp2.set(280.0f, 0.6f, rate);
        m_echoLpL.set(4800.0f, rate);
        m_echoLpR.set(4800.0f, rate);
        m_declickFade = std::exp(-1.0f / (0.003f * rate));
        m_hpL.set(120.0f, 0.6f, rate);
        m_hpR.set(120.0f, 0.6f, rate);
    }

    void command(const Command &c, AudioContext &ctx) override {
        // Clip events land on their exact sample: hold them for their offset
        // in the coming block. Live ones are late and play at once.
        const int off = ctx.offsetOf(c.t, kBlock);
        if (off > 0 && m_pendingCount < int(m_pending.size())) {
            m_pending[size_t(m_pendingCount++)] = {c, off};
            return;
        }
        apply(c, ctx);
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        // Pending commands, in time order, split the block.
        std::stable_sort(m_pending.begin(), m_pending.begin() + m_pendingCount,
                         [](const Pending &a, const Pending &b) { return a.offset < b.offset; });
        int at = 0, k = 0;
        while (at < n) {
            while (k < m_pendingCount && m_pending[size_t(k)].offset <= at)
                apply(m_pending[size_t(k++)].c, ctx);
            const int until = k < m_pendingCount ? std::min(n, m_pending[size_t(k)].offset) : n;
            if (until > at)
                renderSegment(l, r, at, until - at, ctx);
            at = until;
        }
        for (; k < m_pendingCount; ++k)
            apply(m_pending[size_t(k)].c, ctx);
        m_pendingCount = 0;
        renderEcho(l, r, n, ctx);
        m_clock += n;
    }

    void silence(bool hard) override {
        for (auto &line : m_lines) {
            line.count = 0;
            line.bend = line.throwAmt = line.octave = 0;
        }
        for (auto &v : m_voices) {
            if (hard)
                v.active = false;
            else if (v.active)
                release(v);
        }
        if (hard) {
            m_echoL.clear();
            m_echoR.clear();
            for (auto &d : m_sym)
                d.clear();
            for (auto &c : m_chik) {
                c.string.clear();
                c.wait = -1;
                c.pluck = 0;
            }
            m_symLevel = 0;
        }
    }

    void publish(float *data) override {
        // See lead.h for what each number is. The layers above the first
        // are shown for the slot that sounds them (one plays at a time).
        auto sounding = [&](int l) {
            const Line &c = m_lines[size_t(l)];
            return c.voice >= 0 && m_voices[size_t(c.voice)].active && m_voices[size_t(c.voice)].owner == l;
        };
        for (int shown = 0; shown < kShownLines; ++shown) {
            int li = shown;
            if (shown > kClipSlots) {
                const int layer = shown - kClipSlots;
                li = 1 + kClipSlots * layer;
                for (int s = 0; s < kClipSlots; ++s)
                    if (sounding(1 + s + kClipSlots * layer)) {
                        li = 1 + s + kClipSlots * layer;
                        break;
                    }
            }
            float *d = data + kLineData * shown;
            const Line &line = m_lines[size_t(li)];
            const Voice *v = line.voice >= 0 ? &m_voices[size_t(line.voice)] : nullptr;
            const bool on = v && v->active && v->owner == li;
            d[0] = on ? (line.count > 0 ? 1.0f : 0.5f) : 0.0f;
            d[1] = m_pubPos[li];
            d[2] = on ? v->level : 0.0f;
            d[3] = on ? v->y : 0.0f;
            for (int s = 0; s < kHarmSlots; ++s) {
                d[4 + s] = m_pubHarm[li][s];
                d[7 + s] = on ? v->banks[1 + s].level : 0.0f;
            }
            d[10] = line.bend;
            d[11] = line.throwAmt;
            d[12] = float(line.sound);
            d[13] = line.top() && line.top()->follow ? 1.0f : 0.0f;
            d[14] = on ? m_pubVib[li] : 0.0f;
            d[15] = line.octave;
        }
        data[kEchoLevel] = m_echoLevel;
        data[kVoiceCount] = float(voices());
    }

    int voices() const override {
        int n = 0;
        for (auto &v : m_voices)
            n += v.active;
        return n;
    }

private:
    struct Pending {
        Command c;
        int offset = 0;
    };

    // --- commands ---

    void apply(const Command &c, AudioContext &ctx) {
        const int li = lineOf(c.id);
        Line &line = m_lines[size_t(li)];
        switch (c.type) {
        case Command::On: {
            NoteIn n = noteFrom(c);
            n.vel = clampf(c.f[0], 0.1f, 1.0f);
            n.follow = (c.i[3] & kFollow) != 0;
            line.gate = (c.i[3] >> kGateShift) & 3;
            line.tone = unpackTone(c.i[3]);
            // The harmony held when the note landed, so a clip taken in the
            // middle of a held harmony plays it too.
            for (int s = 0; s < kHarmSlots; ++s) {
                const int row = unpackRow(c.i[3], s);
                if (row >= 0) {
                    // Three bits hold up to 6; a clip from a file may say so.
                    if (line.harmLevel[s] <= 0)
                        line.harmRow[s] = float(std::min(row, kRows - 1));
                    line.harmLevel[s] = std::max(line.harmLevel[s], 0.75f);
                } else if (li != 0) {
                    line.harmLevel[s] = 0;
                }
            }
            noteOn(li, n, std::clamp(c.i[2], 0, kSounds - 1), ctx);
            break;
        }
        case Command::Move:
            for (int i = 0; i < line.count; ++i)
                if (line.notes[i].id == c.id) {
                    const float vel = line.notes[i].vel;
                    const bool follow = line.notes[i].follow;
                    line.notes[i] = noteFrom(c);
                    line.notes[i].vel = vel;
                    line.notes[i].follow = follow;
                }
            break;
        case Command::Off:
            noteOff(li, c.id, ctx);
            break;
        case Command::Param:
            switch (c.i[0]) {
            case PHarmony: {
                const int s = std::clamp(c.i[1], 0, kHarmSlots - 1);
                line.harmRow[s] = clampf(c.f[0], 0.0f, float(kRows - 1));
                line.harmLevel[s] = clampf(c.f[1], 0.0f, 1.0f);
                break;
            }
            case PBend:
                line.bend = clampf(c.f[0], -4.0f, 4.0f);
                break;
            case PThrow:
                line.throwAmt = clampf(c.f[0], 0.0f, 1.0f);
                break;
            case POctave:
                line.octave = c.f[0] > 0.5f ? 1.0f : 0.0f;
                break;
            case PGate:
                line.gate = std::clamp(c.i[1], 0, kGates - 1);
                break;
            case PGlide:
                line.glideScale = clampf(c.f[0], 0.1f, 6.0f);
                break;
            case PTone:
                line.tone = std::clamp(c.i[1], -kToneSteps, kToneSteps);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    static NoteIn noteFrom(const Command &c) {
        NoteIn n;
        n.id = c.id;
        // A clip read from a file may hold any degree and octave; far past
        // the keyboard they would overflow the pitch sums and the tuning's
        // table, so they are kept to a few octaves either side of the neck.
        n.degree = std::clamp(c.i[0], -24, 24);
        n.octave = std::clamp(c.i[1], -2, 10);
        n.y = clampf(c.f[1], 0.0f, 1.0f);
        n.off = clampf(c.f[2], -1.5f, 1.5f);
        n.motion = clampf(c.f[3], 0.0f, 1.0f);
        n.fat = clampf(c.f[4], 0.0f, 1.0f);
        n.twist = clampf(c.f[5], -1.5f, 1.5f);
        n.press = clampf(c.f[6], 0.0f, 1.0f);
        return n;
    }

    void noteOn(int li, const NoteIn &n, int snd, AudioContext &ctx) {
        Line &line = m_lines[size_t(li)];
        // A note already held under this id (a replayed clip coming round)
        // is replaced, not stacked.
        removeNote(line, n.id);
        if (line.count == int(std::size(line.notes)))
            removeNote(line, line.notes[0].id);
        const bool legato = line.count > 0;
        Voice *v = line.voice >= 0 ? &m_voices[size_t(line.voice)] : nullptr;
        const bool mine = v && v->active && v->owner == li;
        // A sitarist's other stroke: a second finger tapped high on the pad
        // while a note rings is the chikari, the high drone strings struck
        // beside the melody string, not a hammer-on. It is no note of the
        // line, so its lift is no pull-off either.
        if (legato && mine && sound(v->sound).chikari > 0 && n.y >= kChikariY) {
            // As hard as the melody string still swings: a player strikes
            // the drones to keep the line company, never over it, so a tap
            // late in a long alap sits under the fading note (some 8 dB)
            // instead of jumping out of it.
            const Bank &k = v->banks[0];
            const float now = clampf(k.swing / std::max(k.swingTop, 1e-6f), 0.04f, 1.0f);
            strum(n.vel, sound(v->sound).chikari * 0.85f * now);
            return;
        }
        line.notes[line.count++] = n;
        if (legato && mine) {
            // A hammer-on: the voice glides on; plucks and bells strike again.
            const Sound &s = sound(v->sound);
            v->tau = glideTime(*v, line, n, s.glide, ctx);
            v->held = 0;
            huayin(*v, line, n, ctx);
            // A guitarist's legato is a hammer-on: no new pick.
            if (s.model == Sound::Guitar)
                for (auto &k : v->banks)
                    if (k.guitarLive)
                        k.guitar.hammer(pickForce(n.vel));
            if (s.retrig)
                strike(*v, n.vel, false);
            return;
        }
        line.sound = snd;
        const Sound &s = sound(snd);
        if (mine && s.porta > 0 && v->sound == snd) {
            // A mono synth with portamento: the voice still ringing glides
            // from where it was into the new note and starts again from its
            // own level, with no click.
            v->releasing = false;
            v->tau = glideTime(*v, line, n, s.porta, ctx);
            v->held = 0;
            huayin(*v, line, n, ctx);
            strike(*v, n.vel, true);
            return;
        }
        if (mine) {
            // The old note rings out on its own. It may still be held: a
            // replayed clip's note coming round under the same id replaces
            // one whose Off never came, and left unreleased it would sing on,
            // owned by nobody, after every loop pass and past the clip's stop.
            if (!v->releasing)
                release(*v);
            v->owner = -1;
        }
        const int vi = allocate(snd);
        line.voice = vi;
        start(m_voices[size_t(vi)], li, snd, n, ctx);
    }

    void noteOff(int li, int id, AudioContext &ctx) {
        Line &line = m_lines[size_t(li)];
        const NoteIn *was = line.top();
        const bool wasTop = was && was->id == id;
        if (wasTop)
            line.last = *was;
        if (!removeNote(line, id))
            return;
        Voice *v = line.voice >= 0 ? &m_voices[size_t(line.voice)] : nullptr;
        if (!v || !v->active || v->owner != li)
            return;
        if (line.count == 0) {
            line.offAt = m_clock;
            release(*v);
        } else if (wasTop) {
            // A pull-off: back to the note still held, gliding, no new attack
            // (a guitar's string is plucked by the finger leaving it).
            v->tau = glideTime(*v, line, *line.top(), sound(v->sound).glide, ctx);
            if (sound(v->sound).model == Sound::Guitar)
                for (auto &k : v->banks)
                    if (k.guitarLive)
                        k.guitar.pullOff(pickForce(v->vel));
        }
    }

    // A player's glide: quick across a step, so trills and runs stay
    // crisp, and slower across a leap, so a jump of a fifth or an octave
    // is heard as a swoop.
    static float glideTime(const Voice &v, const Line &line, const NoteIn &to, float base, AudioContext &ctx) {
        float pitch[Voice::kBanks], level[Voice::kBanks];
        targetsFor(line, to, ctx.harmony(), pitch, level);
        const float interval = std::fabs(pitch[0] - v.banks[0].pitch);
        // A step (one or two semitones) takes about a sixth of the sound's
        // glide, so it lands within ten cents in thirty milliseconds; a
        // fifth takes all of it, an octave more. A swooping sound (Scream)
        // keeps its slur even across steps.
        const float shape = std::pow(interval / 7.0f, 1.4f);
        return base * line.glideScale * clampf(shape, sound(v.sound).swoop ? 0.45f : 0.15f, 1.6f);
    }

    // An erhu's line joined across more than a whole tone slides: the
    // finger travels the string to the new note (a huayin) instead of the
    // pitch simply gliding there.
    static void huayin(Voice &v, const Line &line, const NoteIn &to, AudioContext &ctx) {
        if (sound(v.sound).model != Sound::Bow)
            return;
        float pitch[Voice::kBanks], level[Voice::kBanks];
        targetsFor(line, to, ctx.harmony(), pitch, level);
        const float interval = std::fabs(pitch[0] - v.banks[0].pitch);
        if (interval <= kSlideOver)
            return;
        v.slide = 0.6f;
        v.slideSize = interval;
    }

    static bool removeNote(Line &line, int id) {
        for (int i = 0; i < line.count; ++i)
            if (line.notes[i].id == id) {
                for (int j = i; j + 1 < line.count; ++j)
                    line.notes[j] = line.notes[j + 1];
                --line.count;
                return true;
            }
        return false;
    }

    int allocate(int snd) {
        // A percussionist has a few tam-tams, not eight: a fifth note
        // takes the quietest of the four already ringing that nobody holds.
        if (sound(snd).model == Sound::Gong && sound(snd).gong == gong::TamTam) {
            int ringing = 0, best = -1;
            float quiet = 1e9f;
            for (size_t i = 0; i < m_voices.size(); ++i) {
                const Voice &v = m_voices[i];
                if (!v.active || v.sound != snd)
                    continue;
                ++ringing;
                if ((v.owner < 0 || v.releasing) && v.level < quiet) {
                    quiet = v.level;
                    best = int(i);
                }
            }
            if (ringing >= kTamTams && best >= 0)
                return best;
        }
        for (size_t i = 0; i < m_voices.size(); ++i)
            if (!m_voices[i].active)
                return int(i);
        // Steal the quietest voice nobody holds, else the oldest.
        int best = -1;
        float quiet = 1e9f;
        for (size_t i = 0; i < m_voices.size(); ++i)
            if ((m_voices[i].owner < 0 || m_voices[i].releasing) && m_voices[i].level < quiet) {
                quiet = m_voices[i].level;
                best = int(i);
            }
        if (best >= 0)
            return best;
        uint64_t oldest = ~0ull;
        for (size_t i = 0; i < m_voices.size(); ++i)
            if (m_voices[i].born < oldest) {
                oldest = m_voices[i].born;
                best = int(i);
            }
        return std::max(best, 0);
    }

    void start(Voice &v, int li, int snd, const NoteIn &n, AudioContext &ctx) {
        const Sound &s = sound(snd);
        if (v.active) {
            // A stolen voice: its line lets go of it, and where its wave
            // stood is eased back to zero over a few milliseconds instead of
            // stepping there.
            if (v.owner >= 0 && v.owner != li)
                m_lines[size_t(v.owner)].voice = -1;
            m_declickL += v.lastL;
            m_declickR += v.lastR;
        }
        v.lastL = v.lastR = 0;
        v.active = true;
        v.releasing = false;
        v.owner = li;
        v.from = li;
        v.sound = snd;
        v.born = ++m_age;
        v.vel = n.vel;
        v.tau = s.glide;
        v.y = v.ySet = n.y;
        v.press = v.pressSet = n.press;
        v.fat = v.fatSet = n.fat;
        v.motion = v.motionSet = n.motion;
        v.held = 0;
        v.vibPhase = 0;
        v.pwmPhase = v.rng.unit();
        v.fL.reset();
        v.fR.reset();
        for (auto &os : v.ladOs) {
            std::fill(std::begin(os.up), std::end(os.up), 0.0f);
            std::fill(std::begin(os.down), std::end(os.down), 0.0f);
        }
        for (auto &ch : v.topLp) {
            ch[0].reset();
            ch[1].reset();
            ch[0].set(19000.0f, 0.54f, m_rate);
            ch[1].set(19000.0f, 1.31f, m_rate);
        }
        for (auto &ch : v.form)
            for (auto &f : ch)
                f.reset();
        v.airLp.y = 0;
        v.amp.v = v.filt.v = 0;
        v.amp.stage = v.filt.stage = Env::Idle;
        v.level = 0;
        // Every bank starts on its own pitch: no glide into a first note.
        const Line &line = m_lines[size_t(li)];
        float targets[Voice::kBanks], levels[Voice::kBanks];
        targetsFor(line, n, ctx.harmony(), targets, levels);
        for (int b = 0; b < Voice::kBanks; ++b) {
            Bank &k = v.banks[b];
            k.pitch = k.target = targets[b];
            k.pitchVel = 0;
            k.level = k.want = levels[b];
            for (auto &o : k.saw)
                o.phase = v.rng.unit();
            k.pulse[0].phase = v.rng.unit();
            k.pulse[1].phase = v.rng.unit();
            k.car[0] = k.car[1] = k.mod[0] = k.mod[1] = 0;
            k.layer.phase = v.rng.unit();
            k.layer2.phase = v.rng.unit();
            k.sub.phase = 0;
            k.breath.reset();
            if (s.model == Sound::String || s.model == Sound::Bow)
                k.string.clear();
            if (s.model == Sound::Bow)
                k.bridge.clear();
            k.strLp = 0;
            k.pluck = 0;
            k.pluckLp.y = 0;
            k.wasLive = levels[b] > 0;
            k.jawF.reset();
            k.jawN.reset();
            k.swing = k.swingTop = 0;
            k.jawSweep = 0;
            for (int m = 0; m < kModes; ++m) {
                k.modeE[m] = k.modeF[m] = k.modeS[m] = k.modeA[m] = 0;
                k.modePh[m][0] = v.rng.unit();
                k.modePh[m][1] = v.rng.unit();
            }
            // (Fixed phases, so no other sound's random numbers move.)
            for (int j = 0; j < 2; ++j) {
                k.haloE[j] = k.haloA[j] = 0;
                k.haloPh[j] = 0.25f * float(j + 1);
            }
            k.bloom = k.thud = k.settle = k.ping = 0;
            k.pingLeft = 0;
            k.gongLive = false;
            if (s.model == Sound::Gong)
                k.gong.silence();
            k.trombaLive = false;
            k.trombaFor = -100;
            if (s.model == Sound::TrombaMarina)
                k.tromba.silence();
            if (s.model == Sound::Dax)
                k.tongue.silence();
            k.guitarLive = false;
            k.fret = k.anchor = k.rested = targets[b];
            if (s.model == Sound::Kargyraa)
                k.kg.reset();
            k.saxLive = false;
            k.thudLp.reset();
            k.pingF.reset();
            k.pingLp.reset();
            for (auto &o : k.sync)
                o = SyncOsc{};
            for (auto &o : k.sync2)
                o = SyncOsc{};
            if (s.model == Sound::Sync || s.model == Sound::Bow) {
                // Its half-rate filters' memory, so a note never starts
                // with the end of another one's.
                for (auto *os : {&k.osL, &k.osR}) {
                    std::fill(std::begin(os->down), std::end(os->down), 0.0f);
                    std::fill(std::begin(os->up), std::end(os->up), 0.0f);
                }
            }
            // (Three draws, as there were three reeds before the fourth
            // came, so every other sound's oscillators start where they did.)
            for (int o = 0; o < 3; ++o)
                k.reeds[o].phase = v.rng.unit();
            // The octave reed starts in step with the middle one's second
            // harmonic, so the pair speaks as one and the octave is heard.
            k.reeds[3].phase = 2.0f * k.reeds[0].phase + 0.5f - std::floor(2.0f * k.reeds[0].phase + 0.5f);
        }
        v.rollLate = 0;
        v.rollHand = 0;
        v.rollAge = 0;
        v.rollSlot = -1;
        v.symBoost = 1;
        v.syncLfo = float(m_age % 7) / 7.0f; // somewhere new each note, without a draw from the voice's own random numbers
        v.syncWob = 0;
        v.bowDir = v.bowDirS = 1;
        v.bowPos = 0;
        v.bowArc = bowArcAt(0);
        v.bowWas[0] = -1;
        v.vibRateJ = v.vibDepthJ = 1;
        v.vibW = 0;
        v.slide = 0;
        v.slideDip = 1;
        v.daxBow = v.daxLand = 0;
        for (auto &ch : v.bodyLp)
            for (auto &f : ch)
                f.reset();
        registerWeights(n.y, v.reg);
        // An accordion's bellows turn round by the phrase, as a player's
        // do. In a run, once they have drawn a bellows' worth of air the
        // next note turns them, and the reeds catch the other way with a
        // swell; a run of short notes inside that never turns them, so its
        // tremolo and tuning stay put note after note. After a rest of a
        // breath or more, bellows half spent are turned in the silence,
        // unheard, so the next phrase starts on fresh air.
        Line &ln = m_lines[size_t(li)];
        bool turn = false;
        if (s.model == Sound::Reed) {
            if (m_clock - ln.offAt > int64_t(kBreathS * m_rate)) {
                if (ln.air > 0.5f * kBellowsAir) {
                    ln.bellows = -ln.bellows;
                    ln.air = 0;
                }
            } else if (ln.air >= kBellowsAir) {
                ln.bellows = -ln.bellows;
                ln.air = 0;
                turn = true;
            }
        }
        v.bellows = ln.bellows;
        v.jerk = turn ? 1.0f : 0.0f;
        // Each note begins somewhere in its drift, as an old synth's would.
        // (Only a drifting sound draws it, so the others' phases stay as they were.)
        for (int o = 0; o < Voice::kDrifts; ++o) {
            v.drift[o] = v.driftTo[o] = s.drift > 0 ? v.rng.bipolar() : 0.0f;
            v.driftLeft[o] = 0;
        }
        v.cut = cutoffFor(v, s, 1.0f, line.tone);
        strike(v, n.vel, true);
    }

    // Starts the envelopes from wherever they are, so a struck legato note
    // or a portamento restart never clicks.
    void strike(Voice &v, float vel, bool scoop) {
        const Sound &s = sound(v.sound);
        // A hard press tongues a quicker attack, if the sound has one.
        v.amp.set(lerpf(s.a, s.aHard, hardness(v, s)), s.d, s.s, s.r, m_rate);
        v.filt.set(0.001f, s.envDecay, 0.0f, 0.3f, m_rate);
        v.amp.gate(true);
        v.filt.gate(true);
        v.fmEnv = 1.0f;
        v.chiff = v.chiffFast = 1.0f;
        v.syncEnv = v.syncEnv2 = 1.0f;
        // A string is plucked, a pan struck, in every voice of the line.
        for (int b = 0; b < Voice::kBanks; ++b) {
            Bank &k = v.banks[b];
            k.pluck = -1; // its length is worked out at its own pitch, when it sounds
            if (s.model == Sound::Gong && (b == 0 || k.want > 0 || k.level > 0))
                gongStrike(v, k, b, vel, 1.0f);
            if (s.model == Sound::Guitar && (b == 0 || k.want > 0 || k.level > 0))
                guitarPick(v, k, vel);
            if (s.model == Sound::Pan)
                hit(k, vel, v.y, 1.0f, false);
            if (s.model == Sound::Reed)
                k.thud = 0.6f + 0.4f * vel; // the key's click
            if (s.model == Sound::Kargyraa)
                k.kg.start(vel > 0.9f, v.rng); // from silence, a new phrase
            // A detached note on a horn still sounding: the tongue stops
            // the reed and lets it go again.
            if (s.model == Sound::Sax && k.saxLive)
                k.sax.tongue(vel);
        }
        // A bowed note struck again while it still sounds is the bow
        // changing direction: its speed turns through nothing, the hair
        // loses the string for a moment and catches it again. The finger
        // is already on the string, so it doesn't scoop into the note
        // from under it (that would step the string's length at once, a
        // tick): it slides there from the last.
        if (((s.model == Sound::Bow || s.model == Sound::TrombaMarina) && v.amp.v > 0.05f) || (s.model == Sound::Dax && v.daxBow > 0.05f)) {
            v.bowDir = -v.bowDir;
            v.bowPos = 0;
            scoop = false;
        }
        if (scoop)
            v.scoop = -s.scoop * (0.7f + 0.6f * vel);
        v.vel = vel;
    }

    // A guitar's pick force from a note's velocity: the pad's touch lands
    // between 0.6 and 1 (the keys 0.75, or 1 accented), and the guitar's
    // touch is all of its range.
    static float pickForce(float vel) { return clampf((vel - 0.6f) / 0.4f, 0.05f, 1.0f); }
    // A flat finger is a palm on the strings.
    static float palmOf(const Voice &v) { return clampf((v.fat - 0.4f) / 0.45f, 0.0f, 1.0f); }

    // A guitar picked: a fresh string, the one its note lies on, picked as
    // hard as the finger landed and as far toward the bridge as it is high
    // on the pad.
    void guitarPick(Voice &v, Bank &k, float vel) {
        k.guitar.silence();
        k.guitar.start(k.pitch);
        k.guitar.setPitch(midiToHz(clampf(k.pitch, 0.0f, 140.0f)), 0);
        k.guitar.setMute(palmOf(v), true);
        k.guitar.pluck(pickForce(vel), clampf(v.y, 0.0f, 1.0f));
        k.guitarLive = true;
    }

    // A gong struck: hung (tuned to its voice's pitch) if it isn't yet,
    // then the mallet lands. Where is the finger's height: the lower half
    // of the pad is the middle (the boss, the dome), higher strikes further
    // out toward the rim. A flatter finger is a softer, fatter beater. Each
    // voice of a line strikes its own gong at its own place round it.
    void gongStrike(Voice &v, Bank &k, int b, float vel, float stroke) {
        const Sound &s = sound(v.sound);
        const float hz = clampf(midiToHz(clampf(k.pitch, 0.0f, 140.0f)) * std::exp2(s.gongOct), 20.0f, 9000.0f);
        if (!k.gongLive) {
            k.gong.start(gong::Kind(s.gong), hz);
            k.gongLive = true;
        } else {
            k.gong.setPitch(hz);
        }
        const float out = clampf((v.y - 0.5f) / 0.5f, 0.0f, 1.0f);
        const float where = 0.03f + 0.67f * out * out * (3.0f - 2.0f * out);
        const float angle = 0.7f + 1.37f * float(b) + 0.4f * float(v.rollHand);
        k.gong.strike(clampf(vel * stroke, 0.0f, 1.0f), where, angle, 0, clampf(v.fat, 0.0f, 1.0f));
    }

    // A pan's note struck. The stick's first stroke gives every mode the
    // energy it brings, the upper ones more of it the harder and higher on
    // the pad it lands, and starts the bloom and the settle. A roll's
    // strokes strike the ringing note again: each gives the modes a struck
    // part that dies within the stroke, on top of what rings on, so the
    // note pulses with the sticks; the ringing part is drawn toward the
    // roll's own level rather than simply topped up.
    static void hit(Bank &k, float vel, float y, float stroke, bool roll) {
        const float hard = clampf(0.45f * vel + 0.45f * y + 0.1f, 0.0f, 1.0f) * (roll ? 0.7f + 0.3f * stroke : 1.0f);
        // A low note area is a cello pan's: its octave and twelfth are
        // tuned in strong and speak at once.
        const float low = clampf((64.0f - k.pitch) / 12.0f, 0.0f, 1.5f);
        const float lift[kModes] = {1.0f - 0.2f * low, 1.0f + 0.8f * low, 1.0f + 0.45f * low, 1.0f + 0.3f * low, 1.0f};
        for (int m = 0; m < kModes; ++m) {
            const float want = kModeLevel[m] * lift[m] * stroke * vel * std::pow(0.35f + 0.65f * hard, float(m));
            if (roll) {
                k.modeE[m] = 0.85f * k.modeE[m] + 0.25f * want;
                k.modeF[m] += want;
            } else {
                k.modeE[m] = std::max(k.modeE[m], want) + 0.1f * std::min(k.modeE[m], want);
                k.modeF[m] = 0;
            }
        }
        // The note's neighbours on the pan, its octave and its fifth, are
        // set ringing by it through the steel: a halo a little under it
        // and a little off its own partials, louder the harder the stroke.
        for (int j = 0; j < 2; ++j) {
            const float want = kHaloLevel[j] * stroke * vel * (0.6f + 0.4f * hard);
            k.haloE[j] = roll ? 0.9f * k.haloE[j] + 0.2f * want : std::max(k.haloE[j], want);
        }
        if (!roll) {
            k.bloom = 1.0f - 0.5f * clampf(low, 0.0f, 1.0f);
            // Only the first stroke thuds: a roll's strokes are the tip
            // bouncing on a note already ringing.
            k.pluck = -1;
            k.thud = vel;
            k.settle = 1.0f + 1.2f * low;
            // A firm stroke high on the note lands on the stick's harder
            // core through the rubber: a bright ping of a few milliseconds
            // in the presence band, which a soft one hasn't, so a pan
            // played out cuts through horns.
            k.ping = vel * clampf((hard - 0.55f) / 0.35f, 0.0f, 1.0f);
            k.pingLeft = k.ping > 0 ? -1 : 0;
        }
    }

    void release(Voice &v) {
        v.releasing = true;
        v.amp.gate(false);
        v.filt.gate(false);
        // A guitar's note let go: the fretting hand mutes the string.
        if (sound(v.sound).model == Sound::Guitar)
            for (auto &k : v.banks)
                k.guitar.damp(sound(v.sound).hand);
        // A gong let go has a hand laid on it.
        if (sound(v.sound).model == Sound::Gong)
            for (auto &k : v.banks)
                k.gong.mute(sound(v.sound).hand);
        // An accordion's key coming back up closes its pallet: a softer click.
        if (sound(v.sound).model == Sound::Reed) {
            v.banks[0].pluck = -1;
            v.banks[0].thud = 0.3f;
        }
    }

    // --- pitch ---

    static bool chordTone(int degree, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        for (int k = 0; k < h.chordCount; ++k)
            if (((degree - h.chordTones[k]) % per + per) % per == 0)
                return true;
        return false;
    }

    // The chord tone nearest a degree, by ear (in semitones); ties go down.
    static int nearestChordTone(int degree, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        int best = degree;
        double bestDist = 1e9;
        for (int d = degree - per; d <= degree + per; ++d) {
            if (!chordTone(d, h))
                continue;
            const double dist =
                std::fabs(h.tuning.degreeSemis(d) - h.tuning.degreeSemis(degree)) + (d > degree ? 0.01 : 0.0);
            if (dist < bestDist) {
                bestDist = dist;
                best = d;
            }
        }
        return best;
    }

    // The chord tone `rank` places above a degree (rank < 0: below).
    static int chordToneFrom(int degree, int rank, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        int d = degree, found = 0;
        const int step = rank > 0 ? 1 : -1;
        for (int guard = 0; guard < 4 * per && found < std::abs(rank); ++guard) {
            d += step;
            found += chordTone(d, h);
        }
        return found == std::abs(rank) ? d : degree + 2 * rank;
    }

    // Where a harmony row puts its voice, in degrees, for a lead degree.
    static float harmonyDegree(int lead, float row, bool follow, const Harmony &h) {
        const int per = h.tuning.degreesPerOctave();
        auto at = [&](int r) -> float {
            if (!follow || r == 0 || r == kRows - 1)
                return float(lead) + rowDegrees(r, per);
            // On the chord: the next chord tone up, the one after, or the
            // one just below put up an octave (a sixth over a chord tone).
            if (r == 1)
                return float(chordToneFrom(lead, 1, h));
            if (r == 2)
                return float(chordToneFrom(lead, 2, h));
            return float(chordToneFrom(lead, -1, h) + per);
        };
        const int r0 = std::clamp(int(std::floor(row)), 0, kRows - 1);
        const int r1 = std::min(r0 + 1, kRows - 1);
        const float f = row - float(r0);
        return at(r0) + (at(r1) - at(r0)) * f;
    }

    // Each bank's target pitch (MIDI) and level for a line's note, and
    // where the line and its harmony sing in degrees, for the page.
    struct Targets {
        float leadDegree = 0; // where the line sings, in degrees, for the page
        float harmDegree[kHarmSlots] = {};
    };
    // (fret and anchor, if asked for: each bank's note without the finger's
    // continuous offsets, and with its twist and bend but without its slide.)
    static Targets targetsFor(const Line &line, const NoteIn &n, const Harmony &h, float *pitch, float *level,
                              float *fret = nullptr, float *anchor = nullptr) {
        Targets t;
        const int per = h.tuning.degreesPerOctave();
        const int abs = n.octave * per + n.degree;
        const int tgt = n.follow ? nearestChordTone(abs, h) : abs;
        // Resting, the note sits on its degree (or the chord tone); moving,
        // the pitch follows the finger. The twist and the bend wheel add steps.
        // A daxophone glides wherever the finger is, the dax sliding with
        // it, unless the line follows the chord: then it rests in the key
        // as every other lead does.
        const float motion = sound(line.sound).model == Sound::Dax && !n.follow ? 1.0f : n.motion;
        const float cont = (float(abs) + n.off - float(tgt)) * motion + n.twist + line.bend;
        const float base = float(h.tuning.baseMidi()) + 12.0f * line.octave;
        pitch[0] = base + float(h.tuning.continuousSemis(double(tgt) + double(cont)));
        level[0] = 1.0f;
        t.leadDegree = float(tgt) + cont + float(per) * line.octave;
        const float bend = n.twist + line.bend;
        if (fret) {
            fret[0] = base + float(h.tuning.continuousSemis(double(tgt)));
            anchor[0] = base + float(h.tuning.continuousSemis(double(tgt) + double(bend)));
        }
        for (int s = 0; s < kHarmSlots; ++s) {
            const float hd0 = harmonyDegree(tgt, line.harmRow[s], n.follow, h), hd = hd0 + cont;
            pitch[1 + s] = base + float(h.tuning.continuousSemis(double(hd)));
            level[1 + s] = line.harmLevel[s];
            t.harmDegree[s] = hd + float(per) * line.octave;
            if (fret) {
                fret[1 + s] = base + float(h.tuning.continuousSemis(double(hd0)));
                anchor[1 + s] = base + float(h.tuning.continuousSemis(double(hd0) + double(bend)));
            }
        }
        return t;
    }

    // How much of the filter envelope a touch gets: all of it, unless the
    // sound waits on pressure, when a light finger gets almost none.
    static float envTouch(const Voice &v, const Sound &s) {
        const float k = clampf((v.press - 0.25f) / 0.6f, 0.0f, 1.0f);
        return 1.0f - s.envPress + s.envPress * k;
    }

    // How hard a sound with a quicker hard attack is being pressed, 0..1.
    static float hardness(const Voice &v, const Sound &s) {
        return s.aHard > 0 ? clampf((v.press - 0.45f) / 0.45f, 0.0f, 1.0f) : 0.0f;
    }

    static float cutoffFor(const Voice &v, const Sound &s, float fe, int tone) {
        const float note = v.banks[0].pitch;
        float c = s.cutoff + s.cutoffY * (2.0f * v.y - 1.0f) + 0.16f * (v.press - 0.5f) +
                  s.envAmt * fe * (0.6f + 0.5f * v.vel) * envTouch(v, s);
        c = c * 8.1f + s.keyTrack * (note - 72.0f) / 12.0f + float(tone) * kToneOctaves;
        return clampf(c, 0.5f, 8.6f);
    }

    // --- sound ---

    void renderSegment(float *l, float *r, int at, int n, AudioContext &ctx) {
        const Harmony &h = ctx.harmony();
        m_rootHz = midiToHz(h.midi(0, 0)); // Kargyraa: the growl prefers the key's root
        const float dt = float(n) / m_rate;
        // The tromba marina's string is tuned to the key: its root two
        // octaves under the neck's (G1 to F♯2).
        {
            double root = h.tuning.baseMidi() + h.tuning.degreeSemis(0);
            while (root < 31.0)
                root += 12.0;
            while (root >= 43.0)
                root -= 12.0;
            m_trombaRef = midiToHz(root);
        }
        // Lines drive their voices.
        for (int li = 0; li < kLines; ++li) {
            Line &line = m_lines[size_t(li)];
            if (line.voice < 0)
                continue;
            Voice &v = m_voices[size_t(line.voice)];
            if (!v.active || v.owner != li)
                continue;
            const NoteIn *top = line.top();
            if (top)
                line.last = *top;
            const NoteIn &note = line.last;
            float targets[Voice::kBanks], levels[Voice::kBanks], frets[Voice::kBanks], anchors[Voice::kBanks];
            const Targets t = targetsFor(line, note, h, targets, levels, frets, anchors);
            for (int b = 0; b < Voice::kBanks; ++b) {
                v.banks[b].target = targets[b];
                v.banks[b].want = levels[b];
                v.banks[b].fret = frets[b];
                v.banks[b].anchor = anchors[b];
            }
            v.ySet = note.y;
            v.pressSet = note.press;
            v.fatSet = note.fat;
            v.motionSet = note.motion;
            m_pubPos[li] = t.leadDegree;
            for (int s = 0; s < kHarmSlots; ++s)
                m_pubHarm[li][s] = t.harmDegree[s];
        }
        m_throw = 0;
        for (auto &line : m_lines)
            m_throw = std::max(m_throw, line.throwAmt);
        const Transport &tr = ctx.transport();
        m_spb = clampf(float(tr.secondsPerBeat()), 0.2f, 2.0f);
        for (auto &v : m_voices)
            if (v.active) {
                // The gate runs on the studio's beat, so it lands with the drums.
                const int steps = gateSteps(m_lines[size_t(v.from)].gate);
                const double phase = tr.beatAt(at) * steps;
                m_beat = tr.beatAt(at);
                renderVoice(v, l + at, r + at, m_sendL + at, m_sendR + at, m_symSend + at, n, dt, float(phase - std::floor(phase)),
                            float(tr.beatsPerSample * steps), steps > 0);
            }
        renderRigs(l + at, r + at, m_sendL + at, m_sendR + at, n);
    }

    // Guitar: every line's rigs play what their voices' pickups sent them
    // (a released note's tail and the next note's attack through the same
    // valves), into the mix and the echo, as loud whatever the touch: a
    // guitar's dynamics are the amp's. A rig left alone rings down and sleeps.
    void renderRigs(float *outL, float *outR, float *sendL, float *sendR, int n) {
        static const float bankPan[Voice::kBanks][2] = {{1.0f, 1.0f}, {1.15f, 0.82f}, {0.82f, 1.15f}, {1.0f, 1.0f}};
        float mic[kBlock];
        for (int li = 0; li < kLines; ++li)
            for (int b = 0; b < Voice::kBanks; ++b) {
                guitar::Rig &rig = m_rig[size_t(li)][size_t(b)];
                const bool fed = m_rigFed[li][b];
                if (!fed && !rig.live())
                    continue;
                rig.process(m_rigIn[li][b], mic, nullptr, n, fed);
                std::fill(m_rigIn[li][b], m_rigIn[li][b] + n, 0.0f);
                m_rigFed[li][b] = false;
                const Sound &s = sound(m_rigSound[li]);
                // (Harmony voices about five decibels under the melody, as the others.)
                const float g = kGuitarLevel * s.gain * 0.25f * (b == 0 ? 1.0f : 0.65f);
                const float send = s.echo + 0.9f * m_lines[size_t(li)].throwAmt;
                for (int i = 0; i < n; ++i) {
                    const float o = mic[i] * g;
                    outL[i] += o * bankPan[b][0];
                    outR[i] += o * bankPan[b][1];
                    sendL[i] += o * bankPan[b][0] * send;
                    sendR[i] += o * bankPan[b][1] * send;
                }
            }
    }

    void renderVoice(Voice &v, float *outL, float *outR, float *sendL, float *sendR, float *sym, int n, float dt,
                     float gatePhase, float gateInc, bool gated) {
        const Sound &s = sound(v.sound);
        const float rate = m_rate;
        // Expression follows the finger closely but never steps.
        const float ex = 1.0f - std::exp(-dt / 0.012f);
        v.y += (v.ySet - v.y) * ex;
        v.press += (v.pressSet - v.press) * ex;
        v.fat += (v.fatSet - v.fat) * (1.0f - std::exp(-dt / 0.05f));
        v.motion += (v.motionSet - v.motion) * ex;
        v.held += dt;

        // Pitch: a glide that tightens while the finger moves, so slides
        // and wiggles are heard as played and a new note still glides in.
        const float tau = lerpf(std::max(v.tau, 0.004f), 0.008f, v.motion);
        const float g = 1.0f - std::exp(-dt / tau);
        // Vibrato fades in on a held note, deeper as the finger leans in,
        // and gives way to the player's own wiggle.
        const float fade = clampf((v.held - s.vibDelay) / 0.5f, 0.0f, 1.0f);
        const float lean = clampf((v.press - 0.3f) / 0.5f, 0.0f, 1.4f);
        float vibShape = fade * (0.3f + 0.7f * lean) * (1.0f - v.motion), vibShape0 = vibShape;
        float vibRate = s.vibRate;
        if (s.model == Sound::Bow) {
            // An erhu player's vibrato is never a clock. It comes late and
            // narrow, then widens and quickens as the bow leans into the
            // middle of its stroke (from about five cycles a second to six
            // and a half), and eases with it toward the bow's end; no two
            // cycles are quite the same speed or width. The hand's width is
            // followed over a few tens of milliseconds, so a note played
            // again, whose vibrato starts over, lets the old one go rather
            // than dropping it, which would kick the string's length.
            const float grow = std::pow(clampf((v.held - s.vibDelay) / 0.75f, 0.0f, 1.0f), 1.5f);
            const float lift = clampf((v.bowArc - 0.78f) / 0.3f, 0.0f, 1.0f);
            const float want = grow * (0.5f + 0.5f * lift) * v.vibDepthJ * (0.3f + 0.7f * lean) * (1.0f - v.motion);
            // (Its width slides across the block, from where the last one
            // left it: a width stepped between blocks steps the pitch, and
            // the string's length with it, a tick every block.)
            vibShape0 = v.vibW;
            v.vibW += (want - v.vibW) * (1.0f - std::exp(-dt / 0.08f));
            vibShape = v.vibW;
            vibRate = s.vibRate * (1.0f + 0.3f * grow * (0.35f + 0.65f * lift)) * v.vibRateJ;
        }
        // A guitarist's vibrato is no clock either: each cycle its own speed
        // and width (it changes at the cycle's foot, on the fret's pitch).
        if (s.model == Sound::Guitar)
            vibRate *= v.vibRateJ;
        const float vibDepth = s.vibDepth * vibShape;
        const float sin0 = sine()(v.vibPhase);
        const float ph0 = v.vibPhase;
        v.vibPhase += vibRate * dt;
        if (v.vibPhase >= 1.0f && (s.model == Sound::Bow || s.model == Sound::Guitar)) {
            // A new cycle, at the sine's zero crossing: its own speed and
            // width, a few per cent either way.
            v.vibRateJ = 1.0f + 0.07f * v.rng.bipolar();
            v.vibDepthJ = 1.0f + 0.18f * v.rng.bipolar();
        }
        v.vibPhase -= std::floor(v.vibPhase);
        const float sin1 = sine()(v.vibPhase);
        // An old synth's drift: each oscillator wanders a few cents on its
        // own, slowly, to a new place every half second or so, mostly well
        // off centre, so the saws' beating quickens and slows and the
        // square leans against them.
        if (s.drift > 0) {
            const float k = 1.0f - std::exp(-dt / 0.4f);
            for (int o = 0; o < Voice::kDrifts; ++o) {
                v.driftLeft[o] -= dt;
                if (v.driftLeft[o] <= 0) {
                    const float to = 0.5f + 0.5f * v.rng.unit();
                    v.driftTo[o] = v.rng.bipolar() < 0 ? -to : to;
                    v.driftLeft[o] = 0.3f + 0.5f * v.rng.unit();
                }
                v.drift[o] += (v.driftTo[o] - v.drift[o]) * k;
            }
        }
        // A guitarist's vibrato is a bend rocked to and fro: it only ever
        // raises the string's tension, so it swings above the note, from the
        // fret's pitch up and back, lingering at the top of each push.
        float vib0 = s.vibDepth * vibShape0 * sin0, vib1 = vibDepth * sin1;
        if (s.model == Sound::Guitar) {
            // (Leaning in widens it to some 70 cents, no further: its middle
            // stands above the note, and a wider one is heard sharp.)
            vibShape0 = std::min(vibShape0, 1.0f);
            vibShape = std::min(vibShape, 1.0f);
            auto push = [](float ph) {
                const float w = 0.5f - 0.5f * sine()(ph + 0.25f - std::floor(ph + 0.25f));
                return 2.0f * (1.0f - std::pow(1.0f - w, 1.6f));
            };
            vib0 = s.vibDepth * vibShape0 * v.vibDepthJ * push(ph0);
            vib1 = s.vibDepth * vibShape * v.vibDepthJ * push(v.vibPhase);
        }
        // A wind player's vibrato is mostly in the breath: the level swells with it.
        float amp0 = 1.0f + s.vibAmp * vibShape0 * sin0, amp1 = 1.0f + s.vibAmp * vibShape * sin1;
        const float scoop0 = v.scoop;
        v.scoop *= std::exp(-dt / std::max(s.scoopTime, 0.005f));
        // The puff of a tongued note: up in a few milliseconds, gone in
        // fifty (the difference of a slow and a fast fall, peaking at 1).
        const float chiff0 = 1.48f * (v.chiff - v.chiffFast);
        v.chiff *= std::exp(-dt / 0.045f);
        v.chiffFast *= std::exp(-dt / 0.005f);
        const float chiff1 = 1.48f * (v.chiff - v.chiffFast);
        if (v.owner >= 0)
            m_pubVib[v.owner] = vibDepth;

        float inc0[Voice::kBanks], inc1[Voice::kBanks], gain0[Voice::kBanks], gain1[Voice::kBanks];
        bool live[Voice::kBanks];
        for (int b = 0; b < Voice::kBanks; ++b) {
            Bank &k = v.banks[b];
            const float p0 = k.pitch + vib0 + scoop0;
            if (v.slide > 0) {
                // A huayin: the finger slides along the string, carried
                // a little past the note by its own momentum and settling
                // back onto it, a spring rather than a glide.
                const float acc = kSlideW * kSlideW * (k.target - k.pitch) - 2.0f * kSlideZeta * kSlideW * k.pitchVel;
                k.pitchVel += acc * dt;
                k.pitch += k.pitchVel * dt;
            } else {
                k.pitch += (k.target - k.pitch) * g;
                k.pitchVel = 0;
            }
            const float p1 = k.pitch + vib1 + v.scoop;
            inc0[b] = midiToHz(p0) / rate;
            inc1[b] = midiToHz(p1) / rate;
            const float l0 = k.level;
            k.level += (k.want - k.level) * (1.0f - std::exp(-dt / 0.035f));
            if (k.want <= 0 && k.level < 1e-3f)
                k.level = 0;
            // Harmony voices sit about five decibels under the melody.
            const float hg = b == 0 ? 1.0f : 0.65f;
            gain0[b] = l0 * hg;
            gain1[b] = k.level * hg;
            live[b] = b == 0 || l0 > 0 || k.level > 0;
        }
        // A huayin's slide is lighter than the notes it joins: the finger
        // eases its press on the string to travel, so the line dips a
        // couple of decibels with the slide's speed and comes back as it
        // lands.
        if (v.slide > 0) {
            const float fast = clampf(std::fabs(v.banks[0].pitchVel) / (0.5f * kSlideW * v.slideSize), 0.0f, 1.0f);
            amp0 *= v.slideDip;
            v.slideDip = 1.0f - 0.18f * fast;
            amp1 *= v.slideDip;
            v.slide -= dt;
            if (v.slide <= 0) {
                v.slide = 0;
                v.slideDip = 1;
            }
        }
        // Harmony makes the line bigger: each voice adds about a decibel,
        // while the melody gives up only a fraction of one and stays on top.
        {
            float h0 = 0, h1 = 0;
            for (int b = 1; b < Voice::kBanks; ++b) {
                h0 += v.banks[b].level;
                h1 += v.banks[b].level;
            }
            (void)h0;
            const float n1 = 1.0f / (1.0f + 0.04f * h1);
            for (int b = 0; b < Voice::kBanks; ++b) {
                gain0[b] *= n1;
                gain1[b] *= n1;
            }
        }

        // Unison: the outer saws furthest out in pitch and in the field. A
        // flatter finger detunes and spreads them further.
        static const float det7[Bank::kUni] = {0.0f, -1.0f, 1.0f, -0.46f, 0.46f, -0.15f, 0.15f};
        // Where each saw stands: the outer pair at the edges, the next pair
        // crossed over to the opposite sides, the inner pair half out, so
        // even a light touch is wide and no side is just the flat saws.
        static const float pan7[Bank::kUni] = {0.0f, -1.0f, 1.0f, 0.8f, -0.8f, -0.55f, 0.55f};
        static const float det2[2] = {-0.5f, 0.5f};
        const int U = std::clamp(s.unison, 1, Bank::kUni);
        const float cents = s.detune * (0.55f + 1.0f * v.fat);
        const float spread = clampf(s.spread * (0.85f + 0.3f * v.fat), 0.0f, 1.0f);
        float uniRatio[Bank::kUni], uniL[Bank::kUni], uniR[Bank::kUni];
        for (int u = 0; u < U; ++u) {
            const float d = U == 2 ? det2[u] : det7[u];
            uniRatio[u] = std::exp2((d * cents + (u < Voice::kDrifts - 1 ? s.drift * v.drift[u] : 0.0f)) / 1200.0f);
            const float pan = clampf((U == 2 ? d * 2.0f : U >= 5 ? pan7[u] : d) * spread, -1.0f, 1.0f);
            uniL[u] = std::sqrt(0.5f * (1.0f - pan));
            uniR[u] = std::sqrt(0.5f * (1.0f + pan));
        }
        const float uniGain = s.saw * 1.2f / std::sqrt(float(U));
        const float layerDet = std::exp2((4.0f + 6.0f * v.fat) / 1200.0f);
        const float pulseDet = std::exp2((2.0f + 6.0f * v.fat) / 1200.0f);
        const float fmDet = std::exp2((s.detune * (0.25f + 0.75f * v.fat)) / 1200.0f);
        v.pwmPhase += s.pwmRate * dt;
        v.pwmPhase -= std::floor(v.pwmPhase);
        const float pw = clampf(s.pw + s.pwm * sine()(v.pwmPhase), 0.06f, 0.94f);
        // The FM strike falls to its floor; a high note gets less index so
        // its sidebands stay under Nyquist.
        v.fmEnv = s.fmFloor + (v.fmEnv - s.fmFloor) * std::exp(-dt / std::max(s.fmDecay, 0.01f));
        const float hz0 = inc1[0] * rate;
        const float fmIndexMax = std::max(0.0f, 0.42f * rate / (hz0 * (s.fmRatio + 1.0f)) - 1.0f);
        // Under C5 the index grows, so low notes keep their reedy upper
        // harmonics instead of thinning to a sine.
        const float low = clampf((72.0f - v.banks[0].pitch) / 12.0f, 0.0f, 1.5f);
        const float fmIndex =
            std::min(s.fmIndex * v.fmEnv * (0.7f + 0.5f * v.vel + 0.25f * v.y) * (1.0f + s.fmLow * low), fmIndexMax);
        // The square an octave down: its own drift, thinning out from E4 to
        // gone at E5, and up with a flatter finger.
        const float subRatio = 0.5f * std::exp2(s.drift * v.drift[Voice::kDrifts - 1] / 1200.0f);
        float subGain[Voice::kBanks];
        for (int b = 0; b < Voice::kBanks; ++b)
            subGain[b] = s.sub * clampf((76.0f - v.banks[b].pitch) / 12.0f, 0.0f, 1.0f) * (0.6f + 0.8f * v.fat);

        // The filter, set once a segment from smoothed numbers.
        const float fe = v.filt.v;
        v.cut += (cutoffFor(v, s, fe, m_lines[size_t(v.from)].tone) - v.cut) * (1.0f - std::exp(-dt / 0.004f));
        const float fc = std::min(60.0f * std::exp2(v.cut), rate * (0.45f - 0.12f * s.res));
        // (The sync lead's runs at twice the rate.)
        const float ladRate = s.model == Sound::Sync ? 2.0f * rate : rate;
        v.fL.set(fc, s.res, ladRate);
        v.fR.set(fc, s.res, ladRate);
        // The breath sits in a wide band over each voice's note, never
        // under 2.5 kHz, where air is heard: a low note's breath is no
        // duller than a high one's.
        if (s.breath > 0)
            for (int b = 0; b < Voice::kBanks; ++b)
                if (live[b])
                    v.banks[b].breath.set(std::max(2.5f * inc1[b] * rate, 2500.0f), 0.9f, rate);
        // The air's level: steady under the held note, puffing on the attack.
        // It comes in ahead of the tone, so it has its own envelope rather
        // than the tone's slower one.
        // A hard-tongued note already speaks quickly: its puff is softer, so
        // a fast run doesn't spit.
        const float puff = s.chiff * (1.0f - 0.5f * hardness(v, s));
        const float air0 = puff * chiff0, air1 = puff * chiff1;
        // The air's top follows the tone keys, not the note.
        if (s.breath > 0)
            v.airLp.set(8000.0f * std::exp2(float(m_lines[size_t(v.from)].tone) * kToneOctaves), rate);
        // A flatter finger, a breathier note; and a low note takes more air.
        const float breath = s.breath * (0.7f + 0.8f * v.fat) * (1.0f + 0.28f * low);
        float formGain[3] = {};
        if (s.formant) {
            // The height walks through the vowels.
            const float pos = clampf(v.y, 0.0f, 1.0f) * 3.999f;
            const int a = int(pos);
            const float f = pos - float(a);
            // F1 wide, the upper two narrower; each band at unity on its peak.
            static const float q[3] = {4.0f, 7.0f, 9.0f};
            static const float gk[3] = {1.0f, 0.7f, 0.45f};
            float hz[3];
            for (int k = 0; k < 3; ++k) {
                hz[k] = std::exp(lerpf(std::log(kVowels[a][k]), std::log(kVowels[a + 1][k]), f));
                formGain[k] = gk[k] / q[k];
                v.form[0][k].set(hz[k], q[k], rate);
                v.form[1][k].set(hz[k], q[k], rate);
            }
            // Each vowel passes a different share of a saw's energy, so a
            // sweep would jump in level: work out that share for this note
            // (harmonic by harmonic, the upper ones weighted up the way
            // the ear weighs them) and make it up, with a little of the
            // dry saw blended in so no vowel is ever thin.
            const float f0 = std::max(inc1[0] * rate, 40.0f);
            float pDry = 0, pForm = 0;
            const int harmonics = std::min(48, int(6000.0f / f0));
            for (int h = 1; h <= harmonics; ++h) {
                const float fh = f0 * float(h);
                const float w = (fh > 1500.0f ? 2.0f : 1.0f) / float(h * h);
                float g = 0;
                for (int k = 0; k < 3; ++k) {
                    const float r = fh / hz[k], bw = r / q[k];
                    const float mag = bw / std::sqrt((1.0f - r * r) * (1.0f - r * r) + bw * bw);
                    g += gk[k] * gk[k] * mag * mag;
                }
                pDry += w;
                pForm += w * g;
            }
            const float want = clampf(std::sqrt(pDry / (pForm + kFormDry * kFormDry * pDry)), 0.25f, 6.0f);
            v.formNorm += (want - v.formNorm) * (1.0f - std::exp(-dt / 0.02f));
        }
        const float drive = s.drive;
        const float driveGain = 1.0f + 6.0f * drive;
        const float driveNorm = 1.0f / (1.0f + 1.6f * drive);
        const float resComp = 1.0f + 0.8f * s.res * s.res;
        // Pressure swells the level. A sound that also tongues quicker under
        // a hard press swells less, or its runs would come out hot; one whose
        // filter waits on pressure gets the level back that a light, dark
        // touch loses, so a whistle is as present as a quack.
        const float dark = 1.0f - envTouch(v, s);
        const float swell = (s.aHard > 0 ? 0.62f + 0.5f * v.press : 0.5f + 0.75f * v.press) * (1.0f + 0.55f * dark * dark);
        const float outGain = s.gain * 0.3f * swell * (0.45f + 0.55f * v.vel);
        const float send = s.echo + 0.9f * m_lines[size_t(v.from)].throwAmt;
        const float inv = 1.0f / float(n);

        // --- the models, set up for the segment ---
        const int model = s.model;
        const int tone = m_lines[size_t(v.from)].tone;
        // String: the loop's filter takes a little more off each harmonic
        // every time round (less, the higher the finger and the harder the
        // pluck); its loss is set so the fundamental falls 60 dB in the
        // sound's sustain (shorter up the neck), and its delay at the
        // fundamental is taken off the line's, so the string is in tune.
        float strB = 0, strG[Voice::kBanks] = {}, strTau[Voice::kBanks] = {};
        float jaw = 0, jawGain = 0, swingFall = 0, scrapeFall = 0;
        if (model == Sound::String) {
            strB = clampf(0.12f - 0.08f * v.y - 0.03f * v.vel - 0.08f * float(tone) / kToneSteps, 0.02f, 0.5f);
            for (int b = 0; b < Voice::kBanks; ++b) {
                const float w = kTwoPi * inc1[b];
                const float c = std::cos(w), sn = std::sin(w);
                const float mag = (1.0f - strB) / std::sqrt(1.0f - 2.0f * strB * c + strB * strB);
                strTau[b] = std::atan2(strB * sn, 1.0f - strB * c) / std::max(w, 1e-6f);
                const float t60 = s.sustainS * clampf(std::exp2(-(v.banks[b].pitch - 60.0f) / 24.0f), 0.3f, 1.6f);
                strG[b] = std::min(std::pow(10.0f, -3.0f / (t60 * inc1[b] * rate)) / mag, 0.9995f);
                // A harmony string coming in is plucked then, not left silent.
                Bank &k = v.banks[b];
                if (live[b] && !k.wasLive && !v.releasing)
                    k.pluck = -1;
                k.wasLive = live[b];
                // The jawari's formant. The wider the string swings, the
                // longer each period it lies along the curved bridge and
                // the shorter, sharper the kink it leaves: the buzz's
                // strongest harmonics stand high on the stroke (higher the
                // harder it is plucked) and slide down through the others
                // as the string settles onto the bridge, over most of a
                // second whatever the note's sustain (the sitar's "jiiing",
                // a stripe sweeping down through the harmonics), and
                // further as the swing dies, until it rests a couple of
                // harmonics over the note.
                if (live[b]) {
                    const float f0 = inc1[b] * rate;
                    const float lo = std::max(2.2f * f0, 700.0f);
                    const float hi = std::max(clampf(14.0f * f0, 3500.0f, 7500.0f) * (0.6f + 0.4f * v.vel), lo);
                    const float drop = 20.0f * std::log10(std::max(k.swingTop, 1e-6f) / std::max(k.swing, 1e-6f));
                    const float sweep = k.jawSweep * k.jawSweep * (3.0f - 2.0f * k.jawSweep);
                    const float jawHz = std::max(lo * std::pow(hi / lo, sweep) * std::exp2(-drop / 30.0f), lo);
                    k.jawF.set(jawHz, kJawQ, rate);
                    k.jawN.set(jawHz, 2.0f, rate);
                    k.jawSweep = std::max(0.0f, k.jawSweep - dt / kJawSweepS);
                }
            }
            // How far the bridge folds the string's wave, a share of its
            // period for each unit of swing, and how loud its formant
            // stands. A flatter finger buzzes more.
            jaw = s.jawari * 0.3f * (0.75f + 0.5f * v.fat);
            jawGain = s.jawari * 11.0f * (0.8f + 0.4f * v.fat) / kJawQ;
            swingFall = std::exp(-1.0f / (0.03f * rate));
            scrapeFall = std::exp(-1.0f / (0.006f * rate));
        }
        // Pan: each mode's ring, per sample (the high notes ring shorter,
        // as small note areas do), its two sines a hair apart so it
        // shimmers (the fundamental's twin nearer and weaker on a low
        // note, so a cello pan's note is defined, not smeared), the bloom's
        // rise, and the settle onto pitch. A held note is rolled: strokes
        // about thirteen a second, never quite even, the two sticks a little
        // unequal, harder the harder the press.
        float modeDec[kModes] = {}, split[kModes] = {}, twin[kModes] = {}, fastDec = 0, bloomDec = 0, smoothA = 0;
        float sharp0 = 1, sharp1 = 1, haloDec[2] = {}, haloRise = 0;
        if (model == Sound::Pan) {
            const float shorter = clampf(std::exp2(-(v.banks[0].pitch - 64.0f) / 18.0f), 0.35f, 1.5f);
            const float low = clampf((64.0f - v.banks[0].pitch) / 12.0f, 0.0f, 1.0f);
            for (int m = 0; m < kModes; ++m) {
                modeDec[m] = std::exp(-6.9078f / (s.sustainS * kModeRing[m] * shorter * rate));
                split[m] = (0.35f + 0.25f * float(m)) / rate;
                twin[m] = 0.35f;
            }
            split[0] *= 1.0f - 0.6f * low;
            twin[0] *= 1.0f - 0.6f * low;
            fastDec = std::exp(-1.0f / (0.028f * rate));
            bloomDec = std::exp(-1.0f / (0.018f * rate));
            for (int j = 0; j < 2; ++j)
                haloDec[j] = std::exp(-6.9078f / (s.sustainS * kHaloRing[j] * shorter * rate));
            // A neighbour isn't struck: it takes a few tens of
            // milliseconds to come up through the steel.
            haloRise = 1.0f - std::exp(-1.0f / (0.03f * rate));
            // Two stages, so a stroke's rise is a rounded contact of a couple
            // of milliseconds, never a step: no tick across the spectrum.
            smoothA = 1.0f - std::exp(-1.0f / (0.0024f * rate));
            {
                Bank &k = v.banks[0];
                sharp0 = std::exp2(s.settle * k.settle / 12.0f);
                for (auto &kb : v.banks)
                    kb.settle *= std::exp(-dt / 0.07f);
                sharp1 = std::exp2(s.settle * k.settle / 12.0f);
            }
            // The roll keeps the studio's time: it begins once the note
            // has been held an eighth, and its strokes run on the beat's
            // subdivision nearest a panist's thirteen a second (sixteenths
            // at a fast tempo, sixteenth triplets, thirty-seconds, or
            // thirty-second triplets at a slow one), on the studio's own
            // grid, so the stroke on the beat lands with the drums, each
            // a little off that grid, never drifting from it.
            const float spb = m_spb;
            int per = 4;
            for (int p : {6, 8, 12})
                if (std::fabs(float(p) / spb - 13.0f) < std::fabs(float(per) / spb - 13.0f))
                    per = p;
            // A player swells into a roll, its first strokes lighter, over
            // a few tenths of a second, and leans on the strokes that land
            // on the studio's beat.
            if (s.roll > 0 && !v.releasing && v.held > 0.5f * spb) {
                const double grid = m_beat * double(per);
                if (v.rollSlot < 0)
                    v.rollSlot = int64_t(std::floor(grid));
                v.rollAge += dt;
                const int64_t slot = int64_t(std::floor(grid - double(v.rollLate)));
                if (slot > v.rollSlot) {
                    v.rollSlot = slot;
                    v.rollHand ^= 1;
                    const float firm = clampf((v.press - 0.15f) / 0.7f, 0.0f, 1.0f);
                    const float into = clampf(v.rollAge / kRollSwellS, 0.0f, 1.0f);
                    const float swell = 0.45f + 0.55f * into * into * (3.0f - 2.0f * into);
                    const bool onBeat = slot % per == 0;
                    const float stroke = s.roll * (0.25f + 0.45f * firm) * (v.rollHand ? 0.88f : 1.0f) *
                                         (1.0f + 0.15f * v.rng.bipolar()) * swell * (onBeat ? kRollAccent : 1.0f);
                    v.rollLate = 0.1f * v.rng.bipolar();
                    for (int b = 0; b < Voice::kBanks; ++b)
                        if (live[b])
                            hit(v.banks[b], v.vel, v.y, stroke, true);
                }
            }
        }
        // Gong: each voice's gong follows its pitch (a glide, the bend, a
        // twist: the whole gong, in time) and rings the segment out on its
        // own; a harmony voice coming in hangs its gong and strikes it. A
        // held tam-tam is rolled with soft beaters on the studio's grid, as
        // the pan is, slower: it begins once the note has been held a beat
        // (a single stroke left to ring is held too), swells into it, and
        // the press is how hard it is rolled. A gong's loudness is the
        // physics' (a small gong struck as hard against its size radiates
        // more), evened out across the neck so a line is one instrument.
        if (model == Sound::Gong) {
            if (s.roll > 0 && !v.releasing && v.held > m_spb) {
                const float spb = m_spb;
                int per = 2;
                for (int p : {3, 4, 6, 8})
                    if (std::fabs(float(p) / spb - s.rollHz) < std::fabs(float(per) / spb - s.rollHz))
                        per = p;
                const double grid = m_beat * double(per);
                if (v.rollSlot < 0)
                    v.rollSlot = int64_t(std::floor(grid));
                v.rollAge += dt;
                const int64_t slot = int64_t(std::floor(grid - double(v.rollLate)));
                if (slot > v.rollSlot) {
                    v.rollSlot = slot;
                    v.rollHand ^= 1;
                    const float firm = clampf((v.press - 0.1f) / 0.75f, 0.0f, 1.0f);
                    const float into = clampf(v.rollAge / 1.5f, 0.0f, 1.0f);
                    const float swell = 0.3f + 0.7f * into * into * (3.0f - 2.0f * into);
                    const float stroke = s.roll * (0.2f + 0.6f * firm) * (v.rollHand ? 0.9f : 1.0f) *
                                         (1.0f + 0.12f * v.rng.bipolar()) * swell;
                    v.rollLate = 0.08f * v.rng.bipolar();
                    for (int b = 0; b < Voice::kBanks; ++b)
                        if (live[b])
                            gongStrike(v, v.banks[b], b, 1.0f, stroke);
                }
            }
            for (int b = 0; b < Voice::kBanks; ++b) {
                Bank &k = v.banks[b];
                std::fill(m_gongL[b], m_gongL[b] + n, 0.0f);
                std::fill(m_gongR[b], m_gongR[b] + n, 0.0f);
                if (!live[b])
                    continue;
                if (!k.gongLive && !v.releasing)
                    gongStrike(v, k, b, v.vel, 1.0f);
                if (!k.gongLive)
                    continue;
                k.gong.setPitch(clampf(inc1[b] * rate, 8.0f, 18000.0f) * std::exp2(s.gongOct));
                const float even = std::pow(clampf(inc1[b] * rate * std::exp2(s.gongOct) / 330.0f, 0.5f, 2.5f), -0.75f);
                k.gong.render(m_gongL[b], m_gongR[b], n, kGongLevel[std::clamp(s.gong, 0, gong::kKinds - 1)] * even);
            }
        }
        // Tromba marina: each voice of the line is a string of its own,
        // tuned to the key two octaves under the neck's root; its note
        // picks the harmonic the thumb touches (chosen again only when the
        // note changes, so a bend or a glide inside it keeps the thumb
        // where it is, and a new one slides it to another node). The bow
        // is drawn by the envelope, its weight, speed and place the
        // player's (tromba::bowFor), and it turns round at the end of its
        // stroke, or on a note struck again while it sounds, passing
        // through rest.
        if (model == Sound::TrombaMarina) {
            if (!v.releasing) {
                v.bowPos += dt;
                if (v.bowPos >= kTrombaStrokeS) {
                    v.bowDir = -v.bowDir;
                    v.bowPos = 0;
                }
            }
            // (A quick turn: a slow one lingers near rest, where the
            // string catches in several slips a period and may stay.)
            v.bowDirS += (v.bowDir - v.bowDirS) * (1.0f - std::exp(-dt / 0.01f));
            const float drawn = clampf(v.amp.v, 0.0f, 1.0f);
            for (int b = 0; b < Voice::kBanks; ++b) {
                Bank &k = v.banks[b];
                std::fill(m_trombaL[b], m_trombaL[b] + n, 0.0f);
                std::fill(m_trombaR[b], m_trombaR[b] + n, 0.0f);
                if (!live[b])
                    continue;
                if (!k.trombaLive && v.releasing)
                    continue;
                const float hz = inc1[b] * rate;
                const bool fresh = !k.trombaLive;
                if (fresh || std::fabs(k.target - k.trombaFor) > 0.5f) {
                    const int h = tromba::harmonicFor(midiToHz(clampf(k.target, 0.0f, 140.0f)), m_trombaRef);
                    k.tromba.setNote(hz, h, fresh);
                    k.trombaFor = k.target;
                } else {
                    k.tromba.setNote(hz, k.tromba.harmonic());
                }
                k.trombaLive = true;
                const tromba::Bowing bw =
                    tromba::bowFor(v.press, v.y, v.vel, k.tromba.harmonic(), hz / float(k.tromba.harmonic()));
                // The weight comes onto the string ahead of the speed, as a
                // player sets the bow before drawing it (Guettler's clean
                // attacks want the force up while the bow accelerates); as
                // it leaves, both ease off together. A flatter finger sets
                // the guidon closer, more bray.
                const float weight = v.releasing ? drawn : std::min(1.0f, 2.5f * drawn);
                k.tromba.setGap(tromba::Physics::kGap - 8e-6f * clampf(v.fat, 0.0f, 1.0f));
                k.tromba.setBow(drawn > 0.002f ? bw.force * weight : 0.0f, bw.speed * drawn * v.bowDirS, bw.beta);
                // Its loudness is the physics', evened out across the neck
                // so a line is one instrument.
                const float even = std::pow(clampf(hz / 440.0f, 0.4f, 3.0f), 0.35f);
                k.tromba.render(m_trombaL[b], m_trombaR[b], n, kTrombaLevel * even);
            }
        }
        // Daxophone: every voice of the line its own tongue, bowed on its
        // end. The dax goes where the note is, sliding with the finger, the
        // vibrato and the bend, and the player's ear brings it in; the press
        // is the bow's force against what the note needs (the lightest
        // skates, the hardest breaks the note, rough or falling flat); the
        // finger's height is where the bow is, high on the pad on the very
        // end, where every mode swings most and the tone is roughest, lower
        // in along the edge, rounder; a flatter finger presses the dax
        // lighter, so its cardboard gives and loses more. A held bow
        // turns at the end of its stroke, and a note played again while one
        // sounds changes the bow. Let go, the bow lifts and the tongue rings
        // out on the dax.
        float daxOut = 0;
        if (model == Sound::Dax) {
            // A player lands the bow with its weight first and then draws
            // it, so the hair grips and drags the tongue before it first
            // slips, and the tone speaks at once; let go, the bow lifts.
            const float want = v.releasing ? 0.0f : 1.0f;
            v.daxLand += (want - v.daxLand) * (1.0f - std::exp(-dt / (want > v.daxLand ? 0.002f : 0.025f)));
            v.daxBow += (want - v.daxBow) * (1.0f - std::exp(-dt / (want > v.daxBow ? 0.03f : 0.025f)));
            if (!v.releasing) {
                v.bowPos += dt;
                if (v.bowPos >= kDaxStrokeS) {
                    v.bowDir = -v.bowDir;
                    v.bowPos = 0;
                }
            }
            v.bowDirS += (v.bowDir - v.bowDirS) * (1.0f - std::exp(-dt / 0.03f));
            const float drawn = kDaxSpeed + kDaxSpeedVel * v.vel;
            const float speed = drawn * v.daxBow * v.bowDirS;
            const float along = clampf(1.0f - v.y, 0.0f, 1.0f);
            // (A player's ordinary press on the dax is moderate, as firm as
            // still lets it rock under a firm bow; flatter, lighter.)
            const float press = clampf(kDaxPress * (1.0f - v.fat), 0.0f, 1.0f);
            const float speedUp = drawn / 0.2f * v.daxLand;
            for (int b = 0; b < Voice::kBanks; ++b) {
                Bank &k = v.banks[b];
                std::fill(m_daxL[b], m_daxL[b] + n, 0.0f);
                std::fill(m_daxR[b], m_daxR[b] + n, 0.0f);
                if (!live[b])
                    continue;
                const float hz = clampf(inc1[b] * rate, 20.0f, 0.4f * rate);
                k.tongue.setPitch(hz);
                k.tongue.setDaxPress(press);
                // An ordinary press draws the note where the window says
                // (dax::ordinaryForce), lighter and harder from there.
                float lo = 0, hi = 0;
                dax::window(hz, lo, hi);
                const float ordinary = dax::ordinaryForce(hz);
                const float force = v.press < 0.5f ? ordinary * std::pow(clampf(v.press / 0.5f, 0.0f, 1.0f), kDaxSoft)
                                                   : ordinary * std::pow(kDaxCrush * hi / ordinary, (v.press - 0.5f) * (v.press - 0.5f) * 4.0f);
                k.tongue.setBow(force * speedUp, speed, 0.3f, along);
                // (Evened only so far: a tone broken off its note, croaking,
                // puts its sound where the part behind the dax passes far
                // more than at the note, and must not be lifted with it.)
                k.tongue.render(m_daxL[b], m_daxR[b], n, std::pow(std::max(k.tongue.transmission(), kDaxEvenFloor), -kDaxEven));
            }
            for (int c = 0; c < 2; ++c) {
                v.form[c][0].set(kDaxLowHz, 0.7f, rate);
                v.form[c][1].set(kDaxBandHz, kDaxBandQ, rate);
                v.form[c][2].set(kDaxTopHz, 0.7f, rate);
            }
            daxOut = s.gain * 0.3f * kDaxLevel;
        }
        // Guitar: each voice's guitar follows its pitch. The finger's slide
        // crosses the frets: counted from where it last rested, past 0.6 of
        // a semitone it moves a fret at a time (the string snapping onto
        // each), and once it rests again it is on its note exactly (in any
        // tuning). Rocking it in place is continuous; the twist, the bend
        // wheel and the vibrato bend the string, raising its tension. A flat finger is a palm on the strings; a firm press
        // stands nearer the amp, for feedback; the tone keys turn the amp's
        // treble. A harmony voice coming in is picked then.
        if (model == Sound::Guitar) {
            const float palm = palmOf(v);
            const float near = s.feedback * clampf((v.press - 0.7f) / 0.25f, 0.0f, 1.0f);
            for (int b = 0; b < Voice::kBanks; ++b) {
                Bank &k = v.banks[b];
                if (!live[b])
                    continue;
                if (!k.guitarLive && !v.releasing)
                    guitarPick(v, k, v.vel);
                if (!k.guitarLive)
                    continue;
                if (v.motion < 0.15f)
                    k.rested = k.anchor;
                const float d = k.pitch - k.rested;
                const float frets = std::fabs(d) > 0.6f ? std::round(d) : 0.0f;
                const float midi = (frets != 0.0f ? k.rested + frets : k.pitch) + vib1 + v.scoop;
                // (The fret under the finger: the note's, or as far along as the slide has crossed.)
                const float fretNow = frets != 0.0f ? k.rested - (k.anchor - k.fret) + frets : k.fret;
                k.guitar.setPitch(midiToHz(clampf(midi, 0.0f, 140.0f)), midi - fretNow);
                k.guitar.setMute(palm);
                k.guitar.setFeedback(v.releasing ? 0.0f : near);
                // Into the line's own rig (each harmony voice its own player's).
                guitar::Rig &rig = m_rig[size_t(v.from)][b];
                rig.setTone(tone);
                k.guitar.pickup(m_rigIn[v.from][b], n, &rig);
                m_rigFed[v.from][b] = true;
                m_rigSound[v.from] = v.sound;
                v.level = std::max(v.level * std::exp(-dt / 0.08f), k.guitar.level());
            }
        }
        // Sax: each voice's horn, played: the breath rides the envelope (so
        // the attack is the breath's rise behind the tongue, the release its
        // fall) from just over the reed's threshold under a light touch to a
        // hard, beating edge under a hard one; the finger's height is the
        // embouchure (low subtone, high edge); a flatter finger hums through
        // it, the growl, which a press at the very top also brings; the
        // pitch is the fingers, the lip and the vibrato, all in the bank's.
        if (model == Sound::Sax) {
            Env ahead = v.amp;
            for (int i = 0; i < n; ++i)
                ahead.step();
            const float a1 = ahead.v;
            const float touch = std::pow(clampf(v.press, 0.0f, 1.0f), 1.2f) * (0.85f + 0.15f * v.vel);
            const float growl = clampf((v.fat - 0.45f) / 0.35f, 0.0f, 1.0f) + 0.6f * clampf((v.press - 0.92f) / 0.08f, 0.0f, 1.0f);
            for (int b = 0; b < Voice::kBanks; ++b) {
                Bank &k = v.banks[b];
                std::fill(m_saxOut[b], m_saxOut[b] + n, 0.0f);
                if (!live[b]) {
                    k.saxLive = false;
                    continue;
                }
                const float hz = clampf(inc1[b] * rate * std::exp2(s.saxOct), 20.0f, 4000.0f);
                // The honk: a low note (under concert F♯3) pressed nearly to
                // the top, blown harder through a wider opening and growled
                // into (the growl not piling on a top press's own, which
                // would swamp the reed): the R&B tenor's.
                const float honk = s.saxHonk * clampf((v.press - 0.85f) / 0.1f, 0.0f, 1.0f) *
                                   clampf((190.0f - hz) / 40.0f, 0.0f, 1.0f);
                if (!k.saxLive) {
                    k.sax.start(sax::Kind(std::clamp(s.sax, 0, sax::kKinds - 1)), hz);
                    k.sax.tongue(v.vel, true);
                    k.saxLive = true;
                }
                sax::Sax::Control c;
                c.hz = hz;
                c.lip = clampf((v.y - 0.15f) / 0.7f, 0.0f, 1.0f);
                const float th = k.sax.threshold();
                // (From a little over the threshold, which the reed's
                // embouchure and opening move, to a hard, beating breath.)
                c.blow = (th + 0.06f + std::max(kSaxBlowTop - th - 0.06f, 0.08f) * touch + 0.1f * honk) * a1;
                c.growl = std::min(1.0f, std::max(growl, 0.35f * honk));
                c.air = 0.5f * clampf(v.fat, 0.0f, 1.0f);
                // (A light touch also closes the reed down under the lip: a
                // pianissimo is a smaller opening, not only less breath.)
                c.open = 0.5f + 0.6f * touch + 0.4f * honk;
                c.find = true;
                k.sax.render(m_saxOut[b], n, c);
            }
        }
        // Sync: each slave's ratio sweeps down from the attack to where it
        // settles, never all the way to the note, so the tear holds; height
        // and pressure lift it, a slow wander moves it, and so does the
        // pitch's own swing (the vibrato, a wiggle, a slide), so a held note
        // keeps tearing. The second voice, an octave up, sweeps quicker
        // from higher and wanders against the first. The two sides share
        // their masters, so the note's root never beats against itself in
        // mono, but their slaves stand a little apart (a flatter finger,
        // further), so the formants are wide. Every ratio is kept under
        // where the slave's own pitch would reach Nyquist.
        float ratio0 = 1, ratio1 = 1, ratioB0 = 1, ratioB1 = 1, syncDet = 1, syncFm = 0;
        if (model == Sound::Sync) {
            const float swingNow = 12.0f * std::log2(inc1[0] / std::max(inc0[0], 1e-9f)) / dt; // semitones a second
            v.syncWob += (clampf(0.06f * swingNow, -1.0f, 1.0f) - v.syncWob) * (1.0f - std::exp(-dt / 0.03f));
            const float wob = v.syncWob;
            const float fadeW = clampf(v.held / 0.6f, 0.0f, 1.0f);
            syncFm = s.syncFm * clampf((v.press - 0.5f) / 0.4f, 0.0f, 1.0f);
            const float top = 0.45f / (std::max(inc1[0], 1e-6f) * (1.0f + syncFm));
            auto ratioAt = [&](float env, float wander) {
                return clampf(s.syncTo + (s.syncFrom - s.syncTo) * env + s.syncY * v.y + 0.5f * (v.press - 0.5f) +
                                  wander + wob,
                              1.0f, std::max(top, 1.0f));
            };
            auto ratioBAt = [&](float env, float wander) {
                return clampf(1.7f + (1.3f * s.syncFrom - 1.7f) * env + 0.6f * s.syncY * v.y - wander - 0.5f * wob, 1.0f,
                              std::max(0.5f * top, 1.0f));
            };
            const float w0 = s.syncLfo * fadeW * sine()(v.syncLfo);
            v.syncLfo += 0.4f * dt;
            v.syncLfo -= std::floor(v.syncLfo);
            const float w1 = s.syncLfo * fadeW * sine()(v.syncLfo);
            ratio0 = ratioAt(v.syncEnv, w0);
            ratioB0 = ratioBAt(v.syncEnv2, w0);
            v.syncEnv *= std::exp(-dt / std::max(s.syncTime, 0.01f));
            v.syncEnv2 *= std::exp(-dt / std::max(0.55f * s.syncTime, 0.01f));
            ratio1 = ratioAt(v.syncEnv, w1);
            ratioB1 = ratioBAt(v.syncEnv2, w1);
            syncDet = std::exp2((25.0f + 50.0f * v.fat) / 1200.0f);
        }
        // Bow: where the bow sits and how hard it presses. A light press
        // lets the string slip easily (the friction's slope steep): a
        // breathy flautando that whistles on its upper partials or breaks;
        // a hard one holds it longer each period, a grinding sul
        // ponticello. Higher on the pad bows nearer the bridge, brighter.
        // The string's loss and its delay at the fundamental, as the
        // plucked string's; the bow turning round, and the rosin's grain,
        // in a narrow band high over the note.
        float bowSlope = 0, bowBeta = 0, bowP = 0, bowTau[Voice::kBanks] = {}, rosin = 0, dir0 = 1, dir1 = 1;
        float bowSlope0 = 0, bowBeta0 = 0, bowP0 = 0, bowTau0[Voice::kBanks] = {}, bowGain0 = outGain, bowVel0 = v.vel;
        float arc0 = 1, arc1 = 1, grit = 0, grit0 = 0;
        float bodyA0[2][3] = {}, bodyA1[2][3] = {}; // the two moving roll-offs' coefficients, from and to
        if (model == Sound::Bow) {
            const float force = clampf(0.05f + 0.95f * v.press, 0.0f, 1.0f);
            // A short, high string needs more weight on the bow to hold its
            // Helmholtz motion (a fiddler leans into the high positions),
            // so the same press means more the higher the note.
            // At twice the rate the junction holds the string more surely,
            // so a light bow is made steeper still, or it would never let
            // go of the fundamental and whistle.
            const float up = clampf((v.banks[0].pitch - 69.0f) / 12.0f, 0.0f, 1.5f);
            bowSlope = (3.6f - 2.8f * force + 3.0f * std::max(0.0f, 0.4f - force) / 0.4f) * (1.0f - 0.3f * up);
            bowBeta = 0.08f + 0.11f * (1.0f - v.y);
            bowP = clampf(0.45f - 0.2f * (v.y - 0.5f) - 0.1f * float(tone) / kToneSteps, 0.2f, 0.7f);
            // The string runs at twice the rate, so the corner the hair
            // leaves in it each period is made where it has room, and what
            // it throws over the top is filtered away rather than folded
            // back down as a whine. Its loss, per sample, is the same
            // filter at the new rate: the pole's square root.
            bowP = std::sqrt(bowP);
            for (int b = 0; b < Voice::kBanks; ++b) {
                const float w = kPi * inc1[b];
                bowTau[b] = std::atan2(bowP * std::sin(w), 1.0f - bowP * std::cos(w)) / std::max(w, 1e-6f);
                if (live[b])
                    v.banks[b].breath.set(clampf(6.0f * inc1[b] * rate, 2200.0f, 4500.0f), 4.0f, rate);
            }
            // The bow moves on the string as the finger does, sample by
            // sample from where the last block left it, never in steps a
            // block apart: the junction is a sharp nonlinearity, and a
            // slope or a length that jumped (a lighter bow, or one moved
            // toward the bridge, at a new note) would kick the string, a
            // tick right across the spectrum.
            // A heavy bow grinds: the skin's upper resonance, round three
            // kilohertz, comes forward with the weight, a nasal grit in the
            // body's own range rather than fizz over it.
            grit = clampf((v.press - 0.55f) / 0.4f, 0.0f, 1.0f);
            const bool fresh = v.bowWas[0] < 0;
            grit0 = fresh ? grit : v.bowWas[5];
            v.bowWas[5] = grit;
            bowSlope0 = fresh ? bowSlope : v.bowWas[0];
            bowBeta0 = fresh ? bowBeta : v.bowWas[1];
            bowP0 = fresh ? bowP : v.bowWas[2];
            bowGain0 = fresh ? outGain : v.bowWas[3];
            bowVel0 = fresh ? v.vel : v.bowWas[4];
            v.bowWas[0] = bowSlope;
            v.bowWas[1] = bowBeta;
            v.bowWas[2] = bowP;
            v.bowWas[3] = outGain;
            v.bowWas[4] = v.vel;
            for (int b = 0; b < Voice::kBanks; ++b) {
                bowTau0[b] = fresh ? bowTau[b] : v.bowTauWas[b];
                v.bowTauWas[b] = bowTau[b];
            }
            // The bow's arc, and at the end of the hair the bow turning
            // round by itself, as it must. What is heard follows the arc
            // over a few tens of milliseconds rather than jumping with it:
            // a bow changed early, mid-stroke, starts its new stroke light
            // while the old one was heavy, and that weight stepping at
            // once, between two blocks, would tick right across the
            // spectrum, loudest where the small body leaves nothing else.
            if (!v.releasing) {
                v.bowPos += dt;
                if (v.bowPos >= kBowStrokeS) {
                    v.bowDir = -v.bowDir;
                    v.bowPos = 0;
                }
            }
            arc0 = v.bowArc;
            v.bowArc += (bowArcAt(v.bowPos) - v.bowArc) * (1.0f - std::exp(-dt / 0.06f));
            arc1 = v.bowArc;
            // Over the grit the small box rolls the string off, lower the
            // harder it grinds, so the grit stays in the middle. As the
            // weight comes on, the roll-off's coefficients slide there
            // sample by sample: set anew each block, their steps would
            // tick right across the top they are there to keep quiet.
            for (int k = 0; k < 2; ++k) {
                const Svf &was = v.bodyLp[0][k];
                bodyA0[k][0] = was.a1;
                bodyA0[k][1] = was.a2;
                bodyA0[k][2] = was.a3;
            }
            for (int c = 0; c < 2; ++c) {
                v.bodyLp[c][0].set(4200.0f - 1100.0f * grit, 0.55f, rate);
                v.bodyLp[c][1].set(5000.0f - 1100.0f * grit, 0.7f, rate);
                v.bodyLp[c][2].set(5200.0f, 0.6f, rate);
            }
            for (int k = 0; k < 2; ++k) {
                const Svf &now = v.bodyLp[0][k];
                bodyA1[k][0] = now.a1;
                bodyA1[k][1] = now.a2;
                bodyA1[k][2] = now.a3;
                if (fresh)
                    std::copy(bodyA1[k], bodyA1[k] + 3, bodyA0[k]);
            }
            dir0 = v.bowDirS;
            v.bowDirS += (v.bowDir - v.bowDirS) * (1.0f - std::exp(-dt / 0.012f));
            dir1 = v.bowDirS;
            rosin = s.scratch * (0.5f + 0.8f * force);
        }
        // Reed: the registers drawn; the musette's outer reeds a fixed
        // number of hertz either side of the middle one, however high the
        // note, as an accordion's are tuned, so its tremolo keeps its speed
        // up the keyboard (a flatter finger tunes it wetter); the bellows'
        // pressure narrowing each reed's opening; the reeds that speak on
        // the pull a hair sharp of the push ones, as no two sets are tuned
        // alike, and voiced apart: the pull's reeds edgier, their tongues
        // swinging through a narrower opening, the push's rounder; and the
        // bellows turning round.
        float beatInc = 0, reedPw = 0.34f, reedDet = 1, jerk0 = 0, jerk1 = 0;
        if (model == Sound::Reed) {
            float w[kRegisters];
            registerWeights(v.y, w);
            for (int k = 0; k < kRegisters; ++k)
                v.reg[k] += (w[k] - v.reg[k]) * (1.0f - std::exp(-dt / 0.04f));
            const bool pull = v.bellows < 0;
            beatInc = s.beatHz * (0.85f + 0.35f * v.fat) * (pull ? 1.06f : 1.0f) / rate;
            // The air the bellows draw while the note sounds, faster the
            // harder they are pressed.
            if (!v.releasing && v.owner >= 0)
                m_lines[size_t(v.owner)].air += dt * (0.6f + 0.8f * v.press);
            reedPw = 0.42f - 0.16f * v.press + (pull ? -0.08f : 0.03f);
            reedDet = std::exp2((pull ? 2.0f : 0.0f) / 1200.0f);
            jerk0 = v.jerk;
            v.jerk *= std::exp(-dt / 0.07f);
            jerk1 = v.jerk;
            v.banks[0].breath.set(260.0f, 0.8f, rate); // the bellows' gust, low
        }
        // The bow's fiddle body and the reeds' chamber: fixed resonances,
        // whatever the note. The erhu's is a small box closed by python
        // skin, nasal around a kilohertz.
        if (model == Sound::Bow)
            for (int k = 0; k < 3; ++k) {
                v.form[0][k].set(kBowBodyHz[k], kBowBodyQ[k], rate);
                v.form[1][k].set(kBowBodyHz[k], kBowBodyQ[k], rate);
            }
        // The sync's formant, sung: a resonance riding on the first slave's
        // own pitch, so the harmonics it sweeps through ring out like a
        // vowel ("yow" on the attack, the tear while held), harder the
        // harder the note is struck. Nothing the other leads' filters
        // follow.
        // Over it a second formant, the vowel's: the finger's height
        // moves it from "ow" low on the pad (under a kilohertz) to "ee"
        // high (two and a half), so the tear can be sung on a vowel.
        float yowMix = 0, vowelMix = 0;
        if (model == Sound::Sync) {
            const float hz = clampf(inc1[0] * rate * ratio1, 200.0f, 0.4f * rate);
            const float f2 = kSyncF2Low * std::pow(kSyncF2High / kSyncF2Low, clampf((v.y - 0.2f) / 0.6f, 0.0f, 1.0f));
            for (int c = 0; c < 2; ++c) {
                v.form[c][0].set(hz * (c ? syncDet : 1.0f / syncDet), kYowQ, rate);
                v.form[c][1].set(f2 * (c ? 1.02f : 0.98f), kSyncF2Q, rate);
            }
            yowMix = kYow * (0.6f + 0.6f * v.vel) / kYowQ; // the band's own peak is its Q
            vowelMix = kSyncF2 * (0.6f + 0.6f * v.vel) / kSyncF2Q;
        }
        // The reeds' chamber, where the pull's bite sits higher and
        // narrower than the push's: a bandoneonist hears which way the
        // bellows go.
        if (model == Sound::Reed)
            for (int c = 0; c < 2; ++c) {
                v.form[c][0].set(v.bellows < 0 ? 2400.0f : 1500.0f, v.bellows < 0 ? 1.8f : 1.1f, rate);
                v.form[c][1].set(620.0f, 1.6f, rate);
            }
        // A played string let go hands what it still swings to the bridge
        // as it dies, and the taraf takes it up: the more of it as it
        // goes, so the sympathetic strings bloom after the note rather
        // than only under it.
        const float symBoost0 = v.symBoost;
        v.symBoost += ((v.releasing ? kTarafBloom : 1.0f) - v.symBoost) * (1.0f - std::exp(-dt / 0.03f));
        const float symBoost1 = v.symBoost;
        const float gateAttack = 1.0f - std::exp(-1.0f / (0.0015f * rate));
        const float gateRelease = 1.0f - std::exp(-1.0f / (0.004f * rate));

        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const float t = (float(i) + 0.5f) * inv;
            const float a = v.amp.step();
            v.filt.step();
            float sl = 0, sr = 0, air = 0;
            float extra = 0; // what is heard past the envelope: an accordion key's click
            for (int b = 0; b < Voice::kBanks; ++b) {
                if (!live[b])
                    continue;
                Bank &k = v.banks[b];
                const float inc = inc0[b] + (inc1[b] - inc0[b]) * t;
                const float gb = gain0[b] + (gain1[b] - gain0[b]) * t;
                float bl = 0, br = 0;
                if (s.saw > 0) {
                    for (int u = 0; u < U; ++u) {
                        const float o = k.saw[u].step(std::min(inc * uniRatio[u], 0.45f));
                        bl += o * uniL[u];
                        br += o * uniR[u];
                    }
                    bl *= uniGain;
                    br *= uniGain;
                }
                if (s.pulse > 0) {
                    const float p0 = k.pulse[0].step(std::min(inc, 0.45f), pw);
                    const float p1 = k.pulse[1].step(std::min(inc * pulseDet, 0.45f), 1.0f - pw);
                    // A second pulse, a few cents off and leaning right, comes
                    // up as the finger flattens: a chorus.
                    const float w = std::max(s.pulse2, 0.2f + 0.6f * v.fat);
                    bl += s.pulse * 0.6f * (p0 + 0.6f * w * p1);
                    br += s.pulse * 0.6f * (0.8f * p0 + w * p1);
                }
                float tone = 0; // the FM pair's sound, which the breath is rocked by
                if (s.fm > 0) {
                    float fmo[2];
                    for (int c = 0; c < 2; ++c) {
                        const float ci = std::min(inc * (c ? fmDet : 1.0f / fmDet), 0.45f);
                        k.car[c] += ci;
                        k.mod[c] += ci * s.fmRatio;
                        k.car[c] -= std::floor(k.car[c]);
                        k.mod[c] -= std::floor(k.mod[c]);
                        fmo[c] = sine()(k.car[c] + fmIndex * 0.15915494f * sine()(k.mod[c]));
                    }
                    // Each side mostly its own carrier: wide, but whole in mono.
                    bl += s.fm * 0.8f * (0.8f * fmo[0] + 0.2f * fmo[1]);
                    br += s.fm * 0.8f * (0.8f * fmo[1] + 0.2f * fmo[0]);
                    tone = 0.5f * (fmo[0] + fmo[1]);
                }
                if (s.sub > 0) {
                    // An octave down, in the middle.
                    const float o = k.sub.step(std::min(inc * subRatio, 0.45f)) * subGain[b];
                    bl += o;
                    br += o;
                }
                if (s.breath > 0) {
                    air += k.breath.step(v.rng.bipolar()).band * (0.65f + 0.35f * tone) * gb;
                }
                switch (model) {
                case Sound::String: {
                    // The pluck: one period of the shape a wire plectrum
                    // leaves the string in, pulled aside near its end (a
                    // triangle, its mean taken out), with a little scrape
                    // of noise, brighter for a harder stroke.
                    const float D = clampf(1.0f / std::max(inc, 1e-6f) - strTau[b], 4.0f, float(k.string.buf.size()) - 8.0f);
                    float in = 0;
                    if (k.pluck < 0) {
                        k.pluck = k.pluckLen = std::max(8, int(D));
                        k.pluckLp.set(1500.0f + 9000.0f * v.vel * (0.4f + 0.6f * v.y), rate);
                        k.swingTop = 0;
                        k.jawSweep = 1;
                        k.thud = v.vel;
                        k.thudLp.set(3800.0f, 0.8f, rate);
                    }
                    if (k.pluck > 0) {
                        constexpr float at = 0.12f;
                        const float ph = 1.0f - float(k.pluck) / float(k.pluckLen);
                        const float shape = (ph < at ? ph / at : (1.0f - ph) / (1.0f - at)) - 0.5f;
                        in = v.vel * (1.6f * shape + 0.8f * k.pluckLp.step(v.rng.bipolar()));
                        --k.pluck;
                    }
                    // The loop itself stays linear: the bridge is heard on
                    // the way out (below), never fed back. A string whose
                    // length followed its own swing, sample by sample, was
                    // chaotic: a rounding's difference grew into another
                    // buzz within a few hundred passes, so a compiler that
                    // fuses a multiply-add played another note.
                    const float y = k.string.read(D);
                    k.strLp += (y - k.strLp) * (1.0f - strB);
                    k.string.push(clampf(k.strLp * strG[b] + in, -3.0f, 3.0f));
                    // What is heard is the string as it slaps the bridge:
                    // while it swings toward it, past a small rest, it is
                    // heard later in its cycle the further it swings, so
                    // each period's wave folds over into a buzz; and the
                    // buzz's formant, where the swing puts it.
                    k.swing = std::max(std::fabs(y), k.swing * swingFall);
                    k.swingTop = std::max(k.swingTop, k.swing);
                    // The fold is measured against the swing the pluck left,
                    // so the buzz keeps its upper partials as the note
                    // rings (the bridge keeps feeding them, which is why a
                    // sitar stays bright where another string goes dull);
                    // its colour is the formant's to move.
                    const float ref = std::max(k.swingTop, 0.05f);
                    const float lean = std::min(std::max(0.0f, y / ref - 0.05f) * jaw * D * 0.5f, 0.5f * D);
                    const float heard = k.string.read(1.0f + lean);
                    float buzz = heard + jawGain * k.jawF.step(heard).band;
                    // And while it touches, the wire sizzles on the bridge:
                    // noise in the formant's band, as loud as the swing and
                    // fading as the stroke's "jiii" slides down, so the
                    // stroke is a buzz and not a filter closing on a saw.
                    if (lean > 0.0f)
                        buzz += kSizzle * k.swing * k.jawSweep * k.jawN.step(v.rng.bipolar()).band;
                    else
                        k.jawN.step(0.0f);
                    // The mizrab, the wire plectrum on the player's finger,
                    // scraping off the steel string: a few milliseconds of
                    // bright noise, the "chik" before the note.
                    if (k.thud > 1e-4f) {
                        buzz += k.thudLp.step(v.rng.bipolar()).band * k.thud * kScrape;
                        k.thud *= scrapeFall;
                    }
                    bl += buzz;
                    br += buzz;
                    break;
                }
                case Sound::Pan: {
                    const float sharp = sharp0 + (sharp1 - sharp0) * t;
                    for (int m = 0; m < kModes; ++m) {
                        const float mi = inc * kModeRatio[m] * sharp;
                        if (mi > 0.45f)
                            break;
                        k.modeE[m] *= modeDec[m];
                        k.modeF[m] *= fastDec;
                        k.modeS[m] += (k.modeE[m] + k.modeF[m] - k.modeS[m]) * smoothA;
                        k.modeA[m] += (k.modeS[m] - k.modeA[m]) * smoothA;
                        float &pa = k.modePh[m][0], &pb = k.modePh[m][1];
                        pa += mi;
                        pb += mi + split[m];
                        pa -= float(int(pa));
                        pb -= float(int(pb));
                        // The mode on its pitch, and its weaker twin a hair
                        // sharp of it, leaning right: a slow shimmer, never
                        // a null.
                        const float amp = k.modeA[m] * (1.0f - kModeBloom[m] * k.bloom);
                        const float sa = sine()(pa) * amp, sb = sine()(pb) * amp * twin[m];
                        bl += 0.8f * (sa + 0.4f * sb);
                        br += 0.8f * (sa + sb);
                    }
                    k.bloom *= bloomDec;
                    for (int j = 0; j < 2; ++j) {
                        const float hi = inc * kHaloRatio[j] * sharp;
                        if (hi > 0.45f)
                            continue;
                        k.haloA[j] += (k.haloE[j] - k.haloA[j]) * haloRise;
                        k.haloE[j] *= haloDec[j];
                        k.haloPh[j] += hi;
                        k.haloPh[j] -= float(int(k.haloPh[j]));
                        const float h = sine()(k.haloPh[j]) * k.haloA[j];
                        bl += 0.8f * h * (1.0f - 0.5f * kHaloPan[j]);
                        br += 0.8f * h * (1.0f + 0.5f * kHaloPan[j]);
                    }
                    // The stick's rubber tip: a soft, dull thud of a few
                    // milliseconds, as hard as the stroke, rising and falling
                    // as a half sine so it has no edge.
                    if (k.pluck < 0) {
                        k.pluck = k.pluckLen = int(0.005f * rate);
                        k.thudLp.set(500.0f + 1200.0f * k.thud, 0.7f, rate);
                    }
                    if (k.pluck > 0) {
                        const float ph = 1.0f - float(k.pluck) / float(k.pluckLen);
                        const float thud = k.thudLp.step(v.rng.bipolar()).low * 0.6f * k.thud * sine()(0.5f * ph);
                        bl += thud;
                        br += thud;
                        --k.pluck;
                    }
                    // The ping: a resonant band round four kilohertz, rung
                    // for three milliseconds under a half sine and left as
                    // long again to die away inside the band, then through
                    // a lowpass just over it, so it is bright where a mix
                    // has room and splashes nothing above six.
                    if (k.pingLeft < 0) {
                        k.pingLeft = k.pingLen = int(0.006f * rate);
                        k.pingF.set(4000.0f, 2.0f, rate);
                        k.pingLp.set(5000.0f, 0.6f, rate);
                    }
                    if (k.pingLeft > 0) {
                        const float ph = 1.0f - float(k.pingLeft) / float(k.pingLen);
                        const float in = ph < 0.5f ? v.rng.bipolar() * sine()(ph) : 0.0f;
                        const float pg = k.pingLp.step(k.pingF.step(in).band).low * 0.45f * k.ping;
                        bl += pg;
                        br += pg;
                        --k.pingLeft;
                    }
                    break;
                }
                case Sound::Gong:
                    bl += m_gongL[b][i];
                    br += m_gongR[b][i];
                    break;
                case Sound::TrombaMarina:
                    bl += m_trombaL[b][i];
                    br += m_trombaR[b][i];
                    break;
                case Sound::Dax:
                    bl += m_daxL[b][i];
                    br += m_daxR[b][i];
                    break;
                case Sound::Guitar: // (heard through its line's rig: renderRigs)
                    break;
                case Sound::Kargyraa: {
                    // The note is the melody, the growl's undertone under
                    // it; its voice held at a steady level (each note as
                    // loud as the last), the envelope shaping it after.
                    // (Harmony voices share the lead voice's growl when they
                    // fit it: one drone under the chord, not three; the
                    // throat sounds through the envelope's release.)
                    const float o = k.kg.step(inc * rate, v.y, v.press, !v.releasing || v.amp.v > 0.05f, kKgLevel,
                                              m_rootHz, b > 0 ? v.banks[0].kg.under() : 0.0f);
                    bl += o;
                    br += o;
                    break;
                }
                case Sound::Sax: {
                    const float o = m_saxOut[b][i] * kSaxLevel;
                    bl += o;
                    br += o;
                    break;
                }
                case Sound::Sync: {
                    // At four times the rate: four quarter steps, the second
                    // voice's master an octave up, through two half-band
                    // filters back down, so what the slaves fold above
                    // Nyquist, and the sidebands a hard press's FM throws
                    // up there while the vibrato moves them, is filtered
                    // away rather than heard running against the note.
                    const float ratio = ratio0 + (ratio1 - ratio0) * t, ratioB = ratioB0 + (ratioB1 - ratioB0) * t;
                    const float q = 0.25f * std::min(inc, 0.45f);
                    float ol = 0, orr = 0;
                    for (int h = 0; h < 4; ++h) {
                        const float xl = 0.6f * k.sync[0].step(q, ratio / syncDet, kSyncOwn, syncFm) +
                                         s.sync2 * 0.45f * k.sync2[0].step(2.0f * q, ratioB * syncDet, 0.0f, syncFm);
                        const float xr = 0.6f * k.sync[1].step(q, ratio * syncDet, kSyncOwn, syncFm) +
                                         s.sync2 * 0.45f * k.sync2[1].step(2.0f * q, ratioB / syncDet, 0.0f, syncFm);
                        // Each half-band filter's output is wanted only every
                        // other sample: between, the sample is only stored.
                        if (h == 1 || h == 3) {
                            const float hl = k.osL.fir(k.osL.up, k.osL.upPos, xl);
                            const float hr = k.osR.fir(k.osR.up, k.osR.upPos, xr);
                            if (h == 3) {
                                ol = k.osL.fir(k.osL.down, k.osL.downPos, hl);
                                orr = k.osR.fir(k.osR.down, k.osR.downPos, hr);
                            } else {
                                store(k.osL.down, k.osL.downPos, hl);
                                store(k.osR.down, k.osR.downPos, hr);
                            }
                        } else {
                            store(k.osL.up, k.osL.upPos, xl);
                            store(k.osR.up, k.osR.upPos, xr);
                        }
                    }
                    // Each side mostly its own, so the mono sum never loses
                    // the note to the two masters' beating.
                    bl += 0.8f * ol + 0.2f * orr;
                    br += 0.8f * orr + 0.2f * ol;
                    break;
                }
                case Sound::Bow: {
                    // The string either side of the bow: what reaches the
                    // bow from the bridge (after the string's loss) and
                    // from the nut, each turned over where it is reflected.
                    // Where their sum moves slower than the bow, the hair
                    // holds it (the friction's curve near 1); once it is
                    // pulled too far it slips (near 0) and flies back, once
                    // a period: the Helmholtz motion, with its corner as
                    // sharp as the bow's press allows. Two steps a sample,
                    // at twice the rate, its lengths counted in them.
                    const float tauB = bowTau0[b] + (bowTau[b] - bowTau0[b]) * t;
                    const float slope = bowSlope0 + (bowSlope - bowSlope0) * t;
                    const float pole = bowP0 + (bowP - bowP0) * t;
                    const float D = clampf(2.0f / std::max(inc, 1e-6f) - tauB, 12.0f, float(k.string.buf.size()) - 8.0f);
                    const float Db = std::max((bowBeta0 + (bowBeta - bowBeta0) * t) * D, 3.0f), Dn = std::max(D - Db, 3.0f);
                    const float arc = arc0 + (arc1 - arc0) * t;
                    const float speed =
                        a * arc * (0.1f + 0.12f * (bowVel0 + (v.vel - bowVel0) * t)) * (dir0 + (dir1 - dir0) * t);
                    float grains = 0, o = 0;
                    for (int h = 0; h < 2; ++h) {
                        const float fromBridge = k.bridge.read(Db);
                        k.strLp = (1.0f - pole) * fromBridge + pole * k.strLp;
                        const float bridgeRefl = -0.95f * k.strLp;
                        const float nutRefl = -k.string.read(Dn);
                        const float dv = speed - (bridgeRefl + nutRefl);
                        float x = std::fabs(dv * slope) + 0.75f;
                        x *= x;
                        const float rho = std::min(1.0f / (x * x), 1.0f);
                        const float slip = 1.0f - rho;
                        // The rosin's grain: while the hair slips it catches
                        // the string unevenly, so its noise comes in time
                        // with each slip and through the string's own
                        // harmonics.
                        const float grain = v.rng.bipolar() * slip;
                        grains += 0.5f * grain;
                        const float nv = dv * rho + 0.004f * rosin * grain * std::fabs(speed) * 8.0f;
                        k.string.push(clampf(bridgeRefl + nv, -2.0f, 2.0f));
                        k.bridge.push(clampf(nutRefl + nv, -2.0f, 2.0f));
                        if (h == 1)
                            o = k.osL.fir(k.osL.down, k.osL.downPos, 4.0f * fromBridge);
                        else
                            store(k.osL.down, k.osL.downPos, 4.0f * fromBridge);
                    }
                    // And heard through the air, in a narrow band high
                    // over the note, louder as the bow catches on its attack
                    // or turns round.
                    const float catchUp = 1.0f + 3.0f * (chiff0 + (chiff1 - chiff0) * t) + 6.0f * (1.0f - std::fabs(dir1));
                    o += k.breath.step(grains * 1.41f * catchUp).band * 0.03f * rosin;
                    bl += o;
                    br += o;
                    break;
                }
                case Sound::Reed: {
                    // The middle reed is always drawn; the bandoneon adds
                    // one an octave up, as the bandoneon's own pairs are
                    // tuned, the musette the two that beat against it, one
                    // either side.
                    const float wc = v.reg[0], wb = v.reg[1], wm = v.reg[2];
                    const float ri = inc * reedDet;
                    const float mid = k.reeds[0].step(std::min(ri, 0.45f), reedPw);
                    float octave = 0, hi = 0, lo = 0;
                    if (wb > 0.001f)
                        octave = k.reeds[3].step(std::min(2.0f * ri * 1.0004f, 0.45f), reedPw);
                    if (wm > 0.001f) {
                        hi = k.reeds[1].step(std::min(ri + beatInc, 0.45f), reedPw);
                        lo = k.reeds[2].step(std::min(std::max(ri - 0.93f * beatInc, 1e-5f), 0.45f), reedPw);
                    }
                    // The valve opening: a breath of air as the note speaks.
                    const float valve = 0.08f * v.rng.bipolar() * (chiff0 + (chiff1 - chiff0) * t);
                    const float m = (0.55f + 0.25f * wc) * mid + 0.5f * wb * octave + valve;
                    bl += m + wm * (0.5f * lo + 0.32f * hi);
                    br += m + wm * (0.32f * lo + 0.5f * hi);
                    if (b == 0) {
                        // The key: its pallet knocking open (or shut, softer,
                        // as it is let go), a few milliseconds of click; and
                        // the bellows turning round, a low gust of air.
                        if (k.pluck < 0) {
                            k.pluck = k.pluckLen = int(0.006f * rate);
                            k.thudLp.set(2600.0f, 1.4f, rate);
                        }
                        if (k.pluck > 0) {
                            const float ph = float(k.pluck) / float(k.pluckLen);
                            extra += k.thudLp.step(v.rng.bipolar()).band * 0.35f * k.thud * ph * ph * ph;
                            --k.pluck;
                        }
                        const float j = jerk0 + (jerk1 - jerk0) * t;
                        if (j > 0.001f)
                            extra += k.breath.step(v.rng.bipolar()).low * 0.5f * j;
                    }
                    break;
                }
                default:
                    break;
                }
                if (s.layer > 0) {
                    // Two saws an octave up, a few cents apart, one each side.
                    bl += k.layer.step(std::min(inc * 2.0f * layerDet, 0.45f)) * s.layer;
                    br += k.layer2.step(std::min(inc * 2.0f / layerDet, 0.45f)) * s.layer;
                }
                // Each voice is driven on its own: driven together, a third
                // and its melody would grind out difference tones down in
                // the bass's range. Hard drive runs at twice the rate so its
                // harmonics don't fold.
                if (drive > 0.3f) {
                    bl = k.osL.process(bl, [&](float x) { return fastTanh(x * driveGain); }) * driveNorm;
                    br = k.osR.process(br, [&](float x) { return fastTanh(x * driveGain); }) * driveNorm;
                } else if (drive > 0) {
                    bl = fastTanh(bl * driveGain) * driveNorm;
                    br = fastTanh(br * driveGain) * driveNorm;
                }
                // Harmony voices stand a little to either side of the melody.
                static const float bankPan[Voice::kBanks][2] = {{1.0f, 1.0f}, {1.15f, 0.82f}, {0.82f, 1.15f}, {1.0f, 1.0f}};
                sl += bl * gb * bankPan[b][0];
                sr += br * gb * bankPan[b][1];
            }
            if (s.formant) {
                float fl = 0, fr = 0;
                for (int k = 0; k < 3; ++k) {
                    fl += v.form[0][k].step(sl).band * formGain[k];
                    fr += v.form[1][k].step(sr).band * formGain[k];
                }
                sl = (fl + kFormDry * sl) * v.formNorm;
                sr = (fr + kFormDry * sr) * v.formNorm;
            } else if (model == Sound::Bow) {
                float fl = 0, fr = 0;
                for (int k = 0; k < 3; ++k) {
                    const float g =
                        kBowBodyGain[k] / kBowBodyQ[k] * (k == 2 ? 1.0f + 2.0f * (grit0 + (grit - grit0) * t) : 1.0f);
                    fl += v.form[0][k].step(sl).band * g;
                    fr += v.form[1][k].step(sr).band * g;
                }
                // A small box: little of it speaks over seven or eight
                // kilohertz, so a hard press near the bridge grinds in its
                // middle instead of fizzing up into the air.
                sl = 1.6f * fl + 0.3f * sl;
                sr = 1.6f * fr + 0.3f * sr;
                for (int k = 0; k < 2; ++k)
                    for (auto &f : {&v.bodyLp[0][k], &v.bodyLp[1][k]}) {
                        f->a1 = bodyA0[k][0] + (bodyA1[k][0] - bodyA0[k][0]) * t;
                        f->a2 = bodyA0[k][1] + (bodyA1[k][1] - bodyA0[k][1]) * t;
                        f->a3 = bodyA0[k][2] + (bodyA1[k][2] - bodyA0[k][2]) * t;
                    }
                for (int k = 0; k < 3; ++k) {
                    sl = v.bodyLp[0][k].step(sl).low;
                    sr = v.bodyLp[1][k].step(sr).low;
                }
            } else if (model == Sound::Sync) {
                const float dl = sl, dr = sr;
                sl += yowMix * v.form[0][0].step(dl).band + vowelMix * v.form[0][1].step(dl).band;
                sr += yowMix * v.form[1][0].step(dr).band + vowelMix * v.form[1][1].step(dr).band;
            } else if (model == Sound::Dax) {
                // The contact microphones in the block.
                for (int c = 0; c < 2; ++c) {
                    float &x = c ? sr : sl;
                    x = v.form[c][0].step(x).high;
                    x += kDaxBand * v.form[c][1].step(x).band;
                    x = v.form[c][2].step(x).low;
                }
            } else if (model == Sound::Reed) {
                // The musette's chamber is wet and bright; the bandoneon's
                // pair wants the box's darker, woody body under it.
                const float wet = 0.55f * (0.55f + 0.45f * v.reg[2]) * (v.bellows < 0 ? 1.35f : 1.0f), body = 0.4f * v.reg[1];
                sl = 0.8f * sl + wet * v.form[0][0].step(sl).band + body * v.form[0][1].step(sl).band;
                sr = 0.8f * sr + wet * v.form[1][0].step(sr).band + body * v.form[1][1].step(sr).band;
            }
            // Into the ladder at half level: its own soft clip stays a
            // colour, not a distortion that would smear a bell's partials.
            // The sync lead goes in lower still, and its ladder runs at
            // twice the rate: its formants put most of its energy high up,
            // where the soft clip, at the page's rate, would fold its
            // harmonics back between the others, faint mirrored lines
            // near the top. Coming back down, the half-band filter lets a
            // little of what lay just over the page's Nyquist fold into
            // the last few kilohertz, so a gentle four-pole roll-off from
            // 19 kHz takes off what no one hears and what was mirrored.
            const float into = model == Sound::Sync ? 0.12f : 0.5f;
            if (model == Sound::Sync) {
                auto ladL = [&](float x) { return v.fL.step(x); };
                auto ladR = [&](float x) { return v.fR.step(x); };
                sl = v.ladOs[0].process(sl * into, ladL) * (1.0f / into) * resComp;
                sr = v.ladOs[1].process(sr * into, ladR) * (1.0f / into) * resComp;
                sl = v.topLp[0][1].step(v.topLp[0][0].step(sl).low).low;
                sr = v.topLp[1][1].step(v.topLp[1][0].step(sr).low).low;
            } else {
                sl = v.fL.step(sl * into) * (1.0f / into) * resComp;
                sr = v.fR.step(sr * into) * (1.0f / into) * resComp;
            }
            // The gate: open for a little over half of each step, with
            // edges a few milliseconds long so it pulses without clicking.
            float gate = 1.0f;
            if (gated) {
                const float want = gatePhase < 0.56f ? 1.0f : 0.0f;
                v.gateEnv += (want - v.gateEnv) * (want > v.gateEnv ? gateAttack : gateRelease);
                gate = v.gateEnv;
                gatePhase += gateInc;
                if (gatePhase >= 1.0f)
                    gatePhase -= 1.0f;
            } else {
                v.gateEnv += (1.0f - v.gateEnv) * gateRelease;
                gate = v.gateEnv;
            }
            // A bowed string's level is its bow's speed already: its envelope
            // only lets the note go, and the bow's arc shapes it, the weight
            // a player leans into the middle of the stroke. The bellows
            // turning round push a swell through the reeds as they catch.
            // A gong's level is its own: a hand stills it, not an envelope.
            // A tromba marina's is its bow's, which the envelope draws. So is a
            // guitar's own: its string rings, the amp sustains it, a hand mutes it.
            // A saxophone's is its breath's, already.
            const float env = model == Sound::Bow ? std::min(1.0f, 4.0f * a) * (arc0 + (arc1 - arc0) * t)
                              : model == Sound::Gong || model == Sound::TrombaMarina || model == Sound::Dax || model == Sound::Guitar ||
                                        model == Sound::Sax
                                  ? 1.0f
                                  : a;
            const float jerk = model == Sound::Reed ? 1.0f + 0.5f * (jerk0 + (jerk1 - jerk0) * t) * std::min(1.0f, v.held / 0.03f) : 1.0f;
            const float level = model == Sound::Bow                                ? bowGain0 + (outGain - bowGain0) * t
                                : model == Sound::TrombaMarina || model == Sound::Sax ? s.gain * 0.3f
                                : model == Sound::Dax                              ? daxOut
                                                                                   : outGain;
            const float g2 = env * level * gate * jerk * (amp0 + (amp1 - amp0) * t);
            sl *= g2;
            sr *= g2;
            if (extra != 0) {
                sl += extra * outGain * gate;
                sr += extra * outGain * gate;
            }
            if (s.breath > 0) {
                // The air skips the ladder (its band is already set under the
                // cutoff) so its puff can come before the tone has bloomed.
                const float ga = (breath * a + air0 + (air1 - air0) * t) * outGain * gate;
                air = v.airLp.step(air) * ga;
                sl += air;
                sr += air;
            }
            outL[i] += sl;
            outR[i] += sr;
            v.lastL = sl;
            v.lastR = sr;
            sendL[i] += sl * send;
            sendR[i] += sr * send;
            if (s.sympathy > 0) {
                sym[i] += (sl + sr) * s.sympathy * (symBoost0 + (symBoost1 - symBoost0) * t);
                m_symFed = true;
            }
            peak = std::max(peak, std::fabs(sl) + std::fabs(sr));
        }
        if (model != Sound::Guitar)
            v.level = std::max(peak * 2.0f, v.level * std::exp(-dt / 0.08f));
        // A released voice is done once it's 70 dB down; the envelope's own
        // idea of silence is far below anything audible, and every voice
        // still ringing costs its full price.
        if (model == Sound::Dax) {
            // Done once the bow is off and the tongues have rung out.
            if (v.releasing && v.daxLand < 1e-3f && v.level < 3e-4f)
                v.active = false;
        } else if (model == Sound::Gong) {
            // Done once every gong of the voice has rung out.
            bool ringing = false;
            for (const auto &k : v.banks)
                ringing |= k.gongLive && k.gong.active();
            if (v.releasing && !ringing)
                v.active = false;
        } else if (model == Sound::TrombaMarina) {
            // Done once the bow has left and every string has rung out.
            bool ringing = false;
            for (const auto &k : v.banks)
                ringing |= k.trombaLive && k.tromba.active();
            if (v.releasing && (v.amp.stage == Env::Idle || v.amp.v < 3e-4f) && !ringing)
                v.active = false;
        } else if (model == Sound::Guitar) {
            bool ringing = false;
            for (const auto &k : v.banks)
                ringing |= k.guitarLive && k.guitar.active();
            if (v.releasing && !ringing)
                v.active = false;
        } else if (v.releasing && (v.amp.stage == Env::Idle || v.amp.v < 3e-4f)) {
            v.active = false;
        }
    }

    // The sitar's other strings, which no finger stops: the taraf, eleven
    // sympathetic strings under the playing ones, tuned up the scale from
    // its root (between A3 and G♯4), never plucked, ringing only with what
    // the played strings give them at their own harmonics, each from its
    // own place across the field, so any note of the scale sets a shimmer
    // going around it; and the chikari, three high drone strings (the
    // root above the taraf's, its octave and the fifth between) that a
    // second finger strikes, buzzing on their own bridge. Each is a comb
    // whose loop is tuned the way the played strings' are. Nothing to do
    // while they are silent and nobody feeds them.
    void renderStrings(float *l, float *r, int n, const Harmony &h) {
        bool chikari = false;
        for (const auto &c : m_chik)
            chikari |= c.wait >= 0 || c.pluck > 0;
        if (!m_symFed && !chikari && m_symLevel < 1e-6f) {
            std::fill(m_symSend, m_symSend + n, 0.0f);
            return;
        }
        m_symFed = false;
        const int per = h.tuning.degreesPerOctave();
        double root = h.tuning.baseMidi() + h.tuning.degreeSemis(0);
        while (root < 57.0)
            root += 12.0;
        while (root >= 69.0)
            root -= 12.0;
        auto loop = [&](double midi, float b, float size) {
            const float w = kTwoPi * midiToHz(midi) / m_rate;
            return clampf(kTwoPi / w - std::atan2(b * std::sin(w), 1.0f - b * std::cos(w)) / w, 4.0f, size - 8.0f);
        };
        float symD[kTaraf];
        for (int c = 0; c < kTaraf; ++c)
            symD[c] = loop(root + h.tuning.degreeSemis(c) - h.tuning.degreeSemis(0), kSymB, float(m_sym[c].buf.size()));
        // The chikari: the root an octave over the taraf's, its fifth (the
        // scale degree nearest seven semitones) and its octave.
        int fifth = 0;
        double best = 1e9;
        for (int d = 1; d < per; ++d) {
            const double e = std::fabs(h.tuning.degreeSemis(d) - h.tuning.degreeSemis(0) - 7.0);
            if (e < best) {
                best = e;
                fifth = d;
            }
        }
        const double chikNote[kChikari] = {root + 12.0, root + 12.0 + h.tuning.degreeSemis(fifth) - h.tuning.degreeSemis(0),
                                           root + 24.0};
        float chikD[kChikari], chikG[kChikari];
        for (int c = 0; c < kChikari; ++c) {
            chikD[c] = loop(chikNote[c], kChikB, float(m_chik[c].string.buf.size()));
            chikG[c] = std::pow(10.0f, -3.0f / (1.6f * float(midiToHz(chikNote[c])))); // 60 dB down in 1.6 s
        }
        const float fall = std::exp(-float(n) / (0.03f * m_rate));
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            float in = m_symSend[i] * 0.012f;
            float outL = 0, outR = 0;
            for (int c = 0; c < kChikari; ++c) {
                Chikari &k = m_chik[c];
                if (k.wait >= 0 && k.wait-- == 0)
                    k.pluck = k.len = std::max(8, int(chikD[c]));
                float x = 0;
                if (k.pluck > 0) {
                    // A sharp stroke of the mizrab near the string's end.
                    const float ph = 1.0f - float(k.pluck) / float(k.len);
                    x = k.vel * ((ph < 0.08f ? ph / 0.08f : (1.0f - ph) / 0.92f) - 0.5f + 0.3f * m_rng.bipolar());
                    --k.pluck;
                }
                const float y = k.string.read(chikD[c]);
                k.lp += (y - k.lp) * (1.0f - kChikB);
                k.string.push(clampf(k.lp * chikG[c] + x, -3.0f, 3.0f));
                const float lean = std::min(std::max(0.0f, y - 0.02f) * 0.3f * chikD[c], 0.5f * chikD[c]);
                const float heard = k.string.read(1.0f + lean);
                const float o = m_chikLevel * (heard + 0.3f * k.jawF.step(heard).band);
                outL += o * kChikPan[c][0];
                outR += o * kChikPan[c][1];
                in += 0.02f * o;
            }
            for (int c = 0; c < kTaraf; ++c) {
                const float y = m_sym[c].read(symD[c]);
                m_symLp[c] += (y - m_symLp[c]) * (1.0f - kSymB);
                m_sym[c].push(fastTanh(m_symLp[c] * kSymKeep + in));
                // Across the field, neighbours in pitch on opposite sides.
                const float pan = float((c * 7 + 3) % kTaraf) / float(kTaraf - 1);
                outL += kSymOut * y * (1.0f - 0.8f * pan);
                outR += kSymOut * y * (0.2f + 0.8f * pan);
            }
            m_symSend[i] = 0;
            l[i] += outL;
            r[i] += outR;
            peak = std::max(peak, std::fabs(outL) + std::fabs(outR) + std::fabs(in) * 100.0f);
        }
        m_symLevel = std::max(peak, m_symLevel * fall);
    }

    // The chikari struck: down across the three strings, a few
    // milliseconds apart, as hard as the tap.
    void strum(float vel, float level) {
        m_chikLevel = level;
        for (int c = 0; c < kChikari; ++c) {
            m_chik[c].wait = int(float(c) * 0.009f * m_rate);
            m_chik[c].vel = 0.5f + 0.5f * vel;
        }
    }

    // The part's own echo: tempo-synced ping-pong, darker and thinner with
    // every repeat. A throw pushes the line into it and lets the feedback
    // climb; letting go leaves the repeats to fade on their own.
    void renderEcho(float *l, float *r, int n, AudioContext &ctx) {
        const Transport &tr = ctx.transport();
        float beats = 0.75f, fbBase = 0.25f;
        for (const auto &line : m_lines) {
            if (line.voice < 0)
                continue;
            const Voice &v = m_voices[size_t(line.voice)];
            if (!v.active)
                continue;
            beats = sound(v.sound).echoBeats;
            fbBase = std::max(fbBase, sound(v.sound).echoFb);
        }
        const float target = std::min(beats * float(tr.secondsPerBeat()) * m_rate, 3.0f * m_rate);
        // A throw lifts the feedback near its edge and lets go slowly, and
        // darkens the repeats as it goes, so the tail is long and sinks
        // behind the line instead of clattering over it.
        const float fbTarget = fbBase + (0.88f - fbBase) * m_throw;
        const float dt = float(n) / m_rate;
        m_echoFb += (fbTarget - m_echoFb) * (1.0f - std::exp(-dt / (fbTarget > m_echoFb ? 0.05f : 4.0f)));
        const float dark = clampf((m_echoFb - fbBase) / 0.5f, 0.0f, 1.0f);
        const float lpHz = 4800.0f * std::exp2(-1.6f * dark);
        m_echoLpL.set(lpHz, m_rate);
        m_echoLpR.set(lpHz, m_rate);
        renderStrings(l, r, n, ctx.harmony());
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const float d = m_echoTime.step(target);
            const float dl = m_echoL.read(d), dr = m_echoR.read(d);
            // Ping-pong: what goes in starts on the left, every repeat crosses.
            const float fl = m_echoLpL.step(m_echoHp.step(dr).high) * m_echoFb;
            const float fr = m_echoLpR.step(m_echoHp2.step(dl).high) * m_echoFb;
            m_echoL.push(fastTanh(0.7f * (m_sendL[i] + m_sendR[i]) + fl));
            m_echoR.push(fastTanh(fr));
            m_declickL *= m_declickFade;
            m_declickR *= m_declickFade;
            const float ol = l[i] + dl * 0.7f + m_declickL, orr = r[i] + dr * 0.7f + m_declickR;
            // Nothing under the lead's own range: the −OCT voice keeps its
            // body, the rumble goes.
            l[i] = m_dcL.step(m_hpL.step(ol).high);
            r[i] = m_dcR.step(m_hpR.step(orr).high);
            peak = std::max(peak, std::fabs(dl) + std::fabs(dr));
            m_sendL[i] = m_sendR[i] = 0;
        }
        m_echoLevel = std::max(peak, m_echoLevel * 0.97f);
    }

    float m_rate = 48000;
    float m_rootHz = 0; // Kargyraa: the key's root, for the growl to hold
    uint64_t m_age = 0;
    std::array<Voice, 8> m_voices;
    std::array<Line, kLines> m_lines;
    std::array<Pending, 64> m_pending;
    int m_pendingCount = 0;
    float m_pubPos[kLines] = {}, m_pubHarm[kLines][kHarmSlots] = {}, m_pubVib[kLines] = {};

    float m_sendL[kBlock] = {}, m_sendR[kBlock] = {};
    // The taraf and the chikari, and what the played strings send them.
    static constexpr int kTaraf = 11, kChikari = 3;
    static constexpr float kSymB = 0.35f;  // the taraf's loop damping: they ring sweet, not bright
    static constexpr float kSymKeep = 0.9985f; // and what each pass keeps: they ring on a few seconds after the note
    static constexpr float kSymOut = 0.5f;     // and how loud they are heard
    static constexpr float kChikB = 0.12f; // the chikari's: bright wire
    static constexpr float kChikPan[kChikari][2] = {{0.6f, 0.9f}, {0.9f, 0.6f}, {0.7f, 0.8f}};
    struct Chikari {
        Delay string;
        float lp = 0, vel = 0;
        int wait = -1, pluck = 0, len = 1; // samples until it is struck; the stroke still being fed in
        Svf jawF;
    };
    float m_symSend[kBlock] = {};
    float m_gongL[Voice::kBanks][kBlock] = {}, m_gongR[Voice::kBanks][kBlock] = {};
    float m_trombaL[Voice::kBanks][kBlock] = {}, m_trombaR[Voice::kBanks][kBlock] = {};
    float m_trombaRef = 55; // the tromba marina's open string, two octaves under the neck's root (Hz)
    float m_daxL[Voice::kBanks][kBlock] = {}, m_daxR[Voice::kBanks][kBlock] = {};
    dax::Ear m_daxEar;
    // Guitar: an amp and a cab for every line and every voice of it, what
    // its voices' pickups send them this segment, and what plays through them.
    std::array<std::array<guitar::Rig, Voice::kBanks>, kLines> m_rig;
    float m_rigIn[kLines][Voice::kBanks][kBlock] = {};
    bool m_rigFed[kLines][Voice::kBanks] = {};
    int m_rigSound[kLines] = {};
    float m_saxOut[Voice::kBanks][kBlock] = {};
    Delay m_sym[kTaraf];
    float m_symLp[kTaraf] = {};
    Chikari m_chik[kChikari];
    float m_chikLevel = 0, m_symLevel = 0;
    bool m_symFed = false;
    Rng m_rng{0x5174A2u};
    int64_t m_clock = 0; // samples rendered
    float m_spb = 0.5f;  // the studio's seconds per beat, for a pan's roll
    double m_beat = 0;   // and the beat the block being rendered starts on, for its accents
    Delay m_echoL, m_echoR;
    Smooth m_echoTime;
    Svf m_echoHp, m_echoHp2;
    OnePoleLp m_echoLpL, m_echoLpR;
    float m_echoFb = 0.3f, m_echoLevel = 0, m_throw = 0;
    float m_declickL = 0, m_declickR = 0, m_declickFade = 0.99f;
    Svf m_hpL, m_hpR;
    DcBlock m_dcL, m_dcR;
};

// --- control ------------------------------------------------------------------

namespace {

// How much a finger is moving, 0 resting .. 1 sliding or wiggling: quick to
// rise, slower to fall, so a vibrato's turning points don't count as rest.
struct Motion {
    float last = 0, speed = 0, m = 0;
    double t = -1;
    float update(float p, double now) {
        if (t < 0) {
            t = now;
            last = p;
            return m;
        }
        const float dt = float(std::max(now - t, 0.002));
        const float v = std::fabs(p - last) / dt;
        speed += (v - speed) * std::min(1.0f, dt / 0.03f);
        const float want = clampf((speed - 0.6f) / 2.4f, 0.0f, 1.0f);
        m += (want - m) * std::min(1.0f, dt / (want > m ? 0.02f : 0.16f));
        last = p;
        t = now;
        return m;
    }
};

// Rests a continuous position on whole steps unless it is moving.
float rounded(float p, float motion) {
    const float q = std::round(p);
    return q + (p - q) * motion;
}

QString noteName(double midi) {
    const int m = int(std::lround(midi));
    return QString::fromUtf8(Tuning::noteName(m)) + QString::number(m / 12 - 1);
}

} // namespace

class LeadControl : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        syncKey(ctx);
        if (f.button != m_click) {
            m_click = f.button;
            param(ctx, POctave, 0, m_click ? 1.0f : 0.0f, 0, f.t);
        }
        // Lifts first.
        for (auto &h : m_held) {
            if (h.id < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still)
                lift(h, f.t, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Held *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (h)
                    land(*h, t, f.t, ctx);
                continue;
            }
            follow(*h, t, f.t, ctx);
        }
        m_status = describe();
    }

    void background(double t, ControlContext &ctx) override {
        // The take is the studio's to describe; the hint only says whether
        // Space has something to loop and how it would land. How many bars
        // it takes is the session's decision, shown once it has made it.
        const PartView &pv = ctx.studio().parts[ctx.part()];
        // Once Space has taken a phrase (a take waiting, or a new loop
        // playing), those notes are spoken for: the hint waits for new ones.
        // A take that has landed: a clip starting to play, or the playing
        // one replaced. Say what it is, from the clip itself, for a few
        // seconds: "looping 2 bars in slot 3".
        const ClipView *clip = pv.playing >= 0 ? &pv.clips[pv.playing] : nullptr;
        const bool landed = clip && pv.recording < 0 &&
                            (pv.playing != m_seenPlaying || clip->lengthBeats != m_seenLength || clip->events != m_seenEvents);
        if (pv.recording >= 0 || landed)
            m_takenAt = t;
        if (landed && clip->lengthBeats > 0) {
            const int bars = int(std::lround(clip->lengthBeats / kBeatsPerBar));
            const QString length = clip->lengthBeats < kBeatsPerBar - 0.01f
                                       ? QStringLiteral("%1 beats").arg(int(std::lround(clip->lengthBeats)))
                                       : QStringLiteral("%1 bar%2").arg(bars).arg(bars > 1 ? "s" : "");
            m_landed = QStringLiteral("looping %1 in slot %2").arg(length).arg(pv.playing + 1);
            m_landedAt = t;
        }
        m_seenPlaying = pv.playing;
        m_seenLength = clip ? clip->lengthBeats : 0;
        m_seenEvents = clip ? clip->events : 0;
        m_loopText = loopState(ctx.beatAt(t), pv);
        if (m_loopText.isEmpty() && pv.playing >= 0 && t - m_landedAt < 3.0)
            m_loopText = m_landed;
        const SpacePlan pl = m_loopText.isEmpty() ? spacePlan(ctx) : SpacePlan{};
        m_loopHint = !pl.any ? QString()
                     : pl.sets ? QStringLiteral("space on the next one loops it")
                               : QStringLiteral("space loops it");
        m_status = describe();
        // The trail: what the lines sang, sampled for the page to draw as
        // it rises away from the note names.
        if (t - m_lastSample < 0.01)
            return;
        m_lastSample = t;
        const PartView &v = ctx.studio().parts[ctx.part()];
        TrailPoint &p = m_trail[size_t(m_trailHead++ % kTrail)];
        p.t = t;
        for (int k = 0; k <= kClipLayers; ++k) {
            const int line = k == 0 ? 0 : (v.playing >= 0 ? shownLine(v.playing, k - 1) : -1);
            if (line < 0) {
                for (float &lv : p.level[k])
                    lv = 0;
                continue;
            }
            const float *d = v.data + kLineData * line;
            p.pos[k][0] = d[1];
            p.level[k][0] = d[0] > 0 ? d[2] : 0.0f;
            for (int s = 0; s < kHarmSlots; ++s) {
                p.pos[k][1 + s] = d[4 + s];
                p.level[k][1 + s] = d[0] > 0 ? d[2] * d[7 + s] : 0.0f;
            }
        }
    }

    void leave(ControlContext &ctx) override {
        for (auto &h : m_held)
            if (h.id >= 0)
                lift(h, ctx.now(), ctx);
        for (auto &k : m_keys)
            if (k.down) {
                k.down = false;
                off(ctx, k.id, ctx.now());
            }
        if (m_click) {
            m_click = false;
            param(ctx, POctave, 0, 0, 0, ctx.now());
        }
        if (m_latch) {
            m_latch = false;
            releaseHeldHarmony(ctx);
        }
        m_frame = Frame{};
    }

    // The song file keeps what the player chose here: the sound and the
    // trance gate by name (their lists may grow or be reordered), the
    // neck's octave, the glide and the tone in steps, and whether notes
    // follow the chord and the harmony holds. Fingers, keys and the harmony
    // they hold this moment are not kept, and neither is overdub, which is
    // the studio's.
    void save(QJsonObject &out) const override {
        out[QStringLiteral("sound")] = QString::fromUtf8(sound(m_sound).name);
        out[QStringLiteral("octave")] = m_shift;
        out[QStringLiteral("glide")] = m_glide;
        out[QStringLiteral("gate")] = QString::fromUtf8(gateName(m_gate));
        out[QStringLiteral("tone")] = m_tone;
        out[QStringLiteral("follow")] = m_follow;
        out[QStringLiteral("hold")] = m_latch;
    }

    // Each value is taken only when it is of its kind and within its range
    // (a number out of range is pulled into it); anything else falls back
    // to the page's own default, not to what the last song left, so a song
    // opens the same whatever came before it. The glide, gate and tone go
    // to the engine as their keys send them; the sound, follow and the
    // octave travel with each note.
    void load(const QJsonObject &in, ControlContext &ctx) override {
        auto number = [&](const char *key, int lo, int hi, int fallback) {
            const QJsonValue v = in.value(QLatin1String(key));
            if (!v.isDouble() || !std::isfinite(v.toDouble()))
                return fallback;
            return int(std::lround(std::clamp(v.toDouble(), double(lo), double(hi))));
        };
        auto flag = [&](const char *key) { return in.value(QLatin1String(key)).toBool(false); };
        const QString soundName = in.value(QStringLiteral("sound")).toString();
        m_sound = 0;
        for (int i = 0; i < kSounds; ++i)
            if (soundName == QString::fromUtf8(sound(i).name))
                m_sound = i;
        const QString gate = in.value(QStringLiteral("gate")).toString();
        int g = 0;
        for (int i = 0; i < kGates; ++i)
            if (gate == QString::fromUtf8(gateName(i)))
                g = i;
        m_shift = number("octave", -1, 1, 0);
        m_follow = flag("follow");
        const bool hold = flag("hold");
        if (m_latch && !hold)
            releaseHeldHarmony(ctx);
        m_latch = hold;
        setGlide(number("glide", -2, 2, 0), ctx);
        setGate(g, ctx);
        setTone(number("tone", -kToneSteps, kToneSteps, 0), ctx);
        m_status = describe();
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool used = handleKey(k, ctx);
        m_status = describe();
        return used;
    }

    bool handleKey(const KeyEvent &k, ControlContext &ctx) {
        const int degree = pianoDegree(k.key);
        if (degree >= 0) {
            if (k.repeat)
                return true;
            KeyHeld &kh = m_keys[size_t(std::clamp(pianoIndex(k.key), 0, int(m_keys.size()) - 1))];
            const double t = ctx.now();
            if (k.down && !kh.down) {
                syncKey(ctx);
                kh.down = true;
                kh.id = kKeyId + pianoIndex(k.key);
                const bool accent = k.modifiers & Qt::ShiftModifier;
                const int abs = (m_neckOct + m_shift) * m_per + degree;
                Command on;
                on.type = Command::On;
                on.id = kh.id;
                on.t = t;
                on.i[0] = ((abs % m_per) + m_per) % m_per;
                on.i[1] = (abs - on.i[0]) / m_per;
                on.i[2] = m_sound;
                on.i[3] = flags();
                on.f[0] = accent ? 1.0f : 0.75f;
                on.f[1] = 0.6f;
                on.f[4] = 0.25f;
                on.f[6] = accent ? 0.85f : 0.55f;
                ctx.send(on);
                kh.abs = abs;
                logOnset(t);
            } else if (!k.down && kh.down) {
                kh.down = false;
                off(ctx, kh.id, t);
            }
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (k.modifiers & Qt::ShiftModifier) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 cleared").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        // The shared keys first (docs/PARTS.md 4b), each with the older
        // keys this page had for the same thing as second keys.
        const bool shift = k.modifiers & Qt::ShiftModifier;
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
            // Shift with ← →: the trance gate, the shared rhythm keys.
            if (shift)
                stepGate(k.key == Qt::Key_Right ? 1 : -1, ctx);
            else
                setSound((m_sound + (k.key == Qt::Key_Left ? kSounds - 1 : 1)) % kSounds, ctx);
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            // Shift with ↑ ↓: the glide, from a quarter of the sound's own to four times it.
            if (shift)
                shiftGlide(k.key == Qt::Key_Up ? 1 : -1, ctx);
            else
                shiftOctave(k.key == Qt::Key_Up ? 1 : -1, ctx);
            return true;
        case Qt::Key_Minus:
        case Qt::Key_Plus:  // its own key on a Nordic keyboard, Shift+= on a US one
        case Qt::Key_Equal: // a second key: Shift+0 on a Nordic keyboard
            shiftTone(k.key == Qt::Key_Minus ? -1 : 1, ctx);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift)
                toggleOverdub(ctx);
            else
                toggleLatch(ctx);
            return true;
        case Qt::Key_section:   // the key left of 1: § on a Nordic keyboard,
        case Qt::Key_QuoteLeft: // ` on a US one
        case Qt::Key_Backslash: // AltGr on a Nordic keyboard, so a second key
        case Qt::Key_F11:
        case Qt::Key_AsciiTilde: // Shift+` on a US keyboard
        case Qt::Key_onehalf:    // Shift+§ on a Nordic one
            toggleFollow(ctx);
            return true;
        case Qt::Key_F12:
            toggleLatch(ctx);
            return true;
        case Qt::Key_F10:
            toggleOverdub(ctx);
            return true;
        case Qt::Key_Delete:
            letGo(ctx);
            return true;
        default:
            return false;
        }
    }

    // --- settings, from a key or a chip under the pad ---

    void setSound(int snd, ControlContext &ctx) {
        m_sound = std::clamp(snd, 0, kSounds - 1);
        ctx.notice(QString::fromUtf8(sound(m_sound).name));
    }

    void shiftOctave(int step, ControlContext &ctx) {
        m_shift = std::clamp(m_shift + step, -1, 1);
        ctx.notice(m_shift == 0 ? QStringLiteral("Middle octave")
                                : QStringLiteral("Octave %1%2").arg(m_shift > 0 ? "+" : "−").arg(std::abs(m_shift)));
    }

    void shiftGlide(int step, ControlContext &ctx) {
        setGlide(m_glide + step, ctx);
        ctx.notice(QStringLiteral("Glide ×%1").arg(double(std::exp2(float(m_glide)))));
    }

    void stepGate(int step, ControlContext &ctx) {
        setGate((m_gate + step + kGates) % kGates, ctx);
        ctx.notice(m_gate ? QStringLiteral("Gate %1").arg(QString::fromUtf8(gateName(m_gate)))
                          : QStringLiteral("Gate off"));
    }

    void shiftTone(int step, ControlContext &ctx) {
        setTone(m_tone + step, ctx);
        ctx.notice(m_tone == 0 ? QStringLiteral("Tone: the sound's own")
                               : QStringLiteral("Tone %1%2").arg(m_tone > 0 ? "+" : "−").arg(std::abs(m_tone)));
    }

    void toggleFollow(ControlContext &ctx) {
        m_follow = !m_follow;
        ctx.notice(m_follow ? QStringLiteral("Following the chord") : QStringLiteral("Free in the scale"));
    }

    void toggleOverdub(ControlContext &ctx) {
        const bool on = !ctx.studio().parts[ctx.part()].overdub;
        ctx.overdub(on);
        ctx.notice(on ? QStringLiteral("Overdub on") : QStringLiteral("Overdub off"));
    }

    // Lets go of the harmony voices that only the hold keeps; a finger
    // still on a harmony row keeps its own.
    bool releaseHeldHarmony(ControlContext &ctx) {
        bool any = false;
        for (int s = 0; s < kHarmSlots; ++s)
            if (m_slots[s].on && !m_slots[s].held) {
                m_slots[s].on = false;
                param(ctx, PHarmony, s, m_slots[s].row, 0, ctx.now());
                any = true;
            }
        return any;
    }

    void letGo(ControlContext &ctx) {
        ctx.notice(releaseHeldHarmony(ctx) ? QStringLiteral("Held harmony let go")
                                           : QStringLiteral("No harmony held"));
    }

    bool toggleLatch(ControlContext &ctx) {
        m_latch = !m_latch;
        if (!m_latch)
            releaseHeldHarmony(ctx);
        ctx.notice(m_latch ? QStringLiteral("Harmony holds") : QStringLiteral("Harmony follows the fingers"));
        return true;
    }

    // The mouse on the band under the pad: the clips and the sounds, then
    // every setting the keys reach.
    enum Spot {
        SpotSound = 100,
        SpotOctDown = 200,
        SpotOctUp,
        SpotGlideDown,
        SpotGlideUp,
        SpotDarker,
        SpotBrighter,
        SpotGateDown,
        SpotGateUp,
        SpotFollow,
        SpotHold,
        SpotOverdub,
        SpotLetGo
    };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
            return true;
        if (id >= SpotSound && id < SpotSound + kSounds) {
            if (id - SpotSound != m_sound)
                setSound(id - SpotSound, ctx);
            m_status = describe();
            return true;
        }
        switch (id) {
        case SpotOctDown: shiftOctave(-1, ctx); break;
        case SpotOctUp: shiftOctave(1, ctx); break;
        case SpotGlideDown: shiftGlide(-1, ctx); break;
        case SpotGlideUp: shiftGlide(1, ctx); break;
        case SpotDarker: shiftTone(-1, ctx); break;
        case SpotBrighter: shiftTone(1, ctx); break;
        case SpotGateDown: stepGate(-1, ctx); break;
        case SpotGateUp: stepGate(1, ctx); break;
        case SpotFollow: toggleFollow(ctx); break;
        case SpotHold: toggleLatch(ctx); break;
        case SpotOverdub: toggleOverdub(ctx); break;
        case SpotLetGo: letGo(ctx); break;
        default: return false;
        }
        m_status = describe();
        return true;
    }

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "← →\tsound: Anthem, Pluck, Bell, Chip, Vox, Scream, Hoover, Big room, Mini (a vintage mono synth), "
            "Flute, Sitar (raga: a string buzzing on its bridge, its buzz sweeping down through the harmonics "
            "after the stroke, a taraf of sympathetic strings in the scale that rings on after the note; it rings "
            "long, and legato bends the string, meend), Steel pan (calypso: a note longer than an eighth is "
            "rolled on the studio's grid, swelling into the roll and leaning on the beat; its octave and fifth "
            "ring with it), Sync (Italo disco: two hard-synced saws that keep tearing, their formant sung on a "
            "vowel), Erhu (a bowed Chinese fiddle; a held bow swells and turns by itself, its vibrato comes "
            "late and widens with the bow, and a legato leap slides, huayin), Accordion (cumbia, tango: three "
            "registers; the bellows turn by the phrase, or in a rest, and the pull bites harder than the push), "
            "Gong (gamelan: a bossed gong, every voice its own, its octave beating against the smith's, the ombak), "
            "Opera gong (Chinese opera: low on the neck the big flat gongs, sharp when struck hard and gliding down; "
            "high the small domed ones, gliding up), Tam-tam (film score: a thud an octave down that, struck hard, "
            "blossoms over a second into a shimmer; held, it is rolled), Tromba marina (baroque: one long bowed "
            "string that sounds only its harmonics, the thumb sliding from node to node, on a bridge whose free foot "
            "knocks on the board every period, a trumpet's bray when bowed firmly; no vibrato), "
            "Daxophone (free improv: a wooden tongue "
            "bowed on its end, the finger the dax sliding along it, so it glides wherever the finger goes; "
            "it rests in the key only when following the chord), "
            "Guitar (rock: an overdriven electric guitar, "
            "a picked steel string through a humbucker, a cranked valve amp and a 4x12; a second finger hammers on, "
            "lifting it pulls off, a slide steps fret by fret, held notes sustain and, pressed, feed back), "
            "Kargyraa (Tuvan throat singing: the note "
            "is the overtone the vowel picks out, a whistle over a growl in the singer's own register, its "
            "undertone, the false folds closing on every other pulse; higher on the pad brightens the vowel, the "
            "press is the lungs), "
            "Tenor sax (R&B) and Alto sax (smooth jazz: "
            "a cane reed beating on a conical brass horn; a light touch is a breathy subtone, a hard one a bright, "
            "beating edge; legato slurs under the breath, the keys moving); "
            "the row under the pad shows the ones around the chosen "
            "sound, faded at an end with more beyond; the status line says what music each is for\n"
            "↑ ↓\toctave of the neck and the keys (Shift+↑ ↓: glide longer, shorter)\n"
            "Shift+← →\ttrance gate: off, 1/8, 1/16, 1/8 triplets, on the studio's beat\n"
            "- and + (or =)\ttone: darker, brighter\n"
            "Return (or F12)\tharmony holds after the fingers lift\n"
            "Shift+Return (or F10)\toverdub onto the playing loop\n"
            "the key left of 1: § (` on US; or \\, F11)\tfollow the chord: notes and harmony land on chord tones\n"
            "Delete\tlet the held harmony go\n"
            "F1–F8, Shift+F1–F8\tlaunch a slot, clear it\n"
            "Backspace\tstop this part at the next bar\n"
            "under the pad, with the mouse\tthe clips (click: launch or stop), the sounds; octave, glide, tone, gate, "
            "follow, hold, overdub, let go\n"
            "finger on the neck\ta note of the scale; one voice, singing\n"
            "second finger while one is held\tlegato: glides there, lift to fall back (trills, runs); on Sitar, "
            "tapped high on the pad, the chikari: the high drone strings struck beside the melody\n"
            "slide\tglide through the scale; rest and it settles in tune (on Daxophone, it stays where the finger is)\n"
            "wiggle\tvibrato as wide as the wiggle; hold still and it comes by itself\n"
            "higher on the pad\tbrighter (on Vox, the vowel: oo, oh, ah, eh, ee; on Sync, the sweep held higher and "
            "the vowel from ow to ee; on "
            "Erhu, bowed nearer the bridge; on Accordion, the register: clarinet low, bandoneon in the middle, musette "
            "high, and the bandoneon from the keys; on the gongs, the strike moves out from the middle toward the rim; "
            "on Tromba marina, bowed nearer the nut; on Daxophone, bowed on the very end, rougher; lower, further in "
            "along its edge, rounder; on Guitar, picked nearer the bridge; on the saxes, the embouchure: low a dropped "
            "jaw and much lip, subtone, high a firm lip, edge)\n"
            "press harder\tswell, and a deeper vibrato (on Mini, a quack on each attack, a light touch whistles; "
            "on Flute, a quicker attack; on Steel pan, a harder roll, and high on the note a ping; on Sync, a snarl of FM into the sync; on Erhu, "
            "the bow's weight: a light touch whistles, a hard one grinds; on the gongs, a harder mallet: further glides, "
            "a stronger ombak, a brighter blossom, and a harder tam-tam roll; on Tromba marina, the bow's weight: "
            "light is a soft flageolet, firmer sets the bridge knocking, all the way scratches; on Daxophone, the bow's weight: "
            "the lightest skates, harder is rougher, the hardest breaks the note: rough, or falling a few semitones; "
            "on Guitar, a harder pick as the finger lands, and held, nearer the amp until it feeds back)\n"
            "flatten the finger\tfatter: the unison spreads (on Flute, breathier; on Sitar, more buzz; "
            "on Accordion, a wetter musette; on the gongs, a softer beater; on Daxophone, the dax pressed lighter; "
            "on Guitar, a palm muting the strings; on the saxes, a growl, a hum through the horn)\n"
            "twist the finger\tbend up or down one step of the scale\n"
            "click the pad\tleap an octave up while held\n"
            "left column, harmony rows\teach finger adds a voice: −oct, 3rd, 5th, 6th, +oct; slide between; press to bring it out\n"
            "BEND\ta pitch wheel: slide up or down, rests on whole steps\n"
            "THROW\tinto the echo; higher throws harder; lift and the repeats ring out\n"
            "Space, after playing\tloops it (the studio's key): alone, press it on the phrase's next ONE and it sets the tempo; over a loop, what you repeated (or the whole phrase) loops at once. The pad says what it took\n"
            "Space again\tcalls a waiting take off\n"
            "Z X C … / A S D … / Q W E …\tthe scale on three rows, an octave apart; overlap keys for legato; Shift accents");
    }

    QString status() const override { return m_status; }
    // For the tutorial: glides ever played (a slide into another note, or a
    // second finger's legato), hand vibratos (a finger rocked to and fro on
    // a note), notes sung with a harmony voice, and follow and sound.
    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("glides"))
            return float(m_glides);
        if (name == QLatin1String("wiggles"))
            return float(m_wiggles);
        if (name == QLatin1String("harmonized"))
            return float(m_harmonized);
        if (name == QLatin1String("follow"))
            return m_follow ? 1.0f : 0.0f;
        if (name == QLatin1String("sound"))
            return float(m_sound);
        return PartControl::tutorialProbe(name);
    }

private:
    enum class Role : uint8_t { None, Note, Harmony, Bend, Throw };
    struct Held {
        int id = -1;
        Role role = Role::None;
        double t0 = 0;
        int q = 0;          // the neck cell, with hysteresis
        int abs = 0;        // its absolute degree
        int slot = -1;      // harmony slot
        float y0 = 0, angle0 = 0;
        Motion motion, twistMotion;
        double lastSend = 0;
        float sent[8] = {};
        float shown = 0;    // what the finger's tag says (bend amount, row)
        float wigPos = 0;   // a rocking finger, for the tutorial: where it last turned,
        int wigDir = 0, wigTurns = 0; // which way it goes, how often it turned
    };
    struct KeyHeld {
        bool down = false;
        int id = 0, abs = 0;
    };
    struct Slot {
        bool on = false, held = false;
        float row = 1, level = 0;
    };
    static constexpr int kTrail = 360;
    struct TrailPoint {
        double t = -1;
        // [0] the live line, [1 + layer] the playing loop's layers
        float pos[1 + kClipLayers][1 + kHarmSlots] = {};
        float level[1 + kClipLayers][1 + kHarmSlots] = {};
    };

    Held *find(int id) {
        for (auto &h : m_held)
            if (h.id == id)
                return &h;
        return nullptr;
    }

    void syncKey(ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        m_per = family(v.family).count;
        m_neckOct = neckOctave(v.root);
    }

    int flags() const {
        int rows[kHarmSlots];
        for (int s = 0; s < kHarmSlots; ++s)
            rows[s] = m_slots[s].on ? int(std::lround(m_slots[s].row)) : -1;
        return (m_follow ? kFollow : 0) | (m_gate << kGateShift) | packTone(m_tone) | packRows(rows);
    }

    void param(ControlContext &ctx, int which, int slot, float a, float b, double t) {
        Command c;
        c.type = Command::Param;
        c.t = t;
        c.i[0] = which;
        c.i[1] = slot;
        c.f[0] = a;
        c.f[1] = b;
        ctx.send(c);
    }

    // The settings the engine keeps on the live line, without a word: the
    // keys say what they did, and an opened song says nothing.
    void setGlide(int glide, ControlContext &ctx) {
        m_glide = std::clamp(glide, -2, 2);
        param(ctx, PGlide, 0, std::exp2(float(m_glide)), 0, ctx.now());
    }
    void setGate(int gate, ControlContext &ctx) {
        m_gate = std::clamp(gate, 0, kGates - 1);
        param(ctx, PGate, m_gate, 0, 0, ctx.now());
    }
    void setTone(int tone, ControlContext &ctx) {
        m_tone = std::clamp(tone, -kToneSteps, kToneSteps);
        param(ctx, PTone, m_tone, 0, 0, ctx.now());
    }

    void off(ControlContext &ctx, int id, double t) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        c.t = t;
        ctx.send(c);
    }

    // --- loops ---
    //
    // Space is the studio's: on every page it loops what was just played.
    // With nothing else playing, Space on the phrase's next ONE closes the
    // loop and sets the tempo; over a playing clock, one cycle of what was
    // repeated (or the whole phrase, in whole bars) loops at once. This page
    // only shows it: that Space has something to take, a take waiting if
    // there is one, and then what the take became.
    // Playing alone never makes a loop.

    void logOnset(double t) {
        m_onsets[size_t(m_onsetHead++ % m_onsets.size())] = t;
    }

    // Whether Space has a phrase to loop now (notes since a pause of two
    // bars, as the session looks for them), and whether that loop would
    // set the tempo (nothing else playing).
    struct SpacePlan {
        bool any = false;
        bool sets = false;
    };
    SpacePlan spacePlan(ControlContext &ctx) const {
        SpacePlan pl;
        const StudioView &sv = ctx.studio();
        const double now = ctx.now(), spb = 60.0 / std::max(1.0, sv.bpm);
        const long n = std::min<long>(m_onsetHead, long(m_onsets.size()));
        const double latest = n > 0 ? m_onsets[size_t((m_onsetHead - 1) % long(m_onsets.size()))] : -1e9;
        if (latest <= m_takenAt || now - latest > 8 * spb)
            return pl;
        pl.any = true;
        bool playing = false;
        for (int p = 0; p < sv.partCount; ++p)
            playing |= sv.parts[p].playing >= 0 || sv.parts[p].queued >= 0;
        pl.sets = !playing && !sv.metronome;
        return pl;
    }

    // The take as the studio shows it: waiting for its bar line in a slot.
    QString loopState(double beat, const PartView &v) const {
        if (v.recording < 0)
            return {};
        // The studio publishes the take's real length and the beat it lands
        // on; an open-ended one (a recording) has neither yet.
        QString length;
        if (v.takeBeats >= kBeatsPerBar - 0.01f) {
            const int bars = int(std::lround(v.takeBeats / kBeatsPerBar));
            length = QStringLiteral("%1 bar%2 · ").arg(bars).arg(bars > 1 ? "s" : "");
        } else if (v.takeBeats > 0) {
            const int beats = int(std::lround(v.takeBeats));
            length = QStringLiteral("%1 beat%2 · ").arg(beats).arg(beats > 1 ? "s" : "");
        }
        const double left = v.takeBeats > 0 ? v.takeAt - beat : std::ceil(beat / kBeatsPerBar - 1e-9) * kBeatsPerBar - beat;
        return QStringLiteral("loop %1slot %2 · %3")
            .arg(length)
            .arg(v.recording + 1)
            .arg(left > 1.0   ? QStringLiteral("in %1 beats").arg(int(std::ceil(left)))
                 : left > 0.0 ? QStringLiteral("in 1 beat")
                              : QStringLiteral("now"));
    }

    void land(Held &h, const Touch &t, double now, ControlContext &ctx) {
        h = Held{};
        h.id = t.id;
        h.t0 = now;
        h.y0 = t.y;
        h.angle0 = t.angle;
        h.lastSend = now;
        if (t.x >= kColumn) {
            h.role = Role::Note;
            if (notesHeld(&h) > 0)
                ++m_glides; // legato
            if (harmonyOn())
                ++m_harmonized;
            const float pos = neckPosition(t.x, m_per);
            h.wigPos = pos;
            h.q = std::clamp(int(std::lround(pos)), 0, neckCells(m_per) - 1);
            h.motion.update(pos, now);
            h.abs = (m_neckOct + m_shift) * m_per + h.q;
            Command on;
            on.type = Command::On;
            on.id = t.id;
            on.t = now;
            on.i[0] = ((h.abs % m_per) + m_per) % m_per;
            on.i[1] = (h.abs - on.i[0]) / m_per;
            on.i[2] = m_sound;
            on.i[3] = flags();
            on.f[0] = clampf(0.6f + 0.5f * t.pressure, 0.3f, 1.0f);
            fillNote(on, h, t, pos, 0, 0);
            ctx.send(on);
            std::copy(on.f + 1, on.f + 7, h.sent + 1);
            logOnset(now);
            return;
        }
        if (t.y >= kHarmBottom) {
            // A free voice first; with the harmony held, a held-over one next.
            int slot = -1;
            for (int s = 0; s < kHarmSlots && slot < 0; ++s)
                if (!m_slots[s].on)
                    slot = s;
            for (int s = 0; s < kHarmSlots && slot < 0; ++s)
                if (!m_slots[s].held)
                    slot = s;
            if (slot < 0)
                return; // three voices of harmony is all there is; this finger rests
            h.role = Role::Harmony;
            h.slot = slot;
            if (notesHeld(&h) > 0)
                ++m_harmonized;
            const float row = harmonyRow(t.y);
            h.motion.update(row, now);
            Slot &s = m_slots[slot];
            s.held = s.on = true;
            s.row = std::round(row);
            s.level = clampf(0.55f + 0.6f * t.pressure, 0.4f, 1.0f);
            h.sent[0] = s.row;
            h.sent[1] = s.level;
            param(ctx, PHarmony, slot, s.row, s.level, now);
            return;
        }
        h.role = t.x < kBendSplit ? Role::Bend : Role::Throw;
        if (h.role == Role::Throw) {
            const float a = throwAmount(t);
            h.sent[0] = a;
            param(ctx, PThrow, 0, a, 0, now);
        }
    }

    void follow(Held &h, const Touch &t, double now, ControlContext &ctx) {
        const bool due = now - h.lastSend >= 0.0155;
        switch (h.role) {
        case Role::Note: {
            const float pos = neckPosition(t.x, m_per);
            const int cells = neckCells(m_per);
            if (std::fabs(pos - float(h.q)) > 0.62f)
                h.q = std::clamp(int(std::lround(pos)), 0, cells - 1);
            const float m = h.motion.update(pos, now);
            // A twist is measured from how the finger sat when it last held
            // still: a contact's angle drifts as a finger slides, so a slide
            // takes the angle along with it rather than bending. And the
            // angle only means anything for a finger that isn't round.
            if (m > 0.4f)
                h.angle0 = t.angle;
            float twist = 0;
            if (t.eccentricity > 0.2f) {
                float d = t.angle - h.angle0;
                while (d > kPi / 2)
                    d -= kPi;
                while (d < -kPi / 2)
                    d += kPi;
                // Nothing for the first 20°, a whole step by 50°.
                const float mag = clampf((std::fabs(d) - 0.35f) / 0.5f, 0.0f, 1.0f);
                // Steep in the middle, so a twist sits on the step or off it.
                const float s = mag * mag * (3.0f - 2.0f * mag);
                twist = d < 0 ? s : -s; // clockwise bends up
            }
            // And like the slide, a twist held still rests on a whole step:
            // a finger that happens to sit a little turned stays in tune.
            twist = rounded(twist, h.twistMotion.update(twist * 4.0f, now));
            const int abs = (m_neckOct + m_shift) * m_per + h.q;
            if (abs != h.abs)
                ++m_glides;
            // Rocked to and fro by more than a tenth of a cell, four turns
            // over: the player's own vibrato.
            if (std::fabs(pos - h.wigPos) > 0.1f) {
                const int dir = pos > h.wigPos ? 1 : -1;
                if (h.wigDir != 0 && dir != h.wigDir && ++h.wigTurns == 4) {
                    ++m_wiggles;
                    h.wigTurns = 0;
                }
                h.wigDir = dir;
                h.wigPos = pos;
            }
            Command mv;
            mv.type = Command::Move;
            mv.id = t.id;
            mv.t = now;
            mv.i[0] = ((abs % m_per) + m_per) % m_per;
            mv.i[1] = (abs - mv.i[0]) / m_per;
            fillNote(mv, h, t, pos, m, twist);
            float change = 0;
            for (int k = 1; k < 7; ++k)
                change = std::max(change, std::fabs(mv.f[k] - h.sent[k]));
            if (abs != h.abs || (due && change > 0.005f)) {
                h.abs = abs;
                h.lastSend = now;
                std::copy(mv.f + 1, mv.f + 7, h.sent + 1);
                ctx.send(mv);
            }
            return;
        }
        case Role::Harmony: {
            const float row = harmonyRow(t.y);
            const float m = h.motion.update(row, now);
            Slot &s = m_slots[h.slot];
            s.row = rounded(row, m);
            s.level = clampf(0.55f + 0.6f * t.pressure, 0.4f, 1.0f);
            if (due && (std::fabs(s.row - h.sent[0]) > 0.005f || std::fabs(s.level - h.sent[1]) > 0.01f)) {
                h.sent[0] = s.row;
                h.sent[1] = s.level;
                h.lastSend = now;
                param(ctx, PHarmony, h.slot, s.row, s.level, now);
            }
            return;
        }
        case Role::Bend: {
            // About eight millimetres a step on a real pad.
            const float raw = clampf((t.y - h.y0) / 0.075f, -3.0f, 3.0f);
            const float m = h.motion.update(raw, now);
            const float bend = rounded(raw, m);
            h.shown = bend;
            if (due && std::fabs(bend - h.sent[0]) > 0.005f) {
                h.sent[0] = bend;
                h.lastSend = now;
                param(ctx, PBend, 0, bend, 0, now);
            }
            return;
        }
        case Role::Throw: {
            const float a = throwAmount(t);
            if (due && std::fabs(a - h.sent[0]) > 0.01f) {
                h.sent[0] = a;
                h.lastSend = now;
                param(ctx, PThrow, 0, a, 0, now);
            }
            return;
        }
        case Role::None:
            return;
        }
    }

    void lift(Held &h, double now, ControlContext &ctx) {
        switch (h.role) {
        case Role::Note:
            off(ctx, h.id, now);
            break;
        case Role::Harmony: {
            Slot &s = m_slots[h.slot];
            s.held = false;
            if (!m_latch) {
                s.on = false;
                param(ctx, PHarmony, h.slot, s.row, 0, now);
            } else {
                s.row = std::round(s.row);
                param(ctx, PHarmony, h.slot, s.row, s.level, now);
            }
            break;
        }
        case Role::Bend:
            if (h.sent[0] != 0)
                param(ctx, PBend, 0, 0, 0, now);
            break;
        case Role::Throw:
            param(ctx, PThrow, 0, 0, 0, now);
            break;
        case Role::None:
            break;
        }
        h.id = -1;
    }

    void fillNote(Command &c, const Held &h, const Touch &t, float pos, float motion, float twist) const {
        c.f[1] = t.y;
        c.f[2] = pos - float(h.q);
        c.f[3] = motion;
        c.f[4] = clampf((t.size - 0.8f) / 1.0f, 0.0f, 1.0f); // a normal touch is 0.2
        c.f[5] = twist;
        c.f[6] = t.pressure;
    }

    static float harmonyRow(float y) {
        return clampf((y - kHarmBottom) / (1.0f - kHarmBottom) * float(kRows) - 0.5f, 0.0f, float(kRows - 1));
    }
    static float throwAmount(const Touch &t) {
        return clampf((0.45f + 0.55f * t.y / kHarmBottom) * (0.75f + 0.5f * t.pressure), 0.0f, 1.0f);
    }

    // The piano rows: each a run of the scale from a root, an octave apart.
    // The keys past L and P differ between layouts, so both the US ones and
    // the Nordic ones (this is often played on a Swedish Mac keyboard) play
    // the same notes.
    static int pianoIndex(int key) {
        static const int rows[3][12] = {
            {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_N, Qt::Key_M, Qt::Key_Comma,
             Qt::Key_Period, Qt::Key_Slash, 0, 0},
            {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L,
             Qt::Key_Semicolon, Qt::Key_Apostrophe, 0},
            {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O,
             Qt::Key_P, Qt::Key_BracketLeft, Qt::Key_BracketRight}};
        // - is the shared tone key (docs/PARTS.md 4b), so the Nordic Z row
        // ends before it.
        static const int nordic[3][12] = {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, Qt::Key_Odiaeresis, Qt::Key_Adiaeresis, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, Qt::Key_Aring, 0}};
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 12; ++i)
                if ((rows[r][i] && rows[r][i] == key) || (nordic[r][i] && nordic[r][i] == key))
                    return r * 12 + i;
        return -1;
    }
    int pianoDegree(int key) const {
        const int idx = pianoIndex(key);
        if (idx < 0)
            return -1;
        return (idx / 12) * m_per + idx % 12;
    }

    QString describe() const;
    int notesHeld(const Held *besides) const {
        int n = 0;
        for (const auto &o : m_held)
            n += &o != besides && o.id >= 0 && o.role == Role::Note;
        for (const auto &k : m_keys)
            n += k.down;
        return n;
    }
    bool harmonyOn() const {
        for (const auto &sl : m_slots)
            if (sl.on)
                return true;
        return false;
    }

    std::array<Held, 16> m_held{};
    std::array<KeyHeld, 36> m_keys{};
    Slot m_slots[kHarmSlots];
    double m_takenAt = -1e9; // when Space last took a phrase
    int m_seenPlaying = -1, m_seenEvents = 0;
    float m_seenLength = 0;
    QString m_landed;        // what the last take turned into
    double m_landedAt = -1e9;
    std::array<double, 256> m_onsets{}; // when the last notes landed, touch clock
    long m_onsetHead = 0;
    QString m_loopText, m_loopHint; // the take on its way; what Space would do now
    Frame m_frame;
    int m_per = 7, m_neckOct = 1, m_shift = 0, m_sound = 0, m_glide = 0, m_gate = 0, m_tone = 0;
    console::Hotspots m_spots; // what paint() drew to be clicked
    bool m_follow = false, m_latch = false, m_click = false;
    uint32_t m_glides = 0, m_wiggles = 0, m_harmonized = 0; // for the tutorial
    QString m_status;
    std::array<TrailPoint, kTrail> m_trail{};
    long m_trailHead = 0;
    double m_lastSample = 0;
};

void LeadControl::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const StudioView &sv = *pc.studio;
    const PartView &pv = *pc.view;
    const QColor col = pc.color;
    Tuning tuning;
    tuning.set(sv.root, sv.family, sv.brightness, sv.just);
    const int per = tuning.degreesPerOctave();
    const int cells = neckCells(per);
    const int neckBase = (neckOctave(sv.root) + m_shift) * per;
    const qreal unit = pc.pad.height() / 600.0;
    auto midiOf = [&](double degree) { return tuning.baseMidi() + tuning.continuousSemis(degree); };
    auto xOf = [&](float absDegree) { return neckX(absDegree - float(neckBase), per); };
    // A line sung above or below the neck (an octave leap, a high harmony)
    // is drawn folded back onto it by octaves, fainter, so it never just
    // disappears.
    auto fold = [&](float absDegree, int *octaves) {
        float q = absDegree - float(neckBase);
        int o = 0;
        while (q > float(cells) - 0.5f && o < 4) {
            q -= float(per);
            ++o;
        }
        while (q < -0.5f && o > -4) {
            q += float(per);
            --o;
        }
        *octaves = o;
        return q + float(neckBase);
    };

    c::pad(p, pc, QStringLiteral("Lead"));
    p.setRenderHint(QPainter::Antialiasing, true);

    // --- the neck ---
    const QRectF neck = pc.rect(kColumn, 0, 1, 1);
    const bool follow = m_follow;
    auto isChordTone = [&](int degree) {
        for (int k = 0; k < sv.chordCount; ++k)
            if (((degree - sv.chordTones[k]) % per + per) % per == 0)
                return true;
        return false;
    };
    // Columns first, unsmoothed: they sit on whole pixels anyway, and the
    // rasteriser's antialiased path is several times slower on big fills.
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int k = 0; k < cells; ++k) {
        const int abs = neckBase + k;
        const QRectF cell(neck.left() + neck.width() * k / cells, neck.top(), neck.width() / cells, neck.height());
        if (((abs % per) + per) % per == 0)
            p.fillRect(cell, c::alpha(col, 0.09f));
        if (follow && isChordTone(abs))
            p.fillRect(cell, c::alpha(col, 0.07f));
    }
    p.setPen(QPen(c::alpha(col, 0.15f), 1));
    for (int k = 1; k < cells; ++k) {
        const qreal x = std::round(neck.left() + neck.width() * k / cells) + 0.5;
        p.drawLine(QPointF(x, neck.top()), QPointF(x, neck.bottom()));
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    // The note names, and over them a mark on each chord tone: the safe
    // places to land.
    p.setFont(c::font(int(11 * unit + 1), true));
    for (int k = 0; k < cells; ++k) {
        const int abs = neckBase + k;
        const QRectF cell(neck.left() + neck.width() * k / cells, neck.top(), neck.width() / cells, neck.height());
        const bool tone = isChordTone(abs);
        if (tone) {
            const QRectF mark(cell.center().x() - 3 * unit - 1, cell.bottom() - 36 * unit, 6 * unit + 2, 6 * unit + 2);
            p.setPen(Qt::NoPen);
            p.setBrush(c::alpha(col, follow ? 0.9f : 0.45f));
            p.drawEllipse(mark);
        }
        p.setPen(c::alpha(col, follow && !tone ? 0.3f : 0.75f));
        p.drawText(QRectF(cell.left(), cell.bottom() - 24 * unit, cell.width(), 20 * unit), Qt::AlignCenter,
                   QString::fromUtf8(Tuning::noteName(int(std::lround(midiOf(abs))))));
    }
    // The height's meaning, on the right edge.
    {
        const bool vox = sound(m_sound).formant;
        const QRectF r(neck.right() - 70 * unit, neck.top() + 6 * unit, 64 * unit, 16 * unit);
        c::text(p, r, vox ? QStringLiteral("ee") : QStringLiteral("bright"), c::alpha(col, 0.4f), int(10 * unit + 1),
                false, Qt::AlignRight | Qt::AlignVCenter);
        c::text(p, r.translated(0, neck.height() * 0.5 - 14 * unit), vox ? QStringLiteral("ah") : QString(),
                c::alpha(col, 0.4f), int(10 * unit + 1), false, Qt::AlignRight | Qt::AlignVCenter);
        c::text(p, r.translated(0, neck.height() - 64 * unit), vox ? QStringLiteral("oo") : QStringLiteral("dark"),
                c::alpha(col, 0.4f), int(10 * unit + 1), false, Qt::AlignRight | Qt::AlignVCenter);
    }

    // The trail: the last few seconds of what each line sang, rising from
    // the note names as it ages. Glides are curves, vibrato is a ripple. It
    // is drawn as a few polylines, one per stretch of a tenth of the span,
    // each as bright and thick as it was loud: thousands of separate strokes
    // would hold the session's lock for milliseconds every frame.
    {
        p.save();
        p.setClipRect(neck.adjusted(1, 1, -1, -1));
        const double span = 3.2;
        const int buckets = 10;
        const double now = pc.time;
        const qreal floorY = neck.bottom() - 40 * unit, rise = neck.height() - 40 * unit;
        auto xAt = [&](float pos) { return neck.left() + (xOf(pos) - kColumn) / (1 - kColumn) * neck.width(); };
        for (int k = kClipLayers; k >= 0; --k) {
            const QColor lc = k == 0 ? col : c::alpha(col, 0.55f);
            for (int b = 0; b < 1 + kHarmSlots; ++b) {
                QPolygonF poly;
                float levelSum = 0;
                int bucket = -1, octaves = 0;
                auto flush = [&] {
                    if (poly.size() >= 2) {
                        const float la = levelSum / float(poly.size());
                        const float fade = float(1.0 - (bucket + 0.5) / buckets) * (octaves ? 0.45f : 1.0f);
                        const float w = (b == 0 ? 2.0f : 1.2f) + (b == 0 ? 7.0f : 3.0f) * std::min(1.0f, la);
                        p.setPen(QPen(c::alpha(lc, fade * (b == 0 ? 0.85f : 0.5f) * std::min(1.0f, 0.4f + la)),
                                      w * unit, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                        p.drawPolyline(poly);
                    }
                    poly.clear();
                    levelSum = 0;
                };
                for (int i = 1; i < kTrail; ++i) {
                    const TrailPoint &a = m_trail[size_t((m_trailHead - i + kTrail) % kTrail)];
                    if (a.t < 0 || a.t > now + 0.05)
                        break;
                    const double age = std::max(0.0, now - a.t);
                    if (age > span)
                        break;
                    const float level = a.level[k][b];
                    int oct = 0;
                    const float pos = fold(a.pos[k][b], &oct);
                    const int bk = std::min(buckets - 1, int(age / span * buckets));
                    const QPointF pt(xAt(pos), floorY - rise * age / span);
                    // A silence, a jump, a new octave or a new stretch ends a stroke;
                    // a new stretch starts where the last one ended, so it stays whole.
                    const bool gap = level < 0.01f || (!poly.isEmpty() && (oct != octaves ||
                                                                          std::fabs(pt.x() - poly.last().x()) > neck.width() * 0.2));
                    if (gap) {
                        flush();
                        if (level < 0.01f)
                            continue;
                    } else if (bk != bucket && !poly.isEmpty()) {
                        const QPointF joint = poly.last();
                        flush();
                        poly << joint;
                    }
                    bucket = bk;
                    octaves = oct;
                    poly << pt;
                    levelSum += level;
                }
                flush();
            }
        }
        p.restore();
    }

    // Where each line sings right now: a bright string across the neck, the
    // live one and each layer of the playing loop.
    for (int k = kClipLayers; k >= 0; --k) {
        const int line = k == 0 ? 0 : (pv.playing >= 0 ? shownLine(pv.playing, k - 1) : -1);
        if (line < 0)
            continue;
        const float *d = pv.data + kLineData * line;
        if (d[0] <= 0 || d[2] < 0.005f)
            continue;
        auto string = [&](float pos, float level, float width) {
            int octaves = 0;
            pos = fold(pos, &octaves);
            if (octaves)
                level *= 0.5f;
            const qreal x = neck.left() + (xOf(pos) - kColumn) / (1 - kColumn) * neck.width();
            QLinearGradient g(QPointF(x - 14 * unit, 0), QPointF(x + 14 * unit, 0));
            g.setColorAt(0, c::alpha(col, 0));
            g.setColorAt(0.5, c::alpha(col, (k == 0 ? 0.35f : 0.2f) * std::min(1.0f, level * 1.5f)));
            g.setColorAt(1, c::alpha(col, 0));
            p.setRenderHint(QPainter::Antialiasing, false);
            p.fillRect(QRectF(x - 14 * unit, neck.top(), 28 * unit, neck.height()), g);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(c::alpha(col, std::min(1.0f, 0.3f + level)), width * unit,
                          octaves ? Qt::DashLine : Qt::SolidLine));
            p.drawLine(QPointF(x, neck.top()), QPointF(x, neck.bottom() - 40 * unit));
        };
        string(d[1], d[2], k == 0 ? 2.5f : 1.5f);
        for (int s = 0; s < kHarmSlots; ++s)
            if (d[7 + s] > 0.02f)
                string(d[4 + s], d[2] * d[7 + s], 1.2f);
    }

    // --- the left hand's column ---
    const QRectF column = pc.rect(0, 0, kColumn, 1);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(column, QColor(10, 7, 0));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c::alpha(col, 0.7f), 1.5));
    p.drawLine(QPointF(column.right(), column.top()), QPointF(column.right(), column.bottom()));
    const float *live = pv.data;
    for (int row = 0; row < kRows; ++row) {
        const float y0 = kHarmBottom + (1 - kHarmBottom) * float(row) / kRows;
        const float y1 = kHarmBottom + (1 - kHarmBottom) * float(row + 1) / kRows;
        const QRectF r = pc.rect(0, y0, kColumn, y1).adjusted(6 * unit, 4 * unit, -6 * unit, -4 * unit);
        bool lit = false;
        float level = 0;
        for (int s = 0; s < kHarmSlots; ++s) {
            if (m_slots[s].on && int(std::lround(m_slots[s].row)) == row) {
                lit = true;
                level = std::max(level, live[7 + s]);
            }
        }
        // A lit row glows as brightly as its voice sounds, with a solid bar
        // at its edge; the finger on it stays visible.
        p.setPen(QPen(c::alpha(col, lit ? 1.0f : 0.35f), 1.5, lit && m_latch ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(lit ? QBrush(c::alpha(col, 0.18f + 0.3f * std::min(1.0f, level * 1.5f))) : Qt::NoBrush);
        p.drawRect(r);
        if (lit)
            p.fillRect(QRectF(r.left(), r.top(), 6 * unit, r.height()), col);
        c::text(p, r.adjusted(14 * unit, 0, 0, 0), QString::fromUtf8(rowName(row)), lit ? col : c::alpha(col, 0.7f),
                int(15 * unit + 1), true, Qt::AlignLeft | Qt::AlignVCenter);

    }
    // BEND: a wheel's slot with the bend drawn from its centre.
    {
        const QRectF r = pc.rect(0, 0, kBendSplit, kHarmBottom).adjusted(6 * unit, 6 * unit, -3 * unit, -6 * unit);
        p.setPen(QPen(c::alpha(col, 0.6f), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
        const float bend = live[10];
        const qreal mid = r.center().y();
        const qreal step = (r.height() - 30 * unit) / 6.0;
        p.setPen(QPen(c::alpha(col, 0.3f), 1));
        for (int s = -3; s <= 3; ++s)
            p.drawLine(QPointF(r.left() + r.width() * 0.3, mid - s * step), QPointF(r.right() - r.width() * 0.3, mid - s * step));
        if (std::fabs(bend) > 0.01f) {
            const QRectF bar(r.left() + r.width() * 0.25, std::min(mid, mid - bend * step), r.width() * 0.5,
                             std::fabs(bend * step));
            p.fillRect(bar, c::alpha(col, 0.8f));
        }
        p.setPen(QPen(col, 2));
        p.drawLine(QPointF(r.left() + 4 * unit, mid - bend * step), QPointF(r.right() - 4 * unit, mid - bend * step));
        c::text(p, QRectF(r.left(), r.bottom() - 20 * unit, r.width(), 18 * unit), QStringLiteral("bend"),
                c::alpha(col, 0.8f), int(12 * unit + 1), true, Qt::AlignCenter);
    }
    // THROW: how hard, and the echo it leaves.
    {
        const QRectF r = pc.rect(kBendSplit, 0, kColumn, kHarmBottom).adjusted(3 * unit, 6 * unit, -6 * unit, -6 * unit);
        const float amt = live[11];
        const float echo = std::min(1.0f, pv.data[kEchoLevel] * 2.0f);
        p.setPen(QPen(c::alpha(col, amt > 0 ? 1.0f : 0.6f), 1.5));
        p.setBrush(amt > 0 ? QBrush(c::alpha(col, 0.15f + 0.4f * amt)) : Qt::NoBrush);
        p.drawRect(r);
        const QRectF meter(r.left() + r.width() * 0.35, r.top() + 8 * unit, r.width() * 0.3, r.height() - 36 * unit);
        const int segs = 12;
        for (int s = 0; s < segs; ++s) {
            const qreal h = meter.height() / segs;
            const QRectF seg(meter.left(), meter.bottom() - (s + 1) * h + 1, meter.width(), h - 2);
            p.fillRect(seg, float(s) / segs < echo ? col : c::alpha(col, 0.12f));
        }
        c::text(p, QRectF(r.left(), r.bottom() - 20 * unit, r.width(), 18 * unit), QStringLiteral("throw"),
                c::alpha(col, 0.8f), int(12 * unit + 1), true, Qt::AlignCenter);
    }

    // The take, straight from the studio's view of this part; once it has
    // landed, what it became.
    m_loopText = loopState(pc.beat, pv);
    if (m_loopText.isEmpty() && pv.playing >= 0 && pc.time - m_landedAt < 3.0)
        m_loopText = m_landed;
    // The loop machinery, at the top of the neck where it can't be missed:
    // a loop counting down to its bar line, a recording armed or running,
    // or, quietly, what Space would loop right now.
    {
        const QPointF at(neck.center().x(), neck.top() + 16 * unit);
        if (!m_loopText.isEmpty()) {
            const bool rec = !m_loopText.startsWith(QStringLiteral("looping")); // waiting, not landed
            const bool blink = std::fmod(pc.beat, 1.0) < 0.5;
            c::tag(p, at, QStringLiteral("● ") + m_loopText, rec ? (blink ? c::red() : c::alpha(c::red(), 0.6f)) : col,
                   int(13 * unit + 1), true);
        } else if (!m_loopHint.isEmpty()) {
            c::text(p, QRectF(at.x() - 180 * unit, at.y() - 9 * unit, 360 * unit, 18 * unit), m_loopHint,
                    c::alpha(col, 0.5f), int(11 * unit + 1), true, Qt::AlignCenter);
        }
    }

    // Modes, at the neck's top left.
    {
        qreal x = neck.left() + 10 * unit;
        const qreal y = neck.top() + 16 * unit;
        auto mode = [&](const QString &s, bool on) {
            if (on) {
                const QRectF r = c::tag(p, QPointF(x, y), s, col, int(11 * unit + 1));
                x = r.right() + 6 * unit;
            }
        };
        mode(QStringLiteral("follow chord"), m_follow);
        mode(m_shift > 0 ? QStringLiteral("oct +1") : QStringLiteral("oct −1"), m_shift != 0);
        mode(QStringLiteral("glide ×%1").arg(double(std::exp2(float(m_glide)))), m_glide != 0);
        mode(QStringLiteral("gate %1").arg(QString::fromUtf8(gateName(m_gate))), m_gate != 0);
        mode(QStringLiteral("tone %1%2").arg(m_tone > 0 ? "+" : "−").arg(std::abs(m_tone)), m_tone != 0);
        mode(QStringLiteral("8va"), m_click);
        mode(QStringLiteral("harmony held"), m_latch);
        if (live[14] > 0.02f)
            mode(QStringLiteral("vibrato"), true);
    }

    // An empty pad says how to start.
    if (m_frame.count == 0 && live[0] <= 0 && pv.playing < 0 && m_loopHint.isEmpty() && m_loopText.isEmpty()) {
        const QRectF r(neck.left(), neck.center().y() - 50 * unit, neck.width(), 100 * unit);
        p.setPen(c::alpha(col, 0.45f));
        p.setFont(c::font(int(14 * unit + 1), true));
        p.drawText(r, Qt::AlignCenter,
                   QStringLiteral("one finger sings · a second glides to it\n"
                                  "slide, wiggle, press, twist, click\n"
                                  "left hand: harmony · bend · throw\n"
                                  "space loops what you played"));
    }

    // Fingers, each saying what it does.
    int topNote = -1;
    double topT = -1;
    for (const auto &h : m_held)
        if (h.id >= 0 && h.role == Role::Note && h.t0 > topT) {
            topT = h.t0;
            topNote = h.id;
        }
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        const Held *h = nullptr;
        for (const auto &o : m_held)
            if (o.id == t.id)
                h = &o;
        QString label;
        float glow = 0;
        if (h) {
            switch (h->role) {
            case Role::Note:
                if (t.id == topNote) {
                    label = live[0] > 0 ? noteName(midiOf(live[1])) : QString();
                    glow = std::min(1.0f, 0.3f + live[2]);
                } else {
                    label = noteName(midiOf(h->abs));
                }
                break;
            case Role::Harmony:
                // The row says the interval; the finger says the note it makes.
                label = live[0] > 0 && live[7 + h->slot] > 0.05f ? noteName(midiOf(live[4 + h->slot]))
                                                                  : QString::fromUtf8(rowName(int(std::lround(m_slots[h->slot].row))));
                break;
            case Role::Bend:
                label = QStringLiteral("%1%2").arg(h->shown >= 0 ? "+" : "").arg(double(h->shown), 0, 'f', 1);
                break;
            case Role::Throw:
                label = QStringLiteral("throw");
                glow = 0.6f;
                break;
            case Role::None:
                break;
            }
        }
        c::finger(p, pc, t, h && h->role == Role::Note && t.id != topNote ? c::alpha(col, 0.6f) : col,
                  label, glow);
    }
    c::beats(p, pc, col);
    // Below the pad, every one clickable: the clips and the sounds, then
    // every setting the keys reach. A setting stepped by two keys is a
    // stepper whose two ends are those keys; a switch is a chip lit when
    // on, showing its key.
    m_spots.clear();
    // Shares a row out between items in proportion to the room each
    // wants, so the longest names keep their letters on a narrow window.
    auto lay = [](const QRectF &row, const std::vector<qreal> &want, qreal gap) {
        qreal total = 0;
        for (qreal w : want)
            total += w;
        const qreal scale = (row.width() - gap * qreal(want.size() - 1)) / total;
        std::vector<QRectF> out;
        qreal x = row.left();
        for (qreal w : want) {
            out.emplace_back(x, row.top(), w * scale, row.height());
            x += w * scale + gap;
        }
        return out;
    };
    auto chipWant = [&](const QString &label) { return console::textWidth(c::font(12, true), label) + 12; };
    c::clips(p, c::bandRow(pc, 0, 0, 0.42), pv, col, pc.time, &m_spots);
    {
        // More sounds than the row has room for: the shared chip row shows
        // a window onto them that follows the one chosen, faded at an end
        // where there are more beyond it.
        QStringList names;
        for (int s = 0; s < kSounds; ++s)
            names << QString::fromUtf8(sound(s).name).toUpper();
        c::chips(p, pc, c::bandRow(pc, 0, 0.43, 1), names, m_sound, &m_spots, SpotSound);
    }
    {
        struct Stepper {
            QString name, down, up;
            int downId, upId;
            float value; // where the gauge stands, 0..1
            int segments;
            bool bipolar;
        };
        struct Switch {
            QString label;
            bool lit;
            int id;
        };
        const Stepper steppers[] = {
            {QStringLiteral("OCT"), QStringLiteral("↓"), QStringLiteral("↑"), SpotOctDown, SpotOctUp,
             (float(m_shift) + 1) / 2, 2, true},
            {QStringLiteral("GLIDE"), QStringLiteral("⇧↓"), QStringLiteral("⇧↑"), SpotGlideDown, SpotGlideUp,
             (float(m_glide) + 2) / 4, 4, true},
            {QStringLiteral("TONE"), QStringLiteral("-"), QStringLiteral("+"), SpotDarker, SpotBrighter,
             (float(m_tone) + kToneSteps) / (2 * kToneSteps), 2 * kToneSteps, true},
            {m_gate ? QString::fromUtf8(gateName(m_gate)) : QStringLiteral("GATE"), QStringLiteral("⇧←"),
             QStringLiteral("⇧→"), SpotGateDown, SpotGateUp, float(m_gate) / (kGates - 1), kGates - 1, false},
        };
        const Switch switches[] = {
            {QStringLiteral("FOLLOW §"), m_follow, SpotFollow},
            {QStringLiteral("HOLD ↵"), m_latch, SpotHold},
            {QStringLiteral("OVERDUB ⇧↵"), pv.overdub, SpotOverdub},
            {QStringLiteral("LET GO DEL"), false, SpotLetGo},
        };
        const qreal end = chipWant(QStringLiteral("⇧↓"));
        std::vector<qreal> want;
        for (const auto &st : steppers)
            want.push_back(2 * end + std::max(console::textWidth(c::font(11, true), QStringLiteral("GLIDE")),
                                              console::textWidth(c::font(11, true), st.name)) + 4);
        for (const auto &sw : switches)
            want.push_back(chipWant(sw.label));
        const auto cells = lay(c::bandRow(pc, 1), want, 6);
        size_t i = 0;
        for (const auto &st : steppers) {
            const QRectF g = cells[i++];
            const qreal e = std::min(end, g.width() * 0.3);
            // A faint frame makes the two keys and the gauge read as one control.
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c::alpha(col, 0.3f), 1));
            p.drawRect(g);
            c::chip(p, pc, QRectF(g.left(), g.top(), e, g.height()), st.down, false, &m_spots, st.downId);
            c::chip(p, pc, QRectF(g.right() - e, g.top(), e, g.height()), st.up, false, &m_spots, st.upId);
            const QRectF mid(g.left() + e, g.top(), g.width() - 2 * e, g.height());
            c::text(p, mid.adjusted(0, 0, 0, -11), st.name, col, 11, true, Qt::AlignCenter);
            c::gauge(p, QRectF(mid.left() + 4, mid.bottom() - 9, mid.width() - 8, 6), st.value, col, st.segments,
                     st.bipolar);
        }
        for (const auto &sw : switches)
            c::chip(p, pc, cells[i++], sw.label, sw.lit, &m_spots, sw.id);
    }
    // The legend again, over the column, and the sound after it.
    {
        const QFont legend = c::font(13, true);
        const qreal lw = console::textWidth(legend, QStringLiteral("Lead")) + 16;
        const QRectF lr(pc.pad.left() + 16, c::tabY(pc) - 9, lw, 18);
        p.fillRect(lr, QColor(0, 0, 0));
        c::text(p, lr, QStringLiteral("Lead"), col, 13, true, Qt::AlignCenter);
        c::variation(p, pc, QString::fromUtf8(sound(m_sound).name));
    }

}

QString LeadControl::describe() const {
    QStringList parts;
    if (!m_loopText.isEmpty())
        parts << m_loopText;
    // The sound, and what music it is for, so a newcomer knows what to
    // reach for it for: "Steel pan · calypso".
    const Sound &snd = sound(m_sound);
    parts << (*snd.style ? QStringLiteral("%1 · %2").arg(QString::fromUtf8(snd.name), QString::fromUtf8(snd.style))
                         : QString::fromUtf8(snd.name));
    int notes = 0;
    for (const auto &h : m_held)
        notes += h.id >= 0 && h.role == Role::Note;
    for (const auto &k : m_keys)
        notes += k.down;
    // An accordion's register is the finger's height: say which one the
    // melody's finger has drawn (from the keys, the bandoneon's).
    if (snd.model == Sound::Reed && notes > 0) {
        float y = 0.6f;
        for (const auto &h : m_held)
            if (h.id >= 0 && h.role == Role::Note)
                for (int i = 0; i < m_frame.count; ++i)
                    if (m_frame.touches[i].id == h.id)
                        y = m_frame.touches[i].y;
        parts << QStringLiteral("%1 stop").arg(QString::fromUtf8(registerName(y)));
    }
    if (notes == 1)
        parts << QStringLiteral("singing");
    else if (notes > 1)
        parts << QStringLiteral("legato, %1 held").arg(notes);
    QStringList harm;
    for (const auto &s : m_slots)
        if (s.on)
            harm << QString::fromUtf8(rowName(int(std::lround(s.row))));
    if (!harm.isEmpty())
        parts << QStringLiteral("harmony ") + harm.join(QStringLiteral(" "));
    for (const auto &h : m_held) {
        if (h.id < 0)
            continue;
        if (h.role == Role::Bend)
            parts << QStringLiteral("bend %1").arg(double(h.shown), 0, 'f', 1);
        if (h.role == Role::Throw)
            parts << QStringLiteral("throwing");
    }
    if (m_click)
        parts << QStringLiteral("an octave up");
    if (m_shift)
        parts << (m_shift > 0 ? QStringLiteral("neck an octave up") : QStringLiteral("neck an octave down"));
    if (m_follow)
        parts << QStringLiteral("on the chord");
    if (m_gate)
        parts << QStringLiteral("gated ") + QString::fromUtf8(gateName(m_gate));
    if (m_tone)
        parts << QStringLiteral("tone %1%2").arg(m_tone > 0 ? "+" : "−").arg(std::abs(m_tone));
    if (m_latch)
        parts << QStringLiteral("harmony held");
    return parts.join(QStringLiteral(" · "));
}

} // namespace lead

PartSpec makeLead() {
    PartSpec s;
    s.name = QStringLiteral("Lead");
    s.blurb = QStringLiteral("melody, harmony, glide");
    s.color = QColor("#FFB000");
    s.engine = std::make_unique<lead::LeadEngine>();
    s.control = std::make_unique<lead::LeadControl>();
    // The lead has its own echo; the room's tape echo only adds a little air.
    const float channel[ChannelCount] = {0.69f, 0.5f, 0.5f, 0.28f, 0.08f, 0.35f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
