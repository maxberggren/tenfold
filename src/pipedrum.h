#pragma once

// A pipe drum, as physics: a rack of PVC drain pipes, open at both ends,
// slapped on their mouths with a flip-flop (a tubulum, a thongophone, the
// "pipe drum" of street drummers). Not samples and not a recipe: the air in
// each pipe, the paddle, and the plastic, in SI units, integrated sample by
// sample. The measurements it is built from are in docs/pipe-drum.md.
//
//   the air     each pipe's column is a pair of travelling pressure waves
//               (a digital waveguide: exact for a cylinder, whatever the
//               ends do), slowed and worn away on the way by the walls'
//               viscous and thermal boundary layer (α ≈ 3·10⁻⁵ √f / a per
//               metre; the same layer slows the wave a few cents' worth).
//   the ends    each end is solved sample by sample as what it is: the air
//               just outside the mouth, an inertance ρδ/S (the end
//               correction, 0.6133 a unflanged) in parallel with a
//               radiation resistance that grows as ω² ((ka)²/4 · ρc/S).
//               So the open end reflects nearly all of a low note back,
//               inverted and a little late, and lets the highs out: the
//               round, boomy pop.
//   the paddle  a flip-flop's sole of EVA foam on a hand. It flies at the
//               mouth, squeezes the air between itself and the rim out
//               sideways and into the pipe, lands on the rim (the foam a
//               stiffening spring, solved against the rim's own motion),
//               pushes the column like a piston while it covers the mouth,
//               and leaves. While it is near, the mouth is a gap: the air
//               leaving it has the inertance of a radial sheet between two
//               discs (ρ/8πh, so a lid at height h lowers the pitch as a
//               key does a clarinet's hole) and loses its jet's dynamic
//               head. So the column starts closed at one end and opens as
//               the paddle lifts: the quarter-wave "thoomp" of the first
//               milliseconds blooming into the half-wave note. Held on, the
//               paddle keeps the mouth shut: the pipe sounds as one closed
//               at one end, near an octave lower, odd harmonics only; lifted
//               slowly, the note glides up to the open one.
//   the plastic thin PVC (1.8 mm, E ≈ 3 GPa, loss factor ≈ 0.015 to 0.03):
//               the pipe's own axial, bending and ovalling modes, struck
//               through the rim by the paddle's force and radiating weakly:
//               the dry plastic knock under the note.
//   the sound   what reaches two microphones at the player's head is the
//               air pushed out of each pipe's two ends (volume velocity,
//               differentiated: a small source's pressure follows its
//               volume acceleration), each end after its own time of
//               flight, the far ends a pipe's length further away; the
//               paddle's own crack (a plate stopped dead radiates as a
//               dipole); and the plastic's knock.
//
// Each pipe costs a few dozen operations a sample while it sounds, and none
// once it has rung out.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"

namespace tf::pipedrum {

constexpr int kPipes = 10;

// --- the rack, as the pad draws it --------------------------------------------------------------------------
//
// The pipes' mouths face the player, lowest at the left and up the scale to
// the right, zig-zagging between a near row and a far one (as a tubulum's
// rack staggers them so they fit). Pad units: x across, 0..1; y from the
// player's edge, 0..1.

struct Mouth {
    float x, y;
};
constexpr float kRows[2] = {0.36f, 0.74f};
constexpr float kMouthRx = 0.082f, kMouthRy = 0.16f; // a mouth's radius on the pad, each way

inline Mouth mouth(int n) {
    n = std::clamp(n, 0, kPipes - 1);
    return {0.095f + 0.09f * float(n), kRows[n & 1]};
}

// Where on a pipe a point is: u across, w along the pad, in mouth radii;
// rho how far from its middle.
struct Spot {
    int pipe = 0;
    float u = 0, w = 0, rho = 0;
};

inline Spot spotOn(int n, float x, float y) {
    const Mouth m = mouth(n);
    Spot s;
    s.pipe = std::clamp(n, 0, kPipes - 1);
    s.u = (x - m.x) / kMouthRx;
    s.w = (y - m.y) / kMouthRy;
    s.rho = std::sqrt(s.u * s.u + s.w * s.w);
    return s;
}

// The pipe a point is over, or nearest: every point of the pad plays one.
inline Spot spotAt(float x, float y) {
    Spot best;
    best.rho = 1e9f;
    for (int n = 0; n < kPipes; ++n) {
        const Spot s = spotOn(n, x, y);
        if (s.rho < best.rho)
            best = s;
    }
    return best;
}

// How far from a mouth's middle a strike can be said to be (in its radii)
// in a hit's 0..1 position.
constexpr float kSpotReach = 2.0f;

// --- the numbers ------------------------------------------------------------------------------------------
//
// SI units. Sources in docs/pipe-drum.md.

struct Physics {
    // Air at 20 °C.
    static constexpr float kC = 343.2f;        // m/s
    static constexpr float kRhoAir = 1.204f;   // kg/m³
    // Kirchhoff's boundary layer: α = kWall·√f / a (Np/m), and the phase
    // velocity is c·(1 − α/k).
    static constexpr float kWall = 2.95e-5f;   // from ν = 1.51e-5 m²/s, γ = 1.40, Pr = 0.71
    // JIS K 6741 VU 50: 60 mm outside, 1.8 mm wall at least, 2.0 mm as made
    // (the standard's 56 mm bore).
    static constexpr float kOuter = 0.030f;    // m, outside radius
    static constexpr float kThick = 0.0020f;   // m, wall
    static constexpr float kRadius = kOuter - kThick; // m, the bore's
    // Unplasticised PVC.
    static constexpr float kPvcE = 2.94e9f;    // Pa (JIS)
    static constexpr float kPvcRho = 1430.0f;  // kg/m³
    static constexpr float kPvcNu = 0.38f;
    static constexpr float kPvcEta = 0.03f;    // loss factor near 1 kHz (0.028–0.040)
    // End corrections of an unflanged pipe (Levine & Schwinger) and the
    // radiation resistance's ω² slope ((ka)²/4 · ρc/S).
    static constexpr float kEnd = 0.6133f;
    // A flip-flop: an EVA foam sole, 12 mm thick, on a hand.
    static constexpr float kPaddleMass = 0.25f;  // kg: the sole and the hand behind it, as the wrist flicks
    static constexpr float kSoleArea = 0.025f;   // m²: 25 × 10 cm
    static constexpr float kSoleThick = 0.012f;  // m
    static constexpr float kFoamE = 6.0e6f;      // Pa, a sole's EVA (150–250 kg/m³)
    static constexpr float kSoleHalf = 0.05f;    // m, how far the sole reaches past the bore's axis, across
    // How far apart two pipes' mouths are in the rack, and how the pipes
    // run away from the player: back and down.
    static constexpr float kPitch = 0.07f;       // m, mouth to mouth across
    static constexpr float kTier = 0.075f;       // m, the far row above the near
    static constexpr float kSlope = 0.6f;        // rad, how steeply the pipes fall away
};

// The model's constants that measurements and listening set (docs/pipe-drum.md
// says which measurement each one answers to).
struct Knobs {
    static inline float level = 0.48f;     // full scale per pascal at the microphones
    static inline float crack = 0.03f;      // the paddle's own dipole
    static inline float plastic = 0.5f;     // how much of the wall's motion is heard
    static inline float jet = 0.6f;         // the jet's discharge coefficient at the gap and the open ends
    static inline float spread = 0.85f;     // the rim sinks into the sole, which flexes: k = spread·E·A/T
    static inline float foamLoss = 0.4f;    // s/m, Hunt–Crossley: restitution ≈ 1 − ⅔λv, about a half at a slap's 2–4 m/s
    static inline float tilt = 0.15f;       // the share of the paddle's force across the pipe, struck square
    static inline float leak = 5.0e-6f;     // m: the gap a paddle pressed on a sawn rim still leaves
};

// --- the instrument -----------------------------------------------------------------------------------------

class Body {
public:
    static constexpr int kLine = 1024;  // each direction's delay line: 7 m of pipe at 48 kHz
    static constexpr int kOut = 1024;   // each end's flight to the microphones
    static constexpr int kWallModes = 7;
    static constexpr int kEvents = 64;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        for (int n = 0; n < kPipes; ++n)
            m_hz[n] = 110.0f * std::exp2(float(n) * 2.0f / 12.0f);
        m_tuned = false;
        retune();
        silence();
    }

