#include <QtTest>
#include <cmath>
#include <vector>

#include "engine.h"
#include "gesture.h"
#include "offline.h"
#include "tuning.h"

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
    Finger *finger(int id) {
        for (auto &f : down)
            if (f.id == id)
                return &f;
        return nullptr;
    }
    // Lifting a held finger: its lift counts after a short grace.
    void release(int id) {
        lift(id);
        run(0.05);
    }
    void tap(int id, float x, float y) {
        land({id, x, y, 0.8f});
        run(0.05);
        lift(id);
    }
};

double rms(const std::vector<float> &v, size_t from, size_t to) {
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += double(v[i]) * v[i];
    return std::sqrt(s / double(std::max<size_t>(1, to - from)));
}

}

class TenfoldTests : public QObject {
    Q_OBJECT

private slots:
    void justPentatonic() {
        Tuning t;
        t.set(2, 1, 3, true); // D minor pentatonic, just
        QCOMPARE(t.degreesPerOctave(), 5);
        QCOMPARE(t.baseMidi(), 38.0);
        QVERIFY(std::fabs(t.degreeSemis(1) - 12 * std::log2(6.0 / 5)) < 1e-6);
        QVERIFY(std::fabs(t.degreeSemis(3) - 12 * std::log2(3.0 / 2)) < 1e-6);
        QVERIFY(std::fabs(t.degreeSemis(5) - 12.0) < 1e-9);
        QVERIFY(std::fabs(t.degreeSemis(-1) - (12 * std::log2(9.0 / 5) - 12)) < 1e-6);
        QCOMPARE(QString(t.modeName()), QString("Minor"));
        t.set(2, 1, 3, false);
        QVERIFY(std::fabs(t.degreeSemis(3) - 7.0) < 1e-9);
    }

    void modesRunBrightToDark() {
        // A brighter diatonic mode never has a lower degree than a darker one.
        Tuning a, b;
        for (int m = 0; m + 1 < 7; ++m) {
            a.set(0, 0, m, false);
            b.set(0, 0, m + 1, false);
            for (int d = 0; d < 7; ++d)
                QVERIFY(a.degreeSemis(d) >= b.degreeSemis(d));
        }
    }

