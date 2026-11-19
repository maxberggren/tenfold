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

// The console's lettering: a condensed monospace in capitals.
QFont consoleFont(int px, bool bold = false) {
    QFont f("Noto Sans Mono");
    f.setPixelSize(std::max(9, px));
    f.setStretch(QFont::SemiCondensed);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    f.setWeight(bold ? QFont::Bold : QFont::DemiBold);
    return f;
}

constexpr QRgb kInk = 0xff0a0806; // text on a lit (inverse) field

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
    // A phosphor each: amber, violet, cyan, blue, pink, green, orange,
    // magenta, lime, red.
    static const char *colors[RoleCount] = {nullptr,   "#A98BFF", "#2EE6FF", "#4DA3FF", "#FF6FA8",
                                            "#39FF88", "#FF8A1F", "#FF4FD8", "#B6FF3B", "#FF4136"};
    if (role <= 0 || role >= RoleCount)
        return accent;
    return QColor(colors[role]);
}

QColor Surface::materialColor(float m) {
    static const QColor c[5] = {QColor("#FF8A1F"), QColor("#FF6FA8"), QColor("#E0A060"), QColor("#2EE6FF"),
                                QColor("#E8F0FF")};
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
    const qreal margin = 14, side = 30, bottom = 98;
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
    const qreal radius = 0;
    QPainterPath padPath;
    padPath.addRect(pad);
    p->fillPath(padPath, QColor(0, 0, 0));
    p->save();
    p->setClipPath(padPath);

    // Root columns: one note per octave a little lighter, so the hand finds home.
    for (int c = 0; c < cells; ++c) {
        if (c % tuning.degreesPerOctave() != 0)
            continue;
        const QRectF col(pad.left() + c * pad.width() / cells, pad.top(), pad.width() / cells, pad.height());
        p->fillRect(col, withAlpha(m_accent, 0.07f));
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
        // Green phosphor ridges, each hiding the ones behind it.
        const QColor phosphor("#39FF88");
        p->fillPath(path, QColor(0, 0, 0, int(235 * (1 - age * 0.6f))));
        const float fade = (1.0f - age) * (1.0f - age) * (1.0f - age);
        if (fade > 0.03f) {
            p->setPen(QPen(withAlpha(phosphor, 0.85f * fade), k == 0 ? 2.0 : 1.1));
            p->drawPath(path);
        }
    }

    // Note grid and names.
    QFont small = consoleFont(int(unit * 0.22));
    p->setFont(small);
    for (int c = 0; c <= cells; ++c) {
        const qreal x = pad.left() + c * pad.width() / cells;
        p->setPen(QPen(withAlpha(m_accent, 0.16f), 1, Qt::DotLine));
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
            p->setPen(QPen(withAlpha(m_accent, 0.12f), 1, Qt::DashLine));
            const qreal ly = pad.bottom() - m * pad.height() / 5;
            p->drawLine(QPointF(pad.left(), ly), QPointF(pad.right(), ly));
        }
        p->save();
        p->setClipping(false);
        p->translate(pad.left() - 16, y);
        p->rotate(-90);
        p->setPen(withAlpha(materialColor(float(m)), 0.9f));
        p->drawText(QRectF(-unit, -unit * 0.2, 2 * unit, unit * 0.4), Qt::AlignCenter,
                    QCoreApplication::translate("Surface", materials[m]));
        p->restore();
    }

    // Frozen voices: soft lights left in the landscape, breathing with their level.
    for (const VoiceView &v : ev.voices) {
        if (!v.active || !v.frozen)
            continue;
        // A crisp diamond for each frozen note, its size its level, over a
        // faint halo.
        const QPointF c = at(v.x, v.y);
        const QColor col = roleColor(v.role, m_accent);
        const qreal halo = unit * (0.4 + 2.5 * std::sqrt(v.level));
        QRadialGradient g(c, halo);
        g.setColorAt(0, withAlpha(col, 0.12f));
        g.setColorAt(1, withAlpha(col, 0.0f));
        p->setPen(Qt::NoPen);
        p->setBrush(g);
        p->drawEllipse(c, halo, halo);
        const qreal d = 4 + unit * 0.6 * std::sqrt(v.level * 6.0f);
        QPolygonF diamond({c + QPointF(0, -d), c + QPointF(d, 0), c + QPointF(0, d), c + QPointF(-d, 0)});
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(withAlpha(col, 0.9f), 1.6));
        p->drawPolygon(diamond);
    }

    // Frozen chords as layers: a ring around each one's light, solid when
    // pinned, bright while a finger holds it.
    // Each is a target: a square console box the size a fingertip can find,
    // with its letter on a lit tag; solid when pinned, lit while held.
    int layerIndex = 0;
    for (const LayerView &l : ev.layers) {
        if (!l.active)
            continue;
        const QPointF c = at(l.x, l.y);
        const qreal half = unit * 0.55;
        const QRectF box(c.x() - half, c.y() - half, 2 * half, 2 * half);
        const QColor col = l.held ? m_accent : QColor("#39FF88");
        const float vis = 0.45f + 0.55f * std::clamp(l.level * 12.0f, 0.0f, 1.0f);
        p->setBrush(QColor(0, 0, 0, 200));
        p->setPen(QPen(withAlpha(col, vis), l.pinned ? 3.0 : 1.6));
        p->drawRect(box);
        // Corner ticks, like a target reticle.
        p->setPen(QPen(withAlpha(col, vis), 2));
        const qreal t = half * 0.45;
        for (int sx : {-1, 1})
            for (int sy : {-1, 1}) {
                const QPointF corner(c.x() + sx * (half + 4), c.y() + sy * (half + 4));
                p->drawLine(corner, corner - QPointF(sx * t, 0));
                p->drawLine(corner, corner - QPointF(0, sy * t));
            }
        QString text = QString(QChar('A' + layerIndex % 26));
        if (l.transpose != 0)
            text += QString(" %1%2").arg(l.transpose > 0 ? "+" : "").arg(l.transpose);
        const QFont tagFont = consoleFont(int(unit * 0.26), true);
        p->setFont(tagFont);
        const qreal tw = QFontMetricsF(tagFont).horizontalAdvance(text) + 10;
        const QRectF tag(c.x() - tw / 2, c.y() - unit * 0.2, tw, unit * 0.4);
        p->fillRect(tag, withAlpha(col, vis));
        p->setPen(QColor::fromRgb(kInk));
        p->drawText(tag, Qt::AlignCenter, text);
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
        // Its wetness, as a haze around the ring: the higher a loop sits,
        // the more of it goes to the echo and the room.
        {
            const float wetness = std::pow(std::clamp(l.homeY, 0.0f, 1.0f), 1.3f);
            const qreal hr = r * (1.15 + 0.9 * wetness);
            QRadialGradient haze(c, hr);
            haze.setColorAt(r / hr, withAlpha(QColor("#93c5fd"), 0.18f * wetness * alpha));
            haze.setColorAt(1, withAlpha(QColor("#93c5fd"), 0));
            p->setPen(Qt::NoPen);
            p->setBrush(haze);
            p->drawEllipse(c, hr, hr);
        }
        // A dial: a solid ring with twelve ticks, magenta like every loop;
        // amber while a finger holds it, heavier when pinned.
        const QColor ring = l.held ? m_accent : QColor("#FF4FD8");
        p->setBrush(QColor(0, 0, 0, 170));
        p->setPen(QPen(withAlpha(ring, alpha), l.pinned ? 3.0 : 1.6));
        p->drawEllipse(c, r, r);
        for (int tick = 0; tick < 12; ++tick) {
            const float ta = float(tick) / 12.0f * 2 * kPiF;
            const qreal len = tick % 3 == 0 ? 7 : 4;
            p->drawLine(QPointF(c.x() + std::cos(ta) * r, c.y() + std::sin(ta) * r),
                        QPointF(c.x() + std::cos(ta) * (r + len), c.y() + std::sin(ta) * (r + len)));
        }
        if (l.flash > 0.02f) {
            // A hit lights the inside of the dial for a moment.
            p->setPen(Qt::NoPen);
            p->setBrush(withAlpha(ring, 0.22f * l.flash * alpha));
            p->drawEllipse(c, r * 0.92, r * 0.92);
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
        p->setPen(QPen(withAlpha(ring, alpha), 2.0));
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
    QFont label = consoleFont(int(unit * 0.26), true);
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
            const qreal tw = QFontMetricsF(label).horizontalAdvance(text) + 10;
            QRectF box(lx - tw / 2, pos.y() - kr - unit * 0.55, tw, unit * 0.4);
            for (const QRectF &o : placedLabels)
                if (o.intersects(box)) {
                    box.moveTop(pos.y() + kr + unit * 0.12);
                    break;
                }
            placedLabels.push_back(box);
            // An inverse-video tag, like a lit console legend.
            p->setPen(Qt::NoPen);
            p->setBrush(col);
            p->drawRect(box.adjusted(-3, -1, 3, 1));
            p->setPen(QColor::fromRgb(kInk));
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

    // The pad's frame, dimmed by the damper, with its legend cut into the
    // top edge.
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(withAlpha(m_accent, 0.55f + 0.45f * (1.0f - ev.damper)), 2.0));
    p->drawPath(padPath);
    {
        const QFont legendFont = consoleFont(13, true);
        p->setFont(legendFont);
        const QString legend = tr("Trackpad · %1 notes · 5 materials").arg(cells);
        const qreal lw = QFontMetricsF(legendFont).horizontalAdvance(legend) + 16;
        const QRectF lr(pad.left() + 16, pad.top() - 9, lw, 18);
        p->fillRect(lr, QColor(0, 0, 0));
        p->setPen(m_accent);
        p->drawText(lr, Qt::AlignCenter, legend);
    }

    // The ten knobs, always in view, so a player learns them by looking down:
    // a console button each, lit while a finger holds it, with a segmented
    // gauge for the slide and one for the twist.
    const qreal stripTop = pad.bottom() + 14;
    const qreal cellW = pad.width() / int(RoleCount);
    const qreal stripLeft = pad.left();
    bool held[RoleCount] = {};
    for (int i = 0; i < gv.count; ++i)
        if ((gv.contacts[i].state == TouchState::Voice || gv.contacts[i].state == TouchState::Spent) &&
            gv.contacts[i].role >= 0)
            held[gv.contacts[i].role] = true;
    const QFont head = consoleFont(int(std::clamp(cellW / 9.5, 9.0, 13.0)), true),
                body = consoleFont(int(std::clamp(cellW / 12.0, 8.0, 10.0)));
    constexpr int kSegments = 10;
    for (int r = 0; r < RoleCount; ++r) {
        const QRectF cell(stripLeft + r * cellW + 3, stripTop, cellW - 6, 78);
        const QColor col = roleColor(r, m_accent);
        const QRectF header(cell.left(), cell.top(), cell.width(), 22);
        p->setPen(QPen(col, 2));
        p->setBrush(Qt::NoBrush);
        p->drawRect(cell);
        if (held[r])
            p->fillRect(header, col);
        p->setFont(head);
        p->setPen(held[r] ? QColor::fromRgb(kInk) : col);
        const QRectF headerText = header.adjusted(6, 0, -4, 0);
        p->drawText(headerText, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetricsF(head).elidedText(QString("%1 %2").arg(r + 1).arg(m_backend->roleName(r)),
                                                   Qt::ElideRight, headerText.width()));
        p->setFont(body);
        for (int axis = 0; axis < 2; ++axis) {
            const qreal y = header.bottom() + 6 + axis * 25;
            p->setPen(withAlpha(col, 0.85f));
            p->drawText(QRectF(cell.left() + 7, y, cell.width() - 14, 11), Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetricsF(body).elidedText(axis == 0 ? m_backend->roleSlide(r) : m_backend->roleTwist(r),
                                                       Qt::ElideRight, cell.width() - 14));
            const qreal segW = (cell.width() - 14) / kSegments;
            const int lit = int(std::lround(gv.knobs[r][axis] * kSegments));
            for (int k = 0; k < kSegments; ++k) {
                const QRectF seg(cell.left() + 7 + k * segW, y + 13, segW - 2, 7);
                if (k < lit)
                    p->fillRect(seg, axis == 0 ? col : withAlpha(col, 0.65f));
                else
                    p->fillRect(seg, withAlpha(col, 0.13f));
            }
        }
    }
}
