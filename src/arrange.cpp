// The arrange page: where the loops made on the other pages become a track.
//
// It has no engine of its own. It conducts: it launches the parts' clips on
// the bar, rides their mixer channels and the master bus, and keeps a song,
// a sequence of bars recorded by playing (or written by the page from the
// loops it finds) that replays from background() whatever page is showing.
//
// Three layers make up what the mixer hears, recomputed every tick:
//
//   inputs   the performer's mix per part (level, pan, filter, sends, pump,
//            mute, solo) and the tension fader: the home settings plus what
//            the fingers are riding now, or the song's recorded values plus
//            the player's trims while a song plays;
//   moves    timed transitions (build, drop, breakdown, echo-out, cut,
//            filter-out, key lift), pure functions of the beat, so they land
//            on bar lines and replay identically from a song;
//   the mix  inputs and moves combined into channel and master values, sent
//            to the studio only when they change.
//
// Clip launches are bar-quantised by the studio itself, so they are posted a
// beat ahead and land sample-exact. Mixer changes are control-rate, so they
// are evaluated a tick ahead: a mute that opens a drop lands just before the
// line rather than just after it, and the first kick arrives whole.

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "console.h"
#include "parts.h"

namespace tf {

namespace {

constexpr int P = kMaxParts;
constexpr int kSteps = 16;        // song steps per bar: sixteenths
constexpr int kMaxSongBars = 256; // about eight minutes at 124
constexpr double kGap = 0.5;      // the breath before a drop, in beats

// EchoTime settings that pick a quarter, an eighth and a sixteenth note
// (the studio picks from 1/16, 1/8, dotted 1/8, 1/4, dotted 1/4, 1/2).
constexpr float kEcho16 = 0.02f, kEcho8 = 0.2f, kEcho4 = 0.55f;

// The pad, in pad units with y up.
constexpr float kNameW = 0.12f;  // part names, left
constexpr float kCellsR = 0.73f; // clip cells end here
constexpr float kStripR = 0.87f; // lane readouts end here; the tension fader takes the rest
constexpr float kTopY = 0.885f;  // the scene strip and the move buttons, above the lanes

float clampf01(float v) { return std::clamp(v, 0.0f, 1.0f); }
double fract(double v) { return v - std::floor(v); }
float smooth01(float u) {
    u = clampf01(u);
    return u * u * (3 - 2 * u);
}

// What each part is to a track, found by name so the page follows the
// studio's order whatever it becomes.
enum Role { RDrums, RBass, RChords, RLead, RArp, RAtmos, RFx, RoleCount };

// --- the song -----------------------------------------------------------------

// What the performer's hands had set, one sixteenth of the song: every
// channel of every part (mute and solo as 0 or 1) and the tension fader,
// quantised to bytes.
struct Step {
    uint8_t tension = 128;
    uint8_t ch[P][ChannelCount] = {};
};
uint8_t q8(float v) { return uint8_t(std::lround(clampf01(v) * 255.0f)); }
float dq8(uint8_t v) { return float(v) / 255.0f; }

enum Label : uint8_t { LNone, LIntro, LGroove, LBuild, LDrop, LBreak, LOutro, LEnd, LabelCount };
const char *labelName(int l) {
    static const char *names[] = {"", "Intro", "Groove", "Build", "Drop", "Break", "Outro", "End"};
    return names[std::clamp(l, 0, int(LabelCount) - 1)];
}

struct SongBar {
    int8_t slot[P];       // the clip each part plays, -1 none
    uint8_t relaunch = 0; // parts whose clip starts from its top at this bar
    uint8_t label = LNone;
    SongBar() { std::fill(std::begin(slot), std::end(slot), int8_t(-1)); }
};

enum MoveKind : uint8_t { MBuild, MDrop, MBreak, MUnbreak, MEchoOut, MCut, MSink, MRise, MLift, MoveKinds };

struct SongMove {
    int step; // sixteenths from the song's top
    uint8_t kind;
    int param; // build: bars; break: muted parts; cut: sixteenths; lift: semitones
};

struct Song {
    std::vector<SongBar> bars;
    std::vector<Step> steps; // bars × kSteps
    std::vector<SongMove> moves;
    double bpm = 124;
    int root = 9, family = 0, brightness = 4;
    int length() const { return int(bars.size()); }
    bool empty() const { return bars.empty(); }
    void resize(int n) {
        n = std::clamp(n, 0, kMaxSongBars);
        bars.resize(size_t(n));
        steps.resize(size_t(n) * kSteps);
    }
};

struct Section {
    int bar0, bars, label;
};

// --- moves --------------------------------------------------------------------

struct Move {
    uint8_t kind = MBuild;
    double b0 = 0, b1 = 1e18; // when it acts, in studio beats
    double aux = 1e18;        // a second moment: where a filter-out turns back, where a break's effects end
    double gap = 0;           // build: the silent breath before b1
    float flat = -1;          // build: a held tension (the fader let go high) instead of a ramp
    float auxValue = -1;     // sink: its depth when it turned back; drop: the echo time to hold through the ONE
    int param = 0;
    bool song = false; // replayed from the song: it launches nothing, the song's bars do
    double endFor[P];  // break: when each muted part comes back
    Move() { std::fill(std::begin(endFor), std::end(endFor), 1e18); }
};

// What the moves and the tension add to the performer's mix for one moment.
struct Mods {
    float add[P][ChannelCount] = {};
    float pumpMul[P];
    bool mute[P] = {};
    float master[MasterCount] = {};
    float echoTime = -1;
    float tBuild = 0, tBuildFilter = 0; // build tension from moves
    Mods() { std::fill(std::begin(pumpMul), std::end(pumpMul), 1.0f); }
};

// --- the page -----------------------------------------------------------------

class ArrangeControl : public PartControl {
public:
    ArrangeControl(QStringList names, std::vector<QColor> colors)
        : m_names(std::move(names)), m_colors(std::move(colors)) {
        auto find = [&](std::initializer_list<const char *> keys, int fallback) {
            for (int i = 0; i < m_names.size(); ++i)
                for (const char *k : keys)
                    if (m_names[i].contains(QLatin1String(k), Qt::CaseInsensitive))
                        return i;
            return fallback < m_names.size() ? fallback : -1;
        };
        m_role[RDrums] = find({"drum", "beat"}, 0);
        m_role[RBass] = find({"bass"}, 1);
        m_role[RChords] = find({"chord"}, 2);
        m_role[RLead] = find({"lead", "melod"}, 3);
        m_role[RArp] = find({"arp"}, 4);
        m_role[RAtmos] = find({"atmo", "pad"}, 5);
        m_role[RFx] = find({"fx", "riser", "effect"}, 6);
        m_moves.reserve(32);
    }

    // --- time -----------------------------------------------------------------

    void background(double t, ControlContext &ctx) override { tick(t, ctx); }

    void tick(double t, ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        // Wait for the studio's first view: until then its master is zeros.
        if (!m_ready) {
            if (v.sample == 0)
                return;
            adoptAll(v, t);
        }
        m_parts = std::clamp(v.partCount, 0, P);
        const double dt = t - m_lastT;
        if (dt > 0 && dt < 0.1)
            m_tickDt = std::clamp(std::max(dt, m_tickDt * 0.97), 0.004, 0.03);
        if (dt > 0)
            m_lastT = t;
        const double bps = v.bpm / 60.0;
        const double beat = ctx.beatAt(t);
        // The studio's clock jumps when the first loop sets the tempo: the bar
        // bookkeeping starts again from where it now is.
        if (beat < m_lastBeat - 0.5 || beat > m_lastBeat + 64) {
            const int bar = int(std::floor(beat / kBeatsPerBar));
            m_plannedBar = bar;
            m_lateBar = bar;
            m_recBarDone = std::max(m_recBarDone, bar - 1);
        }
        m_lastBeat = beat;
        m_beat = beat;
        m_look = (m_tickDt * 1.25 + 0.003) * bps;
        const double beatE = beat + m_look;

        m_anyClip = false;
        for (int p = 0; p < m_parts; ++p)
            m_anyClip = m_anyClip || hasClips(v, p);
        for (int p = 0; p < m_parts; ++p)
            if (v.parts[p].playing >= 0) {
                m_lastSlot[p] = v.parts[p].playing;
                m_lastPlayedBar[p] = int(std::floor(beat / kBeatsPerBar));
            }

        springs(dt > 0 && dt < 0.1 ? dt : 0.0);
        songMoves(beat, ctx);
        earlyPhase(beat, ctx);
        latePhase(beatE, ctx);
        compose(beatE, t, ctx);
        record(beat, v);
        expire(beat);
    }

    // --- fingers --------------------------------------------------------------

    void frame(const Frame &f, ControlContext &ctx) override {
        m_frame = f;
        m_parts = std::clamp(ctx.studio().partCount, 0, P);
        for (auto &fg : m_fingers) {
            if (fg.id < 0)
                continue;
            bool still = false;
            for (int i = 0; i < f.count; ++i)
                still |= f.touches[i].id == fg.id;
            if (!still) {
                lift(fg, f.t, ctx);
                fg = Finger{};
            }
        }
        for (int i = 0; i < f.count; ++i) {
            const Touch &t = f.touches[i];
            Finger *fg = finger(t.id);
            if (!fg) {
                fg = finger(-1);
                if (fg)
                    land(*fg, t, f.t, ctx);
                continue;
            }
            fg->x = t.x;
            fg->y = t.y;
            fg->pressure = t.pressure;
            fg->angle = t.angle;
            fg->trust = t.eccentricity;
            if (std::hypot((t.x - fg->x0) * ctx.aspect(), t.y - fg->y0) > 0.035f)
                fg->moved = true;
            if (fg->kind == Finger::Name && !fg->solo && f.t - fg->t0 > 0.28) {
                fg->solo = true;
                m_heldSolo[fg->lane] = true;
            }
            if (fg->kind == Finger::Tension)
                m_tensionLive = std::clamp(fg->value0 + (t.y - fg->y0) * 1.9f, -1.0f, 1.0f);
        }
        // The click: latches what a finger rides, stores a scene, drops.
        if (f.button && !m_button)
            click(ctx);
        m_button = f.button;
        rides(f.t);
        tick(f.t, ctx);
    }

    void leave(ControlContext &) override {
        // Rides spring home and held solos let go; the song and the clips go on.
        for (auto &fg : m_fingers)
            if (fg.id >= 0) {
                if (fg.kind == Finger::Lane)
                    foldGain(fg.lane, fg.dGain);
                fg = Finger{};
            }
        std::fill(std::begin(m_heldSolo), std::end(m_heldSolo), false);
        m_tensionHeld = false;
        m_frame = Frame{};
        rides(m_lastT);
    }

    // --- keys -----------------------------------------------------------------

    bool key(const KeyEvent &k, ControlContext &ctx) override {
        const bool shift = k.modifiers & Qt::ShiftModifier;
        // Solos are held: A S D F G H J, while the key is down.
        static const int soloKeys[] = {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H, Qt::Key_J};
        for (int p = 0; p < 7; ++p)
            if (k.key == soloKeys[p]) {
                if (p >= m_parts || k.repeat)
                    return true;
                if (shift) {
                    if (k.down)
                        toggleLatchedSolo(p, ctx);
                } else {
                    m_heldSolo[p] = k.down;
                }
                return true;
            }
        if (!k.down || k.repeat)
            return false;
        if (k.key >= Qt::Key_F1 && k.key <= Qt::Key_F8) {
            const int s = k.key - Qt::Key_F1;
            if (shift)
                storeScene(s, ctx);
            else
                launchScene(s, ctx);
            return true;
        }
        static const int muteKeys[] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U};
        for (int p = 0; p < 7; ++p)
            if (k.key == muteKeys[p]) {
                if (p < m_parts) {
                    if (shift)
                        m_muteAtBar[p] = nextBar(); // on the next bar line
                    else
                        toggleMute(p);
                }
                return true;
            }
        switch (k.key) {
        case Qt::Key_Return:
        case Qt::Key_Enter: drop(ctx); return true;
        case Qt::Key_B: build(shift ? 8 : 4, ctx); return true;
        case Qt::Key_N: breakdown(ctx); return true;
        case Qt::Key_X: echoOut(ctx); return true;
        case Qt::Key_C: cut(shift ? 2 : 4, ctx); return true;
        case Qt::Key_Z: filterOut(ctx); return true;
        case Qt::Key_L: liftKey(shift ? -2 : 2, ctx); return true;
        case Qt::Key_V: variation(ctx); return true;
        case Qt::Key_I: autoArrange(shift, ctx); return true;
        case Qt::Key_Space: // on the part pages Space records a loop; here, the song
        case Qt::Key_O: toggleRecord(ctx); return true;
        case Qt::Key_P: togglePlay(shift, ctx); return true;
        case Qt::Key_Delete: clearSlot(m_selPart, m_selSlot, ctx); return true;
        case Qt::Key_Left: jumpSection(-1, ctx); return true;
        case Qt::Key_Right: jumpSection(1, ctx); return true;
        case Qt::Key_Backspace:
            if (shift)
                clearSong(ctx);
            else
                stopEverything(ctx);
            return true;
        default: return false;
        }
    }

    // --- paint, help, status --------------------------------------------------

    void paint(QPainter &p, const PaintContext &pc) override;

    QString help() const override {
        return QStringLiteral(
            "tap a clip\tstarts it on the next bar; tap a playing one to stop it\n"
            "hold a lane and slide\tup and down rides its level, left and right its DJ filter (springs back)\n"
            "press harder on a lane\tthrows it into the echo; a twist pumps it harder\n"
            "second finger on a lane\tup washes it in reverb, sideways pans it\n"
            "click while riding\tlatches the filter and sends where they are\n"
            "hold a clip still, then click\tclears it (or touch it and press Delete)\n"
            "tap a part's name\tmutes it; hold the name to solo it, click to keep the solo\n"
            "tension fader, right\tup builds (filter, wash, an echo roll), down sinks the mix\n"
            "let go of it high\tthe build drops on the next ONE\n"
            "scene strip, top\ttap a scene to launch it on the bar; hold it and click to store\n"
            "BUILD, BREAK, DROP\tthe moves, on the bar\n"
            "F1–F8\tlaunch a scene; Shift stores what plays into it\n"
            "Q W E R T Y U\tmute a part now; with Shift, on the next bar\n"
            "A S D F G H J\tsolo while held; with Shift, latched\n"
            "Return\tDROP: everything back on the next ONE, after a breath\n"
            "B\tBUILD to the end of the 4-bar phrase (Shift: 8), then the drop\n"
            "N\tbreakdown to chords and atmos; again to bring it back\n"
            "X\techo-out: the last bar into the echo, then out\n"
            "C\tcut the last beat before the bar (Shift: half a beat)\n"
            "Z\tfilter the mix out; again to open it back onto a bar\n"
            "L\tlift the key a whole tone on the bar (Shift: down)\n"
            "V\tevery part to its next clip on the bar\n"
            "I\twrite a whole track from the loops and play it (Shift: a short one)\n"
            "Space or O\trecord the song from the next bar (punch in while it plays); again to stop\n"
            "P\tplay the song from the top (Shift: loop it); again to stop\n"
            "Left, Right\tjump a section back or on, on the bar\n"
            "Backspace\tstop everything on the bar; Shift clears the song");
    }

