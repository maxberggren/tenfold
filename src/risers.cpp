// FX: the transitions desk. Everything that makes a build-up build and a drop
// hit, played with ten fingers.
//
// The pad is two instruments. The upper four fifths are the build field:
// five columns of risers (noise, pitch, saw, roll, swell) and five rows of
// lengths, 1, 2, 4, 8 and 16 bars from the bottom up. A finger that lands
// there starts that riser so it peaks exactly on a downbeat: the drop. Once
// a drop is coming, every further finger aims at the same one, and its row
// says when it comes in (the 1-bar row enters one bar before the drop), so
// a whole build can be stacked layer by layer and lands as one. Sliding up
// makes a riser bigger and brighter, sliding sideways moves its pitch or
// voicing in the key, pressure swells it, a twist sets it moving and a flat
// finger adds grit. A quick flick down as the finger leaves pulls that riser
// out: it falls away instead of rising.
//
// The bottom fifth is the hit row: impact, sub drop, downlifter, laser zap,
// power-down, echo throw, reverb wash and the DJ sweep. Taps there are
// rhythms, and tap-then-hold loops them like every other page.
//
// Pressing the pad until it clicks (or Return) is the drop gesture: the
// drop comes on the next bar line, and whatever is rising rushes into it.
// On the drop the engine fires the impact and the sub on the exact sample,
// while this page moves the whole mix around it: a high-pass that climbs the
// master through the build, a little drive, a bigger room, and, depending on
// the drop style, a half-beat gap or drums and bass stripped out for the last
// bar. All of it snaps back on the ONE, to exactly what it was.

#include "risers.h"

#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <array>
#include <cmath>
#include <cstring>

#include "console.h"
#include "parts.h"
#include "phrase.h"

namespace tf {

namespace {

using namespace risers;

QColor alphaOf(QColor c, float a) { return console::alpha(c, a); }

constexpr float kHitTop = 0.2f;                      // the hit row is the bottom fifth
constexpr int kRows = 5;                              // 1, 2, 4, 8, 16 bars
constexpr float kRowH = (1.0f - kHitTop) / kRows;
constexpr double kDecide = 0.12;                      // a riser finger waits this long for a click
constexpr int kKeyId = 1 << 20;                       // ids of notes played from the keyboard
constexpr int kMods = MasterCount + kMaxParts * ChannelCount;

enum Style { Clean = 0, Gap, Strip, StyleCount };
const char *styleName(int s) {
    static const char *names[StyleCount] = {"clean", "gap", "strip"};
    return names[std::clamp(s, 0, int(StyleCount) - 1)];
}

const char *kHitKeys = "ZXCVBNM,";
const char *kRiserKeys = "ASDFG";

int rowBars(int row) { return 1 << std::clamp(row, 0, kRows - 1); }

class FxPage : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        m_phrase.expire(f.t);
        const double beat = ctx.beatAt(f.t);

        // The click: the drop comes at the next bar line. A riser finger
        // that landed just before is the finger doing the clicking, not a
        // riser.
        if (f.button && !m_button) {
            for (Finger &h : m_fingers)
                if (h.id >= 0 && !h.sent && isRiser(h.kind))
                    h.dropFinger = true;
            sendDrop(f.t, ctx);
        }
        m_button = f.button;

