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
// Left and Right choose the set: the sound of all of it, from EDM through
// analog, dub, lo-fi, trap, drum and bass, cinema, synthwave and techno to
// ambient (see risers.h). Each riser and hit keeps the set it began in.
//
// Pressing the pad until it clicks (or Return) is the drop gesture: the
// drop comes on the next bar line, and whatever is rising rushes into it.
// On the drop the engine fires the impact and the sub on the exact sample,
// while this page moves the whole mix around it: a high-pass that climbs the
// master through the build, a little drive, a bigger room, and, depending on
// the drop style, a half-beat gap or drums and bass stripped out for the last
// bar. All of it snaps back on the ONE, to exactly what it was.

#include "risers.h"

#include <QFontMetricsF>
#include <QJsonValue>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <array>
#include <cmath>
#include <cstring>

#include "console.h"
#include "parts.h"

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

// What each set is, for the notice when Left or Right picks it.
const char *setBlurb(int s) {
    static const char *blurbs[SetCount] = {
        "noise sweeps, supersaw stabs, crash and sub",
        "square waves, singing filters, chorus, a spring crash, a resonant power-down",
        "siren (twist it to sweep), offbeat skank, spring splash, echoes that run away",
        "dust and crackle, keys, tape wow, a vinyl brake",
        "a whistle, FM bells, hi-hats changing rate, an 808 dive, reversed vocal chops",
        "a chopped break into a fill, reese riser and stabs, a screaming zap, a rewind",
        "wind, strings, taiko, a braam on the drop",
        "gated snare and crash, chorused brass, laser toms, a tape stop",
        "ring-modulated metal, an acid line, offbeat chords, gated swells, a clang",
        "grains, soft pads, a shaker, a bloom instead of a crash",
    };
    return blurbs[std::clamp(s, 0, int(SetCount) - 1)];
}

// The hit row's own names, where a set has none of its own.
const char *kHitLabels[kHits] = {"IMPACT", "SUB DROP", "DOWNLIFT", "ZAP", "STOP", "THROW", "WASH", "SWEEP"};
QString kindTitle(int set, int kind) {
    if (!isRiser(kind) && kind < KindCount && !palette(set).names[kind])
        return QString::fromUtf8(kHitLabels[kind - kRisers]);
    return QString::fromUtf8(kindLabel(set, kind)).toUpper();
}

const char *kHitKeys = "ZXCVBNM,";
const char *kRiserKeys = "ASDFG";

int rowBars(int row) { return 1 << std::clamp(row, 0, kRows - 1); }

