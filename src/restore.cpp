#include "restore.h"

// The network follows the Apollo paper and the shapes of its published
// weights. Activations are kept as [256 channels][80 bands][frames], so
// that a 1×1 convolution over every band and frame at once is one matrix
// product; the attention across bands gathers a few frames' worth at a
// time into [256][frames × 80] and back.

#include <QByteArray>
#include <QFile>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "stems.h"

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

namespace tf::restore {

namespace {

constexpr int kWin = 882;     // 20 ms
constexpr int kBins = 442;    // kWin / 2 + 1
constexpr int kBands = 80;    // 79 of 5 bins, the last 47
constexpr int kDim = 256;     // features per band
constexpr int kLayers = 6;
constexpr int kHeads = 8, kHead = 32;
constexpr int kBlocks = 3;    // convolution blocks per layer, each 7 frames wide
constexpr int kGroup = 16;    // frames the attention gathers at a time
constexpr double kPi = 3.14159265358979323846;

int bandWidth(int b) { return b < kBands - 1 ? 5 : kBins - 5 * (kBands - 1); }
int bandStart(int b) { return 5 * b; }

} // namespace

// ---------------------------------------------------------------------------
// The weights.

struct Tensor {
    std::vector<int> shape;
    std::vector<float> v;
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

// Every weight the network reads, with its shape: what makes a file this
// network's, checked before any song is spent on it.
std::vector<std::pair<std::string, std::vector<int>>> expectedShapes() {
    std::vector<std::pair<std::string, std::vector<int>>> e;
    auto add = [&](const std::string &n, std::vector<int> s) { e.emplace_back(n, std::move(s)); };
    for (int b = 0; b < kBands; ++b) {
        const int w = bandWidth(b);
        const std::string bn = "BN." + std::to_string(b), out = "output." + std::to_string(b);
        add(bn + ".0.weight", {2 * w + 1});
        add(bn + ".1.weight", {kDim, 2 * w + 1, 1});
        add(bn + ".1.bias", {kDim});
        add(out + ".0.weight", {kDim});
        add(out + ".1.weight", {4 * w, kDim, 1});
        add(out + ".1.bias", {4 * w});
    }
    for (int l = 0; l < kLayers; ++l) {
        const std::string band = "net." + std::to_string(l) + ".band_net.";
        add(band + "input_norm.weight", {kDim});
        add(band + "weight.weight", {3 * kDim, kDim, 1});
        add(band + "output.weight", {kDim, kDim, 1});
        add(band + "MLP.0.weight", {kDim});
        add(band + "MLP.1.weight", {8 * kDim, kDim, 1});
        add(band + "MLP_output.weight", {kDim, 4 * kDim, 1});
        add(band + "cos_freq", {100, kHead});
        add(band + "sin_freq", {100, kHead});
        for (int k = 0; k < kBlocks; ++k) {
            const std::string c = "net." + std::to_string(l) + ".seq_net.blocks." + std::to_string(k) + ".conv.";
            add(c + "0.weight", {kDim, 1, 7});
            add(c + "0.bias", {kDim});
            add(c + "1.weight", {kDim});
            add(c + "2.weight", {4 * kDim, kDim, 1});
            add(c + "2.bias", {4 * kDim});
            add(c + "4.weight", {kDim, 4 * kDim, 1});
            add(c + "4.bias", {kDim});
        }
    }
    return e;
}

// --- PyTorch's file: a zip, stored, not compressed.

struct ZipEntry {
    qsizetype at = 0, size = 0;
};

uint32_t le32(const char *p) {
    return uint32_t(uint8_t(p[0])) | uint32_t(uint8_t(p[1])) << 8 | uint32_t(uint8_t(p[2])) << 16 |
           uint32_t(uint8_t(p[3])) << 24;
}
uint16_t le16(const char *p) { return uint16_t(uint8_t(p[0]) | uint8_t(p[1]) << 8); }

bool readZip(const char *z, qsizetype n, std::unordered_map<std::string, ZipEntry> &entries) {
    if (n < 22)
        return false;
    // The end of the central directory: its signature, searched for from
    // the end, past at most a comment.
    qsizetype end = -1;
    for (qsizetype i = n - 22; i >= std::max<qsizetype>(0, n - 22 - 65535); --i)
        if (le32(z + i) == 0x06054b50u) {
            end = i;
            break;
        }
    if (end < 0)
        return false;
    const char *e = z + end;
    const qsizetype count = le16(e + 10), dirSize = le32(e + 12), dirAt = le32(e + 16);
    if (dirAt < 0 || dirSize < 0 || dirAt + dirSize > n)
        return false;
    qsizetype at = dirAt;
    for (qsizetype k = 0; k < count; ++k) {
        if (at + 46 > n || le32(z + at) != 0x02014b50u)
            return false;
        const char *c = z + at;
        const int method = le16(c + 10);
        const qsizetype size = le32(c + 20), nameLen = le16(c + 28), extraLen = le16(c + 30), commentLen = le16(c + 32),
                        local = le32(c + 42);
        if (at + 46 + nameLen > n)
            return false;
        const std::string name(c + 46, size_t(nameLen));
        at += 46 + nameLen + extraLen + commentLen;
        if (local + 30 > n || le32(z + local) != 0x04034b50u)
            return false;
        const qsizetype data = local + 30 + le16(z + local + 26) + le16(z + local + 28);
        if (method != 0 || data + size > n)
            continue; // compressed: never a tensor of PyTorch's
        entries[name] = {data, size};
    }
    return true;
}

// --- The pickle naming the tensors. Only plain data is built from it:
// dicts, lists, tuples, numbers, strings, and the tensors themselves.
// Anything else (the training's configuration objects) stays an opaque
// placeholder; no code is ever looked up or run.

struct Val;
using Ptr = std::shared_ptr<Val>;
struct Val {
    enum Kind { None, Bool, Int, Float, Str, Tuple, List, Dict, Global, Object, Storage, TensorRef } kind = None;
    long long i = 0;
    double f = 0;
    std::string s, s2;            // Str; Global's module and name; Storage's key and type
    std::vector<Ptr> items;       // Tuple, List
    std::vector<std::pair<Ptr, Ptr>> dict;
    Ptr storage;                  // TensorRef:
    long long offset = 0;
    std::vector<long long> size, stride;
};

Ptr make(Val::Kind k) {
    auto v = std::make_shared<Val>();
    v->kind = k;
    return v;
}

class Unpickler {
public:
    // Reads [from, from + size), which must outlive it.
    Unpickler(const char *from, qsizetype size) : p(from), end(from + size) {}

