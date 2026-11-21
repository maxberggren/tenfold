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
enum { PFreeze = 0, PCloud, PPump, PSpace, PSwell, PChord, PChordToLoop, PTone };
constexpr int kPubTone = 19;
constexpr int A = 9, C = 0, D = 2, E = 4, F = 5, G = 7, B = 11;

}

class AtmosTests : public QObject {
    Q_OBJECT
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
        // At one pressure, every colour and height of a column within 3 dB.
        for (int family : {Drone, Pad, Choir, Glass, Air}) {
            double lo = 1e9, hi = 0;
            for (float c : {0.1f, 0.5f, 0.9f})
                for (float y : {0.1f, 0.5f, 0.9f}) {
                    Bench b;
                    b.param(PSpace, 0); // the dry layer, not the room
                    b.on(1, family, y, c, 0.6f);
                    b.run(4);
                    const double r = b.rms(2, 4);
                    lo = std::min(lo, r);
                    hi = std::max(hi, r);
                }
            QVERIFY2(20 * std::log10(hi / lo) < 3.0, qPrintable(QStringLiteral("family %1: %2 dB").arg(family).arg(20 * std::log10(hi / lo))));
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
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Pulse")), qPrintable(s.pageStatus()));
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
        // rows of four (centres 0.034 and 0.062 under the pad). Row 1 in two
        // thin rows (0.105 and 0.133 under): the switches, then the steppers
        // and the chords.
        testkit::play("page 0 6\nbutton 0.2 0.65 -0.034\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Ocean")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nbutton 0.2 0.93 -0.062\nend 1\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Pulse")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 6\nbutton 0.2 0.03 -0.105\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("hold")), qPrintable(s.pageStatus()));
        });
        // Freeze; the space stepper's right half twice and its left half
        // once; the chord IV.
        const auto take = testkit::play("page 0 6\nbutton 0.2 0.18 -0.105\n"
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
        const std::string on = "page 0 6\nkey 0.1 Q\nbutton 0.2 0.45 -0.105\n";
        testkit::play(on + "end 6\n", [](Session &s) {
            QVERIFY(s.view().parts[kAtmos].data[4] > 0.2f);
            QCOMPARE(s.probe(kAtmos, QStringLiteral("swells")), 1.0f);
        });
        const auto ended = testkit::play(on + "end 20\n");
        QCOMPARE(ended.view.parts[kAtmos].data[4], 0.0f);
        // A second click lets it fall at once.
        const auto off = testkit::play(on + "button 3 0.45 -0.105\nend 6\n");
        QCOMPARE(off.view.parts[kAtmos].data[4], 0.0f);
    }

    void aClickOnAClipStopsIt() {
        const auto take = testkit::play("page 0 6\nkey 0.05 Ctrl+M\nkey 0.2 R\nkey 3.5 Space\n"
                                        "button 9 0.026 -0.048\nend 14\n");
        QCOMPARE(take.view.parts[kAtmos].playing, -1);
        QVERIFY(take.view.parts[kAtmos].clips[0].state != ClipView::Empty);
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
};

TESTKIT_REGISTER(AtmosTests)
#include "atmos_tests.moc"
