// The Chords page: harmony in key, voice leading, the chord the others
// follow, colours, gates on the beat, loops that keep driving the harmony,
// keys, hold, and the low end left to the bass.

#include <QJsonArray>
#include <array>
#include <complex>
#include <functional>
#include <set>

#include "testkit.h"

using namespace tf;

namespace {

constexpr int kChords = 2; // the part's index: page 3

// Where a finger lands for degree d (of seven) in colour row r.
std::string cell(int d, int r) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f %.4f", (d + 0.5) / 7.0, (r + 0.5) * 0.16);
    return buf;
}
enum { Power = 0, Sus, Triad, Add9, Seventh };

std::string num(double v) { return std::to_string(v); }

// Like testkit::play, but calls watch with the studio's view after every
// frame, so a test can follow the harmony as it happens.
testkit::Take playWatching(const std::string &script, const std::function<void(double, const StudioView &)> &watch) {
    testkit::Take take;
    ScriptPlayer player;
    if (!player.load(script, &take.error))
        return take;
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
            take.left.push_back(buf[2 * i]);
            take.right.push_back(buf[2 * i + 1]);
        }
        session.refresh();
        watch(t, session.view());
    }
    take.view = session.view();
    return take;
}

// The published voicing of a lane (0 live, 1 loop).
std::vector<float> voicing(const StudioView &v, int lane = 0) {
    const float *d = v.parts[kChords].data + 16 * lane;
    std::vector<float> out;
    if (d[0] < 0)
        return out;
    for (int i = 0; i < int(d[10]) && i < 6; ++i)
        out.push_back(d[4 + i]);
    return out;
}

// The share of a stretch's energy between lo and hi Hz, from one long FFT
// (or, with absolute, the energy itself).
double bandShare(const testkit::Take &take, double from, double lo, double hi, bool absolute = false) {
    const size_t a = size_t(from * 48000), n = 1 << 15;
    if (a + n > take.left.size())
        return 1;
    std::vector<std::complex<double>> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n - 1));
        x[i] = 0.5 * (take.left[a + i] + take.right[a + i]) * w;
    }
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2 * M_PI / double(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w = 1;
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    double in = 0, all = 0;
    for (size_t k = 1; k < n / 2; ++k) {
        const double f = double(k) * 48000.0 / double(n), e = std::norm(x[k]);
        all += e;
        if (f >= lo && f < hi)
            in += e;
    }
    if (absolute)
        return in;
    return all > 0 ? in / all : 1;
}

// A loop made the deliberate way: chords one beat each from the first bar
// line, then Space where the phrase starts again (the next ONE).
constexpr double kBeat = 60.0 / 124.0, kBar = 4 * kBeat;
std::string recordedLoop(std::initializer_list<int> degrees, int row = Triad, const std::string &before = {}) {
    std::string s = "page 0 3\n" + before;
    double t = kBar;
    int id = 60;
    for (int d : degrees) {
        s += "down " + num(t + 0.01) + " " + std::to_string(id) + " " + cell(d, row) + "\n";
        s += "up " + num(t + kBeat - 0.03) + " " + std::to_string(id) + "\n";
        t += kBeat;
        ++id;
    }
    s += "key " + num(2 * kBar + 0.02) + " Space\n";
    return s;
}

// Where a finger lands on the rhythm rail for pattern cell k.
std::string rail(int k, float y = 0.9f) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f %.3f", (k + 0.5) / 8.0, y);
    return buf;
}
enum { Hold = 0, Offbeat, Eighths, Tresillo, Charleston, TranceGate, Sixteenths, Wub };

// How unevenly a stretch's level moves from one sixteenth to the next: near
// zero for a held chord, large for a gated one.
double choppiness(const testkit::Take &take, double from, double to, double bpm) {
    const double step = 60.0 / bpm / 8.0; // half a sixteenth
    double lo = 1e9, hi = 0;
    for (double t = from; t + step < to; t += step) {
        const double r = take.rms(t, t + step);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    return hi / std::max(lo, 1e-6);
}

// Pitch classes of A natural minor, the studio's default key.
const std::set<int> kAMinor = {9, 11, 0, 2, 4, 5, 7};

}

