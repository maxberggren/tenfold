// The Bass page: its engine driven directly (pitch, low end, slides, the
// chord follower, the roll, the two lanes), and its controls through real
// scripted performances.

#include <memory>

#include "bass.h"
#include "parts.h"
#include "testkit.h"

using namespace tf;

namespace {

// The studio's side of an engine, reduced to what the bass reads.
struct FakeAudio : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return double(tr.sample) / double(tr.rate); }
    int offsetOf(double t, int n) const override {
        const int o = int((t - now()) * double(tr.rate));
        return o < 0 ? 0 : std::min(o, n - 1);
    }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// The bass engine on its own, at 124 BPM in A minor.
struct Rig {
    FakeAudio ctx;
    std::unique_ptr<PartEngine> engine;
    std::vector<float> left, right;
    float pub[kPartData] = {};
    std::vector<double> gateOpens; // beats where a roll step opened
    float lastGate = 0;

    Rig() {
        PartSpec spec = makeBass();
        engine = std::move(spec.engine);
        engine->init(48000);
        ctx.tr.rate = 48000;
        ctx.tr.bpm = 124;
        ctx.tr.beatsPerSample = ctx.tr.bpm / 60.0 / 48000.0;
        ctx.h.tuning.set(9, 0, 4, false);
    }
    void send(Command c) {
        c.t = 0;
        engine->command(c, ctx);
    }
    void run(double seconds) {
        const int blocks = int(seconds * 48000 / kBlock);
        for (int b = 0; b < blocks; ++b) {
            ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
            float l[kBlock] = {}, r[kBlock] = {};
            engine->render(l, r, kBlock, ctx);
            ctx.tr.sample += kBlock;
            left.insert(left.end(), l, l + kBlock);
            right.insert(right.end(), r, r + kBlock);
            engine->publish(pub);
            const float g = pub[bass::PubGate];
            if (g > 0 && lastGate <= 0)
                gateOpens.push_back(ctx.tr.beat);
            lastGate = g;
        }
    }

    // The pitch: the lowest strong peak under 400 Hz over the last span
    // seconds (within 10 dB of the strongest, so a bright acid's loud third
    // harmonic doesn't count as the note), refined by a parabola.
    double pitch(double span = 0.4) const {
        const size_t n = size_t(span * 48000);
        const size_t from = left.size() - n;
        std::vector<double> mag;
        for (double hz = 20; hz < 400; hz += 0.25) {
            double re = 0, im = 0;
            const double w = 2 * M_PI * hz / 48000.0;
            for (size_t i = 0; i < n; i += 2) {
                const double v = 0.5 * (left[from + i] + right[from + i]) *
                                 (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n)));
                re += v * std::cos(w * double(i));
                im += v * std::sin(w * double(i));
            }
            mag.push_back(re * re + im * im);
        }
        const double top = *std::max_element(mag.begin(), mag.end());
        for (size_t k = 1; k + 1 < mag.size(); ++k)
            if (mag[k] >= 0.1 * top && mag[k] >= mag[k - 1] && mag[k] >= mag[k + 1]) {
                const double d = mag[k - 1] - 2 * mag[k] + mag[k + 1];
                return 20 + 0.25 * (double(k) + (d != 0 ? 0.5 * (mag[k - 1] - mag[k + 1]) / d : 0));
            }
        return 0;
    }
    // The side (L−R) signal's energy under hz against the mid's, in dB.
    double sideBelow(double hz, double span = 0.5) const {
        const size_t n = size_t(span * 48000), from = left.size() - n;
        double side = 0, mid = 0;
        for (double f = 20; f < hz; f += 2) {
            double sr = 0, si = 0, mr = 0, mi = 0;
            const double w = 2 * M_PI * f / 48000.0;
            for (size_t i = 0; i < n; i += 4) {
                const double c = std::cos(w * double(i)), s = std::sin(w * double(i));
                const double l = left[from + i], r = right[from + i];
                sr += (l - r) * c;
                si += (l - r) * s;
                mr += (l + r) * c;
                mi += (l + r) * s;
            }
            side += sr * sr + si * si;
            mid += mr * mr + mi * mi;
        }
        return 10 * std::log10((side + 1e-20) / (mid + 1e-20));
    }
    double peak() const {
        double p = 0;
        for (size_t i = 0; i < left.size(); ++i)
            p = std::max({p, double(std::fabs(left[i])), double(std::fabs(right[i]))});
        return p;
    }
    bool finite() const {
        for (size_t i = 0; i < left.size(); ++i)
            if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
                return false;
        return true;
    }
    // Level in dB of what is above 150 Hz (two one-pole high-passes) between t0 and t1.
    double topRms(double t0, double t1) const {
        double y1 = 0, x1 = 0, y2 = 0, x2 = 0, sum = 0;
        const double a = std::exp(-2 * M_PI * 150.0 / 48000.0);
        size_t count = 0;
        for (size_t i = 0; i < size_t(t1 * 48000) && i < left.size(); ++i) {
            const double x = 0.5 * (left[i] + right[i]);
            const double h1 = a * (y1 + x - x1);
            x1 = x;
            y1 = h1;
            const double h2 = a * (y2 + h1 - x2);
            x2 = h1;
            y2 = h2;
            if (i >= size_t(t0 * 48000)) {
                sum += h2 * h2;
                ++count;
            }
        }
        return 10 * std::log10(sum / double(std::max<size_t>(count, 1)) + 1e-20);
    }
};

