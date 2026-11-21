// The Lead page: a neck that sings in the key, legato that glides without a
// new attack, harmony that moves with the melody, and clips that replay it.

#include <cstdio>

#include <QImage>
#include <QPainter>

#include "parts.h"
#include "testkit.h"

using namespace tf;

namespace {

constexpr int kLead = 3; // the Lead's index among the parts

// The x of a neck cell, as the page lays it out (two octaves of a
// seven-note scale and the root on top, right of the left hand's column).
float cellX(int q, int per = 7) { return 0.22f + 0.78f * (float(q) + 0.5f) / float(2 * per + 1); }
// The y of a harmony row: −OCT, 3RD, 5TH, 6TH, +OCT from the bottom.
float rowY(int row) { return 0.40f + 0.60f * (float(row) + 0.5f) / 5.0f; }

std::string fmt(const char *f, double a, double b = 0, double c = 0, double d = 0) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a, b, c, d);
    return buf;
}

// Three presses of → turn the sound to Chip: a plain pulse, easy to measure.
const char *kChip = "key 0.05 Right\nkey 0.1 Right\nkey 0.15 Right\n";

// A script played through the real session, with the Lead's published
// numbers sampled every tick.
struct Trace {
    struct Tick {
        double t;
        float data[kPartData];
        int voices;
    };
    std::vector<Tick> ticks;
    testkit::Take take;
    const Tick &at(double t) const {
        for (const auto &k : ticks)
            if (k.t >= t)
                return k;
        return ticks.back();
    }
};

Trace trace(const std::string &script) {
    Trace tr;
    ScriptPlayer player;
    if (!player.load(script, &tr.take.error))
        return tr;
    Studio studio(48000.0f);
    studio.setTimeOrigin(0);
    setScriptClock(0);
    Session session(&studio);
    session.setClock(&scriptClock);
    const double end = player.end() + 0.01;
    float buf[384 * 2];
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > end)
            break;
        session.refresh();
        player.tick(t, session);
        studio.render(buf, 384);
        for (int i = 0; i < 384; ++i) {
            tr.take.left.push_back(buf[2 * i]);
            tr.take.right.push_back(buf[2 * i + 1]);
        }
        session.refresh();
        Trace::Tick k;
        k.t = t;
        std::copy(session.view().parts[kLead].data, session.view().parts[kLead].data + kPartData, k.data);
        k.voices = session.view().parts[kLead].voices;
        tr.ticks.push_back(k);
    }
    session.refresh();
    tr.take.view = session.view();
    return tr;
}

// The fundamental of the mix between two times, by autocorrelation.
double pitchHz(const std::vector<float> &left, const std::vector<float> &right, double from, double to) {
    const size_t a = size_t(from * 48000), b = std::min(left.size(), size_t(to * 48000));
    std::vector<double> x;
    for (size_t i = a; i < b; ++i)
        x.push_back(0.5 * (left[i] + right[i]));
    const int minLag = 48000 / 1500, maxLag = 48000 / 70;
    std::vector<double> r(size_t(maxLag + 2), 0.0);
    double r0 = 1e-12;
    for (double v : x)
        r0 += v * v;
    for (int lag = minLag; lag <= maxLag + 1; ++lag) {
        double s = 0;
        for (size_t i = 0; i + size_t(lag) < x.size(); ++i)
            s += x[i] * x[i + size_t(lag)];
        r[size_t(lag)] = s / r0;
    }
    // The first strong peak is the period; later ones are its multiples.
    double best = 0;
    for (int l = minLag + 1; l <= maxLag; ++l)
        best = std::max(best, r[size_t(l)]);
    int lag = 0;
    for (int l = minLag + 1; l <= maxLag; ++l)
        if (r[size_t(l)] > 0.85 * best && r[size_t(l)] >= r[size_t(l - 1)] && r[size_t(l)] >= r[size_t(l + 1)]) {
            lag = l;
            break;
        }
    if (lag == 0)
        return 1;
    const double y0 = r[size_t(lag - 1)], y1 = r[size_t(lag)], y2 = r[size_t(lag + 1)];
    const double d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
    return 48000.0 / (lag + d);
}
double pitchHz(const testkit::Take &take, double from, double to) { return pitchHz(take.left, take.right, from, to); }

double midiOf(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }

// An audio context for driving the engine directly, chord and all.
struct Fake : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    Fake() {
        tr.bpm = 120;
        tr.beatsPerSample = 120.0 / 60.0 / 48000.0;
        h.tuning.set(9, 0, 4, false);
    }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return 0; }
    int offsetOf(double, int) const override { return 0; }
    void pump(int, float) override {}
    void setChord(int root, const int *tones, int count) override {
        h.chordRoot = root;
        h.chordCount = count;
        for (int i = 0; i < count; ++i)
            h.chordTones[i] = tones[i];
        ++h.chordSeq;
    }
    const float *duck() const override { return duckBuf; }
};

