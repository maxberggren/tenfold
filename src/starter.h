#pragma once

// Starter parts: the simplest thing each page can be, built on the shared
// synth voices, so the studio plays end to end before a part has its own
// design. They also show the framework in use: commands in scale degrees,
// recording a loop with Enter, console drawing.
//
//   notes  columns are the notes of the scale over two octaves; a finger is a
//          note, sliding up opens its filter; Enter records a loop.
//   drums  six zones across the pad: kick, snare, clap, hat, open hat, perc.

#include <QPainter>
#include <array>
#include <cmath>

#include "console.h"
#include "electro.h"
#include "part.h"
#include "phrase.h"

namespace tf::starter {

// --- notes ------------------------------------------------------------------

// Enter records this part from the next bar; Enter again ends the loop at
// the bar after. Loops only ever come from this, never from timing.
inline bool toggleRecording(ControlContext &ctx) {
    if (ctx.studio().parts[ctx.part()].recording >= 0) {
        ctx.stopRecording();
        ctx.notice(QStringLiteral("Loop closes at the bar"));
    } else {
        const int slot = std::max(0, ctx.freeSlot());
        ctx.record(slot, 8);
        ctx.notice(QStringLiteral("Recording slot %1 from the next bar").arg(slot + 1));
    }
    return true;
}

struct NotesConfig {
    Patch patch = Patch::Lead;
    int octave = 1;      // the pad's lowest octave above the key's base (A2)
    int span = 2;        // octaves across the pad
    float cutoff = 0.6f;
    float res = 0.25f;
    float envAmt = 0.45f, envDecay = 0.35f;
    float width = 0.4f, sub = 0.3f;
    int chordTones = 3;  // for the chord patches
};

class NotesEngine : public PartEngine {
public:
    explicit NotesEngine(NotesConfig c) : m_cfg(c) {}

    void init(float rate) override {
        m_rate = rate;
        for (size_t i = 0; i < m_voices.size(); ++i)
            m_voices[i].v.init(0x5151u + uint32_t(i) * 7919u);
    }

