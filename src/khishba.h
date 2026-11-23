#pragma once

// A khishba (خشبة, "a piece of wood"), as physics: the long Iraqi goblet
// drum of the chalghi and the maqam, its thin skin over a narrow bowl on a
// long hollow stem of wood, open at the foot. Not samples and not a recipe:
// the drum's own parts, in SI units, integrated sample by sample. The
// measurements it is built from are in docs/khishba-research.md, and
// docs/khishba.md says which one each constant answers to.
//
//   the skin    a circular membrane under tension T, of surface density σ,
//               glued over the bowl's mouth. Its modes are Bessel's: mode
//               (m, n) has m nodal diameters and n nodal circles, shape
//               J_m(j_mn·r/a)·cos(m(θ − θ_k)), and in a vacuum it would ring
//               at j_mn/(2πa)·√(T/σ), the inharmonic 1 : 1.59 : 2.14 : 2.30 …
//               of an ideal membrane. Air loads it: each mode carries along
//               the air it pushes on either side, about ρ/k per square metre
//               of skin for a mode of wavenumber k (the evanescent near
//               field of a wave slower than sound), and a mode that changes
//               the volume (m = 0) also a piston's radiation mass outside.
//               The low modes are slowed most, so air pulls the ratios
//               together. The skin's own loss rises with frequency; every
//               mode also loses what it radiates, from the far field of its
//               own shape (a monopole for m = 0, a dipole for m = 1 …). The
//               two of each pair with m > 0 are a few tenths of a percent
//               apart (a hand-stretched skin is never quite even), so a ring
//               beats. Stretched far, the skin's tension rises with its mean
//               strain (Kirchhoff–Carrier): a hard dum starts a little sharp.
//   the column  the air inside, from the skin down the bowl, through the
//               throat and the long stem to the open foot: Webster's horn
//               equation for this bore, solved once for its own modes. Each
//               is a tank of the Foster series (a compliance from its modal
//               volume, an inertance from its frequency), so the pressure
//               under the skin is the sum of the tanks', driven by the
//               volume the skin's m = 0 modes push in, and pushing back on
//               them. The lowest is the bowl's air on the plug in the stem,
//               a Helmholtz resonance: the dum's boom. Skin and column trade
//               energy both ways, and the skin's fundamental and the boom
//               repel each other, as a kettle's air raises a timpani's.
//               Each mode loses what its foot radiates and the stem's walls
//               take (the viscous and thermal boundary layer).
//   the wood    the body's ring of wood at the rim, a few bending modes
//               n = 2 … 5 of a thin cylindrical shell, that a finger
//               catching the rim (a tek, a ka) or a knuckle on the body
//               sets ringing: the dry click that gives the drum its name.
//   the hand    a fingertip (or three, flat, for a dum) of a few grams to a
//               few tens, arriving at up to a few metres a second, its pulp
//               a stiffening Hertzian spring with Hunt–Crossley loss, solved
//               every sample against the skin's own motion under it,
//               averaged over the patch it covers (so a flat dum reaches few
//               high modes and a fingertip at the rim many). Near the rim
//               part of the finger lands on the wood, a much stiffer spring:
//               a short, bright crack. A finger left lying on the skin is a
//               heavy, lossy point on it (the pulp's spring and dashpot, the
//               hand pressing it in): the damping that closes a stroke.
//   the sound   two microphones in front of the head, either side of its
//               axis, hear each mode's exact far field (a membrane mode's is
//               J_m(k₀a·sin θ) in angle), and the foot's volume acceleration
//               after its own time of flight. Below the boom the skin and
//               the foot push air in opposite directions and cancel (a
//               dipole), as an open-backed drum's do.
//
// Its cost does not grow with the number of fingers: the skin rings once,
// whatever strikes it, and a stroke only adds a contact for the few
// milliseconds it touches.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"

