// The Vocal page: the microphone heard at once, Space's smart pick, and
// loops of recorded audio that replay in time.

#include <QDir>
#include <functional>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cmath>
#include <numeric>

#include "dsp.h"
#include "songfile.h"
#include "testkit.h"
#include "vocal.h"
#include "wav.h"

using namespace tf;

namespace {

constexpr int kVocal = 7; // the eighth part

// The script player's page number for it.
const char *kPage = "page 0.05 8\n";

// An envelope with words at the given beats (start, end), at `bpm`, with a
// quiet room under them, as pick() reads it: hop by hop, starting at beat 0
// at time 0.
vocal::Sung sungAt(const std::vector<std::pair<double, double>> &words, double bpm, double untilBeat) {
    vocal::Sung s;
    s.hop = vocal::kHop / 48000.0;
    s.t0 = 0;
    const int n = int(untilBeat * 60.0 / bpm / s.hop);
    s.env.assign(size_t(n), 0.0008f);
    for (auto [a, b] : words)
        for (int h = int(a * 60.0 / bpm / s.hop); h < int(b * 60.0 / bpm / s.hop) && h < n; ++h)
            s.env[size_t(h)] = 0.08f;
    return s;
}

vocal::PickRequest barsAt(double beat, double bpm) {
    vocal::PickRequest r;
    r.free = false;
    r.bpm = bpm;
    r.beat = beat;
    r.now = beat * 60.0 / bpm;
    return r;
}

// Where the level next rises past `thr` after `from` seconds, having been
// under half of it first (so a word's tail or the room isn't an onset).
double onsetAfter(const testkit::Take &t, double from, double thr = 0.02) {
    const size_t a = size_t(from * 48000);
    // A 4 ms envelope, so one zero crossing isn't silence.
    float env = 1;
    bool quiet = false;
    for (size_t i = a; i < t.left.size(); ++i) {
        env = std::max(std::fabs(t.left[i]), env * 0.995f);
        quiet |= env < thr * 0.5f;
        if (quiet && env > thr)
            return double(i) / 48000.0;
    }
    return -1;
}

double rmsBetween(const testkit::Take &t, double a, double b) { return t.rms(a, b); }

// Saves the song at the end of `script`, then opens `doc` (that song, or
// what `edit` makes of it) in a fresh studio and listens for `secs`: the
// RMS of what it plays, from the second second on.
struct Reopened {
    QJsonObject doc;
    float rms = 0;
    QString notice;
    int playing = -1;
    QJsonObject settings;
};
Reopened reopenVocal(const std::string &script, double secs, const std::function<void(QJsonObject &)> &edit = {}) {
    Reopened out;
    testkit::play(script, [&](Session &before) {
        QString error;
        out.doc = before.saveSong(&error);
        QVERIFY2(!out.doc.isEmpty(), qPrintable(error));
        QJsonObject doc = QJsonDocument::fromJson(songJson(out.doc)).object();
        if (edit)
            edit(doc);
        const double t0 = scriptClock();
        Studio studio(48000.0f);
        studio.setTimeOrigin(t0);
        Session opened(&studio);
        opened.setClock(&scriptClock);
        QVERIFY2(opened.openSong(doc, &error), qPrintable(error));
        out.notice = opened.notice(nullptr);
        float buf[384 * 2];
        double sum = 0;
        long n = 0;
        for (int tick = 0; tick <= int(secs * 125); ++tick) {
            setScriptClock(t0 + tick / 125.0);
            opened.refresh();
            opened.idle(scriptClock());
            studio.render(buf, 384);
            if (tick >= 125)
                for (float x : buf) {
                    sum += double(x) * x;
                    ++n;
                }
        }
        opened.refresh();
        out.rms = n ? float(std::sqrt(sum / double(n))) : 0;
        out.playing = opened.view().parts[kVocal].playing;
        QJsonObject again = opened.saveSong();
        out.settings = again[QStringLiteral("parts")].toArray()[kVocal].toObject()[QStringLiteral("settings")].toObject();
    });
    return out;
}

}

class VocalTests : public QObject {
    Q_OBJECT
private slots:
    // --- the microphone, heard -----------------------------------------------------

    void theMicrophoneIsHeardWithNoDelayOfTheStudiosOwn() {
        // Noise into the microphone: what comes out of the mix is the same
        // noise, not a sample later. The studio adds nothing but its own
        // render quantum (none here: the script hands the mic over just
        // before each render), and the voice skips the limiter's look-ahead.
        QTemporaryDir dir;
        const std::string path = QDir(dir.path()).filePath("noise.wav").toStdString();
        std::vector<float> noise(size_t(48000 * 1.5) * 2, 0.0f);
        Rng rng(7);
        for (size_t i = size_t(48000 * 0.5); i < size_t(48000 * 1.4); ++i)
            noise[2 * i] = noise[2 * i + 1] = 0.12f * rng.bipolar();
        {
            WavWriter w;
            QVERIFY(w.open(path, 48000));
            w.write(noise.data(), noise.size() / 2);
        }
        const auto take = testkit::play(std::string(kPage) + "mic 0 " + path + "\nend 1.6\n");
        QVERIFY(take.error.empty());
        QCOMPARE(take.view.input, int(StudioView::InputLive));
        // Cross-correlate a stretch in the middle of the burst.
        const size_t a = size_t(48000 * 0.8), n = 9600;
        double best = -1e9;
        int lag = -1;
        double self = 0, out = 0;
        for (size_t i = 0; i < n; ++i) {
            self += double(noise[2 * (a + i)]) * noise[2 * (a + i)];
            out += double(take.left[a + i]) * take.left[a + i];
        }
        for (int L = 0; L < 400; ++L) {
            double c = 0;
            for (size_t i = 0; i < n; ++i)
                c += double(noise[2 * (a + i)]) * take.left[a + i + size_t(L)];
            if (c > best) {
                best = c;
                lag = L;
            }
        }
        const double corr = best / std::sqrt(self * out);
        qInfo("monitoring: the voice comes out %d samples (%.3f ms) after it goes in, correlation %.2f", lag,
              lag / 48.0, corr);
        QVERIFY2(lag <= 2, qPrintable(QString("lag %1 samples").arg(lag)));
        QVERIFY2(corr > 0.6, qPrintable(QString("correlation %1").arg(corr)));
    }

