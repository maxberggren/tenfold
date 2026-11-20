// Drums: a ten-finger beat machine. Sixteen synthesised drums in four rows
// (the beat nearest the player, cymbals at the far edge), five kits, and a
// way of playing where every finger means something:
//
//   * a tap is a hit, as hard as it was pressed, coloured by where on the
//     pad it landed;
//   * a finger that stays rolls, locked to the grid: slide up for faster
//     (1/4 to 1/32 triplets), sideways to bend its pitch, twist to open
//     it, press to swell it;
//   * fingers that land together and stay become a groove: each pad plays
//     its part in the kit's style, up for busier, sideways to shift it;
//   * a click while rolling or grooving keeps it: at the bar line it
//     becomes a loop (or is overdubbed into the loop that plays);
//   * a click on a fresh finger grabs that drum's sound: drag to tune it,
//     up for longer, twist for its tone, while it plays on every beat;
//   * a finger run fast across pads plays every pad it crosses, a fill;
//   * and the tap-then-hold loop of every page: tap a beat, come back to
//     its first pad on time and stay.
//
// Rolls and grooves are scheduled from here a little ahead of time, on the
// studio's grid, as ordinary hits: they land sample-exact, and a captured
// loop holds them exactly as heard.

#include <QPainter>
#include <QStringList>
#include <array>
#include <cmath>

#include "console.h"
#include "drums.h"
#include "parts.h"
#include "phrase.h"

namespace tf {

namespace {

using namespace drums;

constexpr int kCols = 4, kRows = 4;
constexpr double kHoldSecs = 0.22;     // a finger down longer than this repeats
constexpr double kTogetherSecs = 0.12; // fingers landing this close are one hand
constexpr double kLookahead = 0.06;    // how far ahead rolls are scheduled
constexpr float kLadderStep = 0.07f;   // pad heights of travel per rate or density step
constexpr int kKeyBit = 1 << 20;       // holders for keys, apart from finger ids

int padAt(float x, float y) {
    const int col = std::clamp(int(x * kCols), 0, kCols - 1);
    const int row = std::clamp(int(y * kRows), 0, kRows - 1);
    return row * kCols + col;
}

// The keyboard is a second pad: the left hand's three rows mirror the
// bottom three rows of the trackpad, U I O P are the cymbal row, and the
// right hand gets the two rows that matter most again.
struct KeyPad {
    int key;
    int pad;
    const char *label;
};
const KeyPad kKeyPads[] = {
    {Qt::Key_Z, Kick, "Z"},  {Qt::Key_X, Snare, "X"},     {Qt::Key_C, Clap, "C"},      {Qt::Key_V, Hat, "V"},
    {Qt::Key_A, Sub, "A"},   {Qt::Key_S, Rim, "S"},       {Qt::Key_D, Shaker, "D"},    {Qt::Key_F, Open, "F"},
    {Qt::Key_Q, TomLo, "Q"}, {Qt::Key_W, TomMid, "W"},    {Qt::Key_E, TomHi, "E"},     {Qt::Key_R, Perc, "R"},
    {Qt::Key_U, Crash, "U"}, {Qt::Key_I, Ride, "I"},      {Qt::Key_O, Bell, "O"},      {Qt::Key_P, Zap, "P"},
    {Qt::Key_M, Kick, "M"},  {Qt::Key_Comma, Snare, ","}, {Qt::Key_Period, Clap, "."}, {Qt::Key_Slash, Hat, "/"},
    {Qt::Key_J, Sub, "J"},   {Qt::Key_K, Rim, "K"},       {Qt::Key_L, Shaker, "L"},    {Qt::Key_Semicolon, Open, ";"},
};
constexpr int kKeyPadCount = int(sizeof(kKeyPads) / sizeof(kKeyPads[0]));

// A contact's orientation turns through half a circle; the change between
// two readings is the shorter way round.
float twistBetween(float from, float to) {
    float d = to - from;
    while (d > kPi * 0.5f)
        d -= kPi;
    while (d < -kPi * 0.5f)
        d += kPi;
    return d;
}

// How hard a landing finger hit: pressure, helped by how much of the
// fingertip came down.
float landingVelocity(float pressure, float size) {
    return clampf(0.3f + 0.9f * pressure + 0.1f * (size - 1.0f), 0.15f, 1.0f);
}
// How loud a held finger keeps a roll: pressing harder swells it.
float holdingDynamics(float pressure) { return clampf(0.45f + 0.85f * pressure, 0.3f, 1.05f); }

class DrumsControl : public PartControl {
public:
    DrumsControl() { m_swing = kitSwing(m_kit); }

    // --- touch ---------------------------------------------------------------

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        m_phrase.expire(f.t);

        // Lifts first, so a finger that left and one that landed in the
        // same frame don't meet.
        for (auto &h : m_holders) {
            if (!h.used || h.key)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.src;
            if (!still)
                release(h, f.t, ctx, true);
        }

        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Holder *h = holderFor(t.id, false);
            if (!h) {
                land(t, f.t, ctx);
                continue;
            }
            h->x = t.x;
            h->y = t.y;
            h->pressure = t.pressure;
            h->size = t.size;
            h->angle = t.angle;
            follow(*h, f.t, ctx);
        }

        // The click: grabs a fresh finger's sound, keeps a held roll or groove.
        if (f.button && !m_button)
            click(f.t, ctx);
        m_button = f.button;

        resolve(f.t, ctx);
    }

