#pragma once

// A daxophone (docs/daxophone.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace tf::dax {

// Generalised symmetric eigenproblem K v = λ M v (M positive definite), by
// Cholesky reduction and cyclic Jacobi. Returns eigenvalues ascending and,
// in V (row k = mode k), M-normalised eigenvectors.
inline std::vector<double> eigenGeneral(const std::vector<double> &K, const std::vector<double> &M, int n,
                                        std::vector<double> &V) {
    // M = L Lᵀ
    std::vector<double> L(size_t(n) * n, 0.0);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j <= i; ++j) {
            double s = M[size_t(i) * n + j];
            for (int k = 0; k < j; ++k)
                s -= L[size_t(i) * n + k] * L[size_t(j) * n + k];
            L[size_t(i) * n + j] = i == j ? std::sqrt(std::max(s, 1e-300)) : s / L[size_t(j) * n + j];
        }
    // A = L⁻¹ K L⁻ᵀ: first X = L⁻¹ K, then A = X L⁻ᵀ = (L⁻¹ Xᵀ)ᵀ
    auto lsolve = [&](std::vector<double> &B) { // B ← L⁻¹ B, column by column (B n×n, row major)
        for (int c = 0; c < n; ++c)
            for (int i = 0; i < n; ++i) {
                double s = B[size_t(i) * n + c];
                for (int k = 0; k < i; ++k)
                    s -= L[size_t(i) * n + k] * B[size_t(k) * n + c];
                B[size_t(i) * n + c] = s / L[size_t(i) * n + i];
            }
    };
    std::vector<double> A = K;
    lsolve(A);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            std::swap(A[size_t(i) * n + j], A[size_t(j) * n + i]);
    lsolve(A);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const double s = 0.5 * (A[size_t(i) * n + j] + A[size_t(j) * n + i]);
            A[size_t(i) * n + j] = A[size_t(j) * n + i] = s;
        }
    std::vector<double> E(size_t(n) * n, 0.0);
    for (int i = 0; i < n; ++i)
        E[size_t(i) * n + i] = 1;
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0, diag = 0;
        for (int i = 0; i < n; ++i) {
            diag += A[size_t(i) * n + i] * A[size_t(i) * n + i];
            for (int j = i + 1; j < n; ++j)
                off += A[size_t(i) * n + j] * A[size_t(i) * n + j];
        }
        if (off <= 1e-26 * diag)
            break;
        for (int p = 0; p < n; ++p)
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
                    const double ekp = E[size_t(k) * n + p], ekq = E[size_t(k) * n + q];
                    E[size_t(k) * n + p] = c * ekp - s * ekq;
                    E[size_t(k) * n + q] = s * ekp + c * ekq;
                }
            }
    }
    std::vector<int> order(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        order[size_t(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return A[size_t(a) * n + a] < A[size_t(b) * n + b]; });
    std::vector<double> lam(static_cast<size_t>(n));
    V.assign(size_t(n) * n, 0.0);
    for (int r = 0; r < n; ++r) {
        const int k = order[size_t(r)];
        lam[size_t(r)] = A[size_t(k) * n + k];
        // v = L⁻ᵀ y (back substitution with Lᵀ)
        for (int i = n - 1; i >= 0; --i) {
            double s = E[size_t(i) * n + k];
            for (int j = i + 1; j < n; ++j)
                s -= L[size_t(j) * n + i] * V[size_t(r) * n + j];
            V[size_t(r) * n + i] = s / L[size_t(i) * n + i];
        }
    }
    return lam;
}


// --- the tongue as built ----------------------------------------------------------------------------------

struct Spec {
    // The tongue: a board of wood sawn to an outline, clamped at one end.
    double L = 0.33, w0 = 0.03, h = 0.005;    // m: length out of the clamp, reference width, thickness
    double E = 12.6e9, rho = 705, G = 0.79e9; // along the grain, density, shear across it (sugar maple)
    double etaL = 0.008, etaLT = 0.02;        // loss factors: bending along the grain, twisting
    double air = 1.0;                         // s⁻¹: the air's drag and radiation, every mode alike
    // The outline: width (× w0) and how far the middle of the width stands
    // off the clamp's centre line (× w0), at points along the length (× L).
    static constexpr int kPts = 7;
    double xs[kPts] = {0, 0.15, 0.35, 0.55, 0.75, 0.9, 1.0};
    double ws[kPts] = {1.0, 0.85, 1.15, 0.8, 1.1, 0.9, 0.7};
    double cs[kPts] = {0, 0.05, -0.08, 0.12, -0.05, 0.1, 0.15};
    // The dax pressed flat on the tongue through the cardboard on its
    // smooth side, in the tongue's own units (EI/L³ against the press,
    // EI/L against rocking), and the cardboard's loss factor.
    double Kc = 2e5, Kr = 70.0, etaC = 0.25;
    // How hard the dax is pressed (N), lightly and firmly, and half the
    // length of tongue its curved face presses on (m): the most moment it
    // holds before it rocks on its edge is the press times that.
    double pressLight = 8.0, pressFirm = 40.0, patch = 0.006;
    // What the back of the tongue and the block take from a mode: the
    // loss factor of what passes the dax into them.
    double etaBack = 0.1;
    // What the clamp's block, on its stand, takes from the part behind the
    // dax: the loss factor of its modes' swing into the block.
    double etaClamp = 0.06;
    double profile(const double *v, double x) const {
        for (int i = 0; i + 1 < kPts; ++i)
            if (x <= xs[i + 1])
                return v[i] + (v[i + 1] - v[i]) * (x - xs[i]) / (xs[i + 1] - xs[i]);
        return v[kPts - 1];
    }
    double width(double x) const { return profile(ws, x); }
    double offset(double x) const { return profile(cs, x); }
    // ω of a mode whose dimensionless eigenvalue is Λ: √Λ · h/L² · √(E/12ρ).
    double omegaScale() const { return h / (L * L) * std::sqrt(E / (12.0 * rho)); }
    double mass() const { return rho * h * w0 * L; } // m0, the reference mass
};

constexpr int kFront = 16; // the bowed part's mode families, followed along the dax's travel
constexpr int kBack = 12;  // the part behind the dax's (every one under 4 kHz, all along)
constexpr int kU = 64;     // dax positions tabulated
constexpr double kUMin = 0.03, kUMax = 0.92;

