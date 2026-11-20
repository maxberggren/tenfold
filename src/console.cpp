#include "console.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace tf::console {

QFont font(int px, bool bold) {
    QFont f("Noto Sans Mono");
    f.setPixelSize(std::max(8, px));
    f.setStretch(QFont::SemiCondensed);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    f.setWeight(bold ? QFont::Bold : QFont::DemiBold);
    return f;
}

QColor alpha(QColor c, float a) {
    c.setAlphaF(std::clamp(a, 0.0f, 1.0f));
    return c;
}

void pad(QPainter &p, const PaintContext &pc, const QString &legend) {
    p.fillRect(pc.pad, QColor(0, 0, 0));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pc.color, 2.0));
    p.drawRect(pc.pad);
    if (legend.isEmpty())
        return;
    const QFont f = font(13, true);
    p.setFont(f);
    const qreal w = QFontMetricsF(f).horizontalAdvance(legend) + 16;
    const QRectF r(pc.pad.left() + 16, pc.pad.top() - 9, w, 18);
    p.fillRect(r, QColor(0, 0, 0));
    p.setPen(pc.color);
    p.drawText(r, Qt::AlignCenter, legend);
}

QRectF tag(QPainter &p, QPointF at, const QString &s, QColor color, int px, bool centred) {
    const QFont f = font(px, true);
    p.setFont(f);
    const qreal w = QFontMetricsF(f).horizontalAdvance(s) + 12, h = px + 7;
    QRectF r(centred ? at.x() - w / 2 : at.x(), at.y() - h / 2, w, h);
    p.fillRect(r, color);
    p.setPen(QColor::fromRgb(kInk));
    p.drawText(r, Qt::AlignCenter, s);
    return r;
}

void text(QPainter &p, const QRectF &r, const QString &s, QColor color, int px, bool bold, Qt::Alignment align) {
    const QFont f = font(px, bold);
    p.setFont(f);
    p.setPen(color);
    p.drawText(r, int(align), QFontMetricsF(f).elidedText(s, Qt::ElideRight, r.width()));
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
    p.fillRect(QRectF(x0 - 6, pc.pad.top() - 10, 4 * box + 3 * gap + 12, 20), QColor(0, 0, 0));
    const double within = pc.beat - std::floor(pc.beat / kBeatsPerBar) * kBeatsPerBar;
    const int beat = std::clamp(int(within), 0, 3);
    for (int b = 0; b < 4; ++b) {
        const QRectF r(x0 + b * (box + gap), pc.pad.top() - 7, box, box);
        if (b == beat) {
            p.fillRect(r, b == 0 ? color : alpha(color, 0.75f));
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(alpha(color, 0.6f), 1.5));
            p.drawRect(r);
        }
    }
}

void clips(QPainter &p, const QRectF &r, const PartView &v, QColor color, double time) {
    const qreal w = r.width() / kClipSlots;
    const bool blink = std::fmod(time, 0.5) < 0.25;
    for (int s = 0; s < kClipSlots; ++s) {
        const ClipView &c = v.clips[s];
        const QRectF cell(r.left() + s * w + 2, r.top(), w - 4, r.height());
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
        QString label = QString::number(s + 1);
        if (c.state != ClipView::Empty && c.lengthBeats > 0)
            label += c.lengthBeats >= 4 ? QStringLiteral(" %1B").arg(int(c.lengthBeats / 4))
                                        : QStringLiteral(" %1/4").arg(int(c.lengthBeats));
        text(p, cell, label,
             c.state == ClipView::Playing || c.state == ClipView::Recording ? QColor::fromRgb(kInk) : col,
             int(std::clamp(cell.height() * 0.45, 9.0, 13.0)), true, Qt::AlignCenter);
    }
}

}
