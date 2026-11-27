// Bass: the page for the low end. One finger plays a line in the key; the
// fingers around it bend that line into any bass a dance track needs.
//
// The pad is two parts. Below, the note field: twelve columns of the scale,
// from two degrees under the root to the third an octave and a half up. A
// finger plays the column it lands in; its height is the tone (dark sub at
// the bottom, open and biting at the top), its pressure the accent, its
// twist the sound's morph, a flat finger fattens it, and sliding sideways
// glides to the next note. A second note finger while the first is held
// slides to it without a new attack, as a 303 slides; lifting it slides
// back. Above, the rail: LOOP at its left end, then four zones a finger
// lands in to bend whatever is sounding, the loop included, for as long as
// it stays:
//
//   LOOP  tap: record from the next bar; tap again: close at the bar line,
//         and it loops. Nothing else ever makes a loop.
//   OCT   up an octave (two fingers, two octaves): tap it for octave pumps
//   ROLL  held notes strike in a rhythm locked to the bar; slide sideways
//         for the pattern, up and down for the gate
//   WOB   a tempo-synced wobble; sideways is the rate, up and down the depth
//   GRIT  drive; up and down is how much, sideways warm → tube → fold
//
// Pressing the pad until it clicks strikes the sounding note again with an
// accent, or, with only shapers down, latches them so they stay.
//
// The keyboard is a second way in: two rows of the scale (Z to / and A to '
// an octave up; Ö Ä on a Nordic keyboard), the first ten sounds on Q to P
// (Shift: square), and the keys every part page shares (docs/PARTS.md 4b):
// ← → the sound (every one of the sixteen: the six past P have no
// letter, and → from P steps on to them), ↑ ↓ the octave, Shift+← → the
// roll, - + the tone, Return latch, Shift+Return overdub, the key left
// of 1 (§ or `) follow the chords, Delete free the shapers, F1–F8 and
// Backspace the clips. Shift+↑ ↓ is the slide. The older keys ([ ] = \ F9 F10, Enter for LOOP) still work; O and
// P, once follow and let go, are the ninth and tenth sounds now.
// Everything a key does, the band under the pad does for the mouse.

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "bass.h"
#include "console.h"
#include "parts.h"
#include "songfile.h"

namespace tf {

namespace {

using namespace bass;

constexpr float kField = 0.76f; // the note field below this height, the rail above
constexpr int kLowDegree = -2;  // the leftmost column: two degrees under the root
constexpr int kColumns = 12;
constexpr int kKeyIdBase = 0x200000; // ids for notes played on the keyboard
constexpr float kLoopW = 0.08f; // the rail's LOOP button, at its left end
constexpr float kRailMid = 0.5f * (kField + 1.0f);  // a zone's two rows of stops meet here

// The stops marked in the ROLL and WOB zones, four across and two rows
// down: where a finger lands is what it plays, sliding along the row
// reaches the rest. WOB's top row is the straight rates, the bottom the
// triplets and 1/32; ROLL's top row leaves the downbeat to the kick.
constexpr int kWobStops[2][4] = {{0, 1, 3, 5}, {2, 4, 6, 7}};
constexpr int kRollStops[2][4] = {{0, 1, 2, 3}, {7, 4, 5, 6}};

enum Role { NoRole, NoteRole, OctRole, RollRole, WobRole, GritRole, FiltRole, LoopRole };

// The rail after LOOP, left to right: five zones, ROLL and WOB widest for
// their four stops across.
constexpr int kZones = 5;
constexpr Role kZoneRole[kZones] = {OctRole, RollRole, WobRole, FiltRole, GritRole};
constexpr float kZoneEdge[kZones + 1] = {kLoopW, 0.24f, 0.45f, 0.66f, 0.83f, 1.0f};

int zoneOf(Role r) {
    for (int z = 0; z < kZones; ++z)
        if (kZoneRole[z] == r)
            return z;
    return 0;
}
float zoneLeft(Role r) { return kZoneEdge[zoneOf(r)]; }
float zoneWidth(Role r) { return kZoneEdge[zoneOf(r) + 1] - kZoneEdge[zoneOf(r)]; }

const char *roleName(Role r) {
    switch (r) {
    case OctRole: return "OCT";
    case RollRole: return "ROLL";
    case WobRole: return "WOB";
    case GritRole: return "GRIT";
    case FiltRole: return "FILTER";
    default: return "";
    }
}

Shaper shaperOf(Role r) {
    return r == OctRole    ? ShapeOct
           : r == RollRole ? ShapeRoll
           : r == WobRole  ? ShapeWob
           : r == FiltRole ? ShapeFilter
                           : ShapeGrit;
}
Role roleOf(Shaper s) {
    return s == ShapeRoll ? RollRole : s == ShapeWob ? WobRole : s == ShapeGrit ? GritRole : s == ShapeFilter ? FiltRole : OctRole;
}

// What the twist does on a sound, as its readouts name it.
QString morphName(int sound) {
    switch (sound) {
    case Acid: return QStringLiteral("SQUELCH");
    case Moog: return QStringLiteral("CONTOUR");
    case Upright: return QStringLiteral("RING");
    case Log: return QStringLiteral("HONK");
    case Tuba: return QStringLiteral("BLARE");
    case Chip: return QStringLiteral("ARP");
    case Tabla: return QStringLiteral("SWOOP");
    case Didge: return QStringLiteral("VOICE");
    case Marimbula: return QStringLiteral("SPOT");
    default: return QStringLiteral("MORPH");
    }
}

// Twist from where the finger landed, folded to ±90° (a contact ellipse
// has no front or back).
float twistOf(float angle, float angle0) {
    float d = angle - angle0;
    while (d > 0.5f * kPi)
        d -= kPi;
    while (d < -0.5f * kPi)
        d += kPi;
    return d;
}

class Control : public PartControl {
public:
    Control() {
        for (int s = 0; s < SoundCount; ++s)
            m_morph[s] = 0.3f;
    }

    // --- touch --------------------------------------------------------------

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        const bool click = f.button && !m_button;
        m_button = f.button;

        for (auto &h : m_fingers) {
            if (h.id < 0)
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
                if (h)
                    land(*h, t, f.t, ctx);
                continue;
            }
            drag(*h, t, f.t, ctx);
        }
        if (click)
            this->click(f.t, ctx);
        settle(f.t, ctx);
    }

    void idle(double t, ControlContext &ctx) override { settle(t, ctx); }

    // A bend held still for a moment settles: the engine lands it on the
    // nearest note of the key.
    void settle(double t, ControlContext &ctx) {
        for (auto &h : m_fingers)
            if (h.id >= 0 && h.role == OctRole && h.row == 1 && !h.settled && t - h.lastMove > 0.15 &&
                newestIn(OctRole, h)) {
                h.settled = true;
                sendShaper(ShapeBend, h.amount, 1, t, ctx, true);
            }
    }

    // Recording goes on whichever page shows: keep its state, close an
    // early second tap at the right moment, and say what was caught.
    void background(double t, ControlContext &ctx) override {
        (void)t;
        const PartView &pv = ctx.studio().parts[ctx.part()];
        const double beat = ctx.beatAt(ctx.now());
        if (m_recSlot >= 0 && m_closeEarly && beat >= m_recStart + 0.5) {
            // Tapped again before the take began: close after its first bar.
            m_closeEarly = false;
            m_closing = true;
            ctx.stopRecording();
        }
        // Any take, Space's or this page's own. Space's land at once, so the
        // studio's count of landed takes is the cue: say what was caught and
        // light the LOOP button for a moment. A take that waits (REC, or
        // Space looping what is sounding) shows while it waits.
        if (!m_takesKnown) {
            m_takesKnown = true;
            m_takesSeen = pv.takes;
        }
        if (pv.takes != m_takesSeen) {
            m_takesSeen = pv.takes;
            const int slot = m_taking >= 0 ? m_taking : m_recSlot >= 0 ? m_recSlot : pv.playing;
            if (slot >= 0 && slot < kClipSlots && pv.clips[slot].state != ClipView::Empty) {
                const ClipView &cv = pv.clips[slot];
                const int beats = int(std::lround(cv.lengthBeats));
                m_landedText = beats >= 4 ? QStringLiteral("%1 %2").arg(beats / 4).arg(beats >= 8 ? "bars" : "bar")
                                          : QStringLiteral("%1 beats").arg(beats);
                m_landedSlot = slot;
                m_landedAt = t;
                ctx.notice(QStringLiteral("Bass loop in slot %1: %2, %3 events, playing")
                               .arg(slot + 1)
                               .arg(m_landedText)
                               .arg(cv.events));
            }
            m_taking = -1;
            m_recSlot = -1;
            m_closing = m_closeEarly = false;
        }
        if (pv.recording >= 0) {
            m_taking = pv.recording;
        } else if (m_taking >= 0) {
            // The view showed a take and now shows none, with nothing landed:
            // it was called off (the session said so).
            m_taking = -1;
            m_recSlot = -1;
            m_closing = m_closeEarly = false;
        }
        // A slot that has emptied takes its loop's shaper settings with it.
        for (int k = 0; k < kClipSlots; ++k) {
            const bool full = pv.clips[k].state != ClipView::Empty;
            if (m_slotFull[k] && !full)
                resetSlot(k, ctx);
            m_slotFull[k] = full;
        }
    }

    void leave(ControlContext &ctx) override {
        for (auto &h : m_fingers)
            if (h.id >= 0) {
                if (h.role == NoteRole)
                    off(h.id, 0, ctx);
                h.id = -1;
            }
        for (auto &k : m_keys)
            if (k.key) {
                off(k.id, 0, ctx);
                k.key = 0;
            }
        // Shapers spring back unless latched; latched ones are settings, and
        // stay with the loop that plays on.
        for (int s = 0; s < ShaperCount; ++s)
            if (!m_latched[s])
                sendShaper(Shaper(s), 0, restB(Shaper(s)), ctx.now(), ctx, true);
        m_frame = Frame{};
        m_button = false;
    }

    // --- the song file ------------------------------------------------------