    // The open (both ends) pitch of each pipe, in Hz: the pipes are cut to
    // the lengths that sound them. Retuning recuts them under whatever rings.
    void tune(const float *hz) {
        bool same = m_tuned;
        for (int n = 0; n < kPipes; ++n)
            same = same && std::fabs(hz[n] - m_hz[n]) <= m_hz[n] * 1e-5f;
        if (same)
            return;
        for (int n = 0; n < kPipes; ++n)
            m_hz[n] = std::clamp(hz[n], 40.0f, 1500.0f);
        m_tuned = true;
        retune();
    }

    // A rougher or smoother bore, by a power of two of its losses: the
    // pipes ring shorter or longer.
    void setDecay(float powerOfTwo) {
        const float s = std::exp2(-std::clamp(powerOfTwo, -3.0f, 3.0f));
        if (std::fabs(s - m_lossScale) < 1e-4f)
            return;
        m_lossScale = s;
        retune();
    }

    // The paddle slaps pipe n at velocity vel 0..1, u w across and along its
    // mouth (in its radii: past 1 it lands on the rim, glancing); tone −1
    // (a thick soft sole) … 1 (a hard paddle). delay: samples into the next
    // block.
    void strike(int n, float vel, float u, float w, float tone, int delay) {
        Event e;
        e.kind = Event::Strike;
        e.pipe = std::clamp(n, 0, kPipes - 1);
        e.vel = std::clamp(vel, 0.02f, 1.0f);
        e.cover = coverAt(std::sqrt(u * u + w * w));
        e.tone = std::clamp(tone, -1.0f, 1.0f);
        push(e, delay);
    }
    // The hand puts the paddle back down on pipe n's mouth, as hard as vel
    // 0..1 (a slap again, at a fraction of its pace), and holds it there.
    void hold(int n, float vel, float tone, int delay) {
        Event e;
        e.kind = Event::Hold;
        e.pipe = std::clamp(n, 0, kPipes - 1);
        e.vel = std::clamp(vel, 0.0f, 1.0f);
        e.tone = std::clamp(tone, -1.0f, 1.0f);
        push(e, delay);
    }
    // Held, the paddle tilted up off the rim to a gap of `gap` (0 shut …
    // 1 a bore's radius), as a finger slides.
    void open(int n, float gap, int delay) {
        Event e;
        e.kind = Event::Open;
        e.pipe = std::clamp(n, 0, kPipes - 1);
        e.vel = std::clamp(gap, 0.0f, 3.0f);
        push(e, delay);
    }
    // The hand lifts the paddle away.
    void lift(int n, int delay) {
        Event e;
        e.kind = Event::Lift;
        e.pipe = std::clamp(n, 0, kPipes - 1);
        push(e, delay);
    }

    // Adds the instrument's sound into l and r (n ≤ the block).
    void render(float *l, float *r, int n) {
        if (!m_awakeAny && m_eventCount == 0)
            return;
        int i = 0;
        while (i < n) {
            // Events due now take effect before this sample.
            int next = n;
            for (int k = 0; k < m_eventCount; ++k) {
                if (m_events[k].wait <= i)
                    continue;
                next = std::min(next, m_events[k].wait);
            }
            fireDue(i);
            for (; i < next; ++i)
                step(l[i], r[i]);
        }
        for (int k = 0; k < m_eventCount; ++k)
            m_events[k].wait -= n;
        // Asleep once it has rung out (and no paddle is on it).
        float total = 0;
        m_awakeAny = false;
        for (int p = 0; p < kPipes; ++p) {
            Pipe &q = m_pipe[p];
            if (!q.awake)
                continue;
            q.energy /= float(std::max(n, 1));
            total += q.energy;
            if (q.paddle.mode == Paddle::Idle && q.energy < m_sleepEnergy) {
                if ((q.quiet += n) > int(0.03f * m_rate))
                    sleep(q);
            } else {
                q.quiet = 0;
            }
            m_awakeAny |= q.awake;
            q.shown = std::max(q.energy, q.shown * m_showFall);
            q.energy = 0;
        }
        m_level = total;
        if (!m_awakeAny) {
            m_level = 0;
            m_ql = m_qr = 0;
            m_dip.reset();
        }
    }

    // A hand across the mouths: everything dies within about `seconds`.
    void mute(float seconds) {
        m_mute = std::exp(-1.0f / (std::max(seconds, 0.001f) * 0.25f * m_rate));
    }

    void silence() {
        for (auto &q : m_pipe)
            sleep(q);
        m_eventCount = 0;
        m_awakeAny = false;
        m_mute = 1.0f;
        m_ql = m_qr = 0;
        m_dip.reset();
        m_level = 0;
    }

    bool active() const { return m_awakeAny || m_eventCount > 0; }
    // Whether a hand holds a paddle on any pipe.
    bool holding() const {
        for (const auto &q : m_pipe)
            if (q.awake && q.paddle.mode == Paddle::Servo && q.paddle.goal < 0.1f)
                return true;
        return false;
    }
    // Loudness, roughly 0..1, for drawing: the whole rack, or one pipe.
    float level() const { return std::sqrt(std::max(0.0f, m_level) / m_refEnergy); }
    float level(int n) const { return std::sqrt(std::max(0.0f, m_pipe[std::clamp(n, 0, kPipes - 1)].shown) / m_refEnergy); }
    float rate() const { return m_rate; }