    Ptr run() {
        for (;;) {
            const uint8_t op = byte();
            switch (op) {
            case 0x80: byte(); break; // PROTO
            case 0x95: take(8); break; // FRAME
            case '}': push(make(Val::Dict)); break;
            case ']': push(make(Val::List)); break;
            case ')': push(make(Val::Tuple)); break;
            case '(': marks.push_back(stack.size()); break;
            case 't': {
                auto t = make(Val::Tuple);
                t->items = popMark();
                push(t);
                break;
            }
            case 0x85: case 0x86: case 0x87: { // TUPLE1..3
                auto t = make(Val::Tuple);
                const int k = op - 0x84;
                if (stack.size() < size_t(k))
                    throw std::runtime_error("pickle");
                t->items.assign(stack.end() - k, stack.end());
                stack.resize(stack.size() - size_t(k));
                push(t);
                break;
            }
            case 'X': str(le32(take(4))); break;
            case 0x8c: str(byte()); break;
            case 'J': num(int32_t(le32(take(4)))); break;
            case 'K': num(byte()); break;
            case 'M': num(le16(take(2))); break;
            case 0x8a: { // LONG1
                const int k = byte();
                const char *d = take(size_t(k));
                long long v = 0;
                for (int j = std::min(k, 8) - 1; j >= 0; --j)
                    v = v << 8 | uint8_t(d[j]);
                if (k > 0 && k < 8 && (uint8_t(d[k - 1]) & 0x80))
                    v -= 1LL << (8 * k);
                num(v);
                break;
            }
            case 'G': {
                const char *d = take(8);
                uint64_t bits = 0;
                for (int j = 0; j < 8; ++j)
                    bits = bits << 8 | uint8_t(d[j]);
                auto v = make(Val::Float);
                std::memcpy(&v->f, &bits, 8);
                push(v);
                break;
            }
            case 0x88: case 0x89: {
                auto v = make(Val::Bool);
                v->i = op == 0x88;
                push(v);
                break;
            }
            case 'N': push(make(Val::None)); break;
            case 'q': memo[byte()] = top(); break;
            case 'r': memo[le32(take(4))] = top(); break;
            case 0x94: memo[uint32_t(memo.size())] = top(); break;
            case 'h': push(get(byte())); break;
            case 'j': push(get(le32(take(4)))); break;
            case 'c': {
                auto g = make(Val::Global);
                g->s = line();
                g->s2 = line();
                push(g);
                break;
            }
            case 0x93: { // STACK_GLOBAL
                auto g = make(Val::Global);
                g->s2 = pop()->s;
                g->s = pop()->s;
                push(g);
                break;
            }
            case 'R': {
                Ptr args = pop(), fn = pop();
                push(reduce(fn, args));
                break;
            }
            case 0x81: { // NEWOBJ
                pop();
                pop();
                push(make(Val::Object));
                break;
            }
            case 'b': pop(); break; // BUILD: an object's state, not needed
            case 'Q': push(persistent(pop())); break;
            case 's': {
                Ptr v = pop(), k = pop();
                if (top()->kind == Val::Dict)
                    top()->dict.emplace_back(k, v);
                break;
            }
            case 'u': {
                auto items = popMark();
                if (top()->kind == Val::Dict)
                    for (size_t j = 0; j + 1 < items.size(); j += 2)
                        top()->dict.emplace_back(items[j], items[j + 1]);
                break;
            }
            case 'a': {
                Ptr v = pop();
                if (top()->kind == Val::List)
                    top()->items.push_back(v);
                break;
            }
            case 'e': {
                auto items = popMark();
                if (top()->kind == Val::List)
                    top()->items.insert(top()->items.end(), items.begin(), items.end());
                break;
            }
            case '.': return pop();
            default: throw std::runtime_error("pickle");
            }
        }
    }

private:
    uint8_t byte() { return uint8_t(*take(1)); }
    const char *take(size_t k) {
        if (size_t(end - p) < k)
            throw std::runtime_error("pickle");
        const char *at = p;
        p += k;
        return at;
    }
    std::string line() {
        const char *nl = static_cast<const char *>(std::memchr(p, '\n', size_t(end - p)));
        if (!nl)
            throw std::runtime_error("pickle");
        std::string s(p, size_t(nl - p));
        p = nl + 1;
        return s;
    }
    void str(size_t k) {
        auto v = make(Val::Str);
        v->s.assign(take(k), k);
        push(v);
    }
    void num(long long i) {
        auto v = make(Val::Int);
        v->i = i;
        push(v);
    }
    void push(Ptr v) {
        if (stack.size() > 100000)
            throw std::runtime_error("pickle");
        stack.push_back(std::move(v));
    }
    Ptr pop() {
        if (stack.empty() || (!marks.empty() && stack.size() <= marks.back()))
            throw std::runtime_error("pickle");
        Ptr v = std::move(stack.back());
        stack.pop_back();
        return v;
    }
    Ptr top() const {
        if (stack.empty())
            throw std::runtime_error("pickle");
        return stack.back();
    }
    Ptr get(uint32_t k) const {
        const auto it = memo.find(k);
        if (it == memo.end())
            throw std::runtime_error("pickle");
        return it->second;
    }
    std::vector<Ptr> popMark() {
        if (marks.empty())
            throw std::runtime_error("pickle");
        const size_t m = marks.back();
        marks.pop_back();
        std::vector<Ptr> items(stack.begin() + long(m), stack.end());
        stack.resize(m);
        return items;
    }
    static bool global(const Ptr &v, const char *module, const char *name) {
        return v->kind == Val::Global && v->s == module && v->s2 == name;
    }
    static std::vector<long long> ints(const Ptr &v) {
        std::vector<long long> out;
        if (v->kind != Val::Tuple)
            throw std::runtime_error("pickle");
        for (const Ptr &x : v->items) {
            if (x->kind != Val::Int)
                throw std::runtime_error("pickle");
            out.push_back(x->i);
        }
        return out;
    }
    // torch._utils._rebuild_tensor_v2(storage, offset, size, stride, ...)
    // makes a tensor; collections.OrderedDict() a dict; anything else is a
    // placeholder.
    static Ptr reduce(const Ptr &fn, const Ptr &args) {
        if (global(fn, "torch._utils", "_rebuild_tensor_v2")) {
            if (args->kind != Val::Tuple || args->items.size() < 4 || args->items[0]->kind != Val::Storage ||
                args->items[1]->kind != Val::Int)
                throw std::runtime_error("pickle");
            auto t = make(Val::TensorRef);
            t->storage = args->items[0];
            t->offset = args->items[1]->i;
            t->size = ints(args->items[2]);
            t->stride = ints(args->items[3]);
            return t;
        }
        if (global(fn, "collections", "OrderedDict"))
            return make(Val::Dict);
        return make(Val::Object);
    }
    // ('storage', torch.FloatStorage, key, location, count): where a
    // tensor's numbers are in the zip.
    static Ptr persistent(const Ptr &id) {
        if (id->kind != Val::Tuple || id->items.size() < 5 || id->items[0]->kind != Val::Str ||
            id->items[0]->s != "storage" || id->items[1]->kind != Val::Global || id->items[2]->kind != Val::Str ||
            id->items[4]->kind != Val::Int)
            throw std::runtime_error("pickle");
        auto s = make(Val::Storage);
        s->s = id->items[2]->s;
        s->s2 = id->items[1]->s2;
        s->i = id->items[4]->i;
        return s;
    }

