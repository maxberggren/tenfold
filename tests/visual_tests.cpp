// The visualizer: each part's playing read as its own signal, the hand's
// gestures turned into the world's shape, the uniforms the shaders read,
// the page in the session and in song files, and the video recorder's
// command lines and its sync with the sound.

#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cmath>
#include <cstring>
#include <regex>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

#include <rhi/qshader.h>

#include "recorder.h"
#include "testkit.h"
#include "visual.h"
#include "wav.h"

using namespace tf;
using namespace tf::visual;

namespace {

QString source(const QString &path) { return QStringLiteral(TENFOLD_SRC "/") + path; }

std::string readFile(const QString &path) {
    std::ifstream f(path.toStdString());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Plays a script, calling `each` with the session and the script's time
// after every tick (125 a second), the visual tracker stepped first.
void watch(const std::string &script, const std::function<void(Session &, const Signals &, double)> &each) {
    ScriptPlayer player;
    std::string error;
    QVERIFY2(player.load(script, &error), error.c_str());
    Studio studio(48000.0f);
    studio.setTimeOrigin(0);
    setScriptClock(0);
    Session session(&studio);
    session.setClock(&scriptClock);
    SignalTracker tracker;
    float buf[384 * 2];
    const double end = player.end() + 0.01;
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > end)
            break;
        session.refresh();
        player.tick(t, session);
        studio.render(buf, 384);
        session.refresh();
        tracker.update(session.view(), 1.0 / 125.0);
        each(session, tracker.now(), t);
    }
}

Frame frameOf(double t, std::initializer_list<std::array<float, 3>> fingers) {
    Frame f;
    f.t = t;
    int id = 1;
    for (const auto &xy : fingers) {
        Touch &touch = f.touches[size_t(f.count++)];
        touch.id = id++;
        touch.x = xy[0];
        touch.y = xy[1];
        touch.pressure = xy[2];
        touch.size = 1;
    }
    return f;
}

// n fingers on a circle round (cx, cy) of radius r (in pad heights), turned by a.
Frame ring(double t, int n, float cx, float cy, float r, float a, float aspect = 1.6f) {
    Frame f;
    f.t = t;
    for (int i = 0; i < n; ++i) {
        Touch &touch = f.touches[size_t(f.count++)];
        touch.id = i + 1;
        const float ang = a + 6.2831853f * float(i) / float(n);
        touch.x = cx + r * std::cos(ang) / aspect;
        touch.y = cy + r * std::sin(ang);
        touch.pressure = 0.5f;
        touch.size = 1;
    }
    return f;
}

} // namespace

class VisualTests : public QObject {
    Q_OBJECT

private slots:
    // --- the music, read part by part ------------------------------------------------

