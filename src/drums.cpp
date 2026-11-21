// Drums: a ten-finger beat machine. Sixteen synthesised drums in four rows
// (the beat nearest the player, cymbals at the far edge), five kits, and a
// way of playing where every finger means something:
//
//   * a tap is a hit, as hard as it was pressed, coloured by where on the
//     pad it landed;
//   * with a thumb resting in the REPEAT strip along the near edge (or B
//     or N held), a pad rolls the moment it lands, MPC note-repeat style:
//     the landing is the first step, at the rate the thumb points at;
//   * a finger that stays rolls, locked to the grid: slide up for faster
//     (1/4 to 1/32 triplets), sideways to bend its pitch, twist to open
//     it, press to swell it;
//   * fingers that land together and stay become a groove: each pad plays
//     its part in the kit's style, up for busier, sideways to shift it;
//   * a click while anything rolls or grooves keeps it: at the bar line
//     it becomes a loop (or is overdubbed into the loop that plays);
//   * with nothing held, a click on a fresh finger grabs that drum's
//     sound: drag to tune it, up for longer, twist for its tone, while it
//     plays on every beat;
//   * a finger run fast across pads plays every pad it crosses, a fill;
//   * Space records a loop from the next bar, as on every page; holding a
//     pad and pressing Delete takes that drum out of the loop that plays.
//
// Loops come only from Space or a click: holding a pad always rolls, never
// closes a loop by its timing.
//
// Rolls and grooves are scheduled from here a little ahead of time, on the
// studio's grid, as ordinary hits: they land sample-exact, and a captured
// loop holds them exactly as heard.

#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <array>
#include <cmath>

#include "console.h"
#include "drums.h"
#include "parts.h"

namespace tf {

namespace {

using namespace drums;

constexpr int kCols = 4, kRows = 4;
// A finger still down after kHoldSecs is held: it stops being a possible
// fill and becomes a roll or a groove. Its first repeat is the first grid
// step at least kFirstStepSecs after landing, and goes out only if the
// finger is still down kFirstLeadSecs before it. So a tap must last about
// 130 ms to sprout a step, longer than a tap lasts, while a roll still
// starts within a step and kFirstStepSecs of landing, with no hole after.
constexpr double kHoldSecs = 0.08;
constexpr double kFirstStepSecs = 0.17;
constexpr double kFirstLeadSecs = 0.012;
// A held step must go out before its time comes, so the lead is at least
// the longest recent gap between ticks (250 Hz live: 4 ms; a still finger
// or a held key gets no frames) plus a margin, and never over 25 ms.
constexpr double kTickMargin = 0.004, kMaxLeadSecs = 0.025;
constexpr double kTogetherSecs = 0.08; // fingers landing this close are one hand
// The REPEAT strip: the near edge of the pad, where a thumb rests. Along it,
// left to right, the roll rates; a landing within kSnapSecs of a step of
// the grid counts as that step.
constexpr float kStripY = 0.05f;
// A contact this much larger than a fingertip, this near the edge and this
// light, is a thumb resting: REPEAT too, a little above the drawn strip. A
// fat, firm hit low on the kick is still a kick.
constexpr float kThumbY = 0.12f;
constexpr float kThumbSize = 1.3f;
constexpr float kThumbPressure = 0.35f;
constexpr double kSnapSecs = 0.03;
constexpr double kRepeatGapSecs = 0.08; // the closest a first repeat may follow a landing that plays at once

// A light first reading is often a finger still coming down: such a landing
// waits one more frame (a few milliseconds) and takes the hardest it pressed.
// A firm one plays at once.
constexpr double kVelocitySecs = 0.006;
constexpr float kFirmPressure = 0.45f;
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
    return clampf(0.12f + 1.0f * pressure + 0.08f * (size - 1.0f), 0.08f, 1.0f);
}



class DrumsControl : public PartControl {
public:
    DrumsControl() { m_swing = kitSwing(m_kit); }

    // --- touch ---------------------------------------------------------------

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;

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
            h->peak = std::max(h->peak, t.pressure);
            if (h->landing && f.t - h->t0 >= kVelocitySecs)
                sendLanding(*h, ctx);
            follow(*h, f.t, ctx);
        }

        // The click: grabs a fresh finger's sound, keeps a held roll or groove.
        if (f.button && !m_button)
            click(f.t, ctx);
        m_button = f.button;

