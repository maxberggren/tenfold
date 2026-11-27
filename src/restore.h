#pragma once

// Restoring a song that went through a lossy codec (an MP3, a stream's
// AAC or Opus): the top octave the encoder threw away is drawn back, and
// its smeared, swirling artefacts are cleaned up. On the CPU and with
// nothing but tenfold itself: Apollo (Kai Li and Yi Luo, "Apollo:
// Band-sequence Modeling for High-Quality Audio Restoration", 2024; weights
// CC BY-SA 4.0, extras/LICENSE-apollo) computed here in C++ from its
// published weights, written from the paper and the shapes of those
// weights. The packages carry them in tenfold's compact form (apollo.tfq,
// 12.6 MB where the published file is 66), which restores the same to
// within 0.01 dB (see extras/apollo-bits.txt); the published file,
// PyTorch's own (JusperLee/Apollo), is read as it is too: a zip of raw
// tensors and a pickle naming them, of which only plain data is taken.
//
// The network works on 20 ms frames, 10 ms apart, of each channel alone,
// split into 80 bands. Within a frame its attention runs across the bands;
// along time it only has short convolutions, which see 54 frames either
// way. So a song is cut into blocks of frames with 56 more on each side,
// each block runs on a core of its own, and the result is the network's
// on the whole song, to within float rounding.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tf::restore {

// The network's weights, read from their file and shared by every worker.
struct Model;

// Reads the weights: tenfold's compact file (apollo.tfq), or PyTorch's
// file as Hugging Face has it. nullptr, and the reason in *error, if the
// file is missing or is neither.
std::shared_ptr<const Model> load(const std::string &path, std::string *error);

// Writes the compact file from either: bits(name) is how finely each
// convolution's weights are kept, the step 2^-bits of each output channel's
// RMS (0: as published, as are the norms, biases and rotary tables).
bool compact(const std::string &from, const std::string &to, const std::function<double(const std::string &)> &bits,
             std::string *error);

// Each tensor of two models side by side, for the compact file's tests.
void forEachWeight(const Model &a, const Model &b,
                   const std::function<void(const std::string &name, const std::vector<int> &shape, const float *x,
                                            const float *y, size_t n)> &fn);

constexpr int kRate = 44100; // the network's own
constexpr int kHop = 441;    // samples between frames
constexpr int kMargin = 56;  // frames of context a block takes either side

struct Options {
    int threads = 0;                           // 0: one per fast core
    const std::atomic<bool> *cancel = nullptr; // set: stop as soon as possible
    // How far it is, 0..1, about ten times a second, from the thread that
    // called restore().
    std::function<void(float)> progress;
    // Frames a block keeps; 0 chooses. Any size gives the same result.
    int block = 0;
    // Run at the lowest priority, out of the way of the sound and the
    // screen: for a restore that goes on while the studio plays.
    bool background = false;
};

// 16-bit stereo `mix` (interleaved, `frames` long, at `rate`) restored,
// into `out`, as long as the mix and at its rate. False if it was called
// off or failed; *error then says why in words for the player.
bool restore(const Model &model, const int16_t *mix, size_t frames, int rate, std::vector<int16_t> &out,
             const Options &options, std::string *error);

// The same at the network's own rate, on floats: in[2] channels of n
// samples at 44.1 kHz into out[2].
bool restore44(const Model &model, const float *const in[2], size_t n, std::vector<float> out[2],
               const Options &options, std::string *error);

// For checking the port against PyTorch: one channel of n samples through
// the network in one piece, as Apollo.forward takes it, into out (n).
// Each stage's output goes to probe.
using Probe = std::function<void(const char *name, const float *data, const std::vector<int> &shape)>;
void whole(const Model &model, const float *in, size_t n, float *out, const Probe &probe = {});

// Whether a file's name says its sound went through a lossy codec, the
// kind of song restoring is for.
bool lossyName(const std::string &fileName);

} // namespace tf::restore
