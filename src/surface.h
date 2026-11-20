#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickPaintedItem>

#include "backend.h"

// The pad as the player sees it: the trackpad's own shape, drawn by the page
// that is showing, with room under it for the page's clip slots.
class Surface : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(Backend *app READ backend WRITE setBackend NOTIFY backendChanged)

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

signals:
    void backendChanged();

private:
    QRectF padRect() const;
    void sendPointer(QPointF pos, bool down);

    QPointer<Backend> m_backend;
    bool m_mouseHeld = false;
};
