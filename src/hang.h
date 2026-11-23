#pragma once

// A hang, as physics. Not samples and not a recipe: the instrument's own
// parts, in SI units, integrated sample by sample. The measurements it is
// built from and checked against are in docs/hang.md.
//
//   the shell   two lens-shaped shells of nitrided steel, 0.93 mm thick,
//               glued at the rim. On the top one a central dome, the ding,
//               and a ring of eight elliptical dimples, the tone fields,
//               each hammered so its first three modes are a fundamental,
//               its octave and its compound fifth (1 : 2 : 3, within a
//               maker's few cents), with the next modes near the third
//               octave. A field's size follows the steelpan law (length ∝
//               f^−2/3), and every mode is a damped oscillator with its own
//               modal mass from the steel and the field's area.
//   the gu      the hole in the bottom shell. The air in it is a plug of
//               mass ρ·S·L on the spring of the cavity's 25 litres: a
//               Helmholtz resonator, sized to sound at 85 Hz under a 148 Hz
//               ding. The cavity's pressure is ρc²/V times the change in
//               its volume, and every mode that changes the volume (a
//               field's dome like a piston of its effective area, its
//               octave with unequal lobes a little) feels that pressure and
//               makes it. So the shell and the air trade energy both ways:
//               the ding pumps the gu, the gu pushes back on the ding and
//               every fundamental, the shell's give pulls the gu below the
//               rigid-walled formula, and the low end of a hang is the two
//               together, as in a bass-reflex box. The gu's lip rings as a
//               small bell of its own when it is slapped.
//   the shoulder the steel between the fields joins them. Partials of
//               different fields that fall together (A3's octave on A4,
//               the ding's fifth on A4 and on A3's octave …) are coupled
//               pairwise, as the holograms show them driving each other:
//               struck, one rings the other, and the pair, a few cents
//               apart, beats a few times a second. A weak shared motion of
//               the shoulder leaks a little of everything everywhere. The
//               shoulder has broad inharmonic modes of its own, the dry
//               "tok" of a tap between the fields.
//   the hand    a fingertip of a few grams arriving at up to a couple of
//               metres a second, its pulp a stiffening Hertzian spring
//               (F = K·δ^1.5): it presses into the steel, the steel gives
//               way, the hand pulls it back. The force is solved against the
//               surface's own motion every sample, so a contact lasts one
//               to four milliseconds, shorter and brighter when harder, and
//               where it lands decides which modes it reaches (the middle
//               of a field is on the nodal lines of its octave and fifth;
//               off centre along the long axis wakes the octave, across it
//               the fifth; past the rim, the shoulder). While it touches,
//               the force itself radiates from the steel under it, as from
//               an infinite plate: the click of a fingertip on metal.
//   the dimple  a curved shell is not linear: its fundamental, moving far,
//               drives its octave and fifth through the curvature (a cubic
//               potential, so energy flows both ways) and sags in pitch, the
//               internal resonance and softening steelpan studies measure.
//               A hard strike starts flat, blooms into its octave and then
//               its fifth; a soft one stays nearly pure. The volume a
//               curved dome sweeps is not linear in its swing either, so a
//               hard strike radiates harmonics above the fifth.
//   the sound   what reaches two microphones either side of the player is
//               the outside air pushed by every part: the volume velocity of
//               each mode and of the plug in the gu, differentiated (a small
//               source's pressure follows its volume acceleration). Modes
//               with a nodal line radiate as dipoles, so each microphone
//               hears them with the sign of the lobe facing it.
//
// Its cost does not grow with the number of fingers: the shell rings once,
// whatever strikes it, and a strike only adds a contact for the few
// milliseconds it touches.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"

