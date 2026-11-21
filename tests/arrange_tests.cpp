// The Arrange page: scenes on the bar, the lane mixer, mutes and solos, the
// moves (build, drop, breakdown, echo-out, cut, lift), and the song, written
// or recorded, replayed from background() whatever page is showing.

#include <QJsonArray>
#include <fstream>
#include <sstream>

#include "testkit.h"

using namespace tf;

namespace {

using testkit::watch;

// Four one-bar loops at 120 BPM, each played and then looped with Space the
// way every part page does it, from single hits and notes so they lean as
// little as possible on any page's layout (the drums' kick and snare pads on
// the near row; single notes on the other pages): drums (page 1), whose Space on the next ONE
// sets the tempo and makes 2.5 s the studio's bar line; then bass (2),
// chords (3) and atmos (6), each played over one bar and looped by a Space
// just after the bar line. Bars fall every 2 s (4.5, 6.5, …); everything
// plays by 14.5 s, and the arrange page shows at 15.
const char *kLoops = "page 0 1\n"
                     "tap 0.5 0.12 0.12\ntap 1.0 0.38 0.12\ntap 1.5 0.12 0.12\ntap 2.0 0.38 0.12\nkey 2.5 Space\n"
                     "page 3.0 2\n"
                     "tap 4.5 0.03 0.5 len=0.3\ntap 5.0 0.03 0.5 len=0.15\ntap 5.5 0.36 0.5 len=0.3\n"
                     "tap 6.0 0.3 0.5 len=0.15\nkey 6.6 Space\n"
                     "page 7.0 3\n"
                     "tap 8.5 0.03 0.6 len=0.15\ntap 9.0 0.03 0.6 len=0.15\ntap 9.5 0.36 0.6 len=0.15\n"
                     "tap 10.0 0.36 0.6 len=0.15\nkey 10.6 Space\n"
                     "page 11.0 6\n"
                     "tap 12.5 0.03 0.4 len=0.9\ntap 13.5 0.17 0.4 len=0.9\nkey 14.6 Space\n"
                     "page 15.0 0\n";
constexpr double kBar0 = 2.5; // the studio's first bar line, in script seconds; bars are 2 s long
double barAt(int n) { return kBar0 + 2.0 * n; }

std::string loops(const std::string &rest) { return std::string(kLoops) + rest; }

// The pad's lanes: seven parts under the scene strip (y 0..0.885).
// A lane per part, all eight of them.
float laneY(int part) { return 0.885f * (1.0f - (part + 0.5f) / float(kMaxParts)); }
float cellX(int slot) { return 0.12f + (0.73f - 0.12f) * (slot + 0.5f) / 8.0f; }

// True on the views published just after the script time x (about every 8 ms).
bool at(double t, double x) { return t >= x - 1e-9 && t < x + 0.02; }

double db(double v) { return 20 * std::log10(std::max(v, 1e-9)); }

// The level below 150 Hz between two times (two one-pole low-passes).
double lowRms(const testkit::Take &take, double from, double to) {
    const double k = 1 - std::exp(-2 * 3.14159265 * 150.0 / 48000.0);
    double a = 0, b = 0, sum = 0;
    size_t n = 0;
    const size_t i0 = size_t(std::max(0.0, from - 0.2) * 48000), i1 = std::min(take.left.size(), size_t(to * 48000));
    for (size_t i = i0; i < i1; ++i) {
        a += k * (0.5 * (take.left[i] + take.right[i]) - a);
        b += k * (a - b);
        if (double(i) / 48000 >= from) {
            sum += b * b;
            ++n;
        }
    }
    return n ? std::sqrt(sum / double(n)) : 0;
}

std::string num(double v) { return QString::number(v, 'f', 4).toStdString(); }

// The mouse: a press and release on a point of the pad, a quick click or a
// held one (its "hold").
std::string mouseClick(double t, float x, float y, double len = 0.1) {
    const std::string id = std::to_string(kMouseId);
    return "down " + num(t) + " " + id + " " + num(x) + " " + num(y) + "\nup " + num(t + len) + " " + id + "\n";
}
std::string mouseHold(double t, float x, float y) { return mouseClick(t, x, y, 0.6); }

// How far a beat is from the nearest bar line, in beats.
double offBar(double beat) { return beat - 4.0 * std::round(beat / 4.0); }

// Plays an opened session on for so many seconds from the script clock's
// now, pressing keys on the showing page `at` seconds in, and calls fn
// after every tick.
struct KeyAt {
    double at;
    int key;
};
void press(Session &s, int key) {
    KeyEvent e;
    e.key = key;
    if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde)
        e.text = QString(QChar(key)).toLower();
    s.key(e);
    e.down = false;
    s.key(e);
}
void playOn(Session &s, double seconds, const std::vector<KeyAt> &keys,
            const std::function<void(double, Session &)> &fn = {}) {
    const double t0 = scriptClock();
    float buf[384 * 2];
    for (int tick = 1; tick <= int(seconds * 125); ++tick) {
        const double t = t0 + tick / 125.0;
        setScriptClock(t);
        s.refresh();
        for (const KeyAt &k : keys)
            if (k.at > (tick - 1) / 125.0 && k.at <= tick / 125.0)
                press(s, k.key);
        s.idle(t);
        s.studio()->render(buf, 384);
        s.refresh();
        if (fn)
            fn(t - t0, s);
    }
}

// The clip each part plays in each bar of a song file's "song", -1 for none.
std::vector<std::array<int, 7>> songBars(const QJsonObject &song, Session &s) {
    std::vector<std::array<int, 7>> out;
    for (const QJsonValue &b : song[QStringLiteral("bars")].toArray()) {
        std::array<int, 7> row;
        row.fill(-1);
        const QJsonObject play = b.toObject()[QStringLiteral("play")].toObject();
        for (int p = 0; p < 7 && p < s.partCount(); ++p)
            row[size_t(p)] = play.value(s.pageName(p)).toInt(-1);
        out.push_back(row);
    }
    return out;
}

}

class ArrangeTests : public QObject {
    Q_OBJECT
private slots:
    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 0\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.8\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    void theLoopsAreThere() {
        const auto take = testkit::play(loops("end 16\n"));
        QCOMPARE(int(std::lround(take.view.bpm)), 120);
        for (int p : {0, 1, 2, 5})
            QVERIFY2(take.view.parts[p].playing == 0, qPrintable(QString("part %1").arg(p)));
    }

    void aSceneLaunchesOnTheBar() {
        // Everything stops, then F1 brings column 1 back: not before the
        // bar line, and every clip from the top of its loop on it.
        double startedBeat = -1, keyBeat = -1;
        int playingBefore = 0;
        watch(loops("key 15.2 Backspace\nkey 17.3 F1\nend 21\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (t > 17.29 && keyBeat < 0)
                keyBeat = v.beat;
            if (t > 17.0 && t < 17.3)
                for (int p = 0; p < v.partCount; ++p)
                    playingBefore += v.parts[p].playing >= 0;
            if (keyBeat >= 0 && startedBeat < 0 && v.parts[0].playing == 0) {
                startedBeat = v.beat - v.parts[0].clips[0].phase * v.parts[0].clips[0].lengthBeats;
                QCOMPARE(v.parts[1].playing, 0);
                QCOMPARE(v.parts[2].playing, 0);
                QCOMPARE(v.parts[5].playing, 0);
            }
        });
        QCOMPARE(playingBefore, 0);
        QVERIFY(startedBeat > keyBeat);
        QVERIFY2(std::fabs(offBar(startedBeat)) < 1e-3, qPrintable(QString::number(startedBeat)));
    }

    void aMouseClickStopsAClipAndAnotherStartsIt() {
        // Bass playing: a click on its cell stops it on the bar; another starts it again.
        int afterStop = 99, afterStart = -9;
        watch(loops(mouseClick(15.3, cellX(0), laneY(1)) + mouseClick(17.3, cellX(0), laneY(1)) + "end 19\n"),
              [&](double t, Session &s) {
                  if (at(t, 17.2))
                      afterStop = s.view().parts[1].playing;
                  if (at(t, 18.9))
                      afterStart = s.view().parts[1].playing;
              });
        QCOMPARE(afterStop, -1);
        QCOMPARE(afterStart, 0);
    }

    void anEmptySlotStaysHonest() {
        // A tap on an empty cell launches nothing; I with no loops writes no song.
        const auto take = testkit::play("page 0 0\ntap 0.5 0.5 0.5\nkey 0.8 I\nkey 1.0 P\nend 4\n");
        for (int p = 0; p < take.view.partCount; ++p)
            QCOMPARE(take.view.parts[p].playing, -1);
        QVERIFY(take.rms(1, 4) < 1e-4);
    }