    void idle(double t, ControlContext &ctx) override {
        m_phrase.expire(t);
        resolve(t, ctx);
    }

    // Rolls, grooves and anything kept are scheduled here, whether the page
    // shows or not: a groove that was kept goes on to its bar line even if
    // the player has turned to the bass.
    void background(double t, ControlContext &ctx) override { schedule(t, ctx); }

    // --- keys ----------------------------------------------------------------

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        for (int i = 0; i < kKeyPadCount; ++i) {
            if (kKeyPads[i].key != k.key)
                continue;
            if (k.repeat)
                return true;
            Holder *h = holderFor(kKeyBit | i, true);
            if (k.down && !h)
                pressKey(i, shift, ctx);
            else if (!k.down && h)
                release(*h, ctx.now(), ctx, true);
            return true;
        }
        if (!k.down)
            return false;
        switch (k.key) {
        case Qt::Key_Left:
        case Qt::Key_Right:
            setKit((m_kit + (k.key == Qt::Key_Right ? 1 : KitCount - 1)) % KitCount, ctx);
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down: {
            // Held keys roll faster or slower, and so will the next ones.
            const int step = k.key == Qt::Key_Up ? 1 : -1;
            m_keyRate = std::clamp(m_keyRate + step, -3, 3);
            for (auto &h : m_holders)
                if (h.used && h.key && h.gen >= 0 && !m_gens[size_t(h.gen)].groove) {
                    h.rate = std::clamp(h.rate + step, 0, kRates - 1);
                    m_gens[size_t(h.gen)].setRoll(h.rate);
                }
            ctx.notice(m_keyRate == 0 ? QStringLiteral("Keys roll at each drum's own rate")
                                      : QStringLiteral("Keys roll %1 %2 faster")
                                            .arg(m_keyRate)
                                            .arg(std::abs(m_keyRate) == 1 ? "step" : "steps"));
            return true;
        }
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            m_swing = clampf(m_swing + (k.key == Qt::Key_BracketRight ? 0.02f : -0.02f), 0.5f, 0.7f);
            ctx.notice(QStringLiteral("Swing %1%").arg(int(std::lround(m_swing * 100))));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift) {
                m_userOverdub = !m_userOverdub;
                ctx.overdub(m_userOverdub);
                ctx.notice(m_userOverdub ? QStringLiteral("Overdub: what you play joins the loop")
                                         : QStringLiteral("Overdub off"));
            } else if (ctx.studio().parts[ctx.part()].recording >= 0) {
                ctx.stopRecording();
                ctx.notice(QStringLiteral("The loop closes at the bar"));
            } else {
                const int slot = freeSlot(ctx);
                ctx.record(slot, 8);
                ctx.notice(QStringLiteral("Recording slot %1 from the next bar").arg(slot + 1));
            }
            return true;
        case Qt::Key_Delete:
            for (int p = 0; p < PadCount; ++p)
                if (m_edit[p].any()) {
                    m_edit[p] = {};
                    sendEdit(p, ctx);
                }
            ctx.notice(QStringLiteral("Every drum back to the kit's sound"));
            return true;
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        default:
            break;
        }
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 emptied").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        return false;
    }

    void leave(ControlContext &ctx) override {
        for (auto &h : m_holders)
            if (h.used)
                release(h, ctx.now(), ctx, false);
        m_frame = Frame{};
        m_button = false;
    }

    // --- drawing -------------------------------------------------------------

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "tap a pad\ta hit, as hard as you press; where on the pad colours it (the ride's far edge is its bell)\n"
            "hold a pad\tit rolls on the grid: slide up for faster (1/4 … 1/32T), sideways bends its pitch, "
            "twist opens it, press to swell\n"
            "land fingers together and hold\ta groove: each pad plays its part in the kit's style; "
            "slide one up for busier, sideways to shift it\n"
            "hold another pad while grooving\tadds its drum to the groove\n"
            "click while rolling or grooving\tkeep it: at the bar it loops (or overdubs into the loop playing)\n"
            "click a pad you just touched, drag\tshape that drum: sideways tunes, up lengthens, twist changes its tone\n"
            "run a finger fast across pads\tplays each one it crosses: a fill\n"
            "hold the 808\tsustains; slide sideways to glide through the scale\n"
            "tap a beat, hold its first pad on time\tloops it into the next free slot\n"
            "Z X C V · A S D F · Q W E R · U I O P\tthe pads, row by row from the bottom (M , . / and J K L ; too); "
            "hold to roll, Shift for an accent\n"
            "Up / Down\tfaster or slower key rolls\n"
            "Left / Right\tthe kit: house, techno, trap, breaks, garage\n"
            "[ / ]\tless or more swing\n"
            "Enter · Shift+Enter\trecord a loop from the next bar · overdub on or off\n"
            "Delete\tevery drum back to the kit's own sound\n"
            "F1–F8 · Shift+F1–F8\tlaunch a slot at the next bar · empty it\n"
            "Backspace\tstop the beat at the next bar");
    }

    QString status() const override {
        QStringList parts;
        QStringList groove;
        for (const auto &h : m_holders) {
            if (!h.used)
                continue;
            const QString name = QString::fromUtf8(padName(h.pad, m_kit)).toUpper();
            switch (h.mode) {
            case Mode::Roll:
                if (h.gen >= 0)
                    parts << QStringLiteral("%1 rolls %2").arg(name, QString::fromUtf8(rateName(m_gens[size_t(h.gen)].rate)));
                break;
            case Mode::Groove: groove << name; break;
            case Mode::Edit: parts << QStringLiteral("shaping %1").arg(name); break;
            case Mode::Bass: parts << QStringLiteral("808 held"); break;
            case Mode::Sweep: parts << QStringLiteral("fill"); break;
            default: break;
            }
        }
        if (!groove.isEmpty())
            parts.prepend(QStringLiteral("%1 grooving").arg(groove.join('+')));
        if (m_keep.active)
            parts << QStringLiteral("keeping at the bar");
        parts << QStringLiteral("%1 kit, swing %2%")
                     .arg(QString::fromUtf8(kitName(m_kit)))
                     .arg(int(std::lround(m_swing * 100)));
        return parts.join(QStringLiteral(" · "));
    }

