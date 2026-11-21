#include "session.h"

#include <QPainter>
#include <Qt>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "parts.h"

namespace tf {

struct Session::Page {
    PartSpec spec;
    bool followsChords = true;
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
        if (!(c.flags & Command::NoRecord) && (c.type == Command::On || (c.flags & Command::Quantize))) {
            // A Continue keeps a phrase going (an arp running from held
            // notes) but never starts one.
            // Its key says which sound it was (a pad, a note, a chord): the
            // first field is each page's "what", so Space can tell a
            // repeated pattern from new playing.
            uint32_t key = uint32_t(c.type) * 2654435761u ^ uint32_t(c.i[0]) * 40503u;
            if (m_s->m_pages[size_t(m_part)]->followsChords)
                key ^= uint32_t(m_s->m_view.chordRoot + 1) * 7919u;
            onsets[onsetHead % onsets.size()] = {c.t > 0 ? c.t : now(), !(c.flags & Command::Continue), key};
            ++onsetHead;
        }
        if (!(c.flags & Command::NoRecord)) {
            if (c.type == Command::On && heldCount < int(held.size())) {
                held[size_t(heldCount++)] = c.id;
            } else if (c.type == Command::Off) {
                for (int i = 0; i < heldCount; ++i)
                    if (held[size_t(i)] == c.id) {
                        held[size_t(i)] = held[size_t(--heldCount)];
                        break;
                    }
            }
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
    void take(int slot, double b0, double b1) override {
        StudioMsg m;
        m.kind = StudioMsg::TakeBars;
        m.part = int8_t(m_part);
        m.slot = int8_t(slot);
        m.d0 = b0;
        m.d1 = b1;
        post(m);
    }
    void stopRecording() override { simple(StudioMsg::StopRecord, m_part, 0, 0); }
    void overdub(bool on) override { simple(StudioMsg::Overdub, m_part, 0, on); }
    void launch(int slot, bool now) override { launchPart(m_part, slot, now); }
    void stop(bool now) override { stopPart(m_part, now); }
    void clear(int slot) override { clearPart(m_part, slot); }

    void launchPart(int part, int slot, bool now) override { simple(StudioMsg::Launch, part, slot, now); }
    void copyClip(int part, int from, int to) override { simple(StudioMsg::CopyClip, part, from, to); }
    void mergeClips(int part, int into, int other, bool chain) override {
        StudioMsg m;
        m.kind = StudioMsg::MergeClips;
        m.part = int8_t(part);
        m.slot = int8_t(into);
        m.value = other;
        m.f = chain ? 1.0f : 0.0f;
        post(m);
    }
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

    struct Onset {
        double t = 0;
        bool real = false;
        uint32_t key = 0;
    };
    std::array<Onset, 256> onsets{};
    size_t onsetHead = 0;
    // Sounds started and not yet ended: a chord or a pad held down is still
    // playing, so the phrase hasn't paused.
    std::array<int32_t, 64> held{};
    int heldCount = 0;
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
        page->followsChords = spec.followsChords;
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
QString Session::pageStatus(int page) const {
    std::lock_guard lock(m_lock);
    return page >= 0 && page < pageCount() ? m_pages[size_t(page)]->spec.control->status() : QString();
}
float Session::probe(int page, const QString &name) const {
    std::lock_guard lock(m_lock);
    return page >= 0 && page < pageCount() ? m_pages[size_t(page)]->spec.control->tutorialProbe(name) : std::nanf("");
}

void Session::toggleTutorial() {
    std::lock_guard lock(m_lock);
    if (m_tutorial.active())
        m_tutorial.stop();
    else
        m_tutorial.start(*this);
}
void Session::tutorialNext() {
    std::lock_guard lock(m_lock);
    m_tutorial.next(*this);
}
void Session::tutorialBack() {
    std::lock_guard lock(m_lock);
    m_tutorial.back(*this);
}

void Session::frame(const Frame &f) {
    std::lock_guard lock(m_lock);
    m_last = f;
    deliver(withMouse(f));
}

Frame Session::withMouse(Frame f) const {
    if (m_mouseDown && f.count < int(f.touches.size())) {
        Touch &t = f.touches[f.count++];
        t.id = kMouseId;
        t.x = std::clamp(m_mouseX, 0.0f, 1.0f);
        t.y = std::clamp(m_mouseY, 0.0f, 1.0f);
        t.size = 1.0f;
        t.pressure = 0.5f;
    }
    return f;
}

void Session::pointer(float x, float y, bool down, bool inside) {
    std::lock_guard lock(m_lock);
    const bool was = m_mouseDown;
    m_mouseX = x;
    m_mouseY = y;
    m_mouseIn = inside;
    m_mouseDown = down;
    if (!down && !was)
        return;
    // A frame of its own: the pad's fingers as they last were, plus the
    // mouse (or without it, on release).
    Frame f = m_last;
    f.t = now();
    f.button = false;
    deliver(withMouse(f));
}

void Session::deliver(const Frame &f) {
    m_lastFrame = f.t;
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
        case Qt::Key_Z: undoTake(); return true;
        case Qt::Key_N: startOver(); return true;
        case Qt::Key_T: toggleTutorial(); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!m_tutorial.active())
                return false;
            shift ? tutorialBack() : tutorialNext();
            return true;
        case Qt::Key_R: m_mixRecording = !m_mixRecording; return true;
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
            toggleRecording(*p.ctx, (k.modifiers & Qt::ShiftModifier) != 0);
        return true;
    }
    return false;
}

void Session::refresh() {
    std::lock_guard lock(m_lock);
    if (m_studio->fetchView()) {
        m_view = m_studio->view();
        // Takes land in the studio; remember which part made each, in
        // order, so Ctrl+Z takes back the newest first.
        for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p) {
            while (m_takesSeen[p] < m_view.parts[p].takes) {
                ++m_takesSeen[p];
                m_takeOrder.push_back(p);
            }
        }
        if (m_takeOrder.size() > 64)
            m_takeOrder.erase(m_takeOrder.begin(), m_takeOrder.end() - 64);
        const long bin = long(std::floor(m_view.time * 4.0));
        for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p) {
            const bool on = m_view.parts[p].voices > 0 && m_view.parts[p].playing < 0;
            // Bins skipped since the last view were the same as now.
            for (long b = std::max(m_soundBin[p] + 1, bin - kSoundBins + 1); b <= bin; ++b)
                m_sounding[p][((b % kSoundBins) + kSoundBins) % kSoundBins] = on;
            m_soundBin[p] = bin;
        }
    }
    m_tutorial.update(*this);
}

