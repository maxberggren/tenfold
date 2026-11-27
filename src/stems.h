#pragma once

// Splitting a song into drums, bass, vocals and the rest, on the CPU and
// with nothing but tenfold itself: the Hybrid Transformer Demucs network
// (htdemucs, Défossez and Rouard, Meta, MIT licence) computed here in C++
// from its published weights (adefossez/HTDemucs on Hugging Face).
// Tenfold's packages carry them in a compact form, half the size, whose
// stems differ from the published weights' by 45 to 56 dB (see compact()
// and stems.cpp); the published file reads too.
//
// The network takes 7.8 seconds of 44.1 kHz stereo at a time. A song is
// cut into such segments overlapping by a quarter, each runs on a core of
// its own, and the overlaps are cross-faded, exactly as Demucs's own
// apply_model() does; from the published weights the result matches
// PyTorch's to within float rounding.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tf::stems {

// The network's weights, read from their file and shared by every worker.
struct Model;

// Reads the weights: tenfold's compact file (htdemucs.tfq), or htdemucs's
// safetensors file as Hugging Face has it. nullptr, and the reason in
// *error, if the file is missing or isn't either.
std::shared_ptr<const Model> load(const std::string &path, std::string *error);

// Writes the compact file from either: bits(name) is how finely each
// convolution or linear layer's weights are kept, the step 2^-bits of its
// channels' RMS (0 keeps them as halves). See stems.cpp.
bool compact(const std::string &from, const std::string &to, const std::function<double(const std::string &)> &bits,
             std::string *error);

// The network's sources, in its own order.
enum Source : int { Drums = 0, Bass, Other, Vocals, kSources };
inline const char *sourceName(int s) {
    static const char *names[] = {"drums", "bass", "other", "vocals"};
    return names[s];
}

struct Options {
    int threads = 0;                          // 0: one per fast core
    const std::atomic<bool> *cancel = nullptr; // set: stop as soon as possible
    // How far it is, 0..1, about ten times a second, from the thread that
    // called separate().
    std::function<void(float)> progress;
    // Samples between the segments' starts at 44.1 kHz; 0 chooses (a
    // quarter's overlap, stretched a little so that the cores finish
    // together).
    int stride = 0;
};

// The four stems of 16-bit stereo `mix` (interleaved, `frames` long, at
// `rate`), each as long as the mix and at its rate, in out[Drums..Vocals].
// False if it was called off or failed; *error then says why in words for
// the player.
bool separate(const Model &model, const int16_t *mix, size_t frames, int rate, std::vector<int16_t> out[kSources],
              const Options &options, std::string *error);

// The same at the network's own rate, on floats: in[2] channels of n
// samples at 44.1 kHz, out[source][channel] of n samples.
bool separate44(const Model &model, const float *const in[2], size_t n, std::vector<float> out[kSources][2],
                const Options &options, std::string *error);

// For checking the port against PyTorch: one segment (2 × kSegment
// samples, already normalised as separate44 does) through the network into
// out (4 × 2 × kSegment), with each layer's output handed to probe.
constexpr int kRate = 44100;
constexpr int kSegment = 343980; // 7.8 s, what the network was trained on
using Probe = std::function<void(const char *name, const float *data, const std::vector<int> &shape)>;
void segment(const Model &model, const float *in, float *out, const Probe &probe = {});

// One channel from one sample rate to another, with a Kaiser-windowed sinc
// (100 dB down from 95% of the lower rate's Nyquist frequency), centred so
// nothing is delayed. Returns ceil(n · to / from) samples.
std::vector<float> resample(const float *in, size_t n, int from, int to);

// Each tensor of two models side by side, for the compact file's tests.
void forEachWeight(const Model &a, const Model &b,
                   const std::function<void(const std::string &name, const std::vector<int> &shape, const float *x,
                                            const float *y, size_t n)> &fn);

// Matrix product for the network, exposed for its tests: C (M×N, row
// stride ldc) = A·B, plus C itself if `accumulate`. A is M×K (element (m,k)
// at A[m·lda + k], or A[k·lda + m] when transA), B is K×N likewise.
void gemm(int M, int N, int K, const float *A, long lda, bool transA, const float *B, long ldb, bool transB, float *C,
          long ldc, bool accumulate, int threads = 1);

} // namespace tf::stems
