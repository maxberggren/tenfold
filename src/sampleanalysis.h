#pragma once

// What the sampler needs to know about a song before it can chop it: where
// every beat is, which beats are bar lines, and what key it is in.
//
// The beats are found in the classic way and then cleaned up for warping.
// An onset envelope (how much new sound arrives, 200 times a second) gives
// the tempo by autocorrelation and the beats by dynamic programming. Each
// beat is then moved onto the foot of the transient it sits on, measured at
// one millisecond, and the grid is regularised: a song made with a machine
// gets an exactly even grid from a line fitted through its beats; a song
// played by a drummer gets a grid that follows the tempo but is smoothed
// until neighbouring beats differ by less than half a percent, because the
// warp's playback speed follows the beat lengths and any jitter is heard.
//
// Nothing here runs on the audio thread: analyze() is called once per song
// from a worker and allocates freely.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>
#ifdef TF_SAMPLEANALYSIS_DEBUG
#include <cstdlib>
#include <string>
#endif

namespace tf::sampler {

struct Analysis {
    bool ok = false;            // false: too short or silent to find a beat in (then the fields below are a fallback grid at 120 BPM from sample 0)
    double bpm = 0;             // the tempo of the grid: 60·rate / the median beat length
    bool steady = false;        // the song keeps one tempo throughout (made with a click or a machine): the grid is then exactly regular
    // Every beat as a (fractional) sample position at `rate`, strictly increasing, covering the whole file:
    // beats.front() is within one beat after sample 0 (it may be slightly negative), beats.back() within one
    // beat of the end (it may be past it). Where the music is silent or beatless (an intro, a breakdown, the
    // fade) the grid carries on at the local tempo, in phase with the beats on either side.
    std::vector<double> beats;
    int downbeat = 0;           // 0..3: beats[downbeat + 4k] are the bar lines (4/4 assumed)
    float downbeatConfidence = 0; // 0..1
    std::vector<float> strength; // per beat, 0..1: how strong an onset lands on it
    std::vector<float> sub;      // 4 per beat (index 4·i + s): onset strength 0..1 at the beat and its three sixteenths after
    // Per beat: where the sound that lands on the beat really begins — the foot of its transient, a few
    // milliseconds early rather than late, so a chop cut there keeps its attack and has no click from the
    // sound before — within ±35 ms of beats[i]; beats[i] itself where nothing lands.
    std::vector<double> attack;
    // Per bar (bar k starts at beats[downbeat + 4k]): 0..1, how much the music changes at that bar line
    // against the bars before it (a part enters or leaves, the harmony or timbre shifts): section boundaries score high.
    std::vector<float> change;
    int keyRoot = 0;            // pitch class of the tonic, C = 0 … B = 11
    bool keyMinor = false;
    float keyConfidence = 0;    // 0..1
    double tuningCents = 0;     // how far the song's A is from 440 Hz, −50..+50
};

namespace detail {

constexpr double kPi = 3.14159265358979323846;

inline double sq(double v) { return v * v; }
inline int nextPow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

#ifdef TF_SAMPLEANALYSIS_DEBUG
// For trying a constant from the command line while tuning: TF_<name>=value.
inline double tune(const char *name, double fallback) {
    const char *v = std::getenv((std::string("TF_") + name).c_str());
    return v ? std::atof(v) : fallback;
}
// The scratch CLI prints what the stages decided; the app never defines this.
inline std::vector<std::pair<std::string, std::vector<double>>> &debugLog() {
    static std::vector<std::pair<std::string, std::vector<double>>> log;
    return log;
}
inline void dbg(const char *name, std::vector<double> v) { debugLog().emplace_back(name, std::move(v)); }
#else
inline void dbg(const char *, const std::vector<double> &) {}
inline double tune(const char *, double fallback) { return fallback; }
#endif

struct Ctx {
    const float *x;
    size_t frames;
    double rate;
    const std::function<void(float)> *progress;
    const std::atomic<bool> *cancel;
    bool cancelled() const { return cancel && cancel->load(std::memory_order_relaxed); }
    void report(double p) const {
        if (progress && *progress) (*progress)(float(p));
    }
};

// A real-input FFT of a power-of-two size, done as a complex FFT of half the
// size (even samples as the real part, odd as the imaginary) and untangled
// afterwards: half the work of the plain way, and the song needs some
// fifty thousand of these.
class Fft {
public:
    explicit Fft(int size) : n(size), m(size / 2), rev(size_t(m)), twr(size_t(std::max(1, m - 1))), twi(size_t(std::max(1, m - 1))), cr(size_t(m) + 1), ci(size_t(m) + 1), zr(size_t(m)), zi(size_t(m)) {
        int bits = 0;
        while ((1 << bits) < m) ++bits;
        for (int i = 0; i < m; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[size_t(i)] = r;
        }
        // Each stage's twiddles lie next to each other, so the inner loop
        // walks straight through memory and the compiler can vectorise it.
        for (int h = 1; h < m; h <<= 1)
            for (int j = 0; j < h; ++j) {
                const double a = -kPi * j / h;
                twr[size_t(h - 1 + j)] = float(std::cos(a));
                twi[size_t(h - 1 + j)] = float(std::sin(a));
            }
        for (int k = 0; k <= m; ++k) {
            const double a = -2.0 * kPi * k / n;
            cr[size_t(k)] = float(std::cos(a));
            ci[size_t(k)] = float(std::sin(a));
        }
    }
    // The spectrum of n real samples: bins 0..n/2 into re and im.
    void forward(const float *x, float *re, float *im) {
        for (int k = 0; k < m; ++k) {
            zr[size_t(rev[size_t(k)])] = x[2 * k];
            zi[size_t(rev[size_t(k)])] = x[2 * k + 1];
        }
        for (int h = 1; h < m; h <<= 1) {
            const float *wr = &twr[size_t(h - 1)], *wi = &twi[size_t(h - 1)];
            for (int i = 0; i < m; i += 2 * h) {
                float *ar = &zr[size_t(i)], *ai = &zi[size_t(i)], *br = ar + h, *bi = ai + h;
                for (int j = 0; j < h; ++j) {
                    const float tr = br[j] * wr[j] - bi[j] * wi[j];
                    const float ti = br[j] * wi[j] + bi[j] * wr[j];
                    br[j] = ar[j] - tr;
                    bi[j] = ai[j] - ti;
                    ar[j] += tr;
                    ai[j] += ti;
                }
            }
        }
        for (int k = 0; k <= m; ++k) {
            const size_t a = size_t(k == m ? 0 : k), b = size_t(k == 0 ? 0 : m - k);
            const float er = 0.5f * (zr[a] + zr[b]), ei = 0.5f * (zi[a] - zi[b]);
            const float orr = 0.5f * (zi[a] + zi[b]), oi = -0.5f * (zr[a] - zr[b]);
            re[k] = er + cr[size_t(k)] * orr - ci[size_t(k)] * oi;
            im[k] = ei + cr[size_t(k)] * oi + ci[size_t(k)] * orr;
        }
    }
    const int n, m;

private:
    std::vector<int> rev;
    std::vector<float> twr, twi, cr, ci, zr, zi;
};

inline double medianOf(std::vector<double> v) {
    if (v.empty()) return 0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + ptrdiff_t(mid), v.end());
    return v[mid];
}

// The value below which half the weight lies.
inline double weightedMedian(std::vector<std::pair<double, double>> vw) {
    if (vw.empty()) return 0;
    std::sort(vw.begin(), vw.end());
    double total = 0;
    for (const auto &p : vw) total += p.second;
    double run = 0;
    for (const auto &p : vw) {
        run += p.second;
        if (run >= 0.5 * total) return p.first;
    }
    return vw.back().first;
}

// ---------------------------------------------------------------------------
// Stage 1: the onset envelope.

// How much new sound arrives in each 5 ms frame, overall and in three bands
// (the kick's, the body's, the top's), and a coarse picture of the timbre
// for telling sections apart.
struct Onsets {
    int hop = 1;        // samples between frames; frame j is centred on sample j·hop
    size_t n = 0;
    std::vector<float> all, lo, mid, hi;
    int bands = 0;
    int stride = 8;     // onset frames per timbre frame
    size_t tn = 0;
    std::vector<float> timbre; // tn × bands: the mean compressed level of each band
};

inline bool onsetFeatures(const Ctx &c, double gain, Onsets &o, double progressFrom, double progressTo) {
    const int hop = std::max(1, int(std::lround(c.rate / 200.0)));
    const int N = nextPow2(int(c.rate * 0.016));
    const int half = N / 2;
    // Bands a third of an octave wide, but never narrower than one bin, up to 16 kHz.
    std::vector<int> edge;
    const int top = std::max(8, std::min(half - 1, int(16000.0 * N / c.rate)));
    for (int e = 1; e < top; e = std::max(e + 1, int(std::lround(e * 1.26)))) edge.push_back(e);
    edge.push_back(top);
    const int nb = int(edge.size()) - 1;
    int loEnd = 0, midEnd = 0;
    for (int b = 0; b < nb; ++b) {
        const double upper = edge[size_t(b) + 1] * c.rate / N;
        if (upper <= 190.0) loEnd = b + 1;
        if (upper <= 2600.0) midEnd = b + 1;
    }
    loEnd = std::max(1, loEnd);
    midEnd = std::max(loEnd, midEnd);
    // Above 300 Hz a band is compared with the loudest of itself and its two
    // neighbours two frames ago, so a note that bends or wobbles from band to
    // band is not taken for a new one. Below that the bands are a kick and a
    // bass apart and each is compared only with itself.
    int spreadFrom = nb;
    for (int b = nb - 1; b >= 0; --b)
        if (edge[size_t(b)] * c.rate / N >= 300.0) spreadFrom = b;

    Fft fft(N);
    std::vector<float> win(size_t(N), 0.0f), buf(size_t(N), 0.0f), re(size_t(half) + 1), im(size_t(half) + 1);
    // Scaled so a sine of amplitude 1 in a song at the reference level reads 1.
    for (int i = 0; i < N; ++i) win[size_t(i)] = float((0.5 - 0.5 * std::cos(2.0 * kPi * i / N)) * gain * 4.0 / N);

    o.hop = hop;
    o.n = c.frames / size_t(hop) + 1;
    o.bands = nb;
    o.all.assign(o.n, 0.0f);
    o.lo.assign(o.n, 0.0f);
    o.mid.assign(o.n, 0.0f);
    o.hi.assign(o.n, 0.0f);
    o.tn = o.n / size_t(o.stride) + 1;
    o.timbre.assign(o.tn * size_t(nb), 0.0f);
    std::vector<float> ring(size_t(3 * nb), 0.0f);

    for (size_t j = 0; j < o.n; ++j) {
        if ((j & 255) == 0) {
            if (c.cancelled()) return false;
            if ((j & 2047) == 0) c.report(progressFrom + (progressTo - progressFrom) * double(j) / double(o.n));
        }
        const ptrdiff_t start = ptrdiff_t(j) * hop - half;
        if (start >= 0 && size_t(start) + size_t(N) <= c.frames) {
            const float *s = c.x + start;
            for (int i = 0; i < N; ++i) buf[size_t(i)] = s[i] * win[size_t(i)];
        } else {
            for (int i = 0; i < N; ++i) {
                const ptrdiff_t s = start + i;
                buf[size_t(i)] = s >= 0 && size_t(s) < c.frames ? c.x[s] * win[size_t(i)] : 0.0f;
            }
        }
        fft.forward(buf.data(), re.data(), im.data());
        float *cur = &ring[size_t(j % 3) * size_t(nb)];
        const float *old = &ring[size_t((j + 1) % 3) * size_t(nb)]; // two frames ago
        for (int b = 0; b < nb; ++b) {
            float s = 0;
            for (int k = edge[size_t(b)]; k < edge[size_t(b) + 1]; ++k) s += std::sqrt(re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)]);
            // Compressed like hearing: below about −40 dB of the song's level a band counts for little.
            cur[b] = std::log1p(1000.0f * s);
        }
        float fl = 0, fm = 0, fh = 0;
        for (int b = 0; b < nb; ++b) {
            float ref = old[b];
            if (b >= spreadFrom) {
                if (b > 0) ref = std::max(ref, old[b - 1]);
                if (b + 1 < nb) ref = std::max(ref, old[b + 1]);
            }
            const float d = cur[b] - ref;
            if (d > 0) (b < loEnd ? fl : b < midEnd ? fm : fh) += d;
        }
        o.lo[j] = fl;
        o.mid[j] = fm;
        o.hi[j] = fh;
        o.all[j] = fl + fm + fh;
        float *t = &o.timbre[(j / size_t(o.stride)) * size_t(nb)];
        for (int b = 0; b < nb; ++b) t[b] += cur[b] * (1.0f / float(o.stride));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Stage 2: the loudness of the song millisecond by millisecond, twice: as it
// is (where a kick's thump shows) and differentiated (where its click, a
// snare and a hat show). Each follows its peaks and lets go over 40 ms, so
// the swing of a low note's own waveform is not taken for new hits.

struct Fine {
    int hop = 1;
    size_t n = 0;
    std::vector<float> full, hf;
    float floorFull = 0, floorHf = 0;
};

inline bool fineEnvelope(const Ctx &c, Fine &f) {
    f.hop = std::max(1, int(std::lround(c.rate / 1000.0)));
    f.n = c.frames / size_t(f.hop);
    f.full.assign(f.n, 0.0f);
    f.hf.assign(f.n, 0.0f);
    double sumFull = 0, sumHf = 0;
    float prev = 0;
    for (size_t b = 0; b < f.n; ++b) {
        if ((b & 16383) == 0 && c.cancelled()) return false;
        const float *s = c.x + b * size_t(f.hop);
        float a = 0, d = 0;
        for (int i = 0; i < f.hop; ++i) {
            const float v = s[i], dv = v - prev;
            prev = v;
            a += v * v;
            d += dv * dv;
        }
        sumFull += a;
        sumHf += d;
        f.full[b] = std::sqrt(a / float(f.hop));
        f.hf[b] = std::sqrt(d / float(f.hop));
    }
    const double count = double(std::max<size_t>(1, f.n)) * f.hop;
    // A rise only counts against this floor, 30 dB under the song's level,
    // so the hiss in a silence does not look like an attack.
    f.floorFull = float(0.03 * std::sqrt(sumFull / count));
    f.floorHf = float(0.03 * std::sqrt(sumHf / count));
    const float release = float(std::exp(-1.0 / 40.0));
    float ea = 0, ed = 0;
    for (size_t b = 0; b < f.n; ++b) {
        ea = std::max(f.full[b], ea * release);
        ed = std::max(f.hf[b], ed * release);
        f.full[b] = ea;
        f.hf[b] = ed;
    }
    return true;
}

struct Foot {
    double pos = 0; // samples
    float rise = 0; // natural log of how far the level jumps
    bool found = false;
};

// The steepest rise of one envelope within `radius` of `centre`, and where
// that rise begins: the point where the level has climbed a tenth of the way
// from where it stood before to the top.
inline Foot footIn(const std::vector<float> &e, float floor, int hop, double centre, double radius) {
    Foot out;
    const ptrdiff_t n = ptrdiff_t(e.size());
    const ptrdiff_t lo = std::max<ptrdiff_t>(2, ptrdiff_t(std::floor((centre - radius) / hop)));
    const ptrdiff_t hi = std::min<ptrdiff_t>(n - 5, ptrdiff_t(std::ceil((centre + radius) / hop)));
    if (hi < lo) return out;
    float best = 0;
    ptrdiff_t at = -1;
    for (ptrdiff_t i = lo; i <= hi; ++i) {
        const float r = (e[size_t(i + 3)] + floor) / (e[size_t(i - 1)] + floor);
        if (r > best) {
            best = r;
            at = i;
        }
    }
    if (at < 0 || best < 1.2f) return out;
    // The single steepest millisecond inside that rise.
    ptrdiff_t k = at;
    float step = 0;
    for (ptrdiff_t i = at; i <= at + 3; ++i) {
        const float d = e[size_t(i)] - e[size_t(i - 1)];
        if (d > step) {
            step = d;
            k = i;
        }
    }
    ptrdiff_t s = k - 1, u = k;
    while (s > 1 && k - s < 14 && e[size_t(s - 1)] < e[size_t(s)]) --s;
    while (u + 1 < n && u - k < 14 && e[size_t(u + 1)] > e[size_t(u)]) ++u;
    const float pre = e[size_t(s)], peak = e[size_t(u)];
    const float thr = pre + 0.1f * (peak - pre);
    ptrdiff_t j = s + 1;
    while (j < u && e[size_t(j)] < thr) ++j;
    const float span = e[size_t(j)] - e[size_t(j - 1)];
    const double frac = span > 0 ? std::clamp(double((thr - e[size_t(j - 1)]) / span), 0.0, 1.0) : 0.0;
    // A block's level is known at its end, so the crossing lies between the
    // end of block j−1 and the end of block j.
    out.pos = (double(j) + frac) * hop;
    out.rise = std::log((peak + floor) / (pre + floor));
    out.found = true;
    return out;
}

// The foot of whatever lands near `centre`: the band whose level jumps
// further tells, since a kick with no click only shows in the plain one and
// a hat over a held bass only in the differentiated one.
inline Foot findFoot(const Fine &f, double centre, double radius) {
    const Foot a = footIn(f.full, f.floorFull, f.hop, centre, radius);
    const Foot b = footIn(f.hf, f.floorHf, f.hop, centre, radius);
    if (!a.found) return b;
    if (!b.found) return a;
    return b.rise >= a.rise ? b : a;
}

// ---------------------------------------------------------------------------
// Stage 3: pitch content. The song is brought down to about 12 kHz and its
// spectral peaks between 50 and 2100 Hz are filed by pitch in 10-cent bins,
// the bass (under 260 Hz) apart from the rest; once the whole song has said
// how far it is tuned from 440, the bins are folded onto the twelve pitch
// classes.

struct Chroma {
    double hop = 1; // samples (at the song's rate) between frames; frame i is centred on i·hop
    size_t n = 0;
    std::vector<float> lo, hi; // n × 12 after folding
    double tuning = 0;         // cents
};

inline bool chromaFeatures(const Ctx &c, double gain, Chroma &ch, double progressFrom, double progressTo) {
    const int dec = std::max(1, int(std::lround(c.rate / 12000.0)));
    const double drate = c.rate / dec;
    const int N = 4096, H = 1024, fine = 120;
    // The low-pass before decimating: a windowed sinc that is flat to 2.5 kHz
    // and has let go of everything that would fold back under 2.1 kHz.
    const int taps = dec > 1 ? 8 * dec + 1 : 1;
    std::vector<float> fir(size_t(taps), 1.0f);
    if (dec > 1) {
        double sum = 0;
        for (int i = 0; i < taps; ++i) {
            const double t = i - (taps - 1) * 0.5, fc = 0.4 / dec;
            const double s = t == 0 ? 2 * fc : std::sin(2 * kPi * fc * t) / (kPi * t);
            const double w = 0.5 - 0.5 * std::cos(2 * kPi * (i + 1) / (taps + 1));
            fir[size_t(i)] = float(s * w);
            sum += s * w;
        }
        for (float &v : fir) v = float(v / sum);
    }
    const ptrdiff_t reach = (taps - 1) / 2;
    auto decimated = [&](ptrdiff_t m) -> float {
        const ptrdiff_t centre = m * dec;
        if (centre + reach < 0 || centre - reach >= ptrdiff_t(c.frames)) return 0.0f;
        float s = 0;
        if (centre - reach >= 0 && centre + reach < ptrdiff_t(c.frames)) {
            const float *p = c.x + (centre - reach);
            for (int i = 0; i < taps; ++i) s += p[i] * fir[size_t(i)];
        } else {
            for (int i = 0; i < taps; ++i) {
                const ptrdiff_t q = centre - reach + i;
                if (q >= 0 && q < ptrdiff_t(c.frames)) s += c.x[q] * fir[size_t(i)];
            }
        }
        return s;
    };

    Fft fft(N);
    std::vector<float> win(size_t(N), 0.0f), seg(size_t(N), 0.0f), buf(size_t(N), 0.0f), re(size_t(N / 2) + 1), im(size_t(N / 2) + 1), mag(size_t(N / 2) + 1);
    for (int i = 0; i < N; ++i) win[size_t(i)] = float((0.5 - 0.5 * std::cos(2.0 * kPi * i / N)) * gain * 4.0 / N);
    const double binHz = drate / N;
    const int kLo = std::max(2, int(std::ceil(50.0 / binHz))), kHi = std::min(N / 2 - 2, int(2100.0 / binHz));

    ch.hop = double(H) * dec;
    ch.n = (c.frames / size_t(dec)) / size_t(H) + 1;
    std::vector<float> fineLo(ch.n * size_t(fine), 0.0f), fineHi(ch.n * size_t(fine), 0.0f);
    double tuneSin = 0, tuneCos = 0;

    // seg holds the decimated samples of the frame: the window slides by H.
    for (int i = 0; i < N; ++i) seg[size_t(i)] = decimated(ptrdiff_t(i) - N / 2);
    for (size_t f = 0; f < ch.n; ++f) {
        if ((f & 15) == 0) {
            if (c.cancelled()) return false;
            if ((f & 127) == 0) c.report(progressFrom + (progressTo - progressFrom) * double(f) / double(ch.n));
        }
        if (f > 0) {
            std::copy(seg.begin() + H, seg.end(), seg.begin());
            const ptrdiff_t first = ptrdiff_t(f) * H - N / 2 + (N - H);
            for (int i = 0; i < H; ++i) seg[size_t(N - H + i)] = decimated(first + i);
        }
        for (int i = 0; i < N; ++i) buf[size_t(i)] = seg[size_t(i)] * win[size_t(i)];
        fft.forward(buf.data(), re.data(), im.data());
        float top = 0;
        for (int k = kLo - 1; k <= kHi + 1; ++k) {
            mag[size_t(k)] = std::sqrt(re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)]);
            top = std::max(top, mag[size_t(k)]);
        }
        const float thr = std::max(0.03f * top, 1e-4f);
        float *lo = &fineLo[f * size_t(fine)], *hi = &fineHi[f * size_t(fine)];
        for (int k = kLo; k <= kHi; ++k) {
            const float m = mag[size_t(k)];
            if (m < thr || m <= mag[size_t(k) - 1] || m < mag[size_t(k) + 1]) continue;
            // A parabola through the peak and its neighbours (in log level) puts the
            // partial between the bins.
            const double a = std::log(double(mag[size_t(k) - 1]) + 1e-12), b = std::log(double(m) + 1e-12), d = std::log(double(mag[size_t(k) + 1]) + 1e-12);
            const double den = a - 2 * b + d;
            const double shift = den < -1e-9 ? std::clamp(0.5 * (a - d) / den, -0.5, 0.5) : 0.0;
            const double hz = (k + shift) * binHz;
            double cents = 1200.0 * std::log2(hz / 440.0) + 900.0; // above some C
            cents -= 1200.0 * std::floor(cents / 1200.0);
            const float weight = std::sqrt(m); // so the loudest part does not drown the harmony
            (hz < 260.0 ? lo : hi)[std::min(fine - 1, int(cents / 10.0))] += weight;
            if (hz >= 110.0 && m > 0.1f * top) {
                const double ang = 2.0 * kPi * cents / 100.0;
                tuneSin += weight * std::sin(ang);
                tuneCos += weight * std::cos(ang);
            }
        }
    }
    // The partials sit a little above or below the semitones of A = 440 all
    // together; the mean of that offset around the circle is the tuning.
    ch.tuning = (tuneSin != 0 || tuneCos != 0) ? std::atan2(tuneSin, tuneCos) / (2.0 * kPi) * 100.0 : 0.0;
    ch.tuning = std::clamp(ch.tuning, -50.0, 50.0);
    // Fold: each 10-cent bin goes to its nearest semitone of the song's own
    // tuning, counting less the further between two semitones it lies.
    int target[120];
    float weight[120];
    for (int b = 0; b < fine; ++b) {
        const double semis = (b * 10.0 + 5.0 - ch.tuning) / 100.0;
        const double nearest = std::floor(semis + 0.5);
        target[b] = ((int(nearest) % 12) + 12) % 12;
        weight[b] = float(sq(std::cos(kPi * (semis - nearest))));
    }
    ch.lo.assign(ch.n * 12, 0.0f);
    ch.hi.assign(ch.n * 12, 0.0f);
    for (size_t f = 0; f < ch.n; ++f)
        for (int b = 0; b < fine; ++b) {
            ch.lo[f * 12 + size_t(target[b])] += weight[b] * fineLo[f * size_t(fine) + size_t(b)];
            ch.hi[f * 12 + size_t(target[b])] += weight[b] * fineHi[f * size_t(fine) + size_t(b)];
        }
    return true;
}

