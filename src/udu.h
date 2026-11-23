#pragma once

// An udu, as physics: the Igbo water-pot drum, a vessel of fired clay with
// a neck on top and a hole in its side, simulated as it is built. SI units,
// integrated sample by sample. docs/udu-research.md is the survey it is
// built from, and docs/udu.md says which measurement each constant answers.
//
//   the air     the pot's ten litres of air are one spring, ρc²/V pascals
//               for each cubic metre pushed in, and each opening holds a
//               plug of air with a mass of its own, ρ·L′/S. L′ is the neck's
//               length, or the wall's, plus an end correction at each end.
//               That makes a Helmholtz resonator with two necks side by
//               side, so their conductances add: ω² = c²/V·(S₁/L′₁ + S₂/L′₂).
//               With both open it sounds the bass, 134 Hz. A hand over the
//               side hole leaves the mouth's long narrow neck alone, an
//               octave lower; a hand over the mouth leaves the hole, a
//               major third lower. The plugs lose energy four ways:
//                 - to the air they push out (radiation, ρck²/4π, both
//                   openings into the same near field);
//                 - to the viscous and thermal layers on their walls;
//                 - to the thermal layer on the cavity's walls;
//                 - when loud, to the jet that separates at the lip
//                   (Δp ≈ ρu|u|/2C_d²), so a hard slap dies faster at first.
//               Above the Helmholtz mode the cavity has standing waves of
//               its own (a sphere's first at ka ≈ 2.08, ≈ 0.85 kHz). The
//               plugs and the clay drive them where they open onto them,
//               and a slap's pulse rings them through the openings: the
//               hollow "pok" a few dB into its attack.
//   the hand    a palm slapped over an opening is a mass on the spring of
//               its flesh (F = K·δ^1.5, lossy), stopped by the lip, which it
//               presses all round (so the ring modes round the hole feel
//               almost none of it) and damps while it lies there. Coming
//               down, it drives the air over the opening and the film it
//               squeezes off the lip into the pot: the pressure pulse that
//               makes the bass, tens of pascals soft to most of a kilopascal
//               hard. The gap it leaves round the lip is the opening's way
//               out, a thin radial channel that closes smoothly over the
//               last half millimetre. Its inertance ρ·ln(b/a)/2πg and its
//               viscous resistance 6μ·ln(b/a)/πg³ both run away as the gap
//               g closes:
//                 - a hand resting on the hole seals it (the mouth's low
//                   note);
//                 - lifting it, heel last, opens the gap through every
//                   height between, so the pitch glides up an octave as it
//                   leaves, most of the way in its first few millimetres.
//                   That is the udu's "whoop".
//               A finger resting across part of an opening shades it. The
//               open crescent conducts as (open share)^0.6 of the whole
//               (Rayleigh's 2a goes with a hole's size, not its area, and a
//               thin crescent keeps a little more), so sliding over an
//               opening bends the pitch continuously; the narrow crescent's
//               walls and edge drink the flow, so a part-covered tone dies
//               sooner.
//   the clay    the walls are fired earthenware, 9 mm thick. Its modes are
//               those measured on an udu:
//                 - a "cowbell" family round the lip of the neck, 1 : 2 : 3;
//                 - a pair round the side hole;
//                 - the belly's modes, a sphere's lower branch (membrane
//                   and bending) scaled onto the measured pair, on past its
//                   ring frequency into the dense bending modes that make
//                   a fingertip's "tk", each standing for the real modes in
//                   its third of an octave.
//               Each rings for a fraction of a second, the clay's loss
//               factor of 1.5 %. A fingertip is a Hertzian spring solved
//               against the shell's motion where it lands. Its force rings
//               the modes whose shapes are there and radiates from the wall
//               (Heckl's point-forced plate), and the skin's smack cracks
//               as fast as the contact grows. The modes radiate as a
//               vibrating sphere's do, each toward each ear with its own
//               pattern, and those that change the pot's volume squeeze
//               its air.
//   the sound   two microphones in front of the player hear the outside air
//               pushed by every part, each after its own time of flight:
//                 - the flow out of each opening;
//                 - each mode's equivalent volume velocity;
//                 - the air a palm squeezes out sideways (a dipole), and the
//                   jet hissing through a closing gap.
//               Each is differentiated, because a small source's pressure
//               follows its volume acceleration.
//
// A pot of another size is the same pot scaled: every length by s, every
// frequency by 1/s. The kit sizes it to the key (its open bass on the
// key's root), as a player picks an udu for a song.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"

namespace tf::udu {

// Where on the pot a hand lands.
enum Place : int { Hole, Mouth, Body, Neck, kPlaces };
constexpr int kOpenings = 2; // the side hole and the mouth

// --- the instrument's shape, as drawn ---------------------------------------------------------------------
//
// The pot seen from the front and a little above, in units of its belly's
// radius: X to the right, Y up, the belly's centre at the origin.
//   - The side hole is on the front of the belly, to the left.
//   - The neck rises from the top to the mouth, an ellipse seen a little
//     from above.
// The openings are drawn larger than true (the hole 0.21 of the belly's
// radius, the mouth 0.15) so a fingertip can cover one.

constexpr float kBodyRy = 0.92f;                                     // the belly, a little squat
constexpr float kHoleX = -0.40f, kHoleY = 0.12f, kHoleR = 0.32f;      // the side hole
constexpr float kNeckR = 0.30f, kNeckBase = 0.84f;                   // the neck's outside
constexpr float kMouthY = 1.40f, kMouthR = 0.30f, kMouthTilt = 0.40f; // the mouth's lip, and how flat it looks
constexpr float kHand = 0.36f;                                       // a resting hand's reach, in the drawing

// The share of an opening (radius hole) that a hand (radius hand) covers
// when its middle is dx, dy from the opening's: the area of the two
// circles' overlap over the opening's.
inline float overlap(float dx, float dy, float hand, float hole) {
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d >= hand + hole)
        return 0;
    if (d <= std::fabs(hand - hole))
        return hand >= hole ? 1.0f : (hand * hand) / (hole * hole);
    const float r2 = hand * hand, R2 = hole * hole;
    const float a = r2 * std::acos(std::clamp((d * d + r2 - R2) / (2 * d * hand), -1.0f, 1.0f));
    const float b = R2 * std::acos(std::clamp((d * d + R2 - r2) / (2 * d * hole), -1.0f, 1.0f));
    const float c = 0.5f * std::sqrt(std::max(0.0f, (-d + hand + hole) * (d + hand - hole) * (d - hand + hole) * (d + hand + hole)));
    return std::clamp((a + b - c) / (kPi * R2), 0.0f, 1.0f);
}

// How much of an opening a hand of reach `hand` resting at X, Y covers.
// The mouth is drawn foreshortened, so its covering is worked out in its
// own plane.
inline float coverOf(int opening, float X, float Y, float hand = kHand) {
    if (opening == Hole)
        return overlap(X - kHoleX, Y - kHoleY, hand, kHoleR);
    return overlap(X, (Y - kMouthY) / kMouthTilt, hand, kMouthR);
}

// The place a point of the drawing is on.
inline int placeAt(float X, float Y) {
    const float hx = (X - kHoleX) / kHoleR, hy = (Y - kHoleY) / kHoleR;
    const float mx = X / kMouthR, my = (Y - kMouthY) / (kMouthR * kMouthTilt);
    if (hx * hx + hy * hy <= 1.0f)
        return Hole;
    if (mx * mx + my * my <= 1.44f || (Y > kMouthY && std::fabs(X) < 1.2f * kMouthR))
        return Mouth;
    if (Y > kNeckBase && std::fabs(X) <= 1.15f * kNeckR)
        return Neck;
    return Body;
}

// --- the numbers ------------------------------------------------------------------------------------------
//
// SI units, for the reference pot (s = 1). Sources in docs/udu.md.

struct Physics {
    // Air at 20 °C.
    static constexpr float kRhoAir = 1.204f;   // kg/m³
    static constexpr float kC = 343.0f;        // m/s
    static constexpr float kMu = 1.81e-5f;     // Pa·s
    static constexpr float kGamma = 1.4f;
    static constexpr float kPrandtl = 0.71f;
    // Fired earthenware, open-fired at 700–900 °C as an udu is.
    static constexpr float kClay = 1900.0f;    // kg/m³
    static constexpr float kYoung = 9.0e9f;    // Pa
    static constexpr float kPoisson = 0.15f;
    static constexpr float kEta = 0.015f;      // the loss factor
    static constexpr float kThick = 0.009f;    // m, the wall
    // The reference pot: a traditional udu, its belly 29 cm across, 10.2
    // litres inside (Vieira & Aguiar's, filled with water).
    static constexpr float kRadius = 0.145f;   // m, the belly's outside radius
    static constexpr float kVolume = 0.0102f;  // m³
    // The mouth: a neck 4.4 cm across inside and 6.3 cm long. Its lip is
    // free outside (unflanged, 0.61a) and flanged by the shoulder inside.
    static constexpr float kMouthA = 0.022f, kMouthL = 0.063f;
    static constexpr float kMouthEndIn = 0.82f, kMouthEndOut = 0.61f;
    // The side hole: 6 cm across in a 12 mm wall, flanged both sides.
    static constexpr float kHoleA = 0.030f, kHoleL = 0.012f;
    static constexpr float kHoleEndIn = 0.82f, kHoleEndOut = 0.85f;
    // How far out past each opening a palm laid on it reaches, over the
    // opening's radius: the radial gap's outer radius over its inner. Over
    // the hole the palm lies on the pot's wall out to its own 6 cm; over
    // the mouth only on the neck's lip.
    static constexpr float kLipHole = 2.0f, kLipMouth = 1.36f;
    // The palm.
    static constexpr float kPalmMass = 0.4f;     // kg, the hand as it slaps
    static constexpr float kPalmFlesh = 6.0e5f;  // N/m^1.5, the palm's flesh on clay
    static constexpr float kPalmLoss = 0.25f;    // s/m, soft tissue's hysteresis (Hunt–Crossley)
    static constexpr float kFleshDamp = 600.0f;  // N·s/m, the flesh's drag on the clay under it (a palm deadens what it lies on)
    static constexpr float kLeak = 3.0e-5f;      // m, the gap left under a hand pressed firmly on (skin on unglazed clay)
};

