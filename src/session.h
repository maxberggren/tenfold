#pragma once

// The control side of the studio: the pages (one per part, then the arrange
// page), which of them is showing, and the one lock under which every
// page's controls run, whether a touch frame, a key or the screen calls.
//
// Global keys, whatever page is showing:
//   1–8 (and the keypad's)  the parts' pages     0  the arrange page
//   Shift+1                 the Sampler, the ninth part
//   9                       the visualizer
//   Tab / Shift+Tab         next / previous page
//   Ctrl+↑ / Ctrl+↓         tempo ±1 BPM (with Shift ±5)
//   Ctrl+← / Ctrl+→         key down / up a fifth
//   Ctrl+[ / Ctrl+]         mode darker / brighter
//   Ctrl+M                  metronome
//   Ctrl+G                  quantise, on to begin with: Space and overdubs
//                           tighten hits to the sixteenths (off: loops keep
//                           the playing as it was heard)
//   Ctrl+P                  pause every loop at once; again, play them on from a downbeat
//   Enter                   play and pause every loop (as Ctrl+P); Shift+Enter
//                           is what Return was on the page (hold, latch, drop),
//                           F10 overdub
//   Ctrl+Backspace          stop every clip at the next bar
//   Ctrl+Z                  take back this page's newest loop (on the Song
//                           page, the newest anywhere)
//   Ctrl+N, Ctrl+N          start over: every loop cleared
//   Ctrl+T                  the tutorial, on or off; while it runs Ctrl+Return
//                           goes to its next step, Ctrl+Shift+Return back
//   Ctrl+R                  (scripts only: the window keeps it) marks the mix as
//                           recording, as the window's recorder would
//   Space                   loop what was just played; over this page's own
//                           loop it layers on top, as one loop. Space again
//                           while a take waits calls it off
//   Shift+Space             a new loop, in place of this page's playing loop
//                           (the Vocal page takes Space itself and the other
//                           way round: Space a new loop, Shift+Space a layer)
//   Escape                  escape(): a menu, the tutorial, then the pad
//                           back; with none of those, pause and play
// ?, Ctrl+R, Ctrl+S, Ctrl+O and Ctrl+Q are the window's (help, record the
// mix, save and open songs, quit). The pad is taken with the window's Take
// the trackpad button.

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

#include "part.h"
#include "studio.h"
#include "tutorial.h"

namespace tf {

class FilePicker;

namespace visual {
class VisualPage;
}

class Session : public FrameSink {
public:
    // Builds every part (see parts.cpp) into the studio.
    explicit Session(Studio *studio);
    ~Session() override;

    Studio *studio() const { return m_studio; }
    int pageCount() const { return int(m_pages.size()); }
    int partCount() const { return m_partCount; }
    // After the parts' pages: the arrange page (key 0), then the visualizer
    // (key 9), which has no part either.
    int arrangePage() const { return m_partCount; }
    int visualPage() const { return m_partCount + 1; }
    // The visualizer's page, for the window to read under lock().
    visual::VisualPage *visual() const { return m_visual; }
    int page() const { return m_page; }
    void setPage(int page);

