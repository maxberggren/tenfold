#pragma once

// Export: the song rendered offline into a folder of files another studio
// (Ableton Live, any DAW) lines up as tenfold played it. See docs/export.md
// for what the files hold and why.
//
// Everything is rendered in one pass of a studio and a session of their own
// (the window's keep playing), from the song as a song file holds it:
//
//   the mix          what tenfold plays, the master bus and all
//   a stem per part  its channel, its own share of the room, and the
//                    master as the options say (Studio::exportStems)
//   voices           finer, where the part's loops can be told apart: a
//                    drum per pad, a Sampler song per separated stem, the
//                    layers Space put on a loop on the other pages
//                    (Studio::addVoice)
//   a MIDI file      a track per part whose notes mean something as notes:
//                    drums (General MIDI), bass, chords, lead and arp
//
// Every file starts on the downbeat of the song's first bar (sample 0 is
// beat 1 of bar 1, nothing before it), at the studio's rate, 24-bit, and all
// are the same length: whole bars, the song's and, with tails, as many more
// as the reverb and echo need to ring out. Each WAV carries an 'acid' chunk
// (tempo, beats, the key's root) and a 'bext' chunk (BWF: time reference 0,
// so a DAW that spots by timestamp puts them all at the same place); the
// names say the tempo and the key.

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace tf {

struct ExportOptions {
    QString folder;           // written into, made if need be
    QString prefix;           // every file's name starts with it
    float rate = 48000;       // the studio's
    bool song = true;         // the Song page's arrangement; false (or no song): the loops playing
    int bars = 8;             // the loops: how many bars of them
    QStringList parts;        // the parts to write, by name; empty: every part with a clip
    bool voices = false;      // each part's voices too, where it has them
    bool mix = true;
    bool midi = true;
    bool tails = true;        // ring out past the end (to whole bars), or stop on its last bar line
    bool preFader = false;    // the engines' own dry sound: no channel, room or master
    bool master = true;       // post-fader: the master's glue and limiter on the stems, as on the mix
    double maxTail = 20;      // seconds, at most
};

struct ExportResult {
    bool ok = false, cancelled = false;
    QString error;            // a sentence, when not ok
    QString folder;
    QStringList files;        // the names written, in the folder
    QStringList notes;        // what was left out, and why
    double bpm = 0, seconds = 0;
    int bars = 0;             // of the song (or loops), without the tail
    int64_t frames = 0;       // every file's length
};

// fraction 0..1, and what it is doing.
using ExportProgress = std::function<void(double fraction, const QString &what)>;

// Renders and writes. Blocking: run it on a thread of its own. cancel,
// when set, stops it, and what it wrote so far is removed.
ExportResult exportSong(const QJsonObject &song, const ExportOptions &options, const std::atomic<bool> *cancel = nullptr,
                        const ExportProgress &progress = {});

// --- the pieces, for the tests ---------------------------------------------

// An ACID chunk ('acid' and its 24 bytes): a loop that stretches, at this
// tempo, this many beats long, in 4/4; the root a MIDI note, or −1 for none.
QByteArray acidChunk(double bpm, int beats, int rootNote);
// A Broadcast WAV 'bext' chunk (version 1): the description, tenfold as the
// originator, the date, and a time reference of 0.
QByteArray bextChunk(const QString &description, const QDateTime &when, int rate);

// The key signature a key has in standard notation: sharps (+) or flats (−),
// and whether it is minor; false when the scale has no such thing (not one
// of the seven diatonic modes).
bool keySignature(int root, int family, int brightness, int *sharps, bool *minor);

struct MidiNote {
    int64_t tick = 0, length = 1;
    int key = 60, velocity = 100;
};
struct MidiTrack {
    QString name;
    int channel = 0; // 9: the drums
    std::vector<MidiNote> notes;
};
// A Standard MIDI File, type 1: a first track with the tempo, 4/4 and the
// key signature (sharps 99: none), then a track each.
QByteArray midiFile(const QString &title, double bpm, int ppq, int sharps, bool minor, const std::vector<MidiTrack> &tracks);

// "<prefix>01 Drums - 124 bpm A aeolian", as the files are named (without
// the extension); index 0 is the mix, voices "01.03".
QString exportFileName(const QString &prefix, const QString &index, const QString &name, double bpm,
                       const QString &key);

}
