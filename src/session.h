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
//   Ctrl+T                  the tutorial, on or off; while it runs Ctrl+Return
//                           goes to its next step, Ctrl+Shift+Return back
//   Ctrl+R                  (scripts only: the window keeps it) marks the mix as
//                           recording, as the window's recorder would
//   Space                   loop what was just played; over this page's own
//                           loop it layers on top, as one loop. Space again
//                           while a take waits calls it off
//   Shift+Space             a new loop, in place of this page's playing loop
// Escape, ?, Ctrl+R and Ctrl+Q are the window's (give the pad back, help,
// record the mix, quit). The pad is taken with the window's Play button.

#include <QString>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

#include "part.h"
#include "studio.h"
#include "tutorial.h"

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
    // Debugging: writes what Space decided and every hit the parts hear to
    // this file (truncated), drained from the studio as the session idles.
    void setDebugLog(const QString &path);
    QString pageStatus(int page) const;
    // A page's tutorialProbe() (NaN when it doesn't know the name).
    float probe(int page, const QString &name) const;

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
    void redoTake(); // Ctrl+Shift+Z: what Ctrl+Z took back comes back
    // The loop Space last made on this page, while it can still be made
    // longer or shorter (until another loop of that part, Ctrl+Z or Ctrl+N):
    // its beats now and one step each way (0 where there is none, or when
    // nothing can be changed). The window's buttons use these.
    int loopBeats(int dir) const;
    void resizeLoop(int dir); // the buttons: as ↑ / ↓ just after Space, at any time
    void startOver();
    // How many times Ctrl+Z took a loop back, for the tutorial.
    int undos() const { return m_undos; }
    // Whether the window records the mix to a file (the window says so; a
    // script's Ctrl+R toggles it).
    bool mixRecording() const { return m_mixRecording; }
    void setMixRecording(bool on) { m_mixRecording = on; }

    // The walk from silence to a track. The session drives it: every
    // refresh() ticks it, paint() draws its spots over the pad.
    Tutorial &tutorial() { return m_tutorial; }
    const Tutorial &tutorial() const { return m_tutorial; }
    void toggleTutorial();
    void tutorialNext();
    void tutorialBack();
    // Locks the session for a caller that reads the tutorial from another
    // thread than the UI's.
    std::recursive_mutex &lock() const { return m_lock; }
    // Space: loop what was just played, layered onto the part's playing loop
    // unless layer is false (Shift+Space).
    void toggleRecording(ControlContext &ctx, bool layer = true);

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
    std::FILE *m_debugLog = nullptr;
    void drainDebug();
    void dlog(const char *fmt, ...);
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
    // The beat each part's loop last changed (a take landed, another slot
    // started): a layer takes only what was played since.
    double m_loopSince[kMaxParts] = {};
    int m_lastPlaying[kMaxParts] = {};
    double m_startOverAsked = -1, m_tutorialAsked = -1;
    void clearEverything(); // every loop, the mix home, each page's song state
    int m_undos = 0;
    std::vector<int> m_redoOrder; // parts, as Ctrl+Z took their steps back
    // The take Space just made, for ↑↓ to make it longer or shorter while
    // the player listens; it locks in once the arrows rest.
    struct LastTake {
        int part = -1, slot = -1, into = -1;
        double end = 0, length = 0, since = -1e18, at = -1e9;
        uint32_t takes = 0; // the part's take count before it; a later take ends its time to change
        // A loop that set the tempo: `n` rounds of a pattern `unit` seconds
        // long, ending at `end` (seconds), instead of beats on the clock.
        bool free = false;
        double unit = 0;
        int n = 0;
        std::vector<double> rounds; // when each round of the pattern began
    } m_lastTake;
    int freeRounds(int dir) const; // the rounds one step that way, or 0
    static constexpr double kRetakeSecs = 3.0;
    bool adjustTake(int dir); // ↑ (+1) longer, ↓ (−1) shorter; false when there's nothing to adjust
    double retakeLength(int dir) const; // the length one step that way, or 0
    bool m_mixRecording = false;
    Tutorial m_tutorial;
    Frame m_last;            // the last frame, as the pad sent it
    std::vector<int> m_ignored; // fingers down when the page changed, until they lift
    QString m_notice;
    uint32_t m_noticeSeq = 0;
    double (*m_clock)() = nullptr;
};

}