class FxPage : public PartControl {
public:
    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
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
        }
    }

    void background(double t, ControlContext &ctx) override { automate(t, ctx); }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        if (k.repeat)
            return true;
        if (k.down && k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (k.modifiers & Qt::ShiftModifier) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 emptied").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
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
        // Delete lets go, as on every page: here the whole build. Q was its
        // key first and stays a second one.
        case Qt::Key_Delete:
        case Qt::Key_Q:
            fallAway(t, ctx);
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            setKeyRow(m_keyRow + (k.key == Qt::Key_Up ? 1 : -1), ctx);
            return true;
        // Left and Right, the sound: the set. Shift with them (or [ ]), the
        // drop style, the page's rhythm.
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (k.modifiers & Qt::ShiftModifier)
                setStyle((m_style + (k.key == Qt::Key_Right ? 1 : StyleCount - 1)) % StyleCount, t, ctx);
            else
                setSet((m_set + (k.key == Qt::Key_Right ? 1 : SetCount - 1)) % SetCount, ctx);
            return true;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            setStyle((m_style + (k.key == Qt::Key_BracketRight ? 1 : StyleCount - 1)) % StyleCount, t, ctx);
            return true;
        case Qt::Key_W:
            toggleBuildMix(ctx);
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
                lift(h, t, ctx);
        m_frame = Frame{};
        m_button = false;
    }

    // Ctrl+S: the drop style, how long the keys' risers are, and whether a
    // build moves the mix. A build on its way is a moment, not a setting,
    // and a build kept in a loop is the studio's to save with its clips.
    // The style is saved by its name and the risers by their bars, so the
    // file reads as the page does.
    void save(QJsonObject &out) const override {
        out[QStringLiteral("set")] = QString::fromUtf8(palette(m_set).name);
        out[QStringLiteral("dropStyle")] = QString::fromUtf8(styleName(m_style));
        out[QStringLiteral("riserBars")] = rowBars(m_keyRow);
        out[QStringLiteral("buildMix")] = m_buildMix;
    }

    // Ctrl+O. What the file lacks, or holds in a form the page doesn't
    // know, is the page's default, never what the song before it left.
    void load(const QJsonObject &in, ControlContext &ctx) override {
        m_style = Clean;
        m_set = SetEdm;
        m_keyRow = 2;
        const QString set = in.value(QStringLiteral("set")).toString();
        for (int s = 0; s < SetCount; ++s)
            if (set.compare(QString::fromUtf8(palette(s).name), Qt::CaseInsensitive) == 0)
                m_set = s;
        m_buildMix = true;
        const QString style = in.value(QStringLiteral("dropStyle")).toString();
        for (int s = 0; s < StyleCount; ++s)
            if (style == QLatin1String(styleName(s)))
                m_style = s;
        // A length the field has no row for (a hand-edited 6) is the
        // nearest one it has.
        const QJsonValue bars = in.value(QStringLiteral("riserBars"));
        if (bars.isDouble() && std::isfinite(bars.toDouble()))
            m_keyRow = std::clamp(int(std::lround(std::log2(std::clamp(bars.toDouble(), 1.0, 16.0)))), 0, kRows - 1);
        const QJsonValue mix = in.value(QStringLiteral("buildMix"));
        if (mix.isBool())
            m_buildMix = mix.toBool();
        // The engine keeps the style too (in a gap its risers stop half a
        // beat early), so it hears it just as a key would send it.
        sendStyle(0, ctx);
    }

    void paint(QPainter &p, const PaintContext &pc) override;

    // The mouse on the band under the pad: everything the keys do that the
    // pad doesn't already.
    enum Spot { SpotStyle = 100, SpotShorter = 200, SpotLonger, SpotDrop, SpotFall, SpotMix, SpotSet = 300 };
    bool click(QPointF, ControlContext &ctx) override;

    QString help() const override {
        return QStringLiteral(
            "Left / Right\tthe set, the sound of every riser and hit: EDM, analog, dub, lo-fi, trap, drum and bass, "
            "cinema, synthwave (80S), techno, ambient. What is already playing keeps its own, and loops keep theirs. "
            "Some cells take the set's own name: a dub SIREN (twist it for a swept siren), trap HATS, a cinema BRAAM\n"
            "Shift+Left / Right (or [ ])\tdrop style: clean; gap, a half-beat breath before the ONE with only the "
            "risers' room ringing; or strip, the risers alone for the build's last bar, then the breath\n"
            "Up / Down\tlonger or shorter risers from the keys (1, 2, 4, 8, 16 bars)\n"
            "Return\tdrop on the next bar line: everything rising rushes into it\n"
            "Delete (or Q)\tthe whole build falls away\n"
            "F1–F8 · Shift+F1–F8\tlaunch a slot at the next bar · empty it\n"
            "Backspace\tstop the FX clip at the next bar\n"
            "Space\tloop what you just played (as on every page); Shift+Space layers it onto the loop playing\n"
            "under the pad, with the mouse\tthe clips (click: launch or stop), the sets, the drop styles, riser "
            "length, DROP, the build falling away, and whether builds move the mix\n"
            "land in the build field\ta riser: the column is the sound (noise, pitch, saw, roll, swell, renamed by "
            "some sets: a dub siren, trap hats), the row its "
            "length (1, 2, 4, 8, 16 bars up the pad); it peaks on the next phrase line (every 8 bars, 16 for the top "
            "row) and comes in that many bars before it; the finger shows the bar\n"
            "more fingers\tstack layers on the same drop; each enters its row's length before it\n"
            "slide up / sideways\tbigger and brighter / pitch or voicing, always in the key\n"
            "press, twist, flatten\tswell it / set it moving / add grit\n"
            "flick down as you lift\tpull that riser out: it falls away\n"
            "click the pad\tdrop on the next bar line instead, as Return does\n"
            "bottom row\timpact, sub drop, downlifter, laser zap, power-down, echo throw, reverb wash, DJ sweep\n"
            "sub drop during a build\tarms a sub onto the drop (the drop's boom gives way to it)\n"
            "hold a hit\tsub sustains, zaps fire sixteenths up the chord, throw and wash hold; higher in a cell is bigger\n"
            "hold the sweep\tslide left to close a low-pass on the whole mix, up to open a high-pass; press for drive\n"
            "record a build and its drop\tbuild, let it drop, press Space in the bar after: the loop runs from the bar "
            "the build began in to the bar after the drop, and drops on the same bar every pass\n"
            "A S D F G\tnoise, pitch, saw, roll, swell risers, as long as Up / Down says\n"
            "Z X C V B N M ,\tthe bottom row's hits, held as long as the key\n"
            "W\tbuilds move the mix (high-pass, room, and drive when pushed) or leave it alone");
    }

    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("risers"))
            return float(m_risers);
        if (name == QLatin1String("drops"))
            return float(m_drops);
        if (name == QLatin1String("set"))
            return float(m_set);
        return PartControl::tutorialProbe(name);
    }

    QString status() const override {
        QStringList parts;
        for (const Finger &h : m_fingers)
            if (h.id >= 0 && !h.dropFinger) {
                if (isRiser(h.kind))
                    parts << QStringLiteral("%1 %2").arg(kindTitle(h.set, h.kind)).arg(h.bars);
                else
                    parts << kindTitle(h.set, h.kind);
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
        if (m_set != SetEdm)
            s += QStringLiteral("  ·  %1").arg(QString::fromUtf8(palette(m_set).name));
        if (m_style != Clean)
            s += QStringLiteral("  ·  %1").arg(QString::fromUtf8(styleName(m_style)));
        if (m_take >= 0)
            s += QStringLiteral("  ·  looping into slot %1").arg(m_take + 1);
        return s;
    }

private:
    struct Finger {
        int id = -1;
        int kind = 0, bars = 1, flags = 0, set = 0;
        double t0 = 0, drop = -1;
        float x0 = 0, y0 = 0, a0 = 0;
        float x = 0, y = 0, size = 1, press = 0.5f, angle = 0;
        float int0 = 0.6f, tone0 = 0;
        float tone = 0, intensity = 0.6f, motion = 0, grit = 0;
        bool sent = false, dropFinger = false, isKey = false;
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
        h.set = m_set;
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
        h.flags = FlagEnterLate;
        if (pending > 0) {
            h.drop = pending;
        } else {
            h.drop = phraseDrop(ctx, beat, h.bars);
            m_localDrop = h.drop;
        }
    }

    // Where a new build lands: the next phrase line (every 8 bars, or 16 for
    // the 16-bar row) with room for the row's length, always forward. The
    // phrase is counted from when the music started (the bar the first loop
    // began on), or from bar 0. The riser then enters its length before
    // that line.
    double phraseDrop(ControlContext &, double beat, int bars) const {
        const double origin = m_songStart >= 0 ? m_songStart : 0.0;
        const double phrase = bars >= 16 ? 64.0 : 32.0;
        const double earliest = beat + 4.0 * bars - 2.0;
        double k = std::ceil((earliest - origin) / phrase - 1e-9);
        return nearestBar(origin + k * phrase);
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
        c.i[3] = h.set;
        c.f[0] = h.press;
        c.f[1] = h.tone;
        c.f[2] = h.intensity;
        c.f[3] = h.motion;
        c.f[4] = h.grit;
        c.f[5] = isRiser(h.kind) ? float(h.drop - ctx.beatAt(h.t0)) : 0.0f;
        ctx.send(c);
        if (isRiser(h.kind))
            m_liveDrop = h.drop;
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

    void lift(Finger &h, double t, ControlContext &ctx) {
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
            // And let go of it: the session counts what is held.
            Command off;
            off.type = Command::Off;
            off.id = h.id;
            off.t = t;
            ctx.send(off);
            ctx.notice(QStringLiteral("%1 falls away").arg(kindTitle(h.set, h.kind)));
            bool others = false;
            for (const Finger &o : m_fingers)
                others |= o.id >= 0 && o.id != h.id && isRiser(o.kind) && o.sent;
            if (!others && data(ctx)[pub::Layers] <= 1)
                m_localDrop = m_liveDrop = -1;
        }
        h.id = -1;
    }

    // The engine needs the style too: in a gap the risers stop half a beat
    // early. A Param, so a clip replays it.
    void sendStyle(double t, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.t = t;
        c.i[0] = ParamStyle;
        c.f[0] = float(m_style);
        ctx.send(c);
    }

    void sendDrop(double t, ControlContext &ctx) {
        Command c;
        c.type = CmdDrop;
        c.t = t;
        c.i[0] = m_set;
        ctx.send(c);
        const double beat = ctx.beatAt(t);
        const double target = std::ceil((beat - 0.25) / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        const double pending = pendingDrop(ctx, beat);
        m_localDrop = pending > 0 ? std::min(pending, target) : target;
        m_liveDrop = m_localDrop;
        ctx.notice(QStringLiteral("DROP on the next ONE"));
    }

    // Every riser falls away at once and the drop is called off.
    void fallAway(double t, ControlContext &ctx) {
        Command c;
        c.type = CmdFall;
        c.id = kEveryone;
        c.t = t;
        ctx.send(c);
        m_localDrop = m_liveDrop = -1;
        ctx.notice(QStringLiteral("The build falls away"));
    }

    void setStyle(int style, double t, ControlContext &ctx) {
        m_style = std::clamp(style, 0, int(StyleCount) - 1);
        sendStyle(t, ctx);
        ctx.notice(QStringLiteral("Drop style: %1").arg(QString::fromUtf8(styleName(m_style)).toUpper()));
    }

    void setSet(int set, ControlContext &ctx) {
        m_set = std::clamp(set, 0, int(SetCount) - 1);
        ctx.notice(QStringLiteral("%1: %2").arg(QString::fromUtf8(palette(m_set).label), QString::fromUtf8(setBlurb(m_set))));
    }

    void setKeyRow(int row, ControlContext &ctx) {
        m_keyRow = std::clamp(row, 0, kRows - 1);
        ctx.notice(QStringLiteral("Keys play %1-bar risers").arg(rowBars(m_keyRow)));
    }

    void toggleBuildMix(ControlContext &ctx) {
        m_buildMix = !m_buildMix;
        ctx.notice(m_buildMix ? QStringLiteral("Builds move the mix") : QStringLiteral("Builds leave the mix alone"));
    }

    // --- the mix -------------------------------------------------------------

    struct Mod {
        bool owned = false;
        float base = 0, sent = -1;
        double released = -100;
        double releaseAt = 0; // when the hand-back is due, if scheduled
        double sendAt = 0;    // when this tick's value is due, if scheduled
        float w = 0, wt = 0;
    };

    static int ch(int part, int c) { return MasterCount + part * ChannelCount + c; }

    void pull(int idx, float target, float weight, double when = 0) {
        if (weight <= 1e-4f)
            return;
        m_mods[idx].w += weight;
        m_mods[idx].wt += weight * target;
        m_mods[idx].sendAt = std::max(m_mods[idx].sendAt, when);
    }

    // Every moment, what the page wants of the mix, from what the engine
    // says is happening: so a build or a throw replayed from a clip moves
    // the mix just as it did live.
    void automate(double t, ControlContext &ctx) {
        const StudioView &sv = ctx.studio();
        // Nothing to go on until the studio has published a first view: its
        // zeros are not the mix's settings.
        if (sv.sample == 0)
            return;
        const float *d = data(ctx);
        const double beat = ctx.beatAt(t);
        const double bps = sv.bpm / 60.0;
        // For the tutorial: risers on their way (rising or waiting) and the
        // drops that have fired, live or from a loop.
        m_risers = int(d[pub::Layers]);
        if (d[pub::LastDrop] >= 0 && d[pub::LastDrop] != m_lastDropSeen) {
            if (m_lastDropSeen != -2)
                ++m_drops;
            m_lastDropSeen = d[pub::LastDrop];
        } else if (m_lastDropSeen == -2) {
            m_lastDropSeen = d[pub::LastDrop];
        }
        // Hand-backs at a drop are scheduled, not sent on a tick: once the
        // ONE is closer than the next few ticks, what the build holds is
        // released at a set moment. Mutes act at once, so they lift on the
        // ONE itself; the studio still smooths the master over 30 ms, so the
        // filter, drive and room go home 60 ms before it, to be there on it.
        const double horizon = 0.12 * bps;
        const double ease = 0.06;
        const int parts = std::min(sv.partCount, kMaxParts);
        for (Mod &m : m_mods) {
            m.w = m.wt = 0;
            m.sendAt = 0;
        }

        const double drop = d[pub::Drop];
        m_take = sv.parts[ctx.part()].recording;
        // When the music started: the bar the first loop began on, kept
        // while anything plays.
        int playing = -1;
        for (int p = 0; p < sv.partCount && p < kMaxParts; ++p)
            if (sv.parts[p].playing >= 0 && playing < 0)
                playing = p;
        if (playing < 0) {
            m_songStart = -1;
        } else if (m_songStart < 0) {
            const PartView &pv = sv.parts[playing];
            const ClipView &c = pv.clips[pv.playing];
            m_songStart = nearestBar(sv.beat - double(c.phase) * c.lengthBeats);
        }
        m_statusDrop = drop;
        m_statusBeat = beat;
        if (drop > 0) {
            const double start = d[pub::Start];
            const float I = d[pub::Intensity];
            const double tDrop = ctx.timeAt(drop);
            if (beat >= drop - horizon && beat < drop)
                for (int idx : {int(MasterFilter), int(MasterDrive), int(RoomSize)})
                    m_mods[idx].releaseAt = tDrop - ease;
            if (m_buildMix && beat < drop - horizon && beat >= start) {
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
            // The gap mutes everything for the last half beat: the risers stop
            // there too (the engine knows the style), and only their room
            // rings into the ONE. The strip leaves the risers alone for the
            // build's last bar (every other part goes) and ends in the same
            // breath.
            // Mutes lift 5 ms before the line: the studio ramps a channel in
            // over a couple of milliseconds, and the kick's first ones are its
            // edge. The breath is silent there, so nothing leaks.
            for (int part = 0; part < parts; ++part)
                if (beat >= drop - horizon && beat < drop)
                    m_mods[ch(part, Mute)].releaseAt = tDrop - 0.005;
            if (beat < drop - horizon) {
                if (m_style == Gap && beat >= drop - 0.5)
                    for (int part = 0; part < parts; ++part)
                        if (part != ctx.part())
                            pull(ch(part, Mute), 1, 1);
                if (m_style == Strip && drop - start >= 4 && beat >= drop - kBeatsPerBar)
                    for (int part = 0; part < parts; ++part)
                        if (part != ctx.part())
                            pull(ch(part, Mute), 1, 1);
            }
        }

        // Recording a build and its drop with Space: once a live build's drop
        // has fired, mark the beat before the next bar line as part of what
        // was played. Space's take then runs from the build's first landing
        // to the bar after the drop, in whole bars, and the loop replays the
        // build with its drop on the same bar of every pass. The mark is not
        // an onset of its own (Continue): it never starts a phrase.
        if (m_liveDrop > 0 && beat >= m_liveDrop + 0.25) {
            if (std::fabs(d[pub::LastDrop] - m_liveDrop) < 0.01) {
                Command c;
                c.type = CmdMark;
                c.flags = Command::Quantize | Command::Continue;
                c.t = ctx.timeAt(m_liveDrop + kBeatsPerBar - 1.0);
                ctx.send(c);
            }
            m_liveDrop = -1;
        }

        // The lift: the drop comes in a notch louder than the music before
        // it, scheduled to arrive with the ONE (the studio smooths the master
        // over 30 ms) and easing back over its bar. The studio's glue and
        // limiter answer peaks, so no layer laid on a kick can make the ONE
        // louder by itself; this does, and is handed back exactly.
        if (m_buildMix) {
            constexpr float lift = 0.92f; // the lift, at unity (gain goes as the square)
            if (drop > 0 && beat >= drop - horizon && beat < drop) {
                pull(MasterGain, lift, 1, ctx.timeAt(drop) - 0.03);
                m_liftDrop = drop; // the view only says it fired a tick later
            }
            const double last = std::max(double(d[pub::LastDrop]), m_liftDrop);
            if (last > 0 && beat >= last && beat < last + kBeatsPerBar) {
                const float u = float((beat - last) / kBeatsPerBar);
                pull(MasterGain, lift, u < 0.25f ? 1.0f : 1.0f - smoothstep((u - 0.25f) / 0.75f));
            }
        }

        // The power-down: the mix darkens and sinks until its bar line.
        const double s0 = d[pub::StopStart], s1 = d[pub::StopEnd];
        // It comes home 150 ms early: from that deep, the studio's smoothing
        // needs that long to have it all back on the ONE.
        const double home = 0.15 * bps;
        if (s1 > 0 && beat >= s1 - home - horizon && beat < s1) {
            m_mods[MasterFilter].releaseAt = ctx.timeAt(s1) - 0.15;
            m_mods[MasterGain].releaseAt = ctx.timeAt(s1) - 0.15;
        }
        if (s1 > 0 && beat < s1 - home - horizon && beat >= s0) {
            // A brake is all the way down within its second; a power-down
            // takes until its bar line.
            const double ramp = d[pub::StopRamp] > 0 ? double(d[pub::StopRamp]) : s1 - s0;
            const float u = clampf(float((beat - s0) / std::max(0.25, ramp)), 0, 1);
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
            auto send = [&](float v, double when = 0) {
                if (master)
                    ctx.setMaster(c, v, when);
                else
                    ctx.setChannel(part, c, v, when);
                m.sent = v;
            };
            if (m.w > 1e-4f) {
                m.releaseAt = 0;
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
                    send(v, m.sendAt > t ? m.sendAt : 0);
            } else if (m.owned) {
                // At a drop, on its scheduled moment; otherwise now.
                const double when = m.releaseAt > t ? m.releaseAt : 0;
                send(m.base, when);
                m.owned = false;
                m.released = std::max(t, when);
                m.releaseAt = 0;
            }
        }
    }

    std::array<Finger, 24> m_fingers{};
    Mod m_mods[kMods];
    Frame m_frame;
    console::Hotspots m_spots; // what paint() drew to be clicked
    bool m_button = false;
    bool m_buildMix = true;
    int m_style = Clean;
    int m_set = SetEdm;
    int m_keyRow = 2; // keys play 4-bar risers until Up or Down says otherwise
    double m_localDrop = -1;
    double m_liveDrop = -1; // the drop a build played live is aiming at
    int m_risers = 0, m_drops = 0;
    float m_lastDropSeen = -2; // −2 until the first view has been seen
    double m_statusDrop = -1, m_statusBeat = 0;
    int m_take = -1;
    double m_liftDrop = -1;
    double m_songStart = -1;
};

bool FxPage::click(QPointF at, ControlContext &ctx) {
    const int id = m_spots.at(at);
    if (id < 0)
        return false;
    if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
        return true;
    const double t = ctx.now();
    if (id >= SpotSet && id < SpotSet + int(SetCount)) {
        if (id - SpotSet != m_set)
            setSet(id - SpotSet, ctx);
        return true;
    }
    if (id >= SpotStyle && id < SpotStyle + int(StyleCount)) {
        if (id - SpotStyle != m_style)
            setStyle(id - SpotStyle, t, ctx);
        return true;
    }
    switch (id) {
    case SpotShorter: setKeyRow(m_keyRow - 1, ctx); return true;
    case SpotLonger: setKeyRow(m_keyRow + 1, ctx); return true;
    case SpotDrop: sendDrop(t, ctx); return true;
    case SpotFall: fallAway(t, ctx); return true;
    case SpotMix: toggleBuildMix(ctx); return true;
    default: return false;
    }
}

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
        c::text(p, QRectF(cell.left() + 8, cell.bottom() - 15, cell.width(), 13), label, c::alpha(col, 0.75f), 10, true);
    }
    for (int k = 0; k < kRisers; ++k) {
        const QRectF head = pc.rect(float(k) / kRisers, 1 - kRowH, float(k + 1) / kRisers, 1);
        // On a black plate, so a riser climbing under it never hides it.
        const qreal gw = std::min<qreal>(84, head.width() * 0.6);
        const QRectF plate(head.center().x() - gw / 2 - 8, head.top() + 40, gw + 16, 40);
        p.fillRect(plate, QColor(0, 0, 0, 220));
        c::text(p, QRectF(head.left(), head.top() + 42, head.width(), 18), kindTitle(m_set, k), col, 13, true,
                Qt::AlignCenter);
        glyph(p, QRectF(head.center().x() - gw / 2, head.top() + 61, gw, 14), k, c::alpha(col, 0.8f));
    }

    // The hit row.
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
        c::text(p, QRectF(cell.left(), cell.center().y() - 2, cell.width(), 18), kindTitle(m_set, kRisers + k),
                lit > 0.5f ? QColor::fromRgb(c::kInk) : col, 12, true, Qt::AlignCenter);
        const qreal gw = std::min<qreal>(56, cell.width() * 0.5);
        glyph(p, QRectF(cell.center().x() - gw / 2, cell.center().y() - 24, gw, 18), kRisers + k,
              lit > 0.5f ? QColor::fromRgb(c::kInk) : c::alpha(col, 0.6f));
        c::text(p, QRectF(cell.left() + 6, cell.top() + 4, 30, 14), QString(QChar(kHitKeys[k])), c::alpha(col, 0.75f), 11, true);
    }

    // The top band: the build's timeline and its countdown in the middle,
    // what the page is doing to the mix on the right. The drop style, the
    // keys' length and whether builds move the mix are in the band under
    // the pad, where the mouse can change them.
    const qreal bandTop = pc.pad.top() + 12;
    // It ends where the meters on the right begin, however narrow the pad.
    const qreal meterW = std::min<qreal>(150, pc.pad.width() * 0.13);
    const qreal meterLeft = pc.pad.right() - 2 * (meterW + 54) - 8;
    const qreal bandLeft = pc.pad.left() + pc.pad.width() * 0.24;
    const QRectF band(bandLeft, bandTop, std::min<qreal>(pc.pad.width() * 0.3, meterLeft - bandLeft - 16), 10);
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

    } else {
        p.setPen(QPen(c::alpha(col, 0.25f), 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(band);
        c::text(p, QRectF(band.left(), band.bottom() + 4, band.width(), 16), QStringLiteral("LAND A RISER · CLICK TO DROP"),
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
        const qreal w = meterW;
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
        float glow = 0, intensity = -1;
        for (const Finger &h : m_fingers)
            if (h.id == t.id) {
                if (h.dropFinger)
                    label = QStringLiteral("DROP");
                else if (isRiser(h.kind))
                    // Where it will land, before the finger lifts.
                    label = QStringLiteral("%1 %2B → BAR %3")
                                .arg(kindTitle(h.set, h.kind))
                                .arg(h.bars)
                                .arg(int(std::floor(h.drop / kBeatsPerBar + 0.5)) + 1);
                else
                    label = kindTitle(h.set, h.kind);
                glow = 0.3f + 0.7f * h.intensity;
                intensity = h.intensity;
            }
        c::finger(p, pc, t, col, QString(), glow);
        // The tag beside the finger, on its left when the right would run off
        // the pad.
        const QPointF at = pc.at(t.x, t.y);
        const qreal r = pc.pad.height() / 10.0 * 0.32 * std::clamp(t.size, 0.6f, 4.0f);
        const qreal w = console::textWidth(c::font(12, true), label) + 12;
        const bool left = at.x() + r + 6 + w > pc.pad.right() - 4;
        const qreal x = left ? at.x() - r - 6 - w : at.x() + r + 6;
        if (!label.isEmpty())
            c::tag(p, QPointF(x, at.y() - r * 0.6), label, col, 12);
        if (intensity >= 0)
            c::gauge(p, QRectF(left ? x + w - 64 : x, at.y() - r * 0.6 + 12, 64, 5), intensity, col, 8);
    }

    // The ONE: the whole pad lights.
    if (flash > 0.01f) {
        p.fillRect(pc.pad, c::alpha(col, 0.35f * flash));
        c::text(p, pc.rect(0, 0.35f, 1, 0.75f), QStringLiteral("DROP"), c::alpha(QColor(255, 255, 255), flash),
                int(unit * 0.2), true, Qt::AlignCenter);
    }
    // The last bar: the beats to the ONE, big, in white on top of
    // everything.
    if (drop > beat && drop - beat < 4) {
        const double left = drop - beat;
        const float within = float(left - std::floor(left));
        const qreal size = unit * (0.2 + 0.06 * within);
        const QRectF box(pc.pad.left(), pc.at(0, 0.6f).y() - size / 2, pc.pad.width(), size);
        const QString digit = QString::number(int(std::ceil(left - 1e-6)));
        c::text(p, box.translated(3, 3), digit, QColor(0, 0, 0, 200), int(size), true, Qt::AlignCenter);
        c::text(p, box, digit, QColor(255, 244, 236, int(150 + 105 * within)), int(size), true, Qt::AlignCenter);
    }
    // A loop waiting to be taken (Space).
    if (pc.view->recording >= 0)
        c::tag(p, QPointF(pc.pad.left() + 12, pc.pad.bottom() - kHitTop * pc.pad.height() - 14),
               QStringLiteral("LOOPING INTO SLOT %1").arg(pc.view->recording + 1), c::red(), 11);
    // A sub armed onto the drop.
    if (d[pub::SubArmed] > 0) {
        const QRectF cell = pc.rect(1.0f / kHits, 0, 2.0f / kHits, kHitTop);
        c::tag(p, QPointF(cell.center().x(), cell.top() + 12), QStringLiteral("ON THE ONE"), col, 10, true);
    }
    c::beats(p, pc, col);

    // Below the pad, every one clickable. Row 0: the clips and the ten sets
    // (the page's sound, as on every page). Row 1: the drop styles, the
    // keys' riser length, the drop, the build falling away and whether
    // builds move the mix. Each group shows its keys; short labels, so it
    // all reads on a small window too.
    m_spots.clear();
    c::clips(p, c::bandRow(pc, 0, 0, 0.26), *pc.view, col, pc.time, &m_spots);
    c::text(p, c::bandRow(pc, 0, 0.26, 0.31), QStringLiteral("← →"), c::alpha(col, 0.7f), 11, true, Qt::AlignCenter);
    QStringList sets;
    for (int s = 0; s < SetCount; ++s)
        sets << QString::fromUtf8(palette(s).label);
    c::chips(p, pc, c::bandRow(pc, 0, 0.31, 1), sets, m_set, &m_spots, SpotSet, 3);
    c::text(p, c::bandRow(pc, 1, 0, 0.07), QStringLiteral("⇧← →"), c::alpha(col, 0.7f), 11, true, Qt::AlignCenter);
    QStringList styles;
    for (int s = 0; s < StyleCount; ++s)
        styles << QString::fromUtf8(styleName(s)).toUpper();
    c::chips(p, pc, c::bandRow(pc, 1, 0.07, 0.33), styles, m_style, &m_spots, SpotStyle);
    // The length: shorter and longer either side of it, as arrows.
    const QRectF row = c::bandRow(pc, 1, 0.34, 0.58);
    const qreal aw = std::min<qreal>(34, row.width() * 0.2);
    c::chip(p, pc, QRectF(row.left(), row.top(), aw, row.height()), QStringLiteral("↓"), false, &m_spots, SpotShorter);
    const QRectF length(row.left() + aw + 4, row.top(), row.width() - 2 * aw - 8, row.height());
    c::text(p, length.adjusted(0, 0, 0, -12),
            rowBars(m_keyRow) == 1 ? QStringLiteral("KEYS 1 BAR") : QStringLiteral("KEYS %1 BARS").arg(rowBars(m_keyRow)),
            col, 11, true, Qt::AlignCenter);
    c::gauge(p, QRectF(length.left() + 6, length.bottom() - 10, length.width() - 12, 7), float(m_keyRow + 1) / kRows, col,
             kRows);
    c::chip(p, pc, QRectF(row.right() - aw, row.top(), aw, row.height()), QStringLiteral("↑"), false, &m_spots,
            SpotLonger);
    const QRectF right = c::bandRow(pc, 1, 0.59, 1);
    const double rw = (right.width() + 4) / 3;
    c::chip(p, pc, QRectF(right.left(), right.top(), rw - 4, right.height()), QStringLiteral("DROP ↵"), drop > beat,
            &m_spots, SpotDrop);
    c::chip(p, pc, QRectF(right.left() + rw, right.top(), rw - 4, right.height()), QStringLiteral("FALL DEL"), false,
            &m_spots, SpotFall);
    c::chip(p, pc, QRectF(right.left() + 2 * rw, right.top(), rw - 4, right.height()), QStringLiteral("MIX W"),
            m_buildMix, &m_spots, SpotMix);
}

}

PartSpec makeRisers() {
    PartSpec s;
    s.name = QStringLiteral("FX");
    s.blurb = QStringLiteral("risers, impacts, sweeps");
    s.color = QColor("#FF4136");
    s.engine = std::make_unique<risers::Engine>();
    s.control = std::make_unique<FxPage>();
    // No sends: risers and hits have a room of their own inside the engine,
    // one that never hears the boom or the sub, and nothing here pumps (the
    // risers breathe with the kick themselves). Throw and wash still take
    // these sends when they are held.
    const float channel[ChannelCount] = {0.72f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