    QString status() const override;

private:
    struct Finger {
        enum Kind : uint8_t { None, Scene, Button, Name, Lane, Send, Tension } kind = None;
        int id = -1;
        int lane = -1, index = -1;
        double t0 = 0;
        float x0 = 0, y0 = 0, x = 0, y = 0, a0 = 0, angle = 0, pressure = 0.5f, trust = 0;
        float value0 = 0, dGain = 0;
        bool moved = false, clicked = false, solo = false;
    };
    struct Ride {
        float filt = 0, echo = 0, verb = 0, pump = 0, pan = 0;
        bool held = false;
    };
    struct Posted {
        int bar = -1000000, slot = -2;
        bool relaunch = false;
    };
    enum Button { BBuild, BBreak, BDrop };

    // --- helpers --------------------------------------------------------------

    Finger *finger(int id) {
        for (auto &f : m_fingers)
            if (f.id == id)
                return &f;
        return nullptr;
    }
    int role(Role r) const { return m_role[r] >= 0 && m_role[r] < m_parts ? m_role[r] : -1; }
    bool hasClips(const StudioView &v, int p) const {
        if (p < 0 || p >= m_parts)
            return false;
        for (const auto &c : v.parts[p].clips)
            if (c.state != ClipView::Empty)
                return true;
        return false;
    }
    int firstSlot(const StudioView &v, int p, int after = -1) const {
        if (p < 0 || p >= m_parts)
            return -1;
        for (int k = 1; k <= kClipSlots; ++k) {
            const int s = (after + k + kClipSlots) % kClipSlots;
            if (v.parts[p].clips[s].state != ClipView::Empty)
                return s;
        }
        return -1;
    }
    // The clip a part will be playing once the coming bar line has passed.
    int predicted(const StudioView &v, int p, int bar) const {
        if (m_posted[p].bar == bar)
            return m_posted[p].slot;
        const PartView &pv = v.parts[p];
        if (pv.queued >= 0)
            return pv.queued;
        if (pv.playing >= 0 && pv.clips[pv.playing].state == ClipView::Stopping)
            return -1;
        return pv.playing;
    }
    // The clip a part played lately (now, or within sixteen bars), if it
    // still holds one; -1 for a part the player hasn't played.
    int recentSlot(const StudioView &v, int p, int bar) const {
        const int now = predicted(v, p, bar);
        if (now >= 0)
            return now;
        const int s = m_lastSlot[p];
        if (s < 0 || bar - m_lastPlayedBar[p] > 16 || v.parts[p].clips[s].state == ClipView::Empty)
            return -1;
        return s;
    }
    int nextBar() const { return int(std::floor(m_beat / kBeatsPerBar)) + 1; }
    static const char *noteOf(int root) { return Tuning::noteName(((root % 12) + 12) % 12); }
    QColor laneColor(int i, QColor fallback) const {
        return i >= 0 && i < int(m_colors.size()) ? m_colors[size_t(i)] : fallback;
    }
    bool recordable(double b0) const {
        if (m_recording)
            return b0 >= m_recStartBar * double(kBeatsPerBar);
        return m_recArmBar >= 0 && b0 >= m_recArmBar * double(kBeatsPerBar);
    }
    const Move *pending(uint8_t kind) const {
        for (const auto &m : m_moves)
            if (m.kind == kind && m_beat < m.b1 + (kind == MDrop ? 2.0 : 0.0))
                return &m;
        return nullptr;
    }

    // --- the mixer: home, inputs, moves, the sum -------------------------------

    void adoptAll(const StudioView &v, double t) {
        m_ready = true;
        for (int p = 0; p < P; ++p)
            for (int c = 0; c < ChannelCount; ++c) {
                m_home[p][c] = m_sent[p][c] = v.parts[p].channel.value[c];
                m_sentAt[p][c] = t;
            }
        for (int m = 0; m < MasterCount; ++m) {
            m_masterHome[m] = m_masterSent[m] = v.master[m];
            m_masterSentAt[m] = t;
        }
    }

    // The performer's mix at a moment: home plus rides, or the song plus trims
    // plus rides while a song plays.
    void inputs(double beatE, float out[P][ChannelCount], float &tension) const {
        const Step *st = songStep(beatE);
        for (int p = 0; p < m_parts; ++p) {
            for (int c = 0; c < ChannelCount; ++c)
                out[p][c] = st ? dq8(st->ch[p][c]) + m_trim[p][c] : m_home[p][c];
            if (st)
                out[p][Mute] = float((st->ch[p][Mute] > 127) != m_muteFlip[p]);
            const Ride &r = m_ride[p];
            out[p][Gain] = clampf01(out[p][Gain] + liveGain(p));
            out[p][Filter] = clampf01(out[p][Filter] + r.filt);
            out[p][EchoSend] = clampf01(out[p][EchoSend] + r.echo);
            out[p][ReverbSend] = clampf01(out[p][ReverbSend] + r.verb);
            out[p][PumpDepth] = clampf01(out[p][PumpDepth] + r.pump);
            out[p][Pan] = clampf01(out[p][Pan] + r.pan);
            out[p][Mute] = out[p][Mute] > 0.5f ? 1.0f : 0.0f;
            out[p][Solo] = (out[p][Solo] > 0.5f || m_heldSolo[p]) ? 1.0f : 0.0f;
        }
        const bool live = m_tensionHeld || std::fabs(m_tensionLive) > 0.002f || !st;
        tension = live ? m_tensionLive : dq8(st->tension) * 2.0f - 1.0f;
    }
    float liveGain(int p) const {
        float g = 0;
        for (const auto &f : m_fingers)
            if (f.id >= 0 && f.kind == Finger::Lane && f.lane == p)
                g += f.dGain;
        return g;
    }
    const Step *songStep(double beat) const {
        if (!m_playing || m_song.empty())
            return nullptr;
        const long s = long(std::floor((beat - m_origin * double(kBeatsPerBar)) * 4.0));
        if (s < 0)
            return nullptr;
        const long n = long(m_song.steps.size());
        return &m_song.steps[size_t(std::min(s, n - 1))];
    }

    void applyTension(float tPos, float tFilt, float tNeg, float tFader, Mods &m) const {
        const int drums = role(RDrums);
        if (tPos > 0 || tFilt > 0) {
            // A build: the mix thins from below, the room grows and shimmers,
            // the echoes run longer, everything breathes harder with the kick.
            m.master[MasterFilter] += 0.2f * tFilt * tFilt;
            m.master[RoomSize] += std::max(0.0f, 0.86f - m_masterHome[RoomSize]) * tPos;
            m.master[RoomShimmer] += 0.35f * tPos;
            m.master[EchoFeedback] += std::max(0.0f, 0.62f - m_masterHome[EchoFeedback]) * tPos;
            m.master[MasterDrive] += 0.12f * tPos;
            for (int p = 0; p < m_parts; ++p) {
                if (p == drums)
                    continue;
                m.add[p][ReverbSend] += 0.32f * tPos;
                m.add[p][PumpDepth] += 0.3f * tPos;
                m.add[p][Filter] += 0.1f * tFilt;
                if (p == role(RChords) || p == role(RLead) || p == role(RArp) || p == role(RAtmos))
                    m.add[p][EchoSend] += 0.25f * tPos;
            }
        }
        // The fader's echo roll: past half way the drums pour into a tape
        // echo stepping at eighths, near the top at sixteenths.
        if (tFader >= 0.5f && drums >= 0) {
            m.add[drums][EchoSend] += 0.75f * smooth01((tFader - 0.45f) / 0.4f);
            const float et = tFader >= 0.85f ? kEcho16 : kEcho8;
            m.echoTime = m.echoTime < 0 ? et : std::min(m.echoTime, et);
        }
        if (tNeg > 0) {
            // Sinking: the mix goes under water.
            m.master[MasterFilter] -= 0.33f * std::pow(tNeg, 0.8f);
            m.master[RoomSize] += 0.15f * tNeg;
            for (int p = 0; p < m_parts; ++p)
                if (p != drums)
                    m.add[p][ReverbSend] += 0.18f * tNeg;
        }
    }

    void applyMove(const Move &mv, double b, Mods &m) const {
        auto muteAll = [&] {
            for (int p = 0; p < m_parts; ++p)
                m.mute[p] = true;
        };
        if (b < mv.b0)
            return;
        switch (mv.kind) {
        case MBuild: {
            if (b >= mv.b1)
                return;
            const double rampEnd = mv.b1 - mv.gap;
            float t;
            if (mv.flat >= 0) {
                // The fader let go high: its tension (and its echo roll) holds to the line.
                t = mv.flat;
                const int drums = role(RDrums);
                if (drums >= 0 && t >= 0.5f) {
                    m.add[drums][EchoSend] += 0.75f * smooth01((t - 0.45f) / 0.4f);
                    const float et = t >= 0.85f ? kEcho16 : kEcho8;
                    m.echoTime = m.echoTime < 0 ? et : std::min(m.echoTime, et);
                }
            } else {
                const double u = std::clamp((b - mv.b0) / std::max(0.25, rampEnd - mv.b0), 0.0, 1.0);
                t = float(0.08 + 0.92 * std::pow(u, 1.5));
                // The bass leaves for the last two beats, to come back with the drop.
                const int bass = role(RBass);
                const double L = mv.b1 - mv.b0, rem = mv.b1 - b;
                if (bass >= 0 && rem <= 2.0)
                    m.mute[bass] = true;
                // The echo roll: from half way the drums pour into the tape
                // echo, which steps from quarters to eighths to sixteenths
                // (and each step zips up in pitch as the tape speeds), so
                // every hit rolls, exactly in time, faster toward the drop.
                const int drums = role(RDrums);
                if (rem <= L / 2) {
                    const float et = rem <= L / 8 + mv.gap ? kEcho16 : rem <= L / 4 ? kEcho8 : kEcho4;
                    m.echoTime = m.echoTime < 0 ? et : std::min(m.echoTime, et);
                    if (drums >= 0)
                        m.add[drums][EchoSend] += 0.85f * smooth01(float((L / 2 - rem) / (L / 2) * 1.6));
                    m.master[EchoFeedback] += 0.12f * smooth01(float((L / 2 - rem) / (L / 2)));
                }
            }
            m.tBuild = std::max(m.tBuild, t);
            // The filter lets go a moment before the ONE, inside the silent
            // gap, so the first kick of the drop arrives whole.
            if (b < mv.b1 - std::min(mv.gap, 0.2))
                m.tBuildFilter = std::max(m.tBuildFilter, t);
            if (mv.gap > 0 && b >= rampEnd)
                muteAll();
            break;
        }
        case MDrop: {
            const double u = (b - mv.b0) / 2.0;
            if (u < 1)
                m.master[MasterDrive] += 0.16f * float(1 - u);
            // The echo roll stops dead on the ONE: no feedback for a beat,
            // then its time goes home on a silent tape, without a zip.
            if (mv.auxValue >= 0 && b < mv.b0 + 1.5) {
                m.master[EchoFeedback] -= 1.0f;
                m.echoTime = m.echoTime < 0 ? mv.auxValue : std::min(m.echoTime, mv.auxValue);
                for (int p = 0; p < m_parts; ++p)
                    m.add[p][EchoSend] -= 1.0f;
            }
            break;
        }
        case MBreak: {
            if (b >= mv.b1)
                return;
            for (int p = 0; p < m_parts; ++p)
                if ((mv.param >> p) & 1 && b < mv.endFor[p])
                    m.mute[p] = true;
            const float ramp = smooth01(float((b - mv.b0) / 4.0));
            // No kick, no pump: what is left breathes freely until the drums return.
            const int drums = role(RDrums);
            const double pumpBack = drums >= 0 ? std::min(mv.endFor[drums], mv.b1) : mv.b1;
            if (b < pumpBack)
                for (int p = 0; p < m_parts; ++p)
                    m.pumpMul[p] *= 1.0f - ramp;
            if (b < mv.aux) {
                for (int p = 0; p < m_parts; ++p)
                    if (!((mv.param >> p) & 1))
                        m.add[p][ReverbSend] += 0.24f * ramp;
                m.master[RoomSize] += 0.16f * ramp;
                m.master[MasterFilter] -= 0.07f * ramp;
            }
            break;
        }
        case MEchoOut: {
            const double e = b - mv.b0;
            const int drums = role(RDrums);
            if (e < 4) {
                // The last bar pours into the echo.
                const float u = float(e / 4.0);
                for (int p = 0; p < m_parts; ++p)
                    m.add[p][EchoSend] += (p == drums ? 0.45f * u : 0.8f * std::pow(u, 0.7f));
                m.master[EchoFeedback] += std::max(0.0f, 0.74f - m_masterHome[EchoFeedback]) * u;
                m.master[MasterFilter] += 0.06f * u;
            } else if (e < 20) {
                // Then only the echo is left: it slows like tape for a moment
                // (the delay stretches to a quarter note), and fades.
                const float v = float((e - 4) / 16.0);
                const float hold = e < 8 ? 1.0f : 1.0f - smooth01(float((e - 8) / 12.0));
                m.master[EchoFeedback] += std::max(0.0f, 0.74f - m_masterHome[EchoFeedback]) * hold;
                m.master[MasterFilter] += 0.06f + 0.08f * v;
                m.echoTime = kEcho4;
                for (int p = 0; p < m_parts; ++p)
                    m.add[p][EchoSend] += 0.8f * hold;
            }
            break;
        }
        case MCut:
            if (b < mv.b1)
                muteAll();
            break;
        case MSink: {
            float depth = smooth01(float((b - mv.b0) / 8.0));
            if (b >= mv.aux) {
                const float u = smooth01(float((b - mv.aux) / std::max(0.25, mv.b1 - mv.aux)));
                depth = mv.auxValue * (1 - u);
            }
            if (b >= mv.b1)
                depth = 0;
            m.master[MasterFilter] -= 0.31f * depth;
            m.master[RoomSize] += 0.1f * depth;
            break;
        }
        default: break;
        }
    }

