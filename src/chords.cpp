// Chords: the harmonic heart of the track, and the part the others follow.
//
// The pad is the key's chords. Each column is a degree of the key (I to VII
// in a seven-note mode, fewer or more in other scales), each of the five rows
// a colour, from a bare power chord at the bottom through sus, triad and
// add9 to the seventh at the top. One finger anywhere is a whole chord, in
// key, voiced so it flows from the last one: the engine tries every
// inversion and keeps the one whose voices move least. Another finger takes
// over with a new chord (pads glide into it); lifting it returns to the
// chord still held. Sliding up or down changes the colour while it sounds,
// pressing harder swells it, a flat finger doubles it out to both octaves,
// and twisting turns its filter.
//
// The rail along the top is the rhythm. Each sound keeps its own gate
// pattern, synced to the bar: tap a cell to keep it, hold a cell to play it
// over everything for as long as the finger stays (a fill over a loop),
// higher on the rail for longer hits, more fingers for double and quadruple
// time.
//
// Loops are made on purpose: play, then press Space (the studio's loop key,
// the same on every page) and what was just played loops; P loops the last
// whole bars at the studio's tempo. The page shows a loop on its way before
// it happens and says what it caught. Pressing the pad until it clicks, or
// Return, holds chords: each rings until the next one, and Delete lets go.
//
// The keyboard plays the same chords: A S D F G H J are I to VII as triads,
// the Q row adds the ninth, the Z row is sus, Shift makes sevenths (power on
// the Z row). The other keys are the ones every part page shares (← → the
// sound, ↑ ↓ the voicing, Shift+← → the gate, - + the filter), and the band
// under the pad has a chip for each, so the mouse alone reaches everything.
//
// Every chord that sounds, live or from a loop, tells the studio
// (ctx.setChord), so Bass, Arp and Lead can follow it.

#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>
#include <array>
#include <cmath>

#include "chords.h"
#include "console.h"
#include "parts.h"
#include "songfile.h"

namespace tf {

namespace chords {

namespace {

constexpr float kRail = 0.8f; // the rhythm rail starts this high on the pad
constexpr float kRowHeight = kRail / float(ColourCount);
constexpr int kHeldMax = 16;

int rowAt(float y) { return std::clamp(int(y / kRowHeight), 0, ColourCount - 1); }
int columnsOf(const Tuning &t) { return std::min(8, t.degreesPerOctave()); }
int railCellAt(float x) { return std::clamp(int(x * float(PatternCount)), 0, PatternCount - 1); }

Tuning tuningOf(const StudioView &s) {
    Tuning t;
    t.set(s.root, s.family, s.brightness, s.just);
    return t;
}

int pitchClass(double midi) { return ((int(std::lround(midi)) % 12) + 12) % 12; }

// Chord names keep their case: the console's capitals would make A minor
// read as A major.
void chordText(QPainter &p, const QRectF &r, const QString &s, QColor color, int px, bool bold = true,
               Qt::Alignment align = Qt::AlignCenter) {
    QFont f = console::font(px, bold);
    f.setCapitalization(QFont::MixedCase);
    p.setFont(f);
    p.setPen(color);
    console::drawText(p, r, int(align), s);
}

// A chord's name from what it is made of: the intervals of its tones above
// the root, whatever scale they came from.
QString chordName(const Tuning &t, const Shape &s) {
    if (s.count <= 0)
        return {};
    const double root = t.degreeSemis(s.tones[0]);
    bool has[12] = {};
    for (int i = 1; i < s.count; ++i)
        has[((int(std::lround(t.degreeSemis(s.tones[i]) - root)) % 12) + 12) % 12] = true;
    const QString name = QString::fromUtf8(Tuning::noteName(pitchClass(t.baseMidi() + root)));
    const bool maj = has[4], min = has[3] && !has[4];
    const bool p5 = has[7], d5 = has[6] && !p5, a5 = has[8] && !p5;
    if (!maj && !min && p5 && !has[5] && !has[2] && !has[10] && !has[11] && !has[9])
        return name + "5";
    if (!maj && !min && has[5] && has[10])
        return name + "7sus4";
    QString q;
    if (maj)
        q = a5 ? "+" : "";
    else if (min)
        q = d5 && !has[10] ? "°" : "m";
    if (has[11])
        q += min ? "(maj7)" : "maj7";
    else if (has[10])
        q += min && d5 ? "7♭5" : "7";
    else if (has[9] && (maj || min))
        q += "6";
    if (!maj && !min) {
        if (has[5])
            q += "sus4";
        else if (has[2])
            q += "sus2";
    } else if (has[2]) {
        q += q.contains('7') ? "(9)" : "add9";
    } else if (has[5]) {
        q += "add11";
    }
    return name + q;
}

QString chordName(const Tuning &t, int degree, int colour) { return chordName(t, shapeOf(t, degree, colour)); }

// A lit tag like the console's, keeping the chord name's case.
QRectF nameTag(QPainter &p, QPointF at, const QString &s, QColor color, int px) {
    QFont f = console::font(px, true);
    f.setCapitalization(QFont::MixedCase);
    p.setFont(f);
    const qreal w = console::textWidth(f, s) + 12, h = px + 7;
    const QRectF r(at.x(), at.y() - h / 2, w, h);
    p.fillRect(r, color);
    p.setPen(QColor::fromRgb(console::kInk));
    console::drawText(p, r, Qt::AlignCenter, s);
    return r;
}

// The degree as a roman numeral, its case and mark from the triad on it.
QString roman(const Tuning &t, int degree) {
    static const char *numerals[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII"};
    const int n = t.degreesPerOctave();
    const int d = ((degree % n) + n) % n;
    QString r = QString::fromLatin1(numerals[std::min(d, 7)]);
    const double third = interval(t, d, nearestStep(t, d, 3.5));
    const double fifth = interval(t, d, nearestStep(t, d, 7.0));
    if (std::fabs(third - 3.0) < 0.3) {
        r = r.toLower();
        if (std::fabs(fifth - 6.0) < 0.3)
            r += "°";
    } else if (std::fabs(fifth - 8.0) < 0.3) {
        r += "+";
    }
    return r;
}

// Chips as the console draws them, each as wide as its label needs: the
// width a row of them needs, and the row laid out across r with the room
// left over shared out evenly.
qreal chipWidth(const QString &label) { return console::textWidth(console::font(12, true), label) + 10; }

qreal chipsWidth(const QStringList &labels, qreal gap) {
    qreal w = gap * qreal(std::max<qsizetype>(0, labels.size() - 1));
    for (const auto &l : labels)
        w += chipWidth(l);
    return w;
}

void fitChips(QPainter &p, const PaintContext &pc, const QRectF &r, const QStringList &labels, const QList<bool> &lit,
              const QList<int> &ids, qreal gap, console::Hotspots *spots) {
    const qsizetype n = labels.size();
    if (n == 0)
        return;
    const qreal extra = std::max<qreal>(0, r.width() - chipsWidth(labels, gap)) / qreal(n);
    qreal x = r.left();
    for (qsizetype i = 0; i < n; ++i) {
        const qreal w = i == n - 1 ? r.right() - x : chipWidth(labels[i]) + extra;
        console::chip(p, pc, QRectF(x, r.top(), w, r.height()), labels[i], lit.value(i), spots, ids.value(i, -1));
        x += w + gap;
    }
}

}

class Control : public PartControl {
public:
    Control() {
        for (int s = 0; s < StyleCount; ++s) {
            m_pattern[s] = styleDef(s).pattern;
            m_length[s] = 0.55f;
            m_filter[s] = 0.5f;
        }
    }

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        const Tuning t = tuningOf(ctx.studio());
        // Pressing the pad down until it clicks: hold.
        if (f.button && !m_button)
            setHold(!m_hold, ctx, f.t);
        m_button = f.button;
        // Lifts first, so a finger replaced in the same frame frees its place.
        for (auto &h : m_held) {
            if (h.touch < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.touch;
            if (!still)
                lift(h, f.t, ctx);
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &tc = f.touches[i];
            Held *h = byTouch(tc.id);
            if (!h)
                land(tc, f.t, t, ctx);
            else if (h->role == Held::Chord)
                moveChord(*h, tc, f.t, ctx);
            else
                moveRail(*h, tc, ctx);
        }
    }

