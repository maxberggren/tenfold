#include "backend.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <chrono>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <ctime>

#include "console.h"
#include "offline.h"
#include "padprompt.h"
#include "visualizer.h"

using namespace tf;

Backend::Backend(bool live, QObject *parent) : QObject(parent), m_simulated(!live) {
    m_studio = std::make_unique<Studio>(48000.0f);
    m_audio = platform::openAudio(m_studio.get());
    m_session = std::make_unique<Session>(m_studio.get());
    m_touch = platform::openPad(m_session.get());
    m_picker = platform::filePicker([this](const QString &why) { m_session->say(why); });
    m_songs = std::make_unique<SongFiles>(m_session.get(), m_picker.get());
    if (!live) {
        m_session->setClock(&scriptClock);
        // A screenshot neither keeps nor offers a song of its own.
        m_songs->setAutosavePath({});
    }

    if (live) {
        // Debugging Space takes: ~/.cache/tenfold/debug.log, fresh each run.
        const QString cache = QDir::homePath() + "/.cache/tenfold";
        QDir().mkpath(cache);
        m_session->setDebugLog(cache + "/debug.log");
        m_audio->start();
        // The song kept when tenfold last quit stays one Ctrl+Shift+O away
        // (listed in the help); a notice at start would sit over the
        // trackpad prompt.
    }

    if (live) {
        m_tickerRun = true;
        m_ticker = std::thread([this] {
            while (m_tickerRun.load()) {
                m_session->idle(monotonicSeconds());
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        });
    }

    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Backend::tick);
    m_timer.start();
}

Backend::~Backend() { shutdown(); }

// Where a recording went is worth a glance, not a place in the bar for
// good: it goes after five seconds (the tutorial still has it).
void Backend::noteSaved(const QString &path) {
    m_savedNotice = path;
    if (!m_savedTimer.isActive() && m_savedTimer.interval() != 5000) {
        m_savedTimer.setSingleShot(true);
        m_savedTimer.setInterval(5000);
        connect(&m_savedTimer, &QTimer::timeout, this, [this] {
            m_savedNotice.clear();
            emit changed();
        });
    }
    m_savedTimer.start();
}

void Backend::setVisualizer(Visualizer *visual, QQmlEngine *engine) {
    m_visual = visual;
    m_video = std::make_unique<VideoRecorder>(engine, visual, m_studio.get());
    connect(m_video.get(), &VideoRecorder::done, this, [this](const QString &mp4, const QString &error) {
        if (!mp4.isEmpty()) {
            m_lastRecording = mp4;
            noteSaved(mp4);
        } else {
            m_session->say(tr("No video: %1").arg(error));
        }
        emit changed();
    });
}

void Backend::shutdown() {
    if (m_shutDown)
        return;
    m_shutDown = true;
    // Kept before the audio stops: the clips are copied out of the studio
    // by the audio thread.
    if (!m_simulated)
        m_songs->autosave();
    m_tickerRun = false;
    if (m_ticker.joinable())
        m_ticker.join();
    stopRecording();
    // Waits for a video still being muxed, while the picture's engine is
    // still there.
    m_video.reset();
    if (m_touch)
        m_touch->stop();
    m_audio->stop();
}

int Backend::padState() const {
    if (m_simulated)
        return Playing;
    if (!m_touch->found())
        return Failed;
    switch (m_touch->status()) {
    case Pad::Idle: return Idle;
    case Pad::Starting: return Starting;
    case Pad::Ready: return m_wantPlay ? Starting : Ready;
    case Pad::Grabbed: return Playing;
    case Pad::Failed: return Failed;
    }
    return Idle;
}

bool Backend::padPrompt() const {
    return !m_simulated && tf::padPrompt(m_touch->found(), m_touch->status(), m_pausedByFocus, m_touch->needsPermission());
}

QString Backend::error() const {
    if (!m_touch->found())
        return tr("No trackpad found.");
    return QString::fromStdString(m_touch->error());
}

QString Backend::padName() const { return QString::fromStdString(m_touch->name()); }

bool Backend::padNeedsPassword() const {
    return !m_simulated && m_touch->needsPermission();
}

