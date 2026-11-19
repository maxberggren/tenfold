#include "backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <chrono>
#include <ctime>

#include "device.h"
#include "offline.h"

using namespace tf;

Backend::Backend(bool live, QObject *parent) : QObject(parent), m_simulated(!live) {
    m_engine = std::make_unique<Engine>(48000.0f);
    m_audio = std::make_unique<AudioOut>(m_engine.get());
    m_sink = std::make_unique<EngineSink>(m_engine.get(), 1.6f);
    m_gesture = std::make_unique<Gesture>(m_sink.get());
    m_touch = std::make_unique<TouchSource>(m_gesture.get());
    m_gv = m_gesture->view();

    if (live) {
        m_audio->start();
        const auto pads = Device::findTouchpads();
        if (!pads.isEmpty())
            m_device = pads.first().path;
    }

    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Backend::tick);
    m_timer.start();
}

Backend::~Backend() { shutdown(); }

void Backend::shutdown() {
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

QString Backend::key() const { return QString::fromUtf8(Tuning::noteName(m_gv.root)); }

QString Backend::mode() const {
    const ScaleFamily &f = family(m_gv.family);
    return tr("%1 %2").arg(QString::fromUtf8(f.modeNames[std::clamp(m_gv.brightness, 0, f.modeCount - 1)]),
                           f.count == 5 && m_gv.family == 1 ? tr("pentatonic") : QString::fromUtf8(f.name).toLower());
}

double Backend::bpm() const {
    const float bar = m_engine->view().barSec;
    return bar > 0 ? 240.0 / bar : 0;
}

int Backend::loops() const {
    int n = 0;
    for (const auto &l : m_engine->view().loops)
        n += l.active;
    return n;
}

int Backend::voices() const {
    int n = 0;
    for (const auto &v : m_engine->view().voices)
        n += v.active && !v.frozen && v.finger >= 0 && v.finger < (1 << 20); // not notes a loop is playing
    return n;
}

int Backend::frozen() const {
    int n = 0;
    for (const auto &v : m_engine->view().voices)
        n += v.active && v.frozen;
    return n;
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

void Backend::shiftRoot(int fifths) { m_gesture->shiftRoot(fifths); }
void Backend::shiftFamily(int step) { m_gesture->shiftFamily(step); }
void Backend::toggleJust() { m_gesture->toggleJust(); }
void Backend::shiftOctave(int step) { m_gesture->shiftOctave(step); }

void Backend::toggleTutorial() {
    if (m_tutorial.active())
        m_tutorial.stop();
    else
        m_tutorial.start(m_gesture->view());
    emit changed();
}

void Backend::tutorialNext() {
    if (m_tutorial.active())
        m_tutorial.next(m_gesture->view());
    emit changed();
}

void Backend::tutorialBack() {
    if (m_tutorial.active())
        m_tutorial.back(m_gesture->view());
    emit changed();
}

void Backend::startTutorialAt(int step) {
    m_tutorial.start(m_gesture->view());
    for (int i = 0; i < step; ++i)
        m_tutorial.next(m_gesture->view());
}
void Backend::resetKnobs() { m_gesture->resetKnobs(); }
void Backend::dissolve() { m_gesture->dissolve(); }
void Backend::dissolveAll() { m_gesture->dissolveAll(); }

QString Backend::roleName(int r) const { return QCoreApplication::translate("Role", tf::roleName(r)); }
QString Backend::roleSlide(int r) const { return QCoreApplication::translate("Role", tf::roleSlide(r)); }
QString Backend::roleTwist(int r) const { return QCoreApplication::translate("Role", tf::roleTwist(r)); }

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
    m_engine->setRecording(true);
    m_recorder = std::thread([this] { recorderLoop(); });
    emit changed();
}

void Backend::recorderLoop() {
    // Written beside the target and renamed into place when finished, so a
    // half-written file never sits under the final name.
    const std::string part = (m_recordPath + ".part").toStdString();
    WavWriter wav;
    if (!wav.open(part, int(m_engine->sampleRate()))) {
        m_recorderRun = false;
        return;
    }
    std::vector<float> buf(48000 * 2);
    for (;;) {
        const bool more = m_recorderRun.load();
        size_t n;
        while ((n = m_engine->drainRecording(buf.data(), 48000)) > 0)
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
    m_engine->setRecording(false);
    m_recorderRun = false;
    if (m_recorder.joinable())
        m_recorder.join();
    m_recording = false;
    m_lastRecording = m_recordPath;
}

void Backend::tick() {
    if (!m_simulated) {
        // Let held-back lifts through even when the pad has gone quiet.
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        m_gesture->idle(double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9);
    }
    m_engine->fetchView();
    m_gv = m_gesture->view();

    // The tutorial watches what's been played, and runs a click while the
    // player learns to tap a loop (until the first loop sets the tempo).
    const double dt = m_tickClock.isValid() ? m_tickClock.restart() / 1000.0 : 0.016;
    if (!m_tickClock.isValid())
        m_tickClock.start();
    Tutorial::State ts;
    ts.playing = padState() == Playing;
    ts.recording = m_recording;
    ts.gesture = &m_gv;
    ts.engine = &m_engine->view();
    m_tutorial.update(ts, std::min(dt, 0.1));
    const bool click = m_tutorial.active() && m_tutorial.step().metronome && !m_engine->view().tempoSet;
    if (click != m_metronome) {
        m_metronome = click;
        tf::Command c;
        c.type = tf::Cmd::Metronome;
        c.f[0] = click ? 1.0f : 0.0f;
        m_engine->send(c);
    }

    if (m_gv.noticeSeq != m_noticeSeq) {
        m_noticeSeq = m_gv.noticeSeq;
        emit noticeChanged();
    }
    emit frame();
    emit changed();
}

bool Backend::simulate(const std::string &script, double until, std::string *error) {
    ScriptPlayer player;
    if (!player.load(script, error))
        return false;
    m_engine->setTimeOrigin(0);
    std::vector<float> buf(384 * 2);
    for (int tick = 0;; ++tick) {
        const double t = tick / 125.0;
        if (t > until)
            break;
        player.tick(t, *m_gesture);
        m_engine->render(buf.data(), 384);
        if (t > until - 1.0)
            this->tick();
    }
    this->tick();
    return true;
}