    void background(double t, ControlContext &ctx) override {
        m_beatNow = ctx.beatAt(t);
        carryOn(t, ctx);
        m_take = ctx.studio().parts[ctx.part()];
        // A loop just landed (Space's take is made at once, P's at its bar
        // line): say what it holds, for a bar.
        if (m_take.takes != m_seenTakes) {
            if (m_seenTakesValid && m_take.playing >= 0) {
                m_landedAt = m_beatNow;
                m_landedSlot = m_take.playing;
                m_landedBeats = m_take.clips[m_take.playing].lengthBeats;
            }
            m_seenTakes = m_take.takes;
        }
        m_seenTakesValid = true;
        // P's wait ends just before the bar line: the take goes to the
        // studio, which lands it on that line. Until then P again can still
        // call it off.
        if (m_retroEnd >= 0 && ctx.beatAt(t) >= m_retroEnd - 0.06) {
            const double end = m_retroEnd;
            m_retroEnd = -1;
            retroCapture(end, ctx);
        }
        // The progression as it goes by, live or from a loop.
        const StudioView &s = ctx.studio();
        if (s.chordSeq != m_seenSeq || (m_heard && m_ribbonCount == 0)) {
            m_seenSeq = s.chordSeq;
            Shape sh;
            sh.count = s.chordCount;
            for (int i = 0; i < 4; ++i)
                sh.tones[i] = s.chordTones[i];
            const Shape &last = m_ribbon[(m_ribbonHead + kRibbon - 1) % kRibbon];
            bool same = m_ribbonCount > 0 && last.count == sh.count;
            for (int i = 0; same && i < sh.count; ++i)
                same = last.tones[i] == sh.tones[i];
            if (!same && m_heard) {
                m_ribbon[m_ribbonHead] = sh;
                m_ribbonHead = (m_ribbonHead + 1) % kRibbon;
                m_ribbonCount = std::min(m_ribbonCount + 1, kRibbon);
            }
        }
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        const double t = ctx.now();
        int degree = -1, colour = Triad;
        chordKey(k.key, shift, &degree, &colour);
        if (degree >= 0) {
            // As many chord keys as the pad has columns.
            if (k.repeat || degree >= columnsOf(tuningOf(ctx.studio())))
                return true;
            if (k.down) {
                if (byKey(k.key))
                    return true;
                Held *h = freeHeld();
                if (!h)
                    return true;
                *h = Held{};
                h->key = k.key;
                h->role = Held::Chord;
                h->t0 = t;
                h->degree = degree;
                h->colour = colour;
                h->pressure = 0.55f;
                h->style = m_style;
                h->order = ++m_order;
                h->cmd = nextId();
                startChord(*h, 0.8f, t, ctx);
            } else if (Held *h = byKey(k.key)) {
                lift(*h, t, ctx);
            }
            return true;
        }
        if (!k.down)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            if (shift) {
                ctx.clear(slot);
                ctx.notice(QStringLiteral("Slot %1 cleared").arg(slot + 1));
            } else {
                ctx.launch(slot);
            }
            return true;
        }
        switch (k.key) {
        case Qt::Key_Backspace:
            ctx.stop();
            return true;
        // The keys every part page shares (docs/PARTS.md 4b): Return holds,
        // Shift+Return overdubs, Delete lets go.
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (shift)
                toggleOverdub(ctx);
            else
                setHold(!m_hold, ctx, t);
            return true;
        case Qt::Key_Delete:
            letGo(t, ctx);
            return true;
        // Shift+← → steps the gate pattern ([ ] need AltGr on a Swedish
        // keyboard, so they are only second keys); ← → alone, the sound.
        case Qt::Key_Left:
        case Qt::Key_Right:
            if (shift) {
                stepPattern(k.key == Qt::Key_Right ? 1 : -1, ctx);
                return true;
            }
            setStyle((m_style + (k.key == Qt::Key_Right ? 1 : StyleCount - 1)) % StyleCount, ctx);
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            stepRegister(k.key == Qt::Key_Up ? 1 : -1, t, ctx);
            return true;
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            stepPattern(k.key == Qt::Key_BracketRight ? 1 : -1, ctx);
            return true;
        // - and + for the filter (+ is a key of its own on a Swedish
        // keyboard; = stays as a second key for US ones).
        case Qt::Key_Minus:
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            stepFilter(k.key == Qt::Key_Minus ? -1 : 1, t, ctx);
            return true;
        // Second keys: L holds as it always has (the tutorial says so), and
        // P loops the last whole bars, which was Return's job before Space
        // took it over on every page.
        case Qt::Key_L:
            setHold(!m_hold, ctx, t);
            return true;
        case Qt::Key_P:
            requestRetro(t, ctx);
            return true;
        default:
            return false;
        }
    }

    void leave(ControlContext &ctx) override {
        const double t = ctx.now();
        for (auto &h : m_held)
            if (h.touch >= 0 || h.key != 0) {
                if (h.role == Held::Chord)
                    sendOff(h.cmd, std::max(t, h.onAt), ctx);
                h = Held{};
            }
        if (m_latched != 0) {
            sendOff(m_latched, t, ctx);
            m_latched = 0;
            m_latchedOrder = 0;
        }
        if (m_override >= 0) {
            m_override = -1;
            sendParam(ParamOverride, 0, -1.0f, ctx);
        }
        if (m_rate != 1) {
            m_rate = 1;
            sendParam(ParamRate, 0, 1.0f, ctx);
        }
        m_frame = Frame{};
    }

    // A new song (Ctrl+N, or one opened): what belonged to the old one goes.
    // The studio has silenced the chord hold kept ringing and turned
    // overdub off, a P still waiting for its bar line would loop the old
    // song's chords into the new one, and the progression shown is the old
    // song's. The sound and its gates are the player's and stay.
    void startOver(ControlContext &ctx) override {
        (void)ctx;
        m_latched = 0;
        m_latchedOrder = 0;
        m_overdub = false;
        m_retroEnd = -1;
        m_onsetCount = 0;
        m_landedSlot = -1;
        m_ribbonCount = 0;
    }

    // The song keeps the sound chosen, the voicing, whether hold is on, and
    // each sound's own gate, gate length, swing and filter: a loop keeps the
    // sound it was played with, so every sound's rhythm is part of the song,
    // not only the one showing. Sounds and gates are saved by name, so a
    // list that grows or is reordered still finds them. A chord hold keeps
    // ringing is not saved (it is playing, not a setting), nor the rail held
    // over everything or its double time (fingers on the rail), nor overdub
    // (the studio starts an opened song with nothing recording).
    void save(QJsonObject &out) const override {
        out[QStringLiteral("sound")] = QString::fromUtf8(styleDef(m_style).name);
        out[QStringLiteral("voicing")] = m_register;
        out[QStringLiteral("hold")] = m_hold;
        QJsonObject sounds;
        for (int s = 0; s < StyleCount; ++s) {
            QJsonObject o;
            o[QStringLiteral("gate")] = QString::fromUtf8(gateDef(m_pattern[s]).name);
            o[QStringLiteral("length")] = tidyFloat(m_length[s]);
            o[QStringLiteral("swing")] = tidyFloat(m_swing[s]);
            o[QStringLiteral("filter")] = tidyFloat(m_filter[s]);
            sounds[QString::fromUtf8(styleDef(s).name)] = o;
        }
        out[QStringLiteral("sounds")] = sounds;
    }