    // What the player set up and would want back: the sound and how each
    // sound is shaped, the register, tone, slide and follow the next notes
    // take, where each shaper starts, and the shapers latched on (they bend
    // the loops that play on). Sounds, patterns and rates go by name, so a
    // list that grows or reorders still opens an old song as it was.
    void save(QJsonObject &out) const override {
        out[QStringLiteral("sound")] = QString::fromUtf8(recipe(m_sound).name);
        out[QStringLiteral("octave")] = m_octave;
        out[QStringLiteral("tone")] = tidyFloat(m_keyTone);
        out[QStringLiteral("slide")] = tidyFloat(m_glide);
        out[QStringLiteral("follow")] = m_follow;
        QJsonObject morph;
        QJsonArray square;
        for (int s = 0; s < SoundCount; ++s) {
            morph[QString::fromUtf8(recipe(s).name)] = tidyFloat(m_morph[s]);
            if (m_square[s])
                square.append(QString::fromUtf8(recipe(s).name));
        }
        out[QStringLiteral("morph")] = morph;
        out[QStringLiteral("square")] = square;
        out[QStringLiteral("roll")] = rollJson(m_pattern, m_gate);
        out[QStringLiteral("wobble")] = wobbleJson(m_rate, m_depth);
        out[QStringLiteral("grit")] = gritJson(m_grit, m_gritChar);
        QJsonObject latched;
        if (m_latched[ShapeOct])
            latched[QStringLiteral("octave")] = int(m_sent[ShapeOct][0]);
        if (m_latched[ShapeRoll])
            latched[QStringLiteral("roll")] = rollJson(int(m_sent[ShapeRoll][1]), m_sent[ShapeRoll][0]);
        if (m_latched[ShapeWob])
            latched[QStringLiteral("wobble")] = wobbleJson(int(m_sent[ShapeWob][1]), m_sent[ShapeWob][0]);
        if (m_latched[ShapeGrit])
            latched[QStringLiteral("grit")] = gritJson(m_sent[ShapeGrit][0], m_sent[ShapeGrit][1]);
        if (m_latched[ShapeFilter])
            latched[QStringLiteral("filter")] = QJsonObject{{QStringLiteral("cutoff"), tidyFloat(m_sent[ShapeFilter][0])},
                                                            {QStringLiteral("morph"), tidyFloat(m_sent[ShapeFilter][1])}};
        out[QStringLiteral("latched")] = latched;
    }

    // Everything missing or unreadable falls back to how a new page starts,
    // never to what the song open before had. The page's own settings reach
    // the engine with the next note it plays; the latches are sent now, as a
    // latch from the rail would be, and every other shaper springs back.
    void load(const QJsonObject &in, ControlContext &ctx) override {
        const Control home;
        const int sound = soundNamed(in.value(QStringLiteral("sound")));
        m_sound = sound >= 0 ? sound : home.m_sound;
        m_octave = int(std::lround(number(in, "octave", float(home.m_octave), 0, 2)));
        m_keyTone = number(in, "tone", home.m_keyTone, 0, 1);
        m_glide = number(in, "slide", home.m_glide, 0, 1);
        const QJsonValue follow = in.value(QStringLiteral("follow"));
        m_follow = follow.isBool() ? follow.toBool() : home.m_follow;
        const QJsonObject morph = in.value(QStringLiteral("morph")).toObject();
        for (int s = 0; s < SoundCount; ++s) {
            m_morph[s] = number(morph, recipe(s).name, home.m_morph[s], 0, 1);
            m_square[s] = false;
        }
        for (const QJsonValue &v : in.value(QStringLiteral("square")).toArray()) {
            // A sound with no saw has none to turn square, as its key says
            // (a string's switch is its palm mute).
            const int s = soundNamed(v);
            if (s >= 0 && hasSwitch(s))
                m_square[s] = true;
        }
        const QJsonObject roll = in.value(QStringLiteral("roll")).toObject();
        const int pattern = patternNamed(roll.value(QStringLiteral("pattern")));
        m_pattern = pattern >= 0 ? pattern : home.m_pattern;
        m_gate = number(roll, "gate", home.m_gate, 0.15f, 1);
        const QJsonObject wobble = in.value(QStringLiteral("wobble")).toObject();
        const int rate = rateNamed(wobble.value(QStringLiteral("rate")));
        m_rate = rate >= 0 ? rate : home.m_rate;
        m_depth = number(wobble, "depth", home.m_depth, 0.05f, 1);
        const QJsonObject grit = in.value(QStringLiteral("grit")).toObject();
        m_grit = number(grit, "amount", home.m_grit, 0.02f, 1);
        m_gritChar = number(grit, "character", home.m_gritChar, 0, 1);

        // The latches: each shaper goes where the file had it latched, or
        // back to rest. A latch whose pattern or rate is unknown takes the
        // one its shaper starts with.
        const QJsonObject latched = in.value(QStringLiteral("latched")).toObject();
        auto shaper = [&](const char *name) { return latched.value(QLatin1String(name)).toObject(); };
        auto has = [&](const char *name) { return latched.value(QLatin1String(name)).isObject(); };
        float a[ShaperCount] = {}, b[ShaperCount] = {};
        bool on[ShaperCount] = {};
        if (latched.value(QStringLiteral("octave")).isDouble()) {
            on[ShapeOct] = true;
            a[ShapeOct] = std::round(number(latched, "octave", 0, 0, 2));
        }
        if (has("roll")) {
            const int p = patternNamed(shaper("roll").value(QStringLiteral("pattern")));
            on[ShapeRoll] = true;
            a[ShapeRoll] = number(shaper("roll"), "gate", m_gate, 0.15f, 1);
            b[ShapeRoll] = float(p >= 0 ? p : m_pattern);
        }
        if (has("wobble")) {
            const int r = rateNamed(shaper("wobble").value(QStringLiteral("rate")));
            on[ShapeWob] = true;
            a[ShapeWob] = number(shaper("wobble"), "depth", m_depth, 0.05f, 1);
            b[ShapeWob] = float(r >= 0 ? r : m_rate);
        }
        if (has("grit")) {
            on[ShapeGrit] = true;
            a[ShapeGrit] = number(shaper("grit"), "amount", m_grit, 0.02f, 1);
            b[ShapeGrit] = number(shaper("grit"), "character", m_gritChar, 0, 1);
        }
        if (has("filter")) {
            on[ShapeFilter] = true;
            a[ShapeFilter] = number(shaper("filter"), "cutoff", 0, -0.7f, 0.7f);
            b[ShapeFilter] = number(shaper("filter"), "morph", 0, -1, 1);
        }
        m_latchNext = false;
        const double now = ctx.now();
        for (int s = 0; s < ShaperCount; ++s) {
            m_latched[s] = on[s];
            sendShaper(Shaper(s), on[s] ? a[s] : 0.0f, on[s] ? b[s] : restB(Shaper(s)), now, ctx, true);
        }
        // The engine still keeps each slot's shapers from the song before (a
        // slot full in both was never seen to empty): the new loops start
        // clean and set their own as they play.
        const PartView &pv = ctx.studio().parts[ctx.part()];
        for (int k = 0; k < kClipSlots; ++k) {
            resetSlot(k, ctx);
            m_slotFull[k] = pv.clips[k].state != ClipView::Empty;
        }
    }