    const char *p, *end;
    std::vector<Ptr> stack;
    std::vector<size_t> marks;
    std::unordered_map<uint32_t, Ptr> memo;
};

} // namespace

std::shared_ptr<const Model> load(const std::string &path, std::string *error) {
    auto fail = [&](const QString &why) -> std::shared_ptr<const Model> {
        if (error)
            *error = why.toStdString();
        return nullptr;
    };
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("The restore model isn't there (%1)").arg(QString::fromStdString(path)));
    const QByteArray bytes = file.readAll();
    const QString damaged = QStringLiteral("The restore model's file is damaged (%1)").arg(QString::fromStdString(path));

    std::unordered_map<std::string, ZipEntry> entries;
    if (!readZip(bytes.constData(), bytes.size(), entries))
        return fail(damaged);
    // The archive's own folder: whatever comes before data.pkl.
    std::string prefix;
    bool found = false;
    for (const auto &[name, entry] : entries)
        if (name == "data.pkl" || (name.size() > 9 && name.compare(name.size() - 9, 9, "/data.pkl") == 0)) {
            prefix = name.substr(0, name.size() - 8);
            found = true;
            break;
        }
    if (!found)
        return fail(damaged);
    if (const auto bo = entries.find(prefix + "byteorder"); bo != entries.end())
        if (bytes.mid(bo->second.at, bo->second.size).trimmed() != "little")
            return fail(damaged);
    const ZipEntry pkl = entries[prefix + "data.pkl"];

    std::unordered_map<std::string, Ptr> tensors;
    try {
        Unpickler u(bytes.constData() + pkl.at, pkl.size);
        const Ptr root = u.run();
        Ptr state;
        if (root->kind == Val::Dict)
            for (const auto &[k, v] : root->dict)
                if (k->kind == Val::Str && k->s == "state_dict")
                    state = v;
        if (!state || state->kind != Val::Dict)
            return fail(damaged);
        for (const auto &[k, v] : state->dict)
            if (k->kind == Val::Str && v->kind == Val::TensorRef)
                tensors[k->s] = v;
    } catch (const std::exception &) {
        return fail(damaged);
    }