        // Lifts first.
        for (Finger &h : m_fingers) {
            if (h.id < 0 || h.isKey)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still)
                lift(h, f.t, ctx);
        }

        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Finger *h = find(t.id);
            if (!h) {
                h = find(-1);
                if (!h)
                    continue;
                land(*h, t, f.t, beat, ctx);
                continue;
            }
            track(*h, t, f.t);
            if (!h->sent && !h->dropFinger && f.t - h->t0 >= kDecide)
                sendOn(*h, ctx);
            if (h->sent)
                sendMove(*h, f.t, ctx);
            if (!isRiser(h->kind) && !h->long_ && f.t - h->t0 >= 0.3) {
                h->long_ = true;
                double start = 0;
                if (m_phrase.closes(h->t0, 0, &start)) {
                    const int slot = std::max(0, ctx.freeSlot());
                    ctx.capture(slot, start, h->t0);
                    ctx.notice(QStringLiteral("FX loop in slot %1").arg(slot + 1));
                    h->looped = true;
                    m_phrase.clear();
                }
            }
        }
    }

    void background(double t, ControlContext &ctx) override { automate(t, ctx); }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        if (k.repeat)
            return true;
        if (k.down && k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            ctx.launch(k.key - Qt::Key_F1);
            return true;
        }
        const double t = ctx.now();
        if (!k.down) {
            if (Finger *h = find(kKeyId + k.key)) {
                lift(*h, t, ctx);
                return true;
            }
            return false;
        }
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            sendDrop(t, ctx);
            return true;
        case Qt::Key_Q: {
            Command c;
            c.type = CmdFall;
            c.id = kEveryone;
            c.t = t;
            ctx.send(c);
            m_localDrop = -1;
            ctx.notice(QStringLiteral("The build falls away"));
            return true;
        }
        case Qt::Key_Up:
        case Qt::Key_Down:
            m_keyRow = std::clamp(m_keyRow + (k.key == Qt::Key_Up ? 1 : -1), 0, kRows - 1);
            ctx.notice(QStringLiteral("Keys play %1-bar risers").arg(rowBars(m_keyRow)));
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
            m_style = (m_style + (k.key == Qt::Key_Right ? 1 : StyleCount - 1)) % StyleCount;
            ctx.notice(QStringLiteral("Drop style: %1").arg(QString::fromUtf8(styleName(m_style)).toUpper()));
            return true;
        case Qt::Key_W:
            m_buildMix = !m_buildMix;
            ctx.notice(m_buildMix ? QStringLiteral("Builds move the mix") : QStringLiteral("Builds leave the mix alone"));
            return true;
        case Qt::Key_R:
            if (ctx.studio().parts[ctx.part()].recording >= 0) {
                ctx.stopRecording();
                ctx.notice(QStringLiteral("Recording stops at the bar"));
            } else {
                const int slot = std::max(0, ctx.freeSlot());
                ctx.record(slot, 8);
                ctx.notice(QStringLiteral("Recording FX into slot %1 from the next bar").arg(slot + 1));
            }
            return true;
        default:
            break;
        }
        // Letters: the home row starts risers, the bottom row fires hits,
        // each held for as long as the key is.
        int kind = -1;
        if (k.key > 0 && k.key < 0x80) {
            const char ch = char(QChar(k.key).toUpper().toLatin1());
            if (const char *at = std::strchr(kRiserKeys, ch))
                kind = int(at - kRiserKeys);
            else if (const char *hit = std::strchr(kHitKeys, ch))
                kind = kRisers + int(hit - kHitKeys);
        }
        if (kind < 0)
            return false;
        Finger *h = find(-1);
        if (!h || find(kKeyId + k.key))
            return true;
        Touch touch;
        touch.id = kKeyId + k.key;
        touch.pressure = 0.55f;
        touch.size = 1;
        if (isRiser(kind)) {
            touch.x = (kind + 0.5f) / kRisers;
            touch.y = kHitTop + (m_keyRow + 0.5f) * kRowH;
        } else {
            touch.x = (kind - kRisers + 0.5f) / kHits;
            touch.y = kHitTop * 0.6f;
        }
        land(*h, touch, t, ctx.beatAt(t), ctx);
        h->isKey = true;
        if (!h->sent)
            sendOn(*h, ctx);
        return true;
    }

    void leave(ControlContext &ctx) override {
        // Fingers and keys let go of what they hold: hits stop, mix moves
        // return. Risers already launched keep rising to their drop, as a
        // struck cymbal keeps ringing.
        const double t = ctx.now();
        for (Finger &h : m_fingers)
            if (h.id >= 0)
                lift(h, t, ctx, false);
        m_frame = Frame{};
        m_button = false;
    }

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "land in the build field\ta riser: the column is the sound (noise, pitch, saw, roll, swell), "
            "the row its length (1, 2, 4, 8, 16 bars up the pad); it peaks on the downbeat\n"
            "more fingers\tstack layers on the same drop; each enters its row's length before it\n"
            "slide up / sideways\tbigger and brighter / pitch or voicing, always in the key\n"
            "press, twist, flatten\tswell it / set it moving / add grit\n"
            "flick down as you lift\tpull that riser out: it falls away\n"
            "click the pad, Return\tdrop on the next bar line: everything rising rushes into it\n"
            "bottom row\timpact, sub drop, downlifter, laser zap, power-down, echo throw, reverb wash, DJ sweep\n"
            "hold a hit\tsub sustains, zaps fire sixteenths up the chord, throw and wash hold; higher in a cell is bigger\n"
            "hold the sweep\tslide left to close a low-pass on the whole mix, up to open a high-pass; press for drive\n"
            "tap a rhythm on the bottom row, hold the next beat\tloops it into a free slot\n"
            "A S D F G\tnoise, pitch, saw, roll, swell risers; Up / Down sets their length\n"
            "Z X C V B N M ,\tthe bottom row's hits, held as long as the key\n"
            "Left / Right\tdrop style: clean, a half-beat gap, or drums and bass out for the last bar\n"
            "Q\tthe whole build falls away\n"
            "W\tbuilds move the mix (high-pass, room, and drive when pushed) or leave it alone\n"
            "R\trecord FX from the next bar (again to stop)\n"
            "F1–F8\tlaunch a slot at the next bar\n"
            "Backspace\tstop the FX clip at the next bar");
    }

    QString status() const override {
        QStringList parts;
        for (const Finger &h : m_fingers)
            if (h.id >= 0 && !h.dropFinger) {
                if (isRiser(h.kind))
                    parts << QStringLiteral("%1 %2").arg(QString::fromUtf8(kindName(h.kind)).toUpper()).arg(h.bars);
                else
                    parts << QString::fromUtf8(kindName(h.kind)).toUpper();
            }
        QString s = parts.join(QStringLiteral(" + "));
        if (m_statusDrop > m_statusBeat) {
            const double left = m_statusDrop - m_statusBeat;
            const QString when = left >= 4 ? QStringLiteral("%1 bars").arg(left / 4.0, 0, 'f', 1)
                                            : QStringLiteral("%1 beats").arg(left, 0, 'f', 1);
            s += (s.isEmpty() ? QString() : QStringLiteral("  →  ")) + QStringLiteral("drop in ") + when;
        }
        if (s.isEmpty())
            s = QStringLiteral("land a riser to build; click to drop");
        if (m_style != Clean)
            s += QStringLiteral("  ·  %1").arg(QString::fromUtf8(styleName(m_style)));
        return s;
    }

