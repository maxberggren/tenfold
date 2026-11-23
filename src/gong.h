#pragma once

// A gong, as physics. Not samples and not a recipe: a thin bronze shell, its
// modes and the membrane stresses that couple them, in SI units, integrated
// sample by sample. The measurements it is built from and checked against
// are in docs/gong.md (the survey behind them in docs/gong-research.md).
//
//   the plate   the part of a gong that rings: a circular sheet of bronze,
//               a fraction of a millimetre to a few millimetres thick, held
//               at its edge by what surrounds it (an opera gong's stiff
//               conical flange, a bossed gong's deep rim, a tam-tam's
//               rolled-down ring). Its modes are the clamped plate's,
//               J_m(λr) − c·I_m(λr) times cos or sin of mθ, computed from
//               Bessel functions when the instrument is built; every mode
//               with nodal diameters comes as a pair of twins, which a real
//               gong's uneven hammering pulls a little apart, so they beat.
//   the shell   a gong is not flat: an opera gong's centre is flat or domed
//               by a millimetre, a bossed gong's face is curved, a tam-tam
//               has a central dome and rings of hammered bumps whose slope
//               changes sharply. That rise, w0(r), is what couples a
//               mode's swing to the stretching of the sheet at first order.
//   stretching  swung further than about its thickness, a sheet must
//               stretch. Its in-plane stress is von Kármán's Airy function,
//               Δ²F = −Eh/2·[L(w,w) + 2L(w0,w)]. For the opera and bossed
//               gongs it is resolved on the plate's axisymmetric in-plane
//               modes plus the uniform tension the stiff rim holds
//               (Berger's mean): each a strain ε = ½·xᵀHx + G·x of the modes
//               x with an energy ½κε², a sum of squares, so the stretching
//               only ever moves energy about. (Axisymmetric in-plane modes
//               couple modes of the same symmetry only, which is all a
//               strike in the middle of a thick gong wakes; it is a
//               truncation, not the full set.) It makes:
//               · a flat gong's pitch rise with its swing (hardening, the
//                 large opera gong glides down as it dies, Rossing &
//                 Fletcher 1983); a domed one's fall (the cubic xᵀHx·Gx
//                 term, softening: the small opera gong glides up,
//                 Fletcher 1985, whose one-mode equation this is exactly);
//               · a curved gong sound its fundamental's harmonics, which
//                 beat against the modes the smith tuned beside them (the
//                 Balinese gong's ombak, Krueger et al. 2010).
//   tam-tam     a thin, nearly flat sheet driven past its thickness has to
//               pass energy to modes of every symmetry, as wave turbulence
//               does. Its stress is resolved instead on sixteen patches of
//               the sheet, each with the uniform tension of its own mean
//               stretch (Marguerre's ½|∇w|² + ∇w0·∇w where the patch is): a
//               local, Berger-like approximation of an in-plane stress that
//               is really nonlocal, phenomenological, but dense, cheap
//               (4 multiply-adds per mode and patch) and still a sum of
//               squares. What lets it reach every symmetry is the hammered
//               sheet's irregular texture, its rings of bumps and hammer
//               marks: where the slope turns, the stretching drives modes
//               (Legge & Fletcher 1989). The sheet's linear part (plate,
//               smooth dome, texture) is solved for its own modes once, and
//               the tam-tam rings in those, so the stretching's nonlinear
//               rest can come and go without retuning anything. Struck
//               hard, the thud's energy climbs into ever higher modes over a
//               second or so: the tam-tam's shimmering swell.
//   the mallet  a padded beater, a mass on a felt spring that stiffens as
//               it is squeezed (F = K·δ^p), solved against the plate's
//               own motion under it every sample: soft and long on a big
//               gong, so the strike itself reaches only the low modes and
//               all the brightness must come from the stretching; quicker
//               and brighter the harder it lands.
//   the sound   each mode pushes the air by the volume it sweeps (an
//               axisymmetric mode like a piston; one with nodal diameters
//               as a multipole, weak until its wavelength in air is as
//               small as the gong), heard by two microphones a little
//               either side of its axis.
//
// The nonlinear dynamics are scale-free: in units of the sheet's
// thickness, they depend only on the shape (and the rise over the
// thickness), so each kind of gong is built once and every note is that
// gong scaled.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

#include "dsp.h"

