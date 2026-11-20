// The Chords page: harmony in key, voice leading, the chord the others
// follow, colours, gates on the beat, loops that keep driving the harmony,
// keys, hold, and the low end left to the bass.

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

// The share of a stretch's energy between lo and hi Hz, from one long FFT.
double bandShare(const testkit::Take &take, double from, double lo, double hi) {
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
    return all > 0 ? in / all : 1;
}

// A progression of taps (a quarter of a second each, every half second),
// closed by a hold on the next beat: the tap-then-hold loop.
std::string tappedLoop(std::initializer_list<int> degrees, int row = Triad) {
    std::string s = "page 0 3\n";
    double t = 0.5;
    int first = -1;
    for (int d : degrees) {
        if (first < 0)
            first = d;
        s += "tap " + num(t) + " " + cell(d, row) + " len=0.3\n";
        t += 0.5;
    }
    s += "down " + num(t) + " 9 " + cell(first, row) + "\nup " + num(t + 0.4) + " 9\n";
    return s;
}

// Pitch classes of A natural minor, the studio's default key.
const std::set<int> kAMinor = {9, 11, 0, 2, 4, 5, 7};

}

class ChordsTests : public QObject {
    Q_OBJECT
private slots:
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

    // Tap a progression in time, land on the next beat and hold: it loops,
    // and as it replays it keeps telling the studio which chord is playing.
    void aLoopKeepsDrivingTheHarmony() {
        std::set<int> rootsAfter;
        double lastChange = 0;
        int last = -1;
        const auto take = playWatching(tappedLoop({0, 5, 2, 6}) + "end 9\n", [&](double now, const StudioView &v) {
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
        playWatching(tappedLoop({0, 5, 2, 6}) + "down 5.0 20 " + cell(4, Seventh) + "\nup 7.0 20\nend 8\n",
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

    // Return loops the last bars played, however they were played: four
    // chords held a bar each become a four-bar loop.
    void returnLoopsTheLastBars() {
        const double bar = 4 * 60.0 / 124.0;
        std::string s = "page 0 3\n";
        const char *keys[] = {"A", "F", "D", "G"};
        for (int i = 0; i < 4; ++i) {
            const double on = 0.02 + i * bar, off = on + bar - 0.05;
            s += "key " + num(on) + " " + keys[i] + " down\n";
            s += "key " + num(off) + " " + keys[i] + " up\n";
        }
        s += "key " + num(4 * bar - 0.3) + " Return\nend " + num(9 * bar) + "\n";
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

    // Clicking the pad holds: the chord rings after the finger lifts, until
    // the next one replaces it.
    void clickHolds() {
        const auto held = testkit::play("page 0 3\ndown 0.2 1 " + cell(0, Triad) + "\nclick 0.3\nup 0.6 1\nend 2.5\n");
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
        playWatching(tappedLoop({0, 0, 0, 0}) + "key 5.0 Ctrl+Right\nend 7\n", [&](double now, const StudioView &v) {
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
};

TESTKIT_REGISTER(ChordsTests)
#include "chords_tests.moc"
