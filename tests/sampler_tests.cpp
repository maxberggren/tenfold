// The Sampler page: a song dropped in, cut on its bars, played in the
// studio's time. The songs are made here (a beat with a bass line), so the
// tests need no sound files, no ffmpeg and no network.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <random>
#include <map>
#include <set>
#include <sstream>

#include "sampler.h"
#include "songfile.h"
#include "visual.h"
#include "testkit.h"

using namespace tf;
using namespace tf::sampler;

namespace {

constexpr int kSampler = 8; // the ninth part
constexpr int kRate = 48000;

// A song with a beat: a kick on every beat (louder on the one), a snare on
// two and four, hats on the eighths, a bass note to the bar (A F C G) with
// a fifth over it, `bars` long at `bpm`, after `lead` seconds of silence.
std::vector<int16_t> song(double bpm, int bars, double lead = 0) {
    const double beat = 60.0 / bpm * kRate;
    const int64_t n = int64_t(lead * kRate + bars * 4 * beat);
    std::vector<float> x(size_t(n), 0.0f);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1, 1);
    const double notes[4] = {55.0, 43.65, 65.41, 49.0};
    for (int b = 0; b < bars * 4; ++b) {
        const int64_t at = int64_t(lead * kRate + b * beat);
        const double f = notes[(b / 4) % 4];
        for (int i = 0; i < int(beat) && at + i < n; ++i) {
            const double t = double(i) / kRate;
            float &v = x[size_t(at + i)];
            v += 0.2f * float(std::sin(2 * M_PI * f * t) + 0.5 * std::sin(2 * M_PI * 2 * f * t)) *
                 float(std::min(1.0, t * 200) * std::exp(-t * 2));
            v += 0.04f * float(std::sin(2 * M_PI * f * 4 * t) + std::sin(2 * M_PI * f * 6 * t));
            // (Each rings out to nothing: a drum cut short would be a
            // click of its own for the tests to count.)
            if (i < 12000)
                v += (b % 4 == 0 ? 0.7f : 0.5f) * float(std::sin(2 * M_PI * (48 + 110 * std::exp(-t * 45)) * t) * std::exp(-t * 16)) *
                         float(std::min(1.0, (12000 - i) / 2400.0)) +
                     (i < 60 ? 0.25f * u(rng) : 0.0f);
            if (b % 2 == 1 && i < 12000)
                v += 0.3f * u(rng) * float(std::exp(-t * 28));
        }
        for (int h = 0; h < 2; ++h) {
            const int64_t ha = at + int64_t(h * beat / 2);
            for (int i = 0; i < 1800 && ha + i < n; ++i)
                x[size_t(ha + i)] += 0.12f * u(rng) * float(std::exp(-i / 300.0));
        }
    }
    std::vector<int16_t> d(size_t(2 * n));
    for (int64_t i = 0; i < n; ++i)
        d[size_t(2 * i)] = d[size_t(2 * i + 1)] = int16_t(std::clamp(x[size_t(i)], -1.0f, 1.0f) * 30000);
    return d;
}

Pcm pcmOf(const std::vector<int16_t> &d) {
    Pcm p;
    p.data = d;
    p.prepare();
    return p;
}

// Where sounds begin in a mono signal: the highs rising sharply. Of
// rises closer than `apart` seconds the loudest is the hit, and a rise far
// quieter than the hits (30 dB under a hi-hat, as a splice can leave in a
// bass note) is not one: a drum heard twice would be as loud as a drum.
std::vector<double> onsets(const float *x, size_t n, double apart = 0.05) {
    struct Rise {
        double t, e;
    };
    std::vector<Rise> rises;
    const int hop = 48;
    double prev = 0, slow = 0;
    for (size_t i = hop; i + 11 * hop < n; i += hop) {
        double e = 0;
        for (int j = 0; j < hop; ++j) {
            const double d = double(x[i + j]) - double(x[i + j - 1]);
            e += d * d;
        }
        if (e > 6 * slow + 1e-6 && e > prev) {
            // How loud: the highs over the 10 ms from there.
            double loud = 0;
            for (int j = 0; j < 10 * hop; ++j) {
                const double d = double(x[i + j]) - double(x[i + j - 1]);
                loud += d * d;
            }
            rises.push_back({double(i) / kRate, loud});
        }
        slow += (e - slow) * 0.05;
        prev = e;
    }
    if (rises.empty())
        return {};
    std::vector<double> levels;
    for (const Rise &r : rises)
        levels.push_back(r.e);
    std::sort(levels.begin(), levels.end());
    const double least = 0.03 * levels[levels.size() * 9 / 10]; // 15 dB under the hits
    std::sort(rises.begin(), rises.end(), [](const Rise &a, const Rise &b) { return a.e > b.e; });
    std::vector<double> out;
    for (const Rise &r : rises) {
        if (r.e < least)
            break;
        bool taken = false;
        for (double t : out)
            taken |= std::fabs(t - r.t) < apart;
        if (!taken)
            out.push_back(r.t);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// What a script needs to know of a loaded song to find its chops: read
// from the page once the song is in.
struct Lay {
    double origin = 0, rowBeats = 16, first = 0, beat = 1, downbeat = 0, bpm = 0;
    int chops = 0, rows = 0;
    // The grid's beat a sample of the song falls on.
    double beatOf(double sample) const { return (sample - first) / beat; }
    // Where on the pad the chop holding that beat is (its row's page, and
    // the place in it), half a beat into it.
    int pageOf(double gridBeat) const { return int(std::floor((gridBeat - origin) / rowBeats)) / kRows; }
    std::string at(double gridBeat) const {
        const double into = gridBeat + 0.5 - origin;
        const int row = int(std::floor(into / rowBeats)) % kRows;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.4f %.4f", (into - std::floor(into / rowBeats) * rowBeats) / rowBeats,
                      1.0 - (row + 0.5) / kRows);
        return buf;
    }
};

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

}

class SamplerTests : public QObject {
    Q_OBJECT
    Q_CLASSINFO("alone", "tenFingersHeldStayCheap sPutsTheSongThroughTheModelAndKeepsItsStems "
                         "aLossySongIsRestoredByItselfAndKept")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev",
                "atItsOwnTempoTheSongPlaysSampleForSample aDroppedSongIsReadAndTheStudioTakesItsTempo "
                "aTapPlaysItsChopOnceAsTheSongHasIt "
                "spaceLoopsTheChopsJustPlayed theSlotKeysStopLaunchAndClear ctrlZTakesBackTheSamplersLoop aSongFileBringsTheSongBackWithItsLoop "
                "aFileThatIsNoSoundSaysSo thePageDrawsEmptyLoadedAndPlaying")