    // A number from the file, clamped to its range; anything else (missing,
    // a string, a bool) is the default.
    static float number(const QJsonObject &o, const char *key, float def, float lo, float hi) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            return def;
        return float(std::clamp(v.toDouble(), double(lo), double(hi)));
    }
    // A sound, pattern or rate by its name, in any case; −1 if none has it.
    template <typename Name>
    static int named(const QJsonValue &v, int count, Name name) {
        const QString s = v.toString();
        for (int k = 0; k < count && !s.isEmpty(); ++k)
            if (s.compare(QString::fromUtf8(name(k)), Qt::CaseInsensitive) == 0)
                return k;
        return -1;
    }
    static int soundNamed(const QJsonValue &v) { return named(v, SoundCount, [](int k) { return recipe(k).name; }); }
    static int patternNamed(const QJsonValue &v) { return named(v, kPatternCount, [](int k) { return kPatterns[k].name; }); }
    static int rateNamed(const QJsonValue &v) { return named(v, kRateCount, [](int k) { return kRateNames[k]; }); }
    static QJsonObject rollJson(int pattern, float gate) {
        return {{QStringLiteral("pattern"), QString::fromUtf8(kPatterns[std::clamp(pattern, 0, kPatternCount - 1)].name)},
                {QStringLiteral("gate"), tidyFloat(gate)}};
    }
    static QJsonObject wobbleJson(int rate, float depth) {
        return {{QStringLiteral("rate"), QString::fromUtf8(kRateNames[std::clamp(rate, 0, kRateCount - 1)])},
                {QStringLiteral("depth"), tidyFloat(depth)}};
    }
    static QJsonObject gritJson(float amount, float character) {
        return {{QStringLiteral("amount"), tidyFloat(amount)}, {QStringLiteral("character"), tidyFloat(character)}};
    }

    // --- keys ---------------------------------------------------------------

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const int code = normalise(k.key);
        const int degree = keyDegree(code);
        const bool shift = k.modifiers & Qt::ShiftModifier;
        if (degree != kNoKey) {
            if (k.repeat)
                return true;
            if (k.down)
                keyOn(code, degree, shift, ctx);
            else
                keyOff(code, ctx);
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift) {
                ctx.clear(slot);
                resetSlot(slot, ctx);
                ctx.notice(QStringLiteral("Bass slot %1 cleared").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        // The shared keys (docs/PARTS.md 4b), each with its old key beside
        // it. The primary ones need no AltGr on a Swedish keyboard.
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (shift)
                stepRoll(k.key == Qt::Key_Right ? 1 : -1, ctx);
            else
                setSound((m_sound + (k.key == Qt::Key_Right ? 1 : SoundCount - 1)) % SoundCount, ctx);
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            // Shift+↑ ↓ is the slide, the bass's second vertical setting.
            if (shift)
                stepSlide(k.key == Qt::Key_Up ? 1 : -1, ctx);
            else
                stepOctave(k.key == Qt::Key_Up ? 1 : -1, ctx);
            return true;
        // Morph keeps its old keys, Shift+[ ] (a shifted bracket may arrive
        // as the brace it types); on a Swedish keyboard it is the chip, the
        // finger's twist and the FILTER zone.
        case Qt::Key_BraceLeft:
        case Qt::Key_BraceRight:
            stepMorph(k.key == Qt::Key_BraceRight ? 1 : -1, ctx);
            return true;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            if (shift)
                stepMorph(k.key == Qt::Key_BracketRight ? 1 : -1, ctx);
            else
                stepRoll(k.key == Qt::Key_BracketRight ? 1 : -1, ctx);
            return true;
        // Tone: - and +, = as well. Shift is ignored, since + is Shift+= on
        // a US keyboard (and Shift+- may come along with it as _).
        case Qt::Key_Minus:
        case Qt::Key_Underscore:
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            stepTone(k.key == Qt::Key_Plus || k.key == Qt::Key_Equal ? 1 : -1, ctx);
            return true;
        case Qt::Key_Return:
            // Return latches the shapers held, as a click does; with none
            // held it is LOOP, as it always was.
            if (shift)
                toggleOverdub(ctx);
            else if (!latchHeld(ctx))
                loop(ctx);
            return true;
        case Qt::Key_Enter:
        case Qt::Key_F9:
            if (shift)
                toggleOverdub(ctx);
            else
                loop(ctx);
            return true;
        case Qt::Key_F10:
            toggleOverdub(ctx);
            return true;
        // The key left of 1: § on a Nordic keyboard, ` on a US one.
        case Qt::Key_section:
        case Qt::Key_QuoteLeft:
        case Qt::Key_Backslash:
            toggleFollow(ctx);
            return true;
        case Qt::Key_Delete:
            freeShapers(ctx);
            return true;
        case Qt::Key_Q: case Qt::Key_W: case Qt::Key_E: case Qt::Key_R: case Qt::Key_T:
        case Qt::Key_Y: case Qt::Key_U: case Qt::Key_I: case Qt::Key_O: case Qt::Key_P: {
            static const int order[kLetteredSounds] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
                                                       Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P};
            // Shift with a sound's letter: that sound, its saw turned square
            // (or back), the same keys on every layout.
            for (int s = 0; s < kLetteredSounds; ++s)
                if (order[s] == k.key) {
                    if (s != m_sound || !shift)
                        setSound(s, ctx);
                    if (shift)
                        toggleSquare(ctx);
                }
            return true;
        }
        default:
            return false;
        }
    }

    // --- what the keys and the band under the pad do ---------------------

    void setSound(int s, ControlContext &ctx) {
        m_sound = s;
        ctx.notice(QStringLiteral("%1: %2").arg(QString::fromUtf8(recipe(m_sound).name),
                                               QString::fromUtf8(recipe(m_sound).what)));
    }

    void stepOctave(int dir, ControlContext &ctx) {
        m_octave = std::clamp(m_octave + dir, 0, 2);
        ctx.notice(QStringLiteral("Bass octave %1").arg(octaveText()));
    }
    QString octaveText() const {
        return m_octave == 0 ? QStringLiteral("low") : QStringLiteral("+%1").arg(m_octave);
    }

    // The roll from the keys: off, then each pattern in turn, latched so
    // it strikes whatever is held (and the loop) until Delete lets it go.
    int rollIndex() const {
        return m_latched[ShapeRoll] && m_sent[ShapeRoll][0] > 0 ? int(m_sent[ShapeRoll][1]) : -1;
    }
    void stepRoll(int dir, ControlContext &ctx) {
        const int next = std::clamp(rollIndex() + dir, -1, kPatternCount - 1);
        if (next < 0) {
            if (m_latched[ShapeRoll]) {
                m_latched[ShapeRoll] = false;
                sendShaper(ShapeRoll, 0, restB(ShapeRoll), ctx.now(), ctx, true);
            }
            ctx.notice(QStringLiteral("Roll off: ] rolls again"));
            return;
        }
        m_latched[ShapeRoll] = true;
        m_pattern = next;
        sendShaper(ShapeRoll, m_gate, float(next), ctx.now(), ctx, true);
        ctx.notice(QStringLiteral("Roll %1, latched: Delete lets go").arg(QString::fromUtf8(kPatterns[next].name)));
    }

    void stepTone(int dir, ControlContext &ctx) {
        m_keyTone = clampf(m_keyTone + 0.1f * float(dir), 0, 1);
        ctx.notice(QStringLiteral("Keyboard tone %1%").arg(int(std::lround(m_keyTone * 100))));
    }

    void stepSlide(int dir, ControlContext &ctx) {
        m_glide = clampf(m_glide + 0.125f * float(dir), 0, 1);
        ctx.notice(QStringLiteral("Slide %1 ms").arg(int(std::lround(1000 * glideSeconds()))));
    }

    void stepMorph(int dir, ControlContext &ctx) {
        m_morph[m_sound] = clampf(m_morph[m_sound] + 0.1f * float(dir), 0, 1);
        ctx.notice(QStringLiteral("%1 %2 %3%").arg(QString::fromUtf8(recipe(m_sound).name), morphName(m_sound).toLower())
                       .arg(int(std::lround(m_morph[m_sound] * 100))));
    }

    // Whether a sound has the switch Shift and its letter flips: a saw to
    // turn square, or a string (or a marimbula's tongue) to mute.
    static bool hasSwitch(int sound) {
        return recipe(sound).saw > 0 || recipe(sound).string > 0 || recipe(sound).marimbula > 0;
    }

    void toggleSquare(ControlContext &ctx) {
        // On a string the same switch is the palm: muted, every note a
        // dead thud.
        const bool string = recipe(m_sound).string > 0 || recipe(m_sound).marimbula > 0;
        if (!hasSwitch(m_sound)) {
            ctx.notice(QStringLiteral("%1 has no saw to turn square").arg(QString::fromUtf8(recipe(m_sound).name)));
            return;
        }
        m_square[m_sound] = !m_square[m_sound];
        ctx.notice(QStringLiteral("%1: %2").arg(QString::fromUtf8(recipe(m_sound).name),
                                           string ? (m_square[m_sound] ? QStringLiteral("muted") : QStringLiteral("open"))
                                                  : (m_square[m_sound] ? QStringLiteral("square") : QStringLiteral("saw"))));
    }

    void toggleFollow(ControlContext &ctx) {
        m_follow = !m_follow;
        ctx.notice(m_follow ? QStringLiteral("Bass follows the chords: columns count from the chord's root")
                            : QStringLiteral("Bass plays the key"));
    }

    void toggleOverdub(ControlContext &ctx) {
        const bool on = !ctx.studio().parts[ctx.part()].overdub;
        ctx.overdub(on);
        ctx.notice(on ? QStringLiteral("Overdub: play over the loop") : QStringLiteral("Overdub off"));
    }

    void freeShapers(ControlContext &ctx) {
        m_latchNext = false;
        for (int s = 0; s < ShaperCount; ++s)
            if (m_latched[s]) {
                m_latched[s] = false;
                sendShaper(Shaper(s), 0, restB(Shaper(s)), ctx.now(), ctx, true);
            }
        ctx.notice(QStringLiteral("Shapers released"));
    }

    // Latches the shapers the hand holds, as a click does. False if none.
    bool latchHeld(ControlContext &ctx) {
        QStringList names;
        for (const auto &h : m_fingers)
            if (h.id >= 0 && h.role != NoteRole && h.role != NoRole && h.role != LoopRole) {
                m_latched[shaperOf(h.role)] = true;
                remember(h);
                names << QString::fromUtf8(roleName(h.role));
            }
        if (names.isEmpty())
            return false;
        names.removeDuplicates();
        ctx.notice(QStringLiteral("%1 latched: Delete lets go").arg(names.join(QStringLiteral(" + "))));
        return true;
    }

    // The mouse on the band under the pad. The mouse is one finger, so it
    // cannot hold a shaper and click: LATCH with nothing held arms instead,
    // and the next shaper let go stays.
    enum Spot {
        SpotSound = 100,
        SpotOctDown = 200, SpotOctUp, SpotRollDown, SpotRollUp, SpotToneDown, SpotToneUp,
        SpotSlideDown, SpotSlideUp, SpotMorphDown, SpotMorphUp,
        SpotSquare, SpotFollow, SpotLatch, SpotOverdub, SpotFree
    };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
            return true;
        if (id >= SpotSound && id < SpotSound + int(SoundCount)) {
            setSound(id - SpotSound, ctx);
            return true;
        }
        switch (id) {
        case SpotOctDown: stepOctave(-1, ctx); return true;
        case SpotOctUp: stepOctave(1, ctx); return true;
        case SpotRollDown: stepRoll(-1, ctx); return true;
        case SpotRollUp: stepRoll(1, ctx); return true;
        case SpotToneDown: stepTone(-1, ctx); return true;
        case SpotToneUp: stepTone(1, ctx); return true;
        case SpotSlideDown: stepSlide(-1, ctx); return true;
        case SpotSlideUp: stepSlide(1, ctx); return true;
        case SpotMorphDown: stepMorph(-1, ctx); return true;
        case SpotMorphUp: stepMorph(1, ctx); return true;
        case SpotSquare: toggleSquare(ctx); return true;
        case SpotFollow: toggleFollow(ctx); return true;
        case SpotOverdub: toggleOverdub(ctx); return true;
        case SpotFree: freeShapers(ctx); return true;
        case SpotLatch:
            if (!latchHeld(ctx)) {
                m_latchNext = !m_latchNext;
                ctx.notice(m_latchNext ? QStringLiteral("Latch: the next shaper you let go stays")
                                       : QStringLiteral("Latch off"));
            }
            return true;
        default: return false;
        }
    }

    // --- the screen ---------------------------------------------------------

    void paint(QPainter &p, const PaintContext &pc) override;

    // The shapers moved over a playing loop are playing: Space keeps them
    // in it, a filter swept or grit turned up as the hand did it.
    bool layersKeepKnobMoves() const override { return true; }

    PlayArea playArea() const override {
        return {0.0f, 0.0f, 1.0f, kField, QStringLiteral("octave, roll, wobble, filter, grit")};
    }

    // The sound browser: the bass sounds, and a bar of a bassline to hear one.
    std::vector<SoundChoice> soundChoices() const override {
        std::vector<SoundChoice> out;
        for (int s = 0; s < SoundCount; ++s)
            out.push_back({QString::fromUtf8(recipe(s).name), QString::fromUtf8(recipe(s).what)});
        return out;
    }
    int soundChoice() const override { return m_sound; }
    void chooseSound(int index, ControlContext &ctx) override {
        if (index >= 0 && index < SoundCount && index != m_sound)
            setSound(index, ctx);
    }
    std::vector<AuditionStep> audition() const override {
        std::vector<AuditionStep> out;
        auditionNote(out, 0.0, Qt::Key_Z, 0.4);
        auditionNote(out, 0.75, Qt::Key_Z, 0.2);
        auditionNote(out, 1.5, Qt::Key_C, 0.4);
        auditionNote(out, 2.0, Qt::Key_Z, 0.35);
        auditionNote(out, 2.5, Qt::Key_V, 0.35);
        auditionNote(out, 3.0, Qt::Key_C, 0.35);
        auditionNote(out, 3.5, Qt::Key_X, 0.4);
        return out;
    }

    QString help() const override {
        return QStringLiteral(
            "← →\tthe sound: sub, pluck, reese, acid, wobble, growl, 808, bounce, moog, upright (or Q W E R T Y U I O P), "
            "then past P, → once to six times (their chips say P→1 … P→6): log (amapiano log drum), "
            "tuba (Balkan brass), chip (NES bass), tabla (the bayan), didge (didgeridoo), "
            "marimbula (the Caribbean box lamellophone)\n"
            "↑ ↓\tthe pad's octave\n"
            "Shift+← → (or [ ])\tthe roll: off, then OFFBEAT … 8THS, latched on whatever plays\n"
            "- + (or =)\tdarker, brighter: the tone of notes played on the keys\n"
            "Shift+Return\twith shapers down, latch them (as a click does); with none, LOOP\n"
            "F10\toverdub on or off\n"
            "§ (` on US, or \\)\tfollow the chords, on or off\n"
            "Delete\tlet the latched shapers go\n"
            "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot · clear it · stop the loop\n"
            "under the pad, with the mouse\tthe clips (click: launch or stop), the sounds, and every setting "
            "the keys reach; LATCH with nothing held: the next shaper you let go stays\n"
            "play, then Space\tloops what you just played (Space again calls a waiting loop off)\n"
            "touch the lower field\ta note in the key: higher is brighter (clean sub to biting), harder accents\n"
            "slide sideways, or land another note\tglide to it without a new attack; lift to slide back "
            "(UPRIGHT: another note is plucked, and lifting pulls off to the one held)\n"
            "twist / flatten the finger\tmorph the sound (ACID squelch, MOOG contour, UPRIGHT ring, LOG honk, "
            "TUBA blare, CHIP arpeggio, TABLA swoop, DIDGE voice, MARIMBULA spot) / fatten it\n"
            "OCT: +12 row / BEND row\tup an octave while held (two fingers, two): tap to pump; any finger there bends with its height\n"
            "ROLL\tland on a pattern: the held note strikes it on the bar; slide along the row, up/down the gate\n"
            "WOB\tland on a rate (straight on top, triplets below): a wobble on the tempo; up/down the depth\n"
            "GRIT\tdrive; up/down how much, sideways warm, tube, fold\n"
            "FILTER\ta fader for whatever sounds, the loop too: middle leaves it, up opens, down darkens; sideways morph (ACID: squelch)\n"
            "click\twith a note down, strike it again accented; with only shapers down, latch them\n"
            "LOOP (rail, far left), or F9\talso: record the coming bars from the next bar line; tap again to close\n"
            "Z X C … /   A S D … '   Shift\tthe scale from the keyboard, and an octave up; Shift accents\n"
            "Shift+↑ ↓   Shift+[ ]   Shift+Q … P\tslide time   morph   that sound with a square for its saw (UPRIGHT: palm-muted)\n"
            "UPRIGHT pressed lightly\ta ghost note: mostly the finger\n"
            "LOG, TABLA\tstruck, an octave over the pad: every finger strikes its note, sliding in from the one "
            "held (a wider slide takes longer); lift it and the held note slides back and is struck again, softly\n"
            "LOG\tthe twist pushes its nasal bwomp (the honk that carries on a phone speaker) and lets it ring longer\n"
            "TUBA played hard\tbrass flares: blown harder its wave steepens toward a shock front, and past "
            "mezzo-forte the attack blats brighter, the lips catch, the twist "
            "scoops it up to two semitones; slurred notes flare from their own breath, and pressing a held note "
            "keeps it brassy\n"
            "CHIP\tmoves once a frame, as the NES did: the pulse's duty steps 12.5 %, 25 %, 50 %; up to 40 % the "
            "twist is a tracker's macros, so a held note keeps moving (the pulse swells, its duty flips, a "
            "vibrato eases in; none at 0 %); past 40 %, the chord's arpeggio over the held root (faster, then "
            "the octave on top; an octave up under A1); an "
            "accent is the kick blip, triangle sweep and noise burst\n"
            "TABLA\tan octave up, where the bayan speaks; tapped lightly (under about a third of full pressure, "
            "fully closed at a fifth) it is the closed ke, a dead thud with a crack; firmer, the open ghe; "
            "pressed harder after the strike, the wrist bends it up, up to four semitones\n"
            "DIDGE\theight is the mouth (800 Hz to 2 kHz); pressing harder blows harder and opens it more, so "
            "pressing in and easing off pumps the breath by hand; twist sings a voice into it "
            "that growls; an accent toots a tenth up; ROLL pumps the breath on its steps\n"
            "MARIMBULA\ta steel tongue plucked on a plywood box, the box's air tuned to the key: every finger "
            "plucks its own tongue and a tongue rings while its finger is down; lift it and the finger "
            "stills it, press into it to still it sooner (MUTE: every note stilled at once); harder slaps it "
            "(a hard press or an accent: the finger lands at speed, the tip may clack on the box); height is "
            "the finger's soft pad (low) or its nail (high), a flat finger the pad; twist plucks further in "
            "from the tip, thinner; the click knocks the box with the other hand, the palm low on the pad, "
            "the knuckles high\n"
            "the sound chips\tthe row shows those around the one playing; the faded ends have more beyond, click to move along");
    }

    // For the tutorial: whether the bass follows the chords, and its sound.
    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("follow"))
            return m_follow ? 1.0f : 0.0f;
        if (name == QLatin1String("sound"))
            return float(m_sound);
        return PartControl::tutorialProbe(name);
    }

    QString status() const override {
        QStringList parts;
        const QString take = loopState(m_beat);
        if (!take.isEmpty())
            parts << take.toUpper();
        else
            parts << QStringLiteral("Space loops what you just played");
        parts << QString::fromUtf8(recipe(m_sound).name);
        for (const auto &h : m_fingers) {
            if (h.id < 0)
                continue;
            switch (h.role) {
            case NoteRole:
                parts << QStringLiteral("%1 tone %2%").arg(noteName(h.degree, h.octave)).arg(int(std::lround(h.tone * 100)));
                break;
            case OctRole:
                parts << QStringLiteral("octave up");
                break;
            case RollRole:
                parts << QStringLiteral("roll %1").arg(QString::fromUtf8(kPatterns[h.index].name).toLower());
                break;
            case WobRole:
                parts << QStringLiteral("wobble %1").arg(QString::fromUtf8(kRateNames[h.index]));
                break;
            case GritRole:
                parts << QStringLiteral("grit %1%").arg(int(std::lround(h.amount * 100)));
                break;
            case FiltRole:
                parts << QStringLiteral("filter %1%2").arg(h.amount >= 0 ? "+" : "").arg(int(std::lround(h.amount * 100)));
                break;
            default:
                break;
            }
        }
        for (int s = 0; s < ShaperCount; ++s)
            if (m_latched[s])
                parts << QStringLiteral("%1 latched").arg(QString::fromUtf8(roleName(roleOf(Shaper(s)))).toLower());
        if (m_follow)
            parts << QStringLiteral("follows the chords");
        return parts.join(QStringLiteral(" · "));
    }

