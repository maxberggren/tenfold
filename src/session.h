#pragma once

// The control side of the studio: the pages (one per part, then the arrange
// page), which of them is showing, and the one lock under which every
// page's controls run, whether a touch frame, a key or the screen calls.
//
// Global keys, whatever page is showing:
//   1–7 (and the keypad's)  the parts' pages     0  the arrange page
//   Tab / Shift+Tab         next / previous page
//   Ctrl+↑ / Ctrl+↓         tempo ±1 BPM (with Shift ±5)
//   Ctrl+← / Ctrl+→         key down / up a fifth
//   Ctrl+[ / Ctrl+]         mode darker / brighter
//   Ctrl+M                  metronome
//   Ctrl+Backspace          stop every clip at the next bar
//   Ctrl+Z                  take back the newest loop (on any page)
//   Ctrl+N, Ctrl+N          start over: every loop cleared
//   Space                   record a loop on this page from the next bar; Space
//                           again closes it (unless the page records its own way)
// Escape, ?, Ctrl+R and Ctrl+Q are the window's (give the pad back, help,
// record the mix, quit). The pad is taken with the window's Play button.

#include <QString>
#include <memory>
#include <mutex>
#include <vector>

#include "part.h"
#include "studio.h"

namespace tf {

class Session : public FrameSink {
public:
    // Builds every part (see parts.cpp) into the studio.
    explicit Session(Studio *studio);
    ~Session() override;

    Studio *studio() const { return m_studio; }
    int pageCount() const { return int(m_pages.size()); }
    int partCount() const { return m_partCount; }
    // The arrange page is the last page.
    int arrangePage() const { return pageCount() - 1; }
    int page() const { return m_page; }
    void setPage(int page);

    QString pageName(int page) const;
    QString pageBlurb(int page) const;
    QColor pageColor(int page) const;
    QString pageHelp(int page) const;
    QString pageStatus() const;

    // FrameSink: touch thread.
    void frame(const Frame &f) override;
    // The mouse over the pad, in pad coordinates (UI thread): held down it
    // is one more finger (kMouseId); hovering, pages can show what it is on.
    void pointer(float x, float y, bool down, bool inside);
    void idle(double t) override;

    // UI thread. Returns true when the key was used.
    bool key(const KeyEvent &k);

    // UI thread: take the newest studio view; then paint the showing page.
    void refresh();
    const StudioView &view() const { return m_view; }
    void paint(QPainter &p, const QRectF &pad, double time);

    void setAspect(float a) override { m_aspect = a; }
    // The notice the last page or key left, with a counter that changes.
    QString notice(uint32_t *seq) const;

    // Global actions, also used by the window and scripts.
    void shiftTempo(double delta);
    void shiftRoot(int fifths);
    void shiftMode(int step);
    void toggleMetronome();
    void stopAll();
    // Ctrl+Z: take back the newest loop on any page; what played before
    // plays again. Ctrl+N: start over, every loop cleared (asks first).
    void undoTake();
    void startOver();
    // Space: arm a loop on this part from the next bar, or close it.
    void toggleRecording(ControlContext &ctx, bool layer = false);

    // The clock the touch frames use (the same one live, script time
    // offline), so a page's ctx.now() is right between frames.
    void setClock(double (*clock)()) { m_clock = clock; }
    double clock() const { return now(); }

    struct Page;
    class Context;

private:
    double now() const;
    Context &context(int page);

    Studio *m_studio;
    std::vector<std::unique_ptr<Page>> m_pages;
    int m_partCount = 0;
    int m_page = 0;
    mutable std::recursive_mutex m_lock;
    StudioView m_view;
    float m_aspect = 1.6f;
    double m_lastFrame = 0;
    bool m_mouseDown = false, m_mouseIn = false;
    float m_mouseX = 0, m_mouseY = 0;
    void deliver(const Frame &f);
    Frame withMouse(Frame f) const;
    std::vector<int> m_takeOrder;          // the part of each take that landed, oldest first
    // Whether each part was sounding with no clip of its own playing, in
    // quarter-second bins over the last minute: a riser building for bars,
    // a latched arp, a ringing pad isn't a pause in the playing.
    static constexpr int kSoundBins = 256;
    uint8_t m_sounding[kMaxParts][kSoundBins] = {};
    long m_soundBin[kMaxParts] = {};
    bool soundedThrough(int part, double t0, double t1, double maxGap) const;
    double lastSounding(int part, double since) const;
    uint32_t m_takesSeen[kMaxParts] = {};
    double m_startOverAsked = -1;
    Frame m_last;            // the last frame, as the pad sent it
    std::vector<int> m_ignored; // fingers down when the page changed, until they lift
    QString m_notice;
    uint32_t m_noticeSeq = 0;
    double (*m_clock)() = nullptr;
};

}
