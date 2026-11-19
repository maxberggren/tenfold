#include "tutorial.h"

#include <cmath>

using namespace tf;

namespace {

QRectF row(int r) { return QRectF(0, r * 0.2, 1, 0.2); }
QRectF piece(int p) { return QRectF(p / 6.0, 0, 1 / 6.0, 0.2); }

}

Tutorial::Tutorial() {
    using G = GestureView;
    m_steps = {
        {tr("Welcome"),
         tr("This builds a track with you, one idea at a time. Each step finishes by itself when you've done it. "
            "N skips ahead, B goes back, T leaves the tutorial. Press Space now to hand the trackpad to tenfold."),
         {}, {}, false,
         [](const State &s, const G &) { return s.playing; }},

        {tr("The kick"),
         tr("The bottom row is a drum kit. Tap the kick, far left, four times. How hard you tap is how loud it is."),
         piece(0), tr("kick"), false,
         [](const State &s, const G &a) { return s.gesture->hits[0] - a.hits[0] >= 4; }},

        {tr("The kit"),
         tr("Each part of the bottom row is a different drum: kick, snare, clap, hat, open hat, perc. Tap every one of them."),
         row(DrumRow), tr("the kit"), false,
         [](const State &s, const G &a) {
             for (int p = 0; p < kDrumPieces; ++p)
                 if (s.gesture->hits[p] == a.hits[p])
                     return false;
             return true;
         }},

        {tr("Your first loop"),
         tr("Listen to the click. Tap the kick on each click: one, two, three, four. On the next ONE, put a finger "
            "on the kick and keep it there. That held finger turns your taps into a loop, and the loop sets the "
            "tempo. Then lift it: the loop keeps playing on its own. Missed? Just start again."),
         piece(0), tr("tap 1 2 3 4, then hold on the next 1"), true,
         [](const State &s, const G &a) { return (s.gesture->loopRows & 1u) && s.gesture->loopsMade > a.loopsMade; }},

        {tr("Add to a loop"),
         tr("Your loop is the magenta dial. Tap it once, and on the second tap keep your finger down: you're "
            "holding the loop. While you hold it, tap the hat with another finger, between the kicks. Every tap is "
            "added to the loop. Lift the holding finger when it sounds right."),
         piece(3), tr("hold the dial, tap hats"), false,
         [](const State &s, const G &a) { return s.gesture->overdubs - a.overdubs >= 3; }},

        {tr("Claps on two and four"),
         tr("Same trick: hold the dial again (tap, then tap and hold), and tap the clap on the second and fourth beat."),
         piece(2), tr("hold the dial, clap on 2 and 4"), false,
         [](const State &s, const G &a) {
             return s.gesture->overdubs - a.overdubs >= 2 && s.gesture->hits[2] - a.hits[2] >= 2;
         }},

        {tr("A bass line"),
         tr("The second row is a bass. Every column is a note of A minor, so nothing you play is wrong. Tap a short "
            "line in time with the drums (try the far left note a few times, then one a little to the right), and "
            "on the next ONE hold a finger on the bass row: that loops it."),
         row(BassRow), tr("tap a line, hold on the next 1"), false,
         [](const State &s, const G &) { return (s.gesture->loopRows >> BassRow) & 1u; }},

        {tr("Chords"),
         tr("The middle row plays chords: one finger is a whole chord. Hold one down and slide it sideways to move "
            "to another chord. Try four chords, one per bar of the loop."),
         row(ChordRow), tr("hold and slide"), false,
         [](const State &s, const G &a) { return s.gesture->chordMoves - a.chordMoves >= 3; }},

        {tr("Keep it"),
         tr("Hold a chord and press the trackpad down until it clicks. Now lift: the chord keeps playing. It's "
            "latched. Its lettered box lets you pick it up again later (tap, then tap and hold on it)."),
         row(ChordRow), tr("hold, then press down"), false,
         [](const State &s, const G &a) { return s.gesture->freezes > a.freezes; }},

        {tr("Your first knob"),
         tr("A finger you hold also turns a knob. The first one is Filter. Hold a note on the lead row (fourth from "
            "the bottom) and slide that finger up and down: the sound opens and closes. Twist it for resonance."),
         row(LeadRow), tr("hold and slide up or down"), false,
         [](const State &s, const G &) {
             return s.gesture->focusRow == LeadRow &&
                    std::fabs(s.gesture->knobs[Filter][0] - rowDefault(LeadRow, Filter, 0)) > 0.2f;
         }},

        {tr("A melody"),
         tr("Tap a melody on the lead row. Short taps are plucks, longer holds sustain. Stay in one area and move a "
            "column or two at a time: small steps make tunes."),
         row(LeadRow), tr("tap a melody"), false,
         [](const State &s, const G &a) { return s.gesture->notes[LeadRow] - a.notes[LeadRow] >= 10; }},

        {tr("Arpeggio"),
         tr("The top row is an arpeggiator: hold one finger and it plays its chord as running notes, in time with "
            "the loops. Slide sideways to change the chord."),
         row(ArpRow), tr("hold"), false,
         [](const State &s, const G &) { return s.gesture->heldSeconds[ArpRow] >= 3.0f; }},

        {tr("The pump"),
         tr("Every kick pushes the rest of the mix down for a moment: the pump. Knobs come in the order fingers "
            "land, and the fifth is Pump. Hold five fingers anywhere above the drums, then slide the fifth one up."),
         {}, {}, false,
         [](const State &s, const G &) { return s.gesture->knobs[Pump][0] > 0.75f; }},

        {tr("Build-up"),
         tr("Hold the snare: it rolls in time. Press harder and it doubles. Hold it for a bar, then let go right on "
            "the ONE for the drop."),
         piece(1), tr("hold and press harder"), false,
         [](const State &s, const G &a) { return s.gesture->rollStrong - a.rollStrong >= 1.0f; }},

        {tr("Change key"),
         tr("Flick three fingers sideways across the pad and lift: everything moves to a new key together, loops "
            "and latched chords too. Flick back the other way to return."),
         {}, {}, false,
         [](const State &s, const G &a) { return s.gesture->swipes > a.swipes; }},

        {tr("Record it"),
         tr("Press R to record what you play to ~/Music/tenfold, and R again to stop. That's everything: drums, "
            "loops, bass, chords, knobs, melody, arpeggio, the pump, a build-up and a key change. Have fun."),
         {}, {}, false,
         [](const State &s, const G &) { return s.recording; }},
    };
}

void Tutorial::start(const GestureView &now) {
    m_active = true;
    m_finished = false;
    enter(0, now);
}

void Tutorial::enter(int index, const GestureView &now) {
    m_index = std::clamp(index, 0, count() - 1);
    m_atStart = now;
    m_doneTimer = 0;
}

void Tutorial::next(const GestureView &now) {
    if (m_index + 1 >= count()) {
        m_finished = true;
        m_active = false;
        return;
    }
    enter(m_index + 1, now);
}

void Tutorial::back(const GestureView &now) { enter(m_index - 1, now); }

bool Tutorial::update(const State &now, double dt) {
    if (!m_active)
        return false;
    if (m_doneTimer > 0) {
        // A beat to see "done" before the next step appears.
        m_doneTimer -= dt;
        if (m_doneTimer <= 0) {
            next(*now.gesture);
            return true;
        }
        return false;
    }
    if (step().done(now, m_atStart)) {
        m_doneTimer = 1.4;
        return true;
    }
    return false;
}