Command note(int type, int id, int degree, int octave, int flags = 0, int sound = 0) {
    Command c;
    c.type = uint16_t(type);
    c.id = id;
    c.i[0] = degree;
    c.i[1] = octave;
    c.i[2] = sound;
    c.i[3] = flags;
    c.f[0] = 0.8f;
    c.f[1] = 0.5f;
    c.f[6] = 0.5f;
    return c;
}

// Renders the engine for a while; keeps the mono mix when asked.
void run(PartEngine &e, Fake &ctx, double seconds, float *data = nullptr, std::vector<float> *out = nullptr) {
    float l[kBlock], r[kBlock];
    for (int i = 0; i < int(seconds * 48000 / kBlock); ++i) {
        std::fill(l, l + kBlock, 0.0f);
        std::fill(r, r + kBlock, 0.0f);
        e.render(l, r, kBlock, ctx);
        if (out)
            for (int k = 0; k < kBlock; ++k)
                out->push_back(0.5f * (l[k] + r[k]));
    }
    if (data)
        e.publish(data);
}

// A, C, E, G on the four beats of the second bar at the studio's 124 BPM.
std::string riffInBarTwo() {
    std::string s = "page 0 4\n";
    const int qs[4] = {0, 2, 4, 6};
    for (int i = 0; i < 4; ++i)
        s += fmt("tap %.4f %.4f 0.5 len=0.2\n", (4 + i) * 60.0 / 124.0, cellX(qs[i]));
    return s;
}

// Over a playing one-bar loop (bar lines every 1.935 s from 3.871 s),
// plays the given taps after a two-bar pause (beats counted from the
// first bar line after it), presses Space, and returns what the page
// said just after and the clip the studio made.
struct Taken {
    QString status;
    float beats = 0;
};
Taken takeOverALoop(const std::vector<std::pair<double, int>> &taps, double space) {
    const double spb = 60.0 / 124.0, from = 20 * spb;
    std::string s = riffInBarTwo() + fmt("key %.4f Space\n", 8 * spb);
    for (const auto &[beat, q] : taps)
        s += fmt("tap %.4f %.4f 0.5 len=0.15\n", from + beat * spb, cellX(q));
    s += fmt("key %.4f Shift+Space\n", from + space * spb); // a new loop, not a layer
    Taken got;
    const auto take = testkit::play(s + fmt("end %.4f\n", from + space * spb + 0.3),
                                    [&](Session &ss) { got.status = ss.pageStatus(); });
    got.beats = take.view.parts[kLead].clips[1].lengthBeats;
    return got;
}

const char *soundName(int i) {
    static const char *n[8] = {"Anthem", "Pluck", "Bell", "Chip", "Vox", "Scream", "Hoover", "Big room"};
    return n[i];
}

}

class LeadTests : public QObject {
    Q_OBJECT
private slots:
    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 4\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void aFingerSingsItsDegreeInTune() {
        // A minor, the neck from A3: the fifth cell is E4, the eighth A4.
        const std::string s = std::string("page 0 4\n") + kChip + fmt("down 0.3 1 %.4f 0.5\nup 1.0 1\n", cellX(4)) +
                              fmt("down 1.2 2 %.4f 0.5\nup 1.9 2\nend 2.2\n", cellX(7));
        const auto take = testkit::play(s);
        QVERIFY(std::fabs(midiOf(pitchHz(take, 0.4, 0.5)) - 64.0) < 0.1);
        QVERIFY(std::fabs(midiOf(pitchHz(take, 1.4, 1.5)) - 69.0) < 0.1);
    }

    void aSecondFingerIsLegato() {
        // Hold C4, land E4 beside it: one voice glides there with no new
        // attack; lift E4 and the line falls back to C4.
        const std::string s = std::string("page 0 4\n") + kChip + fmt("down 0.3 1 %.4f 0.5\n", cellX(2)) +
                              fmt("down 1.0 2 %.4f 0.5\nup 1.6 2\nup 2.3 1\nend 2.6\n", cellX(4));
        const auto tr = trace(s);
        QVERIFY(std::fabs(tr.at(0.8).data[1] - 9.0f) < 0.02f);
        QVERIFY(std::fabs(tr.at(1.45).data[1] - 11.0f) < 0.02f);
        QVERIFY(std::fabs(tr.at(2.1).data[1] - 9.0f) < 0.02f);
        int most = 0;
        for (const auto &k : tr.ticks)
            if (k.t > 0.4 && k.t < 2.2)
                most = std::max(most, k.voices);
        QCOMPARE(most, 1);
        // No dip and no new attack at the hammer-on: the level just after
        // it is close to the level just before.
        const double before = tr.take.rms(0.85, 0.98), after = tr.take.rms(1.02, 1.15);
        QVERIFY(after > 0.6 * before && after < 1.6 * before);
        // And the pitch really moved there, and back.
        QVERIFY(std::fabs(midiOf(pitchHz(tr.take, 1.15, 1.3)) - 64.0) < 0.15);
        QVERIFY(std::fabs(midiOf(pitchHz(tr.take, 1.8, 1.95)) - 60.0) < 0.15);
    }

