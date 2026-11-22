#include "visualizer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGTexture>
#include <QSGTextureProvider>
#include <QThread>
#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <rhi/qrhi.h>
#include <sstream>
#include <thread>

#include "dsp.h"
#include "offline.h"
#include "session.h"
#include "studio.h"
#include "wav.h"

using namespace tf;

// --- the uniforms ----------------------------------------------------------------

Visualizer::Visualizer(Session *session, QObject *parent) : QObject(parent), m_session(session) {
    m_data = QImage(256, 1, QImage::Format_RGBA8888);
    m_data.fill(QColor(128, 128, 128));
    m_clock.start();
    // Every uniform from the start, so the picture's bindings never see a
    // missing one.
    for (const visual::Uniform &x : visual::uniforms(m_tracker.now(), visual::Settings(), visual::Hand(), m_drive, 0, 0, 0))
        m_u.insert(QLatin1String(x.name), QVector4D(x.v[0], x.v[1], x.v[2], x.v[3]));
}

void Visualizer::stepLive() {
    const double dt = m_clock.nsecsElapsed() * 1e-9;
    m_clock.restart();
    step(dt);
}

void Visualizer::step(double dt) {
    visual::VisualPage::State state;
    {
        std::lock_guard lock(m_session->lock());
        visual::VisualPage *page = m_session->visual();
        page->tick(dt);
        state = page->state();
        m_showing = m_session->page() == m_session->visualPage();
        const StudioView &v = m_session->view();
        m_tracker.update(v, dt);

        // The waveform the voice's ring is drawn from: the microphone's
        // while it sings, else the mix's, each against its own recent peak.
        uchar *row = m_data.scanLine(0);
        const float *vd = v.partCount > visual::Vocal ? v.parts[visual::Vocal].data : nullptr;
        const bool singing = vd && m_tracker.now().vocal > 0.05f && vd[13] > 0.5f; // vocal.cpp pub::HasInput
        float peak = 0;
        float wave[256];
        for (int i = 0; i < 256; ++i) {
            // The microphone's scope is 64 (min, max) pairs from vocal.cpp's
            // pub::Scope (64): drawn as its envelope, out and back.
            wave[i] = singing ? vd[64 + 2 * (i / 4) + (i & 1)] : v.scope[i];
            peak = std::max(peak, std::fabs(wave[i]));
        }
        m_wavePeak = std::max(peak, m_wavePeak * float(std::exp(-dt / 2.0)));
        const float g = 1.0f / std::max(m_wavePeak, 0.02f);
        for (int i = 0; i < 256; ++i) {
            const int c = std::clamp(int(std::lround(127.5f + 127.5f * wave[i] * g)), 0, 255);
            row[4 * i] = row[4 * i + 1] = row[4 * i + 2] = uchar(c);
            row[4 * i + 3] = 255;
        }
        ++m_dataSerial;
    }
    m_drive.step(m_tracker.now(), state.settings.look, dt);
    m_scale = visual::sceneScale(state.settings.quality);
    QVariantMap u;
    for (const visual::Uniform &x : visual::uniforms(m_tracker.now(), state.settings, state.hand, m_drive, 0, 0, 0))
        u.insert(QLatin1String(x.name), QVector4D(x.v[0], x.v[1], x.v[2], x.v[3]));
    m_u = u;
    const bool askWindow = m_windowAsks >= 0 && state.windowAsks != m_windowAsks;
    const bool askFull = m_fullScreenAsks >= 0 && state.fullScreenAsks != m_fullScreenAsks;
    m_windowAsks = state.windowAsks;
    m_fullScreenAsks = state.fullScreenAsks;
    emit updated();
    if (askWindow)
        emit windowAsked();
    if (askFull)
        emit fullScreenAsked();
}

// --- the waveform texture ----------------------------------------------------------

