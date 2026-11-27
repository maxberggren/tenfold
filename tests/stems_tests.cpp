// The stem separator (stems.h): its matrix product against the plain sum,
// alone and split between threads; its resampler; a file that isn't the
// model; and, when the weights are on this machine, the compact file and a
// song separated.
//
// That the network itself is Demucs's was checked layer by layer against
// PyTorch's htdemucs (Demucs 4.1.0) when it was written: every layer within
// 1e-5 of PyTorch's (relative RMS), a whole song at 115 dB SNR.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

#include "stems.h"
#include "testkit.h"

using namespace tf;

namespace {

// The published weights, when this machine has them.
QString published() {
    for (const QString &p :
         {QStringLiteral(TENFOLD_SRC "/../build/htdemucs.safetensors"),
          QDir::homePath() + QStringLiteral("/.cache/huggingface/hub/models--adefossez--HTDemucs/snapshots/"
                                            "cbc8a9b1a87023b7fd74e7b3412e6321c0eab003/955717e8.safetensors")})
        if (QFile::exists(p))
            return p;
    return {};
}

// Where the weights may be: as the app looks for them, plus the build's.
QString weights() {
    QStringList paths = {qEnvironmentVariable("TENFOLD_STEMS_MODEL"),
                         QStringLiteral(TENFOLD_SRC "/../build/htdemucs.tfq"),
                         QStringLiteral(TENFOLD_SRC "/../build/htdemucs.safetensors"),
                         QDir::homePath() + QStringLiteral("/.cache/huggingface/hub/models--adefossez--HTDemucs/"
                                                           "snapshots/cbc8a9b1a87023b7fd74e7b3412e6321c0eab003/"
                                                           "955717e8.safetensors")};
    for (const QString &p : paths)
        if (!p.isEmpty() && QFile::exists(p))
            return p;
    return {};
}

double rms(const float *x, size_t n) {
    double s = 0;
    for (size_t i = 0; i < n; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(n, 1)));
}

} // namespace

class StemsTests : public QObject {
    Q_OBJECT
    // Separating takes every fast core, which the tests that time the
    // studio would feel: these two run on their own, after the rest.
    Q_CLASSINFO("alone", "aSongIsSplitIntoStemsThatAddUpToIt separatingStopsWhenCalledOff")
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev",
                "theProductIsThePlainSumAtAnyShapeAndThreadCount aToneThroughTheResamplerKeepsItsLevelAndPlace "
                "aFileThatIsNotTheModelIsSaidToBe")