private:
    enum class Mode : uint8_t {
        Pending, // just landed: a tap, unless it stays
        Roll,    // repeats at a rate
        Groove,  // plays its part of a groove
        Edit,    // clicked: shaping the drum's sound
        Bass,    // the 808, held and gliding
        Sweep,   // run across the pads
        Loop,    // closed a phrase into a loop: nothing more to do
        Kept,    // its roll or groove was kept: it plays on by itself
    };

    // A finger or a key on a pad.
    struct Holder {
        bool used = false, key = false;
        int src = -1;
        int pad = 0;
        Mode mode = Mode::Pending;
        double t0 = 0;
        float x0 = 0, y0 = 0, x = 0, y = 0;
        float angle0 = 0, angle = 0;
        float pressure = 0.5f, size = 1;
        float vel = 0.8f;
        int cluster = 0;
        int gen = -1;
        int rate = 0, density = 1;
        int lastCell = 0;
        int degree = 0; // the 808's glide, in degrees
        Edit editFrom;
    };

    // A generator: what a held pad (or a kept one) plays, bar after bar.
    struct Gen {
        bool active = false, groove = false, preview = false;
        int holder = -1;
        int pad = 0;
        int rate = 0, density = 1, shift = 0;
        float dyn = 1, sx = 0.5f, sy = 0.5f;
        int degrees = 0;
        float semis = 0, decay = 0;
        double cursor = 0; // scheduled up to this beat
        double start = 0;  // the first beat it played
        double until = -1; // a kept generator stops here
        bool untilInclusive = false;
        Pattern pattern;
        void setRoll(int r) {
            rate = r;
            pattern = rollPattern(r);
        }
    };

    // A click's keep: what is held becomes a loop at a bar line.
    struct Keep {
        bool active = false, overdub = false, overdubOn = false;
        double from = 0, until = 0; // beats
        int slot = 0;
    };

    // --- holders ---------------------------------------------------------------

    Holder *holderFor(int src, bool key) {
        for (auto &h : m_holders)
            if (h.used && h.key == key && h.src == src)
                return &h;
        return nullptr;
    }
    Holder *freeHolder() {
        for (auto &h : m_holders)
            if (!h.used)
                return &h;
        return nullptr;
    }
    int indexOf(const Holder &h) const { return int(&h - m_holders.data()); }

    // A finger lands: the hit goes out at once, and the finger waits to see
    // whether it stays.
    void land(const Touch &t, double now, ControlContext &ctx) {
        Holder *h = freeHolder();
        if (!h)
            return;
        *h = Holder{};
        h->used = true;
        h->src = t.id;
        h->pad = padAt(t.x, t.y);
        h->t0 = now;
        h->x0 = h->x = t.x;
        h->y0 = h->y = t.y;
        h->angle0 = h->angle = t.angle;
        h->pressure = t.pressure;
        h->size = t.size;
        h->vel = landingVelocity(t.pressure, t.size);
        h->lastCell = h->pad;
        h->cluster = clusterFor(now);
        if (h->pad == Sub)
            sendSubOn(*h, now, ctx);
        else
            hit(h->pad, h->vel, t.x * kCols - float(h->pad % kCols), t.y * kRows - float(h->pad / kCols), now, ctx);
    }

    void pressKey(int index, bool accent, ControlContext &ctx) {
        Holder *h = freeHolder();
        if (!h)
            return;
        const double now = ctx.now();
        *h = Holder{};
        h->used = true;
        h->key = true;
        h->src = kKeyBit | index;
        h->pad = kKeyPads[index].pad;
        h->t0 = now;
        h->x0 = h->x = (float(h->pad % kCols) + 0.5f) / kCols;
        h->y0 = h->y = (float(h->pad / kCols) + 0.5f) / kRows;
        h->vel = accent ? 1.0f : 0.78f;
        h->pressure = accent ? 0.7f : 0.45f;
        h->lastCell = h->pad;
        h->cluster = clusterFor(now);
        if (h->pad == Sub)
            sendSubOn(*h, now, ctx);
        else
            hit(h->pad, h->vel, 0.5f, 0.5f, now, ctx);
    }

    // Fingers (and keys) landing within a moment of one another are one
    // hand: if they stay, they groove together.
    int clusterFor(double now) {
        for (const auto &o : m_holders)
            if (o.used && o.mode == Mode::Pending && now - o.t0 < kTogetherSecs)
                return o.cluster;
        return ++m_clusterSerial;
    }

    // Has a finger run far enough into another pad to count as there? A
    // little way in, so a finger resting on a border doesn't chatter.
    static bool wellInside(float x, float y) {
        const float cx = x * kCols, cy = y * kRows;
        const float fx = cx - std::floor(cx), fy = cy - std::floor(cy);
        return std::min({fx, 1.0f - fx, fy, 1.0f - fy}) >= 0.12f;
    }

    // A finger moves.
    void follow(Holder &h, double now, ControlContext &ctx) {
        switch (h.mode) {
        case Mode::Pending:
        case Mode::Sweep: {
            // Running across pads before it settles: a fill, each pad as
            // it's crossed.
            const int cell = padAt(h.x, h.y);
            if (cell == h.lastCell || !wellInside(h.x, h.y))
                break;
            if (h.mode == Mode::Pending && h.pad == Sub)
                sendOff(h, now, ctx);
            h.mode = Mode::Sweep;
            h.lastCell = cell;
            hit(cell, landingVelocity(h.pressure, h.size) * 0.92f, h.x * kCols - float(cell % kCols),
                h.y * kRows - float(cell / kCols), now, ctx);
            break;
        }
        case Mode::Roll:
        case Mode::Groove:
            shapeGen(h);
            break;
        case Mode::Edit:
            shapeSound(h, ctx);
            break;
        case Mode::Bass: {
            const int deg = std::clamp(int(std::lround((h.x - h.x0) * 20.0f)), -7, 7);
            if (deg != h.degree) {
                h.degree = deg;
                Command mv;
                mv.type = Command::Move;
                mv.id = h.src & 0xFFFFFF;
                mv.t = now;
                mv.i[0] = Sub;
                mv.i[1] = deg;
                ctx.send(mv);
            }
            break;
        }
        default:
            break;
        }
    }

    // A roll's or a groove's finger: up and down climbs the ladder,
    // sideways bends (a roll) or shifts (a groove), twist sets the decay,
    // pressure the loudness.
    void shapeGen(Holder &h) {
        if (h.gen < 0)
            return;
        Gen &g = m_gens[size_t(h.gen)];
        const int steps = int(std::lround((h.y - h.y0) / kLadderStep));
        const float dx = h.x - h.x0;
        if (g.groove) {
            const int density = std::clamp(h.density + steps, 0, kDensities - 1);
            const int shift = std::clamp(int(std::lround(dx / 0.06f)), -8, 8);
            if (density != g.density || shift != g.shift) {
                g.density = density;
                g.shift = shift;
                g.pattern = groovePattern(g.pad, m_kit, density, shift);
            }
        } else {
            const int rate = std::clamp(h.rate + steps, 0, kRates - 1);
            if (rate != g.rate)
                g.setRoll(rate);
            if (tonal(g.pad))
                g.degrees = std::clamp(int(std::lround(dx * 20.0f)), -7, 7);
            else
                g.semis = clampf(dx * 40.0f, -12.0f, 12.0f);
        }
        if (!h.key) {
            g.decay = clampf(twistBetween(h.angle0, h.angle) * 1.3f, -2.0f, 2.0f);
            g.dyn = holdingDynamics(h.pressure);
        }
    }

    // A clicked finger: drag to shape its drum.
    void shapeSound(Holder &h, ControlContext &ctx) {
        Edit e = h.editFrom;
        const float dx = h.x - h.x0, dy = h.y - h.y0;
        e.tune = tonal(h.pad) ? std::clamp(std::round(h.editFrom.tune + dx * 20.0f), -7.0f, 7.0f)
                              : clampf(h.editFrom.tune + dx * 40.0f, -12.0f, 12.0f);
        e.decay = clampf(h.editFrom.decay + dy * 5.0f, -2.0f, 2.0f);
        e.tone = clampf(h.editFrom.tone + twistBetween(h.angle0, h.angle) * 1.2f, -1.0f, 1.0f);
        Edit &cur = m_edit[h.pad];
        if (std::fabs(e.tune - cur.tune) > 0.05f || std::fabs(e.decay - cur.decay) > 0.01f ||
            std::fabs(e.tone - cur.tone) > 0.01f) {
            cur = e;
            sendEdit(h.pad, ctx);
        }
    }

    // A finger or a key leaves (or the page does).
    void release(Holder &h, double now, ControlContext &ctx, bool countTap) {
        if (countTap && h.mode == Mode::Pending)
            m_phrase.tap(h.t0, now - h.t0, h.pad);
        if (h.pad == Sub && (h.mode == Mode::Pending || h.mode == Mode::Bass || h.mode == Mode::Edit ||
                             h.mode == Mode::Loop))
            sendOff(h, now, ctx);
        if (h.gen >= 0) {
            Gen &g = m_gens[size_t(h.gen)];
            if (g.holder == indexOf(h))
                g.active = false;
        }
        h = Holder{};
    }

    // Fingers that have been down long enough decide what they are.
    void resolve(double now, ControlContext &ctx) {
        for (auto &h : m_holders) {
            if (!h.used || h.mode != Mode::Pending || now - h.t0 < kHoldSecs)
                continue;
            // Wait for the rest of a hand that is still landing.
            int together = 0;
            bool waiting = false;
            for (const auto &o : m_holders) {
                if (!o.used || o.cluster != h.cluster)
                    continue;
                if (o.mode == Mode::Pending) {
                    ++together;
                    waiting |= now - o.t0 < kHoldSecs;
                } else if (o.mode == Mode::Groove) {
                    ++together;
                }
            }
            if (waiting)
                continue;
            bool grooving = false;
            for (const auto &o : m_holders)
                grooving |= o.used && o.mode == Mode::Groove;
            double start = 0;
            if (together >= 2 || grooving) {
                becomeGroove(h, now, ctx);
            } else if (!anyGen() && !m_phrase.taps().empty() && m_phrase.taps().front().group == h.pad &&
                       m_phrase.closes(h.t0, h.pad, &start)) {
                // Back on the phrase's first pad, on time: the loop.
                const int slot = freeSlot(ctx);
                ctx.capture(slot, start, h.t0);
                ctx.notice(QStringLiteral("Beat in slot %1").arg(slot + 1));
                m_phrase.clear();
                h.mode = Mode::Loop;
            } else if (h.pad == Sub) {
                h.mode = Mode::Bass;
            } else {
                becomeRoll(h, now, ctx);
            }
        }
    }

    bool anyGen() const {
        for (const auto &g : m_gens)
            if (g.active)
                return true;
        return false;
    }

    Gen *newGen(Holder &h, double now, ControlContext &ctx) {
        for (size_t i = 0; i < m_gens.size(); ++i) {
            Gen &g = m_gens[i];
            if (g.active)
                continue;
            g = Gen{};
            g.active = true;
            g.holder = indexOf(h);
            g.pad = h.pad;
            g.cursor = ctx.beatAt(now);
            g.start = g.cursor;
            g.sx = std::clamp(h.x0 * kCols - float(h.pad % kCols), 0.0f, 1.0f);
            g.sy = std::clamp(h.y0 * kRows - float(h.pad / kCols), 0.0f, 1.0f);
            g.dyn = h.key ? (h.vel >= 1.0f ? 1.0f : 0.85f) : holdingDynamics(h.pressure);
            h.gen = int(i);
            return &g;
        }
        return nullptr;
    }

    // Measure the ladder from where the finger is when it starts to
    // repeat, not where it landed: settling into a hold shouldn't change it.
    static void rebase(Holder &h) {
        h.y0 = h.y;
        h.x0 = h.x;
        h.angle0 = h.angle;
    }

    void becomeRoll(Holder &h, double now, ControlContext &ctx) {
        Gen *g = newGen(h, now, ctx);
        h.mode = Mode::Roll;
        h.rate = std::clamp(defaultRate(h.pad) + (h.key ? m_keyRate : 0), 0, kRates - 1);
        if (g)
            g->setRoll(h.rate);
        rebase(h);
    }

    void becomeGroove(Holder &h, double now, ControlContext &ctx) {
        if (h.pad == Sub)
            sendOff(h, now, ctx);
        Gen *g = newGen(h, now, ctx);
        h.mode = Mode::Groove;
        h.density = 1;
        if (g) {
            g->groove = true;
            g->density = 1;
            g->pattern = groovePattern(h.pad, m_kit, 1, 0);
        }
        rebase(h);
    }

    // --- the click -------------------------------------------------------------

    void click(double now, ControlContext &ctx) {
        // Fresh fingers: grab their drums' sounds.
        bool editing = false;
        for (auto &h : m_holders) {
            if (!h.used || h.key || h.mode != Mode::Pending)
                continue;
            h.mode = Mode::Edit;
            h.editFrom = m_edit[h.pad];
            rebase(h);
            // It plays on every beat while it's shaped, so the change is heard.
            if (h.pad != Sub)
                if (Gen *g = newGen(h, now, ctx)) {
                    g->preview = true;
                    g->setRoll(0);
                    g->dyn = 0.85f;
                }
            editing = true;
        }
        if (editing) {
            ctx.notice(QStringLiteral("Drag to shape: sideways tunes, up lengthens, twist for tone"));
            return;
        }
        keep(now, ctx);
    }

    // Keeps every held roll and groove: they play on by themselves to the
    // next bar line, and that bar becomes a loop (or, with a loop playing,
    // they are overdubbed into one whole pass of it).
    void keep(double now, ControlContext &ctx) {
        if (m_keep.active)
            return;
        double first = 1e18;
        int count = 0;
        for (auto &g : m_gens)
            if (g.active && !g.preview && g.until < 0) {
                first = std::min(first, g.start);
                ++count;
            }
        if (!count)
            return;
        const double beat = ctx.beatAt(now);
        const PartView &pv = ctx.studio().parts[ctx.part()];
        Keep k;
        k.active = true;
        if (pv.playing >= 0 && pv.clips[pv.playing].lengthBeats > 0) {
            k.overdub = true;
            k.from = std::ceil(beat / kBeatsPerBar - 1e-6) * kBeatsPerBar;
            k.until = k.from + pv.clips[pv.playing].lengthBeats;
            k.slot = pv.playing;
        } else {
            // A whole bar of it, ending on a bar line.
            const double need = std::max(beat, first + kBeatsPerBar);
            k.from = std::ceil(need / kBeatsPerBar - 1e-6) * kBeatsPerBar;
            k.until = k.from;
            k.slot = freeSlot(ctx);
        }
        for (auto &g : m_gens) {
            if (!g.active || g.preview || g.until >= 0)
                continue;
            g.until = k.until;
            g.untilInclusive = !k.overdub;
            if (g.holder >= 0) {
                m_holders[size_t(g.holder)].mode = Mode::Kept;
                m_holders[size_t(g.holder)].gen = -1;
            }
            g.holder = -1;
        }
        m_keep = k;
        ctx.notice(k.overdub ? QStringLiteral("Into the loop from the bar")
                             : QStringLiteral("Loop in slot %1 at the bar").arg(k.slot + 1));
    }

    // --- scheduling ------------------------------------------------------------

    void schedule(double now, ControlContext &ctx) {
        const double beatNow = ctx.beatAt(now);
        const double horizon = ctx.beatAt(now + kLookahead);

        if (m_keep.active && beatNow > m_keep.until + kBeatsPerBar) {
            // The clock jumped (a loop on another page set a new tempo):
            // this keep's bar has gone.
            if (m_keep.overdubOn && !m_userOverdub)
                ctx.overdub(false);
            m_keep = Keep{};
        }
        if (m_keep.active && m_keep.overdub && !m_keep.overdubOn && horizon >= m_keep.from) {
            // Everything before the pass begins goes out first, then the
            // overdub opens, so the pass holds exactly one round.
            sendSteps(beatNow, m_keep.from - 1e-6, ctx);
            if (!m_userOverdub)
                ctx.overdub(true);
            m_keep.overdubOn = true;
        }
        sendSteps(beatNow, horizon, ctx);
        if (m_keep.active && m_keep.overdub && horizon >= m_keep.until) {
            if (!m_userOverdub)
                ctx.overdub(false);
            m_keep = Keep{};
        }
        if (m_keep.active && !m_keep.overdub && beatNow >= m_keep.from + 0.02) {
            ctx.capture(m_keep.slot, ctx.timeAt(m_keep.from - kBeatsPerBar), ctx.timeAt(m_keep.from), 0.85f);
            m_keep = Keep{};
        }
    }

    // Sends every step of every generator from its cursor up to `to`.
    void sendSteps(double beatNow, double to, ControlContext &ctx) {
        for (auto &g : m_gens) {
            if (!g.active)
                continue;
            // A cursor far behind means the clock jumped: start from now
            // rather than catch up with a burst.
            if (g.cursor < beatNow - 0.25)
                g.cursor = beatNow;
            double end = to;
            bool finishing = false;
            if (g.until >= 0 && g.until <= to) {
                end = g.until;
                finishing = true;
            }
            if (end <= g.cursor)
                continue;
            constexpr double bar = kBeatsPerBar;
            for (long b0 = long(std::floor(g.cursor / bar)); double(b0) * bar <= end; ++b0) {
                for (int i = 0; i < g.pattern.count; ++i) {
                    const Step &s = g.pattern.steps[i];
                    const double b = double(b0) * bar + double(s.tick) * bar / kTicks;
                    if (b <= g.cursor + 1e-9 || b > end + 1e-9)
                        continue;
                    if (finishing && !g.untilInclusive && b >= g.until - 1e-9)
                        continue;
                    if (b < beatNow - 0.02)
                        continue; // too late to play in time
                    sendStep(g, s, b, ctx);
                }
            }
            g.cursor = end;
            if (finishing)
                g.active = false;
        }
    }

    void sendStep(const Gen &g, const Step &s, double beat, ControlContext &ctx) {
        // Swing: every other sixteenth comes late, the MPC way.
        double b = beat;
        if (s.tick % 12 == 6)
            b += double(m_swing - 0.5f) * 0.5;
        Command c;
        c.type = Hit;
        c.flags = g.preview ? Command::NoRecord : 0;
        c.t = ctx.timeAt(b);
        c.i[0] = g.pad;
        c.i[1] = g.degrees + s.degree;
        c.f[0] = clampf(s.vel * g.dyn, 0.05f, 1.0f);
        c.f[1] = g.sx;
        c.f[2] = g.sy;
        c.f[3] = g.semis;
        c.f[4] = g.decay;
        if (g.pad == Sub) // a groove's 808 rings a little over a sixteenth
            c.f[5] = float(0.4 * 60.0 / std::max(40.0, ctx.studio().bpm));
        ctx.send(c);
    }

    // --- commands --------------------------------------------------------------

    void hit(int pad, float vel, float sx, float sy, double t, ControlContext &ctx) {
        Command c;
        c.type = Hit;
        c.flags = Command::Quantize;
        c.t = t;
        c.i[0] = pad;
        c.f[0] = vel;
        c.f[1] = clampf(sx, 0, 1);
        c.f[2] = clampf(sy, 0, 1);
        if (pad == Sub) // run over by a fill, the 808 is a short hit
            c.f[5] = 0.25f;
        ctx.send(c);
    }

    void sendSubOn(const Holder &h, double t, ControlContext &ctx) {
        Command c;
        c.type = Command::On;
        c.id = h.src & 0xFFFFFF;
        c.t = t;
        c.i[0] = Sub;
        c.f[0] = h.vel;
        ctx.send(c);
    }

    void sendOff(const Holder &h, double t, ControlContext &ctx) {
        Command c;
        c.type = Command::Off;
        c.id = h.src & 0xFFFFFF;
        c.t = t;
        c.i[0] = Sub;
        ctx.send(c);
    }

    void sendEdit(int pad, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = ParamEdit;
        c.i[1] = pad;
        c.f[0] = m_edit[pad].tune;
        c.f[1] = m_edit[pad].decay;
        c.f[2] = m_edit[pad].tone;
        ctx.send(c);
    }

    void setKit(int kit, ControlContext &ctx) {
        m_kit = kit;
        m_swing = kitSwing(kit);
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = ParamKit;
        c.i[1] = m_kit;
        ctx.send(c);
        // Grooves that are playing take up the new kit's parts at once.
        for (auto &g : m_gens)
            if (g.active && g.groove)
                g.pattern = groovePattern(g.pad, m_kit, g.density, g.shift);
        ctx.notice(QStringLiteral("%1 kit, swing %2%")
                       .arg(QString::fromUtf8(kitName(kit)).toUpper())
                       .arg(int(std::lround(m_swing * 100))));
    }

    int freeSlot(ControlContext &ctx) const {
        const int s = ctx.freeSlot();
        if (s >= 0)
            return s;
        // All full: the one after the slot playing, so the beat that's on
        // isn't the one replaced.
        const int playing = ctx.studio().parts[ctx.part()].playing;
        return playing >= 0 ? (playing + 1) % kClipSlots : 0;
    }

    // --- state -------------------------------------------------------------------

    int m_kit = House;
    float m_swing = 0.54f;
    int m_keyRate = 0;
    Edit m_edit[PadCount];
    std::array<Holder, 24> m_holders{};
    std::array<Gen, 24> m_gens{};
    Keep m_keep;
    int m_clusterSerial = 0;
    bool m_button = false;
    bool m_userOverdub = false;
    Phrase m_phrase;
    Frame m_frame;
};

