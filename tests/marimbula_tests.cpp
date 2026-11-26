// The marimbula: a bass sound that is an instrument simulated (marimbula.h),
// checked as the acoustics of lamellophones and plywood boxes measure real
// ones (docs/marimbula.md): its tongues' beam modes and tuning, the box's
// air and panels carrying the fundamental, the finger's pluck and slap,
// the damping and the knock, the page's gestures, stability, and what it
// costs.

#include <QJsonArray>
#include <chrono>
#include <memory>
#include <vector>

#include "bass.h"
#include "marimbula.h"
#include "offline.h"
#include "parts.h"
#include "testkit.h"

using namespace tf;
using marimbula::Marimbula;

namespace {

constexpr float kRate = 48000;

template <typename... A>
std::string fmt(const char *f, A... a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, a...);
    return buf;
}
template <typename... A>
QString qfmt(const char *f, A... a) {
    return QString::fromStdString(fmt(f, a...));
}

// --- the instrument on its own ----------------------------------------------------------------------------

std::vector<float> run(Marimbula &m, double seconds) {
    std::vector<float> out;
    float b[32];
    for (int i = 0; i < int(seconds * kRate) / 32; ++i) {
        std::fill(b, b + 32, 0.0f);
        m.render(b, 32);
        out.insert(out.end(), b, b + 32);
    }
    return out;
}

// The power at hz over n samples from a (Hann-windowed DFT), in dB.
double powerAt(const std::vector<float> &x, size_t a, size_t n, double hz) {
    double re = 0, im = 0;
    const double w = 2 * M_PI * hz / kRate;
    for (size_t i = 0; i < n && a + i < x.size(); ++i) {
        const double v = x[a + i] * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n)));
        re += v * std::cos(w * double(i));
        im += v * std::sin(w * double(i));
    }
    return 10 * std::log10((re * re + im * im) / double(n) / double(n) + 1e-30);
}
// The strongest peak within ±span (a fraction) of hz: its level, and where.
double peakNear(const std::vector<float> &x, size_t a, size_t n, double hz, double span, double *at = nullptr) {
    double best = -1e9, bestHz = hz;
    for (double f = hz * (1 - span); f <= hz * (1 + span); f += hz * 0.0005) {
        const double p = powerAt(x, a, n, f);
        if (p > best)
            best = p, bestHz = f;
    }
    if (at)
        *at = bestHz;
    return best;
}
double rmsDb(const std::vector<float> &x, double t0, double t1) {
    double e = 0;
    size_t n = 0;
    for (size_t i = size_t(t0 * kRate); i < size_t(t1 * kRate) && i < x.size(); ++i, ++n)
        e += double(x[i]) * x[i];
    return 10 * std::log10(e / double(std::max<size_t>(n, 1)) + 1e-30);
}
// Share of the power between lo and hi Hz over t0..t1, in dB (a DFT every 20 Hz).
double bandShare(const std::vector<float> &x, double t0, double t1, double lo, double hi) {
    const size_t a = size_t(t0 * kRate), n = size_t((t1 - t0) * kRate);
    double all = 0, in = 0;
    for (double f = 20; f < 12000; f += 20) {
        const double p = std::pow(10.0, powerAt(x, a, n, f) / 10);
        all += p;
        if (f >= lo && f < hi)
            in += p;
    }
    return 10 * std::log10(in / all + 1e-30);
}

Marimbula fresh(float boxHeard = 52.0f) {
    Marimbula m;
    m.init(kRate);
    m.setBoxHeard(boxHeard);
    return m;
}
Marimbula::Stroke stroke(float vel, float tone = 0.5f, float slap = 0) {
    Marimbula::Stroke st;
    st.vel = vel;
    st.tone = tone;
    st.slap = slap;
    return st;
}
std::vector<float> pluckOne(float hz, Marimbula::Stroke st, double seconds) {
    Marimbula m = fresh();
    m.pluck(m.tongue(hz), st);
    return run(m, seconds);
}

// --- the bass engine ------------------------------------------------------------------------------------------

struct FakeAudio : AudioContext {
    Transport tr;
    Harmony h;
    float duckBuf[kBlock] = {};
    const Transport &transport() const override { return tr; }
    const Harmony &harmony() const override { return h; }
    double now() const override { return double(tr.sample) / double(tr.rate); }
    int offsetOf(double t, int n) const override {
        const int o = int((t - now()) * double(tr.rate));
        return o < 0 ? 0 : std::min(o, n - 1);
    }
    void pump(int, float) override {}
    void setChord(int, const int *, int) override {}
    const float *duck() const override { return duckBuf; }
};

// The bass engine on its own, at 100 BPM, in A minor unless told.
struct Rig {
    FakeAudio ctx;
    std::unique_ptr<PartEngine> engine;
    std::vector<float> left, right, mono;
    float pub[kPartData] = {};