private:
    struct Finger {
        int id = -1;
        Role role = NoRole;
        double t0 = 0, lastSend = 0;
        float x0 = 0, y0 = 0, angle0 = 0;
        int col = 0, degree = 0, octave = 0, sound = 0;
        float tone = 0, pressure = 0, morph = 0, fat = 0; // as last sent
        float amount = 0, second = 0;                     // a shaper's two axes
        int index = 0;                                    // a shaper's pattern or rate
        int row = 0;                                      // the row of stops it landed on (col: the stop)
        float sentA = 0, sentB = 0;                       // what this shaper last asked for
        double lastMove = 0;                              // a bend's last real movement
        bool settled = false;                             // a bend at rest, landed in the key
    };
    struct KeyNote {
        int key = 0, id = 0;
    };
    static constexpr int kNoKey = -1000;

    Finger *find(int id) {
        for (auto &h : m_fingers)
            if (h.id == id)
                return &h;
        return nullptr;
    }

    float glideSeconds() const { return recipe(m_sound).glide * std::exp2((m_glide - 0.5f) * 4.0f); }

    // A note's name and octave as it sounds, with the key (and chord) of
    // the last screen frame, and the register the sound carries (the bayan
    // sounds an octave over the pad).
    QString noteName(int degree, int octave) const {
        Tuning t;
        t.set(m_root, m_family, m_brightness, false);
        Harmony h;
        h.tuning = t;
        h.chordRoot = m_chordRoot;
        const int shift = m_follow ? chordShift(h) : 0;
        const long m = std::lround(h.midi(degree + shift, octave) + rootShift(t) + recipe(m_sound).octave);
        return QString::fromUtf8(Tuning::noteName(int(((m % 12) + 12) % 12))) + QString::number(m / 12 - 1);
    }

    void land(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        h = Finger{};
        h.id = t.id;
        h.t0 = time;
        h.x0 = t.x;
        h.y0 = t.y;
        h.angle0 = t.angle;
        if (t.y >= kField && t.x < kLoopW) {
            h.role = LoopRole;
            loop(ctx);
            return;
        }
        if (t.y >= kField) {
            int z = 0;
            while (z < kZones - 1 && t.x >= kZoneEdge[z + 1])
                ++z;
            h.role = kZoneRole[z];
            const Shaper s = shaperOf(h.role);
            // A finger on a latched zone takes it over, and lets it go on lift.
            m_latched[s] = false;
            // Which stop it landed on: the cell under it, four across, two rows.
            h.row = t.y >= kRailMid ? 0 : 1;
            h.col = std::clamp(int((t.x - zoneLeft(h.role)) / (zoneWidth(h.role) / 4.0f)), 0, 3);
            switch (h.role) {
            case RollRole:
                h.amount = m_gate;
                break;
            case WobRole:
                h.amount = m_depth;
                break;
            case GritRole:
                h.amount = m_grit;
                h.second = m_gritChar;
                break;
            default:
                break;
            }
            shape(h, t, time, ctx, true);
            return;
        }
        h.role = NoteRole;
        h.col = std::clamp(int(t.x * float(kColumns)), 0, kColumns - 1);
        h.degree = kLowDegree + h.col;
        h.octave = m_octave;
        h.sound = m_sound;
        h.tone = clampf(t.y / kField, 0, 1);
        h.pressure = t.pressure;
        h.morph = m_morph[m_sound];
        h.fat = fatOf(t);
        h.lastSend = time;
        Command on;
        on.type = Command::On;
        on.id = t.id;
        on.t = time;
        on.i[0] = h.degree;
        on.i[1] = h.octave;
        on.i[2] = (m_follow ? Follow : 0) | (t.pressure > 0.82f ? Accent : 0) | (m_square[m_sound] ? Square : 0);
        on.i[3] = h.sound;
        on.f[0] = velocityOf(t.pressure);
        on.f[1] = h.tone;
        on.f[2] = h.morph;
        on.f[3] = h.fat;
        on.f[4] = m_glide;
        on.f[5] = float(m_rate);
        ctx.send(on);
    }

    void drag(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        if (h.role == LoopRole)
            return;
        if (h.role != NoteRole) {
            shape(h, t, time, ctx, false);
            return;
        }
        // Sideways glides to the next column once the finger is well into it.
        const float fx = t.x * float(kColumns);
        int col = h.col;
        if (fx < float(h.col) - 0.2f || fx > float(h.col) + 1.2f)
            col = std::clamp(int(fx), 0, kColumns - 1);
        const float tone = clampf(t.y / kField, 0, 1);
        float morph = m_morph[h.sound];
        if (t.eccentricity > 0.25f)
            morph = clampf(morph + twistOf(t.angle, h.angle0) / (kPi / 3.0f) * 0.6f, 0, 1);
        const float fat = fatOf(t);
        const bool slide = col != h.col;
        const bool changed = std::fabs(tone - h.tone) > 0.006f || std::fabs(t.pressure - h.pressure) > 0.02f ||
                             std::fabs(morph - h.morph) > 0.01f || std::fabs(fat - h.fat) > 0.02f;
        if (slide || (changed && time - h.lastSend >= 1.0 / 60.0)) {
            h.col = col;
            h.degree = kLowDegree + col;
            h.tone = tone;
            h.pressure = t.pressure;
            h.morph = morph;
            h.fat = fat;
            h.lastSend = time;
            Command mv;
            mv.type = Command::Move;
            mv.id = h.id;
            mv.t = time;
            // A slide to another column is a new note of the line, played
            // with the same finger: Space hears it as one, and a loop pulls
            // it to the grid like one.
            if (slide)
                mv.flags = Command::Quantize;
            mv.i[0] = h.degree;
            mv.i[1] = h.octave;
            mv.f[0] = clampf(0.3f + 0.8f * t.pressure, 0, 1);
            mv.f[1] = tone;
            mv.f[2] = morph;
            mv.f[3] = fat;
            ctx.send(mv);
        }
    }

    void lift(Finger &h, double time, ControlContext &ctx) {
        const int id = h.id;
        h.id = -1;
        if (h.role == NoteRole) {
            off(id, time, ctx);
        } else if (h.role != NoRole && h.role != LoopRole) {
            remember(h);
            const Shaper s = shaperOf(h.role);
            // LATCH armed from the band: this shaper stays where it was let go.
            if (m_latchNext) {
                m_latchNext = false;
                m_latched[s] = true;
                ctx.notice(QStringLiteral("%1 latched: Delete lets go").arg(QString::fromUtf8(roleName(h.role))));
            }
            // Another finger still in the same zone takes over where it is.
            Finger *other = nullptr;
            for (auto &f : m_fingers)
                if (f.id >= 0 && f.role == h.role && (!other || f.t0 > other->t0))
                    other = &f;
            if (s == ShapeOct) {
                sendShaper(s, float(octFingers()), 0, time, ctx, true);
                sendShaper(ShapeBend, other && other->row == 1 ? other->amount : 0.0f,
                           other && other->settled ? 1.0f : 0.0f, time, ctx, true);
            } else if (other)
                sendShaper(s, other->sentA, other->sentB, time, ctx, true);
            else if (!m_latched[s])
                sendShaper(s, 0, restB(s), time, ctx, true);
        }
    }

    // A shaper finger: where it is now, against where it landed.
    void shape(Finger &h, const Touch &t, double time, ControlContext &ctx, bool first) {
        const float dx = t.x - h.x0, dy = t.y - h.y0;
        const float press = t.pressure - 0.5f;
        float a = 0, b = 0;
        switch (h.role) {
        case OctRole: {
            // The top row lifts an octave and nothing else, so octave pumps
            // stay in tune however the finger drifts.
            a = float(octFingers());
            sendShaper(ShapeOct, a, 0, time, ctx, first);
            if (h.row == 0)
                return;
            // The BEND row bends with height, down two octaves (a dive) or
            // up one, past a small dead zone so a resting finger is in tune.
            constexpr float dead = 0.02f;
            const float travel = std::fabs(dy) <= dead ? 0.0f : dy - std::copysign(dead, dy);
            const float bend = clampf(travel * 40.0f, -24.0f, 12.0f);
            if (first || std::fabs(bend - h.amount) > 0.05f) {
                h.amount = bend;
                h.lastMove = time;
                h.settled = false;
            }
            // With several fingers here, the newest bends.
            if (newestIn(OctRole, h))
                sendShaper(ShapeBend, h.amount, h.settled ? 1.0f : 0.0f, time, ctx, first);
            return;
        }
        case RollRole:
            h.index = kRollStops[h.row][stopAlong(h, dx)];
            h.amount = clampf(m_gate + dy * 2.5f, 0.15f, 1.0f);
            a = h.amount;
            b = float(h.index);
            break;
        case WobRole:
            h.index = kWobStops[h.row][stopAlong(h, dx)];
            h.amount = clampf(m_depth + dy * 2.5f, 0.05f, 1.0f);
            a = clampf(h.amount + 0.4f * press, 0.05f, 1.0f);
            b = float(h.index);
            break;
        case FiltRole: {
            // A fader, like a DJ filter: the zone's middle leaves the sound
            // alone, lower darkens it, higher opens it, on whatever sounds,
            // the loop included. Sideways from the middle is morph (on ACID,
            // the squelch).
            const float half = 0.5f * (1.0f - kField);
            const float mid = zoneLeft(FiltRole) + 0.5f * zoneWidth(FiltRole);
            h.amount = clampf((t.y - kRailMid) / half * 0.7f, -0.7f, 0.7f);
            h.second = clampf((t.x - mid) / (0.5f * zoneWidth(FiltRole)), -1.0f, 1.0f);
            a = h.amount;
            b = h.second;
            break;
        }
        case GritRole:
            h.amount = clampf(m_grit + dy * 2.5f, 0.02f, 1.0f);
            h.second = clampf(m_gritChar + dx * 2.5f, 0, 1);
            a = clampf(h.amount + 0.5f * press, 0.02f, 1.0f);
            b = h.second;
            break;
        default:
            return;
        }
        h.sentA = a;
        h.sentB = b;
        sendShaper(shaperOf(h.role), a, b, time, ctx, first);
    }

    // A shaper at rest: off, its second axis left where it was (a rate, a
    // pattern), except the filter's morph, which rests at zero too.
    float restB(Shaper s) const { return s == ShapeFilter ? 0.0f : m_sent[s][1]; }

    // Sliding along a row of stops: one cell per cell's width.
    static int stopAlong(const Finger &h, float dx) {
        return std::clamp(h.col + int(std::lround(dx / (zoneWidth(h.role) / 4.0f))), 0, 3);
    }
    bool newestIn(Role r, const Finger &h) const {
        for (const auto &f : m_fingers)
            if (f.id >= 0 && f.role == r && f.t0 > h.t0)
                return false;
        return true;
    }

    // Depth, gate and grit carry over to the next finger; the rate and the
    // pattern are where the finger lands.
    void remember(const Finger &h) {
        switch (h.role) {
        case RollRole:
            m_pattern = h.index;
            m_gate = h.amount;
            break;
        case WobRole:
            m_rate = h.index;
            m_depth = h.amount;
            break;
        case GritRole:
            m_grit = h.amount;
            m_gritChar = h.second;
            break;
        default:
            break;
        }
    }

    int octFingers() const {
        int n = m_latched[ShapeOct] ? int(m_sent[ShapeOct][0]) : 0;
        for (const auto &h : m_fingers)
            n += h.id >= 0 && h.role == OctRole && h.row == 0;
        return std::min(n, 2);
    }

    void sendShaper(Shaper s, float a, float b, double time, ControlContext &ctx, bool force) {
        // Only real changes, and a moving finger at most sixty times a
        // second (a big jump goes at once): clips stay lean.
        float *last = m_sent[s];
        const bool changed = std::fabs(a - last[0]) > 0.005f || b != last[1];
        if (!changed)
            return;
        if (!force && time - m_sentAt[s] < 1.0 / 60.0 && std::fabs(a - last[0]) < 0.1f && b == last[1])
            return;
        last[0] = a;
        last[1] = b;
        m_sentAt[s] = time;
        Command c;
        c.type = Command::Param;
        c.id = 1 + int(s);
        c.t = time;
        c.i[0] = s;
        c.f[0] = a;
        c.f[1] = b;
        ctx.send(c);
    }

    void click(double time, ControlContext &ctx) {
        // The latest note finger, if there is one, is struck again.
        const Finger *note = nullptr;
        bool shapers = false;
        for (const auto &h : m_fingers) {
            if (h.id < 0)
                continue;
            if (h.role == NoteRole && (!note || h.t0 > note->t0))
                note = &h;
            shapers |= h.role != NoteRole;
        }
        if (note) {
            Command c;
            c.type = kStrike;
            c.flags = Command::Quantize;
            c.id = note->id;
            c.t = time;
            ctx.send(c);
            return;
        }
        if (shapers)
            latchHeld(ctx);
    }

    // The one way a loop is made: a deliberate tap on LOOP (or Enter).
    // The first arms a take from the next bar; the second closes it at the
    // bar's end, and the loop plays from there. The rail and the status line
    // show each step, and a notice says what was caught.
    void loop(ControlContext &ctx) {
        const PartView &pv = ctx.studio().parts[ctx.part()];
        const double beat = ctx.beatAt(ctx.now());
        if (m_recSlot < 0 && pv.recording >= 0) {
            ctx.notice(QStringLiteral("A loop is already on its way: Space calls it off"));
            return;
        }
        if (m_recSlot >= 0) {
            if (m_closing || m_closeEarly)
                return;
            if (beat < m_recStart) {
                m_closeEarly = true;
                ctx.notice(QStringLiteral("A one-bar loop: the take closes after its first bar"));
            } else {
                m_closing = true;
                ctx.stopRecording();
                ctx.notice(QStringLiteral("The loop closes at the bar line"));
            }
            return;
        }
        const int slot = ctx.freeSlot();
        if (slot < 0) {
            ctx.notice(QStringLiteral("All eight bass slots hold loops: Shift+F1–F8 clears one"));
            return;
        }
        resetSlot(slot, ctx);
        ctx.record(slot, 8);
        m_recSlot = slot;
        m_recStart = std::ceil(beat / kBeatsPerBar - 1e-9) * kBeatsPerBar;
        m_closing = m_closeEarly = false;
        ctx.notice(QStringLiteral("Bass records into slot %1 from the next bar: play, then tap LOOP again").arg(slot + 1));
    }

    // Where the take is, for the rail and the status line.
    QString loopState(double beat) const {
        if (m_taking >= 0 && m_taking != m_recSlot)
            return QStringLiteral("loop into slot %1 at the bar: Space calls it off").arg(m_taking + 1);
        if (m_landedSlot >= 0 && m_recSlot < 0 && m_time - m_landedAt < 3.0)
            return QStringLiteral("looped %1 into slot %2").arg(m_landedText).arg(m_landedSlot + 1);
        if (m_recSlot < 0)
            return {};
        if (beat < m_recStart) {
            const int beats = int(std::ceil(m_recStart - beat));
            return QStringLiteral("armed: slot %1 records in %2 %3")
                .arg(m_recSlot + 1)
                .arg(beats)
                .arg(beats == 1 ? QStringLiteral("beat") : QStringLiteral("beats"));
        }
        const int bar = int((beat - m_recStart) / kBeatsPerBar) + 1;
        if (m_closing || m_closeEarly)
            return QStringLiteral("recording bar %1: closing at the bar").arg(bar);
        return QStringLiteral("recording bar %1 of 8: tap LOOP to close").arg(bar);
    }

    void resetSlot(int slot, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = ResetSlot;
        c.f[0] = float(slot);
        ctx.send(c);
    }

    void off(int id, double time, ControlContext &ctx) {
        Command c;
        c.type = Command::Off;
        c.id = id;
        c.t = time;
        ctx.send(c);
    }

    // The keyboard's two rows, as scale degrees.
    static int normalise(int key) {
        switch (key) {
        case Qt::Key_Less: return Qt::Key_Comma;
        case Qt::Key_Greater: return Qt::Key_Period;
        case Qt::Key_Colon: return Qt::Key_Semicolon;
        case Qt::Key_QuoteDbl: return Qt::Key_Apostrophe;
        // A Nordic keyboard has Ö and Ä where a US one has ; and '.
        case Qt::Key_Odiaeresis: return Qt::Key_Semicolon;
        case Qt::Key_Adiaeresis: return Qt::Key_Apostrophe;
        default: return key;
        }
    }
    static int keyDegree(int key) {
        static const int low[] = {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B,
                                  Qt::Key_N, Qt::Key_M, Qt::Key_Comma, Qt::Key_Period, Qt::Key_Slash};
        static const int high[] = {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H,
                                   Qt::Key_J, Qt::Key_K, Qt::Key_L, Qt::Key_Semicolon, Qt::Key_Apostrophe};
        for (int i = 0; i < 10; ++i)
            if (low[i] == key)
                return i;
        for (int i = 0; i < 11; ++i)
            if (high[i] == key)
                return 100 + i; // the row an octave up
        return kNoKey;
    }

    void keyOn(int code, int degree, bool accent, ControlContext &ctx) {
        KeyNote *slot = nullptr;
        for (auto &k : m_keys)
            if (k.key == code)
                return;
        for (auto &k : m_keys)
            if (!k.key) {
                slot = &k;
                break;
            }
        if (!slot)
            return;
        const double now = ctx.now();
        int octave = m_octave;
        if (degree >= 100) {
            degree -= 100;
            octave += 1;
        }
        *slot = {code, kKeyIdBase + code};
        Command on;
        on.type = Command::On;
        on.id = slot->id;
        on.t = now;
        on.i[0] = degree;
        on.i[1] = octave;
        on.i[2] = (m_follow ? Follow : 0) | (accent ? Accent : 0) | (m_square[m_sound] ? Square : 0);
        on.i[3] = m_sound;
        on.f[0] = accent ? 1.0f : 0.75f;
        on.f[1] = m_keyTone;
        on.f[2] = m_morph[m_sound];
        on.f[3] = 0;
        on.f[4] = m_glide;
        on.f[5] = float(m_rate);
        ctx.send(on);
    }

    void keyOff(int code, ControlContext &ctx) {
        for (auto &k : m_keys)
            if (k.key == code) {
                off(k.id, ctx.now(), ctx);
                k.key = 0;
            }
    }

    static float velocityOf(float pressure) { return clampf(0.35f + 0.8f * pressure, 0.2f, 1.0f); }
    static float fatOf(const Touch &t) { return clampf((t.size - 1.15f) * 1.4f, 0, 1); }

    void paintField(QPainter &p, const PaintContext &pc, const Tuning &tuning);
    void paintRail(QPainter &p, const PaintContext &pc);
    void paintScope(QPainter &p, const PaintContext &pc);
    void paintBand(QPainter &p, const PaintContext &pc);

    Finger m_fingers[16];
    KeyNote m_keys[16];
    // The take LOOP armed: its slot (−1: none), the beat it starts, and
    // whether it is closing.
    int m_recSlot = -1;
    int m_taking = -1;              // the slot any take is filling, as the studio shows it
    uint32_t m_takesSeen = 0;       // the studio's count of landed takes, as last seen
    bool m_takesKnown = false;
    int m_landedSlot = -1;          // the last take that landed, when, and how long
    double m_landedAt = -10, m_time = 0;
    QString m_landedText;
    bool m_slotFull[kClipSlots] = {};
    double m_recStart = 0;
    bool m_closing = false, m_closeEarly = false;
    double m_beat = 0; // as of the last paint, for status()
    Frame m_frame;
    bool m_button = false;

    int m_sound = Pluck;
    float m_morph[SoundCount];
    bool m_square[SoundCount] = {}; // the saw swapped for a square (Shift and the sound's letter)
    float m_glide = 0.5f, m_keyTone = 0.6f;
    int m_octave = 0;
    bool m_follow = false;

    // Where each shaper starts next time.
    int m_pattern = 3, m_rate = 3;
    float m_gate = 0.6f, m_depth = 0.8f, m_grit = 0.5f, m_gritChar = 0.25f;
    bool m_latched[ShaperCount] = {};
    bool m_latchNext = false; // LATCH clicked with nothing held: the next shaper let go stays
    console::Hotspots m_spots; // what paint() drew to be clicked
    float m_sent[ShaperCount][2] = {};
    double m_sentAt[ShaperCount] = {};

    // The key and chord, as of the last paint, for naming notes in status().
    int m_root = 9, m_family = 0, m_brightness = 4, m_chordRoot = 0;
};

