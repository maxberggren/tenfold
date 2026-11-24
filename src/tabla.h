#pragma once

// A tabla, as physics. Not samples and not a recipe: the dayan and the
// bayan as they are made, in SI units, solved for their modes from what
// they are made of and integrated sample by sample. The measurements it is
// built from and checked against are in docs/tabla.md.
//
//   the heads   goatskin, 0.25 kg/m², stretched over sealed shells: the
//               dayan 14 cm across over 0.7 L of air, the bayan 21 cm over
//               5.5 L. Round the edge the kinar, a ring of skin laid loose
//               over the first, which rubs rather than loads.
//   the syahi   iron filings and starch laid on in layers, thickest in the
//               middle: 15 g over the middle of the dayan, 18 g off the
//               bayan's centre. The heavy patch is what tunes a drumhead's
//               inharmonic modes (2.30, 3.60 … × its first on a bare skin)
//               onto Raman's harmonics: (1,1) the 2nd of Sa, (2,1)+(0,2) the
//               3rd, (3,1)+(1,2) the 4th, (4,1)+(2,2)+(0,3) the 5th, the
//               (0,1) a whole tone above Sa. Each head's modes are solved
//               for, once, by Rayleigh–Ritz over the bare membrane's Bessel
//               modes with the patch's mass, the kinar's and the air's.
//   the shells  sealed: the air inside is a spring on the head's net
//               volume, coupling every mode that sweeps one. The dayan's
//               (0,1) rises, modes with a nodal diameter don't move; the
//               bayan's ge is its lowest mode, the syahi riding on the skin
//               and the air together.
//   the fingers a Hertzian pulp with Hunt–Crossley loss, solved against the
//               skin under it: where it lands decides which modes it drives
//               (its patch averaged over each mode's shape). The strokes are
//               where they land and what rests on the skin while they ring:
//               na at the kinar with the ring finger resting on the syahi,
//               holding a node there so the fundamental dies and the 2nd
//               and 3rd ring; tin further in, barely touched; tun on the syahi,
//               left free; ge past the bayan's syahi with the heel resting
//               at its near edge; ka the hand flat. A finger left down rests
//               too, a spring and a dashpot solved implicitly, so a hand only
//               ever takes energy out.
//   the stretch the tension rises with the skin's own swing (Kirchhoff–
//               Carrier: a hard stroke starts sharp) and with the heel
//               pressed in: the bayan's glide, an octave at most.
//   the sound   two microphones over the player's shoulders hear each
//               mode's far field, in closed form from its Bessel terms, and
//               each mode loses what it radiates; the whole head's net
//               volume radiates together.
//
// Each head rings once, whatever strikes it: a stroke adds a contact for
// the milliseconds it touches, and a finger resting adds a little while the
// head under it rings.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp.h"