// No stretch longer than maxGap in [t0, t1] where the part was silent.
bool Session::soundedThrough(int part, double t0, double t1, double maxGap) const {
    if (part < 0 || part >= kMaxParts)
        return false;
    const long b0 = long(std::floor(t0 * 4.0)), b1 = long(std::floor(t1 * 4.0));
    if (b1 - b0 >= kSoundBins || b1 > m_soundBin[part] + 2)
        return false;
    double silent = 0;
    for (long b = b0; b <= b1; ++b) {
        if (m_sounding[part][((b % kSoundBins) + kSoundBins) % kSoundBins])
            silent = 0;
        else if ((silent += 0.25) > maxGap)
            return false;
    }
    return true;
}

// The latest moment since `since` the part was sounding, or -1.
double Session::lastSounding(int part, double since) const {
    if (part < 0 || part >= kMaxParts)
        return -1;
    const long b0 = std::max(long(std::floor(since * 4.0)), m_soundBin[part] - kSoundBins + 1);
    for (long b = m_soundBin[part]; b >= b0; --b)
        if (m_sounding[part][((b % kSoundBins) + kSoundBins) % kSoundBins])
            return double(b + 1) / 4.0;
    return -1;
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
    pc.hover = m_mouseIn;
    pc.hoverX = m_mouseX;
    pc.hoverY = m_mouseY;
    painter.save();
    p.spec.control->paint(painter, pc);
    painter.restore();
    if (m_tutorial.active()) {
        // Spots in the colour of the page the step happens on.
        pc.color = pageColor(m_tutorial.step().page);
        m_tutorial.paint(painter, pc, m_page, pageName(m_tutorial.step().page));
    }
}

