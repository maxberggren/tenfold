// The Arp page: its patterns and rhythms as pure functions, its engine on a
// clock of the test's own (every onset checked against the beat grid to the
// sample), and the page played through the real session.

#include <QImage>
#include <QPainter>
#include <map>
#include <set>

#include "arp.h"
#include "testkit.h"

using namespace tf;

namespace {

// A studio clock for the engine alone: the transport moves on with every
// block, commands carry times on the same clock.
struct Clock : AudioContext {
    Transport tr;
    Harmony h;
    float duckEnv[kBlock] = {};
    double peak = 0;
    explicit Clock(double bpm = 120) {
        tr.rate = 48000;
        tr.bpm = bpm;
        tr.beatsPerSample = bpm / 60.0 / 48000.0;
        h.tuning.set(9, 0, 4, false); // A natural minor
    }
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return double(tr.sample) / double(tr.rate); }
    int offsetOf(double t, int n) const override {
        const double o = (t - now()) * tr.rate;
        return o <= 0 ? 0 : o >= n ? n - 1 : int(o);
    }
    void pump(int, float) override {}
    void setChord(int root, const int *tones, int count) override {
        h.chordRoot = root;
        h.chordCount = count;
        for (int i = 0; i < count; ++i)
            h.chordTones[i] = tones[i];
        ++h.chordSeq;
    }
    const float *duck() const override { return duckEnv; }
    double timeOfBeat(double b) const { return b / tr.beatsPerSample / tr.rate; }

    // Renders up to (not including) the given beat, in uneven blocks so
    // events land anywhere within them.
    void runTo(arp::Engine &e, double beat) {
        static const int sizes[] = {32, 17, 32, 5, 29, 32, 1, 32, 23};
        int i = 0;
        float l[kBlock], r[kBlock];
        for (;;) {
            const double left = std::ceil((beat - tr.beat) / tr.beatsPerSample - 1e-7);
            if (left <= 0)
                break;
            const int n = int(std::min<double>(sizes[i++ % 9], left));
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
            e.render(l, r, n, *this);
            for (int k = 0; k < n; ++k)
                peak = std::max<double>(peak, std::max(std::fabs(l[k]), std::fabs(r[k])));
            tr.sample += uint64_t(n);
            tr.beat += double(n) * tr.beatsPerSample;
        }
    }
};

arp::Engine *fresh(std::unique_ptr<arp::Engine> &e) {
    e = std::make_unique<arp::Engine>();
    e->init(48000);
    return e.get();
}

void state(arp::Engine &e, Clock &c, const arp::Settings &s, double beat, int id = arp::kStateId) {
    Command cmd;
    cmd.type = arp::kState;
    cmd.id = id;
    cmd.t = c.timeOfBeat(beat);
    s.pack(cmd);
    e.command(cmd, c);
}

void note(arp::Engine &e, Clock &c, uint16_t type, int id, int col, int row, double beat, int stack = 0,
          int phase = -1) {
    Command cmd;
    cmd.type = type;
    cmd.id = id;
    cmd.t = c.timeOfBeat(beat);
    cmd.i[0] = col;
    cmd.i[1] = stack | row << 4;
    cmd.i[3] = phase;
    cmd.f[0] = 0.8f;
    e.command(cmd, c);
}

arp::Engine *engineOf(Session &s) {
    for (int p = 0; p < s.partCount(); ++p)
        if (s.pageName(p) == QStringLiteral("Arp"))
            return dynamic_cast<arp::Engine *>(s.studio()->engine(p));
    return nullptr;
}

// Pitch classes of A natural minor.
bool inAMinor(float midi) {
    static const std::set<int> pcs = {9, 11, 0, 2, 4, 5, 7};
    return pcs.count(((int(std::lround(midi)) % 12) + 12) % 12) > 0;
}

// The grid's columns on the pad; row 0 sits at y 0.32, row 1 at 0.7.
std::string gridX(int col) { return std::to_string(0.58 * (col + 0.5) / 8.0); }

}

class ArpTests : public QObject {
    Q_OBJECT
private slots:
    // --- patterns and rhythm ------------------------------------------------

    void euclideanRhythmsSpreadTheirHits() {
        // Three in eight is the tresillo: x..x..x.
        QString s;
        for (int i = 0; i < 8; ++i)
            s += arp::euclid(i, 3, 8, 0) ? 'x' : '.';
        QCOMPARE(s, QStringLiteral("x..x..x."));
        // Every count lands exactly that many hits in a bar, at any turn.
        for (int hits = 1; hits <= 16; ++hits)
            for (int rot = 0; rot < 16; ++rot) {
                int n = 0;
                for (int i = 0; i < 16; ++i)
                    n += arp::euclid(i, hits, 16, rot);
                QCOMPARE(n, hits);
            }
        // Turning by one moves the rhythm one step later.
        for (int i = 0; i < 16; ++i)
            QCOMPARE(arp::euclid(i + 1, 5, 16, 1), arp::euclid(i, 5, 16, 0));
    }