// One dax position's modes, each family in its place.
struct Entry {
    float u = 0;
    // In front of the dax (bowed): frequency, the tip's swing (× L) and
    // twist (rad) per unit of the mode, the moment and torque it puts on
    // the dax, and the shares of its strain energy in twisting and in the
    // dax's hold.
    float hz[kFront] = {}, tipW[kFront] = {}, tipP[kFront] = {}, rootM[kFront] = {}, rootT[kFront] = {};
    float inW[2][kFront] = {}; // the swing a sixteenth and an eighth of the bowed part in from the tip
    float fT[kFront] = {}, fR[kFront] = {};
    // Behind it: frequency, how a moment and a torque at the dax drive the
    // mode, what it puts on the clamp, and its share in twisting and in
    // the dax's springs.
    float bhz[kBack] = {}, inM[kBack] = {}, inT[kBack] = {}, outM[kBack] = {}, outT[kBack] = {};
    float bfT[kBack] = {}, bfD[kBack] = {};
    float statM = 0, statT = 0; // the clamp's moment (torque) for a moment (torque) on the dax, held still
    float rootShare = 0;        // of the dax's hold on the bowed part's rocking, how much is the back's
    float kRoot = 1;            // that hold, all of it (EI/L)
};

struct Design {
    Spec spec;
    Entry e[kU];
    double m0 = 1, omega = 1;
    // The dax position for an index s ∈ [0, kU−1], finer toward the tip,
    // where the pitch moves fastest.
    static double uAt(double s) {
        const double t = s / double(kU - 1);
        return kUMin + (kUMax - kUMin) * (1.0 - (1.0 - t) * (1.0 - t));
    }
    static double sAt(double u) {
        const double x = (std::clamp(u, kUMin, kUMax) - kUMin) / (kUMax - kUMin);
        return (1.0 - std::sqrt(std::max(0.0, 1.0 - x))) * double(kU - 1);
    }
};

namespace detail {

constexpr int kSamp = 24; // points along a part its shapes are compared at, to follow each family

// A part of the tongue between x0 and x1 (× L), on Ne elements: Euler–
// Bernoulli bending on Hermite elements and Saint-Venant twisting on
// linear ones, coupled through the outline's mass off the centre line.
// Each end's three motions (swing, rocking, twist) are clamped (cl), held
// by a spring (k > 0), or free.
struct End {
    bool cl[3] = {false, false, false};
    double k[3] = {0, 0, 0};
};

struct Modes {
    int n = 0;
    std::vector<double> lam;
    std::vector<double> end0[3], end1[3]; // each mode's swing, rocking and twist at either end
    std::vector<double> near1[2];         // and its swing one and two elements in from the far end
    std::vector<double> reac0[3];         // and what the near end holds: shear, moment, torque
    std::vector<double> fT, f0, f1;       // its strain energy's shares: twisting, the springs at x0, at x1
    std::vector<std::vector<double>> shape;
    double stat[2][2] = {}; // the near end's moment and torque, for a unit moment and torque at the far end, held still
};

inline Modes solvePart(const Spec &sp, double x0, double x1, int Ne, const End &a, const End &b, int keep) {
    const double wl = sp.w0 / sp.L, hw = sp.h / sp.w0, GE = sp.G / sp.E;
    // DOF numbering: node·3 + motion, the clamped ones taken out.
    std::vector<int> map(size_t(3 * (Ne + 1)), -1);
    int n = 0;
    for (int node = 0; node <= Ne; ++node)
        for (int d = 0; d < 3; ++d)
            if (!(node == 0 && a.cl[d]) && !(node == Ne && b.cl[d]))
                map[size_t(3 * node + d)] = n++;
    std::vector<double> K(size_t(n) * n, 0.0), M(size_t(n) * n, 0.0);
    std::vector<double> Kt(size_t(Ne), 0.0);
    double Ke0[6][6] = {}, Me0[6][6] = {};
    static const double gx[4] = {-0.861136311594053, -0.339981043584856, 0.339981043584856, 0.861136311594053};
    static const double gw[4] = {0.347854845137454, 0.652145154862546, 0.652145154862546, 0.347854845137454};
    const double le = (x1 - x0) / Ne, l2 = le * le;
    for (int e = 0; e < Ne; ++e) {
        const double xm = x0 + (e + 0.5) * le;
        const double w = sp.width(xm), c = sp.offset(xm) * wl;
        const double EI = w, mu = w, GJ = 4.0 * GE * w * (1.0 - 0.63 * hw / w), Ip = w * w * w * wl * wl / 12.0;
        Kt[size_t(e)] = GJ / le;
        double Ke[6][6] = {}, Me[6][6] = {}; // w0 θ0 φ0 w1 θ1 φ1
        const int bi[4] = {0, 1, 3, 4};
        const double kb[4][4] = {{12, 6 * le, -12, 6 * le}, {6 * le, 4 * l2, -6 * le, 2 * l2}, {-12, -6 * le, 12, -6 * le}, {6 * le, 2 * l2, -6 * le, 4 * l2}};
        const double mb[4][4] = {{156, 22 * le, 54, -13 * le}, {22 * le, 4 * l2, 13 * le, -3 * l2}, {54, 13 * le, 156, -22 * le}, {-13 * le, -3 * l2, -22 * le, 4 * l2}};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                Ke[bi[i]][bi[j]] += EI / (l2 * le) * kb[i][j];
                Me[bi[i]][bi[j]] += mu * le / 420.0 * mb[i][j];
            }
        Ke[2][2] += GJ / le, Ke[5][5] += GJ / le, Ke[2][5] -= GJ / le, Ke[5][2] -= GJ / le;
        Me[2][2] += Ip * le / 3, Me[5][5] += Ip * le / 3, Me[2][5] += Ip * le / 6, Me[5][2] += Ip * le / 6;
        // The outline's mass off the centre line: a swing twists it, a twist swings it.
        for (int g = 0; g < 4; ++g) {
            const double xi = 0.5 * (gx[g] + 1), W = 0.5 * gw[g] * le;
            const double Nw[4] = {1 - 3 * xi * xi + 2 * xi * xi * xi, le * (xi - 2 * xi * xi + xi * xi * xi), 3 * xi * xi - 2 * xi * xi * xi,
                                  le * (-xi * xi + xi * xi * xi)};
            const double Np[2] = {1 - xi, xi};
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 2; ++j) {
                    const double v = mu * c * Nw[i] * Np[j] * W;
                    Me[bi[i]][j ? 5 : 2] += v;
                    Me[j ? 5 : 2][bi[i]] += v;
                }
        }
        if (e == 0)
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 6; ++j)
                    Ke0[i][j] = Ke[i][j], Me0[i][j] = Me[i][j];
        for (int i = 0; i < 6; ++i)
            for (int j = 0; j < 6; ++j) {
                const int ia = map[size_t(3 * e + i)], ib = map[size_t(3 * e + j)];
                if (ia >= 0 && ib >= 0) {
                    K[size_t(ia) * n + ib] += Ke[i][j];
                    M[size_t(ia) * n + ib] += Me[i][j];
                }
            }
    }
    for (int d = 0; d < 3; ++d) {
        if (!a.cl[d] && a.k[d] > 0)
            K[size_t(map[size_t(d)]) * n + map[size_t(d)]] += a.k[d];
        if (!b.cl[d] && b.k[d] > 0)
            K[size_t(map[size_t(3 * Ne + d)]) * n + map[size_t(3 * Ne + d)]] += b.k[d];
    }
    Modes o;
    // Held still: the far end rocked by a unit moment, then twisted by a
    // unit torque; what the near end holds then (Gaussian elimination).
    for (int load = 0; load < 2; ++load) {
        const int dof = map[size_t(3 * Ne + 1 + load)];
        if (dof < 0)
            continue;
        std::vector<double> A = K, x(size_t(n), 0.0);
        x[size_t(dof)] = 1.0;
        for (int c = 0; c < n; ++c) {
            int p = c;
            for (int r = c + 1; r < n; ++r)
                if (std::fabs(A[size_t(r) * n + c]) > std::fabs(A[size_t(p) * n + c]))
                    p = r;
            if (p != c) {
                for (int k = 0; k < n; ++k)
                    std::swap(A[size_t(c) * n + k], A[size_t(p) * n + k]);
                std::swap(x[size_t(c)], x[size_t(p)]);
            }
            for (int r = c + 1; r < n; ++r) {
                const double f = A[size_t(r) * n + c] / A[size_t(c) * n + c];
                if (f == 0)
                    continue;
                for (int k = c; k < n; ++k)
                    A[size_t(r) * n + k] -= f * A[size_t(c) * n + k];
                x[size_t(r)] -= f * x[size_t(c)];
            }
        }
        for (int c = n - 1; c >= 0; --c) {
            double s = x[size_t(c)];
            for (int k = c + 1; k < n; ++k)
                s -= A[size_t(c) * n + k] * x[size_t(k)];
            x[size_t(c)] = s / A[size_t(c) * n + c];
        }
        double ue[6] = {};
        for (int i = 0; i < 6; ++i) {
            const int g = map[size_t(i)];
            ue[i] = g >= 0 ? x[size_t(g)] : 0.0;
        }
        for (int r = 0; r < 2; ++r) {
            double s = 0;
            for (int j = 0; j < 6; ++j)
                s += Ke0[1 + r][j] * ue[j];
            o.stat[r][load] = s;
        }
    }
    std::vector<double> V;
    std::vector<double> lam = eigenGeneral(K, M, n, V);
    o.n = std::min(n, keep);
    auto val = [&](const double *v, int node, int d) {
        const int g = map[size_t(3 * node + d)];
        return g >= 0 ? v[g] : 0.0;
    };
    for (int k = 0; k < o.n; ++k) {
        const double *v = &V[size_t(k) * n];
        const double L = std::max(lam[size_t(k)], 1e-9);
        o.lam.push_back(L);
        double ue[6];
        for (int i = 0; i < 6; ++i)
            ue[i] = val(v, i / 3, i % 3);
        for (int d = 0; d < 3; ++d) {
            o.end0[d].push_back(val(v, 0, d));
            o.end1[d].push_back(val(v, Ne, d));
            if (d < 2)
                o.near1[d].push_back(val(v, Ne - 1 - d, 0));
            double r = 0;
            for (int j = 0; j < 6; ++j)
                r += (Ke0[d][j] - L * Me0[d][j]) * ue[j];
            // A sprung motion holds what its spring does.
            if (!a.cl[d])
                r = a.k[d] * val(v, 0, d);
            o.reac0[d].push_back(r);
        }
        double ft = 0;
        for (int e = 0; e < Ne; ++e) {
            const double d = val(v, e + 1, 2) - val(v, e, 2);
            ft += Kt[size_t(e)] * d * d;
        }
        double f0 = 0, f1 = 0;
        for (int d = 0; d < 3; ++d) {
            f0 += a.cl[d] ? 0.0 : a.k[d] * val(v, 0, d) * val(v, 0, d);
            f1 += b.cl[d] ? 0.0 : b.k[d] * val(v, Ne, d) * val(v, Ne, d);
        }
        o.fT.push_back(ft / L);
        o.f0.push_back(f0 / L);
        o.f1.push_back(f1 / L);
        // The shape at fixed points along the part, weighted by the mass
        // there, so a family is the same motion from one dax position to the next.
        std::vector<double> s(2 * kSamp);
        for (int j = 0; j < kSamp; ++j) {
            const double xs = (j + 0.5) / kSamp * Ne;
            const int e = std::min(int(xs), Ne - 1);
            const double xi = xs - e;
            const double wv = (1 - 3 * xi * xi + 2 * xi * xi * xi) * val(v, e, 0) + le * (xi - 2 * xi * xi + xi * xi * xi) * val(v, e, 1) +
                              (3 * xi * xi - 2 * xi * xi * xi) * val(v, e + 1, 0) + le * (-xi * xi + xi * xi * xi) * val(v, e + 1, 1);
            const double pv = (1 - xi) * val(v, e, 2) + xi * val(v, e + 1, 2);
            const double w = sp.width(x0 + (x1 - x0) * (j + 0.5) / kSamp);
            s[size_t(2 * j)] = wv * std::sqrt(w);
            s[size_t(2 * j + 1)] = pv * std::sqrt(w * w * w * wl * wl / 12.0);
        }
        o.shape.push_back(std::move(s));
    }
    return o;
}