    explicit Rig(int root = 9) {
        PartSpec spec = makeBass();
        engine = std::move(spec.engine);
        engine->init(kRate);
        ctx.tr.rate = int(kRate);
        ctx.tr.bpm = 100;
        ctx.tr.beatsPerSample = ctx.tr.bpm / 60.0 / kRate;
        ctx.h.tuning.set(root, 0, 4, false);
    }
    void send(Command c) {
        c.t = 0;
        engine->command(c, ctx);
    }
    void run(double seconds) {
        const int blocks = int(seconds * kRate / kBlock);
        for (int b = 0; b < blocks; ++b) {
            ctx.tr.beat = double(ctx.tr.sample) * ctx.tr.beatsPerSample;
            float l[kBlock] = {}, r[kBlock] = {};
            engine->render(l, r, kBlock, ctx);
            ctx.tr.sample += kBlock;
            left.insert(left.end(), l, l + kBlock);
            right.insert(right.end(), r, r + kBlock);
            for (int i = 0; i < kBlock; ++i)
                mono.push_back(0.5f * (l[i] + r[i]));
            engine->publish(pub);
        }
    }
    double peak() const {
        double p = 0;
        for (size_t i = 0; i < left.size(); ++i)
            p = std::max({p, double(std::fabs(left[i])), double(std::fabs(right[i]))});
        return p;
    }
    bool finite() const {
        for (size_t i = 0; i < left.size(); ++i)
            if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
                return false;
        return true;
    }
    double rms(double t0, double t1) const { return rmsDb(mono, t0, t1); }
    double hzOf(int degree) const { return midiToHz(ctx.h.midi(degree, 0) + bass::rootShift(ctx.h.tuning)); }
};

Command noteOn(int id, int degree, int flags = 0, float vel = 0.8f, float tone = 0.5f, float morph = 0.3f) {
    Command c;
    c.type = Command::On;
    c.id = id;
    c.i[0] = degree;
    c.i[2] = flags;
    c.i[3] = bass::Marimbula;
    c.f[0] = vel;
    c.f[1] = tone;
    c.f[2] = morph;
    c.f[4] = 0.5f;
    c.f[5] = 3;
    return c;
}
Command noteOff(int id) {
    Command c;
    c.type = Command::Off;
    c.id = id;
    return c;
}
Command move(int id, int degree, float pressure, float tone = 0.5f) {
    Command c;
    c.type = Command::Move;
    c.id = id;
    c.i[0] = degree;
    c.f[0] = pressure;
    c.f[1] = tone;
    c.f[2] = 0.3f;
    return c;
}

// The bass page set to MARIMBULA: P, then → six times.
std::string toMarimbula() {
    std::string s = "page 0 2\nkey 0.1 P\n";
    for (int k = 0; k < 6; ++k)
        s += fmt("key %.2f Right\n", 0.15 + 0.05 * k);
    return s;
}

QJsonObject bassSettings(const QJsonObject &doc) {
    return doc[QStringLiteral("parts")].toArray()[1].toObject()[QStringLiteral("settings")].toObject();
}

} // namespace

class MarimbulaTests : public QObject {
    Q_OBJECT
    // The tests bin/test runs while developing; the rest run with --full.
    Q_CLASSINFO("dev", "itStaysFiniteAndSleeps itIsChosenSavedAndReopened")
private slots:
    // --- the tongue: a beam --------------------------------------------------------------------------------

    void theBeamIsACantileverWhenHeldHard() {
        // Both root springs stiff: the clamped cantilever's λ and ratios.
        const marimbula::BeamModes b = marimbula::solveBeam(1e7, 1e9);
        QVERIFY2(std::fabs(b.lambda[0] - 1.8751) < 1e-3, qPrintable(QString::number(b.lambda[0])));
        const double want[3] = {6.267, 17.547, 34.386};
        for (int n = 1; n < 4; ++n) {
            const double r = b.lambda[n] * b.lambda[n] / (b.lambda[0] * b.lambda[0]);
            QVERIFY2(std::fabs(r / want[n - 1] - 1) < 2e-3, qPrintable(qfmt("mode %d ratio %.3f", n, r)));
        }
        // Tip-normalised, the modal mass is a quarter of the beam's and the
        // first mode's ∫φ is 0.3915 of its length.
        QVERIFY2(std::fabs(b.M[0] - 0.25) < 2e-3 && std::fabs(b.P[0] - 0.3915) < 2e-3,
                 qPrintable(qfmt("%.4f %.4f", b.M[0], b.P[0])));
        // A root that only turns on its spring is a bar on a hinge:
        // λ₁⁴ → 3k as k → 0.
        const marimbula::BeamModes soft = marimbula::solveBeam(0.05, 1e9);
        QVERIFY2(std::fabs(std::pow(soft.lambda[0], 4) / 0.15 - 1) < 0.03, qPrintable(QString::number(soft.lambda[0])));
    }

