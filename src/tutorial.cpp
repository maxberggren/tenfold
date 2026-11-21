#include "tutorial.h"

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "console.h"
#include "session.h"

namespace tf {

namespace {

// The pages, as the session counts them (the number keys are one more,
// arrange is 0).
enum Page { Drums = 0, Bass, Chords, Lead, Arp, Atmos, FX, Arrange };

// The probes the steps compare against their start.
const Tutorial::Probe kProbes[] = {
    {Drums, "hits"},     {Drums, "rolls"},      {Drums, "grooves"}, {Drums, "keeps"},
    {Chords, "chords"},  {Lead, "glides"},      {Lead, "wiggles"},  {Lead, "harmonized"},
    {Atmos, "swells"},
};
constexpr int kProbeCount = int(sizeof(kProbes) / sizeof(kProbes[0]));

// How long a step's success is shown before the next one begins.
constexpr double kCheer = 1.6;

// The chords of the progression the tutorial builds the song on, as columns
// of the chords page in the default key of A minor: i VI III VII.
constexpr int kAm = 0, kF = 5, kC = 2, kG = 6;

Tutorial::Spot spot(float x0, float y0, float x1, float y1, const QString &label,
                    Tutorial::Spot::Arrow arrow = Tutorial::Spot::None) {
    Tutorial::Spot s;
    s.x0 = x0;
    s.y0 = y0;
    s.x1 = x1;
    s.y1 = y1;
    s.label = label;
    s.arrow = arrow;
    return s;
}

// A column of the chords page (seven in the key), over the triad and add9 rows.
Tutorial::Spot chordColumn(int degree, const QString &label) {
    return spot(float(degree) / 7.0f, 0.32f, float(degree + 1) / 7.0f, 0.64f, label);
}

}

class Tutorial::Look {
public:
    Session *s = nullptr;
    double now = 0, dt = 0;
    StudioView base;              // the studio when the step began
    float baseProbe[kProbeCount] = {};
    int baseUndos = 0;
    double acc[4] = {};
    bool flags[8] = {};
    const std::vector<bool> *checks = nullptr;