QString Session::notice(uint32_t *seq) const {
    std::lock_guard lock(m_lock);
    if (seq)
        *seq = m_noticeSeq;
    return m_notice;
}

namespace {

struct Hit {
    double x;
    uint32_t key;
};

// How well what was played in [a, b) repeats what was played one period
// earlier, from 0 to 1: every hit with a twin (the same sound, at the same
// place a period before) counts, on both sides.
// The windows reach `edge` early, for hits a little ahead of their place.
double repeats(const std::vector<Hit> &hits, double a, double b, double period, double tol, double edge) {
    std::vector<const Hit *> now, before;
    for (const Hit &h : hits) {
        if (h.x >= a - edge && h.x < b - edge)
            now.push_back(&h);
        if (h.x >= a - period - edge && h.x < b - period - edge)
            before.push_back(&h);
    }
    if (now.empty() || before.empty())
        return 0;
    std::vector<bool> used(before.size(), false);
    int matched = 0;
    for (const Hit *h : now)
        for (size_t j = 0; j < before.size(); ++j)
            if (!used[j] && before[j]->key == h->key && std::fabs(h->x - period - before[j]->x) < tol) {
                used[j] = true;
                ++matched;
                break;
            }
    return 2.0 * matched / double(now.size() + before.size());
}

// The loop the player meant: the shortest period that what was just played
// repeats with, then stretched back over as many repeats as there are, so
// the tinkering before the pattern settled stays out. Periods are the
// candidates given, longest stretch at most `longest`; returns the loop's
// length (a whole number of periods), or 0 when nothing repeats.
double settledLoop(const std::vector<Hit> &hits, double end, double earliest, const std::vector<double> &periods,
                   double longest, double tol, double minScore, double edge) {
    for (double p : periods) {
        if (end - 2 * p < earliest - tol)
            break; // no room for a repeat before it
        if (repeats(hits, end - p, end, p, tol, edge) < minScore)
            continue;
        double length = p;
        for (double m = 2; m * p <= longest + 1e-9; m *= 2) {
            const double start = end - m * p;
            if (start < earliest - tol)
                break;
            // Every repeat from there on matches the one before it.
            if (repeats(hits, start + p, end, p, tol, edge) < minScore)
                break;
            length = m * p;
        }
        return length;
    }
    return 0;
}

}

