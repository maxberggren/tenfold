#pragma once

// A cuíca, as physics. Not samples and not a recipe: the instrument's own
// parts, in SI units, integrated sample by sample. The measurements it is
// built from and checked against are in docs/cuica.md.
//
//   the head    a goatskin 0.3 mm thick stretched over a 9-inch steel shell
//               at 4500 N/m: a membrane whose modes are Bessel functions,
//               J_m(j_mn·r/a)·cos mθ, each a damped oscillator with its own
//               modal mass from the skin. Its tension follows its stretch
//               (Kirchhoff–Carrier: ΔT = Y/(2A)·∫|∇w|²), which at this
//               tension moves the note by hundredths of a semitone: the
//               skin is taut, the pitch is not in its stretch.
//   the stick   a thin bamboo rod (a haste) tied with a knot through the
//               head, a little off its middle, standing inside the shell.
//               Longitudinally it is a free–free rod: it moves as a whole with
//               the head's middle, its first compression mode squeaking at
//               9 kHz. The knot is a stiff, lossy spring between the stick's
//               end and a patch of skin round the middle, solved against both
//               sides' give. The stick's five grams hang on the head's middle,
//               which is why a cuíca speaks so much lower than its skin alone.
//   the cloth   a damp cloth pinched round the stick and drawn along it.
//               Friction between them is stick–slip: the cloth drags the
//               stick (and the head's middle) until the skin pulls back harder
//               than static friction holds, slips on the falling kinetic
//               friction, and grips again, locked to the head as a bow is to
//               a string; the faster the cloth, the nearer its note comes up
//               to the head's own. It is solved against the stick's and the cloth's own
//               response every sample (the friction curve against the
//               contact's admittance, with hysteresis), so how fast and how
//               hard the hand draws decides whether it speaks, creaks or is
//               silent. The fingertips hold the cloth through a soft shear
//               spring that stiffens as they pinch; the wet fibres catch and
//               let go, wavering the load, and the cloth hisses as it slides.
//               The player's pinch is set before the hand moves and held
//               through the turns; a fast draw squeezes harder still.
//   the thumb   the other hand's thumb pressing the head from outside, near
//               or on the knot: a fingertip's pulp, stiffening exponentially
//               with its load, with Hunt–Crossley loss, pushed by the hand and
//               solved implicitly against the skin. It holds down a patch of
//               skin (a point would not: a point on a membrane constrains
//               nothing), and its stiffness joins the spring the stick's mass
//               rides on: nearer the knot and harder is higher, up about an
//               octave, and further out it hardly moves the note.
//   the shell   a cylinder open at the far end: its air is a waveguide, a
//               plane wave either way between the head and the open end,
//               reflected there inverted with the radiation's loss (an
//               unflanged pipe's, with its end correction) and with the
//               player's forearm in the bore taking half of it. The head
//               pushes the air in it and the air pushes back, and the front of
//               the head is loaded by the radiation mass of the open air.
//   the sound   what reaches two microphones in front: the volume
//               acceleration of the head's front face and, delayed round the
//               shell to each ear differently, of the air leaving the open
//               end, plus the cloth's hiss.
//
// One hand and one thumb cost a few tenths of a percent of a core; each
// more finger adds a contact, a couple of tenths more.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

#include "dsp.h"

namespace tf::cuica {

// Bessel's J_m(x), by its power series (in double, good past x = 25).
inline double besselJ(int m, double x) {
    double term = 1.0;
    for (int k = 1; k <= m; ++k)
        term *= x / (2.0 * k);
    double sum = term;
    const double q = -0.25 * x * x;
    for (int k = 1; k < 120; ++k) {
        term *= q / (double(k) * double(k + m));
        sum += term;
        if (double(k) > x && std::fabs(term) < 1e-16 * (std::fabs(sum) + 1e-30))
            break;
    }
    return sum;
}

// --- the numbers ------------------------------------------------------------------------------------------
//
// SI units. Sources in docs/cuica.md.

struct Physics {
    static constexpr float kRhoAir = 1.2f;   // kg/m³
    static constexpr float kC = 343.0f;      // m/s
    // The head: a 9-inch cuíca's goatskin.
    static constexpr float kRadius = 0.1143f; // m, a 9" head
    static constexpr float kSkin = 0.38f;     // kg/m², goatskin 0.3 mm at 1260 kg/m³
    static constexpr float kTension = 4500.0f; // N/m, as Statsenko's fit (4470)
    static constexpr float kStretch = 1.65e5f; // N/m, E·h/(1−ν): damp goatskin, 330 MPa × 0.3 mm / 0.6
    // The shell: stainless steel, open at the far end.
    static constexpr float kShellLength = 0.30f; // m
    // The stick: bamboo.
    static constexpr float kStickLength = 0.275f; // m, from the knot to its free end (the shell less an inch)
    static constexpr float kStickRadius = 0.0028f; // m
    static constexpr float kBamboo = 750.0f;      // kg/m³
    static constexpr float kBambooE = 1.8e10f;    // Pa, along the grain
    static constexpr float kKnot = 0.006f;        // m, the radius of skin the knot holds
    static constexpr float kKnotOffset = 0.0014f; // m, how far off the middle it is tied
    static constexpr float kKnotStiffness = 1.5e6f; // N/m, a cord knotted through soaked skin, dried hard
    // The cloth hand.
    static constexpr float kCloth = 0.0005f;   // kg, the wet cloth and the skin of the fingers on it
    // Fingertips through wet cloth in shear: stiffer the harder they pinch
    // (a finger pad's tangential stiffness grows with its normal load).
    static constexpr float kShear = 1000.0f;   // N/m, barely pinching
    static constexpr float kShearPerN = 400.0f; // N/m more per newton of grip
    static constexpr float kShearLoss = 3.0f;  // N·s/m
    // The thumb.
    static constexpr float kThumbPatch = 0.008f; // m, the radius of skin it holds
    static constexpr float kThumbPulpA = 0.3f;     // N
    static constexpr float kThumbPulpD = 0.00035f; // m: the pulp's stiffness is (F + A)/δ₀
    static inline float kThumbLoss = 0.05f;   // s/m, Hunt–Crossley
};

// The model's constants that measured sounds set (docs/cuica.md says which
// measurement each one answers to).
struct Knobs {
    static inline float muStatic = 0.9f, muKinetic = 0.45f, muSpeed = 0.5f; // m/s, where friction falls
    static inline float rough = 0.25f;    // how much the wet fibres' catching wavers the cloth's load
    static inline float rasp = 0.002f;    // the cloth's own hiss on the bamboo
    static inline float skinQ = 35.0f;    // the skin's own loss
    static inline float stickQ = 60.0f;   // bamboo's
    static inline float knotLoss = 3.0f;  // N·s/m
    static inline float back = 0.9f;      // how much of the open end reaches the front
    static inline float armInBore = 0.5f; // the open end's reflection with a forearm in it
    static inline float cloth = 0.0005f, shear = 200.0f, shearPerN = 100.0f, shearLoss = 15.0f;
    static inline float creep = 0.05f; // s, the fingertip pulp's shear relaxing under a held load
    static inline float aged = 0.1f, wear = 0.004f, heal = 0.15f; // the contact's ageing: extra static friction, m of sliding to wear it, s to heal
    static inline float regrip = 0.03f, still = 0.02f; // s to settle while the arm is all but still (under m/s), at a turn
    static inline float growl = 0.7f, growlFrom = 0.8f, growlSpeed = 0.4f; // a firm pinch's own load (of the most), from where, and the draw speed it is gone by (m/s)
    static inline float easeIn = 0.4f, easeTime = 0.08f; // a stroke's opening squeeze (more, of the steady one) and how it eases off (s)
    static inline float squeezeHold = 0.003f;           // s, how long the squeeze takes to let up after the speed drops
    static inline float level = 0.183f;
    static inline float maxGrip = 20.0f;  // N, the cloth squeezed hard (DAFx: 0–20 N)
    static inline float thumbNear = 0.1f; // the nearest the thumb's middle comes to the knot (of the radius)
    static inline float gripFollow = 1.0f; // how much of the way to the pinch it wants a sample goes
    static inline float armHz = 40.0f;  // how quickly the arm follows the player's motion
    static inline float strokeFrom = 0.33f, strokeUp = 0.25f; // a stroke's speed out of the turn, and how long it speeds up
    static inline float gripBase = 0.03f, gripLean = 0.5f, gripPerSpeed = 0.9f; // the hand at rest, the pinch's share, and the squeeze per m/s
    static inline float maxThumb = 20.0f; // N, the thumb pressed hard
};

// A stroke as the drums' pads play it: where the hand starts along the
// stick, how far it draws (positive: toward the open end, pulling), over how
// long, how hard it grips, and where the thumb presses (r from the knot,
// 0..1 of the head's radius; force 0..1, 0 = no thumb) at the start and end.
struct Stroke {
    float from = 0.4f, length = 0.06f, seconds = 0.1f, grip = 0.6f;
    float thumbR = 0.3f, thumb0 = 0, thumb1 = 0;
    float thumbR1 = -1; // where the thumb slides to over the stroke (−1: it stays)
};

class Body {
public:
    static constexpr int kModes = 21;  // the head's
    static constexpr int kStick = 2;   // the stick's: its rigid motion and its first compression mode (10 kHz; the next is past what a 48 kHz step can hold)
    static constexpr int kHands = 3; // a cuíca has one cloth: the player's, a loop's, a stroke's
    static constexpr int kThumbs = 3;
    static constexpr int kLine = 256;  // the shell's delay lines

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        build();
        silence();
    }