// ---------------------------------------------------------------------------
// Stage 4: tempo.

// The envelope the tempo and the beats are read from: the new sound in the
// low and middle bands, where drums have their body, and three tenths of
// the top band, which is mostly hats and they are as happy between the
// beats as on them. Its slow average is taken off, and it is levelled so a
// quiet verse counts like a loud chorus.
inline std::vector<float> beatEnvelope(const Onsets &o, double fps) {
    const size_t n = o.n;
    std::vector<float> raw(n), env(n);
    const float wl = float(tune("WL", 1.0)), wm = float(tune("WM", 1.0)), wh = float(tune("WH", 1.0));
    for (size_t i = 0; i < n; ++i) raw[i] = wl * o.lo[i] + wm * o.mid[i] + wh * o.hi[i];
    // A little blur (about 8 ms either side) so a beat a frame early or late still scores.
    static const float kernel[5] = {0.1f, 0.24f, 0.32f, 0.24f, 0.1f};
    for (size_t i = 0; i < n; ++i) {
        float s = 0;
        for (int k = -2; k <= 2; ++k) {
            const ptrdiff_t q = ptrdiff_t(i) + k;
            if (q >= 0 && q < ptrdiff_t(n)) s += raw[size_t(q)] * kernel[k + 2];
        }
        env[i] = s;
    }
    // Running sums give the moving averages.
    auto movingMean = [&](const std::vector<float> &v, ptrdiff_t radius) {
        std::vector<double> acc(n + 1, 0.0);
        for (size_t i = 0; i < n; ++i) acc[i + 1] = acc[i] + v[i];
        std::vector<float> out(n);
        for (ptrdiff_t i = 0; i < ptrdiff_t(n); ++i) {
            const ptrdiff_t a = std::max<ptrdiff_t>(0, i - radius), b = std::min<ptrdiff_t>(ptrdiff_t(n), i + radius + 1);
            out[size_t(i)] = float((acc[size_t(b)] - acc[size_t(a)]) / double(b - a));
        }
        return out;
    };
    const std::vector<float> mean = movingMean(env, ptrdiff_t(fps * 0.4));
    double total = 0;
    for (size_t i = 0; i < n; ++i) {
        env[i] = std::max(0.0f, env[i] - mean[i]);
        raw[i] = env[i] * env[i];
        total += raw[i];
    }
    const float global = float(std::sqrt(total / double(std::max<size_t>(1, n))));
    if (global <= 0) return env;
    const std::vector<float> power = movingMean(raw, ptrdiff_t(fps * 3.0));
    for (size_t i = 0; i < n; ++i) env[i] /= std::max(std::sqrt(power[i]), 0.3f * global);
    return env;
}