    auto model = std::make_shared<Model>();
    for (const auto &[name, shape] : expectedShapes()) {
        const auto it = tensors.find(name);
        if (it == tensors.end())
            return fail(damaged);
        const Val &t = *it->second;
        if (t.storage->s2 != "FloatStorage" || t.size.size() != shape.size())
            return fail(damaged);
        long long count = 1, expect = 1;
        for (size_t d = shape.size(); d-- > 0;) {
            if (t.size[d] != shape[d] || (shape[d] > 1 && t.stride[d] != expect))
                return fail(damaged); // only plain, contiguous tensors
            expect *= shape[d];
        }
        count = expect;
        const auto data = entries.find(prefix + "data/" + t.storage->s);
        if (data == entries.end() || t.offset < 0 || t.offset + count > t.storage->i ||
            data->second.size < qsizetype(t.storage->i) * 4)
            return fail(damaged);
        Tensor out;
        out.shape = shape;
        out.v.resize(size_t(count));
        const char *src = bytes.constData() + data->second.at + t.offset * 4;
        for (long long i = 0; i < count; ++i) {
            const uint32_t b = le32(src + 4 * i);
            std::memcpy(&out.v[size_t(i)], &b, 4);
        }
        model->t.emplace(name, std::move(out));
    }
    return model;
}

// ---------------------------------------------------------------------------
// The pieces of the network.

namespace {

// exp(x) to within a few units in the last place, in a form the compiler
// can vectorise (Cephes's expf), as stems.cpp has it.
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
inline float sigmoid(float x) { return 1.0f / (1.0f + fastExp(-x)); }
inline float silu(float x) { return x * sigmoid(x); }

// The short-time transform as torch.stft and torch.istft take it here: a
// periodic Hann window of 882, hop 441, centred. 882 is no power of two, so
// the transform is a matrix product, which costs little beside the network.
struct Dft {
    std::vector<float> window, forward, inverse;
    Dft() : window(kWin), forward(size_t(2 * kBins) * kWin), inverse(size_t(kWin) * 2 * kBins) {
        for (int i = 0; i < kWin; ++i)
            window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * kPi * i / kWin));
        for (int k = 0; k < kBins; ++k)
            for (int i = 0; i < kWin; ++i) {
                const double a = 2 * kPi * double((long(k) * i) % kWin) / kWin;
                // Re = Σ x cos, Im = −Σ x sin.
                forward[size_t(k) * kWin + size_t(i)] = float(std::cos(a));
                forward[size_t(kBins + k) * kWin + size_t(i)] = float(-std::sin(a));
                // irfft: the ends once, the rest twice (their mirror images),
                // the imaginary parts of the ends dropped.
                const double twice = k == 0 || k == kBins - 1 ? 1.0 : 2.0;
                inverse[size_t(i) * 2 * kBins + size_t(k)] = float(twice * std::cos(a) / kWin);
                inverse[size_t(i) * 2 * kBins + size_t(kBins + k)] =
                    k == 0 || k == kBins - 1 ? 0.f : float(-2.0 * std::sin(a) / kWin);
            }
    }
};
const Dft &dft() {
    static const Dft d;
    return d;
}

// What the overlapping windows add up to at each sample (the squares of
// the window, as torch.istft divides by), for n samples.
std::vector<float> envelope(size_t n) {
    const auto &w = dft().window;
    const long frames = long(n) / kHop + 1;
    std::vector<float> env(n);
    for (size_t s = 0; s < n; ++s) {
        // Frame t covers samples [t·hop − hop, t·hop + hop).
        const long t1 = (long(s) + kHop) / kHop, i1 = long(s) + kHop - t1 * kHop;
        float e = 0;
        if (t1 < frames)
            e += w[size_t(i1)] * w[size_t(i1)];
        if (t1 - 1 >= 0 && t1 - 1 < frames)
            e += w[size_t(i1 + kHop)] * w[size_t(i1 + kHop)];
        env[s] = e;
    }
    return env;
}

// RMSNorm over the `rows` of each column of x (row stride ld), times a
// weight per row, into out (row stride ldo; may be x).
void rmsNorm(const float *x, long ld, int rows, long cols, const float *weight, float *out, long ldo) {
    std::vector<float> sum(size_t(cols), 0.f);
    float *s = sum.data();
    for (int r = 0; r < rows; ++r) {
        const float *xr = x + r * ld;
        for (long c = 0; c < cols; ++c)
            s[c] += xr[c] * xr[c];
    }
    for (long c = 0; c < cols; ++c)
        s[c] = 1.0f / std::sqrt(s[c] / float(rows) + 1e-5f);
    for (int r = 0; r < rows; ++r) {
        const float *xr = x + r * ld;
        float *o = out + r * ldo;
        const float g = weight[r];
        for (long c = 0; c < cols; ++c)
            o[c] = xr[c] * s[c] * g;
    }
}

class Net {
public:
    Net(const Model &m, int threads, const std::atomic<bool> *cancel, std::atomic<float> *progress, const Probe &probe)
        : m(m), threads(threads), cancel(cancel), progressOut(progress), probe(probe) {}

    struct Cancelled {};

