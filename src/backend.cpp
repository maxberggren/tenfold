#include "backend.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <chrono>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <ctime>

#include "console.h"
#include "export.h"
#include "offline.h"
#include "visualizer.h"

using namespace tf;

Backend::Backend(bool live, QObject *parent) : QObject(parent), m_simulated(!live) {
    m_studio = std::make_unique<Studio>(48000.0f);
    m_audio = platform::openAudio(m_studio.get());
    m_session = std::make_unique<Session>(m_studio.get());
    m_touch = platform::openPad(m_session.get());
    m_picker = platform::filePicker([this](const QString &why) { m_session->say(why); });
    m_songs = std::make_unique<SongFiles>(m_session.get(), m_picker.get());
    m_session->setFilePicker(m_picker.get());
    if (!live) {
        m_session->setClock(&scriptClock);
        // A screenshot neither keeps nor offers a song of its own.
        m_songs->setAutosavePath({});
        m_session->setStartOverBackup({});
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
        m_session->setAskStartOver(true); // the window's dialog confirms it
        // The newcomer's window or everything, as the player left it (it
        // only ever opens). A first start is a newcomer's, with a style to
        // pick; a player who has used tenfold before (a song kept at quit)
        // starts with everything out, and is never asked for a style.
        QSettings settings;
        const bool known = !m_songs->autosavePath().isEmpty() && QFile::exists(m_songs->autosavePath());
        const int level = settings.value(QStringLiteral("level"), known ? 2 : 0).toInt();
        m_session->setLevel(level);
        m_savedLevel = m_session->level();
        m_welcome = m_session->level() == 0 && !settings.value(QStringLiteral("welcomed"), known).toBool();
        // What NEXT has already shown (a page's sounds, every instrument):
        // once, not each run.
        m_savedBrowsed = settings.value(QStringLiteral("browsedSounds"), known).toBool();
        m_savedAllParts = settings.value(QStringLiteral("sawAllParts"), known).toBool();
        m_session->setBrowsedSounds(m_savedBrowsed);
        m_session->setSawAllParts(m_savedAllParts);
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

    m_exportFolder = QDir(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)).filePath(
        QStringLiteral("tenfold/exports"));

    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Backend::tick);
    m_timer.start();
}

Backend::~Backend() { shutdown(); }

// --- export -------------------------------------------------------------------

QStringList Backend::exportParts() const {
    QStringList out;
    const StudioView &v = m_session->view();
    for (int p = 0; p < m_session->partCount() && p < v.partCount; ++p) {
        bool any = false;
        for (const ClipView &c : v.parts[p].clips)
            any |= c.state != ClipView::Empty;
        if (any)
            out << m_session->pageName(p);
    }
    return out;
}

int Backend::loopBars() const {
    // The longest loop playing, in whole bars: one pass of everything.
    const StudioView &v = m_session->view();
    float beats = 0;
    for (int p = 0; p < v.partCount; ++p)
        if (v.parts[p].playing >= 0)
            beats = std::max(beats, v.parts[p].clips[v.parts[p].playing].lengthBeats);
    return std::max(4, int(std::ceil(beats / 4.0f - 1e-3f)));
}

QString Backend::exportStatus() const {
    std::lock_guard lock(m_exportLock);
    return exporting() ? m_exportWhat : m_exportResult;
}

QString Backend::defaultExportPrefix() const {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm "));
}

void Backend::openExportFolder() {
    if (!m_exportedFolder.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_exportedFolder));
}

void Backend::setExportFolder(const QString &folder) {
    if (folder.isEmpty())
        return;
    m_exportFolder = folder;
    emit exportChanged();
}

void Backend::pickExportFolder() {
    m_picker->pickFolder(tr("Export to folder"), m_exportFolder, [this](const QString &folder) { setExportFolder(folder); });
}

bool Backend::startExport(const QVariantMap &in) {
    if (exporting())
        return false;
    QString error;
    // The song as Ctrl+S would save it, read here; the render is the
    // export thread's, in a studio of its own, while this one plays on.
    const QJsonObject doc = m_session->saveSong(&error);
    if (doc.isEmpty()) {
        std::lock_guard lock(m_exportLock);
        m_exportResult = error;
        emit exportChanged();
        return false;
    }
    ExportOptions o;
    o.folder = in.value(QStringLiteral("folder"), m_exportFolder).toString();
    o.prefix = in.value(QStringLiteral("prefix")).toString();
    o.rate = m_studio->sampleRate();
    o.song = in.value(QStringLiteral("song"), true).toBool();
    o.bars = in.value(QStringLiteral("bars"), 8).toInt();
    o.parts = in.value(QStringLiteral("parts")).toStringList();
    o.voices = in.value(QStringLiteral("voices"), false).toBool();
    o.mix = in.value(QStringLiteral("mix"), true).toBool();
    o.midi = in.value(QStringLiteral("midi"), true).toBool();
    o.tails = in.value(QStringLiteral("tails"), true).toBool();
    o.preFader = in.value(QStringLiteral("preFader"), false).toBool();
    o.master = in.value(QStringLiteral("master"), true).toBool();
    if (o.parts.isEmpty() && !o.mix) {
        std::lock_guard lock(m_exportLock);
        m_exportResult = tr("Nothing chosen to export");
        emit exportChanged();
        return false;
    }
    if (o.parts.isEmpty())
        o.parts << QStringLiteral("-"); // no part, the mix alone
    m_exportCancel = false;
    m_exportFinished = false;
    m_exportFraction = 0;
    m_exportedFolder.clear();
    {
        std::lock_guard lock(m_exportLock);
        m_exportWhat = tr("Getting ready");
        m_exportResult.clear();
    }
    m_exportThread = std::thread([this, doc, o] {
        const ExportResult r = exportSong(doc, o, &m_exportCancel, [this](double f, const QString &what) {
            m_exportFraction = f;
            std::lock_guard lock(m_exportLock);
            m_exportWhat = what;
        });
        {
            std::lock_guard lock(m_exportLock);
            if (r.cancelled)
                m_exportResult = tr("Export cancelled; nothing was kept");
            else if (!r.ok)
                m_exportResult = tr("Not exported: %1").arg(r.error);
            else
                m_exportResult = tr("%n file(s) in %1", nullptr, int(r.files.size())).arg(r.folder) +
                                 (r.notes.isEmpty() ? QString() : QStringLiteral(" (") + r.notes.join(QStringLiteral("; ")) + QLatin1Char(')'));
        }
        {
            std::lock_guard lock(m_exportLock);
            m_exportWrote = r.ok ? r.folder : QString();
        }
        m_exportFinished = true;
    });
    emit exportChanged();
    return true;
}