    // The head's tension (N/m), as the player has tightened it.
    void setTension(float T) {
        T = std::clamp(T, 500.0f, 3.0e4f);
        if (std::fabs(T - m_tension) < 1e-4f * T)
            return;
        m_tension = T;
        retune();
    }

    // A hand takes the stick in its cloth (id: whose), `along` the stick
    // 0..1 (0 at the knot), gripping 0..1. delay: samples into the next block.
    void grip(int id, float along, float grip, int delay) {
        Hand *h = handFor(id);
        if (!h)
            h = freeHand();
        *h = Hand{};
        h->on = true;
        h->id = id;
        h->delay = std::max(0, delay);
        const float s = place(along);
        h->target = h->from = h->to = h->bone = s;
        h->grip = clampf(grip, 0, 1);
        h->gripNow = 0;
        wake();
    }
    // It moves (a new place along the stick, a new grip).
    void slide(int id, float along, float grip) {
        Hand *h = handFor(id);
        if (!h)
            return;
        const float s = place(along);
        // Ramp from where the target is now to the new one over the time
        // the last move took to come (the page sends at its frame rate): a
        // piecewise-linear path for the arm, so its speed has no steps.
        h->from = h->target;
        h->to = s;
        h->ramp = std::clamp(h->sinceMove, 0.002f * m_rate, 0.05f * m_rate);
        h->rampAt = 0;
        h->sinceMove = 0;
        h->grip = clampf(grip, 0, 1);
    }
    void letGo(int id) {
        if (Hand *h = handFor(id))
            h->leaving = true;
    }

    // A thumb on the head from outside: r from the knot (0..1 of the head's
    // radius), force 0..1.
    void press(int id, float r, float force, int delay) {
        Thumb *t = thumbFor(id);
        if (!t)
            t = freeThumb();
        *t = Thumb{};
        t->on = true;
        t->id = id;
        t->delay = std::max(0, delay);
        t->rTarget = t->r = clampf(r, 0, 1);
        t->force = clampf(force, 0, 1);
        shapeThumb(*t);
        wake();
    }
    void movePress(int id, float r, float force) {
        if (Thumb *t = thumbFor(id)) {
            t->rTarget = clampf(r, 0, 1);
            t->force = clampf(force, 0, 1);
        }
    }
    void lift(int id) {
        if (Thumb *t = thumbFor(id))
            t->leaving = true;
    }

    // A whole stroke, as a pad plays it: a hand and (if any) a thumb that
    // act it out by themselves and leave.
    void stroke(const Stroke &s, int delay) {
        const int id = kScripted + (m_scriptSerial++ & 0xFFFF);
        // The stroke before it is the same hand: it turns and draws back
        // from where it is, still pinching, rather than a second hand
        // taking the stick.
        Hand *h = nullptr;
        for (auto &o : m_hands)
            if (o.on && o.script && !o.leaving)
                h = &o;
        const bool turn = h != nullptr;
        if (turn) {
            Hand keep = *h;
            *h = Hand{};
            h->bone = keep.bone;
            h->boneV = keep.boneV;
            h->cloth = keep.cloth;
            h->clothV = keep.clothV;
            h->stuck = keep.stuck;
            h->pull = keep.pull;
            h->rest = keep.rest;
            h->settled = keep.settled;
            h->drive = keep.drive;
            h->pace = keep.pace;
            h->fresh = keep.fresh;
            h->heading = keep.heading;
            h->armed = keep.armed;
            h->rising = keep.rising;
            h->gripNow = keep.gripNow;
        } else {
            h = freeHand();
            *h = Hand{};
        }
        h->on = true;
        h->id = id;
        h->delay = std::max(0, delay);
        // (a pressed stroke: the thumb is down first, and the cloth moves
        // once it is)
        if (s.thumb0 > 0 || s.thumb1 > 0)
            h->delay += int(kThumbLead * m_rate);
        h->script = true;
        h->scriptFrom = turn ? h->bone : place(s.from);
        // (back the other way if the stroke would run off the stick)
        const float lo = place(0), hi = place(1);
        float len = s.length;
        if (turn && (h->scriptFrom + len > hi || h->scriptFrom + len < lo))
            len = -len;
        h->scriptLength = len;
        h->scriptSeconds = std::max(0.02f, s.seconds);
        h->target = h->from = h->to = h->scriptFrom;
        if (!turn)
            h->bone = h->scriptFrom;
        h->grip = clampf(s.grip, 0, 1);
        // (the hand already has the stick pinched: it held it after the
        // last stroke)
        if (!turn)
            h->gripNow = Knobs::maxGrip * Knobs::gripBase;
        if (s.thumb0 > 0 || s.thumb1 > 0) {
            Thumb *t = freeThumb();
            *t = Thumb{};
            t->on = true;
            t->id = id;
            t->delay = std::max(0, delay);
            t->quick = true;
            t->rTarget = t->r = clampf(s.thumbR, 0, 1);
            t->force = clampf(s.thumb0, 0, 1);
            t->script = true;
            t->scriptFrom = s.thumb0;
            t->scriptTo = s.thumb1;
            t->scriptR0 = t->r;
            t->scriptR1 = s.thumbR1 >= 0 ? clampf(s.thumbR1, 0, 1) : t->r;
            // (the thumb a beat ahead of the cloth, as the player's hands
            // go: it is where it means to be as the stroke ends)
            t->scriptSeconds = h->scriptSeconds + kThumbLead;
            shapeThumb(*t);
        }
        wake();
    }

    // Adds the instrument's sound into l and r (n ≤ the block).
    void render(float *l, float *r, int n);