// --- painting -------------------------------------------------------------------

namespace c = console;

void Control::paint(QPainter &p, const PaintContext &pc) {
    const StudioView &sv = *pc.studio;
    m_root = sv.root;
    m_family = sv.family;
    m_brightness = sv.brightness;
    m_chordRoot = sv.chordRoot;
    m_beat = pc.beat;
    m_time = pc.time;
    Tuning tuning;
    tuning.set(sv.root, sv.family, sv.brightness, sv.just);

    c::pad(p, pc, QStringLiteral("Bass"));
    c::variation(p, pc, QString::fromUtf8(recipe(m_sound).name));
    paintField(p, pc, tuning);
    paintScope(p, pc);
    paintRail(p, pc);

    // Held notes chain in the order they landed: each slid to the next.
    {
        const Finger *chain[16];
        int n = 0;
        for (const auto &f : m_fingers)
            if (f.id >= 0 && f.role == NoteRole)
                chain[n++] = &f;
        for (int a = 1; a < n; ++a)
            for (int b = a; b > 0 && chain[b]->t0 < chain[b - 1]->t0; --b)
                std::swap(chain[b], chain[b - 1]);
        auto where = [&](const Finger *f) {
            for (int i = 0; i < m_frame.count; ++i)
                if (m_frame.touches[i].id == f->id)
                    return pc.at(m_frame.touches[i].x, m_frame.touches[i].y);
            return QPointF();
        };
        for (int k = 1; k < n; ++k) {
            if (chain[k]->degree == chain[k - 1]->degree && chain[k]->octave == chain[k - 1]->octave)
                continue;
            const QPointF a = where(chain[k - 1]), b = where(chain[k]);
            const bool newest = k == n - 1;
            p.setPen(QPen(c::alpha(pc.color, newest ? 0.8f : 0.4f), 2, Qt::DashLine));
            p.drawLine(a, b);
            if (newest)
                c::tag(p, (a + b) / 2, QStringLiteral("SLIDE"), c::alpha(pc.color, 0.85f), 10, true);
        }
    }

    // Fingers, each saying what it does.
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        const Finger *h = nullptr;
        for (const auto &f : m_fingers)
            if (f.id == t.id)
                h = &f;
        QString label;
        QColor col = pc.color;
        if (h && h->role == NoteRole) {
            label = noteName(h->degree, h->octave);
        } else if (h) {
            // A shaper's state is drawn in its zone; the finger just glows.
            col = c::amber();
        }
        c::finger(p, pc, t, col, label, h && h->role == NoteRole ? 0.6f : 0.3f);
    }
    c::beats(p, pc, pc.color);
    paintBand(p, pc);
}