    void compose(double beatE, double t, ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        float in[P][ChannelCount] = {};
        float tension = 0;
        inputs(beatE, in, tension);
        m_tensionShown = tension;
        Mods mods;
        m_flatShown = 0;
        for (const auto &mv : m_moves) {
            applyMove(mv, beatE, mods);
            if (mv.kind == MBuild && mv.flat >= 0 && beatE >= mv.b0 && beatE < mv.b1 - mv.gap)
                m_flatShown = std::max(m_flatShown, mv.flat);
        }
        const float tPos = std::max(tension, 0.0f);
        m_buildShown = std::max(tPos, mods.tBuild);
        applyTension(std::max(tPos, mods.tBuild), std::max(tPos, mods.tBuildFilter), std::max(-tension, 0.0f), tPos,
                     mods);
        m_echoShown = mods.echoTime >= 0 && mods.echoTime < 0.58f && m_buildShown > 0 ? mods.echoTime : -1.0f;

        bool idle = !m_playing && m_moves.empty() && std::fabs(m_tensionLive) < 0.002f && !m_tensionHeld;
        for (int p = 0; p < m_parts; ++p) {
            const Ride &r = m_ride[p];
            idle = idle && !r.held && r.filt == 0 && r.echo == 0 && r.verb == 0 && r.pump == 0 && r.pan == 0 &&
                   !m_heldSolo[p];
        }
        for (int p = 0; p < m_parts; ++p) {
            float out[ChannelCount];
            for (int c = 0; c < ChannelCount; ++c)
                out[c] = clampf01(in[p][c] + mods.add[p][c]);
            out[PumpDepth] = clampf01((in[p][PumpDepth] + mods.add[p][PumpDepth]) * mods.pumpMul[p]);
            out[Mute] = (in[p][Mute] > 0.5f || mods.mute[p]) ? 1.0f : 0.0f;
            out[Solo] = in[p][Solo];
            for (int c = 0; c < ChannelCount; ++c) {
                // At rest, whatever another page set becomes the new home.
                if (idle && t - m_sentAt[p][c] > 0.3 &&
                    std::fabs(v.parts[p].channel.value[c] - m_sent[p][c]) > 0.001f) {
                    m_home[p][c] = m_sent[p][c] = v.parts[p].channel.value[c];
                    continue;
                }
                sendChannel(ctx, p, c, out[c], t);
            }
            m_shown[p] = {out[Filter], out[EchoSend], out[ReverbSend], out[PumpDepth], out[Gain], out[Pan]};
        }
        for (int m = 0; m < MasterCount; ++m) {
            float val = clampf01(m_masterHome[m] + mods.master[m]);
            if (m == EchoTime && mods.echoTime >= 0)
                val = mods.echoTime;
            if (idle && t - m_masterSentAt[m] > 0.3 && std::fabs(v.master[m] - m_masterSent[m]) > 0.001f) {
                m_masterHome[m] = m_masterSent[m] = v.master[m];
                continue;
            }
            const float d = std::fabs(val - m_masterSent[m]);
            if (d > 0.004f || (d > 0.0005f && t - m_masterSentAt[m] > 0.04)) {
                ctx.setMaster(m, val);
                m_masterSent[m] = val;
                m_masterSentAt[m] = t;
            }
        }
    }

    void sendChannel(ControlContext &ctx, int p, int c, float value, double t) {
        float &last = m_sent[p][c];
        if (c == Mute || c == Solo) {
            if (value != last) {
                ctx.setChannel(p, c, value);
                last = value;
                m_sentAt[p][c] = t;
            }
            return;
        }
        // Lean: only real changes, and small ones at most about 25 times a second.
        const float d = std::fabs(value - last);
        if (d > 0.004f || (d > 0.0005f && t - m_sentAt[p][c] > 0.04)) {
            ctx.setChannel(p, c, value);
            last = value;
            m_sentAt[p][c] = t;
        }
    }

    // Lanes nobody holds spring back home, and so does the tension fader.
    void springs(double dt) {
        const float k = dt > 0 ? float(std::exp(-dt / 0.12)) : 1.0f;
        for (int p = 0; p < P; ++p) {
            Ride &r = m_ride[p];
            if (r.held)
                continue;
            for (float *x : {&r.filt, &r.echo, &r.verb, &r.pump, &r.pan}) {
                *x *= k;
                if (std::fabs(*x) < 0.002f)
                    *x = 0;
            }
        }
        if (!m_tensionHeld) {
            m_tensionLive *= dt > 0 ? float(std::exp(-dt / 0.22)) : 1.0f;
            if (std::fabs(m_tensionLive) < 0.003f)
                m_tensionLive = 0;
        }
    }

    static float deadband(float d, float band) {
        return std::fabs(d) < band ? 0.0f : d - std::copysign(band, d);
    }

    // What the fingers on each lane ride now.
    void rides(double t) {
        Ride next[P];
        for (const auto &f : m_fingers) {
            if (f.id < 0 || f.lane < 0 || (f.kind != Finger::Lane && f.kind != Finger::Send))
                continue;
            Ride &r = next[f.lane];
            r.held = true;
            const float dx = f.x - f.x0, dy = f.y - f.y0;
            if (f.kind == Finger::Lane) {
                // Sideways is the DJ filter: left low-pass, right high-pass,
                // with a dead zone so a resting finger leaves it open.
                r.filt += std::clamp(deadband(dx, 0.02f) * 1.7f, -0.45f, 0.45f);
                // Pressing past a normal touch throws the lane into the echo.
                r.echo += 0.85f * smooth01((f.pressure - 0.62f) / 0.3f);
                // A twist pumps it harder (or less), as far as the contact's
                // shape can be trusted.
                float da = f.angle - f.a0;
                while (da > 3.14159f)
                    da -= 6.28318f;
                while (da < -3.14159f)
                    da += 6.28318f;
                r.pump += std::clamp(-da / 1.2f, -0.6f, 0.6f) * std::clamp(f.trust * 2.0f, 0.0f, 1.0f);
            } else {
                r.verb += std::clamp(deadband(dy, 0.02f) * 1.6f, -0.3f, 0.8f);
                r.pan += std::clamp(deadband(dx, 0.02f) * 1.8f, -0.5f, 0.5f);
            }
        }
        for (int p = 0; p < P; ++p) {
            if (next[p].held)
                m_ride[p] = next[p];
            else
                m_ride[p].held = false;
        }
        // Each lane's first finger rides its level with a vertical slide,
        // once it is clearly a slide (or a hold) and not a tap.
        for (auto &f : m_fingers)
            if (f.id >= 0 && f.kind == Finger::Lane) {
                if (!f.moved && t - f.t0 < 0.3) {
                    f.dGain = 0;
                    continue;
                }
                const float base = m_playing ? gainOfSong(f.lane) : m_home[f.lane][Gain];
                f.dGain = std::clamp(f.value0 + deadband(f.y - f.y0, 0.025f) * 1.6f, 0.0f, 1.0f) - base;
            }
    }
    float gainOfSong(int p) const {
        const Step *st = songStep(m_beat);
        return st ? clampf01(dq8(st->ch[p][Gain]) + m_trim[p][Gain]) : m_home[p][Gain];
    }

    void foldGain(int p, float dGain) {
        if (p < 0 || dGain == 0)
            return;
        if (m_playing)
            m_trim[p][Gain] += dGain;
        else
            m_home[p][Gain] = clampf01(m_home[p][Gain] + dGain);
    }
    // A click while riding: the ride becomes the lane's new setting.
    void latch(int p) {
        Ride &r = m_ride[p];
        const float add[ChannelCount] = {0, r.pan, r.filt, r.verb, r.echo, r.pump, 0, 0};
        for (int c = 0; c < ChannelCount; ++c) {
            if (add[c] == 0)
                continue;
            if (m_playing)
                m_trim[p][c] += add[c];
            else
                m_home[p][c] = clampf01(m_home[p][c] + add[c]);
        }
        r = Ride{};
    }

    void land(Finger &f, const Touch &t, double time, ControlContext &ctx) {
        f = Finger{};
        f.id = t.id;
        f.t0 = time;
        f.x0 = f.x = t.x;
        f.y0 = f.y = t.y;
        f.a0 = f.angle = t.angle;
        f.pressure = t.pressure;
        f.trust = t.eccentricity;
        if (t.y >= kTopY) {
            if (t.x < kNameW) {
                f.kind = Finger::Button;
                f.index = BBuild;
                build(4, ctx);
            } else if (t.x < kCellsR) {
                f.kind = Finger::Scene;
                f.index = std::clamp(int((t.x - kNameW) / (kCellsR - kNameW) * kClipSlots), 0, kClipSlots - 1);
            } else {
                f.kind = Finger::Button;
                f.index = t.x < kStripR ? BBreak : BDrop;
                if (f.index == BBreak)
                    breakdown(ctx);
                else
                    drop(ctx);
            }
            return;
        }
        if (t.x >= kStripR) {
            // The tension fader picks up where it is, wherever the finger lands.
            for (auto &o : m_fingers)
                if (o.id >= 0 && o.id != f.id && o.kind == Finger::Tension)
                    o.kind = Finger::None;
            f.kind = Finger::Tension;
            f.value0 = m_tensionLive;
            m_tensionHeld = true;
            return;
        }
        const int lanes = std::max(1, m_parts);
        f.lane = std::clamp(int((kTopY - t.y) / kTopY * float(lanes)), 0, lanes - 1);
        if (t.x < kNameW) {
            f.kind = Finger::Name;
            return;
        }
        f.index = t.x < kCellsR
                      ? std::clamp(int((t.x - kNameW) / (kCellsR - kNameW) * kClipSlots), 0, kClipSlots - 1)
                      : -1;
        bool first = true;
        for (const auto &o : m_fingers)
            first = first && !(o.id >= 0 && o.id != f.id && o.kind == Finger::Lane && o.lane == f.lane);
        f.kind = first ? Finger::Lane : Finger::Send;
        if (f.index >= 0) {
            m_selPart = f.lane;
            m_selSlot = f.index;
        }
        if (f.kind == Finger::Lane)
            f.value0 = m_playing ? gainOfSong(f.lane) : m_home[f.lane][Gain];
    }

    void lift(Finger &f, double time, ControlContext &ctx) {
        const bool tap = time - f.t0 < 0.3 && !f.moved;
        switch (f.kind) {
        case Finger::Scene:
            if (tap && !f.clicked)
                launchScene(f.index, ctx);
            break;
        case Finger::Name:
            if (f.solo)
                m_heldSolo[f.lane] = false;
            else if (tap)
                toggleMute(f.lane);
            break;
        case Finger::Lane:
        case Finger::Send:
            if (f.kind == Finger::Lane)
                foldGain(f.lane, f.dGain);
            if (f.clicked)
                latch(f.lane);
            if (tap && f.index >= 0 && !f.clicked)
                toggleClip(f.lane, f.index, ctx);
            break;
        case Finger::Tension:
            m_tensionHeld = false;
            // Let go high: the build is spent on the next ONE.
            if (m_tensionLive >= 0.6f)
                dropFromFader(ctx);
            break;
        default: break;
        }
    }

    void click(ControlContext &ctx) {
        for (auto &f : m_fingers) {
            if (f.id < 0)
                continue;
            f.clicked = true;
            switch (f.kind) {
            case Finger::Scene: storeScene(f.index, ctx); break;
            case Finger::Lane:
            case Finger::Send:
                // A clip held still for a moment, then a click: it is cleared.
                if (f.index >= 0 && !f.moved && m_frame.t - f.t0 > 0.25 && f.dGain == 0 && m_ride[f.lane].filt == 0 &&
                    m_ride[f.lane].echo < 0.01f)
                    clearSlot(f.lane, f.index, ctx);
                break;
            case Finger::Name:
                m_heldSolo[f.lane] = false;
                toggleLatchedSolo(f.lane, ctx);
                f.kind = Finger::None;
                break;
            case Finger::Tension:
                f.kind = Finger::None;
                m_tensionHeld = false;
                dropFromFader(ctx);
                break;
            default: break;
            }
        }
    }

    // --- clips and scenes -----------------------------------------------------

    void post(ControlContext &ctx, int p, int slot, int bar, bool relaunch) {
        if (slot >= 0)
            ctx.launchPart(p, slot);
        else
            ctx.stopPart(p);
        m_posted[p] = {bar, slot, relaunch};
        if (slot >= 0)
            m_relaunchAt[p] = bar;
    }

    // Clears a slot made by accident (the part's page made it; only the
    // player clears it).
    void clearSlot(int p, int slot, ControlContext &ctx) {
        if (p < 0 || p >= m_parts || slot < 0)
            return;
        if (ctx.studio().parts[p].clips[slot].state == ClipView::Empty) {
            ctx.notice(QStringLiteral("%1 slot %2 is already empty").arg(m_names.value(p)).arg(slot + 1));
            return;
        }
        ctx.clearPart(p, slot);
        if (m_lastSlot[p] == slot)
            m_lastSlot[p] = -1;
        ctx.notice(QStringLiteral("Cleared %1 slot %2").arg(m_names.value(p)).arg(slot + 1));
    }

    void toggleClip(int p, int slot, ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        const ClipView &c = v.parts[p].clips[slot];
        const int bar = nextBar();
        if (c.state == ClipView::Empty) {
            ctx.notice(QStringLiteral("%1 slot %2 is empty: make a loop on page %3")
                           .arg(m_names.value(p))
                           .arg(slot + 1)
                           .arg(p + 1));
            return;
        }
        if (predicted(v, p, bar) == slot)
            post(ctx, p, -1, bar, false);
        else
            post(ctx, p, slot, bar, true);
    }

    // A scene: each part's clip, or keep playing (-2), or stop (-1). Until
    // stored, scene k is column k, and a part with nothing in that column
    // keeps whatever it plays, so loops made in any order still make scenes.
    void sceneSlots(int s, const StudioView &v, int8_t out[P]) const {
        for (int p = 0; p < P; ++p) {
            if (m_scene[s].stored)
                out[p] = m_scene[s].slot[p];
            else
                out[p] = p < m_parts && v.parts[p].clips[s].state != ClipView::Empty ? int8_t(s) : int8_t(-2);
        }
    }
    void launchScene(int s, ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        int8_t sl[P];
        sceneSlots(s, v, sl);
        const int bar = nextBar();
        int n = 0;
        for (int p = 0; p < m_parts; ++p) {
            if (sl[p] == -2)
                continue;
            if (sl[p] >= 0 && v.parts[p].clips[sl[p]].state == ClipView::Empty)
                continue;
            post(ctx, p, sl[p], bar, sl[p] >= 0);
            n += sl[p] >= 0;
        }
        m_sceneQueued = s;
        m_sceneQueuedBar = bar;
        ctx.notice(n ? QStringLiteral("Scene %1 on the bar").arg(s + 1) : QStringLiteral("Scene %1 is empty").arg(s + 1));
    }
    void storeScene(int s, ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        auto &sc = m_scene[s];
        sc.stored = true;
        int n = 0;
        for (int p = 0; p < P; ++p) {
            const int will = p < m_parts ? predicted(v, p, nextBar()) : -1;
            sc.slot[p] = int8_t(will);
            n += will >= 0;
        }
        ctx.notice(QStringLiteral("Scene %1 stored: %2 parts").arg(s + 1).arg(n));
    }

    void variation(ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        const int bar = nextBar();
        int n = 0;
        for (int p = 0; p < m_parts; ++p) {
            const int now = predicted(v, p, bar);
            if (now < 0)
                continue;
            const int s = firstSlot(v, p, now);
            if (s >= 0 && s != now) {
                post(ctx, p, s, bar, true);
                ++n;
            }
        }
        ctx.notice(n ? QStringLiteral("Variation on the bar") : QStringLiteral("No other clips to vary to"));
    }

