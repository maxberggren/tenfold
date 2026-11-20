#pragma once

// The console's look, shared by every page: black glass, phosphor lines in
// each part's colour, condensed capitals, lit tags with dark ink, segmented
// gauges. Pages draw with these so the whole instrument reads as one panel.

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QString>

#include "part.h"

namespace tf::console {

constexpr QRgb kInk = 0xff0a0806; // text on a lit field

// Fixed phosphors besides each part's own.
inline QColor amber() { return QColor("#FFB000"); }
inline QColor green() { return QColor("#39FF88"); }
inline QColor cyan() { return QColor("#2EE6FF"); }
inline QColor magenta() { return QColor("#FF4FD8"); }
inline QColor orange() { return QColor("#FF8A1F"); }
inline QColor red() { return QColor("#FF4136"); }
inline QColor violet() { return QColor("#A98BFF"); }
inline QColor dim() { return QColor(255, 255, 255, 60); }

// A condensed monospace in capitals.
QFont font(int px, bool bold = false);
QColor alpha(QColor c, float a);

// The pad: black, its frame in the part's colour, a legend cut into the top
// edge on the left.
void pad(QPainter &p, const PaintContext &pc, const QString &legend);
// A lit tag: a filled box with dark ink, its left edge at x (or centred
// on it), its middle at y. Returns where it went.
QRectF tag(QPainter &p, QPointF at, const QString &text, QColor color, int px = 13, bool centred = false);
// Text in the console font, in a box.
void text(QPainter &p, const QRectF &r, const QString &s, QColor color, int px = 12, bool bold = false,
          Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter);
// A segmented gauge across r, lit up to value 0..1 (bipolar: lit from the
// middle toward the value).
void gauge(QPainter &p, const QRectF &r, float value, QColor color, int segments = 10, bool bipolar = false);
// A console button: an outlined box with a header; lit when on.
void button(QPainter &p, const QRectF &r, const QString &title, QColor color, bool lit);
// A finger: an ellipse sized by contact, a lit tag with what it does.
void finger(QPainter &p, const PaintContext &pc, const Touch &t, QColor color, const QString &label = {},
            float glow = 0);
// Faint lines dividing the pad into columns and rows.
void grid(QPainter &p, const PaintContext &pc, int columns, int rows, QColor color, float a = 0.18f);
// The four beats of the bar, the current one lit, at the pad's top right.
void beats(QPainter &p, const PaintContext &pc, QColor color);
// This part's eight clip slots in a row: empty, stopped, queued (blinking),
// playing (a hand sweeping round), recording (red).
void clips(QPainter &p, const QRectF &r, const PartView &v, QColor color, double time);

}
