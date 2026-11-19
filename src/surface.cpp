#include "surface.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <cmath>

#include "backend.h"
#include "tuning.h"

using namespace tf;

namespace {

constexpr float kPiF = 3.14159265f;

void fft(std::vector<std::complex<float>> &a, const std::vector<std::complex<float>> &tw) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * tw[k * step];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

QColor withAlpha(QColor c, float a) {
    c.setAlphaF(std::clamp(a, 0.0f, 1.0f));
    return c;
}

}

Surface::Surface(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    m_clock.start();
    m_window.resize(kFft);
    for (int i = 0; i < kFft; ++i)
        m_window[i] = 0.5f - 0.5f * std::cos(2 * kPiF * i / (kFft - 1));
    m_twiddle.resize(kFft / 2);
    for (int i = 0; i < kFft / 2; ++i)
        m_twiddle[i] = std::polar(1.0f, -2 * kPiF * i / kFft);
    m_history.assign(kHistory, std::vector<float>(kColumns, 0.0f));
}

void Surface::setBackend(Backend *b) {
    if (m_backend == b)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = b;
    if (b)
        connect(b, &Backend::frame, this, &Surface::onFrame);
    emit backendChanged();
}

void Surface::setAccent(const QColor &c) {
    if (m_accent == c)
        return;
    m_accent = c;
    emit accentChanged();
    update();
}

QColor Surface::roleColor(int role, const QColor &accent) {
    static const char *colors[RoleCount] = {nullptr,   "#9D7CFF", "#6EE7F9", "#60A5FA", "#F472B6",
                                            "#34D399", "#FB923C", "#E879F9", "#A3E635", "#F87171"};
    if (role <= 0 || role >= RoleCount)
        return accent;
    return QColor(colors[role]);
}

QColor Surface::materialColor(float m) {
    static const QColor c[5] = {QColor("#F59E0B"), QColor("#FB7185"), QColor("#C08457"), QColor("#7DD3FC"),
                                QColor("#E5E7EB")};
    const float mc = std::clamp(m, 0.0f, 4.0f);
    const int i = std::min(3, int(mc));
    const float t = mc - float(i);
    return QColor::fromRgbF(c[i].redF() + (c[i + 1].redF() - c[i].redF()) * t,
                            c[i].greenF() + (c[i + 1].greenF() - c[i].greenF()) * t,
                            c[i].blueF() + (c[i + 1].blueF() - c[i].blueF()) * t);
}

void Surface::onFrame() {
    if (!m_backend)
        return;
    const GestureView &gv = m_backend->gestureView();
    const qint64 now = m_clock.elapsed();
    // New strikes become ripples, on the screen's own clock.
    if (gv.strikeSeq - m_seenStrikes > gv.strikes.size())
        m_seenStrikes = gv.strikeSeq - uint32_t(gv.strikes.size());
    while (m_seenStrikes < gv.strikeSeq) {
        const auto &s = gv.strikes[m_seenStrikes % gv.strikes.size()];
        // Born when the finger landed, not when the screen noticed.
        const qint64 age = qint64(std::max(0.0, gv.t - s.t) * 1000.0);
        if (age < 900)
            m_ripples.push_back({s.x, s.y, s.vel, s.material, now - age});
        ++m_seenStrikes;
    }
    m_ripples.erase(std::remove_if(m_ripples.begin(), m_ripples.end(),
                                   [&](const Ripple &r) { return now - r.born > 900; }),
                    m_ripples.end());
    if (++m_frameCount % 2 == 0)
        updateSpectrum();
    update();
}