    // --- for tests and the docs: the instrument as built --------------------------------------------------

    // A pipe's length (m), cut so it sounds its note open at both ends.
    float length(int n) const { return m_pipe[std::clamp(n, 0, kPipes - 1)].len; }
    // The note it was cut for.
    float openHz(int n) const { return m_hz[std::clamp(n, 0, kPipes - 1)]; }
    // The textbook's pitches for a pipe this long: open at both ends,
    // c/2(L + 2·0.6133a); closed at one (a flat lid, no correction), c/4(L
    // + 0.6133a). (The model's own pitch also has the boundary layer's few
    // cents and the end correction's slow fall with frequency.)
    static float textbookOpenHz(float len) { return Physics::kC / (2.0f * (len + 2.0f * Physics::kEnd * Physics::kRadius)); }
    static float textbookClosedHz(float len) { return Physics::kC / (4.0f * (len + Physics::kEnd * Physics::kRadius)); }
    // The phase velocity in the bore at f, the boundary layer's slowing
    // and the wall's give both in it.
    static float phaseVelocity(float hz) {
        const float a = Physics::kRadius;
        const float korteweg = 1.0f / std::sqrt(1.0f + 2.0f * a * Physics::kRhoAir * Physics::kC * Physics::kC /
                                                           (Physics::kPvcE * Physics::kThick));
        return Physics::kC * korteweg * (1.0f - Physics::kWall * Physics::kC / (kTwoPi * a * std::sqrt(std::max(hz, 1.0f))));
    }
    // The effective end correction (m) of an open end at f: the model's
    // own radiation network, as it reflects.
    float endCorrection(float hz) const { return endDelta(hz); }
    // The paddle's height over pipe n's rim (m; below zero, pressing into
    // the foam), and whether a hand holds it there.
    float paddleHeight(int n) const { return m_pipe[std::clamp(n, 0, kPipes - 1)].paddle.x; }
    bool paddleHeld(int n) const { return m_pipe[std::clamp(n, 0, kPipes - 1)].paddle.mode == Paddle::Servo; }
    // How long the last strike's paddle touched the rim, in seconds.
    float lastContact() const { return m_lastContact; }
    // The highest pressure (Pa) inside any pipe's mouth since the last ask.
    float peakPressure() {
        const float p = m_peakP;
        m_peakP = 0;
        return p;
    }
    // The frequency (Hz) and decay time constant (s) of pipe n's wall mode k.
    float wallHz(int n, int k) const { return m_pipe[std::clamp(n, 0, kPipes - 1)].wall[std::clamp(k, 0, kWallModes - 1)].hz; }
    float wallTau(int n, int k) const { return m_pipe[std::clamp(n, 0, kPipes - 1)].wall[std::clamp(k, 0, kWallModes - 1)].tau; }
    int awake() const {
        int k = 0;
        for (const auto &q : m_pipe)
            k += q.awake;
        return k;
    }

private:
    struct Event {
        enum Kind : uint8_t { Strike, Hold, Open, Lift } kind = Strike;
        int pipe = 0;
        int wait = 0;
        float vel = 0, cover = 1, tone = 0;
    };

    // The hand and its flip-flop.
    struct Paddle {
        enum Mode : uint8_t { Idle, Fly, Servo } mode = Idle;
        float x = 1.0f, v = 0;   // height over the rim (m) and its rate (m/s; negative: coming down)
        float target = 0, goal = 0, slew = 0; // a holding hand: where it keeps the sole, where it is taking it, how fast (m/s)
        float tv = 0;            // how fast the hand is moving the sole now (m/s): it speeds up and slows down smoothly
        float stiff = 0, damp = 0;
        float pull = 0;          // a slapping hand: how hard it pulls back once the sole lands (m/s²)
        float back = 1.0f;       // … up to this speed (m/s): a wrist, not a catapult
        bool touched = false, away = false;
        bool stay = false;       // slapped back on: the hand keeps it there from the moment it lands
        int age = 0, contact = 0;
        float arrive = 1.0f;
        float mass = Physics::kPaddleMass, k = 1e5f, cover = 1.0f, tilt = 0.15f;
        float seal = 5e-6f;      // the gap a pressed sole still leaves round a sawn rim (m)
        float open = 0;          // the part of the mouth a glancing sole leaves uncovered (m²)
        float lgap = 0;          // the gap the air sees, log2 of metres: it seals and unseals over a fraction of a millisecond
        int held = 0;            // samples a hand has held it
        float accPrev = 0, vPrev = 0;
    };

    // A mode of the plastic: u' = d·u − c·x + f·dt²/m, x' = x + u'.
    struct WallMode {
        float hz = 500, tau = 0.02f;
        float d = 0, c = 0, inv = 0;
        float x = 0, u = 0;
        float rim = 0;      // how far it moves the rim along the pipe, per unit of x
        float side = 0;     // how far a force across the pipe drives it
        float radMouth = 0, radFar = 0; // the volume it pushes at each end, per metre per sample
    };

    // One end of a pipe: the air just outside it, ρδ/S in parallel with
    // the radiation resistance, and the gap a paddle leaves.
    struct End {
        float uL = 0, pPrev = 0;   // the radiation inertance's flow, and the pressure across it
        float flow = 0;            // out of the pipe, m³/s
    };

    struct Pipe {
        bool awake = false;
        int quiet = 0;
        float len = 1.0f;
        // Waveguide: toward the far end and back.
        float fwd[kLine] = {}, back[kLine] = {};
        int w = 0;
        int nf = 100, nb = 100;
        float apEta = 0, apX = 0, apY = 0;   // the fractional delay (Thiran)
        float dcX = 0, dcY = 0;
        float lossG = 1, lossB = 0, lossF = 0, lossK = 0; // one-way wall loss, each direction
        End mouth, far;
        Paddle paddle;
        WallMode wall[kWallModes];
        // The two ends' sound on its way to the microphones.
        float outMouth[kOut] = {}, outFar[kOut] = {};
        float dMouthL = 0, dMouthR = 0, dFarL = 0, dFarR = 0;
        float gMouthL = 0, gMouthR = 0, gFarL = 0, gFarR = 0;
        float energy = 0, shown = 0;
        float pMouth = 0; // the pressure in the mouth last sample
    };

    void push(Event e, int delay) {
        e.wait = std::max(0, delay);
        if (m_eventCount >= kEvents) {
            // Too many at once: the oldest goes now.
            fireOne(m_events[0]);
            for (int k = 1; k < m_eventCount; ++k)
                m_events[k - 1] = m_events[k];
            --m_eventCount;
        }
        m_events[m_eventCount++] = e;
    }