// Which solved mode each of `families` is: the one most like it was a step
// before, so two that cross keep their own motion (as they do when the dax
// slides through quickly). The signs are turned to match.
inline void follow(const Modes &m, std::vector<std::vector<double>> &prev, int families, std::vector<int> &pick,
                   std::vector<double> &sign) {
    pick.assign(size_t(families), -1);
    sign.assign(size_t(families), 1.0);
    if (prev.empty()) {
        for (int f = 0; f < families; ++f)
            pick[size_t(f)] = std::min(f, m.n - 1);
    } else {
        struct P {
            double mac;
            int f, c;
            double dot;
        };
        std::vector<P> pairs;
        for (int f = 0; f < families; ++f)
            for (int c = 0; c < m.n; ++c) {
                double dot = 0, x = 0, y = 0;
                for (size_t i = 0; i < prev[size_t(f)].size(); ++i) {
                    dot += prev[size_t(f)][i] * m.shape[size_t(c)][i];
                    x += prev[size_t(f)][i] * prev[size_t(f)][i];
                    y += m.shape[size_t(c)][i] * m.shape[size_t(c)][i];
                }
                pairs.push_back({dot * dot / std::max(x * y, 1e-300), f, c, dot});
            }
        std::sort(pairs.begin(), pairs.end(), [](const P &p, const P &q) { return p.mac > q.mac; });
        std::vector<char> used(size_t(m.n), 0);
        for (const P &p : pairs)
            if (pick[size_t(p.f)] < 0 && !used[size_t(p.c)]) {
                pick[size_t(p.f)] = p.c;
                sign[size_t(p.f)] = p.dot < 0 ? -1.0 : 1.0;
                used[size_t(p.c)] = 1;
            }
    }
    std::vector<std::vector<double>> now(static_cast<size_t>(families));
    for (int f = 0; f < families; ++f) {
        now[size_t(f)] = m.shape[size_t(pick[size_t(f)])];
        for (double &x : now[size_t(f)])
            x *= sign[size_t(f)];
    }
    prev = std::move(now);
}

} // namespace detail