    // Frames [a, b) of one channel x (n samples) through the network, and
    // the sound of frames [k0, k1) among them overlap-added into y (n
    // samples, under `lock` if there is one), not yet divided by the
    // windows' envelope.
    void block(const float *x, size_t n, long a, long b, long k0, long k1, float *y, std::mutex *lock) {
        const long T = b - a;
        const long ld = long(kBands) * T;
        step(0);
        // The spectrum: [882 samples][T frames] windowed, through the transform.
        const Dft &d = dft();
        frames.assign(size_t(kWin) * size_t(T), 0.f);
        for (int i = 0; i < kWin; ++i)
            for (long t = 0; t < T; ++t) {
                long j = (a + t) * kHop - kHop + i;
                if (j < 0)
                    j = -j;
                if (j >= long(n))
                    j = 2 * (long(n) - 1) - j;
                frames[size_t(i) * size_t(T) + size_t(t)] = x[j] * d.window[size_t(i)];
            }
        spec.resize(size_t(2 * kBins) * size_t(T));
        stems::gemm(2 * kBins, int(T), kWin, d.forward.data(), kWin, false, frames.data(), T, false, spec.data(), T,
                    false, threads);

        // Each band normalised by its power, with the power's log beside
        // it, and brought to 256 features.
        X.resize(size_t(kDim) * size_t(ld));
        for (int band = 0; band < kBands; ++band) {
            const int w = bandWidth(band), s0 = bandStart(band), rows = 2 * w + 1;
            feat.resize(size_t(rows) * size_t(T));
            power.assign(size_t(T), 0.f);
            for (int r = 0; r < w; ++r) {
                const float *re = spec.data() + long(s0 + r) * T, *im = spec.data() + long(kBins + s0 + r) * T;
                for (long t = 0; t < T; ++t)
                    power[size_t(t)] += re[t] * re[t] + im[t] * im[t];
            }
            for (long t = 0; t < T; ++t)
                power[size_t(t)] = std::sqrt(power[size_t(t)] + 1.1920928955078125e-07f);
            for (int r = 0; r < w; ++r) {
                const float *re = spec.data() + long(s0 + r) * T, *im = spec.data() + long(kBins + s0 + r) * T;
                float *fr = feat.data() + long(r) * T, *fi = feat.data() + long(w + r) * T;
                for (long t = 0; t < T; ++t) {
                    fr[t] = re[t] / power[size_t(t)];
                    fi[t] = im[t] / power[size_t(t)];
                }
            }
            for (long t = 0; t < T; ++t)
                feat[size_t(2 * w) * size_t(T) + size_t(t)] = std::log(power[size_t(t)]);
            const std::string bn = "BN." + std::to_string(band);
            rmsNorm(feat.data(), T, rows, T, m[bn + ".0.weight"].data(), feat.data(), T);
            const Tensor &W = m[bn + ".1.weight"], &B = m[bn + ".1.bias"];
            float *out = X.data() + long(band) * T;
            for (int c = 0; c < kDim; ++c)
                std::fill(out + c * ld, out + c * ld + T, B.v[size_t(c)]);
            stems::gemm(kDim, int(T), rows, W.data(), rows, false, feat.data(), T, false, out, ld, true, threads);
        }
        show("features", T);

        for (int l = 0; l < kLayers; ++l) {
            acrossBands(l, T);
            alongTime(l, T);
            show(("layer" + std::to_string(l)).c_str(), T);
            step(float(l + 1) / (kLayers + 1));
        }

        // Each band's features back to its bins: real and imaginary parts,
        // gated (GLU).
        out.assign(size_t(2 * kBins) * size_t(T), 0.f);
        norm.resize(size_t(kDim) * size_t(T));
        for (int band = 0; band < kBands; ++band) {
            const int w = bandWidth(band), s0 = bandStart(band);
            const std::string o = "output." + std::to_string(band);
            rmsNorm(X.data() + long(band) * T, ld, kDim, T, m[o + ".0.weight"].data(), norm.data(), T);
            const Tensor &W = m[o + ".1.weight"], &B = m[o + ".1.bias"];
            gated.resize(size_t(4 * w) * size_t(T));
            for (int r = 0; r < 4 * w; ++r)
                std::fill(gated.data() + long(r) * T, gated.data() + long(r + 1) * T, B.v[size_t(r)]);
            stems::gemm(4 * w, int(T), kDim, W.data(), kDim, false, norm.data(), T, false, gated.data(), T, true,
                        threads);
            for (int r = 0; r < 2 * w; ++r) {
                const float *v = gated.data() + long(r) * T, *g = gated.data() + long(2 * w + r) * T;
                float *dst = out.data() + long(r < w ? s0 + r : kBins + s0 + (r - w)) * T;
                for (long t = 0; t < T; ++t)
                    dst[t] = v[t] * sigmoid(g[t]);
            }
        }
        if (probe)
            probe("spectrum", out.data(), {2 * kBins, int(T)});

        // Back to sound, the kept frames only.
        const long K = k1 - k0;
        sound.resize(size_t(kWin) * size_t(K));
        stems::gemm(kWin, int(K), 2 * kBins, d.inverse.data(), 2 * kBins, false, out.data() + (k0 - a), T, false,
                    sound.data(), K, false, threads);
        std::unique_lock<std::mutex> guard;
        if (lock)
            guard = std::unique_lock<std::mutex>(*lock);
        for (long t = 0; t < K; ++t) {
            const long start = (k0 + t) * kHop - kHop;
            for (int i = 0; i < kWin; ++i) {
                const long s = start + i;
                if (s >= 0 && s < long(n))
                    y[s] += sound[size_t(i) * size_t(K) + size_t(t)] * d.window[size_t(i)];
            }
        }
        step(1);
    }

private:
    void check() const {
        if (cancel && cancel->load(std::memory_order_relaxed))
            throw Cancelled();
    }
    void step(float done) {
        check();
        if (progressOut)
            progressOut->store(done, std::memory_order_relaxed);
    }
    // X as PyTorch has it: [bands][256][frames].
    void show(const char *name, long T) const {
        if (!probe)
            return;
        std::vector<float> v(size_t(kBands) * kDim * size_t(T));
        const long ld = long(kBands) * T;
        for (int band = 0; band < kBands; ++band)
            for (int c = 0; c < kDim; ++c)
                std::memcpy(v.data() + (long(band) * kDim + c) * T, X.data() + c * ld + long(band) * T,
                            sizeof(float) * size_t(T));
        probe(name, v.data(), {kBands, kDim, int(T)});
    }

