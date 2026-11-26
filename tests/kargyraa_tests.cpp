// Kargyraa (kargyraa.h), checked against what the published measurements
// say a kargyraa singer's larynx and tract do (docs/kargyraa.md): the false
// folds closing on every other glottal pulse, so the undertone at half the
// folds' rate; alternate pulses unequal in height and in length; the vowel's
// resonances each on a harmonic of the undertone, steered by the vowel or a
// finger; and that it stays quiet in silence, bounded when shouted into,
// quick, and cheap.

#include <chrono>
#include <cmath>
#include <vector>

#include "kargyraa.h"
#include "testkit.h"
#include "vocal.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;

struct Vowel {
    float f1, f2, f3;
};
// The test singer's vowels (as the offline player's).
Vowel vowel(char v) {
    switch (v) {
    case 'u': return {325, 700, 2530};
    case 'o': return {450, 800, 2830};
    case 'i': return {280, 2250, 2900};
    default: return {800, 1150, 2900};
    }
}

// A sung vowel for the microphone: a glottal buzz (a saw, darkened) through
// three formants, a 5.5 Hz vibrato once held, a breath of noise. The vowel
// may change half way (`then`), over 30 ms.
std::vector<float> sing(float hz, double at, double dur, double total, char v = 'o', char then = 0,
                        float level = 0.3f, uint32_t seed = 1) {
    std::vector<float> out(size_t(total * kRate), 0.0f);
    Svf a, b, c;
    OnePoleLp tilt;
    tilt.set(1400, kRate);
    Rng rng(seed);
    Vowel now = vowel(v);
    double ph = 0;
    const size_t i0 = size_t(at * kRate), n = size_t(dur * kRate);
    for (size_t i = 0; i < n && i0 + i < out.size(); ++i) {
        const double t = double(i) / kRate;
        const Vowel want = vowel(then && t > 0.5 * dur ? then : v);
        const float g = 1.0f - std::exp(-1.0f / (0.03f * kRate));
        now.f1 += (want.f1 - now.f1) * g;
        now.f2 += (want.f2 - now.f2) * g;
        now.f3 += (want.f3 - now.f3) * g;
        if ((i & 15) == 0) {
            a.set(now.f1, 6, kRate);
            b.set(now.f2, 8, kRate);
            c.set(now.f3, 10, kRate);
        }
        const double vib = 1 + 0.012 * std::min(1.0, std::max(0.0, (t - 0.25) / 0.2)) * std::sin(kTwoPi * 5.5 * t);
        ph += hz * vib / kRate;
        ph -= std::floor(ph);
        const float buzz = tilt.step(float(2 * ph - 1)) + 0.02f * rng.bipolar();
        const float s = a.step(buzz).band + 0.5f * b.step(buzz).band + 0.25f * c.step(buzz).band;
        const float env = float(std::min(1.0, t / 0.03) * std::min(1.0, (dur - t) / 0.05));
        out[i0 + i] = s * env * level * 2.0f;
    }
    return out;
}