private:
    QTemporaryDir m_dir;
    QString m_song, m_slow; // 100 BPM from the top; 93 BPM after a silence

    QString write(const QString &name, const std::vector<int16_t> &d) {
        const QString path = m_dir.filePath(name);
        writeWavStereo(QFile::encodeName(path).toStdString(), d.data(), d.size() / 2, kRate);
        return path;
    }
    static std::string drop(const QString &path, double t = 0.1) { return "drop " + num(t) + " " + path.toStdString() + "\n"; }
    // The layout of a song once dropped (and anything else the page says).
    Lay layOf(const QString &path, const std::string &then = {}) {
        Lay l;
        testkit::play(drop(path) + then + "end 0.4\n", [&](Session &s) {
            auto probe = [&](const char *name) { return double(s.probe(kSampler, QString::fromLatin1(name))); };
            l.origin = probe("origin");
            l.rowBeats = probe("rowBeats");
            l.first = probe("gridFirst");
            l.beat = probe("gridBeat");
            l.downbeat = probe("downbeat");
            l.bpm = probe("bpm");
            l.chops = int(probe("chops"));
            l.rows = int(probe("rows"));
        });
        return l;
    }
    // The spans the engine plays (in the song's beats), every pass: the
    // hand's between heardFrom and heardTo, one span, trimmed shorter than
    // its two-beat chop; and the loop's from loopFrom on, the same one.
    static void loopPlaysAsHeard(const std::string &script, double heardFrom, double heardTo, double loopFrom) {
        std::set<std::pair<int, int>> heard, looped;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            for (int i = 0; i < kVoices; ++i) {
                const float *d = s.view().parts[kSampler].data + pub::First + i * pub::Per;
                if (d[pub::Handle] <= 0)
                    continue;
                const std::pair<int, int> span{int(std::lround(d[pub::Start] * 50)), int(std::lround(d[pub::End] * 50))};
                const bool loop = (int(d[pub::Flags]) & 2) != 0;
                if (!loop && t > heardFrom && t < heardTo)
                    heard.insert(span);
                if (loop && t > loopFrom)
                    looped.insert(span);
            }
        });
        auto say = [](const std::set<std::pair<int, int>> &spans) {
            QStringList out;
            for (const auto &[a, b] : spans)
                out << QStringLiteral("%1–%2").arg(a / 50.0).arg(b / 50.0);
            return out.join(QStringLiteral(", "));
        };
        QVERIFY(take.view.parts[kSampler].playing >= 0);
        QVERIFY2(heard.size() == 1, qPrintable(QStringLiteral("heard: ") + say(heard)));
        const auto [a, b] = *heard.begin();
        QVERIFY2(b - a < 50 * 1.7, qPrintable(QStringLiteral("not trimmed: ") + say(heard)));
        QVERIFY2(looped == heard, qPrintable(QStringLiteral("heard %1; the loop played %2").arg(say(heard), say(looped))));
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        // Nothing of the tests' goes into the player's own cache.
        qputenv("TENFOLD_SAMPLER_CACHE", QFile::encodeName(m_dir.filePath(QStringLiteral("cache"))));
        qputenv("TENFOLD_SAMPLES_DIR", QFile::encodeName(m_dir.filePath(QStringLiteral("samples"))));
        m_song = write(QStringLiteral("beat.wav"), song(100, 24));
        m_slow = write(QStringLiteral("slow.wav"), song(93, 16, 1.5));
    }

    // --- the stretcher -------------------------------------------------------

    void atItsOwnTempoTheSongPlaysSampleForSample() {
        const Pcm p = pcmOf(song(100, 4));
        Stretcher s;
        s.start(&p, 1000);
        float l[32], r[32];
        double worst = 0;
        for (int i = 0; i < kRate; i += 32) {
            s.render(l, r, 32, 1000.0 + i, 1.0, 1.0);
            for (int j = 0; j < 32; ++j)
                worst = std::max(worst, std::fabs(double(l[j]) - p.data[size_t(2 * (1000 + i + j))] / 32768.0));
        }
        QCOMPARE(worst, 0.0);
        QCOMPARE(s.jumps(), 0);
    }

    // Slower or faster, every drum is still there, once, and on time.
    void stretchedEveryHitIsKeptOnceAndOnTime_data() {
        QTest::addColumn<double>("speed");
        QTest::addColumn<double>("rate");
        QTest::newRow("a little slower") << 0.96 << 1.0;
        QTest::newRow("slower") << 0.85 << 1.0;
        QTest::newRow("much slower") << 0.72 << 1.0;
        QTest::newRow("a little faster") << 1.05 << 1.0;
        QTest::newRow("faster") << 1.2 << 1.0;
        QTest::newRow("much faster") << 1.4 << 1.0;
        QTest::newRow("up two semitones") << 1.0 << std::pow(2.0, 2.0 / 12);
        QTest::newRow("down three semitones") << 1.0 << std::pow(2.0, -3.0 / 12);
    }
    void stretchedEveryHitIsKeptOnceAndOnTime() {
        QFETCH(double, speed);
        QFETCH(double, rate);
        const Pcm p = pcmOf(song(100, 8));
        Stretcher s;
        s.start(&p, 0);
        const int64_t n = int64_t(double(p.frames - kRate) / speed);
        std::vector<float> l(size_t(n) + 32), r(size_t(n) + 32);
        double nominal = 0;
        for (int64_t i = 0; i < n; i += 32) {
            s.render(l.data() + i, r.data() + i, 32, nominal, speed, rate);
            nominal += 32 * speed;
        }
        // The song read at the new pitch with no stretching is what the
        // hits are compared with: where each is, moved to the new tempo.
        std::vector<float> ref(size_t(double(p.frames) / rate));
        for (size_t i = 0; i < ref.size(); ++i) {
            float a, b;
            p.read(double(i) * rate, a, b);
            ref[i] = a;
        }
        const auto want = onsets(ref.data(), ref.size()), got = onsets(l.data(), size_t(n));
        QVERIFY(want.size() > 40);
        std::vector<bool> used(got.size(), false);
        int missed = 0, extra = 0;
        double sum = 0, worst = 0;
        int hits = 0;
        for (double t : want) {
            const double at = t * rate / speed;
            if (at > double(n) / kRate - 0.2)
                continue;
            int best = -1;
            double err = 0.02;
            for (size_t i = 0; i < got.size(); ++i)
                if (!used[i] && std::fabs(got[i] - at) < err) {
                    err = std::fabs(got[i] - at);
                    best = int(i);
                }
            if (best < 0) {
                ++missed;
                continue;
            }
            used[size_t(best)] = true;
            ++hits;
            sum += err;
            worst = std::max(worst, err);
        }
        for (size_t i = 0; i < got.size(); ++i)
            extra += !used[i] && got[i] < double(n) / kRate - 0.2;
        const QByteArray said = QStringLiteral("%1 hits, %2 missed, %3 extra, mean %4 ms, worst %5 ms")
                                    .arg(hits).arg(missed).arg(extra).arg(sum / std::max(1, hits) * 1000, 0, 'f', 2).arg(worst * 1000, 0, 'f', 2)
                                    .toUtf8();
        QVERIFY2(missed == 0, said.constData());
        QVERIFY2(extra == 0, said.constData());
        QVERIFY2(sum / std::max(1, hits) < 0.004, said.constData());
        QVERIFY2(worst < 0.016, said.constData());
    }

    void aTempoFarFromTheStudiosIsHalvedOrDoubled() {
        QCOMPARE(foldFor(100, 100), 1.0);
        QCOMPARE(foldFor(170, 90), 0.5); // drum and bass on a 90 beat: two of its beats to one
        QCOMPARE(foldFor(70, 128), 2.0);
        QCOMPARE(foldFor(120, 100), 1.0);
        QCOMPARE(foldFor(100, 140), 1.0);
    }

    // A song at about twice the studio's tempo plays two of its beats to
    // each of the studio's: near its own speed, not dragged to half.
    void aSongTwiceTheStudiosTempoPlaysTwoBeatsToOne() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n";
        const Lay l = layOf(m_song, size);
        const double bar = std::round(l.beatOf(0)) + 8;
        // The song is at 100: the studio at 52 is nearer half of it.
        const auto take = testkit::play(drop(m_song) + size + "tempo 0.5 52\ntap 1.0 " + l.at(bar) + "\nend 5\n");
        // A bar of the song is four of its beats: two of the studio's, 2.3 s.
        QVERIFY(take.rms(1.1, 3.1) > 0.02);
        QVERIFY2(take.rms(3.5, 5.0) < 0.002, qPrintable(QString::number(take.rms(3.5, 5.0))));
    }

    void aSongIsMovedToTheStudiosKeyTheShortWay() {
        Sound s;
        s.keyKnown = true;
        s.keyRoot = 9; // A minor: the notes of C major
        s.keyMinor = true;
        Tuning t;
        t.set(9, 0, 4, false); // A Aeolian
        QCOMPARE(keyShift(t, s), 0);
        t.set(0, 0, 1, false); // C Ionian: the same notes
        QCOMPARE(keyShift(t, s), 0);
        t.set(11, 0, 4, false); // B Aeolian: up two
        QCOMPARE(keyShift(t, s), 2);
        t.set(4, 0, 4, false); // E Aeolian: down five rather than up seven
        QCOMPARE(keyShift(t, s), -5);
        t.set(2, 0, 3, false); // D Dorian: C major's notes again
        QCOMPARE(keyShift(t, s), 0);
        s.keyKnown = false; // a key not heard for sure is left alone
        t.set(4, 0, 4, false);
        QCOMPARE(keyShift(t, s), 0);
    }

    // --- the layout ----------------------------------------------------------

    void theSongIsCutOnItsBarsWhereSoundsLand() {
        Sound s;
        s.frames = 24000 * 64;
        for (int i = 0; i <= 64; ++i)
            s.beats.push_back(24000.0 * i);
        Marks m;
        // A sound on the one and the three of every bar; the two and the
        // four are held through.
        for (int i = 0; i <= 64; ++i) {
            m.strength.push_back(i % 2 == 0 ? 0.8f : 0.01f);
            m.attack.push_back(24000.0 * i - 120);
        }
        const Layout a = layOut(s, m, Auto);
        QCOMPARE(a.rows, 4);
        QCOMPARE(a.rowBeats, 16);
        QCOMPARE(int(a.chops.size()), 32); // two to the bar
        QCOMPARE(a.chops[1].at, 4);
        QCOMPARE(a.chops[1].length, 4);
        QCOMPARE(a.chops[1].x0, 0.125f);
        // Cut at the attack's foot, a little before the beat, and each
        // ends where the next begins.
        QVERIFY(a.chops[1].start < 2.0 && a.chops[1].start > 1.99);
        QCOMPARE(a.chops[0].end, a.chops[1].start);
        QCOMPARE(a.find(0, 0.13f), 1);
        QCOMPARE(a.find(3, 0.99f), 31);
        QCOMPARE(a.find(4, 0.5f), -1);
        // By the beat, the bar, the two bars: rows of 16, 32 and 64 beats.
        QCOMPARE(int(layOut(s, m, Beat).chops.size()), 64);
        QCOMPARE(int(layOut(s, m, Half).chops.size()), 128);
        QCOMPARE(layOut(s, m, Half).rowBeats, 8);
        QCOMPARE(int(layOut(s, m, Bar).chops.size()), 16);
        QCOMPARE(layOut(s, m, Bar).rows, 2);
        QCOMPARE(int(layOut(s, m, TwoBars).chops.size()), 8);
        // The bar lines a beat later: a pickup of one beat, then whole bars.
        const Layout shifted = layOut(s, m, Bar, 1);
        QCOMPARE(shifted.chops.front().at, -6); // the bar the pickup ends: its one beat of the song
        QVERIFY(std::fabs(shifted.chops.front().start) < 0.01 && std::fabs(shifted.chops.front().end - 0.995) < 0.01);
        QCOMPARE(shifted.chops[1].at, 2);
        QCOMPARE(shifted.chops[1].length, 8);
    }

    void theRowsStartWhereTheSongsSectionsDo() {
        Sound s;
        s.frames = 24000 * 96;
        for (int i = 0; i <= 96; ++i)
            s.beats.push_back(24000.0 * i);
        Marks m;
        m.strength.assign(97, 0.8f);
        for (int i = 0; i <= 96; ++i)
            m.attack.push_back(24000.0 * i);
        // A two-bar intro, then a new section every four bars.
        m.change.assign(24, 0.05f);
        for (int k = 2; k < 24; k += 4)
            m.change[size_t(k)] = 0.9f;
        const Layout a = layOut(s, m, Beat);
        QCOMPARE(a.origin, -8); // the intro fills the second half of the first row
        QCOMPARE(a.chops.front().at, 0);
        QCOMPARE(a.chops.front().x0, 0.5f);
        QCOMPARE(a.chops[8].row, 1); // the first section starts the second row
        QCOMPARE(a.chops[8].x0, 0.0f);
    }

    // --- the page ------------------------------------------------------------

    void aNinthPartHasItsPlaceBesideTheEight() {
        // Shift+1, as keyboards send it: "!".
        testkit::play("key 0.1 !\nend 0.3\n", [](Session &s) {
            QCOMPARE(s.partCount(), 9);
            QCOMPARE(s.page(), kSampler);
            QCOMPARE(s.pageName(kSampler), QStringLiteral("Sampler"));
            QCOMPARE(s.pageKey(kSampler), QStringLiteral("⇧1"));
            QCOMPARE(s.pageKey(s.arrangePage()), QStringLiteral("0"));
            QCOMPARE(s.pageKey(s.visualPage()), QStringLiteral("9"));
            QCOMPARE(s.pageNamed(QStringLiteral("sampler")), kSampler);
        });
        // Where the digits are typed with Shift (a French keyboard), the 1
        // that Shift types is still the Drums. And Tab, from the Vocal
        // page, comes to the Sampler before the Song page.
        testkit::play("page 0.05 4\nkey 0.1 Shift+1\nend 0.3\n", [](Session &s) { QCOMPARE(s.page(), 0); });
        testkit::play("page 0.1 8\nkey 0.2 Tab\nend 0.3\n", [](Session &s) { QCOMPARE(s.page(), kSampler); });
        testkit::play("page 0.1 8\nkey 0.2 Tab\nkey 0.25 Tab\nend 0.4\n", [](Session &s) { QCOMPARE(s.page(), s.arrangePage()); });
        // F9 is the Bass page's again (its LOOP), not a page.
        testkit::play("page 0.1 2\nkey 0.2 F9\nend 0.3\n", [](Session &s) { QCOMPARE(s.page(), 1); });
        // 9 is still the picture, 0 the song.
        testkit::play("key 0.1 9\nend 0.3\n", [](Session &s) { QCOMPARE(s.page(), s.visualPage()); });
        testkit::play("page 0.1 sampler\nend 0.3\n", [](Session &s) { QCOMPARE(s.page(), kSampler); });
    }

    // OPEN FILE, under DROP A SONG HERE: the file the dialog picks is read
    // in, as if it had been dropped.
    void openFileReadsWhatTheDialogPicks() {
        struct Picker : FilePicker {
            QString path;
            int asked = 0;
            void openFile(const QString &, std::function<void(const QString &)> done) override { done({}); }
            void openSound(const QString &, std::function<void(const QString &)> done) override {
                ++asked;
                done(path);
            }
            void saveFile(const QString &, const QString &, const QString &, std::function<void(const QString &)>) override {}
        } picker;
        picker.path = m_song;
        float songs = -1;
        const auto take = testkit::watch("page 0.05 sampler\nmouse 0.2 0.39 0.31 down\nmouse 0.25 0.39 0.31 up\nend 0.8\n",
                                         [&](double, Session &s) {
                                             s.setFilePicker(&picker);
                                             songs = s.probe(kSampler, QStringLiteral("songs"));
                                         });
        QVERIFY2(take.error.empty(), take.error.c_str());
        QCOMPARE(picker.asked, 1);
        QCOMPARE(songs, 1.0f);
    }

    // RECORD, under DROP A SONG HERE: the microphone until RECORD again,
    // then the take is read in as a song, saved as a WAV.
    void recordTakesTheMicrophoneAsASong() {
        QDir(m_dir.filePath(QStringLiteral("samples"))).removeRecursively();
        float songs = -1;
        const auto take = testkit::watch("page 0.05 sampler\nmic 0.1 " + m_song.toStdString() +
                                             "\nmouse 0.3 0.61 0.31 down\nmouse 0.32 0.61 0.31 up\n"
                                             "mouse 4.3 0.61 0.31 down\nmouse 4.32 0.61 0.31 up\nend 4.8\n",
                                         [&](double, Session &s) { songs = s.probe(kSampler, QStringLiteral("songs")); });
        QVERIFY2(take.error.empty(), take.error.c_str());
        QCOMPARE(songs, 1.0f);
        const QStringList saved = QDir(m_dir.filePath(QStringLiteral("samples"))).entryList({QStringLiteral("*.wav")}, QDir::Files);
        QCOMPARE(saved.size(), 1);
        // About the four seconds between the two presses.
        const QFileInfo info(m_dir.filePath(QStringLiteral("samples/") + saved.first()));
        const double secs = double(info.size() - 44) / (48000.0 * 6);
        QVERIFY2(secs > 3.7 && secs < 4.2, qPrintable(QString::number(secs)));
    }

    void aDroppedSongIsReadAndTheStudioTakesItsTempo() {
        const auto take = testkit::play(drop(m_song) + "end 0.5\n", [&](Session &s) {
            // Dropped on the drums' page, the Sampler takes it and is shown.
            QCOMPARE(s.page(), kSampler);
            QCOMPARE(s.probe(kSampler, QStringLiteral("songs")), 1.0f);
            QCOMPARE(s.probe(kSampler, QStringLiteral("matched")), 1.0f);
            QVERIFY2(std::fabs(s.probe(kSampler, QStringLiteral("bpm")) - 100.0f) < 0.15f,
                     qPrintable(QString::number(s.probe(kSampler, QStringLiteral("bpm")))));
        });
        QVERIFY(take.error.empty());
        // Nothing was made yet: the studio is the song's.
        QVERIFY2(std::fabs(take.view.bpm - 100.0) < 0.15, qPrintable(QString::number(take.view.bpm)));
    }

    void intoATrackAlreadyGoingTheStudioKeepsItsTempo() {
        // A drum loop first (taps on the kick pad, then Space), then the song.
        const auto take = testkit::play("page 0.05 1\ntap 0.5 0.12 0.12\ntap 1.0 0.12 0.12\ntap 1.5 0.12 0.12\ntap 2.0 0.12 0.12\n"
                                        "key 2.5 Space\n" + drop(m_song, 4.0) + "end 4.5\n",
                                        [&](Session &s) { QCOMPARE(s.probe(kSampler, QStringLiteral("songs")), 1.0f); });
        QVERIFY(take.view.parts[0].playing >= 0);
        QVERIFY2(std::fabs(take.view.bpm - 120.0) < 1.0, qPrintable(QString::number(take.view.bpm)));
    }

    void theGridSitsOnTheSongsBeatsAndBars() {
        const Lay l = layOf(m_song);
        QVERIFY2(std::fabs(l.bpm - 100) < 0.15, qPrintable(QString::number(l.bpm)));
        // The song's kicks are every 28800 samples from its top: on the
        // grid's beats, the first of them a bar line.
        const double b = l.beatOf(0);
        QVERIFY2(std::fabs(b - std::round(b)) < 0.03, qPrintable(QString::number(b)));
        QCOMPARE((int(std::lround(b)) - int(l.downbeat) + 400) % 4, 0);
        QVERIFY(l.chops >= 24 * 2);
        // And through a silence at the top, at another tempo.
        const Lay s = layOf(m_slow);
        QVERIFY2(std::fabs(s.bpm - 93) < 0.15, qPrintable(QString::number(s.bpm)));
        const double sb = s.beatOf(1.5 * kRate);
        QVERIFY2(std::fabs(sb - std::round(sb)) < 0.03, qPrintable(QString::number(sb)));
        QCOMPARE((int(std::lround(sb)) - int(s.downbeat) + 400) % 4, 0);
    }

    void aTapPlaysItsChopOnceAsTheSongHasIt() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n"); // by the beat
        const double beat = std::round(l.beatOf(0)) + 8; // the song's ninth beat: a one
        const auto take = testkit::play(drop(m_song) + "key 0.2 Shift+Right\nkey 0.25 Shift+Right\ntap 1.0 " + l.at(beat) + "\nend 3\n");
        // A beat of sound (0.6 s), then nothing.
        QVERIFY2(take.rms(1.02, 1.5) > 0.02, qPrintable(QString::number(take.rms(1.02, 1.5))));
        QVERIFY2(take.rms(1.75, 3.0) < 0.002, qPrintable(QString::number(take.rms(1.75, 3.0))));
        // It is that beat of the song: what comes out lines up with the
        // song from there, sample for sample but for the level.
        const std::vector<int16_t> src = song(100, 24);
        const size_t from = size_t(8 * 28800), n = 12000;
        double best = 0;
        int bestLag = 0;
        for (int lag = -1500; lag <= 1500; ++lag) {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = 0; i < n; i += 4) {
                const double a = take.left[size_t(48000 + 2400 + lag) + i], b = src[2 * (from + 2400 + i)] / 32768.0;
                xy += a * b;
                xx += a * a;
                yy += b * b;
            }
            const double c = xy / std::sqrt(xx * yy + 1e-12);
            if (c > best) {
                best = c;
                bestLag = lag;
            }
        }
        QVERIFY2(best > 0.98, qPrintable(QStringLiteral("%1 at %2").arg(best).arg(bestLag)));
    }

    void aHeldChopRepeatsUntilTheFingerLifts() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const double beat = std::round(l.beatOf(0)) + 8;
        // Held for two and a half beats: it goes round three times, the
        // third to its end, then stops.
        const auto take = testkit::play(drop(m_song) + "key 0.2 Shift+Right\nkey 0.25 Shift+Right\ndown 1.0 1 " + l.at(beat) +
                                        "\nup 2.5 1\nend 4.5\n");
        const auto on = onsets(take.left.data() + 48000, size_t(3.4 * 48000), 0.4);
        QVERIFY2(on.size() == 3, qPrintable(QString::number(on.size())));
        // The same kick each time, a beat apart.
        QVERIFY2(std::fabs(on[1] - on[0] - 0.6) < 0.01, qPrintable(QString::number(on[1] - on[0])));
        QVERIFY2(std::fabs(on[2] - on[1] - 0.6) < 0.01, qPrintable(QString::number(on[2] - on[1])));
        QVERIFY(take.rms(2.5, 2.75) > 0.01);  // the third pass plays out after the lift
        QVERIFY(take.rms(2.95, 4.5) < 0.002); // and that is the end of it
    }

    void theNextTapCutsOffTheChopBefore() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n"); // by the bar
        const double bar = std::round(l.beatOf(0)) + 8;
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n";
        // A bar is 2.4 s; a second bar tapped after 0.6 s takes over: only
        // one voice then, and the sound ends a bar after the second tap.
        const auto take = testkit::watch(drop(m_song) + size + "tap 1.0 " + l.at(bar) + "\ntap 1.6 " + l.at(bar + 4) + "\nend 5\n",
                                         [&](double t, Session &s) {
                                             if (t > 1.7 && t < 3.9)
                                                 QCOMPARE(s.view().parts[kSampler].voices, 1);
                                         });
        QVERIFY(take.rms(3.0, 3.9) > 0.02);
        QVERIFY2(take.rms(4.1, 5.0) < 0.002, qPrintable(QString::number(take.rms(4.1, 5.0))));
    }

    // The studio at another tempo than the song's: the song's beats are
    // the studio's, and its pitch its own.
    void atAnotherTempoTheSongsBeatsAreTheStudios_data() {
        QTest::addColumn<int>("bpm");
        QTest::newRow("slower") << 88;
        QTest::newRow("faster") << 118;
    }
    void atAnotherTempoTheSongsBeatsAreTheStudios() {
        QFETCH(int, bpm);
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n";
        const Lay l = layOf(m_song, size);
        const double bar = std::round(l.beatOf(0)) + 8;
        const double beat = 60.0 / bpm;
        const auto take = testkit::play(drop(m_song) + size + "tempo 0.5 " + std::to_string(bpm) + "\ndown 1.0 1 " + l.at(bar) +
                                        "\nup " + num(1.0 + 7.5 * beat) + " 1\nend " + num(1.0 + 9 * beat) + "\n");
        QVERIFY2(std::fabs(take.view.bpm - bpm) < 0.01, qPrintable(QString::number(take.view.bpm)));
        // Kicks a studio beat apart, for two bars (the bar goes round once).
        std::vector<float> low(take.left.size());
        const auto on = onsets(take.left.data() + 48000, size_t(8 * beat * 48000), 0.75 * beat * 0.5);
        std::vector<double> kicks; // every other onset is an eighth's hat: keep those near a beat
        for (double t : on) {
            const double b = t / beat;
            if (std::fabs(b - std::round(b)) < 0.12)
                kicks.push_back(t);
        }
        QVERIFY2(kicks.size() >= 7, qPrintable(QString::number(kicks.size())));
        double worst = 0;
        for (size_t i = 0; i < kicks.size(); ++i)
            worst = std::max(worst, std::fabs(kicks[i] - kicks[0] - std::round((kicks[i] - kicks[0]) / beat) * beat));
        QVERIFY2(worst < 0.012, qPrintable(QStringLiteral("%1 ms off the studio's beat").arg(worst * 1000)));
    }

    void aSlideMovesTheStartAndASecondFingerTheEnd() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const double beat = std::round(l.beatOf(0)) + 8;
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        std::istringstream where(l.at(beat));
        double x = 0, y = 0;
        where >> x >> y;
        // One finger, held, then slid a quarter of the pad to the right:
        // the start half a beat later (less the little it takes to be a
        // slide). Then a second finger beside it, slid left: the end earlier.
        const std::string script = drop(m_song) + size + "down 1.0 1 " + num(x) + " " + num(y) + "\nglide 1.6 2.1 1 " +
                                   num(x + 0.25) + " " + num(y) + "\ndown 2.4 2 " + num(x + 0.37) + " " + num(y) +
                                   "\nglide 2.8 3.2 2 " + num(x + 0.17) + " " + num(y) + "\nup 3.3 2\nup 3.4 1\n";
        auto trims = [](Session &s) {
            const QJsonObject doc = s.saveSong();
            for (const QJsonValue &p : doc.value(QStringLiteral("parts")).toArray())
                if (p.toObject().value(QStringLiteral("name")).toString() == QLatin1String("Sampler"))
                    return p.toObject().value(QStringLiteral("settings")).toObject().value(QStringLiteral("songs")).toArray()
                        .first().toObject().value(QStringLiteral("trims")).toArray();
            return QJsonArray();
        };
        testkit::play(script + "end 3.7\n", [&](Session &s) {
            const QJsonArray t = trims(s);
            QCOMPARE(t.size(), 1);
            const QJsonArray one = t.first().toArray();
            QCOMPARE(one[1].toInt(), 2); // a chop a beat long
            QVERIFY2(std::fabs(one[2].toDouble() - 0.44) < 0.05, qPrintable(QString::number(one[2].toDouble())));
            // The two fingers' way together: half of what the second slid.
            QVERIFY2(std::fabs(one[3].toDouble() + 0.14) < 0.06, qPrintable(QString::number(one[3].toDouble())));
            QVERIFY(s.probe(kSampler, QStringLiteral("trims")) >= 2.0f);
        });
        // It is remembered: the chop tapped again starts half a beat in,
        // on the eighth's hat, with no kick; and Delete puts it back.
        const auto again = testkit::play(script + "tap 4.0 " + num(x) + " " + num(y) + "\nend 5\n");
        const auto plain = testkit::play(drop(m_song) + size + "tap 4.0 " + num(x) + " " + num(y) + "\nend 5\n");
        QVERIFY2(again.rms(4.02, 4.1) < 0.6 * plain.rms(4.02, 4.1),
                 qPrintable(QStringLiteral("%1 %2").arg(again.rms(4.02, 4.1)).arg(plain.rms(4.02, 4.1))));
        testkit::play(script + "key 3.5 Delete\nend 3.8\n", [&](Session &s) { QCOMPARE(trims(s).size(), 0); });
    }

    // A hand playing chops around one that is held is playing chops: only
    // a finger set down right beside the held one is its second.
    void aFingerFurtherOffIsItsOwnChop() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const double b = std::round(l.beatOf(0)) + 16; // the first beat of a row: room to its right
        testkit::watch(drop(m_song) + size + "down 1.0 1 " + l.at(b) + "\ndown 2.0 2 " + l.at(b + 4) + "\nup 2.3 2\nup 3.0 1\nend 3.2\n",
                       [&](double t, Session &s) {
                           if (t > 2.05 && t < 2.25)
                               QCOMPARE(s.view().parts[kSampler].voices, 2);
                       });
        // Two fingers set down together, side by side: one chop, held by both.
        testkit::watch(drop(m_song) + size + "down 1.0 1 " + l.at(b) + "\ndown 1.02 2 " + l.at(b + 1) + "\nup 2.0 2\nup 2.0 1\nend 2.2\n",
                       [&](double t, Session &s) {
                           if (t > 1.1 && t < 1.9)
                               QCOMPARE(s.view().parts[kSampler].voices, 1);
                       });
    }

    // Finger drumming skids: a tap that lands and slides, however far, a
    // short hold that wanders, a second finger that lands beside a held
    // one and leaves again. None of it is a slide.
    void aTapThatSkidsMovesNothing() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        std::istringstream where(l.at(std::round(l.beatOf(0)) + 8));
        double x = 0, y = 0;
        where >> x >> y;
        const std::string script =
            drop(m_song) + size +
            // A tap skidding a tenth of the pad in a fifth of a second.
            "down 1.0 1 " + num(x) + " " + num(y) + "\nglide 1.02 1.2 1 " + num(x + 0.1) + " " + num(y) + "\nup 1.22 1\n" +
            // A hold just short of long enough, sliding all the while.
            "down 2.0 2 " + num(x) + " " + num(y) + "\nglide 2.0 2.35 2 " + num(x + 0.2) + " " + num(y) + "\nup 2.37 2\n" +
            // Held long, but hardly moved: a finger's own tremor.
            "down 3.0 3 " + num(x) + " " + num(y) + "\nglide 3.5 3.9 3 " + num(x + 0.02) + " " + num(y) + "\n" +
            // A second finger beside it for a moment, skidding as it taps.
            "down 4.0 4 " + num(x + 0.15) + " " + num(y) + "\nglide 4.02 4.15 4 " + num(x + 0.25) + " " + num(y) + "\nup 4.17 4\nup 4.5 3\nend 5\n";
        testkit::play(script, [&](Session &s) {
            QCOMPARE(s.probe(kSampler, QStringLiteral("trims")), 0.0f);
            const QJsonObject doc = s.saveSong();
            QVERIFY(!tf::songJson(doc).contains("\"trims\""));
        });
    }

    // Pressing the pad down (or Return) keeps nothing going: a chop plays
    // on by itself only once Space has made it a loop.
    void aHardPressKeepsNothingGoing() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const std::string at = l.at(std::round(l.beatOf(0)) + 8);
        const auto take = testkit::play(drop(m_song) + size + "down 1.0 1 " + at + "\nclick 1.3\nkey 1.4 Shift+Return\nup 1.6 1\nend 4\n");
        QVERIFY(take.rms(1.1, 1.5) > 0.02);
        QVERIFY2(take.rms(2.5, 4.0) < 0.002, qPrintable(QString::number(take.rms(2.5, 4.0))));
        QCOMPARE(take.view.parts[kSampler].playing, -1);
    }

    // A chop held round three times, let go, then Space: the loop goes on
    // repeating the chop to its end, as the hold did, rather than falling
    // silent where the finger lifted.
    void aHeldChopLoopsWithoutAGapAtTheEnd() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\n"; // two beats
        const Lay l = layOf(m_song, size);
        const auto take = testkit::play(drop(m_song) + size + "down 1.0 1 " + l.at(std::round(l.beatOf(0)) + 8) +
                                        "\nup 4.55 1\nkey 4.6 Space\nend 16\n");
        const int slot = take.view.parts[kSampler].playing;
        QVERIFY(slot >= 0);
        QCOMPARE(int(take.view.parts[kSampler].clips[slot].lengthBeats), 4); // two of its rounds: a bar
        // Nowhere in two passes of the loop a quarter second of silence.
        double least = 1;
        for (double t = 6.0; t < 15.5; t += 0.25)
            least = std::min(least, take.rms(t, t + 0.25));
        QVERIFY2(least > 0.03, qPrintable(QString::number(least)));
    }

    // Taps a beat apart over three bars, then Space a bar later: a four-bar
    // loop. A tap plays its chop once, so the bar with nothing played in it
    // stays as it was played (the fill is only for held chops), and a held
    // chop that went round fills to the next chop but no further.
    void aHeldChopsFillStopsAtTheNextChop() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\n"; // two beats
        const Lay l = layOf(m_song, size);
        const double b = std::round(l.beatOf(0)) + 8;
        // Held two rounds (2.4 s), let go, a tap of another chop later on.
        const auto take = testkit::watch(drop(m_song) + size + "down 1.0 1 " + l.at(b) + "\nup 3.3 1\ntap 4.0 " + l.at(b + 4) +
                                             "\nkey 4.6 Space\nend 12\n",
                                         [&](double t, Session &s) {
                                             // Never two chops at once: the tap cuts the fill.
                                             if (t > 6.0)
                                                 QVERIFY2(s.view().parts[kSampler].voices <= 1,
                                                          qPrintable(QString::number(s.view().parts[kSampler].voices)));
                                         });
        QVERIFY(take.view.parts[kSampler].playing >= 0);
    }

    // A chop trimmed and then looped: the loop plays the chop as it was
    // trimmed, as the hand was heard playing it going round, not the whole
    // chop it was cut from. Held, its start slid half a beat later (the
    // mouse, one finger), held round, then Space: with the finger still
    // down, or just lifted, after a couple of rounds (the loop then begins
    // where the finger landed, before the slide) or after many.
    void aTrimmedHeldChopLoopsAsTrimmed() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\n"; // two beats
        const Lay l = layOf(m_song, size);
        std::istringstream where(l.at(std::round(l.beatOf(0)) + 8));
        double x = 0, y = 0;
        where >> x >> y;
        std::string held = drop(m_song) + size + "mouse 1.0 " + num(x) + " " + num(y) + " down\n";
        for (int i = 0; i <= 10; ++i)
            held += "mouse " + num(1.5 + 0.02 * i) + " " + num(x + 0.03 * i) + " " + num(y) + " move\n";
        const std::string up = " " + num(x + 0.3) + " " + num(y) + " up\n";
        // The first pass plays out whole (a start slid is heard from the
        // next time round, at 2.2 s); the trimmed rounds after it are 0.9 s.
        struct Case { double space, lift; };
        for (const Case c : {Case{3.3, 3.5}, Case{3.3, 3.25}, Case{6.4, 6.6}}) {
            const std::string script =
                held + (c.lift < c.space ? "mouse " + num(c.lift) + up + "key " + num(c.space) + " Space\n"
                                         : "key " + num(c.space) + " Space\nmouse " + num(c.lift) + up) + "end 12\n";
            loopPlaysAsHeard(script, 2.3, c.space - 0.1, c.space + 0.1);
            if (QTest::currentTestFailed())
                return qWarning("Space at %g, the finger lifted at %g", c.space, c.lift);
        }
    }

    // Trimmed (its end, two fingers), let go, then the trimmed chop tapped
    // and Space: the loop's chops are the trimmed ones, the hold's too.
    void aTrimmedChopTappedLoopsAsTrimmed() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\n"; // two beats
        const Lay l = layOf(m_song, size);
        std::istringstream where(l.at(std::round(l.beatOf(0)) + 8));
        double x = 0, y = 0;
        where >> x >> y;
        // A second finger beside the held one, slid half the pad left: the
        // end half a beat earlier, at once (the pass playing is past it, and
        // goes round). The chop as trimmed is a beat and a half: 0.9 s.
        const std::string trimmed = drop(m_song) + size + "down 1.0 1 " + num(x) + " " + num(y) + "\ndown 1.5 2 " + num(x + 0.1) +
                                    " " + num(y) + "\nglide 1.8 2.1 2 " + num(x - 0.45) + " " + num(y) + "\nup 2.3 2\nup 2.35 1\n";
        for (const int taps : {1, 6}) {
            std::string script = trimmed;
            for (int i = 0; i < taps; ++i)
                script += "tap " + num(2.9 + 0.9 * i) + " " + num(x) + " " + num(y) + "\n";
            const double space = 2.9 + 0.9 * taps - 0.02;
            loopPlaysAsHeard(script + "key " + num(space) + " Space\nend " + num(space + 7) + "\n", 2.15, space - 0.1, space + 0.1);
            if (QTest::currentTestFailed())
                return qWarning("%d taps", taps);
        }
    }

    // The visualizer hears the Sampler: a loop of a beat with a kick moves
    // the picture's kick, with the drum page silent.
    void theVisualizerHearsTheSampler() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const Lay l = layOf(m_song, size);
        const double b = std::round(l.beatOf(0)) + 8;
        std::string script = drop(m_song) + size;
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(1.0 + 0.6 * i) + " " + l.at(b + i) + "\n";
        script += "key 3.4 Space\n";
        visual::SignalTracker tracker;
        int first = -1;
        testkit::watch(script + "end 9\n", [&](double t, Session &s) {
            tracker.update(s.view(), 1.0 / 125);
            if (t > 4.0 && first < 0)
                first = tracker.now().kicks;
        });
        QVERIFY(first >= 0);
        QVERIFY2(tracker.now().kicks - first >= 4, qPrintable(QStringLiteral("%1 kicks").arg(tracker.now().kicks - first)));
        QVERIFY(tracker.now().level[visual::Sampler] > 0.1f);
    }

    // The loop is what was being played, once: a chop held round makes a
    // loop as long as the chop; two chops played in turn, the two of them.
    // (Other pages take two rounds of a short pattern, for the hand's feel;
    // a chop is a phrase already.)
    void spaceLoopsOneRoundOfWhatWasPlayed() {
        const std::string twoBeats = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\n";
        const std::string bar = twoBeats + "key 0.35 Shift+Right\n";
        const Lay l2 = layOf(m_song, twoBeats), l4 = layOf(m_song, bar);
        auto loopBeats = [&](const testkit::Take &t) {
            const PartView &pv = t.view.parts[kSampler];
            return pv.playing >= 0 ? int(pv.clips[pv.playing].lengthBeats) : -1;
        };
        // Held five rounds of two beats (6 s at 100), Space as the sixth
        // begins: two of its rounds, a bar.
        QCOMPARE(loopBeats(testkit::play(drop(m_song) + twoBeats + "down 1.0 1 " + l2.at(std::round(l2.beatOf(0)) + 8) +
                                          "\nkey 7.0 Space\nup 7.2 1\nend 9\n")),
                 4);
        // A bar-long chop held three rounds: a bar.
        QCOMPARE(loopBeats(testkit::play(drop(m_song) + bar + "down 1.0 1 " + l4.at(std::round(l4.beatOf(0)) + 8) +
                                          "\nkey 8.2 Space\nup 8.4 1\nend 10\n")),
                 4);
        // Two bar-long chops in turn, three times over: two bars.
        std::string turns = drop(m_song) + bar;
        for (int i = 0; i < 6; ++i)
            turns += "tap " + num(1.0 + 2.4 * i) + " " + l4.at(std::round(l4.beatOf(0)) + 8 + 4 * (i % 2)) + "\n";
        QCOMPARE(loopBeats(testkit::play(turns + "key 15.4 Space\nend 17\n")), 8);
    }

    // The hand goes on playing as Space lands the loop: where it plays the
    // chop the loop plays at the same moment, one is heard, not two.
    void theHandPlayingOnAfterSpaceDoesntDoubleTheLoop() {
        const std::string bar = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n";
        const Lay l = layOf(m_song, bar);
        const double b = std::round(l.beatOf(0)) + 8;
        std::string script = drop(m_song) + bar;
        // Two bar-long chops in turn, then Space, and the hand plays on: a
        // little early, a little late, on the loop's own chops.
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(1.0 + 2.4 * i) + " " + l.at(b + 4 * (i % 2)) + "\n";
        script += "key 10.6 Space\ntap 12.97 " + l.at(b + 4) + "\ntap 15.44 " + l.at(b) + "\nend 17\n";
        int most = 0;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            // The same chop sounding twice at once (a cut one's few
            // milliseconds of fade aside; the loop's last chop running out
            // as the hand's next begins is no double).
            std::map<int, int> same;
            for (int i = 0; i < kVoices; ++i) {
                const float *d = s.view().parts[kSampler].data + pub::First + i * pub::Per;
                if (d[pub::Handle] > 0)
                    most = t > 11.0 ? std::max(most, ++same[int(std::lround(d[pub::Start] * 100))]) : most;
            }
        });
        QVERIFY(take.view.parts[kSampler].playing >= 0);
        QVERIFY2(most <= 1, qPrintable(QString::number(most)));
    }

    // A chop held round a long while earlier, then other chops played and
    // looped: the loop has those, not the old held one.
    void anOldHeldChopIsntCarriedIntoALaterLoop() {
        const std::string bar = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n";
        const Lay l = layOf(m_song, bar);
        const double b = std::round(l.beatOf(0)) + 8;
        // (The metronome on: the take is made on the studio's bars, where
        // what is still held is carried into the loop.)
        std::string script = drop(m_song) + bar + "key 0.5 Ctrl+M\ndown 1.0 1 " + l.at(b + 16) + "\nup 8.3 1\n";
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(10.0 + 2.4 * i) + " " + l.at(b + 4 * (i % 2)) + "\n";
        script += "key 19.6 Space\nend 27\n";
        const double held = l.first + (b + 16) * l.beat; // the held chop's first sample
        bool heard = false;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            if (t < 20.0)
                return;
            for (int i = 0; i < kVoices; ++i) {
                const float *d = s.view().parts[kSampler].data + pub::First + i * pub::Per;
                heard |= d[pub::Handle] > 0 && std::fabs(d[pub::Start] - (b + 16)) < 0.3;
            }
        });
        (void)held;
        QVERIFY(take.view.parts[kSampler].playing >= 0);
        QVERIFY2(!heard, "the old held chop plays in the loop");
    }

    // A loop Space made is taken back by Ctrl+Z, and is silent after it.
    void ctrlZTakesBackTheSamplersLoop() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const double b = std::round(l.beatOf(0)) + 8;
        std::string script = drop(m_song) + size;
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(1.0 + 0.6 * i) + " " + l.at(b + (i % 2 ? 2 : 0)) + "\n";
        script += "key 3.4 Space\nkey 6.0 Ctrl+Z\nend 9\n";
        const auto take = testkit::play(script);
        QVERIFY(take.rms(4.5, 5.8) > 0.02);
        QCOMPARE(take.view.parts[kSampler].playing, -1);
        for (const ClipView &c : take.view.parts[kSampler].clips)
            QCOMPARE(int(c.state), int(ClipView::Empty));
        QVERIFY2(take.rms(6.8, 9.0) < 0.002, qPrintable(QString::number(take.rms(6.8, 9.0))));
    }

    void spaceLoopsTheChopsJustPlayed() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const double b = std::round(l.beatOf(0)) + 8;
        // Four chops, a beat apart, then Space where the first would come again.
        std::string script = drop(m_song) + size;
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(1.0 + 0.6 * i) + " " + l.at(b + (i % 2 ? 2 : 0)) + "\n";
        const auto take = testkit::play(script + "key 3.4 Space\nend 9\n");
        QVERIFY(take.view.parts[kSampler].playing >= 0);
        // Two chops in turn, played twice: two beats a round, so two rounds
        // of it, a bar (a loop of chops is never much under a bar).
        QCOMPARE(int(take.view.parts[kSampler].clips[take.view.parts[kSampler].playing].lengthBeats), 4);
        // It plays on by itself, in the song's time.
        QVERIFY(take.rms(5.0, 6.0) > 0.02);
        QVERIFY(take.rms(8.0, 9.0) > 0.02);
        QVERIFY2(std::fabs(take.view.bpm - 100.0) < 0.5, qPrintable(QString::number(take.view.bpm)));
    }

    // The slot keys its guide names, as on every page: Backspace stops the
    // loop, F1 launches it again, Shift+F1 clears the slot.
    void theSlotKeysStopLaunchAndClear() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const double b = std::round(l.beatOf(0)) + 8;
        std::string script = drop(m_song) + size;
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(1.0 + 0.6 * i) + " " + l.at(b + (i % 2 ? 2 : 0)) + "\n";
        script += "key 3.4 Space\nkey 5.0 Backspace\n";
        const auto stopped = testkit::play(script + "end 8\n");
        QCOMPARE(stopped.view.parts[kSampler].playing, -1);
        QVERIFY(stopped.view.parts[kSampler].clips[0].state != ClipView::Empty);
        const auto again = testkit::play(script + "key 8.0 F1\nend 11\n");
        QCOMPARE(again.view.parts[kSampler].playing, 0);
        const auto cleared = testkit::play(script + "key 8.0 Shift+F1\nend 9\n");
        QCOMPARE(int(cleared.view.parts[kSampler].clips[0].state), int(ClipView::Empty));
    }

    void aSongFileBringsTheSongBackWithItsLoop() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const double b = std::round(l.beatOf(0)) + 8;
        std::string script = drop(m_song) + size;
        for (int i = 0; i < 4; ++i)
            script += "tap " + num(1.0 + 0.6 * i) + " " + l.at(b + i) + "\n";
        script += "key 3.4 Space\nkey 3.6 Period\nend 6\n";
        testkit::reopen(script, 3.0, [&](Session &, Session &opened, const QJsonObject &doc) {
            // The file names the song, not its sound.
            QVERIFY(tf::songJson(doc).size() < 200000);
            QCOMPARE(opened.probe(kSampler, QStringLiteral("songs")), 1.0f);
            QCOMPARE(opened.probe(kSampler, QStringLiteral("size")), float(Beat));
            QCOMPARE(opened.probe(kSampler, QStringLiteral("tune")), 1.0f);
            QVERIFY2(std::fabs(opened.probe(kSampler, QStringLiteral("gridBeat")) - 28800.0f) < 30.0f,
                     qPrintable(QString::number(opened.probe(kSampler, QStringLiteral("gridBeat")))));
            // And its loop sounds.
            QVERIFY(opened.view().parts[kSampler].playing >= 0);
            QVERIFY2(opened.view().parts[kSampler].channel.peak > 0.02f,
                     qPrintable(QString::number(opened.view().parts[kSampler].channel.peak)));
        });
    }

    void aSongFileWhoseSongIsGoneSaysSoAndKeepsItsPlace() {
        const QString gone = write(QStringLiteral("gone.wav"), song(100, 8));
        const std::string script = drop(gone) + "tap 1.0 0.03 0.9\nend 2\n";
        testkit::play(script, [&](Session &before) {
            const QJsonObject doc = before.saveSong();
            QFile::remove(gone);
            tf::Studio studio(48000.0f);
            tf::Session opened(&studio);
            opened.setClock(&tf::scriptClock);
            QString error;
            QVERIFY(opened.openSong(doc, &error));
            QCOMPARE(opened.probe(kSampler, QStringLiteral("songs")), 0.0f);
            uint32_t seq = 0;
            QVERIFY2(opened.notice(&seq).contains(QStringLiteral("gone.wav")), qPrintable(opened.notice(&seq)));
            // Saved again, the song is still named: nothing is lost by
            // opening a song file on a computer without the sound.
            const QJsonObject again = opened.saveSong();
            QVERIFY(tf::songJson(again).contains("gone.wav"));
            // Dropped in again, it is back in its place.
            write(QStringLiteral("gone.wav"), song(100, 8));
            QVERIFY(opened.drop(gone));
            QCOMPARE(opened.probe(kSampler, QStringLiteral("songs")), 1.0f);
        });
    }

    void stemsAlreadyMadeAreReadAndChosenWithTheArrows() {
        // The stems as the separator leaves them: here the drums are the
        // whole song and the others silence, so each is told by ear.
        const QString path = write(QStringLiteral("stemmed.wav"), song(100, 8));
        const QString dir = QString::fromStdString(cacheFolder(path.toStdString())) + QStringLiteral("/stems");
        QVERIFY(QDir().mkpath(dir));
        const std::vector<int16_t> all = song(100, 8), none(all.size(), 0);
        for (int s = Drums; s < kStems; ++s) {
            const std::vector<int16_t> &d = s == Drums ? all : none;
            QVERIFY(writeWavStereo(QFile::encodeName(dir + '/' + stemFile(s) + ".wav").toStdString(), d.data(), d.size() / 2, kRate));
        }
        const Lay l = layOf(path);
        const std::string at = l.at(std::round(l.beatOf(0)) + 4);
        const auto take = testkit::play(drop(path) + "tap 1.0 " + at + "\nkey 2.0 Right\ntap 2.5 " + at + "\nkey 3.5 Right\ntap 4.0 " + at +
                                        "\nend 5\n",
                                        [](Session &s) {
                                            QCOMPARE(s.probe(kSampler, QStringLiteral("stems")), 1.0f);
                                            QCOMPARE(s.probe(kSampler, QStringLiteral("stem")), float(Bass));
                                        });
        QVERIFY(take.rms(1.02, 1.4) > 0.02); // the mix
        QVERIFY(take.rms(2.52, 2.9) > 0.02); // the drums
        QVERIFY2(take.rms(4.02, 4.4) < 0.001, qPrintable(QString::number(take.rms(4.02, 4.4)))); // the bass: silence
    }

    void sPutsTheSongThroughTheModelAndKeepsItsStems() {
        // The real separator, when its weights are on this machine: S makes
        // the four stems, leaves them in the song's cache folder, and the
        // arrows then have them to choose from.
        QString model = qEnvironmentVariable("TENFOLD_STEMS_MODEL");
        if (model.isEmpty())
            model = QStringLiteral(TENFOLD_SRC "/../build/htdemucs.tfq");
        if (!QFile::exists(model))
            QSKIP("no htdemucs.tfq here (bin/model copies it)");
        qputenv("TENFOLD_STEMS_MODEL", QFile::encodeName(model));
        qputenv("TENFOLD_STEMS_IN_SCRIPTS", "1");
        const QString path = write(QStringLiteral("separated.wav"), song(120, 4));
        const QString dir = QString::fromStdString(cacheFolder(path.toStdString())) + QStringLiteral("/stems");
        testkit::play(drop(path) + "key 1.0 S\nkey 1.5 Right\nend 2\n", [](Session &s) {
            QCOMPARE(s.probe(kSampler, QStringLiteral("stems")), 1.0f);
            QCOMPARE(s.probe(kSampler, QStringLiteral("stem")), float(Drums));
        });
        qunsetenv("TENFOLD_STEMS_MODEL");
        qunsetenv("TENFOLD_STEMS_IN_SCRIPTS");
        for (int st = Drums; st < kStems; ++st) {
            std::vector<int16_t> pcm;
            int rate = 0;
            QVERIFY(readWavStereo(QFile::encodeName(dir + '/' + stemFile(st) + ".wav").toStdString(), pcm, rate));
            QCOMPARE(rate, kRate);
            QCOMPARE(pcm.size(), song(120, 4).size());
        }
        QVERIFY(QDir(dir).entryList({QStringLiteral(".*.part")}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void aRestoredSongIsReadAndOGoesBackAndForth() {
        // The song restored as the restorer leaves it, here silence, so the
        // two are told by ear: it is read with the song and played, and O
        // goes to the original and back.
        const QString path = write(QStringLiteral("restoredbefore.wav"), song(100, 8));
        const QString cache = QString::fromStdString(cacheFolder(path.toStdString()));
        QVERIFY(QDir().mkpath(cache));
        const std::vector<int16_t> none(song(100, 8).size(), 0);
        QVERIFY(writeWavStereo(QFile::encodeName(cache + QStringLiteral("/restored.wav")).toStdString(), none.data(),
                               none.size() / 2, kRate));
        const Lay l = layOf(path);
        const std::string at = l.at(std::round(l.beatOf(0)) + 4);
        float first = 0, between = 0, last = 0;
        const auto take = testkit::watch(drop(path) + "tap 1.0 " + at + "\nkey 2.0 O\ntap 2.5 " + at + "\nkey 3.5 O\ntap 4.0 " + at +
                                             "\nend 5\n",
                                         [&](double t, Session &s) {
                                             const float r = s.probe(kSampler, QStringLiteral("restored"));
                                             (t < 2.0 ? first : t < 3.5 ? between : last) = r;
                                         });
        QCOMPARE(first, 1.0f);   // restored
        QCOMPARE(between, 0.5f); // O: the original
        QCOMPARE(last, 1.0f);    // O: restored again
        QVERIFY2(take.rms(1.02, 1.4) < 0.001, qPrintable(QString::number(take.rms(1.02, 1.4)))); // restored: silence
        QVERIFY(take.rms(2.52, 2.9) > 0.02);                                                    // the original
        QVERIFY2(take.rms(4.02, 4.4) < 0.001, qPrintable(QString::number(take.rms(4.02, 4.4)))); // restored again
    }

    void aLossySongIsRestoredByItselfAndKept() {
        // The real restorer, when its weights are on this machine: a song
        // whose file says it went through a lossy codec (WAV inside, which
        // the page reads without ffmpeg) is restored as it comes in, and the
        // restored song is kept in its cache folder.
        QString model = qEnvironmentVariable("TENFOLD_RESTORE_MODEL");
        if (model.isEmpty())
            model = QStringLiteral(TENFOLD_SRC "/../build/apollo.tfq");
        if (!QFile::exists(model))
            QSKIP("no apollo.tfq here (bin/model copies it)");
        qputenv("TENFOLD_RESTORE_MODEL", QFile::encodeName(model));
        qputenv("TENFOLD_RESTORE_IN_SCRIPTS", "1");
        const QString path = write(QStringLiteral("lossy.mp3"), song(120, 1));
        const QString cache = QString::fromStdString(cacheFolder(path.toStdString()));
        float restored = 0, afterO = 0;
        testkit::watch(drop(path) + "key 1.0 O\nend 1.2\n", [&](double t, Session &s) {
            (t < 1.0 ? restored : afterO) = s.probe(kSampler, QStringLiteral("restored"));
        });
        qunsetenv("TENFOLD_RESTORE_MODEL");
        qunsetenv("TENFOLD_RESTORE_IN_SCRIPTS");
        QCOMPARE(restored, 1.0f);
        QCOMPARE(afterO, 0.5f); // O: the original
        std::vector<int16_t> pcm;
        int rate = 0;
        QVERIFY(readWavStereo(QFile::encodeName(cache + QStringLiteral("/restored.wav")).toStdString(), pcm, rate));
        QCOMPARE(rate, kRate);
        QCOMPARE(pcm.size(), song(120, 1).size());
        QVERIFY(QDir(cache).entryList({QStringLiteral(".*.part")}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void withoutStemsTheArrowsSayHowToGetThem() {
        testkit::play(drop(m_song) + "key 1.0 Right\nend 1.2\n", [](Session &s) {
            QCOMPARE(s.probe(kSampler, QStringLiteral("stem")), float(Mix));
            uint32_t seq = 0;
            QVERIFY2(s.notice(&seq).contains(QStringLiteral("stems")), qPrintable(s.notice(&seq)));
        });
    }

    void tunedUpTheSongKeepsItsTime() {
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\nkey 0.3 Shift+Right\nkey 0.35 Shift+Right\n";
        const Lay l = layOf(m_song, size);
        const std::string at = l.at(std::round(l.beatOf(0)) + 8);
        std::string up;
        for (int i = 0; i < 5; ++i)
            up += "key " + num(0.5 + 0.05 * i) + " Period\n";
        const auto plain = testkit::play(drop(m_song) + size + "tap 1.0 " + at + "\nend 3.6\n");
        const auto tuned = testkit::play(drop(m_song) + size + up + "tap 1.0 " + at + "\nend 3.6\n");
        // The bass note (55 Hz, with its octave) a fourth higher: more
        // crossings of zero in the same stretch, by about that ratio.
        auto crossings = [](const testkit::Take &t) {
            // Under the hats: a low-pass at about 300 Hz first.
            int n = 0;
            float lp = 0, was = 0;
            for (size_t i = size_t(1.1 * 48000); i < size_t(3.3 * 48000); ++i) {
                lp += (t.left[i] - lp) * 0.04f;
                n += (lp > 0) != (was > 0);
                was = lp;
            }
            return double(n);
        };
        const double ratio = crossings(tuned) / crossings(plain);
        QVERIFY2(ratio > 1.2 && ratio < 1.5, qPrintable(QString::number(ratio)));
        // And the bar is still a bar: it ends when the plain one does.
        QVERIFY(tuned.rms(3.0, 3.35) > 0.01);
        QVERIFY2(tuned.rms(3.5, 3.6) < 0.002, qPrintable(QString::number(tuned.rms(3.5, 3.6))));
    }

    // A tempo heard at half or double: D doubles it, H halves it, the bars
    // are laid out again, and a loop made before still sounds the same.
    void theTempoCanBeDoubledOrHalvedAndLoopsKeepTheirSound() {
        const Lay l = layOf(m_song, "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n");
        const std::string size = "key 0.2 Shift+Right\nkey 0.25 Shift+Right\n";
        const double b = std::round(l.beatOf(0)) + 8;
        std::string loop = drop(m_song) + size;
        for (int i = 0; i < 4; ++i)
            loop += "tap " + num(1.0 + 0.6 * i) + " " + l.at(b + (i % 2 ? 2 : 0)) + "\n";
        loop += "key 3.4 Space\n";
        testkit::play(drop(m_song) + "key 0.5 D\nend 0.7\n", [&](Session &s) {
            QVERIFY2(std::fabs(s.probe(kSampler, QStringLiteral("bpm")) - 200.0f) < 0.3f,
                     qPrintable(QString::number(s.probe(kSampler, QStringLiteral("bpm")))));
            QVERIFY2(std::fabs(s.probe(kSampler, QStringLiteral("gridBeat")) - 14400.0f) < 20.0f,
                     qPrintable(QString::number(s.probe(kSampler, QStringLiteral("gridBeat")))));
            // Twice the beats, so twice the rows.
            QVERIFY(s.probe(kSampler, QStringLiteral("rows")) >= 2 * l.rows - 1);
        });
        testkit::play(drop(m_song) + "key 0.5 H\nend 0.7\n", [&](Session &s) {
            QVERIFY2(std::fabs(s.probe(kSampler, QStringLiteral("bpm")) - 50.0f) < 0.1f,
                     qPrintable(QString::number(s.probe(kSampler, QStringLiteral("bpm")))));
            // Its bar lines are still the song's: the grid's first beat on a kick.
            const double first = s.probe(kSampler, QStringLiteral("gridFirst")), step = s.probe(kSampler, QStringLiteral("gridBeat"));
            const double atZero = -first / step;
            QVERIFY2(std::fabs(atZero - std::round(atZero)) < 0.03, qPrintable(QString::number(atZero)));
        });
        // The loop, with and without the tempo doubled after it was made:
        // the same sound.
        const auto plain = testkit::play(loop + "end 9\n");
        const auto doubled = testkit::play(loop + "key 4.0 D\nend 9\n");
        double diff = 0, sum = 0;
        for (size_t i = size_t(6.0 * 48000); i < size_t(8.5 * 48000); ++i) {
            diff += std::pow(double(plain.left[i]) - doubled.left[i], 2);
            sum += std::pow(double(plain.left[i]), 2);
        }
        QVERIFY(sum > 1.0);
        QVERIFY2(diff < 0.01 * sum, qPrintable(QStringLiteral("%1 of %2").arg(diff).arg(sum)));
    }

    void thePagesOfALongSongTurnWithTheArrows() {
        // 24 bars: five rows of four bars, and a second page for the rest.
        const Lay l = layOf(m_song);
        QVERIFY(l.rows >= 6);
        const double far = std::round(l.beatOf(0)) + 22 * 4; // the 23rd bar
        QCOMPARE(l.pageOf(far), 1);
        const auto take = testkit::play(drop(m_song) + "key 0.5 Down\ntap 1.0 " + l.at(far) + "\nend 2\n",
                                        [](Session &s) { QCOMPARE(s.probe(kSampler, QStringLiteral("page")), 1.0f); });
        QVERIFY(take.rms(1.02, 1.4) > 0.02);
    }

    void aFileThatIsNoSoundSaysSo() {
        const QString path = m_dir.filePath(QStringLiteral("notes.txt"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a song");
        f.close();
        // (With ffmpeg here it is asked, and says no; without, the page does.)
        testkit::play(drop(path) + "end 0.5\n", [](Session &s) {
            QCOMPARE(s.probe(kSampler, QStringLiteral("songs")), 0.0f);
            uint32_t seq = 0;
            QVERIFY(!s.notice(&seq).isEmpty());
        });
        testkit::play("drop 0.1 /nowhere/at/all.wav\nend 0.3\n", [](Session &s) {
            QCOMPARE(s.probe(kSampler, QStringLiteral("songs")), 0.0f);
            uint32_t seq = 0;
            QVERIFY2(s.notice(&seq).contains(QStringLiteral("Not a sound file")), qPrintable(s.notice(&seq)));
        });
    }

    void thePageDrawsEmptyLoadedAndPlaying() {
        const Lay l = layOf(m_song);
        const std::string at = l.at(std::round(l.beatOf(0)) + 4);
        auto draw = [](Session &s) {
            QImage img(1200, 800, QImage::Format_ARGB32);
            img.fill(Qt::black);
            QPainter painter(&img);
            s.paint(painter, QRectF(30, 14, 1100, 690), 1.5);
            painter.end();
            // Something of the page's own colour is on it.
            int lit = 0;
            for (int y = 20; y < 700; y += 7)
                for (int x = 40; x < 1120; x += 5)
                    lit += qBlue(img.pixel(x, y)) > 120;
            return lit;
        };
        int empty = 0, loaded = 0;
        testkit::play("page 0.05 sampler\nend 0.2\n", [&](Session &s) { empty = draw(s); });
        testkit::play(drop(m_song) + "down 1.0 1 " + at + "\nend 1.5\n", [&](Session &s) { loaded = draw(s); });
        QVERIFY(empty > 20);
        QVERIFY2(loaded > 4 * empty, qPrintable(QStringLiteral("%1 %2").arg(empty).arg(loaded)));
    }

    // Ten fingers, each repeating a chop of a song stretched to another
    // tempo and moved to another key: the worst the engine is asked for.
    void tenFingersHeldStayCheap() {
        TENFOLD_TIMED();
        const Lay l = layOf(m_song);
        std::string script = drop(m_song) + "tempo 0.5 112\nkey 0.6 Period\nkey 0.65 Period\n";
        // Two to a row, no two side by side (those would be one hold).
        for (int i = 0; i < 10; ++i)
            script += "down " + num(1.0 + 0.01 * i) + " " + std::to_string(i + 1) + " " +
                      num(0.1 + 0.5 * (i % 2) + 0.2 * ((i / 2) % 2)) + " " + num(0.9 - 0.2 * (i / 2)) + "\n";
        script += "end 9\n";
        const QString out = m_dir.filePath(QStringLiteral("load.wav"));
        const RenderStats st = renderScript(script, out.toStdString());
        QVERIFY2(st.error.empty(), st.error.c_str());
        QVERIFY(st.maxVoices >= 10);
        // Of a 128-frame callback: the whole studio with nothing else in it.
        // (About 0.04 on a quiet desktop; CI's shared runners measure 0.06 to
        // 0.07 for the same work.)
        QVERIFY2(st.meanLoad < 0.12, qPrintable(QStringLiteral("mean %1 p99 %2").arg(st.meanLoad).arg(st.p99Load)));
    }
};

TESTKIT_REGISTER(SamplerTests)
#include "sampler_tests.moc"