// The model's constants that measured spectra and decays set (docs/udu.md
// says which measurement each one answers).
struct Knobs {
    static inline float radiation = 1.0f;     // the openings' radiation, × free field (a lap doubles it)
    static inline float airQ = 35.0f;         // the cavity's standing waves (the porous wall drinks them)
    static inline float jet = 1.0f;           // the separated jet's loss, × ρu|u|/2C_d²
    static inline float open = 0.6f;          // a shaded opening conducts as (open share)^open
    static inline float volumeShare = 0.03f;  // how much a clay mode changes the pot's volume
    static inline float cavityShare = 0.25f;  // how much it pushes the standing wave of its own order
    static inline float radShell = 1.0f;      // the clay's radiation, × a vibrating sphere's
    static inline float tick = 1.0f;          // the contact force's own radiation
    static inline float air = 0.4f;           // the skin's rasp on the clay
    static inline float hiss = 1.0f;          // the jet's turbulence at a closing lip
    static inline float level = 0.56f;        // pascals at a metre to the studio's full scale
    static inline float pulp = 2.0e6f, fingerMass = 0.02f, retract = 0.004f;
    static inline float rough = 0.3f;         // the skin's ridges: how much a fingertip's force frets
    static inline float grain = 1.0f;         // the crowd of clay modes above the ring frequency, rendered as their sum
    static inline float skin = 3.0e7f, pulpDepth = 0.0002f;
    static inline float dwell = 0.045f;       // s a slapping palm stays on when nothing holds it
    static inline float lift = 1.0f;          // m/s a palm leaves at, its heel first
    static inline float puff = 1.0f;          // the air squeezed out from under a palm, radiated
    static inline float smack = 1.0f;         // skin meeting clay: the slap's and the tap's crack
    static inline float hold = 25.0f;         // N the forearm presses a palm on with
};

// What a mode of the clay is.
enum Kind : uint8_t { Lip, AtHole, Belly };

// --- the instrument ---------------------------------------------------------------------------------------

class Pot {
public:
    static constexpr int kShell = 32;    // the clay's modes: 14 low, and the dense tail above the ring frequency
    static constexpr int kCoupled = 14;  // the low ones, which squeeze the air (the tail's many lobes cancel)
    static constexpr int kAir = 3;       // the cavity's standing waves (l = 1 up and across, l = 2)
    static constexpr int kContacts = 16;
    static constexpr int kCovers = 12;
    static constexpr int kEvents = 32;
    static constexpr int kDelay = 64;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        build();
        silence();
    }

    // Sizes the pot so its open bass (both openings open, the shell's give
    // and all) sounds at hz. Retuning a ringing pot would glide it, as no
    // real one could, so the kit only does it at a slap or a stroke.
    void tune(float hz) {
        hz = std::clamp(hz, 40.0f, 400.0f);
        if (std::fabs(hz - m_target) <= m_target * 1e-5f)
            return;
        m_target = hz;
        m_scale = std::clamp(m_refHeard / hz, 0.45f, 2.2f);
        retune();
    }

    // Longer (or shorter) rings, as a harder-fired (or softer) pot: the
    // clay's loss scales by a power of two.
    void setDecay(float powerOfTwo) {
        const float s = std::exp2(std::clamp(powerOfTwo, -3.0f, 3.0f));
        if (std::fabs(s - m_decayScale) < 1e-4f)
            return;
        m_decayScale = s;
        retune();
    }

    // A palm slapped over an opening.
    //   - vel: 0..1, how fast it comes.
    //   - tone: −1 (a flat, soft hand) … 1 (cupped and quick).
    //   - cover: how much of the opening the palm lands across (1 squarely).
    //   - delay: samples into the next block.
    void slap(int opening, float vel, float tone, float cover, int delay) {
        Event e;
        e.kind = Event::Slap;
        e.a = std::clamp(opening, 0, kOpenings - 1);
        e.vel = std::clamp(vel, 0.02f, 1.0f);
        e.tone = std::clamp(tone, -1.0f, 1.0f);
        e.cover = std::clamp(cover, 0.0f, 1.0f);
        post(e, delay);
    }

    // A fingertip on the clay at X, Y of the drawing (place Body or Neck).
    void strike(int place, float X, float Y, float vel, float tone, int delay) {
        Event e;
        e.kind = Event::Strike;
        e.a = place == Neck ? Neck : Body;
        e.x = X;
        e.y = Y;
        e.vel = std::clamp(vel, 0.02f, 1.0f);
        e.tone = std::clamp(tone, -1.0f, 1.0f);
        post(e, delay);
    }

    // A hand (id: whose) resting over the openings: covering share `hole`
    // of the side hole and `mouth` of the mouth, pressing 0..1. Sent again
    // as it moves; with both shares 0 it is gone.
    void cover(int id, float hole, float mouth, float press, int delay) {
        Event e;
        e.kind = Event::Cover;
        e.id = id;
        e.x = std::clamp(hole, 0.0f, 1.0f);
        e.y = std::clamp(mouth, 0.0f, 1.0f);
        e.vel = std::clamp(press, 0.0f, 1.0f);
        post(e, delay);
    }
    void uncover(int id, int delay) { cover(id, 0, 0, 0, delay); }

    // Adds the instrument's sound into l and r (n ≤ the block).
    void render(float *l, float *r, int n) {
        if (m_sleeping && m_eventCount == 0)
            return;
        for (int i = 0; i < n; ++i) {
            if (m_eventCount)
                events(i);
            if (!m_sleeping)
                step(l[i], r[i]);
        }
        for (int e = 0; e < m_eventCount; ++e)
            m_events[e].delay -= n;
        if (m_sleeping)
            return;
        // Asleep once it has rung out and no hand is moving on it.
        m_energy = energy();
        bool busy = m_touching;
        for (const auto &h : m_hand)
            busy |= h.on && (h.slapping || std::fabs(h.v) > 0.002f || !h.resting);
        if (!busy && m_energy < m_sleepEnergy) {
            if (++m_quiet > 8)
                rest();
        } else {
            m_quiet = 0;
        }
    }

    // A hand laid over the whole pot: everything dies within about `seconds`,
    // until it is next played.
    void mute(float seconds) {
        if (m_sleeping)
            return;
        m_muteTau = std::max(seconds, 0.001f) * 0.25f;
        retune();
    }

    void silence() {
        for (auto &c : m_covers)
            c = Cover{};
        m_eventCount = 0;
        for (auto &h : m_hand)
            h = Hand{};
        coverTargets();
        rest();
    }

    bool active() const { return !m_sleeping; }
    // Loudness, roughly 0..1, for drawing.
    float level() const { return std::sqrt(std::max(0.0f, m_energy) / m_refEnergy); }
    // How open an opening is now: its conductance over the bare opening's,
    // 0 (sealed) … 1.
    float openness(int opening) const {
        const int i = std::clamp(opening, 0, kOpenings - 1);
        return m_conduct[i];
    }
    float rate() const { return m_rate; }

    // --- for tests and the docs ------------------------------------------------------------------------------

    float scale() const { return m_scale; }
    float volume() const { return m_V; }
    float radius(int opening) const { return m_a[std::clamp(opening, 0, 1)]; }
    // An opening's effective length: its neck plus both end corrections.
    float neckLength(int opening) const { return m_L[std::clamp(opening, 0, 1)]; }
    // Helmholtz's formula with rigid walls, for openings shaded to open
    // shares (1 open … 0 shut, conducting as share^open).
    float helmholtzHz(float holeOpen = 1, float mouthOpen = 1) const {
        const float k = m_K[Hole] * shade(holeOpen) + m_K[Mouth] * shade(mouthOpen);
        return Physics::kC / kTwoPi * std::sqrt(k / m_V);
    }
    // The same, but with the shell's give (its modes' added compliance)
    // allowed for: where it sounds.
    float heardHz(float holeOpen = 1, float mouthOpen = 1) const { return helmholtzHz(holeOpen, mouthOpen) * m_give; }
    int shellModes() const { return kShell; }
    float shellHz(int k) const { return m_hz[std::clamp(k, 0, kShell - 1)]; }
    float shellTau(int k) const { return m_tau[std::clamp(k, 0, kShell - 1)]; }
    Kind shellKind(int k) const { return m_kind[std::clamp(k, 0, kShell - 1)]; }
    // How strongly a stroke at X, Y of the drawing reaches clay mode k.
    float shellShape(int k, int place, float X, float Y) const { return shapeAt(std::clamp(k, 0, kShell - 1), place, X, Y); }
    float airHz(int n) const { return m_airHz[std::clamp(n, 0, kAir - 1)]; }
    // The bass's Q from its linear losses (radiation, the openings' layers,
    // the cavity's walls), for openings open (1) or shut (0): its energy
    // ½Cp² against what each branch's flow p/ωM loses.
    float bassQ(bool holeOpen = true, bool mouthOpen = true) const {
        const float w = kTwoPi * heardHz(holeOpen ? 1.0f : 0.0f, mouthOpen ? 1.0f : 0.0f);
        const bool open[2] = {holeOpen, mouthOpen};
        float loss = 0, sum = 0;
        for (int i = 0; i < kOpenings; ++i)
            if (open[i]) {
                const float u = 1.0f / (w * m_M[i]);
                loss += m_Rv[i] * std::sqrt(w / m_omH) * u * u;
                sum += u;
            }
        loss += m_Rrad * (w / m_omH) * (w / m_omH) * sum * sum;
        return 1.0f / (loss / (w * m_C) + 1.0f / (m_Qth * std::sqrt(w / m_omH)));
    }
    // How much energy clay mode k holds now (J), and standing wave n.
    float shellEnergy(int k) const {
        const float v = m_u[k] * m_rate;
        return 0.5f * m_mass[k] * (v * v + m_w2[k] * m_x[k] * m_x[k]);
    }
    float airEnergy(int n) const { return 0.5f * m_C * m_kc * m_kc * (m_W[n] * m_W[n] + m_om2[n] * m_Y[n] * m_Y[n]); }
    // The cavity's own pressure now (Pa).
    float pressure() const { return m_P0; }
    // The flow out of an opening now (m³/s).
    float flow(int opening) const { return m_U[std::clamp(opening, 0, 1)]; }
    // The ring frequency of the belly (Hz).
    float ringHz() const { return m_ring; }
    // How long the last fingertip touched the clay, in seconds.
    float lastContact() const { return m_lastContact; }
    // The hand over an opening: its height over the lip (m), and whether one is there.
    float palmHeight(int opening) const { return m_hand[std::clamp(opening, 0, 1)].z; }
    bool palmOn(int opening) const { return m_hand[std::clamp(opening, 0, 1)].on; }
    // The energy in the instrument (J): air, plugs, palms and clay.
    float energy() const {
        float e = 0.5f * m_C * m_P0 * m_P0;
        for (int n = 0; n < kAir; ++n)
            e += 0.5f * m_C * m_kc * m_kc * (m_W[n] * m_W[n] + m_om2[n] * m_Y[n] * m_Y[n]);
        for (int i = 0; i < kOpenings; ++i)
            e += 0.5f * m_M[i] * m_U[i] * m_U[i];
        for (int k = 0; k < kShell; ++k) {
            const float v = m_u[k] * m_rate;
            e += 0.5f * m_mass[k] * (v * v + m_w2[k] * m_x[k] * m_x[k]);
        }
        return e;
    }