    void fireDue(int at) {
        int keep = 0;
        for (int k = 0; k < m_eventCount; ++k) {
            if (m_events[k].wait <= at)
                fireOne(m_events[k]);
            else
                m_events[keep++] = m_events[k];
        }
        m_eventCount = keep;
    }

    void fireOne(const Event &e) {
        Pipe &q = m_pipe[e.pipe];
        Paddle &pd = q.paddle;
        wake(q);
        m_mute = 1.0f;
        switch (e.kind) {
        case Event::Strike: {
            // A wrist flicks the sole at the mouth. It is a few millimetres
            // off when the hit is due (where it squeezes the air), lands,
            // and the hand pulls it back as it bounces.
            const float v0 = kSlowest + (kFastest - kSlowest) * std::pow(e.vel, kVelCurve);
            sole(pd, e.tone);
            pd.cover = e.cover;
            pd.tilt = Knobs::tilt + 0.6f * (1.0f - e.cover);
            pd.seal = Knobs::leak;
            // (a glancing sole leaves the rest of the mouth open beside it:
            // a crescent the air leaves by whatever the foam does)
            pd.open = (1.0f - e.cover) * m_area;
            if (pd.mode == Paddle::Idle || pd.x > 0.003f)
                pd.x = std::clamp(v0 * 0.003f, 0.0006f, 0.012f);
            pd.v = -v0;
            pd.vPrev = pd.v;
            pd.accPrev = 0;
            pd.mode = Paddle::Fly;
            pd.pull = v0 / 0.004f;
            pd.back = std::min(1.5f, 0.3f + 0.4f * v0);
            pd.stay = false;
            pd.touched = false;
            pd.age = pd.contact = 0;
            if (pd.mode == Paddle::Fly && pd.x > 0.0f)
                pd.arrive = 0.0f;
            pd.held = 0;
            break;
        }
        case Event::Hold:
            // Put down and pressed on: from wherever it is, at the pace of a
            // hand placing something, then held with a couple of newtons.
            sole(pd, e.tone);
            pd.cover = 1.0f;
            pd.tilt = Knobs::tilt;
            pd.seal = Knobs::leak;
            pd.open = 0;
            {
                // Slapped back on and kept there, as firmly as the finger
                // landed: the closed pipe gets a slap of its own. It flies
                // as a slap does, and from the moment it lands the hand
                // holds it where it is (below, in step).
                // (half a slap's pace: a "thup" on, not a second hit)
                const float pace = 0.2f + 1.8f * e.vel;
                if (pd.mode == Paddle::Servo) {
                    servo(pd, kPress, 0.05f, kGrip);
                } else {
                    if (pd.mode == Paddle::Idle) {
                        pd.x = std::clamp(pace * 0.003f, 0.0006f, 0.012f);
                        pd.arrive = 0.0f;
                    }
                    pd.v = std::min(pd.v, -pace);
                    pd.vPrev = pd.v;
                    pd.accPrev = 0;
                    pd.mode = Paddle::Fly;
                    pd.stay = true;
                    pd.touched = false;
                    pd.age = pd.contact = 0;
                }
            }
            pd.held = 0;
            break;
        case Event::Open:
            // (gap 0..1 lifts it to 0..0.4 of the bore's radius, finely at
            // first, where the note moves most)
            // (a wrist tilting the sole, slowly; the grip stays firm)
            if (pd.mode == Paddle::Servo && pd.goal < 0.1f)
                servo(pd, e.vel > 0.0f ? 0.4f * e.vel * e.vel * Physics::kRadius : kPress, 0.03f, kGrip);
            break;
        case Event::Lift:
            // Lifted off and away, as a lid is raised.
            if (pd.mode == Paddle::Servo)
                servo(pd, 0.15f, 0.6f, kGrip);
            break;
        }
    }

    // A holding hand takes the sole to `goal` at `slew` m/s, following it as
    // a spring of `stiff` N/m, critically damped or near it.
    static void servo(Paddle &pd, float goal, float slew, float stiff) {
        pd.goal = goal;
        pd.slew = slew;
        pd.stiff = stiff;
        // (a hand is a well-damped thing: it doesn't bounce the sole)
        pd.damp = 2.0f * 1.2f * std::sqrt(stiff * pd.mass);
    }

    // The sole: a hard paddle (tone 1) or a thick soft flip-flop (−1).
    static void sole(Paddle &pd, float tone) {
        const float a = Physics::kRadius + 0.5f * Physics::kThick;
        const float rim = kTwoPi * a * Physics::kThick; // the annulus the foam lands on
        pd.k = Knobs::spread * Physics::kFoamE * rim / Physics::kSoleThick * std::exp2(2.0f * tone);
        pd.mass = Physics::kPaddleMass * std::exp2(-0.5f * tone);
    }

    void wake(Pipe &q) {
        if (!q.awake) {
            q.awake = true;
            q.quiet = 0;
        }
        m_awakeAny = true;
    }

    void sleep(Pipe &q) {
        std::fill(std::begin(q.fwd), std::end(q.fwd), 0.0f);
        std::fill(std::begin(q.back), std::end(q.back), 0.0f);
        std::fill(std::begin(q.outMouth), std::end(q.outMouth), 0.0f);
        std::fill(std::begin(q.outFar), std::end(q.outFar), 0.0f);
        q.apX = q.apY = q.dcX = q.dcY = 0;
        q.lossF = q.lossK = 0;
        q.mouth = End{};
        q.far = End{};
        for (auto &m : q.wall)
            m.x = m.u = 0;
        q.paddle.mode = Paddle::Idle;
        q.paddle.x = 1.0f;
        q.paddle.v = 0;
        q.awake = false;
        q.quiet = 0;
        q.energy = q.shown = 0;
        q.pMouth = 0;
        q.paddle.arrive = 1.0f;
    }

    // How much of the mouth a paddle landing this far from its middle (in
    // radii) covers: all of it in the middle, none past the rim's far side.
    // (the sole a disc of kSoleHalf round where it lands: the part of the
    // bore's circle inside it, the two circles' lens)
    static float coverAt(float rho) {
        const float a = Physics::kRadius, R = Physics::kSoleHalf, d = std::max(rho, 0.0f) * a;
        if (d <= R - a)
            return 1.0f;
        if (d >= R + a)
            return 0.0f;
        const float ca = std::clamp((d * d + a * a - R * R) / (2.0f * d * a), -1.0f, 1.0f);
        const float cr = std::clamp((d * d + R * R - a * a) / (2.0f * d * R), -1.0f, 1.0f);
        const float lens = a * a * std::acos(ca) + R * R * std::acos(cr) -
                           0.5f * std::sqrt(std::max(0.0f, (-d + a + R) * (d + a - R) * (d - a + R) * (d + a + R)));
        return std::clamp(lens / (kPi * a * a), 0.0f, 1.0f);
    }

