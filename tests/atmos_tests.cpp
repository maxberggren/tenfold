// The Atmos page: its engine on its own (in the chord, following it, the
// voicing, the envelope, freeze, the gate, the budget) and the page through
// the whole session (latching, scenes, loops, recording, leaving).

#include <QJsonArray>
#include <array>
#include <chrono>
#include <complex>

#include "parts.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr int kAtmos = 5; // the sixth page

// The loud moments of a stretch: the 90th percentile of its 50 ms levels,
// in dB. For a sustained bed it is about its rms; for stabs it is the stabs.
double loudness(const std::vector<float> &l, const std::vector<float> &r, double from, double to) {
    std::vector<double> lv;
    for (double t = from; t + 0.05 <= to; t += 0.05) {
        double e = 0;
        const size_t a = size_t(t * 48000), z = size_t((t + 0.05) * 48000);
        for (size_t i = a; i < z; ++i)
            e += double(l[i]) * l[i] + double(r[i]) * r[i];
        lv.push_back(10 * std::log10(e / double(2 * (z - a)) + 1e-18));
    }
    std::sort(lv.begin(), lv.end());
    return lv[lv.size() * 9 / 10];
}

// The balance of a stretch, third-octave by third-octave from 100 Hz to
// 12.8 kHz, in dB against the whole: power spectra of 8192-sample Hann
// frames, half overlapping, averaged and summed into the bands.
constexpr int kThirds = 21;
std::array<double, kThirds> thirds(const std::vector<float> &l, const std::vector<float> &r, double from, double to) {
    constexpr size_t N = 8192;
    std::vector<double> power(N / 2, 0.0);
    std::vector<std::complex<double>> a(N);
    for (size_t at = size_t(from * 48000); at + N <= size_t(to * 48000); at += N / 2) {
        for (size_t i = 0; i < N; ++i)
            a[i] = 0.5 * (l[at + i] + r[at + i]) * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(N)));
        for (size_t i = 1, j = 0; i < N; ++i) {
            size_t bit = N >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(a[i], a[j]);
        }
        for (size_t len = 2; len <= N; len <<= 1) {
            const std::complex<double> wl = std::polar(1.0, -2 * M_PI / double(len));
            for (size_t i = 0; i < N; i += len) {
                std::complex<double> w(1);
                for (size_t j = 0; j < len / 2; ++j) {
                    const auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        for (size_t i = 0; i < N / 2; ++i)
            power[i] += std::norm(a[i]);
    }
    std::array<double, kThirds> bands{};
    double all = 0;
    for (size_t i = 1; i < N / 2; ++i) {
        const double hz = double(i) * 48000.0 / double(N);
        const int k = int(std::floor(3.0 * std::log2(hz / 100.0)));
        if (k >= 0 && k < kThirds) {
            bands[size_t(k)] += power[i];
            all += power[i];
        }
    }
    for (double &e : bands)
        e = 10 * std::log10(e / all + 1e-12);
    return bands;
}

// The engine alone, with a clock and a key of its own.
struct Bench : AudioContext {
    PartSpec spec = makeAtmos();
    PartEngine &engine = *spec.engine;
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    std::vector<float> left, right;

    Bench() {
        tr.rate = 48000;
        tr.bpm = 124;
        tr.beatsPerSample = tr.bpm / 60.0 / tr.rate;
        h.tuning.set(9, 0, 4, false); // A minor
        h.chordRoot = 0;
        h.chordCount = 3;
        engine.init(48000);
    }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return double(tr.sample) / 48000.0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }

    void on(int id, int family, float height, float colour = 0.3f, float press = 0.5f, float motion = 0,
            float attack = 0.05f, int style = 0) {
        Command c;
        c.type = Command::On;
        c.id = id;
        c.i[0] = family;
        c.i[1] = style;
        c.f[0] = height;
        c.f[1] = colour;
        c.f[2] = press;
        c.f[3] = motion;
        c.f[4] = 0.8f;
        c.f[5] = attack;
        engine.command(c, *this);
    }
    void off(int id) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        engine.command(c, *this);
    }
    void param(int which, float v) {
        Command c;
        c.type = Command::Param;
        c.i[0] = which;
        c.f[0] = v;
        engine.command(c, *this);
    }
    void run(double seconds) {
        const int blocks = int(seconds * 48000 / kBlock);
        float l[kBlock], r[kBlock];
        for (int b = 0; b < blocks; ++b) {
            std::fill(l, l + kBlock, 0.0f);
            std::fill(r, r + kBlock, 0.0f);
            engine.render(l, r, kBlock, *this);
            left.insert(left.end(), l, l + kBlock);
            right.insert(right.end(), r, r + kBlock);
            tr.sample += kBlock;
            tr.beat += kBlock * tr.beatsPerSample;
        }
    }
    double rms(double from, double to) const {
        const size_t a = size_t(from * 48000), b = std::min(left.size(), size_t(to * 48000));
        double s = 0;
        for (size_t i = a; i < b; ++i)
            s += double(left[i]) * left[i] + double(right[i]) * right[i];
        return b > a ? std::sqrt(s / double(2 * (b - a))) : 0;
    }
    // Energy at one frequency between from and to: a Hann-windowed Goertzel.
    double energyAt(double hz, double from, double to, bool window = true) const {
        const size_t a = size_t(from * 48000), b = std::min(left.size(), size_t(to * 48000));
        const double w = 2 * M_PI * hz / 48000.0, cw = std::cos(w);
        double s1 = 0, s2 = 0;
        for (size_t i = a; i < b; ++i) {
            const double win = window ? 0.5 - 0.5 * std::cos(2 * M_PI * double(i - a) / double(b - a)) : 1.0;
            const double x = 0.5 * (left[i] + right[i]) * win + s1 * 2 * cw - s2;
            s2 = s1;
            s1 = x;
        }
        return s1 * s1 + s2 * s2 - 2 * cw * s1 * s2;
    }
    // Energy at each pitch class (C = 0), measured on every semitone from E2
    // to C7.
    std::array<double, 12> chroma(double from, double to) const {
        std::array<double, 12> pc{};
        for (int m = 40; m <= 96; ++m)
            pc[size_t(m % 12)] += energyAt(440.0 * std::exp2((m - 69) / 12.0), from, to);
        return pc;
    }
    // The share of the pitched energy that falls on these pitch classes.
    double share(double from, double to, std::initializer_list<int> pcs) const {
        const auto c = chroma(from, to);
        double in = 0, all = 0;
        for (double v : c)
            all += v;
        for (int k : pcs)
            in += c[size_t(k)];
        return all > 0 ? in / all : 0;
    }
    // Energy-weighted mean pitch, in MIDI.
    double meanPitch(double from, double to) const {
        double num = 0, den = 0;
        for (int m = 40; m <= 100; ++m) {
            const double e = energyAt(440.0 * std::exp2((m - 69) / 12.0), from, to);
            num += e * m;
            den += e;
        }
        return den > 0 ? num / den : 0;
    }
};

enum { Drone, Pad, Choir, Glass, Air };
enum { Classic, Neon, Dub, Tape, kStyles };
constexpr int kScenes = 10;
const char *const kSceneKeys[kScenes] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
const char *const kSceneNames[kScenes] = {"Dawn", "Ocean", "Cathedral", "Stardust", "Nostalgia",
                                          "Euphoria", "Night rain", "Pulse", "Neon", "Dub"};
enum { PFreeze = 0, PCloud, PPump, PSpace, PSwell, PChord, PChordToLoop, PTone };
constexpr int kPubTone = 19;
constexpr int kPubOwnLoop = 16, kPubOwnHand = 17;

// A part's settings in a song file, found by the part's name.
QJsonObject settingsOf(const QJsonObject &doc, const QString &name = QStringLiteral("Atmos")) {
    for (const QJsonValue &p : doc[QStringLiteral("parts")].toArray())
        if (p.toObject()[QStringLiteral("name")].toString() == name)
            return p.toObject()[QStringLiteral("settings")].toObject();
    return {};
}
constexpr int A = 9, C = 0, D = 2, E = 4, F = 5, G = 7, B = 11;

}