namespace tf::gong {

enum Kind : int { Bossed = 0, Opera, TamTam, kKinds };

// --- Bessel functions, for the plate's mode shapes ---------------------------------------------------------
//
// From their integral representations, by the trapezoid rule over a full
// period: exponentially accurate once it has more points than x + m. Each
// call gives orders m − 1, m and m + 1 of J and I at once (what a mode's
// shape and its slopes need).

struct Bessel3 {
    double J[3], I[3]; // orders m − 1, m, m + 1
};

inline Bessel3 bessel3(int m, double x) {
    const int K = 40 + 2 * (int(std::fabs(x)) + std::abs(m));
    Bessel3 b{};
    for (int i = 0; i < K; ++i) {
        const double t = 2.0 * M_PI * (i + 0.5) / K;
        const double ct = std::cos(t), st = std::sin(t);
        const double ph = double(m) * t - x * st;
        const double c = std::cos(ph), s = std::sin(ph);
        b.J[1] += c;
        b.J[2] += c * ct - s * st;
        b.J[0] += c * ct + s * st;
        const double e = std::exp(x * ct), cm = std::cos(double(m) * t), sm = std::sin(double(m) * t);
        b.I[1] += e * cm;
        b.I[2] += e * (cm * ct - sm * st);
        b.I[0] += e * (cm * ct + sm * st);
    }
    for (int j = 0; j < 3; ++j) {
        b.J[j] /= K;
        b.I[j] /= K;
    }
    return b;
}

// The clamped circular plate's frequency equation for m nodal diameters:
// R(1) = R'(1) = 0 for R = J_m(λρ) − c·I_m(λρ).
inline double clampedEq(int m, double l) {
    const Bessel3 b = bessel3(m, l);
    const double Jd = 0.5 * (b.J[0] - b.J[2]), Id = 0.5 * (b.I[0] + b.I[2]);
    return b.J[1] * Id - Jd * b.I[1];
}

// Its roots λ (ω = λ²·√(D/ρh)/a²) up to lmax: about π apart, so a coarse
// scan brackets each and bisection finds it.
inline std::vector<double> clampedRoots(int m, double lmax) {
    std::vector<double> out;
    const double step = 0.25;
    double a = 0.3 + 0.5 * m, fa = clampedEq(m, a);
    for (double b = a + step; b < lmax + step; b += step) {
        const double fb = clampedEq(m, b);
        if ((fa < 0) != (fb < 0)) {
            double lo = b - step, hi = b, flo = fa;
            for (int it = 0; it < 40; ++it) {
                const double mid = 0.5 * (lo + hi), fm = clampedEq(m, mid);
                if ((fm < 0) == (flo < 0))
                    lo = mid, flo = fm;
                else
                    hi = mid;
            }
            out.push_back(0.5 * (lo + hi));
        }
        a = b;
        fa = fb;
    }
    return out;
}

// --- the materials and the instruments -------------------------------------------------------------------
//
// SI units. Sources in docs/gong.md.

struct Bronze {
    static constexpr float kE = 1.0e11f;   // Pa, B20 bell bronze
    static constexpr float kRho = 8700.0f; // kg/m³
    static constexpr float kNu = 0.34f;
    static constexpr float kAir = 343.0f;  // m/s
    static constexpr float kRhoAir = 1.2f; // kg/m³
};

// The model's constants that measured instruments set (docs/gong.md says
// which measurement each one answers to).
struct Knobs {
    // How much of the uniform tension an immovable rim would hold, the
    // flange's or ring's in-plane stiffness against the sheet's.
    static inline float rimOpera = 1.0f, rimBossed = 1.0f, rimTamTam = 0.28f;
    static inline float level = 0.5f;
    static inline float speed = 1.0f, texture = 1.0f, texSlope = -1.0f; // for exploring
    // A tam-tam's swing energy is limited to this (in thicknesses² of its
    // thud's swing): a few hard strokes' worth, a numerical guard.
    static inline float yield = 12.0f;
    static inline float cap = 0.18f; // modes above this share of the rate sit out
};

// What each kind of gong is: its sheet, its rise, its modes, its mallet.
struct Spec {
    Kind kind = Opera;
    float thick = 0.0008f;     // m
    float rise = 0;            // the shell's rise over the plate, in thicknesses
    float riseOver = 1.0f;     // the radius (of the plate's) it rises over: 1 the whole face, less a central dome
    float texture = 0;         // the hand-hammered sheet's irregular rise (rms per angular order, thicknesses)
    float textureSlope = 0.5f; // and how it falls with the order p (rms ∝ p^−slope)
    float rim = 1.0f;          // see Knobs
    int maxModes = 24;
    int maxM = 8;              // nodal diameters
    float maxRatio = 14.0f;    // modes up to this many times the fundamental
    float twin = 0.02f;        // how far a pair of twins is pulled apart, at most (a share of its frequency)
    float twinFirst = 0.05f;   // the first pair's (an opera gong's single-diameter twins sit far apart)
    int airy = 4;              // the in-plane modes the stress is resolved on
    int patches = 0;           // or, instead, the patches of the sheet it is resolved on (see Design)
    // The mallet, a player's choice for the gong: its mass as a share of
    // the fundamental's modal mass, its felt (F = K·δ^p, K set so that on a
    // rigid face at 1 m/s it touches for this share of the fundamental's
    // period), and how fast it lands at full strength, in the sheet's own
    // speed (its thickness times its fundamental's ω: a player strikes a
    // small gong with a lighter hand, as hard as it takes).
    float malletMass = 1.0f, felt = 2.5f, contact = 0.4f, speed = 3.0f;
    float velExp = 1.0f; // how the strength 0..1 maps to the speed (its power)
    float head = 0.2f;   // the beater's padded head: its radius, as a share of the sheet's
    float contactMax = 0; // the longest a stroke touches (rigid face), in periods; 0: no limit
};

inline Spec specOf(Kind k) {
    Spec s;
    s.kind = k;
    switch (k) {
    case Bossed:
        s.thick = 0.003f;
        s.rise = 3.0f;
        s.rim = Knobs::rimBossed;
        s.maxModes = 20;
        s.maxM = 5;
        s.maxRatio = 9.0f;
        s.twin = 0.018f;
        s.twinFirst = 0.1f;
        s.airy = 3;
        s.malletMass = 0.2f;
        s.felt = 1.5f;
        s.head = 0.35f;
        s.contactMax = 0.6f;
        s.contact = 0.55f;
        s.speed = 0.6f;
        s.velExp = 1.4f;
        break;
    case Opera:
        s.thick = 0.0008f;
        s.rise = 0; // set per note: see operaRise
        s.rim = Knobs::rimOpera;
        s.maxModes = 24;
        s.maxM = 7;
        s.maxRatio = 13.0f;
        s.twin = 0.015f;
        s.twinFirst = 0.08f;
        s.airy = 4;
        s.malletMass = 1.5f;
        s.head = 0.3f;
        s.contactMax = 0.7f;
        s.contact = 0.45f;
        s.speed = 4.5f;
        break;
    case TamTam:
        s.thick = 0.0015f;
        s.rise = 0.6f;
        s.riseOver = 0.35f;
        s.texture = 0.9f;
        s.patches = 16;
        s.rim = Knobs::rimTamTam;
        s.maxModes = 190;
        s.maxM = 40;
        s.maxRatio = 200.0f;
        s.twin = 0.012f;
        s.twinFirst = 0.02f;
        s.airy = 4;
        s.malletMass = 1.3f;
        s.head = 0.12f;
        s.contact = 0.7f;
        s.speed = 8.0f;
        break;
    default: break;
    }
    s.speed *= Knobs::speed;
    s.texture *= Knobs::texture;
    if (Knobs::texSlope >= 0)
        s.textureSlope = Knobs::texSlope;
    return s;
}

// The Chinese opera gong's centre, by its pitch: the large gongs (176–212
// Hz in Rossing & Fletcher's table) are flat in the middle and glide down;
// the small ones (472–700 Hz) are domed by about a millimetre over 0.7 mm
// of bronze and glide up. Between, the gong maker's choice.
inline float operaRise(float hz) {
    const float t = clampf(std::log2(hz / 330.0f) / std::log2(470.0f / 330.0f), 0.0f, 1.0f);
    return 1.43f * t * t * (3.0f - 2.0f * t);
}

// --- the instrument as built: modes, shapes and couplings --------------------------------------------------

struct Design {
    Kind kind = Opera;
    float riseKey = 0; // the rise it was built for (an opera gong's varies)
    int n = 0;         // modes
    int R = 0;         // stress channels: the rim's tension, then the Airy modes
    int P = 0;         // pairs of modes the stress couples
    static constexpr int kGrid = 128;
    std::vector<int> m, nr, orient;        // per mode: nodal diameters, circles (1…), cos (0) or sin (1)
    std::vector<float> lambda2;            // the plate's λ² (frequency ∝ λ²)
    std::vector<float> ratio;              // small-amplitude frequency over the fundamental, as built
    std::vector<float> mu;                 // modal mass over ρ·h·a²
    std::vector<float> kLin;               // the plate's own stiffness over E·h⁵/a² (per thickness²)
    std::vector<float> shape;              // R(ρ) on a grid, kGrid per mode (max |R| = 1)
    std::vector<float> area;               // the volume a mode sweeps per thickness of swing, over h·a²
    std::vector<float> kap;                // per channel, its stiffness over E·h⁵/a²
    std::vector<float> G;                  // [R][n] the linear (curvature) strain per mode
    std::vector<float> He, Hf;             // [R][P] the quadratic strain per pair (He: as it enters ε)
    std::vector<uint16_t> pk, pl;          // the pair's modes
    std::vector<int> axis;                 // the axisymmetric modes
    // A tam-tam's stress, resolved on patches of the sheet instead: each
    // patch's slope by every mode ([Pt][n], x and y), the rise's slope
    // there, and the patch's stiffness.
    int Pt = 0;
    std::vector<float> px, py, w0x, w0y, kapPt;
    std::vector<float> pxT, pyT; // the same, [n][kPtPad], for working out every patch at once
    std::vector<float> wtx, wty, Kax; // the texture's slope alone at each patch; the dome's smooth stiffness [na][na]
    // A tam-tam rings in its sheet's own modes (eigen): each one's λ² and
    // frequency over the thud's, the plate mode it is most of, and the
    // plate modes in it ([plate k][sheet j], mass-normalised).
    bool eigen = false;
    std::vector<float> eLam2, eRatio, T;
    std::vector<int> eDom;
    int thud = 0;
    float memScale = 0;                    // 12(1−ν²): the stretching's acceleration over Ω² per (κ·ε·∂ε)
    float lam1 = 1;                        // the fundamental, in the plate's λ² (ω = λ²·Ω)
    bool tuned = false;                    // its frequencies are a smith's (ratio), not the plate's own
};

// The eigenvalues of a symmetric matrix (n×n, row-major), by Jacobi
// rotations, in the order they fall; and, if asked, the eigenvectors (the
// columns of V).
inline std::vector<double> eigen(std::vector<double> K, int na, std::vector<double> *V = nullptr) {
    if (V) {
        V->assign(size_t(na) * na, 0.0);
        for (int i = 0; i < na; ++i)
            (*V)[size_t(i) * na + i] = 1.0;
    }
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0, diag = 0;
        for (int i = 0; i < na; ++i) {
            diag += K[size_t(i) * na + i] * K[size_t(i) * na + i];
            for (int j = i + 1; j < na; ++j)
                off += K[size_t(i) * na + j] * K[size_t(i) * na + j];
        }
        if (off < 1e-26 * diag)
            break;
        for (int p = 0; p < na; ++p)
            for (int q = p + 1; q < na; ++q) {
                const double apq = K[size_t(p) * na + q];
                if (std::fabs(apq) < 1e-300)
                    continue;
                const double th = 0.5 * std::atan2(2.0 * apq, K[size_t(q) * na + q] - K[size_t(p) * na + p]);
                const double c = std::cos(th), s = std::sin(th);
                for (int k = 0; k < na; ++k) {
                    const double kp = K[size_t(k) * na + p], kq = K[size_t(k) * na + q];
                    K[size_t(k) * na + p] = c * kp - s * kq;
                    K[size_t(k) * na + q] = s * kp + c * kq;
                }
                for (int k = 0; k < na; ++k) {
                    const double pk = K[size_t(p) * na + k], qk = K[size_t(q) * na + k];
                    K[size_t(p) * na + k] = c * pk - s * qk;
                    K[size_t(q) * na + k] = s * pk + c * qk;
                }
                if (V)
                    for (int k = 0; k < na; ++k) {
                        const double vp = (*V)[size_t(k) * na + p], vq = (*V)[size_t(k) * na + q];
                        (*V)[size_t(k) * na + p] = c * vp - s * vq;
                        (*V)[size_t(k) * na + q] = s * vp + c * vq;
                    }
            }
    }
    std::vector<double> ev;
    for (int i = 0; i < na; ++i)
        ev.push_back(K[size_t(i) * na + i]);
    return ev;
}

inline std::vector<double> eigenvalues(std::vector<double> K, int na) {
    auto ev = eigen(std::move(K), na);
    std::sort(ev.begin(), ev.end());
    return ev;
}

// The radial grid every shape is integrated on (midpoints).
constexpr int kQ = 360;
// A tam-tam's patches, at most (its stretching works on them all at once).
constexpr int kPtPad = 16;

// A clamped plate's radial shape R(ρ) and its slopes, max |R| = 1,
// positive where it first swings.
struct Radial {
    double l = 0;
    std::vector<double> R, R1, R2;
};

inline Radial radialShape(int m, double l) {
    Radial out;
    out.l = l;
    out.R.resize(kQ);
    out.R1.resize(kQ);
    out.R2.resize(kQ);
    const Bessel3 e = bessel3(m, l);
    const double c = e.J[1] / e.I[1];
    double top = 0;
    for (int i = 0; i < kQ; ++i) {
        const double x = l * (i + 0.5) / kQ;
        const Bessel3 b = bessel3(m, x);
        const double J = b.J[1], I = b.I[1];
        const double Jd = 0.5 * (b.J[0] - b.J[2]), Id = 0.5 * (b.I[0] + b.I[2]);
        const double Jdd = -Jd / x - (1.0 - double(m * m) / (x * x)) * J;
        const double Idd = -Id / x + (1.0 + double(m * m) / (x * x)) * I;
        out.R[i] = J - c * I;
        out.R1[i] = l * (Jd - c * Id);
        out.R2[i] = l * l * (Jdd - c * Idd);
        top = std::max(top, std::fabs(out.R[i]));
    }
    double first = 0;
    for (int i = 0; i < kQ && first == 0; ++i)
        if (std::fabs(out.R[i]) > 0.3 * top)
            first = out.R[i];
    const double g = (first < 0 ? -1.0 : 1.0) / top;
    for (int i = 0; i < kQ; ++i) {
        out.R[i] *= g;
        out.R1[i] *= g;
        out.R2[i] *= g;
    }
    return out;
}

// The clamped plate's roots for m nodal diameters up to λ = 40, and its
// shapes by m and circles n (1…), worked out once each and shared by every
// design. (Built on the main thread: see prepare.)
inline const std::vector<double> &plateRoots(int m) {
    static std::deque<std::vector<double>> roots;
    while (int(roots.size()) <= m)
        roots.push_back(clampedRoots(int(roots.size()), 40.0));
    return roots[size_t(m)];
}

inline const Radial &plateShape(int m, int n) {
    static std::deque<std::deque<Radial>> shapes;
    while (int(shapes.size()) <= m)
        shapes.emplace_back();
    auto &sm = shapes[size_t(m)];
    const auto &rm = plateRoots(m);
    n = std::clamp(n, 1, int(rm.size()));
    while (int(sm.size()) < n)
        sm.push_back(radialShape(m, rm[sm.size()]));
    return sm[size_t(n - 1)];
}