namespace tf::khishba {

// --- strokes and where they land ------------------------------------------------------------------------
//
// The head seen from above, in units of its radius: x to the right, y away
// from the player. A dum in the middle; the rim struck by the right hand's
// fingers is the tek, by the left's the ka; past the skin, the wooden body.

enum Stroke : int { Dum, Tek, Ka, Wood, kStrokes };

constexpr float kReach = 1.45f;      // how far (in head radii) the page's 0..1 spans either way of the middle
constexpr float kDumRadius = 0.56f;  // within it a stroke is a dum
constexpr float kRimFrom = 0.86f;    // a fingertip this far out starts to catch the wooden rim
constexpr float kWoodFrom = 1.12f;   // past this, off the skin: the body

inline Stroke strokeAt(float x, float y) {
    const float r = std::sqrt(x * x + y * y);
    if (r < kDumRadius)
        return Dum;
    if (r > kWoodFrom)
        return Wood;
    return x >= 0 ? Tek : Ka;
}

// Where each stroke falls when nothing says where (a key, a loop from an
// older song): a dum a little off the middle, a tek and a ka a finger's
// width in from the rim, a knock on the body below the rim.
inline void strokeSpot(Stroke s, float &x, float &y) {
    switch (s) {
    case Dum: x = 0.08f, y = -0.06f; break;
    case Tek: x = 0.93f, y = -0.14f; break;
    case Ka: x = -0.93f, y = -0.14f; break;
    default: x = 0.9f, y = -0.95f; break;
    }
}

// --- Bessel's functions ------------------------------------------------------------------------------------
//
// Computed once, building the drum (the standard library's cyl_bessel_j
// isn't in every one). J_0 … J_mMax at x by Miller's backward recurrence,
// normalised by J₀ + 2(J₂ + J₄ + …) = 1: stable for any x and order.
inline void besselAll(double x, int mMax, double *out) {
    if (x < 1e-12) {
        for (int m = 0; m <= mMax; ++m)
            out[m] = m == 0 ? 1.0 : 0.0;
        return;
    }
    int top = std::max(mMax, int(x)) + 32 + int(std::sqrt(40.0 * std::max(mMax, int(x)) + 40.0));
    top += top & 1;
    double next = 0, cur = 1e-30, sum = 0;
    double scale = 1;
    for (int m = top; m > 0; --m) {
        const double prev = 2.0 * double(m) / x * cur - next; // J_{m−1}
        next = cur;
        cur = prev;
        if (m - 1 <= mMax)
            out[m - 1] = cur;
        if ((m - 1) % 2 == 0 && m - 1 > 0)
            sum += 2.0 * cur;
        if (std::fabs(cur) > 1e200) {
            // Renormalise as it grows: everything so far by the same factor.
            next *= 1e-200, cur *= 1e-200, sum *= 1e-200, scale *= 1e-200;
            for (int j = m - 1; j <= mMax; ++j)
                out[j] *= 1e-200;
        }
    }
    sum += cur;
    (void)scale;
    for (int m = 0; m <= mMax; ++m)
        out[m] /= sum;
}

inline double besselJ(int m, double x) {
    if (m < 0)
        return (m & 1 ? -1.0 : 1.0) * besselJ(-m, x);
    double v[160];
    besselAll(std::fabs(x), std::min(m, 158), v);
    return (x < 0 && (m & 1)) ? -v[m] : v[m];
}

// J_m(z) by its power series, for the small arguments of a far field
// (k₀a ≤ about 12): cheap enough to run when the skin is retuned.
inline double besselSeries(int m, double z) {
    double term = 1;
    for (int i = 1; i <= m; ++i)
        term *= 0.5 * z / double(i);
    double sum = term;
    const double q = -0.25 * z * z;
    for (int s = 1; s < 40; ++s) {
        term *= q / (double(s) * double(s + m));
        sum += term;
        if (std::fabs(term) < 1e-14 * std::fabs(sum))
            break;
    }
    return sum;
}

// --- the numbers ------------------------------------------------------------------------------------------
//
// SI units. Sources in docs/khishba.md.

struct Physics {
    // Air.
    static constexpr float kRhoAir = 1.2f;   // kg/m³
    static constexpr float kC = 343.0f;      // m/s
    // The skin: a thin sheep or fish skin glued over a tiny head (the
    // Geidai khishbas: 9.5 cm across, 35.5 cm long), moistened before
    // playing.
    static constexpr float kHeadRadius = 0.0475f; // m
    static constexpr float kSkin = 0.17f;         // kg/m², σ: fish skin, 0.15 mm at 1150 kg/m³
    static constexpr float kTension = 1200.0f;    // N/m, T: (0,1) near 680 Hz in a vacuum
    static constexpr float kStretch = 4.5e4f;     // N/m, E·h/(1 − ν): a wetted skin's ~150 MPa × 0.17 mm
    static constexpr float kLoss = 0.028f;        // the skin's loss factor η at low frequency (Q ≈ 36) …
    static constexpr float kLossPerKHz = 0.006f;  // … rising this much per kHz (viscoelastic)
    // A carved wooden bore is rougher than the smooth tube the boundary
    // layer's formula assumes: its losses this many times the formula's.
    static constexpr float kRough = 1.5f;
    // The foot is not in free air: on the knee (Kubica) or slung against
    // the hip (Geidai) it opens a few centimetres from the player's leg or
    // body. That gap adds mass to the end (this many foot radii of end
    // correction) and a resistance (this many ρc/S of the foot's area).
    static constexpr float kFootMass = 0.5f, kFootResist = 0.07f;
    static constexpr float kSplit = 0.006f;       // how far apart a pair of the uneven skin's modes may fall
    static constexpr float kBowlDepth = 0.05f;    // m, the cup under the skin before it narrows to the neck
    // The bore, from the skin (z = 0) to the foot: radius at depth z (m).
    // A shallow cup (a third of a litre), the long neck a hand holds, a
    // little flare at the open foot.
    static constexpr int kBore = 7;
    static constexpr float kBoreZ[kBore] = {0.0f, 0.03f, 0.07f, 0.09f, 0.30f, 0.33f, 0.355f};
    static constexpr float kBoreR[kBore] = {0.0465f, 0.046f, 0.0175f, 0.0172f, 0.0175f, 0.021f, 0.025f};
    // The wood: one piece of it, its wall at the rim a thin cylindrical shell.
    static constexpr float kWoodE = 1.1e10f;   // Pa, along the grain of a fruitwood
    static constexpr float kWoodRho = 700.0f;  // kg/m³
    static constexpr float kWoodWall = 0.006f; // m
    static constexpr float kWoodQ = 30.0f;     // its own loss with the skin glued on and a hand round it
    // Where it is heard from: microphones a metre from the head, this far
    // off its axis either side; the foot is this much further away.
    static constexpr float kMicAngle = 0.85f;  // rad: slung across the body, the head faces the player's side
    // … and round the head's axis, the two microphones' bearings (rad, from
    // the tek's side of the rim): the head turned toward them, so both see
    // its tek side, apart.
    static constexpr float kMicLeft = 0.95f, kMicRight = 0.65f;
    // The wood's coincidence frequency, c²/(1.8·h·c_L): above it its wall
    // radiates as well as a piston of its size; below, little.
    static constexpr float kWoodCoincidence = 2700.0f;
    static constexpr float kMicDistance = 1.0f;
    static constexpr float kFootFurther = 0.33f;
};

// The model's constants that measured strokes set (docs/khishba.md says
// which measurement each one answers to).
struct Knobs {
    // The fingers: mass (kg), patch radius (m), pulp stiffness (N/m^1.5),
    // Hunt–Crossley loss (s/m), arrival speed at full and least velocity
    // (m/s), and how soon the hand pulls back (s).
    // A dum is three loose fingers landing side by side, a fraction of a
    // millisecond apart, each flicked from its last joint (about a third of
    // the ~3 g tip at the pad); a tek or a ka one fingertip.
    static inline float dumMass = 0.001f, dumPatch = 0.010f, dumPulp = 2.0e5f, dumLoss = 0.5f;
    static inline float dumFast = 2.6f, dumSlow = 0.25f;
    // The hand pulls back within about this long, lazily after a soft
    // stroke and whipped back after a hard one (the wrist that throws the
    // fingers snaps them away): a dum's, a tek's.
    static inline float dumBackSoft = 0.004f, dumBackHard = 0.001f, tekBackSoft = 0.003f, tekBackHard = 0.0012f;
    static inline float dumBone = 1.0e7f, dumBoneAt = 0.0003f; // the bone behind the pulp, once it is pressed this far (m)
    static inline float dumSpread = 0.34f, dumStagger = 0.0006f; // finger spacing (head radii), landing spread (s)
    static inline float tekMass = 0.0035f, tekPatch = 0.005f, tekPulp = 2.0e5f, tekLoss = 0.4f;
    static inline float tekFast = 3.0f, tekSlow = 0.45f;
    static inline float tekBone = 1.5e7f, tekBoneAt = 0.0004f;
    static inline float kaWeaker = 0.8f;   // the other hand's speed
    // How far one stroke strays from the last aimed alike (radius, angle in
    // rad, velocity and tone fractions, seconds late).
    static inline float scatterR = 0.025f, scatterTheta = 0.1f, scatterVel = 0.06f, scatterTone = 0.15f, scatterTime = 0.001f;
    // A resting finger: its mass, its pulp's spring (N/m) and dashpot
    // (N·s/m), and the hand's press at full pressure (N).
    static inline float restMass = 0.012f, restSpring = 3000.0f, restDash = 4.0f, restPress = 2.0f;
    static inline float restHand = 4.0f; // N·s/m: the hand and arm behind the finger, holding it still
    static inline float level = 0.6f; // the hacha'a groove peaking near −3 dBFS through the studio
    static inline float tick = 1.0f, rasp = 0.4f, wood = 1.0f;
    static inline float foot = 1.0f;
};

// --- the drum ---------------------------------------------------------------------------------------------

class Body {
public:
    static constexpr int kMax = 208;    // the skin's modes, pairs counted twice
    static constexpr int kAir = 8;      // the column's
    static constexpr int kWood = 8;     // the rim's (four, each a pair)
    static constexpr int kContacts = 24;
    static constexpr int kRests = 6;
    static constexpr int kTable = 65;   // radial samples of each mode's shape
    static constexpr int kAiry = 1601;  // 2J₁(x)/x on 0 … 80
    static constexpr int kDelay = 128;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        m_settle = 1.0f - std::exp(-1.0f / (0.02f * rate));
        buildTables();
        build();
        silence();
    }

    // The head's tension, in semitones from the drum as built (a player
    // warms a skin to tighten it): every mode moves with √T.
    void setTension(float semis) {
        semis = clampf(semis, -12.0f, 12.0f);
        if (std::fabs(semis - m_semis) < 1e-3f)
            return;
        m_semis = semis;
        retune();
    }

    // Longer (or shorter) rings, as a wetter or drier skin, by a power of two.
    void setDecay(float powerOfTwo) {
        const float s = std::exp2(clampf(powerOfTwo, -3.0f, 3.0f));
        if (std::fabs(s - m_decayScale) < 1e-4f)
            return;
        m_decayScale = s;
        damp();
    }

    // A stroke at x, y (head radii), velocity 0..1, tone −1 (the soft pad
    // of the finger) … 1 (the hard tip), delay samples into the next block.
    // A dum lands three fingers side by side across the stroke (the middle
    // one first, the others a fraction of a millisecond after, a little
    // softer); a tek, a ka or a knock one.
    void strike(Stroke stroke, float vel, float x, float y, float tone, int delay) {
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            damp();
        }
        // No hand lands twice alike: a few hundredths of the radius off
        // where it aimed, a few degrees round, a few per cent harder or
        // softer, the finger a little flatter or more on its tip, up to a
        // millisecond late.
        {
            const float r = std::sqrt(x * x + y * y), th = std::atan2(y, x);
            const float r2 = std::max(0.0f, r + Knobs::scatterR * m_noise.bipolar());
            const float th2 = th + Knobs::scatterTheta * m_noise.bipolar();
            x = r2 * std::cos(th2);
            y = r2 * std::sin(th2);
            vel *= 1.0f + Knobs::scatterVel * m_noise.bipolar();
            tone += Knobs::scatterTone * m_noise.bipolar();
            delay += int(Knobs::scatterTime * m_rate * m_noise.unit());
        }
        if (stroke != Dum) {
            finger(stroke, vel, x, y, tone, delay);
            return;
        }
        // The fingers lie across the line from the player to the stroke.
        const float r = std::sqrt(x * x + y * y);
        const float ux = r > 0.05f ? -y / r : 1.0f, uy = r > 0.05f ? x / r : 0.0f;
        const float d = Knobs::dumSpread;
        // (A hard dum lands its fingers closer together.)
        const int late = int(Knobs::dumStagger * m_rate * (1.3f - 0.8f * clampf(vel, 0, 1)));
        const float wobble = 0.5f + 0.5f * m_noise.bipolar();
        finger(Dum, vel, x, y, tone, delay);
        finger(Dum, vel * 0.9f, x + d * ux, y + d * uy, tone, delay + int(float(late) * (0.6f + 0.4f * wobble)));
        finger(Dum, vel * 0.85f, x - d * ux, y - d * uy, tone, delay + int(float(late) * (1.4f - 0.4f * wobble)));
    }

