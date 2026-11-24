#pragma once

// A tromba marina, as physics. Not samples and not a recipe: the
// instrument's own parts, in SI units, integrated sample by sample. The
// measurements it is built from and checked against are in
// docs/tromba-marina.md (the survey behind them in
// docs/tromba-marina-research.md).
//
//   the string  one long gut string, 1.6 m from the nut to the bridge,
//               1.6 mm thick, tuned low (two octaves under the lead's
//               neck, G1 to F♯2). Velocity waves run along it in delay
//               lines between the four places where something happens to
//               them: the nut, the bow, the thumb and the bridge. Its
//               losses (air and the gut's own) are lumped at the bridge
//               in a one-pole filter whose low and high decay times are
//               gut's.
//   the thumb   the player never stops the string. The thumb only rests
//               on it, lightly, at one of its nodes: at a share 1/n of
//               the way from the nut. A light touch is a point damper (a
//               resistance twice the string's impedance): every mode that
//               moves there loses half its energy each time it passes;
//               only the modes with a node under the thumb, the
//               n-th harmonic and its multiples, pass untouched. So the
//               string sounds its n-th harmonic, whatever the bow does: the
//               flageolet, the only way the instrument is played. A thumb
//               moving to another node passes over places that are no node
//               of anything, where the string chokes for a moment.
//   the bow     drawn above the thumb, between it and the nut. The hair
//               grips the string (rosin's friction falls from static to
//               dynamic as the string slides under it, a hyperbola in the
//               sliding speed) and lets go once a period: the Helmholtz
//               motion, solved where the string's impedance line meets the
//               friction curve, with its hysteresis. With the thumb at a
//               node it is the n-th harmonic's own Helmholtz motion, a
//               corner running in each of the n loops at once. Between
//               Schelleng's limits it speaks; under them it whistles
//               on its upper partials, over them it sticks too long and
//               scratches.
//   the bridge  the tromba marina's own: a shoe whose one foot carries the
//               string while the other, free, stands a hair (20 µm, set by
//               the "guidon") off the soundboard. The string's swing in the
//               bow's plane rocks it about the loaded foot. Each period the
//               free foot lifts and comes down on the board: a unilateral
//               Hertzian contact with Hunt–Crossley damping, integrated
//               with the string's own impedance by an energy-conserving
//               scheme (the contact a discrete gradient of its potential,
//               solved by Newton), so it neither gains nor loses what
//               the physics doesn't. Its
//               knocks are the phase ruptures that make the sound a
//               trumpet's (Padilla & Gibiat): the buzz. A light bow's
//               rounded corner rocks the bridge too gently to close the gap;
//               a firm one's sharp corner kicks it over: the buzz has a
//               threshold in the bow's force.
//   the box     a long tapered soundboard of spruce, 1.35 m, 20 cm wide at
//               the bridge and 7 cm at the neck, glued along its edges to
//               the triangular box. Its modes are a strip's whose width
//               changes along it (each cross-section clamped at its edges,
//               a beam along the grain on an elastic foundation that
//               stiffens as the board narrows): computed when the
//               instrument is built, they crowd into the wide end, where
//               the bridge is. Each mode pushes air by the volume it
//               sweeps; above them, the board answers the bridge's force
//               as an infinite plate does (its volume acceleration F/ρh),
//               which is how the knocks are heard.
//   the sound   the pressure a metre in front of the box, at two
//               microphones, one by its lower half and one by its upper.
//
// Its cost does not grow with the note: six delay-line reads, one friction
// solve, one bridge step at four times the rate, and the board's modes at the
// rate.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace tf::tromba {

// --- what it is made of ---------------------------------------------------------------------------------

struct Physics {
    // The string: gut.
    static constexpr float kLength = 1.6f;      // m, nut to bridge
    static constexpr float kDiameter = 1.6e-3f; // m
    static constexpr float kGutRho = 1300.0f;   // kg/m³
    // Its decay on rigid ends, at its open fundamental and at 3 kHz (air
    // and the gut's own losses).
    static constexpr float kT60Low = 6.0f;  // s
    static constexpr float kT60High = 0.5f; // s at kLossHz
    static constexpr float kLossHz = 3000.0f;
    // The thumb: a point damper, this many times the string's impedance:
    // at 2 it absorbs the most a point damper can, half the energy that
    // passes, so a mode the thumb has left (a slide to another node) dies
    // within a few passes rather than going on under the bow.
    static constexpr float kThumbR = 2.0f;
    static constexpr float kThumbSlideS = 0.035f; // a slide to another node
    // Once the bow has left, the player's hand lies on the string: it dies
    // in this long (T60) instead of ringing on as a free harmonic.
    static constexpr float kHandT60 = 0.8f;
    // The bow: rosin's friction, a hyperbola in the sliding speed (Smith &
    // Woodhouse 2000), and the hair's grain while it slides.
    static constexpr float kMuS = 0.8f, kMuD = 0.3f, kV0 = 0.1f; // m/s
    static constexpr float kGrain = 0.03f;
    // The bridge, as seen from the free foot: its mass and its rocking
    // stiffness, the lever from the string's height to the foot, the gap
    // the guidon leaves, and the foot's contact with the board's plate.
    static constexpr float kBridgeMass = 0.75e-3f; // kg, effective at the free foot (a shoe of a couple of grams rocking about its end)
    static constexpr float kBridgeHz = 1000.0f;    // its rocking resonance, on the string
    static constexpr float kBridgeQ = 20.0f;
    static constexpr float kLever = 1.2f;         // string height over foot span
    static constexpr float kGap = 20e-6f;         // m (Padilla & Gibiat: "around 20 micrometers")
    static constexpr float kContactK = 1.06e9f;      // N/m^1.5, bone on a hardwood plate
    static constexpr float kContactAlpha = 1.5f;  // Hertz (computed as δ·√δ)
    static constexpr float kContactLambda = 1.5f; // s/m, Hunt–Crossley
    // The soundboard: spruce along the grain and across it, its size.
    static constexpr float kBoardLength = 1.35f;
    static constexpr float kBoardWide = 0.20f, kBoardNarrow = 0.07f; // m, at the bridge's end and at the neck's
    static constexpr float kBoardThick = 3.0e-3f;
    static constexpr float kSpruceRho = 430.0f;
    static constexpr float kEL = 11.0e9f, kET = 0.75e9f, kGLT = 0.70e9f, kNuLT = 0.37f;
    static constexpr float kBoardQ = 12.0f;
    static constexpr float kBridgeAt = 0.16f;  // m from the board's wide end
    static constexpr float kModesTop = 700.0f; // Hz: the board's own modes up to here, the infinite plate's answer over
    // The air.
    static constexpr float kAirRho = 1.2f;
};

