#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <thread>

#include "console.h"
#include "platform.h"
#include "session.h"
#include "songfile.h"
#include "studio.h"
#include "wav.h"

class QQmlEngine;
class Visualizer;
class VideoRecorder;

// The app's state for QML: owns the studio, the session (the pages), the
// touch helper, the audio stream and the recorder, and turns them into
// properties.
// The console's arrows and key symbols for the window's text: QML asks for
// "image://icon/<hex code point>/<rrggbb>" and gets the same line art the
// pages draw (console::drawIcon).
class IconProvider : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int padState READ padState NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString padName READ padName NOTIFY changed)
    // The pad isn't readable as this user, so taking it asks for the password.
    Q_PROPERTY(bool padNeedsPassword READ padNeedsPassword NOTIFY changed)
    // The box that asks for the trackpad stands (padprompt.h), and the pad
    // was put down only because tenfold went behind another window.
    Q_PROPERTY(bool padPrompt READ padPrompt NOTIFY changed)
    Q_PROPERTY(bool padAway READ padAway NOTIFY changed)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount CONSTANT)
    Q_PROPERTY(QStringList pageNames READ pageNames CONSTANT)
    // The key each page answers to, as its tab says it (1–8, ⇧1, 0, 9).
    Q_PROPERTY(QStringList pageKeys READ pageKeys CONSTANT)
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
    Q_PROPERTY(bool metronome READ metronome NOTIFY changed)
    Q_PROPERTY(bool quantise READ quantise NOTIFY changed)
    Q_PROPERTY(bool loopsPaused READ loopsPaused NOTIFY changed)
    Q_PROPERTY(int clips READ clips NOTIFY changed)
    Q_PROPERTY(int voices READ voices NOTIFY changed)
    Q_PROPERTY(double cpu READ cpu NOTIFY changed)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY changed)
    Q_PROPERTY(bool audioOk READ audioOk NOTIFY changed)
    Q_PROPERTY(QString audioError READ audioError NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    // Shift+Record: the visualizer's picture is recorded with the sound.
    Q_PROPERTY(bool recordingVideo READ recordingVideo NOTIFY changed)
    Q_PROPERTY(int arrangePage READ arrangePage CONSTANT)
    Q_PROPERTY(int visualPage READ visualPage CONSTANT)
    Q_PROPERTY(double recordSeconds READ recordSeconds NOTIFY changed)
    Q_PROPERTY(QString lastRecording READ lastRecording NOTIFY changed)
    // The bottom bar's "Saved …": the last recording, for five seconds after it lands.
    Q_PROPERTY(QString savedNotice READ savedNotice NOTIFY changed)
    Q_PROPERTY(double level READ level NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    // The tutorial's step, for its panel.
    // The loop Space last made on this page, while it can be resized: its
    // beats, and what Shorter / Longer would make it (0: none that way).
    Q_PROPERTY(int loopBeats READ loopBeats NOTIFY changed)
    Q_PROPERTY(int loopShorterBeats READ loopShorterBeats NOTIFY changed)
    Q_PROPERTY(int loopLongerBeats READ loopLongerBeats NOTIFY changed)
    // The showing page has a loop playing that New loop would stop.
    Q_PROPERTY(bool canNewLoop READ canNewLoop NOTIFY changed)
    Q_PROPERTY(bool tutorialActive READ tutorialActive NOTIFY changed)
    Q_PROPERTY(int tutorialStep READ tutorialStep NOTIFY changed)
    Q_PROPERTY(int tutorialCount READ tutorialCount CONSTANT)
    Q_PROPERTY(QStringList tutorialChapters READ tutorialChapters CONSTANT)
    Q_PROPERTY(QVariantList tutorialChapterColors READ tutorialChapterColors CONSTANT)
    Q_PROPERTY(int tutorialChapter READ tutorialChapter NOTIFY changed)
    Q_PROPERTY(int tutorialPage READ tutorialPage NOTIFY changed)
    Q_PROPERTY(QString tutorialTitle READ tutorialTitle NOTIFY changed)
    Q_PROPERTY(QString tutorialText READ tutorialText NOTIFY changed)
    Q_PROPERTY(QString tutorialTip READ tutorialTip NOTIFY changed)
    Q_PROPERTY(QString tutorialCheer READ tutorialCheer NOTIFY changed)
    Q_PROPERTY(QString tutorialButton READ tutorialButton NOTIFY changed)
    Q_PROPERTY(QStringList tutorialKeys READ tutorialKeys NOTIFY changed)
    Q_PROPERTY(QVariantList tutorialChecks READ tutorialChecks NOTIFY changed)
    // 0 working on it, 1 just done (celebrating), 2 done before (came back with Back)
    Q_PROPERTY(int tutorialState READ tutorialState NOTIFY changed)

public:
    enum PadState { Idle, Starting, Ready, Playing, Failed };
    Q_ENUM(PadState)

    // live = false: no sound card and no trackpad, for screenshots and tests.
    explicit Backend(bool live = true, QObject *parent = nullptr);
    ~Backend() override;

    int padState() const;
    QString error() const;
    QString padName() const;
    bool padNeedsPassword() const;
    bool padPrompt() const;
    bool padAway() const { return m_pausedByFocus; }
    int page() const { return m_session->page(); }
    void setPage(int page);
    int pageCount() const { return m_session->pageCount(); }
    int arrangePage() const { return m_session->arrangePage(); }
    int visualPage() const { return m_session->visualPage(); }
    bool recordingVideo() const { return m_recordingVideo; }
    // The visualizer the window draws, stepped every screen frame, and the
    // engine its offscreen video picture is made in (main.cpp).
    void setVisualizer(Visualizer *visual, QQmlEngine *engine);
    QStringList pageNames() const;
    QStringList pageKeys() const;
    QStringList pageBlurbs() const;
    QVariantList pageColors() const;
    QString pageHelp() const { return tf::console::keyNames(m_session->pageHelp(m_session->page())); }
    QString pageStatus() const { return tf::console::keyNames(m_session->pageStatus()); }
    QVariantList pagesPlaying() const;
    // Per page: "" when it can be heard, else why not ("muted", "solo
    // elsewhere"): a part silenced on the arrange page says so everywhere.
    QVariantList pagesSilenced() const;
    QString key() const;
    QString mode() const;
    QString chord() const;
    double bpm() const { return m_session->view().bpm; }
    bool metronome() const { return m_session->view().metronome; }
    bool quantise() const { return m_session->view().quantise; }
    bool loopsPaused() const { return m_session->paused(); }
    int clips() const;
    int voices() const;
    double cpu() const { return m_session->view().cpu; }
    double latencyMs() const;
    bool audioOk() const { return m_audio->running(); }
    QString audioError() const { return QString::fromStdString(m_audio->error()); }
    // Recording the mix; a simulated window shows what its script asked for.
    bool recording() const { return m_recording || (m_simulated && m_session->mixRecording()); }
    double recordSeconds() const;
    QString lastRecording() const { return m_lastRecording; }
    QString savedNotice() const { return m_savedNotice; }
    double level() const { return m_session->view().rms; }
    QString notice() const { return tf::console::keyNames(m_notice); }

    bool tutorialActive() const { return m_session->tutorial().active(); }
    int tutorialStep() const { return m_session->tutorial().index(); }
    int tutorialCount() const { return m_session->tutorial().count(); }
    QStringList tutorialChapters() const { return m_session->tutorial().chapters(); }
    int tutorialChapter() const { return m_session->tutorial().chapterIndex(); }
    int tutorialPage() const { return m_session->tutorial().step().page; }
    QString tutorialTitle() const { return tf::console::keyNames(m_session->tutorial().step().title); }
    QString tutorialText() const { return tf::console::keyNames(m_session->tutorial().step().text); }
    QString tutorialTip() const { return tf::console::keyNames(m_session->tutorial().step().tip); }
    QString tutorialCheer() const { return tf::console::keyNames(m_session->tutorial().step().cheer); }
    QString tutorialButton() const { return tf::console::keyNames(m_session->tutorial().step().button); }
    QStringList tutorialKeys() const {
        QStringList keys = m_session->tutorial().step().keys;
        for (QString &k : keys)
            k = tf::console::keyNames(k);
        return keys;
    }
    QVariantList tutorialChecks() const;
    QVariantList tutorialChapterColors() const;
    int tutorialState() const { return int(m_session->tutorial().state()); }

    tf::Session *session() const { return m_session.get(); }

    Q_INVOKABLE void play();     // take the trackpad
    Q_INVOKABLE void giveBack(); // let go of it, sound keeps going
    Q_INVOKABLE bool closeMenu(); // a page's right-click menu, if one is open
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void windowActive(bool active);
    Q_INVOKABLE void toggleRecording();
    // Record with the picture (needs ffmpeg; without it, the sound alone and a notice).
    Q_INVOKABLE void toggleVideoRecording();
    Q_INVOKABLE void toggleMetronome() { m_session->toggleMetronome(); }
    Q_INVOKABLE void toggleQuantise() { m_session->toggleQuantise(); }
    Q_INVOKABLE void togglePause() { m_session->togglePause(); }
    Q_INVOKABLE void shiftTempo(double d) { m_session->shiftTempo(d); }
    Q_INVOKABLE void shiftRoot(int fifths) { m_session->shiftRoot(fifths); }
    Q_INVOKABLE void undoTake() { m_session->undoTake(); }
    Q_INVOKABLE void redoTake() { m_session->redoTake(); }
    Q_INVOKABLE void resizeLoop(int dir) { m_session->resizeLoop(dir); }
    // Text for a QML Text with textFormat StyledText: the console's own
    // arrows and key symbols as inline images in `color`, `px` the font's
    // pixel size. hasIcons says whether a string needs it (plain text can
    // elide; styled text can't).
    Q_INVOKABLE QString rich(const QString &text, const QColor &color, int px) const;
    Q_INVOKABLE bool hasIcons(const QString &text) const;
    // A key's name as this system says it (⌘ for Ctrl on macOS).
    Q_INVOKABLE QString keys(const QString &text) const { return tf::console::keyNames(text); }
    int loopBeats() const { return m_session->loopBeats(0); }
    int loopShorterBeats() const { return m_session->loopBeats(-1); }
    int loopLongerBeats() const { return m_session->loopBeats(1); }
    bool canNewLoop() const { return m_session->canNewLoop(); }
    Q_INVOKABLE void newLoop() { m_session->newLoop(); }
    Q_INVOKABLE void startOver() { m_session->startOver(); }
    // Song files: Ctrl+S, Ctrl+Shift+S, Ctrl+O, and Ctrl+Shift+O for the
    // song kept when tenfold last quit.
    Q_INVOKABLE void saveSong() { m_songs->save(); }
    Q_INVOKABLE void saveSongAs() { m_songs->saveAs(); }
    Q_INVOKABLE void openSong() { m_songs->open(); }
    // A song the system hands over (a song file opened from the Finder).
    void openSongFile(const QString &path) { m_songs->openFrom(path); }
    Q_INVOKABLE void reopenLastSong() { m_songs->reopenLast(); }
    Q_INVOKABLE void toggleTutorial();
    Q_INVOKABLE void tutorialNext();
    Q_INVOKABLE void tutorialBack();
    // A file dragged onto the window (its path or URL), or a link: the
    // page that takes such things (the Sampler) gets it and is shown.
    // paste() hands over the clipboard's text the same way (Ctrl+V).
    Q_INVOKABLE bool drop(const QString &what);
    Q_INVOKABLE void paste();
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
    // Taking and putting down the pad, leaving alone whether it was put
    // down for the player (m_pausedByFocus).
    void take();
    void release();
    void stopRecording();
    void recorderLoop();

    std::unique_ptr<tf::Studio> m_studio;
    std::unique_ptr<tf::Session> m_session;
    std::unique_ptr<tf::Pad> m_touch;
    std::unique_ptr<tf::Audio> m_audio;
    std::unique_ptr<tf::FilePicker> m_picker;
    std::unique_ptr<tf::SongFiles> m_songs;
    bool m_shutDown = false;
    QTimer m_timer;
    bool m_wantPlay = false;
    bool m_pausedByFocus = false;
    bool m_inFront = true;
    bool m_simulated = false;
    uint32_t m_noticeSeq = 0;
    QString m_notice;
    int m_lastPage = -1;

    bool m_recording = false;
    bool m_recordingVideo = false;
    Visualizer *m_visual = nullptr;
    std::unique_ptr<VideoRecorder> m_video;
    QString m_recordPath, m_lastRecording;
    QString m_savedNotice;
    QTimer m_savedTimer; // clears m_savedNotice
    void noteSaved(const QString &path);
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