        resolve(f.t, ctx);
    }

    void idle(double t, ControlContext &ctx) override {
        for (auto &h : m_holders)
            if (h.used && h.landing && t - h.t0 >= kVelocitySecs)
                sendLanding(h, ctx);
        resolve(t, ctx);
    }

    // Rolls, grooves and anything kept are scheduled here, whether the page
    // shows or not: a groove that was kept goes on to its bar line even if
    // the player has turned to the bass.
    void background(double t, ControlContext &ctx) override {
        const PartView &pv = ctx.studio().parts[ctx.part()];
        // A take the session is making (Space) starts its slot afresh: no
        // drum is out of a loop that hasn't been played yet.
        if (pv.recording >= 0 && pv.recording != m_recording)
            clearMutes(pv.recording, ctx);
        m_playing = pv.playing;
        m_recording = pv.recording;
        schedule(t, ctx);
    }

    // --- keys ----------------------------------------------------------------

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (k.key == Qt::Key_Space) {
            // Space while a roll or groove plays under a finger or key keeps
            // it, as a click does: whole bars of it on the clock, from when
            // it began, so the taps and rolls tried before it stay out and
            // the tempo stays. It plays on to the bar line and the loop takes
            // over from there, without the hand doubling it. Over a playing
            // loop it goes into that loop; Shift+Space makes a new one.
            // Otherwise Space is the session's, as on every page.
            if (!holdingGen() && !m_keep.active)
                return false;
            if (k.down && !k.repeat && !m_keep.active)
                keepHeld(ctx.now(), ctx, shift);
            return true;
        }
        if (k.key == Qt::Key_B || k.key == Qt::Key_N) {
            // REPEAT, as long as it's held: pads roll the moment they land.
            const uint8_t bit = k.key == Qt::Key_B ? 1 : 2;
            if (!k.repeat)
                m_repeatKeys = k.down ? m_repeatKeys | bit : m_repeatKeys & ~bit;
            return true;
        }
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
            if (m_keyRate == 0)
                ctx.notice(QStringLiteral("Held keys roll at each drum's own rate"));
            else
                ctx.notice(QStringLiteral("Held keys roll %1 %2 %3")
                               .arg(std::abs(m_keyRate))
                               .arg(std::abs(m_keyRate) == 1 ? "step" : "steps")
                               .arg(m_keyRate > 0 ? "faster" : "slower"));
            return true;
        }
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            shiftSwing(k.key == Qt::Key_BracketRight ? 1 : -1, ctx);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift) {
                toggleOverdub(ctx);
                return true;
            }
            // Return keeps what rolls or grooves, as a click does.
            if (holdingGen() && !m_keep.active) {
                keepHeld(ctx.now(), ctx);
                return true;
            }
            return false;
        case Qt::Key_Delete:
            if (toggleLanes(ctx))
                return true;
            {
                // With no loop to take a held drum out of, Delete still
                // means that drum: only its shaping goes back to the kit.
                uint32_t held = 0;
                for (const auto &h : m_holders)
                    if (h.used && h.mode != Mode::Repeat)
                        held |= 1u << h.pad;
                QStringList names;
                for (int p = 0; p < PadCount; ++p)
                    if ((!held || (held >> p & 1u)) && m_edit[p].any()) {
                        m_edit[p] = {};
                        sendEdit(p, ctx);
                    }
                for (int p = 0; p < PadCount; ++p)
                    if (held >> p & 1u)
                        names << QString::fromUtf8(padName(p, m_kit)).toUpper();
                ctx.notice(held ? QStringLiteral("%1 back to the kit's sound").arg(names.join('+'))
                                : QStringLiteral("Every drum back to the kit's sound"));
            }
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
                clearMutes(slot, ctx);
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
        m_repeatKeys = 0;
    }

    // --- drawing -------------------------------------------------------------

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "tap a pad\ta hit, as hard as you press; where on the pad colours it (the ride's far edge is its bell)\n"
            "thumb on the REPEAT strip (near edge)\tpads roll the moment they land, at the rate under the thumb "
            "(1/4 left … 1/32T right); B or N held does the same at each drum's rate\n"
            "hold a pad\tit rolls on the grid: slide up for faster (1/4 … 1/32T), sideways bends its pitch, "
            "twist opens it, press to swell\n"
            "land fingers together and hold\ta groove: each pad plays its part in the kit's style; "
            "slide one up for busier, sideways to shift it\n"
            "hold another pad while grooving\tadds its drum to the groove\n"
            "click, Return or Space while rolling or grooving\tkeep it: at the bar it loops, as many bars (1, 2 or 4) as it "
            "was held, without what you tried before it (or overdubs into the loop playing; Shift+Space: a new loop)\n"
            "click a pad you just touched, drag\twith nothing rolling, shape that drum: sideways tunes, up lengthens, "
            "twist changes its tone\n"
            "run a finger fast across pads\tplays each one it crosses: a fill\n"
            "hold the 808\tsustains; slide sideways to glide through the scale\n"
            "Z X C V · A S D F · Q W E R · U I O P\tthe pads, row by row from the bottom (M , . / and J K L ; too); "
            "hold to roll, Shift for an accent, hold several together for a groove\n"
            "Up / Down\tfaster or slower key rolls\n"
            "Left / Right\tthe kit: house, techno, trap, breaks, garage\n"
            "[ / ]\tless or more swing\n"
            "under the pad, with the mouse\tthe clips (click: launch or stop), the kits, swing, overdub, sounds back\n"
            "Space\tloops what you just played (alone, it sets the tempo; over a loop, from its bar)\n"
            "Shift+Enter\toverdub on or off: what you play joins the loop\n"
            "hold a pad, Delete\ttakes that drum out of the loop playing (again: back in); with no loop, only its "
            "shaping goes back to the kit's\n"
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
        if (repeating())
            parts.prepend(QStringLiteral("REPEAT: pads roll as they land"));
        if (m_keep.active)
            parts << QStringLiteral("keeping %1 at the bar").arg(barsText(m_keep.length));
        if (m_recording >= 0)
            parts.prepend(QStringLiteral("a take waits for slot %1").arg(m_recording + 1));
        if (m_playing >= 0 && m_muted[m_playing]) {
            QStringList out;
            for (int p = 0; p < PadCount; ++p)
                if (m_muted[m_playing] >> p & 1u)
                    out << QString::fromUtf8(padName(p, m_kit)).toUpper();
            parts << QStringLiteral("%1 out of loop %2").arg(out.join('+')).arg(m_playing + 1);
        }
        parts << QStringLiteral("%1 kit, swing %2%")
                     .arg(QString::fromUtf8(kitName(m_kit)))
                     .arg(int(std::lround(m_swing * 100)));
        return parts.join(QStringLiteral(" · "));
    }

    // For the tutorial: hits ever played, by pad and in all; rolls and
    // grooves ever started and how many play under fingers now; clicks
    // that kept something.
    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("hits"))
            return float(m_hits);
        if (name == QLatin1String("pads"))
            return float(m_padsHit);
        if (name == QLatin1String("rolls"))
            return float(m_rolls);
        if (name == QLatin1String("grooves"))
            return float(m_grooves);
        if (name == QLatin1String("keeps"))
            return float(m_keeps);
        if (name == QLatin1String("rolling") || name == QLatin1String("grooving")) {
            const Mode want = name == QLatin1String("rolling") ? Mode::Roll : Mode::Groove;
            int n = 0;
            for (const auto &h : m_holders)
                n += h.used && h.mode == want;
            return float(n);
        }
        return PartControl::tutorialProbe(name);
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
        Repeat,  // a thumb in the REPEAT strip: pads roll as they land
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
        bool landing = true; // its first hit waits a moment for the pressure's peak
        float peak = 0;
        float p0 = 0.5f; // the pressure its landing was played at
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
        bool first = true; // its first step hasn't gone out yet
        bool strip = false; // REPEAT started it: it follows the strip's rate
        bool swing = true; // a pattern finer than sixteenths isn't swung
        Pattern pattern;
        void setPattern(const Pattern &p) {
            pattern = p;
            swing = true;
            for (int i = 0; i < p.count; ++i)
                swing &= p.steps[i].tick % 6 == 0;
        }
        void setRoll(int r) {
            rate = r;
            setPattern(rollPattern(r));
        }
    };

    // A click's keep: what is held becomes a loop at a bar line.
    struct Keep {
        bool active = false, overdub = false, overdubOn = false;
        double from = 0, until = 0; // beats
        double length = kBeatsPerBar;
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
        if (t.y < kStripY || (t.y < kThumbY && t.size > kThumbSize && t.pressure < kThumbPressure)) {
            // A thumb on the strip: no sound, just REPEAT at the rate under it.
            h->mode = Mode::Repeat;
            h->landing = false;
            h->x = h->x0 = t.x;
            h->y = h->y0 = t.y;
            h->t0 = now;
            h->rate = stripRate(t.x);
            return;
        }
        h->pad = padAt(t.x, t.y);
        h->t0 = now;
        h->x0 = h->x = t.x;
        h->y0 = h->y = t.y;
        h->angle0 = h->angle = t.angle;
        h->pressure = t.pressure;
        h->size = t.size;
        h->peak = t.pressure;
        h->vel = landingVelocity(t.pressure, t.size);
        h->lastCell = h->pad;
        h->cluster = clusterFor(now);
        if (repeating() && h->pad != Sub)
            instantRoll(*h, ctx);
        else if (t.pressure >= kFirmPressure)
            sendLanding(*h, ctx);
    }

    static int stripRate(float x) { return std::clamp(int(x * kRates), 0, kRates - 1); }

    // Is REPEAT held, by a thumb on the strip or by B or N?
    bool repeating() const {
        if (m_repeatKeys)
            return true;
        for (const auto &o : m_holders)
            if (o.used && o.mode == Mode::Repeat)
                return true;
        return false;
    }

    // With REPEAT held, a pad rolls the moment it lands. Its landing is the
    // first step: if a step of the grid is within kSnapSecs it lands on it
    // (waiting a few milliseconds if the step is still to come), and the
    // repeats carry on from the next step, so there is no hole at all.
    void instantRoll(Holder &h, ControlContext &ctx) {
        int rate = std::clamp(defaultRate(h.pad) + (h.key ? m_keyRate : 0), 0, kRates - 1);
        for (const auto &o : m_holders)
            if (o.used && o.mode == Mode::Repeat)
                rate = o.rate;
        h.landing = false;
        if (!h.key) {
            h.vel = landingVelocity(h.peak, h.size);
            h.p0 = h.peak;
        }
        // The landing is step one. Just after a step (within kSnapSecs) it
        // is that step, played at once. Otherwise it plays at once only if
        // the next step is at least kRepeatGapSecs away, longer than a tap
        // lasts, so a quick tap stays one hit; nearer than that, it waits
        // for the next step and lands on it, as note repeat does. Either
        // way the repeats carry on from the next step: no hole, and no two
        // hits close together.
        const double stepBeats = double(rateTicks(rate)) * kBeatsPerBar / kTicks;
        const double landed = ctx.beatAt(h.t0);
        const double swingBeats = double(m_swing - 0.5f) * 0.5;
        auto heardAt = [&](double k) {
            const long tick = (long(std::lround(k * stepBeats * kTicks / kBeatsPerBar)) % kTicks + kTicks) % kTicks;
            const bool swung = rateTicks(rate) >= 6 && tick % 12 == 6;
            return ctx.timeAt(k * stepBeats + (swung ? swingBeats : 0.0));
        };
        double k = std::floor(landed / stepBeats) + 1;
        while (heardAt(k) > h.t0)
            k -= 1; // k: the last step heard at or before the landing
        double t = h.t0;
        double cursor = landed;
        // A quarter of a step at most, so a fast rate's first repeat isn't rushed.
        const double snap = std::min(kSnapSecs, 0.25 * stepBeats * 60.0 / std::max(1.0, ctx.studio().bpm));
        if (h.t0 - heardAt(k) <= snap) {
            cursor = k * stepBeats + 1e-6;
        } else if (heardAt(k + 1) - h.t0 < kRepeatGapSecs) {
            t = heardAt(k + 1);
            cursor = (k + 1) * stepBeats + 1e-6;
        }
        hit(h.pad, h.vel, h.x0 * kCols - float(h.pad % kCols), h.y0 * kRows - float(h.pad / kCols), t, ctx);
        Gen *g = newGen(h, ctx.now(), ctx);
        h.mode = Mode::Roll;
        ++m_rolls;
        h.rate = rate;
        if (g) {
            g->setRoll(rate);
            g->cursor = cursor;
            g->start = cursor;
            g->first = false;
            g->strip = true;
        }
        rebase(h);
    }

    // The landing's hit, timed when the finger came down, as hard as it
    // pressed since.
    void sendLanding(Holder &h, ControlContext &ctx) {
        h.landing = false;
        h.vel = landingVelocity(h.peak, h.size);
        h.p0 = h.peak;
        if (h.pad == Sub)
            sendSubOn(h, h.t0, ctx);
        else
            hit(h.pad, h.vel, h.x0 * kCols - float(h.pad % kCols), h.y0 * kRows - float(h.pad / kCols), h.t0, ctx);
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
        h->landing = false;
        if (h->pad == Sub)
            sendSubOn(*h, now, ctx);
        else if (repeating())
            instantRoll(*h, ctx);
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
            if (h.landing)
                sendLanding(h, ctx);
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
        case Mode::Repeat: {
            const int rate = stripRate(h.x);
            if (rate == h.rate)
                break;
            // Rolls that REPEAT started follow the thumb, each still offset
            // by however far its finger has climbed.
            for (auto &g : m_gens) {
                if (!g.active || !g.strip || g.holder < 0)
                    continue;
                Holder &o = m_holders[size_t(g.holder)];
                o.rate = rate;
                g.setRoll(std::clamp(rate + ladderSteps(o), 0, kRates - 1));
            }
            h.rate = rate;
            break;
        }
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
                mv.i[1] = deg + int(std::lround(m_edit[Sub].tune));
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
        // A rung is a fixed travel, shortened where the pad runs out: the
        // top row can still climb three rungs, the bottom row fall three.
        const int steps = ladderSteps(h);
        const float dx = h.x - h.x0;
        if (g.groove) {
            const int density = std::clamp(h.density + steps, 0, kDensities - 1);
            const int shift = std::clamp(int(std::lround(dx / 0.06f)), -8, 8);
            if (density != g.density || shift != g.shift) {
                g.density = density;
                g.shift = shift;
                g.setPattern(groovePattern(g.pad, m_kit, density, shift));
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
            g.dyn = g.groove ? grooveVelocity(h) : heldVelocity(h);
        }
    }

    // How many rungs a held finger has climbed (or fallen) since it began
    // to repeat. A rung is a fixed travel, shortened where the pad runs
    // out: the top row can still climb three rungs, the bottom row fall three.
    static int ladderSteps(const Holder &h) {
        const float dy = h.y - h.y0;
        const float room = dy >= 0 ? 1.0f - h.y0 : h.y0;
        const float rung = clampf(room / 3.5f, 0.025f, kLadderStep);
        return int(std::lround(dy / rung));
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
        if (countTap && h.landing)
            sendLanding(h, ctx);
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
            if (h.landing)
                sendLanding(h, ctx);
            if (together >= 2 || grooving) {
                becomeGroove(h, now, ctx);
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
            // From the first step at least kFirstStepSecs after landing, or
            // a hair before now if that has passed: never a hole.
            // (A swung step plays late, so it counts from where it is heard.)
            const double swung = double(m_swing - 0.5f) * 0.5;
            g.cursor = std::max(ctx.beatAt(h.t0 + kFirstStepSecs) - swung, ctx.beatAt(now - 0.008));
            g.start = g.cursor;
            g.sx = std::clamp(h.x0 * kCols - float(h.pad % kCols), 0.0f, 1.0f);
            g.sy = std::clamp(h.y0 * kRows - float(h.pad / kCols), 0.0f, 1.0f);
            g.dyn = heldVelocity(h);
            h.gen = int(i);
            return &g;
        }
        return nullptr;
    }

    // How hard a held pad keeps playing: as hard as it landed, and pressing
    // harder or easing off from there swells or softens it, a pressure
    // step for a velocity step.
    static float heldVelocity(const Holder &h) {
        return h.key ? h.vel : clampf(h.vel + (h.pressure - h.p0), 0.05f, 1.0f);
    }
    // A groove sits nearer full level: a relaxed hand grooves at about the
    // level of a firm hit, and pressure still swells or softens it.
    static float grooveVelocity(const Holder &h) { return clampf(0.55f + 0.5f * heldVelocity(h), 0.4f, 1.05f); }

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
        ++m_rolls;
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
        ++m_grooves;
        h.density = 1;
        if (g) {
            g->groove = true;
            g->dyn = grooveVelocity(h);
            g->density = 1;
            g->setPattern(groovePattern(h.pad, m_kit, 1, 0));
        }
        rebase(h);
    }

    // --- the click -------------------------------------------------------------

    // Ctrl+Z with a roll or groove kept but its pass not begun (or its bar
    // not reached): that keep is the newest thing done, so it goes, and
    // the loop it would have gone into stays as it was.
    bool takeBackPending(ControlContext &ctx) override {
        if (!m_keep.active || (m_keep.overdub && m_keep.overdubOn))
            return false;
        dropKept();
        m_keep = Keep{};
        ctx.notice(QStringLiteral("Took back the kept roll before it went in"));
        return true;
    }

    // The studio took back the pass a kept roll was playing into: the roll
    // stops with it, rather than playing on to the end of the pass.
    void undone(ControlContext &ctx) override {
        if (!m_keep.active)
            return;
        if (m_keep.overdubOn && !m_userOverdub)
            ctx.overdub(false);
        dropKept();
        m_keep = Keep{};
    }

    void dropKept() {
        for (auto &g : m_gens)
            if (g.active && g.until >= 0 && g.holder < 0)
                g.active = false;
    }

    bool holdingGen() const {
        for (const auto &g : m_gens)
            if (g.active && !g.preview && g.holder >= 0)
                return true;
        return false;
    }

    // Keeps what rolls and grooves under the hand: a click, or Space.
    void keepHeld(double now, ControlContext &ctx, bool fresh = false) {
        for (auto &h : m_holders)
            if (h.used && !h.key && h.mode == Mode::Pending) {
                if (h.pad == Sub)
                    sendOff(h, now, ctx);
                h.mode = Mode::Kept;
            }
        keep(now, ctx, fresh);
    }

    void click(double now, ControlContext &ctx) {
        // While anything rolls or grooves under a finger, a click keeps it,
        // whichever finger pressed: a thumb that came down to click is only
        // the click.
        if (holdingGen()) {
            keepHeld(now, ctx);
            return;
        }
        // Otherwise fresh fingers grab their drums' sounds.
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
        if (editing)
            ctx.notice(QStringLiteral("Drag to shape: sideways tunes, up lengthens, twist for tone"));
    }

    // Keeps every held roll and groove: they play on by themselves to the
    // next bar line, and that bar becomes a loop (or, with a loop playing,
    // they are overdubbed into one whole pass of it).
    void keep(double now, ControlContext &ctx, bool fresh = false) {
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
        if (!fresh && pv.playing >= 0 && pv.clips[pv.playing].lengthBeats > 0) {
            k.overdub = true;
            k.from = std::ceil(beat / kBeatsPerBar - 1e-6) * kBeatsPerBar;
            k.until = k.from + pv.clips[pv.playing].lengthBeats;
            k.slot = pv.playing;
        } else {
            // Whole bars of it, ending on a bar line: as many (one, two or
            // four) as it has been held, so a groove that was varied while
            // it played keeps its variation.
            const double need = std::max(beat, first + kBeatsPerBar);
            k.from = std::ceil(need / kBeatsPerBar - 1e-6) * kBeatsPerBar;
            k.until = k.from;
            for (double bars : {2.0, 4.0})
                if (k.from - bars * kBeatsPerBar >= first - 0.3)
                    k.length = bars * kBeatsPerBar;
            k.slot = freeSlot(ctx);
            clearMutes(k.slot, ctx);
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
        ++m_keeps;
        ctx.notice(k.overdub ? QStringLiteral("Into the loop from the bar, for one pass")
                             : QStringLiteral("The last %1 into slot %2 at the bar").arg(barsText(k.length)).arg(k.slot + 1));
    }

    // --- scheduling ------------------------------------------------------------

    void schedule(double now, ControlContext &ctx) {
        // The longest gap between ticks lately, forgotten over a second or so.
        if (m_lastTick > 0 && now > m_lastTick)
            m_tickGap = std::max(now - m_lastTick, m_tickGap * std::exp(-(now - m_lastTick) / 1.5));
        m_lastTick = now;
        const double lead = std::clamp(m_tickGap + kTickMargin, kFirstLeadSecs, kMaxLeadSecs);
        const double beatNow = ctx.beatAt(now);
        const double horizon = ctx.beatAt(now + kLookahead);
        const double tight = ctx.beatAt(now + lead); // how far ahead a held step may go

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
            sendSteps(beatNow, m_keep.from - 1e-6, tight, ctx);
            if (!m_userOverdub)
                ctx.overdub(true);
            m_keep.overdubOn = true;
        }
        sendSteps(beatNow, horizon, tight, ctx);
        if (m_keep.active && m_keep.overdub && horizon >= m_keep.until) {
            if (!m_userOverdub)
                ctx.overdub(false);
            m_keep = Keep{};
        }
        if (m_keep.active && !m_keep.overdub && beatNow >= m_keep.from + 0.02) {
            ctx.capture(m_keep.slot, ctx.timeAt(m_keep.from - m_keep.length), ctx.timeAt(m_keep.from), 0.85f);
            m_keep = Keep{};
        }
    }

    // Sends every step of every generator from its cursor up to `to`.
    void sendSteps(double beatNow, double to, double tight, ControlContext &ctx) {
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
            // While a finger or key holds it, each step goes out only just
            // before it is heard (swing included), so a lift stops it at
            // once and a finger that lifts before the first step was a tap.
            // The studio holds a command with its exact time until its
            // block, so this costs no accuracy. Kept generators, with no
            // finger, look the full lookahead ahead.
            const bool gated = g.holder >= 0;
            const double swing = double(m_swing - 0.5f) * 0.5;
            constexpr double bar = kBeatsPerBar;
            bool held = false;
            for (long b0 = long(std::floor(g.cursor / bar)); !held && double(b0) * bar <= end; ++b0) {
                for (int i = 0; i < g.pattern.count; ++i) {
                    const Step &s = g.pattern.steps[i];
                    const double b = double(b0) * bar + double(s.tick) * bar / kTicks;
                    if (b <= g.cursor + 1e-9 || b > end + 1e-9)
                        continue;
                    if (finishing && !g.untilInclusive && b >= g.until - 1e-9)
                        continue;
                    if (b < beatNow - 0.02)
                        continue; // too late to play in time
                    if (gated && b + (g.swing && s.tick % 12 == 6 ? swing : 0.0) > tight) {
                        end = b - 1e-6; // not yet: come back for it
                        held = true;
                        break;
                    }
                    sendStep(g, s, b, ctx);
                    g.first = false;
                }
            }
            g.cursor = end;
            if (finishing && !held)
                g.active = false;
        }
    }

    void sendStep(const Gen &g, const Step &s, double beat, ControlContext &ctx) {
        // Swing: every other sixteenth comes late, the MPC way.
        double b = beat;
        if (g.swing && s.tick % 12 == 6)
            b += double(m_swing - 0.5f) * 0.5;
        // No two strokes alike: a few percent of velocity, the same for the
        // same step of the same pad every bar, so a loop of it still loops.
        uint32_t hash = uint32_t(g.pad) * 2654435761u ^ uint32_t(s.tick) * 40503u;
        hash ^= hash >> 13;
        hash *= 0x5bd1e995u;
        // A roll stays close to the hit that began it; a groove breathes more.
        const float spread = g.groove ? 0.07f : 0.03f;
        const float human = 1.0f + spread * (float(hash >> 8 & 0xFFFF) / 32768.0f - 1.0f);
        Command c;
        c.type = Hit;
        c.flags = g.preview ? Command::NoRecord : 0;
        c.t = ctx.timeAt(b);
        c.i[0] = g.pad;
        c.i[1] = g.degrees + s.degree;
        c.f[0] = clampf(s.vel * g.dyn * human, 0.05f, 1.0f);
        c.f[1] = g.sx;
        c.f[2] = g.sy;
        c.f[3] = g.semis;
        c.f[4] = g.decay;
        if (g.pad == Sub) // a groove's 808 rings a little over a sixteenth
            c.f[5] = float(0.4 * 60.0 / std::max(40.0, ctx.studio().bpm));
        stamp(c);
        ctx.send(c);
        if (!g.preview) {
            ++m_hits;
            m_padsHit |= 1u << g.pad;
        }
    }

    // Every hit carries the page's whole sound for its pad, the kit and the
    // shaping, so a loop sounds as it was played whatever comes later.
    void stamp(Command &c) const {
        const int pad = std::clamp(c.i[0], 0, PadCount - 1);
        const Edit &e = m_edit[pad];
        c.i[2] = m_kit + 1;
        if (tonal(pad))
            c.i[1] += int(std::lround(e.tune));
        else
            c.f[3] += e.tune;
        c.f[4] += e.decay;
        c.f[6] = e.tone;
    }

    // Holding pads and pressing Delete takes those drums out of the loop
    // that plays (and puts them back). Clips can't be edited from a page, so
    // the engine silences that pad's lane in that slot; the slot forgets it
    // when something new is recorded into it. False when nothing applies.
    bool toggleLanes(ControlContext &ctx) {
        if (m_playing < 0)
            return false;
        uint32_t pads = 0;
        for (const auto &h : m_holders)
            if (h.used && h.mode != Mode::Repeat)
                pads |= 1u << h.pad;
        if (!pads)
            return false;
        QStringList names;
        bool out = false;
        for (int p = 0; p < PadCount; ++p) {
            if (!(pads >> p & 1u))
                continue;
            m_muted[m_playing] ^= 1u << p;
            out = m_muted[m_playing] >> p & 1u;
            sendMute(m_playing, p, out, ctx);
            names << QString::fromUtf8(padName(p, m_kit)).toUpper();
        }
        ctx.notice(QStringLiteral("%1 %2 loop %3").arg(names.join('+'), out ? "out of" : "back in").arg(m_playing + 1));
        return true;
    }

    void sendMute(int slot, int pad, bool on, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = ParamMute;
        c.i[1] = slot;
        c.i[2] = pad;
        c.i[3] = on ? 1 : 0;
        ctx.send(c);
    }

    void clearMutes(int slot, ControlContext &ctx) {
        if (slot < 0 || slot >= kClipSlots)
            return;
        for (int p = 0; p < PadCount; ++p)
            if (m_muted[slot] >> p & 1u)
                sendMute(slot, p, false, ctx);
        m_muted[slot] = 0;
    }

    static QString barsText(double beats) {
        const int bars = int(std::lround(beats / kBeatsPerBar));
        return bars == 1 ? QStringLiteral("bar") : QStringLiteral("%1 bars").arg(bars);
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
        stamp(c);
        ctx.send(c);
        ++m_hits;
        m_padsHit |= 1u << pad;
    }

    void sendSubOn(const Holder &h, double t, ControlContext &ctx) {
        Command c;
        c.type = Command::On;
        c.id = h.src & 0xFFFFFF;
        c.t = t;
        c.i[0] = Sub;
        c.f[0] = h.vel;
        stamp(c);
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

    void shiftSwing(int dir, ControlContext &ctx) {
        m_swing = clampf(m_swing + 0.02f * float(dir), 0.5f, 0.7f);
        ctx.notice(QStringLiteral("Swing %1%").arg(int(std::lround(m_swing * 100))));
    }

    void toggleOverdub(ControlContext &ctx) {
        m_userOverdub = !m_userOverdub;
        ctx.overdub(m_userOverdub);
        ctx.notice(m_userOverdub ? QStringLiteral("Overdub: what you play joins the loop")
                                 : QStringLiteral("Overdub off"));
    }

    void resetSounds(ControlContext &ctx) {
        for (int p = 0; p < PadCount; ++p)
            if (m_edit[p].any()) {
                m_edit[p] = {};
                sendEdit(p, ctx);
            }
        ctx.notice(QStringLiteral("Every drum back to the kit's sound"));
    }

    // The mouse on the band under the pad.
    enum Spot { SpotKit = 100, SpotSwingDown = 200, SpotSwingUp, SpotOverdub, SpotReset };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
            return true;
        if (id >= SpotKit && id < SpotKit + KitCount) {
            if (id - SpotKit != m_kit)
                setKit(id - SpotKit, ctx);
            return true;
        }
        switch (id) {
        case SpotSwingDown: shiftSwing(-1, ctx); return true;
        case SpotSwingUp: shiftSwing(1, ctx); return true;
        case SpotOverdub: toggleOverdub(ctx); return true;
        case SpotReset: resetSounds(ctx); return true;
        default: return false;
        }
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
                g.setPattern(groovePattern(g.pad, m_kit, g.density, g.shift));
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
    console::Hotspots m_spots; // what paint() drew to be clicked
    int m_keyRate = 0;
    Edit m_edit[PadCount];
    std::array<Holder, 24> m_holders{};
    std::array<Gen, 24> m_gens{};
    Keep m_keep;
    int m_clusterSerial = 0;
    bool m_button = false;
    bool m_userOverdub = false;
    uint8_t m_repeatKeys = 0; // B and N, held as REPEAT
    double m_lastTick = 0, m_tickGap = 0; // when schedule() last ran, and the longest recent gap
    int m_playing = -1, m_recording = -1; // this part's clips, as the studio last said
    uint32_t m_muted[kClipSlots] = {};  // per slot, the pads taken out of its loop
    uint32_t m_hits = 0, m_padsHit = 0, m_rolls = 0, m_grooves = 0, m_keeps = 0; // for the tutorial
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

        // The drum's decay, as this kit and any shaping make it: over a
        // second, a hat is a spike and a crash a long tail.
        const bool shaping = gen && gen->preview;
        if (!gen || shaping) {
            const QRectF box(cell.right() - cell.width() * 0.42 - 8 * s, cell.bottom() - cell.height() * 0.42 - 6 * s,
                             cell.width() * 0.42, cell.height() * 0.32);
            const float tau = displayDecay(pad, m_kit) * std::exp2(m_edit[pad].decay);
            QPainterPath curve(QPointF(box.left(), box.bottom()));
            for (int i = 0; i <= 48; ++i) {
                const float t = float(i) / 48.0f;
                curve.lineTo(box.left() + box.width() * t, box.bottom() - box.height() * std::exp(-t * 1.2f / tau));
            }
            curve.lineTo(box.right(), box.bottom());
            p.setPen(Qt::NoPen);
            p.setBrush(c::alpha(col, 0.07f + 0.25f * std::max(live, replay)));
            p.drawPath(curve);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c::alpha(col, 0.3f + 0.6f * std::max(live, replay)), 1.2));
            p.drawPath(curve);
        }

        // A drum that's been shaped shows how: tune, length, tone; while
        // it's being shaped, large and named.
        const Edit &e = m_edit[pad];
        if (shaping) {
            const float span = tonal(pad) ? 7.0f : 12.0f;
            const float values[3] = {0.5f + 0.5f * e.tune / span, 0.5f + 0.25f * e.decay, 0.5f + 0.5f * e.tone};
            const char *names[3] = {"TUNE", "LENGTH", "TONE"};
            for (int i = 0; i < 3; ++i) {
                const QRectF row(cell.left() + 8 * s, cell.top() + label + 16 * s + i * 22 * s, cell.width() * 0.62, 18 * s);
                c::text(p, QRectF(row.left(), row.top(), 64 * s, row.height()), QString::fromUtf8(names[i]), col, label, true);
                c::gauge(p, QRectF(row.left() + 70 * s, row.top() + 3 * s, row.width() - 70 * s, row.height() - 6 * s),
                         values[i], col, 12, true);
            }
        } else if (e.any()) {
            const QRectF g(cell.left() + 8 * s, cell.top() + label + 14 * s, std::min(cell.width() * 0.45, 90 * s), 4 * s);
            const float span = tonal(pad) ? 7.0f : 12.0f;
            c::gauge(p, g, 0.5f + 0.5f * e.tune / span, col, 10, true);
            c::gauge(p, g.translated(0, 7 * s), 0.5f + 0.25f * e.decay, col, 10, true);
            c::gauge(p, g.translated(0, 14 * s), 0.5f + 0.5f * e.tone, col, 10, true);
        }

        // A pad that repeats shows its bar of steps along the bottom, the
        // one just played lit, and what it is doing in the middle.
        if (gen) {
            // (Above the REPEAT strip on the bottom row.)
            const double lift = row == 0 ? pc.pad.height() * kStripY + 6 * s : 0.0;
            const QRectF strip(cell.left() + 8 * s, cell.bottom() - 18 * s - lift, cell.width() - 16 * s, 10 * s);
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
            // Under a finger its tag says this; a kept one has no finger.
            bool fingerOn = gen->holder >= 0 && !m_holders[size_t(gen->holder)].key;
            for (const auto &h : m_holders)
                fingerOn |= h.used && !h.key && h.pad == pad && h.mode == Mode::Kept;
            if (!shaping && !fingerOn) {
                QString what = gen->groove ? QString::fromUtf8(densityName(gen->density)).toUpper()
                                           : QString::fromUtf8(rateName(gen->rate));
                if (gen->until >= 0)
                    what += QStringLiteral(" · KEPT");
                c::text(p, QRectF(cell.left(), cell.center().y() - label * 1.4, cell.width(), 2 * label), what,
                        c::alpha(col, 0.55f), int(label * 1.3), true, Qt::AlignCenter);
            }
        }
    }

    // Pads taken out of the playing loop.
    const int playing = pc.view->playing;
    if (playing >= 0 && playing < kClipSlots && m_muted[playing])
        for (int pad = 0; pad < PadCount; ++pad) {
            if (!(m_muted[playing] >> pad & 1u))
                continue;
            const int row = pad / kCols, column = pad % kCols;
            const QRectF cell = pc.rect(float(column) / kCols, float(row) / kRows, float(column + 1) / kCols,
                                        float(row + 1) / kRows)
                                    .adjusted(5 * s, 5 * s, -5 * s, -5 * s);
            p.setPen(QPen(c::alpha(c::red(), 0.7f), 2));
            p.drawLine(cell.bottomLeft(), cell.topRight());
            c::tag(p, QPointF(cell.center().x(), cell.bottom() - 14 * s), QStringLiteral("OUT OF LOOP %1").arg(playing + 1),
                   c::red(), small, true);
        }

    // The 808's note while it sounds.
    if (data[32] > 0.002f) {
        const QRectF cell = pc.rect(0, 0.25f, 0.25f, 0.5f);
        const int deg = int(std::lround(data[33])) + int(std::lround(m_edit[Sub].tune));
        const int pc12 = (int(std::lround(harm.midi(deg, 0))) % 12 + 12) % 12;
        c::tag(p, cell.center(), QString::fromUtf8(Tuning::noteName(pc12)), col, int(label * 1.2), true);
    }

    // The REPEAT strip along the near edge, its rates left to right; lit
    // under a resting thumb. B or N held lights the whole of it.
    {
        int thumb = -1;
        for (const auto &h : m_holders)
            if (h.used && h.mode == Mode::Repeat)
                thumb = h.rate;
        const bool on = thumb >= 0 || m_repeatKeys;
        const QRectF strip = pc.rect(0, 0, 1, kStripY);
        for (int r = 0; r < kRates; ++r) {
            const QRectF seg(strip.left() + strip.width() * r / kRates + 2, strip.top() + 1,
                             strip.width() / kRates - 4, strip.height() - 2);
            const bool lit = r == thumb || (thumb < 0 && m_repeatKeys);
            p.fillRect(seg, lit ? col : c::alpha(col, on ? 0.25f : 0.08f));
            c::text(p, seg, r == 0 ? QStringLiteral("REPEAT %1").arg(rateName(r)) : QString::fromUtf8(rateName(r)),
                    lit ? QColor::fromRgb(c::kInk) : c::alpha(col, 0.6f), int(std::clamp(seg.height() * 0.6, 8.0, 12.0)),
                    true, Qt::AlignCenter);
        }
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
            case Mode::Repeat: break; // the strip under it says so
            default: break;
            }
        }
        c::finger(p, pc, t, col, what, glow);
    }

    // A keep on its way to the bar line, or a take the session is waiting
    // to make into a loop.
    if (m_keep.active) {
        const double left = std::max(0.0, m_keep.from - pc.beat);
        const QString msg = m_keep.overdub
                                ? QStringLiteral("INTO THE LOOP IN %1 BEATS").arg(left, 0, 'f', 1)
                                : QStringLiteral("%1 INTO SLOT %2 IN %3 BEATS")
                                      .arg(barsText(m_keep.length).toUpper())
                                      .arg(m_keep.slot + 1)
                                      .arg(left, 0, 'f', 1);
        c::tag(p, QPointF(pc.pad.center().x(), pc.pad.top()), msg,
               std::fmod(pc.time, 0.5) < 0.25 ? col : c::alpha(col, 0.6f), 13, true);
    }

    if (pc.view->recording >= 0 && !m_keep.active)
        c::tag(p, QPointF(pc.pad.center().x(), pc.pad.top()),
               pc.view->takeBeats > 0
                   ? QStringLiteral("%1 INTO SLOT %2 IN %3 BEATS · SPACE CALLS IT OFF")
                         .arg(barsText(pc.view->takeBeats).toUpper())
                         .arg(pc.view->recording + 1)
                         .arg(std::max(0.0, pc.view->takeAt - pc.beat), 0, 'f', 1)
                   : QStringLiteral("LOOPING INTO SLOT %1 · SPACE CALLS IT OFF").arg(pc.view->recording + 1),
               std::fmod(pc.time, 0.5) < 0.25 ? c::red() : c::alpha(c::red(), 0.6f), 13, true);
    c::beats(p, pc, col);
    // Below the pad, every one clickable: the clips and the kits, then the
    // swing, overdub and the drums' own sounds back.
    m_spots.clear();
    c::clips(p, c::bandRow(pc, 0, 0, 0.55), *pc.view, col, pc.time, &m_spots);
    QStringList kits;
    for (int k = 0; k < KitCount; ++k)
        kits << QString::fromUtf8(kitName(k)).toUpper();
    c::chips(p, pc, c::bandRow(pc, 0, 0.57, 1), kits, m_kit, &m_spots, SpotKit);
    const QRectF row = c::bandRow(pc, 1, 0, 0.55);
    const double w = row.width() / 4;
    c::chip(p, pc, QRectF(row.left(), row.top(), w - 4, row.height()), QStringLiteral("SWING −  ["), false, &m_spots,
            SpotSwingDown);
    const QRectF gauge(row.left() + w, row.top(), w - 4, row.height());
    c::text(p, gauge.adjusted(0, 0, 0, -12), QStringLiteral("SWING %1%").arg(int(std::lround(m_swing * 100))), col, 11,
            true, Qt::AlignCenter);
    c::gauge(p, QRectF(gauge.left() + 6, gauge.bottom() - 10, gauge.width() - 12, 7), (m_swing - 0.5f) / 0.2f, col, 10);
    c::chip(p, pc, QRectF(row.left() + 2 * w, row.top(), w - 4, row.height()), QStringLiteral("SWING +  ]"), false,
            &m_spots, SpotSwingUp);
    c::chips(p, pc, c::bandRow(pc, 1, 0.57, 1),
             {QStringLiteral("OVERDUB  ⇧⏎"), QStringLiteral("SOUNDS BACK  DEL")}, m_userOverdub ? 0 : -1, &m_spots,
             SpotOverdub);
}

}

PartSpec makeDrums() {
    PartSpec s;
    s.followsChords = false;
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