    // The band transformer (a Roformer across the 80 bands of each frame,
    // rotary positions, eight heads of 32), on kGroup frames at a time.
    void acrossBands(int l, long T) {
        const std::string p = "net." + std::to_string(l) + ".band_net.";
        const Tensor &Wqkv = m[p + "weight.weight"], &Wout = m[p + "output.weight"];
        const Tensor &W1 = m[p + "MLP.1.weight"], &W2 = m[p + "MLP_output.weight"];
        const float *inNorm = m[p + "input_norm.weight"].data(), *mlpNorm = m[p + "MLP.0.weight"].data();
        const float *cosF = m[p + "cos_freq"].data(), *sinF = m[p + "sin_freq"].data();
        const long ld = long(kBands) * T;
        const float scale = 1.0f / std::sqrt(float(kHead));
        for (long g0 = 0; g0 < T; g0 += kGroup) {
            check();
            const long G = std::min<long>(kGroup, T - g0), N = G * kBands;
            // Gathered: column g·80 + band.
            xg.resize(size_t(kDim) * size_t(N));
            for (int c = 0; c < kDim; ++c)
                for (int band = 0; band < kBands; ++band) {
                    const float *src = X.data() + c * ld + long(band) * T + g0;
                    float *dst = xg.data() + c * N + band;
                    for (long g = 0; g < G; ++g)
                        dst[g * kBands] = src[g];
                }
            hn.resize(size_t(kDim) * size_t(N));
            rmsNorm(xg.data(), N, kDim, N, inNorm, hn.data(), N);
            qkv.resize(size_t(3 * kDim) * size_t(N));
            stems::gemm(3 * kDim, int(N), kDim, Wqkv.data(), kDim, false, hn.data(), N, false, qkv.data(), N, false,
                        threads);
            att.resize(size_t(kDim) * size_t(N));
            for (long g = 0; g < G; ++g)
                for (int h = 0; h < kHeads; ++h)
                    head(qkv.data() + long(h) * 3 * kHead * N + g * kBands, N, cosF, sinF, scale,
                         att.data() + long(h) * kHead * N + g * kBands);
            stems::gemm(kDim, int(N), kDim, Wout.data(), kDim, false, att.data(), N, false, xg.data(), N, true,
                        threads);
            // The gated MLP: silu, then halves multiplied with silu again on
            // the first (the network does both).
            rmsNorm(xg.data(), N, kDim, N, mlpNorm, hn.data(), N);
            mlp.resize(size_t(8 * kDim) * size_t(N));
            stems::gemm(8 * kDim, int(N), kDim, W1.data(), kDim, false, hn.data(), N, false, mlp.data(), N, false,
                        threads);
            const long half = long(4 * kDim) * N;
            float *mp = mlp.data();
            for (long i = 0; i < half; ++i)
                mp[i] = silu(silu(mp[i])) * silu(mp[half + i]);
            stems::gemm(kDim, int(N), 4 * kDim, W2.data(), 4 * kDim, false, mlp.data(), N, false, xg.data(), N, true,
                        threads);
            for (int c = 0; c < kDim; ++c)
                for (int band = 0; band < kBands; ++band) {
                    float *dst = X.data() + c * ld + long(band) * T + g0;
                    const float *src = xg.data() + c * N + band;
                    for (long g = 0; g < G; ++g)
                        dst[g] = src[g * kBands];
                }
        }
    }

