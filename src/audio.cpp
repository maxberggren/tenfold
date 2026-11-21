#include "audio.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <time.h>

#include <cstdlib>
#include <mutex>

#include "dsp.h"
#include "studio.h"

namespace tf {

struct AudioOut::Impl {
    AudioOut *owner = nullptr;
    Studio *studio = nullptr;
    float rate = 48000;
    pw_thread_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    bool flushed = false;
    bool clockSet = false;

    // The microphone.
    pw_stream *capture = nullptr;
    spa_hook captureListener{};
    std::atomic<uint64_t> captured{0}; // samples it ever delivered
    uint64_t seen = 0;                 // as the playback side last saw it
    uint64_t quiet = 0;                // playback samples since it last delivered
    std::atomic<int> captureState{PW_STREAM_STATE_UNCONNECTED};
    std::atomic<float> inMs{0}, outMs{0};
    int reported = -1;
    mutable std::mutex errorLock;
    std::string inputError;
};

// The latency the server reports for a stream: from the device to us
// (capture) or from us to the device (playback), with what its resampler
// holds, in milliseconds.
static float streamMs(pw_stream *stream, float rate) {
    pw_time t{};
    if (pw_stream_get_time_n(stream, &t, sizeof(t)) < 0 || t.rate.denom == 0)
        return 0;
    const double delay = double(t.delay) * t.rate.num / t.rate.denom;
    return float((delay + double(t.buffered) / rate) * 1000.0);
}

void onProcess(void *data) {
    auto *d = static_cast<AudioOut::Impl *>(data);
    AudioOut *self = d->owner;
    if (!d->flushed) {
        flushDenormals();
        d->flushed = true;
    }
    pw_buffer *b = pw_stream_dequeue_buffer(d->stream);
    if (!b)
        return;
    spa_buffer *buf = b->buffer;
    auto *dst = static_cast<float *>(buf->datas[0].data);
    if (!dst) {
        pw_stream_queue_buffer(d->stream, b);
        return;
    }
    const uint32_t stride = sizeof(float) * 2;
    uint32_t frames = buf->datas[0].maxsize / stride;
    if (b->requested)
        frames = std::min<uint32_t>(frames, uint32_t(b->requested));
    if (!d->clockSet) {
        // The engine's clock starts now on the monotonic clock, the one the
        // helper stamps every touch with.
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        self->m_engine->setTimeOrigin(double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9);
        d->clockSet = true;
    }
    // The microphone is live while it keeps delivering: a second without a
    // sample (no source, or one that stopped) and the page says so.
    if (d->capture) {
        const uint64_t got = d->captured.load(std::memory_order_acquire);
        if (got != d->seen) {
            d->seen = got;
            d->quiet = 0;
        } else {
            d->quiet += frames;
        }
        const int cs = d->captureState.load(std::memory_order_relaxed);
        const int state = cs == PW_STREAM_STATE_ERROR                         ? StudioView::InputFailed
                          : d->quiet < uint64_t(self->m_rate.load()) && got > 0 ? StudioView::InputLive
                                                                               : StudioView::InputConnecting;
        const float outMs = streamMs(d->stream, float(self->m_rate.load()));
        d->outMs.store(outMs, std::memory_order_relaxed);
        self->m_engine->setInput(state, d->inMs.load(std::memory_order_relaxed), outMs);
    }
    self->m_engine->render(dst, int(frames));
    self->m_quantum.store(int(frames), std::memory_order_relaxed);
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = frames * stride;
    pw_stream_queue_buffer(d->stream, b);
}

static void onCapture(void *data) {
    auto *d = static_cast<AudioOut::Impl *>(data);
    pw_buffer *b = pw_stream_dequeue_buffer(d->capture);
    if (!b)
        return;
    spa_buffer *buf = b->buffer;
    const auto *src = static_cast<const float *>(buf->datas[0].data);
    if (src && buf->datas[0].chunk) {
        const uint32_t offset = std::min(buf->datas[0].chunk->offset, buf->datas[0].maxsize);
        const uint32_t size = std::min(buf->datas[0].chunk->size, buf->datas[0].maxsize - offset);
        const int frames = int(size / sizeof(float));
        if (frames > 0) {
            d->studio->pushInput(src + offset / sizeof(float), frames);
            d->captured.fetch_add(uint64_t(frames), std::memory_order_release);
        }
    }
    d->inMs.store(streamMs(d->capture, d->rate), std::memory_order_relaxed);
    pw_stream_queue_buffer(d->capture, b);
}

static void onCaptureState(void *data, pw_stream_state, pw_stream_state state, const char *error) {
    auto *d = static_cast<AudioOut::Impl *>(data);
    d->captureState.store(int(state), std::memory_order_relaxed);
    if (state == PW_STREAM_STATE_ERROR) {
        std::lock_guard lock(d->errorLock);
        d->inputError = error ? error : "the microphone stream failed";
    }
}

static const pw_stream_events streamEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onProcess;
    return e;
}();

