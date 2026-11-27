#include "session.h"
#include "console.h"

#include "platform.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPainter>
#include <Qt>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "parts.h"
#include "songfile.h"
#include "visual.h"

namespace tf {

namespace {
// Said when everything pauses; once something plays again, by any way
// (F1, a clip clicked), it gives way to "Playing" rather than stay up.
const QString kPausedNotice = QStringLiteral("Paused · Ctrl+P plays it again");
}

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
            if (c.type == Command::Move && heldCount > 0)
                lastShaped = c.t > 0 ? c.t : now();
            if (c.type == Command::Param)
                lastTurned = c.t > 0 ? c.t : now();
            // (An On for a sound already held, a chop going round again,
            // is the same held sound, not another.)
            if (c.type == Command::On && heldCount < int(held.size()) &&
                std::find(held.begin(), held.begin() + heldCount, c.id) == held.begin() + heldCount) {
                held[size_t(heldCount++)] = c.id;
            } else if (c.type == Command::Off) {
                for (int i = 0; i < heldCount; ++i)
                    if (held[size_t(i)] == c.id) {
                        held[size_t(i)] = held[size_t(--heldCount)];
                        lastRelease = c.t > 0 ? c.t : now();
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
    void takeAudio(int slot, double b0, double b1, const Command &sound, int into, bool retake, bool free) override {
        if (m_part < 0)
            return;
        StudioMsg m;
        m.kind = free ? (retake ? StudioMsg::Recapture : StudioMsg::Capture)
                      : (retake ? StudioMsg::Retake : StudioMsg::TakeBars);
        m.part = int8_t(m_part);
        m.slot = int8_t(slot);
        m.d0 = b0;
        m.d1 = b1;
        m.audio = true;
        m.cmd = sound;
        if (!free && into >= 0)
            m.value = into + 1;
        post(m);
        // Shown at once, as Space's own take is, so a second Space knows
        // there is one to call off.
        if (!free)
            m_s->m_view.parts[m_part].recording = slot;
    }
    void cancelTake() override {
        if (m_part < 0)
            return;
        simple(StudioMsg::CancelRecord, m_part, 0, 0);
        m_s->m_view.parts[m_part].recording = -1;
    }
    void stopRecording() override { simple(StudioMsg::StopRecord, m_part, 0, 0); }
    void overdub(bool on) override { simple(StudioMsg::Overdub, m_part, 0, on); }
    void launch(int slot, bool now) override { launchPart(m_part, slot, now); }
    void stop(bool now) override { stopPart(m_part, now); }
    void clear(int slot) override { clearPart(m_part, slot); }
    bool resizeLoop(int dir) override { return m_s->retakeLength(0) > 0 && m_s->adjustTake(dir); }
    int loopStep(int dir) const override { return int(std::lround(m_s->retakeLength(dir))); }

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
    bool newLoop() override { return m_s->newLoop(); }
    void browseSounds() override { m_s->m_soundsAsked = true; }
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
    bool scripted() const override { return m_s->m_clock != nullptr; }
    void log(const QString &line) override { m_s->dlog("%s", qPrintable(line)); }
    void pickSound(const QString &title) override { m_s->pickSound(m_part, title); }
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
    // When a held sound was last shaped (slid, pressed, turned): playing
    // that goes on without new notes.
    double lastShaped = -1;
    // When a held sound was last let go: a note held and released before
    // Space is played until then, not just from its onset.
    double lastRelease = -1;
    // When a knob (a shaper, a filter) was last moved: over a playing loop,
    // on a page that says so, that is playing too, and Space keeps it.
    double lastTurned = -1;
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
    std::fill(std::begin(m_pausedSlots), std::end(m_pausedSlots), -1);
    std::vector<PartSpec> parts = makeParts();
    QStringList names;
    std::vector<QColor> colors;
    for (auto &spec : parts) {
        names << spec.name;
        colors.push_back(spec.color);
    }
    for (auto &spec : parts) {
        auto page = std::make_unique<Page>();
        page->part = m_studio->addPart(std::move(spec.engine), spec.channel, spec.monitors);
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
    auto shown = std::make_unique<Page>();
    shown->spec = visual::makeVisual();
    shown->part = -1;
    shown->followsChords = false;
    shown->ctx = std::make_unique<Context>(this, -1);
    m_visual = static_cast<visual::VisualPage *>(shown->spec.control.get());
    m_pages.push_back(std::move(shown));
    m_view.partCount = m_partCount;
    for (int i = 0; i < m_partCount && i < kMaxParts; ++i)
        for (int c = 0; c < ChannelCount; ++c)
            m_view.parts[i].channel.value[c] = m_pages[size_t(i)]->spec.channel[c];
    m_pages[0]->spec.control->enter(*m_pages[0]->ctx);
    if (const char *log = std::getenv("TENFOLD_DEBUG_LOG"))
        setDebugLog(QString::fromLocal8Bit(log));
}

Session::~Session() {
    if (m_debugLog)
        std::fclose(m_debugLog);
}

void Session::setDebugLog(const QString &path) {
    std::lock_guard lock(m_lock);
    if (m_debugLog)
        std::fclose(m_debugLog);
    // Kept across runs (a restart mid-session loses nothing), started over
    // once it grows past a few megabytes.
    const QByteArray file = path.toLocal8Bit();
    std::FILE *old = std::fopen(file.constData(), "r");
    long size = 0;
    if (old) {
        std::fseek(old, 0, SEEK_END);
        size = std::ftell(old);
        std::fclose(old);
    }
    m_debugLog = std::fopen(file.constData(), size > 8 * 1024 * 1024 ? "w" : "a");
    dlog("=== tenfold started. ON = a hit a part heard (src live, or the slot it replayed from); TAKE = a loop made");
}

void Session::dlog(const char *fmt, ...) {
    if (!m_debugLog)
        return;
    std::fprintf(m_debugLog, "[%10.4f] ", now());
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(m_debugLog, fmt, ap);
    va_end(ap);
    std::fputc('\n', m_debugLog);
    std::fflush(m_debugLog);
}

void Session::drainDebug() {
    Studio::DebugEvent e;
    bool any = false;
    while (m_studio->popDebug(e)) {
        any = true;
        if (!m_debugLog)
            continue;
        const char *name = e.part >= 0 && e.part < int(m_pages.size()) ? "" : "?";
        if (e.kind == 0) {
            char src[16];
            if (e.src < 0)
                std::snprintf(src, sizeof src, "live");
            else
                std::snprintf(src, sizeof src, "slot%d", e.src + 1);
            dlog("ON   part=%d%s %-6s beat=%9.4f (bar %d + %6.3f) t=%9.4f late=%6.1fms type=%d id=%d i0=%d fl=%d v=%.2f", e.part,
                 name, src, e.beat, int(std::floor(e.beat / kBeatsPerBar)) + 1,
                 e.beat - std::floor(e.beat / kBeatsPerBar) * kBeatsPerBar, e.t, (e.heard - e.t) * 1000.0, e.type, e.id, e.i0,
                 e.flags, double(e.v));
        } else if (e.kind == 3) {
            dlog("PASS part=%d slot=%d: playing into the loop from beat %.4f (it had %d events; Ctrl+Z takes it back)",
                 e.part, e.src + 1, e.beat, e.events);
        } else if (e.kind == 4) {
            dlog("UNDONE part=%d slot=%d back to len=%.3f events=%d at beat %.4f, slot %d plays, %d steps left", e.part,
                 e.src + 1, e.len, e.events, e.beat, e.id + 1, e.i0);
        } else if (e.kind == 1) {
            dlog("TAKE part=%d slot=%d window %.4f..%.4f (%.3f beats) -> clip len=%.3f anchor=%.4f events=%d (clock beat %.4f)",
                 e.part, e.src + 1, e.a, e.b, e.b - e.a, e.len, e.anchor, e.events, e.beat);
        } else {
            dlog("  EV slot=%d pos=%8.4f type=%d id=%d i0=%d fl=%d v=%.2f", e.src + 1, e.a, e.type, e.id, e.i0, e.flags,
                 double(e.v));
        }
    }
    if (any && m_debugLog)
        std::fflush(m_debugLog);
}

double Session::now() const {
    if (m_clock)
        return m_clock();
    return monotonicSeconds();
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

QString Session::pageKey(int page) const {
    if (page == arrangePage())
        return QStringLiteral("0");
    if (page == visualPage())
        return QStringLiteral("9");
    if (page >= 0 && page < 8)
        return QString::number(page + 1);
    return page == 8 ? QStringLiteral("⇧1") : QString();
}

int Session::pageNamed(const QString &name) const {
    for (int i = 0; i < pageCount(); ++i)
        if (name.compare(pageKey(i), Qt::CaseInsensitive) == 0 || name.compare(pageName(i), Qt::CaseInsensitive) == 0)
            return i;
    if (name.compare(QLatin1String("Shift+1"), Qt::CaseInsensitive) == 0 && m_partCount > 8)
        return 8;
    return -1;
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
    if (m_tutorial.active()) {
        m_tutorial.stop();
        return;
    }
    // The guide builds a song from silence, in A minor at 124: it starts
    // from a cleared studio with the mix at home, so nothing left muted or
    // playing from before hides what a step asks for. Loops made already
    // are asked about first.
    bool anyLoop = false;
    for (int p = 0; p < m_view.partCount; ++p)
        for (const auto &c : m_view.parts[p].clips)
            anyLoop |= c.state != ClipView::Empty;
    const double t = now();
    if (anyLoop && (m_tutorialAsked < 0 || t - m_tutorialAsked > 3.0)) {
        m_tutorialAsked = t;
        context(m_page).notice(QStringLiteral("The tutorial starts from silence: Ctrl+T again clears every loop"));
        return;
    }
    m_tutorialAsked = -1;
    m_startOverAsked = -1;
    const bool kept = keepBeforeClearing();
    clearEverything();
    ++m_freshStarts;
    context(m_page).setKey(9, 0, 4);
    context(m_page).setTempo(124);
    m_tutorial.start(*this);
    if (kept)
        say(QStringLiteral("The tutorial starts from silence · Ctrl+Shift+O brings your song back"));
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

QRectF Session::padRect(QSizeF item) {
    const qreal top = 14 + console::kHead, side = 30, bottom = kBandHeight;
    return QRectF(side, top, std::max<qreal>(1, item.width() - 2 * side), std::max<qreal>(1, item.height() - top - bottom));
}

void Session::mouseAt(QPointF at, bool over) {
    std::lock_guard lock(m_lock);
    m_mousePx = at;
    m_mouseOver = over;
}

bool Session::rightClickAt(QPointF at) {
    std::lock_guard lock(m_lock);
    m_mousePx = at;
    m_mouseOver = true;
    Page &p = *m_pages[size_t(m_page)];
    return p.spec.control->menu(at, *p.ctx);
}

bool Session::menuClickAt(QPointF at) {
    std::lock_guard lock(m_lock);
    Page &p = *m_pages[size_t(m_page)];
    return p.spec.control->menuClick(at, *p.ctx);
}

bool Session::closeMenu() {
    std::lock_guard lock(m_lock);
    return m_pages[size_t(m_page)]->spec.control->closeMenu();
}

bool Session::clickAt(QPointF at) {
    std::lock_guard lock(m_lock);
    m_mousePx = at;
    m_mouseOver = true;
    Page &p = *m_pages[size_t(m_page)];
    if (!m_studioTag.isEmpty() && m_studioTag.contains(at)) {
        // The way there: Studio, with this page whole.
        const QString what = zoomArea().what;
        m_level = 2;
        context(m_page).notice(QStringLiteral("Studio: %1").arg(what));
        return true;
    }
    if (console::nextChipAt(at) || (!m_soundNext.isEmpty() && m_soundNext.contains(at))) {
        KeyEvent k;
        k.key = Qt::Key_Right;
        keyForPage(k);
        k.down = false;
        keyForPage(k);
        return true;
    }
    if ((!m_soundChip.isEmpty() && m_soundChip.contains(at)) || console::browseChipAt(at)) {
        m_soundsAsked = true;
        return true;
    }
    if (const QRectF fold = foldedRow(m_padRect); !fold.isEmpty() && fold.contains(at)) {
        m_level = 2;
        context(m_page).notice(QStringLiteral("Studio: every control"));
        return true;
    }
    return p.spec.control->click(at, *p.ctx);
}

bool Session::drop(const QString &what) {
    std::lock_guard lock(m_lock);
    Page &showing = *m_pages[size_t(m_page)];
    if (showing.spec.control->dropped(what, *showing.ctx))
        return true;
    for (int i = 0; i < pageCount(); ++i) {
        Page &p = *m_pages[size_t(i)];
        if (i != m_page && p.spec.control->dropped(what, *p.ctx)) {
            setPage(i);
            return true;
        }
    }
    return false;
}

void Session::deliver(const Frame &f) {
    m_lastFrame = f.t;
    deliverPicked();
    for (auto &page : m_pages)
        page->spec.control->background(f.t, *page->ctx);
    Page &p = *m_pages[size_t(m_page)];
    // Below Studio a page that zooms onto its play area hears the fingers
    // there: the visible pad is that area.
    Frame zoomed;
    const Frame *in = &f;
    if (const PartControl::PlayArea a = zoomArea(); a.zooms()) {
        zoomed = f;
        for (int i = 0; i < zoomed.count; ++i) {
            Touch &t = zoomed.touches[i];
            t.x = a.x0 + t.x * (a.x1 - a.x0);
            t.y = a.y0 + t.y * (a.y1 - a.y0);
        }
        in = &zoomed;
    }
    const Frame &fr = *in;
    const bool quiet = f.t < m_quietUntil && p.spec.control->quietAfterSpace();
    if (m_ignored.empty() && !quiet) {
        p.spec.control->frame(fr, *p.ctx);
        return;
    }
    Frame g = fr;
    g.count = 0;
    std::vector<int> still;
    for (int i = 0; i < fr.count; ++i) {
        const int id = fr.touches[i].id;
        // A finger landing in the quiet moment after Space is ignored too,
        // until it lifts: it was the hand still going, not new playing.
        if (quiet || std::find(m_ignored.begin(), m_ignored.end(), id) != m_ignored.end())
            still.push_back(id);
        else
            g.touches[g.count++] = fr.touches[i];
    }
    m_ignored = still;
    p.spec.control->frame(g, *p.ctx);
}

void Session::handOver() {
    // Every finger on the pad now steps aside; the page hears them lift at
    // once, so what they held ends as the loop starts.
    const Frame f = withMouse(m_last);
    for (int i = 0; i < f.count; ++i)
        if (std::find(m_ignored.begin(), m_ignored.end(), f.touches[i].id) == m_ignored.end())
            m_ignored.push_back(f.touches[i].id);
    const double t = now();
    m_quietUntil = t + kHandOverSecs;
    Frame lifted = f;
    lifted.t = t;
    deliver(lifted);
}

void Session::pickSound(int part, const QString &title) {
    if (!m_picker) {
        context(m_page).notice(QStringLiteral("No file dialog here: drop a sound file on the window"));
        return;
    }
    // The dialog may answer at once or long after, under this lock or not:
    // what it picks waits here, and idle() hands it to the page.
    m_picker->openSound(title, [this, part](const QString &path) {
        if (path.isEmpty())
            return;
        std::lock_guard picked(m_pickedLock);
        m_picked.push_back({part, path});
    });
}

// A file the dialog picked goes to the page that asked, as a drop.
void Session::deliverPicked() {
    std::vector<std::pair<int, QString>> picked;
    {
        std::lock_guard l(m_pickedLock);
        picked.swap(m_picked);
    }
    for (const auto &[part, path] : picked)
        for (auto &page : m_pages)
            if (page->part == part && page->ctx)
                page->spec.control->dropped(path, *page->ctx);
}

void Session::idle(double t) {
    std::lock_guard lock(m_lock);
    drainDebug();
    deliverPicked();
    playAudition(now());
    for (auto &page : m_pages)
        page->spec.control->background(t, *page->ctx);
    Page &p = *m_pages[size_t(m_page)];
    p.spec.control->idle(t, *p.ctx);
}

bool Session::key(const KeyEvent &k) {
    std::lock_guard lock(m_lock);
    if (!k.repeat) {
        const auto it = std::find(m_keysDown.begin(), m_keysDown.end(), k.key);
        if (k.down && it == m_keysDown.end())
            m_keysDown.push_back(k.key);
        else if (!k.down && it != m_keysDown.end())
            m_keysDown.erase(it);
    }
    const bool ctrl = k.modifiers & Qt::ControlModifier;
    const bool shift = k.modifiers & Qt::ShiftModifier;
    if (k.down && !ctrl) {
        // The digits reach the first eight parts; 9 and 0 are the picture
        // and the song, as they were before there was a ninth part. That
        // one, the Sampler, is Shift+1: the first key again, a row up. It
        // arrives as "!" on most keyboards; where the digits themselves
        // are typed with Shift (French), Shift+1 is the 1 and "!" a key
        // of its own, so the 1 it types stays the Drums.
        if (m_partCount > 8 && (k.key == Qt::Key_Exclam || (k.key == Qt::Key_1 && shift && k.text != QLatin1String("1")))) {
            setPage(8);
            return true;
        }
        if (k.key >= Qt::Key_1 && k.key <= Qt::Key_9) {
            const int page = k.key - Qt::Key_1;
            if (k.key == Qt::Key_9)
                setPage(visualPage());
            else if (page < m_partCount)
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
        case Qt::Key_G: toggleQuantise(); return true;
        case Qt::Key_Backspace: stopAll(); return true;
        case Qt::Key_P: togglePause(); return true;
        case Qt::Key_Z:
            if (shift)
                redoTake();
            else
                undoTake();
            return true;
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
    // The window's own Esc goes to escape() with the pad's state; a key
    // that reaches here (a script's) has no pad to give back.
    if (k.key == Qt::Key_Escape) {
        if (k.down && !k.repeat)
            escape(false);
        return true;
    }
    if (k.key == Qt::Key_Question)
        return false;
    // Enter plays and pauses everything, on every page, as Ctrl+P does.
    // What Return did on a page (hold, latch, keep, drop) is Shift+Enter
    // there now, and the overdub Shift+Return was is F10: the pages still
    // hear the keys they always did, translated here.
    if (k.key == Qt::Key_Return || k.key == Qt::Key_Enter) {
        if (!shift) {
            if (k.down && !k.repeat)
                togglePause();
            return true;
        }
        KeyEvent plain = k;
        plain.modifiers &= ~int(Qt::ShiftModifier);
        return keyForPage(plain);
    }
    if (k.key == Qt::Key_F10) {
        KeyEvent overdub = k;
        overdub.key = Qt::Key_Return;
        overdub.modifiers |= Qt::ShiftModifier;
        return keyForPage(overdub);
    }
    return keyForPage(k);
}

bool Session::keyForPage(const KeyEvent &k) {
    std::lock_guard lock(m_lock);
    Page &p = *m_pages[size_t(m_page)];
    // Just after Space, ↑ and ↓ make the loop longer or shorter (the page
    // gets them back once they rest).
    if ((k.key == Qt::Key_Up || k.key == Qt::Key_Down) && !(k.modifiers & Qt::ControlModifier) &&
        retakeLength(0) > 0 && now() - m_lastTake.at <= kRetakeSecs) {
        if (k.down)
            adjustTake(k.key == Qt::Key_Up ? 1 : -1);
        return true;
    }
    if (p.spec.control->key(k, *p.ctx))
        return true;
    // Space loops what was just played. Over this part's own playing loop it
    // layers on top of it, as one loop; Shift+Space makes a new loop instead,
    // in a free slot, in that loop's place.
    if (k.key == Qt::Key_Space) {
        if (k.down && !k.repeat && p.part >= 0)
            toggleRecording(*p.ctx, (k.modifiers & Qt::ShiftModifier) == 0);
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
                m_redoOrder.erase(std::remove(m_redoOrder.begin(), m_redoOrder.end(), p), m_redoOrder.end());
                m_takeOrder.push_back(p);
                // The studio keeps only so many of a part's changes.
                if (std::count(m_takeOrder.begin(), m_takeOrder.end(), p) > kUndoSteps)
                    m_takeOrder.erase(std::find(m_takeOrder.begin(), m_takeOrder.end(), p));
                m_loopSince[p] = m_view.beat;
            }
            if (m_view.parts[p].playing != m_lastPlaying[p]) {
                m_lastPlaying[p] = m_view.parts[p].playing;
                if (m_view.parts[p].playing >= 0)
                    m_lastLoopSlot[p] = m_view.parts[p].playing;
                m_loopSince[p] = m_view.beat;
            }
        }
        if (m_takeOrder.size() > size_t(kUndoSteps * kMaxParts))
            m_takeOrder.erase(m_takeOrder.begin(), m_takeOrder.end() - kUndoSteps * kMaxParts);
        const long bin = long(std::floor(m_view.time * 4.0));
        // Sound bridges a pause in the playing only while a hand is on the
        // page making it (a finger down, a key held): a chord held through a
        // bar. A bed latched and left to sound would bridge every pause, and
        // a loop made over it would reach back to when it was latched.
        const int here = m_pages[size_t(m_page)]->part;
        const bool hand = m_last.count > 0 || !m_keysDown.empty();
        for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p) {
            const bool on = m_view.parts[p].voices > 0 && m_view.parts[p].playing < 0;
            const bool held = on && p == here && hand;
            // Bins skipped since the last view were the same as now.
            for (long b = std::max(m_soundBin[p] + 1, bin - kSoundBins + 1); b <= bin; ++b) {
                m_sounding[p][((b % kSoundBins) + kSoundBins) % kSoundBins] = on;
                m_handSounding[p][((b % kSoundBins) + kSoundBins) % kSoundBins] = held;
            }
            m_soundBin[p] = bin;
        }
    }
    // (Not in the first moments of the pause: a view the studio published
    // before it heard the pause still shows the loops playing.)
    if (m_notice == kPausedNotice && now() - m_pausedAt > 0.25) {
        bool playing = false;
        for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
            playing |= m_view.parts[p].playing >= 0;
        if (playing) {
            m_notice = QStringLiteral("Playing");
            ++m_noticeSeq;
        }
    }
    m_tutorial.update(*this);
}

// No stretch longer than maxGap in [t0, t1] where the part was silent.
bool Session::soundedThrough(int part, double t0, double t1, double maxGap, bool handOnly) const {
    if (part < 0 || part >= kMaxParts)
        return false;
    const long b0 = long(std::floor(t0 * 4.0)), b1 = long(std::floor(t1 * 4.0));
    if (b1 - b0 >= kSoundBins || b1 > m_soundBin[part] + 2)
        return false;
    double silent = 0;
    for (long b = b0; b <= b1; ++b) {
        const long i = ((b % kSoundBins) + kSoundBins) % kSoundBins;
        if (handOnly ? m_handSounding[part][i] : m_sounding[part][i])
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
    pc.mouseOver = m_mouseOver;
    pc.mouse = m_mousePx;
    pc.level = m_level;
    console::resetBrowseChips();
    console::setPaintLevel(p.part >= 0 ? m_level : 2);
    m_studioTag = {};
    const PartControl::PlayArea zoom = zoomArea();
    painter.save();
    if (zoom.zooms()) {
        // The page paints on a larger pad whose play area is the pad seen;
        // what lies outside it (the fine-tuning) is cut away, the band
        // under the pad kept.
        const qreal w = pad.width() / (zoom.x1 - zoom.x0), h = pad.height() / (zoom.y1 - zoom.y0);
        const qreal left = pad.left() - zoom.x0 * w, bottom = pad.bottom() + zoom.y0 * h;
        pc.pad = QRectF(left, bottom - h, w, h);
        pc.visible = pad;
        pc.hoverX = zoom.x0 + m_mouseX * (zoom.x1 - zoom.x0);
        pc.hoverY = zoom.y0 + m_mouseY * (zoom.y1 - zoom.y0);
        QRegion clip(pad.adjusted(-1, -1, 1, 1).toAlignedRect());
        clip += QRectF(pad.left() - 40, pad.bottom() + 3, pad.width() + 80, Session::kBandHeight + 40).toAlignedRect();
        painter.setClipRegion(clip);
    }
    p.spec.control->paint(painter, pc);
    painter.restore();
    if (zoom.zooms()) {
        // The pad's own frame and legend, and on its top edge the one line
        // that says where the rest went.
        PaintContext edge = pc;
        edge.pad = pad;
        // The frame as console::pad draws it, over the edges cut away; its
        // inside is the page's.
        painter.setPen(QPen(pc.color, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(pad.adjusted(0, -console::kHead, 0, 0));
        painter.fillRect(QRectF(pad.left() + 1, pad.top() - console::kHead + 1, pad.width() - 2, console::kHead - 1),
                         Qt::black);
        const QFont lf = console::font(13, true);
        const QString legend = pageName(m_page).toUpper();
        const QRectF lr(pad.left() + 16, console::tabY(edge) - 9, console::textWidth(lf, legend) + 16, 18);
        painter.fillRect(lr, Qt::black);
        console::text(painter, lr, legend, pc.color, 13, true, Qt::AlignCenter);
        const auto choices = p.spec.control->soundChoices();
        const int chosen = p.spec.control->soundChoice();
        if (chosen >= 0 && chosen < int(choices.size()))
            console::variation(painter, edge, choices[size_t(chosen)].name);
        const QString line = QStringLiteral("▸ IN STUDIO: %1").arg(zoom.what.toUpper());
        const QFont f = console::font(11, true);
        const qreal tw = console::textWidth(f, line) + 16;
        m_studioTag = QRectF(pad.right() - tw - 80, console::tabY(edge) - 10, tw, 20);
        const bool over = m_mouseOver && m_studioTag.contains(m_mousePx);
        painter.fillRect(m_studioTag, Qt::black);
        painter.setPen(QPen(console::alpha(pc.color, over ? 1.0f : 0.5f), 1, Qt::DashLine));
        painter.drawRect(m_studioTag);
        console::text(painter, m_studioTag, line, console::alpha(pc.color, over ? 1.0f : 0.7f), 11, true, Qt::AlignCenter);
    }
    m_padRect = pad;
    m_soundChip = {};
    m_soundNext = {};
    if (m_level == 0 && p.part >= 0 && p.part < 7 && !p.spec.control->soundChoices().empty() &&
        p.spec.control->soundsOnPad()) {
        // Its sounds are on the pad: the row under it is the clip slots alone.
        const QRectF row = console::bandRow(pc, 0);
        const QRectF clips = console::lastClipStrip();
        const qreal left = !clips.isEmpty() && std::abs(clips.top() - row.top()) < 2 ? clips.right() + 8 : row.left();
        painter.fillRect(QRectF(left, row.top() - 4, row.right() - left + 4, row.height() + 8), Qt::black);
    } else if (m_level == 0 && p.part >= 0 && p.part < 7 && !p.spec.control->soundChoices().empty()) {
        // A newcomer's first row: the clip slots once there is a loop, and in
        // place of the row of sounds one chip that opens them all, heard.
        const QRectF row = console::bandRow(pc, 0);
        const QRectF clips = console::lastClipStrip();
        bool anyClip = false;
        for (const auto &c : pc.view->clips)
            anyClip |= c.state != ClipView::Empty;
        const qreal left = anyClip && !clips.isEmpty() && std::abs(clips.top() - row.top()) < 2 ? clips.right() + 8
                                                                                                   : row.left();
        painter.fillRect(QRectF(left, row.top() - 4, row.right() - left + 4, row.height() + 8), Qt::black);
        m_soundNext = QRectF(row.right() - console::kNextChipWidth, row.top(), console::kNextChipWidth, row.height());
        console::nextChip(painter, pc, m_soundNext);
        m_soundChip = QRectF(left, row.top(), m_soundNext.left() - 4 - left, row.height());
        const auto choices = p.spec.control->soundChoices();
        const int chosen = p.spec.control->soundChoice();
        const QString name = chosen >= 0 && chosen < int(choices.size()) ? choices[size_t(chosen)].name.toUpper()
                                                                          : QStringLiteral("—");
        const bool over = m_mouseOver && m_soundChip.contains(m_mousePx);
        painter.fillRect(m_soundChip, console::alpha(pc.color, over ? 0.3f : 0.14f));
        painter.setPen(QPen(pc.color, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_soundChip);
        console::text(painter, m_soundChip.adjusted(10, 0, -10, 0),
                      QStringLiteral("SOUND  ·  %1").arg(name), pc.color, 13, true, Qt::AlignLeft | Qt::AlignVCenter);
        console::text(painter, m_soundChip.adjusted(10, 0, -10, 0),
                      QStringLiteral("%1 MORE  ▸").arg(int(choices.size()) - 1), console::alpha(pc.color, 0.75f), 12,
                      true, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (const QRectF fold = foldedRow(pad); !fold.isEmpty()) {
        // Level 0: the band's second row folds away under one line that
        // unfolds it.
        painter.fillRect(fold.adjusted(-4, -4, 4, 6), Qt::black);
        const bool over = m_mouseOver && fold.contains(m_mousePx);
        painter.setPen(QPen(console::alpha(pc.color, over ? 0.9f : 0.45f), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(fold);
        console::text(painter, fold, QStringLiteral("▸  MORE CONTROLS  ·  SWING, TONE, GLIDE, LENGTH…"),
                      console::alpha(pc.color, over ? 1.0f : 0.7f), 12, true, Qt::AlignCenter);
    }
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
// The pattern the player settled into, found from the hits alone (times in
// seconds), so it doesn't matter when Space comes: the period that the
// latest playing repeats with, the run of repeats back to where it began,
// and the cycles of it to loop, from a cycle's first hit to where the next
// cycle starts (or would, if Space came early).
struct Cycle {
    double period = 0, start = 0, end = 0;
    int cycles = 0, matched = 0;
    std::vector<double> rounds; // when each whole round began, oldest first
};

// The shortest loop worth making of phrases (the Sampler's chops), in
// seconds: about a bar at the tempos tracks are made at.
constexpr double kLeastPhraseLoop = 1.8;

Cycle findCycle(const std::vector<Hit> &hits, bool oneCycle = false) {
    const int n = int(hits.size());
    // (A chop that went round three times is a pattern already.)
    if (n < (oneCycle ? 3 : 4))
        return {};
    // Candidates: the gaps between a recent hit and an earlier one of the
    // same sound.
    std::vector<double> cands;
    for (int j = std::max(0, n - 24); j < n; ++j)
        for (int i = j - 1; i >= 0; --i) {
            const double d = hits[size_t(j)].x - hits[size_t(i)].x;
            if (d > 8.0)
                break;
            if (d >= 0.25 && hits[size_t(i)].key == hits[size_t(j)].key)
                cands.push_back(d);
        }
    std::sort(cands.begin(), cands.end());
    std::vector<double> periods;
    for (double d : cands)
        if (periods.empty() || d > periods.back() * 1.03)
            periods.push_back(d);
    auto twinOf = [&](int j, double p, double tol) {
        int best = -1;
        double bestD = tol;
        for (int i = j - 1; i >= 0; --i) {
            const double d = std::fabs(hits[size_t(j)].x - p - hits[size_t(i)].x);
            if (hits[size_t(i)].x < hits[size_t(j)].x - p - tol)
                break;
            if (hits[size_t(i)].key == hits[size_t(j)].key && d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return best;
    };
    std::vector<Cycle> found;
    for (double p0 : periods) {
        const double tol = 0.05 + 0.04 * p0;
        // From the latest hit back: each with its twin a period earlier.
        // One stray hit is forgiven; two end the run.
        int matched = 0, misses = 0, k = -1;
        std::vector<double> gaps;
        for (int j = n - 1; j >= 0; --j) {
            const int t = twinOf(j, p0, tol);
            if (t < 0) {
                if (++misses > 1)
                    break;
                continue;
            }
            ++matched;
            k = j;
            gaps.push_back(hits[size_t(j)].x - hits[size_t(t)].x);
        }
        if (matched < 2 || k < 0)
            continue;
        std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
        const double p = gaps[gaps.size() / 2];
        // The run's first cycle starts at the twin of its earliest repeat;
        // follow that hit forward a period at a time: each is a cycle's
        // first hit.
        std::vector<int> chain{twinOf(k, p0, tol)};
        if (chain[0] < 0)
            continue;
        for (;;) {
            const double want = hits[size_t(chain.back())].x + p;
            int next = -1;
            double bestD = tol;
            for (int i = chain.back() + 1; i < n; ++i) {
                const double d = std::fabs(hits[size_t(i)].x - want);
                if (hits[size_t(i)].key == hits[size_t(chain[0])].key && d < bestD) {
                    bestD = d;
                    next = i;
                }
            }
            if (next < 0)
                break;
            chain.push_back(next);
        }
        if (chain.size() < 2)
            continue;
        Cycle c;
        c.period = p;
        c.matched = matched;
        // A cycle only begun (its first hit played, then Space) ends the
        // loop there; a whole one ends a period after its first hit.
        const double lastStart = hits[size_t(chain.back())].x;
        int inLast = 0, inPrev = 0;
        for (const Hit &h : hits) {
            if (h.x >= lastStart - tol / 2)
                ++inLast;
            else if (h.x >= lastStart - p - tol / 2)
                ++inPrev;
        }
        // One hit a cycle is a pulse, not a pattern: the phrase decides.
        // Where every hit is a phrase of its own (a chop), one chop held
        // or played round and round is a pattern too, but only when it is
        // all that was played lately: one of several chops coming round is
        // part of a longer pattern.
        if (inPrev <= 1 && inLast <= 1) {
            bool alone = oneCycle;
            for (const Hit &h : hits)
                if (h.x >= lastStart - 2 * p - tol)
                    alone &= h.key == hits[size_t(chain.back())].key;
            if (!alone)
                continue;
        }
        if (inLast < 0.6 * inPrev) {
            c.end = lastStart;
            chain.pop_back();
        } else {
            c.end = lastStart + p;
        }
        if (chain.empty())
            continue;
        // Two cycles of it when they are short, so the loop keeps the
        // hand's own feel; one when it is long already. Where each hit is
        // a phrase, one cycle is the loop, but never under about a bar:
        // as many cycles as reach that, of those that were played.
        int take = !oneCycle && chain.size() >= 2 && 2 * p <= 6.0 ? 2 : 1;
        if (oneCycle)
            take = std::clamp(int(std::ceil(kLeastPhraseLoop / p - 1e-6)), 1, int(chain.size()));
        c.cycles = take;
        for (int i : chain)
            c.rounds.push_back(hits[size_t(i)].x);
        c.start = hits[size_t(chain[chain.size() - size_t(take)])].x;
        found.push_back(c);
    }
    if (found.empty())
        return {};
    // The shortest period that explains nearly as much as the best one: the
    // pattern itself, not two or four of it.
    int most = 0;
    for (const Cycle &c : found)
        most = std::max(most, c.matched);
    for (const Cycle &c : found)
        if (c.matched >= 0.85 * most)
            return c;
    return found.front();
}

// Where a first loop's grid sits best under the playing: the loop that
// [start, stop) found begins on a hit, and that hit may be a little late or
// early against the rest (a kick played behind the beat). Quantise off, the
// loop keeps every hit as played, so its bar would start on that one hit
// and everything else would sit off the beat. A shadow quantise instead:
// the sixteenths of the loop's tempo are laid over the hits, moved by up to
// half a sixteenth and stretched by up to 2%, and the window goes where the
// hits fall closest to them, most of them counting, a stray or swung one
// only so much. The hits themselves stay where they were played.
struct GridFit {
    double start, stop;
};

GridFit fitGrid(const std::vector<Hit> &hits, double start, double stop) {
    const double secs = stop - start;
    if (secs <= 0.15)
        return {start, stop};
    const double step = secs / (freeLoopBeats(secs) * 4.0); // a sixteenth, in seconds
    std::vector<double> xs;
    for (const Hit &h : hits)
        if (h.x >= start - step / 2 && h.x < stop - step / 2)
            xs.push_back(h.x);
    // (Two numbers fitted to three hits would fit anything.)
    if (xs.size() < 4)
        return {start, stop};
    // The cost of a hit off the grid by r sixteenths: r², but never more
    // than that of a hit a third of a sixteenth off.
    auto cost = [&](double origin, double unit) {
        double sum = 0;
        for (double x : xs) {
            const double u = (x - origin) / unit;
            const double r = u - std::round(u);
            sum += std::min(r * r, 1.0 / 9.0);
        }
        return sum;
    };
    double bestStart = start, bestScale = 1, best = cost(start, step);
    for (int si = -20; si <= 20; ++si) {
        const double scale = 1.0 + si * 0.001;
        for (int oi = -50; oi <= 50; ++oi) {
            const double origin = start + oi * step / 100.0;
            // (A hair worse than where the loop's own hit put it: the hit
            // keeps its place unless the grid is truly better elsewhere.)
            const double c = cost(origin, step * scale) + 1e-4 * (std::abs(si) + std::abs(oi)) / 70.0;
            if (c < best) {
                best = c;
                bestStart = origin;
                bestScale = scale;
            }
        }
    }
    // Moved only when the hits sit clearly closer to the grid (2 ms nearer,
    // on the whole): a hair's gain is the hand's own scatter.
    const double gain = (std::sqrt(cost(start, step) / double(xs.size())) - std::sqrt(best / double(xs.size()))) * step;
    if (gain < 0.002)
        return {start, stop};
    return {bestStart, bestStart + secs * bestScale};
}

[[maybe_unused]] double settledLoop(const std::vector<Hit> &hits, double end, double earliest, const std::vector<double> &periods,
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
    if (m_debugLog) {
        const double nowT = now();
        dlog("SPACE part=%d layer=%d now=%.4f beat=%.4f bpm=%.2f playing=%d recording=%d held=%d voices=%d", part,
             int(layer), nowT, ctx.beatAt(nowT), m_view.bpm, v.playing, v.recording, ctx.heldCount, v.voices);
        for (int q = 0; q < m_view.partCount; ++q)
            if (m_view.parts[q].playing >= 0)
                dlog("  part %d plays slot %d len=%.3f", q, m_view.parts[q].playing + 1,
                     double(m_view.parts[q].clips[m_view.parts[q].playing].lengthBeats));
        dlog("  page says: %s", qPrintable(pageStatus()));
        const size_t n = std::min(ctx.onsetHead, ctx.onsets.size());
        for (size_t i = n; i-- > 0;) {
            const auto &o = ctx.onsets[(ctx.onsetHead - 1 - i) % ctx.onsets.size()];
            const double b = ctx.beatAt(o.t);
            if (b > ctx.beatAt(nowT) - 64)
                dlog("  onset beat=%9.4f t=%9.4f real=%d key=%08x", b, o.t, int(o.real), o.key);
        }
    }
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
    // Layering onto this part's own loop: only what was played since the
    // loop last changed. What came before is in it already.
    // A new loop in place of it (Shift+Space) takes only that too: what
    // was played before the playing loop last changed is in that loop.
    const bool layering = layer && m_view.parts[part].playing >= 0;
    const bool sinceLoop = m_view.parts[part].playing >= 0;
    const double since = sinceLoop ? m_loopSince[part] : -1e18;
    std::vector<Context::Onset> times;
    const size_t n = std::min(ctx.onsetHead, ctx.onsets.size());
    for (size_t i = 0; i < n; ++i) {
        const auto &o = ctx.onsets[(ctx.onsetHead - 1 - i) % ctx.onsets.size()];
        if (!sinceLoop || ctx.beatAt(o.t) >= since - 0.02)
            times.push_back(o);
    }
    dlog("  layering=%d since=%.4f onsets kept=%d", int(layering), since, int(times.size()));
    std::sort(times.begin(), times.end(), [](const auto &x, const auto &y) { return x.t < y.t; });
    bool handOnly = false;
    for (const auto &pg : m_pages)
        handOnly = handOnly || (pg->part == part && !pg->spec.control->soundLeftOnJoinsThePlaying());
    double first = -1, earliest = -1, last = -1, lastReal = -1;
    bool clocked = false; // sounds carried on through the phrase: it already runs on the clock
    for (size_t i = times.size(); i-- > 0;) {
        const double t = times[i].t;
        if (t > nowT + 0.05)
            continue;
        // A pause is two bars with nothing played and nothing sounding. After
        // the last note any sound counts (a chord ringing out, a bed left on:
        // that is the loop); between notes, on a page that says so, only
        // sound a hand was making, so a bed latched and left doesn't join new
        // playing to old.
        if (last < 0) {
            if (nowT - t > 8 * spb && !soundedThrough(part, t, nowT, 8 * spb))
                break;
            last = t;
        } else if (earliest - t > 8 * spb && !soundedThrough(part, t, earliest, 8 * spb, handOnly)) {
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
    // Knobs moved over this part's own loop since it last changed, on a page
    // whose knob moves are playing (the Bass page's shapers): they go into
    // the layer as they were moved, in their place in the loop, so a layer
    // that holds them spans the loop.
    bool knobsLayer = false;
    for (const auto &pg : m_pages)
        knobsLayer = knobsLayer || (pg->part == part && pg->spec.control->layersKeepKnobMoves());
    const bool turned = layering && knobsLayer && ctx.lastTurned >= 0 && ctx.lastTurned <= nowT + 0.05 &&
                        ctx.beatAt(ctx.lastTurned) >= since - 0.02;
    // A layer ends where the knobs last moved, on the sixteenth after it
    // (what sits within an eighth of a beat of its end belongs to the next
    // round): the moves up to there, a loop's worth, are the layer.
    const double turnedEnd = turned ? std::ceil((ctx.beatAt(ctx.lastTurned) + 0.13) * 4.0) / 4.0 : 0;
    if (first < 0 && turned) {
        const int into = m_view.parts[part].playing;
        const double length = m_view.parts[part].clips[into].lengthBeats;
        StudioMsg m;
        m.kind = StudioMsg::TakeBars;
        m.part = int8_t(part);
        m.slot = int8_t(slot);
        m.d0 = turnedEnd - length;
        m.d1 = turnedEnd;
        m.value = into + 1;
        m.d2 = since;
        dlog("  knob moves only, last at %.4f -> TakeBars %.4f..%.4f layered onto slot %d", ctx.beatAt(ctx.lastTurned),
             m.d0, m.d1, into + 1);
        ctx.post(m);
        m_view.parts[part].recording = slot;
        handOver();
        m_lastTake = {part, slot, into, turnedEnd, length, since, now(), m_view.parts[part].takes, false, 0, 0, {}, 0};
        m_lastTake.base = length;
        ctx.notice(QStringLiteral("Layered your moves onto slot %1 · ↑↓ longer, shorter").arg(into + 1));
        return;
    }
    // (A layer with nothing new played has nothing to add: the loop is what
    // sounds.)
    if (first < 0 && m_view.parts[part].voices > 0 && !(layering && ctx.heldCount == 0)) {
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
    dlog("  phrase: first=%.4f earliest=%.4f last=%.4f lastReal=%.4f clocked=%d", first, earliest, last, lastReal,
         int(clocked));
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
        double start = first, stop = nowT;
        bool settled = false;
        Cycle cycle = findCycle(hits, m_pages[size_t(m_page)]->spec.control->loopsOneCycle());
        if (cycle.cycles > 0) {
            start = cycle.start;
            stop = cycle.end;
            settled = true;
            dlog("  cycle: period=%.4f s, %d cycle(s) %.4f..%.4f, %d hits repeat", cycle.period, cycle.cycles,
                 cycle.start, cycle.end, cycle.matched);
        }
        // Without a pattern, the loop only sets the tempo when Space came in
        // the flow of the playing, about where the next note would have
        // come: the phrase then ends there. After a wait, or tinkering that
        // never settled, the tempo stays and the playing is taken in bars
        // of it, as over a loop (↑↓ then set how many).
        bool inFlow = false;
        if (!settled && hits.size() >= 2) {
            std::vector<double> gaps;
            for (size_t i = 1; i < hits.size(); ++i)
                if (hits[i].x - hits[i - 1].x > 0.05)
                    gaps.push_back(hits[i].x - hits[i - 1].x);
            if (!gaps.empty()) {
                std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
                const double gap = gaps[gaps.size() / 2];
                inFlow = nowT - hits.back().x <= 1.6 * gap + 0.1;
            }
        }
        if (!settled && !inFlow) {
            dlog("  no pattern and Space after a wait: the tempo stays, bars of it");
            goto bars;
        }
        dlog("  decided (free, sets the tempo): first=%.4f start=%.4f end=%.4f -> capture %.4f s (%.4f beats at the old "
             "tempo) settled=%d hits=%d slot=%d",
             first, start, stop, stop - start, (stop - start) / spb, int(settled), int(hits.size()), slot + 1);
        // The loop's bar where the playing's own beat is, not on its first
        // hit; then tightened while quantise is on, and off, each hit where
        // it was heard against that beat.
        // (Not a loop of phrases, the Sampler's chops: where each begins is
        // the hand's, on no grid of its own.)
        const bool phrases = m_pages[size_t(m_page)]->spec.control->loopsOneCycle();
        const GridFit fit = phrases ? GridFit{start, stop} : fitGrid(hits, start, stop);
        dlog("  grid fit: start %+.1f ms, length %+.2f%%", (fit.start - start) * 1000.0,
             ((fit.stop - fit.start) / (stop - start) - 1.0) * 100.0);
        // (↑↓ take rounds on the same grid: a time moves as the window did.)
        const double scale = (fit.stop - fit.start) / (stop - start);
        const double from = start;
        auto onGrid = [&](double x) { return fit.start + (x - from) * scale; };
        start = fit.start;
        stop = fit.stop;
        if (settled) {
            cycle.period *= scale;
            for (double &r : cycle.rounds)
                r = onGrid(r);
        }
        ctx.capture(slot, start, stop, m_view.quantise ? 0.85f : 0.0f);
        m_view.parts[part].recording = slot;
        handOver();
        m_lastTake = {};
        m_lastTake.part = part;
        m_lastTake.slot = slot;
        m_lastTake.end = stop;
        m_lastTake.at = now();
        m_lastTake.takes = m_view.parts[part].takes;
        m_lastTake.free = true;
        m_lastTake.unit = settled ? cycle.period : stop - start;
        m_lastTake.n = settled ? cycle.cycles : 1;
        m_lastTake.rounds = settled ? cycle.rounds : std::vector<double>{start};
        ctx.notice(settled ? QStringLiteral("Loop of what you repeated, slot %1, sets the tempo · ↑↓").arg(slot + 1)
                           : QStringLiteral("Loop in slot %1 sets the tempo · ↑↓").arg(slot + 1));
        return;
    }
bars:
    // Sound that went on after the last note (a riser building into its
    // drop, an impact ringing out) is part of what was played.
    bool carried = clocked; // sound that carries itself on (a riser, a running arp)
    {
        // A note's own release isn't that: only sound going on well past it,
        // on a page that says its sound carries on by itself (it sends
        // Continue: a riser, a running arp). A chord's long tail is just its
        // tail, not more playing.
        const double sounding = clocked ? lastSounding(part, last) : -1;
        if (sounding > last + 2 * spb) {
            last = std::min(sounding, nowT);
            carried = true;
        }
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
        // A held sound still being shaped (slid, pressed, its tone turned)
        // is still being played: the phrase runs on to the last of it.
        double played = lastReal;
        if (ctx.lastShaped > played && ctx.lastShaped <= nowT + 0.05)
            played = ctx.lastShaped;
        const double bar = std::ceil((ctx.beatAt(played) + 0.5) / kBeatsPerBar) * kBeatsPerBar;
        end = std::min(bar, std::round(beatNow * 4.0) / 4.0);
    }
    // Whatever the rounding, the last note played is in the take: Space
    // right after a note loops it too.
    if (lastReal >= 0)
        end = std::max(end, std::ceil((ctx.beatAt(lastReal) + 0.13) * 4.0) / 4.0);
    // A note held and let go before Space is in it to its end: a held root
    // isn't cut to the beat after it began.
    if (ctx.heldCount == 0 && lastReal >= 0 && ctx.lastRelease > lastReal && ctx.lastRelease <= nowT + 0.05)
        end = std::max(end, std::min(std::ceil(ctx.beatAt(ctx.lastRelease) * 4.0) / 4.0,
                                     std::ceil(beatNow * 4.0) / 4.0));
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
        // Two cycles must fit in what was played. The end sits a beat
        // after the last note, so a phrase whose notes come every other
        // beat ends before its cycle does: the first cycle may start a
        // little before the first note (the score still checks the match).
        if (end - 2 * p < beatFirst - std::max(0.325, std::min(2.0, 0.3 * p)))
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
        // Played once: the phrase, in whole bars, with no bars of silence
        // added to round it, and no more than its last four bars (↑ just
        // after Space takes more). Longer
        // playing that never repeated is a performance, not a loop; a loop
        // already playing that is longer still sets the length below. Sound
        // that carries itself (a build into its drop) stays whole.
        const double played = std::ceil((end - beatFirst - 0.25) / kBeatsPerBar);
        length = kBeatsPerBar * std::clamp(played, 1.0, carried ? 16.0 : 4.0);
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
    // A layer fits its loop: the shortest length that goes into the loop a
    // whole number of times and still holds all that was played (what is
    // added before it is silence), and never longer than the loop.
    double layerBase = 0;
    if (layer && m_view.parts[part].playing >= 0) {
        layerBase = m_view.parts[part].clips[m_view.parts[part].playing].lengthBeats;
        bool spans = false;
        for (const auto &pg : m_pages)
            spans = spans || (pg->part == part && pg->spec.control->layersSpanTheLoop());
        spans = spans || turned;
        if (layerBase > 0 && spans) {
            if (std::fabs(layerBase - length) > 1e-6)
                dlog("  layer over a %.3f-beat loop spans it (the page's choice)", layerBase);
            length = layerBase;
        } else if (layerBase > 0) {
            const auto fits = layerLengths(layerBase);
            double fit = fits.back();
            for (double d : fits)
                if (d >= length - 1e-6) {
                    fit = d;
                    break;
                }
            if (std::fabs(fit - length) > 1e-6)
                dlog("  layer over a %.3f-beat loop: %.3f beats fits it as %.3f", layerBase, length, fit);
            length = fit;
        }
    }
    // Knobs still moving after the last note: the layer runs on to them.
    if (turned)
        end = std::max(end, turnedEnd);
    const int bars = int(std::lround(length / kBeatsPerBar));
    StudioMsg m;
    m.kind = StudioMsg::TakeBars;
    m.part = int8_t(part);
    m.slot = int8_t(slot);
    m.d0 = end - length;
    m.d1 = end;
    dlog("  decided: first=%.4f last=%.4f lastReal=%.4f end=%.4f length=%.3f settled=%d -> TakeBars %.4f..%.4f slot=%d",
         ctx.beatAt(first), ctx.beatAt(last), lastReal >= 0 ? ctx.beatAt(lastReal) : -1.0, end, length, int(settled),
         m.d0, m.d1, slot + 1);
    // Space: on top of this part's playing loop, as one loop (Shift+Space
    // asks for a new loop instead).
    const int into = m_view.parts[part].playing;
    if (layer && into >= 0) {
        m.value = into + 1;
        m.d2 = since;
    }
    ctx.post(m);
    m_view.parts[part].recording = slot;
    handOver();
    m_lastTake = {part, slot, layer && into >= 0 ? into : -1, end, length, m.d2, now(), m_view.parts[part].takes, false, 0, 0, {}, 0};
    m_lastTake.base = m_lastTake.into >= 0 ? layerBase : 0;
    QString what = bars == 1 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(bars);
    if (settled)
        what = QStringLiteral("the %1 you repeated").arg(what);
    if (layer && into >= 0)
        ctx.notice(QStringLiteral("Layered %1 onto slot %2 · ↑↓ longer, shorter").arg(what).arg(into + 1));
    else
        ctx.notice(QStringLiteral("Looping %1 in slot %2 · ↑↓ longer, shorter").arg(what).arg(slot + 1));
}

std::vector<double> Session::layerLengths(double base) {
    std::vector<double> out;
    const double unit = base >= kBeatsPerBar ? kBeatsPerBar : 1.0;
    for (int k = 64; k >= 1; --k) {
        const double d = base / k, units = d / unit;
        if (d >= unit - 1e-6 && std::fabs(units - std::round(units)) < 1e-6)
            out.push_back(d);
    }
    if (out.empty())
        out.push_back(base);
    return out;
}

int Session::freeRounds(int dir) const {
    const LastTake &lt = m_lastTake;
    std::vector<int> rounds{1, 2, 4, 8};
    if (std::find(rounds.begin(), rounds.end(), lt.n) == rounds.end())
        rounds.push_back(lt.n);
    std::sort(rounds.begin(), rounds.end());
    const long at = long(std::find(rounds.begin(), rounds.end(), lt.n) - rounds.begin()) + dir;
    if (at < 0 || at >= long(rounds.size()) || rounds[size_t(at)] * lt.unit < 0.2)
        return 0;
    return rounds[size_t(at)];
}

double Session::retakeLength(int dir) const {
    const LastTake &lt = m_lastTake;
    if (lt.part < 0 || lt.part != m_pages[size_t(m_page)]->part || m_view.parts[lt.part].takes > lt.takes + 1)
        return 0;
    if (lt.free) {
        // As the studio sizes a loop that sets the tempo: whole beats,
        // doubling until the tempo is at least 88.
        const int n = freeRounds(dir);
        if (n <= 0)
            return 0;
        const double secs = n * lt.unit;
        double len = 1;
        while (len * 60.0 / secs < 88.0 && len < 32)
            len *= 2;
        return len;
    }
    // A layer: the lengths that fit its loop.
    if (lt.into >= 0 && lt.base > 0) {
        const auto lengths = layerLengths(lt.base);
        const long at = long(std::find_if(lengths.begin(), lengths.end(),
                                          [&](double d) { return std::fabs(d - lt.length) < 1e-6; }) -
                             lengths.begin()) +
                        dir;
        return at >= 0 && at < long(lengths.size()) ? lengths[size_t(at)] : 0;
    }
    // Whole bars, by doubling and halving, through the length Space chose.
    std::vector<double> lengths{4, 8, 16, 32, 64};
    if (std::find(lengths.begin(), lengths.end(), lt.length) == lengths.end())
        lengths.push_back(lt.length);
    std::sort(lengths.begin(), lengths.end());
    const long at = long(std::find(lengths.begin(), lengths.end(), lt.length) - lengths.begin()) + dir;
    return at >= 0 && at < long(lengths.size()) ? lengths[size_t(at)] : 0;
}

int Session::loopBeats(int dir) const {
    std::lock_guard lock(m_lock);
    // A page that makes its own takes (the Vocal page's audio) says.
    const Page &p = *m_pages[size_t(m_page)];
    if (p.part >= 0) {
        const int beats = p.spec.control->loopBeats(dir);
        if (beats >= 0)
            return beats;
    }
    return int(std::lround(retakeLength(dir)));
}

bool Session::canNewLoop() const {
    std::lock_guard lock(m_lock);
    const Page &p = *m_pages[size_t(m_page)];
    return p.part >= 0 && p.part < m_view.partCount &&
           (m_view.parts[p.part].playing >= 0 || m_view.parts[p.part].queued >= 0);
}

bool Session::newLoop() {
    std::lock_guard lock(m_lock);
    Page &p = *m_pages[size_t(m_page)];
    if (p.part < 0 || p.part >= m_view.partCount)
        return false;
    PartView &pv = m_view.parts[p.part];
    const int slot = pv.playing >= 0 ? pv.playing : pv.queued;
    if (slot < 0) {
        p.ctx->notice(QStringLiteral("No loop plays here: play, then Space makes one"));
        return false;
    }
    // Stopped now, not at the bar: the hands are about to play the new one.
    p.ctx->stopPart(p.part, true);
    pv.playing = pv.queued = -1;
    // The loop just stopped is no longer one to make longer or shorter,
    // and what was played over it isn't part of what comes next.
    if (m_lastTake.part == p.part)
        m_lastTake = {};
    m_loopSince[p.part] = m_view.beat + std::max(0.0, now() - m_view.time) * m_view.bpm / 60.0;
    p.ctx->notice(QStringLiteral("NEW LOOP: slot %1 is kept (F%1 plays it again) · play, then Space").arg(slot + 1));
    return true;
}

void Session::resizeLoop(int dir) {
    std::lock_guard lock(m_lock);
    Page &p = *m_pages[size_t(m_page)];
    if (p.part >= 0 && p.spec.control->loopBeats(0) >= 0) {
        p.spec.control->resizeLoop(dir, *p.ctx);
        return;
    }
    if (retakeLength(0) > 0)
        adjustTake(dir);
}

bool Session::adjustTake(int dir) {
    LastTake &lt = m_lastTake;
    const double t = now();
    if (retakeLength(0) <= 0)
        return false;
    const double next = retakeLength(dir);
    lt.at = t; // each press keeps the arrows' window open
    Context &ctx = context(m_page);
    if (lt.free) {
        const int n = freeRounds(dir);
        const int beats = int(std::lround(next));
        const QString what = beats < int(kBeatsPerBar) ? QStringLiteral("%1 beats").arg(beats)
                             : beats == int(kBeatsPerBar) ? QStringLiteral("1 bar")
                                                          : QStringLiteral("%1 bars").arg(beats / int(kBeatsPerBar));
        if (n <= 0) {
            ctx.notice(dir > 0 ? QStringLiteral("Loop: as long as it goes") : QStringLiteral("Loop: as short as it goes"));
            return true;
        }
        StudioMsg m;
        m.kind = StudioMsg::Recapture;
        m.part = int8_t(lt.part);
        m.slot = int8_t(lt.slot);
        // From the hit that began that round, as played; rounds before the
        // pattern settled go back a period at a time.
        const long r = long(lt.rounds.size()) - n;
        m.d0 = r >= 0 ? lt.rounds[size_t(r)] : lt.rounds.front() + double(r) * lt.unit;
        m.d1 = lt.end;
        m.f = m_view.quantise ? 0.85f : 0.0f; // as the take it redoes
        ctx.post(m);
        lt.n = n;
        dlog("RECAPTURE part=%d slot=%d %d rounds of %.4f s", lt.part, lt.slot + 1, n, lt.unit);
        ctx.notice(QStringLiteral("Loop: %1 (%2 rounds) · ↑↓").arg(what).arg(n));
        return true;
    }
    const double length = next > 0 ? next : lt.length;
    const int bars = int(std::lround(length / kBeatsPerBar));
    const QString what = bars == 1 ? QStringLiteral("1 bar") : QStringLiteral("%1 bars").arg(bars);
    if (length == lt.length) {
        ctx.notice(QStringLiteral("Loop: %1, as %2 as it goes").arg(what, dir > 0 ? QStringLiteral("long")
                                                                                   : QStringLiteral("short")));
        return true;
    }
    StudioMsg m;
    m.kind = StudioMsg::Retake;
    m.part = int8_t(lt.part);
    m.slot = int8_t(lt.slot);
    m.d0 = lt.end - length;
    m.d1 = lt.end;
    if (lt.into >= 0) {
        m.value = lt.into + 1;
        m.d2 = lt.since;
    }
    ctx.post(m);
    m_view.parts[lt.part].recording = lt.slot;
    lt.length = length;
    dlog("RETAKE part=%d slot=%d %.4f..%.4f (%d bars)", lt.part, lt.slot + 1, m.d0, m.d1, bars);
    ctx.notice(QStringLiteral("Loop: %1 · ↑↓").arg(what));
    return true;
}

void Session::undoTake() {
    std::lock_guard lock(m_lock);
    // On a part's page, Ctrl+Z takes back that part's newest change; on the
    // Song page, the newest change anywhere. The studio keeps the last
    // kUndoSteps changes of each part; older ones are gone.
    const int here = m_pages[size_t(m_page)]->part;
    // Something the page promised and hasn't made yet goes first.
    if (here >= 0 && m_pages[size_t(m_page)]->spec.control->takeBackPending(*m_pages[size_t(m_page)]->ctx)) {
        ++m_undos;
        dlog("UNDO pending on page %d", m_page);
        return;
    }
    auto it = m_takeOrder.end();
    for (auto i = m_takeOrder.rbegin(); i != m_takeOrder.rend(); ++i)
        if (here < 0 || *i == here) {
            it = std::next(i).base();
            break;
        }
    if (it == m_takeOrder.end()) {
        context(m_page).notice(here < 0 ? QStringLiteral("No loop to take back")
                                        : QStringLiteral("Nothing on %1 to take back").arg(pageName(here)));
        return;
    }
    const int part = *it;
    if (part == m_lastTake.part)
        m_lastTake = {}; // what ↑↓ would change is gone
    Page *owner = nullptr;
    for (auto &pg : m_pages)
        if (pg->part == part)
            owner = pg.get();
    m_takeOrder.erase(it);
    dlog("UNDO part=%d (on page %d)", part, m_page);
    ++m_undos;
    StudioMsg m;
    m.kind = StudioMsg::UndoTake;
    m.part = int8_t(part);
    m_studio->post(m);
    if (owner)
        owner->spec.control->undone(*owner->ctx);
    m_redoOrder.push_back(part);
    context(m_page).notice(QStringLiteral("Took back the last %1 loop").arg(pageName(part)));
}

void Session::redoTake() {
    std::lock_guard lock(m_lock);
    const int here = m_pages[size_t(m_page)]->part;
    auto it = m_redoOrder.end();
    for (auto i = m_redoOrder.rbegin(); i != m_redoOrder.rend(); ++i)
        if (here < 0 || *i == here) {
            it = std::next(i).base();
            break;
        }
    if (it == m_redoOrder.end()) {
        context(m_page).notice(QStringLiteral("Nothing taken back to bring again"));
        return;
    }
    const int part = *it;
    m_redoOrder.erase(it);
    m_takeOrder.push_back(part);
    dlog("REDO part=%d (on page %d)", part, m_page);
    StudioMsg m;
    m.kind = StudioMsg::RedoTake;
    m.part = int8_t(part);
    m_studio->post(m);
    context(m_page).notice(QStringLiteral("Brought back the %1 loop · Ctrl+Z takes it again").arg(pageName(part)));
}

void Session::startOver() {
    std::lock_guard lock(m_lock);
    const double t = now();
    if (m_startOverAsked < 0 || t - m_startOverAsked > 3.0) {
        m_startOverAsked = t;
        context(m_page).notice(QStringLiteral("Ctrl+N again to clear every loop and set the mix back"));
        return;
    }
    m_startOverAsked = -1;
    const bool kept = keepBeforeClearing();
    clearEverything();
    ++m_freshStarts;
    context(m_page).notice(kept ? QStringLiteral("Starting over: the next loop sets the tempo · Ctrl+Shift+O brings the old song back")
                                : QStringLiteral("Starting over: the next loop sets the tempo"));
}

bool Session::keepBeforeClearing() {
    if (m_startOverBackup.isEmpty() || !anythingMade())
        return false;
    QString error;
    const QJsonObject doc = saveSong(&error);
    if (doc.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(m_startOverBackup).absolutePath());
    return writeSongFile(m_startOverBackup, doc, &error);
}

void Session::clearEverything(bool studio) {
    if (studio) {
        StudioMsg m;
        m.kind = StudioMsg::ClearAll;
        m_studio->post(m);
    }
    // The mix as the studio is about to set it, so the pages see it now.
    for (auto &page : m_pages)
        if (page->part >= 0 && page->part < m_view.partCount)
            for (int c = 0; c < ChannelCount; ++c)
                m_view.parts[page->part].channel.value[c] = page->spec.channel[c];
    for (auto &page : m_pages)
        page->spec.control->startOver(*page->ctx);
    // A fresh start: what was played before it is no phrase for Space.
    for (auto &page : m_pages)
        if (page->ctx) {
            page->ctx->onsetHead = 0;
            page->ctx->heldCount = 0;
            page->ctx->lastShaped = -1;
        }
    m_takeOrder.clear();
    m_redoOrder.clear();
    m_lastTake = {};
    std::fill(std::begin(m_pausedSlots), std::end(m_pausedSlots), -1);
    m_songPaused = false;
    holdMetronome(false); // nothing is paused any more
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p) {
        m_view.parts[p].playing = -1;
        for (auto &c : m_view.parts[p].clips)
            c.state = ClipView::Empty;
    }
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
    // M is the player's say: on is heard at once, paused or not.
    m_metronomeHeld = false;
    StudioMsg m;
    m.kind = StudioMsg::Metronome;
    m.value = !m_view.metronome;
    m_view.metronome = m.value;
    m_studio->post(m);
}

void Session::toggleQuantise() {
    std::lock_guard lock(m_lock);
    StudioMsg m;
    m.kind = StudioMsg::Quantise;
    m.value = !m_view.quantise;
    m_view.quantise = m.value;
    m_studio->post(m);
    context(m_page).notice(m_view.quantise ? QStringLiteral("Quantise on: Space and overdubs tighten hits to the sixteenths")
                                           : QStringLiteral("Quantise off: loops keep your timing as you played it"));
}

// Silences the metronome for a pause, or lets it sound again; it stays on
// (and shown on) throughout. Called with m_lock held.
void Session::holdMetronome(bool hold) {
    if (hold == m_metronomeHeld)
        return;
    m_metronomeHeld = hold;
    if (!m_view.metronome)
        return;
    StudioMsg m;
    m.kind = StudioMsg::Metronome;
    m.value = 1 | (hold ? 2 : 0);
    m_studio->post(m);
}

void Session::stopAll() {
    std::lock_guard lock(m_lock);
    // Remembered, as a pause is: play brings it back.
    bool any = false;
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
        any |= m_view.parts[p].playing >= 0 || m_view.parts[p].queued >= 0;
    if (any)
        for (int p = 0; p < kMaxParts; ++p)
            m_pausedSlots[p] = p < m_view.partCount ? (m_view.parts[p].playing >= 0 ? m_view.parts[p].playing
                                                                                     : m_view.parts[p].queued)
                                                    : -1;
    for (int i = 0; i < m_partCount; ++i)
        context(m_page).stopPart(i, false);
    context(m_page).notice(QStringLiteral("Everything stops at the bar · Ctrl+P plays it again"));
}

bool Session::paused() const {
    std::lock_guard lock(m_lock);
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
        if (m_view.parts[p].playing >= 0 || m_view.parts[p].queued >= 0)
            return false;
    for (int p = 0; p < kMaxParts; ++p)
        if (m_pausedSlots[p] >= 0)
            return true;
    return m_songPaused;
}

Session::Escaped Session::escape(bool padTaken) {
    std::lock_guard lock(m_lock);
    if (closeMenu())
        return Escaped::Menu;
    if (m_tutorial.active()) {
        toggleTutorial();
        return Escaped::Tutorial;
    }
    if (padTaken)
        return Escaped::Pad;
    togglePause();
    return Escaped::Pause;
}

void Session::togglePause() {
    std::lock_guard lock(m_lock);
    Context &ctx = context(m_page);
    bool playing = false;
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
        playing |= m_view.parts[p].playing >= 0 || m_view.parts[p].queued >= 0;
    Page &arrange = *m_pages[size_t(arrangePage())];
    if (playing) {
        // Pause: every loop stops now, and what played is remembered. The
        // Song page's song stops where it is.
        // A part that isn't playing keeps what an earlier pause remembered
        // of it: a loop made while paused plays alone, and pausing it again
        // doesn't forget the loops that were paused before it.
        for (int p = 0; p < kMaxParts; ++p) {
            if (p >= m_view.partCount) {
                m_pausedSlots[p] = -1;
                continue;
            }
            PartView &pv = m_view.parts[p];
            if (pv.playing >= 0 || pv.queued >= 0)
                m_pausedSlots[p] = pv.playing >= 0 ? pv.playing : pv.queued;
        }
        m_songPaused = arrange.spec.control->pause(*arrange.ctx);
        for (int i = 0; i < m_partCount; ++i)
            ctx.stopPart(i, true);
        // Shown at once; the studio's next view says the same.
        for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
            m_view.parts[p].playing = m_view.parts[p].queued = -1;
        // A loop can't be made longer or shorter across a pause: the clock
        // moves on to a bar line when play comes.
        m_lastTake = {};
        holdMetronome(true);
        m_pausedAt = now();
        ctx.notice(kPausedNotice);
        return;
    }
    uint64_t which = 0;
    int count = 0;
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p) {
        const int slot = m_pausedSlots[p];
        if (slot >= 0 && slot < kClipSlots && m_view.parts[p].clips[slot].state != ClipView::Empty) {
            which |= uint64_t(slot + 1) << (4 * p);
            ++count;
        }
    }
    if (count == 0 && !m_songPaused) {
        holdMetronome(false);
        bool made = false;
        for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
            for (const ClipView &c : m_view.parts[p].clips)
                made = made || c.state != ClipView::Empty;
        ctx.notice(made ? QStringLiteral("Nothing was paused: F1–F8 on a page start its loops")
                        : QStringLiteral("Nothing to play yet: play, then Space makes a loop"));
        return;
    }
    StudioMsg m;
    m.kind = StudioMsg::Resume;
    m.d0 = double(which); // four bits a part: whole numbers this size are exact in a double
    m_studio->post(m);
    // After the clock has moved on to the bar line: the first click is the one.
    holdMetronome(false);
    // The bar the loops start again on: the one coming now (the studio moves
    // its clock on to it at once, as nothing plays).
    const double beat = m_view.beat + std::max(0.0, now() - m_view.time) * m_view.bpm / 60.0;
    const int bar = int(std::ceil(beat / kBeatsPerBar - 1e-9));
    if (m_songPaused)
        arrange.spec.control->resume(bar, *arrange.ctx);
    m_songPaused = false;
    for (int p = 0; p < kMaxParts; ++p)
        m_pausedSlots[p] = -1;
    m_lastTake = {};
    ctx.notice(QStringLiteral("Playing"));
}

// --- song files ----------------------------------------------------------------

namespace {

// The names a song file gives the mixer's settings, in the order of the
// Channel and Master enums.
const char *const kChannelNames[ChannelCount] = {"gain", "pan", "filter", "reverb", "echo", "pump", "mute", "solo"};
const char *const kMasterNames[MasterCount] = {"room", "shimmer", "echoTime", "echoFeedback", "filter", "drive", "gain"};

// A setting from 0 to 1, or `fallback` when the file doesn't say.
float unit(const QJsonObject &o, const char *name, float fallback) {
    const QJsonValue v = o.value(QLatin1String(name));
    return v.isDouble() && std::isfinite(v.toDouble()) ? float(std::clamp(v.toDouble(), 0.0, 1.0)) : fallback;
}

}

bool Session::anythingMade() const {
    std::lock_guard lock(m_lock);
    for (int p = 0; p < m_view.partCount && p < kMaxParts; ++p)
        for (const auto &c : m_view.parts[p].clips)
            if (c.state != ClipView::Empty)
                return true;
    return false;
}

int Session::songBars() const {
    std::lock_guard lock(m_lock);
    return m_pages[size_t(arrangePage())]->spec.control->songBars();
}

void Session::playSong(int bar) {
    std::lock_guard lock(m_lock);
    Page &page = *m_pages[size_t(arrangePage())];
    page.spec.control->playSong(bar, *page.ctx);
}

void Session::say(const QString &text) {
    std::lock_guard lock(m_lock);
    context(m_page).notice(text);
}

QJsonObject Session::saveSong(QString *error) {
    std::lock_guard lock(m_lock);
    ClipSet clips;
    if (!m_studio->readClips(clips)) {
        if (error)
            *error = QStringLiteral("Not saved: the audio isn't answering");
        return {};
    }
    QJsonObject doc;
    doc[QStringLiteral("tenfold")] = kSongFormat;
    doc[QStringLiteral("tempo")] = tidyNumber(m_view.bpm, 6);
    QJsonObject key;
    key[QStringLiteral("root")] = m_view.root;
    key[QStringLiteral("family")] = m_view.family;
    key[QStringLiteral("brightness")] = m_view.brightness;
    const ScaleFamily &fam = family(m_view.family);
    key[QStringLiteral("name")] = QStringLiteral("%1 %2").arg(
        QString::fromUtf8(Tuning::noteName(m_view.root)),
        QString::fromUtf8(fam.modeNames[std::clamp(m_view.brightness, 0, fam.modeCount - 1)]));
    doc[QStringLiteral("key")] = key;
    doc[QStringLiteral("metronome")] = m_view.metronome;
    doc[QStringLiteral("quantise")] = m_view.quantise;
    doc[QStringLiteral("page")] = m_pages[size_t(m_page)]->spec.name;
    QJsonObject master;
    for (int i = 0; i < MasterCount; ++i)
        master[QLatin1String(kMasterNames[i])] = tidyFloat(m_view.master[i]);
    doc[QStringLiteral("master")] = master;
    QJsonArray parts;
    for (auto &page : m_pages) {
        if (page->part < 0)
            continue;
        const int p = page->part;
        QJsonObject part;
        part[QStringLiteral("name")] = page->spec.name;
        QJsonObject mix;
        for (int c = 0; c < ChannelCount; ++c)
            mix[QLatin1String(kChannelNames[c])] = tidyFloat(m_view.parts[p].channel.value[c]);
        part[QStringLiteral("mix")] = mix;
        QJsonObject settings;
        page->spec.control->save(settings);
        part[QStringLiteral("settings")] = settings;
        const bool known = p < clips.parts;
        part[QStringLiteral("playing")] = known ? clips.playing[p] : -1;
        // What a pause (or Ctrl+Backspace) stopped, for Ctrl+P after opening.
        if (known && clips.playing[p] < 0 && m_pausedSlots[p] >= 0 && clips.clips[p][m_pausedSlots[p]].length > 0)
            part[QStringLiteral("paused")] = m_pausedSlots[p];
        QJsonArray list;
        for (int c = 0; known && c < kClipSlots; ++c)
            if (clips.clips[p][c].length > 0)
                list.append(clipToJson(c, clips.clips[p][c]));
        part[QStringLiteral("clips")] = list;
        if (known) {
            std::vector<const Command *> sounds;
            for (int c = 0; c < kClipSlots; ++c)
                for (const ClipEvent &e : clips.clips[p][c].events)
                    sounds.push_back(&e.c);
            QJsonObject audio;
            page->spec.control->saveAudio(sounds, audio);
            if (!audio.isEmpty())
                part[QStringLiteral("audio")] = audio;
        }
        parts.append(part);
    }
    doc[QStringLiteral("parts")] = parts;
    QJsonObject song;
    m_pages[size_t(arrangePage())]->spec.control->save(song);
    doc[QStringLiteral("song")] = song;
    QJsonObject shown;
    m_pages[size_t(visualPage())]->spec.control->save(shown);
    doc[QStringLiteral("visual")] = shown;
    return doc;
}

bool Session::openSong(const QJsonObject &doc, QString *error) {
    std::lock_guard lock(m_lock);
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!checkSongFormat(doc, error))
        return false;

    // Everything is read and checked first; nothing changes until the whole
    // file has passed.
    const QJsonValue tempoValue = doc.value(QStringLiteral("tempo"));
    if (!tempoValue.isDouble() || !std::isfinite(tempoValue.toDouble()))
        return fail(QStringLiteral("This song has no tempo"));
    const double bpm = std::clamp(tempoValue.toDouble(), 40.0, 240.0);
    const QJsonObject key = doc.value(QStringLiteral("key")).toObject();
    const int root = std::clamp(key.value(QStringLiteral("root")).toInt(9), 0, 11);
    const int fam = std::clamp(key.value(QStringLiteral("family")).toInt(0), 0, familyCount() - 1);
    const int bright = std::clamp(key.value(QStringLiteral("brightness")).toInt(4), 0, family(fam).modeCount - 1);

    struct PartIn {
        int page = -1;
        QJsonObject mix, settings, audio;
        int playing = -1, paused = -1;
    };
    std::vector<PartIn> partsIn;
    auto set = std::make_unique<ClipSet>();
    set->parts = m_partCount;
    const QJsonValue partsValue = doc.value(QStringLiteral("parts"));
    if (!partsValue.isArray())
        return fail(QStringLiteral("This song has no parts"));
    int unknown = 0;
    for (const QJsonValue &pv : partsValue.toArray()) {
        if (!pv.isObject())
            return fail(QStringLiteral("A part of this song isn't an object"));
        const QJsonObject po = pv.toObject();
        const QString name = po.value(QStringLiteral("name")).toString();
        int page = -1;
        for (int i = 0; i < m_partCount; ++i)
            if (m_pages[size_t(i)]->spec.name == name)
                page = i;
        if (page < 0) {
            // A part this tenfold doesn't have (a newer one's, or a typo):
            // the rest of the song still opens.
            ++unknown;
            continue;
        }
        for (const PartIn &in : partsIn)
            if (in.page == page)
                return fail(QStringLiteral("The song has two %1 parts").arg(name));
        const int p = m_pages[size_t(page)]->part;
        bool filled[kClipSlots] = {};
        const QJsonValue clipsValue = po.value(QStringLiteral("clips"));
        if (!clipsValue.isUndefined() && !clipsValue.isArray())
            return fail(QStringLiteral("%1's clips aren't a list").arg(name));
        for (const QJsonValue &cv : clipsValue.toArray()) {
            int slot = -1;
            ClipSet::Clip clip;
            QString why;
            if (!clipFromJson(cv, &slot, &clip, &why))
                return fail(QStringLiteral("%1: %2").arg(name, why));
            if (filled[slot])
                return fail(QStringLiteral("%1 has two clips in slot %2").arg(name).arg(slot + 1));
            filled[slot] = true;
            set->clips[p][slot] = std::move(clip);
        }
        PartIn in;
        in.page = page;
        in.mix = po.value(QStringLiteral("mix")).toObject();
        in.settings = po.value(QStringLiteral("settings")).toObject();
        in.audio = po.value(QStringLiteral("audio")).toObject();
        const int playing = po.value(QStringLiteral("playing")).toInt(-1);
        in.playing = playing >= 0 && playing < kClipSlots && filled[playing] ? playing : -1;
        const int paused = po.value(QStringLiteral("paused")).toInt(-1);
        in.paused = in.playing < 0 && paused >= 0 && paused < kClipSlots && filled[paused] ? paused : -1;
        partsIn.push_back(in);
    }

    // The song's in. What was going on stops, each page starts over, and
    // the studio swaps every clip at once (no Ctrl+N first: between the two
    // a page would see its slots empty and drop what belongs to them).
    if (m_tutorial.active())
        m_tutorial.stop();
    m_startOverAsked = m_tutorialAsked = -1;
    Page &showing = *m_pages[size_t(m_page)];
    showing.spec.control->leave(*showing.ctx);
    clearEverything(false);
    // A page that keeps its clips' audio takes it back now, started over
    // and before the studio has the clips that play it.
    for (const PartIn &in : partsIn) {
        Page &page = *m_pages[size_t(in.page)];
        std::vector<Command *> sounds;
        for (int c = 0; c < kClipSlots; ++c)
            for (ClipEvent &e : set->clips[page.part][c].events)
                sounds.push_back(&e.c);
        page.spec.control->openAudio(in.audio, sounds, *page.ctx);
    }
    float lengths[kMaxParts][kClipSlots] = {};
    int counts[kMaxParts][kClipSlots] = {};
    for (int p = 0; p < m_partCount && p < kMaxParts; ++p)
        for (int c = 0; c < kClipSlots; ++c) {
            lengths[p][c] = float(set->clips[p][c].length);
            counts[p][c] = int(set->clips[p][c].events.size());
        }
    if (!m_studio->writeClips(*set)) {
        showing.spec.control->enter(*showing.ctx);
        return fail(QStringLiteral("Not opened: the audio isn't answering"));
    }
    // The clips as the studio now has them, so the pages see them as they
    // load (the studio's next view says the same).
    for (int p = 0; p < m_partCount && p < kMaxParts; ++p) {
        PartView &pv = m_view.parts[p];
        pv.playing = pv.queued = pv.recording = -1;
        pv.overdub = false;
        for (int c = 0; c < kClipSlots; ++c) {
            ClipView &cv = pv.clips[c];
            cv.state = lengths[p][c] > 0 ? ClipView::Stopped : ClipView::Empty;
            cv.lengthBeats = lengths[p][c];
            cv.events = counts[p][c];
            cv.phase = 0;
        }
    }
    Context &ctx = context(arrangePage());
    ctx.setTempo(bpm);
    ctx.setKey(root, fam, bright);
    if (doc.value(QStringLiteral("metronome")).toBool() != m_view.metronome)
        toggleMetronome();
    // A song saved before there was a choice was quantised.
    if (doc.value(QStringLiteral("quantise")).toBool(true) != m_view.quantise)
        toggleQuantise();
    const QJsonObject master = doc.value(QStringLiteral("master")).toObject();
    static const float kMasterHome[MasterCount] = {0.35f, 0.1f, 0.5f, 0.35f, 0.5f, 0.0f, 0.8f};
    for (int i = 0; i < MasterCount; ++i)
        ctx.setMaster(i, unit(master, kMasterNames[i], kMasterHome[i]), 0);
    for (int i = 0; i < m_partCount; ++i) {
        const PartIn *in = nullptr;
        for (const PartIn &x : partsIn)
            if (x.page == i)
                in = &x;
        Page &page = *m_pages[size_t(i)];
        for (int c = 0; c < ChannelCount; ++c)
            ctx.setChannel(page.part, c, in ? unit(in->mix, kChannelNames[c], page.spec.channel[c]) : page.spec.channel[c], 0);
        // A part the file doesn't have loads nothing, which every page
        // takes as its defaults: the last song's sound doesn't stay on.
        page.spec.control->load(in ? in->settings : QJsonObject(), *page.ctx);
    }
    Page &arrange = *m_pages[size_t(arrangePage())];
    arrange.spec.control->load(doc.value(QStringLiteral("song")).toObject(), *arrange.ctx);
    // The visualizer's look, when the file has one (an older song opens on
    // the look a new song starts with).
    if (doc.value(QStringLiteral("visual")).isObject()) {
        Page &shown = *m_pages[size_t(visualPage())];
        shown.spec.control->load(doc.value(QStringLiteral("visual")).toObject(), *shown.ctx);
    }
    // What was playing plays again, every part from the same bar line.
    for (const PartIn &in : partsIn)
        if (in.playing >= 0)
            ctx.launchPart(m_pages[size_t(in.page)]->part, in.playing, false);
    // A song saved paused opens paused: Ctrl+P plays what was stopped.
    for (const PartIn &in : partsIn)
        if (in.paused >= 0)
            m_pausedSlots[m_pages[size_t(in.page)]->part] = in.paused;
    // Back on the page that was showing when the song was saved.
    QString shown = doc.value(QStringLiteral("page")).toString();
    if (shown == QLatin1String("Arrange"))
        shown = QStringLiteral("Song"); // what the Song page was called before

    for (int i = 0; i < pageCount(); ++i)
        if (m_pages[size_t(i)]->spec.name == shown)
            m_page = i;
    m_ignored.clear();
    for (int i = 0; i < m_last.count; ++i)
        m_ignored.push_back(m_last.touches[i].id);
    m_pages[size_t(m_page)]->spec.control->enter(*m_pages[size_t(m_page)]->ctx);
    if (error)
        *error = unknown == 0 ? QString()
                 : unknown == 1 ? QStringLiteral("a part this tenfold doesn't have was left out")
                                : QStringLiteral("%1 parts this tenfold doesn't have were left out").arg(unknown);
    return true;
}

}

namespace tf {

// --- the sound browser -------------------------------------------------------

std::vector<PartControl::SoundChoice> Session::soundChoices() const {
    std::lock_guard lock(m_lock);
    return m_pages[size_t(m_page)]->spec.control->soundChoices();
}

int Session::soundChoice() const {
    std::lock_guard lock(m_lock);
    return m_pages[size_t(m_page)]->spec.control->soundChoice();
}

void Session::chooseSound(int index, bool hear) {
    std::lock_guard lock(m_lock);
    Page &p = *m_pages[size_t(m_page)];
    const int count = int(p.spec.control->soundChoices().size());
    if (index < 0 || index >= count)
        return;
    stopAudition();
    p.spec.control->chooseSound(index, *p.ctx);
    if (!hear)
        return;
    // Never into the player's loop: not while a take waits for its bar
    // line, nor while the loop overdubs.
    if (p.part >= 0 && p.part < m_view.partCount &&
        (m_view.parts[p.part].recording >= 0 || m_view.parts[p.part].overdub))
        return;
    const double bpm = std::max(60.0, m_view.bpm);
    const double t0 = now() + 0.03;
    for (const auto &step : p.spec.control->audition())
        m_audition.push_back({t0 + step.beat * 60.0 / bpm, m_page, step.key, step.down});
    std::stable_sort(m_audition.begin(), m_audition.end(),
                     [](const Audition &a, const Audition &b) { return a.t < b.t; });
    playAudition(now());
}

bool Session::auditioning() const {
    std::lock_guard lock(m_lock);
    return !m_audition.empty();
}

void Session::playAudition(double t) {
    if (m_audition.empty())
        return;
    int page = -1;
    while (!m_audition.empty() && m_audition.front().t <= t) {
        const Audition a = m_audition.front();
        m_audition.erase(m_audition.begin());
        Page &p = *m_pages[size_t(a.page)];
        KeyEvent k;
        k.key = a.key;
        k.down = a.down;
        k.text = QString(QChar(a.key)).toLower();
        p.spec.control->key(k, *p.ctx);
        page = a.page;
    }
    // A phrase played to be heard is no phrase for Space to loop: once it
    // is over, Space (the LOOP button) starts from what the hands play next.
    if (m_audition.empty() && page >= 0)
        forgetPlaying(page);
}

void Session::stopAudition() {
    // Every key still down goes up now, so nothing is left sounding.
    std::vector<Audition> ups;
    for (const Audition &a : m_audition)
        if (!a.down)
            ups.push_back(a);
    const int page = m_audition.empty() ? -1 : m_audition.front().page;
    m_audition.clear();
    for (const Audition &a : ups) {
        Page &p = *m_pages[size_t(a.page)];
        KeyEvent k;
        k.key = a.key;
        k.down = false;
        p.spec.control->key(k, *p.ctx);
    }
    if (page >= 0)
        forgetPlaying(page);
}

void Session::forgetPlaying(int page) {
    Context &ctx = *m_pages[size_t(page)]->ctx;
    ctx.onsets.fill({});
    ctx.onsetHead = 0;
}

// --- the LOOP button -----------------------------------------------------------

Session::LoopState Session::loopState() const {
    std::lock_guard lock(m_lock);
    const Page &p = *m_pages[size_t(m_page)];
    if (p.part < 0 || p.part >= m_view.partCount)
        return LoopState::None;
    const PartView &v = m_view.parts[p.part];
    if (v.recording >= 0)
        return LoopState::Waiting;
    if (v.playing >= 0 || v.queued >= 0)
        return LoopState::Layer;
    const Context &ctx = *p.ctx;
    if (!m_audition.empty())
        return LoopState::Empty; // a sound being tried is not playing to keep
    // As Space sees it: something sounding now (a latched bed, an arp left
    // running) is looped as it sounds; else what was played since the
    // part's loop last changed.
    if (v.voices > 0)
        return LoopState::Make;
    if (ctx.onsetHead > 0) {
        const auto &last = ctx.onsets[(ctx.onsetHead - 1) % ctx.onsets.size()];
        if (ctx.beatAt(last.t) >= m_loopSince[p.part] - 1e-6)
            return LoopState::Make;
    }
    return LoopState::Empty;
}

void Session::pressLoop(bool fresh) {
    KeyEvent k;
    k.key = Qt::Key_Space;
    k.modifiers = fresh ? int(Qt::ShiftModifier) : 0;
    k.text = QStringLiteral(" ");
    key(k);
    k.down = false;
    key(k);
}

// --- styles: every page's sound and the tempo at once -----------------------------

const std::vector<Session::Style> &Session::styles() {
    // Each names a sound on each page by its name (as soundChoices() says
    // it, any case), and a tempo.
    static const std::vector<Style> list = {
        {"House", "four on the floor, piano chords", 124,
         {"house", "bounce", "piano", "pluck", "pluck", "euphoria", "edm"}},
        {"Trap", "808s, rolling hats, dark bells", 140, {"trap", "808", "keys", "bell", "glass", "night rain", "trap"}},
        {"Lo-fi", "dusty beats and warm keys", 84,
         {"boom bap", "upright", "keys", "flute", "rhodes", "nostalgia", "lo-fi"}},
        {"Synthwave", "neon, drum machines, 80s synths", 100,
         {"linn", "moog", "strings", "sync", "juno", "neon", "synthwave"}},
        {"Techno", "dark, hypnotic, acid", 130, {"techno", "acid", "dub", "mini", "acid", "dub", "techno"}},
        {"Trance", "supersaws and big anthems", 138,
         {"house", "reese", "trance", "anthem", "supersaw", "stardust", "edm"}},
        {"Amapiano", "log drums and shakers", 112,
         {"amapiano", "log", "piano", "steel pan", "log drum", "city pop", "amapiano"}},
        {"Gamelan", "Balinese bronze and gongs", 96,
         {"gamelan", "marimbula", "gamelan", "gong", "gamelan", "gamelan", "gamelan"}},
    };
    return list;
}

bool Session::applyStyle(int index) {
    std::lock_guard lock(m_lock);
    const auto &all = styles();
    if (index < 0 || index >= int(all.size()))
        return false;
    const Style &st = all[size_t(index)];
    static const char *pages[] = {"drums", "bass", "chords", "lead", "arp", "atmos", "fx"};
    for (int i = 0; i < 7; ++i) {
        const int page = pageNamed(QString::fromLatin1(pages[i]));
        if (page < 0)
            continue;
        Page &p = *m_pages[size_t(page)];
        const auto choices = p.spec.control->soundChoices();
        for (int c = 0; c < int(choices.size()); ++c)
            if (choices[size_t(c)].name.compare(QString::fromUtf8(st.sounds[i]), Qt::CaseInsensitive) == 0) {
                // The atmosphere's scene sounds the moment it is laid: leave
                // it chosen but silent until the player gets there.
                if (QLatin1String(pages[i]) == QLatin1String("atmos"))
                    break;
                p.spec.control->chooseSound(c, *p.ctx);
                break;
            }
    }
    context(m_page).setTempo(st.bpm);
    context(m_page).notice(QStringLiteral("%1 · %2 BPM").arg(QString::fromUtf8(st.name)).arg(st.bpm));
    return true;
}

// --- the next step, for a newcomer ---------------------------------------------

// How each page is played, in a sentence a newcomer can follow.
QString Session::howToPlay(int page) const {
    auto is = [&](const char *name) { return page == pageNamed(QString::fromLatin1(name)); };
    if (is("drums"))
        return QStringLiteral("Your turn: tap the pads, or press Z X C V on the keyboard");
    if (is("bass"))
        return QStringLiteral("Your turn: touch the pad, or press Z X C V B for bass notes");
    if (is("chords"))
        return QStringLiteral("Your turn: hold a column, or press A S D F G for chords");
    if (is("lead"))
        return QStringLiteral("Your turn: slide a finger, or press A S D F G H. A thumb on the left adds a harmony");
    if (is("arp"))
        return QStringLiteral("Your turn: hold notes on the left, or hold A D G");
    if (is("atmos"))
        return QStringLiteral("Your turn: rest fingers on the pad, or press Q W E R for a scene");
    if (is("fx"))
        return QStringLiteral("Your turn: touch the field for a riser, or Z X C V for hits");
    return QStringLiteral("Your turn: click or tap the pad to play");
}

int Session::partsWithLoops() const {
    int parts = 0;
    for (int q = 0; q < m_view.partCount; ++q) {
        bool any = false;
        for (const auto &c : m_view.parts[q].clips)
            any |= c.state != ClipView::Empty;
        parts += any;
    }
    return parts;
}

int Session::suggestedPage() const {
    std::lock_guard lock(m_lock);
    // Once a part loops, the next of the first four without a loop.
    if (loopState() != LoopState::Layer || partsWithLoops() >= 4)
        return -1;
    static const char *order[] = {"drums", "bass", "chords", "lead"};
    for (const char *name : order) {
        const int page = pageNamed(QString::fromLatin1(name));
        if (page < 0 || page == m_page)
            continue;
        const Page &q = *m_pages[size_t(page)];
        bool any = false;
        for (const auto &c : m_view.parts[q.part].clips)
            any |= c.state != ClipView::Empty;
        if (!any)
            return page;
    }
    return -1;
}

QString Session::nextStep() const {
    std::lock_guard lock(m_lock);
    const Page &p = *m_pages[size_t(m_page)];
    if (p.part < 0)
        return m_page == arrangePage() ? QStringLiteral("Press I and your loops become a whole track") : QString();
    switch (loopState()) {
    case LoopState::Waiting: return QStringLiteral("Keep playing: the loop starts on the bar line");
    case LoopState::Make: return QStringLiteral("Like it? Press LOOP IT to keep it going round");
    case LoopState::Layer: {
        const int next = suggestedPage();
        if (next >= 0)
            return QStringLiteral("It loops! Now add %1: press %2 or click its tab")
                .arg(pageName(next).toUpper(), pageKey(next));
        if (m_level == 0)
            return QStringLiteral("A band! Tempo, key, swing and every control are under STUDIO, top right");
        return QStringLiteral("Open 0 SONG and press I: your loops become a track");
    }
    default: break;
    }
    return howToPlay(m_page);
}

}

namespace tf {

QRectF Session::foldedRow(const QRectF &pad) const {
    const Page &p = *m_pages[size_t(m_page)];
    if (m_level > 0 || p.part < 0 || p.part >= 7 || pad.isEmpty())
        return {};
    PaintContext pc;
    pc.pad = pad;
    return console::bandRow(pc, 1);
}

}

namespace tf {

void Session::previewStyle(int index) {
    std::lock_guard lock(m_lock);
    if (!applyStyle(index))
        return;
    stopAudition();
    // Its drums, bass and chords together, a bar of each.
    const double bpm = std::max(60.0, double(styles()[size_t(index)].bpm));
    const double t0 = now() + 0.05;
    for (const char *name : {"drums", "bass", "chords"}) {
        const int page = pageNamed(QString::fromLatin1(name));
        if (page < 0)
            continue;
        for (const auto &step : m_pages[size_t(page)]->spec.control->audition())
            m_audition.push_back({t0 + step.beat * 60.0 / bpm, page, step.key, step.down});
    }
    std::stable_sort(m_audition.begin(), m_audition.end(),
                     [](const Audition &a, const Audition &b) { return a.t < b.t; });
}

bool Session::takeSoundsAsked() {
    std::lock_guard lock(m_lock);
    const bool asked = m_soundsAsked;
    m_soundsAsked = false;
    return asked;
}

}

namespace tf {

PartControl::PlayArea Session::zoomArea() const {
    const Page &p = *m_pages[size_t(m_page)];
    if (m_level >= 2 || p.part < 0 || m_tutorial.active())
        return {};
    return p.spec.control->playArea();
}

}

namespace tf {

std::vector<Session::LoopSummary> Session::loopSummary() const {
    std::lock_guard lock(m_lock);
    std::vector<LoopSummary> out;
    for (int page = 0; page < int(m_pages.size()); ++page) {
        const Page &p = *m_pages[size_t(page)];
        if (p.part < 0 || p.part >= m_view.partCount)
            continue;
        const PartView &v = m_view.parts[p.part];
        int loops = 0, slot = -1;
        for (int s = 0; s < kClipSlots; ++s)
            if (v.clips[s].state != ClipView::Empty) {
                ++loops;
                if (slot < 0)
                    slot = s;
            }
        if (loops == 0)
            continue;
        if (v.playing >= 0)
            slot = v.playing;
        else if (v.queued >= 0)
            slot = v.queued;
        else if (m_lastLoopSlot[p.part] >= 0 && v.clips[m_lastLoopSlot[p.part]].state != ClipView::Empty)
            slot = m_lastLoopSlot[p.part];
        LoopSummary l;
        l.page = page;
        l.loops = loops;
        l.beats = int(std::lround(v.clips[slot].lengthBeats));
        l.playing = v.playing >= 0;
        l.queued = v.playing < 0 && v.queued >= 0;
        out.push_back(l);
    }
    return out;
}

void Session::toggleLoop(int page) {
    std::lock_guard lock(m_lock);
    if (page < 0 || page >= int(m_pages.size()))
        return;
    Page &p = *m_pages[size_t(page)];
    if (p.part < 0 || p.part >= m_view.partCount)
        return;
    const PartView &v = m_view.parts[p.part];
    if (v.playing >= 0 || v.queued >= 0) {
        p.ctx->stop(false);
        p.ctx->notice(QStringLiteral("%1 stops at the bar").arg(pageName(page)));
        return;
    }
    // The loop it played last, or its first.
    int slot = m_lastLoopSlot[p.part];
    if (slot < 0 || v.clips[slot].state == ClipView::Empty) {
        slot = -1;
        for (int s = 0; s < kClipSlots && slot < 0; ++s)
            if (v.clips[s].state != ClipView::Empty)
                slot = s;
    }
    if (slot < 0)
        return;
    p.ctx->launch(slot, false);
    p.ctx->notice(QStringLiteral("%1 plays from the bar").arg(pageName(page)));
}

}

namespace tf {

bool Session::clickableAt(QPointF at) const {
    std::lock_guard lock(m_lock);
    for (const QRectF &r : {m_soundChip, m_soundNext, m_studioTag, foldedRow(m_padRect)})
        if (!r.isEmpty() && r.contains(at))
            return true;
    return console::clickableAt(at);
}

}