    void command(const Command &c, AudioContext &ctx) override {
        switch (c.type) {
        case Command::On: {
            Slot &s = take(c.id);
            s.degree = c.i[0];
            s.octave = c.i[1];
            s.cut = c.f[1];
            float chord[4] = {};
            const int tones = chordish() ? m_cfg.chordTones : 1;
            const Harmony &h = ctx.harmony();
            for (int k = 0; k < tones; ++k)
                chord[k] = float(h.tuning.degreeSemis(s.degree + 2 * k) - h.tuning.degreeSemis(s.degree));
            s.v.start(m_cfg.patch, c.id, float(pitch(s, h)), chord, tones, c.f[0], 0.5f, c.f[1], m_rate, ++m_age);
            break;
        }
        case Command::Move:
            for (auto &s : m_voices)
                if (s.v.active && s.v.finger == c.id)
                    s.cut = c.f[1];
            break;
        case Command::Off:
            for (auto &s : m_voices)
                if (s.v.active && s.v.finger == c.id && !s.v.releasing)
                    s.v.noteOff();
            break;
        default:
            break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        const Harmony &h = ctx.harmony();
        const Transport &tr = ctx.transport();
        const double steps = tr.beat * 4.0;
        for (auto &s : m_voices) {
            if (!s.v.active)
                continue;
            s.v.midiTarget = float(pitch(s, h));
            SynthParams p;
            p.cutoff = m_cfg.cutoff * 0.6f + 0.55f * s.cut;
            p.res = m_cfg.res;
            p.envAmt = m_cfg.envAmt;
            p.envDecay = m_cfg.envDecay;
            p.width = m_cfg.width;
            p.sub = m_cfg.sub;
            if (m_cfg.patch == Patch::Arp) {
                p.arpPhase = float(steps - std::floor(steps));
                p.arpStep = int(std::floor(steps));
            }
            s.v.render(l, r, n, p, m_rate, 1.0f);
        }
    }

    void silence(bool hard) override {
        for (auto &s : m_voices) {
            if (hard)
                s.v.active = false;
            else if (s.v.active)
                s.v.noteOff();
        }
    }

    void publish(float *data) override {
        // Per voice: degree, octave, level, live (1) or replayed (2).
        int k = 0;
        for (auto &s : m_voices) {
            if (!s.v.active || k >= 16)
                continue;
            data[4 * k] = float(s.degree);
            data[4 * k + 1] = float(s.octave);
            data[4 * k + 2] = s.v.level;
            data[4 * k + 3] = (s.v.finger & (1 << 30)) ? 2.0f : 1.0f;
            ++k;
        }
        for (; k < 16; ++k)
            data[4 * k + 3] = 0;
    }

    int voices() const override {
        int n = 0;
        for (auto &s : m_voices)
            n += s.v.active;
        return n;
    }

private:
    struct Slot {
        SynthVoice v;
        int degree = 0, octave = 0;
        float cut = 0.5f;
    };
    bool chordish() const { return m_cfg.patch == Patch::Chord || m_cfg.patch == Patch::Arp; }
    double pitch(const Slot &s, const Harmony &h) const { return h.midi(s.degree, m_cfg.octave + s.octave); }
    Slot &take(int id) {
        for (auto &s : m_voices)
            if (s.v.active && s.v.finger == id)
                return s;
        for (auto &s : m_voices)
            if (!s.v.active)
                return s;
        Slot *quietest = &m_voices[0];
        for (auto &s : m_voices)
            if (s.v.level < quietest->v.level)
                quietest = &s;
        return *quietest;
    }

    NotesConfig m_cfg;
    float m_rate = 48000;
    uint64_t m_age = 0;
    std::array<Slot, 16> m_voices;
};

class NotesControl : public PartControl {
public:
    explicit NotesControl(NotesConfig c, QString legend) : m_cfg(c), m_legend(std::move(legend)) {}

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
                const int perOctave = ctx.studio().family == 1 ? 5 : 7;
        const int cells = perOctave * m_cfg.span + 1;
        // Lifts first.
        for (auto &h : m_held) {
            if (h.id < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (still)
                continue;
            Command off;
            off.type = Command::Off;
            off.id = h.id;
            off.t = f.t;
            ctx.send(off);
            h.id = -1;
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Held *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (!h)
                    continue;
                const int cell = std::clamp(int(t.x * float(cells)), 0, cells - 1);
                *h = {t.id, f.t, cell % perOctave, cell / perOctave, false, false, t.y};
                Command on;
                on.type = Command::On;
                on.id = t.id;
                on.t = f.t;
                on.i[0] = h->degree;
                on.i[1] = h->octave;
                on.f[0] = std::clamp(0.35f + 0.8f * t.pressure, 0.2f, 1.0f);
                on.f[1] = t.y;
                ctx.send(on);
                continue;
            }
            if (std::fabs(t.y - h->y) > 0.01f) {
                h->y = t.y;
                Command mv;
                mv.type = Command::Move;
                mv.id = t.id;
                mv.t = f.t;
                mv.f[1] = t.y;
                ctx.send(mv);
            }
            if (!h->long_ && f.t - h->t0 >= 0.25) {
                h->long_ = true;
            }
        }
    }

    void leave(ControlContext &ctx) override {
        for (auto &h : m_held)
            if (h.id >= 0) {
                Command off;
                off.type = Command::Off;
                off.id = h.id;
                ctx.send(off);
                h.id = -1;
            }
        m_frame = Frame{};
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        if (!k.down)
            return false;
        // Number row is the session's; F1–F8 launch this part's slots, and
        // Backspace stops it.
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            ctx.launch(k.key - Qt::Key_F1);
            return true;
        }
        if (k.key == Qt::Key_Backspace) {
            ctx.stop();
            return true;
        }
        if (k.key == Qt::Key_Return || k.key == Qt::Key_Enter)
            return toggleRecording(ctx);
        return false;
    }