    void theOvertonesAreAsInharmonicAsMeasuredTines() {
        // A turning root stretches the ratios, a bridge that gives squeezes
        // them: the first overtone lands where kalimba tines are measured
        // (5.3–6.8 times the fundamental), the second toward theirs (11.7–14).
        Marimbula m = fresh();
        for (float hz : {41.2f, 55.0f, 82.4f, 110.0f, 165.0f, 196.0f}) {
            const int s = m.tongue(hz);
            const float r1 = m.modeHz(s, 1) / m.modeHz(s, 0), r2 = m.modeHz(s, 2) / m.modeHz(s, 0);
            const QString what = QStringLiteral("%1 Hz: L %2 cm, ratios %3 %4")
                                     .arg(hz).arg(100 * m.lengthOf(s), 0, 'f', 1).arg(r1, 0, 'f', 2).arg(r2, 0, 'f', 2);
            QVERIFY2(r1 > 5.3f && r1 < 6.8f, qPrintable(what));
            QVERIFY2(r2 > 12.0f && r2 < 17.5f, qPrintable(what));
            // Cut from one spring (28 × 0.75 mm), a bass tongue is 6–12 cm,
            // as that stock gives (f·L² ≈ 0.57 m²/s).
            if (hz <= 110)
                QVERIFY2(m.lengthOf(s) > 0.06f && m.lengthOf(s) < 0.13f, qPrintable(what));
        }
    }