Command noteOn(int id, int degree, int sound, int flags = 0, float vel = 0.8f, float tone = 0.5f) {
    Command c;
    c.type = Command::On;
    c.id = id;
    c.i[0] = degree;
    c.i[2] = flags;
    c.i[3] = sound;
    c.f[0] = vel;
    c.f[1] = tone;
    c.f[2] = 0.3f;
    c.f[4] = 0.5f;
    return c;
}
Command noteOff(int id) {
    Command c;
    c.type = Command::Off;
    c.id = id;
    return c;
}
Command shaper(int which, float a, float b, int id = 1) {
    Command c;
    c.type = Command::Param;
    c.id = id;
    c.i[0] = which;
    c.f[0] = a;
    c.f[1] = b;
    return c;
}
double hzOf(double midi) { return 440.0 * std::exp2((midi - 69.0) / 12.0); }
double cents(double hz, double midi) { return 1200 * std::log2(hz / hzOf(midi)); }
constexpr int kReplay = 1 << 30; // how the studio marks a clip's ids

}

class BassTests : public QObject {
    Q_OBJECT
private slots:
    // Every sound plays the note it is asked for, in the key, with one sine
    // under 120 Hz in the middle, and no overs, played hard and bright.
    void everySoundPlaysInKeyMonoAndClean() {
        const double expect[3] = {33, 36, 40}; // A minor's degrees 0, 2, 4: A1, C2, E2
        for (int s = 0; s < bass::SoundCount; ++s) {
            for (int k = 0; k < 3; ++k) {
                Rig rig;
                rig.send(noteOn(1, 2 * k, s, 0, 1.0f, 0.9f));
                rig.run(0.9);
                const QString what = QStringLiteral("%1 degree %2").arg(bass::recipe(s).name).arg(2 * k);
                QVERIFY2(std::fabs(cents(rig.pitch(), expect[k])) < 15, qPrintable(what));
                QVERIFY2(rig.sideBelow(120) < -30, qPrintable(what + " is wide under 120 Hz"));
                QVERIFY(rig.finite());
                QVERIFY2(rig.peak() < 0.95, qPrintable(what + QStringLiteral(" peaks at %1").arg(rig.peak())));
            }
        }
    }

    // The bass sits where basses sit: the key's root between E1 and D#2.
    void rootLandsInTheSubOctave() {
        for (int root = 0; root < 12; ++root) {
            Rig rig;
            rig.ctx.h.tuning.set(root, 0, 4, false);
            rig.send(noteOn(1, 0, bass::Sub));
            rig.run(0.6);
            const double hz = rig.pitch();
            QVERIFY2(hz > 40 && hz < 80, qPrintable(QStringLiteral("root %1: %2 Hz").arg(root).arg(hz)));
        }
    }

    // A note that lands while another is held slides to it without a new
    // pluck; lifting it slides back.
    void legatoSlidesWithoutANewAttack() {
        Rig legato, fresh;
        for (Rig *r : {&legato, &fresh}) {
            r->send(noteOn(1, 0, bass::Pluck));
            r->run(0.5);
        }
        legato.send(noteOn(2, 4, bass::Pluck));
        fresh.send(noteOff(1));
        fresh.send(noteOn(2, 4, bass::Pluck));
        legato.run(0.5);
        fresh.run(0.5);
        // The fresh note plucks its filter open again; the slide doesn't.
        QVERIFY(fresh.topRms(0.5, 0.56) > legato.topRms(0.5, 0.56) + 3);
        QVERIFY(std::fabs(cents(legato.pitch(0.3), 40)) < 15);
        legato.send(noteOff(2));
        legato.run(0.5);
        QVERIFY(std::fabs(cents(legato.pitch(0.3), 33)) < 15);
    }