    void paint(QPainter &p, const PaintContext &pc) override {
        namespace c = console;
        c::pad(p, pc, m_legend);
        const int perOctave = pc.studio->family == 1 ? 5 : 7;
        const int cells = perOctave * m_cfg.span + 1;
        Tuning tuning;
        tuning.set(pc.studio->root, pc.studio->family, pc.studio->brightness, pc.studio->just);
        for (int k = 0; k < cells; ++k) {
            const QRectF col = pc.rect(float(k) / cells, 0, float(k + 1) / cells, 1);
            if (k % perOctave == 0)
                p.fillRect(col, c::alpha(pc.color, 0.08f));
            const int pc12 = int(std::lround(tuning.baseMidi() + tuning.degreeSemis(k))) % 12;
            c::text(p, QRectF(col.left(), col.bottom() - 22, col.width(), 18),
                    QString::fromUtf8(Tuning::noteName(pc12)), c::alpha(pc.color, 0.6f), 11, true, Qt::AlignCenter);
        }
        c::grid(p, pc, cells, 1, pc.color, 0.15f);
        // What's sounding, live and from clips.
        for (int v = 0; v < 16; ++v) {
            const float kind = pc.view->data[4 * v + 3];
            if (kind <= 0)
                continue;
            const int cell = int(pc.view->data[4 * v + 1]) * perOctave + int(pc.view->data[4 * v]);
            const QRectF col = pc.rect(float(cell) / cells, 0, float(cell + 1) / cells, 1);
            p.fillRect(col, c::alpha(pc.color, std::min(0.5f, 0.1f + pc.view->data[4 * v + 2])));
        }
        for (int i = 0; i < m_frame.count; ++i)
            c::finger(p, pc, m_frame.touches[i], pc.color);
        c::beats(p, pc, pc.color);
        c::clips(p, QRectF(pc.pad.left(), pc.pad.bottom() + 14, pc.pad.width(), 30), *pc.view, pc.color, pc.time);
    }

    QString help() const override {
        return QStringLiteral("touch\ta note of the scale; higher on the pad, brighter\n"
                              "Enter\trecord a loop from the next bar; Enter again closes it\n"
                              "F1–F8\tlaunch a slot at the next bar\n"
                              "Backspace\tstop this part at the next bar");
    }

private:
    struct Held {
        int id = -1;
        double t0 = 0;
        int degree = 0, octave = 0;
        bool long_ = false, looped = false;
        float y = 0;
    };
    Held *find(int id) {
        for (auto &h : m_held)
            if (h.id == id)
                return &h;
        return nullptr;
    }
    NotesConfig m_cfg;
    QString m_legend;
    std::array<Held, 16> m_held{};
    Frame m_frame;
};

// --- drums ------------------------------------------------------------------

class DrumsEngine : public PartEngine {
public:
    void init(float rate) override {
        m_rate = rate;
        for (size_t i = 0; i < m_voices.size(); ++i)
            m_voices[i].rng = Rng(0xBEEF + uint32_t(i) * 104729u);
    }
    void command(const Command &c, AudioContext &ctx) override {
        if (c.type != Command::User)
            return;
        const Drum kind = Drum(std::clamp(c.i[0], 0, int(Drum::Count) - 1));
        const int delay = ctx.offsetOf(c.t, kBlock);
        if (kind == Drum::HatClosed)
            for (auto &d : m_voices)
                if (d.active && d.kind == Drum::HatOpen)
                    d.choke(m_rate);
        DrumVoice *v = &m_voices[0];
        for (auto &d : m_voices)
            if (!d.active) {
                v = &d;
                break;
            }
        float tune = 0.9f + 0.25f * c.f[1];
        float pan = 0;
        if (kind == Drum::Kick) {
            float hz = midiToHz(24.0 + ctx.harmony().tuning.root());
            while (hz < 45.0f)
                hz *= 2.0f;
            tune = hz;
            ctx.pump(delay, 1.0f);
        } else if (kind == Drum::HatClosed) {
            m_alt ^= 1;
            pan = m_alt ? -0.3f : 0.3f;
        } else if (kind == Drum::HatOpen) {
            pan = 0.35f;
        } else if (kind == Drum::Perc) {
            m_alt ^= 1;
            pan = m_alt ? -0.55f : 0.55f;
        }
        static const float level[int(Drum::Count)] = {1.0f, 1.05f, 1.0f, 0.75f, 0.8f, 0.6f};
        v->start(kind, c.f[0], tune, pan, 0.55f * level[int(kind)], delay, m_rate);
        m_flash[int(kind)] = 1.0f;
    }
    void render(float *l, float *r, int n, AudioContext &) override {
        for (auto &d : m_voices)
            if (d.active)
                d.render(l, r, n, m_rate);
        for (float &f : m_flash)
            f *= std::exp(-float(n) / (0.12f * m_rate));
    }
    void silence(bool) override {
        for (auto &d : m_voices)
            d.active = false;
    }
    void publish(float *data) override {
        for (int i = 0; i < int(Drum::Count); ++i)
            data[i] = m_flash[i];
    }
    int voices() const override {
        int n = 0;
        for (auto &d : m_voices)
            n += d.active;
        return n;
    }

private:
    float m_rate = 48000;
    std::array<DrumVoice, 24> m_voices;
    float m_flash[int(Drum::Count)] = {};
    int m_alt = 0;
};