    void aPluckedTongueSoundsInTuneOnItsBox() {
        // The maker slides each tongue until it sounds in tune on the box
        // (the box's give pulls it flat below its resonances, sharp above).
        // Every semitone from E1 to E4, for three boxes: within a few cents,
        // with most of its energy at the note, but for the wolves. A note a
        // little above one of the box's resonances (a front mode, or its air)
        // falls in the gap the tongue and the box open between them when
        // coupled: no length of tongue sounds there, and the note smears.
        for (float heard : {45.0f, 55.0f, 64.0f, 72.0f}) {
            // The box's own resonances: its panels' modes and its air, and
            // where they ring coupled (the peaks of an impulse's response,
            // struck off every node).
            std::vector<double> peaks;
            {
                Marimbula box = fresh(heard);
                peaks.push_back(box.heard());
                for (int k = 0; k < 14; ++k)
                    peaks.push_back(box.panelModeHz(k));
                box.impulse(0.001f, 0.37f, 0.31f);
                const auto r = run(box, 1.0);
                std::vector<double> p;
                for (double f = 30; f < 450; f += 1)
                    p.push_back(powerAt(r, 0, r.size(), f));
                const double top = *std::max_element(p.begin(), p.end());
                for (size_t i = 1; i + 1 < p.size(); ++i)
                    if (p[i] > p[i - 1] && p[i] >= p[i + 1] && p[i] > top - 40)
                        peaks.push_back(30.0 + double(i));
            }
            int wolves = 0;
            QString which;
            for (int semi = 0; semi <= 36; ++semi) {
                const float hz = 41.2f * std::exp2(float(semi) / 12.0f);
                Marimbula m = fresh(heard);
                m.pluck(m.tongue(hz), stroke(0.6f));
                const auto x = run(m, 1.3);
                const size_t a = size_t(0.1 * kRate), n = size_t(1.2 * kRate);
                double at = 0;
                peakNear(x, a, n, hz, 0.03, &at);
                // The share of the power within 1.5% of the note: the
                // DFT's own bins there against all of it (Parseval).
                double all = 0, near = 0;
                for (size_t i = 0; i < n; ++i) {
                    const double v = x[a + i] * (0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n)));
                    all += v * v;
                }
                for (long k = long(std::ceil(hz * 0.985 * double(n) / kRate)); double(k) <= hz * 1.015 * double(n) / kRate; ++k)
                    near += 2 * std::pow(10.0, powerAt(x, a, n, double(k) * kRate / double(n)) / 10) * double(n) * double(n) / double(n);
                const double cents = 1200 * std::log2(at / hz);
                if (std::fabs(cents) < 8 && near > 0.3 * all)
                    continue;
                ++wolves;
                which += qfmt(" %.1f Hz (%+.0f c, %.0f%%)", hz, cents, 100 * near / all);
                bool byMode = false;
                for (double f : peaks)
                    byMode |= hz / f > 0.97 && hz / f < 1.15;
                QVERIFY2(byMode, qPrintable(qfmt("box %.0f:", heard) + which));
            }
            QVERIFY2(wolves <= 7, qPrintable(qfmt("box %.0f: %d wolves:", heard, wolves) + which));
        }
    }

    void theFirstOvertoneIsHeardInTheAttack() {
        // The tongue's first overtone, about six times the note, is in the
        // attack, under the fundamental (Chapman's kalimba: 2–5 dB in a
        // whole note's spectrum), and dies faster than it.
        const float hz = 73.4f;
        Marimbula m = fresh();
        const int s = m.tongue(hz);
        const float f2 = m.modeHz(s, 1);
        m.pluck(s, stroke(0.7f, 0.7f));
        const auto x = run(m, 1.2);
        const size_t n = size_t(0.08 * kRate);
        const double f1a = peakNear(x, 240, n, hz, 0.03), f2a = peakNear(x, 240, n, f2, 0.06);
        const double f1b = peakNear(x, size_t(0.5 * kRate), n, hz, 0.03), f2b = peakNear(x, size_t(0.5 * kRate), n, f2, 0.06);
        const QString what = QStringLiteral("attack f2 %1 dB, at 0.5 s %2 dB").arg(f2a - f1a, 0, 'f', 1).arg(f2b - f1b, 0, 'f', 1);
        QVERIFY2(f2a - f1a > -38 && f2a - f1a < 0, qPrintable(what));
        QVERIFY2(f2b - f1b < f2a - f1a - 10, qPrintable(what));
    }

    // --- the box --------------------------------------------------------------------------------------------

    void theHoleIsCutForTheBoxToSound() {
        // Helmholtz's formula, rigid walls: c/2π·√(S/(V·L)), L the panel and
        // an end correction each side. A 6 cm hole in this 44-litre box:
        const double S = M_PI * 0.06 * 0.06, L = 0.00635 + 1.7 * 0.06, V = 0.58 * 0.38 * 0.20;
        const double f = 343 / (2 * M_PI) * std::sqrt(S / (V * L));
        QVERIFY2(std::fabs(marimbula::helmholtzHz(0.06) / f - 1) < 1e-6, qPrintable(QString::number(f)));
        // What it sounds at with the panels giving: some 0.8–0.85 of the
        // formula (Kičák measured 0.85 on a cajón), solved from the box's
        // own equation. An impulse on the front panel rings the box: there
        // is a peak where the air was set to sound.
        for (float heard : {45.0f, 55.0f, 70.0f}) {
            Marimbula m = fresh(heard);
            const double ratio = m.heard() / m.helmholtz();
            QVERIFY2(std::fabs(m.heard() / heard - 1) < 0.01 && ratio > 0.75 && ratio < 0.9,
                     qPrintable(qfmt("set %.0f: heard %.1f, formula %.1f", heard, m.heard(), m.helmholtz())));
            m.impulse(0.001f, 0.5f, 0.22f);
            const auto x = run(m, 2.0);
            std::vector<double> p;
            for (double hz = heard * 0.85; hz < heard * 1.15; hz += 0.25)
                p.push_back(powerAt(x, 0, x.size(), hz));
            size_t top = 0;
            for (size_t i = 1; i + 1 < p.size(); ++i)
                if (p[i] > p[i - 1] && p[i] >= p[i + 1] && (top == 0 || p[i] > p[top]))
                    top = i;
            const double at = heard * 0.85 + 0.25 * double(top);
            QVERIFY2(top > 0 && std::fabs(at / heard - 1) < 0.04, qPrintable(qfmt("set %.0f, rings at %.1f", heard, at)));
            // Holes a maker cuts: a few centimetres to fifteen across.
            QVERIFY2(m.holeRadius() > 0.015f && m.holeRadius() < 0.08f, qPrintable(QString::number(m.holeRadius())));
        }
    }

    void theBoxCarriesTheFundamental() {
        // A tongue is a strip a few centimetres wide: on its own it hardly
        // moves air at a bass frequency. What is heard of the fundamental
        // comes from the box: the panels and the air in the hole.
        const float hz = 52.0f; // where the box's air sounds
        auto fundamental = [&](bool box, bool tongue, bool coupled) {
            Marimbula m = fresh();
            m.setPaths(box, tongue);
            m.setCoupled(coupled);
            m.pluck(m.tongue(hz), stroke(0.7f));
            const auto x = run(m, 1.0);
            return peakNear(x, size_t(0.1 * kRate), size_t(0.8 * kRate), hz, 0.03);
        };
        const double all = fundamental(true, true, true), box = fundamental(true, false, true),
                     tongue = fundamental(false, true, true), alone = fundamental(true, true, false);
        const QString what = QStringLiteral("all %1, box %2, tongue %3, tongue on a rigid block %4 dB")
                                 .arg(all, 0, 'f', 1).arg(box, 0, 'f', 1).arg(tongue, 0, 'f', 1).arg(alone, 0, 'f', 1);
        QVERIFY2(std::fabs(all - box) < 1.0, qPrintable(what));
        QVERIFY2(box > tongue + 25, qPrintable(what));
        QVERIFY2(all > alone + 25, qPrintable(what));
        // And of the box, the air: with no air spring (no hole, the walls
        // alone), a note on the air's resonance is weaker.
        Marimbula m = fresh();
        m.setAir(false);
        m.pluck(m.tongue(hz), stroke(0.7f));
        const auto x = run(m, 1.0);
        const double noAir = peakNear(x, size_t(0.1 * kRate), size_t(0.8 * kRate), hz, 0.03);
        QVERIFY2(box > noAir + 4, qPrintable(what + QStringLiteral(", no air %1").arg(noAir, 0, 'f', 1)));
    }

    void theBoxShakesTheTonguesToo() {
        // The coupling goes both ways: a knock on the panel sets a still
        // tongue swinging through the bridge (sympathy).
        Marimbula m = fresh();
        const int s = m.tongue(110.0f);
        m.knock(0.8f, 0.0f);
        double tipMax = 0;
        for (int i = 0; i < 40; ++i) {
            run(m, 0.005);
            tipMax = std::max(tipMax, double(std::fabs(m.tipOf(s))));
        }
        QVERIFY2(tipMax > 1e-6, qPrintable(QString::number(tipMax)));
        Marimbula q = fresh();
        q.setCoupled(false);
        const int u = q.tongue(110.0f);
        q.knock(0.8f, 0.0f);
        run(q, 0.2);
        QVERIFY2(std::fabs(q.tipOf(u)) < 1e-9f, qPrintable(QString::number(q.tipOf(u))));
    }

    // --- the finger -------------------------------------------------------------------------------------------

    void harderIsLouderAndTheNailBrighter() {
        double level[3];
        const float vels[3] = {0.2f, 0.5f, 0.9f};
        for (int k = 0; k < 3; ++k)
            level[k] = rmsDb(pluckOne(73.4f, stroke(vels[k]), 0.4), 0, 0.3);
        QVERIFY2(level[1] > level[0] + 3 && level[2] > level[1] + 3,
                 qPrintable(qfmt("%.1f %.1f %.1f", level[0], level[1], level[2])));
        double bright[3];
        const float tones[3] = {0.1f, 0.5f, 0.9f};
        for (int k = 0; k < 3; ++k)
            bright[k] = bandShare(pluckOne(73.4f, stroke(0.7f, tones[k]), 0.3), 0.005, 0.105, 500, 6000);
        QVERIFY2(bright[1] > bright[0] + 4 && bright[2] > bright[1] + 4,
                 qPrintable(qfmt("%.1f %.1f %.1f", bright[0], bright[1], bright[2])));
    }

    void aSlapHasAnAttackAPluckDoesNot() {
        // A pluck is let go from rest: its sound starts as the finger slips.
        // A slap lands at speed first: a knock in the mids with the note.
        const auto pluck = pluckOne(73.4f, stroke(0.9f, 0.4f, 0.0f), 0.5);
        const auto slap = pluckOne(73.4f, stroke(0.9f, 0.4f, 1.0f), 0.5);
        const double p = bandShare(pluck, 0.0, 0.03, 300, 3000), s = bandShare(slap, 0.0, 0.03, 300, 3000);
        QVERIFY2(s > p + 6, qPrintable(qfmt("mids in the first 30 ms: pluck %.1f, slap %.1f dB", p, s)));
        // Slapped as hard as a hand can, the tip clacks on the panel;
        // plucked, it never reaches it.
        auto deepest = [](const Marimbula::Stroke &st) {
            Marimbula m = fresh();
            const int t = m.tongue(73.4f);
            m.pluck(t, st);
            float d = 0;
            for (int i = 0; i < 100; ++i) {
                run(m, 64 / kRate);
                d = std::max(d, m.tipOf(t));
            }
            return d;
        };
        const float gap = float(marimbula::Physics::kGap);
        QVERIFY2(deepest(stroke(1.0f, 0.0f, 1.0f)) > gap, qPrintable(QString::number(deepest(stroke(1.0f, 0.0f, 1.0f)))));
        QVERIFY2(deepest(stroke(1.0f, 0.5f, 0.0f)) < gap, qPrintable(QString::number(deepest(stroke(1.0f, 0.5f, 0.0f)))));
    }

    void aFingerLaidOnTheTongueStillsIt() {
        // A finger laid on a ringing tongue's tip stills it in a tenth of a
        // second.
        Marimbula a = fresh(), b = fresh();
        const int s = a.tongue(65.4f), t = b.tongue(65.4f);
        a.pluck(s, stroke(0.7f));
        b.pluck(t, stroke(0.7f));
        run(a, 0.2);
        run(b, 0.2);
        b.damp(t, 1.0f);
        const auto x = run(a, 0.5), y = run(b, 0.5);
        const double free = rmsDb(x, 0.3, 0.5), held = rmsDb(y, 0.3, 0.5);
        QVERIFY2(held < free - 30, qPrintable(qfmt("free %.1f, stilled %.1f dB", free, held)));
        // Free, it rings for a second or a few on the clamp's own loss (the
        // survey's estimate for a marimbula: T60 1–3 s; a kalimba tine's
        // tail is 4–5 s) …
        const auto z = pluckOne(65.4f, stroke(0.7f), 3.0);
        const double tail = 60 / ((rmsDb(z, 1.0, 1.2) - rmsDb(z, 2.6, 2.8)) / 1.6);
        QVERIFY2(tail > 1 && tail < 4, qPrintable(qfmt("T60 %.1f s", tail)));
        // … unless it sits by a resonance of the box, which draws its
        // energy out fast (the box's modes are the fast-dying part of a
        // kalimba's attack).
        Marimbula w = fresh();
        const int k = w.tongue(w.panelModeHz(0) * 1.05f);
        w.pluck(k, stroke(0.7f));
        const auto v = run(w, 1.0);
        const double wolf = 60 / ((rmsDb(v, 0.1, 0.2) - rmsDb(v, 0.6, 0.7)) / 0.5);
        QVERIFY2(wolf < tail / 2, qPrintable(qfmt("T60 %.1f s; by the panel's first mode (%.0f Hz) %.2f s", tail,
                                                 w.panelModeHz(0), wolf)));
    }

    void theKnockIsThePalmOrTheKnuckles() {
        // The palm flat on the panel: the box's air and its lowest modes, a
        // bass drum; the knuckles near the top edge: its upper modes, dry.
        auto knock = [](float tone) {
            Marimbula m = fresh();
            m.knock(0.8f, tone);
            return run(m, 0.5);
        };
        const auto palm = knock(0.0f), knuckle = knock(1.0f);
        const double palmLow = bandShare(palm, 0, 0.3, 20, 200);
        const double palmHigh = bandShare(palm, 0, 0.1, 400, 4000), knuckleHigh = bandShare(knuckle, 0, 0.1, 400, 4000);
        QVERIFY2(palmLow > -1 && knuckleHigh > palmHigh + 15,
                 qPrintable(qfmt("under 200 Hz: palm %.1f; 0.4–4 kHz: palm %.1f knuckle %.1f", palmLow, palmHigh, knuckleHigh)));
        // Both are short: a box, not a drum skin.
        for (const auto *x : {&palm, &knuckle})
            QVERIFY2(rmsDb(*x, 0.3, 0.4) < rmsDb(*x, 0, 0.05) - 40,
                     qPrintable(qfmt("%.1f %.1f", rmsDb(*x, 0, 0.05), rmsDb(*x, 0.3, 0.4))));
    }

    // --- the page --------------------------------------------------------------------------------------------

    void everyNoteOfTheKeyPlaysInTune() {
        for (int root : {4, 9, 2}) {
            for (int degree : {-2, 0, 2, 4, 7}) {
                Rig rig(root);
                rig.send(noteOn(1, degree, 0, 0.75f));
                rig.run(1.0);
                const double hz = rig.hzOf(degree);
                double at = 0;
                peakNear(rig.mono, size_t(0.3 * kRate), size_t(0.65 * kRate), hz, 0.03, &at);
                const QString what =
                    QString::fromStdString(fmt("root %d degree %d: %.2f Hz at %.2f, peak %.2f", root, degree, hz, at, rig.peak()));
                QVERIFY2(std::fabs(1200 * std::log2(at / hz)) < 8, qPrintable(what));
                QVERIFY2(rig.finite() && rig.peak() < 0.95, qPrintable(what));
                // Every note within a sensible level of the others (the
                // engineer's EQ evens out the box's tilt; the quietest is
                // C1 in E, below any marimbula's lowest tongue).
                const double level = rig.rms(0, 0.3);
                QVERIFY2(level > -48 && level < -8, qPrintable(what + QStringLiteral(" rms %1").arg(level)));
            }
        }
    }

    void liftingStillsItAndPressingStillsItSooner() {
        auto tail = [](bool lift, bool press, bool mute) {
            Rig rig;
            rig.send(noteOn(1, 0, mute ? bass::Square : 0, 0.75f));
            rig.run(0.25);
            if (press)
                rig.send(move(1, 0, 1.0f));
            if (lift)
                rig.send(noteOff(1));
            rig.run(0.5);
            return rig.rms(0.55, 0.75);
        };
        const double held = tail(false, false, false), lifted = tail(true, false, false),
                     pressed = tail(false, true, false), muted = tail(false, false, true);
        const QString what = QString::fromStdString(fmt("held %.1f, lifted %.1f, pressed %.1f, muted %.1f", held, lifted, pressed, muted));
        QVERIFY2(lifted < held - 30, qPrintable(what));
        QVERIFY2(pressed < held - 20, qPrintable(what));
        QVERIFY2(muted < held - 30, qPrintable(what));
    }

    void everyFingerPlaysItsOwnTongue() {
        // A second finger plucks its own tongue; the first, still held,
        // rings on under it. Lifting the second stills only its own.
        Rig rig;
        rig.send(noteOn(1, 0, 0, 0.8f));
        rig.run(0.3);
        rig.send(noteOn(2, 4, 0, 0.8f));
        rig.run(0.4);
        const double a = rig.hzOf(0), b = rig.hzOf(4);
        const size_t from = size_t(0.4 * kRate), n = size_t(0.3 * kRate);
        const double first = peakNear(rig.mono, from, n, a, 0.02), second = peakNear(rig.mono, from, n, b, 0.02);
        rig.send(noteOff(2));
        rig.run(0.4);
        const size_t from2 = size_t(0.85 * kRate);
        const double firstAfter = peakNear(rig.mono, from2, n, a, 0.02), secondAfter = peakNear(rig.mono, from2, n, b, 0.02);
        const QString what =
            QString::fromStdString(fmt("both: %.1f %.1f; second lifted: %.1f %.1f", first, second, firstAfter, secondAfter));
        QVERIFY2(first > second - 20, qPrintable(what));
        QVERIFY2(secondAfter < second - 25 && firstAfter > secondAfter + 15, qPrintable(what));
    }

    void theClickKnocksAndTheRollPlucks() {
        // The click knocks the box: something sounds that the note alone
        // doesn't, and the note rings on.
        Rig plain, knocked;
        for (Rig *r : {&plain, &knocked}) {
            r->send(noteOn(1, 0, 0, 0.7f, 0.9f));
            r->run(0.4);
        }
        Command click;
        click.type = bass::kStrike;
        click.id = 1;
        knocked.send(click);
        plain.run(0.2);
        knocked.run(0.2);
        QVERIFY2(knocked.rms(0.4, 0.45) > plain.rms(0.4, 0.45) + 6,
                 qPrintable(qfmt("%.1f %.1f", plain.rms(0.4, 0.45), knocked.rms(0.4, 0.45))));
        // Rolled, a held note is plucked again on the pattern's steps.
        Rig roll;
        Command r;
        r.type = Command::Param;
        r.i[0] = bass::ShapeRoll;
        r.f[0] = 0.5f;
        r.f[1] = 4; // DRIVE: every sixteenth
        roll.send(r);
        roll.send(noteOn(1, 0, 0, 0.8f));
        roll.run(1.2);
        // Sixteenths at 100 BPM are 150 ms apart: at each the level climbs
        // (over 40 ms or so: the root sits on the box's air, which takes
        // that long to swell).
        int onsets = 0;
        bool rising = false;
        QString trace;
        std::vector<double> l;
        for (double t = 0.0; t < 1.2; t += 0.01) {
            l.push_back(roll.rms(t, t + 0.01));
            trace += QString::number(int(l.back())) + QLatin1Char(' ');
            const size_t k = l.size() - 1;
            const double low = *std::min_element(l.begin() + long(k >= 4 ? k - 4 : 0), l.end());
            const bool up = l[k] > low + 6;
            if (up && !rising)
                ++onsets;
            rising = up;
        }
        QVERIFY2(onsets >= 6, qPrintable(QString::number(onsets) + QStringLiteral(": ") + trace));
        QVERIFY(roll.finite());
    }

    void itIsChosenSavedAndReopened() {
        const std::string script = toMarimbula() + "down 0.6 1 0.2083 0.5 p=0.6\nup 1.2 1\nend 1.5\n";
        const auto take = testkit::reopen(script, 0.4, [&](Session &before, Session &opened, const QJsonObject &doc) {
            QVERIFY2(before.pageStatus(1).contains(QLatin1String("MARIMBULA")), qPrintable(before.pageStatus(1)));
            QCOMPARE(bassSettings(doc)[QStringLiteral("sound")].toString(), QStringLiteral("MARIMBULA"));
            QCOMPARE(int(opened.probe(1, QStringLiteral("sound"))), int(bass::Marimbula));
        });
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY2(take.peak() < 1.0, qPrintable(QString::number(take.peak())));
        QVERIFY2(take.rms(0.6, 1.0) > 0.01, qPrintable(QString::number(take.rms(0.6, 1.0))));
    }

    // --- stability ---------------------------------------------------------------------------------------------

    void itStaysFiniteAndSleeps() {
        // Everything at once, at three rates: plucks and slaps on every
        // tongue, the same one hammered, knocks, damping on and off, bends
        // and retunings while ringing, the box rebuilt under it.
        for (float rate : {44100.0f, 48000.0f, 96000.0f}) {
            Marimbula m;
            m.init(rate);
            m.setBoxHeard(55);
            Rng rng(7);
            float b[64];
            float top = 0;
            for (int blk = 0; blk < int(4 * rate) / 64; ++blk) {
                const float r = rng.unit();
                if (blk % 7 == 0) {
                    const int s = m.tongue(40.0f * std::exp2(3.0f * rng.unit()));
                    m.pluck(s, stroke(rng.unit(), rng.unit(), r < 0.4f ? 1.0f : 0.0f));
                }
                if (blk % 23 == 0)
                    m.knock(rng.unit(), rng.unit());
                if (blk % 11 == 0)
                    m.damp(int(rng.unit() * marimbula::kSlots), rng.unit() < 0.5f ? 1.0f : 0.0f);
                if (blk % 13 == 0)
                    m.setPitch(int(rng.unit() * marimbula::kSlots), 40.0f * std::exp2(4.0f * rng.unit()));
                if (blk % 500 == 0)
                    m.setBoxHeard(45.0f + 35.0f * rng.unit());
                std::fill(b, b + 64, 0.0f);
                m.render(b, 64);
                for (float v : b) {
                    QVERIFY2(std::isfinite(v), qPrintable(qfmt("rate %.0f block %d", rate, blk)));
                    top = std::max(top, std::fabs(v));
                }
            }
            // A few pascals at a metre, never more than a hand could make.
            QVERIFY2(top < 40, qPrintable(QString::number(top)));
            // Left alone, it rings out and sleeps.
            for (int s = 0; s < marimbula::kSlots; ++s)
                m.damp(s, 1.0f);
            for (int blk = 0; blk < int(6 * rate) / 64 && m.active(); ++blk) {
                std::fill(b, b + 64, 0.0f);
                m.render(b, 64);
            }
            QVERIFY2(!m.active(), qPrintable(QString::number(rate)));
        }
    }

    void thePageSurvivesAnything() {
        // Fingers through the bass page as a player would abuse it, the
        // sound switched away and back while it rings.
        std::string s = toMarimbula();
        for (int f = 0; f < 24; ++f) {
            const double t = 0.5 + 0.08 * f;
            s += fmt("down %.3f %d %.3f %.3f p=%.2f\n", t, 10 + f, 0.05 + 0.037 * (f % 12), 0.2 + 0.03 * (f % 7),
                     0.3 + 0.03 * (f % 20));
            s += fmt("up %.3f %d\n", t + 0.05 + 0.02 * (f % 5), 10 + f);
        }
        s += "down 2.6 50 0.3 0.4 p=0.9\nclick 2.7\nkey 2.8 Left\nkey 2.9 Right\nup 3.2 50\nend 4\n";
        const auto take = testkit::play(s);
        QVERIFY(take.error.empty());
        QVERIFY(take.finite());
        QVERIFY2(take.peak() < 1.0, qPrintable(QString::number(take.peak())));
        QVERIFY2(take.rms(0.5, 2.5) > 0.005, qPrintable(QString::number(take.rms(0.5, 2.5))));
    }

    // --- the cost ----------------------------------------------------------------------------------------------

    void theInstrumentIsCheap() {
        // Four tongues ringing on the box at once, a knock now and then:
        // about a percent of a core per second of sound (two at most).
        TENFOLD_TIMED();
        Marimbula m = fresh();
        float b[32];
        const auto t0 = std::chrono::steady_clock::now();
        for (int blk = 0; blk < int(4 * kRate) / 32; ++blk) {
            if (blk % 300 == 0)
                for (int k = 0; k < marimbula::kSlots; ++k)
                    m.pluck(m.tongue(55.0f * std::exp2(float(k) / 3.0f)), stroke(0.8f));
            if (blk % 450 == 0)
                m.knock(0.7f, 0.5f);
            std::fill(b, b + 32, 0.0f);
            m.render(b, 32);
        }
        const double load = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 4.0;
        QVERIFY2(load < 0.02, qPrintable(QString::number(load)));
    }

    void tenFingersOnTheMarimbulaStayInsideBudget() {
        // Ten fingers on the bass page through the whole studio, as the
        // other pages' budgets are measured.
        TENFOLD_TIMED();
        std::string s = toMarimbula();
        for (int f = 0; f < 10; ++f)
            for (int k = 0; k < 10; ++k)
                s += fmt("tap %.3f %.3f %.3f p=0.8 len=0.08\n", 0.6 + 0.3 * k + 0.02 * f, 0.05 + 0.09 * f, 0.3 + 0.04 * (f % 5));
        for (int f = 0; f < 10; ++f)
            s += fmt("down %.3f %d %.3f %.3f p=0.7\n", 3.8 + 0.01 * f, 100 + f, 0.05 + 0.09 * f, 0.4);
        s += "end 6\n";
        const RenderStats st = renderScript(s, "");
        QVERIFY(st.error.empty());
        QVERIFY2(st.p99Load < 0.12, qPrintable(QString::number(st.p99Load)));
        QVERIFY(st.peakDb < 0);
    }
};

TESTKIT_REGISTER(MarimbulaTests)
#include "marimbula_tests.moc"
