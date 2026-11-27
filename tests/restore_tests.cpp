// The restorer (restore.h): which files it takes for songs that went
// through a lossy codec; a file that isn't the model; and, when the
// weights are on this machine, the network against numbers PyTorch's
// Apollo gave for the same sound, a song restored in blocks against in one
// piece, a sound cut off at 16 kHz given its top back, and calling it off.
//
// That the network itself is Apollo's was checked stage by stage against
// PyTorch when it was written: on sound with something in every band,
// every layer within 3e-6 of PyTorch's (relative RMS) and the output at
// 121 dB SNR. (On an MP3's empty top bands the two differ at 45 dB: there
// both transforms hand the network only their own rounding.)

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <vector>

#include "restore.h"
#include "stems.h"
#include "testkit.h"

using namespace tf;

namespace {

QString weights() {
    QStringList paths = {qEnvironmentVariable("TENFOLD_RESTORE_MODEL"),
                         QStringLiteral(TENFOLD_SRC "/../build/apollo.bin"),
                         QDir::homePath() + QStringLiteral("/.cache/huggingface/hub/models--JusperLee--Apollo/"
                                                           "snapshots/c68bd80fdd9c0d93d2f4a833cb154624f660a561/"
                                                           "pytorch_model.bin")};
    for (const QString &p : paths)
        if (!p.isEmpty() && QFile::exists(p))
            return p;
    return {};
}

// The noise the PyTorch reference was made with: a 32-bit LCG.
std::vector<double> lcgNoise(size_t n, uint32_t seed) {
    std::vector<double> out(n);
    uint32_t s = seed;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        out[i] = double(s >> 8) / double(1 << 24) * 2.0 - 1.0;
    }
    return out;
}

// Two tones and white noise to Nyquist: no band of it is near silence.
std::vector<float> everything(size_t n) {
    const auto noise = lcgNoise(n, 12345);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / restore::kRate;
        x[i] = float(0.3 * std::sin(2 * M_PI * 440 * t) + 0.2 * std::sin(2 * M_PI * 5000.5 * t) + 0.05 * noise[i]);
    }
    return x;
}

// The share of x's amplitude (as RMS) above `from` Hz, in 8192 Hann-
// windowed samples from its middle, by a plain DFT.
double shareAbove(const std::vector<float> &x, int rate, double from) {
    constexpr size_t N = 8192;
    const size_t at = (x.size() - N) / 2;
    std::vector<double> w(N);
    for (size_t i = 0; i < N; ++i)
        w[i] = x[at + i] * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / N));
    double above = 0, all = 0;
    for (size_t k = 1; k < N / 2; ++k) {
        const std::complex<double> step = std::polar(1.0, -2 * M_PI * double(k) / N);
        std::complex<double> turn = 1, s = 0;
        for (size_t i = 0; i < N; ++i, turn *= step)
            s += w[i] * turn;
        all += std::norm(s);
        if (double(k) * rate / N > from)
            above += std::norm(s);
    }
    return std::sqrt(above / all);
}

} // namespace

class RestoreTests : public QObject {
    Q_OBJECT
    // Restoring takes every fast core, which the tests that time the
    // studio would feel: these run on their own, after the rest.
    Q_CLASSINFO("alone", "theNetworkIsApollos aSongRestoredInBlocksIsTheSameAsInOnePiece "
                         "aSoundCutOffAt16kHzGetsItsTopBack restoringStopsWhenCalledOff")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "lossyFilesAreKnownByTheirEnding aFileThatIsNotTheModelIsSaidToBe")