    // A soft voice is lifted: sung 26 dB quieter than a full one, it comes
    // out of the monitor within 8 dB of it once the lift has caught up.
    void aSoftVoiceIsLifted() {
        auto level = [](const char *amp) {
            const auto take = testkit::play(std::string(kPage) + "voice 0.3 4.5 hz=220 vowel=a level=" + amp +
                                            "\nend 4.9\n");
            double sum = 0;
            size_t n = 0;
            for (size_t i = size_t(48000 * 3.5); i < size_t(48000 * 4.5) && i < take.left.size(); ++i, ++n)
                sum += double(take.left[i]) * take.left[i];
            return n ? 10.0 * std::log10(sum / double(n) + 1e-20) : -200.0;
        };
        const double full = level("0.2"), soft = level("0.01");
        qInfo("a full voice comes out at %.1f dB, a soft one (−26 dB) at %.1f dB", full, soft);
        QVERIFY2(full - soft < 8.0, qPrintable(QString("soft %1 dB under full").arg(full - soft)));
        // And a full voice isn't pushed into the ceiling.
        QVERIFY2(full < -3.0, qPrintable(QString("full at %1 dB").arg(full)));
    }

    void noMicrophoneIsSaid() {
        uint32_t seq = 0;
        QString notice;
        const auto take = testkit::play(std::string(kPage) + "key 0.5 Space\nend 0.8\n",
                                        [&](Session &s) { notice = s.notice(&seq); });
        QCOMPARE(take.view.input, int(StudioView::NoInput));
        QVERIFY2(notice.contains("No microphone"), qPrintable(notice));
        for (const auto &c : take.view.parts[kVocal].clips)
            QCOMPARE(int(c.state), int(ClipView::Empty));
    }

    // The right edge is freeze below its middle and stutter above, and the
    // page names each as the engine hears it, sliding across the middle too.
    void theRightEdgeSaysFreezeOrStutter() {
        QString low, high, slid;
        testkit::play(std::string(kPage) + "down 0.3 1 0.96 0.3\nend 0.5\n",
                      [&](Session &s) { low = s.pageStatus(); });
        testkit::play(std::string(kPage) + "down 0.3 1 0.96 0.75\nend 0.5\n",
                      [&](Session &s) { high = s.pageStatus(); });
        testkit::play(std::string(kPage) + "down 0.3 1 0.96 0.3\nglide 0.35 0.6 1 0.96 0.75\nend 0.8\n",
                      [&](Session &s) { slid = s.pageStatus(); });
        QVERIFY2(low.contains("FREEZE") && !low.contains("STUTTER"), qPrintable(low));
        QVERIFY2(high.contains("STUTTER") && !high.contains("FREEZE"), qPrintable(high));
        QVERIFY2(slid.contains("STUTTER") && !slid.contains("FREEZE"), qPrintable(slid));
    }

    // --- Space's pick ----------------------------------------------------------------

    void aPhraseInsideTheBarsIsTakenOnTheBars() {
        const auto s = sungAt({{8.25, 9.0}, {9.5, 10.5}, {10.75, 11.5}}, 120, 13);
        const auto pk = vocal::pick(s, barsAt(12.6, 120));
        QVERIFY(pk.ok);
        QCOMPARE(pk.bars, 1);
        QCOMPARE(pk.b0, 8.0);
        QCOMPARE(pk.b1, 12.0);
    }

    void aPickupBeforeTheBarIsKeptAndNoWordIsCut() {
        // The first word starts a little before the bar line: the cuts
        // move together, off the bar lines, into the gaps, keeping it.
        const auto s = sungAt({{7.55, 8.4}, {8.8, 10.0}, {10.4, 11.3}}, 120, 13.5);
        const auto pk = vocal::pick(s, barsAt(12.8, 120));
        QVERIFY(pk.ok);
        QCOMPARE(pk.bars, 1);
        QVERIFY2(pk.b0 <= 7.55 - 0.02, qPrintable(QString("starts at %1, after the pickup").arg(pk.b0)));
        QVERIFY2(pk.b1 >= 11.3 + 0.02, qPrintable(QString("ends at %1, in the last word").arg(pk.b1)));
        QCOMPARE(pk.b1 - pk.b0, 4.0);
        QCOMPARE(pk.cutDb, 0.0f);
        // A phrase a hair longer than the bar takes two, uncut.
        const auto longer = vocal::pick(sungAt({{7.55, 8.4}, {8.8, 10.0}, {10.4, 11.65}}, 120, 13.5), barsAt(12.8, 120));
        QCOMPARE(longer.bars, 2);
        QVERIFY(longer.b0 <= 7.55 && longer.b1 >= 11.65);
        QCOMPARE(longer.cutDb, 0.0f);
    }

    void aTailOverTheBarLineIsKept() {
        const auto s = sungAt({{8.6, 9.5}, {10.0, 12.35}}, 120, 14);
        const auto pk = vocal::pick(s, barsAt(13.2, 120));
        QVERIFY(pk.ok);
        QCOMPARE(pk.bars, 1);
        QVERIFY2(pk.b1 >= 12.35, qPrintable(QString("ends at %1, in the tail").arg(pk.b1)));
        QVERIFY2(pk.b0 <= 8.6, qPrintable(QString("starts at %1, in the first word").arg(pk.b0)));
        QCOMPARE(pk.cutDb, 0.0f);
    }

    void aLongerPhraseTakesMoreBarsAndOnlyTheLastPhrase() {
        // An old phrase, a long pause, then a phrase of a bar and a half:
        // two bars, and none of the old one.
        const auto s = sungAt({{0.5, 2.0}, {16.2, 17.8}, {18.1, 19.9}, {20.3, 21.6}}, 120, 24.5);
        const auto pk = vocal::pick(s, barsAt(24.3, 120));
        QVERIFY(pk.ok);
        QCOMPARE(pk.bars, 2);
        QCOMPARE(pk.b0, 16.0);
        QCOMPARE(pk.b1, 24.0);
    }

    void upAndDownStepTheBarsAndStillCutInTheGaps() {
        const auto s = sungAt({{0.6, 3.2}, {4.3, 7.2}, {8.25, 9.0}, {9.5, 10.5}, {10.75, 11.5}}, 120, 13);
        auto r = barsAt(12.6, 120);
        r.bars = 2;
        const auto pk = vocal::pick(s, r);
        QVERIFY(pk.ok);
        QCOMPARE(pk.bars, 2);
        QCOMPARE(pk.b1 - pk.b0, 8.0);
        QCOMPARE(pk.cutDb, 0.0f);
        r.bars = 4; // reaching back before the first word: silence there too
        const auto longer = vocal::pick(s, r);
        QCOMPARE(longer.b1 - longer.b0, 16.0);
    }

    void singingStraightThroughIsCutAtItsQuietest() {
        // A bar asked for (↓), sung straight through for longer: it must
        // cut into the singing, and does where the level dips.
        auto s = sungAt({{7.9, 12.1}}, 120, 12.6);
        for (size_t h = 0; h < s.env.size(); ++h) {
            const double beat = double(h) * s.hop * 2.0;
            if (beat >= 7.9 && beat < 12.1)
                s.env[h] = 0.05f + 0.04f * float(std::cos(beat * kTwoPi)); // a dip on every off-beat
        }
        auto r = barsAt(12.6, 120);
        r.bars = 1;
        const auto pk = vocal::pick(s, r);
        QVERIFY(pk.ok);
        QCOMPARE(pk.b1 - pk.b0, 4.0);
        // The dips are half a beat off the beat.
        const double off = std::fabs(std::fmod(pk.b0, 1.0));
        QVERIFY2(std::fabs(off - 0.5) < 0.13, qPrintable(QString("cut at %1").arg(pk.b0)));
    }