void Surface::updateSpectrum() {
    const GestureView &gv = m_backend->gestureView();
    if (gv.root != m_mappedRoot || gv.family != m_mappedFamily || gv.brightness != m_mappedBright ||
        gv.just != m_mappedJust || m_columnFreq.empty()) {
        Tuning t;
        t.set(gv.root, gv.family, gv.brightness, gv.just);
        const int cells = 3 * t.degreesPerOctave() + 1;
        m_columnFreq.resize(kColumns);
        for (int c = 0; c < kColumns; ++c) {
            const double deg = (c + 0.5) / kColumns * cells - 0.5;
            m_columnFreq[c] = float(440.0 * std::exp2((t.baseMidi() + t.continuousSemis(deg) - 69.0) / 12.0));
        }
        m_mappedRoot = gv.root;
        m_mappedFamily = gv.family;
        m_mappedBright = gv.brightness;
        m_mappedJust = gv.just;
    }
    std::vector<float> scope(kFft);
    m_backend->engine()->copyScope(scope.data(), kFft);
    std::vector<std::complex<float>> a(kFft);
    for (int i = 0; i < kFft; ++i)
        a[i] = scope[i] * m_window[i];
    fft(a, m_twiddle);
    const float rate = m_backend->engine()->sampleRate();
    std::vector<float> &row = m_history[m_historyHead];
    for (int c = 0; c < kColumns; ++c) {
        // Power at the column's exact frequency, interpolated between bins
        // (a low note sits between two bins, not on one).
        const float bin = std::clamp(m_columnFreq[c] / rate * kFft, 1.0f, float(kFft / 2 - 2));
        const int b = int(bin);
        const float t = bin - float(b);
        const float e = std::norm(a[b]) * (1 - t) + std::norm(a[b + 1]) * t;
        const float db = 10.0f * std::log10(e + 1e-12f) - 54.0f;
        row[c] = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    }
    // A light blur across columns: a ridge, not a comb.
    std::vector<float> blurred(row);
    for (int c = 1; c + 1 < kColumns; ++c)
        blurred[c] = 0.25f * row[c - 1] + 0.5f * row[c] + 0.25f * row[c + 1];
    row = blurred;
    m_historyHead = (m_historyHead + 1) % kHistory;
}

QRectF Surface::padRect() const {
    const float aspect = m_backend ? std::clamp(m_backend->padAspect(), 1.0f, 2.4f) : 1.6f;
    const qreal margin = 18, side = 40, bottom = 92;
    const qreal availW = width() - 2 * side, availH = height() - margin - bottom;
    qreal w = availW, h = w / aspect;
    if (h > availH) {
        h = availH;
        w = h * aspect;
    }
    return QRectF((width() - w) / 2, margin, w, h);
}

