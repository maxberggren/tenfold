// The Bass page: its engine driven directly (pitch, low end, slides, the
// chord follower, the roll, the two lanes), and its controls through real
// scripted performances.

#include <QJsonArray>
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
    // Level in dB, relative to all of it, of lo..hi Hz between t0 and t1:
    // a DFT every 25 Hz across the band against the whole signal's power.
    double band(double t0, double t1, double lo, double hi) const {
        const size_t a = size_t(t0 * 48000), n = size_t((t1 - t0) * 48000);
        double inBand = 0, all = 0;
        for (size_t i = 0; i < n; i += 2) {
            const double v = 0.5 * (left[a + i] + right[a + i]);
            all += v * v;
        }
        for (double f = lo; f < hi; f += 25) {
            double re = 0, im = 0;
            const double w = 2 * M_PI * f / 48000.0;
            for (size_t i = 0; i < n; i += 2) {
                const double v = 0.5 * (left[a + i] + right[a + i]) * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n)));
                re += v * std::cos(w * double(i));
                im += v * std::sin(w * double(i));
            }
            inBand += re * re + im * im;
        }
        // Scaled so the sum of bins compares with the time-domain power.
        return 10 * std::log10((inBand * 2.0 * 25.0 / 24000.0 * 8.0 / 3.0 / (double(n) / 2) + 1e-20) / (all / (double(n) / 2) + 1e-20));
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

// A stretch of a take (the whole mix) as a Rig, to measure it the same way.
Rig stretch(const testkit::Take &take, double t0, double t1) {
    Rig r;
    r.left.assign(take.left.begin() + long(t0 * 48000), take.left.begin() + long(t1 * 48000));
    r.right.assign(take.right.begin() + long(t0 * 48000), take.right.begin() + long(t1 * 48000));
    return r;
}