    // The gap between a paddle and the rim, as the air leaving through it
    // sees it: the length a pad at height h adds to an open end (Dalmont &
    // Nederveen's fit for a key over a tone hole, the wall's thickness w and
    // the pad's reach R in it), as an inertance ρ·t/S. Close to, it grows
    // as the radial sheet between two discs does (ρ/8πh); past a radius or
    // two, it is gone.
    static float gapMassOf(float h) {
        const float a = Physics::kRadius, w = Physics::kThick, R = Physics::kSoleHalf;
        const float q = std::max(h, 1e-7f) / a;
        const float t = a / (3.5f * std::pow(q, 0.8f) * std::pow(q + 3.0f * w / a, -0.4f) +
                             30.0f * std::pow(std::max(h, 1e-7f) / R, 2.6f));
        // (and never less than the radial sheet's, which it tends to as the
        // gap closes)
        // Past three radii the paddle is gone: the gap's air fades to
        // nothing there, smoothly, so nothing steps when it leaves.
        return std::max(Physics::kRhoAir * t / (kPi * a * a), Physics::kRhoAir / (8.0f * kPi * std::max(h, 1e-7f)) * std::max(0.0f, 1.0f - h / a)) *
               taper(h);
    }
    static float taper(float h) {
        const float t = std::clamp(1.0f - h / (3.0f * Physics::kRadius), 0.0f, 1.0f);
        return t * t;
    }
    // How much of the paddle's motion goes into (and pushes back from) the
    // mouth: all of it close, none once it is far.
    static float nearness(float h) {
        const float q = std::max(h, 0.0f) / (0.5f * Physics::kRadius);
        return taper(h) / (1.0f + q * q);
    }
    // A jet's loss through the gap's curtain 2πah (contracted to 0.6 of it),
    // on top of the open end's own.
    static float curtainK(float h) {
        const float a = Physics::kRadius;
        const float c = Knobs::jet * 2.0f * kPi * a * std::max(h, 1e-7f);
        return 0.5f * Physics::kRhoAir / (c * c) * taper(h);
    }
    // Both, read from tables over log h (three pow()s a sample otherwise).
    static constexpr int kGapN = 512;
    static constexpr float kGapMin = 1e-7f;
    void buildGap() {
        const float span = std::log2(3.0f * Physics::kRadius / kGapMin);
        m_gapScale = float(kGapN - 1) / span;
        for (int i = 0; i < kGapN; ++i) {
            const float h = kGapMin * std::exp2(float(i) / m_gapScale);
            m_gapM[i] = gapMassOf(h);
            m_gapK[i] = curtainK(h);
        }
        m_gapM[kGapN - 1] = m_gapK[kGapN - 1] = 0.0f;
    }
    void gapAt(float h, float &M, float &K) const {
        const float u = std::log2(std::max(h, kGapMin) / kGapMin) * m_gapScale;
        if (u >= float(kGapN - 1)) {
            M = 0;
            K = m_jetOpen;
            return;
        }
        const int i = int(u);
        const float t = u - float(i);
        M = m_gapM[i] + t * (m_gapM[i + 1] - m_gapM[i]);
        K = m_jetOpen + m_gapK[i] + t * (m_gapK[i + 1] - m_gapK[i]);
    }

    // One end, one sample: the wave `in` arriving, a volume `push` (m³/s)
    // forced into it by a paddle, the gap's inertance `M` and jet loss `K`.
    // Returns the wave going back into the pipe; e.flow is what leaves.
    float endStep(End &e, float in, float push, float M, float K) const {
        // The radiation network by the trapezoid rule: p = α·U + B.
        const float B = -m_radA * (e.uL + m_radB * e.pPrev);
        // The tube: p = 2·in + Z0·(push − U). The gap's air, M·dU/dt, by a
        // backward step on the flow (its inertance comes and goes as the
        // paddle moves: the flow carries on through the change, and nothing
        // is left ringing when it goes). Together: K|U|U + D·U = C.
        const float D = m_z0 + m_radA + M * m_rate;
        const float C = 2.0f * in + m_z0 * push - B + m_rate * M * e.flow;
        float U;
        if (K * std::fabs(C) < 1e-6f * D * D) {
            U = C / D;
        } else {
            const float s = C >= 0 ? 1.0f : -1.0f;
            U = s * (2.0f * std::fabs(C)) / (D + std::sqrt(D * D + 4.0f * K * std::fabs(C)));
        }
        const float pOut = m_radA * U + B;
        e.uL += m_radB * (pOut + e.pPrev);
        e.pPrev = pOut;
        e.flow = U;
        const float p = 2.0f * in + m_z0 * (push - U);
        return p - in;
    }