    // Everything starts as a new page has it, so what the file leaves out
    // (an older song, a hand edit) is the default, never the song open
    // before. Then each sound's gate goes to the engine as the rail sends
    // it; the sound, the voicing and the filter travel in every chord.
    void load(const QJsonObject &in, ControlContext &ctx) override {
        auto number = [](const QJsonObject &o, const char *key, double lo, double hi, double fallback) {
            const QJsonValue v = o.value(QLatin1String(key));
            return v.isDouble() && std::isfinite(v.toDouble()) ? std::clamp(v.toDouble(), lo, hi) : fallback;
        };
        auto named = [](const QJsonValue &v, int count, const char *(*name)(int), int fallback) {
            for (int i = 0; v.isString() && i < count; ++i)
                if (v.toString().compare(QString::fromUtf8(name(i)), Qt::CaseInsensitive) == 0)
                    return i;
            return fallback;
        };
        const auto styleName = [](int s) { return styleDef(s).name; };
        const auto gateName = [](int p) { return gateDef(p).name; };
        m_style = named(in.value(QStringLiteral("sound")), StyleCount, styleName, Pad);
        m_register = int(std::lround(number(in, "voicing", -3, 3, 0)));
        const QJsonValue hold = in.value(QStringLiteral("hold"));
        m_hold = hold.isBool() && hold.toBool();
        const QJsonObject sounds = in.value(QStringLiteral("sounds")).toObject();
        for (int s = 0; s < StyleCount; ++s) {
            const QJsonObject o = sounds.value(QString::fromUtf8(styleDef(s).name)).toObject();
            m_pattern[s] = named(o.value(QStringLiteral("gate")), PatternCount, gateName, styleDef(s).pattern);
            m_length[s] = float(number(o, "length", 0.2, 1.0, 0.55));
            m_swing[s] = float(number(o, "swing", 0.0, 0.4, 0.0));
            m_filter[s] = float(number(o, "filter", 0.0, 1.0, 0.5));
            sendParam(ParamPattern, s, float(m_pattern[s]), ctx);
            sendParam(ParamLength, s, m_length[s], ctx);
            sendParam(ParamSwing, s, m_swing[s], ctx);
        }
    }

    // The mouse on the band under the pad: the clip slots, the sounds, and
    // a chip for every setting a key reaches.
    enum Spot {
        SpotStyle = 100,
        SpotHold = 200,
        SpotLetGo,
        SpotGateDown,
        SpotGateUp,
        SpotVoicingDown,
        SpotVoicingUp,
        SpotDarker,
        SpotBrighter,
        SpotOverdub,
    };
    bool click(QPointF at, ControlContext &ctx) override {
        const int id = m_spots.at(at);
        if (id < 0)
            return false;
        if (console::clickClip(id, ctx.studio().parts[ctx.part()], ctx))
            return true;
        const double t = ctx.now();
        if (id >= SpotStyle && id < int(SpotStyle) + int(StyleCount)) {
            if (id - SpotStyle != m_style)
                setStyle(id - SpotStyle, ctx);
            return true;
        }
        switch (id) {
        case SpotHold: setHold(!m_hold, ctx, t); return true;
        case SpotLetGo: letGo(t, ctx); return true;
        case SpotGateDown: stepPattern(-1, ctx); return true;
        case SpotGateUp: stepPattern(1, ctx); return true;
        case SpotVoicingDown: stepRegister(-1, t, ctx); return true;
        case SpotVoicingUp: stepRegister(1, t, ctx); return true;
        case SpotDarker: stepFilter(-1, t, ctx); return true;
        case SpotBrighter: stepFilter(1, t, ctx); return true;
        case SpotOverdub: toggleOverdub(ctx); return true;
        default: return false;
        }
    }

    void paint(QPainter &p, const PaintContext &pc) override;
    QString help() const override;
    QString status() const override;
    // For the tutorial: chords ever played by hand, and which columns
    // (degrees, one bit each); whether hold is on.
    float tutorialProbe(const QString &name) const override {
        if (name == QLatin1String("chords"))
            return float(m_chordsPlayed);
        if (name == QLatin1String("columns"))
            return float(m_columnsPlayed);
        if (name == QLatin1String("hold"))
            return m_hold ? 1.0f : 0.0f;
        return PartControl::tutorialProbe(name);
    }

private:
    struct Held {
        enum Role : uint8_t { Chord, Rail };
        int touch = -1; // the finger, or −1
        int key = 0;    // the key, or 0
        Role role = Chord;
        int cmd = 0;    // the chord's command id (never the touch id: the pad reuses those)
        double t0 = 0;
        int degree = 0, colour = Triad, style = Pad, order = 0;
        float angle0 = 0, filter0 = 0.5f, swing0 = 0;
        float pressure = 0.5f, big = 0;
        double lastMove = 0;
        double onAt = 0; // when its chord sounds (a hair later than the touch, if it was early)
        bool moved = false;
        int cell = 0;
    };

    // --- commands -------------------------------------------------------------

    int nextId() {
        m_nextId = m_nextId % 0xFFFFF + 1;
        return m_nextId;
    }

    void startChord(Held &h, float velocity, double t, ControlContext &ctx) {
        // A chord landed a hair before a beat (up to 30 ms) is meant for that
        // beat: it sounds there, and a loop taken from that beat keeps it.
        // Only while a beat can be heard (a loop playing, the metronome):
        // in free play the player's own timing is the music.
        const StudioView &sv = ctx.studio();
        bool audible = sv.metronome;
        for (int p = 0; p < sv.partCount; ++p)
            audible |= sv.parts[p].playing >= 0;
        const double beat = ctx.beatAt(t), whole = std::round(beat);
        if (audible && whole > beat && ctx.timeAt(whole) - t < 0.03)
            t = ctx.timeAt(whole);
        h.onAt = t;
        Command on;
        on.type = Command::On;
        on.id = h.cmd;
        on.t = t;
        on.i[0] = h.degree;
        on.i[1] = h.colour;
        on.i[2] = h.style;
        on.i[3] = m_register;
        on.f[0] = velocity;
        on.f[1] = h.pressure;
        on.f[2] = h.big;
        on.f[3] = m_filter[h.style];
        ctx.send(on);
        h.filter0 = m_filter[h.style];
        h.lastMove = t;
        m_heard = true;
        ++m_chordsPlayed;
        m_columnsPlayed |= 1u << std::clamp(h.degree, 0, 15);
        // In hold mode the chord this one replaces lets go now.
        if (m_latched != 0 && m_latched != h.cmd) {
            sendOff(m_latched, t, ctx);
            m_latched = 0;
            m_latchedOrder = 0;
        }
        m_onsets[m_onsetHead] = ctx.beatAt(t);
        m_onsetHead = (m_onsetHead + 1) % kOnsets;
        m_onsetCount = std::min(m_onsetCount + 1, kOnsets);
    }

    void sendMove(Held &h, double t, ControlContext &ctx) {
        Command mv;
        mv.type = Command::Move;
        mv.id = h.cmd;
        mv.t = std::max(t, h.onAt);
        mv.i[1] = h.colour;
        mv.i[3] = m_register;
        mv.f[1] = h.pressure;
        mv.f[2] = h.big;
        mv.f[3] = m_filter[h.style];
        ctx.send(mv);
        h.lastMove = t;
    }

    void sendOff(int cmd, double t, ControlContext &ctx) {
        Command off;
        off.type = Command::Off;
        off.id = cmd;
        off.t = t;
        ctx.send(off);
    }

    // Rhythm settings are the page's, not the performance's: they never go
    // into a clip.
    void sendParam(int which, int style, float value, ControlContext &ctx) {
        Command c;
        c.type = Command::Param;
        c.flags = Command::NoRecord;
        c.i[0] = which;
        c.i[1] = style;
        c.f[0] = value;
        ctx.send(c);
    }