void Session::toggleRecording(ControlContext &base, bool layer) {
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
    // The phrase: what was played back from the latest, until a pause of two
    // bars. It starts at its earliest real onset; carried-on sounds bridge
    // pauses and mark where it ends.
    std::vector<Context::Onset> times;
    const size_t n = std::min(ctx.onsetHead, ctx.onsets.size());
    for (size_t i = 0; i < n; ++i)
        times.push_back(ctx.onsets[(ctx.onsetHead - 1 - i) % ctx.onsets.size()]);
    std::sort(times.begin(), times.end(), [](const auto &x, const auto &y) { return x.t < y.t; });
    double first = -1, earliest = -1, last = -1, lastReal = -1;
    bool clocked = false; // sounds carried on through the phrase: it already runs on the clock
    for (size_t i = times.size(); i-- > 0;) {
        const double t = times[i].t;
        if (t > nowT + 0.05)
            continue;
        // A pause is two bars with nothing played and nothing sounding.
        if (last < 0) {
            if (nowT - t > 8 * spb && !soundedThrough(part, t, nowT, 8 * spb))
                break;
            last = t;
        } else if (earliest - t > 8 * spb && !soundedThrough(part, t, earliest, 8 * spb)) {
            break;
        }
        earliest = t;
        if (times[i].real) {
            first = t;
            if (lastReal < 0)
                lastReal = t;
        }
        else
            clocked = true;
    }
    if (first < 0)
        first = earliest;
    if (first < 0 && m_view.parts[part].voices > 0) {
        // Nothing played lately, but the part is sounding (a bed latched a
        // while ago, an arp left running): loop the bar that just went by,
        // or the one now ending. The take carries in what was held across
        // its start, so the loop is the sound as it is.
        const double beatNow = ctx.beatAt(nowT);
        const double barNow = std::floor(beatNow / kBeatsPerBar) * kBeatsPerBar;
        const double end = beatNow - barNow < 1.0 ? barNow : barNow + kBeatsPerBar;
        StudioMsg m;
        m.kind = StudioMsg::TakeBars;
        m.part = int8_t(part);
        m.slot = int8_t(slot);
        m.d0 = end - kBeatsPerBar;
        m.d1 = end;
        ctx.post(m);
        m_view.parts[part].recording = slot;
        ctx.notice(QStringLiteral("Looping what is sounding: 1 bar in slot %1").arg(slot + 1));
        return;
    }
    if (first < 0) {
        ctx.notice(QStringLiteral("Play something, then Space loops it"));
        return;
    }
    bool anyPlaying = false;
    for (int p = 0; p < m_view.partCount; ++p)
        anyPlaying |= m_view.parts[p].playing >= 0 || m_view.parts[p].queued >= 0;
    if (!anyPlaying && !m_view.metronome && !clocked) {
        if (nowT - first < 0.3) {
            ctx.notice(QStringLiteral("Press Space where the phrase starts again"));
            return;
        }
        // If the player went round a pattern more than once, the loop is
        // that pattern, from where it settled; otherwise the whole phrase.
        std::vector<Hit> hits;
        for (const auto &o : times)
            if (o.real && o.t >= first - 0.01 && o.t <= nowT + 0.05)
                hits.push_back({o.t, o.key});
        std::vector<double> periods;
        for (size_t i = hits.size(); i-- > 0;) {
            const double p = nowT - hits[i].x;
            if (p >= 0.3)
                periods.push_back(p);
        }
        double start = first;
        bool settled = false;
        for (double p : periods) {
            const double tol = 0.06 + 0.03 * p;
            const double length = settledLoop(hits, nowT, first, {p}, 16.0, tol, 0.8, tol / 2);
            if (length > 0) {
                // Start on the hit nearest the found start: the player's own time.
                const double want = nowT - length;
                double bestD = 1e9;
                for (const Hit &h : hits)
                    if (std::fabs(h.x - want) < bestD) {
                        bestD = std::fabs(h.x - want);
                        start = h.x;
                    }
                settled = start > first + 0.05;
                break;
            }
        }
        ctx.capture(slot, start, nowT, 0.85f);
        m_view.parts[part].recording = slot;
        ctx.notice(settled ? QStringLiteral("Loop of what you repeated, slot %1: it sets the tempo").arg(slot + 1)
                           : QStringLiteral("Loop in slot %1: it sets the tempo").arg(slot + 1));
        return;
    }
    // Sound that went on after the last note (a riser building into its
    // drop, an impact ringing out) is part of what was played.
    {
        // A note's own release isn't that: only sound going on well past it.
        const double sounding = lastSounding(part, last);
        if (sounding > last + 2 * spb)
            last = std::min(sounding, nowT);
    }
    const double beatNow = ctx.beatAt(nowT), beatFirst = ctx.beatAt(first), beatLast = ctx.beatAt(last);
    // The loop ends where the playing did: now, or a beat after the last
    // note if there was a pause before Space, on the sixteenth grid. It
    // doesn't wait for a bar line: whatever repeats, repeats from any point,
    // and the studio stores the loop in step with the bars.
    double end = std::round(std::min(beatNow, beatLast + 1.0) * 4.0) / 4.0;
    // Only held sounds carry the phrase on past its last new note (a chord
    // held through the bar): the loop ends at the bar line after that note,
    // not wherever Space came, so it stays whole bars on the bar lines.
    if (ctx.heldCount > 0 && lastReal >= 0) {
        const double bar = std::ceil((ctx.beatAt(lastReal) + 0.5) / kBeatsPerBar) * kBeatsPerBar;
        end = std::min(bar, std::round(beatNow * 4.0) / 4.0);
    }
    // Did the player settle into a pattern? The shortest whole number of
    // bars that what was just played repeats with, stretched back over
    // every repeat, leaving out what came before it.
    std::vector<Hit> hits;
    for (const auto &o : times)
        if (o.real && o.t >= first - 0.01 && o.t <= nowT + 0.05)
            hits.push_back({ctx.beatAt(o.t), o.key});
    // One cycle of it: the shortest period that repeats cleanly, or, if no
    // period repeats cleanly, the one that repeats best. A two-bar melody
    // whose bars are alike but not the same stays two bars once it has been
    // played twice.
    double length = 0, bestScore = 0.75;
    for (double p : {4.0, 8.0, 16.0, 32.0, 64.0}) {
        if (end - 2 * p < beatFirst - 0.125 - 0.2)
            break;
        const double score = repeats(hits, end - p, end, p, 0.2, 0.125);
        if (score >= 0.9) {
            length = p;
            break;
        }
        if (score > bestScore) {
            bestScore = score;
            length = p;
        }
    }
    const bool settled = length > 0;
    if (!settled) {
        // Played once: the whole phrase, in whole bars, with no bars of
        // silence added to round it.
        const double played = std::ceil((end - beatFirst - 0.25) / kBeatsPerBar);
        length = kBeatsPerBar * std::clamp(played, 1.0, 16.0);
    }
    // Over loops already playing, a take long enough to cover the longest
    // of them takes its length: a bass line over a four-bar progression
    // loops all four bars, in step with it.
    {
        double phrase = 0;
        for (int q = 0; q < m_view.partCount; ++q)
            if ((q != part || layer) && m_view.parts[q].playing >= 0)
                phrase = std::max(phrase, double(m_view.parts[q].clips[m_view.parts[q].playing].lengthBeats));
        if (phrase > length && phrase <= 64 && end - phrase >= beatFirst - 1.0)
            length = phrase;
    }
    const int bars = int(std::lround(length / kBeatsPerBar));
    StudioMsg m;
    m.kind = StudioMsg::TakeBars;
    m.part = int8_t(part);
    m.slot = int8_t(slot);
    m.d0 = end - length;
    m.d1 = end;
    // Shift+Space: on top of this part's playing loop, as one loop.
    const int into = m_view.parts[part].playing;
    if (layer && into >= 0)
        m.value = into + 1;
    ctx.post(m);
    m_view.parts[part].recording = slot;
    QString what = bars == 1 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(bars);
    if (settled)
        what = QStringLiteral("the %1 you repeated").arg(what);
    if (layer && into >= 0)
        ctx.notice(QStringLiteral("Layered %1 onto slot %2 (Ctrl+Z takes it off)").arg(what).arg(into + 1));
    else
        ctx.notice(QStringLiteral("Looping %1 in slot %2").arg(what).arg(slot + 1));
}

