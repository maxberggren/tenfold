// The Lead page: a neck that sings in the key, legato that glides without a
// new attack, harmony that moves with the melody, and clips that replay it.

#include <climits>
#include <cstdio>
#include <tuple>

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
        s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
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

// The level of one frequency in a stretch of the mono mix, in dB of full
// scale (a full-scale sine reads 0), by Goertzel over a Hann window.
double levelAt(const testkit::Take &take, double from, double to, double hz) {
    const size_t a = size_t(from * 48000), b = std::min(take.left.size(), size_t(to * 48000));
    const double w = 2 * 3.14159265358979 * hz / 48000.0, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2 * 3.14159265358979 * double(i - a) / double(b - a));
        const double s0 = 0.5 * (take.left[i] + take.right[i]) * hann + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 20 * std::log10(std::sqrt(std::max(s1 * s1 + s2 * s2 - c * s1 * s2, 1e-30)) * 4.0 / double(b - a) + 1e-12);
}

// How a note's level swings: in steps of `step` seconds, the mean of its
// peaks less the mean of its troughs (dB), and how many peaks a second.
struct Swing {
    double depth = 0, rate = 0;
};
Swing swingOf(const testkit::Take &take, double from, double to, double step) {
    std::vector<double> e;
    for (double x = from; x < to; x += step)
        e.push_back(20 * std::log10(take.rms(x, x + step) + 1e-12));
    double peaks = 0, troughs = 0;
    int np = 0, nt = 0;
    for (size_t i = 1; i + 1 < e.size(); ++i) {
        if (e[i] >= e[i - 1] && e[i] > e[i + 1]) {
            peaks += e[i];
            ++np;
        }
        if (e[i] <= e[i - 1] && e[i] < e[i + 1]) {
            troughs += e[i];
            ++nt;
        }
    }
    Swing sw;
    if (np && nt) {
        sw.depth = peaks / np - troughs / nt;
        sw.rate = np / (to - from);
    }
    return sw;
}

// The level of a stretch of the mono mix in dB of full scale, as levelAt
// reads a sine: a full-scale sine reads 0.
double levelOf(const testkit::Take &take, double from, double to) {
    const size_t a = size_t(from * 48000), b = std::min(take.left.size(), size_t(to * 48000));
    double e = 1e-30;
    for (size_t i = a; i < b; ++i) {
        const double x = 0.5 * (take.left[i] + take.right[i]);
        e += x * x;
    }
    return 10 * std::log10(2.0 * e / double(std::max<size_t>(b - a, 1)));
}

// The energy of a band of the mono mix (dB, against the same measure of
// any other band): the power at every 10 Hz from lo to hi, by Goertzel
// over a Hann window, summed.
double bandDb(const testkit::Take &take, double from, double to, double lo, double hi) {
    double e = 1e-30;
    for (double hz = lo; hz < hi; hz += 10.0)
        e += std::pow(10.0, levelAt(take, from, to, hz) / 10.0);
    return 10 * std::log10(e);
}