    // --- fingers --------------------------------------------------------------

    Held *byTouch(int id) {
        for (auto &h : m_held)
            if (h.touch == id && id >= 0)
                return &h;
        return nullptr;
    }
    Held *byKey(int key) {
        for (auto &h : m_held)
            if (h.key == key && key != 0)
                return &h;
        return nullptr;
    }
    Held *freeHeld() {
        for (auto &h : m_held)
            if (h.touch < 0 && h.key == 0)
                return &h;
        return nullptr;
    }
    int railFingers() const {
        int n = 0;
        for (const auto &h : m_held)
            n += h.touch >= 0 && h.role == Held::Rail;
        return n;
    }
    // The rail finger that chooses the pattern: the first one down.
    const Held *leadRail() const {
        const Held *lead = nullptr;
        for (const auto &h : m_held)
            if (h.touch >= 0 && h.role == Held::Rail && (!lead || h.order < lead->order))
                lead = &h;
        return lead;
    }

    static float velocityOf(const Touch &t) { return std::clamp(0.5f + 0.6f * t.pressure, 0.35f, 1.0f); }
    static float bigOf(const Touch &t) { return std::clamp((t.size - 1.15f) / 0.6f, 0.0f, 1.0f); }

    // How far a finger has turned since it landed, −1..1 for a quarter turn
    // either way. The first few degrees count for nothing, so a finger that
    // only rests never wobbles what it turns.
    static float twist(const Touch &tc, float from) {
        float d = tc.angle - from;
        while (d > float(M_PI_2))
            d -= float(M_PI);
        while (d < -float(M_PI_2))
            d += float(M_PI);
        const float u = d / float(M_PI_2), dead = 0.08f;
        return std::fabs(u) < dead ? 0.0f : (u - std::copysign(dead, u)) / (1.0f - dead);
    }

    void land(const Touch &tc, double t, const Tuning &tuning, ControlContext &ctx) {
        Held *h = freeHeld();
        if (!h)
            return;
        *h = Held{};
        h->touch = tc.id;
        h->t0 = t;
        h->order = ++m_order;
        h->angle0 = tc.angle;
        if (tc.y >= kRail) {
            h->role = Held::Rail;
            h->cell = railCellAt(tc.x);
            h->swing0 = m_swing[m_style];
            if (railFingers() == 1) {
                m_override = h->cell;
                sendParam(ParamOverride, 0, float(h->cell), ctx);
                setLength(lengthAt(tc.y), ctx);
            }
            updateRate(ctx);
            return;
        }
        const int cols = columnsOf(tuning);
        h->role = Held::Chord;
        h->degree = std::clamp(int(tc.x * float(cols)), 0, cols - 1);
        h->colour = rowAt(tc.y);
        h->style = m_style;
        h->pressure = tc.pressure;
        h->big = bigOf(tc);
        h->cmd = nextId();
        startChord(*h, velocityOf(tc), t, ctx);
    }

    void moveChord(Held &h, const Touch &tc, double t, ControlContext &ctx) {
        bool send = false, now = false;
        // The colour follows the finger up and down, a quarter of a row past
        // each edge so a wobble never flips it.
        const float rowPos = tc.y / kRowHeight;
        if (rowPos < float(h.colour) - 0.25f || rowPos > float(h.colour + 1) + 0.25f) {
            const int row = rowAt(std::min(tc.y, kRail - 0.001f));
            if (row != h.colour) {
                h.colour = row;
                send = now = true;
            }
        }
        // Twist turns the filter, a knob under the finger, from where it was.
        if (tc.eccentricity > 0.2f) {
            const float f = std::clamp(h.filter0 + twist(tc, h.angle0) * 0.6f, 0.0f, 1.0f);
            if (std::fabs(f - m_filter[h.style]) > 0.005f) {
                m_filter[h.style] = f;
                send = true;
            }
        }
        const float big = bigOf(tc);
        if (std::fabs(tc.pressure - h.pressure) > 0.01f || std::fabs(big - h.big) > 0.01f) {
            h.pressure = tc.pressure;
            h.big = big;
            send = true;
        }
        if (send && (now || t - h.lastMove >= 1.0 / 60.0))
            sendMove(h, t, ctx);
    }

    // --- loops ----------------------------------------------------------------
    //
    // A loop is always something the player asks for, and the page says what
    // it caught. Space is the studio's: the session finds the phrase (one
    // cycle of what was repeated, or the whole phrase in whole bars) and
    // loops it at once, setting the tempo when nothing else plays; the page
    // announces it as it lands. P is this page's own: the last whole bars at
    // the tempo already running, taken at the bar line, with the page
    // counting down to it.

    // Space ends a take a beat after the last onset it heard, which would
    // cut a chord held across a bar short. While a beat can be heard and a
    // chord is held (by a finger, a key or hold), each beat sends a marker
    // that says the playing goes on: an On flagged Continue (never an
    // onset of its own), closed at once, which the engine ignores.
    void carryOn(double t, ControlContext &ctx) {
        const StudioView &sv = ctx.studio();
        bool audible = sv.metronome;
        for (int p = 0; p < sv.partCount; ++p)
            audible |= sv.parts[p].playing >= 0;
        bool held = m_latched != 0;
        for (const auto &h : m_held)
            held |= (h.touch >= 0 || h.key != 0) && h.role == Held::Chord;
        const double beat = std::floor(ctx.beatAt(t));
        if (!audible || !held || beat <= m_carriedBeat) {
            if (!held)
                m_carriedBeat = beat;
            return;
        }
        m_carriedBeat = beat;
        Command on;
        on.type = Command::On;
        on.flags = Command::Continue;
        on.id = kCarryOnId;
        on.t = t;
        ctx.send(on);
        Command off;
        off.type = Command::Off;
        off.id = kCarryOnId;
        off.t = t;
        ctx.send(off);
    }

    // What is about to happen with loops, or what just did, for the screen
    // and the status line; empty when there is nothing to say. *waiting
    // says whether something is still to come.
    QString loopCue(double beat, const PartView &v, bool *waiting = nullptr) const {
        if (waiting)
            *waiting = m_retroEnd >= 0 || v.recording >= 0;
        auto beats = [](double n) {
            const int b = std::max(1, int(std::ceil(n - 1e-6)));
            return QStringLiteral("%1 BEAT%2").arg(b).arg(b > 1 ? "S" : "");
        };
        auto bars = [](double lengthBeats) {
            const int b = std::max(1, int(std::lround(lengthBeats / kBeatsPerBar)));
            return QStringLiteral("%1 BAR%2").arg(b).arg(b > 1 ? "S" : "");
        };
        if (m_retroEnd >= 0)
            return QStringLiteral("LOOPING THE LAST %1 IN %2 · P CALLS IT OFF")
                .arg(bars(m_retroBars * kBeatsPerBar), beats(m_retroEnd - beat));
        if (v.recording >= 0) {
            const double at = v.takeBeats > 0 ? v.takeAt : std::ceil(beat / kBeatsPerBar - 1e-9) * kBeatsPerBar;
            const QString what = v.takeBeats > 0 ? bars(v.takeBeats) : QStringLiteral("WHAT YOU PLAYED");
            return QStringLiteral("LOOPING %1 IN %2 · SPACE CALLS IT OFF").arg(what, beats(at - beat));
        }
        if (v.queued >= 0 && v.queued == v.playing)
            return QStringLiteral("THE LOOP STARTS FROM THE TOP IN %1")
                .arg(beats(std::ceil(beat / kBeatsPerBar - 1e-9) * kBeatsPerBar - beat));
        if (m_landedSlot >= 0 && beat - m_landedAt < kBeatsPerBar && v.playing == m_landedSlot)
            return QStringLiteral("LOOPED %1 · SLOT %2").arg(bars(m_landedBeats)).arg(m_landedSlot + 1);
        return {};
    }