    // The short track's loops, made one page at a time: while each page is
    // played, its own signal moves and the others' marks stay still.
    void everyPartHasItsOwnSignal() {
        std::string song = readFile(source(QStringLiteral("../docs/parts/arrange/short-track.tf")));
        song = std::regex_replace(song, std::regex("\nend [0-9.]+"), "\n");
        song += "end 47\n";
        int kicks0 = -1, kicks1 = 0, snares0 = -1, snares1 = 0, hats0 = -1, hats1 = 0;
        float earlyOthers = 0, bassMax = 0, atmosBefore = 0, atmosDuring = 0, kickPeak = 0;
        std::set<int> bassPitches;
        int chordsAt15 = -1, chordsAt25 = 0;
        std::vector<NoteEvent> leads, arps;
        watch(song, [&](Session &, const Signals &s, double t) {
            if (t >= 0.4 && t < 4.4) {
                if (kicks0 < 0)
                    kicks0 = s.kicks, snares0 = s.snares, hats0 = s.hats;
                kicks1 = s.kicks, snares1 = s.snares, hats1 = s.hats;
                kickPeak = std::max(kickPeak, s.kick);
                earlyOthers = std::max({earlyOthers, s.lead, s.arp, s.atmos, s.vocal, s.build, s.bass});
            }
            if (t >= 6 && t < 14.5) {
                bassMax = std::max(bassMax, s.bass);
                if (s.bassEnv > 0.1f)
                    bassPitches.insert(int(std::lround(s.bassPitch * 36)));
            }
            if (t >= 15 && chordsAt15 < 0)
                chordsAt15 = s.chordChanges;
            if (t < 25)
                chordsAt25 = s.chordChanges;
            if (t > 20 && t < 25)
                atmosBefore = std::max(atmosBefore, s.atmos);
            if (t > 27 && t < 34.5)
                atmosDuring = std::max(atmosDuring, s.atmos);
            if (t > 35.5 && t < 40.6 && s.notes[0].kind == 1 && (leads.empty() || leads.back().t != s.notes[0].t))
                leads.push_back(s.notes[0]);
            if (t > 42.4 && t < 46.6 && s.notes[0].kind == 0 && (arps.empty() || arps.back().t != s.notes[0].t))
                arps.push_back(s.notes[0]);
        });
        // Drums: two bars of kicks on the beat, claps on two and four, hats off the beat.
        QVERIFY2(kicks1 - kicks0 >= 6, qPrintable(QString::number(kicks1 - kicks0)));
        QVERIFY2(snares1 - snares0 >= 3, qPrintable(QString::number(snares1 - snares0)));
        QVERIFY2(hats1 - hats0 >= 6, qPrintable(QString::number(hats1 - hats0)));
        QVERIFY(kickPeak > 0.5f);
        // Nothing else playing yet: nothing else moves.
        QVERIFY2(earlyOthers < 0.05f, qPrintable(QString::number(earlyOthers)));
        // Bass: loud, and its line moves.
        QVERIFY(bassMax > 0.3f);
        QVERIFY2(bassPitches.size() >= 3, qPrintable(QString::number(bassPitches.size())));
        // Chords: a progression is a run of changes.
        QVERIFY2(chordsAt25 - chordsAt15 >= 3, qPrintable(QString::number(chordsAt25 - chordsAt15)));
        // Atmosphere: there once its page plays.
        QVERIFY(atmosBefore < 0.05f);
        QVERIFY(atmosDuring > 0.3f);
        // Lead: a note per key, at the keys' pitches.
        QVERIFY2(leads.size() >= 8, qPrintable(QString::number(leads.size())));
        std::set<int> leadPitches;
        for (const NoteEvent &n : leads)
            leadPitches.insert(int(std::lround(n.pitch * 28)));
        QVERIFY(leadPitches.size() >= 3);
        // Arp: a burst per note.
        QVERIFY2(arps.size() >= 8, qPrintable(QString::number(arps.size())));
    }

    // A part that can't be heard draws nothing: here the drums, muted on the
    // Song page (Q) before they are played.
    void aMutedPartIsNotDrawn() {
        int kicks = 0;
        float kick = 0;
        watch("page 0 0\nkey 0.2 Q\npage 0.3 1\ntap 0.5 0.12 0.12 p=0.75\ntap 1.0 0.12 0.12 p=0.75\nend 1.5\n",
              [&](Session &, const Signals &s, double) {
                  kicks = s.kicks;
                  kick = std::max(kick, s.kick);
              });
        QCOMPARE(kicks, 0);
        QCOMPARE(kick, 0.0f);
    }

    // The voice: its level, and its pitch follows the sung note.
    void theVoiceIsHeardAndPitched() {
        float low = -1, high = -1, level = 0;
        watch("page 0 8\nvoice 0.5 1.2 hz=196\nvoice 2.5 1.2 hz=440\nend 4\n", [&](Session &, const Signals &s, double t) {
            if (t > 1.3 && t < 1.6)
                low = s.vocalPitch;
            if (t > 3.3 && t < 3.6)
                high = s.vocalPitch;
            level = std::max(level, s.vocal);
        });
        QVERIFY2(level > 0.3f, qPrintable(QString::number(level)));
        QVERIFY2(high > low + 0.1f, qPrintable(QStringLiteral("%1 %2").arg(low).arg(high)));
    }

    // A riser builds, then the drop flashes and is counted.
    void aBuildAndItsDrop() {
        const double spb = 60.0 / 124.0;
        float build = 0;
        int drops = 0;
        float drop = 0;
        watch(QStringLiteral("page 0 7\nkey 0.5 S\nkey %1 Return\nend %2\n").arg(5.5 * spb).arg(8.0 * spb + 0.5).toStdString(),
              [&](Session &, const Signals &s, double t) {
                  if (t < 8.0 * spb)
                      build = std::max(build, s.build);
                  drops = s.drops;
                  drop = std::max(drop, s.drop);
              });
        QVERIFY2(build > 0.2f, qPrintable(QString::number(build)));
        QCOMPARE(drops, 1);
        QVERIFY(drop > 0.5f);
    }

