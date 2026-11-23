#pragma once

// A slit drum, as physics: a tuned wooden tongue drum, the box with H-shaped
// cuts in its top. Not samples and not a recipe: the instrument's own parts,
// in SI units, integrated sample by sample. The measurements it is built
// from and checked against are in docs/slit-drum.md.
//
//   the tongues  four H-shaped cuts in a 13 mm padauk top leave eight
//                tongues 40 mm wide, two to each H, pointing at each other
//                across its crossbar. Each is a cantilever: a strip of wood
//                held at its root and free at its tip. Its modes are found as
//                a beam's are, by finite elements, once: the tongue and the
//                strip of top behind its root (the land, held by the end
//                wall), so the root is not a clamp but gives a little, as a
//                real one does. Timoshenko's elements: wood is far stiffer
//                along its grain than in shear (E/G ≈ 12), so a tongue's
//                bending modes come out below the slender-beam ratios
//                (1 : 6.27 : 17.5 : 34.4), and its first twisting mode,
//                which a steel tongue would put near the second bending
//                mode, falls between the first two, at 3 to 4: none of it
//                harmonic. Each mode is a damped oscillator with its modal
//                mass from the wood, its loss from wood's own loss factor
//                (along the grain for bending, shear for twisting, and the
//                shear share of each bending mode), the land and the glue it
//                leaks into, and what it radiates.
//   the box      a closed box of 12 mm walls holding 17 litres of air. The
//                cuts are its ports: a 2 mm kerf round each H is a plug of
//                air on the cavity's spring, a Helmholtz resonator low in
//                the tongues' range and broad (a long slit radiates its
//                energy quickly). Above it, the box's own standing waves,
//                every one coupled to every tongue by how much of the mode's
//                pressure the tongue's motion meets. Between the H's the top
//                is strips of wood running wall to wall, clamped at both
//                ends; the land behind each root is one plate with the strips
//                beside it, so a tongue pulls on them across the grain. The
//                strips, the bottom and the walls give under the pressure
//                and ring of their own: the knock of the box. So a struck
//                tongue pumps the air and shakes the top, the air and the
//                top push back on every tongue (the sympathy between them),
//                and the box breathes out of every slit: the hollow, woody
//                bloom of a slit drum is the tongue and the box together, as
//                in a bass-reflex cabinet.
//   the mallet   a ball on a stick, held loosely, its contact a stiffening
//                Hertzian spring (F = K·δ^1.5) with Hunt–Crossley loss,
//                solved against the tongue's own motion every sample: a
//                rubber superball touches for two to four milliseconds, a
//                hard wooden one a fraction of one, a fingertip's pulp
//                several. The shorter the contact, the higher the partials
//                it reaches. Where it lands decides which modes it reaches:
//                the tip moves in every bending mode, about three quarters
//                of the way out is the second mode's node (the purest note),
//                near the root only the stiff upper modes speak, and off the
//                centre line the tongue twists. The hand lifts as the ball
//                lands, and lets it bounce.
//   the sound    two microphones either side of the player hear the volume
//                acceleration of every part: each tongue's top face, the air
//                in each slit, the panels; the modes that only rock the air
//                from side to side as dipoles. Each tongue reaches each
//                microphone after its own time of flight.
//
// Its cost does not grow with the number of mallets: the box rings once,
// whatever strikes it, and a strike only adds a contact for the few
// milliseconds it touches.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "dsp.h"

namespace tf::slit {

constexpr int kTongues = 8;      // two to each H, lowest (0) to highest (7)
constexpr int kFrame = kTongues; // a strike on the box itself
constexpr int kCuts = kTongues / 2;

// --- the numbers ----------------------------------------------------------------------------------------
//
// SI units. Sources in docs/slit-drum.md.

struct Physics {
    // Padauk, the classic tongue-drum top: density, its moduli along the
    // grain, across it and in shear, and its loss factors.
    static constexpr float kWood = 745.0f;    // kg/m³
    static constexpr float kEL = 13.1e9f;     // Pa, along the grain
    static constexpr float kER = 1.4e9f;      // Pa, across it
    static constexpr float kG = 1.1e9f;       // Pa, shear in the planes that hold the grain
    static constexpr float kEtaL = 0.005f;    // loss factor along the grain
    static constexpr float kEtaG = 0.02f;     // in shear
    static constexpr float kEtaJoint = 0.012f; // the land, the glue and the wall behind a root
    // The box, outside, and its wood.
    static constexpr float kDepth = 0.272f, kHeight = 0.20f; // its length is what the cuts need
    static constexpr float kTop = 0.013f, kWall = 0.012f;
    // The cuts: tongues 40 mm wide, a 2 mm kerf, 6 mm between two tips.
    static constexpr float kWidth = 0.040f, kKerf = 0.002f, kGap = 0.006f;
    static constexpr float kLand = 0.03f; // from the end wall to a root
    static constexpr float kLowest = 0.172f; // m, a first guess at the lowest tongue
    // How wide a strip of the top behind a root bends with the tongue, in
    // tongue widths.
    static constexpr float kLandWidth = 2.0f;
    // The notes the cuts were sized for (the maker's trim does the rest):
    // a major scale up from G3.
    static constexpr float kRootHz = 196.0f;
    // Air.
    static constexpr float kRhoAir = 1.2f, kC = 343.0f, kMu = 1.81e-5f;
    // The box's air modes lose to its walls.
    static constexpr float kAirQ = 35.0f;
    // The panels: wood with glued joints.
    static constexpr float kEtaPanel = 0.025f;
    // Inside.
    static constexpr float innerDepth() { return kDepth - 2 * kWall; }
    static constexpr float innerHeight() { return kHeight - kTop - kWall; }
};

// The model's constants that measured spectra and decays set (docs/slit-drum.md
// says which measurement each one answers to).
struct Knobs {
    static inline float level = 0.1f;
    static inline float radLobe = 0.5f;   // a bending mode's lobes, as dipoles
    static inline float radTwist = 0.5f;  // a twisting tongue's two edges
    static inline float radPanel = 0.3f;  // a panel's lobes
    static inline float spread = 0.015f; // a piece of wood's own scatter in its upper ratios
};

// What a mode is.
enum Kind : uint8_t { Bend, Twist, Wall, Port, Air };

// --- a tongue's modes, by finite elements ---------------------------------------------------------------
//
// The tongue and its land as one beam, clamped at the end wall: the land
// (kLandWidth tongues wide) then the tongue. Timoshenko elements for bending,
// linear ones for twisting; the generalised eigenproblem K·φ = ω²·M·φ by
// Cholesky and Jacobi. Done once for the geometry (the wood and the cuts
// don't change; the maker's trim scales every mode of a tongue together).

namespace fe {

constexpr double kPiD = 3.14159265358979323846;

constexpr int kLand = 4, kAlong = 16, kElems = kLand + kAlong, kNodes = kElems + 1;
constexpr int kB = 4, kT = 3; // bending and twisting modes kept

// Symmetric eigenproblem by cyclic Jacobi: a (n×n, row-major) is destroyed,
// its eigenvalues land in w and its eigenvectors in the columns of v.
inline void jacobi(double *a, double *v, double *w, int n) {
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            v[i * n + j] = i == j ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                off += a[i * n + j] * a[i * n + j];
        if (off < 1e-22)
            break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = a[p * n + q];
                if (std::fabs(apq) < 1e-300)
                    continue;
                const double th = (a[q * n + q] - a[p * n + p]) / (2.0 * apq);
                const double t = (th >= 0 ? 1.0 : -1.0) / (std::fabs(th) + std::sqrt(th * th + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = a[k * n + p], akq = a[k * n + q];
                    a[k * n + p] = c * akp - s * akq;
                    a[k * n + q] = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = a[p * n + k], aqk = a[q * n + k];
                    a[p * n + k] = c * apk - s * aqk;
                    a[q * n + k] = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = v[k * n + p], vkq = v[k * n + q];
                    v[k * n + p] = c * vkp - s * vkq;
                    v[k * n + q] = s * vkp + c * vkq;
                }
            }
    }
    for (int i = 0; i < n; ++i)
        w[i] = a[i * n + i];
}

// K·φ = λ·M·φ for symmetric K and positive definite M (n ≤ kMaxN): the
// eigenvalues ascending in lam, their vectors (M-orthonormal) in the
// columns of phi.
constexpr int kMaxN = 2 * kElems;
inline void general(const double *K, const double *M, int n, double *lam, double *phi) {
    double L[kMaxN * kMaxN] = {}, X[kMaxN * kMaxN], A[kMaxN * kMaxN], V[kMaxN * kMaxN], w[kMaxN];
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double s = M[i * n + j];
            for (int k = 0; k < j; ++k)
                s -= L[i * n + k] * L[j * n + k];
            L[i * n + j] = i == j ? std::sqrt(std::max(s, 1e-300)) : s / L[j * n + j];
        }
    // X = L⁻¹·K, then A = L⁻¹·Xᵀ (= L⁻¹·K·L⁻ᵀ, K symmetric).
    auto lower = [&](const double *B, double *Y, bool transposed) {
        for (int c = 0; c < n; ++c)
            for (int i = 0; i < n; ++i) {
                double s = transposed ? B[c * n + i] : B[i * n + c];
                for (int k = 0; k < i; ++k)
                    s -= L[i * n + k] * Y[k * n + c];
                Y[i * n + c] = s / L[i * n + i];
            }
    };
    lower(K, X, false);
    lower(X, A, true);
    jacobi(A, V, w, n);
    int order[kMaxN];
    for (int i = 0; i < n; ++i)
        order[i] = i;
    std::sort(order, order + n, [&](int a, int b) { return w[a] < w[b]; });
    for (int m = 0; m < n; ++m) {
        const int c = order[m];
        lam[m] = w[c];
        // φ = L⁻ᵀ·y
        for (int i = n - 1; i >= 0; --i) {
            double s = V[i * n + c];
            for (int k = i + 1; k < n; ++k)
                s -= L[k * n + i] * phi[k * n + m];
            phi[i * n + m] = s / L[i * n + i];
        }
    }
}

