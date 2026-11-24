#pragma once

// A saxophone, as physics: a single cane reed on a mouthpiece, blown by a
// player whose vocal tract sits upstream of it, driving a conical brass bore
// whose length the keys set, radiating from its bell and open tone holes.
// The measurements it is built from are in docs/saxophone.md (the survey
// behind them in docs/saxophone-research.md).
//
//   the bore    a truncated cone, its missing tip stood in for by the
//               mouthpiece (a short cylinder closed by the reed), its far
//               end a lattice of open tone holes and then the bell. When
//               the instrument is built, every fingering's input impedance
//               is computed from that geometry by transmission lines
//               (spherical waves in each conical piece, visco-thermal wall
//               losses, Keefe's open-hole shunts, the bell's radiation),
//               and the tone holes themselves are placed, one after the
//               other up from the bell, where each puts its note in tune,
//               as a maker does. Each impedance peak becomes a mode (a
//               parallel resonance: its frequency, Q and peak), so the bore
//               plays as a couple of dozen resonators driven by the reed's
//               flow. A cone's resonances stand near every harmonic, which
//               is why a saxophone has the full series where a clarinet's
//               cylinder favours the odd ones; the truncation stretches the
//               upper ones sharp, and above the open-hole lattice's cutoff
//               the resonances fade.
//   registers   the octave key opens a small vent (one on the body, one on
//               the neck) near the second mode's pressure node, which
//               spoils the first mode, so the reed locks onto the second:
//               the upper register is the lower one's tubes, overblown.
//               Above the palm keys, and below the low B♭, the same horn
//               scaled smaller or larger plays (docs/saxophone.md).
//   the reed    a mass on a spring with the lip's damping, closed by the
//               pressure across it (Bernoulli's flow through the slit
//               between reed and lay, closing over the lay's curve),
//               beating shut against the lay once blown hard enough. The
//               flow, the reed and the bore are solved together each
//               sample, explicitly (Guillemain, Kergomard & Voinier 2005):
//               the bore's response to this sample's flow is one known
//               number plus a fixed share of the flow, so the flow is the
//               root of a quadratic.
//   the player  the mouth pressure (breath, from subtone to edge), the
//               lip's damping, grip and opening (embouchure), the tongue on
//               the reed (articulation), the vocal tract's resonances in
//               series with the bore, a lip and jaw that bend the note by
//               giving the reed room (an equivalent volume at the
//               mouthpiece), a hum through the vocal folds under the note
//               (growl), the turbulence of the jet through the slit (breath
//               noise), and an ear: the note lipped into tune, a cracked
//               note caught, a note that won't speak blown a little harder,
//               each remembered for that note.
//   the sound   what the modes hold carried to a microphone a metre away by
//               the bore's own transfer to the bell and open holes (worked
//               out from the same lines, fitted mode by mode by least
//               squares), and above the modes the flow at the transfer's
//               level there.
//
// Pressures are in units of the reed's closing pressure P_M, flows in
// P_M over the cone's characteristic impedance at the mouthpiece.

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace tf::sax {

enum Kind : int { Alto = 0, Tenor, kKinds };

using cd = std::complex<double>;

// Air in a played horn: warm and wet, about 27 °C.
constexpr double kC = 347.0;  // m/s
constexpr double kRho = 1.18; // kg/m³

constexpr int kMaxModes = 24; // bore modes held per fingering
constexpr int kTract = 2;     // the vocal tract's resonances
constexpr int kAll = kMaxModes + kTract + 1; // and the continuum above the modes
constexpr int kContinuum = kMaxModes + kTract;
// How long a key takes to travel (its pad lifting or closing), seconds.
constexpr float kKeyS = 0.008f;
// The corner of the high-passed flow the radiation's fit may use (Hz).
constexpr double kRadHp = 3000.0;
// The reed's quality factor with the pinched upper-register embouchure.
constexpr float kUpperQ = 1.5f;
constexpr int kHoles = 19; // tone holes above the bell (the low B♭'s), to written F5

// --- the instrument ---------------------------------------------------------------------------------------

struct Spec {
    Kind kind = Alto;
    int lowMidi = 49;          // the lowest note (written low B♭), concert
    double r1 = 0.0061;        // the bore's radius where the mouthpiece fits, m
    double halfAngle = 0.0305; // the cone's half angle, rad
    double mpLen = 0.06, mpR = 0.006; // the mouthpiece, as a cylinder closed by the reed (m)
    double holeRatio = 0.75;   // an open tone hole's radius over the bore's there
    double chimney = 0.35;     // its chimney's height over its radius
    double bell = 1.25;        // the bell's mouth radius over the cone's at its end
    double ventR = 0.0012, ventT = 0.008; // the register vents' radius and chimney, m
    double lossK = 1.0;        // the walls' losses over a smooth tube's
    double breath = 1.0;       // the breath noise through the slit with the lip slack (subtone), over the alto's
    // Where the radiated spectrum peaks before it falls (Benade & Lutgen's
    // break), and by how much it is lifted there (see Sax::start): the
    // alto's own, unlifted.
    double breakHz = 837, breakDb = 0;
    int register2 = 16;        // the first overblown note (semitones over the lowest): written D5
    // The reed, at an ordinary embouchure (see Sax::Control::lip for the range).
    double reedHz = 1800;    // its resonance with the lip on it
    double reedQ = 1.5;      // and its quality factor
    double pm = 6000;        // closing pressure P_M, Pa
    double zeta = 0.45;      // the embouchure parameter, Z_c·w·H0·√(2/ρP_M)
};

inline Spec specOf(Kind k) {
    Spec s;
    s.kind = k;
    if (k == Alto) {
        // Half angle 0.028 rad (Petersen et al. 2020), the missing tip
        // 0.196 m (Kergomard et al. 2019), the mouthpiece a cylinder 6 mm in
        // radius as Colinot et al.'s (2021), but 30 mm long where theirs is
        // 60: so the bore keeps a measured alto's inharmonicity (its second
        // and third resonances 2.05 and 3.14 times the first), without which
        // the low notes jump to the octave at a forte.
        s.lowMidi = 49;
        s.r1 = 0.028 * 0.196;
        s.halfAngle = 0.028;
        s.mpLen = 0.03;
        s.mpR = 0.006;
        s.holeRatio = 0.6;
        s.chimney = 0.5;
        s.reedHz = 672;     // Colinot et al. 2021: ω_r = 4224 rad/s
        s.reedQ = 1.0;      // q_r = 1
        s.zeta = 0.8;
        s.pm = 3800;        // K_a·H0, 4.8 MPa/m × 0.8 mm
        s.ventR = 0.0015;
        s.ventT = 0.006;
    } else {
        // The tenor, a fourth below, is not the alto scaled: its parts are
        // proportioned differently, from a tenor's measurements where there
        // are any (the sources in docs/saxophone-research.md, sec. 7;
        // est. marks an estimate).
        // - The cone: 1.52° (0.0265 rad; Chen, Smith & Wolfe 2009; 1.55° in
        //   Jensch & Grothe's CT of a tenor) against the alto's 1.60°.
        // - The neck: 6.7 mm in radius where the mouthpiece fits (the CT),
        //   barely wider than an alto's 6.0 to 6.25 (this model's alto is
        //   5.5, its cone's truncation): the tenor is longer mostly
        //   because its cone runs further towards its tip (0.25 m missing,
        //   against the alto's 0.2).
        // - The mouthpiece: 8.5 mm in radius, the mean shank of 41 tenor
        //   mouthpieces; 32 mm long (est.), so 7.3 ml, 0.61 of the missing
        //   cone's volume, as the alto's is 0.55 of its own. Real ones fill
        //   about all of it; here a tenor's that size sends its lowest notes
        //   into a hollow, near-sine regime.
        // - The tone holes: b/a 0.54 (Jensch & Grothe 2021, measured on a
        //   tenor) against the alto's 0.6 (est.), chimneys as the alto's:
        //   the lattice cuts off at 0.7 to 1.0 kHz, inside the 760 ± 250 Hz
        //   measured on a tenor (CSW 2009), the alto's at 1.2 to 1.5 kHz.
        // - The bell: as the alto's for the cone's end (a mouth ~150 mm
        //   against ~120 on a cone 1.33 times longer).
        // - The reed: 514 Hz, Q 1 (est.). The one reed measured in playing,
        //   672 Hz with Q 1.13 (Muñoz Arancón et al.), is a tenor's, but this
        //   model's alto was tuned with it (as Colinot et al.'s is), and an
        //   alto reed (70 mm long) rests above a tenor's (80 mm), as a
        //   cantilever by (80/70)²; so the tenor's is the alto's 672 Hz
        //   over 1.31. (At 672 Hz its lowest notes start an octave up.)
        // - The break: its radiated spectrum lifted 4 dB around 618 Hz,
        //   where Benade & Lutgen found a tenor's peaks (an alto's at
        //   837 Hz); the height est. This model's excitation falls too fast
        //   to make that peak itself (docs/saxophone.md, "Darker than
        //   measured"). With it, played hard, B♭2 and C3 have their 6th and
        //   5th harmonics strongest (a recorded tenor's low notes peak at
        //   415 to 730 Hz); elsewhere the fundamental still leads.
        // - The breath: three times the alto's noise in the subtone term
        //   (est.: nothing measured), the bigger reed's slit and a slack lip.
        s.kind = Tenor;
        s.lowMidi = 44;
        s.r1 = 0.0067;
        s.halfAngle = 0.0265;
        s.mpLen = 0.032;
        s.mpR = 0.0085;
        s.holeRatio = 0.54;
        s.chimney = 0.5;
        s.bell = 1.25;
        s.breakHz = 618;
        s.breakDb = 4;
        s.reedHz = 514;
        s.reedQ = 1.0;
        s.zeta = 0.8;
        s.pm = 4300;
        s.ventR = 0.0015 * 1.33;
        s.ventT = 0.006 * 1.33;
        s.breath = 3.0;
        return s;
    }
    s.lossK = 1.0;
    return s;
}

inline double hzOf(double midi) { return 440.0 * std::exp2((midi - 69.0) / 12.0); }

// Benade's wall losses: the attenuation (1/m) of a wave at hz in a tube of radius r.
inline double wallLoss(double hz, double r) { return 2.96e-5 * std::sqrt(std::max(hz, 1.0)) / std::max(r, 1e-4); }

struct Hole {
    double x = 0, b = 0, t = 0; // apex distance, radius, chimney height (m)
};

// One fingering's air column, from the reed to the bell.
struct Bore {
    double x1 = 0.2, tanA = 0.03; // the cone's truncation (apex distance at the mouthpiece) and slope
    double end = 1.2;             // the bell's apex distance
    double bellR = 0.04;          // its mouth's radius
    std::vector<Hole> open;       // the open holes and vents, by apex distance
    double mpL = 0.1, mpS = 1e-4; // the mouthpiece: a closed cylinder of this length and section
    double lossK = 1.0;           // the walls' losses over a smooth tube's (Benade's)
    double lossRef = 90.0;        // and where their rise with frequency starts (lossRise)
};

// Through a conical piece from x2 (the bell's side) up to x1, carrying the
// pressure and the flow (toward the bell): spherical waves, p = (A·e^{-jkx}
// + B·e^{jkx})/x, with the wall's losses in the wavenumber.
// A real horn loses more than a smooth tube, the more the higher the
// frequency: its closed holes' cavities and pads, its joints, the lattice's
// own radiation. Fitted so a fingering's resonances have about the same Q
// across the low register, as the measured alto's do (Colinot et al. 2021,
// Table 2: Q 34, 35, 28.5, 37 for the first four).
// (For an alto; a larger horn's rise starts lower in proportion, ref.)
inline double lossRise(double hz, double ref = 90.0) { return std::max(1.0, std::sqrt(hz / ref)); }

inline void conePiece(cd &p, cd &u, double xa, double xb, double tanA, double w, double lossK, double lossRef) {
    if (xb - xa < 1e-9)
        return;
    const double hz = w / (2 * M_PI);
    // (The loss of a piece by its mean 1/r: ln(xb/xa)/(L·tanα).)
    const double alpha = lossK * lossRise(hz, lossRef) * 2.96e-5 * std::sqrt(std::max(hz, 1.0)) * std::log(xb / xa) / ((xb - xa) * tanA);
    const cd k = cd(w / kC + alpha, -alpha);
    const cd j(0, 1);
    const double Sa = M_PI * (xa * tanA) * (xa * tanA), Sb = M_PI * (xb * tanA) * (xb * tanA);
    // At xb (local phase 0): p = (A + B)/xb, U = Sb·(j/ωρ)·(A(−jk/xb − 1/xb²) + B(jk/xb − 1/xb²)).
    const cd g = j / (w * kRho);
    const cd m11 = 1.0 / xb, m12 = 1.0 / xb;
    const cd m21 = Sb * g * (-j * k / xb - 1.0 / (xb * xb)), m22 = Sb * g * (j * k / xb - 1.0 / (xb * xb));
    const cd det = m11 * m22 - m12 * m21;
    const cd A = (m22 * p - m12 * u) / det, B = (m11 * u - m21 * p) / det;
    // At xa = xb − L: e^{-jk(xa−xb)} = e^{jkL}.
    const double L = xb - xa;
    const cd ep = std::exp(j * k * L), em = 1.0 / ep;
    const cd a = A * ep, b = B * em;
    p = (a + b) / xa;
    u = Sa * g * (a * (-j * k / xa - 1.0 / (xa * xa)) + b * (j * k / xa - 1.0 / (xa * xa)));
}
// A long piece in steps of at most 12 cm, so the losses follow the radius.
inline void coneUp(cd &p, cd &u, double xa, double xb, double tanA, double w, double lossK, double lossRef) {
    const int n = std::max(1, int(std::ceil((xb - xa) / 0.12)));
    for (int i = n - 1; i >= 0; --i)
        conePiece(p, u, xa + (xb - xa) * i / n, xa + (xb - xa) * (i + 1) / n, tanA, w, lossK, lossRef);
}

// What the reed sees (the admittance at the reed, in m³/(Pa·s)) at w rad/s,
// and the pressure a metre from the horn per unit pressure at the reed.
inline cd admittance(const Bore &bore, double w, cd *radiated = nullptr) {
    const cd j(0, 1);
    const double k0 = w / kC;
    // The bell: an unflanged mouth's radiation (Levine & Schwinger's low-ka
    // terms, kept bounded as a parallel mass and resistance).
    const double x = 0.61 * k0 * bore.bellR, rr = 1.49;
    const cd zr = kRho * kC / (M_PI * bore.bellR * bore.bellR) * (j * x * rr) / (rr + j * x);
    cd u = 1.0, p = zr * u;
    cd out = u;
    double at = bore.end;
    for (auto it = bore.open.rbegin(); it != bore.open.rend(); ++it) {
        const Hole &h = *it;
        if (h.x >= at || h.x <= bore.x1)
            continue;
        coneUp(p, u, h.x, at, bore.tanA, w, bore.lossK, bore.lossRef);
        at = h.x;
        // An open hole: Keefe's shunt (the chimney's mass with its inner and
        // outer end corrections, its radiation and its own wall losses) and
        // the small negative series length it puts in the bore.
        const double a = h.x * bore.tanA, d = std::min(h.b / a, 1.0);
        const double ti = h.b * (0.82 - 0.193 * d - 1.09 * d * d + 1.27 * d * d * d - 0.71 * d * d * d * d);
        const double te = h.t + ti + 0.61 * h.b;
        const double ah = wallLoss(w / (2 * M_PI), h.b);
        const cd kh = cd(k0 + ah, -ah);
        const cd zs = kRho * kC / (M_PI * h.b * h.b) * (j * kh * te + 0.25 * (k0 * h.b) * (k0 * h.b));
        const cd za = kRho * kC / (M_PI * a * a) * (j * k0 * (-0.28 * h.b * d * d));
        p += 0.5 * za * u;
        const cd uh = p / zs;
        out += uh;
        u += uh;
        p += 0.5 * za * u;
    }
    coneUp(p, u, bore.x1, at, bore.tanA, w, bore.lossK, bore.lossRef);
    // The mouthpiece: a cylinder closed by the reed (its length and radius
    // the spec's: see docs/saxophone.md for why it is shorter than the
    // missing tip's volume would make it).
    const double am = std::sqrt(bore.mpS / M_PI);
    const double alm = bore.lossK * lossRise(w / (2 * M_PI), bore.lossRef) * wallLoss(w / (2 * M_PI), am);
    const cd km = cd(k0 + alm, -alm) * bore.mpL;
    const cd zm = kRho * kC / bore.mpS;
    const cd cs = std::cos(km), sn = std::sin(km);
    const cd pc = p * cs + j * zm * sn * u;
    const cd ur = u * cs + j * sn / zm * p;
    if (radiated) // each opening a monopole, heard a metre off: ρ/(4π)·dU/dt
        *radiated = j * w * kRho / (4 * M_PI) * out / pc;
    return ur / pc;
}

// A fingering's modes, the reed's view of the bore.
struct Fingering {
    int n = 0; // modes found
    float w[kMaxModes] = {}, q[kMaxModes] = {}, a[kMaxModes] = {}; // rad/s, Q, peak (over Z_c)
    float cw = 0, cq = 0.6f, ca = 0; // the continuum above them
    // The radiation: what each mode's state (and the continuum's) is worth
    // a metre off, a complex weight, fitted so the modes together radiate
    // as the horn does (its transfer from the reed's flow to the pressure a
    // metre off, worked out from the same lines) at every frequency.
    float rr[kMaxModes + 1] = {}, ri[kMaxModes + 1] = {};
    // (And above the modes, where the horn's transfer is broad and flat, the
    // flow itself through two high-passes at kRadHp, at its level there.)
    float uh2 = 0;
    int note = 0;   // the mode the note sounds on
    int mode = 1;   // which resonance of its tube that is
    int tube = 0;   // the tube (semitones over the lowest note's)
    int vent = 0;   // 0 none, 1 the body's, 2 the neck's
    bool alt = false; // above the second register: altissimo
    float hz = 0;   // the note's frequency
    float closed = 0; // the cone's truncation over its length to the first open hole, x1/(L + x1)
};

struct Design {
    Spec spec;
    double x1 = 0.2, zRef = 1;     // the truncation, and Z_c at the mouthpiece (Pa·s/m³)
    double mpL = 0, mpS = 0;
    int sMin = -12, sMax = 50;     // semitones over the lowest note held in the table
    std::vector<Fingering> notes;  // by semitone, from sMin
    double tubeEnd[kHoles + 1] = {}; // the bell's apex distance for the lowest note's tube
    double holeX[kHoles + 1] = {};   // hole j's apex distance (j = 1 … kHoles)
    double holeB[kHoles + 1] = {};
    double ventX[3] = {};            // the vents' apex distances (1 body, 2 neck)
    const Fingering &at(int s) const { return notes[size_t(std::clamp(s, sMin, sMax) - sMin)]; }
};

// The bore of a fingering: tube j (j < 0 a longer bell, j > 0 holes 1 … j
// open), and a vent.
inline Bore boreOf(const Design &d, int tube, int vent) {
    Bore b;
    b.x1 = d.x1;
    b.tanA = std::tan(d.spec.halfAngle);
    b.mpL = d.mpL;
    b.mpS = d.mpS;
    b.lossK = d.spec.lossK;
    b.lossRef = 90.0 * hzOf(d.spec.lowMidi) / hzOf(49);
    b.end = d.tubeEnd[0];
    b.bellR = d.spec.bell * b.end * b.tanA;
    for (int h = 1; h <= std::min(tube, kHoles); ++h)
        b.open.push_back({d.holeX[h], d.holeB[h], d.spec.chimney * d.holeB[h]});
    if (vent > 0)
        b.open.push_back({d.ventX[vent], d.spec.ventR, d.spec.ventT});
    std::sort(b.open.begin(), b.open.end(), [](const Hole &x, const Hole &y) { return x.x < y.x; });
    return b;
}

// The susceptance's upward zero nearest hz: a resonance's frequency.
inline double resonanceNear(const Bore &b, double hz, double span = 0.25) {
    auto B = [&](double f) { return admittance(b, 2 * M_PI * f).imag(); };
    double lo = hz * (1 - span), hi = hz * (1 + span);
    // Scan for the upward crossing closest to hz.
    const int steps = 48;
    double best = -1, bestD = 1e9, fa = lo, ba = B(lo);
    for (int i = 1; i <= steps; ++i) {
        const double fb = lo * std::pow(hi / lo, double(i) / steps), bb = B(fb);
        if (ba < 0 && bb >= 0) {
            double a = fa, c = fb, va = ba, vc = bb;
            for (int it = 0; it < 30 && c - a > 1e-4; ++it) {
                const double m = 0.5 * (a + c), vm = B(m);
                if (vm < 0)
                    a = m, va = vm;
                else
                    c = m, vc = vm;
            }
            (void)va;
            (void)vc;
            const double f = 0.5 * (a + c);
            if (std::fabs(std::log(f / hz)) < bestD) {
                bestD = std::fabs(std::log(f / hz));
                best = f;
            }
        }
        fa = fb;
        ba = bb;
    }
    return best;
}

// Every resonance of a fingering up to fMax: its frequency, Q, peak and
// radiation, from the admittance near it (a parallel resonance: G + jB with
// B rising through zero; the peak is 1/G and Q = ω·B'/2G).
inline void modesOf(const Design &d, const Bore &b, Fingering &f, double fMax) {
    const double fLo = 0.35 * hzOf(d.spec.lowMidi - 12);
    const int steps = int(std::log(fMax / fLo) / 0.012);
    double fa = fLo, ba = admittance(b, 2 * M_PI * fLo).imag();
    f.n = 0;
    double lastHz = 0;
    for (int i = 1; i <= steps && f.n < kMaxModes; ++i) {
        const double fb = fLo * std::exp(0.012 * i);
        const double bb = admittance(b, 2 * M_PI * fb).imag();
        if (ba < 0 && bb >= 0) {
            // Regula falsi, then a few bisections where it stalls.
            double lo = fa, hi = fb, vlo = ba, vhi = bb;
            for (int it = 0; it < 24 && hi - lo > 1e-3; ++it) {
                double m = (it % 3 == 2) ? 0.5 * (lo + hi) : lo - vlo * (hi - lo) / (vhi - vlo);
                m = std::clamp(m, lo + 1e-6, hi - 1e-6);
                const double vm = admittance(b, 2 * M_PI * m).imag();
                if (vm < 0)
                    lo = m, vlo = vm;
                else
                    hi = m, vhi = vm;
            }
            const double hz = 0.5 * (lo + hi), w = 2 * M_PI * hz;
            const cd y = admittance(b, w);
            const double dw = w * 5e-4;
            const double dB = (admittance(b, w + dw).imag() - admittance(b, w - dw).imag()) / (2 * dw);
            const double G = std::max(y.real(), 1e-15);
            const double Q = w * dB / (2 * G);
            if (Q > 0.8 && dB > 0) {
                const int k = f.n++;
                f.w[k] = float(w);
                f.q[k] = float(Q);
                f.a[k] = float(1.0 / G / d.zRef);
                lastHz = hz;
            }
        }
        fa = fb;
        ba = bb;
    }
    // Above the last mode held, the bore is near its characteristic
    // impedance with small ripples: one broad resonance stands for its
    // average, radiating as the bore does there.
    const double top = std::max(lastHz * 1.15, 2500.0);
    double sum = 0;
    int cnt = 0;
    for (double hz = top; hz < top * 1.8; hz *= 1.03, ++cnt)
        sum += (1.0 / admittance(b, 2 * M_PI * hz)).real();
    const double mid = top * 1.35;
    f.cw = float(2 * M_PI * mid);
    f.cq = 0.6f;
    f.ca = float(std::max(sum / std::max(cnt, 1), 0.0) / d.zRef);
    // The radiation's weights, by least squares on a dense grid: the horn's
    // own transfer from the reed's flow to a metre off, H = T·Z (T the
    // pressure there per pressure at the reed, Z the input impedance over
    // Z_c), against what the modes radiate with weight W_k: mode k's state
    // per unit flow is g(ω) = C/(jω − s) (C = Aω_k/2Q·(1 + jσ/ω_d), s = −σ +
    // jω_d), and 2·Re(W·z) has the response W·g(ω) + W*·conj(g(−ω)), linear
    // in Re W and Im W. Each frequency weighted by the inverse of the
    // transfer there (a fit in relative terms), with a little ridge.
    {
        const int N = f.n + 1, U = 2 * N;
        std::vector<double> AtA(size_t(U * U), 0.0), Atb(size_t(U), 0.0);
        std::vector<cd> col(static_cast<size_t>(U));
        // (The grid covers every mode kept, and the continuum.)
        double top = 7000.0;
        for (int k = 0; k < f.n; ++k)
            top = std::max(top, 1.15 * f.w[k] / (2 * M_PI));
        top = std::max(top, 1.2 * f.cw / (2 * M_PI));
        const double lo = std::max(0.5 * (f.n > 0 ? f.w[0] / (2 * M_PI) : 100.0), 30.0), hi = top;
        const int M = 220;
        for (int i = 0; i < M; ++i) {
            const double hz = lo * std::pow(hi / lo, double(i) / (M - 1)), w = 2 * M_PI * hz;
            cd t;
            const cd z = 1.0 / admittance(b, w, &t) / d.zRef;
            const cd H = t * z;
            const double wt = 1.0 / std::max(std::abs(H), 1e-12);
            for (int k = 0; k < N; ++k) {
                const double wk = k < f.n ? f.w[k] : f.cw, qk = k < f.n ? f.q[k] : f.cq, ak = k < f.n ? f.a[k] : f.ca;
                const double q = std::max(qk, 0.55), sg = wk / (2 * q), wd = wk * std::sqrt(std::max(1 - 1 / (4 * q * q), 0.01));
                const cd C = ak * wk / (2 * q) * cd(1.0, sg / wd), sp(-sg, wd);
                const cd j(0, 1);
                const cd gp = C / (j * w - sp), gm = std::conj(C / (-j * w - sp));
                col[size_t(2 * k)] = (gp + gm) * wt;
                col[size_t(2 * k + 1)] = j * (gp - gm) * wt;
            }
            const cd hb = H * wt;
            for (int r = 0; r < U; ++r) {
                for (int c = 0; c < U; ++c)
                    AtA[size_t(r * U + c)] += (std::conj(col[size_t(r)]) * col[size_t(c)]).real();
                Atb[size_t(r)] += (std::conj(col[size_t(r)]) * hb).real();
            }
        }
        // (Each unknown scaled to a unit column first, so the ridge treats
        // them alike.)
        std::vector<double> sc(size_t(U), 1.0);
        for (int r = 0; r < U; ++r)
            sc[size_t(r)] = 1.0 / std::sqrt(std::max(AtA[size_t(r * U + r)], 1e-300));
        for (int r = 0; r < U; ++r) {
            for (int c = 0; c < U; ++c)
                AtA[size_t(r * U + c)] *= sc[size_t(r)] * sc[size_t(c)];
            Atb[size_t(r)] *= sc[size_t(r)];
            AtA[size_t(r * U + r)] += 1e-3;
        }
        // (Gaussian elimination with partial pivoting.)
        std::vector<double> x(size_t(U), 0.0);
        for (int c = 0; c < U; ++c) {
            int piv = c;
            for (int r = c + 1; r < U; ++r)
                if (std::fabs(AtA[size_t(r * U + c)]) > std::fabs(AtA[size_t(piv * U + c)]))
                    piv = r;
            if (piv != c) {
                for (int k = 0; k < U; ++k)
                    std::swap(AtA[size_t(c * U + k)], AtA[size_t(piv * U + k)]);
                std::swap(Atb[size_t(c)], Atb[size_t(piv)]);
            }
            const double dg = AtA[size_t(c * U + c)];
            if (std::fabs(dg) < 1e-300)
                continue;
            for (int r = c + 1; r < U; ++r) {
                const double m = AtA[size_t(r * U + c)] / dg;
                for (int k = c; k < U; ++k)
                    AtA[size_t(r * U + k)] -= m * AtA[size_t(c * U + k)];
                Atb[size_t(r)] -= m * Atb[size_t(c)];
            }
        }
        for (int r = U - 1; r >= 0; --r) {
            double v = Atb[size_t(r)];
            for (int k = r + 1; k < U; ++k)
                v -= AtA[size_t(r * U + k)] * x[size_t(k)];
            const double dg = AtA[size_t(r * U + r)];
            x[size_t(r)] = std::fabs(dg) > 1e-300 ? v / dg : 0.0;
        }
        for (int r = 0; r < U; ++r)
            x[size_t(r)] *= sc[size_t(r)];
        for (int k = 0; k < N; ++k) {
            const int i = k < f.n ? k : kMaxModes;
            f.rr[i] = float(x[size_t(2 * k)]);
            f.ri[i] = float(x[size_t(2 * k + 1)]);
        }
        // Above the modes the transfer is broad, its phase turning fast with
        // the paths to the bell and holes: there only its level is fitted,
        // the flow through two high-passes at the level the horn has there
        // on average.
        double e = 0;
        int cnt = 0;
        for (double hz = 3500; hz < 7000; hz *= 1.02, ++cnt) {
            cd t;
            const cd z = 1.0 / admittance(b, 2 * M_PI * hz, &t) / d.zRef;
            e += std::norm(t * z);
        }
        f.uh2 = float(std::sqrt(e / std::max(cnt, 1)));
    }
}

// Builds an instrument from its spec: the bell's length for the lowest note,
// each tone hole placed in turn up from the bell where its note is in tune,
// the vents, and every semitone's fingering and modes.
inline Design build(const Spec &sp) {
    Design d;
    d.spec = sp;
    const double tanA = std::tan(sp.halfAngle);
    d.x1 = sp.r1 / tanA;
    d.zRef = kRho * kC / (M_PI * sp.r1 * sp.r1);
    // The mouthpiece stands in for the missing tip: a cylinder closed by the
    // reed, as in the models the saxophone's regimes have been studied with
    // (Colinot et al. 2021), here about half the tip's volume (see specOf).
    d.mpL = sp.mpLen;
    d.mpS = M_PI * sp.mpR * sp.mpR;
    // Tunes one length (the bell's, or a hole's place) so a fingering's
    // first resonance lands on hz: a secant on the length.
    auto tune = [&](double hz, const std::function<Bore(double)> &make, double guess) {
        double xa = guess, xb = guess * 1.02;
        double fa = resonanceNear(make(xa), hz, 0.35), fb = resonanceNear(make(xb), hz, 0.35);
        for (int it = 0; it < 12; ++it) {
            if (fa <= 0 || fb <= 0 || std::fabs(fb - fa) < 1e-9)
                break;
            const double ea = std::log(fa / hz), eb = std::log(fb / hz);
            if (std::fabs(eb) < 2e-5)
                break;
            const double xn = xb - eb * (xb - xa) / (eb - ea);
            xa = xb;
            fa = fb;
            xb = std::clamp(xn, d.x1 + 0.01, 4.0);
            fb = resonanceNear(make(xb), hz, 0.35);
        }
        return xb;
    };
    const double f0 = hzOf(sp.lowMidi);
    {
        auto make = [&](double end) {
            Design t = d;
            t.tubeEnd[0] = end;
            return boreOf(t, 0, 0);
        };
        d.tubeEnd[0] = tune(f0, make, kC / (2 * f0) * 0.93);
    }
    // The holes, from the bell up.
    for (int h = 1; h <= kHoles; ++h) {
        auto make = [&](double x) {
            Design t = d;
            t.holeX[h] = x;
            t.holeB[h] = sp.holeRatio * x * tanA;
            return boreOf(t, h, 0);
        };
        const double prev = d.holeX[h - 1] > 0 ? d.holeX[h - 1] : d.tubeEnd[0];
        d.holeX[h] = tune(f0 * std::exp2(h / 12.0), make, (prev - 0.0) * std::exp2(-1.0 / 12.0));
        d.holeB[h] = sp.holeRatio * d.holeX[h] * tanA;
    }
    // The vents, each at the second mode's pressure node (half way to the
    // apex) of the middle tube it serves: the body's for written D5–G♯5
    // (tubes 4–10), the neck's for A5–E6 (tubes 11–16).
    d.ventX[1] = 0.5 * d.holeX[7];
    d.ventX[2] = std::max(0.5 * d.holeX[13], d.x1 + 0.01);
    // Every semitone's fingering: the first register (written B♭3–C♯5) on
    // its own tube; the second (written D5–E6) the first's tubes overblown
    // with a vent open. Above that, and below the low B♭, the same horn
    // made smaller or larger (a sopranino, a baritone, every length scaled
    // alike) playing the fingering an octave away, as no saxophone's own
    // altissimo is held without the player's vocal tract tuned to it (see
    // docs/saxophone.md).
    d.notes.assign(size_t(d.sMax - d.sMin + 1), Fingering{});
    for (int s = d.sMin; s <= d.sMax; ++s) {
        int src = s;
        while (src > 28)
            src -= 12;
        while (src < 0)
            src += 12;
        int tube = src, vent = 0, mode = 1;
        if (src >= sp.register2) {
            tube = src - 12;
            vent = src <= 22 ? 1 : 2;
            mode = 2;
        }
        const float scaled = float(std::exp2((s - src) / 12.0));
        Fingering &f = d.notes[size_t(s - d.sMin)];
        const Bore b = boreOf(d, tube, vent);
        f.tube = tube;
        f.vent = vent;
        f.mode = mode;
        f.hz = float(hzOf(sp.lowMidi + s));
        f.alt = s > 28;
        modesOf(d, b, f, 6000.0 / scaled);
        for (int k = 0; k < f.n; ++k)
            f.w[k] *= scaled;
        f.cw *= scaled;
        // The mode nearest the note.
        float best = 1e9f;
        for (int k = 0; k < f.n; ++k) {
            const float e = std::fabs(std::log(f.w[k] / (2 * float(M_PI) * f.hz)));
            if (e < best) {
                best = e;
                f.note = k;
            }
        }
        double firstOpen = b.end;
        for (const Hole &h : b.open)
            if (h.b > 0.5 * sp.ventR * 2 && h.x < firstOpen && h.x > d.x1 && (vent == 0 || h.x != d.ventX[vent]))
                firstOpen = h.x;
        f.closed = float(d.x1 / firstOpen);
    }
    return d;
}

inline const Design &design(Kind k) {
    static const Design designs[kKinds] = {build(specOf(Alto)), build(specOf(Tenor))};
    return designs[std::clamp(int(k), 0, kKinds - 1)];
}

// Builds both instruments, so the audio thread never does.
inline void prepare() { (void)design(Alto); }

// --- playing it -------------------------------------------------------------------------------------------

class Sax {
public:
    // What the player does, block by block.
    struct Control {
        float hz = 440;    // the note sounding (fingers, lip and bends together)
        float blow = 0;    // mouth pressure over P_M
        float lip = 0.5f;  // embouchure: 0 subtone (much lip, jaw dropped), 1 edge (firm, little lip)
        float growl = 0;   // how hard the player hums under the note
        float air = 0;     // extra breathiness
        // How far the lip lets the reed stand open, over an ordinary
        // embouchure's rest opening H0: a pianissimo is played with the reed
        // nearly closed by the lip. The closing pressure P_M = K·H0 follows
        // it, so every pressure in the horn scales with it, and ζ ∝ √H0.
        float open = 1;
        // A player who blows and hears nothing blows a little harder, and
        // remembers it for that note (off: the breath is exactly `blow`).
        bool find = false;
    };

    void init(float rate) {
        m_rate = rate;
        std::fill(std::begin(m_ceiling), std::end(m_ceiling), 2.0f);
        m_T = 1.0f / rate;
        m_noiseF.set(2000.0f, 0.5f, rate);
        m_dc.r = std::exp(-2.0f * float(M_PI) * 20.0f / rate);
        m_earA = 1.0f - std::exp(-2.0f * float(M_PI) * 25.0f / rate);
        {
            // (A bilinear high-pass, prewarped to kRadHp.)
            const float k = std::tan(float(M_PI) * float(kRadHp) / rate);
            m_hpA = (1.0f - k) / (1.0f + k);
            m_hpB = 1.0f / (1.0f + k);
        }
    }

    void start(const Design &d, float hz) {
        // (Another horn: what the player knew of this one doesn't carry over.)
        if (m_d == &d)
            remember();
        if (m_d != &d) {
            std::fill(std::begin(m_learn), std::end(m_learn), 0.0f);
            std::fill(std::begin(m_ceiling), std::end(m_ceiling), 2.0f);
            std::fill(std::begin(m_support), std::end(m_support), 0.0f);
            std::fill(std::begin(m_find), std::end(m_find), 0.0f);
        }
        m_d = &d;
        {
            // The break: a peaking section (RBJ's), Q 0.4 (three octaves
            // wide), at the horn's break frequency.
            const double w0 = 2 * M_PI * d.spec.breakHz / m_rate, A = std::pow(10.0, d.spec.breakDb / 40.0);
            const double al = std::sin(w0) / (2 * 0.4), cw = std::cos(w0), a0 = 1 + al / A;
            m_bq[0] = float((1 + al * A) / a0);
            m_bq[1] = float(-2 * cw / a0);
            m_bq[2] = float((1 - al * A) / a0);
            m_bq[3] = float(-2 * cw / a0);
            m_bq[4] = float((1 - al / A) / a0);
        }
        silence();
        m_s = noteOf(hz);
        const Fingering &f = d.at(m_s);
        load(f, true);
        m_ear = m_learn[std::clamp(m_s - d.sMin, 0, kLearn - 1)];
        for (int k = 0; k < kTract; ++k) {
            m_w[kMaxModes + k] = m_tw[k] = kTractHz[k] * 2 * float(M_PI);
            m_q[kMaxModes + k] = m_tq[k] = kTractQ[k];
            m_a[kMaxModes + k] = m_ta[k] = kTractA[k];
        }
        m_blow = 0;
        m_live = true;
    }
    void start(Kind k, float hz) { start(design(k), hz); }

    void silence() {
        std::fill(std::begin(m_zr), std::end(m_zr), 0.0f);
        std::fill(std::begin(m_zi), std::end(m_zi), 0.0f);
        m_x = m_xPrev = 0;
        m_tongue = 0;
        m_tongueOn = false;
        m_glottis = 0;
        m_energy = 0;
        m_closedCount = m_count = 0;
        m_noiseF.reset();
        m_dc = DcBlock{0, 0, m_dc.r};
        m_tractP = 0;
        m_hpU = m_hp1 = m_hp2 = 0;
        m_bz[0] = m_bz[1] = 0;
        m_fresh = true;
        m_e1Re = m_e1Im = m_e2Re = m_e2Im = 0;
        m_upRe = m_upIm = m_dnRe = m_dnIm = 0;
        m_halfRe = 1;
        m_halfIm = 0;
        m_catch = m_crackT = 0;
        m_xVar = 0;
        m_rotRe = 1;
        m_rotIm = 0;
        m_earHold = 0.03f;
        m_live = false;
    }

    // The tongue leaves the reed: a tongued attack. It was on the reed
    // (shut, damped) while the breath built behind it.
    // The ear's correction for the note just played, kept if it was locked
    // on it (and never much).
    void remember() {
        if (m_d && m_kept)
            m_learn[std::clamp(m_s - m_d->sMin, 0, kLearn - 1)] = clampf(m_keptEar, -100.0f, 100.0f);
        m_locked = 0;
        m_kept = false;
    }

    // On a horn already sounding the tongue comes onto the reed over a
    // couple of milliseconds, stops it for the "t" (a dozen), and comes off;
    // on a fresh note it is already there, the breath built behind it.
    void tongue(float hard, bool fresh = false) {
        const float hd = clampf(hard, 0.0f, 1.0f);
        m_tongueUp = fresh ? 1.0f : 1.0f / (0.002f * m_rate);
        m_tongueHold = int((fresh ? 0.015f : 0.012f) * m_rate);
        m_tongueFall = std::exp(-1.0f / ((0.008f - 0.005f * hd) * m_rate));
        m_tongueOn = true;
        if (fresh)
            m_tongue = 1.0f;
        m_settle = 0.15f;
    }

    bool live() const { return m_live; }
    // How loud it is radiating, in Pa at a metre (a slow RMS).
    float level() const { return std::sqrt(std::max(m_energy, 0.0f)); }
    // The share of samples since the last call that the reed lay shut
    // against the lay (beating).
    float closedShare() {
        const float s = m_count > 0 ? float(m_closedCount) / float(m_count) : 0.0f;
        m_closedCount = m_count = 0;
        return s;
    }
    // The mouthpiece pressure's swing since the last call (over P_M).
    void swing(float &lo, float &hi) {
        lo = m_pLo;
        hi = m_pHi;
        m_pLo = 1e9f;
        m_pHi = -1e9f;
    }
    const Fingering &fingering() const { return m_d->at(m_s); }
    // The oscillation's linear threshold, by the static reed (Kergomard):
    // ζ(3γ−1)/2√γ = 1/Z, Z the note's peak over Z_c.
    static float thresholdOf(float zeta, float peak) {
        const float z = std::max(zeta, 1e-3f), a = std::max(peak, 1e-3f);
        const float s = (2.0f / a + std::sqrt(4.0f / (a * a) + 12.0f * z * z)) / (6.0f * z);
        return s * s;
    }
    // The threshold of the note now, with the reed's own dynamics (its
    // response at the note, Re D, scaling its share): ζ(√γ·Re D − (1−γ)/2√γ)
    // = 1/Z, a quadratic in √γ.
    float threshold() const {
        const float w = m_lw[m_note], r = w / std::max(m_wr, 1.0f);
        const float den = (1 - r * r) * (1 - r * r) + (r / m_qr) * (r / m_qr);
        const float reD = std::max((1 - r * r) / den, 0.05f);
        const float z = std::max(m_zeta, 1e-3f), a = std::max(m_a[m_note], 1e-3f), k = 2 * reD + 1;
        const float sq = (2 / a + std::sqrt(4 / (a * a) + 4 * z * z * k)) / (2 * z * k);
        return sq * sq;
    }
    float zeta() const { return m_zeta; }

    // Renders n samples (Pa at a metre), the breath sliding from where the
    // last block left it to c.blow.
    void render(float *out, int n, const Control &in) {
        if (!m_d || n <= 0)
            return;
        // (Whatever comes in, within what a player can do.)
        Control c = in;
        auto sane = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? clampf(v, lo, hi) : def; };
        c.hz = sane(c.hz, 15.0f, 5000.0f, 220.0f);
        c.blow = sane(c.blow, 0.0f, 1.5f, 0.0f);
        c.lip = sane(c.lip, 0.0f, 1.0f, 0.5f);
        c.growl = sane(c.growl, 0.0f, 1.0f, 0.0f);
        c.air = sane(c.air, 0.0f, 1.0f, 0.0f);
        c.open = sane(c.open, 0.3f, 1.5f, 1.0f);
        // While the keys travel (or the lip and the ear move) the horn's
        // coefficients change fast: four samples at a time then, so they
        // glide rather than step at each block's edge.
        if ((m_moving || noteOf(c.hz) != m_s) && n > kStep) {
            Control part = c;
            const float b0 = m_blow;
            for (int at = 0; at < n; at += kStep) {
                const int m = std::min(kStep, n - at);
                part.blow = b0 + (c.blow - b0) * float(at + m) / float(n);
                renderBlock(out + at, m, part);
            }
            return;
        }
        renderBlock(out, n, c);
    }

private:
    static constexpr int kStep = 4;

    void renderBlock(float *out, int n, const Control &c) {
        setup(c, n);
        const float T = m_T;
        const float blow0 = m_blow, dBlow = (c.blow - m_blow) / float(n);
        m_blow = c.blow;
        const float pm = float(m_d->spec.pm) * m_open;
        float e = 0;
        int closed = 0;
        for (int i = 0; i < n; ++i) {
            // The bore's (and the tract's) response to everything before:
            // the history each mode carries into this sample.
            float V = 0;
            for (int k = 0; k < kAll; ++k) {
                const float zr = m_pr[k] * m_zr[k] - m_pi[k] * m_zi[k];
                const float zi = m_pr[k] * m_zi[k] + m_pi[k] * m_zr[k];
                m_zr[k] = zr;
                m_zi[k] = zi;
                V += zr;
            }
            V *= 2.0f;
            m_tractP = 2.0f * (m_zr[kMaxModes] + m_zr[kMaxModes + 1]);
            // The mouth: the breath, and the folds' hum through it.
            float gamma = blow0 + dBlow * float(i + 1);
            // The upper register needs support: blown softer, a saxophone
            // drops to the octave below, so the player never blows it so.
            gamma = std::min(gamma + m_lift * std::min(1.0f, gamma / 0.2f), m_cap);
            if (m_floor > 0)
                gamma = std::max(gamma, m_floor * std::min(1.0f, gamma / 0.3f));
            if (m_growl > 0) {
                m_glottis += m_humInc;
                if (m_glottis >= 1.0f) {
                    m_glottis -= 1.0f;
                    m_humInc = m_humBase * (1.0f + 0.012f * m_rng.bipolar()); // the voice's jitter
                }
                const float ph = m_glottis;
                const float open = ph < 0.62f ? 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * ph / 0.62f) : 0.0f;
                gamma *= 1.0f + m_growl * (open - 0.31f);
            }
            // The reed's opening, the tongue still lifting off it.
            float h = 1.0f + m_x;
            if (m_tongueOn) {
                m_tongue = std::min(m_tongue + m_tongueUp, 1.0f);
                if (m_tongue >= 1.0f && m_tongueHold-- <= 0)
                    m_tongueOn = false;
            } else if (m_tongue > 1e-4f) {
                m_tongue *= m_tongueFall;
            } else {
                m_tongue = 0;
            }
            if (m_tongue > 0)
                h *= 1.0f - m_tongue;
            h = std::max(h, 0.0f);
            closed += h <= 0.0f;
            // The reed rolls onto the curved lay, and the lip cushions it, so
            // the slit closes over the last of the swing rather than at once
            // (more so the more lip there is: subtone).
            h = h * h / (h + m_soft);
            // Bernoulli's flow through the slit, solved with the bore's
            // response to it: p = V + b·u, u = ζh·√(γ − p) (signed).
            const float W = gamma - V;
            const float zh = m_zeta * h, zh2 = zh * zh;
            const float bb = zh2 * m_b;
            float ub = 0.5f * (std::sqrt(bb * bb + 4.0f * zh2 * std::fabs(W)) - bb);
            if (W < 0)
                ub = -ub;
            // The jet's turbulence, as loud as the flow.
            const float noise = m_noiseF.step(m_rng.bipolar()).band;
            const float u = ub * (1.0f + m_sigma * noise);
            const float p = V + m_b * u;
            m_pLo = std::min(m_pLo, p);
            m_pHi = std::max(m_pHi, p);
            // Each mode takes the flow, and the horn radiates what its
            // modes hold (the tract's carry no weight).
            float rad = 0;
            for (int k = 0; k < kAll; ++k) {
                m_zr[k] += m_gr[k] * u;
                m_zi[k] += m_gi[k] * u;
                m_wre[k] += m_dwre[k];
                m_wim[k] += m_dwim[k];
                rad += m_wre[k] * m_zr[k] - m_wim[k] * m_zi[k];
            }
            m_uh2 += m_duh2;
            const float pb = p - m_tractP;
            m_lastP = pb;
            // The player's ear: the note as the reed swings to it (a clean
            // view of its fundamental, which the bore's pressure, all
            // harmonics on a low note, barely shows), heard against the note
            // wanted (turned down to it, smoothed twice).
            {
                const float rr = m_rotRe * m_incRe - m_rotIm * m_incIm;
                m_rotIm = m_rotRe * m_incIm + m_rotIm * m_incRe;
                m_rotRe = rr;
                const float heard = m_x - m_xMean;
                m_xMean += 0.001f * (m_x - m_xMean);
                m_xVar += m_earA * (heard * heard - m_xVar);
                m_e1Re += m_earA * (heard * m_rotRe - m_e1Re);
                m_e1Im += m_earA * (-heard * m_rotIm - m_e1Im);
                m_e2Re += m_earA * (m_e1Re - m_e2Re);
                m_e2Im += m_earA * (m_e1Im - m_e2Im);
                // (And at the octave above and below, for a cracked note.)
                const float o2Re = m_rotRe * m_rotRe - m_rotIm * m_rotIm, o2Im = 2 * m_rotRe * m_rotIm;
                m_upRe += m_earA * (heard * o2Re - m_upRe);
                m_upIm += m_earA * (-heard * o2Im - m_upIm);
                const float hr = m_halfRe * m_halfIncRe - m_halfIm * m_halfIncIm;
                m_halfIm = m_halfRe * m_halfIncIm + m_halfIm * m_halfIncRe;
                m_halfRe = hr;
                m_dnRe += m_earA * (heard * m_halfRe - m_dnRe);
                m_dnIm += m_earA * (-heard * m_halfIm - m_dnIm);
            }
            // (The high-passed flow, bilinear, twice.)
            const float h1 = m_hpB * (u - m_hpU) + m_hpA * m_hp1;
            m_hpU = u;
            const float h2 = m_hpB * (h1 - m_hp1) + m_hpA * m_hp2;
            m_hp1 = h1;
            m_hp2 = h2;
            rad = 2.0f * rad + m_uh2 * h2;
            // (No steady flow radiates.)
            rad = m_dc.step(rad) * pm;
            // (Lifted at the break.)
            {
                const float y = m_bq[0] * rad + m_bz[0];
                m_bz[0] = m_bq[1] * rad - m_bq[3] * y + m_bz[1];
                m_bz[1] = m_bq[2] * rad - m_bq[4] * y;
                rad = y;
            }
            out[i] += rad;
            e += rad * rad;
            // The reed, moved by the pressure across it, beating shut on the lay.
            const float delta = gamma - p;
            float xn = (m_c1 * m_x - m_c2 * m_xPrev - m_c3 * delta) * m_c0;
            if (xn < -1.0f) {
                xn = -1.0f;
                m_x = -1.0f;
            }
            m_xPrev = m_x;
            m_x = xn;
            (void)T;
        }
        if (!(std::isfinite(m_x) && std::isfinite(m_zr[0]) && std::isfinite(e))) {
            // Never: but a horn that has gone wrong goes quiet, not loud.
            for (int i = 0; i < n; ++i)
                if (!std::isfinite(out[i]))
                    out[i] = 0;
            e = 0;
            silence();
            m_live = true;
        }
        m_closedCount += closed;
        m_count += n;
        listen(c, n);
        const float k = 1.0f - std::exp(-float(n) / (0.03f * m_rate));
        m_energy += (e / float(n) - m_energy) * k;
    }

