// The arrange page: every part's clips in one grid, to play a track from
// the loops made on the other pages. A starter until it has its own design:
// tap a slot to start it at the next bar (or stop it), slide on a part's
// name to set its level.

#include <QPainter>
#include <array>

#include "console.h"
#include "parts.h"

namespace tf {

namespace {

class ArrangeControl : public PartControl {
public:
    ArrangeControl(QStringList names, std::vector<QColor> colors)
        : m_names(std::move(names)), m_colors(std::move(colors)) {}

    static constexpr float kNames = 0.14f; // the names' column, as a share of the width

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        const int parts = std::max(1, ctx.studio().partCount);
        for (auto &h : m_held) {
            if (h.id < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == h.id;
            if (!still)
                h.id = -1;
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Held *h = find(t.id);
            const int part = std::clamp(int((1.0f - t.y) * float(parts)), 0, parts - 1);
            if (!h) {
                h = find(-1);
                if (!h)
                    continue;
                *h = {t.id, part, t.y, ctx.studio().parts[part].channel.value[Gain], t.x < kNames};
                if (!h->level) {
                    const int slot = std::clamp(int((t.x - kNames) / (1.0f - kNames) * kClipSlots), 0, kClipSlots - 1);
                    const PartView &v = ctx.studio().parts[part];
                    const auto state = v.clips[slot].state;
                    if (state == ClipView::Playing || state == ClipView::Queued)
                        ctx.stopPart(part);
                    else if (state != ClipView::Empty)
                        ctx.launchPart(part, slot);
                }
                continue;
            }
            if (h->level)
                ctx.setChannel(h->part, Gain, std::clamp(h->gain0 + (t.y - h->y0) * 2.5f, 0.0f, 1.0f));
        }
    }

    void leave(ControlContext &) override {
        for (auto &h : m_held)
            h.id = -1;
        m_frame = Frame{};
    }

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        if (!k.down)
            return false;
        // F1–F8 launch a whole column: every part's clip in that slot.
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int slot = k.key - Qt::Key_F1;
            for (int p = 0; p < ctx.studio().partCount; ++p) {
                if (ctx.studio().parts[p].clips[slot].state != ClipView::Empty)
                    ctx.launchPart(p, slot);
                else
                    ctx.stopPart(p);
            }
            return true;
        }
        return false;
    }

    void paint(QPainter &p, const PaintContext &pc) override {
        namespace c = console;
        c::pad(p, pc, QStringLiteral("Arrange"));
        const int parts = std::max(1, pc.studio->partCount);
        for (int i = 0; i < parts; ++i) {
            const float y1 = 1.0f - float(i) / parts, y0 = 1.0f - float(i + 1) / parts;
            const QColor col = i < int(m_colors.size()) ? m_colors[size_t(i)] : pc.color;
            const PartView &v = pc.studio->parts[i];
            const QRectF name = pc.rect(0, y0, kNames, y1).adjusted(4, 4, -4, -4);
            c::text(p, name.adjusted(4, 0, 0, -name.height() / 2), QStringLiteral("%1 %2").arg(i + 1).arg(m_names.value(i)),
                    col, 13, true);
            c::gauge(p, QRectF(name.left() + 4, name.center().y() + 4, name.width() - 8, 6), v.channel.value[Gain], col);
            c::gauge(p, QRectF(name.left() + 4, name.center().y() + 14, name.width() - 8, 4),
                     std::min(1.0f, v.channel.rms * 4.0f), c::alpha(col, 0.7f));
            const QRectF strip = pc.rect(kNames, y0, 1, y1).adjusted(2, 6, -2, -6);
            c::clips(p, strip, v, col, pc.time);
        }
        c::grid(p, pc, 1, parts, pc.color, 0.2f);
        for (int i = 0; i < m_frame.count; ++i)
            c::finger(p, pc, m_frame.touches[i], pc.color);
        c::beats(p, pc, pc.color);
    }

    QString help() const override {
        return QStringLiteral("tap a slot\tstarts it at the next bar; tap a playing one to stop\n"
                              "slide on a part's name\tits level\n"
                              "F1–F8\tlaunch that column across every part");
    }

private:
    struct Held {
        int id = -1, part = 0;
        float y0 = 0, gain0 = 0;
        bool level = false;
    };
    Held *find(int id) {
        for (auto &h : m_held)
            if (h.id == id)
                return &h;
        return nullptr;
    }
    QStringList m_names;
    std::vector<QColor> m_colors;
    std::array<Held, 16> m_held{};
    Frame m_frame;
};

}

PartSpec makeArrange(const QStringList &names, const std::vector<QColor> &colors) {
    PartSpec s;
    s.name = QStringLiteral("Arrange");
    s.blurb = QStringLiteral("clips into a track");
    s.color = QColor("#FFD60A");
    s.control = std::make_unique<ArrangeControl>(names, colors);
    return s;
}

}
