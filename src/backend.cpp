#include "backend.h"

#include <QCoreApplication>
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
#include "device.h"
#include "offline.h"

using namespace tf;

Backend::Backend(bool live, QObject *parent) : QObject(parent), m_simulated(!live) {
    m_studio = std::make_unique<Studio>(48000.0f);
    m_audio = std::make_unique<AudioOut>(m_studio.get());
    m_session = std::make_unique<Session>(m_studio.get());
    m_touch = std::make_unique<TouchSource>(m_session.get());
    if (!live)
        m_session->setClock(&scriptClock);

    if (live) {
        // Debugging Space takes: ~/.cache/tenfold/debug.log, fresh each run.
        const QString cache = QDir::homePath() + "/.cache/tenfold";
        QDir().mkpath(cache);
        m_session->setDebugLog(cache + "/debug.log");
        m_audio->start();
        const auto pads = Device::findTouchpads();
        if (!pads.isEmpty())
            m_device = pads.first().path;
    }

    if (live) {
        m_tickerRun = true;
        m_ticker = std::thread([this] {
            while (m_tickerRun.load()) {
                timespec ts{};
                clock_gettime(CLOCK_MONOTONIC, &ts);
                m_session->idle(double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9);
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

void Backend::shutdown() {
    m_tickerRun = false;
    if (m_ticker.joinable())
        m_ticker.join();
    stopRecording();
    if (m_touch)
        m_touch->stop();
    m_audio->stop();
}

int Backend::padState() const {
    if (m_simulated)
        return Playing;
    if (m_device.isEmpty())
        return Failed;
    switch (m_touch->status()) {
    case TouchSource::Idle: return Idle;
    case TouchSource::Starting: return Starting;
    case TouchSource::Ready: return m_wantPlay ? Starting : Ready;
    case TouchSource::Grabbed: return Playing;
    case TouchSource::Failed: return Failed;
    }
    return Idle;
}

QString Backend::error() const {
    if (m_device.isEmpty())
        return tr("No trackpad found.");
    return QString::fromStdString(m_touch->error());
}

QString Backend::padName() const { return QString::fromStdString(m_touch->padName()); }

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

QString Backend::position() const {
    const double beat = std::max(0.0, m_session->view().beat);
    const long b = long(std::floor(beat));
    return QStringLiteral("%1.%2").arg(b / kBeatsPerBar + 1).arg(b % kBeatsPerBar + 1);
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
    if (m_device.isEmpty())
        return;
    m_wantPlay = true;
    m_pausedByFocus = false;
    if (m_touch->status() == TouchSource::Idle || m_touch->status() == TouchSource::Failed) {
        m_touch->stop();
        m_touch->start(m_device.toStdString());
    }
    m_touch->grab(true);
    emit changed();
}

void Backend::giveBack() {
    m_wantPlay = false;
    m_touch->grab(false);
    emit changed();
}

bool Backend::closeMenu() {
    return m_session->closeMenu();
}

void Backend::togglePlay() {
    if (m_touch->status() == TouchSource::Grabbed || m_wantPlay)
        giveBack();
    else
        play();
}

void Backend::windowActive(bool active) {
    // Never keep the pad from the rest of the desktop when tenfold isn't in
    // front: a grabbed pad with no window to answer to would leave the user
    // without a pointer.
    if (!active && (m_wantPlay || m_touch->status() == TouchSource::Grabbed)) {
        giveBack();
        m_pausedByFocus = true;
    } else if (active && m_pausedByFocus) {
        play();
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
    m_session->setMixRecording(false);
}

void Backend::tick() {
    if (!m_simulated) {
        // Let held-back lifts through even when the pad has gone quiet.
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        m_session->idle(double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9);
    }
    m_session->refresh();
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