public:

    // Test access: a fingering's mode as it is now (after the lip).
    float modeHz(int k) const { return m_lw[std::clamp(k, 0, kAll - 1)] / (2 * float(M_PI)); }
    int noteMode() const { return m_note; }
    float ear() const { return m_ear; }
    float reedX() const { return m_x; }
    bool moving() const { return m_moving; }
    float lastP() const { return m_lastP; }
    void dump() const { const int li = std::clamp(m_s - m_d->sMin, 0, kLearn - 1); std::printf("x %g cap %g ceil %g find %g lift %g catch %g mag %g swing %g blow %g\n", m_x, m_cap, m_ceiling[li], m_find[li], m_lift, m_catch, std::hypot(m_e2Re, m_e2Im), std::sqrt(m_xVar), m_blow); }
    float lipVolume() const { return m_dC; }

    // The vocal tract at rest (Hz, Q, peak over Z_c): low, modest resonances
    // that leave the bore in charge (Scavone et al. 2008).
    static constexpr float kTractHz[kTract] = {200.0f, 800.0f};
    static constexpr float kTractQ[kTract] = {4.0f, 5.0f};
    static constexpr float kTractA[kTract] = {0.3f, 0.6f};

private:
    // The semitone a pitch is fingered on (equal-tempered from A440; what the
    // lip bends from there).
    int noteOf(float hz) const {
        const float s = 69.0f + 12.0f * std::log2(std::max(hz, 1.0f) / 440.0f) - float(m_d->spec.lowMidi);
        return std::clamp(int(std::lround(s)), m_d->sMin, m_d->sMax);
    }

    // The ear, once a block: how fast the note heard turns against the note
    // wanted is how far off it is. While the horn speaks near the note (not
    // in another regime, not silent), the lip draws it in over a few
    // hundredths of a second, as a player's does, and remembers each
    // note's correction for the next time it is played.
    void listen(const Control &c, int n) {
        const float mag = std::hypot(m_e2Re, m_e2Im);
        const float th = std::atan2(m_e2Im, m_e2Re);
        float d = th - m_earTh;
        d -= 2 * float(M_PI) * std::floor((d + float(M_PI)) / (2 * float(M_PI)));
        m_earTh = th;
        const float dt = float(n) * m_T;
        // How far off the note it sounds (a share of its frequency), as the
        // note heard turns against the note wanted.
        m_rel = d / (dt * 2 * float(M_PI) * std::max(c.hz, 20.0f));
        // (The rotator kept on the unit circle.)
        const float r = 1.0f / std::max(std::hypot(m_rotRe, m_rotIm), 1e-6f);
        m_rotRe *= r;
        m_rotIm *= r;
        const float rh = 1.0f / std::max(std::hypot(m_halfRe, m_halfIm), 1e-6f);
        m_halfRe *= rh;
        m_halfIm *= rh;
        // A cracked note: the horn has jumped to the octave above the note
        // (a low note blown too hard), or, in the upper register, fallen to
        // the octave below (blown too softly for the vent to hold it). A
        // player hears it at once and catches it, the lip on the reed and
        // the breath eased (or, below, more support), and blows that note
        // a little differently from then on.
        {
            const float up = std::hypot(m_upRe, m_upIm), dn = std::hypot(m_dnRe, m_dnIm);
            const bool upper = fingering().mode >= 2;
            const float swing = std::sqrt(std::max(m_xVar, 0.0f));
            // (Swinging, but not at the note: off by more than a semitone, or
            // the note hardly in it; below it in the upper register is a
            // fall, anything else a jump.)
            const bool off = swing > 0.15f && (mag < 0.3f * swing || std::fabs(m_rel) > 0.06f);
            const bool fell = upper && ((dn > 1.3f * mag && dn > 0.02f) || (off && m_rel < 0));
            const bool jumped = !fell && (off || (swing > 0.15f && up > 1.5f * mag && mag < 0.35f * swing));
            const bool cracked = fell || jumped;
            // (Not while the note is still speaking: an attack's transient
            // is not a crack.)
            m_settle -= dt;
            m_crackT = cracked && m_settle <= 0 ? m_crackT + dt : 0.0f;
            const int li = std::clamp(m_s - m_d->sMin, 0, kLearn - 1);
            if (m_crackT > 0.04f && m_catch <= 0) {
                if (fell)
                    m_support[li] = std::min(m_support[li] + 0.06f, 0.35f);
                else
                    m_ceiling[li] = std::max(std::min(m_ceiling[li], m_blow) - 0.05f, threshold() + 0.1f);
                m_catch = 0.06f;
                m_crackT = 0;
            }
            if (m_catch > 0)
                m_catch -= dt;
            // Blowing and not speaking: a little more breath, remembered.
            if (c.find && m_blow > 0.2f && mag < 0.02f && swing < 0.08f) {
                m_quiet += dt;
                // (Slowly, a note needs some periods to build up; and never
                // so far that the breath shuts the reed.)
                if (m_quiet > 0.08f) {
                    m_find[li] = std::min(m_find[li] + 0.015f, std::max(0.88f - m_blow, 0.0f));
                    m_quiet = 0.04f;
                }
            } else {
                m_quiet = 0;
            }
            // (What was learnt fades over some seconds.)
            m_ceiling[li] = std::min(m_ceiling[li] + 0.01f * dt, 2.0f);
            m_support[li] = std::max(m_support[li] - 0.01f * dt, 0.0f);
        }
        if (m_earHold > 0) {
            m_earHold -= dt;
            return;
        }
        if (m_rel > -1) {
            // (Locked on the note: within 6 %, and heard.)
            if (mag > 0.01f && std::fabs(m_rel) < 0.06f) {
                const float cents = 1200.0f * std::log2(1.0f + m_rel);
                m_ear = clampf(m_ear - cents * (1.0f - std::exp(-dt / 0.05f)), -150.0f, 150.0f);
                m_locked += dt;
                // (What it heard while locked, kept for when the note ends:
                // a note fading as the finger lifts loses the lock first.)
                if (m_locked > 0.1f) {
                    m_keptEar = m_ear;
                    m_kept = true;
                }
            } else {
                m_locked = 0;
            }
        }
    }

    // A fingering's modes become the targets; `now` jumps there (a new note).
    void load(const Fingering &f, bool now) {
        for (int k = 0; k < kMaxModes; ++k) {
            const bool has = k < f.n;
            m_tw0[k] = has ? f.w[k] : m_w[k] > 0 ? m_w[k] : 2 * float(M_PI) * 8000.0f;
            m_tq0[k] = has ? f.q[k] : m_q[k] > 0 ? m_q[k] : 2.0f;
            // (Under a vented note, the resonances below it weakened further
            // than the vent's line model leaves them: as computed, the
            // neck vent barely spoils the first mode of the short tubes, and
            // the top notes locked onto it at a forte.)
            m_ta0[k] = has ? f.a[k] * (k < f.note ? 0.6f : 1.0f) : 0.0f;
            m_trr[k] = has ? f.rr[k] : 0.0f;
            m_tri[k] = has ? f.ri[k] : 0.0f;
            if (now) {
                m_w[k] = m_tw0[k];
                m_q[k] = m_tq0[k];
                m_a[k] = m_ta0[k];
            }
        }
        m_tw0[kContinuum] = f.cw;
        m_tq0[kContinuum] = f.cq;
        m_ta0[kContinuum] = f.ca;
        m_trr[kContinuum] = f.rr[kMaxModes];
        m_tri[kContinuum] = f.ri[kMaxModes];
        if (now) {
            m_w[kContinuum] = f.cw;
            m_q[kContinuum] = f.cq;
            m_a[kContinuum] = f.ca;
            for (int k = 0; k < kAll; ++k) {
                m_wre[k] = m_trr[k];
                m_wim[k] = m_tri[k];
            }
            m_uh2 = f.uh2;
        }
        m_note = f.note;
    }

    // A block's coefficients: the fingering (keys moving over a few
    // milliseconds), the reed with the lip on it, the lip's bend onto the
    // note, the tract.
    void setup(const Control &c, int n) {
        const Design &d = *m_d;
        const Spec &sp = d.spec;
        const float T = m_T;
        const int s = noteOf(c.hz);
        if (s != m_s) {
            // (Each resonance slides from where it was to where the new
            // fingering puts it, as the keys travel. Across the octave key
            // the note moves to another resonance of the tube: the air
            // swinging at the note is carried into the new note's slot, so it
            // goes on sounding the note, not the vented first mode that has
            // taken its place.)
            const Fingering &was = d.at(m_s), &now = d.at(s);
            if (was.note != now.note) {
                const int a = was.note, b = now.note;
                std::swap(m_zr[a], m_zr[b]);
                std::swap(m_zi[a], m_zi[b]);
                std::swap(m_w[a], m_w[b]);
                std::swap(m_q[a], m_q[b]);
                std::swap(m_a[a], m_a[b]);
                std::swap(m_wre[a], m_wre[b]);
                std::swap(m_wim[a], m_wim[b]);
            }
            // (The ear remembers a note's correction only if it was locked
            // on it, and never much.)
            remember();
            m_settle = std::max(m_settle, 0.08f);
            m_s = s;
            load(now, false);
            m_ear = m_learn[std::clamp(s - d.sMin, 0, kLearn - 1)];
            m_earHold = 0.03f;
        }
        const Fingering &f = d.at(m_s);
        // The keys travel: a few milliseconds from one fingering to the next.
        const float kk = 1.0f - std::exp(-float(n) / (kKeyS * m_rate));
        for (int k = 0; k < kMaxModes + 1; ++k) {
            const int i = k < kMaxModes ? k : kContinuum;
            m_w[i] += (m_tw0[i] - m_w[i]) * kk;
            m_q[i] += (m_tq0[i] - m_q[i]) * kk;
            m_a[i] += (m_ta0[i] - m_a[i]) * kk;
        }
        // The reed with the lip on it: subtone, much lip and a dropped jaw
        // (heavily damped, a lower resonance); edge, a firm lip (lighter
        // damping, higher).
        const float lip = clampf(c.lip, 0.0f, 1.0f);
        // (A reed the breath pushes shut drives the bore only below its own
        // resonance, Wilson & Beavers 1974: as the note rises the player
        // firms the lip and keeps the reed's resonance above it, near twice
        // the note. Above the first register the embouchure is pinched,
        // with less of the lip on the reed: far lighter damping, which the
        // overblown modes need, as the lip's drag on the swing pulls the
        // note down and spends the energy they have less of.)
        const bool low = f.mode < 2 && !f.alt;
        const float wrWant = std::max(2 * float(M_PI) * float(sp.reedHz) * (low ? 0.8f + 0.1f * lip : 0.9f), 1.9f * 2 * float(M_PI) * c.hz);
        const float qrWant = float(sp.reedQ) * (low ? (m_catch > 0 ? 0.3f : 0.5f) : (m_catch > 0 ? 1.0f : kUpperQ));
        // (The embouchure changes over the keys' travel, not in a sample.)
        if (m_fresh) {
            m_wr = wrWant;
            m_qr = qrWant;
        }
        m_wr += (wrWant - m_wr) * kk;
        m_qr += (qrWant - m_qr) * kk;
        const float wr = m_wr, qr = m_qr;
        m_open += (clampf(c.open, 0.3f, 1.5f) - m_open) * (1.0f - std::exp(-float(n) / (0.01f * m_rate)));
        m_zeta = float(sp.zeta) * (0.85f + 0.35f * lip) * std::sqrt(m_open);
        {
            const float a = wr * T / (2 * qr);
            m_c0 = 1.0f / (1.0f + a);
            m_c1 = 2.0f - wr * wr * T * T;
            m_c2 = 1.0f - a;
            m_c3 = wr * wr * T * T;
        }
        // The reed itself pulls the note flat: its swing's share of the flow
        // lags the pressure (its damping), a susceptance Im Y_r at the note (Y_r: the flow the reed lets in
        // per pressure in the mouthpiece, linearised).
        const float g0 = std::sqrt(std::max(c.blow, 0.25f));
        auto reedB = [&](float w) {
            const float r = w / wr, dn = (1 - r * r) * (1 - r * r) + (r / qr) * (r / qr);
            return m_zeta * g0 * (-(r / qr) / dn);
        };
        // Intonation: the player tunes the horn (the mouthpiece on the cork,
        // the habit of the embouchure) so the fingered note sounds where it
        // should with the reed on it: every mode scaled alike, so the bore
        // keeps its own alignment. Mode m as a parallel resonance (peak A,
        // Q) sounds where its susceptance (Q/A)(ω/ω_m − ω_m/ω) meets the
        // reed's: x = ω_m/ω solves x² + (A·B_r/Q)·x − 1 = 0.
        const int m = f.note;
        const float wt = 2 * float(M_PI) * c.hz, ws = 2 * float(M_PI) * f.hz;
        // (Worked out on the fingering's own note mode, where the keys are
        // going, not on whatever mode is sliding through its slot.)
        const float aq = std::max(m_ta0[m], 1e-3f) / std::max(m_tq0[m], 0.5f);
        const float bs = aq * reedB(ws);
        const float scale0 = clampf(0.5f * (std::sqrt(bs * bs + 4) - bs) * ws / m_tw0[m] * std::exp2(m_ear / 1200.0f), 0.6f, 2.0f);
        m_incRe = std::cos(wt * T);
        m_incIm = std::sin(wt * T);
        m_halfIncRe = std::cos(0.5f * wt * T);
        m_halfIncIm = std::sin(0.5f * wt * T);
        {
            const int li = std::clamp(m_s - d.sMin, 0, kLearn - 1);
            // (What the player has learnt of each note's breath comes in
            // with the keys, not in a sample.)
            m_lift += ((c.find ? m_find[li] : 0.0f) - m_lift) * kk;
            m_floor += ((f.mode == 2 ? 0.6f : f.alt ? 0.5f : 0.0f) + m_support[li] - m_floor) * kk;
            // A catch: the breath eased to just over the note's threshold.
            const float cap = m_catch > 0 && f.mode < 2 ? thresholdOf(m_zeta, m_a[m]) + 0.05f : m_ceiling[li];
            m_cap += (cap - m_cap) * (m_catch > 0 ? 1.0f : kk);
        }
        // The lip: from the fingered note to the pitch wanted (a bend, the
        // vibrato, a slide between keys) by giving the reed more room or
        // less, an equivalent volume at the mouthpiece in parallel with
        // every mode. Mode k (compliance C = Q/(A·ω)) moves to ω·√(C/(C+ΔC)),
        // its peak the same, its Q up as much: the upper modes more, so a
        // bent note loses some of its alignment, as a real one does (see
        // below for the modes too broad to carry it).
        const float scale = scale0;
        const float wm = m_tw0[m] * scale;
        const float Cm = m_tq0[m] / (std::max(m_ta0[m], 1e-4f) * wm);
        // (Aimed where the ear has learnt this note must be aimed to sound
        // where it is wanted.)
        const float wa = wt * std::exp2(m_ear / 1200.0f);
        float dC = Cm * ((wm / wa) * (wm / wa) - 1.0f) + reedB(wa) / wa;
        dC = clampf(dC, -0.6f * Cm, 4.0f * Cm);
        // (The lip and the ear move as the keys do, not in a sample: across a
        // change of key the bend comes off one note and onto the next.)
        if (m_fresh) {
            m_dC = dC;
            m_scaleS = scale;
            m_fresh = false;
        }
        const float dCWant = dC;
        m_dC += (dC - m_dC) * kk;
        m_scaleS += (scale - m_scaleS) * kk;
        dC = m_dC;
        // (The note mode's own ratio; a mode that can't carry the volume as a
        // resonance, broad or with a compliance hardly bigger than the lip's
        // change, simply moves with the note.)
        const float Cn = m_q[m] / (std::max(m_a[m], 1e-6f) * m_w[m] * m_scaleS);
        const float rn = clampf(Cn / std::max(Cn + dC, 0.2f * Cn), 0.64f, 1.5625f);
        for (int k = 0; k < kMaxModes; ++k) {
            const float w = m_w[k] * m_scaleS;
            const float C = m_q[k] / (std::max(m_a[k], 1e-6f) * w);
            // (Handed over gradually between its own ratio and the note's, as
            // the lip's change nears its compliance: never a jump.)
            float r = rn;
            if (m_q[k] >= 3.0f && dC != 0.0f) {
                const float u = clampf((C / std::fabs(dC) - 3.0f) / 3.0f, 0.0f, 1.0f);
                r = rn + (clampf(C / (C + dC), 0.64f, 1.5625f) - rn) * u;
            } else if (m_q[k] >= 3.0f) {
                r = 1.0f;
            }
            m_lw[k] = w * std::sqrt(r);
            m_lq[k] = m_q[k] / std::sqrt(r);
        }
        m_lw[kContinuum] = m_w[kContinuum];
        m_lq[kContinuum] = m_q[kContinuum];
        // The vocal tract, in series with the bore: low, modest resonances
        // that leave the bore in charge (Chen, Smith & Wolfe 2011: peaks
        // near 150–250 Hz and 500–1100 Hz in ordinary playing, far under
        // the bore's). (A saxophonist's altissimo is held by tuning its first
        // resonance onto the note; the smaller horn that stands in for the
        // altissimo here needs no such help: docs/saxophone.md.)
        const float tw = kTractHz[0] * 2 * float(M_PI), ta = kTractA[0], tq = kTractQ[0];
        const float kt = 1.0f - std::exp(-float(n) / (0.02f * m_rate));
        m_w[kMaxModes] += (tw - m_w[kMaxModes]) * kt;
        m_a[kMaxModes] += (ta - m_a[kMaxModes]) * kt;
        m_q[kMaxModes] += (tq - m_q[kMaxModes]) * kt;
        for (int k = 0; k < kTract; ++k) {
            m_lw[kMaxModes + k] = m_w[kMaxModes + k];
            m_lq[kMaxModes + k] = m_q[kMaxModes + k];
        }
        // Discrete modes: each a parallel resonance Z(s) = A(ω/Q)s/(s² + (ω/Q)s
        // + ω²), by impulse invariance, its jump at the origin halved (so
        // the direct share b is Σ T·Aω/2Q).
        float b = 0;
        for (int k = 0; k < kAll; ++k) {
            const float w = std::min(m_lw[k], 0.45f * 2 * float(M_PI) * m_rate);
            const float q = std::max(m_lq[k], 0.55f);
            const float sg = w / (2 * q), wd = w * std::sqrt(std::max(1.0f - 1.0f / (4 * q * q), 0.01f));
            const float r = std::exp(-sg * T);
            m_pr[k] = r * std::cos(wd * T);
            m_pi[k] = r * std::sin(wd * T);
            const float re = m_a[k] * w / (2 * q);
            m_gr[k] = T * re;
            m_gi[k] = T * re * sg / wd;
            b += T * re;
        }
        m_b = b;
        // (Still moving: a mode not yet where its fingering puts it, or the
        // lip or the ear's scale still on its way.)
        {
            // (Only the keys and the ear's scale: the lip's slow wander, a
            // vibrato, moves little a block.)
            (void)dCWant;
            float mv = std::fabs(m_scaleS - scale) / scale;
            for (int k = 0; k < f.n; ++k)
                mv = std::max(mv, std::fabs(m_tw0[k] - m_w[k]) / m_tw0[k]);
            m_moving = mv > 0.002f;
        }
        // The radiation's weights, following the keys, sample by sample
        // across the block (a weight stepped at the block's edge would step
        // the sound).
        const float inv = 1.0f / float(n);
        m_duh2 = (f.uh2 - m_uh2) * kk * inv;
        for (int k = 0; k < kMaxModes + 1; ++k) {
            const int i = k < kMaxModes ? k : kContinuum;
            m_dwre[i] = (m_trr[i] - m_wre[i]) * kk * inv;
            m_dwim[i] = (m_tri[i] - m_wim[i]) * kk * inv;
        }
        m_soft = 0.02f + 0.3f * (1.0f - lip) * (1.0f - lip);
        // The jet's turbulence: more with the lip slack and the jaw down.
        m_sigma = 0.02f + 0.08f * float(m_d->spec.breath) * (1.0f - lip) * (1.0f - lip) + 0.1f * clampf(c.air, 0.0f, 1.0f);
        // The hum: a voice's pitch in its chest range under the note.
        m_growl = 0.45f * clampf(c.growl, 0.0f, 1.0f);
        float hum = c.hz * 0.41f;
        while (hum > 190.0f)
            hum *= 0.5f;
        while (hum < 85.0f)
            hum *= 2.0f;
        m_humBase = hum / m_rate;
        if (m_humInc <= 0)
            m_humInc = m_humBase;
    }

    const Design *m_d = nullptr;
    float m_rate = 48000, m_T = 1.0f / 48000;
    int m_s = 0, m_note = 0;
    bool m_live = false;
    // Modes: state, the fingering's targets, where they are now (keys
    // moving), after the lip, and the discrete coefficients.
    float m_zr[kAll] = {}, m_zi[kAll] = {};
    float m_tw0[kAll] = {}, m_tq0[kAll] = {}, m_ta0[kAll] = {};
    float m_w[kAll] = {}, m_q[kAll] = {}, m_a[kAll] = {};
    float m_lw[kAll] = {}, m_lq[kAll] = {};
    float m_pr[kAll] = {}, m_pi[kAll] = {}, m_gr[kAll] = {}, m_gi[kAll] = {};
    // The radiation.
    float m_trr[kAll] = {}, m_tri[kAll] = {}, m_wre[kAll] = {}, m_wim[kAll] = {}, m_tractP = 0;
    float m_dwre[kAll] = {}, m_dwim[kAll] = {}, m_duh2 = 0;
    float m_uh2 = 0, m_hpA = 0, m_hpB = 1, m_hpU = 0, m_hp1 = 0, m_hp2 = 0;
    float m_tw[kTract] = {}, m_tq[kTract] = {}, m_ta[kTract] = {};
    float m_b = 0, m_dC = 0, m_scaleS = 1;
    bool m_moving = false;
    bool m_fresh = true;
    // The reed.
    float m_x = 0, m_xPrev = 0, m_c0 = 1, m_c1 = 0, m_c2 = 0, m_c3 = 0, m_zeta = 0.45f;
    float m_tongue = 0, m_tongueFall = 0, m_tongueUp = 1;
    int m_tongueHold = 0;
    bool m_tongueOn = false;
    // The player.
    static constexpr int kLearn = 80;
    float m_learn[kLearn] = {};
    float m_ceiling[kLearn], m_support[kLearn] = {}, m_find[kLearn] = {}, m_catch = 0, m_crackT = 0, m_quiet = 0, m_lift = 0, m_settle = 0;
    float m_upRe = 0, m_upIm = 0, m_dnRe = 0, m_dnIm = 0, m_halfRe = 1, m_halfIm = 0, m_halfIncRe = 1, m_halfIncIm = 0;
    float m_rel = 0, m_locked = 0, m_keptEar = 0;
    bool m_kept = false;
    float m_ear = 0, m_xMean = 0, m_xVar = 0, m_earTh = 0, m_earHold = 0, m_earA = 0.004f, m_floor = 0;
    float m_rotRe = 1, m_rotIm = 0, m_incRe = 1, m_incIm = 0, m_e1Re = 0, m_e1Im = 0, m_e2Re = 0, m_e2Im = 0;
    float m_lastP = 0;
    float m_cap = 2.0f, m_open = 1, m_soft = 0.05f, m_wr = 4000, m_qr = 0.5f;
    float m_bq[5] = {1, 0, 0, 0, 0}, m_bz[2] = {};
    float m_blow = 0, m_sigma = 0.08f, m_growl = 0, m_glottis = 0, m_humBase = 0, m_humInc = 0;
    Svf m_noiseF;
    DcBlock m_dc;
    Rng m_rng{0x5A7u};
    float m_energy = 0, m_pLo = 1e9f, m_pHi = -1e9f;
    int m_closedCount = 0, m_count = 0;
};

}