void Control::paintField(QPainter &p, const PaintContext &pc, const Tuning &tuning) {
    const StudioView &sv = *pc.studio;
    const float *d = pc.view->data;
    const int n = tuning.degreesPerOctave();
    Harmony harmony;
    harmony.tuning = tuning;
    harmony.chordRoot = sv.chordRoot;
    const int shift = m_follow ? chordShift(harmony) : 0;
    // The chord's tones as degrees above its root, for lighting them.
    bool chordTone[16] = {};
    if (m_follow)
        for (int k = 0; k < std::clamp(sv.chordCount, 1, 4); ++k)
            chordTone[(((sv.chordTones[k] - sv.chordRoot) % n) + n) % n] = true;

    const float colW = 1.0f / float(kColumns);
    for (int k = 0; k < kColumns; ++k) {
        const int deg = kLowDegree + k;
        const int rel = ((deg % n) + n) % n;
        const QRectF col = pc.rect(colW * float(k), 0, colW * float(k + 1), kField);
        const bool root = rel == 0;
        const bool lit = m_follow ? chordTone[rel] : root;
        if (lit)
            p.fillRect(col, c::alpha(pc.color, root ? 0.10f : 0.06f));
        if (k < -kLowDegree)
            p.fillRect(col, QColor(0, 0, 0, 90)); // under the root: there, but quieter
        const double midi = tuning.baseMidi() + tuning.degreeSemis(deg + shift) + 12.0 * m_octave +
                            rootShift(tuning) + recipe(m_sound).octave;
        const long m = std::lround(midi);
        const int pc12 = int(((m % 12) + 12) % 12);
        const QString name = QString::fromUtf8(Tuning::noteName(pc12)) + QString::number(m / 12 - 1);
        c::text(p, QRectF(col.left(), col.bottom() - 22, col.width(), 18), name,
                c::alpha(pc.color, lit ? 0.95f : 0.55f), 11, lit, Qt::AlignCenter);
        if (m_follow) {
            static const char *interval[] = {"R", "2", "3", "4", "5", "6", "7", "8"};
            const QString iv = n == 7 ? QString::fromUtf8(interval[rel]) : QString::number(rel + 1);
            c::text(p, QRectF(col.left(), col.bottom() - 40, col.width(), 16), iv,
                    c::alpha(pc.color, chordTone[rel] ? 0.9f : 0.35f), 10, true, Qt::AlignCenter);
        }
    }
    // The tone rises with the finger: the field brightens toward the rail.
    {
        QLinearGradient g(pc.at(0, 0), pc.at(0, kField));
        g.setColorAt(0, c::alpha(pc.color, 0.0f));
        g.setColorAt(1, c::alpha(pc.color, 0.07f));
        p.fillRect(pc.rect(0, 0, 1, kField), g);
    }
    c::grid(p, pc, kColumns, 1, pc.color, 0.12f);
    // Tone guides: the cutoff rises with the finger.
    p.setPen(QPen(c::alpha(pc.color, 0.08f), 1, Qt::DashLine));
    for (int q = 1; q < 4; ++q) {
        const qreal y = pc.at(0, kField * float(q) / 4.0f).y();
        p.drawLine(QPointF(pc.pad.left(), y), QPointF(pc.pad.right(), y));
    }
    p.setPen(QPen(c::alpha(pc.color, 0.5f), 1.5));
    p.drawLine(pc.at(0, kField), pc.at(1, kField));

    // What's sounding: a column lit to its level, the filter as a bar
    // across it (the wobble moves it), the loop's line dimmer.
    for (int k = kClipLayers; k >= 0; --k) {
        // 0 live, 1 the loop, 2… the loop's other layers (drawn as the loop).
        const int lane = std::min(k, 1);
        const float *ln = d + (k < 2 ? PubLane + 8 * k : PubLayerLanes + 8 * (k - 2));
        if (ln[PubOn] <= 0)
            continue;
        const int col = int(ln[PubDegree]) - n * m_octave - kLowDegree;
        if (col < 0 || col >= kColumns)
            continue;
        const float a = lane == 0 ? 1.0f : 0.55f * (1.0f - d[PubClipMute]);
        const QRectF r = pc.rect(colW * float(col), 0, colW * float(col + 1), kField);
        p.fillRect(r, c::alpha(pc.color, a * std::min(0.45f, 0.08f + 0.5f * ln[PubEnv])));
        const qreal y = pc.at(0, kField * ln[PubCut]).y();
        p.fillRect(QRectF(r.left() + 3, y - 2, r.width() - 6, 4), c::alpha(pc.color, a * 0.9f));
        if (lane == 1)
            // At the column's foot, over the note names: the top of the
            // field carries the sound's name and description.
            c::text(p, QRectF(r.left(), r.bottom() - 34, r.width(), 14), QStringLiteral("LOOP"),
                    c::alpha(pc.color, a), 10, true, Qt::AlignCenter);
    }

    // The sound, big, at the top left of the field.
    const Recipe &R = recipe(m_sound);
    const QPointF at = pc.at(0.012f, kField - 0.045f);
    const QRectF tagR = c::tag(p, at, QString::fromUtf8(R.name), pc.color, 16);
    if (pc.level == 0) {
        // A newcomer's line: what the sound does under a finger, without
        // the numbers or the keys (a part naming a key, "\ saw or
        // square", is left to Shape).
        QStringList what;
        for (const QString &part : QString::fromUtf8(R.what).split(QStringLiteral(", ")))
            if (!part.contains(QLatin1Char('\\')))
                what << part;
        c::text(p, QRectF(tagR.right() + 10, tagR.top(), pc.pad.width() * 0.6, tagR.height()), what.join(QStringLiteral(", ")),
                c::alpha(pc.color, 0.75f), 12);
    } else
    c::text(p, QRectF(tagR.right() + 10, tagR.top(), pc.pad.width() * 0.6, tagR.height()),
            QStringLiteral("%1 · %2 %3% · slide %4 ms")
                .arg(QString::fromUtf8(R.what), morphName(m_sound).toLower())
                .arg(int(std::lround(m_morph[m_sound] * 100)))
                .arg(int(std::lround(1000 * glideSeconds()))),
            c::alpha(pc.color, 0.75f), 11);
    if (m_follow) {
        const int pc12 = int(std::lround(tuning.baseMidi() + tuning.degreeSemis(sv.chordRoot))) % 12;
        const QPointF fr = pc.at(0.988f, kField - 0.045f);
        c::tag(p, QPointF(fr.x() - 150, fr.y()),
               QStringLiteral("FOLLOWS %1").arg(QString::fromUtf8(Tuning::noteName((pc12 + 12) % 12))), c::cyan(),
               13);
    }
}