private:
    void finger(Stroke stroke, float vel, float x, float y, float tone, int delay) {
        Contact *c = freeContact();
        vel = clampf(vel, 0.02f, 1.0f);
        tone = clampf(tone, -1.0f, 1.0f);
        *c = Contact{};
        c->on = true;
        c->delay = std::max(0, delay);
        const float r = std::sqrt(x * x + y * y), th = std::atan2(y, x);
        float mass, patch, pulp, loss, fast, slow, back, bone, boneAt;
        if (stroke == Dum) {
            mass = Knobs::dumMass, patch = Knobs::dumPatch, pulp = Knobs::dumPulp, loss = Knobs::dumLoss;
            fast = Knobs::dumFast, slow = Knobs::dumSlow, back = -lerpf(Knobs::dumBackSoft, Knobs::dumBackHard, vel);
            bone = Knobs::dumBone, boneAt = Knobs::dumBoneAt;
        } else {
            mass = Knobs::tekMass, patch = Knobs::tekPatch, pulp = Knobs::tekPulp, loss = Knobs::tekLoss;
            fast = Knobs::tekFast * (stroke == Ka ? Knobs::kaWeaker : 1.0f), slow = Knobs::tekSlow;
            back = -lerpf(Knobs::tekBackSoft, Knobs::tekBackHard, vel);
            bone = Knobs::tekBone, boneAt = Knobs::tekBoneAt;
        }
        // Pressed far enough, the pulp is used up and the bone behind it
        // meets the skin (sooner with the tip than with the soft pad).
        c->bone = bone * std::exp2(0.5f * tone);
        c->boneAt = boneAt * std::exp2(-0.5f * tone);
        // The hard tip: stiffer, smaller, quicker; the soft pad the other way.
        pulp *= std::exp2(1.2f * tone);
        patch *= std::exp2(-0.5f * tone);
        c->mass = mass;
        c->pulp = pulp;
        c->loss = loss;
        // (A fingertip flicked at the rim speeds up more evenly with effort
        // than a dum's hand, which a soft stroke barely lifts.)
        c->v = slow + (fast - slow) * std::pow(vel, stroke == Dum ? 1.4f : 1.25f);
        // The hand pulls back: a fingertip flicked off the rim at its own
        // rate however hard it came (the contact is what the pulp, the bone
        // and the skin make of it); a dum's fingers as fast as they came.
        c->retract = back > 0 ? back : c->v / -back;
        // How much of the patch lies over the wood: none well inside, all of
        // it off the skin.
        const float b = patch / Physics::kHeadRadius;
        float wood = stroke == Wood ? 1.0f : clampf((r + b - kRimFrom) / (kWoodFrom - kRimFrom + b), 0.0f, 1.0f);
        wood = wood * wood * (3.0f - 2.0f * wood);
        // In the rim's last millimetres the skin, clamped there, can hardly
        // move under a finger: what the finger meets is the wood.
        if (stroke != Wood) {
            const float edge = clampf((r - 0.95f) / 0.05f, 0.0f, 1.0f);
            wood = std::max(wood, edge * edge * (3.0f - 2.0f * edge));
        }
        c->woodShare = wood;
        // (On the rim the wood is rigid: what gives is the finger's own pulp,
        // then its bone.)
        c->woodPulp = pulp;
        project(c->phi, std::min(r, 1.0f), th, patch);
        if (wood >= 1.0f)
            for (int k = 0; k < m_n; ++k)
                c->phi[k] = 0;
        for (int j = 0; j < kWood / 2; ++j) {
            const float n = float(j + 2);
            c->wphi[2 * j] = std::cos(n * th);
            c->wphi[2 * j + 1] = std::sin(n * th);
        }
        c->muSkin = mobility(c->phi);
        // The force radiates as from an infinite skin only where the skin
        // under it is free to move like one.
        c->tickShare = clampf(c->muSkin / std::max(m_muFree, 1e-12f), 0.0f, 1.0f);
        c->muWood = 0;
        for (int j = 0; j < kWood; ++j)
            c->muWood += c->wphi[j] * c->wphi[j] * m_winv[j];
        m_sleeping = false;
        m_touching = true;
        m_quiet = 0;
    }

