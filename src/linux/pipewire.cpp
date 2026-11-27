#include "pipewire.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/parser.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "dsp.h"
#include "outputkind.h"
#include "studio.h"

namespace tf {

// --- what the sound plays through ------------------------------------------------

// Follows the PipeWire graph (on the thread loop it is made on) for what
// the stream plays into: the nodes, the devices' active routes, the links
// and the default sink, as outputkind::Graph, classified on every change
// and handed to onKind (outputkind.h says how).
class OutputWatch {
public:
    OutputWatch(pw_loop *loop, std::function<void(int)> onKind) : m_onKind(std::move(onKind)) {
        m_context = pw_context_new(loop, nullptr, 0);
        if (m_context)
            m_core = pw_context_connect(m_context, nullptr, 0);
        if (!m_core)
            return;
        m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
        static const pw_registry_events events = [] {
            pw_registry_events e{};
            e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = [](void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props) {
                static_cast<OutputWatch *>(data)->global(id, type, props);
            };
            e.global_remove = [](void *data, uint32_t id) { static_cast<OutputWatch *>(data)->remove(id); };
            return e;
        }();
        pw_registry_add_listener(m_registry, &m_registryHook, &events, this);
    }
    ~OutputWatch() {
        for (auto &[id, b] : m_bound) {
            spa_hook_remove(&b->hook);
            pw_proxy_destroy(b->proxy);
        }
        m_bound.clear();
        if (m_registry) {
            spa_hook_remove(&m_registryHook);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_registry));
        }
        if (m_core)
            pw_core_disconnect(m_core);
        if (m_context)
            pw_context_destroy(m_context);
    }
    bool connected() const { return m_core != nullptr; }
    // The stream whose sinks count (0: the default sink).
    void setStream(uint32_t node) {
        if (node != m_stream) {
            m_stream = node;
            update();
        }
    }
    int kind() const { return m_kind; }
    const outputkind::Graph &graph() const { return m_graph; }