// The voice through the kargyraa, as the Vocal page drives it: its pitch
// tracker, the pitch held through a quarter second of consonant, 128-
// sample blocks. The glottal flow's pulses and the areas, sample by sample,
// for the larynx's checks.
struct Out {
    std::vector<float> l, r, flow, ag, as;
    std::vector<float> under, focus, hz;
    std::vector<int> harmonic;
    float peak() const {
        float p = 0;
        for (size_t i = 0; i < l.size(); ++i)
            p = std::max({p, std::fabs(l[i]), std::fabs(r[i])});
        return p;
    }
    bool finite() const {
        for (size_t i = 0; i < l.size(); ++i)
            if (!std::isfinite(l[i]) || !std::isfinite(r[i]))
                return false;
        return true;
    }
};
Out growled(const std::vector<float> &in, int ratio = kargyraa::Half, float steer = 0) {
    kargyraa::Kargyraa k;
    k.init(kRate);
    k.setRatio(ratio);
    k.setSteer(steer);
    vocal::PitchTracker pt;
    pt.init(kRate);
    Out o;
    o.l.assign(in.size(), 0.0f);
    o.r.assign(in.size(), 0.0f);
    o.flow.assign(in.size(), 0.0f);
    o.ag.assign(in.size(), 0.0f);
    o.as.assign(in.size(), 0.0f);
    float held = 0;
    int unv = 1 << 30;
    for (size_t a = 0; a + 128 <= in.size(); a += 128) {
        for (int i = 0; i < 128; ++i)
            pt.push(in[a + size_t(i)], true);
        const bool now = pt.hz() > 0;
        if (now)
            held = pt.hz(), unv = 0;
        else
            unv += 128;
        const bool voiced = now || (held > 0 && unv < int(0.25f * kRate));
        k.setPitch(voiced ? held : 0, now, pt.clarity());
        // (A sample at a time inside the block, to watch the larynx; the
        // pitch and the steer are the block's, as on the page.)
        for (size_t i = a; i < a + 128; ++i) {
            k.process(&in[i], &o.l[i], &o.r[i], 1);
            o.flow[i] = k.larynx().flow;
            o.ag[i] = k.larynx().lastAg;
            o.as[i] = k.larynx().lastAs;
        }
        o.under.push_back(k.under());
        o.hz.push_back(k.hz());
        o.focus.push_back(k.focusHz());
        o.harmonic.push_back(k.harmonic());
    }
    return o;
}
// The block nearest a time.
size_t blockAt(double t) { return size_t(t * kRate / 128); }

