#include "stems.h"

// The port follows demucs/htdemucs.py (Demucs 4.1.0) line by line; the
// comments name the Python each part stands for. Activations are kept as
// [channels][frequencies][time] floats, the time branch with one
// "frequency", so every convolution, norm and gate is one piece of code
// for both branches.

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#if defined(__linux__)
#include <fstream>
#include <sched.h>
#include <set>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace tf::stems {


namespace {

constexpr int kFft = 4096, kHop = 1024, kBins = 2048;
constexpr int kDepth = 4;
constexpr double kPi = 3.14159265358979323846;

} // namespace

// ---------------------------------------------------------------------------
// The weights.

struct Tensor {
    std::vector<int> shape;
    std::vector<float> v;
    size_t size() const { return v.size(); }
    const float *data() const { return v.data(); }
};

struct Model {
    std::unordered_map<std::string, Tensor> t;
    const Tensor &operator[](const std::string &name) const {
        const auto it = t.find(name);
        if (it == t.end())
            throw std::runtime_error("no " + name);
        return it->second;
    }
};

namespace {

float halfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1f, man = h & 0x3ffu, bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;
        } else { // subnormal: normalise it
            exp = 127 - 15 + 1;
            while (!(man & 0x400u)) {
                man <<= 1;
                --exp;
            }
            bits = sign | (exp << 23) | ((man & 0x3ffu) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000u | (man << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (man << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Every weight the network reads, with its shape: what makes a file this
// network's, checked before any song is spent on it.
std::vector<std::pair<std::string, std::vector<int>>> expectedShapes() {
    std::vector<std::pair<std::string, std::vector<int>>> e;
    auto add = [&](const std::string &n, std::vector<int> s) { e.emplace_back(n, std::move(s)); };
    auto dconv = [&](const std::string &p, int c) {
        for (int d = 0; d < 2; ++d) {
            const std::string l = p + ".dconv.layers." + std::to_string(d);
            add(l + ".0.weight", {c / 8, c, 3});
            add(l + ".0.bias", {c / 8});
            add(l + ".1.weight", {c / 8});
            add(l + ".1.bias", {c / 8});
            add(l + ".3.weight", {2 * c, c / 8, 1});
            add(l + ".3.bias", {2 * c});
            add(l + ".4.weight", {2 * c});
            add(l + ".4.bias", {2 * c});
            add(l + ".6.scale", {c});
        }
    };
    int cin = 4, cint = 2;
    for (int i = 0; i < kDepth; ++i) {
        const int c = 48 << i;
        const std::string en = "encoder." + std::to_string(i), te = "tencoder." + std::to_string(i);
        add(en + ".conv.weight", {c, cin, 8, 1});
        add(en + ".conv.bias", {c});
        add(en + ".rewrite.weight", {2 * c, c, 1, 1});
        add(en + ".rewrite.bias", {2 * c});
        dconv(en, c);
        add(te + ".conv.weight", {c, cint, 8});
        add(te + ".conv.bias", {c});
        add(te + ".rewrite.weight", {2 * c, c, 1});
        add(te + ".rewrite.bias", {2 * c});
        dconv(te, c);
        // decoder.(3-i) mirrors encoder.i.
        const int cout = i == 0 ? 16 : c / 2, coutt = i == 0 ? 8 : c / 2;
        const std::string de = "decoder." + std::to_string(kDepth - 1 - i),
                          td = "tdecoder." + std::to_string(kDepth - 1 - i);
        add(de + ".conv_tr.weight", {c, cout, 8, 1});
        add(de + ".conv_tr.bias", {cout});
        add(de + ".rewrite.weight", {2 * c, c, 3, 3});
        add(de + ".rewrite.bias", {2 * c});
        dconv(de, c);
        add(td + ".conv_tr.weight", {c, coutt, 8});
        add(td + ".conv_tr.bias", {coutt});
        add(td + ".rewrite.weight", {2 * c, c, 3});
        add(td + ".rewrite.bias", {2 * c});
        dconv(td, c);
        cin = c;
        cint = c;
    }
    add("freq_emb.embedding.weight", {512, 48});
    for (const char *s : {"", "_t"}) {
        add(std::string("channel_upsampler") + s + ".weight", {512, 384, 1});
        add(std::string("channel_upsampler") + s + ".bias", {512});
        add(std::string("channel_downsampler") + s + ".weight", {384, 512, 1});
        add(std::string("channel_downsampler") + s + ".bias", {384});
    }
    for (const char *s : {"norm_in", "norm_in_t"}) {
        add(std::string("crosstransformer.") + s + ".weight", {512});
        add(std::string("crosstransformer.") + s + ".bias", {512});
    }
    for (const char *branch : {"layers", "layers_t"})
        for (int i = 0; i < 5; ++i) {
            const std::string p = std::string("crosstransformer.") + branch + "." + std::to_string(i);
            const std::string attn = p + (i % 2 ? ".cross_attn" : ".self_attn");
            add(attn + ".in_proj_weight", {1536, 512});
            add(attn + ".in_proj_bias", {1536});
            add(attn + ".out_proj.weight", {512, 512});
            add(attn + ".out_proj.bias", {512});
            add(p + ".linear1.weight", {2048, 512});
            add(p + ".linear1.bias", {2048});
            add(p + ".linear2.weight", {512, 2048});
            add(p + ".linear2.bias", {512});
            for (const char *n : {"norm1", "norm2", "norm_out"}) {
                add(p + "." + n + ".weight", {512});
                add(p + "." + n + ".bias", {512});
            }
            if (i % 2) {
                add(p + ".norm3.weight", {512});
                add(p + ".norm3.bias", {512});
            }
            add(p + ".gamma_1.scale", {512});
            add(p + ".gamma_2.scale", {512});
        }
    return e;
}

} // namespace

// The compact form tenfold carries (htdemucs.tfq), half the size of the
// published file. The weights of each convolution and linear layer are
// whole multiples of a step of their own for each output channel, the
// step a power-of-two fraction of that channel's RMS: finer for the layers
// the stems are most sensitive to (those nearest the output, measured
// layer by layer), coarser for the transformer, which is three quarters of
// the weights and the least sensitive. The multiples, zigzagged into 16
// bits, are deflated as two streams, low bytes and high bytes. Biases and
// norms stay as the published halves.
//
//   "TFSTEMQ1", u32 length, a JSON array of {n: name, s: shape, f: "h" or
//   "q", o: offset}, with for "q" lo and hi: the two streams' lengths after
//   the steps (f32, one per output channel); then the data.
namespace {

constexpr char kCompactMagic[9] = "TFSTEMQ1";

// Output channels and the weights in each: dimension 0, or dimension 1 of
// a transposed convolution's [in][out][k] weight.
struct Rows {
    size_t rows, inner;
    bool transposed;
    size_t of(size_t i) const { return transposed ? (i / inner) % rows : i / inner; }
};
Rows rowsOf(const std::string &name, const std::vector<int> &shape, size_t count) {
    const bool tr = name.find("conv_tr") != std::string::npos;
    const size_t rows = size_t(shape[tr ? 1 : 0]);
    return {rows, tr ? count / size_t(shape[0]) / rows : count / rows, tr};
}
bool compactable(const std::vector<int> &shape, size_t count) { return shape.size() >= 2 && count >= 1024; }

uint32_t u32At(const char *p) {
    const auto *b = reinterpret_cast<const unsigned char *>(p);
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}

// The tensors of a compact file, or false if it is damaged.
bool readCompact(const QByteArray &bytes, Model &model) {
    if (bytes.size() < 12)
        return false;
    const uint32_t headerSize = u32At(bytes.constData() + 8);
    if (headerSize > uint64_t(bytes.size()) - 12)
        return false;
    const QJsonArray entries = QJsonDocument::fromJson(bytes.mid(12, qsizetype(headerSize))).array();
    const char *base = bytes.constData() + 12 + headerSize;
    const uint64_t dataSize = uint64_t(bytes.size()) - 12 - headerSize;
    std::unordered_map<std::string, QJsonObject> byName;
    for (const QJsonValue &e : entries)
        byName[e.toObject().value(QStringLiteral("n")).toString().toStdString()] = e.toObject();
    for (const auto &[name, shape] : expectedShapes()) {
        const auto it = byName.find(name);
        if (it == byName.end())
            return false;
        const QJsonObject &entry = it->second;
        const QJsonArray dims = entry.value(QStringLiteral("s")).toArray();
        bool same = dims.size() == qsizetype(shape.size());
        for (qsizetype i = 0; same && i < dims.size(); ++i)
            same = dims[i].toInt() == shape[size_t(i)];
        if (!same)
            return false;
        const size_t count = std::accumulate(shape.begin(), shape.end(), size_t(1), std::multiplies<size_t>());
        const uint64_t at = uint64_t(entry.value(QStringLiteral("o")).toInteger(-1));
        Tensor t;
        t.shape = shape;
        t.v.resize(count);
        if (entry.value(QStringLiteral("f")).toString() == QLatin1String("h")) {
            if (at > dataSize || dataSize - at < 2 * count)
                return false;
            const auto *p = reinterpret_cast<const unsigned char *>(base + at);
            for (size_t i = 0; i < count; ++i)
                t.v[i] = halfToFloat(uint16_t(p[2 * i] | p[2 * i + 1] << 8));
        } else {
            if (!compactable(shape, count))
                return false;
            const Rows r = rowsOf(name, shape, count);
            const uint64_t lo = uint64_t(entry.value(QStringLiteral("lo")).toInteger(-1)),
                           hi = uint64_t(entry.value(QStringLiteral("hi")).toInteger(-1));
            if (at > dataSize || lo > dataSize || hi > dataSize || dataSize - at < 4 * r.rows + lo + hi)
                return false;
            std::vector<float> step(r.rows);
            std::memcpy(step.data(), base + at, 4 * r.rows);
            const QByteArray low = qUncompress(reinterpret_cast<const uchar *>(base + at + 4 * r.rows), qsizetype(lo));
            const QByteArray high =
                qUncompress(reinterpret_cast<const uchar *>(base + at + 4 * r.rows + lo), qsizetype(hi));
            if (size_t(low.size()) != count || size_t(high.size()) != count)
                return false;
            for (size_t i = 0; i < count; ++i) {
                const uint32_t z = uint8_t(low[qsizetype(i)]) | uint32_t(uint8_t(high[qsizetype(i)])) << 8;
                const int32_t q = int32_t(z >> 1) ^ -int32_t(z & 1);
                t.v[i] = float(q) * step[r.of(i)];
            }
        }
        model.t.emplace(name, std::move(t));
    }
    return true;
}

// The tensors of htdemucs's safetensors file, or false if it isn't that.
bool readSafetensors(const QByteArray &bytes, Model &model) {
    if (bytes.size() < 8)
        return false;
    uint64_t headerSize = 0;
    for (int i = 7; i >= 0; --i)
        headerSize = headerSize << 8 | uint8_t(bytes[i]);
    if (headerSize > uint64_t(bytes.size()) - 8)
        return false;
    const QJsonObject header = QJsonDocument::fromJson(bytes.mid(8, qsizetype(headerSize))).object();
    const char *base = bytes.constData() + 8 + headerSize;
    const uint64_t dataSize = uint64_t(bytes.size()) - 8 - headerSize;
    for (const auto &[name, shape] : expectedShapes()) {
        const QJsonObject entry = header.value(QString::fromStdString(name)).toObject();
        const QJsonArray dims = entry.value(QStringLiteral("shape")).toArray();
        const QJsonArray offsets = entry.value(QStringLiteral("data_offsets")).toArray();
        const QString dtype = entry.value(QStringLiteral("dtype")).toString();
        bool same = dims.size() == qsizetype(shape.size()) && offsets.size() == 2;
        for (qsizetype i = 0; same && i < dims.size(); ++i)
            same = dims[i].toInt() == shape[size_t(i)];
        const size_t count = std::accumulate(shape.begin(), shape.end(), size_t(1), std::multiplies<size_t>());
        const int width = dtype == QLatin1String("F16") ? 2 : dtype == QLatin1String("F32") ? 4 : 0;
        if (!same || !width)
            return false;
        const uint64_t from = uint64_t(offsets[0].toInteger()), to = uint64_t(offsets[1].toInteger());
        if (to < from || to > dataSize || to - from != count * uint64_t(width))
            return false;
        Tensor t;
        t.shape = shape;
        t.v.resize(count);
        const auto *p = reinterpret_cast<const unsigned char *>(base + from);
        for (size_t i = 0; i < count; ++i) {
            if (width == 2) {
                t.v[i] = halfToFloat(uint16_t(p[2 * i] | p[2 * i + 1] << 8));
            } else {
                uint32_t b = uint32_t(p[4 * i]) | uint32_t(p[4 * i + 1]) << 8 | uint32_t(p[4 * i + 2]) << 16 |
                             uint32_t(p[4 * i + 3]) << 24;
                std::memcpy(&t.v[i], &b, 4);
            }
        }
        model.t.emplace(name, std::move(t));
    }
    return true;
}

} // namespace

std::shared_ptr<const Model> load(const std::string &path, std::string *error) {
    auto fail = [&](const QString &why) -> std::shared_ptr<const Model> {
        if (error)
            *error = why.toStdString();
        return nullptr;
    };
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("The stem model isn't there (%1)").arg(QString::fromStdString(path)));
    const QByteArray bytes = file.readAll();
    auto model = std::make_shared<Model>();
    const bool compact = bytes.startsWith(kCompactMagic);
    if (!(compact ? readCompact(bytes, *model) : readSafetensors(bytes, *model)))
        return fail(QStringLiteral("The stem model's file is damaged (%1)").arg(QString::fromStdString(path)));
    return model;
}

void forEachWeight(const Model &a, const Model &b,
                   const std::function<void(const std::string &, const std::vector<int> &, const float *, const float *,
                                            size_t)> &fn) {
    for (const auto &[name, shape] : expectedShapes()) {
        const Tensor &x = a[name], &y = b[name];
        fn(name, shape, x.data(), y.data(), x.size());
    }
}

bool compact(const std::string &from, const std::string &to, const std::function<double(const std::string &)> &bits,
             std::string *error) {
    auto fail = [&](const std::string &why) {
        if (error)
            *error = why;
        return false;
    };
    const auto model = load(from, error);
    if (!model)
        return false;
    QJsonArray entries;
    QByteArray data;
    for (const auto &[name, shape] : expectedShapes()) {
        const Tensor &t = model->t.at(name);
        QJsonObject e{{QStringLiteral("n"), QString::fromStdString(name)}, {QStringLiteral("o"), qint64(data.size())}};
        QJsonArray dims;
        for (int d : shape)
            dims.append(d);
        e.insert(QStringLiteral("s"), dims);
        const double b = compactable(shape, t.size()) ? bits(name) : 0;
        if (b <= 0) {
            e.insert(QStringLiteral("f"), QStringLiteral("h"));
            for (float x : t.v) {
                // Back to the half it was read from (exactly: every float here came from one).
                uint32_t u;
                std::memcpy(&u, &x, 4);
                const uint32_t sign = (u >> 16) & 0x8000u, mag = u & 0x7fffffffu;
                uint16_t h;
                if (mag >= 0x38800000u)
                    h = uint16_t(sign | (((mag >> 23) - 112) << 10) | ((mag >> 13) & 0x3ffu));
                else if (mag == 0)
                    h = uint16_t(sign);
                else // a subnormal half
                    h = uint16_t(sign | uint32_t(std::ldexp(double(std::bit_cast<float>(mag)), 24)));
                data.append(char(h & 255)).append(char(h >> 8));
            }
        } else {
            e.insert(QStringLiteral("f"), QStringLiteral("q"));
            const Rows r = rowsOf(name, shape, t.size());
            std::vector<double> sq(r.rows, 0);
            for (size_t i = 0; i < t.size(); ++i)
                sq[r.of(i)] += double(t.v[i]) * t.v[i];
            std::vector<float> step(r.rows);
            for (size_t k = 0; k < r.rows; ++k)
                step[k] = float(std::sqrt(sq[k] / double(t.size() / r.rows)) * std::exp2(-b));
            QByteArray low(qsizetype(t.size()), 0), high(qsizetype(t.size()), 0);
            for (size_t i = 0; i < t.size(); ++i) {
                const float s = step[r.of(i)];
                const long q = s > 0 ? std::lround(t.v[i] / s) : 0;
                const uint32_t z = uint32_t(q << 1) ^ uint32_t(q >> 31);
                if (q < -32768 || q > 32767)
                    return fail(name + " needs more than 16 bits at " + std::to_string(b));
                low[qsizetype(i)] = char(z & 255);
                high[qsizetype(i)] = char(z >> 8);
            }
            const QByteArray lo = qCompress(low, 9), hi = qCompress(high, 9);
            e.insert(QStringLiteral("lo"), qint64(lo.size()));
            e.insert(QStringLiteral("hi"), qint64(hi.size()));
            data.append(reinterpret_cast<const char *>(step.data()), qsizetype(4 * step.size()));
            data.append(lo).append(hi);
        }
        entries.append(e);
    }
    const QByteArray header = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    QFile out(QString::fromStdString(to));
    if (!out.open(QIODevice::WriteOnly))
        return fail("can't write " + to);
    const uint32_t n = uint32_t(header.size());
    const char length[4] = {char(n & 255), char(n >> 8 & 255), char(n >> 16 & 255), char(n >> 24)};
    out.write(kCompactMagic, 8);
    out.write(length, 4);
    out.write(header);
    out.write(data);
    return out.error() == QFile::NoError || fail("can't write " + to);
}

// ---------------------------------------------------------------------------
// Running things side by side.

namespace {

// fn(i) for i in [0, n), on up to `threads` threads (this one among them).
void parallelFor(int n, int threads, const std::function<void(int)> &fn) {
    threads = std::max(1, std::min(threads, n));
    if (threads == 1) {
        for (int i = 0; i < n; ++i)
            fn(i);
        return;
    }
    std::atomic<int> next{0};
    auto work = [&] {
        for (int i; (i = next.fetch_add(1)) < n;)
            fn(i);
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t)
        pool.emplace_back(work);
    work();
    for (auto &t : pool)
        t.join();
}

// The CPUs worth a long computation, and the number of them. Chips that mix
// fast and slow cores (Apple silicon, recent Intel) lose time when a job
// waits for its slowest member, and two threads on one hyper-threaded core
// get in each other's way: on an M2 Pro all twelve cores did the work in
// 70 s that the eight fast ones did in 49.
struct Cores {
    int count = 0;
#if defined(__linux__)
    cpu_set_t set;
    bool pin = false;
#endif
};

Cores fastCores() {
    Cores c;
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof allowed, &allowed) == 0) {
        auto read = [](int cpu, const char *name) {
            std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/" + name);
            std::string s;
            std::getline(f, s);
            return s;
        };
        std::vector<int> cpus;
        for (int i = 0; i < CPU_SETSIZE; ++i)
            if (CPU_ISSET(i, &allowed))
                cpus.push_back(i);
        // ARM says how fast each core is; Intel's hybrids list their fast cores.
        int best = 0;
        std::vector<int> capacity;
        for (int cpu : cpus) {
            const std::string s = read(cpu, "cpu_capacity");
            capacity.push_back(s.empty() ? 0 : std::atoi(s.c_str()));
            best = std::max(best, capacity.back());
        }
        std::vector<int> chosen;
        for (size_t i = 0; i < cpus.size(); ++i)
            if (capacity[i] == best)
                chosen.push_back(cpus[i]);
        std::ifstream pcores("/sys/devices/cpu_core/cpus");
        std::string list;
        if (std::getline(pcores, list) && !list.empty()) {
            std::set<int> perf;
            size_t at = 0;
            while (at < list.size()) {
                const size_t comma = std::min(list.find(',', at), list.size());
                const std::string part = list.substr(at, comma - at);
                const size_t dash = part.find('-');
                const int a = std::atoi(part.c_str()), b = dash == std::string::npos ? a : std::atoi(part.c_str() + dash + 1);
                for (int i = a; i <= b; ++i)
                    perf.insert(i);
                at = comma + 1;
            }
            std::vector<int> kept;
            for (int cpu : chosen)
                if (perf.count(cpu))
                    kept.push_back(cpu);
            if (!kept.empty())
                chosen = kept;
        }
        // One CPU of every group of hyper-threads sharing a core.
        std::set<std::string> seen;
        CPU_ZERO(&c.set);
        for (int cpu : chosen) {
            std::string siblings = read(cpu, "topology/thread_siblings_list");
            if (siblings.empty())
                siblings = std::to_string(cpu);
            if (seen.insert(siblings).second) {
                CPU_SET(cpu, &c.set);
                ++c.count;
            }
        }
        c.pin = c.count > 0;
    }
#elif defined(__APPLE__)
    for (const char *name : {"hw.perflevel0.physicalcpu", "hw.physicalcpu"}) {
        int n = 0;
        size_t size = sizeof n;
        if (sysctlbyname(name, &n, &size, nullptr, 0) == 0 && n > 0) {
            c.count = n;
            break;
        }
    }
#endif
    if (c.count <= 0)
        c.count = int(std::max(1u, std::thread::hardware_concurrency()));
    return c;
}

void keepOn(const Cores &c) {
#if defined(__linux__)
    if (c.pin)
        sched_setaffinity(0, sizeof c.set, &c.set);
#else
    (void)c;
#endif
}

double memoryGigabytes() {
#if defined(__linux__) || defined(__APPLE__)
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && size > 0)
        return double(pages) * double(size) / double(1 << 30);
#endif
    return 8.0;
}

