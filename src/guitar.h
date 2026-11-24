#pragma once

// An overdriven electric guitar, as physics: a solid-body guitar's steel
// string, picked, read by a humbucking pickup, through a cranked British
// valve amp and a mic'd 4x12. Not samples and not a recipe: each part is a
// model of the thing, with its constants taken from measurements and data
// sheets (docs/guitar.md maps every one; the survey behind them is
// docs/guitar-research.md).
//
//   the string  steel under tension, as a digital waveguide (a single delay
//               loop, Karjalainen, Välimäki & Tolonen 1998) for each of its
//               two polarisations, across the fretboard and toward the
//               pickup. Each loop has its losses (a one-pole loss filter
//               set from the decay times of the fundamental and of 3 kHz)
//               and its stiffness (first-order allpasses in cascade, Van
//               Duyne & Smith 1994, fitted to Fletcher's inharmonicity
//               f_n = n·f0·√(1 + B·n²), B = π³E·d⁴ / (64·T·L²)). The two
//               polarisations are tuned a hair apart and decay at their own
//               rates; the bridge passes energy between them, so the pickup
//               hears partials beat and a two-stage decay. A string swung
//               wide is stretched, so it starts a few cents sharp and settles
//               (tension modulation, Tolonen, Välimäki & Karjalainen 2000).
//   the pick    leaves the string in a triangle with its apex where it
//               pulled it, released through the plectrum's rounded edge: the
//               pluck position's comb, sin(nπβ)/n², and a roll-off that is
//               higher the harder the stroke. Its angle sets how much goes
//               into each polarisation. A pick landing on a ringing string
//               takes most of its swing first.
//   the fret    the string's length is the fret's: a slide steps fret by
//               fret, a hammer-on cuts the string short at once and taps it,
//               a pull-off plucks it. A bend raises the tension: the pitch
//               (as √T), the stiffness (as 1/T), and pushes the string
//               sideways over the pickup's poles.
//   the pickup  a humbucker near the bridge. Each coil senses the string's
//               displacement where it sits (a comb with notches at the
//               harmonics whose nodes fall there), through the steel's
//               magnetic field, which grows as the string nears the pole
//               (1/r², so the swing toward it is louder than the one away:
//               even harmonics), and sees its velocity (Faraday's law). Its
//               coil (4.4 H) and the cable's capacitance resonate, loaded by
//               the guitar's pots and the amp, around 3 kHz.
//   the amp     Marshall's JCM800 2204 as its 1981 drawing has it: three
//               12AX7 stages, each from Koren's triode model on its load
//               line, tabulated. A grid driven past its cathode conducts and
//               charges its coupling capacitor, shifting the bias (blocking);
//               the input stage's bypassed cathode creeps with the average
//               current; the second stage is biased cold, so it clips one
//               side first (even harmonics). A cathode follower, the
//               Fender/Marshall tone stack (Yeh & Smith 2006, here from the
//               circuit's own nodal equations), a phase inverter, then a
//               push-pull pair of EL34s on a supply that sags as they draw
//               current, into an output transformer. Run at four times the
//               rate, every nonlinearity antialiased by its antiderivative.
//   the cab     a Celestion-style 12" speaker in a closed 4x12, close-mic'd:
//               the box and the speaker's resonance, the cone's break-up
//               modes and the steep fall above them, as resonances.
//   feedback    the cab's sound reaches the string again through the air
//               and the body. At stage volume the loop gain at some partial
//               passes one; the amp's saturation then sets how loud the
//               string is kept ringing, and a held note sustains, often on
//               a harmonic. How close the player stands is the pressure.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "dsp.h"

