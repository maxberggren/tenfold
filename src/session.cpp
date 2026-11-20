#include "session.h"

#include <QPainter>
#include <Qt>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

#include "parts.h"

namespace tf {

struct Session::Page {
    PartSpec spec;
    int part = -1; // the studio's index, or −1 for the arrange page
    std::unique_ptr<Context> ctx;
};

class Session::Context : public ControlContext {
public:
    Context(Session *s, int part) : m_s(s), m_part(part) {}

    int part() const override { return m_part; }
    void send(const Command &c) override {
        if (m_part < 0)
            return;
        // Onsets, for Space to know what was just played.
        if (!(c.flags & (Command::NoRecord | Command::Continue)) &&
            (c.type == Command::On || (c.flags & Command::Quantize))) {
            onsets[onsetHead % onsets.size()] = c.t > 0 ? c.t : now();
            ++onsetHead;
        }
        StudioMsg m;
        m.kind = StudioMsg::ToPart;
        m.part = int8_t(m_part);
        m.cmd = c;
        post(m);
    }
    const StudioView &studio() const override { return m_s->m_view; }
    double beatAt(double t) const override {
        const StudioView &v = m_s->m_view;
        return v.beat + (t - v.time) * v.bpm / 60.0;
    }
    double timeAt(double beat) const override {
        const StudioView &v = m_s->m_view;
        return v.time + (beat - v.beat) * 60.0 / std::max(1.0, v.bpm);
    }
    double now() const override { return m_s->now(); }

    void capture(int slot, double t0, double t1, float quantize) override {
        StudioMsg m;
        m.kind = StudioMsg::Capture;
        m.part = int8_t(m_part);
        m.slot = int8_t(slot);
        m.d0 = t0;
        m.d1 = t1;
        m.f = quantize;
        post(m);
    }
    void record(int slot, int maxBars) override { simple(StudioMsg::Record, m_part, slot, maxBars); }
    void stopRecording() override { simple(StudioMsg::StopRecord, m_part, 0, 0); }
    void overdub(bool on) override { simple(StudioMsg::Overdub, m_part, 0, on); }
    void launch(int slot, bool now) override { launchPart(m_part, slot, now); }
    void stop(bool now) override { stopPart(m_part, now); }
    void clear(int slot) override { clearPart(m_part, slot); }

    void launchPart(int part, int slot, bool now) override { simple(StudioMsg::Launch, part, slot, now); }
    void stopPart(int part, bool now) override { simple(StudioMsg::Stop, part, 0, now); }
    void clearPart(int part, int slot) override { simple(StudioMsg::Clear, part, slot, 0); }
    void setChannel(int part, int channel, float value, double t) override {
        StudioMsg m;
        m.kind = StudioMsg::SetChannel;
        m.part = int8_t(part);
        m.slot = int8_t(channel);
        m.f = value;
        m.d0 = t;
        post(m);
        // Shown at once, before the studio's next view says so, unless it is
        // set for later: then the view says so when it happens.
        if (t <= now() && part >= 0 && part < kMaxParts && channel >= 0 && channel < ChannelCount)
            m_s->m_view.parts[part].channel.value[channel] = value;
    }
    void setMaster(int master, float value, double t) override {
        StudioMsg m;
        m.kind = StudioMsg::SetMaster;
        m.slot = int8_t(master);
        m.f = value;
        m.d0 = t;
        post(m);
        if (t <= now() && master >= 0 && master < MasterCount)
            m_s->m_view.master[master] = value;
    }
    void setTempo(double bpm) override {
        StudioMsg m;
        m.kind = StudioMsg::Tempo;
        m.d0 = bpm;
        post(m);
        m_s->m_view.bpm = std::clamp(bpm, 40.0, 240.0);
    }
    void setKey(int root, int family, int brightness) override {
        StudioMsg m;
        m.kind = StudioMsg::Key;
        m.part = int8_t(((root % 12) + 12) % 12);
        m.slot = int8_t(family);
        m.value = brightness;
        post(m);
        m_s->m_view.root = m.part;
        m_s->m_view.family = family;
        m_s->m_view.brightness = brightness;
    }
    float aspect() const override { return m_s->m_aspect; }
    void notice(const QString &text) override {
        m_s->m_notice = text;
        ++m_s->m_noticeSeq;
    }

