#include "console.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QRegularExpression>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace tf::console {

QString keyNames(const QString &text) {
#ifdef Q_OS_MACOS
    if (!text.contains(QLatin1String("Ctrl"), Qt::CaseInsensitive))
        return text;
    static const QRegularExpression ctrl(QStringLiteral("\\bctrl\\+"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression word(QStringLiteral("\\bctrl\\b"), QRegularExpression::CaseInsensitiveOption);
    QString s = text;
    s.replace(ctrl, QStringLiteral("\u2318"));
    s.replace(word, QStringLiteral("\u2318"));
    return s;
#else
    return text;
#endif
}

void loadFonts() {
    for (const char *weight : {"Regular", "Medium", "Bold", "Black"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansMono-%1.ttf").arg(QLatin1String(weight)));
}

int unshifted(int key) {
    switch (key) {
    case Qt::Key_Less: return Qt::Key_Comma;
    case Qt::Key_Greater: return Qt::Key_Period;
    case Qt::Key_Question: return Qt::Key_Slash;
    case Qt::Key_Colon: return Qt::Key_Semicolon;
    case Qt::Key_QuoteDbl: return Qt::Key_Apostrophe;
    case Qt::Key_BraceLeft: return Qt::Key_BracketLeft;
    case Qt::Key_BraceRight: return Qt::Key_BracketRight;
    default: return key;
    }
}

QFont font(int px, bool bold) {
    QFont f("Noto Sans Mono");
    f.setPixelSize(std::max(8, px));
    f.setStretch(QFont::SemiCondensed);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    f.setWeight(bold ? QFont::Bold : QFont::DemiBold);
    return f;
}

bool isIcon(QChar c) {
    switch (c.unicode()) {
    case 0x2190: case 0x2191: case 0x2192: case 0x2193: // ← ↑ → ↓
    case 0x2194: case 0x2195:                           // ↔ ↕
    case 0x21E7:                                        // ⇧
    case 0x21B5: case 0x23CE:                           // ↵ ⏎
    case 0x25C0: case 0x25B6: case 0x25B2: case 0x25BC: // ◀ ▶ ▲ ▼
        return true;
    default:
        return false;
    }
}

// The console font's measurements, from its design rather than asked of
// the system: Noto Sans Mono's letters are all 600 thousandths of an em
// wide, its capitals 714 high. Every page lays its buttons out by these, so
// asking each system's rasterizer (which rounds its own way) would put them
// in different places on Linux and macOS.
static bool isConsoleFont(const QFont &f) {
    return f.pixelSize() > 0 && f.family() == QLatin1String("Noto Sans Mono");
}
static qreal letterAdvance(const QFont &f) {
    const int stretch = f.stretch() > 0 ? f.stretch() : 100;
    return 0.6 * f.pixelSize() * stretch / 100.0;
}
static qreal spacing(const QFont &f) {
    return f.letterSpacingType() == QFont::AbsoluteSpacing ? f.letterSpacing() : 0.0;
}
static qreal capHeight(const QFont &f) {
    return isConsoleFont(f) ? 0.714 * f.pixelSize() : QFontMetricsF(f).capHeight();
}

// An icon's box is as wide as it is tall, the height of a capital: it sits
// on the line like a letter.
static qreal iconSize(const QFont &f) { return capHeight(f) * 1.18; }
static qreal iconAdvance(const QFont &f) {
    const qreal space = isConsoleFont(f) ? letterAdvance(f) : QFontMetricsF(f).horizontalAdvance(QLatin1Char(' '));
    return iconSize(f) + space * 0.35;
}

void drawIcon(QPainter &p, QChar c, const QRectF &box, QColor color) {
    const qreal s = std::min(box.width(), box.height());
    const QRectF b(box.center().x() - s / 2, box.center().y() - s / 2, s, s);
    // Strokes as heavy as the console's bold letters, square-ended, mitred:
    // the same line as the frames and the type.
    const qreal w = std::max(1.25, s * 0.15);
    QPen pen(color, w, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const qreal l = b.left() + w / 2, r = b.right() - w / 2, t = b.top() + w / 2, bt = b.bottom() - w / 2;
    const qreal cx = b.center().x(), cy = b.center().y(), head = s * 0.38;
    auto arrow = [&](QPointF from, QPointF to) {
        // A shaft and an open chevron head.
        p.drawLine(from, to);
        const QPointF d = to - from;
        const qreal len = std::hypot(d.x(), d.y());
        const QPointF u = d / len, n(-u.y(), u.x());
        QPolygonF h;
        h << to - u * head + n * head << to << to - u * head - n * head;
        p.drawPolyline(h);
    };
    auto triangle = [&](QPolygonF tri) {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(tri);
    };
    switch (c.unicode()) {
    case 0x2190: arrow({r, cy}, {l, cy}); break;
    case 0x2192: arrow({l, cy}, {r, cy}); break;
    case 0x2191: arrow({cx, bt}, {cx, t}); break;
    case 0x2193: arrow({cx, t}, {cx, bt}); break;
    case 0x2194:
        arrow({cx, cy}, {l, cy});
        arrow({cx, cy}, {r, cy});
        break;
    case 0x2195:
        arrow({cx, cy}, {cx, t});
        arrow({cx, cy}, {cx, bt});
        break;
    case 0x21E7: {
        // Shift: the keycap's outlined arrow, a roof over a stem.
        const qreal stem = s * 0.2;
        QPolygonF a;
        a << QPointF(cx, t) << QPointF(r, cy) << QPointF(cx + stem, cy) << QPointF(cx + stem, bt)
          << QPointF(cx - stem, bt) << QPointF(cx - stem, cy) << QPointF(l, cy);
        p.drawPolygon(a);
        break;
    }
    case 0x21B5:
    case 0x23CE: {
        // Return: down from the top right, then left, into the line.
        const qreal y = cy + s * 0.12;
        p.drawPolyline(QPolygonF{QPointF(r, t), QPointF(r, y), QPointF(l + w, y)});
        QPolygonF h;
        h << QPointF(l + head, y - head) << QPointF(l, y) << QPointF(l + head, y + head);
        p.drawPolyline(h);
        break;
    }
    case 0x25C0: triangle(QPolygonF{QPointF(r, t), QPointF(l, cy), QPointF(r, bt)}); break;
    case 0x25B6: triangle(QPolygonF{QPointF(l, t), QPointF(r, cy), QPointF(l, bt)}); break;
    case 0x25B2: triangle(QPolygonF{QPointF(l, bt), QPointF(cx, t), QPointF(r, bt)}); break;
    case 0x25BC: triangle(QPolygonF{QPointF(l, t), QPointF(cx, bt), QPointF(r, t)}); break;
    default: break;
    }
    p.restore();
}

qreal textWidth(const QFont &f, const QString &text) {
    const QString s = keyNames(text);
    const QFontMetricsF m(f);
    const bool mono = isConsoleFont(f);
    const qreal step = mono ? letterAdvance(f) + spacing(f) : 0;
    qreal w = 0;
    QString run;
    for (const QChar c : s) {
        if (isIcon(c)) {
            w += m.horizontalAdvance(run) + iconAdvance(f);
            run.clear();
        } else if (mono && m.inFont(c)) {
            // A letter of the console font: one advance and its spacing.
            w += step;
        } else {
            run += c;
        }
    }
    return w + m.horizontalAdvance(run);
}

void drawText(QPainter &p, const QRectF &r, int flags, const QString &text) {
    QFont f = p.font();
    QString s = keyNames(text);
    // Too wide for its box (a small window's narrow chips): a smaller size,
    // down to three quarters of it and no less than 7 px, before letters go.
    if (f.pixelSize() > 7 && textWidth(f, s) > r.width() + 0.5) {
        const int floor = std::max(7, int(std::ceil(f.pixelSize() * 0.75)));
        while (f.pixelSize() > floor && textWidth(f, s) > r.width() + 0.5)
            f.setPixelSize(f.pixelSize() - 1);
    }
    const QFont was = p.font();
    p.setFont(f);
    struct Restore {
        QPainter &p;
        QFont f;
        ~Restore() { p.setFont(f); }
    } restore{p, was};
    const QFontMetricsF m(f);
    bool hasIcon = false;
    for (const QChar c : s)
        hasIcon |= isIcon(c);
    if (!hasIcon) {
        p.drawText(r, flags, m.elidedText(s, Qt::ElideRight, r.width()));
        return;
    }
    // Too long: letters go from the end until it fits, then an ellipsis.
    if (textWidth(f, s) > r.width() + 0.5) {
        const QString dots = QStringLiteral("…");
        while (!s.isEmpty() && textWidth(f, s + dots) > r.width() + 0.5)
            s.chop(1);
        s += dots;
    }
    const qreal total = textWidth(f, s);
    qreal x = r.left();
    if (flags & Qt::AlignHCenter)
        x = r.center().x() - total / 2;
    else if (flags & Qt::AlignRight)
        x = r.right() - total;
    // The baseline where Qt would put one line with the same flags.
    qreal base = r.top() + m.ascent();
    if (flags & Qt::AlignVCenter)
        base = r.center().y() - m.height() / 2 + m.ascent();
    else if (flags & Qt::AlignBottom)
        base = r.bottom() - m.descent();
    const qreal size = iconSize(f);
    QString run;
    auto flush = [&] {
        if (run.isEmpty())
            return;
        p.drawText(QPointF(x, base), run);
        x += m.horizontalAdvance(run);
        run.clear();
    };
    const QColor color = p.pen().color();
    for (const QChar c : s) {
        if (!isIcon(c)) {
            run += c;
            continue;
        }
        flush();
        const qreal adv = iconAdvance(f);
        drawIcon(p, c, QRectF(x + (adv - size) / 2, base - m.capHeight() / 2 - size / 2, size, size), color);
        x += adv;
    }
    flush();
}

QColor alpha(QColor c, float a) {
    c.setAlphaF(std::clamp(a, 0.0f, 1.0f));
    return c;
}

void pad(QPainter &p, const PaintContext &pc, const QString &legend) {
    const QRectF frame = pc.pad.adjusted(0, -kHead, 0, 0);
    p.fillRect(frame, QColor(0, 0, 0));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pc.color, 2.0));
    p.drawRect(frame);
    if (legend.isEmpty())
        return;
    const QFont f = font(13, true);
    p.setFont(f);
    const qreal w = QFontMetricsF(f).horizontalAdvance(legend) + 16;
    const QRectF r(pc.pad.left() + 16, tabY(pc) - 9, w, 18);
    p.fillRect(r, QColor(0, 0, 0));
    p.setPen(pc.color);
    drawText(p, r, Qt::AlignCenter, legend);
}

qreal variation(QPainter &p, const PaintContext &pc, const QString &name) {
    return tag(p, QPointF(pc.pad.left() + 110, tabY(pc)), name.toUpper(), pc.color, 12).right() + 8;
}

QRectF tag(QPainter &p, QPointF at, const QString &s, QColor color, int px, bool centred) {
    const QFont f = font(px, true);
    p.setFont(f);
    const qreal w = textWidth(f, s) + 12, h = px + 7;
    QRectF r(centred ? at.x() - w / 2 : at.x(), at.y() - h / 2, w, h);
    p.fillRect(r, color);
    p.setPen(QColor::fromRgb(kInk));
    drawText(p, r, Qt::AlignCenter, s);
    return r;
}

void text(QPainter &p, const QRectF &r, const QString &s, QColor color, int px, bool bold, Qt::Alignment align) {
    const QFont f = font(px, bold);
    p.setFont(f);
    p.setPen(color);
    drawText(p, r, int(align), s);
}

void gauge(QPainter &p, const QRectF &r, float value, QColor color, int segments, bool bipolar) {
    value = std::clamp(value, 0.0f, 1.0f);
    const qreal w = r.width() / segments;
    const int lit = int(std::lround(value * segments));
    const int mid = segments / 2;
    for (int k = 0; k < segments; ++k) {
        const QRectF seg(r.left() + k * w, r.top(), w - 2, r.height());
        bool on = k < lit;
        if (bipolar)
            on = value >= 0.5f ? (k >= mid && k < lit) : (k < mid && k >= lit);
        p.fillRect(seg, on ? color : alpha(color, 0.13f));
    }
}

QRectF chip(QPainter &p, const PaintContext &pc, const QRectF &r, const QString &raw, bool lit, Hotspots *spots,
            int id) {
    const QString label = friendly(raw);
    const bool over = pc.mouseOver && r.contains(pc.mouse);
    if (lit)
        p.fillRect(r, pc.color);
    else if (over)
        p.fillRect(r, alpha(pc.color, 0.22f));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(lit || over ? pc.color : alpha(pc.color, 0.6f), 1.5));
    p.drawRect(r);
    text(p, r.adjusted(4, 0, -4, 0), label, lit ? QColor::fromRgb(kInk) : pc.color,
         int(std::clamp(r.height() * 0.42, 9.0, 13.0)), true, Qt::AlignCenter);
    if (spots && id >= 0)
        spots->add(r, id);
    return r;
}

void button(QPainter &p, const QRectF &r, const QString &title, QColor color, bool lit) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, 2));
    p.drawRect(r);
    const QRectF header(r.left(), r.top(), r.width(), std::min<qreal>(22, r.height()));
    if (lit)
        p.fillRect(header, color);
    text(p, header.adjusted(6, 0, -4, 0), title, lit ? QColor::fromRgb(kInk) : color,
         int(std::clamp(r.width() / 9.5, 9.0, 13.0)), true);
}