namespace tf::guitar {

// --- the strings ----------------------------------------------------------------------------------------------
//
// A 10–46 set on a 24.75" (628 mm) scale (a Les Paul's). Tensions are
// D'Addario's published ones for the 25.5" scale times (24.75/25.5)²;
// a wound string's stiffness is its core's.

constexpr double kScale = 0.628;   // m, nut to saddle
constexpr double kSteelE = 2.0e11; // Pa
constexpr double kPi_ = 3.14159265358979323846;

struct StringSpec {
    float midi;    // open, concert pitch
    float core;    // m: the steel that bends (a plain string's diameter, a wound one's core)
    float tension; // N at pitch on kScale
    float t60;     // s: the open string's fundamental (the slower polarisation)
    float t60hi;   // s: at 3 kHz
    bool wound;
};
constexpr int kStrings = 6;
inline const StringSpec &stringSpec(int s) {
    static const StringSpec specs[kStrings] = {
        {40.0f, 0.483e-3f, 73.3f, 18.0f, 2.2f, true},  // E2 .046w
        {45.0f, 0.394e-3f, 81.7f, 16.5f, 2.3f, true},  // A2 .036w
        {50.0f, 0.292e-3f, 77.1f, 15.0f, 2.4f, true},  // D3 .026w
        {55.0f, 0.432e-3f, 69.5f, 13.5f, 3.6f, false}, // G3 .017
        {59.0f, 0.330e-3f, 64.5f, 12.0f, 3.8f, false}, // B3 .013
        {64.0f, 0.254e-3f, 67.9f, 10.5f, 4.1f, false}, // E4 .010
    };
    return specs[std::clamp(s, 0, kStrings - 1)];
}
// Fletcher's inharmonicity coefficient of the open string.
inline double openB(const StringSpec &s) {
    const double d2 = double(s.core) * s.core;
    return kPi_ * kPi_ * kPi_ * kSteelE * d2 * d2 / (64.0 * s.tension * kScale * kScale);
}
// The steel's stretch stiffness EA over its tension: how far a swing (or a
// bend) changes it.
inline double stretchOverT(const StringSpec &s) {
    return kSteelE * kPi_ * 0.25 * double(s.core) * s.core / s.tension;
}
// Which string a lead player takes a note on: the one that puts it nearest
// the seventh fret (the middle of the neck, where lead lines live), never
// under the open string or past the 22nd fret.
inline int stringFor(float midi) {
    int best = midi < stringSpec(0).midi ? 0 : kStrings - 1;
    float bestD = 1e9f;
    for (int s = 0; s < kStrings; ++s) {
        const float fret = midi - stringSpec(s).midi;
        if (fret < -0.3f || fret > 22.3f)
            continue;
        const float d = std::fabs(fret - 7.0f) + (fret < 0.7f ? 2.0f : 0.0f); // an open string is a last resort
        if (d < bestD) {
            bestD = d;
            best = s;
        }
    }
    return best;
}

// --- the pickup -----------------------------------------------------------------------------------------------
//
// A humbucker under the bridge: its two coils' centres either side of
// kPickupD from the saddle, kCoilGap apart; the string kPoleGap over the pole
// pieces, a little off their axis (kPoleOff) as no string sits exactly over
// its pole. Electrically a source in series with the coil's resistance and
// inductance, across its own and the cable's capacitance, loaded by the
// volume and tone pots and the amp's input.

constexpr float kPickupD = 0.042f;   // m, from the saddle to the pickup's middle
constexpr float kCoilGap = 0.018f;   // m, between the coils' centres
constexpr float kPoleGap = 2.5f;     // mm, string to pole piece
constexpr float kPoleOff = 0.6f;     // mm, string off the pole's axis
constexpr double kCoilL = 4.4;       // H
constexpr double kCoilR = 8.2e3;     // Ω
constexpr double kCoilC = 130e-12;   // F, the winding's own
constexpr double kCableC = 450e-12;  // F, 4.5 m of cable
constexpr double kLoadR = 200e3;     // Ω: 500k volume ∥ 500k tone ∥ 1M input
constexpr float kEmf = 7.0e-5f;      // V·s per unit of flux (a unit per mm where the string rests): a hard stroke peaks near 1 V, an ordinary one some 100–300 mV

// --- small linear filters --------------------------------------------------------------------------------------

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float step(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) {
        b0 = float(B0 / A0);
        b1 = float(B1 / A0);
        b2 = float(B2 / A0);
        a1 = float(A1 / A0);
        a2 = float(A2 / A0);
    }
    // The RBJ cookbook's.
    void peak(double hz, double q, double db, double rate) {
        const double A = std::pow(10.0, db / 40.0), w = 2 * kPi_ * hz / rate, al = std::sin(w) / (2 * q), c = std::cos(w);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowpass(double hz, double q, double rate) {
        const double w = 2 * kPi_ * hz / rate, al = std::sin(w) / (2 * q), c = std::cos(w);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(double hz, double q, double rate) {
        const double w = 2 * kPi_ * hz / rate, al = std::sin(w) / (2 * q), c = std::cos(w);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highshelf(double hz, double db, double rate) {
        const double A = std::pow(10.0, db / 40.0), w = 2 * kPi_ * hz / rate, c = std::cos(w);
        const double al = std::sin(w) / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
            (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

// A first-order section by the bilinear transform: (b0 + b1·s)/(a0 + a1·s).
struct OnePole {
    float b0 = 1, b1 = 0, a1 = 0, z = 0;
    void set(double B0, double B1, double A0, double A1, double rate) {
        const double K = 2 * rate, d = A0 + A1 * K;
        b0 = float((B0 + B1 * K) / d);
        b1 = float((B0 - B1 * K) / d);
        a1 = float((A0 - A1 * K) / d);
    }
    void lowpass(double hz, double rate) { set(1, 0, 1, 1 / (2 * kPi_ * hz), rate); }
    void highpass(double hz, double rate) { set(0, 1 / (2 * kPi_ * hz), 1, 1 / (2 * kPi_ * hz), rate); }
    inline float step(float x) {
        const float y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
    void reset() { z = 0; }
};

// The Fender/Marshall treble–middle–bass stack: C1 from the input to the
// treble pot's top, the slope resistor R4 from the input to C2 (to the
// treble pot's foot and the bass pot) and C3 (to the mid pot); the bass pot
// a variable resistor down to the mid pot, the mid pot one to ground; out
// at the treble pot's wiper. Its transfer function is third order (Yeh &
// Smith, DAFx 2006). Here it comes from the circuit itself: by nodal
// analysis, the output is a ratio of two determinants of the admittance
// matrix, each a polynomial of the third degree in s (every capacitor
// appears in it at most once), whose coefficients four evaluations round a
// circle in the s-plane give exactly (a discrete Fourier transform); then
// the bilinear transform. Its input is driven by the cathode follower (a
// low impedance) and its output feeds a high one, as the analysis assumes.
struct ToneStack {
    // (In double: a third-order section's poles lie close to z = 1 at
    // twice the rate, where single precision moves them.)
    double b[4] = {1, 0, 0, 0}, a[4] = {1, 0, 0, 0}, z[3] = {};
    // Marshall 2203 values: C1 470p, C2 22n, C3 22n; treble 220k, bass 1M, mid 25k, slope 33k.
    static constexpr double C1 = 470e-12, C2 = 22e-9, C3 = 22e-9, R1 = 220e3, R2 = 1e6, R3 = 25e3, R4 = 33e3;
    using cplx = std::complex<double>;
    static cplx det5(cplx A[5][5]) {
        cplx d = 1;
        for (int c = 0; c < 5; ++c) {
            int piv = c;
            for (int r = c + 1; r < 5; ++r)
                if (std::abs(A[r][c]) > std::abs(A[piv][c]))
                    piv = r;
            if (piv != c) {
                for (int j = 0; j < 5; ++j)
                    std::swap(A[c][j], A[piv][j]);
                d = -d;
            }
            d *= A[c][c];
            if (std::abs(A[c][c]) == 0.0)
                return 0;
            for (int r = c + 1; r < 5; ++r) {
                const cplx k = A[r][c] / A[c][c];
                for (int j = c; j < 5; ++j)
                    A[r][j] -= k * A[c][j];
            }
        }
        return d;
    }
    // The admittance matrix's determinant, and its numerator for the
    // output (Cramer), at s (nodes: treble top, wiper, treble foot, slope, mid).
    static void dets(cplx s, double t, double m, double l, cplx &num, cplx &den) {
        cplx Y[5][5] = {}, I[5] = {};
        auto add = [&](int p, int q, cplx y) { // q = −1 ground, −2 the input (1 V)
            Y[p][p] += y;
            if (q >= 0) {
                Y[q][q] += y;
                Y[p][q] -= y;
                Y[q][p] -= y;
            }
            if (q == -2)
                I[p] += y;
        };
        add(0, -2, s * C1);
        add(0, 1, 1 / std::max((1 - t) * R1, 10.0));
        add(1, 2, 1 / std::max(t * R1, 10.0));
        add(3, -2, 1 / R4);
        add(3, 2, s * C2);
        add(3, 4, s * C3);
        add(2, 4, 1 / std::max(l * R2, 10.0));
        add(4, -1, 1 / std::max(m * R3, 10.0));
        cplx A[5][5];
        std::copy(&Y[0][0], &Y[0][0] + 25, &A[0][0]);
        den = det5(A);
        for (int r = 0; r < 5; ++r)
            Y[r][1] = I[r];
        num = det5(Y);
    }
    // The analog response, for checking.
    static cplx analog(double hz, double t, double m, double l) {
        cplx n, d;
        dets(cplx(0, 2 * kPi_ * hz), t, m, l, n, d);
        return n / d;
    }
    void set(double t, double m, double l, double rate) {
        t = std::clamp(t, 0.0, 1.0);
        m = std::clamp(m, 0.0, 1.0);
        l = std::clamp(l, 0.0, 1.0);
        // Coefficients of s^k times r^k, from four points on |s| = r.
        const double r = 2 * kPi_ * 300;
        cplx N[4], D[4];
        for (int j = 0; j < 4; ++j)
            dets(r * std::polar(1.0, 2 * kPi_ * (j + 0.125) / 4), t, m, l, N[j], D[j]);
        double bs[4], as[4];
        for (int k = 0; k < 4; ++k) {
            cplx nk = 0, dk = 0;
            for (int j = 0; j < 4; ++j) {
                const cplx w = std::polar(1.0, -2 * kPi_ * k * (j + 0.125) / 4);
                nk += N[j] * w;
                dk += D[j] * w;
            }
            bs[k] = (nk / 4.0).real() / std::pow(r, k);
            as[k] = (dk / 4.0).real() / std::pow(r, k);
        }
        // s → K(1 − z⁻¹)/(1 + z⁻¹), times (1 + z⁻¹)³.
        static const double P[4][4] = {{1, 3, 3, 1}, {1, 1, -1, -1}, {1, -1, -1, 1}, {1, -3, 3, -1}};
        const double K = 2 * rate;
        double nb[4] = {}, na[4] = {}, Kk = 1;
        for (int k = 0; k < 4; ++k, Kk *= K)
            for (int j = 0; j < 4; ++j) {
                nb[j] += bs[k] * Kk * P[k][j];
                na[j] += as[k] * Kk * P[k][j];
            }
        for (int j = 0; j < 4; ++j) {
            b[j] = nb[j] / na[0];
            a[j] = na[j] / na[0];
        }
    }
    inline float step(float xf) {
        // Transposed direct form II.
        const double x = xf, y = b[0] * x + z[0];
        z[0] = b[1] * x - a[1] * y + z[1];
        z[1] = b[2] * x - a[2] * y + z[2];
        z[2] = b[3] * x - a[3] * y;
        return float(y);
    }
    void reset() { z[0] = z[1] = z[2] = 0; }
};

// --- four times the rate -----------------------------------------------------------------------------------------
//
// Kaiser-windowed half-band FIRs run polyphase: every other coefficient is
// zero but the middle one, so a stage costs K multiply-adds a sample each
// way. Two stages: from the page's rate to twice it with K = 12 (47 taps:
// pass band to 18 kHz, stop band from 30 kHz at 48 kHz, about 80 dB down),
// then to four times it with K = 5 (19 taps: everything under 20 kHz passes,
// the stop band starts at 76 kHz).

template <int K>
inline const float *halfband() {
    static const std::vector<float> g = [] {
        std::vector<float> c(K);
        auto bessel0 = [](double x) {
            double s = 1, t = 1;
            for (int k = 1; k < 30; ++k) {
                t *= (x / (2 * k)) * (x / (2 * k));
                s += t;
            }
            return s;
        };
        const double beta = 8.0, half = 2.0 * K - 1.0;
        double sum = 0;
        for (int j = 0; j < K; ++j) {
            const double k = 2.0 * j + 1.0; // offset from the middle
            const double r = k / half;
            const double w = bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / bessel0(beta);
            c[size_t(j)] = float(std::sin(kPi_ * k / 2) / (kPi_ * k) * w);
            sum += 2 * c[size_t(j)];
        }
        for (float &v : c)
            v = float(v * 0.5 / sum); // the odd taps sum to one half, the middle is the other
        return c;
    }();
    return g.data();
}

template <int K>
struct Up2 {
    float x[2 * K] = {};
    int p = 0;
    // One sample in, two out (in time order).
    inline void step(float in, float &y0, float &y1) {
        const float *g = halfband<K>();
        p = p == 0 ? 2 * K - 1 : p - 1;
        x[p] = in;
        auto at = [&](int d) { return x[(p + d) % (2 * K)]; };
        float acc = 0;
        for (int j = 0; j < K; ++j)
            acc += g[j] * (at(K - 1 - j) + at(K + j));
        y0 = 2.0f * acc;
        y1 = at(K - 1);
    }
    void reset() { std::fill(std::begin(x), std::end(x), 0.0f), p = 0; }
};

template <int K>
struct Down2 {
    float e[2 * K] = {}, o[2 * K] = {};
    int p = 0;
    // Two samples in (in time order), one out.
    inline float step(float u0, float u1) {
        const float *g = halfband<K>();
        p = p == 0 ? 2 * K - 1 : p - 1;
        e[p] = u0;
        o[p] = u1;
        auto E = [&](int d) { return e[(p + d) % (2 * K)]; };
        float acc = 0.5f * o[(p + K) % (2 * K)];
        for (int j = 0; j < K; ++j)
            acc += g[j] * (E(K - 1 - j) + E(K + j));
        return acc;
    }
    void reset() {
        std::fill(std::begin(e), std::end(e), 0.0f);
        std::fill(std::begin(o), std::end(o), 0.0f);
        p = 0;
    }
};

// --- valves ------------------------------------------------------------------------------------------------------
//
// Koren's models (1996, 2003): a triode's plate current from its grid and
// plate voltages, E1 = (Vpk/kp)·ln(1 + exp(kp·(1/µ + Vgk/√(kvb + Vpk²)))),
// Ip = 2·E1^ex / kg1; a pentode's the same from the screen voltage, its
// plate a current source down to the knee. The grid conducts to the cathode
// through about 2 kΩ once positive.

struct Koren {
    double mu, ex, kg1, kp, kvb, rgi;
};
constexpr Koren k12AX7{100.0, 1.4, 1060.0, 600.0, 300.0, 2000.0};
constexpr Koren kEL34{11.0, 1.35, 650.0, 60.0, 24.0, 1000.0};

inline double softplus(double x) { return x > 30 ? x : x < -30 ? std::exp(x) : std::log1p(std::exp(x)); }
inline double korenIp(const Koren &t, double vgk, double vpk) {
    if (vpk <= 0)
        return 0;
    const double e1 = vpk / t.kp * softplus(t.kp * (1.0 / t.mu + vgk / std::sqrt(t.kvb + vpk * vpk)));
    return e1 > 0 ? 2.0 * std::pow(e1, t.ex) / t.kg1 : 0.0;
}
// Grid current, smoothed over the last few tens of millivolts before it starts.
inline double gridI(const Koren &t, double vgk) { return 0.04 * softplus(vgk / 0.04) / t.rgi; }

// One 12AX7 stage, its static part solved once and tabulated against
// w, the voltage that drives it (the coupling capacitor's far side, less a
// bypassed cathode's own): through the source's resistance to the grid,
// which conducts when positive; the plate on its load line; an unbypassed
// cathode resistor's feedback inside. Stored: the plate voltage (as it
// leaves its resting value), its antiderivative for antialiasing, the
// cathode current and the grid current.
struct TriodeSpec {
    double B, Rp, Rk, Rs; // supply, plate load, unbypassed cathode resistor (0: bypassed), source resistance
};
struct TriodeTable {
    double w0 = 0, dw = 1, inv = 1;
    int n = 0;
    std::vector<float> vp, ik, ig;
    std::vector<double> F;
    double vq = 0;  // resting plate voltage
    double wq = 0;  // resting drive (a bypassed cathode's bias, as −vk)
    float ikq = 0;
    void build(const TriodeSpec &s, double lo, double hi, double step, const Koren &k = k12AX7) {
        // Along the grid–cathode voltage, where everything is explicit but
        // the plate current, then onto an even grid of w.
        std::vector<double> W, P, K, G;
        double ip = 0;
        for (double vgk = -120.0; vgk <= 40.0; vgk += 0.005) {
            const double ig = gridI(k, vgk);
            // Ip = Koren(vgk, B − Rp·Ip − Rk·(Ip + ig)): Newton from the last one, bisection if it strays.
            auto f = [&](double i) { return i - korenIp(k, vgk, s.B - s.Rp * i - s.Rk * (i + ig)); };
            double lo2 = 0, hi2 = s.B / (s.Rp + s.Rk + 1e-9);
            bool ok = false;
            for (int it = 0; it < 12; ++it) {
                const double fx = f(ip), h = 1e-9 + 1e-6 * ip, d = (f(ip + h) - fx) / h;
                const double nx = ip - fx / d;
                if (!(nx >= lo2 && nx <= hi2))
                    break;
                if (std::fabs(nx - ip) < 1e-13) {
                    ip = nx;
                    ok = true;
                    break;
                }
                ip = nx;
            }
            if (!ok) {
                for (int it = 0; it < 80; ++it) {
                    const double mid = 0.5 * (lo2 + hi2);
                    (f(mid) > 0 ? hi2 : lo2) = mid;
                }
                ip = 0.5 * (lo2 + hi2);
            }
            W.push_back(vgk + s.Rk * (ip + ig) + s.Rs * ig);
            P.push_back(s.B - s.Rp * ip);
            K.push_back(ip + ig);
            G.push_back(ig);
        }
        w0 = lo;
        dw = step;
        inv = 1.0 / step;
        n = int((hi - lo) / step) + 1;
        vp.resize(size_t(n));
        ik.resize(size_t(n));
        ig.resize(size_t(n));
        size_t j = 0;
        for (int i = 0; i < n; ++i) {
            const double w = lo + i * step;
            while (j + 2 < W.size() && W[j + 1] < w)
                ++j;
            const double t = std::clamp((w - W[j]) / std::max(W[j + 1] - W[j], 1e-12), 0.0, 1.0);
            vp[size_t(i)] = float(P[j] + (P[j + 1] - P[j]) * t);
            ik[size_t(i)] = float(K[j] + (K[j + 1] - K[j]) * t);
            ig[size_t(i)] = float(G[j] + (G[j + 1] - G[j]) * t);
        }
        // At rest: an unbypassed stage's grid sits at 0 V; a bypassed one's
        // cathode at Rk·Ik, which the drive then sees as −vk.
        wq = 0;
        vq = at(vp, 0.0);
        ikq = float(at(ik, 0.0));
        F.resize(size_t(n));
        F[0] = 0;
        for (int i = 1; i < n; ++i)
            F[size_t(i)] = F[size_t(i - 1)] + 0.5 * dw * (double(vp[size_t(i - 1)]) + vp[size_t(i)] - 2 * vq);
    }
    // Where a bypassed cathode (Rk) rests.
    void settleBypassed(double Rk) {
        double lo = -20, hi = 0;
        for (int it = 0; it < 80; ++it) {
            const double mid = 0.5 * (lo + hi);
            // w = −Rk·Ik(w)
            (mid + Rk * at(ik, mid) > 0 ? hi : lo) = mid;
        }
        wq = 0.5 * (lo + hi);
        vq = at(vp, wq);
        ikq = float(at(ik, wq));
        F[0] = 0;
        for (int i = 1; i < n; ++i)
            F[size_t(i)] = F[size_t(i - 1)] + 0.5 * dw * (double(vp[size_t(i - 1)]) + vp[size_t(i)] - 2 * vq);
    }
    double at(const std::vector<float> &v, double w) const {
        const double x = (w - w0) * inv;
        if (x <= 0)
            return v[0];
        if (x >= n - 1)
            return v[size_t(n - 1)];
        const int i = int(x);
        const double t = x - i;
        return v[size_t(i)] + (double(v[size_t(i + 1)]) - v[size_t(i)]) * t;
    }
    // All of it at once, one index: ∫(vp − vq), the plate voltage (less its
    // resting value), the cathode current and the grid current.
    inline void look(double w, double &F_, double &v, double &k, double &g) const {
        const double x = (w - w0) * inv;
        if (x <= 0 || x >= n - 1) {
            F_ = Fat(w);
            v = at(vp, w) - vq;
            k = at(ik, w);
            g = at(ig, w);
            return;
        }
        const int i = int(x);
        const double t = x - i, a = double(vp[size_t(i)]) - vq, b = double(vp[size_t(i + 1)]) - vq;
        F_ = F[size_t(i)] + dw * (a * t + 0.5 * (b - a) * t * t);
        v = a + (b - a) * t;
        k = ik[size_t(i)] + (ik[size_t(i + 1)] - ik[size_t(i)]) * t;
        g = ig[size_t(i)] + (ig[size_t(i + 1)] - ig[size_t(i)]) * t;
    }
    // ∫ (vp − vq) dw from w0, exact for the table's linear pieces.
    double Fat(double w) const {
        const double x = (w - w0) * inv;
        if (x <= 0)
            return (w - w0) * (double(vp[0]) - vq);
        if (x >= n - 1)
            return F[size_t(n - 1)] + (w - (w0 + (n - 1) * dw)) * (double(vp[size_t(n - 1)]) - vq);
        const int i = int(x);
        const double t = x - i, a = double(vp[size_t(i)]) - vq, b = double(vp[size_t(i + 1)]) - vq;
        return F[size_t(i)] + dw * (a * t + 0.5 * (b - a) * t * t);
    }
};

// A memoryless curve, tabulated with its antiderivative, for first-order
// antiderivative antialiasing: y = (F(x) − F(x₁))/(x − x₁) (Parker,
// Zavalishin & Le Bivic, DAFx 2016), which a cubic-free table makes cheap.
// Past its ends it carries on straight.
struct Curve {
    double w0 = 0, dw = 1, inv = 1;
    int n = 0;
    std::vector<float> f;
    std::vector<double> F;
    template <typename Fn>
    void build(Fn fn, double lo, double hi, double step) {
        w0 = lo;
        dw = step;
        inv = 1 / step;
        n = int((hi - lo) / step) + 1;
        f.resize(size_t(n));
        F.resize(size_t(n));
        for (int i = 0; i < n; ++i)
            f[size_t(i)] = float(fn(lo + i * step));
        F[0] = 0;
        for (int i = 1; i < n; ++i)
            F[size_t(i)] = F[size_t(i - 1)] + 0.5 * dw * (double(f[size_t(i - 1)]) + f[size_t(i)]);
    }
    inline double at(double w) const {
        const double x = (w - w0) * inv;
        if (x <= 0)
            return f[0];
        if (x >= n - 1)
            return f[size_t(n - 1)];
        const int i = int(x);
        return f[size_t(i)] + (double(f[size_t(i + 1)]) - f[size_t(i)]) * (x - i);
    }
    inline double Fat(double w) const {
        const double x = (w - w0) * inv;
        if (x <= 0)
            return (w - w0) * f[0];
        if (x >= n - 1)
            return F[size_t(n - 1)] + (w - (w0 + (n - 1) * dw)) * f[size_t(n - 1)];
        const int i = int(x);
        const double t = x - i, a = f[size_t(i)], b = f[size_t(i + 1)];
        return F[size_t(i)] + dw * (a * t + 0.5 * (b - a) * t * t);
    }
};
// A curve run antialiased: it remembers where it was, and its antiderivative there.
struct Adaa {
    double w1 = 0, F1 = 0;
    void reset(const Curve &c, double w) {
        w1 = w;
        F1 = c.Fat(w);
    }
    inline double step(const Curve &c, double w, double eps) {
        const double F = c.Fat(w), d = w - w1;
        const double y = std::fabs(d) > eps ? (F - F1) / d : c.at(0.5 * (w + w1));
        w1 = w;
        F1 = F;
        return y;
    }
};

// The power valves' supply, between these voltages: the rows of their tables.
constexpr int kSupplyRows = 8;
constexpr double kSupplyLo = 360, kSupplyStep = 20;

// The tables, built once (never on the audio thread): the three preamp
// stages, the phase inverter, the cathode follower, the EL34s and the
// output's knee.
struct Tables {
    TriodeTable first, cold, third;
    Curve inverter;     // the phase inverter's swing, against its 70 V limit: tanh
    Curve follower;     // the cathode follower's squash, volts
    Curve knee;         // the plates' approach to their knee: tanh
    // One EL34 driven through its coupling capacitor (a, the drive less
    // the capacitor's charge), its grid conducting through the inverter's
    // 40k once past the cathode: its plate and screen current (A) per
    // supply row, and its grid current.
    Curve el34[kSupplyRows];
    Curve el34Grid;
    double el34Bias = -37; // V: what puts 36 mA through each valve at rest
};
// Koren's pentode current from the grid and the screen (its plate above the knee).
inline double pentodeI(double vg1, double vg2) {
    const double e1 = vg2 / kEL34.kp * softplus(kEL34.kp * (1.0 / kEL34.mu + vg1 / vg2));
    return e1 > 0 ? 2.0 * std::pow(e1, kEL34.ex) / kEL34.kg1 : 0.0;
}
constexpr double kInverterR = 40e3; // Ω: the inverter's plate resistance and the 5k6 grid stopper, as the EL34's grid sees them
inline const Tables &tables() {
    static const Tables t = [] {
        Tables T;
        // Marshall's 1981 drawing of the 2204 (50 W). The input stage: 100k
        // plate on a 256 V node, 2k7 cathode bypassed by 0.68 µF, 68k grid
        // stopper (and the pickup's own resistance).
        T.first.build({256, 100e3, 0, 76e3}, -60, 60, 0.05);
        T.first.settleBypassed(2.7e3);
        // The cold stage: 100k plate on 264 V, 10k cathode unbypassed;
        // driven from the volume pot through 470k ∥ 470 pF.
        T.cold.build({264, 100e3, 10e3, 470e3}, -120, 160, 0.1);
        // The third: 100k plate on 264 V, 820 Ω cathode unbypassed; driven
        // from the cold stage's plate (about 90k) through 470k.
        T.third.build({264, 100e3, 820, 560e3}, -120, 160, 0.1);
        T.inverter.build([](double x) { return std::tanh(x); }, -12, 12, 0.01);
        T.knee.build([](double x) { return std::tanh(x); }, -12, 12, 0.01);
        T.follower.build([](double x) { return x <= 25 ? x : 25 + (x - 25) / (1 + (x - 25) / 30); }, -400, 400, 0.25);
        // The bias that idles each valve at 36 mA on its 460 V plate (one 2203 measured: 451 V, 36 mA).
        double lo = -80, hi = 0;
        for (int it = 0; it < 80; ++it) {
            const double mid = 0.5 * (lo + hi);
            (pentodeI(mid, 460.0) > 0.036 ? hi : lo) = mid;
        }
        T.el34Bias = 0.5 * (lo + hi);
        // Along the grid–cathode voltage, explicit; then onto an even grid of a.
        std::vector<double> A, G, V;
        for (double vg = -300; vg <= 40; vg += 0.005) {
            const double ig = gridI(kEL34, vg);
            A.push_back(vg + kInverterR * ig - T.el34Bias);
            V.push_back(vg);
            G.push_back(ig);
        }
        auto along = [&](const std::vector<double> &Y, double a) {
            const size_t j = size_t(std::lower_bound(A.begin(), A.end(), a) - A.begin());
            if (j == 0)
                return Y[0];
            if (j >= A.size())
                return Y.back();
            const double u = (a - A[j - 1]) / std::max(A[j] - A[j - 1], 1e-12);
            return Y[j - 1] + (Y[j] - Y[j - 1]) * u;
        };
        T.el34Grid.build([&](double a) { return along(G, a); }, -260, 400, 0.5);
        for (int r = 0; r < kSupplyRows; ++r) {
            const double B = kSupplyLo + r * kSupplyStep;
            T.el34[r].build([&](double a) { return pentodeI(along(V, a), B); }, -260, 400, 0.5);
        }
        return T;
    }();
    return t;
}

// A triode stage running: the table, its coupling capacitor and grid leak,
// a bypassed cathode's capacitor, and the input capacitance (Miller's) that
// the source's resistance charges, a low-pass ahead of it.
struct Triode {
    const TriodeTable *t = nullptr;
    double kc = 0, gLeak = 0; // dt/Cc, 1/Rg (0: no coupling capacitor)
    double kk = 0, gk = 0;    // dt/Ck, 1/Rk (0: no bypassed cathode)
    double vc = 0, vk = 0, w1 = 0, F1 = 0, v1 = 0;
    OnePole miller;
    void init(const TriodeTable &table, double rate2, double Cc, double Rg, double Rk, double Ck, double millerHz) {
        t = &table;
        const double dt = 1.0 / rate2;
        kc = Cc > 0 ? dt / Cc : 0;
        gLeak = Cc > 0 ? 1.0 / Rg : 0;
        kk = Ck > 0 ? dt / Ck : 0;
        gk = Ck > 0 ? 1.0 / Rk : 0;
        miller.lowpass(millerHz, rate2);
        reset();
    }
    void reset() {
        vc = 0;
        vk = -t->wq;
        w1 = t->wq;
        F1 = t->Fat(w1);
        v1 = 0;
        miller.reset();
    }
    // The input as it leaves its resting value, in volts; the plate's, likewise.
    inline float step(float in) {
        const double x = miller.step(in);
        const double w = x - vc - vk;
        const double d = w - w1;
        double F, v, ik, ig;
        t->look(w, F, v, ik, ig);
        // First-order antiderivative antialiasing (Parker, Zavalishin & Le
        // Bivic 2016); where the step is too small for it, the midpoint
        // (the plate's own value there, near enough).
        const double y = std::fabs(d) > 1e-4 ? (F - F1) / d : 0.5 * (v + v1);
        w1 = w;
        F1 = F;
        v1 = v;
        if (kc > 0)
            vc += kc * ((x - vc) * gLeak + ig);
        if (kk > 0)
            vk += kk * (ik - vk * gk);
        return float(y);
    }
};

// --- the amp -----------------------------------------------------------------------------------------------------
//
// The whole amp, at `over` times the page's rate inside (four as played;
// the tests run it at sixteen to see what four leaves): one sample in from
// the guitar's cable, one out to the speaker (volts across it).
struct Amp {
    float rate = 48000;
    int over = 4;
    Up2<12> up;
    Down2<12> down;
    Up2<5> upMore[3];
    Down2<5> downMore[3];
    Triode first, cold, third;
    OnePole into;       // the first stage's 470k ∥ 470 pF into the 1M volume pot
    OnePole bright;     // the volume pot's 1 nF bright cap
    OnePole divider;    // the cold stage's 470k ∥ 470 pF into 470k to ground and the third's input capacitance
    ToneStack stack;
    OnePole presence;   // the presence control's lift in the power amp's feedback
    Adaa follower, inverter, knee;
    // The phase inverter's two sides into the EL34s' grids: coupling caps
    // (22 nF, 220k bias feed) charged by the grid leak and by grid current.
    double pc[2] = {0, 0};
    double side1[2] = {0, 0};
    double sideF[2][2] = {}; // each side's antiderivative where it was, on this row and the next
    int sideRow = -1;
    double kpc = 0, gpl = 0;
    // The supply: the reservoir's voltage, sagging under the valves' current.
    double B = 460, B0 = 488, Rsup = 90, kB = 0, vmax = 360, ivmax = 1.0 / 360;
    double gsup = 1.0 / 90;
    int row = 0;
    double rowT = 0;
    OnePole otLow;   // the output transformer's primary inductance
    Biquad otHigh;   // its leakage inductance and capacitance
    float volume = 0.75f;  // the preamp volume (the drive): up, as a lead player has it, the master down
    float master = 0.35f;  // the master volume
    double masterShare = 0.042; // what it passes: a 1M audio-taper pot, (10^(2x) − 1)/99
    int treble = 0;        // tone steps

    void init(float r, int oversample = 4) {
        rate = r;
        over = oversample <= 2 ? 2 : oversample <= 4 ? 4 : oversample <= 8 ? 8 : 16;
        const double r2 = over * double(r);
        const Tables &T = tables();
        // Each grid's input capacitance, Cgk + (1 + A)·Cgp of a 12AX7 (1.6
        // and 1.7 pF; A the stage's gain: about 60, 8 and 37), against what
        // drives it: 68k and the pickup; the volume pot's wiper; the cold
        // stage's plate through the 470 pF.
        first.init(T.first, r2, 0, 1e6, 2.7e3, 0.68e-6, 22000);
        cold.init(T.cold, r2, 22e-9, 1e6, 0, 0, 30000);
        third.init(T.third, r2, 22e-9, 470e3, 0, 0, 25000);
        // 470k ∥ 470 pF into the 1M pot: (1 + sτ)/(1 + R1/Rpot + sτ), τ = 470k·470 pF.
        into.set(1, 470e3 * 470e-12, 1 + 470e3 / 1e6, 470e3 * 470e-12, r2);
        // The bright cap passes the treble past the volume pot's upper part.
        setVolume(volume);
        setMaster(master);
        // 470k ∥ 470 pF into 470k ∥ 65 pF: (1 + sτ1)/(2 + s(τ1 + τm)), down 6 dB under 700 Hz, 1 dB over 1.3 kHz.
        divider.set(1, 470e3 * 470e-12, 2, 470e3 * (470e-12 + 65e-12), r2);
        setTone(0);
        presence.set(1, 2.2 / (2 * kPi_ * 2500), 1, 1 / (2 * kPi_ * 2500), r2); // +7 dB shelf from 2.5 kHz, the presence at noon
        kpc = (1.0 / r2) / 22e-9;
        gpl = 1.0 / 220e3;
        // A solid-state bridge into 100 µF: 488 V unloaded, its source
        // resistance (the transformer's windings and the choke) about 90 Ω.
        B0 = 488;
        Rsup = 90;
        gsup = 1.0 / Rsup;
        kB = (1.0 / r2) / 100e-6;
        otLow.highpass(32, r2);
        otHigh.lowpass(19000, 0.7, r2);
        reset();
    }
    void setVolume(float v) {
        volume = std::clamp(v, 0.0f, 1.0f);
        // A 1M audio-taper pot: the share passed is (10^(2v) − 1)/99. What
        // lies above the wiper is bypassed by 1 nF: a shelf up to the top.
        const double a = std::max(0.002, (std::pow(10.0, 2.0 * volume) - 1.0) / 99.0), Rup = (1 - a) * 1e6;
        // (Rlow·(1 + sτ))/(Rlow·(1 + sτ) + Rup) = a(1 + sτ)/(1 + a·sτ), τ = Rup·C.
        bright.set(a, a * Rup * 1e-9, 1, a * Rup * 1e-9, over * double(rate));
    }
    void setMaster(float m) {
        master = std::clamp(m, 0.0f, 1.0f);
        masterShare = (std::pow(10.0, 2.0 * master) - 1.0) / 99.0;
    }
    void setTone(int step) {
        treble = step;
        // Treble from noon (and the − = keys), middle and bass as a lead player sets them.
        stack.set(std::clamp(0.6 + 0.1 * step, 0.05, 1.0), 0.55, 0.45, over * double(rate));
    }
    void reset() {
        const Tables &T = tables();
        up.reset();
        down.reset();
        for (auto &u : upMore)
            u.reset();
        for (auto &d : downMore)
            d.reset();
        first.reset();
        cold.reset();
        third.reset();
        into.reset();
        bright.reset();
        divider.reset();
        stack.reset();
        presence.reset();
        otLow.reset();
        otHigh.reset();
        follower.reset(T.follower, 0);
        inverter.reset(T.inverter, 0);
        knee.reset(T.knee, 0);
        pc[0] = pc[1] = 0;
        side1[0] = side1[1] = 0;
        sideRow = -1;
        // The supply settled under the idle current (and the preamp's, a little).
        B = B0;
        for (int i = 0; i < 6; ++i) {
            setRow();
            B = B0 - Rsup * (1.15 * 2 * idleI() + 0.008);
        }
        setRow();
    }
    void setRow() {
        const double x = std::clamp((B - kSupplyLo) / kSupplyStep, 0.0, kSupplyRows - 1.001);
        row = int(x);
        rowT = x - row;
    }
    double idleI() const {
        const Tables &T = tables();
        return (1 - rowT) * T.el34[row].at(0) + rowT * T.el34[row + 1].at(0);
    }
    // The tone stack's output, in volts, through the power amp: the speaker's.
    inline float power(double x) {
        const Tables &T = tables();
        // The phase inverter: a long-tailed pair, its two outputs swinging
        // ±, saturating near 70 V each way.
        const double g = 18.0 * masterShare * presence.step(float(x));
        const double drive = 70.0 * inverter.step(T.inverter, g / 70.0, 1e-6);
        // Each side through its coupling capacitor to an EL34's grid, which
        // conducts once driven past its cathode, charging the capacitor: the
        // bias shifts down after loud passages (blocking). The valve's
        // current at this supply, between the table's rows.
        const Curve &c0 = T.el34[row], &c1 = T.el34[row + 1];
        if (sideRow != row) {
            // (On a new row, where each side was is looked up again.)
            for (int side = 0; side < 2; ++side) {
                sideF[side][0] = c0.Fat(side1[side]);
                sideF[side][1] = c1.Fat(side1[side]);
            }
            sideRow = row;
        }
        double I[2];
        for (int side = 0; side < 2; ++side) {
            const double a = (side ? -drive : drive) - pc[side], a1 = side1[side], d = a - a1;
            const double f0 = c0.Fat(a), f1 = c1.Fat(a);
            double i0, i1;
            if (std::fabs(d) > 1e-4) {
                const double id = 1.0 / d;
                i0 = (f0 - sideF[side][0]) * id;
                i1 = (f1 - sideF[side][1]) * id;
            } else {
                i0 = c0.at(0.5 * (a + a1));
                i1 = c1.at(0.5 * (a + a1));
            }
            sideF[side][0] = f0;
            sideF[side][1] = f1;
            side1[side] = a;
            I[side] = i0 + (i1 - i0) * rowT;
            pc[side] += kpc * (a * gpl + T.el34Grid.at(a));
        }
        // The plates: each half of the primary sees a quarter of the
        // plate-to-plate load (3.2 kΩ); a plate can't swing below its
        // knee, about 100 V at these currents, so the swing saturates
        // there (smoothly).
        const double vs = vmax * knee.step(T.knee, (I[0] - I[1]) * 800.0 * ivmax, 1e-6);
        // The supply sags under what the valves draw, screens included,
        // and the reservoir recharges through the rectifier.
        const double draw = 1.15 * (I[0] + I[1]) + 0.008;
        B += kB * ((B0 - B) * gsup - draw);
        B = std::clamp(B, kSupplyLo, B0);
        // 3.2 kΩ to 16 Ω: a turns ratio of 14.1 for the whole primary.
        return otHigh.step(otLow.step(float(2.0 * vs / 14.14)));
    }
    inline float stage(float x) {
        const Tables &T = tables();
        float y = first.step(x);
        y = bright.step(into.step(y));
        y = cold.step(y);
        y = third.step(divider.step(y));
        // The cathode follower (direct-coupled from the third stage's plate)
        // follows until the swing up drives its grid past its cathode and it
        // draws grid current through that plate's load: the top squashes
        // softly.
        y = float(follower.step(T.follower, y, 1e-4));
        y = stack.step(y);
        return power(y);
    }
    inline float process(float x) {
        setRow();
        vmax = std::max(B - 100.0, 50.0);
        ivmax = 1.0 / vmax;
        float a[16], b[16];
        up.step(x, a[0], a[1]);
        int n = 2, levels = 0;
        for (; n < over; n *= 2, ++levels) {
            for (int i = 0; i < n; ++i)
                upMore[levels].step(a[i], b[2 * i], b[2 * i + 1]);
            std::copy(b, b + 2 * n, a);
        }
        for (int i = 0; i < n; ++i)
            a[i] = stage(a[i]);
        for (; levels > 0; --levels, n /= 2)
            for (int i = 0; i < n / 2; ++i)
                a[i] = downMore[levels - 1].step(a[2 * i], a[2 * i + 1]);
        return down.step(a[0], a[1]);
    }
};

// --- the cab -----------------------------------------------------------------------------------------------------
//
// A Vintage-30-type 12" in a closed-back 4x12, close-mic'd off the dust
// cap's edge with a dynamic mic, as its published response draws it:
// the speaker's resonance in the box, its impedance peak raised by the
// valve amp's high output impedance, the cone's lower-mid dip, its
// break-up between 1 and 5 kHz (peaks and the notches between them), and
// the cone's fall past 5 kHz, steep: some 15 dB under 1 kHz at 7 kHz, 25 at
// 9, 35 at 12.
struct Cab {
    static constexpr int kN = 12;
    Biquad f[kN];
    void init(float rate) {
        f[0].highpass(70, 0.9, rate);
        f[1].peak(110, 1.5, 3.5, rate);
        f[2].peak(450, 0.9, -3.0, rate);
        f[3].peak(1200, 2.0, 1.5, rate);
        f[4].peak(1900, 4.0, -2.5, rate);
        f[5].peak(2600, 2.5, 5.0, rate);
        f[6].peak(3400, 4.0, 3.0, rate);
        f[7].peak(4200, 4.0, -6.0, rate);
        f[8].peak(4900, 5.0, 2.0, rate);
        f[9].lowpass(5000, 0.55, rate);
        f[10].lowpass(5200, 1.2, rate);
        f[11].lowpass(7500, 0.7, rate);
    }
    void reset() {
        for (auto &b : f)
            b.reset();
    }
    inline float step(float x) {
        for (auto &b : f)
            x = b.step(x);
        return x;
    }
};

// --- the string --------------------------------------------------------------------------------------------------

// One polarisation's loop: the wave leaving the bridge, u, round the string
// and back. Displacement in millimetres.
struct Loop {
    std::vector<float> buf;
    uint32_t mask = 0, w = 0;
    float loss = 0;          // the loss filter's state
    float g = 0.99f, a = 0;  // its gain and pole
    static constexpr int kMaxAp = 16;
    float ap[kMaxAp] = {}; // the dispersion allpasses' states
    float apA = 0;
    int apM = 4;
    void init(size_t size) {
        size_t n = 1;
        while (n < size + 8)
            n <<= 1;
        buf.assign(n, 0.0f);
        mask = uint32_t(n - 1);
        clear();
    }
    void clear() {
        std::fill(buf.begin(), buf.end(), 0.0f);
        w = 0;
        loss = 0;
        std::fill(std::begin(ap), std::end(ap), 0.0f);
    }
    inline float tap(uint32_t d) const { return buf[(w - d) & mask]; }
    // Cubic Hermite, as dsp.h's Delay reads (d >= 1).
    inline float read(float d) const {
        d = std::max(d, 1.0f);
        const float fl = std::floor(d), t = d - fl;
        const uint32_t i = uint32_t(fl);
        const float y1 = tap(i), y2 = tap(i + 1), y3 = tap(i + 2);
        const float y0 = i > 1 ? tap(i - 1) : y1; // (the newest has no newer one)
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
    inline void push(float x) {
        buf[w] = x;
        w = (w + 1) & mask;
    }
    // The bridge: what came back, its losses and its stiffness.
    inline float bridge(float y) {
        loss = g * (1 + a) * y - a * loss;
        float x = loss;
        for (int k = 0; k < apM; ++k) {
            // (a + z⁻¹)/(1 + a·z⁻¹), transposed: one state each.
            const float o = apA * x + ap[k];
            ap[k] = x - apA * o;
            x = o;
        }
        return x;
    }
};

// Phase delay (samples) of M first-order allpasses with coefficient a at ω.
inline double allpassDelay(double a, int M, double w) {
    return M * (w - 2.0 * std::atan2(a * std::sin(w), 1.0 + a * std::cos(w))) / w;
}
// The coefficient that bends M allpasses' phase delay most nearly to a
// stiff string's, τ(ω) ≈ τ0 − κ·ω² (κ = B·P³/8π², P the period in
// samples), over the partials that matter: up to the twentieth (or 4.6 kHz
// at 48 kHz, whichever is lower), each weighted as 1/ω, since a low
// partial's few cents are heard more than a high one's. Least squares,
// found at start-up for each κ, M and top of the band, and looked up after.
struct Dispersion {
    static constexpr int kKappas = 161; // log10 κ from −4 to 4 in 0.05
    static constexpr int kMs = 3;       // M = 4, 8, 16
    static constexpr int kTops = 6;     // the band's top ω: 0.6·1.5^−k
    float a[kMs][kTops][kKappas];
    Dispersion() {
        constexpr int kW = 24;
        for (int mi = 0; mi < kMs; ++mi) {
            const int M = 4 << mi;
            for (int top = 0; top < kTops; ++top) {
                const double wmax = 0.6 * std::pow(1.5, -top);
                for (int k = 0; k < kKappas; ++k) {
                    const double kappa = std::pow(10.0, -4.0 + 0.05 * k);
                    auto cost = [&](double aa) {
                        double r[kW], wt[kW], mean = 0, sw = 0;
                        for (int i = 0; i < kW; ++i) {
                            const double w = wmax * (i + 0.5) / kW;
                            r[i] = allpassDelay(aa, M, w) + kappa * w * w;
                            wt[i] = 1.0 / (i + 0.5);
                            mean += wt[i] * r[i];
                            sw += wt[i];
                        }
                        mean /= sw;
                        double c = 0;
                        for (int i = 0; i < kW; ++i)
                            c += wt[i] * (r[i] - mean) * (r[i] - mean);
                        return c;
                    };
                    double lo = -0.95, hi = 0.0;
                    double m1 = lo + (hi - lo) * 0.381966, m2 = lo + (hi - lo) * 0.618034, c1 = cost(m1), c2 = cost(m2);
                    for (int it = 0; it < 40; ++it) {
                        if (c1 < c2) {
                            hi = m2, m2 = m1, c2 = c1;
                            m1 = lo + (hi - lo) * 0.381966, c1 = cost(m1);
                        } else {
                            lo = m1, m1 = m2, c1 = c2;
                            m2 = lo + (hi - lo) * 0.618034, c2 = cost(m2);
                        }
                    }
                    a[mi][top][k] = float(0.5 * (lo + hi));
                }
            }
        }
    }
    // For a string's κ, M's set, and the band's top (ω of its twentieth partial).
    float coef(double kappa, int mi, double wmax) const {
        const double x = std::clamp((std::log10(std::max(kappa, 1e-6)) + 4.0) / 0.05, 0.0, kKappas - 1.001);
        const double y = std::clamp(std::log(0.6 / std::clamp(wmax, 0.01, 0.6)) / std::log(1.5), 0.0, kTops - 1.001);
        const int i = int(x), j = int(y);
        const double u = x - i, v = y - j;
        const float *r0 = a[mi][j], *r1 = a[mi][j + 1];
        const double c0 = r0[i] + (r0[i + 1] - r0[i]) * u, c1 = r1[i] + (r1[i + 1] - r1[i]) * u;
        return float(c0 + (c1 - c0) * v);
    }
};
inline const Dispersion &dispersion() {
    static const Dispersion d;
    return d;
}

// --- the guitar --------------------------------------------------------------------------------------------------

// The rig: the amp and the cab a guitar is played through, and the air and
// the body that carry the cab's sound back to the strings. One per player:
// every note a line plays goes through the same one, so a note's tail and the
// next note's attack meet in the same valves, and the supply's sag and the
// grids' charge carry from note to note.
class Rig {
public:
    void init(float rate) {
        m_rate = rate;
        m_amp.init(rate);
        m_cab.init(rate);
        m_fb.assign(kFbSize, 0.0f);
        reset();
    }
    void reset() {
        m_amp.reset();
        m_cab.reset();
        std::fill(m_fb.begin(), m_fb.end(), 0.0f);
        m_fbW = 0;
        m_out = 0;
        m_quiet = 0;
        m_live = false;
    }
    void setTone(int step) {
        if (step != m_amp.treble)
            m_amp.setTone(step);
    }
    // The mic's sound d samples before sample i of the block about to be
    // played (d > i: it has been heard already).
    inline float heard(int i, int d) const { return m_fb[(m_fbW + uint32_t(i) - uint32_t(std::max(d, i + 1))) & kFbMask]; }
    // A block of the pickups' voltage in; the mic's (a fortieth of the
    // speaker's volts) and the speaker's out (either may be null). Asleep,
    // with nothing coming in, it costs nothing.
    void process(const float *in, float *mic, float *spk, int n, bool fed) {
        if (fed) {
            m_live = true;
            m_quiet = 0;
        }
        if (!m_live) {
            for (int i = 0; i < n; ++i) {
                if (mic)
                    mic[i] = 0;
                if (spk)
                    spk[i] = 0;
            }
            return;
        }
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const float v = m_amp.process(in ? in[i] : 0.0f);
            const float m = m_cab.step(v);
            m_fb[m_fbW] = m;
            m_fbW = (m_fbW + 1) & kFbMask;
            if (mic)
                mic[i] = m * (1.0f / 40.0f);
            if (spk)
                spk[i] = v;
            peak = std::max(peak, std::fabs(v));
        }
        m_out = std::max(peak, m_out * std::exp(-float(n) / (0.01f * m_rate)));
        // Asleep once nothing comes in and the speaker has gone quiet for
        // a few blocks (−80 dB under full power).
        if (!fed && m_out < 4e-3f) {
            if (++m_quiet > 16)
                m_live = false;
        } else if (!fed || m_out >= 4e-3f) {
            m_quiet = fed ? 0 : m_quiet;
        }
    }
    bool live() const { return m_live; }
    float level() const { return m_out; } // the speaker's recent peak, volts
    const Amp &amp() const { return m_amp; }
    Amp &amp() { return m_amp; }

private:
    static constexpr uint32_t kFbSize = 1024, kFbMask = kFbSize - 1;
    float m_rate = 48000;
    Amp m_amp;
    Cab m_cab;
    std::vector<float> m_fb;
    uint32_t m_fbW = 0;
    float m_out = 0;
    int m_quiet = 0;
    bool m_live = false;
};

// The humbucker's flux against the string's distance from its pole (mm):
// Novak et al.'s fit to a measured Seymour Duncan SH-2N (DAFx-18),
// Φ(x) = (x + Leq)/∛(req² + (x + Leq)²) − x/∛(req² + x²), Leq = 9.86 mm,
// req = 1.34 mm; scaled so it falls by one unit per millimetre where the
// string rests, and tabulated against the distance squared (so the two
// polarisations' offsets add without a square root).
constexpr float kPoleGapMax = 12.0f; // mm: the table's reach
inline const std::vector<float> &fluxTable() {
    static const std::vector<float> t = [] {
        auto phi = [](double x) {
            const double L = 9.86, r2 = 1.34 * 1.34;
            return (x + L) / std::cbrt(r2 + (x + L) * (x + L)) - x / std::cbrt(r2 + x * x);
        };
        const double h = 1e-4, slope = (phi(kPoleGap + h) - phi(kPoleGap - h)) / (2 * h), at = phi(kPoleGap);
        std::vector<float> v(4097);
        for (size_t i = 0; i < v.size(); ++i) {
            const double r2 = double(kPoleGapMax) * kPoleGapMax * double(i) / double(v.size() - 1);
            v[i] = float((phi(std::sqrt(r2)) - at) / -slope);
        }
        return v;
    }();
    return t;
}

// Everything built once, before the audio thread runs.
inline void prepare() {
    (void)tables(), (void)halfband<12>(), (void)halfband<5>(), (void)dispersion(), (void)fluxTable();
}

class Guitar {
public:
    // What render writes: the mic'd cab (the sound), or for the tests the
    // pickup's voltage or the speaker's.
    enum Tap { Sound = 0, Pickup, Speaker };

    void init(float rate) {
        m_rate = rate;
        for (auto &l : m_loop)
            l.init(size_t(rate / 22.0f) + 64);
        for (auto &e : m_exc)
            e.assign(m_loop[0].buf.size(), 0.0f);
        for (auto &l : m_inj)
            l.init(256);
        m_rig.init(rate);
        // The pickup's resonance: L, R and C against the load, by the
        // bilinear transform warped to its peak.
        const double a2 = kCoilL * (kCoilC + kCableC), a1 = kCoilL / kLoadR + kCoilR * (kCoilC + kCableC),
                     a0 = 1 + kCoilR / kLoadR;
        const double wr = 1.0 / std::sqrt(a2), K = wr / std::tan(wr / (2.0 * rate));
        m_lcr.set(1, 2, 1, a2 * K * K + a1 * K + a0, 2 * (a0 - a2 * K * K), a2 * K * K - a1 * K + a0);
        // The way back: the cab's sound pushes on the guitar's body and
        // neck, whose low modes (about 180 and 420 Hz, **inf**) carry it to
        // the string as a force; little over a kilohertz gets through.
        m_fbLp.lowpass(600, 0.6, rate);
        m_fbLp2.lowpass(600, 0.9, rate);
        m_body[0].peak(180, 3.0, 12.0, rate);
        m_body[1].peak(420, 3.0, 9.0, rate);
        m_fbLp3.lowpass(400, 0.7, rate);
        m_fbInt = float(2 * kPi_ * 200.0 / rate);
        m_scrapeFall = std::exp(-1.0f / (0.0015f * rate));
        m_fbLeak = float(2 * kPi_ * 25.0 / rate);
        silence();
    }

    // The string at rest (and, played alone, its rig too).
    void silence() {
        for (auto &l : m_loop)
            l.clear();
        for (auto &e : m_exc)
            std::fill(e.begin(), e.end(), 0.0f);
        m_excLeft = m_excLen = 0;
        m_lcr.reset();
        m_rig.reset();
        m_fbLp.reset();
        m_fbLp2.reset();
        m_fbLp3.reset();
        for (auto &l : m_inj)
            l.clear();
        for (auto &b : m_body)
            b.reset();
        m_fbI = 0;
        m_fbNow = 0;
        m_sway = 0;
        m_pick1.reset();
        m_pick2.reset();
        m_phi1 = -1e9f;
        m_off = m_offSlew = -1;
        m_energy = 0;
        m_uh1 = m_uv1 = 0;
        m_keep = 1;
        m_scrape = 0;
        m_scrapeF.reset();
        m_mute = 0;
        m_muteNow = 0;
        m_damp = 0;
        m_dampNow = 0;
        m_out = 0;
        m_quiet = 0;
        m_live = false;
        m_D[0] = m_D[1] = -1;
    }

    // A new note: which string it is on, and the string's length for it.
    void start(float midi) {
        m_string = stringFor(midi);
        m_hz = midiToHz(midi);
        m_bend = 0;
        m_fret = midi - stringSpec(m_string).midi;
        m_damp = m_dampNow = 0;
        m_live = true;
        m_quiet = 0;
        const double kappa = kappaFor(midiToHz(midi));
        m_apSet = kappa < 5 ? 0 : kappa < 30 ? 1 : 2;
        for (auto &l : m_loop)
            l.apM = 4 << m_apSet;
    }

    // The pick: how hard (0..1), and where along the string (0 over the neck
    // pickup, 1 a few centimetres from the bridge).
    void pluck(float vel, float where) {
        vel = clampf(vel, 0.0f, 1.0f);
        where = clampf(where, 0.0f, 1.0f);
        // From 16 cm off the saddle (over the neck pickup) to 4 cm.
        pickAt(vel, 0.16f - 0.12f * where);
    }
    // The pick `fromBridge` metres off the saddle.
    void pickAt(float vel, float fromBridge) {
        vel = clampf(vel, 0.0f, 1.0f);
        // The pick pushes with a force (a light stroke a quarter of a
        // newton, a hard one two), and the string gives as a string held at
        // both ends does: δ = F·x(L − x)/(T·L), so a stroke near the bridge
        // moves it less (and brighter).
        const double L = kScale * std::exp2(-std::max(0.0f, m_fret) / 12.0), x = std::clamp(double(fromBridge), 0.002, 0.9 * L);
        const float F = 0.25f + 1.75f * vel;
        const float A = float(1e3 * F * x * (L - x) / (stringSpec(m_string).tension * tensionRatio() * L)); // mm
        const float angle = (14.0f + 26.0f * vel) * float(kPi_ / 180.0); // a harder stroke digs toward the body
        // (A palm on the strings dulls the pick's release too.)
        excite(fromBridge, A, angle, (3000.0f + 9000.0f * vel * vel) * (1.0f - 0.6f * m_mute), 0.85f);
        m_scrape = kScrape * vel * vel * (1.0f - 0.8f * m_mute);
        m_scrapeF.set(3500.0f, 0.8f, m_rate);
    }
    // A hammer-on: the finger lands on the string at its new fret, which is
    // now its end; the string keeps ringing at its new length, tapped there.
    void hammer(float vel) {
        m_damp = m_dampNow = 0;
        const float L = float(kScale * std::exp2(-std::max(0.0f, m_fret) / 12.0f));
        excite(std::max(0.85f * L, L - 0.03f), 0.12f + 0.35f * clampf(vel, 0.0f, 1.0f), 1.3f, 2500.0f, 0.0f);
    }
    // A pull-off: the finger leaving the fret plucks the string sideways.
    void pullOff(float vel) {
        m_damp = m_dampNow = 0;
        const float L = float(kScale * std::exp2(-std::max(0.0f, m_fret) / 12.0f));
        excite(std::max(0.8f * L, L - 0.04f), 0.2f + 0.6f * clampf(vel, 0.0f, 1.0f), 0.2f, 3000.0f, 0.0f);
    }
    // The fretting hand lets go: its flesh lies on the string, which dies
    // within about `seconds`.
    void damp(float seconds) { m_damp = clampf(seconds, 0.02f, 5.0f); }
    // A palm on the strings by the bridge: 0 none, 1 a full chug (at once,
    // for a note picked with it already there).
    void setMute(float palm, bool now = false) {
        m_mute = clampf(palm, 0.0f, 1.0f);
        if (now)
            m_muteNow = m_mute;
    }
    // How near the amp the player stands: how readily it feeds back.
    void setFeedback(float f) { m_fbAmt = clampf(f, 0.0f, 1.5f); }
    void setTone(int step) { m_rig.setTone(step); }
    void setTap(Tap t) { m_tap = t; }

    // The pitch (Hz), and how much of it is bend (semitones over the fret's
    // own, raised by tension; negative: a tremolo arm let down).
    void setPitch(float hz, float bendSemis) {
        hz = clampf(hz, 20.0f, 0.42f * m_rate);
        m_hz = hz;
        m_bend = clampf(bendSemis, -12.0f, 12.0f);
        const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f) - m_bend;
        m_fret = midi - stringSpec(m_string).midi;
    }

    bool active() const { return m_live; }
    // How hard the string swings: the mean square of its waves' speed (mm²/s²).
    float swing() const { return m_energy; }
    // The pickup's recent peak (V).
    float level() const { return m_out; }
    int string() const { return m_string; }
    float fret() const { return m_fret; }
    // The string's inharmonicity coefficient now, as fretted and bent.
    double B() const {
        const StringSpec &s = stringSpec(m_string);
        const double L = kScale * std::exp2(-std::max(0.0f, m_fret) / 12.0);
        return openB(s) * (kScale / L) * (kScale / L) / tensionRatio();
    }
    // Its own rig, when it is played alone.
    const Rig &rig() const { return m_rig; }
    Rig &rig() { return m_rig; }

    // Played alone, through its own rig: what the tap gives, times gain.
    void render(float *out, int n, float gain) {
        float v[64];
        for (int at = 0; at < n; at += 64) {
            const int k = std::min(64, n - at);
            std::fill(v, v + k, 0.0f);
            pickup(v, k, &m_rig);
            if (m_tap == Pickup) {
                for (int i = 0; i < k; ++i)
                    out[at + i] += v[i] * gain;
                continue;
            }
            float mic[64], spk[64];
            m_rig.process(v, mic, spk, k, m_live);
            for (int i = 0; i < k; ++i)
                out[at + i] += (m_tap == Speaker ? spk[i] : mic[i]) * gain;
        }
    }

    // The string and its pickup: adds the pickup's voltage to v. The rig it
    // is played through (if any) is what it hears back.
    void pickup(float *vout, int n, const Rig *rig) {
        if (!m_live)
            return;
        const float rate = m_rate;
        const StringSpec &s = stringSpec(m_string);
        const double L = kScale * std::exp2(-std::max(0.0f, m_fret) / 12.0);
        // Tension: a bend's, and the swing's own stretch (its mean slope²).
        const double EAoT = stretchOverT(s);
        const double c = 2.0 * L * m_hz; // wave speed, m/s
        const double swing = EAoT * m_energy / (c * c) * 1e-6; // mm²/s² → m²/s²
        const double f0 = m_hz * std::sqrt(1.0 + std::min(swing, 0.05));
        // The polarisations a hair apart: the one toward the pickup, which is
        // what is heard and so what the string is tuned by, on the note; the
        // one along the fretboard a hair flat.
        const double P[2] = {rate / (f0 / 1.0015), rate / f0};
        // Losses: the string's own, the palm, the hand.
        const float muteK = 1.0f - std::exp(-float(n) / (0.012f * rate));
        m_muteNow += (m_mute - m_muteNow) * muteK;
        m_dampNow += ((m_damp > 0 ? 1.0f : 0.0f) - m_dampNow) * (1.0f - std::exp(-float(n) / (0.008f * rate)));
        // A fretted string is shorter, and the finger behind the fret takes some of it.
        const float Lr = float(L / kScale);
        float T0 = s.t60 * std::sqrt(Lr), Th = s.t60hi * std::sqrt(Lr);
        // A palm by the bridge: a quarter-second thump, its top gone in tens of milliseconds.
        const float mu = m_muteNow;
        T0 = T0 * (1 - mu) * (1 - mu) + 0.8f * mu;
        Th = Th * (1 - mu) * (1 - mu) + 0.05f * mu;
        if (m_dampNow > 0) {
            const float h = m_damp > 0 ? m_damp : 0.15f;
            T0 = T0 + (h - T0) * m_dampNow;
            Th = Th + (0.3f * h - Th) * m_dampNow;
        }
        Th = std::min(Th, T0);
        const double kap = kappaFor(f0);
        const float apA = dispersion().coef(kap, m_apSet, std::min(20.0 * 2 * kPi_ * f0 / rate, 0.6));
        float Dnew[2];
        for (int p = 0; p < 2; ++p) {
            Loop &l = m_loop[p];
            // The vertical polarisation drives the bridge harder: it decays sooner.
            const float t0 = p ? 0.85f * T0 : T0, th = p ? 0.85f * Th : Th;
            const double w0 = 2 * kPi_ * f0 / rate;
            const double G0 = std::pow(10.0, -3.0 / (t0 * f0)), Gh = std::pow(10.0, -3.0 / (th * f0));
            const double wh = 2 * kPi_ * std::min(3000.0, 0.4 * rate) / rate;
            const double r = std::min(Gh / G0, 1.0), cw = std::cos(wh);
            double a = 0;
            if (r < 0.9999) {
                const double A = 1 - r * r, Bq = 2 * (1 - r * r * cw);
                a = (-Bq + std::sqrt(std::max(Bq * Bq - 4 * A * A, 0.0))) / (2 * A);
            }
            a = std::clamp(a, -0.95, 0.0);
            const double mag0 = (1 + a) / std::sqrt(1 + 2 * a * std::cos(w0) + a * a);
            l.a = float(a);
            l.g = float(std::min(G0 / mag0, 0.99995));
            l.apA = apA;
            const double tauLoss = std::atan2(-a * std::sin(w0), 1 + a * std::cos(w0)) / w0;
            const double tauAp = allpassDelay(apA, l.apM, w0);
            Dnew[p] = float(std::clamp(P[p] - tauLoss - tauAp, 2.0, double(l.mask) - 8.0));
            if (m_D[p] < 0)
                m_D[p] = Dnew[p];
        }
        // The bridge passes energy between the polarisations: a rotation by
        // a small angle each round trip, about 1.2 radians a second.
        const float th = clampf(float(1.2 / f0), 0.0f, 0.05f), cr = std::cos(th), sr = std::sin(th);
        // Where the coils sit, in the loop's samples: the wave reaches a
        // coil d from the saddle (d/L)·(P/2) after leaving it, whatever of
        // the round trip is lumped at the saddle.
        const float Pm = float(P[1]);
        const float G1 = clampf(Pm * (kPickupD - 0.5f * kCoilGap) / float(L), 1.0f, Pm - 1.0f);
        const float G2 = clampf(Pm * (kPickupD + 0.5f * kCoilGap) / float(L), 1.0f, Pm - 1.0f);
        // A bend pushes the string sideways at the fret; over the pickup
        // it stands that far times d/L off the poles. (Once the hand has let
        // go, so has the bend.)
        float off = kPoleOff;
        if (m_bend > 0 && m_damp <= 0) {
            const double dT = s.tension * (std::exp2(m_bend / 6.0) - 1.0);
            const double a = std::max(kScale - L, 0.01), b = L;
            const double dL = dT * (kScale + 0.05) / (s.tension * EAoT);
            const double delta = std::sqrt(2 * dL / (1 / a + 1 / b)); // m at the fret
            off += float(1e3 * delta * kPickupD / L);
        }
        // (Eased over 15 ms: the string comes back over the poles as the
        // finger lets the bend go, not in a step, which would ring the coil.)
        if (m_offSlew < 0)
            m_offSlew = off;
        m_offSlew += (off - m_offSlew) * (1.0f - std::exp(-float(n) / (0.015f * rate)));
        off = m_offSlew;
        if (m_off < 0)
            m_off = off;
        // Feedback: the cab's sound, through the air (a metre and a half
        // to the amp: 4.4 ms) and the guitar's body, back into the string
        // as a force (so integrated into its displacement). The palm and
        // the fretting hand on the strings stop it.
        // (Leaning in, the player comes from 3 m to 0.6 m from the cab, 8.7
        // ms to 1.7 ms of air, and sways there, a third of a metre either
        // way every five seconds or so: the loop's phase drifts through
        // where the note's is right, as a guitarist finds the spot.)
        m_sway += float(n) / rate * 0.2f;
        m_sway -= std::floor(m_sway);
        const float lean = std::min(m_fbNow, 1.0f);
        const float metres = 3.0f - 2.4f * lean + lean * 0.35f * (1.0f - std::cos(float(2 * kPi_) * m_sway));
        const int fbD = int(metres / 343.0f * rate);
        const float quiet = (1 - mu) * (1 - mu) * (1 - m_dampNow);
        // (The player moves toward the amp over most of a second.)
        m_fbNow += (m_fbAmt - m_fbNow) * (1.0f - std::exp(-float(n) / (0.8f * rate)));
        const float fbK = rig ? kFeedback * m_fbNow * m_fbNow * quiet : 0.0f;
        const float eK = 1.0f - std::exp(-1.0f / (0.02f * rate));
        const float emf = kEmf * rate;
        const float inv = 1.0f / float(n);
        const std::vector<float> &flux = fluxTable();
        const float fluxScale = float(flux.size() - 1) / (kPoleGapMax * kPoleGapMax);
        float peak = 0;
        for (int i = 0; i < n; ++i) {
            const float t = (float(i) + 1.0f) * inv;
            float fb = 0;
            if (fbK > 0 || m_fbI != 0) {
                const float x = m_fbLp2.step(m_fbLp.step(rig ? rig->heard(i, fbD) : 0.0f));
                m_fbI += m_fbInt * m_fbLp3.step(m_body[1].step(m_body[0].step(x))) - m_fbLeak * m_fbI;
                fb = fbK * m_fbI;
            }
            float h = m_loop[0].bridge(m_loop[0].read(m_D[0] + (Dnew[0] - m_D[0]) * t));
            float v = m_loop[1].bridge(m_loop[1].read(m_D[1] + (Dnew[1] - m_D[1]) * t));
            if (m_excLeft > 0) {
                // The pick catches the string over a millisecond and a half.
                const float k = 1.0f - (1.0f - m_keep) * std::min(1.0f, float(m_excLen - m_excLeft) * m_catch);
                h *= k;
                v *= k;
            }
            // The plectrum's edge scraping the string as it leaves it: a
            // few milliseconds of noise in the string's motion, 2–6 kHz.
            if (m_scrape > 1e-5f) {
                const float z = m_scrapeF.step(m_rng.bipolar()).band * m_scrape;
                m_scrape *= m_scrapeFall;
                h += 0.5f * z;
                v += z;
            }
            float uh = cr * h + sr * v, uv = cr * v - sr * h;
            if (m_excLeft > 0) {
                const int k = m_excLen - m_excLeft;
                uh += m_pick1.step(m_exc[0][size_t(k)]);
                uv += m_pick2.step(m_exc[1][size_t(k)]);
                --m_excLeft;
            }
            uh = clampf(uh + 0.35f * fb, -20.0f, 20.0f);
            uv = clampf(uv + fb, -20.0f, 20.0f);
            m_loop[0].push(uh);
            m_loop[1].push(uv);
            m_inj[0].push(0.35f * fb);
            m_inj[1].push(fb);
            const float dh = uh - m_uh1, dv = uv - m_uv1;
            m_uh1 = uh;
            m_uv1 = uv;
            m_energy += ((dh * dh + dv * dv) * rate * rate - m_energy) * eK;
            // The pickup: each coil's flux from the string's distance to its
            // pole, down over the gap and sideways (the measured curve).
            const float offNow = m_off + (off - m_off) * t;
            // A coil d from the saddle sees u(t − G/2) − u(t + G/2) (G the
            // round trip's share for 2d); both coils are read at one
            // instant, the far coil's half-trip ago.
            // (The second term stands for the wave on its way back to the
            // saddle, by its reflection there; what the feedback pushed in
            // at the saddle since is no part of it, and is taken out, so the
            // pickup hears the push only once it has gone round the string.)
            float phi = 0;
            const bool pushed = fbK > 0 || m_fbI != 0;
            for (const float G : {G1, G2}) {
                const float a = 1.0f + 0.5f * (G2 + G), b = 1.0f + 0.5f * (G2 - G);
                const float back1 = m_loop[1].read(b) - (pushed ? m_inj[1].read(b) : 0.0f);
                const float back0 = m_loop[0].read(b) - (pushed ? m_inj[0].read(b) : 0.0f);
                const float yv = m_loop[1].read(a) - back1;
                const float yh = kLateral * (m_loop[0].read(a) - back0 + offNow);
                const float down = kPoleGap - yv;
                const float r2 = std::max(down * down + yh * yh, 0.0f) * fluxScale;
                const float x = std::min(r2, float(flux.size() - 2));
                const int j = int(x);
                phi += flux[size_t(j)] + (flux[size_t(j + 1)] - flux[size_t(j)]) * (x - float(j));
            }
            if (m_phi1 < -1e8f)
                m_phi1 = phi;
            const float e = (phi - m_phi1) * emf;
            m_phi1 = phi;
            const float vpu = m_lcr.step(e);
            vout[i] += vpu;
            peak = std::max(peak, std::fabs(vpu));
        }
        m_D[0] = Dnew[0];
        m_D[1] = Dnew[1];
        m_off = off;
        // Asleep once the string has stopped.
        m_out = std::max(peak, m_out * std::exp(-float(n) / (0.01f * rate)));
        if (m_energy < 1e-2f && m_out < 1e-6f && m_excLeft == 0) {
            if (++m_quiet > 8)
                m_live = false;
        } else {
            m_quiet = 0;
        }
    }

private:
    // The pickup's sideways sensitivity against its vertical one (the
    // field's breadth over a humbucker's row of poles, **inf**): the
    // horizontal swing is heard some 15 dB under the vertical.
    static constexpr float kLateral = 0.8f;
    // Feedback's strength: millimetres of string per volt-second of the
    // cab's sound through the body, at the nearest.
    static constexpr float kFeedback = 2.4e-4f;
    // The pick's scrape at a full stroke, mm of string (**inf**).
    static constexpr float kScrape = 0.25f;

    double tensionRatio() const { return std::exp2(m_bend / 6.0); }
    // κ = B·P³/8π²: how far the string's stiffness bends its phase delay.
    double kappaFor(double hz) const {
        const double P = m_rate / std::max(hz, 20.0);
        return B() * P * P * P / (8 * kPi_ * kPi_);
    }

    // The string left in a triangle with its apex `fromBridge` (m) off the
    // saddle, A mm aside at an angle to the fretboard; fed into each loop over
    // one round trip, through the plectrum's release (a two-pole low-pass at
    // `edgeHz`). `stop`: how much of the string's swing the pick takes first.
    void excite(float fromBridge, float A, float angle, float edgeHz, float stop) {
        const double L = kScale * std::exp2(-std::max(0.0f, m_fret) / 12.0);
        const int N = std::clamp(int(std::lround(m_rate / std::max(m_hz, 20.0f))), 8, int(m_exc[0].size()) - 1);
        const double beta = std::clamp(double(fromBridge) / L, 0.02, 0.98);
        const float sv = std::sin(angle), sh = std::cos(angle);
        // Whatever of the last excitation hasn't gone in yet goes in with this one.
        const int half = N / 2;
        for (int i = 0; i < N; ++i) {
            // The wave leaving the bridge: the left-going half of the shape,
            // inverted, then the right-going half after the nut, mirrored.
            const double x = i < half ? double(i) / half : 1.0 - double(i - half) / (N - half);
            const double y = x < beta ? x / beta : (1 - x) / (1 - beta);
            const float u = float((i < half ? -0.5 : 0.5) * A * y);
            // (Read ahead of where it writes, so the rest of the last one is intact.)
            const int from = m_excLen - m_excLeft + i;
            const bool rest = m_excLeft > 0 && from < m_excLen;
            const float prevH = rest ? m_exc[0][size_t(from)] : 0.0f, prevV = rest ? m_exc[1][size_t(from)] : 0.0f;
            m_exc[0][size_t(i)] = u * sh + prevH;
            m_exc[1][size_t(i)] = u * sv + prevV;
        }
        // (A longer rest of the last one is cut at this one's end: the pick has caught the string.)
        m_excLen = m_excLeft = N;
        m_keep = 1.0f - stop;
        m_catch = 1.0f / (0.0015f * m_rate);
        m_pick1.lowpass(edgeHz, 0.6, m_rate);
        m_pick2.lowpass(edgeHz, 0.6, m_rate);
        m_live = true;
        m_quiet = 0;
    }

    float m_rate = 48000;
    Loop m_loop[2]; // 0 horizontal (along the fretboard), 1 vertical (toward the pickup)
    std::vector<float> m_exc[2];
    int m_excLeft = 0, m_excLen = 0;
    float m_keep = 1, m_catch = 1;
    float m_scrape = 0, m_scrapeFall = 0.99f;
    Svf m_scrapeF;
    Rng m_rng{0x91a7u};
    Biquad m_pick1, m_pick2;
    Biquad m_lcr;
    Rig m_rig;
    Biquad m_fbLp, m_fbLp2, m_fbLp3, m_body[2];
    Loop m_inj[2]; // what the feedback pushed into each loop, sample by sample
    float m_fbI = 0, m_fbInt = 0, m_fbLeak = 0;
    float m_fbAmt = 0, m_fbNow = 0, m_sway = 0;
    int m_string = 3;
    int m_apSet = 0;
    float m_hz = 196, m_bend = 0, m_fret = 0;
    float m_D[2] = {-1, -1};
    float m_phi1 = -1e9f;
    float m_off = -1, m_offSlew = -1;
    float m_energy = 0, m_uh1 = 0, m_uv1 = 0;
    float m_mute = 0, m_muteNow = 0;
    float m_damp = 0, m_dampNow = 0;
    float m_out = 0;
    int m_quiet = 0;
    bool m_live = false;
    Tap m_tap = Sound;
};

}
