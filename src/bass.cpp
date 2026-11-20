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
// back. Above, the shaper rail, four zones a finger lands in to bend
// whatever is sounding, the loop included, for as long as it stays:
//
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
// an octave up), the eight sounds on Q to I, O to follow the chords, and
// the usual F1–F8, Backspace for the clips.

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "bass.h"
#include "console.h"
#include "parts.h"
#include "phrase.h"

namespace tf {

namespace {

using namespace bass;

constexpr float kField = 0.76f; // the note field below this height, the rail above
constexpr int kLowDegree = -2;  // the leftmost column: two degrees under the root
constexpr int kColumns = 12;
constexpr int kKeyIdBase = 0x200000; // ids for notes played on the keyboard
constexpr double kHold = 0.25;  // a touch this long is a hold, not a tap

enum Role { NoRole, NoteRole, OctRole, RollRole, WobRole, GritRole };

const char *roleName(Role r) {
    switch (r) {
    case OctRole: return "OCT";
    case RollRole: return "ROLL";
    case WobRole: return "WOB";
    case GritRole: return "GRIT";
    default: return "";
    }
}

Shaper shaperOf(Role r) {
    return r == OctRole ? ShapeOct : r == RollRole ? ShapeRoll : r == WobRole ? ShapeWob : ShapeGrit;
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
        m_phrase.expire(f.t);
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
        holds(f.t, ctx);
    }

