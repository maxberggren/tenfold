// The riot's scream (scream.h), checked against what the reference
// recording measures (docs/riot-scream.md): a screamer's register with
// the first resonance on the second harmonic, a phone's band, noise riding
// on the glottal pulses, false folds that come in with effort, open air
// in a field and walls in a room; and that it stays quiet in silence,
// bounded when shouted into, quick, and cheap.

#include <chrono>
#include <cmath>
#include <vector>

#include "scream.h"
#include "testkit.h"
#include "vocal.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;

// A sung vowel for the microphone: a glottal buzz (a saw, darkened) through
// three formants (an "a" by default), a 5.5 Hz vibrato once held, a breath
// of noise; `level` its peak-ish level. Silence before `at` and after
// at + dur.
std::vector<float> sing(float hz, double at, double dur, double total, float level = 0.3f, float f1 = 800,
                        float f2 = 1150, float f3 = 2900, uint32_t seed = 1) {
    std::vector<float> out(size_t(total * kRate), 0.0f);
    Svf a, b, c;
    a.set(f1, 6, kRate);
    b.set(f2, 8, kRate);
    c.set(f3, 10, kRate);
    OnePoleLp tilt;
    tilt.set(1400, kRate);
    Rng rng(seed);
    double ph = 0;
    const size_t i0 = size_t(at * kRate), n = size_t(dur * kRate);
    for (size_t i = 0; i < n && i0 + i < out.size(); ++i) {
        const double t = double(i) / kRate;
        const double vib = 1 + 0.012 * std::min(1.0, std::max(0.0, (t - 0.25) / 0.2)) * std::sin(kTwoPi * 5.5 * t);
        ph += hz * vib / kRate;
        ph -= std::floor(ph);
        const float buzz = tilt.step(float(2 * ph - 1)) + 0.02f * rng.bipolar();
        const float v = a.step(buzz).band + 0.5f * b.step(buzz).band + 0.25f * c.step(buzz).band;
        const float env = float(std::min(1.0, t / 0.03) * std::min(1.0, (dur - t) / 0.05));
        out[i0 + i] = v * env * level * 2.0f;
    }
    return out;
}