    void stopEverything(ControlContext &ctx) {
        const int bar = nextBar();
        for (int p = 0; p < m_parts; ++p)
            post(ctx, p, -1, bar, false);
        if (m_playing)
            stopSong();
        if (m_recording || m_recArmBar >= 0)
            m_recStopBar = bar;
        for (auto &mv : m_moves)
            mv.b1 = std::min(mv.b1, double(bar * kBeatsPerBar));
        ctx.notice(QStringLiteral("Everything stops on the bar"));
    }

    void toggleMute(int p) {
        if (p < 0 || p >= m_parts)
            return;
        if (m_playing)
            m_muteFlip[p] = !m_muteFlip[p];
        else
            m_home[p][Mute] = m_home[p][Mute] > 0.5f ? 0.0f : 1.0f;
    }
    bool songSolo(int p) const {
        const Step *st = songStep(m_beat);
        return st && st->ch[p][Solo] > 127;
    }
    void toggleLatchedSolo(int p, ControlContext &ctx) {
        if (p < 0 || p >= m_parts)
            return;
        bool on;
        if (m_playing) {
            on = !((songSolo(p) ? 1.0f : 0.0f) + m_trim[p][Solo] > 0.5f);
            m_trim[p][Solo] = on ? 1.0f : (songSolo(p) ? -1.0f : 0.0f);
        } else {
            on = m_home[p][Solo] < 0.5f;
            m_home[p][Solo] = on ? 1.0f : 0.0f;
        }
        ctx.notice(QStringLiteral("%1 solo %2").arg(m_names.value(p), on ? QStringLiteral("on") : QStringLiteral("off")));
    }

    // --- moves ----------------------------------------------------------------

    Move *active(uint8_t kind) {
        for (auto &m : m_moves)
            if (m.kind == kind && m_beat < m.b1)
                return &m;
        return nullptr;
    }
    void addMove(const Move &m, ControlContext &ctx) {
        if (m_moves.size() >= 32)
            m_moves.erase(m_moves.begin());
        m_moves.push_back(m);
        // Recorded where it starts, so the song does it again.
        if (!m.song && recordable(m.b0))
            recordMoveAt(m.kind, m.b0, m.param);
        // The launches of a move on a bar that's already planned go out now.
        const int bar = int(std::lround(m.b0 / kBeatsPerBar));
        if (std::fabs(m.b0 - bar * kBeatsPerBar) < 1e-6 && bar <= m_plannedBar && bar > m_beat / kBeatsPerBar)
            planBar(bar, ctx, true);
    }
    void recordMoveAt(uint8_t kind, double beat, int param) {
        if (!recordable(beat))
            return;
        const int step = int(std::lround((beat - m_origin * double(kBeatsPerBar)) * 4.0));
        if (step < 0)
            return;
        // Kept in order of time, so the song replays them in order.
        auto at = std::upper_bound(m_song.moves.begin(), m_song.moves.end(), step,
                                   [](int s, const SongMove &m) { return s < m.step; });
        m_song.moves.insert(at, {step, kind, param});
    }

    // Everything comes back on the next ONE, after half a beat of air if
    // there is time for it.
    void drop(ControlContext &ctx) {
        const double nb = nextBar() * double(kBeatsPerBar);
        bool any = false;
        for (int p = 0; p < m_parts; ++p)
            any = any || recentSlot(ctx.studio(), p, nextBar()) >= 0;
        if (!any && !active(MBuild)) {
            ctx.notice(QStringLiteral("Nothing to drop: no part has played yet"));
            return;
        }
        for (const auto &m : m_moves)
            if (m.kind == MDrop && m.b0 <= nb + 1e-6 && m.b0 > m_beat) {
                ctx.notice(QStringLiteral("DROP on the ONE"));
                return;
            }
        if (Move *b = active(MBuild)) {
            // A build in progress is cut short: it drops on the next bar.
            b->b1 = nb;
            b->gap = nb - m_beat >= 0.75 ? kGap : 0;
            for (auto &m : m_moves)
                if (m.kind == MDrop && m.b0 > nb)
                    m.b0 = nb;
            recordMoveAt(MDrop, nb, 0);
            planBar(nextBar(), ctx, true);
            ctx.notice(QStringLiteral("DROP on the ONE"));
            return;
        }
        if (nb - m_beat >= 0.75)
            addGap(nb, false);
        Move d;
        d.kind = MDrop;
        d.b0 = nb;
        d.b1 = nb + 8;
        endMovesAt(nb);
        addMove(d, ctx);
        ctx.notice(QStringLiteral("DROP on the ONE"));
    }
    void addGap(double nb, bool song) {
        Move gap;
        gap.kind = MCut;
        gap.b0 = nb - kGap;
        gap.b1 = nb;
        gap.song = song;
        m_moves.push_back(gap);
    }
    void endMovesAt(double b) {
        for (auto &m : m_moves)
            if (m.b0 < b && (m.kind == MBreak || m.kind == MSink || m.kind == MBuild))
                m.b1 = std::min(m.b1, b);
    }
    void dropFromFader(ControlContext &ctx) {
        // The fader's tension holds to the line, takes a breath, and drops.
        const double nb = nextBar() * double(kBeatsPerBar);
        if (const Move *b = pending(MBuild); b && b->b1 > m_beat && b->b1 <= nb + 1e-6)
            return;
        endMovesAt(nb);
        Move b;
        b.kind = MBuild;
        b.b0 = m_beat;
        b.b1 = nb;
        b.flat = std::clamp(m_tensionLive, 0.6f, 1.0f);
        b.gap = nb - m_beat >= 0.75 ? kGap : 0;
        m_moves.push_back(b);
        Move d;
        d.kind = MDrop;
        d.b0 = nb;
        d.b1 = nb + 8;
        d.auxValue = m_tensionLive >= 0.85f ? kEcho16 : kEcho8;
        m_tensionLive = 0;
        addMove(d, ctx);
        ctx.notice(QStringLiteral("DROP on the ONE"));
    }

    // A build that runs to the end of the phrase (4 bars, or 8), at least
    // half a phrase long, then the drop.
    void build(int phrase, ControlContext &ctx) {
        if (active(MBuild) || active(MEchoOut)) {
            ctx.notice(QStringLiteral("Already moving"));
            return;
        }
        const int start = nextBar();
        int end = (start / phrase + 1) * phrase;
        if (end - start < phrase / 2)
            end += phrase;
        startBuild(start, end - start, false, ctx);
        ctx.notice(QStringLiteral("BUILD: %1 bars to the drop").arg(end - start));
    }
    void startBuild(int bar, int bars, bool song, ControlContext &ctx) {
        Move b;
        b.kind = MBuild;
        b.b0 = bar * double(kBeatsPerBar);
        b.b1 = (bar + bars) * double(kBeatsPerBar);
        b.gap = kGap;
        b.param = bars;
        b.song = song;
        // Out of a breakdown: the arp comes back first, the drums half way
        // (as the echo starts to roll), the rest with the drop.
        for (auto &m : m_moves)
            if (m.kind == MBreak && m.b1 > b.b0) {
                m.aux = std::min(m.aux, b.b0);
                const int arp = role(RArp), drums = role(RDrums);
                if (arp >= 0)
                    m.endFor[arp] = std::min(m.endFor[arp], b.b0);
                if (drums >= 0)
                    m.endFor[drums] = std::min(m.endFor[drums], b.b0 + (b.b1 - b.b0) / 2);
                m.b1 = std::min(m.b1, b.b1);
            }
        Move d;
        d.kind = MDrop;
        d.b0 = b.b1;
        d.b1 = b.b1 + 8;
        d.song = song;
        d.auxValue = kEcho16;
        m_moves.push_back(d); // the build records itself; its drop comes with it
        addMove(b, ctx);
    }

    void breakdown(ControlContext &ctx) {
        const int bar = nextBar();
        if (Move *b = active(MBreak)) {
            // Again: everything back on the bar, without the drama of a drop.
            b->b1 = std::min(b->b1, bar * double(kBeatsPerBar));
            recordMoveAt(MUnbreak, bar * double(kBeatsPerBar), 0);
            ctx.notice(QStringLiteral("Back in on the bar"));
            return;
        }
        Move m;
        m.kind = MBreak;
        m.b0 = bar * double(kBeatsPerBar);
        m.param = breakMask(ctx.studio());
        addMove(m, ctx);
        ctx.notice(QStringLiteral("BREAKDOWN on the bar"));
    }
    // What a breakdown takes away: the rhythm, keeping the chords and the
    // atmosphere (or, with neither, the melody), so it is never silence.
    int breakMask(const StudioView &v) const {
        int keep = 0;
        const int bar = nextBar();
        auto plays = [&](Role r) { return role(r) >= 0 && predicted(v, role(r), bar) >= 0; };
        for (Role r : {RChords, RAtmos})
            if (plays(r))
                keep |= 1 << role(r);
        if (!keep)
            for (Role r : {RLead, RArp})
                if (plays(r)) {
                    keep |= 1 << role(r);
                    break;
                }
        if (role(RFx) >= 0)
            keep |= 1 << role(RFx);
        int mask = 0;
        for (int p = 0; p < m_parts; ++p)
            if (!((keep >> p) & 1))
                mask |= 1 << p;
        return mask;
    }

    void echoOut(ControlContext &ctx) {
        if (active(MEchoOut))
            return;
        Move m;
        m.kind = MEchoOut;
        m.b0 = nextBar() * double(kBeatsPerBar);
        m.b1 = m.b0 + 20;
        addMove(m, ctx);
        ctx.notice(QStringLiteral("ECHO-OUT: one more bar"));
    }

    void cut(int sixteenths, ControlContext &ctx) {
        double nb = nextBar() * double(kBeatsPerBar);
        const double len = sixteenths / 4.0;
        if (nb - len < m_beat + m_look)
            nb += kBeatsPerBar;
        Move m;
        m.kind = MCut;
        m.b0 = nb - len;
        m.b1 = nb;
        m.param = sixteenths;
        addMove(m, ctx);
        ctx.notice(QStringLiteral("CUT before the bar"));
    }

    void filterOut(ControlContext &ctx) {
        if (Move *s = active(MSink)) {
            if (s->aux < 1e17)
                return;
            // Back up, landing open on the next bar (or the one after, if
            // the next is too close to make a sweep of it).
            double nb = nextBar() * double(kBeatsPerBar);
            if (nb - m_beat < 1.5)
                nb += kBeatsPerBar;
            s->auxValue = smooth01(float((m_beat - s->b0) / 8.0));
            s->aux = m_beat;
            s->b1 = nb;
            recordMoveAt(MRise, m_beat, 0);
            ctx.notice(QStringLiteral("The filter opens onto the bar"));
            return;
        }
        Move m;
        m.kind = MSink;
        m.b0 = m_beat;
        addMove(m, ctx);
        ctx.notice(QStringLiteral("Filtering out"));
    }

    void liftKey(int semis, ControlContext &ctx) {
        Move m;
        m.kind = MLift;
        m.b0 = nextBar() * double(kBeatsPerBar);
        m.b1 = m.b0 + 0.01;
        m.param = semis;
        addMove(m, ctx);
        ctx.notice(QStringLiteral("Key of %1 on the bar").arg(QString::fromUtf8(noteOf(ctx.studio().root + semis))));
    }

    void expire(double beat) {
        m_moves.erase(std::remove_if(m_moves.begin(), m_moves.end(),
                                     [&](const Move &m) {
                                         const double tail = m.kind == MDrop ? 2.0 : 0.0;
                                         return beat > std::max(m.b1, m.b0 + tail) + 0.5;
                                     }),
                      m_moves.end());
    }

    // --- the bar lines --------------------------------------------------------

    // A beat before each bar: what the song and the moves launch on it.
    void earlyPhase(double beat, ControlContext &ctx) {
        const int next = int(std::floor(beat / kBeatsPerBar)) + 1;
        if (beat >= next * double(kBeatsPerBar) - 1.0 && next > m_plannedBar) {
            m_plannedBar = next;
            planBar(next, ctx, false);
        }
    }

    void planBar(int bar, ControlContext &ctx, bool again) {
        const StudioView &v = ctx.studio();
        int want[P];
        bool relaunch[P] = {};
        std::fill(std::begin(want), std::end(want), -3); // no change
        const double b = bar * double(kBeatsPerBar);

        // The song: what changes on this bar, and phrase tops.
        if (m_playing && !m_song.empty()) {
            int sb = bar - m_origin;
            if (sb >= m_song.length() && m_loop && !m_recording) {
                m_origin += m_song.length();
                m_jumpBar = bar;
                sb = 0;
            }
            if (sb >= 0 && sb < m_song.length()) {
                const SongBar &cur = m_song.bars[size_t(sb)];
                const bool top = bar == m_jumpBar;
                for (int p = 0; p < m_parts; ++p) {
                    const int prev = sb > 0 ? m_song.bars[size_t(sb - 1)].slot[p] : -2;
                    const bool re = (cur.relaunch >> p) & 1;
                    if (top || cur.slot[p] != prev || re) {
                        want[p] = cur.slot[p];
                        relaunch[p] = cur.slot[p] >= 0 && (top || re);
                    }
                }
            }
        }
        // A take starts phrase-aligned: everything playing starts from its top.
        if (m_recArmBar == bar && !m_playing)
            for (int p = 0; p < m_parts; ++p) {
                const int s = predicted(v, p, bar);
                if (s >= 0) {
                    want[p] = s;
                    relaunch[p] = true;
                }
            }
        // The moves. A move launches nothing the player didn't play: the
        // drop brings back what was playing (or played in the last sixteen
        // bars, before a breakdown or an echo-out), each on the clip it last
        // played, all from the top of their loops, together.
        for (const auto &m : m_moves) {
            if (m.song || m.kind != MDrop || std::fabs(m.b0 - b) > 1e-6)
                continue;
            for (int p = 0; p < m_parts; ++p) {
                const int s = recentSlot(v, p, bar);
                if (s < 0)
                    continue;
                want[p] = s;
                relaunch[p] = true;
            }
        }
        // An echo-out stops everything once its bar has poured out, song or not.
        for (const auto &m : m_moves)
            if (m.kind == MEchoOut && std::fabs(m.b0 + 4 - b) < 1e-6)
                for (int p = 0; p < m_parts; ++p) {
                    want[p] = -1;
                    relaunch[p] = false;
                }
        for (int p = 0; p < m_parts; ++p) {
            if (want[p] == -3)
                continue;
            if (want[p] >= 0 && v.parts[p].clips[want[p]].state == ClipView::Empty)
                continue;
            if (again && m_posted[p].bar == bar && m_posted[p].slot == want[p] &&
                (m_posted[p].relaunch || !relaunch[p]))
                continue;
            const int now = predicted(v, p, bar);
            if (want[p] == now && !relaunch[p])
                continue;
            if (want[p] < 0 && now < 0)
                continue;
            post(ctx, p, want[p], bar, relaunch[p]);
        }
    }

