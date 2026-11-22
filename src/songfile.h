#pragma once

// Song files: everything needed to pick up where you left off, saved as
// JSON a person can read, with a .tenfold extension.
//
//   {
//     "tenfold": 1,                       the format; a newer one is refused
//     "tempo": 124,
//     "key": {"root": 9, "family": 0, "brightness": 4, "name": "A Aeolian"},
//     "metronome": false,
//     "page": "Drums",                    the page that was showing
//     "master": {"room": 0.35, "shimmer": 0.1, "echoTime": 0.5, ...},
//     "parts": [
//       {
//         "name": "Drums",
//         "mix": {"gain": 0.8, "pan": 0.5, "filter": 0.5, "reverb": 0.15, ...},
//         "settings": {"kit": 2, ...},     the page's own (PartControl::save)
//         "playing": 0,                   the slot that was playing, or -1
//         "clips": [
//           {"slot": 0, "beats": 16, "anchor": 0, "events": [
//             {"at": 0, "type": "on", "id": 1, "i": [0], "f": [0.9]},
//             {"at": 0.5, "type": 17, "flags": 1, "layer": 1, "id": 2, "i": [3, 1], "f": [0.7, 0.25]}
//           ]}
//         ],
//         "audio": {...}                  what clips of recorded audio play (the
//                                         page's saveAudio; the Vocal page's
//                                         loops, as 16-bit PCM in base64)
//       }
//     ],
//     "song": {...}                       the Song page's own (its save)
//   }
//
// Parts are found by name, so a song saved by a tenfold with a part this
// one lacks opens with that part left out, and a part the file lacks starts
// as it would after Ctrl+N. An event is a Command at a position in beats:
// "type" is "on", "off", "move", "param" or the page's own number (16 and
// up); "i" and "f" are its values with trailing zeros left off; "flags" and
// "layer" are left off when 0. "name" under "key" is only for reading.

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <functional>

#include "studio.h"

namespace tf {

class Session;

constexpr int kSongFormat = 1;

// The JSON a song file holds: two-space indents, and anything short enough
// (an event, a mix) on one line, keys in a fixed order that reads top down.
QByteArray songJson(const QJsonObject &doc);

// Reads a song file, or says in a sentence why it can't: not there, not
// JSON, not a song, too big, or saved by a newer tenfold.
bool readSongFile(const QString &path, QJsonObject *doc, QString *error);
// Writes beside the file and renames it over, so a crash or a full disk
// never leaves half a song where the old one was.
bool writeSongFile(const QString &path, const QJsonObject &doc, QString *error);
// The format check alone, on a document already parsed.
bool checkSongFormat(const QJsonObject &doc, QString *error);

// Numbers as a file shows them: a float as the shortest decimal that reads
// back as the same float (0.8, not 0.800000011920929), and a double to so
// many decimals (0.9988, not 0.9988000001906876).
double tidyFloat(float f);
double tidyNumber(double v, int decimals);

// A clip to JSON and back. Positions are kept to a millionth of a beat.
// The anchor is kept against the clip's length (the beat count it was made
// at means nothing in another session), and an overdub's "not before" beat
// is left out: it has passed.
QJsonObject clipToJson(int slot, const ClipSet::Clip &clip);
bool clipFromJson(const QJsonValue &v, int *slot, ClipSet::Clip *clip, QString *error);

// A line about a song, for `tenfold check`: tempo, key, parts and clips.
QString songSummary(const QJsonObject &doc);

// Where Open and Save ask for a file. The window's goes through the desktop
// portal; the tests' answers at once.
class FilePicker {
public:
    virtual ~FilePicker() = default;
    // done gets the path picked, or an empty one if the dialog was closed.
    virtual void openFile(const QString &title, std::function<void(const QString &)> done) = 0;
    virtual void saveFile(const QString &title, const QString &name, const QString &folder,
                          std::function<void(const QString &)> done) = 0;
};

// Ctrl+S, Ctrl+Shift+S, Ctrl+O and the song kept on quit: the session's
// song to and from files, each step said in the window's notice.
// The name a new song is first offered under: when it was made, then its
// tempo and key, so a folder of them sorts by date and says what each is.
// "2026-11-22 15.43 · 124 bpm · A aeolian.tenfold", with plain # and b for
// the sharps and flats, which every file system and keyboard has.
QString songFileName(double bpm, int root, const QString &mode, const QDateTime &when);

class SongFiles {
public:
    SongFiles(Session *session, FilePicker *picker);

    // Ctrl+S: to the file the song came from or was last saved to; the
    // first time, the picker asks where.
    void save();
    // Ctrl+Shift+S: always asks.
    void saveAs();
    // Ctrl+O.
    void open();
    // Ctrl+Shift+O: the song as it was when tenfold last quit.
    void reopenLast();

    bool saveTo(const QString &path);
    bool openFrom(const QString &path);
    QString path() const { return m_path; }

    // On quit, the song is kept in the state folder (unless nothing was
    // made), and the next start offers it back in a notice.
    void setAutosavePath(const QString &path) { m_autosave = path; }
    QString autosavePath() const { return m_autosave; }
    bool autosave();
    bool hasAutosave() const;

private:
    Session *m_session;
    FilePicker *m_picker;
    QString m_path, m_autosave;
};

}
