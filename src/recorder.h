#pragma once

// Video for Shift+Record: the visualizer's frames piped to the ffmpeg
// command (not linked: the app stays free of codecs, and any ffmpeg the
// player has will do), then muxed with the WAV the recorder writes anyway.
//
// Sync: the WAV's sample clock is the master. A frame is rendered when the
// timer fires, and AvSync says how many copies of it the video needs so
// that frame k always covers the audio from k/fps to (k+1)/fps: a frame
// that came late is written twice, a timer that fired early writes none.
// So the video can never drift from the sound, however the screen or the
// machine stutters; at worst a frame is held a little longer.
//
// Nothing here runs on the audio thread: the encoder writes to ffmpeg on a
// thread of its own, and the render happens on the window's thread.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

namespace tf::video {

// The ffmpeg to use: TENFOLD_FFMPEG, then PATH, then where Homebrew and
// MacPorts put it (an app started from the Finder doesn't get the shell's
// PATH). Empty when there is none.
QString findFfmpeg();

// Raw RGBA frames on standard input into an H.264 video of its own.
QStringList encodeArgs(int width, int height, int fps, const QString &out);
// The video and the WAV into one .mp4: the picture copied as it is, the
// sound into AAC.
QStringList muxArgs(const QString &video, const QString &audio, const QString &out);

class AvSync {
public:
    AvSync(int fps, double rate) : m_fps(fps), m_rate(rate) {}
    // Frames the video must hold once the audio has this many samples: the
    // frame covering the newest sample included.
    static int64_t due(int64_t samples, int fps, double rate);
    // A frame was just rendered while the audio stood at `samples`: how many
    // times to write it (0 when the video is already ahead).
    int take(int64_t samples);
    // Recording stopped with the audio at `samples`: copies of the last
    // frame that bring the video to the audio's length.
    int finish(int64_t samples);
    int64_t frames() const { return m_written; }
    double seconds() const { return double(m_written) / m_fps; }

private:
    int m_fps;
    double m_rate;
    int64_t m_written = 0;
};

// ffmpeg on a thread of its own, fed frames through a queue.
class Encoder {
public:
    ~Encoder();
    bool start(const QString &ffmpeg, const QStringList &args, QString *error);
    // A frame, `copies` times; an empty frame repeats the last one (cheap
    // when the encoder falls behind: nothing more is queued but a count).
    void push(QByteArray frame, int copies);
    // Frames waiting to be written.
    int backlog() const;
    // Closes ffmpeg's input and waits for it to finish the file.
    bool finish(QString *error);
    bool running() const { return m_thread.joinable(); }

private:
    void run(QString ffmpeg, QStringList args);
    struct Item {
        QByteArray frame;
        int copies = 1;
    };
    mutable std::mutex m_lock;
    std::condition_variable m_wake, m_started;
    std::deque<Item> m_queue;
    bool m_closing = false, m_ready = false, m_ok = false;
    QString m_error;
    std::thread m_thread;
};

// Runs ffmpeg with these arguments to the end: true if it made its file.
bool runFfmpeg(const QString &ffmpeg, const QStringList &args, QString *error);

}