// --- drawing -------------------------------------------------------------------

void DrumsControl::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const float *data = pc.view->data;
    c::pad(p, pc, QStringLiteral("DRUMS"));
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = std::max(0.6, double(pc.scale));
    const int label = int(std::clamp(14.0 * s, 10.0, 18.0));
    const int small = int(std::clamp(11.0 * s, 8.0, 14.0));

    Harmony harm;
    harm.tuning.set(pc.studio->root, pc.studio->family, pc.studio->brightness, pc.studio->just);

    // Which generator plays on which pad, for the step strips.
    const Gen *genOn[PadCount] = {};
    for (const auto &g : m_gens)
        if (g.active && !genOn[g.pad])
            genOn[g.pad] = &g;

    const double barPos = pc.beat / kBeatsPerBar - std::floor(pc.beat / kBeatsPerBar);

    for (int pad = 0; pad < PadCount; ++pad) {
        const int row = pad / kCols, column = pad % kCols;
        const QRectF cell = pc.rect(float(column) / kCols, float(row) / kRows, float(column + 1) / kCols,
                                    float(row + 1) / kRows)
                                .adjusted(5 * s, 5 * s, -5 * s, -5 * s);
        const float live = data[pad], replay = data[16 + pad];
        // The glow of a hit: bright for a hand, softer for the loop.
        p.fillRect(cell, c::alpha(col, 0.04f + 0.5f * live + 0.22f * replay));
        const Gen *gen = genOn[pad];
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c::alpha(col, gen ? 0.95f : 0.35f + 0.5f * live), gen ? 2.5 : 1.2));
        p.drawRect(cell);

        // Its name, and the note for tuned drums.
        QString name = QString::fromUtf8(padName(pad, m_kit)).toUpper();
        if (tonal(pad)) {
            int deg = pad == TomMid ? 2 : pad == TomHi || pad == Perc || pad == Bell ? 4 : 0;
            deg += int(std::lround(m_edit[pad].tune));
            const int pc12 = (int(std::lround(harm.midi(deg, 0))) % 12 + 12) % 12;
            name += QStringLiteral(" ") + QString::fromUtf8(Tuning::noteName(pc12));
        }
        const QColor ink = live > 0.4f ? QColor::fromRgb(c::kInk) : col;
        const QRectF head(cell.left() + 8 * s, cell.top() + 4 * s, cell.width() - 16 * s, label + 6);
        c::text(p, head, name, ink, label, true);
        // The keys that play it.
        QString keys;
        for (int i = 0; i < kKeyPadCount; ++i)
            if (kKeyPads[i].pad == pad)
                keys += QString::fromUtf8(kKeyPads[i].label);
        c::text(p, head, keys, c::alpha(ink, 0.5f), small, false, Qt::AlignRight | Qt::AlignVCenter);

        // A drum that's been shaped shows how: tune, length, tone.
        const Edit &e = m_edit[pad];
        if (e.any()) {
            const QRectF g(cell.left() + 8 * s, cell.top() + label + 14 * s, std::min(cell.width() * 0.45, 90 * s), 4 * s);
            const float span = tonal(pad) ? 7.0f : 12.0f;
            c::gauge(p, g, 0.5f + 0.5f * e.tune / span, col, 10, true);
            c::gauge(p, g.translated(0, 7 * s), 0.5f + 0.25f * e.decay, col, 10, true);
            c::gauge(p, g.translated(0, 14 * s), 0.5f + 0.5f * e.tone, col, 10, true);
        }

        // A pad that repeats shows its bar of steps along the bottom, the
        // one just played lit, and what it is doing in the middle.
        if (gen) {
            const QRectF strip(cell.left() + 8 * s, cell.bottom() - 18 * s, cell.width() - 16 * s, 10 * s);
            const int nowTick = int(barPos * kTicks);
            int lastTick = -1;
            for (int i = 0; i < gen->pattern.count; ++i)
                if (gen->pattern.steps[i].tick <= nowTick)
                    lastTick = gen->pattern.steps[i].tick;
            const double w = std::max(2.0, strip.width() / kTicks * 2.0);
            for (int i = 0; i < gen->pattern.count; ++i) {
                const Step &st = gen->pattern.steps[i];
                const double x = strip.left() + strip.width() * st.tick / kTicks;
                const double h = strip.height() * (0.3 + 0.7 * std::min(1.0f, st.vel));
                p.fillRect(QRectF(x, strip.bottom() - h, w, h), c::alpha(col, st.tick == lastTick ? 1.0f : 0.4f));
            }
            p.setPen(QPen(c::alpha(col, 0.8f), 1));
            const double hx = strip.left() + strip.width() * barPos;
            p.drawLine(QPointF(hx, strip.top() - 3 * s), QPointF(hx, strip.bottom() + 2 * s));
            QString what = gen->preview ? QStringLiteral("SHAPING")
                           : gen->groove ? QString::fromUtf8(densityName(gen->density)).toUpper()
                                         : QString::fromUtf8(rateName(gen->rate));
            if (gen->until >= 0 && !gen->preview)
                what += QStringLiteral(" · KEPT");
            c::text(p, QRectF(cell.left(), cell.center().y() - label, cell.width(), 2 * label), what,
                    c::alpha(col, 0.9f), int(label * 1.3), true, Qt::AlignCenter);
        }
    }

    // The 808's note while it sounds.
    if (data[32] > 0.002f) {
        const QRectF cell = pc.rect(0, 0.25f, 0.25f, 0.5f);
        const int deg = int(std::lround(data[33])) + int(std::lround(m_edit[Sub].tune));
        const int pc12 = (int(std::lround(harm.midi(deg, 0))) % 12 + 12) % 12;
        c::tag(p, cell.center(), QString::fromUtf8(Tuning::noteName(pc12)), col, int(label * 1.2), true);
    }

    // The fingers, each saying what it does.
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        QString what;
        float glow = 0;
        for (const auto &h : m_holders) {
            if (!h.used || h.key || h.src != t.id)
                continue;
            switch (h.mode) {
            case Mode::Roll:
                if (h.gen >= 0)
                    what = QString::fromUtf8(rateName(m_gens[size_t(h.gen)].rate));
                glow = 0.8f;
                break;
            case Mode::Groove:
                if (h.gen >= 0)
                    what = QString::fromUtf8(densityName(m_gens[size_t(h.gen)].density)).toUpper();
                glow = 0.8f;
                break;
            case Mode::Edit: {
                const Edit &e = m_edit[h.pad];
                what = QStringLiteral("%1%2 · LEN %3%4")
                           .arg(e.tune >= 0 ? "+" : "")
                           .arg(double(e.tune), 0, 'f', tonal(h.pad) ? 0 : 1)
                           .arg(e.decay >= 0 ? "+" : "")
                           .arg(double(e.decay), 0, 'f', 1);
                glow = 0.5f;
                break;
            }
            case Mode::Bass: what = QStringLiteral("808"); break;
            case Mode::Sweep: what = QStringLiteral("FILL"); break;
            case Mode::Loop: what = QStringLiteral("LOOP"); break;
            case Mode::Kept: what = QStringLiteral("KEPT"); break;
            default: break;
            }
        }
        c::finger(p, pc, t, col, what, glow);
    }

    // A keep on its way to the bar line.
    if (m_keep.active) {
        const double left = std::max(0.0, m_keep.from - pc.beat);
        const QString msg = m_keep.overdub ? QStringLiteral("INTO THE LOOP IN %1").arg(left, 0, 'f', 1)
                                           : QStringLiteral("LOOP %1 IN %2").arg(m_keep.slot + 1).arg(left, 0, 'f', 1);
        c::tag(p, QPointF(pc.pad.center().x(), pc.pad.top()), msg,
               std::fmod(pc.time, 0.5) < 0.25 ? col : c::alpha(col, 0.6f), 13, true);
    }

    c::beats(p, pc, col);
    // Below the pad: the clips, then the kits with the one playing lit and
    // the swing.
    const double split = pc.pad.left() + pc.pad.width() * 0.62;
    c::clips(p, QRectF(pc.pad.left(), pc.pad.bottom() + 14, split - pc.pad.left(), 30), *pc.view, col, pc.time);
    const double kx0 = split + 10;
    const double swingW = 74;
    const double kw = (pc.pad.right() - kx0 - swingW) / double(int(KitCount));
    for (int k = 0; k < KitCount; ++k) {
        const QRectF r(kx0 + k * kw + 2, pc.pad.bottom() + 14, kw - 4, 30);
        if (k == m_kit) {
            p.fillRect(r, col);
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c::alpha(col, 0.45f), 1.2));
            p.drawRect(r);
        }
        c::text(p, r, QString::fromUtf8(kitName(k)), k == m_kit ? QColor::fromRgb(c::kInk) : c::alpha(col, 0.7f),
                int(std::clamp(r.height() * 0.38, 8.0, 12.0)), true, Qt::AlignCenter);
    }
    const QRectF sw(pc.pad.right() - swingW + 6, pc.pad.bottom() + 14, swingW - 6, 30);
    c::text(p, sw.adjusted(0, 0, 0, -14), QStringLiteral("SWING %1").arg(int(std::lround(m_swing * 100))), col, 11, true,
            Qt::AlignCenter);
    c::gauge(p, QRectF(sw.left() + 4, sw.bottom() - 10, sw.width() - 8, 8), (m_swing - 0.5f) / 0.2f, col, 10);
}

}

PartSpec makeDrums() {
    PartSpec s;
    s.name = QStringLiteral("Drums");
    s.blurb = QStringLiteral("kicks, snares, hats, rolls, grooves");
    s.color = QColor("#FF8A1F");
    s.engine = std::make_unique<drums::Engine>();
    s.control = std::make_unique<DrumsControl>();
    // The drums have a room of their own, so only a touch of the studio's
    // reverb; and they don't duck themselves.
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.06f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