void Control::paintScope(QPainter &p, const PaintContext &pc) {
    const float *d = pc.view->data;
    if (d[PubScopeCycles] <= 0) {
        // Nothing sounding and no hand: say how to begin.
        if (m_frame.count == 0)
            c::text(p, pc.rect(0, 0.4f * kField, 1, 0.5f * kField),
                    QStringLiteral("one finger plays · higher is brighter · the rail above bends it"),
                    c::alpha(pc.color, 0.35f), 13, false, Qt::AlignCenter);
        return;
    }
    float peak = 0;
    for (int k = 0; k < PubScopeSize; ++k)
        peak = std::max(peak, std::fabs(d[PubScope + k]));
    if (peak < 1e-4f)
        return;
    // Phase-locked, so the wave stands still and only its shape moves.
    const float gain = 0.26f * kField / std::max(0.18f, peak);
    QPainterPath path;
    for (int k = 0; k < PubScopeSize; ++k) {
        const float x = 0.03f + 0.94f * float(k) / float(PubScopeSize - 1);
        const QPointF pt = pc.at(x, 0.45f * kField + gain * d[PubScope + k]);
        if (k == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c::alpha(pc.color, 0.12f), 9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(QPen(c::alpha(pc.color, 0.75f), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
}

void Control::paintRail(QPainter &p, const PaintContext &pc) {
    const float *d = pc.view->data;
    const QColor amber = c::amber();
    const bool blink = std::fmod(pc.time, 0.5) < 0.25;

    // LOOP: the take of the moment, whoever started it (Space, this button
    // or Enter), from the studio's own view of it.
    {
        const QRectF r = pc.rect(0, kField, kLoopW, 1.0f).adjusted(6, 6, -6, -6);
        const int taking = pc.view->recording;
        QString title = QStringLiteral("LOOP"), body = QStringLiteral("SPACE");
        QColor col = pc.color;
        bool lit = false;
        if (taking < 0 && m_landedSlot >= 0 && pc.time - m_landedAt < 2.0) {
            // A take just landed: which slot, lit for a moment.
            title = QStringLiteral("SLOT %1").arg(m_landedSlot + 1);
            body = QStringLiteral("LOOPED");
            lit = true;
        } else if (taking >= 0) {
            col = c::red();
            const bool mine = taking == m_recSlot;
            if (mine && pc.beat < m_recStart) {
                title = QStringLiteral("ARMED");
                body = QStringLiteral("IN %1").arg(int(std::ceil(m_recStart - pc.beat)));
                lit = blink;
            } else if (mine) {
                title = QStringLiteral("REC %1").arg(int((pc.beat - m_recStart) / kBeatsPerBar) + 1);
                body = m_closing || m_closeEarly ? QStringLiteral("AT BAR") : QStringLiteral("TAP: CLOSE");
                lit = true;
            } else {
                title = QStringLiteral("SLOT %1").arg(taking + 1);
                body = QStringLiteral("AT BAR");
                lit = blink;
            }
        }
        c::button(p, r, title, col, lit);
        c::text(p, r.adjusted(4, 24, -4, -2), body, c::alpha(col, 0.85f), 10, true, Qt::AlignCenter);
    }

    bool held[ShaperCount] = {};
    for (const auto &h : m_fingers)
        if (h.id >= 0 && h.role >= OctRole && h.role <= FiltRole)
            held[shaperOf(h.role)] = true;
    for (int z = 0; z < kZones; ++z) {
        const Role role = kZoneRole[z];
        const Shaper s = shaperOf(role);
        const float x0 = zoneLeft(role), zw = zoneWidth(role);
        const QRectF frame = pc.rect(x0, kField, x0 + zw, 1.0f).adjusted(4, 4, -4, -4);
        const bool on = s == ShapeOct      ? d[PubOct] > 0 || held[s]
                        : s == ShapeRoll   ? d[PubRoll] > 0
                        : s == ShapeWob    ? held[s] || m_latched[s]
                        : s == ShapeFilter ? held[s] || m_latched[s] || std::fabs(d[PubFilter]) > 0.005f
                                           : d[PubGrit] > 0;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c::alpha(amber, on ? 1.0f : 0.55f), on ? 2.0 : 1.2));
        p.drawRect(frame);
        c::text(p, QRectF(frame.left() + 6, frame.top() + 1, frame.width() - 12, 14), QString::fromUtf8(roleName(role)),
                c::alpha(amber, on ? 1.0f : 0.6f), 10, true);
        if (m_latched[s])
            c::tag(p, QPointF(frame.right() - 50, frame.top() + 9), QStringLiteral("LATCH"), c::red(), 9);

        // A cell of the zone's stop grid, in pad units, as touched.
        auto cell = [&](int row, int col) {
            const float cw = zw / 4.0f;
            const float ya = row == 0 ? kRailMid : kField, yb = row == 0 ? 1.0f : kRailMid;
            return pc.rect(x0 + cw * float(col), ya, x0 + cw * float(col + 1), yb).adjusted(3, row == 0 ? 15 : 2, -3, -3);
        };
        if (s == ShapeRoll || s == ShapeWob) {
            const int current = s == ShapeRoll ? (on ? int(d[PubRoll]) - 1 : -1) : (on ? int(d[PubWobRate]) : -1);
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 4; ++col) {
                    const int stop = s == ShapeRoll ? kRollStops[row][col] : kWobStops[row][col];
                    const QRectF b = cell(row, col);
                    const bool lit = stop == current;
                    p.fillRect(b, c::alpha(amber, lit ? 0.9f : 0.08f));
                    if (s == ShapeRoll) {
                        // The pattern itself, as steps.
                        const char *steps = kPatterns[stop].steps;
                        const int n = patternSteps(stop);
                        const int now = lit ? int(d[PubRollStep]) : -1;
                        const qreal w = (b.width() - 4) / n;
                        for (int k = 0; k < n; ++k) {
                            const QRectF st(b.left() + 2 + k * w, b.bottom() - 9, w - 1.5, 6);
                            const bool x = steps[k] == 'x';
                            p.fillRect(st, lit ? QColor::fromRgb(c::kInk)
                                               : c::alpha(amber, x ? (k == now ? 1.0f : 0.55f) : 0.12f));
                            if (lit && k == now)
                                p.fillRect(st.adjusted(0, -3, 0, 0), QColor::fromRgb(c::kInk));
                        }
                        c::text(p, b.adjusted(0, 0, 0, -10), QString::fromUtf8(kPatterns[stop].name),
                                lit ? QColor::fromRgb(c::kInk) : c::alpha(amber, 0.75f), 9, true, Qt::AlignCenter);
                    } else {
                        c::text(p, b, QString::fromUtf8(kRateNames[stop]),
                                lit ? QColor::fromRgb(c::kInk) : c::alpha(amber, 0.7f), 11, true, Qt::AlignCenter);
                    }
                }
            if (s == ShapeWob && d[PubLfo] >= 0 && d[PubWobDepth] > 0) {
                // The LFO, a dot riding the zone's floor.
                const QPointF a = pc.at(x0, kField), b = pc.at(x0 + zw, kField);
                const float v = d[PubLfo];
                p.fillRect(QRectF(a.x() + 4, a.y() - 4 - 3, (b.x() - a.x() - 8) * v * d[PubWobDepth], 3), amber);
            }
        } else if (s == ShapeOct) {
            const int lit = int(std::lround(d[PubOct]));
            const QRectF up = pc.rect(x0, kRailMid, x0 + zw, 1.0f).adjusted(6, 15, -6, -3);
            const QRectF bend = pc.rect(x0, kField, x0 + zw, kRailMid).adjusted(6, 2, -6, -3);
            p.fillRect(up, c::alpha(amber, lit > 0 ? 0.9f : 0.08f));
            c::text(p, up, lit > 1 ? QStringLiteral("+24") : QStringLiteral("+12"),
                    lit > 0 ? QColor::fromRgb(c::kInk) : c::alpha(amber, 0.7f), 11, true, Qt::AlignCenter);
            const bool bending = std::fabs(m_sent[ShapeBend][0]) > 0.05f;
            p.fillRect(bend, c::alpha(amber, bending ? 0.9f : 0.08f));
            c::text(p, bend,
                    bending ? QStringLiteral("BEND %1").arg(double(m_sent[ShapeBend][0]), 0, 'f', 1)
                            : QStringLiteral("BEND ↕"),
                    bending ? QColor::fromRgb(c::kInk) : c::alpha(amber, 0.7f), 10, true, Qt::AlignCenter);
        } else if (s == ShapeFilter) {
            // Two bipolar meters: the cutoff it lifts or sinks (height), and
            // the morph it adds (sideways; the squelch on ACID).
            const QRectF body = pc.rect(x0, kField, x0 + zw, 1.0f).adjusted(10, 20, -10, -8);
            const float cut = d[PubFilter], mo = d[PubFilterMorph];
            // The fader's rest: the line across the zone's middle.
            p.setPen(QPen(c::alpha(amber, 0.35f), 1, Qt::DashLine));
            p.drawLine(pc.at(x0, kRailMid), pc.at(x0 + zw, kRailMid));
            c::text(p, QRectF(body.left(), body.top(), body.width(), 12), QStringLiteral("↕ TONE"),
                    c::alpha(amber, 0.7f), 9, true);
            c::gauge(p, QRectF(body.left(), body.top() + 13, body.width(), body.height() * 0.28),
                     clampf(0.5f + 0.5f * cut / 0.7f, 0, 1), amber, 10, true);
            c::text(p, QRectF(body.left(), body.top() + body.height() * 0.5, body.width(), 12),
                    QStringLiteral("↔ ") + morphName(m_sound), c::alpha(amber, 0.7f), 9, true);
            c::gauge(p, QRectF(body.left(), body.top() + body.height() * 0.5 + 13, body.width(), body.height() * 0.28),
                     clampf(0.5f + 0.5f * mo, 0, 1), amber, 10, true);
        } else {
            const QRectF body = pc.rect(x0, kField, x0 + zw, 1.0f).adjusted(10, 20, -10, -10);
            c::gauge(p, QRectF(body.left(), body.top(), body.width(), body.height() * 0.55), on ? d[PubGrit] : 0,
                     amber, 12);
            // Sideways goes warm, tube, fold: the one it is on is lit.
            const float ch = on ? d[PubGritChar] : m_gritChar;
            const int which = ch < 0.34f ? 0 : ch < 0.67f ? 1 : 2;
            static const char *kinds[] = {"WARM", "TUBE", "FOLD"};
            for (int k = 0; k < 3; ++k) {
                const QRectF w(body.left() + body.width() * k / 3.0, body.top() + body.height() * 0.62, body.width() / 3.0 - 3,
                               body.height() * 0.38);
                if (k == which)
                    p.fillRect(w, c::alpha(amber, on ? 0.9f : 0.25f));
                c::text(p, w, QString::fromUtf8(kinds[k]),
                        k == which && on ? QColor::fromRgb(c::kInk) : c::alpha(amber, 0.7f), 9, true, Qt::AlignCenter);
            }
        }
    }
}

// The largest console font size, up to px, that fits s into width.
int fitPx(const QString &s, qreal width, int px) {
    for (; px > 8; --px)
        if (console::textWidth(c::font(px, true), s) <= width)
            break;
    return px;
}

// A chip that says what it is over its key, both fitted to its width, so
// a row of them still reads when the window is narrow.
void keyChip(QPainter &p, const PaintContext &pc, const QRectF &r, const QString &name, const QString &key, bool lit,
             c::Hotspots &spots, int id) {
    c::chip(p, pc, r, QString(), lit, &spots, id);
    const QColor ink = lit ? QColor::fromRgb(c::kInk) : pc.color;
    const qreal w = r.width() - 6;
    // At Shape: the plain name alone, a line lower; the key is Studio's.
    if (console::paintLevel() == 1) {
        const QString plain = console::friendly(name);
        c::text(p, QRectF(r.left() + 3, r.top(), w, r.height()), plain, ink, fitPx(plain, w, 12), true, Qt::AlignCenter);
        return;
    }
    c::text(p, QRectF(r.left() + 3, r.top() + 1, w, 16), name, ink, fitPx(name, w, 12), true, Qt::AlignCenter);
    c::text(p, QRectF(r.left() + 3, r.top() + 16, w, 13), key, lit ? ink : c::alpha(ink, 0.7f), fitPx(key, w, 10), true,
            Qt::AlignCenter);
}

// A setting the keys step, as one box: its name over its value, the left
// half clicks down (id) and the right half up (id + 1), each half showing
// its key at its edge.
void stepChip(QPainter &p, const PaintContext &pc, const QRectF &r, const QString &rawName, const QString &value,
              const QString &rawDown, const QString &rawUp, c::Hotspots &spots, int id) {
    // At Shape: a plain name and − / + for its halves.
    const bool shape = console::paintLevel() == 1;
    const QString name = console::friendly(rawName);
    const QString down = shape ? QStringLiteral("−") : rawDown, up = shape ? QStringLiteral("+") : rawUp;
    const QRectF left(r.left(), r.top(), r.width() / 2, r.height()), right(left.right(), r.top(), r.width() / 2, r.height());
    spots.add(left, id);
    spots.add(right, id + 1);
    const bool overL = pc.mouseOver && left.contains(pc.mouse), overR = pc.mouseOver && right.contains(pc.mouse);
    if (overL || overR)
        p.fillRect(overL ? left : right, c::alpha(pc.color, 0.22f));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(overL || overR ? pc.color : c::alpha(pc.color, 0.6f), 1.5));
    p.drawRect(r);
    const qreal keyW = std::max(console::textWidth(c::font(12, true), down),
                                console::textWidth(c::font(12, true), up)) + 4;
    const QRectF bottom(r.left() + 4, r.top() + 13, r.width() - 8, 16);
    c::text(p, QRectF(r.left() + 3, r.top() + 1, r.width() - 6, 13), name, c::alpha(pc.color, 0.65f),
            fitPx(name, r.width() - 6, 10), true, Qt::AlignCenter);
    c::text(p, bottom, down, overL ? pc.color : c::alpha(pc.color, 0.7f), 12, true, Qt::AlignLeft | Qt::AlignVCenter);
    c::text(p, bottom, up, overR ? pc.color : c::alpha(pc.color, 0.7f), 12, true, Qt::AlignRight | Qt::AlignVCenter);
    const qreal vw = bottom.width() - 2 * keyW;
    c::text(p, QRectF(bottom.left() + keyW, bottom.top(), vw, bottom.height()), value, pc.color, fitPx(value, vw, 12),
            true, Qt::AlignCenter);
}