public:
    // A finger laid on the skin (id: which), pressing 0..1; closed: it is
    // the hand of a closed stroke staying where it struck, already pressed
    // in, rather than a finger settling onto the skin.
    void rest(int id, float x, float y, float press, int delay, bool closed = false) {
        Rest *s = nullptr;
        for (auto &q : m_rests)
            if (q.on && q.id == id)
                s = &q;
        if (!s)
            for (auto &q : m_rests)
                if (!q.on) {
                    s = &q;
                    break;
                }
        if (!s)
            return;
        *s = Rest{};
        s->on = true;
        s->id = id;
        s->delay = std::max(0, delay);
        s->press = Knobs::restPress * clampf(0.2f + press, 0.2f, 1.2f);
        if (closed) {
            // The stroke's fingers stay flat where they struck: a dum's three
            // over a hand's width, a tek's one.
            s->pressNow = s->press;
            const bool middle = x * x + y * y < kDumRadius * kDumRadius;
            s->patch = middle ? 0.022f : 0.012f;
            s->fingers = middle ? 3.0f : 1.0f;
            s->press *= s->fingers;
        }
        place(*s, x, y);
        m_resting = true;
        if (!m_sleeping)
            m_quiet = 0;
    }

    void restMove(int id, float x, float y, float press) {
        for (auto &s : m_rests)
            if (s.on && s.id == id) {
                s.press = Knobs::restPress * clampf(0.2f + press, 0.2f, 1.2f);
                if (std::hypot(x - s.x, y - s.y) > 0.02f)
                    place(s, x, y);
            }
    }

    void lift(int id) {
        bool any = false;
        for (auto &s : m_rests) {
            if (s.on && s.id == id)
                s.on = false;
            any |= s.on;
        }
        m_resting = any;
    }

    void liftAll() {
        for (auto &s : m_rests)
            s.on = false;
        m_resting = false;
    }

    // Adds the drum's sound into l and r (n ≤ the block).
    void render(float *l, float *r, int n) {
        if (m_sleeping)
            return;
        const int N = m_n, N0 = m_n0, NA = m_nAir;
        float *x = m_x, *u = m_u, *f = m_force;
        float heard = 0;
        for (int i = 0; i < n; ++i) {
            // The volume the skin pushes into the column, and the column's
            // pressure under it, the sum of its tanks'.
            float vol = 0;
            for (int k = 0; k < N0; ++k)
                vol += m_area[k] * x[k];

            float p = 0, foot = 0;
            for (int a = 0; a < NA; ++a) {
                const float pa = (-vol - m_xi[a]) * m_invC[a];
                p += pa;
                foot += m_footGain[a] * pa;
                // (The tank's spring pulls ξ toward the volume pushed in, so
                // pressed and held still the cup's pressure is exactly nothing.)
                m_eta[a] = m_ad[a] * m_eta[a] - m_ac[a] * (m_xi[a] + vol);
            }
            for (int a = 0; a < NA; ++a)
                m_xi[a] += m_eta[a];
            p *= m_airOn;
            foot *= m_airOn;
            // The skin's strain (the tension it adds), and where each hand
            // on it finds it, in one pass.
            float strain = 0;
            for (int k = 0; k < N; ++k)
                strain += m_g[k] * x[k] * x[k];
            // Hands on it: the force each makes, from where the skin is
            // (into m_force, which the step below empties).
            float tick = 0, woodTick = 0, rasp = 0;
            if (m_touching)
                contacts(f, tick, woodTick, rasp);
            if (m_resting)
                rests(f);
            const float dT = m_stretch * strain;
            // The skin, and what it radiates.
            float qe = 0, qeR = 0, qo = 0, qoR = 0;
            for (int k = 0; k < N; ++k) {
                const float fk = f[k] - dT * m_g[k] * x[k] + m_area[k] * p;
                f[k] = 0;
                u[k] = m_d[k] * u[k] - m_c[k] * x[k] + m_inv[k] * fk;
                x[k] += u[k];
                qe += m_ge[k] * u[k];
                qeR += m_geR[k] * u[k];
                qo += m_go[k] * u[k];
                qoR += m_goR[k] * u[k];
            }
            // The rim's wood.
            float wq = 0, wqR = 0;
            for (int j = 0; j < kWood; ++j) {
                m_wu[j] = m_wd[j] * m_wu[j] - m_wc[j] * m_wx[j] + m_winv[j] * m_wf[j];
                m_wx[j] += m_wu[j];
                wq += m_wgl[j] * m_wu[j];
                wqR += m_wgr[j] * m_wu[j];
                m_wf[j] = 0;
            }
            // The foot is heard after the head, from further away.
            m_wr = (m_wr + 1) & (kDelay - 1);
            m_footLine[m_wr] = foot;
            const float footHeard = tap(m_footLine, m_footDelay) * m_footLevel;
            // Pressure follows the volume acceleration: the even modes' (and
            // the m = 0 monopoles') by differencing their volume velocity, the
            // odd ones' a quarter cycle on (their far field is in quadrature),
            // which their velocity already is.
            const float t = m_tickHp.step(tick).high * m_tickGain;
            const float wt = m_woodHp.step(woodTick).high * m_woodTickGain;
            const float air = m_raspHp.step(rasp * m_noise.bipolar()).high * m_raspGain;
            const float outL = (qe - m_qe) * m_gain + qo * m_oddGain + (wq - m_wq) * m_woodGain + footHeard + t + wt + air;
            const float outR = (qeR - m_qeR) * m_gain + qoR * m_oddGain + (wqR - m_wqR) * m_woodGain + footHeard + t + wt + air;
            heard += outL * outL + outR * outR;
            m_qe = qe;
            m_qeR = qeR;
            m_wq = wq;
            m_wqR = wqR;
            l[i] += outL;
            r[i] += outR;
        }
        // Asleep once it has rung out, 120 dB down (and no hand strikes it).
        m_energy = heard / float(2 * std::max(n, 1));
        if (!m_touching && m_energy < 1e-12f) {
            if (++m_quiet > 8)
                sleep();
        } else {
            m_quiet = 0;
        }
    }

    // A hand laid flat on it: everything dies within about `seconds`.
    void mute(float seconds) {
        if (m_sleeping)
            return;
        m_muteD = std::exp(-2.0f / (std::max(seconds, 0.001f) * 0.25f * m_rate));
        damp();
    }

    void silence() {
        liftAll();
        sleep();
    }

    bool active() const { return !m_sleeping; }
    // Loudness, roughly 0..1 (a full dum's first moments near 1), for drawing.
    float level() const { return std::sqrt(std::max(0.0f, m_energy)) * 4.0f; }
    float rate() const { return m_rate; }
    int resting() const {
        int n = 0;
        for (const auto &s : m_rests)
            n += s.on;
        return n;
    }

    // --- for tests and the docs: the drum as built -----------------------------------------------------

    int modes() const { return m_n; }
    int airModes() const { return m_nAir; }
    // A skin mode's frequency (with the air's mass on it, before the column
    // couples), its decay time constant, its (m, n).
    float modeHz(int k) const { return m_hz[std::clamp(k, 0, m_n - 1)]; }
    float modeTau(int k) const { return m_tau[std::clamp(k, 0, m_n - 1)]; }
    int modeM(int k) const { return m_m[std::clamp(k, 0, m_n - 1)]; }
    int modeN(int k) const { return m_nn[std::clamp(k, 0, m_n - 1)]; }
    // The first mode (m, n) in the list, or −1.
    int find(int m, int n) const {
        for (int k = 0; k < m_n; ++k)
            if (m_m[k] == m && m_nn[k] == n)
                return k;
        return -1;
    }
    // The skin mode's frequency in a vacuum, √(T/σ) alone.
    float vacuumHz(int k) const { return m_vacHz[std::clamp(k, 0, m_n - 1)]; }
    // The column's modes as Webster's equation gives them for a rigid
    // skin, and their Q.
    float airHz(int a) const { return m_airHz[std::clamp(a, 0, kAir - 1)]; }
    float airQ(int a) const { return m_airQ[std::clamp(a, 0, kAir - 1)]; }
    // The wood's modes.
    float woodHz(int j) const { return m_whz[std::clamp(j, 0, kWood - 1)]; }
    // How long the last stroke's finger touched, in seconds.
    float lastContact() const { return m_lastContact; }
    // The normalised coupling of the skin's (0,1) to the column's lowest
    // mode, g/(ω₁ω₂): the share of their frequencies the two would split
    // by if tuned together.
    float airCoupling() const {
        const int k = find(0, 1);
        if (k < 0 || m_nAir == 0)
            return 0;
        // Skin: stiffness K = M ω², tank: 1/C; their cross stiffness A/C.
        const float M = m_mass[k], L = m_L[0];
        const float g = m_area[k] * m_invC[0] / std::sqrt(M * L);
        return g / (kTwoPi * m_hz[k] * kTwoPi * m_airHz[0]);
    }
    // How far mode k swings now (m).
    float swing(int k) const {
        k = std::clamp(k, 0, m_n - 1);
        const float th = std::max(kTwoPi * m_hz[k] / m_rate, 1e-4f);
        return std::sqrt(m_x[k] * m_x[k] + (m_u[k] / th) * (m_u[k] / th));
    }
    // For tests: the column taken away (a frame drum's open back) or put back.
    void setAir(bool on) {
        m_airOn = on ? 1.0f : 0.0f;
        retune();
    }
    // The mean tension now, over the built one (the stretch of a hard stroke).
    float tensionNow() const {
        float strain = 0;
        for (int k = 0; k < m_n; ++k)
            strain += m_g[k] * m_x[k] * m_x[k];
        return 1.0f + m_stretch * strain / m_T;
    }