// One tongue's modes with this wood, as built (before the maker's trim).
struct Modes {
    float hz[kB + kT] = {};       // bending (Timoshenko's), then twisting
    float eulerHz[kB] = {};       // bending without shear (Euler–Bernoulli), for comparison
    float mass[kB + kT] = {};     // kg per unit tip displacement; kg·m² per unit tip rotation
    float shear[kB] = {};         // the share of a bending mode's energy in shear
    float land[kB + kT] = {};     // the share of its strain energy in the land
    // Nodal shapes along the beam from the wall (land then tongue): bending
    // displacement and slope, twisting rotation; per unit at the tip.
    float w[kB][kNodes] = {}, slope[kB][kNodes] = {}, th[kT][kNodes] = {};
    float landLen = 0, length = 0;

    // A bending mode's displacement s metres from the wall (Hermite).
    float bendAt(int k, float s) const {
        const float le = elemLength(s);
        int e = elemOf(s);
        const float x0 = nodeAt(e), t = std::clamp((s - x0) / le, 0.0f, 1.0f);
        const float h1 = 1 - 3 * t * t + 2 * t * t * t, h2 = le * (t - 2 * t * t + t * t * t);
        const float h3 = 3 * t * t - 2 * t * t * t, h4 = le * (t * t * t - t * t);
        return h1 * w[k][e] + h2 * slope[k][e] + h3 * w[k][e + 1] + h4 * slope[k][e + 1];
    }
    float twistAt(int k, float s) const {
        const int e = elemOf(s);
        const float x0 = nodeAt(e), t = std::clamp((s - x0) / elemLength(s), 0.0f, 1.0f);
        return th[k][e] + t * (th[k][e + 1] - th[k][e]);
    }
    float nodeAt(int i) const {
        return i <= kLand ? landLen * float(i) / kLand : landLen + length * float(i - kLand) / kAlong;
    }
    int elemOf(float s) const {
        if (s < landLen)
            return std::clamp(int(s / landLen * kLand), 0, kLand - 1);
        return std::clamp(kLand + int((s - landLen) / length * kAlong), kLand, kElems - 1);
    }
    float elemLength(float s) const { return s < landLen ? landLen / kLand : length / kAlong; }
};

// Saint-Venant's torsion constant of a b×h strip (h < b), as β·b·h³.
inline double torsionConstant(double b, double h) {
    const double r = h / b;
    return b * h * h * h * (1.0 / 3.0 - 0.21 * r * (1.0 - r * r * r * r / 12.0));
}

inline Modes solve(float length, float land) {
    Modes md;
    md.length = length;
    md.landLen = std::max(land, 0.0001f);
    const double rho = Physics::kWood, E = Physics::kEL, G = Physics::kG, h = Physics::kTop;
    const double bT = Physics::kWidth, bL = Physics::kWidth * Physics::kLandWidth;
    // Bending: 2 DOFs a node (w, θ), the wall's node clamped. Timoshenko's
    // elements (shear-deformable, with rotary inertia; Przemieniecki's
    // interdependent interpolation, free of locking): a thick beam's shear
    // lowers each mode by its wavenumber against the thickness, far more in
    // wood (E/G ≈ 12) than in metal. Solved once more without shear, as
    // Euler and Bernoulli's, for the share of each mode's stiffness that
    // shear takes (and so damps as shear).
    {
        const int n = 2 * kElems;
        double Ke[kElems][16];
        double lam[kMaxN], phi[kMaxN * kMaxN], lamEB[kMaxN], phiEB[kMaxN * kMaxN];
        auto beam = [&](bool timoshenko, double *lamOut, double *phiOut) {
            double K[kMaxN * kMaxN] = {}, M[kMaxN * kMaxN] = {};
            for (int e = 0; e < kElems; ++e) {
                const bool inLand = e < kLand;
                const double le = inLand ? md.landLen / kLand : double(length) / kAlong;
                const double b = inLand ? bL : bT;
                const double EI = E * b * h * h * h / 12.0, rA = rho * b * h, rI = rho * b * h * h * h / 12.0;
                const double ps = timoshenko ? 12.0 * EI / (5.0 / 6.0 * G * b * h * le * le) : 0.0;
                const double k[16] = {12, 6 * le, -12, 6 * le,
                                      6 * le, (4 + ps) * le * le, -6 * le, (2 - ps) * le * le,
                                      -12, -6 * le, 12, -6 * le,
                                      6 * le, (2 - ps) * le * le, -6 * le, (4 + ps) * le * le};
                const double m[16] = {156, 22 * le, 54, -13 * le, 22 * le, 4 * le * le, 13 * le, -3 * le * le,
                                      54, 13 * le, 156, -22 * le, -13 * le, -3 * le * le, -22 * le, 4 * le * le};
                const double r[16] = {36, 3 * le, -36, 3 * le, 3 * le, 4 * le * le, -3 * le, -le * le,
                                      -36, -3 * le, 36, -3 * le, 3 * le, -le * le, -3 * le, 4 * le * le};
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j) {
                        const double kij = EI / (le * le * le * (1 + ps)) * k[i * 4 + j];
                        if (timoshenko)
                            Ke[e][i * 4 + j] = kij;
                        const int gi = 2 * e + i - 2, gj = 2 * e + j - 2; // less the clamped node's two
                        if (gi < 0 || gj < 0)
                            continue;
                        K[gi * n + gj] += kij;
                        M[gi * n + gj] += rA * le / 420.0 * m[i * 4 + j] + (timoshenko ? rI / (30.0 * le) * r[i * 4 + j] : 0.0);
                    }
            }
            general(K, M, n, lamOut, phiOut);
        };
        beam(true, lam, phi);
        beam(false, lamEB, phiEB);
        for (int m = 0; m < kB; ++m) {
            // Per unit tip displacement.
            const double tip = phi[(n - 2) * n + m];
            const double sc = std::fabs(tip) > 1e-12 ? 1.0 / tip : 1.0;
            md.w[m][0] = md.slope[m][0] = 0;
            for (int i = 1; i < kNodes; ++i) {
                md.w[m][i] = float(phi[(2 * i - 2) * n + m] * sc);
                md.slope[m][i] = float(phi[(2 * i - 1) * n + m] * sc);
            }
            md.mass[m] = float(sc * sc); // φᵀMφ = 1 before scaling
            // The share of the strain energy in the land.
            double inLand = 0, all = 0;
            for (int e = 0; e < kElems; ++e) {
                const double d[4] = {md.w[m][e], md.slope[m][e], md.w[m][e + 1], md.slope[m][e + 1]};
                double u = 0;
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        u += d[i] * Ke[e][i * 4 + j] * d[j];
                all += u;
                if (e < kLand)
                    inLand += u;
            }
            md.land[m] = float(inLand / std::max(all, 1e-300));
            md.hz[m] = float(std::sqrt(std::max(lam[m], 0.0)) / (2 * kPiD));
            md.eulerHz[m] = float(std::sqrt(std::max(lamEB[m], 0.0)) / (2 * kPiD));
            md.shear[m] = float(std::clamp(1.0 - lam[m] / std::max(lamEB[m], 1e-30), 0.0, 1.0));
        }
    }
    // Twisting: one DOF a node (θ).
    {
        const int n = kElems;
        double K[kElems * kElems] = {}, M[kElems * kElems] = {};
        for (int e = 0; e < kElems; ++e) {
            const bool inLand = e < kLand;
            const double le = inLand ? md.landLen / kLand : double(length) / kAlong;
            const double b = inLand ? bL : bT;
            const double GJ = G * torsionConstant(b, h);
            const double rI = rho * bT * h * (bT * bT + h * h) / 12.0;
            const double k[4] = {1, -1, -1, 1}, m[4] = {2, 1, 1, 2};
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j) {
                    const int gi = e + i - 1, gj = e + j - 1;
                    if (gi < 0 || gj < 0)
                        continue;
                    K[gi * n + gj] += GJ / le * k[i * 2 + j];
                    M[gi * n + gj] += rI * le / 6.0 * m[i * 2 + j];
                }
        }
        double lam[kElems], phi[kElems * kElems];
        general(K, M, n, lam, phi);
        for (int m = 0; m < kT; ++m) {
            const double tip = phi[(n - 1) * n + m];
            const double sc = std::fabs(tip) > 1e-12 ? 1.0 / tip : 1.0;
            md.th[m][0] = 0;
            for (int i = 1; i < kNodes; ++i)
                md.th[m][i] = float(phi[(i - 1) * n + m] * sc);
            md.mass[kB + m] = float(sc * sc);
            md.hz[kB + m] = float(std::sqrt(std::max(lam[m], 0.0)) / (2 * kPiD));
            double inLand = 0, all = 0;
            for (int e = 0; e < kElems; ++e) {
                const double le = e < kLand ? md.landLen / kLand : double(length) / kAlong;
                const double b = e < kLand ? bL : bT;
                const double d = md.th[m][e + 1] - md.th[m][e];
                const double u = G * torsionConstant(b, h) / le * d * d;
                all += u;
                if (e < kLand)
                    inLand += u;
            }
            md.land[kB + m] = float(inLand / std::max(all, 1e-300));
        }
    }
    return md;
}

} // namespace fe

