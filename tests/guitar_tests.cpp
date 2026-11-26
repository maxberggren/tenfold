// The overdriven guitar (guitar.h), checked against what the papers and the
// data sheets say of the real one (docs/guitar.md): its strings' stiffness
// (Fletcher), every note in tune, the pickup's comb, the amp's operating
// points (Marshall's drawing), its uneven clipping and its compression, its
// sag and its blocking, the tone stack against the circuit, what its
// oversampling leaves of aliasing, that nothing runs away, and what it
// costs. Then the Lead's Guitar sound, played.

#include <chrono>
#include <cstdio>
#include <vector>

#include <QJsonArray>

#include "guitar.h"
#include "lead.h"
#include "offline.h"
#include "parts.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr float kRate = 48000;
constexpr int kLead = 3;

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}

// The Lead's Guitar, found by its name (other sounds may come before it).
int guitarSound() {
    for (int s = 0; s < lead::kSounds; ++s)
        if (std::string(lead::sound(s).name) == "Guitar")
            return s;
    return -1;
}

// One note picked on a fresh guitar, what the chosen tap gives for `seconds`.
std::vector<float> pick(guitar::Guitar &g, float midi, float vel, double seconds,
                        guitar::Guitar::Tap tap = guitar::Guitar::Sound, float where = 0.5f, float feedback = 0) {
    g.setTap(tap);
    g.silence();
    g.start(midi);
    g.setPitch(midiToHz(midi), 0);
    g.setFeedback(feedback);
    g.pluck(vel, where);
    std::vector<float> out;
    float buf[32];
    for (int b = 0; b < int(seconds * kRate) / 32; ++b) {
        std::fill(buf, buf + 32, 0.0f);
        g.setPitch(midiToHz(midi), 0);
        g.render(buf, 32, 1.0f);
        out.insert(out.end(), buf, buf + 32);
    }
    return out;
}

// The level (dB) of a Hann-windowed single frequency over [from, from+len).
double levelAt(const std::vector<float> &x, double from, double len, double hz) {
    const size_t a = size_t(from * kRate), n = size_t(len * kRate);
    double re = 0, im = 0;
    const double step = 2 * M_PI * hz / kRate;
    for (size_t i = 0; i < n && a + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n - 1));
        re += w * x[a + i] * std::cos(step * double(i));
        im -= w * x[a + i] * std::sin(step * double(i));
    }
    return 20 * std::log10(std::sqrt(re * re + im * im) / (0.25 * double(n)) + 1e-15);
}

// The strongest peak within ±cents of hz over a window: coarse, then fine.
double peakHz(const std::vector<float> &x, double from, double len, double hz, double cents = 40) {
    double best = hz, top = -1e9;
    const double lo = hz * std::exp2(-cents / 1200), hi = hz * std::exp2(cents / 1200);
    const double step = std::max(0.05, 0.25 / len);
    for (double f = lo; f <= hi; f += step) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    const double c = best;
    for (double f = c - step; f <= c + step; f += step / 20) {
        const double l = levelAt(x, from, len, f);
        if (l > top)
            top = l, best = f;
    }
    return best;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

// A sine through the amp alone (and the cab), the last `keep` samples.
std::vector<float> throughAmp(float hz, float volts, double seconds, int over = 4, bool cab = false, size_t keep = 0) {
    static guitar::Amp amp;
    static guitar::Cab c;
    amp.init(kRate, over);
    c.init(kRate);
    std::vector<float> y;
    const int n = int(seconds * kRate);
    for (int i = 0; i < n; ++i) {
        float v = amp.process(volts * std::sin(float(2 * M_PI * hz * i / kRate)));
        if (cab)
            v = c.step(v);
        if (keep == 0 || size_t(n - i) <= keep)
            y.push_back(v);
    }
    return y;
}

// What lies between the harmonics of hz, against them, under 20 kHz (dB),
// by a four-term Blackman–Harris window over the last 16384 samples.
double offHarmonic(const std::vector<float> &y, double hz) {
    const int n = 16384;
    const size_t off = y.size() - n;
    double harm = 0, other = 0;
    std::vector<double> w(n);
    for (int i = 0; i < n; ++i) {
        const double t = 2 * M_PI * i / (n - 1);
        w[size_t(i)] = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
    }
    for (int k = 1; k < n / 2; ++k) {
        const double f = k * kRate / n;
        if (f > 20000)
            break;
        double re = 0, im = 0;
        const double st = 2 * M_PI * double(k) / n;
        for (int i = 0; i < n; ++i) {
            re += w[size_t(i)] * y[off + size_t(i)] * std::cos(st * i);
            im -= w[size_t(i)] * y[off + size_t(i)] * std::sin(st * i);
        }
        const double r = f / hz, d = std::fabs(r - std::round(r)) * hz;
        (d < 15 ? harm : other) += re * re + im * im;
    }
    return 10 * std::log10(other / harm);
}

std::string onSound(int snd, const std::string &rest) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < snd; ++k)
        s += fmt("key %.3f Right\n", 0.01 + 0.012 * k);
    return s + rest;
}
// The x of a neck cell (A minor: 0 is A3, 7 A4).
float cellX(int q) { return 0.22f + 0.78f * (float(q) + 0.5f) / 15.0f; }