    // The key that shows a page, as a tab says it: "1"…"8" for the first
    // eight parts, "⇧1" for the ninth (the digits ran out: Shift+1), "0"
    // for the arrange page, "9" for the visualizer. And the page a name or
    // such a key names ("sampler", "Shift+1", "4"), for scripts; −1 when
    // there is none.
    QString pageKey(int page) const;
    int pageNamed(const QString &name) const;
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
    // The mouse over the page in item pixels (for lighting controls drawn
    // around the pad), and a click outside the pad: true if the page used it.
    void mouseAt(QPointF at, bool over);
    // Where the pad is drawn in an item of this size: all of it, but for a
    // band under it for the page's controls and clip slots, a margin over it
    // for its legend and one at each side. The pad takes the window's shape
    // rather than the trackpad's: a finger is placed by how far across and
    // up the trackpad it is, whatever the two shapes.
    static QRectF padRect(QSizeF item);
    static constexpr double kBandHeight = 96; // under the pad: two rows of controls (console::bandRow)
    bool clickAt(QPointF at);
    // A file dragged onto the window or text pasted into it (a path, a
    // link): the showing page's if it takes such things, else the first
    // page that does, which is then shown. False if no page wanted it.
    bool drop(const QString &what);
    // Where a page's file dialog comes from (OPEN FILE on the Sampler).
    void setFilePicker(FilePicker *picker) { m_picker = picker; }
    // A right click (the page's menu, if it has one), a left click while
    // that menu is open, and Esc closing it. True if the page used it.
    bool rightClickAt(QPointF at);
    bool menuClickAt(QPointF at);
    bool closeMenu();
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
    void toggleQuantise();
    void stopAll();
    // Pause and play (Ctrl+P, the window's button): every loop stops at
    // once, remembered; play brings the same loops back, together, from the
    // top of a bar (at once when nothing else plays). paused(): something
    // is remembered and nothing plays.
    void togglePause();
    bool paused() const;
    // Esc: the first of these that applies. It closes the page's open menu,
    // leaves the tutorial, gives the trackpad back (padTaken: the window has
    // it, or is taking it; the caller lets go of it on Pad), and otherwise
    // pauses or plays, as Ctrl+P. So with the pad taken, Esc twice gives it
    // back and pauses; with it not taken, Esc is pause and play.
    enum class Escaped { Menu, Tutorial, Pad, Pause };
    Escaped escape(bool padTaken);
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
    // The window's New loop button: the showing page's loop stops at once,
    // kept in its slot, so what is played next and looped with Space is a
    // loop of its own rather than a layer on it. False when the page has
    // no loop playing (or is no part's).
    bool newLoop();
    bool canNewLoop() const;
    void startOver();
    // Where starting over (Ctrl+N twice, the tutorial) keeps the song it
    // clears, so Ctrl+Shift+O brings it back; empty: nowhere.
    void setStartOverBackup(const QString &path) { m_startOverBackup = path; }
    QString startOverBackup() const { return m_startOverBackup; }
    // Counts the times the player started a new song (Ctrl+N twice, the
    // tutorial): a song saved before one is not where the next save goes.
    uint32_t freshStarts() const { return m_freshStarts; }
    // How many times Ctrl+Z took a loop back, for the tutorial.
    int undos() const { return m_undos; }
    // Whether the window records the mix to a file (the window says so; a
    // script's Ctrl+R toggles it).
    bool mixRecording() const { return m_mixRecording; }
    void setMixRecording(bool on) { m_mixRecording = on; }

    // Song files (songfile.h). saveSong: the whole song as a song file's
    // JSON, or an empty object and why not. openSong: a song file's JSON in
    // place of everything playing now; every part of it is checked before
    // anything changes, and a song that can't be opened leaves the studio
    // as it was, with a sentence saying why.
    QJsonObject saveSong(QString *error = nullptr);
    bool openSong(const QJsonObject &doc, QString *error = nullptr);
    // Whether there is anything to keep: a clip in any slot.
    bool anythingMade() const;
    // The Song page's song, for an export: its bars (0: no song), and
    // played once from its top at a bar of the studio's clock.
    int songBars() const;
    void playSong(int bar);
    // A notice in the window from outside the pages (a file saved, opened).
    void say(const QString &text);

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

    // The sound browser: the showing page's sounds, the one chosen, and
    // choosing one; hear: a short phrase plays at once, from the page's
    // own keys, so the sound is heard before it is kept.
    std::vector<PartControl::SoundChoice> soundChoices() const;
    int soundChoice() const;
    void chooseSound(int index, bool hear = true);
    bool auditioning() const;

    // The LOOP button: what Space would do on the showing page now.
    //   None     a page that makes no loops (the Song page, the picture)
    //   Empty    nothing played lately: play first
    //   Make     something was played: Space loops it
    //   Layer    this part's loop plays: Space layers on it (Shift: new loop)
    //   Waiting  a loop waits for the bar line: Space calls it off
    enum class LoopState { None, Empty, Make, Layer, Waiting };
    LoopState loopState() const;
    void pressLoop(bool fresh = false); // as Space (fresh: Shift+Space)

    // Styles: a sound on every page and a tempo, chosen at once.
    struct Style {
        const char *name, *what;
        int bpm;
        const char *sounds[7]; // drums, bass, chords, lead, arp, atmos, fx
    };
    static const std::vector<Style> &styles();
    bool applyStyle(int index);
    // A style chosen and heard: its drums, bass and chords play a bar.
    void previewStyle(int index);
    // The page a newcomer is pointed to next (its tab pulses), or −1.
    int suggestedPage() const;
    // The SOUND chip under the pad was clicked (level 0): the window opens
    // the browser. True once per click.
    bool takeSoundsAsked();

    // One sentence for a newcomer: what to do next on the showing page.
    QString nextStep() const;

    // How much the window shows: 0 Play (the pad, LOOP and the sound; the
    // band's second row folded away), 1 Shape (that row too), 2 Studio
    // (everything). The keys work at every level.
    int level() const { return m_level; }
    void setLevel(int level) { m_level = std::clamp(level, 0, 2); }
    // The band's second row, where level 0 folds it (empty: not folded here).
    QRectF foldedRow(const QRectF &pad) const;