void Backend::setPage(int page) {
    m_session->setPage(page);
    if (m_session->page() != m_lastPage) {
        m_lastPage = m_session->page();
        emit pageChanged();
    }
}

QStringList Backend::pageNames() const {
    QStringList l;
    for (int i = 0; i < m_session->pageCount(); ++i)
        l << m_session->pageName(i);
    return l;
}

QStringList Backend::pageKeys() const {
    QStringList l;
    for (int i = 0; i < m_session->pageCount(); ++i)
        l << m_session->pageKey(i);
    return l;
}

bool Backend::drop(const QString &what) {
    // A dragged file arrives as a file:// URL; a link stays as it is.
    const QUrl url(what.trimmed());
    const QString given = url.isLocalFile() ? url.toLocalFile() : what.trimmed();
    if (given.isEmpty())
        return false;
    const bool taken = m_session->drop(given);
    if (!taken)
        m_session->say(tr("Nothing here takes that: drop a sound file, or paste a link to a song"));
    if (m_session->page() != m_lastPage) {
        m_lastPage = m_session->page();
        emit pageChanged();
    }
    return taken;
}

void Backend::paste() {
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        m_session->say(tr("Nothing to paste: copy a link to a song, or a sound file's path"));
        return;
    }
    // Only the first line: a link, or a path.
    drop(text.section(QLatin1Char('\n'), 0, 0));
}

QStringList Backend::pageBlurbs() const {
    QStringList l;
    for (int i = 0; i < m_session->pageCount(); ++i)
        l << m_session->pageBlurb(i);
    return l;
}

QVariantList Backend::pageColors() const {
    QVariantList l;
    for (int i = 0; i < m_session->pageCount(); ++i)
        l << m_session->pageColor(i);
    return l;
}

QVariantList Backend::pagesPlaying() const {
    QVariantList l;
    const StudioView &v = m_session->view();
    for (int i = 0; i < m_session->pageCount(); ++i)
        l << (i < v.partCount && v.parts[i].playing >= 0);
    return l;
}

QVariantList Backend::pagesSilenced() const {
    QVariantList l;
    const StudioView &v = m_session->view();
    bool anySolo = false;
    for (int i = 0; i < v.partCount; ++i)
        anySolo |= v.parts[i].channel.value[Solo] > 0.5f;
    for (int i = 0; i < m_session->pageCount(); ++i) {
        QString why;
        if (i < v.partCount) {
            const float *ch = v.parts[i].channel.value;
            if (ch[Mute] > 0.5f)
                why = tr("muted");
            else if (anySolo && ch[Solo] < 0.5f)
                why = tr("solo elsewhere");
            else if (ch[Gain] < 0.02f)
                why = tr("level down");
        }
        l << why;
    }
    return l;
}

QString Backend::key() const { return QString::fromUtf8(Tuning::noteName(m_session->view().root)); }

QString Backend::mode() const {
    const StudioView &v = m_session->view();
    const ScaleFamily &f = family(v.family);
    return QString::fromUtf8(f.modeNames[std::clamp(v.brightness, 0, f.modeCount - 1)]);
}

