// Song files: saving a song and opening it again, in a fresh tenfold.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "testkit.h"

using namespace tf;

namespace {

// A one-bar beat at 120 BPM (as the framework's tests make it), then a bass
// loop over it, the key a fifth up and the tempo up by one.
const char *kSong = "page 0 1\n"
                    "tap 0.5 0.05 0.5\ntap 1.0 0.25 0.5\ntap 1.5 0.05 0.5\ntap 2.0 0.25 0.5\n"
                    "key 2.5 Space\n"
                    "page 3.0 2\n"
                    "tap 4.55 0.1 0.5\ntap 5.05 0.3 0.5\ntap 5.55 0.2 0.5\ntap 6.05 0.1 0.5\n"
                    "key 6.7 Space\n"
                    "key 7.5 Ctrl+Right\nkey 7.6 Ctrl+Up\n"
                    "end 9\n";

// Answers at once, with the paths a test gives it.
class FakePicker : public FilePicker {
public:
    QString openPath, savePath;
    int asked = 0;
    void openFile(const QString &, std::function<void(const QString &)> done) override {
        ++asked;
        done(openPath);
    }
    void saveFile(const QString &, const QString &, const QString &, std::function<void(const QString &)> done) override {
        ++asked;
        done(savePath);
    }
};

bool sameClips(Studio &a, Studio &b, QString *why) {
    ClipSet x, y;
    if (!a.readClips(x) || !b.readClips(y)) {
        *why = QStringLiteral("readClips failed");
        return false;
    }
    for (int p = 0; p < x.parts; ++p)
        for (int c = 0; c < kClipSlots; ++c) {
            const auto &ca = x.clips[p][c], &cb = y.clips[p][c];
            const QString where = QStringLiteral("part %1 slot %2: ").arg(p).arg(c);
            if (ca.length != cb.length || ca.events.size() != cb.events.size()) {
                *why = where + QStringLiteral("length %1/%2, events %3/%4")
                                   .arg(ca.length).arg(cb.length).arg(ca.events.size()).arg(cb.events.size());
                return false;
            }
            for (size_t i = 0; i < ca.events.size(); ++i) {
                const ClipEvent &ea = ca.events[i], &eb = cb.events[i];
                // Positions are kept to a millionth of a beat.
                bool same = std::fabs(ea.pos - eb.pos) <= 1e-6 && ea.layer == eb.layer && ea.c.type == eb.c.type &&
                            ea.c.flags == eb.c.flags && ea.c.id == eb.c.id;
                for (int k = 0; k < 4; ++k)
                    same = same && ea.c.i[k] == eb.c.i[k];
                for (int k = 0; k < 8; ++k)
                    same = same && ea.c.f[k] == eb.c.f[k];
                if (!same) {
                    *why = where + QStringLiteral("event %1 differs").arg(i);
                    return false;
                }
            }
        }
    return true;
}

QJsonObject saved(const std::string &script) {
    QJsonObject doc;
    testkit::play(script, [&](Session &s) { doc = s.saveSong(); });
    return doc;
}

}

class SongTests : public QObject {
    Q_OBJECT
private slots:
    void aSongComesBackAsItWasSaved() {
        testkit::reopen(kSong, 5.0, [](Session &before, Session &opened, const QJsonObject &doc) {
            QCOMPARE(doc[QStringLiteral("tenfold")].toInt(), 1);
            const StudioView &a = before.view(), &b = opened.view();
            QVERIFY(std::fabs(b.bpm - a.bpm) <= 1e-6);
            QCOMPARE(int(std::lround(b.bpm)), 121);
            QCOMPARE(b.root, a.root);
            QCOMPARE(b.root, 4); // A, a fifth up: E
            QCOMPARE(b.family, a.family);
            QCOMPARE(b.brightness, a.brightness);
            for (int i = 0; i < MasterCount; ++i)
                QCOMPARE(b.master[i], a.master[i]);
            for (int p = 0; p < a.partCount; ++p) {
                for (int c = 0; c < ChannelCount; ++c)
                    QCOMPARE(b.parts[p].channel.value[c], a.parts[p].channel.value[c]);
                // What was playing plays again, from the next bar.
                QCOMPARE(b.parts[p].playing, a.parts[p].playing);
            }
            QCOMPARE(b.parts[0].playing, 0);
            QCOMPARE(b.parts[1].playing, 0);
            QString why;
            QVERIFY2(sameClips(*before.studio(), *opened.studio(), &why), qPrintable(why));
            // Back on the page that was showing.
            QCOMPARE(opened.page(), before.page());
        });
    }

    void anOpenedSongSounds() {
        float rms = 0;
        testkit::reopen(kSong, 4.0, [&](Session &, Session &opened, const QJsonObject &) {
            rms = opened.view().rms;
        });
        QVERIFY(rms > 0.005);
    }

