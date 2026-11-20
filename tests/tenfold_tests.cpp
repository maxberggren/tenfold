#include <QtTest>
#include <cmath>
#include <vector>

#include "electro.h"
#include "engine.h"
#include "gesture.h"
#include "offline.h"
#include "tuning.h"
#include "tutorial.h"

using namespace tf;

namespace {

// Records what the gesture layer asks for instead of playing it.
class RecordingSink : public CommandSink {
public:
    std::vector<Command> commands;
    int nearLoop = -1;
    void command(const Command &c) override { commands.push_back(c); }
    int loopNear(float, float, float) const override { return nearLoop; }
    int count(Cmd t) const {
        int n = 0;
        for (const auto &c : commands)
            n += c.type == t;
        return n;
    }
    const Command *last(Cmd t) const {
        for (auto it = commands.rbegin(); it != commands.rend(); ++it)
            if (it->type == t)
                return &*it;
        return nullptr;
    }
    void clear() { commands.clear(); }
};

struct Finger {
    int id;
    float x, y, p = 0.6f, size = 1.0f, angle = 0;
};

// Row centres, bottom to top: drums, bass, chords, lead, arp.
constexpr float kDrums = 0.1f, kBass = 0.3f, kChords = 0.5f, kLead = 0.7f;

// Drives a gesture layer at the trackpad's own frame rate.
struct Pad {
    explicit Pad(Gesture &gg) : g(gg) {}
    Gesture &g;
    double t = 1.0;
    std::vector<Finger> down;
    bool button = false;
    void frame() {
        Frame f;
        f.t = t;
        f.button = button;
        for (const auto &d : down) {
            Touch &tc = f.touches[f.count++];
            tc.id = d.id;
            tc.x = d.x;
            tc.y = d.y;
            tc.pressure = d.p;
            tc.size = d.size;
            tc.angle = d.angle;
            tc.eccentricity = 0.5f;
        }
        g.frame(f);
    }
    void run(double seconds) {
        const double end = t + seconds;
        while (t < end - 1e-9) {
            t += 0.008;
            frame();
        }
    }
    void land(Finger f) {
        down.push_back(f);
        frame();
    }
    void lift(int id) {
        down.erase(std::remove_if(down.begin(), down.end(), [&](const Finger &f) { return f.id == id; }), down.end());
        frame();
    }
    // Lifting a held finger: its lift counts after a short grace.
    void release(int id) {
        lift(id);
        run(0.05);
    }
    Finger *finger(int id) {
        for (auto &f : down)
            if (f.id == id)
                return &f;
        return nullptr;
    }
    void tap(int id, float x, float y) {
        land({id, x, y, 0.8f});
        run(0.05);
        lift(id);
    }
    int held() const {
        int n = 0;
        const GestureView v = g.view();
        for (int i = 0; i < v.count; ++i)
            n += v.contacts[i].state == TouchState::Voice;
        return n;
    }
};

double rms(const std::vector<float> &v, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += double(v[i]) * v[i];
    return std::sqrt(s / double(std::max<size_t>(1, to - from)));
}

// Energy in a band, by direct DFT at 5 Hz steps (enough for tests).
double band(const std::vector<float> &mono, size_t from, size_t to, double lo, double hi, double rate = 48000) {
    double e = 0;
    for (double f = lo; f < hi; f += 5) {
        double re = 0, im = 0;
        for (size_t n = from; n < to; ++n) {
            re += mono[n] * std::cos(2 * M_PI * f * double(n) / rate);
            im -= mono[n] * std::sin(2 * M_PI * f * double(n) / rate);
        }
        e += re * re + im * im;
    }
    return e;
}

std::vector<float> renderMono(Engine &e, int frames) {
    std::vector<float> st(size_t(frames) * 2);
    std::vector<float> mono(static_cast<size_t>(frames));
    e.render(st.data(), frames);
    for (int i = 0; i < frames; ++i)
        mono[size_t(i)] = 0.5f * (st[2 * size_t(i)] + st[2 * size_t(i) + 1]);
    return mono;
}

}

class TenfoldTests : public QObject {
    Q_OBJECT

private slots:
    void aMinorByDefault() {
        RecordingSink sink;
        Gesture g(&sink);
        const Tuning &t = g.tuning();
        QCOMPARE(t.root(), 9);
        QCOMPARE(QString(t.modeName()), QString("Aeolian"));
        QVERIFY(!t.just());
        QCOMPARE(t.degreeSemis(2), 3.0); // C over A: a minor third
        QCOMPARE(t.degreeSemis(4), 7.0);
    }