    void patternsWalkTheChordTheirOwnWay() {
        arp::PoolView pv;
        const int deg[3] = {4, 0, 2}; // played E, A, C (degrees of A minor)
        const float vel[3] = {1, 1, 1};
        arp::buildPool(deg, vel, 3, 2, 7, pv);
        auto run = [&](int pattern, int steps) {
            QList<int> out;
            for (int s = 0; s < steps; ++s) {
                int d[16];
                float v[16];
                const int n = arp::patternNotes(pattern, pv, uint64_t(s), 1, 2, 7, d, v, 16);
                for (int k = 0; k < n; ++k)
                    out << d[k];
                if (n > 1)
                    out << -1;
            }
            return out;
        };
        QCOMPARE(run(arp::Up, 7), (QList<int>{0, 2, 4, 7, 9, 11, 0}));
        QCOMPARE(run(arp::Down, 6), (QList<int>{11, 9, 7, 4, 2, 0}));
        QCOMPARE(run(arp::UpDown, 11), (QList<int>{0, 2, 4, 7, 9, 11, 9, 7, 4, 2, 0}));
        QCOMPARE(run(arp::Converge, 6), (QList<int>{0, 11, 2, 9, 4, 7}));
        QCOMPARE(run(arp::Order, 6), (QList<int>{4, 0, 2, 11, 7, 9}));
        QCOMPARE(run(arp::Pedal, 6), (QList<int>{0, 2, 0, 4, 0, 7}));
        QCOMPARE(run(arp::ChordStab, 2), (QList<int>{0, 2, 4, -1, 7, 9, 11, -1}));
        // Random never repeats a note straight away, reaches them all, and
        // the same dice give the same notes.
        const QList<int> r1 = run(arp::Random, 64), r2 = run(arp::Random, 64);
        QCOMPARE(r1, r2);
        for (int i = 1; i < r1.size(); ++i)
            QVERIFY(r1[i] != r1[i - 1]);
        QCOMPARE(int(QSet<int>(r1.begin(), r1.end()).size()), 6);
    }

    void settingsSurviveTheTripThroughACommand() {
        arp::Settings s;
        s.pattern = arp::Pedal;
        s.rate = arp::SixteenthTriplet;
        s.octaves = 3;
        s.reg = 2;
        s.sound = arp::Acid;
        s.echoTime = 4;
        s.accent = 2;
        s.stack = 4;
        s.follow = true;
        s.rotate = 13;
        s.seed = 0xABCDE;
        s.gate = 0.33f;
        s.swing = 0.4f;
        s.density = 0.5f;
        s.chaos = 0.7f;
        s.cutoff = 0.2f;
        s.res = 0.9f;
        s.color = 0.6f;
        s.echo = 0.45f;
        s.accentDepth = 0.8f;
        s.ratchet = 0.3f;
        s.envBoost = 0.25f;
        s.throwFb = 0.5f;
        Command c;
        s.pack(c);
        arp::Settings t;
        t.unpack(c);
        QVERIFY(t == s);
        QCOMPARE(t.rotate, 13);
        QVERIFY(t.follow);
        QVERIFY(std::fabs(t.ratchet - 0.3f) < 0.002f);
    }

    // --- the engine on the grid ---------------------------------------------

    void everyStepLandsOnTheGridToTheSample() {
        // All five rates, straight and swung: each onset falls on the
        // sample its step's beat position says, and none is missed.
        for (int rate = 0; rate < arp::RateCount; ++rate)
            for (float swing : {0.0f, 0.6f}) {
                Clock c(128);
                std::unique_ptr<arp::Engine> owner;
                arp::Engine &e = *fresh(owner);
                arp::Settings s;
                s.rate = rate;
                s.swing = swing;
                state(e, c, s, 0.5);
                note(e, c, Command::On, 1, 0, 0, 0.5, 2);
                c.runTo(e, 8.0);
                const auto log = e.log();
                QVERIFY(log.size() > 8);
                for (size_t i = 1; i < log.size(); ++i) {
                    const auto &f = log[i];
                    const int64_t step = arp::stepAt(f.beat + 1e-9, rate, swing);
                    const double want = arp::stepStart(step, rate, swing);
                    const double expect = std::ceil(want / c.tr.beatsPerSample - 1e-7);
                    QVERIFY2(std::fabs(double(f.sample) - expect) <= 1.0,
                             qPrintable(QStringLiteral("rate %1 swing %2: onset at sample %3, step at %4")
                                            .arg(rate)
                                            .arg(swing)
                                            .arg(f.sample)
                                            .arg(expect)));
                }
                const int64_t first = arp::stepAt(log[1].beat + 1e-9, rate, swing);
                const int64_t last = arp::stepAt(log.back().beat + 1e-9, rate, swing);
                QCOMPARE(int64_t(log.size()) - 1, last - first + 1);
            }
    }

    void aLateFingerIsCaughtAnEarlyOneWaits() {
        // 1/16 at 120 BPM: a step is 125 ms. A finger 25 ms after a step
        // plays at once; one 70 ms after waits for the next step.
        {
            Clock c(120);
            std::unique_ptr<arp::Engine> owner;
            arp::Engine &e = *fresh(owner);
            note(e, c, Command::On, 1, 0, 0, 1.05, 2);
            c.runTo(e, 1.6);
            QVERIFY(!e.log().empty());
            QVERIFY(std::fabs(e.log().front().beat - 1.05) < 0.001);
        }
        {
            Clock c(120);
            std::unique_ptr<arp::Engine> owner;
            arp::Engine &e = *fresh(owner);
            note(e, c, Command::On, 1, 0, 0, 1.14, 2);
            c.runTo(e, 1.6);
            QVERIFY(!e.log().empty());
            QVERIFY(std::fabs(e.log().front().beat - 1.25) < 1e-6);
        }
    }