inline Design build(const Spec &sp) {
    Design d;
    d.spec = sp;
    d.m0 = sp.mass();
    d.omega = sp.omegaScale();
    const double hzOf = d.omega / (2.0 * M_PI);
    const double wl = sp.w0 / sp.L;
    std::vector<std::vector<double>> prevF, prevB;
    std::vector<int> pick;
    std::vector<double> sign;
    for (int j = 0; j < kU; ++j) {
        const double u = Design::uAt(j);
        Entry &e = d.e[j];
        e.u = float(u);
        // The part behind the dax's width, for its stiffness to rocking.
        double wb = 0;
        for (int i = 0; i < 8; ++i)
            wb += sp.width(u * (i + 0.5) / 8) / 8;
        // In front: held at the dax against swinging and twisting outright
        // (the dax's edge and the tongue behind it), and against rocking
        // by the dax's hold and the bending stiffness of the part behind.
        const double kBackRock = 4.0 * wb / u;
        detail::End root, tip;
        root.cl[0] = root.cl[2] = true;
        root.k[1] = std::min(sp.Kr + kBackRock, 1e5);
        e.rootShare = float(kBackRock / (sp.Kr + kBackRock));
        e.kRoot = float(root.k[1]);
        const detail::Modes front = detail::solvePart(sp, u, 1.0, 16, root, tip, kFront + 8);
        detail::follow(front, prevF, kFront, pick, sign);
        for (int k = 0; k < kFront; ++k) {
            const int c = pick[size_t(k)];
            const double s = sign[size_t(k)];
            e.hz[k] = float(std::sqrt(front.lam[size_t(c)]) * hzOf);
            e.tipW[k] = float(s * front.end1[0][size_t(c)]);
            e.inW[0][k] = float(s * front.near1[0][size_t(c)]);
            e.inW[1][k] = float(s * front.near1[1][size_t(c)]);
            e.tipP[k] = float(s * front.end1[2][size_t(c)]);
            e.rootM[k] = float(s * front.reac0[1][size_t(c)]);
            e.rootT[k] = float(s * front.reac0[2][size_t(c)]);
            e.fT[k] = float(front.fT[size_t(c)]);
            e.fR[k] = float(front.f0[size_t(c)]);
        }
    }
    // The part behind the dax, followed from where it is longest (and its
    // modes lowest and most) toward the clamp, so the families are the
    // ones that matter where they are heard: from a stub a centimetre
    // long, its few low modes would be twisting ones.
    for (int j = kU - 1; j >= 0; --j) {
        const double u = Design::uAt(j);
        Entry &e = d.e[j];
        // Clamped in the block, held at the dax by its press (against
        // swinging), its rocking hold, and its edge across the width
        // (against twisting).
        const double wd = sp.width(u) * wl, kTw = sp.Kc * wd * wd / 12.0;
        detail::End clamp, dax;
        clamp.cl[0] = clamp.cl[1] = clamp.cl[2] = true;
        dax.k[0] = sp.Kc, dax.k[1] = sp.Kr, dax.k[2] = kTw;
        const detail::Modes back = detail::solvePart(sp, 0.0, u, 14, clamp, dax, kBack + 8);
        detail::follow(back, prevB, kBack, pick, sign);
        for (int k = 0; k < kBack; ++k) {
            const int c = pick[size_t(k)];
            const double s = sign[size_t(k)];
            e.bhz[k] = float(std::sqrt(back.lam[size_t(c)]) * hzOf);
            e.inM[k] = float(s * back.end1[1][size_t(c)]);
            e.inT[k] = float(s * back.end1[2][size_t(c)]);
            e.outM[k] = float(s * back.reac0[1][size_t(c)]);
            e.outT[k] = float(s * back.reac0[2][size_t(c)]);
            e.bfT[k] = float(back.fT[size_t(c)]);
            e.bfD[k] = float(back.f1[size_t(c)]);
        }
        e.statM = float(back.stat[0][0]);
        e.statT = float(back.stat[1][1]);
    }
    return d;
}

inline const Design &design() {
    static const Design d = build(Spec{});
    return d;
}
inline void prepare() { (void)design(); }


// The window of bow force (N, at 0.2 m/s) in which the tongue sings its
// note, every three semitones from C2 (MIDI 36) to C7, from this model's
// own scan (docs/daxophone.md): the forces at which it sings at every
// touch the Lead gives (the dax pressed at 0 and 0.35, bowed on the very
// end and an eighth in). What a player's arm learns of a tongue. Under it
// the bow skates; over it the hair holds on too long. It narrows and
// widens as the modes behind the dax and the tongue's own meet the note's
// harmonics, so it is a table, not a law.
constexpr int kWindows = 21;
constexpr float kWinLo[kWindows] = {0.187f, 0.187f, 0.248f, 0.327f, 0.376f, 0.433f, 0.498f, 0.498f, 0.573f, 0.658f, 0.871f,
                                    1.001f, 1.152f, 2.014f, 1.523f, 1.751f, 2.014f, 2.316f, 2.664f, 3.063f, 3.522f};
constexpr float kWinHi[kWindows] = {0.433f, 0.187f, 0.658f, 0.573f, 0.871f, 1.152f, 1.152f, 1.324f, 1.152f, 2.014f, 2.014f,
                                    2.014f, 2.316f, 2.664f, 3.522f, 2.316f, 3.522f, 4.051f, 3.522f, 3.063f, 4.658f};
// The window's floor and roof for a note (log-interpolated), and where an
// ordinary bow draws it: its middle, or where it is narrow, 1.2 times its
// floor (but never over its roof).
inline void window(float hz, float &lo, float &hi) {
    const float x = std::clamp((12.0f * std::log2(std::max(hz, 1.0f) / 440.0f) + 69.0f - 36.0f) / 3.0f, 0.0f, float(kWindows - 1) - 1e-3f);
    const int i = int(x);
    const float t = x - float(i);
    lo = kWinLo[i] * std::pow(kWinLo[i + 1] / kWinLo[i], t);
    hi = kWinHi[i] * std::pow(kWinHi[i + 1] / kWinHi[i], t);
}
inline float ordinaryForce(float hz) {
    float lo = 0, hi = 0;
    window(hz, lo, hi);
    return std::max(std::sqrt(lo * hi), std::min(1.2f * lo, hi));
}
// What a player has learned of where the dax goes on a tongue: a
// correction to the pitch, for each of kU places along it. Tongues of one
// design played by one player share one.
struct Ear {
    float map[kU];
    Ear() { std::fill(std::begin(map), std::end(map), 1.0f); }
};