class DataProvider : public QSGTextureProvider {
public:
    ~DataProvider() override { delete m_tex; }
    QSGTexture *texture() const override { return m_tex; }
    void set(QSGTexture *t) {
        delete m_tex;
        m_tex = t;
        emit textureChanged();
    }

private:
    QSGTexture *m_tex = nullptr;
};

VisualData::VisualData(QQuickItem *parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

VisualData::~VisualData() {
    // The provider belongs to the render thread; it goes when that is ready.
    if (m_provider)
        m_provider->deleteLater();
}

void VisualData::setSource(Visualizer *v) {
    if (m_source == v)
        return;
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = v;
    if (v)
        connect(v, &Visualizer::updated, this, &QQuickItem::update);
    emit sourceChanged();
}

QSGTextureProvider *VisualData::textureProvider() const {
    if (!m_provider)
        m_provider = new DataProvider;
    return m_provider;
}

QSGNode *VisualData::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    // Nothing is drawn; this is the moment the render thread may make the
    // texture (the window's thread waits meanwhile).
    if (m_source && window() && m_source->dataSerial() != m_serial) {
        m_serial = m_source->dataSerial();
        textureProvider();
        QSGTexture *t = window()->createTextureFromImage(m_source->data());
        t->setFiltering(QSGTexture::Linear);
        t->setHorizontalWrapMode(QSGTexture::Repeat);
        m_provider->set(t);
    }
    return old;
}

void VisualData::releaseResources() {
    if (m_provider) {
        m_provider->deleteLater();
        m_provider = nullptr;
    }
}

// --- offscreen ---------------------------------------------------------------------

VisualRenderer::VisualRenderer(QQmlEngine *engine, QSize size) : m_engine(engine), m_size(size) {}

VisualRenderer::~VisualRenderer() {
    // The picture before the window, the window before its render control,
    // the RHI's objects while the RHI is still there.
    m_item.reset();
    m_rp.reset();
    m_rt.reset();
    m_ds.reset();
    m_tex.reset();
    m_window.reset();
    m_rc.reset();
}

bool VisualRenderer::init(QString *error) {
    m_rc = std::make_unique<QQuickRenderControl>();
    m_window = std::make_unique<QQuickWindow>(m_rc.get());
    m_window->setGeometry(0, 0, m_size.width(), m_size.height());
    m_window->contentItem()->setSize(m_size);
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/Visualizer.qml")));
    QObject *obj = component.create();
    m_item.reset(qobject_cast<QQuickItem *>(obj));
    if (!m_item) {
        delete obj;
        if (error)
            *error = component.errorString();
        return false;
    }
    m_item->setParentItem(m_window->contentItem());
    m_item->setSize(m_size);
    m_item->setProperty("crt", 1.0);
    if (qEnvironmentVariableIsSet("TENFOLD_VISUAL_STAGE"))
        m_item->setProperty("stage", qEnvironmentVariableIntValue("TENFOLD_VISUAL_STAGE"));
    if (!m_rc->initialize()) {
        if (error)
            *error = QStringLiteral("no graphics for the offscreen picture");
        return false;
    }
    QRhi *rhi = m_rc->rhi();
    m_tex.reset(rhi->newTexture(QRhiTexture::RGBA8, m_size, 1,
                                QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    m_ds.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, m_size, 1));
    if (!m_tex->create() || !m_ds->create()) {
        if (error)
            *error = QStringLiteral("no texture for the offscreen picture");
        return false;
    }
    QRhiTextureRenderTargetDescription desc{QRhiColorAttachment(m_tex.get())};
    desc.setDepthStencilBuffer(m_ds.get());
    m_rt.reset(rhi->newTextureRenderTarget(desc));
    m_rp.reset(m_rt->newCompatibleRenderPassDescriptor());
    m_rt->setRenderPassDescriptor(m_rp.get());
    if (!m_rt->create()) {
        if (error)
            *error = QStringLiteral("no render target for the offscreen picture");
        return false;
    }
    m_window->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(m_rt.get()));
    return true;
}