private slots:
    void theProductIsThePlainSumAtAnyShapeAndThreadCount() {
        // Shapes on both sides of every block size, both operands either way
        // round, adding into what is there or not: each element as a plain
        // loop in doubles has it, and the same to the bit on 1 thread or 2–5.
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> u(-1, 1);
        double worst = 0;
        for (int t = 0; t < 60; ++t) {
            const int M = 1 + int(rng() % 200), N = 1 + int(rng() % 7000), K = 1 + int(rng() % 600);
            const bool ta = rng() % 2, tb = rng() % 2, add = rng() % 2;
            const int threads = 2 + int(rng() % 4);
            const long lda = ta ? M : K, ldb = tb ? K : N, ldc = N + long(rng() % 9);
            std::vector<float> A(size_t(M) * K), B(size_t(K) * N), C0(size_t(M) * ldc);
            for (auto &x : A) x = u(rng);
            for (auto &x : B) x = u(rng);
            for (auto &x : C0) x = u(rng);
            std::vector<float> C1 = C0, C2 = C0;
            stems::gemm(M, N, K, A.data(), lda, ta, B.data(), ldb, tb, C1.data(), ldc, add, 1);
            stems::gemm(M, N, K, A.data(), lda, ta, B.data(), ldb, tb, C2.data(), ldc, add, threads);
            QVERIFY2(C1 == C2, qPrintable(QStringLiteral("%1×%2×%3 on %4 threads").arg(M).arg(N).arg(K).arg(threads)));
            for (int probe = 0; probe < 50; ++probe) {
                const int i = int(rng() % uint32_t(M)), j = int(rng() % uint32_t(N));
                double want = add ? C0[size_t(i) * size_t(ldc) + size_t(j)] : 0.0;
                for (int k = 0; k < K; ++k)
                    want += double(ta ? A[size_t(k) * size_t(lda) + size_t(i)] : A[size_t(i) * size_t(lda) + size_t(k)]) *
                            (tb ? B[size_t(j) * size_t(ldb) + size_t(k)] : B[size_t(k) * size_t(ldb) + size_t(j)]);
                worst = std::max(worst, std::fabs(want - C1[size_t(i) * size_t(ldc) + size_t(j)]) / std::sqrt(double(K)));
            }
            // Columns past N are left alone.
            for (int i = 0; i < M; ++i)
                for (long j = N; j < ldc; ++j)
                    QCOMPARE(C1[size_t(i) * size_t(ldc) + size_t(j)], C0[size_t(i) * size_t(ldc) + size_t(j)]);
        }
        qInfo("worst error over sqrt(K): %.2g", worst);
        QVERIFY(worst < 1e-6);
    }

    void aToneThroughTheResamplerKeepsItsLevelAndPlace() {
        // 1 kHz at 48 kHz, to 44.1 and back: the same samples within -80
        // dB, not a sample late, away from the ends.
        const int n = 48000;
        std::vector<float> x(n);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = 0.5f * float(std::sin(2 * M_PI * 1000.0 * i / 48000));
        const std::vector<float> down = stems::resample(x.data(), x.size(), 48000, 44100);
        QCOMPARE(down.size(), size_t(44100));
        const std::vector<float> back = stems::resample(down.data(), down.size(), 44100, 48000);
        QCOMPARE(back.size(), size_t(n));
        double err = 0, sig = 0;
        for (int i = 2000; i < n - 2000; ++i) {
            err += double(back[size_t(i)] - x[size_t(i)]) * (back[size_t(i)] - x[size_t(i)]);
            sig += double(x[size_t(i)]) * x[size_t(i)];
        }
        qInfo("1 kHz round trip: %.1f dB", 10 * std::log10(sig / err));
        QVERIFY(err < sig * 1e-8);
        // At 44.1 kHz it is where it was: sample i of 44100 is at i/44100 s.
        double off = 0;
        for (int i = 2000; i < 42000; ++i)
            off = std::max(off, std::fabs(down[size_t(i)] - 0.5 * std::sin(2 * M_PI * 1000.0 * i / 44100)));
        QVERIFY2(off < 1e-4, qPrintable(QString::number(off)));
    }

    void aToneAboveTheNewNyquistIsGone() {
        // 23 kHz at 48 kHz has no place at 44.1: 90 dB down at least.
        const int n = 48000;
        std::vector<float> x(n);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = 0.5f * float(std::sin(2 * M_PI * 23000.0 * i / 48000));
        const std::vector<float> down = stems::resample(x.data(), x.size(), 48000, 44100);
        const double level = rms(down.data() + 2000, down.size() - 4000) / rms(x.data(), x.size());
        qInfo("23 kHz to 44.1 kHz: %.1f dB", 20 * std::log10(level));
        QVERIFY(level < 3e-5);
    }

    void aFileThatIsNotTheModelIsSaidToBe() {
        QTemporaryDir dir;
        std::string why;
        QVERIFY(!stems::load((dir.path() + QStringLiteral("/none.safetensors")).toStdString(), &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("isn't there")), why.c_str());
        QFile f(dir.path() + QStringLiteral("/junk.safetensors"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("\x10\0\0\0\0\0\0\0{\"a\": 1}       junk", 32));
        f.close();
        why.clear();
        QVERIFY(!stems::load(f.fileName().toStdString(), &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("damaged")), why.c_str());
    }

    void theCompactFileKeepsEachWeightWithinHalfItsStep() {
        // Every layer compacted at 6 bits: each weight comes back within
        // half a step (1/128 of its channel's RMS) of the published one, the
        // layers left as halves exactly, and a file cut short is damaged.
        const QString from = published();
        if (from.isEmpty())
            QSKIP("no htdemucs.safetensors here (bin/model --remake fetches it)");
        QTemporaryDir dir;
        const std::string to = (dir.path() + QStringLiteral("/m.tfq")).toStdString();
        std::string why;
        QVERIFY2(stems::compact(from.toStdString(), to, [](const std::string &name) {
            return name.rfind("crosstransformer.layers.0.", 0) == 0 ? 0.0 : 6.0;
        }, &why), why.c_str());
        qInfo("compact at 6 bits: %.1f MB", double(QFileInfo(QString::fromStdString(to)).size()) / 1e6);
        const auto a = stems::load(from.toStdString(), &why), b = stems::load(to, &why);
        QVERIFY2(a && b, why.c_str());
        int exact = 0, kept = 0;
        stems::forEachWeight(*a, *b, [&](const std::string &name, const std::vector<int> &shape, const float *x,
                                         const float *y, size_t n) {
            if (shape.size() < 2 || n < 1024 || name.rfind("crosstransformer.layers.0.", 0) == 0) {
                QVERIFY2(std::equal(x, x + n, y), name.c_str());
                ++exact;
                return;
            }
            ++kept;
            const bool tr = name.find("conv_tr") != std::string::npos;
            const size_t rows = size_t(shape[tr ? 1 : 0]), inner = tr ? n / size_t(shape[0]) / rows : n / rows;
            auto row = [&](size_t i) { return tr ? (i / inner) % rows : i / inner; };
            std::vector<double> sq(rows, 0);
            for (size_t i = 0; i < n; ++i)
                sq[row(i)] += double(x[i]) * x[i];
            for (size_t i = 0; i < n; ++i) {
                const double step = std::sqrt(sq[row(i)] / double(n / rows)) / 64;
                QVERIFY2(std::fabs(double(x[i]) - y[i]) <= 0.5 * step * 1.0001 + 1e-12, name.c_str());
            }
        });
        QCOMPARE(exact + kept, 533);
        QVERIFY(kept > 100);
        QFile f(QString::fromStdString(to));
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.resize(f.size() / 2));
        f.close();
        QVERIFY(!stems::load(to, &why));
        QVERIFY2(QString::fromStdString(why).contains(QStringLiteral("damaged")), why.c_str());
    }

    void aSongIsSplitIntoStemsThatAddUpToIt() {
        const QString path = weights();
        if (path.isEmpty())
            QSKIP("no stem model here (bin/model copies one; TENFOLD_STEMS_MODEL names one)");
        std::string why;
        const auto model = stems::load(path.toStdString(), &why);
        QVERIFY2(model, why.c_str());
        // Ten seconds at 48 kHz: a kick on every beat at 120 bpm, a bass
        // line under it, a sung "ah" over it.
        const int rate = 48000, frames = 10 * rate;
        std::vector<int16_t> mix(size_t(frames) * 2);
        for (int i = 0; i < frames; ++i) {
            const double t = double(i) / rate, beat = std::fmod(t, 0.5);
            const double kick = 0.5 * std::exp(-beat * 18) * std::sin(2 * M_PI * (50 + 80 * std::exp(-beat * 30)) * beat);
            const double note = 55.0 * (std::fmod(t, 2.0) < 1 ? 1.0 : 1.5);
            const double bass = 0.2 * std::sin(2 * M_PI * note * t) + 0.05 * std::sin(4 * M_PI * note * t);
            double voice = 0;
            for (int h = 1; h < 30; ++h) {
                const double f = 220 * h * (1 + 0.004 * std::sin(2 * M_PI * 5.5 * t));
                const double formant = std::exp(-std::pow((f - 700) / 150, 2)) + 0.5 * std::exp(-std::pow((f - 1200) / 200, 2));
                voice += 0.08 * formant * std::sin(2 * M_PI * f * t) / h * 4;
            }
            const double v = kick + bass + (t > 2 ? voice : 0);
            mix[size_t(2 * i)] = mix[size_t(2 * i + 1)] = int16_t(std::clamp(v, -1.0, 1.0) * 30000);
        }
        std::vector<int16_t> out[stems::kSources];
        stems::Options options;
        const auto t0 = std::chrono::steady_clock::now();
        QVERIFY2(stems::separate(*model, mix.data(), size_t(frames), rate, out, options, &why), why.c_str());
        qInfo("10 s separated in %.1f s",
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        double err = 0, sig = 0, energy[stems::kSources] = {};
        for (int s = 0; s < stems::kSources; ++s)
            QCOMPARE(out[s].size(), mix.size());
        for (size_t i = 0; i < mix.size(); ++i) {
            double sum = 0;
            for (int s = 0; s < stems::kSources; ++s) {
                sum += out[s][i];
                energy[s] += double(out[s][i]) * out[s][i];
            }
            err += (sum - mix[i]) * (sum - mix[i]);
            sig += double(mix[i]) * mix[i];
        }
        qInfo("stems add up to the mix within %.1f dB; drums %.1f, bass %.1f, other %.1f, vocals %.1f dB of it",
              10 * std::log10(sig / err), 10 * std::log10(energy[0] / sig), 10 * std::log10(energy[1] / sig),
              10 * std::log10(energy[2] / sig), 10 * std::log10(energy[3] / sig));
        QVERIFY(err < sig * 0.1);
        // The kick is the drums' and the bass line the bass's.
        QVERIFY(energy[stems::Drums] > sig * 0.05);
        QVERIFY(energy[stems::Bass] > sig * 0.05);
    }

    void separatingStopsWhenCalledOff() {
        const QString path = weights();
        if (path.isEmpty())
            QSKIP("no stem model here");
        std::string why;
        const auto model = stems::load(path.toStdString(), &why);
        QVERIFY(model);
        std::vector<int16_t> mix(size_t(60 * 48000) * 2, 0);
        for (size_t i = 0; i < mix.size(); ++i)
            mix[i] = int16_t((i * 7919) % 2000) - 1000;
        std::atomic<bool> cancel{false};
        stems::Options options;
        options.cancel = &cancel;
        options.progress = [&](float p) {
            if (p > 0.05f)
                cancel = true;
        };
        std::vector<int16_t> out[stems::kSources];
        const auto t0 = std::chrono::steady_clock::now();
        QVERIFY(!stems::separate(*model, mix.data(), mix.size() / 2, 48000, out, options, &why));
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        qInfo("called off after %.1f s: %s", took, why.c_str());
        QCOMPARE(QString::fromStdString(why), QStringLiteral("Stems called off"));
    }
};

TESTKIT_REGISTER(StemsTests)
#include "stems_tests.moc"