// Builds a kind of gong (once: it takes a few tens of milliseconds).
inline Design buildDesign(const Spec &sp, float rise) {
    Design d;
    d.kind = sp.kind;
    d.riseKey = rise;
    const double nu = Bronze::kNu;
    // The modes: every (m, n) of the clamped plate, by frequency.
    struct MN {
        int m, n;
        double l;
    };
    std::vector<MN> all;
    for (int m = 0; m <= sp.maxM; ++m) {
        const auto &roots = plateRoots(m);
        for (size_t j = 0; j < roots.size(); ++j)
            all.push_back({m, int(j) + 1, roots[j]});
    }
    std::sort(all.begin(), all.end(), [](const MN &a, const MN &b) { return a.l < b.l; });
    const double l1 = all.front().l;
    std::vector<MN> pick;
    int count = 0;
    for (const MN &q : all) {
        if (q.l * q.l > sp.maxRatio * l1 * l1)
            break;
        const int add = q.m == 0 ? 1 : 2;
        if (count + add > sp.maxModes)
            break;
        pick.push_back(q);
        count += add;
    }
    // The radial grid (midpoints) and each (m, n)'s shape and its slopes.
    constexpr int Q = kQ;
    std::vector<double> rho(Q);
    for (int i = 0; i < Q; ++i)
        rho[i] = (i + 0.5) / Q;
    const double dr = 1.0 / Q;
    std::vector<const Radial *> shapes;
    for (const MN &q : pick)
        shapes.push_back(&plateShape(q.m, q.n));
    // The rise: a spherical cap over riseOver of the radius, in
    // thicknesses; its slope and curvature by ρ. (A tam-tam's rings of
    // bumps and its hammer marks are its texture, below.)
    std::vector<double> w1(Q), w2(Q);
    {
        const double c = sp.riseOver;
        for (int i = 0; i < Q; ++i) {
            const double r = rho[i];
            // w0 = rise·(1 − (r/c)²) inside, 0 outside, smoothed at its edge.
            double s1 = 0, s2 = 0;
            if (r < c) {
                s1 = -2.0 * rise * r / (c * c);
                s2 = -2.0 * rise / (c * c);
            }
            w1[i] = s1;
            w2[i] = s2;
        }
        // (The cap's edge, where its curvature stops: smoothed over a few
        // grid steps so the strain has no step.)
        if (c < 1.0) {
            std::vector<double> s(w2);
            for (int i = 2; i < Q - 2; ++i)
                w2[i] = (s[i - 2] + s[i - 1] + s[i] + s[i + 1] + s[i + 2]) / 5.0;
        }
    }
    // The in-plane (Airy) modes: axisymmetric, F = F' = 0 at the edge.
    const int A = std::max(0, sp.airy);
    std::vector<double> zeta;
    std::vector<const Radial *> airyP;
    for (int r = 0; r < A; ++r) {
        airyP.push_back(&plateShape(0, r + 1));
        zeta.push_back(airyP.back()->l);
    }
    auto airyR = [&](int r) -> const Radial & { return *airyP[size_t(r)]; };
    // Now each mode, twins and all.
    d.n = 0;
    Rng twinRng(0x60e5u + uint32_t(sp.kind));
    std::vector<int> modeOf; // which (m, n) each mode is
    for (size_t j = 0; j < pick.size(); ++j) {
        const MN &q = pick[j];
        const int copies = q.m == 0 ? 1 : 2;
        // Twins: the uneven hammering pulls each pair apart, the first
        // furthest (a gong maker's single-diameter pair).
        const double split = q.m == 0 ? 0.0 : (q.m == 1 && q.n == 1 ? sp.twinFirst : sp.twin * (0.3 + 0.7 * twinRng.unit()));
        for (int o = 0; o < copies; ++o) {
            d.m.push_back(q.m);
            d.nr.push_back(q.n);
            d.orient.push_back(o);
            const double l2 = q.l * q.l * (copies == 2 ? (o == 0 ? 1.0 - 0.5 * split : 1.0 + 0.5 * split) : 1.0);
            d.lambda2.push_back(float(l2));
            modeOf.push_back(int(j));
            ++d.n;
        }
    }
    const int N = d.n;
    d.mu.resize(N);
    d.kLin.resize(N);
    d.area.resize(N);
    d.shape.resize(size_t(N) * Design::kGrid);
    for (int k = 0; k < N; ++k) {
        const Radial &s = *shapes[size_t(modeOf[size_t(k)])];
        const double ang = d.m[k] == 0 ? 2.0 * M_PI : M_PI; // ∫cos²(mθ)dθ
        double r2 = 0, r1 = 0;
        for (int i = 0; i < Q; ++i) {
            r2 += s.R[i] * s.R[i] * rho[i] * dr;
            r1 += s.R[i] * rho[i] * dr;
        }
        d.mu[k] = float(ang * r2);
        // ω² = λ⁴·D/(ρh·a⁴), so the stiffness over E·h⁵/a², per thickness
        // of swing squared: μ·λ⁴/(12(1−ν²)).
        d.kLin[k] = float(d.mu[k] * double(d.lambda2[k]) * d.lambda2[k] / (12.0 * (1.0 - nu * nu)));
        d.area[k] = d.m[k] == 0 ? float(2.0 * M_PI * r1) : 0.0f;
        for (int g = 0; g < Design::kGrid; ++g) {
            const double r = double(g) / (Design::kGrid - 1);
            const double fi = std::clamp(r * Q - 0.5, 0.0, double(Q - 1));
            const int i0 = std::min(int(fi), Q - 2);
            const double t = fi - i0;
            d.shape[size_t(k) * Design::kGrid + g] = float(s.R[i0] + t * (s.R[i0 + 1] - s.R[i0]));
        }
        if (d.m[k] == 0)
            d.axis.push_back(k);
    }
    // The channels: 0 the rim's uniform tension (Berger), then each Airy mode.
    d.R = 1 + A;
    d.kap.assign(d.R, 0.0f);
    d.G.assign(size_t(d.R) * N, 0.0f);
    // Berger: ε = mean over the face of ½|∇w|² + ∇w0·∇w, its energy
    // Eh·πa²/(4(1−ν))·ε² for an immovable rim, of which the rim holds `rim`.
    d.kap[0] = float(sp.rim * M_PI / (2.0 * (1.0 - nu)));
    // Airy: ζ⁴·∫Ψ²dA·η = −Eh·(½ΣH x x + ΣG x), energy ½·Eh·a²/(ζ⁴N)·ε²,
    // over E·h⁵/a² per (h/a)⁴ of strain: 1/(ζ⁴N).
    std::vector<double> airyN(A);
    for (int r = 0; r < A; ++r) {
        double s = 0;
        for (int i = 0; i < Q; ++i)
            s += airyR(r).R[i] * airyR(r).R[i] * rho[i] * dr;
        airyN[size_t(r)] = 2.0 * M_PI * s;
        const double z = zeta[size_t(r)];
        d.kap[1 + r] = float(1.0 / (z * z * z * z * airyN[size_t(r)]));
    }
    // The linear (curvature) strains. The axisymmetric rise reaches the
    // axisymmetric modes; the irregular texture of a hand-hammered sheet,
    // a little of every angular order p (random in size and phase, as no
    // two sheets are hammered alike), reaches the modes of its own order:
    // where the sheet's slope turns, its stretching drives them.
    auto lsum = [](double F, double F1, double F2, double R, double R1, double R2, double r, double mm) {
        // ∫L(F·cos, R·cos)dθ / (π·cos(α−β)), for m > 0.
        const double ta = F1 / r - F / (r * r), tb = R1 / r - R / (r * r);
        return F2 * (R1 / r - mm * R / (r * r)) + R2 * (F1 / r - mm * F / (r * r)) - 2.0 * mm * ta * tb;
    };
    std::vector<double> texA(size_t(sp.maxM + 1), 0.0), texPh(size_t(sp.maxM + 1), 0.0);
    {
        Rng tr(0x7e47u + uint32_t(sp.kind) * 31u);
        for (int p = 1; p <= sp.maxM; ++p) {
            // (A Gaussian, near enough: the sum of four uniforms.)
            const double g = 0.5 * double(tr.bipolar() + tr.bipolar() + tr.bipolar() + tr.bipolar());
            texA[size_t(p)] = sp.texture * g / std::pow(double(p), double(sp.textureSlope));
            texPh[size_t(p)] = 2.0 * M_PI * double(tr.unit());
        }
    }
    for (int k = 0; k < N; ++k) {
        const Radial &s = *shapes[size_t(modeOf[size_t(k)])];
        const int m = d.m[k];
        if (m == 0) {
            double gb = 0;
            for (int i = 0; i < Q; ++i)
                gb += w1[i] * s.R1[i] * rho[i] * dr;
            d.G[size_t(k)] = float(2.0 * gb); // (2π/π)·∫w0'R'ρdρ
            for (int r = 0; r < A; ++r) {
                double g = 0;
                for (int i = 0; i < Q; ++i) {
                    // L(w0, φ) = w0''·R'/ρ + (w0'/ρ)·R'' for m = 0.
                    const double L = w2[i] * s.R1[i] / rho[i] + w1[i] / rho[i] * s.R2[i];
                    g += airyR(r).R[i] * L * rho[i] * dr;
                }
                d.G[size_t(1 + r) * N + k] = float(2.0 * M_PI * g);
            }
            continue;
        }
        if (sp.texture <= 0 || m > sp.maxM)
            continue;
        // The texture's order-m part, T(ρ) = ρ^q·(1 − ρ²) at its peak 1.
        const double q = std::min(m, 2);
        double top = 0;
        for (int i = 0; i < Q; ++i)
            top = std::max(top, std::pow(rho[i], q) * (1.0 - rho[i] * rho[i]));
        const double beta = d.orient[k] ? -0.5 * M_PI : 0.0;
        const double amp = texA[size_t(m)] * std::cos(texPh[size_t(m)] - beta) / top;
        const double mm = double(m * m);
        double gb = 0;
        std::vector<double> ga(size_t(A), 0.0);
        for (int i = 0; i < Q; ++i) {
            const double r = rho[i];
            const double F = std::pow(r, q) - std::pow(r, q + 2.0);
            const double F1 = q * std::pow(r, q - 1.0) - (q + 2.0) * std::pow(r, q + 1.0);
            const double F2 = q * (q - 1.0) * std::pow(r, q - 2.0) - (q + 2.0) * (q + 1.0) * std::pow(r, q);
            gb += (F1 * s.R1[i] + mm * F * s.R[i] / (r * r)) * r * dr;
            const double L = M_PI * lsum(F, F1, F2, s.R[i], s.R1[i], s.R2[i], r, mm);
            for (int rr = 0; rr < A; ++rr)
                ga[size_t(rr)] += airyR(rr).R[i] * L * r * dr;
        }
        d.G[size_t(k)] = float(amp * gb); // (π/π)·∫(T'R' + m²TR/ρ²)ρdρ
        for (int rr = 0; rr < A; ++rr)
            d.G[size_t(1 + rr) * N + k] = float(amp * ga[size_t(rr)]);
    }
    // The quadratic strains, between modes of the same symmetry (the same
    // number of nodal diameters, the same orientation): the axisymmetric
    // stress sees nothing else.
    struct PairH {
        int k, l;
        std::vector<double> h; // per channel, ∂²ε/∂x_k∂x_l
    };
    std::vector<PairH> pairs;
    for (int k = 0; k < N; ++k)
        for (int l = k; l < N; ++l) {
            if (d.m[k] != d.m[l] || d.orient[k] != d.orient[l])
                continue;
            const int m = d.m[k];
            const Radial &a = *shapes[size_t(modeOf[size_t(k)])], &b = *shapes[size_t(modeOf[size_t(l)])];
            PairH p{k, l, std::vector<double>(size_t(d.R), 0.0)};
            const double ang = m == 0 ? 2.0 * M_PI : M_PI;
            double hb = 0;
            std::vector<double> ha(size_t(A), 0.0);
            const double mm = double(m * m);
            for (int i = 0; i < Q; ++i) {
                const double r = rho[i];
                hb += (a.R1[i] * b.R1[i] + mm * a.R[i] * b.R[i] / (r * r)) * r * dr;
                // L(φk, φl), averaged round: cos² and sin² each half (m > 0).
                const double ca = a.R1[i] / r - mm * a.R[i] / (r * r), cb = b.R1[i] / r - mm * b.R[i] / (r * r);
                const double ta = a.R1[i] / r - a.R[i] / (r * r), tb = b.R1[i] / r - b.R[i] / (r * r);
                const double Lc = a.R2[i] * cb + b.R2[i] * ca;
                const double L = m == 0 ? 2.0 * M_PI * Lc : M_PI * Lc - 2.0 * mm * M_PI * ta * tb;
                for (int rr = 0; rr < A; ++rr)
                    ha[size_t(rr)] += airyR(rr).R[i] * L * r * dr;
            }
            // (Berger's mean: over the face's area π.)
            p.h[0] = ang * hb / M_PI;
            for (int rr = 0; rr < A; ++rr)
                p.h[size_t(1 + rr)] = ha[size_t(rr)];
            pairs.push_back(p);
        }
    d.P = int(pairs.size());
    d.He.assign(size_t(d.R) * d.P, 0.0f);
    d.Hf.assign(size_t(d.R) * d.P, 0.0f);
    d.pk.resize(d.P);
    d.pl.resize(d.P);
    for (int p = 0; p < d.P; ++p) {
        d.pk[p] = uint16_t(pairs[size_t(p)].k);
        d.pl[p] = uint16_t(pairs[size_t(p)].l);
        const bool diag = pairs[size_t(p)].k == pairs[size_t(p)].l;
        for (int r = 0; r < d.R; ++r) {
            const double h = pairs[size_t(p)].h[size_t(r)];
            d.He[size_t(r) * d.P + p] = float(diag ? 0.5 * h : h);
            d.Hf[size_t(r) * d.P + p] = float(h);
        }
    }
    // Patches: the sheet cut into Pt pieces, rings of them, each with the
    // uniform isotropic tension its own mean stretch makes (Marguerre's
    // shallow-shell strain ½|∇w|² + ∇w0·∇w, taken where the patch is): a
    // piece can't give its stretch to its neighbours, so stress is resolved
    // patch by patch rather than on a few smooth in-plane modes, and every
    // mode stretches every patch it slopes in. Energy ½κ·ε² each, κ the
    // patch's share of the rim's.
    if (sp.patches > 0) {
        const int rings = 4;
        static const double rr[rings] = {0.18, 0.42, 0.64, 0.84};
        static const double share[rings] = {0.19, 0.25, 0.25, 0.31}; // of the points
        std::vector<double> pr, pt;
        Rng jr(0x9a7cu);
        for (int g = 0; g < rings; ++g) {
            const int cnt = std::max(1, int(std::lround(share[g] * sp.patches)));
            for (int j = 0; j < cnt; ++j) {
                pr.push_back(rr[g] + 0.03 * double(jr.bipolar()));
                pt.push_back(2.0 * M_PI * (j + 0.5 + 0.3 * double(jr.bipolar())) / cnt + 0.4 * g);
            }
        }
        d.Pt = std::min(int(pr.size()), kPtPad);
        const int Pt = d.Pt;
        d.px.assign(size_t(Pt) * N, 0.0f);
        d.py.assign(size_t(Pt) * N, 0.0f);
        d.w0x.assign(size_t(Pt), 0.0f);
        d.w0y.assign(size_t(Pt), 0.0f);
        d.kapPt.assign(size_t(Pt), 0.0f);
        auto at = [&](const std::vector<double> &v, double r) {
            const double fi = std::clamp(r * Q - 0.5, 0.0, double(Q - 1));
            const int i0 = std::min(int(fi), Q - 2);
            const double t = fi - i0;
            return v[size_t(i0)] + t * (v[size_t(i0 + 1)] - v[size_t(i0)]);
        };
        for (int p = 0; p < Pt; ++p) {
            const double r = pr[size_t(p)], th = pt[size_t(p)], c = std::cos(th), sn = std::sin(th);
            // Each ring's patches share its annulus.
            int g = 0;
            while (g < rings - 1 && std::fabs(r - rr[g]) > std::fabs(r - rr[g + 1]))
                ++g;
            const double inner = g == 0 ? 0.0 : 0.5 * (rr[g - 1] + rr[g]), outer = g == rings - 1 ? 1.0 : 0.5 * (rr[g] + rr[g + 1]);
            int cnt = 0;
            for (int q = 0; q < Pt; ++q) {
                int gq = 0;
                while (gq < rings - 1 && std::fabs(pr[size_t(q)] - rr[gq]) > std::fabs(pr[size_t(q)] - rr[gq + 1]))
                    ++gq;
                cnt += gq == g;
            }
            const double areaP = M_PI * (outer * outer - inner * inner) / cnt;
            d.kapPt[size_t(p)] = float(sp.rim * areaP / (2.0 * (1.0 - nu)));
            for (int k = 0; k < N; ++k) {
                const Radial &s = *shapes[size_t(modeOf[size_t(k)])];
                const int m = d.m[k];
                const double R = at(s.R, r), R1 = at(s.R1, r);
                const double ang = d.orient[k] ? std::sin(m * th) : std::cos(m * th);
                const double dang = d.orient[k] ? m * std::cos(m * th) : -m * std::sin(m * th);
                const double gr = R1 * ang, gt = R / r * dang;
                d.px[size_t(p) * N + k] = float(gr * c - gt * sn);
                d.py[size_t(p) * N + k] = float(gr * sn + gt * c);
            }
            // The rise's slope there: the dome's and the texture's (and,
            // apart, the texture's alone: see the sheet's modes, below).
            const double domeSlope = at(w1, r);
            double gr = 0, gt = 0;
            for (int q = 1; q <= sp.maxM && sp.texture > 0; ++q) {
                const double e = std::min(q, 2);
                double top = 0;
                for (int i = 0; i < Q; ++i)
                    top = std::max(top, std::pow(rho[i], e) * (1.0 - rho[i] * rho[i]));
                const double F = (std::pow(r, e) - std::pow(r, e + 2.0)) / top;
                const double F1 = (e * std::pow(r, e - 1.0) - (e + 2.0) * std::pow(r, e + 1.0)) / top;
                gr += texA[size_t(q)] * F1 * std::cos(q * th + texPh[size_t(q)]);
                gt += -texA[size_t(q)] * q * F / r * std::sin(q * th + texPh[size_t(q)]);
            }
            d.wtx.push_back(float(gr * c - gt * sn));
            d.wty.push_back(float(gr * sn + gt * c));
            // (The smooth central dome stiffens the sheet, below, but its
            // gentle curvature is not where the stretching passes energy
            // on: Legge & Fletcher put that at the sharp turns of shape, the
            // rings of hammered bumps and the hammer marks, which the
            // texture is. So the patches see the texture's slope only.)
            (void)domeSlope;
            d.w0x[size_t(p)] = float(gr * c - gt * sn);
            d.w0y[size_t(p)] = float(gr * sn + gt * c);
        }
        // The dome's own stiffening, smooth and exact, from the channels:
        // Σκ·G·Gᵀ among the axisymmetric modes. (Sampled at the few patches
        // in the middle, it would be lumpy, and mix the struck fundamental
        // into every high mode.)
        {
            const int na = int(d.axis.size());
            d.Kax.assign(size_t(na) * na, 0.0f);
            for (int i = 0; i < na; ++i)
                for (int j = 0; j < na; ++j) {
                    double v = 0;
                    for (int r = 0; r < d.R; ++r)
                        v += double(d.kap[size_t(r)]) * d.G[size_t(r) * N + d.axis[size_t(i)]] * d.G[size_t(r) * N + d.axis[size_t(j)]];
                    d.Kax[size_t(i) * na + j] = float(v);
                }
        }
        // No channels: the patches are the stress, the whole rise's.
        d.R = 0;
        d.P = 0;
        d.kap.clear();
        d.G.clear();
        d.He.clear();
        d.Hf.clear();
        d.pk.clear();
        d.pl.clear();
    }
    d.memScale = float(12.0 * (1.0 - nu * nu));
    // The small-amplitude frequencies as built: the plate's, and the
    // axisymmetric ones stiffened by the shell's membrane (Σκ·G·Gᵀ),
    // which couples them. Their eigenvalues, by Jacobi.
    d.ratio.resize(N);
    for (int k = 0; k < N; ++k)
        d.ratio[k] = d.lambda2[k];
    {
        const int na = int(d.axis.size());
        std::vector<double> K(size_t(na) * na);
        for (int i = 0; i < na; ++i)
            for (int j = 0; j < na; ++j) {
                const int a = d.axis[size_t(i)], b = d.axis[size_t(j)];
                double s = 0;
                for (int r = 0; r < d.R; ++r)
                    s += double(d.kap[r]) * d.G[size_t(r) * N + a] * d.G[size_t(r) * N + b];
                for (int q = 0; q < d.Pt; ++q) {
                    const size_t o = size_t(q) * N;
                    s += double(d.kapPt[size_t(q)]) * (d.w0x[size_t(q)] * d.px[o + a] + d.w0y[size_t(q)] * d.py[o + a]) *
                         (d.w0x[size_t(q)] * d.px[o + b] + d.w0y[size_t(q)] * d.py[o + b]);
                }
                s *= d.memScale; // in the plate stiffness's units
                if (i == j)
                    s += d.kLin[a] * 12.0 * (1.0 - nu * nu);
                K[size_t(i) * na + j] = s / std::sqrt(double(d.mu[a]) * d.mu[b]);
            }
        std::vector<double> ev = eigenvalues(K, na);
        for (auto &e : ev)
            e = std::sqrt(std::max(e, 0.0));
        // (ω/Ω = λ², so the eigenvalues of K/μ are λ⁴.)
        for (int i = 0; i < na; ++i)
            d.ratio[size_t(d.axis[size_t(i)])] = float(ev[size_t(i)]);
    }
    // A tam-tam's sheet, with its dome and its texture, is not a plate:
    // the stretching's linear part (κ·(∇w0·∇w)² over the patches) stiffens
    // and couples every mode. Its own modes are worked out exactly here,
    // and the gong rings in them, so that whatever the stretching does on
    // top (its nonlinear part, all the patches carry at run time) never
    // retunes it. Sorted by frequency; the thud is the one that is most the
    // plate's axisymmetric fundamental.
    if (d.Pt > 0) {
        std::vector<double> K(size_t(N) * N, 0.0), V;
        for (int i = 0; i < N; ++i)
            for (int j = i; j < N; ++j) {
                double v = 0;
                for (int q = 0; q < d.Pt; ++q) {
                    const size_t o = size_t(q) * N;
                    v += double(d.kapPt[size_t(q)]) * (d.wtx[size_t(q)] * d.px[o + i] + d.wty[size_t(q)] * d.py[o + i]) *
                         (d.wtx[size_t(q)] * d.px[o + j] + d.wty[size_t(q)] * d.py[o + j]);
                }
                K[size_t(i) * N + j] = K[size_t(j) * N + i] = v;
            }
        {
            const int na = int(d.axis.size());
            for (int a = 0; a < na; ++a)
                for (int b = 0; b < na; ++b)
                    K[size_t(d.axis[size_t(a)]) * N + d.axis[size_t(b)]] += d.Kax[size_t(a) * na + b];
        }
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                double v = K[size_t(i) * N + j] * d.memScale;
                if (i == j)
                    v += double(d.mu[i]) * d.lambda2[i] * d.lambda2[i];
                K[size_t(i) * N + j] = v / std::sqrt(double(d.mu[i]) * d.mu[j]);
            }
        const auto ev = eigen(K, N, &V);
        std::vector<int> order(static_cast<size_t>(N), 0);
        for (int j = 0; j < N; ++j)
            order[size_t(j)] = j;
        std::sort(order.begin(), order.end(), [&](int x, int y) { return ev[size_t(x)] < ev[size_t(y)]; });
        d.eigen = true;
        d.eLam2.resize(size_t(N));
        d.eDom.resize(size_t(N));
        d.T.assign(size_t(N) * N, 0.0f);
        const int a0 = d.axis[0];
        int thud = 0;
        for (int j = 0; j < N; ++j) {
            const int e = order[size_t(j)];
            d.eLam2[size_t(j)] = float(std::sqrt(std::max(ev[size_t(e)], 0.0)));
            int dom = 0;
            for (int k = 0; k < N; ++k) {
                d.T[size_t(k) * N + j] = float(V[size_t(k) * N + e] / std::sqrt(double(d.mu[k])));
                if (std::fabs(V[size_t(k) * N + e]) > std::fabs(V[size_t(dom) * N + e]))
                    dom = k;
            }
            d.eDom[size_t(j)] = dom;
            if (std::fabs(V[size_t(a0) * N + e]) > std::fabs(V[size_t(a0) * N + order[size_t(thud)]]))
                thud = j;
        }
        d.thud = thud;
        d.ratio[size_t(a0)] = d.eLam2[size_t(thud)];
        // The patches' slopes, by the sheet's own modes.
        std::vector<float> px(size_t(d.Pt) * N, 0.0f), py(size_t(d.Pt) * N, 0.0f);
        for (int q = 0; q < d.Pt; ++q)
            for (int j = 0; j < N; ++j) {
                double sx = 0, sy = 0;
                for (int k = 0; k < N; ++k) {
                    sx += double(d.px[size_t(q) * N + k]) * d.T[size_t(k) * N + j];
                    sy += double(d.py[size_t(q) * N + k]) * d.T[size_t(k) * N + j];
                }
                px[size_t(q) * N + j] = float(sx);
                py[size_t(q) * N + j] = float(sy);
            }
        d.px = px;
        d.py = py;
        d.pxT.assign(size_t(N) * kPtPad, 0.0f);
        d.pyT.assign(size_t(N) * kPtPad, 0.0f);
        for (int q = 0; q < d.Pt; ++q)
            for (int j = 0; j < N; ++j) {
                d.pxT[size_t(j) * kPtPad + q] = d.px[size_t(q) * N + j];
                d.pyT[size_t(j) * kPtPad + q] = d.py[size_t(q) * N + j];
            }
    }
    // The pitch is the axisymmetric fundamental's, the mode a strike in
    // the middle sounds (on a tam-tam, its thud).
    const float f1 = d.ratio[size_t(d.axis[0])];
    d.lam1 = f1;
    for (auto &r : d.ratio)
        r /= f1;
    for (auto &r : d.eLam2)
        d.eRatio.push_back(r / f1);
    // A bossed gong's spectrum is the smith's, not a plate's: the heavy
    // boss and the deep rim pull its modes together (Krueger et al.'s
    // Balinese gong: 1, 1.490 and 1.648, 1.964, 2.433 and 2.482, 2.591
    // and 2.632, 3.822). Measured ones where measured; the rest the
    // plate's, compressed as the measured ones are (λ² to the 0.68).
    if (sp.kind == Bossed) {
        d.tuned = true;
        for (int k = 0; k < N; ++k) {
            const int m = d.m[k], nn = d.nr[k], o = d.orient[k];
            float t = std::pow(d.lambda2[k] / d.lambda2[size_t(d.axis[0])], 0.68f);
            if (m == 0 && nn == 1)
                t = 1.0f;
            else if (m == 1 && nn == 1)
                t = o ? 1.648f : 1.490f;
            else if (m == 0 && nn == 2)
                t = 1.964f; // retuned per note for its ombak (Gong::start)
            else if (m == 2 && nn == 1)
                t = o ? 2.482f : 2.433f;
            else if (m == 3 && nn == 1)
                t = o ? 2.632f : 2.591f;
            else if (m == 1 && nn == 2)
                t = o ? 3.86f : 3.822f;
            d.ratio[k] = t;
        }
    }
    return d;
}