    void aStepLandsQuicklyALeapSwoops() {
        // On Anthem, a hammer-on a whole step up (A4 to B4) is within about
        // ten cents of B4 by 30 ms; a leap of an octave (to A5) is still on
        // its way there at the same moment.
        const std::string s = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.5\ndown 1.0 2 %.4f 0.5\nup 1.4 2\n", cellX(7), cellX(8)) +
                              fmt("down 1.8 3 %.4f 0.5\nup 2.6 3\nup 2.6 1\nend 2.8\n", cellX(14));
        const auto take = testkit::play(s);
        // A frame lands up to 8 ms after its script time; measure 38–60 ms on.
        QVERIFY(std::fabs(midiOf(pitchHz(take, 1.038, 1.065)) - 71.0) < 0.12);
        QVERIFY(midiOf(pitchHz(take, 1.838, 1.865)) < 80.0);
    }

    void harmonyMakesTheLineBiggerNotQuieter() {
        // Two harmony rows under a held note: louder by one to three
        // decibels, never quieter.
        auto play = [](bool harmony) {
            std::string s = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.6\nup 2.0 1\n", cellX(7));
            if (harmony)
                s += fmt("down 0.3 2 0.08 %.4f\ndown 0.3 3 0.15 %.4f\nup 2.0 2\nup 2.0 3\n", rowY(1), rowY(2));
            return testkit::play(s + "end 2.1\n");
        };
        const double alone = play(false).rms(0.8, 1.8), full = play(true).rms(0.8, 1.8);
        const double gain = 20 * std::log10(full / alone);
        QVERIFY2(gain > 0.5 && gain < 3.0, qPrintable(QString::number(gain)));
    }