// --- one tongue, bowed --------------------------------------------------------------------------------------

class Tongue {
public:
    // Rosin's friction: μ at rest, sliding fast, and the sliding speed it
    // falls over (a hyperbola, as rosin's measured curve is fitted).
    static constexpr float kMuS = 0.8f, kMuD = 0.35f, kV0 = 0.1f;

    void init(float rate, const Design *d = nullptr) {
        m_d = d ? d : &design();
        m_rate = rate;
        m_dt = 1.0f / rate;
        m_ear = &m_ownEar;
        const Spec &sp = m_d->spec;
        m_omega = float(m_d->omega);
        m_invM = float(1.0 / m_d->m0);
        m_invL = float(1.0 / sp.L);
        m_moment = float(sp.E * sp.w0 * sp.h * sp.h * sp.h / 12.0 / sp.L); // EI/L: the dimensionless moment's newton-metres
        m_slackGain = float(sp.L) * m_omega * m_omega;
        silence();
    }
    void silence() {
        for (int k = 0; k < kFront; ++k)
            m_q[k] = m_v[k] = 0;
        for (int k = 0; k < kBack; ++k)
            m_r[k] = m_rv[k] = 0;
        m_stick = false;
        m_slip = 0;
        m_energy = 0;
        m_fresh = true;
        m_gain = -1;
        m_heard = 0;
        m_was[0] = -1;
        // (The ear is not reset: a player knows where the dax goes on this
        // tongue, note after note.)
        m_period = 0;
        m_since = 0;
        m_q0 = m_frac = m_theta = 0;
        m_drawn = 0;
        m_cycles = 0;
    }
    // Where the dax stands (× L from the clamp), how hard it is pressed
    // (0 light .. 1 firm: a light press lets the cardboard lose more), the
    // bow's force (N) and speed (m/s), and where across the end it sits
    // (−1 .. 1, a corner either side) and along it (below). Reached at the
    // end of the next block.
    void setDax(float u) { m_uTo = std::clamp(u, float(kUMin), float(kUMax)); }
    void setDaxPress(float p) { m_pressTo = std::clamp(p, 0.0f, 1.0f); }
    // `along`: 0 on the very end, 1 an eighth of the bowed part in from
    // it (on the edge), where the upper modes swing less.
    void setBow(float force, float speed, float across, float along = 0) {
        m_forceTo = std::max(force, 0.0f);
        m_speedTo = speed;
        m_acrossTo = std::clamp(across, -1.0f, 1.0f);
        m_alongTo = std::clamp(along, 0.0f, 1.0f);
    }
    // Plays a pitch: the dax goes where the bowed part's fundamental is
    // that pitch, corrected by what the player has learned of this tongue:
    // a bowed tone sits flat of its mode, more as the dax rocks, so the
    // dax goes a little further out. The learning is the ear: once the bow
    // is drawn, the fundamental's cycles are timed, and while they are
    // within three semitones of the note the dax is nudged over a tenth of
    // a second until they are on it, and the place is remembered for this
    // stretch of the tongue (kU places), from note to note. hz ≤ 0 leaves
    // the dax where it is.
    void setPitch(float hz) {
        m_target = hz;
        if (hz > 0) {
            m_place = float(Design::sAt(daxFor(hz)));
            setDax(daxFor(hz * ear()));
        }
    }
    float heardHz() const { return m_period > 0 ? m_rate / m_period : 0.0f; }
    // What the player has learned for the place the note is at.
    float ear() const {
        const int j = std::min(int(m_place), kU - 2);
        const float t = m_place - float(j);
        return m_ear->map[j] + (m_ear->map[j + 1] - m_ear->map[j]) * t;
    }
    // Learns into (and plays from) a shared ear instead of its own.
    void shareEar(Ear *ear) { m_ear = ear ? ear : &m_ownEar; }