    void aLaneRidesLevelAndFilterAndTheFilterSpringsBack() {
        // A finger on the chords lane: slide up and right (level up, high-pass),
        // let go: the level stays, the filter comes home.
        float gainHeld = 0, filtHeld = 0, gainAfter = 0, filtAfter = 0;
        const float y = laneY(2);
        watch(loops("down 15.5 9 0.8 " + std::to_string(y) +
                    "\nglide 15.6 16.2 9 0.95 " + std::to_string(y + 0.12f) + "\nup 16.6 9\nend 17.5\n"),
              [&](double t, Session &s) {
                  const auto &ch = s.view().parts[2].channel;
                  if (at(t, 16.4)) {
                      gainHeld = ch.value[Gain];
                      filtHeld = ch.value[Filter];
                  }
                  if (at(t, 17.4)) {
                      gainAfter = ch.value[Gain];
                      filtAfter = ch.value[Filter];
                  }
              });
        QVERIFY2(gainHeld > 0.82f, qPrintable(QString::number(gainHeld)));
        QVERIFY2(filtHeld > 0.65f, qPrintable(QString::number(filtHeld)));
        QVERIFY(std::fabs(gainAfter - gainHeld) < 0.01f);
        QVERIFY2(std::fabs(filtAfter - 0.5f) < 0.01f, qPrintable(QString::number(filtAfter)));
    }

    void aClickLatchesTheRide() {
        const float y = laneY(1);
        const auto take = testkit::play(loops("down 15.5 9 0.8 " + std::to_string(y) +
                                              "\nglide 15.6 16.0 9 0.6 " + std::to_string(y) +
                                              "\nclick 16.2\nup 16.6 9\nend 17.5\n"));
        QVERIFY2(take.view.parts[1].channel.value[Filter] < 0.3f,
                 qPrintable(QString::number(take.view.parts[1].channel.value[Filter])));
    }

    void pressureThrowsALaneIntoTheEcho() {
        float echo = 0;
        const float y = laneY(2);
        watch(loops("down 15.5 9 0.8 " + std::to_string(y) + " p=0.5\nglide 15.6 15.9 9 0.8 " + std::to_string(y) +
                    " p=0.95\nup 16.4 9\nend 16.6\n"),
              [&](double t, Session &s) {
                  if (at(t, 16.2))
                      echo = s.view().parts[2].channel.value[EchoSend];
              });
        QVERIFY2(echo > 0.6f, qPrintable(QString::number(echo)));
    }

    void muteKeysAndHeldSolos() {
        float drumsMuted = 0, bassSolo = 0, bassSoloAfter = 1;
        watch(loops("key 15.5 Q\nkey 16.0 S down\nkey 17.0 S up\nend 17.5\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (at(t, 15.8))
                drumsMuted = v.parts[0].channel.value[Mute];
            if (at(t, 16.5))
                bassSolo = v.parts[1].channel.value[Solo];
            if (at(t, 17.4))
                bassSoloAfter = v.parts[1].channel.value[Solo];
        });
        QCOMPARE(drumsMuted, 1.0f);
        QCOMPARE(bassSolo, 1.0f);
        QCOMPARE(bassSoloAfter, 0.0f);
    }

    void aBuildThinsTheMixTakesABreathAndDropsOnTheOne() {
        // B: a build to the end of the 4-bar phrase, a half-beat breath, the drop.
        double gapBeat = -1, dropBeat = -1, dropTime = -1;
        float topFilter = 0.5f, topReverb = 0;
        int dropAt = -1;
        const auto take = watch(loops("key 15.3 B\nend 32\n"), [&](double, Session &s) {
            const StudioView &v = s.view();
            topFilter = std::max(topFilter, v.master[MasterFilter]);
            topReverb = std::max(topReverb, v.parts[2].channel.value[ReverbSend]);
            bool allMuted = true;
            for (int p = 0; p < v.partCount; ++p)
                allMuted = allMuted && v.parts[p].channel.value[Mute] > 0.5f;
            if (allMuted && gapBeat < 0)
                gapBeat = v.beat;
            if (gapBeat >= 0 && !allMuted && dropBeat < 0) {
                dropBeat = v.beat;
                dropAt = int(std::lround(v.beat / 4.0));
                dropTime = v.time + (dropAt * 4.0 - v.beat) * 0.5; // 120 BPM
            }
        });
        QVERIFY2(topFilter > 0.65f, qPrintable(QString::number(topFilter)));
        QVERIFY(topReverb > 0.45f);
        QVERIFY(gapBeat > 0 && dropBeat > gapBeat);
        QCOMPARE(dropAt % 4, 0);
        // The views come about every 8 ms: the breath is half a beat and the
        // drop is on the line, as far as they can tell.
        QVERIFY2(std::fabs(dropBeat - gapBeat - 0.5) < 0.03, qPrintable(QString("%1 %2").arg(gapBeat).arg(dropBeat)));
        QVERIFY2(offBar(dropBeat) >= -0.001 && offBar(dropBeat) < 0.02, qPrintable(QString::number(dropBeat)));
        // The sound says it to the sample: silence (only the room) through
        // the breath, and the first kick of the drop starts within a
        // millisecond of the line.
        QVERIFY(take.rms(dropTime - 0.2, dropTime - 0.01) < 0.3 * take.rms(dropTime, dropTime + 0.15));
        double onset = -1;
        const size_t from = size_t((dropTime - 0.02) * 48000);
        for (size_t i = from; i < from + 4800 && i < take.left.size(); ++i)
            if (std::fabs(take.left[i]) > 0.25f) {
                onset = double(i) / 48000;
                break;
            }
        QVERIFY2(onset >= dropTime - 0.0005 && onset < dropTime + 0.004,
                 qPrintable(QString("onset %1, line %2").arg(onset).arg(dropTime)));
        // After the drop the bus is open again.
        QVERIFY(std::fabs(take.view.master[MasterFilter] - 0.5f) < 0.01f);
    }

    void theDropBringsEverythingBackFromTheTop() {
        // Stop everything, then Return: on the next bar every part with a clip
        // starts from the top of its loop together.
        double anchors[4] = {-1, -1, -1, -1};
        watch(loops("key 15.2 Backspace\nkey 17.3 Return\nend 20\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (t < 17.3)
                return;
            const int parts[4] = {0, 1, 2, 5};
            for (int k = 0; k < 4; ++k) {
                const auto &pv = v.parts[parts[k]];
                if (anchors[k] < 0 && pv.playing >= 0 && v.beat > 4)
                    anchors[k] = v.beat - pv.clips[pv.playing].phase * pv.clips[pv.playing].lengthBeats;
            }
        });
        for (double a : anchors) {
            QVERIFY(a > 0);
            QVERIFY(std::fabs(offBar(a)) < 1e-3);
            QVERIFY(std::fabs(a - anchors[0]) < 1e-3);
        }
    }