    void theFileReadsTopDown() {
        const QByteArray json = songJson(saved(kSong));
        const QList<QByteArray> lines = json.split('\n');
        QVERIFY(lines.size() > 10);
        QCOMPARE(lines[0], QByteArray("{"));
        QCOMPARE(lines[1], QByteArray("  \"tenfold\": 1,"));
        // Every event is one line, its position first.
        int events = 0;
        for (const QByteArray &line : lines)
            if (line.trimmed().startsWith("{\"at\": ")) {
                QVERIFY2(line.trimmed().endsWith("}") || line.trimmed().endsWith("},"), line.constData());
                ++events;
            }
        QCOMPARE(events, 4 + 8); // the beat's four hits, the bass's four notes on and off
        QVERIFY(json.contains("\"type\": \"on\""));
    }

    void theMixInTheFileIsTheMixOpened() {
        QJsonObject doc = saved(kSong);
        QJsonArray parts = doc[QStringLiteral("parts")].toArray();
        QJsonObject bass = parts[1].toObject();
        QCOMPARE(bass[QStringLiteral("name")].toString(), QStringLiteral("Bass"));
        QJsonObject mix = bass[QStringLiteral("mix")].toObject();
        mix[QStringLiteral("gain")] = 0.5;
        mix[QStringLiteral("mute")] = 1;
        bass[QStringLiteral("mix")] = mix;
        parts[1] = bass;
        doc[QStringLiteral("parts")] = parts;
        testkit::play("end 0.1\n", [&](Session &s) {
            QString error;
            QVERIFY2(s.openSong(doc, &error), qPrintable(error));
            QCOMPARE(s.view().parts[1].channel.value[Gain], 0.5f);
            QCOMPARE(s.view().parts[1].channel.value[Mute], 1.0f);
        });
    }

    void aBadSongIsRefusedAndNothingChanges() {
        const QJsonObject good = saved(kSong);
        auto with = [&](const std::function<void(QJsonObject &)> &edit) {
            QJsonObject d = good;
            edit(d);
            return d;
        };
        auto firstClip = [](QJsonObject &d, const std::function<void(QJsonObject &)> &edit) {
            QJsonArray parts = d[QStringLiteral("parts")].toArray();
            QJsonObject part = parts[0].toObject();
            QJsonArray clips = part[QStringLiteral("clips")].toArray();
            QJsonObject clip = clips[0].toObject();
            edit(clip);
            clips[0] = clip;
            part[QStringLiteral("clips")] = clips;
            parts[0] = part;
            d[QStringLiteral("parts")] = parts;
        };
        struct Bad {
            const char *what;
            QJsonObject doc;
            const char *says;
        };
        const Bad bad[] = {
            {"no version", with([](QJsonObject &d) { d.remove(QStringLiteral("tenfold")); }), "isn't a tenfold song"},
            {"a newer version", with([](QJsonObject &d) { d[QStringLiteral("tenfold")] = 2; }), "newer tenfold"},
            {"no tempo", with([](QJsonObject &d) { d[QStringLiteral("tempo")] = QStringLiteral("fast"); }), "no tempo"},
            {"parts not a list", with([](QJsonObject &d) { d[QStringLiteral("parts")] = 3; }), "no parts"},
            {"a slot out of range", with([&](QJsonObject &d) {
                 firstClip(d, [](QJsonObject &c) { c[QStringLiteral("slot")] = 9; });
             }), "slot"},
            {"an event past the clip's end", with([&](QJsonObject &d) {
                 firstClip(d, [](QJsonObject &c) {
                     QJsonArray e = c[QStringLiteral("events")].toArray();
                     QJsonObject first = e[0].toObject();
                     first[QStringLiteral("at")] = 99;
                     e[0] = first;
                     c[QStringLiteral("events")] = e;
                 });
             }), "isn't inside the clip"},
            {"an unknown event type", with([&](QJsonObject &d) {
                 firstClip(d, [](QJsonObject &c) {
                     QJsonArray e = c[QStringLiteral("events")].toArray();
                     QJsonObject first = e[0].toObject();
                     first[QStringLiteral("type")] = QStringLiteral("explode");
                     e[0] = first;
                     c[QStringLiteral("events")] = e;
                 });
             }), "no type"},
        };
        testkit::play(kSong, [&](Session &s) {
            ClipSet was;
            QVERIFY(s.studio()->readClips(was));
            for (const Bad &b : bad) {
                QString error;
                QVERIFY2(!s.openSong(b.doc, &error), b.what);
                QVERIFY2(error.contains(QLatin1String(b.says)), qPrintable(QStringLiteral("%1: %2").arg(b.what, error)));
            }
            // Refused before anything changed: the loops still play.
            QCOMPARE(s.view().parts[0].playing, 0);
            QCOMPARE(s.view().parts[1].playing, 0);
            ClipSet now;
            QVERIFY(s.studio()->readClips(now));
            QCOMPARE(now.clips[0][0].events.size(), was.clips[0][0].events.size());
        });
    }