static const pw_stream_events captureEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onCapture;
    e.state_changed = onCaptureState;
    return e;
}();

AudioOut::AudioOut(Studio *studio) : m_engine(studio) {}

AudioOut::~AudioOut() { stop(); }

std::string AudioOut::inputError() const {
    if (!d)
        return {};
    std::lock_guard lock(d->errorLock);
    return d->inputError;
}

bool AudioOut::start() {
    if (d)
        return true;
    pw_init(nullptr, nullptr);
    d = new Impl;
    d->owner = this;
    d->studio = m_engine;
    d->rate = m_engine->sampleRate();
    d->loop = pw_thread_loop_new("tenfold-audio", nullptr);
    if (!d->loop) {
        m_error = "Cannot start PipeWire.";
        return false;
    }
    const int rate = int(m_engine->sampleRate());
    m_rate = rate;
    char latency[32];
    std::snprintf(latency, sizeof(latency), "128/%d", rate);
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                             PW_KEY_MEDIA_ROLE, "Music", PW_KEY_NODE_NAME, "tenfold",
                                             PW_KEY_NODE_DESCRIPTION, "tenfold", PW_KEY_APP_NAME, "tenfold",
                                             PW_KEY_NODE_LATENCY, latency, nullptr);
    // TENFOLD_SINK / TENFOLD_SOURCE: a node to connect to in place of the
    // defaults (the latency measurement's own, or a chosen interface).
    if (const char *sink = std::getenv("TENFOLD_SINK"))
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, sink);
    d->stream = pw_stream_new_simple(pw_thread_loop_get_loop(d->loop), "tenfold", props, &streamEvents, d);
    if (!d->stream) {
        m_error = "Cannot create a PipeWire stream.";
        return false;
    }
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = 2;
    info.rate = uint32_t(rate);
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
    const int res = pw_stream_connect(d->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                      pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                      PW_STREAM_FLAG_RT_PROCESS),
                                      params, 1);
    if (res < 0) {
        m_error = "Cannot connect to PipeWire.";
        return false;
    }

    // The microphone: mono, the same rate and quantum, processed on the
    // same real-time thread. Without one the studio plays on; the Vocal
    // page says what is missing.
    if (!std::getenv("TENFOLD_NO_MIC")) {
        pw_properties *cprops = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Music",
            PW_KEY_NODE_NAME, "tenfold-mic", PW_KEY_NODE_DESCRIPTION, "tenfold microphone", PW_KEY_APP_NAME,
            "tenfold", PW_KEY_NODE_LATENCY, latency, nullptr);
        if (const char *source = std::getenv("TENFOLD_SOURCE"))
            pw_properties_set(cprops, PW_KEY_TARGET_OBJECT, source);
        d->capture = pw_stream_new_simple(pw_thread_loop_get_loop(d->loop), "tenfold-mic", cprops, &captureEvents, d);
        if (d->capture) {
            uint8_t cbuffer[1024];
            spa_pod_builder cb = SPA_POD_BUILDER_INIT(cbuffer, sizeof(cbuffer));
            spa_audio_info_raw ci{};
            ci.format = SPA_AUDIO_FORMAT_F32;
            ci.channels = 1;
            ci.rate = uint32_t(rate);
            ci.position[0] = SPA_AUDIO_CHANNEL_MONO;
            const spa_pod *cparams[1] = {spa_format_audio_raw_build(&cb, SPA_PARAM_EnumFormat, &ci)};
            if (pw_stream_connect(d->capture, PW_DIRECTION_INPUT, PW_ID_ANY,
                                  pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                                  PW_STREAM_FLAG_RT_PROCESS),
                                  cparams, 1) < 0) {
                std::lock_guard lock(d->errorLock);
                d->inputError = "PipeWire would not open a microphone stream";
                d->captureState = PW_STREAM_STATE_ERROR;
            }
        } else {
            std::lock_guard lock(d->errorLock);
            d->inputError = "PipeWire would not create a microphone stream";
            d->captureState = PW_STREAM_STATE_ERROR;
        }
        m_engine->setInput(StudioView::InputConnecting);
    }

    if (pw_thread_loop_start(d->loop) < 0) {
        m_error = "Cannot start the audio thread.";
        return false;
    }
    m_running = true;
    return true;
}

