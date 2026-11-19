#include "audio.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <time.h>

#include "dsp.h"
#include "engine.h"

namespace tf {

struct AudioOut::Impl {
    AudioOut *owner = nullptr;
    pw_thread_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    bool flushed = false;
    bool clockSet = false;
};

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
    self->m_engine->render(dst, int(frames));
    self->m_quantum.store(int(frames), std::memory_order_relaxed);
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = frames * stride;
    pw_stream_queue_buffer(d->stream, b);
}

static const pw_stream_events streamEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onProcess;
    return e;
}();

AudioOut::AudioOut(Engine *engine) : m_engine(engine) {}

AudioOut::~AudioOut() { stop(); }

bool AudioOut::start() {
    if (d)
        return true;
    pw_init(nullptr, nullptr);
    d = new Impl;
    d->owner = this;
    d->loop = pw_thread_loop_new("tenfold-audio", nullptr);
    if (!d->loop) {
        m_error = "Cannot start PipeWire.";
        return false;
    }
    const int rate = int(m_engine->sampleRate());
    char latency[32];
    std::snprintf(latency, sizeof(latency), "128/%d", rate);
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                             PW_KEY_MEDIA_ROLE, "Music", PW_KEY_NODE_NAME, "tenfold",
                                             PW_KEY_NODE_DESCRIPTION, "tenfold", PW_KEY_APP_NAME, "tenfold",
                                             PW_KEY_NODE_LATENCY, latency, nullptr);
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
    if (pw_thread_loop_start(d->loop) < 0) {
        m_error = "Cannot start the audio thread.";
        return false;
    }
    m_rate = rate;
    m_running = true;
    return true;
}

void AudioOut::stop() {
    if (!d)
        return;
    if (d->loop)
        pw_thread_loop_stop(d->loop);
    if (d->stream)
        pw_stream_destroy(d->stream);
    if (d->loop)
        pw_thread_loop_destroy(d->loop);
    delete d;
    d = nullptr;
    m_running = false;
}

}