    const StudioView &view() const { return s->view(); }
    float probe(int page, const char *name) const {
        const float v = s->probe(page, QString::fromLatin1(name));
        return std::isnan(v) ? 0.0f : v;
    }
    // How much a counting probe went up since the step began.
    float since(int page, const char *name) const {
        for (int i = 0; i < kProbeCount; ++i)
            if (kProbes[i].page == page && std::strcmp(kProbes[i].name, name) == 0)
                return probe(page, name) - baseProbe[i];
        return 0;
    }
    bool has(int page, const char *name, uint32_t bits) const {
        return (uint32_t(probe(page, name)) & bits) != 0;
    }
    bool playing(int part) const { return view().parts[part].playing >= 0; }
    int takesSince(int part) const { return int(view().parts[part].takes - base.parts[part].takes); }
    float data(int part, int i) const { return view().parts[part].data[i]; }
    float dataAtStart(int part, int i) const { return base.parts[part].data[i]; }
    bool muted(int part) const { return view().parts[part].channel.value[Mute] > 0.5f; }
    QString status(int page) const { return s->pageStatus(page); }
    int undosSince() const { return s->undos() - baseUndos; }
    bool recording() const { return s->mixRecording(); }
    // True once cond has held for secs on end (accumulator i).
    bool lasted(int i, double secs, bool cond) {
        acc[i] = cond ? acc[i] + dt : 0;
        return acc[i] >= secs;
    }
    bool &flag(int i) { return flags[i]; }
    bool checked(int i) const { return i < int(checks->size()) && (*checks)[size_t(i)]; }
    // The song the arrange page plays, from its status: "▶ song 3/74".
    int songBars() const {
        static const QRegularExpression re(QStringLiteral("song \\d+/(\\d+)"));
        const auto m = re.match(status(Arrange));
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    }
};

Tutorial::Tutorial() : m_look(std::make_unique<Look>()) { build(); }
Tutorial::~Tutorial() = default;

void Tutorial::build() {
    using A = Spot::Arrow;
    auto add = [&](Step s) { m_steps.push_back(std::move(s)); };

    // --- The beat: drums ----------------------------------------------------
    // Rows of the drum pads, from the near edge: kick, snare, clap, hat
    // first. The REPEAT strip lies along the very bottom (y < 0.05), so the
    // spots start just above it.
    const Spot kick = spot(0.0f, 0.06f, 0.25f, 0.25f, QStringLiteral("KICK"));
    const Spot clap = spot(0.5f, 0.06f, 0.75f, 0.25f, QStringLiteral("CLAP"));
    const Spot hat = spot(0.75f, 0.06f, 1.0f, 0.25f, QStringLiteral("HAT"));

    add({QStringLiteral("Beat"), Drums, QStringLiteral("Tap the kick"),
         QStringLiteral("Every track here starts from a kick. Tap the bottom-left pad, KICK: the harder you press, "
                        "the louder it hits. Take pad hands the whole trackpad to tenfold; the mouse plays too."),
         QStringLiteral("Each pad shows its drum and the keys that play it: Z is the kick."),
         QStringLiteral("Boom. That's your first sound."),
         {},
         QStringLiteral("pad"),
         {kick},
         {{QStringLiteral("Kick"), [](Look &l) { return l.has(Drums, "pads", 1u); }}}});

    add({QStringLiteral("Beat"), Drums, QStringLiteral("Play a beat by hand"),
         QStringLiteral("Kick on every count, CLAP on two and four, HAT in between: boom, tss, CLAP, tss. "
                        "It doesn't need to be perfect; you're getting a feel for the pads."),
         QStringLiteral("Z X C V play this row from the keyboard; Shift accents."),
         QStringLiteral("That's a beat."),
         {},
         {},
         {spot(0.0f, 0.06f, 0.25f, 0.25f, QStringLiteral("1 KICK")), spot(0.5f, 0.06f, 0.75f, 0.25f, QStringLiteral("2 CLAP")),
          spot(0.75f, 0.06f, 1.0f, 0.25f, QStringLiteral("3 HAT"))},
         {{QStringLiteral("Kick"), [](Look &l) { return l.has(Drums, "pads", 1u); }},
          {QStringLiteral("Clap or snare"), [](Look &l) { return l.has(Drums, "pads", 2u | 4u); }},
          {QStringLiteral("Hat"), [](Look &l) { return l.has(Drums, "pads", 8u); }},
          {QStringLiteral("A bar of it: eight hits"), [](Look &l) { return l.since(Drums, "hits") >= 8; }}}});

    add({QStringLiteral("Beat"), Drums, QStringLiteral("Hold for a roll"),
         QStringLiteral("Hold a finger on HAT and keep it there: it rolls, locked to the tempo. Slide up while "
                        "you hold and it rolls faster, from eighths to thirty-seconds: that's how fills and "
                        "build-ups are played."),
         QStringLiteral("Sideways bends the drum's pitch; a twist opens it."),
         QStringLiteral("Rolling. Fills are yours now."),
         {},
         {},
         {spot(0.75f, 0.06f, 1.0f, 0.5f, QStringLiteral("HOLD · SLIDE UP"), A::Up)},
         {{QStringLiteral("A roll, held for a second"),
           [](Look &l) { return l.lasted(0, 1.0, l.probe(Drums, "rolling") >= 1); }}}});

    add({QStringLiteral("Beat"), Drums, QStringLiteral("Three fingers: a groove"),
         QStringLiteral("Land three fingers together on KICK, CLAP and HAT and leave them down. Each drum plays "
                        "its part of a house groove, in time. Slide one finger up for a busier part."),
         QStringLiteral("Left and right arrows change the kit: house, techno, trap, breaks, garage."),
         QStringLiteral("Now it grooves by itself."),
         {},
         {},
         {spot(0.0f, 0.06f, 0.25f, 0.25f, QStringLiteral("KICK")), spot(0.5f, 0.06f, 0.75f, 0.25f, QStringLiteral("CLAP")),
          spot(0.75f, 0.06f, 1.0f, 0.25f, QStringLiteral("HAT"))},
         {{QStringLiteral("Fingers grooving together"),
           [](Look &l) { return l.probe(Drums, "grooving") >= 2 || l.since(Drums, "grooves") >= 2 || l.playing(Drums); }}}});

    add({QStringLiteral("Beat"), Drums, QStringLiteral("Click to keep it"),
         QStringLiteral("While the groove plays under your fingers, press the pad down until it clicks. At the next "
                        "bar line the groove becomes a loop and plays on by itself: lift your fingers and listen. "
                        "Your loops are what the song is built from."),
         QStringLiteral("F1 relaunches slot 1 and Backspace stops it. Ctrl+↑ and Ctrl+↓ change the tempo of everything."),
         QStringLiteral("Your first loop! It plays until you stop it."),
         {QStringLiteral("click")},
         {},
         {spot(0.0f, 0.06f, 1.0f, 0.25f, QStringLiteral("HOLD THE GROOVE · CLICK"))},
         {{QStringLiteral("A drum loop plays"), [](Look &l) { return l.playing(Drums); }}}});

    add({QStringLiteral("Beat"), Drums, QStringLiteral("Layer on top: Shift+Space"),
         QStringLiteral("Over the loop, play something extra: OPEN hat on the off-beats, or a few SHAKER hits. "
                        "Then Shift+Space: what you just played joins the loop, as one loop."),
         QStringLiteral("Plain Space would make it a loop of its own instead."),
         QStringLiteral("Thicker. Loops grow by layers."),
         {QStringLiteral("Shift Space")},
         {},
         {spot(0.5f, 0.25f, 0.75f, 0.5f, QStringLiteral("SHAKER")), spot(0.75f, 0.25f, 1.0f, 0.5f, QStringLiteral("OPEN"))},
         {{QStringLiteral("Layered into the drum loop"),
           [](Look &l) { return l.takesSince(Drums) >= 1 && l.playing(Drums); }}}});

    // --- Chords: the progression everything else follows --------------------
    add({QStringLiteral("Chords"), Chords, QStringLiteral("Four chords"),
         QStringLiteral("Each column is a chord of the key, A minor. Play Am, F, C, G, one per bar, holding each until "
                        "the next lands: the song's progression. Bass, lead, arp and atmosphere will all follow it."),
         QStringLiteral("Higher in a column is a richer colour: power, sus, triad, add9, seventh. Slide while it sounds."),
         QStringLiteral("That's the heart of the song."),
         {},
         {},
         {chordColumn(kAm, QStringLiteral("1 Am")), chordColumn(kF, QStringLiteral("2 F")),
          chordColumn(kC, QStringLiteral("3 C")), chordColumn(kG, QStringLiteral("4 G"))},
         {{QStringLiteral("Am"), [](Look &l) { return l.has(Chords, "columns", 1u << kAm); }},
          {QStringLiteral("F"), [](Look &l) { return l.has(Chords, "columns", 1u << kF); }},
          {QStringLiteral("C"), [](Look &l) { return l.has(Chords, "columns", 1u << kC); }},
          {QStringLiteral("G"), [](Look &l) { return l.has(Chords, "columns", 1u << kG); }}}});

    add({QStringLiteral("Chords"), Chords, QStringLiteral("Space loops what you played"),
         QStringLiteral("Play the four chords again over the beat, and press Space just after the last one. Space "
                        "loops what you just played, on every page: the bars you played become a loop that carries "
                        "on in time."),
         QStringLiteral("Ctrl+← and Ctrl+→ move the key; every part follows."),
         QStringLiteral("The progression loops. Watch CHORD in the transport change with it."),
         {QStringLiteral("Space")},
         {},
         {chordColumn(kAm, QStringLiteral("1")), chordColumn(kF, QStringLiteral("2")), chordColumn(kC, QStringLiteral("3")),
          chordColumn(kG, QStringLiteral("4"))},
         {{QStringLiteral("A chord loop plays"), [](Look &l) { return l.playing(Chords); }}}});

    // --- Bass ------------------------------------------------------------------
    // With follow on, the bass page's columns count from the chord's root:
    // the third column (x 2/12 to 3/12) is always the root.
    const Spot root = spot(2.0f / 12.0f, 0.0f, 3.0f / 12.0f, 0.76f, QStringLiteral("ROOT"));
    add({QStringLiteral("Bass"), Bass, QStringLiteral("The bass follows the chords"),
         QStringLiteral("Press O: the bass follows the chords. Its columns now count from the chord that plays, so "
                        "the lit column is always the root, whichever chord comes round."),
         QStringLiteral("Q W E R T Y U I pick the sound: R is acid, Y a growl."),
         QStringLiteral("Bass and chords are locked together."),
         {QStringLiteral("O")},
         {},
         {root},
         {{QStringLiteral("O: follows the chords"), [](Look &l) { return l.probe(Bass, "follow") > 0.5f; }}}});

    add({QStringLiteral("Bass"), Bass, QStringLiteral("A bassline in one finger"),
         QStringLiteral("Tap the ROOT on the off-beats, between the kicks: dum-DUM, dum-DUM. Play a bar or two, "
                        "then Space. The loop walks with the chords as they change."),
         QStringLiteral("Higher in the column is brighter; press harder for an accent."),
         QStringLiteral("Now it bounces."),
         {QStringLiteral("Space")},
         {},
         {root},
         {{QStringLiteral("A bass loop plays"), [](Look &l) { return l.playing(Bass); }}}});

    add({QStringLiteral("Bass"), Bass, QStringLiteral("Try a variation"),
         QStringLiteral("Hold the ROOT with one finger and touch OFFBEAT or GALLOP on the ROLL rail with another: "
                        "the bass plays that pattern by itself. Slide up on FILTER to open it. Space loops the "
                        "new line."),
         QStringLiteral("FILTER rides whatever the bass plays, its loop too."),
         QStringLiteral("A second bassline. Which one's better?"),
         {QStringLiteral("Space")},
         {},
         {root, spot(0.24f, 0.76f, 0.45f, 1.0f, QStringLiteral("ROLL")),
          spot(0.66f, 0.76f, 0.83f, 1.0f, QStringLiteral("FILTER"), A::Up)},
         {{QStringLiteral("A new bass loop"), [](Look &l) { return l.takesSince(Bass) >= 1; }}}});

    add({QStringLiteral("Bass"), Bass, QStringLiteral("Changed your mind? Ctrl+Z"),
         QStringLiteral("Ctrl+Z takes back the newest loop on any page, and what played before plays again. Press it "
                        "now and the first bassline comes back. Nothing you try can spoil the song."),
         QStringLiteral("Ctrl+N twice clears every loop, for when you want a fresh start."),
         QStringLiteral("Taken back. Experiment freely."),
         {QStringLiteral("Ctrl Z")},
         QStringLiteral("undo"),
         {},
         {{QStringLiteral("Ctrl+Z"), [](Look &l) { return l.undosSince() >= 1; }}}});

    // --- Lead -------------------------------------------------------------------
    // The neck is everything right of x 0.22; the left column holds the
    // harmony rows (from y 0.4 up: −oct, 3rd, 5th, 6th, +oct) over BEND and THROW.
    const Spot neck = spot(0.22f, 0.0f, 1.0f, 1.0f, QStringLiteral("NECK"));
    add({QStringLiteral("Lead"), Lead, QStringLiteral("Sing one note"),
         QStringLiteral("The neck is the scale, low to high. Hold a finger on it and keep still: after a moment the "
                        "note blooms with vibrato. Rock the finger to and fro for your own. Higher on the pad is "
                        "brighter."),
         QStringLiteral("← and → change the sound: anthem, pluck, bell, chip, vox, scream, hoover, big room."),
         QStringLiteral("It sings."),
         {},
         {},
         {spot(0.22f, 0.3f, 1.0f, 0.8f, QStringLiteral("HOLD STILL · OR WIGGLE"))},
         {{QStringLiteral("Vibrato"), [](Look &l) { return l.data(Lead, 14) > 0.03f || l.since(Lead, "wiggles") >= 1; }}}});

    add({QStringLiteral("Lead"), Lead, QStringLiteral("Glide between notes"),
         QStringLiteral("Slide along the neck and the note glides through the scale. Or hold one note and land a "
                        "second finger further up: it glides there without a new attack. Lift it and it falls back."),
         QStringLiteral("Shift+↑ and Shift+↓ make the glides longer or shorter."),
         QStringLiteral("Smooth."),
         {},
         {},
         {spot(0.22f, 0.3f, 1.0f, 0.8f, QStringLiteral("SLIDE"), A::Right)},
         {{QStringLiteral("Three glides"), [](Look &l) { return l.since(Lead, "glides") >= 3; }}}});

    add({QStringLiteral("Lead"), Lead, QStringLiteral("Add a harmony"),
         QStringLiteral("While a note sings, put a finger of your other hand on 3RD in the left column: a second "
                        "voice sings a third above, always in the key. 5TH, 6TH and the octaves are there too."),
         QStringLiteral("Press harder on the harmony finger to bring it out."),
         QStringLiteral("Two voices. Lush."),
         {},
         {},
         {spot(0.0f, 0.52f, 0.22f, 0.64f, QStringLiteral("3RD")), spot(0.22f, 0.3f, 1.0f, 0.8f, QStringLiteral("NOTE"))},
         {{QStringLiteral("A note with a harmony"), [](Look &l) { return l.probe(Lead, "harmonized") >= 1; }}}});

    add({QStringLiteral("Lead"), Lead, QStringLiteral("Your melody"),
         QStringLiteral("Press Enter: the lead lands on the notes of the chord playing, so nothing clashes. Play a "
                        "short melody, a bar or two, play it once more, and press Space: one round of it loops."),
         QStringLiteral("Played it a few times? Space keeps the last round you repeated, not the tries before."),
         QStringLiteral("That's the hook."),
         {QStringLiteral("Enter"), QStringLiteral("Space")},
         {},
         {neck},
         {{QStringLiteral("Enter: on the chord"), [](Look &l) { return l.probe(Lead, "follow") > 0.5f; }},
          {QStringLiteral("A lead loop plays"), [](Look &l) { return l.playing(Lead); }}}});

    // --- Arp --------------------------------------------------------------------
    add({QStringLiteral("Arp"), Arp, QStringLiteral("Notes that run"),
         QStringLiteral("Press M so the arp follows the chords. Then hold two fingers on the grid: the notes run "
                        "up and down in time, through the chord that plays."),
         QStringLiteral("Twist the first finger: the chord under it grows, fifth, triad, seventh."),
         QStringLiteral("Sparkling."),
         {QStringLiteral("M")},
         {},
         {spot(0.0f, 0.135f, 0.58f, 0.865f, QStringLiteral("HOLD NOTES"))},
         {{QStringLiteral("M: follows the chords"),
           [](Look &l) { return l.status(Arp).contains(QLatin1String("following the chords")); }},
          {QStringLiteral("The arp runs"), [](Look &l) { return l.data(Arp, 6) > 0.5f || l.playing(Arp); }}}});

    add({QStringLiteral("Arp"), Arp, QStringLiteral("Pick a pattern, latch, loop"),
         QStringLiteral("Tap a pattern along the top, UP·DN or CONV. Click the pad while notes run: they latch and "
                        "keep running when you lift. Then Space loops it."),
         QStringLiteral("The pads on the right shape it: RATE, RHYTHM, RANGE, TONE, GROOVE, ECHO."),
         QStringLiteral("The arp runs on its own."),
         {QStringLiteral("click"), QStringLiteral("Space")},
         {},
         {spot(0.0f, 0.865f, 0.58f, 1.0f, QStringLiteral("PATTERN")),
          spot(0.0f, 0.135f, 0.58f, 0.865f, QStringLiteral("NOTES · CLICK"))},
         {{QStringLiteral("An arp loop plays"), [](Look &l) { return l.playing(Arp); }}}});

    // --- Atmosphere ---------------------------------------------------------------
    add({QStringLiteral("Atmos"), Atmos, QStringLiteral("A bed of sound"),
         QStringLiteral("Lay three fingers: DRONE, PAD and CHOIR. They sing the chord of the moment, wide and soft. "
                        "Click while they're down: they latch and stay when you lift."),
         QStringLiteral("Q to I are whole scenes at a key: dawn, ocean, cathedral, stardust…"),
         QStringLiteral("Space opens up."),
         {QStringLiteral("click")},
         {},
         {spot(0.0f, 0.15f, 0.2f, 0.85f, QStringLiteral("DRONE")), spot(0.2f, 0.15f, 0.4f, 0.85f, QStringLiteral("PAD")),
          spot(0.4f, 0.15f, 0.6f, 0.85f, QStringLiteral("CHOIR"))},
         {{QStringLiteral("Layers latched"), [](Look &l) { return l.probe(Atmos, "latched") >= 2 || l.playing(Atmos); }}}});

    add({QStringLiteral("Atmos"), Atmos, QStringLiteral("Swell, then loop"),
         QStringLiteral("Hold S: the bed swells, louder and brighter, for as long as you hold it. Let go, then Space "
                        "loops the bed with its chords."),
         QStringLiteral("F freezes the bed; ↑ and ↓ set how big the room is."),
         QStringLiteral("The bed is in."),
         {QStringLiteral("S"), QStringLiteral("Space")},
         {},
         {},
         {{QStringLiteral("S: a swell"), [](Look &l) { return l.since(Atmos, "swells") >= 1; }},
          {QStringLiteral("An atmos loop plays"), [](Look &l) { return l.playing(Atmos); }}}});

    // --- FX: a riser and a drop ---------------------------------------------------
    add({QStringLiteral("FX"), FX, QStringLiteral("A riser"),
         QStringLiteral("Touch NOISE in the 2-BAR row. A riser waits, then climbs for two bars to peak on the next "
                        "phrase line, in time with everything. Stack more columns for a bigger build."),
         QStringLiteral("Higher rows are longer risers, up to sixteen bars."),
         QStringLiteral("Tension's building…"),
         {},
         {},
         {spot(0.0f, 0.36f, 0.2f, 0.52f, QStringLiteral("NOISE · 2 BARS"))},
         {{QStringLiteral("A riser on its way"),
           [](Look &l) { return l.data(FX, 4) >= 1 || l.data(FX, 0) >= 0 || l.data(FX, 14) != l.dataAtStart(FX, 14); }}}});

    add({QStringLiteral("FX"), FX, QStringLiteral("Drop!"),
         QStringLiteral("Don't wait for it: click the pad. Everything rising rushes into a drop on the next bar, "
                        "with an impact on the ONE."),
         QStringLiteral("The bottom row has impacts, sub drops and sweeps: X is the sub drop."),
         QStringLiteral("DROP."),
         {QStringLiteral("click")},
         {},
         {spot(0.0f, 0.2f, 1.0f, 1.0f, QStringLiteral("CLICK"))},
         {{QStringLiteral("A drop"), [](Look &l) { return l.data(FX, 14) != l.dataAtStart(FX, 14) && l.data(FX, 14) >= 0; }}}});

    // --- Arrange: the mix and the song -----------------------------------------
    // Lanes run down from the scene strip (y 0.885), one per part; a lane's
    // clips sit between x 0.12 and 0.73. The bass is the second lane.
    const float lane = 0.885f / 7.0f;
    add({QStringLiteral("Song"), Arrange, QStringLiteral("Point and tap"),
         QStringLiteral("Every part has a lane here with its loops. One finger moves a pointer, like a cursor; a "
                        "second finger's tap, anywhere, does what it points at. Point at the bass loop and tap: it "
                        "stops on the bar. Tap again and it's back. The mouse just clicks."),
         QStringLiteral("Hold the second finger on a loop to pick it: then D copies it, M layers two."),
         QStringLiteral("Every loop is in your hands here."),
         {},
         {},
         {spot(0.12f, 0.885f - 2 * lane, 0.12f + 0.07625f, 0.885f - lane, QStringLiteral("BASS 1"))},
         {{QStringLiteral("The bass stops"), [](Look &l) { return !l.playing(Bass) && l.view().parts[Bass].queued < 0; }},
          {QStringLiteral("…and comes back"), [](Look &l) { return l.checked(0) && l.playing(Bass); }}}});


    add({QStringLiteral("Song"), Arrange, QStringLiteral("Mute and back"),
         QStringLiteral("Q W E R T Y U mute a part at once, in page order, and the tab says so. Mute the drums with Q, hear the space it leaves, and bring them back with Q."),
         QStringLiteral("Hold A S D F G H J to hear one part alone."),
         QStringLiteral("Taking things away is half of arranging."),
         {QStringLiteral("Q")},
         {},
         {},
         {{QStringLiteral("Q: drums muted"), [](Look &l) { return l.muted(Drums); }},
          {QStringLiteral("Q again: back"), [](Look &l) { return l.checked(0) && !l.muted(Drums); }}}});

    add({QStringLiteral("Song"), Arrange, QStringLiteral("A breakdown"),
         QStringLiteral("Press N: from the next bar the beat drops out and the chords and atmosphere breathe alone. "
                        "Wait a few bars, then N again brings everything back on the bar."),
         QStringLiteral("X echoes the last bar out; Z filters the whole mix out and back."),
         QStringLiteral("Hear how much bigger it feels when it comes back?"),
         {QStringLiteral("N")},
         {},
         {spot(0.73f, 0.885f, 0.87f, 1.0f, QStringLiteral("N"))},
         {{QStringLiteral("N: breakdown"), [](Look &l) { return l.status(Arrange).contains(QLatin1String("breakdown")); }},
          {QStringLiteral("N again: back"),
           [](Look &l) { return l.checked(0) && !l.status(Arrange).contains(QLatin1String("breakdown")); }}}});

    add({QStringLiteral("Song"), Arrange, QStringLiteral("Build and drop"),
         QStringLiteral("Press B: the whole mix builds to the end of the phrase, filter rising, an echo roll, a "
                        "breath, then the drop on the ONE. Return drops at once, on the next bar."),
         QStringLiteral("The tension fader on the right does it by hand: push up, let go high to drop."),
         QStringLiteral("That's the moment people wait for."),
         {QStringLiteral("B"), QStringLiteral("Return")},
         {},
         {spot(0.0f, 0.885f, 0.12f, 1.0f, QStringLiteral("B")), spot(0.87f, 0.885f, 1.0f, 1.0f, QStringLiteral("RETURN"))},
         {{QStringLiteral("B: a build"), [](Look &l) { return l.status(Arrange).contains(QLatin1String("build")); }},
          {QStringLiteral("The drop"), [](Look &l) { return l.checked(0) && !l.status(Arrange).contains(QLatin1String("build")); }}}});

    add({QStringLiteral("Song"), Arrange, QStringLiteral("Record your song"),
         QStringLiteral("Press Space: from the next bar the song records everything you do here. Let it groove, N "
                        "for a breakdown, N to come back, B to build and drop. Space again stops. That's a song, "
                        "played by you."),
         QStringLiteral("P plays the song back; Shift+O takes back the last take."),
         QStringLiteral("Recorded. A song, played by hand."),
         {QStringLiteral("Space"), QStringLiteral("N"), QStringLiteral("B")},
         {},
         {},
         {{QStringLiteral("Space: the song records"),
           [](Look &l) {
               const QString st = l.status(Arrange);
               return st.contains(QLatin1String("recording the song")) || st.contains(QLatin1String("punch"));
           }},
          {QStringLiteral("Space: stopped"),
           [](Look &l) {
               const QString st = l.status(Arrange);
               return l.checked(0) && !st.contains(QLatin1String("recording the song")) && !st.contains(QLatin1String("punch"));
           }}}});

    add({QStringLiteral("Song"), Arrange, QStringLiteral("Write the whole track"),
         QStringLiteral("Ctrl+R records everything you hear to a file. Start it, then press I, and I again to "
                        "confirm: tenfold writes a whole track from your loops, intro, groove, builds, drops, a "
                        "breakdown and an outro, and plays it from the next bar."),
         QStringLiteral("Shift+I writes a short one."),
         QStringLiteral("Your track is playing, and recording."),
         {QStringLiteral("Ctrl R"), QStringLiteral("I"), QStringLiteral("I")},
         QStringLiteral("record"),
         {},
         {{QStringLiteral("Ctrl+R: recording"), [](Look &l) { return l.recording(); }},
          {QStringLiteral("I, I: the track plays"), [](Look &l) { return l.songBars() >= 30; }}}});

    add({QStringLiteral("Song"), Arrange, QStringLiteral("Listen, then save"),
         QStringLiteral("This is your track: every part you played, arranged. It ends by itself in about two and a "
                        "half minutes, with an echo-out. When it's over, Ctrl+R stops the recording and saves it in "
                        "Music/tenfold."),
         {},
         QStringLiteral("Saved. You made a track."),
         {QStringLiteral("Ctrl R")},
         QStringLiteral("record"),
         {},
         {{QStringLiteral("Ctrl+R: saved"),
           [](Look &l) {
               if (l.recording())
                   l.flag(0) = true;
               return l.flag(0) && !l.recording();
           }}}});

    add({QStringLiteral("Song"), Arrange, QStringLiteral("You made a track"),
         QStringLiteral("Drums, bass, chords, lead, arp, atmosphere, a riser and a drop, mixed, arranged and "
                        "recorded, all with your fingers. Keep playing: ? shows everything each page can do. Ctrl+N "
                        "twice clears the loops for the next one."),
         QStringLiteral("Esc or Ctrl+T leaves the tutorial."),
         {},
         {QStringLiteral("?"), QStringLiteral("Ctrl N")},
         {},
         {},
         {}});
}

QStringList Tutorial::chapters() const {
    QStringList out;
    for (const auto &s : m_steps)
        if (out.isEmpty() || out.last() != s.chapter)
            out << s.chapter;
    return out;
}

int Tutorial::chapterIndex() const { return int(chapters().indexOf(step().chapter)); }

void Tutorial::start(Session &s) {
    m_active = true;
    enter(0, s, false);
    s.setPage(step().page);
}

void Tutorial::stop() { m_active = false; }

void Tutorial::next(Session &s) {
    if (!m_active)
        return;
    if (m_index + 1 >= count()) {
        ++m_finishes;
        stop();
        return;
    }
    enter(m_index + 1, s, false);
    s.setPage(step().page);
}

void Tutorial::back(Session &s) {
    if (!m_active || m_index == 0)
        return;
    enter(m_index - 1, s, true);
    s.setPage(step().page);
}

void Tutorial::jump(int index, Session &s) {
    m_active = true;
    enter(std::clamp(index, 0, count() - 1), s, false);
    s.setPage(step().page);
}

void Tutorial::enter(int index, Session &s, bool backward) {
    m_index = index;
    m_state = Working;
    m_checked.assign(step().checks.size(), false);
    Look &l = *m_look;
    l = Look{};
    l.s = &s;
    l.now = s.clock();
    l.base = s.view();
    for (int i = 0; i < kProbeCount; ++i) {
        const float v = s.probe(kProbes[i].page, QString::fromLatin1(kProbes[i].name));
        l.baseProbe[i] = std::isnan(v) ? 0.0f : v;
    }
    l.baseUndos = s.undos();
    l.checks = &m_checked;
    // A step that is already done when it begins: going forward it
    // celebrates and moves on; come back to with Back, it waits for Next.
    update(s);
    if (m_state == Done && backward)
        m_state = Reviewed;
}

void Tutorial::update(Session &s) {
    if (!m_active)
        return;
    Look &l = *m_look;
    const double now = s.clock();
    l.dt = std::clamp(now - l.now, 0.0, 0.25);
    l.now = now;
    const auto &checks = step().checks;
    for (size_t i = 0; i < checks.size(); ++i)
        if (!m_checked[i] && checks[i].test(l))
            m_checked[i] = true;
    if (m_state == Working && !checks.empty() && std::all_of(m_checked.begin(), m_checked.end(), [](bool b) { return b; })) {
        m_state = Done;
        m_doneAt = now;
    }
    if (m_state == Done && now - m_doneAt >= kCheer && m_index + 1 < count()) {
        enter(m_index + 1, s, false);
        // Within a part's chapter the page stays; a new chapter asks for its
        // page by number, which is the way to it the player will keep using.
    }
}

void Tutorial::paint(QPainter &p, const PaintContext &pc, int page, const QString &pageName) const {
    if (!m_active)
        return;
    const Step &st = step();
    const QColor hot = pc.color.lighter(130);
    const float pulse = 0.5f + 0.5f * float(std::sin(pc.time * 5.0));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (page != st.page) {
        // Not on the step's page yet: say which number takes the player there.
        p.fillRect(pc.pad, QColor(0, 0, 0, 150));
        const QString key = st.page == Arrange ? QStringLiteral("0") : QString::number(st.page + 1);
        const QRectF box(pc.pad.center().x() - 240, pc.pad.center().y() - 70, 480, 140);
        p.fillRect(box, QColor(0, 0, 0, 230));
        p.setPen(QPen(console::amber(), 3));
        p.drawRect(box);
        console::text(p, box.adjusted(0, 12, 0, -60), QStringLiteral("TUTORIAL"), console::amber(), 14, true, Qt::AlignCenter);
        console::text(p, box.adjusted(0, 40, 0, -10), QStringLiteral("PRESS %1 · %2").arg(key, pageName.toUpper()),
                      console::amber(), 34, true, Qt::AlignCenter);
        p.restore();
        return;
    }
    if (st.spots.empty() || m_state != Working) {
        if (m_state == Done) {
            // A brief green flash round the pad: done.
            const float a = float(std::clamp(1.0 - (pc.time - m_doneAt) / kCheer, 0.0, 1.0));
            p.setPen(QPen(console::alpha(console::green(), a), 6));
            p.drawRect(pc.pad.adjusted(-4, -4, 4, 4));
        }
        p.restore();
        return;
    }
    // Dim the pad around the spots, so the eye goes where the fingers go.
    QPainterPath dim;
    dim.addRect(pc.pad);
    QPainterPath holes;
    for (const Spot &s : st.spots)
        holes.addRect(pc.rect(s.x0, s.y0, s.x1, s.y1));
    p.fillPath(dim.subtracted(holes), QColor(0, 0, 0, 120));
    for (const Spot &s : st.spots) {
        const QRectF r = pc.rect(s.x0, s.y0, s.x1, s.y1).adjusted(2, 2, -2, -2);
        // A glowing frame with bright corners, breathing.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(console::alpha(hot, 0.25f + 0.35f * pulse), 6));
        p.drawRect(r);
        p.setPen(QPen(hot, 2.5));
        p.drawRect(r);
        const qreal c = std::min({22.0, r.width() / 3, r.height() / 3});
        p.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::SquareCap));
        for (int k = 0; k < 4; ++k) {
            const QPointF o(k % 2 ? r.right() : r.left(), k / 2 ? r.bottom() : r.top());
            const qreal sx = k % 2 ? -1 : 1, sy = k / 2 ? -1 : 1;
            p.drawLine(o, o + QPointF(sx * c, 0));
            p.drawLine(o, o + QPointF(0, sy * c));
        }
        if (s.arrow != Spot::None) {
            // Chevrons drifting the way the finger should slide.
            const bool vertical = s.arrow == Spot::Up || s.arrow == Spot::Down;
            const qreal dir = (s.arrow == Spot::Up || s.arrow == Spot::Left) ? -1 : 1;
            const qreal span = vertical ? r.height() : r.width();
            const qreal size = std::min(18.0, std::min(r.width(), r.height()) / 4);
            for (int k = 0; k < 3; ++k) {
                const qreal f = std::fmod(pc.time * 0.6 + k / 3.0, 1.0);
                const qreal along = (dir > 0 ? f : 1 - f) * span * 0.7 + span * 0.15;
                const QPointF at = vertical ? QPointF(r.center().x(), r.top() + along) : QPointF(r.left() + along, r.center().y());
                const float a = float(std::sin(f * 3.14159));
                p.setPen(QPen(console::alpha(Qt::white, a), 3));
                if (vertical) {
                    p.drawLine(at + QPointF(-size, -dir * size * 0.6), at);
                    p.drawLine(at + QPointF(size, -dir * size * 0.6), at);
                } else {
                    p.drawLine(at + QPointF(-dir * size * 0.6, -size), at);
                    p.drawLine(at + QPointF(-dir * size * 0.6, size), at);
                }
            }
        }
        if (!s.label.isEmpty())
            console::tag(p, QPointF(r.center().x(), r.top() + 16), s.label, hot, 13, true);
    }
    p.restore();
}

}