const Cores &theFastCores() {
    static const Cores c = fastCores();
    return c;
}

} // namespace

int fastCoreCount() { return theFastCores().count; }
void keepToFastCores() { keepOn(theFastCores()); }
double physicalMemoryGigabytes() { return memoryGigabytes(); }

// ---------------------------------------------------------------------------
// The matrix product. Nearly all of the network's time is spent here: in
// its convolutions (as products of the weights with the input's columns)
// and its transformer. Blocked as BLIS does it: a panel of B packed to sit
// in the L2 cache, a block of A in L1, and a kernel that keeps an MR×NR
// tile of C in registers.

namespace {

constexpr int KC = 256, MC = 120, NC = 3072;

// TF_STEMS_GENERIC_KERNEL builds the vector-type kernel the x86 builds use, so that its
// arithmetic can be tested on ARM too.
#if defined(__aarch64__) && !defined(TF_STEMS_GENERIC_KERNEL)

constexpr int MR = 8, NR = 12;

// The 24 accumulators are separate variables, never an array: an array
// whose address is taken gets kept on the stack, and stored every step.
void kernel(int kc, const float *__restrict a, const float *__restrict b, float *__restrict c, long ldc, bool add) {
#define TF_ZERO(i) float32x4_t c##i##0 = vdupq_n_f32(0), c##i##1 = c##i##0, c##i##2 = c##i##0;
    TF_ZERO(0) TF_ZERO(1) TF_ZERO(2) TF_ZERO(3) TF_ZERO(4) TF_ZERO(5) TF_ZERO(6) TF_ZERO(7)
#undef TF_ZERO
    for (int k = 0; k < kc; ++k) {
        const float32x4_t b0 = vld1q_f32(b), b1 = vld1q_f32(b + 4), b2 = vld1q_f32(b + 8);
        const float32x4_t a0 = vld1q_f32(a), a1 = vld1q_f32(a + 4);
#define TF_ROW(i, av, lane)                                                                                            \
    c##i##0 = vfmaq_laneq_f32(c##i##0, b0, av, lane);                                                                  \
    c##i##1 = vfmaq_laneq_f32(c##i##1, b1, av, lane);                                                                  \
    c##i##2 = vfmaq_laneq_f32(c##i##2, b2, av, lane);
        TF_ROW(0, a0, 0) TF_ROW(1, a0, 1) TF_ROW(2, a0, 2) TF_ROW(3, a0, 3)
        TF_ROW(4, a1, 0) TF_ROW(5, a1, 1) TF_ROW(6, a1, 2) TF_ROW(7, a1, 3)
#undef TF_ROW
        a += MR;
        b += NR;
    }
#define TF_STORE(i)                                                                                                    \
    {                                                                                                                  \
        float *p = c + i * ldc;                                                                                        \
        if (add) {                                                                                                     \
            c##i##0 = vaddq_f32(c##i##0, vld1q_f32(p));                                                                \
            c##i##1 = vaddq_f32(c##i##1, vld1q_f32(p + 4));                                                            \
            c##i##2 = vaddq_f32(c##i##2, vld1q_f32(p + 8));                                                            \
        }                                                                                                              \
        vst1q_f32(p, c##i##0);                                                                                         \
        vst1q_f32(p + 4, c##i##1);                                                                                     \
        vst1q_f32(p + 8, c##i##2);                                                                                     \
    }
    TF_STORE(0) TF_STORE(1) TF_STORE(2) TF_STORE(3) TF_STORE(4) TF_STORE(5) TF_STORE(6) TF_STORE(7)
#undef TF_STORE
}

using Kernel = void (*)(int, const float *, const float *, float *, long, bool);
struct Shape {
    int mr, nr;
    Kernel k;
};
Shape chooseKernel() { return {MR, NR, kernel}; }

#else

// Elsewhere the compiler's vector types: AVX2 with fused multiply-adds when
// the processor has them (chosen at run time, since the packages are built
// for any x86-64), SSE otherwise.
template <int MRows, int NV, int L>
__attribute__((always_inline)) inline void kernelBody(int kc, const float *__restrict a, const float *__restrict b,
                                                      float *__restrict c, long ldc, bool add) {
    typedef float V __attribute__((vector_size(L * 4)));
    V acc[MRows][NV];
    for (int i = 0; i < MRows; ++i)
        for (int j = 0; j < NV; ++j)
            acc[i][j] = V{};
    for (int k = 0; k < kc; ++k) {
        V bv[NV];
        for (int j = 0; j < NV; ++j)
            std::memcpy(&bv[j], b + j * L, sizeof(V));
        for (int i = 0; i < MRows; ++i) {
            const V av = V{} + a[i];
            for (int j = 0; j < NV; ++j)
                acc[i][j] += av * bv[j];
        }
        a += MRows;
        b += NV * L;
    }
    for (int i = 0; i < MRows; ++i)
        for (int j = 0; j < NV; ++j) {
            float *p = c + i * ldc + j * L;
            V v = acc[i][j];
            if (add) {
                V o;
                std::memcpy(&o, p, sizeof(V));
                v += o;
            }
            std::memcpy(p, &v, sizeof(V));
        }
}

using Kernel = void (*)(int, const float *, const float *, float *, long, bool);
struct Shape {
    int mr, nr;
    Kernel k;
};

#if defined(__x86_64__)
__attribute__((target("avx2,fma"))) void kernelAvx2(int kc, const float *a, const float *b, float *c, long ldc,
                                                    bool add) {
    kernelBody<6, 2, 8>(kc, a, b, c, ldc, add);
}
#endif
void kernelVec(int kc, const float *a, const float *b, float *c, long ldc, bool add) {
    kernelBody<6, 2, 4>(kc, a, b, c, ldc, add);
}

Shape chooseKernel() {
#if defined(__x86_64__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"))
        return {6, 16, kernelAvx2};
#endif
    return {6, 8, kernelVec};
}

#endif

const Shape &shape() {
    static const Shape s = chooseKernel();
    return s;
}

// A's rows [i0, i0+mc) and columns [p0, p0+kc) as panels of mr rows,
// each column of a panel contiguous; rows past M are zeros.
void packA(const float *A, long lda, bool trans, int M, int i0, int mc, int p0, int kc, int mr, float *out) {
    for (int ir = 0; ir < mc; ir += mr) {
        const int rows = std::min(mr, M - (i0 + ir));
        float *dst = out + long(ir) * kc;
        if (trans) {
            for (int k = 0; k < kc; ++k) {
                const float *src = A + long(p0 + k) * lda + i0 + ir;
                int i = 0;
                for (; i < rows; ++i)
                    dst[k * mr + i] = src[i];
                for (; i < mr; ++i)
                    dst[k * mr + i] = 0;
            }
        } else {
            for (int i = 0; i < mr; ++i) {
                if (i < rows) {
                    const float *src = A + long(i0 + ir + i) * lda + p0;
                    for (int k = 0; k < kc; ++k)
                        dst[k * mr + i] = src[k];
                } else {
                    for (int k = 0; k < kc; ++k)
                        dst[k * mr + i] = 0;
                }
            }
        }
    }
}

// B's rows [p0, p0+kc) and columns [j0, j0+nc) as panels of nr columns.
void packB(const float *B, long ldb, bool trans, int N, int j0, int nc, int p0, int kc, int nr, float *out) {
    for (int jr = 0; jr < nc; jr += nr) {
        const int cols = std::min(nr, N - (j0 + jr));
        float *dst = out + long(jr) * kc;
        if (!trans) {
            for (int k = 0; k < kc; ++k) {
                const float *src = B + long(p0 + k) * ldb + j0 + jr;
                int j = 0;
                for (; j < cols; ++j)
                    dst[k * nr + j] = src[j];
                for (; j < nr; ++j)
                    dst[k * nr + j] = 0;
            }
        } else {
            for (int j = 0; j < nr; ++j) {
                if (j < cols) {
                    const float *src = B + long(j0 + jr + j) * ldb + p0;
                    for (int k = 0; k < kc; ++k)
                        dst[k * nr + j] = src[k];
                } else {
                    for (int k = 0; k < kc; ++k)
                        dst[k * nr + j] = 0;
                }
            }
        }
    }
}

void gemmOne(int M, int N, int K, const float *A, long lda, bool transA, const float *B, long ldb, bool transB,
             float *C, long ldc, bool accumulate) {
    const Shape &s = shape();
    thread_local std::vector<float> pa, pb;
    pa.resize(size_t(MC + s.mr) * KC);
    pb.resize(size_t(NC + s.nr) * KC);
    float tile[16 * 16];
    for (int jc = 0; jc < N; jc += NC) {
        const int nc = std::min(NC, N - jc);
        for (int pc = 0; pc < K; pc += KC) {
            const int kc = std::min(KC, K - pc);
            packB(B, ldb, transB, N, jc, nc, pc, kc, s.nr, pb.data());
            const bool add = accumulate || pc > 0;
            for (int ic = 0; ic < M; ic += MC) {
                const int mc = std::min(MC, M - ic);
                packA(A, lda, transA, M, ic, mc, pc, kc, s.mr, pa.data());
                for (int jr = 0; jr < nc; jr += s.nr) {
                    const int nr = std::min(s.nr, nc - jr);
                    for (int ir = 0; ir < mc; ir += s.mr) {
                        const int mr = std::min(s.mr, mc - ir);
                        float *c = C + long(ic + ir) * ldc + jc + jr;
                        const float *a = pa.data() + long(ir) * kc, *b = pb.data() + long(jr) * kc;
                        if (mr == s.mr && nr == s.nr) {
                            s.k(kc, a, b, c, ldc, add);
                        } else {
                            s.k(kc, a, b, tile, s.nr, false);
                            for (int i = 0; i < mr; ++i)
                                for (int j = 0; j < nr; ++j)
                                    c[i * ldc + j] = (add ? c[i * ldc + j] : 0.f) + tile[i * s.nr + j];
                        }
                    }
                }
            }
        }
    }
}

} // namespace

void gemm(int M, int N, int K, const float *A, long lda, bool transA, const float *B, long ldb, bool transB, float *C,
          long ldc, bool accumulate, int threads) {
    if (M <= 0 || N <= 0)
        return;
    if (K <= 0) {
        if (!accumulate)
            for (int i = 0; i < M; ++i)
                std::fill(C + long(i) * ldc, C + long(i) * ldc + N, 0.f);
        return;
    }
    const int nr = shape().nr;
    // Split the columns between the threads, each a whole number of panels.
    const int parts = std::max(1, std::min(threads, N / (nr * 8)));
    if (parts == 1) {
        gemmOne(M, N, K, A, lda, transA, B, ldb, transB, C, ldc, accumulate);
        return;
    }
    const int per = ((N + parts - 1) / parts + nr - 1) / nr * nr;
    parallelFor(parts, parts, [&](int p) {
        const int n0 = p * per, n1 = std::min(N, n0 + per);
        if (n0 < n1)
            gemmOne(M, n1 - n0, K, A, lda, transA, transB ? B + long(n0) * ldb : B + n0, ldb, transB, C + n0, ldc,
                    accumulate);
    });
}

// ---------------------------------------------------------------------------
// The pieces of the network.

namespace {

// exp(x) to within a few units in the last place, in a form the compiler
// can vectorise (Cephes's expf).
inline float fastExp(float x) {
    x = std::clamp(x, -87.3f, 88.3f);
    const float n = std::nearbyint(x * 1.44269504088896341f);
    float r = x - n * 0.693359375f;
    r -= n * -2.12194440e-4f;
    float p = 1.9875691500e-4f;
    p = p * r + 1.3981999507e-3f;
    p = p * r + 8.3334519073e-3f;
    p = p * r + 4.1665795894e-2f;
    p = p * r + 1.6666665459e-1f;
    p = p * r + 5.0000001201e-1f;
    p = p * r * r + r + 1.0f;
    const int32_t bits = (int32_t(n) + 127) << 23;
    float scale;
    std::memcpy(&scale, &bits, 4);
    return p * scale;
}

// erf by Abramowitz and Stegun 7.1.26 (within 1.5e-7): GELU as PyTorch
// computes it, with erf, not its tanh approximation.
inline float gelu(float x) {
    const float z = std::fabs(x) * 0.70710678118654752f;
    const float t = 1.0f / (1.0f + 0.3275911f * z);
    const float poly = ((((1.061405429f * t - 1.453152027f) * t + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t;
    const float e = 1.0f - poly * fastExp(-z * z);
    return 0.5f * x * (1.0f + std::copysign(e, x));
}

inline float sigmoid(float x) { return 1.0f / (1.0f + fastExp(-x)); }

// Floats from the worker's own pool, so that the same few hundred
// megabytes are reused from layer to layer and segment to segment instead
// of being asked of the system (and zeroed by it) each time.
class Buffer {
public:
    Buffer() = default;
    explicit Buffer(size_t n) : n_(n) {
        auto &pool = spare();
        size_t best = pool.size();
        for (size_t i = 0; i < pool.size(); ++i)
            if (pool[i].second >= n && (best == pool.size() || pool[i].second < pool[best].second))
                best = i;
        if (best < pool.size()) {
            p_ = std::move(pool[best].first);
            cap_ = pool[best].second;
            pool.erase(pool.begin() + long(best));
        } else {
            p_.reset(new float[std::max<size_t>(n, 1)]);
            cap_ = std::max<size_t>(n, 1);
        }
    }
    Buffer(Buffer &&o) noexcept : p_(std::move(o.p_)), n_(o.n_), cap_(o.cap_) { o.n_ = o.cap_ = 0; }
    Buffer &operator=(Buffer &&o) noexcept {
        if (this != &o) {
            give();
            p_ = std::move(o.p_);
            n_ = o.n_;
            cap_ = o.cap_;
            o.n_ = o.cap_ = 0;
        }
        return *this;
    }
    ~Buffer() { give(); }
    float *data() { return p_.get(); }
    const float *data() const { return p_.get(); }
    size_t size() const { return n_; }
    static void drain() { spare().clear(); }

private:
    void give() {
        if (p_)
            spare().emplace_back(std::move(p_), cap_);
    }
    static std::vector<std::pair<std::unique_ptr<float[]>, size_t>> &spare() {
        thread_local std::vector<std::pair<std::unique_ptr<float[]>, size_t>> pool;
        return pool;
    }
    std::unique_ptr<float[]> p_;
    size_t n_ = 0, cap_ = 0;
};

// [C][F][T]: channels, frequencies (1 for the time branch) and time.
struct Act {
    int C = 0, F = 1, T = 0;
    Buffer b;
    Act() = default;
    Act(int c, int f, int t) : C(c), F(f), T(t), b(size_t(c) * f * t) {}
    long n() const { return long(F) * T; }
    float *p() { return b.data(); }
    const float *p() const { return b.data(); }
    float *row(int c) { return b.data() + long(c) * n(); }
    const float *row(int c) const { return b.data() + long(c) * n(); }
};

class Net {
public:
    Net(const Model &m, int threads, const std::atomic<bool> *cancel, std::atomic<float> *progress, const Probe &probe)
        : m(m), threads(threads), cancel(cancel), progressOut(progress), probe(probe) {}

    // HTDemucs.forward for one segment of kSegment samples, normalised.
    void forward(const float *in, float *out);

private:
    const Tensor &w(const std::string &name) const { return m[name]; }
    void check() const {
        if (cancel && cancel->load(std::memory_order_relaxed))
            throw Cancelled();
    }
    void step(float done) {
        check();
        if (progressOut)
            progressOut->store(done, std::memory_order_relaxed);
    }
    void show(const char *name, const Act &a) const {
        if (probe)
            probe(name, a.p(), a.F == 1 && a.C > 0 ? std::vector<int>{a.C, a.T} : std::vector<int>{a.C, a.F, a.T});
    }

public:
    struct Cancelled {};

private:
    // A convolution over [C][F][T]: kernel kf×kt, stride sf×st, time
    // dilation dt, zero padding pf×pt, and padRight more zeros after the
    // end of time (the time encoder's own padding to a multiple of its
    // stride).
    Act conv(const Act &in, const std::string &name, int kf, int kt, int sf, int st, int pf, int pt, int dt = 1,
             int padRight = 0) {
        const Tensor &W = w(name + ".weight"), &B = w(name + ".bias");
        const int cout = W.shape[0], K = in.C * kf * kt;
        const int Fo = (in.F + 2 * pf - kf) / sf + 1;
        const int To = (in.T + padRight + 2 * pt - dt * (kt - 1) - 1) / st + 1;
        Act out(cout, Fo, To);
        const long N = out.n();
        for (int c = 0; c < cout; ++c)
            std::fill(out.row(c), out.row(c) + N, B.v[size_t(c)]);
        if (kf == 1 && kt == 1 && sf == 1 && st == 1 && pf == 0 && pt == 0) {
            gemm(cout, int(N), K, W.data(), K, false, in.p(), N, false, out.p(), N, true, threads);
            return out;
        }
        // The input's columns for a block of outputs at a time (im2col),
        // so that the copy stays a few megabytes.
        const long nb = std::clamp<long>((long(1) << 21) / K, 256, N);
        Buffer col(size_t(K) * size_t(nb));
        for (long n0 = 0; n0 < N; n0 += nb) {
            const long width = std::min(nb, N - n0);
            for (int ci = 0; ci < in.C; ++ci)
                for (int a = 0; a < kf; ++a)
                    for (int b = 0; b < kt; ++b) {
                        float *dst = col.data() + ((long(ci) * kf + a) * kt + b) * width;
                        const int off = b * dt - pt;
                        // Outputs to whose input this tap reaches: ti = to·st + off in [0, T).
                        const int lo = off >= 0 ? 0 : (-off + st - 1) / st;
                        const int hi = std::min(To, in.T - off <= 0 ? 0 : (in.T - off + st - 1) / st);
                        for (long n = n0; n < n0 + width;) {
                            const int fo = int(n / To), t0 = int(n % To);
                            const int t1 = int(std::min<long>(To, t0 + (n0 + width - n)));
                            const int fi = fo * sf + a - pf;
                            float *d = dst + (n - n0);
                            if (fi < 0 || fi >= in.F) {
                                std::fill(d, d + (t1 - t0), 0.f);
                            } else {
                                const float *src = in.p() + (long(ci) * in.F + fi) * in.T + off;
                                const int a0 = std::clamp(lo, t0, t1), a1 = std::clamp(hi, a0, t1);
                                std::fill(d, d + (a0 - t0), 0.f);
                                if (st == 1)
                                    std::memcpy(d + (a0 - t0), src + a0, sizeof(float) * size_t(a1 - a0));
                                else
                                    for (int t = a0; t < a1; ++t)
                                        d[t - t0] = src[long(t) * st];
                                std::fill(d + (a1 - t0), d + (t1 - t0), 0.f);
                            }
                            n += t1 - t0;
                        }
                    }
            gemm(cout, int(width), K, W.data(), K, false, col.data(), width, false, out.p() + n0, N, true, threads);
        }
        return out;
    }

    // ConvTranspose with kernel 8 and stride 4 along frequency (alongF) or
    // time, cropped by 2 at the start as HDecLayer does, to 4F
    // frequencies or `length` samples.
    Act convTr(const Act &in, const std::string &name, bool alongF, int length) {
        const Tensor &W = w(name + ".weight"), &B = w(name + ".bias");
        const int cout = W.shape[1], M = cout * 8;
        Act out = alongF ? Act(cout, 4 * in.F, in.T) : Act(cout, 1, length);
        const long No = out.n(), N = in.n();
        for (int c = 0; c < cout; ++c)
            std::fill(out.row(c), out.row(c) + No, B.v[size_t(c)]);
        const long nb = std::clamp<long>((long(1) << 21) / M, 256, N);
        Buffer y(size_t(M) * size_t(nb));
        for (long n0 = 0; n0 < N; n0 += nb) {
            const long width = std::min(nb, N - n0);
            // weight [cin][cout][8]: as A[(co·8 + k)][ci], transposed.
            gemm(M, int(width), in.C, W.data(), M, true, in.p() + n0, N, false, y.data(), width, false, threads);
            for (int co = 0; co < cout; ++co)
                for (int k = 0; k < 8; ++k) {
                    const float *src = y.data() + (long(co) * 8 + k) * width;
                    float *dst = out.row(co);
                    if (alongF) {
                        for (long n = n0; n < n0 + width;) {
                            const int fi = int(n / in.T), t0 = int(n % in.T);
                            const int t1 = int(std::min<long>(in.T, t0 + (n0 + width - n)));
                            const int fo = 4 * fi + k - 2;
                            if (fo >= 0 && fo < out.F) {
                                float *d = dst + long(fo) * in.T;
                                const float *s = src + (n - n0) - t0;
                                for (int t = t0; t < t1; ++t)
                                    d[t] += s[t];
                            }
                            n += t1 - t0;
                        }
                    } else {
                        for (long n = n0; n < n0 + width; ++n) {
                            const long to = 4 * n + k - 2;
                            if (to >= 0 && to < length)
                                dst[to] += src[n - n0];
                        }
                    }
                }
        }
        return out;
    }

    // GroupNorm with one group, separately for each frequency (that is how
    // DConv sees the frequency branch: each frequency its own sequence),
    // then GELU if asked.
    void groupNormRows(Act &a, const std::string &name, bool withGelu) {
        const Tensor &g = w(name + ".weight"), &b = w(name + ".bias");
        for (int f = 0; f < a.F; ++f) {
            double sum = 0, sq = 0;
            for (int c = 0; c < a.C; ++c) {
                const float *x = a.row(c) + long(f) * a.T;
                float s = 0, q = 0;
                for (int t = 0; t < a.T; ++t) {
                    s += x[t];
                    q += x[t] * x[t];
                }
                sum += s;
                sq += q;
            }
            const double count = double(a.C) * a.T, mean = sum / count;
            const float rstd = float(1.0 / std::sqrt(std::max(0.0, sq / count - mean * mean) + 1e-5));
            for (int c = 0; c < a.C; ++c) {
                float *x = a.row(c) + long(f) * a.T;
                const float scale = rstd * g.v[size_t(c)], shift = b.v[size_t(c)] - float(mean) * scale;
                if (withGelu)
                    for (int t = 0; t < a.T; ++t)
                        x[t] = gelu(x[t] * scale + shift);
                else
                    for (int t = 0; t < a.T; ++t)
                        x[t] = x[t] * scale + shift;
            }
        }
    }

    static void geluAll(Act &a) {
        float *x = a.p();
        const long n = long(a.C) * a.n();
        for (long i = 0; i < n; ++i)
            x[i] = gelu(x[i]);
    }

    // GLU over channels: the first half gated by the second.
    static Act glu(const Act &a) {
        Act out(a.C / 2, a.F, a.T);
        const long n = a.n();
        for (int c = 0; c < out.C; ++c) {
            const float *x = a.row(c), *g = a.row(c + out.C);
            float *y = out.row(c);
            for (long i = 0; i < n; ++i)
                y[i] = x[i] * sigmoid(g[i]);
        }
        return out;
    }

    // DConv: two residual branches of dilated convolutions along time.
    void dconv(Act &x, const std::string &name) {
        for (int d = 0; d < 2; ++d) {
            const std::string l = name + ".dconv.layers." + std::to_string(d);
            const int dil = 1 << d;
            Act h = conv(x, l + ".0", 1, 3, 1, 1, 0, dil, dil);
            groupNormRows(h, l + ".1", true);
            Act g = conv(h, l + ".3", 1, 1, 1, 1, 0, 0);
            groupNormRows(g, l + ".4", false);
            const Tensor &scale = w(l + ".6.scale");
            const long n = x.n();
            for (int c = 0; c < x.C; ++c) {
                const float s = scale.v[size_t(c)];
                const float *a = g.row(c), *b = g.row(c + x.C);
                float *y = x.row(c);
                for (long i = 0; i < n; ++i)
                    y[i] += s * a[i] * sigmoid(b[i]);
            }
        }
    }

    // HEncLayer, frequency branch or time branch.
    Act encode(const Act &x, int i, bool freq) {
        const std::string name = (freq ? "encoder." : "tencoder.") + std::to_string(i);
        Act y = freq ? conv(x, name + ".conv", 8, 1, 4, 1, 2, 0)
                     : conv(x, name + ".conv", 1, 8, 1, 4, 0, 2, 1, (4 - x.T % 4) % 4);
        geluAll(y);
        dconv(y, name);
        return glu(conv(y, name + ".rewrite", 1, 1, 1, 1, 0, 0));
    }

    // HDecLayer: the skip added, a 3-wide convolution, DConv, and the
    // transposed convolution up a level.
    Act decode(Act &x, const Act &skip, int j, bool freq, int length) {
        const std::string name = (freq ? "decoder." : "tdecoder.") + std::to_string(j);
        float *p = x.p();
        const float *s = skip.p();
        for (long i = 0, n = long(x.C) * x.n(); i < n; ++i)
            p[i] += s[i];
        Act y = glu(freq ? conv(x, name + ".rewrite", 3, 3, 1, 1, 1, 1) : conv(x, name + ".rewrite", 1, 3, 1, 1, 0, 1));
        dconv(y, name);
        Act z = convTr(y, name + ".conv_tr", freq, length);
        if (j != kDepth - 1)
            geluAll(z);
        return z;
    }

    // --- The cross transformer, on [512][tokens].

    // LayerNorm over the channels of each token.
    void layerNorm(const float *x, int C, long N, const std::string &name, float *out) {
        const Tensor &g = w(name + ".weight"), &b = w(name + ".bias");
        Buffer mean{size_t(N)}, var{size_t(N)};
        float *mu = mean.data(), *va = var.data();
        std::fill(mu, mu + N, 0.f);
        std::fill(va, va + N, 0.f);
        for (int c = 0; c < C; ++c) {
            const float *r = x + long(c) * N;
            for (long i = 0; i < N; ++i)
                mu[i] += r[i];
        }
        for (long i = 0; i < N; ++i)
            mu[i] /= float(C);
        for (int c = 0; c < C; ++c) {
            const float *r = x + long(c) * N;
            for (long i = 0; i < N; ++i) {
                const float d = r[i] - mu[i];
                va[i] += d * d;
            }
        }
        for (long i = 0; i < N; ++i)
            va[i] = 1.0f / std::sqrt(va[i] / float(C) + 1e-5f);
        for (int c = 0; c < C; ++c) {
            const float *r = x + long(c) * N;
            float *o = out + long(c) * N;
            const float gc = g.v[size_t(c)], bc = b.v[size_t(c)];
            for (long i = 0; i < N; ++i)
                o[i] = (r[i] - mu[i]) * va[i] * gc + bc;
        }
    }

    // MyGroupNorm with one group: over every token and channel at once.
    void groupNormAll(float *x, int C, long N, const std::string &name) {
        const Tensor &g = w(name + ".weight"), &b = w(name + ".bias");
        double sum = 0, sq = 0;
        for (int c = 0; c < C; ++c) {
            const float *r = x + long(c) * N;
            float s = 0, q = 0;
            for (long i = 0; i < N; ++i) {
                s += r[i];
                q += r[i] * r[i];
            }
            sum += s;
            sq += q;
        }
        const double count = double(C) * N, mean = sum / count;
        const float rstd = float(1.0 / std::sqrt(std::max(0.0, sq / count - mean * mean) + 1e-5));
        for (int c = 0; c < C; ++c) {
            float *r = x + long(c) * N;
            const float scale = rstd * g.v[size_t(c)], shift = b.v[size_t(c)] - float(mean) * scale;
            for (long i = 0; i < N; ++i)
                r[i] = r[i] * scale + shift;
        }
    }

    // nn.MultiheadAttention, eight heads of 64: queries from q (Nq
    // tokens), keys and values from kv (Nk tokens, the same as q when
    // self).
    void attention(const float *q, long Nq, const float *kv, long Nk, bool self, const std::string &name, float *out) {
        constexpr int D = 512, H = 8, Dh = 64;
        const Tensor &Win = w(name + ".in_proj_weight"), &Bin = w(name + ".in_proj_bias");
        Buffer Q(size_t(D) * Nq), KV(size_t(2 * D) * Nk);
        auto project = [&](const float *x, long N, int row0, int rows, float *y) {
            for (int r = 0; r < rows; ++r)
                std::fill(y + long(r) * N, y + long(r + 1) * N, Bin.v[size_t(row0 + r)]);
            gemm(rows, int(N), D, Win.data() + long(row0) * D, D, false, x, N, false, y, N, true, threads);
        };
        project(q, Nq, 0, D, Q.data());
        project(self ? q : kv, Nk, D, 2 * D, KV.data());
        {
            float *p = Q.data();
            const float scale = 1.0f / std::sqrt(float(Dh));
            for (long i = 0, n = long(D) * Nq; i < n; ++i)
                p[i] *= scale;
        }
        const float *K = KV.data(), *V = KV.data() + long(D) * Nk;
        Buffer O(size_t(D) * Nq);
        // The scores are computed transposed, keys × queries: the softmax
        // then runs down columns, eight queries to a vector with no
        // horizontal sums, and the product with V reads them in order.
        // Queries go 264 at a time (22 of the kernel's panels).
        constexpr long QB = 264;
        Buffer S(size_t(QB) * Nk);
        std::vector<float> top(QB), sum(QB);
        for (int h = 0; h < H; ++h) {
            check();
            for (long q0 = 0; q0 < Nq; q0 += QB) {
                const long qb = std::min(QB, Nq - q0);
                float *s = S.data();
                gemm(int(Nk), int(qb), Dh, K + long(h) * Dh * Nk, Nk, true, Q.data() + long(h) * Dh * Nq + q0, Nq,
                     false, s, qb, false, threads);
                std::fill(top.begin(), top.begin() + qb, -3.0e38f);
                std::fill(sum.begin(), sum.begin() + qb, 0.f);
                float *mx = top.data(), *sm = sum.data();
                for (long k = 0; k < Nk; ++k) {
                    const float *r = s + k * qb;
                    for (long i = 0; i < qb; ++i)
                        mx[i] = std::max(mx[i], r[i]);
                }
                for (long k = 0; k < Nk; ++k) {
                    float *r = s + k * qb;
                    for (long i = 0; i < qb; ++i) {
                        r[i] = fastExp(r[i] - mx[i]);
                        sm[i] += r[i];
                    }
                }
                // The division by each query's sum is left until after the
                // product with V: 64 a query instead of one per key.
                float *o = O.data() + long(h) * Dh * Nq + q0;
                gemm(Dh, int(qb), int(Nk), V + long(h) * Dh * Nk, Nk, false, s, qb, false, o, Nq, false, threads);
                for (long i = 0; i < qb; ++i)
                    sm[i] = 1.0f / sm[i];
                for (int d = 0; d < Dh; ++d)
                    for (long i = 0; i < qb; ++i)
                        o[long(d) * Nq + i] *= sm[i];
            }
        }
        const Tensor &Wo = w(name + ".out_proj.weight"), &Bo = w(name + ".out_proj.bias");
        for (int r = 0; r < D; ++r)
            std::fill(out + long(r) * Nq, out + long(r + 1) * Nq, Bo.v[size_t(r)]);
        gemm(D, int(Nq), D, Wo.data(), D, false, O.data(), Nq, false, out, Nq, true, threads);
    }

    // linear2(gelu(linear1(x))).
    void feedForward(const float *x, long N, const std::string &name, float *out) {
        constexpr int D = 512, Hd = 2048;
        const Tensor &W1 = w(name + ".linear1.weight"), &B1 = w(name + ".linear1.bias");
        const Tensor &W2 = w(name + ".linear2.weight"), &B2 = w(name + ".linear2.bias");
        Buffer h(size_t(Hd) * N);
        for (int r = 0; r < Hd; ++r)
            std::fill(h.data() + long(r) * N, h.data() + long(r + 1) * N, B1.v[size_t(r)]);
        gemm(Hd, int(N), D, W1.data(), D, false, x, N, false, h.data(), N, true, threads);
        float *p = h.data();
        for (long i = 0, n = long(Hd) * N; i < n; ++i)
            p[i] = gelu(p[i]);
        for (int r = 0; r < D; ++r)
            std::fill(out + long(r) * N, out + long(r + 1) * N, B2.v[size_t(r)]);
        gemm(D, int(N), Hd, W2.data(), Hd, false, h.data(), N, false, out, N, true, threads);
    }

    // x += gamma · y, per channel (LayerScale).
    void addScaled(float *x, const float *y, long N, const std::string &name) {
        const Tensor &g = w(name + ".scale");
        for (int c = 0; c < 512; ++c) {
            const float s = g.v[size_t(c)];
            float *r = x + long(c) * N;
            const float *a = y + long(c) * N;
            for (long i = 0; i < N; ++i)
                r[i] += s * a[i];
        }
    }

    // MyTransformerEncoderLayer (other == nullptr) or
    // CrossTransformerEncoderLayer, norm_first, on x in place.
    void layer(float *x, long N, const float *other, long No, const std::string &name) {
        constexpr int D = 512;
        Buffer h(size_t(D) * N), a(size_t(D) * N);
        layerNorm(x, D, N, name + ".norm1", h.data());
        if (!other) {
            attention(h.data(), N, h.data(), N, true, name + ".self_attn", a.data());
        } else {
            Buffer k(size_t(D) * No);
            layerNorm(other, D, No, name + ".norm2", k.data());
            attention(h.data(), N, k.data(), No, false, name + ".cross_attn", a.data());
        }
        addScaled(x, a.data(), N, name + ".gamma_1");
        check();
        layerNorm(x, D, N, name + (other ? ".norm3" : ".norm2"), h.data());
        feedForward(h.data(), N, name, a.data());
        addScaled(x, a.data(), N, name + ".gamma_2");
        groupNormAll(x, D, N, name + ".norm_out");
    }

    // CrossTransformerEncoder.forward, with the channel up- and
    // downsamplers around it. x is [384][8][T1], xt [384][T2].
    void transformer(Act &x, Act &xt, float from, float to) {
        constexpr int D = 512;
        Act u = conv(x, "channel_upsampler", 1, 1, 1, 1, 0, 0);
        Act ut = conv(xt, "channel_upsampler_t", 1, 1, 1, 1, 0, 0);
        show("up", u);
        show("up_t", ut);
        const int Fr = x.F, T1 = x.T, T2 = xt.T;
        const long N = long(Fr) * T1, Nt = T2;
        // Tokens stay in [512][f·T1 + t] order; PyTorch's (t·Fr + f) order
        // differs, but nothing here depends on the order of the tokens.
        Act a(D, 1, int(N)), at(D, 1, int(Nt));
        layerNorm(u.p(), D, N, "crosstransformer.norm_in", a.p());
        layerNorm(ut.p(), D, Nt, "crosstransformer.norm_in_t", at.p());
        // create_2d_sin_embedding: the first half of the channels by time,
        // the second by frequency, sines and cosines interleaved.
        for (int j = 0; j < D / 4; ++j) {
            const double div = std::exp(double(2 * j) * -(std::log(10000.0) / (D / 2)));
            for (int f = 0; f < Fr; ++f)
                for (int t = 0; t < T1; ++t) {
                    const long n = long(f) * T1 + t;
                    a.row(2 * j)[n] += float(std::sin(t * div));
                    a.row(2 * j + 1)[n] += float(std::cos(t * div));
                    a.row(D / 2 + 2 * j)[n] += float(std::sin(f * div));
                    a.row(D / 2 + 2 * j + 1)[n] += float(std::cos(f * div));
                }
        }
        // create_sin_embedding: cosines, then sines.
        for (int i = 0; i < D / 2; ++i) {
            const double period = std::pow(10000.0, double(i) / (D / 2 - 1));
            for (int t = 0; t < T2; ++t) {
                at.row(i)[t] += float(std::cos(t / period));
                at.row(D / 2 + i)[t] += float(std::sin(t / period));
            }
        }
        for (int i = 0; i < 5; ++i) {
            const std::string li = "crosstransformer.layers." + std::to_string(i),
                              lt = "crosstransformer.layers_t." + std::to_string(i);
            if (i % 2 == 0) {
                layer(a.p(), N, nullptr, 0, li);
                layer(at.p(), Nt, nullptr, 0, lt);
            } else {
                Act old(D, 1, int(N));
                std::memcpy(old.p(), a.p(), sizeof(float) * size_t(D) * size_t(N));
                layer(a.p(), N, at.p(), Nt, li);
                layer(at.p(), Nt, old.p(), N, lt);
            }
            if (probe) {
                // As PyTorch hands them out: [tokens][512], tokens t-major.
                std::vector<float> tok(size_t(N) * D), tokt(size_t(Nt) * D);
                for (int c = 0; c < D; ++c) {
                    for (int f = 0; f < Fr; ++f)
                        for (int t = 0; t < T1; ++t)
                            tok[(size_t(t) * Fr + f) * D + c] = a.row(c)[long(f) * T1 + t];
                    for (int t = 0; t < T2; ++t)
                        tokt[size_t(t) * D + c] = at.row(c)[t];
                }
                probe(("tl" + std::to_string(i)).c_str(), tok.data(), {int(N), D});
                probe(("tlt" + std::to_string(i)).c_str(), tokt.data(), {int(Nt), D});
            }
            step(from + (to - from) * float(i + 1) / 5);
        }
        a.F = Fr;
        a.T = T1;
        show("ct_x", a);
        show("ct_xt", at);
        x = conv(a, "channel_downsampler", 1, 1, 1, 1, 0, 0);
        xt = conv(at, "channel_downsampler_t", 1, 1, 1, 1, 0, 0);
    }

    const Model &m;
    const int threads;
    const std::atomic<bool> *cancel;
    std::atomic<float> *progressOut;
    const Probe probe;
};

// ---------------------------------------------------------------------------
// The spectrogram: torch.stft and torch.istft as HTDemucs calls them (a
// periodic Hann window of 4096, hop 1024, normalized=True).

class Fft {
public:
    explicit Fft(int n) : n(n), rev(size_t(n)), wr(size_t(n / 2)), wi(size_t(n / 2)) {
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            rev[size_t(i)] = r;
        }
        for (int k = 0; k < n / 2; ++k) {
            wr[size_t(k)] = float(std::cos(-2 * kPi * k / n));
            wi[size_t(k)] = float(std::sin(-2 * kPi * k / n));
        }
    }
    // In place; inverse without the 1/n.
    void run(float *re, float *im, bool inverse) const {
        for (int i = 0; i < n; ++i) {
            const int r = rev[size_t(i)];
            if (r > i) {
                std::swap(re[i], re[r]);
                std::swap(im[i], im[r]);
            }
        }
        const float sign = inverse ? -1.f : 1.f;
        for (int h = 1; h < n; h <<= 1) {
            const int step = n / (2 * h);
            for (int i = 0; i < n; i += 2 * h)
                for (int j = 0; j < h; ++j) {
                    const float cr = wr[size_t(j * step)], ci = sign * wi[size_t(j * step)];
                    float *ar = re + i + j, *ai = im + i + j, *br = ar + h, *bi = ai + h;
                    const float tr = *br * cr - *bi * ci, ti = *br * ci + *bi * cr;
                    *br = *ar - tr;
                    *bi = *ai - ti;
                    *ar += tr;
                    *ai += ti;
                }
        }
    }
    const int n;

private:
    std::vector<int> rev;
    std::vector<float> wr, wi;
};

const std::vector<float> &hann() {
    static const std::vector<float> w = [] {
        std::vector<float> v(kFft);
        for (int i = 0; i < kFft; ++i)
            v[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * kPi * i / kFft));
        return v;
    }();
    return w;
}

// HTDemucs._spec, with the magnitude as complex-as-channels: [4][2048][le]
// for a 2 × L input, the channels left re, left im, right re, right im.
Act spectrogram(const float *in, int L) {
    const int le = (L + kHop - 1) / kHop, pad = kHop / 2 * 3;
    const int padded = le * kHop + 2 * pad;
    // Reflection padding; of torch.stft's own centring nothing is reached
    // by the frames that are kept.
    std::vector<float> x[2];
    for (int c = 0; c < 2; ++c) {
        x[c].resize(size_t(padded));
        for (int i = 0; i < padded; ++i) {
            int j = i - pad;
            if (j < 0)
                j = -j;
            if (j >= L)
                j = 2 * (L - 1) - j;
            x[c][size_t(i)] = j >= 0 && j < L ? in[long(c) * L + j] : 0.f;
        }
    }
    Act z(4, kBins, le);
    const Fft fft(kFft);
    const auto &win = hann();
    std::vector<float> re(kFft), im(kFft);
    const float norm = 1.0f / std::sqrt(float(kFft));
    for (int t = 0; t < le; ++t) {
        // Both channels in one transform: left as the real part, right as
        // the imaginary, told apart by their symmetries afterwards.
        for (int i = 0; i < kFft; ++i) {
            re[size_t(i)] = x[0][size_t(t * kHop + i)] * win[size_t(i)];
            im[size_t(i)] = x[1][size_t(t * kHop + i)] * win[size_t(i)];
        }
        fft.run(re.data(), im.data(), false);
        for (int k = 0; k < kBins; ++k) {
            const int mk = (kFft - k) % kFft;
            const float ar = re[size_t(k)], ai = im[size_t(k)], br = re[size_t(mk)], bi = im[size_t(mk)];
            z.row(0)[long(k) * le + t] = 0.5f * (ar + br) * norm;
            z.row(1)[long(k) * le + t] = 0.5f * (ai - bi) * norm;
            z.row(2)[long(k) * le + t] = 0.5f * (ai + bi) * norm;
            z.row(3)[long(k) * le + t] = -0.5f * (ar - br) * norm;
        }
    }
    return z;
}

// HTDemucs._ispec for two channels' spectra at once (complex as channels,
// [4][2048][le]), added into out[2][L].
void inverseSpectrogram(const float *re0, const float *im0, const float *re1, const float *im1, int le, int L,
                        float *out0, float *out1) {
    const Fft fft(kFft);
    const auto &win = hann();
    // The window's overlap: with a hop of a quarter, four frames cover
    // every sample of the part that is kept.
    std::vector<float> env(kHop);
    for (int r = 0; r < kHop; ++r) {
        double s = 0;
        for (int j = 0; j < 4; ++j)
            s += double(win[size_t(r + j * kHop)]) * win[size_t(r + j * kHop)];
        env[size_t(r)] = float(s);
    }
    std::vector<float> re(kFft), im(kFft);
    const float norm = std::sqrt(float(kFft)) / float(kFft);
    const int pad = kHop / 2 * 3;
    for (int t = 0; t < le; ++t) {
        // One inverse transform for both: A + iB, each Hermitian; irfft
        // drops the imaginary parts of DC and Nyquist, so they go here too.
        for (int k = 0; k <= kBins; ++k) {
            const float ar = k < kBins ? re0[long(k) * le + t] : 0.f, ai = k < kBins && k > 0 ? im0[long(k) * le + t] : 0.f;
            const float br = k < kBins ? re1[long(k) * le + t] : 0.f, bi = k < kBins && k > 0 ? im1[long(k) * le + t] : 0.f;
            re[size_t(k)] = ar - bi;
            im[size_t(k)] = ai + br;
            if (k > 0 && k < kBins) {
                re[size_t(kFft - k)] = ar + bi;
                im[size_t(kFft - k)] = br - ai;
            }
        }
        fft.run(re.data(), im.data(), true);
        // Frame t covers samples [t·hop − pad, t·hop − pad + 4096).
        const int start = t * kHop - pad;
        for (int i = 0; i < kFft; ++i) {
            const int s = start + i;
            if (s < 0 || s >= L)
                continue;
            const float g = win[size_t(i)] * norm / env[size_t((s + pad + 2 * kHop) % kHop)];
            out0[s] += re[size_t(i)] * g;
            out1[s] += im[size_t(i)] * g;
        }
    }
}

void Net::forward(const float *in, float *out) {
    const int L = kSegment;
    step(0);
    Act spec = spectrogram(in, L);
    if (probe) {
        std::vector<float> re(size_t(2) * spec.n()), im(size_t(2) * spec.n());
        for (int c = 0; c < 2; ++c) {
            std::memcpy(re.data() + c * spec.n(), spec.row(2 * c), sizeof(float) * size_t(spec.n()));
            std::memcpy(im.data() + c * spec.n(), spec.row(2 * c + 1), sizeof(float) * size_t(spec.n()));
        }
        probe("spec_re", re.data(), {2, spec.F, spec.T});
        probe("spec_im", im.data(), {2, spec.F, spec.T});
    }
    // Both branches normalised by their own mean and (unbiased) deviation.
    auto normalise = [](Act &a, double &mean, double &stdev) {
        const long n = long(a.C) * a.n();
        const float *p = a.p();
        double s = 0;
        for (long i = 0; i < n; ++i)
            s += p[i];
        mean = s / double(n);
        double q = 0;
        for (long i = 0; i < n; ++i)
            q += (p[i] - mean) * (p[i] - mean);
        stdev = std::sqrt(q / double(n - 1));
        const float m = float(mean), inv = float(1.0 / (1e-5 + stdev));
        float *w = a.p();
        for (long i = 0; i < n; ++i)
            w[i] = (w[i] - m) * inv;
    };
    double mean, stdev, meant, stdt;
    Act x = std::move(spec);
    normalise(x, mean, stdev);
    Act xt(2, 1, L);
    std::memcpy(xt.p(), in, sizeof(float) * 2 * size_t(L));
    normalise(xt, meant, stdt);

    // The encoder.
    Act saved[kDepth], savedT[kDepth];
    int lengthsT[kDepth];
    for (int i = 0; i < kDepth; ++i) {
        lengthsT[i] = xt.T;
        xt = encode(xt, i, false);
        show(("tenc" + std::to_string(i)).c_str(), xt);
        x = encode(x, i, true);
        show(("enc" + std::to_string(i)).c_str(), x);
        if (i == 0) {
            // The frequency embedding, so that the convolutions over
            // frequency can tell where they are.
            const Tensor &e = w("freq_emb.embedding.weight");
            for (int c = 0; c < x.C; ++c)
                for (int f = 0; f < x.F; ++f) {
                    const float v = 0.2f * 10.0f * e.v[size_t(f) * size_t(x.C) + size_t(c)];
                    float *r = x.row(c) + long(f) * x.T;
                    for (int t = 0; t < x.T; ++t)
                        r[t] += v;
                }
        }
        saved[i] = Act(x.C, x.F, x.T);
        std::memcpy(saved[i].p(), x.p(), sizeof(float) * size_t(x.C) * size_t(x.n()));
        savedT[i] = Act(xt.C, xt.F, xt.T);
        std::memcpy(savedT[i].p(), xt.p(), sizeof(float) * size_t(xt.C) * size_t(xt.n()));
        step(0.04f * float(i + 1));
    }

    transformer(x, xt, 0.16f, 0.80f);

    // The decoder.
    for (int j = 0; j < kDepth; ++j) {
        const int level = kDepth - 1 - j;
        x = decode(x, saved[level], j, true, 0);
        saved[level] = Act();
        show(("dec" + std::to_string(j)).c_str(), x);
        xt = decode(xt, savedT[level], j, false, lengthsT[level]);
        savedT[level] = Act();
        show(("tdec" + std::to_string(j)).c_str(), xt);
        step(0.80f + 0.04f * float(j + 1));
    }

    // Back to sound: the spectrogram branch through the inverse transform,
    // the time branch as it is, each with its normalisation undone.
    const int le = x.T;
    for (long i = 0, n = long(x.C) * x.n(); i < n; ++i)
        x.p()[i] = x.p()[i] * float(stdev) + float(mean);
    for (int s = 0; s < kSources; ++s) {
        float *o0 = out + (long(s) * 2 + 0) * L, *o1 = out + (long(s) * 2 + 1) * L;
        for (int i = 0; i < L; ++i) {
            o0[i] = xt.row(2 * s)[i] * float(stdt) + float(meant);
            o1[i] = xt.row(2 * s + 1)[i] * float(stdt) + float(meant);
        }
        inverseSpectrogram(x.row(4 * s), x.row(4 * s + 1), x.row(4 * s + 2), x.row(4 * s + 3), le, L, o0, o1);
    }
    step(1.0f);
}

} // namespace

void segment(const Model &model, const float *in, float *out, const Probe &probe) {
    Net net(model, 1, nullptr, nullptr, probe);
    net.forward(in, out);
}

// ---------------------------------------------------------------------------
// A song: normalised, cut into segments, separated a segment per core, and
// cross-faded back together (demucs.apply.apply_model with split=True).

bool separate44(const Model &model, const float *const in[2], size_t n, std::vector<float> out[kSources][2],
                const Options &options, std::string *error) {
    auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    if (n == 0)
        return fail("The song is empty");
    // Demucs is used on audio normalised by the mono mix's mean and
    // deviation.
    double sum = 0;
    for (size_t i = 0; i < n; ++i)
        sum += 0.5 * (double(in[0][i]) + in[1][i]);
    const double mean = sum / double(n);
    double sq = 0;
    for (size_t i = 0; i < n; ++i) {
        const double d = 0.5 * (double(in[0][i]) + in[1][i]) - mean;
        sq += d * d;
    }
    const double deviation = std::sqrt(sq / double(std::max<size_t>(1, n - 1))) + 1e-8;

    const Cores cores = fastCores();
    const int threads = options.threads > 0 ? options.threads : cores.count;
    const long seg = kSegment;
    auto count = [&](long stride) { return int((long(n) + stride - 1) / stride); };
    // A worker needs about 0.4 GB; half the memory is ours to take.
    const int affordable = std::max(1, int((memoryGigabytes() / 2 - 0.5) / 0.4));
    long stride = options.stride > 0 ? options.stride : long(0.75 * seg);
    const int segments0 = count(stride);
    const int workers = std::max(1, std::min({threads, segments0, affordable}));
    if (options.stride <= 0 && workers > 1) {
        // The workers take segments in rounds, and a last round of only a
        // few leaves most cores idle for all of it: a stride stretched a
        // little (an overlap down to 0.15) can even the rounds out.
        const int even = segments0 / workers * workers;
        if (even != segments0 && even > 0) {
            const long stretched = (long(n) + even - 1) / even;
            if (stretched <= long(0.85 * seg) && count(stretched) == even)
                stride = stretched;
        }
    }
    const int segments = count(stride);
    const int inner = std::max(1, threads / workers);

    for (int s = 0; s < kSources; ++s)
        for (int c = 0; c < 2; ++c)
            out[s][c].assign(n, 0.f);
    std::vector<float> weightSum(n, 0.f);
    // A triangle, highest in the middle of the segment.
    std::vector<float> weight(static_cast<size_t>(seg));
    const long half = seg / 2, top = std::max(half, seg - half);
    for (long i = 0; i < seg; ++i)
        weight[size_t(i)] = float(i < half ? i + 1 : seg - i) / float(top);

    std::atomic<int> next{0}, done{0};
    std::atomic<bool> stop{false}, failed{false};
    std::vector<std::atomic<float>> partial(static_cast<size_t>(workers));
    for (auto &p : partial)
        p = 0;
    std::mutex adding;
    std::string why;
    auto work = [&](int me) {
        keepOn(cores);
        try {
            std::vector<float> input(size_t(2 * seg)), output(size_t(kSources) * 2 * size_t(seg));
            Net net(model, inner, &stop, &partial[size_t(me)], {});
            for (int k; !stop && (k = next.fetch_add(1)) < segments;) {
                const long offset = long(k) * stride, length = std::min(seg, long(n) - offset);
                // TensorChunk.padded: a short last segment takes the song
                // before it as well, centred, and zeros after the end.
                const long start = offset - (seg - length) / 2;
                for (int c = 0; c < 2; ++c)
                    for (long i = 0; i < seg; ++i) {
                        const long j = start + i;
                        input[size_t(c * seg + i)] =
                            j >= 0 && j < long(n) ? float((double(in[c][j]) - mean) / deviation) : 0.f;
                    }
                net.forward(input.data(), output.data());
                const long trim = (seg - length) / 2;
                {
                    std::lock_guard lock(adding);
                    for (int s = 0; s < kSources; ++s)
                        for (int c = 0; c < 2; ++c) {
                            const float *src = output.data() + (long(s) * 2 + c) * seg + trim;
                            float *dst = out[s][c].data() + offset;
                            for (long i = 0; i < length; ++i)
                                dst[i] += weight[size_t(i)] * src[i];
                        }
                    for (long i = 0; i < length; ++i)
                        weightSum[size_t(offset + i)] += weight[size_t(i)];
                }
                partial[size_t(me)] = 0;
                ++done;
            }
        } catch (const Net::Cancelled &) {
        } catch (const std::bad_alloc &) {
            std::lock_guard lock(adding);
            why = "The computer ran out of memory while separating the song";
            failed = true;
            stop = true;
        } catch (const std::exception &) {
            std::lock_guard lock(adding);
            why = "The stem model's file is damaged";
            failed = true;
            stop = true;
        }
        Buffer::drain();
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < workers; ++i)
        pool.emplace_back(work, i);
    float shown = 0;
    while (done < segments && !failed) {
        if (options.cancel && options.cancel->load())
            stop = true;
        if (stop)
            break;
        float now = float(done.load());
        for (auto &p : partial)
            now += p.load();
        shown = std::max(shown, std::min(0.999f, now / float(segments)));
        if (options.progress)
            options.progress(shown);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto &t : pool)
        t.join();
    if (failed)
        return fail(why.c_str());
    if (done < segments)
        return fail("Stems called off");
    for (size_t i = 0; i < n; ++i) {
        const float inv = 1.0f / weightSum[i];
        for (int s = 0; s < kSources; ++s)
            for (int c = 0; c < 2; ++c)
                out[s][c][i] = float(out[s][c][i] * inv * deviation + mean);
    }
    if (options.progress)
        options.progress(1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Sample rates.

namespace {

double besselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

} // namespace

std::vector<float> resample(const float *in, size_t n, int from, int to) {
    if (from == to)
        return std::vector<float>(in, in + n);
    const int g = std::gcd(from, to);
    const long up = to / g, down = from / g;
    const size_t outN = size_t((uint64_t(n) * uint64_t(to) + uint64_t(from) - 1) / uint64_t(from));
    // Cut off at 95.5% of the lower rate's Nyquist frequency, 100 dB down
    // by its end (Kaiser's formulas, β = 10.06 and 142 taps at the lower
    // rate).
    const double lower = std::min(from, to);
    const double fc = 0.955 * 0.5 * lower / from; // cycles per input sample
    const double beta = 10.06;
    const int half = int(std::ceil(71.0 * from / lower));
    const int taps = 2 * half;
    // One row of taps per phase; past 4096 phases (rates with no small
    // ratio) the nearest of 4096 is used.
    const long phases = std::min<long>(up, 4096);
    std::vector<float> table(size_t(phases) * size_t(taps));
    const double i0 = besselI0(beta);
    for (long p = 0; p < phases; ++p) {
        const double frac = double(p) / double(phases);
        double total = 0;
        for (int j = 0; j < taps; ++j) {
            const double t = double(j - half + 1) - frac;
            const double u = t / (half + 1);
            const double win = std::fabs(u) < 1 ? besselI0(beta * std::sqrt(1 - u * u)) / i0 : 0.0;
            const double x = 2 * fc * t;
            const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            table[size_t(p * taps + j)] = float(2 * fc * sinc * win);
            total += 2 * fc * sinc * win;
        }
        // Each phase passes DC unchanged.
        for (int j = 0; j < taps; ++j)
            table[size_t(p * taps + j)] = float(table[size_t(p * taps + j)] / total);
    }
    std::vector<float> out(outN);
    for (size_t k = 0; k < outN; ++k) {
        const uint64_t pos = uint64_t(k) * uint64_t(down);
        const long base = long(pos / uint64_t(up));
        const long phase = phases == up ? long(pos % uint64_t(up)) : long((pos % uint64_t(up)) * uint64_t(phases) / uint64_t(up));
        const float *h = table.data() + phase * taps;
        const long first = base - half + 1;
        float acc = 0;
        if (first >= 0 && first + taps <= long(n)) {
            const float *x = in + first;
            for (int j = 0; j < taps; ++j)
                acc += x[j] * h[j];
        } else {
            for (int j = 0; j < taps; ++j) {
                const long i = first + j;
                if (i >= 0 && i < long(n))
                    acc += in[i] * h[j];
            }
        }
        out[k] = acc;
    }
    return out;
}

bool separate(const Model &model, const int16_t *mix, size_t frames, int rate, std::vector<int16_t> out[kSources],
              const Options &options, std::string *error) {
    if (frames == 0) {
        if (error)
            *error = "The song is empty";
        return false;
    }
    std::vector<float> ch[2], at44[2];
    for (int c = 0; c < 2; ++c) {
        ch[c].resize(frames);
        for (size_t i = 0; i < frames; ++i)
            ch[c][i] = float(mix[2 * i + size_t(c)]) / 32768.0f;
    }
    const Cores cores = fastCores();
    const int threads = options.threads > 0 ? options.threads : cores.count;
    parallelFor(2, threads, [&](int c) { at44[c] = resample(ch[c].data(), frames, rate, kRate); });
    ch[0] = {};
    ch[1] = {};
    const float *in[2] = {at44[0].data(), at44[1].data()};
    std::vector<float> stems[kSources][2];
    // The resampling's few seconds are counted as a twentieth of the work.
    Options inner = options;
    if (options.progress)
        inner.progress = [&](float p) { options.progress(0.95f * p); };
    if (!separate44(model, in, at44[0].size(), stems, inner, error))
        return false;
    at44[0] = {};
    at44[1] = {};
    if (options.cancel && options.cancel->load()) {
        if (error)
            *error = "Stems called off";
        return false;
    }
    parallelFor(kSources * 2, threads, [&](int k) {
        const int s = k / 2, c = k % 2;
        std::vector<float> back = resample(stems[s][c].data(), stems[s][c].size(), kRate, rate);
        stems[s][c] = std::move(back);
    });
    for (int s = 0; s < kSources; ++s) {
        out[s].assign(frames * 2, 0);
        for (int c = 0; c < 2; ++c) {
            const std::vector<float> &v = stems[s][c];
            for (size_t i = 0; i < frames && i < v.size(); ++i)
                out[s][2 * i + size_t(c)] = int16_t(std::clamp(std::lrint(v[i] * 32768.0f), -32768L, 32767L));
        }
    }
    if (options.progress)
        options.progress(1.0f);
    return true;
}

} // namespace tf::stems