void finger(QPainter &p, const PaintContext &pc, const Touch &t, QColor color, const QString &label, float glow) {
    const QPointF c = pc.at(t.x, t.y);
    const qreal unit = pc.pad.height() / 10.0;
    const qreal r = unit * 0.32 * std::clamp(t.size, 0.6f, 4.0f);
    if (glow > 0) {
        QRadialGradient g(c, r * 2.4);
        g.setColorAt(0, alpha(color, 0.45f * glow));
        g.setColorAt(1, alpha(color, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c, r * 2.4, r * 2.4);
    }
    p.save();
    p.translate(c);
    p.rotate(-t.angle * 180.0 / 3.14159265);
    p.setBrush(alpha(color, 0.18f + 0.3f * t.pressure));
    p.setPen(QPen(color, 2.0));
    p.drawEllipse(QPointF(0, 0), r, r * 0.8);
    p.restore();
    if (!label.isEmpty())
        tag(p, QPointF(c.x() + r + 6, c.y() - r * 0.6), label, color, 12);
}

void grid(QPainter &p, const PaintContext &pc, int columns, int rows, QColor color, float a) {
    p.setPen(QPen(alpha(color, a), 1));
    for (int i = 1; i < columns; ++i) {
        const qreal x = pc.pad.left() + pc.pad.width() * i / columns;
        p.drawLine(QPointF(x, pc.pad.top()), QPointF(x, pc.pad.bottom()));
    }
    for (int i = 1; i < rows; ++i) {
        const qreal y = pc.pad.top() + pc.pad.height() * i / rows;
        p.drawLine(QPointF(pc.pad.left(), y), QPointF(pc.pad.right(), y));
    }
}

void beats(QPainter &p, const PaintContext &pc, QColor color) {
    const qreal box = 14, gap = 6;
    const qreal x0 = pc.pad.right() - 16 - 4 * box - 3 * gap;
    p.fillRect(QRectF(x0 - 6, tabY(pc) - 10, 4 * box + 3 * gap + 12, 20), QColor(0, 0, 0));
    // Until something plays or the metronome clicks, the clock is silent:
    // a moving beat would only be a guess at where the player's ONE is.
    bool audible = pc.studio && pc.studio->metronome;
    for (int i = 0; pc.studio && i < pc.studio->partCount; ++i)
        audible |= pc.studio->parts[i].playing >= 0;
    const double within = pc.beat - std::floor(pc.beat / kBeatsPerBar) * kBeatsPerBar;
    const int beat = audible ? std::clamp(int(within), 0, 3) : -1;
    for (int b = 0; b < 4; ++b) {
        const QRectF r(x0 + b * (box + gap), tabY(pc) - 7, box, box);
        if (b == beat) {
            p.fillRect(r, b == 0 ? color : alpha(color, 0.75f));
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(alpha(color, 0.6f), 1.5));
            p.drawRect(r);
        }
    }
}

