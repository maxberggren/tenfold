#pragma once

// The tutorial: a walk from silence to a finished, recorded track, one page
// at a time, each feature met at the moment the song needs it.
//
// Every step says what to do and why, names the page it happens on, lights
// the places on the pad where the fingers go (or the keys to press), and
// finishes by itself once the player has done it, judged from the studio
// as it really is: the clips playing, the channels, and the pages' own
// tutorialProbe() numbers. A step done celebrates for a moment and the next
// one begins. Back and Next (or skip) move by hand; a step returned to with
// Back that is already done waits for Next rather than running on.
//
// The session owns the tutorial and drives it: update() with every screen
// frame, paint() over the showing page's pad. Everything here runs on the UI
// thread, under the session's lock.

#include <QRectF>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

#include "part.h"

class QPainter;

namespace tf {

class Session;

class Tutorial {
public:
    // What a step's checks can ask about the studio and the pages.
    class Look;
    using Test = std::function<bool(Look &)>;

    // A place on the pad, in pad coordinates (0..1, y up), with a label and
    // which way the finger should slide there, if it should.
    struct Spot {
        float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
        QString label;
        enum Arrow : uint8_t { None, Up, Down, Left, Right } arrow = None;
    };
    struct Check {
        QString label;
        Test test;
    };
    struct Step {
        QString chapter;   // "Beat", "Chords"…: the part of the song this belongs to
        int page = 0;      // the session's page index where it happens
        QString title;     // a few words: what to do
        QString text;      // what to do and why, in plain words
        QString tip;       // one more thing to try, for the curious
        QString cheer;     // said when it's done
        QStringList keys;  // keys it uses, as the window's buttons write them: "Space", "Ctrl Z"
        QString button;    // a window button to point at: "undo", "record", "tutorial"
        std::vector<Spot> spots;
        std::vector<Check> checks; // all of them, and the step is done
    };
    enum State { Working, Done, Reviewed };

    Tutorial();
    ~Tutorial();

    bool active() const { return m_active; }
    int index() const { return m_index; }
    int count() const { return int(m_steps.size()); }
    const Step &step() const { return m_steps[size_t(m_index)]; }
    const Step &step(int i) const { return m_steps[size_t(i)]; }
    State state() const { return m_state; }
    bool checked(int i) const { return i >= 0 && i < int(m_checked.size()) && m_checked[size_t(i)]; }
    // The chapters in order, and which one the step is in.
    QStringList chapters() const;
    int chapterIndex() const;
    // How many times the tutorial reached its end.
    int finishes() const { return m_finishes; }

    void start(Session &s);
    void stop();
    void next(Session &s);
    void back(Session &s);
    void jump(int index, Session &s);

    // Every screen frame: tick the checks, celebrate, move on.
    void update(Session &s);
    // Over the pad of the page that is showing: the step's spots, or which
    // page to go to.
    void paint(QPainter &p, const PaintContext &pc, int page, const QString &pageName) const;

    // The probes the steps look at, snapshot at each step's start, so a
    // check can ask what changed since (part page, name).
    struct Probe {
        int page;
        const char *name;
    };

private:
    void enter(int index, Session &s, bool backward);
    void build();

    std::vector<Step> m_steps;
    bool m_active = false;
    int m_index = 0;
    State m_state = Working;
    std::vector<bool> m_checked;
    double m_doneAt = 0;
    int m_finishes = 0;
    std::unique_ptr<Look> m_look;
};

}
