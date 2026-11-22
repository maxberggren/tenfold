#pragma once

// The visualizer as the window sees it (the logic is visual.h):
//
//   Visualizer      each screen frame, reads the studio's view and the
//                   visual page under the session's lock, and hands
//                   Visualizer.qml its uniforms (one map, `visual.u`).
//   VisualData      a small texture of waveforms for the shaders (the
//                   voice's ring is drawn from it).
//   VisualRenderer  Visualizer.qml rendered offscreen, frame by frame on
//                   demand (QQuickRenderControl), at a fixed size: what the
//                   video recorder and `tenfold visualize` grab, so the video
//                   is the same picture as the window's however the window
//                   is sized, shown or hidden.
//   VideoRecorder   Shift+Record: frames from a VisualRenderer to ffmpeg,
//                   in step with the WAV the recorder writes (recorder.h).
//
// The second window is QML (VisualWindow.qml), opened by the page's Return
// key or the button; the pad's fingers keep playing whatever page the main
// window shows, whichever window has the focus.

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QQuickItem>
#include <QSize>
#include <QTimer>
#include <QVariantMap>
#include <memory>
#include <string>
#include <thread>

#include "recorder.h"
#include "visual.h"

class QQmlEngine;
class QQuickRenderControl;
class QQuickWindow;
class QRhiTexture;
class QRhiRenderBuffer;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;

namespace tf {
class Session;
class Studio;
}

class Visualizer : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap u READ u NOTIFY updated)
    Q_PROPERTY(double sceneScale READ sceneScale NOTIFY updated)
    Q_PROPERTY(bool showing READ showing NOTIFY updated)
    // The page's CRT glass (C): scanlines and a curve, off by default.
    Q_PROPERTY(bool crt READ crt NOTIFY crtChanged)

public:
    explicit Visualizer(tf::Session *session, QObject *parent = nullptr);

    // One frame: dt seconds since the last. Live, the window's timer calls
    // it with the time that really passed; offline, with 1 / fps.
    void step(double dt);
    // Live: step() with the time since the last call.
    void stepLive();

    // A window drew a frame of the picture: live, the world's resolution
    // follows how long frames take, so playing never drops frames.
    Q_INVOKABLE void frameDrawn();

    QVariantMap u() const { return m_u; }
    double sceneScale() const { return m_scale * m_fit; }
    // The visual page is showing in the main window.
    bool showing() const { return m_showing; }
    bool crt() const { return m_crt; }
    const QImage &data() const { return m_data; }
    int dataSerial() const { return m_dataSerial; }
    const tf::visual::Signals &signalsNow() const { return m_tracker.now(); }

signals:
    void updated();
    void crtChanged();
    // The page asked for the second window, or for it full screen.
    void windowAsked();
    void fullScreenAsked();

private:
    tf::Session *m_session;
    tf::visual::SignalTracker m_tracker;
    tf::visual::Drive m_drive;
    QVariantMap m_u;
    double m_scale = 0.5;
    double m_fit = 1.0;        // the share of m_scale the live frame time allows
    double m_frameAvg = 1.0 / 60;
    QElapsedTimer m_frames;
    bool m_showing = false;
    bool m_crt = false;
    int m_windowAsks = -1, m_fullScreenAsks = -1;
    QImage m_data;
    int m_dataSerial = 0;
    float m_wavePeak = 0.1f;
    QElapsedTimer m_clock;
};

// A 256 × 1 texture of the waveform the voice's ring is drawn from.
class VisualData : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(Visualizer *source READ source WRITE setSource NOTIFY sourceChanged)

public:
    explicit VisualData(QQuickItem *parent = nullptr);
    ~VisualData() override;
    Visualizer *source() const { return m_source; }
    void setSource(Visualizer *v);
    bool isTextureProvider() const override { return true; }
    QSGTextureProvider *textureProvider() const override;

signals:
    void sourceChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void releaseResources() override;

private:
    Visualizer *m_source = nullptr;
    mutable class DataProvider *m_provider = nullptr;
    int m_serial = -1;
};

class VisualRenderer {
public:
    VisualRenderer(QQmlEngine *engine, QSize size);
    ~VisualRenderer();
    bool init(QString *error);
    // Renders one frame and reads it back: RGBA, top row first.
    QImage frame();
    QSize size() const { return m_size; }

private:
    QQmlEngine *m_engine;
    QSize m_size;
    std::unique_ptr<QQuickRenderControl> m_rc;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QQuickItem> m_item;
    std::unique_ptr<QRhiTexture> m_tex;
    std::unique_ptr<QRhiRenderBuffer> m_ds;
    std::unique_ptr<QRhiTextureRenderTarget> m_rt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;
};

class VideoRecorder : public QObject {
    Q_OBJECT
public:
    VideoRecorder(QQmlEngine *engine, Visualizer *visual, tf::Studio *studio, QObject *parent = nullptr);
    ~VideoRecorder() override;
    // Starts with the WAV that is starting now (`wav` its final path); the
    // video goes beside it as .mp4. False, and why, when it can't.
    bool start(const QString &wav, QString *error);
    // The WAV is finished (its recorder joined): the video catches up to its
    // length, ffmpeg finishes, and the two are muxed on a thread of their
    // own; done() says where the .mp4 is (or why there is none).
    void stop();
    bool running() const { return m_encoder.running(); }

    static constexpr int kFps = 30;
    static QSize frameSize();

signals:
    void done(const QString &mp4, const QString &error);

private:
    void tick();
    QQmlEngine *m_engine;
    Visualizer *m_visual;
    tf::Studio *m_studio;
    std::unique_ptr<VisualRenderer> m_renderer;
    tf::video::Encoder m_encoder;
    std::unique_ptr<tf::video::AvSync> m_sync;
    QTimer m_timer;
    QString m_wav, m_video, m_mp4, m_ffmpeg;
    int m_skipped = 0;
    std::thread m_finisher;
};

namespace tf {
// `tenfold visualize`: a script played into the studio as `render` plays it,
// the visualizer drawn at every frame. out.mp4: the whole performance with
// its sound; out.png: the frame at `at` seconds (the picture's echoes
// warmed up over the seconds before).
struct VisualizeOptions {
    std::string script, out;
    int fps = 30, width = 1280, height = 720;
    double at = -1;        // a still at this time (png)
    double from = 0, to = -1; // the stretch of an mp4
    int quality = -1;      // the page's own unless set
};
int visualize(const VisualizeOptions &o);
}