    void aBreakdownStripsToChordsAndAtmosAndComesBack() {
        float during[7] = {}, after[7] = {};
        float pumpDuring = 1;
        watch(loops("key 15.3 N\nkey 18.6 N\nend 21.5\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            if (at(t, 18.0)) {
                for (int p = 0; p < 7; ++p)
                    during[p] = v.parts[p].channel.value[Mute];
                pumpDuring = v.parts[2].channel.value[PumpDepth];
            }
            if (at(t, 21.0))
                for (int p = 0; p < 7; ++p)
                    after[p] = v.parts[p].channel.value[Mute];
        });
        QCOMPARE(during[0], 1.0f); // drums
        QCOMPARE(during[1], 1.0f); // bass
        QCOMPARE(during[2], 0.0f); // chords
        QCOMPARE(during[5], 0.0f); // atmos
        QVERIFY(pumpDuring < 0.1f); // no kick, no pump
        for (float m : after)
            QCOMPARE(m, 0.0f);
    }

    void anEchoOutStopsEverythingAndLeavesATail() {
        // X just after a bar: one bar into the echo, then everything stops on
        // the next line and only the echo is left, fading.
        const auto take = watch(loops("key 16.6 X\nend 33\n"), {});
        for (int p = 0; p < take.view.partCount; ++p)
            QCOMPARE(take.view.parts[p].playing, -1);
        const double stop = barAt(9); // 16.6 → the out bar is 18.5..20.5, the stop at 20.5
        // No cliff: the first beat after the stop is within 10 dB of the bar
        // before it, and the echo still rings two bars on, fading.
        const double before = db(take.rms(stop - 2, stop)), first = db(take.rms(stop, stop + 0.5));
        QVERIFY2(first > before - 10, qPrintable(QString("%1 %2").arg(before).arg(first)));
        QVERIFY(first < before);
        QVERIFY(take.rms(stop + 4, stop + 4.5) > 0.002);
        QVERIFY(take.rms(32, 33) < take.rms(stop + 4, stop + 4.5));
    }

    void aCutSilencesTheLastBeat() {
        const auto take = watch(loops("key 15.3 C\nend 19\n"), {});
        // The bar after 15.3 begins at 16.5: the cut is 16.0..16.5.
        QVERIFY(take.rms(16.05, 16.45) < 0.25 * take.rms(15.5, 15.95));
        QVERIFY(take.rms(16.55, 17.0) > 0.5 * take.rms(15.5, 15.95));
    }

    void aLiftChangesKeyOnTheBar() {
        int before = -1, after = -1;
        watch(loops("key 15.3 L\nend 17\n"), [&](double t, Session &s) {
            if (at(t, 16.3))
                before = s.view().root;
            if (at(t, 16.7))
                after = s.view().root;
        });
        QCOMPARE(before, 9);
        QCOMPARE(after, 11);
    }

    void theTensionFaderBuildsAndLetGoHighDrops() {
        // Up the fader from the middle: the bus thins; let go high: on the next
        // ONE the bus is open and every part sounds.
        float top = 0.5f;
        double gapBeat = -1;
        const auto take = watch(loops("down 15.05 9 0.94 0.4\nglide 15.1 15.5 9 0.94 0.85\nup 15.6 9\nend 19.5\n"),
                                [&](double, Session &s) {
                                    const StudioView &v = s.view();
                                    top = std::max(top, v.master[MasterFilter]);
                                    if (gapBeat < 0 && v.parts[0].channel.value[Mute] > 0.5f)
                                        gapBeat = v.beat;
                                });
        QVERIFY2(top > 0.6f, qPrintable(QString::number(top)));
        QVERIFY(gapBeat > 0);
        QVERIFY(std::fabs(offBar(gapBeat + 0.5)) < 0.06);
        QVERIFY(std::fabs(take.view.master[MasterFilter] - 0.5f) < 0.01f);
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].channel.value[Mute], 0.0f);
    }

    void theTensionFaderLetGoLowSpringsBack() {
        const auto take = watch(loops("down 15.3 9 0.94 0.4\nglide 15.4 15.8 9 0.94 0.55\nup 15.9 9\nend 18\n"), {});
        QVERIFY(std::fabs(take.view.master[MasterFilter] - 0.5f) < 0.01f);
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].channel.value[Mute], 0.0f);
    }

    void aWrittenTrackHasItsShape() {
        // I writes a long track and plays it from the next bar (16.5): intro
        // with drums and atmos, no bass; then the groove brings the bass; the
        // drop has everything; the breakdown has no drums.
        std::vector<std::array<int, 7>> bars;
        std::vector<std::array<int, 7>> muted;
        const auto take = watch(loops("key 15.2 I\nkey 15.4 I\nend 175\n"), [&](double t, Session &s) {
            const double bar = (t - 16.5) / 2.0 - 0.25;
            if (bar >= 0 && std::fabs(bar - std::round(bar)) < 0.003 && int(bars.size()) <= int(std::round(bar))) {
                std::array<int, 7> b{}, m{};
                for (int p = 0; p < 7; ++p) {
                    b[size_t(p)] = s.view().parts[p].playing;
                    m[size_t(p)] = s.view().parts[p].channel.value[Mute] > 0.5f;
                }
                bars.push_back(b);
                muted.push_back(m);
            }
        });
        QVERIFY(bars.size() >= 74);
        auto audible = [&](int bar, int part) { return bars[size_t(bar)][size_t(part)] >= 0 && !muted[size_t(bar)][size_t(part)]; };
        // Intro.
        QVERIFY(audible(0, 0) && audible(0, 5) && !audible(0, 1));
        // Groove.
        QVERIFY(audible(9, 0) && audible(9, 1));
        // First drop (bars 20–35): everything with a clip.
        for (int p : {0, 1, 2, 5})
            QVERIFY2(audible(21, p), qPrintable(QString("drop part %1").arg(p)));
        // Breakdown (36–43): chords and atmos, no drums or bass.
        QVERIFY(!audible(38, 0) && !audible(38, 1) && audible(38, 2) && audible(38, 5));
        // Second drop (48–63).
        for (int p : {0, 1, 2, 5})
            QVERIFY(audible(50, p));
        // After the echo-out, everything has stopped; the track ends in its echo.
        for (int p = 0; p < 7; ++p)
            QCOMPARE(bars[73][size_t(p)], -1);
        QVERIFY(take.rms(16.5 + 2 * 21, 16.5 + 2 * 23) > 1.4 * take.rms(16.5 + 2 * 37, 16.5 + 2 * 39));
        // The drop hits: at least 2 dB over the groove; the build's last bar
        // has its low end taken away (10 dB under the drop's); the last drop
        // is no weaker than the first.
        auto bar = [&](int b) { return 16.5 + 2.0 * b; };
        const double groove = db(take.rms(bar(9), bar(15))), drop1 = db(take.rms(bar(21), bar(27)));
        QVERIFY2(drop1 > groove + 2, qPrintable(QString("groove %1 drop %2").arg(groove).arg(drop1)));
        const double lowBuild = db(lowRms(take, bar(19), bar(19) + 1.7)), lowDrop = db(lowRms(take, bar(20), bar(22)));
        QVERIFY2(lowBuild < lowDrop - 10, qPrintable(QString("build %1 drop %2").arg(lowBuild).arg(lowDrop)));
        const double drop2 = db(take.rms(bar(49), bar(63)));
        QVERIFY2(drop2 > drop1 - 0.5, qPrintable(QString("drop1 %1 drop2 %2").arg(drop1).arg(drop2)));
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
    }

    // A song paused (Ctrl+P) stops where it is and plays on from the top of
    // that bar: everything after comes that much later, on the new bars.
    void aPausedSongPlaysOnFromWhereItStopped() {
        // The written track plays from 16.5 (bars every 2 s). Paused 0.7 s
        // into its fourth bar (23.2), played at 26.3: that bar again from the
        // top, 3.8 s after it first began, and the rest 3.8 s late.
        auto firstBass = [&](const std::string &pause, float *whilePaused, float *after) {
            double first = -1;
            bool wasOff = false;
            watch(loops("key 15.2 I\nkey 15.4 I\n" + pause + "end 48\n"), [&](double t, Session &s) {
                if (whilePaused && std::fabs(t - 25.0) < 0.004)
                    *whilePaused = s.probe(s.arrangePage(), QStringLiteral("songPlaying"));
                if (after && std::fabs(t - 27.0) < 0.004)
                    *after = s.probe(s.arrangePage(), QStringLiteral("songPlaying"));
                // The intro has no bass: the first time it comes back.
                const bool on = s.view().parts[1].playing >= 0;
                if (t > 16.6 && first < 0 && on && wasOff)
                    first = t;
                if (t > 16.6)
                    wasOff |= !on;
            });
            return first;
        };
        float whilePaused = -1, after = -1;
        const double plain = firstBass("", nullptr, nullptr);
        const double paused = firstBass("key 23.2 Ctrl+P\nkey 26.3 Ctrl+P\n", &whilePaused, &after);
        QCOMPARE(whilePaused, 0.0f);
        QCOMPARE(after, 1.0f);
        QVERIFY(plain > 23.2);
        QVERIFY2(std::fabs(paused - plain - 3.8) < 0.05, qPrintable(QString("bass at %1, unpaused %2").arg(paused).arg(plain)));
    }

    void aRecordedSongReplaysBarByBarFromAnyPage() {
        // Record: four bars of everything, F1-less: mute the drums for two
        // bars with the keys, then stop recording. Play it back from another
        // page: the mutes come back on the same bars.
        //   15.3 O → recording from 16.5 (bar 7)
        //   18.6 Q → drums muted from 18.6; 22.6 Q → back
        //   23.3 O → recording ends on 24.5: four bars (16.5–24.5)
        //   24.7 P → the song plays from 26.5; 25.0 page 2: the arrange page is hidden
        std::vector<std::pair<double, float>> mutes;
        const auto take = watch(loops("key 15.3 O\nkey 18.6 Q\nkey 22.6 Q\nkey 23.3 O\nkey 24.7 P\npage 25.0 2\nend 35.5\n"),
                                [&](double t, Session &s) {
                                    const float m = s.view().parts[0].channel.value[Mute];
                                    if (mutes.empty() || mutes.back().second != m)
                                        mutes.push_back({t, m});
                                });
        // Muted live at 18.6, back at 22.6; replayed at 28.6 (±1/16) and 32.6.
        QVERIFY2(mutes.size() >= 5, qPrintable(QString::number(mutes.size())));
        QVERIFY(std::fabs(mutes[1].first - 18.6) < 0.02);
        QVERIFY(std::fabs(mutes[2].first - 22.6) < 0.02);
        QVERIFY2(std::fabs(mutes[3].first - 28.6) < 0.15, qPrintable(QString::number(mutes[3].first)));
        QVERIFY2(std::fabs(mutes[4].first - 32.6) < 0.15, qPrintable(QString::number(mutes[4].first)));
        QCOMPARE(take.view.parts[0].channel.value[Mute], 0.0f);
    }

    void aRecordedBuildReplays() {
        // A build recorded into the song comes back with its drop on the same bar.
        //   15.3 O → take from 16.5; 16.7 B → a build from 18.5 to the end of
        //   the next 4-bar phrase (the studio's bar 16, at 28.5), where it drops;
        //   29.3 O → the take ends at 30.5 (seven bars); 30.7 P → plays from
        //   32.5: its drop at 44.5
        std::vector<double> drops;
        bool wasGap = false;
        watch(loops("key 15.3 O\nkey 16.7 B\nkey 29.3 O\nkey 30.7 P\nend 46\n"), [&](double t, Session &s) {
            const StudioView &v = s.view();
            bool all = true;
            for (int p = 0; p < v.partCount; ++p)
                all = all && v.parts[p].channel.value[Mute] > 0.5f;
            if (wasGap && !all)
                drops.push_back(t);
            wasGap = all;
        });
        QCOMPARE(int(drops.size()), 2);
        QVERIFY2(std::fabs(drops[0] - 28.5) < 0.02, qPrintable(QString::number(drops[0])));
        QVERIFY2(std::fabs(drops[1] - 44.5) < 0.02, qPrintable(QString::number(drops[1])));
    }

    void theDropLaunchesNothingThePlayerHasNotPlayed() {
        // The bass is stopped (a tap on its clip) and stays stopped for more
        // than sixteen bars; the drop brings back the others, not the bass.
        const auto take = testkit::play(loops(mouseClick(15.3, cellX(0), laneY(1)) + "key 51.3 Return\nend 55\n"));
        QCOMPARE(take.view.parts[1].playing, -1);
        for (int p : {0, 2, 5})
            QCOMPARE(take.view.parts[p].playing, 0);
        // And with nothing played at all, a drop does nothing.
        const auto none = testkit::play("page 0 0\nkey 0.5 Return\nkey 0.6 B\nend 6\n");
        QVERIFY(none.rms(0, 6) < 1e-4);
    }

    // --- touching: the pointer, the second finger, the mouse ---

    void thePointerMovesLikeACursorAndASecondFingerActs() {
        // The pointer starts at (0.42, 0.5). A finger landing in the clip area
        // and moving slowly moves it as far as itself: here onto the bass's
        // clip (slot 1). Nothing happens on landing; the status names what it
        // would do; a second finger's tap (anywhere in the clip area) stops
        // the bass on the bar.
        const float dx = cellX(0) - 0.42f, dy = laneY(1) - 0.5f;
        QString hint;
        int bassAfter = 99, bassDuring = 99;
        watch(loops("down 15.2 1 0.5 0.4\nglide 15.3 18.3 1 " + num(0.5 + dx) + " " + num(0.4 + dy) + "\n" +
                    "tap 18.6 0.3 0.2 len=0.08\nup 18.9 1\nend 21\n"),
              [&](double t, Session &s) {
                  if (at(t, 18.4)) {
                      hint = s.pageStatus();
                      bassDuring = s.view().parts[1].playing;
                  }
                  if (at(t, 20.8))
                      bassAfter = s.view().parts[1].playing;
              });
        QVERIFY2(hint.contains(QStringLiteral("start / stop")), qPrintable(hint));
        QCOMPARE(bassDuring, 0);
        QCOMPARE(bassAfter, -1);
    }

    void aFastFingerMovesThePointerFurther() {
        // The same distance, quickly: the pointer goes further (acceleration),
        // past the bass to the chords' row or beyond.
        QString slow, fast;
        watch(loops("down 15.2 1 0.5 0.4\nglide 15.3 18.3 1 0.5 0.55\nend 18.5\n"),
              [&](double t, Session &s) {
                  if (at(t, 18.4))
                      slow = s.pageStatus();
              });
        watch(loops("down 15.2 1 0.5 0.4\nglide 15.3 15.4 1 0.5 0.55\nend 15.6\n"),
              [&](double t, Session &s) {
                  if (at(t, 15.5))
                      fast = s.pageStatus();
              });
        // Slow: up 0.15 from 0.5 lands on the chords' row (0.68 is the bass's).
        // Fast: further up, onto the bass's row or the drums'.
        QVERIFY(slow != fast);
    }

    void aHeldSecondFingerSolosAndTheClickActs() {
        // The pointer on the bass's name: a second finger held solos it while
        // held; the pad's click then mutes it.
        float soloHeld = 0, soloAfter = 1, muteAfter = 0;
        const float dx = 0.06f - 0.42f, dy = laneY(1) - 0.5f;
        watch(loops("down 15.2 1 0.6 0.4\nglide 15.3 18.3 1 " + num(0.6 + dx) + " " + num(0.4 + dy) + "\n" +
                    "down 18.5 2 0.3 0.2\nup 19.4 2\nclick 19.8\nup 20.2 1\nend 20.5\n"),
              [&](double t, Session &s) {
                  if (at(t, 19.2))
                      soloHeld = s.view().parts[1].channel.value[Solo];
                  if (at(t, 19.6))
                      soloAfter = s.view().parts[1].channel.value[Solo];
                  if (at(t, 20.3))
                      muteAfter = s.view().parts[1].channel.value[Mute];
              });
        QCOMPARE(soloHeld, 1.0f);
        QCOMPARE(soloAfter, 0.0f);
        QCOMPARE(muteAfter, 1.0f);
    }

    void theMouseClicksWhatItIsOnAndRidesTheMixer() {
        // A click on scene 1's cell launches it; a drag up the fader builds.
        float top = 0.5f;
        const auto take = watch(loops("key 15.2 Backspace\n" + mouseClick(17.0, cellX(0), 0.95f) +
                                      "down 19.0 " + std::to_string(kMouseId) + " 0.94 0.3\nglide 19.1 19.6 " +
                                      std::to_string(kMouseId) + " 0.94 0.55\nup 19.7 " + std::to_string(kMouseId) +
                                      "\nend 20\n"),
                                [&](double t, Session &s) {
                                    if (t > 19)
                                        top = std::max(top, s.view().master[MasterFilter]);
                                });
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].playing, 0);
        QVERIFY(top > 0.52f);
    }

    // --- editing clips ---

    void aHeldClipIsPickedAndDoubledInTime() {
        // A held mouse button picks the bass; D copies it into the next free
        // slot, in time with the original; the other parts are untouched.
        int drumsEvents = -1, chordsEvents = -1;
        const auto take = watch(loops(mouseHold(15.2, cellX(0), laneY(1)) + "key 16.0 D\nend 18.6\n"),
                                [&](double t, Session &s) {
                                    if (at(t, 15.1)) {
                                        drumsEvents = s.view().parts[0].clips[0].events;
                                        chordsEvents = s.view().parts[2].clips[0].events;
                                    }
                                });
        const auto &bass = take.view.parts[1];
        QVERIFY(bass.clips[1].state != ClipView::Empty);
        QCOMPARE(bass.clips[1].events, bass.clips[0].events);
        QCOMPARE(bass.clips[1].lengthBeats, bass.clips[0].lengthBeats);
        QVERIFY(std::fabs(bass.clips[1].phase - bass.clips[0].phase) < 1e-4);
        QCOMPARE(bass.playing, 0);
        QCOMPARE(take.view.parts[0].clips[0].events, drumsEvents);
        QCOMPARE(take.view.parts[2].clips[0].events, chordsEvents);
        for (int p : {0, 2, 5})
            QVERIFY(take.view.parts[p].clips[1].state == ClipView::Empty);
    }

    void ctrlZTakesADoubleBack() {
        const auto take = testkit::play(loops(mouseHold(15.2, cellX(0), laneY(1)) + "key 16.0 D\nkey 16.6 Ctrl+Z\nend 17.5\n"));
        QVERIFY(take.view.parts[1].clips[1].state == ClipView::Empty);
        QVERIFY(take.view.parts[1].clips[0].state != ClipView::Empty);
    }

    void twoPickedClipsLayerOrChain() {
        // Double the bass, pick both (hold slot 1, then hold slot 2), chain:
        // slot 1 becomes twice as long; slot 2 is as it was. Ctrl+Z takes the
        // chain back. Then layer: the same length, twice the events.
        const std::string pick = mouseHold(15.2, cellX(0), laneY(1)) + "key 16.0 D\n" +
                                 mouseHold(16.4, cellX(1), laneY(1));
        const auto chained = testkit::play(loops(pick + "key 17.3 Shift+M\nend 18\n"));
        const auto &b = chained.view.parts[1];
        QCOMPARE(b.clips[0].lengthBeats, 2 * b.clips[1].lengthBeats);
        QCOMPARE(b.clips[0].events, 2 * b.clips[1].events);
        const auto undone = testkit::play(loops(pick + "key 17.3 Shift+M\nkey 17.6 Ctrl+Z\nend 18\n"));
        QCOMPARE(undone.view.parts[1].clips[0].lengthBeats, undone.view.parts[1].clips[1].lengthBeats);
        const auto layered = testkit::play(loops(pick + "key 17.3 M\nend 18\n"));
        const auto &l = layered.view.parts[1];
        QCOMPARE(l.clips[0].lengthBeats, l.clips[1].lengthBeats);
        QCOMPARE(l.clips[0].events, 2 * l.clips[1].events);
        for (int p : {0, 2, 5})
            QCOMPARE(layered.view.parts[p].clips[1].state, uint8_t(ClipView::Empty));
    }

    void deleteClearsAClipAndCtrlZBringsItBack() {
        const auto once = testkit::play(loops(mouseHold(15.2, cellX(0), laneY(2)) + "key 16.0 Delete\nend 16.5\n"));
        QVERIFY(once.view.parts[2].clips[0].state == ClipView::Empty);
        QVERIFY(once.view.parts[1].clips[0].state != ClipView::Empty);
        const auto back =
            testkit::play(loops(mouseHold(15.2, cellX(0), laneY(2)) + "key 16.0 Delete\nkey 16.3 Ctrl+Z\nend 16.5\n"));
        QVERIFY(back.view.parts[2].clips[0].state != ClipView::Empty);
    }

    void theChipsUnderTheSongEditThePick() {
        // Picked, the bass's actions show as chips under the song band:
        // DOUBLE first, after the fixed name column (a quarter less 1%).
        const int n = 5; // DOUBLE, MOVE left, MOVE right, CLEAR, UNPICK
        const double lw = 0.24, w = (1 - lw + 4.0 / 900) / n - 4.0 / 900;
        const std::string x = num(lw + w * 0.5);
        const auto take = testkit::play(loops(mouseHold(15.2, cellX(0), laneY(1)) + "button 16.0 " + x + " -0.119\nend 16.5\n"));
        QVERIFY(take.error.empty());
        const auto &bass = take.view.parts[1];
        QVERIFY(bass.clips[1].state != ClipView::Empty);
        QCOMPARE(bass.clips[1].events, bass.clips[0].events);
        // Nothing picked: the row has no chips, and a click there does nothing.
        const auto none = testkit::play(loops("button 16.0 " + x + " -0.119\nend 16.5\n"));
        QVERIFY(none.view.parts[1].clips[1].state == ClipView::Empty);
    }

    void aRightClickOpensTheMenuOfThePick() {
        // A right click on the bass picks it and opens the menu at the mouse;
        // its first item (DOUBLE) doubles it. Items start 28 px below the
        // click and are 28 px high, on a 562.5 px high pad.
        const double dy = 41.0 / 562.5, dx = 40.0 / 900;
        const auto item = [&](double t, int slot, int row) {
            return "button " + num(t) + " " + num(cellX(slot) + dx) + " " + num(laneY(1) - dy - row * 28.0 / 562.5) + "\n";
        };
        const auto rclick = [&](double t, int slot) {
            return "rbutton " + num(t) + " " + num(cellX(slot)) + " " + num(laneY(1)) + "\n";
        };
        const auto doubled = testkit::play(loops(rclick(15.2, 0) + item(15.4, 0, 0) + "end 16\n"));
        QVERIFY(doubled.error.empty());
        QVERIFY(doubled.view.parts[1].clips[1].state != ClipView::Empty);
        // A click beside the menu only closes it.
        const auto closed = testkit::play(loops(rclick(15.2, 0) + "button 15.4 0.95 0.95\n" + item(15.6, 0, 0) + "end 16\n"));
        QVERIFY(closed.view.parts[1].clips[1].state == ClipView::Empty);
        // A right click on an empty slot, a lane's name or the mixer opens
        // nothing: a click where the menu's first item would be does nothing.
        for (const std::string &x : {num(cellX(3)), num(0.05), num(0.8)}) {
            const auto none = testkit::play(loops("rbutton 15.2 " + x + " " + num(laneY(1)) + "\n" + item(15.4, 3, 0) +
                                                  item(15.5, 0, 0) + "end 16\n"));
            for (int s = 1; s < 8; ++s)
                QVERIFY(none.view.parts[1].clips[s].state == ClipView::Empty);
        }
        // ...and it only closes an open menu, keeping the pick.
        const auto kept = testkit::play(loops(rclick(15.2, 0) + "rbutton 15.4 " + num(cellX(3)) + " " + num(laneY(1)) +
                                              "\n" + item(15.6, 0, 0) + "key 15.8 D\nend 16.2\n"));
        QVERIFY(kept.view.parts[1].clips[1].state != ClipView::Empty);
        QVERIFY(kept.view.parts[1].clips[2].state == ClipView::Empty);
        // With a clip picked, a right click on its lane's name opens no menu
        // there either: a click where its first item would be is no DOUBLE.
        const std::string onName = "rbutton 15.4 0.05 " + num(laneY(1)) + "\nbutton 15.6 " + num(0.05 + dx) + " " +
                                   num(laneY(1) - dy) + "\n";
        const auto named = testkit::play(loops(rclick(15.2, 0) + onName + "end 16\n"));
        QVERIFY(named.view.parts[1].clips[1].state == ClipView::Empty);
        // Doubled, then a right click on the copy adds it to the pick: the
        // menu's first item now layers it into slot 1.
        const auto layered =
            testkit::play(loops(rclick(15.2, 0) + item(15.4, 0, 0) + rclick(15.8, 1) + item(16.0, 1, 0) + "end 16.5\n"));
        const auto &l = layered.view.parts[1];
        QCOMPARE(l.clips[0].lengthBeats, l.clips[1].lengthBeats);
        QCOMPARE(l.clips[0].events, 2 * l.clips[1].events);
    }

    void aMovedClipCarriesOnInTime() {
        // Pick the bass and press Shift+→: it moves to slot 2. Playing, it
        // carries on from there at the next bar, and slot 1 is cleared then.
        double anchorBefore = -1, anchorAfter = -1;
        const auto take = watch(loops(mouseHold(15.2, cellX(0), laneY(1)) + "key 16.0 Shift+Right\nend 19\n"),
                                [&](double t, Session &s) {
                                    const PartView &b = s.view().parts[1];
                                    const auto anchor = [&](int slot) {
                                        return s.view().beat - b.clips[slot].phase * b.clips[slot].lengthBeats;
                                    };
                                    if (at(t, 16.1))
                                        anchorBefore = std::fmod(anchor(0), 4.0);
                                    if (at(t, 18.9))
                                        anchorAfter = std::fmod(anchor(1), 4.0);
                                });
        const auto &b = take.view.parts[1];
        QCOMPARE(b.playing, 1);
        QVERIFY(b.clips[0].state == ClipView::Empty);
        QVERIFY(std::fabs(anchorBefore - anchorAfter) < 1e-3 || std::fabs(std::fabs(anchorBefore - anchorAfter) - 4) < 1e-3);
    }

    // --- editing the song ---

    void songSectionsAreRemovedDoubledJoinedAndMovedAndComeBack() {
        // A written track (I twice), stopped (P); the arrows pick sections.
        // The song's length shows in the status once P plays it again.
        auto lengthAfter = [&](const std::string &edits) {
            int n = 0;
            watch(loops("key 15.2 I\nkey 15.4 I\nkey 16.0 P\n" + edits + "key 20.0 P\nend 21\n"),
                  [&](double t, Session &s) {
                      const QString st = s.pageStatus();
                      const int slash = st.indexOf('/'), sp = st.indexOf(' ', slash);
                      if (slash > 0 && at(t, 20.8))
                          n = st.mid(slash + 1, sp - slash - 1).toInt();
                  });
            return n;
        };
        // The long form: intro 8, groove 8, build 4, drop 8 + 8, break 8, build 4,
        // drop 8 + 8, outro 8, end 4 = 76 bars. Right picks the intro, Right
        // again the groove.
        QCOMPARE(lengthAfter(""), 76);
        QCOMPARE(lengthAfter("key 16.5 Right\nkey 16.7 Right\nkey 17.0 Delete\n"), 68);
        QCOMPARE(lengthAfter("key 16.5 Right\nkey 16.7 Right\nkey 17.0 D\n"), 84);
        QCOMPARE(lengthAfter("key 16.5 Right\nkey 16.7 Right\nkey 17.0 Delete\nkey 17.5 Shift+O\n"), 76);
        QCOMPARE(lengthAfter("key 16.5 Right\nkey 17.0 [\n"), 76);
        // Joining keeps the bars and makes one section of two.
        QString picked;
        watch(loops("key 15.2 I\nkey 15.4 I\nkey 16.0 P\nkey 16.5 Right\nkey 17.0 M\nkey 17.5 Right\nend 18\n"),
              [&](double t, Session &s) {
                  if (at(t, 17.8))
                      picked = s.pageStatus();
              });
        // After the join, the second section is the build (the groove joined the intro).
        QVERIFY2(picked.contains(QStringLiteral("build")), qPrintable(picked));
    }

    void aFaderClickLeavesAClipUnderARestingFingerAlone() {
        // A finger rests on the chords clip; another drives the fader up and
        // clicks to drop: the chords clip is still there.
        const std::string chords = std::to_string(cellX(0)) + " " + std::to_string(laneY(2));
        const auto take = testkit::play(loops("down 15.2 1 " + chords + "\ndown 15.4 2 0.94 0.40\n" +
                                              "glide 15.5 16.2 2 0.94 0.85\nclick 16.3\nup 16.5 2\nup 16.7 1\nend 19\n"));
        QVERIFY(take.view.parts[2].clips[0].state != ClipView::Empty);
        QCOMPARE(take.view.parts[2].playing, 0);
    }

    void aFaderClickBelowTheLineDoesNothing() {
        // A click on a fader barely touched: no breath, no drop, nothing muted.
        bool muted = false;
        watch(loops("down 15.2 1 0.94 0.40\nclick 15.8\nup 16.2 1\nend 19\n"), [&](double t, Session &s) {
            if (t > 15.2)
                for (int p = 0; p < s.view().partCount; ++p)
                    muted = muted || s.view().parts[p].channel.value[Mute] > 0.5f;
        });
        QVERIFY(!muted);
    }

    void movesNeedSomethingPlaying() {
        // N, B and X with nothing playing do nothing, and leave nothing muted
        // for the loop made later.
        const auto take = testkit::play("page 0 0\nkey 0.5 N\nkey 0.7 B\nkey 0.9 X\npage 2.0 1\n"
                                        "tap 4.5 0.12 0.12\ntap 5.0 0.38 0.12\ntap 5.5 0.12 0.12\ntap 6.0 0.38 0.12\n"
                                        "key 6.5 Space\nend 14\n");
        for (int p = 0; p < take.view.partCount; ++p)
            QCOMPARE(take.view.parts[p].channel.value[Mute], 0.0f);
        QVERIFY(take.rms(10, 14) > 0.01);
    }

    void aBreakdownOfDrumsAloneIsNeverSilentAndEnds() {
        // Only drums: N keeps them, high-passed and washed; eight bars on,
        // they are back as they were.
        const char *drums = "page 0 1\ntap 0.5 0.12 0.12\ntap 1.0 0.38 0.12\ntap 1.5 0.12 0.12\ntap 2.0 0.38 0.12\n"
                            "key 2.5 Space\npage 3.0 0\nkey 3.3 N\nend 26\n";
        float filterDuring = 0.5f;
        const auto take = watch(drums, [&](double t, Session &s) {
            if (at(t, 8.0))
                filterDuring = s.view().parts[0].channel.value[Filter];
        });
        for (int b = 1; b < 10; ++b)
            QVERIFY2(take.rms(barAt(b), barAt(b + 1)) > 0.005, qPrintable(QString("bar %1").arg(b)));
        QVERIFY(filterDuring > 0.6f);
        QCOMPARE(take.view.parts[0].channel.value[Mute], 0.0f);
        QVERIFY(std::fabs(take.view.parts[0].channel.value[Filter] - 0.5f) < 0.01f);
    }

    void aWrittenTrackFromThinLoopsHasNoSilentBar() {
        // Drums alone, and drums with bass: every bar before the outro sounds.
        const std::string drumsLoop = "page 0 1\ntap 0.5 0.12 0.12\ntap 1.0 0.38 0.12\ntap 1.5 0.12 0.12\n"
                                      "tap 2.0 0.38 0.12\nkey 2.5 Space\n";
        const std::string bassLoop = "page 3.0 2\ntap 4.5 0.03 0.5 len=0.3\ntap 5.0 0.03 0.5 len=0.15\n"
                                     "tap 5.5 0.36 0.5 len=0.3\ntap 6.0 0.3 0.5 len=0.15\nkey 6.6 Space\n";
        for (const std::string &start : {drumsLoop, drumsLoop + bassLoop}) {
            // I (twice: clips play) on 7.3: the track starts on the bar at 8.5.
            const auto take = testkit::play(start + "page 7.0 0\nkey 7.1 I\nkey 7.3 I\nend 90\n");
            // The short form: 32 bars before the outro.
            for (int b = 0; b < 32; ++b) {
                const double t0 = 8.5 + 2.0 * b;
                QVERIFY2(take.rms(t0, t0 + 2) > 0.005, qPrintable(QString("bar %1 is silent").arg(b)));
            }
        }
    }

    void aWrittenTrackEndsInSilenceAndGivesTheSendsBack() {
        // Drums and bass, the short form from 8.5: 40 bars, to 88.5. Its
        // echo-out dies away inside the song's end bars, and afterwards
        // every channel's echo send is what it was before the track.
        const std::string start = "page 0 1\ntap 0.5 0.12 0.12\ntap 1.0 0.38 0.12\ntap 1.5 0.12 0.12\n"
                                  "tap 2.0 0.38 0.12\nkey 2.5 Space\n"
                                  "page 3.0 2\ntap 4.5 0.03 0.5 len=0.3\ntap 5.0 0.03 0.5 len=0.15\n"
                                  "tap 5.5 0.36 0.5 len=0.3\ntap 6.0 0.3 0.5 len=0.15\nkey 6.6 Space\n";
        float before[2] = {-1, -1};
        const auto take = watch(start + "page 7.0 0\nkey 7.1 I\nkey 7.3 I\nend 95\n", [&](double t, Session &s) {
            if (at(t, 7.0))
                for (int p = 0; p < 2; ++p)
                    before[p] = s.view().parts[p].channel.value[EchoSend];
        });
        for (int p = 0; p < 2; ++p)
            QVERIFY2(std::fabs(take.view.parts[p].channel.value[EchoSend] - before[p]) < 0.02f,
                     qPrintable(QString("part %1 echo %2, was %3").arg(p).arg(take.view.parts[p].channel.value[EchoSend]).arg(before[p])));
        // The echo rings into the end bars and is gone by the song's end.
        QVERIFY(take.rms(80.5, 81.5) > 0.002);
        QVERIFY2(take.rms(88.0, 88.5) < 0.1 * take.rms(80.5, 81.5),
                 qPrintable(QString("%1 %2").arg(take.rms(88.0, 88.5)).arg(take.rms(80.5, 81.5))));
    }

    void anArmedSongTakeShowsBeforeItsBar() {
        // Space at 15.2 arms a take for the bar at 16.5: armed, and saying
        // so, until then; recording after.
        float armed = 0, recording = 0;
        QString status;
        watch(loops("key 15.2 Space\nend 17\n"), [&](double t, Session &s) {
            if (at(t, 15.6)) {
                armed = s.probe(s.arrangePage(), QStringLiteral("songArmed"));
                status = s.pageStatus();
            }
            if (at(t, 16.8))
                recording = s.probe(s.arrangePage(), QStringLiteral("songRecording"));
        });
        QCOMPARE(armed, 1.0f);
        QVERIFY2(status.contains(QStringLiteral("from the next bar")), qPrintable(status));
        QCOMPARE(recording, 1.0f);
    }

    void songsSurviveSlipsOfTheHand() {
        // A written song; then Space twice (a take armed and called off), and
        // P still plays it. A take recorded over it can be undone with Shift+O.
        //   15.2/15.4 I: the track; 16.0 P stops it; 16.2/16.3 Space Space;
        //   17.0 P: it plays again from 18.5.
        const auto take = testkit::play(loops("key 15.2 I\nkey 15.4 I\nkey 16.0 P\nkey 16.2 Space\nkey 16.3 Space\n"
                                              "key 17.0 P\nend 24\n"));
        QVERIFY(take.rms(19, 24) > 0.01);
        int lengthAfterTake = 0, lengthAfterUndo = 0;
        // A take armed at 16.2 (from 16.5) and closed at 19.0 (on 20.5) holds
        // two bars and replaces it; Shift+O brings the written one back.
        watch(loops("key 15.2 I\nkey 15.4 I\nkey 16.0 P\nkey 16.2 Space\nkey 19.0 Space\nkey 21.0 P\n"
                    "key 22.0 P\nkey 22.2 Shift+O\nkey 22.4 P\nend 24\n"),
              [&](double t, Session &s) {
                  const QString st = s.pageStatus();
                  const int slash = st.indexOf('/'), sp = st.indexOf(' ', slash);
                  if (slash > 0 && at(t, 21.5))
                      lengthAfterTake = st.mid(slash + 1, sp - slash - 1).toInt();
                  if (slash > 0 && at(t, 23.0))
                      lengthAfterUndo = st.mid(slash + 1, sp - slash - 1).toInt();
              });
        QCOMPARE(lengthAfterTake, 2);
        QCOMPARE(lengthAfterUndo, 76);
    }

    void storedScenesRecallTheMix() {
        // Shift+F3 stores what plays; Backspace stops it all; F3 brings it back.
        const auto take = testkit::play(loops("key 15.3 Shift+F3\nkey 15.5 Backspace\nkey 17.0 F3\nend 19\n"));
        for (int p : {0, 1, 2, 5})
            QCOMPARE(take.view.parts[p].playing, 0);
    }

    void staysLeanOnTheQueue() {
        // A whole written track sends a modest number of mixer changes: only
        // real changes, never every tick.
        int changes = 0;
        float last[8][ChannelCount] = {};
        watch(loops("key 15.2 Shift+I\nkey 15.4 Shift+I\nend 60\n"), [&](double, Session &s) {
            for (int p = 0; p < 7; ++p)
                for (int c = 0; c < ChannelCount; ++c) {
                    const float v = s.view().parts[p].channel.value[c];
                    if (v != last[p][c])
                        ++changes;
                    last[p][c] = v;
                }
        });
        QVERIFY2(changes < 6000, qPrintable(QString::number(changes)));
    }

    // --- song files ---

    void aSavedSongComesBackStoppedAndPlaysAsItDid() {
        // A written track (I twice), stopped (P); scene 3 stored over the
        // four loops (Shift+F3); the bass muted (W). Saved, opened in a fresh
        // tenfold: the song is all there, and waits for P.
        const std::string script = loops("key 15.1 Shift+F3\nkey 15.2 I\nkey 15.4 I\nkey 16.0 P\nkey 16.2 W\nend 17\n");
        bool ran = false;
        testkit::reopen(script, 3.0, [&](Session &before, Session &opened, const QJsonObject &doc) {
            ran = true;
            const QJsonObject song = doc[QStringLiteral("song")].toObject();
            QCOMPARE(song[QStringLiteral("bars")].toArray().size(), 76);
            QCOMPARE(int(std::lround(song[QStringLiteral("tempo")].toDouble())), 120);
            QStringList moves;
            for (const QJsonValue &m : song[QStringLiteral("moves")].toArray())
                moves << m.toObject()[QStringLiteral("move")].toString();
            for (const char *m : {"build", "breakdown", "echoOut"})
                QVERIFY2(moves.contains(QLatin1String(m)), qPrintable(moves.join(' ')));
            QVERIFY(song[QStringLiteral("channels")].toObject().contains(QStringLiteral("Drums")));
            const QJsonArray scenes = song[QStringLiteral("scenes")].toArray();
            QCOMPARE(scenes.size(), 1);
            QCOMPARE(scenes[0].toObject()[QStringLiteral("scene")].toInt(), 2);
            // What the opened page saves is what the file holds.
            QCOMPARE(opened.saveSong()[QStringLiteral("song")].toObject(), song);
            // The song waits for P; the loops play on, the bass still muted.
            const int arrange = opened.arrangePage();
            QVERIFY2(!opened.pageStatus(arrange).contains(QStringLiteral("song "), Qt::CaseInsensitive),
                     qPrintable(opened.pageStatus(arrange)));
            QCOMPARE(opened.view().parts[1].channel.value[Mute], 1.0f);
            for (int p = 0; p < 7; ++p)
                QCOMPARE(opened.view().parts[p].playing, before.view().parts[p].playing);

            // Everything stopped, then F3 brings back the stored scene.
            playOn(opened, 5.0, {{0.3, Qt::Key_Backspace}, {2.6, Qt::Key_F3}});
            for (int p : {0, 1, 2, 5})
                QCOMPARE(opened.view().parts[p].playing, 0);

            // P (half way through a bar): the song launches the clips its
            // bars name, bar by bar, from the next.
            const auto want = songBars(song, opened);
            std::vector<std::array<int, 7>> got;
            int origin = -1;
            playOn(opened, 50.0, {}, [&](double, Session &s) {
                const double beat = s.view().beat;
                if (origin < 0 && beat - 4 * std::floor(beat / 4) >= 2.0) {
                    press(s, Qt::Key_P);
                    origin = int(std::floor(beat / 4)) + 1;
                }
                if (origin < 0)
                    return;
                const double into = beat - (origin + int(got.size())) * 4.0;
                if (into >= 2.0 && into < 4.0 && got.size() < 22) {
                    std::array<int, 7> row;
                    for (int p = 0; p < 7; ++p)
                        row[size_t(p)] = s.view().parts[p].playing;
                    got.push_back(row);
                }
            });
            QCOMPARE(int(got.size()), 22);
            for (size_t b = 0; b < got.size(); ++b)
                for (int p = 0; p < 7; ++p)
                    QVERIFY2(got[b][size_t(p)] == want[b][size_t(p)],
                             qPrintable(QString("bar %1 part %2: %3, the song says %4")
                                            .arg(b).arg(p).arg(got[b][size_t(p)]).arg(want[b][size_t(p)])));
            QVERIFY2(opened.pageStatus(arrange).contains(QStringLiteral("song "), Qt::CaseInsensitive),
                     qPrintable(opened.pageStatus(arrange)));
        });
        QVERIFY(ran);
    }

    void aRecordedSongComesBackWithItsMutes() {
        // The recorded song of aRecordedSongReplaysBarByBarFromAnyPage: the
        // drums muted for two of its four bars. Opened again, P replays the
        // mute on the same bars of the song.
        bool ran = false;
        testkit::reopen(loops("key 15.3 O\nkey 18.6 Q\nkey 22.6 Q\nkey 23.3 O\nend 26\n"), 1.0,
                        [&](Session &, Session &opened, const QJsonObject &doc) {
                            ran = true;
                            const QJsonObject song = doc[QStringLiteral("song")].toObject();
                            QCOMPARE(song[QStringLiteral("bars")].toArray().size(), 4);
                            const QJsonArray mute =
                                song[QStringLiteral("channels")][QStringLiteral("Drums")][QStringLiteral("mute")].toArray();
                            QVERIFY2(mute.size() == 3, qPrintable(QString::number(mute.size())));
                            QCOMPARE(opened.saveSong()[QStringLiteral("song")].toObject(), song);
                            // Muted from about bar 1 + 1/16 of the song to bar 3 + 1/16 (18.6 and 22.6 after 16.5).
                            std::vector<std::pair<double, float>> mutes;
                            double origin = -1;
                            playOn(opened, 12.0, {}, [&](double, Session &s) {
                                const StudioView &v = s.view();
                                if (origin < 0 && v.beat - 4 * std::floor(v.beat / 4) >= 2.0) {
                                    press(s, Qt::Key_P);
                                    origin = (std::floor(v.beat / 4) + 1) * 4;
                                }
                                if (origin < 0)
                                    return;
                                const float m = v.parts[0].channel.value[Mute];
                                if (mutes.empty() || mutes.back().second != m)
                                    mutes.push_back({v.beat - origin, m});
                            });
                            QVERIFY2(mutes.size() >= 3, qPrintable(QString::number(mutes.size())));
                            QVERIFY2(std::fabs(mutes[1].first - 4.2) < 0.3, qPrintable(QString::number(mutes[1].first)));
                            QVERIFY2(std::fabs(mutes[2].first - 12.2) < 0.3, qPrintable(QString::number(mutes[2].first)));
                        });
        QVERIFY(ran);
    }

    void aSongFileOfNonsenseOpensSane() {
        // The loops, saved; the song in the file replaced by wrong types,
        // huge numbers, slots and steps out of range and parts that aren't
        // there. It opens, plays, and saves back only what made sense.
        QJsonObject doc;
        testkit::play(loops("end 16\n"), [&](Session &s) { doc = s.saveSong(); });
        QVERIFY(!doc.isEmpty());
        QJsonArray bars;
        bars.append(QJsonObject{{"label", 7},
                                {"join", "yes"},
                                {"play", QJsonObject{{"Drums", 12}, {"Bass", -3}, {"Vocals", 0}, {"Chords", 0.5}, {"Atmos", 0}}},
                                {"fromTop", QJsonArray{"Drums", "Nobody", 3, "Atmos"}}});
        bars.append("junk");
        bars.append(5);
        for (int i = 0; i < 400; ++i)
            bars.append(QJsonObject{{"label", "Drop"}, {"play", QJsonObject{{"Drums", 0}, {"Bass", 1e300}}}});
        QJsonObject song{
            {"tempo", 1e300},
            {"key", QJsonObject{{"root", -50}, {"family", 99}, {"brightness", "x"}}},
            {"bars", bars},
            {"channels", QJsonObject{{"Drums", QJsonObject{{"gain", QJsonArray{QJsonArray{0, 5}, QJsonArray{-4, 0.3},
                                                                               QJsonArray{1e12, 0.2}, "x", QJsonArray{3},
                                                                               QJsonArray{16, 0.4}}},
                                                           {"mute", "loud"}}},
                                     {"Nobody", QJsonObject{{"gain", QJsonArray{QJsonArray{0, 1}}}}},
                                     {"Bass", 4}}},
            {"tension", QJsonArray{QJsonArray{10, 99}, QJsonArray{0, -7}}},
            {"moves", QJsonArray{QJsonObject{{"at", 1e9}, {"move", "drop"}},
                                 QJsonObject{{"at", 16}, {"move", "build"}, {"bars", 1e9}},
                                 QJsonObject{{"at", 32}, {"move", "breakdown"}, {"parts", QJsonArray{"Drums", "Ghost"}}, {"bars", -4}},
                                 QJsonObject{{"at", 48}, {"move", "teleport"}},
                                 QJsonObject{{"at", "soon"}, {"move", "cut"}},
                                 QJsonObject{{"at", 8}, {"move", "lift"}, {"semitones", 400}},
                                 7}},
            {"scenes", QJsonArray{QJsonObject{{"scene", 12}, {"play", QJsonObject{{"Drums", 0}}}},
                                  QJsonObject{{"scene", 1}, {"play", QJsonObject{{"Drums", 99}, {"Bass", 0}}},
                                              {"stop", QJsonArray{"Chords", "Ghost"}}},
                                  "x"}},
            {"surprise", true}};
        doc[QStringLiteral("song")] = song;
        doc[QStringLiteral("page")] = QStringLiteral("Arrange");

        setScriptClock(100);
        Studio studio(48000.0f);
        studio.setTimeOrigin(scriptClock());
        Session s(&studio);
        s.setClock(&scriptClock);
        QString error;
        QVERIFY2(s.openSong(doc, &error), qPrintable(error));
        playOn(s, 1.0, {});
        const QJsonObject back = s.saveSong()[QStringLiteral("song")].toObject();
        const QJsonArray b = back[QStringLiteral("bars")].toArray();
        QCOMPARE(b.size(), 256);
        QCOMPARE(back[QStringLiteral("tempo")].toDouble(), 240.0);
        QCOMPARE(back[QStringLiteral("key")][QStringLiteral("root")].toInt(), 0);
        QVERIFY(back[QStringLiteral("key")][QStringLiteral("family")].toInt() < 99);
        QCOMPARE(b[0].toObject()[QStringLiteral("play")].toObject(), (QJsonObject{{"Atmos", 0}}));
        QCOMPARE(b[0].toObject()[QStringLiteral("fromTop")].toArray(), (QJsonArray{"Atmos"}));
        QVERIFY(!b[0].toObject().contains(QStringLiteral("label")));
        QCOMPARE(b[1].toObject(), QJsonObject{});
        QCOMPARE(b[3].toObject()[QStringLiteral("play")].toObject(), (QJsonObject{{"Drums", 0}}));
        const QJsonObject drums = back[QStringLiteral("channels")][QStringLiteral("Drums")].toObject();
        QCOMPARE(drums[QStringLiteral("gain")].toArray(), (QJsonArray{QJsonArray{0, 1}, QJsonArray{16, 0.4}}));
        QCOMPARE(drums[QStringLiteral("mute")].toArray(), (QJsonArray{QJsonArray{0, 0}}));
        QVERIFY(!back[QStringLiteral("channels")].toObject().contains(QStringLiteral("Nobody")));
        QCOMPARE(back[QStringLiteral("tension")].toArray(), (QJsonArray{QJsonArray{0, -1}, QJsonArray{10, 1}}));
        const QJsonArray moves = back[QStringLiteral("moves")].toArray();
        QCOMPARE(moves.size(), 3);
        QCOMPARE(moves[0].toObject(), (QJsonObject{{"at", 8}, {"move", "lift"}, {"semitones", 12}}));
        QCOMPARE(moves[1].toObject(), (QJsonObject{{"at", 16}, {"move", "build"}, {"bars", 32}}));
        QCOMPARE(moves[2].toObject(),
                 (QJsonObject{{"at", 32}, {"move", "breakdown"}, {"parts", QJsonArray{"Drums"}}, {"bars", 1}}));
        QCOMPARE(back[QStringLiteral("scenes")].toArray(),
                 (QJsonArray{QJsonObject{{"scene", 1}, {"play", QJsonObject{{"Bass", 0}}}, {"stop", QJsonArray{"Chords"}}}}));
        QVERIFY(!back.contains(QStringLiteral("surprise")));

        // It plays: P takes the song's tempo; F2 is the scene as far as it made sense.
        float peak = 0;
        playOn(s, 8.0, {{0.3, Qt::Key_P}}, [&](double, Session &x) { peak = std::max(peak, x.view().peak); });
        QVERIFY(std::isfinite(peak));
        QVERIFY2(std::fabs(s.view().bpm - 240) < 1e-6, qPrintable(QString::number(s.view().bpm)));
        QVERIFY(s.pageStatus(s.arrangePage()).contains(QStringLiteral("song "), Qt::CaseInsensitive));
        playOn(s, 3.0, {{0.1, Qt::Key_P}, {0.5, Qt::Key_F2}});
        QCOMPARE(s.view().parts[1].playing, 0);

        // A song that is nothing but the wrong types opens as no song at all.
        doc[QStringLiteral("song")] = QJsonObject{{"bars", "many"}, {"moves", 3}, {"scenes", QJsonObject{}}, {"tension", "high"}};
        QVERIFY(s.openSong(doc, &error));
        playOn(s, 1.0, {{0.3, Qt::Key_P}});
        QCOMPARE(s.saveSong()[QStringLiteral("song")].toObject(), QJsonObject{});
        QVERIFY(!s.pageStatus(s.arrangePage()).contains(QStringLiteral("song "), Qt::CaseInsensitive));
    }

    void debugSong() {
        const char *path = std::getenv("ARRANGE_DEBUG");
        if (!path)
            QSKIP("set ARRANGE_DEBUG to a script to print the studio bar by bar");
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        int lastBar = -1;
        watch(ss.str(), [&](double t, Session &s) {
            const StudioView &v = s.view();
            const int bar = int(std::floor(v.beat / 4));
            static QString lastNotice;
            if (bar == lastBar && s.notice(nullptr) == lastNotice)
                return;
            lastBar = bar;
            lastNotice = s.notice(nullptr);
            std::string line;
            for (int p = 0; p < v.partCount; ++p) {
                const auto &pv = v.parts[p];
                line += std::to_string(pv.playing) + (pv.channel.value[Mute] > 0.5f ? "m" : "") + "[";
                for (const auto &c : pv.clips)
                    line += c.state == ClipView::Empty ? "." : "#";
                line += "] ";
            }
            std::printf("t=%7.2f bar=%4d %s mf=%.2f | %s | %s\n", t, bar, line.c_str(), v.master[MasterFilter],
                        s.pageStatus().toStdString().c_str(), s.notice(nullptr).toStdString().c_str());
        });
    }
};

TESTKIT_REGISTER(ArrangeTests)
#include "arrange_tests.moc"