constexpr int kOver = 4; // the string and bridge run at this many times the rate (two halfbands down)
static_assert(kOver == 4, "the way down is two halvings");
constexpr int kMinHarmonic = 2, kMaxHarmonic = 14;
constexpr int kMaxBoardModes = 48;

// A halfband lowpass for halving the rate: a Blackman-windowed sinc of N
// taps (N = 4k + 3), every other one zero but the middle. Push every input,
// read every other.
template <int N>
struct Halfband {
    static_assert(N % 4 == 3, "a halfband's taps");
    static constexpr int kLen = 64;
    float h[N] = {};
    float x[kLen] = {};
    int pos = 0;
    Halfband() {
        const int c = N / 2;
        float sum = 0;
        for (int i = 0; i < N; ++i) {
            const int n = i - c;
            const float sinc = n == 0 ? 0.5f : std::sin(kPi * 0.5f * float(n)) / (kPi * float(n));
            const float w = 0.42f - 0.5f * std::cos(kTwoPi * float(i) / float(N - 1)) +
                            0.08f * std::cos(2.0f * kTwoPi * float(i) / float(N - 1));
            h[i] = sinc * w;
            sum += h[i];
        }
        for (float &v : h)
            v /= sum;
    }
    void push(float v) {
        x[pos] = v;
        pos = (pos + 1) & (kLen - 1);
    }
    float read() const {
        float f = h[N / 2] * x[(pos - 1 - N / 2) & (kLen - 1)];
        for (int t = 0; t < N; t += 2)
            f += h[t] * x[(pos - 1 - t) & (kLen - 1)];
        return f;
    }
    void clear() {
        std::fill(std::begin(x), std::end(x), 0.0f);
        pos = 0;
    }
};

// --- the soundboard, built once ---------------------------------------------------------------------------

struct Board {
    int n = 0;
    float hz[kMaxBoardModes] = {};
    float atBridge[kMaxBoardModes] = {}; // the mass-normalised shape under the bridge
    float volL[kMaxBoardModes] = {}, volR[kMaxBoardModes] = {}; // the volume it sweeps per unit, weighted by each mic's distance
    float bridgeL = 1, bridgeR = 1;      // the bridge's own distance weighting, for the infinite plate's answer
    // Over the modes, what the infinite plate's answer adds to theirs: its
    // volume acceleration per newton, 1/ρh, less what the modes already
    // give there (each, above its resonance, ψ·vol per newton).
    float residL = 0, residR = 0;
    float rhoH = 1;                      // kg/m²
    float fundamental = 0;
};

// The clamped–clamped beam's first mode across the board's width, on
// ξ ∈ [0, 1], scaled so ∫φ² = 1.
inline double crossShape(double xi) {
    const double l = 4.730040744862704;
    const double s = (std::cosh(l) - std::cos(l)) / (std::sinh(l) - std::sin(l));
    return std::cosh(l * xi) - std::cos(l * xi) - s * (std::sinh(l * xi) - std::sin(l * xi));
}