    // Two fingers drumming one note overlap a little; each still strikes.
    void theSameNoteAgainStrikesAgain() {
        Rig again, slide;
        for (Rig *r : {&again, &slide}) {
            r->send(noteOn(1, 0, bass::Pluck));
            r->run(0.5);
        }
        again.send(noteOn(2, 0, bass::Pluck));
        slide.send(noteOn(2, 4, bass::Pluck));
        again.run(0.2);
        slide.run(0.2);
        // As hard as the first attack, and well above a slide.
        QVERIFY(again.topRms(0.5, 0.56) > again.topRms(0.0, 0.06) - 1);
        QVERIFY(again.topRms(0.5, 0.56) > slide.topRms(0.5, 0.56) + 3);
    }

    // A line that follows the chords moves its root with them; one that
    // doesn't, stays.
    void followTheChordsMovesTheLine() {
        Rig follow, key;
        follow.send(noteOn(1, 0, bass::Pluck, bass::Follow));
        key.send(noteOn(1, 0, bass::Pluck));
        for (Rig *r : {&follow, &key}) {
            r->run(0.4);
            r->ctx.h.chordRoot = 5; // F major in A minor: the VI
            ++r->ctx.h.chordSeq;
            r->run(0.6);
        }
        // F folds down to F1, under the A, not up to F2.
        QVERIFY(std::fabs(cents(follow.pitch(), 29)) < 15);
        QVERIFY(std::fabs(cents(key.pitch(), 33)) < 15);
        // Two degrees above the chord's root is the chord's third.
        Rig third;
        third.ctx.h.chordRoot = 3; // D minor, the iv
        third.send(noteOn(1, 2, bass::Pluck, bass::Follow));
        third.run(0.6);
        QVERIFY(std::fabs(cents(third.pitch(), 41)) < 15); // F2
    }

    // The roll strikes only on its pattern's lit steps, on the grid.
    void rollStrikesOnTheGrid() {
        Rig rig;
        rig.send(shaper(bass::ShapeRoll, 0.5f, 3)); // ROLL: -xxx
        rig.run(0.13);                              // land between steps
        rig.send(noteOn(1, 0, bass::Pluck));
        rig.run(60.0 / 124.0 * 4);
        // The touch itself sounds at once, being early in a lit step; every
        // strike after it is the roll's.
        QVERIFY(rig.gateOpens.size() >= 10);
        QVERIFY(rig.gateOpens.front() * 4 - std::floor(rig.gateOpens.front() * 4) < 0.5);
        rig.gateOpens.erase(rig.gateOpens.begin());
        for (double b : rig.gateOpens) {
            const double sixteenth = b * 4;
            QVERIFY2(std::fabs(sixteenth - std::round(sixteenth)) < 0.02, qPrintable(QString::number(sixteenth)));
            QVERIFY2(std::lround(sixteenth) % 4 != 0, "the roll leaves the downbeat to the kick");
        }
    }

    // A note that lands on a rest waits for the pattern's next strike.
    void rollWaitsOnARest() {
        Rig rig;
        rig.send(shaper(bass::ShapeRoll, 0.6f, 0)); // OFFBEAT: --x-
        rig.send(noteOn(1, 0, bass::Sub));          // on the downbeat, a rest
        rig.run(60.0 / 124.0 * 2);
        QVERIFY(rig.gateOpens.size() >= 2);
        QVERIFY(std::fabs(rig.gateOpens[0] * 4 - 2) < 0.02);
        QVERIFY(std::fabs(rig.gateOpens[1] * 4 - 6) < 0.02);
        QVERIFY(rig.peak() > 0.1);
    }

    // The loop steps aside while the player holds a note, and comes back.
    void theLoopStepsAsideForTheHand() {
        Rig rig;
        rig.send(noteOn(kReplay | 1, 0, bass::Sub));
        rig.run(0.5);
        QVERIFY(std::fabs(cents(rig.pitch(), 33)) < 15);
        rig.send(noteOn(5, 4, bass::Sub));
        rig.run(0.5);
        QVERIFY(std::fabs(cents(rig.pitch(), 40)) < 15);
        QVERIFY(rig.pub[bass::PubClipMute] > 0.95f);
        rig.send(noteOff(5));
        rig.run(0.6);
        QVERIFY(std::fabs(cents(rig.pitch(), 33)) < 15);
        QVERIFY(rig.pub[bass::PubClipMute] < 0.05f);
    }

