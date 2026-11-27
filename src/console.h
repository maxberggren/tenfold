#pragma once

// The console's look, shared by every page: black glass, phosphor lines in
// each part's colour, condensed capitals, lit tags with dark ink, segmented
// gauges. Pages draw with these so the whole instrument reads as one panel.

#include <QColor>
#include <QFont>
#include <QPainter>
#include <utility>
#include <vector>
#include <QRectF>
#include <QString>
#include <QStringList>

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
// Noto Sans Mono, the one typeface everything is drawn in, is compiled in
// (src/fonts.qrc, under the OFL): loaded once at start, it makes the window
// and every page's layout the same on any system, installed font or not.
// The pages place their buttons by its measurements, so the tests load it
// too.
void loadFonts();

// The keys as this system names them: tenfold's Ctrl shortcuts are the
// Command key on macOS (Qt maps them), so "Ctrl M" reads "⌘ M" there.
// Every label, help line, tutorial step and notice goes through this.
QString keyNames(const QString &text);

// A punctuation key as it is unshifted on a US keyboard: Shift+, arrives
// as <, Shift+; as :, and so on. Pages that play notes from those keys and
// take Shift as an accent look them up by this, on press and on release,
// so an accent plays and a key let go after Shift doesn't leave a note on.
int unshifted(int key);

QFont font(int px, bool bold = false);

// Arrows and key symbols (← → ↑ ↓ ⇧ ↵ ⏎ ◀ ▶ ▲ ▼ ↕ ↔) are drawn by the
// console itself, as line art in the text's colour, rather than taken from
// fonts that lack them or render them as emoji. drawText and textWidth know
// them; everything the console writes goes through them.
bool isIcon(QChar c);
void drawIcon(QPainter &p, QChar c, const QRectF &box, QColor color);
qreal textWidth(const QFont &f, const QString &s);
// One line of `s` in `r` with Qt alignment flags (no wrapping), elided with
// … when it doesn't fit.
void drawText(QPainter &p, const QRectF &r, int flags, const QString &s);
QColor alpha(QColor c, float a);

// The pad: black, its frame in the part's colour, a legend cut into the top
// edge on the left.
void pad(QPainter &p, const PaintContext &pc, const QString &legend);
// A lit tag: a filled box with dark ink, its left edge at x (or centred
// on it), its middle at y. Returns where it went.
QRectF tag(QPainter &p, QPointF at, const QString &text, QColor color, int px = 13, bool centred = false);
// The pad's frame runs this far above the pad itself, the same gap as at
// its sides; the legend and the tags sit on the top edge as tabs, mostly
// above it, so they stay clear of the content.
constexpr qreal kHead = 5;
inline qreal edge(const PaintContext &pc) { return pc.pad.top() - kHead; }
// Where a tab on that edge is centred: a little above the line.
inline qreal tabY(const PaintContext &pc) { return edge(pc) - 4; }
// The page's active variation, cut into the pad's top edge a little after
// its legend (INSTRUMENT ── VARIATION); returns where the next tag goes.
qreal variation(QPainter &p, const PaintContext &pc, const QString &name);
// Text in the console font, in a box.
void text(QPainter &p, const QRectF &r, const QString &s, QColor color, int px = 12, bool bold = false,
          Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter);
// A segmented gauge across r, lit up to value 0..1 (bipolar: lit from the
// middle toward the value).
void gauge(QPainter &p, const QRectF &r, float value, QColor color, int segments = 10, bool bipolar = false);
// A console button: an outlined box with a header; lit when on.
// What a page drew to be clicked: added while painting, asked by click().
// The ids are the page's own.
struct Hotspots {
    std::vector<std::pair<QRectF, int>> spots;
    void clear() { spots.clear(); }
    void add(const QRectF &r, int id) { spots.push_back({r, id}); }
    int at(QPointF p) const {
        for (auto it = spots.rbegin(); it != spots.rend(); ++it)
            if (it->first.contains(p))
                return it->second;
        return -1;
    }
};