private slots:
    void lossyFilesAreKnownByTheirEnding() {
        for (const char *name : {"song.mp3", "/a/b/Song.MP3", "x.m4a", "clip.webm", "voice.opus", "y.ogg", "v.mp4"})
            QVERIFY2(restore::lossyName(name), name);
        for (const char *name : {"take.wav", "album.flac", "a.aiff", "noext", "dir.mp3/file", "x.mp3.wav"})
            QVERIFY2(!restore::lossyName(name), name);
    }

    void aFileThatIsNotTheModelIsSaidToBe() {
        std::string why;
        QVERIFY(!restore::load("/no/such/apollo.bin", &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("isn't there")), why.c_str());
        QTemporaryDir dir;
        // Not a zip; a zip with no pickle in it; the stem model's kind of file.
        const QByteArray bodies[] = {QByteArray(1000, 'x'),
                                     QByteArray("PK\x05\x06", 4) + QByteArray(18, '\0'),
                                     QByteArray("\x10\x00\x00\x00\x00\x00\x00\x00{\"a\":{}}      ", 24)};
        for (const QByteArray &body : bodies) {
            QFile f(dir.filePath(QStringLiteral("apollo.bin")));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(body);
            f.close();
            why.clear();
            QVERIFY(!restore::load(f.fileName().toStdString(), &why));
            QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("damaged")), why.c_str());
        }
    }

    void theNetworkIsApollos() {
        const QString path = weights();
        if (path.isEmpty())
            QSKIP("no apollo.bin here (bin/model fetches it; TENFOLD_RESTORE_MODEL names one)");
        std::string why;
        const auto model = restore::load(path.toStdString(), &why);
        QVERIFY2(model, why.c_str());
        // One second through the network in one piece; PyTorch's Apollo
        // (JusperLee/Apollo at c68bd80) gave these for the same sound.
        const std::vector<float> x = everything(restore::kRate);
        std::vector<float> y(x.size());
        restore::whole(*model, x.data(), x.size(), y.data());
        double s = 0;
        for (float v : y)
            s += double(v) * v;
        const double rms = std::sqrt(s / double(y.size()));
        qInfo("RMS %.7f (PyTorch 0.2997876)", rms);
        QVERIFY(std::fabs(rms - 0.2997876) < 3e-5);
        const std::pair<int, double> at[] = {{1000, 0.0635954}, {12345, 0.1931628}, {30000, 0.1914947}, {44099, 0.0051441}};
        for (const auto &[i, want] : at)
            QVERIFY2(std::fabs(y[size_t(i)] - want) < 3e-5,
                     qPrintable(QStringLiteral("y[%1] = %2, PyTorch %3").arg(i).arg(y[size_t(i)], 0, 'f', 7).arg(want, 0, 'f', 7)));
    }

    void aSongRestoredInBlocksIsTheSameAsInOnePiece() {
        const QString path = weights();
        if (path.isEmpty())
            QSKIP("no apollo.bin here");
        std::string why;
        const auto model = restore::load(path.toStdString(), &why);
        QVERIFY(model);
        // 1.5 s, the two channels different: in blocks of 37 frames (each
        // with its 56 of context either side) and in one block.
        const size_t n = size_t(1.5 * restore::kRate);
        std::vector<float> left = everything(n), right(n);
        for (size_t i = 0; i < n; ++i)
            right[i] = 0.7f * left[(i * 3) % n];
        const float *in[2] = {left.data(), right.data()};
        std::vector<float> small[2], one[2];
        restore::Options options;
        options.block = 37;
        QVERIFY2(restore::restore44(*model, in, n, small, options, &why), why.c_str());
        options.block = 100000;
        QVERIFY2(restore::restore44(*model, in, n, one, options, &why), why.c_str());
        for (int c = 0; c < 2; ++c) {
            QCOMPARE(small[c].size(), n);
            double err = 0, sig = 0;
            for (size_t i = 0; i < n; ++i) {
                QVERIFY(std::isfinite(small[c][i]));
                err += double(small[c][i] - one[c][i]) * (small[c][i] - one[c][i]);
                sig += double(one[c][i]) * one[c][i];
            }
            qInfo("channel %d: blocks against one piece at %.1f dB", c, 10 * std::log10(sig / std::max(err, 1e-30)));
            QVERIFY(err < sig * 1e-10); // 100 dB: the same, but for float rounding
        }
        // The two channels are each their own.
        double diff = 0;
        for (size_t i = 0; i < n; ++i)
            diff += std::fabs(one[0][i] - one[1][i]);
        QVERIFY(diff > 1.0);
    }

    void aSoundCutOffAt16kHzGetsItsTopBack() {
        const QString path = weights();
        if (path.isEmpty())
            QSKIP("no apollo.bin here");
        std::string why;
        const auto model = restore::load(path.toStdString(), &why);
        QVERIFY(model);
        // Two seconds as an MP3 leaves them: a bright tone (harmonics of
        // 220 Hz falling as 1/k) and noise hits four times a second, with
        // nothing above 15.3 kHz (through 32 kHz and back); as 16-bit stereo
        // at 48 kHz, as the Sampler has songs.
        const int rate = 48000, frames = 2 * rate;
        const auto noise = lcgNoise(size_t(frames), 777);
        std::vector<float> full(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            const double t = double(i) / rate;
            double v = 0;
            for (int k = 1; k <= 72; ++k) // 72 × 220 = 15840 Hz
                v += 0.25 / k * std::sin(2 * M_PI * 220 * k * t);
            const int since = i % (rate / 4);
            if (since < 4000)
                v += 0.4 * std::exp(-since / 650.0) * noise[size_t(i)];
            full[size_t(i)] = float(v);
        }
        const std::vector<float> low = stems::resample(full.data(), full.size(), rate, 32000);
        std::vector<float> before = stems::resample(low.data(), low.size(), 32000, rate);
        before.resize(size_t(frames));
        std::vector<int16_t> mix(size_t(frames) * 2);
        for (int i = 0; i < frames; ++i) {
            mix[size_t(2 * i)] = mix[size_t(2 * i + 1)] = int16_t(std::lrint(std::clamp(before[size_t(i)], -1.0f, 1.0f) * 30000));
            before[size_t(i)] = mix[size_t(2 * i)] / 32768.0f;
        }
        std::vector<int16_t> out;
        restore::Options options;
        const auto t0 = std::chrono::steady_clock::now();
        QVERIFY2(restore::restore(*model, mix.data(), size_t(frames), rate, out, options, &why), why.c_str());
        qInfo("2 s of stereo restored in %.1f s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        QCOMPARE(out.size(), mix.size());
        std::vector<float> after(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i)
            after[size_t(i)] = out[size_t(2 * i)] / 32768.0f;
        // Before, above 16.5 kHz there is only the 16-bit rounding;
        // restored, there is far more.
        const double was = shareAbove(before, rate, 16500), is = shareAbove(after, rate, 16500);
        qInfo("above 16.5 kHz: %.4f of the level before, %.4f after", was, is);
        QVERIFY(is > 0.004);
        QVERIFY(is > 3 * was);
        QVERIFY(is < 0.3);
    }

    void restoringStopsWhenCalledOff() {
        const QString path = weights();
        if (path.isEmpty())
            QSKIP("no apollo.bin here");
        std::string why;
        const auto model = restore::load(path.toStdString(), &why);
        QVERIFY(model);
        std::vector<int16_t> mix(size_t(60 * 48000) * 2, 0);
        for (size_t i = 0; i < mix.size(); ++i)
            mix[i] = int16_t((i * 7919) % 2000) - 1000;
        std::atomic<bool> cancel{false};
        restore::Options options;
        options.cancel = &cancel;
        options.progress = [&](float p) {
            if (p > 0.02f)
                cancel = true;
        };
        std::vector<int16_t> out;
        const auto t0 = std::chrono::steady_clock::now();
        QVERIFY(!restore::restore(*model, mix.data(), mix.size() / 2, 48000, out, options, &why));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        qInfo("called off after %.1f s: %s", took, why.c_str());
        QCOMPARE(QString::fromStdString(why), QStringLiteral("Restoring called off"));
        QVERIFY(took < 30);
    }
};

TESTKIT_REGISTER(RestoreTests)
#include "restore_tests.moc"
