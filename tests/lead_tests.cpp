// The Lead page: a neck that sings in the key, legato that glides without a
// new attack, harmony that moves with the melody, and clips that replay it.

#include <climits>
#include <cstdio>

#include <QImage>
#include <QJsonArray>
#include <QPainter>

#include "lead.h"
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

// The Lead's settings in a song file.
QJsonObject leadSettings(const QJsonObject &doc) {
    return doc[QStringLiteral("parts")].toArray()[kLead].toObject()[QStringLiteral("settings")].toObject();
}
QJsonObject withLeadSettings(QJsonObject doc, const QJsonObject &settings) {
    QJsonArray parts = doc[QStringLiteral("parts")].toArray();
    QJsonObject lead = parts[kLead].toObject();
    lead[QStringLiteral("settings")] = settings;
    parts[kLead] = lead;
    doc[QStringLiteral("parts")] = parts;
    return doc;
}

const char *soundName(int i) { return lead::sound(i).name; }

// A script that turns to a sound with → and holds a cell of the neck (in
// A minor: 0 is A3, 7 A4, 11 E5) from 0.3 s to 1.5 s.
std::string hold(int snd, int cell, double y = 0.6, double press = 0.55) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < snd; ++k)
        s += fmt("key %.2f Right\n", 0.02 + 0.02 * k);
    return s + fmt("down 0.3 1 %.4f %.3f p=%.3f\nup 1.5 1\nend 1.8\n", cellX(cell), y, press);
}
std::string holdA4(int snd) { return hold(snd, 7); }

// How bright a stretch is: the energy of its slope against its own, which
// rises with every harmonic it has.
double brightness(const testkit::Take &take, double from, double to) {
    const size_t a = size_t(from * 48000), b = std::min(take.left.size(), size_t(to * 48000));
    double e = 1e-12, d = 0;
    for (size_t i = a + 1; i < b; ++i) {
        const double x = take.left[i] + take.right[i], dx = x - (take.left[i - 1] + take.right[i - 1]);
        e += x * x;
        d += dx * dx;
    }
    return d / e;
}