private:
    struct Finger {
        int id = -1;
        int kind = 0, bars = 1, flags = 0;
        double t0 = 0, drop = -1;
        float x0 = 0, y0 = 0, a0 = 0;
        float x = 0, y = 0, size = 1, press = 0.5f, angle = 0;
        float int0 = 0.6f, tone0 = 0;
        float tone = 0, intensity = 0.6f, motion = 0, grit = 0;
        bool sent = false, dropFinger = false, long_ = false, looped = false, isKey = false;
        float last[5] = {-9, -9, -9, -9, -9};
        double lastSend = 0;
        double ht[8] = {};
        float hy[8] = {};
        int hn = 0;
    };

    Finger *find(int id) {
        for (Finger &h : m_fingers)
            if (h.id == id)
                return &h;
        return nullptr;
    }

    const float *data(ControlContext &ctx) const { return ctx.studio().parts[ctx.part()].data; }

    // The drop new risers aim at: the engine's (it may come from a clip),
    // or the one this page has just asked for and the engine hasn't shown.
    double pendingDrop(ControlContext &ctx, double beat) const {
        const double published = data(ctx)[pub::Drop];
        if (published > beat)
            return published;
        return m_localDrop > beat ? m_localDrop : -1;
    }

    void land(Finger &h, const Touch &t, double time, double beat, ControlContext &ctx) {
        h = Finger{};
        h.id = t.id;
        h.t0 = time;
        h.x0 = h.x = t.x;
        h.y0 = h.y = t.y;
        h.a0 = h.angle = t.angle;
        h.size = t.size;
        h.press = t.pressure;
        h.ht[0] = time;
        h.hy[0] = t.y;
        h.hn = 1;
        h.grit = clampf((h.size - 1.25f) / 0.8f, 0, 1);
        if (t.y < kHitTop) {
            const float cell = t.x * kHits;
            h.kind = kRisers + std::clamp(int(cell), 0, kHits - 1);
            // Higher in the cell, bigger. Across a zap's cell, a few steps
            // of the scale either side of the chord's root.
            h.int0 = h.intensity = clampf(0.35f + 0.65f * t.y / kHitTop, 0, 1);
            if (h.kind == Zap)
                h.tone0 = h.tone = clampf((cell - std::floor(cell) - 0.5f) * 0.9f, -1, 1);
            sendOn(h, ctx);
            return;
        }
        h.kind = std::clamp(int(t.x * kRisers), 0, kRisers - 1);
        const int row = std::clamp(int((t.y - kHitTop) / kRowH), 0, kRows - 1);
        h.bars = rowBars(row);
        h.int0 = h.intensity = 0.6f;
        // Aim: at the drop that is coming, entering this row's length before
        // it; or, with none coming, at the bar line this many bars away,
        // which becomes the drop.
        const double pending = pendingDrop(ctx, beat + 0.25);
        if (pending > 0) {
            h.drop = pending;
            h.flags = FlagEnterLate;
        } else {
            double d = nearestBar(beat + 4.0 * h.bars);
            if (d < beat + 2.0)
                d += kBeatsPerBar;
            h.drop = d;
            h.flags = 0;
            m_localDrop = d;
        }
    }

    void track(Finger &h, const Touch &t, double time) {
        h.x = t.x;
        h.y = t.y;
        h.size = t.size;
        h.press = t.pressure;
        h.angle = t.angle;
        h.ht[h.hn % 8] = time;
        h.hy[h.hn % 8] = t.y;
        ++h.hn;
        h.tone = clampf(h.tone0 + (h.x - h.x0) * 3.0f, -1, 1);
        // The sweep lives in the pad's bottom corner: left closes the
        // low-pass, up (or right) opens the high-pass.
        if (h.kind == Sweep)
            h.tone = clampf((h.x - h.x0 + h.y - h.y0) * 2.5f, -1, 1);
        h.intensity = clampf(h.int0 + (h.y - h.y0) * 2.2f, 0, 1);
        // An ellipse's angle repeats every half turn.
        float twist = h.angle - h.a0;
        while (twist > kPi / 2)
            twist -= kPi;
        while (twist < -kPi / 2)
            twist += kPi;
        h.motion = clampf(std::fabs(twist) / 0.7f, 0, 1);
        h.grit = clampf((h.size - 1.25f) / 0.8f, 0, 1);
    }

    void sendOn(Finger &h, ControlContext &ctx) {
        Command c;
        c.type = Command::On;
        c.id = h.id;
        c.t = h.t0;
        c.i[0] = h.kind;
        c.i[1] = h.bars;
        c.i[2] = h.flags;
        c.f[0] = h.press;
        c.f[1] = h.tone;
        c.f[2] = h.intensity;
        c.f[3] = h.motion;
        c.f[4] = h.grit;
        c.f[5] = isRiser(h.kind) ? float(h.drop - ctx.beatAt(h.t0)) : 0.0f;
        ctx.send(c);
        h.sent = true;
        h.last[0] = h.press;
        h.last[1] = h.tone;
        h.last[2] = h.intensity;
        h.last[3] = h.motion;
        h.last[4] = h.grit;
        h.lastSend = h.t0;
    }

    // Moves go out only on a real change, at most sixty times a second.
    void sendMove(Finger &h, double t, ControlContext &ctx) {
        if (t - h.lastSend < 1.0 / 60.0)
            return;
        const float now[5] = {h.press, h.tone, h.intensity, h.motion, h.grit};
        bool changed = false;
        for (int k = 0; k < 5; ++k)
            changed |= std::fabs(now[k] - h.last[k]) > 0.006f;
        if (!changed)
            return;
        Command c;
        c.type = Command::Move;
        c.id = h.id;
        c.t = t;
        for (int k = 0; k < 5; ++k) {
            c.f[k] = now[k];
            h.last[k] = now[k];
        }
        ctx.send(c);
        h.lastSend = t;
    }

    void lift(Finger &h, double t, ControlContext &ctx, bool phrase = true) {
        if (h.dropFinger) {
            h.id = -1;
            return;
        }
        // A quick tap on a riser launches it all the same.
        if (!h.sent)
            sendOn(h, ctx);
        bool flick = false;
        if (isRiser(h.kind) && h.hn >= 3 && !h.isKey) {
            // How fast it was moving down in its last tenth of a second.
            const int last = (h.hn - 1) % 8;
            int back = last;
            for (int k = 1; k < std::min(h.hn, 8); ++k) {
                const int idx = (h.hn - 1 - k) % 8;
                back = idx;
                if (h.ht[last] - h.ht[idx] >= 0.08)
                    break;
            }
            const double dt = h.ht[last] - h.ht[back];
            if (dt > 0.02) {
                const double vy = (h.hy[last] - h.hy[back]) / dt;
                flick = vy < -1.6 && h.y0 - h.y > 0.05;
            }
        }
        Command c;
        c.type = flick ? uint16_t(CmdFall) : uint16_t(Command::Off);
        c.id = h.id;
        c.t = t;
        ctx.send(c);
        if (flick) {
            ctx.notice(QStringLiteral("%1 falls away").arg(QString::fromUtf8(kindName(h.kind)).toUpper()));
            bool others = false;
            for (const Finger &o : m_fingers)
                others |= o.id >= 0 && o.id != h.id && isRiser(o.kind) && o.sent;
            if (!others && data(ctx)[pub::Layers] <= 1)
                m_localDrop = -1;
        }
        if (phrase && !isRiser(h.kind) && !h.long_ && !h.isKey)
            m_phrase.tap(h.t0, t - h.t0, 0);
        h.id = -1;
    }

    void sendDrop(double t, ControlContext &ctx) {
        Command c;
        c.type = CmdDrop;
        c.t = t;
        ctx.send(c);
        const double beat = ctx.beatAt(t);
        const double target = std::ceil((beat - 0.25) / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        const double pending = pendingDrop(ctx, beat);
        m_localDrop = pending > 0 ? std::min(pending, target) : target;
        ctx.notice(QStringLiteral("DROP on the next ONE"));
    }

    // --- the mix -------------------------------------------------------------

    struct Mod {
        bool owned = false;
        float base = 0, sent = -1;
        double released = -100;
        float w = 0, wt = 0;
    };

    static int ch(int part, int c) { return MasterCount + part * ChannelCount + c; }

    void pull(int idx, float target, float weight) {
        if (weight <= 1e-4f)
            return;
        m_mods[idx].w += weight;
        m_mods[idx].wt += weight * target;
    }

    // Every moment, what the page wants of the mix, from what the engine
    // says is happening: so a build or a throw replayed from a clip moves
    // the mix just as it did live.
    void automate(double t, ControlContext &ctx) {
        const StudioView &sv = ctx.studio();
        const float *d = data(ctx);
        const double beat = ctx.beatAt(t);
        const double bps = sv.bpm / 60.0;
        // Smoothed by the studio over 30 ms, the filter and drive must start
        // home a little before the ONE to be all the way there on it; a mute
        // lifts in two.
        const double lead = 0.075 * bps, muteLead = 0.012 * bps;
        const int parts = std::min(sv.partCount, kMaxParts);
        for (Mod &m : m_mods)
            m.w = m.wt = 0;

        const double drop = d[pub::Drop];
        m_statusDrop = drop;
        m_statusBeat = beat;
        if (drop > 0) {
            const double start = d[pub::Start];
            const float I = d[pub::Intensity];
            if (m_buildMix && beat < drop - lead && beat >= start) {
                const float p = clampf(float((beat - start) / std::max(0.25, drop - start)), 0, 1);
                // Longer and harder builds climb further: a one-bar build to
                // about 130 Hz, four bars at full tilt to about 650.
                const float depth = clampf(float(drop - start) / 8.0f, 0.55f, 1.0f) * (0.7f + 0.3f * I);
                pull(MasterFilter, 0.5f + 0.26f * depth, std::pow(p, 2.2f));
                // Drive for the last stretch of a build pushed past its
                // default (a finger slid up): it is the climax that wants it,
                // and the bus saturator is the studio's dearest stage, not
                // to be lit by every riser.
                const float last = clampf(float(1.0 - (drop - beat) / std::min(4.0, 0.25 * (drop - start))), 0, 1);
                const float push = clampf((I - 0.65f) / 0.3f, 0, 1);
                pull(MasterDrive, 0.1f + 0.12f * push, last * last * push);
                pull(RoomSize, 0.72f, 0.6f * p * depth);
            }
            if (beat < drop - muteLead) {
                if (m_style == Gap && beat >= drop - 0.5)
                    for (int part = 0; part < parts; ++part)
                        pull(ch(part, Mute), 1, 1);
                if (m_style == Strip && drop - start >= 4 && beat >= drop - kBeatsPerBar)
                    for (int part = 0; part < std::min(parts, 2); ++part)
                        pull(ch(part, Mute), 1, 1);
            }
        }

        // The power-down: the mix darkens and sinks until its bar line.
        const double s0 = d[pub::StopStart], s1 = d[pub::StopEnd];
        if (s1 > 0 && beat < s1 - lead && beat >= s0) {
            const float u = clampf(float((beat - s0) / std::max(0.25, s1 - s0)), 0, 1);
            pull(MasterFilter, 0.03f, std::pow(u, 0.6f));
            pull(MasterGain, 0.12f, u * u);
        }

        // Echo throw: every part into the tape echo, the feedback up so it
        // rings out after.
        if (const float a = d[pub::Throw]; a > 0)
            for (int part = 0; part < parts; ++part)
                pull(ch(part, EchoSend), 0.8f, a);
        if (const float tail = d[pub::ThrowTail]; tail > 0)
            pull(EchoFeedback, 0.45f + 0.45f * d[pub::ThrowFeedback], tail);

        // Reverb wash.
        if (const float w = d[pub::Wash]; w > 0) {
            for (int part = 0; part < parts; ++part)
                pull(ch(part, ReverbSend), 0.85f, w);
            pull(RoomSize, 0.9f, w);
            pull(RoomShimmer, 0.15f + 0.5f * d[pub::WashShimmer], w);
        }

        // The DJ sweep: left of where the finger landed closes a low-pass,
        // right opens a high-pass.
        if (const float s = d[pub::Sweep]; std::fabs(s) > 0.002f)
            pull(MasterFilter, 0.5f + 0.47f * s, 1);
        if (const float sd = d[pub::SweepDrive]; sd > 0)
            pull(MasterDrive, 0.5f, sd);

        apply(t, ctx);
    }

    // Takes over what is pulled (remembering what it was) and hands back,
    // exactly, what no longer is.
    void apply(double t, ControlContext &ctx) {
        const StudioView &sv = ctx.studio();
        for (int idx = 0; idx < kMods; ++idx) {
            Mod &m = m_mods[idx];
            const bool master = idx < MasterCount;
            const int part = master ? -1 : (idx - MasterCount) / ChannelCount;
            const int c = master ? idx : (idx - MasterCount) % ChannelCount;
            if (!master && part >= sv.partCount)
                continue;
            auto send = [&](float v) {
                if (master)
                    ctx.setMaster(c, v);
                else
                    ctx.setChannel(part, c, v);
                m.sent = v;
            };
            if (m.w > 1e-4f) {
                if (!m.owned) {
                    // Just handed back, the studio's view may still show our
                    // own last value: keep the base we had.
                    if (t - m.released > 0.4)
                        m.base = master ? sv.master[c] : sv.parts[part].channel.value[c];
                    m.owned = true;
                    m.sent = -1;
                }
                float v = m.w > 1 ? m.wt / m.w : m.base * (1 - m.w) + m.wt;
                if (!master && c == Mute)
                    v = v >= 0.5f ? 1.0f : m.base;
                v = clampf(v, 0, 1);
                if (std::fabs(v - m.sent) > 0.0015f)
                    send(v);
            } else if (m.owned) {
                send(m.base);
                m.owned = false;
                m.released = t;
            }
        }
    }

    std::array<Finger, 24> m_fingers{};
    Mod m_mods[kMods];
    Phrase m_phrase;
    Frame m_frame;
    bool m_button = false;
    bool m_buildMix = true;
    int m_style = Clean;
    int m_keyRow = 2; // keys play 4-bar risers until Up or Down says otherwise
    double m_localDrop = -1;
    double m_statusDrop = -1, m_statusBeat = 0;
};