    void step(float &outL, float &outR) {
        const float dt = m_dt, rate = m_rate;
        float ql = 0, qr = 0, dip = 0;
        for (int p = 0; p < kPipes; ++p) {
            Pipe &q = m_pipe[p];
            if (!q.awake)
                continue;
            Paddle &pd = q.paddle;

            // The rim's motion along the pipe, from the plastic's modes.
            float rim = 0, rimV = 0;
            for (const auto &m : q.wall) {
                rim += m.rim * m.x;
                rimV += m.rim * m.u;
            }
            rimV *= rate;

            // The paddle: the foam against the rim, the air in the mouth
            // against its face, the hand.
            float contact = 0, push = 0, M = 0, K = m_jetOpen;
            if (pd.mode != Paddle::Idle) {
                ++pd.age;
                const float h = pd.x - rim;
                if (h < 0.0f) {
                    const float dlt = -h, rel = rimV - pd.v; // closing speed
                    // (foam stiffens as its cells close: twice as stiff
                    // every kFoamStiffen deeper)
                    // (and a sawn rim's rounded edge sinks in gently at first:
                    // over its first 0.1 mm the force grows as δ², C¹ into
                    // the line)
                    const float stiffen = (1.0f + dlt / kFoamStiffen) * (dlt < kEdge ? 0.5f * dlt / kEdge : 1.0f - 0.5f * kEdge / dlt);
                    // (EVA's hysteresis as Hunt & Crossley's: a loss that
                    // grows with the squeeze, so the force starts from
                    // nothing and never pulls)
                    contact = std::max(0.0f, pd.k * dlt * stiffen * (1.0f + Knobs::foamLoss * rel));
                    if (!pd.touched) {
                        pd.touched = true;
                        pd.contact = pd.age;
                    }
                    if (pd.mode == Paddle::Fly && pd.stay) {
                        // Landed, and the hand keeps it there: from where it
                        // is, pressing on into the foam.
                        pd.mode = Paddle::Servo;
                        pd.stay = false;
                        // (the hand's pace carries on into the hold and eases
                        // to a stop: no step in its force)
                        pd.target = pd.x;
                        pd.tv = pd.v;
                        servo(pd, kPress, 0.05f, kGrip);
                        pd.held = 0;
                    }
                    if (pd.mode == Paddle::Fly)
                        m_lastContact = float(pd.age - pd.contact) * dt;
                }
                // The air: the sole over the mouth pushes what it covers.
                // (pressed harder, the foam seals better)
                const float gap = std::max(h, 0.0f) + pd.seal / (1.0f + std::max(-h, 0.0f) / 1e-5f);
                // (it is put a few millimetres off when the hit is due; its
                // pull on the air fades in smoothly over 1.5 ms, as if it had
                // flown in from far; a sole landing on the rim beside the
                // mouth still squeezes a little air in)
                pd.arrive = std::min(1.0f, pd.arrive + m_arriveStep);
                const float in = pd.arrive * pd.arrive * (3.0f - 2.0f * pd.arrive);
                const float near = nearness(h) * (0.1f + 0.9f * pd.cover) * in;
                push = -m_area * near * (pd.v - rimV);
                // (the air sees the seal close as the film between foam and
                // rim drains and the foam conforms: an octave of gap every
                // kSealSamples at most, a third of a millisecond to seal)
                {
                    // (as fast as the foam conforms over the rim's last
                    // kConform: quick for a hard slap, slow for a soft one)
                    const float lg = std::log2(std::max(gap, kGapMin));
                    if (pd.age <= 1)
                        pd.lgap = lg;
                    // (and lets go at once: a lifting sole breaks the film)
                    const float seal = std::clamp((std::fabs(pd.v - rimV) + 0.05f) * dt / (kConform * 0.6931f), 0.02f, 0.5f);
                    pd.lgap += std::clamp(lg - pd.lgap, -seal, m_unseal);
                }
                const float gapAir = std::exp2(pd.lgap);
                gapAt(gapAir, M, K);
                if (pd.open > 0.0f) {
                    // The crescent beside a glancing sole: a hole of its own
                    // area with its own end correction, alongside the gap.
                    const float Mu = Physics::kRhoAir * 0.85f * std::sqrt(pd.open / kPi) / pd.open;
                    M = M > 0.0f ? M * Mu / (M + Mu) : 0.0f;
                    const float c = Knobs::jet * (kTwoPi * Physics::kRadius * gapAir + pd.open);
                    K = m_jetOpen + 0.5f * Physics::kRhoAir / (c * c) * taper(gapAir);
                }
                M *= in;
                K = m_jetOpen + (K - m_jetOpen) * in;
                float force = contact + q.pMouth * m_area * near;
                if (pd.mode == Paddle::Fly) {
                    if (pd.touched && pd.v < pd.back)
                        force += pd.mass * std::min(pd.pull, (pd.back - pd.v) * rate);
                } else {
                    // (a hand speeds up and slows down: its pace eases toward
                    // the goal at no more than kHandAcc, and settles in)
                    const float want = std::clamp((pd.goal - pd.target) * 100.0f, -pd.slew, pd.slew);
                    pd.tv += std::clamp(want - pd.tv, -kHandAcc * dt, kHandAcc * dt);
                    pd.target += pd.tv * dt;
                    force += pd.stiff * (pd.target - pd.x) - pd.damp * (pd.v - pd.tv);
                    // A hand doesn't hold on forever.
                    if (++pd.held > int(6.0f * rate) && pd.goal < 0.1f)
                        servo(pd, 0.15f, 0.6f, kGrip);
                }
                pd.v += force / pd.mass * dt;
                pd.x += pd.v * dt;
                // Its crack: a plate stopped dead, a dipole (jerk).
                const float acc = (pd.v - pd.vPrev) * rate;
                dip += (acc - pd.accPrev) * rate * pd.cover;
                pd.accPrev = acc;
                pd.vPrev = pd.v;
                // Gone once it is well clear, rising.
                if (pd.x > 3.0f * Physics::kRadius && pd.v > 0.0f && (pd.mode == Paddle::Fly || pd.goal > 0.1f)) {
                    pd.mode = Paddle::Idle;
                    pd.x = 1.0f;
                    pd.v = pd.vPrev = pd.accPrev = 0;
                    M = 0;
                    push = 0;
                    K = m_jetOpen;
                }
            }

            // The plastic: the foam's force on the rim (inward), a little of
            // it across the pipe.
            float wallMouth = 0, wallFar = 0;
            for (auto &m : q.wall) {
                const float f = -contact * (m.rim + pd.tilt * m.side);
                m.u = m.d * m.u - m.c * m.x + m.inv * f;
                m.x += m.u;
                wallMouth += m.radMouth * m.u;
                wallFar += m.radFar * m.u;
            }

            // The air column: the mouth, then the far end.
            const int w = q.w;
            const float inMouth = q.back[(w - q.nb) & (kLine - 1)] * m_mute;
            const float outM = endStep(q.mouth, inMouth, push, M, K);
            q.pMouth = inMouth + outM;
            // Wall loss toward the far end (one pole each way).
            q.lossF = q.lossG * outM + q.lossB * q.lossF;
            q.fwd[w] = q.lossF;
            const float inFar = q.fwd[(w - q.nf) & (kLine - 1)];
            const float outF = endStep(q.far, inFar, 0.0f, 0.0f, m_jetOpen);
            q.lossK = q.lossG * outF + q.lossB * q.lossK;
            // The fractional part of the length (first-order Thiran).
            const float y = q.apEta * q.lossK + q.apX - q.apEta * q.apY;
            q.apX = q.lossK;
            q.apY = y;
            // A steady flow straight through the pipe is no note: the
            // bore's friction and the ends' jets stop it in a fraction of a
            // second (a leak below 2 Hz).
            const float z = y - q.dcX + m_dcR * q.dcY;
            q.dcX = y;
            q.dcY = z;
            q.back[w] = z;
            q.w = (w + 1) & (kLine - 1);
            m_peakP = std::max(m_peakP, std::fabs(q.pMouth));
            // (the plastic's ringing counts too, in the pressure it would
            // make in the mouth)
            q.energy += inMouth * inMouth + inFar * inFar + (m_z0 * wallMouth) * (m_z0 * wallMouth);

            // What leaves each end (the air going out of the pipe, minus
            // what the paddle pushed in), and the plastic's share.
            const float mouthQ = q.mouth.flow - push + wallMouth;
            const float farQ = q.far.flow + wallFar;
            const int o = m_outW;
            q.outMouth[o] = mouthQ;
            q.outFar[o] = farQ;
            ql += tap(q.outMouth, q.dMouthL) * q.gMouthL + tap(q.outFar, q.dFarL) * q.gFarL;
            qr += tap(q.outMouth, q.dMouthR) * q.gMouthR + tap(q.outFar, q.dFarR) * q.gFarR;
        }
        m_outW = (m_outW + 1) & (kOut - 1);
        // Pressure follows the volume acceleration: ρ/4π · d(Q/r)/dt.
        const float gain = Knobs::level * Physics::kRhoAir / (4.0f * kPi) * rate;
        // The paddle's dipole, toned by the sole's own give.
        const float d = m_dip.step(dip * m_dipGain).low;
        outL += (ql - m_ql) * gain + d;
        outR += (qr - m_qr) * gain + d;
        m_ql = ql;
        m_qr = qr;
    }