namespace {
QRectF g_clipStrip;
}
QRectF lastClipStrip() { return g_clipStrip; }
namespace {
int g_paintLevel = 2;
}
void setPaintLevel(int level) { g_paintLevel = level; }
int paintLevel() { return g_paintLevel; }

QString friendly(const QString &label) {
    static const QRegularExpression key(QStringLiteral("^(F\\d{1,2}|DEL|§|[A-Z]|[-+]|.*[⇧↵←→↑↓\\\\].*)$"));
    QStringList words = label.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // An arrow among the keys left off tells a pair apart (RHYTHM ← and
    // RHYTHM →): it stays, without its ⇧.
    QString arrow;
    while (words.size() > 1 && key.match(words.last()).hasMatch()) {
        for (const QChar c : words.last())
            if (QStringLiteral("←→↑↓").contains(c))
                arrow = QString(c);
        words.removeLast();
    }
    QString s = words.join(QLatin1Char(' '));
    auto withArrow = [&](const QString &w) {
        return arrow.isEmpty() || w == QStringLiteral("−") || w == QStringLiteral("+") ? w : w + QLatin1Char(' ') + arrow;
    };
    static const std::pair<const char *, const char *> plain[] = {
        {"OVERDUB", "PLAY OVER"},    {"KEY TONE", "BRIGHTNESS"},
        {"LATCH", "HOLD NOTES"},     {"BACK", "SOUNDS BACK"},    {"LOOP−", "SHORTER"},
        {"LOOP+", "LONGER"},         {"FALL", "FALL AWAY"},      {"MIX", "BUILD THE MIX"},
        {"DICE", "SURPRISE ME"},     {"GATE", "RHYTHM"},         {"DARK", "DARKER"},
        {"BRIGHT", "BRIGHTER"},      {"VOICE", "VOICING"},       {"SQUELCH", "SQUELCH"},
    };
    for (const auto &[from, to] : plain)
        if (s == QString::fromUtf8(from))
            return withArrow(QString::fromUtf8(to));
    return withArrow(s);
}