void Session::undoTake() {
    std::lock_guard lock(m_lock);
    // The studio keeps the last four takes per part; older ones are gone.
    if (m_takeOrder.empty()) {
        context(m_page).notice(QStringLiteral("No loop to take back"));
        return;
    }
    const int part = m_takeOrder.back();
    m_takeOrder.pop_back();
    ++m_undos;
    StudioMsg m;
    m.kind = StudioMsg::UndoTake;
    m.part = int8_t(part);
    m_studio->post(m);
    context(m_page).notice(QStringLiteral("Took back the last %1 loop").arg(pageName(part)));
}

void Session::startOver() {
    std::lock_guard lock(m_lock);
    const double t = now();
    if (m_startOverAsked < 0 || t - m_startOverAsked > 3.0) {
        m_startOverAsked = t;
        context(m_page).notice(QStringLiteral("Ctrl+N again to clear every loop"));
        return;
    }
    m_startOverAsked = -1;
    StudioMsg m;
    m.kind = StudioMsg::ClearAll;
    m_studio->post(m);
    m_takeOrder.clear();
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p) {
        m_view.parts[p].playing = -1;
        for (auto &c : m_view.parts[p].clips)
            c.state = ClipView::Empty;
    }
    context(m_page).notice(QStringLiteral("Starting over: the next loop sets the tempo"));
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