class AtmosTests : public QObject {
    Q_OBJECT
    // Times the studio against the clock: bin/test runs it with the machine to itself.
    Q_CLASSINFO("alone", "tenLayersStayClearOfTheSubAndInBudget")
private slots:
    void oneClickOverALatchedBedLoopsOneBar() {
        // An old swell held and let go, three layers latched, then a while
        // later one chord change (N) and Space: the loop is the bar with the
        // change, the bed carried in as it sounds. The latched bed doesn't
        // make the playing reach back to the swell (it was four bars, with
        // the old swell coming back every pass).
        const std::string bed = "page 0.05 6\nkey 0.1 Ctrl+M\n"
                                "down 1.0 1 0.3 0.5 p=0.6\nup 5.0 1\n"
                                "down 6.0 2 0.06 0.3 p=0.6\ndown 6.1 3 0.25 0.4 p=0.6\ndown 6.2 4 0.45 0.4 p=0.6\n"
                                "click 6.5\nup 7.0 2\nup 7.0 3\nup 7.0 4\n"
                                "key 11.0 N\nkey 13.4 Space\n";
        const auto take = testkit::play(bed + "end 16\n");
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.parts[5].playing, 0);
        QCOMPARE(int(take.view.parts[5].clips[0].lengthBeats), 4);
        // Two more changes layered on it: the layer spans the loop rather
        // than repeating inside it, so the loop stays one bar.
        const auto layered = testkit::play(bed + "key 16.0 X\nkey 17.5 C\nkey 19.3 Space\nend 22\n");
        QCOMPARE(layered.view.parts[5].playing, 0);
        QCOMPARE(int(layered.view.parts[5].clips[0].lengthBeats), 4);
        QVERIFY(layered.view.parts[5].clips[1].state == ClipView::Empty);
    }

    void everyLayerIsInTheChord() {
        // A minor, the tonic chord: whatever the layer and its style, nearly
        // all its pitched energy is A, C or E (the rest is upper harmonics),
        // Neon's chorus and Dub's echoes included; Dub's chord is the minor
        // ninth, A C E G B, and nothing else.
        for (int style = Classic; style < kStyles; ++style)
            for (int family : {Drone, Pad, Choir, Glass})
                for (float colour : {0.1f, 0.9f}) {
                    Bench b;
                    b.on(1, family, 0.5f, colour, 0.5f, 0, 0.05f, style);
                    b.run(3);
                    const bool ninth = style == Dub && family == Pad;
                    const double s = ninth ? b.share(1, 3, {A, C, E, G, B}) : b.share(1, 3, {A, C, E});
                    if (ninth) {
                        // The seventh and the ninth are really there.
                        QVERIFY(b.share(1, 3, {G}) > 0.05);
                        QVERIFY(b.share(1, 3, {B}) > 0.05);
                    }
                    QVERIFY2(s > 0.85, qPrintable(QStringLiteral("style %1 family %2 colour %3: %4")
                                                      .arg(style).arg(family).arg(colour).arg(s)));
                }
    }

    void theDroneIsRootAndFifth() {
        Bench b;
        b.on(1, Drone, 0.3f);
        b.run(3);
        QVERIFY(b.share(1, 3, {A, E}) > 0.95);
    }

    void followsTheChordOfTheMoment() {
        // In every style: Dub's echoes of the old chord go quickly on a change.
        for (int style = Classic; style < kStyles; ++style) {
            Bench b;
            b.on(1, Pad, 0.5f, 0.3f, 0.5f, 0, 0.05f, style);
            b.h.chordRoot = 5; // VI: F A C
            b.h.chordTones[0] = 5, b.h.chordTones[1] = 7, b.h.chordTones[2] = 9;
            ++b.h.chordSeq;
            b.run(3);
            // (Dub's chords are ninths, in the key: VI's is F A C E G, VII's G B D F A.)
            const bool ninth = style == Dub;
            QVERIFY2(b.share(1.5, 3, ninth ? std::initializer_list<int>{F, A, C, E, G} : std::initializer_list<int>{F, A, C}) > 0.85,
                     qPrintable(QString::number(style)));
            b.h.chordRoot = 6; // VII: G B D, nothing in common
            b.h.chordTones[0] = 6, b.h.chordTones[1] = 8, b.h.chordTones[2] = 10;
            ++b.h.chordSeq;
            b.run(3);
            const double s = b.share(4.5, 6, ninth ? std::initializer_list<int>{G, B, D, F, A} : std::initializer_list<int>{G, B, D});
            QVERIFY2(s > 0.85, qPrintable(QStringLiteral("style %1: %2").arg(style).arg(s)));
            // The change is a crossfade, not a cut: no gap in the middle.
            QVERIFY(b.rms(3.0, 3.5) > 0.5 * b.rms(2.0, 3.0));
        }
    }

    void followsAKeyChange() {
        Bench b;
        b.h.tuning.set(2, 0, 4, false); // D minor
        b.on(1, Choir, 0.5f, 0.5f);
        b.run(3);
        QVERIFY(b.share(1, 3, {D, F, A}) > 0.85);
    }

    void ownChordsSoftenTheDiminished() {
        // On II of A minor the triad would be B D F, diminished; the bed
        // plays B D G instead, a first-inversion G.
        Bench b;
        b.param(PChord, 1);
        b.on(1, Pad, 0.5f);
        b.run(3);
        const auto c = b.chroma(1, 3);
        double all = 0;
        for (double v : c)
            all += v;
        QVERIFY(c[F] / all < 0.03);
        QVERIFY((c[B] + c[D] + c[G]) / all > 0.85);
        // And back to following the chords page.
        b.param(PChord, -1);
        b.run(3);
        QVERIFY(b.share(4.5, 6, {A, C, E}) > 0.85);
    }

    void theVoicingClimbsWithHeight() {
        Bench low, high;
        low.on(1, Pad, 0.0f);
        high.on(1, Pad, 1.0f);
        low.run(2);
        high.run(2);
        QVERIFY(high.meanPitch(1, 2) - low.meanPitch(1, 2) > 15);
    }

    void aLiftFadesAndTheVoiceEnds() {
        Bench b;
        b.on(1, Pad, 0.5f);
        b.run(2);
        const double held = b.rms(1, 2);
        b.off(1);
        b.run(1);
        QVERIFY(b.rms(2.0, 2.3) > 0.4 * held); // a long tail, no cut
        b.run(14);
        QCOMPARE(b.engine.voices(), 0);
        QVERIFY(b.rms(16, 17) < held * 0.001);
    }

    void aTapIsAWholeBreath() {
        Bench b;
        b.on(1, Pad, 0.5f, 0.3f, 0.5f, 0, 1.0f); // a one-second swell
        b.run(0.05);
        b.off(1);
        b.run(3);
        // The swell carried on after the finger left.
        QVERIFY(b.rms(0.8, 1.1) > 4 * b.rms(0.0, 0.1));
    }

    void freezeHoldsTheBedThroughChordChanges() {
        Bench b;
        b.on(1, Pad, 0.5f);
        b.on(2, Choir, 0.5f, 0.5f);
        b.run(2);
        const double held = b.rms(1, 2);
        b.param(PFreeze, 1);
        b.run(0.3);
        b.off(1);
        b.off(2);
        b.run(3.7);
        // The layers hang at their level after the fingers left.
        QCOMPARE(b.engine.voices(), 2);
        QVERIFY(std::fabs(20 * std::log10(b.rms(4, 6) / held)) < 2.0);
        // The chord moves on: the frozen bed goes with it, as loud as before.
        b.h.chordRoot = 5; // VI: F A C
        b.h.chordTones[0] = 5, b.h.chordTones[1] = 7, b.h.chordTones[2] = 9;
        ++b.h.chordSeq;
        b.run(4);
        QVERIFY(std::fabs(20 * std::log10(b.rms(8, 10) / held)) < 2.0);
        QVERIFY(b.share(8, 10, {F, A, C}) > 0.85);
        // A thaw lets them go.
        b.param(PFreeze, 0);
        b.run(20);
        QCOMPARE(b.engine.voices(), 0);
        QVERIFY(b.rms(28, 30) < 0.01 * held);
    }

    void colourAndHeightChangeTheSoundNotTheLevel() {
        // At one pressure, every colour and height of a column within 3 dB
        // in its loud moments, in every style; and a style's layers as loud
        // as the classic ones.
        double classic = 0;
        for (int style = Classic; style < kStyles; ++style) {
            double all = 0;
            for (int family : {Drone, Pad, Choir, Glass, Air}) {
                double lo = 1e9, hi = 0;
                for (float c : {0.1f, 0.5f, 0.9f})
                    for (float y : {0.1f, 0.5f, 0.9f}) {
                        Bench b;
                        b.param(PSpace, 0); // the dry layer, not the room
                        b.on(1, family, y, c, 0.6f, 0, 0.05f, style);
                        b.run(4);
                        QVERIFY(std::isfinite(b.rms(2, 4)));
                        // (a stab is measured by its loud moments, a sustained layer by its rms)
                        const double r = style == Dub && family == Pad ? std::pow(10.0, loudness(b.left, b.right, 2, 4) / 20) : b.rms(2, 4);
                        lo = std::min(lo, r);
                        hi = std::max(hi, r);
                        all += r * r;
                    }
                QVERIFY2(20 * std::log10(hi / lo) < 3.0, qPrintable(QStringLiteral("style %1 family %2: %3 dB")
                                                                       .arg(style).arg(family).arg(20 * std::log10(hi / lo))));
            }
            if (style == Classic)
                classic = all;
            const double db = 10 * std::log10(all / classic);
            QVERIFY2(std::fabs(db) < 2.0, qPrintable(QStringLiteral("style %1: %2 dB against classic").arg(style).arg(db)));
        }
    }

    void theChoirSingsTheChordNotOnePartial() {
        // As the player hears it, through the room: whatever the vowel, no
        // pitch class takes much more than half (a pad there gives 45%).
        for (float c : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float x = (2 + 0.08f + 0.84f * c) / 5;
            const auto take = testkit::play("page 0 6\ndown 0.2 1 " + std::to_string(x) + " 0.5 p=0.6\nend 3.5\n");
            Bench b; // for its analysis
            b.left = take.left;
            b.right = take.right;
            const auto ch = b.chroma(1.5, 3.5);
            double all = 0, top = 0;
            for (double v : ch) {
                all += v;
                top = std::max(top, v);
            }
            QVERIFY2(top / all < 0.52, qPrintable(QStringLiteral("colour %1: %2").arg(c).arg(top / all)));
        }
    }

    void aHeldSwellOpensTheBed() {
        Bench b;
        b.on(1, Pad, 0.4f);
        b.on(2, Choir, 0.4f, 0.5f);
        b.run(3);
        b.param(PSwell, 1);
        b.run(60.0 / 124.0 * 16); // four bars held
        const double t1 = b.left.size() / 48000.0;
        auto centroid = [&](double a, double z) {
            double num = 0, den = 0;
            for (double hz = 50; hz < 12000; hz += 10) {
                const double e = b.energyAt(hz, a, z);
                num += e * hz;
                den += e;
            }
            return num / den;
        };
        auto above = [&](double a, double z) {
            double e = 0;
            for (double hz = 2000; hz < 12000; hz *= 1.02)
                e += b.energyAt(hz, a, z);
            return e;
        };
        const double c0 = centroid(1.5, 3), c1 = centroid(t1 - 1.5, t1);
        const double up = 10 * std::log10(above(t1 - 1.5, t1) / above(1.5, 3));
        QVERIFY2(c1 > 1.8 * c0, qPrintable(QStringLiteral("%1 -> %2 Hz").arg(c0).arg(c1)));
        QVERIFY2(up > 6, qPrintable(QString::number(up)));
    }

    void theToneDarkensAndBrightens() {
        // The same bed at the tone's three places: - closes a low-pass over
        // it, = lifts its top, and the middle is the bed as it was built.
        auto above = [](float tone) {
            Bench b;
            b.param(PTone, tone);
            b.on(1, Pad, 0.4f);
            b.on(2, Choir, 0.4f, 0.5f);
            b.run(3);
            double e = 0;
            for (double hz = 2000; hz < 12000; hz *= 1.02)
                e += b.energyAt(hz, 1.5, 3);
            return e;
        };
        const double mid = above(0.5f);
        const double dark = 10 * std::log10(above(0.0f) / mid), bright = 10 * std::log10(above(1.0f) / mid);
        QVERIFY2(dark < -10, qPrintable(QString::number(dark)));
        QVERIFY2(bright > 2, qPrintable(QString::number(bright)));
    }

    void theLowEndIsMono() {
        Bench b;
        b.on(1, Drone, 0.1f, 0.5f, 0.7f);
        b.on(2, Pad, 0.0f, 0.5f, 0.7f);
        b.run(4);
        // Correlation of left and right in the band under 150 Hz.
        std::vector<double> l, r;
        Svf fl, fr, fl2, fr2;
        fl.set(130, 0.54f, 48000);
        fr.set(130, 0.54f, 48000);
        fl2.set(130, 1.31f, 48000);
        fr2.set(130, 1.31f, 48000);
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < b.left.size(); ++i) {
            const double a = fl2.step(fl.step(b.left[i]).low).low, c = fr2.step(fr.step(b.right[i]).low).low;
            if (i < 96000)
                continue;
            lr += a * c;
            ll += a * a;
            rr += c * c;
        }
        QVERIFY2(lr / std::sqrt(ll * rr) > 0.8, qPrintable(QString::number(lr / std::sqrt(ll * rr))));
    }

    void anticlockwiseGatesOnTheSixteenths() {
        Bench b;
        b.on(1, Pad, 0.5f, 0.3f, 0.6f, -1.0f);
        b.run(4);
        // The level of each sixteenth's first half against its second.
        const double step = 60.0 / 124.0 / 4.0;
        double on = 0, offv = 0;
        for (double s0 = std::ceil(1.0 / step) * step; s0 + step < 4.0; s0 += step) {
            on += b.rms(s0 + 0.05 * step, s0 + 0.45 * step);
            offv += b.rms(s0 + 0.62 * step, s0 + 0.95 * step);
        }
        QVERIFY2(on > 3 * offv, qPrintable(QString::number(on / offv)));
    }

    void tenLayersStayClearOfTheSubAndInBudget() {
        Bench b;
        for (int i = 0; i < 10; ++i)
            b.on(i + 1, i / 2, 0.1f * float(i % 5), 0.25f * float(i % 4), 0.7f, i % 3 == 0 ? 0.6f : 0.0f);
        b.param(PCloud, 1);
        b.run(1);
        const auto t0 = std::chrono::steady_clock::now();
        b.run(4);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        // Ten layers, the cloud and the halo in well under a tenth of the
        // time they play for (the budget is six percent of the callback;
        // this is a loose guard against a slip, not the measurement).
        QVERIFY2(took / 4.0 < 0.1, qPrintable(QString::number(took / 4.0)));
        // Hardly anything under 60 Hz.
        double sub = 0, all = 0;
        for (double hz = 20; hz < 4000; hz *= 1.03) {
            const double e = b.energyAt(hz, 2, 4);
            all += e;
            if (hz < 60)
                sub += e;
        }
        QVERIFY2(sub / all < 0.005, qPrintable(QString::number(sub / all)));
        QCOMPARE(b.engine.voices(), 10);
        QVERIFY(std::isfinite(b.rms(1, 5)));
    }

    void aDubChordStabsOnTheOffbeats() {
        // The Dub pad, dry (no room), is struck on the offbeat eighths: just
        // after each "and" it is far louder than just before it.
        Bench b;
        b.param(PSpace, 0);
        b.on(1, Pad, 0.4f, 0.8f, 0.6f, 0, 0.05f, Dub);
        b.run(6);
        const double beat = 60.0 / 124.0;
        double after = 0, before = 0;
        for (double t = std::ceil(2.0 / beat) * beat + 0.5 * beat; t + beat < 6; t += beat) {
            after += b.rms(t + 0.01, t + 0.08);
            before += b.rms(t - 0.08, t - 0.01);
        }
        QVERIFY2(after > 2.0 * before, qPrintable(QString::number(after / before)));
        // Twisted anticlockwise it rolls 3-3-2, never on the beat: a hit on
        // the bar's sixth sixteenth, where the offbeats have none, and none on
        // the downbeat.
        Bench r;
        r.param(PSpace, 0);
        r.on(1, Pad, 0.4f, 0.8f, 0.6f, -1.0f, 0.05f, Dub);
        r.run(6);
        const double bar = 4 * beat;
        double rolled = 0, plain = 0;
        double down = 0;
        for (double t = std::ceil(2.0 / bar) * bar; t + bar < 6; t += bar) {
            const double t5 = t + 5 * beat / 4;
            rolled += r.rms(t5 + 0.005, t5 + 0.05) / r.rms(t5 - 0.05, t5 - 0.005);
            plain += b.rms(t5 + 0.005, t5 + 0.05) / b.rms(t5 - 0.05, t5 - 0.005);
            down += r.rms(t + 0.005, t + 0.05) / r.rms(t - 0.05, t - 0.005);
        }
        QVERIFY2(rolled > 2 * plain, qPrintable(QStringLiteral("%1 against %2").arg(rolled).arg(plain)));
        // (only the echo of the bar's last stab lands there, softer than a hit)
        QVERIFY2(down < 0.6 * rolled, qPrintable(QStringLiteral("%1 against %2").arg(down).arg(rolled)));
    }

    void dubEchoesAndNeonIsItsOwnPad() {
        // Dub's stab rings on in its echo after the layer has gone; Neon's
        // pad, at either end of its colour, is none of the classic pad's
        // sounds: its spectrum, third-octave by third-octave, is far from
        // each of them (strings, saw and hollow).
        Bench d;
        d.param(PSpace, 0);
        d.on(1, Pad, 0.4f, 0.9f, 0.6f, 0, 0.05f, Dub);
        d.run(3);
        d.off(1);
        d.run(3);
        QVERIFY2(d.rms(4.0, 4.5) > 0.05 * d.rms(1, 3), qPrintable(QString::number(d.rms(4.0, 4.5) / d.rms(1, 3))));
        auto spectrum = [](int style, float colour) {
            Bench b;
            b.param(PSpace, 0);
            b.on(1, Pad, 0.5f, colour, 0.6f, 0, 0.05f, style);
            b.run(3);
            std::vector<double> bands;
            double all = 0;
            for (double lo = 200; lo < 8000; lo *= std::exp2(1.0 / 3.0)) {
                double e = 0;
                for (double hz = lo; hz < lo * std::exp2(1.0 / 3.0); hz *= 1.01)
                    e += b.energyAt(hz, 1, 3);
                bands.push_back(e);
                all += e;
            }
            for (double &e : bands)
                e = 10 * std::log10(e / all + 1e-12);
            return bands;
        };
        // And so is Tape's felt-keys pad, through its cassette.
        for (int style : {Neon, Tape})
            for (float colour : {0.05f, 0.95f})
                for (float classic : {0.05f, 0.5f, 0.95f}) {
                    const auto c = spectrum(Classic, classic), n = spectrum(style, colour);
                    double d = 0;
                    for (size_t k = 0; k < c.size(); ++k)
                        d += (c[k] - n[k]) * (c[k] - n[k]) / double(c.size());
                    QVERIFY2(std::sqrt(d) > 2.5, qPrintable(QStringLiteral("style %1 colour %2 against classic %3: %4 dB")
                                                                .arg(style).arg(colour).arg(classic).arg(std::sqrt(d))));
                }
    }

    void tapeWobblesButStaysInTune() {
        // A Tape layer's pitch drifts with the wow but, at the default space,
        // by under ten cents: its energy stays on the chord's pitches,
        // measured as narrowly as a tenth of a semitone either side. With
        // the space all the way up the tape is seasick, the wow three times
        // as deep: far more of the energy wanders off the pitch.
        auto steadiness = [](float space) {
            Bench b;
            b.param(PSpace, space);
            b.on(1, Glass, 0.3f, 0.05f, 0.6f, 0, 0.05f, Tape);
            b.run(5);
            double on = 0, near = 0;
            for (int m = 67; m <= 96; ++m) {
                if (m % 12 != A && m % 12 != C && m % 12 != E)
                    continue;
                for (double c = -0.5; c <= 0.5; c += 0.05) {
                    const double e = b.energyAt(440.0 * std::exp2((m + c - 69) / 12.0), 1, 5);
                    (std::fabs(c) <= 0.1 ? on : near) += e;
                }
            }
            return on / near;
        };
        const double held = steadiness(0.5f), seasick = steadiness(1.0f);
        QVERIFY2(held > 2, qPrintable(QString::number(held)));
        QVERIFY2(seasick < 0.7 * held, qPrintable(QStringLiteral("%1 against %2").arg(seasick).arg(held)));
    }

    void tapeKeysAreStruck() {
        // Tape's pad is felt keys: a hammer as the layer lands and again as
        // each chord comes, at least 7.5 dB over the held note, then down to
        // it within a second.
        Bench b;
        b.param(PSpace, 0);
        b.on(1, Pad, 0.4f, 0.5f, 0.6f, 0, 0.02f, Tape);
        b.run(3);
        b.h.chordRoot = 5; // VI
        b.h.chordTones[0] = 5, b.h.chordTones[1] = 7, b.h.chordTones[2] = 9;
        ++b.h.chordSeq;
        b.run(3);
        const double held = b.rms(2.0, 2.9), heldAfter = b.rms(5.0, 6.0);
        const double landed = 20 * std::log10(b.rms(0.04, 0.09) / held);
        const double changed = 20 * std::log10(b.rms(3.01, 3.06) / heldAfter);
        QVERIFY2(landed > 7.5, qPrintable(QString::number(landed)));
        QVERIFY2(changed > 7.5, qPrintable(QString::number(changed)));
        QVERIFY2(20 * std::log10(b.rms(3.9, 4.4) / heldAfter) < 2.0, qPrintable(QString::number(b.rms(3.9, 4.4) / heldAfter)));
        QVERIFY(std::fabs(20 * std::log10(heldAfter / held)) < 1.5);
        // Twisted anticlockwise the hand plays the held chord again, on the
        // bar's first, seventh and thirteenth sixteenths: a strike on the
        // "and" of two, where an untwisted layer only holds.
        Bench r;
        r.param(PSpace, 0);
        r.on(1, Pad, 0.4f, 0.5f, 0.6f, -1.0f, 0.02f, Tape);
        r.run(10);
        const double beat = 60.0 / 124.0, bar = 4 * beat;
        double rolled = 0;
        int bars = 0;
        for (double t = std::ceil(1.0 / bar) * bar + 1.5 * beat; t + 0.1 < 10; t += bar, ++bars)
            rolled += r.rms(t + 0.005, t + 0.05) / r.rms(t - 0.05, t - 0.005);
        QVERIFY2(rolled > 1.8 * bars, qPrintable(QString::number(rolled / bars)));
    }

    void tenStyledLayersStayInBudget() {
        Bench b;
        for (int i = 0; i < 10; ++i)
            b.on(i + 1, i % 5, 0.1f * float(i % 5), 0.25f * float(i % 4), 0.7f, i % 3 == 0 ? 0.6f : 0.0f, 0.05f,
                 1 + i % 3);
        b.param(PCloud, 1);
        b.run(1);
        const auto t0 = std::chrono::steady_clock::now();
        b.run(4);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        QVERIFY2(took / 4.0 < 0.1, qPrintable(QString::number(took / 4.0)));
        QVERIFY(std::isfinite(b.rms(1, 5)));
        QVERIFY(b.rms(1, 5) > 0.02);
    }

    // --- through the session ---------------------------------------------------

    void oneFingerIsInstantlyMusical() {
        const auto take = testkit::play("page 0 6\ndown 0.2 1 0.3 0.5 p=0.8\nup 3 1\nend 4\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.rms(1, 3) > 0.03);
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void aClickLatchesAndATapOnTheOrbLetsGo() {
        const auto take = testkit::play("page 0 6\n"
                                        "down 0.2 1 0.3 0.5 p=0.7\nclick 1.0\nup 2.0 1\n"
                                        "tap 8.0 0.31 0.5\n"
                                        "end 18\n");
        // Still sounding long after the lift, until its orb is tapped.
        QVERIFY(take.rms(6, 8) > 0.02);
        QVERIFY(take.rms(17, 18) < 0.05 * take.rms(6, 8));
        QCOMPARE(take.view.parts[kAtmos].voices, 0);
    }

    void aSceneLaysABedAndTheSameKeyFadesIt() {
        // W, and the new ones on O and P, whose echo fades with the rest.
        for (const char *k : {"W", "O", "P"}) {
            const auto take = testkit::play(std::string("page 0 6\nkey 0.2 ") + k + "\nkey 6 " + k + "\nend 24\n");
            QVERIFY2(take.rms(4, 6) > 0.03, k);
            QVERIFY2(take.rms(23, 24) < 0.05 * take.rms(4, 6), k);
            QCOMPARE(take.view.parts[kAtmos].voices, 0);
        }
    }

    void keysSetTheBedsState() {
        const auto take = testkit::play("page 0 6\nkey 0.2 F\nkey 0.3 G\nkey 0.4 ]\nkey 0.5 Up\nkey 0.6 V\nend 1\n");
        const float *d = take.view.parts[kAtmos].data;
        QCOMPARE(d[0], 1.0f);   // frozen
        QCOMPARE(d[1], 1.0f);   // cloud
        QCOMPARE(int(d[2]), 2); // pump on every beat
        QCOMPARE(d[3], 0.75f);  // more space
        QCOMPARE(int(d[5]), 3); // the bed's own chord on IV
        QCOMPARE(int(d[6]), 3); // and it plays it
    }

    void aClickAddsALayerToTheLatchedBed() {
        // A bed of three latched layers; a glass finger lands and clicks at
        // once: it joins the bed, nothing drops.
        const auto take = testkit::play("page 0 6\n"
                                        "down 0.2 1 0.06 0.3 p=0.6\ndown 0.3 2 0.25 0.4 p=0.6\ndown 0.4 3 0.45 0.4 p=0.6\n"
                                        "click 2.0\nup 2.5 1\nup 2.5 2\nup 2.5 3\n"
                                        "down 5.0 4 0.70 0.5 p=0.7\nclick 5.15\nup 6.0 4\nend 14\n");
        QVERIFY(take.rms(12, 14) > 0.7 * take.rms(3, 5));
        QCOMPARE(take.view.parts[kAtmos].voices, 4);
    }

    void aLongClickLetsEverythingGo() {
        const auto take = testkit::play("page 0 6\n"
                                        "down 0.2 1 0.3 0.5 p=0.7\nclick 1.0\nup 2.0 1\n"
                                        "down 6.0 2 0.6 0.5 p=0.7\nclick 6.1 len=1.0\nup 7.3 2\nend 20\n");
        QVERIFY(take.rms(4, 6) > 0.02);
        QVERIFY(take.rms(19, 20) < 0.05 * take.rms(4, 6));
        QCOMPARE(take.view.parts[kAtmos].voices, 0);
    }

    void spaceLoopsTheLatchedBedWithoutADropOrADouble() {
        // Over a playing loop (the metronome on), a latched scene, then
        // Space: the bars just played become the loop, the bed in them. The
        // hand's layers hand over, so Delete afterwards changes nothing.
        const auto take = testkit::play("page 0 6\nkey 0.05 Ctrl+M\nkey 0.2 R\nkey 3.5 Space\nkey 8 Delete\nend 20\n");
        const PartView &v = take.view.parts[kAtmos];
        QCOMPARE(v.playing, 0);
        // Every layer starts the loop and ends it: three layers.
        QVERIFY(v.clips[0].events >= 6);
        const double before = take.rms(2.5, 3.5), after = take.rms(10, 20);
        QVERIFY2(std::fabs(20 * std::log10(after / before)) < 1.5,
                 qPrintable(QString::number(20 * std::log10(after / before))));
        QVERIFY(v.voices <= 3);
    }

    void aBedLoopedWithSpaceHoldsAcrossTheSeam() {
        // Layers latched inside the take (s6/s7 from the critic): the loop
        // holds them from its first moment, round after round.
        for (const char *script :
             {"page 0.05 6\nkey 0.1 Ctrl+M\ndown 15.784 1 0.06 0.25 p=0.6\ndown 15.884 2 0.25 0.4 p=0.6\n"
              "down 15.984 3 0.45 0.4 p=0.5\nclick 16.484\nup 16.984 1\nup 16.984 2\nup 16.984 3\n"
              "down 19.355 4 0.70 0.5 p=0.7\nup 20.516 4\nkey 21.677 Space\nkey 26.129 Delete\nend 46.452\n",
              "page 0.05 6\nkey 0.1 Ctrl+M\ndown 16.452 1 0.06 0.25 p=0.6\ndown 16.552 2 0.25 0.4 p=0.6\n"
              "click 17.052\nup 17.452 1\nup 17.452 2\nkey 19.548 Space\nkey 24.194 Delete\nend 42.581\n"}) {
            const auto take = testkit::play(script);
            QVERIFY(take.view.parts[kAtmos].playing >= 0);
            const double bar = 60.0 / 124.0 * 4, half = bar / 2;
            const double from = std::ceil(24.5 / bar) * bar;
            double lo = 1e9, hi = -1e9, prev = 0, worst = 0;
            bool first = true;
            for (double t = from; t + half < 40; t += half) {
                const double r = 20 * std::log10(take.rms(t, t + half));
                lo = std::min(lo, r);
                hi = std::max(hi, r);
                if (!first)
                    worst = std::max(worst, std::fabs(r - prev));
                prev = r;
                first = false;
            }
            QVERIFY2(worst < 1.5, qPrintable(QStringLiteral("a half bar jumps %1 dB").arg(worst)));
            QString all;
            for (double t = from; t + half < 40; t += half)
                all += QStringLiteral(" %1").arg(20 * std::log10(take.rms(t, t + half)), 0, 'f', 1);
            QVERIFY2(hi - lo < 3.0, qPrintable(QStringLiteral("%1 dB across the loop:%2").arg(hi - lo).arg(all)));
        }
    }

    void spaceLoopsABedLatchedLongAgo() {
        // Nothing played for bars, the bed still sounding: Space loops the
        // bar going by, and the bed carries on at its level after Delete.
        const auto take = testkit::play("page 0.05 6\nkey 0.1 Ctrl+M\nkey 0.3 W\nkey 12 Space\nkey 16 Delete\nend 36\n");
        QCOMPARE(take.view.parts[kAtmos].playing, 0);
        QVERIFY(std::fabs(20 * std::log10(take.rms(24, 36) / take.rms(8, 12))) < 1.5);
    }

    void aLoopCarriesTheBedsOwnProgression() {
        // Z, then N a bar later, then Space on the next one: the loop
        // alternates i and VI. (Space a quarter bar late takes three bars,
        // i VI VI, since the held bed counts as playing until Space.)
        const auto take = testkit::play("page 0.05 6\nkey 0.1 Ctrl+M\ndown 7.942 1 0.25 0.4 p=0.6\n"
                                        "down 8.042 2 0.45 0.4 p=0.5\nclick 8.542\nup 8.942 1\nup 8.942 2\n"
                                        "key 7.742 Z\nkey 9.677 N\nkey 11.62 Space\nend 31\n");
        QCOMPARE(take.view.parts[kAtmos].playing, 0);
        QCOMPARE(take.view.parts[kAtmos].data[16], 1.0f); // the loop drives the chord
        // Pitched energy, bar by bar: E (in i, not VI) against F (in VI, not i).
        const double bar = 60.0 / 124.0 * 4;
        auto energy = [&](double hz, double a, double z) {
            double s1 = 0, s2 = 0;
            const double w = 2 * M_PI * hz / 48000.0, cw = std::cos(w);
            for (size_t i = size_t(a * 48000); i < size_t(z * 48000); ++i) {
                const double x = 0.5 * (take.left[i] + take.right[i]) + 2 * cw * s1 - s2;
                s2 = s1;
                s1 = x;
            }
            return s1 * s1 + s2 * s2 - 2 * cw * s1 * s2;
        };
        auto pc = [&](int pitchClass, double a, double z) {
            double e = 0;
            for (int m = 48; m <= 84; ++m)
                if (m % 12 == pitchClass)
                    e += energy(440.0 * std::exp2((m - 69) / 12.0), a, z);
            return e;
        };
        int alternations = 0;
        for (int b = 8; b < 15; b += 2) {
            const double a0 = b * bar + 0.8, a1 = (b + 1) * bar, f0 = (b + 1) * bar + 0.8, f1 = (b + 2) * bar;
            const bool minor = pc(4, a0, a1) > pc(5, a0, a1);
            const bool sixth = pc(5, f0, f1) > pc(4, f0, f1);
            alternations += minor && sixth;
        }
        QVERIFY2(alternations >= 3, qPrintable(QString::number(alternations)));
    }

    void theChoirsVowelsDiffer() {
        // oo against ee, as heard: the second formant's region (1.6 to 2.5
        // kHz) at least 6 dB apart, at the same level.
        auto measure = [](float colour, double &level, double &upper) {
            const float x = (2 + 0.08f + 0.84f * colour) / 5;
            const auto take = testkit::play("page 0 6\ndown 0.2 1 " + std::to_string(x) + " 0.5 p=0.6\nend 3.5\n");
            Bench b;
            b.left = take.left;
            b.right = take.right;
            level = take.rms(1.5, 3.5);
            upper = 0;
            for (double hz = 1600; hz < 2500; hz *= 1.005)
                upper += b.energyAt(hz, 1.5, 3.5);
        };
        double lo, uo, le, ue;
        measure(0.0f, lo, uo);
        measure(1.0f, le, ue);
        QVERIFY2(10 * std::log10(ue / uo) > 6, qPrintable(QString::number(10 * std::log10(ue / uo))));
        QVERIFY(std::fabs(20 * std::log10(le / lo)) < 2.0);
    }

    void globalTogglesNeverLoop() {
        // The cloud switched on while a take is played, off after the loop
        // formed: the loop never switches it back.
        const auto take = testkit::play("page 0 6\nkey 0.05 Ctrl+M\nkey 0.2 R\nkey 1 G\n"
                                        "key 3.5 Space\nkey 6 G\nend 20\n");
        QCOMPARE(take.view.parts[kAtmos].playing, 0);
        QCOMPARE(take.view.parts[kAtmos].data[1], 0.0f);
    }

    void leavingLetsFingersGoAndKeepsTheBed() {
        const auto take = testkit::play("page 0 6\nkey 0.2 Q\ndown 0.5 1 0.5 0.5 p=0.8\npage 3 1\nend 14\n");
        // The scene's three layers keep sounding; the finger's ended.
        QCOMPARE(take.view.parts[kAtmos].voices, 3);
        QVERIFY(take.rms(12, 14) > 0.02);
    }

    // The keys every part page shares (PARTS.md 4b), and the band under
    // the pad that gives the mouse all of them (4c).
    void theSharedKeysReachTheBed() {
        // ← → step the scenes, from either end.
        testkit::play("page 0 6\nkey 0.2 Right\nkey 0.5 Right\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Ocean")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nkey 0.2 Left\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Dub")), qPrintable(s.pageStatus()));
        });
        // From the last round to the first.
        testkit::play("page 0 6\nkey 0.2 P\nkey 0.5 Right\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Dawn")), qPrintable(s.pageStatus()));
        });
        // Shift+← the pump down (from the kick to off), + the tone up a
        // step, and § (the key left of 1) follows the Chords page again
        // after the bed's own chord.
        const auto take = testkit::play("page 0 6\nkey 0.2 Shift+Left\nkey 0.3 +\nkey 0.4 V\nkey 0.6 §\nend 1\n");
        const float *d = take.view.parts[kAtmos].data;
        QCOMPARE(int(d[2]), 0);
        QCOMPARE(d[kPubTone], 0.625f);
        QCOMPARE(int(d[5]), -1);
        // The second keys: ] and Shift+→ the pump back up, = and - the tone,
        // ` (left of 1 on US keyboards) and the backslash to follow.
        const auto second = testkit::play("page 0 6\nkey 0.2 ]\nkey 0.25 Shift+Right\nkey 0.3 =\nkey 0.35 =\n"
                                          "key 0.4 -\nkey 0.45 V\nkey 0.6 `\nend 1\n");
        d = second.view.parts[kAtmos].data;
        QCOMPARE(int(d[2]), 0); // kick, beat, then round to off
        QCOMPARE(d[kPubTone], 0.625f);
        QCOMPARE(int(d[5]), -1);
        const auto slash = testkit::play("page 0 6\nkey 0.2 V\nkey 0.6 \\\nend 1\n");
        QCOMPARE(int(slash.view.parts[kAtmos].data[5]), -1);
    }

    void returnLatchesWhatIsHeldOrHolds() {
        // With a finger down, Return latches it, as the click does.
        const auto take = testkit::play("page 0 6\ndown 0.2 1 0.3 0.5 p=0.7\nkey 1.0 Return\nup 2.0 1\nend 8\n",
                                        [](Session &s) { QCOMPARE(s.probe(kAtmos, QStringLiteral("latched")), 1.0f); });
        QVERIFY(take.rms(6, 8) > 0.02);
        // With none, it turns hold on: a layer held a moment stays.
        testkit::play("page 0 6\nkey 0.2 Return\ndown 0.5 1 0.3 0.5 p=0.7\nup 1.5 1\nend 3\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("hold")), qPrintable(s.pageStatus()));
            QCOMPARE(s.probe(kAtmos, QStringLiteral("latched")), 1.0f);
        });
        // Shift+Return is overdub.
        const auto dub = testkit::play("page 0 6\nkey 0.2 Shift+Return\nend 0.5\n");
        QVERIFY(dub.view.parts[kAtmos].overdub);
    }

    void theBandUnderThePadIsClickable() {
        // On the scripts' window the pad is 900 by 562.5 px. Row 0 of the
        // band: the clips to 0.42 of the width, then the scenes in two thin
        // rows of five (centres 0.034 and 0.062 under the pad). Row 1 in two
        // thin rows (0.105 and 0.133 under): the switches, then the steppers
        // and the chords.
        testkit::play("page 0 6\nbutton 0.2 0.65 -0.034\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Ocean")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nbutton 0.2 0.72 -0.062\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Pulse")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nbutton 0.2 0.95 -0.062\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Dub")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nbutton 0.2 0.03 -0.105\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("hold")), qPrintable(s.pageStatus()));
        });
        // Freeze; the space stepper's right half twice and its left half
        // once; the chord IV.
        const auto take = testkit::play("page 0 6\nbutton 0.2 0.14 -0.105\n"
                                        "button 0.3 0.11 -0.133\nbutton 0.4 0.11 -0.133\nbutton 0.5 0.02 -0.133\n"
                                        "button 0.6 0.69 -0.133\nend 1\n");
        const float *d = take.view.parts[kAtmos].data;
        QCOMPARE(d[0], 1.0f);
        QCOMPARE(d[3], 0.75f);
        QCOMPARE(int(d[5]), 3);
    }

    void theSwellChipIsAToggleThatEndsByItself() {
        // A click has no release: the swell chip turns the swell on, and
        // it ends by itself once it has risen all the way (eight bars).
        const std::string on = "page 0 6\nkey 0.1 Q\nbutton 0.2 0.30 -0.105\n";
        testkit::play(on + "end 6\n", [](Session &s) {
            QVERIFY(s.view().parts[kAtmos].data[4] > 0.2f);
            QCOMPARE(s.probe(kAtmos, QStringLiteral("swells")), 1.0f);
        });
        const auto ended = testkit::play(on + "end 20\n");
        QCOMPARE(ended.view.parts[kAtmos].data[4], 0.0f);
        // A second click lets it fall at once.
        const auto off = testkit::play(on + "button 3 0.30 -0.105\nend 6\n");
        QCOMPARE(off.view.parts[kAtmos].data[4], 0.0f);
    }

    void aClickOnAClipStopsIt() {
        const auto take = testkit::play("page 0 6\nkey 0.05 Ctrl+M\nkey 0.2 R\nkey 3.5 Space\n"
                                        "button 9 0.026 -0.048\nend 14\n");
        QCOMPARE(take.view.parts[kAtmos].playing, -1);
        QVERIFY(take.view.parts[kAtmos].clips[0].state != ClipView::Empty);
    }

    void everySceneIsCleanAndItsOwn() {
        // Each of the ten scenes alone: finite, under the peak, no DC, and as
        // loud as the rest in its loud moments (the stabs of Dub are short,
        // so its mean is lower, not its punch); and Neon and Dub are their
        // own sound: their balance, third-octave by third-octave, at least
        // 4 dB (rms over the bands) from every other scene's.
        std::vector<std::array<double, kThirds>> looks;
        std::vector<double> loud, rmsDb;
        for (int sc = 0; sc < kScenes; ++sc) {
            const auto take = testkit::play(std::string("page 0 6\nkey 0.2 ") + kSceneKeys[sc] + "\nend 9\n");
            QVERIFY(take.finite());
            QVERIFY2(take.peak() < 0.9, qPrintable(QStringLiteral("%1: %2").arg(kSceneNames[sc]).arg(take.peak())));
            const size_t a = 4 * 48000, z = 9 * 48000;
            double dc = 0;
            for (size_t i = a; i < z; ++i)
                dc += take.left[i] + take.right[i];
            QVERIFY2(std::fabs(dc / double(2 * (z - a))) < 1e-3, kSceneNames[sc]);
            loud.push_back(loudness(take.left, take.right, 4, 9));
            rmsDb.push_back(20 * std::log10(take.rms(4, 9)));
            looks.push_back(thirds(take.left, take.right, 4, 9));
        }
        double mean = 0;
        for (double l : loud)
            mean += l / kScenes;
        for (int sc = 0; sc < kScenes; ++sc)
            QVERIFY2(std::fabs(loud[size_t(sc)] - mean) < 3.0,
                     qPrintable(QStringLiteral("%1: %2 dB against %3").arg(kSceneNames[sc]).arg(loud[size_t(sc)]).arg(mean)));
        // And by plain rms too, so a sparse scene can't hide behind its
        // loud moments: stepping onto it never sounds like the music stopped.
        double meanRms = 0;
        for (double l : rmsDb)
            meanRms += l / kScenes;
        for (int sc = 0; sc < kScenes; ++sc)
            QVERIFY2(std::fabs(rmsDb[size_t(sc)] - meanRms) < 3.0,
                     qPrintable(QStringLiteral("%1: %2 dB rms against %3").arg(kSceneNames[sc]).arg(rmsDb[size_t(sc)]).arg(meanRms)));
        auto distance = [&](int p, int q) {
            double d = 0;
            for (size_t k = 0; k < kThirds; ++k)
                d += (looks[size_t(p)][k] - looks[size_t(q)][k]) * (looks[size_t(p)][k] - looks[size_t(q)][k]) / kThirds;
            return std::sqrt(d);
        };
        // ATMOS_LOOKS=1 prints every scene's loudness and its distance to each.
        if (qEnvironmentVariableIsSet("ATMOS_LOOKS")) {
            QString table;
            for (int sc = 0; sc < kScenes; ++sc) {
                table += QStringLiteral("\n%1 %2 dB (rms %3):").arg(kSceneNames[sc]).arg(loud[size_t(sc)], 0, 'f', 1)
                             .arg(rmsDb[size_t(sc)], 0, 'f', 1);
                for (int o = 0; o < kScenes; ++o)
                    table += QStringLiteral(" %1").arg(distance(sc, o), 0, 'f', 1);
            }
            qInfo().noquote() << table;
        }
        for (int sc = 8; sc < kScenes; ++sc)
            for (int other = 0; other < kScenes; ++other)
                if (other != sc)
                    QVERIFY2(distance(sc, other) > 4.0, qPrintable(QStringLiteral("%1 and %2: %3 dB")
                                                                       .arg(kSceneNames[sc], kSceneNames[other]).arg(distance(sc, other))));
    }

    void theDubSceneStabsWithGapsBetween() {
        // Laid as a scene, the stabs stand at least 12 dB over the quietest
        // moment between them (folded over two beats, in 1/48-beat slots),
        // so it is heard as stabs and their echo, not as a pumping pad.
        const auto take = testkit::play("page 0 6\nkey 0.2 P\nend 12\n");
        const double beat = 60.0 / 124.0, slot = 2 * beat / 48;
        std::array<double, 48> e{};
        for (double t0 = std::ceil(4.0 / (2 * beat)) * 2 * beat; t0 + 2 * beat < 12; t0 += 2 * beat)
            for (int k = 0; k < 48; ++k) {
                const double r = take.rms(t0 + k * slot, t0 + (k + 1) * slot);
                e[size_t(k)] += r * r;
            }
        const double hi = *std::max_element(e.begin(), e.end()), lo = *std::min_element(e.begin(), e.end());
        QVERIFY2(10 * std::log10(hi / lo) >= 12.0, qPrintable(QString::number(10 * std::log10(hi / lo))));
    }

    void theStyleIsAChoiceOfItsOwn() {
        auto style = [](Session &s) { return int(s.probe(kAtmos, QStringLiteral("style"))); };
        // L steps it: classic, neon, dub, tape, and round again.
        testkit::play("page 0 6\nkey 0.2 L\nend 0.5\n", [&](Session &s) { QCOMPARE(style(s), int(Neon)); });
        testkit::play("page 0 6\nkey 0.2 L\nkey 0.3 L\nend 0.5\n", [&](Session &s) {
            QCOMPARE(style(s), int(Dub));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("dub layers")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nkey 0.2 L\nkey 0.3 L\nkey 0.4 L\ndown 1.0 1 0.38 0.5 p=0.6\nend 2\n", [&](Session &s) {
            QCOMPARE(style(s), int(Tape));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("bright")) && s.pageStatus().contains(QStringLiteral("tape layers")),
                     qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nkey 0.2 L\nkey 0.3 L\nkey 0.4 L\nkey 0.5 L\nend 0.7\n",
                      [&](Session &s) { QCOMPARE(style(s), int(Classic)); });
        // Laying Neon sets it, and a finger on the pad plays Neon's pad.
        testkit::play("page 0 6\nkey 0.2 O\ndown 1.0 1 0.22 0.5 p=0.6\nend 2\n", [&](Session &s) {
            QCOMPARE(style(s), int(Neon));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("strings")), qPrintable(s.pageStatus()));
            const float *d = s.view().parts[kAtmos].data;
            bool neon = false;
            for (int k = 0; k < 20; ++k)
                neon |= int(d[20 + 9 * k]) == 2 && (int(d[20 + 9 * k + 4]) >> 4) == Neon;
            QVERIFY(neon);
        });
        // Fading it with its own key sets it back: the pad is classic again.
        testkit::play("page 0 6\nkey 0.2 O\nkey 0.5 O\ndown 1.0 1 0.38 0.5 p=0.6\nend 2\n", [&](Session &s) {
            QCOMPARE(style(s), int(Classic));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("hollow")), qPrintable(s.pageStatus()));
        });
        // Nostalgia is lo-fi tape: laying it sets Tape, fading it Classic.
        testkit::play("page 0 6\nkey 0.2 T\nend 0.5\n", [&](Session &s) { QCOMPARE(style(s), int(Tape)); });
        testkit::play("page 0 6\nkey 0.2 T\nkey 0.4 T\nend 0.5\n", [&](Session &s) { QCOMPARE(style(s), int(Classic)); });
        // Delete lets the scene go but keeps the style the player chose.
        testkit::play("page 0 6\nkey 0.2 P\nkey 0.5 Delete\ndown 1.0 1 0.38 0.5 p=0.6\nend 2\n", [&](Session &s) {
            QCOMPARE(style(s), int(Dub));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("stab")), qPrintable(s.pageStatus()));
        });
        // The chips, the last of the band's switches: TAPE, DUB, then CLASSIC.
        testkit::play("page 0 6\nbutton 0.2 0.975 -0.105\nend 0.5\n", [&](Session &s) { QCOMPARE(style(s), int(Tape)); });
        testkit::play("page 0 6\nbutton 0.2 0.91 -0.105\nend 0.5\n", [&](Session &s) { QCOMPARE(style(s), int(Dub)); });
        testkit::play("page 0 6\nbutton 0.2 0.975 -0.105\nbutton 0.3 0.79 -0.105\nend 0.5\n",
                      [&](Session &s) { QCOMPARE(style(s), int(Classic)); });
    }

    void tenFingersAtOnceAreFullNotLoud() {
        std::string s = "page 0 6\n";
        for (int i = 0; i < 10; ++i)
            s += "down " + std::to_string(0.2 + 0.05 * i) + " " + std::to_string(i + 1) + " " +
                 std::to_string(0.05 + 0.1 * i) + " " + std::to_string(0.2 + 0.06 * i) + " p=0.8\n";
        const auto ten = testkit::play(s + "end 5\n");
        const auto one = testkit::play("page 0 6\ndown 0.2 1 0.25 0.5 p=0.8\nend 5\n");
        QVERIFY(ten.finite());
        QVERIFY(ten.peak() < 1.0);
        // Ten layers are fuller than one but far from ten times the power.
        const double ratio = ten.rms(3, 5) / one.rms(3, 5);
        QVERIFY2(ratio > 1.05 && ratio < 3.0, qPrintable(QString::number(ratio)));
    }

    // Song files: the bed comes back as it was left.
    // A styled scene comes back styled: Dub laid on P is saved with the
    // style hand-laid layers take next and each layer's own, and opens as
    // the same stabs, not a classic bed.
    void aStyledSceneComesBackWithTheSong() {
        testkit::reopen("page 0 6\nkey 0.2 P\nend 1.5\n", 1.5, [](Session &, Session &opened, const QJsonObject &doc) {
            const QJsonObject saved = settingsOf(doc);
            QCOMPARE(saved[QStringLiteral("scene")].toString(), QStringLiteral("Dub"));
            QCOMPARE(saved[QStringLiteral("style")].toString(), QStringLiteral("Dub"));
            bool dubLayer = false;
            for (const QJsonValue &v : saved[QStringLiteral("bed")].toArray())
                dubLayer |= v.toObject()[QStringLiteral("style")].toString() == QStringLiteral("Dub");
            QVERIFY(dubLayer);
            QCOMPARE(settingsOf(opened.saveSong()), saved);
            QCOMPARE(int(opened.probe(kAtmos, QStringLiteral("style"))), 2);
        });
    }

    void theBedComesBackWithTheSong() {
        // A scene, every setting off its default, and a layer latched by
        // hand: saved, opened in a new tenfold, played on.
        const std::string script = "page 0 6\nkey 0.2 W\nkey 0.4 F\nkey 0.5 G\nkey 0.6 ]\nkey 0.7 Up\nkey 0.8 +\n"
                                   "key 0.9 V\nkey 1.0 H\ndown 1.5 1 0.3 0.5 p=0.7\nclick 2.0\nup 2.5 1\nend 4\n";
        testkit::reopen(script, 4.0, [](Session &, Session &opened, const QJsonObject &doc) {
            const QJsonObject saved = settingsOf(doc);
            QCOMPARE(saved[QStringLiteral("space")].toDouble(), 0.75);
            QCOMPARE(saved[QStringLiteral("tone")].toDouble(), 0.625);
            QCOMPARE(saved[QStringLiteral("pump")].toString(), QStringLiteral("beat"));
            QVERIFY(saved[QStringLiteral("cloud")].toBool());
            QVERIFY(saved[QStringLiteral("freeze")].toBool());
            QVERIFY(saved[QStringLiteral("hold")].toBool());
            QCOMPARE(saved[QStringLiteral("chord")].toInt(), 3);
            QCOMPARE(saved[QStringLiteral("scene")].toString(), QStringLiteral("Ocean"));
            const QJsonArray bed = saved[QStringLiteral("bed")].toArray();
            QCOMPARE(bed.size(), 4);
            QCOMPARE(bed[0].toObject()[QStringLiteral("sound")].toString(), QStringLiteral("Drone"));
            QVERIFY(bed[0].toObject()[QStringLiteral("scene")].toBool());
            QCOMPARE(bed[3].toObject()[QStringLiteral("sound")].toString(), QStringLiteral("Pad"));
            QVERIFY(!bed[3].toObject().contains(QStringLiteral("scene")));
            // Opened, the page saves what it was given.
            QCOMPARE(settingsOf(opened.saveSong()), saved);
            // And the engine plays it: frozen, the cloud, the pump on every
            // beat, more space, a step brighter, its own IV, four layers.
            const float *d = opened.view().parts[kAtmos].data;
            QCOMPARE(d[0], 1.0f);
            QCOMPARE(d[1], 1.0f);
            QCOMPARE(int(d[2]), 2);
            QCOMPARE(d[3], 0.75f);
            QCOMPARE(d[kPubTone], 0.625f);
            QCOMPARE(int(d[5]), 3);
            QCOMPARE(d[kPubOwnHand], 1.0f);
            QCOMPARE(opened.view().parts[kAtmos].voices, 4);
            QCOMPARE(opened.probe(kAtmos, QStringLiteral("latched")), 4.0f);
            const QString status = opened.pageStatus();
            QVERIFY2(status.contains(QStringLiteral("Ocean")) && status.contains(QStringLiteral("hold")), qPrintable(status));
        });
    }

    void aLoopsOwnChordsComeBackWithItsLoop() {
        // The bed's own progression looped with Space: the chords are the
        // loop's, so the file says to follow, and the loop takes them again.
        const std::string script = "page 0.05 6\nkey 0.1 Ctrl+M\ndown 7.942 1 0.25 0.4 p=0.6\n"
                                   "down 8.042 2 0.45 0.4 p=0.5\nclick 8.542\nup 8.942 1\nup 8.942 2\n"
                                   "key 7.742 Z\nkey 9.677 N\nkey 11.62 Space\nend 18\n";
        testkit::reopen(script, 6.0, [](Session &before, Session &opened, const QJsonObject &doc) {
            QCOMPARE(before.view().parts[kAtmos].playing, 0);
            const QJsonObject saved = settingsOf(doc);
            QCOMPARE(saved[QStringLiteral("chord")].toInt(), -1);
            QCOMPARE(settingsOf(opened.saveSong()), saved);
            QCOMPARE(opened.view().parts[kAtmos].playing, 0);
            QCOMPARE(opened.view().parts[kAtmos].data[kPubOwnLoop], 1.0f);
        });
    }

    void aBrokenFileOpensASaneBed() {
        QJsonObject doc;
        testkit::play("page 0 6\nend 0.5\n", [&](Session &s) { doc = s.saveSong(); });
        auto withSettings = [&](const QJsonValue &settings) {
            QJsonObject d = doc;
            QJsonArray parts = d[QStringLiteral("parts")].toArray();
            for (int i = 0; i < parts.size(); ++i) {
                QJsonObject p = parts[i].toObject();
                if (p[QStringLiteral("name")].toString() == QStringLiteral("Atmos")) {
                    p[QStringLiteral("settings")] = settings;
                    parts[i] = p;
                }
            }
            d[QStringLiteral("parts")] = parts;
            return d;
        };
        auto opened = [&](const QJsonObject &d) {
            QJsonObject out;
            testkit::play("end 0.1\n", [&](Session &s) {
                QString error;
                QVERIFY2(s.openSong(d, &error), qPrintable(error));
                out = settingsOf(s.saveSong());
            });
            return out;
        };

        // Wrong types, numbers far out of range, names nobody knows, more
        // layers than voices: every value is the default or clamped.
        QJsonArray bed;
        bed.append(QJsonObject{{"sound", "Kazoo"}, {"height", 0.5}});
        bed.append(7);
        bed.append(QStringLiteral("pad"));
        bed.append(QJsonObject{{"sound", "pad"}, {"colour", -5}, {"height", 1e9}, {"press", "x"}, {"motion", -1e30}, {"width", 7}, {"scene", true}});
        bed.append(QJsonObject{{"sound", "GLASS"}});
        for (int i = 0; i < 40; ++i)
            bed.append(QJsonObject{{"sound", "Air"}});
        const QJsonObject s = opened(withSettings(QJsonObject{{"space", 1e300}, {"tone", "bright"}, {"pump", "sideways"},
                                                              {"cloud", 3}, {"freeze", "yes"}, {"hold", QJsonValue()},
                                                              {"chord", -1e12}, {"scene", 42}, {"comet", QJsonObject{{"x", 1}}},
                                                              {"bed", bed}}));
        QCOMPARE(s[QStringLiteral("space")].toDouble(), 1.0);
        QCOMPARE(s[QStringLiteral("tone")].toDouble(), 0.5);
        QCOMPARE(s[QStringLiteral("pump")].toString(), QStringLiteral("kick"));
        QCOMPARE(s[QStringLiteral("cloud")].toBool(), false);
        QCOMPARE(s[QStringLiteral("freeze")].toBool(), false);
        QCOMPARE(s[QStringLiteral("hold")].toBool(), false);
        QCOMPARE(s[QStringLiteral("chord")].toInt(), -1);
        QVERIFY(!s.contains(QStringLiteral("scene")));
        QVERIFY(!s.contains(QStringLiteral("comet")));
        const QJsonArray back = s[QStringLiteral("bed")].toArray();
        QCOMPARE(back.size(), 20); // no more layers than the engine has voices
        const QJsonObject pad = back[0].toObject();
        QCOMPARE(pad[QStringLiteral("sound")].toString(), QStringLiteral("Pad"));
        QCOMPARE(pad[QStringLiteral("colour")].toDouble(), 0.0);
        QCOMPARE(pad[QStringLiteral("height")].toDouble(), 1.0);
        QCOMPARE(pad[QStringLiteral("press")].toDouble(), 0.5);
        QCOMPARE(pad[QStringLiteral("motion")].toDouble(), -1.0);
        QCOMPARE(pad[QStringLiteral("width")].toDouble(), 1.0);
        QVERIFY(!pad.contains(QStringLiteral("scene"))); // no scene to belong to
        QCOMPARE(back[1].toObject()[QStringLiteral("sound")].toString(), QStringLiteral("Glass"));

        // Not an object at all, or a bed that isn't a list: the defaults.
        for (const QJsonValue &v : {QJsonValue(5), QJsonValue(QJsonObject{{"bed", QJsonObject{{"sound", "Pad"}}}, {"space", -3}})}) {
            const QJsonObject d = opened(withSettings(v));
            QCOMPARE(d[QStringLiteral("pump")].toString(), QStringLiteral("kick"));
            QCOMPARE(d[QStringLiteral("tone")].toDouble(), 0.5);
            QVERIFY(d[QStringLiteral("bed")].toArray().isEmpty());
        }
        // A file that left the page's settings out keeps the defaults even
        // over a session that had moved them.
        QJsonObject over;
        testkit::play("page 0 6\nkey 0.2 G\nkey 0.3 Up\nend 0.6\n", [&](Session &s) {
            QString error;
            QVERIFY2(s.openSong(withSettings(QJsonObject{}), &error), qPrintable(error));
            over = settingsOf(s.saveSong());
        });
        QCOMPARE(over[QStringLiteral("cloud")].toBool(), false);
        QCOMPARE(over[QStringLiteral("space")].toDouble(), 0.5);
    }
};

TESTKIT_REGISTER(AtmosTests)
#include "atmos_tests.moc"