    // One head's attention across one frame's 80 bands: q, k and v are
    // rows [0, 32), [32, 64), [64, 96) from `in` (row stride N), the 80
    // bands its columns; the result into `o`'s 32 rows.
    void head(const float *in, long N, const float *cosF, const float *sinF, float scale, float *o) {
        float q[kBands][kHead], k[kBands][kHead], v[kBands][kHead];
        for (int d = 0; d < kHead; ++d)
            for (int j = 0; j < kBands; ++j) {
                q[j][d] = in[long(d) * N + j];
                k[j][d] = in[long(kHead + d) * N + j];
                v[j][d] = in[long(2 * kHead + d) * N + j];
            }
        // Rotary positions: each pair (x0, x1) turned by its band's angle.
        for (int j = 0; j < kBands; ++j) {
            const float *c = cosF + j * kHead, *s = sinF + j * kHead;
            for (int d = 0; d < kHead; d += 2) {
                const float q0 = q[j][d], q1 = q[j][d + 1], k0 = k[j][d], k1 = k[j][d + 1];
                q[j][d] = q0 * c[d] - q1 * s[d];
                q[j][d + 1] = q1 * c[d + 1] + q0 * s[d + 1];
                k[j][d] = k0 * c[d] - k1 * s[d];
                k[j][d + 1] = k1 * c[d + 1] + k0 * s[d + 1];
            }
        }
        float w[kBands], acc[kHead];
        for (int i = 0; i < kBands; ++i) {
            float top = -3.0e38f;
            for (int j = 0; j < kBands; ++j) {
                float sdot = 0;
                for (int d = 0; d < kHead; ++d)
                    sdot += q[i][d] * k[j][d];
                w[j] = sdot * scale;
                top = std::max(top, w[j]);
            }
            float sum = 0;
            for (int j = 0; j < kBands; ++j) {
                w[j] = fastExp(w[j] - top);
                sum += w[j];
            }
            std::fill(acc, acc + kHead, 0.f);
            for (int j = 0; j < kBands; ++j)
                for (int d = 0; d < kHead; ++d)
                    acc[d] += w[j] * v[j][d];
            const float inv = 1.0f / sum;
            for (int d = 0; d < kHead; ++d)
                o[long(d) * N + i] = acc[d] * inv;
        }
    }

    // The sequence net: for each band, three residual blocks of a 7-frame
    // depthwise convolution, RMSNorm, and a pointwise MLP with SiLU.
    void alongTime(int l, long T) {
        const long ld = long(kBands) * T;
        hid.resize(size_t(4 * kDim) * size_t(T));
        dw.resize(size_t(kDim) * size_t(T));
        for (int band = 0; band < kBands; ++band) {
            check();
            float *xb = X.data() + long(band) * T;
            for (int k = 0; k < kBlocks; ++k) {
                const std::string c = "net." + std::to_string(l) + ".seq_net.blocks." + std::to_string(k) + ".conv.";
                const Tensor &Wd = m[c + "0.weight"], &Bd = m[c + "0.bias"];
                for (int ch = 0; ch < kDim; ++ch) {
                    const float *src = xb + ch * ld, *wk = Wd.data() + ch * 7;
                    float *dst = dw.data() + long(ch) * T;
                    std::fill(dst, dst + T, Bd.v[size_t(ch)]);
                    for (int j = 0; j < 7; ++j) {
                        const long off = j - 3;
                        const long t0 = std::max<long>(0, -off), t1 = std::min<long>(T, T - off);
                        const float wj = wk[j];
                        for (long t = t0; t < t1; ++t)
                            dst[t] += wj * src[t + off];
                    }
                }
                rmsNorm(dw.data(), T, kDim, T, m[c + "1.weight"].data(), dw.data(), T);
                const Tensor &W2 = m[c + "2.weight"], &B2 = m[c + "2.bias"];
                for (int r = 0; r < 4 * kDim; ++r)
                    std::fill(hid.data() + long(r) * T, hid.data() + long(r + 1) * T, B2.v[size_t(r)]);
                stems::gemm(4 * kDim, int(T), kDim, W2.data(), kDim, false, dw.data(), T, false, hid.data(), T, true,
                            threads);
                float *hp = hid.data();
                for (long i = 0, e = long(4 * kDim) * T; i < e; ++i)
                    hp[i] = silu(hp[i]);
                const Tensor &W4 = m[c + "4.weight"], &B4 = m[c + "4.bias"];
                for (int ch = 0; ch < kDim; ++ch) {
                    float *r = xb + ch * ld;
                    const float bias = B4.v[size_t(ch)];
                    for (long t = 0; t < T; ++t)
                        r[t] += bias;
                }
                stems::gemm(kDim, int(T), 4 * kDim, W4.data(), 4 * kDim, false, hid.data(), T, false, xb, ld, true,
                            threads);
            }
        }
    }

    const Model &m;
    const int threads;
    const std::atomic<bool> *cancel;
    std::atomic<float> *progressOut;
    const Probe probe;
    // Kept from block to block, so a worker asks for its memory once.
    std::vector<float> frames, spec, X, feat, power, out, norm, gated, sound, xg, hn, qkv, att, mlp, hid, dw;
};

// A worker of a long restore keeps out of the way of the sound and the
// screen: the lowest priority the system gives without asking.
void yieldToTheStudio() {
#if defined(__linux__)
    setpriority(PRIO_PROCESS, pid_t(syscall(SYS_gettid)), 19);
#elif defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
}

} // namespace

void whole(const Model &model, const float *in, size_t n, float *out, const Probe &probe) {
    std::fill(out, out + n, 0.f);
    if (n < size_t(2 * kHop)) {
        std::copy(in, in + n, out);
        return;
    }
    Net net(model, 1, nullptr, nullptr, probe);
    const long frames = long(n) / kHop + 1;
    net.block(in, n, 0, frames, 0, frames, out, nullptr);
    const std::vector<float> env = envelope(n);
    for (size_t i = 0; i < n; ++i)
        out[i] /= env[i];
}

// ---------------------------------------------------------------------------
// A song: both channels cut into blocks of frames, each restored on a core
// of its own, and put back together.