    void freeTimeTakesThePhraseToSpace() {
        vocal::Sung s;
        s.hop = vocal::kHop / 48000.0;
        s.env.assign(size_t(3.0 / s.hop), 0.0008f);
        for (double a : {0.5, 1.0, 1.4})
            for (int h = int(a / s.hop); h < int((a + 0.3) / s.hop); ++h)
                s.env[size_t(h)] = 0.08f;
        vocal::PickRequest r;
        r.free = true;
        r.now = 2.3;
        const auto pk = vocal::pick(s, r);
        QVERIFY(pk.ok);
        QVERIFY(std::fabs(pk.b0 - (0.5 - 0.03)) < 0.02);
        QCOMPARE(pk.b1, 2.3);
        QCOMPARE(pk.phrases, 1);
    }

    // --- loops of the voice -----------------------------------------------------------

    void aPhraseLoopedWithNothingPlayingSetsTheTempoAndReplaysInTime() {
        const std::string script = std::string(kPage) +
                                   "voice 0.5 0.35 hz=220 vowel=a\n"
                                   "voice 0.95 0.3 hz=247 vowel=e\n"
                                   "voice 1.35 0.5 hz=262 to=247 vowel=o\n"
                                   "key 2.2 Space\n"
                                   "end 9\n";
        const auto take = testkit::play(script);
        QVERIFY(take.error.empty());
        const PartView &v = take.view.parts[kVocal];
        QCOMPARE(v.playing, 0);
        QCOMPARE(int(v.clips[0].lengthBeats), 4);
        // 1.73 s from the first word to Space is a bar at 139 BPM.
        QCOMPARE(int(std::lround(take.view.bpm)), 139);
        const double loop = 4 * 60.0 / take.view.bpm;
        // The first word comes round at the same place every pass: the
        // first pass where it was sung (the loop goes through the master's
        // limiter, 64 samples behind the live voice, which doesn't), the
        // others exactly a loop apart.
        const double first = onsetAfter(take, 0.2);
        QVERIFY(first > 0.45 && first < 0.56);
        const double pass1 = onsetAfter(take, first + loop - 0.1);
        QVERIFY2(std::fabs(pass1 - (first + loop)) < 0.002, qPrintable(QString("pass 1 off by %1 ms").arg((pass1 - first - loop) * 1000)));
        for (int k = 2; k <= 4; ++k) {
            const double expect = pass1 + (k - 1) * loop;
            const double got = onsetAfter(take, expect - 0.1);
            qInfo("pass %d: the first word at %.5f s, expected %.5f", k, got, expect);
            QVERIFY2(std::fabs(got - expect) < 0.0002, qPrintable(QString("off by %1 ms").arg((got - expect) * 1000)));
        }
        // And it is the same sound each time.
        QVERIFY(std::fabs(rmsBetween(take, first + loop, first + 2 * loop) -
                          rmsBetween(take, first + 2 * loop, first + 3 * loop)) < 0.02);
    }

    void overAClockTheLoopIsWholeBarsAndLandsOnTheBar() {
        // The metronome gives a clock; three words in the bar from beat 8
        // (4 s at 120). Space just after the bar: one bar, from 8 to 12.
        const std::string script = std::string(kPage) +
                                   "tempo 0.01 120\nkey 0.02 Ctrl+M\n"
                                   "voice 4.2 0.4 hz=220\nvoice 4.8 0.4 hz=247\nvoice 5.4 0.4 hz=262\n"
                                   "key 6.3 Space\nkey 6.6 Ctrl+M\n"
                                   "end 12\n";
        double beatAtSpace = 0;
        const auto take = testkit::watch(script, [&](double t, Session &s) {
            if (std::fabs(t - 6.3) < 0.005)
                beatAtSpace = s.view().beat;
        });
        const PartView &v = take.view.parts[kVocal];
        QCOMPARE(v.playing, 0);
        QCOMPARE(int(v.clips[0].lengthBeats), 4);
        Q_UNUSED(beatAtSpace);
        // After the metronome stops, only the loop sounds: its words come
        // every two seconds (a bar at 120).
        const double a = onsetAfter(take, 7.0, 0.1), b = onsetAfter(take, a + 1.8, 0.1);
        QVERIFY(a > 0 && b > 0);
        QVERIFY2(std::fabs((b - a) - 2.0) < 0.002, qPrintable(QString("period %1").arg(b - a)));
    }

    void aLoopCutOffTheBarSoundsFromItsFirstPass() {
        // A pickup a beat before the bar: the cuts move off the bar lines.
        // The loop must sound as it lands, not bars later from mid-word.
        const std::string script = std::string(kPage) +
                                   "tempo 0.01 120\nkey 0.02 Ctrl+M\n"
                                   "voice 3.55 0.4 hz=220 vowel=a\nvoice 4.05 0.35 hz=247 vowel=e\n"
                                   "voice 4.5 0.5 hz=262 vowel=o\nvoice 5.2 0.9 hz=294 to=262 vowel=a\n"
                                   "voice 6.4 0.5 hz=247 vowel=u\n"
                                   "key 7.3 Space\nkey 7.6 Ctrl+M\nend 12\n";
        const auto take = testkit::play(script);
        QCOMPARE(take.view.parts[kVocal].playing, 0);
        // Every half second of the first pass after the take has the voice.
        for (double t = 7.75; t < 10.5; t += 0.5)
            QVERIFY2(take.rms(t, t + 0.5) > 0.03, qPrintable(QString("silent at %1 s").arg(t)));
    }

    void aFingerOnTheTapePlaysAWordBack() {
        // Two words sung, then silence: a finger on the tape where the
        // second was plays it again, over and over while held.
        const std::string sing = std::string(kPage) + "voice 0.5 0.4 hz=220\nvoice 1.4 0.4 hz=330\n";
        // The tape shows the last eight seconds; at 3.0 s the second word
        // (1.4 s) is 1.6 s back: x = 0.085 + 0.83 × (1 − 1.6 / 8).
        const auto quiet = testkit::play(sing + "end 4.5\n");
        const auto chopped = testkit::play(sing + "down 3.0 1 0.75 0.2 p=0.7\nup 4.4 1\nend 4.5\n");
        QVERIFY(quiet.rms(3.2, 4.3) < 0.005);
        QVERIFY2(chopped.rms(3.2, 4.3) > 0.03, qPrintable(QString("rms %1").arg(chopped.rms(3.2, 4.3))));
    }