QString Backend::chord() const {
    // The chord of the moment, as a roman numeral of the key.
    static const char *numerals[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    const StudioView &v = m_session->view();
    const int r = ((v.chordRoot % 7) + 7) % 7;
    return QString::fromLatin1(numerals[r]);
}

int Backend::clips() const {
    int n = 0;
    const StudioView &v = m_session->view();
    for (int p = 0; p < v.partCount; ++p)
        n += v.parts[p].playing >= 0;
    return n;
}

int Backend::voices() const {
    int n = 0;
    const StudioView &v = m_session->view();
    for (int p = 0; p < v.partCount; ++p)
        n += v.parts[p].voices;
    return n;
}

bool Backend::sendKey(int key, int modifiers, bool down, bool repeat, const QString &text) {
    KeyEvent k;
    k.key = key;
    k.modifiers = modifiers;
    k.down = down;
    k.repeat = repeat;
    k.text = text;
    const bool used = m_session->key(k);
    if (m_session->page() != m_lastPage) {
        m_lastPage = m_session->page();
        emit pageChanged();
    }
    return used;
}

QVariantList Backend::tutorialChecks() const {
    QVariantList l;
    const Tutorial &t = m_session->tutorial();
    for (int i = 0; i < int(t.step().checks.size()); ++i) {
        QVariantMap m;
        m["label"] = t.step().checks[size_t(i)].label;
        m["done"] = t.checked(i);
        l << m;
    }
    return l;
}

// Each chapter in the colour of the page it happens on.
QVariantList Backend::tutorialChapterColors() const {
    QVariantList l;
    const Tutorial &t = m_session->tutorial();
    QString last;
    for (int i = 0; i < t.count(); ++i)
        if (t.step(i).chapter != last) {
            last = t.step(i).chapter;
            l << m_session->pageColor(t.step(i).page);
        }
    return l;
}

// Each may change the page: the tabs and the guide follow.
void Backend::toggleTutorial() {
    m_session->toggleTutorial();
    tick();
}
void Backend::tutorialNext() {
    m_session->tutorialNext();
    tick();
}
void Backend::tutorialBack() {
    m_session->tutorialBack();
    tick();
}

double Backend::latencyMs() const {
    const int q = m_audio->quantum();
    return q > 0 ? 1000.0 * q / m_audio->rate() : 0;
}

double Backend::recordSeconds() const { return m_recording ? m_recordClock.elapsed() / 1000.0 : 0; }

void Backend::play() {
    m_pausedByFocus = false;
    take();
}

void Backend::take() {
    if (!m_touch->found())
        return;
    m_wantPlay = true;
    if (m_touch->status() == Pad::Idle || m_touch->status() == Pad::Failed) {
        m_touch->stop();
        m_touch->start();
    }
    m_touch->grab(true);
    emit changed();
}

void Backend::giveBack() {
    m_pausedByFocus = false;
    release();
}

void Backend::release() {
    m_wantPlay = false;
    m_touch->grab(false);
    emit changed();
}

bool Backend::closeMenu() {
    return m_session->closeMenu();
}

void Backend::togglePlay() {
    if (m_touch->status() == Pad::Grabbed || m_wantPlay)
        giveBack();
    else
        play();
}

void Backend::windowActive(bool active) {
    // Never keep the pad from the rest of the desktop when tenfold isn't in
    // front: a grabbed pad with no window to answer to would leave the user
    // without a pointer.
    // Taken again when it's back in front: until then the pad is away, and
    // the window doesn't ask for it (padPrompt).
    m_inFront = active;
    if (!active && (m_wantPlay || m_touch->status() == Pad::Grabbed)) {
        m_pausedByFocus = true;
        release();
    } else if (active && m_pausedByFocus) {
        take();
    }
}

void Backend::toggleRecording() {
    if (m_recording) {
        stopRecording();
        emit changed();
        return;
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    dir += "/tenfold";
    QDir().mkpath(dir);
    m_recordPath = dir + "/tenfold-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".wav";
    m_recorderRun = true;
    m_recording = true;
    m_recordClock.start();
    m_studio->setRecording(true);
    m_recorder = std::thread([this] { recorderLoop(); });
    m_session->setMixRecording(true);
    emit changed();
}

void Backend::toggleVideoRecording() {
    if (m_recording) {
        toggleRecording();
        return;
    }
    if (!m_video || video::findFfmpeg().isEmpty()) {
        // The sound is recorded anyway: the take isn't lost to a missing tool.
        m_session->say(tr("Video needs ffmpeg: recording the sound only"));
        toggleRecording();
        return;
    }
    toggleRecording();
    QString error;
    m_recordingVideo = m_video->start(m_recordPath, &error);
    if (!m_recordingVideo)
        m_session->say(tr("No video (%1): recording the sound only").arg(error));
    emit changed();
}

void Backend::recorderLoop() {
    // Written beside the target and renamed into place when finished, so a
    // half-written file never sits under the final name.
    const std::string part = (m_recordPath + ".part").toStdString();
    WavWriter wav;
    if (!wav.open(part, int(m_studio->sampleRate()))) {
        m_recorderRun = false;
        return;
    }
    std::vector<float> buf(48000 * 2);
    for (;;) {
        const bool more = m_recorderRun.load();
        size_t n;
        while ((n = m_studio->drainRecording(buf.data(), 48000)) > 0)
            wav.write(buf.data(), n);
        if (!more)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    wav.close();
    QFile::remove(m_recordPath);
    QFile::rename(QString::fromStdString(part), m_recordPath);
}

void Backend::stopRecording() {
    if (!m_recording)
        return;
    m_studio->setRecording(false);
    m_recorderRun = false;
    if (m_recorder.joinable())
        m_recorder.join();
    m_recording = false;
    m_lastRecording = m_recordPath;
    noteSaved(m_recordPath);
    m_session->setMixRecording(false);
    // The WAV is whole now: the video catches up to its length and is
    // muxed with it (done() says where it went).
    if (m_recordingVideo && m_video) {
        m_recordingVideo = false;
        m_video->stop();
        m_session->say(tr("Saving the video…"));
    }
}

void Backend::tick() {
    if (!m_simulated) {
        // Let held-back lifts through even when the pad has gone quiet.
        m_session->idle(monotonicSeconds());
    }
    m_session->refresh();
    // Back in front and taken again: no longer away, so a pad lost from
    // here on asks for itself again.
    if (m_pausedByFocus && m_inFront && m_touch->status() == Pad::Grabbed)
        m_pausedByFocus = false;
    if (m_visual)
        m_visual->stepLive();
    uint32_t seq = 0;
    const QString n = m_session->notice(&seq);
    if (seq != m_noticeSeq) {
        m_noticeSeq = seq;
        m_notice = n;
        emit noticeChanged();
    }
    if (m_session->page() != m_lastPage) {
        m_lastPage = m_session->page();
        emit pageChanged();
    }
    emit frame();
    emit changed();
}

bool Backend::simulate(const std::string &script, double until, std::string *error) {
    ScriptPlayer player;
    if (!player.load(script, error))
        return false;
    until = std::min(until, player.end());
    m_studio->setTimeOrigin(0);
    std::vector<float> buf(384 * 2);
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > until)
            break;
        m_session->refresh();
        player.tick(t, *m_session);
        m_studio->render(buf.data(), 384);
    }
    setScriptClock(until);
    this->tick();
    return true;
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requested) {
    // Drawn three times over and scaled down by the text: crisp at any scale.
    const QStringList parts = id.split('/');
    const int px = std::clamp(requested.height() > 0 ? requested.height() : 16, 6, 128) * 3;
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (parts.size() == 2) {
        bool ok = false;
        const uint code = parts[0].toUInt(&ok, 16);
        const QColor color(QLatin1Char('#') + parts[1]);
        if (ok && color.isValid()) {
            QPainter p(&img);
            tf::console::drawIcon(p, QChar(char16_t(code)), QRectF(0, 0, px, px), color);
        }
    }
    if (size)
        *size = img.size();
    return img;
}

bool Backend::hasIcons(const QString &text) const {
    for (const QChar c : text)
        if (tf::console::isIcon(c))
            return true;
    return false;
}

QString Backend::rich(const QString &text, const QColor &color, int px) const {
    // An icon is as tall as a capital, set in the middle of the line.
    const int size = std::max(6, int(std::lround(px * 0.82)));
    const QString hex = color.name(QColor::HexRgb).mid(1);
    QString out;
    for (const QChar c : text) {
        if (tf::console::isIcon(c)) {
            out += QStringLiteral("<img src=\"image://icon/%1/%2\" width=\"%3\" height=\"%3\" align=\"middle\">")
                       .arg(QString::number(c.unicode(), 16), hex)
                       .arg(size);
        } else if (c == QLatin1Char('&')) {
            out += QStringLiteral("&amp;");
        } else if (c == QLatin1Char('<')) {
            out += QStringLiteral("&lt;");
        } else if (c == QLatin1Char('>')) {
            out += QStringLiteral("&gt;");
        } else if (c == QLatin1Char('\n')) {
            out += QStringLiteral("<br>");
        } else {
            out += c;
        }
    }
    return out;
}