    void chordsAreColouredRoundTheCircleOfFifths() {
        QCOMPARE(fifthsHue(0), 0.0f);           // C
        QCOMPARE(fifthsHue(7), 1.0f / 12.0f);   // G, a fifth up: the next colour
        QCOMPARE(fifthsHue(2), 2.0f / 12.0f);   // D
        QCOMPARE(fifthsHue(5), 11.0f / 12.0f);  // F, a fifth down: the colour before C's
        QCOMPARE(fifthsHue(-5), fifthsHue(7));
    }

    // --- the hand --------------------------------------------------------------------

    void twoFingersTwistToRollAndSpreadToZoom() {
        HandMorph h;
        for (int i = 0; i <= 10; ++i)
            h.frame(ring(i * 0.01, 2, 0.5f, 0.5f, 0.2f, float(i) * 0.157f), 1.6f); // a quarter turn
        QVERIFY2(std::fabs(h.look.twist - 1.5708f) < 0.05f, qPrintable(QString::number(h.look.twist)));
        QCOMPARE(h.look.zoom, 0.0f);
        HandMorph z;
        for (int i = 0; i <= 10; ++i)
            z.frame(ring(i * 0.01, 2, 0.5f, 0.5f, 0.1f + 0.005f * float(i), 0), 1.6f); // spread half as wide again
        QVERIFY2(std::fabs(z.look.zoom - 1.5f * std::log(1.5f)) < 0.02f, qPrintable(QString::number(z.look.zoom)));
        QVERIFY(std::fabs(z.look.twist) < 1e-4f);
    }

    // Three fingers travel through the worlds; lifted, the world stays, and
    // landing again elsewhere doesn't jump it.
    void threeFingersTravelAndTheWorldStays() {
        HandMorph h;
        for (int i = 0; i <= 10; ++i)
            h.frame(ring(i * 0.01, 3, 0.3f + 0.02f * float(i), 0.5f, 0.1f, 0), 1.6f);
        const float travelled = h.look.world;
        QVERIFY2(std::fabs(travelled - 0.2f * 1.6f * 3.0f) < 0.01f, qPrintable(QString::number(travelled)));
        h.frame(Frame{0.2, false, 0, {}}, 1.6f);
        QCOMPARE(h.look.world, travelled);
        h.frame(ring(0.3, 3, 0.8f, 0.2f, 0.1f, 0), 1.6f);
        QCOMPARE(h.look.world, travelled);
    }

    void fiveFingersDialTheSeed() {
        HandMorph h;
        for (int i = 0; i <= 10; ++i)
            h.frame(ring(i * 0.01, 5, 0.5f, 0.5f, 0.2f, -0.05f * float(i)), 1.6f);
        QVERIFY2(std::fabs(h.look.seed - (-0.5f * 2.0f)) < 0.02f, qPrintable(QString::number(h.look.seed)));
        QCOMPARE(h.look.world, 0.0f);
    }

    // Two hands: the left steers, the right travels.
    void twoHandsSplitTheWork() {
        HandMorph h;
        for (int i = 0; i <= 10; ++i) {
            const float up = 0.01f * float(i), right = 0.01f * float(i);
            h.frame(frameOf(i * 0.01, {{0.1f, 0.3f + up, 0.5f}, {0.15f, 0.4f + up, 0.5f}, {0.2f, 0.35f + up, 0.5f},
                                       {0.7f + right, 0.3f, 0.5f}, {0.75f + right, 0.4f, 0.5f}, {0.8f + right, 0.35f, 0.5f}}),
                    1.6f);
        }
        QVERIFY2(std::fabs(h.look.pitch - 0.2f) < 0.01f, qPrintable(QString::number(h.look.pitch)));
        QVERIFY2(std::fabs(h.look.world - 0.1f * 1.6f * 3.0f) < 0.01f, qPrintable(QString::number(h.look.world)));
        QVERIFY(std::fabs(h.look.yaw) < 0.01f);
    }