    // The player's shapers work on the loop too: an octave finger lifts it.
    void liveShapersReachTheLoop() {
        Rig rig;
        rig.send(noteOn(kReplay | 1, 0, bass::Sub));
        rig.run(0.4);
        rig.send(shaper(bass::ShapeOct, 1, 0));
        rig.run(0.5);
        QVERIFY(std::fabs(cents(rig.pitch(), 45)) < 15);
        rig.send(shaper(bass::ShapeOct, 0, 0));
        rig.run(0.5);
        QVERIFY(std::fabs(cents(rig.pitch(), 33)) < 15);
    }

    // A loop's shapers stay with its slot: the next loop starts clean.
    void loopShapesStayWithTheirSlot() {
        Rig rig;
        rig.send(shaper(bass::ShapeWob, 0.9f, 3, kReplay | (1 << 24) | 1)); // slot 2 left a wobble on
        rig.send(noteOn(kReplay | (1 << 24) | 7, 0, bass::Sub));
        rig.run(0.3);
        QVERIFY(rig.pub[bass::PubWobDepth] > 0.8f);
        rig.send(noteOff(kReplay | (1 << 24) | 7));
        rig.send(noteOn(kReplay | (2 << 24) | 7, 0, bass::Sub)); // slot 3 plays
        rig.run(0.3);
        QVERIFY(rig.pub[bass::PubWobDepth] < 0.01f);
    }

    // The wobble is locked to the tempo, at the rate the finger picks.
    void wobbleFollowsTheTempo() {
        for (int rate : {3, 5}) {
            Rig rig;
            rig.send(shaper(bass::ShapeWob, 1.0f, float(rate)));
            rig.send(noteOn(1, 0, bass::Wobble, 0, 0.8f, 0.6f));
            rig.run(2.5);
            // The top band's level every 5 ms, and its strongest movement.
            std::vector<double> env;
            for (double t = 0.5; t + 0.005 < 2.5; t += 0.005)
                env.push_back(rig.topRms(t, t + 0.005));
            double mean = 0;
            for (double v : env)
                mean += v / double(env.size());
            double bestHz = 0, best = 0;
            for (double hz = 1; hz < 20; hz += 0.05) {
                double re = 0, im = 0;
                for (size_t i = 0; i < env.size(); ++i) {
                    const double w = 2 * M_PI * hz * 0.005 * double(i);
                    re += (env[i] - mean) * std::cos(w);
                    im += (env[i] - mean) * std::sin(w);
                }
                if (re * re + im * im > best) {
                    best = re * re + im * im;
                    bestHz = hz;
                }
            }
            const double expect = 124.0 / 60.0 / bass::kRateBeats[rate];
            QVERIFY2(std::fabs(bestHz - expect) < 0.25,
                     qPrintable(QStringLiteral("rate %1: %2 Hz, expected %3").arg(rate).arg(bestHz).arg(expect)));
        }
    }

    // The 808 starts high and falls onto its note: that drop is the punch.
    void the808DropsOntoItsNote() {
        Rig rig;
        rig.send(noteOn(1, 0, bass::Boom));
        rig.run(0.6);
        auto crossings = [&](double t0, double t1) {
            int n = 0;
            for (size_t i = size_t(t0 * 48000) + 1; i < size_t(t1 * 48000); ++i)
                n += rig.left[i - 1] < 0 && rig.left[i] >= 0;
            return double(n) / (t1 - t0);
        };
        QVERIFY(crossings(0.0, 0.025) > 1.4 * crossings(0.3, 0.5));
        QVERIFY(std::fabs(cents(rig.pitch(), 33)) < 15);
    }

    // A click strikes the held note again, brighter at the front.
    void clickStrikesWithAnAccent() {
        Rig a, b;
        for (Rig *r : {&a, &b}) {
            r->send(noteOn(1, 0, bass::Acid, 0, 0.7f));
            r->run(0.6);
        }
        Command strike;
        strike.type = bass::kStrike;
        strike.id = 1;
        a.send(strike);
        a.run(0.2);
        b.run(0.2);
        QVERIFY(a.topRms(0.6, 0.66) > b.topRms(0.6, 0.66) + 5);
        QVERIFY(a.topRms(0.6, 0.66) > a.topRms(0.0, 0.06)); // harder than the note's own attack
    }