private:
    struct Bound {
        OutputWatch *owner = nullptr;
        uint32_t id = 0;
        pw_proxy *proxy = nullptr;
        spa_hook hook{};
    };

    static outputkind::Props propsOf(const spa_dict *dict) {
        outputkind::Props p;
        if (dict) {
            const spa_dict_item *item;
            spa_dict_for_each(item, dict) if (item->key && item->value) p[item->key] = item->value;
        }
        return p;
    }

    void global(uint32_t id, const char *type, const spa_dict *props) {
        if (!type)
            return;
        if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
            const outputkind::Props p = propsOf(props);
            const std::string out = outputkind::get(p, PW_KEY_LINK_OUTPUT_NODE),
                              in = outputkind::get(p, PW_KEY_LINK_INPUT_NODE);
            if (!out.empty() && !in.empty()) {
                m_links[id] = {uint32_t(std::strtoul(out.c_str(), nullptr, 10)), uint32_t(std::strtoul(in.c_str(), nullptr, 10))};
                relink();
            }
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
            // (Bound: a node's full properties, card.profile.device and the
            // link group among them, come with its info, not its global.)
            m_graph.nodes[id] = propsOf(props);
            static const pw_node_events events = [] {
                pw_node_events e{};
                e.version = PW_VERSION_NODE_EVENTS;
                e.info = [](void *data, const pw_node_info *info) {
                    auto *b = static_cast<Bound *>(data);
                    if (info && (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)) {
                        b->owner->m_graph.nodes[b->id] = propsOf(info->props);
                        b->owner->update();
                    }
                };
                return e;
            }();
            bind(id, type, PW_VERSION_NODE, &events);
            update();
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
            m_graph.devices[id] = propsOf(props);
            static const pw_device_events events = [] {
                pw_device_events e{};
                e.version = PW_VERSION_DEVICE_EVENTS;
                e.info = [](void *data, const pw_device_info *info) {
                    auto *b = static_cast<Bound *>(data);
                    if (!info)
                        return;
                    if (info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS)
                        b->owner->m_graph.devices[b->id] = propsOf(info->props);
                    if (info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS)
                        for (uint32_t i = 0; i < info->n_params; ++i)
                            if (info->params[i].id == SPA_PARAM_Route && (info->params[i].flags & SPA_PARAM_INFO_READ)) {
                                // The routes again, from the start.
                                b->owner->m_graph.routes[b->id].clear();
                                pw_device_enum_params(reinterpret_cast<pw_device *>(b->proxy), 0, SPA_PARAM_Route, 0,
                                                      UINT32_MAX, nullptr);
                            }
                    b->owner->update();
                };
                e.param = [](void *data, int, uint32_t id, uint32_t, uint32_t, const spa_pod *param) {
                    auto *b = static_cast<Bound *>(data);
                    if (id == SPA_PARAM_Route && param) {
                        outputkind::Route r;
                        if (parseRoute(param, r)) {
                            b->owner->m_graph.routes[b->id].push_back(std::move(r));
                            b->owner->update();
                        }
                    }
                };
                return e;
            }();
            bind(id, type, PW_VERSION_DEVICE, &events);
            update();
        } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
            const outputkind::Props p = propsOf(props);
            if (outputkind::get(p, PW_KEY_METADATA_NAME) != "default")
                return;
            static const pw_metadata_events events = [] {
                pw_metadata_events e{};
                e.version = PW_VERSION_METADATA_EVENTS;
                e.property = [](void *data, uint32_t subject, const char *key, const char *, const char *value) {
                    auto *b = static_cast<Bound *>(data);
                    if (subject == PW_ID_CORE && key && std::strcmp(key, "default.audio.sink") == 0) {
                        // {"name":"alsa_output…"}
                        std::string name;
                        if (value)
                            if (const char *at = std::strstr(value, "\"name\"")) {
                                const char *open = std::strchr(at + 6, '"');
                                const char *close = open ? std::strchr(open + 1, '"') : nullptr;
                                if (open && close)
                                    name.assign(open + 1, close);
                            }
                        b->owner->m_graph.defaultSink = name;
                        b->owner->update();
                    }
                    return 0;
                };
                return e;
            }();
            bind(id, type, PW_VERSION_METADATA, &events);
        }
    }

    static bool parseRoute(const spa_pod *param, outputkind::Route &r) {
        uint32_t direction = SPA_DIRECTION_OUTPUT;
        int32_t device = -1;
        const char *name = nullptr;
        const spa_pod *info = nullptr;
        if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamRoute, nullptr, SPA_PARAM_ROUTE_direction,
                                 SPA_POD_Id(&direction), SPA_PARAM_ROUTE_device, SPA_POD_Int(&device),
                                 SPA_PARAM_ROUTE_name, SPA_POD_OPT_String(&name), SPA_PARAM_ROUTE_info,
                                 SPA_POD_OPT_Pod(&info)) < 0)
            return false;
        r.output = direction == SPA_DIRECTION_OUTPUT;
        r.device = device;
        r.name = name ? name : "";
        if (info) {
            // Struct(Int n, then n pairs of strings).
            spa_pod_parser prs;
            spa_pod_frame frame;
            spa_pod_parser_pod(&prs, info);
            int32_t n = 0;
            if (spa_pod_parser_push_struct(&prs, &frame) == 0 && spa_pod_parser_get_int(&prs, &n) == 0)
                for (int32_t i = 0; i < n; ++i) {
                    const char *k = nullptr, *v = nullptr;
                    if (spa_pod_parser_get_string(&prs, &k) < 0 || spa_pod_parser_get_string(&prs, &v) < 0)
                        break;
                    if (k && v)
                        r.info[k] = v;
                }
        }
        return true;
    }

    template <typename Events>
    void bind(uint32_t id, const char *type, uint32_t version, const Events *events) {
        auto *proxy = static_cast<pw_proxy *>(pw_registry_bind(m_registry, id, type, version, 0));
        if (!proxy)
            return;
        auto b = std::make_unique<Bound>();
        b->owner = this;
        b->id = id;
        b->proxy = proxy;
        pw_proxy_add_object_listener(proxy, &b->hook, events, b.get());
        if (auto old = m_bound.find(id); old != m_bound.end()) {
            spa_hook_remove(&old->second->hook);
            pw_proxy_destroy(old->second->proxy);
        }
        m_bound[id] = std::move(b);
    }

    void remove(uint32_t id) {
        if (auto b = m_bound.find(id); b != m_bound.end()) {
            spa_hook_remove(&b->second->hook);
            pw_proxy_destroy(b->second->proxy);
            m_bound.erase(b);
        }
        m_graph.nodes.erase(id);
        m_graph.devices.erase(id);
        m_graph.routes.erase(id);
        if (m_links.erase(id))
            relink();
        update();
    }

    void relink() {
        m_graph.links.clear();
        for (const auto &[id, l] : m_links)
            m_graph.links.push_back(l);
        update();
    }

    void update() {
        const int k = outputkind::classify(m_graph, m_stream ? m_stream : UINT32_MAX);
        if (k != m_kind || !m_told) {
            m_kind = k;
            m_told = true;
            if (m_onKind)
                m_onKind(k);
        }
    }

    std::function<void(int)> m_onKind;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    spa_hook m_registryHook{};
    std::map<uint32_t, std::unique_ptr<Bound>> m_bound;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> m_links;
    outputkind::Graph m_graph;
    uint32_t m_stream = 0;
    int m_kind = StudioView::OutputUnknown;
    bool m_told = false;
};