QImage VisualRenderer::frame() {
    QRhi *rhi = m_rc->rhi();
    m_rc->polishItems();
    m_rc->beginFrame();
    m_rc->sync();
    m_rc->render();
    QRhiReadbackResult rb;
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    u->readBackTexture(QRhiReadbackDescription(m_tex.get()), &rb);
    m_rc->commandBuffer()->resourceUpdate(u);
    // An offscreen frame ends waiting for the GPU, so the readback is in.
    m_rc->endFrame();
    QImage img(reinterpret_cast<const uchar *>(rb.data.constData()), rb.pixelSize.width(), rb.pixelSize.height(),
               QImage::Format_RGBA8888);
    if (rhi->isYUpInFramebuffer())
        return img.flipped(Qt::Vertical);
    return img.copy();
}

// --- live video ----------------------------------------------------------------------

QSize VideoRecorder::frameSize() {
    // TENFOLD_VIDEO_SIZE=1920x1080 for a larger video, at the machine's cost.
    int w = 1280, h = 720;
    const QByteArray env = qgetenv("TENFOLD_VIDEO_SIZE");
    if (!env.isEmpty() && std::sscanf(env.constData(), "%dx%d", &w, &h) == 2) {
        w = std::clamp(w, 64, 3840) & ~1;
        h = std::clamp(h, 64, 2160) & ~1;
    }
    return {w, h};
}

VideoRecorder::VideoRecorder(QQmlEngine *engine, Visualizer *visual, Studio *studio, QObject *parent)
    : QObject(parent), m_engine(engine), m_visual(visual), m_studio(studio) {
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 / kFps / 2);
    connect(&m_timer, &QTimer::timeout, this, &VideoRecorder::tick);
}

VideoRecorder::~VideoRecorder() {
    m_timer.stop();
    if (m_finisher.joinable())
        m_finisher.join();
    QString ignored;
    m_encoder.finish(&ignored);
}

bool VideoRecorder::start(const QString &wav, QString *error) {
    if (m_finisher.joinable())
        m_finisher.join();
    m_ffmpeg = video::findFfmpeg();
    if (m_ffmpeg.isEmpty()) {
        if (error)
            *error = tr("no ffmpeg found");
        return false;
    }
    const QSize size = frameSize();
    if (!m_renderer || m_renderer->size() != size) {
        m_renderer = std::make_unique<VisualRenderer>(m_engine, size);
        if (!m_renderer->init(error)) {
            m_renderer.reset();
            return false;
        }
    }
    m_wav = wav;
    QString base = wav;
    if (base.endsWith(QLatin1String(".wav")))
        base.chop(4);
    m_mp4 = base + QStringLiteral(".mp4");
    m_video = base + QStringLiteral(".video.mp4");
    if (!m_encoder.start(m_ffmpeg, video::encodeArgs(size.width(), size.height(), kFps, m_video), error))
        return false;
    m_sync = std::make_unique<video::AvSync>(kFps, double(m_studio->sampleRate()));
    m_skipped = 0;
    // Twice a frame: the sync writes a frame when one is due, so the timer's
    // jitter never decides when one is missed.
    m_timer.start();
    tick();
    return true;
}

void VideoRecorder::tick() {
    if (!m_sync)
        return;
    const int copies = m_sync->take(int64_t(m_studio->recordedFrames()));
    if (copies <= 0)
        return;
    // Behind (the encoder or the disk can't keep up): the last frame again,
    // which costs the queue nothing, rather than a pile of new ones.
    if (m_encoder.backlog() > 8) {
        m_encoder.push({}, copies);
        ++m_skipped;
        return;
    }
    const QImage img = m_renderer->frame();
    m_encoder.push(QByteArray(reinterpret_cast<const char *>(img.constBits()), qsizetype(img.sizeInBytes())), copies);
}

