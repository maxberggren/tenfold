#pragma once

#include <QCoreApplication>
#include <QRectF>
#include <QString>
#include <functional>
#include <vector>

#include "engine.h"
#include "gesture.h"

// A guided walk from one tapped kick to a whole track. Each step says what to
// do and where, highlights that part of the pad, and finishes itself when the
// player has done it, judged from what the gesture layer and engine report.
class Tutorial {
    Q_DECLARE_TR_FUNCTIONS(Tutorial)

public:
    struct State {
        bool playing = false, recording = false;
        const tf::GestureView *gesture = nullptr;
        const tf::EngineView *engine = nullptr;
    };

    struct Step {
        QString title, text;
        QRectF highlight; // in pad coordinates, x right and y up, 0..1; empty for none
        QString highlightLabel;
        bool metronome = false;
        std::function<bool(const State &now, const tf::GestureView &atStart)> done;
    };

    Tutorial();

    bool active() const { return m_active; }
    int index() const { return m_index; }
    int count() const { return int(m_steps.size()); }
    const Step &step() const { return m_steps[size_t(m_index)]; }
    bool finished() const { return m_finished; }
    // True for a moment after a step is completed, before moving on.
    bool justDone() const { return m_doneTimer > 0; }

    void start(const tf::GestureView &now);
    void stop() { m_active = false; }
    void next(const tf::GestureView &now);
    void back(const tf::GestureView &now);
    // Called every screen frame; returns true when something changed.
    bool update(const State &now, double dt);

private:
    void enter(int index, const tf::GestureView &now);

    std::vector<Step> m_steps;
    bool m_active = false, m_finished = false;
    int m_index = 0;
    tf::GestureView m_atStart;
    double m_doneTimer = 0;
};