private:
    struct Event {
        enum Kind : uint8_t { Slap, Strike, Cover } kind = Slap;
        int delay = 0;
        int a = 0, id = 0;
        float x = 0, y = 0, vel = 0, tone = 0, cover = 1;
    };

    struct Cover {
        int id = -1;
        float share[kOpenings] = {};
        float press = 0;
    };

    // A palm over an opening.
    struct Hand {
        bool on = false;      // its gap is in the opening's way
        bool slapping = false;
        bool resting = false; // a hand stays on it
        bool caught = false;  // the arm has it: it has touched
        bool bottomed = false; // it has pressed in and stopped
        float z = 0, v = 0;   // its flesh's height over the lip (m, out positive), and its velocity
        float cover = 0;      // how much of the opening it spans
        float restCover = 0;  // what a resting hand spans
        float press = 0;
        int age = 0, dwell = 0;
        float flesh = Physics::kPalmFlesh;
        float tilt = 0;       // lifting, how much higher its far edge is than its heel (m)
        float shadeOf = -1, shade = 1; // (the cover its shading was last worked out for)
        float lastF = 0;
        float v0 = 0;         // the velocity it came with
        float slapCover = 0;  // how much of the opening the slapping palm spans
        float liftV = 0.5f;   // how fast it will leave
        float follow = 10.0f; // how hard the forearm presses it on (N)
        int restAge = 1 << 20; // how long a laid-on hand has been pressing on (samples)
        int liftAge = 0;
        float push = 0;       // an arm's push still under way (N), and for how many samples
        int pushLeft = 0;
    };

    struct Contact {
        bool on = false;
        int age = 0, touched = 0;
        float z = 0, v = 0;
        float mass = 0.01f, pulp = 6e5f, retract = 500.0f;
        float bone = 0, boneAt = 1;
        float pan = 0, lastF = 0;
        float phi[kShell] = {};
    };

    void post(const Event &e, int delay) {
        if (m_eventCount >= kEvents)
            return;
        Event q = e;
        q.delay = std::max(0, delay);
        m_events[m_eventCount++] = q;
    }

    // The events due at sample i of this block.
    void events(int i) {
        int keep = 0;
        const int n = m_eventCount;
        for (int k = 0; k < n; ++k) {
            const Event e = m_events[k];
            if (e.delay > i) {
                m_events[keep++] = e;
                continue;
            }
            if (m_muteTau > 0) {
                m_muteTau = 0;
                retune();
            }
            m_sleeping = false;
            m_quiet = 0;
            switch (e.kind) {
            case Event::Slap: startSlap(e); break;
            case Event::Strike: startStrike(e); break;
            case Event::Cover: setCover(e); break;
            }
        }
        m_eventCount = keep;
    }

    void setCover(const Event &e) {
        Cover *slot = nullptr;
        for (auto &c : m_covers)
            if (c.id == e.id)
                slot = &c;
        const bool gone = e.x <= 0 && e.y <= 0;
        if (!slot && !gone)
            for (auto &c : m_covers)
                if (c.id < 0) {
                    slot = &c;
                    break;
                }
        if (!slot)
            return;
        if (gone) {
            *slot = Cover{};
        } else {
            slot->id = e.id;
            slot->share[Hole] = e.x;
            slot->share[Mouth] = e.y;
            slot->press = e.vel;
        }
        coverTargets();
    }

    // Every resting hand's cover of each opening, together.
    void coverTargets() {
        for (int i = 0; i < kOpenings; ++i) {
            float c = 0, press = 0;
            for (const auto &k : m_covers)
                if (k.id >= 0 && k.share[i] > 0) {
                    c += k.share[i];
                    press = std::max(press, k.press);
                }
            Hand &h = m_hand[i];
            const bool rest = c > 0.02f;
            if (rest && !h.on) {
                // A hand comes to rest over it, from a little above.
                h = Hand{};
                h.on = true;
                // (gently: a finger laid on, not slapped; it presses on over
                // some 30 ms and, lifted, leaves slowly)
                h.z = 0.3f * m_a[i];
                h.v = h.v0 = -0.05f;
                h.flesh = fleshOn(i);
                h.liftV = 0.25f;
                h.restAge = 0;
                h.cover = std::min(c, 1.0f);
            }
            h.restCover = std::min(c, 1.0f);
            h.press = press;
            h.resting = rest;
            if (rest)
                h.follow = Knobs::hold * (0.2f + 0.8f * press);
        }
    }

    // The palm's flesh on an opening's lip: on the wall round the side hole
    // it lies on a broad face of clay; on the mouth's narrow lip it sinks
    // in more for the same push (a Hertzian contact goes as √ of its radius).
    static float fleshOn(int opening) { return Physics::kPalmFlesh * (opening == Mouth ? 0.33f : 1.0f); }

    void startSlap(const Event &e) {
        Hand &h = m_hand[e.a];
        const bool was = h.on;
        // The palm arrives from just above, faster the harder (a few cm/s to
        // 3 m/s, a hard clap's speed); a cupped hand comes quicker and
        // stiffer.
        const float v0 = (0.02f + 2.98f * std::pow(e.vel, 1.7f)) * std::exp2(0.25f * e.tone);
        if (h.on) {
            // A palm already there (down, or still near) is driven on by the
            // arm from where it is, a push over a few milliseconds: it never
            // jumps, and the air under it is never jerked.
            const float want = std::min(0.0f, -v0 - h.v);
            h.pushLeft = int(0.004f * m_rate);
            h.push = Physics::kPalmMass * want / 0.004f;
        } else {
            h.z = std::max(h.on ? h.z : 0.0f, std::clamp(v0 * 0.003f, 0.0003f, 0.4f * m_a[e.a]));
            h.v = -v0;
            h.v0 = h.v;
        }
        h.on = true;
        h.slapping = true;
        h.caught = h.caught && h.z < 0; // (a palm pressed in stays held)
        h.bottomed = h.bottomed && h.z < 0;
        h.tilt = 0;
        h.age = 0;
        // (a palm already on spreads to its new reach; a new one lands with it)
        h.slapCover = e.cover;
        if (!was)
            h.cover = std::max(e.cover, h.restCover);
        h.dwell = int(Knobs::dwell * m_rate);
        h.liftV = Knobs::lift * (0.4f + 0.8f * e.vel);
        h.follow = std::max(h.resting ? h.follow : 0.0f, Knobs::hold * (0.1f + 0.9f * e.vel));
        h.flesh = fleshOn(e.a) * std::exp2(0.8f * e.tone);
    }

    void startStrike(const Event &e) {
        Contact *c = nullptr;
        for (auto &k : m_contacts)
            if (!k.on) {
                c = &k;
                break;
            }
        if (!c) {
            c = &m_contacts[0];
            for (auto &k : m_contacts)
                if (k.age > c->age)
                    c = &k;
        }
        *c = Contact{};
        c->on = true;
        // The fingertip: the pad (soft) to the tip and nail (hard), quicker
        // and harder the harder it is played; the hand pulls it back a few
        // milliseconds after it lands.
        c->mass = Knobs::fingerMass;
        c->pulp = Knobs::pulp * std::exp2(1.5f * e.tone);
        c->v = 0.25f + 2.25f * std::pow(e.vel, 1.3f);
        c->retract = c->v / Knobs::retract;
        // Pressed far enough, the bone behind the pulp meets the clay: the
        // click of a hard stroke.
        c->bone = Knobs::skin * std::exp2(0.5f * e.tone);
        c->boneAt = Knobs::pulpDepth * std::exp2(-0.5f * e.tone);
        c->pan = std::clamp(e.x * 0.7f, -0.8f, 0.8f);
        for (int k = 0; k < kShell; ++k)
            c->phi[k] = shapeAt(k, e.a, e.x, e.y);
        m_touching = true;
    }

    // A clay mode's shape where a hand lands (1 at its antinode):
    //   - the lip's family is a ring of the neck, strongest at its free lip
    //     and dying away down into the shoulder;
    //   - the hole's pair rings round the hole;
    //   - the belly's modes are spherical harmonics over it (polar angle
    //     from the neck, azimuth from the front).
    float shapeAt(int k, int place, float X, float Y) const {
        switch (m_kind[k]) {
        case Lip:
            return lipShape(k, std::asin(std::clamp(X / (1.1f * kNeckR), -1.0f, 1.0f)), Y);
        case AtHole: {
            const float dx = X - kHoleX, dy = Y - kHoleY;
            const float d = std::sqrt(dx * dx + dy * dy) / kHoleR;
            const float ring = d < 1.0f ? d : std::exp(-(d - 1.0f) * (d - 1.0f) / 1.2f);
            const float th = std::atan2(dy, dx);
            return ring * std::cos(float(m_n[k]) * th + m_phase[k]);
        }
        default: {
            const float ct = std::clamp(Y / kBodyRy, -1.0f, 1.0f);
            const float st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
            const float sp = st > 1e-3f ? std::clamp(X / st, -1.0f, 1.0f) : 0.0f;
            const float phi = std::asin(sp);
            const float shape = legendre(m_n[k], m_m[k], ct) * std::cos(float(m_m[k]) * phi + m_phase[k]) * m_norm[k];
            return (place == Neck ? 0.25f : 1.0f) * shape;
        }
        }
    }

    // A lip mode round the neck: cos(n·ψ) round it (ψ the angle from the
    // front), strongest at the free lip and dying away into the shoulder.
    float lipShape(int k, float psi, float Y) const {
        const float up = std::clamp((Y - 0.55f) / (kMouthY - 0.55f), 0.0f, 1.0f);
        return up * up * std::cos(float(m_n[k]) * psi + m_phase[k]);
    }

    // How a mode radiates toward a direction (x right, y away from the
    // player, z up, from the pot's middle): its own angular pattern there,
    // as a vibrating sphere's mode is heard in the far field.
    float patternToward(int k, float dx, float dy, float dz) const {
        const float az = std::atan2(dx, -dy); // from the front, to the right
        switch (m_kind[k]) {
        case Lip: return std::cos(float(m_n[k]) * az + m_phase[k]);
        case AtHole: {
            // A ring pattern round the hole's axis: nothing on the axis,
            // growing as sin^n off it, signed by the side it is seen from.
            const float nx = kHoleX, nz = kHoleY, ny = -std::sqrt(std::max(0.0f, 1.0f - nx * nx - nz * nz));
            const float c = std::clamp(dx * nx + dy * ny + dz * nz, -1.0f, 1.0f);
            const float s = std::sqrt(std::max(0.0f, 1.0f - c * c));
            // (the azimuth round the axis: up the pot is β = π/2)
            const float ux = dx - c * nx, uz = dz - c * nz;
            const float beta = std::atan2(uz, ux * (nx < 0 ? -1.0f : 1.0f) + 1e-9f);
            return std::pow(s, float(m_n[k])) * std::cos(float(m_n[k]) * beta + m_phase[k]);
        }
        default:
            return legendre(m_n[k], m_m[k], dz) * std::cos(float(m_m[k]) * az + m_phase[k]) * m_norm[k];
        }
    }

    // The associated Legendre function P_n^m(x), for the belly's shapes.
    static float legendre(int n, int m, float x) {
        float pmm = 1.0f;
        const float s = std::sqrt(std::max(0.0f, 1.0f - x * x));
        for (int i = 1; i <= m; ++i)
            pmm *= -float(2 * i - 1) * s;
        if (n == m)
            return pmm;
        float pm1 = x * float(2 * m + 1) * pmm;
        if (n == m + 1)
            return pm1;
        float pn = 0;
        for (int l = m + 2; l <= n; ++l) {
            pn = (x * float(2 * l - 1) * pm1 - float(l + m - 1) * pmm) / float(l - m);
            pmm = pm1;
            pm1 = pn;
        }
        return pn;
    }

    // A shaded opening conducts as its open share^open.
    static float shade(float open) { return std::pow(std::clamp(open, 0.0f, 1.0f), Knobs::open); }

    // --- one sample -------------------------------------------------------------------------------------

    void step(float &outL, float &outR) {
        const float dt = m_dt;
        // The pressures now: each standing wave's.
        float P[kAir];
        for (int n = 0; n < kAir; ++n)
            P[n] = m_kc * m_W[n];
        // Radiation: both openings push on the same near field, a pressure
        // in phase with their summed flow's acceleration, as a resistance
        // at the bass's frequency.
        // The losses are taken at the frequency the air rings at now:
        // radiation goes as ω², the boundary layers as √ω.
        const float now = m_omNow / m_omH, layers = std::sqrt(now);
        const float prad = m_Rrad * now * now * (m_U[Hole] + m_U[Mouth]);
        // The openings: each plug of air driven by the pressure at its inner
        // end, against whatever is over its outer end.
        float hiss = 0, rim = 0, conduct = 0, smack = 0, puff = 0;
        for (int k = 0; k < kShell; ++k)
            m_damp[k] = 0;
        for (int i = 0; i < kOpenings; ++i) {
            float pin = m_P0 - prad;
            for (int n = 0; n < kAir; ++n)
                pin += m_psi[n][i] * P[n];
            Hand &h = m_hand[i];
            const float U = m_U[i];
            // The jet that separates at the lip when it is loud.
            const float Rj = Knobs::jet * Physics::kRhoAir * std::fabs(U) * m_jet[i];
            if (!h.on) {
                const float R = m_Rv[i] * layers + Rj;
                m_U[i] = (U + dt / m_M[i] * pin) / (1.0f + dt * R / m_M[i]);
                m_conduct[i] = 1.0f;
                continue;
            }
            // A hand over it. Its spread shades the opening, so the crescent
            // it leaves has an inertance of its own.
            // (a hand spreads over or slides off an opening, never jumps)
            const float reach = h.slapping ? std::max(h.slapCover, h.restCover) : h.restCover;
            if (h.resting || h.slapping)
                h.cover += (reach - h.cover) * m_coverGlide;
            const float open = 1.0f - h.cover;
            if (std::fabs(h.cover - h.shadeOf) > 1e-4f) {
                h.shadeOf = h.cover;
                h.shade = shade(open);
            }
            const float sh = h.shade;
            const float Mo = m_M[i] / std::max(sh, 1e-4f) - m_M[i];
            // The gap under the palm: a radial channel out to its edge.
            // (a hand pressed on leaves less of a gap the harder it presses:
            // a slapping one is pressed hard)
            const float press = h.slapping ? 1.0f : h.press;
            // A lifting palm tilts, its heel last: round the lip the gap runs
            // from g₀ to g₀ + tilt, so the sectors in parallel conduct as the
            // mean gap (the inertance) and the mean cube (the viscous film).
            // (flesh and an irregular clay lip seal over the last millimetre
            // of travel, not at one point: the gap closes smoothly, as a
            // softplus of the palm's height 0.4 mm wide)
            const float zs = 0.0004f * std::log1p(std::exp(std::clamp(h.z / 0.0004f, -30.0f, 30.0f)));
            const float g0 = zs + m_leak * (1.0f + 5.0f * (1.0f - press)) / (1.0f + std::max(0.0f, -h.z) / 0.0005f);
            const float gA = g0 + 0.5f * h.tilt, gB = 0.5f * h.tilt;
            const float g = gA, g3 = gA * gA * gA + 1.5f * gA * gB * gB;
            const float lnr = m_lnLip[i];
            // (the thin-channel formula holds for gaps under about half the
            // opening's radius; past its radius the palm is nearly out of the
            // flow's way, so it fades)
            const float ga = g / m_a[i];
            const float Mg = Physics::kRhoAir * lnr / (kTwoPi * g) / (1.0f + ga * ga);
            const float gapArea = kTwoPi * m_a[i] * g;
            // What the palm drives through it: the air over the opening, and,
            // while it is still coming down, the half of the film under it over
            // the lip that is squeezed inward. (Held, the flesh on the lip is
            // pinned, and only its pad over the opening faces the air.)
            if (!h.bottomed && h.z < 0 && h.v >= 0)
                h.bottomed = true; // (it has pressed in and stopped: the film under it is pinned)
            const float S = m_S[i] * h.cover * (h.bottomed || h.resting ? 1.0f : m_pump[i]);
            const float Ug = U - S * h.v;
            // (the air squeezed out from under a closing palm leaves as from a
            // sharp orifice, ρu²/2C_d²; under a lifting one it leaves through
            // its own widening channel, a radial diffuser that wins back part
            // of its speed as pressure: about half its dynamic pressure)
            const bool lifting = !h.slapping && !h.resting;
            const float Rg = 6.0f * Physics::kMu * lnr / (kPi * g3) +
                             Knobs::jet * Physics::kRhoAir * (lifting ? 0.25f : 0.5f / (0.6f * 0.6f)) * std::fabs(Ug) /
                                 (gapArea * gapArea);
            // The gap and the open crescent side by side: their impedances in
            // parallel, Z = Z₁Z₂/(Z₁ + Z₂), taken at the frequency the air
            // rings at now, as a mass and a resistance in series. (A gap
            // closed so far that it is all resistance carries nothing, and
            // leaves the crescent as it is.)
            float Mob = Mg, Rob = Rg, gapShare = 1;
            if (open > 1e-4f) {
                const float w = m_omNow;
                // The crescent's own losses: its viscous layers through the
                // wall (its width about the open share of the diameter), and
                // the jet at the finger's edge.
                const float So = std::max(open * m_S[i], 1e-7f), wid = std::max(2.0f * m_a[i] * open, 2e-4f);
                const float r1 = Physics::kRhoAir / So * m_layer * std::sqrt(w) * (m_bare[i] / wid + 2.0f) +
                                 Knobs::jet * Physics::kRhoAir * 0.5f / (0.6f * 0.6f) * std::fabs(U) / (So * So);
                const float x1 = w * Mo, r2 = Rg, x2 = w * Mg;
                const float nr = r1 * r2 - x1 * x2, ni = r1 * x2 + x1 * r2;
                const float dr = r1 + r2, di = x1 + x2;
                const float d2 = dr * dr + di * di;
                Rob = (nr * dr + ni * di) / d2;
                Mob = (ni * dr - nr * di) / d2 / w;
                // (the share of the flow the gap carries: |Z₁|/|Z₁ + Z₂|)
                gapShare = std::sqrt((r1 * r1 + x1 * x1) / d2);
            }
            // The palm's other forces: its flesh on the lip (lossy), and the arm.
            float Frim = 0;
            if (h.z < 0) {
                const float d = -h.z;
                // (Hunt–Crossley going in; coming back out the flesh gives
                // back little: soft tissue's restitution is low)
                Frim = h.flesh * d * std::sqrt(d) *
                       std::max(0.0f, h.v < 0 ? 1.0f - Physics::kPalmLoss * h.v : 1.0f - 4.0f * h.v);
                // The flesh on the clay drags on what rings under it.
                for (int k = 0; k < kShell; ++k)
                    m_damp[k] += Physics::kFleshDamp * h.cover * m_rimPhi2[i][k];
            }
            const float F = Frim + arm(h);
            // The plug, the gap and the palm, solved together and implicitly
            // in the gap's resistance, which runs away as it closes:
            //   [Mi + Ri·dt + G   −S·G    ] [U̇]   [pin − Ri·U − Rob·Ug]
            //   [−S·G             m + S²·G] [v̇] = [S·Rob·Ug + F       ],  G = Mob + Rob·dt.
            const float G = Mob + Rob * dt;
            const float Mi = m_M[i], Ri = m_Rv[i] * layers + Rj;
            const float a11 = Mi + Ri * dt + G, a12 = -S * G, a22 = Physics::kPalmMass + S * S * G;
            const float b1 = pin - Ri * U - Rob * Ug, b2 = S * Rob * Ug + F;
            const float det = a11 * a22 - a12 * a12;
            m_U[i] = U + dt * (b1 * a22 - a12 * b2) / det;
            const float dv = dt * (a11 * b2 - a12 * b1) / det;
            h.v += dv;
            h.z += dt * h.v;
            // The flesh's force, pressing all round the lip (the heel a
            // little harder), rings the clay there; how fast it grows is the
            // skin's smack.
            if (Frim > 0) {
                rim += Frim;
                for (int k = 0; k < kShell; ++k)
                    m_force[k] -= m_rimPhi[i][k] * Frim;
            }
            // (a broad soft palm rolls onto the clay: far less noise for its
            // force than a fingertip's small hard contact)
            smack += 0.2f * std::max(0.0f, Frim - h.lastF);
            h.lastF = Frim;
            // The air squeezed out sideways from under it, and back in as it
            // lifts: a puff from the lip.
            // (its rate: the air ahead of an arriving palm is already on its
            // way, and goes on its way when the palm is gone)
            puff -= S * dv;
            // The jet hissing through a closing gap.
            // (what the gap carries, at most a good fraction of the speed of
            // sound)
            const float ug = std::clamp((m_U[i] * gapShare - S * h.v) / gapArea, -120.0f, 120.0f);
            hiss += ug * std::fabs(ug) * std::min(1.0f, g / (0.15f * m_a[i]));
            m_conduct[i] = Mi / (Mi + Mob);
            conduct += (1.0f - m_conduct[i]) * m_K[i];
            ++h.age;
            // Gone once it is well clear and moving away.
            if (!h.resting && !h.slapping && h.z > 2.0f * m_a[i] && h.v > 0) {
                h = Hand{};
                m_conduct[i] = 1.0f;
            }
        }
        // (where the air rings now, for the next sample's crescents)
        m_omNow = Physics::kC * std::sqrt(std::max(m_K[0] + m_K[1] - conduct, 0.02f * m_K[1]) / m_V);
        // The clay: each mode pushed by the pressures, the hands and the
        // fingers on it.
        // (A palm's force is spread over a patch wider than the clay's
        // bending wavelength above about 600 Hz, so it radiates through the
        // clay's modes, not as a point's tick: only fingertips tick.)
        float tickL = 0, tickR = 0, rasp = 0, grain = 0;
        (void)rim;
        if (m_touching)
            contacts(tickL, tickR, rasp, smack, grain);
        // (in straight passes over the modes, which the compiler vectorises;
        // only the low modes, which the air feels, couple to it)
        float shellVol = 0, qL = 0, qR = 0, sv[kAir] = {};
        {
            const float rate = m_rate, P0 = m_P0;
            float *x = m_x, *u = m_u, *f = m_force;
            for (int k = 0; k < kShell; ++k)
                f[k] -= m_damp[k] * u[k] * rate;
            for (int k = 0; k < kCoupled; ++k)
                f[k] += m_A[k] * P0 + m_Bt[0][k] * P[0] + m_Bt[1][k] * P[1] + m_Bt[2][k] * P[2];
            for (int k = 0; k < kShell; ++k) {
                u[k] = m_d[k] * u[k] - m_c[k] * x[k] + m_inv[k] * f[k];
                x[k] += u[k];
                f[k] = 0;
            }
            for (int k = 0; k < kShell; ++k) {
                qL += m_gL[k] * u[k];
                qR += m_gR[k] * u[k];
            }
            for (int k = 0; k < kCoupled; ++k) {
                shellVol += m_A[k] * u[k];
                sv[0] += m_Bt[0][k] * u[k];
                sv[1] += m_Bt[1][k] * u[k];
                sv[2] += m_Bt[2][k] * u[k];
            }
        }
        shellVol *= m_rate; // m³/s
        // The air: its pressure from the flows out and the clay's give; the
        // thermal layer on the cavity's walls.
        m_P0 = (1.0f - m_wallLoss * layers) * m_P0 - m_kc * dt * (m_U[Hole] + m_U[Mouth] + shellVol);
        for (int n = 0; n < kAir; ++n) {
            float drive = 0;
            for (int i = 0; i < kOpenings; ++i)
                drive -= m_psi[n][i] * m_U[i];
            drive -= sv[n] * m_rate;
            m_W[n] = m_airD[n] * m_W[n] + dt * (drive - m_om2[n] * m_Y[n]);
            m_Y[n] += dt * m_W[n];
        }
        // What reaches each microphone: each opening's flow after its time
        // of flight, and the clay's; differentiated.
        m_wr = (m_wr + 1) & (kDelay - 1);
        m_line[0][m_wr] = m_U[Hole];
        m_line[1][m_wr] = m_U[Mouth];
        const float sL = tap(m_line[0], m_delay[0][0]) * m_mic[0][0] + tap(m_line[1], m_delay[1][0]) * m_mic[1][0] + qL * m_rate;
        const float sR = tap(m_line[0], m_delay[0][1]) * m_mic[0][1] + tap(m_line[1], m_delay[1][1]) * m_mic[1][1] + qR * m_rate;
        const float air = m_rasp.step(rasp * m_tickGain * Knobs::air).high;
        const float jet = m_hiss.step(hiss * m_noise.bipolar()).band * m_hissGain;
        // The skin's smack: noise as fast as the contact grows, dying in a
        // few milliseconds, in the band skin on clay rings (1–5 kHz).
        m_smackEnv = std::max(m_smackEnv * m_smackFall, smack * m_smackGain);
        m_smackLp += m_smackLpA * (m_smackEnv * m_noise.bipolar() - m_smackLp);
        const float crack = m_smack.step(m_smackLp).band;
        // The puff is a dipole, the hand's two faces pushing opposite ways:
        // its flow heard once more differentiated, times the hand's size over
        // c (8 cm / 343 m/s), and smoothed above 4 kHz.
        m_puffHp += m_puffA * (puff * m_rate * (0.08f / Physics::kC) * Knobs::puff - m_puffHp);
        const float dip = m_puffHp;
        // Above the ring frequency the clay's modes crowd too densely to ring
        // one by one; their sum is a noise as loud as the force that drives
        // it, dying as the clay's loss lets them there (η ≈ 2.5 %, a few
        // milliseconds at 3–6 kHz), heard a little differently at each ear.
        m_grainEnv = std::max(m_grainEnv * m_grainFall, grain * m_grainGain);
        const float gl = m_grainL.step(m_grainEnv * m_noise.bipolar()).band, gr = m_grainR.step(m_grainEnv * m_noise.bipolar()).band;
        const float tl = m_tickL.step(tickL * m_tickGain).high + air + jet + crack + gl;
        const float tr = m_tickR.step(tickR * m_tickGain).high + air + jet + crack + gr;
        const float sLp = sL + dip * 0.6f, sRp = sR + dip * 0.4f;
        outL += (sLp - m_sL) * m_gain + tl;
        outR += (sRp - m_sR) * m_gain + tr;
        m_sL = sLp;
        m_sR = sRp;
    }

    // The arm on a palm. A slap's palm flies on its own until it has pressed
    // in and come back, or its dwell is up. Then it stays pressed on if a
    // hand rests there, or lifts away.
    float arm(Hand &h) {
        float pushing = 0;
        if (h.pushLeft > 0) {
            --h.pushLeft;
            pushing = h.push;
        }
        return pushing + armHold(h);
    }
    float armHold(Hand &h) {
        // The forearm follows through behind the palm: from the moment it
        // touches, it presses on (Knobs::hold) and its own mass and muscle
        // drag on any rebound, so the palm stays on the lip, sealed, through
        // the dwell. A resting hand presses the same.
        if (!h.caught && h.z < 0)
            h.caught = true;
        const float holding = -h.follow - 60.0f * std::max(h.v, 0.0f);
        if (h.slapping) {
            if (h.age > h.dwell && !h.resting) {
                h.slapping = false;
                h.liftAge = 0;
            } else if (h.age > h.dwell && h.caught) {
                h.slapping = false; // it stays as a resting hand
            } else {
                return h.caught ? holding : -0.3f * h.follow;
            }
        }
        if (h.resting) {
            h.liftAge = 0;
            // (a hand laid on presses in over 60 ms, the arm easing it both
            // ways so it settles rather than rings on its flesh)
            const float on = std::min(1.0f, float(++h.restAge) / (0.06f * m_rate));
            if (h.restAge < int(0.25f * m_rate))
                return -h.follow * on - 80.0f * h.v;
            return h.caught ? -h.follow * on - 60.0f * std::max(h.v, 0.0f) : -h.follow * on - 30.0f * h.v;
        }
        // Lifting, the far edge first, the heel following (the palm tilts):
        // the arm takes 10 ms to come up to speed, a quicker lift after a
        // harder slap.
        const float t = std::min(1.0f, float(++h.liftAge) / (0.010f * m_rate));
        const float ramp = 0.5f - 0.5f * std::cos(kPi * t);
        h.tilt = std::min(h.tilt + 1.5f * h.liftV * ramp * m_dt, 3.0f * m_a[0]);
        return Physics::kPalmMass * (0.6f * h.liftV * ramp - h.v) / 0.005f;
    }

    // The fingertips touching the clay this sample.
    void contacts(float &tickL, float &tickR, float &rasp, float &smack, float &grain) {
        bool any = false;
        for (auto &c : m_contacts) {
            if (!c.on)
                continue;
            any = true;
            float surface = 0;
            for (int k = 0; k < kShell; ++k)
                surface -= c.phi[k] * m_x[k];
            // The fingertip arrives where the clay is now.
            if (c.age == 0)
                c.z = surface;
            ++c.age;
            const float delta = c.z - surface;
            float force = 0;
            if (delta > 0) {
                // The pulp, stiffening smoothly into the bone behind it as it
                // is used up.
                const float d15 = delta * std::sqrt(delta);
                const float r = delta / (delta + c.boneAt), r2 = r * r;
                force = c.pulp * d15 + c.bone * d15 * r2 * r2;
                // (the skin's ridges and the clay's grain make the force fret
                // as it presses: what rings the fine modes a smooth pulse
                // would pass by)
                const float fret = force * (1.0f + Knobs::rough * m_noise.bipolar());
                for (int k = 0; k < kShell; ++k)
                    m_force[k] -= c.phi[k] * fret;
                c.touched = c.age;
                tickL += force * std::sqrt(0.5f * (1.0f - c.pan));
                tickR += force * std::sqrt(0.5f * (1.0f + c.pan));
                rasp += force * force * (1.0f / 400.0f) * m_noise.bipolar();
                grain += force;
                smack += 0.3f * std::max(0.0f, force - c.lastF);
            }
            c.lastF = force;
            c.v -= (force / c.mass + c.retract) * m_dt;
            c.z += c.v * m_dt;
            // Gone once it is off the clay and moving away, or after 40 ms.
            if ((delta <= 0 && c.v < 0 && c.age > 4) || c.age > int(0.04f * m_rate)) {
                c.on = false;
                m_lastContact = float(c.touched) / m_rate;
            }
        }
        m_touching = any;
    }

    float tap(const float *line, float d) const {
        const int i = int(d);
        const float t = d - float(i);
        const float a = line[(m_wr - i) & (kDelay - 1)], b = line[(m_wr - i - 1) & (kDelay - 1)];
        return a + t * (b - a);
    }

    // Still: the air and the clay at rest, and a hand that rests stays.
    void rest() {
        m_P0 = 0;
        for (int n = 0; n < kAir; ++n)
            m_W[n] = m_Y[n] = 0;
        bool hands = false;
        for (int i = 0; i < kOpenings; ++i) {
            m_U[i] = 0;
            Hand &h = m_hand[i];
            if (h.resting) {
                h.slapping = false;
                h.v = 0;
                h.cover = h.restCover;
                hands = true;
            } else {
                h = Hand{};
                m_conduct[i] = 1.0f;
            }
        }
        for (int k = 0; k < kShell; ++k)
            m_x[k] = m_u[k] = m_force[k] = 0;
        for (auto &c : m_contacts)
            c.on = false;
        m_touching = false;
        for (auto &l : m_line)
            for (float &v : l)
                v = 0;
        m_sL = m_sR = 0;
        m_tickL.reset();
        m_tickR.reset();
        m_rasp.reset();
        m_hiss.reset();
        m_smack.reset();
        m_smackEnv = m_puffHp = m_puffLast = m_smackLp = m_grainEnv = 0;
        m_grainL.reset();
        m_grainR.reset();
        m_energy = 0;
        m_quiet = 0;
        // (a resting hand keeps nothing awake: it is still, and the next
        // slap or stroke finds it there)
        (void)hands;
        m_sleeping = true;
        if (m_muteTau > 0) {
            m_muteTau = 0;
            retune();
        }
    }

    // --- building it -----------------------------------------------------------------------------------

    void build() {
        // The clay's modes. Measured on an udu (Anderson, Hilton & Giorgini):
        //   - the lip's "cowbell" at 556, 1108 and 1644 Hz (1 : 1.99 : 2.96);
        //   - round the hole, 970 and 1898 Hz;
        //   - 1378 and 1470 Hz, the belly's first pair.
        // The belly's higher modes continue a sphere's lower branch from that
        // pair, up past the ring frequency where bending takes over and the
        // modes crowd (n = 8…13, to about 6 kHz): the clay's own "tk".
        struct Def {
            Kind kind;
            int n, m;
            float hz; // measured, or 0: the sphere's branch
        };
        static const Def defs[kShell] = {
            {Lip, 2, 0, 556.0f},    {Lip, 3, 0, 1108.0f},   {Lip, 4, 0, 1644.0f},  {AtHole, 2, 0, 970.0f},
            {AtHole, 3, 0, 1898.0f}, {Belly, 2, 0, 1378.0f}, {Belly, 2, 1, 1470.0f}, {Belly, 3, 1, 0.0f},
            {Belly, 3, 2, 0.0f},    {Belly, 4, 1, 0.0f},    {Belly, 4, 3, 0.0f},   {Belly, 5, 2, 0.0f},
            {Belly, 6, 3, 0.0f},    {Belly, 7, 2, 0.0f},    {Belly, 8, 1, 0.0f},   {Belly, 8, 4, 0.0f},
            {Belly, 8, 6, 0.0f},    {Belly, 9, 2, 0.0f},    {Belly, 9, 5, 0.0f},   {Belly, 9, 7, 0.0f},
            {Belly, 10, 1, 0.0f},   {Belly, 10, 4, 0.0f},   {Belly, 10, 8, 0.0f},  {Belly, 11, 3, 0.0f},
            {Belly, 11, 6, 0.0f},   {Belly, 11, 9, 0.0f},   {Belly, 12, 2, 0.0f},  {Belly, 12, 5, 0.0f},
            {Belly, 12, 10, 0.0f},  {Belly, 13, 4, 0.0f},   {Belly, 13, 7, 0.0f},  {Belly, 13, 11, 0.0f},
        };
        Rng rng(0x0d0u);
        for (int k = 0; k < kShell; ++k) {
            m_kind[k] = defs[k].kind;
            m_n[k] = defs[k].n;
            m_m[k] = defs[k].m;
            m_measured[k] = defs[k].hz;
            m_phase[k] = 0.4f * rng.bipolar();
            float peak = 1e-6f;
            for (int j = 0; j <= 64; ++j)
                peak = std::max(peak, std::fabs(legendre(m_n[k], m_m[k], -1.0f + 2.0f * float(j) / 64.0f)));
            m_norm[k] = 1.0f / peak;
            m_split[k] = 1.0f + 0.03f * rng.bipolar();
            m_volSign[k] = rng.bipolar() < 0 ? -1.0f : 1.0f;
            for (int n = 0; n < kAir; ++n)
                m_airSign[k][n] = rng.bipolar();
        }
        // Each mode heard at each microphone (50 cm in front, 35 cm apart,
        // a little above): its own pattern toward it, over that pattern's
        // root mean square over all directions, so a mode's power is its own
        // and its sign at each ear is its shape's there.
        for (int k = 0; k < kShell; ++k) {
            double sum = 0;
            int count = 0;
            for (int a = 0; a < 24; ++a)
                for (int b = 0; b < 12; ++b) {
                    const float ct = -1.0f + (float(b) + 0.5f) / 6.0f, st = std::sqrt(1.0f - ct * ct);
                    const float az = kTwoPi * float(a) / 24.0f;
                    const float v = patternToward(k, st * std::sin(az), -st * std::cos(az), ct);
                    sum += double(v) * v;
                    ++count;
                }
            const float rms = float(std::sqrt(sum / count)) + 1e-3f;
            for (int e = 0; e < 2; ++e) {
                const float mx = e == 0 ? -0.175f : 0.175f, my = -0.5f, mz = 0.1f;
                const float len = std::sqrt(mx * mx + my * my + mz * mz);
                m_dir[k][e] = patternToward(k, mx / len, my / len, mz / len) / rms;
            }
        }
        // Where a palm presses each opening's lip: all round it, the heel (the
        // side toward the player's wrist, below) a third harder; what each
        // mode feels of it, and how much of each mode the flesh lies on.
        for (int k = 0; k < kShell; ++k) {
            float sum = 0, sum2 = 0, wsum = 0;
            for (int j = 0; j < 32; ++j) {
                const float th = kTwoPi * float(j) / 32.0f;
                const float w = 1.0f + 0.3f * std::cos(th + 0.5f * kPi);
                const float v = shapeAt(k, Body, kHoleX + 1.25f * kHoleR * std::cos(th), kHoleY + 1.25f * kHoleR * std::sin(th));
                sum += w * v;
                sum2 += v * v;
                wsum += w;
            }
            m_rimPhi[Hole][k] = sum / wsum;
            m_rimPhi2[Hole][k] = sum2 / 32.0f;
            sum = sum2 = wsum = 0;
            for (int j = 0; j < 32; ++j) {
                const float psi = kTwoPi * float(j) / 32.0f;
                const float w = 1.0f + 0.3f * std::cos(psi);
                const float v = m_kind[k] == Lip ? lipShape(k, psi, kMouthY)
                                                 : shapeAt(k, Mouth, 1.1f * kNeckR * std::sin(psi), kMouthY);
                sum += w * v;
                sum2 += v * v;
                wsum += w;
            }
            m_rimPhi[Mouth][k] = sum / wsum;
            m_rimPhi2[Mouth][k] = sum2 / 32.0f;
        }
        m_scale = 1;
        retune();
        m_refHeard = heardHz();
        m_target = m_refHeard;
        // (the force's radiation as a plate's, above where the pot is small
        // against the wavelength, kR = 1, about 380 Hz)
        m_tickL.set(380.0f, 0.6f, m_rate);
        m_tickR.set(380.0f, 0.6f, m_rate);
        m_rasp.set(5000.0f, 0.6f, m_rate);
        m_hiss.set(3000.0f, 0.4f, m_rate);
        m_smack.set(2500.0f, 0.6f, m_rate);
        m_grainL.set(4200.0f, 0.5f, m_rate);
        m_grainR.set(4200.0f, 0.5f, m_rate);
        m_grainFall = std::exp(-1.0f / (0.0035f * m_rate));
        m_smackLpA = 1.0f - std::exp(-kTwoPi * 6000.0f / m_rate);
        m_smackFall = std::exp(-1.0f / (0.003f * m_rate));
        m_puffA = 1.0f - std::exp(-kTwoPi * 4000.0f / m_rate);
    }

    // A thin sphere's lower branch, Ω = ωR/c: the membrane's root, with
    // bending added (Rayleigh's sum).
    static float sphereOmega(int n, float beta) {
        const float nu = Physics::kPoisson, lam = float(n * (n + 1));
        const float B = 1.0f + 3.0f * nu + lam, C = (lam - 2.0f) * (1.0f - nu * nu);
        const float m = 0.5f * (B - std::sqrt(B * B - 4.0f * C));
        return std::sqrt(m + beta * (lam - 2.0f) * (lam - 2.0f));
    }

    // Everything that follows from the pot's size, the clay's damping and
    // the openings.
    void retune() {
        const float s = m_scale;
        const float rho = Physics::kRhoAir, c = Physics::kC;
        // The air.
        m_V = Physics::kVolume * s * s * s;
        m_C = m_V / (rho * c * c);
        m_kc = rho * c * c / m_V;
        const float A[2] = {Physics::kHoleA * s, Physics::kMouthA * s};
        const float L[2] = {Physics::kHoleL * s + (Physics::kHoleEndIn + Physics::kHoleEndOut) * A[0],
                            Physics::kMouthL * s + (Physics::kMouthEndIn + Physics::kMouthEndOut) * A[1]};
        const float bare[2] = {Physics::kHoleL * s, Physics::kMouthL * s};
        const float lip[2] = {Physics::kLipHole, Physics::kLipMouth};
        for (int i = 0; i < kOpenings; ++i) {
            m_a[i] = A[i];
            m_L[i] = L[i];
            m_S[i] = kPi * A[i] * A[i];
            m_M[i] = rho * L[i] / m_S[i];
            m_K[i] = m_S[i] / L[i];
            m_lnLip[i] = std::log(lip[i]);
            m_jet[i] = 0.5f / (0.6f * 0.6f) / (m_S[i] * m_S[i]); // ρu|u|/2C_d², per flow
            m_conduct[i] = 1.0f;
        }
        m_leak = Physics::kLeak;
        for (int i = 0; i < kOpenings; ++i) {
            m_bare[i] = bare[i];
            m_pump[i] = 1.0f + 0.25f * (lip[i] * lip[i] - 1.0f);
        }
        m_layer = std::sqrt(2.0f * Physics::kMu / rho);
        // The open bass's frequency, at which the losses are taken.
        const float omH = c * std::sqrt((m_K[0] + m_K[1]) / m_V);
        m_omH = omH;
        m_omNow = omH;
        // Each opening's viscous and thermal layers along its length and
        // round its two ends (Ingard: (ρ/S)·√(2νω)·(L/a + 2)).
        const float nu = Physics::kMu / rho;
        for (int i = 0; i < kOpenings; ++i)
            m_Rv[i] = rho / m_S[i] * std::sqrt(2.0f * nu * omH) * (bare[i] / m_a[i] + 2.0f);
        // Radiation: a small source in free air, ρck²/4π, on the summed flow.
        m_Rrad = Knobs::radiation * rho * omH * omH / (4.0f * kPi * c);
        // The thermal layer on the cavity's walls: 1/Q = (γ−1)·δ_t·A/2V.
        {
            const float ac = std::cbrt(3.0f * m_V / (4.0f * kPi));
            const float dt = std::sqrt(2.0f * nu / (Physics::kPrandtl * omH));
            m_Qth = 2.0f * m_V / ((Physics::kGamma - 1.0f) * dt * 4.0f * kPi * ac * ac);
            m_wallLoss = omH / (m_Qth * m_rate);
        }
        // The cavity's standing waves: an ellipsoid's l = 1 up (the mouth
        // above), l = 1 across (the hole on the side), and l = 2.
        {
            const float ac = std::cbrt(3.0f * m_V / (4.0f * kPi));
            const float ka[kAir] = {2.0816f * 1.04f, 2.0816f * 0.98f, 3.3421f};
            // Where each opening is: the mouth at the top (θ = 0), the hole
            // on the side (θ ≈ 85°, toward the front-left).
            const float th = 1.48f, ph = -0.6f;
            const float sur[kAir][2] = {
                {std::cos(th), 1.0f},                                      // l = 1 up
                {std::sin(th) * std::cos(ph), 0.0f},                       // l = 1 across
                {0.5f * (3.0f * std::cos(th) * std::cos(th) - 1.0f), 1.0f} // l = 2
            };
            // A rigid sphere's mode at its wall, normalised so (1/V)∫ψ² = 1:
            // A·j_l(ka) with A² = (2l+1)/(3∫₀¹j_l(ka·x)²x²dx). That is 1.93 for
            // l = 1 and 2.68 for l = 2, times the mode's angular function.
            const float wallGain[kAir] = {1.927f, 1.927f, 2.684f};
            for (int n = 0; n < kAir; ++n) {
                const float hz = ka[n] * c / (kTwoPi * ac);
                m_airHz[n] = hz;
                const float w = kTwoPi * hz;
                // (symplectic Euler: set so its poles land on the mode)
                const float wd = 2.0f * m_rate * std::sin(0.5f * w / m_rate);
                m_om2[n] = wd * wd;
                m_airD[n] = 1.0f - w / (Knobs::airQ * m_rate);
                m_psi[n][Hole] = wallGain[n] * sur[n][0];
                m_psi[n][Mouth] = wallGain[n] * sur[n][1];
            }
            // Below their own frequencies, the standing waves an opening drives
            // load it as a mass, kc·ψ²/ω². Those are the first terms of the
            // modal sum that its inner end correction is, so they are taken
            // back out of the plug and not counted twice.
            for (int i = 0; i < kOpenings; ++i) {
                float own = 0;
                for (int n = 0; n < kAir; ++n)
                    own += m_kc * m_psi[n][i] * m_psi[n][i] / std::pow(kTwoPi * m_airHz[n], 2.0f);
                m_M[i] = std::max(m_M[i] - own, 0.5f * m_M[i]);
            }
        }
        // The clay.
        const float R = Physics::kRadius * s;
        const float h = Physics::kThick * s;
        const float cp = std::sqrt(Physics::kYoung / (Physics::kClay * (1.0f - Physics::kPoisson * Physics::kPoisson)));
        m_ring = cp / (kTwoPi * R);
        const float beta = h * h / (12.0f * R * R);
        // The sphere's branch, scaled onto the measured belly pair.
        const float onto = 1378.0f / (m_ring * s * sphereOmega(2, beta));
        const float surf = 4.0f * kPi * R * R;
        const float shell = Physics::kClay * h * surf * 0.85f;
        const float neckMass = Physics::kClay * h * kTwoPi * (Physics::kMouthA * s + h) * (Physics::kMouthL * s);
        const float holePatch = Physics::kClay * h * kPi * (std::pow(2.5f * A[0], 2.0f) - A[0] * A[0]);
        for (int k = 0; k < kShell; ++k) {
            const float hz = m_kind[k] != Belly || m_measured[k] > 0 ? m_measured[k] / s
                                                                       : m_ring * sphereOmega(m_n[k], beta) * onto * m_split[k];
            m_hz[k] = std::min(hz, 0.4f * m_rate);
        }
        // A finger on the belly meets as much give as the real shell offers
        // (its driving-point mobility, which goes with how many modes crowd
        // each band): each modelled mode stands for the real ones in its
        // third of an octave, and carries only their share of the clay.
        // A plate has (S/2)·√(ρh/B) modes per hertz; a shell below its ring
        // frequency fewer, as f/f_ring.
        {
            const float B = Physics::kYoung * h * h * h / (12.0f * (1.0f - Physics::kPoisson * Physics::kPoisson));
            const float plate = 0.5f * surf * 0.85f * std::sqrt(Physics::kClay * h / B);
            for (int k = 0; k < kShell; ++k) {
                if (m_kind[k] != Belly)
                    continue;
                int here = 0;
                for (int j = 0; j < kShell; ++j)
                    here += m_kind[j] == Belly && std::fabs(std::log2(m_hz[j] / m_hz[k])) < 1.0f / 6.0f;
                const float width = m_hz[k] * (std::exp2(1.0f / 6.0f) - std::exp2(-1.0f / 6.0f));
                const float real = plate * std::min(1.0f, m_hz[k] / m_ring) * width;
                m_share[k] = std::clamp(float(here) / std::max(real, 1e-3f), 0.05f, 1.0f);
            }
        }
        for (int k = 0; k < kShell; ++k) {
            const int n = m_n[k];
            float mass, area;
            switch (m_kind[k]) {
            case Lip:
                mass = neckMass; // (the ring carries on down into the shoulder)
                area = 0.5f * kTwoPi * (Physics::kMouthA * s) * (Physics::kMouthL * s) * 2.0f;
                break;
            case AtHole:
                mass = 0.35f * holePatch;
                area = holePatch / (Physics::kClay * h);
                break;
            default:
                mass = 0.25f * shell * m_share[k];
                area = 0.25f * surf;
                break;
            }
            m_mass[k] = mass;
            // A sphere's harmonics change no volume; the pot's neck, hole and
            // base break that a little. They push the standing wave of their
            // own order (l = 2 for n = 2) most.
            m_A[k] = Knobs::volumeShare * m_volSign[k] * area / float(n);
            for (int a = 0; a < kAir; ++a) {
                const bool same = a == 2 ? n == 2 : false;
                m_B[k][a] = k < kCoupled ? Knobs::cavityShare * (same ? 1.0f : 0.15f) * m_airSign[k][a] * area / float(n) : 0.0f;
                m_Bt[a][k] = m_B[k][a];
            }
            if (k >= kCoupled)
                m_A[k] = 0;
            // The clay's loss, Q = 1/η, a little more for the modes high up,
            // which the wall radiates.
            const float eta = Physics::kEta / m_decayScale * (1.0f + 0.2f * std::pow(m_hz[k] / 2500.0f, 2.0f));
            m_tau[k] = 1.0f / (kPi * m_hz[k] * eta);
            if (m_muteTau > 0)
                m_tau[k] = std::min(m_tau[k], m_muteTau);
            const float w = kTwoPi * m_hz[k];
            // (The cavity stiffens a mode that changes its volume: taken back
            // out of its own spring, so it lands where it is tuned.)
            const float w2 = std::max(w * w - m_kc * m_A[k] * m_A[k] / m_mass[k], 1.0f);
            m_w2[k] = w2;
            const float r = std::exp(-1.0f / (m_tau[k] * m_rate));
            const float th = std::sqrt(w2) / m_rate;
            m_d[k] = r * r;
            m_c[k] = 1.0f + r * r - 2.0f * r * std::cos(th);
            m_inv[k] = m_dt * m_dt / m_mass[k];
            // Radiation as a vibrating sphere's mode of order n: efficiency
            // σ ≈ x^(2n+2)/((2n−1)!!²·(n+1)² + x^(2n+2)), x = kR, and the
            // volume velocity that radiates the same power, √(4πσS)/k per
            // m/s of the mode.
            const float kk = w / c, x = kk * R;
            float dfact = 1;
            for (int j = 2 * n - 1; j > 1; j -= 2)
                dfact *= float(j);
            const float xp = std::pow(x, float(2 * n + 2));
            const float sigma = xp / (dfact * dfact * float((n + 1) * (n + 1)) + xp);
            // (a belly mode standing for its band's real modes carries their
            // energy in its share of the mass, so it radiates from the same
            // share of the area: the band's power comes out the same)
            const float radArea = m_kind[k] == Belly ? area * m_share[k] : area;
            const float eq = Knobs::radShell * std::sqrt(4.0f * kPi * sigma * radArea) / kk;
            m_gL[k] = m_A[k] + eq * m_dir[k][0];
            m_gR[k] = m_A[k] + eq * m_dir[k][1];
        }
        // The shell's give: each mode's static compliance adds to the air's,
        // so the bass sounds a little below the rigid formula.
        {
            float add = 0;
            for (int k = 0; k < kShell; ++k)
                add += m_A[k] * m_A[k] / (m_mass[k] * m_w2[k]);
            m_give = std::sqrt(m_C / (m_C + add));
        }
        // The microphones: 50 cm in front, 35 cm apart. The hole is on the
        // left of the pot's front, the mouth at its top.
        {
            const float mic[2][3] = {{-0.175f, -0.5f, 0.1f}, {0.175f, -0.5f, 0.1f}};
            const float src[2][3] = {{kHoleX * R, -0.9f * R, kHoleY * R}, {0.0f, 0.0f, kMouthY * R}};
            float least = 1e9f, d[2][2];
            for (int i = 0; i < 2; ++i)
                for (int e = 0; e < 2; ++e) {
                    const float dx = mic[e][0] - src[i][0], dy = mic[e][1] - src[i][1], dz = mic[e][2] - src[i][2];
                    d[i][e] = std::sqrt(dx * dx + dy * dy + dz * dz);
                    least = std::min(least, d[i][e]);
                }
            for (int i = 0; i < 2; ++i)
                for (int e = 0; e < 2; ++e) {
                    m_delay[i][e] = std::clamp((d[i][e] - least) / c * m_rate, 0.0f, float(kDelay - 2));
                    m_mic[i][e] = 0.5f / d[i][e];
                }
        }
        // Pressure ∝ ρ/4πr·dQ/dt, scaled to the studio; a force on the wall
        // radiates as one on a plate of mass ρh per area (F/ρh).
        m_gain = Knobs::level * rho / (4.0f * kPi) * m_rate;
        // (Heckl: below coincidence a plate driven at a point radiates the
        // power of a monopole of volume acceleration √2·F/ρh)
        m_tickGain = Knobs::level * Knobs::tick * rho / (4.0f * kPi) / (Physics::kClay * h) * std::sqrt(2.0f);
        m_hissGain = Knobs::level * Knobs::hiss * 1e-3f;
        m_smackGain = Knobs::level * Knobs::smack * 0.2f;
        m_grainGain = Knobs::level * Knobs::grain * 6e-3f;
        m_coverGlide = 1.0f - std::exp(-1.0f / (0.005f * m_rate));
        // A firm slap's air, for the energy scale; asleep 90 dB below it.
        m_refEnergy = 0.5f * m_C * 150.0f * 150.0f;
        m_sleepEnergy = m_refEnergy * 1e-9f;
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    float m_scale = 1, m_target = 0, m_refHeard = 134, m_decayScale = 1, m_muteTau = 0;
    // The air.
    float m_V = 0.0102f, m_C = 0, m_kc = 0, m_wallLoss = 0, m_omH = 840, m_omNow = 840, m_Qth = 800;
    float m_P0 = 0;
    float m_W[kAir] = {}, m_Y[kAir] = {}, m_om2[kAir] = {}, m_airD[kAir] = {}, m_airHz[kAir] = {};
    float m_psi[kAir][kOpenings] = {};
    // The openings.
    float m_a[kOpenings] = {}, m_L[kOpenings] = {}, m_S[kOpenings] = {}, m_M[kOpenings] = {}, m_K[kOpenings] = {};
    float m_Rv[kOpenings] = {}, m_jet[kOpenings] = {}, m_lnLip[kOpenings] = {};
    float m_Rrad = 0;
    float m_U[kOpenings] = {}, m_conduct[kOpenings] = {1, 1};
    float m_leak = 1.5e-4f, m_give = 1;
    Hand m_hand[kOpenings];
    Cover m_covers[kCovers];
    float m_coverGlide = 0.004f;
    // The clay.
    int m_n[kShell] = {}, m_m[kShell] = {};
    Kind m_kind[kShell] = {};
    float m_measured[kShell] = {}, m_phase[kShell] = {}, m_norm[kShell] = {}, m_split[kShell] = {}, m_volSign[kShell] = {};
    float m_airSign[kShell][kAir] = {}, m_dir[kShell][2] = {};
    float m_hz[kShell] = {}, m_tau[kShell] = {}, m_mass[kShell] = {}, m_w2[kShell] = {}, m_share[kShell] = {};
    float m_A[kShell] = {}, m_B[kShell][kAir] = {};
    alignas(32) float m_Bt[kAir][kShell] = {};
    alignas(32) float m_x[kShell] = {}, m_u[kShell] = {}, m_force[kShell] = {};
    alignas(32) float m_d[kShell] = {}, m_c[kShell] = {}, m_inv[kShell] = {}, m_gL[kShell] = {}, m_gR[kShell] = {};
    float m_rimPhi[kOpenings][kShell] = {}, m_rimPhi2[kOpenings][kShell] = {};
    float m_damp[kShell] = {};
    float m_pump[kOpenings] = {1, 1}, m_bare[kOpenings] = {}, m_layer = 0;
    Svf m_smack;
    float m_smackLp = 0, m_smackLpA = 0.5f;
    Svf m_grainL, m_grainR;
    float m_grainEnv = 0, m_grainFall = 0.99f, m_grainGain = 0;
    float m_smackEnv = 0, m_smackFall = 0.99f, m_smackGain = 0, m_puffHp = 0, m_puffLast = 0, m_puffA = 0.4f;
    float m_ring = 2400;
    Contact m_contacts[kContacts];
    bool m_touching = false;
    float m_lastContact = 0;
    // The microphones.
    float m_line[2][kDelay] = {};
    float m_delay[2][2] = {}, m_mic[2][2] = {};
    int m_wr = 0;
    float m_sL = 0, m_sR = 0;
    float m_gain = 1, m_tickGain = 0, m_hissGain = 0;
    Svf m_tickL, m_tickR, m_rasp, m_hiss;
    Rng m_noise{0x0d0du};
    // Events, and sleep.
    Event m_events[kEvents];
    int m_eventCount = 0;
    bool m_sleeping = true;
    float m_energy = 0, m_refEnergy = 1, m_sleepEnergy = 0;
    int m_quiet = 0;
};

}