void VideoRecorder::stop() {
    if (!m_sync)
        return;
    m_timer.stop();
    m_encoder.push({}, m_sync->finish(int64_t(m_studio->recordedFrames())));
    m_sync.reset();
    // Finishing the encode and muxing take a moment: off this thread, so
    // the window and the pad keep going.
    const QString ffmpeg = m_ffmpeg, video = m_video, wav = m_wav, mp4 = m_mp4;
    auto *encoder = &m_encoder;
    if (m_finisher.joinable())
        m_finisher.join();
    m_finisher = std::thread([this, encoder, ffmpeg, video, wav, mp4] {
        QString error;
        bool ok = encoder->finish(&error);
        if (ok)
            ok = video::runFfmpeg(ffmpeg, video::muxArgs(video, wav, mp4), &error);
        if (ok)
            QFile::remove(video);
        QMetaObject::invokeMethod(this, [this, ok, mp4, error] { emit done(ok ? mp4 : QString(), error); },
                                  Qt::QueuedConnection);
    });
}

// --- offline ---------------------------------------------------------------------------

namespace tf {

int visualize(const VisualizeOptions &o) {
    flushDenormals();
    std::ifstream f(o.script);
    if (!f) {
        std::fprintf(stderr, "tenfold: cannot read %s\n", o.script.c_str());
        return 1;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    ScriptPlayer player;
    std::string err;
    if (!player.load(ss.str(), &err)) {
        std::fprintf(stderr, "tenfold: %s\n", err.c_str());
        return 1;
    }
    const QString out = QString::fromLocal8Bit(o.out.c_str());
    const bool still = out.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) ||
                       out.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive);
    const double end = still ? std::min(o.at >= 0 ? o.at : player.end(), player.end())
                             : (o.to > 0 ? std::min(o.to, player.end()) : player.end());
    const double start = still ? std::max(0.0, end - 4.0) : std::clamp(o.from, 0.0, end);
    QString ffmpeg;
    if (!still) {
        ffmpeg = video::findFfmpeg();
        if (ffmpeg.isEmpty()) {
            std::fprintf(stderr, "tenfold: a video needs ffmpeg, and there is none (TENFOLD_FFMPEG names one)\n");
            return 1;
        }
    }