// --- the shape of it ------------------------------------------------------------------------------------
//
// The top seen from above, in metres from its middle: x to the right, y
// away from the player. Four H's, one above the other; in each, a tongue
// from the left end pointing right and one from the right pointing left.
// The lowest note is nearest the player on the left, the scale runs up the
// left-hand tongues, then from the far right-hand one back down to the
// highest, nearest the player on the right.

struct Tongue {
    float rootX = 0;   // where it leaves the top
    float y = 0;       // its centre line
    float length = 0;  // root to tip
    float land = 0;    // from the end wall to the root
    int dir = 1;       // +1 points right, −1 left
    float tipX() const { return rootX + float(dir) * length; }
};

inline float nominalHz(int n) {
    static const int semis[kTongues] = {0, 2, 4, 5, 7, 9, 11, 12};
    return Physics::kRootHz * std::exp2(float(semis[std::clamp(n, 0, kTongues - 1)]) / 12.0f);
}

inline float cutY(int cut) {
    const float pitch = Physics::innerDepth() / float(kCuts);
    return -0.5f * Physics::innerDepth() + pitch * (float(cut) + 0.5f);
}

// The cuts as a maker lays them out: each H pairs a low tongue with a high
// one, so the four H's are near enough one length (the scale runs up the
// left-hand tongues from the player and back down the right-hand ones);
// every root the same short land from its end wall; each tongue cut to the
// length that, with its land, sounds its note, found by the finite
// elements a few rounds over (a cantilever's frequency goes as its length
// to the −2). The box is as long as the longest H needs; a shorter H's
// crossbar is a wider slot. Once.
struct Built {
    Tongue t[kTongues];
    fe::Modes m[kTongues];
    float inner = 0.3f; // the box's inside length
};

// The tongue (by note) on the left of cut c, and on its right.
inline int leftOf(int cut) { return cut; }
inline int rightOf(int cut) { return kTongues - 1 - cut; }

inline const Built &built() {
    static const Built b = [] {
        Built out;
        float len[kTongues];
        for (int n = 0; n < kTongues; ++n) {
            len[n] = Physics::kLowest * std::sqrt(nominalHz(0) / nominalHz(n));
            for (int round = 0; round < 6; ++round) {
                out.m[n] = fe::solve(len[n], Physics::kLand);
                len[n] *= std::sqrt(out.m[n].hz[0] / nominalHz(n));
            }
            out.m[n] = fe::solve(len[n], Physics::kLand);
        }
        float span = 0;
        for (int c = 0; c < kCuts; ++c)
            span = std::max(span, len[leftOf(c)] + Physics::kGap + len[rightOf(c)]);
        out.inner = span + 2 * Physics::kLand;
        for (int c = 0; c < kCuts; ++c)
            for (int side = 0; side < 2; ++side) {
                const int n = side ? rightOf(c) : leftOf(c);
                Tongue &t = out.t[n];
                t.y = cutY(c);
                t.land = Physics::kLand;
                t.length = len[n];
                t.dir = side ? -1 : 1;
                t.rootX = (side ? 0.5f : -0.5f) * out.inner + float(t.dir) * Physics::kLand;
            }
        return out;
    }();
    return b;
}

// The box's inside length, its outside, and the air it holds.
inline float innerLength() { return built().inner; }
inline float boxLength() { return built().inner + 2 * Physics::kWall; }
inline float volume() { return innerLength() * Physics::innerDepth() * Physics::innerHeight(); }

inline Tongue tongue(int n) { return built().t[std::clamp(n, 0, kTongues - 1)]; }
inline const fe::Modes &modesOf(int n) { return built().m[std::clamp(n, 0, kTongues - 1)]; }

// Where on the top a point is: the tongue under it (xi from its root, 0, to
// its tip, 1; eta across, ±1 at its edges), or the box (kFrame).
struct Spot {
    int tongue = kFrame;
    float xi = 0, eta = 0;
};

inline Spot spotAt(float x, float y) {
    Spot sp;
    for (int n = 0; n < kTongues; ++n) {
        const Tongue t = tongue(n);
        // On it, or on the kerf beside it (or a few millimetres past, a
        // fingertip's width), between the root and the middle of the
        // crossbar, is on the tongue; the strips of top between the H's are
        // the box.
        if (std::fabs(y - t.y) > 0.5f * Physics::kWidth + Physics::kKerf + 0.004f)
            continue;
        const float along = (x - t.rootX) * float(t.dir);
        const float gap = innerLength() - 2 * Physics::kLand - t.length - tongue(kTongues - 1 - n).length;
        if (along < 0 || along > t.length + 0.5f * gap)
            continue;
        sp.tongue = n;
        sp.xi = std::min(along / t.length, 1.0f);
        sp.eta = std::clamp((y - t.y) / (0.5f * Physics::kWidth), -1.0f, 1.0f);
        return sp;
    }
    return sp;
}

// --- the box's panels and air -----------------------------------------------------------------------------
//
// Coordinates inside the box: x and y as the top's, z up from the floor
// (0) to the underside of the top.

struct Panel {
    float cx, cy, cz;   // centre
    float ux, uy, uz;   // its first axis (its grain)
    float vx, vy, vz;   // its second
    float a, b;         // its size along each
    float thick;        // m
    int m, n;           // the mode: half-waves along each axis (a strip's: its m-th)
    bool strip;         // a strip of the top between two H's: a beam clamped at both ends
    float scatter;      // this board's own departure from the book's numbers
};

// The top between the cuts: three strips of wood running from end wall to
// end wall between the H's, three modes each (the strips beside the front
// and back walls are glued down along them and hardly move); then the
// bottom, the front and back walls, the ends. Every board its own piece of
// wood, a few percent off the next.
constexpr int kStrips = kCuts - 1, kStripModes = 3;
constexpr int kPanels = kStrips * kStripModes + 7;

inline float stripY(int strip) { return 0.5f * (cutY(strip) + cutY(strip + 1)); }
inline float stripWidth() {
    return Physics::innerDepth() / float(kCuts) - Physics::kWidth - 2 * Physics::kKerf;
}

inline Panel panel(int i) {
    const float Lx = innerLength(), Ly = Physics::innerDepth(), Lz = Physics::innerHeight();
    const float t = Physics::kWall;
    static const float scatter[kPanels] = {1.0f,  1.0f,   1.0f,  0.97f, 0.98f, 1.01f, 1.03f, 1.02f,
                                           0.99f, 1.0f,   1.0f,  1.0f,  0.98f, 1.03f, 1.0f,  1.04f};
    if (i < kStrips * kStripModes) {
        const int strip = i / kStripModes;
        return {0, stripY(strip), Lz, 1, 0, 0, 0, 1, 0, Lx, stripWidth(), Physics::kTop,
                i % kStripModes + 1, 1, true, scatter[i]};
    }
    const float sc = scatter[i];
    switch (i - kStrips * kStripModes) {
    // The bottom.
    case 0: return {0, 0, 0, 1, 0, 0, 0, 1, 0, Lx, Ly, t, 1, 1, false, sc};
    case 1: return {0, 0, 0, 1, 0, 0, 0, 1, 0, Lx, Ly, t, 2, 1, false, sc};
    case 2: return {0, 0, 0, 1, 0, 0, 0, 1, 0, Lx, Ly, t, 1, 2, false, sc};
    // The front (the player's side) and the back.
    case 3: return {0, -0.5f * Ly, 0.5f * Lz, 1, 0, 0, 0, 0, 1, Lx, Lz, t, 1, 1, false, sc};
    case 4: return {0, 0.5f * Ly, 0.5f * Lz, 1, 0, 0, 0, 0, 1, Lx, Lz, t, 1, 1, false, sc};
    // The ends.
    case 5: return {-0.5f * Lx, 0, 0.5f * Lz, 0, 1, 0, 0, 0, 1, Ly, Lz, t, 1, 1, false, sc};
    default: return {0.5f * Lx, 0, 0.5f * Lz, 0, 1, 0, 0, 0, 1, Ly, Lz, t, 1, 1, false, sc};
    }
}