    void anthemIsWideUnderANormalTouch() {
        const auto take = testkit::play("page 0 4\n" + fmt("down 0.3 1 %.4f 0.6\nup 2.0 1\nend 2.1\n", cellX(7)));
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = size_t(0.8 * 48000); i < size_t(1.8 * 48000); ++i) {
            lr += double(take.left[i]) * take.right[i];
            ll += double(take.left[i]) * take.left[i];
            rr += double(take.right[i]) * take.right[i];
        }
        const double corr = lr / std::sqrt(ll * rr);
        QVERIFY2(corr < 0.75 && corr > 0.2, qPrintable(QString::number(corr)));
    }

    void aVowelSweepKeepsItsLevel() {
        // Vox, A4, the five vowels from the bottom of the pad to the top:
        // within three decibels of each other.
        std::string s = "page 0 4\nkey 0.05 Right\nkey 0.08 Right\nkey 0.11 Right\nkey 0.14 Right\n";
        const float ys[5] = {0.02f, 0.25f, 0.5f, 0.75f, 0.98f};
        for (int i = 0; i < 5; ++i)
            s += fmt("down %.1f %.0f %.4f %.2f\n", 0.3 + 1.2 * i, 1 + i, cellX(7), ys[i]) +
                 fmt("up %.1f %.0f\n", 1.3 + 1.2 * i, 1 + i);
        const auto take = testkit::play(s + "end 6.4\n");
        double lo = 1e9, hi = 0;
        for (int i = 0; i < 5; ++i) {
            const double r = take.rms(0.6 + 1.2 * i, 1.2 + 1.2 * i);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        QVERIFY2(20 * std::log10(hi / lo) < 3.0, qPrintable(QString::number(20 * std::log10(hi / lo))));
    }

    void aSlideGlidesAndRestsInTune() {
        // Slide from A3 to D4 and stay: on the way the pitch passes between
        // the notes, at rest it settles exactly on D4.
        const std::string s = std::string("page 0 4\n") + kChip + fmt("down 0.3 1 %.4f 0.5\n", cellX(0)) +
                              fmt("glide 0.6 1.0 1 %.4f 0.5\nup 2.0 1\nend 2.2\n", cellX(3));
        const auto tr = trace(s);
        bool between = false;
        for (const auto &k : tr.ticks)
            if (k.t > 0.65 && k.t < 0.95) {
                const float p = k.data[1] - 7.0f;
                between |= p > 0.3f && std::fabs(p - std::round(p)) > 0.2f;
            }
        QVERIFY(between);
        QVERIFY(std::fabs(tr.at(1.3).data[1] - 10.0f) < 0.01f);
        QVERIFY(std::fabs(midiOf(pitchHz(tr.take, 1.3, 1.45)) - 62.0) < 0.15);
    }

    void aWiggleIsVibratoAroundTheNote() {
        // Rock the finger a millimetre each way, five times a second.
        std::string s = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.5\n", cellX(5));
        double t = 0.6;
        for (int i = 0; i < 10; ++i, t += 0.1)
            s += fmt("glide %.3f %.3f 1 %.4f 0.5\n", t, t + 0.1, cellX(5) + (i % 2 ? -0.008 : 0.008));
        s += fmt("glide %.3f %.3f 1 %.4f 0.5\nup 2.6 1\nend 2.8\n", t, t + 0.1, cellX(5));
        const auto tr = trace(s);
        float lo = 99, hi = -99;
        for (const auto &k : tr.ticks)
            if (k.t > 0.8 && k.t < 1.6) {
                lo = std::min(lo, k.data[1]);
                hi = std::max(hi, k.data[1]);
            }
        QVERIFY(hi - lo > 0.15f); // the pitch moves with the finger
        QVERIFY(hi - lo < 0.6f);  // but stays around the note
        QVERIFY(std::fabs(tr.at(2.4).data[1] - 12.0f) < 0.02f);
    }

    void harmonyRowsMoveWithTheMelodyInTheKey() {
        // 3RD held by the left hand, the melody steps A3, B3, C4: the
        // harmony walks C4, D4, E4, always two degrees of the scale above.
        std::string s = "page 0 4\n" + fmt("down 0.2 9 0.1 %.4f\n", rowY(1));
        for (int i = 0; i < 3; ++i)
            s += fmt("down %.2f %.0f %.4f 0.5\n", 0.4 + 0.5 * i, 1 + i, cellX(i)) +
                 fmt("up %.2f %.0f\n", 0.8 + 0.5 * i, 1 + i);
        s += "end 2.0\n";
        const auto tr = trace(s);
        for (int i = 0; i < 3; ++i) {
            const auto &k = tr.at(0.7 + 0.5 * i);
            QVERIFY(std::fabs(k.data[1] - float(7 + i)) < 0.02f);
            QVERIFY(std::fabs(k.data[4] - float(9 + i)) < 0.02f);
            QVERIFY(k.data[7] > 0.5f);
        }
    }

    void harmonyFollowsTheChordWhenAsked() {
        // Over E minor (degrees 4, 6, 8), following the chord, a finger on
        // F lands on the nearest chord tone, E; its 3RD is the chord's next
        // tone up, G, and its 6TH the chord tone below it, B, put up an
        // octave.
        auto spec = makeLead();
        Fake ctx;
        spec.engine->init(48000);
        const int v[3] = {4, 6, 8};
        ctx.setChord(4, v, 3);
        Command h1;
        h1.type = Command::Param;
        h1.i[0] = 0; // harmony
        h1.i[1] = 0;
        h1.f[0] = 1; // 3RD
        h1.f[1] = 1;
        spec.engine->command(h1, ctx);
        Command h2 = h1;
        h2.i[1] = 1;
        h2.f[0] = 3; // 6TH
        spec.engine->command(h2, ctx);
        spec.engine->command(note(Command::On, 1, 5, 1, 1), ctx);
        float data[kPartData] = {};
        run(*spec.engine, ctx, 0.2, data);
        QCOMPARE(data[1], 11.0f);
        QCOMPARE(data[4], 13.0f);
        QCOMPARE(data[5], 15.0f);
        // Free in the scale, F stays F and its 3RD is A.
        spec.engine->command(note(Command::Off, 1, 5, 1), ctx);
        spec.engine->command(note(Command::On, 2, 5, 1, 0), ctx);
        run(*spec.engine, ctx, 0.2, data);
        QCOMPARE(data[1], 12.0f);
        QCOMPARE(data[4], 14.0f);
    }

    void aKeyChangeRetunesAHeldNote() {
        // Commands carry degrees: change the key under a held note and it
        // moves to the new key's degree.
        auto spec = makeLead();
        Fake ctx;
        spec.engine->init(48000);
        Command on = note(Command::On, 1, 2, 2, 0, 3); // C5 in A minor, on Chip
        on.f[6] = 0;                                  // a light touch: hardly any vibrato
        spec.engine->command(on, ctx);
        std::vector<float> out;
        run(*spec.engine, ctx, 0.3, nullptr, &out);
        QVERIFY(std::fabs(midiOf(pitchHz(out, out, 0.1, 0.3)) - 72.0) < 0.1);
        ctx.h.tuning.set(9, 0, 1, false); // A major: the third is C♯
        out.clear();
        run(*spec.engine, ctx, 0.4, nullptr, &out);
        QVERIFY(std::fabs(midiOf(pitchHz(out, out, 0.2, 0.4)) - 73.0) < 0.15);
    }

    void theClickLeapsAnOctave() {
        const std::string s = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.5\n", cellX(2)) +
                              "click 0.8 len=0.6\nup 2.0 1\nend 2.2\n";
        const auto tr = trace(s);
        QVERIFY(std::fabs(tr.at(0.7).data[1] - 9.0f) < 0.02f);
        QVERIFY(std::fabs(tr.at(1.3).data[1] - 16.0f) < 0.02f);
        QVERIFY(std::fabs(tr.at(1.9).data[1] - 9.0f) < 0.02f);
    }

    void aTwistBendsAStepButASlideDoesNot() {
        // Turn the finger in place by 60° clockwise: the note bends up one
        // step of the scale. Slide another finger while its contact turns
        // the same way: it just slides.
        const std::string twist = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.5 twist=0\n", cellX(4)) +
                                  fmt("glide 0.6 0.9 1 %.4f 0.5 twist=-60\nup 1.6 1\nend 1.7\n", cellX(4));
        const auto a = trace(twist);
        QVERIFY(std::fabs(a.at(1.4).data[1] - 12.0f) < 0.02f);
        const std::string slide = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.5 twist=0\n", cellX(4)) +
                                  fmt("glide 0.6 0.9 1 %.4f 0.5 twist=-60\nup 1.6 1\nend 1.7\n", cellX(6));
        const auto b = trace(slide);
        QVERIFY(std::fabs(b.at(1.4).data[1] - 13.0f) < 0.02f);
    }

    void theBendWheelRestsOnWholeSteps() {
        // Push the BEND finger up about a step and a half and stop: the
        // bend settles on a whole number of steps; let go and it's gone.
        const std::string s = "page 0 4\n" + fmt("down 0.3 1 %.4f 0.5\n", cellX(4)) +
                              "down 0.5 2 0.05 0.15\nglide 0.6 0.8 2 0.05 0.27\nup 1.6 2\nup 2.2 1\nend 2.3\n";
        const auto tr = trace(s);
        const float bend = tr.at(1.4).data[10];
        QVERIFY(std::fabs(bend - std::round(bend)) < 0.02f);
        QVERIFY(std::round(bend) >= 1.0f);
        QVERIFY(std::fabs(tr.at(1.4).data[1] - (11.0f + std::round(bend))) < 0.03f);
        QVERIFY(std::fabs(tr.at(2.0).data[1] - 11.0f) < 0.03f);
    }

    void aThrowLeavesRepeatsRinging() {
        // The same short pluck twice, once thrown: after the note has gone,
        // the thrown one keeps ringing in its echo.
        auto play = [](bool thrown) {
            std::string s = "page 0 4\nkey 0.05 Right\n" + fmt("down 0.3 1 %.4f 0.6\nup 0.5 1\n", cellX(9));
            if (thrown)
                s += "down 0.25 2 0.17 0.3\nup 0.55 2\n";
            s += "end 3.0\n";
            return testkit::play(s);
        };
        const auto dry = play(false), wet = play(true);
        QVERIFY(wet.rms(1.5, 2.5) > 2.0 * dry.rms(1.5, 2.5));
        QVERIFY(wet.rms(1.5, 2.5) > 0.003);
    }

    void spaceOnTheNextOneLoopsThePhrase() {
        // Nothing else playing: A C E G on the beats of the second bar,
        // Space on the next ONE. The bar loops on its own line and keeps
        // the tempo it was played in.
        const auto tr = trace(riffInBarTwo() + fmt("key %.4f Space\nend 7.0\n", 8 * 60.0 / 124.0));
        const PartView &v = tr.take.view.parts[kLead];
        QCOMPARE(v.playing, 0);
        QCOMPARE(int(v.clips[0].lengthBeats), 4);
        QCOMPARE(int(std::lround(tr.take.view.bpm)), 124);
        bool sang = false;
        for (const auto &k : tr.ticks)
            sang |= k.t > 4.5 && k.data[16 * 1] > 0;
        QVERIFY(sang);
        QVERIFY(tr.take.rms(5.0, 7.0) > 0.01);
    }

    void theLandedTakeSaysWhatItBecame() {
        // Phrases of 1, 3, 5 and 6 bars, played once: the whole phrase, in
        // its own whole bars, up to its last four (↑ takes more), and the
        // page names the clip the studio made.
        const int phrases[4] = {1, 3, 5, 6}, expect[4] = {1, 3, 4, 4};
        for (int i = 0; i < 4; ++i) {
            std::vector<std::pair<double, int>> taps;
            for (int b = 0; b < 4 * phrases[i]; ++b)
                taps.push_back({b + 0.05, 7 + b % 5});
            const Taken got = takeOverALoop(taps, 4.0 * phrases[i] - 0.4);
            QCOMPARE(int(got.beats), 4 * expect[i]);
            const QString want = expect[i] == 1 ? QStringLiteral("looping 1 bar in slot 2")
                                                : QStringLiteral("looping %1 bars in slot 2").arg(expect[i]);
            QVERIFY2(got.status.contains(want), qPrintable(got.status + " / want " + want));
        }
    }

    void aRiffRepeatedFromMidBarLoopsOneBar() {
        // A one-bar riff started on the third beat and played twice: one
        // cycle of it, one bar, and the page says so.
        std::vector<std::pair<double, int>> taps;
        const int riff[4] = {7, 9, 11, 14};
        for (int pass = 0; pass < 2; ++pass)
            for (int k = 0; k < 4; ++k)
                taps.push_back({2.0 + 4 * pass + k + 0.05, riff[k]});
        const Taken got = takeOverALoop(taps, 10.0 - 0.3);
        QCOMPARE(int(got.beats), 4);
        QVERIFY2(got.status.contains(QStringLiteral("looping 1 bar in slot 2")), qPrintable(got.status));
    }

    void ordinaryPlayingNeverLoops() {
        // A riff tapped in time and its first note held on the next beat,
        // then a melody that comes back to its first note and stays: with
        // no Enter, nothing loops.
        std::string s = "page 0 4\n";
        const int qs[4] = {0, 2, 4, 6};
        for (int i = 0; i < 4; ++i)
            s += fmt("tap %.2f %.4f 0.5 len=0.2\n", 0.5 + 0.5 * i, cellX(qs[i]));
        s += fmt("down 2.5 1 %.4f 0.5\nup 3.5 1\n", cellX(0));
        const int mel[6] = {7, 9, 11, 9, 8, 7};
        for (int i = 0; i < 6; ++i)
            s += fmt("down %.2f %.0f %.4f 0.6\n", 4.0 + 0.4 * i, 10 + i, cellX(mel[i])) +
                 fmt("up %.2f %.0f\n", 4.0 + 0.4 * i + (i == 5 ? 1.5 : 0.35), 10 + i);
        s += "end 7.0\n";
        const auto take = testkit::play(s);
        for (const auto &c : take.view.parts[kLead].clips)
            QCOMPARE(int(c.state), int(ClipView::Empty));
    }

    void aLiveLineAndALoopNeverStealEachOthersLegato() {
        // A loop plays while a live finger holds a note: both sound, each on
        // its own line.
        const std::string s = riffInBarTwo() + fmt("key %.4f Space\n", 8 * 60.0 / 124.0) +
                              fmt("down 4.2 2 %.4f 0.5\nup 6.2 2\nend 6.5\n", cellX(11));
        const auto tr = trace(s);
        int both = 0;
        for (const auto &k : tr.ticks)
            if (k.t > 4.5 && k.t < 6.0)
                both += k.data[0] > 0 && k.data[16] > 0;
        QVERIFY(both > 20);
        QVERIFY(std::fabs(tr.at(5.5).data[1] - 18.0f) < 0.02f);
    }

    void theKeyboardIsAPianoInTheScale() {
        // Z is the neck's lowest root (A3), the row above starts an octave
        // up, the top row two. Overlapping keys are legato.
        const auto tr = trace("page 0 4\nkey 0.3 Z down\nkey 0.8 D down\nkey 1.0 Z up\nkey 1.6 D up\n"
                              "key 1.8 Q down\nkey 2.3 Q up\nend 2.5\n");
        QVERIFY(std::fabs(tr.at(0.6).data[1] - 7.0f) < 0.02f);
        QVERIFY(std::fabs(tr.at(1.3).data[1] - 16.0f) < 0.02f); // D: C5
        QVERIFY(std::fabs(tr.at(2.2).data[1] - 21.0f) < 0.02f); // Q: A5
        int most = 0;
        for (const auto &k : tr.ticks)
            if (k.t > 0.3 && k.t < 1.6)
                most = std::max(most, k.voices);
        QCOMPARE(most, 1);
    }

    void theGateChopsInTimeWithTheStudio() {
        // Shift+→ twice: a sixteenth-note gate. A held note pulses at four steps a
        // beat (124 BPM: 8.27 Hz), with real gaps between the pulses.
        const auto tr = trace("page 0 4\nkey 0.1 Shift+Right\nkey 0.15 Shift+Right\n" + fmt("down 0.3 1 %.4f 0.6\nup 2.3 1\nend 2.4\n", cellX(7)));
        const auto &l = tr.take.left;
        const int w = 240; // 5 ms
        std::vector<double> env;
        for (size_t i = size_t(0.6 * 48000); i + w < size_t(2.2 * 48000); i += w) {
            double s = 0;
            for (int k = 0; k < w; ++k)
                s += double(l[i + k]) * l[i + k];
            env.push_back(std::sqrt(s / w));
        }
        const double hi = *std::max_element(env.begin(), env.end());
        const double lo = *std::min_element(env.begin(), env.end());
        QVERIFY(lo < 0.1 * hi);
        // Count the pulses: rises through half the peak.
        int pulses = 0;
        for (size_t i = 1; i < env.size(); ++i)
            pulses += env[i - 1] < 0.5 * hi && env[i] >= 0.5 * hi;
        const double expected = 1.6 * 124.0 / 60.0 * 4.0;
        QVERIFY(std::abs(pulses - expected) <= 2.0);
    }

    void theSharedKeysWorkHereToo() {
        // The key left of 1 follows the chord, as on every part page (§ on
        // a Nordic keyboard, ` on a US one); \ still does.
        for (const char *key : {"§", "`", "\\"})
            testkit::play(std::string("page 0 4\nkey 0.1 ") + key + "\nend 0.3\n", [&](Session &s) {
                QVERIFY2(s.pageStatus().contains(QStringLiteral("on the chord")), key);
            });
        // Shift+← → step the trance gate, the sound stays.
        testkit::play("page 0 4\nkey 0.1 Shift+Right\nkey 0.15 Shift+Right\nend 0.3\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("gated 1/16")), qPrintable(s.pageStatus()));
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Anthem")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 4\nkey 0.1 Shift+Right\nkey 0.15 Shift+Right\nkey 0.2 Shift+Left\nend 0.3\n",
                      [](Session &s) {
                          QVERIFY2(s.pageStatus().contains(QStringLiteral("gated 1/8")), qPrintable(s.pageStatus()));
                          QVERIFY2(!s.pageStatus().contains(QStringLiteral("gated 1/8T")), qPrintable(s.pageStatus()));
                      });
        testkit::play("page 0 4\nkey 0.1 Shift+Left\nend 0.3\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("gated 1/8T")), qPrintable(s.pageStatus()));
        });
        // Shift+Return overdubs.
        const auto dub = testkit::play("page 0 4\nkey 0.1 Shift+Return\nend 0.3\n");
        QVERIFY(dub.view.parts[kLead].overdub);
        // Return holds the harmony after the finger lifts, and Delete lets it go.
        const std::string held = "page 0 4\nkey 0.1 Return\n" +
                                 fmt("down 0.3 1 %.4f 0.6\ndown 0.4 2 0.1 %.4f\nup 0.8 2\n", cellX(4), rowY(1));
        testkit::play(held + "end 1.0\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("harmony 3RD")), qPrintable(s.pageStatus()));
        });
        testkit::play(held + "key 0.9 Delete\nend 1.0\n", [](Session &s) {
            QVERIFY2(!s.pageStatus().contains(QStringLiteral("harmony 3RD")), qPrintable(s.pageStatus()));
        });
    }

    void minusAndPlusSetTheTone() {
        // The same held Anthem note, darker four steps and brighter four:
        // the sound's own sits between them in its high end (the energy of
        // the first difference against the signal's).
        auto edge = [](const std::string &keys) {
            const auto take =
                testkit::play("page 0 4\n" + keys + fmt("down 0.3 1 %.4f 0.5\nup 1.6 1\nend 1.7\n", cellX(7)));
            double e = 0, d = 0;
            for (size_t i = size_t(0.6 * 48000); i < size_t(1.5 * 48000); ++i) {
                e += double(take.left[i]) * take.left[i];
                const double diff = double(take.left[i]) - take.left[i - 1];
                d += diff * diff;
            }
            return d / std::max(e, 1e-12);
        };
        const double dark = edge("key 0.05 -\nkey 0.08 -\nkey 0.11 -\nkey 0.14 -\n");
        const double own = edge("");
        const double bright = edge("key 0.05 +\nkey 0.08 +\nkey 0.11 +\nkey 0.14 +\n");
        QVERIFY2(dark < 0.7 * own, qPrintable(QStringLiteral("%1 %2").arg(dark).arg(own)));
        QVERIFY2(bright > 1.2 * own, qPrintable(QStringLiteral("%1 %2").arg(bright).arg(own)));
    }

    void theBandUnderThePadIsClickable() {
        // On the scripts' window the pad is 562.5 px high: row 0 of the band
        // is 0.048 of it under the pad, row 1 0.119. Row 0 ends with the
        // sounds, Big room last; row 1 starts with the octave stepper (↑ at
        // its right end) and has FOLLOW a little right of the middle, HOLD
        // after it.
        testkit::play("page 0 4\nbutton 0.2 0.98 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Big room")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 4\nbutton 0.2 0.59 -0.119\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("on the chord")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 4\nbutton 0.2 0.71 -0.119\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("harmony held")), qPrintable(s.pageStatus()));
        });
        testkit::play("page 0 4\nbutton 0.2 0.115 -0.119\nend 0.5\n", [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("neck an octave up")), qPrintable(s.pageStatus()));
        });
        // A playing loop's slot, clicked, stops at the bar.
        const auto take = testkit::play(riffInBarTwo() + fmt("key %.4f Space\n", 8 * 60.0 / 124.0) +
                                        "button 6.0 0.02 -0.048\nend 9.0\n");
        QCOMPARE(take.view.parts[kLead].playing, -1);
        QVERIFY(take.view.parts[kLead].clips[0].state != ClipView::Empty);
    }

    void stillFingersSendFewEvents() {
        // A finger held still through a looped phrase leaves hardly
        // anything in the clip besides its own on and off.
        const std::string s = "page 0 4\n" + fmt("down 2.0 1 %.4f 0.5\nup 3.8 1\n", cellX(3)) +
                              "key 3.9 Space\nend 6.5\n";
        const auto take = testkit::play(s);
        const PartView &v = take.view.parts[kLead];
        int events = -1;
        for (const auto &c : v.clips)
            if (c.state != ClipView::Empty)
                events = c.events;
        QVERIFY(events >= 2);
        QVERIFY(events <= 8);
    }

    void leavingThePageEndsItsNotes() {
        const std::string s = "page 0 4\n" + fmt("down 0.2 1 %.4f 0.5\n", cellX(4)) +
                              "key 0.25 Z down\nkey 0.8 3\nend 3.0\n";
        const auto take = testkit::play(s);
        QVERIFY(take.rms(0.4, 0.7) > 0.01);
        QVERIFY(take.rms(2.5, 3.0) < 0.002);
    }

    void everySoundStaysInBoundsAtTheExtremes() {
        // Each sound pushed: top of the pad, flat finger, hard press, the
        // highest note, harmony on three rows, an octave up, thrown.
        for (int snd = 0; snd < 8; ++snd) {
            std::string s = "page 0 4\n";
            for (int k = 0; k < snd; ++k)
                s += fmt("key %.2f Right\n", 0.02 + 0.02 * k);
            s += fmt("down 0.3 11 0.08 %.4f p=1\ndown 0.3 12 0.15 %.4f p=1\ndown 0.3 13 0.08 %.4f p=1\n", rowY(4),
                     rowY(3), rowY(2));
            s += "down 0.3 14 0.17 0.35 p=1\n";
            s += fmt("down 0.4 1 %.4f 1.0 p=1 size=2.5\nglide 0.6 1.2 1 %.4f 0.0 p=1\nglide 1.2 1.6 1 %.4f 1.0\n",
                     cellX(14), cellX(0), cellX(14));
            s += "click 1.6 len=0.6\nup 2.4 1\nup 2.4 11\nup 2.4 12\nup 2.4 13\nup 2.4 14\nend 3.5\n";
            const auto take = testkit::play(s);
            QVERIFY2(take.finite(), soundName(snd));
            QVERIFY2(take.peak() < 1.0, soundName(snd));
            QVERIFY2(take.rms(0.5, 2.3) > 0.02, soundName(snd));
        }
    }

    void thePagePaints() {
        QImage img(1100, 760, QImage::Format_ARGB32);
        img.fill(Qt::black);
        Studio studio;
        Session session(&studio);
        session.setPage(kLead);
        Frame f;
        f.t = 0.1;
        f.count = 3;
        f.touches[0] = {1, cellX(5), 0.5f, 1.0f, 0.6f, 0.0f, 0.3f};
        f.touches[1] = {2, 0.1f, rowY(1), 1.0f, 0.5f, 0.0f, 0.3f};
        f.touches[2] = {3, 0.05f, 0.2f, 1.0f, 0.5f, 0.0f, 0.3f};
        session.frame(f);
        QPainter p(&img);
        session.paint(p, QRectF(40, 40, 960, 600), 0.2);
        p.end();
        QVERIFY(!session.pageHelp(kLead).isEmpty());
        QVERIFY(session.pageStatus().contains(QStringLiteral("3RD")));
    }
};

TESTKIT_REGISTER(LeadTests)
#include "lead_tests.moc"