    // A hand flat on the head: everything dies within about `seconds`.
    void mute(float seconds) {
        if (m_sleeping)
            return;
        m_muteD = std::exp(-2.0f / (std::max(seconds, 0.001f) * 0.25f * m_rate));
        for (auto &h : m_hands)
            h.leaving = true;
        for (auto &t : m_thumbs)
            t.leaving = true;
    }

    void silence() {
        for (int k = 0; k < kModes; ++k)
            m_x[k] = m_u[k] = 0;
        for (int k = 0; k < kStick; ++k)
            m_sx[k] = m_su[k] = 0;
        for (auto &h : m_hands)
            h.on = false;
        for (auto &t : m_thumbs)
            t.on = false;
        for (int i = 0; i < kLine; ++i)
            m_out[i] = m_in[i] = 0;
        m_reflect = m_front = m_frontIn = m_uIn = m_uFront = 0;
        m_lastL = m_lastR = 0;
        m_backL.y = m_backR.y = 0;
        m_noise.reset();
        m_raspLo.reset();
        m_raspF.reset();
        for (auto &e : m_endLine)
            e = 0;
        m_muteD = 1.0f;
        m_sleeping = true;
        m_energy = 0;
        m_quiet = 0;
        m_tensionNow = m_tension;
    }

    bool active() const { return !m_sleeping; }
    // Loudness, roughly 0..1, for drawing.
    float level() const { return std::sqrt(std::max(0.0f, m_energy) / m_refEnergy); }
    float rate() const { return m_rate; }

    // --- for tests, the page and the docs: the instrument as built -----------------------------------