struct PipeWireAudio::Impl {
    PipeWireAudio *owner = nullptr;
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

    // What the sound plays through, for the Vocal page's monitor.
    std::unique_ptr<OutputWatch> watch;
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
    auto *d = static_cast<PipeWireAudio::Impl *>(data);
    PipeWireAudio *self = d->owner;
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
        self->m_engine->setTimeOrigin(monotonicSeconds());
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
    auto *d = static_cast<PipeWireAudio::Impl *>(data);
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
    auto *d = static_cast<PipeWireAudio::Impl *>(data);
    d->captureState.store(int(state), std::memory_order_relaxed);
    if (state == PW_STREAM_STATE_ERROR) {
        std::lock_guard lock(d->errorLock);
        d->inputError = error ? error : "the microphone stream failed";
    }
}

// Once the stream has a node of its own, the sinks it is linked to are
// the ones that count.
static void onStreamState(void *data, pw_stream_state, pw_stream_state state, const char *) {
    auto *d = static_cast<PipeWireAudio::Impl *>(data);
    if (d->watch && (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING))
        d->watch->setStream(pw_stream_get_node_id(d->stream));
}

static const pw_stream_events streamEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onProcess;
    e.state_changed = onStreamState;
    return e;
}();

static const pw_stream_events captureEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onCapture;
    e.state_changed = onCaptureState;
    return e;
}();

PipeWireAudio::PipeWireAudio(Studio *studio) : m_engine(studio) {}

PipeWireAudio::~PipeWireAudio() { stop(); }

std::string PipeWireAudio::inputError() const {
    if (!d)
        return {};
    std::lock_guard lock(d->errorLock);
    return d->inputError;
}

bool PipeWireAudio::start() {
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

    // Speakers or headphones: best effort, nothing fails without it.
    d->watch = std::make_unique<OutputWatch>(pw_thread_loop_get_loop(d->loop),
                                             [studio = m_engine](int kind) { studio->setOutput(kind); });

    if (pw_thread_loop_start(d->loop) < 0) {
        m_error = "Cannot start the audio thread.";
        return false;
    }
    m_running = true;
    return true;
}

void PipeWireAudio::stop() {
    if (!d)
        return;
    if (d->loop)
        pw_thread_loop_stop(d->loop);
    d->watch.reset();
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

double monoNow() { return monotonicSeconds(); }

// The studio, live, for `seconds`, on the Vocal page or not; idles the
// session as the window would.
void runStudio(Studio &studio, Session &session, PipeWireAudio &out, double seconds, const std::function<void(double)> &tick) {
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

int measurePipeWireLatency(bool json) {
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
        PipeWireAudio out(&studio);
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
        PipeWireAudio out(&studio);
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
            std::printf("so, from the mic to that output       about %.1f ms%s\n", softwareMs + inMs + outMs,
                        outMs > 15 ? "  (that output buffers a lot: monitor on headphones, or set\n"
                                     "                                       TENFOLD_SINK to the headphone device)"
                                   : "");
    }
    return softwareMs >= 0 ? 0 : 1;
}

}

namespace tf {

int describePipeWireOutput() {
    pw_init(nullptr, nullptr);
    pw_main_loop *loop = pw_main_loop_new(nullptr);
    if (!loop) {
        std::printf("cannot start PipeWire\n");
        return 1;
    }
    int code = 0;
    {
        OutputWatch watch(pw_main_loop_get_loop(loop), {});
        if (!watch.connected()) {
            std::printf("cannot reach PipeWire\n");
            code = 1;
        } else {
            // Half a second for the graph to arrive, then what it says.
            spa_source *timer = pw_loop_add_timer(
                pw_main_loop_get_loop(loop), [](void *data, uint64_t) { pw_main_loop_quit(static_cast<pw_main_loop *>(data)); },
                loop);
            timespec in{0, 500000000}, never{0, 0};
            pw_loop_update_timer(pw_main_loop_get_loop(loop), timer, &in, &never, false);
            pw_main_loop_run(loop);
            const outputkind::Graph &g = watch.graph();
            static const char *names[] = {"unknown (nothing muted)", "speakers (the Vocal page mutes its monitor)",
                                          "headphones"};
            std::printf("default sink: %s\n", g.defaultSink.empty() ? "?" : g.defaultSink.c_str());
            for (const auto &[id, p] : g.nodes)
                if (outputkind::get(p, PW_KEY_MEDIA_CLASS) == "Audio/Sink")
                    std::printf("  sink %u %s: %s\n", id, outputkind::get(p, PW_KEY_NODE_NAME).c_str(),
                                names[outputkind::sinkKind(g, id, 0)]);
            std::printf("plays through: %s\n", names[watch.kind()]);
        }
    }
    pw_main_loop_destroy(loop);
    return code;
}

}