class DrumsControl : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
                constexpr int pieces = int(Drum::Count);
        for (auto &h : m_held) {
            if (h.id < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still) {
                h.id = -1;
            }
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Held *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (!h)
                    continue;
                *h = {t.id, f.t, false};
                const float x = t.x * pieces;
                const int piece = std::clamp(int(x), 0, pieces - 1);
                Command hit;
                hit.type = Command::User;
                hit.flags = Command::Quantize;
                hit.id = t.id;
                hit.t = f.t;
                hit.i[0] = piece;
                hit.f[0] = std::clamp(0.35f + 0.8f * t.pressure, 0.2f, 1.0f);
                hit.f[1] = x - float(piece);
                ctx.send(hit);
                continue;
            }
            if (!h->long_ && f.t - h->t0 >= 0.3) {
                h->long_ = true;
            }
        }
    }
    void leave(ControlContext &) override {
        for (auto &h : m_held)
            h.id = -1;
        m_frame = Frame{};
    }
    bool key(const KeyEvent &k, ControlContext &ctx) override {
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            ctx.launch(k.key - Qt::Key_F1);
            return true;
        }
        if (k.key == Qt::Key_Backspace) {
            ctx.stop();
            return true;
        }
        if (k.key == Qt::Key_Return || k.key == Qt::Key_Enter)
            return toggleRecording(ctx);
        return false;
    }
    void paint(QPainter &p, const PaintContext &pc) override {
        namespace c = console;
        c::pad(p, pc, QStringLiteral("Drums"));
        constexpr int pieces = int(Drum::Count);
        for (int k = 0; k < pieces; ++k) {
            const QRectF zone = pc.rect(float(k) / pieces, 0, float(k + 1) / pieces, 1);
            p.fillRect(zone, c::alpha(pc.color, 0.05f + 0.5f * pc.view->data[k]));
            c::text(p, QRectF(zone.left(), zone.bottom() - 26, zone.width(), 20),
                    QString::fromUtf8(drumName(Drum(k))), pc.color, 13, true, Qt::AlignCenter);
        }
        c::grid(p, pc, pieces, 1, pc.color, 0.3f);
        for (int i = 0; i < m_frame.count; ++i)
            c::finger(p, pc, m_frame.touches[i], pc.color);
        c::beats(p, pc, pc.color);
        c::clips(p, QRectF(pc.pad.left(), pc.pad.bottom() + 14, pc.pad.width(), 30), *pc.view, pc.color, pc.time);
    }
    QString help() const override {
        return QStringLiteral("tap\ta drum: kick, snare, clap, hat, open hat, perc\n"
                              "Enter\trecord a loop from the next bar; Enter again closes it\n"
                              "F1–F8\tlaunch a slot\nBackspace\tstop the beat");
    }

private:
    struct Held {
        int id = -1;
        double t0 = 0;
        bool long_ = false;
    };
    Held *find(int id) {
        for (auto &h : m_held)
            if (h.id == id)
                return &h;
        return nullptr;
    }
    std::array<Held, 16> m_held{};
    Frame m_frame;
};

}