// Kick against snare: at every frame, the new sound in the upper bands less
// the new sound in the kick's band, each over its own typical size. A kick
// reads negative and a snare positive, so where they take turns this signal
// flips sign from beat to beat, and that tells the beat from its double and
// its half as nothing else does.
inline std::vector<float> contrastEnvelope(const Onsets &o) {
    const size_t n = o.n;
    std::vector<float> out(n, 0.0f);
    double pl = 0, ph = 0;
    for (size_t i = 0; i < n; ++i) {
        pl += sq(o.lo[i]);
        ph += sq(o.mid[i] + o.hi[i]);
    }
    if (pl <= 0 || ph <= 0 || pl < 1e-4 * ph) return out; // no low end to speak of
    const float gl = float(1.0 / std::sqrt(pl / double(n))), gh = float(1.0 / std::sqrt(ph / double(n)));
    static const float kernel[5] = {0.1f, 0.24f, 0.32f, 0.24f, 0.1f};
    for (size_t i = 0; i < n; ++i) {
        float v = 0;
        for (int k = -2; k <= 2; ++k) {
            const ptrdiff_t q = ptrdiff_t(i) + k;
            if (q >= 0 && q < ptrdiff_t(n)) v += ((o.mid[size_t(q)] + o.hi[size_t(q)]) * gh - o.lo[size_t(q)] * gl) * kernel[k + 2];
        }
        out[i] = v;
    }
    return out;
}