namespace tf::tabla {

// --- Bessel functions -------------------------------------------------------------------------------------
//
// J_m(x) by Miller's backward recurrence, normalised by
// J_0 + 2·ΣJ_2k = 1: accurate to double precision for the orders and
// arguments a drumhead needs, and portable (libc++ has no
// std::cyl_bessel_j).

inline double besselJ(int m, double x) {
    if (m < 0)
        return (m & 1 ? -1.0 : 1.0) * besselJ(-m, x);
    if (std::fabs(x) < 1e-12)
        return m == 0 ? 1.0 : 0.0;
    const bool neg = x < 0;
    x = std::fabs(x);
    const int top = 2 * ((std::max(m, int(x)) + 15 + int(std::sqrt(40.0 * (std::max(m, int(x)) + 15)))) / 2);
    double jp = 0, j = 1e-30, sum = 0, want = 0;
    for (int k = top; k > 0; --k) {
        const double jm = 2.0 * k / x * j - jp;
        jp = j;
        j = jm;
        if (std::fabs(j) > 1e250) {
            j *= 1e-250;
            jp *= 1e-250;
            sum *= 1e-250;
            want *= 1e-250;
        }
        if ((k - 1) % 2 == 0 && k - 1 > 0)
            sum += j;
        if (k - 1 == m)
            want = j;
    }
    // j is now J_0 (unnormalised); sum holds J_2 + J_4 + …
    const double norm = j + 2.0 * sum;
    const double r = want / norm;
    return neg && (m & 1) ? -r : r;
}

// The first `count` positive zeros of J_m, in one pass (zeros of J_m are
// more than π apart less a little, so a step of 0.4 never skips one).
inline std::vector<double> besselZeros(int m, int count) {
    std::vector<double> z;
    double x = 0.4 + 0.9 * m, prev = besselJ(m, x);
    while (int(z.size()) < count) {
        const double nx = x + 0.4, cur = besselJ(m, nx);
        if ((prev < 0) != (cur < 0)) {
            double lo = x, hi = nx;
            for (int i = 0; i < 48; ++i) {
                const double mid = 0.5 * (lo + hi);
                ((besselJ(m, mid) < 0) == (prev < 0) ? lo : hi) = mid;
            }
            z.push_back(0.5 * (lo + hi));
        }
        x = nx;
        prev = cur;
    }
    return z;
}
inline double besselZero(int m, int n) { return besselZeros(m, n).back(); }

// --- the symmetric eigenproblem ---------------------------------------------------------------------------
//
// Cyclic Jacobi on a dense symmetric matrix (row-major, n×n): its
// eigenvalues on the diagonal, its eigenvectors in v's columns.

inline void jacobi(std::vector<double> &a, std::vector<double> &v, int n) {
    v.assign(size_t(n) * size_t(n), 0.0);
    for (int i = 0; i < n; ++i)
        v[size_t(i) * size_t(n) + size_t(i)] = 1.0;
    auto A = [&](int i, int j) -> double & { return a[size_t(i) * size_t(n) + size_t(j)]; };
    auto V = [&](int i, int j) -> double & { return v[size_t(i) * size_t(n) + size_t(j)]; };
    for (int sweep = 0; sweep < 60; ++sweep) {
        double off = 0, diag = 0;
        for (int i = 0; i < n; ++i) {
            diag += A(i, i) * A(i, i);
            for (int j = i + 1; j < n; ++j)
                off += A(i, j) * A(i, j);
        }
        if (off <= 1e-22 * diag)
            break;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                const double apq = A(p, q);
                if (std::fabs(apq) < 1e-300 || std::fabs(apq) <= 1e-18 * std::sqrt(std::fabs(A(p, p) * A(q, q))))
                    continue;
                const double theta = (A(q, q) - A(p, p)) / (2.0 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k) {
                    const double akp = A(k, p), akq = A(k, q);
                    A(k, p) = c * akp - s * akq;
                    A(k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = A(p, k), aqk = A(q, k);
                    A(p, k) = c * apk - s * aqk;
                    A(q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = V(k, p), vkq = V(k, q);
                    V(k, p) = c * vkp - s * vkq;
                    V(k, q) = s * vkp + c * vkq;
                }
            }
    }
}

// --- a head -----------------------------------------------------------------------------------------------
//
// What a head is made of, in SI units: its radius; the bare skin's mass per
// area; the kinar, a second ring of skin laid over the first round the
// edge; the syahi, a paste of iron filings and starch laid on in layers,
// thickest in its middle, its centre off the head's by `offset` (the
// bayan's) along x.

struct Design {
    double radius = 0.07;       // m
    double skin = 0.27;         // kg/m²
    double kinarFrom = 0.8;     // r/a where the kinar begins
    double kinar = 0.27;        // its extra kg/m²
    double syahi = 0.42;        // its radius over the head's
    double offset = 0.0;        // its centre, over the head's radius, along +x
    double load = 3.0;          // kg/m² at its middle, over the skin
    double rim = 0.35;          // what is left at its edge, over the middle's
    double power = 1.0;         // how the layers thin toward the edge
    int steps = 0;              // laid on in this many layers (0: smoothly rubbed down)
    double airMass = 1.0;       // the air either side, as a share of 2ρ/κ
};

// A head's modes, solved for: the mode shapes as sums of the bare
// membrane's Bessel modes (Rayleigh–Ritz), each mode's ω²/T, its modal
// mass and the volume it sweeps per metre at its largest point.
struct Solved {
    struct Basis {
        int m = 0, n = 1;
        bool sine = false;
        double kappa = 0; // j_mn / a
        double norm = 1;  // so ∫φ² dA = 1
    };
    std::vector<Basis> basis;
    int modes = 0;
    std::vector<double> lambda; // ω²/T
    std::vector<double> mass;   // kg, the shape scaled to 1 at its peak
    std::vector<double> volume; // m², ∫φ dA
    std::vector<double> coef;   // modes × basis, the shapes (peak 1)
    std::vector<int> order;     // the dominant (m) of each mode
    std::vector<int> radial;    // and its n
    std::vector<double> syahiShare, kinarShare; // how much of its mass moves in the syahi, the kinar

    // A mode's displacement at (x, y) in units of the head's radius.
    double shape(int k, double x, double y, double a) const {
        const double r = std::sqrt(x * x + y * y), th = std::atan2(y, x);
        double s = 0;
        const size_t nb = basis.size();
        for (size_t i = 0; i < nb; ++i) {
            const double c = coef[size_t(k) * nb + i];
            if (c == 0)
                continue;
            const Basis &b = basis[i];
            const double ang = b.m == 0 ? 1.0 : b.sine ? std::sin(b.m * th) : std::cos(b.m * th);
            s += c * b.norm * besselJ(b.m, b.kappa * r * a) * ang;
        }
        return s;
    }
};

// The mass the kinar and the syahi add to the bare skin, kg/m², at (x, y)
// in units of the head's radius.
inline double kinarAt(const Design &d, double r) { return r >= d.kinarFrom ? d.kinar : 0.0; }
inline double syahiAt(const Design &d, double x, double y) {
    const double dx = x - d.offset, rho2 = (dx * dx + y * y) / (d.syahi * d.syahi);
    if (rho2 >= 1.0)
        return 0.0;
    double v = std::pow(1.0 - rho2, d.power);
    if (d.steps > 0) // each layer a disc of its own thickness, each smaller than the last
        v = std::ceil(v * d.steps - 1e-9) / d.steps;
    return d.load * (d.rim + (1.0 - d.rim) * v);
}
inline double sigmaAt(const Design &d, double x, double y) {
    return d.skin + kinarAt(d, std::sqrt(x * x + y * y)) + syahiAt(d, x, y);
}

// The head's modes by Rayleigh–Ritz over the bare membrane's own modes,
// J_m(j_mn·r/a)·{cos, sin}(mθ): their stiffness is T·κ² each (they are the
// Laplacian's eigenfunctions), and the mass matrix is the skin's, the
// air's either side (2ρ/κ for each, the evanescent air a mode carries) and
// what the kinar and syahi add. A centred syahi keeps every angular order
// apart; one off centre along x keeps only the cosines from the sines.
// The syahi's mass (kg): its paste over the head.
inline double syahiMass(const Design &d) {
    double m = 0;
    const int n = 400;
    for (int i = 0; i < n; ++i) {
        const double r = (i + 0.5) / n * d.syahi;
        m += syahiAt(d, d.offset + r, 0) * kTwoPi * r * d.syahi / n * d.radius * d.radius;
    }
    return m;
}

inline Solved solve(const Design &d, int mMax = 8, int nMax = 7, int keep = 40) {
    Solved out;
    const double a = d.radius;
    const bool centred = std::fabs(d.offset) < 1e-9;
    for (int m = 0; m <= mMax; ++m) {
        const std::vector<double> zeros = besselZeros(m, nMax);
        for (int sine = 0; sine < (m == 0 ? 1 : 2); ++sine)
            for (int n = 1; n <= nMax; ++n) {
                Solved::Basis b;
                b.m = m;
                b.n = n;
                b.sine = sine;
                const double j = zeros[size_t(n - 1)];
                b.kappa = j / a;
                // ∫ J_m(j r/a)² r dr over [0,a] = a²/2 · J_{m+1}(j)²; the
                // angle gives 2π (m = 0) or π.
                const double jn = besselJ(m + 1, j);
                b.norm = 1.0 / std::sqrt((m == 0 ? 2.0 : 1.0) * M_PI * 0.5 * a * a * jn * jn);
                out.basis.push_back(b);
            }
    }
    const int nb = int(out.basis.size());
    auto group = [&](int i) {
        const auto &b = out.basis[size_t(i)];
        return centred ? 2 * b.m + int(b.sine) : int(b.sine);
    };
    std::vector<double> M(size_t(nb) * size_t(nb), 0.0), MK(M.size(), 0.0), MS(M.size(), 0.0);
    auto at = [&](int i, int j) -> double & { return M[size_t(i) * size_t(nb) + size_t(j)]; };
    auto atK = [&](int i, int j) -> double & { return MK[size_t(i) * size_t(nb) + size_t(j)]; };
    auto atS = [&](int i, int j) -> double & { return MS[size_t(i) * size_t(nb) + size_t(j)]; };
    for (int i = 0; i < nb; ++i)
        at(i, i) = d.skin + d.airMass * 2.0 * 1.2 / out.basis[size_t(i)].kappa;
    // Radial quadrature (the angle exact): the kinar, and a centred syahi.
    const int nr = 256;
    std::vector<double> jt(static_cast<size_t>(nb) * static_cast<size_t>(nr));
    for (int i = 0; i < nb; ++i)
        for (int ir = 0; ir < nr; ++ir) {
            const auto &b = out.basis[size_t(i)];
            jt[size_t(i) * size_t(nr) + size_t(ir)] = b.norm * besselJ(b.m, b.kappa * a * (ir + 0.5) / nr);
        }
    for (int ir = 0; ir < nr; ++ir) {
        const double r = (ir + 0.5) / nr;
        const double ek = kinarAt(d, r), es = centred ? syahiAt(d, r, 0) : 0.0;
        if (ek + es == 0)
            continue;
        const double w = r * a * a / nr;
        for (int i = 0; i < nb; ++i)
            for (int j = i; j < nb; ++j) {
                const auto &bi = out.basis[size_t(i)], &bj = out.basis[size_t(j)];
                if (bi.m != bj.m || bi.sine != bj.sine)
                    continue;
                const double v = w * (bi.m == 0 ? 2.0 * M_PI : M_PI) * jt[size_t(i) * size_t(nr) + size_t(ir)] *
                                 jt[size_t(j) * size_t(nr) + size_t(ir)];
                at(i, j) += (ek + es) * v;
                atK(i, j) += ek * v;
                atS(i, j) += es * v;
            }
    }
    // An off-centre syahi: a polar grid over its own disc, each group (the
    // cosines, the sines) on its own.
    if (!centred) {
        std::vector<int> members[2];
        for (int i = 0; i < nb; ++i)
            members[group(i)].push_back(i);
        std::vector<double> local[2];
        for (int g = 0; g < 2; ++g)
            local[g].assign(members[g].size() * members[g].size(), 0.0);
        const int nrs = 64, nts = 80;
        double pv[2][128];
        for (int ir = 0; ir < nrs; ++ir) {
            const double rho = (ir + 0.5) / nrs * d.syahi;
            for (int it = 0; it < nts; ++it) {
                const double ps = 2.0 * M_PI * (it + 0.5) / nts;
                const double x = d.offset + rho * std::cos(ps), y = rho * std::sin(ps);
                const double r = std::sqrt(x * x + y * y), th = std::atan2(y, x);
                if (r >= 1.0)
                    continue;
                const double w = syahiAt(d, x, y) * rho * (d.syahi / nrs) * (2.0 * M_PI / nts) * a * a;
                // (the radial table, interpolated)
                const double fr = std::clamp(r * nr - 0.5, 0.0, nr - 1.001);
                const int i0 = int(fr);
                const double t = fr - i0;
                double cm[16], sm[16];
                for (int m = 0; m <= std::min(mMax, 15); ++m) {
                    cm[m] = std::cos(m * th);
                    sm[m] = std::sin(m * th);
                }
                for (int g = 0; g < 2; ++g) {
                    const int gn = int(members[g].size());
                    for (int p = 0; p < gn; ++p) {
                        const int i = members[g][size_t(p)];
                        const auto &bs = out.basis[size_t(i)];
                        const double *row = &jt[size_t(i) * size_t(nr)];
                        pv[g][p] = (row[i0] + t * (row[i0 + 1] - row[i0])) * (bs.sine ? sm[bs.m] : cm[bs.m]);
                    }
                    double *loc = local[g].data();
                    for (int p = 0; p < gn; ++p) {
                        const double wp = w * pv[g][p];
                        double *lr = loc + size_t(p) * size_t(gn);
                        for (int q = p; q < gn; ++q)
                            lr[q] += wp * pv[g][q];
                    }
                }
            }
        }
        for (int g = 0; g < 2; ++g) {
            const int gn = int(members[g].size());
            for (int p = 0; p < gn; ++p)
                for (int q = p; q < gn; ++q) {
                    const double v = local[g][size_t(p) * size_t(gn) + size_t(q)];
                    at(members[g][size_t(p)], members[g][size_t(q)]) += v;
                    atS(members[g][size_t(p)], members[g][size_t(q)]) += v;
                }
        }
    }
    for (int i = 0; i < nb; ++i)
        for (int j = 0; j < i; ++j) {
            at(i, j) = at(j, i);
            atK(i, j) = atK(j, i);
            atS(i, j) = atS(j, i);
        }
    // Each group on its own: K = T·diag(κ²), so with D = diag(1/κ),
    // D·M·D y = (1/λ) y and the shape is D·y.
    struct Found {
        double lambda;
        int group;
        std::vector<double> v;
    };
    std::vector<Found> found;
    const int groups = centred ? 2 * mMax + 2 : 2;
    for (int g = 0; g < groups; ++g) {
        std::vector<int> mem;
        for (int i = 0; i < nb; ++i)
            if (group(i) == g)
                mem.push_back(i);
        const int n = int(mem.size());
        if (!n)
            continue;
        std::vector<double> A(size_t(n) * size_t(n)), V;
        for (int p = 0; p < n; ++p)
            for (int q = 0; q < n; ++q)
                A[size_t(p) * size_t(n) + size_t(q)] =
                    at(mem[size_t(p)], mem[size_t(q)]) /
                    (out.basis[size_t(mem[size_t(p)])].kappa * out.basis[size_t(mem[size_t(q)])].kappa);
        jacobi(A, V, n);
        for (int c = 0; c < n; ++c) {
            Found f;
            f.group = g;
            f.lambda = 1.0 / A[size_t(c) * size_t(n) + size_t(c)];
            f.v.assign(size_t(nb), 0.0);
            for (int p = 0; p < n; ++p)
                f.v[size_t(mem[size_t(p)])] =
                    V[size_t(p) * size_t(n) + size_t(c)] / out.basis[size_t(mem[size_t(p)])].kappa;
            found.push_back(std::move(f));
        }
    }
    std::sort(found.begin(), found.end(), [](const Found &p, const Found &q) {
        return p.lambda != q.lambda ? p.lambda < q.lambda : p.group < q.group;
    });
    keep = std::min(keep, int(found.size()));
    out.modes = keep;
    out.coef.assign(size_t(keep) * size_t(nb), 0.0);
    for (int k = 0; k < keep; ++k) {
        double *c = &out.coef[size_t(k) * size_t(nb)];
        double best = 0;
        int bi = 0;
        for (int i = 0; i < nb; ++i) {
            c[i] = found[size_t(k)].v[size_t(i)];
            if (std::fabs(c[i]) * out.basis[size_t(i)].norm > best) {
                best = std::fabs(c[i]) * out.basis[size_t(i)].norm;
                bi = i;
            }
        }
        out.lambda.push_back(found[size_t(k)].lambda);
        out.order.push_back(out.basis[size_t(bi)].m);
        // Its radial order: how many of that angular order's modes came before it.
        int nth = 1;
        for (int q = 0; q < k; ++q)
            nth += out.order[size_t(q)] == out.basis[size_t(bi)].m;
        out.radial.push_back(out.basis[size_t(bi)].m == 0 ? nth : (nth + 1) / 2);
        // Scaled to 1 at its peak, found on a grid; then its modal mass vᵀMv
        // and the volume it sweeps (only the m = 0 parts sweep any).
        double peak = 0;
        std::vector<int> nz;
        for (int i = 0; i < nb; ++i)
            if (c[i] != 0)
                nz.push_back(i);
        for (int it = 0; it < 64; ++it) {
            const double th = 2.0 * M_PI * it / 64.0;
            double ang[24];
            for (int m = 0; m <= std::min(mMax, 23); ++m)
                ang[m] = std::cos(m * th);
            double angS[24];
            for (int m = 0; m <= std::min(mMax, 23); ++m)
                angS[m] = std::sin(m * th);
            for (int ir = 0; ir < nr; ir += 5) {
                double v = 0;
                for (const int i : nz) {
                    const auto &b = out.basis[size_t(i)];
                    v += c[i] * jt[size_t(i) * size_t(nr) + size_t(ir)] * (b.sine ? angS[b.m] : ang[b.m]);
                }
                peak = std::max(peak, std::fabs(v));
            }
        }
        for (int i = 0; i < nb; ++i)
            c[i] /= peak;
        double mk = 0, mkK = 0, mkS = 0;
        for (int i = 0; i < nb; ++i)
            if (c[i] != 0)
                for (int j = 0; j < nb; ++j) {
                    mk += c[i] * at(i, j) * c[j];
                    mkK += c[i] * atK(i, j) * c[j];
                    mkS += c[i] * atS(i, j) * c[j];
                }
        out.mass.push_back(mk);
        out.kinarShare.push_back(mkK / mk);
        out.syahiShare.push_back(mkS / mk);
        double vol = 0;
        for (int i = 0; i < nb; ++i) {
            const auto &b = out.basis[size_t(i)];
            if (b.m == 0) // ∫ J_0(κr) 2πr dr = 2πa·J_1(j)/κ
                vol += c[i] * b.norm * 2.0 * M_PI * a * besselJ(1, b.kappa * a) / b.kappa;
        }
        out.volume.push_back(vol);
    }
    return out;
}


// --- the pair ---------------------------------------------------------------------------------------------

enum Drum : int { Dayan = 0, Bayan = 1 };
// The strokes, by where they land: the dayan's kinar (na), its maidan
// (tin), its syahi (tun); the bayan open (ge) and the bayan slapped flat (ka).
enum Stroke : int { Na = 0, Tin, Tun, Ge, Ka, kStrokes };
inline int drumOf(int stroke) { return stroke == Ge || stroke == Ka ? Bayan : Dayan; }

// SI units. Sources in docs/tabla.md.
struct Physics {
    static constexpr double kRho = 1.2;  // kg/m³
    static constexpr double kC = 343.0;  // m/s
    // Goatskin, dried on the drum under tension.
    static constexpr double kSkinE = 0.6e9;   // Pa
    static constexpr double kSkinNu = 0.4;
    static constexpr double kSkinThick = 0.00022; // m
    static constexpr double kSkin = 0.27;         // kg/m²
    // The dayan: a 14 cm head, the kinar a 15 mm ring laid loose over its
    // edge (a damper more than a load), the syahi over 55 % of its radius:
    // 15 g of paste laid on in seven layers, each a disc smaller than the
    // last, which put the head's modes on Raman's harmonics with the
    // shell's air pushing back on them.
    static Design dayan() {
        Design d;
        d.radius = 0.07;
        d.skin = 0.25;
        d.kinarFrom = 1.0 - 0.015 / 0.07;
        d.kinar = 0.05;
        d.syahi = 0.545;
        d.load = 4.689;
        d.rim = 0.465;
        d.power = 1.596;
        d.steps = 7;
        return d;
    }
    // The bayan: a 21 cm head, its syahi over 25 % of the radius and off
    // the centre by 18 % of it (away from the player), heavier, 18 g.
    static Design bayan() {
        Design d;
        d.radius = 0.105;
        d.skin = 0.28;
        d.kinarFrom = 1.0 - 0.015 / 0.105;
        d.kinar = 0.05;
        d.syahi = 0.248;
        d.offset = -0.18;
        d.load = 9.888;
        d.rim = 0.78;
        d.power = 2.4;
        return d;
    }
    static constexpr double kVolume[2] = {0.0007, 0.0055}; // m³ inside each shell
    // Where the heads' centres are (m), in the player's frame: x to the
    // right, y away from the player, z up; and two microphones over the
    // player's shoulders, a little behind the drums and above.
    static constexpr double kAt[2][3] = {{0.16, 0.0, 0.0}, {-0.17, 0.02, 0.0}};
    static constexpr double kMic[2][3] = {{-0.12, -0.32, 0.45}, {0.12, -0.32, 0.45}};
};

// Where on its drum each stroke is played (x, y in head radii, x to the
// player's right, y away): the stroke a point is in, and each stroke's own
// spot. On the dayan the kinar is na, the maidan tin, the syahi tun; on
// the bayan the near part, where the hand lies flat, is ka, past it ge.
inline int strokeAt(int drum, float x, float y) {
    const float r = std::sqrt(x * x + y * y);
    if (drum == Bayan)
        return y < -0.45f ? Ka : Ge;
    const Design d = Physics::dayan();
    return r >= float(d.kinarFrom) ? Na : r >= float(d.syahi) ? Tin : Tun;
}
inline void strokeHome(int stroke, float &x, float &y) {
    static const float at[kStrokes][2] = {{0.925f, 0.1f}, {0.66f, 0.15f}, {0.05f, 0.1f}, {0.12f, 0.38f}, {0.0f, -0.6f}};
    stroke = std::clamp(stroke, 0, kStrokes - 1);
    x = at[stroke][0];
    y = at[stroke][1];
}

// The constants that published decays, spectra and levels set
// (docs/tabla.md says which measurement each answers).
struct Knobs {
    static inline float level = 0.38f; // Pa → full scale
    // The heads' own losses: a loss factor for each drum (the bayan's
    // slack head and thick paste lossier), and the skin and the kinar
    // rubbing on it, whose loss factor grows as f² (s⁻¹ at 1 kHz, ∝ f³):
    // what keeps the dayan to Raman's five harmonics.
    static inline float eta[2] = {0.0006f, 0.02f};
    static inline float high = 8.0f;
    // Air leaking through the lacing at the head's edge, on its net volume
    // velocity (kg/(m⁴·s)), as the radiation is.
    static inline float leak[2] = {800.0f, 0.0f};
    static inline float split = 4.0f; // cents a well-tuned head's degenerate pairs lie apart, at most
    // The ring finger resting in na and tin: already on the skin as the
    // index finger strikes, round from it (radians, toward the player), at
    // the syahi's edge (over the syahi's radius), a fingertip wide; its
    // pulp's stiffness (N/m) and loss (N·s/m).
    static inline float ringAt = 0.9f, ringAngle = 1.2f, ringSize = 0.005f, ringK = 300.0f, ringC = 20.0f;
    // In tin it barely touches: the 2nd harmonic rings with the 3rd.
    static inline float ringCTin = 3.0f;
    // A finger left on the dayan after it struck (te, a closed stroke):
    // pressed flat on the skin.
    static inline float restK = 3000.0f, restC = 30.0f;
    // The heel of the left hand on the bayan, on the syahi's diameter at
    // the near side: where (over the head's radius), how wide, stiffness
    // and loss.
    static inline float heelAt = 0.7f, heelSize = 0.025f, heelK = 2000.0f, heelC = 8.0f;
    static inline float tick = 1.0f; // the contact force's own sound
};

// Where each microphone is from a head (m): how far, its elevation from
// the head's axis, its azimuth in the head's own frame (the bayan's turned
// so its syahi, solved along −x, sits away from the player).
inline void micFrom(int drum, int mic, double &dist, double &theta, double &phi) {
    const double dx = Physics::kMic[mic][0] - Physics::kAt[drum][0];
    const double dy = Physics::kMic[mic][1] - Physics::kAt[drum][1];
    const double dz = Physics::kMic[mic][2] - Physics::kAt[drum][2];
    dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    theta = std::acos(dz / dist);
    phi = drum == Bayan ? std::atan2(dx, -dy) : std::atan2(dy, dx);
}

// The shapes and sums a head needs at run time, solved once per process.
struct Built {
    static constexpr int kNr = 48, kNt = 72;
    // The far field is tabulated against the wavenumber in air, 20 Hz to
    // 12 kHz, so retuning while playing costs lookups.
    static constexpr int kKw = 64;
    static constexpr double kKwLo = kTwoPi * 20.0 / Physics::kC, kKwHi = kTwoPi * 12000.0 / Physics::kC;
    Design design;
    Solved s;
    int n = 0;
    std::vector<float> table; // n × (kNr + 1) × kNt, each mode's shape (peak 1)
    // Each mode's Bessel terms, for the far field in closed form.
    struct Term {
        int m;
        bool sine;
        double w;      // c·norm·a·κ·J_{m+1}(κa)
        double kappa2; // κ²
    };
    std::vector<Term> terms;
    std::vector<int> termFrom; // n + 1
    std::vector<float> power;  // n × kKw: ∫|F|² over the half space
    std::vector<float> fRe[2], fIm[2]; // n × kKw: F toward each microphone

    float shapeAt(int k, float x, float y) const {
        const float r = std::sqrt(x * x + y * y);
        if (r >= 1.0f)
            return 0.0f;
        float th = std::atan2(y, x);
        if (th < 0)
            th += kTwoPi;
        const float fr = r * kNr, ft = th * (kNt / kTwoPi);
        const int ir = std::min(int(fr), kNr - 1), it = int(ft) % kNt, it1 = (it + 1) % kNt;
        const float tr = fr - float(ir), tt = ft - std::floor(ft);
        const float *base = &table[size_t(k) * size_t((kNr + 1) * kNt)];
        const float a0 = base[ir * kNt + it], a1 = base[ir * kNt + it1];
        const float b0 = base[(ir + 1) * kNt + it], b1 = base[(ir + 1) * kNt + it1];
        return (a0 + tt * (a1 - a0)) * (1 - tr) + (b0 + tt * (b1 - b0)) * tr;
    }
    // Every mode's shape at (x, y), times w, added into out (one lookup of
    // where the point is for all the modes).
    void shapesAt(float x, float y, float w, float *out) const {
        const float r = std::sqrt(x * x + y * y);
        if (r >= 1.0f)
            return;
        float th = std::atan2(y, x);
        if (th < 0)
            th += kTwoPi;
        const float fr = r * kNr, ft = th * (kNt / kTwoPi);
        const int ir = std::min(int(fr), kNr - 1), it = int(ft) % kNt, it1 = (it + 1) % kNt;
        const float tr = fr - float(ir), tt = ft - std::floor(ft);
        const float w00 = w * (1 - tr) * (1 - tt), w01 = w * (1 - tr) * tt, w10 = w * tr * (1 - tt), w11 = w * tr * tt;
        const size_t stride = size_t((kNr + 1) * kNt);
        const size_t i00 = size_t(ir * kNt + it), i01 = size_t(ir * kNt + it1), i10 = size_t((ir + 1) * kNt + it),
                     i11 = size_t((ir + 1) * kNt + it1);
        const float *base = table.data();
        for (int k = 0; k < n; ++k, base += stride)
            out[k] += w00 * base[i00] + w01 * base[i01] + w10 * base[i10] + w11 * base[i11];
    }
    static double kwAt(int j) { return kKwLo * std::pow(kKwHi / kKwLo, double(j) / (kKw - 1)); }
    // A tabulated value of mode k at wavenumber kw (log-interpolated).
    double lookup(const std::vector<float> &t, int k, double kw) const {
        const double f = std::clamp(std::log(kw / kKwLo) / std::log(kKwHi / kKwLo) * (kKw - 1), 0.0, kKw - 1.001);
        const int j = int(f);
        const double u = f - j;
        const float *row = &t[size_t(k) * kKw];
        return row[j] + u * (row[j + 1] - row[j]);
    }
};

// Mode k's far-field sums at wavenumber k0 along the head, by angular
// order: Σ c·norm·aκJ_{m+1}(κa)/(κ² − k0²) over its cos and sin terms.
inline void farSums(const Built &b, int k, double k0, double *cosSum, double *sinSum) {
    for (int m = 0; m < 12; ++m)
        cosSum[m] = sinSum[m] = 0;
    for (int t = b.termFrom[size_t(k)]; t < b.termFrom[size_t(k) + 1]; ++t) {
        const Built::Term &term = b.terms[size_t(t)];
        double den = term.kappa2 - k0 * k0;
        if (std::fabs(den) < 1e-6 * term.kappa2)
            den = 1e-6 * term.kappa2;
        // (the order as a plain value: gcc 12 and 13 at -O3 read std::min's
        // reference to a temporary here as garbage, and filed every term
        // under the wrong order: the dayan's dipoles radiated as monopoles)
        const int m = term.m < 11 ? term.m : 11;
        double *sums = term.sine ? sinSum : cosSum;
        sums[m] += term.w / den;
    }
}

// The far field of mode k at wavenumber k0 along the head (ω/c·sin θ) and
// azimuth phi, F = ∫φ·e^{−ik·r} dA, by Jacobi–Anger and Lommel's integral
// over each Bessel term (each is zero at the rim):
//   ∫∫ J_m(κr)·cos mθ · e^{−i k0 r cos(θ−φ)} r dr dθ
//     = 2π (−i)^m cos mφ · a κ J_{m+1}(κa) J_m(k0 a) / (κ² − k0²).
// jm: J_m(k0·a) for m < 12.
inline void farField(const Built &b, int k, double k0, double phi, const double *jm, double &re, double &im) {
    double cs[12], sn[12];
    farSums(b, k, k0, cs, sn);
    re = im = 0;
    for (int m = 0; m < 12; ++m) {
        const double v = kTwoPi * jm[m] * (cs[m] * std::cos(m * phi) + sn[m] * std::sin(m * phi));
        switch (m & 3) { // (−i)^m
        case 0: re += v; break;
        case 1: im -= v; break;
        case 2: re -= v; break;
        default: im += v; break;
        }
    }
}

// ∫|F|² over the half space above the head, for mode k at wavenumber kw in
// air: the azimuth in closed form (different m don't mix), the elevation
// by Gauss–Legendre. jm[g][m]: J_m(kw·sin θ_g·a) at the eight nodes.
inline double farPower(const Built &b, int k, double kw, const double (*jm)[12]) {
    static const double gx[8] = {-0.9602898565, -0.7966664774, -0.5255324099, -0.1834346425,
                                 0.1834346425,  0.5255324099,  0.7966664774,  0.9602898565};
    static const double gw[8] = {0.1012285363, 0.2223810345, 0.3137066459, 0.3626837834,
                                 0.3626837834, 0.3137066459, 0.2223810345, 0.1012285363};
    double total = 0;
    for (int g = 0; g < 8; ++g) {
        const double th = 0.25 * kPi * (gx[g] + 1.0); // 0 … π/2
        double cs[12], sn[12];
        farSums(b, k, kw * std::sin(th), cs, sn);
        double ring = 0;
        for (int m = 0; m < 12; ++m) {
            const double amp = kTwoPi * jm[g][m];
            ring += amp * amp * (m == 0 ? kTwoPi * cs[0] * cs[0] : kPi * (cs[m] * cs[m] + sn[m] * sn[m]));
        }
        total += gw[g] * 0.25 * kPi * ring * std::sin(th);
    }
    return total;
}
inline double gaussElevation(int g) {
    static const double gx[8] = {-0.9602898565, -0.7966664774, -0.5255324099, -0.1834346425,
                                 0.1834346425,  0.5255324099,  0.7966664774,  0.9602898565};
    return 0.25 * kPi * (gx[g] + 1.0);
}

inline Built buildHead(const Design &d, int keep, int drum) {
    Built b;
    b.design = d;
    b.s = solve(d, 8, 7, keep);
    b.n = b.s.modes;
    const int nb = int(b.s.basis.size());
    // No head is tensioned perfectly evenly: each degenerate pair of a
    // centred head splits by a few cents, its nodal lines turned to where
    // the straps pull (Raman's nodal line rocking to and fro, Courtney's
    // doublet in an out-of-tune na).
    if (std::fabs(d.offset) < 1e-9) {
        Rng rng(0x7ab1au);
        for (int k = 0; k + 1 < b.n; ++k) {
            if (b.s.order[size_t(k)] == 0 || b.s.order[size_t(k + 1)] != b.s.order[size_t(k)] ||
                std::fabs(b.s.lambda[size_t(k + 1)] / b.s.lambda[size_t(k)] - 1.0) > 1e-6)
                continue;
            const int m = b.s.order[size_t(k)];
            const double psi = kTwoPi * rng.unit() / m, c = std::cos(m * psi), sn = std::sin(m * psi);
            double *p = &b.s.coef[size_t(k) * size_t(nb)], *q = &b.s.coef[size_t(k + 1) * size_t(nb)];
            for (int i = 0; i < nb; ++i) {
                const double a0 = p[i], b0 = q[i];
                p[i] = c * a0 + sn * b0;
                q[i] = -sn * a0 + c * b0;
            }
            const double cents = Knobs::split * (0.3 + 0.7 * rng.unit());
            b.s.lambda[size_t(k)] *= std::exp2(-cents / 1200.0);
            b.s.lambda[size_t(k + 1)] *= std::exp2(cents / 1200.0);
            ++k;
        }
    }
    const double a = d.radius;
    // Radial tables of every basis function.
    std::vector<double> rt(size_t(nb) * size_t(Built::kNr + 1));
    for (int i = 0; i < nb; ++i)
        for (int ir = 0; ir <= Built::kNr; ++ir) {
            const auto &bs = b.s.basis[size_t(i)];
            rt[size_t(i) * size_t(Built::kNr + 1) + size_t(ir)] =
                bs.norm * besselJ(bs.m, bs.kappa * a * double(ir) / Built::kNr);
        }
    b.table.assign(size_t(b.n) * size_t((Built::kNr + 1) * Built::kNt), 0.0f);
    for (int k = 0; k < b.n; ++k) {
        const double *c = &b.s.coef[size_t(k) * size_t(nb)];
        float *t = &b.table[size_t(k) * size_t((Built::kNr + 1) * Built::kNt)];
        for (int i = 0; i < nb; ++i) {
            if (c[i] == 0)
                continue;
            const auto &bs = b.s.basis[size_t(i)];
            for (int it = 0; it < Built::kNt; ++it) {
                const double th = kTwoPi * double(it) / Built::kNt;
                const double ang = bs.m == 0 ? 1.0 : bs.sine ? std::sin(bs.m * th) : std::cos(bs.m * th);
                for (int ir = 0; ir <= Built::kNr; ++ir)
                    t[ir * Built::kNt + it] += float(c[i] * ang * rt[size_t(i) * size_t(Built::kNr + 1) + size_t(ir)]);
            }
        }
        b.termFrom.push_back(int(b.terms.size()));
        for (int i = 0; i < nb; ++i) {
            if (std::fabs(c[i]) < 1e-12)
                continue;
            const auto &bs = b.s.basis[size_t(i)];
            Built::Term term;
            term.m = bs.m;
            term.sine = bs.sine;
            term.w = c[i] * bs.norm * a * bs.kappa * besselJ(bs.m + 1, bs.kappa * a);
            term.kappa2 = bs.kappa * bs.kappa;
            b.terms.push_back(term);
        }
    }
    b.termFrom.push_back(int(b.terms.size()));
    // The far field against the wavenumber: its power over the half space
    // (the radiation damping) and toward each microphone.
    b.power.assign(size_t(b.n) * Built::kKw, 0.0f);
    for (int e = 0; e < 2; ++e) {
        b.fRe[e].assign(size_t(b.n) * Built::kKw, 0.0f);
        b.fIm[e].assign(size_t(b.n) * Built::kKw, 0.0f);
    }
    double mdist[2], mth[2], mphi[2];
    for (int e = 0; e < 2; ++e)
        micFrom(drum, e, mdist[e], mth[e], mphi[e]);
    for (int j = 0; j < Built::kKw; ++j) {
        const double kw = Built::kwAt(j);
        double jg[8][12], je[2][12];
        for (int g = 0; g < 8; ++g)
            for (int m = 0; m < 12; ++m)
                jg[g][m] = besselJ(m, kw * std::sin(gaussElevation(g)) * a);
        for (int e = 0; e < 2; ++e)
            for (int m = 0; m < 12; ++m)
                je[e][m] = besselJ(m, kw * std::sin(mth[e]) * a);
        for (int k = 0; k < b.n; ++k) {
            b.power[size_t(k) * Built::kKw + size_t(j)] = float(farPower(b, k, kw, jg));
            for (int e = 0; e < 2; ++e) {
                double re, im;
                farField(b, k, kw * std::sin(mth[e]), mphi[e], je[e], re, im);
                b.fRe[e][size_t(k) * Built::kKw + size_t(j)] = float(re);
                b.fIm[e][size_t(k) * Built::kKw + size_t(j)] = float(im);
            }
        }
    }
    return b;
}

constexpr int kDayanModes = 44, kBayanModes = 36;

inline const Built &built(int drum) {
    static const Built d = buildHead(Physics::dayan(), kDayanModes, Dayan);
    static const Built b = buildHead(Physics::bayan(), kBayanModes, Bayan);
    return drum == Bayan ? b : d;
}

// --- the body ---------------------------------------------------------------------------------------------

class Body {
public:
    static constexpr int kMax = 48;
    static constexpr int kStrikes = 12;
    static constexpr int kRests = 16;
    static constexpr int kDelay = 64;
    static constexpr int kBendSteps = 12;

    void init(float rate) {
        m_rate = rate;
        m_dt = 1.0f / rate;
        for (int d = 0; d < 2; ++d) {
            const Built &b = built(d);
            Head &h = m_head[d];
            h.n = b.n;
            h.live = b.n;
            h.b = &b;
            for (int k = 0; k < h.n; ++k) {
                h.mass[k] = float(b.s.mass[size_t(k)]);
                h.vol[k] = float(b.s.volume[size_t(k)]);
                h.lm[k] = float(b.s.lambda[size_t(k)] * b.s.mass[size_t(k)]);
            }
            h.kCav = float(Physics::kRho * Physics::kC * Physics::kC / Physics::kVolume[d]);
            // The (1,1) that sweeps no volume: on the dayan the pair's lower
            // member, on the bayan the one antisymmetric about the syahi's
            // diameter (the heel lies on that diameter, on its node).
            h.pitchMode = 0;
            for (int k = h.n - 1; k >= 0; --k)
                if (b.s.order[size_t(k)] == 1 && std::fabs(b.s.volume[size_t(k)]) < 1e-9 && b.s.radial[size_t(k)] == 1)
                    h.pitchMode = k;
            const double A = kPi * b.design.radius * b.design.radius;
            h.area = float(A);
            // The skin's stretch: the area a swing adds, ½∫|∇w|², shared
            // equally both ways in the plane, is a strain of ∫|∇w|²/(4A)
            // each way, and ΔT = E·h/(1−ν) times that.
            h.stretch = float(Physics::kSkinE * Physics::kSkinThick / (1.0 - Physics::kSkinNu) / (4.0 * A));
            // Each microphone: how far and how late (which way is in the
            // far field's tables).
            for (int e = 0; e < 2; ++e) {
                double dist, theta, phi;
                micFrom(d, e, dist, theta, phi);
                h.micDist[e] = float(dist);
                h.micDelay[e] = float(dist / Physics::kC * rate);
            }
        }
        // Delays relative to the nearest path.
        float least = 1e9f;
        for (auto &h : m_head)
            least = std::min({least, h.micDelay[0], h.micDelay[1]});
        for (auto &h : m_head)
            for (int e = 0; e < 2; ++e)
                h.micDelay[e] = std::clamp(h.micDelay[e] - least, 0.0f, float(kDelay - 2));
        m_head[Dayan].f0 = m_head[Bayan].f0 = 0;
        tune(277.2f, 92.4f);
        silence();
    }

    // The two heads' pitches: the dayan's Sa (its (1,1) an octave above),
    // the bayan's open ge.
    void tune(float dayanHz, float bayanHz) {
        const float want[2] = {std::clamp(dayanHz, 150.0f, 500.0f), std::clamp(bayanHz, 50.0f, 200.0f)};
        for (int d = 0; d < 2; ++d)
            if (std::fabs(want[d] - m_head[d].f0) > m_head[d].f0 * 1e-5f) {
                m_head[d].f0 = want[d];
                retune(d);
            }
    }

    void setDecay(float powerOfTwo) {
        const float s = std::exp2(std::clamp(powerOfTwo, -3.0f, 3.0f));
        if (std::fabs(s - m_decayScale) < 1e-4f)
            return;
        m_decayScale = s;
        damp(0);
        damp(1);
    }

    // A stroke: where on its drum (x, y in units of the head's radius, x
    // to the player's right, y away from the player), how hard (0..1), the
    // finger's hardness (−1 the soft pad … 1 the hard tip), samples into
    // the next block. A finger that stays (id ≠ 0) can rest on the skin
    // later (move) until it lifts.
    void strike(int stroke, float vel, float x, float y, float tone, int delay, int id = 0) {
        stroke = std::clamp(stroke, 0, kStrokes - 1);
        const int d = drumOf(stroke);
        Head &h = m_head[d];
        // A stroke is where it is played: a na struck in the middle of the
        // syahi (from somewhere that only knows the pad) is played on the
        // kinar.
        if (strokeAt(d, x, y) != stroke || x * x + y * y > 1.0f)
            strokeHome(stroke, x, y);
        vel = std::clamp(vel, 0.02f, 1.0f);
        tone = std::clamp(tone, -1.0f, 1.0f);
        delay = std::max(0, delay);
        // The hand moves to this stroke: what it left resting on this drum
        // lifts (the fingers that are still held stay).
        for (auto &r : m_rests)
            if (r.on && r.drum == d && r.id == 0)
                r.lift(delay);
        const Finger f = finger(stroke);
        Contact *c = freeContact();
        *c = Contact{};
        c->on = true;
        c->drum = d;
        c->delay = delay;
        c->mass = f.mass;
        c->pulp = f.pulp * std::exp2(0.4f * tone);
        c->hc = f.hc;
        c->v = f.speed * (0.06f + 0.94f * vel);
        c->retract = c->v / f.retract;
        {
            // (the skin's mass where it lands, in the head's own frame: the
            // bayan's syahi is solved along −x)
            float fx = x, fy = y;
            toFrame(h, fx, fy);
            c->sigma = float(sigmaAt(h.b->design, fx, fy));
        }
        c->pan = x;
        patch(h, x, y, f.patch, c->phi);
        // The other fingers each stroke rests on the skin.
        switch (stroke) {
        case Na:
        case Tin: {
            // The ring finger lightly on the syahi's edge, already there as
            // the index finger strikes, a hand's span round from it toward
            // the player: it holds a nodal line there, so of each pair of
            // modes the stroke wakes, the one with its node under the
            // finger rings on.
            const float r = std::sqrt(x * x + y * y);
            const float a0 = r > 1e-3f ? std::atan2(y, x) : -0.5f * kPi;
            const float a1 = std::sin(a0 + Knobs::ringAngle) < std::sin(a0 - Knobs::ringAngle) ? a0 + Knobs::ringAngle
                                                                                            : a0 - Knobs::ringAngle;
            const float at = float(h.b->design.syahi) * Knobs::ringAt;
            Rest *q = rest(d, 0, std::cos(a1) * at, std::sin(a1) * at, Knobs::ringSize, Knobs::ringK,
                           stroke == Tin ? Knobs::ringCTin : Knobs::ringC, 2.0f, delay);
            q->engage = q->want = 1.0f;
            q->delay = 0;
            break;
        }
        case Ge:
            // The heel of the palm rests at the near edge.
            heelOn(id, delay, 0.0f);
            break;
        case Ka:
            // The hand stays flat across the skin: the palm where it
            // landed, the fingers over the syahi.
            rest(d, id, x, y, 0.035f, 4000.0f, 60.0f, id ? -1.0f : 0.3f, delay + int(0.004f * m_rate));
            rest(d, id, 0.5f * x, 0.5f * y + 0.2f, 0.035f, 4000.0f, 60.0f, id ? -1.0f : 0.3f, delay + int(0.006f * m_rate));
            break;
        default: break;
        }
        // A finger that stays: it may rest where it is (move).
        if (id != 0 && stroke != Ge && stroke != Ka) {
            Rest *r = rest(d, id, x, y, f.patch, Knobs::restK, Knobs::restC, -1.0f, delay, true);
            r->engage = r->want = 0.0f; // not yet: only once it is held
            r->latent = true;
        }
        h.sleeping = false;
        h.quietRests = false;
        h.live = h.n;
        m_touching = true;
    }

    // A held finger: where it is now, how far it has pressed on (0..1: on
    // the bayan the heel pushing into the skin), and whether it has stayed
    // long enough to rest there.
    void move(int id, float x, float y, float press, bool resting) {
        if (id == 0)
            return;
        for (auto &r : m_rests) {
            if (!r.on || r.id != id)
                continue;
            if (r.heel) {
                r.press = std::clamp(press, 0.0f, 1.0f);
                continue;
            }
            if (resting && r.want < 1.0f) {
                r.want = 1.0f;
                r.latent = false;
            }
            if (std::fabs(x - r.x) + std::fabs(y - r.y) > 0.02f) {
                r.x = x;
                r.y = y;
                patch(m_head[r.drum], x, y, r.size, r.phi);
                reach(m_head[r.drum], r);
            }
        }
    }

    // A finger leaves.
    void lift(int id) {
        if (id == 0)
            return;
        for (auto &r : m_rests)
            if (r.on && r.id == id)
                r.lift(0);
    }

    void render(float *l, float *r, int n) {
        for (int d = 0; d < 2; ++d)
            if (!m_head[d].sleeping)
                renderHead(d, l, r, n);
        bool touching = false;
        for (auto &c : m_contacts)
            touching |= c.on;
        for (auto &q : m_rests)
            touching |= q.on;
        m_touching = touching;
    }

    // A hand laid flat on both: everything dies within about `seconds`.
    void mute(float seconds) {
        for (int d = 0; d < 2; ++d) {
            Head &h = m_head[d];
            if (h.sleeping)
                continue;
            const float dm = std::exp(-2.0f / (std::max(seconds, 0.001f) * 0.25f * m_rate));
            h.muteD = dm;
            for (int k = 0; k < h.n; ++k)
                h.d[k] = std::min(h.d[k], dm);
        }
    }

    void silence() {
        for (int d = 0; d < 2; ++d) {
            Head &h = m_head[d];
            if (h.muteD < 1.0f) {
                h.muteD = 1.0f;
                damp(d);
            }
            for (int k = 0; k < kMax; ++k)
                h.x[k] = h.u[k] = h.f[k] = 0;
            h.qa[0] = h.qa[1] = 0;
            h.U[0] = h.U[1] = h.U[2] = 0;
            for (int e = 0; e < 2; ++e)
                for (int i = 0; i < kDelay; ++i)
                    h.line[e][i] = 0;
            h.tick.reset();
            h.heel = 0;
            h.sleeping = true;
            h.energy = 0;
            h.quiet = 0;
        }
        for (auto &c : m_contacts)
            c.on = false;
        for (auto &q : m_rests)
            q.on = false;
        m_touching = false;
    }

    bool active() const { return !m_head[0].sleeping || !m_head[1].sleeping; }
    // Loudness of a drum, roughly 0..1, for drawing.
    float level(int d) const {
        const Head &h = m_head[std::clamp(d, 0, 1)];
        return h.sleeping ? 0.0f : std::sqrt(std::max(0.0f, h.energy) / h.refEnergy);
    }
    // How far the heel has bent the bayan up, in semitones.
    float bend() const {
        const Head &h = m_head[Bayan];
        if (h.sleeping || h.heel <= 0)
            return 0.0f;
        for (int j = 1; j <= kBendSteps; ++j)
            if (h.heel <= h.bendEps[j])
                return float(j - 1) + (h.heel - h.bendEps[j - 1]) / std::max(1e-9f, h.bendEps[j] - h.bendEps[j - 1]);
        return float(kBendSteps);
    }
    float rate() const { return m_rate; }

    // --- for tests and the docs -------------------------------------------------------------------------

    int modes(int d) const { return m_head[std::clamp(d, 0, 1)].n; }
    // The drum's k-th lowest mode with its shell's air (Hz), and each of
    // the head's own modes as the air stiffens it alone.
    float heardHz(int d, int k) const { return m_head[std::clamp(d, 0, 1)].heard[std::clamp(k, 0, kMax - 1)]; }
    float modeHz(int d, int k) const { return m_head[std::clamp(d, 0, 1)].hz[std::clamp(k, 0, kMax - 1)]; }
    float modeTau(int d, int k) const { return m_head[std::clamp(d, 0, 1)].tau[std::clamp(k, 0, kMax - 1)]; }
    int modeM(int d, int k) const { return built(d).s.order[size_t(std::clamp(k, 0, built(d).n - 1))]; }
    int modeN(int d, int k) const { return built(d).s.radial[size_t(std::clamp(k, 0, built(d).n - 1))]; }
    float tension(int d) const { return m_head[std::clamp(d, 0, 1)].T; }
    // A mode of the head as it would ring with no shell under it (Hz).
    float membraneHz(int d, int k) const {
        const Head &h = m_head[std::clamp(d, 0, 1)];
        return float(std::sqrt(h.T * h.b->s.lambda[size_t(std::clamp(k, 0, h.n - 1))]) / kTwoPi);
    }
    // How deep the heel presses the bayan's skin (m) to bend the ge an octave.
    float octaveDepth() const { return heelDepth(m_head[Bayan]); }
    float lastContact() const { return m_lastContact; }

private:
    struct Finger {
        float mass, pulp, hc, speed, retract, patch;
    };
    // The striking hand for each stroke: the mass behind it, its pulp's
    // stiffness (N/m^1.5) and loss (Hunt–Crossley, s/m), how fast it
    // comes at full strength (m/s), how quickly it pulls back (s), and the
    // radius of skin it covers (m).
    static Finger finger(int stroke) {
        switch (stroke) {
        case Na: return {0.004f, 6.0e5f, 0.4f, 3.3f, 0.0035f, 0.005f};  // the index finger's side, flicked
        case Tin: return {0.005f, 4.0e5f, 0.5f, 2.6f, 0.004f, 0.006f}; // the index finger's tip, on the maidan
        case Tun: return {0.005f, 1.0e6f, 0.5f, 1.25f, 0.004f, 0.007f}; // the index finger's pad, on the syahi
        case Ge: return {0.018f, 1.0e5f, 0.8f, 2.2f, 0.008f, 0.012f};  // the middle and ring fingers, fleshy
        default: return {0.05f, 1.0e5f, 1.2f, 1.4f, 0.012f, 0.03f};    // the palm
        }
    }

    struct Head {
        int n = 0;
        const Built *b = nullptr;
        alignas(32) float x[kMax] = {}, u[kMax] = {}, f[kMax] = {};
        alignas(32) float d[kMax] = {}, c[kMax] = {}, inv[kMax] = {};
        alignas(32) float K[kMax] = {}, vol[kMax] = {}, lm[kMax] = {};
        alignas(32) float invK[kMax] = {}, invVol[kMax] = {}; // K and the volume, over M·rate²
        alignas(32) float aL[kMax] = {}, vL[kMax] = {}, aR[kMax] = {}, vR[kMax] = {};
        alignas(32) float loud[kMax] = {};
        float mass[kMax] = {}, hz[kMax] = {}, tau[kMax] = {}, heard[kMax] = {};
        float U[3] = {}; // the net volume velocity, the last three samples (m³ per sample)
        float kCav = 0, area = 0, stretch = 0, T = 0, f0 = 0;
        int pitchMode = 0; // the mode tuned to the drum's pitch
        int live = kMax;   // the modes still ringing (the rest are let go until the next stroke)
        int restN = kMax;  // the modes a resting finger damps: those that ring longer than a tenth of a second
        float bendEps[kBendSteps + 1] = {}; // the skin's added strain for 0, 1 … 12 semitones of bend
        float radiate = 0; // ρω₁²/(2πc)·rate: the radiation resistance on the net volume, per U
        float heel = 0;
        float micDist[2] = {}, micDelay[2] = {};
        float cosTh[kMax] = {}; // cos of each oscillator's step
        float qa[2] = {};
        float line[2][kDelay] = {};
        int wr = 0;
        float tickGain = 0;
        Svf tick;
        float muteD = 1.0f;
        float energy = 0, refEnergy = 1, sleepEnergy = 0;
        int quiet = 0;
        bool sleeping = true;
        bool quietRests = false; // rung so far down that the fingers on it need not damp it
    };

    struct Contact {
        bool on = false;
        int drum = 0, delay = 0, age = 0, touched = 0;
        float z = 0, v = 0, mass = 0.005f, pulp = 3e5f, hc = 0.5f, retract = 500.0f;
        float sigma = 0.27f, pan = 0;
        float phi[kMax] = {};
    };

    struct Rest {
        bool on = false, heel = false;
        bool latent = false; // a finger down but not yet resting: waits for its move
        int drum = 0, id = 0, delay = 0;
        float x = 0, y = 0, size = 0.008f;
        float k = 1000.0f, c = 2.0f;
        float engage = 0, want = 1; // 0 lifted … 1 resting fully
        int life = -1;              // samples until it lifts by itself, or −1
        float press = 0;            // the heel: how far it pushes in, 0..1
        float Q = 0;                // Σφ²/(M·rate²): how its force moves the skin under it
        alignas(32) float phi[kMax] = {};
        void lift(int after) {
            want = 0;
            latent = false;
            life = -1;
            delay = std::max(delay, after);
        }
    };

    Contact *freeContact() {
        for (auto &c : m_contacts)
            if (!c.on)
                return &c;
        Contact *o = &m_contacts[0];
        for (auto &c : m_contacts)
            if (c.age > o->age)
                o = &c;
        return o;
    }

    Rest *rest(int d, int id, float x, float y, float size, float k, float c, float seconds, int delay, bool again = false) {
        Rest *r = nullptr;
        // (a finger struck again rests again where it now is: one rest each)
        if (again && id != 0)
            for (auto &q : m_rests)
                if (q.on && q.id == id && !q.heel) {
                    r = &q;
                    break;
                }
        if (!r)
            for (auto &q : m_rests)
                if (!q.on) {
                    r = &q;
                    break;
                }
        if (!r) {
            r = &m_rests[0];
            for (auto &q : m_rests)
                if (q.engage < r->engage)
                    r = &q;
        }
        *r = Rest{};
        r->on = true;
        r->drum = d;
        r->id = id;
        r->delay = delay;
        r->x = x;
        r->y = y;
        r->size = size;
        r->k = k;
        r->c = c;
        r->life = seconds > 0 ? int(seconds * m_rate) + delay : -1;
        patch(m_head[d], x, y, size, r->phi);
        reach(m_head[d], *r);
        m_head[d].sleeping = false;
        return r;
    }

    // The heel of the left hand on the bayan, at the near edge where it
    // rests; held (id ≠ 0) it can press in.
    void heelOn(int id, int delay, float press) {
        for (auto &q : m_rests)
            if (q.on && q.heel) {
                q.id = id;
                q.press = press;
                q.want = 1;
                q.life = id ? -1 : int(2.0f * m_rate);
                return;
            }
        Rest *r = rest(Bayan, id, 0.0f, -Knobs::heelAt, Knobs::heelSize, Knobs::heelK, Knobs::heelC, id ? -1.0f : 2.0f, delay);
        r->heel = true;
        r->press = press;
    }

    // How a resting finger's force moves the skin under it.
    static void reach(const Head &h, Rest &r) {
        float q = 0;
        for (int k = 0; k < h.n; ++k)
            q += r.phi[k] * r.phi[k] * h.inv[k];
        r.Q = q;
    }

    // A finger's patch of skin: each mode's shape averaged over it.
    void patch(const Head &h, float x, float y, float size, float *phi) const {
        const float s = size / float(h.b->design.radius);
        static const float ox[7] = {0, 1, 0.5f, -0.5f, -1, -0.5f, 0.5f};
        static const float oy[7] = {0, 0, 0.866f, 0.866f, 0, -0.866f, -0.866f};
        // In the head's own frame (the bayan's turned so its syahi sits
        // away from the player).
        float hx = x, hy = y;
        toFrame(h, hx, hy);
        for (int k = 0; k < kMax; ++k)
            phi[k] = 0;
        for (int j = 0; j < 7; ++j)
            h.b->shapesAt(hx + 0.6f * s * ox[j], hy + 0.6f * s * oy[j], j == 0 ? 0.25f : 0.125f, phi);
    }

    void toFrame(const Head &h, float &x, float &y) const {
        if (&h == &m_head[Bayan]) {
            // The solved bayan has its syahi along −x; on the drum it sits
            // away from the player (+y): (x, y) on the drum is (−y, x) here.
            const float px = x, py = y;
            x = -py;
            y = px;
        }
    }

    // Everything that follows from a head's tension: its modes' stiffness,
    // what they radiate and lose, what the microphones hear. Lookups and
    // sums only: it runs on the audio thread when the key changes.
    void retune(int d) {
        Head &h = m_head[d];
        const Built &b = *h.b;
        const double rate = m_rate;
        // The tension that puts the drum's pitch where it is wanted. A
        // player tunes the dayan by its (1,1), the 2nd harmonic of Sa (na
        // with the little finger along a diameter), which sweeps no volume,
        // so the shell's air doesn't touch it: T = ω²/λ. The bayan's ge is
        // its lowest mode, the syahi riding on the skin and the shell's air
        // together: the air couples every mode that sweeps a volume (a
        // rank-one stiffness, ρc²/V·v·vᵀ), so the head's own modes are the
        // poles of a secular equation and the drum's are its roots; the
        // tension is found that puts the lowest root on the ge.
        const float oldT = h.T;
        if (d == Dayan) {
            h.T = float(std::pow(kTwoPi * double(h.f0) * 2.0, 2.0) / b.s.lambda[size_t(h.pitchMode)]);
        } else {
            const double want = std::pow(kTwoPi * double(h.f0), 2.0);
            double lo = 10.0, hi = 1.0e5;
            for (int it = 0; it < 40; ++it) {
                const double mid = std::sqrt(lo * hi);
                (lowestRoot(h, mid) < want ? lo : hi) = mid;
            }
            h.T = float(std::sqrt(lo * hi));
            // How far the heel must stretch the skin for each semitone of
            // bend, up to the octave a heel can bring a ge up.
            h.bendEps[0] = 0;
            for (int j = 1; j <= kBendSteps; ++j) {
                const double ratio = std::exp2(double(j) / 12.0);
                double a = h.bendEps[j - 1], z = 4.0;
                for (int it = 0; it < 30; ++it) {
                    const double mid = 0.5 * (a + z);
                    (lowestRoot(h, h.T * (1.0 + mid)) < want * ratio * ratio ? a : z) = mid;
                }
                h.bendEps[j] = float(0.5 * (a + z));
            }
        }
        // A head retuned while it rings keeps its energy: each mode's swing
        // shrinks as its stiffness grows.
        if (oldT > 0 && !h.sleeping) {
            const float keep = std::sqrt(oldT / h.T);
            for (int k = 0; k < h.n; ++k)
                h.x[k] *= keep;
        }
        double roots[kMax];
        allRoots(h, h.T, roots);
        for (int k = 0; k < h.n; ++k)
            h.heard[k] = float(std::sqrt(roots[k]) / kTwoPi);
        // What the head's net volume radiates (ρω²/2πc at the lowest mode)
        // and leaks through the lacing.
        const double w1 = kTwoPi * h.heard[0];
        const double rad1 = Physics::kRho * w1 * w1 / (kTwoPi * Physics::kC); // kg/(m⁴·s)
        h.radiate = float((rad1 + Knobs::leak[d]) * rate);
        for (int k = 0; k < h.n; ++k) {
            const double kMem = h.T * b.s.lambda[size_t(k)] * h.mass[k];
            h.K[k] = float(kMem);
            // Heard with the cavity's stiffness (its own share, for the
            // radiation and the decay; the coupling itself is a force).
            const double w2 = (kMem + h.kCav * double(h.vol[k]) * h.vol[k]) / h.mass[k];
            const double w = std::sqrt(w2);
            h.hz[k] = float(w / kTwoPi);
            // Radiation: R = ρω²/(4π²c)·∫|F|²dΩ, its rate R/2M; less what
            // the whole head's net volume radiates together (renderHead).
            const double kw = w / Physics::kC;
            const double R = Physics::kRho * w2 / (4.0 * kPi * kPi * Physics::kC) * b.lookup(b.power, k, kw);
            const double aRad = std::max(0.0, R - rad1 * double(h.vol[k]) * h.vol[k]) / (2.0 * h.mass[k]);
            // The skin's, the syahi's and the kinar's own losses.
            const double f1k = w / (kTwoPi * 1000.0);
            const double aMat = 0.5 * w * Knobs::eta[d] + Knobs::high * f1k * f1k * f1k;
            h.tau[k] = float(1.0 / (aRad + aMat));
            // The oscillator steps at the membrane's own stiffness (the
            // cavity's comes in as a force).
            h.cosTh[k] = float(std::cos(std::min(std::sqrt(kMem / h.mass[k]), 0.45 * kTwoPi * rate) / rate));
            h.inv[k] = float(1.0 / (rate * rate * h.mass[k]));
            h.invK[k] = float(kMem / (rate * rate * h.mass[k]));
            h.invVol[k] = h.inv[k] * h.vol[k];
            // What each microphone hears of it: p = ρ/(2πr)·(Re F·ẍ − Im F·ω·ẋ).
            for (int e = 0; e < 2; ++e) {
                const double g = Physics::kRho / (kTwoPi * h.micDist[e]) * Knobs::level;
                (e ? h.aR : h.aL)[k] = float(g * b.lookup(b.fRe[e], k, kw) * rate * rate);
                (e ? h.vR : h.vL)[k] = float(-g * b.lookup(b.fIm[e], k, kw) * w * rate);
            }
            h.loud[k] = float(w2 * h.mass[k]);
        }
        // A resting finger damps the modes that would ring on: up to the last
        // that rings longer than 0.1 s by itself (in fours, for the vector
        // unit).
        h.restN = 8;
        for (int k = 0; k < h.n; ++k)
            if (6.91f * h.tau[k] >= 0.1f)
                h.restN = std::max(h.restN, k + 1);
        h.restN = std::min(h.n, (h.restN + 3) / 4 * 4);
        // A full-strength tun's amplitude (0.5 mm) for the drawing's scale.
        h.refEnergy = float(h.loud[0] * 0.0005 * 0.0005 * 0.5);
        h.sleepEnergy = h.refEnergy * 1e-12f; // asleep 120 dB under it
        h.tick.set(float(std::min(0.42 * rate, 1.15 * double(h.hz[h.n - 1]))), 0.6f, float(rate));
        h.tickGain = float(Physics::kRho / (kTwoPi * 0.5) * Knobs::level * Knobs::tick);
        damp(d);
    }

    // The oscillators' decay: exact frequency and time constant, u' = d·u −
    // c·x has poles r·e^{±iθ} for d = r², c = 1 + r² − 2r·cos θ.
    void damp(int d) {
        Head &h = m_head[d];
        for (int k = 0; k < h.n; ++k) {
            const double r = std::exp(-1.0 / (double(h.tau[k]) * m_decayScale * m_rate));
            h.d[k] = float(std::min(r * r, double(h.muteD)));
            h.c[k] = float(1.0 + r * r - 2.0 * r * h.cosTh[k]);
        }
    }

    void renderHead(int d, float *outL, float *outR, int n) {
        Head &h = m_head[d];
        const int N = h.live;
        float *x = h.x, *u = h.u, *f = h.f;
        const float kCav = h.kCav, G = h.stretch / std::max(h.T, 1.0f);
        for (int i = 0; i < n; ++i) {
            // The shell's air and the skin's stretch, from every mode.
            float dv = 0, S = 0;
            for (int k = 0; k < N; ++k) {
                dv += h.vol[k] * x[k];
                S += h.lm[k] * x[k] * x[k];
            }
            // The heel eases toward where it is pressed (a hand moves in
            // tens of milliseconds).
            float eps = G * S;
            if (d == Bayan) {
                float want = 0;
                for (auto &q : m_rests)
                    if (q.on && q.heel)
                        want = std::max(want, q.engage * heelStrain(h, q.press));
                h.heel += (want - h.heel) * (1.0f / (0.03f * m_rate));
                eps += h.heel;
            }
            // The pressure on the head: the shell's air inside, and outside
            // the air its net volume radiates into pushing back, a small
            // source's radiation resistance ρω²/2πc on its volume velocity
            // (taken at the drum's lowest mode, where it matters).
            const float p = -kCav * dv - h.radiate * h.U[0];
            // The fingers striking it (their forces into f), then every
            // mode a step: u' = d·u − c·x + (A·p − ε·K·x + f)/(M·rate²).
            float tick = 0;
            const bool struck = contacts(d, f, tick);
            if (struck)
                for (int k = 0; k < N; ++k)
                    u[k] = h.d[k] * u[k] - (h.c[k] + eps * h.invK[k]) * x[k] + h.invVol[k] * p + h.inv[k] * f[k];
            else
                for (int k = 0; k < N; ++k)
                    u[k] = h.d[k] * u[k] - (h.c[k] + eps * h.invK[k]) * x[k] + h.invVol[k] * p;
            restOn(d);
            float qaL = 0, qaR = 0, qvL = 0, qvR = 0, U = 0;
            for (int k = 0; k < N; ++k) {
                x[k] += u[k];
                U += h.vol[k] * u[k];
                qaL += h.aL[k] * u[k];
                qaR += h.aR[k] * u[k];
                qvL += h.vL[k] * u[k];
                qvR += h.vR[k] * u[k];
            }
            h.U[2] = h.U[1];
            h.U[1] = h.U[0];
            h.U[0] = U;
            const float t = h.tick.step(tick).high * h.tickGain;
            const float sL = (qaL - h.qa[0]) + qvL + t, sR = (qaR - h.qa[1]) + qvR + t;
            h.qa[0] = qaL;
            h.qa[1] = qaR;
            h.wr = (h.wr + 1) & (kDelay - 1);
            h.line[0][h.wr] = sL;
            h.line[1][h.wr] = sR;
            outL[i] += tap(h.line[0], h.wr, h.micDelay[0]);
            outR[i] += tap(h.line[1], h.wr, h.micDelay[1]);
        }
        float e = 0;
        int top = 0;
        for (int k = 0; k < N; ++k) {
            const float th = kTwoPi * h.hz[k] / m_rate;
            const float ek = h.loud[k] * 0.5f * (x[k] * x[k] + (u[k] / th) * (u[k] / th));
            e += ek;
            if (ek > h.refEnergy * 1e-10f)
                top = k + 1;
        }
        h.energy = e;
        // Asleep once it has rung out with no finger striking it; a finger
        // still held on it stays (and damps it again when it is struck).
        bool striking = false;
        for (auto &c : m_contacts)
            striking |= c.on && c.drum == d;
        // The modes above the last one still ringing (100 dB under a full
        // tun) are let go: high modes die in tenths of a second, the low
        // ones ring on.
        // A stroke wakes them all again.
        if (!striking) {
            const int live = std::min(N, std::max(std::min(h.n, 8), (top + 3) / 4 * 4));
            for (int k = live; k < N; ++k)
                x[k] = u[k] = 0;
            h.live = live;
        }
        h.quietRests = !striking && e < h.sleepEnergy * 1e3f;
        if (!striking && e < h.sleepEnergy) {
            if (++h.quiet > 8) {
                for (int k = 0; k < kMax; ++k)
                    x[k] = u[k] = 0;
                h.U[0] = h.U[1] = h.U[2] = 0;
                for (auto &q : m_rests)
                    if (q.drum == d && q.id == 0)
                        q.on = false;
                h.sleeping = true;
                h.heel = 0;
            }
        } else {
            h.quiet = 0;
        }
    }

    // The drum's ω² with its shell's air, at tension T: the roots of
    // 1 + ρc²/V·Σ wⱼ²/(Tλⱼ − μ) = 0, wⱼ = volⱼ/√Mⱼ, one between each two
    // poles (the head's own modes that sweep a volume) and the last within
    // ρc²/V·Σw² past the last pole; modes that sweep none keep their own.
    static void allRoots(const Head &h, double T, double *out) {
        int n = 0, np = 0;
        double poles[kMax], w2[kMax], sum = 0;
        for (int j = 0; j < h.n; ++j) {
            const double lam = T * h.b->s.lambda[size_t(j)];
            const double w = double(h.vol[j]) * h.vol[j] / h.mass[j];
            if (w < 1e-14) {
                out[n++] = lam;
            } else {
                poles[np] = lam;
                w2[np++] = w;
                sum += w;
            }
        }
        for (int p = 0; p < np; ++p) {
            double a = poles[p], b = p + 1 < np ? poles[p + 1] : poles[p] + h.kCav * sum + 1.0;
            for (int it = 0; it < 50; ++it) {
                const double mid = 0.5 * (a + b);
                double g = 1.0;
                for (int j = 0; j < np; ++j)
                    g += h.kCav * w2[j] / (poles[j] - mid);
                (g < 0 ? a : b) = mid;
            }
            out[n++] = 0.5 * (a + b);
        }
        std::sort(out, out + n);
    }
    // Only the lowest, for tuning: the first root, or a mode below it that
    // sweeps no volume.
    static double lowestRoot(const Head &h, double T) {
        double lowestFree = 1e300, poles[kMax], w2[kMax];
        int np = 0;
        for (int j = 0; j < h.n; ++j) {
            const double lam = T * h.b->s.lambda[size_t(j)];
            const double w = double(h.vol[j]) * h.vol[j] / h.mass[j];
            if (w < 1e-14)
                lowestFree = std::min(lowestFree, lam);
            else {
                poles[np] = lam;
                w2[np++] = w;
            }
        }
        if (!np)
            return lowestFree;
        double sum = 0;
        for (int j = 0; j < np; ++j)
            sum += w2[j];
        double a = poles[0], b = np > 1 ? poles[1] : poles[0] + h.kCav * sum + 1.0;
        for (int it = 0; it < 40; ++it) {
            const double mid = 0.5 * (a + b);
            double g = 1.0;
            for (int j = 0; j < np; ++j)
                g += h.kCav * w2[j] / (poles[j] - mid);
            (g < 0 ? a : b) = mid;
        }
        return std::min(lowestFree, 0.5 * (a + b));
    }

    // The strain the heel's push adds: a membrane pressed a depth δ over a
    // patch of radius r0, a distance R from the rim, takes the logarithmic
    // shape of a point-loaded membrane, so ∫|∇w|² = 2πδ²/ln(R/r0); and
    // ΔT/T = E·h/((1−ν)·T)·that/(4A). Pressed all the way (press 1) the
    // heel goes as deep as brings the ge up an octave; the depth goes as
    // the press.
    float heelStrain(const Head &h, float press) const {
        const float depth = heelDepth(h) * press;
        return h.stretch / std::max(h.T, 1.0f) * kTwoPi * depth * depth / std::log(kHeelSpan / kHeelPatch);
    }
    float heelDepth(const Head &h) const {
        return std::sqrt(h.bendEps[kBendSteps] * std::max(h.T, 1.0f) / h.stretch * std::log(kHeelSpan / kHeelPatch) / kTwoPi);
    }
    static constexpr float kHeelSpan = 0.08f, kHeelPatch = 0.035f; // (a heel is broad)

    static float tap(const float *line, int wr, float dl) {
        const int i = int(dl);
        const float t = dl - float(i);
        const float a = line[(wr - i) & (kDelay - 1)], b = line[(wr - i - 1) & (kDelay - 1)];
        return a + t * (b - a);
    }

    // This sample's forces from the fingers striking drum d (into f), and
    // the resting fingers' engagement. Whether any finger pushed.
    bool contacts(int d, float *f, float &tick) {
        Head &h = m_head[d];
        const int N = h.n;
        bool pushed = false;
        for (auto &c : m_contacts) {
            if (!c.on || c.drum != d)
                continue;
            if (c.delay > 0) {
                --c.delay;
                continue;
            }
            if (!pushed) {
                for (int k = 0; k < N; ++k)
                    f[k] = 0;
                pushed = true;
            }
            float surface = 0; // inward
            for (int k = 0; k < N; ++k)
                surface -= c.phi[k] * h.x[k];
            if (c.age == 0)
                c.z = surface;
            ++c.age;
            const float delta = c.z - surface;
            float force = 0;
            if (delta > 0) {
                float sv = 0;
                for (int k = 0; k < N; ++k)
                    sv -= c.phi[k] * h.u[k];
                const float rel = c.v - sv * m_rate; // closing speed
                force = std::max(0.0f, c.pulp * delta * std::sqrt(delta) * (1.0f + c.hc * rel));
                for (int k = 0; k < N; ++k)
                    f[k] -= c.phi[k] * force;
                c.touched = c.age;
                tick += force / c.sigma;
            }
            c.v -= (force / c.mass + c.retract) * m_dt;
            c.z += c.v * m_dt;
            if ((delta <= 0 && c.v < 0 && c.age > 4) || c.age > int(0.06f * m_rate)) {
                c.on = false;
                m_lastContact = float(c.touched) / m_rate;
            }
        }
        for (auto &r : m_rests) {
            if (!r.on || r.drum != d)
                continue;
            if (r.delay > 0) {
                --r.delay;
                continue;
            }
            if (r.life > 0 && --r.life == 0)
                r.want = 0;
            r.engage += (r.want - r.engage) * (1.0f / (0.008f * m_rate));
            if (r.want == 0 && r.engage < 1e-3f && !r.latent) {
                r.on = false;
                continue;
            }
        }
        return pushed;
    }

    // The fingers resting on drum d, after the oscillators have moved u
    // (predicted): each a spring and a dashpot on the skin under it,
    // solved implicitly in its own velocity (F = −k·w − c·v', v' the
    // skin's speed after F acts), so however hard a hand presses it only
    // ever takes energy out.
    void restOn(int d) {
        Head &h = m_head[d];
        if (h.quietRests)
            return; // (nothing left under them to damp)
        for (auto &r : m_rests) {
            if (!r.on || r.drum != d || r.delay > 0 || r.engage <= 0)
                continue;
            // (only the modes that would ring on by themselves: those that
            // die in a tenth of a second anyway it need not damp)
            const int N = std::min(h.restN, h.live);
            float w = 0, P = 0, Q = 0;
            for (int k = 0; k < N; ++k) {
                w += r.phi[k] * h.x[k];
                P += r.phi[k] * h.u[k];
                Q += r.phi[k] * r.phi[k] * h.inv[k];
            }
            const float press = r.heel ? 1.0f + 3.0f * r.press : 1.0f;
            const float k = r.engage * press * r.k, c = r.engage * press * r.c * m_rate;
            const float force = (-k * w - c * P) / (1.0f + c * Q);
            for (int j = 0; j < N; ++j)
                h.u[j] += h.inv[j] * r.phi[j] * force;
        }
    }

    float m_rate = 48000, m_dt = 1.0f / 48000;
    Head m_head[2];
    Contact m_contacts[kStrikes];
    Rest m_rests[kRests];
    float m_decayScale = 1;
    float m_lastContact = 0;
    bool m_touching = false;
};

} // namespace tf::tabla