// A sound's shape while a note sounds: the level (dB under the loudest) at
// every half harmonic of A4 (by default up to the eighth harmonic), so an octave-down
// oscillator and the air between the harmonics count as much as the
// harmonics themselves. By Goertzel, over a Hann window.
std::vector<double> profile(const testkit::Take &take, double from, double to, int halves = 16) {
    const size_t a = size_t(from * 48000), b = std::min(take.left.size(), size_t(to * 48000));
    std::vector<double> out;
    double top = 1e-12;
    for (int k = 1; k <= halves; ++k) {
        const double w = 2 * 3.14159265358979 * 220.0 * k / 48000.0, c = 2 * std::cos(w);
        double s1 = 0, s2 = 0;
        for (size_t i = a; i < b; ++i) {
            const double hann = 0.5 - 0.5 * std::cos(2 * 3.14159265358979 * double(i - a) / double(b - a));
            const double s0 = 0.5 * (take.left[i] + take.right[i]) * hann + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        out.push_back(std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 1e-24)));
        top = std::max(top, out.back());
    }
    for (double &v : out)
        v = std::max(20 * std::log10(v / top), -60.0);
    return out;
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

    void aLoopsLayersAreLinesOfTheirOwn() {
        // A loop's first take holds a note; a note layered on top of it (the
        // same slot, layer 1) sounds beside it on its own line, instead of
        // taking the first take's line over (a legato slide away from it).
        auto spec = makeLead();
        Fake ctx;
        spec.engine->init(48000);
        const int layer0 = (1 << 30) | 5, layer1 = (1 << 30) | (1 << 27) | 5;
        spec.engine->command(note(Command::On, layer0, 2, 1), ctx);
        float data[kPartData] = {};
        run(*spec.engine, ctx, 0.15, data);
        const float first = data[lead::kLineData * 1 + 1];
        spec.engine->command(note(Command::On, layer1, 6, 1), ctx);
        run(*spec.engine, ctx, 0.3, data);
        // Slot 1 still shows the first take's note, held, at its pitch...
        QCOMPARE(data[lead::kLineData * 1 + 0], 1.0f);
        QVERIFY(std::fabs(data[lead::kLineData * 1 + 1] - first) < 0.02f);
        // ...and both sound.
        QVERIFY2(data[lead::kVoiceCount] >= 2.0f, qPrintable(QStringLiteral("voices %1 at %2, layer line on %3").arg(data[lead::kVoiceCount]).arg(lead::kVoiceCount).arg(data[lead::kLineData * lead::shownLine(0, 1)])));
        // The layer's note ends; the first take's goes on.
        spec.engine->command(note(Command::Off, layer1, 6, 1), ctx);
        run(*spec.engine, ctx, 0.2, data);
        QCOMPARE(data[lead::kLineData * 1 + 0], 1.0f);
        QVERIFY(std::fabs(data[lead::kLineData * 1 + 1] - first) < 0.02f);
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
        // sounds, more of them than fit: a window onto them that follows
        // the chosen one, so a click on its last chip (faded, with more
        // beyond) moves along, and enough of them reach Accordion, the last;
        // row 1 starts with the octave stepper (↑ at its right end) and has
        // FOLLOW about 0.64 across, HOLD after it.
        testkit::play("page 0 4\nbutton 0.2 0.98 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(!s.pageStatus().contains(QStringLiteral("Anthem")), qPrintable(s.pageStatus()));
        });
        {
            std::string walk = "page 0 4\n";
            for (int k = 0; k < 12; ++k)
                walk += fmt("button %.2f 0.98 -0.048\n", 0.2 + 0.1 * k);
            testkit::play(walk + "end 1.6\n", [](Session &s) {
                QVERIFY2(s.pageStatus().contains(QStringLiteral("Accordion")), qPrintable(s.pageStatus()));
            });
        }
        testkit::play("page 0 4\nbutton 0.2 0.635 -0.119\nend 0.5\n", [](Session &s) {
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
        for (int snd = 0; snd < lead::kSounds; ++snd) {
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

    void everySoundSingsInTune() {
        // A held note low, in the middle and high on the neck, on each
        // sound, before its vibrato has grown: on its pitch within a few
        // cents and in its own octave (an octave-down oscillator must not
        // make it sound an octave below its cell), and finite.
        const struct {
            int cell;
            double midi;
        } notes[] = {{0, 57.0}, {7, 69.0}, {11, 76.0}};
        for (int snd = 0; snd < lead::kSounds; ++snd)
            for (const auto &n : notes) {
                const auto take = testkit::play(hold(snd, n.cell));
                const double midi = midiOf(pitchHz(take, 0.62, 0.8));
                QVERIFY2(std::fabs(midi - n.midi) < 0.1,
                         qPrintable(QStringLiteral("%1 %2: %3").arg(soundName(snd)).arg(n.midi).arg(midi)));
                QVERIFY2(take.finite(), soundName(snd));
                QVERIFY2(take.rms(0.32, 0.6) > 0.02, soundName(snd));
            }
    }

    void theNewSoundsAreTheirOwn() {
        // Mini and Flute, held, each sound unlike every other: their
        // spectra in the first sixty milliseconds and once the note is held
        // (at the half harmonics, up to the sixteenth harmonic while held,
        // so a sub, breath, an attack's flash and the brightness all count)
        // stand apart from each of the eight before them and from each other.
        std::vector<std::vector<double>> shape;
        for (int snd = 0; snd < lead::kSounds; ++snd) {
            const auto take = testkit::play(holdA4(snd));
            auto s = profile(take, 0.3, 0.36);
            const auto held = profile(take, 0.65, 0.9, 32);
            s.insert(s.end(), held.begin(), held.end());
            shape.push_back(s);
        }
        for (int a : {8, 9})
            for (int b = 0; b < lead::kSounds; ++b) {
                if (a == b)
                    continue;
                double d = 0;
                for (size_t k = 0; k < shape[size_t(a)].size(); ++k) {
                    const double e = shape[size_t(a)][k] - shape[size_t(b)][k];
                    d += e * e;
                }
                d = std::sqrt(d / double(shape[size_t(a)].size()));
                QVERIFY2(d > 8.0, qPrintable(QStringLiteral("%1 vs %2: %3 dB").arg(soundName(a), soundName(b)).arg(d)));
            }
    }

    void theMiniAndTheFluteAnswerTheTouch() {
        // The Mini glides even between detached notes, as an old mono
        // synth's portamento does: just after the second note lands, the
        // pitch is still on its way up from the first; it settles in tune.
        std::string mini = "page 0 4\n";
        for (int k = 0; k < 8; ++k)
            mini += fmt("key %.2f Right\n", 0.02 + 0.02 * k);
        mini += fmt("down 0.3 1 %.4f 0.6\nup 0.7 1\n", cellX(0)) + fmt("down 0.72 2 %.4f 0.6\nup 1.9 2\nend 2.0\n", cellX(4));
        const auto m = testkit::play(mini);
        const double early = midiOf(pitchHz(m, 0.725, 0.75)), late = midiOf(pitchHz(m, 1.45, 1.85));
        QVERIFY2(std::fabs(late - 64.0) < 0.1, qPrintable(QString::number(late)));
        QVERIFY2(early < late - 1.0, qPrintable(QStringLiteral("%1 %2").arg(early).arg(late)));
        // Its touch is its character: low on the pad, a light finger
        // whistles a near sine; a hard press quacks the filter open.
        const auto light = testkit::play(hold(8, 11, 0.15, 0.25)), hard = testkit::play(hold(8, 11, 0.15, 0.95));
        QVERIFY2(brightness(hard, 0.3, 0.36) > 4.0 * brightness(light, 0.3, 0.36),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(hard, 0.3, 0.36)).arg(brightness(light, 0.3, 0.36))));
        QVERIFY2(light.rms(0.5, 1.4) > 0.02, "the whistle is heard");
        // The Flute's tongued attack is a puff of air: noise between the
        // harmonics stands higher in its first fifty milliseconds than once
        // the note is blowing.
        const auto f = testkit::play(holdA4(9));
        auto air = [&](double from, double to) {
            const auto p = profile(f, from, to);
            double odd = 0;
            for (size_t k = 2; k < p.size(); k += 2) // 660, 1100, … Hz: between the harmonics
                odd += p[k];
            return odd / double(p.size() / 2 - 1);
        };
        QVERIFY2(air(0.3, 0.36) > air(0.9, 1.2) + 6.0, qPrintable(QStringLiteral("%1 %2").arg(air(0.3, 0.36)).arg(air(0.9, 1.2))));
        // A low note is reedy, not an ocarina: at A3 its third harmonic
        // stands within a dozen decibels of the loudest.
        const auto low = profile(testkit::play(hold(9, 0)), 0.9, 1.2); // half harmonics of A4 are A3's harmonics
        QVERIFY2(low[2] > -12.0, qPrintable(QString::number(low[2])));
        // A hard press tongues a quicker attack than a light one, for runs.
        const auto soft = testkit::play(hold(9, 7, 0.6, 0.4)), firm = testkit::play(hold(9, 7, 0.6, 0.95));
        QVERIFY2(firm.rms(0.3, 0.33) > 1.5 * soft.rms(0.3, 0.33) * firm.rms(0.6, 0.9) / soft.rms(0.6, 0.9),
                 qPrintable(QStringLiteral("%1 %2").arg(firm.rms(0.3, 0.33)).arg(soft.rms(0.3, 0.33))));
    }

    // --- the five from beyond EDM: Sitar, Steel pan, Sync, Erhu, Accordion ---

    void theWorldSoundsSingInTheKeyAndSaveByName() {
        // Each of the five, held on A4: on its pitch, finite, under full
        // scale, and heard. Then chosen, saved with a song and opened in a
        // fresh tenfold, it comes back by its name and the page says so.
        for (int snd = 10; snd < lead::kSounds; ++snd) {
            const auto take = testkit::play(holdA4(snd));
            const double midi = midiOf(pitchHz(take, 0.62, 0.8));
            QVERIFY2(std::fabs(midi - 69.0) < 0.1, qPrintable(QStringLiteral("%1: %2").arg(soundName(snd)).arg(midi)));
            QVERIFY2(take.finite(), soundName(snd));
            QVERIFY2(take.peak() < 1.0, soundName(snd));
            QVERIFY2(take.rms(0.32, 1.4) > 0.02, soundName(snd));
            std::string s = "page 0 4\n";
            for (int k = 0; k < snd; ++k)
                s += fmt("key %.2f Right\n", 0.02 + 0.02 * k);
            const QString name = QString::fromUtf8(soundName(snd));
            testkit::reopen(s + "end 0.5\n", 0.3, [&](Session &, Session &opened, const QJsonObject &doc) {
                QCOMPARE(leadSettings(doc)[QStringLiteral("sound")].toString(), name);
                QCOMPARE(leadSettings(opened.saveSong())[QStringLiteral("sound")].toString(), name);
                QVERIFY2(opened.pageStatus().contains(name), qPrintable(opened.pageStatus()));
            });
        }
    }

    void theSitarBuzzesAndBendsAndItsStringsRingAlong() {
        // The jawari: the stroke buzzes bright and sweetens as it rings.
        const auto a = testkit::play(holdA4(10));
        QVERIFY2(brightness(a, 0.32, 0.42) > 2.0 * brightness(a, 1.1, 1.4),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(a, 0.32, 0.42)).arg(brightness(a, 1.1, 1.4))));
        // Still a buzz, not a plain string: well into the note its upper
        // harmonics (the ninth to the sixteenth) stand closer to the
        // fundamental than a Pluck's do.
        auto upper = [](const testkit::Take &t) {
            const auto p = profile(t, 0.7, 0.9, 32);
            double u = 0;
            for (int h = 9; h <= 16; ++h)
                u += p[size_t(2 * h - 1)];
            return u / 8.0 - p[1];
        };
        const double sitarUpper = upper(a), pluckUpper = upper(testkit::play(holdA4(1)));
        QVERIFY2(sitarUpper > pluckUpper + 10.0, qPrintable(QStringLiteral("%1 %2").arg(sitarUpper).arg(pluckUpper)));
        // Meend: a hammer-on a fifth up bends the ringing string there
        // slowly, no new stroke; it settles in tune.
        std::string meend = "page 0 4\n";
        for (int k = 0; k < 10; ++k)
            meend += fmt("key %.2f Right\n", 0.02 + 0.02 * k);
        meend += fmt("down 0.3 1 %.4f 0.6\ndown 0.8 2 %.4f 0.6\nup 2.0 2\nup 2.0 1\nend 2.2\n", cellX(0), cellX(4));
        const auto m = testkit::play(meend);
        const double early = midiOf(pitchHz(m, 0.83, 0.88)), late = midiOf(pitchHz(m, 1.4, 1.6));
        QVERIFY2(std::fabs(late - 64.0) < 0.15, qPrintable(QString::number(late)));
        QVERIFY2(early < late - 1.0, qPrintable(QStringLiteral("%1 %2").arg(early).arg(late)));
        // The sympathetic strings, tuned to A and E: a note on the key's
        // root leaves them humming after the string is damped, a B (whose
        // harmonics miss theirs) far less so.
        auto tail = [](int cell) {
            const auto t = testkit::play(hold(10, cell));
            return t.rms(1.75, 1.8) / t.rms(1.2, 1.4);
        };
        const double onRoot = tail(7), offRoot = tail(8);
        QVERIFY2(onRoot > 1.5 * offRoot, qPrintable(QStringLiteral("%1 %2").arg(onRoot).arg(offRoot)));
    }

    void theSteelPanRollsAHeldNote() {
        // A held note is rolled: its level, in five-millisecond steps,
        // rises a dozen or so times a second; a short stroke isn't.
        auto strokes = [](const testkit::Take &t, double from, double to) {
            std::vector<double> e;
            for (double x = from; x < to; x += 0.005)
                e.push_back(t.rms(x, x + 0.005));
            int peaks = 0;
            for (size_t i = 2; i + 2 < e.size(); ++i)
                if (e[i] > e[i - 1] && e[i] >= e[i + 1] && e[i] > 1.06 * e[i - 2] && e[i] > 1.06 * e[i + 2])
                    ++peaks;
            return peaks;
        };
        const auto held = testkit::play(hold(11, 7, 0.6, 0.7));
        const int rolled = strokes(held, 0.8, 1.4);
        QVERIFY2(rolled >= 5 && rolled <= 11, qPrintable(QString::number(rolled)));
        QVERIFY2(held.rms(1.0, 1.4) > 0.3 * held.rms(0.3, 0.4), "the roll sustains it");
        std::string tap = "page 0 4\n";
        for (int k = 0; k < 11; ++k)
            tap += fmt("key %.2f Right\n", 0.02 + 0.02 * k);
        const auto once = testkit::play(tap + fmt("down 0.3 1 %.4f 0.6 p=0.7\nup 0.42 1\nend 1.6\n", cellX(7)));
        QVERIFY2(strokes(once, 0.8, 1.4) <= 1, qPrintable(QString::number(strokes(once, 0.8, 1.4))));
        // Its octave blooms after the strike: the second harmonic stands
        // higher against the fundamental a little in than at once.
        auto octave = [&](double from, double to) {
            const auto p = profile(held, from, to);
            return p[3] - p[1];
        };
        QVERIFY2(octave(0.33, 0.37) > octave(0.3, 0.32) + 2.0,
                 qPrintable(QStringLiteral("%1 %2").arg(octave(0.3, 0.32)).arg(octave(0.33, 0.37))));
    }

    void theSyncLeadSweepsOnItsAttack() {
        // Every attack opens with the slave swept high: far brighter in its
        // first thirty milliseconds than held, and the finger's height
        // holds the sweep up.
        const auto mid = testkit::play(hold(12, 7, 0.5));
        QVERIFY2(brightness(mid, 0.3, 0.33) > 2.0 * brightness(mid, 0.8, 1.2),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(mid, 0.3, 0.33)).arg(brightness(mid, 0.8, 1.2))));
        const auto low = testkit::play(hold(12, 7, 0.1)), high = testkit::play(hold(12, 7, 0.95));
        QVERIFY2(brightness(high, 0.8, 1.2) > 1.5 * brightness(low, 0.8, 1.2),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(high, 0.8, 1.2)).arg(brightness(low, 0.8, 1.2))));
    }

    void theErhuSlidesInAndSingsWithADeepVibrato() {
        const auto e = testkit::play(holdA4(13));
        // It slides up into the note.
        const double start = midiOf(pitchHz(e, 0.31, 0.35));
        QVERIFY2(start < 68.8, qPrintable(QString::number(start)));
        // Its vibrato, once grown, is deep: a held note's pitch swings
        // through more than half a semitone.
        double lo = 1e9, hi = -1e9;
        for (double t = 1.0; t < 1.45; t += 0.03) {
            const double p = midiOf(pitchHz(e, t, t + 0.045));
            lo = std::min(lo, p);
            hi = std::max(hi, p);
        }
        QVERIFY2(hi - lo > 0.5, qPrintable(QStringLiteral("%1 %2").arg(lo).arg(hi)));
        QVERIFY2(hi - lo < 1.2, qPrintable(QStringLiteral("%1 %2").arg(lo).arg(hi)));
    }

    void theAccordionBeatsAtOneSpeedUpTheKeyboard() {
        // Musette: the reeds beat a fixed number of hertz apart, so the
        // tremolo of a low A and a high E has the same speed: the level's
        // own period (by autocorrelation of its ten-millisecond steps)
        // agrees within a quarter.
        auto period = [](const testkit::Take &t) {
            std::vector<double> e;
            for (double x = 0.5; x < 1.45; x += 0.01)
                e.push_back(t.rms(x, x + 0.01));
            double mean = 0;
            for (double v : e)
                mean += v;
            mean /= double(e.size());
            for (double &v : e)
                v -= mean;
            int best = 0;
            double top = 0;
            for (int lag = 10; lag <= 45; ++lag) {
                double r = 0;
                for (size_t i = 0; i + size_t(lag) < e.size(); ++i)
                    r += e[i] * e[i + size_t(lag)];
                if (r > top) {
                    top = r;
                    best = lag;
                }
            }
            return best * 0.01;
        };
        const auto low = testkit::play(hold(14, 0)), high = testkit::play(hold(14, 11));
        const double pl = period(low), ph = period(high);
        QVERIFY2(pl > 0.1 && ph > 0.1, qPrintable(QStringLiteral("%1 %2").arg(pl).arg(ph)));
        QVERIFY2(std::fabs(pl - ph) < 0.25 * pl, qPrintable(QStringLiteral("%1 %2").arg(pl).arg(ph)));
        // And it is a tremolo you hear: the level swings by more than a decibel.
        double lo = 1e9, hi = 0;
        for (double x = 0.6; x < 1.4; x += 0.01) {
            lo = std::min(lo, low.rms(x, x + 0.01));
            hi = std::max(hi, low.rms(x, x + 0.01));
        }
        QVERIFY2(20 * std::log10(hi / lo) > 1.0, qPrintable(QStringLiteral("%1 %2").arg(lo).arg(hi)));
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

    void aSongKeepsTheSettingsChosenHere() {
        // Chip, the neck an octave up, a longer glide, a sixteenth gate,
        // two steps darker, following the chord, the harmony holding; then
        // a bar looped. Saved and opened in a fresh tenfold, the page is as
        // it was and the loop plays again.
        const std::string s = riffInBarTwo() + kChip +
                              "key 0.2 Up\nkey 0.25 Shift+Up\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n"
                              "key 0.4 -\nkey 0.45 -\nkey 0.5 §\nkey 0.55 Return\n" +
                              fmt("key %.4f Space\nend 6.5\n", 8 * 60.0 / 124.0);
        testkit::reopen(s, 3.0, [](Session &before, Session &opened, const QJsonObject &doc) {
            const QJsonObject saved = leadSettings(doc);
            QCOMPARE(saved[QStringLiteral("sound")].toString(), QStringLiteral("Chip"));
            QCOMPARE(saved[QStringLiteral("octave")].toInt(), 1);
            QCOMPARE(saved[QStringLiteral("glide")].toInt(), 1);
            QCOMPARE(saved[QStringLiteral("gate")].toString(), QStringLiteral("1/16"));
            QCOMPARE(saved[QStringLiteral("tone")].toInt(), -2);
            QCOMPARE(saved[QStringLiteral("follow")].toBool(), true);
            QCOMPARE(saved[QStringLiteral("hold")].toBool(), true);
            QCOMPARE(leadSettings(opened.saveSong()), saved);
            QCOMPARE(opened.page(), before.page());
            for (const char *said : {"Chip", "neck an octave up", "on the chord", "gated 1/16", "tone −2", "harmony held"})
                QVERIFY2(opened.pageStatus().contains(QString::fromUtf8(said)), qPrintable(opened.pageStatus()));
            QCOMPARE(opened.view().parts[kLead].playing, 0);
            // Another song opened over it, one with no Lead settings at all:
            // the page goes back to its own defaults, not this song's.
            QString error;
            QVERIFY2(opened.openSong(withLeadSettings(doc, {}), &error), qPrintable(error));
            const QJsonObject home = leadSettings(opened.saveSong());
            QCOMPARE(home[QStringLiteral("sound")].toString(), QStringLiteral("Anthem"));
            QCOMPARE(home[QStringLiteral("gate")].toString(), QStringLiteral("off"));
            QCOMPARE(home[QStringLiteral("octave")].toInt(), 0);
            QCOMPARE(home[QStringLiteral("hold")].toBool(), false);
        });
    }

    void aSongWithNonsenseSettingsOpensSane() {
        // Wrong kinds, numbers far out of range, names this tenfold
        // doesn't know: each falls back to its default or into its range.
        QJsonObject doc;
        testkit::play("end 0.1\n", [&](Session &s) { doc = s.saveSong(); });
        QJsonObject bad;
        bad[QStringLiteral("sound")] = 3;
        bad[QStringLiteral("octave")] = 1e300;
        bad[QStringLiteral("glide")] = QStringLiteral("long");
        bad[QStringLiteral("gate")] = QStringLiteral("1/64");
        bad[QStringLiteral("tone")] = -1e9;
        bad[QStringLiteral("follow")] = QStringLiteral("yes");
        bad[QStringLiteral("hold")] = 1;
        bad[QStringLiteral("wobble")] = QJsonArray{1, 2, 3};
        testkit::play("page 0 4\nkey 0.05 Right\nkey 0.1 Shift+Right\nend 0.2\n", [&](Session &s) {
            QString error;
            QVERIFY2(s.openSong(withLeadSettings(doc, bad), &error), qPrintable(error));
            const QJsonObject got = leadSettings(s.saveSong());
            QCOMPARE(got[QStringLiteral("sound")].toString(), QStringLiteral("Anthem"));
            QCOMPARE(got[QStringLiteral("octave")].toInt(), 1);
            QCOMPARE(got[QStringLiteral("glide")].toInt(), 0);
            QCOMPARE(got[QStringLiteral("gate")].toString(), QStringLiteral("off"));
            QCOMPARE(got[QStringLiteral("tone")].toInt(), -4);
            QCOMPARE(got[QStringLiteral("follow")].toBool(), false);
            QCOMPARE(got[QStringLiteral("hold")].toBool(), false);
            QVERIFY(!got.contains(QStringLiteral("wobble")));
        });
    }

    void clipEventsFromAFileStayInBounds() {
        // A song's events come from disk: any int and any finite float in
        // a note or a param plays without reaching outside the engine's
        // tables, and the sound stays finite.
        auto spec = makeLead();
        Fake ctx;
        spec.engine->init(48000);
        const int ints[] = {INT_MIN, -1, INT_MAX};
        const float floats[] = {-3.4e38f, 3.4e38f};
        int id = 1;
        for (int a : ints)
            for (int b : ints) {
                Command on = note(Command::On, id, a, b, a, b);
                for (float f : floats) {
                    for (float &x : on.f)
                        x = f;
                    spec.engine->command(on, ctx);
                    Command mv = on;
                    mv.type = Command::Move;
                    spec.engine->command(mv, ctx);
                    for (int which = 0; which < 8; ++which) {
                        Command p;
                        p.type = Command::Param;
                        p.i[0] = which;
                        p.i[1] = a;
                        p.f[0] = p.f[1] = f;
                        spec.engine->command(p, ctx);
                    }
                    std::vector<float> out;
                    float data[kPartData] = {};
                    run(*spec.engine, ctx, 0.05, data, &out);
                    for (float v : out)
                        QVERIFY(std::isfinite(v));
                }
                spec.engine->command(note(Command::Off, id++, 0, 0), ctx);
            }
    }
};

TESTKIT_REGISTER(LeadTests)
#include "lead_tests.moc"