// The beat length (in envelope frames) at every frame. Twelve-second
// windows of the envelope are autocorrelated. A candidate beat length must
// repeat at one, two and four times itself (beat, half bar, bar): the three
// are multiplied, so a guess at two-thirds or four-thirds of the beat, whose
// "bar" is no bar, scores nothing. On top comes the backbeat: if kick and
// snare take turns at this length and agree at twice it, this is the beat
// and not its half or double. All of it is weighed by how likely a producer
// is to tap that tempo. One path is then drawn through the windows: free to
// drift, charged for a jump, and charged heavily for a jump to double, half,
// or a simple fraction, which is nearly always the same tempo heard at
// another level.
struct Tempo {
    std::vector<float> period; // beat length in envelope frames, per frame
    std::vector<size_t> jumps; // frames where the tempo really changes (by more than 6 % from one second to the next)
};

inline Tempo tempoCurve(const Ctx &c, const std::vector<float> &env, const std::vector<float> &contrast, double fps) {
    const size_t n = env.size();
    const int W = int(fps * 12.0), H = std::max(1, int(fps * 1.0)), maxLag = int(fps * 6.6) + 2;
    const int P = nextPow2(W + maxLag);
    const int nw = int(n / size_t(H)) + 1;
    std::vector<double> period, prior;
    for (double bpm = 215.0; bpm >= 55.0; bpm /= 1.005) {
        period.push_back(fps * 60.0 / bpm);
        prior.push_back(std::exp(-0.5 * sq(std::log2(bpm / tune("PC", 125.0)) / tune("PS", 0.55))));
    }
    const int ns = int(period.size());
    std::vector<float> obs(size_t(nw) * size_t(ns), 0.0f);
    Fft fft(P);
    std::vector<float> buf(static_cast<size_t>(P)), re(size_t(P / 2) + 1), im(size_t(P / 2) + 1), acf(size_t(maxLag) + 2), acd(size_t(maxLag) + 2);
    int valid = 0;
    auto autocorrelate = [&](const std::vector<float> &src, ptrdiff_t a, int len, std::vector<float> &out) {
        double mean = 0;
        for (int i = 0; i < len; ++i) mean += src[size_t(a + i)];
        mean /= len;
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < len; ++i) buf[size_t(i)] = float(src[size_t(a + i)] - mean);
        fft.forward(buf.data(), re.data(), im.data());
        // The autocorrelation is the transform of the power spectrum, which is
        // real and even, so the same forward transform gives it.
        for (int k = 0; k <= P / 2; ++k) {
            const float p = re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)];
            buf[size_t(k)] = p;
            if (k > 0 && k < P / 2) buf[size_t(P - k)] = p;
        }
        fft.forward(buf.data(), re.data(), im.data());
        const double r0 = re[0] / double(len);
        if (r0 < 1e-4 * double(P)) return false; // nothing happens in this window
        for (int l = 0; l <= maxLag; ++l) out[size_t(l)] = l < valid ? float(re[size_t(l)] / double(len - l) / r0) : 0.0f;
        return true;
    };
    auto at = [&](const std::vector<float> &v, double lag) {
        const int l = int(lag);
        if (l + 1 >= valid) return 0.0;
        const double t = lag - l;
        return double(v[size_t(l)]) * (1 - t) + double(v[size_t(l) + 1]) * t;
    };
    std::vector<double> scanG(size_t(ns), 0.0), scanAlt(size_t(ns), 0.0), scanD(size_t(ns), 0.0);
    for (int w = 0; w < nw; ++w) {
        if (c.cancelled()) return Tempo{};
        const ptrdiff_t centre = ptrdiff_t(w) * H;
        const ptrdiff_t a = std::clamp<ptrdiff_t>(centre - W / 2, 0, std::max<ptrdiff_t>(0, ptrdiff_t(n) - W));
        const int len = int(std::min<ptrdiff_t>(W, ptrdiff_t(n) - a));
        if (len < int(fps * 2.5)) continue;
        valid = std::min(maxLag, int(len * 0.7));
        if (!autocorrelate(env, a, len, acf)) continue;
        const bool turns = autocorrelate(contrast, a, len, acd);
        float *row = &obs[size_t(w) * size_t(ns)];
        for (int j = 0; j < ns; ++j) {
            const double tau = period[size_t(j)];
            const double a1 = std::max(0.0, at(acf, tau)), a2 = std::max(0.0, at(acf, 2 * tau));
            // In a file shorter than the window the bar may not fit; the half bar stands in.
            const double a4 = int(4 * tau) + 1 < valid ? std::max(0.0, at(acf, 4 * tau)) : a2;
            const double g = std::cbrt(a1 * a2 * a4);
            const double alt = turns ? std::max(0.0, at(acd, 2 * tau) - at(acd, tau)) : 0.0;
            const double dom = at(acf, 4 * tau) - std::max(std::max(at(acf, 3 * tau), at(acf, 8 * tau / 3)), std::max(at(acf, 6 * tau), at(acf, 16 * tau / 3)));
            row[j] = float((g + tune("ALT", 0.5) * alt) * std::clamp(1.0 + tune("DOM", 4.0) * std::min(0.0, dom), 0.2, 1.0) * prior[size_t(j)]);
            scanG[size_t(j)] += g;
            scanAlt[size_t(j)] += alt;
            scanD[size_t(j)] += at(acf, 4 * tau) - std::max(std::max(at(acf, 3 * tau), at(acf, 8 * tau / 3)), std::max(at(acf, 6 * tau), at(acf, 16 * tau / 3)));
        }
    }
#ifdef TF_SAMPLEANALYSIS_DEBUG
    {
        std::vector<double> scan;
        for (int j = 0; j < ns; ++j) {
            scan.push_back(fps * 60.0 / period[size_t(j)]);
            scan.push_back(scanG[size_t(j)] / nw);
            scan.push_back(scanAlt[size_t(j)] / nw);
            scan.push_back(scanD[size_t(j)] / nw);
        }
        dbg("tempo_scan_bpm_g_alt", scan);
    }
#endif
    // The cheapest path through the windows.
    std::vector<float> cost(static_cast<size_t>(ns));
    const double octPerBin = std::log2(1.005);
    for (int d = 0; d < ns; ++d) {
        const double oct = d * octPerBin, ratio = std::exp2(oct);
        double v = tune("JUMP", 6.0);
        if (oct < 0.1) v = 60.0 * oct * oct;
        for (double simple : {4.0 / 3.0, 1.5, 2.0, 3.0, 4.0})
            if (std::fabs(ratio / simple - 1.0) < 0.04) v = 40.0;
        cost[size_t(d)] = float(v);
    }
    std::vector<float> score(static_cast<size_t>(ns)), next(static_cast<size_t>(ns));
    std::vector<int16_t> from(size_t(nw) * size_t(ns), 0);
    for (int j = 0; j < ns; ++j) score[size_t(j)] = obs[size_t(j)];
    for (int w = 1; w < nw; ++w) {
        const float *row = &obs[size_t(w) * size_t(ns)];
        for (int j = 0; j < ns; ++j) {
            float best = -1e30f;
            int bi = j;
            for (int i = 0; i < ns; ++i) {
                const float v = score[size_t(i)] - cost[size_t(std::abs(i - j))];
                if (v > best) {
                    best = v;
                    bi = i;
                }
            }
            next[size_t(j)] = best + row[j];
            from[size_t(w) * size_t(ns) + size_t(j)] = int16_t(bi);
        }
        score.swap(next);
    }
    std::vector<int> path(size_t(nw), 0);
    path[size_t(nw) - 1] = int(std::max_element(score.begin(), score.end()) - score.begin());
    for (int w = nw - 1; w > 0; --w) path[size_t(w) - 1] = from[size_t(w) * size_t(ns) + size_t(path[size_t(w)])];
    Tempo out;
    out.period.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const double pos = double(i) / H;
        const int w0 = std::min(nw - 1, int(pos)), w1 = std::min(nw - 1, w0 + 1);
        // Across a jump there is nothing to glide through: each side keeps its own tempo.
        const bool jump = std::abs(path[size_t(w0)] - path[size_t(w1)]) > 12;
        const double t = jump ? (pos - w0 < 0.5 ? 0.0 : 1.0) : pos - w0;
        out.period[i] = float(std::exp(std::log(period[size_t(path[size_t(w0)])]) * (1 - t) + std::log(period[size_t(path[size_t(w1)])]) * t));
    }
    for (int w = 0; w + 1 < nw; ++w)
        if (std::abs(path[size_t(w)] - path[size_t(w) + 1]) > 12) out.jumps.push_back(std::min(n - 1, size_t((w + 0.5) * H)));