    struct Page;
    class Context;

private:
    double now() const;
    Context &context(int page);
    // The rest of key(), once Enter and F10 are translated for the page.
    bool keyForPage(const KeyEvent &k);

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
    // The same, only while a hand was on the part's page (a finger down, a
    // key held): what bridges a pause in the playing. A bed latched and left
    // would bridge them all.
    uint8_t m_handSounding[kMaxParts][kSoundBins] = {};
    // Keys held down now (a note played from the keyboard is a hand on the
    // page as much as a finger is).
    std::vector<int> m_keysDown;
    long m_soundBin[kMaxParts] = {};
    // handOnly: count only sound made while a hand was on the page.
    bool soundedThrough(int part, double t0, double t1, double maxGap, bool handOnly = false) const;
    double lastSounding(int part, double since) const;
    uint32_t m_takesSeen[kMaxParts] = {};
    // The beat each part's loop last changed (a take landed, another slot
    // started): a layer takes only what was played since.
    double m_loopSince[kMaxParts] = {};
    int m_lastPlaying[kMaxParts] = {};
    double m_startOverAsked = -1, m_tutorialAsked = -1;
    std::atomic<uint32_t> m_freshStarts{0};
    FilePicker *m_picker = nullptr;
    std::mutex m_pickedLock; // the dialog's answer may come on any thread
    std::vector<std::pair<int, QString>> m_picked; // (part, path) waiting for idle()
    void pickSound(int part, const QString &title);
    void deliverPicked();
    // Every loop, the mix home, each page's song state. Opening a song
    // leaves the studio's part to its own message (studio = false).
    void clearEverything(bool studio = true);
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
        double base = 0; // a layer: the length of the loop it went onto, in beats
    } m_lastTake;
    // The lengths a layer may have over a loop `base` beats long: the ones
    // that go into it a whole number of times, in whole bars (whole beats
    // for a loop under a bar), shortest first. A layer that didn't would
    // drift against the loop and be cut off where the loop wraps.
    static std::vector<double> layerLengths(double base);
    int freeRounds(int dir) const; // the rounds one step that way, or 0
    static constexpr double kRetakeSecs = 3.0;
    bool adjustTake(int dir); // ↑ (+1) longer, ↓ (−1) shorter; false when there's nothing to adjust
    double retakeLength(int dir) const; // the length one step that way, or 0
    bool m_mixRecording = false;
    Tutorial m_tutorial;
    Frame m_last;            // the last frame, as the pad sent it
    QPointF m_mousePx;
    bool m_mouseOver = false;
    std::vector<int> m_ignored; // fingers down when the page changed, until they lift
    // Just after Space makes a loop, the hand steps aside: fingers down
    // then, and any that land before this time, are ignored until they
    // lift, so the loop takes over without the hand doubling it.
    double m_quietUntil = -1;
    static constexpr double kHandOverSecs = 0.18; // a tap already on its way, not the next one played
    void handOver();
    QString m_notice;
    // What a pause (or Ctrl+Backspace) stopped: each part's slot, or −1.
    int m_pausedSlots[kMaxParts];
    bool m_songPaused = false;
    double m_pausedAt = -1e9; // when the last pause came, for its notice
    QString m_startOverBackup;
    // The sound browser's phrase: keys going down and up, by the clock.
    struct Audition {
        double t;
        int page, key;
        bool down;
    };
    std::vector<Audition> m_audition;
    int m_level = 2;
    QRectF m_soundChip;   // level 0: the SOUND chip under the pad, where last painted
    bool m_soundsAsked = false;
    QRectF m_soundNext;   // level 0: the → beside it
    std::vector<int> m_liveIds; // touches in the last frame delivered, to tell a landing
    int partsWithLoops() const;
    QString howToPlay(int page) const; // a sentence on how the page is played
    QRectF m_padRect; // where the pad was last painted, for foldedRow()
    void playAudition(double t);
    void stopAudition();
    void forgetPlaying(int page);
    // Writes the song about to be cleared to m_startOverBackup; true if
    // there was something and it was kept.
    bool keepBeforeClearing();
    // The metronome, left on across a pause, is silent until play: the
    // studio keeps its count, so it comes back on the one.
    bool m_metronomeHeld = false;
    void holdMetronome(bool hold);
    uint32_t m_noticeSeq = 0;
    double (*m_clock)() = nullptr;
    visual::VisualPage *m_visual = nullptr;
};

}