    constexpr float kRate = 48000.0f;
    Studio studio(kRate);
    studio.setTimeOrigin(0);
    setScriptClock(0);
    Session session(&studio);
    session.setClock(&scriptClock);
    session.setAspect(1.6f);
    if (o.quality >= 0) {
        std::lock_guard lock(session.lock());
        KeyEvent k;
        k.key = Qt::Key_Q;
        // Q steps the quality round; step until it is the one asked for.
        for (int i = 0; i < 4 && session.visual()->state().settings.quality != std::clamp(o.quality, 0, 3); ++i) {
            const int page = session.page();
            session.setPage(session.visualPage());
            session.key(k);
            session.setPage(page);
        }
    }
    Visualizer vis(&session);
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("visual"), &vis);
    VisualRenderer renderer(&engine, QSize(o.width & ~1, o.height & ~1));
    QString error;
    if (!renderer.init(&error)) {
        std::fprintf(stderr, "tenfold: %s\n", qPrintable(error));
        return 1;
    }

    const QString base = still ? QString() : (out.endsWith(QLatin1String(".mp4")) ? out.chopped(4) : out);
    const QString wavPath = base + QStringLiteral(".audio.wav"), videoPath = base + QStringLiteral(".video.mp4");
    WavWriter wav;
    video::Encoder encoder;
    if (!still) {
        if (!wav.open(wavPath.toStdString(), int(kRate))) {
            std::fprintf(stderr, "tenfold: cannot write %s\n", qPrintable(wavPath));
            return 1;
        }
        if (!encoder.start(ffmpeg, video::encodeArgs(renderer.size().width(), renderer.size().height(), o.fps, videoPath),
                           &error)) {
            std::fprintf(stderr, "tenfold: %s\n", qPrintable(error));
            return 1;
        }
    }

    // The studio in ticks of 1/125 s, as `render` plays a script; a frame
    // whenever the sound has passed its time. The sound written is from
    // `start` on, so frame k is the picture at start + k / fps.
    const double tickRate = 125.0;
    const int perTick = int(std::lround(kRate / tickRate));
    std::vector<float> buf(size_t(perTick) * 2);
    const int64_t startSample = int64_t(std::llround(start * kRate));
    int64_t sample = 0;
    int frame = 0;
    QImage last;
    const double frameDt = 1.0 / o.fps;
    // The visual state before `start` matters too (the echoes, the drift):
    // it is stepped from four seconds before at the frame rate, drawn only
    // from there.
    const double warm = std::max(0.0, start - 4.0);
    double nextStep = warm;
    // How long a frame takes to draw and read back, for the report.
    QElapsedTimer drawClock;
    qint64 drawNs = 0;
    int drawn = 0;
    for (int tick = 0;; ++tick) {
        const double t = tick / tickRate;
        if (t > end + 1e-9)
            break;
        session.refresh();
        player.tick(t, session);
        studio.render(buf.data(), perTick);
        const int64_t s0 = sample;
        sample += perTick;
        if (!still && sample > startSample) {
            const int64_t from = std::max<int64_t>(0, startSample - s0);
            wav.write(buf.data() + 2 * from, size_t(perTick - from));
        }
        const double heard = double(sample) / kRate;
        while (nextStep <= heard && nextStep <= end + 1e-9) {
            session.refresh();
            vis.step(frameDt);
            if (nextStep >= start - 1e-9 || still) {
                drawClock.start();
                last = renderer.frame();
                drawNs += drawClock.nsecsElapsed();
                ++drawn;
                if (!still && nextStep >= start - 1e-9) {
                    encoder.push(QByteArray(reinterpret_cast<const char *>(last.constBits()), qsizetype(last.sizeInBytes())),
                                 1);
                    ++frame;
                    if (frame % (o.fps * 5) == 0)
                        std::fprintf(stderr, "  %.0f s\n", nextStep);
                }
            }
            nextStep += frameDt;
        }
    }
    if (qEnvironmentVariableIsSet("TENFOLD_VISUAL_DEBUG")) {
        const QVariantMap u = vis.u();
        for (auto it = u.begin(); it != u.end(); ++it) {
            const QVector4D v = it.value().value<QVector4D>();
            std::fprintf(stderr, "%-8s %8.3f %8.3f %8.3f %8.3f\n", qPrintable(it.key()), v.x(), v.y(), v.z(), v.w());
        }
    }
    if (still) {
        if (qEnvironmentVariableIsSet("TENFOLD_VISUAL_DEBUG"))
            std::fprintf(stderr, "%.1f ms a frame\n", drawn ? double(drawNs) / drawn / 1e6 : 0.0);
        if (!last.save(out)) {
            std::fprintf(stderr, "tenfold: cannot write %s\n", qPrintable(out));
            return 1;
        }
        return 0;
    }
    wav.close();
    if (!encoder.finish(&error) || !video::runFfmpeg(ffmpeg, video::muxArgs(videoPath, wavPath, out), &error)) {
        std::fprintf(stderr, "tenfold: %s\n", qPrintable(error));
        return 1;
    }
    QFile::remove(videoPath);
    QFile::remove(wavPath);
    std::printf("%s: %d frames at %d fps, %.1f s; %.1f ms a frame to draw and read back (%dx%d)\n", qPrintable(out),
                frame, o.fps, double(frame) / o.fps, drawn ? double(drawNs) / drawn / 1e6 : 0.0, renderer.size().width(),
                renderer.size().height());
    return 0;
}

}