#ifdef TF_SAMPLEANALYSIS_DEBUG
    std::vector<double> bpms;
    for (int w = 0; w < nw; w += 5) bpms.push_back(fps * 60.0 / period[size_t(path[size_t(w)])]);
    dbg("tempo_path_bpm_every_5s", bpms);
#endif
    return out;
}

// ---------------------------------------------------------------------------
// Stage 5: the beats, by dynamic programming (Ellis 2007): the best chain of
// frames that lands on onsets while keeping each step close to the beat
// length the tempo curve gives there. The chain always links back, so it
// walks on through a silence at the local tempo and arrives in phase with
// whatever comes after.
inline std::vector<int> trackBeats(const Ctx &c, const std::vector<float> &env, const std::vector<float> &period, double tight) {
    const ptrdiff_t n = ptrdiff_t(env.size());
    if (n < 4) return {};
    std::vector<float> score(static_cast<size_t>(n), 0.0f);
    std::vector<int> back(static_cast<size_t>(n), -1);
    float longest = 1;
    for (float p : period) longest = std::max(longest, p);
    std::vector<float> logOf(size_t(2 * longest) + 4);
    for (size_t i = 1; i < logOf.size(); ++i) logOf[i] = std::log(float(i));
    for (ptrdiff_t t = 0; t < n; ++t) {
        if ((t & 4095) == 0 && c.cancelled()) return {};
        const float tau = period[size_t(t)], logTau = std::log(tau);
        const ptrdiff_t from = std::max<ptrdiff_t>(0, t - ptrdiff_t(2 * tau)), to = t - std::max<ptrdiff_t>(1, ptrdiff_t(0.5f * tau));
        float best = -1e30f;
        int bi = -1;
        for (ptrdiff_t p = from; p <= to; ++p) {
            const float d = logOf[size_t(t - p)] - logTau;
            const float v = score[size_t(p)] - float(tight) * d * d;
            if (v > best) {
                best = v;
                bi = int(p);
            }
        }
        score[size_t(t)] = env[size_t(t)] + (bi >= 0 ? best : 0.0f);
        back[size_t(t)] = bi;
    }
    const ptrdiff_t tail = std::max<ptrdiff_t>(0, n - ptrdiff_t(period[size_t(n) - 1]));
    ptrdiff_t at = tail;
    for (ptrdiff_t t = tail; t < n; ++t)
        if (score[size_t(t)] > score[size_t(at)]) at = t;
    std::vector<int> beats;
    for (int t = int(at); t >= 0; t = back[size_t(t)]) beats.push_back(t);
    std::reverse(beats.begin(), beats.end());
    return beats;
}

// ---------------------------------------------------------------------------
// Stage 6: from tracked beats to a grid fit to warp by.

struct RegularFit {
    double a = 0, b = 0; // beats at a + b·k
    bool steady = false;
    double inliers = 0, drift = 0, coherence = 0;
};

// One tempo through beats [from, to)? The measured attacks are wrapped
// around a circle one half-beat long: at the right beat length they all
// point the same way, however the tracker counted them and whether it sat
// on the beat or between beats in places. The beat length that lines them
// up best is searched near the tracker's own, then polished by least
// squares. The stretch is steady if three quarters of its clear attacks lie
// within 12 ms of that grid and no run of them drifts away from it, which a
// drummer's tempo does within a few bars. The grid found is right to half a
// beat; which half is decided afterwards.
inline RegularFit fitRegular(const std::vector<double> &y, const std::vector<double> &w, size_t from, size_t to, double guess, double rate) {
    RegularFit out;
    std::vector<size_t> good;
    for (size_t i = from; i < to; ++i)
        if (w[i] >= 0.25) good.push_back(i);
    if (good.size() < 16 || guess <= 0) return out;
    const double mid = 0.5 * (y[good.front()] + y[good.back()]), span = y[good.back()] - y[good.front()];
    if (span < 12 * guess) return out;
    double phase = 0;
    auto coherence = [&](double b) {
        double cs = 0, sn = 0, total = 0;
        for (size_t i : good) {
            const double ang = 4.0 * kPi * (y[i] - mid) / b;
            cs += w[i] * std::cos(ang);
            sn += w[i] * std::sin(ang);
            total += w[i];
        }
        phase = std::atan2(sn, cs);
        return std::sqrt(cs * cs + sn * sn) / total;
    };
    // Coarse steps turn the far ends of the stretch by a twentieth of a half-beat.
    double step = 0.05 * guess * guess / span, b = guess, best = -1;
    for (double v = guess * 0.98; v <= guess * 1.02; v += step) {
        const double r = coherence(v);
        if (r > best) {
            best = r;
            b = v;
        }
    }
    for (int round = 0; round < 3; ++round) {
        const double centre = b;
        for (int k = -10; k <= 10; ++k) {
            const double v = centre + k * step * 0.1, r = coherence(v);
            if (r > best) {
                best = r;
                b = v;
            }
        }
        step *= 0.1;
    }
    out.coherence = coherence(b);
    double a = mid + phase * b / (4.0 * kPi);
    auto miss = [&](size_t i) {
        const double h = 0.5 * b, r = y[i] - a;
        return r - h * std::floor(r / h + 0.5);
    };
    // Least squares on the half-beat count of each attack that is near the grid.
    for (int it = 0; it < 4; ++it) {
        const double h = 0.5 * b, reach = (it < 2 ? 0.020 : 0.012) * rate;
        double sw = 0, su = 0, sy = 0, suu = 0, suy = 0;
        for (size_t i : good) {
            const double r = miss(i);
            if (std::fabs(r) >= reach) continue;
            const double u = std::floor((y[i] - a) / h + 0.5), ww = w[i] * sq(1 - sq(r / reach)), v = y[i] - mid;
            sw += ww;
            su += ww * u;
            sy += ww * v;
            suu += ww * u * u;
            suy += ww * u * v;
        }
        const double den = sw * suu - su * su;
        if (sw <= 0 || den <= 1e-12 * sw * suu) break;
        const double h2 = (sw * suy - su * sy) / den, a2 = (sy - h2 * su) / sw + mid;
        if (std::fabs(h2 / h - 1) > 0.002) break; // least squares should polish, not wander
        a = a2;
        b = 2 * h2;
    }
    std::vector<std::pair<double, double>> vw;
    double wTotal = 0, wNear = 0;
    for (size_t i : good) {
        const double r = miss(i);
        wTotal += w[i];
        if (std::fabs(r) < 0.012 * rate) {
            wNear += w[i];
            vw.emplace_back(r, w[i]);
        }
    }
    if (vw.size() < 12) return out;
    a += weightedMedian(vw); // the phase: the attack in the middle of the pack
    out.a = a;
    out.b = b;
    out.inliers = wNear / wTotal;
    // Drift: the median miss over any 24 clear beats in a row, looking only at
    // those near enough the grid to be the same beat and not another note.
    std::vector<double> run;
    for (size_t g = 0; g + 24 <= good.size(); g += 4) {
        run.clear();
        for (size_t q = g; q < g + 24; ++q) {
            const double r = miss(good[q]);
            if (std::fabs(r) < 0.045 * rate) run.push_back(r);
        }
        if (run.size() >= 12) out.drift = std::max(out.drift, std::fabs(medianOf(run)));
    }
    out.steady = (out.inliers >= 0.55 && out.drift < 0.009 * rate) || out.inliers >= 0.85;
    return out;
}

