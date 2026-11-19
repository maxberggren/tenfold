#pragma once

// A 24-bit stereo WAV writer with TPDF dither, for recordings and offline
// renders. The header is rewritten with the real length on close.

#include <cstdint>
#include <cstdio>
#include <string>

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

}