// Power in dB between lo and hi Hz over a whole stretch (a DFT every 10 Hz).
double bandPower(const Rig &r, double lo, double hi) {
    const size_t n = r.left.size();
    double sum = 0;
    for (double f = lo; f < hi; f += 10) {
        double re = 0, im = 0;
        const double w = 2 * M_PI * f / 48000.0;
        for (size_t i = 0; i < n; i += 2) {
            const double v = 0.5 * (r.left[i] + r.right[i]) * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n)));
            re += v * std::cos(w * double(i));
            im += v * std::sin(w * double(i));
        }
        sum += re * re + im * im;
    }
    return 10 * std::log10(sum / double(n) / double(n) + 1e-20);
}

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
    c.f[5] = 3; // eighths
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
    void aNoteHeldAndLetGoBeforeSpaceLoopsWhole() {
        // Nothing else playing but the click: a note held for most of a bar,
        // let go, then Space. The loop holds it as long as it was held (it
        // was cut to the beat after it began).
        const std::string script = "page 0.05 2\nkey 0.1 Ctrl+M\n"
                                   "down 1.935 1 0.3 0.5\nup 3.774 1\nkey 3.895 Space\nend 6.0\n";
        const double spb = 60.0 / 124.0;
        int samples = 0, held = 0;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            // The loop's second time round, beats 1.5 to 3.5 into it.
            if (t > 9.5 * spb && t < 11.5 * spb) {
                ++samples;
                held += s.view().parts[1].data[bass::PubLane + 8 + bass::PubOn] == 1.0f;
            }
        });
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.parts[1].playing, 0);
        QVERIFY(samples > 0);
        QVERIFY2(held == samples, qPrintable(QStringLiteral("held %1 of %2").arg(held).arg(samples)));
    }

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

    // Height opens every sound: more above a kilohertz the higher the finger.
    void heightBrightensEverySound() {
        for (int sound = 0; sound < bass::SoundCount; ++sound) {
            double level[3];
            const float tones[3] = {0.15f, 0.5f, 0.85f};
            for (int k = 0; k < 3; ++k) {
                Rig rig;
                rig.send(noteOn(1, 0, sound, 0, 0.75f, tones[k]));
                rig.run(0.5);
                level[k] = rig.band(0.05, 0.45, 1000, 6000);
            }
            const QString what = QStringLiteral("%1: %2 %3 %4 dB").arg(bass::recipe(sound).name)
                                     .arg(level[0], 0, 'f', 1).arg(level[1], 0, 'f', 1).arg(level[2], 0, 'f', 1);
            QVERIFY2(level[1] > level[0] + 2 && level[2] > level[1] + 2, qPrintable(what));
        }
    }

    // On ACID the twist is the squelch: resonance, envelope depth and decay
    // together. Turned up, more bite between 1 and 4 kHz, and a longer tail.
    void acidTwistIsTheSquelch() {
        double body[2], tail[2];
        for (int k = 0; k < 2; ++k) {
            Rig rig;
            Command on = noteOn(1, 0, bass::Acid, 0, 0.75f, 0.4f);
            on.f[2] = k == 0 ? 0.0f : 1.0f;
            rig.send(on);
            rig.run(0.6);
            body[k] = rig.band(0.0, 0.3, 1000, 4000);
            tail[k] = rig.band(0.25, 0.55, 1000, 4000);
        }
        QVERIFY2(body[1] > body[0] + 3, qPrintable(QStringLiteral("%1 %2").arg(body[0]).arg(body[1])));
        QVERIFY2(tail[1] > tail[0] + 6, qPrintable(QStringLiteral("%1 %2").arg(tail[0]).arg(tail[1])));
    }

    // The kick ducks each sound its own way: a house sub breathes with it,
    // the 808 hardly moves (the channel's light pump comes on top of both).
    void eachSoundPumpsItsOwnWay() {
        auto drop = [](int sound) {
            Rig open, ducked;
            std::fill(std::begin(ducked.ctx.duckBuf), std::end(ducked.ctx.duckBuf), 1.0f);
            for (Rig *r : {&open, &ducked}) {
                r->send(noteOn(1, 0, sound));
                r->run(0.5);
            }
            return open.topRms(0, 0.5) - ducked.topRms(0, 0.5);
        };
        QVERIFY(drop(bass::Sub) > 4);
        QVERIFY(drop(bass::Boom) < 0.5);
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

    // An upright is plucked, never slid onto: a second finger while the
    // first still holds plucks its own note, as loud as the first, even once
    // the first has rung away. Lifting it pulls off to the note still held.
    void uprightPlucksEveryFinger() {
        Rig rig;
        rig.send(noteOn(1, 0, bass::Upright, 0, 0.8f, 0.6f)); // A1
        rig.run(1.2);
        rig.send(noteOn(2, 4, bass::Upright, 0, 0.8f, 0.6f)); // E2, the first still held
        rig.run(0.6);
        const double first = rig.topRms(0.0, 0.06), second = rig.topRms(1.2, 1.26), before = rig.topRms(1.1, 1.18);
        qInfo("first pluck %.1f dB, before the second %.1f dB, second pluck %.1f dB", first, before, second);
        QVERIFY2(second > first - 3, qPrintable(QStringLiteral("%1 vs %2 dB").arg(second).arg(first)));
        QVERIFY(second > before + 10);
        // In tune at once: no slide up from A.
        QVERIFY(std::fabs(cents(rig.pitch(0.25), 40)) < 15);
        rig.send(noteOff(2));
        rig.run(0.3);
        QVERIFY(std::fabs(cents(rig.pitch(0.2), 33)) < 15);
        QVERIFY(rig.finite());
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
    void theLoopDucksUnderTheHand() {
        // A held note over the loop: the loop plays on at half, under it.
        Rig rig;
        rig.send(noteOn(kReplay | 1, 0, bass::Sub));
        rig.run(0.5);
        QVERIFY(std::fabs(cents(rig.pitch(), 33)) < 15);
        rig.send(noteOn(5, 4, bass::Sub));
        rig.run(0.5);
        QVERIFY(std::fabs(rig.pub[bass::PubClipMute] - (1.0f - bass::kLoopUnderHand)) < 0.05f);
        QCOMPARE(int(rig.engine->voices()), 2);
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

    // Each sound is its own: over a held note, its levels in five bands at
    // the front, the middle and the tail (relative to its own loudest)
    // differ from every other sound's by several dB, so no two of the ten
    // are the same sound at another setting.
    void everySoundIsItsOwn() {
        constexpr int kBands = 5, kTimes = 3;
        const double edges[kBands + 1] = {20, 90, 300, 1000, 3000, 9000};
        const double times[kTimes] = {0.02, 0.3, 1.0};
        std::vector<std::array<double, kBands * kTimes>> shape(size_t(bass::SoundCount));
        for (int s = 0; s < bass::SoundCount; ++s) {
            Rig rig;
            rig.send(noteOn(1, 0, s, 0, 0.8f, 0.5f));
            rig.run(1.2);
            // The level of a tenth of a second, in dB: band() is relative to it.
            auto level = [&](double t0) {
                double e = 0;
                for (size_t i = size_t(t0 * 48000); i < size_t((t0 + 0.1) * 48000); ++i)
                    e += 0.25 * std::pow(rig.left[i] + rig.right[i], 2);
                return 10 * std::log10(e / 4800 + 1e-12);
            };
            double top = -1e9;
            for (int t = 0; t < kTimes; ++t)
                for (int b = 0; b < kBands; ++b) {
                    const double v = rig.band(times[t], times[t] + 0.1, edges[b], edges[b + 1]) + level(times[t]);
                    shape[size_t(s)][size_t(t * kBands + b)] = v;
                    top = std::max(top, v);
                }
            for (double &v : shape[size_t(s)])
                v = std::max(v - top, -50.0);
        }
        for (int a = 0; a < bass::SoundCount; ++a)
            for (int b = a + 1; b < bass::SoundCount; ++b) {
                double d = 0;
                for (int k = 0; k < kBands * kTimes; ++k)
                    d += std::pow(shape[size_t(a)][size_t(k)] - shape[size_t(b)][size_t(k)], 2);
                d = std::sqrt(d / (kBands * kTimes));
                QVERIFY2(d > 3.5, qPrintable(QStringLiteral("%1 and %2 differ by %3 dB")
                                               .arg(bass::recipe(a).name, bass::recipe(b).name)
                                               .arg(d, 0, 'f', 1)));
            }
    }

    // The upright's string is in tune with its sub and slides with it: its
    // second harmonic sits on twice the note, before and after a legato
    // slide, and a fresh pluck rings away while held.
    void uprightStringIsInTuneAndRingsAway() {
        // The strongest peak between lo and hi Hz over the last span seconds.
        auto peakHz = [](const Rig &rig, double lo, double hi, double span) {
            const size_t n = size_t(span * 48000), from = rig.left.size() - n;
            double best = 0, bestHz = 0;
            for (double hz = lo; hz < hi; hz += 0.05) {
                double re = 0, im = 0;
                const double w = 2 * M_PI * hz / 48000.0;
                for (size_t i = 0; i < n; i += 2) {
                    const double v = 0.5 * (rig.left[from + i] + rig.right[from + i]) *
                                     (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n)));
                    re += v * std::cos(w * double(i));
                    im += v * std::sin(w * double(i));
                }
                if (re * re + im * im > best) {
                    best = re * re + im * im;
                    bestHz = hz;
                }
            }
            return bestHz;
        };
        Rig rig;
        rig.send(noteOn(1, 0, bass::Upright, 0, 0.8f, 0.6f)); // A1
        rig.run(0.5);
        const double a2 = peakHz(rig, 100, 120, 0.4);
        QVERIFY2(std::fabs(cents(a2, 45)) < 5, qPrintable(QStringLiteral("%1 Hz").arg(a2)));
        rig.send(noteOn(2, 4, bass::Upright, 0, 0.8f, 0.6f)); // E2, slid to
        rig.run(0.5);
        const double e3 = peakHz(rig, 150, 180, 0.4);
        QVERIFY2(std::fabs(cents(e3, 52)) < 5, qPrintable(QStringLiteral("%1 Hz").arg(e3)));
        QVERIFY(rig.finite());

        // Held, a plucked string dies away; a Moog holds its note.
        auto fall = [](int sound) {
            Rig r;
            r.send(noteOn(1, 0, sound));
            r.run(1.6);
            return r.topRms(0.05, 0.15) - r.topRms(1.4, 1.5);
        };
        QVERIFY2(fall(bass::Upright) > 20, qPrintable(QStringLiteral("%1 dB").arg(fall(bass::Upright))));
        QVERIFY2(fall(bass::Moog) < 8, qPrintable(QStringLiteral("%1 dB").arg(fall(bass::Moog))));
    }

    // The twist: on the upright how long the string rings, on the Moog the
    // contour, how far its envelope opens the ladder.
    void uprightTwistRingsAndMoogTwistSweeps() {
        double tail[2], sweep[2];
        for (int k = 0; k < 2; ++k) {
            Rig up, moog;
            Command on = noteOn(1, 0, bass::Upright);
            on.f[2] = k == 0 ? 0.0f : 1.0f;
            up.send(on);
            on.i[3] = bass::Moog;
            moog.send(on);
            up.run(1.0);
            moog.run(0.4);
            tail[k] = up.topRms(0.7, 0.9);
            sweep[k] = moog.band(0.0, 0.15, 1000, 4000);
        }
        QVERIFY2(tail[1] > tail[0] + 10, qPrintable(QStringLiteral("%1 %2").arg(tail[0]).arg(tail[1])));
        QVERIFY2(sweep[1] > sweep[0] + 3, qPrintable(QStringLiteral("%1 %2").arg(sweep[0]).arg(sweep[1])));
    }

    // MOOG says "bwow": the envelope opens the ladder wide and closes it
    // fast, so at mid height what is above a kilohertz falls 10 dB in the
    // first 150 ms; CONTOUR (the twist) slows it to a wah. And a staccato
    // sixteenth of a funk line at 105 BPM, let go after 100 ms, still says
    // "pew": 8 dB down from its front before it ends.
    void moogSaysBwow() {
        // The 1–4 kHz level, absolute: the band's share of a stretch plus
        // that stretch's level.
        auto level = [](const Rig &rig, double t0, double t1) {
            double e = 0;
            for (size_t i = size_t(t0 * 48000); i < size_t(t1 * 48000); ++i)
                e += 0.25 * std::pow(rig.left[i] + rig.right[i], 2);
            return rig.band(t0, t1, 1000, 4000) + 10 * std::log10(e / ((t1 - t0) * 48000) + 1e-12);
        };
        auto fall = [&](float morph) {
            Rig rig;
            Command on = noteOn(1, 0, bass::Moog, 0, 0.8f, 0.5f);
            on.f[2] = morph;
            rig.send(on);
            rig.run(0.3);
            return level(rig, 0.0, 0.03) - level(rig, 0.12, 0.15);
        };
        const double snappy = fall(0.0f), usual = fall(0.3f), wah = fall(1.0f);
        const QString what = QStringLiteral("%1 %2 %3 dB").arg(snappy).arg(usual).arg(wah);
        QVERIFY2(usual > 10, qPrintable(what));
        QVERIFY2(snappy > usual && usual > wah + 6, qPrintable(what));

        Rig sixteenth;
        sixteenth.ctx.tr.bpm = 105;
        sixteenth.ctx.tr.beatsPerSample = 105 / 60.0 / 48000.0;
        sixteenth.send(noteOn(1, 0, bass::Moog, 0, 0.8f, 0.5f));
        sixteenth.run(0.1);
        sixteenth.send(noteOff(1));
        sixteenth.run(0.05);
        const double pew = level(sixteenth, 0.0, 0.02) - level(sixteenth, 0.07, 0.1);
        QVERIFY2(pew > 8, qPrintable(QStringLiteral("%1 dB").arg(pew)));
    }

    // The upright's string loses its top as fast on a low note as on a high
    // one (a kilohertz dies in a fraction of a second), so A1 thumps and
    // then blooms dark instead of ringing like a harpsichord.
    void uprightLowStringDarkens() {
        Rig rig;
        rig.send(noteOn(1, 0, bass::Upright, 0, 0.8f, 0.5f)); // A1
        rig.run(0.8);
        const double early = rig.band(0.02, 0.12, 640, 1280) + 10 * std::log10(1e-12 + [&] {
            double e = 0;
            for (size_t i = 960; i < 5760; ++i)
                e += 0.25 * std::pow(rig.left[i] + rig.right[i], 2);
            return e / 4800;
        }());
        const double late = rig.band(0.62, 0.72, 640, 1280) + 10 * std::log10(1e-12 + [&] {
            double e = 0;
            for (size_t i = 29760; i < 34560; ++i)
                e += 0.25 * std::pow(rig.left[i] + rig.right[i], 2);
            return e / 4800;
        }());
        QVERIFY2(early - late > 25, qPrintable(QStringLiteral("%1 dB").arg(early - late)));
    }

    // Palm-muted (Shift and its letter) or pressed lightly (a ghost), the
    // upright is a dead thud: 200 ms in, far under an open note. Letting a
    // note go knocks softly, without a click.
    void uprightMutesGhostsAndKnocks() {
        auto body = [](int flags, float vel) {
            Rig rig;
            rig.send(noteOn(1, 0, bass::Upright, flags, vel, 0.5f));
            rig.run(0.4);
            return rig.topRms(0.2, 0.3);
        };
        const double open = body(0, 0.8f), muted = body(bass::Square, 0.8f), ghost = body(0, 0.4f);
        const QString what = QStringLiteral("%1 %2 %3 dB").arg(open).arg(muted).arg(ghost);
        QVERIFY2(muted < open - 15 && ghost < open - 15, qPrintable(what));

        // The knock: the note let go after it has died away is heard, but
        // nothing in it reaches up past 6 kHz.
        Rig rig;
        rig.send(noteOn(1, 0, bass::Upright, 0, 0.8f, 0.5f));
        rig.run(1.0);
        rig.send(noteOff(1));
        rig.run(0.1);
        QVERIFY(rig.finite());
        QVERIFY2(rig.band(1.0, 1.04, 200, 1000) > rig.band(1.0, 1.04, 6000, 12000) + 30,
                 qPrintable(QStringLiteral("%1 %2").arg(rig.band(1.0, 1.04, 200, 1000)).arg(rig.band(1.0, 1.04, 6000, 12000))));
        QVERIFY(rig.topRms(1.0, 1.04) > rig.topRms(1.06, 1.1) + 6);
    }

    // The upright re-plucked fast (a roll of sixteenths) and bent far down
    // stays finite and under the peak: the string is stopped, not stacked.
    void uprightRollsAndBendsCleanly() {
        Rig rig;
        rig.send(shaper(bass::ShapeRoll, 0.6f, 4)); // DRIVE: every sixteenth
        rig.send(noteOn(1, 0, bass::Upright, bass::Accent, 1.0f, 1.0f));
        rig.run(1.5);
        rig.send(shaper(bass::ShapeBend, -24, 0));
        rig.send(shaper(bass::ShapeOct, 2, 0));
        rig.run(1.0);
        QVERIFY(rig.finite());
        QVERIFY2(rig.peak() < 0.95, qPrintable(QStringLiteral("%1").arg(rig.peak())));
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
        // Louder above the sub, and brighter: more of it between 1 and 4 kHz.
        QVERIFY(a.topRms(0.6, 0.66) > b.topRms(0.6, 0.66) + 3);
        QVERIFY(a.band(0.6, 0.66, 1000, 4000) > b.band(0.6, 0.66, 1000, 4000) + 3);
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

    // LOOP arms a take from the next bar, a second tap closes it at the bar
    // line, and it loops; the notice says what was caught.
    void loopButtonRecordsABar() {
        QString notice;
        const auto take = testkit::play("page 0 2\ntempo 0 120\n"
                                        "tap 0.3 0.05 0.9\n" // LOOP: armed, the take starts at 2.0 s
                                        "tap 2.0 0.21 0.4\ntap 2.5 0.37 0.4\ntap 3.0 0.21 0.4\n"
                                        "tap 3.2 0.05 0.9\n" // LOOP again: closes at 4.0 s
                                        "tap 3.5 0.46 0.4\nend 6\n",
                                        [&](Session &session) {
                                            uint32_t seq = 0;
                                            notice = session.notice(&seq);
                                        });
        const PartView &bass = take.view.parts[1];
        QCOMPARE(int(bass.clips[0].state), int(ClipView::Playing));
        QCOMPARE(int(bass.clips[0].lengthBeats), 4);
        QCOMPARE(int(take.view.bpm), 120);
        QVERIFY(take.rms(4.5, 6) > 0.02); // the loop plays on after the hand lifts
        QVERIFY2(notice.contains(QStringLiteral("slot 1")) && notice.contains(QStringLiteral("bar")), qPrintable(notice));
    }

    // Enter does what LOOP does; tapped twice before the bar, it makes a
    // one-bar loop.
    void enterRecordsTooAndTwiceIsOneBar() {
        const auto take = testkit::play("page 0 2\ntempo 0 120\nkey 0.3 Return\nkey 0.6 Return\n"
                                        "tap 2.0 0.21 0.4\ntap 2.5 0.37 0.4\ntap 3.0 0.21 0.4\ntap 3.5 0.46 0.4\nend 5\n");
        QCOMPARE(int(take.view.parts[1].clips[0].state), int(ClipView::Playing));
        QCOMPARE(int(take.view.parts[1].clips[0].lengthBeats), 4);
    }

    // Space over a playing loop lands at once: the page says what it
    // caught, and the LOOP button and the status line show which slot.
    void spaceTakesShowAndReport() {
        const std::string script = "page 0 2\ntempo 0 120\n"
                                   "tap 0.2 0.04 0.9\n" // LOOP: the first loop, bar 2
                                   "tap 2.0 0.21 0.4\ntap 2.5 0.37 0.4\ntap 3.0 0.04 0.9\n"
                                   "tap 4.5 0.29 0.4\ntap 5.0 0.46 0.4\n"
                                   "key 5.5 Shift+Space\n"; // a new loop beside the first
        QString status, notice;
        const auto take = testkit::play(script + "end 5.8\n", [&](Session &s) {
            status = s.pageStatus();
            uint32_t seq = 0;
            notice = s.notice(&seq);
        });
        QCOMPARE(int(take.view.parts[1].clips[1].state), int(ClipView::Playing));
        QVERIFY2(status.contains(QStringLiteral("into slot 2"), Qt::CaseInsensitive), qPrintable(status));
        QVERIFY2(notice.contains(QStringLiteral("Bass loop in slot 2")), qPrintable(notice));
    }

    // A riff that starts mid-bar, played twice over a playing loop, then
    // Space: one bar of it loops at once, with no gap where the bar line
    // would have been.
    void aRiffPlayedTwiceLoopsOneBar() {
        std::string script = "page 0 2\ntempo 0 120\n"
                             "tap 0.2 0.04 0.9\ntap 2.1 0.37 0.4\ntap 3.0 0.04 0.9\n"; // a first loop
        // Beats at 0.5 s; the riff starts on beat 10 (half way through a bar)
        // and each note lasts until the next.
        const double notes[4][3] = {{10, 0, 0.5}, {10.5, 2, 1}, {11.5, 4, 1}, {12.5, 6, 1.5}};
        int id = 10;
        for (int pass = 0; pass < 2; ++pass)
            for (const auto &n : notes) {
                const double t = (n[0] + 4 * pass) * 0.5;
                const double x = (n[1] + 2 + 0.5) / 12.0;
                ++id;
                script += "down " + std::to_string(t) + " " + std::to_string(id) + " " + std::to_string(x) +
                          " 0.4\nup " + std::to_string(t + n[2] * 0.5 - 0.01) + " " + std::to_string(id) + "\n";
            }
        script += "key 9.05 Shift+Space\nend 13.0\n";
        const auto take = testkit::play(script);
        const ClipView &clip = take.view.parts[1].clips[1];
        QCOMPARE(int(clip.state), int(ClipView::Playing));
        QCOMPARE(int(clip.lengthBeats), 4);
        for (double t = 9.1; t < 12.9; t += 0.25)
            QVERIFY2(take.rms(t, t + 0.25) > 0.015, qPrintable(QStringLiteral("a gap at %1 s").arg(t)));
    }

    // A root landed just before the bar and held through it is the loop's
    // downbeat on every repeat, and a shaper set when the take began (a
    // latched GRIT) plays in the loop after the hand lets it go.
    void heldRootAndShapersCarryIntoTheLoop() {
        // 120 BPM: bars at 0, 2, 4, 6, 8 s.
        const std::string take = "page 0 2\ntempo 0 120\n"
                                 "down 0.6 9 0.95 0.83\nclick 0.8\nup 1.0 9\n" // GRIT latched
                                 "tap 0.2 0.05 0.9\n"                              // LOOP: from the bar at 2 s
                                 "down 1.93 1 0.21 0.4 p=0.6\nup 3.4 1\n"         // A1 from beat -0.15
                                 "tap 3.0 0.05 0.9\n"                              // LOOP: closes at 4 s
                                 "key 4.5 P\n";                                    // the hand lets GRIT go
        const auto t = testkit::play(take + "end 8.5\n");
        const PartView &bass = t.view.parts[1];
        QCOMPARE(int(bass.clips[0].state), int(ClipView::Playing));
        // Each repeat's downbeat sounds: A1, and gritty.
        for (double bar : {4.0, 6.0, 8.0})
            QVERIFY2(t.rms(bar + 0.02, bar + 0.3) > 0.03, qPrintable(QString::number(bar)));
        QVERIFY(bass.data[bass::PubGrit] > 0.05f);
        QVERIFY(bass.data[bass::PubLane + 8 + bass::PubOn] > 0); // the loop's lane sounds
    }

    // The same through Space over a playing loop, pressed before the bar.
    void spaceCarriesTheHeldRootToo() {
        const std::string take = "page 0 2\ntempo 0 120\n"
                                 "tap 0.2 0.04 0.9\ntap 2.1 0.37 0.4\ntap 3.0 0.04 0.9\n" // a first loop
                                 "down 7.93 1 0.21 0.4 p=0.6\nup 9.4 1\n" // A1 from just before bar 8 s
                                 "key 9.0 Shift+Space\n";                  // loops 8-10 s at 10 s
        const auto t = testkit::play(take + "end 14.4\n");
        QCOMPARE(int(t.view.parts[1].clips[1].state), int(ClipView::Playing));
        QCOMPARE(int(t.view.parts[1].clips[1].lengthBeats), 4);
        for (double bar : {10.0, 12.0, 14.0})
            QVERIFY2(t.rms(bar + 0.02, bar + 0.3) > 0.03, qPrintable(QString::number(bar)));
    }

    // Playing never makes a loop by itself: taps and then a hold on the
    // first note, on the beat, are just a bass line.
    void playingNeverLoopsByItself() {
        const auto take = testkit::play("page 0 2\n"
                                        "tap 0.5 0.3 0.4\ntap 1.0 0.4 0.4\ntap 1.5 0.3 0.4\ntap 2.0 0.5 0.4\n"
                                        "down 2.5 1 0.3 0.4\nup 3.2 1\nend 3.5\n");
        for (int slot = 0; slot < kClipSlots; ++slot)
            QCOMPARE(int(take.view.parts[1].clips[slot].state), int(ClipView::Empty));
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
    // latches it; Delete lets go.
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
        QVERIFY(!wobbling("page 0 2\nkey 0.1 W\ndown 0.2 2 0.6 0.9\nclick 0.4\nup 0.7 2\nkey 0.8 Delete\n"
                          "down 0.9 1 0.3 0.3\nend 1.5\n"));
    }

    // A finger lands on the stop it aims at: 1/16 is 1/16 and GALLOP is
    // GALLOP, whatever the last finger left.
    void shaperStopsAreWhereTheyLand() {
        // WOB is 0.45..0.66 wide, four stops across; 1/16 is the top row's last.
        auto wobRate = [](const std::string &land) {
            const auto take = testkit::play("page 0 2\ndown 0.1 1 0.3 0.3\n" + land + "end 0.6\n");
            return int(take.view.parts[1].data[bass::PubWobRate]);
        };
        QCOMPARE(wobRate("down 0.2 2 0.6338 0.95\n"), 5);                          // 1/16
        QCOMPARE(wobRate("down 0.2 2 0.4763 0.80\n"), 2);                          // 1/4T
        QCOMPARE(wobRate("down 0.2 2 0.6338 0.95\nup 0.3 2\ndown 0.4 3 0.5813 0.95\n"), 3); // then 1/8
        QCOMPARE(wobRate("down 0.2 2 0.4763 0.95\nglide 0.3 0.4 2 0.5813 0.95\n"), 3); // slid two stops
        const auto roll = testkit::play("page 0 2\ndown 0.1 1 0.3 0.3\ndown 0.2 2 0.3712 0.95\nend 0.6\n");
        QCOMPARE(int(roll.view.parts[1].data[bass::PubRoll]) - 1, 2); // GALLOP
    }

    // An OCT-zone finger on the BEND row dives the pitch with its height.
    void bendRowDives() {
        Rig rig;
        rig.send(noteOn(1, 7, bass::Sub)); // A2, 110 Hz
        rig.send(shaper(bass::ShapeBend, -12, 0));
        rig.run(0.6);
        QVERIFY(std::fabs(cents(rig.pitch(), 33)) < 15);
    }

    // The FILTER finger rides a playing loop: the acid riff, looped with
    // Space, opens by more than 15 dB between 1 and 4 kHz as the finger
    // climbs the fader from its foot to its top over two bars, and its
    // notes play on throughout.
    void filterFingerRidesTheLoop() {
        std::string script = "page 0 2\nkey 0.1 R\n";
        // One bar of sixteenths at 120 BPM, then Space on the next one.
        const int degrees[16] = {0, -9, 7, 0, 2, -9, 3, 4, 0, -9, 7, 0, 6, -9, 4, 2};
        for (int k = 0; k < 16; ++k)
            if (degrees[k] > -9)
                script += "tap " + std::to_string(0.5 + k * 0.125) + " " +
                          std::to_string((degrees[k] + 2 + 0.5) / 12.0) + " 0.25 len=0.1\n";
        script += "key 2.5 Space\n"
                  "down 4.5 50 0.745 0.765\nglide 4.6 8.5 50 0.745 0.995\nend 10.5\n";
        const auto take = testkit::play(script);
        QCOMPARE(int(take.view.parts[1].clips[0].state), int(ClipView::Playing));
        const double before = bandPower(stretch(take, 4.6, 5.4), 1000, 4000); // the finger at the bottom
        const double after = bandPower(stretch(take, 8.5, 10.5), 1000, 4000);
        QVERIFY2(after > before + 15, qPrintable(QStringLiteral("%1 → %2 dB").arg(before).arg(after)));
        QVERIFY(take.rms(8.5, 10.5) > 0.02);
        QVERIFY(take.view.parts[1].data[bass::PubFilter] > 0.6f);
    }

    // The +12 row never bends: an octave tap that drifts stays in tune.
    void octaveTapsStayInTune() {
        const auto take = testkit::play("page 0 2\nkey 0.2 Q\ndown 0.5 1 0.5417 0.45\n" // E2
                                        "down 1.0 2 0.16 0.94\nglide 1.05 1.4 2 0.161 0.946\nend 2.4\n");
        QVERIFY2(std::fabs(cents(stretch(take, 1.6, 2.4).pitch(0.7), 52)) < 3, "E3, in tune");
    }

    // The BEND row: still within a hair of where it landed, no bend; moved
    // and held, it bends, then lands on a note of the key.
    void bendHasADeadZoneAndLandsInKey() {
        const std::string note = "page 0 2\nkey 0.2 Q\ndown 0.5 1 0.5417 0.45\n"; // E2
        const auto still = testkit::play(note + "down 1.0 2 0.16 0.82\nglide 1.05 1.3 2 0.16 0.835\nend 2.4\n");
        QVERIFY(std::fabs(cents(stretch(still, 1.6, 2.4).pitch(0.7), 40)) < 3);
        // Down 0.1: about three semitones under E2, between C# and C. It
        // settles on C, the note of A minor nearest.
        const auto bent = testkit::play(note + "down 1.0 2 0.16 0.82\nglide 1.05 1.3 2 0.16 0.72\nend 2.6\n");
        QVERIFY2(std::fabs(cents(stretch(bent, 1.8, 2.6).pitch(0.7), 36)) < 10, qPrintable(QString::number(stretch(bent, 1.8, 2.6).pitch(0.7))));
    }

    // A note a loop carries in where it had slid to starts there, rather
    // than sliding over again on every downbeat.
    void aCarriedSlideStartsWhereItEnded() {
        Rig rig;
        Command on = noteOn(kReplay | 1, 2, bass::Sub); // C2
        rig.send(on);
        Command mv;
        mv.type = Command::Move;
        mv.id = kReplay | 1;
        mv.i[0] = 0; // to A1
        mv.f[0] = 0.75f;
        mv.f[1] = 0.5f;
        mv.f[2] = 0.3f;
        rig.send(mv);
        rig.run(0.06);
        // Rising zero crossings of the first 50 ms: A1's rate, not C2's.
        int n = 0;
        for (size_t i = 481; i < size_t(0.0505 * 48000); ++i)
            n += rig.left[i - 1] < 0 && rig.left[i] >= 0;
        QVERIFY2(n >= 2 && n <= 3, qPrintable(QString::number(n)));
        QVERIFY(std::fabs(rig.pub[bass::PubMidi] - 33.0f) < 0.05f);
    }

    // GROWL folding hard: what folds back stays 45 dB under the harmonics.
    void growlFoldStaysClean() {
        Rig rig;
        rig.send(shaper(bass::ShapeGrit, 1.0f, 1.0f));
        rig.send(noteOn(1, 0, bass::Growl, 0, 0.8f, 0.6f));
        rig.run(1.0);
        const size_t n = 24000, from = rig.left.size() - n;
        double harmonic = 0, other = 0;
        for (double f = 1500; f < 12000; f += 4) {
            double re = 0, im = 0;
            const double w = 2 * M_PI * f / 48000.0;
            for (size_t i = 0; i < n; i += 2) {
                const double x = double(i) / double(n);
                const double win = 0.42 - 0.5 * std::cos(2 * M_PI * x) + 0.08 * std::cos(4 * M_PI * x);
                const double v = 0.5 * (rig.left[from + i] + rig.right[from + i]) * win;
                re += v * std::cos(w * double(i));
                im += v * std::sin(w * double(i));
            }
            const double h = f / 55.0;
            (std::fabs(h - std::round(h)) * 55.0 < 8 ? harmonic : other) += re * re + im * im;
        }
        const double db = 10 * std::log10(other / harmonic);
        QVERIFY2(db < -45, qPrintable(QStringLiteral("%1 dB").arg(db)));
    }

    // The keys every part page shares (docs/PARTS.md 4b), on the bass,
    // the primary ones (no AltGr on a Swedish keyboard) and their old
    // second keys: ← → the sound, § the chords, Shift+← → the roll,
    // Delete lets it go, - + the tone. Q to P are the ten sounds.
    void sharedKeysDoWhatTheTableSays() {
        auto probe = [](const std::string &keys, const char *name) {
            float v = 0;
            testkit::play("page 0 2\n" + keys + "end 0.4\n",
                          [&](Session &s) { v = s.probe(1, QString::fromLatin1(name)); });
            return v;
        };
        QCOMPARE(int(probe("key 0.1 Right\n", "sound")), int(bass::Reese));          // from PLUCK
        QCOMPARE(int(probe("key 0.1 Q\nkey 0.2 Left\n", "sound")), int(bass::Upright)); // round from SUB
        QCOMPARE(int(probe("key 0.1 P\nkey 0.2 Right\n", "sound")), int(bass::Sub));     // and back
        QCOMPARE(int(probe("key 0.1 O\n", "sound")), int(bass::Moog));
        QCOMPARE(int(probe("key 0.1 P\n", "sound")), int(bass::Upright));
        QCOMPARE(probe("key 0.1 O\n", "follow"), 0.0f); // O was follow's old key
        QCOMPARE(probe("key 0.1 §\n", "follow"), 1.0f);
        QCOMPARE(probe("key 0.1 `\n", "follow"), 1.0f);
        QCOMPARE(probe("key 0.1 \\\n", "follow"), 1.0f);
        // Shift+→ twice (or ] twice): the roll is on, latched on the second
        // pattern, rolling a held note; Delete lets it go.
        auto rolled = [](const std::string &keys) {
            return int(testkit::play("page 0 2\n" + keys + "down 0.3 1 0.3 0.4\nend 0.8\n")
                           .view.parts[1]
                           .data[bass::PubRoll]);
        };
        QCOMPARE(rolled("key 0.1 Shift+Right\nkey 0.2 Shift+Right\n"), 2);
        QCOMPARE(rolled("key 0.1 ]\nkey 0.2 ]\n"), 2);
        QCOMPARE(rolled("key 0.1 ]\nkey 0.2 ]\nkey 0.25 Shift+Left\n"), 1);
        QCOMPARE(rolled("key 0.1 ]\nkey 0.2 ]\nkey 0.5 Delete\n"), 0);
        // - + are the tone (= too); Shift+↑ ↓ the slide; Shift and a sound's
        // letter turns its saw square.
        auto notice = [](const std::string &keys) {
            QString n;
            testkit::play("page 0 2\n" + keys + "end 0.4\n", [&](Session &s) { n = s.notice(nullptr); });
            return n;
        };
        QCOMPARE(notice("key 0.1 +\n"), QStringLiteral("Keyboard tone 70%"));
        QCOMPARE(notice("key 0.1 =\n"), QStringLiteral("Keyboard tone 70%"));
        QCOMPARE(notice("key 0.1 -\n"), QStringLiteral("Keyboard tone 50%"));
        QVERIFY(notice("key 0.1 Shift+Up\n").startsWith(QStringLiteral("Slide ")));
        QVERIFY(notice("key 0.1 Shift+Up\n") != notice("key 0.1 Shift+Down\n"));
        QCOMPARE(notice("key 0.1 Shift+E\n").toLower(), QStringLiteral("reese: square"));
    }

    // Return with a shaper held latches it, as a click does, and makes no
    // loop; with nothing held it is still LOOP (enterRecordsToo…).
    void returnLatchesHeldShapers() {
        const auto take = testkit::play("page 0 2\nkey 0.1 W\ndown 0.2 2 0.6 0.9\nkey 0.4 Return\nup 0.7 2\n"
                                        "down 0.9 1 0.3 0.3\nend 1.5\n");
        const PartView &bass = take.view.parts[1];
        QVERIFY(bass.data[bass::PubWobDepth] > 0.05f && bass.data[bass::PubLfo] >= 0);
        QCOMPARE(bass.recording, -1);
        for (int slot = 0; slot < kClipSlots; ++slot)
            QCOMPARE(int(bass.clips[slot].state), int(ClipView::Empty));
    }

    // The band under the pad, with the mouse. On the scripts' window the
    // pad is 562.5 px high: row 0 is 0.048 under it, row 1 0.119. The
    // sounds fill row 0 from 0.38 of the width, ten across; row 1 is five
    // steppers (0.11, 0.14, 0.12, 0.12, 0.12 wide, the left half down, the
    // right half up), then five switches.
    void theBandUnderThePadIsClickable() {
        // ACID, the fourth sound, and UPRIGHT, the last.
        testkit::play("page 0 2\nbutton 0.2 0.597 -0.048\nend 0.4\n", [](Session &s) {
            QCOMPARE(int(s.probe(1, QStringLiteral("sound"))), int(bass::Acid));
        });
        testkit::play("page 0 2\nbutton 0.2 0.969 -0.048\nend 0.4\n", [](Session &s) {
            QCOMPARE(int(s.probe(1, QStringLiteral("sound"))), int(bass::Upright));
        });
        // FOLLOW, the second switch.
        testkit::play("page 0 2\nbutton 0.2 0.727 -0.119\nend 0.4\n", [](Session &s) {
            QCOMPARE(s.probe(1, QStringLiteral("follow")), 1.0f);
            QVERIFY2(s.pageStatus().contains(QStringLiteral("follows the chords")), qPrintable(s.pageStatus()));
        });
        // ROLL's right half twice, then a note: it rolls the second pattern.
        QCOMPARE(int(testkit::play("page 0 2\nbutton 0.2 0.22 -0.119\nbutton 0.3 0.22 -0.119\n"
                                   "down 0.5 1 0.3 0.4\nend 0.9\n")
                         .view.parts[1]
                         .data[bass::PubRoll]),
                 2);
    }

    // The mouse is one finger: it cannot hold a shaper and click. LATCH
    // clicked with nothing held arms instead, and the next shaper let go
    // stays; LET GO frees it.
    void latchFromTheBandKeepsTheNextShaper() {
        auto wobbling = [](const std::string &script) {
            const auto take = testkit::play(script);
            return take.view.parts[1].data[bass::PubWobDepth] > 0.05f && take.view.parts[1].data[bass::PubLfo] >= 0;
        };
        const std::string latch = "page 0 2\nkey 0.05 W\nbutton 0.1 0.805 -0.119\ndown 0.2 2 0.6 0.9\nup 0.5 2\n";
        QVERIFY(wobbling(latch + "down 0.9 1 0.3 0.3\nend 1.5\n"));
        QVERIFY(!wobbling(latch + "button 0.7 0.961 -0.119\ndown 0.9 1 0.3 0.3\nend 1.5\n"));
        // Unarmed, the same finger springs back.
        QVERIFY(!wobbling("page 0 2\nkey 0.05 W\ndown 0.2 2 0.6 0.9\nup 0.5 2\ndown 0.9 1 0.3 0.3\nend 1.5\n"));
    }

    // Leaving the page ends what the fingers hold.
    void leavingThePageSilencesTheHand() {
        const auto take = testkit::play("page 0 2\ndown 0.2 1 0.3 0.5\npage 1.0 3\nend 2.0\n");
        QVERIFY(take.rms(0.4, 0.9) > 0.03);
        QVERIFY(take.rms(1.6, 2.0) < 0.002);
    }

    // A song saved and opened again keeps the bass as it was set: REESE with
    // its saw squared, up an octave, the tone up, the slide shorter, more
    // morph, following the chords, a roll latched from the keys and a
    // wobble latched with a click.
    void aSongKeepsTheBassAsItWasSet() {
        QJsonObject home;
        testkit::play("end 0.1\n", [&](Session &s) { home = bassSettings(s.saveSong()); });
        const char *script = "page 0 2\nkey 0.1 E\nkey 0.15 Shift+E\nkey 0.2 Up\nkey 0.25 +\nkey 0.3 Shift+Down\n"
                             "key 0.35 Shift+]\nkey 0.4 `\nkey 0.45 ]\nkey 0.5 ]\n"
                             "down 0.6 2 0.55 0.9\nclick 0.7\nup 0.8 2\nend 1.2\n";
        testkit::reopen(script, 1.0, [&](Session &, Session &opened, const QJsonObject &doc) {
            QCOMPARE(doc[QStringLiteral("parts")].toArray()[1].toObject()[QStringLiteral("name")].toString(),
                     QStringLiteral("Bass"));
            const QJsonObject settings = bassSettings(doc);
            QCOMPARE(bassSettings(opened.saveSong()), settings);
            QVERIFY(!settings.isEmpty() && settings != home);
            QCOMPARE(settings[QStringLiteral("sound")].toString(), QStringLiteral("REESE"));
            QCOMPARE(settings[QStringLiteral("octave")].toInt(), 1);
            QCOMPARE(settings[QStringLiteral("follow")].toBool(), true);
            QCOMPARE(settings[QStringLiteral("square")].toArray(), QJsonArray{QStringLiteral("REESE")});
            const QJsonObject latched = settings[QStringLiteral("latched")].toObject();
            QCOMPARE(latched[QStringLiteral("roll")].toObject()[QStringLiteral("pattern")].toString(),
                     QStringLiteral("SKIP"));
            QVERIFY(latched[QStringLiteral("wobble")].isObject());
            // The page and the engine have it back.
            QCOMPARE(int(opened.probe(1, QStringLiteral("sound"))), int(bass::Reese));
            QCOMPARE(opened.probe(1, QStringLiteral("follow")), 1.0f);
            QCOMPARE(int(opened.view().parts[1].data[bass::PubRoll]), 2);
            const QString status = opened.pageStatus(1);
            QVERIFY2(status.contains(QStringLiteral("roll latched")) && status.contains(QStringLiteral("wob latched")),
                     qPrintable(status));
        });
    }

    // A file edited by hand: wrong types keep the default, numbers are
    // clamped, unknown names are ignored, and nothing breaks.
    void aBadBassInAFileOpensSane() {
        QJsonObject home;
        testkit::play("end 0.1\n", [&](Session &s) { home = bassSettings(s.saveSong()); });
        testkit::play("page 0 2\nkey 0.1 ]\nend 0.3\n", [&](Session &s) {
            QJsonObject doc = s.saveSong();
            const QJsonObject garbage = QJsonDocument::fromJson(R"({
                "sound": 5, "octave": 1e300, "tone": -50, "slide": "short", "follow": 1,
                "morph": {"ACID": 99, "SUB": "x", "NOPE": 0.5},
                "square": ["NOPE", 3, "acid", "SUB"],
                "roll": {"pattern": "NOPE", "gate": 1e9},
                "wobble": {"rate": 7, "depth": -1},
                "grit": [1, 2],
                "latched": {"octave": 99, "roll": {"pattern": 7e9}, "filter": {"cutoff": -9, "morph": 9},
                            "wobble": true, "bogus": {}},
                "extra": null
            })").object();
            QVERIFY(!garbage.isEmpty());
            QJsonArray parts = doc[QStringLiteral("parts")].toArray();
            QJsonObject bassPart = parts[1].toObject();
            bassPart[QStringLiteral("settings")] = garbage;
            parts[1] = bassPart;
            doc[QStringLiteral("parts")] = parts;
            QString error;
            QVERIFY2(s.openSong(doc, &error), qPrintable(error));
            const QJsonObject got = bassSettings(s.saveSong());
            QCOMPARE(got[QStringLiteral("sound")].toString(), QStringLiteral("PLUCK"));
            QCOMPARE(got[QStringLiteral("octave")].toInt(), 2);
            QCOMPARE(got[QStringLiteral("tone")].toDouble(), 0.0);
            QCOMPARE(got[QStringLiteral("slide")].toDouble(), 0.5);
            QCOMPARE(got[QStringLiteral("follow")].toBool(), false);
            const QJsonObject morph = got[QStringLiteral("morph")].toObject();
            QCOMPARE(morph[QStringLiteral("ACID")].toDouble(), 1.0);
            QCOMPARE(morph[QStringLiteral("SUB")].toDouble(), 0.3);
            QVERIFY(!morph.contains(QStringLiteral("NOPE")));
            // SUB has no saw to turn square.
            QCOMPARE(got[QStringLiteral("square")].toArray(), QJsonArray{QStringLiteral("ACID")});
            const QJsonObject roll = got[QStringLiteral("roll")].toObject();
            QCOMPARE(roll[QStringLiteral("pattern")].toString(), QStringLiteral("PSY"));
            QCOMPARE(roll[QStringLiteral("gate")].toDouble(), 1.0);
            const QJsonObject wobble = got[QStringLiteral("wobble")].toObject();
            QCOMPARE(wobble[QStringLiteral("rate")].toString(), QStringLiteral("1/8"));
            QCOMPARE(wobble[QStringLiteral("depth")].toDouble(), 0.05);
            QCOMPARE(got[QStringLiteral("grit")].toObject()[QStringLiteral("amount")].toDouble(), 0.5);
            const QJsonObject latched = got[QStringLiteral("latched")].toObject();
            QCOMPARE(latched.keys(), (QStringList{QStringLiteral("filter"), QStringLiteral("octave"), QStringLiteral("roll")}));
            QCOMPARE(latched[QStringLiteral("octave")].toInt(), 2);
            QCOMPARE(latched[QStringLiteral("roll")].toObject()[QStringLiteral("pattern")].toString(), QStringLiteral("PSY"));
            QCOMPARE(latched[QStringLiteral("filter")].toObject()[QStringLiteral("cutoff")].toDouble(), -0.7);
            QCOMPARE(latched[QStringLiteral("filter")].toObject()[QStringLiteral("morph")].toDouble(), 1.0);
            // Settings that aren't an object at all: a new page's.
            parts = doc[QStringLiteral("parts")].toArray();
            bassPart = parts[1].toObject();
            bassPart[QStringLiteral("settings")] = QStringLiteral("nope");
            parts[1] = bassPart;
            doc[QStringLiteral("parts")] = parts;
            QVERIFY2(s.openSong(doc, &error), qPrintable(error));
            QCOMPARE(bassSettings(s.saveSong()), home);
            QVERIFY(!s.pageStatus(1).contains(QStringLiteral("latched")));
        });
    }

private:
    static QJsonObject bassSettings(const QJsonObject &doc) {
        return doc[QStringLiteral("parts")].toArray()[1].toObject()[QStringLiteral("settings")].toObject();
    }
};

TESTKIT_REGISTER(BassTests)
#include "bass_tests.moc"