// A grid that follows a drummer: the positions that stay close to the
// measured attacks (each weighed by how clear it is) while bending as little
// as possible. It is a penalty on the change between neighbouring beat
// lengths, solved exactly (a five-diagonal system); the penalty is then
// raised wherever two neighbours still differ by more than 0.4 %, until
// none do. Beats with no attack on them have next to no say, so the grid
// runs straight through a silence from the beats before to the beats after.
inline std::vector<double> smoothGrid(const std::vector<double> &y, const std::vector<double> &w0, size_t from, size_t to, double rate) {
    const size_t n = to - from;
    std::vector<double> x(y.begin() + ptrdiff_t(from), y.begin() + ptrdiff_t(to));
    if (n < 4) return x;
    // Work on what is left after a straight line, so the numbers stay small.
    const double base = x.front(), slope = (x.back() - x.front()) / double(n - 1);
    std::vector<double> t(n), w(n), lam(n, 30.0), d(n), e(n), f(n), l1(n), l2(n), dd(n), z(n);
    for (size_t i = 0; i < n; ++i) {
        t[i] = x[i] - (base + slope * double(i));
        w[i] = w0[from + i] + 0.02;
    }
    std::vector<double> s(n, 0.0);
    auto solve = [&]() {
        for (size_t i = 0; i < n; ++i) {
            d[i] = w[i];
            e[i] = 0;
            f[i] = 0;
        }
        for (size_t j = 1; j + 1 < n; ++j) {
            const double l = lam[j];
            d[j - 1] += l;
            d[j] += 4 * l;
            d[j + 1] += l;
            e[j - 1] -= 2 * l;
            e[j] -= 2 * l;
            f[j - 1] += l;
        }
        for (size_t i = 0; i < n; ++i) {
            double v = d[i];
            if (i >= 1) v -= l1[i - 1] * l1[i - 1] * dd[i - 1];
            if (i >= 2) v -= l2[i - 2] * l2[i - 2] * dd[i - 2];
            dd[i] = v;
            if (i + 1 < n) l1[i] = (e[i] - (i >= 1 ? l1[i - 1] * l2[i - 1] * dd[i - 1] : 0.0)) / v;
            if (i + 2 < n) l2[i] = f[i] / v;
        }
        for (size_t i = 0; i < n; ++i) {
            double v = w[i] * t[i];
            if (i >= 1) v -= l1[i - 1] * z[i - 1];
            if (i >= 2) v -= l2[i - 2] * z[i - 2];
            z[i] = v;
        }
        for (size_t i = n; i-- > 0;) {
            double v = z[i] / dd[i];
            if (i + 1 < n) v -= l1[i] * s[i + 1];
            if (i + 2 < n) v -= l2[i] * s[i + 2];
            s[i] = v;
        }
    };
    solve();
    // An attack far from the curve is some other note (or the tracker a
    // sixteenth out for a bar): it stops counting.
    for (int it = 0; it < 3; ++it) {
        for (size_t i = 0; i < n; ++i) {
            const double r = (t[i] - s[i]) / (0.030 * rate);
            w[i] = (w0[from + i] + 0.02) * (std::fabs(r) < 1 ? sq(1 - r * r) : 0.0) + 1e-4;
        }
        solve();
    }
    for (int it = 0; it < 40; ++it) {
        bool rough = false;
        for (size_t i = 1; i + 1 < n; ++i) {
            const double before = slope + s[i] - s[i - 1], after = slope + s[i + 1] - s[i];
            if (std::fabs(after - before) > 0.004 * std::min(before, after)) {
                rough = true;
                for (size_t q = (i >= 2 ? i - 2 : 0); q <= std::min(n - 1, i + 2); ++q) lam[q] = std::min(lam[q] * 3.0, 1e9);
            }
        }
        if (!rough) break;
        solve();
    }
    for (size_t i = 0; i < n; ++i) x[i] = base + slope * double(i) + s[i];
    return x;
}

struct Grid {
    std::vector<double> beats;
    bool steady = false;
    double lag = 0; // how far (samples) an attack's foot lies after the envelope frame that scored it
};

inline float peakNear(const std::vector<float> &v, double frame, int radius) {
    const ptrdiff_t n = ptrdiff_t(v.size()), mid = ptrdiff_t(std::lround(frame));
    float best = 0;
    for (ptrdiff_t i = std::max<ptrdiff_t>(0, mid - radius); i <= std::min(n - 1, mid + radius); ++i) best = std::max(best, v[size_t(i)]);
    return best;
}

inline Grid buildGrid(const Ctx &c, const Onsets &o, const Fine &fine, const Tempo &tempo, const std::vector<int> &tracked) {
    Grid g;
    const size_t n = tracked.size();
    const double rate = c.rate;
    std::vector<double> y(n), w(n, 0.0);
    // The envelope's frames and the true attacks are a constant few
    // milliseconds apart (the analysis window sees a hit coming). Measure
    // that on a first look, then look again closely around where each attack
    // should be.
    std::vector<std::pair<double, double>> lagVotes;
    for (size_t i = 0; i < n; ++i) {
        const double at = double(tracked[i]) * o.hop;
        const Foot f = findFoot(fine, at, 0.045 * rate);
        if (f.found && f.rise > 0.7f) lagVotes.emplace_back(f.pos - at, f.rise);
    }
    g.lag = lagVotes.size() >= 8 ? weightedMedian(lagVotes) : 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double at = double(tracked[i]) * o.hop + g.lag;
        const Foot f = findFoot(fine, at, 0.030 * rate);
        y[i] = f.found ? f.pos : at;
        w[i] = f.found ? std::clamp((double(f.rise) - 0.3) / 1.2, 0.0, 1.0) : 0.0;
    }
    // A real change of tempo splits the song into stretches handled one by
    // one. The tempo curve says roughly where; the beat where the tracked
    // beat lengths before and after differ most says exactly.
    std::vector<size_t> cuts{0};
    for (size_t frame : tempo.jumps) {
        const size_t near = size_t(std::lower_bound(tracked.begin(), tracked.end(), int(frame)) - tracked.begin());
        size_t at = 0;
        double most = 0;
        std::vector<double> a, b;
        for (size_t i = (near > 10 ? near - 10 : 4); i <= near + 10 && i + 5 <= n; ++i) {
            if (i < 4) continue;
            a.clear();
            b.clear();
            for (size_t k = 0; k < 4; ++k) {
                a.push_back(double(tracked[i - k] - tracked[i - k - 1]));
                b.push_back(double(tracked[i + k + 1] - tracked[i + k]));
            }
            const double d = std::fabs(std::log(medianOf(b) / medianOf(a)));
            if (d > most) {
                most = d;
                at = i;
            }
        }
        if (at >= cuts.back() + 8 && at + 8 <= n) cuts.push_back(at);
    }
    cuts.push_back(n);

    std::vector<double> x;
    x.reserve(n + 8);
    bool allSteady = true;
    std::vector<double> stats;
    for (size_t s = 0; s + 1 < cuts.size(); ++s) {
        const size_t from = cuts[s], to = cuts[s + 1];
        const double guess = to - from >= 2 ? double(tracked[to - 1] - tracked[from]) * o.hop / double(to - from - 1) : 0.0;
        RegularFit fit = fitRegular(y, w, from, to, guess, rate);
        std::vector<double> part;
        if (fit.steady) {
            // On the beat or between the beats? Hats and bass notes are happy
            // between the beats; what marks the beat is a drum with a body.
            // So the middle band decides (where both a kick's knock and a
            // snare's crack are), with the low band summed over the 50 ms a
            // kick's thump takes to arrive, and a little of the top.
            const double lo = y[from] - 0.5 * fit.b, hi = y[to - 1] + 0.5 * fit.b;
            double sums[6] = {};
            auto thump = [&](double frame) {
                double t = 0;
                for (ptrdiff_t q = ptrdiff_t(frame) - 1; q <= ptrdiff_t(frame) + 9; ++q)
                    if (q >= 0 && q < ptrdiff_t(o.n)) t += o.lo[size_t(q)];
                return t;
            };
            const double k0 = std::ceil((lo - fit.a) / fit.b);
            for (double k = k0; fit.a + k * fit.b < hi; k += 1.0) {
                const double t = fit.a + k * fit.b, f = (t - g.lag) / o.hop, h = (t + 0.5 * fit.b - g.lag) / o.hop;
                sums[0] += thump(f);
                sums[1] += thump(h);
                sums[2] += peakNear(o.mid, f, 2);
                sums[3] += peakNear(o.mid, h, 2);
                sums[4] += peakNear(o.hi, f, 2);
                sums[5] += peakNear(o.hi, h, 2);
            }
            auto lean = [&](int i) { return (sums[i] - sums[i + 1]) / std::max(1e-9, sums[i] + sums[i + 1]); };
            const double onBeat = lean(0) + lean(2) + 0.3 * lean(4);
            stats.insert(stats.end(), {lean(0), lean(2), lean(4)});
            if (onBeat < 0) fit.a += 0.5 * fit.b;
            const double k1 = std::ceil((lo - fit.a) / fit.b);
            for (double k = k1; fit.a + k * fit.b < hi; k += 1.0) part.push_back(fit.a + k * fit.b);
        } else {
            allSteady = false;
            part = smoothGrid(y, w, from, to, rate);
        }
        stats.insert(stats.end(), {double(to - from), fit.inliers, fit.drift / rate * 1000.0, fit.coherence, fit.b > 0 ? 60.0 * rate / fit.b : 0.0});
        // Where two stretches meet, no beat closer than 0.6 of a beat to the one before.
        while (!x.empty() && part.size() >= 2 && part.front() - x.back() < 0.6 * std::min(part[1] - part[0], x.size() >= 2 ? x.back() - x[x.size() - 2] : part[1] - part[0])) part.erase(part.begin());
        x.insert(x.end(), part.begin(), part.end());
    }
    dbg("segments_[lo_mid_hi]_len_inliers_driftms_coherence_bpm", stats);
    dbg("envelope_lag_ms", {g.lag / rate * 1000.0});
    g.steady = allSteady && cuts.size() == 2;
    // Carry it to both ends of the file at the tempo it has there.
    const double early = -0.020 * rate;
    while (x.size() > 2 && x.front() < early) x.erase(x.begin());
    if (x.size() < 2) return g;
    const double first = x[1] - x[0], last = x[x.size() - 1] - x[x.size() - 2];
    if (first <= 0 || last <= 0) return g;
    std::vector<double> head;
    for (double v = x.front() - first; v >= early; v -= first) head.push_back(v);
    std::reverse(head.begin(), head.end());
    head.insert(head.end(), x.begin(), x.end());
    while (head.back() < double(c.frames)) head.push_back(head.back() + last);
    while (head.size() > 2 && head[head.size() - 2] >= double(c.frames)) head.pop_back();
    g.beats = std::move(head);
    return g;
}

// ---------------------------------------------------------------------------
// Stage 7: what each beat holds, the bar lines, the sections and the key.

inline double cosineDistance(const float *a, const float *b, int n) {
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; ++i) {
        ab += double(a[i]) * b[i];
        aa += double(a[i]) * a[i];
        bb += double(b[i]) * b[i];
    }
    if (aa <= 1e-12 || bb <= 1e-12) return 0;
    return std::clamp(1.0 - ab / std::sqrt(aa * bb), 0.0, 1.0);
}