    // The bowed part's fundamental with the dax at u, and where the dax
    // stands for a fundamental (the player's ear).
    float hzAt(float u) const {
        const float s = float(Design::sAt(u));
        const int j = std::min(int(s), kU - 2);
        const float t = s - float(j);
        return m_d->e[j].hz[0] * std::pow(m_d->e[j + 1].hz[0] / m_d->e[j].hz[0], t);
    }
    float daxFor(float hz) const {
        const Entry *e = m_d->e;
        if (hz <= e[0].hz[0])
            return float(kUMin);
        if (hz >= e[kU - 1].hz[0])
            return float(kUMax);
        int lo = 0, hi = kU - 1;
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            (e[mid].hz[0] <= hz ? lo : hi) = mid;
        }
        const float t = std::log(hz / e[lo].hz[0]) / std::log(e[hi].hz[0] / e[lo].hz[0]);
        return float(Design::uAt(double(lo) + double(t)));
    }

    // Renders n samples (n ≤ 64) of what the two contact microphones in
    // the block hear (the clamp's moment, N·m, times gain, ramped to it
    // across the block), added to l and r alike.
    void render(float *l, float *r, int n, float gain) {
        if (m_fresh) {
            m_u = m_uTo, m_press = m_pressTo, m_force = m_forceTo, m_speed = m_speedTo, m_across = m_acrossTo, m_along = m_alongTo;
            coefficients(m_c0);
            m_uWas = m_u;
            m_fresh = false;
        }
        const float force0 = m_force, speed0 = m_speed;
        m_u = m_uTo, m_press = m_pressTo, m_force = m_forceTo, m_speed = m_speedTo, m_across = m_acrossTo, m_along = m_alongTo;
        // Nothing the coefficients hang on has moved (the dax still): they
        // stand as they were, and nothing is blended across the block.
        // (The ear's nudges, under 2·10⁻⁴ of a table step, a
        // hundredth of a cent, count as still until they add up.)
        const bool still = m_was[0] >= 0 && std::fabs(Design::sAt(m_u) - Design::sAt(m_was[0])) < 2e-4 && m_was[1] == m_press &&
                           m_was[2] == m_across && m_was[3] == m_along;
        if (still) {
            m_c1 = m_c0;
            m_u = m_was[0];
        } else {
            coefficients(m_c1);
            m_was[0] = m_u, m_was[1] = m_press, m_was[2] = m_across, m_was[3] = m_along;
        }
        // (A dax thrown further than a step of the table in one block
        // blends two tongues' rocking; for that block it doesn't rock.)
        if (std::fabs(Design::sAt(m_u) - Design::sAt(m_uWas)) > 1.0)
            for (int k = 0; k < kFront; ++k)
                m_c0.f[TH][k] = m_c1.f[TH][k] = 0;
        m_uWas = m_u;
        const float inv = 1.0f / float(n);
        Coef d;
        if (!still) {
            for (int f = 0; f < kFC; ++f)
                for (int k = 0; k < kFront; ++k)
                    d.f[f][k] = (m_c1.f[f][k] - m_c0.f[f][k]) * inv;
            for (int f = 0; f < kBC; ++f)
                for (int k = 0; k < kBack; ++k)
                    d.b[f][k] = (m_c1.b[f][k] - m_c0.b[f][k]) * inv;
            for (int f = 0; f < kSC; ++f)
                d.s[f] = (m_c1.s[f] - m_c0.s[f]) * inv;
        }
        Coef c = m_c0;
        // A mode retuned keeps its swing's action (its energy over its
        // frequency), as the swing of a string does under a finger that
        // slides: it is carried over at the square root of the old
        // frequency over the new, a sample at a time.
        float carry[kFront], carryB[kBack];
        for (int k = 0; k < kFront; ++k)
            carry[k] = still ? 1.0f : std::exp(0.5f * inv * std::log(m_c0.f[W][k] / m_c1.f[W][k]));
        for (int k = 0; k < kBack; ++k)
            carryB[k] = still ? 1.0f : std::exp(0.5f * inv * std::log(m_c0.b[WB][k] / m_c1.b[WB][k]));
        const float w2 = m_omega * m_omega;
        float clampSq = 0;
        if (m_gain < 0)
            m_gain = gain;
        const float g0 = m_gain * m_moment, dg = (gain - m_gain) * m_moment * inv;
        m_gain = gain;
        for (int i = 0; i < n; ++i) {
            const float t = float(i + 1) * inv;
            if (!still) {
                for (int f = 0; f < kFC; ++f)
                    for (int k = 0; k < kFront; ++k)
                        c.f[f][k] += d.f[f][k];
                for (int f = 0; f < kBC; ++f)
                    for (int k = 0; k < kBack; ++k)
                        c.b[f][k] += d.b[f][k];
                for (int f = 0; f < kSC; ++f)
                    c.s[f] += d.s[f];
            }
            const float Fb = force0 + (m_force - force0) * t, vb = speed0 + (m_speed - speed0) * t;
            // The bowed part's free motion over the sample, and how fast
            // the bow's point would move without a new force.
            float vfree = 0, Y = 0;
            for (int k = 0; k < kFront; ++k) {
                const float q = m_q[k] * carry[k], v = m_v[k] * carry[k];
                m_q[k] = c.f[A11][k] * q + c.f[A12][k] * v;
                m_v[k] = c.f[A21][k] * q + c.f[A22][k] * v;
                const float bw = c.f[B][k] * c.f[W][k];
                vfree += bw * m_v[k];
                Y += bw * c.f[B][k] * c.f[GV][k];
            }
            // Rosin is grains: its grip is never quite the same twice.
            m_grain = 0.7f * m_grain + 0.3f * m_rng.bipolar();
            const float F = friction(vfree, vb, Fb * (1.0f + kGrain * m_grain), Y * m_invM) * m_invM;
            // The dax rocks: what it would hold of the tongue's rocking
            // over it, linearly, is more than its press can, so it rolls on
            // its curved face and the moment it holds flattens off. What it
            // fails to hold works on the tongue (its hold softer at the
            // peaks), and passes on behind it no more than it holds.
            const float rock = c.s[KD] * m_theta;
            const float held = c.s[MS] * fastTanh(rock / c.s[MS]);
            const float slack = (rock - held) * m_slackGain;
            float Mf = 0, Tf = 0, theta = 0;
            for (int k = 0; k < kFront; ++k) {
                const float b = c.f[B][k] * F + c.f[TH][k] * slack;
                m_q[k] += b * c.f[GQ][k];
                m_v[k] += b * c.f[GV][k];
                Mf += c.f[RM][k] * m_q[k];
                Tf += c.f[RT][k] * m_q[k];
                theta += c.f[TH][k] * m_q[k];
            }
            Mf = Mf * m_invL - (rock - held);
            m_theta = theta * m_invL;
            // The ear: the bowed part's fundamental swinging up through the
            // middle, timed to a fraction of a sample, cycle after cycle.
            ++m_since;
            if (m_q0 < 0 && m_q[0] >= 0) {
                const float frac = m_q[0] / (m_q[0] - m_q0);
                const float at = float(m_since) - frac + m_frac;
                if (at > 4.0f && at < m_rate / 20.0f) {
                    m_period = m_cycles == 0 ? at : m_period + 0.3f * (at - m_period);
                    ++m_cycles;
                }
                m_since = 0;
                m_frac = frac;
            }
            m_q0 = m_q[0];
            Tf *= m_invL;
            // What passes the dax: the part behind it, driven by the
            // moment and torque the bowed part puts on the dax, and what
            // it puts on the clamp (its modes, plus what the modes not
            // followed give held still).
            float Mc = c.s[SMM] * Mf + c.s[SMT] * Tf;
            for (int k = 0; k < kBack; ++k) {
                const float rr = m_r[k] * carryB[k], rv = m_rv[k] * carryB[k];
                const float a = w2 * (c.b[IM][k] * Mf + c.b[IT][k] * Tf);
                m_r[k] = c.b[A11][k] * rr + c.b[A12][k] * rv + a * c.b[GQ][k];
                m_rv[k] = c.b[A21][k] * rr + c.b[A22][k] * rv + a * c.b[GV][k];
                Mc += c.b[OM][k] * m_r[k];
            }
            clampSq += Mc * Mc;
            const float g = g0 + dg * float(i + 1);
            // Two piezo discs, one either side of the middle, wired in
            // parallel: a twist reaches them in opposite senses and cancels,
            // so what they say is the clamp's moment, one voice.
            l[i] += Mc * g;
            r[i] += Mc * g;
        }
        float e = 0;
        for (int k = 0; k < kFront; ++k)
            e += m_w[k] * m_w[k] * (m_v[k] * m_v[k] + m_q[k] * m_q[k]);
        m_energy = 0.5f * e * float(m_d->m0);
        // What reaches the clamp, against what swings.
        if (m_energy > 1e-9f) {
            const float now = std::sqrt(clampSq * inv / m_energy);
            const float k = 1.0f - std::exp(-float(n) * m_dt / 0.08f);
            m_heard = m_heard > 0 ? m_heard + (now - m_heard) * k : now;
        }
        m_c0 = m_c1;
        if (m_since > int(0.1f * m_rate))
            m_cycles = 0; // no cycle for a tenth of a second: nothing to hear
        // The ear listens once the bow is drawn (most of its speed, for a
        // twentieth of a second) and the tone has settled into its cycles.
        m_drawn = std::fabs(m_speed) > 0.05f && m_force > 0 ? m_drawn + float(n) * m_dt : 0.0f;
        if (m_target > 0 && m_cycles >= 6 && m_drawn > 0.05f) {
            const float off = std::log2(m_rate / m_period / m_target);
            if (std::fabs(off) < 3.0f / 12.0f) {
                const float step = std::exp2(-off * std::min(1.0f, float(n) * m_dt / kEarS));
                const int j = std::min(int(m_place), kU - 2);
                const float t = m_place - float(j);
                m_ear->map[j] = std::clamp(m_ear->map[j] * std::pow(step, 1.0f - t), 0.79f, 1.26f);
                m_ear->map[j + 1] = std::clamp(m_ear->map[j + 1] * std::pow(step, t), 0.79f, 1.26f);
            }
        }
    }

    float energy() const { return m_energy; } // J, in the bowed part
    // How much of the bowed part's swing reaches the clamp: the clamp's
    // moment (rms, dimensionless) over the square root of the bowed part's
    // energy (J), as heard over the last tenth of a second or so. Before
    // there is anything to hear, the part behind the dax's response at the
    // fundamental where the dax is going (kTransHeard of it, on average,
    // is what is heard).
    float transmission() {
        if (m_fresh) {
            m_u = m_uTo, m_press = m_pressTo, m_across = m_acrossTo, m_along = m_alongTo;
            coefficients(m_c0);
        }
        return m_heard > 0 ? m_heard : kTransHeard * m_trans;
    }
    float dax() const { return m_u; }
    bool sticking() const { return m_stick; }
    float bowPointSpeed() const { return m_lastV; }
    float frictionForce() const { return m_lastF; }
    // The families' frequencies now (Hz), for the tests.
    float modeHz(int k) const { return m_w[std::clamp(k, 0, kFront - 1)] / kTwoPi; }
    float modalTransmission() const { return m_trans; }

    // How long the player's ear takes to bring the pitch in, and how much
    // rosin's grip varies from grain to grain.
    static constexpr float kEarS = 0.06f, kGrain = 0.04f, kSlipNoise = 0.25f;
    static constexpr float kTransHeard = 0.69f;