// A script that turns to a sound with → and then plays `rest`.
std::string onSound(int snd, const std::string &rest) {
    std::string s = "page 0 4\n";
    for (int k = 0; k < snd; ++k)
        s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
    return s + rest;
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
        // beyond) moves along, and enough of them reach Tam-tam, the last;
        // row 1 starts with the octave stepper (↑ at its right end) and has
        // FOLLOW about 0.64 across, HOLD after it.
        testkit::play("page 0 4\nbutton 0.2 0.98 -0.048\nend 0.5\n", [](Session &s) {
            QVERIFY2(!s.pageStatus().contains(QStringLiteral("Anthem")), qPrintable(s.pageStatus()));
        });
        {
            std::string walk = "page 0 4\n";
            for (int k = 0; k < 16; ++k)
                walk += fmt("button %.2f 0.98 -0.048\n", 0.2 + 0.1 * k);
            testkit::play(walk + "end 2.0\n", [](Session &s) {
                QVERIFY2(s.pageStatus().contains(QStringLiteral("Tam-tam")), qPrintable(s.pageStatus()));
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
                s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
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
        // (The gongs' tuning is checked in gong_tests.cpp, by their spectra:
        // an autocorrelation hears a gong's inharmonic partials as periods
        // of their own.)
        for (int snd = 0; snd < lead::kSounds; ++snd)
            for (const auto &n : notes) {
                if (lead::sound(snd).model == lead::Sound::Gong)
                    continue;
                const auto take = testkit::play(hold(snd, n.cell));
                const double midi = midiOf(pitchHz(take, 0.62, 0.8));
                QVERIFY2(std::fabs(midi - n.midi) < 0.1,
                         qPrintable(QStringLiteral("%1 %2: %3").arg(soundName(snd)).arg(n.midi).arg(midi)));
                QVERIFY2(take.finite(), soundName(snd));
                QVERIFY2(take.rms(0.32, 0.6) > 0.02, soundName(snd));
            }
    }

    void theNewSoundsAreTheirOwn() {
        // Mini, Flute and the five after them, held, each unlike every other:
        // their spectra in the first sixty milliseconds and once the note is
        // held (at the half harmonics, up to the sixteenth harmonic while
        // held, so a sub, breath, an attack's flash and the brightness all
        // count) stand apart from every other sound.
        std::vector<std::vector<double>> shape;
        for (int snd = 0; snd < lead::kSounds; ++snd) {
            const auto take = testkit::play(holdA4(snd));
            auto s = profile(take, 0.3, 0.36);
            const auto held = profile(take, 0.65, 0.9, 32);
            s.insert(s.end(), held.begin(), held.end());
            shape.push_back(s);
        }
        // The five after them too: each at least 6 dB from every other
        // sound, and the Sitar, a plucked string like the Pluck, a full 8 dB
        // from it: its buzz and the formant sliding through it are not a
        // filter closing on a saw. The Sync, an analog lead among analog
        // leads, 8 dB from every one: its sung formant is its own.
        for (int a : {8, 9, 10, 11, 12, 13, 14})
            for (int b = 0; b < lead::kSounds; ++b) {
                if (a == b)
                    continue;
                double d = 0;
                for (size_t k = 0; k < shape[size_t(a)].size(); ++k) {
                    const double e = shape[size_t(a)][k] - shape[size_t(b)][k];
                    d += e * e;
                }
                d = std::sqrt(d / double(shape[size_t(a)].size()));
                // (A bossed gong and a steel pan are both struck metal with a
                // strong octave: on these profiles they stand a little under
                // 6 dB apart. What tells them apart, the gong's octave pair
                // beating and its inharmonic partials, gong_tests.cpp checks.)
                const bool gongAndPan = a == 11 && lead::sound(b).model == lead::Sound::Gong;
                const double least = a <= 9 || a == 12 || (a == 10 && b == 1) ? 8.0 : gongAndPan ? 5.5 : 6.0;
                QVERIFY2(d > least, qPrintable(QStringLiteral("%1 vs %2: %3 dB").arg(soundName(a), soundName(b)).arg(d)));
            }
    }

    void theMiniAndTheFluteAnswerTheTouch() {
        // The Mini glides even between detached notes, as an old mono
        // synth's portamento does: just after the second note lands, the
        // pitch is still on its way up from the first; it settles in tune.
        std::string mini = "page 0 4\n";
        for (int k = 0; k < 8; ++k)
            mini += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
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
        // fresh tenfold, it comes back by its name, and the page says so
        // with the music it is for ("Steel pan · calypso").
        // (The gongs, after them, have tests of their own: gong_tests.cpp.)
        for (int snd = 10; snd < 15; ++snd) {
            const auto take = testkit::play(holdA4(snd));
            const double midi = midiOf(pitchHz(take, 0.62, 0.8));
            QVERIFY2(std::fabs(midi - 69.0) < 0.1, qPrintable(QStringLiteral("%1: %2").arg(soundName(snd)).arg(midi)));
            QVERIFY2(take.finite(), soundName(snd));
            QVERIFY2(take.peak() < 1.0, soundName(snd));
            QVERIFY2(take.rms(0.32, 1.4) > 0.02, soundName(snd));
            std::string s = "page 0 4\n";
            for (int k = 0; k < snd; ++k)
                s += fmt("key %.3f Right\n", 0.01 + 0.015 * k);
            const QString name = QString::fromUtf8(soundName(snd));
            const QString styled = name + QStringLiteral(" · ") + QString::fromUtf8(lead::sound(snd).style);
            testkit::reopen(s + "end 0.5\n", 0.3, [&](Session &, Session &opened, const QJsonObject &doc) {
                QCOMPARE(leadSettings(doc)[QStringLiteral("sound")].toString(), name);
                QCOMPARE(leadSettings(opened.saveSong())[QStringLiteral("sound")].toString(), name);
                QVERIFY2(opened.pageStatus().contains(styled), qPrintable(opened.pageStatus()));
            });
        }
        testkit::play(onSound(11, "end 0.5\n"), [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("Steel pan · calypso")), qPrintable(s.pageStatus()));
        });
    }

    void theSitarsJawariSlidesDownThroughTheHarmonics() {
        // The buzz's formant: the loudest harmonic from the third up stands
        // high on the stroke and slides down through the others as the
        // string rings, never back up, over a second.
        const auto a = testkit::play(holdA4(10));
        auto loudest = [&](double from) {
            const auto p = profile(a, from, from + 0.05, 32); // harmonic h at index 2h − 1
            int best = 3;
            for (int h = 3; h <= 16; ++h)
                if (p[size_t(2 * h - 1)] > p[size_t(2 * best - 1)])
                    best = h;
            return best;
        };
        const double at[] = {0.32, 0.5, 0.7, 1.0, 1.3};
        int was = 99;
        QString path;
        for (double t : at) {
            const int h = loudest(t);
            path += QStringLiteral("%1 ").arg(h);
            QVERIFY2(h <= was, qPrintable(path));
            was = h;
        }
        QVERIFY2(loudest(0.32) >= 10 && loudest(1.3) <= 4, qPrintable(path));
        // And it slides as a stripe through the harmonics over the first
        // half second or so, the "ji" of the stroke, not at once: a fifth of
        // a second in, it is still well up and on its way down, lower
        // again two fifths in.
        QVERIFY2(loudest(0.5) >= 5 && loudest(0.5) < loudest(0.32) && loudest(0.7) < loudest(0.5), qPrintable(path));
        // A buzz that keeps its partials: well into the note, the ninth to
        // the sixteenth stand far closer to the fundamental than a Pluck's.
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
        const auto m = testkit::play(onSound(10, fmt("down 0.3 1 %.4f 0.6\ndown 0.8 2 %.4f 0.6\nup 2.0 2\nup 2.0 1\nend 2.2\n",
                                                     cellX(0), cellX(4))));
        const double early = midiOf(pitchHz(m, 0.83, 0.88)), late = midiOf(pitchHz(m, 1.4, 1.6));
        QVERIFY2(std::fabs(late - 64.0) < 0.15, qPrintable(QString::number(late)));
        QVERIFY2(early < late - 1.0, qPrintable(QStringLiteral("%1 %2").arg(early).arg(late)));
    }

    void theSitarsTarafShimmersInTheScaleAndTheChikariRings() {
        // The taraf, tuned up the scale: once the played string is let go,
        // a note of the scale leaves it ringing on, wide across the field;
        // the same string bent half a step off the scale, whose harmonics
        // miss its strings, leaves far less.
        auto tail = [](float off, double *side) {
            auto spec = makeLead();
            Fake ctx;
            spec.engine->init(48000);
            Command on = note(Command::On, 1, 0, 1, 0, 10);
            on.f[2] = off;
            on.f[3] = off != 0 ? 1.0f : 0.0f;
            spec.engine->command(on, ctx);
            run(*spec.engine, ctx, 1.0);
            spec.engine->command(note(Command::Off, 1, 0, 1), ctx);
            run(*spec.engine, ctx, 1.2);
            float l[kBlock], r[kBlock];
            double mid = 0, sd = 0;
            for (int i = 0; i < int(0.3 * 48000 / kBlock); ++i) {
                std::fill(l, l + kBlock, 0.0f);
                std::fill(r, r + kBlock, 0.0f);
                spec.engine->render(l, r, kBlock, ctx);
                for (int k = 0; k < kBlock; ++k) {
                    mid += 0.25 * (l[k] + r[k]) * (l[k] + r[k]);
                    sd += 0.25 * (l[k] - r[k]) * (l[k] - r[k]);
                }
            }
            if (side)
                *side = std::sqrt(sd / std::max(mid, 1e-20));
            return std::sqrt(mid);
        };
        double side = 0;
        const double in = tail(0, &side), off = tail(0.5f, nullptr);
        QVERIFY2(in > 3.0 * off, qPrintable(QStringLiteral("%1 %2").arg(in).arg(off)));
        QVERIFY2(side > 0.15, qPrintable(QString::number(side)));
        // And it blooms after the note: the played string, let go, hands
        // what it still swings to the taraf, which rings on, a third of a
        // second after the release within 13 dB of the note just before it.
        const auto rung = testkit::play(onSound(10, fmt("down 0.3 1 %.4f 0.7 p=0.7\nup 1.3 1\nend 2.0\n", cellX(7))));
        const double before = levelOf(rung, 1.2, 1.3), after = levelOf(rung, 1.6, 1.8);
        QVERIFY2(after > before - 13.0, qPrintable(QStringLiteral("%1 %2").arg(before).arg(after)));
        // The chikari: a second finger tapped high on the pad, while A4
        // rings, is no hammer-on (the line stays on A4) but strikes the high
        // drone strings, the E a fifth over it among them.
        const std::string held = fmt("down 0.3 1 %.4f 0.6 p=0.6\n", cellX(7));
        const auto tapped = testkit::play(onSound(10, held + fmt("down 0.8 2 %.4f 0.92 p=0.7\nup 0.88 2\nup 1.5 1\nend 1.8\n", cellX(9))));
        const auto plain = testkit::play(onSound(10, held + "up 1.5 1\nend 1.8\n"));
        const double a4 = levelAt(tapped, 0.9, 1.0, 440.0), c5 = levelAt(tapped, 0.9, 1.0, 523.25);
        QVERIFY2(c5 < a4 - 30.0, qPrintable(QStringLiteral("%1 %2").arg(a4).arg(c5)));
        const double e5 = levelAt(tapped, 0.82, 0.95, 659.26), without = levelAt(plain, 0.82, 0.95, 659.26);
        QVERIFY2(e5 > without + 15.0, qPrintable(QStringLiteral("%1 %2").arg(e5).arg(without)));
    }

    void theSitarsAlapSustainsThroughAMeend() {
        // An alap's Sa, plucked and held, slid up a step from a second to
        // two seconds after the stroke: the meend is heard on a living
        // string, a second and a half after the pluck within 15 dB of the
        // stroke and bent on its way to B4, two seconds after it within 20,
        // and four seconds after it still well heard, within 30.
        const std::string alap = fmt("down 0.3 1 %.4f 0.7 p=0.7\nglide 1.3 2.3 1 %.4f 0.7 p=0.7\n", cellX(7), cellX(8));
        const auto plain = testkit::play(onSound(10, alap + "up 4.6 1\nend 4.8\n"));
        const double stroke = levelOf(plain, 0.3, 0.4);
        const double meend = levelOf(plain, 1.75, 1.85), two = levelOf(plain, 2.25, 2.35), four = levelOf(plain, 4.25, 4.35);
        const QString got = QStringLiteral("stroke %1, meend %2, at 2 s %3, at 4 s %4").arg(stroke).arg(meend).arg(two).arg(four);
        QVERIFY2(meend > stroke - 15.0, qPrintable(got));
        QVERIFY2(two > stroke - 20.0, qPrintable(got));
        QVERIFY2(four > stroke - 30.0, qPrintable(got));
        const double bent = midiOf(pitchHz(plain, 1.75, 1.85));
        QVERIFY2(bent > 69.1 && bent < 71.0, qPrintable(QString::number(bent)));
        // The chikari struck late in it sits under the fading line, as
        // hard as the string still swings: its fifth is heard, but the
        // line with it is no more than 3 dB up on the line alone.
        const auto tapped = testkit::play(
            onSound(10, alap + fmt("down 3.8 2 %.4f 0.92 p=0.7\nup 3.88 2\nup 4.6 1\nend 4.8\n", cellX(10))));
        const double e5 = levelAt(tapped, 3.85, 4.05, 659.26), without = levelAt(plain, 3.85, 4.05, 659.26);
        QVERIFY2(e5 > without + 10.0, qPrintable(QStringLiteral("%1 %2").arg(e5).arg(without)));
        const double with = levelOf(tapped, 3.82, 4.1), alone = levelOf(plain, 3.82, 4.1);
        QVERIFY2(with < alone + 3.0, qPrintable(QStringLiteral("%1 %2").arg(with).arg(alone)));
    }

    void theSteelPanRollsAHeldNote() {
        // A held note is rolled: every stroke strikes it again, so its
        // level pulses six decibels and more, ten to fourteen times a second.
        const auto held = testkit::play(hold(11, 7, 0.6, 0.7));
        const Swing sw = swingOf(held, 0.8, 1.4, 0.01);
        QVERIFY2(sw.depth >= 6.0, qPrintable(QStringLiteral("%1 dB at %2 Hz").arg(sw.depth).arg(sw.rate)));
        QVERIFY2(sw.rate >= 10.0 && sw.rate <= 14.5, qPrintable(QStringLiteral("%1 dB at %2 Hz").arg(sw.depth).arg(sw.rate)));
        QVERIFY2(held.rms(1.0, 1.4) > 0.3 * held.rms(0.3, 0.4), "the roll sustains it");
        // Each stroke is the stick's rounded contact, not a step: nothing
        // above the modes, from 6 to 10 kHz, comes up to −90 dBFS while it
        // rolls.
        for (double hz = 6200; hz < 10000; hz += 700)
            for (double t = 0.8; t < 1.4; t += 0.1)
                QVERIFY2(levelAt(held, t, t + 0.1, hz) < -90.0,
                         qPrintable(QStringLiteral("%1 Hz at %2 s: %3").arg(hz).arg(t).arg(levelAt(held, t, t + 0.1, hz))));
        // A short note is one stroke: at 90 BPM a sixteenth, and an eighth
        // (a third of a second), both let go before the roll begins.
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
        for (double len : {0.12, 0.33}) {
            const auto once =
                testkit::play(onSound(11, fmt("tempo 0.01 90\ndown 0.3 1 %.4f 0.6 p=0.7\nup %.3f 1\nend 1.6\n", cellX(7), 0.3 + len)));
            QVERIFY2(strokes(once, 0.4, 1.4) <= 1, qPrintable(QStringLiteral("%1: %2").arg(len).arg(strokes(once, 0.4, 1.4))));
        }
        // The roll keeps the studio's time. It begins once the note has
        // been held an eighth: at 150 BPM its strokes have begun a fifth of
        // a second in, at 75 BPM not before two fifths. Its strokes run on
        // the beat's subdivision nearest thirteen a second: 16th triplets
        // at 128 BPM (12.8 a second), 32nd triplets at 75 (15).
        auto rolled = [&](double bpm) {
            return testkit::play(onSound(11, fmt("tempo 0.01 %.0f\ndown 0.3 1 %.4f 0.6 p=0.7\nup 2.4 1\nend 2.6\n", bpm, cellX(7))));
        };
        const auto fast = rolled(150), slow = rolled(75);
        QVERIFY2(strokes(fast, 0.45, 0.68) >= 2, qPrintable(QString::number(strokes(fast, 0.45, 0.68))));
        QVERIFY2(strokes(slow, 0.45, 0.68) == 0, qPrintable(QString::number(strokes(slow, 0.45, 0.68))));
        for (const auto &[bpm, want] : {std::pair{128.0, 12.8}, std::pair{75.0, 15.0}}) {
            const Swing r = swingOf(rolled(bpm), 1.0, 2.3, 0.01);
            QVERIFY2(std::fabs(r.rate - want) < 1.0, qPrintable(QStringLiteral("%1 BPM: %2 a second").arg(bpm).arg(r.rate)));
        }
        // A player swells into a roll and leans on the beat. At 120 BPM,
        // a note struck on the beat: the roll's first stroke lands well
        // under the strokes a second later, and the strokes on the studio's
        // beats stand over the ones between them (16th triplets, twelve a
        // second; the middle three of each beat's six, away from the
        // accent's own ring).
        const auto swung = testkit::play(onSound(11, fmt("tempo 0.01 120\ndown 0.5 1 %.4f 0.6 p=0.7\nup 3.2 1\nend 3.4\n", cellX(7))));
        auto strokeAt = [&](double at) {
            double top = 0;
            for (double x = at - 0.03; x < at + 0.03; x += 0.0025)
                top = std::max(top, swung.rms(x, x + 0.005));
            return 20 * std::log10(top);
        };
        double onBeat = 0, between = 0;
        for (double beat : {1.5, 2.0, 2.5, 3.0}) {
            onBeat += strokeAt(beat) / 4.0;
            for (int j = 2; j <= 4; ++j)
                between += strokeAt(beat - 0.5 + j / 12.0) / 12.0;
        }
        QVERIFY2(onBeat > between + 1.0, qPrintable(QStringLiteral("%1 %2").arg(onBeat).arg(between)));
        QVERIFY2(strokeAt(0.84) < between - 2.0, qPrintable(QStringLiteral("%1 %2").arg(strokeAt(0.84)).arg(between)));
        // And its neighbours ring with it: a single stroke on A4 sets the
        // pan's E5 ringing, the fifth over it a few cents flat, between 6
        // and 24 dB under the note.
        const auto single = testkit::play(onSound(11, fmt("down 0.3 1 %.4f 0.6 p=0.3\nup 0.42 1\nend 1.2\n", cellX(7))));
        const double a4 = levelAt(single, 0.4, 0.9, 440.0), e5 = levelAt(single, 0.4, 0.9, 440.0 * 1.4958);
        QVERIFY2(e5 > a4 - 24.0 && e5 < a4 - 6.0, qPrintable(QStringLiteral("%1 %2").arg(a4).arg(e5)));
        // A firm stroke high on the note pings: the stick's first few
        // milliseconds stand 10 dB brighter from 3 to 6 kHz, against the
        // fundamental, than a soft low one's, and splash nothing above
        // 12 kHz within 40 dB of it.
        auto ping = [&](double y, double p, double *over) {
            const auto t = testkit::play(onSound(11, fmt("down 0.3 1 %.4f %.2f p=%.2f\nup 0.6 1\nend 0.8\n", cellX(7), y, p)));
            const double fund = bandDb(t, 0.3, 0.315, 400, 480);
            if (over)
                *over = bandDb(t, 0.3, 0.315, 12000, 20000) - fund;
            return bandDb(t, 0.3, 0.315, 3000, 6000) - fund;
        };
        double over = 0;
        const double firm = ping(0.95, 0.95, &over), soft = ping(0.3, 0.3, nullptr);
        QVERIFY2(firm > soft + 10.0, qPrintable(QStringLiteral("%1 %2").arg(firm).arg(soft)));
        QVERIFY2(over < -40.0, qPrintable(QString::number(over)));
        // Its octave blooms after the strike: the second harmonic stands
        // higher against the fundamental a little in than at once.
        auto octave = [&](double from, double to) {
            const auto p = profile(held, from, to);
            return p[3] - p[1];
        };
        QVERIFY2(octave(0.33, 0.37) > octave(0.3, 0.32) + 2.0,
                 qPrintable(QStringLiteral("%1 %2").arg(octave(0.3, 0.32)).arg(octave(0.33, 0.37))));
        // Low down it is a cello pan: A2's octave rings as strong as the
        // note itself, and the struck note settles down onto its pitch.
        const auto low = testkit::play(onSound(11, fmt("key 0.25 Down\ndown 0.3 1 %.4f 0.5 p=0.6\nup 1.5 1\nend 1.8\n", cellX(0))));
        const double fund = levelAt(low, 0.5, 0.7, 110.0), oct = levelAt(low, 0.5, 0.7, 220.0);
        QVERIFY2(oct > fund - 3.0, qPrintable(QStringLiteral("%1 %2").arg(fund).arg(oct)));
        const double struck = midiOf(pitchHz(low, 0.3, 0.34)), settled = midiOf(pitchHz(low, 0.6, 0.8));
        QVERIFY2(struck > settled + 0.1 && std::fabs(settled - 45.0) < 0.1, qPrintable(QStringLiteral("%1 %2").arg(struck).arg(settled)));
    }

    void theSyncLeadTearsAndKeepsTearing() {
        // Every attack opens with the slaves swept high: far brighter in its
        // first thirty milliseconds than held, and the finger's height
        // holds the sweep up.
        const auto mid = testkit::play(hold(12, 7, 0.5));
        QVERIFY2(brightness(mid, 0.3, 0.33) > 1.5 * brightness(mid, 0.8, 1.2),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(mid, 0.3, 0.33)).arg(brightness(mid, 0.8, 1.2))));
        const auto low = testkit::play(hold(12, 7, 0.1)), high = testkit::play(hold(12, 7, 0.95));
        QVERIFY2(brightness(high, 0.8, 1.2) > 1.5 * brightness(low, 0.8, 1.2),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(high, 0.8, 1.2)).arg(brightness(low, 0.8, 1.2))));
        // Held low on the pad it never settles into a plain saw: the
        // harmonics the slave's formant lights keep changing, each of the
        // second to the sixth moving by decibels from one tenth of a second
        // to the next.
        double moved = 0;
        int steps = 0;
        std::vector<double> was;
        for (double t = 0.8; t < 1.45; t += 0.1) {
            const auto p = profile(low, t, t + 0.05);
            if (!was.empty())
                for (int h = 2; h <= 6; ++h, ++steps)
                    moved += std::fabs(p[size_t(2 * h - 1)] - was[size_t(2 * h - 1)]);
            was = p;
        }
        QVERIFY2(moved / steps > 2.0, qPrintable(QString::number(moved / steps)));
        // A hard press snarls: FM into the sync moves the held spectrum.
        const auto pressed = testkit::play(hold(12, 7, 0.5, 0.95));
        const auto a = profile(mid, 0.8, 1.2, 32), b = profile(pressed, 0.8, 1.2, 32);
        double d = 0;
        for (size_t k = 1; k < a.size(); k += 2)
            d += (a[k] - b[k]) * (a[k] - b[k]);
        d = std::sqrt(d / double(a.size() / 2));
        QVERIFY2(d > 4.0, qPrintable(QString::number(d)));
        // At the top of the neck, swept hard and pressed, run at twice the
        // rate it stays clean: between the harmonics of A5, nothing within
        // 60 dB of the note.
        const auto top = testkit::play(onSound(12, fmt("key 0.25 Up\ndown 0.3 1 %.4f 0.95 p=1\nup 1.5 1\nend 1.8\n", cellX(14))));
        double peak = -200, between = -200;
        for (int h = 1; h <= 10; ++h) {
            peak = std::max(peak, levelAt(top, 0.6, 1.2, 1760.0 * h));
            between = std::max(between, levelAt(top, 0.6, 1.2, 1760.0 * (h + 0.5)));
        }
        QVERIFY2(between < peak - 60.0, qPrintable(QStringLiteral("%1 %2").arg(peak).arg(between)));
        // With the FM snarl all the way in and the vibrato running, at A4
        // and at A5, nothing folds back between the harmonics: from 4 to
        // 8 kHz, midway between them, every tenth of a second, at least
        // 75 dB under the note.
        for (const auto &[up, cell, hz] : {std::tuple{false, 7, 440.0}, std::tuple{true, 14, 1760.0}}) {
            const auto t = testkit::play(onSound(12, std::string(up ? "key 0.25 Up\n" : "") +
                                                         fmt("down 0.3 1 %.4f 0.5 p=0.95\nup 2.5 1\nend 2.7\n", cellX(cell))));
            for (double from = 0.9; from < 2.4; from += 0.1) {
                const double note = levelOf(t, from, from + 0.1);
                for (double f = 1.5 * hz; f < 8000.0; f += hz)
                    if (f > 4000.0)
                        QVERIFY2(levelAt(t, from, from + 0.1, f) < note - 75.0,
                                 qPrintable(QStringLiteral("%1 Hz at %2 s: %3 under the note")
                                                .arg(f)
                                                .arg(from)
                                                .arg(note - levelAt(t, from, from + 0.1, f))));
            }
        }
        // A hard-pressed glide up to the top of the neck: the ladder's soft
        // clip runs at twice the rate and what the return to the page's
        // rate would mirror is rolled off, so from 21.5 to 23.5 kHz there
        // is nothing within 80 dB of the note (at the page's rate the clip
        // left mirrored lines there some 65 dB under it).
        const auto glide = testkit::play(onSound(
            12, fmt("down 0.3 1 %.4f 0.8 p=0.95\nglide 0.8 1.6 1 %.4f 0.8 p=0.95\nup 2.0 1\nend 2.2\n", cellX(7), cellX(14))));
        const double whole = bandDb(glide, 0.8, 1.9, 100, 19500), mirrored = bandDb(glide, 0.8, 1.9, 21500, 23500);
        QVERIFY2(mirrored < whole - 80.0, qPrintable(QStringLiteral("%1 under").arg(whole - mirrored)));
    }

    void theSyncLeadSingsAVowel() {
        // A second formant over the tear: low on the pad the vowel is "ow",
        // its second harmonic far over its sixth; high on the pad it is
        // "ee", the sixth (2.6 kHz) lifted over the second.
        const auto ow = testkit::play(hold(12, 7, 0.15)), ee = testkit::play(hold(12, 7, 0.85));
        auto vowel = [](const testkit::Take &t) { return levelAt(t, 0.7, 1.4, 2640.0) - levelAt(t, 0.7, 1.4, 880.0); };
        QVERIFY2(vowel(ow) < -10.0, qPrintable(QString::number(vowel(ow))));
        QVERIFY2(vowel(ee) > 2.0, qPrintable(QString::number(vowel(ee))));
    }

    void theErhuIsABowedString() {
        const auto e = testkit::play(holdA4(13));
        // It slides up into the note.
        const double start = midiOf(pitchHz(e, 0.31, 0.35));
        QVERIFY2(start < 68.8, qPrintable(QString::number(start)));
        // The bow decides the tone. At a normal press the string swings in
        // its Helmholtz motion, the fundamental on top; a light one can't
        // hold it, and it whistles on its second partial, the fundamental
        // well under it; a hard one grinds, brighter than a normal one.
        auto partials = [](const testkit::Take &t) {
            return std::make_pair(levelAt(t, 0.9, 0.95, 440.0), levelAt(t, 0.9, 0.95, 880.0));
        };
        const auto normal = partials(e);
        QVERIFY2(normal.first > normal.second, qPrintable(QStringLiteral("%1 %2").arg(normal.first).arg(normal.second)));
        const auto light = testkit::play(hold(13, 7, 0.6, 0.15));
        QVERIFY2(partials(light).second > partials(light).first + 10.0,
                 qPrintable(QStringLiteral("%1 %2").arg(partials(light).first).arg(partials(light).second)));
        // A hard one grinds: from 2 to 5 kHz, against its fundamental, it
        // stands 3 dB over a normal one.
        const auto hard = testkit::play(hold(13, 7, 0.6, 0.95));
        auto grit = [](const testkit::Take &t) { return bandDb(t, 0.8, 1.2, 2000, 5000) - bandDb(t, 0.8, 1.2, 420, 460); };
        QVERIFY2(grit(hard) > grit(e) + 3.0, qPrintable(QStringLiteral("%1 %2").arg(grit(hard)).arg(grit(e))));
        // And it grinds in the small body's middle, not up in the air:
        // pressed hard right by the bridge, on A4 and on E5, everything
        // above 10 kHz stays 40 dB under the fundamental.
        for (const auto &[cell, hz] : {std::pair{7, 440.0}, std::pair{11, 659.26}}) {
            const auto grind = testkit::play(hold(13, cell, 0.9, 0.95));
            const double fund = bandDb(grind, 0.9, 1.3, hz * 0.95, hz * 1.05), air = bandDb(grind, 0.9, 1.3, 10000, 23000);
            QVERIFY2(air < fund - 40.0, qPrintable(QStringLiteral("%1 Hz: %2 under").arg(hz).arg(fund - air)));
        }
        // The rosin rasps, but in a narrow band and well under the note:
        // between the harmonics around it, at least 35 dB down.
        const double fund = levelAt(e, 0.62, 0.67, 440.0);
        for (double h : {4.5, 5.5, 6.5, 7.5})
            QVERIFY2(levelAt(e, 0.62, 0.67, 440.0 * h) < fund - 35.0,
                     qPrintable(QStringLiteral("%1: %2 under %3").arg(h).arg(levelAt(e, 0.62, 0.67, 440.0 * h)).arg(fund)));
        // The skin's nasal formant: around a kilohertz the body lifts A3's
        // fifth harmonic (1100 Hz) over its third, which a bowed string's
        // own falling spectrum would leave the louder.
        const auto a3 = testkit::play(hold(13, 0));
        QVERIFY2(levelAt(a3, 0.62, 0.7, 1100.0) > levelAt(a3, 0.62, 0.7, 660.0),
                 qPrintable(QStringLiteral("%1 %2").arg(levelAt(a3, 0.62, 0.7, 1100.0)).arg(levelAt(a3, 0.62, 0.7, 660.0))));
        // A note played again just after is a change of bow: the same
        // string turns round (no second voice), and the hair catching it
        // again rasps, the noise between its harmonics up on the steady note.
        const std::string twice =
            fmt("down 0.3 1 %.4f 0.6 p=0.55\nup 0.9 1\ndown 0.98 2 %.4f 0.6 p=0.55\nup 1.6 2\nend 1.8\n", cellX(7), cellX(7));
        const Trace tr = trace(onSound(13, twice));
        const testkit::Take &changed = tr.take;
        QVERIFY2(tr.at(1.0).voices == 1, qPrintable(QString::number(tr.at(1.0).voices)));
        auto rasp = [&](double from) {
            double n = 0;
            for (double h : {5.5, 6.5, 7.5})
                n += std::pow(10.0, levelAt(changed, from, from + 0.04, 440.0 * h) / 10.0);
            return 10 * std::log10(n) - levelAt(changed, 0.7, 0.8, 440.0);
        };
        QVERIFY2(rasp(0.98) > rasp(0.7) + 6.0, qPrintable(QStringLiteral("%1 %2").arg(rasp(0.98)).arg(rasp(0.7))));
        // A held bow sings in an arc: it swells into the middle of the
        // stroke and tapers toward its end, where the bow turns round by
        // itself. Never dead flat.
        const auto bow = testkit::play(onSound(13, fmt("down 0.3 1 %.4f 0.6 p=0.55\nup 4.3 1\nend 4.6\n", cellX(7))));
        const double early = levelOf(bow, 0.6, 0.8), middle = levelOf(bow, 1.5, 1.9), late = levelOf(bow, 3.1, 3.3);
        const QString arc = QStringLiteral("%1 %2 %3").arg(early).arg(middle).arg(late);
        QVERIFY2(middle > early + 1.5 && middle > late + 2.0, qPrintable(arc));
        // Past the bow's length, the next stroke sings on.
        QVERIFY2(levelOf(bow, 3.9, 4.1) > middle - 4.0, qPrintable(arc + QStringLiteral(" then %1").arg(levelOf(bow, 3.9, 4.1))));
    }

    void theErhusVibratoIsAPlayers() {
        // On a held note the vibrato comes late and narrow, then widens
        // and quickens into the bow's arc: deep once grown (near half a
        // semitone through a 45 ms window that rounds its peaks, under one
        // and a quarter), its cycles quicker in the middle of the stroke
        // than as it starts, and never one sine repeated (the cycles'
        // lengths and widths differ).
        const auto bow = testkit::play(onSound(13, fmt("down 0.3 1 %.4f 0.6 p=0.55\nup 3.3 1\nend 3.5\n", cellX(7))));
        std::vector<double> t, p;
        for (double x = 0.5; x < 3.2; x += 0.01) {
            t.push_back(x + 0.0225);
            p.push_back(midiOf(pitchHz(bow, x, x + 0.045)));
        }
        // Each cycle, from one upward crossing of the note to the next.
        struct Cycle {
            double at, len, width;
        };
        std::vector<Cycle> cycles;
        double last = -1;
        for (size_t i = 1; i < p.size(); ++i)
            if (p[i - 1] < 69.0 && p[i] >= 69.0) {
                if (last > 0) {
                    double lo = 1e9, hi = -1e9;
                    for (size_t j = 0; j < p.size(); ++j)
                        if (t[j] >= last && t[j] < t[i]) {
                            lo = std::min(lo, p[j]);
                            hi = std::max(hi, p[j]);
                        }
                    cycles.push_back({last, t[i] - last, hi - lo});
                }
                last = t[i];
            }
        QVERIFY2(cycles.size() > 8, qPrintable(QString::number(cycles.size())));
        auto over = [&](double from, double to, auto field) {
            double sum = 0;
            int count = 0;
            for (const auto &c : cycles)
                if (c.at >= from && c.at < to) {
                    sum += field(c);
                    ++count;
                }
            return count ? sum / count : 0.0;
        };
        auto width = [](const Cycle &c) { return c.width; };
        auto len = [](const Cycle &c) { return c.len; };
        const double early = over(0.6, 1.25, width), grown = over(1.4, 2.4, width);
        const QString w = QStringLiteral("%1 %2").arg(early).arg(grown);
        QVERIFY2(grown > 0.45 && grown < 1.25, qPrintable(w));
        QVERIFY2(early < 0.6 * grown, qPrintable(w));
        const double slow = over(0.6, 1.25, len), quick = over(1.4, 2.4, len);
        QVERIFY2(quick < 0.92 * slow, qPrintable(QStringLiteral("%1 %2").arg(slow).arg(quick)));
        QVERIFY2(1.0 / quick > 5.6 && 1.0 / quick < 7.2, qPrintable(QString::number(1.0 / quick)));
        double lmin = 1e9, lmax = 0, wmin = 1e9, wmax = 0;
        for (const auto &c : cycles)
            if (c.at >= 1.4 && c.at < 3.0) {
                lmin = std::min(lmin, c.len);
                lmax = std::max(lmax, c.len);
                wmin = std::min(wmin, c.width);
                wmax = std::max(wmax, c.width);
            }
        QVERIFY2(lmax > lmin + 0.008 && wmax > 1.1 * wmin,
                 qPrintable(QStringLiteral("%1-%2 s, %3-%4 st").arg(lmin).arg(lmax).arg(wmin).arg(wmax)));
    }

    void theErhuSlidesAcrossALeap() {
        // A legato leap of a fifth, D4 up to A4: the finger travels the
        // string, a huayin, landing a little past the A (more than a tenth
        // of a semitone) and settling back onto it, while the line
        // lightens as it slides. A step of a tone glides on without one.
        const auto leap = testkit::play(onSound(
            13, fmt("down 0.3 1 %.4f 0.6 p=0.55\ndown 0.9 2 %.4f 0.6 p=0.55\nup 0.9 1\nup 1.8 2\nend 2.0\n", cellX(3), cellX(7))));
        double top = 0;
        for (double x = 0.92; x < 1.2; x += 0.01)
            top = std::max(top, midiOf(pitchHz(leap, x, x + 0.03)));
        const double landed = midiOf(pitchHz(leap, 1.45, 1.5));
        QVERIFY2(top > 69.12 && top < 69.8, qPrintable(QStringLiteral("%1 then %2").arg(top).arg(landed)));
        QVERIFY2(std::fabs(landed - 69.0) < 0.35, qPrintable(QString::number(landed)));
        const double before = levelOf(leap, 0.7, 0.85), sliding = levelOf(leap, 0.93, 1.0);
        QVERIFY2(sliding < before - 0.8, qPrintable(QStringLiteral("%1 %2").arg(before).arg(sliding)));
        const auto step = testkit::play(onSound(
            13, fmt("down 0.3 1 %.4f 0.6 p=0.55\ndown 0.9 2 %.4f 0.6 p=0.55\nup 0.9 1\nup 1.8 2\nend 2.0\n", cellX(6), cellX(7))));
        double stepTop = 0;
        for (double x = 0.92; x < 1.2; x += 0.01)
            stepTop = std::max(stepTop, midiOf(pitchHz(step, x, x + 0.03)));
        QVERIFY2(stepTop < 69.1, qPrintable(QString::number(stepTop)));
    }

    void theErhusNotesStartWithoutATick() {
        // A legato line bowed note by note, each starting 20 ms after the
        // last let go (so the bow changes on a string still ringing), some
        // bowed nearer the bridge and some leaping into a slide: over
        // 12 kHz, where the small body leaves almost nothing, the first few
        // milliseconds of each note are no more than 6 dB over the local
        // level, the note's own steady state or the last one's just before
        // it let go, whichever is the louder. A bow change that stepped the
        // arc's weight, the vibrato's width, or the friction would tick
        // there by 20 to 50.
        auto line = [](const std::vector<std::tuple<int, double, double>> &notes) {
            std::string s;
            double at = 0.3;
            for (size_t k = 0; k < notes.size(); ++k) {
                const auto &[cell, y, press] = notes[k];
                s += fmt("down %.3f %.0f %.4f %.2f", at, double(k + 1), cellX(cell), y) + fmt(" p=%.2f\n", press);
                s += fmt("up %.3f %.0f\n", at + 0.28, double(k + 1));
                at += 0.3;
            }
            return testkit::play(onSound(13, s + fmt("end %.3f\n", at + 0.3)));
        };
        auto hf = [](const testkit::Take &t, double from) { return bandDb(t, from, from + 0.005, 12000, 23000); };
        auto tick = [&](const testkit::Take &t, double on) {
            double top = -1e9;
            for (double x = on - 0.005; x < on + 0.012; x += 0.0025)
                top = std::max(top, hf(t, x));
            return top;
        };
        const auto even = line({{11, 0.5, 0.6}, {10, 0.5, 0.6}, {9, 0.5, 0.6}, {8, 0.3, 0.6}, {7, 0.5, 0.65},
                                {9, 0.5, 0.6}, {11, 0.7, 0.6}, {7, 0.5, 0.6}});
        for (int k = 1; k < 8; ++k) {
            const double on = 0.3 + 0.3 * k;
            double steady = 0;
            for (double x = on + 0.1; x < on + 0.2; x += 0.005)
                steady += std::pow(10.0, hf(even, x) / 10.0);
            steady = std::max(10 * std::log10(steady / 20.0), hf(even, on - 0.03));
            const double over = tick(even, on) - steady;
            QVERIFY2(over < 6.0, qPrintable(QStringLiteral("note %1: %2 over").arg(k + 1).arg(over)));
        }
        // Into a light bow and out of it again the string itself breaks
        // from its Helmholtz motion into the whistle and catches again, a
        // real event of the string's own; over a steady state some 130 dB
        // under full scale it shows, but it stays 75 dB under the note.
        const auto light = line({{9, 0.5, 0.6}, {8, 0.5, 0.2}, {7, 0.4, 0.65}});
        for (int k = 1; k < 3; ++k) {
            const double on = 0.3 + 0.3 * k, note = bandDb(light, on + 0.1, on + 0.105, 100, 12000);
            QVERIFY2(tick(light, on) < note - 75.0,
                     qPrintable(QStringLiteral("note %1: %2 under").arg(k + 1).arg(note - tick(light, on))));
        }
    }

    void theAccordionsRegistersAndBellows() {
        // High on the pad, the musette: its reeds beat a fixed number of
        // hertz apart, so the tremolo of a low A and a high E has the same
        // speed (the level's own period, by autocorrelation of its
        // ten-millisecond steps, agrees within a quarter) and is heard.
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
        const auto low = testkit::play(hold(14, 0, 0.85)), high = testkit::play(hold(14, 11, 0.85));
        const double pl = period(low), ph = period(high);
        QVERIFY2(pl > 0.1 && ph > 0.1, qPrintable(QStringLiteral("%1 %2").arg(pl).arg(ph)));
        QVERIFY2(std::fabs(pl - ph) < 0.25 * pl, qPrintable(QStringLiteral("%1 %2").arg(pl).arg(ph)));
        auto tremolo = [](const testkit::Take &t) {
            double lo = 1e9, hi = 0;
            for (double x = 0.6; x < 1.4; x += 0.02) {
                lo = std::min(lo, t.rms(x, x + 0.02));
                hi = std::max(hi, t.rms(x, x + 0.02));
            }
            return 20 * std::log10(hi / lo);
        };
        QVERIFY2(tremolo(low) > 2.0, qPrintable(QString::number(tremolo(low))));
        // Low on the pad, the clarinet: one reed, dry, no tremolo. In the
        // middle, the bandoneon: its pair an octave apart lifts the octave
        // well over the clarinet's.
        const auto clarinet = testkit::play(hold(14, 0, 0.15)), bandoneon = testkit::play(hold(14, 0, 0.5));
        QVERIFY2(tremolo(clarinet) < 1.0, qPrintable(QString::number(tremolo(clarinet))));
        auto octave = [](const testkit::Take &t) { return levelAt(t, 0.8, 1.2, 440.0) - levelAt(t, 0.8, 1.2, 220.0); };
        QVERIFY2(octave(bandoneon) > octave(clarinet) + 4.0,
                 qPrintable(QStringLiteral("%1 %2").arg(octave(bandoneon)).arg(octave(clarinet))));
        // Every note speaks with its key's click, before the reed has come
        // up, and the reed sags onto its pitch from a little sharp.
        QVERIFY2(brightness(clarinet, 0.3, 0.315) > 3.0 * brightness(clarinet, 0.8, 1.0),
                 qPrintable(QStringLiteral("%1 %2").arg(brightness(clarinet, 0.3, 0.315)).arg(brightness(clarinet, 0.8, 1.0))));
        const double sharp = midiOf(pitchHz(clarinet, 0.305, 0.33)), onPitch = midiOf(pitchHz(clarinet, 0.6, 0.8));
        QVERIFY2(sharp > onPitch + 0.05 && std::fabs(onPitch - 57.0) < 0.1,
                 qPrintable(QStringLiteral("%1 %2").arg(sharp).arg(onPitch)));
        // The bellows turn by the phrase. Once they have drawn a bellows'
        // worth of air, the next note of the run turns them: it speaks with
        // a swell a note from rest doesn't have.
        const std::string second = fmt("down 3.3 2 %.4f 0.15 p=0.6\nup 3.9 2\nend 4.1\n", cellX(4));
        const auto turned = testkit::play(onSound(14, fmt("down 0.3 1 %.4f 0.15 p=0.6\nup 3.2 1\n", cellX(4)) + second));
        const auto fresh = testkit::play(onSound(14, second));
        const double swell = 20 * std::log10(turned.rms(3.33, 3.38) / fresh.rms(3.33, 3.38));
        QVERIFY2(swell > 1.5, qPrintable(QString::number(swell)));
        // And the pull is voiced apart from the push, not only tuned a
        // hair sharp: the same note in the same stop, drawn on the pull
        // just after the bellows turned, bites harder from 2 to 5 kHz
        // against its fundamental, and its chamber rings higher (2.2 to
        // 2.7 kHz against 1.3 to 1.7), than one drawn on the push just
        // after a shorter note that left them as they were. (Tuned apart
        // alone, the two differ by some 6 and 3 dB.)
        const auto pushed = testkit::play(onSound(
            14, fmt("down 0.3 1 %.4f 0.15 p=0.6\nup 1.5 1\ndown 1.6 2 %.4f 0.15 p=0.6\nup 2.2 2\nend 2.4\n", cellX(4), cellX(4))));
        const double root = pitchHz(fresh, 3.5, 3.8);
        auto bite = [&](const testkit::Take &t, double from) {
            return bandDb(t, from, from + 0.35, 2000, 5000) - levelAt(t, from, from + 0.35, root);
        };
        auto chamber = [&](const testkit::Take &t, double from) {
            return bandDb(t, from, from + 0.35, 2200, 2700) - bandDb(t, from, from + 0.35, 1300, 1700);
        };
        QVERIFY2(bite(turned, 3.5) > bite(pushed, 1.8) + 8.0,
                 qPrintable(QStringLiteral("%1 %2").arg(bite(turned, 3.5)).arg(bite(pushed, 1.8))));
        QVERIFY2(chamber(turned, 3.5) > chamber(pushed, 1.8) + 5.5,
                 qPrintable(QStringLiteral("%1 %2").arg(chamber(turned, 3.5)).arg(chamber(pushed, 1.8))));
        // A staccato run never turns them note by note: eight eighths at
        // 96 BPM in the musette, the same A, each speak as the first does,
        // with none of the low gust of the bellows turning (at 160 Hz,
        // under the note), so the run keeps one direction of the bellows,
        // its tremolo's speed and its tuning, to the end.
        std::string run = "tempo 0.01 96\n";
        for (int k = 0; k < 8; ++k)
            run += fmt("down %.4f %.0f %.4f 0.85 p=0.6\n", 0.3 + k * 0.3125, 10 + k, cellX(7)) +
                   fmt("up %.4f %.0f\n", 0.55 + k * 0.3125, 10 + k);
        const auto eighths = testkit::play(onSound(14, run + "end 3.0\n"));
        const double gust0 = levelAt(eighths, 0.3, 0.36, 160.0);
        for (int k = 1; k < 8; ++k) {
            const double at = 0.3 + k * 0.3125, gust = levelAt(eighths, at, at + 0.06, 160.0);
            QVERIFY2(gust < gust0 + 4.0, qPrintable(QStringLiteral("note %1: gust %2, the first's %3").arg(k + 1).arg(gust).arg(gust0)));
        }
        // But the turn the run's air comes to is heard, and a rest of a
        // breath turns them unheard: after the long note, a note at once
        // brings the gust; one after half a second's rest doesn't.
        auto after = [&](double gap) {
            const auto t = testkit::play(onSound(14, fmt("down 0.3 1 %.4f 0.85 p=0.6\nup 3.2 1\n", cellX(7)) +
                                                         fmt("down %.3f 2 %.4f 0.85 p=0.6\nup 4.2 2\nend 4.4\n", 3.2 + gap, cellX(7))));
            return levelAt(t, 3.2 + gap, 3.26 + gap, 160.0) - gust0;
        };
        QVERIFY2(after(0.1) > 8.0, qPrintable(QString::number(after(0.1))));
        QVERIFY2(after(0.5) < 4.0, qPrintable(QString::number(after(0.5))));
        // The page names the register the finger has drawn.
        testkit::play(onSound(14, fmt("down 0.3 1 %.4f 0.85 p=0.6\nend 0.6\n", cellX(4))), [](Session &s) {
            QVERIFY2(s.pageStatus().contains(QStringLiteral("musette stop")), qPrintable(s.pageStatus()));
        });
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