    void idle(double t, ControlContext &ctx) override { holds(t, ctx); }

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
                sendShaper(Shaper(s), 0, 0, 0, ctx, true);
        m_frame = Frame{};
        m_button = false;
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
        const PartView &pv = ctx.studio().parts[ctx.part()];
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        case Qt::Key_F9:
            if (pv.recording >= 0) {
                ctx.stopRecording();
                ctx.notice(QStringLiteral("Bass recording ends at the bar"));
            } else {
                const int slot = std::max(0, ctx.freeSlot());
                resetSlot(slot, ctx);
                ctx.record(slot, 8);
                ctx.notice(QStringLiteral("Bass records into slot %1 from the next bar").arg(slot + 1));
            }
            return true;
        case Qt::Key_F10:
            ctx.overdub(!pv.overdub);
            ctx.notice(pv.overdub ? QStringLiteral("Overdub off") : QStringLiteral("Overdub: play over the loop"));
            return true;
        case Qt::Key_Q: case Qt::Key_W: case Qt::Key_E: case Qt::Key_R:
        case Qt::Key_T: case Qt::Key_Y: case Qt::Key_U: case Qt::Key_I: {
            static const int order[] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R,
                                        Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I};
            for (int s = 0; s < SoundCount; ++s)
                if (order[s] == k.key)
                    m_sound = s;
            ctx.notice(QStringLiteral("%1: %2").arg(QString::fromUtf8(recipe(m_sound).name),
                                                   QString::fromUtf8(recipe(m_sound).what)));
            return true;
        }
        case Qt::Key_O:
            m_follow = !m_follow;
            ctx.notice(m_follow ? QStringLiteral("Bass follows the chords: columns count from the chord's root")
                                : QStringLiteral("Bass plays the key"));
            return true;
        case Qt::Key_P:
            for (int s = 0; s < ShaperCount; ++s)
                if (m_latched[s]) {
                    m_latched[s] = false;
                    sendShaper(Shaper(s), 0, 0, ctx.now(), ctx, true);
                }
            ctx.notice(QStringLiteral("Shapers released"));
            return true;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            m_morph[m_sound] = clampf(m_morph[m_sound] + (k.key == Qt::Key_BracketRight ? 0.1f : -0.1f), 0, 1);
            ctx.notice(QStringLiteral("%1 morph %2%").arg(QString::fromUtf8(recipe(m_sound).name))
                           .arg(int(std::lround(m_morph[m_sound] * 100))));
            return true;
        case Qt::Key_Minus:
        case Qt::Key_Equal:
            m_glide = clampf(m_glide + (k.key == Qt::Key_Equal ? 0.125f : -0.125f), 0, 1);
            ctx.notice(QStringLiteral("Slide %1 ms").arg(int(std::lround(1000 * glideSeconds()))));
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            m_octave = std::clamp(m_octave + (k.key == Qt::Key_Up ? 1 : -1), 0, 2);
            ctx.notice(QStringLiteral("Bass octave %1").arg(m_octave == 0 ? QStringLiteral("low")
                                                            : m_octave == 1 ? QStringLiteral("+1")
                                                                            : QStringLiteral("+2")));
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
            m_keyTone = clampf(m_keyTone + (k.key == Qt::Key_Right ? 0.1f : -0.1f), 0, 1);
            ctx.notice(QStringLiteral("Keyboard tone %1%").arg(int(std::lround(m_keyTone * 100))));
            return true;
        default:
            return false;
        }
    }

    // --- the screen ---------------------------------------------------------

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "touch the lower field\ta note of the scale; higher is brighter, from clean sub to biting\n"
            "press harder\taccent: louder, brighter, more drive\n"
            "slide sideways\tglide to the next note\n"
            "twist / flatten the finger\tmorph the sound / fatten it\n"
            "second note while one is held\tslide to it without a new attack; lift to slide back\n"
            "OCT (top left)\tup an octave while held, two fingers two; tap it to pump octaves\n"
            "ROLL\tthe held note strikes in a rhythm on the bar; sideways picks it, up/down the gate\n"
            "WOB\ttempo-synced wobble; sideways the rate (1/2 to 1/32), up/down the depth\n"
            "GRIT\tdrive; up/down how much, sideways warm, tube, fold\n"
            "click with a note down\tstrike it again, accented\n"
            "click with only shapers down\tlatch them; P releases\n"
            "tap a riff, then hold its first note on the next beat\tloops it into the next free slot\n"
            "Z X C … /   A S D … '\tthe scale from the keyboard, low row and an octave up; Shift accents\n"
            "Q W E R T Y U I\tsub, pluck, reese, acid, wobble, growl, 808, bounce\n"
            "O\tfollow the chords: the line moves with the Chords page\n"
            "[ ]   - =\tmorph the sound   shorter / longer slides\n"
            "↑ ↓   ← →\tthe pad's octave   the keyboard's tone\n"
            "F1–F8, Shift+F1–F8\tlaunch a slot at the next bar, clear it\n"
            "F9   F10   Backspace\trecord from the next bar   overdub   stop the loop");
    }

    QString status() const override {
        QStringList parts;
        parts << QString::fromUtf8(recipe(m_sound).name);
        for (const auto &h : m_fingers) {
            if (h.id < 0)
                continue;
            switch (h.role) {
            case NoteRole:
                parts << QStringLiteral("%1 tone %2%").arg(noteName(h.degree)).arg(int(std::lround(h.tone * 100)));
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
            default:
                break;
            }
        }
        for (int s = 0; s < ShaperCount; ++s)
            if (m_latched[s])
                parts << QStringLiteral("%1 latched").arg(QString::fromUtf8(roleName(Role(s + OctRole))).toLower());
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
        float sentA = 0, sentB = 0;                       // what this shaper last asked for
        bool long_ = false, looped = false;
    };
    struct KeyNote {
        int key = 0, id = 0;
        double t0 = 0;
        bool long_ = false, looped = false;
        int group = 0; // the note, for closing a riff
    };
    static constexpr int kNoKey = -1000;

    Finger *find(int id) {
        for (auto &h : m_fingers)
            if (h.id == id)
                return &h;
        return nullptr;
    }

    float glideSeconds() const { return recipe(m_sound).glide * std::exp2((m_glide - 0.5f) * 4.0f); }

    QString noteName(int degree) const {
        Tuning t;
        t.set(m_root, m_family, m_brightness, false);
        const int pc12 = int(std::lround(t.baseMidi() + t.degreeSemis(degree + (m_follow ? m_chordRoot : 0)))) % 12;
        return QString::fromUtf8(Tuning::noteName((pc12 + 12) % 12));
    }

    void land(Finger &h, const Touch &t, double time, ControlContext &ctx) {
        h = Finger{};
        h.id = t.id;
        h.t0 = time;
        h.x0 = t.x;
        h.y0 = t.y;
        h.angle0 = t.angle;
        if (t.y >= kField) {
            h.role = Role(OctRole + std::clamp(int(t.x * 4.0f), 0, 3));
            const Shaper s = shaperOf(h.role);
            // A finger on a latched zone takes it over, and lets it go on lift.
            m_latched[s] = false;
            switch (h.role) {
            case RollRole:
                h.index = m_pattern;
                h.amount = m_gate;
                break;
            case WobRole:
                h.index = m_rate;
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
        on.i[2] = (m_follow ? Follow : 0) | (t.pressure > 0.82f ? Accent : 0);
        on.i[3] = h.sound;
        on.f[0] = velocityOf(t.pressure);
        on.f[1] = h.tone;
        on.f[2] = h.morph;
        on.f[3] = h.fat;
        on.f[4] = m_glide;
        ctx.send(on);
    }

    void drag(Finger &h, const Touch &t, double time, ControlContext &ctx) {
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
            if (!h.long_ && !h.looped)
                m_phrase.tap(h.t0, time - h.t0, h.degree + 100 * h.octave);
        } else if (h.role != NoRole) {
            remember(h);
            const Shaper s = shaperOf(h.role);
            // Another finger still in the same zone takes over where it is.
            Finger *other = nullptr;
            for (auto &f : m_fingers)
                if (f.id >= 0 && f.role == h.role && (!other || f.t0 > other->t0))
                    other = &f;
            if (s == ShapeOct)
                sendShaper(s, float(octFingers()), 0, time, ctx, true);
            else if (other)
                sendShaper(s, other->sentA, other->sentB, time, ctx, true);
            else if (!m_latched[s])
                sendShaper(s, 0, 0, time, ctx, true);
        }
    }

    // A shaper finger: where it is now, against where it landed.
    void shape(Finger &h, const Touch &t, double time, ControlContext &ctx, bool first) {
        const float dx = t.x - h.x0, dy = t.y - h.y0;
        const float press = t.pressure - 0.5f;
        float a = 0, b = 0;
        switch (h.role) {
        case OctRole:
            a = float(octFingers());
            break;
        case RollRole:
            h.index = std::clamp(m_pattern + int(std::lround(dx / 0.08f)), 0, kPatternCount - 1);
            h.amount = clampf(m_gate + dy * 2.5f, 0.15f, 1.0f);
            a = h.amount;
            b = float(h.index);
            break;
        case WobRole:
            h.index = std::clamp(m_rate + int(std::lround(dx / 0.08f)), 0, kRateCount - 1);
            h.amount = clampf(m_depth + dy * 2.5f, 0.05f, 1.0f);
            a = clampf(h.amount + 0.4f * press, 0.05f, 1.0f);
            b = float(h.index);
            break;
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

    // The shaper's last place is where the next one starts.
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
            n += h.id >= 0 && h.role == OctRole;
        return std::min(n, 2);
    }

    void sendShaper(Shaper s, float a, float b, double time, ControlContext &ctx, bool force) {
        float *last = m_sent[s];
        const bool changed = std::fabs(a - last[0]) > 0.005f || b != last[1];
        if (!changed && !force)
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
        if (!shapers)
            return;
        QStringList names;
        for (const auto &h : m_fingers)
            if (h.id >= 0 && h.role != NoteRole) {
                m_latched[shaperOf(h.role)] = true;
                remember(h);
                names << QString::fromUtf8(roleName(h.role));
            }
        names.removeDuplicates();
        ctx.notice(QStringLiteral("%1 latched: P lets go").arg(names.join(QStringLiteral(" + "))));
    }

    // Holds close a tapped phrase into a loop.
    void holds(double t, ControlContext &ctx) {
        for (auto &h : m_fingers)
            if (h.id >= 0 && h.role == NoteRole && !h.long_ && t - h.t0 >= kHold) {
                h.long_ = true;
                h.looped = closeLoop(h.t0, h.degree + 100 * h.octave, ctx);
            }
        for (auto &k : m_keys)
            if (k.key && !k.long_ && t - k.t0 >= kHold) {
                k.long_ = true;
                k.looped = closeLoop(k.t0, k.group, ctx);
            }
    }

    // A riff loops when the hold lands on its first note again, on time: a
    // line that merely ends on a long note doesn't.
    bool closeLoop(double t0, int group, ControlContext &ctx) {
        double start = 0;
        if (m_phrase.taps().empty() || m_phrase.taps().front().group != group || !m_phrase.closes(t0, group, &start))
            return false;
        const int slot = std::max(0, ctx.freeSlot());
        resetSlot(slot, ctx);
        ctx.capture(slot, start, t0);
        ctx.notice(QStringLiteral("Bass loop in slot %1").arg(slot + 1));
        m_phrase.clear();
        return true;
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
        *slot = {code, kKeyIdBase + code, now, false, false, degree + 100 * octave};
        Command on;
        on.type = Command::On;
        on.id = slot->id;
        on.t = now;
        on.i[0] = degree;
        on.i[1] = octave;
        on.i[2] = (m_follow ? Follow : 0) | (accent ? Accent : 0);
        on.i[3] = m_sound;
        on.f[0] = accent ? 1.0f : 0.75f;
        on.f[1] = m_keyTone;
        on.f[2] = m_morph[m_sound];
        on.f[3] = 0;
        on.f[4] = m_glide;
        ctx.send(on);
    }

    void keyOff(int code, ControlContext &ctx) {
        for (auto &k : m_keys)
            if (k.key == code) {
                const double now = ctx.now();
                off(k.id, now, ctx);
                if (!k.long_ && !k.looped)
                    m_phrase.tap(k.t0, std::max(0.05, now - k.t0), k.group);
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
    Phrase m_phrase;
    Frame m_frame;
    bool m_button = false;

    int m_sound = Pluck;
    float m_morph[SoundCount];
    float m_glide = 0.5f, m_keyTone = 0.6f;
    int m_octave = 0;
    bool m_follow = false;

    // Where each shaper starts next time.
    int m_pattern = 3, m_rate = 3;
    float m_gate = 0.6f, m_depth = 0.8f, m_grit = 0.5f, m_gritChar = 0.25f;
    bool m_latched[ShaperCount] = {};
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
    Tuning tuning;
    tuning.set(sv.root, sv.family, sv.brightness, sv.just);

    c::pad(p, pc, QStringLiteral("Bass"));
    paintField(p, pc, tuning);
    paintScope(p, pc);
    paintRail(p, pc);

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
            label = noteName(h->degree);
        } else if (h) {
            col = c::amber();
            switch (h->role) {
            case OctRole:
                label = octFingers() > 1 ? QStringLiteral("+2 OCT") : QStringLiteral("+1 OCT");
                break;
            case RollRole:
                label = QString::fromUtf8(kPatterns[h->index].name);
                break;
            case WobRole:
                label = QStringLiteral("WOB %1").arg(QString::fromUtf8(kRateNames[h->index]));
                break;
            case GritRole:
                label = QStringLiteral("%1 %2")
                            .arg(QString::fromUtf8(gritName(h->second)))
                            .arg(int(std::lround(h->amount * 100)));
                break;
            default:
                break;
            }
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
                            rootShift(tuning);
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
    for (int lane = 1; lane >= 0; --lane) {
        const float *ln = d + PubLane + 8 * lane;
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
            c::text(p, QRectF(r.left(), r.top() + 26, r.width(), 14), QStringLiteral("LOOP"),
                    c::alpha(pc.color, a), 10, true, Qt::AlignCenter);
    }

    // The sound, big, at the top left of the field.
    const Recipe &R = recipe(m_sound);
    const QPointF at = pc.at(0.012f, kField - 0.045f);
    const QRectF tagR = c::tag(p, at, QString::fromUtf8(R.name), pc.color, 16);
    c::text(p, QRectF(tagR.right() + 10, tagR.top(), pc.pad.width() * 0.6, tagR.height()),
            QStringLiteral("%1 · morph %2% · slide %3 ms")
                .arg(QString::fromUtf8(R.what))
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
    if (d[PubScopeCycles] <= 0)
        return;
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
    bool held[ShaperCount] = {};
    for (const auto &h : m_fingers)
        if (h.id >= 0 && h.role != NoteRole && h.role != NoRole)
            held[shaperOf(h.role)] = true;
    for (int z = 0; z < ShaperCount; ++z) {
        const Shaper s = Shaper(z);
        const QRectF r = pc.rect(0.25f * float(z), kField, 0.25f * float(z + 1), 1.0f).adjusted(6, 14, -6, -6);
        const bool on = s == ShapeOct    ? d[PubOct] > 0
                        : s == ShapeRoll ? d[PubRoll] > 0
                        : s == ShapeWob  ? held[s] || m_latched[s]
                                         : d[PubGrit] > 0;
        const int pattern = on && s == ShapeRoll ? std::clamp(int(d[PubRoll]) - 1, 0, kPatternCount - 1) : m_pattern;
        const int rate = on && s == ShapeWob ? std::clamp(int(d[PubWobRate]), 0, kRateCount - 1) : m_rate;
        QString title;
        switch (s) {
        case ShapeOct:
            title = d[PubOct] > 1.5f ? QStringLiteral("OCT +2") : QStringLiteral("OCT");
            break;
        case ShapeRoll:
            title = QStringLiteral("ROLL %1").arg(QString::fromUtf8(kPatterns[pattern].name));
            break;
        case ShapeWob:
            title = QStringLiteral("WOB %1").arg(QString::fromUtf8(kRateNames[rate]));
            break;
        default:
            title = QStringLiteral("GRIT %1").arg(QString::fromUtf8(gritName(on ? d[PubGritChar] : m_gritChar)));
            break;
        }
        c::button(p, r, title, amber, on || held[s]);
        if (m_latched[s])
            c::tag(p, QPointF(r.right() - 54, r.top() + 11), QStringLiteral("LATCH"), c::red(), 10);
        const QRectF body = r.adjusted(8, 28, -8, -8);
        switch (s) {
        case ShapeOct: {
            const int lit = int(std::lround(d[PubOct]));
            for (int k = 0; k < 2; ++k) {
                const QRectF b(body.left() + k * (body.width() / 2), body.top(), body.width() / 2 - 6, body.height());
                p.fillRect(b, c::alpha(amber, k < lit ? 0.85f : 0.1f));
                c::text(p, b, k == 0 ? QStringLiteral("+12") : QStringLiteral("+24"),
                        k < lit ? QColor::fromRgb(c::kInk) : c::alpha(amber, 0.5f), 11, true, Qt::AlignCenter);
            }
            break;
        }
        case ShapeRoll: {
            const int steps = patternSteps(pattern);
            const qreal w = body.width() / steps;
            const int now = int(d[PubRollStep]);
            for (int k = 0; k < steps; ++k) {
                const QRectF b(body.left() + k * w, body.top(), w - 3, body.height());
                const bool x = kPatterns[pattern].steps[k] == 'x';
                const bool cur = on && k == now;
                p.fillRect(b, c::alpha(amber, x ? (cur ? 0.95f : 0.45f) : (cur ? 0.2f : 0.06f)));
            }
            break;
        }
        case ShapeWob: {
            // The LFO's shape, with a dot where it is.
            QPainterPath path;
            for (int k = 0; k <= 32; ++k) {
                const float ph = float(k) / 32.0f;
                const float v = 0.5f - 0.5f * std::cos(kTwoPi * ph);
                const QPointF pt(body.left() + body.width() * ph, body.bottom() - body.height() * v);
                if (k == 0)
                    path.moveTo(pt);
                else
                    path.lineTo(pt);
            }
            p.setPen(QPen(c::alpha(amber, 0.5f), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            if (d[PubLfo] >= 0 && d[PubWobDepth] > 0) {
                const float v = d[PubLfo];
                const float ph = std::acos(clampf(1.0f - 2.0f * v, -1, 1)) / kTwoPi;
                p.setBrush(amber);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(body.left() + body.width() * ph, body.bottom() - body.height() * v), 4, 4);
            }
            break;
        }
        default:
            c::gauge(p, QRectF(body.left(), body.top() + body.height() * 0.2, body.width(), body.height() * 0.6),
                     on ? d[PubGrit] : 0, amber, 12);
            break;
        }
    }
}

void Control::paintBand(QPainter &p, const PaintContext &pc) {
    const QRectF band(pc.pad.left(), pc.pad.bottom() + 14, pc.pad.width(), 30);
    c::clips(p, QRectF(band.left(), band.top(), band.width() * 0.5, band.height()), *pc.view, pc.color, pc.time);
    static const char keys[] = "QWERTYUI";
    const qreal x0 = band.left() + band.width() * 0.52, w = (band.right() - x0) / double(SoundCount);
    for (int s = 0; s < SoundCount; ++s) {
        const QRectF cell(x0 + s * w + 2, band.top(), w - 4, band.height());
        const bool cur = s == m_sound;
        if (cur) {
            p.fillRect(cell, pc.color);
        } else {
            p.setPen(QPen(c::alpha(pc.color, 0.5f), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cell);
        }
        c::text(p, cell, QStringLiteral("%1 %2").arg(QChar(keys[s])).arg(QString::fromUtf8(recipe(s).name)),
                cur ? QColor::fromRgb(c::kInk) : c::alpha(pc.color, 0.8f), 10, true, Qt::AlignCenter);
    }
}

} // namespace

PartSpec makeBass() {
    PartSpec s;
    s.name = QStringLiteral("Bass");
    s.blurb = QStringLiteral("sub, reese, acid, wobble, 808");
    s.color = QColor("#FF4FD8");
    s.engine = std::make_unique<bass::Engine>();
    s.control = std::make_unique<Control>();
    // Mono, centred and dry: the room would only blur the low end. The kick
    // pumps it, and the sub a little more, inside the engine.
    const float channel[ChannelCount] = {0.8f, 0.5f, 0.5f, 0.0f, 0.0f, 0.55f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