void Backend::finishExport() {
    if (m_exportThread.joinable())
        m_exportThread.join();
    m_exportFinished = false;
    QString said;
    {
        std::lock_guard lock(m_exportLock);
        said = m_exportResult;
        m_exportedFolder = m_exportWrote;
    }
    m_session->say(said);
    emit exportChanged();
}

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
    m_exportCancel = true;
    if (m_exportThread.joinable())
        m_exportThread.join();
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
        return m_simulatedPad;
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

void Backend::escape() {
    const bool taken = !m_simulated && (m_touch->status() == Pad::Grabbed || m_wantPlay);
    if (m_session->escape(taken) == Session::Escaped::Pad)
        giveBack();
    tick();
}

void Backend::windowActive(bool active) {
    // Never keep the pad from the rest of the desktop when tenfold isn't in
    // front: a grabbed pad with no window to answer to would leave the user
    // without a pointer.
    // Taken again when it's back in front: until then the pad is away (the
    // status line says so).
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
    if (m_exportFinished.load())
        finishExport();
    else if (exporting())
        emit exportChanged();
    // Back in front and taken again: no longer away, so a pad lost from
    // here on is waiting for its button again.
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
    if (m_session->takeSoundsAsked())
        emit soundsAsked();
    if (m_session->takeAllPartsAsked())
        emit allPartsAsked();
    if (m_session->takeStartOverWanted())
        emit startOverAsked();
    if (!m_simulated && (m_session->browsedSounds() != m_savedBrowsed || m_session->sawAllParts() != m_savedAllParts)) {
        m_savedBrowsed = m_session->browsedSounds();
        m_savedAllParts = m_session->sawAllParts();
        QSettings settings;
        settings.setValue(QStringLiteral("browsedSounds"), m_savedBrowsed);
        settings.setValue(QStringLiteral("sawAllParts"), m_savedAllParts);
    }
    // The band's fold opens the level from the pad itself: kept, as the
    // window's own choice is.
    if (!m_simulated && m_session->level() != m_savedLevel) {
        m_savedLevel = m_session->level();
        QSettings().setValue(QStringLiteral("level"), m_savedLevel);
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

void Backend::setUiLevel(int level) {
    m_session->setLevel(level);
    if (!m_simulated && m_session->level() != m_savedLevel) {
        m_savedLevel = m_session->level();
        QSettings().setValue(QStringLiteral("level"), m_savedLevel);
    }
    emit changed();
}

void Backend::endWelcome(int style) {
    if (style >= 0)
        m_session->applyStyle(style);
    m_welcome = false;
    if (!m_simulated)
        QSettings().setValue(QStringLiteral("welcomed"), true);
    emit changed();
    emit pageChanged(); // the sounds chosen show in the browser
}

QStringList Backend::soundNames() const {
    QStringList out;
    for (const auto &c : m_session->soundChoices())
        out << c.name;
    return out;
}

QStringList Backend::soundWhats() const {
    QStringList out;
    for (const auto &c : m_session->soundChoices())
        out << c.what;
    return out;
}

QStringList Backend::styleNames() const {
    QStringList out;
    for (const auto &s : tf::Session::styles())
        out << QString::fromUtf8(s.name);
    return out;
}

QStringList Backend::styleWhats() const {
    QStringList out;
    for (const auto &s : tf::Session::styles())
        out << QString::fromUtf8(s.what);
    return out;
}

QVariantList Backend::styleDetails() const {
    QVariantList out;
    for (const auto &s : tf::Session::styles()) {
        QVariantMap m;
        m[QStringLiteral("bpm")] = s.bpm;
        QStringList sounds;
        for (int i = 0; i < 4; ++i) // drums, bass, chords, lead
            sounds << QString::fromUtf8(s.sounds[i]);
        m[QStringLiteral("sounds")] = sounds;
        out << m;
    }
    return out;
}

QVariantList Backend::partLoops() const {
    QVariantList out;
    for (const auto &l : m_session->loopSummary()) {
        QVariantMap m;
        m[QStringLiteral("page")] = l.page;
        m[QStringLiteral("loops")] = l.loops;
        m[QStringLiteral("beats")] = l.beats;
        m[QStringLiteral("playing")] = l.playing;
        m[QStringLiteral("queued")] = l.queued;
        out << m;
    }
    return out;
}