    void moveRail(Held &h, const Touch &tc, ControlContext &ctx) {
        if (&h != leadRail())
            return;
        const float cellPos = tc.x * float(PatternCount);
        if (cellPos < float(h.cell) - 0.2f || cellPos > float(h.cell + 1) + 0.2f) {
            const int cell = railCellAt(tc.x);
            if (cell != h.cell) {
                h.cell = cell;
                h.moved = true;
                m_override = cell;
                sendParam(ParamOverride, 0, float(cell), ctx);
            }
        }
        const float len = lengthAt(tc.y);
        if (std::fabs(len - m_length[m_style]) > 0.02f)
            setLength(len, ctx);
        if (tc.eccentricity > 0.2f) {
            const float sw = std::clamp(h.swing0 + twist(tc, h.angle0) * 0.4f, 0.0f, 0.4f);
            if (std::fabs(sw - m_swing[m_style]) > 0.01f) {
                m_swing[m_style] = sw;
                sendParam(ParamSwing, m_style, sw, ctx);
            }
        }
    }

    static float lengthAt(float y) { return std::clamp(0.2f + 0.8f * (y - kRail) / (1.0f - kRail), 0.2f, 1.0f); }

    void lift(Held &h, double t, ControlContext &ctx) {
        if (h.role == Held::Rail) {
            const bool lead = &h == leadRail();
            const bool tap = lead && !h.moved && t - h.t0 < 0.3;
            const int cell = h.cell;
            h = Held{};
            if (lead) {
                if (tap)
                    setPattern(m_pattern[m_style] == cell && cell != Hold ? Hold : cell, ctx);
                const Held *next = leadRail();
                m_override = next ? next->cell : -1;
                sendParam(ParamOverride, 0, float(m_override), ctx);
            }
            updateRate(ctx);
            return;
        }
        // In hold mode the newest chord keeps ringing after the finger lifts.
        bool newest = true;
        for (const auto &o : m_held)
            if (&o != &h && (o.touch >= 0 || o.key != 0) && o.role == Held::Chord && o.order > h.order)
                newest = false;
        if (m_hold && newest && m_latchedOrder < h.order) {
            if (m_latched != 0)
                sendOff(m_latched, t, ctx);
            m_latched = h.cmd;
            m_latchedOrder = h.order;
        } else {
            sendOff(h.cmd, std::max(t, h.onAt), ctx);
        }
        h = Held{};
    }

    void updateRate(ControlContext &ctx) {
        const int n = railFingers();
        const int rate = n >= 3 ? 4 : n == 2 ? 2 : 1;
        if (rate != m_rate) {
            m_rate = rate;
            sendParam(ParamRate, 0, float(rate), ctx);
        }
    }

    // --- settings -------------------------------------------------------------

    void setHold(bool on, ControlContext &ctx, double t) {
        m_hold = on;
        if (!on && m_latched != 0) {
            sendOff(m_latched, t, ctx);
            m_latched = 0;
            m_latchedOrder = 0;
        }
        ctx.notice(on ? QStringLiteral("Hold: chords ring until the next one (Delete lets go)")
                      : QStringLiteral("Hold off"));
    }

    // Delete: the chord that hold keeps ringing stops; hold stays on for
    // the next one.
    void letGo(double t, ControlContext &ctx) {
        if (m_latched == 0) {
            ctx.notice(m_hold ? QStringLiteral("Nothing held yet: the next chord rings until another")
                              : QStringLiteral("Nothing held: Return holds chords"));
            return;
        }
        sendOff(m_latched, t, ctx);
        m_latched = 0;
        m_latchedOrder = 0;
        ctx.notice(QStringLiteral("Let go"));
    }

    void toggleOverdub(ControlContext &ctx) {
        m_overdub = !m_overdub;
        ctx.overdub(m_overdub);
        ctx.notice(m_overdub ? QStringLiteral("Overdub: what you play joins the loop") : QStringLiteral("Overdub off"));
    }

    void stepPattern(int dir, ControlContext &ctx) {
        setPattern((m_pattern[m_style] + PatternCount + dir) % PatternCount, ctx);
    }

    void stepRegister(int dir, double t, ControlContext &ctx) {
        setRegister(std::clamp(m_register + dir, -3, 3), t, ctx);
    }

    void stepFilter(int dir, double t, ControlContext &ctx) {
        setFilter(m_filter[m_style] + 0.05f * float(dir), t, ctx);
        ctx.notice(QStringLiteral("%1 filter %2%")
                       .arg(QString::fromUtf8(styleDef(m_style).name))
                       .arg(int(std::lround(m_filter[m_style] * 100))));
    }

    void setStyle(int s, ControlContext &ctx) {
        m_style = s;
        const StyleDef &sd = styleDef(s);
        ctx.notice(QStringLiteral("%1 — %2, %3 gate")
                       .arg(QString::fromUtf8(sd.name), QString::fromUtf8(sd.blurb),
                            QString::fromUtf8(gateDef(m_pattern[s]).name).toLower()));
    }

    void setPattern(int p, ControlContext &ctx) {
        m_pattern[m_style] = p;
        sendParam(ParamPattern, m_style, float(p), ctx);
        ctx.notice(QStringLiteral("%1 gate: %2")
                       .arg(QString::fromUtf8(styleDef(m_style).name), QString::fromUtf8(gateDef(p).name)));
    }

    void setLength(float len, ControlContext &ctx) {
        m_length[m_style] = len;
        sendParam(ParamLength, m_style, len, ctx);
    }

    void setFilter(float f, double t, ControlContext &ctx) {
        m_filter[m_style] = std::clamp(f, 0.0f, 1.0f);
        for (auto &h : m_held)
            if ((h.touch >= 0 || h.key != 0) && h.role == Held::Chord && h.style == m_style)
                sendMove(h, t, ctx);
    }

    // The voicing's register: every step moves the chord's centre by about a
    // third, so the voice leading picks the next inversion up or down.
    void setRegister(int r, double t, ControlContext &ctx) {
        if (r == m_register)
            return;
        m_register = r;
        for (auto &h : m_held)
            if ((h.touch >= 0 || h.key != 0) && h.role == Held::Chord)
                sendMove(h, t, ctx);
        ctx.notice(QStringLiteral("Voicing %1%2").arg(r > 0 ? "+" : "").arg(r));
    }

    // --- keys -----------------------------------------------------------------

    // Three rows of chord keys: Z row sus, A row triads, Q row add9. Shift
    // makes sevenths, or power chords on the Z row.
    static void chordKey(int key, bool shift, int *degree, int *colour) {
        static const int rows[3][8] = {
            {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_N, Qt::Key_M, Qt::Key_Comma},
            {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H, Qt::Key_J, Qt::Key_K},
            {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I},
        };
        static const int colours[3][2] = {{Sus, Power}, {Triad, Seventh}, {Add9, Seventh}};
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 8; ++k)
                if (rows[r][k] == key) {
                    *degree = k;
                    *colour = colours[r][shift ? 1 : 0];
                    return;
                }
    }

    // P: the last whole bars played become a loop, at the tempo already
    // running. Pressed in the first beat of a bar it takes the bars up to
    // that bar line; later, it waits for the next one, so the bar being
    // played is in. P again while it waits calls it off, and it stands back
    // for a loop Space has already set going.
    void requestRetro(double t, ControlContext &ctx) {
        if (m_retroEnd >= 0) {
            m_retroEnd = -1;
            ctx.notice(QStringLiteral("Loop called off"));
            return;
        }
        if (ctx.studio().parts[ctx.part()].recording >= 0) {
            ctx.notice(QStringLiteral("A loop is already waiting for the bar line: Space calls it off"));
            return;
        }
        const double beat = ctx.beatAt(t);
        const double bar = std::floor(beat / kBeatsPerBar) * kBeatsPerBar;
        if (beat - bar < 1.0) {
            retroCapture(bar, ctx);
            return;
        }
        const int bars = retroBars(bar + kBeatsPerBar);
        if (bars == 0) {
            ctx.notice(QStringLiteral("Play some chords first, then P loops them"));
            return;
        }
        m_retroEnd = bar + kBeatsPerBar;
        m_retroBars = bars;
        ctx.notice(QStringLiteral("Looping the last %1 bar%2 at the bar line").arg(bars).arg(bars > 1 ? "s" : ""));
    }