private:
    struct Contact {
        bool on = false;
        int delay = 0, age = 0, touched = 0;
        float z = 0, v = 0, zPrev = 0; // the finger's travel inward from where it met the skin, and its speed
        float mass = 0.006f, pulp = 4e5f, loss = 0.5f, retract = 1000.0f;
        float woodShare = 0, woodPulp = 6e6f;
        float bone = 0, boneAt = 1;
        float muSkin = 0, muWood = 0; // how far a newton moves each surface under it in one sample
        float ds = 0, dw = 0;          // how far the finger is into each, last sample
        float tickShare = 1;           // how freely the skin moves under it (for its force's own sound)
        float fs = 0, fw = 0;          // and the forces it made there
        float sPrev = 0, wPrev = 0;
        alignas(32) float phi[kMax] = {};
        float wphi[kWood] = {};
    };

    struct Rest {
        bool on = false;
        int id = 0, delay = 0;
        float x = 0, y = 0;
        float z = 0, v = 0, press = 1.0f, pressNow = 0, d = 0, mu = 0;
        float onWood = 0;        // how much of it lies on the rim's wood
        float patch = 0.012f, fingers = 1; // its patch (m), and how many fingers it is
        float wphi[kWood] = {};  // the wood's modes where it lies
        bool met = false;
        float sPrev = 0;
        alignas(32) float phi[kMax] = {};
    };

    Contact *freeContact() {
        for (auto &k : m_contacts)
            if (!k.on)
                return &k;
        Contact *c = &m_contacts[0];
        for (auto &k : m_contacts)
            if (k.age > c->age)
                c = &k;
        return c;
    }

    float tap(const float *line, float d) const {
        const int i = int(d);
        const float t = d - float(i);
        const float a = line[(m_wr - i) & (kDelay - 1)], b = line[(m_wr - i - 1) & (kDelay - 1)];
        return a + t * (b - a);
    }

    // Each mode's shape at (r, θ), averaged over a round patch of radius b
    // (m): J_m(j·r)·cos(m(θ − θ_k)) times the patch's 2J₁(kb)/(kb).
    void project(float *phi, float r, float th, float b) const {
        const float pos = clampf(r, 0.0f, 1.0f) * float(kTable - 1);
        const int i0 = std::min(int(pos), kTable - 2);
        const float t = pos - float(i0);
        for (int k = 0; k < m_n; ++k) {
            const float *tab = m_shape + k * kTable;
            const float j = tab[i0] + t * (tab[i0 + 1] - tab[i0]);
            const float ang = m_m[k] == 0 ? 1.0f : std::cos(float(m_m[k]) * th - m_ori[k]);
            phi[k] = j * ang * airy(m_k[k] * b);
        }
    }

    // How far the skin under a patch moves in one sample per newton on it.
    float mobility(const float *phi) const {
        float mu = 0;
        for (int k = 0; k < m_n; ++k)
            mu += phi[k] * phi[k] * m_inv[k];
        return mu;
    }

    float airy(float x) const {
        const float pos = clampf(x, 0.0f, 79.99f) * 20.0f;
        const int i = int(pos);
        const float t = pos - float(i);
        return m_airy[i] + t * (m_airy[i + 1] - m_airy[i]);
    }

    void place(Rest &s, float x, float y) {
        s.x = x;
        s.y = y;
        const float r = std::sqrt(x * x + y * y);
        project(s.phi, std::min(r, 1.0f), std::atan2(y, x), s.patch); // a finger lying flat
        if (r > 1.0f) // off the skin it damps the skin not at all
            for (int k = 0; k < m_n; ++k)
                s.phi[k] = 0;
        s.mu = mobility(s.phi);
        // Lying across the rim, it damps the wood's ring too.
        const float th = std::atan2(y, x);
        s.onWood = clampf((r - kRimFrom) / (kWoodFrom - kRimFrom), 0.0f, 1.0f);
        for (int j = 0; j < kWood / 2; ++j) {
            s.wphi[2 * j] = std::cos(float(j + 2) * th);
            s.wphi[2 * j + 1] = std::sin(float(j + 2) * th);
        }
    }

    // The strokes' contact forces this sample (outward displacement x; a
    // finger presses inward). Solved implicitly: a light skin moves a long
    // way in one sample under a stiff fingertip, so each force is the one
    // that, applied this step, leaves the finger as far into the surface as
    // the force says (bisection on the Hertz–Hunt–Crossley law), against
    // the surface's own motion carried on a sample and the finger's.
    void contacts(float *f, float &tick, float &woodTick, float &rasp) {
        bool any = false;
        const float dt2 = m_dt * m_dt;
        for (auto &c : m_contacts) {
            if (!c.on)
                continue;
            any = true;
            if (c.delay > 0) {
                --c.delay;
                continue;
            }
            // The skin's and the wood's surface under the finger, inward.
            float s = 0;
            for (int k = 0; k < m_n; ++k)
                s -= c.phi[k] * m_x[k];
            float w = 0;
            for (int j = 0; j < kWood; ++j)
                w -= c.wphi[j] * m_wx[j];
            if (c.age == 0) {
                c.z = s * (1.0f - c.woodShare) + w * c.woodShare;
                c.sPrev = s;
                c.wPrev = w;
                c.ds = c.z - s;
                c.dw = c.z - w;
            }
            ++c.age;
            // Where the surfaces and the finger would be next sample with
            // no force between them.
            const float s0 = 2.0f * s - c.sPrev, w0 = 2.0f * w - c.wPrev;
            float z = c.z + (c.v - c.retract * m_dt) * m_dt;
            c.sPrev = s;
            c.wPrev = w;
            const float fs = c.woodShare < 1.0f ? solve(c, z - s0, c.ds, c.muSkin + dt2 / c.mass, 1.0f - c.woodShare, c.pulp, c.fs) : 0.0f;
            z -= fs * dt2 / c.mass;
            const float fw = c.woodShare > 0.0f ? solve(c, z - w0, c.dw, c.muWood + dt2 / c.mass, c.woodShare, c.woodPulp, c.fw) : 0.0f;
            z -= fw * dt2 / c.mass;
            c.ds = z - (s0 + fs * c.muSkin);
            c.dw = z - (w0 + fw * c.muWood);
            if (fs > 0) {
                for (int k = 0; k < m_n; ++k)
                    f[k] -= c.phi[k] * fs;
                tick += fs * c.tickShare;
                rasp += fs * fs;
            }
            if (fw > 0) {
                for (int j = 0; j < kWood; ++j)
                    m_wf[j] -= c.wphi[j] * fw;
                woodTick += fw;
            }
            if (fs > 0 || fw > 0)
                c.touched = c.age;
            c.v = (z - c.z) * m_rate;
            c.z = z;
            const float force = fs + fw;
            if ((force <= 0 && c.v < 0 && c.age > 4) || c.age > int(0.05f * m_rate)) {
                c.on = false;
                m_lastContact = float(c.touched) / m_rate;
            }
        }
        m_touching = any;
    }

    // The force F with F = share·g(δ0 − μ·F), g the pulp's (and, pressed
    // far, the bone's) Hertzian spring with Hunt–Crossley loss on the rate
    // the compression changes from `was`: Newton's method from last
    // sample's force, kept inside a bracket that bisection would also keep.
    float solve(Contact &c, float d0, float was, float mu, float share, float pulp, float &guess) const {
        if (d0 <= 0)
            return guess = 0;
        const float R = m_rate;
        // g and its slope at compression d.
        auto g = [&](float d, float &slope) {
            if (d <= 0) {
                slope = 0;
                return 0.0f;
            }
            const float sd = std::sqrt(d);
            float k = pulp * d * sd, dk = 1.5f * pulp * sd;
            if (d > c.boneAt) {
                const float b = d - c.boneAt, sb = std::sqrt(b);
                k += c.bone * b * sb;
                dk += 1.5f * c.bone * sb;
            }
            const float hc = 1.0f + c.loss * (d - was) * R;
            if (hc <= 0) {
                slope = 0;
                return 0.0f;
            }
            slope = share * (dk * hc + k * c.loss * R);
            return share * k * hc;
        };
        float slope;
        float lo = 0, hi = g(d0, slope);
        float F = clampf(guess, lo, hi);
        for (int i = 0; i < 8; ++i) {
            const float h = g(d0 - mu * F, slope) - F;
            if (h > 0)
                lo = F;
            else
                hi = F;
            float next = F - h / (-mu * slope - 1.0f);
            if (!(next > lo && next < hi))
                next = 0.5f * (lo + hi);
            if (std::fabs(next - F) <= 1e-5f * (hi + 1e-6f)) {
                F = next;
                break;
            }
            F = next;
        }
        return guess = F;
    }

    // Fingers lying on the skin: each a mass pressed in by the hand, on the
    // pulp's spring and dashpot; linear, so solved implicitly in one go.
    void rests(float *f) {
        for (auto &s : m_rests) {
            if (!s.on)
                continue;
            const float K = Knobs::restSpring * s.fingers, BR = Knobs::restDash * s.fingers * m_rate;
            const float M = Knobs::restMass * s.fingers;
            if (s.delay > 0) {
                --s.delay;
                continue;
            }
            float surf = 0;
            for (int k = 0; k < m_n; ++k)
                surf -= s.phi[k] * m_x[k];
            if (!s.met) {
                s.met = true;
                s.z = surf;
                s.sPrev = surf;
                s.d = 0;
                s.v = 0;
            }
            // (The hand settles onto the skin over some 20 ms, not at once.)
            s.pressNow += (s.press - s.pressNow) * m_settle;
            const float s0 = 2.0f * surf - s.sPrev;
            s.sPrev = surf;
            // The hand presses the finger in and holds it from bouncing.
            const float vh = (s.v + s.pressNow / M * m_dt) / (1.0f + Knobs::restHand * s.fingers / M * m_dt);
            float z = s.z + vh * m_dt;
            const float mu = s.mu + m_dt * m_dt / M;
            const float d0 = z - s0;
            float force = ((K + BR) * d0 - BR * s.d) / (1.0f + (K + BR) * mu);
            force = std::max(0.0f, force);
            z -= force * m_dt * m_dt / M;
            s.d = z - (s0 + force * s.mu);
            if (force > 0)
                for (int k = 0; k < m_n; ++k)
                    f[k] -= s.phi[k] * force;
            s.v = (z - s.z) * m_rate;
            s.z = z;
            // On the wood its pulp is a dashpot on the rim's motion (a
            // light load on a heavy ring: explicit is enough).
            if (s.onWood > 0 && force > 0) {
                float vw = 0;
                for (int j = 0; j < kWood; ++j)
                    vw += s.wphi[j] * m_wu[j];
                const float fw = s.onWood * Knobs::restDash * s.fingers * vw * m_rate;
                for (int j = 0; j < kWood; ++j)
                    m_wf[j] -= s.wphi[j] * fw;
            }
        }
    }

    void sleep() {
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            damp();
        }
        for (int k = 0; k < kMax; ++k)
            m_x[k] = m_u[k] = m_force[k] = 0;
        for (int a = 0; a < kAir; ++a)
            m_xi[a] = m_eta[a] = 0;
        for (int j = 0; j < kWood; ++j)
            m_wx[j] = m_wu[j] = m_wf[j] = 0;
        for (auto &c : m_contacts)
            c.on = false;
        for (auto &s : m_rests)
            s.met = false;
        m_touching = false;
        m_qe = m_qeR = m_wq = m_wqR = 0;
        for (float &v : m_footLine)
            v = 0;
        m_tickHp.reset();
        m_woodHp.reset();
        m_raspHp.reset();
        m_sleeping = true;
        m_energy = 0;
        m_quiet = 0;
    }

    void buildTables() {
        double v[2];
        for (int i = 0; i < kAiry; ++i) {
            const double x = double(i) / 20.0;
            besselAll(x, 1, v);
            m_airy[i] = i == 0 ? 1.0f : float(2.0 * v[1] / x);
        }
    }

    // The skin's modes, the column's and the wood's, once.
    void build() {
        const float a = Physics::kHeadRadius;
        // Every (m, n) with j_mn below the cap, from a sweep of J_0 … J_mMax
        // over x for their sign changes, each zero then bisected; sorted by
        // j, kept up to kMax (a pair counting twice), the m = 0 ones first.
        constexpr int kM = 40, kCand = 700;
        constexpr double kJCap = 44.0;
        struct Cand {
            int m, n;
            double j;
        };
        Cand cand[kCand];
        int nc = 0;
        {
            double prev[kM + 1], cur[kM + 1];
            int count[kM + 1] = {};
            besselAll(0.05, kM, prev);
            for (double x = 0.1; x <= kJCap && nc < kCand; x += 0.05) {
                besselAll(x, kM, cur);
                for (int m = 0; m <= kM && nc < kCand; ++m) {
                    if ((prev[m] > 0) == (cur[m] > 0) || std::fabs(prev[m]) + std::fabs(cur[m]) < 1e-30)
                        continue;
                    double lo = x - 0.05, hi = x, flo = prev[m], v[kM + 1];
                    for (int it = 0; it < 44; ++it) {
                        const double mid = 0.5 * (lo + hi);
                        besselAll(mid, m, v);
                        if ((v[m] > 0) == (flo > 0))
                            lo = mid, flo = v[m];
                        else
                            hi = mid;
                    }
                    cand[nc++] = {m, ++count[m], 0.5 * (lo + hi)};
                }
                std::copy(cur, cur + kM + 1, prev);
            }
        }
        std::sort(cand, cand + nc, [](const Cand &p, const Cand &q) { return p.j < q.j; });
        int keep = 0, count = 0;
        while (keep < nc && count + (cand[keep].m ? 2 : 1) <= kMax)
            count += cand[keep++].m ? 2 : 1;
        std::stable_sort(cand, cand + keep, [](const Cand &p, const Cand &q) { return (p.m == 0) > (q.m == 0); });
        Rng rng(0x4b15u);
        int k = 0;
        m_n0 = 0;
        double v[kM + 2];
        for (int c = 0; c < keep; ++c) {
            const int m = cand[c].m, n = cand[c].n;
            const double j = cand[c].j;
            // A pair's axes lie wherever the skin's unevenness puts them.
            const float axis = m ? kTwoPi * rng.unit() / float(m) : 0.0f;
            besselAll(j, m + 1, v);
            const double jn = v[m + 1]; // J_{m+1}(j) = −J'_m(j) at the zero
            const float split = Physics::kSplit * (0.3f + 0.7f * rng.unit());
            for (int o = 0; o < (m ? 2 : 1); ++o, ++k) {
                m_m[k] = uint8_t(m);
                m_nn[k] = uint8_t(n);
                m_j[k] = float(j);
                m_k[k] = float(j) / a;
                m_ori[k] = axis + (o ? kPi / (2.0f * float(m)) : 0.0f);
                // ∫φ² over the head: πa²·J_{m+1}(j)² for m = 0, half that otherwise.
                m_norm[k] = float(kPi * a * a * jn * jn * (m ? 0.5 : 1.0));
                // The volume it sweeps per metre: ∫J₀(kr)·2πr dr = 2πa·J₁(j)/k.
                m_area[k] = m == 0 ? float(kTwoPi * a * jn / (j / a)) : 0.0f;
                m_jn[k] = float(jn);
                m_split[k] = m ? (o ? 0.5f : -0.5f) * split : 0.0f;
                float *tab = m_shape + k * kTable;
                double w[kM + 2];
                for (int i = 0; i < kTable; ++i) {
                    besselAll(j * double(i) / double(kTable - 1), m, w);
                    tab[i] = float(w[m]);
                }
            }
            if (m == 0)
                m_n0 = k;
        }
        m_n = k;
        buildColumn();
        buildWood();
        m_woodHp.set(2500.0f, 0.6f, m_rate);
        m_raspHp.set(5000.0f, 0.6f, m_rate);
        retune();
    }

    // The bore's radius at depth z.
    static float boreRadius(float z) {
        const float *Z = Physics::kBoreZ, *R = Physics::kBoreR;
        if (z <= Z[0])
            return R[0];
        for (int i = 1; i < Physics::kBore; ++i)
            if (z <= Z[i]) {
                const float t = (z - Z[i - 1]) / (Z[i] - Z[i - 1]);
                return R[i - 1] + t * (R[i] - R[i - 1]);
            }
        return R[Physics::kBore - 1];
    }

    // Webster's horn equation for the bore, shot from the open foot (p = 0
    // one end correction past it) up to the skin, at angular frequency w:
    // dp/dz = −(ωρ/S)·Ũ, dŨ/dz = (ωS/ρc²)·p, with the volume flow U = −jŨ.
    // Returns Ũ at the skin (zero where the column resonates against a
    // rigid skin). Also: p at the skin (for Ũ = 1 at the foot), ∫S·p² dz
    // (the modal volume as it comes), and the walls' share of a boundary
    // layer's losses: ∫perimeter·(Ũ/S)² dz (viscous) and ∫perimeter·p² dz
    // (thermal).
    struct Shot {
        double top = 0, p0 = 0, volume = 0, viscous = 0, thermal = 0;
    };
    static Shot shoot(double w) {
        const double rho = Physics::kRhoAir, c = Physics::kC, pi = 3.14159265358979323846;
        const double L = Physics::kBoreZ[Physics::kBore - 1];
        const double rEnd = Physics::kBoreR[Physics::kBore - 1];
        const double bottom = L + (0.6133 + Physics::kFootMass) * rEnd; // the end's correction, the gap's mass
        const int steps = 600;
        const double h = bottom / steps;
        auto radius = [&](double z) { return z >= L ? rEnd : double(boreRadius(float(z))); };
        auto dp = [&](double z, double u) { const double r = radius(z); return -(w * rho / (pi * r * r)) * u; };
        auto du = [&](double z, double p) { const double r = radius(z); return (w * pi * r * r / (rho * c * c)) * p; };
        Shot s;
        double p = 0, U = 1;
        for (int i = 0; i < steps; ++i) {
            const double z = bottom - h * i;
            // RK4 upward (dz = −h).
            const double k1p = dp(z, U), k1u = du(z, p);
            const double k2p = dp(z - h / 2, U - h / 2 * k1u), k2u = du(z - h / 2, p - h / 2 * k1p);
            const double k3p = dp(z - h / 2, U - h / 2 * k2u), k3u = du(z - h / 2, p - h / 2 * k2p);
            const double k4p = dp(z - h, U - h * k3u), k4u = du(z - h, p - h * k3p);
            const double p1 = p - h / 6 * (k1p + 2 * k2p + 2 * k3p + k4p);
            const double U1 = U - h / 6 * (k1u + 2 * k2u + 2 * k3u + k4u);
            const double zm = z - h / 2, r = radius(zm), S = pi * r * r;
            const double pm = 0.5 * (p + p1), um = 0.5 * (U + U1);
            s.volume += S * pm * pm * h;
            if (zm < L) {
                s.viscous += 2 * pi * r * (um / S) * (um / S) * h;
                s.thermal += 2 * pi * r * pm * pm * h;
            }
            p = p1;
            U = U1;
        }
        s.top = U;
        s.p0 = p;
        return s;
    }

    void buildColumn() {
        const double rho = Physics::kRhoAir, c = Physics::kC, pi = 3.14159265358979323846;
        const double rEnd = Physics::kBoreR[Physics::kBore - 1];
        const double sEnd = pi * rEnd * rEnd;
        const double mu = 1.81e-5, gamma = 1.4, prandtl = 0.71;
        m_nAir = 0;
        double prevW = kTwoPi * 20.0;
        double prev = shoot(prevW).top;
        for (double w = prevW + kTwoPi * 3.0; w < kTwoPi * 2600.0 && m_nAir < kAir; w += kTwoPi * 3.0) {
            const double cur = shoot(w).top;
            if ((cur > 0) != (prev > 0)) {
                double lo = prevW, hi = w, flo = prev;
                for (int i = 0; i < 40; ++i) {
                    const double mid = 0.5 * (lo + hi), fm = shoot(mid).top;
                    if ((fm > 0) == (flo > 0))
                        lo = mid, flo = fm;
                    else
                        hi = mid;
                }
                const double wn = 0.5 * (lo + hi);
                const Shot s = shoot(wn);
                // Normalised to 1 Pa under the skin.
                const double p2 = s.p0 * s.p0;
                const double Lambda = s.volume / p2;
                const int a = m_nAir++;
                m_airHz[a] = float(wn / kTwoPi);
                m_C[a] = float(Lambda / (rho * c * c));
                m_L[a] = float(1.0 / (wn * wn * m_C[a]));
                // The foot's volume acceleration per pascal under the skin.
                m_footGainRaw[a] = float(wn / s.p0);
                // Losses: the foot's radiation, (ka)²/4 of ρc/S unflanged;
                // the walls' viscous and thermal boundary layers.
                const double k0 = wn / c;
                const double E = 0.5 * Lambda / (rho * c * c);
                const double Rrad = rho * c / sEnd * (k0 * rEnd) * (k0 * rEnd) / 4.0;
                const double Prad = 0.5 * (Rrad + Physics::kFootResist * rho * c / sEnd) / p2;
                const double dv = std::sqrt(2.0 * mu / (rho * wn)), dt = dv / std::sqrt(prandtl);
                const double Pwall = 0.25 * rho * wn * dv * s.viscous / p2 +
                                     0.25 * (gamma - 1.0) * wn * dt / (rho * c * c) * s.thermal / p2;
                m_airQ[a] = float(wn * E / (Prad + Physics::kRough * Pwall));
            }
            prev = cur;
            prevW = w;
        }
    }

    // The rim's wood: a ring of the body's wall, bending in n = 2 … 5 lobes
    // round it (Love's thin-ring formula), each a pair a little apart. Its
    // wall radiates as a piston of its own area above the wood's
    // coincidence frequency and falls off steeply below it (a bending wave
    // slower than sound radiates only from its ends); each microphone hears
    // a lobe's sign where it faces.
    void buildWood() {
        const float r = Physics::kHeadRadius + 0.5f * Physics::kWoodWall, h = Physics::kWoodWall;
        const float base = h / (kTwoPi * r * r) * std::sqrt(Physics::kWoodE / (12.0f * Physics::kWoodRho));
        // The ring's modal mass: half the wall over a bowl's depth.
        const float mass = Physics::kWoodRho * h * kTwoPi * r * Physics::kBowlDepth * 0.5f;
        const float area = kTwoPi * r * Physics::kBowlDepth * 0.5f;
        for (int j = 0; j < kWood; ++j) {
            const float n = float(j / 2 + 2);
            m_whz[j] = base * n * (n * n - 1.0f) / std::sqrt(n * n + 1.0f) * (j & 1 ? 1.012f : 1.0f);
            m_wmass[j] = mass;
            const float q = std::pow(Physics::kWoodCoincidence / m_whz[j], 4.0f);
            const float eff = area / std::sqrt(1.0f + q);
            const float phase = (j & 1) ? 0.5f * kPi : 0.0f;
            m_wgl[j] = eff * std::cos(n * Physics::kMicLeft - phase);
            m_wgr[j] = eff * std::cos(n * Physics::kMicRight - phase);
        }
    }

    // Frequencies, losses and what is heard, from the tension.
    void retune() {
        const float a = Physics::kHeadRadius, rho = Physics::kRhoAir, c0 = Physics::kC;
        m_T = Physics::kTension * std::exp2(m_semis / 6.0f);
        m_stretch = Physics::kStretch / (2.0f * kPi * a * a);
        const float sinMic = std::sin(Physics::kMicAngle);
        // The volume the m = 0 modes push is a piston's at the open end of a
        // pipe (the head is no baffle): outside it carries the unflanged
        // end's mass, 0.6133ρ/(πa) per m³ of volume, squared, and inside the
        // column's air (its tanks).
        const float pistonMass = 0.6133f * rho / (kPi * a);
        const float S = kPi * a * a;
        for (int k = 0; k < m_n; ++k) {
            const float kk = m_k[k];
            // The rest of each shape, what sweeps no volume, carries its near
            // field each side: ρ/k outside, ρ/(k·tanh(k·depth)) in the cup.
            const float airSide = rho / kk + rho / (kk * std::tanh(kk * Physics::kBowlDepth));
            const float piston = m_m[k] == 0 ? m_area[k] * m_area[k] / S : 0.0f; // the piston's share of ∫φ²
            m_mass[k] = Physics::kSkin * m_norm[k] + airSide * (m_norm[k] - piston) +
                        (m_m[k] == 0 ? pistonMass * m_area[k] * m_area[k] : 0.0f);
            m_g[k] = kk * kk * m_norm[k];
            const float K = m_T * m_g[k];
            m_vacHz[k] = std::sqrt(K / (Physics::kSkin * m_norm[k])) / kTwoPi;
            m_hz[k] = std::sqrt(K / m_mass[k]) / kTwoPi * (1.0f + m_split[k]);
            const float w = kTwoPi * m_hz[k];
            // Losses: the skin's own, rising with frequency, and what the
            // mode radiates: its far field integrated over the half space in
            // front, ρω⁴|D|²/(8π²c) per unit swing.
            const float eta = Physics::kLoss + Physics::kLossPerKHz * m_hz[k] / 1000.0f;
            const float k0 = w / c0;
            double P = 0;
            for (int i = 0; i < 24; ++i) {
                const double th = (double(i) + 0.5) / 24.0 * 0.5 * kPi;
                const double D = farField(k, k0, float(std::sin(th)));
                P += D * D * std::sin(th) * (0.5 * kPi / 24.0);
            }
            P *= (m_m[k] ? kPi : kTwoPi) * rho * std::pow(double(w), 4.0) / (8.0 * kPi * kPi * c0);
            // (A monopole at an unflanged pipe's end radiates into the whole
            // space, not a half: half the baffled piston's resistance.)
            if (m_m[k] == 0)
                P *= 0.5;
            const double E = 0.5 * m_mass[k] * w * w;
            const float Qrad = float(w * E / std::max(P, 1e-30));
            const float Q = 1.0f / (eta + 1.0f / Qrad);
            m_tau0[k] = 2.0f * Q / w;
            // The microphones: the far field's directivity there, at this
            // mode's frequency, with its phase (−j)^m: m = 0 and 2 in phase
            // with the volume acceleration (2 inverted), 1 and 3 a quarter
            // cycle on (1 as ω times the velocity, 3 inverted).
            const float Dm = farField(k, k0, sinMic);
            const int m = m_m[k];
            const float cl = m ? std::cos(float(m) * (Physics::kMicLeft - m_ori[k])) : 1.0f;
            const float cr = m ? std::cos(float(m) * (Physics::kMicRight - m_ori[k])) : 1.0f;
            const float sgn = (m & 3) >= 2 ? -1.0f : 1.0f;
            if (m & 1) {
                m_ge[k] = m_geR[k] = 0;
                m_go[k] = sgn * Dm * cl * w / m_rate;
                m_goR[k] = sgn * Dm * cr * w / m_rate;
            } else {
                m_ge[k] = sgn * Dm * cl;
                m_geR[k] = sgn * Dm * cr;
                m_go[k] = m_goR[k] = 0;
            }
        }
        // The column's tanks: inertance L on compliance C, driven by the
        // volume pushed in.
        for (int i = 0; i < kAir; ++i) {
            m_invC[i] = i < m_nAir ? 1.0f / m_C[i] : 0.0f;
            m_ainv[i] = i < m_nAir ? m_dt * m_dt / m_L[i] : 0.0f;
            m_footGain[i] = i < m_nAir ? m_footGainRaw[i] : 0.0f;
        }
        // Pressure ∝ ρ/(2πR)·(volume acceleration); u is metres per sample,
        // so two of rate to m³/s². The foot's comes as m³/s² already, from
        // further away and later.
        const float scale = Knobs::level * rho / (kTwoPi * Physics::kMicDistance);
        m_gain = scale * m_rate * m_rate;
        m_oddGain = scale * m_rate * m_rate;
        m_woodGain = scale * Knobs::wood * m_rate * m_rate;
        m_footLevel = scale * Knobs::foot * Physics::kMicDistance / (Physics::kMicDistance + Physics::kFootFurther);
        m_footDelay = clampf(Physics::kFootFurther / c0 * m_rate, 0.0f, float(kDelay - 2));
        // A point force on a membrane of mass σ per area radiates, above its
        // modes, as the volume acceleration F/σ; on the wood, F/(ρh).
        // (Above the highest mode: below it the modes are the force's sound.)
        m_tickHp.set(std::min(m_hz[m_n - 1], 0.45f * m_rate), 0.6f, m_rate);
        m_tickGain = scale * Knobs::tick / (Physics::kSkin + 2.0f * rho / m_k[m_n - 1]);
        m_woodTickGain = scale * Knobs::tick / (Physics::kWoodRho * Physics::kWoodWall);
        m_raspGain = scale * Knobs::rasp * 0.05f / Physics::kSkin;
        damp();
    }

    // The oscillators from the frequencies and decays, exact: u' = d·u −
    // c·x + f·dt²/m, x' = x + u', poles r·e^{±iθ} for d = r², c = 1 + r² −
    // 2r·cos θ. (Cheap: what a stroke's decay calls for.)
    void damp() {
        auto pole = [&](float hz, float tau, float &d, float &c) {
            const float th = kTwoPi * std::min(hz, 0.45f * m_rate) / m_rate;
            const float rr = std::exp(-1.0f / (std::max(tau, 1e-4f) * m_rate));
            d = std::min(rr * rr, m_muteD);
            c = 1.0f + rr * rr - 2.0f * rr * std::cos(th);
        };
        for (int k = 0; k < m_n; ++k) {
            m_tau[k] = m_tau0[k] * m_decayScale;
            pole(m_hz[k], m_tau[k], m_d[k], m_c[k]);
            m_inv[k] = m_dt * m_dt / m_mass[k];
        }
        for (int i = 0; i < m_nAir; ++i)
            pole(m_airHz[i], 2.0f * m_airQ[i] / (kTwoPi * m_airHz[i]), m_ad[i], m_ac[i]);
        {
            float phi[kMax];
            project(phi, 0.8f, 0.0f, Knobs::tekPatch);
            m_muFree = mobility(phi);
        }
        for (int j = 0; j < kWood; ++j) {
            pole(m_whz[j], 2.0f * Physics::kWoodQ / (kTwoPi * m_whz[j]), m_wd[j], m_wc[j]);
            m_winv[j] = m_dt * m_dt / m_wmass[j];
        }
    }

    // A mode's far field at polar angle θ (sin θ given), per metre of its
    // swing: 2πa·J_{m+1}(j)·J_m(k₀a·sin θ)/k, the Hankel transform of its
    // shape (the head a baffled source, its modes slower than sound).
    float farField(int k, float k0, float sinTh) const {
        const float a = Physics::kHeadRadius;
        const double J = besselSeries(m_m[k], double(k0 * a * sinTh));
        return float(kTwoPi * a * m_jn[k] * J / m_k[k]);
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    int m_n = 0, m_n0 = 0, m_nAir = 0;
    alignas(32) float m_x[kMax] = {}, m_u[kMax] = {}, m_force[kMax] = {};
    alignas(32) float m_d[kMax] = {}, m_c[kMax] = {}, m_inv[kMax] = {}, m_g[kMax] = {};
    alignas(32) float m_area[kMax] = {}, m_ge[kMax] = {}, m_geR[kMax] = {}, m_go[kMax] = {}, m_goR[kMax] = {};
    float m_mass[kMax] = {}, m_hz[kMax] = {}, m_vacHz[kMax] = {}, m_tau[kMax] = {}, m_tau0[kMax] = {};
    float m_k[kMax] = {}, m_j[kMax] = {}, m_ori[kMax] = {}, m_norm[kMax] = {}, m_jn[kMax] = {}, m_split[kMax] = {};
    uint8_t m_m[kMax] = {}, m_nn[kMax] = {};
    float m_shape[kMax * kTable] = {};
    float m_airy[kAiry] = {};
    // The column.
    float m_xi[kAir] = {}, m_eta[kAir] = {}, m_ad[kAir] = {}, m_ac[kAir] = {}, m_ainv[kAir] = {};
    float m_invC[kAir] = {}, m_C[kAir] = {}, m_L[kAir] = {}, m_footGain[kAir] = {}, m_footGainRaw[kAir] = {};
    float m_airHz[kAir] = {}, m_airQ[kAir] = {};
    float m_airOn = 1.0f;
    // The wood.
    float m_wx[kWood] = {}, m_wu[kWood] = {}, m_wf[kWood] = {}, m_wd[kWood] = {}, m_wc[kWood] = {}, m_winv[kWood] = {};
    float m_whz[kWood] = {}, m_wmass[kWood] = {}, m_wgl[kWood] = {}, m_wgr[kWood] = {};
    // Hands.
    Contact m_contacts[kContacts];
    Rest m_rests[kRests];
    // The sound.
    float m_footLine[kDelay] = {};
    int m_wr = 0;
    float m_footDelay = 0, m_footLevel = 0;
    float m_gain = 1, m_oddGain = 1, m_woodGain = 1, m_tickGain = 0, m_woodTickGain = 0, m_raspGain = 0;
    float m_qe = 0, m_qeR = 0, m_wq = 0, m_wqR = 0;
    Svf m_tickHp, m_woodHp, m_raspHp;
    Rng m_noise{0x6b15u};
    // State.
    float m_T = Physics::kTension, m_stretch = 0, m_semis = 0, m_decayScale = 1, m_muteD = 1.0f;
    float m_energy = 0, m_lastContact = 0;
    float m_muFree = 1;  // the skin's mobility under a fingertip at r = 0.8, for reference
    float m_settle = 1.0f / (0.02f * 48000.0f);
    int m_quiet = 0;
    bool m_sleeping = true, m_touching = false, m_resting = false;
};

}