// The designs, built once per kind (an opera gong's for each of a few
// rises, the nearest used).
inline const Design &design(Kind k, float rise = 0) {
    static std::deque<Design> cache[kKinds];
    auto &c = cache[std::clamp(int(k), 0, kKinds - 1)];
    const Spec sp = specOf(k);
    const float want = k == Opera ? std::round(rise * 8.0f) / 8.0f : sp.rise;
    for (const auto &d : c)
        if (std::fabs(d.riseKey - want) < 1e-4f)
            return d;
    c.push_back(buildDesign(sp, want));
    return c.back();
}

// Builds every design a lead could ask for, so none is built on the
// audio thread.
inline void prepare() {
    design(Bossed);
    design(TamTam);
    for (int i = 0; i <= 12; ++i)
        design(Opera, float(i) / 8.0f);
}

// --- one gong ----------------------------------------------------------------------------------------------

class Gong {
public:
    static constexpr int kMaxModes = 200;
    static constexpr int kMaxPairs = 768;
    static constexpr int kMaxR = 8;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        silence();
    }

    // A new gong of this kind, tuned so its fundamental sounds at hz (a
    // tam-tam's: its deep thud). The old one, if it rang, is gone.
    void start(Kind kind, float hz) {
        const Spec sp = specOf(kind);
        const float rise = kind == Opera ? operaRise(std::isfinite(hz) ? hz : 220.0f) : sp.rise;
        m_d = &design(kind, rise);
        m_kind = kind;
        m_spec = sp;
        m_hz0 = std::isfinite(hz) ? clampf(hz, 20.0f, 0.2f * m_rate) : 220.0f;
        m_scale = 1.0f;
        const Design &d = *m_d;
        // The sheet: as thick as its kind, as wide as its pitch asks
        // (ω₁ = λ₁²·√(D/ρh)/a²).
        const float h = sp.thick;
        const float D = Bronze::kE * h * h * h / (12.0f * (1.0f - Bronze::kNu * Bronze::kNu));
        const float plate = std::sqrt(D / (Bronze::kRho * h));
        m_omega = kTwoPi * m_hz0 / d.lam1;
        m_radius = std::sqrt(plate / m_omega);
        m_mass0 = Bronze::kRho * h * m_radius * m_radius;
        // The beater for this gong.
        m_malletMass = sp.malletMass * m_mass0 * d.mu[size_t(d.axis[0])];
        // (The small domed opera gongs take a lighter stick: Fletcher's
        // light soft stick never reaches the reversing glide.)
        m_rise = rise;
        m_speed = sp.speed * kTwoPi * m_hz0 * h * (kind == Opera ? 1.0f - 0.5f * rise / 1.43f : 1.0f);
        m_felt = feltFor(sp.felt, m_malletMass, sp.contact / m_hz0, m_speed);
        // A bossed gong's ombak: its second axisymmetric mode a few hertz
        // under its fundamental's octave, as the smith hammers it, so the
        // octave the boss's stretching sounds beats against it (Krueger et
        // al.: 2.25–2.5 Hz on a 62 Hz gong; a smaller gong's quicker).
        for (int k = 0; k < d.n; ++k)
            m_ratio[k] = d.ratio[k];
        if (kind == Bossed)
            for (int k = 0; k < d.n; ++k)
                if (d.m[k] == 0 && d.nr[k] == 2)
                    m_ratio[k] = 2.0f - ombakHz(m_hz0) / m_hz0;
        // A smith's tuning is what the gong sounds, its shell and all: the
        // own stiffness of each mode is what is left once the membrane has
        // stiffened (and coupled) the axisymmetric ones, found so their
        // eigenfrequencies land on the targets.
        if (d.tuned) {
            for (int k = 0; k < d.n; ++k) {
                const double t = double(m_ratio[k]) * d.lam1;
                m_own[k] = float(t * t);
            }
            const int na = int(d.axis.size());
            std::vector<double> M(static_cast<size_t>(na * na), 0.0), want(static_cast<size_t>(na), 0.0);
            for (int i = 0; i < na; ++i) {
                const int a = d.axis[size_t(i)];
                want[size_t(i)] = double(m_own[a]);
                for (int j = 0; j < na; ++j) {
                    const int b = d.axis[size_t(j)];
                    double v = 0;
                    for (int c = 0; c < d.R; ++c)
                        v += double(d.kap[size_t(c)]) * d.G[size_t(c) * d.n + a] * d.G[size_t(c) * d.n + b];
                    M[size_t(i) * na + j] = double(d.memScale) * v / std::sqrt(double(d.mu[a]) * d.mu[b]);
                }
            }
            std::vector<double> own(static_cast<size_t>(na), 0.0);
            for (int i = 0; i < na; ++i)
                own[size_t(i)] = want[size_t(i)] - M[size_t(i) * na + i];
            std::vector<int> order(static_cast<size_t>(na), 0);
            for (int i = 0; i < na; ++i)
                order[size_t(i)] = i;
            std::sort(order.begin(), order.end(), [&](int x, int y) { return want[size_t(x)] < want[size_t(y)]; });
            for (int it = 0; it < 12; ++it) {
                std::vector<double> A(M);
                for (int i = 0; i < na; ++i)
                    A[size_t(i) * na + i] += own[size_t(i)];
                const auto ev = eigenvalues(A, na);
                for (int r = 0; r < na; ++r) {
                    const int i = order[size_t(r)];
                    own[size_t(i)] += want[size_t(i)] - ev[size_t(r)];
                }
            }
            for (int i = 0; i < na; ++i)
                m_own[d.axis[size_t(i)]] = float(std::max(own[size_t(i)], 1e-6));
        }
        silence();
        retune();
        if (d.eigen) {
            radiate();
            retune(); // (its energy scale wants the weights)
        }
    }

    // Bends (a lead's glide, its bend wheel): the whole gong in time, its
    // stretching as stiff in proportion.
    void setPitch(float hz) {
        if (!std::isfinite(hz))
            return;
        const float s = clampf(hz, 20.0f, 0.2f * m_rate) / m_hz0;
        if (std::fabs(s - m_scale) < 1e-5f * m_scale)
            return;
        m_scale = s;
        retune();
    }

    // A mallet lands at `where` (0 the centre, 1 the rim) and angle, at
    // strength vel 0..1; delay: samples into the next block.
    // soft 0..1: a fatter, softer beater (its felt gives more, its contact
    // is longer and duller).
    void strike(float vel, float where, float angle, int delay = 0, float soft = 0) {
        if (!m_d)
            return;
        Contact *c = &m_contacts[0];
        for (auto &k : m_contacts)
            if (!k.on) {
                c = &k;
                break;
            }
        *c = Contact{};
        c->on = true;
        c->delay = std::max(0, delay);
        vel = std::isfinite(vel) ? clampf(vel, 0.0f, 1.0f) : 0.0f;
        where = std::isfinite(where) ? where : 0.0f;
        angle = std::isfinite(angle) ? angle : 0.0f;
        soft = std::isfinite(soft) ? soft : 0.0f;
        c->v = m_speed * (0.04f + 0.96f * std::pow(vel, m_spec.velExp));
        c->mass = m_malletMass;
        c->p = m_spec.felt;
        c->K = m_felt * std::exp2(-3.5f * clampf(soft, 0.0f, 1.0f));
        // A gentle stroke stays on longer (T ∝ v^((1−p)/(p+1)), and
        // K^(−1/(p+1))); a player's stick is never so soft that it smothers
        // the note it strikes.
        if (m_spec.contactMax > 0) {
            const float p = m_spec.felt;
            const float T = m_spec.contact * std::pow(std::max(c->v, 1e-6f) / m_speed, (1.0f - p) / (p + 1.0f)) *
                            std::pow(c->K / m_felt, -1.0f / (p + 1.0f));
            if (T > m_spec.contactMax)
                c->K *= std::pow(T / m_spec.contactMax, p + 1.0f);
        }
        const float rho = clampf(where, 0.0f, 0.95f);
        const Design &d = *m_d;
        // The beater's padded head lands on a patch of the sheet, not a
        // point: what it drives each mode by is the mode's shape averaged
        // over that patch (its middle and a ring round it), so the modes
        // whose lobes are smaller than the head take little.
        // (The small opera gongs' face is two thirds the big ones' across,
        // under the same stick.)
        const float head = m_spec.head * (m_kind == Opera ? 1.0f + 0.5f * m_rise / 1.43f : 1.0f);
        const float cx = rho * std::cos(angle), cy = rho * std::sin(angle), hr = 0.6f * head;
        for (int k = 0; k < d.n; ++k)
            c->phi[k] = 0;
        for (int p = 0; p < 7; ++p) {
            const float px = cx + (p ? hr * std::cos(1.047f * float(p)) : 0.0f);
            const float py = cy + (p ? hr * std::sin(1.047f * float(p)) : 0.0f);
            const float pr = std::min(std::sqrt(px * px + py * py), 0.98f), pa = std::atan2(py, px);
            const float w = p ? 1.0f / 7.0f : 1.0f / 7.0f;
            for (int k = 0; k < d.n; ++k) {
                const float r = shapeAt(d, k, pr);
                const float ang = d.m[k] == 0 ? 1.0f : (d.orient[k] ? std::sin(float(d.m[k]) * pa) : std::cos(float(d.m[k]) * pa));
                c->phi[k] += w * r * ang;
            }
        }
        // (A tam-tam's sheet modes: the plate's shapes, mixed.)
        if (d.eigen) {
            float plate[kMaxModes];
            std::copy(c->phi, c->phi + d.n, plate);
            for (int j = 0; j < d.n; ++j) {
                float v = 0;
                for (int k = 0; k < d.n; ++k)
                    v += plate[k] * d.T[size_t(k) * d.n + j];
                c->phi[j] = v;
            }
        }
        // The felt's stiffness is held under what the step can carry: the
        // sheet's give at the patch in one step and the beater's together
        // can't take more than half the spring a step needs to overshoot.
        // (A felt squeezed that far is all but solid; the step decides.)
        {
            double give = double(c->mass) > 0 ? double(m_dt) * m_dt / c->mass : 0.0;
            for (int k = 0; k < d.n; ++k)
                give += double(m_spec.thick) * c->phi[k] * c->phi[k] * m_cf[k];
            c->kmax = float(2.0 / std::max(give, 1e-30));
            c->dmax = std::pow(c->kmax / (c->p * c->K), 1.0f / (c->p - 1.0f));
        }
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            retune();
        }
        m_sleeping = false;
        m_touching = true;
        m_quietSheet = false;
        m_sheet = 1.0f;
        m_quiet = 0;
    }

    // A hand on it: everything dies within about `seconds`, until it is
    // struck again.
    void mute(float seconds) {
        if (m_sleeping || !m_d)
            return;
        m_muteD = std::exp(-2.0f / (std::max(seconds, 0.001f) * 0.25f * m_rate));
        for (int k = 0; k < m_d->n; ++k)
            m_dd[k] = std::min(m_dd[k], m_muteD);
    }

    void silence() {
        for (int k = 0; k < kMaxModes; ++k)
            m_x[k] = m_u[k] = m_f[k] = 0;
        for (auto &c : m_contacts)
            c.on = false;
        m_ql = m_qr = 0;
        m_sleeping = true;
        m_energy = 0;
        m_quiet = 0;
        if (m_muteD < 1.0f) {
            m_muteD = 1.0f;
            if (m_d)
                retune();
        }
    }

    bool active() const { return !m_sleeping; }
    // Loudness, roughly 0..1 for a hard strike, for drawing and for when to stop.
    float level() const { return std::sqrt(std::max(0.0f, m_energy) / m_refEnergy); }

    // Adds the gong's sound into l and r.
    void render(float *l, float *r, int n, float gain) {
        if (m_sleeping || !m_d)
            return;
        const Design &d = *m_d;
        const int N = d.n, P = d.P, R = d.R;
        float *x = m_x, *u = m_u, *f = m_f;
        const float *He = d.He.data(), *Hf = d.Hf.data(), *G = d.G.data();
        const uint16_t *pk = d.pk.data(), *pl = d.pl.data();
        const float pGain = gain * m_pGain;
        // A tam-tam rings in its sheet's own modes, whose linear part is
        // in each mode's own stiffness: what the patches add is the rest of
        // the stretching, κ·(L·Q + ½Q²) for each, L = ∇w0·∇w its linear
        // strain and Q = ½|∇w|². Rung down to where that is a whisper (46 dB
        // under a hard strike, by the swing of everything but the thud)
        // and untouched, it is left out: what is left is linear, and costs
        // a fraction. It fades out over a third of a second, slowly against
        // every mode's period, and stays out until the next strike (switched
        // to and fro, it would itself pump the modes).
        if (d.eigen) {
            float e = 0, all = 0;
            for (int k = 0; k < m_nAct; ++k) {
                const float ek = m_cc[k] * x[k] * x[k] + u[k] * u[k];
                all += ek;
                if (k != d.thud)
                    e += ek;
            }
            if (!m_touching && !m_quietSheet)
                m_quietSheet = e < m_sheetQuiet;
            m_mech = all;
            // A limiter, as a guard: hammered past what a few hard strokes
            // give it, the sheet's swing is scaled back within a block or
            // two. (Strokes landing on strokes would otherwise swing it
            // further than the explicit step can follow; a single stroke
            // never reaches it.)
            if (all > m_yield && !m_touching) {
                const float keep = std::sqrt(std::sqrt(m_yield / all));
                for (int k = 0; k < m_nAct; ++k) {
                    x[k] *= keep;
                    u[k] *= keep;
                }
            }
        }
        const float sheet0 = m_sheet;
        if (m_quietSheet)
            m_sheet = std::max(0.0f, m_sheet - float(n) / (0.3f * m_rate));
        const int Pt = sheet0 > 0 ? d.Pt : 0;
        const float sheetStep = (m_sheet - sheet0) / float(std::max(n, 1));
        float sheet = sheet0;
        const float *PX = d.px.data(), *PY = d.py.data(), *TX = d.pxT.data(), *TY = d.pyT.data();
        const int NA = d.eigen ? m_nAct : N;
        for (int i = 0; i < n; ++i) {
            if (d.eigen) {
                for (int k = 0; k < NA; ++k)
                    f[k] = 0;
                if (Pt > 0) {
                    // Every patch's slope at once (the modes outside, the
                    // patches inside, so each patch's sum runs on its own).
                    float gx[kPtPad], gy[kPtPad], sx[kPtPad], sy[kPtPad];
                    for (int q = 0; q < kPtPad; ++q)
                        gx[q] = gy[q] = 0;
                    for (int k = 0; k < NA; ++k) {
                        const float xk = x[k];
                        const float *tx = TX + size_t(k) * kPtPad, *ty = TY + size_t(k) * kPtPad;
                        for (int q = 0; q < kPtPad; ++q) {
                            gx[q] += tx[q] * xk;
                            gy[q] += ty[q] * xk;
                        }
                    }
                    for (int q = 0; q < Pt; ++q) {
                        const float wx = m_w0x[q], wy = m_w0y[q];
                        const float L = wx * gx[q] + wy * gy[q], Q = 0.5f * (gx[q] * gx[q] + gy[q] * gy[q]);
                        const float kq = sheet * m_kapPt[q];
                        // (A numerical guard, not bronze: a patch's stretch
                        // beyond the linear, Q, is held under what would
                        // stiffen some mode past what the explicit step
                        // holds. The linear part is exact in the sheet's
                        // own modes and is never touched. Played strokes
                        // don't reach it.)
                        const float st = L + std::min(Q, m_strainMax[q]);
                        sx[q] = kq * (st * gx[q] + (st - L) * wx);
                        sy[q] = kq * (st * gy[q] + (st - L) * wy);
                    }
                    for (int q = 0; q + 1 < Pt; q += 2) {
                        const float *a0 = PX + size_t(q) * N, *b0 = PY + size_t(q) * N;
                        const float *a1 = a0 + N, *b1 = b0 + N;
                        const float s0 = sx[q], t0 = sy[q], s1 = sx[q + 1], t1 = sy[q + 1];
                        for (int k = 0; k < NA; ++k)
                            f[k] -= s0 * a0[k] + t0 * b0[k] + s1 * a1[k] + t1 * b1[k];
                    }
                    if (Pt & 1) {
                        const int q = Pt - 1;
                        const float *a0 = PX + size_t(q) * N, *b0 = PY + size_t(q) * N;
                        for (int k = 0; k < NA; ++k)
                            f[k] -= sx[q] * a0[k] + sy[q] * b0[k];
                    }
                    for (int k = 0; k < NA; ++k)
                        f[k] *= m_cm[k];
                }
                if (m_touching)
                    contacts(f);
                float ql = 0, qr = 0;
                for (int k = 0; k < NA; ++k) {
                    u[k] = m_dd[k] * u[k] - m_cc[k] * x[k] + f[k];
                    x[k] += u[k];
                    ql += m_gl[k] * u[k];
                    qr += m_gr[k] * u[k];
                }
                l[i] += (ql - m_ql) * pGain;
                r[i] += (qr - m_qr) * pGain;
                m_ql = ql;
                m_qr = qr;
                sheet += sheetStep;
                continue;
            }
            // The stretching: each channel's strain, its stress, and what
            // that stress does to every mode.
            float eps[kMaxR];
            for (int c = 0; c < R; ++c) {
                float e = 0;
                const float *g = G + size_t(c) * N;
                for (int k = 0; k < N; ++k)
                    e += g[k] * x[k];
                eps[c] = e;
            }
            for (int p = 0; p < P; ++p)
                m_prod[p] = x[pk[p]] * x[pl[p]];
            for (int c = 0; c < R; ++c) {
                const float *h = He + size_t(c) * P;
                float e = 0;
                for (int p = 0; p < P; ++p)
                    e += h[p] * m_prod[p];
                eps[c] = m_kap[c] * (eps[c] + e);
            }
            for (int k = 0; k < N; ++k)
                f[k] = 0;
            for (int p = 0; p < P; ++p)
                m_S[p] = 0;
            for (int c = 0; c < R; ++c) {
                const float s = eps[c];
                const float *h = Hf + size_t(c) * P;
                for (int p = 0; p < P; ++p)
                    m_S[p] += s * h[p];
                const float *g = G + size_t(c) * N;
                for (int k = 0; k < N; ++k)
                    f[k] -= s * g[k];
            }
            for (int p = 0; p < P; ++p) {
                const int a = pk[p], b = pl[p];
                const float s = m_S[p];
                f[a] -= s * x[b];
                if (a != b)
                    f[b] -= s * x[a];
            }
            for (int k = 0; k < N; ++k)
                f[k] *= m_cm[k];
            // The mallet, while it touches.
            if (m_touching)
                contacts(f);
            float ql = 0, qr = 0;
            for (int k = 0; k < N; ++k) {
                u[k] = m_dd[k] * u[k] - m_cc[k] * x[k] + f[k];
                x[k] += u[k];
                ql += m_gl[k] * u[k];
                qr += m_gr[k] * u[k];
            }
            // Pressure follows the volume acceleration.
            l[i] += (ql - m_ql) * pGain;
            r[i] += (qr - m_qr) * pGain;
            m_ql = ql;
            m_qr = qr;
        }
        float e = 0, top = 0;
        for (int k = 0; k < N; ++k) {
            e += m_loud[k] * (m_cc[k] * x[k] * x[k] + u[k] * u[k]);
            top = std::max(top, std::fabs(x[k]));
        }
        m_energy = e;
        // (Never: a sum of squares can't run away. But a NaN from outside
        // must not ring forever.)
        if (!(top < 50.0f)) {
            silence();
            return;
        }
        if (!m_touching && e < m_sleepEnergy) {
            if (++m_quiet > 8)
                silence();
        } else {
            m_quiet = 0;
        }
    }

    // --- for tests and the docs ---------------------------------------------------------------------------
    const Design *built() const { return m_d; }
    // The mode that is the fundamental (a tam-tam's thud).
    int fundamental() const { return m_d ? (m_d->eigen ? m_d->thud : m_d->axis[0]) : 0; }
    int modes() const { return m_d ? m_d->n : 0; }
    float modeHz(int k) const { return m_hz[std::clamp(k, 0, kMaxModes - 1)]; }
    float modeT60(int k) const { return m_t60[std::clamp(k, 0, kMaxModes - 1)]; }
    // How far mode k swings now, in thicknesses.
    float swing(int k) const {
        k = std::clamp(k, 0, kMaxModes - 1);
        const float th = std::max(kTwoPi * m_hz[k] / m_rate, 1e-4f);
        return std::sqrt(m_x[k] * m_x[k] + (m_u[k] / th) * (m_u[k] / th));
    }
    float radius() const { return m_radius; }
    float thickness() const { return m_spec.thick; }
    float lastContact() const { return m_lastContact; }
    float radiation(int k) const { return m_gl[std::clamp(k, 0, kMaxModes - 1)]; }
    // How much of a tam-tam's stretching is still in play (1 until it fades).
    float stretching() const { return m_sheet; }
    // A tam-tam's swing energy now, in its thud's thicknesses².
    float mechanical() const {
        if (!m_d)
            return 0;
        const float th = kTwoPi * m_hz[fundamental()] / m_rate;
        return m_mech / std::max(th * th, 1e-12f);
    }
    static float ombakHz(float hz) { return clampf(2.5f * std::sqrt(hz / 62.0f), 2.0f, 6.0f); }
    // No losses at all (for checking that the stretching only moves energy
    // about, never makes or loses any).
    void setLossless(bool on) {
        m_lossless = on;
        if (m_d)
            retune();
    }
    // The gong's whole energy, every mode's swing and the sheet's stretch,
    // in units of ρ·h³·a² (joules over that): Σμ·(½ẋ² + ½ω²x²) + Ω²·12(1−ν²)·U.
    double energy() const {
        if (!m_d)
            return 0;
        const Design &d = *m_d;
        const int N = d.n;
        double e = 0;
        // (The integrator's own energy, which it keeps exactly: with u the
        // last step, ½u² + ½c·x·(x − u) per mode, c its stiffness a sample.)
        for (int k = 0; k < N; ++k) {
            const double u = m_u[k], x = m_x[k], c = double(m_w[k]) * m_w[k] / (double(m_rate) * m_rate);
            e += (d.eigen ? 1.0 : double(d.mu[k])) * double(m_rate) * m_rate * (0.5 * u * u + 0.5 * c * x * (x - u));
        }
        double U = 0;
        for (int c = 0; c < d.R && d.Pt == 0; ++c) {
            double eps = 0;
            for (int k = 0; k < N; ++k)
                eps += double(d.G[size_t(c) * N + k]) * m_x[k];
            for (int p = 0; p < d.P; ++p)
                eps += double(d.He[size_t(c) * d.P + p]) * m_x[d.pk[p]] * m_x[d.pl[p]];
            U += 0.5 * d.kap[size_t(c)] * eps * eps;
        }
        if (d.Pt > 0) {
            // (The stretching's linear part is in a sheet mode's own
            // stiffness; the patches hold the rest, κ·(L·Q + ½Q²).)
            for (int q = 0; q < d.Pt; ++q) {
                double gx = 0, gy = 0;
                for (int k = 0; k < N; ++k) {
                    gx += double(d.px[size_t(q) * N + k]) * m_x[k];
                    gy += double(d.py[size_t(q) * N + k]) * m_x[k];
                }
                const double L = d.w0x[size_t(q)] * gx + d.w0y[size_t(q)] * gy, Qd = 0.5 * (gx * gx + gy * gy);
                U += double(m_sheet) * d.kapPt[size_t(q)] * (L * Qd + 0.5 * Qd * Qd);
            }
        }
        const double Om = double(m_omega) * m_scale;
        return e + Om * Om * d.memScale * U;
    }