inline void standardise(std::vector<double> &v) {
    if (v.empty()) return;
    double mean = 0, var = 0;
    for (double x : v) mean += x;
    mean /= double(v.size());
    for (double x : v) var += sq(x - mean);
    const double sd = std::sqrt(var / double(v.size()));
    for (double &x : v) x = sd > 1e-9 ? (x - mean) / sd : 0.0;
}

// Per beat: the mean of the feature frames that fall inside it.
inline std::vector<float> perBeat(const std::vector<double> &beats, const std::vector<float> &frames, size_t count, int width, double hop, double offset) {
    const size_t nb = beats.size();
    std::vector<float> out(nb * size_t(width), 0.0f);
    if (count == 0) return out;
    for (size_t i = 0; i < nb; ++i) {
        const double len = i + 1 < nb ? beats[i + 1] - beats[i] : beats[i] - beats[i - 1];
        const double a = beats[i] + 0.1 * len, b = beats[i] + 0.9 * len;
        ptrdiff_t fa = ptrdiff_t(std::ceil((a - offset) / hop)), fb = ptrdiff_t(std::floor((b - offset) / hop));
        if (fb < fa) fa = fb = ptrdiff_t(std::lround((beats[i] + 0.5 * len - offset) / hop));
        fa = std::clamp<ptrdiff_t>(fa, 0, ptrdiff_t(count) - 1);
        fb = std::clamp<ptrdiff_t>(fb, fa, ptrdiff_t(count) - 1);
        float *dst = &out[i * size_t(width)];
        for (ptrdiff_t f = fa; f <= fb; ++f)
            for (int k = 0; k < width; ++k) dst[k] += frames[size_t(f) * size_t(width) + size_t(k)];
        const float scale = 1.0f / float(fb - fa + 1);
        for (int k = 0; k < width; ++k) dst[k] *= scale;
    }
    return out;
}

// The mean of rows [from, to) of a per-beat table; rows off either end count as silence.
inline std::vector<float> meanRows(const std::vector<float> &table, int width, ptrdiff_t from, ptrdiff_t to) {
    std::vector<float> out(size_t(width), 0.0f);
    const ptrdiff_t rows = ptrdiff_t(table.size()) / width;
    for (ptrdiff_t r = std::max<ptrdiff_t>(0, from); r < std::min(rows, to); ++r)
        for (int k = 0; k < width; ++k) out[size_t(k)] += table[size_t(r) * size_t(width) + size_t(k)];
    if (to > from)
        for (float &v : out) v /= float(to - from);
    return out;
}

inline void describeBeats(const Ctx &c, const Onsets &o, const Fine &fine, const Chroma &ch, const Grid &g, Analysis &out) {
    const std::vector<double> &beats = out.beats;
    const size_t nb = beats.size();
    const double rate = c.rate;
    auto lengthOf = [&](size_t i) { return i + 1 < nb ? beats[i + 1] - beats[i] : beats[i] - beats[i - 1]; };
    auto frameOf = [&](double sample) { return (sample - g.lag) / o.hop; };

    // Onset strength on each beat and its sixteenths, against the level a
    // strong beat of this song reaches.
    std::vector<float> raw(nb * 4);
    for (size_t i = 0; i < nb; ++i)
        for (int s = 0; s < 4; ++s) raw[4 * i + size_t(s)] = peakNear(o.all, frameOf(beats[i] + 0.25 * s * lengthOf(i)), 3);
    std::vector<double> onBeat(nb);
    for (size_t i = 0; i < nb; ++i) onBeat[i] = raw[4 * i];
    std::sort(onBeat.begin(), onBeat.end());
    const double strong = std::max(1e-6, onBeat[size_t(double(nb - 1) * 0.9)]);
    out.sub.resize(nb * 4);
    out.strength.resize(nb);
    for (size_t i = 0; i < nb * 4; ++i) out.sub[i] = float(std::clamp(double(raw[i]) / strong, 0.0, 1.0));
    for (size_t i = 0; i < nb; ++i) out.strength[i] = out.sub[4 * i];

#ifdef TF_SAMPLEANALYSIS_DEBUG
    {
        std::vector<double> oo(8, 0.0);
        for (size_t i = 0; i + 1 < nb; ++i) {
            const double f = frameOf(beats[i]), h = frameOf(beats[i] + 0.5 * lengthOf(i));
            oo[0] += peakNear(o.lo, f, 2);
            oo[1] += peakNear(o.lo, h, 2);
            oo[2] += peakNear(o.mid, f, 2);
            oo[3] += peakNear(o.mid, h, 2);
            oo[4] += peakNear(o.hi, f, 2);
            oo[5] += peakNear(o.hi, h, 2);
            oo[6] += findFoot(fine, beats[i], 0.03 * rate).rise;
            oo[7] += findFoot(fine, beats[i] + 0.5 * lengthOf(i), 0.03 * rate).rise;
        }
        for (double &v : oo) v /= double(nb);
        dbg("onoff_lo_mid_hi_rise", oo);
        std::vector<double> ii(6, 0.0);
        auto sumOver = [&](const std::vector<float> &v, double frame) {
            double t = 0;
            for (ptrdiff_t q = ptrdiff_t(frame) - 1; q <= ptrdiff_t(frame) + 9; ++q)
                if (q >= 0 && q < ptrdiff_t(v.size())) t += v[size_t(q)];
            return t;
        };
        for (size_t i = 0; i + 1 < nb; ++i) {
            const double f = frameOf(beats[i]), h = frameOf(beats[i] + 0.5 * lengthOf(i));
            ii[0] += sumOver(o.lo, f);
            ii[1] += sumOver(o.lo, h);
            ii[2] += sumOver(o.mid, f);
            ii[3] += sumOver(o.mid, h);
            ii[4] += sumOver(o.hi, f);
            ii[5] += sumOver(o.hi, h);
        }
        for (double &v : ii) v /= double(nb);
        dbg("onoff_sums", ii);
    }
#endif
    // The cut point: the foot of what lands on the beat, and two
    // milliseconds before it for safety.
    out.attack.resize(nb);
    for (size_t i = 0; i < nb; ++i) {
        const Foot f = findFoot(fine, beats[i], 0.030 * rate);
        out.attack[i] = f.found && f.rise > 0.4f ? std::clamp(f.pos - 0.002 * rate, beats[i] - 0.035 * rate, beats[i] + 0.035 * rate) : beats[i];
    }

    // Per-beat pictures of harmony and timbre.
    std::vector<float> both(ch.n * 12);
    for (size_t i = 0; i < both.size(); ++i) both[i] = ch.lo[i] + ch.hi[i];
    const std::vector<float> harmony = perBeat(beats, both, ch.n, 12, ch.hop, 0.0);
    const std::vector<float> bass = perBeat(beats, ch.lo, ch.n, 12, ch.hop, 0.0);
    const std::vector<float> timbre = perBeat(beats, o.timbre, o.tn, o.bands, double(o.hop) * o.stride, double(o.hop) * (o.stride - 1) * 0.5);

    // --- Bar lines. Each cue says, for each of the four possible phases, how
    // much more of it lands there than on an average beat.
    std::vector<double> kick(nb), snare(nb), harm(nb, 0.0), bassMove(nb, 0.0), section(nb, 0.0);
    for (size_t i = 0; i < nb; ++i) {
        const double f = frameOf(beats[i]);
        // The kick's thump takes some 50 ms to arrive, so its band is summed over that.
        double thump = 0;
        for (ptrdiff_t q = ptrdiff_t(f) - 1; q <= ptrdiff_t(f) + 9; ++q)
            if (q >= 0 && q < ptrdiff_t(o.n)) thump += o.lo[size_t(q)];
        kick[i] = tune("KTH", 0.0) > 0 ? thump : double(peakNear(o.lo, f, 3));
        snare[i] = tune("SNMID", 0.0) * peakNear(o.mid, f, 3) + peakNear(o.hi, f, 3);
    }
    for (ptrdiff_t i = 2; i + 2 <= ptrdiff_t(nb); ++i) {
        const std::vector<float> a = meanRows(harmony, 12, i - 2, i), b = meanRows(harmony, 12, i, i + 2);
        harm[size_t(i)] = cosineDistance(a.data(), b.data(), 12);
        const std::vector<float> a2 = meanRows(bass, 12, i - 2, i), b2 = meanRows(bass, 12, i, i + 2);
        bassMove[size_t(i)] = cosineDistance(a2.data(), b2.data(), 12);
    }
    for (ptrdiff_t i = 4; i + 4 <= ptrdiff_t(nb); ++i) {
        const std::vector<float> a = meanRows(timbre, o.bands, i - 4, i), b = meanRows(timbre, o.bands, i, i + 4);
        double d = 0;
        for (int k = 0; k < o.bands; ++k) d += std::fabs(double(a[size_t(k)]) - b[size_t(k)]);
        section[size_t(i)] = d / o.bands;
    }
#ifdef TF_SAMPLEANALYSIS_DEBUG
    {
        std::vector<double> lp(nb), mp(nb), hp(nb), st(nb);
        for (size_t i = 0; i < nb; ++i) {
            const double f = frameOf(beats[i]);
            lp[i] = peakNear(o.lo, f, 3);
            mp[i] = peakNear(o.mid, f, 3);
            hp[i] = peakNear(o.hi, f, 3);
            st[i] = out.strength[i];
        }
        std::vector<double> th = kick, all;
        for (auto *v : {&lp, &th, &mp, &hp, &st}) {
            standardise(*v);
            double sum[4] = {}, cnt[4] = {};
            for (size_t i = 0; i < nb; ++i) {
                sum[i & 3] += (*v)[i];
                cnt[i & 3] += 1;
            }
            for (int q = 0; q < 4; ++q) all.push_back(sum[q] / std::max(1.0, cnt[q]));
        }
        dbg("phase_lopeak_thump_mid_hi_strength", all);
    }
#endif
    standardise(kick);
    standardise(snare);
    standardise(harm);
    standardise(bassMove);
    auto byPhase = [&](const std::vector<double> &v) {
        std::vector<double> sum(4, 0.0), count(4, 0.0);
        for (size_t i = 0; i < nb; ++i) {
            sum[i & 3] += v[i];
            count[i & 3] += 1;
        }
        for (int p = 0; p < 4; ++p) sum[size_t(p)] /= std::max(1.0, count[size_t(p)]);
        return sum;
    };
    const std::vector<double> kickAt = byPhase(kick), snareAt = byPhase(snare), harmAt = byPhase(harm), bassAt = byPhase(bassMove);
    std::vector<double> sectionAt(4, 0.0);
    double sectionTotal = 0;
    for (size_t i = 0; i < nb; ++i) {
        // Only where the change peaks: the windows are four beats wide, so
        // the beats either side of a boundary see it too.
        bool top = section[i] > 0;
        for (size_t q = (i >= 3 ? i - 3 : 0); q <= std::min(nb - 1, i + 3) && top; ++q)
            if (section[q] > section[i] || (section[q] == section[i] && q < i)) top = false;
        if (!top) continue;
        sectionAt[i & 3] += sq(section[i]);
        sectionTotal += sq(section[i]);
    }
    for (double &v : sectionAt) v = sectionTotal > 0 ? v / sectionTotal - 0.25 : 0.0;
    double total[4];
    std::vector<double> cues;
    for (int p = 0; p < 4; ++p) {
        // The backbeat: snare on two and four, kick on one and three.
        const double back = 0.5 * ((snareAt[size_t((p + 1) & 3)] - kickAt[size_t((p + 1) & 3)]) + (snareAt[size_t((p + 3) & 3)] - kickAt[size_t((p + 3) & 3)]) -
                                   (snareAt[size_t(p)] - kickAt[size_t(p)]) - (snareAt[size_t((p + 2) & 3)] - kickAt[size_t((p + 2) & 3)]));
        total[p] = tune("DK", 1.0) * kickAt[size_t(p)] + tune("DB", 0.3) * back + tune("DH", 1.25) * harmAt[size_t(p)] + tune("DBA", 1.0) * bassAt[size_t(p)] + tune("DS", 2.5) * sectionAt[size_t(p)];
        cues.insert(cues.end(), {kickAt[size_t(p)], back, harmAt[size_t(p)], bassAt[size_t(p)], sectionAt[size_t(p)], total[p]});
    }
    dbg("downbeat_cues_kick_back_harm_bass_section_total_x4", cues);
    int best = 0;
    for (int p = 1; p < 4; ++p)
        if (total[p] > total[best]) best = p;
    double second = -1e30;
    for (int p = 0; p < 4; ++p)
        if (p != best) second = std::max(second, total[p]);
    out.downbeat = best;
    out.downbeatConfidence = float(std::clamp(1.0 - std::exp(-(total[best] - second) / 0.4), 0.0, 1.0));

    // --- Sections: at each bar line, the two bars after against the two before.
    const size_t bars = (nb - size_t(best) + 3) / 4;
    out.change.assign(bars, 0.0f);
    for (size_t k = 0; k < bars; ++k) {
        const ptrdiff_t at = ptrdiff_t(best) + 4 * ptrdiff_t(k);
        const std::vector<float> ta = meanRows(timbre, o.bands, at - 8, at), tb = meanRows(timbre, o.bands, at, at + 8);
        double d = 0;
        for (int q = 0; q < o.bands; ++q) d += std::fabs(double(ta[size_t(q)]) - tb[size_t(q)]);
        d /= o.bands;
        const std::vector<float> ha = meanRows(harmony, 12, at - 8, at), hb = meanRows(harmony, 12, at, at + 8);
        const double h = cosineDistance(ha.data(), hb.data(), 12);
        out.change[k] = float(std::clamp(std::sqrt(sq(d / 1.0) + sq(h / 0.5)), 0.0, 1.0));
    }
}