    // Fingers play and the mix stays sane.
    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 2\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.7 p=0.8 size=2 twist=40\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
        QVERIFY(take.rms(0.8, 1.8) > 0.03);
    }

    // Tap a rhythm, hold the next beat: a loop, and the studio's tempo.
    void tapThenHoldMakesALoop() {
        const auto take = testkit::play("page 0 2\n"
                                        "tap 0.5 0.3 0.4\ntap 1.0 0.4 0.4\ntap 1.5 0.3 0.4\ntap 2.0 0.5 0.4\n"
                                        "down 2.5 1 0.3 0.4\nup 2.9 1\nend 5\n");
        const PartView &bass = take.view.parts[1];
        QCOMPARE(int(bass.clips[0].state), int(ClipView::Playing));
        QCOMPARE(int(take.view.bpm), 120);
        QVERIFY(take.rms(3.5, 5) > 0.02); // the loop plays on after the hand lifts
    }

    // A line that ends on a long note of its own is not a riff to loop:
    // only landing on the riff's first note again closes it.
    void endingOnAnotherNoteDoesNotLoop() {
        const auto take = testkit::play("page 0 2\n"
                                        "tap 0.5 0.3 0.4\ntap 1.0 0.4 0.4\ntap 1.5 0.3 0.4\ntap 2.0 0.5 0.4\n"
                                        "down 2.5 1 0.6 0.4\nup 2.9 1\nend 3.5\n");
        QCOMPARE(int(take.view.parts[1].clips[0].state), int(ClipView::Empty));
    }

    // The keyboard plays the scale and picks the sound.
    void keyboardPlaysAndPicksSounds() {
        const auto take = testkit::play("page 0 2\n"
                                        "key 0.2 E\nkey 0.3 Z down\nkey 1.0 Z up\n"
                                        "key 1.2 U\nkey 1.3 B down\nend 1.8\n");
        QVERIFY(take.rms(0.4, 0.9) > 0.03);
        const float *d = take.view.parts[1].data;
        QCOMPARE(int(d[bass::PubLane + bass::PubSound]), int(bass::Boom));
        QCOMPARE(int(d[bass::PubLane + bass::PubDegree]), 4); // B is the fifth key of the low row
        QVERIFY(d[bass::PubLane + bass::PubOn] > 0);
    }

    // A wobble finger wobbles while it stays; a click with only shapers down
    // latches it; P lets go.
    void railShapersSpringBackOrLatch() {
        auto wobbling = [](const std::string &script) {
            const auto take = testkit::play(script);
            return take.view.parts[1].data[bass::PubWobDepth] > 0.05f && take.view.parts[1].data[bass::PubLfo] >= 0;
        };
        const std::string base = "page 0 2\nkey 0.1 W\ndown 0.2 1 0.3 0.3\ndown 0.4 2 0.6 0.9\n";
        QVERIFY(wobbling(base + "end 1.0\n"));
        QVERIFY(!wobbling(base + "up 0.8 2\nend 1.2\n"));
        // Two fingers in the zone: lifting one leaves the other in charge.
        QVERIFY(wobbling(base + "down 0.5 3 0.65 0.85\nup 0.8 3\nend 1.2\n"));
        QVERIFY(wobbling(base + "down 0.5 3 0.65 0.85\nup 0.8 2\nend 1.2\n"));
        // Latch: click with the wobble finger alone, lift it, then play.
        QVERIFY(wobbling("page 0 2\nkey 0.1 W\ndown 0.2 2 0.6 0.9\nclick 0.4\nup 0.7 2\n"
                         "down 0.9 1 0.3 0.3\nend 1.5\n"));
        QVERIFY(!wobbling("page 0 2\nkey 0.1 W\ndown 0.2 2 0.6 0.9\nclick 0.4\nup 0.7 2\nkey 0.8 P\n"
                          "down 0.9 1 0.3 0.3\nend 1.5\n"));
    }

    // Leaving the page ends what the fingers hold.
    void leavingThePageSilencesTheHand() {
        const auto take = testkit::play("page 0 2\ndown 0.2 1 0.3 0.5\npage 1.0 3\nend 2.0\n");
        QVERIFY(take.rms(0.4, 0.9) > 0.03);
        QVERIFY(take.rms(1.6, 2.0) < 0.002);
    }
};

TESTKIT_REGISTER(BassTests)
#include "bass_tests.moc"