// The band under the pad holds two rows of controls, each 30 px high:
// row 0 the clip slots and the page's main choice (kit, sound), row 1 the
// settings the keys reach (swing, follow, latch, overdub…), so the mouse can
// reach everything. x0, x1 are fractions of the pad's width.
inline QRectF bandRow(const PaintContext &pc, int row, double x0 = 0, double x1 = 1) {
    const QRectF &pad = pc.visible.isEmpty() ? pc.pad : pc.visible;
    return QRectF(pad.left() + pad.width() * x0, pad.bottom() + 12 + row * 40, pad.width() * (x1 - x0), 30);
}

// Lays `labels` out as equal chips across `r`, the `lit` one filled; each
// clickable as idBase + its index. A label "" leaves a gap. `glow`, if
// given, is one 0..1 per label: a soft fill under the chips not lit (the
// drums' kits a loop is playing while another kit is shown).
// browse: a row of the page's sounds (kBrowseRow, or kBrowseRowNext for the
// one row of a page that also gets the → chip). When they don't all fit, it
// shows those around the lit one and ends in a "+N MORE" chip (browseChip)
// that opens the sound browser with every one of them; with Next, a "→"
// chip at its end steps to the next sound, as the → key does.
constexpr int kBrowseRow = 1, kBrowseRowNext = 2;
void chips(QPainter &p, const PaintContext &pc, const QRectF &r, const QStringList &labels, int lit,
           Hotspots *spots, int idBase, double gap = 4, const float *glow = nullptr, int browse = 0);
// The "→" chip: the next sound, as → does (nextChipAt for the session).
void nextChip(QPainter &p, const PaintContext &pc, const QRectF &r);
bool nextChipAt(QPointF at);
constexpr double kNextChipWidth = 46;
// The "+N MORE" chip: white, so it reads as a way on rather than a sound.
// Where it was drawn is remembered for the session (browseChipAt), which
// opens the browser when it is clicked; resetBrowseChips() starts a paint.
void browseChip(QPainter &p, const PaintContext &pc, const QRectF &r, int more);
bool browseChipAt(QPointF at);
void resetBrowseChips();
// The first of `shown` consecutive items that keeps `lit` among them,
// moving from `rest` only as far as it must.
int windowFirst(int lit, int rest, int shown, int count);

// Ids the shared clip strip answers to: kClipSpot + slot.
constexpr int kClipSpot = 1000;
// The clip slots, clickable (kClipSpot + slot); clickClip() does what a
// click on one should: launch it at the next bar, or stop it if it plays.
bool clickClip(int id, const PartView &v, ControlContext &ctx);
// The slot the next loop goes into (drawn as "+"), or −1 when all are full.
int firstEmptySlot(const PartView &v);
// Where clips() last drew the clip strip (the UI thread's last paint).
QRectF lastClipStrip();
// The window's level for what follows (Session::paint sets it): below
// Studio the clip slots say "1 BAR" rather than "1 · 1B".
void setPaintLevel(int level);

// A row of clickable chips under the pad (or anywhere): each a label in
// the page colour, lit when on, brighter under the mouse. Returns the rect.
QRectF chip(QPainter &p, const PaintContext &pc, const QRectF &r, const QString &label, bool lit,
            Hotspots *spots = nullptr, int id = -1);

void button(QPainter &p, const QRectF &r, const QString &title, QColor color, bool lit);
// A finger: an ellipse sized by contact, a lit tag with what it does.
void finger(QPainter &p, const PaintContext &pc, const Touch &t, QColor color, const QString &label = {},
            float glow = 0);
// Faint lines dividing the pad into columns and rows.
void grid(QPainter &p, const PaintContext &pc, int columns, int rows, QColor color, float a = 0.18f);
// The four beats of the bar, the current one lit, at the pad's top right;
// dark until a clip plays or the metronome is on.
void beats(QPainter &p, const PaintContext &pc, QColor color);
// This part's eight clip slots in a row: empty, stopped, queued (blinking),
// playing (a hand sweeping round), recording (red).
void clips(QPainter &p, const QRectF &r, const PartView &v, QColor color, double time, Hotspots *spots = nullptr);

}
