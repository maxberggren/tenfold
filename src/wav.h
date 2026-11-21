#pragma once

// A 24-bit stereo WAV writer with TPDF dither, for recordings and offline
// renders. The header is rewritten with the real length on close.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dsp.h"

namespace tf {

class WavWriter {
public:
    ~WavWriter() { close(); }

    bool open(const std::string &path, int rate) {
        m_file = std::fopen(path.c_str(), "wb");
        if (!m_file)
            return false;
        m_rate = rate;
        m_frames = 0;
        writeHeader();
        return true;
    }

    void write(const float *interleaved, size_t frames) {
        if (!m_file)
            return;
        uint8_t buf[6 * 256];
        size_t done = 0;
        while (done < frames) {
            const size_t n = std::min<size_t>(256, frames - done);
            for (size_t i = 0; i < n * 2; ++i) {
                // Triangular dither at one 24-bit step: free of distortion on
                // the quietest reverb tails.
                const float dither = (m_rng.unit() - m_rng.unit()) / 8388608.0f;
                const float v = clampf(interleaved[done * 2 + i] + dither, -1.0f, 1.0f);
                const int32_t s = int32_t(std::lrint(v * 8388607.0f));
                buf[i * 3] = uint8_t(s & 0xff);
                buf[i * 3 + 1] = uint8_t((s >> 8) & 0xff);
                buf[i * 3 + 2] = uint8_t((s >> 16) & 0xff);
            }
            std::fwrite(buf, 1, n * 6, m_file);
            done += n;
        }
        m_frames += frames;
    }

    bool close() {
        if (!m_file)
            return true;
        std::fseek(m_file, 0, SEEK_SET);
        writeHeader();
        const bool ok = std::fclose(m_file) == 0;
        m_file = nullptr;
        return ok;
    }

    size_t frames() const { return m_frames; }

private:
    void put32(uint32_t v) { std::fwrite(&v, 4, 1, m_file); }
    void put16(uint16_t v) { std::fwrite(&v, 2, 1, m_file); }
    void writeHeader() {
        const uint32_t data = uint32_t(m_frames * 6);
        std::fwrite("RIFF", 1, 4, m_file);
        put32(36 + data);
        std::fwrite("WAVEfmt ", 1, 8, m_file);
        put32(16);
        put16(1);
        put16(2);
        put32(uint32_t(m_rate));
        put32(uint32_t(m_rate) * 6);
        put16(6);
        put16(24);
        std::fwrite("data", 1, 4, m_file);
        put32(data);
    }

    FILE *m_file = nullptr;
    int m_rate = 48000;
    size_t m_frames = 0;
    Rng m_rng{0xD17E};
};

// Reads a WAV (16- or 24-bit PCM, or 32-bit float; any channels, mixed
// to mono) at `rate`, resampling linearly if it was made at another. For
// feeding a script's microphone. Empty on failure.
inline std::vector<float> readWavMono(const std::string &path, int rate) {
    std::vector<float> out;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return out;
    auto u32 = [](const uint8_t *b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; };
    uint8_t head[12];
    if (std::fread(head, 1, 12, f) != 12 || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4)) {
        std::fclose(f);
        return out;
    }
    int format = 0, channels = 0, bits = 0, fileRate = 0;
    std::vector<uint8_t> data;
    uint8_t ch[8];
    while (std::fread(ch, 1, 8, f) == 8) {
        const uint32_t size = u32(ch + 4);
        std::vector<uint8_t> body(size);
        if (size && std::fread(body.data(), 1, size, f) != size)
            break;
        if (size & 1)
            std::fgetc(f);
        if (!std::memcmp(ch, "fmt ", 4) && size >= 16) {
            format = body[0] | body[1] << 8;
            channels = body[2] | body[3] << 8;
            fileRate = int(u32(body.data() + 4));
            bits = body[14] | body[15] << 8;
            if (format == 0xFFFE && size >= 26)
                format = body[24] | body[25] << 8; // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
        } else if (!std::memcmp(ch, "data", 4)) {
            data = std::move(body);
        }
    }
    std::fclose(f);
    if (channels <= 0 || fileRate <= 0 || !((format == 1 && (bits == 16 || bits == 24)) || (format == 3 && bits == 32)))
        return out;
    const size_t bytes = size_t(bits / 8), frames = data.size() / (bytes * size_t(channels));
    std::vector<float> mono(frames);
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0;
        for (int c = 0; c < channels; ++c) {
            const uint8_t *b = data.data() + (i * size_t(channels) + size_t(c)) * bytes;
            float v = 0;
            if (format == 3)
                std::memcpy(&v, b, 4);
            else if (bits == 16)
                v = float(int16_t(uint16_t(b[0] | b[1] << 8))) / 32768.0f;
            else
                v = float(int32_t(uint32_t(b[0]) << 8 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 24) >> 8) / 8388608.0f;
            sum += v;
        }
        mono[i] = sum / float(channels);
    }
    if (fileRate == rate)
        return mono;
    const double step = double(fileRate) / rate;
    out.resize(size_t(double(frames) / step));
    for (size_t i = 0; i < out.size(); ++i) {
        const double x = double(i) * step;
        const size_t a = size_t(x);
        const float u = float(x - double(a));
        out[i] = a + 1 < frames ? mono[a] + (mono[a + 1] - mono[a]) * u : mono[std::min(a, frames - 1)];
    }
    return out;
}

}
