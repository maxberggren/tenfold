#pragma once

// A marimbula, as physics: the Caribbean box lamellophone that plays the
// bass in Cuban son and changüí and in Jamaican mento. Not samples and not
// a recipe: the instrument's own parts in SI units, integrated sample by
// sample. The measurements it is built from and checked against are in
// docs/marimbula.md (the survey behind them in docs/marimbula-research.md).
//
//   the tongues  broad strips of spring steel (old gramophone and clock
//                springs, saw blades), clamped under a bar and pressed
//                down over a bridge, their free ends pointing at the sound
//                hole. Each is an Euler–Bernoulli beam, free at its tip
//                and held at the bridge by the short length of steel
//                between bridge and bar, which resists the root turning
//                like a spring (K = 4EI/a), its root riding on a bridge
//                that gives a little. Its modes are that beam's, solved
//                when the instrument is built: a cantilever's
//                1 : 6.27 : 17.5 : 34.4 when the root is held hard,
//                stretched by a root that turns, squeezed by a bridge that
//                gives (measured tines: 5.3–6.8 for the first). Inharmonic, so
//                a maker tunes only the fundamental, by sliding the tongue
//                under its bar; the overtones fall where the steel puts
//                them. A first twisting mode rides along, woken when the
//                finger lands off the tongue's middle.
//   the bridge   a tongue does not push air: it is a strip a couple of
//                centimetres wide, a dipole far smaller than the
//                wavelengths of a bass. What it does is shake the clamp
//                board it is bolted to, and the board the front panel,
//                through the force, the moment and the twist at its root. The panel's motion shakes the tongue back
//                (its base moves), so every tongue hears the box and every
//                other tongue: the coupling is the beam's own, inertial
//                one way and elastic the other, exactly as the
//                Lagrangian of a beam on a moving base has it.
//   the box      a crate of plywood, sat on by the player, a round sound
//                hole in its front. The air in the hole is a plug of mass
//                ρ·S·L on the spring of the cavity: a Helmholtz resonator,
//                tuned by the box's volume and the hole's size into the
//                bass (the hole cut so the box sounds where a maker wants
//                it, the panels' give included). The front and back panels are orthotropic plates
//                with their own modes; the ones that change the cavity's
//                volume feel its pressure and make it, so panels and air
//                trade energy both ways, as a guitar's top and its hole do.
//                The low fundamental a player hears is that box and its
//                air, pumped by the tongue.
//   the finger    a pluck: the finger pushes the tongue's tip down a few
//                millimetres and slips off it. The tongue starts from its
//                bent shape and the finger's force goes to nothing over the
//                time it takes to slip (half a millisecond off a nail,
//                several off the soft pad), which decides how much of the
//                upper modes it wakes. A slap: the finger arrives at speed,
//                a stiffening pulp (F = K·δ^1.5) solved with the tongue's
//                own step, rides it down and slips off; struck hard
//                enough, the tongue's tip hits the panel. A finger caught
//                on a ringing tongue, or laid back on it, is a dashpot at
//                its tip that stills it. A maker tunes each tongue on its
//                box: its length is solved so that, coupled, it sounds at
//                its note.
//   the knock    the other hand on the front panel, from the flat palm to
//                the knuckles, a Hertzian contact solved with the panel's
//                step: the box's own
//                modes and its air, the drum a marimbulero plays between
//                the notes.
//   the sound    what reaches a microphone a metre in front is the volume
//                acceleration of every part that pushes air: the plug in
//                the hole and the panels' net volume (monopoles), the
//                panels' other modes weakly, the tongues as the small
//                dipoles they are, and, where a hard contact presses, the
//                force itself radiating as from an infinite plate (a
//                knuckle on wood, the tip's clack).
//
// The box rings once whatever drives it; only the tongues that sound cost
// anything, and they sleep once rung out.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>

#include "dsp.h"