    // Ten fingers: every bend at once, eased in, and out when they lift.
    void tenFingersAreTheExtreme() {
        HandMorph h;
        h.frame(ring(0, 10, 0.5f, 0.5f, 0.25f, 0), 1.6f);
        for (int i = 0; i < 10; ++i)
            h.tick(0.1, false);
        QVERIFY(h.hand().extreme > 0.9f);
        const auto u = uniforms(Signals(), Settings(), h.hand(), Drive(), 640, 360, 0);
        for (const Uniform &x : u)
            if (std::strcmp(x.name, "morph2") == 0)
                QVERIFY(x.v[1] > 0.75f); // the inversion
        h.frame(Frame{1.1, false, 0, {}}, 1.6f);
        for (int i = 0; i < 20; ++i)
            h.tick(0.1, false);
        QVERIFY(h.hand().extreme < 0.01f);
    }

    // Every finger keeps its slot while down and fades from it after a lift.
    void liftedFingersFade() {
        HandMorph h;
        h.frame(frameOf(0, {{0.2f, 0.2f, 0.8f}, {0.6f, 0.7f, 0.3f}}), 1.6f);
        QCOMPARE(h.hand().count, 2);
        QCOMPARE(h.hand().fingers[0].alive, 1.0f);
        QCOMPARE(h.hand().fingers[0].pressure, 0.8f);
        h.frame(frameOf(0.1, {{0.2f, 0.2f, 0.8f}}), 1.6f);
        h.tick(0.3, false);
        QCOMPARE(h.hand().fingers[0].alive, 1.0f);
        QVERIFY(h.hand().fingers[1].alive > 0.2f && h.hand().fingers[1].alive < 0.8f);
        h.tick(0.25, false);
        h.tick(0.25, false);
        QCOMPARE(h.hand().fingers[1].alive, 0.0f);
    }

    // Left alone with the autopilot on, the world wanders, but only after a
    // while; with it off it stays.
    void theAutopilotWandersOnlyWhenLeftAlone() {
        HandMorph on, off;
        on.frame(ring(0, 2, 0.5f, 0.5f, 0.1f, 0), 1.6f);
        on.frame(Frame{0.1, false, 0, {}}, 1.6f);
        off = on;
        for (int i = 0; i < 30; ++i)
            on.tick(0.1, true);
        QCOMPARE(on.look.world, 0.0f); // three seconds: not yet
        for (int i = 0; i < 100; ++i) {
            on.tick(0.1, true);
            off.tick(0.1, false);
        }
        QVERIFY(on.look.world > 0.01f && on.look.seed > 0.01f);
        QCOMPARE(off.look, Look());
    }

    void aLookSurvivesJsonAndBadValues() {
        Look l;
        l.world = 3.25f;
        l.fold = 0.8f;
        l.symmetry = 0.6f;
        l.twist = -2.5f;
        l.seed = 17.5f;
        l.hue = 0.4f;
        const Look back = fromJson(toJson(l), Look());
        QCOMPARE(back, l);
        QJsonObject bad = toJson(l);
        bad[QStringLiteral("fold")] = 7;              // clamped
        bad[QStringLiteral("world")] = -1.0;          // wrapped
        bad[QStringLiteral("speed")] = QStringLiteral("fast"); // not a number: kept
        const Look tidied = fromJson(bad, Look());
        QCOMPARE(tidied.fold, 1.0f);
        QCOMPARE(tidied.world, 4.0f);
        QCOMPARE(tidied.speed, Look().speed);
    }

    // --- what the shaders get --------------------------------------------------------