// The voice through the scream, as the Vocal page drives it: its pitch
// tracker, the pitch held through a quarter second of consonant, 128-
// sample blocks. Stereo out.
struct Out {
    std::vector<float> l, r;
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
Out screamed(const std::vector<float> &in, int place = scream::Field, scream::Scream *keep = nullptr,
           const std::vector<float> &gangHz = {}) {
    scream::Scream local;
    scream::Scream &s = keep ? *keep : local;
    if (!keep)
        s.init(kRate);
    s.setPlace(place);
    vocal::PitchTracker pt;
    pt.init(kRate);
    Out o;
    o.l.assign(in.size(), 0.0f);
    o.r.assign(in.size(), 0.0f);
    float held = 0;
    int unv = 1 << 30;
    std::vector<float> gl(gangHz.size(), 1.0f);
    s.setGang(gangHz.data(), gl.data(), int(gangHz.size()));
    for (size_t a = 0; a + 128 <= in.size(); a += 128) {
        for (int i = 0; i < 128; ++i)
            pt.push(in[a + size_t(i)], true);
        const bool now = pt.hz() > 0;
        if (now)
            held = pt.hz(), unv = 0;
        else
            unv += 128;
        const bool voiced = now || (held > 0 && unv < int(0.25f * kRate));
        s.setPitch(voiced ? held : 0, now, pt.clarity());
        s.process(&in[a], &o.l[a], &o.r[a], 128);
    }
    return o;
}

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
// The strongest peak within ±3 % of hz.
double peakNear(const std::vector<float> &x, double hz, double a, double b) {
    double best = 0;
    for (double f = hz * 0.97; f <= hz * 1.03; f += 1)
        best = std::max(best, power(x, f, a, b));
    return best;
}
double db(double p) { return 10 * std::log10(std::max(p, 1e-30)); }
// The energy within ±bw Hz of fc over [a, b): the DFT's bins there (a bin
// every 1/(b − a) Hz), so a harmonic's vibrato is all in it.
double bandPower(const std::vector<float> &x, double fc, double bw, double a, double b) {
    const double df = 1.0 / (b - a);
    double s = 0;
    for (double f = std::ceil((fc - bw) / df) * df; f <= fc + bw; f += df)
        s += power(x, f, a, b);
    return s;
}
// The energy between lo and hi Hz over [a, b), in dB of the whole (DFT bins
// through a Hann window, both ears summed).
double bandDb(const Out &o, double lo, double hi, double a, double b) {
    std::vector<float> m(o.l.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (o.l[i] + o.r[i]);
    const double N = (b - a) * kRate, df = kRate / N;
    double band = 0, total = 0;
    for (double f = df; f < kRate / 2; f += df * 4) {
        const double p = power(m, f, a, b);
        total += p;
        if (f >= lo && f < hi)
            band += p;
    }
    return db(band / std::max(total, 1e-30));
}
std::vector<float> mono(const Out &o) {
    std::vector<float> m(o.l.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (o.l[i] + o.r[i]);
    return m;
}
// RMS of [a, b) seconds.
double rms(const std::vector<float> &x, double a, double b) {
    const size_t i0 = size_t(a * kRate), i1 = std::min(x.size(), size_t(b * kRate));
    double s = 0;
    for (size_t i = i0; i < i1; ++i)
        s += double(x[i]) * x[i];
    return i1 > i0 ? std::sqrt(s / double(i1 - i0)) : 0;
}

}

class ScreamTests : public QObject {
    Q_OBJECT
    Q_CLASSINFO("alone", "aScreamCostsUnderAPercent")
private slots:
    // --- the throat -----------------------------------------------------------------

    void eachPhraseIsScreamedInAScreamersRegister() {
        // Sung at a man's, a woman's and a girl's pitch, the scream is the
        // note moved by whole octaves to the one nearest the reference's
        // 540 Hz: its fundamental there, and nothing left at the note sung
        // (the singer's own buzz never gets through).
        struct Case {
            float sung, screamed;
        } cases[] = {{110, 440}, {130.8f, 523.2f}, {220, 440}, {311.1f, 622.2f}, {587.3f, 587.3f}};
        for (const Case &c : cases) {
            const auto in = sing(c.sung, 0.2, 1.6, 2.0);
            const auto out = mono(screamed(in));
            const double at = peakNear(out, c.screamed, 0.8, 1.6), up = peakNear(out, c.screamed * 2, 0.8, 1.6);
            const double sungLeft = c.sung < c.screamed ? peakNear(out, c.sung, 0.8, 1.6) : 0;
            // Its harmonics are the scream's: a frequency half way between
            // two of them is far under them.
            const double between = power(out, c.screamed * 1.5, 0.8, 1.6);
            qInfo("sung %.0f Hz: screamed %.0f (%.1f dB), its octave %.1f dB, the note sung %.1f dB, half way %.1f dB",
                  c.sung, c.screamed, db(at), db(up), db(sungLeft), db(between));
            QVERIFY2(db(std::max(at, up)) > db(sungLeft) + 25, qPrintable(QString::number(c.sung)));
            QVERIFY2(db(std::max(at, up)) > db(between) + 12, qPrintable(QString::number(c.sung)));
            QVERIFY(c.screamed >= 380 && c.screamed <= 760);
        }
    }

    void theFirstResonanceRidesTheSecondHarmonic() {
        // The belter's tuning, as in the reference (H2 strongest, H1 −7 and
        // H3 −14 dB under it): on an open vowel the second harmonic leads
        // both its neighbours.
        const auto out = mono(screamed(sing(220, 0.2, 1.8, 2.2)));
        const double h1 = peakNear(out, 440, 0.8, 1.8), h2 = peakNear(out, 880, 0.8, 1.8), h3 = peakNear(out, 1320, 0.8, 1.8);
        qInfo("H1 %.1f, H2 %.1f, H3 %.1f dB", db(h1), db(h2), db(h3));
        QVERIFY2(h2 > h1 * 2 && h2 > h3 * 2, "H2 doesn't lead");
    }

    void theNoiseRidesOnTheGlottalPulses() {
        // The jet's turbulence is made by the flow: above 3 kHz, the
        // scream's envelope rises and falls with each glottal cycle (its
        // autocorrelation at one period is strong, at half a period weak),
        // and between the harmonics there is noise, 10 to 25 dB under them
        // (the reference: 16.6 dB).
        const auto in = sing(220, 0.2, 1.8, 2.2);
        const auto out = mono(screamed(in, scream::Close));
        Svf hp;
        hp.set(3000, 0.7f, kRate);
        std::vector<float> env;
        OnePoleLp lp;
        lp.set(2500, kRate);
        for (size_t i = size_t(0.8 * kRate); i < size_t(1.8 * kRate); ++i)
            env.push_back(lp.step(std::fabs(hp.step(out[i]).high)));
        double mean = 0;
        for (float v : env)
            mean += v;
        mean /= double(env.size());
        auto ac = [&](int lag) {
            double s = 0, n0 = 0;
            for (size_t i = 0; i + size_t(lag) < env.size(); ++i) {
                s += (env[i] - mean) * (env[i + size_t(lag)] - mean);
                n0 += (env[i] - mean) * (env[i] - mean);
            }
            return s / n0;
        };
        const int period = int(std::lround(kRate / 440.0f));
        const double one = std::max({ac(period - 1), ac(period), ac(period + 1)}), half = ac(period / 2);
        // (As the reference was measured: the band ±12 % of f0 round each
        // harmonic against the same band half way to the next.)
        double harm = 0, noise = 0;
        for (int k = 2; k <= 8; ++k) {
            harm += bandPower(out, 440.0 * k, 53, 0.8, 1.8);
            noise += bandPower(out, 440.0 * (k + 0.5), 53, 0.8, 1.8);
        }
        qInfo("the 3 kHz+ envelope: %.2f at one period, %.2f at half; harmonics %.1f dB over the noise between them",
              one, half, db(harm) - db(noise));
        QVERIFY2(one > 0.3 && one > half + 0.3, "the noise doesn't ride the pulses");
        QVERIFY2(db(harm) - db(noise) > 10 && db(harm) - db(noise) < 25, "noise out of the reference's range");
    }

    void effortDrawsInTheFalseFolds() {
        // Soft, the false folds stand open; loud, they cap the flow and
        // flutter at half the pitch: partials half way between the
        // harmonics come up, and the noise with them.
        auto halfHarmonics = [](const std::vector<float> &out) {
            double h = 0, sub = 0;
            for (int k = 1; k <= 4; ++k) {
                h += peakNear(out, 440.0 * k, 0.6, 2.6);
                sub += peakNear(out, 440.0 * (k + 0.5), 0.6, 2.6);
            }
            return db(sub) - db(h);
        };
        scream::Scream soft, loud;
        soft.init(kRate);
        loud.init(kRate);
        const auto s = mono(screamed(sing(220, 0.2, 2.6, 2.8, 0.02f), scream::Close, &soft));
        const float softStrain = soft.strain();
        const auto l = mono(screamed(sing(220, 0.2, 2.6, 2.8, 0.45f), scream::Close, &loud));
        const float loudStrain = loud.strain();
        const double a = halfHarmonics(s), b = halfHarmonics(l);
        qInfo("strain %.2f soft, %.2f loud; half-harmonics %.1f dB soft, %.1f dB loud (re the harmonics)", softStrain,
              loudStrain, a, b);
        QVERIFY(softStrain < 0.2f && loudStrain > 0.6f);
        QVERIFY2(b > a + 6, "no period doubling with effort");
    }

    void theThroatsFlowIsBernoulliThroughTwoSlits() {
        // With the false folds wide open and the glottis held open, the
        // flow is the glottis's own: U = A·√(2P/ρ). With the false folds
        // at the glottis's width, the two in series pass 1/√2 of it.
        auto settled = [](float As) {
            scream::Throat t;
            float u = 0;
            for (int i = 0; i < 4000; ++i) {
                t.phase = 0.26f; // the glottis at its widest, held there
                t.step(0.0f, 1000.0f, 1.0f, As, 0.0f, 0.0f, 0.0f, 16000.0f);
                u = t.flow;
            }
            return u;
        };
        const float Ag = scream::Throat::kLeak + scream::Throat::kAmax;
        const float open = settled(1.0f), equal = settled(Ag);
        const float bernoulli = Ag * std::sqrt(2.0f * 1000.0f / scream::Throat::kRho);
        qInfo("flow %.3g m³/s with the false folds open (Bernoulli %.3g), %.3g at the glottis's width", open,
              bernoulli, equal);
        QVERIFY(std::fabs(open / bernoulli - 1) < 0.02f);
        QVERIFY(std::fabs(equal / bernoulli - 1 / std::sqrt(2.0f)) < 0.02f);
    }

    // --- the band and the place -----------------------------------------------------

    void theBandIsThePhoneRecordingsSeenFromAFewMetres() {
        // The reference, voiced frames, re its 1–2 kHz band: 4–6 kHz −17 dB,
        // 6–8 kHz −29, 8–12 kHz −28 (the jets' noise goes on up after the
        // voice's harmonics stop at about 5.5 kHz), and the AAC phone
        // clip's top 40 dB down by 16 kHz. The scream: within 6 dB of each,
        // nothing under 150 Hz.
        const Out o = screamed(sing(130.8f, 0.2, 1.8, 2.2));
        const double mid = bandDb(o, 1000, 2000, 0.6, 1.8);
        const double low = bandDb(o, 0, 150, 0.6, 1.8) - mid, b46 = bandDb(o, 4000, 6000, 0.6, 1.8) - mid,
                     b68 = bandDb(o, 6000, 8000, 0.6, 1.8) - mid, b812 = bandDb(o, 8000, 12000, 0.6, 1.8) - mid,
                     top = bandDb(o, 16000, 24000, 0.6, 1.8) - mid;
        qInfo("re 1–2 kHz: under 150 Hz %.1f dB, 4–6 kHz %.1f, 6–8 kHz %.1f, 8–12 kHz %.1f, over 16 kHz %.1f", low,
              b46, b68, b812, top);
        QVERIFY(low < -40);
        QVERIFY(std::fabs(b46 + 17) < 6);
        QVERIFY(std::fabs(b68 + 29) < 6);
        QVERIFY(std::fabs(b812 + 28) < 6);
        QVERIFY(top < -40);
    }

    void theFieldHasTheGroundsEchoAndNoTail() {
        // An impulse into the place: in the field, the ground answers 3.1 ms
        // later at about half the direct sound (dulled by grass), and 100 ms
        // on almost nothing is left (open air); in the room the walls keep
        // it going for over half a second; close, nothing comes back.
        auto ir = [](int place) {
            scream::Place3 p;
            p.init(16000);
            p.set(place);
            std::vector<float> out;
            for (int i = 0; i < 16000; ++i) {
                float l, r;
                p.step(i == 0 ? 1.0f : 0.0f, l, r);
                out.push_back(0.5f * (l + r));
            }
            return out;
        };
        auto energy = [](const std::vector<float> &x, int a, int b) {
            double s = 0;
            for (int i = a; i < b; ++i)
                s += double(x[size_t(i)]) * x[size_t(i)];
            return s;
        };
        const auto field = ir(scream::Field), room = ir(scream::Room), close = ir(scream::Close);
        // The ground: the strongest return between 2 and 5 ms.
        int at = 0;
        float best = 0;
        for (int i = 32; i < 80; ++i)
            if (std::fabs(field[size_t(i)]) > best)
                best = std::fabs(field[size_t(i)]), at = i;
        const double ground = std::sqrt(energy(field, at - 8, at + 24) / energy(field, 0, 24));
        const double fieldLate = db(energy(field, 1600, 16000) / energy(field, 0, 1600));
        // The room's T60 from its Schroeder curve: the energy still to come,
        // falling from −5 to −25 dB, extrapolated to 60.
        auto t60 = [&](const std::vector<float> &x) {
            const double total = energy(x, 0, 16000);
            int a = -1, b = -1;
            for (int i = 0; i < 16000; ++i) {
                const double left = db(energy(x, i, 16000) / total);
                if (a < 0 && left < -5)
                    a = i;
                if (b < 0 && left < -25)
                    b = i;
            }
            return b > a && a >= 0 ? 3.0 * double(b - a) / 16000.0 : 0.0;
        };
        const double roomT60 = t60(room);
        const double roomRev = db(energy(room, 160, 16000) / energy(room, 0, 160));
        const double closeLate = energy(close, 800, 16000);
        qInfo("field: the ground at %.2f ms, %.2f of the direct; after 100 ms %.1f dB. Room: T60 %.2f s, everything "
              "after 10 ms %.1f dB re the first 10",
              at / 16.0, ground, fieldLate, roomT60, roomRev);
        QVERIFY2(std::abs(at - 50) <= 3, qPrintable(QString::number(at)));
        QVERIFY(ground > 0.25 && ground < 0.7);
        QVERIFY(fieldLate < -25);
        // A small furnished room, 3 m off: T60 about half a second, and far
        // more of the sound reflected than direct (its critical distance,
        // 0.057·√(V/T60), is 0.6 m; a voice's directivity toward the
        // listener takes some of that back).
        QVERIFY(roomT60 > 0.35 && roomT60 < 0.9);
        QVERIFY(roomRev > 4 && roomRev < 14);
        QVERIFY(closeLate < 1e-12);
    }

    void afterAPhraseTheFieldFallsQuicklyAndTheRoomRings() {
        // The reference, after a sung note stops: the band falls 24 dB in
        // 40 ms. The field does it too; the room holds the note up.
        const auto in = sing(311.1f, 0.2, 0.8, 2.0);
        auto after = [&](int place, double ms) {
            const auto o = mono(screamed(in, place));
            return db(power(o, 1244.4, 1.0 + ms / 1000, 1.03 + ms / 1000) / power(o, 1244.4, 0.85, 0.88));
        };
        const double f40 = after(scream::Field, 40), r40 = after(scream::Room, 40), r200 = after(scream::Room, 200);
        qInfo("40 ms after: field %.1f dB, room %.1f dB; 200 ms after, room %.1f dB", f40, r40, r200);
        QVERIFY(f40 < -20);
        QVERIFY(r40 > -15);
        QVERIFY(r200 > -40);
    }

    // --- stable, quick, cheap -----------------------------------------------------

    void silenceStaysSilentAndAPhraseDiesAway() {
        // Nothing in, nothing out; and after a loud phrase, every place
        // falls to nothing within a couple of seconds.
        const std::vector<float> quiet(size_t(kRate * 2), 0.0f);
        const Out o = screamed(quiet);
        QVERIFY(o.finite());
        QCOMPARE(o.peak(), 0.0f);
        for (int place = 0; place < scream::PlaceCount; ++place) {
            const Out p = screamed(sing(220, 0.2, 1.0, 4.0, 0.5f), place);
            QVERIFY(p.finite());
            const double tail = rms(mono(p), 3.5, 4.0);
            qInfo("%s: %.2g RMS 2.3 s after the phrase", scream::placeName(place), tail);
            QVERIFY(tail < 1e-5);
        }
    }

    void shoutedIntoItStaysBounded() {
        // A clipped full-scale square, white noise at full scale, a sine
        // sweeping 60 Hz to 2 kHz, and a voice twice over full scale leaping
        // octaves: finite, and never more than several times as loud as
        // what went in (the page's ceiling takes the rest).
        const size_t n = size_t(kRate * 3);
        std::vector<float> sq(n), noise(n), sweep(n), leaps(n);
        Rng rng(3);
        double ph = 0;
        for (size_t i = 0; i < n; ++i) {
            const double t = double(i) / kRate;
            sq[i] = std::sin(kTwoPi * 150 * t) > 0 ? 1.0f : -1.0f;
            noise[i] = rng.bipolar();
            ph += 60 * std::pow(2000.0 / 60, t / 3) / kRate;
            sweep[i] = float(std::sin(kTwoPi * ph));
        }
        for (int k = 0; k < 6; ++k) {
            const auto part = sing(k % 2 ? 98.0f : 700.0f, 0, 0.5, 0.5, 1.0f, 800, 1150, 2900, uint32_t(k + 1));
            std::copy(part.begin(), part.end(), leaps.begin() + std::ptrdiff_t(size_t(k) * part.size()));
        }
        for (const auto *x : {&sq, &noise, &sweep, &leaps})
            for (int place = 0; place < scream::PlaceCount; ++place) {
                const Out o = screamed(*x, place);
                QVERIFY(o.finite());
                qInfo("%s: peak %.2f", scream::placeName(place), o.peak());
                float in = 0;
                for (float v : *x)
                    in = std::max(in, std::fabs(v));
                // (A room returns more than the direct sound, 5 dB of
                // reverberant energy over it, and a pure tone sweeping
                // through its modes rings them with a crest of 3 to 4 over
                // the room's level: so its bound is higher. A voice never
                // does that; the page's ceiling after the style takes it.)
                const float bound = place == scream::Room ? 20.0f : 8.0f;
                QVERIFY2(o.peak() < bound * std::max(1.0f, in), qPrintable(QString::number(o.peak())));
            }
    }

    void aConsonantIsHeardWithinAMillisecond() {
        // The scream is live: a click into the microphone (made in the
        // mouth: it comes through as itself) is out within 1.5 ms.
        std::vector<float> in(size_t(kRate), 0.0f);
        Rng rng(9);
        for (size_t i = 24000; i < 24000 + 96; ++i)
            in[i] = 0.4f * rng.bipolar();
        const auto out = mono(screamed(in, scream::Close));
        size_t first = 0;
        float top = 0;
        for (size_t i = 23000; i < 26000; ++i)
            top = std::max(top, std::fabs(out[i]));
        for (size_t i = 23000; i < 26000; ++i)
            if (std::fabs(out[i]) > 0.3f * top) {
                first = i;
                break;
            }
        qInfo("the click is out %.2f ms after it went in", (double(first) - 24000) / 48.0);
        QVERIFY(top > 0);
        QVERIFY(first >= 24000 && first < 24000 + 72);
    }

    void aScreamCostsUnderAPercent() {
        TENFOLD_TIMED();
        // Ten seconds of singing through one screamer, and through five
        // (four fingers' gang shout): the best of three, as a fraction of
        // the time the sound lasts.
        const auto in = sing(220, 0.1, 9.8, 10.0);
        auto timed = [&](int gang) {
            std::vector<float> l(in.size()), r(in.size());
            const float hz[4] = {261.6f, 293.7f, 329.6f, 349.2f}, lvl[4] = {1, 1, 1, 1};
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep) {
                scream::Scream s;
                s.init(kRate);
                s.setGang(hz, lvl, gang);
                const auto t0 = std::chrono::steady_clock::now();
                for (size_t a = 0; a + 128 <= in.size(); a += 128) {
                    s.setPitch(220, true, 0.95f);
                    s.process(&in[a], &l[a], &r[a], 128);
                }
                best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            }
            return best / 10.0;
        };
        const double one = timed(0), five = timed(4);
        qInfo("one screamer %.2f %% of a core, five %.2f %%", 100 * one, 100 * five);
        QVERIFY2(one < 0.01, qPrintable(QString::number(one)));
        QVERIFY2(five < 0.02, qPrintable(QString::number(five)));
    }
};

TESTKIT_REGISTER(ScreamTests)
#include "scream_tests.moc"