int firstEmptySlot(const PartView &v) {
    for (int s = 0; s < kClipSlots; ++s)
        if (v.clips[s].state == ClipView::Empty)
            return s;
    return -1;
}

void clips(QPainter &p, const QRectF &r, const PartView &v, QColor color, double time, Hotspots *spots) {
    g_clipStrip = r;
    const qreal w = r.width() / kClipSlots;
    const bool blink = std::fmod(time, 0.5) < 0.25;
    // The first empty slot is where the next loop goes: a "+" there, and a
    // click on it starts one (clickClip).
    const int next = firstEmptySlot(v);
    for (int s = 0; s < kClipSlots; ++s) {
        const ClipView &c = v.clips[s];
        const QRectF cell(r.left() + s * w + 2, r.top(), w - 4, r.height());
        if (spots)
            spots->add(cell, kClipSpot + s);
        QColor col = color;
        if (c.state == ClipView::Recording)
            col = red();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.state == ClipView::Empty ? alpha(col, 0.3f) : col, 1.5,
                      c.state == ClipView::Empty ? Qt::DotLine : Qt::SolidLine));
        p.drawRect(cell);
        switch (c.state) {
        case ClipView::Playing:
        case ClipView::Stopping: {
            p.fillRect(cell, alpha(col, 0.25f));
            QRectF hand(cell.left(), cell.top(), cell.width() * c.phase, cell.height());
            p.fillRect(hand, alpha(col, c.state == ClipView::Stopping && blink ? 0.3f : 0.65f));
            break;
        }
        case ClipView::Queued:
            if (blink)
                p.fillRect(cell, alpha(col, 0.5f));
            break;
        case ClipView::Recording:
            p.fillRect(cell, alpha(col, blink ? 0.7f : 0.35f));
            break;
        default:
            break;
        }
        if (s == next) {
            p.setPen(QPen(alpha(col, 0.75f), 1.5, Qt::DashLine));
            p.drawRect(cell);
            text(p, cell, cell.width() >= 64 ? QStringLiteral("+ NEW") : QStringLiteral("+"), alpha(col, 0.9f),
                 int(std::clamp(cell.height() * 0.45, 9.0, 13.0)), true, Qt::AlignCenter);
            continue;
        }
        QString label = QString::number(s + 1);
        if (g_paintLevel < 2 && c.state != ClipView::Empty && c.lengthBeats > 0) {
            // A newcomer's slot: its length in words.
            const int bars = int(c.lengthBeats / 4);
            label = c.lengthBeats < 4 ? QStringLiteral("%1 BEAT%2").arg(int(c.lengthBeats)).arg(c.lengthBeats > 1 ? "S" : "")
                                      : QStringLiteral("%1 BAR%2").arg(bars).arg(bars > 1 ? "S" : "");
        } else if (c.state != ClipView::Empty && c.lengthBeats > 0) {
            const int bars = int(c.lengthBeats / 4);
            const QString len = c.lengthBeats >= 4 ? (cell.width() >= 74 ? QStringLiteral("%1 BAR").arg(bars)
                                                                          : QStringLiteral("%1B").arg(bars))
                                                   : QStringLiteral("%1/4").arg(int(c.lengthBeats));
            label += QStringLiteral(" · ") + len;
        }
        // The words on a dark plate over the sweeping fill, so they read
        // wherever the hand is.
        int px = int(std::clamp(cell.height() * 0.45, 9.0, 13.0));
        // Smaller rather than cut off, in a narrow slot.
        while (px > 9 && textWidth(font(px, true), label) > cell.width() - 4)
            --px;
        // Still too wide to read: the bars as a number, with the "BAR"
        // left to the wider slots.
        if (g_paintLevel < 2 && c.state != ClipView::Empty && textWidth(font(px, true), label) > cell.width() - 4)
            label = c.lengthBeats >= 4 ? QStringLiteral("%1").arg(int(c.lengthBeats / 4))
                                       : QStringLiteral("%1/4").arg(int(c.lengthBeats));
        if (c.state != ClipView::Empty) {
            const qreal tw = std::min(cell.width() - 4, textWidth(font(px, true), label) + 8);
            p.fillRect(QRectF(cell.center().x() - tw / 2, cell.center().y() - px * 0.7, tw, px * 1.4),
                       QColor(0, 0, 0, 200));
        }
        text(p, cell, label, c.state == ClipView::Recording ? red() : col, px, true, Qt::AlignCenter);
    }
}