double levelOf(const testkit::Take &take, double from, double to) {
    const size_t a = size_t(from * kRate), b = std::min(take.left.size(), size_t(to * kRate));
    double e = 1e-30;
    for (size_t i = a; i < b; ++i) {
        const double x = 0.5 * (take.left[i] + take.right[i]);
        e += x * x;
    }
    return 10 * std::log10(2.0 * e / double(std::max<size_t>(b - a, 1)));
}

std::vector<float> mono(const testkit::Take &t) {
    std::vector<float> m(t.left.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (t.left[i] + t.right[i]);
    return m;
}

}

class GuitarTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs them with the machine to itself.
    Q_CLASSINFO("alone", "aGuitarStaysInsideBudget aRunStaysInsideBudget")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "nothingRunsAway theLeadPlaysItsGuitar")
private slots:
    void initTestCase() { guitar::prepare(); }

    void theStringsAreSteel() {
        // Fletcher's B = π³E·d⁴/(64·T·L²) for a 10–46 set on a Les Paul,
        // with D'Addario's tensions: the plain G the stiffest of them, the
        // plain E the least (about 1.1e-4 and 1.3e-5, docs/guitar.md).
        const double g = guitar::openB(guitar::stringSpec(3)), e = guitar::openB(guitar::stringSpec(5)),
                     b = guitar::openB(guitar::stringSpec(4));
        QVERIFY2(g > 0.9e-4 && g < 1.4e-4, qPrintable(QString::number(g)));
        QVERIFY2(e > 1.0e-5 && e < 1.8e-5, qPrintable(QString::number(e)));
        QVERIFY2(b > 3.0e-5 && b < 5.5e-5, qPrintable(QString::number(b)));
        // A lead player's notes land in the middle of the neck.
        QCOMPARE(guitar::stringFor(69), 5); // A4: the E string, fifth fret
        QCOMPARE(guitar::stringFor(57), 2); // A3: the D string, seventh fret
        QCOMPARE(guitar::stringFor(40), 0); // the low E, open
    }

    void everyNoteIsOnItsPitch() {
        // Picked, through the pickup, from E2 to E6: the fundamental on its
        // note within a few cents. (A string swinging this wide is stretched
        // a little by its own swing, and rings a cent or three sharp of the
        // pitch it was tuned to at rest, as a real one does.)
        static guitar::Guitar g;
        g.init(kRate);
        for (float midi : {40.0f, 45.0f, 52.0f, 57.0f, 62.0f, 69.0f, 76.0f, 81.0f, 88.0f}) {
            const auto x = pick(g, midi, 0.5f, 1.4, guitar::Guitar::Pickup);
            const double want = midiToHz(midi), got = peakHz(x, 0.6, 0.8, want);
            QVERIFY2(std::fabs(cents(got, want)) < 4.0, qPrintable(QStringLiteral("%1: %2 Hz").arg(midi).arg(got)));
        }
    }

    void itsPartialsStretchAsFletcherSays() {
        // On the plain G string at the seventh fret (B about 2.8e-4), the
        // partials stand where f_n = n·f1·√((1 + B·n²)/(1 + B)) puts them,
        // within a few cents (the allpasses' fit is closest low down), and
        // plainly sharp of the harmonics: the eighth some 15 cents.
        static guitar::Guitar g;
        g.init(kRate);
        const float midi = 62;
        const auto x = pick(g, midi, 0.5f, 1.6, guitar::Guitar::Pickup);
        QCOMPARE(g.string(), 3);
        const double B = g.B(), f1 = peakHz(x, 0.5, 1.0, midiToHz(midi));
        QVERIFY2(B > 2.2e-4 && B < 3.4e-4, qPrintable(QString::number(B)));
        for (int n = 2; n <= 8; ++n) {
            const double want = n * f1 * std::sqrt((1 + B * n * n) / (1 + B));
            const double got = peakHz(x, 0.5, 1.0, want, 25);
            QVERIFY2(std::fabs(cents(got, want)) < 8.0, qPrintable(QStringLiteral("n=%1: %2 against %3").arg(n).arg(got).arg(want)));
            if (n == 8)
                QVERIFY2(cents(got, n * f1) > 10, qPrintable(QString::number(cents(got, n * f1))));
        }
    }

    void thePickupCombsTheString() {
        // A humbucker's middle 42 mm from the saddle hears nothing of the
        // partials whose nodes fall there: with the B string fretted to 10
        // times that and the E string to 8, the tenth and the eighth partial
        // are notched well under their neighbours (the notch moves with the
        // fret). Picked lightly, 6 mm off the saddle, so the pick's own comb
        // lies far above them.
        static guitar::Guitar g;
        g.init(kRate);
        for (const auto &c : {std::pair<int, int>{4, 10}, {5, 8}}) {
            const int k = c.second;
            const double L = guitar::kPickupD * k;
            const float midi = float(guitar::stringSpec(c.first).midi + 12 * std::log2(guitar::kScale / L));
            g.setTap(guitar::Guitar::Pickup);
            g.silence();
            g.start(midi);
            g.setPitch(midiToHz(midi), 0);
            g.pickAt(0.6f, 0.006f);
            QCOMPARE(g.string(), c.first);
            std::vector<float> x;
            float buf[32];
            for (int b = 0; b < int(0.6 * kRate) / 32; ++b) {
                std::fill(buf, buf + 32, 0.0f);
                g.render(buf, 32, 1.0f);
                x.insert(x.end(), buf, buf + 32);
            }
            const double f1 = midiToHz(midi);
            auto at = [&](int n) { return levelAt(x, 0.05, 0.4, peakHz(x, 0.05, 0.4, n * f1 * std::sqrt(1 + g.B() * n * n), 15)); };
            const double notch = at(k), around = 0.5 * (at(k - 1) + at(k + 1));
            QVERIFY2(notch < around - 12, qPrintable(QStringLiteral("k=%1: %2 against %3").arg(k).arg(notch).arg(around)));
        }
    }

    void theAmpRestsWhereMarshallDrewIt() {
        // The 2204's drawing: the input stage's plate about 192 V with 1.6 V
        // on its cathode, the cold stage's plate 230 V and 2.4 V across its
        // 10k, the third's plate 150 V with 0.9 V across its 820 Ω; the
        // EL34s biased near −40 V for 36 mA.
        const auto &T = guitar::tables();
        QVERIFY2(std::fabs(T.first.vq - 192) < 12, qPrintable(QString::number(T.first.vq)));
        QVERIFY2(std::fabs(-T.first.wq - 1.6) < 0.25, qPrintable(QString::number(-T.first.wq)));
        QVERIFY2(std::fabs(T.cold.vq - 230) < 15, qPrintable(QString::number(T.cold.vq)));
        QVERIFY2(std::fabs(T.cold.ikq * 10e3 - 2.4) < 0.4, qPrintable(QString::number(T.cold.ikq * 10e3)));
        QVERIFY2(std::fabs(T.third.vq - 150) < 12, qPrintable(QString::number(T.third.vq)));
        QVERIFY2(std::fabs(T.third.ikq * 820 - 0.9) < 0.2, qPrintable(QString::number(T.third.ikq * 820)));
        QVERIFY2(T.el34Bias < -30 && T.el34Bias > -50, qPrintable(QString::number(T.el34Bias)));
    }

    void theColdStageClipsOneSideFirst() {
        // The cold stage (10k unbypassed) swung either way by the same drive:
        // cut off on one side long before the grid conducts on the other,
        // so its plate swings unevenly, the source of the even harmonics.
        const auto &t = guitar::tables().cold;
        const double up = t.at(t.vp, -8.0) - t.vq, down = t.vq - t.at(t.vp, 8.0);
        QVERIFY2(up > 0 && down > 0, "");
        QVERIFY2(std::max(up, down) > 1.3 * std::min(up, down), qPrintable(QStringLiteral("%1 %2").arg(up).arg(down)));
    }

    void theAmpSoundsEvenHarmonicsAndCompresses() {
        // A sine into the amp, at a picked note's level as it fades: the
        // second harmonic stands within 30 dB of the first (a symmetric
        // clipper's would be far under). Turned up this far it saturates
        // from a few tens of millivolts: from 30 mV to 1 V the output rises
        // by under 4 dB, where from 1 mV it rises by more than 15.
        const float hz = 330;
        for (float v : {0.03f, 0.1f}) {
            const auto x = throughAmp(hz, v, 0.8);
            const double h1 = levelAt(x, 0.5, 0.25, hz), h2 = levelAt(x, 0.5, 0.25, 2 * hz);
            QVERIFY2(h2 > h1 - 30, qPrintable(QStringLiteral("%1 V: %2 %3").arg(v).arg(h1).arg(h2)));
        }
        const auto faint = throughAmp(hz, 0.001f, 0.8), soft = throughAmp(hz, 0.03f, 0.8), hard = throughAmp(hz, 1.0f, 0.8);
        auto rms = [](const std::vector<float> &x) {
            double e = 0;
            for (size_t i = x.size() / 2; i < x.size(); ++i)
                e += double(x[i]) * x[i];
            return 10 * std::log10(e / double(x.size() / 2));
        };
        QVERIFY2(rms(hard) - rms(soft) < 4.0, qPrintable(QStringLiteral("%1 %2").arg(rms(hard)).arg(rms(soft))));
        QVERIFY2(rms(hard) - rms(faint) > 15.0, qPrintable(QStringLiteral("%1 %2").arg(rms(hard)).arg(rms(faint))));
        QVERIFY2(rms(hard) > 20, qPrintable(QString::number(rms(hard)))); // tens of volts on the speaker
    }

    void itsSupplySagsAndItsGridsBlock() {
        // Played hard, the EL34s draw their supply down by tens of volts
        // (from about 470 V; Ampbooks reckons 60–80 V at full power), and
        // the third stage's grid, driven far past its cathode by the cold
        // stage's swing, conducts on the peaks and charges its coupling
        // capacitor, which holds the grid that much under where it was: its
        // bias moves down by volts (blocking). Left alone, both recover
        // within a second. (The cold stage's own grid, its cathode some 12 V
        // up on its 10k, conducts only on the hardest strokes.)
        static guitar::Amp a;
        a.init(kRate);
        const double rest = a.B;
        QVERIFY2(rest > 455 && rest < 485, qPrintable(QString::number(rest)));
        double low = rest, charged = 0;
        for (int i = 0; i < int(0.5 * kRate); ++i) {
            a.process(0.5f * std::sin(float(2 * M_PI * 220 * i / kRate)));
            low = std::min(low, a.B);
            charged = std::max(charged, a.third.vc);
        }
        QVERIFY2(rest - low > 15 && rest - low < 100, qPrintable(QString::number(rest - low)));
        QVERIFY2(charged > 1.0, qPrintable(QString::number(charged)));
        for (int i = 0; i < int(1.0 * kRate); ++i)
            a.process(0);
        QVERIFY2(std::fabs(a.B - rest) < 3, qPrintable(QString::number(a.B)));
        QVERIFY2(std::fabs(a.third.vc) < 0.2 * std::fabs(charged), qPrintable(QString::number(a.third.vc)));
    }

    void theToneStackIsTheCircuit() {
        // The digital stack (at the amp's rate) against the circuit's nodal
        // solution, at the lead setting and the extremes, within 0.2 dB up
        // to 6 kHz.
        const double rate = 4 * kRate;
        for (const auto &knobs : {std::array<double, 3>{0.6, 0.55, 0.45}, {0.1, 1.0, 0.0}, {1.0, 0.05, 1.0}}) {
            guitar::ToneStack st;
            st.set(knobs[0], knobs[1], knobs[2], rate);
            for (double f : {40.0, 120.0, 400.0, 900.0, 2500.0, 6000.0}) {
                const std::complex<double> z = std::exp(std::complex<double>(0, -2 * M_PI * f / rate));
                const auto num = st.b[0] + st.b[1] * z + st.b[2] * z * z + st.b[3] * z * z * z;
                const auto den = 1.0 + st.a[1] * z + st.a[2] * z * z + st.a[3] * z * z * z;
                const double got = 20 * std::log10(std::abs(num / den));
                const double want = 20 * std::log10(std::abs(guitar::ToneStack::analog(f, knobs[0], knobs[1], knobs[2])));
                QVERIFY2(std::fabs(got - want) < 0.2, qPrintable(QStringLiteral("%1 Hz: %2 against %3").arg(f).arg(got).arg(want)));
            }
        }
    }

    void littleFoldsBack() {
        // A full-scale sine through the amp and the cab, which clip it to
        // a near square: what lands between its harmonics (aliases of those
        // past the inner rate's Nyquist) is under −55 dB at E4 and under
        // −45 dB two octaves up, at the four times oversampling it is
        // played at; and sixteen times oversampling leaves less again, so
        // what is measured is folding, not the window.
        for (const auto &c : {std::pair<float, double>{333.7f, -55.0}, {1334.7f, -45.0}}) {
            const double four = offHarmonic(throughAmp(c.first, 0.5f, 0.75, 4, true, 16384), c.first);
            QVERIFY2(four < c.second, qPrintable(QStringLiteral("%1 Hz: %2 dB").arg(c.first).arg(four)));
            if (c.first > 1000) {
                const double sixteen = offHarmonic(throughAmp(c.first, 0.5f, 0.75, 16, true, 16384), c.first);
                QVERIFY2(sixteen < four - 8, qPrintable(QStringLiteral("%1 against %2").arg(sixteen).arg(four)));
            }
        }
    }

    void heldItFeedsBack() {
        // A note held with the player close to the amp is kept ringing by its
        // own sound through the body: from three and a half seconds to six it
        // still swings, on average, within 25 dB of how it did at half a
        // second (the feedback comes and goes as the player sways), where on
        // its own it has fallen further; and it feeds back as a guitar does, on
        // the note, its octave or its twelfth (most of what the pickup hears
        // of the string is in its first three partials), not a whistle high
        // above it. A palm on the strings stops it.
        static guitar::Guitar g;
        g.init(kRate);
        bool finite = true;
        std::vector<float> string, heard; // what the pickup hears of it, and the mic
        auto swingAt = [&](float fb, float palm) {
            g.setTap(guitar::Guitar::Sound);
            g.silence();
            g.start(69);
            g.setPitch(440, 0);
            g.setFeedback(fb);
            g.pluck(0.6f, 0.5f);
            string.clear();
            heard.clear();
            double early = 0, late = 0;
            int lateN = 0;
            float v[32], mic[32];
            for (int b = 0; b < int(6.0 * kRate) / 32; ++b) {
                if (b == int(0.5 * kRate) / 32) {
                    early = g.swing();
                    g.setMute(palm);
                }
                std::fill(v, v + 32, 0.0f);
                g.setPitch(440, 0);
                g.pickup(v, 32, &g.rig());
                g.rig().process(v, mic, nullptr, 32, true);
                finite = finite && std::isfinite(mic[0]);
                string.insert(string.end(), v, v + 32);
                heard.insert(heard.end(), mic, mic + 32);
                if (b >= int(3.5 * kRate) / 32) {
                    late += g.swing();
                    ++lateN;
                }
            }
            return 10 * std::log10(late / lateN / early);
        };
        const double alone = swingAt(0.0f, 0), muted = swingAt(1.0f, 1), fed = swingAt(1.0f, 0);
        QVERIFY(finite);
        QVERIFY2(fed > -25, qPrintable(QString::number(fed)));
        QVERIFY2(alone < fed - 10, qPrintable(QStringLiteral("%1 %2").arg(alone).arg(fed)));
        QVERIFY2(muted < fed - 30, qPrintable(QStringLiteral("%1 %2").arg(muted).arg(fed)));
        // (At the pickup and at the mic: held still, with no vibrato to hide a
        // whistle, the amp's clipping keeps whatever is loudest at its input.)
        for (const auto *x : {&string, &heard}) {
            double low = 0, all = 0;
            for (int n = 1; n <= 24; ++n) {
                const double p = std::pow(10.0, levelAt(*x, 5.4, 0.5, n * 440.0 * std::sqrt(1 + g.B() * n * n)) / 10);
                all += p;
                low += n <= 3 ? p : 0;
            }
            QVERIFY2(low > 0.6 * all, qPrintable(QStringLiteral("%1: %2").arg(x == &string ? "pickup" : "mic").arg(low / all)));
        }
    }

    void theTouchIsHeard() {
        // At the mic, an A3 picked lightly and hard: the hard one is louder
        // while it sounds (by 2.5 dB and more) and holds up far longer (6 dB
        // and more a second on), where the light one cleans up and fades.
        static guitar::Guitar g;
        g.init(kRate);
        auto level = [&](float vel, double from, double to) {
            const auto x = pick(g, 57, vel, to + 0.05);
            double e = 1e-30;
            for (size_t i = size_t(from * kRate); i < size_t(to * kRate); ++i)
                e += double(x[i]) * x[i];
            return 10 * std::log10(e / ((to - from) * kRate));
        };
        const double sustained = level(0.95f, 0.3, 0.8) - level(0.15f, 0.3, 0.8);
        const double later = level(0.95f, 0.9, 1.1) - level(0.15f, 0.9, 1.1);
        QVERIFY2(sustained > 2.5, qPrintable(QString::number(sustained)));
        QVERIFY2(later > 5.0, qPrintable(QString::number(later)));
    }

    void theTouchIsTheTimbre() {
        // Into an amp this far gone, how hard the pick lands is heard less
        // as level than as timbre: a light stroke chimes (its own upper
        // partials come through), a hard one is clipped toward the note's
        // square, fatter. The spectral centroid moves by more than 8 %.
        static guitar::Guitar g;
        g.init(kRate);
        auto centroid = [&](float vel) {
            const auto x = pick(g, 69, vel, 0.6);
            double num = 0, den = 0;
            for (int n = 1; n <= 24; ++n) {
                const double f = n * 440.0 * std::sqrt(1 + g.B() * n * n), p = std::pow(10.0, levelAt(x, 0.1, 0.4, f) / 10);
                num += f * p;
                den += p;
            }
            return num / den;
        };
        const double light = centroid(0.2f), hard = centroid(1.0f);
        QVERIFY2(std::fabs(light - hard) > 0.08 * std::max(light, hard), qPrintable(QStringLiteral("%1 %2").arg(light).arg(hard)));
    }

    void nothingRunsAway() {
        // Picked as hard as it goes, anywhere, ten times a second, with
        // hammer-ons, pull-offs, bends up and down, the palm on and off and
        // full feedback: finite and bounded; then muted, it goes to sleep.
        static guitar::Guitar g;
        g.init(kRate);
        g.silence();
        g.start(64);
        g.setPitch(330, 0);
        g.setFeedback(1.5f);
        Rng rng(0x9e7u);
        float buf[32], top = 0;
        for (int b = 0; b < int(5.0 * kRate) / 32; ++b) {
            const float bend = 2.5f * std::sin(0.013f * float(b));
            g.setPitch(330.0f * std::exp2((bend + 12.0f * std::sin(0.002f * float(b))) / 12.0f), bend);
            g.setMute(b % 600 < 150 ? 1.0f : 0.0f);
            if (b % 150 == 0)
                g.pluck(1.0f, rng.unit());
            if (b % 150 == 50)
                g.hammer(1.0f);
            if (b % 150 == 100)
                g.pullOff(1.0f);
            std::fill(buf, buf + 32, 0.0f);
            g.render(buf, 32, 1.0f);
            for (float v : buf) {
                QVERIFY(std::isfinite(v));
                top = std::max(top, std::fabs(v));
            }
        }
        QVERIFY2(top < 3.0f, qPrintable(QString::number(top)));
        g.setFeedback(0);
        g.damp(0.12f);
        for (int b = 0; b < int(1.5 * kRate) / 32 && g.active(); ++b)
            g.render(buf, 32, 1.0f);
        QVERIFY(!g.active());
    }

    void theLeadPlaysItsGuitar() {
        // Chosen with → past the gongs, it says what it is for and saves by
        // its name; a note is heard, finite, under full scale and in tune.
        const int snd = guitarSound();
        QVERIFY(snd >= 0);
        testkit::reopen(onSound(snd, "end 0.5\n"), 0.3, [&](Session &, Session &opened, const QJsonObject &doc) {
            const auto settings = doc[QStringLiteral("parts")].toArray()[kLead].toObject()[QStringLiteral("settings")].toObject();
            QCOMPARE(settings[QStringLiteral("sound")].toString(), QStringLiteral("Guitar"));
            QVERIFY2(opened.pageStatus().contains(QStringLiteral("Guitar · rock")), qPrintable(opened.pageStatus()));
        });
        const auto take = testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=0.6\nup 1.6 1\nend 2.0\n", cellX(7))));
        QVERIFY(take.finite() && take.peak() < 1.0);
        QVERIFY2(take.rms(0.35, 1.5) > 0.02, qPrintable(QString::number(take.rms(0.35, 1.5))));
        const double got = peakHz(mono(take), 0.5, 0.5, 440.0, 50);
        QVERIFY2(std::fabs(cents(got, 440.0)) < 8, qPrintable(QString::number(got)));
        // Let go, the fretting hand mutes it within a few tenths of a second
        // (what is left is the echo's).
        const auto held = testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=0.6\nup 2.4 1\nend 2.5\n", cellX(7))));
        QVERIFY2(levelOf(take, 1.85, 2.0) < levelOf(held, 1.85, 2.0) - 15,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(take, 1.85, 2.0)).arg(levelOf(held, 1.85, 2.0))));
    }

    void aHammerOnLandsAtOnce() {
        // A second finger on the neck is a hammer-on: within a few
        // milliseconds the string sounds the new note (no glide), and with no
        // new pick it is no louder than the note it came from.
        const int snd = guitarSound();
        const auto take = testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=0.6\ndown 0.8 2 %.4f 0.5 p=0.6\n"
                                                         "up 1.3 2\nup 1.4 1\nend 1.5\n",
                                                         cellX(7), cellX(9))));
        const auto x = mono(take);
        const double c5 = 523.25;
        QVERIFY2(std::fabs(cents(peakHz(x, 0.82, 0.12, c5, 80), c5)) < 15, qPrintable(QString::number(peakHz(x, 0.82, 0.12, c5, 80))));
        QVERIFY2(levelOf(take, 0.8, 0.9) < levelOf(take, 0.65, 0.75) + 3,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(take, 0.8, 0.9)).arg(levelOf(take, 0.65, 0.75))));
    }

    void thePadReachesItsTouch() {
        // On the page, an A3 pressed lightly and hard as the finger lands:
        // the pad's whole touch is the pick's, so the hard one is still well
        // up a second on, where the light one has faded.
        const int snd = guitarSound();
        auto play = [&](double p) {
            return testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=%.2f\nup 2.0 1\nend 2.1\n", cellX(0), p)));
        };
        const auto light = play(0.15), hard = play(0.95);
        QVERIFY2(levelOf(hard, 1.2, 1.4) > levelOf(light, 1.2, 1.4) + 3,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(hard, 1.2, 1.4)).arg(levelOf(light, 1.2, 1.4))));
    }

    void aFlatFingerPalmMutes() {
        // A flat finger is a palm on the strings by the bridge: the string
        // dies within a few tenths of a second, a chug, where a fingertip's
        // note rings on.
        const int snd = guitarSound();
        auto play = [&](const char *size) {
            return testkit::play(onSound(snd, fmt("down 0.3 1 %.4f 0.5 p=0.5 %s\nup 1.5 1\nend 1.6\n", cellX(2), size)));
        };
        const auto tip = play(""), palm = play("size=2.5");
        QVERIFY(palm.finite());
        QVERIFY2(levelOf(palm, 1.0, 1.4) < levelOf(tip, 1.0, 1.4) - 15,
                 qPrintable(QStringLiteral("%1 %2").arg(levelOf(palm, 1.0, 1.4)).arg(levelOf(tip, 1.0, 1.4))));
    }

    void aGuitarStaysInsideBudget() {
        // One guitar picked hard and let ring, rendered against the clock: its
        // string, pickup and cab cost little; the amp at four times the rate
        // about two and a half per cent of real time.
        TENFOLD_TIMED();
        static guitar::Guitar g;
        g.init(kRate);
        g.silence();
        g.start(64);
        g.setPitch(330, 0);
        g.pluck(1.0f, 0.5f);
        float buf[32];
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < int(4.0 * kRate) / 32; ++b) {
            g.setPitch(330, 0);
            g.render(buf, 32, 1.0f);
        }
        const double share = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        QVERIFY2(share < 0.05, qPrintable(QString::number(share)));
    }

    void aRunStaysInsideBudget() {
        // A fast run of picked notes through the whole studio: each released
        // note is muted and sleeps, so a few guitars at most sound at once and
        // the studio keeps its time.
        TENFOLD_TIMED();
        std::string s = onSound(guitarSound(), "");
        for (int i = 0; i < 16; ++i)
            s += fmt("down %.3f %d %.4f 0.5 p=0.8\nup %.3f %d\n", 0.4 + 0.12 * i, i + 1, cellX((i * 5) % 15), 0.5 + 0.12 * i, i + 1);
        s += "end 3.0\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.maxVoices <= 5, qPrintable(QString::number(st.maxVoices)));
        QVERIFY2(st.p99Load < 0.3, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(GuitarTests)
#include "guitar_tests.moc"