    // Just before each bar line (a tick ahead): what changes exactly on it.
    void latePhase(double beatE, ControlContext &ctx) {
        const int bar = int(std::floor(beatE / kBeatsPerBar));
        while (m_lateBar < bar) {
            const int b = ++m_lateBar;
            const double line = b * double(kBeatsPerBar);
            for (int p = 0; p < m_parts; ++p)
                if (m_muteAtBar[p] == b) {
                    toggleMute(p);
                    m_muteAtBar[p] = -1;
                }
            for (auto &m : m_moves) {
                if (std::fabs(m.b0 - line) > 1e-6)
                    continue;
                if (m.kind == MLift) {
                    const StudioView &v = ctx.studio();
                    ctx.setKey(v.root + m.param, v.family, v.brightness);
                }
                if (m.kind == MDrop && !m.song) {
                    // The drop brings back what the hands had muted, too.
                    for (int p = 0; p < m_parts; ++p) {
                        if (m_playing) {
                            m_muteFlip[p] = false;
                            m_trim[p][Solo] = 0;
                        } else {
                            m_home[p][Mute] = 0;
                            m_home[p][Solo] = 0;
                        }
                    }
                }
            }
            if (m_playing && b == m_jumpBar && b == m_origin && m_song.length() > 0) {
                const StudioView &v = ctx.studio();
                if (v.root != m_song.root || v.family != m_song.family || v.brightness != m_song.brightness)
                    ctx.setKey(m_song.root, m_song.family, m_song.brightness);
            }
            // The end of the song: it stops; the clips play on as they are.
            if (m_playing && !m_recording && m_recArmBar < 0 && b - m_origin >= m_song.length() && !m_loop) {
                stopSong();
                ctx.notice(QStringLiteral("The end"));
            }
            if (m_recArmBar == b) {
                m_recording = true;
                m_recStartBar = b;
                m_recArmBar = -1;
                m_recStep = long(b - m_origin) * kSteps - 1;
            }
            if (m_recStopBar == b) {
                m_recording = false;
                m_recStopBar = -1;
                m_song.resize(std::max(m_song.length(), std::min(b - m_origin, kMaxSongBars)));
                if (m_playing) {
                    // The trims and flips are in the take now.
                    for (int p = 0; p < P; ++p) {
                        std::fill(std::begin(m_trim[p]), std::end(m_trim[p]), 0.0f);
                        m_muteFlip[p] = false;
                    }
                }
                labelSong();
                ctx.notice(QStringLiteral("Song: %1 bars").arg(m_song.length()));
            }
            if (m_recording && b - m_origin >= kMaxSongBars) {
                m_recording = false;
                labelSong();
            }
        }
    }

    void stopSong() {
        // What the song had set becomes home.
        for (int p = 0; p < m_parts; ++p) {
            for (int c = 0; c < ChannelCount; ++c)
                if (c != Mute && c != Solo)
                    m_home[p][c] = clampf01(m_sent[p][c]);
            m_home[p][Mute] = m_home[p][Solo] = 0;
            std::fill(std::begin(m_trim[p]), std::end(m_trim[p]), 0.0f);
            m_muteFlip[p] = false;
        }
        // The moves' own offsets are not home: take them back out.
        for (int p = 0; p < m_parts; ++p) {
            m_home[p][Filter] = clampf01(m_home[p][Filter] - (m_shown[p].filt - m_home[p][Filter]) * 0.0f);
        }
        m_playing = false;
        m_loop = false;
    }

    // Song moves start a beat ahead of their step, so their launches can be
    // planned; their effects wait for their own beat.
    void songMoves(double beat, ControlContext &ctx) {
        if (!m_playing || m_song.moves.empty())
            return;
        const double top = m_origin * double(kBeatsPerBar);
        if (m_moveCursorOrigin != m_origin) {
            m_moveCursorOrigin = m_origin;
            m_moveCursor = 0;
        }
        while (m_moveCursor < m_song.moves.size()) {
            const SongMove &sm = m_song.moves[m_moveCursor];
            const double at = top + sm.step / 4.0;
            if (at - 1.0 > beat + 1e-9)
                break;
            ++m_moveCursor;
            if (at < beat - 0.5)
                continue; // long gone (a jump landed past it)
            Move m;
            m.b0 = at;
            m.song = true;
            m.param = sm.param;
            const bool was = m_recording;
            m_recording = false; // replayed moves are in the song already
            switch (sm.kind) {
            case MBuild: startBuild(int(std::lround(at / kBeatsPerBar)), std::max(1, sm.param), true, ctx); break;
            case MDrop:
                if (Move *b = active(MBuild); b && at < b->b1) {
                    b->b1 = at;
                    for (auto &d : m_moves)
                        if (d.kind == MDrop && d.b0 > at)
                            d.b0 = at;
                } else {
                    addGap(at, true);
                    m.kind = MDrop;
                    m.b1 = at + 8;
                    endMovesAt(at);
                    addMove(m, ctx);
                }
                break;
            case MBreak:
                m.kind = MBreak;
                addMove(m, ctx);
                break;
            case MUnbreak:
                if (Move *b = active(MBreak))
                    b->b1 = std::min(b->b1, at);
                break;
            case MEchoOut:
                m.kind = MEchoOut;
                m.b1 = at + 20;
                addMove(m, ctx);
                break;
            case MCut:
                m.kind = MCut;
                m.b0 = at - std::max(1, sm.param) / 4.0;
                m.b1 = at;
                addMove(m, ctx);
                break;
            case MSink:
                m.kind = MSink;
                addMove(m, ctx);
                break;
            case MRise:
                if (Move *s = active(MSink)) {
                    s->auxValue = smooth01(float((at - s->b0) / 8.0));
                    s->aux = at;
                    double nb = (std::floor(at / kBeatsPerBar) + 1) * kBeatsPerBar;
                    if (nb - at < 1.5)
                        nb += kBeatsPerBar;
                    s->b1 = nb;
                }
                break;
            case MLift:
                m.kind = MLift;
                m.b1 = at + 0.01;
                addMove(m, ctx);
                break;
            default: break;
            }
            m_recording = was;
        }
    }

    // --- the song: recording, playing, writing --------------------------------

    void record(double beat, const StudioView &v) {
        if (!m_recording)
            return;
        const double top = m_origin * double(kBeatsPerBar);
        const long step = long(std::floor((beat - top) * 4.0));
        if (step < 0)
            return;
        const int bars = int(step / kSteps) + 1;
        if (bars > kMaxSongBars)
            return;
        if (bars > m_song.length())
            m_song.resize(bars);
        if (step > m_recStep) {
            float in[P][ChannelCount] = {};
            float tension = 0;
            inputs(beat, in, tension);
            for (long s = std::max(m_recStep + 1, step - 64); s <= step; ++s) {
                Step &st = m_song.steps[size_t(s)];
                // A fader let go high holds its tension to the drop: so does the take.
                st.tension = q8(0.5f + 0.5f * (tension >= 0 ? std::max(tension, m_flatShown) : tension));
                for (int p = 0; p < P; ++p)
                    for (int c = 0; c < ChannelCount; ++c)
                        st.ch[p][c] = p < m_parts ? q8(in[p][c]) : 0;
            }
            m_recStep = step;
        }
        // The clips of a bar are read a sixteenth into it, once they've launched.
        const int bar = int(std::floor((beat - 0.25) / kBeatsPerBar));
        if (bar > m_recBarDone && bar >= m_recStartBar) {
            m_recBarDone = bar;
            const int sb = bar - m_origin;
            if (sb >= 0 && sb < m_song.length()) {
                SongBar &sbar = m_song.bars[size_t(sb)];
                sbar.relaunch = 0;
                for (int p = 0; p < m_parts; ++p) {
                    sbar.slot[p] = int8_t(v.parts[p].playing);
                    if ((m_relaunchAt[p] == bar || bar == m_recStartBar) && v.parts[p].playing >= 0)
                        sbar.relaunch |= uint8_t(1 << p);
                }
            }
        }
    }

    void toggleRecord(ControlContext &ctx) {
        if (m_recArmBar >= 0 && !m_recording) {
            m_recArmBar = -1;
            ctx.notice(QStringLiteral("Recording called off"));
            return;
        }
        if (m_recording) {
            m_recStopBar = nextBar();
            ctx.notice(QStringLiteral("Recording ends on the bar"));
            return;
        }
        m_recArmBar = nextBar();
        m_recBarDone = m_recArmBar - 1;
        if (!m_playing) {
            // A new take: the old song is kept aside, the new one starts on the bar.
            m_prevSong = m_song;
            m_song = Song{};
            m_sections.clear();
            m_origin = m_recArmBar;
            const StudioView &v = ctx.studio();
            m_song.bpm = v.bpm;
            m_song.root = v.root;
            m_song.family = v.family;
            m_song.brightness = v.brightness;
            m_moves.erase(std::remove_if(m_moves.begin(), m_moves.end(),
                                         [&](const Move &m) { return m.b0 >= m_recArmBar * double(kBeatsPerBar); }),
                          m_moves.end());
        }
        if (m_recArmBar <= m_plannedBar)
            planBar(m_recArmBar, ctx, true);
        ctx.notice(m_playing ? QStringLiteral("Punching in on the bar") : QStringLiteral("Recording from the next bar"));
    }

    void togglePlay(bool loop, ControlContext &ctx) {
        if (m_playing) {
            if (m_recording)
                m_recStopBar = nextBar();
            stopSong();
            ctx.notice(QStringLiteral("Song stopped; the clips play on"));
            return;
        }
        if (m_song.empty()) {
            ctx.notice(QStringLiteral("No song yet: Space records one, I writes one from your loops"));
            return;
        }
        startSong(nextBar(), loop, ctx);
        ctx.notice(loop ? QStringLiteral("Song on a loop from the next bar") : QStringLiteral("Song from the top on the bar"));
    }

    void startSong(int bar, bool loop, ControlContext &ctx) {
        m_playing = true;
        m_loop = loop;
        m_origin = bar;
        m_jumpBar = bar;
        m_moveCursorOrigin = -1;
        for (int p = 0; p < P; ++p) {
            std::fill(std::begin(m_trim[p]), std::end(m_trim[p]), 0.0f);
            m_muteFlip[p] = false;
        }
        // Live moves give way to the song's.
        for (auto &m : m_moves)
            m.b1 = std::min(m.b1, bar * double(kBeatsPerBar));
        m_moves.erase(std::remove_if(m_moves.begin(), m_moves.end(),
                                     [&](const Move &m) { return m.b0 >= bar * double(kBeatsPerBar); }),
                      m_moves.end());
        if (std::fabs(ctx.studio().bpm - m_song.bpm) > 0.5)
            ctx.setTempo(m_song.bpm);
        if (bar <= m_plannedBar)
            planBar(bar, ctx, true);
    }

    void clearSong(ControlContext &ctx) {
        m_prevSong = m_song;
        m_song = Song{};
        m_sections.clear();
        if (m_playing)
            stopSong();
        m_recording = false;
        m_recArmBar = m_recStopBar = -1;
        ctx.notice(QStringLiteral("Song cleared"));
    }

    void jumpSection(int dir, ControlContext &ctx) {
        if (!m_playing || m_sections.empty())
            return;
        const int bar = nextBar();
        const int sb = bar - 1 - m_origin;
        int cur = 0;
        for (int i = 0; i < int(m_sections.size()); ++i)
            if (m_sections[size_t(i)].bar0 <= sb)
                cur = i;
        const int to = std::clamp(cur + dir, 0, int(m_sections.size()) - 1);
        // The jump lands on the bar: the song's top moves so that the target
        // section starts there.
        m_origin = bar - m_sections[size_t(to)].bar0;
        m_jumpBar = bar;
        m_moveCursorOrigin = -1;
        for (auto &m : m_moves)
            m.b1 = std::min(m.b1, bar * double(kBeatsPerBar));
        if (bar <= m_plannedBar)
            planBar(bar, ctx, true);
        ctx.notice(QStringLiteral("%1 on the bar").arg(QString::fromUtf8(labelName(m_sections[size_t(to)].label))));
    }

    // Names the song's sections from what plays in them and the moves made:
    // intro, groove, build, drop, break, outro.
    void labelSong() {
        const int n = m_song.length();
        if (n == 0) {
            m_sections.clear();
            return;
        }
        const int drums = role(RDrums), bass = role(RBass);
        const size_t un = static_cast<size_t>(n);
        std::vector<int> energy(un), flags(un);
        std::vector<uint8_t> moveAt(un, 255), inBuild(un), inBreak(un), echo(un);
        for (const auto &sm : m_song.moves) {
            const int b = sm.step / kSteps;
            if (b < 0 || b >= n)
                continue;
            moveAt[size_t(b)] = sm.kind;
            if (sm.kind == MBuild)
                for (int k = b; k < std::min(n, b + std::max(1, sm.param)); ++k)
                    inBuild[size_t(k)] = 1;
            if (sm.kind == MEchoOut)
                for (int k = b; k < std::min(n, b + 1); ++k)
                    echo[size_t(k)] = 1;
        }
        bool breaking = false;
        for (int b = 0; b < n; ++b) {
            if (moveAt[size_t(b)] == MBreak)
                breaking = true;
            if (moveAt[size_t(b)] == MDrop || moveAt[size_t(b)] == MUnbreak ||
                (b > 0 && inBuild[size_t(b - 1)] && !inBuild[size_t(b)]))
                breaking = false;
            inBreak[size_t(b)] = breaking;
            const SongBar &sb = m_song.bars[size_t(b)];
            const Step &st = m_song.steps[size_t(b) * kSteps + 4];
            int e = 0;
            bool d = false, ba = false;
            for (int p = 0; p < m_parts; ++p)
                if (sb.slot[p] >= 0 && st.ch[p][Mute] < 128) {
                    ++e;
                    d |= p == drums;
                    ba |= p == bass;
                }
            energy[size_t(b)] = e;
            flags[size_t(b)] = (d ? 1 : 0) | (ba ? 2 : 0);
        }
        const int maxE = *std::max_element(energy.begin(), energy.end());
        // The intro lasts as long as the first bar's parts do.
        bool intro = true;
        bool hadDrop = false;
        int lastDrop = -1;
        for (int b = 0; b < n; ++b) {
            for (int p = 0; p < m_parts && b > 0; ++p)
                intro = intro && (m_song.bars[size_t(b)].slot[p] >= 0) == (m_song.bars[0].slot[p] >= 0);
            int l = LGroove;
            const bool full = (flags[size_t(b)] & 3) == 3 && energy[size_t(b)] >= maxE - 1;
            if (energy[size_t(b)] == 0)
                l = LEnd;
            else if (echo[size_t(b)])
                l = LOutro;
            else if (inBuild[size_t(b)])
                l = LBuild;
            else if (inBreak[size_t(b)] || (hadDrop && !(flags[size_t(b)] & 1)))
                l = LBreak;
            else if (moveAt[size_t(b)] == MDrop || (b > 0 && inBuild[size_t(b - 1)]) || (full && hadDrop))
                l = LDrop;
            else if (!hadDrop && intro)
                l = LIntro;
            // A drop goes on until something else happens.
            if (l == LGroove && b > 0 && m_song.bars[size_t(b - 1)].label == LDrop && energy[size_t(b)] >= maxE - 2)
                l = LDrop;
            if (l == LDrop) {
                hadDrop = true;
                lastDrop = b;
            }
            m_song.bars[size_t(b)].label = uint8_t(l);
        }
        // After the last drop, what is left is the outro.
        if (lastDrop >= 0)
            for (int b = lastDrop + 1; b < n; ++b) {
                auto &l = m_song.bars[size_t(b)].label;
                if (l == LGroove || l == LIntro)
                    l = LOutro;
            }
        sections();
    }
    void sections() {
        m_sections.clear();
        for (int b = 0; b < m_song.length(); ++b) {
            const int l = m_song.bars[size_t(b)].label;
            // A section breaks where the label changes, and long drops at
            // eight bars, so the timeline reads in phrases.
            if (m_sections.empty() || m_sections.back().label != l ||
                (l == LDrop && m_song.bars[size_t(b)].relaunch && m_sections.back().bars >= 8))
                m_sections.push_back({b, 1, l});
            else
                ++m_sections.back().bars;
        }
    }