// Below the pad, every one clickable, each with its key: the clips and the
// sounds, then the settings the keys step and the switches.
void Control::paintBand(QPainter &p, const PaintContext &pc) {
    m_spots.clear();
    c::clips(p, c::bandRow(pc, 0, 0, 0.36), *pc.view, pc.color, pc.time, &m_spots);
    static const char keys[] = "QWERTYUIOP";
    static_assert(sizeof(keys) - 1 == kLetteredSounds, "a key for every lettered sound");
    // A sound's key: its letter, or, for those past P, how many steps of →
    // from P reach it.
    auto keyOf = [](int s) {
        return s < kLetteredSounds ? QString(QChar(keys[s])) : QStringLiteral("P→%1").arg(s - kLetteredSounds + 1);
    };
    {
        // As many chips as fit with their names whole at the full size;
        // more sounds than that, and the row is a window onto them that
        // follows the one playing, the chip at each end faded where there
        // are more beyond it (a click there moves along), as console::chips
        // does for a row of plain labels.
        QRectF row = c::bandRow(pc, 0, 0.38, 1);
        constexpr double gap = 4;
        // → at the end: the next sound, as the → key.
        console::nextChip(p, pc, QRectF(row.right() - console::kNextChipWidth, row.top(), console::kNextChipWidth, row.height()));
        row.setRight(row.right() - console::kNextChipWidth - gap);
        double need = 0;
        for (int s = 0; s < SoundCount; ++s)
            need = std::max(need, console::textWidth(c::font(12, true), QString::fromUtf8(recipe(s).name)) + 12);
        const int fits = std::max(3, int((row.width() + gap) / (need + gap)));
        // More than fit: those around the one playing, then "+N MORE",
        // which opens every sound in the browser.
        const bool over = SoundCount > fits;
        const int shown = over ? fits - 1 : SoundCount;
        const int first = over ? console::windowFirst(m_sound, std::max(0, m_sound - shown / 2), shown, SoundCount) : 0;
        const qreal w = (row.width() + gap) / double(over ? fits : shown);
        for (int k = 0; k < shown; ++k) {
            const int s = first + k;
            keyChip(p, pc, QRectF(row.left() + k * w, row.top(), w - gap, row.height()), QString::fromUtf8(recipe(s).name),
                    keyOf(s), s == m_sound, m_spots, SpotSound + s);
        }
        if (over)
            console::browseChip(p, pc, QRectF(row.left() + shown * w, row.top(), w - gap, row.height()),
                                SoundCount - shown);
    }

    const QRectF row = c::bandRow(pc, 1);
    qreal x = row.left();
    auto stepper = [&](double share, const QString &name, const QString &value, const QString &down,
                       const QString &up, int id) {
        const qreal w = row.width() * share;
        stepChip(p, pc, QRectF(x, row.top(), w - 4, row.height()), name, value, down, up, m_spots, id);
        x += w;
    };
    const int roll = rollIndex();
    stepper(0.11, QStringLiteral("OCTAVE"), octaveText().toUpper(), QStringLiteral("↓"), QStringLiteral("↑"),
            SpotOctDown);
    stepper(0.14, QStringLiteral("ROLL"), roll < 0 ? QStringLiteral("OFF") : QString::fromUtf8(kPatterns[roll].name),
            QStringLiteral("⇧←"), QStringLiteral("⇧→"), SpotRollDown);
    stepper(0.12, QStringLiteral("KEY TONE"), QStringLiteral("%1%").arg(int(std::lround(m_keyTone * 100))),
            QStringLiteral("−"), QStringLiteral("+"), SpotToneDown);
    stepper(0.12, QStringLiteral("SLIDE"), QStringLiteral("%1MS").arg(int(std::lround(1000 * glideSeconds()))),
            QStringLiteral("⇧↓"), QStringLiteral("⇧↑"), SpotSlideDown);
    stepper(0.12, morphName(m_sound), QStringLiteral("%1%").arg(int(std::lround(m_morph[m_sound] * 100))),
            QStringLiteral("⇧["), QStringLiteral("⇧]"), SpotMorphDown);

    // The switches, lit when on; LATCH blinks while armed.
    bool latched = false;
    for (int s = 0; s < ShaperCount; ++s)
        latched |= m_latched[s];
    const bool blink = std::fmod(pc.time, 0.5) < 0.25;
    const qreal w = (row.right() - x + 4) / 5.0;
    auto toggle = [&](int i, const QString &name, const QString &key, bool lit, int id) {
        keyChip(p, pc, QRectF(x + i * w, row.top(), w - 4, row.height()), name, key, lit, m_spots, id);
    };
    {
        // A sound with nothing to switch (no saw, no string) shows the
        // switch dimmed and dashed out, so it doesn't sit there looking lit.
        const bool has = hasSwitch(m_sound);
        if (!has) {
            p.save();
            p.setOpacity(0.25);
        }
        toggle(0, recipe(m_sound).string > 0 || recipe(m_sound).marimbula > 0 ? QStringLiteral("MUTE") : !has ? QStringLiteral("—")
                  : m_square[m_sound] ? QStringLiteral("SQUARE") : QStringLiteral("SAW"),
               // Past P a sound has no letter, so no key: the chip is clicked.
               has && m_sound < kLetteredSounds ? QStringLiteral("⇧%1").arg(QChar(keys[m_sound])) : QString(),
               has && m_square[m_sound], SpotSquare);
        if (!has)
            p.restore();
    }
    toggle(1, QStringLiteral("FOLLOW"), QStringLiteral("§"), m_follow, SpotFollow);
    toggle(2, QStringLiteral("LATCH"), QStringLiteral("⇧⏎"), m_latchNext ? blink : latched, SpotLatch);
    toggle(3, QStringLiteral("OVERDUB"), QStringLiteral("F10"), pc.view->overdub, SpotOverdub);
    toggle(4, QStringLiteral("LET GO"), QStringLiteral("DEL"), false, SpotFree);
}

} // namespace

PartSpec makeBass() {
    PartSpec s;
    s.name = QStringLiteral("Bass");
    s.blurb = QStringLiteral("sub, reese, acid, 808, upright, log drum, tuba");
    s.color = QColor("#FF4FD8");
    s.engine = std::make_unique<bass::Engine>();
    s.control = std::make_unique<Control>();
    // Mono, centred and dry: the room would only blur the low end. The
    // channel pumps lightly; each sound adds its own duck in the engine.
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.0f, 0.0f, 0.25f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