    // How many bars a loop ending at endBeat should hold: the passage back to
    // the first of a run of onsets no more than two bars apart, as 1, 2, 4
    // or 8 bars; 0 when nothing was played.
    int retroBars(double endBeat) const {
        double first = endBeat;
        bool any = false;
        for (int k = 0; k < m_onsetCount; ++k) {
            const double b = m_onsets[(m_onsetHead - 1 - k + 2 * kOnsets) % kOnsets];
            if (b >= endBeat - 0.125)
                continue;
            if (b < first - 2.0 * kBeatsPerBar)
                break;
            first = b;
            any = true;
        }
        if (!any)
            return 0;
        // A chord landed a hair before its bar belongs to that bar.
        const double played = (endBeat - std::floor((first + 0.125) / kBeatsPerBar) * kBeatsPerBar) / kBeatsPerBar;
        int bars = 1;
        while (bars * 2 <= played + 0.01 && bars < 8)
            bars *= 2;
        return bars;
    }

    // The take goes through the studio exactly as Space's does: a chord a
    // hair early for its bar belongs to it, chords held across the start
    // are carried in, and a take whose bar line has just passed sounds
    // what it holds at once.
    void retroCapture(double endBeat, ControlContext &ctx) {
        const int bars = retroBars(endBeat);
        if (bars == 0) {
            ctx.notice(QStringLiteral("Play some chords first, then P loops them"));
            return;
        }
        const int slot = ctx.freeSlot();
        if (slot < 0) {
            ctx.notice(QStringLiteral("Every slot is full: Shift+F1–F8 clears one"));
            return;
        }
        ctx.take(slot, endBeat - bars * kBeatsPerBar, endBeat);
        ctx.notice(QStringLiteral("The last %1 bar%2 loop in slot %3").arg(bars).arg(bars > 1 ? "s" : "").arg(slot + 1));
        m_onsetCount = 0;
    }

    // --- drawing --------------------------------------------------------------

    void paintRail(QPainter &p, const PaintContext &pc, const float *d);
    void paintKeys(QPainter &p, const QRectF &r, const PaintContext &pc, const float *d);

    std::array<Held, kHeldMax> m_held{};
    double m_beatNow = 0;
    PartView m_take; // this part as the studio last showed it: a take waiting, a loop queued
    double m_carriedBeat = 0; // the last beat a carry-on marker went out for
    uint32_t m_seenTakes = 0;
    bool m_seenTakesValid = false;
    double m_landedAt = -1e9; // the beat the last loop landed, its slot and length
    int m_landedSlot = -1;
    float m_landedBeats = 0;
    int m_retroBars = 0;
    Frame m_frame;
    console::Hotspots m_spots; // what paint() drew to be clicked
    bool m_button = false, m_heard = false;
    int m_nextId = 0, m_order = 0;

    int m_style = Pad;
    int m_register = 0;
    int m_pattern[StyleCount] = {};
    float m_length[StyleCount] = {};
    float m_swing[StyleCount] = {};
    float m_filter[StyleCount] = {};
    int m_override = -1, m_rate = 1;
    bool m_hold = false, m_overdub = false;
    int m_latched = 0, m_latchedOrder = 0;
    uint32_t m_chordsPlayed = 0, m_columnsPlayed = 0; // for the tutorial

    static constexpr int kOnsets = 128;
    double m_onsets[kOnsets] = {};
    int m_onsetHead = 0, m_onsetCount = 0;
    double m_retroEnd = -1;

