#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>

#include "backend.h"

// The pad as the player sees it, as large as the item allows, drawn by the page
// that is showing, with room under it for the page's clip slots.
class Surface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *app READ backend WRITE setBackend NOTIFY backendChanged)
    // Where the pad is drawn, in item pixels: the visualizer's picture goes there.
    Q_PROPERTY(QRectF pad READ padRect NOTIFY padChanged)

public:
    explicit Surface(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *b);

    void paint(QPainter *p) override;

protected:
    // The mouse plays the pad too: pressed it is a finger, over it a pointer.
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void hoverLeaveEvent(QHoverEvent *e) override;

    QRectF padRect() const;

signals:
    void backendChanged();
    void padChanged();

protected:
    void geometryChange(const QRectF &now, const QRectF &old) override;

private:
    QRectF m_pad;
    void sendPointer(QPointF pos, bool down);

    QPointer<Backend> m_backend;
    bool m_mouseHeld = false;
    bool m_hand = false; // the pointer is a hand (over something clickable)
};