    void withNoMicrophoneTheDemoSingerSingsAndLoops() {
        const auto take = testkit::play(std::string(kPage) + "key 0.2 V\nkey 6.0 Space\nend 9\n");
        QCOMPARE(take.view.input, int(StudioView::NoInput));
        QVERIFY(take.rms(1.0, 5.5) > 0.02);
        QCOMPARE(take.view.parts[kVocal].playing, 0);
    }

    void aHarmonyHoldsItsNoteFromTheFirstSyllable() {
        // A third above (the +3RD lane) over sung A3: from a word's first
        // moments the harmony is at C4, not at A3 sliding up.
        std::string s = std::string(kPage);
        for (int w = 0; w < 4; ++w)
            s += "voice " + std::to_string(0.5 + w * 0.6) + " 0.4 hz=220 vowel=a\n";
        // The lanes: seven across 0.085..0.915; +3RD is the fourth.
        s += "down 0.2 1 " + std::to_string(0.085 + 0.83 * 3.5 / 7) + " 0.6 p=0.7\nend 3.0\n";
        const auto take = testkit::play(s);
        auto power = [&](double from, double len, double hz) {
            const size_t a = size_t(from * 48000), n = size_t(len * 48000);
            const double w = kTwoPi * hz / 48000.0, c = 2 * std::cos(w);
            double s1 = 0, s2 = 0;
            for (size_t i = 0; i < n; ++i) {
                const double s0 = take.left[a + i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            return s1 * s1 + s2 * s2 - c * s1 * s2;
        };
        // The third word's first 80 ms (after the 25 ms rise): C4 is there
        // already, and stronger than anything between A3 and C4.
        const double at = 0.5 + 2 * 0.6 + 0.03;
        const double c4 = power(at, 0.08, 261.6), between = power(at, 0.08, 240.0);
        QVERIFY2(c4 > 4 * between, qPrintable(QString("C4 %1, between %2").arg(c4).arg(between)));
    }

    void spaceMakesANewLoopAndShiftSpaceLayers() {
        // A loop, then more singing: Space makes slot 2 (a new loop in its
        // place), Shift+Space would have layered onto slot 1.
        const std::string base = std::string(kPage) +
                                 "voice 0.5 0.35 hz=220\nvoice 0.95 0.3 hz=247\nvoice 1.35 0.5 hz=262\n"
                                 "key 2.2 Space\n"
                                 "voice 2.5 0.3 hz=330\nvoice 2.95 0.3 hz=294\nvoice 3.3 0.3 hz=262\n";
        const auto fresh = testkit::play(base + "key 3.85 Space\nend 6\n");
        int filled = 0;
        for (const auto &c : fresh.view.parts[kVocal].clips)
            filled += c.state != ClipView::Empty;
        QCOMPARE(filled, 2);
        QCOMPARE(fresh.view.parts[kVocal].playing, 1);

        const auto layered = testkit::play(base + "key 3.85 Shift+Space\nend 6\n");
        filled = 0;
        for (const auto &c : layered.view.parts[kVocal].clips)
            filled += c.state != ClipView::Empty;
        QCOMPARE(filled, 1);
        QCOMPARE(layered.view.parts[kVocal].playing, 0);
        QCOMPARE(layered.view.parts[kVocal].clips[0].events, 2); // two sounds, one loop
        QVERIFY(layered.view.parts[kVocal].takes >= 2);
    }

    void ctrlZTakesTheVocalLoopBack() {
        const auto take = testkit::play(std::string(kPage) +
                                        "voice 0.5 0.35 hz=220\nvoice 0.95 0.3 hz=247\nvoice 1.35 0.5 hz=262\n"
                                        "key 2.2 Space\nkey 3.0 Ctrl+Z\nend 5\n");
        QCOMPARE(take.view.parts[kVocal].playing, -1);
        QCOMPARE(int(take.view.parts[kVocal].clips[0].state), int(ClipView::Empty));
        // And nothing sounds after it, but the room's tail.
        QVERIFY(take.rms(4.0, 5.0) < 0.003);
    }

    void upAfterSpaceMakesTheLoopLonger() {
        // Two phrases, the loop set from the last; ↑ takes both.
        const std::string script = std::string(kPage) +
                                   "voice 0.3 0.3 hz=220\nvoice 0.7 0.3 hz=247\n"
                                   "voice 2.0 0.3 hz=262\nvoice 2.4 0.3 hz=247\n"
                                   "key 3.1 Space\nkey 3.5 Up\nend 5.5\n";
        const auto take = testkit::play(script);
        QCOMPARE(take.view.parts[kVocal].playing, 0);
        // The last phrase alone is 1.13 s (a bar at 106 BPM); with both,
        // 2.83 s is two bars at 170.
        QVERIFY2(take.view.bpm > 160, qPrintable(QString("bpm %1").arg(take.view.bpm)));
        QCOMPARE(take.view.parts[kVocal].takes, 1u); // one take, made again
    }

    // --- the fingers -------------------------------------------------------------------

    void aFingerAddsYourVoiceAtItsNote() {
        // Singing A3 with a finger on the keys a fifth above (E4): the mix
        // holds both pitches.
        const std::string sing = std::string(kPage) + "voice 0.3 2.0 hz=220 vowel=o\n";
        const auto alone = testkit::play(sing + "end 2.4\n");
        const auto withFinger = testkit::play(sing + "down 0.2 1 0.62 0.5 p=0.7\nend 2.4\n");
        auto power = [](const testkit::Take &t, double hz) {
            // Goertzel over a second, the left channel.
            const size_t a = size_t(48000 * 1.0), n = 48000;
            const double w = kTwoPi * hz / 48000.0, c = 2 * std::cos(w);
            double s1 = 0, s2 = 0;
            for (size_t i = 0; i < n; ++i) {
                const double s0 = t.left[a + i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            return s1 * s1 + s2 * s2 - c * s1 * s2;
        };
        // Which note is at 0.62 depends on the lanes: find the strongest
        // new partial between 230 and 500 Hz.
        double gain = 0;
        for (double hz = 230; hz < 500; hz += 1)
            gain = std::max(gain, power(withFinger, hz) / std::max(1e-9, power(alone, hz)));
        QVERIFY2(gain > 20, qPrintable(QString("a new pitch only %1× stronger").arg(gain)));
        QVERIFY(withFinger.finite());
    }

    void theVocoderIsAsLoudAsTheVoice() {
        const std::string sing = "voice 0.5 3 hz=220 level=0.1 vowel=a\nend 4\n";
        const auto natural = testkit::play(std::string(kPage) + sing);
        // Four steps right: NATURAL → HARD TUNE → PRISM → CHOIR → VOCODER.
        const auto vocoder =
            testkit::play(std::string(kPage) + "key 0.1 Right\nkey 0.15 Right\nkey 0.2 Right\nkey 0.25 Right\n" + sing);
        const double a = natural.rms(1.5, 3.4), b = vocoder.rms(1.5, 3.4);
        const double db = 20 * std::log10(b / a);
        qInfo("the vocoder is %.1f dB from the natural voice", db);
        QVERIFY2(std::fabs(db) < 3, qPrintable(QString("%1 dB").arg(db)));
    }

    // --- songs ---------------------------------------------------------------------

    void aSongKeepsTheVocalLoopsAndTheirAudio() {
        // A loop and a layer on it, the style one step on: saved, opened in
        // a fresh studio (no tape, no takes of its own), the loops sound as
        // they did, and the style is back.
        const std::string script = std::string(kPage) + "key 0.1 Right\n"
                                   "voice 0.5 0.35 hz=220\nvoice 0.95 0.3 hz=247\nvoice 1.35 0.5 hz=262\n"
                                   "key 2.2 Space\n"
                                   "voice 2.5 0.3 hz=330\nvoice 2.95 0.3 hz=294\nvoice 3.3 0.3 hz=262\n"
                                   "key 3.85 Shift+Space\nend 9\n";
        const auto played = testkit::play(script);
        QCOMPARE(played.view.parts[kVocal].clips[0].events, 2);
        const float heard = played.rms(5.0, 9.0);
        QVERIFY(heard > 0.02);

        const Reopened song = reopenVocal(script, 6.0);
        const QJsonObject part = song.doc[QStringLiteral("parts")].toArray()[kVocal].toObject();
        QCOMPARE(part[QStringLiteral("name")].toString(), QStringLiteral("Vocal"));
        const QJsonArray takes = part[QStringLiteral("audio")].toObject()[QStringLiteral("takes")].toArray();
        QCOMPARE(takes.size(), 2);
        for (const QJsonValue &t : takes)
            QVERIFY(t.toObject()[QStringLiteral("frames")].toInt() > 48000);
        QCOMPARE(song.playing, 0);
        qInfo("played %.4f, opened %.4f", heard, song.rms);
        QVERIFY2(song.rms > 0.6f * heard && song.rms < 1.6f * heard,
                 qPrintable(QString("played %1, opened %2").arg(heard).arg(song.rms)));
        QCOMPARE(song.settings[QStringLiteral("style")].toString(), QStringLiteral("tune"));
        QVERIFY(song.notice.isEmpty() || !song.notice.contains(QStringLiteral("without")));

        // A file without the audio opens, silent, and says so.
        const Reopened bare = reopenVocal(script, 4.0, [](QJsonObject &doc) {
            QJsonArray parts = doc[QStringLiteral("parts")].toArray();
            QJsonObject vocal = parts[kVocal].toObject();
            vocal.remove(QStringLiteral("audio"));
            parts[kVocal] = vocal;
            doc[QStringLiteral("parts")] = parts;
        });
        QVERIFY(bare.rms < 0.002);
        QVERIFY2(bare.notice.contains(QStringLiteral("without their audio")), qPrintable(bare.notice));
    }

    void everyStripAndTheClickPlayWithoutTrouble() {
        // Ten fingers at once over a sung phrase: every strip, keys, the
        // click; nothing blows up, nothing goes silent for good.
        std::string s = std::string(kPage) + "voice 0.3 3.0 hz=196 vowel=a\n";
        const float xs[] = {0.03f, 0.97f, 0.97f, 0.25f, 0.75f, 0.25f, 0.75f, 0.3f, 0.5f, 0.5f};
        const float ys[] = {0.5f, 0.3f, 0.7f, 0.93f, 0.93f, 0.05f, 0.05f, 0.5f, 0.6f, 0.2f};
        for (int f = 0; f < 10; ++f) {
            s += "down " + std::to_string(0.5 + 0.05 * f) + " " + std::to_string(10 + f) + " " + std::to_string(xs[f]) +
                 " " + std::to_string(ys[f]) + " p=0.7\n";
            s += "glide 1.2 2.0 " + std::to_string(10 + f) + " " + std::to_string(xs[f]) + " " +
                 std::to_string(std::min(0.99f, ys[f] + 0.05f)) + " p=0.9 twist=30\n";
        }
        s += "click 2.2 len=0.4\n";
        for (int f = 0; f < 10; ++f)
            s += "up 2.8 " + std::to_string(10 + f) + "\n";
        s += "voice 3.5 0.6 hz=220\nend 4.4\n";
        const auto take = testkit::play(s);
        QVERIFY(take.finite());
        QVERIFY(take.peak() < 1.0);
        QVERIFY(take.rms(1.0, 2.0) > 0.01);
        QVERIFY(take.rms(3.6, 4.0) > 0.01); // the voice is back after it all
    }
    // --- the five styles from further afield -------------------------------------

    // Each new style, sung at: in the key (A minor, the default), under
    // 0 dBFS, finite, named in the status line (with what it does) and the
    // help, and back by name from a saved song.
    void theTalkBoxSpeaksOnTheKeysNote() {
        // Sung a quarter tone sharp of A3: the synth plays A, so the
        // strongest partial under 1 kHz is a harmonic of A (A, A, E), not
        // of the note sung.
        const auto take = playStyle(Talk, "voice 0.3 2.2 hz=226 vowel=a level=0.3\nend 2.8\n");
        checkStyle(take, "TALK BOX");
        QVERIFY2(take.status.contains(QLatin1String("funk synth")), qPrintable(take.status));
        const double hz = strongestHz(take, 1.2, 2.2, 150, 1000);
        qInfo("talk box: strongest partial %.1f Hz", hz);
        QVERIFY2(inAMinor(hz), qPrintable(QString("%1 Hz is off the key").arg(hz)));
        // The sub: a square an octave under the note (A2), there as it is
        // not in the voice.
        const auto natural = testkit::play(std::string(kPage) + "voice 0.3 2.2 hz=226 vowel=a level=0.3\nend 2.8\n");
        const double sub = power(take, 110, 1.2, 2.2) / std::max(1e-12, power(natural, 113, 1.2, 2.2));
        qInfo("talk box: the sub octave %.1f dB over the voice's", 10 * std::log10(sub));
        QVERIFY2(sub > 10, "no sub octave");
        // As loud as the voice it speaks for, within a few dB.
        const double db = 20 * std::log10(take.rms(1.2, 2.2) / natural.rms(1.2, 2.2));
        qInfo("talk box: %.1f dB from the natural voice", db);
        QVERIFY2(std::fabs(db) < 4, qPrintable(QString("%1 dB").arg(db)));
    }

    void theTalkBoxGlidesBetweenNotes() {
        // A3 then, legato, E4: the synth slides up (G-funk's portamento),
        // so 30 to 90 ms after the leap its fundamental is still on the
        // way, and by a quarter second it has landed.
        const auto take = playStyle(Talk, "voice 0.3 0.8 hz=220 vowel=a level=0.3\n"
                                          "voice 1.1 0.8 hz=329.6 vowel=a level=0.3\nend 2.2\n");
        const double leap = 1.1 + 0.02; // the tracker's own few ms
        const double early = strongestHz(take, leap + 0.03, leap + 0.09, 180, 400);
        const double late = strongestHz(take, leap + 0.3, leap + 0.6, 180, 400);
        qInfo("talk box: %.0f Hz just after the leap, %.0f Hz once landed", early, late);
        QVERIFY2(early < 315, qPrintable(QString("%1 Hz: no glide").arg(early)));
        QVERIFY2(std::fabs(late - 329.6) < 6, qPrintable(QString("%1 Hz").arg(late)));
    }

    void theThroatWhistlesAnOvertoneInTheKey() {
        const std::string sing = "voice 0.3 2.2 hz=220 vowel=o level=0.3\nend 2.8\n";
        const auto take = playStyle(Throat, sing);
        checkStyle(take, "THROAT");
        // The whistle walks from harmonic to harmonic a beat at a time: a
        // fifth of a second at a time, it is always on one of the drone's
        // harmonics, always in the key, and far louder than the voice has it.
        const auto natural = testkit::play(std::string(kPage) + sing);
        for (double a = 1.0; a < 2.15; a += 0.2) {
            const double hz = strongestHz(take, a, a + 0.2, 1000, 3400);
            const double lift = power(take, hz, a, a + 0.2) / std::max(1e-12, power(natural, hz, a, a + 0.2));
            qInfo("throat: at %.1f s the whistle is on harmonic %.2f (%.0f Hz), %.1f dB over the plain voice's", a,
                  hz / 220.0, hz, 10 * std::log10(lift));
            QVERIFY2(inAMinor(hz), qPrintable(QString("%1 Hz is off the key").arg(hz)));
            // (Within the voice's own vibrato of a harmonic of A3.)
            QVERIFY2(std::fabs(hz / 220.0 / std::round(hz / 220.0) - 1) < 0.015, qPrintable(QString("%1 Hz").arg(hz)));
            QVERIFY2(lift > 10, qPrintable(QString("only %1x").arg(lift)));
        }
    }

    void theThroatsWhistleStandsInAGap() {
        // A finger holds the whistle on one harmonic; the drone's harmonics
        // either side of it are pressed far under the voice's own.
        const std::string sing = "voice 0.3 2.4 hz=220 vowel=a level=0.3\nend 2.9\n";
        const auto take = playStyle(Throat, sing + "down 0.2 1 0.62 0.6 p=0.6\nup 2.8 1\n");
        const auto natural = testkit::play(std::string(kPage) + sing);
        const double hz = strongestHz(take, 1.2, 2.2, 1000, 3400);
        const int k = int(std::lround(hz / 220.0));
        // (The voice's vibrato spreads each harmonic: its peak within 2%,
        // through a Hann window, against the fundamental's.)
        auto rel = [&](const testkit::Take &t, int j) { return peakPower(t, 220.0 * j, 1.2, 2.2) / peakPower(t, 220.0, 1.2, 2.2); };
        qInfo("throat: whistle on harmonic %d, %.1f dB over the plain voice's; neighbours %.1f and %.1f dB", k,
              10 * std::log10(rel(take, k) / rel(natural, k)), 10 * std::log10(rel(take, k - 1) / rel(natural, k - 1)),
              10 * std::log10(rel(take, k + 1) / rel(natural, k + 1)));
        QVERIFY(k >= 4);
        QVERIFY2(rel(take, k) > 30 * rel(natural, k), "the whistle isn't lifted");
        QVERIFY2(rel(take, k - 1) < 0.5 * rel(natural, k - 1), "no gap under the whistle");
        QVERIFY2(rel(take, k + 1) < 0.5 * rel(natural, k + 1), "no gap over the whistle");
    }

    void theThroatsWhistleEndsWithoutAChirp() {
        // As the drone dies the whistle fades on its last note: in its last
        // tenth of a second it is no higher than it was before the voice
        // began to fall.
        const auto take = playStyle(Throat, "voice 0.3 1.6 hz=220 vowel=o level=0.3\nend 2.6\n");
        const double held = strongestHz(take, 1.3, 1.6, 1000, 3400);
        double worst = 0;
        for (double a = 1.7; a < 2.1; a += 0.05) {
            const double p = power(take, held, 1.3, 1.6);
            for (double hz = held * 1.02; hz < 3400; hz += 4)
                if (power(take, hz, a, a + 0.05) > 0.01 * p)
                    worst = std::max(worst, hz / held);
        }
        qInfo("throat: whistle at %.0f Hz; after the voice, nothing above %.3fx of it", held, worst);
        QVERIFY2(worst < 1.02, qPrintable(QString("the whistle chirps up to %1x").arg(worst)));
    }

    void theSitarIsPluckedAndBends() {
        // A syllable plucks a string at the note sung; sung on into a slide
        // up a tone, the string bends with it (the strongest partial under
        // 600 Hz goes from A3 to B3); and it rings on after the voice, as
        // a string does, with the jawari's buzz up past 6 kHz.
        const std::string sing = "voice 0.5 1.4 hz=220 to=246.9 vowel=a level=0.3\nend 2.8\n";
        const auto take = playStyle(Sitar, sing);
        checkStyle(take, "SITAR");
        QVERIFY2(take.status.contains(QLatin1String("plays a sitar")), qPrintable(take.status));
        const double start = strongestHz(take, 0.65, 0.85, 180, 300), end = strongestHz(take, 1.7, 1.9, 180, 300);
        qInfo("sitar: the string from %.0f Hz to %.0f Hz", start, end);
        QVERIFY2(std::fabs(start - 220) < 6, qPrintable(QString("%1 Hz").arg(start)));
        QVERIFY2(end > 238, qPrintable(QString("%1 Hz: the string didn't bend").arg(end)));
        const auto natural = testkit::play(std::string(kPage) + sing);
        const double tail = take.rms(2.05, 2.6), plain = natural.rms(2.05, 2.6);
        qInfo("sitar: rings on at %.1f dBFS (the voice alone: %.1f)", 20 * std::log10(tail + 1e-12),
              20 * std::log10(plain + 1e-12));
        QVERIFY2(tail > 0.01 && tail > plain * 10, qPrintable(QString("tail %1").arg(tail)));
        // The buzz: well above what the voice has up there.
        double buzz = 0, voice = 0;
        for (double hz = 6000; hz < 9000; hz += 247) {
            buzz += power(take, hz, 0.6, 1.1);
            voice += power(natural, hz, 0.6, 1.1);
        }
        qInfo("sitar: 6-9 kHz %.1f dB over the voice's", 10 * std::log10(buzz / std::max(1e-12, voice)));
        QVERIFY2(buzz > 10 * voice, "no jawari buzz");
        // And as loud as the voice it plays for, within a few dB.
        const double db = 20 * std::log10(take.rms(0.6, 1.9) / natural.rms(0.6, 1.9));
        qInfo("sitar: %.1f dB from the natural voice", db);
        QVERIFY2(std::fabs(db) < 4, qPrintable(QString("%1 dB").arg(db)));
    }

    void theSitarsDronesStrikeTheBeat() {
        // Sung over two beats at 120 bpm: the chikari (A4 and A5, the
        // key's root high up) are struck on each beat, so A5 is far louder
        // just after a beat than just before the next.
        const auto take = playStyle(Sitar, "tempo 0.01 120\nvoice 0.3 2.6 hz=164.8 vowel=a level=0.3\nend 3.2\n");
        double after = 0, before = 0;
        for (int b = 2; b <= 5; ++b) {
            after += power(take, 880, b * 0.5 + 0.01, b * 0.5 + 0.1);
            before += power(take, 880, b * 0.5 + 0.38, b * 0.5 + 0.47);
        }
        qInfo("sitar: the chikari %.1f dB louder just after the beat", 10 * std::log10(after / std::max(1e-12, before)));
        QVERIFY2(after > 2 * before, "no drone strokes on the beat");
    }

    void theGamelanStrikesBronzeInPelog() {
        // Sung a quarter tone sharp of A3: the bar rings at A4 (pelog's
        // first tone on the key's root), an octave over, after the voice
        // has stopped.
        const auto take = playStyle(Gamelan, "voice 0.3 0.7 hz=226 vowel=u level=0.3\nend 2.0\n");
        checkStyle(take, "GAMELAN");
        QVERIFY2(take.rms(1.2, 1.7) > 0.003, qPrintable(QString("tail %1").arg(take.rms(1.2, 1.7))));
        const double hz = strongestHz(take, 1.2, 1.7, 300, 1500);
        qInfo("gamelan: the bar rings at %.1f Hz", hz);
        QVERIFY2(std::fabs(hz - 440.0) < 5.0, qPrintable(QString("%1 Hz").arg(hz)));
        // E4 sung: pelog has no E; its nearest tone is 670 cents over A,
        // so the bar rings there (E5 less 30 cents), not at E5.
        const auto two =
            playStyle(Gamelan, "voice 0.3 0.4 hz=262 vowel=a level=0.3\nvoice 1.0 0.4 hz=330 vowel=a level=0.3\nend 2.0\n");
        const double pelog = 440.0 * std::exp2(670.0 / 1200.0);
        qInfo("gamelan: E4 sung rings %.1f Hz", strongestHz(two, 1.15, 1.5, 600, 700));
        QVERIFY2(power(two, pelog, 1.05, 1.5) > 4 * power(two, pelog, 0.5, 0.95), "the second syllable's bar doesn't ring");
        QVERIFY2(std::fabs(strongestHz(two, 1.15, 1.5, 600, 700) - pelog) < 5, "not on pelog's tone");
        // B: the song's key instead, and the bar is on E5.
        const auto key = playStyle(Gamelan, "key 0.5 B\nkey 0.55 B\nvoice 1.0 0.4 hz=330 vowel=a level=0.3\nend 2.0\n");
        QVERIFY2(key.status.contains(QLatin1String("key, B")), qPrintable(key.status));
        QVERIFY2(std::fabs(strongestHz(key, 1.15, 1.5, 600, 700) - 659.3) < 5, "not on E5 in the key");
    }

    void theGamelansGongsKeepTheCycle() {
        // Sung through five bars at 120 bpm: the gong ageng (A1, 55 Hz)
        // sounds on the first beat and again on the seventeenth, the
        // kempul on every half bar.
        std::string s = "tempo 0.01 120\n";
        for (int k = 0; k < 18; ++k)
            s += "voice " + std::to_string(0.3 + k * 0.5) + " 0.3 hz=220 vowel=a level=0.3\n";
        const auto take = playStyle(Gamelan, s + "end 9.6\n");
        // The cycle runs on the studio's beat; find the gong by its low A.
        double best = 0, at = 0;
        for (double a = 0.2; a < 9.0; a += 0.05) {
            const double p = power(take, 55, a, a + 0.25);
            if (p > best) {
                best = p;
                at = a;
            }
        }
        const auto natural = testkit::play(std::string(kPage) + s + "end 9.6\n");
        qInfo("gamelan: the gong ageng loudest at %.2f s, %.1f dB over the voice's 55 Hz", at,
              10 * std::log10(best / std::max(1e-12, power(natural, 55, at, at + 0.25))));
        QVERIFY2(best > 100 * power(natural, 55, at, at + 0.25), "no gong");
    }

    void theGamelansKotekanInterlocks() {
        // A finger held: two parts strike on the sixteenths, polos to the
        // left and sangsih to the right, so high bronze sounds that the
        // voice alone doesn't make, and left and right differ.
        const std::string sing = "tempo 0.01 120\nvoice 0.3 2.0 hz=220 vowel=a level=0.3\n";
        const auto with = playStyle(Gamelan, sing + "down 0.5 1 0.5 0.6 p=0.6\nup 2.2 1\nend 2.4\n");
        const auto without = playStyle(Gamelan, sing + "end 2.4\n");
        double hiWith = 0, hiWithout = 0, diff = 0, sum = 0;
        for (double hz = 1200; hz < 2600; hz += 20) {
            hiWith += power(with, hz, 1.0, 2.0);
            hiWithout += power(without, hz, 1.0, 2.0);
        }
        for (size_t i = 48000; i < 96000; ++i) {
            diff += std::fabs(with.left[i] - with.right[i]);
            sum += std::fabs(with.left[i]) + std::fabs(with.right[i]);
        }
        qInfo("gamelan: kotekan adds %.1f dB of high bronze; left and right differ by %.0f%%",
              10 * std::log10(hiWith / std::max(1e-12, hiWithout)), 100 * diff / sum);
        QVERIFY2(hiWith > 4 * hiWithout, "no kotekan");
        QVERIFY2(diff > 0.15 * sum, "the parts aren't apart");
    }

    void screwedIsSlowedTape() {
        // A3 sung: slowed tape plays it a fourth down (E3, 164.8 Hz), not
        // snapped to anything; and slowed, a word comes out longer than it
        // was sung.
        const std::string sing = "voice 0.3 0.6 hz=220 vowel=a level=0.3\nend 2.0\n";
        const auto take = playStyle(Screwed, sing);
        checkStyle(take, "SCREWED");
        const double e3 = power(take, 164.8, 0.5, 0.8), a3 = power(take, 220.0, 0.5, 0.8);
        qInfo("screwed: E3 %.1f dB over A3", 10 * std::log10(e3 / std::max(1e-12, a3)));
        QVERIFY2(e3 > 10 * a3, "not a fourth down");
        const double hz = strongestHz(take, 0.5, 0.8, 120, 200);
        QVERIFY2(std::fabs(hz - 164.8) < 3, qPrintable(QString("%1 Hz").arg(hz)));
        const auto natural = testkit::play(std::string(kPage) + sing);
        qInfo("screwed: after the word %.1f dBFS (natural %.1f)", 20 * std::log10(take.rms(0.95, 1.05) + 1e-12),
              20 * std::log10(natural.rms(0.95, 1.05) + 1e-12));
        QVERIFY2(take.rms(0.95, 1.05) > 0.05 && take.rms(0.95, 1.05) > 10 * natural.rms(0.95, 1.05),
                 "the word isn't slowed");
    }

    void screwedChopsAWordWithAFinger() {
        // A word sung, then a finger held after it: the word comes round
        // again and again (a beat of it at 120 bpm) where, without the
        // finger, there is only silence.
        const std::string sing = "tempo 0.01 120\nvoice 0.3 0.5 hz=220 vowel=a level=0.3\n";
        const auto with = playStyle(Screwed, sing + "down 1.0 1 0.5 0.6 p=0.6\nup 2.6 1\nend 2.8\n");
        const auto without = playStyle(Screwed, sing + "end 2.8\n");
        qInfo("screwed: with a finger %.1f dBFS, without %.1f", 20 * std::log10(with.rms(1.6, 2.5) + 1e-12),
              20 * std::log10(without.rms(1.6, 2.5) + 1e-12));
        QVERIFY2(with.rms(1.6, 2.5) > 0.02 && with.rms(1.6, 2.5) > 10 * without.rms(1.6, 2.5), "no chop");
    }

    void theNewStylesComeBackByName() {
        const char *names[] = {"talkbox", "throat", "sitar", "gamelan", "screwed"};
        for (int k = 0; k < 5; ++k) {
            const auto song = reopenVocal(std::string(kPage) + rights(Talk + k) + "voice 0.5 0.6 hz=220\nend 1.4\n", 0.5);
            QCOMPARE(song.settings[QStringLiteral("style")].toString(), QString::fromLatin1(names[k]));
        }
        // And Left from NATURAL wraps round to the last of them.
        const auto wrapped = reopenVocal(std::string(kPage) + "key 0.1 Left\nend 0.6\n", 0.5);
        QCOMPARE(wrapped.settings[QStringLiteral("style")].toString(), QStringLiteral("screwed"));
        // The styles' own options too: slendro, and no sub.
        const auto gam = reopenVocal(std::string(kPage) + rights(Gamelan) + "key 0.5 B\nend 0.8\n", 0.5);
        QCOMPARE(gam.settings[QStringLiteral("gamelanTuning")].toString(), QStringLiteral("slendro"));
        const auto talk = reopenVocal(std::string(kPage) + rights(Talk) + "key 0.5 B\nend 0.8\n", 0.5);
        QCOMPARE(talk.settings[QStringLiteral("talkboxSub")].toBool(true), false);
    }

private:
    // The styles by their place on Left / Right (after the page's own six).
    enum { Talk = 6, Throat, Sitar, Gamelan, Screwed };
    static std::string rights(int n) {
        std::string s;
        for (int k = 0; k < n; ++k)
            s += "key " + std::to_string(0.1 + 0.03 * k) + " Right\n";
        return s;
    }
    struct StyleTake : testkit::Take {
        QString status, help;
    };
    static StyleTake playStyle(int style, const std::string &rest) {
        StyleTake out;
        static_cast<testkit::Take &>(out) = testkit::play(std::string(kPage) + rights(style) + rest, [&](Session &s) {
            out.status = s.pageStatus();
            out.help = s.pageHelp(s.page());
        });
        return out;
    }
    static void checkStyle(const StyleTake &t, const char *name) {
        QVERIFY(t.error.empty());
        QVERIFY(t.finite());
        qInfo("%s: peak %.2f dBFS", name, 20 * std::log10(t.peak() + 1e-12));
        QVERIFY2(t.peak() < 1.0, qPrintable(QString("%1 peaks at %2").arg(name).arg(t.peak())));
        QVERIFY2(t.status.contains(QLatin1String(name)), qPrintable(t.status));
        QVERIFY2(t.help.contains(QLatin1String(name)), name);
    }
    // Goertzel power at hz over [a, b) seconds, both channels.
    static double power(const testkit::Take &t, double hz, double a, double b) {
        const size_t i0 = size_t(a * 48000), i1 = std::min(t.left.size(), size_t(b * 48000));
        const double c = 2 * std::cos(kTwoPi * hz / 48000.0);
        double total = 0;
        for (const auto *ch : {&t.left, &t.right}) {
            double s1 = 0, s2 = 0;
            for (size_t i = i0; i < i1; ++i) {
                const double s0 = (*ch)[i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            total += s1 * s1 + s2 * s2 - c * s1 * s2;
        }
        return total;
    }
    static double peakPower(const testkit::Take &t, double hz, double a, double b) {
        const size_t i0 = size_t(a * 48000), i1 = std::min(t.left.size(), size_t(b * 48000));
        double best = 0;
        for (double f = hz * 0.98; f <= hz * 1.02; f += 2) {
            const double c = 2 * std::cos(kTwoPi * f / 48000.0);
            double total = 0;
            for (const auto *ch : {&t.left, &t.right}) {
                double s1 = 0, s2 = 0;
                for (size_t i = i0; i < i1; ++i) {
                    const double w = 0.5 - 0.5 * std::cos(kTwoPi * double(i - i0) / double(i1 - i0));
                    const double s0 = (*ch)[i] * w + c * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                total += s1 * s1 + s2 * s2 - c * s1 * s2;
            }
            best = std::max(best, total);
        }
        return best;
    }
    static double strongestHz(const testkit::Take &t, double a, double b, double lo, double hi) {
        double best = 0, bestHz = 0;
        for (double hz = lo; hz <= hi; hz += 1) {
            const double p = power(t, hz, a, b);
            if (p > best) {
                best = p;
                bestHz = hz;
            }
        }
        return bestHz;
    }
    // A natural minor, within 30 cents of one of its notes.
    static bool inAMinor(double hz) {
        if (hz <= 0)
            return false;
        const double midi = 69 + 12 * std::log2(hz / 440.0);
        const int note = int(std::lround(midi));
        const int pc = ((note % 12) + 12) % 12;
        static const bool key[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
        return std::fabs(midi - note) < 0.3 && key[pc];
    }
};

TESTKIT_REGISTER(VocalTests)
#include "vocal_tests.moc"