// --- drawing -------------------------------------------------------------------

// A small picture of each sound's shape over time, left to right: what the
// column or cell will do before a finger asks it to.
void glyph(QPainter &p, const QRectF &r, int kind, QColor col) {
    QPainterPath path;
    auto at = [&](double u, double v) { return QPointF(r.left() + u * r.width(), r.bottom() - v * r.height()); };
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(col, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (kind) {
    case Noise: // a hiss that swells and brightens
        for (int i = 0; i < 28; ++i) {
            const double u = (i + 0.5) / 28.0, h = std::pow(u, 2.0) * (0.55 + 0.45 * std::fabs(std::sin(i * 2.3)));
            p.drawLine(at(u, 0.5 - h / 2), at(u, 0.5 + h / 2));
        }
        return;
    case Pitch: // a glide up, its vibrato quickening
        for (int i = 0; i <= 60; ++i) {
            const double u = i / 60.0, v = 0.05 + 0.85 * std::pow(u, 1.5) + 0.06 * u * std::sin(u * u * 60.0);
            i ? path.lineTo(at(u, v)) : path.moveTo(at(u, v));
        }
        break;
    case Saw: // stabs coming faster and taller
    case Roll: { // hits coming faster
        double u = 0;
        const double steps[4] = {0.125, 0.0625, 0.03125, 0.0156};
        while (u < 0.98) {
            const int part = u < 0.5 ? 0 : u < 0.75 ? 1 : u < 0.875 ? 2 : 3;
            const double h = 0.2 + 0.8 * u;
            if (kind == Saw) {
                p.drawLine(at(u, 0.05), at(u, 0.05 + h * 0.9));
                p.drawLine(at(u, 0.05 + h * 0.9), at(u + steps[part] * 0.7, 0.05 + h * 0.25));
            } else {
                p.setBrush(col);
                p.drawEllipse(at(u, 0.5), 1.0 + 2.0 * h, 1.0 + 2.0 * h);
                p.setBrush(Qt::NoBrush);
            }
            u += steps[part];
        }
        return;
    }
    case Swell: // a sound played backwards: nothing, then all at once
        for (int i = 0; i <= 50; ++i) {
            const double u = i / 50.0, v = std::exp((u - 1.0) * 4.5);
            i ? path.lineTo(at(u, 0.5 + 0.48 * v)) : path.moveTo(at(u, 0.5 + 0.48 * v));
        }
        for (int i = 50; i >= 0; --i) {
            const double u = i / 50.0, v = std::exp((u - 1.0) * 4.5);
            path.lineTo(at(u, 0.5 - 0.48 * v));
        }
        p.setBrush(alphaOf(col, 0.25f));
        break;
    case Impact: // a hit and a long tail
        path.moveTo(at(0, 0.5));
        path.lineTo(at(0.04, 0.98));
        for (int i = 1; i <= 40; ++i) {
            const double u = 0.04 + 0.96 * i / 40.0;
            path.lineTo(at(u, 0.5 + 0.48 * std::exp(-(u - 0.04) * 4.0)));
        }
        for (int i = 40; i >= 0; --i) {
            const double u = 0.04 + 0.96 * i / 40.0;
            path.lineTo(at(u, 0.5 - 0.48 * std::exp(-(u - 0.04) * 4.0)));
        }
        path.closeSubpath();
        p.setBrush(alphaOf(col, 0.25f));
        break;
    case Sub: // a dive onto the root
    case Down: // falling away
        for (int i = 0; i <= 40; ++i) {
            const double u = i / 40.0;
            const double v = kind == Sub ? 0.15 + 0.75 * (1 - u) * (1 - u) * (1 - u) : 0.95 - 0.85 * std::pow(u, 0.6);
            i ? path.lineTo(at(u, v)) : path.moveTo(at(u, v));
        }
        break;
    case Zap: // pew, pew, pew
        for (int k = 0; k < 3; ++k) {
            QPainterPath z;
            for (int i = 0; i <= 16; ++i) {
                const double u = i / 16.0, x = 0.04 + k * 0.33 + u * 0.26, v = 0.2 + 0.75 * std::exp(-u * 5.0) + 0.1 * k;
                i ? z.lineTo(at(x, v)) : z.moveTo(at(x, v));
            }
            p.drawPath(z);
        }
        return;
    case Stop: // a wave slowing down and sinking
        for (int i = 0; i <= 120; ++i) {
            const double u = i / 120.0, ph = 30.0 * (u - 0.5 * u * u), a = 0.45 * (1 - u * u);
            const double v = 0.5 + a * std::sin(ph);
            i ? path.lineTo(at(u, v)) : path.moveTo(at(u, v));
        }
        break;
    case Throw: // repeats, each one quieter
        for (int k = 0; k < 6; ++k) {
            const double u = 0.06 + k * 0.17, h = 0.9 * std::pow(0.68, k);
            p.drawLine(at(u, 0.5 - h / 2), at(u, 0.5 + h / 2));
        }
        return;
    case Wash: // a cloud
        for (int k = 0; k < 4; ++k) {
            const QRectF e(at(0.1 + k * 0.2, 0.75), at(0.45 + k * 0.2, 0.1));
            p.drawEllipse(e.normalized());
        }
        return;
    case Sweep: // a filter's slope, moving
        path.moveTo(at(0, 0.7));
        path.lineTo(at(0.45, 0.7));
        path.quadTo(at(0.6, 0.85), at(0.68, 0.55));
        path.lineTo(at(0.85, 0.05));
        p.drawPath(path);
        p.drawLine(at(0.3, 0.2), at(0.95, 0.2));
        p.drawLine(at(0.95, 0.2), at(0.88, 0.3));
        p.drawLine(at(0.3, 0.2), at(0.37, 0.3));
        return;
    default:
        return;
    }
    p.drawPath(path);
}

void FxPage::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const float *d = pc.view->data;
    const StudioView &sv = *pc.studio;
    const double beat = pc.beat;
    c::pad(p, pc, QStringLiteral("FX"));
    p.setRenderHint(QPainter::Antialiasing, true);

    const double drop = d[pub::Drop];
    const double start = d[pub::Start];
    const float flash = d[pub::Flash];
    const qreal unit = pc.pad.height();

    // The build field's grid: columns are sounds, rows lengths.
    static const char *columns[kRisers] = {"NOISE", "PITCH", "SAW", "ROLL", "SWELL"};
    for (int k = 0; k < kRisers; k += 2)
        p.fillRect(pc.rect(float(k) / kRisers, kHitTop, float(k + 1) / kRisers, 1), c::alpha(col, 0.035f));
    p.setPen(QPen(c::alpha(col, 0.18f), 1));
    for (int k = 1; k < kRisers; ++k)
        p.drawLine(pc.at(float(k) / kRisers, kHitTop), pc.at(float(k) / kRisers, 1));
    p.setPen(QPen(c::alpha(col, 0.1f), 1, Qt::DotLine));
    for (int r = 1; r < kRows; ++r)
        p.drawLine(pc.at(0, kHitTop + r * kRowH), pc.at(1, kHitTop + r * kRowH));
    // Each riser climbs its column, reaching the top on the drop.
    int perColumn[kRisers] = {}, seen[kRisers] = {};
    for (int v = 0; v < pub::MaxVoices; ++v) {
        const float *o = d + pub::Voices + pub::Stride * v;
        if (o[pub::VKind] >= 0 && o[pub::VKind] < kRisers)
            ++perColumn[int(o[pub::VKind])];
    }
    for (int v = 0; v < pub::MaxVoices; ++v) {
        const float *o = d + pub::Voices + pub::Stride * v;
        const int kind = int(o[pub::VKind]);
        if (o[pub::VKind] < 0 || kind >= kRisers)
            continue;
        const int lane = seen[kind]++, lanes = std::max(1, perColumn[kind]);
        const float x0 = float(kind) / kRisers, w = 1.0f / kRisers;
        const float lx0 = x0 + w * (0.08f + 0.84f * lane / lanes);
        const float lx1 = x0 + w * (0.08f + 0.84f * (lane + 1) / lanes) - 0.004f;
        const int state = int(o[pub::VState]);
        float prog = o[pub::VProgress];
        // Between published frames, run on the clock.
        if (state == Running && o[pub::VEnd] > o[pub::VStart])
            prog = clampf(float((beat - o[pub::VStart]) / (o[pub::VEnd] - o[pub::VStart])), 0, 1);
        const QColor tint = state == Falling ? QColor(255, 255, 255, 140) : col;
        if (state == Waiting) {
            const int row = std::clamp(int(std::log2(std::max(1.0f, o[pub::VBars]))), 0, kRows - 1);
            const QRectF r = pc.rect(lx0, kHitTop, lx1, kHitTop + (row + 1) * kRowH);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c::alpha(col, 0.55f), 1.5, Qt::DashLine));
            p.drawRect(r);
            const double in = o[pub::VStart] - beat;
            c::text(p, QRectF(r.left(), r.top() + 6, r.width(), 16),
                    in >= 4 ? QStringLiteral("IN %1 BARS").arg(std::ceil(in / 4.0 - 1e-6))
                            : QStringLiteral("IN %1 BEATS").arg(std::ceil(in - 1e-6)),
                    col, 10, true, Qt::AlignCenter);
            continue;
        }
        const float top = kHitTop + (1 - kHitTop) * prog;
        const QRectF fill = pc.rect(lx0, kHitTop, lx1, top);
        QLinearGradient g(fill.bottomLeft(), fill.topLeft());
        g.setColorAt(0, c::alpha(tint, 0.05f));
        g.setColorAt(1, c::alpha(tint, 0.25f + std::min(0.5f, o[pub::VLevel] * 2.0f)));
        p.fillRect(fill, g);
        p.setPen(QPen(tint, 3));
        p.drawLine(QPointF(fill.left(), fill.top()), QPointF(fill.right(), fill.top()));
        if (o[pub::VWho] > 1.5f)
            c::text(p, QRectF(fill.left(), fill.top() - 18, fill.width(), 14), QStringLiteral("CLIP"), c::alpha(tint, 0.7f), 9,
                    true, Qt::AlignCenter);
    }

    // Row lengths down the left edge; with a drop coming, when each row
    // would come in.
    for (int r = 0; r < kRows; ++r) {
        const QRectF cell = pc.rect(0, kHitTop + r * kRowH, 1.0f / kRisers, kHitTop + (r + 1) * kRowH);
        const int bars = rowBars(r);
        QString label = bars == 1 ? QStringLiteral("1 BAR") : QStringLiteral("%1 BARS").arg(bars);
        if (drop > beat) {
            const double enter = drop - 4.0 * bars;
            label = QStringLiteral("%1 · %2").arg(bars).arg(
                enter <= beat ? QStringLiteral("NOW") : QStringLiteral("IN %1").arg(std::ceil((enter - beat) / 4.0 - 1e-6)));
        }
        c::text(p, QRectF(cell.left() + 8, cell.bottom() - 15, cell.width(), 13), label, c::alpha(col, 0.45f), 10, true);
    }
    for (int k = 0; k < kRisers; ++k) {
        const QRectF head = pc.rect(float(k) / kRisers, 1 - kRowH, float(k + 1) / kRisers, 1);
        c::text(p, QRectF(head.left(), head.top() + 42, head.width(), 18), QString::fromUtf8(columns[k]),
                c::alpha(col, 0.85f), 13, true, Qt::AlignCenter);
        const qreal gw = std::min<qreal>(84, head.width() * 0.6);
        glyph(p, QRectF(head.center().x() - gw / 2, head.top() + 61, gw, 14), k, c::alpha(col, 0.55f));
    }

    // The hit row.
    static const char *hits[kHits] = {"IMPACT", "SUB DROP", "DOWNLIFT", "ZAP", "STOP", "THROW", "WASH", "SWEEP"};
    p.setPen(QPen(c::alpha(col, 0.6f), 1.5));
    p.drawLine(pc.at(0, kHitTop), pc.at(1, kHitTop));
    for (int k = 0; k < kHits; ++k) {
        const QRectF cell = pc.rect(float(k) / kHits, 0, float(k + 1) / kHits, kHitTop);
        float lit = d[pub::Hits + k];
        if (k == Throw - kRisers)
            lit = std::max(lit, d[pub::Throw]);
        if (k == Wash - kRisers)
            lit = std::max(lit, d[pub::Wash]);
        if (k == Sweep - kRisers)
            lit = std::max(lit, std::fabs(d[pub::Sweep]));
        if (k == Stop - kRisers && d[pub::StopEnd] > 0)
            lit = std::max(lit, 0.6f);
        lit = clampf(lit, 0, 1);
        p.fillRect(cell.adjusted(1, 1, -1, -1), c::alpha(col, 0.04f + 0.6f * lit));
        if (k > 0) {
            p.setPen(QPen(c::alpha(col, 0.25f), 1));
            p.drawLine(cell.topLeft(), cell.bottomLeft());
        }
        c::text(p, QRectF(cell.left(), cell.center().y() - 2, cell.width(), 18), QString::fromUtf8(hits[k]),
                lit > 0.5f ? QColor::fromRgb(c::kInk) : col, 12, true, Qt::AlignCenter);
        const qreal gw = std::min<qreal>(56, cell.width() * 0.5);
        glyph(p, QRectF(cell.center().x() - gw / 2, cell.center().y() - 24, gw, 18), kRisers + k,
              lit > 0.5f ? QColor::fromRgb(c::kInk) : c::alpha(col, 0.6f));
        c::text(p, QRectF(cell.left() + 6, cell.top() + 4, 30, 14), QString(QChar(kHitKeys[k])), c::alpha(col, 0.4f), 10, true);
    }

    // The top band: the drop style and the keys' length on the left, the
    // build's timeline and its countdown in the middle, what the page is
    // doing to the mix on the right.
    const qreal bandTop = pc.pad.top() + 12;
    {
        QRectF r = c::tag(p, QPointF(pc.pad.left() + 12, bandTop + 7),
                          QStringLiteral("DROP %1").arg(QString::fromUtf8(styleName(m_style)).toUpper()), col, 11);
        r = c::tag(p, QPointF(r.right() + 6, r.center().y()), QStringLiteral("KEYS %1 BAR").arg(rowBars(m_keyRow)),
                   c::alpha(col, 0.7f), 11);
        if (!m_buildMix)
            c::tag(p, QPointF(pc.pad.left() + 12, bandTop + 29), QStringLiteral("MIX LEFT ALONE"), c::alpha(col, 0.5f), 11);
    }
    const QRectF band(pc.pad.left() + pc.pad.width() * 0.26, bandTop, pc.pad.width() * 0.3, 10);
    if (drop > beat) {
        const double span = std::max(0.25, drop - start);
        const double prog = std::clamp((beat - start) / span, 0.0, 1.0);
        p.fillRect(band, c::alpha(col, 0.12f));
        p.fillRect(QRectF(band.left(), band.top(), band.width() * prog, band.height()), c::alpha(col, 0.6f));
        p.setPen(QPen(c::alpha(col, 0.9f), 1));
        for (double b = std::ceil(start / 4.0) * 4.0; b <= drop + 1e-6; b += 4.0) {
            const qreal x = band.left() + band.width() * (b - start) / span;
            p.drawLine(QPointF(x, band.top() - 3), QPointF(x, band.bottom() + 3));
        }
        const double left = drop - beat;
        const int beatsLeft = int(std::ceil(left - 1e-6));
        const QString count = left >= 4 ? QStringLiteral("DROP IN %1 BARS").arg(left / 4.0, 0, 'f', 1)
                                        : QStringLiteral("DROP IN %1").arg(beatsLeft);
        c::tag(p, QPointF(band.center().x(), band.bottom() + 13), count, col, 13, true);
        if (left < 4) {
            // The last bar: the beats to the ONE, big.
            const float within = float(left - std::floor(left));
            const qreal size = unit * (0.16 + 0.06 * within);
            c::text(p, QRectF(pc.pad.left(), pc.at(0, 0.58f).y() - size / 2, pc.pad.width(), size),
                    QString::number(beatsLeft), c::alpha(col, 0.2f + 0.3f * within), int(size), true, Qt::AlignCenter);
        }
    } else {
        p.setPen(QPen(c::alpha(col, 0.25f), 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(band);
        c::text(p, QRectF(band.left(), band.bottom() + 4, band.width(), 16), QStringLiteral("LAND A RISER, OR CLICK TO DROP"),
                c::alpha(col, 0.45f), 10, true, Qt::AlignCenter);
    }
    {
        struct Meter {
            const char *name;
            float value;
            bool bipolar;
        };
        const Meter meters[4] = {{"FILTER", sv.master[MasterFilter], true},
                                 {"DRIVE", sv.master[MasterDrive], false},
                                 {"ROOM", sv.master[RoomSize], false},
                                 {"ECHO", sv.master[EchoFeedback], false}};
        const qreal w = std::min<qreal>(150, pc.pad.width() * 0.13);
        for (int k = 0; k < 4; ++k) {
            const qreal x = pc.pad.right() - 2 * (w + 54) - 8 + (k % 2) * (w + 54), y = bandTop + 2 + (k / 2) * 16;
            c::text(p, QRectF(x, y - 2, 48, 12), QString::fromUtf8(meters[k].name), c::alpha(col, 0.6f), 9, true);
            c::gauge(p, QRectF(x + 48, y, w, 8), meters[k].value, col, 12, meters[k].bipolar);
        }
    }

    // Fingers, with what each one plays.
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        QString label;
        float glow = 0;
        for (const Finger &h : m_fingers)
            if (h.id == t.id) {
                if (h.dropFinger)
                    label = QStringLiteral("DROP");
                else if (isRiser(h.kind))
                    label = QStringLiteral("%1 %2B").arg(QString::fromUtf8(kindName(h.kind)).toUpper()).arg(h.bars);
                else
                    label = QString::fromUtf8(kindName(h.kind)).toUpper();
                glow = 0.3f + 0.7f * h.intensity;
            }
        c::finger(p, pc, t, col, label, glow);
    }

    // The ONE: the whole pad lights.
    if (flash > 0.01f) {
        p.fillRect(pc.pad, c::alpha(col, 0.35f * flash));
        c::text(p, pc.rect(0, 0.35f, 1, 0.75f), QStringLiteral("DROP"), c::alpha(QColor(255, 255, 255), flash),
                int(unit * 0.2), true, Qt::AlignCenter);
    }
    c::beats(p, pc, col);
    c::clips(p, QRectF(pc.pad.left(), pc.pad.bottom() + 14, pc.pad.width(), 30), *pc.view, col, pc.time);
}

}

PartSpec makeRisers() {
    PartSpec s;
    s.name = QStringLiteral("FX");
    s.blurb = QStringLiteral("risers, impacts, sweeps");
    s.color = QColor("#FF4136");
    s.engine = std::make_unique<risers::Engine>();
    s.control = std::make_unique<FxPage>();
    // A big room and a little echo: risers and impacts live in the space
    // around the mix. They don't pump; the drop pumps everything else.
    const float channel[ChannelCount] = {0.72f, 0.5f, 0.5f, 0.32f, 0.12f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