    static constexpr int kRibbon = 8;
    Shape m_ribbon[kRibbon];
    int m_ribbonHead = 0, m_ribbonCount = 0;
    uint32_t m_seenSeq = 0;
};

// --- painting -------------------------------------------------------------------

void Control::paint(QPainter &p, const PaintContext &pc) {
    namespace c = console;
    const QColor col = pc.color;
    const float *d = pc.view->data;
    const Tuning t = tuningOf(*pc.studio);
    const int cols = columnsOf(t);
    const int per = std::max(1, t.degreesPerOctave());
    c::pad(p, pc, QStringLiteral("Chords"));

    // Which chord to show: the one held live, else the loop's.
    const int lane = d[pub::Degree] >= 0 ? kLiveLane : d[pub::LaneStride + pub::Degree] >= 0 ? kReplayLane : -1;
    const float *ld = lane >= 0 ? d + lane * pub::LaneStride : nullptr;
    const int showDegree = ld ? int(ld[pub::Degree]) : -1;
    const int showColumn = ld ? ((showDegree % per) + per) % per : -1;
    const int showColour = ld ? int(ld[pub::Colour]) : 0;
    const float level = ld ? ld[pub::Level] : 0.0f;
    const QRectF field = pc.rect(0, 0, 1, kRail);

    // The chord's name, huge and faint behind the grid, breathing with it.
    if (ld) {
        chordText(p, field, chordName(t, showDegree, showColour), c::alpha(col, 0.06f + 0.12f * level),
                  int(field.height() * 0.36));
    }

    // Columns lit by what sounds in them: the fingers' bright, a loop's softer.
    for (int k = 0; k < cols; ++k) {
        const float live = d[pub::Columns + k], loop = d[pub::Columns + 8 + k];
        const QRectF colRect = pc.rect(float(k) / cols, 0, float(k + 1) / cols, kRail);
        if (k % 2 == 1)
            p.fillRect(colRect, c::alpha(col, 0.03f));
        if (loop > 0.01f)
            p.fillRect(colRect, c::alpha(col, 0.05f + 0.15f * loop));
        if (live > 0.01f)
            p.fillRect(colRect, c::alpha(col, 0.08f + 0.28f * live));
    }

    // Cells: each chord's name where it lives; the one sounding lit.
    const qreal cellH = pc.pad.height() * kRowHeight;
    for (int k = 0; k < cols; ++k) {
        for (int r = 0; r < ColourCount; ++r) {
            const QRectF cell = pc.rect(float(k) / cols, r * kRowHeight, float(k + 1) / cols, (r + 1) * kRowHeight);
            const QString name = chordName(t, k, r);
            if (k == showColumn && r == showColour) {
                p.fillRect(cell.adjusted(3, 3, -3, -3), c::alpha(col, 0.4f + 0.5f * level));
                chordText(p, cell, name, QColor::fromRgb(c::kInk), int(std::clamp(cellH * 0.21, 12.0, 19.0)));
            } else {
                chordText(p, cell, name, c::alpha(col, r == Triad ? 0.85f : 0.55f),
                          int(std::clamp(cellH * 0.17, 11.0, 16.0)), r == Triad);
            }
        }
        // The numeral heads its column.
        const QRectF head = pc.rect(float(k) / cols, kRail - kRowHeight * 0.34f, float(k + 1) / cols, kRail);
        c::text(p, head, roman(t, k), c::alpha(col, k == showColumn ? 1.0f : 0.75f),
                int(std::clamp(cellH * 0.19, 11.0, 17.0)), true, Qt::AlignCenter);
    }
    // The rows' names, down the left edge.
    for (int r = 0; r < ColourCount; ++r) {
        const QPointF at = pc.at(0, r * kRowHeight);
        c::text(p, QRectF(at.x() + 6, at.y() - 16, 60, 13), QString::fromUtf8(colourName(r)), c::alpha(col, 0.45f), 9,
                true);
    }
    c::grid(p, pc, cols, 1, col, 0.22f);
    p.setPen(QPen(c::alpha(col, 0.12f), 1));
    for (int r = 1; r < ColourCount; ++r) {
        const qreal y = pc.at(0, r * kRowHeight).y();
        p.drawLine(QPointF(pc.pad.left(), y), QPointF(pc.pad.right(), y));
    }

    paintRail(p, pc, d);

    // Along the top edge: the sound, hold, the register, overdub.
    qreal tagsRight = pc.pad.left() + 110;
    {
        qreal &x = tagsRight;
        const qreal y = pc.pad.top();
        x = c::tag(p, QPointF(x, y), QString::fromUtf8(styleDef(m_style).name), col, 12).right() + 8;
        if (m_hold)
            x = c::tag(p, QPointF(x, y), QStringLiteral("HOLD"), c::amber(), 12).right() + 8;
        if (m_register != 0)
            x = c::tag(p, QPointF(x, y), QStringLiteral("VOICING %1%2").arg(m_register > 0 ? "+" : "").arg(m_register),
                       c::alpha(col, 0.8f), 12)
                    .right() +
                8;
        if (m_overdub)
            x = c::tag(p, QPointF(x, y), QStringLiteral("OVERDUB"), c::red(), 12).right() + 8;
    }

    // The progression so far along the top edge, newest at the right, just
    // left of the beat lights and right of the tags.
    if (m_ribbonCount > 0) {
        QFont f = c::font(11, true);
        f.setCapitalization(QFont::MixedCase);
        const QFontMetricsF fm(f);
        qreal x = pc.pad.right() - 16 - 4 * 14 - 3 * 6 - 14;
        const qreal y = pc.pad.top();
        for (int i = m_ribbonCount - 1; i >= 0; --i) {
            const Shape &s = m_ribbon[(m_ribbonHead - m_ribbonCount + i + 2 * kRibbon) % kRibbon];
            const QString name = chordName(t, s);
            const qreal w = console::textWidth(f, name) + 12;
            if (x - w < std::max(pc.pad.left() + pc.pad.width() * 0.42, tagsRight))
                break;
            x -= w;
            const bool last = i == m_ribbonCount - 1;
            p.fillRect(QRectF(x - 3, y - 10, w + 6, 20), QColor(0, 0, 0));
            nameTag(p, QPointF(x, y), name, last ? col : c::alpha(col, 0.35f + 0.08f * float(i)), 11);
            x -= 8;
        }
    }

    // A loop on its way (P's, or a take still waiting for its bar line):
    // the pad framed in red, pulsing on the beat, and what is coming in amber.
    // A loop just landed: what it holds, in the part's own colour.
    bool waiting = false;
    const QString cue = loopCue(pc.beat, *pc.view, &waiting);
    if (!cue.isEmpty()) {
        if (waiting) {
            const float pulse = 1.0f - float(pc.beat - std::floor(pc.beat));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c::alpha(c::red(), 0.45f + 0.5f * pulse), 3.0));
            p.drawRect(pc.pad.adjusted(-4, -4, 4, 4));
        }
        c::tag(p, QPointF(pc.pad.center().x(), pc.at(0, kRail).y()), cue, waiting ? c::amber() : col, 15, true);
    }

    // Fingers: a chord finger glows (its cell already names the chord), the
    // first rail finger names the rhythm it plays.
    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &tc = m_frame.touches[i];
        const Held *held = nullptr;
        for (const auto &h : m_held)
            if (h.touch == tc.id)
                held = &h;
        if (!held || held->role == Held::Rail) {
            QString label;
            if (held && held == leadRail()) {
                label = QString::fromUtf8(gateDef(held->cell).name);
                if (m_rate > 1)
                    label += QStringLiteral(" ×%1").arg(m_rate);
            }
            c::finger(p, pc, tc, col, label);
            continue;
        }
        c::finger(p, pc, tc, col, {}, 0.4f + 0.6f * level);
    }


    c::beats(p, pc, col);

    // Below the pad, every one clickable. Row 0: the clips, the sounds, and
    // the chord's notes on a keyboard; row 1: a chip for each setting a key
    // reaches, its key on it. Chips are as wide as their words need, so they
    // read on a narrow window too.
    m_spots.clear();
    const qreal gap = 4, between = 12;
    QStringList sounds;
    QList<bool> soundLit;
    QList<int> soundIds;
    for (int s = 0; s < StyleCount; ++s) {
        sounds << QString::fromUtf8(styleDef(s).name);
        soundLit << (s == m_style);
        soundIds << SpotStyle + s;
    }
    const QRectF top = c::bandRow(pc, 0);
    // Ten sounds need most of the row: the keyboard gives up a little, and
    // the sounds take what their words need and a few pixels each to
    // breathe, the clips the rest.
    const qreal keysW = top.width() * 0.16;
    const qreal soundsW = std::max(chipsWidth(sounds, gap) + 3.0 * qreal(sounds.size()), top.width() * 0.41);
    const qreal clipsW = top.width() - keysW - soundsW - 2 * between;
    c::clips(p, QRectF(top.left(), top.top(), clipsW, top.height()), *pc.view, col, pc.time, &m_spots);
    fitChips(p, pc, QRectF(top.left() + clipsW + between, top.top(), soundsW, top.height()), sounds, soundLit,
             soundIds, gap, &m_spots);
    paintKeys(p, QRectF(top.right() - keysW, top.top(), keysW, top.height()), pc, d);

    struct Setting {
        const char *name, *key;
        int id;
        bool lit;
    };
    const Setting settings[] = {
        {"HOLD", "↵", SpotHold, m_hold},
        {"LET GO", "DEL", SpotLetGo, false},
        {"GATE", "⇧←", SpotGateDown, false},
        {"GATE", "⇧→", SpotGateUp, false},
        {"VOICE", "↓", SpotVoicingDown, false},
        {"VOICE", "↑", SpotVoicingUp, false},
        {"DARK", "-", SpotDarker, false},
        {"BRIGHT", "+", SpotBrighter, false},
        {"OVERDUB", "⇧↵", SpotOverdub, m_overdub},
    };
    const QRectF bottom = c::bandRow(pc, 1);
    // On a narrow window the space between a setting and its key closes up.
    QStringList labels;
    QList<bool> lit;
    QList<int> ids;
    for (const char *sep : {"  ", " "}) {
        labels.clear();
        for (const auto &st : settings)
            labels << QString::fromUtf8(st.name) + QLatin1String(sep) + QString::fromUtf8(st.key);
        if (chipsWidth(labels, gap) <= bottom.width())
            break;
    }
    for (const auto &st : settings) {
        lit << st.lit;
        ids << st.id;
    }
    fitChips(p, pc, bottom, labels, lit, ids, gap, &m_spots);
}

