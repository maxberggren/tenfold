#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <memory>
#include <thread>

#include "audio.h"
#include "engine.h"
#include "enginesink.h"
#include "gesture.h"
#include "touchsource.h"
#include "wav.h"

// The app's state for QML: owns the engine, the gesture layer, the touch
// helper, the audio stream and the recorder, and turns them into properties.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int padState READ padState NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString padName READ padName NOTIFY changed)
    Q_PROPERTY(QString key READ key NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool just READ just NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm NOTIFY changed)
    Q_PROPERTY(bool tempoSet READ tempoSet NOTIFY changed)
    Q_PROPERTY(int loops READ loops NOTIFY changed)
    Q_PROPERTY(int voices READ voices NOTIFY changed)
    Q_PROPERTY(int frozen READ frozen NOTIFY changed)
    Q_PROPERTY(double cpu READ cpu NOTIFY changed)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY changed)
    Q_PROPERTY(bool audioOk READ audioOk NOTIFY changed)
    Q_PROPERTY(QString audioError READ audioError NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY changed)
    Q_PROPERTY(QString lastRecording READ lastRecording NOTIFY changed)
    Q_PROPERTY(double level READ level NOTIFY changed)

public:
    enum PadState { Idle, Starting, Ready, Playing, Failed };
    Q_ENUM(PadState)

    // live = false: no sound card and no trackpad, for screenshots and tests.
    explicit Backend(bool live = true, QObject *parent = nullptr);
    ~Backend() override;

    int padState() const;
    QString error() const;
    QString padName() const;
    QString key() const;
    QString mode() const;
    bool just() const { return m_gv.just; }
    double bpm() const;
    bool tempoSet() const { return m_engine->view().tempoSet; }
    int loops() const;
    int voices() const;
    int frozen() const;
    double cpu() const { return m_engine->view().cpu; }
    double latencyMs() const;
    bool audioOk() const { return m_audio->running(); }
    QString audioError() const { return QString::fromStdString(m_audio->error()); }
    bool recording() const { return m_recording; }
    double recordSeconds() const;
    QString lastRecording() const { return m_lastRecording; }
    double level() const { return m_engine->view().rms; }

    tf::Engine *engine() const { return m_engine.get(); }
    const tf::GestureView &gestureView() const { return m_gv; }
    float padAspect() const { return m_touch->aspect(); }

    Q_INVOKABLE void play();         // take the trackpad
    Q_INVOKABLE void giveBack();     // let go of it, sound keeps going
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void windowActive(bool active);
    Q_INVOKABLE void shiftRoot(int fifths);
    Q_INVOKABLE void shiftFamily(int step);
    Q_INVOKABLE void toggleJust();
    Q_INVOKABLE void resetKnobs();
    Q_INVOKABLE void dissolve();
    Q_INVOKABLE void dissolveAll();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE QString roleName(int role) const;
    Q_INVOKABLE QString roleSlide(int role) const;
    Q_INVOKABLE QString roleTwist(int role) const;

    void shutdown();
    // Plays a script into the gesture layer and engine up to a moment, as
    // fast as it renders, with the screen following for its last second.
    bool simulate(const std::string &script, double until, std::string *error);

signals:
    void changed();
    void frame();

private:
    void tick();
    void stopRecording();
    void recorderLoop();

    std::unique_ptr<tf::Engine> m_engine;
    std::unique_ptr<tf::EngineSink> m_sink;
    std::unique_ptr<tf::Gesture> m_gesture;
    std::unique_ptr<tf::TouchSource> m_touch;
    std::unique_ptr<tf::AudioOut> m_audio;
    tf::GestureView m_gv;
    QTimer m_timer;
    QString m_device;
    bool m_wantPlay = false;
    bool m_pausedByFocus = false;
    bool m_simulated = false;

    bool m_recording = false;
    QString m_recordPath, m_lastRecording;
    std::thread m_recorder;
    std::atomic<bool> m_recorderRun{false};
    QElapsedTimer m_recordClock;
};