    // Every uniform a shader declares is one the app gives it (or one the
    // picture's QML makes from them: res and roll get the item's size and
    // glass), and the QML hands each shader just those.
    void everyShaderUniformIsFed() {
        std::set<std::string> given;
        for (const Uniform &u : uniforms(Signals(), Settings(), Hand(), Drive(), 640, 360, 1))
            given.insert(u.name);
        const QString qml = QString::fromStdString(readFile(source(QStringLiteral("Visualizer.qml"))));
        for (const char *name : {"scene", "feedback", "blur", "post"}) {
            const QString frag = QString::fromStdString(readFile(source(QStringLiteral("visual/%1.frag").arg(QLatin1String(name)))));
            QVERIFY(!frag.isEmpty());
            const auto block = QRegularExpression(QStringLiteral("uniform buf \\{([^}]*)\\}")).match(frag);
            QVERIFY(block.hasMatch());
            for (auto it = QRegularExpression(QStringLiteral("vec4 (\\w+);")).globalMatch(block.captured(1)); it.hasNext();) {
                const QString u = it.next().captured(1);
                if (QStringLiteral("dir debug").split(' ').contains(u))
                    continue; // the picture's own, not the music's
                QVERIFY2(given.count(u.toStdString()), qPrintable(QStringLiteral("%1.frag: %2").arg(QLatin1String(name), u)));
                QVERIFY2(qml.contains(QStringLiteral("property vector4d %1:").arg(u)), qPrintable(u));
            }
        }
    }

    // The baked shaders are the sources' (bin/shaders after an edit): each
    // .qsb holds OpenGL's GLSL, and its uniform block has the members the
    // source declares, in order.
    void theBakedShadersMatchTheirSources() {
        for (const char *name : {"scene", "feedback", "blur", "post"}) {
            QFile f(source(QStringLiteral("visual/%1.frag.qsb").arg(QLatin1String(name))));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QShader shader = QShader::fromSerialized(f.readAll());
            QVERIFY(shader.isValid());
            QVERIFY(!shader.shader(QShaderKey(QShader::GlslShader, QShaderVersion(120))).shader().isEmpty() ||
                    !shader.shader(QShaderKey(QShader::GlslShader, QShaderVersion(150))).shader().isEmpty());
            QStringList baked;
            for (const auto &m : shader.description().uniformBlocks().value(0).members)
                baked << QString::fromUtf8(m.name);
            const QString frag = QString::fromStdString(readFile(source(QStringLiteral("visual/%1.frag").arg(QLatin1String(name)))));
            QStringList declared;
            const auto block = QRegularExpression(QStringLiteral("uniform buf \\{([^}]*)\\}")).match(frag);
            for (auto it = QRegularExpression(QStringLiteral("(?:mat4|float|vec4) (\\w+);")).globalMatch(block.captured(1)); it.hasNext();)
                declared << it.next().captured(1);
            QVERIFY2(baked == declared, qPrintable(QStringLiteral("%1: run bin/shaders").arg(QLatin1String(name))));
        }
    }

    void signalsReachTheirUniforms() {
        Signals s;
        s.kick = 0.9f;
        s.snare = 0.4f;
        s.chordHue = 0.25f;
        s.leadPitch = 0.7f;
        s.vocal = 0.6f;
        s.level[Atmos] = 0.5f;
        Settings set;
        set.quality = 3;
        Hand h;
        h.count = 1;
        h.fingers[3] = {0.25f, 0.75f, 0.6f, 1.0f, 7};
        Drive d;
        d.travel = 12.5;
        auto find = [](const std::vector<Uniform> &u, const char *name) {
            for (const Uniform &x : u)
                if (std::strcmp(x.name, name) == 0)
                    return x;
            return Uniform{"", {-1, -1, -1, -1}};
        };
        const auto u = uniforms(s, set, h, d, 640, 360, 0.5f);
        QCOMPARE(find(u, "drums").v[0], 0.9f);
        QCOMPARE(find(u, "drums").v[1], 0.4f);
        QCOMPARE(find(u, "chord").v[0], 0.25f);
        QCOMPARE(find(u, "lead").v[1], 0.7f);
        QCOMPARE(find(u, "vocal").v[0], 0.6f);
        QCOMPARE(find(u, "levels1").v[1], 0.5f);
        QCOMPARE(find(u, "morph2").v[0], 12.5f);
        QCOMPARE(find(u, "morph3").v[0], float(marchSteps(3)));
        QCOMPARE(find(u, "f3").v[0], 0.25f);
        QCOMPARE(find(u, "f3").v[3], 1.0f);
        QCOMPARE(find(u, "roll").v[3], 0.5f);
    }