    float tension() const { return m_tension; }
    // The tension this moment, stretched by everything bending the skin.
    float tensionNow() const { return m_tensionNow; }
    float stickMass() const { return m_stickMass; }
    // The skin's own (0,1) mode, in vacuo, at the tuned tension.
    float skinHz() const { return float(2.4048 / (kTwoPi * Physics::kRadius) * std::sqrt(m_tension / Physics::kSkin)); }
    float modeHz(int k) const { return m_hz[std::clamp(k, 0, kModes - 1)]; }
    int modeM(int k) const { return m_m[std::clamp(k, 0, kModes - 1)]; }
    float stickHz(int k) const { return m_shz[std::clamp(k, 0, kStick - 1)]; }
    // The shell's quarter wave, with the open end's correction.
    static float shellHz() { return Physics::kC / (4.0f * (Physics::kShellLength + 0.6133f * Physics::kRadius)); }
    // The driven head's first resonance (Hz) as linear acoustics has it, at
    // tension T, a thumb at r (0..1) pressing with force (0..1; 0: none).
    double resonance(double T, float thumbR, float thumbForce) const {
        return resonanceK(T, thumbR, thumbForce > 0 ? thumbStiffness(thumbForce * Knobs::maxThumb) : 0.0);
    }
    // The same with the thumb's stiffness given (N/m).
    double resonanceK(double T, float thumbR, double kThumb) const;
    // The head's conductance seen by the cloth at the stick's middle (m/s
    // per N, its real part) at hz: its peaks are the resonances.
    double conductance(double T, float thumbR, double kThumb, double hz) const;
    // The thumb's pulp, pressed with force F (N): how far it gives (m) and
    // how stiff it is there (N/m). The pulp's Hertzian spring, and past
    // its depth the bone behind it.
    static double thumbDepth(double F) {
        double lo = 0, hi = 0.02;
        for (int i = 0; i < 50; ++i) {
            const double mid = 0.5 * (lo + hi);
            (pulpForce(mid) < F ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }
    static double pulpForce(double d) {
        // Fingertip pulp stiffens exponentially: F = A·(e^{δ/δ₀} − 1), so its
        // stiffness grows with the force, (F + A)/δ₀.
        if (d <= 0)
            return 0;
        return Physics::kThumbPulpA * (std::exp(std::min(d / Physics::kThumbPulpD, 8.0)) - 1.0);
    }
    static double thumbStiffness(double F) {
        const double d = thumbDepth(std::max(F, 0.01)), e = 1e-6;
        return (pulpForce(d + e) - pulpForce(d - e)) / (2 * e);
    }
    // How many hands and thumbs are on it.
    int hands() const {
        int n = 0;
        for (const auto &h : m_hands)
            n += h.on;
        return n;
    }
    int thumbs() const {
        int n = 0;
        for (const auto &t : m_thumbs)
            n += t.on;
        return n;
    }
    // The nearest a thumb can press to the knot (of the head's radius):
    // beside it, the pad of the thumb against the cord's knot. (On top of
    // the knot the thumb-stiffened fundamental and the stick-loaded (0,2)
    // fight for the cloth, and the note flips between them.)
    static float minThumbR() { return Knobs::thumbNear; }
    // How hard the thumbs press now, 0..1 of a hard press.
    float pressNow() const {
        float p = 0;
        for (const auto &t : m_thumbs)
            if (t.on)
                p = std::max(p, t.push / Knobs::maxThumb);
        return p;
    }
    // The friction force the first hand's cloth makes now (N), and whether
    // it sticks: for the tests' look at the stick–slip.
    float frictionNow() const { return m_lastFriction; }
    float dbg(int i) const { const Hand &h = m_hands[0]; return i == 0 ? h.bone : i == 1 ? h.target : i == 2 ? h.cloth : i == 3 ? h.rest : i == 4 ? h.gripNow : h.boneV; }
    bool stuckNow() const { return m_lastStuck; }
    // The head's middle, as the knot holds it (m, outward).
    float knotX() const { return float(m_lastKnot); }
    // A whole hand stroke takes this long once begun (for the drums' voices).
    static constexpr int kScripted = 1 << 28;
    static constexpr float kArmSpeed = 3.0f; // m/s
    static constexpr float kThumbSpeed = 0.5f; // m/s, a thumb sliding over the skin
    static constexpr float kArmMass = 0.3f;  // kg, the forearm and hand as the cloth feels them
    static constexpr float kThumbLead = 0.02f; // s, a pressed stroke's thumb before its cloth
    static constexpr float kHold = 0.08f;      // s, a stroke's hand holds the stick still before letting go

private:
    struct Hand {
        bool on = false, leaving = false, script = false, stuck = false;
        int id = 0, delay = 0, age = 0;
        float target = 0, from = 0, to = 0, ramp = 1, rampAt = 0, sinceMove = 0;
        float bone = 0, boneV = 0;      // the arm: its place along the stick (m from the knot), speed
        float cloth = 0, clothV = 0;    // the cloth: its offset from the arm (m, outward), its speed (m/s, outward)
        float grip = 0, gripNow = 0;    // 0..1 asked, N now
        float scriptFrom = 0, scriptLength = 0, scriptSeconds = 0.1f;
        float pull = 0;                 // the cloth's pull on the arm (N, away from the knot)
        float rest = 0;                 // where the fingertips' shear has crept to (m)
        float settled = 1;              // how settled the contact is, 0..1
        float drive = 0;                // the arm's speed as the squeeze follows it (m/s)
        float pace = 0;                 // its speed of late, over a tenth of a second
        float fresh = 0, heading = 0;   // the stroke's opening squeeze, 0..1, and its way
        bool armed = true, rising = false; // the opening squeeze waits for the first slip, then comes in
        int slipRun = 0;
    };
    struct Thumb {
        bool on = false, leaving = false, script = false;
        int id = 0, delay = 0, age = 0;
        float r = 0, rTarget = 0, force = 0, push = 0; // where (0..1), asked (0..1), N now
        float bone = 0, boneV = 0;                     // the thumb's bone, inward (m, m/s)
        float scriptFrom = 0, scriptTo = 0, scriptSeconds = 0;
        float phi[kModes] = {};                        // the head's modes, over its patch
        float lastR = -1, lastForce = 0;
        float give = 0; // Σφ²·dt²/m: the skin's step under the patch per newton
        bool quick = false; // a stroke's thumb: down in a few milliseconds
        float scriptR0 = 0, scriptR1 = 0;
    };

    // How far through its length a stroke is at a fraction p of its time:
    // the arm comes out of the turn already moving and speeds up through
    // most of the stroke (the note gliding up as the cloth goes faster, as
    // a stick–slip note does), holds, and stops quickly to turn.
    static float strokeShape(float p) {
        // speed: from a third of its top, rising to the top over [0, 0.25],
        // held to 0.92, falling to 0 at 1.
        const float a0 = Knobs::strokeFrom, up = Knobs::strokeUp, hold = 0.92f;
        const float area = up * (a0 + 1.0f) * 0.5f + (hold - up) + 0.5f * (1.0f - hold);
        float x;
        if (p < up)
            x = a0 * p + (1.0f - a0) * 0.5f * p * p / up;
        else if (p < hold)
            x = up * (a0 + 1.0f) * 0.5f + (p - up);
        else {
            const float q = p - hold;
            x = up * (a0 + 1.0f) * 0.5f + (hold - up) + q - 0.5f * q * q / (1.0f - hold);
        }
        return x / area;
    }

    void wake() {
        m_sleeping = false;
        m_quiet = 0;
    }

    float place(float along) const {
        // The cloth can't reach the knot (the hand would hit the head) or
        // run off the end.
        return Physics::kStickLength * (0.12f + 0.8f * clampf(along, 0, 1));
    }

    Hand *handFor(int id) {
        for (auto &h : m_hands)
            if (h.on && h.id == id)
                return &h;
        return nullptr;
    }
    Hand *freeHand() {
        for (auto &h : m_hands)
            if (!h.on)
                return &h;
        Hand *o = &m_hands[0];
        for (auto &h : m_hands)
            if (h.age > o->age)
                o = &h;
        return o;
    }
    Thumb *thumbFor(int id) {
        for (auto &t : m_thumbs)
            if (t.on && t.id == id)
                return &t;
        return nullptr;
    }
    Thumb *freeThumb() {
        for (auto &t : m_thumbs)
            if (!t.on)
                return &t;
        Thumb *o = &m_thumbs[0];
        for (auto &t : m_thumbs)
            if (t.age > o->age)
                o = &t;
        return o;
    }

    // The thumb's patch over the head's modes: the mean of each mode over a
    // disc is its value at the middle times 2J₁(kρ)/(kρ) (Graf).
    void shapeThumb(Thumb &t) {
        if (std::fabs(t.r - t.lastR) < 1e-3f)
            return;
        t.lastR = t.r;
        const double a = Physics::kRadius;
        const double r0 = std::max(double(minThumbR()), double(t.r)) * a;
        t.give = 0;
        for (int k = 0; k < kModes; ++k) {
            const double kk = m_j[k] / a;
            const double kr = kk * Physics::kThumbPatch;
            const double avg = 2.0 * besselJ(1, kr) / kr;
            t.phi[k] = float(besselJ(m_m[k], kk * r0) * avg);
            t.give += t.phi[k] * t.phi[k] * m_inv[k];
        }
    }

    void build();
    void retune();
    void thumb(Thumb &t, float ease);

    float m_rate = 48000, m_dt = 1.0f / 48000;
    // The head.
    int m_m[kModes] = {};
    float m_j[kModes] = {};
    alignas(32) float m_x[kModes] = {}, m_u[kModes] = {}, m_f[kModes] = {};
    alignas(32) float m_d[kModes] = {}, m_cd[kModes] = {}, m_cs[kModes] = {}, m_inv[kModes] = {};
    alignas(32) float m_knot[kModes] = {}, m_area[kModes] = {}, m_stretch[kModes] = {};
    float m_mass[kModes] = {}, m_hz[kModes] = {}, m_tau[kModes] = {};
    // The stick.
    float m_sx[kStick] = {}, m_su[kStick] = {}, m_sd[kStick] = {}, m_sc[kStick] = {}, m_sinv[kStick] = {};
    float m_smass[kStick] = {}, m_shz[kStick] = {};
    float m_knotGive = 0;
    float m_driveFall = 1, m_paceFall = 1, m_easeFall = 1, m_easeRise = 0;
    float m_stickMass = 0;
    // The shell's air.
    float m_out[kLine] = {}, m_in[kLine] = {}; // toward the open end, back toward the head
    int m_wr = 0;
    float m_lineDelay = 30;
    float m_reflect = 0, m_reflectA = 0.5f;  // the open end's loss, a one-pole
    float m_z0 = 1, m_zFront = 1, m_frontA = 0.9f;
    float m_front = 0, m_frontIn = 0;         // the radiation's pressure on the head's front, high-passed
    float m_uIn = 0, m_uFront = 0;            // last sample's volume velocities
    float m_backDelay = 40;
    OnePoleLp m_backL, m_backR;
    float m_lastL = 0, m_lastR = 0;
    // Tension.
    float m_tension = 3000, m_tensionNow = 3000, m_stretchK = 0;
    // The players.
    Hand m_hands[kHands];
    Thumb m_thumbs[kThumbs];
    uint32_t m_scriptSerial = 0;
    Rng m_rng{0xc01cau};
    Svf m_noise, m_raspLo, m_raspF;
    float m_endLine[64] = {};
    int m_endW = 0;
    static constexpr int kEndDelayL = 14, kEndDelayR = 40; // samples: 0.3 and 0.8 ms
    float m_gain = 1;
    float m_energy = 0, m_refEnergy = 1, m_sleepEnergy = 0;
    int m_quiet = 0;
    float m_muteD = 1.0f;
    bool m_sleeping = true;
    float m_lastFriction = 0;
    bool m_lastStuck = false;
    double m_lastKnot = 0;
};

// --- building it ------------------------------------------------------------------------------------------

inline void Body::build() {
    static const struct {
        int m;
        float j;
    } modes[kModes] = {
        {0, 2.40483f}, {0, 5.52008f}, {0, 8.65373f}, {0, 11.79153f}, {0, 14.93092f}, {0, 18.07106f},
        {1, 3.83171f}, {1, 7.01559f}, {1, 10.17347f}, {1, 13.32369f}, {1, 16.47063f},
        {2, 5.13562f}, {2, 8.41724f}, {2, 11.61984f}, {2, 14.79595f},
        {3, 6.38016f}, {3, 9.76102f}, {3, 13.01520f},
        {4, 7.58834f}, {4, 11.06471f},
        {5, 8.77148f},
    };
    const double a = Physics::kRadius, S = kPi * a * a;
    for (int k = 0; k < kModes; ++k) {
        m_m[k] = modes[k].m;
        m_j[k] = modes[k].j;
        const double j = modes[k].j;
        // Modal mass: σ∫φ²dA, with φ = J_m(j r/a)·cos mθ (1 at the middle
        // for the round modes).
        const double jm1 = besselJ(m_m[k] + 1, j);
        m_mass[k] = float(Physics::kSkin * S * jm1 * jm1 * (m_m[k] == 0 ? 1.0 : 0.5));
        // The knot's patch: a little off the middle, as a hand ties it, so
        // the stick moves the lobed modes a little as well as the round
        // ones. Only the round ones sweep a volume.
        {
            const double kr = j / a * Physics::kKnot;
            m_knot[k] = float(besselJ(m_m[k], j / a * Physics::kKnotOffset) * 2.0 * besselJ(1, kr) / kr);
        }
        if (m_m[k] == 0)
            m_area[k] = float(2.0 * kPi * a * a * besselJ(1, j) / j);
        // ∫|∇φ|²dA = (j/a)²·∫φ²dA: the skin's stretch, per square metre of
        // swing, is this over the head's area.
        m_stretch[k] = float(std::pow(j / a, 2.0) * S * jm1 * jm1 * (m_m[k] == 0 ? 1.0 : 0.5) / (2.0 * S));
    }
    // The stick: a free–free bamboo rod. Its rigid motion (mass M) and two
    // compression modes (M/2 each, cos nπs/L along it).
    const double A = kPi * Physics::kStickRadius * Physics::kStickRadius;
    m_stickMass = float(Physics::kBamboo * A * Physics::kStickLength);
    const double cl = std::sqrt(Physics::kBambooE / Physics::kBamboo);
    for (int k = 0; k < kStick; ++k) {
        m_smass[k] = m_stickMass * (k == 0 ? 1.0f : 0.5f);
        m_shz[k] = float(k * cl / (2.0 * Physics::kStickLength));
    }
    // The shell's air: a plane wave each way, Z₀ = ρc/S, over its length
    // and the open end's correction.
    m_z0 = float(Physics::kRhoAir * Physics::kC / S);
    m_lineDelay = float((Physics::kShellLength + 0.6133 * a) / Physics::kC * m_rate);
    // The open end gives back less the higher it goes (|R| ≈ e^{−(ka)²/2}
    // up to ka ≈ 1): a one-pole low-pass at 0.78·c/(2πa).
    {
        const float fc = 0.78f * Physics::kC / (kTwoPi * float(a));
        m_reflectA = 1.0f - std::exp(-kTwoPi * fc / m_rate);
        // (its phase lag lengthens the loop: taken back out of the line)
        m_lineDelay = std::max(2.0f, m_lineDelay - 0.5f * (1.0f - m_reflectA) / m_reflectA);
    }
    // The front of the head radiates into the open air: a mass (ρ·0.61a/S)
    // in parallel with ρc/S, a high-pass at c/(0.61a) of the volume velocity.
    m_zFront = m_z0;
    m_frontA = std::exp(-float(Physics::kC / (0.6133 * a)) / m_rate);
    // The open end is round the shell from the microphone: later, and
    // duller (diffraction round the body).
    m_backDelay = float((Physics::kShellLength + 0.1) / Physics::kC * m_rate);
    m_backL.set(1800.0f, m_rate);
    m_backR.set(2200.0f, m_rate);
    m_noise.set(1200.0f, 0.7f, m_rate);
    m_raspLo.set(1500.0f, 0.6f, m_rate);
    m_raspF.set(6000.0f, 0.6f, m_rate);
    m_tension = Physics::kTension;
    retune();
}

inline void Body::retune() {
    const double a = Physics::kRadius;
    const double c = std::sqrt(m_tension / Physics::kSkin);
    for (int k = 0; k < kModes; ++k) {
        const double w = m_j[k] / a * c;
        m_hz[k] = float(w / kTwoPi);
        // The skin's own loss; the air takes more from the round ones itself.
        m_tau[k] = float(Knobs::skinQ / (kPi * std::max(m_hz[k], 20.0f)));
        const double r = std::exp(-1.0 / (m_tau[k] * m_rate));
        const double th = std::min(w / m_rate, 2.6);
        m_d[k] = float(r * r);
        // c = (1 − r)² + r·4sin²(θ/2): the damping part and the spring
        // part, so the spring can follow the tension sample by sample.
        m_cd[k] = float((1 - r) * (1 - r));
        m_cs[k] = float(r * 4.0 * std::pow(std::sin(0.5 * th), 2.0));
        m_inv[k] = m_dt * m_dt / m_mass[k];
    }
    for (int k = 0; k < kStick; ++k) {
        const double w = kTwoPi * m_shz[k];
        const double tau = k == 0 ? 10.0 : Knobs::stickQ / (kPi * m_shz[k]);
        const double r = std::exp(-1.0 / (tau * m_rate));
        const double th = std::min(w / m_rate, 2.6);
        m_sd[k] = float(r * r);
        m_sc[k] = float((1 - r) * (1 - r) + r * 4.0 * std::pow(std::sin(0.5 * th), 2.0));
        m_sinv[k] = m_dt * m_dt / m_smass[k];
    }
    m_driveFall = std::exp(-1.0f / (Knobs::squeezeHold * m_rate));
    m_paceFall = std::exp(-1.0f / (0.1f * m_rate));
    m_easeFall = std::exp(-1.0f / (Knobs::easeTime * m_rate));
    m_easeRise = 1.0f - std::exp(-1.0f / (0.015f * m_rate)); // (in over a few tens of milliseconds)
    // The knot's give: how far its two ends part per newton in one step.
    m_knotGive = 0;
    for (int k = 0; k < kStick; ++k)
        m_knotGive += m_sinv[k];
    for (int k = 0; k < kModes; ++k)
        m_knotGive += m_knot[k] * m_knot[k] * m_inv[k];
    // The stretch: ΔT = Y·ε, ε the mean strain, ½∫|∇w|²/S.
    m_stretchK = Physics::kStretch;
    m_tensionNow = m_tension;
    // Pressure at a metre from a volume acceleration: ρ/(4π)·dQ/dt.
    m_gain = Knobs::level * Physics::kRhoAir / (4.0f * kPi);
    // The energy of a loud note's head, for drawing; asleep 90 dB under it.
    {
        const float amp = 1.5e-4f;
        const float w = kTwoPi * m_hz[0];
        m_refEnergy = 0.5f * (m_mass[0] + m_stickMass) * w * w * amp * amp;
        m_sleepEnergy = m_refEnergy * 1e-9f;
    }
}

// --- linear acoustics, for tuning and the tests -----------------------------------------------------------

inline double Body::conductance(double T, float thumbR, double kThumbIn, double probeHz) const {
    using cd = std::complex<double>;
    const double a = Physics::kRadius;
    const bool thumb = kThumbIn > 0;
    // The round modes always; all of them with a thumb.
    int idx[kModes], n = 0;
    for (int k = 0; k < kModes; ++k)
        if (thumb || m_m[k] == 0 || std::fabs(m_knot[k]) > 1e-3f)
            idx[n++] = k;
    const int N = n + kStick;
    // The thumb, linearised: its pulp's stiffness at the force it presses with.
    const double kThumb = kThumbIn;
    // (its Hunt–Crossley loss, λ times the force that stiffness comes with)
    const double thumbLoss =
        Physics::kThumbLoss * std::max(0.0, kThumb * Physics::kThumbPulpD - Physics::kThumbPulpA);
    double phiT[kModes] = {};
    if (thumb) {
        const double r0 = std::max(double(minThumbR()), double(thumbR)) * a;
        for (int i = 0; i < n; ++i) {
            const int k = idx[i];
            const double kk = m_j[k] / a, kr = kk * Physics::kThumbPatch;
            phiT[i] = besselJ(m_m[k], kk * r0) * 2.0 * besselJ(1, kr) / kr;
        }
    }
    const double c = std::sqrt(T / Physics::kSkin);
    const double cl = std::sqrt(Physics::kBambooE / Physics::kBamboo);
    // The cloth at the stick's middle.
    const double sCloth = 0.5 * Physics::kStickLength;
    auto admittance = [&](double hz) {
        const double w = kTwoPi * hz;
        cd M[kModes + kStick][kModes + kStick + 1] = {};
        const cd jw(0, w);
        const double ka = w / Physics::kC * a;
        // The open end, with the player's forearm in it taking part of what
        // it gives back.
        const cd zRad = double(m_z0) * cd(ka * ka / 4.0, 0.6133 * ka);
        const cd rEnd = double(Knobs::armInBore) * (zRad - double(m_z0)) / (zRad + double(m_z0));
        const cd zEnd = double(m_z0) * (1.0 + rEnd) / (1.0 - rEnd);
        const double kL = w / Physics::kC * Physics::kShellLength;
        const cd tn(std::tan(kL), 0);
        const cd zIn = double(m_z0) * (zEnd + cd(0, 1) * double(m_z0) * tn) / (double(m_z0) + cd(0, 1) * zEnd * tn);
        const cd zFront = double(m_z0) * cd(ka * ka / 4.0, 0.6133 * ka);
        const cd kTie(Physics::kKnotStiffness, w * Knobs::knotLoss);
        for (int i = 0; i < n; ++i) {
            const int k = idx[i];
            const double wk = m_j[k] / a * c;
            const double ck = 2.0 * m_mass[k] * kPi * (wk / kTwoPi) / Knobs::skinQ;
            M[i][i] += m_mass[k] * wk * wk - w * w * m_mass[k] + cd(0, w * ck);
            for (int i2 = 0; i2 < n; ++i2) {
                const int k2 = idx[i2];
                // Air on both faces, and the knot between skin and stick.
                M[i][i2] += jw * double(m_area[k]) * double(m_area[k2]) * (zIn + zFront);
                M[i][i2] += kTie * double(m_knot[k]) * double(m_knot[k2]);
                M[i][i2] += (kThumb + jw * thumbLoss) * phiT[i] * phiT[i2];
            }
            M[i][n] -= kTie * double(m_knot[k]);
            M[n][i] -= kTie * double(m_knot[k]);
        }
        for (int s = 0; s < kStick; ++s) {
            const double ws = kTwoPi * m_shz[s];
            const double cs = s == 0 ? 0.0 : 2.0 * m_smass[s] * ws / (2.0 * Knobs::stickQ);
            M[n + s][n + s] += m_smass[s] * ws * ws - w * w * m_smass[s] + cd(0, w * cs);
            // (the knot holds the stick's end, where every mode is 1)
            for (int s2 = 0; s2 < kStick; ++s2)
                M[n + s][n + s2] += kTie;
            if (s > 0)
                for (int i = 0; i < n; ++i) {
                    M[n + s][i] -= kTie * double(m_knot[idx[i]]);
                    M[i][n + s] -= kTie * double(m_knot[idx[i]]);
                }
            M[n + s][N] = std::cos(kPi * s * sCloth / Physics::kStickLength); // a unit force at the cloth
        }
        // Gauss–Jordan.
        for (int col = 0; col < N; ++col) {
            int piv = col;
            for (int r = col + 1; r < N; ++r)
                if (std::abs(M[r][col]) > std::abs(M[piv][col]))
                    piv = r;
            if (piv != col)
                for (int q = 0; q <= N; ++q)
                    std::swap(M[piv][q], M[col][q]);
            const cd p = M[col][col];
            if (std::abs(p) < 1e-30)
                return 0.0;
            for (int q = col; q <= N; ++q)
                M[col][q] /= p;
            for (int r = 0; r < N; ++r)
                if (r != col && std::abs(M[r][col]) > 0) {
                    const cd f = M[r][col];
                    for (int q = col; q <= N; ++q)
                        M[r][q] -= f * M[col][q];
                }
        }
        cd v = 0;
        for (int s = 0; s < kStick; ++s)
            v += jw * M[n + s][N] * std::cos(kPi * s * sCloth / Physics::kStickLength);
        return v.real(); // the conductance: its peaks are the resonances
    };
    (void)cl;
    return admittance(probeHz);
}

inline double Body::resonanceK(double T, float thumbR, double kThumbIn) const {
    // The strongest peak of the conductance between 80 Hz and 2 kHz: the
    // resonance the cloth's stick–slip locks to.
    double best = 0, top = 0;
    for (double f = 80; f < 2000; f *= 1.01) {
        const double y = conductance(T, thumbR, kThumbIn, f);
        if (y > top)
            top = y, best = f;
    }
    if (best <= 0)
        return 0;
    // Refine by golden section on the conductance.
    double lo = best / 1.012, hi = best * 1.012;
    for (int it = 0; it < 30; ++it) {
        const double m1 = lo + 0.382 * (hi - lo), m2 = lo + 0.618 * (hi - lo);
        if (conductance(T, thumbR, kThumbIn, m1) > conductance(T, thumbR, kThumbIn, m2))
            hi = m2;
        else
            lo = m1;
    }
    return 0.5 * (lo + hi);
}

// --- playing it -------------------------------------------------------------------------------------------

// A thumb on the head, this sample: the hand pushes the bone; the pulp
// between bone and skin is a Hertzian spring (and the bone behind it)
// with Hunt–Crossley loss, F = K(δ)·(1 + λ·δ̇); the head's predicted
// motion is in u. The loss is solved implicitly against the skin's
// response at the patch (the contact's admittance), so a firm thumb damps
// without ringing.
inline void Body::thumb(Thumb &t, float ease) {
    const float fs = m_rate, dt = m_dt;
    if (t.delay > 0) {
        --t.delay;
        return;
    }
    ++t.age;
    // (lifting, the hand draws the thumb back off the head)
    float want = t.leaving ? -2.0f : t.force * Knobs::maxThumb;
    if (t.script && !t.leaving) {
        const float p = std::min(1.0f, float(t.age) / (t.scriptSeconds * fs));
        want = Knobs::maxThumb * (t.scriptFrom + (t.scriptTo - t.scriptFrom) * p);
        t.rTarget = t.scriptR0 + (t.scriptR1 - t.scriptR0) * p;
        if (p >= 1.0f)
            t.leaving = true;
    }
    // The hand eases the push in, and lifts unhurriedly.
    t.push += (t.leaving ? 0.5f : t.quick ? 4.0f : 1.0f) * ease * (want - t.push);
    float skin = 0, P = 0; // inward: where the skin is, its predicted step
    for (int k = 0; k < kModes; ++k) {
        skin -= t.phi[k] * m_x[k];
        P -= t.phi[k] * m_u[k];
    }
    const float Y = t.give; // its give under the patch
    if (t.age == 1)
        t.bone = skin;
    const float delta = t.bone - skin;
    float F = 0;
    if (delta > 0) {
        const float Fs = float(pulpForce(delta));
        const float c = Physics::kThumbLoss * Fs;
        F = std::max(0.0f, (Fs + c * (t.boneV - fs * P)) / (1.0f + c * fs * Y));
        for (int k = 0; k < kModes; ++k)
            m_u[k] -= m_inv[k] * t.phi[k] * F;
    }
    t.lastForce = F;
    // The bone: the thumb and hand behind it, pushed by the arm, held back
    // by the pulp and its own sluggishness.
    constexpr float boneMass = 0.03f, boneLoss = 40.0f;
    t.boneV += (t.push - F - boneLoss * t.boneV) / boneMass * dt;
    t.bone += t.boneV * dt;
    if (t.leaving && t.push < 0.0f && delta <= 0)
        t.on = false;
}

inline void Body::render(float *outL, float *outR, int n) {
    if (m_sleeping)
        return;
    const float fs = m_rate, dt = m_dt;
    const float knotK = Physics::kKnotStiffness, knotC = Knobs::knotLoss;
    float *x = m_x, *u = m_u, *f = m_f;
    // Per block: the hands' and thumbs' slow parts.
    const float gripRate = 1.0f - std::exp(-1.0f / (0.012f * fs));
    // A thumb comes down onto the head over some tens of milliseconds.
    const float thumbRate = 1.0f - std::exp(-1.0f / (0.035f * fs));
    for (auto &t : m_thumbs) {
        if (!t.on)
            continue;
        // A thumb slides over the skin at a hand's pace, not faster.
        const float most = kThumbSpeed * float(n) / (fs * Physics::kRadius);
        t.r += clampf(0.3f * (t.rTarget - t.r), -most, most);
        shapeThumb(t);
    }
    for (int i = 0; i < n; ++i) {
        // The head's middle under the knot, its sweep, and its stretch.
        float knot = 0, volV = 0, stretch = 0;
        for (int k = 0; k < kModes; ++k) {
            knot += m_knot[k] * x[k];
            volV += m_area[k] * u[k];
            stretch += m_stretch[k] * x[k] * x[k];
        }
        // The skin stretched: every mode's spring goes up with the tension.
        m_tensionNow = m_tension + m_stretchK * stretch;
        const float g = m_tensionNow / m_tension;
        m_lastKnot = knot;
        // The air: inside, the wave coming back from the open end and what
        // the head pushes into the shell now (Z₀ times the volume velocity,
        // inward); outside, the radiation's mass and resistance.
        const float uIn = -volV * fs, uFront = volV * fs;
        const float back = m_in[(m_wr - int(m_lineDelay)) & (kLine - 1)];
        const float pIn = 2.0f * back + m_z0 * uIn;
        m_front = m_frontA * (m_front + uFront - m_frontIn);
        m_frontIn = uFront;
        const float pFront = m_zFront * m_front;
        const float pNet = pIn - pFront; // outward
        for (int k = 0; k < kModes; ++k)
            f[k] = m_area[k] * pNet;
        // The stick's predicted motion (the knot comes after the cloth).
        float sPred[kStick];
        for (int k = 0; k < kStick; ++k)
            sPred[k] = m_sd[k] * m_su[k] - m_sc[k] * m_sx[k];
        // The hands: the cloth on the stick.
        float rasp = 0;
        bool first = true;
        for (auto &h : m_hands) {
            if (!h.on)
                continue;
            if (h.delay > 0) {
                --h.delay;
                continue;
            }
            ++h.age;
            // Where the arm is going: the script's stroke, or the page's
            // piecewise-linear path.
            if (h.script) {
                const float p = float(h.age) / (h.scriptSeconds * fs);
                h.target = h.scriptFrom + h.scriptLength * strokeShape(std::min(p, 1.0f));
                // (and holds the stick still a moment, damping it, before it
                // lets go)
                if (float(h.age) >= (h.scriptSeconds + kHold) * fs)
                    h.leaving = true;
            } else {
                h.sinceMove += 1;
                if (h.rampAt < h.ramp) {
                    h.rampAt += 1;
                    h.target = h.from + (h.to - h.from) * (h.rampAt / h.ramp);
                }
            }
            // The arm follows where the player moves it, critically damped
            // (about 20 Hz), and gives under what the cloth pulls it with:
            // a hand at rest on the stick goes along with it a little.
            const float wn = kTwoPi * Knobs::armHz;
            h.boneV += (wn * wn * (h.target - h.bone) - 2.0f * wn * h.boneV + h.pull / kArmMass) * dt;
            // (no arm draws a cloth faster than a few metres a second)
            h.boneV = clampf(h.boneV, -kArmSpeed, kArmSpeed);
            h.bone += h.boneV * dt;
            // The grip: squeezing in and letting go take a little while.
            // The player squeezes in step with the draw, as a bow arm leans
            // in as it speeds up: keeping the load in proportion to the speed
            // keeps a stick–slip note's period, so the note holds whether the
            // stroke is quick or slow, speeding up or slowing into the turn.
            // The page's pinch is how much: a firm one holds the stick
            // longer each cycle at any speed, lower and rougher (the growl).
            // (the squeeze rides the stroke's speed, letting up over a few
            // milliseconds as it slows: held longer through the turn, it
            // pins the cloth and the head rings at the stuck resonance)
            h.drive = std::max(std::fabs(h.boneV), h.drive * m_driveFall);
            // A played stroke (a pad's, a key's) begins squeezed: out of
            // rest or a turn the player pinches harder and eases into the
            // glide over its first tens of milliseconds, so the note starts
            // on the flat side and climbs onto the head's, the cuíca's laugh.
            // (A finger on the page is its own player: its pressure is its
            // squeeze.)
            // (The extra squeeze comes in once the cloth first slips, over
            // some milliseconds, so it breaks away at the ordinary load and
            // speaks promptly, then lowers the note and fades.)
            {
                const float dir = h.boneV > 0.02f ? 1.0f : h.boneV < -0.02f ? -1.0f : 0.0f;
                if (dir == 0.0f || dir != h.heading) {
                    h.armed = true;
                    h.rising = false;
                    h.fresh = 0.0f;
                }
                if (dir != 0.0f)
                    h.heading = dir;
                h.slipRun = h.stuck ? 0 : h.slipRun + 1;
                if (h.armed && h.slipRun > 30 && dir != 0.0f) { // (a real slip, not the fibres catching)
                    h.armed = false;
                    h.rising = true;
                }
                if (h.rising) {
                    h.fresh += (1.0f - h.fresh) * m_easeRise + 0.001f;
                    if (h.fresh >= 0.98f)
                        h.rising = false;
                } else {
                    h.fresh *= m_easeFall;
                }
            }
            // (how fast the draw has been lately: a quick turn isn't a slow draw)
            h.pace = std::max(std::fabs(h.boneV), h.pace * m_paceFall);
            // (and a deliberately firm pinch bears down whatever the speed:
            // slow and firm, the cloth holds the stick most of each cycle,
            // the cuíca's low grunt)
            const float bear = Knobs::growl * std::max(0.0f, h.grip - Knobs::growlFrom) / (1.0f - Knobs::growlFrom) *
                               std::max(0.0f, 1.0f - h.pace / Knobs::growlSpeed);
            const float lean = clampf(Knobs::gripBase + bear +
                                          Knobs::gripPerSpeed * h.drive * (1.0f + Knobs::gripLean * (h.grip - 0.5f)) *
                                              (1.0f + (h.script ? Knobs::easeIn * h.fresh : 0.0f)),
                                      0.04f, 1.0f);
            const float gripWant = h.leaving ? 0.0f : lean * Knobs::maxGrip;
            // (the pinch follows the arm as it moves, at once: a player's two
            // hands, and their pinch and draw, move as one; it lets go
            // more gently)
            h.gripNow += (h.leaving ? 3.0f * gripRate : Knobs::gripFollow) * (gripWant - h.gripNow);
            if (h.leaving && h.gripNow < 0.05f) {
                h.on = false;
                continue;
            }
            // Along the stick the cloth's axis is the stick's own, outward
            // (toward the head) positive; the hand drawing away from the
            // knot is moving inward.
            const float s = h.bone; // (where along the stick: the arm's place, give or take the cloth's shear)
            float phi[kStick];
            float vStick = 0, Y = dt / Knobs::cloth;
            for (int k = 0; k < kStick; ++k) {
                phi[k] = std::cos(kPi * float(k) * s / Physics::kStickLength);
                vStick += phi[k] * sPred[k] * fs;
                Y += phi[k] * phi[k] * m_sinv[k] * fs;
            }
            // The cloth, held by the fingertips in shear: its offset from
            // the arm, outward.
            const float armV = -h.boneV;
            // (pulp in shear creeps: a load held still relaxes over a few
            // tenths of a second, so a hand resting on the stick lets go of
            // what it was pulling with rather than keep it on the edge)
            h.rest += (h.cloth - h.rest) * dt / Knobs::creep;
            const float shear = -(Knobs::shear + Knobs::shearPerN * h.gripNow) * (h.cloth - h.rest) -
                                Knobs::shearLoss * (h.clothV - armV);
            h.pull = shear; // (along the stick away from the knot, on the arm)
            const float vCloth = h.clothV + shear / Knobs::cloth * dt;
            // The wet fibres catch and let go as they slide: the contact's
            // load wavers (not while the cloth holds still).
            const float fibres = m_noise.step(m_rng.bipolar()).band;
            const float N = h.gripNow * (1.0f + (h.stuck ? 0.0f : Knobs::rough * fibres));
            const float vr = vCloth - vStick;            // the cloth over the stick
            const float need = -vr / Y;                  // the force that would hold them together
            float F;                                     // on the cloth
            // The contact's age (rate-and-state): a wet cloth that has sat
            // on the stick grips harder (water films thin, fibres settle
            // into the bamboo), and sliding wears that off over a few
            // centimetres of travel; it heals again in a fraction of a
            // second. So a stroke out of rest starts held longer (lower)
            // and rises as it slides, and doesn't sag as the arm slows.
            // (it settles again quickly while the arm is all but still, at
            // a turn, and slowly while it slides)
            h.settled += (1.0f - h.settled) * dt / (std::fabs(h.boneV) < Knobs::still ? Knobs::regrip : Knobs::heal);
            if (!h.stuck)
                h.settled -= h.settled * std::min(1.0f, std::fabs(h.clothV - vStick) * dt / Knobs::wear);
            const float muS = Knobs::muStatic + Knobs::aged * h.settled, muD = Knobs::muKinetic;
            bool stuck = false;
            if (std::fabs(need) <= muS * N && (h.stuck || std::fabs(vr) < 1e-4f)) {
                F = need;
                stuck = true;
            } else {
                // |v| = |vr| − Y·N·(μd + Δμ/(1 + |v|/v0)): a quadratic in |v|.
                const float w0 = std::fabs(vr), v0 = Knobs::muSpeed, dm = muS - muD;
                const float b = w0 - Y * N * muD;
                const float B = v0 - b, Cq = -b * v0 + Y * N * dm * v0;
                const float disc = B * B - 4.0f * Cq;
                float w = -1;
                if (disc >= 0)
                    w = 0.5f * (-B + std::sqrt(disc));
                if (w > 0) {
                    const float sg = vr > 0 ? 1.0f : -1.0f;
                    F = -sg * N * (muD + dm / (1.0f + w / v0));
                } else {
                    F = std::clamp(need, -muS * N, muS * N);
                    stuck = std::fabs(need) <= muS * N;
                }
            }
            h.stuck = stuck;
            // Apply: the cloth gets F, the stick −F where the cloth is.
            h.clothV = vCloth + F / Knobs::cloth * dt;
            h.cloth += (h.clothV - armV) * dt;
            for (int k = 0; k < kStick; ++k)
                sPred[k] -= m_sinv[k] * phi[k] * F;
            // The cloth hisses on the bamboo as it slides, as cloth does:
            // noise in step with how fast and how hard.
            if (!stuck)
                rasp += std::fabs(h.clothV - (vStick - m_sinv[0] * F * fs)) * N;
            if (first) {
                m_lastFriction = F;
                m_lastStuck = stuck;
                first = false;
            }
        }
        const float hiss = m_raspF.step(m_raspLo.step(rasp * m_rng.bipolar()).high).low * Knobs::rasp;
        // The head's predicted step, its springs stretched with the skin.
        for (int k = 0; k < kModes; ++k)
            u[k] = m_d[k] * u[k] - (m_cd[k] + g * m_cs[k]) * x[k] + m_inv[k] * f[k];
        // The knot: a stiff, lossy spring between the stick's end (where
        // every one of its modes is 1) and the skin's middle, solved
        // against both sides' give, so its stiffness can be a dried
        // knot's without the step going unstable.
        {
            float gap = 0, gapU = 0;
            for (int k = 0; k < kStick; ++k) {
                gap += m_sx[k] + sPred[k];
                gapU += sPred[k];
            }
            for (int k = 0; k < kModes; ++k) {
                gap -= m_knot[k] * (x[k] + u[k]);
                gapU -= m_knot[k] * u[k];
            }
            const float tie = (knotK * gap + knotC * fs * gapU) / (1.0f + (knotK + knotC * fs) * m_knotGive);
            for (int k = 0; k < kStick; ++k)
                sPred[k] -= m_sinv[k] * tie;
            for (int k = 0; k < kModes; ++k)
                u[k] += m_inv[k] * m_knot[k] * tie;
        }
        for (int k = 0; k < kStick; ++k) {
            m_su[k] = sPred[k];
            m_sx[k] += m_su[k];
        }
        // The thumbs: pulp against skin, solved with the skin's own response
        // (the pulp's loss is stiff enough to need it).
        for (auto &t : m_thumbs)
            if (t.on)
                thumb(t, thumbRate);
        float volU = 0;
        for (int k = 0; k < kModes; ++k) {
            x[k] += u[k];
            volU += m_area[k] * u[k];
        }
        // The shell: what the head pushes in now goes toward the open end;
        // what arrives there goes back inverted, less what radiates.
        const float pOut = back + m_z0 * (-volU * fs);
        m_wr = (m_wr + 1) & (kLine - 1);
        m_out[m_wr] = pOut;
        const float arrive = m_out[(m_wr - int(m_lineDelay)) & (kLine - 1)];
        // (the cloth hand reaches into the shell through its open end, and
        // the forearm in the bore takes much of what the end gives back)
        m_reflect += m_reflectA * (-Knobs::armInBore * arrive - m_reflect);
        m_in[m_wr] = m_reflect;
        // The air leaving the open end (outward): (p⁺ − p⁻)/Z₀.
        const float uEnd = (arrive - m_reflect) / m_z0;
        const float uHead = volU * fs;
        // At the microphone: the volume acceleration of each.
        // (the open end is round the shell, toward the player's left: the
        // left ear hears it a little sooner than the right)
        m_endLine[m_endW = (m_endW + 1) & 63] = uEnd;
        const float qL = uHead + Knobs::back * m_backL.step(m_endLine[(m_endW - kEndDelayL) & 63]);
        const float qR = uHead + Knobs::back * m_backR.step(m_endLine[(m_endW - kEndDelayR) & 63]);
        outL[i] += (qL - m_lastL) * fs * m_gain + hiss;
        outR[i] += (qR - m_lastR) * fs * m_gain + 0.8f * hiss;
        m_lastL = qL;
        m_lastR = qR;
        if (m_muteD < 1.0f)
            for (int k = 0; k < kModes; ++k)
                u[k] *= m_muteD;
    }
    // Asleep once it has rung out and nothing touches it.
    float e = 0;
    for (int k = 0; k < kModes; ++k) {
        const float w2 = (m_cd[k] + m_cs[k]) * fs * fs;
        e += 0.5f * m_mass[k] * (w2 * x[k] * x[k] + u[k] * u[k] * fs * fs);
    }
    m_energy = e;
    bool touching = false;
    for (const auto &h : m_hands)
        touching |= h.on;
    for (const auto &t : m_thumbs)
        touching |= t.on;
    if (!touching && e < m_sleepEnergy) {
        if (++m_quiet > 8)
            silence();
    } else {
        m_quiet = 0;
    }
}

}