namespace {
std::vector<QRectF> g_browseChips;
}

std::vector<QRectF> g_nextChips;
std::vector<QRectF> g_clickable;

void noteClickable(const QRectF &r) { g_clickable.push_back(r); }

bool clickableAt(QPointF at) {
    for (const QRectF &r : g_clickable)
        if (r.contains(at))
            return true;
    return browseChipAt(at) || nextChipAt(at);
}

void resetBrowseChips() {
    g_browseChips.clear();
    g_nextChips.clear();
    g_clickable.clear();
}

bool nextChipAt(QPointF at) {
    for (const QRectF &r : g_nextChips)
        if (r.contains(at))
            return true;
    return false;
}

void nextChip(QPainter &p, const PaintContext &pc, const QRectF &r) {
    static const QColor white("#F4F1E8");
    const bool over = pc.mouseOver && r.contains(pc.mouse);
    p.fillRect(r, over ? white : alpha(white, 0.12f));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(white, 1.5));
    p.drawRect(r);
    text(p, r, QStringLiteral("→"), over ? QColor::fromRgb(kInk) : white,
         int(std::clamp(r.height() * 0.5, 10.0, 15.0)), true, Qt::AlignCenter);
    g_nextChips.push_back(r);
}

bool browseChipAt(QPointF at) {
    for (const QRectF &r : g_browseChips)
        if (r.contains(at))
            return true;
    return false;
}