bool restore44(const Model &model, const float *const in[2], size_t n, std::vector<float> out[2],
               const Options &options, std::string *error) {
    auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    if (n == 0)
        return fail("The song is empty");
    for (int c = 0; c < 2; ++c)
        out[c].assign(n, 0.f);
    if (n < size_t(2 * kHop)) { // shorter than a frame: nothing to work with
        for (int c = 0; c < 2; ++c)
            std::copy(in[c], in[c] + n, out[c].begin());
        return true;
    }
    const long frames = long(n) / kHop + 1;
    const long keep = options.block > 0 ? options.block : 512;
    struct Unit {
        int channel;
        long k0, k1;
    };
    std::vector<Unit> units;
    for (int c = 0; c < 2; ++c)
        for (long k0 = 0; k0 < frames; k0 += keep)
            units.push_back({c, k0, std::min(frames, k0 + keep)});

    const int threads = options.threads > 0 ? options.threads : stems::fastCoreCount();
    // A worker needs about 0.1 GB at the usual block size; half the memory
    // is ours to take.
    const double perWorker = 0.1 * double(keep + 2 * kMargin) / (512 + 2 * kMargin);
    const int affordable = std::max(1, int((stems::physicalMemoryGigabytes() / 2 - 0.5) / perWorker));
    const int workers = std::max(1, std::min({threads, int(units.size()), affordable}));
    const int inner = std::max(1, threads / workers);

    std::atomic<int> next{0}, done{0};
    std::atomic<bool> stop{false}, failed{false};
    std::vector<std::atomic<float>> partial(static_cast<size_t>(workers));
    for (auto &p : partial)
        p = 0;
    std::mutex adding;
    std::string why;
    auto work = [&](int me) {
        stems::keepToFastCores();
        if (options.background)
            yieldToTheStudio();
        try {
            Net net(model, inner, &stop, &partial[size_t(me)], {});
            for (int k; !stop && (k = next.fetch_add(1)) < int(units.size());) {
                const Unit &u = units[size_t(k)];
                const long a = std::max<long>(0, u.k0 - kMargin), b = std::min(frames, u.k1 + kMargin);
                net.block(in[u.channel], n, a, b, u.k0, u.k1, out[u.channel].data(), &adding);
                partial[size_t(me)] = 0;
                ++done;
            }
        } catch (const Net::Cancelled &) {
        } catch (const std::bad_alloc &) {
            std::lock_guard lock(adding);
            why = "The computer ran out of memory while restoring the song";
            failed = true;
            stop = true;
        } catch (const std::exception &) {
            std::lock_guard lock(adding);
            why = "The restore model's file is damaged";
            failed = true;
            stop = true;
        }
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < workers; ++i)
        pool.emplace_back(work, i);
    float shown = 0;
    while (done < int(units.size()) && !failed) {
        if (options.cancel && options.cancel->load())
            stop = true;
        if (stop)
            break;
        float now = float(done.load());
        for (auto &p : partial)
            now += p.load();
        shown = std::max(shown, std::min(0.999f, now / float(units.size())));
        if (options.progress)
            options.progress(shown);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto &t : pool)
        t.join();
    if (failed)
        return fail(why.c_str());
    if (done < int(units.size()))
        return fail("Restoring called off");
    const std::vector<float> env = envelope(n);
    for (int c = 0; c < 2; ++c)
        for (size_t i = 0; i < n; ++i)
            out[c][i] /= env[i];
    if (options.progress)
        options.progress(1.0f);
    return true;
}

bool restore(const Model &model, const int16_t *mix, size_t frames, int rate, std::vector<int16_t> &out,
             const Options &options, std::string *error) {
    std::vector<float> ch[2];
    for (int c = 0; c < 2; ++c) {
        ch[c].resize(frames);
        for (size_t i = 0; i < frames; ++i)
            ch[c][i] = float(mix[2 * i + size_t(c)]) * (1.0f / 32768.0f);
    }
    std::vector<float> at44[2];
    for (int c = 0; c < 2; ++c)
        at44[c] = stems::resample(ch[c].data(), frames, rate, kRate);
    const float *in[2] = {at44[0].data(), at44[1].data()};
    std::vector<float> fixed[2];
    if (!restore44(model, in, at44[0].size(), fixed, options, error))
        return false;
    std::vector<float> back[2];
    float peak = 0;
    for (int c = 0; c < 2; ++c) {
        back[c] = stems::resample(fixed[c].data(), fixed[c].size(), kRate, rate);
        back[c].resize(frames, 0.f);
        for (float v : back[c])
            peak = std::max(peak, std::fabs(v));
    }
    // Drawn-back peaks can rise past full scale: then the whole song comes
    // down a little rather than clip.
    const float gain = peak > 0.999f ? 0.999f / peak : 1.0f;
    out.resize(2 * frames);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c)
            out[2 * i + size_t(c)] =
                int16_t(std::lrint(std::clamp(back[c][i] * gain * 32768.0f, -32768.0f, 32767.0f)));
    return true;
}

bool lossyName(const std::string &fileName) {
    const size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = fileName.substr(dot + 1);
    for (char &c : ext)
        c = char(std::tolower(uint8_t(c)));
    static const char *lossy[] = {"mp3", "mp2", "m4a", "aac", "mp4", "m4v", "mov", "ogg", "oga", "opus",
                                  "webm", "weba", "wma", "wmv", "amr", "3gp", "ac3", "mka", "mkv", "flv"};
    for (const char *e : lossy)
        if (ext == e)
            return true;
    return false;
}

} // namespace tf::restore