    // Writes a whole track from whatever loops the parts hold: intro,
    // groove, build, drop, breakdown, build, drop, outro, with the moves a
    // producer would make, and plays it from the next bar.
    void autoArrange(bool shortForm, ControlContext &ctx) {
        const StudioView &v = ctx.studio();
        int filled = 0;
        for (int p = 0; p < m_parts; ++p)
            filled += hasClips(v, p);
        if (!filled) {
            ctx.notice(QStringLiteral("No loops yet: make some on pages 1–7 first"));
            return;
        }
        auto has = [&](Role r) { return role(r) >= 0 && hasClips(v, role(r)); };
        auto slotA = [&](Role r) {
            const int p = role(r);
            if (p < 0 || !has(r))
                return -1;
            return m_lastSlot[p] >= 0 && v.parts[p].clips[m_lastSlot[p]].state != ClipView::Empty ? m_lastSlot[p]
                                                                                                 : firstSlot(v, p);
        };
        auto slotB = [&](Role r) {
            const int a = slotA(r);
            if (a < 0)
                return -1;
            const int b = firstSlot(v, role(r), a);
            return b >= 0 ? b : a;
        };
        m_prevSong = m_song;
        Song song;
        song.bpm = v.bpm;
        song.root = v.root;
        song.family = v.family;
        song.brightness = v.brightness;

        struct Plan {
            int label, bars;
            std::vector<Role> roles, late; // late: they join half way
            bool second = false;           // the second clips, for variety
            int move = -1;
        };
        const bool pads = has(RChords) || has(RAtmos);
        const int u = shortForm ? 1 : 2; // bars per unit of four
        std::vector<Role> intro = {RAtmos, RDrums};
        if (!has(RAtmos))
            intro.push_back(RChords);
        std::vector<Plan> plan;
        plan.push_back({LIntro, 4 * u, intro, {RArp}});
        if (!shortForm)
            plan.push_back({LGroove, 8, {RDrums, RBass, RAtmos}, {RArp}});
        plan.push_back({LBuild, 4, {RDrums, RBass, RChords, RArp, RAtmos}, {}, false, MBuild});
        plan.push_back({LDrop, 8 * u, {RDrums, RBass, RChords, RLead, RArp, RAtmos}, {}});
        if (pads)
            plan.push_back({LBreak, 4 * u, {RChords, RAtmos}, {RLead}, false, MBreak});
        else
            plan.push_back({LBreak, 4 * u, {RLead, RArp}, {}, false, MBreak});
        plan.push_back({LBuild, 4, {RDrums, RBass, RChords, RArp, RAtmos, RLead}, {}, false, MBuild});
        plan.push_back({LDrop, 8 * u, {RDrums, RBass, RChords, RLead, RArp, RAtmos}, {}, true});
        plan.push_back({LOutro, 4 * u, {RDrums, RAtmos, RChords, RBass}, {}, false, MEchoOut});
        plan.push_back({LEnd, 2, {}, {}});

        Step base;
        for (int p = 0; p < P; ++p)
            for (int c = 0; c < ChannelCount; ++c)
                base.ch[p][c] = q8(c == Mute || c == Solo ? 0.0f : m_home[p][c]);
        int bar = 0;
        for (const Plan &sec : plan) {
            for (int k = 0; k < sec.bars; ++k, ++bar) {
                SongBar sb;
                sb.label = uint8_t(sec.label);
                auto add = [&](Role r, bool second) {
                    if (has(r))
                        sb.slot[role(r)] = int8_t(second ? slotB(r) : slotA(r));
                };
                for (Role r : sec.roles)
                    add(r, sec.second);
                if (k >= sec.bars / 2)
                    for (Role r : sec.late)
                        add(r, sec.second);
                // The second half of a long drop changes clothes.
                if (sec.label == LDrop && !sec.second && k >= 8)
                    for (Role r : sec.roles)
                        add(r, true);
                // The outro thins out: the bass and the chords leave half way.
                if (sec.label == LOutro && k >= sec.bars / 2) {
                    if (has(RBass))
                        sb.slot[role(RBass)] = -1;
                    if (has(RChords) && has(RAtmos))
                        sb.slot[role(RChords)] = -1;
                }
                // Every section, and the second half of a long drop, starts
                // every loop from its top.
                if (k == 0 || (sec.label == LDrop && k == 8))
                    for (int p = 0; p < m_parts; ++p)
                        if (sb.slot[p] >= 0)
                            sb.relaunch |= uint8_t(1 << p);
                song.bars.push_back(sb);
                for (int s = 0; s < kSteps; ++s) {
                    Step st = base;
                    const float prog = float(k * kSteps + s) / float(sec.bars * kSteps);
                    // Intro: the drums come up out of a high-pass.
                    if (sec.label == LIntro && has(RDrums)) {
                        const int d = role(RDrums);
                        st.ch[d][Filter] = q8(m_home[d][Filter] + 0.2f * (1 - prog));
                    }
                    // Break: the chords open slowly from a low-pass.
                    if (sec.label == LBreak && has(RChords)) {
                        const int c = role(RChords);
                        st.ch[c][Filter] = q8(m_home[c][Filter] - 0.17f * (1 - prog));
                    }
                    // Drops pump harder: everything but the drums breathes with the kick.
                    if (sec.label == LDrop)
                        for (int p = 0; p < m_parts; ++p)
                            if (p != role(RDrums) && p != role(RBass))
                                st.ch[p][PumpDepth] = q8(m_home[p][PumpDepth] + 0.12f);
                    // Drops: the lead throws its last beat into the echo every four bars.
                    if (sec.label == LDrop && has(RLead) && k % 4 == 3 && s >= 12) {
                        const int l = role(RLead);
                        st.ch[l][EchoSend] = q8(m_home[l][EchoSend] + 0.55f);
                    }
                    song.steps.push_back(st);
                }
            }
            const int start = bar - sec.bars;
            if (sec.move == MBuild)
                song.moves.push_back({start * kSteps, MBuild, sec.bars});
            if (sec.move == MBreak) {
                int mask = 0;
                for (Role r : {RDrums, RBass, RArp})
                    if (role(r) >= 0)
                        mask |= 1 << role(r);
                if (!pads)
                    mask &= ~(role(RArp) >= 0 ? 1 << role(RArp) : 0);
                song.moves.push_back({start * kSteps, MBreak, mask});
            }
            if (sec.move == MEchoOut)
                song.moves.push_back({(bar - 1) * kSteps, MEchoOut, 0});
        }
        m_song = std::move(song);
        m_recording = false;
        m_recArmBar = m_recStopBar = -1;
        sections();
        startSong(nextBar(), false, ctx);
        ctx.notice(QStringLiteral("A track from your loops: %1 bars, from the next bar").arg(m_song.length()));
    }

    // --- drawing helpers --------------------------------------------------------

    void paintLanes(QPainter &p, const PaintContext &pc, int parts);
    void paintTop(QPainter &p, const PaintContext &pc, int parts);
    void paintTension(QPainter &p, const PaintContext &pc);
    void paintSong(QPainter &p, const PaintContext &pc);
    void paintHeader(QPainter &p, const PaintContext &pc);
    void paintMoments(QPainter &p, const PaintContext &pc);
    QString fingerLabel(const Finger &f) const;
    QString movesText() const;
    QString songText() const;
    QColor labelColor(int l, QColor gold) const;

    // --- state ----------------------------------------------------------------

    QStringList m_names;
    std::vector<QColor> m_colors;
    int m_role[RoleCount] = {};
    int m_parts = 0;
    bool m_ready = false;

    std::array<Finger, 16> m_fingers{};
    Frame m_frame;
    bool m_button = false;
    Ride m_ride[P];
    bool m_heldSolo[P] = {};
    float m_tensionLive = 0, m_tensionShown = 0, m_buildShown = 0, m_flatShown = 0;
    bool m_tensionHeld = false;

    float m_home[P][ChannelCount] = {};
    float m_trim[P][ChannelCount] = {};
    bool m_muteFlip[P] = {};
    int m_muteAtBar[P] = {-1, -1, -1, -1, -1, -1, -1, -1};
    float m_sent[P][ChannelCount] = {};
    double m_sentAt[P][ChannelCount] = {};
    float m_masterHome[MasterCount] = {}, m_masterSent[MasterCount] = {};
    double m_masterSentAt[MasterCount] = {};
    struct Shown {
        float filt = 0.5f, echo = 0, verb = 0, pump = 0, gain = 0.8f, pan = 0.5f;
    } m_shown[P];

    std::vector<Move> m_moves;
    double m_lastT = 0, m_tickDt = 0.008, m_beat = 0, m_lastBeat = -1e9, m_look = 0;
    int m_plannedBar = -1000000, m_lateBar = -1000000;
    Posted m_posted[P];
    int m_lastSlot[P] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int m_relaunchAt[P] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int m_lastPlayedBar[P] = {-1000000, -1000000, -1000000, -1000000, -1000000, -1000000, -1000000, -1000000};

    float m_echoShown = -1; // the echo time a move or the fader holds, for drawing

    struct Scene {
        bool stored = false;
        int8_t slot[P] = {};
    } m_scene[kClipSlots];
    int m_sceneQueued = -1, m_sceneQueuedBar = -1;
    bool m_anyClip = false;
    int m_selPart = -1, m_selSlot = -1; // the clip last touched: Delete clears it