void Surface::paint(QPainter *p) {
    if (!m_backend)
        return;
    p->setRenderHint(QPainter::Antialiasing);
    const GestureView &gv = m_backend->gestureView();
    const EngineView &ev = m_backend->engine()->view();
    const QRectF pad = padRect();
    const qint64 now = m_clock.elapsed();
    const qreal unit = pad.height() / 10.0;
    auto at = [&](float x, float y) { return QPointF(pad.left() + x * pad.width(), pad.bottom() - y * pad.height()); };

    Tuning tuning;
    tuning.set(gv.root, gv.family, gv.brightness, gv.just);
    const int cells = 3 * tuning.degreesPerOctave() + 1;

    // The pad itself.
    const qreal radius = std::min<qreal>(18, pad.height() * 0.04);
    QPainterPath padPath;
    padPath.addRoundedRect(pad, radius, radius);
    p->fillPath(padPath, QColor(255, 255, 255, 8));
    p->save();
    p->setClipPath(padPath);

    // Root columns: one note per octave a little lighter, so the hand finds home.
    for (int c = 0; c < cells; ++c) {
        if (c % tuning.degreesPerOctave() != 0)
            continue;
        const QRectF col(pad.left() + c * pad.width() / cells, pad.top(), pad.width() / cells, pad.height());
        p->fillRect(col, withAlpha(m_accent, 0.05f));
    }

    // The landscape: past spectra rising from the bottom, newest in front.
    for (int k = kHistory - 1; k >= 0; --k) {
        const std::vector<float> &row = m_history[(m_historyHead - 1 - k + 2 * kHistory) % kHistory];
        const qreal base = pad.bottom() - k * pad.height() / (kHistory * 1.15);
        const qreal amp = pad.height() * 0.34;
        // Curves through the midpoints between columns: ridges, not stairs.
        QPainterPath path;
        path.moveTo(pad.left(), base);
        QPointF prev(pad.left(), base - row[0] * amp);
        path.lineTo(prev);
        for (int c = 1; c < kColumns; ++c) {
            const QPointF pt(pad.left() + (c + 0.5) * pad.width() / kColumns, base - row[c] * amp);
            path.quadTo(prev, (prev + pt) / 2);
            prev = pt;
        }
        path.lineTo(pad.right(), prev.y());
        path.lineTo(pad.right(), base);
        path.closeSubpath();
        const float age = float(k) / kHistory;
        QLinearGradient fill(0, base - amp, 0, base);
        fill.setColorAt(0, withAlpha(m_accent, 0.10f * (1 - age)));
        fill.setColorAt(1, QColor(11, 11, 14, int(235 * (1 - age * 0.7f))));
        p->fillPath(path, fill);
        const float fade = (1.0f - age) * (1.0f - age) * (1.0f - age);
        if (fade > 0.03f) {
            p->setPen(QPen(withAlpha(m_accent, 0.65f * fade), k == 0 ? 1.8 : 1.0));
            p->drawPath(path);
        }
    }

    // Note grid and names.
    QFont small = p->font();
    small.setPixelSize(std::max(9, int(unit * 0.22)));
    p->setFont(small);
    for (int c = 0; c <= cells; ++c) {
        const qreal x = pad.left() + c * pad.width() / cells;
        p->setPen(QPen(QColor(255, 255, 255, 18), 1));
        p->drawLine(QPointF(x, pad.top()), QPointF(x, pad.bottom()));
    }
    for (int c = 0; c < cells; ++c) {
        const double midi = tuning.baseMidi() + tuning.degreeSemis(c);
        const int pc = int(std::lround(midi)) % 12;
        const bool root = c % tuning.degreesPerOctave() == 0;
        p->setPen(root ? withAlpha(m_accent, 0.85f) : QColor(255, 255, 255, 90));
        const QRectF cell(pad.left() + c * pad.width() / cells, pad.bottom() - unit * 0.5, pad.width() / cells,
                          unit * 0.45);
        p->drawText(cell, Qt::AlignCenter, QString::fromUtf8(Tuning::noteName(pc)));
    }
    // Material rows.
    static const char *materials[5] = {QT_TRANSLATE_NOOP("Surface", "skin"), QT_TRANSLATE_NOOP("Surface", "snare"),
                                       QT_TRANSLATE_NOOP("Surface", "wood"), QT_TRANSLATE_NOOP("Surface", "glass"),
                                       QT_TRANSLATE_NOOP("Surface", "air")};
    for (int m = 0; m < 5; ++m) {
        const qreal y = pad.bottom() - (m + 0.5) * pad.height() / 5;
        if (m > 0) {
            p->setPen(QPen(QColor(255, 255, 255, 12), 1, Qt::DashLine));
            const qreal ly = pad.bottom() - m * pad.height() / 5;
            p->drawLine(QPointF(pad.left(), ly), QPointF(pad.right(), ly));
        }
        p->save();
        p->setClipping(false);
        p->translate(pad.left() - 16, y);
        p->rotate(-90);
        p->setPen(withAlpha(materialColor(m + 0.15f), 0.55f));
        p->drawText(QRectF(-unit, -unit * 0.2, 2 * unit, unit * 0.4), Qt::AlignCenter,
                    QCoreApplication::translate("Surface", materials[m]));
        p->restore();
    }

    // Frozen voices: soft lights left in the landscape, breathing with their level.
    for (const VoiceView &v : ev.voices) {
        if (!v.active || !v.frozen)
            continue;
        const QPointF c = at(v.x, v.y);
        const qreal r = unit * (0.5 + 6.0 * std::sqrt(v.level));
        QRadialGradient g(c, r);
        const QColor col = roleColor(v.role, m_accent);
        g.setColorAt(0, withAlpha(col, 0.35f));
        g.setColorAt(1, withAlpha(col, 0.0f));
        p->setPen(Qt::NoPen);
        p->setBrush(g);
        p->drawEllipse(c, r, r);
        p->setBrush(withAlpha(col, 0.8f));
        p->drawEllipse(c, 2.5, 2.5);
    }

    // Frozen chords as layers: a ring around each one's light, solid when
    // pinned, bright while a finger holds it.
    // Each is a target: a ring of the size a fingertip can find, lettered,
    // at the chord's centre.
    int layerIndex = 0;
    for (const LayerView &l : ev.layers) {
        if (!l.active)
            continue;
        const QPointF c = at(l.x, l.y);
        const qreal r = unit * 0.62;
        const float vis = 0.35f + 0.65f * std::clamp(l.level * 12.0f, 0.0f, 1.0f);
        p->setBrush(withAlpha(QColor(10, 10, 14), 0.55f));
        p->setPen(QPen(withAlpha(l.held ? m_accent : QColor(255, 255, 255), (l.held ? 0.95f : 0.55f) * vis),
                       l.pinned ? 2.4 : 1.2, l.pinned ? Qt::SolidLine : Qt::DashLine));
        p->drawEllipse(c, r, r);
        QFont f = small;
        f.setBold(true);
        p->setFont(f);
        p->setPen(withAlpha(l.held ? m_accent : QColor(255, 255, 255), 0.85f * vis));
        QString text = QString(QChar('A' + layerIndex % 26));
        if (l.transpose != 0)
            text += QString(" %1%2").arg(l.transpose > 0 ? "+" : "").arg(l.transpose);
        p->drawText(QRectF(c.x() - 2 * r, c.y() - r, 4 * r, 2 * r), Qt::AlignCenter, text);
        p->setFont(small);
        ++layerIndex;
    }
    // Voices a loop is replaying: small lights that move as the recorded
    // finger did.
    for (const VoiceView &v : ev.voices) {
        if (!v.active || v.frozen || v.finger > -1000)
            continue;
        const QPointF c = at(v.x, v.y);
        const qreal r = unit * (0.3 + 3.0 * std::sqrt(v.level));
        QRadialGradient g(c, r);
        const QColor col = roleColor(v.role, m_accent);
        g.setColorAt(0, withAlpha(col, 0.5f));
        g.setColorAt(1, withAlpha(col, 0.0f));
        p->setPen(Qt::NoPen);
        p->setBrush(g);
        p->drawEllipse(c, r, r);
    }

    // Strike ripples.
    for (const Ripple &r : m_ripples) {
        const float t = float(now - r.born) / 900.0f;
        const QPointF c = at(r.x, r.y);
        const qreal rad = unit * (0.2 + 1.6 * t) * (0.6 + r.vel);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(withAlpha(materialColor(r.material), (1 - t) * (0.4f + 0.6f * r.vel)), 1.5 + 2.0 * (1 - t)));
        p->drawEllipse(c, rad, rad);
    }

    // Loops: rings where they were left, a dot per hit placed by its time
    // around the circle, the hand of a clock sweeping over them.
    for (const LoopView &l : ev.loops) {
        if (!l.active)
            continue;
        const qreal r = unit * (0.75 + 0.35 * l.level);
        // Kept whole inside the pad, even when left at its very edge.
        const QPointF home = at(l.homeX, l.homeY);
        const QPointF c(std::clamp(home.x(), pad.left() + r + 4, pad.right() - r - 4),
                        std::clamp(home.y(), pad.top() + r + 4, pad.bottom() - r - 4));
        const float alpha = 0.25f + 0.75f * l.level;
        QColor ring = l.held ? m_accent : QColor(255, 255, 255);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(withAlpha(ring, alpha * (l.held ? 0.9f : 0.45f)), l.pinned ? 3.0 : 1.2,
                       l.pinned ? Qt::SolidLine : Qt::DashLine));
        p->drawEllipse(c, r, r);
        if (l.flash > 0.02f) {
            QRadialGradient g(c, r * 1.4);
            g.setColorAt(0, withAlpha(m_accent, 0.25f * l.flash * alpha));
            g.setColorAt(1, withAlpha(m_accent, 0));
            p->setPen(Qt::NoPen);
            p->setBrush(g);
            p->drawEllipse(c, r * 1.4, r * 1.4);
        }
        for (int h = 0; h < l.hitCount; ++h) {
            const float a = l.hitPhase[h] * 2 * kPiF - kPiF / 2;
            const QPointF hp(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
            float dist = l.phase - l.hitPhase[h];
            if (dist < 0)
                dist += 1;
            const float lit = std::exp(-dist * 14.0f);
            p->setPen(Qt::NoPen);
            p->setBrush(withAlpha(materialColor(l.hitMaterial[h]), alpha * (0.5f + 0.5f * lit)));
            const qreal dot = 2.2 + 3.5 * lit;
            p->drawEllipse(hp, dot, dot);
        }
        // Recorded voice lines, drawn inside the ring as the pitch the finger
        // played at each moment around the bar.
        for (int k = 0; k < l.laneCount; ++k) {
            QPainterPath line;
            bool open = false;
            for (int b = 0; b <= 64; ++b) {
                const float v = l.lane[k][b % 64];
                if (v < 0) {
                    open = false;
                    continue;
                }
                const float ang = float(b) / 64.0f * 2 * kPiF - kPiF / 2;
                const qreal rr = r * (0.3 + 0.55 * v);
                const QPointF pt(c.x() + std::cos(ang) * rr, c.y() + std::sin(ang) * rr);
                if (open)
                    line.lineTo(pt);
                else
                    line.moveTo(pt);
                open = true;
            }
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(withAlpha(roleColor(l.laneRole[k], m_accent), alpha * 0.85f), 1.6));
            p->drawPath(line);
        }
        const float a = l.phase * 2 * kPiF - kPiF / 2;
        p->setPen(QPen(withAlpha(m_accent, alpha * 0.8f), 1.4));
        p->drawLine(c, QPointF(c.x() + std::cos(a) * r * 0.85, c.y() + std::sin(a) * r * 0.85));
        if (l.transpose != 0 || std::fabs(l.density) > 0.05f) {
            p->setPen(withAlpha(QColor(255, 255, 255), alpha * 0.7f));
            QString t;
            if (l.transpose)
                t += QString("%1%2").arg(l.transpose > 0 ? "+" : "").arg(l.transpose);
            if (l.density > 0.05f)
                t += (t.isEmpty() ? "" : " ") + QString("×%1").arg(1 + l.density * 3, 0, 'f', 1);
            else if (l.density < -0.05f)
                t += (t.isEmpty() ? "" : " ") + QString("%1%").arg(int((1 + l.density) * 100));
            p->drawText(QRectF(c.x() - r, c.y() - unit * 0.2, 2 * r, unit * 0.4), Qt::AlignCenter, t);
        }
    }

    // The phrase being tapped: hold on the next one and it loops.
    for (int i = 0; i < gv.phraseCount; ++i) {
        const auto &t = gv.phrase[i];
        const float age = float(gv.t - t.t);
        p->setPen(Qt::NoPen);
        p->setBrush(withAlpha(m_accent, std::max(0.15f, 0.8f - age * 0.3f)));
        p->drawEllipse(at(t.x, t.y), 3.0, 3.0);
    }

    if (gv.phraseCount > 0) {
        const auto &t = gv.phrase[gv.phraseCount - 1];
        const QPointF c = at(t.x, t.y);
        p->setPen(withAlpha(m_accent, 0.75f));
        p->setFont(small);
        const QString hint = gv.phraseCount == 1 ? tr("hold right here for a pulse")
                                                 : tr("land on the next beat and hold: loop");
        const qreal tx = std::clamp(c.x(), pad.left() + 3 * unit, pad.right() - 3 * unit);
        p->drawText(QRectF(tx - 3 * unit, c.y() - unit * 0.75, 6 * unit, unit * 0.35), Qt::AlignCenter, hint);
        if (gv.phraseNext > 0) {
            // A beat clock: closes as the next beat arrives.
            const float until = float(gv.phraseNext - gv.t);
            const float span = float(gv.phraseNext - gv.phrase[gv.phraseCount - 1].t);
            if (span > 0 && until > -0.15f) {
                const float frac = std::clamp(1.0f - until / span, 0.0f, 1.0f);
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(withAlpha(m_accent, 0.8f), 2));
                const qreal rr = unit * 0.35;
                p->drawArc(QRectF(c.x() - rr, c.y() - rr, 2 * rr, 2 * rr), 90 * 16, int(-360 * 16 * frac));
            }
        }
    }

    // Fingers draw over the pad's edge rather than being cut by it.
    p->setClipping(false);

    // Fingers.
    std::vector<QRectF> placedLabels;
    QFont label = p->font();
    label.setPixelSize(std::max(10, int(unit * 0.26)));
    label.setBold(true);
    for (int i = 0; i < gv.count; ++i) {
        const ContactView &c = gv.contacts[i];
        const QPointF pos = at(c.x, c.y);
        const qreal r = unit * 0.32 * std::clamp(c.size, 0.6f, 4.0f);
        p->save();
        p->translate(pos);
        p->rotate(-c.angle * 180.0 / kPiF);
        switch (c.state) {
        case TouchState::Landing:
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(255, 255, 255, 200), 1.5));
            p->drawEllipse(QPointF(0, 0), r, r * 0.8);
            break;
        case TouchState::Palm: {
            QRadialGradient g(QPointF(0, 0), r * 1.6);
            g.setColorAt(0, QColor(0, 0, 0, 170));
            g.setColorAt(1, QColor(0, 0, 0, 0));
            p->setPen(Qt::NoPen);
            p->setBrush(g);
            p->drawEllipse(QPointF(0, 0), r * 1.6, r * 1.2);
            break;
        }
        case TouchState::Layer: {
            QRadialGradient g(QPointF(0, 0), r * 2.2);
            g.setColorAt(0, withAlpha(m_accent, 0.25f + 0.5f * c.pressure));
            g.setColorAt(1, withAlpha(m_accent, 0));
            p->setPen(Qt::NoPen);
            p->setBrush(g);
            p->drawEllipse(QPointF(0, 0), r * 2.2, r * 2.2);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(m_accent, 2.0));
            p->drawEllipse(QPointF(0, 0), r, r);
            break;
        }
        case TouchState::Swipe:
            p->setBrush(withAlpha(m_accent, 0.5f));
            p->setPen(Qt::NoPen);
            p->drawEllipse(QPointF(0, 0), r * 0.5, r * 0.5);
            break;
        case TouchState::Spent:
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(255, 255, 255, 70), 1, Qt::DotLine));
            p->drawEllipse(QPointF(0, 0), r, r * 0.8);
            break;
        case TouchState::Voice: {
            const QColor col = roleColor(c.role, m_accent);
            QRadialGradient g(QPointF(0, 0), r * 2.4);
            g.setColorAt(0, withAlpha(col, 0.45f + 0.4f * c.pressure));
            g.setColorAt(1, withAlpha(col, 0));
            p->setPen(Qt::NoPen);
            p->setBrush(g);
            p->drawEllipse(QPointF(0, 0), r * 2.4, r * 2.0);
            p->setBrush(withAlpha(col, 0.9f));
            p->drawEllipse(QPointF(0, 0), r * 0.55, r * 0.45);
            break;
        }
        case TouchState::Handle: {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(m_accent, 2.5));
            p->drawEllipse(QPointF(0, 0), r, r);
            p->setPen(QPen(withAlpha(m_accent, 0.6f), 1.2, Qt::DashLine));
            p->rotate(now * 0.12);
            p->drawEllipse(QPointF(0, 0), r * 1.5, r * 1.5);
            break;
        }
        }
        p->restore();

        if ((c.state == TouchState::Voice || c.state == TouchState::Spent) && c.role >= 0) {
            // The knob this finger is turning: slide on the outer arc, twist
            // on the inner one. A finger whose chord froze keeps its knob.
            QColor col = roleColor(c.role, m_accent);
            if (c.state == TouchState::Spent)
                col = withAlpha(col, 0.6f);
            if (c.recording) {
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(QColor("#ef4444"), 2, Qt::DashLine));
                p->drawEllipse(pos, r * 1.25, r * 1.25);
            }
            const qreal kr = r + unit * 0.5;
            const float a = gv.knobs[c.role][0], b = gv.knobs[c.role][1];
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(255, 255, 255, 30), 3, Qt::SolidLine, Qt::RoundCap));
            p->drawArc(QRectF(pos.x() - kr, pos.y() - kr, 2 * kr, 2 * kr), 225 * 16, -270 * 16);
            p->setPen(QPen(col, 3, Qt::SolidLine, Qt::RoundCap));
            p->drawArc(QRectF(pos.x() - kr, pos.y() - kr, 2 * kr, 2 * kr), 225 * 16, int(-270 * 16 * a));
            const qreal kr2 = kr - 6;
            p->setPen(QPen(QColor(255, 255, 255, 25), 2, Qt::SolidLine, Qt::RoundCap));
            p->drawArc(QRectF(pos.x() - kr2, pos.y() - kr2, 2 * kr2, 2 * kr2), 225 * 16, -270 * 16);
            p->setPen(QPen(withAlpha(col, 0.6f), 2, Qt::SolidLine, Qt::RoundCap));
            p->drawArc(QRectF(pos.x() - kr2, pos.y() - kr2, 2 * kr2, 2 * kr2), 225 * 16, int(-270 * 16 * b));
            p->setFont(label);
            p->setPen(col);
            const qreal lx = std::clamp(pos.x(), pad.left() + 1.4 * unit, pad.right() - 1.4 * unit);
            // Above the finger, unless another finger's label is already
            // there; then below it.
            const QString text = QString("%1  %2").arg(c.role + 1).arg(m_backend->roleName(c.role));
            const qreal tw = QFontMetricsF(label).horizontalAdvance(text) + 8;
            QRectF box(lx - tw / 2, pos.y() - kr - unit * 0.55, tw, unit * 0.4);
            for (const QRectF &o : placedLabels)
                if (o.intersects(box)) {
                    box.moveTop(pos.y() + kr + unit * 0.12);
                    break;
                }
            placedLabels.push_back(box);
            p->drawText(box, Qt::AlignCenter, text);
            int voicesHeld = 0;
            for (int j = 0; j < gv.count; ++j)
                voicesHeld += gv.contacts[j].state == TouchState::Voice;
            if (voicesHeld > 3 || c.state == TouchState::Spent)
                continue;
            p->setFont(small);
            p->setPen(QColor(255, 255, 255, 150));
            const qreal vx = std::clamp(pos.x(), pad.left() + 2.5 * unit, pad.right() - 2.5 * unit);
            p->drawText(QRectF(vx - 3 * unit, pos.y() + kr + unit * 0.08, 6 * unit, unit * 0.35),
                        Qt::AlignCenter,
                        QString("%1 %2 · %3 %4")
                            .arg(m_backend->roleSlide(c.role))
                            .arg(int(a * 100))
                            .arg(m_backend->roleTwist(c.role))
                            .arg(int(b * 100)));
        } else if (c.state == TouchState::Handle) {
            p->setFont(label);
            p->setPen(m_accent);
            p->drawText(QRectF(pos.x() - 3 * unit, pos.y() - r * 1.5 - unit * 0.5, 6 * unit, unit * 0.4),
                        Qt::AlignCenter, tr("loop"));
        } else if (c.state == TouchState::Layer) {
            p->setFont(label);
            p->setPen(m_accent);
            p->drawText(QRectF(pos.x() - 3 * unit, pos.y() - r * 2.2 - unit * 0.3, 6 * unit, unit * 0.4),
                        Qt::AlignCenter, tr("layer"));
        } else if (c.state == TouchState::Palm) {
            p->setFont(small);
            p->setPen(QColor(255, 255, 255, 140));
            p->drawText(QRectF(pos.x() - 3 * unit, pos.y() - unit * 0.2, 6 * unit, unit * 0.4), Qt::AlignCenter,
                        tr("damper"));
        }
    }
    p->restore();

    // Pad edge, lit by the damper's opposite: the more open, the brighter.
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(withAlpha(m_accent, 0.25f + 0.35f * (1.0f - ev.damper)), 1.2));
    p->drawPath(padPath);

    // The ten knobs, always in view, so a player learns them by looking down.
    const qreal stripTop = pad.bottom() + 16;
    const qreal cellW = std::min<qreal>(pad.width() / int(RoleCount), 150);
    const qreal stripLeft = width() / 2 - cellW * int(RoleCount) / 2;
    bool held[RoleCount] = {};
    for (int i = 0; i < gv.count; ++i)
        if (gv.contacts[i].state == TouchState::Voice && gv.contacts[i].role >= 0)
            held[gv.contacts[i].role] = true;
    QFont knobFont = p->font();
    knobFont.setPixelSize(11);
    for (int r = 0; r < RoleCount; ++r) {
        const QRectF cell(stripLeft + r * cellW + 4, stripTop, cellW - 8, 64);
        const QColor col = roleColor(r, m_accent);
        p->setPen(Qt::NoPen);
        p->setBrush(held[r] ? withAlpha(col, 0.16f) : QColor(255, 255, 255, 6));
        p->drawRoundedRect(cell, std::min<qreal>(6, radius), std::min<qreal>(6, radius));
        knobFont.setBold(true);
        p->setFont(knobFont);
        p->setPen(held[r] ? col : withAlpha(col, 0.7f));
        p->drawText(cell.adjusted(8, 6, -8, 0), Qt::AlignLeft | Qt::AlignTop,
                    QString("%1 %2").arg(r + 1).arg(m_backend->roleName(r)));
        knobFont.setBold(false);
        p->setFont(knobFont);
        for (int axis = 0; axis < 2; ++axis) {
            const qreal y = cell.top() + 28 + axis * 17;
            const QRectF track(cell.left() + 8, y + 10, cell.width() - 16, 3);
            p->setPen(QColor(255, 255, 255, 110));
            p->drawText(QRectF(cell.left() + 8, y - 4, cell.width() - 16, 14), Qt::AlignLeft | Qt::AlignVCenter,
                        axis == 0 ? m_backend->roleSlide(r) : m_backend->roleTwist(r));
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 255, 255, 22));
            p->drawRoundedRect(track, 1.5, 1.5);
            p->setBrush(withAlpha(col, axis == 0 ? 0.95f : 0.6f));
            p->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * gv.knobs[r][axis], 3), 1.5, 1.5);
        }
    }
}
