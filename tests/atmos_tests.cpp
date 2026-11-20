// The Atmos page: its engine on its own (in the chord, following it, the
// voicing, the envelope, freeze, the gate, the budget) and the page through
// the whole session (latching, scenes, loops, recording, leaving).

#include <array>
#include <chrono>

#include "parts.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr int kAtmos = 5; // the sixth page

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
            float attack = 0.05f) {
        Command c;
        c.type = Command::On;
        c.id = id;
        c.i[0] = family;
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
enum { PFreeze = 0, PCloud, PPump, PSpace, PSwell, PChord };
constexpr int A = 9, C = 0, D = 2, E = 4, F = 5, G = 7, B = 11;

}

class AtmosTests : public QObject {
    Q_OBJECT
private slots:
    void everyLayerIsInTheChord() {
        // A minor, the tonic chord: whatever the layer, nearly all its
        // pitched energy is A, C or E (the rest is upper harmonics).
        for (int family : {Drone, Pad, Choir, Glass}) {
            Bench b;
            b.on(1, family, 0.5f);
            b.run(3);
            const double s = b.share(1, 3, {A, C, E});
            QVERIFY2(s > 0.85, qPrintable(QStringLiteral("family %1: %2").arg(family).arg(s)));
        }
    }

    void theDroneIsRootAndFifth() {
        Bench b;
        b.on(1, Drone, 0.3f);
        b.run(3);
        QVERIFY(b.share(1, 3, {A, E}) > 0.95);
    }

    void followsTheChordOfTheMoment() {
        Bench b;
        b.on(1, Pad, 0.5f);
        b.h.chordRoot = 5; // VI: F A C
        b.h.chordTones[0] = 5, b.h.chordTones[1] = 7, b.h.chordTones[2] = 9;
        ++b.h.chordSeq;
        b.run(3);
        QVERIFY(b.share(1.5, 3, {F, A, C}) > 0.85);
        b.h.chordRoot = 6; // VII: G B D, nothing in common
        b.h.chordTones[0] = 6, b.h.chordTones[1] = 8, b.h.chordTones[2] = 10;
        ++b.h.chordSeq;
        b.run(3);
        QVERIFY(b.share(4.5, 6, {G, B, D}) > 0.85);
        // The change is a crossfade, not a cut: no gap in the middle.
        QVERIFY(b.rms(3.0, 3.5) > 0.5 * b.rms(2.0, 3.0));
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

    void freezeHoldsTheBedAfterTheFingersLeave() {
        Bench b;
        b.on(1, Pad, 0.5f);
        b.on(2, Choir, 0.5f, 0.5f);
        b.run(2);
        const double held = b.rms(1, 2);
        b.param(PFreeze, 1);
        b.run(0.3);
        b.off(1);
        b.off(2);
        b.run(14);
        QCOMPARE(b.engine.voices(), 0);
        // Long after the voices ended, the frozen halo still sings, in the chord.
        QVERIFY(b.rms(14, 16) > 0.1 * held);
        QVERIFY(b.share(14, 16, {A, C, E}) > 0.8);
        b.param(PFreeze, 0);
        b.run(20);
        QVERIFY(b.rms(34, 36) < 0.01 * held);
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
        // Still sounding long after the lift, until the orb is tapped.
        QVERIFY(take.rms(6, 8) > 0.02);
        QVERIFY(take.rms(17, 18) < 0.05 * take.rms(6, 8));
        QCOMPARE(take.view.parts[kAtmos].voices, 0);
    }

    void aSceneLaysABedAndTheSameKeyFadesIt() {
        const auto take = testkit::play("page 0 6\nkey 0.2 W\nkey 6 W\nend 20\n");
        QVERIFY(take.rms(4, 6) > 0.03);
        QVERIFY(take.rms(19, 20) < 0.05 * take.rms(4, 6));
        QCOMPARE(take.view.parts[kAtmos].voices, 0);
    }

    void keysSetTheBedsState() {
        const auto take = testkit::play("page 0 6\nkey 0.2 F\nkey 0.3 G\nkey 0.4 P\nkey 0.5 Up\nkey 0.6 V\nend 1\n");
        const float *d = take.view.parts[kAtmos].data;
        QCOMPARE(d[0], 1.0f);   // frozen
        QCOMPARE(d[1], 1.0f);   // cloud
        QCOMPARE(int(d[2]), 2); // pump on every beat
        QCOMPARE(d[3], 0.75f);  // more space
        QCOMPARE(int(d[5]), 3); // the bed's own chord on IV
        QCOMPARE(int(d[6]), 3); // and it plays it
    }

    void tappedSwellsLoop() {
        const auto take = testkit::play("page 0 6\n"
                                        "tap 0.5 0.3 0.5 p=0.8\ntap 1.0 0.3 0.5 p=0.8\ntap 1.5 0.3 0.5 p=0.8\n"
                                        "tap 2.0 0.3 0.5 p=0.8\ndown 2.5 1 0.3 0.5 p=0.8\nup 2.9 1\nend 9\n");
        QCOMPARE(take.view.parts[kAtmos].playing, 0);
        QVERIFY(take.view.parts[kAtmos].clips[0].events >= 8);
        QVERIFY(take.rms(7, 9) > 0.01); // the swells go on
    }

    void recordingTakesTheWholeLatchedBed() {
        // A scene, then Return: from the next bar to the one after the
        // second press the clip takes the bed, and the latched layers hand
        // over to it.
        const auto take = testkit::play("page 0 6\nkey 0.2 E\nkey 1.0 Return\nkey 6.0 Return\nend 26\n");
        const PartView &v = take.view.parts[kAtmos];
        QCOMPARE(v.playing, 0);
        QVERIFY(v.clips[0].events >= 4);
        // The bed goes on, no louder and no quieter than before: the clip's
        // layers took over from the scene's without a gap or a doubling.
        const double before = take.rms(4, 6), after = take.rms(22, 26), handover = take.rms(8, 12);
        QVERIFY(after > 0.6 * before && after < 1.5 * before);
        QVERIFY(handover > 0.6 * before && handover < 1.5 * before);
        QVERIFY2(v.voices <= 4, qPrintable(QString::number(v.voices)));
    }

    void leavingLetsFingersGoAndKeepsTheBed() {
        const auto take = testkit::play("page 0 6\nkey 0.2 Q\ndown 0.5 1 0.5 0.5 p=0.8\npage 3 1\nend 14\n");
        // The scene's three layers keep sounding; the finger's ended.
        QCOMPARE(take.view.parts[kAtmos].voices, 3);
        QVERIFY(take.rms(12, 14) > 0.02);
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
        QVERIFY2(ratio > 1.2 && ratio < 3.0, qPrintable(QString::number(ratio)));
    }
};

TESTKIT_REGISTER(AtmosTests)
#include "atmos_tests.moc"