namespace tf::hang {

constexpr int kFields = 9;     // the ding (0) and eight tone fields, lowest (1) to highest (8)
constexpr int kGu = kFields;   // a strike there slaps the gu

// --- the instrument's shape -----------------------------------------------------------------------------
//
// The top shell seen from above, in units of its radius: x to the right,
// y away from the player. The lowest field nearest the player, then up
// the scale zig-zagging right and left round the ding to the highest at
// the far side, each field's long axis pointing at the ding.

struct Field {
    float x, y;   // centre
    float a, b;   // semi-axes: a along the long axis, b across it
    float axis;   // the long axis' direction, radians
};

constexpr float kRing = 0.62f; // where the fields' centres sit

inline Field field(int n) {
    n = std::clamp(n, 0, kFields - 1);
    if (n == 0)
        return {0, 0, 0.27f, 0.27f, 0};
    // Round from the player: lowest at the front, then right, left, … to
    // the highest at the back.
    static const float deg[8] = {-90, -45, -135, 0, 180, 45, 135, 90};
    // Sized as the classic hang's notes (A3 … A4), by the steelpan law: a
    // field's length goes as its frequency to the −2/3, an 11 cm A3; its
    // width three quarters of it, which puts its fifth at 3:2 to its octave.
    static const float semis[8] = {0, 1, 3, 5, 7, 8, 10, 12};
    const float th = deg[n - 1] * kPi / 180.0f;
    const float a = 0.215f * std::exp2(-2.0f / 3.0f * semis[n - 1] / 12.0f);
    return {kRing * std::cos(th), kRing * std::sin(th), a, 0.76f * a, th};
}

// Where a point on the shell is, against a field: u along its long axis,
// w across, both 1 at its rim; rho the elliptical distance from its middle.
struct Spot {
    int field = 0; // the nearest field, or kGu off the shell
    float u = 0, w = 0, rho = 0;
};

inline Spot spotOn(int n, float x, float y) {
    const Field f = field(n);
    const float dx = x - f.x, dy = y - f.y;
    const float c = std::cos(f.axis), s = std::sin(f.axis);
    Spot sp;
    sp.field = n;
    sp.u = (dx * c + dy * s) / f.a;
    sp.w = (-dx * s + dy * c) / f.b;
    sp.rho = std::sqrt(sp.u * sp.u + sp.w * sp.w);
    return sp;
}

// The field a point is over, or nearest (by how many of its own sizes away
// it is); off the shell (past its rim) the hand is round the bottom, at the gu.
inline Spot spotAt(float x, float y) {
    if (x * x + y * y > 1.0f) {
        Spot sp;
        sp.field = kGu;
        return sp;
    }
    Spot best;
    best.rho = 1e9f;
    for (int n = 0; n < kFields; ++n) {
        const Spot sp = spotOn(n, x, y);
        if (sp.rho < best.rho)
            best = sp;
    }
    return best;
}

// How far from a field's middle a strike can be said to be (in its rims)
// in a hit's 0..1 position: a shoulder tap between fields is still near
// one of them.
constexpr float kSpotReach = 3.0f;

// --- the numbers ------------------------------------------------------------------------------------------
//
// SI units. Sources in docs/hang.md.

struct Physics {
    // Steel and air.
    static constexpr float kSteel = 7850.0f;   // kg/m³
    static constexpr float kThick = 0.00093f;  // m, the Hang's shell after hammering
    static constexpr float kRhoAir = 1.2f;     // kg/m³
    static constexpr float kC = 343.0f;        // m/s
    static constexpr float kRadius = 0.265f;   // m, the shell's
    // The cavity and its port.
    static constexpr float kVolume = 0.025f;   // m³
    static constexpr float kGuRadius = 0.041f; // m, a handpan blank's 82 mm port …
    static constexpr float kGuNeck = 0.019f;   // m, … and the lip folded into it
    // The maker sizes the gu to the ding: 85 Hz under a 148 Hz ding, free
    // (on a lap, nearer an octave below).
    static constexpr float kGuRatio = 85.0f / 148.0f;
    static constexpr float kGuQ = 25.0f;       // radiation and the lip's viscous loss
    // The gu's lip, a bell: D5, F5, F#5 over the Integral Hang's D3 ding.
    static constexpr float kGuBell[3] = {4.0f, 4.757f, 5.04f};
    // A palm across the gu.
    static constexpr float kPalmMass = 0.04f;
    static constexpr float kPalmPulp = 1.2e5f;   // N/m^1.5
    // How deep a dimple's dome is: the curvature that makes the volume it
    // sweeps grow faster than its swing.
    static constexpr float kDomeDepth = 0.004f; // m
};

// The model's constants that measured spectra and decays set (docs/hang.md
// says which measurement each one answers to).
struct Knobs {
    static inline float beta = 0.05f, gamma = 0.3f, soft = 0.7f;  // the dimple's curvature
    static inline float shell = 2.0e3f;  // the shoulder's shared motion, s⁻²
    static inline float pair = 0.0015f;     // coincident partials of two fields
    static inline float pairApart = 20.0f;  // cents a maker leaves between two fields' shared partials
    static inline float cross = 0.05f;      // a field's fundamental driving the field an octave above it
    static inline float radOctave = 0.65f, radFifth = 1.0f, radUpper = 0.08f, radShoulder = 0.03f, radBell = 0.2f;
    static inline float dome = 1.0f;     // the swept volume's curvature
    static inline float tick = 1.0f;     // the contact force's own radiation
    static inline float air = 0.5f;      // the skin's rasp on the steel while it presses
    static inline float level = 0.195f;
    static inline float guPush = 0.06f;
    static inline float pulp = 3.0e5f, fingerMass = 0.003f, retract = 0.003f;
    static inline float skin = 3.0e6f, pulpDepth = 0.00025f; // the bone under the pulp, and how much pulp there is
    static inline float octaveOffset = 0.1f, lobe = 0.45f; // the octave's lobes are unequal: its middle moves a little
};

// What a mode is.
enum Kind : uint8_t { Fundamental, Octave, Fifth, Upper, Shoulder, Plug, Bell };

// --- the body ---------------------------------------------------------------------------------------------

class Body {
public:
    static constexpr int kMax = 96;
    static constexpr int kContacts = 24;
    static constexpr int kUpper = 3;     // untuned modes above the fifth, per field
    static constexpr int kShoulder = 14; // the shoulder's own modes
    static constexpr int kPairs = 32;
    static constexpr int kGroups = kFields + 1; // each field, and the rest (shoulder, gu)
    static constexpr int kDelay = 128;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        build();
        silence();
    }

    // The fundamentals of the ding and the eight fields, in Hz. Retuning
    // moves the modes under whatever is ringing, as a different hang would.
    void tune(const float *hz) {
        bool same = m_tuned;
        for (int n = 0; n < kFields; ++n)
            same = same && std::fabs(hz[n] - m_f0[n]) <= m_f0[n] * 1e-5f;
        if (same)
            return;
        for (int n = 0; n < kFields; ++n)
            m_f0[n] = std::clamp(hz[n], 40.0f, 4000.0f);
        m_tuned = true;
        retune();
    }

    // Shaping, all of it as a hand would: longer (or shorter) rings, as a
    // drier or wetter hang, by a power of two.
    void setDecay(float powerOfTwo) {
        const float s = std::exp2(std::clamp(powerOfTwo, -3.0f, 3.0f));
        if (std::fabs(s - m_decayScale) < 1e-4f)
            return;
        m_decayScale = s;
        retune();
    }