int windowFirst(int lit, int rest, int shown, int count) {
    rest = std::clamp(rest, 0, std::max(0, count - shown));
    if (lit < 0)
        return rest;
    if (lit < rest)
        return lit;
    if (lit >= rest + shown)
        return std::min(count - shown, lit - shown + 1);
    return rest;
}

void browseChip(QPainter &p, const PaintContext &pc, const QRectF &r, int more) {
    static const QColor white("#F4F1E8");
    const bool over = pc.mouseOver && r.contains(pc.mouse);
    p.fillRect(r, over ? white : alpha(white, 0.12f));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(white, 1.5));
    p.drawRect(r);
    text(p, r.adjusted(4, 0, -4, 0), QStringLiteral("+%1 MORE").arg(more), over ? QColor::fromRgb(kInk) : white,
         int(std::clamp(r.height() * 0.42, 9.0, 13.0)), true, Qt::AlignCenter);
    g_browseChips.push_back(r);
}

void chips(QPainter &p, const PaintContext &pc, const QRectF &row, const QStringList &labels, int lit,
           Hotspots *spots, int idBase, double gap, const float *glow, int browseMode) {
    const int n = int(labels.size());
    if (n == 0)
        return;
    QRectF r = row;
    if (browseMode == kBrowseRowNext && r.width() > 4 * kNextChipWidth) {
        nextChip(p, pc, QRectF(r.right() - kNextChipWidth, r.top(), kNextChipWidth, r.height()));
        r.setRight(r.right() - kNextChipWidth - gap);
    }
    if (browseMode) {
        const QFont f = font(int(std::clamp(r.height() * 0.42, 9.0, 13.0)), true);
        double need = 0;
        for (const QString &l : labels)
            need = std::max(need, textWidth(f, keyNames(l)) + 14);
        const int fits = std::max(3, int((r.width() + gap) / (need + gap)));
        if (n > fits) {
            // The lit one and its neighbours, then the way to all of them.
            const int shown = fits - 1;
            const int first = windowFirst(lit, std::max(0, lit - shown / 2), shown, n);
            const double w = (r.width() + gap) / fits;
            for (int k = 0; k < shown; ++k) {
                const int i = first + k;
                const QRectF cell(r.left() + k * w, r.top(), w - gap, r.height());
                if (glow && i != lit && glow[i] > 0.02f)
                    p.fillRect(cell, alpha(pc.color, 0.3f * std::min(1.0f, glow[i])));
                if (!labels[i].isEmpty())
                    chip(p, pc, cell, labels[i], i == lit, spots, idBase + i);
            }
            browseChip(p, pc, QRectF(r.left() + shown * w, r.top(), w - gap, r.height()), n - shown);
            return;
        }
    }
    // As many as fit with their names whole. More than that, and the row
    // becomes a window onto them that follows the lit one, the chip at
    // each end faded where there are more beyond it (still clickable: a
    // click there moves the window along).
    const QFont f = font(int(std::clamp(r.height() * 0.42, 9.0, 13.0)), true);
    double need = 0;
    for (const QString &l : labels)
        need = std::max(need, textWidth(f, keyNames(l)) + 14);
    const int fits = std::max(3, int((r.width() + gap) / (need + gap)));
    int first = 0, shown = n;
    if (n > fits) {
        shown = fits;
        first = std::clamp((lit < 0 ? 0 : lit) - shown / 2, 0, n - shown);
    }
    const double w = (r.width() + gap) / shown;
    for (int k = 0; k < shown; ++k) {
        const int i = first + k;
        if (labels[i].isEmpty())
            continue;
        const bool more = (k == 0 && first > 0) || (k == shown - 1 && i < n - 1);
        if (more) {
            p.save();
            p.setOpacity(0.3);
        }
        const QRectF cell(r.left() + k * w, r.top(), w - gap, r.height());
        if (glow && i != lit && glow[i] > 0.02f)
            p.fillRect(cell, alpha(pc.color, 0.3f * std::min(1.0f, glow[i])));
        chip(p, pc, cell, labels[i], i == lit, spots, idBase + i);
        if (more)
            p.restore();
    }
}

bool clickClip(int id, const PartView &v, ControlContext &ctx) {
    const int slot = id - kClipSpot;
    if (slot < 0 || slot >= kClipSlots)
        return false;
    const ClipView &c = v.clips[slot];
    if (c.state == ClipView::Empty) {
        // The "+": a new loop. With this part's loop playing, it stops (kept
        // in its slot) and the next Space makes a loop of its own.
        if (v.playing >= 0 || v.queued >= 0)
            ctx.newLoop();
        else
            ctx.notice(QStringLiteral("Play something, then LOOP (Space) keeps it here"));
        return true;
    }
    if (c.state == ClipView::Playing)
        ctx.stop();
    else
        ctx.launch(slot);
    return true;
}

}