    Song m_song, m_prevSong;
    std::vector<Section> m_sections;
    bool m_playing = false, m_loop = false, m_recording = false;
    int m_origin = 0, m_jumpBar = -1;
    int m_recArmBar = -1, m_recStopBar = -1, m_recStartBar = 0, m_recBarDone = -1000000;
    long m_recStep = -1;
    size_t m_moveCursor = 0;
    int m_moveCursorOrigin = -1;
};

// --- drawing ----------------------------------------------------------------------

namespace c = console;

QColor ArrangeControl::labelColor(int l, QColor gold) const {
    switch (l) {
    case LIntro: return c::cyan();
    case LGroove: return c::amber();
    case LBuild: return c::orange();
    case LDrop: return gold;
    case LBreak: return c::violet();
    case LOutro: return c::cyan();
    default: return c::dim();
    }
}

void ArrangeControl::paint(QPainter &p, const PaintContext &pc) {
    const StudioView &v = *pc.studio;
    const int parts = std::clamp(v.partCount, 1, P);
    c::pad(p, pc, QStringLiteral("Arrange"));

    // The build rises from the floor of the pad, breathing on the beat.
    const float tens = m_buildShown;
    if (tens > 0.01f) {
        const float pulse = 0.7f + 0.3f * float(std::cos(fract(pc.beat) * 6.2832));
        QLinearGradient g(pc.at(0, 0), pc.at(0, kTopY));
        g.setColorAt(0, c::alpha(pc.color, 0.26f * tens * pulse));
        g.setColorAt(std::min(1.0, 0.1 + 0.9 * double(tens)), c::alpha(pc.color, 0.0f));
        p.fillRect(pc.rect(0, 0, kStripR, kTopY), g);
    }
    if (m_tensionShown < -0.01f) {
        QLinearGradient g(pc.at(0, kTopY), pc.at(0, 0));
        g.setColorAt(0, c::alpha(c::cyan(), 0.22f * -m_tensionShown));
        g.setColorAt(std::min(1.0, 0.1 - 0.9 * double(m_tensionShown)), c::alpha(c::cyan(), 0.0f));
        p.fillRect(pc.rect(0, 0, kStripR, kTopY), g);
    }

    paintLanes(p, pc, parts);
    paintTop(p, pc, parts);
    paintTension(p, pc);
    paintMoments(p, pc);

    for (int i = 0; i < m_frame.count; ++i) {
        const Touch &t = m_frame.touches[i];
        const Finger *f = nullptr;
        for (const auto &fg : m_fingers)
            if (fg.id == t.id)
                f = &fg;
        QColor col = pc.color;
        if (f && f->lane >= 0 && (f->kind == Finger::Lane || f->kind == Finger::Send || f->kind == Finger::Name))
            col = laneColor(f->lane, pc.color);
        const QString label = f ? fingerLabel(*f) : QString();
        const float glow = f && f->kind == Finger::Tension ? std::fabs(m_tensionShown) : 0.0f;
        if (t.x < 0.72f) {
            c::finger(p, pc, t, col, label, glow);
        } else {
            // Near the right edge the label goes to the finger's left, inside the pad.
            c::finger(p, pc, t, col, QString(), glow);
            if (!label.isEmpty()) {
                const QFont fnt = c::font(12, true);
                const qreal w = QFontMetricsF(fnt).horizontalAdvance(label) + 12;
                const QPointF at = pc.at(t.x, t.y);
                const qreal r = pc.pad.height() / 10.0 * 0.32 * std::clamp(t.size, 0.6f, 4.0f);
                c::tag(p, QPointF(at.x() - r - 6 - w, at.y() - r * 0.6), label, col, 12);
            }
        }
    }
    paintHeader(p, pc);
    c::beats(p, pc, pc.color);
    paintSong(p, pc);
}

void ArrangeControl::paintLanes(QPainter &p, const PaintContext &pc, int parts) {
    const StudioView &v = *pc.studio;
    bool anySolo = false;
    for (int i = 0; i < parts; ++i)
        anySolo |= v.parts[i].channel.value[Solo] > 0.5f;
    const bool blink = std::fmod(pc.time, 0.5) < 0.25;
    const int next = int(std::floor(pc.beat / kBeatsPerBar)) + 1;
    for (int i = 0; i < parts; ++i) {
        const float y1 = kTopY * (1.0f - float(i) / parts), y0 = kTopY * (1.0f - float(i + 1) / parts);
        const QColor col = laneColor(i, pc.color);
        const PartView &pv = v.parts[i];
        const bool muted = pv.channel.value[Mute] > 0.5f;
        const bool soloed = pv.channel.value[Solo] > 0.5f;
        const bool audible = !muted && (!anySolo || soloed);
        const bool playing = pv.playing >= 0;

        // The name: number, name, state, and the level it is making.
        const QRectF name = pc.rect(0, y0, kNameW, y1).adjusted(8, 5, -6, -5);
        if (playing && audible)
            p.fillRect(pc.rect(0, y0, kNameW, y1).adjusted(2, 2, -1, -2), c::alpha(col, 0.07f + 0.25f * std::min(1.0f, pv.channel.rms * 5)));
        c::text(p, QRectF(name.left(), name.top(), name.width(), name.height() * 0.4),
                QStringLiteral("%1 %2").arg(i + 1).arg(m_names.value(i)), audible ? col : c::alpha(col, 0.4f), 13, true);
        qreal tx = name.left();
        const qreal ty = name.top() + name.height() * 0.58;
        if (muted)
            tx = c::tag(p, QPointF(tx, ty), QStringLiteral("MUTE"), c::red(), 10).right() + 4;
        if (soloed)
            tx = c::tag(p, QPointF(tx, ty), QStringLiteral("SOLO"), pc.color, 10).right() + 4;
        if (!muted && !soloed) {
            // Where its clips come from (its page), and its mute and solo keys.
            static const char *keys[] = {"Q A", "W S", "E D", "R F", "T G", "Y H", "U J"};
            c::text(p, QRectF(tx, ty - 7, name.width(), 14),
                    QStringLiteral("PAGE %1  %2").arg(i + 1).arg(QString::fromLatin1(i < 7 ? keys[i] : "")),
                    c::alpha(col, 0.4f), 9);
        }
        c::gauge(p, QRectF(name.left(), name.bottom() - 6, name.width(), 5),
                 std::min(1.0f, pv.channel.peak * 1.4f), audible ? col : c::alpha(col, 0.4f), 12);

        // The clips.
        for (int s = 0; s < kClipSlots; ++s) {
            const float x0 = kNameW + (kCellsR - kNameW) * float(s) / kClipSlots;
            const float x1 = kNameW + (kCellsR - kNameW) * float(s + 1) / kClipSlots;
            const QRectF cell = pc.rect(x0, y0, x1, y1).adjusted(3, 5, -3, -5);
            const ClipView &cv = pv.clips[s];
            const int px = int(std::clamp(cell.height() * 0.2, 9.0, 12.0));
            if (cv.state == ClipView::Empty) {
                // Empty, and honest about it.
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(c::alpha(col, 0.18f), 1, Qt::DotLine));
                p.drawRect(cell);
                if (cell.width() > 44)
                    c::text(p, cell, QStringLiteral("EMPTY"), c::alpha(col, 0.2f), 9, false, Qt::AlignCenter);
                if (i == m_selPart && s == m_selSlot) {
                    p.setPen(QPen(QColor(255, 255, 255, 140), 1, Qt::DashLine));
                    p.drawRect(cell.adjusted(-2, -2, 2, 2));
                }
                continue;
            }
            const QString len = cv.lengthBeats >= 4 ? QStringLiteral("%1 BAR").arg(int(cv.lengthBeats / 4))
                                                    : QStringLiteral("%1/4").arg(int(cv.lengthBeats));
            QColor ink = col;
            switch (cv.state) {
            case ClipView::Playing:
            case ClipView::Stopping: {
                p.fillRect(cell, c::alpha(col, audible ? 0.2f : 0.08f));
                const QRectF hand(cell.left(), cell.top(), cell.width() * cv.phase, cell.height());
                p.fillRect(hand, c::alpha(col, cv.state == ClipView::Stopping && blink ? 0.15f : audible ? 0.55f : 0.2f));
                // The loop's bars, ticked.
                const int bars = std::max(1, int(cv.lengthBeats / 4));
                p.setPen(QPen(QColor(0, 0, 0, 120), 1));
                for (int b = 1; b < bars && bars <= 8; ++b) {
                    const qreal x = cell.left() + cell.width() * b / bars;
                    p.drawLine(QPointF(x, cell.top() + 2), QPointF(x, cell.bottom() - 2));
                }
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(col, 2));
                p.drawRect(cell);
                ink = audible ? QColor::fromRgb(c::kInk) : col;
                break;
            }
            case ClipView::Queued:
                p.fillRect(cell, c::alpha(col, blink ? 0.45f : 0.12f));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(col, 1.5));
                p.drawRect(cell);
                break;
            case ClipView::Recording:
                p.fillRect(cell, c::alpha(c::red(), blink ? 0.6f : 0.3f));
                ink = c::red();
                break;
            default:
                p.fillRect(cell, c::alpha(col, 0.05f));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(c::alpha(col, 0.6f), 1.2));
                p.drawRect(cell);
                break;
            }
            // What the slot holds and what it is doing: its number, its
            // state in words, its length.
            QString state = QStringLiteral("READY");
            switch (cv.state) {
            case ClipView::Playing: state = QStringLiteral("PLAY"); break;
            case ClipView::Queued: state = QStringLiteral("NEXT BAR"); break;
            case ClipView::Stopping: state = QStringLiteral("LAST BAR"); break;
            case ClipView::Recording: state = QStringLiteral("REC"); break;
            default: break;
            }
            const int small = std::max(8, px - 2);
            // Over a moving sweep, labels sit on black so they always read.
            const bool boxed = cv.state == ClipView::Playing || cv.state == ClipView::Stopping;
            if (boxed)
                ink = col;
            auto label = [&](const QString &t, int size, Qt::Alignment align, qreal dx, qreal dy) {
                const QFont f = c::font(size, true);
                const qreal w = QFontMetricsF(f).horizontalAdvance(t) + 6, h = size + 4;
                const qreal x = (align & Qt::AlignRight) ? cell.right() - dx - w : cell.left() + dx;
                const qreal y = (align & Qt::AlignBottom) ? cell.bottom() - dy - h : cell.top() + dy;
                const QRectF r(x, y, w, h);
                if (boxed)
                    p.fillRect(r, QColor(0, 0, 0, 200));
                c::text(p, r, t, ink, size, true, Qt::AlignCenter);
            };
            label(QString::number(s + 1), px, Qt::AlignLeft | Qt::AlignTop, 2, 2);
            label(state, small, Qt::AlignRight | Qt::AlignTop, 2, 2);
            label(len, px, Qt::AlignRight | Qt::AlignBottom, 2, 2);
            if (i == m_selPart && s == m_selSlot) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(255, 255, 255, 170), 1, Qt::DashLine));
                p.drawRect(cell.adjusted(-2, -2, 2, 2));
            }
            // Where the plan sends this part on the next bar.
            if (m_posted[i].bar == next && m_posted[i].slot == s && cv.state != ClipView::Queued && m_posted[i].relaunch)
                c::text(p, cell.adjusted(2, 2, -4, -3), QStringLiteral("↺"), ink, px + 2, true,
                        Qt::AlignRight | Qt::AlignTop);
        }

        // The lane's mix: level, filter, echo, reverb.
        const QRectF ro = pc.rect(kCellsR, y0, kStripR, y1).adjusted(8, 6, -6, -6);
        const Shown &sh = m_shown[i];
        const char *labels[] = {"LVL", "FLT", "ECH", "VRB"};
        const qreal rowH = ro.height() / 4;
        const qreal lw = 26;
        for (int r = 0; r < 4; ++r) {
            const QRectF row(ro.left(), ro.top() + r * rowH, ro.width(), rowH);
            c::text(p, QRectF(row.left(), row.top(), lw, row.height()), QString::fromLatin1(labels[r]),
                    c::alpha(col, 0.55f), 9);
            const QRectF g(row.left() + lw, row.center().y() - 3, row.width() - lw, 6);
            const bool ridden = m_ride[i].held;
            const QColor gc = ridden ? col : c::alpha(col, 0.75f);
            switch (r) {
            case 0: c::gauge(p, g, sh.gain, gc, 10); break;
            case 1: c::gauge(p, g, sh.filt, std::fabs(sh.filt - 0.5f) > 0.01f ? col : c::alpha(col, 0.4f), 10, true); break;
            case 2: c::gauge(p, g, sh.echo, gc, 10); break;
            default: c::gauge(p, g, sh.verb, gc, 10); break;
            }
        }
        if (!audible)
            p.fillRect(pc.rect(kNameW, y0, kStripR, y1), QColor(0, 0, 0, 110));
    }
    // No loops anywhere: say plainly where they come from.
    bool anyClip = false;
    for (int i = 0; i < parts; ++i)
        for (const auto &cv : v.parts[i].clips)
            anyClip = anyClip || cv.state != ClipView::Empty;
    if (!anyClip) {
        const QRectF box = pc.rect(kNameW + 0.04f, kTopY * 0.3f, kCellsR - 0.04f, kTopY * 0.7f);
        p.fillRect(box, QColor(0, 0, 0, 225));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pc.color, 1.5));
        p.drawRect(box);
        c::text(p, box.adjusted(16, 10, -16, -box.height() * 0.62), QStringLiteral("NO LOOPS YET"), pc.color, 18, true);
        const QFont f = c::font(12, false);
        p.setFont(f);
        p.setPen(c::alpha(pc.color, 0.85f));
        p.drawText(box.adjusted(16, box.height() * 0.38, -16, -10), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   QStringLiteral("Loops are made on the part pages, keys 1 to 7: play, press Space to record "
                                  "from the next bar, and Space again to close the loop. Each appears here in its "
                                  "page's row, in the slot it went into, with its length and whether it plays."));
    }
    p.setPen(QPen(c::alpha(pc.color, 0.18f), 1));
    for (int i = 1; i < parts; ++i) {
        const QPointF a = pc.at(0, kTopY * (1.0f - float(i) / parts)), b = pc.at(kStripR, kTopY * (1.0f - float(i) / parts));
        p.drawLine(a, b);
    }
    for (float x : {kNameW, kCellsR, kStripR})
        p.drawLine(pc.at(x, 0), pc.at(x, 1));
    p.drawLine(pc.at(0, kTopY), pc.at(1, kTopY));
}

void ArrangeControl::paintTop(QPainter &p, const PaintContext &pc, int parts) {
    const StudioView &v = *pc.studio;
    const bool blink = std::fmod(pc.time, 0.5) < 0.25;
    const int next = int(std::floor(pc.beat / kBeatsPerBar)) + 1;

    // BUILD.
    {
        const QRectF r = pc.rect(0, kTopY, kNameW, 1).adjusted(6, 14, -4, -5);
        const Move *b = nullptr;
        for (const auto &m : m_moves)
            if (m.kind == MBuild && m.flat < 0 && pc.beat < m.b1)
                b = &m;
        c::button(p, r, QStringLiteral("BUILD"), c::orange(), b != nullptr);
        QString sub = QStringLiteral("B");
        if (b) {
            const double left = b->b1 - pc.beat;
            sub = left > 4 ? QStringLiteral("DROP IN %1").arg(int(std::ceil(left / 4))) + QStringLiteral(" BARS")
                           : QStringLiteral("DROP IN %1").arg(int(std::ceil(left)));
        }
        c::text(p, r.adjusted(6, 22, -4, -2), sub, c::alpha(c::orange(), b ? 1.0f : 0.5f), 11, true);
    }
    // The scenes: what each would launch, part by part.
    int8_t sl[P];
    for (int s = 0; s < kClipSlots; ++s) {
        const float x0 = kNameW + (kCellsR - kNameW) * float(s) / kClipSlots;
        const float x1 = kNameW + (kCellsR - kNameW) * float(s + 1) / kClipSlots;
        const QRectF r = pc.rect(x0, kTopY, x1, 1).adjusted(3, 14, -3, -5);
        sceneSlots(s, v, sl);
        bool any = false, match = true;
        for (int i = 0; i < parts; ++i) {
            any |= sl[i] >= 0;
            if (sl[i] >= 0)
                match = match && v.parts[i].playing == sl[i];
            else if (sl[i] == -1)
                match = match && v.parts[i].playing < 0;
        }
        match = match && any;
        const bool queued = m_sceneQueued == s && m_sceneQueuedBar >= next;
        QString title = QStringLiteral("F%1").arg(s + 1);
        if (m_scene[s].stored)
            title += QStringLiteral(" ★");
        c::button(p, r, title, any ? pc.color : c::alpha(pc.color, 0.35f), match || (queued && blink));
        const qreal sq = std::clamp((r.width() - 12) / parts - 3, 4.0, 9.0);
        const qreal top = r.top() + 26;
        for (int i = 0; i < parts; ++i) {
            const QRectF d(r.left() + 6 + i * (sq + 3), top, sq, sq);
            const QColor col = laneColor(i, pc.color);
            if (sl[i] >= 0)
                p.fillRect(d, col);
            else if (sl[i] == -1) {
                p.setPen(QPen(c::alpha(col, 0.6f), 1));
                p.drawLine(d.topLeft(), d.bottomRight());
                p.drawLine(d.topRight(), d.bottomLeft());
            } else {
                p.fillRect(QRectF(d.left(), d.center().y() - 0.75, d.width(), 1.5), c::alpha(col, 0.3f));
            }
        }
    }
    // BREAK and DROP.
    {
        const QRectF r = pc.rect(kCellsR, kTopY, kStripR, 1).adjusted(6, 14, -4, -5);
        const Move *b = nullptr;
        for (const auto &m : m_moves)
            if (m.kind == MBreak && pc.beat < m.b1)
                b = &m;
        const bool on = b && pc.beat >= b->b0;
        c::button(p, r, QStringLiteral("BREAK"), c::violet(), on || (b && blink));
        c::text(p, r.adjusted(6, 22, -4, -2), on ? QStringLiteral("STRIPPED") : QStringLiteral("N"),
                c::alpha(c::violet(), on ? 1.0f : 0.5f), 11, true);
    }
    {
        const QRectF r = pc.rect(kStripR, kTopY, 1, 1).adjusted(6, 14, -6, -5);
        const Move *d = nullptr;
        for (const auto &m : m_moves)
            if (m.kind == MDrop && pc.beat < m.b0)
                d = &m;
        c::button(p, r, QStringLiteral("DROP"), pc.color, d && blink);
        QString sub = QStringLiteral("RETURN");
        if (d) {
            const double left = d->b0 - pc.beat;
            sub = left > 4 ? QStringLiteral("IN %1 BARS").arg(int(std::ceil(left / 4))) : QStringLiteral("IN %1").arg(int(std::ceil(left)));
        }
        c::text(p, r.adjusted(6, 22, -4, -2), sub, c::alpha(pc.color, d ? 1.0f : 0.5f), 11, true);
    }
}