    void aFileThatIsNotASongIsRefused() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("not.tenfold"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"tenfold\": 1, \"tempo\": ");
        f.close();
        QJsonObject doc;
        QString error;
        QVERIFY(!readSongFile(path, &doc, &error));
        QVERIFY2(error.contains(QLatin1String("isn't a tenfold song")), qPrintable(error));
        QVERIFY(!readSongFile(dir.filePath(QStringLiteral("missing.tenfold")), &doc, &error));
        QVERIFY2(error.contains(QLatin1String("Can't open")), qPrintable(error));
    }

    void ctrlSSavesThroughThePickerAndCtrlOOpens() {
        QTemporaryDir dir;
        FakePicker picker;
        picker.savePath = dir.filePath(QStringLiteral("jam")); // typed without the extension
        testkit::play(kSong, [&](Session &s) {
            SongFiles files(&s, &picker);
            files.setAutosavePath(dir.filePath(QStringLiteral("state/last.tenfold")));
            files.save();
            QCOMPARE(picker.asked, 1);
            const QString path = dir.filePath(QStringLiteral("jam.tenfold"));
            QCOMPARE(files.path(), path);
            QVERIFY(QFile::exists(path));
            // Only the song: nothing left beside it from writing it.
            QCOMPARE(QDir(dir.path()).entryList(QDir::Files), QStringList{QStringLiteral("jam.tenfold")});
            uint32_t seq = 0;
            QVERIFY(s.notice(&seq).startsWith(QStringLiteral("Saved jam.tenfold")));
            // Saved again, it goes where it went without asking.
            files.save();
            QCOMPARE(picker.asked, 1);

            // Opened in a fresh tenfold.
            Studio studio(48000.0f);
            studio.setTimeOrigin(scriptClock());
            Session fresh(&studio);
            fresh.setClock(&scriptClock);
            SongFiles freshFiles(&fresh, &picker);
            picker.openPath = path;
            freshFiles.open();
            QCOMPARE(picker.asked, 2);
            QVERIFY2(fresh.notice(&seq).startsWith(QStringLiteral("Opened jam.tenfold")), qPrintable(fresh.notice(&seq)));
            QCOMPARE(freshFiles.path(), path);
            QString why;
            QVERIFY2(sameClips(*s.studio(), studio, &why), qPrintable(why));
        });
    }

    void aNewerSongSaysSoInTheWindow() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("future.tenfold"));
        QJsonObject doc = saved(kSong);
        doc[QStringLiteral("tenfold")] = 7;
        QVERIFY(writeSongFile(path, doc, nullptr));
        FakePicker picker;
        picker.openPath = path;
        testkit::play("end 0.1\n", [&](Session &s) {
            SongFiles files(&s, &picker);
            QVERIFY(!files.openFrom(path));
            uint32_t seq = 0;
            QVERIFY2(s.notice(&seq).contains(QLatin1String("newer tenfold (format 7")), qPrintable(s.notice(&seq)));
            QVERIFY(files.path().isEmpty());
        });
    }

    void quittingKeepsTheSongAndTheNextStartPicksItUp() {
        QTemporaryDir dir;
        const QString kept = dir.filePath(QStringLiteral("state/last.tenfold"));
        // Nothing made: nothing kept, so an empty quit never replaces a jam.
        testkit::play("end 0.1\n", [&](Session &s) {
            SongFiles files(&s, nullptr);
            files.setAutosavePath(kept);
            QVERIFY(!files.autosave());
            QVERIFY(!files.hasAutosave());
        });
        testkit::play(kSong, [&](Session &s) {
            SongFiles files(&s, nullptr);
            files.setAutosavePath(kept);
            QVERIFY(files.autosave());
            QVERIFY(files.hasAutosave());

            Studio studio(48000.0f);
            studio.setTimeOrigin(scriptClock());
            Session next(&studio);
            next.setClock(&scriptClock);
            SongFiles nextFiles(&next, nullptr);
            nextFiles.setAutosavePath(kept);
            nextFiles.reopenLast();
            uint32_t seq = 0;
            QVERIFY(next.notice(&seq).startsWith(QStringLiteral("Picked up where you left off")));
            // Ctrl+S then asks where to put it, never over the kept one.
            QVERIFY(nextFiles.path().isEmpty());
            QString why;
            QVERIFY2(sameClips(*s.studio(), studio, &why), qPrintable(why));
        });
    }
};

TESTKIT_REGISTER(SongTests)
#include "song_tests.moc"