    float tap(const float *line, float d) const {
        const int i = int(d);
        const float t = d - float(i);
        const float a = line[(m_outW - i) & (kOut - 1)], b = line[(m_outW - i - 1) & (kOut - 1)];
        return a + t * (b - a);
    }

    // The end correction (m) at f of the discrete radiation network.
    float endDelta(float hz) const {
        // The network's reflectance at f (bilinear: s = j·2fs·tan(ωT/2)).
        const float tau = Physics::kEnd * Physics::kRadius / Physics::kC;
        const float rr = 4.0f * Physics::kEnd * Physics::kEnd;
        const float wt = 2.0f * m_rate * std::tan(kPi * hz / m_rate) * tau;
        // R = (jwt(r−1) − r) / (jwt(r+1) + r)
        const float nr = -rr, ni = wt * (rr - 1.0f), dr = rr, di = wt * (rr + 1.0f);
        const float re = (nr * dr + ni * di) / (dr * dr + di * di), im = (ni * dr - nr * di) / (dr * dr + di * di);
        const float theta = kPi - std::atan2(im, re); // R = −|R|·e^{−jθ}, θ = 2kδ
        const float k = kTwoPi * hz / Physics::kC;
        return theta / (2.0f * k);
    }

    void retune() {
        const float a = Physics::kRadius, S = kPi * a * a;
        m_area = S;
        m_z0 = Physics::kRhoAir * Physics::kC / S;
        // The radiation network: inertance L = ρδ/S, resistance r·Z0 with
        // r = 4δ²/a², so its real part is (ka)²/4 · Z0 at low frequencies.
        const float L = Physics::kRhoAir * Physics::kEnd * a / S;
        const float R = 4.0f * Physics::kEnd * Physics::kEnd * m_z0;
        m_radB = m_dt / (2.0f * L);
        m_radA = R / (1.0f + R * m_radB);
        // An open end's own jet: the flow leaving a sharp-edged pipe at high
        // amplitude separates and loses ½ρu² · C of its head, C ≈ 0.6.
        m_jetOpen = 0.5f * Physics::kRhoAir * 0.6f / (S * S);
        m_dcR = 1.0f - kTwoPi * kDcHz / m_rate;
        m_arriveStep = 1.0f / (0.0015f * m_rate);
        m_unseal = 0.5f * 48000.0f / m_rate;
        buildGap();
        m_showFall = std::exp(-float(32) / (0.05f * m_rate)); // the drawing's level falls over 50 ms
        const float mic[2][3] = {{-0.35f, 0.35f, 0.9f}, {0.35f, 0.35f, 0.9f}};
        float nearest = 1e9f;
        float dist[kPipes][4];
        for (int n = 0; n < kPipes; ++n) {
            Pipe &q = m_pipe[n];
            const float f1 = m_hz[n];
            // Cut to length: the open pipe's half wave, less its two ends'
            // corrections, at the phase velocity of its note.
            const float cph = phaseVelocity(f1);
            q.len = std::max(0.05f, cph / (2.0f * f1) - 2.0f * endDelta(f1));
            // The wall's loss over one length, fitted at the note and its
            // third harmonic by one pole: g(1−b)/(1 − b·z⁻¹).
            const float K = Physics::kWall * q.len / a * m_lossScale; // Np at 1 Hz^½
            const float w1 = kTwoPi * f1 / m_rate, w3 = std::min(3.0f * w1, 2.8f);
            const float t1 = std::exp(-K * std::sqrt(f1)), t3 = std::exp(-K * std::sqrt(w3 * m_rate / kTwoPi));
            const float r2 = (t1 * t1) / (t3 * t3);
            // (1 + b²)(1 − r²) − 2b(cos w3 − r² cos w1) = 0
            const float qa = 1.0f - r2, qb = -2.0f * (std::cos(w3) - r2 * std::cos(w1));
            float b = 0;
            if (std::fabs(qa) > 1e-9f) {
                const float disc = std::max(0.0f, qb * qb - 4.0f * qa * qa);
                const float b1 = (-qb - std::sqrt(disc)) / (2.0f * qa), b2 = (-qb + std::sqrt(disc)) / (2.0f * qa);
                b = (b1 >= 0.0f && b1 < 1.0f) ? b1 : (b2 >= 0.0f && b2 < 1.0f) ? b2 : 0.0f;
            }
            b = std::clamp(b, 0.0f, 0.95f);
            const float mag1 = (1.0f - b) / std::sqrt(1.0f - 2.0f * b * std::cos(w1) + b * b);
            q.lossB = b;
            q.lossG = std::min(0.99999f, t1 / mag1) * (1.0f - b);
            const float lossDelay = std::atan2(b * std::sin(w1), 1.0f - b * std::cos(w1)) / w1;
            // The two lines and the fractional rest: the pipe's two lengths
            // of flight at the note's phase velocity, less what the loss
            // filters already delay.
            const float dcLead = std::atan(kDcHz / f1) / w1; // the leak's phase lead, in samples
            const float total = 2.0f * q.len / cph * m_rate - 2.0f * lossDelay + dcLead;
            const int nf = std::clamp(int(total * 0.5f), 1, kLine - 2);
            const int nb = std::clamp(int(total - float(nf) - 0.5f), 1, kLine - 2);
            const float frac = std::clamp(total - float(nf + nb), 0.5f, 1.5f);
            q.nf = nf;
            q.nb = nb;
            q.apEta = (1.0f - frac) / (1.0f + frac);
            // The plastic.
            buildWall(q);
            // Each end to each microphone.
            const float x = (float(n) - 4.5f) * Physics::kPitch, z = (n & 1) ? Physics::kTier : 0.0f;
            const float y = (n & 1) ? 0.06f : 0.0f;
            const float fy = y + q.len * std::cos(Physics::kSlope), fz = z - q.len * std::sin(Physics::kSlope);
            for (int e = 0; e < 2; ++e) {
                auto d3 = [&](float px, float py, float pz) {
                    return std::sqrt((px - mic[e][0]) * (px - mic[e][0]) + (py - mic[e][1]) * (py - mic[e][1]) +
                                     (pz - mic[e][2]) * (pz - mic[e][2]));
                };
                dist[n][e] = d3(x, y, z);
                dist[n][2 + e] = d3(x, fy, fz);
                nearest = std::min(nearest, dist[n][e]);
            }
        }
        for (int n = 0; n < kPipes; ++n) {
            Pipe &q = m_pipe[n];
            auto delay = [&](float d) { return std::clamp((d - nearest) / Physics::kC * m_rate, 0.0f, float(kOut - 2)); };
            q.dMouthL = delay(dist[n][0]);
            q.dMouthR = delay(dist[n][1]);
            q.dFarL = delay(dist[n][2]);
            q.dFarR = delay(dist[n][3]);
            // 1/r, at half a metre the unit.
            q.gMouthL = 0.5f / dist[n][0];
            q.gMouthR = 0.5f / dist[n][1];
            q.gFarL = 0.5f / dist[n][2];
            q.gFarR = 0.5f / dist[n][3];
        }
        // The paddle's dipole: ρ·A·d/(4πrc) · jerk, the sole's give rolling
        // it off above a couple of kilohertz.
        m_dipGain = Knobs::level * Knobs::crack * Physics::kRhoAir * Physics::kSoleArea * 0.1f /
                    (4.0f * kPi * 0.6f * Physics::kC);
        m_dip.set(2500.0f, 0.7f, m_rate);
        // A full slap of a middle pipe for the drawing's scale; asleep 80 dB
        // below it (per sample, both ends' arriving waves).
        const float pFull = Physics::kRhoAir * Physics::kC * 2.0f; // Pa, a 2 m/s piston
        m_refEnergy = pFull * pFull * 2.0f;
        m_sleepEnergy = m_refEnergy * 1e-8f;
    }