void ArrangeControl::paintTension(QPainter &p, const PaintContext &pc) {
    const QRectF r = pc.rect(kStripR, 0, 1, kTopY).adjusted(10, 8, -8, -8);
    const float t = std::clamp(std::max(m_tensionShown, m_buildShown), -1.0f, 1.0f);
    const float tt = m_tensionShown < 0 ? m_tensionShown : t;
    const qreal mid = r.center().y();
    // Ticks, like a fader's scale.
    for (int k = 0; k <= 20; ++k) {
        const qreal y = r.top() + r.height() * k / 20;
        const qreal w = k % 5 == 0 ? r.width() * 0.3 : r.width() * 0.15;
        p.setPen(QPen(c::alpha(pc.color, k == 10 ? 0.6f : 0.22f), 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.left() + w, y));
        p.drawLine(QPointF(r.right() - w, y), QPointF(r.right(), y));
    }
    if (std::fabs(tt) > 0.005f) {
        const qreal h = r.height() / 2 * std::fabs(tt);
        const QRectF fill = tt > 0 ? QRectF(r.left() + 6, mid - h, r.width() - 12, h)
                                   : QRectF(r.left() + 6, mid, r.width() - 12, h);
        QLinearGradient g(fill.topLeft(), fill.bottomLeft());
        const QColor col = tt > 0 ? pc.color : c::cyan();
        g.setColorAt(tt > 0 ? 1 : 0, c::alpha(col, 0.25f));
        g.setColorAt(tt > 0 ? 0 : 1, c::alpha(col, 0.85f));
        p.fillRect(fill, g);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c::alpha(pc.color, 0.5f), 1.5));
    p.drawRect(r);
    c::text(p, QRectF(r.left(), r.top() + 4, r.width(), 16), QStringLiteral("BUILD"), c::alpha(pc.color, 0.7f), 10,
            true, Qt::AlignCenter);
    c::text(p, QRectF(r.left(), r.bottom() - 20, r.width(), 16), QStringLiteral("SINK"), c::alpha(c::cyan(), 0.7f), 10,
            true, Qt::AlignCenter);
    if (std::fabs(tt) > 0.02f)
        c::text(p, QRectF(r.left(), mid - (tt > 0 ? 30 : -6), r.width(), 24),
                QStringLiteral("%1").arg(int(std::lround(std::fabs(tt) * 100))), tt > 0 ? pc.color : c::cyan(), 18,
                true, Qt::AlignCenter);
    else
        c::text(p, QRectF(r.left(), mid - 10, r.width(), 20), QStringLiteral("TENSION"), c::alpha(pc.color, 0.35f), 9,
                true, Qt::AlignCenter);
    // The echo roll: what the tape echo is stepping at.
    if (m_echoShown >= 0) {
        const float flash = float(fract(pc.beat * (m_echoShown < 0.1f ? 4 : m_echoShown < 0.4f ? 2 : 1)) < 0.5);
        const QString what = m_echoShown < 0.1f ? QStringLiteral("ECHO 1/16") : m_echoShown < 0.4f ? QStringLiteral("ECHO 1/8")
                                                                                                 : QStringLiteral("ECHO 1/4");
        c::tag(p, QPointF(r.center().x(), r.top() + 34), what, c::alpha(c::orange(), 0.55f + 0.45f * flash), 10, true);
    }
}

// Big moments drawn over everything: the breath before a drop, the drop
// itself, the echo-out.
void ArrangeControl::paintMoments(QPainter &p, const PaintContext &pc) {
    const QRectF area = pc.rect(0, 0, kStripR, kTopY);
    auto big = [&](const QString &s, QColor col, float a) {
        const QFont f = c::font(int(pc.pad.height() * 0.17), true);
        p.setFont(f);
        p.setPen(c::alpha(col, a));
        p.drawText(area, Qt::AlignCenter, s);
    };
    for (const auto &m : m_moves) {
        const double e = pc.beat - m.b0;
        if (m.kind == MDrop) {
            if (e >= -kGap && e < 0) {
                p.fillRect(area, QColor(0, 0, 0, 150));
                big(QStringLiteral("DROP"), pc.color, 0.35f + 0.3f * float(fract(pc.beat * 4) < 0.5));
            } else if (e >= 0 && e < 2) {
                const float u = float(1 - e / 2);
                p.fillRect(pc.pad, c::alpha(pc.color, 0.32f * u * u));
                big(QStringLiteral("DROP"), pc.color, u);
            }
        } else if (m.kind == MBreak && e >= 0 && e < 2) {
            big(QStringLiteral("BREAK"), c::violet(), float(1 - e / 2) * 0.8f);
        } else if (m.kind == MEchoOut && e >= 4 && e < 12) {
            big(QStringLiteral("ECHO"), c::cyan(), float(1 - (e - 4) / 8) * 0.7f);
        } else if (m.kind == MCut && e >= 0 && pc.beat < m.b1 && !pending(MDrop)) {
            p.fillRect(area, QColor(0, 0, 0, 140));
        } else if (m.kind == MLift && e >= 0 && e < 2) {
            big(QStringLiteral("KEY ▲"), pc.color, float(1 - e / 2) * 0.8f);
        }
    }
}

void ArrangeControl::paintHeader(QPainter &p, const PaintContext &pc) {
    const StudioView &v = *pc.studio;
    Tuning tn;
    tn.set(v.root, v.family, v.brightness, v.just);
    const long bar = long(std::floor(pc.beat / kBeatsPerBar));
    const int beatIn = int(std::floor(pc.beat - bar * kBeatsPerBar)) + 1;
    QString s;
    if (m_playing && !m_song.empty()) {
        s = songText();
    } else if (m_recording) {
        s = QStringLiteral("● REC BAR %1.%2").arg(bar - m_origin + 1).arg(beatIn);
    } else {
        // The phrase: four bars, counted the way a producer counts them.
        s = QStringLiteral("BAR %1.%2  PHRASE %3/4").arg(bar + 1).arg(beatIn).arg(int(((bar % 4) + 4) % 4) + 1);
    }
    s += QStringLiteral("   %1 BPM   %2 %3").arg(int(std::lround(v.bpm))).arg(QString::fromUtf8(noteOf(v.root)),
                                                                             QString::fromUtf8(tn.modeName()));
    const QString mv = movesText();
    if (!mv.isEmpty())
        s += QStringLiteral("   ▸ ") + mv;
    const QFont f = c::font(12, true);
    const qreal w = std::min(QFontMetricsF(f).horizontalAdvance(s) + 16, pc.pad.width() - 260);
    const QRectF r(pc.pad.left() + 120, pc.pad.top() - 9, w, 18);
    p.fillRect(r, QColor(0, 0, 0));
    c::text(p, r.adjusted(8, 0, -4, 0), s, m_recording ? c::red() : pc.color, 12, true);
}

void ArrangeControl::paintSong(QPainter &p, const PaintContext &pc) {
    const QRectF band(pc.pad.left(), pc.pad.bottom() + 10, pc.pad.width(), 30);
    const int n = m_song.length();
    if (n == 0) {
        const QString s = m_recArmBar >= 0 ? QStringLiteral("● RECORDING FROM THE NEXT BAR")
                                           : QStringLiteral("NO SONG YET   ·   SPACE RECORDS ONE AS YOU PLAY   ·   I WRITES ONE FROM YOUR LOOPS");
        c::text(p, band, s, m_recArmBar >= 0 ? c::red() : c::alpha(pc.color, 0.45f), 11, true, Qt::AlignCenter);
        return;
    }
    // A take in progress has room ahead of it on the timeline.
    const int span = m_recording && !m_playing ? std::max(32, (n + 8) / 8 * 8) : std::max(1, n);
    auto xAt = [&](double bar) { return band.left() + band.width() * bar / span; };
    if (m_recording && !m_playing)
        p.fillRect(QRectF(xAt(0), band.top(), xAt(n) - xAt(0), band.height()), c::alpha(c::red(), 0.12f));
    // The shape of the track: how many parts play in each bar.
    {
        QPainterPath path;
        for (int b = 0; b < n; ++b) {
            int e = 0;
            for (int i = 0; i < m_parts; ++i)
                e += m_song.bars[size_t(b)].slot[i] >= 0;
            const qreal y = band.bottom() - band.height() * 0.85 * e / std::max(1, m_parts);
            if (b == 0)
                path.moveTo(xAt(0), y);
            path.lineTo(xAt(b), y);
            path.lineTo(xAt(b + 1), y);
        }
        path.lineTo(xAt(n), band.bottom());
        path.lineTo(xAt(0), band.bottom());
        path.closeSubpath();
        p.fillPath(path, c::alpha(m_recording ? c::red() : pc.color, m_recording ? 0.3f : 0.08f));
    }
    if (!m_sections.empty() && !m_recording) {
        for (const auto &s : m_sections) {
            const QRectF r(xAt(s.bar0) + 1, band.top(), xAt(s.bar0 + s.bars) - xAt(s.bar0) - 2, band.height());
            const QColor col = labelColor(s.label, pc.color);
            p.fillRect(r, c::alpha(col, s.label == LDrop ? 0.3f : 0.16f));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c::alpha(col, 0.7f), 1));
            p.drawRect(r);
            const QString t = QStringLiteral("%1 %2").arg(QString::fromUtf8(labelName(s.label))).arg(s.bars);
            if (r.width() > 34)
                c::text(p, r.adjusted(4, 0, -2, 0), t, col, int(std::clamp(r.width() / 6, 8.0, 11.0)), true);
        }
    } else {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c::alpha(c::red(), 0.6f), 1));
        p.drawRect(band);
    }
    // Four-bar phrase ticks.
    p.setPen(QPen(c::alpha(pc.color, 0.25f), 1));
    for (int b = 4; b < span; b += 4)
        p.drawLine(QPointF(xAt(b), band.bottom()), QPointF(xAt(b), band.bottom() + 3));
    const double pos = pc.beat / kBeatsPerBar - m_origin;
    if ((m_playing || m_recording) && pos >= 0 && pos <= span) {
        const qreal x = xAt(pos);
        const QColor col = m_recording ? c::red() : pc.color;
        p.setPen(QPen(col, 2));
        p.drawLine(QPointF(x, band.top() - 3), QPointF(x, band.bottom() + 3));
        if (m_recording)
            c::tag(p, QPointF(x + 4, band.top() - 4), QStringLiteral("REC"), c::red(), 9);
    }
}

QString ArrangeControl::fingerLabel(const Finger &f) const {
    switch (f.kind) {
    case Finger::Lane: {
        const Shown &s = m_shown[f.lane];
        QString t = QStringLiteral("%1 %2").arg(m_names.value(f.lane).toUpper()).arg(int(std::lround(s.gain * 100)));
        if (s.filt < 0.49f)
            t += QStringLiteral(" LP%1").arg(int(std::lround((0.5f - s.filt) * 200)));
        else if (s.filt > 0.51f)
            t += QStringLiteral(" HP%1").arg(int(std::lround((s.filt - 0.5f) * 200)));
        if (m_ride[f.lane].echo > 0.05f)
            t += QStringLiteral(" ECHO");
        if (std::fabs(m_ride[f.lane].pump) > 0.05f)
            t += QStringLiteral(" PUMP");
        if (f.clicked)
            t += QStringLiteral(" ✓");
        return t;
    }
    case Finger::Send: return QStringLiteral("VERB %1 PAN").arg(int(std::lround(m_shown[f.lane].verb * 100)));
    case Finger::Name: return f.solo ? QStringLiteral("SOLO") : QStringLiteral("MUTE?");
    case Finger::Tension:
        return m_tensionShown >= 0.6f ? QStringLiteral("LET GO: DROP") : m_tensionShown < 0 ? QStringLiteral("SINK")
                                                                                            : QStringLiteral("BUILD");
    case Finger::Scene: return QStringLiteral("SCENE %1").arg(f.index + 1);
    default: return {};
    }
}

QString ArrangeControl::movesText() const {
    QStringList out;
    for (const auto &m : m_moves) {
        const double e = m_beat - m.b0;
        switch (m.kind) {
        case MBuild:
            if (m_beat < m.b1)
                out << (e < 0 ? QStringLiteral("BUILD ON THE BAR")
                              : QStringLiteral("BUILD → DROP IN %1").arg(int(std::ceil(m.b1 - m_beat))));
            break;
        case MBreak:
            if (m_beat < m.b1)
                out << (e < 0 ? QStringLiteral("BREAKDOWN ON THE BAR") : QStringLiteral("BREAKDOWN"));
            break;
        case MEchoOut:
            if (e < 4)
                out << QStringLiteral("ECHO-OUT");
            break;
        case MSink:
            if (m_beat < m.b1)
                out << (m.aux < 1e17 ? QStringLiteral("FILTER OPENS") : QStringLiteral("FILTERED OUT"));
            break;
        case MLift:
            if (e < 0)
                out << QStringLiteral("KEY UP ON THE BAR");
            break;
        default: break;
        }
    }
    out.removeDuplicates();
    return out.join(QStringLiteral("  "));
}

QString ArrangeControl::songText() const {
    const long bar = long(std::floor(m_beat / kBeatsPerBar)) - m_origin;
    const int n = m_song.length();
    QString sec;
    int inSec = 0, secBars = 0;
    for (const auto &s : m_sections)
        if (bar >= s.bar0 && bar < s.bar0 + s.bars) {
            sec = QString::fromUtf8(labelName(s.label)).toUpper();
            inSec = int(bar - s.bar0) + 1;
            secBars = s.bars;
        }
    QString t = QStringLiteral("▶ SONG %1/%2").arg(std::clamp(bar + 1, 1L, long(n))).arg(n);
    if (!sec.isEmpty())
        t += QStringLiteral("  %1 %2/%3").arg(sec).arg(inSec).arg(secBars);
    if (m_loop)
        t += QStringLiteral("  ⟲");
    if (m_recording)
        t += QStringLiteral("  ● PUNCH");
    return t;
}

QString ArrangeControl::status() const {
    QStringList parts;
    for (const auto &f : m_fingers) {
        if (f.id < 0)
            continue;
        const QString l = fingerLabel(f);
        if (!l.isEmpty())
            parts << l.toLower();
    }
    parts.removeDuplicates();
    const QString mv = movesText();
    if (!mv.isEmpty())
        parts << mv.toLower();
    if (m_playing)
        parts << songText().toLower();
    else if (m_recording)
        parts << QStringLiteral("recording the song");
    if (parts.isEmpty() && !m_anyClip)
        return QStringLiteral("no loops yet: on pages 1–7, Space records one from the next bar, Space again closes it");
    if (parts.isEmpty())
        return QStringLiteral("tap clips to launch them on the bar · F1–F8 scenes · I writes a track from your loops");
    return parts.join(QStringLiteral(" · "));
}

}

PartSpec makeArrange(const QStringList &names, const std::vector<QColor> &colors) {
    PartSpec s;
    s.name = QStringLiteral("Arrange");
    s.blurb = QStringLiteral("clips into a track");
    s.color = QColor("#FFD60A");
    s.control = std::make_unique<ArrangeControl>(names, colors);
    return s;
}

}