private:
    // Each mode's state is its swing q and its speed over its angular
    // frequency (u = q̇/ω): its step is then a damped rotation, and two
    // steps blended across a block (the dax moving) stay a rotation that
    // only shrinks, never one that grows, however far apart they are.
    enum { A11 = 0, A12, A21, A22, GQ, GV, B, RM, RT, W, TH, kFC };
    enum { IM = GV + 1, IT, OM, OT, WB, kBC };
    enum { SMM = 0, SMT, STM, STT, KD, MS, kSC };
    struct Coef {
        float f[kFC][kFront] = {};
        float b[kBC][kBack] = {};
        float s[kSC] = {};
    };

    // A damped mode's exact step over a sample, in (q, q̇/ω), and its
    // response to a constant push (per unit of acceleration) through it.
    static void step(float *c[6], int k, float w, float sig, float dt) {
        const float wd = std::sqrt(std::max(w * w - sig * sig, 1.0f));
        const float dec = std::exp(-sig * dt), cs = std::cos(wd * dt), sn = std::sin(wd * dt);
        const float e = sig / wd, rot = w / wd;
        c[0][k] = dec * (cs + e * sn);
        c[1][k] = dec * rot * sn;
        c[2][k] = -dec * rot * sn;
        c[3][k] = dec * (cs - e * sn);
        c[4][k] = (1.0f - dec * (cs + e * sn)) / (w * w);
        c[5][k] = dec * sn / (wd * w);
    }

    // Each family's coefficients with the dax where it is now.
    void coefficients(Coef &c) {
        const Design &D = *m_d;
        const Spec &sp = D.spec;
        const float s = float(Design::sAt(m_u));
        const int j = std::min(int(s), kU - 2);
        const float t = s - float(j);
        const Entry &a = D.e[j], &b = D.e[j + 1];
        auto mix = [&](float x, float y) { return x + (y - x) * t; };
        auto hzMix = [&](float x, float y) { return x * std::exp(t * std::log(std::max(y, 1e-3f) / std::max(x, 1e-3f))); };
        // Over the last stretch to Nyquist a family fades out, so none folds back.
        auto keep = [&](float hz) {
            const float x = std::clamp((0.42f * m_rate - hz) / (0.06f * m_rate), 0.0f, 1.0f);
            return x * x * (3.0f - 2.0f * x);
        };
        const float half = 0.5f * float(sp.width(1.0) * sp.w0 / sp.L);
        const float across = m_across * half * 0.8f;
        const float share = mix(a.rootShare, b.rootShare);
        const float kRoot = mix(a.kRoot, b.kRoot);
        c.s[KD] = float(sp.Kr);
        c.s[MS] = float((sp.pressLight + (sp.pressFirm - sp.pressLight) * m_press) * sp.patch * sp.L /
                        (sp.E * sp.w0 * sp.h * sp.h * sp.h / 12.0));
        // A lighter press lets the cardboard give and lose more.
        const float etaRoot = float(sp.etaC) * (1.0f - share) * (1.6f - m_press) + float(sp.etaBack) * share;
        float *ff[6] = {c.f[A11], c.f[A12], c.f[A21], c.f[A22], c.f[GQ], c.f[GV]};
        for (int k = 0; k < kFront; ++k) {
            const float hz = hzMix(a.hz[k], b.hz[k]), w = kTwoPi * hz;
            const float fT = mix(a.fT[k], b.fT[k]), fR = mix(a.fR[k], b.fR[k]);
            const float eta = float(sp.etaL) * std::max(0.0f, 1.0f - fT - fR) + float(sp.etaLT) * fT + etaRoot * fR;
            step(ff, k, w, 0.5f * eta * w + float(sp.air), m_dt);
            m_w[k] = w;
            c.f[W][k] = w;
            const float x = keep(hz);
            const float w0 = mix(a.tipW[k], b.tipW[k]), w1 = mix(a.inW[0][k], b.inW[0][k]), w2 = mix(a.inW[1][k], b.inW[1][k]);
            const float at = 2.0f * m_along, swing = at < 1.0f ? w0 + (w1 - w0) * at : w1 + (w2 - w1) * (at - 1.0f);
            c.f[B][k] = x * (swing + across * mix(a.tipP[k], b.tipP[k]));
            c.f[RM][k] = x * mix(a.rootM[k], b.rootM[k]);
            // Its rocking at the dax (the hold's moment over its stiffness):
            // the dax's rocking works on the modes well under Nyquist only,
            // where a sample is short against their swing (an explicit push
            // on modes swinging more than a radian a sample isn't stable).
            const float slow = std::clamp((7000.0f - hz) / 3000.0f, 0.0f, 1.0f);
            c.f[TH][k] = slow * mix(a.rootM[k], b.rootM[k]) / kRoot;
            c.f[RT][k] = x * mix(a.rootT[k], b.rootT[k]);
        }
        // (A guard, not physics: the dax's rocking couples every mode
        // through one rocking, and pushed explicitly it holds only while
        // that coupling's stiffness, over a sample, stays under a unit. A
        // short bowed part rocks its high modes hard at the dax: there the
        // coupling is scaled back to the limit.)
        // And what it takes off the tongue's stiffness at most (the dax's
        // whole hold, Kr·Σθ²/Λ) stays a share under one, as it is for the
        // tongue itself; with the modes followed only so far, it might not.)
        float reach = 0, share2 = 0;
        for (int k = 0; k < kFront; ++k) {
            reach += c.f[TH][k] * c.f[TH][k];
            share2 += c.f[TH][k] * c.f[TH][k] * m_omega * m_omega / (m_w[k] * m_w[k]);
        }
        reach *= m_dt * m_dt * m_omega * m_omega * float(sp.Kr);
        share2 *= float(sp.Kr);
        const float cut = std::min({1.0f, std::sqrt(0.5f / std::max(reach, 1e-12f)), std::sqrt(0.9f / std::max(share2, 1e-12f))});
        for (int k = 0; k < kFront; ++k)
            c.f[TH][k] *= cut;
        float *bb[6] = {c.b[A11], c.b[A12], c.b[A21], c.b[A22], c.b[GQ], c.b[GV]};
        const float wf = m_w[0];
        float tRe = 0, tIm = 0;
        const float W2 = m_omega * m_omega;
        c.s[SMM] = mix(a.statM, b.statM);
        c.s[STT] = mix(a.statT, b.statT);
        c.s[SMT] = c.s[STM] = 0;
        for (int k = 0; k < kBack; ++k) {
            const float hz = hzMix(a.bhz[k], b.bhz[k]), w = kTwoPi * hz;
            const float fT = mix(a.bfT[k], b.bfT[k]), fD = mix(a.bfD[k], b.bfD[k]);
            const float eta = float(sp.etaL) * std::max(0.0f, 1.0f - fT - fD) + float(sp.etaLT) * fT +
                              float(sp.etaC) * (1.6f - m_press) * fD + float(sp.etaClamp);
            step(bb, k, w, 0.5f * eta * w + float(sp.air), m_dt);
            const float x = keep(hz);
            const float im = mix(a.inM[k], b.inM[k]), it = mix(a.inT[k], b.inT[k]);
            const float om = x * mix(a.outM[k], b.outM[k]), ot = x * mix(a.outT[k], b.outT[k]);
            c.b[IM][k] = im;
            c.b[WB][k] = w;
            c.b[IT][k] = it;
            c.b[OM][k] = om;
            c.b[OT][k] = ot;
            // What this mode gives held still is in the held-still part
            // already: taken out, as the mode itself gives it.
            const float st = W2 / (w * w);
            const float sig = 0.5f * eta * w + float(sp.air);
            const float dr = w * w - wf * wf, di = 2.0f * sig * wf, den = W2 / (dr * dr + di * di);
            tRe += om * im * (dr * den - st);
            tIm -= om * im * di * den;
            c.s[SMM] -= om * im * st;
            c.s[SMT] -= om * it * st;
            c.s[STM] -= ot * im * st;
            c.s[STT] -= ot * it * st;
        }
        tRe += mix(a.statM, b.statM);
        m_trans = std::sqrt(tRe * tRe + tIm * tIm);
    }

    // The bow's friction this sample: the hair sticks to the tongue while
    // the force that takes is within rosin's grip, else slides along
    // rosin's falling curve (the hyperbola's crossing with the tongue's
    // response is a quadratic), on the side it was sliding.
    float friction(float vfree, float vb, float Fb, float Y) {
        if (Fb <= 1e-6f || !(Y > 0)) {
            m_stick = false;
            m_slip = 0;
            m_lastV = vfree;
            m_lastF = 0;
            return 0;
        }
        const float a = vfree - vb;
        const float Fstick = -a / Y;
        auto stick = [&]() {
            m_stick = true;
            m_slip = 0;
            m_lastV = vb;
            m_lastF = Fstick;
            return Fstick;
        };
        // (A bow landing on a tongue it isn't sliding on grips it if it can.)
        if ((m_stick || m_slip == 0) && std::fabs(Fstick) <= kMuS * Fb)
            return stick();
        auto slide = [&](float sgn, float &x) {
            const float B = sgn * a - Y * Fb * kMuD, C = Y * Fb * (kMuS - kMuD) * kV0;
            const float p = kV0 - B, disc = p * p - 4.0f * (C - B * kV0);
            if (disc < 0)
                return false;
            x = 0.5f * (-p + std::sqrt(disc));
            return x > 0;
        };
        float sgn = m_slip != 0 ? (m_slip > 0 ? 1.0f : -1.0f) : (a > 0 ? 1.0f : -1.0f);
        float x = 0;
        if (!slide(sgn, x)) {
            if (std::fabs(Fstick) <= kMuS * Fb)
                return stick();
            sgn = -sgn;
            if (!slide(sgn, x))
                return stick();
        }
        m_stick = false;
        m_slip = sgn * x;
        // Sliding, the hair's grip is not smooth: its strands catch and let
        // go on the rosin's grains, a noise in the friction as loud as the
        // slide is fast (up to kSlipNoise of it).
        m_hiss = 0.5f * m_hiss + 0.5f * m_rng.bipolar();
        const float rough = 1.0f + kSlipNoise * m_hiss * std::min(1.0f, x / 0.05f);
        const float F = -sgn * Fb * (kMuD + (kMuS - kMuD) * kV0 / (kV0 + x)) * rough;
        m_lastV = vb + m_slip;
        m_lastF = F;
        return F;
    }

    const Design *m_d = nullptr;
    float m_rate = 48000, m_dt = 1.0f / 48000, m_omega = 1, m_invM = 1, m_invL = 1, m_moment = 1;
    float m_q[kFront] = {}, m_v[kFront] = {}, m_w[kFront] = {};
    float m_r[kBack] = {}, m_rv[kBack] = {};
    Coef m_c0, m_c1;
    bool m_fresh = true, m_stick = false;
    float m_slip = 0, m_lastV = 0, m_lastF = 0, m_energy = 0, m_trans = 0, m_gain = -1, m_heard = 0;
    float m_hiss = 0;
    Ear m_ownEar, *m_ear = &m_ownEar;
    float m_place = 0, m_drawn = 0;
    float m_target = 0, m_period = 0, m_grain = 0, m_q0 = 0, m_frac = 0, m_theta = 0, m_slackGain = 1;
    int m_since = 0, m_cycles = 0;
    tf::Rng m_rng{0xDA7u};
    float m_uWas = 0.5f, m_u = 0.5f, m_uTo = 0.5f, m_press = 0.7f, m_pressTo = 0.7f;
    float m_force = 0, m_forceTo = 0, m_speed = 0, m_speedTo = 0, m_across = 0, m_acrossTo = 0, m_along = 0, m_alongTo = 0;
    float m_was[4] = {-1, -1, -1, -1}; // what the coefficients were last worked out for: dax, press, across, along
};

}