private:
    struct Contact {
        bool on = false;
        int delay = 0, age = 0, touched = 0;
        float y = 0, v = 0;  // the mallet's face, metres into the gong, and its speed
        float mass = 0.05f, K = 1e6f, p = 2.5f;
        float dmax = 1e9f, kmax = 1e30f; // where the felt's stiffening stops, and its stiffness there
        float phi[kMaxModes] = {};
    };

    static float shapeAt(const Design &d, int k, float rho) {
        const float g = clampf(rho, 0.0f, 1.0f) * float(Design::kGrid - 1);
        const int i = std::min(int(g), Design::kGrid - 2);
        const float t = g - float(i);
        const float *s = d.shape.data() + size_t(k) * Design::kGrid;
        return s[i] + t * (s[i + 1] - s[i]);
    }

    // The felt's stiffness that gives this contact time on a rigid face at
    // speed v: T = 2·δ/v·∫₀¹dz/√(1 − z^(p+1)), ½Mv² = K·δ^(p+1)/(p+1).
    static float feltFor(double p, double mass, double contact, double v) {
        double I = 0;
        const int Q = 2000;
        for (int i = 0; i < Q; ++i) {
            const double z = (i + 0.5) / Q;
            I += 1.0 / std::sqrt(1.0 - std::pow(z, p + 1.0)) / Q;
        }
        const double delta = contact * v / (2.0 * I);
        return float((p + 1.0) * mass * v * v / (2.0 * std::pow(delta, p + 1.0)));
    }

    void contacts(float *f) {
        bool any = false;
        const Design &d = *m_d;
        const int N = d.n;
        const float h = m_spec.thick;
        for (auto &c : m_contacts) {
            if (!c.on)
                continue;
            any = true;
            if (c.delay > 0) {
                --c.delay;
                continue;
            }
            float surf = 0;
            for (int k = 0; k < N; ++k)
                surf += c.phi[k] * m_x[k];
            surf *= h;
            if (c.age == 0)
                c.y = surf;
            ++c.age;
            const float delta = c.y - surf;
            float force = 0;
            if (delta > 0) {
                force = delta < c.dmax ? c.K * std::pow(delta, c.p) : c.K * std::pow(c.dmax, c.p) + c.kmax * (delta - c.dmax);
                for (int k = 0; k < N; ++k)
                    f[k] += force * c.phi[k] * m_cf[k];
                c.touched = c.age;
            }
            c.v -= force / c.mass * m_dt;
            c.y += c.v * m_dt;
            if ((delta <= 0 && c.v < 0 && c.age > 4) || c.age > int(0.06f * m_rate)) {
                c.on = false;
                m_lastContact = float(c.touched) / m_rate;
            }
        }
        m_touching = any;
    }

    // How long each mode rings: T60 in seconds.
    float t60Of(int k, float hz) const {
        const Design &d = *m_d;
        const float f1 = m_hz0 * m_scale, ratio = hz / f1;
        switch (m_kind) {
        case Bossed: {
            const float t1 = clampf(12.0f * std::pow(f1 / 100.0f, -0.6f), 2.5f, 25.0f);
            return std::max(0.3f, t1 * std::pow(ratio, -0.8f));
        }
        case Opera: {
            const float t1 = clampf(4.5f * std::pow(f1 / 200.0f, -0.25f), 2.5f, 6.0f);
            return std::max(0.2f, t1 * std::pow(ratio, -0.9f));
        }
        case TamTam:
        default:
            if (d.m[k] == 0)
                return d.nr[k] == 1 ? 2.5f : 4.0f;
            return clampf(10.0f * std::pow(hz / 1000.0f, -0.3f), 3.0f, 14.0f);
        }
    }

    void retune() {
        const Design &d = *m_d;
        const int N = d.n;
        const float rate = m_rate, dt = m_dt;
        const float Om = m_omega * m_scale;
        // The own stiffness of each mode: the plate's; or (a smith's
        // tuning) what is left of the target once the shell's membrane has
        // stiffened it; or (a tam-tam) its sheet mode's, whose mass is 1.
        m_nAct = N;
        for (int k = 0; k < N; ++k) {
            float w2, mu = d.mu[k];
            int like = k; // the plate mode it rings as, for its losses
            if (d.eigen) {
                const float w = d.eLam2[k] * Om;
                w2 = w * w;
                m_hz[k] = m_hz0 * m_scale * d.eRatio[k];
                mu = 1.0f;
                like = d.eDom[k];
            } else if (d.tuned) {
                w2 = m_own[k] * Om * Om;
                m_hz[k] = m_hz0 * m_scale * m_ratio[k];
            } else {
                const float w = d.lambda2[k] * Om;
                w2 = w * w;
                m_hz[k] = m_hz0 * m_scale * d.ratio[k];
            }
            w2 = std::max(w2, 1.0f);
            const float wn = std::min(std::sqrt(w2), 0.45f * kTwoPi * rate);
            m_t60[k] = t60Of(like, m_hz[k]);
            const float tau = m_t60[k] / 6.9078f;
            const float rr = m_lossless ? 1.0f : std::exp(-1.0f / (tau * rate));
            const float th = wn / rate;
            m_dd[k] = rr * rr;
            m_cc[k] = 1.0f + rr * rr - 2.0f * rr * std::cos(th);
            // (The stiffness as the integrator has it, for the energy.)
            m_w[k] = std::sqrt(std::max(2.0f - 2.0f * std::cos(th), 0.0f)) * rate;
            m_cm[k] = dt * dt * Om * Om * d.memScale / mu;
            m_cf[k] = dt * dt / (m_mass0 * m_spec.thick * mu);
            // Above 0.18 of the rate (8.6 kHz at 48 kHz) a mode sits out:
            // the explicit step holds a mode to about a third of the rate,
            // and a sheet hammered hard stiffens its high modes several
            // times over, so nearer the top they would not stay put (a
            // tam-tam struck ten times a second does go there). A tam-tam's
            // sheet modes come in order, so those are its last ones.
            if (m_hz[k] > Knobs::cap * rate) {
                m_dd[k] = m_cc[k] = m_cm[k] = m_cf[k] = 0;
                m_x[k] = m_u[k] = 0;
                if (d.eigen)
                    m_nAct = std::min(m_nAct, k);
            }
        }
        for (int c = 0; c < d.R; ++c)
            m_kap[c] = d.kap[c];
        for (int q = 0; q < d.Pt && q < kPtPad; ++q) {
            m_kapPt[q] = d.kapPt[size_t(q)];
            m_w0x[q] = d.w0x[size_t(q)];
            m_w0y[q] = d.w0y[size_t(q)];
            // The most a patch may stretch (a numerical bound): what would,
            // with every patch as far, stiffen some mode to 80 % of where the
            // explicit step gives way (c = 4).
            float most = 1e30f;
            for (int k = 0; k < m_nAct; ++k) {
                const float a2 = d.px[size_t(q) * N + k] * d.px[size_t(q) * N + k] + d.py[size_t(q) * N + k] * d.py[size_t(q) * N + k];
                const float room = 3.2f - m_cc[k];
                if (a2 > 0 && m_cm[k] > 0)
                    most = std::min(most, room / (float(d.Pt) * m_cm[k] * m_kapPt[q] * a2));
            }
            m_strainMax[q] = std::max(most, 0.0f);
        }
        if (!d.eigen)
            radiate();
        // Pressure at a metre: ρ/(4π)·d²V/dt², u being thicknesses a sample.
        m_pGain = Knobs::level * Bronze::kRhoAir / (4.0f * kPi) * rate * rate;
        // A hard strike swings the fundamental about a thickness: the
        // energy scale, and asleep 100 dB under it.
        {
            const int k = fundamental();
            const float amp = 1.0f, th = kTwoPi * m_hz[k] / rate;
            m_refEnergy = std::max(m_loud[k] * (m_cc[k] * amp * amp + (amp * th) * (amp * th)), 1e-30f);
            m_sleepEnergy = m_refEnergy * 1e-10f;
            // (A tam-tam's stretching rests 46 dB under a thickness's swing.)
            m_sheetQuiet = 2.5e-5f * th * th;
            m_yield = Knobs::yield * th * th;
        }
        if (m_muteD < 1.0f)
            for (int k = 0; k < N; ++k)
                m_dd[k] = std::min(m_dd[k], m_muteD);
    }

    // What each mode is worth to the microphones, a metre off and a little
    // either side of the axis: an axisymmetric mode the volume it sweeps,
    // as a piston; one with m diameters as a multipole, weak until the gong
    // is large against the wavelength, with the sign of the lobe that faces
    // each. A tam-tam's sheet modes are worth what their plate modes are.
    void radiate() {
        const Design &d = *m_d;
        const int N = d.n;
        const float h = m_spec.thick, a = m_radius;
        const float micL = 0.6f, micR = -0.6f;
        float pl[kMaxModes], pr[kMaxModes];
        for (int k = 0; k < N; ++k) {
            const float hz = d.eigen ? m_hz0 * m_scale * d.lambda2[k] / d.lam1 : m_hz[k];
            if (d.m[k] == 0) {
                pl[k] = pr[k] = h * a * a * d.area[k];
            } else {
                const int m = d.m[k];
                const float ka = kTwoPi * hz * a / Bronze::kAir;
                const float q = std::pow(ka / float(m + 1), float(m + 1));
                const float s = h * a * a * std::sqrt(d.mu[k]) * 0.8f * q / (1.0f + q);
                const float o = d.orient[k] ? 0.5f * kPi : 0.0f;
                pl[k] = s * std::cos(float(m) * micL - o);
                pr[k] = s * std::cos(float(m) * micR - o);
            }
        }
        for (int j = 0; j < N; ++j) {
            float wl = pl[j], wr = pr[j];
            if (d.eigen) {
                wl = wr = 0;
                for (int k = 0; k < N; ++k) {
                    wl += pl[k] * d.T[size_t(k) * N + j];
                    wr += pr[k] * d.T[size_t(k) * N + j];
                }
            }
            m_gl[j] = wl;
            m_gr[j] = wr;
            m_loud[j] = 0.5f * (wl * wl + wr * wr) + 1e-3f * h * h * a * a * a * a * (d.eigen ? 1.0f : d.mu[j]);
        }
    }

    const Design *m_d = nullptr;
    Kind m_kind = Opera;
    Spec m_spec;
    float m_rate = 48000, m_dt = 1.0f / 48000;
    float m_hz0 = 200, m_scale = 1, m_omega = 1, m_radius = 0.1f, m_mass0 = 1;
    float m_malletMass = 0.05f, m_felt = 1e6f, m_speed = 1, m_rise = 0;
    alignas(16) float m_x[kMaxModes] = {}, m_u[kMaxModes] = {}, m_f[kMaxModes] = {};
    alignas(16) float m_dd[kMaxModes] = {}, m_cc[kMaxModes] = {}, m_cm[kMaxModes] = {}, m_cf[kMaxModes] = {};
    alignas(16) float m_gl[kMaxModes] = {}, m_gr[kMaxModes] = {}, m_loud[kMaxModes] = {};
    float m_ratio[kMaxModes] = {}, m_hz[kMaxModes] = {}, m_t60[kMaxModes] = {}, m_own[kMaxModes] = {}, m_w[kMaxModes] = {};
    bool m_lossless = false;
    alignas(16) float m_prod[kMaxPairs] = {}, m_S[kMaxPairs] = {};
    float m_kap[kMaxR] = {};
    float m_kapPt[kPtPad] = {}, m_w0x[kPtPad] = {}, m_w0y[kPtPad] = {}, m_strainMax[kPtPad] = {};
    float m_pGain = 1;
    float m_ql = 0, m_qr = 0;
    float m_energy = 0, m_refEnergy = 1, m_sleepEnergy = 0;
    int m_quiet = 0;
    float m_muteD = 1.0f, m_lastContact = 0;
    bool m_sleeping = true, m_touching = false, m_quietSheet = false;
    float m_sheet = 1.0f; // how much of a tam-tam's patch stretching is in play
    float m_sheetQuiet = 0, m_yield = 1e30f, m_mech = 0;
    int m_nAct = 0; // a tam-tam's sheet modes in play
    Contact m_contacts[2];
};

}
