#include "recorder.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <cmath>

namespace tf::video {

QString findFfmpeg() {
    const QString env = qEnvironmentVariable("TENFOLD_FFMPEG");
    if (!env.isEmpty())
        return QFileInfo(env).isExecutable() ? env : QString();
    QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"),
                                          {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
                                           QStringLiteral("/opt/local/bin")});
}

QStringList encodeArgs(int width, int height, int fps, const QString &out) {
    return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
            QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height), QStringLiteral("-r"),
            QString::number(fps), QStringLiteral("-i"), QStringLiteral("-"), QStringLiteral("-an"),
            // Fast enough to keep up beside the studio, sharp enough for the
            // picture's fine lines; yuv420p so every player opens it.
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
            QStringLiteral("-crf"), QStringLiteral("18"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), out};
}

QStringList muxArgs(const QString &video, const QString &audio, const QString &out) {
    return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
            QStringLiteral("-i"), video, QStringLiteral("-i"), audio, QStringLiteral("-map"), QStringLiteral("0:v:0"),
            QStringLiteral("-map"), QStringLiteral("1:a:0"), QStringLiteral("-c:v"), QStringLiteral("copy"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("320k"),
            QStringLiteral("-movflags"), QStringLiteral("+faststart"), out};
}

int64_t AvSync::due(int64_t samples, int fps, double rate) {
    if (samples <= 0)
        return 1;
    // Frame k covers [k/fps, (k+1)/fps): the newest sample falls in frame
    // floor((samples − 1) · fps / rate), so that many plus one are due. In
    // integers where they are exact, so no frame is lost to rounding.
    const double r = std::round(rate);
    if (std::fabs(rate - r) < 1e-9)
        return (samples - 1) * fps / int64_t(r) + 1;
    return int64_t(std::floor(double(samples - 1) * fps / rate)) + 1;
}

int AvSync::take(int64_t samples) {
    const int64_t n = due(samples, m_fps, m_rate) - m_written;
    if (n <= 0)
        return 0;
    m_written += n;
    return int(n);
}

int AvSync::finish(int64_t samples) {
    // As many frames as the audio is long, rounded to the nearest frame.
    const int64_t total = int64_t(std::llround(double(samples) * m_fps / m_rate));
    const int64_t n = total - m_written;
    if (n <= 0)
        return 0;
    m_written += n;
    return int(n);
}

Encoder::~Encoder() {
    QString ignored;
    finish(&ignored);
}

bool Encoder::start(const QString &ffmpeg, const QStringList &args, QString *error) {
    if (m_thread.joinable())
        return false;
    m_closing = m_ready = m_ok = false;
    m_queue.clear();
    m_error.clear();
    m_thread = std::thread([this, ffmpeg, args] { run(ffmpeg, args); });
    std::unique_lock lock(m_lock);
    m_started.wait(lock, [this] { return m_ready; });
    if (!m_ok) {
        if (error)
            *error = m_error;
        lock.unlock();
        m_thread.join();
        return false;
    }
    return true;
}

void Encoder::push(QByteArray frame, int copies) {
    if (copies <= 0)
        return;
    std::lock_guard lock(m_lock);
    if (frame.isEmpty() && !m_queue.empty() && m_queue.back().frame.isEmpty())
        m_queue.back().copies += copies;
    else
        m_queue.push_back({std::move(frame), copies});
    m_wake.notify_one();
}

int Encoder::backlog() const {
    std::lock_guard lock(m_lock);
    int n = 0;
    for (const Item &i : m_queue)
        n += i.frame.isEmpty() ? 0 : 1;
    return n;
}

bool Encoder::finish(QString *error) {
    if (!m_thread.joinable())
        return false;
    {
        std::lock_guard lock(m_lock);
        m_closing = true;
        m_wake.notify_one();
    }
    m_thread.join();
    if (error)
        *error = m_error;
    return m_ok;
}

void Encoder::run(QString ffmpeg, QStringList args) {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    proc.start(ffmpeg, args);
    const bool started = proc.waitForStarted(10000);
    {
        std::lock_guard lock(m_lock);
        m_ready = true;
        m_ok = started;
        if (!started)
            m_error = QStringLiteral("%1 would not start").arg(ffmpeg);
        m_started.notify_all();
    }
    if (!started)
        return;
    QByteArray last;
    bool broken = false;
    for (;;) {
        Item item;
        {
            std::unique_lock lock(m_lock);
            m_wake.wait(lock, [this] { return m_closing || !m_queue.empty(); });
            if (m_queue.empty())
                break;
            item = std::move(m_queue.front());
            m_queue.pop_front();
        }
        if (!item.frame.isEmpty())
            last = std::move(item.frame);
        for (int c = 0; c < item.copies && !broken && !last.isEmpty(); ++c) {
            proc.write(last);
            while (proc.bytesToWrite() > 0 && !broken)
                broken = !proc.waitForBytesWritten(10000) && proc.state() != QProcess::Running;
        }
    }
    proc.closeWriteChannel();
    proc.waitForFinished(-1);
    std::lock_guard lock(m_lock);
    m_ok = !broken && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!m_ok)
        m_error = QStringLiteral("ffmpeg stopped (exit %1)").arg(proc.exitCode());
}

bool runFfmpeg(const QString &ffmpeg, const QStringList &args, QString *error) {
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(10000)) {
        if (error)
            *error = QStringLiteral("%1 would not start").arg(ffmpeg);
        return false;
    }
    proc.closeWriteChannel();
    proc.waitForFinished(-1);
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok && error)
        *error = QString::fromLocal8Bit(proc.readAll()).trimmed().left(300);
    return ok;
}

}