    // A pipe's plastic: two axial modes (a free bar, c = √(E/ρ)), three
    // bending modes (a free–free beam of a thin tube, I/A ≈ a²/2) and two
    // ovalling modes (a ring, n = 2 and 3).
    void buildWall(Pipe &q) {
        const float am = Physics::kRadius + 0.5f * Physics::kThick;
        const float mass = Physics::kPvcRho * kTwoPi * am * Physics::kThick * q.len;
        const float cBar = std::sqrt(Physics::kPvcE / Physics::kPvcRho);
        const float cPlate = std::sqrt(Physics::kPvcE / (Physics::kPvcRho * (1.0f - Physics::kPvcNu * Physics::kPvcNu)));
        const float ring = Physics::kThick * cPlate / (kTwoPi * std::sqrt(12.0f) * am * am);
        const float annulus = kTwoPi * am * Physics::kThick;
        const float bl[3] = {22.373f, 61.673f, 120.903f};
        for (int k = 0; k < kWallModes; ++k) {
            WallMode &m = q.wall[k];
            float modal = mass;
            m.rim = m.side = m.radMouth = m.radFar = 0;
            float eta = Physics::kPvcEta;
            if (k < 2) {
                // Axial: the rim moves along the pipe; each end's annulus
                // and the wall's Poisson breathing push a little air.
                const int nn = k + 1;
                m.hz = float(nn) * cBar / (2.0f * q.len);
                modal = 0.5f * mass;
                m.rim = 1.0f;
                const float sgn = (nn & 1) ? 1.0f : -1.0f;
                m.radMouth = Knobs::plastic * annulus;
                m.radFar = Knobs::plastic * annulus * sgn;
            } else if (k < 5) {
                // Bending: across the pipe, a dipole that hardly radiates
                // until the pipe is long against the wavelength; the rack's
                // rubber damps it.
                const float r = am / std::sqrt(2.0f);
                m.hz = bl[k - 2] / (kTwoPi * q.len * q.len) * cBar * r;
                modal = 0.25f * mass;
                m.side = 1.0f;
                const float ka = kTwoPi * m.hz / Physics::kC * am;
                m.radMouth = Knobs::plastic * 2.0f * am * q.len * 0.3f * ka;
                eta += 0.04f;
            } else {
                // Ovalling: the mouth squeezed out of round.
                const int nn = k - 3; // 2, 3
                const float fn = float(nn);
                m.hz = ring * fn * (fn * fn - 1.0f) / std::sqrt(fn * fn + 1.0f);
                modal = 0.5f * mass;
                m.side = 0.5f;
                m.rim = 0.08f;
                const float ka = kTwoPi * m.hz / Physics::kC * am;
                m.radMouth = Knobs::plastic * kTwoPi * am * am * 0.5f * ka * ka;
            }
            m.hz = std::min(m.hz, 0.45f * m_rate);
            m.tau = 1.0f / (kPi * m.hz * eta);
            const float r = std::exp(-1.0f / (m.tau * m_rate));
            const float th = kTwoPi * m.hz / m_rate;
            m.d = r * r;
            m.c = 1.0f + r * r - 2.0f * r * std::cos(th);
            m.inv = m_dt * m_dt / modal;
            // u is metres per sample: the radiated volume velocity is its
            // area times u·rate.
            m.radMouth *= m_rate;
            m.radFar *= m_rate;
        }
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    float m_hz[kPipes] = {};
    bool m_tuned = false;
    float m_lossScale = 1.0f;
    float m_area = 0, m_z0 = 1, m_radA = 0, m_radB = 0, m_jetOpen = 0, m_dcR = 1;
    static constexpr float kDcHz = 2.0f;
    static constexpr float kFoamStiffen = 0.002f; // m
    static constexpr float kEdge = 1e-4f;          // m
    // How fast the wrist brings the sole: softest to hardest (m/s), and how
    // the finger's velocity maps between them.
    static constexpr float kSlowest = 0.03f, kFastest = 4.7f, kVelCurve = 1.2f;
    // A holding hand: how far it presses the sole into the foam (m), and its
    // grip (N/m).
    static constexpr float kPress = -0.0008f, kGrip = 40000.0f;
    static constexpr float kHandAcc = 50.0f; // m/s²
    float m_gapM[kGapN] = {}, m_gapK[kGapN] = {};
    float m_gapScale = 1;
    static constexpr float kConform = 3e-4f; // m: how far the foam sinks before it seals
    float m_unseal = 0.5f;
    float m_showFall = 0.98f, m_arriveStep = 0.01f;
    Pipe m_pipe[kPipes];
    Event m_events[kEvents];
    int m_eventCount = 0;
    int m_outW = 0;
    bool m_awakeAny = false;
    float m_mute = 1.0f;
    float m_ql = 0, m_qr = 0;
    float m_dipGain = 0;
    Svf m_dip;
    float m_level = 0, m_refEnergy = 1, m_sleepEnergy = 0;
    float m_lastContact = 0, m_peakP = 0;
};

}