    void tapStrikesWithoutAVoice() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.5f, 0.1f);
        QCOMPARE(sink.count(Cmd::Strike), 1);
        QCOMPARE(sink.count(Cmd::VoiceOn), 0);
        const Command *s = sink.last(Cmd::Strike);
        QCOMPARE(s->i1, g.strikeDegree(0.5f));
        QVERIFY(s->f[0] < 0.5f); // the bottom of the pad is skin
        QVERIFY(g.view().phraseOpen);
    }

    void holdTakesRolesInOrder() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.2f, 0.4f});
        pad.run(0.3);
        pad.land({2, 0.5f, 0.4f});
        pad.run(0.3);
        pad.land({3, 0.7f, 0.4f});
        pad.run(0.3);
        std::vector<int> roles;
        for (const auto &c : sink.commands)
            if (c.type == Cmd::VoiceOn)
                roles.push_back(c.i1);
        QCOMPARE(roles, (std::vector<int>{Tone, Shadow, Air}));
        // The second finger leaves; the next to land takes its role.
        pad.release(2);
        QCOMPARE(sink.count(Cmd::VoiceOff), 1);
        pad.land({4, 0.6f, 0.4f});
        pad.run(0.3);
        QCOMPARE(sink.last(Cmd::VoiceOn)->i1, int(Shadow));
    }

    void slideTurnsTheKnobAndItStays() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.3f, 0.3f});
        pad.run(0.3);
        const float before = g.view().knobs[Tone][0];
        for (int i = 0; i < 25; ++i) {
            pad.finger(1)->y += 0.01f;
            pad.run(0.008);
        }
        const float after = g.view().knobs[Tone][0];
        QVERIFY(std::fabs(after - (before + 0.25f * 1.6f)) < 0.01f);
        // Twisting turns the second knob.
        for (int i = 0; i < 10; ++i) {
            pad.finger(1)->angle += 0.05f;
            pad.run(0.008);
        }
        pad.run(0.3);
        QVERIFY(g.view().knobs[Tone][1] > roleDefault(Tone, 1) + 0.2f);
        pad.release(1);
        QCOMPARE(g.view().knobs[Tone][0], after);
    }

    void tapsThenHoldMakeALoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        const double start = pad.t + 0.008;
        for (int i = 0; i < 4; ++i) {
            pad.tap(10 + i, 0.1f + 0.1f * i, 0.4f);
            pad.run(0.6 - 0.058);
        }
        // Land on the next one and stay.
        pad.land({20, 0.5f, 0.4f});
        const double one = pad.t;
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
        QCOMPARE(sink.count(Cmd::LoopHit), 4);
        QCOMPARE(sink.count(Cmd::LoopReady), 1);
        QCOMPARE(sink.count(Cmd::VoiceOn), 0);
        const Command *b = sink.last(Cmd::LoopBegin);
        QVERIFY(std::fabs(b->f[0] - float(one - start)) < 0.02f);
        QCOMPARE(b->t, one);
        // While it's held, a tap with another finger is added to it.
        pad.tap(30, 0.8f, 0.8f);
        QCOMPARE(sink.count(Cmd::Overdub), 1);
        QCOMPARE(sink.last(Cmd::Overdub)->id, b->id);
        // Sliding the handle a cell to the right transposes by one degree.
        pad.finger(20)->x += 1.0f / float(g.cells());
        pad.run(0.02);
        QCOMPARE(sink.last(Cmd::LoopMod)->i1, 1);
        pad.release(20);
        QCOMPARE(sink.count(Cmd::LoopRelease), 1);
    }

    void aLongPauseMeansAVoiceNotALoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.2f, 0.3f);
        pad.run(0.4);
        pad.tap(2, 0.3f, 0.3f);
        pad.run(2.5);
        pad.land({3, 0.4f, 0.3f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
        QCOMPARE(sink.count(Cmd::VoiceOn), 1);
    }

    void tapAndHoldOnALoopPicksItUp() {
        RecordingSink sink;
        sink.nearLoop = 7;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.5f, 0.5f);
        pad.run(0.1);
        pad.land({2, 0.505f, 0.5f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopGrab), 1);
        QCOMPARE(sink.last(Cmd::LoopGrab)->id, 7);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
    }

    void pressFreezesWhatIsHeld() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.2f, 0.4f});
        pad.land({2, 0.6f, 0.4f});
        pad.run(0.3);
        pad.button = true;
        pad.run(0.02);
        pad.button = false;
        pad.run(0.02);
        QCOMPARE(sink.count(Cmd::Freeze), 1);
        // The fingers are free now: lifting them doesn't stop the voices.
        pad.lift(1);
        pad.lift(2);
        QCOMPARE(sink.count(Cmd::VoiceOff), 0);
    }

    void palmDampsAndPalmPressClears() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.5f, 0.1f, 0.6f, 3.2f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::Strike), 0);
        QVERIFY(sink.last(Cmd::Damper) && sink.last(Cmd::Damper)->f[0] > 0.4f);
        pad.button = true;
        pad.run(0.02);
        QCOMPARE(sink.count(Cmd::DissolveAll), 1);
        pad.button = false;
        pad.lift(1);
        QCOMPARE(sink.last(Cmd::Damper)->f[0], 0.0f);
    }

    void stillFingerDoesNotTwist() {
        // A finger held still while the pad reports a noisy angle (4°) must
        // leave its twist knob alone.
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        pad.land({1, 0.3f, 0.3f});
        pad.run(0.3);
        const float before = g.view().knobs[Tone][1];
        Rng rng(7);
        for (int i = 0; i < 2500; ++i) { // 20 s
            pad.finger(1)->angle = rng.bipolar() * 0.07f;
            pad.run(0.008);
        }
        QVERIFY(std::fabs(g.view().knobs[Tone][1] - before) < 0.02f);
    }

    void aTapThenAHoldElsewhereIsAVoice() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.2f, 0.3f);
        pad.run(0.8);
        pad.land({2, 0.7f, 0.5f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 0);
        QCOMPARE(sink.count(Cmd::VoiceOn), 1);
    }

    void frozenFingersKeepTheirRoles() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 5; ++i)
            pad.land({1 + i, 0.1f + 0.15f * i, 0.4f});
        pad.run(0.3);
        pad.button = true;
        pad.run(0.02);
        pad.button = false;
        // The five stay down; a sixth finger reaches Echo straight away.
        pad.land({6, 0.95f, 0.4f});
        pad.run(0.3);
        QCOMPARE(sink.last(Cmd::VoiceOn)->i1, int(Echo));
        // And a frozen finger still turns its knob (Tone's brightness).
        const float before = g.view().knobs[Tone][0];
        pad.finger(1)->y += 0.2f;
        pad.run(0.05);
        QVERIFY(g.view().knobs[Tone][0] > before + 0.25f);
    }

    void heldFingersRecordIntoAHeldLoop() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        for (int i = 0; i < 3; ++i) {
            pad.tap(10 + i, 0.2f, 0.1f);
            pad.run(0.5 - 0.058);
        }
        pad.land({20, 0.2f, 0.1f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LoopBegin), 1);
        pad.land({21, 0.6f, 0.5f});
        pad.run(0.3);
        for (int i = 0; i < 40; ++i) {
            pad.finger(21)->y += 0.005f;
            pad.run(0.008);
        }
        QVERIFY(sink.count(Cmd::Motion) > 20);
        const Command *m = sink.last(Cmd::Motion);
        QCOMPARE(m->i1, 0);
        QCOMPARE(m->i2, int(Tone));
    }

    void tapAndHoldOnAFrozenChordPicksItUp() {
        class LayerSink : public RecordingSink {
        public:
            int layerNear(float, float, float) const override { return 3; }
        } sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.tap(1, 0.5f, 0.5f);
        pad.run(0.1);
        pad.land({2, 0.5f, 0.5f});
        pad.run(0.3);
        QCOMPARE(sink.count(Cmd::LayerGrab), 1);
        pad.finger(2)->x += 2.0f / float(g.cells());
        pad.run(0.05);
        QCOMPARE(sink.last(Cmd::LayerMod)->i1, 2);
        pad.release(2);
        QCOMPARE(sink.count(Cmd::LayerRelease), 1);
    }

    void threeFingerSwipeMovesTheKey() {
        RecordingSink sink;
        Gesture g(&sink);
        Pad pad(g);
        const int root = g.view().root;
        pad.land({1, 0.3f, 0.4f});
        pad.land({2, 0.4f, 0.45f});
        pad.land({3, 0.5f, 0.4f});
        for (int i = 0; i < 30; ++i) {
            for (int id = 1; id <= 3; ++id)
                pad.finger(id)->x += 0.008f;
            pad.run(0.008);
        }
        QCOMPARE(g.view().root, (root + 7) % 12);
        QCOMPARE(sink.count(Cmd::VoiceOn), 0);
        // Vertical is the octave.
        for (int id = 1; id <= 3; ++id)
            pad.lift(id);
        pad.run(0.5);
        pad.land({4, 0.3f, 0.3f});
        pad.land({5, 0.4f, 0.3f});
        pad.land({6, 0.5f, 0.3f});
        for (int i = 0; i < 30; ++i) {
            for (int id = 4; id <= 6; ++id)
                pad.finger(id)->y += 0.01f;
            pad.run(0.008);
        }
        QCOMPARE(g.view().octave, 1);
    }

    void aPalmThatGrowsSlowlyIsStillAPalm() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.5f, 0.2f, 0.4f, 1.0f});
        for (int i = 0; i < 60; ++i) {
            pad.finger(1)->size += 0.04f;
            pad.run(0.008);
        }
        QVERIFY(sink.last(Cmd::Damper) && sink.last(Cmd::Damper)->f[0] > 0.3f);
        // If it had bloomed into a voice first, the voice is let go of.
        QCOMPARE(sink.count(Cmd::VoiceOn), sink.count(Cmd::VoiceOff));
    }

    void aDroppedFrameDoesNotCutAVoice() {
        RecordingSink sink;
        Gesture g(&sink);
        sink.clear();
        Pad pad(g);
        pad.land({1, 0.4f, 0.4f});
        pad.run(0.3);
        Finger f = *pad.finger(1);
        pad.lift(1);   // one frame without it
        f.id = 99;     // and back with a new tracking id, as the pad does
        pad.land(f);
        pad.run(0.2);
        QCOMPARE(sink.count(Cmd::VoiceOff), 0);
        QCOMPARE(sink.count(Cmd::VoiceOn), 1);
        QCOMPARE(sink.count(Cmd::Strike), 1);
    }

    void noWidthMeansMono() {
        Engine e(48000);
        Command k;
        k.type = Cmd::Knob;
        k.i1 = Shadow;
        k.f[0] = 0.0f;
        k.f[1] = 0.25f;
        e.send(k);
        Command sp;
        sp.type = Cmd::Knob;
        sp.i1 = Space;
        sp.f[0] = 0.0f;
        e.send(sp);
        Command on;
        on.type = Cmd::VoiceOn;
        on.id = 1;
        on.f[0] = 45.0f;
        on.f[1] = 0.7f;
        on.f[2] = 1.0f;
        on.f[3] = 0.5f;
        e.send(on);
        std::vector<float> out(48000 * 2 * 3);
        e.render(out.data(), 48000 * 3);
        // Direct sound: compare left and right over the last second (the
        // small room adds some width of its own).
        double num = 0, dl = 0, dr = 0;
        for (size_t i = out.size() - 96000; i < out.size(); i += 2) {
            num += double(out[i]) * out[i + 1];
            dl += double(out[i]) * out[i];
            dr += double(out[i + 1]) * out[i + 1];
        }
        QVERIFY(num / std::sqrt(dl * dr) > 0.8);
    }

    void frozenLayersKeepTheirTimbre() {
        // Freeze a bright chord, then turn brightness all the way down: the
        // frozen chord stays bright.
        Engine e(48000);
        auto knob = [&](int role, float a, float b) {
            Command k;
            k.type = Cmd::Knob;
            k.i1 = role;
            k.f[0] = a;
            k.f[1] = b;
            e.send(k);
        };
        knob(Tone, 1.0f, 0.5f);
        knob(Space, 0.0f, 0.0f);
        Command on;
        on.type = Cmd::VoiceOn;
        on.id = 1;
        on.f[0] = 50.0f;
        on.f[1] = 0.7f;
        on.f[2] = 1.0f;
        on.f[3] = 0.5f;
        on.f[5] = 2.0f;
        e.send(on);
        std::vector<float> out(48000 * 2);
        e.render(out.data(), 48000);
        Command f;
        f.type = Cmd::Freeze;
        f.id = 1;
        e.send(f);
        knob(Tone, 0.0f, 0.5f);
        e.render(out.data(), 48000);
        // Count zero crossings of the slope as a rough brightness measure.
        auto edges = [&]() {
            int n = 0;
            for (size_t i = 4; i < out.size(); i += 2)
                n += ((out[i] - out[i - 2]) > 0) != ((out[i - 2] - out[i - 4]) > 0);
            return n;
        };
        const int frozen = edges();
        Command on2 = on;
        on2.id = 2;
        e.send(on2);
        Command dis;
        dis.type = Cmd::DissolveAll;
        e.send(dis);
        for (int i = 0; i < 5; ++i)
            e.render(out.data(), 48000);
        const int dark = edges();
        QVERIFY(frozen > dark * 2);
    }

    void strikeRingsAndDiesAway() {
        Engine e(48000);
        Command c;
        c.type = Cmd::Strike;
        c.i1 = 7;
        c.f[0] = 1.0f;
        c.f[1] = 0.9f;
        c.f[2] = 0.5f;
        e.send(c);
        std::vector<float> out(48000 * 2 * 14);
        e.render(out.data(), 48000 * 14);
        QVERIFY(rms(out, 0, 48000) > 1e-3);
        QVERIFY(rms(out, out.size() - 9600, out.size()) < 1e-4);
        for (float v : out)
            QVERIFY(std::isfinite(v));
    }

    void loopRepeatsInTime() {
        Engine e(48000);
        e.setTimeOrigin(0);
        Command b;
        b.type = Cmd::LoopBegin;
        b.id = 1;
        b.t = 0.0;
        b.f[0] = 2.0f;
        e.send(b);
        for (int i = 0; i < 4; ++i) {
            Command h;
            h.type = Cmd::LoopHit;
            h.id = 1;
            h.f[0] = 0.5f * float(i);
            h.i1 = i;
            h.f[1] = 1.0f;
            h.f[2] = 0.7f;
            e.send(h);
        }
        Command r;
        r.type = Cmd::LoopReady;
        r.id = 1;
        e.send(r);
        std::vector<float> out(48000 * 2 * 10);
        e.render(out.data(), 48000 * 10);
        e.fetchView();
        // Ten seconds of a two-second bar with four taps; the one at the very
        // start was the finger itself and isn't repeated until the next cycle.
        QVERIFY(std::abs(e.view().strikes - 19) <= 1);
        QVERIFY(std::fabs(e.barSeconds() - 2.0f) < 1e-3f);
        QCOMPARE(e.activeLoops(), 1);
    }

    void laterLoopsSnapToTheBar() {
        Engine e(48000);
        e.setTimeOrigin(0);
        auto loop = [&](int id, double t, float len) {
            Command b;
            b.type = Cmd::LoopBegin;
            b.id = id;
            b.t = t;
            b.f[0] = len;
            e.send(b);
            Command h;
            h.type = Cmd::LoopHit;
            h.id = id;
            e.send(h);
            Command r;
            r.type = Cmd::LoopReady;
            r.id = id;
            e.send(r);
        };
        loop(1, 0.0, 2.0f);
        std::vector<float> out(4800 * 2);
        e.render(out.data(), 4800);
        loop(2, 0.1, 1.1f); // a sloppy half bar
        e.render(out.data(), 4800);
        e.fetchView();
        float lengths[2] = {};
        int n = 0;
        for (const auto &l : e.view().loops)
            if (l.active)
                lengths[n++] = l.lengthSec;
        QCOMPARE(n, 2);
        QVERIFY(std::fabs(std::min(lengths[0], lengths[1]) - 1.0f) < 1e-3f);
    }

    void everythingUpStaysBoundedAndCalmsDown() {
        // All ten knobs at the top, ten held voices, then everything let go:
        // nothing may clip past the ceiling, go non-finite, or feed on itself.
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
            on.type = Cmd::VoiceOn;
            on.id = v;
            on.i1 = v;
            on.f[0] = 38.0f + 5.0f * v;
            on.f[1] = 1.0f;
            on.f[2] = 1.0f;
            on.f[3] = 0.1f * v;
            e.send(on);
        }
        std::vector<float> out(48000 * 2 * 20);
        e.render(out.data(), 48000 * 20);
        for (float s : out) {
            QVERIFY(std::isfinite(s));
            QVERIFY(std::fabs(s) < 1.0f);
        }
        for (int v = 0; v < 10; ++v) {
            Command off;
            off.type = Cmd::VoiceOff;
            off.id = v;
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
        e.render(out.data(), 48000 * 20);
        QVERIFY(rms(out, out.size() - 96000, out.size()) < 0.003);
    }

    void demoRendersCleanly() {
        const RenderStats s = renderScript(demoScript(), "");
        QVERIFY(s.error.empty());
        QVERIFY(s.seconds > 60);
        QVERIFY(s.peakDb < -0.9);
        QVERIFY(s.rmsDb > -24);
        QCOMPARE(s.loopsMade, 3);
        QCOMPARE(s.freezes, 2);
    }
};

QTEST_GUILESS_MAIN(TenfoldTests)
#include "tenfold_tests.moc"
