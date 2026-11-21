#include "surface.h"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>

using namespace tf;

Surface::Surface(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void Surface::setBackend(Backend *b) {
    if (m_backend == b)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = b;
    if (b)
        connect(b, &Backend::frame, this, [this] { update(); });
    emit backendChanged();
}

QRectF Surface::padRect() const {
    return Session::padRect(QSizeF(width(), height()), m_backend ? m_backend->padAspect() : 1.6f);
}

void Surface::paint(QPainter *p) {
    if (!m_backend)
        return;
    p->setRenderHint(QPainter::Antialiasing);
    m_backend->session()->paint(*p, padRect(), m_backend->session()->clock());
}

void Surface::sendPointer(QPointF pos, bool down) {
    if (!m_backend)
        return;
    const QRectF pad = padRect();
    const float x = float((pos.x() - pad.left()) / pad.width());
    const float y = float(1.0 - (pos.y() - pad.top()) / pad.height());
    const bool inside = x >= 0 && x <= 1 && y >= 0 && y <= 1;
    m_backend->session()->pointer(x, y, down, inside);
    update();
}

void Surface::mousePressEvent(QMouseEvent *e) {
    const QRectF pad = padRect();
    if (!pad.contains(e->position())) {
        // Around the pad: a control the page drew there, if any.
        if (m_backend && m_backend->session()->clickAt(e->position())) {
            update();
            return;
        }
        e->ignore();
        return;
    }
    m_mouseHeld = true;
    sendPointer(e->position(), true);
}

void Surface::mouseMoveEvent(QMouseEvent *e) {
    if (m_mouseHeld)
        sendPointer(e->position(), true);
}

void Surface::mouseReleaseEvent(QMouseEvent *e) {
    if (!m_mouseHeld)
        return;
    m_mouseHeld = false;
    sendPointer(e->position(), false);
}

void Surface::hoverMoveEvent(QHoverEvent *e) {
    if (m_backend)
        m_backend->session()->mouseAt(e->position(), true);
    if (!m_mouseHeld)
        sendPointer(e->position(), false);
    update();
}

void Surface::hoverLeaveEvent(QHoverEvent *) {
    if (m_backend) {
        m_backend->session()->mouseAt({}, false);
        if (!m_mouseHeld)
            m_backend->session()->pointer(-1, -1, false, false);
    }
    update();
}
