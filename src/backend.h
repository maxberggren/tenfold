#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <thread>

#include "audio.h"
#include "session.h"
#include "studio.h"
#include "touchsource.h"
#include "wav.h"

// The app's state for QML: owns the studio, the session (the pages), the
// touch helper, the audio stream and the recorder, and turns them into
// properties.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int padState READ padState NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString padName READ padName NOTIFY changed)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount CONSTANT)
    Q_PROPERTY(QStringList pageNames READ pageNames CONSTANT)
    Q_PROPERTY(QStringList pageBlurbs READ pageBlurbs CONSTANT)
    Q_PROPERTY(QVariantList pageColors READ pageColors CONSTANT)
    Q_PROPERTY(QString pageHelp READ pageHelp NOTIFY pageChanged)
    Q_PROPERTY(QString pageStatus READ pageStatus NOTIFY changed)
    Q_PROPERTY(QVariantList pagesPlaying READ pagesPlaying NOTIFY changed)
    Q_PROPERTY(QVariantList pagesSilenced READ pagesSilenced NOTIFY changed)
    Q_PROPERTY(QString key READ key NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString chord READ chord NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm NOTIFY changed)
    Q_PROPERTY(QString position READ position NOTIFY changed)
    Q_PROPERTY(bool metronome READ metronome NOTIFY changed)
    Q_PROPERTY(int clips READ clips NOTIFY changed)
    Q_PROPERTY(int voices READ voices NOTIFY changed)
    Q_PROPERTY(double cpu READ cpu NOTIFY changed)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY changed)
    Q_PROPERTY(bool audioOk READ audioOk NOTIFY changed)
    Q_PROPERTY(QString audioError READ audioError NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY changed)
    Q_PROPERTY(QString lastRecording READ lastRecording NOTIFY changed)
    Q_PROPERTY(double level READ level NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    enum PadState { Idle, Starting, Ready, Playing, Failed };
    Q_ENUM(PadState)

    // live = false: no sound card and no trackpad, for screenshots and tests.
    explicit Backend(bool live = true, QObject *parent = nullptr);
    ~Backend() override;

    int padState() const;
    QString error() const;
    QString padName() const;
    int page() const { return m_session->page(); }
    void setPage(int page);
    int pageCount() const { return m_session->pageCount(); }
    QStringList pageNames() const;
    QStringList pageBlurbs() const;
    QVariantList pageColors() const;
    QString pageHelp() const { return m_session->pageHelp(m_session->page()); }
    QString pageStatus() const { return m_session->pageStatus(); }
    QVariantList pagesPlaying() const;
    // Per page: "" when it can be heard, else why not ("muted", "solo
    // elsewhere"): a part silenced on the arrange page says so everywhere.
    QVariantList pagesSilenced() const;
    QString key() const;
    QString mode() const;
    QString chord() const;
    double bpm() const { return m_session->view().bpm; }
    QString position() const;
    bool metronome() const { return m_session->view().metronome; }
    int clips() const;
    int voices() const;
    double cpu() const { return m_session->view().cpu; }
    double latencyMs() const;
    bool audioOk() const { return m_audio->running(); }
    QString audioError() const { return QString::fromStdString(m_audio->error()); }
    bool recording() const { return m_recording; }
    double recordSeconds() const;
    QString lastRecording() const { return m_lastRecording; }
    double level() const { return m_session->view().rms; }
    QString notice() const { return m_notice; }

    tf::Session *session() const { return m_session.get(); }
    float padAspect() const { return m_touch->aspect(); }

    Q_INVOKABLE void play();     // take the trackpad
    Q_INVOKABLE void giveBack(); // let go of it, sound keeps going
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void windowActive(bool active);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void toggleMetronome() { m_session->toggleMetronome(); }
    Q_INVOKABLE void shiftTempo(double d) { m_session->shiftTempo(d); }
    Q_INVOKABLE void shiftRoot(int fifths) { m_session->shiftRoot(fifths); }
    Q_INVOKABLE void undoTake() { m_session->undoTake(); }
    Q_INVOKABLE void startOver() { m_session->startOver(); }
    // A key for the pages; returns true when one used it.
    Q_INVOKABLE bool sendKey(int key, int modifiers, bool down, bool repeat, const QString &text);

    void shutdown();
    // Plays a script into the session and studio up to a moment, as fast as
    // it renders, for screenshots.
    bool simulate(const std::string &script, double until, std::string *error);

signals:
    void changed();
    void frame();
    void pageChanged();
    void noticeChanged();

private:
    void tick();
    void stopRecording();
    void recorderLoop();

    std::unique_ptr<tf::Studio> m_studio;
    std::unique_ptr<tf::Session> m_session;
    std::unique_ptr<tf::TouchSource> m_touch;
    std::unique_ptr<tf::AudioOut> m_audio;
    QTimer m_timer;
    QString m_device;
    bool m_wantPlay = false;
    bool m_pausedByFocus = false;
    bool m_simulated = false;
    uint32_t m_noticeSeq = 0;
    QString m_notice;
    int m_lastPage = -1;

    bool m_recording = false;
    QString m_recordPath, m_lastRecording;
    std::thread m_recorder;
    // Every page's background() and idle() at 250 Hz on a thread of its own,
    // so a page scheduling steps a few milliseconds ahead (a held key's roll,
    // an arrangement's launches) never waits on the screen's 60 Hz timer or
    // on touch frames that stop while a finger rests still.
    std::thread m_ticker;
    std::atomic<bool> m_tickerRun{false};
    std::atomic<bool> m_recorderRun{false};
    QElapsedTimer m_recordClock;
};