    std::array<double, 128> onsets{};
    size_t onsetHead = 0;
    void post(const StudioMsg &m) { m_s->m_studio->post(m); }

private:
    void simple(StudioMsg::Kind k, int part, int slot, int value) {
        StudioMsg m;
        m.kind = k;
        m.part = int8_t(part);
        m.slot = int8_t(slot);
        m.value = value;
        post(m);
    }
    Session *m_s;
    int m_part;
};

Session::Session(Studio *studio) : m_studio(studio) {
    std::vector<PartSpec> parts = makeParts();
    QStringList names;
    std::vector<QColor> colors;
    for (auto &spec : parts) {
        names << spec.name;
        colors.push_back(spec.color);
    }
    for (auto &spec : parts) {
        auto page = std::make_unique<Page>();
        page->part = m_studio->addPart(std::move(spec.engine), spec.channel);
        page->spec = std::move(spec);
        page->ctx = std::make_unique<Context>(this, page->part);
        m_pages.push_back(std::move(page));
    }
    m_partCount = int(m_pages.size());
    auto arrange = std::make_unique<Page>();
    arrange->spec = makeArrange(names, colors);
    arrange->part = -1;
    arrange->ctx = std::make_unique<Context>(this, -1);
    m_pages.push_back(std::move(arrange));
    m_view.partCount = m_partCount;
    for (int i = 0; i < m_partCount && i < kMaxParts; ++i)
        for (int c = 0; c < ChannelCount; ++c)
            m_view.parts[i].channel.value[c] = m_pages[size_t(i)]->spec.channel[c];
    m_pages[0]->spec.control->enter(*m_pages[0]->ctx);
}

Session::~Session() = default;

double Session::now() const {
    if (m_clock)
        return m_clock();
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

Session::Context &Session::context(int page) { return *m_pages[size_t(page)]->ctx; }

void Session::setPage(int page) {
    std::lock_guard lock(m_lock);
    page = std::clamp(page, 0, pageCount() - 1);
    if (page == m_page)
        return;
    Page &old = *m_pages[size_t(m_page)];
    old.spec.control->leave(*old.ctx);
    // Fingers still down belong to the page they landed on: the next page
    // sees only fingers that land after it appears.
    m_ignored.clear();
    for (int i = 0; i < m_last.count; ++i)
        m_ignored.push_back(m_last.touches[i].id);
    m_page = page;
    Page &next = *m_pages[size_t(m_page)];
    next.spec.control->enter(*next.ctx);
}

QString Session::pageName(int page) const {
    return page >= 0 && page < pageCount() ? m_pages[size_t(page)]->spec.name : QString();
}
QString Session::pageBlurb(int page) const {
    return page >= 0 && page < pageCount() ? m_pages[size_t(page)]->spec.blurb : QString();
}
QColor Session::pageColor(int page) const {
    return page >= 0 && page < pageCount() ? m_pages[size_t(page)]->spec.color : QColor();
}
QString Session::pageHelp(int page) const {
    std::lock_guard lock(m_lock);
    return page >= 0 && page < pageCount() ? m_pages[size_t(page)]->spec.control->help() : QString();
}
QString Session::pageStatus() const {
    std::lock_guard lock(m_lock);
    return m_pages[size_t(m_page)]->spec.control->status();
}

void Session::frame(const Frame &f) {
    std::lock_guard lock(m_lock);
    m_lastFrame = f.t;
    m_last = f;
    for (auto &page : m_pages)
        page->spec.control->background(f.t, *page->ctx);
    Page &p = *m_pages[size_t(m_page)];
    if (m_ignored.empty()) {
        p.spec.control->frame(f, *p.ctx);
        return;
    }
    Frame g = f;
    g.count = 0;
    std::vector<int> still;
    for (int i = 0; i < f.count; ++i) {
        const int id = f.touches[i].id;
        if (std::find(m_ignored.begin(), m_ignored.end(), id) != m_ignored.end())
            still.push_back(id);
        else
            g.touches[g.count++] = f.touches[i];
    }
    m_ignored = still;
    p.spec.control->frame(g, *p.ctx);
}

void Session::idle(double t) {
    std::lock_guard lock(m_lock);
    for (auto &page : m_pages)
        page->spec.control->background(t, *page->ctx);
    Page &p = *m_pages[size_t(m_page)];
    p.spec.control->idle(t, *p.ctx);
}

bool Session::key(const KeyEvent &k) {
    std::lock_guard lock(m_lock);
    const bool ctrl = k.modifiers & Qt::ControlModifier;
    const bool shift = k.modifiers & Qt::ShiftModifier;
    if (k.down && !ctrl) {
        if (k.key >= Qt::Key_1 && k.key <= Qt::Key_9) {
            const int page = k.key - Qt::Key_1;
            if (page < m_partCount)
                setPage(page);
            return true;
        }
        if (k.key == Qt::Key_0) {
            setPage(arrangePage());
            return true;
        }
        if (k.key == Qt::Key_Tab || k.key == Qt::Key_Backtab) {
            const int step = (k.key == Qt::Key_Backtab || shift) ? -1 : 1;
            setPage((m_page + step + pageCount()) % pageCount());
            return true;
        }
    }
    if (ctrl) {
        if (!k.down)
            return true;
        switch (k.key) {
        case Qt::Key_Up: shiftTempo(shift ? 5 : 1); return true;
        case Qt::Key_Down: shiftTempo(shift ? -5 : -1); return true;
        case Qt::Key_Left: shiftRoot(-1); return true;
        case Qt::Key_Right: shiftRoot(1); return true;
        case Qt::Key_BracketLeft: shiftMode(1); return true;
        case Qt::Key_BracketRight: shiftMode(-1); return true;
        case Qt::Key_M: toggleMetronome(); return true;
        case Qt::Key_Backspace: stopAll(); return true;
        default: return false;
        }
    }
    if (k.key == Qt::Key_Escape || k.key == Qt::Key_Question)
        return false;
    Page &p = *m_pages[size_t(m_page)];
    if (p.spec.control->key(k, *p.ctx))
        return true;
    // Space records a loop on any part's page that doesn't record its own
    // way: from the next bar into the next free slot, and Space again closes
    // it at the bar after.
    if (k.key == Qt::Key_Space) {
        if (k.down && !k.repeat && p.part >= 0)
            toggleRecording(*p.ctx);
        return true;
    }
    return false;
}

void Session::refresh() {
    std::lock_guard lock(m_lock);
    if (m_studio->fetchView())
        m_view = m_studio->view();
}

void Session::paint(QPainter &painter, const QRectF &pad, double time) {
    std::lock_guard lock(m_lock);
    Page &p = *m_pages[size_t(m_page)];
    PaintContext pc;
    pc.pad = pad;
    pc.studio = &m_view;
    pc.view = p.part >= 0 ? &m_view.parts[p.part] : nullptr;
    pc.color = p.spec.color;
    pc.time = time;
    pc.beat = context(m_page).beatAt(time);
    pc.scale = float(pad.height() / 600.0);
    painter.save();
    p.spec.control->paint(painter, pc);
    painter.restore();
}

QString Session::notice(uint32_t *seq) const {
    std::lock_guard lock(m_lock);
    if (seq)
        *seq = m_noticeSeq;
    return m_notice;
}

void Session::toggleRecording(ControlContext &base) {
    // Space loops what was just played. Play first, then press Space:
    //   * nothing else playing: press it where the phrase would start again
    //     (the next ONE). The phrase becomes the loop and sets the tempo.
    //   * something playing: the bars that hold the phrase become the loop,
    //     ending at the bar line just passed (pressed within a beat of it) or
    //     the next one. It carries on from there without a seam.
    // Space again while a take waits calls it off.
    Context &ctx = static_cast<Context &>(base);
    const int part = ctx.part();
    const PartView &v = m_view.parts[part];
    if (v.recording >= 0) {
        StudioMsg m;
        m.kind = StudioMsg::CancelRecord;
        m.part = int8_t(part);
        ctx.post(m);
        m_view.parts[part].recording = -1;
        ctx.notice(QStringLiteral("Loop called off"));
        return;
    }
    const int slot = ctx.freeSlot();
    if (slot < 0) {
        ctx.notice(QStringLiteral("Every slot is full: clear one first"));
        return;
    }
    const double nowT = now();
    const double spb = 60.0 / std::max(1.0, m_view.bpm);
    // The phrase: onsets back from the latest, until a pause of two bars.
    std::vector<double> times;
    const size_t n = std::min(ctx.onsetHead, ctx.onsets.size());
    for (size_t i = 0; i < n; ++i)
        times.push_back(ctx.onsets[(ctx.onsetHead - 1 - i) % ctx.onsets.size()]);
    std::sort(times.begin(), times.end());
    double first = -1;
    for (size_t i = times.size(); i-- > 0;) {
        if (times[i] > nowT + 0.05)
            continue;
        if (first >= 0 && first - times[i] > 8 * spb)
            break;
        if (first < 0 && nowT - times[i] > 8 * spb)
            break;
        first = times[i];
    }
    if (first < 0) {
        ctx.notice(QStringLiteral("Play something, then Space loops it"));
        return;
    }
    bool anyPlaying = false;
    for (int p = 0; p < m_view.partCount; ++p)
        anyPlaying |= m_view.parts[p].playing >= 0 || m_view.parts[p].queued >= 0;
    if (!anyPlaying && !m_view.metronome) {
        if (nowT - first < 0.3) {
            ctx.notice(QStringLiteral("Press Space where the phrase starts again"));
            return;
        }
        ctx.capture(slot, first, nowT, 0.85f);
        m_view.parts[part].recording = slot;
        ctx.notice(QStringLiteral("Loop in slot %1: it sets the tempo").arg(slot + 1));
        return;
    }
    const double beatNow = ctx.beatAt(nowT), beatFirst = ctx.beatAt(first);
    const double barNow = std::floor(beatNow / kBeatsPerBar) * kBeatsPerBar;
    const double end = beatNow - barNow < 1.0 ? barNow : barNow + kBeatsPerBar;
    // A first note a little early for its bar belongs to that bar.
    const double startBar = std::floor((beatFirst + 0.5) / kBeatsPerBar) * kBeatsPerBar;
    int bars = 1;
    while (bars < 8 && end - bars * kBeatsPerBar > startBar + 1e-6)
        bars *= 2;
    StudioMsg m;
    m.kind = StudioMsg::TakeBars;
    m.part = int8_t(part);
    m.slot = int8_t(slot);
    m.d0 = end - bars * kBeatsPerBar;
    m.d1 = end;
    ctx.post(m);
    m_view.parts[part].recording = slot;
    const QString what = bars == 1 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(bars);
    if (end <= beatNow)
        ctx.notice(QStringLiteral("Looping %1 in slot %2").arg(what).arg(slot + 1));
    else
        ctx.notice(QStringLiteral("Looping %1 in slot %2 at the bar").arg(what).arg(slot + 1));
}

void Session::shiftTempo(double delta) {
    std::lock_guard lock(m_lock);
    const double bpm = std::clamp(std::round(m_view.bpm) + delta, 60.0, 200.0);
    context(m_page).setTempo(bpm);
    context(m_page).notice(QStringLiteral("%1 BPM").arg(bpm));
}

void Session::shiftRoot(int fifths) {
    std::lock_guard lock(m_lock);
    const int root = ((m_view.root + 7 * fifths) % 12 + 12) % 12;
    context(m_page).setKey(root, m_view.family, m_view.brightness);
    context(m_page).notice(QStringLiteral("Key of %1").arg(QString::fromUtf8(Tuning::noteName(root))));
}

void Session::shiftMode(int step) {
    std::lock_guard lock(m_lock);
    const ScaleFamily &fam = family(m_view.family);
    const int b = std::clamp(m_view.brightness + step, 0, fam.modeCount - 1);
    context(m_page).setKey(m_view.root, m_view.family, b);
    context(m_page).notice(QString::fromUtf8(fam.modeNames[b]));
}

void Session::toggleMetronome() {
    std::lock_guard lock(m_lock);
    StudioMsg m;
    m.kind = StudioMsg::Metronome;
    m.value = !m_view.metronome;
    m_view.metronome = m.value;
    m_studio->post(m);
}

void Session::stopAll() {
    std::lock_guard lock(m_lock);
    for (int i = 0; i < m_partCount; ++i)
        context(m_page).stopPart(i, false);
    context(m_page).notice(QStringLiteral("Everything stops at the bar"));
}

}