    // A hand strikes field n (or kGu) at u, w in the field's frame (1 at
    // its rim, further out on the shoulder), at velocity vel 0..1; tone −1
    // (the soft pad of the finger) … 1 (the hard tip). delay: samples into
    // the next block.
    void strike(int n, float vel, float u, float w, float tone, int delay) {
        Contact *c = free(nullptr);
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            retune();
        }
        vel = std::clamp(vel, 0.02f, 1.0f);
        tone = std::clamp(tone, -1.0f, 1.0f);
        *c = Contact{};
        c->on = true;
        c->delay = std::max(0, delay);
        if (n == kGu) {
            // The palm across the gu: the bottom shell round it, the lip's
            // bell, and the air in the port at its edge.
            c->mass = Physics::kPalmMass;
            c->pulp = Physics::kPalmPulp * std::exp2(tone);
            c->v = 0.03f + 0.3f * std::pow(vel, 1.3f);
            c->retract = c->v / 0.006f;
            c->pan = 0;
            add(*c, m_plug, Knobs::guPush);
            for (int j = 0; j < 3; ++j)
                add(*c, m_bell0 + j, j == 0 ? 0.5f : 0.35f);
            for (int j = 0; j < kShoulder; ++j)
                add(*c, m_shoulder0 + j, 0.25f * m_shoulderPhiGu[j]);
            add(*c, m_first[0], 0.04f); // the ding, a little, through the shell
        } else {
            n = std::clamp(n, 0, kFields - 1);
            const float rho = std::sqrt(u * u + w * w);
            // The fingertip: stiffer (the tip rather than the pad) and
            // quicker the harder it is played; the hand pulls back on its
            // own a few milliseconds after the tip arrives, a soft stroke as
            // unhurriedly as a hard one.
            c->mass = Knobs::fingerMass;
            c->pulp = Knobs::pulp * std::exp2(1.5f * tone);
            c->v = 0.02f + 1.28f * std::pow(vel, 1.5f);
            c->retract = c->v / Knobs::retract;
            const int f = m_first[n];
            for (int k = 0; k < m_count[n]; ++k)
                add(*c, f + k, modeShape(m_kind[f + k], k, u, w));
            // The field sits in the shoulder: struck there (past its rim)
            // the shoulder rings, struck in its middle hardly.
            // (Struck inside the rim, the shoulder is shaken where the field
            // meets it, toward the strike.)
            const float out = std::clamp((rho - 0.6f) / 0.8f, 0.0f, 1.0f);
            const Field fd = field(n);
            const float c0 = std::cos(fd.axis), s0 = std::sin(fd.axis);
            const float ru = rho < 1e-3f ? 1.0f : rho < 1.0f ? u / rho : u, rw = rho < 1e-3f ? 0.0f : rho < 1.0f ? w / rho : w;
            const float hx = fd.x + ru * fd.a * c0 - rw * fd.b * s0;
            const float hy = fd.y + ru * fd.a * s0 + rw * fd.b * c0;
            for (int j = 0; j < kShoulder; ++j)
                add(*c, m_shoulder0 + j, (0.15f + 0.85f * out) * shoulderShape(j, hx, hy));
            c->pan = std::clamp(hx * 0.8f, -0.8f, 0.8f);
            // Pressed far enough, the pulp is used up and the bone behind
            // the skin meets the steel: a far stiffer spring, the click of a
            // hard stroke (sooner with the tip than with the soft pad).
            c->bone = Knobs::skin * std::exp2(0.5f * tone);
            c->boneAt = Knobs::pulpDepth * std::exp2(-0.5f * tone);
        }
        m_sleeping = false;
        m_touching = true;
        m_quiet = 0;
    }

    // Adds the instrument's sound into l and r (n ≤ the block).
    void render(float *l, float *r, int n) {
        if (m_sleeping)
            return;
        const int N = m_n;
        const float kCav = m_kCavUse, kSh = m_kShell;
        float *x = m_x, *u = m_u, *f = m_force;
        for (int i = 0; i < n; ++i) {
            // The cavity's pressure and the shoulder's displacement, from
            // every mode at once.
            float dv = 0, sh = 0;
            for (int k = 0; k < N; ++k) {
                dv += m_area[k] * x[k];
                sh += m_bus[k] * x[k];
            }
            const float pressure = -kCav * dv; // Pa: the cavity grows, its pressure falls
            const float shoulder = kSh * sh;
            for (int k = 0; k < N; ++k)
                f[k] = m_area[k] * pressure - m_bus[k] * shoulder;
            // Partials of two fields that fall together drive each other;
            // a field's fundamental drives the fundamental of the field an
            // octave above through the curved steel between them, as its
            // own octave (U = η·xa²·xb), harder the harder it swings.
            for (int p = 0; p < m_pairCount; ++p) {
                const Pair &q = m_pairs[p];
                const float xa = x[q.a], xb = x[q.b];
                f[q.a] -= q.k * xb;
                f[q.b] -= q.k * xa;
            }
            for (int p = 0; p < m_crossCount; ++p) {
                const Pair &q = m_cross[p];
                const float xa = x[q.a], xb = x[q.b];
                f[q.a] -= 2.0f * q.k * xa * xb;
                f[q.b] -= q.k * xa * xa;
            }
            // The dimples' curvature: each field's fundamental drives its
            // octave and fifth (U = β·x1²·x2 + γ·x1·x2·x3) and softens as it
            // swings wide (a steelpan's sag in pitch). And the volume a dome
            // sweeps grows with the square of its swing.
            float gL[kGroups] = {}, gR[kGroups] = {};
            for (int fld = 0; fld < kFields; ++fld) {
                const int a = m_first[fld];
                const float x1 = x[a], x2 = x[a + 1], x3 = x[a + 2];
                const float be = m_beta[fld], ga = m_gamma[fld];
                const float q = x1 * m_invThick;
                f[a] += m_soft[fld] * x1 * q * q / (1.0f + 4.0f * q * q) - 2.0f * be * x1 * x2 - ga * x2 * x3;
                f[a + 1] -= be * x1 * x1 + ga * x1 * x3;
                f[a + 2] -= ga * x1 * x2;
                const float s = x1 + x2 + x3, sv = u[a] + u[a + 1] + u[a + 2];
                gL[fld] += m_domeL[fld] * s * sv;
                gR[fld] += m_domeR[fld] * s * sv;
            }
            // The hands still touching, and the force they make.
            float tickL = 0, tickR = 0, rasp = 0;
            if (m_touching)
                contacts(f, tickL, tickR, rasp);
            float restL = 0, restR = 0;
            for (int k = 0; k < N; ++k) {
                u[k] = m_d[k] * u[k] - m_c[k] * x[k] + m_inv[k] * f[k];
                x[k] += u[k];
                restL += m_restL[k] * u[k];
                restR += m_restR[k] * u[k];
            }
            for (int fld = 0; fld < kFields; ++fld) {
                const int a = m_first[fld];
                gL[fld] += m_gl[a] * u[a] + m_gl[a + 1] * u[a + 1];
                gR[fld] += m_gr[a] * u[a] + m_gr[a + 1] * u[a + 1];
            }
            gL[kFields] += restL;
            gR[kFields] += restR;
            // Each part of the instrument reaches each microphone after its
            // own time of flight, so two fields ringing at one frequency
            // don't cancel at both ears.
            // (The monopoles only: they are what could cancel. The rest is
            // heard from the middle.)
            float ql = gL[kFields], qr = gR[kFields];
            m_wr = (m_wr + 1) & (kDelay - 1);
            for (int g = 0; g < kFields; ++g) {
                m_lineL[g][m_wr] = gL[g];
                m_lineR[g][m_wr] = gR[g];
                ql += tap(m_lineL[g], m_delayL[g]);
                qr += tap(m_lineR[g], m_delayR[g]);
            }
            // Pressure follows the volume acceleration; the force under a
            // finger radiates as from an infinite plate (its volume
            // acceleration F/ρh), above where the plate's own modes speak.
            // And the skin's ridges rasp on the steel while they press:
            // the air above 5 kHz in a fingertip's touch.
            const float air = m_rasp.step(rasp * m_tickGain * Knobs::air).high;
            const float tl = m_tickL.step(tickL * m_tickGain).high + air, tr = m_tickR.step(tickR * m_tickGain).high + air;
            l[i] += (ql - m_ql) * m_gain + tl;
            r[i] += (qr - m_qr) * m_gain + tr;
            m_ql = ql;
            m_qr = qr;
        }
        // Asleep once it has rung out (and no hand is on it).
        float e = 0;
        for (int k = 0; k < N; ++k)
            e += m_loud[k] * (m_c[k] * x[k] * x[k] + u[k] * u[k]);
        m_energy = e;
        if (!m_touching && e < m_sleepEnergy) {
            if (++m_quiet > 8)
                silence();
        } else {
            m_quiet = 0;
        }
    }

    // A hand laid flat on it: everything dies within about `seconds`, until
    // it next sleeps or is struck.
    void mute(float seconds) {
        if (m_sleeping)
            return;
        m_muteD = std::exp(-2.0f / (std::max(seconds, 0.001f) * 0.25f * m_rate));
        for (int k = 0; k < m_n; ++k)
            m_d[k] = std::min(m_d[k], m_muteD);
    }

    void silence() {
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            retune();
        }
        for (int k = 0; k < kMax; ++k)
            m_x[k] = m_u[k] = m_force[k] = 0;
        for (auto &c : m_contacts)
            c.on = false;
        m_touching = false;
        m_ql = m_qr = 0;
        for (int g = 0; g < kGroups; ++g)
            for (int i = 0; i < kDelay; ++i)
                m_lineL[g][i] = m_lineR[g][i] = 0;
        m_tickL.reset();
        m_rasp.reset();
        m_tickR.reset();
        m_sleeping = true;
        m_energy = 0;
        m_quiet = 0;
    }

    bool active() const { return !m_sleeping; }
    // Loudness, roughly 0..1, for drawing.
    float level() const { return std::sqrt(std::max(0.0f, m_energy) / m_refEnergy); }
    float rate() const { return m_rate; }

    // --- for tests and the docs: the instrument as built --------------------------------------------------

    int modes() const { return m_n; }
    // The as-tuned frequency and the decay time constant of a field's mode
    // k (0 fundamental, 1 octave, 2 fifth, 3… the upper ones).
    float modeHz(int n, int k) const { return m_hz[m_first[std::clamp(n, 0, kFields - 1)] + k]; }
    float modeTau(int n, int k) const { return m_tau[m_first[std::clamp(n, 0, kFields - 1)] + k]; }
    float bellHz(int j) const { return m_hz[m_bell0 + std::clamp(j, 0, 2)]; }
    // The gu's Helmholtz frequency by the rigid-walled formula.
    float guHz() const { return m_hz[m_plug]; }
    // Helmholtz's formula for this cavity and a port of this radius.
    static float helmholtzHz(float radius = Physics::kGuRadius) {
        const float S = kPi * radius * radius;
        return Physics::kC / kTwoPi * std::sqrt(S / (Physics::kVolume * neckLength(radius)));
    }
    // The gu's port, as the air in it moves: the neck plus an end
    // correction for each side (flanged inside, the shell outside).
    static float neckLength(float radius = Physics::kGuRadius) { return Physics::kGuNeck + 1.7f * radius; }
    // The gu's radius that puts Helmholtz's frequency at hz (bisection: the
    // frequency grows with the radius).
    static float guRadiusFor(float hz) {
        float lo = 0.01f, hi = 0.12f;
        for (int i = 0; i < 40; ++i) {
            const float mid = 0.5f * (lo + hi);
            (helmholtzHz(mid) < hz ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    }
    float guRadius() const { return m_guRadius; }
    // Where the gu sounds, the shell's give included.
    float guHeardHz() const { return m_guHeard; }
    // How far a field's mode k swings now (m), from its displacement and
    // velocity: what a sensor glued to it would read, beats and all.
    float swing(int n, int k) const { return swingOf(m_first[std::clamp(n, 0, kFields - 1)] + k); }
    float guSwing() const { return swingOf(m_plug); }
    // How long the last strike's finger touched the steel, in seconds.
    float lastContact() const { return m_lastContact; }
    // The normalised coupling of a field's fundamental to the gu's air,
    // g / (ω₁ω₂): the share of their frequencies the two would split by if
    // they were tuned together.
    float airCoupling(int n) const {
        const int k = m_first[std::clamp(n, 0, kFields - 1)];
        const float g = m_kCav * m_area[k] * m_area[m_plug] / std::sqrt(m_mass[k] * m_mass[m_plug]);
        const float w1 = kTwoPi * m_hz[k], w2 = kTwoPi * m_hz[m_plug];
        return g / (w1 * w2);
    }
    // How many pairs of coincident partials the shoulder couples.
    int pairs() const { return m_pairCount; }
    // For tests: turns the cavity off (an open-backed hang) or on.
    void setAir(bool on) {
        m_air = on;
        retune();
    }

private:
    struct Contact {
        bool on = false;
        int delay = 0;
        int age = 0, touched = 0;
        float z = 0, v = 0; // the fingertip's travel past the resting surface, inward, and its speed
        float mass = 0.005f, pulp = 1e6f, retract = 500.0f;
        float pan = 0;
        float bone = 0, boneAt = 1; // the bone's spring, once the pulp is pressed this far (m)
        int count = 0;
        int idx[kFields + kShoulder + 8] = {};
        float phi[kFields + kShoulder + 8] = {};
    };

    struct Pair {
        int a = 0, b = 0;
        float k = 0; // N/m
    };

    // A contact not in use (but not `besides`); with ten hands' worth at
    // once, the oldest, which has surely left.
    Contact *free(const Contact *besides) {
        for (auto &k : m_contacts)
            if (!k.on && &k != besides)
                return &k;
        Contact *c = nullptr;
        for (auto &k : m_contacts)
            if (&k != besides && (!c || k.age > c->age))
                c = &k;
        return c;
    }

    void add(Contact &c, int mode, float phi) {
        if (c.count < int(sizeof(c.idx) / sizeof(c.idx[0])) && std::fabs(phi) > 1e-4f) {
            c.idx[c.count] = mode;
            c.phi[c.count] = phi;
            ++c.count;
        }
    }

    // A delay line read `d` samples back (fractional, linearly).
    float tap(const float *line, float d) const {
        const int i = int(d);
        const float t = d - float(i);
        const float a = line[(m_wr - i) & (kDelay - 1)], b = line[(m_wr - i - 1) & (kDelay - 1)];
        return a + t * (b - a);
    }

    float swingOf(int i) const {
        const float th = std::max(kTwoPi * m_hz[i] / m_rate, 1e-4f);
        return std::sqrt(m_x[i] * m_x[i] + (m_u[i] / th) * (m_u[i] / th));
    }

    // The contact forces this sample: each finger against the surface
    // under it (outward displacement x; the finger presses inward).
    void contacts(float *f, float &tickL, float &tickR, float &rasp) {
        bool any = false;
        for (auto &c : m_contacts) {
            if (!c.on)
                continue;
            any = true;
            if (c.delay > 0) {
                --c.delay;
                continue;
            }
            float surface = 0; // inward
            for (int j = 0; j < c.count; ++j)
                surface -= c.phi[j] * m_x[c.idx[j]];
            // The fingertip arrives where the steel is now, not where it
            // rests: a field already ringing is met mid-swing.
            if (c.age == 0)
                c.z = surface;
            ++c.age;
            const float delta = c.z - surface;
            float force = 0;
            if (delta > 0) {
                force = c.pulp * delta * std::sqrt(delta);
                if (delta > c.boneAt) {
                    const float b = delta - c.boneAt;
                    force += c.bone * b * std::sqrt(b);
                }
                for (int j = 0; j < c.count; ++j)
                    f[c.idx[j]] -= c.phi[j] * force;
                c.touched = c.age;
                tickL += force * std::sqrt(0.5f * (1.0f - c.pan));
                tickR += force * std::sqrt(0.5f * (1.0f + c.pan));
                rasp += force * force * (1.0f / 20.0f) * m_noise.bipolar(); // rubs harder the harder it presses
            }
            c.v -= (force / c.mass + c.retract) * m_dt;
            c.z += c.v * m_dt;
            // Gone once it is off the surface and moving away, or after
            // 40 ms whatever happens.
            if ((delta <= 0 && c.v < 0 && c.age > 4) || c.age > int(0.04f * m_rate)) {
                c.on = false;
                m_lastContact = float(c.touched) / m_rate;
            }
        }
        m_touching = any;
    }

    // How strongly a strike at u, w drives mode k of a field: its mode
    // shape there. The fundamental is a dome over the whole field; the
    // octave has a nodal line across the middle (lobes at the ends of the
    // long axis), the fifth one along it (lobes across); the upper modes
    // more lobes still. A field's rim is not clamped: its modes carry on a
    // little way into the shoulder and die away there.
    static float modeShape(Kind kind, int k, float u, float w) {
        const float rho = std::sqrt(u * u + w * w);
        const float in = std::min(rho, 1.0f);
        const float uc = rho > 1.0f ? u / rho : u, wc = rho > 1.0f ? w / rho : w;
        const float skirt = rho <= 1.0f ? 1.0f : std::exp(-(rho - 1.0f) * 2.5f);
        constexpr float s = 0.8f; // the rim is not a node
        const float dome = std::cos(0.5f * kPi * s * in);
        switch (kind) {
        case Fundamental: return dome * skirt;
        // (A fingertip's patch is wide against a lobe, so it reaches a lobe
        // only partly: the octave and fifth spots gain several dB, not tens.)
        case Octave:
            return (Knobs::octaveOffset * dome + Knobs::lobe * std::sin(kPi * s * uc) * std::cos(0.5f * kPi * s * wc)) * skirt;
        case Fifth: return Knobs::lobe * std::sin(kPi * s * wc) * std::cos(0.5f * kPi * s * uc) * skirt;
        default: {
            // Higher lobes: (2,0), (1,1), (0,2)-like.
            const int j = k - 3;
            if (j == 0)
                return std::cos(1.5f * kPi * s * uc) * std::cos(0.5f * kPi * s * wc) * skirt;
            if (j == 1)
                return std::sin(kPi * s * uc) * std::sin(kPi * s * wc) * skirt;
            return std::cos(1.5f * kPi * s * wc) * std::cos(0.5f * kPi * s * uc) * skirt;
        }
        }
    }

    // The shoulder's modes over the whole top shell: so many lobes round,
    // so many across.
    float shoulderShape(int j, float x, float y) const {
        const float r = std::sqrt(x * x + y * y);
        const float th = std::atan2(y, x);
        return std::cos(float(m_shoulderM[j]) * th + m_shoulderPh[j]) *
               std::sin(kPi * std::min(r, 1.0f) * m_shoulderK[j]);
    }

    // The instrument's parts, once: modes, masses, areas, shapes, and where
    // each is heard from.
    void build() {
        const float hz0[kFields] = {146.8f, 220.0f, 233.1f, 261.6f, 293.7f, 329.6f, 349.2f, 392.0f, 440.0f};
        // Two microphones either side of the player, a little in front: a
        // dipole is heard by each with the sign of the lobe that faces it.
        const float micL[2] = {-0.7f, -0.7f}, micR[2] = {0.7f, -0.7f};
        Rng sign(0x51a7u);
        int k = 0;
        for (int n = 0; n < kFields; ++n) {
            m_first[n] = k;
            const Field fd = field(n);
            const float a = fd.a * Physics::kRadius, b = fd.b * Physics::kRadius;
            const float area = kPi * a * b;
            const float plate = Physics::kSteel * Physics::kThick * area;
            const float ca = std::cos(fd.axis), sa = std::sin(fd.axis);
            for (int m = 0; m < 3 + kUpper; ++m, ++k) {
                m_field[k] = uint8_t(n);
                m_kind[k] = m == 0 ? Fundamental : m == 1 ? Octave : m == 2 ? Fifth : Upper;
                // Modal mass: ∫φ² over the field (0.297 of its plate for
                // the dome, a quarter for the lobed modes).
                m_mass[k] = plate * (m == 0 ? 0.297f : 0.25f);
                // The volume each sweeps per metre of swing: the dome 0.463
                // of the field's area; the octave's unequal lobes a little
                // (it radiates as a monopole); the fifth's lobes cancel (a
                // dipole), the upper modes' nearly.
                m_area[k] = m == 0 ? 0.463f * area : m == 1 ? 0.13f * area : m == 2 ? 0.01f * area : 0.004f * area;
                // Each microphone: the level of the field's side of the
                // instrument, and for a dipole the facing lobe's sign.
                float dl = 0, dr = 0;
                if (m == 1 || m == 2) {
                    const float dx = m == 1 ? ca : -sa, dy = m == 1 ? sa : ca;
                    dl = dx * micL[0] + dy * micL[1];
                    dr = dx * micR[0] + dy * micR[1];
                } else if (m >= 3) {
                    dl = sign.bipolar();
                    dr = sign.bipolar();
                }
                m_micL[k] = std::sqrt(0.5f * (1.0f - 0.6f * fd.x));
                m_micR[k] = std::sqrt(0.5f * (1.0f + 0.6f * fd.x));
                m_dipL[k] = dl;
                m_dipR[k] = dr;
            }
            m_count[n] = 3 + kUpper;
        }
        // The shoulder: broad inharmonic modes of the shell between the
        // fields, heavier and quickly damped where the shells are glued.
        m_shoulder0 = k;
        {
            Rng rng(0x5ba11u);
            for (int j = 0; j < kShoulder; ++j, ++k) {
                m_field[k] = 0;
                m_kind[k] = Shoulder;
                m_mass[k] = Physics::kSteel * Physics::kThick * kPi * Physics::kRadius * Physics::kRadius * 0.08f;
                m_area[k] = 0;
                m_shoulderM[j] = 1 + j % 5;
                m_shoulderK[j] = 1.0f + 0.5f * float(j / 5) + 0.3f * rng.unit();
                m_shoulderPh[j] = kTwoPi * rng.unit();
                m_micL[k] = m_micR[k] = std::sqrt(0.5f);
                m_dipL[k] = rng.bipolar();
                m_dipR[k] = rng.bipolar();
                m_shoulderPhiGu[j] = rng.bipolar();
            }
        }
        // The gu's lip, a small steel bell.
        m_bell0 = k;
        for (int j = 0; j < 3; ++j, ++k) {
            m_field[k] = 0;
            m_kind[k] = Bell;
            m_mass[k] = 0.03f;
            m_area[k] = 0;
            m_micL[k] = m_micR[k] = std::sqrt(0.5f);
            m_dipL[k] = j == 1 ? 0.6f : 0.0f;
            m_dipR[k] = j == 1 ? -0.6f : 0.0f;
        }
        // The air in the gu.
        m_plug = k;
        {
            const float S = kPi * Physics::kGuRadius * Physics::kGuRadius;
            m_field[k] = 0;
            m_kind[k] = Plug;
            m_mass[k] = Physics::kRhoAir * S * neckLength();
            m_area[k] = S;
            m_micL[k] = m_micR[k] = std::sqrt(0.5f);
            m_dipL[k] = m_dipR[k] = 0;
            ++k;
        }
        m_n = k;
        for (int i = 0; i < m_n; ++i)
            m_group[i] = uint8_t(m_kind[i] == Fundamental || m_kind[i] == Octave ? m_field[i] : kFields);
        // The microphones 35 cm apart, 45 cm in front: each part's
        // time of flight to each, less the nearest's.
        {
            const float mic[2][2] = {{-0.175f, -0.45f}, {0.175f, -0.45f}};
            float delay[2][kGroups];
            for (int e = 0; e < 2; ++e) {
                float least = 1e9f;
                for (int g = 0; g < kGroups; ++g) {
                    const Field fd = field(std::min(g, kFields - 1));
                    const float px = g < kFields ? fd.x * Physics::kRadius : 0.0f, py = g < kFields ? fd.y * Physics::kRadius : 0.0f;
                    delay[e][g] = std::hypot(mic[e][0] - px, mic[e][1] - py) / Physics::kC * m_rate;
                    least = std::min(least, delay[e][g]);
                }
                for (int g = 0; g < kGroups; ++g)
                    (e == 0 ? m_delayL : m_delayR)[g] = std::clamp(delay[e][g] - least, 0.0f, float(kDelay - 2));
            }
        }
        m_kCav = Physics::kRhoAir * Physics::kC * Physics::kC / Physics::kVolume;
        for (int n = 0; n < kFields; ++n)
            m_f0[n] = hz0[n];
        m_tuned = false;
        m_tickL.set(1000.0f, 0.6f, m_rate);
        m_tickR.set(1000.0f, 0.6f, m_rate);
        m_rasp.set(5000.0f, 0.6f, m_rate);
        retune();
    }

    // Frequencies, decays and couplings from the fundamentals.
    void retune() {
        Rng rng(0x4a46u); // this instrument's own small mistunings
        for (int n = 0; n < kFields; ++n) {
            const float f0 = m_f0[n];
            const int a = m_first[n];
            // A maker's accuracy: the fundamental within a couple of cents,
            // the octave and fifth within five or so (all real ones are
            // within 13).
            const float cents[3] = {3.0f * (rng.unit() - 0.5f), 10.0f * (rng.unit() - 0.5f), 14.0f * (rng.unit() - 0.5f)};
            // The modes above the fifth, untuned, near the third octave: the
            // ding's next mode is at 9.1×, a field's (2,1) pair at 7.5× and 8.2×.
            const float upper[kUpper] = {7.5f + 0.15f * rng.unit(), 8.15f + 0.15f * rng.unit(), 9.05f + 0.1f * rng.unit()};
            for (int m = 0; m < m_count[n]; ++m) {
                const int k = a + m;
                const float ratio = m < 3 ? float(m + 1) * std::exp2(cents[m] / 1200.0f) : upper[m - 3];
                m_hz[k] = f0 * ratio;
                m_tau[k] = decayOf(n, m, m_hz[k]);
            }
            // The curvature's coupling, as stiff as the dome: a strike that
            // moves the fundamental by a fraction of the shell's thickness
            // drives the octave noticeably.
            const float k1 = m_mass[a] * std::pow(kTwoPi * f0, 2.0f);
            m_beta[n] = Knobs::beta * k1 / Physics::kThick;
            m_gamma[n] = Knobs::gamma * k1 / Physics::kThick;
            m_soft[n] = Knobs::soft * k1;
        }
        // Partials of two fields that fall together (within 35 cents) are
        // coupled pairwise: κ·√(K_a·K_b), which splits a pair by about κ of
        // its frequency and leaves its mean where it was. A maker leaves
        // such partners a few cents apart, so they trade only some of their
        // energy, a few times a second: the beating measured on hangs.
        m_pairCount = 0;
        float gave[kFields] = {};
        for (int a = 0; a < kFields; ++a)
            for (int b = a + 1; b < kFields; ++b)
                for (int ma = 0; ma < 3; ++ma)
                    for (int mb = 0; mb < 3; ++mb) {
                        const int ka = m_first[a] + ma, kb = m_first[b] + mb;
                        const float apart = 1200.0f * std::log2(m_hz[kb] / m_hz[ka]);
                        if (std::fabs(apart) > 35.0f || m_pairCount >= kPairs)
                            continue;
                        if (std::fabs(apart) < Knobs::pairApart) {
                            // The fundamentals are what the maker tunes
                            // closest: an overtone gives way.
                            // (above it, where a hard strike's sag in pitch
                            // carries the fundamental away from it)
                            const float side = 1.0f;
                            // (never further than 12 cents from its own
                            // fundamental's; what is still missing, the
                            // fundamental gives, up to 5 cents flat)
                            auto place = [&](int k, int fund, float sgn, float own) {
                                const float want = 1200.0f * std::log2(m_hz[fund] / own) + sgn * Knobs::pairApart;
                                const float got = std::clamp(want, -12.0f, 12.0f);
                                m_hz[k] = own * std::exp2(got / 1200.0f);
                                // (the whole field a little flat, so its own
                                // partials keep their ratios)
                                const int fld = m_field[fund];
                                const float short_ = std::clamp(want - got, 0.0f, 5.0f - gave[fld]);
                                gave[fld] += short_;
                                for (int j = 0; j < m_count[fld]; ++j)
                                    m_hz[m_first[fld] + j] *= std::exp2(-short_ / 1200.0f);
                            };
                            if (ma == 0 && mb > 0)
                                place(kb, ka, side, m_hz[m_first[b]] * float(mb + 1));
                            else if (mb == 0 && ma > 0)
                                place(ka, kb, side, m_hz[m_first[a]] * float(ma + 1));
                            // (two overtones are left as their own
                            // tunings put them)
                        }
                        const float Ka = m_mass[ka] * std::pow(kTwoPi * m_hz[ka], 2.0f);
                        const float Kb = m_mass[kb] * std::pow(kTwoPi * m_hz[kb], 2.0f);
                        m_pairs[m_pairCount++] = {ka, kb, Knobs::pair * std::sqrt(Ka * Kb)};
                    }
        // Fields an octave apart, fundamental to fundamental (the ding and
        // D4, A3 and A4 in the classic hang): the 1:2 internal resonance
        // steelpan studies measure across notes.
        m_crossCount = 0;
        for (int a = 0; a < kFields; ++a)
            for (int b = 0; b < kFields; ++b) {
                const int ka = m_first[a], kb = m_first[b];
                if (a == b || m_crossCount >= kPairs ||
                    std::fabs(1200.0f * std::log2(m_hz[kb] / (2.0f * m_hz[ka]))) > 35.0f)
                    continue;
                const float kB = m_mass[kb] * std::pow(kTwoPi * m_hz[kb], 2.0f);
                m_cross[m_crossCount++] = {ka, kb, Knobs::cross * kB / Physics::kThick};
            }
        // The shoulder's modes, inharmonic, from about 600 Hz up.
        {
            Rng srng(0x5110u);
            float hz = 560.0f;
            for (int j = 0; j < kShoulder; ++j) {
                const int k = m_shoulder0 + j;
                hz *= 1.17f + 0.12f * srng.unit();
                m_hz[k] = hz;
                m_tau[k] = std::clamp(0.12f * std::pow(1000.0f / hz, 0.7f), 0.012f, 0.2f);
            }
        }
        // The gu's lip, over the ding as on the Integral Hang.
        for (int j = 0; j < 3; ++j) {
            m_hz[m_bell0 + j] = m_f0[0] * Physics::kGuBell[j];
            m_tau[m_bell0 + j] = 1.4f / 6.91f;
        }
        // The gu: sized to the ding. The shell gives under the cavity's
        // pressure, every field's dome a little, so the gu sounds below the
        // rigid-walled formula (damping one field raises a hang's from 85 to
        // about 90 Hz): sized so it sounds where the maker wants it.
        {
            const float heard = Physics::kGuRatio * m_f0[0];
            float rigid = heard;
            for (int it = 0; it < 4 && m_air; ++it) {
                const float r = guRadiusFor(rigid), S = kPi * r * r;
                const float M = Physics::kRhoAir * S * neckLength(r);
                const float wH2 = std::pow(kTwoPi * rigid, 2.0f);
                float shift = 0;
                for (int n = 0; n < kFields; ++n)
                    for (int m = 0; m < 2; ++m) {
                        const int k = m_first[n] + m;
                        const float g = m_kCav * m_area[k] * S / std::sqrt(m_mass[k] * M);
                        shift += g * g / (std::pow(kTwoPi * m_hz[k], 2.0f) - wH2);
                    }
                rigid *= heard / (std::sqrt(std::max(wH2 - shift, 1.0f)) / kTwoPi);
            }
            m_guHeard = heard;
            m_guRadius = guRadiusFor(rigid);
            const float S = kPi * m_guRadius * m_guRadius;
            m_area[m_plug] = S;
            m_mass[m_plug] = Physics::kRhoAir * S * neckLength(m_guRadius);
            const float wH = std::sqrt(m_air ? m_kCav * S * S / m_mass[m_plug] : 0.0f);
            m_hz[m_plug] = wH / kTwoPi;
            m_tau[m_plug] = Physics::kGuQ / (kPi * std::max(m_hz[m_plug], 1.0f));
        }

        // The oscillators. The cavity stiffens every mode by kCav·A²
        // (included through the coupling force, which the integrator sees);
        // the shell's own stiffness is what remains of each mode's target,
        // so a field sounds at its tuning with the air in place, as a maker
        // tunes it. The gu's air has no stiffness but the cavity's.
        const float rate = m_rate;
        for (int k = 0; k < m_n; ++k) {
            const float w = kTwoPi * std::min(m_hz[k], 0.45f * rate);
            float w2 = w * w;
            if (k == m_plug)
                w2 = 0;
            else if (m_air) {
                w2 -= m_kCav * m_area[k] * m_area[k] / m_mass[k];
                // A fundamental (or octave) is pushed up by the gu's air
                // below it: pre-compensated, so it lands in tune.
                if (m_kind[k] == Fundamental || m_kind[k] == Octave) {
                    const float g = m_kCav * m_area[k] * m_area[m_plug] / std::sqrt(m_mass[k] * m_mass[m_plug]);
                    const float wH = kTwoPi * m_hz[m_plug];
                    w2 -= g * g / std::max(w * w - wH * wH, 1.0f);
                }
            }
            w2 = std::max(w2, 1.0f);
            m_bus[k] = 0;
            m_inv[k] = m_dt * m_dt / m_mass[k];
            // Exact frequency and decay: u' = d·u − c·x, x' = x + u' has
            // poles r·e^{±iθ} for d = r², c = 1 + r² − 2r·cos θ.
            const bool fixed = k == m_plug || m_kind[k] == Shoulder || m_kind[k] == Bell;
            const float tau = m_tau[k] * (fixed ? 1.0f : m_decayScale);
            const float r = std::exp(-1.0f / (tau * rate));
            const float th = std::sqrt(w2) / rate;
            m_d[k] = r * r;
            m_c[k] = 1.0f + r * r - 2.0f * r * std::cos(th);
            if (k == m_plug)
                m_c[k] = 1.0f + r * r - 2.0f * r; // no spring of its own
        }

        // The shoulder's shared motion: each mode moves it by the share of
        // its field's rim it shakes (fundamentals most), and the shoulder's
        // own modes most of all. A coupling, not a stiffness: the self-term
        // is taken back out of each mode.
        for (int n = 0; n < kFields; ++n)
            for (int m = 0; m < m_count[n]; ++m) {
                const int k = m_first[n] + m;
                const float share = m == 0 ? 1.0f : m == 1 ? 0.7f : m == 2 ? 0.55f : 0.3f;
                const float sgn = ((n + m) & 1) ? -1.0f : 1.0f; // alternating round the ring
                m_bus[k] = sgn * share * std::sqrt(m_mass[k]);
            }
        for (int j = 0; j < kShoulder; ++j)
            m_bus[m_shoulder0 + j] = (j & 1 ? -0.5f : 0.5f) * std::sqrt(m_mass[m_shoulder0 + j]);
        for (int j = 0; j < 3; ++j)
            m_bus[m_bell0 + j] = 0.3f * std::sqrt(m_mass[m_bell0 + j]);
        m_kShell = Knobs::shell;
        for (int k = 0; k < m_n; ++k)
            if (m_bus[k] != 0)
                m_c[k] -= m_kShell * m_bus[k] * m_bus[k] * m_inv[k];

        // What the microphones hear: each mode's volume velocity (its
        // effective area), the lobed modes' radiation as dipoles, each from
        // where it is on the instrument.
        for (int k = 0; k < m_n; ++k) {
            float mono = 0, dip = 0;
            const float hz = m_hz[k];
            switch (m_kind[k]) {
            case Fundamental: mono = m_area[k]; break;
            // (a source small against its wavelength radiates better the
            // higher it sounds: the octave's unequal lobes as the frequency,
            // a dipole's as its square)
            case Octave:
                mono = Knobs::radOctave * fieldArea(m_field[k]) * std::clamp(std::pow(hz / 500.0f, 1.5f), 0.3f, 3.0f);
                dip = 0.3f * mono;
                break;
            case Fifth: dip = Knobs::radFifth * fieldArea(m_field[k]) * std::min(1.0f, hz / 900.0f); mono = 0.25f * dip; break;
            case Upper:
                dip = Knobs::radUpper * fieldArea(m_field[k]) * std::clamp(std::pow(hz / 2500.0f, 2.0f), 0.1f, 2.0f);
                mono = 0.3f * dip;
                break;
            case Shoulder: dip = Knobs::radShoulder * Physics::kRadius * Physics::kRadius; mono = 0.4f * dip; break;
            case Bell: mono = Knobs::radBell * Physics::kGuRadius * Physics::kGuRadius * 40.0f; dip = mono; break;
            case Plug: mono = m_air ? m_area[k] : 0.0f; break;
            default: break;
            }
            m_gl[k] = mono * m_micL[k] + dip * m_dipL[k];
            m_gr[k] = mono * m_micR[k] + dip * m_dipR[k];
            // (the fundamentals and octaves go through their field's delay)
            const bool own = m_group[k] < kFields;
            m_restL[k] = own ? 0.0f : m_gl[k];
            m_restR[k] = own ? 0.0f : m_gr[k];
            m_loud[k] = mono * mono + dip * dip;
        }
        // The domes' swept volume, second order in the swing: ΔV ≈ A·x +
        // A·x²/(2·depth); its rate, A/depth·x·ẋ, radiates with the field.
        for (int n = 0; n < kFields; ++n) {
            const Field fd = field(n);
            const float A = 0.463f * fieldArea(n) / Physics::kDomeDepth * Knobs::dome;
            m_domeL[n] = A * std::sqrt(0.5f * (1.0f - 0.6f * fd.x));
            m_domeR[n] = A * std::sqrt(0.5f * (1.0f + 0.6f * fd.x));
        }
        // Pressure ∝ ρ/(4πr)·dQ/dt; at a metre, scaled to the studio's level.
        m_gain = Knobs::level * m_rate * m_rate; // u is metres per sample: two of rate to m³/s²
        // A point force on a plate of mass ρh per area has a volume
        // acceleration F/ρh (Junger & Feit), at the same scale.
        m_tickGain = Knobs::level * Knobs::tick / (Physics::kSteel * Physics::kThick);
        // A full-strength strike on a middle field's dome, for the energy
        // scale; asleep 90 dB below it.
        {
            const int k = m_first[4];
            const float amp = 2.0e-5f; // m
            const float th = kTwoPi * m_hz[k] / m_rate;
            m_refEnergy = m_loud[k] * (m_c[k] * amp * amp + (amp * th) * (amp * th));
            m_sleepEnergy = m_refEnergy * 1e-9f;
        }
        m_kCavUse = m_air ? m_kCav : 0.0f;
        if (m_muteD < 1.0f)
            for (int k = 0; k < m_n; ++k)
                m_d[k] = std::min(m_d[k], m_muteD);
    }

    float fieldArea(int n) const {
        const Field fd = field(n);
        return kPi * fd.a * fd.b * Physics::kRadius * Physics::kRadius;
    }

    // How long each mode rings (its time constant, s). Measured on
    // handpans: T60 2.5 s on high fields to 3.2 s on low ones (0.9–5.9 s),
    // the octave and fifth about as long; the ding longest.
    static float decayOf(int n, int m, float hz) {
        const float f1 = hz / float(m < 3 ? m + 1 : 8);
        const float t60 = n == 0 ? 4.5f : std::clamp(3.2f - 0.7f * std::log2(std::max(f1, 100.0f) / 220.0f), 2.2f, 3.4f);
        const float tau1 = t60 / 6.91f;
        switch (m) {
        case 0: return tau1;
        case 1: return 1.0f * tau1;
        case 2: return 0.85f * tau1;
        default: return std::clamp(0.1f * std::pow(1500.0f / hz, 0.5f), 0.03f, 0.2f);
        }
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    int m_n = 0;
    int m_first[kFields] = {}, m_count[kFields] = {};
    int m_shoulder0 = 0, m_bell0 = 0, m_plug = 0;
    alignas(32) float m_x[kMax] = {}, m_u[kMax] = {}, m_force[kMax] = {};
    alignas(32) float m_d[kMax] = {}, m_c[kMax] = {}, m_inv[kMax] = {};
    alignas(32) float m_area[kMax] = {}, m_bus[kMax] = {}, m_gl[kMax] = {}, m_gr[kMax] = {}, m_loud[kMax] = {};
    alignas(32) float m_restL[kMax] = {}, m_restR[kMax] = {};
    float m_mass[kMax] = {}, m_hz[kMax] = {}, m_tau[kMax] = {};
    float m_micL[kMax] = {}, m_micR[kMax] = {}, m_dipL[kMax] = {}, m_dipR[kMax] = {};
    uint8_t m_field[kMax] = {}, m_group[kMax] = {};
    float m_lineL[kGroups][kDelay] = {}, m_lineR[kGroups][kDelay] = {};
    float m_delayL[kGroups] = {}, m_delayR[kGroups] = {};
    int m_wr = 0;
    Kind m_kind[kMax] = {};
    float m_beta[kFields] = {}, m_gamma[kFields] = {}, m_soft[kFields] = {};
    float m_domeL[kFields] = {}, m_domeR[kFields] = {};
    float m_invThick = 1.0f / Physics::kThick;
    int m_shoulderM[kShoulder] = {};
    float m_shoulderK[kShoulder] = {}, m_shoulderPh[kShoulder] = {}, m_shoulderPhiGu[kShoulder] = {};
    Pair m_pairs[kPairs], m_cross[kPairs];
    int m_pairCount = 0, m_crossCount = 0;
    float m_f0[kFields] = {};
    bool m_tuned = false, m_air = true;
    float m_kCav = 0, m_kCavUse = 0, m_kShell = 0;
    float m_decayScale = 1;
    float m_gain = 1, m_tickGain = 0;
    Svf m_tickL, m_tickR, m_rasp;
    Rng m_noise{0x7a5bu};
    float m_ql = 0, m_qr = 0;
    float m_energy = 0, m_refEnergy = 1, m_sleepEnergy = 0;
    int m_quiet = 0;
    float m_muteD = 1.0f;
    float m_guRadius = Physics::kGuRadius, m_lastContact = 0, m_guHeard = 85.0f;
    bool m_sleeping = true, m_touching = false;
    Contact m_contacts[kContacts];
};

}