namespace tf::marimbula {

constexpr int kBend = 6;    // bending modes per tongue
constexpr int kSlots = 6;   // tongues that can sound at once
constexpr int kFront = 14;  // the front panel's modes
constexpr int kBack = 4;    // the back panel's
constexpr int kPanel = kFront + kBack;

// --- the numbers -----------------------------------------------------------------------------------------
//
// SI units. Sources in docs/marimbula.md: the instrument as Thompson (1975)
// measured Caribbean marimbulas, plywood as the Finnish plywood handbook
// gives it, the box's air as Kičák (2009) measured a cajón's.

struct Physics {
    // Spring steel.
    static constexpr double kE = 200e9;       // Pa
    static constexpr double kRho = 7850;      // kg/m³
    static constexpr double kShear = 79e9;    // Pa
    // The tongues: cut from one gramophone spring, "one inch to 1¼ inch wide
    // and a little less than 1/32 inch thick", so all of one width and
    // thickness; only their length differs.
    static constexpr double kWidth = 0.028;   // m
    static constexpr double kThick = 0.00075; // m
    static constexpr double kBack = 0.022;    // m, bridge to the clamping bar
    static constexpr double kClamp = 4.0;     // the bar holds the far end of that span fast: K_r = 4EI/a
    static constexpr double kBridge = 3.0e5;  // N/m, the bridge rod pressed into the wood under the root
    static constexpr double kGap = 0.009;     // m, the tip's height over the panel at rest
    // The box, inside: "the size and the shape of a small suitcase", 58–64 ×
    // 37–47 × 19–28 cm outside; ¼-inch plywood front and back, birch.
    static constexpr double kBoxW = 0.58, kBoxH = 0.38, kBoxD = 0.20; // m: front width, height, depth
    static constexpr double kFrontThick = 0.00635, kBackThick = 0.00635;
    static constexpr double kPlyE1 = 12.7e9, kPlyE2 = 4.8e9; // Pa, bending along and across the face grain
    static constexpr double kPlyRho = 680;                   // kg/m³
    static constexpr double kHoleR = 0.06;    // m (cut per key: setBox)
    static constexpr double kRhoAir = 1.2;    // kg/m³
    static constexpr double kC = 343;         // m/s
    // Where things are on the front panel, in units of its width and height
    // from its lower left corner: the bridge low across it, the tongues
    // standing up from it, their tips at the hole's lower edge; the palm's
    // place and the knuckles' (near the top edge).
    static constexpr double kBridgeY = 0.22, kBridgeX0 = 0.28, kBridgeX1 = 0.72;
    static constexpr double kPalmX = 0.16, kPalmY = 0.50, kKnuckleX = 0.10, kKnuckleY = 0.88;
    // What else rides on the bridge: the bars, the bolts, the other tongues.
    static constexpr double kRack = 0.40;     // kg: a hardwood clamp board, bars, bolts, the other tongues
    static constexpr double kListen = 1.0;    // m, the microphone in front
};

// The constants measured decays and spectra set, or that no measurement
// does and are estimates (docs/marimbula.md says which).
struct Knobs {
    // A tongue's own losses (the clamp's friction, much the same share of
    // its energy each cycle; the steel's are negligible), as a Q for
    // every mode: the slow tail of a kalimba tine's two-stage decay (T60
    // 4–5 s at 304 Hz, Q ≈ 600) with the clamp a crude marimbula's.
    // Its first, fast stage is the box drawing energy out, which the
    // coupling makes on its own.
    static inline float tongueQ = 150.0f;
    // And a loss the same at every frequency (s⁻¹): the clamp's rubbing,
    // which takes a bass tongue's ring down to the 1–3 s the survey
    // estimates for a marimbula.
    static inline float tongueLoss = 2.5f;
    // The most a fingertip pushes with, N: a light touch to a firm press.
    static inline float fingerLight = 1.0f, fingerFirm = 7.0f;
    // The panels: Q of their modes (plywood's loss factor 0.01–0.03, the
    // joints', the floor's), and the hole's (viscous loss and radiation,
    // and the player's legs either side of it).
    static inline float panelQ = 18.0f, holeQ = 8.0f;
    // Glued edges sit between simply supported and clamped: the panels'
    // frequencies over the simply supported plate's.
    static inline float edge = 1.3f;
    // How much a panel mode without net volume still radiates, against
    // one that has it, below the coincidence frequency.
    static inline float radOdd = 0.1f;
    // The tongue as a dipole: the distance its two faces' sources sit apart.
    static inline float dipole = 0.028f;
    // The finger: a seated marimbulero plucks with the index and middle
    // fingers, pressing the tongue down and letting it slip.
    static inline float fingerMass = 0.010f;      // kg
    static inline float fingerPulp = 3.0e5f;      // N/m^1.5
    static inline float fingerAt = 0.97f;         // where it meets the tongue, of its length, at the tip …
    static inline float fingerIn = 0.30f;         // … and how far toward the bridge the twist takes it
    static inline float slipSoft = 0.0045f, slipHard = 0.0004f; // s: the slip off the pad, off the nail
    static inline float catchTime = 0.005f;       // s a ringing tongue is held before it is plucked again
    static inline float catchDamp = 3.0f;         // N·s/m, the flesh that stills it meanwhile
    static inline float fingerDamp = 1.6f;        // N·s/m, the finger laid on a tongue
    // The tongue's tip meeting the panel.
    static inline float tipHit = 4.0e7f;          // N/m^1.5
    static inline float tipLoss = 0.6f;           // s/m (Hunt–Crossley)
    // The other hand: the palm (heavy, soft) and the knuckles (light, hard).
    static inline float palmMass = 0.20f, palm = 2.0e5f;
    static inline float knuckleMass = 0.010f, knuckle = 1.5e7f, knockLoss = 0.4f;
    // Hard contacts on the panel radiating as from a plate, above where
    // the modes speak.
    static inline float tick = 1.0f;
};

// --- the beam -----------------------------------------------------------------------------------------------
//
// A beam held at x = 0 by the bridge and the bar behind it, free at x = L.
// The bridge gives a little (a spring k_t under the root: EI·w‴ = −k_t·w)
// and the span of steel back to the bar resists the root turning (a spring
// K_r: EI·w″ = K_r·w′). With ξ = x/L, k_t' = k_t·L³/EI and k_r' = K_r·L/EI,
//   φ(ξ) = A cosh λξ + B sinh λξ + C cos λξ + D sin λξ,
// λ the roots of the four conditions. Both springs stiff, it is the clamped
// cantilever (λ₁ = 1.8751, 1 : 6.267 : 17.55 : 34.39); a root that turns
// stretches the ratios, a bridge that gives squeezes them (measured kalimba
// tines sit at 5.3–6.8). Every tongue is cut from the same stock and held
// the same way, so both springs depend on its length only.

struct BeamModes {
    double lambda[kBend] = {};
    double A[kBend] = {}, B[kBend] = {}, C[kBend] = {}, D[kBend] = {}; // φ(1) = 1
    // Over the beam's mass m and length L: ∫φ (P), ∫ξφ (Q), ∫φ² (M).
    double P[kBend] = {}, Q[kBend] = {}, M[kBend] = {};
};

inline void beamRows(double l, double kr, double kt, double m[4][4]) {
    const double ch = std::cosh(l), sh = std::sinh(l), c = std::cos(l), s = std::sin(l), l3 = l * l * l;
    const double rows[4][4] = {{l, -kr, -l, -kr}, // the root turning against the bar's span
                               {kt, l3, kt, -l3}, // the root riding on the bridge
                               {ch, sh, -c, -s},  // no moment at the tip
                               {sh, ch, s, -c}};  // no shear at the tip
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            m[i][j] = rows[i][j];
}

inline double det3(const double *a, const double *b, const double *c) {
    return a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
}

// The signed minors of three rows: a vector they are all orthogonal to.
inline void minors(const double *r0, const double *r1, const double *r2, double *v) {
    for (int j = 0; j < 4; ++j) {
        double a[3], b[3], c[3];
        for (int k = 0, col = 0; k < 4; ++k)
            if (k != j) {
                a[col] = r0[k], b[col] = r1[k], c[col] = r2[k];
                ++col;
            }
        v[j] = ((j & 1) ? -1.0 : 1.0) * det3(a, b, c);
    }
}

inline double beamDet(double l, double kr, double kt) {
    double m[4][4], v[4];
    beamRows(l, kr, kt, m);
    minors(m[1], m[2], m[3], v);
    return m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + m[0][3] * v[3];
}

inline double beamShape(const BeamModes &b, int n, double xi) {
    const double l = b.lambda[n] * xi;
    return b.A[n] * std::cosh(l) + b.B[n] * std::sinh(l) + b.C[n] * std::cos(l) + b.D[n] * std::sin(l);
}

inline BeamModes solveBeam(double kr, double kt) {
    BeamModes b;
    int n = 0;
    constexpr double step = 0.002;
    double prev = beamDet(step, kr, kt);
    for (double l = 2 * step; l < 22.0 && n < kBend; l += step) {
        const double d = beamDet(l, kr, kt);
        if ((d > 0) != (prev > 0)) {
            double lo = l - step, hi = l;
            const bool loPos = beamDet(lo, kr, kt) > 0;
            for (int i = 0; i < 60; ++i) {
                const double mid = 0.5 * (lo + hi);
                ((beamDet(mid, kr, kt) > 0) == loPos ? lo : hi) = mid;
            }
            b.lambda[n++] = 0.5 * (lo + hi);
        }
        prev = d;
    }
    for (int m = 0; m < kBend; ++m) {
        double M4[4][4];
        beamRows(b.lambda[m], kr, kt, M4);
        // The mode's coefficients: the null vector of the conditions, from
        // the better conditioned of two choices of three rows.
        double v1[4], v2[4];
        minors(M4[1], M4[2], M4[3], v1);
        minors(M4[0], M4[2], M4[3], v2);
        const double n1 = std::fabs(v1[0]) + std::fabs(v1[1]) + std::fabs(v1[2]) + std::fabs(v1[3]);
        const double n2 = std::fabs(v2[0]) + std::fabs(v2[1]) + std::fabs(v2[2]) + std::fabs(v2[3]);
        const double *v = n1 >= n2 ? v1 : v2;
        b.A[m] = v[0], b.B[m] = v[1], b.C[m] = v[2], b.D[m] = v[3];
        const double tip = beamShape(b, m, 1.0);
        b.A[m] /= tip, b.B[m] /= tip, b.C[m] /= tip, b.D[m] /= tip;
        // Simpson's rule.
        constexpr int N = 400;
        double P = 0, Q = 0, M = 0;
        for (int i = 0; i <= N; ++i) {
            const double xi = double(i) / N, w = (i == 0 || i == N) ? 1.0 : (i & 1) ? 4.0 : 2.0;
            const double f = beamShape(b, m, xi);
            P += w * f;
            Q += w * xi * f;
            M += w * f * f;
        }
        b.P[m] = P / (3.0 * N), b.Q[m] = Q / (3.0 * N), b.M[m] = M / (3.0 * N);
    }
    return b;
}

// --- the tongue's numbers ------------------------------------------------------------------------------

constexpr double kEI = Physics::kE * Physics::kWidth * Physics::kThick * Physics::kThick * Physics::kThick / 12.0;
constexpr double kRhoA = Physics::kRho * Physics::kWidth * Physics::kThick;

// The root's two springs, over EI/L and EI/L³, for a tongue L long.
inline double rootTurn(double L) { return Physics::kClamp * L / Physics::kBack; }
inline double rootRide(double L) { return Physics::kBridge * L * L * L / kEI; }

// The beams for every length a tongue can have, solved once (on the main
// thread: see prepare) and read between.
struct BeamTable {
    static constexpr int kN = 64;
    static constexpr double kLo = 0.008, kHi = 0.30;
    std::array<BeamModes, kN> at{};
    BeamTable() {
        for (int i = 0; i < kN; ++i) {
            const double L = kLo * std::pow(kHi / kLo, double(i) / (kN - 1));
            at[size_t(i)] = solveBeam(rootTurn(L), rootRide(L));
        }
    }
};

inline const BeamTable &beamTable() {
    static const BeamTable t;
    return t;
}

// The modes of a tongue L long, read between the table's two nearest.
inline BeamModes beamFor(double L) {
    const BeamTable &t = beamTable();
    const double u = std::clamp(std::log(L / BeamTable::kLo) / std::log(BeamTable::kHi / BeamTable::kLo), 0.0, 1.0) *
                     (BeamTable::kN - 1);
    const int i = std::min(int(u), BeamTable::kN - 2);
    const double f = u - i;
    const BeamModes &a = t.at[size_t(i)], &c = t.at[size_t(i + 1)];
    BeamModes b;
    auto mix = [f](double x, double y) { return x + (y - x) * f; };
    for (int m = 0; m < kBend; ++m) {
        b.lambda[m] = mix(a.lambda[m], c.lambda[m]);
        b.A[m] = mix(a.A[m], c.A[m]);
        b.B[m] = mix(a.B[m], c.B[m]);
        b.C[m] = mix(a.C[m], c.C[m]);
        b.D[m] = mix(a.D[m], c.D[m]);
        b.P[m] = mix(a.P[m], c.P[m]);
        b.Q[m] = mix(a.Q[m], c.Q[m]);
        b.M[m] = mix(a.M[m], c.M[m]);
    }
    return b;
}

// Builds what the instrument needs before it ever plays.
inline void prepare() { (void)beamTable(); }

// A tongue's first mode on a rigid box, for its length.
inline double tongueHz(double L) {
    const BeamModes b = beamFor(L);
    return b.lambda[0] * b.lambda[0] / (2.0 * M_PI * L * L) * std::sqrt(kEI / kRhoA);
}

// The length that sounds at hz on a rigid box (λ₁ moves a little with the
// length, so a few rounds).
inline double tongueLength(double hz) {
    double L = 0.1;
    for (int i = 0; i < 10; ++i) {
        const BeamModes b = beamFor(L);
        L = b.lambda[0] * std::sqrt(std::sqrt(kEI / kRhoA) / (2.0 * M_PI * hz));
    }
    return std::clamp(L, BeamTable::kLo, BeamTable::kHi);
}

// --- the box ---------------------------------------------------------------------------------------------

struct Panel {
    double w, h, thick;
    int m, n;
};

// The cavity's volume.
constexpr double kVolume = Physics::kBoxW * Physics::kBoxH * Physics::kBoxD;
// The hole through the front panel as the air in it moves: the panel's
// thickness and an end correction each side (flanged).
inline double holeLength(double r) { return Physics::kFrontThick + 2 * 0.85 * r; }
// Helmholtz's formula for this box and a hole of radius r.
inline double helmholtzHz(double r = Physics::kHoleR) {
    return Physics::kC / (2 * M_PI) * std::sqrt(M_PI * r * r / (kVolume * holeLength(r)));
}
// The hole a maker cuts for the box to sound at hz (bisection: the
// frequency grows with the radius).
inline double holeFor(double hz) {
    double lo = 0.015, hi = 0.11;
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (lo + hi);
        (helmholtzHz(mid) < hz ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

// An orthotropic plywood panel's mode (m, n), simply supported, stiffened by
// its glued edges: frequency in Hz.
inline double panelHz(double a, double b, double h, int m, int n) {
    const double nu = 0.07; // plywood's small Poisson ratio
    const double D1 = Physics::kPlyE1 * h * h * h / (12 * (1 - nu * nu));
    const double D2 = Physics::kPlyE2 * h * h * h / (12 * (1 - nu * nu));
    const double H = std::sqrt(D1 * D2);
    const double p = m / a, q = n / b;
    const double w = M_PI * M_PI * std::sqrt((D1 * p * p * p * p + 2 * H * p * p * q * q + D2 * q * q * q * q) /
                                             (Physics::kPlyRho * h));
    return Knobs::edge * w / (2 * M_PI);
}

// --- the instrument ---------------------------------------------------------------------------------------

class Marimbula {
public:
    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        prepare();
        build();
        silence();
    }

    // The tongue tuned to hz: one already sounding at that pitch, or a free
    // one (or the quietest) moved to it. Tongues are laid along the bridge
    // by pitch, the low ones to the player's left.
    int tongue(float hz) {
        hz = std::clamp(hz, 20.0f, 1500.0f);
        for (int s = 0; s < kSlots; ++s)
            if (m_slot[s].used && std::fabs(std::log2(m_slot[s].hz / hz)) < 0.0005f)
                return s;
        int best = 0;
        float bestScore = 1e30f;
        for (int s = 0; s < kSlots; ++s) {
            const Slot &t = m_slot[s];
            // Free first, then the quietest, damped before ringing.
            const float score = !t.used ? -1.0f : t.energy * (t.dampC > 0 ? 0.1f : 1.0f) + (t.finger != Free ? 1e9f : 0.0f);
            if (score < bestScore) {
                bestScore = score;
                best = s;
            }
        }
        Slot &t = m_slot[best];
        for (int n = 0; n < kModes; ++n)
            t.q[n] = t.u[n] = 0;
        t.finger = Free;
        t.dampC = t.dampTo = 0;
        t.used = true;
        t.age = ++m_clock;
        tuneSlot(best, hz);
        return best;
    }

    // Moves a sounding tongue's pitch (a bend), keeping what it is doing.
    void setPitch(int s, float hz) {
        if (s < 0 || s >= kSlots || !m_slot[s].used)
            return;
        hz = std::clamp(hz, 20.0f, 1500.0f);
        if (std::fabs(std::log2(m_slot[s].hz / hz)) < 0.0002f)
            return;
        tuneSlot(s, hz);
    }
    float pitchOf(int s) const { return s >= 0 && s < kSlots ? m_slot[s].hz : 0.0f; }

    struct Stroke {
        float vel = 0.7f;   // 0..1: how far the finger pushes, how fast it comes
        float tone = 0.5f;  // 0 the soft pad … 1 the nail: how quickly it slips off
        float slap = 0;     // 0 a pluck … 1 a slap: the finger arriving at speed
        float off = 0;      // −1..1: where across the tongue it lands, of half its width
        float spot = 0;     // 0 at the tip … 1 a third of the way in toward the bridge
    };

    // The finger plays tongue s.
    void pluck(int s, const Stroke &st) {
        if (s < 0 || s >= kSlots)
            return;
        Slot &t = m_slot[s];
        // A slot never shaped (pluck before tongue): shape it first.
        if (!(t.mass[0] > 0))
            tuneSlot(s, t.hz);
        const float vel = clampf(st.vel, 0.0f, 1.0f), tone = clampf(st.tone, 0.0f, 1.0f);
        const float slap = clampf(st.slap, 0.0f, 1.0f);
        // Still, or already moving (ringing, or the finger on it)?
        float moving = 0;
        for (int n = 0; n < kModes; ++n)
            moving += std::fabs(t.q[n]) + std::fabs(t.u[n]) * 50.0f;
        const bool still = t.finger == Free && moving < 1e-7f;
        t.used = true;
        t.age = ++m_clock;
        t.dampTo = 0;
        t.dampC = 0;
        t.off = clampf(st.off, -1.0f, 1.0f) * 0.5f * float(Physics::kWidth) * 0.6f;
        placeFinger(s, Knobs::fingerAt - Knobs::fingerIn * clampf(st.spot, 0.0f, 1.0f));
        // How far the finger takes the tongue down where it meets it: a
        // millimetre or so touched, most of the way to the panel pushed
        // hard. But a fingertip pushes with a few newtons at most, so a
        // short, stiff tongue goes down less. A slap's hand follows
        // through: slapped hard, the tip is driven onto the panel.
        const float force = Knobs::fingerLight + (Knobs::fingerFirm - Knobs::fingerLight) * vel * vel;
        const float want = 0.0008f + 0.0062f * std::pow(vel, 1.3f);
        const float onto = lerpf(want, 1.1f * float(Physics::kGap) / (t.tipOver > 0 ? t.tipOver : 1.0f), slap * vel * vel);
        float push = std::min(onto * t.kStatic, force * (1.0f + 2.0f * slap));
        // A pluck holds the tongue clear of the panel: its tip at most 95%
        // of the way down (only a slap's follow-through reaches it).
        if (slap <= 0.02f)
            push = std::min(push, 0.95f * float(Physics::kGap) * t.kStatic / std::max(t.tipOver, 1e-3f));
        t.depth = push / t.kStatic;
        // The slip: the soft pad rolls off slowly, the nail flicks; a hard
        // stroke slips a little quicker.
        t.slip = Knobs::slipSoft * std::pow(Knobs::slipHard / Knobs::slipSoft, std::pow(tone, 0.8f)) *
                 (1.0f - 0.25f * vel);
        t.pulp = Knobs::fingerPulp * std::exp2(2.0f * tone);
        if (slap > 0.02f) {
            // The finger thrown at it: it lands on the tongue where it is,
            // moving at up to a metre a second.
            t.finger = Pressing;
            t.z = contactDisp(t);
            t.vz = slap * (0.15f + 0.75f * vel * vel);
            t.push = push;
            t.time = 0;
            t.hold = 0;
        } else if (still) {
            // A pluck of a still tongue: the finger already has it down (the
            // press was slow and silent); it only lets go.
            addStatic(s, push);
            t.finger = Releasing;
            t.release = push;
            t.lastF = push;
            t.time = 0;
            t.hold = 0;
            t.from = push;
        } else {
            // A tongue already moving is caught: the finger lands on it, its
            // flesh a dashpot that soaks up the swing, and presses in over a
            // few milliseconds (a force, which by its end has the tongue
            // where it pushes it, not added to what it was doing); then it
            // lets go.
            t.finger = Releasing;
            t.release = push;
            t.from = t.F;
            t.time = 0;
            t.hold = Knobs::catchTime;
            t.dampC = t.dampTo = Knobs::catchDamp;
            t.dirty = true;
        }
        m_sleeping = false;
    }

    // The finger laid on tongue s (1), or lifted (0), or anything between: a
    // dashpot at its tip, coming on over a few milliseconds.
    void damp(int s, float amount) {
        if (s < 0 || s >= kSlots)
            return;
        // (A finger catching the tongue is already on it.)
        if (m_slot[s].finger == Releasing && m_slot[s].time < m_slot[s].hold)
            return;
        m_slot[s].dampTo = clampf(amount, 0.0f, 2.0f) * Knobs::fingerDamp;
    }

    // The other hand on the front panel, vel 0..1: tone 0 the palm, flat
    // on the panel beside the tongues (the box's air and its lowest modes, a
    // bass drum), 1 the knuckles near its top edge (its upper modes, dry).
    void knock(float vel, float tone) {
        vel = clampf(vel, 0.0f, 1.0f);
        tone = clampf(tone, 0.0f, 1.0f);
        const double a = Physics::kBoxW, b = Physics::kBoxH;
        const double x = lerpf(float(Physics::kPalmX), float(Physics::kKnuckleX), tone) * a;
        const double y = lerpf(float(Physics::kPalmY), float(Physics::kKnuckleY), tone) * b;
        for (int k = 0; k < kFront; ++k)
            m_knockPsi[k] = float(shape(k, x, y));
        m_knock.on = true;
        m_knock.z = panelDispAt(m_knockPsi);
        // A hand lands at up to a couple of metres a second.
        m_knock.v = 0.3f + 1.6f * vel;
        m_knock.gap = 0;
        // Mass and stiffness between the palm's and the knuckle's, on a log scale.
        m_knock.k = Knobs::palm * std::pow(Knobs::knuckle / Knobs::palm, tone);
        m_knock.mass = Knobs::palmMass * std::pow(Knobs::knuckleMass / Knobs::palmMass, tone);
        m_knock.time = 0;
        m_sleeping = false;
    }

    // Adds the sound, Pa at a metre in front, into out (n ≤ 4096).
    //
    // Each sample: the cavity's pressure; what the tongues' roots (and the
    // forces on them the last sample solved) do to the board; the box
    // steps, the hand's knock solved with it; then the tongues step on the
    // moving board, the finger's pulp and the tip's touch on the panel each
    // solved with them. A contact is solved implicitly, against the step
    // that the masses it pushes are about to take, so a stiff one is as
    // stiff at any rate and can never ring itself up.
    void render(float *out, int n) {
        if (m_sleeping)
            return;
        // Per block: each tongue's damping as the finger comes and goes.
        const float dampC = 1.0f - std::exp(-float(n) / (0.006f * m_rate));
        for (int s = 0; s < kSlots; ++s) {
            Slot &t = m_slot[s];
            if (!t.used)
                continue;
            const float was = t.dampC;
            t.dampC += (t.dampTo - t.dampC) * dampC;
            if (std::fabs(t.dampC - was) > 1e-6f || t.dirty)
                coefficients(s);
        }
        float *y = m_y, *v = m_v;
        for (int i = 0; i < n; ++i) {
            // The cavity: its pressure from every panel's volume and the plug's.
            float dv = -m_holeS * m_xp;
            for (int k = 0; k < kPanel; ++k)
                dv += m_area[k] * y[k];
            const float p = m_kCav * dv;
            float f[kPanel];
            for (int k = 0; k < kPanel; ++k)
                f[k] = -p * m_area[k];
            const float fp = p * m_holeS;

            // The tongues' roots on the board, the fingers.
            float tick = 0;
            for (int s = 0; s < kSlots; ++s) {
                Slot &t = m_slot[s];
                if (!t.used)
                    continue;
                if (t.finger == Releasing) {
                    float F = 0;
                    if (t.time < t.hold) {
                        // Caught: the finger presses in, eased.
                        const float r = t.time / t.hold, e = r * r * (3.0f - 2.0f * r);
                        F = lerpf(t.from, t.release, e);
                        // And as the slip begins, the flesh lifts off.
                        if (t.time + m_dt >= t.hold) {
                            t.dampC = t.dampTo = 0;
                            coefficients(int(&t - m_slot));
                        }
                    } else {
                        // The pad slides toward the tip's edge and leaves
                        // it: the force eases off at first and is gone with
                        // a kink as the last of the pad crosses the edge.
                        const float r = (t.time - t.hold) / t.slip;
                        F = r >= 1.0f ? 0.0f : t.release * std::cos(0.5f * kPi * r);
                        if (r >= 1.0f)
                            t.finger = Free;
                    }
                    t.time += m_dt;
                    t.F = F;
                } else if (t.finger == Free) {
                    t.F = 0;
                }
                // (A slap's pulp force, and the tip's touch, are the ones the
                // last sample solved.)
                const float F = t.F, Fc = t.Fc;
                tick += (Fc - t.lastFc) * m_wPanel;
                t.lastFc = Fc;
                // What the root does to the board: the modes' own (stiffness)
                // and what of the outside forces the modes leave out.
                float Fb = F * t.resP - Fc * t.resP1, Mb = F * t.resQ - Fc * t.resQ1;
                for (int m = 0; m < kBend; ++m) {
                    Fb += t.gP[m] * t.q[m];
                    Mb += t.gQ[m] * t.q[m];
                }
                const float Tb = t.gT * t.q[kBend] + F * t.off * t.resT;
                for (int k = 0; k < kFront; ++k)
                    f[k] += Fb * t.psiB[k] + Mb * t.psiS[k] + Tb * t.psiX[k] + Fc * t.psiTip[k];
            }

            // The box steps, free of the hand.
            float un[kPanel];
            for (int k = 0; k < kPanel; ++k)
                un[k] = m_d[k] * v[k] - m_c[k] * y[k] + m_inv[k] * f[k];
            // The hand on the panel, solved with the panel's step.
            float Fk = 0;
            if (m_knock.on) {
                float w = 0, give = 0;
                for (int k = 0; k < kFront; ++k) {
                    w += m_knockPsi[k] * (y[k] + un[k]);
                    give += m_knockPsi[k] * m_knockPsi[k] * m_inv[k];
                }
                const float hand = m_dt * m_dt / m_knock.mass;
                const float free = m_knock.z + m_knock.v * m_dt - w;
                const float loss = clampf(1.0f + Knobs::knockLoss * (free - m_knock.gap) * m_rate, 0.0f, 3.0f);
                Fk = contact(m_knock.k * loss, free, give + hand);
                m_knock.gap = free - (give + hand) * Fk;
                for (int k = 0; k < kFront; ++k)
                    un[k] += m_inv[k] * Fk * m_knockPsi[k];
                m_knock.v -= Fk / m_knock.mass * m_dt;
                m_knock.z += m_knock.v * m_dt;
                m_knock.time += m_dt;
                if ((free <= 0 && m_knock.v < 0) || m_knock.time > 0.02f)
                    m_knock.on = false;
            }
            tick += (Fk - m_lastFk) * m_wPanel;
            m_lastFk = Fk;
            // The box moves: panels and plug, the accelerations kept for the
            // tongues riding on it.
            float acc[kFront];
            float Q = 0;
            for (int k = 0; k < kPanel; ++k) {
                if (k < kFront)
                    acc[k] = un[k] - v[k];
                v[k] = un[k];
                y[k] += un[k];
                Q -= m_rad[k] * un[k];
            }
            {
                const float u = m_dp * m_vp - m_cp * m_xp + m_invp * fp;
                m_vp = u;
                m_xp += u;
                Q += m_holeS * m_holeRad * u;
            }

            // The tongues, their bases moving with the board.
            float dip = 0;
            for (int s = 0; s < kSlots; ++s) {
                Slot &t = m_slot[s];
                if (!t.used)
                    continue;
                float Y = 0, Th = 0, X = 0;
                for (int k = 0; k < kFront; ++k) {
                    Y += t.psiB[k] * acc[k];
                    Th += t.psiS[k] * acc[k];
                    X += t.psiX[k] * acc[k];
                }
                // Free of the slapping pulp and the panel, first.
                const float Fknown = t.finger == Pressing ? 0.0f : t.F;
                float uq[kModes];
                for (int m = 0; m < kBend; ++m)
                    uq[m] = t.d[m] * t.u[m] - t.c[m] * t.q[m] + t.inv[m] * Fknown * t.phiC[m] - t.bP[m] * Y -
                            t.bQ[m] * Th;
                uq[kBend] = t.d[kBend] * t.u[kBend] - t.c[kBend] * t.q[kBend] +
                            t.inv[kBend] * Fknown * t.off * t.phiT - t.bT * X;
                // The slap's pulp, solved with the tongue's step and the
                // finger's: the hand pushes behind it.
                if (t.finger == Pressing) {
                    float w = 0, give = m_dt * m_dt / Knobs::fingerMass;
                    for (int m = 0; m < kBend; ++m) {
                        w += t.phiC[m] * (t.q[m] + uq[m]);
                        give += t.phiC[m] * t.phiC[m] * t.inv[m];
                    }
                    const float zFree = t.z + t.vz * m_dt + t.push * m_dt * m_dt / Knobs::fingerMass;
                    const float F = contact(t.pulp, zFree - w, give);
                    for (int m = 0; m < kBend; ++m)
                        uq[m] += t.inv[m] * F * t.phiC[m];
                    uq[kBend] += t.inv[kBend] * F * t.off * t.phiT;
                    t.vz += (t.push - F) / Knobs::fingerMass * m_dt;
                    t.z += t.vz * m_dt;
                    t.time += m_dt;
                    t.F = F;
                    // It slips off once it has the tongue down as far as it
                    // meant, or the hand's stroke is spent.
                    float at = 0;
                    for (int m = 0; m < kBend; ++m)
                        at += t.phiC[m] * (t.q[m] + uq[m]);
                    if (at >= 0.85f * t.depth || t.time > 0.03f || (t.vz < 0 && F <= 0)) {
                        t.finger = Releasing;
                        t.release = F;
                        t.time = 0;
                        t.hold = 0;
                    }
                }
                // The tip and the panel under it, solved likewise.
                {
                    float tip = -panelDispAt(t.psiTip), give = 0;
                    for (int m = 0; m < kBend; ++m)
                        if (t.live[m]) {
                            tip += t.q[m] + uq[m];
                            give += t.inv[m];
                        }
                    float Fc = 0;
                    const float into = tip - float(Physics::kGap);
                    if (into > 0) {
                        const float loss = clampf(1.0f + Knobs::tipLoss * (into - t.into) * m_rate, 0.0f, 3.0f);
                        Fc = contact(Knobs::tipHit * loss, into, give);
                        for (int m = 0; m < kBend; ++m)
                            if (t.live[m])
                                uq[m] -= t.inv[m] * Fc;
                    }
                    t.into = into - give * Fc;
                    t.Fc = Fc;
                }
                for (int m = 0; m < kModes; ++m) {
                    if (m < kBend)
                        dip += t.rad[m] * (uq[m] - t.u[m]);
                    t.u[m] = uq[m];
                    t.q[m] += uq[m];
                }
            }
            // Pressure: the volume acceleration of the monopoles, the
            // tongues' dipoles, and the hard contacts' clicks on the panel
            // (their force, rebuilt from its steps with a slow leak, above
            // where the modes speak).
            const float mono = (Q - m_lastQ) * m_gain;
            m_lastQ = Q;
            m_force = m_force * m_leak + tick;
            out[i] += mono + dip * m_gain + Knobs::tick * m_tick.step(m_force).high;
        }
        // Asleep once all of it has rung out.
        float e = 0;
        for (int k = 0; k < kPanel; ++k)
            e += m_mass[k] * (v[k] * v[k] + m_c[k] * y[k] * y[k]);
        e += m_massP * m_vp * m_vp;
        e *= m_rate * m_rate;
        m_boxEnergy = e;
        float tongues = 0;
        bool touching = m_knock.on;
        for (int s = 0; s < kSlots; ++s) {
            Slot &t = m_slot[s];
            if (!t.used)
                continue;
            float te = 0;
            for (int m = 0; m < kModes; ++m)
                te += t.mass[m] * (t.u[m] * t.u[m] + t.c[m] * t.q[m] * t.q[m]);
            t.energy = te * m_rate * m_rate;
            tongues += t.energy;
            touching |= t.finger != Free;
            if (t.finger == Free && t.energy < 1e-12f)
                t.used = false, t.energy = 0;
        }
        m_energy = e + tongues;
        // (Should anything ever go non-finite, start again silent rather
        // than sound it forever.)
        if (!std::isfinite(m_energy)) {
            silence();
            return;
        }
        if (!touching && m_energy < 1e-12f) {
            if (++m_quiet > 8)
                silence();
        } else {
            m_quiet = 0;
        }
    }

    void silence() {
        for (int k = 0; k < kPanel; ++k)
            m_y[k] = m_v[k] = 0;
        m_xp = m_vp = 0;
        for (Slot &t : m_slot) {
            for (int m = 0; m < kModes; ++m)
                t.q[m] = t.u[m] = 0;
            t.used = false;
            t.finger = Free;
            t.energy = 0;
            t.F = t.Fc = t.lastF = t.lastFc = 0;
            t.dampC = t.dampTo = 0;
        }
        m_knock.on = false;
        m_lastQ = 0;
        m_lastFk = 0;
        m_force = 0;

        m_tick.reset();
        m_sleeping = true;
        m_energy = m_boxEnergy = 0;
        m_quiet = 0;
    }

    bool active() const { return !m_sleeping; }
    // Loudness, roughly 0..1, for drawing.
    float level() const { return std::sqrt(std::max(0.0f, m_energy) / 2e-3f); }

    // --- for tests and the docs: the instrument as built ------------------------------------------------

    // Tongue s's mode m (0..3 bending, 4 twisting): its frequency on its own
    // (on a rigid box) and its length.
    float modeHz(int s, int m) const { return m_slot[std::clamp(s, 0, kSlots - 1)].modeHz[std::clamp(m, 0, kModes - 1)]; }
    float lengthOf(int s) const { return m_slot[std::clamp(s, 0, kSlots - 1)].L; }
    float panelModeHz(int k) const { return m_hzPanel[std::clamp(k, 0, kPanel - 1)]; }
    float rootStiffness(int s) const { return float(rootTurn(m_slot[std::clamp(s, 0, kSlots - 1)].L)); }
    float bridgeStiffness(int s) const { return float(rootRide(m_slot[std::clamp(s, 0, kSlots - 1)].L)); }
    // How far the tongue's tip is from rest, m.
    float tipOf(int s) const {
        const Slot &t = m_slot[std::clamp(s, 0, kSlots - 1)];
        float tip = 0;
        for (int m = 0; m < kBend; ++m)
            tip += t.q[m];
        return tip;
    }
    // The hole's plug: how far it is from rest, m.
    float plugOf() const { return m_xp; }
    // What it radiates, split: the monopoles (the box and its air) and the
    // tongues' own dipoles, for the last block. Off switches a path.
    void setPaths(bool box, bool tongues) {
        m_pathBox = box;
        m_pathTongue = tongues;
        build();
    }
    // The box's air off (a box with no hole and rigid walls) or on.
    void setAir(bool on) {
        m_air = on;
        m_boxHeard = -1;
        build();
    }
    // The tongues decoupled from the box (as if on a rigid, massive block) or not.
    void setCoupled(bool on) {
        m_coupled = on;
        build();
    }
    // An impulse (N·s) into the front panel at (x, y), in units of its width
    // and height: the box's own response, for measuring it.
    void impulse(float ns, float x, float y) {
        for (int k = 0; k < kFront; ++k)
            m_v[k] += float(ns * shape(k, x * Physics::kBoxW, y * Physics::kBoxH)) / (m_mass[k] * m_rate);
        m_sleeping = false;
    }
    // The box's Helmholtz frequency by the rigid-walled formula, and its hole.
    float helmholtz() const { return float(helmholtzHz(m_holeR)); }
    float holeRadius() const { return float(m_holeR); }
    // The box a maker builds for a tuning: its hole cut so the air sounds
    // at hz, by the rigid-walled formula (setBox) or where it is heard with
    // its panels giving (setBoxHeard: some 0.8–0.85 of the formula; Kičák
    // measured 0.85 on a cajón). Only when it changes: a new box is built.
    void setBoxHeard(float hz) {
        if (hz == m_boxHeard)
            return;
        m_boxHeard = hz;
        if (!m_air) {
            setBox(hz);
            return;
        }
        double lo = 0.012, hi = 0.12;
        for (int i = 0; i < 24; ++i) {
            const double mid = 0.5 * (lo + hi);
            (heardHz(mid) < hz ? lo : hi) = mid;
        }
        setHole(0.5 * (lo + hi));
    }
    // Where the box's air sounds with a hole of radius r and its panels
    // giving: the lowest root of the box's characteristic equation, the
    // cavity's spring shared by the plug and every panel with net volume
    // (1 + κ·Σ aᵢ²/(Kᵢ − ω²Mᵢ) = 0, losses aside).
    double heardHz(double r) const {
        const double S = M_PI * r * r, mp = Physics::kRhoAir * S * holeLength(r), kc = m_kCav;
        double top = 1e9;
        for (int k = 0; k < kPanel; ++k)
            if (m_area[k] > 0)
                top = std::min(top, std::sqrt(m_stiff[k] / m_mass[k]));
        auto f = [&](double w) {
            double sum = -S * S / (w * w * mp);
            for (int k = 0; k < kPanel; ++k)
                sum += double(m_area[k]) * m_area[k] / (m_stiff[k] - w * w * m_mass[k]);
            return 1 + kc * sum;
        };
        double lo = 1, hi = top * (1 - 1e-9);
        for (int i = 0; i < 40; ++i) {
            const double mid = 0.5 * (lo + hi);
            (f(mid) < 0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi) / (2 * M_PI);
    }
    float heard() const { return float(heardHz(m_holeR)); }
    void setBox(float hz) { setHole(holeFor(std::clamp(double(hz), 25.0, 160.0))); }
    void setHole(double r) {
        if (std::fabs(r - m_holeR) < 1e-5)
            return;
        // The air in the hole goes on moving as it was: the same volume
        // through it, the same flow.
        const float was = m_holeS;
        m_holeR = r;
        build();
        m_xp *= was / m_holeS;
        m_vp *= was / m_holeS;
    }

private:
    static constexpr int kModes = kBend + 1;
    enum Finger { Free, Pressing, Releasing };

    struct Slot {
        bool used = false, dirty = false;
        float hz = 110, L = 0.1f;
        uint32_t age = 0;
        float q[kModes] = {}, u[kModes] = {};
        float d[kModes] = {}, c[kModes] = {}, inv[kModes] = {}, mass[kModes] = {}, tau[kModes] = {};
        float w2[kModes] = {}, modeHz[kModes] = {};
        float phiC[kBend] = {};
        float gP[kBend] = {}, gQ[kBend] = {}, bP[kBend] = {}, bQ[kBend] = {}, rad[kBend] = {};
        float gT = 0, bT = 0, phiT = 0, resT = 0;
        float resP = 0, resQ = 0, resP1 = 0, resQ1 = 0;
        float kStatic = 1, xc = 0.1f, xi = 0;
        BeamModes bm;
        float psiB[kFront] = {}, psiS[kFront] = {}, psiX[kFront] = {}, psiTip[kFront] = {};
        // The finger.
        Finger finger = Free;
        float z = 0, vz = 0, push = 0, depth = 0, release = 0, slip = 0.002f, pulp = 4e5f, off = 0;
        float time = 0, hold = 0, from = 0;

        bool live[kBend] = {};
        float tipOver = 1; // how far the tip moves for a unit at the finger, statically
        float into = 0;
        float F = 0, Fc = 0, lastF = 0, lastFc = 0;
        float dampC = 0, dampTo = 0;
        float energy = 0;
    };

    struct Knock {
        bool on = false;
        float z = 0, v = 0, k = 1e6f, mass = 0.01f, time = 0, gap = 0;
    };

    // The front panel's modes, then the back's.
    void build() {
        const float rate = m_rate;
        const double a = Physics::kBoxW, b = Physics::kBoxH;
        // The front's lowest modes, sorted, then the back's.
        struct Cand {
            double hz;
            int m, n;
        };
        auto lowest = [&](double h, int want, Panel *outp) {
            Cand c[48];
            int count = 0;
            for (int m = 1; m <= 8; ++m)
                for (int n = 1; n <= 6; ++n)
                    c[count++] = {panelHz(a, b, h, m, n), m, n};
            std::sort(c, c + count, [](const Cand &x, const Cand &y) { return x.hz < y.hz; });
            for (int i = 0; i < want; ++i)
                outp[i] = {a, b, h, c[i].m, c[i].n};
        };
        lowest(Physics::kFrontThick, kFront, m_panel);
        lowest(Physics::kBackThick, kBack, m_panel + kFront);
        const double rhoC2 = Physics::kRhoAir * Physics::kC * Physics::kC;
        m_kCav = m_air ? float(rhoC2 / kVolume) : 0.0f;
        m_holeS = float(M_PI * m_holeR * m_holeR);
        // Pressure a metre away from a volume acceleration: ρ/4πr, and the
        // samples' units (velocities are kept as metres a sample).
        m_gain = float(Physics::kRhoAir / (4 * M_PI * Physics::kListen)) * rate * rate;
        const double fc = Physics::kC * Physics::kC / (2 * M_PI) *
                          std::sqrt(Physics::kPlyRho * Physics::kFrontThick /
                                    (std::sqrt(Physics::kPlyE1 * Physics::kPlyE2) * std::pow(Physics::kFrontThick, 3) / 12));
        // The clamp board along the bridge, a rigid bar: how each front
        // mode moves it (up and down, tipping along the tongues, rolling
        // end to end), by Simpson's rule along it.
        {
            const double x0 = Physics::kBridgeX0 * a, x1 = Physics::kBridgeX1 * a, yb = Physics::kBridgeY * b;
            m_barMid = 0.5 * (x0 + x1);
            constexpr int N = 16;
            for (int k = 0; k < kFront; ++k) {
                double Y = 0, S = 0, XR = 0, XX = 0;
                for (int i = 0; i <= N; ++i) {
                    const double x = x0 + (x1 - x0) * i / N, w = (i == 0 || i == N) ? 1.0 : (i & 1) ? 4.0 : 2.0;
                    Y += w * shape(k, x, yb);
                    S += w * shapeDy(k, x, yb);
                    XR += w * (x - m_barMid) * shape(k, x, yb);
                    XX += w * (x - m_barMid) * (x - m_barMid);
                }
                m_barY[k] = Y / (3.0 * N), m_barS[k] = S / (3.0 * N), m_barRoll[k] = XR / XX;
            }
        }
        for (int k = 0; k < kPanel; ++k) {
            const Panel &P = m_panel[k];
            const double plate = panelHz(P.w, P.h, P.thick, P.m, P.n);
            const double plateMass = Physics::kPlyRho * P.thick * P.w * P.h / 4.0;
            double mass = plateMass;
            // The board and the rack ride on the front: as much of their mass
            // as the mode moves them (its average along the bar; the cross
            // terms between modes are left out). The plate's stiffness stays,
            // so the mode drops.
            if (k < kFront)
                mass += Physics::kRack * m_barY[k] * m_barY[k];
            const double hz = plate * std::sqrt(plateMass / mass);
            const double odd = (P.m & 1) && (P.n & 1) ? 1.0 : 0.0;
            const double area = odd * P.w * P.h * 4.0 / (M_PI * M_PI * P.m * P.n);
            m_hzPanel[k] = float(hz);
            m_mass[k] = float(mass);
            m_area[k] = m_air ? float(area) : 0.0f;
            // A mode without net volume still radiates a little from its
            // edges, more as it nears coincidence.
            const double radArea = area + Knobs::radOdd * P.w * P.h * 4.0 / (M_PI * M_PI * P.m * P.n) * std::min(1.0, hz / fc);
            m_rad[k] = m_pathBox ? float(radArea) : 0.0f;
            const double w = 2 * M_PI * hz;
            const double r = std::exp(-M_PI * hz / Knobs::panelQ / rate);
            m_d[k] = float(r * r);
            m_c[k] = float(1 + r * r - 2 * r * std::cos(w / rate));
            m_inv[k] = float(1.0 / (mass * rate * rate));
            m_stiff[k] = mass * w * w;
        }

        // The plug: a mass with no spring of its own (the cavity is its spring).
        m_massP = float(Physics::kRhoAir * m_holeS * holeLength(m_holeR));
        {
            const double r = std::exp(-M_PI * helmholtzHz(m_holeR) / Knobs::holeQ / rate);
            m_dp = float(r * r);
            m_cp = float(1 + r * r - 2 * r);
            m_invp = float(1.0 / (m_massP * rate * rate));
        }
        m_holeRad = m_pathBox ? 1.0f : 0.0f;
        // The knock's place, and the clicks: a force on a plate radiates as
        // on an infinite one, ρ₀F/2πrρh, above where the modes stop.
        const double r0 = Physics::kRhoAir / (2 * M_PI * Physics::kListen);
        m_wPanel = float(r0 / (Physics::kPlyRho * Physics::kFrontThick));
        m_tick.set(1400.0f, 0.6f, rate);
        m_leak = std::exp(-kTwoPi * 20.0f / rate);
        for (int s = 0; s < kSlots; ++s)
            if (m_slot[s].used)
                tuneSlot(s, m_slot[s].hz);
    }

    double shape(int k, double x, double y) const {
        const Panel &P = m_panel[k];
        return std::sin(P.m * M_PI * x / P.w) * std::sin(P.n * M_PI * y / P.h);
    }
    double shapeDx(int k, double x, double y) const {
        const Panel &P = m_panel[k];
        return P.m * M_PI / P.w * std::cos(P.m * M_PI * x / P.w) * std::sin(P.n * M_PI * y / P.h);
    }
    double shapeDy(int k, double x, double y) const {
        const Panel &P = m_panel[k];
        return P.n * M_PI / P.h * std::sin(P.m * M_PI * x / P.w) * std::cos(P.n * M_PI * y / P.h);
    }

    float panelDispAt(const float *psi) const {
        float w = 0;
        for (int k = 0; k < kFront; ++k)
            w += psi[k] * m_y[k];
        return w;
    }
    float panelVelAt(const float *psi) const {
        float w = 0;
        for (int k = 0; k < kFront; ++k)
            w += psi[k] * m_v[k];
        return w * m_rate;
    }
    // A Hertzian contact solved with the step: the force F = K·δ^1.5 where
    // δ = free − give·F, free how far the two would overlap if it pushed
    // nothing, give how much its own force moves them apart this step.
    // Monotone in F, so bisected (on √ for the steep start).
    static float contact(float K, float free, float give) {
        if (free <= 0 || K <= 0)
            return 0;
        float lo = 0, hi = free / std::max(give, 1e-12f);
        for (int i = 0; i < 26; ++i) {
            const float F = 0.5f * (lo + hi), d = free - give * F;
            (d > 0 && F < K * d * std::sqrt(d) ? lo : hi) = F;
        }
        return 0.5f * (lo + hi);
    }

    float contactDisp(const Slot &t) const {
        float w = 0;
        for (int m = 0; m < kBend; ++m)
            w += t.phiC[m] * t.q[m];
        return w;
    }

    // The box's receptance as tongue s's first mode sees it at ω: cᵀ(K − ω²M)⁻¹c,
    // the cavity's rank-one coupling taken by Sherman–Morrison.
    // (Complex, with the panels' and the plug's losses, so a tongue tuned
    // onto a resonance meets its finite peak; the real part pulls the pitch.)
    std::complex<double> boxReceptance(const double *cvec, std::complex<double> w) const {
        using C = std::complex<double>;
        C g = 0, ca = 0, aa = 0;
        for (int k = 0; k < kPanel; ++k) {
            const double mk = m_mass[k], wk = std::sqrt(m_stiff[k] / mk);
            const C D = m_stiff[k] - w * w * mk + C(0, 1) * w * mk * wk / double(Knobs::panelQ);
            g += cvec[k] * cvec[k] / D;
            ca += cvec[k] * double(m_area[k]) / D;
            aa += double(m_area[k]) * double(m_area[k]) / D;
        }
        {
            const double wh = kTwoPi * helmholtzHz(m_holeR);
            const C D = -w * w * double(m_massP) + C(0, 1) * w * double(m_massP) * wh / double(Knobs::holeQ);
            aa += double(m_holeS) * double(m_holeS) / D;
        }
        const double kc = m_kCav;
        return g - kc * ca * ca / (1.0 + kc * aa);
    }

    // Where a tongue whose own first mode (on a rigid box) is at f0 is
    // heard on this box, at the bridge position given by psiB/psiS: the
    // root of its first mode's equation with the box, ω² = ω₀²/(1 + ω₀²G(ω)/M)
    // (G the box's complex receptance, losses and all), followed by Newton
    // from the tongue's own frequency, so it stays on the tongue's branch.
    double heardTongue(double f0, const float *psiB, const float *psiS) const {
        using C = std::complex<double>;
        const double L = tongueLength(f0);
        const BeamModes bm = beamFor(L);
        const double m = kRhoA * L, M1 = m * bm.M[0];
        const double w0 = 2 * M_PI * bm.lambda[0] * bm.lambda[0] / (2 * M_PI * L * L) * std::sqrt(kEI / kRhoA);
        double cvec[kPanel] = {};
        for (int k = 0; k < kFront; ++k)
            cvec[k] = m * (bm.P[0] * psiB[k] + bm.Q[0] * L * psiS[k]);
        auto f = [&](C w) { return w0 * w0 - w * w * (1.0 + w0 * w0 * boxReceptance(cvec, w) / M1); };
        C w = w0;
        for (int i = 0; i < 10; ++i) {
            const C h = 1e-5 * w0, fw = f(w);
            const C d = (f(w + h) - fw) / h;
            if (std::abs(d) < 1e-30)
                break;
            C step = fw / d;
            // (No leaps: a step past a tenth of the frequency is cut back.)
            if (std::abs(step) > 0.1 * w0)
                step *= 0.1 * w0 / std::abs(step);
            w -= step;
            if (std::abs(step) < 1e-7 * w0)
                break;
        }
        return std::real(w) / (2 * M_PI);
    }

    // Shapes tongue s for hz: its length (tuned, as a maker slides it,
    // so that on this box it sounds at hz), its modes and its place.
    void tuneSlot(int s, float hz) {
        Slot &t = m_slot[s];
        // What each mode holds, to carry it through the change: a bend is
        // slow against the tongue's swing, so each mode keeps its action,
        // its energy over its frequency (the adiabatic invariant).
        float was[kModes][2] = {};
        for (int n = 0; n < kModes; ++n)
            was[n][0] = t.mass[n] * t.w2[n], was[n][1] = t.mass[n];
        t.hz = hz;
        const double a = Physics::kBoxW, b = Physics::kBoxH;
        // Along the bridge by pitch: A1 at the left end, A4 at the right.
        const double along = std::clamp((std::log2(hz / 55.0) / 3.0), 0.0, 1.0);
        const double x = (Physics::kBridgeX0 + (Physics::kBridgeX1 - Physics::kBridgeX0) * along) * a;
        const double yb = Physics::kBridgeY * b, e = x - m_barMid;
        // Where it sits: on the board, which moves as the bar it is (up and
        // down where the tongue is, tipping along it, rolling across it);
        // its tip over the panel itself.
        for (int k = 0; k < kFront; ++k) {
            t.psiB[k] = float(m_barY[k] + e * m_barRoll[k]);
            t.psiS[k] = float(m_barS[k]);
            t.psiX[k] = float(m_barRoll[k]);
        }
        // On a rigid box first, then the box's pull taken out: below the
        // box's resonances it gives (the tongue sounds flat), above them it
        // stiffens. Twice, as the length moves the coupling.
        // The secant on the tongue's own pitch until it is heard at hz, the
        // best kept should it not get there (a note in a coupled pair's gap).
        double target = hz;
        if (m_coupled) {
            double a = hz, ha = heardTongue(a, t.psiB, t.psiS);
            double b = hz * hz / std::max(ha, 1.0), hb = heardTongue(b, t.psiB, t.psiS);
            double best = std::fabs(ha - hz) < std::fabs(hb - hz) ? a : b;
            double bestErr = std::min(std::fabs(ha - hz), std::fabs(hb - hz));
            for (int it = 0; it < 8 && bestErr > hz * 1e-4; ++it) {
                if (std::fabs(hb - ha) < 1e-12)
                    break;
                const double c = std::clamp(b - (hb - hz) * (b - a) / (hb - ha), hz * 0.6, hz * 1.6);
                a = b, ha = hb;
                b = c, hb = heardTongue(b, t.psiB, t.psiS);
                if (std::fabs(hb - hz) < bestErr)
                    bestErr = std::fabs(hb - hz), best = b;
            }
            target = best;
        }
        const double L = tongueLength(target);
        const BeamModes bm = beamFor(L);
        t.L = float(L);
        t.bm = bm;
        const double m = kRhoA * L;
        const double base = std::sqrt(kEI / kRhoA) / (2 * M_PI * L * L);
        double sumP1 = 0, sumQ1 = 0;
        for (int n = 0; n < kBend; ++n) {
            const double f = bm.lambda[n] * bm.lambda[n] * base;
            const double M = m * bm.M[n];
            const double K = M * (2 * M_PI * f) * (2 * M_PI * f);
            t.modeHz[n] = float(f);
            t.mass[n] = float(M);
            t.w2[n] = float(K / M);
            t.tau[n] = float(1.0 / (M_PI * f / Knobs::tongueQ + Knobs::tongueLoss));
            t.inv[n] = float(1.0 / (M * m_rate * m_rate));
            // A mode too near what the rate can hold takes no part at all:
            // stepped explicitly against the panel, a mode within a few
            // samples a cycle would not stay put (and above 8 kHz or so a
            // tongue's mode is nothing anyone hears from a box).
            t.live[n] = f < 0.2 * m_rate;
            const double coupled = m_coupled && t.live[n] ? 1.0 : 0.0;
            // The root's force and moment, per metre of the mode's tip.
            t.gP[n] = float(coupled * bm.P[n] / bm.M[n] * K);
            t.gQ[n] = float(coupled * bm.Q[n] * L / bm.M[n] * K);
            // The base's acceleration, felt by the mode (as metres a sample).
            t.bP[n] = float(coupled * bm.P[n] / bm.M[n]);
            t.bQ[n] = float(coupled * bm.Q[n] * L / bm.M[n]);
            sumP1 += coupled * bm.P[n] / bm.M[n];
            sumQ1 += coupled * bm.Q[n] * L / bm.M[n];
            // Its own sound: a dipole of the volume it sweeps, ∝ ωd/c.
            const double vol = Physics::kWidth * L * bm.P[n];
            t.rad[n] = m_pathTongue && t.live[n] ? float(vol * (2 * M_PI * f) * Knobs::dipole / Physics::kC) : 0.0f;
        }
        // What the modes leave out of a force at the tip.
        const double coupled = m_coupled ? 1.0 : 0.0;
        t.resP1 = float(coupled * (1.0 - sumP1));
        t.resQ1 = float(coupled * (L - sumQ1));
        // The first twisting mode: a thin strip's (J = wh³/3·(1 − 0.63h/w)),
        // a quarter wave on the tongue, clamped at the bar.
        {
            const double w = Physics::kWidth, h = Physics::kThick;
            const double J = w * h * h * h / 3 * (1 - 0.63 * h / w);
            const double Ip = (w * h * h * h + h * w * w * w) / 12;
            const double ft = 1.0 / (4 * L) * std::sqrt(Physics::kShear * J / (Physics::kRho * Ip));
            const double inertia = Physics::kRho * Ip * L / 2; // ∫ρI_p φ², φ = sin(πξ/2)
            const int n = kBend;
            t.modeHz[n] = float(ft);
            t.mass[n] = float(inertia);
            t.w2[n] = float(std::pow(2 * M_PI * ft, 2));
            t.tau[n] = float(1.0 / (M_PI * ft / Knobs::tongueQ + Knobs::tongueLoss));
            t.inv[n] = float(1.0 / (inertia * m_rate * m_rate));
            const double pT = 2 / M_PI, mT = 0.5;
            t.gT = float(coupled * pT / mT * inertia * t.w2[n]);
            t.bT = float(coupled * pT / mT);
        }
        for (int k = 0; k < kFront; ++k)
            t.psiTip[k] = float(shape(k, x, std::min(0.98 * b, yb + L)));
        placeFinger(s, t.xi > 0 ? t.xi : Knobs::fingerAt);
        for (int n = 0; n < kModes; ++n)
            if (was[n][1] > 0) {
                const float wOld = std::sqrt(was[n][0] / was[n][1]), wNew = std::sqrt(t.w2[n]);
                t.q[n] *= std::sqrt(was[n][1] * wOld / (t.mass[n] * wNew));
                t.u[n] *= std::sqrt(was[n][1] * wNew / (t.mass[n] * wOld));
            }
        {
            // And never swinging into the panel it would have hit: a bent
            // tongue's swing stays clear of it.
            float tip = 0, speed = 0;
            for (int n = 0; n < kBend; ++n)
                if (t.live[n]) {
                    tip += std::fabs(t.q[n]);
                    speed += std::fabs(t.u[n]) * m_rate / (2 * kPi * t.modeHz[n]);
                }
            const float reach = std::max(tip, speed), room = 0.9f * float(Physics::kGap);
            if (reach > room)
                for (int n = 0; n < kModes; ++n)
                    t.q[n] *= room / reach, t.u[n] *= room / reach;
        }
        t.dirty = true;
        coefficients(s);
    }

    // Where along tongue s the finger meets it (xi of its length): how much
    // of each mode it reaches, what the modes leave out of its force at the
    // root, and how stiff the tongue is under it.
    void placeFinger(int s, float xi) {
        Slot &t = m_slot[s];
        xi = clampf(xi, 0.3f, 1.0f);
        t.xi = xi;
        const double L = t.L, xc = xi * L;
        t.xc = float(xc);
        double sumP = 0, sumQ = 0, atTip = 0, atFinger = 0;
        for (int n = 0; n < kBend; ++n) {
            const double phiC = t.live[n] ? beamShape(t.bm, n, xi) : 0.0;
            t.phiC[n] = float(phiC);
            sumP += t.bm.P[n] * phiC / t.bm.M[n];
            sumQ += t.bm.Q[n] * L * phiC / t.bm.M[n];
            if (t.live[n]) {
                atTip += phiC / (t.mass[n] * t.w2[n]);
                atFinger += phiC * phiC / (t.mass[n] * t.w2[n]);
            }
        }
        t.tipOver = atFinger > 0 ? float(atTip / atFinger) : 1.0f;
        const double coupled = m_coupled ? 1.0 : 0.0;
        t.resP = float(coupled * (1.0 - sumP));
        t.resQ = float(coupled * (xc - sumQ));
        // Static stiffness under the finger: the beam's bending, its root
        // turning and its root riding down into the bridge.
        const double Kr = rootTurn(L) * kEI / L;
        t.kStatic = float(1.0 / (xc * xc * xc / (3 * kEI) + xc * xc / Kr + 1.0 / Physics::kBridge));
        t.phiT = float(std::sin(0.5 * M_PI * xi));
        t.resT = float(coupled * (1.0 - (2 / M_PI) * t.phiT / 0.5));
        t.dirty = true;
    }

    // Each mode's step: exact poles for its frequency and its decay (its
    // own, and the finger's if it lies on the tongue).
    void coefficients(int s) {
        Slot &t = m_slot[s];
        t.dirty = false;
        for (int n = 0; n < kModes; ++n) {
            const float w = std::sqrt(t.w2[n]);
            if (n < kBend ? !t.live[n] : w / m_rate > 0.4f * kPi) {
                // Above what the rate can hold: still, and silent.
                t.d[n] = 0;
                t.c[n] = 1;
                t.q[n] = t.u[n] = 0;
                continue;
            }
            const float phi = n < kBend ? t.phiC[n] : t.phiT * float(Physics::kWidth) * 0.3f;
            const float sigma = 1.0f / t.tau[n] + t.dampC * phi * phi / (2.0f * t.mass[n]);
            const float r = std::exp(-sigma / m_rate);
            t.d[n] = r * r;
            t.c[n] = 1 + r * r - 2 * r * std::cos(w / m_rate);
        }
    }

    // A finger's push of `force` on tongue s held still: the beam's static
    // shape (on its modes) and the box's, the plug where it leaves the
    // cavity's pressure as it was. Added to whatever is moving: by
    // linearity what was ringing rings on.
    void addStatic(int s, float force) {
        Slot &t = m_slot[s];
        for (int n = 0; n < kBend; ++n)
            t.q[n] += force * t.phiC[n] / (t.mass[n] * t.w2[n]);
        t.q[kBend] += force * t.off * t.phiT / (t.mass[kBend] * t.w2[kBend]);
        if (!m_coupled)
            return;
        float dv = 0;
        for (int k = 0; k < kFront; ++k) {
            const float g = force * (t.psiB[k] + t.xc * t.psiS[k]) + force * t.off * t.psiX[k];
            const float dy = g / float(m_stiff[k]);
            m_y[k] += dy;
            dv += m_area[k] * dy;
        }
        m_xp += dv / m_holeS;
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    Panel m_panel[kPanel] = {};
    float m_hzPanel[kPanel] = {}, m_mass[kPanel] = {}, m_area[kPanel] = {}, m_rad[kPanel] = {};
    float m_d[kPanel] = {}, m_c[kPanel] = {}, m_inv[kPanel] = {};
    double m_stiff[kPanel] = {};
    float m_y[kPanel] = {}, m_v[kPanel] = {};
    double m_holeR = Physics::kHoleR;
    float m_boxHeard = -1; // what setBoxHeard was last asked for
    float m_holeS = float(M_PI * Physics::kHoleR * Physics::kHoleR);
    float m_xp = 0, m_vp = 0, m_dp = 0, m_cp = 0, m_invp = 0, m_massP = 0, m_holeRad = 1;
    float m_kCav = 0, m_gain = 0, m_wPanel = 0, m_leak = 0, m_force = 0;
    float m_knockPsi[kFront] = {};
    double m_barY[kFront] = {}, m_barS[kFront] = {}, m_barRoll[kFront] = {}, m_barMid = 0;

    Slot m_slot[kSlots];
    Knock m_knock;
    Svf m_tick;
    float m_lastQ = 0, m_lastFk = 0;
    float m_energy = 0, m_boxEnergy = 0;
    int m_quiet = 0;
    uint32_t m_clock = 0;
    bool m_sleeping = true, m_air = true, m_coupled = true, m_pathBox = true, m_pathTongue = true;
};

} // namespace tf::marimbula