    // A drop throws the world on to the next, with the autopilot on; a
    // chord change turns the crystals a little; the camera flies faster
    // with the tempo.
    void theMusicMovesTheWorldOnItsOwn() {
        Drive d, held;
        Signals s;
        Look l;
        QVERIFY(!d.step(s, l, 0.1));
        held.step(s, l, 0.1, false);
        s.drops = 1;
        QVERIFY(d.step(s, l, 0.05));
        QVERIFY(!d.step(s, l, 0.05));
        QVERIFY(!held.step(s, l, 0.05, false));
        VisualPage page;
        page.moveOn();
        QCOMPARE(page.state().settings.look.world, 1.0f);
        QVERIFY(std::fabs(page.state().settings.look.hue - 0.33f) < 1e-5f);
        s.chordChanges = 1;
        for (int i = 0; i < 60; ++i)
            d.step(s, l, 0.05);
        QVERIFY(std::fabs(d.seedShift - 0.35f) < 0.01f);
        Drive slow, fast;
        Signals s90, s180;
        s90.bpm = 90;
        s180.bpm = 180;
        slow.step(s90, l, 1);
        fast.step(s180, l, 1);
        QVERIFY(fast.travel > slow.travel * 1.8);
        QVERIFY(marchSteps(0) < marchSteps(1) && marchSteps(1) < marchSteps(3));
        QVERIFY(sceneScale(0) < sceneScale(3));
    }

    // --- the page --------------------------------------------------------------------

    void nineShowsTheVisualAndZeroStillTheSong() {
        testkit::play("key 0.1 9\nend 0.2\n", [](Session &s) {
            QCOMPARE(s.page(), s.visualPage());
            QCOMPARE(s.pageName(s.page()), QStringLiteral("Visual"));
        });
        testkit::play("key 0.1 9\nkey 0.15 0\nend 0.2\n", [](Session &s) { QCOMPARE(s.page(), s.arrangePage()); });
        testkit::play("page 0.1 9\nend 0.2\n", [](Session &s) { QCOMPARE(s.page(), s.visualPage()); });
    }

    // The page's keys and fingers, and the look kept in the song file.
    void theLookIsSavedWithTheSong() {
        QJsonObject doc;
        Look before;
        testkit::play("page 0 9\nkey 0.1 D\nkey 0.2 K\nkey 0.3 K\nkey 0.4 P\n"
                      "down 0.5 1 0.4 0.5\ndown 0.5 2 0.5 0.5\ndown 0.5 3 0.6 0.5\nglide 0.6 1.0 1 0.5 0.6\n"
                      "glide 0.6 1.0 2 0.6 0.6\nglide 0.6 1.0 3 0.7 0.6\nup 1.1 1\nup 1.1 2\nup 1.1 3\nend 1.2\n",
                      [&](Session &s) {
                          before = s.visual()->state().settings.look;
                          QVERIFY(!s.visual()->state().settings.autopilot);
                          doc = s.saveSong();
                      });
        QVERIFY2(before.world > 2.05f, qPrintable(QString::number(before.world))); // D, then three fingers to the right
        QVERIFY(before.symmetry > 0.3f);
        QVERIFY(doc.value(QStringLiteral("visual")).isObject());
        testkit::play("end 0.1\n", [&](Session &s) {
            QString error;
            QVERIFY(s.openSong(doc, &error));
            // As the file keeps it: to four places.
            QCOMPARE(toJson(s.visual()->state().settings.look), toJson(before));
            QVERIFY(!s.visual()->state().settings.autopilot);
        });
    }

    // --- the video ---------------------------------------------------------------------

    // Frame k covers the sound from k/fps to (k+1)/fps: however the timer
    // fires, the video holds exactly the frames the sound so far needs.
    void theVideoKeepsInStepWithTheSound() {
        QCOMPARE(video::AvSync::due(0, 30, 48000), int64_t(1));
        QCOMPARE(video::AvSync::due(1600, 30, 48000), int64_t(1));
        QCOMPARE(video::AvSync::due(1601, 30, 48000), int64_t(2));
        video::AvSync sync(30, 48000);
        QCOMPARE(sync.take(0), 1);
        QCOMPARE(sync.take(800), 0);    // early: nothing new due
        QCOMPARE(sync.take(3300), 2);   // late: this frame twice
        QCOMPARE(sync.take(3300), 0);
        QCOMPARE(sync.take(9700), 4);
        // At random, never behind and never more than a frame ahead.
        uint32_t r = 1;
        int64_t samples = 9700;
        for (int i = 0; i < 2000; ++i) {
            r = r * 1664525u + 1013904223u;
            samples += int64_t(r >> 20); // up to 4096 samples, about 85 ms
            sync.take(samples);
            const double video = sync.seconds(), audio = double(samples) / 48000;
            QVERIFY(video >= audio && video <= audio + 1.0 / 30 + 1e-9);
        }
        // Stopped: padded to the sound's length, to the nearest frame.
        video::AvSync end(30, 48000);
        end.take(1000);
        QCOMPARE(end.finish(48000), 29);
        QCOMPARE(end.frames(), int64_t(30));
        // A rate that isn't whole.
        QCOMPARE(video::AvSync::due(44101, 25, 44100.5), int64_t(25));
        QCOMPARE(video::AvSync::due(44102, 25, 44100.5), int64_t(26));
    }