// Goertzel power at hz over [a, b) seconds of a signal (Hann-windowed).
double power(const std::vector<float> &x, double hz, double a, double b) {
    const size_t i0 = size_t(a * kRate), i1 = std::min(x.size(), size_t(b * kRate));
    const double c = 2 * std::cos(kTwoPi * hz / kRate);
    double s1 = 0, s2 = 0;
    for (size_t i = i0; i < i1; ++i) {
        const double w = 0.5 - 0.5 * std::cos(kTwoPi * double(i - i0) / double(i1 - i0));
        const double s0 = x[i] * w + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
// The strongest peak within ±1.5 % of hz (the singer's vibrato in it).
double peakNear(const std::vector<float> &x, double hz, double a, double b) {
    double best = 0;
    for (double f = hz * 0.985; f <= hz * 1.015; f += 0.5)
        best = std::max(best, power(x, f, a, b));
    return best;
}
double db(double p) { return 10 * std::log10(std::max(p, 1e-30)); }

// The glottal flow's pulses between a and b seconds: each pulse's peak and
// the samples from its opening to the next one's.
struct Pulses {
    std::vector<float> height;
    std::vector<double> period;
};
Pulses pulses(const Out &o, double a, double b) {
    Pulses p;
    bool open = false;
    float top = 0;
    size_t start = 0;
    std::vector<size_t> at;
    for (size_t i = size_t(a * kRate); i < size_t(b * kRate); ++i) {
        const bool now = o.ag[i] > 2e-6f;
        if (now && !open) {
            open = true;
            top = 0;
            at.push_back(i);
        }
        if (open)
            top = std::max(top, o.flow[i]);
        if (!now && open) {
            open = false;
            p.height.push_back(top);
        }
        (void)start;
    }
    for (size_t j = 0; j + 1 < at.size(); ++j)
        p.period.push_back(double(at[j + 1] - at[j]));
    p.height.resize(std::min(p.height.size(), p.period.size()));
    return p;
}

} // namespace

class KargyraaTests : public QObject {
    Q_OBJECT
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "silenceIsSilent loudInputStaysBounded aPlayedNoteIsItsOvertoneAndFallsSilent")
private slots:
    void theUndertoneIsAtHalfTheSungPitch() {
        // A man sings C3 on /o/: the folds stay on it, and the false folds
        // close once every two cycles, so the half-harmonics (odd multiples
        // of 65.4 Hz) come up among the harmonics, where the voice sung has
        // none, and the undertone itself sounds.
        const auto in = sing(130.8f, 0.2, 2.4, 2.8, 'o');
        const auto o = growled(in);
        QVERIFY(o.finite());
        const double fu = 130.8 / 2;
        QVERIFY2(std::fabs(o.under[blockAt(1.5)] / fu - 1) < 0.02, qPrintable(QString::number(o.under[blockAt(1.5)])));
        // Each half-harmonic against the two harmonics either side of it
        // (in dB, so the vowel's envelope cancels): the subharmonic-to-
        // harmonic ratio, harmonic by harmonic.
        auto shr = [&](const std::vector<float> &x) {
            double sum = 0;
            int n = 0;
            for (int k = 3; k * fu < 1500; k += 2, ++n)
                sum += db(peakNear(x, k * fu, 0.9, 2.3)) -
                       0.5 * (db(peakNear(x, (k - 1) * fu, 0.9, 2.3)) + db(peakNear(x, (k + 1) * fu, 0.9, 2.3)));
            return sum / n;
        };
        const double out = shr(o.l), sung = shr(in);
        qInfo("half-harmonics %.1f dB re their neighbours (sung %.1f)", out, sung);
        QVERIFY2(out > -8, "no undertone in the harmonics");
        QVERIFY2(sung < -30, "the test singer already has one");
        const double under = peakNear(o.l, fu, 0.9, 2.3), h1 = peakNear(o.l, 2 * fu, 0.9, 2.3);
        qInfo("the undertone %.1f dB re the sung note's partial", db(under / h1));
        // (The undertone's own partial: a kargyraa's is weak in level, the
        // ear hears the period; but it is there, well over the singer's.)
        QVERIFY2(under > 1e-3 * h1, "no partial at the undertone");
        QVERIFY2(under > 100 * peakNear(in, fu, 0.9, 2.3) * (peakNear(o.l, 2 * fu, 0.9, 2.3) / peakNear(in, 2 * fu, 0.9, 2.3)),
                 "the undertone no stronger than the singer's");
    }

    void alternatePulsesDifferInHeightAndLength() {
        // The pulse the false folds close on is weaker and longer; the one
        // after it whole and shorter: what the high-speed films show.
        const auto o = growled(sing(130.8f, 0.2, 2.4, 2.8, 'o'));
        const Pulses p = pulses(o, 1.0, 2.2);
        QVERIFY(p.height.size() > 100);
        double hOdd = 0, hEven = 0, tOdd = 0, tEven = 0;
        for (size_t j = 0; j + 1 < p.height.size(); j += 2) {
            hEven += p.height[j];
            hOdd += p.height[j + 1];
            tEven += p.period[j];
            tOdd += p.period[j + 1];
        }
        const double strong = std::max(hEven, hOdd), weak = std::min(hEven, hOdd);
        const double tStrong = hEven > hOdd ? tEven : tOdd, tWeak = hEven > hOdd ? tOdd : tEven;
        qInfo("pulses: weak/strong %.2f; the weak cycle %.1f %% longer", weak / strong,
              100 * (tWeak - tStrong) / (0.5 * (tWeak + tStrong)));
        // (Fuks 1998: every other flow pulse 13 dB down; Lindestad 2001:
        // less. Bailly 2010: the cycle with the contact 1.19 times the
        // other.)
        QVERIFY2(weak < 0.5 * strong, "alternate pulses alike");
        QVERIFY2(weak > 0.15 * strong, "the weak pulse is gone");
        QVERIFY2(tWeak > 1.1 * tStrong, "the weak cycle isn't the longer");
        QVERIFY2(tWeak < 1.3 * tStrong, "the cycles differ too much");
    }

    void theFalseFoldsCloseOnEveryOtherPulse() {
        // In each of their cycles the false folds meet (Lindestad 2001:
        // "complete but short" closures): their area under 0.01 cm² at
        // its least, and opening again by the time the next pulse starts.
        const auto o = growled(sing(130.8f, 0.2, 2.4, 2.8, 'o'));
        const size_t T = size_t(kRate / 65.4);
        int closed = 0, cycles = 0;
        for (size_t a = size_t(1.0 * kRate); a + T < size_t(2.2 * kRate); a += T, ++cycles) {
            float least = 1;
            for (size_t i = a; i < a + T; ++i)
                least = std::min(least, o.as[i]);
            closed += least < 1e-6f;
        }
        qInfo("closed in %d of %d of their cycles", closed, cycles);
        QVERIFY(closed >= cycles - 2);
    }

    void aNoteAtTheRegistersEdgeStaysInItsOctave() {
        // A man on G2 (98 Hz), with vibrato across the register's old
        // edge: the folds stay in one octave, sample by sample.
        const auto o = growled(sing(98.0f, 0.2, 2.4, 2.8, 'o'));
        float lo = 1e9f, hi = 0;
        for (size_t b = blockAt(0.6); b < blockAt(2.4); ++b)
            lo = std::min(lo, o.hz[b]), hi = std::max(hi, o.hz[b]);
        qInfo("G2 sung: the folds from %.1f to %.1f Hz", lo, hi);
        QVERIFY2(hi / lo < 1.05f, "the octave flips");
    }

    void aHighVoiceIsMovedDownToAMansRegister() {
        // A woman on A3: the folds an octave down (A2), the undertone A1.
        const auto o = growled(sing(220.0f, 0.2, 2.0, 2.4, 'o'));
        const float u = o.under[blockAt(1.5)];
        qInfo("sung 220 Hz: undertone %.1f Hz", u);
        QVERIFY2(std::fabs(u / 55.0f - 1) < 0.02f, qPrintable(QString::number(u)));
        QVERIFY(peakNear(o.l, 55 * 3, 1.0, 2.0) > 0.03 * peakNear(o.l, 55 * 2, 1.0, 2.0));
    }

    void threeToOneIsAThird() {
        // B: the false folds at a third of the rate: partials at multiples of
        // a third of the sung pitch.
        const auto o = growled(sing(130.8f, 0.2, 2.4, 2.8, 'o'), kargyraa::Third);
        const double fu = 130.8 / 3;
        double third = 0, whole = 0;
        for (int k = 1; k * fu < 1500; ++k)
            (k % 3 ? third : whole) += peakNear(o.l, k * fu, 0.9, 2.3);
        qInfo("3:1: the thirds %.1f dB re the harmonics", db(third / whole));
        QVERIFY2(third > 0.2 * whole, "no third-harmonics");
    }

    void theVowelPutsAResonanceOnAHarmonic() {
        // Sung /u/ then /a/ on C3: each vowel's resonance sits exactly on a
        // harmonic of the undertone, a different one for each, the /u/'s
        // lower; and that harmonic stands out of the spectrum.
        const auto in = sing(130.8f, 0.2, 3.0, 3.4, 'u', 'a');
        const auto o = growled(in);
        QVERIFY(o.finite());
        auto check = [&](double a, double b, const char *name) {
            const size_t m = blockAt(0.5 * (a + b));
            const float fu = o.under[m], f = o.focus[m];
            const int k = o.harmonic[m];
            const double onIt = peakNear(o.l, double(k) * fu, a, b);
            const double beside = 0.5 * (peakNear(o.l, double(k - 1) * fu, a, b) + peakNear(o.l, double(k + 1) * fu, a, b));
            qInfo("%s: the resonance at %.1f Hz, harmonic %d of %.1f Hz (%.2f); %.1f dB over its neighbours", name, f, k,
                  fu, f / fu, db(onIt / beside));
            [&] {
                QVERIFY(k > 2);
                QVERIFY2(std::fabs(f / fu - float(k)) < 0.05f, "off the harmonic");
                QVERIFY2(onIt > 2 * beside, "the harmonic doesn't stand out");
            }();
            return k;
        };
        const int ku = check(1.0, 1.6, "/u/"), ka = check(2.4, 3.1, "/a/");
        QVERIFY2(ka > ku, "the open vowel isn't on a higher harmonic");
    }

    void aFingerSteersTheResonanceToItsNote() {
        // A finger on E5 (659 Hz): the harmonic of the 65.4 Hz undertone
        // nearest it, the tenth (654 Hz), stands out, far over what the
        // vowel alone gives it.
        const auto in = sing(130.8f, 0.2, 2.4, 2.8, 'o');
        const auto free = growled(in), steered = growled(in, kargyraa::Half, 659.3f);
        const size_t m = blockAt(1.5);
        qInfo("steered: harmonic %d at %.1f Hz", steered.harmonic[m], steered.focus[m]);
        QCOMPARE(steered.harmonic[m], 10);
        const double lift = peakNear(steered.l, 654, 1.0, 2.2) / peakNear(free.l, 654, 1.0, 2.2);
        qInfo("the tenth harmonic up %.1f dB", db(lift));
        QVERIFY(lift > 4);
    }

    void everyMelodyHarmonicStandsOut() {
        // Steered to each of the 6th to the 11th harmonics of the 70 Hz
        // undertone in turn, that harmonic stands at least 4 dB over the
        // louder of its neighbours: no hole in the source sinks one. (The
        // 7th, a half-harmonic between two of the folds' own, which are
        // stronger at the source, need only stand over them: the docs'
        // open point.)
        const auto in = sing(140.0f, 0.2, 2.2, 2.6, 'o');
        for (int k = 6; k <= 11; ++k) {
            const auto o = growled(in, kargyraa::Half, 70.0f * float(k));
            const float fu = o.under[blockAt(1.5)];
            const double on = peakNear(o.l, k * fu, 1.0, 2.2);
            const double beside = std::max(peakNear(o.l, (k - 1) * fu, 1.0, 2.2), peakNear(o.l, (k + 1) * fu, 1.0, 2.2));
            qInfo("harmonic %d: %.1f dB over its louder neighbour", k, db(on / beside));
            QVERIFY2(on > (k == 7 ? 1.0 : 2.5) * beside, qPrintable(QString::number(k)));
        }
    }

    void theTopIsThere() {
        // On /a/, the 2–4 kHz band (re the whole) within 5 dB of the
        // singer's, and 4–6 kHz within 14 (a dark male tract, the docs'
        // open point; cut off at its fifth formant it was 35 under).
        const auto in = sing(140.0f, 0.2, 2.2, 2.6, 'a');
        const auto o = growled(in);
        auto band = [&](const std::vector<float> &x, double lo, double hi) {
            double p = 0;
            for (double f = lo; f < hi; f += 5)
                p += power(x, f, 1.0, 1.4);
            return p;
        };
        for (const double lo : {2000.0, 4000.0}) {
            const double within = lo < 3000 ? 5.0 : 14.0;
            const double out = db(band(o.l, lo, lo + 2000) / band(o.l, 50, 8000)),
                         sung = db(band(in, lo, lo + 2000) / band(in, 50, 8000));
            qInfo("%.0f–%.0f Hz: %.1f dB re the whole (sung %.1f)", lo, lo + 2000, out, sung);
            QVERIFY(out > sung - within);
        }
    }

    void silenceIsSilent() {
        const std::vector<float> in(size_t(2 * kRate), 0.0f);
        const auto o = growled(in);
        QVERIFY(o.finite());
        QVERIFY2(o.peak() < 1e-6f, qPrintable(QString::number(o.peak())));
        // And after a phrase, silence again (no tail, no denormal hum).
        auto sung = sing(130.8f, 0.2, 1.0, 3.0, 'o');
        const auto after = growled(sung);
        float tail = 0;
        for (size_t i = size_t(2.0 * kRate); i < after.l.size(); ++i)
            tail = std::max(tail, std::fabs(after.l[i]));
        QVERIFY2(tail < 1e-4f, qPrintable(QString::number(tail)));
    }

    void loudInputStaysBounded() {
        // Shouted into: a full-scale buzz, a square, noise, a pure tone and
        // clicks, each a second: all finite, its level held to 6 dB over
        // the input's, and so its peaks within a kargyraa's crest factor
        // (about 5: one strong pulse every two periods, rung by narrow
        // resonances) of that.
        Rng rng(3);
        for (int kind = 0; kind < 5; ++kind) {
            std::vector<float> in(size_t(2 * kRate), 0.0f);
            double ph = 0;
            for (size_t i = size_t(0.3 * kRate); i < size_t(1.7 * kRate); ++i) {
                ph += 120.0 / kRate;
                ph -= std::floor(ph);
                const float v = kind == 0 ? float(2 * ph - 1)
                                : kind == 1 ? (ph < 0.5 ? 1.0f : -1.0f)
                                : kind == 2 ? rng.bipolar()
                                : kind == 3 ? float(std::sin(kTwoPi * ph))
                                            : (i % 4800 < 4 ? 1.0f : 0.0f);
                in[i] = v;
            }
            const auto o = growled(in);
            qInfo("kind %d: peak %.2f", kind, o.peak());
            QVERIFY(o.finite());
            QVERIFY2(o.peak() < 12.0f, qPrintable(QString::number(o.peak())));
        }
    }

    void aConsonantComesThrough() {
        // A hiss with no vowel: it is let through as it was made.
        std::vector<float> in(size_t(kRate), 0.0f);
        Rng rng(9);
        Svf hp;
        hp.set(4000, 0.7f, kRate);
        for (size_t i = size_t(0.4 * kRate); i < size_t(0.6 * kRate); ++i)
            in[i] = 0.2f * hp.step(rng.bipolar()).high;
        const auto o = growled(in);
        double pin = 0, pout = 0;
        for (size_t i = size_t(0.42 * kRate); i < size_t(0.58 * kRate); ++i)
            pin += double(in[i]) * in[i], pout += double(o.l[i]) * o.l[i];
        qInfo("a hiss comes through at %.1f dB", db(pout / pin));
        QVERIFY(pout > 0.25 * pin && pout < 2 * pin);
    }

    void itSoundsSoonAfterTheVoice() {
        // From the vowel's start, the growl is within 6 dB of its steady
        // level once the tracker has the pitch: under 60 ms.
        const auto in = sing(130.8f, 0.5, 1.5, 2.2, 'o');
        const auto o = growled(in);
        double steady = 0;
        for (size_t i = size_t(1.2 * kRate); i < size_t(1.8 * kRate); ++i)
            steady += double(o.l[i]) * o.l[i];
        steady /= 0.6 * kRate;
        double at = -1;
        for (size_t i = size_t(0.5 * kRate); i + 480 < size_t(1.2 * kRate); i += 48) {
            double p = 0;
            for (size_t j = i; j < i + 480; ++j)
                p += double(o.l[j]) * o.l[j];
            if (p / 480 > 0.25 * steady) {
                at = double(i + 480) / kRate - 0.5;
                break;
            }
        }
        qInfo("the growl is in %.0f ms after the vowel", 1000 * at);
        QVERIFY(at > 0 && at < 0.06);
    }

    void anAttackDoesNotBurst() {
        // Three phrases on three vowels: in each one's first 200 ms, no
        // 25 ms is more than 4.5 dB over the loudest 25 ms of its steady
        // part (a growl's own 25 ms windows differ by a few dB, its strong
        // pulse coming every two periods).
        std::vector<float> in(size_t(5.4 * kRate), 0.0f);
        const char vowels[] = {'a', 'e', 'u'};
        for (int p = 0; p < 3; ++p) {
            const auto one = sing(130.8f, 0.3 + 1.6 * p, 1.2, 5.4, vowels[p], 0, 0.3f, uint32_t(11 + p));
            for (size_t i = 0; i < in.size(); ++i)
                in[i] += one[i];
        }
        const auto o = growled(in);
        auto loudest = [&](double a, double b) {
            double best = 0;
            for (double t = a; t < b; t += 0.005) {
                double s = 0;
                for (size_t i = size_t(t * kRate); i < size_t((t + 0.025) * kRate); ++i)
                    s += double(o.l[i]) * o.l[i];
                best = std::max(best, s);
            }
            return best;
        };
        for (int p = 0; p < 3; ++p) {
            const double t0 = 0.3 + 1.6 * p;
            const double burst = db(loudest(t0, t0 + 0.2) / loudest(t0 + 0.6, t0 + 0.8));
            qInfo("/%c/: the attack's loudest 25 ms %+.1f dB re the steady part's", vowels[p], burst);
            QVERIFY2(burst < 4.5, qPrintable(QString::number(burst)));
        }
    }

    void aKargyraaCostsUnderAPercent() {
        TENFOLD_TIMED();
        // Ten seconds of singing, best of three, as a share of the time the
        // sound lasts.
        const auto in = sing(130.8f, 0.1, 9.8, 10.0, 'o', 'a');
        std::vector<float> l(in.size()), r(in.size());
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep) {
            kargyraa::Kargyraa k;
            k.init(kRate);
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t a = 0; a + 128 <= in.size(); a += 128) {
                k.setPitch(130.8f, true, 0.95f);
                k.process(&in[a], &l[a], &r[a], 128);
            }
            best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        }
        const double share = best / 10.0;
        qInfo("a kargyraa %.2f %% of a core", 100 * share);
        QVERIFY2(share < 0.01, qPrintable(QString::number(share)));
    }

    void aPlayedKargyraaCostsUnderAPercent() {
        TENFOLD_TIMED();
        // The Lead page's: ten seconds of a melody (a note every half
        // second, legato) through one voice, best of three.
        double best = 1e9;
        for (int rep = 0; rep < 3; ++rep) {
            kargyraa::Played p;
            p.init(kRate);
            Rng rng(5);
            p.start(false, rng);
            float sink = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < int(10 * kRate); ++i) {
                const float note = 220.0f * std::exp2(float((i / 24000) % 8) / 12.0f);
                sink += p.step(note, 0.5f, 0.6f, true, 0.2f);
            }
            best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            QVERIFY(std::isfinite(sink));
        }
        qInfo("a played kargyraa: %.2f %% of a core", 100 * best / 10.0);
        QVERIFY2(best / 10.0 < 0.01, qPrintable(QString::number(best / 10.0)));
    }

    void aPlayedLineHoldsItsGrowl() {
        // A line in A (its root's growl, A1): A4, E5, A5 and E4 are all
        // harmonics of it, so the growl holds under them, the vowel picking
        // another harmonic each time; C5 is not (its 9.5th), so the growl
        // moves, to C2, the note's own name: in the key, never a pure third
        // under the melody.
        kargyraa::Played p;
        p.init(kRate);
        Rng rng(5);
        p.start(false, rng);
        const float rootHz = 55.0f;
        auto play = [&](float note) {
            for (int i = 0; i < int(0.3f * kRate); ++i)
                p.step(note, 0.5f, 0.6f, true, 0.2f, rootHz);
            return p.under();
        };
        for (const float note : {440.0f, 659.26f, 880.0f, 329.63f}) {
            const float u = play(note);
            qInfo("%.0f Hz over %.2f Hz (harmonic %d)", double(note), double(u), p.harmonic());
            QVERIFY2(std::fabs(u / 55.0f - 1.0f) < 0.005f, qPrintable(QString::number(u)));
        }
        const float c = play(523.25f);
        qInfo("C5 over %.2f Hz (harmonic %d)", double(c), p.harmonic());
        QVERIFY2(std::fabs(1200 * std::log2(c / 65.41f)) < 5, qPrintable(QString::number(c)));
    }

    void aPlayedNoteIsItsOvertoneAndFallsSilent() {
        // The Lead's voice: an A4 held two seconds sounds its note as its
        // line, within six cents over the held stretch (over a third of a
        // second it wanders ±5–10, as a held voice does); let go, it is
        // silent a quarter second on.
        kargyraa::Played p;
        p.init(kRate);
        Rng rng(5);
        p.start(false, rng);
        std::vector<float> o(size_t(2.5 * kRate));
        for (size_t i = 0; i < o.size(); ++i)
            o[i] = p.step(440.0f, 0.5f, 0.6f, i < size_t(2 * kRate), 0.2f);
        double best = 0, at = 0;
        for (double f = 430; f < 450; f += 0.1) {
            const double q = power(o, f, 0.4, 1.95);
            if (q > best)
                best = q, at = f;
        }
        qInfo("a played A4: its line at %.2f Hz", at);
        QVERIFY(std::fabs(1200 * std::log2(at / 440.0)) < 6);
        float tail = 0;
        for (size_t i = size_t(2.25 * kRate); i < o.size(); ++i)
            tail = std::max(tail, std::fabs(o[i]));
        QVERIFY2(tail < 1e-4f, qPrintable(QString::number(tail)));
    }
};

TESTKIT_REGISTER(KargyraaTests)
#include "kargyraa_tests.moc"