// The rail: a cell per pattern, its hits drawn as a bar of steps, this
// sound's pattern outlined, the one sounding lit with its playhead.
void Control::paintRail(QPainter &p, const PaintContext &pc, const float *d) {
    namespace c = console;
    const QColor col = pc.color;
    const QRectF rail = pc.rect(0, kRail, 1, 1);
    p.fillRect(rail, c::alpha(col, 0.05f));
    p.setPen(QPen(c::alpha(col, 0.7f), 1.5));
    p.drawLine(rail.bottomLeft(), rail.bottomRight());
    const int sounding = int(d[pub::Pattern + m_style]);
    const float gate = d[pub::Gate + m_style];
    const double beatInBar = pc.beat - std::floor(pc.beat / kBeatsPerBar) * kBeatsPerBar;
    const int rate = std::max(1, int(d[pub::Rate]));
    for (int k = 0; k < PatternCount; ++k) {
        const GateDef &g = gateDef(k);
        const QRectF cell =
            pc.rect(float(k) / float(PatternCount), kRail, float(k + 1) / float(PatternCount), 1).adjusted(3, 5, -3, -4);
        const bool chosen = k == m_pattern[m_style];
        const bool live = k == sounding && k != Hold;
        if (live)
            p.fillRect(cell, c::alpha(col, 0.1f + 0.4f * gate));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(chosen ? col : c::alpha(col, 0.25f), chosen ? 2.0 : 1.0));
        p.drawRect(cell);
        c::text(p, QRectF(cell.left() + 6, cell.top() + 3, cell.width() - 8, 16), QString::fromUtf8(g.name),
                chosen || live ? col : c::alpha(col, 0.6f), int(std::clamp(cell.width() / 9.5, 9.0, 12.0)), true);
        // The steps: tall for accents, short for hits, a dot for rests.
        const qreal sw = (cell.width() - 12) / g.steps;
        const qreal top = cell.top() + cell.height() * 0.5, h = cell.height() * 0.34;
        const int now = int(std::floor(beatInBar / kBeatsPerBar * g.steps * rate)) % g.steps;
        if (k == Hold) {
            p.fillRect(QRectF(cell.left() + 6, top + h * 0.3, cell.width() - 12, h * 0.5),
                       c::alpha(col, chosen ? 0.7f : 0.3f));
            continue;
        }
        for (int s = 0; s < g.steps; ++s) {
            const char hit = g.hits[s];
            const qreal hh = hit == 'X' ? h : hit == 'x' ? h * 0.7 : h * 0.15;
            const QRectF step(cell.left() + 6 + s * sw, top + h - hh, std::max(1.0, sw - 2), hh);
            QColor sc = hit == '.' ? c::alpha(col, 0.25f) : c::alpha(col, live || chosen ? 0.85f : 0.4f);
            if (live && s == now)
                sc = QColor(255, 255, 255, 235);
            p.fillRect(step, sc);
        }
    }
}

// Three and a half octaves of keys with the chord's notes lit: the fingers'
// solid, a loop's softer.
void Control::paintKeys(QPainter &p, const QRectF &r, const PaintContext &pc, const float *d) {
    namespace c = console;
    const QColor col = pc.color;
    constexpr int lo = 45, hi = 96; // A2 .. C7
    static const bool black[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
    int whites = 0;
    for (int m = lo; m <= hi; ++m)
        whites += !black[m % 12];
    const qreal w = r.width() / whites;
    auto lit = [&](int m, int lane) {
        const float *ld = d + lane * pub::LaneStride;
        if (ld[pub::Degree] < 0)
            return false;
        for (int i = 0; i < int(ld[pub::NoteCount]) && i < 6; ++i)
            if (int(std::lround(ld[pub::Notes + i])) == m)
                return true;
        return false;
    };
    int wi = 0;
    for (int m = lo; m <= hi; ++m) {
        if (black[m % 12])
            continue;
        const QRectF key(r.left() + wi * w, r.top(), w - 1.5, r.height());
        const bool live = lit(m, kLiveLane), loop = lit(m, kReplayLane);
        p.fillRect(key, live ? col : loop ? c::alpha(col, 0.5f) : c::alpha(col, 0.1f));
        if (m % 12 == 0)
            c::text(p, QRectF(key.left(), key.bottom() - 11, key.width(), 10), QString::number(m / 12 - 1),
                    live ? QColor::fromRgb(c::kInk) : c::alpha(col, 0.6f), 8, false, Qt::AlignCenter);
        ++wi;
    }
    wi = 0;
    for (int m = lo; m <= hi; ++m) {
        if (!black[m % 12]) {
            ++wi;
            continue;
        }
        const QRectF key(r.left() + wi * w - w * 0.31, r.top(), w * 0.62, r.height() * 0.58);
        const bool live = lit(m, kLiveLane), loop = lit(m, kReplayLane);
        p.fillRect(key, QColor(0, 0, 0));
        p.fillRect(key.adjusted(1, 0, -1, -1), live ? col : loop ? c::alpha(col, 0.55f) : c::alpha(col, 0.22f));
    }
}

QString Control::help() const {
    // The keys every part page shares come first, then this page's own.
    return QStringLiteral(
        "← →\tthe sound: pad, trance, organ, piano, pluck, pump, keys (electric piano), strings (string machine), "
        "dub (stab into echo), guitar (funk, strummed)\n"
        "↑ ↓\tthe voicing higher or lower\n"
        "Shift+← →  (or [ ])\tthe gate pattern for this sound\n"
        "- +  (or =)\tthe filter darker or brighter\n"
        "Return (or L)\thold: chords ring until the next one (again: hold off)\n"
        "Delete\tlet go of the chord hold keeps ringing\n"
        "Shift+Return\toverdub on or off: what you play joins the loop\n"
        "F1–F8 · Shift+F1–F8 · Backspace\tlaunch a slot at the next bar · clear it · stop the loop\n"
        "play, then Space\tloops what you just played at once: played twice, one cycle of it\n"
        "under the pad, with the mouse\tthe clips (click: launch or stop), the sounds, and a chip for every key "
        "above: hold, let go, gate, voicing, darker, brighter, overdub\n"
        "touch a column\ta chord of the key, I to VII, voiced to flow from the last one\n"
        "the five rows\tits colour, bottom to top: power 5, sus, triad, add9, seventh\n"
        "slide up or down\tchange colour while it sounds; the voices glide\n"
        "another finger\ta new chord takes over; lift it and the one still held returns\n"
        "press harder · flat finger\tswell it · double it an octave under and over\n"
        "twist\tthe filter, a knob under the finger\n"
        "press until it clicks\thold, as Return does\n"
        "tap the rail along the top\tkeep that gate rhythm for this sound\n"
        "hold the rail\tplay that rhythm over everything, loops too, while the finger stays\n"
        "higher on the rail · twist there\tlonger hits · swing\n"
        "two, three fingers on the rail\tdouble, then quadruple time, brighter: a build-up\n"
        "A S D F G H J\tchords I to VII; Q row add9, Z row sus; Shift for sevenths (Z row: power)\n"
        "P\tloop the last whole bars at the tempo running (1, 2, 4 or 8), at the bar line; again calls it off");
}

QString Control::status() const {
    QStringList parts;
    int fingers = 0;
    for (const auto &h : m_held)
        fingers += (h.touch >= 0 || h.key != 0) && h.role == Held::Chord;
    if (fingers > 0)
        parts << QStringLiteral("%1 chord%2 held").arg(fingers).arg(fingers == 1 ? "" : "s");
    parts << QString::fromUtf8(styleDef(m_style).name);
    const int sounding = m_override >= 0 ? m_override : m_pattern[m_style];
    parts << QStringLiteral("gate %1%2")
                 .arg(QString::fromUtf8(gateDef(sounding).name))
                 .arg(m_rate > 1 ? QStringLiteral(" ×%1").arg(m_rate) : QString());
    if (m_hold)
        parts << QStringLiteral("hold");
    if (m_register != 0)
        parts << QStringLiteral("voicing %1%2").arg(m_register > 0 ? "+" : "").arg(m_register);
    const QString cue = loopCue(m_beatNow, m_take);
    parts << (cue.isEmpty() ? QStringLiteral("Space loops what you just played") : cue.toLower());
    return parts.join(QStringLiteral(" · "));
}

}

PartSpec makeChords() {
    PartSpec s;
    s.followsChords = false; // it sets the chord
    s.name = QStringLiteral("Chords");
    s.blurb = QStringLiteral("progressions, stabs, gates");
    s.color = QColor("#2EE6FF");
    s.engine = std::make_unique<chords::Engine>();
    s.control = std::make_unique<chords::Control>();
    // A little more room than most, a touch of echo, and the kick's pump.
    const float channel[ChannelCount] = {0.72f, 0.5f, 0.5f, 0.32f, 0.1f, 0.55f, 0.0f, 0.0f};
    std::copy(channel, channel + ChannelCount, s.channel);
    return s;
}

}