    void modesRunBrightToDark() {
        Tuning a, b;
        for (int m = 0; m + 1 < 7; ++m) {
            a.set(0, 0, m, false);
            b.set(0, 0, m + 1, false);
            for (int d = 0; d < 7; ++d)
                QVERIFY(a.degreeSemis(d) >= b.degreeSemis(d));
        }
    }

    void aTapIsAShortNoteRightAway() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.33f, kBass, 0.8f});
        pad.run(0.016);
        // The note is on within two frames of landing.
        QCOMPARE(sink.count(Cmd::NoteOn), 1);
        const Command *on = sink.last(Cmd::NoteOn);
        QCOMPARE(on->i1, int(BassRow));
        pad.run(0.03);
        pad.lift(1);
        QCOMPARE(sink.count(Cmd::NoteOff), 1);
        QVERIFY(g.view().phraseOpen);
    }

    void theDrumRowPlaysTheKit() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.05f, kDrums);
        pad.tap(2, 0.25f, kDrums);
        pad.tap(3, 0.6f, kDrums);
        std::vector<int> pieces;
        for (const auto &c : sink.commands)
            if (c.type == Cmd::Hit)
                pieces.push_back(c.i1);
        QCOMPARE(pieces, (std::vector<int>{int(Drum::Kick), int(Drum::Snare), int(Drum::HatClosed)}));
        QCOMPARE(sink.count(Cmd::NoteOn), 0);
    }

    void holdingADrumRollsIt() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.25f, kDrums, 0.5f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::RollOn), 1);
        pad.finger(1)->p = 0.9f;
        pad.run(0.05);
        QVERIFY(sink.last(Cmd::RollMove)->f[0] > 0.85f);
        pad.release(1);
        QCOMPARE(sink.count(Cmd::RollOff), 1);
    }

    void anOffCentreFingerPlaysInTune() {
        // Wherever a still finger lands in its cell, it plays the cell's note.
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        const float cell = 1.0f / float(g.cells());
        const float x = 5.0f * cell + 0.85f * cell; // well right of centre in cell 5
        pad.land({1, x, kLead, 0.7f});
        pad.run(1.0);
        const double expected = g.tuning().baseMidi() + rowOffset(LeadRow) + g.tuning().degreeSemis(5);
        QVERIFY(std::fabs(sink.last(Cmd::NoteOn)->f[0] - expected) < 0.01);
        QVERIFY(std::fabs(sink.last(Cmd::NoteMove)->f[0] - expected) < 0.05); // within 5 cents
    }

    void holdTakesRolesInOrder() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        pad.land({1, 0.2f, kLead});
        pad.run(0.3);
        pad.land({2, 0.5f, kLead});
        pad.run(0.3);
        pad.land({3, 0.7f, kChords});
        pad.run(0.3);
        std::vector<int> roles;
        const GestureView v = g.view();
        for (int i = 0; i < v.count; ++i)
            roles.push_back(v.contacts[i].role);
        std::sort(roles.begin(), roles.end());
        QCOMPARE(roles, (std::vector<int>{Filter, Env, Width}));
    }

    void slideTurnsTheKnobAndItStays() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        pad.land({1, 0.3f, kLead - 0.05f});
        pad.run(0.3);
        const float before = g.view().knobs[Filter][0];
        for (int i = 0; i < 15; ++i) {
            pad.finger(1)->y += 0.01f;
            pad.run(0.008);
        }
        const float after = g.view().knobs[Filter][0];
        // A small dead zone first, so a resting finger doesn't move the knob.
        QVERIFY(std::fabs(after - std::min(1.0f, before + (0.15f - 0.035f) * 1.6f)) < 0.01f);
        for (int i = 0; i < 10; ++i) {
            pad.finger(1)->angle += 0.05f;
            pad.run(0.008);
        }
        pad.run(0.3);
        QVERIFY(g.view().knobs[Filter][1] > roleDefault(Filter, 1) + 0.2f);
        pad.release(1);
        QCOMPARE(g.view().knobs[Filter][0], after);
    }

    void tapsThenHoldMakeALoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        const double start = pad.t + 0.008;
        for (int i = 0; i < 4; ++i) {
            pad.tap(10 + i, 0.05f, kDrums);
            pad.run(0.5 - 0.058);
        }
        pad.land({20, 0.05f, kDrums});
        const double one = pad.t;
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
        QCOMPARE(sink.count(Cmd::LoopHit), 4);
        QCOMPARE(sink.count(Cmd::LoopReady), 1);
        QCOMPARE(sink.count(Cmd::RollOn), 0);
        const Command *b = sink.last(Cmd::LoopBegin);
        QVERIFY(std::fabs(b->f[0] - float(one - start)) < 0.02f);
        const Command *h = sink.last(Cmd::LoopHit);
        QCOMPARE(h->i1, int(DrumRow));
        QCOMPARE(h->i2, int(Drum::Kick));
        // While it's held, a tap with another finger is added to it.
        pad.tap(30, 0.6f, kDrums);
        QCOMPARE(sink.count(Cmd::Overdub), 1);
        pad.finger(20)->x += 1.0f / float(g.cells());
        pad.run(0.02);
        QCOMPARE(sink.last(Cmd::LoopMod)->i1, 1);
        pad.release(20);
        QCOMPARE(sink.count(Cmd::LoopRelease), 1);
    }

    void aMelodyLoopRemembersNoteLengths() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.3f, kLead});
        pad.run(0.12);
        pad.lift(1);
        pad.run(0.5 - 0.128);
        pad.land({2, 0.4f, kLead});
        pad.run(0.05);
        pad.lift(2);
        pad.run(0.5 - 0.058);
        pad.land({3, 0.3f, kLead});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
        std::vector<float> lengths;
        for (const auto &c : sink.commands)
            if (c.type == Cmd::LoopHit)
                lengths.push_back(c.f[3]);
        QCOMPARE(int(lengths.size()), 2);
        QVERIFY(lengths[0] > 0.1f && lengths[1] < 0.07f);
    }

    void aLongPauseMeansAVoiceNotALoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.2f, kLead);
        pad.run(0.4);
        pad.tap(2, 0.3f, kLead);
        pad.run(2.5);
        pad.land({3, 0.4f, kLead});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
        QCOMPARE(pad.held(), 1);
    }

    void aLateOrFlatHoldStillClosesADrumLoop() {
        // Kicks a beat apart, then the ONE held a quarter of a beat late
        // with a finger that flattens as it presses: still a loop.
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 4; ++i) {
            pad.tap(10 + i, 0.05f, kDrums);
            pad.run(0.484 - 0.058);
        }
        pad.run(0.11);
        pad.land({20, 0.05f, kDrums, 0.3f, 1.0f});
        for (int i = 0; i < 45; ++i) {
            pad.finger(20)->size = std::min(2.0f, pad.finger(20)->size + 0.08f);
            pad.run(0.008);
        }
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
        QCOMPARE(sink.count(Cmd::RollOn), 0);
    }

    void longKickTapsAreTapsNotRolls() {
        // Deliberate taps on the click, each left down a quarter of a second,
        // then the ONE held: four kicks and a loop, no rolls.
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 4; ++i) {
            pad.land({10 + i, 0.05f, kDrums});
            pad.run(0.26);
            pad.lift(10 + i);
            pad.run(0.5 - 0.26);
        }
        pad.land({20, 0.05f, kDrums});
        pad.run(0.4);
        QCOMPARE(sink.count(Cmd::RollOn), 0);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
    }

    void aKickAndClapTogetherStillMakeALoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 4; ++i) {
            pad.land({10 + i, 0.05f, kDrums, 0.8f});
            if (i % 2 == 1)
                pad.land({30 + i, 0.4f, kDrums, 0.8f});
            pad.run(0.05);
            pad.lift(10 + i);
            if (i % 2 == 1)
                pad.lift(30 + i);
            pad.run(0.5 - 0.058);
        }
        pad.land({20, 0.05f, kDrums});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
    }

    void aMelodyThenADroneIsADrone() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 4; ++i) {
            pad.tap(10 + i, 0.1f + 0.1f * i, kLead);
            pad.run(0.5 - 0.058);
        }
        pad.run(0.4);
        pad.land({20, 0.8f, kLead});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
        QCOMPARE(pad.held(), 1);
    }

    void tapAndHoldOnALoopPicksItUp() {
        RecordingSink sink;
        sink.nearLoop = 7;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.5f, kChords);
        pad.run(0.1);
        pad.land({2, 0.505f, kChords});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopGrab), 1);
        QCOMPARE(sink.last(Cmd::LoopGrab)->id, 7);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
    }

    void pressLatchesWhatIsHeld() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.2f, kChords});
        pad.land({2, 0.6f, kLead});
        pad.run(0.3);
        pad.button = true;
        pad.run(0.02);
        pad.button = false;
        pad.run(0.02);
        QCOMPARE(sink.count(Cmd::Freeze), 1);
        pad.release(1);
        pad.release(2);
        QCOMPARE(sink.count(Cmd::NoteOff), 0);
    }

    void frozenFingersKeepTheirRoles() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        for (int i = 0; i < 5; ++i)
            pad.land({1 + i, 0.1f + 0.15f * i, kChords});
        pad.run(0.3);
        pad.button = true;
        pad.run(0.02);
        pad.button = false;
        pad.land({6, 0.95f, kLead});
        pad.run(0.3);
        int role = -1;
        const GestureView v = g.view();
        for (int i = 0; i < v.count; ++i)
            if (v.contacts[i].id == 6)
                role = v.contacts[i].role;
        QCOMPARE(role, int(Echo));
        const float before = g.view().knobs[Filter][0];
        pad.finger(1)->y -= 0.2f;
        pad.run(0.05);
        QVERIFY(g.view().knobs[Filter][0] < before - 0.2f);
    }

    void heldFingersRecordIntoAHeldLoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 3; ++i) {
            pad.tap(10 + i, 0.05f, kDrums);
            pad.run(0.5 - 0.058);
        }
        pad.land({20, 0.05f, kDrums});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
        pad.land({21, 0.6f, kLead});
        pad.run(0.3);
        for (int i = 0; i < 40; ++i) {
            pad.finger(21)->y += 0.002f;
            pad.run(0.008);
        }
        QVERIFY(sink.count(Cmd::Motion) > 20);
        const Command *m = sink.last(Cmd::Motion);
        QCOMPARE(m->i2, int(Filter));
        QCOMPARE(m->i3, int(LeadRow));
    }

    void tapAndHoldOnALatchedChordPicksItUp() {
        class LayerSink : public RecordingSink {
        public:
            int layerNear(float, float, float) const override { return 3; }
        } sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.5f, kChords);
        pad.run(0.1);
        pad.land({2, 0.5f, kChords});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LayerGrab), 1);
        pad.finger(2)->x += 2.0f / float(g.cells());
        pad.run(0.05);
        QCOMPARE(sink.last(Cmd::LayerMod)->i1, 2);
        pad.release(2);
        QCOMPARE(sink.count(Cmd::LayerRelease), 1);
    }

    void threeFingerFlickMovesTheKey() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        const int root = g.view().root;
        pad.land({1, 0.3f, kChords});
        pad.land({2, 0.4f, kChords + 0.05f});
        pad.land({3, 0.5f, kChords});
        for (int i = 0; i < 30; ++i) {
            for (int id = 1; id <= 3; ++id)
                pad.finger(id)->x += 0.008f;
            pad.run(0.008);
        }
        for (int id = 1; id <= 3; ++id)
            pad.lift(id);
        QCOMPARE(g.view().root, (root + 7) % 12);
        QCOMPARE(sink.count(Cmd::Hush), 1);
    }

    void aChordSlidUpTogetherStaysAChord() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        pad.land({1, 0.3f, kBass});
        pad.land({2, 0.4f, kBass});
        pad.land({3, 0.5f, kBass});
        for (int i = 0; i < 60; ++i) {
            for (int id = 1; id <= 3; ++id)
                pad.finger(id)->y += 0.6f * 0.008f;
            pad.run(0.008);
        }
        // Still down past half a second: a chord, not a flick (its notes have
        // been sounding all along).
        pad.run(0.1);
        QCOMPARE(pad.held(), 3);
        QCOMPARE(g.view().octave, 0);
    }

    void stillFingerDoesNotTwist() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        pad.land({1, 0.3f, kLead, 0.6f, 1.0f, 0.4f});
        pad.run(0.3);
        const float before = g.view().knobs[Filter][1];
        Rng rng(11);
        for (int i = 0; i < 2500; ++i) {
            Frame f;
            f.t = pad.t += 0.008;
            f.count = 1;
            Touch &t = f.touches[0];
            t.id = 1;
            t.x = 0.3f;
            t.y = kLead;
            t.pressure = 0.6f;
            t.size = 1.0f;
            t.angle = 0.4f + rng.bipolar() * 0.07f;
            t.eccentricity = 0.15f + 0.2f * rng.unit();
            g.frame(f);
        }
        QVERIFY(std::fabs(g.view().knobs[Filter][1] - before) < 0.02f);
    }

    void knobsBelongToTheirRow() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.3f, kLead - 0.05f});
        pad.run(0.3);
        for (int i = 0; i < 20; ++i) {
            pad.finger(1)->y += 0.01f;
            pad.run(0.008);
        }
        const Command *k = sink.last(Cmd::Knob);
        QCOMPARE(k->i1, int(Filter));
        QCOMPARE(k->i2, int(LeadRow)); // the lead's filter, not the bass's
        pad.release(1);
        // A mix-wide knob (Echo, the sixth role) goes to every row.
        sink.clear();
        for (int i = 0; i < 6; ++i)
            pad.land({10 + i, 0.1f + 0.15f * i, kChords});
        pad.run(0.3);
        for (int i = 0; i < 20; ++i) {
            pad.finger(15)->y += 0.01f;
            pad.run(0.008);
        }
        bool echoToAll = false;
        for (const auto &c : sink.commands)
            echoToAll |= c.type == Cmd::Knob && c.i1 == Echo && c.i2 == -1;
        QVERIFY(echoToAll);
    }

    void aChordOnTheBeatDoesNotCloseABassPhrase() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 4; ++i) {
            pad.tap(10 + i, 0.2f + 0.05f * i, kBass);
            pad.run(0.5 - 0.058);
        }
        pad.land({20, 0.4f, kChords});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
        QCOMPARE(pad.held(), 1);
    }

    void aRestingFingerLeavesItsKnobAlone() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        pad.land({1, 0.3f, kLead});
        pad.run(0.3);
        const float before = g.view().knobs[Filter][0];
        // A slow drift of 3% of the pad's height, as a finger resting on a
        // note does.
        for (int i = 0; i < 30; ++i) {
            pad.finger(1)->y -= 0.001f;
            pad.run(0.03);
        }
        QCOMPARE(g.view().knobs[Filter][0], before);
    }

    void aFingerPressedFlatIsNotAPalm() {
        // A held note pressed hard grows to three times a fingertip: still a
        // note, never the damper.
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.3f, kChords, 0.5f, 1.0f});
        pad.run(0.3);
        for (int i = 0; i < 30; ++i) {
            pad.finger(1)->size = std::min(3.0f, pad.finger(1)->size + 0.1f);
            pad.finger(1)->p = 1.0f;
            pad.run(0.008);
        }
        QCOMPARE(sink.count(Cmd::Damper), 0);
        QCOMPARE(pad.held(), 1);
    }

    void palmDampsAndPalmPressClears() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.5f, kBass, 0.6f, 3.2f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::NoteOn) + sink.count(Cmd::Hit), 0);
        QVERIFY(sink.last(Cmd::Damper) && sink.last(Cmd::Damper)->f[0] > 0.4f);
        pad.button = true;
        pad.run(0.02);
        QCOMPARE(sink.count(Cmd::DissolveAll), 1);
        pad.button = false;
        pad.lift(1);
        QCOMPARE(sink.last(Cmd::Damper)->f[0], 0.0f);
    }

    void aFingerLostForAFewFramesCarriesOn() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.4f, kLead});
        pad.run(0.3);
        Finger f = *pad.finger(1);
        pad.lift(1);
        pad.run(0.024);
        f.id = 77;
        pad.land(f);
        pad.run(0.2);
        QCOMPARE(sink.count(Cmd::NoteOff), 0);
        QCOMPARE(sink.count(Cmd::NoteOn), 1);
    }

    void kickIsTunedToTheKeyAndPunches() {
        Engine e(48000);
        Command h;
        h.type = Cmd::Hit;
        h.i1 = int(Drum::Kick);
        h.f[0] = 0.9f;
        h.f[1] = 0.08f;
        e.send(h);
        const auto mono = renderMono(e, 48000);
        // After the click, the body sits low: most energy under 150 Hz,
        // centred around the A at 55 Hz.
        const double low = band(mono, 1440, 9600, 20, 150), all = band(mono, 1440, 9600, 20, 4000);
        QVERIFY(low / all > 0.85);
        // Once the sweep has landed, the strongest frequency is the A at 55 Hz.
        double best = 0, bestHz = 0;
        for (double hz = 30; hz < 200; hz += 1) {
            const double e = band(mono, 5760, 14400, hz, hz + 1);
            if (e > best) {
                best = e;
                bestHz = hz;
            }
        }
        QVERIFY(bestHz >= 45 && bestHz <= 66);
        QVERIFY(rms(mono, 0, 4800) > 0.05);
        QVERIFY(rms(mono, 33600, 38400) < rms(mono, 0, 4800) * 0.01); // −40 dB by 0.7 s
    }

    void notesSpeakAtOnce() {
        Engine e(48000);
        Command on;
        on.type = Cmd::NoteOn;
        on.id = 1;
        on.i1 = LeadRow;
        on.i2 = 7;
        on.f[0] = 57.0f;
        on.f[1] = 0.8f;
        on.f[2] = 0.5f;
        e.send(on);
        const auto mono = renderMono(e, 4800);
        const double early = rms(mono, 240, 480);     // 5–10 ms
        const double settled = rms(mono, 2400, 4800); // 50–100 ms
        QVERIFY(early > settled * 0.7);                 // within 3 dB
    }

    void chordsAreVoicedSeventhsInKey() {
        Engine e(48000);
        auto chordOn = [&](int id, int degree) {
            Command on;
            on.type = Cmd::NoteOn;
            on.id = id;
            on.i1 = ChordRow;
            on.i2 = degree;
            on.f[1] = 0.7f;
            on.f[2] = 0.5f;
            e.send(on);
            std::vector<float> st(4800 * 2);
            e.render(st.data(), 4800);
            e.fetchView();
            for (const auto &v : e.view().voices)
                if (v.active && v.finger == id)
                    return v.midi;
            return -1.0f;
        };
        const float am = chordOn(1, 7), f = chordOn(2, 12);
        QVERIFY(am > 0 && f > 0);
        // Voice leading: the next chord's bass note is close to the last one.
        QVERIFY(std::fabs(f - am) <= 7.0f);
    }

    void aLoopTappedToTheClickKeepsItsTempo() {
        // With the click on, three quick kicks closed after 0.43 s become a
        // one-beat loop at the click's 120 BPM, not a new tempo.
        Engine e(48000);
        e.setTimeOrigin(0);
        Command m;
        m.type = Cmd::Metronome;
        m.f[0] = 1;
        e.send(m);
        Command b;
        b.type = Cmd::LoopBegin;
        b.id = 1;
        b.t = 0.02;
        b.f[0] = 0.43f;
        e.send(b);
        for (float o : {0.0f, 0.15f, 0.29f}) {
            Command h;
            h.type = Cmd::LoopHit;
            h.id = 1;
            h.f[0] = o;
            h.i1 = DrumRow;
            h.i2 = int(Drum::Kick);
            h.f[1] = 0.9f;
            h.f[2] = 0.1f;
            h.f[3] = 0.05f;
            e.send(h);
        }
        Command r;
        r.type = Cmd::LoopReady;
        r.id = 1;
        e.send(r);
        std::vector<float> out(48000 * 2);
        e.render(out.data(), 48000);
        e.fetchView();
        QVERIFY(std::fabs(e.barSeconds() - 2.0f) < 1e-3f);
        for (const auto &l : e.view().loops)
            if (l.active)
                QVERIFY(std::fabs(l.lengthSec - 0.5f) < 1e-3f);
    }

    void loopsQuantiseAndRepeat() {
        Engine e(48000);
        e.setTimeOrigin(0);
        Command b;
        b.type = Cmd::LoopBegin;
        b.id = 1;
        b.t = 0.0;
        b.f[0] = 1.95f; // a little off 124 BPM
        e.send(b);
        const float offs[4] = {0.0f, 0.51f, 0.97f, 1.44f};
        for (float o : offs) {
            Command h;
            h.type = Cmd::LoopHit;
            h.id = 1;
            h.f[0] = o;
            h.i1 = DrumRow;
            h.i2 = int(Drum::Kick);
            h.f[1] = 0.9f;
            h.f[2] = 0.1f;
            h.f[3] = 0.05f;
            e.send(h);
        }
        Command r;
        r.type = Cmd::LoopReady;
        r.id = 1;
        e.send(r);
        std::vector<float> out(48000 * 2 * 8);
        e.render(out.data(), 48000 * 8);
        e.fetchView();
        // Tempo rounded to a whole BPM, hits pulled onto the beat.
        const float bpm = 240.0f / e.barSeconds();
        QVERIFY(std::fabs(bpm - std::round(bpm)) < 1e-3f);
        const LoopView *lv = nullptr;
        for (const auto &l : e.view().loops)
            if (l.active)
                lv = &l;
        QVERIFY(lv);
        QVERIFY(std::fabs(lv->hitPhase[1] - 0.25f) < 0.01f);
        QVERIFY(e.view().strikes >= 14);
    }

    void everythingUpStaysBounded() {
        Engine e(48000);
        for (int r = 0; r < RoleCount; ++r) {
            Command k;
            k.type = Cmd::Knob;
            k.i1 = r;
            k.f[0] = 1.0f;
            k.f[1] = 1.0f;
            e.send(k);
        }
        for (int v = 0; v < 10; ++v) {
            Command on;
            on.type = Cmd::NoteOn;
            on.id = v + 1;
            on.i1 = 1 + v % 4;
            on.i2 = v * 2;
            on.f[0] = 45.0f + 3.0f * v;
            on.f[1] = 1.0f;
            on.f[2] = 0.1f * v;
            e.send(on);
        }
        std::vector<float> out(48000 * 2 * 10);
        e.render(out.data(), 48000 * 10);
        for (float s : out) {
            QVERIFY(std::isfinite(s));
            QVERIFY(std::fabs(s) < 1.0f);
        }
        for (int v = 0; v < 10; ++v) {
            Command off;
            off.type = Cmd::NoteOff;
            off.id = v + 1;
            e.send(off);
        }
        for (int r = 0; r < RoleCount; ++r) {
            Command k;
            k.type = Cmd::Knob;
            k.i1 = r;
            k.f[0] = roleDefault(r, 0);
            k.f[1] = roleDefault(r, 1);
            e.send(k);
        }
        e.render(out.data(), 48000 * 10);
        QVERIFY(rms(out, out.size() - 96000, out.size()) < 0.003);
    }

    void theTutorialFollowsWhatIsPlayed() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        EngineView ev;
        Tutorial tut;
        tut.start(g.view());
        auto update = [&](bool playing) {
            const GestureView gv = g.view();
            Tutorial::State st;
            st.playing = playing;
            st.gesture = &gv;
            st.engine = &ev;
            for (int i = 0; i < 100; ++i)
                tut.update(st, 0.02);
        };
        QCOMPARE(tut.index(), 0);
        update(true); // taking the pad finishes the welcome
        QCOMPARE(tut.index(), 1);
        for (int i = 0; i < 3; ++i)
            pad.tap(10 + i, 0.05f, kDrums);
        update(true);
        QCOMPARE(tut.index(), 1); // three kicks aren't four
        pad.tap(20, 0.05f, kDrums);
        update(true);
        QCOMPARE(tut.index(), 2);
        QVERIFY(!tut.step().metronome);
        tut.next(g.view());
        QVERIFY(tut.step().metronome); // the first-loop step has a click
        for (int i = 0; i < 4; ++i) {
            pad.tap(30 + i, 0.05f, kDrums);
            pad.run(0.5 - 0.058);
        }
        pad.land({40, 0.05f, kDrums});
        pad.run(0.3);
        pad.release(40);
        update(true);
        QCOMPARE(tut.index(), 4); // the loop finished the step
    }

    void demoRendersCleanly() {
        const RenderStats s = renderScript(demoScript(), "");
        QVERIFY(s.error.empty());
        QVERIFY(s.seconds > 30);
        QVERIFY(s.peakDb < -0.9);
        QVERIFY(s.rmsDb > -18);
        QVERIFY(s.loopsMade >= 2);
    }
};

QTEST_GUILESS_MAIN(TenfoldTests)
#include "tenfold_tests.moc"