class ChordsTests : public QObject {
    Q_OBJECT
private slots:
    void aLayeredChordLeavesTheLoopsChordSounding() {
        // A pad held on the key's chord all bar is looped; then piano stabs on
        // the sixth degree, on beats 1 and 3, are layered on it. The stabs
        // sound over the pad, not instead of it: no beat after the layer goes
        // quiet, and between stabs the chord of the moment (what Bass, Arp and
        // Lead follow) is the pad's again.
        const std::string script = "page 0.05 3\nkey 0.1 Ctrl+M\n"
                                   "key 1.9355 A down\nkey 3.8952 Space\nkey 4.1129 A up\n"
                                   "key 4.8387 Right\nkey 4.8871 Right\nkey 4.9355 Right\n"
                                   "key 6.2903 H down\nkey 6.4839 H up\nkey 7.2581 H down\nkey 7.4516 H up\n"
                                   "key 7.7661 Space\nend 13.6\n";
        int onTonic = 0, samples = 0;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            if (t > 9.7 && t < 11.6) {
                ++samples;
                onTonic += s.view().chordRoot == 0;
            }
        });
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.parts[2].playing, 0);
        QVERIFY(take.view.parts[2].clips[1].state == ClipView::Empty);
        const double spb = 60.0 / 124.0;
        const double before = take.rms(8 * spb, 16 * spb);
        double quietest = 1;
        for (int beat = 18; beat < 26; ++beat)
            quietest = std::min(quietest, take.rms(beat * spb, (beat + 1) * spb));
        QVERIFY2(quietest > before * 0.5, qPrintable(QStringLiteral("a beat at %1 dB of %2 dB")
                                                         .arg(20 * std::log10(quietest), 0, 'f', 1)
                                                         .arg(20 * std::log10(before), 0, 'f', 1)));
        QVERIFY2(onTonic > samples / 3, qPrintable(QStringLiteral("on the tonic %1 of %2").arg(onTonic).arg(samples)));
    }

    void fingersPlayAndStayFinite() {
        const auto take = testkit::play("page 0 3\n"
                                        "tap 0.2 0.2 0.5\ntap 0.5 0.4 0.6\ndown 0.8 1 0.6 0.4\n"
                                        "glide 1.0 1.6 1 0.7 0.7\nup 1.8 1\nend 2.5\n");
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
        QVERIFY(take.rms(0.3, 1.8) > 0.01);
    }

    // One finger in the VI column (F in A minor) is a whole chord, and the
    // studio hears it as the chord of the moment.
    void aFingerTellsTheStudioItsChord() {
        const auto take = testkit::play("page 0 3\ndown 0.2 1 " + cell(5, Triad) + "\nend 0.8\n");
        QCOMPARE(take.view.chordRoot, 5);
        QCOMPARE(take.view.chordCount, 3);
        QCOMPARE(take.view.chordTones[0], 5);
        QCOMPARE(take.view.chordTones[1], 7);
        QCOMPARE(take.view.chordTones[2], 9);
        QVERIFY(voicing(take.view).size() >= 3);
    }

    // The rows are colours: the top row is the seventh, the next add9, the
    // bottom a power chord.
    void rowsAreColours() {
        const auto seventh = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Seventh) + "\nend 0.6\n");
        QCOMPARE(seventh.view.chordCount, 4);
        QCOMPARE(seventh.view.chordTones[3], 6); // Am7: A C E G
        const auto add9 = testkit::play("page 0 3\ndown 0.2 1 " + cell(3, Add9) + "\nend 0.6\n");
        QCOMPARE(add9.view.chordCount, 4);
        QCOMPARE(add9.view.chordTones[3], 3 + 8); // Dm(add9): D F A E
        const auto power = testkit::play("page 0 3\ndown 0.2 1 " + cell(6, Power) + "\nend 0.6\n");
        QCOMPARE(power.view.chordTones[1], 6 + 4); // G5: G D G
        QCOMPARE(power.view.chordTones[2], 6 + 7);
        // The diminished degree never sounds diminished: B D F becomes B D A.
        const auto dim = testkit::play("page 0 3\ndown 0.2 1 " + cell(1, Triad) + "\nend 0.6\n");
        QCOMPARE(dim.view.chordTones[2], 1 + 6);
    }

    // Sliding a held finger up a row changes its colour while it sounds,
    // and the studio follows.
    void slidingChangesColour() {
        const auto sus = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Sus) + "\nend 0.5\n");
        QCOMPARE(sus.view.chordTones[1], 3); // Asus4: A D E
        const auto take = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Sus) + "\nglide 0.6 0.9 1 " +
                                        cell(0, Triad) + "\nend 1.3\n");
        QCOMPARE(take.view.chordCount, 3);
        QCOMPARE(take.view.chordTones[1], 2); // resolved to the third, C
    }

    // Every note a progression sounds is in the key, above the bass's
    // register, and each chord moves smoothly from the last: the top voice
    // steps, and no voice leaps.
    void progressionsVoiceLeadInKey() {
        std::string s = "page 0 3\n";
        double t = 0.2;
        int id = 1;
        for (int d : {0, 5, 2, 6, 3, 0, 4, 0}) {
            s += "down " + num(t) + " " + std::to_string(id) + " " + cell(d, Triad) + "\n";
            s += "up " + num(t + 0.9) + " " + std::to_string(id) + "\n";
            t += 1.0;
            ++id;
        }
        s += "end " + num(t) + "\n";
        std::vector<std::vector<float>> chords;
        int lastRoot = -1;
        playWatching(s, [&](double, const StudioView &v) {
            const auto notes = voicing(v);
            if (!notes.empty() && int(v.parts[kChords].data[0]) != lastRoot) {
                lastRoot = int(v.parts[kChords].data[0]);
                chords.push_back(notes);
            }
        });
        QVERIFY2(chords.size() == 8, qPrintable(QString::number(chords.size())));
        for (size_t c = 0; c < chords.size(); ++c) {
            QString text;
            for (float n : chords[c]) {
                text += QString::number(n) + " ";
                QVERIFY2(kAMinor.count(int(std::lround(n)) % 12), qPrintable(QString("note %1").arg(n)));
                QVERIFY2(n >= 47.5f, qPrintable(QString("note %1 is the bass's").arg(n)));
            }
            qInfo("chord %d: %s", int(c), qPrintable(text));
            if (c == 0)
                continue;
            const auto &a = chords[c - 1], &b = chords[c];
            QVERIFY2(std::fabs(a.back() - b.back()) <= 5.0f,
                     qPrintable(QString("top voice leaps %1 → %2").arg(a.back()).arg(b.back())));
            for (float n : b) {
                float nearest = 1e9f;
                for (float m : a)
                    nearest = std::min(nearest, std::fabs(n - m));
                QVERIFY2(nearest <= 5.0f, qPrintable(QString("a voice leaps to %1").arg(n)));
            }
        }
    }

    // Play a progression, press Space: it loops, and as it replays it keeps
    // telling the studio which chord is playing.
    void aLoopKeepsDrivingTheHarmony() {
        std::set<int> rootsAfter;
        double lastChange = 0;
        int last = -1;
        const auto take = playWatching(recordedLoop({0, 5, 2, 6}) + "end 9\n", [&](double now, const StudioView &v) {
            if (now > 4.0) {
                rootsAfter.insert(v.chordRoot);
                if (v.chordRoot != last)
                    lastChange = now;
            }
            last = v.chordRoot;
        });
        QCOMPARE(take.view.parts[kChords].playing, 0);
        QVERIFY2(rootsAfter.size() == 4, qPrintable(QString::number(rootsAfter.size())));
        QVERIFY(lastChange > 7.0); // still changing near the end, long after the fingers left
        QVERIFY(take.rms(6, 9) > 0.005);
    }

    // A chord held live wins over the loop's for as long as it is held.
    void aLiveChordWinsOverTheLoop() {
        bool heldOnly = true;
        playWatching(recordedLoop({0, 5, 2, 6}) + "down 5.0 20 " + cell(4, Seventh) + "\nup 7.0 20\nend 8\n",
                     [&](double now, const StudioView &v) {
                         if (now > 5.1 && now < 6.9)
                             heldOnly = heldOnly && v.chordRoot == 4 && v.chordCount == 4;
                     });
        QVERIFY(heldOnly);
    }

    // The keyboard plays the same chords: A is I, H is VI, Shift makes a
    // seventh, the Z row is sus.
    void keysPlayChords() {
        const auto h = testkit::play("page 0 3\nkey 0.2 H down\nend 0.5\n");
        QCOMPARE(h.view.chordRoot, 5);
        QCOMPARE(h.view.chordCount, 3);
        const auto seventh = testkit::play("page 0 3\nkey 0.2 Shift+G down\nend 0.5\n");
        QCOMPARE(seventh.view.chordRoot, 4);
        QCOMPARE(seventh.view.chordCount, 4);
        const auto sus = testkit::play("page 0 3\nkey 0.2 X down\nend 0.5\n");
        QCOMPARE(sus.view.chordRoot, 1);
        QVERIFY(sus.rms(0.25, 0.5) > 0.003);
    }

    // P loops the last bars played, however they were played: four chords
    // held a bar each become a four-bar loop.
    void pLoopsTheLastBars() {
        const double bar = 4 * 60.0 / 124.0;
        std::string s = "page 0 3\n";
        const char *keys[] = {"A", "F", "D", "G"};
        for (int i = 0; i < 4; ++i) {
            const double on = 0.02 + i * bar, off = on + bar - 0.05;
            s += "key " + num(on) + " " + keys[i] + " down\n";
            s += "key " + num(off) + " " + keys[i] + " up\n";
        }
        s += "key " + num(4 * bar - 0.3) + " P\nend " + num(9 * bar) + "\n";
        std::set<int> roots;
        const auto take = playWatching(s, [&](double now, const StudioView &v) {
            if (now > 4 * bar + 0.2)
                roots.insert(v.chordRoot);
        });
        QCOMPARE(take.view.parts[kChords].playing, 0);
        QCOMPARE(int(take.view.parts[kChords].clips[0].lengthBeats), 16);
        QVERIFY2(roots.size() == 4, qPrintable(QString::number(roots.size())));
        QVERIFY(take.rms(6 * bar, 8 * bar) > 0.005);
    }

    // A trance gate chops a held chord on the sixteenths of its pattern: the
    // hits loud and the rests quiet, in time with the beat.
    void theGateChopsOnTheBeat() {
        // The trance sound (one step right) comes with its gate.
        const auto take =
            testkit::play("page 0 3\nkey 0.05 Right\ndown 0.3 1 " + cell(0, Triad) + "\nup 4.2 1\nend 4.4\n");
        const char *hits = "X.xx.xx.X.xx.xx.";
        const double step = 60.0 / 124.0 / 4.0;
        double on = 0, off = 0;
        int nOn = 0, nOff = 0;
        for (int k = 8; k < 60; ++k) {
            const double e = take.rms(k * step + step * 0.55, k * step + step * 0.95);
            if (hits[k % 16] == '.')
                off += e, ++nOff;
            else
                on += e, ++nOn;
        }
        on /= nOn;
        off /= nOff;
        QVERIFY2(on > 2.0 * off, qPrintable(QString("on %1 off %2").arg(on).arg(off)));
    }

    // The organ stabs the offbeats from one held finger.
    void organStabsTheOffbeat() {
        const auto take = testkit::play("page 0 3\nkey 0.05 Right\nkey 0.1 Right\ndown 0.3 1 " + cell(0, Triad) +
                                        "\nup 4.2 1\nend 4.4\n");
        const double beat = 60.0 / 124.0;
        double onbeat = 0, offbeat = 0;
        for (int b = 2; b < 8; ++b) {
            onbeat += take.rms(b * beat + 0.01, b * beat + 0.08);
            offbeat += take.rms(b * beat + beat / 2 + 0.01, b * beat + beat / 2 + 0.08);
        }
        QVERIFY2(offbeat > 1.8 * onbeat, qPrintable(QString("off %1 on %2").arg(offbeat).arg(onbeat)));
    }

    // Chords leave the sub to the bass: almost nothing under 80 Hz, with
    // the biggest voicing (a flat finger) on every sound.
    void theLowEndIsLeftToTheBass() {
        std::string step;
        for (int s = 0; s < 6; ++s) {
            const auto take = testkit::play("page 0 3\n" + step + "down 0.3 1 " + cell(5, Seventh) +
                                            " size=2\nup 1.4 1\nend 1.5\n");
            const double sub = bandShare(take, 0.6, 20, 80);
            QVERIFY2(sub < 0.01, qPrintable(QString("sound %1: sub share %2").arg(s).arg(sub)));
            step += "key " + num(0.05 + 0.02 * s) + " Right\n";
        }
    }

    // L holds: the chord rings after the finger lifts, until the next one
    // replaces it.
    void holdKeepsTheChord() {
        const auto held = testkit::play("page 0 3\nkey 0.1 L\ndown 0.2 1 " + cell(0, Triad) + "\nup 0.6 1\nend 2.5\n");
        const auto loose = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Triad) + "\nup 0.6 1\nend 2.5\n");
        QVERIFY(held.rms(2.0, 2.5) > 0.01);
        QVERIFY(loose.rms(2.0, 2.5) < 0.3 * held.rms(2.0, 2.5));
    }

    // Leaving the page ends what the fingers hold.
    void leavingEndsTheChord() {
        const auto take = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Triad) + "\npage 0.8 1\nend 4.5\n");
        QVERIFY(take.rms(0.4, 0.8) > 0.01);
        QVERIFY(take.rms(4.0, 4.5) < 0.1 * take.rms(0.4, 0.8));
    }

    // A loop follows the key: the same degrees, in the new key.
    void aLoopFollowsTheKey() {
        std::vector<float> before, after;
        playWatching(recordedLoop({0, 0, 0, 0}) + "key 5.0 Ctrl+Right\nend 7\n", [&](double now, const StudioView &v) {
            const auto n = voicing(v, 1);
            if (now > 4.0 && now < 4.9 && !n.empty())
                before = n;
            if (now > 6.0 && !n.empty())
                after = n;
        });
        QVERIFY(!before.empty() && !after.empty());
        std::set<int> pcs;
        for (float n : after)
            pcs.insert(int(std::lround(n)) % 12);
        QVERIFY(pcs.count(4) && pcs.count(7) && pcs.count(11)); // A minor became E minor: E G B
    }

    // Holding a cell of the rail plays its rhythm over everything, a loop
    // included, for as long as the finger stays; lifting gives the loop
    // back its own (here none: a sustained pad).
    void holdingTheRailGatesTheLoop() {
        const std::string s = recordedLoop({0, 5, 2, 6}, Seventh) + "down 5.0 30 " + rail(Offbeat) +
                              "\nup 7.0 30\nend 9.5\n";
        const auto take = testkit::play(s);
        QCOMPARE(take.view.parts[kChords].playing, 0);
        const double gated = choppiness(take, 5.3, 6.9, 124), free = choppiness(take, 7.4, 8.4, 124);
        QVERIFY2(gated > 3.0 * free, qPrintable(QString("gated %1 free %2").arg(gated).arg(free)));
    }

    // Tapping a cell keeps it as this sound's rhythm; a second finger on the
    // rail doubles the gate's speed, a third quadruples it.
    void theRailLatchesAndBuilds() {
        int latched = -1, rate2 = 0, rate3 = 0, rateAfter = 0;
        playWatching("page 0 3\ntap 0.2 " + rail(Offbeat) + "\ndown 1.0 1 " + rail(Sixteenths) + "\ndown 1.2 2 " +
                         rail(Sixteenths) + "\ndown 1.6 3 " + rail(Sixteenths) + "\nup 2.0 1\nup 2.0 2\nup 2.0 3\nend 2.3\n",
                     [&](double now, const StudioView &v) {
                         const float *d = v.parts[kChords].data;
                         if (now > 0.6 && now < 0.9)
                             latched = int(d[40 + 0]); // the pad's pattern
                         if (now > 1.4 && now < 1.5)
                             rate2 = int(d[46]);
                         if (now > 1.8 && now < 1.9)
                             rate3 = int(d[46]);
                         if (now > 2.2)
                             rateAfter = int(d[46]);
                     });
        QCOMPARE(latched, int(Offbeat));
        QCOMPARE(rate2, 2);
        QCOMPARE(rate3, 4);
        QCOMPARE(rateAfter, 1);
    }

    // A loop keeps the sound it was played with: an organ loop stays an
    // organ while the pad is played over it.
    void aLoopKeepsItsSound() {
        std::string s = recordedLoop({0, 5, 2, 6}, Triad, "key 0.1 Right\nkey 0.15 Right\n") +
                        "key 4.1 Left\nkey 4.15 Left\ndown 4.3 40 " + cell(3, Triad) + "\nend 5\n";
        int loopStyle = -1, liveStyle = -1;
        playWatching(s, [&](double now, const StudioView &v) {
            const float *d = v.parts[kChords].data;
            if (now > 4.5 && d[16] >= 0)
                loopStyle = int(d[16 + 2]);
            if (now > 4.5 && d[0] >= 0)
                liveStyle = int(d[2]);
        });
        QCOMPARE(loopStyle, 2); // the organ
        QCOMPARE(liveStyle, 0); // the pad
    }

    // Up raises the voicing: the same chord, its notes higher.
    void upRaisesTheVoicing() {
        std::vector<float> low, high;
        playWatching("page 0 3\ndown 0.2 1 " + cell(0, Triad) + "\nkey 0.6 Up\nkey 0.7 Up\nend 1.2\n",
                     [&](double now, const StudioView &v) {
                         if (now > 0.4 && now < 0.55)
                             low = voicing(v);
                         if (now > 1.0)
                             high = voicing(v);
                     });
        QVERIFY(!low.empty() && !high.empty());
        double a = 0, b = 0;
        for (float n : low)
            a += n / low.size();
        for (float n : high)
            b += n / high.size();
        QVERIFY2(b > a + 3.0, qPrintable(QString("%1 → %2").arg(a).arg(b)));
    }

    // A flat finger makes the chord bigger: the root an octave under and the
    // top an octave over join it.
    void aFlatFingerDoublesTheChord() {
        size_t tip = 0, flat = 0;
        playWatching("page 0 3\nkey 0.05 Right\ndown 0.2 1 " + cell(0, Triad) + " size=1\nglide 0.6 0.8 1 " +
                         cell(0, Triad) + " size=2\nend 1.4\n",
                     [&](double now, const StudioView &v) {
                         if (now > 0.4 && now < 0.55)
                             tip = voicing(v).size();
                         if (now > 1.2)
                             flat = voicing(v).size();
                     });
        QCOMPARE(int(tip), 3);
        QVERIFY2(flat >= 4, qPrintable(QString::number(flat)));
    }

    // The chords follow the mode: in A Dorian the IV chord is D major.
    void chordsFollowTheMode() {
        std::vector<float> notes;
        playWatching("page 0 3\nkey 0.1 Ctrl+]\ndown 0.3 1 " + cell(3, Triad) + "\nend 0.8\n",
                     [&](double now, const StudioView &v) {
                         if (now > 0.6)
                             notes = voicing(v);
                     });
        std::set<int> pcs;
        for (float n : notes)
            pcs.insert(int(std::lround(n)) % 12);
        QVERIFY(pcs.count(2) && pcs.count(6) && pcs.count(9)); // D F♯ A
    }

    // Ordinary playing never makes a loop by itself: tapping chords in time
    // and then holding one on the next beat is just playing.
    void playingNeverLoopsByItself() {
        std::string s = "page 0 3\n";
        double t = 0.5;
        for (int d : {0, 5, 2, 6, 0, 5}) {
            s += "tap " + num(t) + " " + cell(d, Triad) + " len=0.12\n";
            t += 0.5;
        }
        s += "down " + num(t) + " 9 " + cell(0, Triad) + "\nup " + num(t + 1.5) + " 9\nend " + num(t + 2) + "\n";
        const auto take = testkit::play(s);
        for (int c = 0; c < 8; ++c)
            QCOMPARE(int(take.view.parts[kChords].clips[c].state), int(ClipView::Empty));
    }

    // Space over a playing loop takes what was just played at once, and the
    // page says what landed; P counts down to its bar line.
    void theLoopIsAnnounced() {
        QString landed, counting;
        const std::string over = recordedLoop({0, 5, 2, 6}) + "down " + num(3 * kBar + 0.01) + " 70 " + cell(3, Triad) +
                                 "\nup " + num(4 * kBar - 0.05) + " 70\nkey " + num(4 * kBar + 0.02) + " Shift+Space\nend " +
                                 num(4 * kBar + 0.5) + "\n";
        const auto take = testkit::play(over, [&](Session &s) { landed = s.pageStatus(); });
        QVERIFY2(landed.contains("looped") && landed.contains("slot 2"), qPrintable(landed));
        QCOMPARE(take.view.parts[kChords].playing, 1);
        testkit::play(recordedLoop({0, 5, 2, 6}) + "down " + num(3 * kBar + 0.05) + " 71 " + cell(4, Triad) + "\nup " +
                          num(3 * kBar + kBeat) + " 71\nkey " + num(3 * kBar + 2 * kBeat) + " P\nend " +
                          num(3 * kBar + 2.5 * kBeat) + "\n",
                      [&](Session &s) { counting = s.pageStatus(); });
        QVERIFY2(counting.contains("looping the last") && counting.contains("p calls it off"), qPrintable(counting));
    }

    // Over a running clock, a progression played twice and then Space loops
    // one cycle of it, landing at once, with no gap before its first chord.
    void aProgressionPlayedTwiceLoopsOnce() {
        std::string s = "page 0 3\nkey 0.02 Ctrl+M\n";
        const int degrees[] = {0, 5, 2, 6};
        int id = 1;
        for (int pass = 0; pass < 2; ++pass)
            for (int b = 0; b < 4; ++b) {
                const double t = (1 + pass * 4 + b) * kBar;
                s += "down " + num(t + 0.01) + " " + std::to_string(id) + " " + cell(degrees[b], Triad) + "\nup " +
                     num(t + kBar - 0.05) + " " + std::to_string(id) + "\n";
                ++id;
            }
        const double space = 9 * kBar + 0.02;
        s += "key " + num(space) + " Space\nkey " + num(space + 0.05) + " Ctrl+M\nend " + num(space + 8 * kBar) + "\n";
        std::vector<std::pair<double, int>> roots;
        const auto take = playWatching(s, [&](double now, const StudioView &v) { roots.push_back({now, v.chordRoot}); });
        QCOMPARE(take.view.parts[kChords].playing, 0);
        QCOMPARE(int(take.view.parts[kChords].clips[0].lengthBeats), 16);
        // It carries straight on: the first chord again, sounding at once.
        int after = -1;
        for (auto &[t, r] : roots)
            if (after < 0 && t > space + 0.15)
                after = r;
        QCOMPARE(after, 0);
        for (int b = 9; b < 17; ++b)
            QVERIFY2(take.rms(b * kBar, (b + 1) * kBar) > 0.02, qPrintable(QString("bar %1 is quiet").arg(b)));
    }

    // A chord landed a hair early for its bar belongs to it: P keeps
    // all four chords of a progression played 20 ms ahead of each bar.
    void earlyChordsStayInTheLoop() {
        std::string s = "tempo 0 128\npage 0.01 3\n";
        const double bar = 240.0 / 128.0;
        for (int b = 0; b < 4; ++b) {
            const int d = std::array<int, 4>{0, 5, 2, 6}[size_t(b)];
            s += "down " + num((b + 1) * bar - 0.02) + " " + std::to_string(b + 1) + " " + cell(d, Triad) + "\n";
            s += "up " + num((b + 2) * bar - 0.08) + " " + std::to_string(b + 1) + "\n";
        }
        s += "key " + num(5 * bar + 0.25) + " P\nend " + num(10 * bar) + "\n";
        std::set<int> roots;
        const auto take = playWatching(s, [&](double now, const StudioView &v) {
            if (now > 5 * bar + 0.3)
                roots.insert(v.chordRoot);
        });
        QCOMPARE(take.view.parts[kChords].clips[0].events, 8);
        QCOMPARE(int(roots.size()), 4);
        for (int b = 6; b < 10; ++b)
            QVERIFY2(take.rms(b * bar, (b + 1) * bar) > 0.01, qPrintable(QString("bar %1 is silent").arg(b)));
    }

    // A different chord held over a loop: the loop steps back, so one
    // harmony sounds; the same chord leaves it whole.
    void aDifferentLiveChordDucksTheLoop() {
        const std::string loop = recordedLoop({0, 0, 0, 0});
        const auto alone = testkit::play(loop + "end 6\n");
        const auto same = testkit::play(loop + "down 4.3 80 " + cell(0, Triad) + " p=0.01\nend 6\n");
        const auto other = testkit::play(loop + "down 4.3 80 " + cell(5, Triad) + " p=0.01\nend 6\n");
        QVERIFY(alone.rms(5, 6) > 0.01);
        // With its own chord in the same column, everything stays.
        QVERIFY(same.rms(5, 6) > alone.rms(5, 6));
        // Another chord: the A minor loop's E steps back under the F.
        const double eAlone = bandShare(alone, 5.0, 325, 335), eOther = bandShare(other, 5.0, 325, 335);
        const double fOther = bandShare(other, 5.0, 344, 354);
        QVERIFY2(eOther < 0.1 * fOther, qPrintable(QString("E %1 F %2 (alone %3)").arg(eOther).arg(fOther).arg(eAlone)));
    }

    // The organ and the piano keep their weight in mono.
    void stabsSurviveMono() {
        for (const char *keys : {"key 0.05 Right\nkey 0.06 Right\n", "key 0.05 Right\nkey 0.06 Right\nkey 0.07 Right\n"}) {
            const auto take = testkit::play(std::string("page 0 3\n") + keys + "down 0.2 1 " + cell(0, Seventh) +
                                            "\nup 3.8 1\nend 4\n");
            double lr = 0, ll = 0, rr = 0, mono = 0, stereo = 0;
            for (size_t i = size_t(0.3 * 48000); i < size_t(3.8 * 48000); ++i) {
                const double l = take.left[i], r = take.right[i];
                lr += l * r, ll += l * l, rr += r * r;
                mono += 0.25 * (l + r) * (l + r);
                stereo += 0.5 * (l * l + r * r);
            }
            const double corr = lr / std::sqrt(ll * rr), loss = 10 * std::log10(mono / stereo);
            QVERIFY2(corr > 0.5 && loss > -1.5, qPrintable(QString("corr %1 loss %2 dB").arg(corr).arg(loss)));
        }
    }

    // A stab that lands between its gate's hits waits for the next one: the
    // offbeat organ never stabs the downbeat a chord lands on.
    void stabsWaitForTheirHit() {
        std::string s = "page 0 3\nkey 0.05 Right\nkey 0.06 Right\n";
        for (int b = 1; b < 5; ++b)
            s += "down " + num(b * kBar + 0.005) + " " + std::to_string(b) + " " + cell(b % 2 ? 0 : 5, Triad) + "\nup " +
                 num((b + 1) * kBar - 0.01) + " " + std::to_string(b) + "\n";
        s += "end " + num(5 * kBar) + "\n";
        const auto take = testkit::play(s);
        double down = 0, off = 0;
        for (int b = 1; b < 5; ++b) {
            down += take.rms(b * kBar + 0.005, b * kBar + 0.09);
            off += take.rms(b * kBar + 2 * kBeat / 4 + 0.005, b * kBar + 2 * kBeat / 4 + 0.09);
        }
        QVERIFY2(down < 0.2 * off, qPrintable(QString("downbeat %1 offbeat %2").arg(down).arg(off)));
    }

    // A stab a little late for its hit still strikes at once: a key 60 or
    // 90 ms after the organ's offbeat sounds within 10 ms of it.
    void lateStabsStrikeAtOnce() {
        for (double late : {0.0, 0.03, 0.06, 0.09}) {
            const double key = 2 * kBar + 2 * kBeat / 4 + late; // the offbeat of bar 3's first beat, late
            const auto take = testkit::play("page 0 3\nkey 0.05 Right\nkey 0.06 Right\nkey " + num(key) +
                                            " A down\nkey " + num(key + 0.3) + " A up\nend " + num(key + 0.6) + "\n");
            double onset = -1;
            for (size_t i = size_t((key - 0.05) * 48000); i < take.left.size(); ++i)
                if (std::fabs(take.left[i]) + std::fabs(take.right[i]) > 0.02) {
                    onset = double(i) / 48000.0;
                    break;
                }
            QVERIFY2(onset >= 0 && onset - key < 0.012 + 1.0 / 125.0,
                     qPrintable(QString("key %1 ms late sounds %2 ms after it").arg(late * 1000).arg((onset - key) * 1000)));
        }
    }

    // Layering the loop's own chords a little late or a little early never
    // ducks the loop on its downbeats.
    void aLooseLayerKeepsTheLoopWhole_data() {
        QTest::addColumn<double>("offset");
        QTest::newRow("45 ms late") << 0.045;
        QTest::newRow("50 ms early") << -0.05;
    }
    void aLooseLayerKeepsTheLoopWhole() {
        QFETCH(double, offset);
        std::string loop = recordedLoop({0, 5, 2, 6}, Triad, "key 0.05 Right\nkey 0.06 Right\nkey 0.07 Right\nkey 0.08 Right\n");
        std::string layer;
        int id = 90;
        for (int bar = 3; bar < 6; ++bar)
            for (int b = 0; b < 4; ++b) {
                const double t = bar * kBar + b * kBeat + offset;
                layer += "down " + num(t) + " " + std::to_string(id) + " " + cell(std::array<int, 4>{0, 5, 2, 6}[size_t(b)], Triad) +
                         "\nup " + num(t + kBeat - 0.01) + " " + std::to_string(id) + "\n";
                ++id;
            }
        const auto alone = testkit::play(loop + "end " + num(6 * kBar) + "\n");
        const auto layered = testkit::play(loop + layer + "end " + num(6 * kBar) + "\n");
        double a = 0, l = 0;
        for (int bar = 3; bar < 6; ++bar)
            for (int b = 0; b < 4; ++b) {
                const double t = bar * kBar + b * kBeat;
                a += alone.rms(t, t + 0.06);
                l += layered.rms(t, t + 0.06);
            }
        QVERIFY2(l >= a, qPrintable(QString("layered %1 alone %2").arg(l).arg(a)));
    }

    // A new chord played over a loop, landing as the loop moves on, is the
    // newcomer: the loop steps back at once rather than rub for half a beat.
    void aNewChordOnTheLoopsBeatDucksAtOnce() {
        const double t = 4 * kBar; // a loop chord lands here too
        const std::string loop = recordedLoop({0, 0, 0, 0});
        const auto alone = testkit::play(loop + "end " + num(t + 1.0) + "\n");
        const auto over = testkit::play(loop + "down " + num(t) + " 80 " + cell(5, Triad) + " p=0.01\nend " + num(t + 1.0) + "\n");
        // The loop's E (A minor's fifth, not in F major) from 60 ms on.
        const double e0 = bandShare(alone, t + 0.06, 325, 335, true), e1 = bandShare(over, t + 0.06, 325, 335, true);
        QVERIFY2(e1 < 0.1 * e0, qPrintable(QString("E alone %1, under the new chord %2").arg(e0).arg(e1)));
    }

    // Once its finger lifts, a live chord stops being the page's chord: the
    // loop's shows again at once.
    void aLiftedChordGivesWayToTheLoop() {
        int shown = -2;
        playWatching(recordedLoop({0, 0, 0, 0}) + "down " + num(3 * kBar) + " 80 " + cell(6, Triad) + "\nup " +
                         num(3 * kBar + 0.5) + " 80\nend " + num(3 * kBar + 0.8) + "\n",
                     [&](double now, const StudioView &v) {
                         if (now > 3 * kBar + 0.6) {
                             const float *d = v.parts[kChords].data;
                             shown = d[0] >= 0 ? 6 : int(d[16]);
                         }
                     });
        QCOMPARE(shown, 0);
    }

    // P pressed just after the bar line: the loop has already begun, and
    // its first chord sounds at once, so no bar goes by without it.
    void lateLoopSoundsAtOnce() {
        std::string s = "page 0 3\n";
        for (int b = 1; b < 5; ++b)
            s += "down " + num(b * kBar + 0.01) + " " + std::to_string(b) + " " +
                 cell(std::array<int, 5>{0, 0, 5, 2, 6}[size_t(b)], Triad) + "\nup " + num((b + 1) * kBar - 0.15) + " " +
                 std::to_string(b) + "\n";
        const double press = 5 * kBar + 0.5 * kBeat;
        s += "key " + num(press) + " P\nend " + num(11 * kBar) + "\n";
        int soon = -1, next = -1;
        const auto take = playWatching(s, [&](double now, const StudioView &v) {
            if (soon < 0 && now > press + 0.1)
                soon = v.chordRoot;
            if (next < 0 && now > 6 * kBar + 0.1)
                next = v.chordRoot;
        });
        QCOMPARE(soon, 0); // the loop's first chord, the moment P lands
        QCOMPARE(next, 5); // and the loop carries on from there
        for (int b = 5; b < 11; ++b)
            QVERIFY2(take.rms(b * kBar, (b + 1) * kBar) > 0.02, qPrintable(QString("bar %1 is quiet").arg(b)));
    }

    // Return holds, as L and the click do; Delete lets go of the chord held
    // and leaves hold on for the next one.
    void returnHoldsAndDeleteLetsGo() {
        const auto held =
            testkit::play("page 0 3\nkey 0.1 Return\ndown 0.2 1 " + cell(0, Triad) + "\nup 0.6 1\nend 2.5\n");
        const auto loose = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Triad) + "\nup 0.6 1\nend 2.5\n");
        QVERIFY(held.rms(2.0, 2.5) > 0.01);
        QVERIFY(loose.rms(2.0, 2.5) < 0.3 * held.rms(2.0, 2.5));
        for (int c = 0; c < 8; ++c)
            QCOMPARE(int(held.view.parts[kChords].clips[c].state), int(ClipView::Empty));
        QString status;
        const auto gone = testkit::play(
            "page 0 3\nkey 0.1 Return\ndown 0.2 1 " + cell(0, Triad) + "\nup 0.6 1\nkey 1.0 Delete\nend 2.5\n",
            [&](Session &s) { status = s.pageStatus(); });
        QVERIFY(gone.rms(2.0, 2.5) < 0.3 * held.rms(2.0, 2.5));
        QVERIFY2(status.contains("hold"), qPrintable(status));
    }

    // The band under the pad does what the keys do. On the scripts' window
    // the pad is 900 × 562.5 px: row 0 is 0.048 of it under the pad, row 1
    // 0.119. Row 0 holds the clips, then the six sounds from about 0.4 to
    // 0.8 of the width; row 1 starts with HOLD.
    void theBandUnderThePadIsClickable() {
        QString status;
        testkit::play("page 0 3\nbutton 0.2 0.48 -0.048\nend 0.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("trance", Qt::CaseInsensitive), qPrintable(status));
        testkit::play("page 0 3\nbutton 0.2 0.76 -0.048\nend 0.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("pump", Qt::CaseInsensitive), qPrintable(status));
        // HOLD, then a chord that keeps ringing after the finger lifts.
        const auto held = testkit::play("page 0 3\nbutton 0.1 0.03 -0.119\ndown 0.2 1 " + cell(0, Triad) +
                                            "\nup 0.6 1\nend 2.5\n",
                                        [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("hold"), qPrintable(status));
        QVERIFY(held.rms(2.0, 2.5) > 0.01);
        // VOICE + twice: the voicing goes up two steps.
        testkit::play("page 0 3\nbutton 0.1 0.6 -0.119\nbutton 0.2 0.6 -0.119\nend 0.5\n",
                      [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("voicing +2"), qPrintable(status));
        // GATE +: the pad's held chord takes the next gate, the offbeat.
        testkit::play("page 0 3\nbutton 0.1 0.37 -0.119\nend 0.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("gate offbeat", Qt::CaseInsensitive), qPrintable(status));
        // The same from the keyboard: Shift+→, or ] as a second key.
        testkit::play("page 0 3\nkey 0.1 Shift+Right\nend 0.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("gate offbeat", Qt::CaseInsensitive), qPrintable(status));
        testkit::play("page 0 3\nkey 0.1 ]\nend 0.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("gate offbeat", Qt::CaseInsensitive), qPrintable(status));
        testkit::play("page 0 3\nkey 0.1 Shift+Left\nend 0.5\n", [&](Session &s) { status = s.pageStatus(); });
        QVERIFY2(status.contains("gate wub", Qt::CaseInsensitive) && status.contains("pad", Qt::CaseInsensitive),
                 qPrintable(status));
    }

    // A clip slot under the pad, clicked, starts its loop or stops it.
    void clickingAClipLaunchesIt() {
        const std::string loop = recordedLoop({0, 5, 2, 6}) + "key " + num(2 * kBar + 0.2) + " Backspace\n";
        const auto stopped = testkit::play(loop + "end " + num(5 * kBar) + "\n");
        QVERIFY(stopped.view.parts[kChords].playing < 0);
        // Slot 1 is the first of eight across the clip strip (0 .. ~0.33).
        const auto launched =
            testkit::play(loop + "button " + num(4 * kBar) + " 0.02 -0.048\nend " + num(6 * kBar) + "\n");
        QCOMPARE(launched.view.parts[kChords].playing, 0);
    }

    // Clicking the pad holds, like L.
    void theClickHolds() {
        const auto held = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Triad) + "\nclick 0.3\nup 0.6 1\nend 2.5\n");
        QVERIFY(held.rms(2.0, 2.5) > 0.01);
        for (int c = 0; c < 8; ++c)
            QCOMPARE(int(held.view.parts[kChords].clips[c].state), int(ClipView::Empty));
    }

    // Ten fingers at once, all over the pad and the rail, stay within bounds.
    void tenFingersStayInBounds() {
        std::string s = "page 0 3\n";
        for (int f = 0; f < 10; ++f) {
            const float x = 0.05f + 0.09f * float(f), y = f < 7 ? 0.1f + 0.1f * float(f % 7) : 0.9f;
            s += "down " + num(0.2 + 0.05 * f) + " " + std::to_string(100 + f) + " " + num(x) + " " + num(y) +
                 " p=0.8 size=1.6\n";
            s += "glide 1.0 2.5 " + std::to_string(100 + f) + " " + num(x) + " " + num(std::min(0.79f, y + 0.2f)) +
                 " p=1 twist=40\n";
        }
        s += "end 3.5\n";
        const auto take = testkit::play(s);
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
        QVERIFY(take.view.parts[kChords].voices <= 10);
    }

    // A song keeps the page's settings: the sound chosen, the voicing, hold,
    // and each sound's own gate, length and filter. They come back in a new
    // tenfold exactly as saved, and the engine gates the organ as before.
    void aSongKeepsTheSoundAndItsGates() {
        const std::string settings = "key 0.1 Right\nkey 0.15 Right\nkey 0.2 Shift+Right\nkey 0.25 Up\nkey 0.3 Up\n"
                                     "key 0.35 -\nkey 0.4 -\ndown 0.5 20 " +
                                     rail(Hold, 0.85f) + "\nup 1.0 20\n";
        const std::string s = recordedLoop({0, 5, 2, 6}, Triad, settings) + "key 4.5 L\nend 5.5\n";
        testkit::reopen(s, 3.0, [](Session &, Session &opened, const QJsonObject &doc) {
            const QJsonObject part = doc[QStringLiteral("parts")].toArray()[kChords].toObject();
            QCOMPARE(part[QStringLiteral("name")].toString(), QStringLiteral("Chords"));
            const QJsonObject saved = part[QStringLiteral("settings")].toObject();
            QCOMPARE(saved[QStringLiteral("sound")].toString(), QStringLiteral("ORGAN"));
            QCOMPARE(saved[QStringLiteral("voicing")].toInt(), 2);
            QCOMPARE(saved[QStringLiteral("hold")].toBool(), true);
            const QJsonObject organ = saved[QStringLiteral("sounds")].toObject()[QStringLiteral("ORGAN")].toObject();
            QCOMPARE(organ[QStringLiteral("gate")].toString(), QStringLiteral("8THS"));
            QVERIFY(std::fabs(organ[QStringLiteral("filter")].toDouble() - 0.4) < 1e-4);
            QVERIFY(std::fabs(organ[QStringLiteral("length")].toDouble() - 0.4) < 1e-4);
            // What the opened song would save is what the file holds.
            const QJsonObject again = opened.saveSong()[QStringLiteral("parts")]
                                          .toArray()[kChords]
                                          .toObject()[QStringLiteral("settings")]
                                          .toObject();
            QCOMPARE(again, saved);
            // The engine gates the organ on eighths again, and the loop plays.
            QCOMPARE(int(opened.view().parts[kChords].data[40 + 2]), int(Eighths));
            QCOMPARE(opened.view().parts[kChords].playing, 0);
            const QString status = opened.pageStatus();
            QVERIFY2(status.contains(QStringLiteral("ORGAN")) && status.contains(QStringLiteral("hold")) &&
                         status.contains(QStringLiteral("voicing +2")),
                     qPrintable(status));
        });
    }

    // A song edited by hand: wrong types, numbers far out of range, names
    // this tenfold doesn't know, and a loop of chords no page would play.
    // The settings open as sane values and the chords stay finite.
    void aGarbledSongOpensSane() {
        QJsonObject doc;
        testkit::play("page 0 3\nend 0.1\n", [&](Session &s) { doc = s.saveSong(); });
        QJsonArray parts = doc[QStringLiteral("parts")].toArray();
        QJsonObject part = parts[kChords].toObject();
        QJsonObject sounds;
        sounds[QStringLiteral("PAD")] = 7;
        sounds[QStringLiteral("ORGAN")] =
            QJsonObject{{"gate", "BOSSA"}, {"length", 1e30}, {"swing", -5}, {"filter", "x"}};
        sounds[QStringLiteral("PIANO")] = QJsonObject{{"gate", "8ths"}, {"swing", 0.25}};
        sounds[QStringLiteral("KAZOO")] = QJsonObject{{"gate", "WUB"}};
        part[QStringLiteral("settings")] = QJsonObject{
            {"sound", "KAZOO"}, {"voicing", 1e9}, {"hold", "yes"}, {"sounds", sounds}, {"extra", QJsonArray{1, 2}}};
        auto event = [](double at, const char *type, int id, QJsonArray i, QJsonArray f) {
            return QJsonObject{{"at", at}, {"type", type}, {"id", id}, {"i", i}, {"f", f}};
        };
        const QJsonArray events = {
            event(0, "on", 1, {2147483647, 99, -99, 2147483647}, {1e6, -1e6, 1e6, 1e6}),
            event(1, "on", 2, {-2147483647 - 1, -2147483647 - 1, 2147483647, -2147483647 - 1}, {-1e6, 1e6, -1e6, -1e6}),
            event(1.5, "param", 3, {2147483647, 2147483647}, {1e6}),
            event(1.6, "param", 4, {0, -2147483647 - 1}, {-1e6}),
            QJsonObject{{"at", 3}, {"type", "off"}, {"id", 1}},
            QJsonObject{{"at", 3.5}, {"type", "off"}, {"id", 2}},
        };
        part[QStringLiteral("clips")] =
            QJsonArray{QJsonObject{{"slot", 0}, {"beats", 4}, {"anchor", 0}, {"events", events}}};
        part[QStringLiteral("playing")] = 0;
        parts[kChords] = part;
        doc[QStringLiteral("parts")] = parts;

        Studio studio(48000.0f);
        studio.setTimeOrigin(0);
        setScriptClock(0);
        Session session(&studio);
        session.setClock(&scriptClock);
        QString error;
        QVERIFY2(session.openSong(doc, &error) && error.isEmpty(), qPrintable(error));
        float buf[384 * 2];
        bool finite = true;
        for (int tick = 0; tick <= 5 * 125; ++tick) {
            setScriptClock(tick / 125.0);
            session.refresh();
            session.idle(scriptClock());
            studio.render(buf, 384);
            for (float x : buf)
                finite = finite && std::isfinite(x);
        }
        session.refresh();
        QVERIFY(finite);
        QCOMPARE(session.view().parts[kChords].playing, 0);
        QVERIFY(session.view().parts[kChords].voices <= 10);

        const QJsonObject got = session.saveSong()[QStringLiteral("parts")]
                                    .toArray()[kChords]
                                    .toObject()[QStringLiteral("settings")]
                                    .toObject();
        QCOMPARE(got[QStringLiteral("sound")].toString(), QStringLiteral("PAD"));
        QCOMPARE(got[QStringLiteral("voicing")].toInt(), 3);
        QCOMPARE(got[QStringLiteral("hold")].toBool(), false);
        QVERIFY(!got.contains(QStringLiteral("extra")));
        const QJsonObject s = got[QStringLiteral("sounds")].toObject();
        QVERIFY(!s.contains(QStringLiteral("KAZOO")));
        const QJsonObject pad = s[QStringLiteral("PAD")].toObject(), organ = s[QStringLiteral("ORGAN")].toObject(),
                          piano = s[QStringLiteral("PIANO")].toObject();
        QCOMPARE(pad[QStringLiteral("gate")].toString(), QStringLiteral("HOLD"));
        QCOMPARE(pad[QStringLiteral("length")].toDouble(), 0.55);
        QCOMPARE(organ[QStringLiteral("gate")].toString(), QStringLiteral("OFFBEAT"));
        QCOMPARE(organ[QStringLiteral("length")].toDouble(), 1.0);
        QCOMPARE(organ[QStringLiteral("swing")].toDouble(), 0.0);
        QCOMPARE(organ[QStringLiteral("filter")].toDouble(), 0.5);
        QCOMPARE(piano[QStringLiteral("gate")].toString(), QStringLiteral("8THS"));
        QCOMPARE(piano[QStringLiteral("swing")].toDouble(), 0.25);
        QCOMPARE(int(session.view().parts[kChords].data[40 + 3]), int(Eighths));
    }
};

TESTKIT_REGISTER(ChordsTests)
#include "chords_tests.moc"