// A beam clamped at both ends, its m-th mode at s (0..1 along it).
inline float clampedBeam(int m, float s) {
    static const float bl[3] = {4.73004f, 7.85320f, 10.99561f};
    static const float sigma[3] = {0.982502f, 1.000777f, 0.999966f};
    const int k = std::clamp(m, 1, 3) - 1;
    const float x = bl[k] * std::clamp(s, 0.0f, 1.0f);
    return std::cosh(x) - std::cos(x) - sigma[k] * (std::sinh(x) - std::sin(x));
}

// A panel's mode shape at (s, t), 0..1 along each of its axes.
inline float panelPhi(const Panel &p, float s, float t) {
    if (s <= 0 || s >= 1 || t <= 0 || t >= 1)
        return 0;
    if (p.strip)
        return clampedBeam(p.m, s);
    return std::sin(float(p.m) * kPi * s) * std::sin(float(p.n) * kPi * t);
}

// A panel's frequency: a strip as a clamped–clamped beam (with Timoshenko's
// correction for its shear), the rest glued at their edges as Warburton's
// clamped orthotropic plate; each scaled by its own board's scatter.
inline float panelHz(const Panel &p) {
    const float h3 = p.thick * p.thick * p.thick;
    if (p.strip) {
        static const float bl[3] = {4.73004f, 7.85320f, 10.99561f};
        const float b = bl[std::clamp(p.m, 1, 3) - 1] / p.a;
        const float f = b * b / kTwoPi * std::sqrt(Physics::kEL * p.thick * p.thick / (12.0f * Physics::kWood));
        const float bh2 = b * p.thick * b * p.thick / 12.0f;
        return p.scatter * f / std::sqrt(1.0f + bh2 * (1.0f + Physics::kEL / (5.0f / 6.0f * Physics::kG)));
    }
    auto G = [](int m) { return m == 1 ? 1.506f : float(m) + 0.5f; };
    auto H = [&](int m) { return m == 1 ? 1.248f : G(m) * G(m) * (1.0f - 2.0f / (G(m) * kPi)); };
    const float Dx = Physics::kEL * h3 / 12.0f, Dy = Physics::kER * h3 / 12.0f;
    // Warburton's H = ν·D_y + 2·D_k, with D_k = G·h³/12 (ν across the grain
    // a few hundredths).
    const float Dk = Physics::kG * h3 / 12.0f, Hxy = 0.03f * std::sqrt(Dx * Dy) + 2.0f * Dk;
    const float w2 = std::pow(kPi, 4.0f) / (Physics::kWood * p.thick) *
                     (Dx * std::pow(G(p.m) / p.a, 4.0f) + Dy * std::pow(G(p.n) / p.b, 4.0f) +
                      2.0f * Hxy * H(p.m) * H(p.n) / (p.a * p.a * p.b * p.b));
    return p.scatter * std::sqrt(w2) / kTwoPi;
}

// How far a bending wave at hz reaches across the grain of the top before
// it dies away (a plate's evanescent wave, (D_y/ρhω²)^¼): how much of a
// knock between two parts reaches each.
inline float acrossGrain(float hz) {
    const float Dy = Physics::kER * std::pow(Physics::kTop, 3.0f) / 12.0f;
    return std::pow(Dy / (Physics::kWood * Physics::kTop * std::pow(kTwoPi * hz, 2.0f)), 0.25f);
}

// The box's standing waves below about 1.3 kHz (and the uniform
// compression, mode 0): half-waves along x, y and z.
struct AirMode {
    int l, m, n;
};
constexpr int kAir = 10;
inline AirMode airMode(int i) {
    static const AirMode modes[kAir] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 0, 0},
                                        {2, 1, 0}, {0, 0, 1}, {1, 0, 1}, {3, 0, 0}, {0, 2, 0}};
    return modes[std::clamp(i, 0, kAir - 1)];
}
inline float airHz(int i) {
    const AirMode a = airMode(i);
    const float fx = float(a.l) / innerLength(), fy = float(a.m) / Physics::innerDepth(),
                fz = float(a.n) / Physics::innerHeight();
    return 0.5f * Physics::kC * std::sqrt(fx * fx + fy * fy + fz * fz);
}
// A mode's pressure shape at a point inside.
inline float airShape(int i, float x, float y, float z) {
    const AirMode a = airMode(i);
    return std::cos(float(a.l) * kPi * (x / innerLength() + 0.5f)) *
           std::cos(float(a.m) * kPi * (y / Physics::innerDepth() + 0.5f)) *
           std::cos(float(a.n) * kPi * z / Physics::innerHeight());
}
// ∫ψ² over the volume, per volume.
inline float airNorm(int i) {
    const AirMode a = airMode(i);
    return (a.l ? 0.5f : 1.0f) * (a.m ? 0.5f : 1.0f) * (a.n ? 0.5f : 1.0f);
}

// --- the body ---------------------------------------------------------------------------------------------
//
// Every part an oscillator, laid out so the sums run over contiguous
// stretches: the tongues' bending modes (four a tongue), the panels, the
// air in the cuts, the box's standing waves, then the tongues' twisting
// modes. The first three move the box's air.