// Cyclic Jacobi for a symmetric matrix (row-major, n×n): its eigenvalues,
// and its eigenvectors as the columns of V.
inline std::vector<double> jacobi(std::vector<double> A, int n, std::vector<double> &V) {
    V.assign(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n; ++i)
        V[size_t(i) * n + i] = 1.0;
    for (int sweep = 0; sweep < 80; ++sweep) {
        double off = 0, diag = 0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                (i == j ? diag : off) += A[size_t(i) * n + j] * A[size_t(i) * n + j];
        if (off < 1e-22 * diag)
            break;
        for (int p = 0; p < n - 1; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = A[size_t(p) * n + q];
                if (std::fabs(apq) < 1e-300)
                    continue;
                const double app = A[size_t(p) * n + p], aqq = A[size_t(q) * n + q];
                const double th = 0.5 * (aqq - app) / apq;
                const double t = (th >= 0 ? 1.0 : -1.0) / (std::fabs(th) + std::sqrt(th * th + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = A[size_t(k) * n + p], akq = A[size_t(k) * n + q];
                    A[size_t(k) * n + p] = c * akp - s * akq;
                    A[size_t(k) * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = A[size_t(p) * n + k], aqk = A[size_t(q) * n + k];
                    A[size_t(p) * n + k] = c * apk - s * aqk;
                    A[size_t(q) * n + k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = V[size_t(k) * n + p], vkq = V[size_t(k) * n + q];
                    V[size_t(k) * n + p] = c * vkp - s * vkq;
                    V[size_t(k) * n + q] = s * vkp + c * vkq;
                }
            }
    }
    std::vector<double> ev(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        ev[size_t(i)] = A[size_t(i) * n + i];
    return ev;
}

// The board as a strip of varying width w(x): each cross-section moves in
// the clamped beam's first mode across it, W(x)·φ(y/w), and Galerkin on
// that shape leaves a beam along the grain (D_x·w), a tension from the
// twisting (2H·∫φ'²/w) and a foundation from the bending across it
// (D_y·λ⁴/w³), with a mass ρh·w. Finite differences along its length,
// clamped at both ends, solved for its modes.
inline Board buildBoard() {
    using P = Physics;
    Board b;
    const double h = P::kBoardThick;
    const double nuTL = double(P::kNuLT) * P::kET / P::kEL;
    const double den = 1.0 - double(P::kNuLT) * nuTL;
    const double Dx = double(P::kEL) * h * h * h / (12.0 * den);
    const double Dy = double(P::kET) * h * h * h / (12.0 * den);
    const double Dk = double(P::kGLT) * h * h * h / 12.0;
    const double H = nuTL * Dx + 2.0 * Dk;
    const double rhoH = double(P::kSpruceRho) * h;
    b.rhoH = float(rhoH);
    // The cross shape's integrals.
    double phi1 = 0, phiD2 = 0, phiSum = 0;
    {
        const int K = 2000;
        for (int i = 0; i < K; ++i) {
            const double xi = (i + 0.5) / K, d = 1e-4;
            const double f = crossShape(xi);
            const double fd = (crossShape(xi + d) - crossShape(xi - d)) / (2 * d);
            phi1 += f / K;
            phiD2 += fd * fd / K;
            phiSum += f * f / K;
        }
        const double norm = 1.0 / std::sqrt(phiSum);
        phi1 *= norm;
        phiD2 *= norm * norm;
    }
    const double lam4 = std::pow(4.730040744862704, 4.0);
    const double phiMid = crossShape(0.5) / std::sqrt(phiSum);
    const int M = 96; // interior points
    const double L = P::kBoardLength, dx = L / (M + 1);
    auto width = [&](double x) { return P::kBoardWide + (P::kBoardNarrow - P::kBoardWide) * (x / L); };
    std::vector<double> K(size_t(M) * M, 0.0), mass(static_cast<size_t>(M));
    auto add = [&](int i, int j, double v) {
        if (i >= 0 && i < M && j >= 0 && j < M)
            K[size_t(i) * M + j] += v;
    };
    // Node i (0..M−1) at x = (i+1)·dx. Curvature at node i:
    // (W_{i+1} − 2W_i + W_{i−1})/dx², clamped ends with W = 0 and a ghost
    // mirroring the neighbour (W' = 0), as a stencil c over i−1, i, i+1.
    for (int i = -1; i <= M; ++i) {
        const double x = (i + 1) * dx, w = width(x);
        // Bending along the grain, at node i (the end nodes too: their
        // curvature from the ghost).
        int idx[3] = {i - 1, i, i + 1};
        double c[3] = {1.0 / (dx * dx), -2.0 / (dx * dx), 1.0 / (dx * dx)};
        if (i == -1 || i == M) {
            // The clamped end: W = 0 there and W' = 0, so its curvature is
            // 2·W_neighbour/dx².
            const int nb = i == -1 ? 0 : M - 1;
            idx[0] = nb;
            c[0] = 2.0 / (dx * dx);
            idx[1] = idx[2] = -1;
            c[1] = c[2] = 0;
        }
        const double kb = Dx * w * dx * (i == -1 || i == M ? 0.5 : 1.0);
        for (int a = 0; a < 3; ++a)
            for (int bb = 0; bb < 3; ++bb)
                add(idx[a], idx[bb], kb * c[a] * c[bb]);
        // The twisting term on the link from i to i+1.
        if (i < M) {
            const double wl = width(x + 0.5 * dx);
            const double kt = 2.0 * H * phiD2 / wl / dx;
            add(i, i, kt);
            add(i + 1, i + 1, kt);
            add(i, i + 1, -kt);
            add(i + 1, i, -kt);
        }
        // The foundation, and the mass.
        if (i >= 0 && i < M) {
            add(i, i, Dy * lam4 / (w * w * w) * dx);
            mass[size_t(i)] = rhoH * w * dx;
        }
    }
    // Symmetrise with the mass: A = M^−½ K M^−½.
    std::vector<double> A(K);
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < M; ++j)
            A[size_t(i) * M + j] /= std::sqrt(mass[size_t(i)] * mass[size_t(j)]);
    std::vector<double> V;
    const auto ev = jacobi(A, M, V);
    std::vector<int> order(static_cast<size_t>(M));
    for (int i = 0; i < M; ++i)
        order[size_t(i)] = i;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return ev[size_t(x)] < ev[size_t(y)]; });
    // The microphones: a metre in front, one by the board's lower half,
    // one by its upper; and the bridge's place.
    const double micL = 0.35, micR = 0.95, dist = 1.0;
    const int at = std::clamp(int(std::lround(P::kBridgeAt / dx)) - 1, 0, M - 1);
    b.bridgeL = float(dist / std::sqrt(dist * dist + (P::kBridgeAt - micL) * (P::kBridgeAt - micL)));
    b.bridgeR = float(dist / std::sqrt(dist * dist + (P::kBridgeAt - micR) * (P::kBridgeAt - micR)));
    for (int r = 0; r < M && b.n < kMaxBoardModes; ++r) {
        const int k = order[size_t(r)];
        const double hz = std::sqrt(std::max(ev[size_t(k)], 0.0)) / (2 * M_PI);
        if (hz > P::kModesTop)
            break;
        double vl = 0, vr = 0;
        for (int i = 0; i < M; ++i) {
            const double x = (i + 1) * dx;
            const double psi = V[size_t(i) * M + k] / std::sqrt(mass[size_t(i)]);
            const double vol = psi * width(x) * phi1 * dx;
            vl += vol * dist / std::sqrt(dist * dist + (x - micL) * (x - micL));
            vr += vol * dist / std::sqrt(dist * dist + (x - micR) * (x - micR));
        }
        const double psiB = V[size_t(at) * M + k] / std::sqrt(mass[size_t(at)]) * phiMid;
        b.hz[b.n] = float(hz);
        b.atBridge[b.n] = float(psiB);
        b.volL[b.n] = float(vl);
        b.volR[b.n] = float(vr);
        ++b.n;
    }
    b.fundamental = b.n > 0 ? b.hz[0] : 0.0f;
    double sl = 0, sr = 0;
    for (int k = 0; k < b.n; ++k) {
        sl += double(b.atBridge[k]) * b.volL[k];
        sr += double(b.atBridge[k]) * b.volR[k];
    }
    b.residL = float(b.bridgeL / rhoH - sl);
    b.residR = float(b.bridgeR / rhoH - sr);
    return b;
}

inline const Board &board() {
    static const Board b = buildBoard();
    return b;
}

// Built once, off the audio thread.
inline void prepare() { (void)board(); }

// The harmonic of an open string tuned to refHz nearest a note at hz (in
// cents), within the ones a player reaches.
inline int harmonicFor(float hz, float refHz) {
    if (!(hz > 0) || !(refHz > 0))
        return kMinHarmonic;
    const float r = hz / refHz;
    const int lo = std::max(1, int(std::floor(r)));
    // Between lo and lo+1, the nearer in pitch: the geometric mean.
    const int n = r >= std::sqrt(float(lo) * float(lo + 1)) ? lo + 1 : lo;
    return std::clamp(n, kMinHarmonic, kMaxHarmonic);
}

// --- the player ---------------------------------------------------------------------------------------
//
// How a player bows a note: the press is the bow's weight, and a player
// leaning in draws it faster too, keeping the force a share of the most the
// bow can use at that speed and place (Schelleng's limit), so the string
// stays in its Helmholtz motion; pressed to the end, the share passes the
// limit and it scratches. The bow stays near the nut: higher on the pad,
// nearer still. On the higher harmonics the loop it bows is short, so it
// sits a larger share of the way along it.
struct Bowing {
    float force = 0, speed = 0, beta = 0.15f;
};
inline Bowing bowFor(float press, float y, float vel, int harmonic, float openHz) {
    press = clampf(press, 0.0f, 1.0f);
    y = clampf(y, 0.0f, 1.0f);
    // The 13th and 14th harmonics (and the notes above them, on a string
    // retuned up) only speak under some weight: lighter, the bow catches
    // them in several slips a period, some an octave or a twelfth off.
    if (harmonic >= 13)
        press = std::max(press, 0.45f);
    Bowing b;
    b.speed = (0.11f + 0.21f * press) * (0.85f + 0.3f * clampf(vel, 0.0f, 1.0f));
    // A light bow is drawn further from the nut; leaning in, nearer.
    const float in = clampf((press - 0.2f) / 0.5f, 0.0f, 1.0f);
    const float near = in * in * (3.0f - 2.0f * in);
    b.beta = clampf((0.27f + (0.18f - 0.27f) * near) * (1.15f - 0.3f * y), 0.1f, 0.42f);
    const float over = std::max(0.0f, press - 0.88f);
    // The high harmonics, bowed on a short loop, need more of it to keep
    // their Helmholtz motion at all (Schelleng's least force grows as the
    // loop shortens against the string's losses): a player leans on them.
    const float least = clampf(0.4f + 0.03f * float(harmonic - 6), 0.4f, 0.75f);
    const float share = least + (0.85f - least) * press + 22.0f * over * over;
    b.force = share * 2.0f * Physics::kGutRho * kPi * 0.25f * Physics::kDiameter * Physics::kDiameter * 2.0f *
              Physics::kLength * openHz * b.speed / ((Physics::kMuS - Physics::kMuD) * b.beta);
    return b;
}

// --- the instrument -------------------------------------------------------------------------------------

class TrombaMarina {
public:
    void init(float rate) {
        m_rate = rate;
        m_rateI = rate * float(kOver);
        m_dtI = 1.0f / m_rateI;
        m_dt = 1.0f / rate;
        // Lines as long as the lowest open string a voice can be tuned to
        // (24 Hz: the −OCT voice under the neck shifted an octave down, on
        // its 2nd harmonic), each segment as long as all of it.
        const size_t len = size_t(m_rateI / (2.0f * 24.0f)) + 8;
        for (auto *d : {&m_aR, &m_aL, &m_bR, &m_bL, &m_cR, &m_cL})
            d->init(len);
        m_maxLen = float(len) - 6.0f;
        const Board &bd = board();
        m_board = &bd;
        for (int k = 0; k < bd.n; ++k) {
            const float w = kTwoPi * bd.hz[k];
            // Symplectic Euler, its frequency pre-warped so the mode rings
            // exactly at w; its loss per sample.
            const float s = 2.0f * std::sin(0.5f * w * m_dt) / m_dt;
            m_bc[k] = s * s;
            m_bd[k] = std::exp(-w / (2.0f * Physics::kBoardQ) * m_dt);
        }
        m_hf.set(Physics::kModesTop, 0.6f, rate);
        silence();
    }

    void silence() {
        // (Its lines are a few hundred kilobytes for the four strings of a
        // voice: cleared only if something was ever written to them since.)
        if (!m_clean)
            for (auto *d : {&m_aR, &m_aL, &m_bR, &m_bL, &m_cR, &m_cL})
                d->clear();
        m_clean = true;
        m_lossY = 0;
        m_y = m_vy = m_yp = 0;
        std::fill(std::begin(m_q), std::end(m_q), 0.0f);
        std::fill(std::begin(m_u), std::end(m_u), 0.0f);
        m_down1.clear();
        m_down2.clear();
        m_hf.reset();
        m_stick = false;
        m_force = m_forceTo = m_speed = m_speedTo = 0;
        m_sleeping = true;
        m_quiet = 0;
        m_fresh = true;
        resetCounts();
    }

    // Where it is tuned and touched: the note it sounds and the harmonic
    // of the open string that is (the open string then stands at hz/n).
    // A new harmonic slides the thumb there along the string; jump puts
    // it there at once (a new note on a silent string).
    void setNote(float hz, int harmonic, bool jump = false) {
        if (!std::isfinite(hz))
            return;
        harmonic = std::clamp(harmonic, kMinHarmonic, kMaxHarmonic);
        hz = clampf(hz, 30.0f, 0.2f * m_rate);
        m_noteTo = hz;
        if (jump || m_fresh) {
            m_harm = harmonic;
            m_note = hz;
            m_touch = m_touchTo = 1.0f / float(harmonic);
            m_open = m_openTo = hz / float(harmonic);
            m_fresh = false;
            return;
        }
        if (harmonic != m_harm) {
            m_harm = harmonic;
            m_touchTo = 1.0f / float(harmonic);
            m_sliding = true;
        }
        m_openTo = hz / float(harmonic);
    }

    // The bow: how hard it presses (N), how fast it is drawn (m/s, its
    // sign the direction) and where, as a share of the way from the nut to
    // the thumb. A force of 0 lifts it off the string.
    void setBow(float force, float speed, float beta) {
        m_forceTo = std::isfinite(force) ? clampf(force, 0.0f, 20.0f) : 0.0f;
        m_speedTo = std::isfinite(speed) ? clampf(speed, -2.0f, 2.0f) : 0.0f;
        m_betaTo = std::isfinite(beta) ? clampf(beta, 0.03f, 0.5f) : 0.15f;
        if (m_forceTo > 0 && m_sleeping) {
            m_sleeping = false;
            m_quiet = 0;
            m_beta = m_betaTo;
        }
    }

    // The guidon: how far the free foot stands off the board (m). Players
    // set it between about 10 and 40 µm for more or less bray.
    void setGap(float metres) {
        if (std::isfinite(metres))
            m_gap = clampf(metres, 5e-6f, 60e-6f);
    }
    float gap() const { return m_gap; }

    // Adds what the two microphones hear, in pascals times gain.
    void render(float *l, float *r, int n, float gain) {
        if (m_sleeping)
            return;
        m_clean = false;
        const Board &bd = *m_board;
        using P = Physics;
        // The segment's targets: the string's tuning and the thumb's place
        // slide (a few tens of milliseconds) when the thumb moves to
        // another node, otherwise follow at once; the bow's numbers ramp
        // across the block.
        const float dtBlock = float(n) * m_dt;
        const float slideA = m_sliding ? 1.0f - std::exp(-dtBlock / P::kThumbSlideS) : 1.0f;
        const float open0 = m_open, touch0 = m_touch;
        m_open = m_sliding ? m_open * std::pow(m_openTo / m_open, slideA) : m_openTo;
        m_touch += (m_touchTo - m_touch) * slideA;
        if (m_sliding && std::fabs(m_touch - m_touchTo) < 1e-4f * m_touchTo &&
            std::fabs(m_open / m_openTo - 1.0f) < 1e-4f) {
            m_touch = m_touchTo;
            m_open = m_openTo;
            m_sliding = false;
        }
        m_note = m_noteTo;
        const float beta0 = m_beta;
        m_beta = m_betaTo;
        // The string for its open pitch: its impedance (tension from the
        // pitch: c = 2Lf, T = μc²), its losses and the delays.
        const float mu = Physics::kGutRho * kPi * 0.25f * P::kDiameter * P::kDiameter;
        const float c = 2.0f * P::kLength * m_open;
        const float Z = mu * c;
        // Loss per round trip, a one-pole lowpass with gut's decay times.
        const float hand = m_forceTo > 0 ? 1.0f : std::exp(-6.9078f / (P::kHandT60 * m_open));
        const float g0 = std::exp(-6.9078f / (P::kT60Low * m_open)) * hand;
        const float gh = std::exp(-6.9078f / (P::kT60High * m_open)) * hand;
        const float wl = kTwoPi * P::kLossHz / m_rateI;
        // |g0(1−p)/(1−p·e^−jw)| = gh: solve for p.
        float p = 0;
        {
            const float q = (gh / g0) * (gh / g0), cw = std::cos(wl);
            // (1−p)² = q·(1 − 2p·cw + p²)  →  (1−q)p² − 2(1 − q·cw)p + (1−q) = 0
            const float A = 1.0f - q, B = -2.0f * (1.0f - q * cw);
            const float disc = std::max(B * B - 4.0f * A * A, 0.0f);
            p = A > 1e-9f ? clampf((-B - std::sqrt(disc)) / (2.0f * A), 0.0f, 0.95f) : 0.0f;
        }
        m_lossG = g0;
        m_lossP = p;
        // The delays: the round trip is the open string's period, less
        // what the loss filter and the bridge's give add at the note; the
        // thumb at 1/n of the string as the note hears it.
        const float wn = kTwoPi * m_note / m_rateI;
        const float tauLoss = std::atan2(p * std::sin(wn), 1.0f - p * std::cos(wn)) / std::max(wn, 1e-6f);
        const float tauBridge = bridgeDelay(Z, m_note) * m_rateI;
        auto delays = [&](float open, float touch, float beta, float *d) {
            const float N1 = m_rateI / (2.0f * open);
            const float end = N1 - 0.5f * (tauLoss + tauBridge);
            const float toThumb = N1 * touch;
            // The bow's place is a share of the loop the thumb is heading
            // for, so it moves (a few millimetres) when a slide begins,
            // not along with the thumb.
            d[0] = clampf(beta * N1 * m_touchTo, 2.0f, std::min(m_maxLen, toThumb - 2.0f));
            d[1] = clampf(toThumb - d[0], 2.0f, m_maxLen);
            d[2] = clampf(end - toThumb, 2.0f, m_maxLen);
        };
        float d0[3], d1[3];
        delays(open0, touch0, beta0, d0);
        delays(m_open, m_touch, m_beta, d1);
        const int steps = n * kOver;
        const float inv = 1.0f / float(steps);
        float dd[3];
        for (int j = 0; j < 3; ++j)
            dd[j] = (d1[j] - d0[j]) * inv;
        float dA = d0[0], dB = d0[1], dC = d0[2];
        const float force0 = m_force, speed0 = m_speed;
        const float forceTo = m_forceTo;
        const float dForce = (forceTo - force0) * inv, dSpeed = (m_speedTo - speed0) * inv;
        float force = force0, speed = speed0;
        m_force = forceTo;
        m_speed = m_speedTo;
        // The thumb: a damper of R = kThumbR·Z; what it lets through.
        const float tau = 2.0f / (P::kThumbR + 2.0f);
        // The bridge.
        const float m = P::kBridgeMass, kb = m * (kTwoPi * P::kBridgeHz) * (kTwoPi * P::kBridgeHz);
        const float cb = m * kTwoPi * P::kBridgeHz / P::kBridgeQ;
        const float rl = P::kLever, dtI = m_dtI;
        const float twoZ = 2.0f * Z, inv4Z = 1.0f / (4.0f * Z);
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            for (int o = 0; o < kOver; ++o) {
                dA += dd[0];
                dB += dd[1];
                dC += dd[2];
                force += dForce;
                speed += dSpeed;
                const float aR = m_aR.read(dA), aL = m_aL.read(dA);
                const float bR = m_bR.read(dB), bL = m_bL.read(dB);
                const float cR = m_cR.read(dC), cL = m_cL.read(dC);
                // The nut: rigid, the wave turned over.
                m_aR.push(-aL);
                // The bow: where the string's impedance line meets the
                // friction curve.
                const float vh = aR + bL;
                float v = vh;
                if (force > 0) {
                    const float delta = speed - vh;
                    const float A = std::fabs(delta);
                    const float N = force * (m_stick ? 1.0f : 1.0f + P::kGrain * m_rng.bipolar());
                    const bool canStick = twoZ * A <= P::kMuS * N;
                    bool slip = !canStick;
                    float u = 0;
                    if (!canStick || !m_stick) {
                        const float B = twoZ * (A - P::kV0) - N * P::kMuD;
                        const float disc = B * B + 8.0f * Z * P::kV0 * (twoZ * A - N * P::kMuS);
                        if (disc >= 0) {
                            u = (B + std::sqrt(disc)) * inv4Z;
                            slip = u > 0 || !canStick;
                        } else {
                            slip = !canStick;
                        }
                        if (!canStick)
                            u = std::max(u, 0.0f);
                    }
                    if (slip) {
                        v = speed - (delta >= 0 ? u : -u);
                        if (m_stick)
                            ++m_slips;
                        ++m_slipSamples;
                    } else {
                        v = speed;
                    }
                    m_stick = !slip;
                    ++m_bowSamples;
                } else {
                    m_stick = false;
                }
                m_aL.push(v - aR);
                m_bR.push(v - bL);
                peak = std::max(peak, std::fabs(v));
                // The thumb.
                const float vf = tau * (bR + cL);
                m_bL.push(vf - bR);
                m_cR.push(vf - cL);
                // The bridge: the string's loss, then the free foot's
                // rocking, solved with the string's impedance and the
                // contact linearised over the step.
                m_lossY = m_lossG * (1.0f - m_lossP) * cR + m_lossP * m_lossY;
                const float in = m_lossY;
                // An energy-conserving step (Störmer–Verlet with the
                // contact's potential as a discrete gradient, Hunt–Crossley's
                // loss and the string's own impedance centred): for
                // s = y(n+1) − y(n−1),
                //   m·(s − 2Δ)/k² + kb·(2y(n−1) + s)/2 + D·s/(2k) + Φ(s) = 2rZ·in,
                // Δ = y(n) − y(n−1), D = c + λKδ(n)^α + r²Z, and Φ(s) the
                // potential's slope between y(n−1) and y(n+1): increasing in
                // s, so one root, which Newton finds from the linear guess.
                const float yp = m_yp, y0 = m_y;
                const float pen0 = std::max(0.0f, -m_gap - y0);
                const float dc = cb + P::kContactLambda * P::kContactK * pen0 * std::sqrt(pen0) + rl * rl * Z;
                const float A = m / (dtI * dtI) + 0.5f * kb + dc / (2.0f * dtI);
                const float B = -2.0f * m * (y0 - yp) / (dtI * dtI) + kb * yp - 2.0f * rl * Z * in;
                const float Vp = potential(yp);
                float sN = -B / A;
                float phi = 0;
                for (int it = 0; it < 8; ++it) {
                    float dphi = 0;
                    phi = slopeBetween(yp, sN, Vp, dphi);
                    const float G = A * sN + B + phi;
                    const float step = G / (A + dphi);
                    sN -= step;
                    if (std::fabs(step) < 1e-13f + 1e-6f * std::fabs(sN))
                        break;
                }
                phi = [&] { float d; return slopeBetween(yp, sN, Vp, d); }();
                const float y1 = yp + sN;
                const float vy = sN / (2.0f * dtI); // the foot's speed at step n
                m_yp = y0;
                m_y = y1;
                m_vy = vy;
                // The contact's force on the foot (up), and its knocks.
                const float fc = -phi - P::kContactLambda * P::kContactK * pen0 * std::sqrt(pen0) * vy;
                if (pen0 > 0 || y1 < -m_gap) {
                    ++m_contactSamples;
                    if (!m_touching)
                        ++m_knocks;
                    m_touching = true;
                } else {
                    m_touching = false;
                }
                m_yLow = std::min(m_yLow, y0);
                m_cL.push(rl * vy - in);
                ++m_samples;
                // What the bridge's feet put into the board (upward):
                // its spring and damper through the loaded foot, less the
                // free foot's knock.
                const float ft = kb * 0.5f * (y1 + yp) + cb * vy - fc;
                // Down to the rate, in two halvings.
                m_down1.push(ft);
                if (o & 1)
                    m_down2.push(m_down1.read());
            }
            const float f = m_down2.read();
            // The board: its modes, driven under the bridge, each pushing
            // air by the volume it sweeps (pressure ∝ volume acceleration);
            // over them the infinite plate's answer, F/ρh.
            float al = 0, ar = 0;
            for (int k = 0; k < bd.n; ++k) {
                const float u0 = m_u[k];
                float u = u0 + m_dt * (bd.atBridge[k] * f - m_bc[k] * m_q[k]);
                u *= m_bd[k];
                m_u[k] = u;
                m_q[k] += m_dt * u;
                const float acc = (u - u0) * m_rate;
                al += bd.volL[k] * acc;
                ar += bd.volR[k] * acc;
            }
            const float hf = m_hf.step(f).high;
            const float scale = P::kAirRho / (2.0f * kPi) * gain;
            const float pl = (al + hf * bd.residL) * scale, pr = (ar + hf * bd.residR) * scale;
            l[i] += pl;
            r[i] += pr;
            m_out = std::max(m_out * 0.9995f, std::fabs(pl) + std::fabs(pr));
        }
        // Guarded: nothing it does is ever allowed to turn into a NaN.
        if (!std::isfinite(m_y) || !std::isfinite(m_vy) || !std::isfinite(m_lossY) || !std::isfinite(m_out)) {
            silence();
            return;
        }
        // Asleep once the bow is off and it has rung out.
        if (m_forceTo <= 0 && peak < 1e-6f && std::fabs(m_vy) < 1e-7f && m_out < 1e-7f * std::max(gain, 1e-6f)) {
            if (++m_quiet > int(0.05f * m_rate / float(n)) + 1)
                silence();
        } else {
            m_quiet = 0;
        }
    }

    bool active() const { return !m_sleeping; }

    // --- read-outs, for the tests ---
    void resetCounts() {
        m_samples = m_contactSamples = m_knocks = m_slips = m_slipSamples = m_bowSamples = 0;
        m_yLow = 0;
    }
    // How much of the time the free foot touched the board, how many times
    // it came down on it, and how many times the bow let go of the string
    // (and the share of its time it slid), per second of string time.
    float contactShare() const { return m_samples ? float(m_contactSamples) / float(m_samples) : 0.0f; }
    float knocksPerSecond() const { return m_samples ? float(m_knocks) * m_rateI / float(m_samples) : 0.0f; }
    float slipsPerSecond() const { return m_samples ? float(m_slips) * m_rateI / float(m_samples) : 0.0f; }
    float slipShare() const { return m_bowSamples ? float(m_slipSamples) / float(m_bowSamples) : 0.0f; }
    int harmonic() const { return m_harm; }
    float openHz() const { return m_open; }
    float footHeight() const { return m_y; }
    // The lowest the free foot came, toward the board, since the counts were reset (m).
    float footLowest() const { return m_yLow; }
    // The string's impedance at an open pitch, and Schelleng's most force a
    // bow at this speed and place can use before the string sticks too
    // long (Schelleng 1973: 2Z·v/((μs − μd)·β)).
    static float impedance(float openHz) {
        const float mu = Physics::kGutRho * kPi * 0.25f * Physics::kDiameter * Physics::kDiameter;
        return mu * 2.0f * Physics::kLength * openHz;
    }
    static float maxForce(float openHz, float speed, float beta) {
        return 2.0f * impedance(openHz) * std::fabs(speed) / ((Physics::kMuS - Physics::kMuD) * std::max(beta, 0.01f));
    }
    const Board &boardModes() const { return *m_board; }

private:
    // The contact's potential at foot height y (zero clear of the board):
    // K·δ^2.5/2.5 for δ = −gap − y > 0.
    float potential(float y) const {
        const float d = -m_gap - y;
        return d > 0 ? Physics::kContactK * d * d * std::sqrt(d) * 0.4f : 0.0f;
    }
    // Its slope between y and y + s, (V(y+s) − V(y))/s (the discrete
    // gradient), and that slope's derivative in s.
    float slopeBetween(float y, float s, float Vy, float &dslope) const {
        const float K = Physics::kContactK;
        if (std::fabs(s) < 1e-12f) {
            const float d = -m_gap - y - 0.5f * s;
            if (d <= 0) {
                dslope = 0;
                return 0;
            }
            const float sq = std::sqrt(d);
            dslope = 0.75f * K * sq; // V''/2
            return -K * d * sq;
        }
        const float y1 = y + s, d1 = -m_gap - y1;
        const float V1 = d1 > 0 ? K * d1 * d1 * std::sqrt(d1) * 0.4f : 0.0f;
        const float dV1 = d1 > 0 ? -K * d1 * std::sqrt(d1) : 0.0f; // V'(y1)
        const float sl = (V1 - Vy) / s;
        dslope = (dV1 - sl) / s;
        return sl;
    }

    // The bridge, seen from the string, is not rigid: its rocking is an
    // impedance (c + j(ωm − k/ω))/r². What its reflection adds to the
    // round trip at hz, in seconds.
    static float bridgeDelay(float Z, float hz) {
        using P = Physics;
        const float w = kTwoPi * hz;
        const float m = P::kBridgeMass, kb = m * (kTwoPi * P::kBridgeHz) * (kTwoPi * P::kBridgeHz);
        const float cb = m * kTwoPi * P::kBridgeHz / P::kBridgeQ;
        const float r2 = P::kLever * P::kLever;
        const float re = cb / r2, im = (w * m - kb / w) / r2;
        // R = (Z − Zb)/(Z + Zb); a rigid end is −1. The phase it adds.
        const float nr = Z - re, ni = -im, dr = Z + re, di = im;
        const float ph = std::atan2(ni, nr) - std::atan2(di, dr); // arg R
        float extra = ph - kPi;                                    // relative to −1
        while (extra > kPi)
            extra -= kTwoPi;
        while (extra <= -kPi)
            extra += kTwoPi;
        return -extra / w;
    }

    float m_rate = 48000, m_rateI = 96000, m_dt = 1.0f / 48000, m_dtI = 1.0f / 96000;
    float m_maxLen = 100;
    Delay m_aR, m_aL, m_bR, m_bL, m_cR, m_cL; // nut–bow, bow–thumb, thumb–bridge; R toward the bridge
    float m_lossY = 0, m_lossG = 0.99f, m_lossP = 0;
    float m_y = 0, m_vy = 0, m_yp = 0; // the free foot's height over its rest, its speed, and its height a step ago
    float m_gap = Physics::kGap;
    float m_yLow = 0;
    bool m_touching = false;
    bool m_stick = false;
    float m_force = 0, m_forceTo = 0, m_speed = 0, m_speedTo = 0, m_beta = 0.15f, m_betaTo = 0.15f;
    float m_note = 220, m_noteTo = 220, m_open = 55, m_openTo = 55, m_touch = 0.25f, m_touchTo = 0.25f;
    int m_harm = 4;
    bool m_sliding = false, m_fresh = true;
    bool m_clean = false; // its lines are all zeros: nothing to clear
    const Board *m_board = nullptr;
    float m_q[kMaxBoardModes] = {}, m_u[kMaxBoardModes] = {}, m_bc[kMaxBoardModes] = {}, m_bd[kMaxBoardModes] = {};
    Svf m_hf;
    Halfband<15> m_down1; // the string's rate to half of it
    Halfband<31> m_down2; // and to the rate
    float m_out = 0;
    bool m_sleeping = true;
    int m_quiet = 0;
    Rng m_rng{0x7A0B1u};
    int64_t m_samples = 0, m_contactSamples = 0, m_knocks = 0, m_slips = 0, m_slipSamples = 0, m_bowSamples = 0;
};

} // namespace tf::tromba