    void ffmpegIsAskedForWhatTheRecordingNeeds() {
        const QStringList enc = video::encodeArgs(1280, 720, 30, QStringLiteral("/tmp/x.video.mp4"));
        QVERIFY(enc.join(' ').contains(QStringLiteral("-f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i -")));
        QVERIFY(enc.join(' ').contains(QStringLiteral("-pix_fmt yuv420p")));
        QVERIFY(enc.contains(QStringLiteral("-an")));
        QCOMPARE(enc.last(), QStringLiteral("/tmp/x.video.mp4"));
        const QStringList mux = video::muxArgs(QStringLiteral("v.mp4"), QStringLiteral("a.wav"), QStringLiteral("out.mp4"));
        QVERIFY(mux.join(' ').contains(QStringLiteral("-i v.mp4 -i a.wav -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac")));
        QCOMPARE(mux.last(), QStringLiteral("out.mp4"));
        qputenv("TENFOLD_FFMPEG", "/nonexistent/ffmpeg");
        QVERIFY(video::findFfmpeg().isEmpty());
        qunsetenv("TENFOLD_FFMPEG");
    }

    // The whole path through a real ffmpeg, when there is one: frames in,
    // muxed with a WAV, a video as long as the sound.
    void aVideoIsMadeWithItsSound() {
        const QString ffmpeg = video::findFfmpeg();
        if (ffmpeg.isEmpty())
            QSKIP("no ffmpeg here");
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("a.wav")), vid = dir.filePath(QStringLiteral("v.mp4")),
                      out = dir.filePath(QStringLiteral("out.mp4"));
        {
            WavWriter w;
            QVERIFY(w.open(wav.toStdString(), 48000));
            std::vector<float> buf(48000 * 2);
            for (size_t i = 0; i < 48000; ++i)
                buf[2 * i] = buf[2 * i + 1] = 0.2f * std::sin(float(i) * 0.05f);
            w.write(buf.data(), 48000);
            w.close();
        }
        video::Encoder enc;
        QString error;
        QVERIFY2(enc.start(ffmpeg, video::encodeArgs(64, 48, 30, vid), &error), qPrintable(error));
        video::AvSync sync(30, 48000);
        QByteArray frame(64 * 48 * 4, char(0));
        for (int64_t s = 0; s <= 40000; s += 2000) {
            frame.fill(char(s / 200));
            enc.push(frame, sync.take(s));
        }
        enc.push({}, sync.finish(48000));
        QCOMPARE(sync.frames(), int64_t(30));
        QVERIFY2(enc.finish(&error), qPrintable(error));
        QVERIFY2(video::runFfmpeg(ffmpeg, video::muxArgs(vid, wav, out), &error), qPrintable(error));
        QVERIFY(QFileInfo(out).size() > 1000);
        // ffmpeg reads it back: one second of picture.
        QProcess probe;
        probe.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-i"), out, QStringLiteral("-map"),
                             QStringLiteral("0:v"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
        QVERIFY(probe.waitForFinished(30000));
        const QString log = QString::fromLocal8Bit(probe.readAllStandardError());
        QVERIFY2(log.contains(QStringLiteral("frame=   30")) || log.contains(QStringLiteral("frame=30")), qPrintable(log.right(400)));
        QVERIFY(log.contains(QStringLiteral("Audio: aac")));
    }
};

TESTKIT_REGISTER(VisualTests)
#include "visual_tests.moc"