void AudioOut::stop() {
    if (!d)
        return;
    if (d->loop)
        pw_thread_loop_stop(d->loop);
    if (d->capture)
        pw_stream_destroy(d->capture);
    if (d->stream)
        pw_stream_destroy(d->stream);
    if (d->loop)
        pw_thread_loop_destroy(d->loop);
    delete d;
    d = nullptr;
    m_running = false;
}

}

// --- tenfold latency -----------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#include "session.h"

namespace tf {

namespace {

// One end of the measurement: a virtual device of its own, a source that
// sends a burst once armed or a sink that records everything with the
// graph's time of each cycle.
struct Probe {
    pw_stream *stream = nullptr;
    spa_hook hook{};
    bool source = false;
    float rate = 48000;
    std::vector<float> burst;
    std::atomic<bool> armed{false}, sent{false};
    std::atomic<int64_t> sentNs{0};
    std::vector<float> got;
    std::vector<int64_t> cycleNs; // per received frame, its cycle's time
    std::atomic<size_t> gotN{0};
    std::atomic<int> quantum{0};
};

void probeProcess(void *data) {
    auto *p = static_cast<Probe *>(data);
    pw_buffer *b = pw_stream_dequeue_buffer(p->stream);
    if (!b)
        return;
    spa_buffer *buf = b->buffer;
    auto *samples = static_cast<float *>(buf->datas[0].data);
    pw_time t{};
    pw_stream_get_time_n(p->stream, &t, sizeof(t));
    if (samples) {
        if (p->source) {
            uint32_t frames = buf->datas[0].maxsize / sizeof(float);
            if (b->requested)
                frames = std::min<uint32_t>(frames, uint32_t(b->requested));
            std::fill(samples, samples + frames, 0.0f);
            if (p->armed.load() && !p->sent.load()) {
                const size_t n = std::min<size_t>(frames, p->burst.size());
                std::copy(p->burst.begin(), p->burst.begin() + long(n), samples);
                p->sentNs.store(t.now);
                p->sent.store(true);
            }
            buf->datas[0].chunk->offset = 0;
            buf->datas[0].chunk->stride = sizeof(float);
            buf->datas[0].chunk->size = frames * sizeof(float);
            p->quantum.store(int(frames));
        } else if (buf->datas[0].chunk) {
            const uint32_t offset = std::min(buf->datas[0].chunk->offset, buf->datas[0].maxsize);
            const uint32_t size = std::min(buf->datas[0].chunk->size, buf->datas[0].maxsize - offset);
            const size_t frames = size / (2 * sizeof(float));
            const float *in = samples + offset / sizeof(float);
            size_t n = p->gotN.load();
            for (size_t i = 0; i < frames && n < p->got.size(); ++i, ++n) {
                p->got[n] = in[2 * i];
                p->cycleNs[n] = t.now + int64_t(double(i) / p->rate * 1e9);
            }
            p->gotN.store(n);
        }
    }
    pw_stream_queue_buffer(p->stream, b);
}

const pw_stream_events probeEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = probeProcess;
    return e;
}();

bool openProbe(Probe &p, pw_thread_loop *loop, const char *name, bool source, int rate) {
    char latency[32];
    std::snprintf(latency, sizeof(latency), "128/%d", rate);
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CLASS,
                                             source ? "Audio/Source" : "Audio/Sink", PW_KEY_NODE_NAME, name,
                                             PW_KEY_NODE_DESCRIPTION, name, PW_KEY_NODE_LATENCY, latency,
                                             PW_KEY_NODE_VIRTUAL, "true", nullptr);
    p.stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), name, props, &probeEvents, &p);
    if (!p.stream)
        return false;
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = uint32_t(rate);
    info.channels = source ? 1 : 2;
    if (source) {
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else {
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
    return pw_stream_connect(p.stream, source ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT, PW_ID_ANY,
                             pw_stream_flags(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS), params, 1) >= 0;
}

double monoNow() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

// The studio, live, for `seconds`, on the Vocal page or not; idles the
// session as the window would.
void runStudio(Studio &studio, Session &session, AudioOut &out, double seconds, const std::function<void(double)> &tick) {
    (void)studio;
    const double t0 = monoNow();
    while (monoNow() - t0 < seconds) {
        session.refresh();
        session.idle(monoNow());
        if (tick)
            tick(monoNow() - t0);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    (void)out;
}

} // namespace

int measureLatency(bool json) {
    pw_init(nullptr, nullptr);
    const int rate = 48000;
    int vocalPage = -1;

    // 1. Through the graph, against private devices: nothing is heard.
    Probe src, sink;
    src.source = true;
    src.rate = sink.rate = float(rate);
    Rng rng(0xB0057);
    src.burst.resize(size_t(rate / 50)); // 20 ms of noise
    for (float &v : src.burst)
        v = 0.4f * rng.bipolar();
    sink.got.assign(size_t(rate * 6), 0.0f);
    sink.cycleNs.assign(size_t(rate * 6), 0);
    pw_thread_loop *loop = pw_thread_loop_new("tenfold-latency", nullptr);
    pw_thread_loop_lock(loop);
    const bool probes = openProbe(src, loop, "tenfold-latency-source", true, rate) &&
                        openProbe(sink, loop, "tenfold-latency-sink", false, rate);
    pw_thread_loop_unlock(loop);
    pw_thread_loop_start(loop);
    double softwareMs = -1, reserveMs = 0;
    std::string savedSource, savedSink;
    int quantum = 0;
    std::string why;
    if (!probes) {
        why = "could not make the virtual devices";
    } else {
        // The probes stand in for the devices; a sink or source the caller
        // chose is put back for the second half.
        const char *oldSource = std::getenv("TENFOLD_SOURCE"), *oldSink = std::getenv("TENFOLD_SINK");
        savedSource = oldSource ? oldSource : "";
        savedSink = oldSink ? oldSink : "";
        setenv("TENFOLD_SOURCE", "tenfold-latency-source", 1);
        setenv("TENFOLD_SINK", "tenfold-latency-sink", 1);
        Studio studio{float(rate)};
        Session session(&studio);
        for (int pg = 0; pg < session.partCount(); ++pg)
            if (session.pageName(pg) == QStringLiteral("Vocal"))
                vocalPage = pg;
        session.setPage(vocalPage);
        session.setClock(&monoNow);
        AudioOut out(&studio);
        if (!out.start()) {
            why = out.error();
        } else {
            runStudio(studio, session, out, 4.0, [&](double t) {
                if (t > 2.0)
                    src.armed.store(true);
            });
            quantum = out.quantum();
            reserveMs = studio.inputWaiting() * 1000.0 / rate;
            out.stop();
        }
        auto restore = [](const char *name, const std::string &v) {
            if (v.empty())
                unsetenv(name);
            else
                setenv(name, v.c_str(), 1);
        };
        restore("TENFOLD_SOURCE", savedSource);
        restore("TENFOLD_SINK", savedSink);
        // Where the burst came out: the best match of it in the recording.
        const size_t n = sink.gotN.load();
        if (src.sent.load() && n > src.burst.size()) {
            double best = 0;
            size_t at = 0;
            for (size_t k = 0; k + src.burst.size() < n; k += 1) {
                double c = 0;
                for (size_t i = 0; i < src.burst.size(); i += 4)
                    c += double(src.burst[i]) * sink.got[k + i];
                if (c > best) {
                    best = c;
                    at = k;
                }
            }
            if (best > 0.05)
                softwareMs = double(sink.cycleNs[at] - src.sentNs.load()) / 1e6;
            else
                why = "the burst never came back (is the Vocal page's monitor on?)";
        } else if (why.empty()) {
            why = src.sent.load() ? "the probe sink heard nothing: PipeWire did not link tenfold to it"
                                  : "the probe source never ran: PipeWire did not link it";
        }
    }
    pw_thread_loop_stop(loop);
    if (src.stream)
        pw_stream_destroy(src.stream);
    if (sink.stream)
        pw_stream_destroy(sink.stream);
    pw_thread_loop_destroy(loop);

    // 2. The devices this machine would use, as PipeWire reports them. The
    //    Vocal page isn't showing, so nothing is heard.
    float inMs = 0, outMs = 0;
    int input = 0;
    {
        Studio studio{float(rate)};
        Session session(&studio);
        session.setClock(&monoNow);
        AudioOut out(&studio);
        if (out.start()) {
            runStudio(studio, session, out, 1.5, {});
            session.refresh();
            inMs = session.view().inputMs;
            outMs = session.view().outputMs;
            input = session.view().input;
            out.stop();
        }
    }
    const double quantumMs = quantum * 1000.0 / rate;
    if (json) {
        std::printf("{\n  \"software_ms\": %.3f,\n  \"quantum_frames\": %d,\n  \"input_waiting_ms\": %.3f,\n"
                    "  \"device_input_ms\": %.3f,\n  \"device_output_ms\": %.3f,\n  \"microphone\": %d,\n"
                    "  \"total_ms\": %.3f,\n  \"error\": \"%s\"\n}\n",
                    softwareMs, quantum, reserveMs, double(inMs), double(outMs), input,
                    softwareMs >= 0 ? softwareMs + inMs + outMs : -1.0, why.c_str());
    } else {
        if (softwareMs >= 0)
            std::printf("mic → studio → ear, through PipeWire   %.2f ms  (quantum %d frames = %.2f ms; %.2f ms "
                        "waiting in the input ring)\n",
                        softwareMs, quantum, quantumMs, reserveMs);
        else
            std::printf("mic → studio → ear, through PipeWire   not measured: %s\n", why.c_str());
        std::printf("this machine's devices, as reported   input %.2f ms, output %.2f ms%s\n", double(inMs),
                    double(outMs), input == StudioView::InputLive ? "" : "  (no live microphone)");
        if (softwareMs >= 0)
            std::printf("so, singing into the headphones       about %.1f ms\n", softwareMs + inMs + outMs);
    }
    return softwareMs >= 0 ? 0 : 1;
}

}