    void aTapShorterThanAStepStillSounds() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        // Lands 60% into a step (too late to catch) and lifts before the next.
        note(e, c, Command::On, 1, 3, 0, 1.15, 0);
        note(e, c, Command::Off, 1, 3, 0, 1.2, 0);
        c.runTo(e, 3.0);
        QCOMPARE(int(e.log().size()), 1);
        QVERIFY(std::fabs(e.log().front().beat - 1.25) < 1e-6);
    }

    void rollsSplitAStepEvenly() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.ratchet = 1.0f; // every step rolls, four to a step
        state(e, c, s, 0.9);
        note(e, c, Command::On, 1, 0, 0, 1.0, 0);
        c.runTo(e, 2.0);
        const auto log = e.log();
        QCOMPARE(int(log.size()), 16);
        for (size_t i = 0; i < log.size(); ++i)
            QVERIFY(std::fabs(log[i].beat - (1.0 + 0.0625 * double(i))) < 1e-6);
        // A roll is one note, fading.
        QCOMPARE(log[0].midi, log[3].midi);
        QVERIFY(log[3].vel < log[0].vel);
    }

    void euclidThinsTheBarToItsHits() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.density = 5.0f / 16.0f;
        state(e, c, s, 3.9);
        note(e, c, Command::On, 1, 0, 0, 4.0, 2);
        c.runTo(e, 12.0);
        QCOMPARE(int(e.log().size()), 10); // two bars of five
        for (const auto &f : e.log())
            QVERIFY(arp::euclid(int64_t(std::llround(f.beat / 0.25)), 5, 16, 0));
    }

    void theFirstNoteGrowsIntoItsChordAndAllStayInKey() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.octaves = 1;
        state(e, c, s, 0.9);
        note(e, c, Command::On, 1, 3, 0, 1.0, 2); // D grows into D F A
        c.runTo(e, 2.0);
        QCOMPARE(int(e.log().size()), 4);
        QCOMPARE(int(std::lround(e.log()[0].midi)), 62); // D4
        QCOMPARE(int(std::lround(e.log()[1].midi)), 65); // F4
        QCOMPARE(int(std::lround(e.log()[2].midi)), 69); // A4
        QCOMPARE(int(std::lround(e.log()[3].midi)), 62);
        for (const auto &f : e.log())
            QVERIFY(inAMinor(f.midi));
    }

    void followingTheChordsTracksEveryChange() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.follow = true;
        s.octaves = 1;
        state(e, c, s, 0.5);
        const int am[3] = {0, 2, 4}, dm[3] = {3, 5, 7};
        c.setChord(0, am, 3);
        note(e, c, Command::On, 1, 0, 0, 1.0); // a range from A3 up an octave
        c.runTo(e, 3.0);
        std::set<int> before;
        for (const auto &f : e.log())
            before.insert(int(std::lround(f.midi)) % 12);
        QCOMPARE(before, (std::set<int>{9, 0, 4})); // A C E
        e.clearLog();
        c.setChord(3, dm, 3);
        c.runTo(e, 5.0);
        std::set<int> after;
        for (const auto &f : e.log())
            after.insert(int(std::lround(f.midi)) % 12);
        QCOMPARE(after, (std::set<int>{2, 5, 9})); // D F A
    }

    void aLoopComingRoundStartsWhereThePatternWas() {
        // A replayed note carrying the pattern position starts there: up
        // through a triad over two octaves, position 4 is C an octave up.
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        note(e, c, Command::On, (1 << 30) | 5, 0, 0, 4.0, 2, 4);
        c.runTo(e, 4.2);
        QCOMPARE(int(e.log().size()), 1);
        QCOMPARE(int(std::lround(e.log()[0].midi)), 72);
        QCOMPARE(e.log()[0].lane, 1);
    }

    void chaosIsRepeatableAndStaysInKey() {
        auto take = [](uint32_t seed) {
            Clock c(120);
            std::unique_ptr<arp::Engine> owner;
            arp::Engine &e = *fresh(owner);
            arp::Settings s;
            s.chaos = 0.8f;
            s.seed = seed;
            state(e, c, s, 0.9);
            note(e, c, Command::On, 1, 0, 0, 1.0, 2);
            c.runTo(e, 17.0);
            std::vector<std::pair<int64_t, int>> out;
            for (const auto &f : e.log()) {
                if (!inAMinor(f.midi))
                    return std::vector<std::pair<int64_t, int>>{};
                out.push_back({int64_t(std::llround(f.beat * 1e4)), int(std::lround(f.midi))});
            }
            return out;
        };
        const auto a = take(7), b = take(7), other = take(8);
        QVERIFY(!a.empty());
        QVERIFY(a == b);
        QVERIFY(a != other);
        // Some steps skipped, never most of them.
        QVERIFY2(a.size() < 64 && a.size() > 34, qPrintable(QString::number(a.size())));
    }

    void tripletsRunThreeToTheBeat() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.rate = arp::EighthTriplet;
        state(e, c, s, 0.9);
        note(e, c, Command::On, 1, 0, 0, 1.0, 2);
        c.runTo(e, 3.0);
        QCOMPARE(int(e.log().size()), 6);
        for (size_t i = 0; i < e.log().size(); ++i)
            QVERIFY(std::fabs(e.log()[i].beat - (1.0 + double(i) / 3.0)) < 1e-6);
    }

    void aRateChangeKeepsTheBar() {
        // From 1/16 to 1/32 mid-beat: the new steps are still on the grid.
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        note(e, c, Command::On, 1, 0, 0, 1.0, 2);
        arp::Settings s;
        s.rate = arp::ThirtySecond;
        state(e, c, s, 2.3);
        c.runTo(e, 3.0);
        int fast = 0;
        for (const auto &f : e.log()) {
            const double grid = f.beat < 2.3 ? 0.25 : 0.125;
            QVERIFY(std::fabs(f.beat / grid - std::round(f.beat / grid)) < 1e-6);
            fast += f.beat > 2.3;
        }
        QCOMPARE(fast, 5); // 2.375 … 2.875
    }

    void aTiedAcidLineSlidesOnOneVoice() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.sound = arp::Acid;
        s.gate = 1.0f;
        state(e, c, s, 0.9);
        note(e, c, Command::On, 1, 0, 0, 1.0, 2);
        c.runTo(e, 3.1);
        QCOMPARE(int(e.log().size()), 9);
        QCOMPARE(e.voices(), 1); // every note slid into the one before
        std::set<int> pitches;
        for (const auto &f : e.log())
            pitches.insert(int(std::lround(f.midi)));
        QCOMPARE(int(pitches.size()), 6);
    }

    void aHighRunStopsClimbingInsteadOfFolding() {
        // Register 3 through four octaves would pass E7: the climb stops an
        // octave short, so "up" still only goes up within each cycle.
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        arp::Settings s;
        s.reg = 3;
        s.octaves = 4;
        state(e, c, s, 0.9);
        note(e, c, Command::On, 1, 0, 0, 1.0, 2);
        c.runTo(e, 5.0);
        const auto log = e.log();
        std::set<float> pitches;
        for (const auto &f : log) {
            QVERIFY(f.midi <= 100.0f);
            pitches.insert(f.midi);
        }
        const size_t cycle = pitches.size();
        QVERIFY(cycle >= 3);
        for (size_t i = 0; i < log.size(); ++i) {
            QCOMPARE(log[i].midi, log[i % cycle].midi);
            if (i % cycle)
                QVERIFY(log[i].midi > log[i - 1].midi);
        }
    }

    void stopsWhenTheNotesGoAndFallsSilent() {
        Clock c(120);
        std::unique_ptr<arp::Engine> owner;
        arp::Engine &e = *fresh(owner);
        note(e, c, Command::On, 1, 0, 0, 1.0, 2);
        note(e, c, Command::Off, 1, 0, 0, 2.0, 2);
        c.runTo(e, 6.0);
        for (const auto &f : e.log())
            QVERIFY(f.beat < 2.0);
        QVERIFY(c.peak < 1.0);
        QVERIFY(c.peak > 0.05);
        e.clearLog();
        c.peak = 0;
        c.runTo(e, 12.0); // the echo has died away by now
        QVERIFY(e.log().empty());
        QVERIFY(c.peak < 1e-3);
    }

    // --- the page through the session ---------------------------------------

    void oneFingerIsARunningTriadInKey() {
        std::vector<arp::Engine::Fired> log;
        const auto take = testkit::play("page 0 5\ndown 0.5 1 " + gridX(0) + " 0.32\nup 4.5 1\nend 5.5\n",
                                        [&](Session &s) { log = engineOf(s)->log(); });
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 0.99);
        QVERIFY(take.rms(1, 4) > 0.03);
        QVERIFY(log.size() >= 30);
        std::set<int> pcs;
        for (const auto &f : log) {
            QVERIFY(inAMinor(f.midi));
            pcs.insert(int(std::lround(f.midi)) % 12);
        }
        QCOMPARE(pcs, (std::set<int>{9, 0, 4})); // the A minor triad
        // On the sixteenths of the studio's clock, but for the first, which
        // the arp caught as the finger landed.
        for (size_t i = 1; i < log.size(); ++i)
            QVERIFY2(std::fabs(log[i].beat * 4 - std::round(log[i].beat * 4)) < 4 * 124.0 / 60.0 / 48000.0, // a sample
                     qPrintable(QStringLiteral("note %1 at beat %2").arg(i).arg(log[i].beat, 0, 'f', 6)));
    }

    void everySoundSitsAtASimilarLevel() {
        std::vector<double> levels;
        for (const char *key : {"Z", "X", "C", "V", "B"}) {
            const auto take = testkit::play(std::string("page 0 5\nkey 0.2 ") + key + "\ndown 0.5 1 " + gridX(0) +
                                            " 0.32\nup 4.5 1\nend 4.6\n");
            QVERIFY(take.finite());
            QVERIFY(take.peak() < 0.99);
            levels.push_back(20 * std::log10(take.rms(1, 4.5)));
        }
        const double lo = *std::min_element(levels.begin(), levels.end());
        const double hi = *std::max_element(levels.begin(), levels.end());
        QVERIFY2(hi - lo < 5.0, qPrintable(QStringLiteral("%1 .. %2 dB").arg(lo).arg(hi)));
    }

    void aClickLatchesAndTheLatchChipReleases() {
        const auto take = testkit::play("page 0 5\ndown 0.5 1 " + gridX(2) + " 0.32\nclick 1.0\nup 1.5 1\n"
                                        "tap 4.0 0.54 0.05\n" // the LATCH chip
                                        "end 7.5\n");
        QVERIFY(take.rms(2.0, 3.8) > 0.02); // still running after the lift
        QVERIFY(take.rms(6.0, 7.5) < 0.003); // stopped, the echo gone too
    }

    void leavingThePageLetsGoOfHeldNotes() {
        const auto take =
            testkit::play("page 0 5\ndown 0.5 1 " + gridX(0) + " 0.32\nkey 1.5 3\nup 5 1\nend 5\n");
        QVERIFY(take.rms(0.6, 1.4) > 0.02);
        QVERIFY(take.rms(3.5, 5.0) < 0.003);
    }

    // Beat b at 124 BPM, in seconds, for scripts.
    static std::string at(double beat) { return std::to_string(beat * 60.0 / 124.0); }

    // Plays a script and returns the arp's log and the studio at the end.
    static testkit::Take run(const std::string &script, std::vector<arp::Engine::Fired> &log, StudioView *view = nullptr,
                             QString *status = nullptr) {
        return testkit::play(script, [&](Session &s) {
            log = engineOf(s)->log();
            if (view)
                *view = s.view();
            if (status)
                *status = s.pageStatus();
        });
    }

    // Every loop note against the live note one or more loop lengths
    // before it, inside the take's window [from, to): returns how many
    // matched, and fails on any that differ.
    static int matchesTheTake(const std::vector<arp::Engine::Fired> &log, double from, double to, double len) {
        // (A chord changed a hair late plays its step over the old one's, so
        // a step may hold two notes; the loop's must be among them.)
        std::map<int64_t, std::set<int>> live;
        for (const auto &f : log)
            if (f.lane == 0)
                live[int64_t(std::llround(f.beat * 8))].insert(int(std::lround(f.midi)));
        int matched = 0;
        for (const auto &f : log) {
            if (f.lane != 1)
                continue;
            double back = f.beat;
            while (back >= to - 1e-6)
                back -= len;
            if (back < from - 1e-6)
                continue;
            const auto it = live.find(int64_t(std::llround(back * 8)));
            if (it == live.end())
                continue;
            if (!it->second.count(int(std::lround(f.midi)))) {
                qWarning("loop note at beat %.3f is %d, the take's at %.3f was %d", f.beat, int(std::lround(f.midi)),
                         back, *it->second.begin());
                return -1;
            }
            ++matched;
        }
        return matched;
    }

    void spaceLoopsWhatWasPlayedFromWhereThePatternWas() {
        // A chord held from bar 1, Space just after bar 3's line: a held
        // chord's take is the bar after its last new note, which the arp
        // played; it loops at once, note for note, and the hand still down
        // steps aside. (The metronome stands in for a playing loop.)
        std::vector<arp::Engine::Fired> log;
        StudioView view;
        const auto take = run("key 0.1 Ctrl+M\npage 0.05 5\ndown " + at(4.0) + " 1 " + gridX(0) + " 0.32\ndown " +
                                  at(4.01) + " 2 " + gridX(4) + " 0.7\nkey " + at(12.05) + " Space\nup " + at(13.5) +
                                  " 1\nup " + at(13.5) + " 2\nend 16\n",
                              log, &view);
        QVERIFY(take.error.empty());
        QCOMPARE(view.parts[4].playing, 0);
        QCOMPARE(int(view.parts[4].clips[0].lengthBeats), 4);
        QVERIFY(take.rms(10, 16) > 0.02);
        QVERIFY(matchesTheTake(log, 4.0, 8.0, 4.0) >= 50);
        for (const auto &f : log)
            if (f.lane == 0)
                QVERIFY2(f.beat < 12.1, qPrintable(QString::number(f.beat)));
    }

    void spaceJustAfterTheBarLineHandsOverAtOnce() {
        // Held or latched, the loop takes over the moment it lands and the
        // level stays where it was: no doubling, no gap.
        for (const bool latched : {false, true}) {
            std::vector<arp::Engine::Fired> log;
            QString status;
            const std::string hand = latched ? "click " + at(5.0) + " len=0.1\nup " + at(6.0) + " 1\n"
                                             : "up " + at(19.0) + " 1\n";
            const auto take = run("key 0.1 Ctrl+M\npage 0.05 5\ntap 0.2 0.90 0.005\ndown " + at(4.0) + " 1 " +
                                      gridX(0) + " 0.32\n" + hand + "key " + at(12.05) + " Space\nend " + at(20.0) + "\n",
                                  log, nullptr, &status);
            auto rms = [&](double b0, double b1) { return 20 * std::log10(take.rms(b0 * 60 / 124, b1 * 60 / 124)); };
            auto peak = [&](double b0, double b1) {
                double p = 0;
                for (size_t i = size_t(b0 * 60 / 124 * 48000); i < size_t(b1 * 60 / 124 * 48000); ++i)
                    p = std::max({p, double(std::fabs(take.left[i])), double(std::fabs(take.right[i]))});
                return 20 * std::log10(p);
            };
            const double before = rms(6, 12), after = rms(12.5, 19.5);
            QVERIFY2(std::fabs(after - before) < 1.0,
                     qPrintable(QStringLiteral("%1: %2 then %3 dB").arg(latched).arg(before).arg(after)));
            QVERIFY2(peak(12, 12.6) < peak(6, 12) + 1.0, qPrintable(QString::number(peak(12, 12.6))));
            for (const auto &f : log)
                if (f.lane == 0)
                    QVERIFY(f.beat < 12.1);
            QVERIFY(matchesTheTake(log, 4.0, 8.0, 4.0) >= 25);
            QVERIFY(!status.contains(QStringLiteral("latched")));
        }
    }

    void aLoopThatJoinsMidBarStartsWhereThePatternWas() {
        // The hand alternates A and D every two beats; Space a beat into a
        // bar loops the bar it repeated, from mid-bar. The loop joins on the
        // note held at that point, part way through its pattern, and plays
        // the take's notes from there on, pass after pass.
        std::vector<arp::Engine::Fired> log;
        StudioView view;
        std::string script = "key 0.1 Ctrl+M\npage 0.05 5\ntap 0.2 0.90 0.005\n";
        int id = 1;
        for (double b = 4.0; b < 13.0; b += 2.0, ++id) {
            const int col = (int(b) / 2) % 2 ? 3 : 0;
            script += "down " + at(b) + " " + std::to_string(id) + " " + gridX(col) + " 0.32\nup " +
                      at(std::min(b + 2.0, 13.5)) + " " + std::to_string(id) + "\n";
        }
        script += "key " + at(13.05) + " Space\nend " + at(30.0) + "\n";
        run(script, log, &view);
        QCOMPARE(view.parts[4].playing, 0);
        const double len = view.parts[4].clips[0].lengthBeats;
        QCOMPARE(int(len), 4);
        const int matched = matchesTheTake(log, 13.0 - len, 13.0, len);
        QVERIFY2(matched >= 50, qPrintable(QString::number(matched)));
    }

    void aOneBarLoopRollsTheSameDiceEveryPass() {
        // Chaos keys its dice to a two-bar phrase; a one-bar loop of it must
        // still repeat the bar it took, pass after pass.
        std::vector<arp::Engine::Fired> log;
        run("key 0.1 Ctrl+M\npage 0.05 5\ntap 0.2 0.90 0.005\ntap 0.4 0.61 0.2225\n" // echo off, chaos ~80%
            "down " + at(4.0) + " 1 " + gridX(0) + " 0.32\nkey " + at(8.05) + " Space\nup " + at(8.6) + " 1\nend " +
                at(25.0) + "\n",
            log);
        std::map<int, int> taken;
        for (const auto &f : log)
            if (f.lane == 0 && f.beat >= 4.0 - 1e-6 && f.beat < 8.0 - 1e-6)
                taken[int(std::lround((f.beat - 4.0) * 8))] = int(std::lround(f.midi));
        QVERIFY(taken.size() >= 6);
        QVERIFY(taken.size() < 16); // chaos skipped some
        // The hand played the first step of pass one itself (the loop landed
        // just after it): the loop carries on from the next.
        bool handPlayedTheOne = false;
        for (const auto &f : log)
            handPlayedTheOne |= f.lane == 0 && std::fabs(f.beat - 8.0) < 1e-3;
        for (int pass = 0; pass < 4; ++pass) {
            std::map<int, int> got;
            const double b0 = 8.0 + 4.0 * pass;
            for (const auto &f : log)
                if (f.lane == 1 && f.beat >= b0 - 1e-6 && f.beat < b0 + 4.0 - 1e-6)
                    got[int(std::lround((f.beat - b0) * 8))] = int(std::lround(f.midi));
            if (pass == 0 && handPlayedTheOne && !got.count(0) && taken.count(0))
                got[0] = taken.at(0);
            QVERIFY2(got == taken, qPrintable(QStringLiteral("pass %1: %2 notes, the take %3")
                                                   .arg(pass + 1)
                                                   .arg(got.size())
                                                   .arg(taken.size())));
        }
    }

    void aNormalTouchOnThePadsNeitherRollsNorBitesNorThrows() {
        std::vector<arp::Engine::Fired> log;
        arp::Settings live;
        testkit::play("page 0.05 5\ndown 0.3 1 " + gridX(0) + " 0.32\n"
                      "down 0.5 2 0.95 0.75 p=0.6\ndown 0.5 3 0.95 0.45 p=0.6\ndown 0.5 4 0.95 0.12 p=0.62\nend 3\n",
                      [&](Session &s) {
                          log = engineOf(s)->log();
                          live = engineOf(s)->laneSettings(0);
                      });
        QCOMPARE(live.ratchet, 0.0f);
        QCOMPARE(live.envBoost, 0.0f);
        QCOMPARE(live.throwFb, 0.0f);
        for (size_t i = 1; i < log.size(); ++i)
            QVERIFY(log[i].beat - log[i - 1].beat > 0.2); // no rolls inside a step
        // A hard press does roll.
        testkit::play("page 0.05 5\ndown 0.3 1 " + gridX(0) + " 0.32\ndown 0.5 2 0.95 0.75 p=0.97\nend 3\n",
                      [&](Session &s) { live = engineOf(s)->laneSettings(0); });
        QVERIFY(live.ratchet > 0.8f);
    }

    void aClickDuringAHardPressDoesNotLatch() {
        const auto take = testkit::play("page 0.05 5\ndown 0.3 1 " + gridX(0) + " 0.32\n"
                                        "down 0.6 2 0.95 0.75 p=0.95\nclick 0.8 len=1.0\nup 2.0 2\nup 2.2 1\nend 5\n");
        QVERIFY(take.rms(0.5, 2.0) > 0.02);
        QVERIFY(take.rms(4.0, 5.0) < 0.003); // not latched: it stopped with the hand
    }

    void aRestingFingerSendsNoSettings() {
        // A finger resting on the tone pad trembles by a fifth of a percent:
        // that is not a move, and nothing is sent.
        std::string still = "page 0.05 5\ndown 0.3 1 " + gridX(0) + " 0.32\ndown 0.5 2 0.9 0.5\n";
        std::string shaky = still;
        for (int i = 0; i < 200; ++i)
            shaky += "move " + std::to_string(0.6 + 0.01 * i) + " 2 " + std::to_string(0.9 + (i % 2 ? 0.0004 : -0.0004)) +
                     " " + std::to_string(0.5 + (i % 3 - 1) * 0.0005) + "\n";
        int a = 0, b = 0;
        testkit::play(still + "end 2.7\n", [&](Session &s) { a = engineOf(s)->statesReceived(); });
        testkit::play(shaky + "end 2.7\n", [&](Session &s) { b = engineOf(s)->statesReceived(); });
        QCOMPARE(b, a);
    }

    void tapsAndHoldsNeverLoopByThemselves() {
        // A rhythm tapped and then a held note is ordinary playing here:
        // only Space makes a loop.
        StudioView view;
        testkit::play("page 0 5\n"
                      "tap 0.5 " + gridX(0) + " 0.32\ntap 1.0 " + gridX(2) + " 0.32\n"
                      "tap 1.5 " + gridX(4) + " 0.32\ntap 2.0 " + gridX(2) + " 0.32\n"
                      "down 2.5 1 " + gridX(0) + " 0.32\nup 3.5 1\nend 5\n",
                      [&](Session &s) { view = s.view(); });
        QCOMPARE(view.parts[4].playing, -1);
        for (const auto &c : view.parts[4].clips)
            QVERIFY(c.state == ClipView::Empty);
    }

    void withNoNotesTheRightHandShapesTheLoop() {
        // A loop at 1/16; a finger high on the rate pad doubles it to 1/32
        // while it stays, and the loop goes back when it lifts.
        std::vector<arp::Engine::Fired> log;
        testkit::play("key 0.1 Ctrl+M\npage 0 5\ndown " + at(4.0) + " 1 " + gridX(0) + " 0.32\nkey " + at(12.05) + " Space\nup 6.5 1\n"
                      "down 8.0 2 0.66 0.97\nup 10.0 2\nend 13\n",
                      [&](Session &s) { log = engineOf(s)->log(); });
        auto count = [&](double from, double to) {
            int n = 0;
            for (const auto &f : log)
                n += f.lane == 1 && f.beat >= from && f.beat < to;
            return n;
        };
        const double bps = 124.0 / 60.0;
        const int before = count(6.0 * bps, 7.5 * bps), during = count(8.3 * bps, 9.8 * bps),
                  after = count(10.5 * bps, 12.0 * bps);
        QVERIFY(before > 8);
        QVERIFY2(during > before * 3 / 2, qPrintable(QStringLiteral("%1 then %2").arg(before).arg(during)));
        QVERIFY2(std::abs(after - before) <= 2, qPrintable(QStringLiteral("%1 then %2").arg(before).arg(after)));
    }

    // --- the shared keys and the mouse --------------------------------------

    void theSharedKeysReachTheSoundFollowAndRegister() {
        // ← → step through the sounds (round the end), the key left of 1
        // (§, or ` on US layouts) follows the chords, Shift+← → the rate,
        // Shift+↑ ↓ the range.
        QString status;
        float follow = -1;
        testkit::play("page 0 5\nkey 0.1 Right\nkey 0.2 Right\nkey 0.3 §\nkey 0.4 Shift+Right\nend 0.6\n",
                      [&](Session &s) {
                          status = s.pageStatus();
                          follow = s.probe(4, QStringLiteral("follow"));
                      });
        QVERIFY2(status.contains(QStringLiteral("supersaw")), qPrintable(status));
        QVERIFY2(status.contains(QStringLiteral("1/16T")), qPrintable(status));
        QCOMPARE(follow, 1.0f);
        testkit::play("page 0 5\nkey 0.1 Left\nkey 0.2 `\nkey 0.3 \\\nkey 0.4 Shift+Up\nend 0.6\n",
                      [&](Session &s) {
                          status = s.pageStatus();
                          follow = s.probe(4, QStringLiteral("follow"));
                      });
        QVERIFY2(status.contains(QStringLiteral("acid")), qPrintable(status));
        QVERIFY2(status.contains(QStringLiteral("3 oct")), qPrintable(status));
        QCOMPARE(follow, 0.0f);
        // M, the tutorial's key, still follows; [ ] still set the rate.
        testkit::play("page 0 5\nkey 0.1 M\nkey 0.2 [\nend 0.4\n", [&](Session &s) {
            follow = s.probe(4, QStringLiteral("follow"));
            status = s.pageStatus();
        });
        QCOMPARE(follow, 1.0f);
        QVERIFY2(status.contains(QStringLiteral("1/8T")), qPrintable(status));
    }

    void registerAndToneKeysMoveTheNotes() {
        // ↑ lifts the grid an octave; + brightens. A note held under each
        // setting: the first an octave above the second's run.
        std::vector<arp::Engine::Fired> low, high;
        testkit::play("page 0 5\ndown 0.5 1 " + gridX(0) + " 0.32\nup 2.0 1\nend 2.1\n",
                      [&](Session &s) { low = engineOf(s)->log(); });
        testkit::play("page 0 5\nkey 0.1 Up\nkey 0.2 +\ndown 0.5 1 " + gridX(0) + " 0.32\nup 2.0 1\nend 2.1\n",
                      [&](Session &s) { high = engineOf(s)->log(); });
        QVERIFY(!low.empty() && !high.empty());
        QCOMPARE(high.front().midi, low.front().midi + 12.0f);
        // + and = brighten, - darkens, a twentieth at a time.
        float tone = 0;
        testkit::play("page 0 5\nkey 0.1 +\nkey 0.2 =\nkey 0.3 -\nkey 0.4 +\nend 0.6\n",
                      [&](Session &s) { tone = s.probe(4, QStringLiteral("tone")); });
        QVERIFY2(std::fabs(tone - 0.65f) < 1e-4f, qPrintable(QString::number(tone)));
    }

    void deleteLetsGoOfTheLatch() {
        const auto take = testkit::play("page 0 5\ndown 0.5 1 " + gridX(2) + " 0.32\nkey 1.0 Return\nup 1.5 1\n"
                                        "key 4.0 Delete\nend 7.5\n");
        QVERIFY(take.rms(2.0, 3.8) > 0.02);  // latched: running after the lift
        QVERIFY(take.rms(6.0, 7.5) < 0.003); // let go, the echo gone too
    }

    // The band under the pad on the scripts' 960×720 window: the pad is
    // 900 × 562.5 px, row 0 of the band 0.048 of its height under it, row 1
    // 0.119. Row 0 holds the clips to 0.45 of the width, then three
    // steppers 0.1713 wide, 0.012 apart; row 1 five columns 0.2025 apart,
    // each 0.19 wide. A stepper's keycaps sit at either end, about 0.04 wide.
    static std::string top(int stepper, bool up) {
        const double x0 = 0.462 + stepper * (0.17133 + 0.012);
        return std::to_string(up ? x0 + 0.17133 - 0.018 : x0 + 0.018);
    }
    static std::string band(int column, bool up) {
        return std::to_string(column * 0.2025 + (up ? 0.19 - 0.02 : 0.02));
    }

    void theBandUnderThePadIsClickable() {
        QString status;
        // Sound up, rate up, range up, first note down.
        testkit::play("page 0 5\nbutton 0.2 " + top(0, true) + " -0.048\nbutton 0.3 " + top(1, true) +
                          " -0.048\nbutton 0.4 " + band(1, true) + " -0.119\nbutton 0.5 " + top(2, false) +
                          " -0.048\nend 0.7\n",
                      [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains(QStringLiteral("glass")), qPrintable(status));
        QVERIFY2(status.contains(QStringLiteral("1/16T")), qPrintable(status));
        QVERIFY2(status.contains(QStringLiteral("3 oct")), qPrintable(status));
        // OVERDUB, the middle of the three chips that fill columns 3 and 4.
        testkit::play("page 0 5\nbutton 0.2 0.80 -0.119\nend 0.4\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains(QStringLiteral("overdub")), qPrintable(status));
        // The first note down from a triad to a fifth: one finger, two notes.
        std::vector<arp::Engine::Fired> log;
        testkit::play("page 0 5\nbutton 0.2 " + top(2, false) + " -0.048\ndown 0.5 1 " + gridX(0) +
                          " 0.32\nup 3.0 1\nend 3.1\n",
                      [&](Session &s) { log = engineOf(s)->log(); });
        std::set<int> pcs;
        for (const auto &f : log)
            pcs.insert(int(std::lround(f.midi)) % 12);
        QCOMPARE(pcs, (std::set<int>{9, 4}));
    }

    void aClickOnTheClipStopsIt() {
        // A loop plays in slot 1; a click on it stops it at the bar.
        StudioView view;
        testkit::play("key 0.1 Ctrl+M\npage 0 5\ndown " + at(4.0) + " 1 " + gridX(0) + " 0.32\nkey " + at(12.05) +
                          " Space\nup 6.5 1\nbutton 8.0 0.025 -0.048\nend 11\n",
                      [&](Session &s) { view = s.view(); });
        QVERIFY(view.parts[4].clips[0].state != ClipView::Empty);
        QCOMPARE(view.parts[4].playing, -1);
    }

    void theMouseIsAFingerOnThePadsChips() {
        // Pressed and let go on the chips drawn on the pad: a pattern, a
        // sound, FOLLOW.
        QString status;
        float follow = -1;
        testkit::play("page 0 5\nmouse 0.2 0.18 0.93 down\nmouse 0.3 0.18 0.93 up\n"
                      "mouse 0.4 0.124 0.07 down\nmouse 0.5 0.124 0.07 up\n"
                      "mouse 0.6 0.456 0.07 down\nmouse 0.7 0.456 0.07 up\nend 0.9\n",
                      [&](Session &s) {
                          status = s.pageStatus();
                          follow = s.probe(4, QStringLiteral("follow"));
                      });
        QVERIFY2(status.contains(QStringLiteral("up·down")), qPrintable(status));
        QVERIFY2(status.contains(QStringLiteral("glass")), qPrintable(status));
        QCOMPARE(follow, 1.0f);
    }

    void theMouseLatchesAChordAClickAtATime() {
        // LATCH, then three clicks on the grid: three notes latched; a click
        // on one takes it away; Delete lets go of the rest.
        auto click = [](double t, const std::string &x) {
            return "mouse " + std::to_string(t) + " " + x + " 0.32 down\nmouse " + std::to_string(t + 0.1) + " " + x +
                   " 0.32 up\n";
        };
        const std::string latch = "page 0 5\nmouse 0.2 0.539 0.07 down\nmouse 0.3 0.539 0.07 up\n" +
                                  click(0.5, gridX(0)) + click(0.8, gridX(2)) + click(1.1, gridX(4));
        QString status;
        testkit::play(latch + "end 1.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.startsWith(QStringLiteral("3 notes")) && status.contains(QStringLiteral("latched")),
                 qPrintable(status));
        testkit::play(latch + click(1.5, gridX(2)) + "end 1.8\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.startsWith(QStringLiteral("2 notes")), qPrintable(status));
        const auto take = testkit::play(latch + "key 2.0 Delete\nend 4.5\n",
                                        [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.startsWith(QStringLiteral("No notes")), qPrintable(status));
        QVERIFY(take.rms(1.2, 1.9) > 0.02);
        QVERIFY(take.rms(4.0, 4.5) < 0.003);
    }

    void thePageDrawsWithHandsOnIt() {
        testkit::play("page 0 5\ndown 0.3 1 " + gridX(1) + " 0.32\ndown 0.4 2 0.7 0.8\ndown 0.5 3 0.9 0.2\nend 1.2\n",
                      [](Session &s) {
                          QImage img(1200, 800, QImage::Format_ARGB32);
                          img.fill(Qt::black);
                          QPainter painter(&img);
                          s.paint(painter, QRectF(30, 14, 1000, 625), 1.2);
                          QVERIFY(!s.pageStatus().isEmpty());
                          int green = 0;
                          for (int y = 0; y < img.height(); y += 4)
                              for (int x = 0; x < img.width(); x += 4) {
                                  const QColor c = img.pixelColor(x, y);
                                  green += c.green() > 200 && c.red() < 120;
                              }
                          QVERIFY(green > 200);
                      });
    }
};

TESTKIT_REGISTER(ArpTests)
#include "arp_tests.moc"