class Body {
public:
    static constexpr int kB = fe::kB, kT = fe::kT, kPer = kB + kT;
    static constexpr int kBend0 = 0;
    static constexpr int kPanel0 = kBend0 + kTongues * kB;
    static constexpr int kPort0 = kPanel0 + kPanels;
    static constexpr int kCoupled = kPort0 + kCuts; // what moves the box's air
    static constexpr int kWave0 = kCoupled;         // the standing waves' own air (mode 1 on)
    static constexpr int kTwist0 = kWave0 + kAir - 1;
    static constexpr int kMax = kTwist0 + kTongues * kT;
    static constexpr int kContacts = 24;
    static constexpr int kDelay = 64;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        build();
        silence();
    }

    // The tongues' first modes, in Hz. Retuning moves the modes under
    // whatever is ringing, as a different drum would.
    void tune(const float *hz) {
        bool same = m_tuned;
        for (int n = 0; n < kTongues; ++n)
            same = same && std::fabs(hz[n] - m_f0[n]) <= m_f0[n] * 1e-5f;
        if (same)
            return;
        for (int n = 0; n < kTongues; ++n)
            m_f0[n] = std::clamp(hz[n], 40.0f, 3000.0f);
        m_tuned = true;
        retune();
    }

    // Longer (or shorter) rings by a power of two, as a livelier or deader
    // piece of wood: one tongue's, or every tongue's. Only that tongue's own
    // oscillators change, so it is cheap enough for every strike.
    void setDecay(int n, float powerOfTwo) {
        n = std::clamp(n, 0, kTongues - 1);
        const float s = std::exp2(std::clamp(powerOfTwo, -3.0f, 3.0f));
        if (std::fabs(s - m_decayN[n]) < 1e-4f)
            return;
        m_decayN[n] = s;
        for (int j = 0; j < kPer; ++j)
            oscillator(index(n, j));
    }
    void setDecay(float powerOfTwo) {
        for (int n = 0; n < kTongues; ++n)
            setDecay(n, powerOfTwo);
    }

    // A mallet strikes tongue n at xi (0 its root … 1 its tip) and eta (±1
    // its edges), or the box (kFrame) at xi, eta across the top (±1 its
    // walls), at velocity vel 0..1. tone −1 a fingertip, −0.5 a yarn mallet,
    // 0 a rubber ball, 1 a hard wooden one. delay: samples into the next
    // block.
    void strike(int n, float vel, float xi, float eta, float tone, int delay) {
        Contact *c = freeContact();
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            retune();
        }
        vel = std::clamp(vel, 0.02f, 1.0f);
        tone = std::clamp(tone, -1.0f, 1.0f);
        *c = Contact{};
        c->on = true;
        c->delay = std::max(0, delay);
        mallet(*c, vel, tone);
        if (n == kFrame) {
            knock(*c, std::clamp(xi, -1.0f, 1.0f) * 0.5f * innerLength(),
                  std::clamp(eta, -1.0f, 1.0f) * 0.5f * Physics::innerDepth());
        } else {
            n = std::clamp(n, 0, kTongues - 1);
            const Tongue t = tongue(n);
            const fe::Modes &md = modesOf(n);
            xi = std::clamp(xi, 0.0f, 1.0f);
            const float y = std::clamp(eta, -1.0f, 1.0f) * 0.5f * Physics::kWidth;
            // The patch the ball presses is a few millimetres across: the
            // shapes averaged over it.
            const float s0 = md.landLen + xi * t.length;
            const float half = std::min(c->patch, 0.25f * t.length);
            const float at[3] = {std::max(md.landLen, s0 - half), s0, std::min(md.landLen + t.length, s0 + half)};
            for (int k = 0; k < kPer; ++k) {
                float phi = 0;
                for (float s : at)
                    phi += (k < kB ? md.bendAt(k, s) : y * md.twistAt(k - kB, s)) / 3.0f;
                add(*c, index(n, k), phi);
            }
            const float x = t.rootX + float(t.dir) * xi * t.length;
            c->pan = std::clamp(x / (0.5f * boxLength()), -0.8f, 0.8f);
        }
        m_sleeping = false;
        m_touching = true;
        m_quiet = 0;
    }

    // Adds the instrument's sound into l and r (n ≤ the block).
    void render(float *l, float *r, int n) {
        if (m_sleeping)
            return;
        constexpr int C = kCoupled;
        const int N = m_n;
        float *x = m_x, *u = m_u, *f = m_force;
        for (int i = 0; i < n; ++i) {
            // The box's air: the uniform compression and each standing wave,
            // from everything that moves a wall (and each wave's own air).
            float fc[C] = {};
            for (int a = 0; a < kAir; ++a) {
                const float *row = m_couple[a];
                float s = a ? x[kWave0 + a - 1] : 0.0f;
                for (int j = 0; j < C; ++j)
                    s += row[j] * x[j];
                const float p = -m_kAirRun[a] * s; // the cavity grows, its pressure falls
                if (a)
                    f[kWave0 + a - 1] = p;
                for (int j = 0; j < C; ++j)
                    fc[j] += row[j] * p;
            }
            for (int j = 0; j < C; ++j)
                f[j] = fc[j];
            for (int k = kTwist0; k < N; ++k)
                f[k] = 0;
            // The lands behind the roots and the strips of top beside them
            // are one plate: each tongue pulls on its neighbours across the
            // grain.
            {
                constexpr int NB = kTongues * kB, NS = kStrips * kStripModes;
                const float *xs = x + kPanel0;
                float fs[NS] = {};
                for (int k = 0; k < NB; ++k) {
                    const float *row = m_join[k];
                    float sum = 0;
                    for (int j = 0; j < NS; ++j) {
                        sum += row[j] * xs[j];
                        fs[j] += row[j] * x[k];
                    }
                    f[k] -= sum;
                }
                for (int j = 0; j < NS; ++j)
                    f[kPanel0 + j] -= fs[j];
            }
            // The mallets still touching, and the force they make.
            if (m_touching)
                contacts(f);
            for (int k = 0; k < N; ++k) {
                u[k] = m_d[k] * u[k] - m_c[k] * x[k] + m_inv[k] * f[k];
                x[k] += u[k];
            }
            // Each tongue's face reaches each microphone after its own time
            // of flight; the rest is heard from the middle.
            float ql = 0, qr = 0;
            for (int k = kPanel0; k < N; ++k) {
                ql += m_gl[k] * u[k];
                qr += m_gr[k] * u[k];
            }
            m_wr = (m_wr + 1) & (kDelay - 1);
            for (int t = 0; t < kTongues; ++t) {
                const float *gl = m_gl + kB * t, *gr = m_gr + kB * t, *ut = u + kB * t;
                m_lineL[t][m_wr] = gl[0] * ut[0] + gl[1] * ut[1] + gl[2] * ut[2] + gl[3] * ut[3];
                m_lineR[t][m_wr] = gr[0] * ut[0] + gr[1] * ut[1] + gr[2] * ut[2] + gr[3] * ut[3];
                ql += tap(m_lineL[t], m_delayL[t]);
                qr += tap(m_lineR[t], m_delayR[t]);
            }
            // Pressure follows the volume acceleration.
            l[i] += (ql - m_ql) * m_gain;
            r[i] += (qr - m_qr) * m_gain;
            m_ql = ql;
            m_qr = qr;
        }
        // Asleep once it has rung out (and no mallet is on it).
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

    // A hand laid flat on it: everything dies within about `seconds`.
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
        for (int g = 0; g < kTongues; ++g)
            for (int i = 0; i < kDelay; ++i)
                m_lineL[g][i] = m_lineR[g][i] = 0;
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
    // Where tongue n's mode k (0–3 bending, 4–6 twisting) is.
    static int index(int n, int k) {
        n = std::clamp(n, 0, kTongues - 1);
        k = std::clamp(k, 0, kPer - 1);
        return k < kB ? kBend0 + kB * n + k : kTwist0 + kT * n + (k - kB);
    }
    // A tongue's mode as tuned, in Hz, and its decay time constant, s.
    float modeHz(int n, int k) const { return m_hz[index(n, k)]; }
    float modeTau(int n, int k) const { return m_tau[index(n, k)]; }
    float panelHz(int j) const { return m_hz[kPanel0 + std::clamp(j, 0, kPanels - 1)]; }
    // The box as a Helmholtz resonator: its volume on the plugs of air in
    // all four cuts, rigid walls.
    float helmholtzHz() const {
        float s = 0;
        for (int p = 0; p < kCuts; ++p)
            s += m_area[kPort0 + p] * m_area[kPort0 + p] / m_mass[kPort0 + p];
        return std::sqrt(m_kAir[0] * s) / kTwoPi;
    }
    float helmholtzQ() const { return m_portQ; }
    float portArea() const {
        float s = 0;
        for (int p = 0; p < kCuts; ++p)
            s += m_area[kPort0 + p];
        return s;
    }
    // How far a tongue's mode swings now (m, or rad for twisting).
    float swing(int n, int k) const { return swingOf(index(n, k)); }
    // How long the last strike's mallet touched, in seconds.
    float lastContact() const { return m_lastContact; }
    // The normalised coupling of a tongue's first mode to the box's
    // Helmholtz air: g/(ω₁ω₂), the share of their frequencies the two would
    // split by if tuned together.
    float airCoupling(int n) const {
        const int k = index(n, 0);
        float S = 0, M = 0;
        for (int p = 0; p < kCuts; ++p) {
            S += m_area[kPort0 + p];
            M += m_mass[kPort0 + p];
        }
        const float g = m_kAir[0] * m_area[k] * S / std::sqrt(m_mass[k] * M);
        return g / (kTwoPi * m_hz[k] * kTwoPi * helmholtzHz());
    }
    // How strongly a tongue's first mode pulls on the strips of top beside
    // its land: the largest K/√(K₁K₂) over the strips' modes.
    float rootCoupling(int n) const {
        const int k = index(n, 0);
        float top = 0;
        for (int j = 0; j < kStrips * kStripModes; ++j)
            top = std::max(top, std::fabs(m_join[k - kBend0][j]) / std::sqrt(stiffnessOf(k) * stiffnessOf(kPanel0 + j)));
        return top;
    }
    // The maker's trim: how much a tongue's stiffness is scaled from the
    // wood as cut to land on the note the key asks of it.
    float trim(int n) const {
        const float r = m_f0[std::clamp(n, 0, kTongues - 1)] / modesOf(n).hz[0];
        return r * r;
    }
    // For tests: turns the box's air off (an open frame) or on.
    void setAir(bool on) {
        m_airOn = on;
        retune();
    }

private:
    struct Contact {
        bool on = false;
        int delay = 0;
        int age = 0, touched = 0;
        float z = 0, v = 0, prev = 0; // the ball's travel past the resting surface, inward; its speed; the last squeeze
        float mass = 0.013f, k = 1.2e6f, loss = 0.1f, patch = 0.003f;
        // The hand behind it: where it is and how fast it goes, and the grip
        // (a soft spring and a damper) that holds the stick or the finger.
        float hand = 0, handV = 0, grip = 100.0f, gripLoss = 1.0f;
        int off = 0; // samples the ball has been off the wood since it last touched
        float bone = 0, boneAt = 1;
        float pan = 0;
        int count = 0;
        int idx[kPer + kPanels] = {};
        float phi[kPer + kPanels] = {};
    };

    // What strikes, by tone: a fingertip, a yarn mallet, a rubber ball, a
    // hard wooden one; and how fast it arrives.
    void mallet(Contact &c, float vel, float tone) {
        // mass at the head (kg), Hertz's K (N/m^1.5), Hunt and Crossley's
        // loss (s/m), the half-width of the patch it presses (m). A
        // superball on a dowel: E ≈ 3–10 MPa, R 12.5 mm, so K ≈ 0.6–2·10⁶,
        // and 12–15 g.
        static const float M[4] = {0.008f, 0.014f, 0.013f, 0.010f};
        static const float K[4] = {3.0e5f, 5.0e5f, 1.2e6f, 6.0e7f};
        static const float L[4] = {0.6f, 0.5f, 0.12f, 0.04f};
        static const float P[4] = {0.006f, 0.006f, 0.004f, 0.002f};
        const float pos = (tone + 1.0f) * 1.5f; // 0..3
        const int i = std::clamp(int(pos), 0, 2);
        const float t = pos - float(i);
        auto geo = [&](const float *v) { return v[i] * std::pow(v[i + 1] / v[i], t); };
        c.mass = geo(M);
        c.k = geo(K);
        c.loss = geo(L);
        c.patch = geo(P);
        if (tone < -0.5f) {
            // A fingertip: slower, held by a stiffer hand, and the bone under
            // the pulp meets the wood on a hard stroke.
            c.v = 0.05f + 1.3f * std::pow(vel, 1.5f);
            c.grip = 400.0f;
            c.bone = 2.0e6f;
            c.boneAt = 0.0012f;
        } else {
            // A mallet thrown from the wrist, up to about 3 m/s, held loosely.
            c.v = 0.08f + 2.9f * std::pow(vel, 1.4f);
            c.grip = 100.0f;
        }
        c.handV = c.v;
        c.gripLoss = std::sqrt(c.grip * c.mass); // half of critical
    }

    // A knock on the box at X, Y on its top: the land behind a root, a strip
    // between the H's, or the edge above a wall; and whatever else is near
    // enough across the grain to feel it.
    void knock(Contact &c, float X, float Y) {
        const float Li = innerLength(), Di = Physics::innerDepth();
        const float fromWall = 0.5f * Li - std::fabs(X);
        for (int j = 0; j < kPanels; ++j) {
            const Panel p = panel(j);
            float phi = 0;
            if (p.strip) {
                const float off = std::max(0.0f, std::fabs(Y - p.cy) - 0.5f * p.b);
                phi = clampedBeam(p.m, X / Li + 0.5f) * std::exp(-off / acrossGrain(m_hz[kPanel0 + j]));
            } else if (j >= kStrips * kStripModes + 3 && j < kStrips * kStripModes + 5) {
                // The front or back wall, glued under the top's edge: a knock
                // over it loads the wall a joint's width (its thickness) down.
                const bool back = j == kStrips * kStripModes + 4;
                const float off = std::max(0.0f, 0.5f * Di - (back ? Y : -Y));
                phi = panelPhi(p, X / Li + 0.5f, 1.0f - Physics::kWall / Physics::innerHeight()) *
                      std::exp(-off / acrossGrain(m_hz[kPanel0 + j]));
            }
            add(c, kPanel0 + j, phi);
        }
        // On the land behind a row's roots: that tongue's own modes there.
        if (fromWall < Physics::kLand)
            for (int n = 0; n < kTongues; ++n) {
                const Tongue t = tongue(n);
                if ((t.dir > 0) != (X < 0))
                    continue;
                const fe::Modes &md = modesOf(n);
                const float off = std::max(0.0f, std::fabs(Y - t.y) - 0.5f * Physics::kWidth * Physics::kLandWidth);
                for (int k = 0; k < kB; ++k)
                    add(c, index(n, k), md.bendAt(k, std::max(fromWall, 0.0f)) * std::exp(-off / acrossGrain(m_hz[index(n, k)])));
            }
        c.pan = std::clamp(X / (0.5f * boxLength()), -0.8f, 0.8f);
    }

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

    void add(Contact &c, int mode, float phi) {
        if (c.count < int(sizeof(c.idx) / sizeof(c.idx[0])) && std::fabs(phi) > 1e-5f) {
            c.idx[c.count] = mode;
            c.phi[c.count] = phi;
            ++c.count;
        }
    }

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

    // The contact forces this sample: each mallet against the wood under it
    // (outward displacement x; the mallet presses inward).
    void contacts(float *f) {
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
            // It arrives where the wood is now, not where it rests.
            if (c.age == 0) {
                c.z = c.hand = surface;
                c.prev = 0;
            }
            ++c.age;
            const float delta = c.z - surface;
            float force = 0;
            if (delta > 0) {
                // Hertz, with Hunt and Crossley's loss: the ball gives back
                // less than it took.
                const float rate = (delta - c.prev) * m_rate;
                force = c.k * delta * std::sqrt(delta) * std::max(0.0f, 1.0f + c.loss * rate);
                if (delta > c.boneAt) {
                    const float b = delta - c.boneAt;
                    force += c.bone * b * std::sqrt(b);
                }
                for (int j = 0; j < c.count; ++j)
                    f[c.idx[j]] -= c.phi[j] * force;
                c.touched = c.age;
            }
            c.prev = std::max(delta, 0.0f);
            // The hand comes on until the ball lands, then lifts; the grip
            // pulls the ball after it, and lets it bounce.
            if (c.touched && c.handV > 0)
                c.handV = -std::max(0.3f * c.handV, std::min(c.handV, 0.1f));
            c.off = delta > 0 ? 0 : c.off + 1;
            c.hand += c.handV * m_dt;
            const float pull = c.grip * (c.hand - c.z) + c.gripLoss * (c.handV - c.v);
            c.v += (pull - force) / c.mass * m_dt;
            c.z += c.v * m_dt;
            // Gone once the ball has been off the wood for 3 ms on its way
            // up (a bounce comes back sooner), or after 40 ms whatever.
            if ((c.touched && c.off > int(0.003f * m_rate) && c.v < 0) || c.age > int(0.04f * m_rate)) {
                c.on = false;
                m_lastContact = float(c.touched) / m_rate;
            }
        }
        m_touching = any;
    }

    // The instrument's parts, once: modes, masses, how each moves the air,
    // and where each is heard from.
    void build() {
        const float Lz = Physics::innerHeight();
        for (int n = 0; n < kTongues; ++n) {
            const fe::Modes &md = modesOf(n);
            for (int j = 0; j < kPer; ++j) {
                const int k = index(n, j);
                m_kind[k] = j < kB ? Bend : Twist;
                m_tongueOf[k] = int8_t(n);
                m_mass[k] = md.mass[j];
            }
        }
        for (int j = 0; j < kPanels; ++j) {
            const Panel p = panel(j);
            const int k = kPanel0 + j;
            m_kind[k] = Wall;
            m_tongueOf[k] = -1;
            // ρh·∫φ² over it.
            constexpr int S = 64;
            float sq = 0;
            for (int i = 0; i < S; ++i)
                for (int e = 0; e < (p.strip ? 1 : S); ++e) {
                    const float v = panelPhi(p, (float(i) + 0.5f) / S, p.strip ? 0.5f : (float(e) + 0.5f) / S);
                    sq += v * v;
                }
            m_mass[k] = Physics::kWood * p.thick * p.a * p.b * sq / float(p.strip ? S : S * S);
        }
        // The air in each cut: a plug as long as the top is thick and its
        // ends' corrections, as wide as the kerf, as long as the cut.
        for (int p = 0; p < kCuts; ++p) {
            const int k = kPort0 + p;
            // The two sides of the H, root to root, and its crossbar: a kerf,
            // or the slot between two tips that don't reach each other.
            const Tongue a = tongue(leftOf(p)), b = tongue(rightOf(p));
            const float gap = std::fabs(b.tipX() - a.tipX());
            m_area[k] = Physics::kKerf * 2.0f * (b.rootX - a.rootX) + gap * (Physics::kWidth + 2 * Physics::kKerf);
            m_kind[k] = Port;
            m_tongueOf[k] = -1;
            m_mass[k] = Physics::kRhoAir * m_area[k] * slitNeck();
        }
        // The box's standing waves (mode 0, the uniform compression, has no
        // air of its own to move: its spring is the whole cavity's).
        for (int a = 0; a < kAir; ++a) {
            m_kAir[a] = Physics::kRhoAir * Physics::kC * Physics::kC / (volume() * airNorm(a));
            if (!a)
                continue;
            const int k = kWave0 + a - 1;
            m_kind[k] = Air;
            m_tongueOf[k] = -1;
            m_mass[k] = m_kAir[a] / std::pow(kTwoPi * airHz(a), 2.0f);
        }
        m_n = kMax;

        // How much of each standing wave everything that moves a wall
        // meets: ∫φ·ψ over its surface (outward).
        for (int n = 0; n < kTongues; ++n) {
            const Tongue t = tongue(n);
            const fe::Modes &md = modesOf(n);
            for (int b = 0; b < kB; ++b) {
                const int j = index(n, b);
                constexpr int S = 48;
                const float total = md.landLen + t.length;
                const float wallX = t.rootX - float(t.dir) * md.landLen;
                float abs = 0;
                for (int a = 0; a < kAir; ++a) {
                    float sum = 0;
                    for (int i = 0; i < S; ++i) {
                        const float s = (float(i) + 0.5f) / S * total;
                        const float width = s < md.landLen ? Physics::kWidth * Physics::kLandWidth : Physics::kWidth;
                        const float x = wallX + float(t.dir) * s;
                        const float w = md.bendAt(b, s) * width * total / S;
                        sum += w * airShape(a, x, t.y, Lz);
                        if (!a)
                            abs += std::fabs(w);
                    }
                    m_couple[a][j] = sum;
                }
                m_area[j] = m_couple[0][j];
                m_lobes[j] = abs;
            }
        }
        for (int p = 0; p < kPanels; ++p) {
            const Panel q = panel(p);
            const int j = kPanel0 + p;
            // Outward: up for the top, down for the bottom, out for the walls.
            const float nx = q.uy * q.vz - q.uz * q.vy, ny = q.uz * q.vx - q.ux * q.vz, nz = q.ux * q.vy - q.uy * q.vx;
            const float sign = (nx * q.cx + ny * q.cy + nz * (q.cz - 0.5f * Lz)) >= 0 ? 1.0f : -1.0f;
            constexpr int S = 24;
            float abs = 0;
            for (int a = 0; a < kAir; ++a) {
                float sum = 0;
                for (int i = 0; i < S; ++i)
                    for (int e = 0; e < S; ++e) {
                        const float s = (float(i) + 0.5f) / S, t = (float(e) + 0.5f) / S;
                        const float px = q.cx + (s - 0.5f) * q.a * q.ux + (t - 0.5f) * q.b * q.vx;
                        const float py = q.cy + (s - 0.5f) * q.a * q.uy + (t - 0.5f) * q.b * q.vy;
                        const float pz = q.cz + (s - 0.5f) * q.a * q.uz + (t - 0.5f) * q.b * q.vz;
                        const float phi = panelPhi(q, s, t);
                        const float dA = q.a * q.b / float(S * S);
                        sum += sign * phi * airShape(a, px, py, pz) * dA;
                        if (!a)
                            abs += std::fabs(phi) * dA;
                    }
                m_couple[a][j] = sum;
            }
            m_area[j] = m_couple[0][j];
            m_lobes[j] = abs;
        }
        for (int p = 0; p < kCuts; ++p) {
            const int j = kPort0 + p;
            // The air in the slit meets each wave where the slit runs, along
            // both sides of the H.
            const Tongue a = tongue(leftOf(p)), b = tongue(rightOf(p));
            const float side = 0.5f * (Physics::kWidth + Physics::kKerf);
            for (int m = 0; m < kAir; ++m) {
                float sum = 0;
                constexpr int S = 24;
                for (int i = 0; i < S; ++i) {
                    const float x = a.rootX + (b.rootX - a.rootX) * (float(i) + 0.5f) / S;
                    sum += 0.5f * (airShape(m, x, a.y - side, Lz) + airShape(m, x, a.y + side, Lz));
                }
                m_couple[m][j] = m_area[j] * sum / S;
            }
        }

        // The land behind each root and the strips of top beside it are one
        // plate: across the grain, the plate between a land's centre line
        // and a strip's is a spring of 12·D_y/d³ per metre of land.
        // Their shapes along the land weigh it for each pair of modes. (A
        // coupling only: each part's own stiffness already has its share.)
        {
            const float Dy = Physics::kER * std::pow(Physics::kTop, 3.0f) / 12.0f;
            for (int n = 0; n < kTongues; ++n) {
                const Tongue t = tongue(n);
                const fe::Modes &md = modesOf(n);
                const float wallX = t.rootX - float(t.dir) * md.landLen;
                for (int j = 0; j < kStrips * kStripModes; ++j) {
                    const Panel q = panel(j);
                    const float d = std::fabs(q.cy - t.y);
                    if (d > Physics::innerDepth() / float(kCuts))
                        continue; // not a neighbour
                    const float kc = 12.0f * Dy / (d * d * d);
                    for (int b = 0; b < kB; ++b) {
                        constexpr int S = 16;
                        float sum = 0;
                        for (int i = 0; i < S; ++i) {
                            const float sl = (float(i) + 0.5f) / S * md.landLen;
                            const float x = wallX + float(t.dir) * sl;
                            sum += md.bendAt(b, sl) * clampedBeam(q.m, x / innerLength() + 0.5f) * md.landLen / S;
                        }
                        const float k = kc * sum;
                        // (½k·(w_land − w_strip)²: pressing the land down
                        // pulls the strip down with it.)
                        m_join[index(n, b) - kBend0][j] = -k;
                    }
                }
            }
        }

        // Two microphones 35 cm apart, 45 cm in front: each tongue's time of
        // flight to each, less the nearest's.
        {
            const float mic[2][2] = {{-0.175f, -0.45f}, {0.175f, -0.45f}};
            for (int e = 0; e < 2; ++e) {
                float d[kTongues], least = 1e9f;
                for (int n = 0; n < kTongues; ++n) {
                    const Tongue t = tongue(n);
                    const float cx = t.rootX + 0.6f * float(t.dir) * t.length;
                    d[n] = std::hypot(mic[e][0] - cx, mic[e][1] - t.y) / Physics::kC * m_rate;
                    least = std::min(least, d[n]);
                }
                for (int n = 0; n < kTongues; ++n)
                    (e == 0 ? m_delayL : m_delayR)[n] = std::clamp(d[n] - least, 0.0f, float(kDelay - 2));
            }
        }
        for (int j = 0; j < kStrips * kStripModes; ++j)
            m_ends[j] = stripEnds(j);
        for (int n = 0; n < kTongues; ++n)
            m_f0[n] = nominalHz(n);
        m_tuned = false;
        retune();
    }

    // The plug of air in a 2 mm slit through the 13 mm top: the top's
    // thickness and an end correction either side of a narrow slit (its
    // width over π times the log of how much longer the wave is), at the
    // box's resonance.
    static float slitNeck() {
        const float w = Physics::kKerf;
        const float k = kTwoPi * 220.0f / Physics::kC;
        const float end = w / kPi * (1.5f - 0.5772f - std::log(k * w / 4.0f));
        return Physics::kTop + 2.0f * end;
    }

    // Frequencies, decays and couplings from the tongues' notes.
    void retune() {
        Rng rng(0x51d7u); // this piece of wood's own scatter
        const float rate = m_rate;
        for (int n = 0; n < kTongues; ++n) {
            const fe::Modes &md = modesOf(n);
            const float scale = m_f0[n] / md.hz[0];
            for (int j = 0; j < kPer; ++j) {
                const int k = index(n, j);
                const float spread = j == 0 ? 1.0f : 1.0f + Knobs::spread * rng.bipolar();
                m_hz[k] = md.hz[j] * scale * spread;
                // Wood's loss along the grain for bending (and in shear, for
                // the share of a bending mode that is shear), in shear for
                // twisting; and the land and the joint behind the root, for
                // the share of the mode's strain that is there.
                const float wood = j < kB ? Physics::kEtaL * (1.0f - md.shear[j]) + Physics::kEtaG * md.shear[j]
                                          : Physics::kEtaG;
                m_eta[k] = wood + Physics::kEtaJoint * md.land[j];
            }
        }
        for (int j = 0; j < kPanels; ++j) {
            m_hz[kPanel0 + j] = slit::panelHz(panel(j));
            m_eta[kPanel0 + j] = Physics::kEtaPanel + (j < kStrips * kStripModes ? Physics::kEtaJoint * m_ends[j] : 0.0f);
        }
        // The air in the slits: their radiation and viscous losses together
        // (a Q of a few: a long slit lets the box breathe out quickly).
        {
            const float fH = helmholtzHz(), w = kTwoPi * fH;
            const float S = portArea(), L = slitNeck(), V = volume();
            const float qRad = kTwoPi * std::sqrt(V * std::pow(L / S, 3.0f));
            const float wall = std::sqrt(w * Physics::kRhoAir * Physics::kMu * 0.5f);
            const float qVisc = w * Physics::kRhoAir * Physics::kKerf * L / (2.0f * Physics::kTop * wall);
            m_portQ = 1.0f / (1.0f / qRad + 1.0f / qVisc);
            for (int p = 0; p < kCuts; ++p) {
                m_hz[kPort0 + p] = fH;
                m_eta[kPort0 + p] = 1.0f / m_portQ;
            }
        }
        for (int a = 1; a < kAir; ++a) {
            m_hz[kWave0 + a - 1] = airHz(a);
            m_eta[kWave0 + a - 1] = 1.0f / Physics::kAirQ;
        }
        // Radiation: a tongue's face pushes on the outside air as a small
        // source, ρω²A²/2πc of resistance, a loss on top of the wood's.
        for (int k = kBend0; k < kPanel0; ++k) {
            const float w = kTwoPi * m_hz[k];
            const float R = Physics::kRhoAir * w * w * m_area[k] * m_area[k] / (kTwoPi * Physics::kC);
            m_eta[k] += R / (w * m_mass[k]);
        }
        for (int k = 0; k < m_n; ++k) {
            const bool own = m_kind[k] == Bend || m_kind[k] == Twist;
            m_tau[k] = 1.0f / (kPi * m_hz[k] * m_eta[k]) * (own ? m_decayN[m_tongueOf[k]] : 1.0f);
        }

        // The oscillators. The box's air stiffens every tongue by what it
        // pushes into each of the cavity's modes, less what the slits, the
        // panels and the other tongues give way by at its frequency (below
        // the box's resonance the slits more than give way). What remains
        // of each mode's own spring is what lands it on its note with the
        // box closed, as a maker tunes it.
        for (int k = 0; k < m_n; ++k) {
            const float w = kTwoPi * std::min(m_hz[k], 0.45f * rate);
            float w2 = w * w;
            if (m_kind[k] == Port || m_kind[k] == Air)
                w2 = 0; // no spring of their own: the cavity's
            else if (m_kind[k] == Bend) {
                if (m_airOn)
                    w2 -= airStiffening(k, w) / m_mass[k];
                // And the strips beside its land pull it, less than the air
                // and the same way: below a strip's mode they give, above it
                // they push back.
                for (int j = 0; j < kStrips * kStripModes; ++j) {
                    const float K = m_join[k - kBend0][j];
                    if (K == 0)
                        continue;
                    const int q = kPanel0 + j;
                    const float wj = kTwoPi * m_hz[q], dr = wj * wj - w * w, di = 2.0f * w / m_tau[q];
                    w2 += K * K * dr / (m_mass[q] * (dr * dr + di * di)) / m_mass[k];
                }
            }
            m_w2[k] = std::max(w2, 1.0f);
            m_inv[k] = m_dt * m_dt / m_mass[k];
            oscillator(k);
        }
        for (int a = 0; a < kAir; ++a)
            m_kAirRun[a] = m_airOn ? m_kAir[a] : 0.0f;

        // What the microphones hear: each part's volume velocity (its net
        // area), and the lobes that only rock the air from side to side, as
        // dipoles, which radiate better the shorter the wave is against them.
        for (int k = 0; k < m_n; ++k) {
            float mono = 0, dip = 0, x = 0, dx = 0;
            const float kw = kTwoPi * m_hz[k] / Physics::kC;
            const int n = m_tongueOf[k];
            if (n >= 0) {
                const Tongue t = tongue(n);
                x = t.rootX + 0.6f * float(t.dir) * t.length;
                dx = float(t.dir);
            }
            switch (m_kind[k]) {
            case Bend: {
                const int b = k - index(n, 0);
                const float lobe = tongue(n).length / float(1 + b);
                mono = m_area[k];
                dip = Knobs::radLobe * (m_lobes[k] - std::fabs(m_area[k])) * std::min(1.0f, kw * lobe);
                break;
            }
            case Twist: {
                const fe::Modes &md = modesOf(n);
                float sum = 0;
                for (int i = 0; i < 16; ++i)
                    sum += std::fabs(md.twistAt(k - index(n, kB), md.landLen + (float(i) + 0.5f) / 16 * md.length));
                const float b = Physics::kWidth;
                dip = Knobs::radTwist * b * b / 8.0f * sum / 16 * md.length * std::min(1.0f, kw * b);
                dx = 0;
                break;
            }
            case Wall: {
                const Panel q = panel(k - kPanel0);
                mono = m_area[k];
                dip = Knobs::radPanel * (m_lobes[k] - std::fabs(mono)) * std::min(1.0f, kw * q.a / float(q.m));
                x = q.cx;
                dx = q.m % 2 ? 0.0f : 1.0f;
                break;
            }
            case Port: mono = m_airOn ? m_area[k] : 0.0f; break;
            default: break;
            }
            const float px = x / (0.5f * boxLength());
            const float ml = std::sqrt(0.5f * (1.0f - 0.6f * px)), mr = std::sqrt(0.5f * (1.0f + 0.6f * px));
            // (A dipole's pressure grows as kd against a monopole's, d the
        // distance between its lobes, until they are half a wave apart.)
        // A dipole along x: each microphone hears the lobe facing it; one
            // across the tongue, both the front lobe.
            m_gl[k] = mono * ml + (dx != 0 ? -0.7f * dip * dx : dip * ml);
            m_gr[k] = mono * mr + (dx != 0 ? 0.7f * dip * dx : dip * mr);
            m_loud[k] = m_kind[k] == Air ? 0.0f : mono * mono + dip * dip;
        }
        // Pressure ∝ ρ/(4πr)·dQ/dt, scaled to the studio's level: u is
        // metres per sample, so two of the rate to m³/s².
        m_gain = Knobs::level * m_rate * m_rate;
        // A full-strength strike on the lowest tongue, for the energy scale;
        // asleep 90 dB below it.
        {
            const int k = index(0, 0);
            const float amp = 3.0e-4f; // m
            const float th = kTwoPi * m_hz[k] / m_rate;
            m_refEnergy = m_loud[k] * (m_c[k] * amp * amp + (amp * th) * (amp * th));
            m_sleepEnergy = m_refEnergy * 1e-9f;
        }
        if (m_muteD < 1.0f)
            for (int k = 0; k < m_n; ++k)
                m_d[k] = std::min(m_d[k], m_muteD);
    }

    // Oscillator k's update from its spring and its decay: exact frequency
    // and decay, poles r·e^{±iθ}. A tongue's decay is its own, scaled.
    void oscillator(int k) {
        const bool own = m_kind[k] == Bend || m_kind[k] == Twist;
        const float tau = own ? 1.0f / (kPi * m_hz[k] * m_eta[k]) * m_decayN[m_tongueOf[k]] : m_tau[k];
        m_tau[k] = tau;
        const float r = std::exp(-1.0f / (tau * m_rate));
        const float th = std::sqrt(m_w2[k]) / m_rate;
        m_d[k] = r * r;
        m_c[k] = m_w2[k] <= 1.0f ? 1.0f + r * r - 2.0f * r : 1.0f + r * r - 2.0f * r * std::cos(th);
        if (m_muteD < 1.0f)
            m_d[k] = std::min(m_d[k], m_muteD);
    }

    // The share of a strip mode's strain energy (∫φ''²) within a land's
    // length of either end wall, where it is glued: what the joint takes.
    static float stripEnds(int j) {
        const Panel p = panel(j);
        constexpr int S = 400;
        const float h = 1.0f / S, edge = Physics::kLand / p.a;
        float all = 0, ends = 0;
        for (int i = 1; i < S; ++i) {
            const float s = float(i) * h;
            const float c = (clampedBeam(p.m, s - h) - 2 * clampedBeam(p.m, s) + clampedBeam(p.m, s + h)) / (h * h);
            all += c * c;
            if (s < edge || s > 1 - edge)
                ends += c * c;
        }
        return ends / all;
    }

    // A mode's own stiffness, m·ω², at its frequency as built.
    float stiffnessOf(int k) const {
        float hz = 0;
        if (m_kind[k] == Wall)
            hz = slit::panelHz(panel(k - kPanel0));
        else if (k < kTwist0)
            hz = modesOf(m_tongueOf[k]).hz[(k - kBend0) % kB];
        else
            hz = modesOf(m_tongueOf[k]).hz[kB + (k - kTwist0) % kT];
        return m_mass[k] * std::pow(kTwoPi * hz, 2.0f);
    }

    // The stiffness the box's air adds to bending mode k at ω: for each of
    // the cavity's modes, ρc²/(VΛ)·A² over one plus that spring times the
    // give of everything else that moves its walls (each a mass on its own
    // spring, damped; the slits and the waves' own air have no spring).
    float airStiffening(int k, float w) const {
        float added = 0;
        for (int a = 0; a < kAir; ++a) {
            const float A = m_couple[a][k];
            if (A == 0)
                continue;
            float re = 0, im = 0;
            auto give = [&](float c, float m, float wj, float tau) {
                // c²/(m(ωj² − ω² + 2iω/τ))
                const float dr = wj * wj - w * w, di = 2.0f * w / tau;
                const float den = m * (dr * dr + di * di);
                re += c * c * dr / den;
                im -= c * c * di / den;
            };
            for (int j = 0; j < kCoupled; ++j)
                if (j != k && m_couple[a][j] != 0) {
                    const float wj = m_kind[j] == Port ? 0.0f : kTwoPi * m_hz[j];
                    give(m_couple[a][j], m_mass[j], wj, m_tau[j]);
                }
            if (a)
                give(1.0f, m_mass[kWave0 + a - 1], 0.0f, m_tau[kWave0 + a - 1]);
            // K·A²/(1 + K·Y), its real part.
            const float K = m_kAir[a];
            const float dr = 1.0f + K * re, di = K * im;
            added += K * A * A * dr / (dr * dr + di * di);
        }
        return added;
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    int m_n = 0;
    alignas(32) float m_x[kMax] = {}, m_u[kMax] = {}, m_force[kMax] = {};
    alignas(32) float m_d[kMax] = {}, m_c[kMax] = {}, m_inv[kMax] = {};
    alignas(32) float m_gl[kMax] = {}, m_gr[kMax] = {}, m_loud[kMax] = {};
    alignas(32) float m_couple[kAir][kCoupled] = {};
    float m_kAir[kAir] = {}, m_kAirRun[kAir] = {};
    float m_area[kMax] = {}, m_lobes[kMax] = {};
    float m_mass[kMax] = {}, m_hz[kMax] = {}, m_tau[kMax] = {}, m_eta[kMax] = {};
    float m_portQ = 5;
    Kind m_kind[kMax] = {};
    int8_t m_tongueOf[kMax] = {};
    float m_lineL[kTongues][kDelay] = {}, m_lineR[kTongues][kDelay] = {};
    float m_delayL[kTongues] = {}, m_delayR[kTongues] = {};
    int m_wr = 0;
    float m_f0[kTongues] = {};
    bool m_tuned = false, m_airOn = true;
    float m_decayN[kTongues] = {1, 1, 1, 1, 1, 1, 1, 1};
    float m_w2[kMax] = {};
    float m_ends[kStrips * kStripModes] = {};
    float m_gain = 1;
    // The lands' pull on the strips beside them, N/m: each tongue's bending
    // modes by each strip mode.
    alignas(32) float m_join[kTongues * fe::kB][kStrips * kStripModes] = {};
    float m_ql = 0, m_qr = 0;
    float m_energy = 0, m_refEnergy = 1, m_sleepEnergy = 0;
    int m_quiet = 0;
    float m_muteD = 1.0f;
    float m_lastContact = 0;
    bool m_sleeping = true, m_touching = false;
    Contact m_contacts[kContacts];
};

}