// The key: the song's pitch classes, summed over its whole length, against
// the Krumhansl-Kessler profiles of the 24 keys.
inline void findKey(const Chroma &ch, Analysis &out) {
    static const double major[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double minor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    double sum[12] = {};
    for (size_t f = 0; f < ch.n; ++f)
        for (int k = 0; k < 12; ++k) sum[k] += double(ch.lo[f * 12 + size_t(k)]) + ch.hi[f * 12 + size_t(k)];
    double mean = 0;
    for (double v : sum) mean += v / 12.0;
    auto correlate = [&](const double *profile, int root) {
        double pm = 0;
        for (int k = 0; k < 12; ++k) pm += profile[k] / 12.0;
        double ab = 0, aa = 0, bb = 0;
        for (int k = 0; k < 12; ++k) {
            const double a = sum[(k + root) % 12] - mean, b = profile[k] - pm;
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        return aa > 1e-12 ? ab / std::sqrt(aa * bb) : 0.0;
    };
    double best = -2, second = -2;
    for (int root = 0; root < 12; ++root)
        for (int m = 0; m < 2; ++m) {
            const double r = correlate(m ? minor : major, root);
            if (r > best) {
                second = best;
                best = r;
                out.keyRoot = root;
                out.keyMinor = m == 1;
            } else if (r > second) {
                second = r;
            }
        }
    out.keyConfidence = float(std::clamp(best, 0.0, 1.0) * (0.4 + 0.6 * std::clamp((best - second) / 0.08, 0.0, 1.0)));
    out.tuningCents = ch.tuning;
}

inline Analysis fallback(size_t frames, double rate) {
    Analysis a;
    const double step = std::max(1.0, rate * 0.5);
    for (double t = 0;; t += step) {
        a.beats.push_back(t);
        if (t >= double(frames)) break;
    }
    if (a.beats.size() < 2) a.beats.push_back(step);
    const size_t n = a.beats.size();
    a.bpm = 120;
    a.strength.assign(n, 0.0f);
    a.sub.assign(n * 4, 0.0f);
    a.attack = a.beats;
    a.change.assign((n + 3) / 4, 0.0f);
    return a;
}

} // namespace detail

// mono: the song mixed to mono, `frames` samples at `rate` (48000 in the app; handle 44100 too).
// progress, if given, is called with 0..1 a few dozen times. cancel, if given, is polled: when it turns
// true, return promptly with ok = false.
inline Analysis analyze(const float *mono, size_t frames, double rate, const std::function<void(float)> &progress = {}, const std::atomic<bool> *cancel = nullptr) {
    using namespace detail;
    const Ctx c{mono, frames, rate, &progress, cancel};
    c.report(0.0);
    if (!mono || rate < 8000.0 || double(frames) < 3.0 * rate) return fallback(frames, rate);
    // Everything is measured against the song's own level, so a quiet
    // master analyses like a loud one.
    double power = 0;
    for (size_t i = 0; i < frames; ++i) power += double(mono[i]) * mono[i];
    const double rms = std::sqrt(power / double(frames));
    if (rms < 1e-5) return fallback(frames, rate);
    const double gain = 0.1 / rms;

    Onsets onsets;
    if (!onsetFeatures(c, gain, onsets, 0.02, 0.55)) return fallback(frames, rate);
    Fine fine;
    if (!fineEnvelope(c, fine)) return fallback(frames, rate);
    c.report(0.6);
    Chroma chroma;
    if (!chromaFeatures(c, gain, chroma, 0.6, 0.85)) return fallback(frames, rate);
    c.report(0.85);

    const double fps = rate / onsets.hop;
    const std::vector<float> env = beatEnvelope(onsets, fps);
    double energy = 0;
    for (float v : env) energy += v;
    if (energy <= 0) return fallback(frames, rate);
    const Tempo tempo = tempoCurve(c, env, contrastEnvelope(onsets), fps);
    if (tempo.period.empty()) return fallback(frames, rate);
    c.report(0.9);
    const std::vector<int> tracked = trackBeats(c, env, tempo.period, tune("TIGHT", 800.0));
    if (tracked.size() < 4) return fallback(frames, rate);
    c.report(0.94);
    const Grid grid = buildGrid(c, onsets, fine, tempo, tracked);
    if (grid.beats.size() < 4 || c.cancelled()) return fallback(frames, rate);

    Analysis out;
    out.ok = true;
    out.steady = grid.steady;
    out.beats = grid.beats;
    std::vector<double> lengths;
    for (size_t i = 0; i + 1 < out.beats.size(); ++i) lengths.push_back(out.beats[i + 1] - out.beats[i]);
    out.bpm = 60.0 * rate / medianOf(lengths);
    describeBeats(c, onsets, fine, chroma, grid, out);
    findKey(chroma, out);
    c.report(1.0);
    return out;
}

} // namespace tf::sampler
