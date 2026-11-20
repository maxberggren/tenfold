#pragma once

// The bass page's sound, and the words its controls and engine share.
//
// One mono bass, built the way a producer builds one in a session: a clean
// sine sub under a crossover that follows the note, and above it the part
// with character (saws, squares, FM, a ladder and a vowel filter, drive
// that is oversampled and level-matched), so the weight under 100 Hz is
// always one sine in phase with the note, however hard the top is pushed.
// Eight sounds share that one voice and differ only in their recipe: a
// clean sub, a rolling pluck, a reese, an acid line, a wobble, a growl, an
// 808 and a bouncing house bass.
//
// There are two lanes, each a mono synth with its own note stack: the
// player's fingers and keys, and the clip that is looping. While the
// player holds a note the loop steps aside, so the low end is never two
// basses at once; the shapers the player holds (octave, roll, wobble,
// grit) work on whatever is sounding, the loop included.

#include <array>
#include <cmath>

#include "electro.h"
#include "part.h"

namespace tf::bass {

// --- the vocabulary -----------------------------------------------------------

enum Sound : int { Sub, Pluck, Reese, Acid, Wobble, Growl, Boom, Bounce, SoundCount };

// Commands, besides the studio's own:
//   On     id; i[0] degree, i[1] octave, i[2] NoteFlags, i[3] Sound;
//          f[0] velocity, f[1] tone 0..1, f[2] morph 0..1, f[3] fat 0..1, f[4] glide 0..1,
//          f[5] the wobble rate a sound that wobbles on its own uses (the WOB finger's last)
//   Move   id; i[0] degree, i[1] octave; f[0] pressure, f[1] tone, f[2] morph, f[3] fat
//   Off    id
//   Param  i[0] Shaper; f[0] amount (0 = off), f[1] its second axis;
//          or i[0] ResetSlot, f[0] the slot (never recorded)
//   Strike id: the note this id plays is struck again with an accent (the click)
constexpr uint16_t kStrike = Command::User;
enum NoteFlag : int { Follow = 1, Accent = 2 };
enum Shaper : int { ShapeOct, ShapeRoll, ShapeWob, ShapeGrit, ShaperCount, ResetSlot = 15 };

// The roll's rhythms, one beat each: a held note is struck on the lit steps.
// The kick owns the downbeat, so most of them leave it free.
struct Pattern {
    const char *name;
    const char *steps; // 'x' struck, '-' rest
};
constexpr Pattern kPatterns[] = {
    {"OFFBEAT", "--x-"}, {"SKIP", "-x-x"}, {"GALLOP", "x-xx"}, {"ROLL", "-xxx"},
    {"DRIVE", "xxxx"},   {"TRIPLET", "-xx"}, {"STUTTER", "xxxxxxxx"},
};
constexpr int kPatternCount = int(sizeof(kPatterns) / sizeof(kPatterns[0]));
inline int patternSteps(int p) {
    int n = 0;
    while (kPatterns[p].steps[n])
        ++n;
    return n;
}

// Wobble rates, in beats per cycle.
constexpr double kRateBeats[] = {2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
constexpr const char *kRateNames[] = {"1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
constexpr int kRateCount = 8;

inline const char *gritName(float character) {
    return character < 0.34f ? "WARM" : character < 0.67f ? "TUBE" : "FOLD";
}

// What the engine publishes for the page, sixty times a second.
enum Pub : int {
    PubLane = 0,      // two lanes of 8: live, then clip
    PubOn = 0,        // sounding (1), or releasing (0.5)
    PubMidi = 1,      // the pitch now
    PubLevel = 2,
    PubDegree = 3,    // the pad's degree played (degree + octave × degrees per octave)
    PubSound = 4,
    PubCut = 5,       // the filter now, 0..1
    PubEnv = 6,       // the amp envelope
    PubGate = 7,      // 1 while a roll step is open
    PubLfo = 16,      // the wobble's position 0..1, and its depth
    PubWobDepth = 17,
    PubWobRate = 18,
    PubRoll = 19,     // the roll's pattern + 1, or 0
    PubRollStep = 20, // the step it is on
    PubGrit = 21,
    PubGritChar = 22,
    PubOct = 23,
    PubChordShift = 24, // degrees the chord moves a following line
    PubClipMute = 25,   // how far the loop has stepped aside
    PubScopeCycles = 26,
    PubScope = 128,     // 128 points: two cycles of the waveform, phase-locked
    PubScopeSize = 128,
};

// --- the sounds -----------------------------------------------------------------

struct Recipe {
    const char *name = "";
    const char *what = "";
    // Oscillators, all locked in phase to the sub: a sine, a unison of
    // saws, a square, a phase-modulated sine and a saw an octave up.
    float sine = 0, saw = 0, square = 0, fm = 0, octSaw = 0;
    int unison = 1;          // saws, 1–3
    float detune = 0;        // cents the outer saws sit from the centre
    float fmRatio = 1, fmIndex = 0;
    float foldPre = 0;       // wavefolding of the sine and FM before the filter
    float sub = 1;           // the clean sine under the crossover
    // The finger's height (tone) runs the cutoff between these; 0..1 is
    // 50 Hz .. 16 kHz.
    float cutLo = 0.1f, cutHi = 0.7f, res = 0.2f;
    float envAmt = 0.3f, envDecay = 0.25f; // filter envelope, seconds
    float formant = 0;       // 0 all ladder .. 1 all vowel filter
    float vowel = 0, vowelTone = 0, vowelEnv = 0; // 0..4 = u o a e i
    float attack = 0.002f, decay = 0.4f, sustain = 0.8f, release = 0.08f;
    float drive = 0.2f, character = 0.2f, toneDrive = 0;
    bool driveFirst = false; // drive, then filter: the tone shapes the harmonics the drive made
    float drop = 0, dropTime = 0.04f; // semitones the note falls from, and how fast
    float glide = 0.05f;     // legato slide, seconds
    float width = 0;         // stereo above 300 Hz
    float accent = 0.6f;
    float wobble = 0;        // its own wobble depth when no finger asks for one
    float wobCut = 1, wobVowel = 0, wobFm = 0;
    // What morph (the twist, [ and ]) does.
    float mSquare = 0, mDetune = 0, mFm = 0, mVowel = 0, mDrive = 0;
    float level = 1;
};

inline const Recipe &recipe(int sound) {
    static const std::array<Recipe, SoundCount> recipes = [] {
        std::array<Recipe, SoundCount> r{};
        {
            // A sine you feel more than hear; the tone axis drives it into a
            // soft, slightly lopsided clip, which adds the second and third
            // harmonics that let a sub speak on small speakers.
            Recipe &s = r[Sub];
            s.name = "SUB";
            s.what = "clean sine, warmth on top";
            s.sine = 1;
            s.sub = 1;
            s.cutLo = 0.2f, s.cutHi = 0.62f, s.res = 0;
            s.envAmt = 0.08f, s.envDecay = 0.2f;
            s.attack = 0.004f, s.decay = 0.5f, s.sustain = 0.92f, s.release = 0.09f;
            s.drive = 0.3f, s.character = 0.5f, s.toneDrive = 0.6f, s.driveFirst = true;
            s.glide = 0.04f;
            s.accent = 0.4f;
            s.mDrive = 0.8f;
            s.level = 0.8f;
        }
        {
            // The workhorse: a saw with some square through the ladder, a
            // short filter envelope, so every note has a pluck at the front
            // and a round body behind it. Rolling 16ths in trance, offbeats
            // in house.
            Recipe &s = r[Pluck];
            s.name = "PLUCK";
            s.what = "rolling saw, techno, trance";
            s.saw = 0.8f, s.square = 0.3f;
            s.sub = 0.85f;
            s.cutLo = 0.12f, s.cutHi = 0.72f, s.res = 0.3f;
            s.envAmt = 0.5f, s.envDecay = 0.16f;
            s.attack = 0.001f, s.decay = 0.22f, s.sustain = 0.5f, s.release = 0.06f;
            s.drive = 0.25f, s.character = 0.15f;
            s.glide = 0.045f;
            s.accent = 0.8f;
            s.mSquare = 1;
            s.level = 1.25f;
        }
        {
            // Three saws a few cents apart beat against each other, so the
            // note phases and tears on its own; wide above 300 Hz, one sine
            // below.
            Recipe &s = r[Reese];
            s.name = "REESE";
            s.what = "detuned saws, drum & bass";
            s.saw = 1, s.unison = 3, s.detune = 11;
            s.sub = 1;
            s.cutLo = 0.2f, s.cutHi = 0.7f, s.res = 0.18f;
            s.envAmt = 0.1f, s.envDecay = 0.4f;
            s.attack = 0.006f, s.decay = 0.6f, s.sustain = 0.9f, s.release = 0.14f;
            s.drive = 0.4f, s.character = 0.25f;
            s.glide = 0.09f;
            s.width = 0.75f;
            s.accent = 0.4f;
            s.mDetune = 18;
            s.level = 1.0f;
        }
        {
            // One saw, a ladder near self-oscillation and an envelope that
            // snaps it shut: accents bite, slides glide, and a distortion
            // after the filter, as the real thing went into a pedal.
            Recipe &s = r[Acid];
            s.name = "ACID";
            s.what = "squelch, accents, slides";
            s.saw = 1;
            s.sub = 0.55f;
            s.cutLo = 0.16f, s.cutHi = 0.6f, s.res = 0.8f;
            s.envAmt = 0.75f, s.envDecay = 0.2f;
            s.attack = 0.001f, s.decay = 0.35f, s.sustain = 0.7f, s.release = 0.03f;
            s.drive = 0.42f, s.character = 0.1f;
            s.glide = 0.065f;
            s.accent = 1;
            s.mSquare = 1;
            s.level = 1.3f;
        }
        {
            // Saws and a square with a little FM grit, a resonant filter the
            // tempo-synced LFO sweeps open and shut: the wub. It wobbles at
            // an eighth on its own; the WOB finger plays the rate.
            Recipe &s = r[Wobble];
            s.name = "WOBBLE";
            s.what = "synced filter, dubstep";
            s.saw = 0.7f, s.square = 0.45f, s.fm = 0.3f, s.unison = 2, s.detune = 7;
            s.fmIndex = 1.2f;
            s.sub = 0.9f;
            s.cutLo = 0.1f, s.cutHi = 0.72f, s.res = 0.45f;
            s.envAmt = 0.1f, s.envDecay = 0.3f;
            s.formant = 0.25f, s.vowel = 1, s.vowelTone = 1.2f;
            s.attack = 0.004f, s.decay = 0.5f, s.sustain = 1, s.release = 0.1f;
            s.drive = 0.45f, s.character = 0.4f;
            s.glide = 0.06f;
            s.width = 0.45f;
            s.accent = 0.6f;
            s.wobble = 0.55f, s.wobCut = 1, s.wobVowel = 0.6f, s.wobFm = 1.6f;
            s.mFm = 3;
            s.level = 1.05f;
        }
        {
            // Phase modulation folded and pushed through a vowel filter: the
            // finger's height and twist move the mouth, so it says "yoy" and
            // "wow" as it is played.
            Recipe &s = r[Growl];
            s.name = "GROWL";
            s.what = "FM and vowels that talk";
            s.saw = 0.2f, s.fm = 1, s.fmIndex = 2.2f, s.foldPre = 0.35f;
            s.sub = 0.9f;
            s.cutLo = 0.3f, s.cutHi = 0.8f, s.res = 0.25f;
            s.envAmt = 0.1f, s.envDecay = 0.3f;
            s.formant = 0.85f, s.vowel = 0.6f, s.vowelTone = 2.6f;
            s.attack = 0.004f, s.decay = 0.5f, s.sustain = 1, s.release = 0.1f;
            s.drive = 0.5f, s.character = 0.75f;
            s.glide = 0.06f;
            s.width = 0.55f;
            s.accent = 0.6f;
            s.wobCut = 0.3f, s.wobVowel = 1, s.wobFm = 2;
            s.mVowel = 2.5f, s.mFm = 1.2f;
            s.level = 0.65f;
        }
        {
            // The trap 808: a sine that falls an octave onto its note in a
            // few tens of milliseconds, rings for seconds, glides when notes
            // overlap, and is driven so a phone can hear it. The drive's
            // harmonics sit above the crossover; the sine under it stays clean.
            Recipe &s = r[Boom];
            s.name = "808";
            s.what = "falling sine, long, glides";
            s.sine = 1;
            s.sub = 1.1f;
            s.cutLo = 0.3f, s.cutHi = 0.75f, s.res = 0;
            s.envAmt = 0, s.envDecay = 0.2f;
            s.attack = 0.001f, s.decay = 1.6f, s.sustain = 0.3f, s.release = 0.25f;
            s.drive = 0.5f, s.character = 0.45f, s.toneDrive = 0.7f, s.driveFirst = true;
            s.drop = 12, s.dropTime = 0.035f;
            s.glide = 0.11f;
            s.accent = 0.5f;
            s.mDrive = 1.2f;
            s.level = 0.9f;
        }
        {
            // A square and a saw an octave up through a vowel filter that the
            // envelope opens from "o" to "a" on every note: the bouncy,
            // talking organ bass of bass house.
            Recipe &s = r[Bounce];
            s.name = "BOUNCE";
            s.what = "organ wah, bass house";
            s.saw = 0.3f, s.square = 0.8f, s.octSaw = 0.45f;
            s.sub = 0.85f;
            s.cutLo = 0.25f, s.cutHi = 0.7f, s.res = 0.2f;
            s.envAmt = 0.35f, s.envDecay = 0.14f;
            s.formant = 0.7f, s.vowel = 0.5f, s.vowelTone = 1.4f, s.vowelEnv = 2.2f;
            s.attack = 0.001f, s.decay = 0.2f, s.sustain = 0.3f, s.release = 0.05f;
            s.drive = 0.3f, s.character = 0.4f;
            s.glide = 0.035f;
            s.width = 0.25f;
            s.accent = 0.6f;
            s.mVowel = 1.5f;
            s.level = 1.5f;
        }
        return r;
    }();
    return recipes[size_t(std::clamp(sound, 0, SoundCount - 1))];
}

// The key's root as the lowest note a bass wants: between E1 and D#2
// (41–78 Hz), the octave every producer puts a sub in.
inline double rootShift(const Tuning &t) { return t.root() >= 4 ? -12.0 : 0.0; }

// How far a chord moves a line that follows it, in degrees: its root, folded
// to the nearer octave so the bass walks to the closest root and stays low.
inline int chordShift(const Harmony &h) {
    const int n = h.tuning.degreesPerOctave();
    int r = ((h.chordRoot % n) + n) % n;
    if (r > n / 2)
        r -= n;
    return r;
}

// --- DSP pieces -------------------------------------------------------------------

// 2× oversampling with a polyphase halfband. Of a 31-tap halfband only the
// 16 even-indexed side taps and the centre are not zero, so the upsampler's
// two output phases are a 16-tap filter and a plain delay, and the
// decimator the same in reverse: 32 multiplies a sample instead of 124.
struct Halfband2x {
    static constexpr int H = 16, D = 7; // side taps; the centre tap's delay in input samples
    float c[H] = {};
    float in[H] = {}, odd[H] = {}, even[H] = {};
    int pos = 0;
    Halfband2x() {
        constexpr int N = 31, centre = N / 2;
        float sum = 0;
        for (int k = 0; k < H; ++k) {
            const int i = 2 * k, n = i - centre;
            const float sinc = std::sin(kPi * 0.5f * float(n)) / (kPi * float(n));
            const float w = 0.42f - 0.5f * std::cos(kTwoPi * float(i) / (N - 1)) +
                            0.08f * std::cos(2.0f * kTwoPi * float(i) / (N - 1));
            c[k] = sinc * w;
            sum += c[k];
        }
        // The side taps carry half of DC, the centre tap (0.5) the other half.
        for (float &v : c)
            v *= 0.5f / sum;
    }
    void reset() {
        std::fill(std::begin(in), std::end(in), 0.0f);
        std::fill(std::begin(odd), std::end(odd), 0.0f);
        std::fill(std::begin(even), std::end(even), 0.0f);
    }
    template <typename F>
    inline float process(float x, F &&shape) {
        in[pos] = x;
        float a = 0;
        for (int k = 0, j = pos; k < H; ++k, j = (j + H - 1) & (H - 1))
            a += c[k] * in[j];
        // The two samples at twice the rate: the side taps (doubled, for the
        // zeros between), then the centre tap's delay of the input.
        even[pos] = shape(2.0f * a);
        odd[pos] = shape(in[(pos + H - D) & (H - 1)]);
        float y = 0.5f * even[(pos + H - D) & (H - 1)];
        for (int k = 0, j = pos; k < H; ++k, j = (j + H - 1) & (H - 1))
            y += c[k] * odd[j];
        pos = (pos + 1) & (H - 1);
        return y;
    }
};


// --- the engine -------------------------------------------------------------------

// What the shaper fingers ask for. A lane plays its own (a loop keeps the
// moves it was played with) under the player's live ones.
struct Shape {
    float oct = 0;   // octaves up, 0..2
    float roll = 0;  // gate length while rolling, 0 = not rolling
    int pattern = 3;
    float wob = 0;   // wobble depth while a finger wobbles, 0 = none
    int rate = 3;
    float grit = 0, gritChar = 0.3f;

    // The live shapers over a loop's own: the player's hand wins.
    static Shape over(const Shape &own, const Shape &live) {
        Shape s = own;
        s.oct = std::clamp(own.oct + live.oct, 0.0f, 2.0f);
        if (live.roll > 0) {
            s.roll = live.roll;
            s.pattern = live.pattern;
        }
        if (live.wob > 0) {
            s.wob = live.wob;
            s.rate = live.rate;
        }
        if (live.grit > own.grit) {
            s.grit = live.grit;
            s.gritChar = live.gritChar;
        }
        return s;
    }
};

struct Note {
    int id = -1;
    int degree = 0, octave = 0, flags = 0, sound = Pluck;
    float vel = 0.8f, tone = 0.5f, morph = 0.3f, fat = 0, glide = 0.5f, pressure = 0.5f;
    int rate = 3; // its own wobble's rate
};

// One mono synth with a note stack: a note that lands while another is held
// slides to it without a new attack (legato, the 303's slide), and lifting
// it slides back.
class Lane {
public:
    static constexpr int kStack = 16;

    void init(float rate, uint32_t seed) {
        m_rate = rate;
        m_rng = Rng(seed);
        m_chorus.init(size_t(0.03f * rate));
        m_post.reset();
        m_sideHp.set(300.0f, 0.7f, rate);
        m_tone.reset(0.5f);
        m_morph.reset(0.3f);
        m_fat.reset(0);
        m_press.reset(0.5f);
        m_mute.reset(1);
    }

    bool holding() const { return m_count > 0; }
    bool active() const { return m_active; }
    const Note &current() const { return m_cur; }

    // A note starts. beat is where in the bar it lands, for the roll.
    void noteOn(const Note &n, const Harmony &h, const Shape &sh, double beat) {
        remove(n.id);
        // A different note while one is held slides to it; the same note
        // again is struck again, so two fingers drumming one note, which
        // always overlap a little, still articulate.
        const bool legato = m_count > 0 && (n.degree != m_cur.degree || n.octave != m_cur.octave);
        if (m_count == kStack)
            remove(m_notes[0].id);
        m_notes[m_count++] = n;
        m_cur = n;
        m_target = pitchOf(n, h) + 12.0 * double(sh.oct);
        m_chordSeq = h.chordSeq;
        if (legato) {
            m_glideTau = glideTime(n) / 3.0f;
            return;
        }
        m_midi = m_target;
        m_glideTau = 0;
        // Held, the lane runs, even while a roll waits for its next step.
        m_active = true;
        if (sh.roll > 0)
            enterRoll(sh, beat);
        else
            trigger(1, n.flags & Accent);
    }

    void noteOff(int id) {
        const bool top = m_count > 0 && m_notes[m_count - 1].id == id;
        remove(id);
        if (!top)
            return;
        if (m_count > 0) {
            m_cur = m_notes[m_count - 1];
            m_glideTau = glideTime(m_cur) / 3.0f;
        } else {
            release();
        }
    }

    void move(const Command &c) {
        for (int k = 0; k < m_count; ++k) {
            Note &n = m_notes[k];
            if (n.id != c.id)
                continue;
            const bool slide = n.degree != c.i[0] || n.octave != c.i[1];
            n.degree = c.i[0];
            n.octave = c.i[1];
            n.pressure = c.f[0];
            n.tone = c.f[1];
            n.morph = c.f[2];
            n.fat = c.f[3];
            if (k == m_count - 1) {
                m_cur = n;
                if (slide) {
                    m_glideTau = glideTime(n) / 3.0f;
                        }
            }
        }
    }

    // The click: the sounding note again, accented.
    void strike() {
        if (m_count > 0)
            trigger(1, true);
    }

    void silence(bool hard) {
        m_count = 0;
        if (hard) {
            m_active = false;
            m_amp = 0;
            m_stage = Idle;
            m_ladder.reset();
            m_os.reset();
        } else {
            release();
        }
    }

    // Renders n samples into l and r (added), off0 samples into the block.
    // wraps, if given, is set where the oscillator starts a cycle, for the
    // page's phase-locked scope.
    void render(float *l, float *r, int n, int off0, AudioContext &ctx, const Shape &sh, float muteTarget,
                uint8_t *wraps) {
        m_mute.setTime(0.012f, m_rate);
        if (!m_active) {
            m_mute.reset(muteTarget);
            m_level *= 0.9f;
            return;
        }
        const Recipe &R = recipe(m_cur.sound);
        const Harmony &h = ctx.harmony();
        const Transport &tr = ctx.transport();
        const float *duck = ctx.duck();
        const float rate = m_rate;

        // The chord moved under a line that follows it: walk to the new root
        // and play it again, as a bass player would.
        if (h.chordSeq != m_chordSeq) {
            m_chordSeq = h.chordSeq;
            if (m_count > 0 && (m_cur.flags & Follow)) {
                m_glideTau = 0.003f;
                    if (sh.roll <= 0)
                    trigger(0.92f, false);
            }
        }
        if (sh.oct != m_oct) {
            // An octave finger is a jump, not a slide.
            m_oct = sh.oct;
            m_glideTau = 0.0015f;
        }
        m_target = pitchOf(m_cur, h) + 12.0 * double(m_oct);

        // The roll starting or stopping under a held note.
        const bool rolling = sh.roll > 0 && m_count > 0;
        if (rolling && !m_rolling)
            enterRoll(sh, tr.beatAt(off0));
        else if (!rolling && m_rolling && m_count > 0 && !m_gate)
            trigger(1, false);
        m_rolling = rolling;

        const float blockSmooth = 1.0f - std::exp(-float(n) / (0.006f * rate));
        const float tone = m_tone.y += (m_cur.tone - m_tone.y) * blockSmooth;
        const float morph = m_morph.y += (m_cur.morph - m_morph.y) * blockSmooth;
        const float fat = m_fat.y += (m_cur.fat - m_fat.y) * blockSmooth;
        const float press = m_press.y += (m_cur.pressure - m_press.y) * blockSmooth;

        const float glideCoef = m_glideTau <= 0 ? 1.0f : 1.0f - std::exp(-1.0f / (m_glideTau * rate));
        const float decayCoef = std::exp(-1.0f / (std::max(0.01f, R.decay) * rate));
        const float releaseCoef = std::exp(-1.0f / (std::max(0.004f, R.release) * rate));
        const float attackInc = 1.0f / (std::max(0.0005f, R.attack) * rate);
        const float fenvCoef = std::exp(-1.0f / (std::max(0.01f, R.envDecay * (1.0f - 0.35f * m_accent)) * rate));
        const float dropCoef = std::exp(-1.0f / (std::max(0.005f, R.dropTime) * rate));
        const float sustain = R.sustain;

        const float wobDepth = sh.wob > 0 ? sh.wob : R.wobble;
        const int wobRate = std::clamp(sh.wob > 0 ? sh.rate : m_cur.rate, 0, kRateCount - 1);
        const float grit = sh.grit;
        const float character =
            grit > 0 ? lerpf(R.character, sh.gritChar, std::min(1.0f, 2.0f * grit)) : R.character;
        const int pattern = std::clamp(sh.pattern, 0, kPatternCount - 1);
        const int steps = patternSteps(pattern);
        const float gateLen = std::clamp(sh.roll, 0.12f, 1.0f);

        // Morph: what it does depends on the sound.
        const float sqMix = std::min(1.0f, morph * R.mSquare);
        const float sawL = R.saw * (1.0f - 0.85f * sqMix);
        const float sqL = R.square + 0.8f * sqMix * (R.saw > 0 ? 1.0f : 0.0f);
        const float fmL = R.fm + grit * sh.gritChar * 0.5f;
        // A held note sits about 14 dB under full scale, the sub's peak near
        // −10 dB: room for the kick it pumps with, and for the bus.
        const float topGain = R.level * (0.5f + 0.5f * m_vel) * 0.42f;
        const float subGain = R.sub * (0.86f + 0.14f * m_vel) * (1.0f + 0.25f * fat) * 0.235f;
        const float width = std::min(1.0f, R.width + 0.4f * fat);
        const int unison = R.unison;
        const float uniNorm = 1.0f / std::sqrt(float(unison));

        float cutNow = m_cutNow;
        float lfo = m_lfo;
        float g = 1, comp = 1, fmIndex = 0, d2 = 1, d3 = 1, envAmt = 0, hiScale = 1;
        float peak = 0;
        auto shapeFn = [&character](float v) {
            // Warm (tanh), tube (lopsided: even harmonics), fold (sine
            // folding: the growl), blended continuously.
            const float t = fastTanh(v);
            const float a = fastTanh(v + 0.35f) - 0.33638f;
            if (character < 0.5f)
                return lerpf(t, a, character * 2.0f);
            const float f = std::sin(v);
            return lerpf(a, f, (character - 0.5f) * 2.0f);
        };

        for (int i = 0; i < n; ++i) {
            const double beat = tr.beat + double(off0 + i) * tr.beatsPerSample;

            // The roll: strike on the pattern's steps, close after the gate.
            if (m_rolling) {
                const double sp = beat * double(steps);
                const long step = long(std::floor(sp));
                if (step != m_step) {
                    m_step = step;
                    const int within = int(((step % steps) + steps) % steps);
                    if (kPatterns[pattern].steps[within] == 'x') {
                        // The step after the beat's middle lifts a little: a
                        // roll leans into the offbeat, which is where it grooves.
                        const bool lean = within * 2 == steps || (steps == 3 && within == 1);
                        trigger(lean ? 1.0f : 0.86f, false);
                        m_gateEnd = (double(step) + gateLen * double(untilNext(pattern, within))) / double(steps);
                    }
                }
                if (m_gate && beat >= m_gateEnd && gateLen < 0.999f)
                    closeGate();
            }

            // Every eight samples: the filter, the vowel, the drive.
            if ((i & 7) == 0) {
                const float hz0 = midiToHz(m_midi);
                if (wobDepth > 0.001f) {
                    const double ph = beat / kRateBeats[wobRate];
                    lfo = 0.5f - 0.5f * std::cos(kTwoPi * float(ph - std::floor(ph)));
                } else {
                    lfo = 0;
                }
                const float keytrack = 0.4f * std::log2(std::max(hz0, 20.0f) / 55.0f) / 8.3f;
                float cut = lerpf(R.cutLo, R.cutHi, tone) + keytrack + 0.1f * (press - 0.5f);
                if (wobDepth > 0.001f)
                    cut = lerpf(cut, lerpf(R.cutLo * 0.6f, cut + 0.22f, lfo), wobDepth * R.wobCut);
                envAmt = R.envAmt * (1.0f + 0.8f * m_accent * R.accent);
                cut += envAmt * m_fenv * 0.6f;
                cutNow = clampf(cut, 0.0f, 1.0f);
                const float fc = std::min(50.0f * std::exp2(cutNow * 8.3f), rate * (0.42f - 0.1f * R.res));
                m_ladder.set(fc, std::min(0.97f, R.res + 0.12f * m_accent * R.accent), rate);
                if (R.formant > 0) {
                    const float v = clampf(R.vowel + tone * R.vowelTone + morph * R.mVowel + m_fenv * R.vowelEnv +
                                               wobDepth * lfo * R.wobVowel * 3.0f,
                                           0.0f, 3.999f);
                    setVowel(v);
                }
                // FM and folding run at the base rate, so they too thin out
                // as the note climbs.
                hiScale = clampf(320.0f / hz0, 0.2f, 1.0f);
                fmIndex = (R.fmIndex + morph * R.mFm + wobDepth * lfo * R.wobFm + grit * sh.gritChar * 3.0f) * hiScale;
                const float drv = R.drive + tone * R.toneDrive + morph * R.mDrive + grit * 1.1f +
                                  m_accent * 0.15f * R.accent + std::max(0.0f, press - 0.55f) * 0.5f;
                // High up the drive eases off: there its harmonics would
                // reach past what even twice the rate can hold.
                g = 1.0f + 14.0f * drv * drv * hiScale;
                comp = 1.0f / std::sqrt(1.0f + 0.5f * (g - 1.0f));
                // The crossover follows the note: everything at the note's
                // own pitch is the sub's, everything from its second harmonic
                // up is the top's.
                const float xo = clampf(hz0 * 1.45f, 80.0f, 320.0f);
                m_hp1.set(xo, 0.7071f, rate);
                m_hp2.set(xo, 0.7071f, rate);
                m_post.set(9000.0f, 0.6f, rate);
                if (unison > 1) {
                    m_drift += float(8 * 0.13 / double(rate));
                    if (m_drift >= 1.0f)
                        m_drift -= 1.0f;
                    const float cents = (R.detune + morph * R.mDetune + fat * 9.0f) *
                                        (1.0f + 0.22f * std::sin(kTwoPi * m_drift));
                    d2 = std::exp2(cents / 1200.0f);
                    d3 = std::exp2(-cents * 1.07f / 1200.0f);
                }
            }

            // Envelopes.
            switch (m_stage) {
            case Attack:
                m_amp += attackInc;
                if (m_amp >= 1.0f) {
                    m_amp = 1.0f;
                    m_stage = Decay;
                }
                break;
            case Decay:
                m_amp = sustain + (m_amp - sustain) * decayCoef;
                break;
            case Release:
                m_amp *= releaseCoef;
                if (m_amp < 1e-4f) {
                    m_amp = 0;
                    m_stage = Idle;
                }
                break;
            case Idle:
                break;
            }
            m_fenv *= fenvCoef;
            m_drop *= dropCoef;
            m_midi += (m_target - m_midi) * double(glideCoef);

            // Oscillators.
            const float hz = midiToHz(m_midi + double(R.drop * m_drop));
            const float inc = std::min(hz / rate, 0.45f);
            m_phase += inc;
            if (m_phase >= 1.0f) {
                m_phase -= 1.0f;
                if (wraps)
                    wraps[i] = 1;
            }
            const float ph = m_phase;
            const float s1 = std::sin(kTwoPi * ph);
            float osc = R.sine * s1;
            if (sawL > 0) {
                // Falling saws, so their fundamental is in phase with the sine.
                float s = 1.0f - 2.0f * ph + polyBlep(ph, inc);
                if (unison > 1) {
                    const float i2 = std::min(inc * d2, 0.45f);
                    m_saw2 += i2;
                    if (m_saw2 >= 1.0f)
                        m_saw2 -= 1.0f;
                    s += 1.0f - 2.0f * m_saw2 + polyBlep(m_saw2, i2);
                }
                if (unison > 2) {
                    const float i3 = std::min(inc * d3, 0.45f);
                    m_saw3 += i3;
                    if (m_saw3 >= 1.0f)
                        m_saw3 -= 1.0f;
                    s += 1.0f - 2.0f * m_saw3 + polyBlep(m_saw3, i3);
                }
                osc += sawL * s * uniNorm;
            }
            if (sqL > 0) {
                float q = ph < 0.5f ? 1.0f : -1.0f;
                q += polyBlep(ph, inc);
                float t2 = ph + 0.5f;
                if (t2 >= 1.0f)
                    t2 -= 1.0f;
                q -= polyBlep(t2, inc);
                osc += sqL * 0.8f * q;
            }
            if (R.octSaw > 0) {
                float p2 = ph * 2.0f;
                if (p2 >= 1.0f)
                    p2 -= 1.0f;
                const float i2 = std::min(inc * 2.0f, 0.45f);
                osc += R.octSaw * (1.0f - 2.0f * p2 + polyBlep(p2, i2));
            }
            if (fmL > 0) {
                m_fmPhase += inc * R.fmRatio;
                if (m_fmPhase >= 1.0f)
                    m_fmPhase -= 1.0f;
                float c = std::sin(kTwoPi * ph + fmIndex * std::sin(kTwoPi * m_fmPhase));
                if (R.foldPre > 0)
                    c = std::sin(c * (1.0f + 3.0f * R.foldPre * hiScale) * 1.5707963f);
                osc += fmL * c;
            }

            // Filters (the ladder, and the vowel bank beside it) and drive,
            // the drive at twice the rate so its harmonics don't fold back.
            float y;
            if (R.driveFirst) {
                y = m_ladder.step(m_os.process(osc * 0.7f * g, shapeFn) * comp);
            } else {
                y = m_ladder.step(osc * 0.7f);
                if (R.formant > 0) {
                    const float x = osc * 0.7f;
                    const float v = m_vf[0].step(x).band * m_vk[0] + 0.7f * m_vf[1].step(x).band * m_vk[1] +
                                    0.35f * m_vf[2].step(x).band * m_vk[2];
                    y = lerpf(y, 1.6f * v, R.formant);
                }
                y = m_os.process(y * g, shapeFn) * comp;
            }
            y = m_post.step(y).low;
            // The crossover: two 2-pole high-passes, a 24 dB slope.
            y = m_hp2.step(m_hp1.step(y).high).high;

            const float amp = m_amp;
            const float mute = m_mute.step(muteTarget);
            const float top = y * amp * topGain * (1.0f + 0.5f * m_accent * R.accent);
            // The sub ducks under the kick harder than the top does.
            const float sub = s1 * amp * subGain * (1.0f - 0.45f * duck[std::min(off0 + i, kBlock - 1)]);
            float mono = (top + sub) * mute;
            float side = 0;
            if (width > 0.01f) {
                m_chorus.push(m_sideHp.step(top * mute).high);
                m_chorusPhase += 0.37f / rate;
                if (m_chorusPhase >= 1.0f)
                    m_chorusPhase -= 1.0f;
                const float lfoC = std::sin(kTwoPi * m_chorusPhase);
                const float a = m_chorus.read((0.0075f + 0.0016f * lfoC) * rate);
                const float b = m_chorus.read((0.0113f - 0.0021f * lfoC) * rate);
                side = (a - b) * 0.55f * width;
            }
            // A soft knee near the top, the same for both sides, so an
            // accented, fat, resonant attack rounds off instead of hitting
            // the bus limiter, and the low end stays one signal.
            const float mag = std::fabs(mono) + std::fabs(side);
            if (mag > 0.45f) {
                const float k = (0.45f + 0.25f * fastTanh((mag - 0.45f) / 0.25f)) / mag;
                mono *= k;
                side *= k;
            }
            l[i] += mono + side;
            r[i] += mono - side;
            peak = std::max(peak, std::fabs(mono) + std::fabs(side));
        }
        m_cutNow = cutNow;
        m_lfo = wobDepth > 0.001f ? lfo : -1.0f;
        m_lfoDepth = wobDepth;
        m_level = std::max(peak, m_level * 0.97f);
        if (m_stage == Idle && m_count == 0)
            m_active = false;
    }

    // For the page.
    float level() const { return m_level; }
    float midi() const { return float(m_midi); }
    float cut() const { return m_cutNow; }
    float amp() const { return m_amp; }
    float lfo() const { return m_lfo; }
    float lfoDepth() const { return m_lfoDepth; }
    bool gate() const { return m_gate; }
    long step() const { return m_step; }
    float mute() const { return m_mute.y; }

private:
    enum Stage { Idle, Attack, Decay, Release };

    double pitchOf(const Note &n, const Harmony &h) const {
        const int d = n.degree + ((n.flags & Follow) ? chordShift(h) : 0);
        return h.midi(d, n.octave) + rootShift(h.tuning);
    }
    float glideTime(const Note &n) const {
        // The glide setting scales the sound's own slide from a quarter to
        // four times.
        return recipe(n.sound).glide * std::exp2((n.glide - 0.5f) * 4.0f);
    }
    void remove(int id) {
        for (int k = 0; k < m_count; ++k)
            if (m_notes[k].id == id) {
                for (int j = k; j + 1 < m_count; ++j)
                    m_notes[j] = m_notes[j + 1];
                --m_count;
                return;
            }
    }

    void trigger(float velScale, bool accent) {
        if (m_amp < 0.02f) {
            // From silence, start every cycle at zero: each note's first
            // swing of the sub is the same, so it punches the same.
            m_phase = m_fmPhase = 0;
            m_saw2 = m_rng.unit();
            m_saw3 = m_rng.unit();
        }
        m_vel = clampf(m_cur.vel * velScale, 0.05f, 1.0f);
        m_accent = accent || m_cur.vel > 0.9f ? 1.0f : 0.0f;
        m_stage = Attack;
        m_fenv = 1;
        m_drop = 1;
        m_gate = true;
        m_active = true;
    }
    void closeGate() {
        m_gate = false;
        if (m_stage != Idle)
            m_stage = Release;
    }
    void release() {
        closeGate();
        m_rolling = false;
    }
    // A note lands, or the roll begins, while rolling: if this step of the
    // pattern is lit and still young, it sounds now; otherwise it waits for
    // the next lit step, so a roll is always on the grid.
    void enterRoll(const Shape &sh, double beat) {
        const int pattern = std::clamp(sh.pattern, 0, kPatternCount - 1);
        const int steps = patternSteps(pattern);
        const double sp = beat * double(steps);
        const long step = long(std::floor(sp));
        m_step = step;
        m_rolling = true;
        const int within = int(((step % steps) + steps) % steps);
        if (kPatterns[pattern].steps[within] == 'x' && sp - double(step) < 0.5) {
            trigger(1, m_cur.flags & Accent);
            m_gateEnd = (double(step) + std::clamp(sh.roll, 0.12f, 1.0f) * double(untilNext(pattern, within))) /
                        double(steps);
        } else if (m_gate) {
            closeGate();
        }
    }
    // Steps from a lit step to the next: the gate is a share of that, so an
    // offbeat note can last an eighth and a sixteenth roll stays tight.
    static int untilNext(int pattern, int within) {
        const int steps = patternSteps(pattern);
        for (int k = 1; k <= steps; ++k)
            if (kPatterns[pattern].steps[(within + k) % steps] == 'x')
                return k;
        return steps;
    }
    void setVowel(float v) {
        // Formants of u, o, a, e, i (a man's voice, a little low).
        static const float F[5][3] = {
            {325, 700, 2530}, {450, 800, 2830}, {800, 1150, 2900}, {400, 1900, 2550}, {290, 2250, 3000}};
        static const float Q[3] = {5.0f, 7.0f, 9.0f};
        const int a = int(v);
        const float t = v - float(a);
        for (int k = 0; k < 3; ++k) {
            const float hz = F[a][k] * std::pow(F[a + 1][k] / F[a][k], t);
            m_vf[k].set(hz, Q[k], m_rate);
            m_vk[k] = 1.0f / Q[k] * 2.0f;
        }
    }

    float m_rate = 48000;
    Rng m_rng;
    Note m_notes[kStack];
    int m_count = 0;
    Note m_cur;
    bool m_active = false, m_gate = false, m_rolling = false;
    Stage m_stage = Idle;
    float m_amp = 0, m_fenv = 0, m_drop = 0, m_vel = 0.8f, m_accent = 0;
    double m_midi = 33, m_target = 33;
    float m_glideTau = 0;
    float m_oct = 0;
    uint32_t m_chordSeq = 0;
    long m_step = 0;
    double m_gateEnd = 0;
    float m_phase = 0, m_saw2 = 0, m_saw3 = 0, m_fmPhase = 0, m_drift = 0, m_chorusPhase = 0;
    Smooth m_tone, m_morph, m_fat, m_press, m_mute;
    Ladder m_ladder;
    Svf m_vf[3];
    float m_vk[3] = {1, 1, 1};
    Halfband2x m_os;
    Svf m_post, m_hp1, m_hp2, m_sideHp;
    Delay m_chorus;
    float m_cutNow = 0, m_lfo = -1, m_lfoDepth = 0, m_level = 0;
};

class Engine : public PartEngine {
public:
    void init(float rate) override {
        m_rate = rate;
        m_live.init(rate, 0xB455u);
        m_clip.init(rate, 0x5EB0u);
    }

    void command(const Command &c, AudioContext &ctx) override {
        // Clip events land inside the coming block: keep them for their
        // sample, so a looped line is as tight as the grid.
        const int off = c.t > 0 ? ctx.offsetOf(c.t, kBlock) : 0;
        if (off > 0 && m_pendingCount < kPending) {
            m_pending[m_pendingCount++] = {c, off};
            return;
        }
        apply(c, ctx, 0);
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        // Pending commands in time order (a handful at most).
        for (int a = 1; a < m_pendingCount; ++a)
            for (int b = a; b > 0 && m_pending[b].off < m_pending[b - 1].off; --b)
                std::swap(m_pending[b], m_pending[b - 1]);
        int done = 0, k = 0;
        while (done < n) {
            while (k < m_pendingCount && m_pending[k].off <= done)
                apply(m_pending[k++].c, ctx, done);
            const int next = k < m_pendingCount ? std::min(n, m_pending[k].off) : n;
            segment(l + done, r + done, next - done, done, ctx);
            done = next;
        }
        while (k < m_pendingCount)
            apply(m_pending[k++].c, ctx, n);
        m_pendingCount = 0;
        m_harmony = ctx.harmony();
        m_beat = ctx.transport().beatAt(n);
    }

    void silence(bool hard) override {
        m_live.silence(hard);
        m_clip.silence(hard);
        m_pendingCount = 0;
        if (hard) {
            m_shapeLive = Shape{};
            for (Shape &sh : m_shapeSlot)
                sh = Shape{};
        }
    }

    void publish(float *data) override {
        const Lane *lanes[2] = {&m_live, &m_clip};
        for (int k = 0; k < 2; ++k) {
            const Lane &ln = *lanes[k];
            float *d = data + PubLane + 8 * k;
            d[PubOn] = ln.active() ? (ln.holding() ? 1.0f : 0.5f) : 0.0f;
            d[PubMidi] = ln.midi();
            d[PubLevel] = ln.level();
            d[PubDegree] = float(ln.current().degree + ln.current().octave * m_harmony.tuning.degreesPerOctave());
            d[PubSound] = float(ln.current().sound);
            d[PubCut] = ln.cut();
            d[PubEnv] = ln.amp();
            d[PubGate] = ln.gate() ? 1.0f : 0.0f;
        }
        const Lane &shown = m_live.active() || !m_clip.active() ? m_live : m_clip;
        const Shape sh = &shown == &m_live ? m_shapeLive : Shape::over(clipShape(), m_shapeLive);
        data[PubLfo] = shown.lfo();
        data[PubWobDepth] = shown.lfoDepth();
        data[PubWobRate] = float(sh.wob > 0 ? sh.rate : shown.current().rate);
        data[PubRoll] = sh.roll > 0 ? float(sh.pattern + 1) : 0.0f;
        {
            const int steps = patternSteps(std::clamp(sh.pattern, 0, kPatternCount - 1));
            const double sp = m_beat * double(steps);
            data[PubRollStep] = float(((long(std::floor(sp)) % steps) + steps) % steps);
        }
        data[PubGrit] = sh.grit;
        data[PubGritChar] = sh.gritChar;
        data[PubOct] = sh.oct;
        data[PubChordShift] = float(chordShift(m_harmony));
        data[PubClipMute] = 1.0f - m_clip.mute();
        scope(data + PubScope, data[PubScopeCycles]);
    }

    int voices() const override { return int(m_live.active()) + int(m_clip.active()); }

private:
    static constexpr int kPending = 64, kScope = 4096;
    struct Pending {
        Command c;
        int off;
    };

    void apply(const Command &c, AudioContext &ctx, int off) {
        const bool replay = (c.id >> 24) != 0;
        Lane &lane = replay ? m_clip : m_live;
        // A loop's shapers belong to its slot, so one loop's wobble never
        // leaks into the next.
        Shape &own = replay ? m_shapeSlot[slotOf(c.id)] : m_shapeLive;
        const Shape eff = replay ? Shape::over(own, m_shapeLive) : m_shapeLive;
        switch (c.type) {
        case Command::On: {
            Note n;
            n.id = c.id;
            n.degree = c.i[0];
            n.octave = std::clamp(c.i[1], -1, 3);
            n.flags = c.i[2];
            n.sound = std::clamp(c.i[3], 0, SoundCount - 1);
            n.vel = clampf(c.f[0], 0.05f, 1.0f);
            n.tone = clampf(c.f[1], 0, 1);
            n.morph = clampf(c.f[2], 0, 1);
            n.fat = clampf(c.f[3], 0, 1);
            n.glide = clampf(c.f[4], 0, 1);
            n.pressure = n.vel;
            n.rate = std::clamp(int(c.f[5]), 0, kRateCount - 1);
            lane.noteOn(n, ctx.harmony(), eff, ctx.transport().beatAt(off));
            break;
        }
        case Command::Off:
            lane.noteOff(c.id);
            break;
        case Command::Move:
            lane.move(c);
            break;
        case Command::Param:
            switch (c.i[0]) {
            case ShapeOct:
                own.oct = clampf(std::round(c.f[0]), 0, 2);
                break;
            case ShapeRoll:
                own.roll = clampf(c.f[0], 0, 1);
                own.pattern = std::clamp(int(c.f[1]), 0, kPatternCount - 1);
                break;
            case ShapeWob:
                own.wob = clampf(c.f[0], 0, 1);
                own.rate = std::clamp(int(c.f[1]), 0, kRateCount - 1);
                break;
            case ShapeGrit:
                own.grit = clampf(c.f[0], 0, 1);
                own.gritChar = clampf(c.f[1], 0, 1);
                break;
            case ResetSlot:
                // The page is about to fill a slot: what an old loop there
                // left set goes with it.
                m_shapeSlot[std::clamp(int(c.f[0]), 0, kClipSlots - 1)] = Shape{};
                break;
            default:
                break;
            }
            break;
        case kStrike:
            if (lane.holding())
                lane.strike();
            break;
        default:
            break;
        }
    }

    static int slotOf(int id) { return std::clamp((id >> 24) & 0x3F, 0, kClipSlots - 1); }
    const Shape &clipShape() const { return m_shapeSlot[slotOf(m_clip.current().id)]; }

    void segment(float *l, float *r, int n, int off0, AudioContext &ctx) {
        uint8_t wraps[kBlock] = {};
        // The shown lane marks its cycles for the scope.
        const bool liveShown = m_live.active() || !m_clip.active();
        float mixL[kBlock] = {}, mixR[kBlock] = {};
        m_live.render(mixL, mixR, n, off0, ctx, m_shapeLive, 1.0f, liveShown ? wraps : nullptr);
        // The loop steps aside while the player holds a note.
        m_clip.render(mixL, mixR, n, off0, ctx, Shape::over(clipShape(), m_shapeLive), m_live.holding() ? 0.0f : 1.0f,
                      liveShown ? nullptr : wraps);
        for (int i = 0; i < n; ++i) {
            l[i] += mixL[i];
            r[i] += mixR[i];
            m_scopeBuf[m_scopeW & (kScope - 1)] = 0.5f * (mixL[i] + mixR[i]);
            if (wraps[i])
                m_wraps[m_wrapW++ & 7] = m_scopeW;
            ++m_scopeW;
        }
    }

    // Two cycles of what is sounding, from one of the oscillator's own
    // zero phases to the next but one, resampled to the page's points.
    void scope(float *out, float &cycles) {
        cycles = 0;
        if (m_wrapW < 3) {
            std::fill(out, out + PubScopeSize, 0.0f);
            return;
        }
        const uint32_t a = m_wraps[(m_wrapW - 3) & 7], b = m_wraps[(m_wrapW - 1) & 7];
        if (b <= a || b - a >= kScope || m_scopeW - a >= kScope) {
            std::fill(out, out + PubScopeSize, 0.0f);
            return;
        }
        cycles = 2;
        const float span = float(b - a);
        for (int k = 0; k < PubScopeSize; ++k) {
            const float pos = float(a) + span * float(k) / float(PubScopeSize - 1);
            const uint32_t i0 = uint32_t(pos);
            const float t = pos - float(i0);
            out[k] = lerpf(m_scopeBuf[i0 & (kScope - 1)], m_scopeBuf[(i0 + 1) & (kScope - 1)], t);
        }
    }

    float m_rate = 48000;
    Lane m_live, m_clip;
    Shape m_shapeLive, m_shapeSlot[kClipSlots];
    Pending m_pending[kPending];
    int m_pendingCount = 0;
    Harmony m_harmony;
    double m_beat = 0;
    float m_scopeBuf[kScope] = {};
    uint32_t m_scopeW = 0;
    uint32_t m_wraps[8] = {};
    uint32_t m_wrapW = 0;
};

}
