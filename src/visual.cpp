#include "visual.h"

#include <QJsonValue>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "arp.h"
#include "bass.h"
#include "drums.h"
#include "lead.h"
#include "risers.h"
#include "tuning.h"

namespace tf::visual {

namespace {

// The Vocal and Atmosphere pages keep their published layouts in their .cpp
// files; these are the few numbers the visualizer reads from them (vocal.cpp
// `pub`, atmos.cpp `Pub`). tests/visual_tests.cpp sings and swells through
// the real pages, so a change there that breaks these shows up.
namespace vocalPub {
constexpr int InRms = 1, Hz = 2, Clarity = 3, LivePeak = 7, Loops = 10;
}
namespace atmosPub {
constexpr int Swell = 4, Halo = 12, Level = 15, Tone = 19;
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float wrap01(float v) { return v - std::floor(v); }

// The shorter way round the hue circle from a to b, at u.
float hueLerp(float a, float b, float u) {
    float d = b - a;
    d -= std::round(d);
    return wrap01(a + d * u);
}

constexpr float kPi = 3.14159265f;

} // namespace

float fifthsHue(int pitchClass) {
    const int pc = ((pitchClass % 12) + 12) % 12;
    // Seven semitones a step: C G D A E B F# … each a twelfth of the circle,
    // so related chords sit near each other in colour and a move to a far
    // key is a far colour.
    return float((pc * 7) % 12) / 12.0f;
}

bool audible(const StudioView &v, int part) {
    if (part < 0 || part >= v.partCount)
        return false;
    bool anySolo = false;
    for (int i = 0; i < v.partCount; ++i)
        anySolo |= v.parts[i].channel.value[Solo] > 0.5f;
    const float *ch = v.parts[part].channel.value;
    return ch[Mute] < 0.5f && !(anySolo && ch[Solo] < 0.5f) && ch[Gain] >= 0.02f;
}

float SignalTracker::decay(float v, double dt, double seconds) {
    return v * float(std::exp(-dt / seconds));
}

void SignalTracker::note(double t, float pitch, float vel, int kind) {
    for (int i = kNotes - 1; i > 0; --i)
        m_s.notes[i] = m_s.notes[i - 1];
    m_s.notes[0] = {t, clamp01(pitch), clamp01(vel), kind};
}

void SignalTracker::update(const StudioView &v, double dt) {
    dt = std::clamp(dt, 0.0, 0.25);
    Signals &s = m_s;
    s.time = v.time;
    s.beat = v.beat;
    s.beatPhase = float(v.beat - std::floor(v.beat));
    s.barPhase = float(v.beat / kBeatsPerBar - std::floor(v.beat / kBeatsPerBar));
    s.bpm = float(v.bpm);
    s.master = clamp01(v.rms * 4.0f);

    bool aud[kParts] = {};
    for (int p = 0; p < kParts; ++p) {
        aud[p] = audible(v, p);
        const float rms = aud[p] ? v.parts[p].channel.rms : 0.0f;
        // Each part against its own loudest of the last few seconds: a soft
        // pad and a loud kick both fill their own gauge.
        m_peak[p] = std::max(rms, m_peak[p] * float(std::exp(-dt / 6.0)));
        const float target = clamp01(rms / std::max(m_peak[p], 0.03f));
        s.level[p] += (target - s.level[p]) * float(1.0 - std::exp(-dt / 0.05));
    }

    // Drums: what each pad was just hit with, live or from a loop. A jump up
    // is a hit; the envelope keeps the loudest and falls away.
    if (v.partCount > Drums) {
        const float *d = v.parts[Drums].data;
        auto hit = [&](std::initializer_list<int> pads) {
            float m = 0;
            for (int pad : pads)
                m = std::max({m, d[pad], d[16 + pad]});
            return aud[Drums] ? m : 0.0f;
        };
        using namespace tf::drums;
        const float now[3] = {hit({Kick}), hit({Snare, Clap, Rim}), hit({Hat, Shaker, Open, Ride})};
        float *env[3] = {&s.kick, &s.snare, &s.hat};
        int *count[3] = {&s.kicks, &s.snares, &s.hats};
        const double falls[3] = {0.18, 0.16, 0.07};
        for (int i = 0; i < 3; ++i) {
            if (now[i] > m_prevDrum[i] + 0.05f && now[i] > 0.1f)
                ++*count[i];
            *env[i] = std::max(decay(*env[i], dt, falls[i]), now[i]);
            m_prevDrum[i] = now[i];
        }
        s.perc = std::max(decay(s.perc, dt, 0.15), hit({TomLo, TomMid, TomHi, Perc, Crash, Bell, Zap}));
        s.sub = aud[Drums] ? clamp01(d[32] * 1.5f) : decay(s.sub, dt, 0.1);
    }

    // Bass: the louder of its live and loop lanes.
    if (v.partCount > Bass) {
        const float *d = v.parts[Bass].data;
        float best = -1;
        for (int lane : {0, 8}) {
            const float *l = d + lane;
            if (l[bass::PubOn] > 0 && l[bass::PubLevel] > best) {
                best = l[bass::PubLevel];
                s.bassPitch = clamp01((l[bass::PubMidi] - 28.0f) / 36.0f);
                s.bassFilter = clamp01(l[bass::PubCut]);
                s.bassEnv = aud[Bass] ? clamp01(l[bass::PubEnv]) : 0.0f;
            }
        }
        if (best < 0)
            s.bassEnv = decay(s.bassEnv, dt, 0.1);
        s.bass = s.level[Bass];
    }

    // Chords: the chord's root as a colour. Every change crossfades from the
    // colour on screen and flashes.
    {
        Tuning t;
        t.set(v.root, v.family, v.brightness, v.just);
        const int pc = int(std::lround(v.root + t.degreeSemis(v.chordRoot)));
        const float target = fifthsHue(pc);
        if (!m_seen) {
            s.chordHue = s.chordPrevHue = target;
            s.chordSeq = v.chordSeq;
        } else if (v.chordSeq != s.chordSeq || std::fabs(target - m_chordTarget) > 1e-4f) {
            s.chordPrevHue = s.chordHue;
            s.chordBlend = 0;
            s.chordSeq = v.chordSeq;
            s.chordFlash = aud[Chords] && s.level[Chords] > 0.05f ? 1.0f : 0.4f;
            ++s.chordChanges;
        }
        m_chordTarget = target;
        s.chordBlend = std::min(1.0f, s.chordBlend + float(dt / 0.6));
        const float u = s.chordBlend * s.chordBlend * (3 - 2 * s.chordBlend);
        s.chordHue = hueLerp(s.chordPrevHue, target, u);
        s.chordFlash = decay(s.chordFlash, dt, 0.35);
        s.chordTones = std::clamp(v.chordCount, 1, 4);
        s.chords = s.level[Chords];
    }

    // Lead: the loudest line sounding. A new note is a line starting, or
    // moving to another degree.
    if (v.partCount > Lead) {
        const float *data = v.parts[Lead].data;
        float best = 0, on = 0, pos = m_prevLeadPos;
        for (int line = 0; line < lead::kShownLines; ++line) {
            const float *d = data + lead::kLineData * line;
            if (d[0] > 0 && d[2] >= best) {
                best = d[2];
                on = d[0];
                pos = d[1];
                s.leadBright = clamp01(d[3]);
                s.leadBend = std::clamp(d[10] / 4.0f, -1.0f, 1.0f);
            }
        }
        if (!aud[Lead])
            on = 0, best = 0;
        if (on >= 1 && (m_prevLeadOn < 1 || std::fabs(pos - m_prevLeadPos) >= 0.5f))
            note(v.time, (pos + 7.0f) / 28.0f, clamp01(best * 2), 1);
        if (on > 0)
            s.leadPitch += ((pos + 7.0f) / 28.0f - s.leadPitch) * float(1.0 - std::exp(-dt / 0.04));
        s.leadPitch = clamp01(s.leadPitch);
        m_prevLeadOn = on;
        m_prevLeadPos = pos;
        s.lead = std::max(decay(s.lead, dt, 0.12), std::max(s.level[Lead], on > 0 ? clamp01(best * 2.5f) : 0.0f));
    }

    // Arp: its ring of recent notes, oldest first; the ones not seen before
    // are new.
    s.arpFlash = decay(s.arpFlash, dt, 0.09);
    m_rate = decay(m_rate, dt, 0.5);
    if (v.partCount > Arp) {
        const float *d = v.parts[Arp].data;
        const float *newest = d + arp::kDataRecent + 4 * (arp::kRecent - 1);
        if (newest[3] > 0 && (newest[0] != m_arpBeat || newest[1] != m_arpDegree)) {
            int fresh = 0;
            for (int i = arp::kRecent - 1; i >= 0 && fresh < 8; --i) {
                const float *o = d + arp::kDataRecent + 4 * i;
                if (o[3] <= 0 || (o[0] == m_arpBeat && o[1] == m_arpDegree))
                    break;
                ++fresh;
            }
            if (m_seen && aud[Arp]) {
                // Oldest first, so the newest ends up notes[0].
                for (int i = arp::kRecent - fresh; i < arp::kRecent; ++i) {
                    const float *o = d + arp::kDataRecent + 4 * i;
                    note(v.time, (o[1] + 3.0f) / 24.0f, std::fabs(o[2]), 0);
                }
                if (fresh > 0) {
                    s.arpFlash = 1;
                    s.arpPitch = s.notes[0].pitch;
                    m_rate += float(fresh);
                }
            }
            m_arpBeat = newest[0];
            m_arpDegree = newest[1];
        }
        s.arpRate = clamp01(m_rate / 8.0f);
        s.arp = aud[Arp] ? std::max(clamp01(d[arp::kDataEnv] * 1.5f), s.level[Arp] * 0.6f) : decay(s.arp, dt, 0.1);
    }

    // Atmosphere.
    if (v.partCount > Atmos) {
        const float *d = v.parts[Atmos].data;
        const float k = aud[Atmos] ? 1.0f : 0.0f;
        s.atmos = s.level[Atmos];
        s.swell += (k * clamp01(d[atmosPub::Swell]) - s.swell) * float(1.0 - std::exp(-dt / 0.2));
        s.halo += (k * clamp01(d[atmosPub::Halo]) - s.halo) * float(1.0 - std::exp(-dt / 0.2));
        s.atmosTone = clamp01(d[atmosPub::Tone]);
        if (d[atmosPub::Level] > 0)
            s.atmos = std::max(s.atmos, k * clamp01(d[atmosPub::Level] * 2));
    }

    // Effects: the build, the drop (a jump in its flash), the sweep. A build
    // is also the whole mix swept through its filter (the Song page's
    // tension fader, a page's own build), and its drop the filter snapping
    // back open.
    if (v.partCount > Fx) {
        const float *d = v.parts[Fx].data;
        const float k = aud[Fx] ? 1.0f : 0.0f;
        const float swept = clamp01((std::fabs(v.master[MasterFilter] - 0.5f) * 2.0f - 0.1f) * 1.4f);
        const float target = std::max(k * clamp01(d[risers::pub::Intensity]), swept);
        s.build += (target - s.build) * float(1.0 - std::exp(-dt / 0.08));
        m_sweepPeak = std::max(swept, m_sweepPeak * float(std::exp(-dt / 0.25)));
        float flash = k * clamp01(d[risers::pub::Flash]);
        if (m_sweepPeak > 0.45f && swept < 0.08f) {
            flash = 1.0f;
            m_sweepPeak = 0;
        }
        if (flash > m_prevDrop + 0.3f)
            ++s.drops;
        m_prevDrop = flash;
        s.drop = std::max(decay(s.drop, dt, 0.4), flash);
        s.sweep = std::clamp(d[risers::pub::Sweep], -1.0f, 1.0f) * k;
        s.wash = clamp01(k * std::max(d[risers::pub::Wash], d[risers::pub::Throw]));
    }

    // Vocal: the microphone while it sings, else the loops it made.
    if (v.partCount > Vocal) {
        const float *d = v.parts[Vocal].data;
        const float mic = aud[Vocal] ? clamp01(d[vocalPub::InRms] * 6.0f) : 0.0f;
        const float loops = aud[Vocal] && d[vocalPub::Loops] > 0 ? s.level[Vocal] : 0.0f;
        s.vocal = std::max(decay(s.vocal, dt, 0.1), std::max(mic, loops));
        s.vocalLoops = clamp01(d[vocalPub::Loops] / 4.0f);
        s.clarity = clamp01(d[vocalPub::Clarity]);
        const float hz = d[vocalPub::Hz];
        if (hz > 40 && s.clarity > 0.3f) {
            const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
            s.vocalPitch += (clamp01((midi - 40.0f) / 40.0f) - s.vocalPitch) * float(1.0 - std::exp(-dt / 0.05));
        }
        (void)vocalPub::LivePeak;
    }
    m_seen = true;
}

// --- the hand ------------------------------------------------------------------

Look tidy(Look l) {
    l.world = std::fmod(l.world, kWorlds);
    if (l.world < 0)
        l.world += kWorlds;
    l.fold = clamp01(l.fold);
    l.symmetry = clamp01(l.symmetry);
    l.warp = clamp01(l.warp);
    l.zoom = std::clamp(l.zoom, -1.0f, 1.0f);
    l.yaw = std::clamp(l.yaw, -1.0f, 1.0f);
    l.pitch = std::clamp(l.pitch, -1.0f, 1.0f);
    l.speed = clamp01(l.speed);
    l.trails = clamp01(l.trails);
    l.hue = wrap01(l.hue);
    // Twist and seed are any angle or number; kept small so a float keeps
    // its precision through a long set.
    l.twist = std::remainder(l.twist, 2 * kPi * 64);
    l.seed = std::remainder(l.seed, 4096.0f);
    return l;
}

void HandMorph::frame(const Frame &f, float aspect) {
    m_aspect = aspect > 0.1f ? aspect : 1.6f;
    Hand &h = m_hand;
    // The fingers now, in pad units that are round (x scaled by the aspect).
    const int n = std::min(f.count, 10);
    float cx = 0, cy = 0, p = 0;
    for (int i = 0; i < n; ++i) {
        cx += f.touches[i].x;
        cy += f.touches[i].y;
        p += f.touches[i].pressure;
    }
    if (n > 0) {
        cx /= float(n);
        cy /= float(n);
        p /= float(n);
    }
    float spread = 0;
    for (int i = 0; i < n; ++i)
        spread += std::hypot((f.touches[i].x - cx) * m_aspect, f.touches[i].y - cy);
    if (n > 0)
        spread /= float(n);

    // The same fingers as the reference frame: how they moved since.
    bool same = n > 0 && n == m_ref.count;
    for (int i = 0; same && i < n; ++i)
        same = f.touches[i].id == m_ref.ids[i];
    if (same) {
        float rx = 0, ry = 0;
        for (int i = 0; i < n; ++i) {
            rx += m_ref.x[i];
            ry += m_ref.y[i];
        }
        rx /= float(n);
        ry /= float(n);
        float rspread = 0, turn = 0, weight = 0;
        for (int i = 0; i < n; ++i) {
            const float ax = (m_ref.x[i] - rx) * m_aspect, ay = m_ref.y[i] - ry;
            const float bx = (f.touches[i].x - cx) * m_aspect, by = f.touches[i].y - cy;
            rspread += std::hypot(ax, ay);
            // Each finger's turn about the centroid, weighted by how far out
            // it is (a finger at the centre says nothing about a twist).
            const float w = std::hypot(ax, ay) * std::hypot(bx, by);
            if (w > 1e-6f) {
                turn += std::atan2(ax * by - ay * bx, ax * bx + ay * by) * w;
                weight += w;
            }
        }
        rspread /= float(n);
        const float dTurn = weight > 1e-6f && n >= 2 ? turn / weight : 0.0f;
        const float dx = (cx - rx) * m_aspect, dy = cy - ry;
        const float dSpread = rspread > 0.01f && spread > 0.01f ? std::log(spread / rspread) : 0.0f;
        const double dt = std::max(1e-3, f.t - m_ref.t);
        const float speed = float(std::hypot(dx, dy) / dt) + std::fabs(dSpread) * 2.0f / float(dt);
        h.motion += (speed - h.motion) * 0.3f;
        // A quick flick glitches; it fades in tick().
        h.chaos = std::max(h.chaos, clamp01((speed - 1.5f) / 4.0f));

        Look &l = look;
        switch (n) {
        case 1:
            l.yaw += dx * 2.0f;
            l.pitch += dy * 2.0f;
            m_speedTarget = clamp01(p * 1.4f - 0.1f);
            break;
        case 2:
            l.twist += dTurn;
            l.zoom += dSpread * 1.5f;
            break;
        case 3:
            l.world += dx * 3.0f;
            l.fold += dy * 1.5f;
            l.symmetry += dTurn / kPi;
            break;
        case 4:
            l.warp += dSpread * 1.5f;
            l.hue += dTurn / (2 * kPi);
            l.fold += dy * 0.8f;
            break;
        case 5:
            l.seed += dTurn * 2.0f;
            l.trails += dSpread * 1.5f;
            l.speed += dy * 1.5f;
            break;
        default: {
            // Two hands: split at the widest gap between the fingers' x.
            // Ten at most: an insertion sort of the fingers' x.
            float xs[10];
            for (int i = 0; i < n; ++i) {
                int j = i;
                for (; j > 0 && xs[j - 1] > f.touches[i].x; --j)
                    xs[j] = xs[j - 1];
                xs[j] = f.touches[i].x;
            }
            int cut = n / 2;
            float gap = 0;
            for (int i = 1; i < n; ++i)
                if (xs[i] - xs[i - 1] > gap) {
                    gap = xs[i] - xs[i - 1];
                    cut = i;
                }
            const float split = (xs[cut - 1] + xs[cut]) / 2;
            float lx = 0, ly = 0, ln = 0, Rx = 0, Ry = 0, Rn = 0;
            for (int i = 0; i < n; ++i) {
                const float mx = (f.touches[i].x - m_ref.x[i]) * m_aspect, my = f.touches[i].y - m_ref.y[i];
                if (f.touches[i].x < split)
                    lx += mx, ly += my, ln += 1;
                else
                    Rx += mx, Ry += my, Rn += 1;
            }
            if (ln > 0) {
                l.yaw += lx / ln * 2.0f;
                l.pitch += ly / ln * 2.0f;
            }
            if (Rn > 0) {
                l.world += Rx / Rn * 3.0f;
                l.fold += Ry / Rn * 1.5f;
            }
            l.zoom += dSpread * 1.5f;
            l.twist += dTurn;
            break;
        }
        }
        l = tidy(l);
    }
    if (n == 0)
        m_speedTarget = -1;
    m_ref.count = n;
    m_ref.t = f.t;
    for (int i = 0; i < n && i < 16; ++i) {
        m_ref.ids[i] = f.touches[i].id;
        m_ref.x[i] = f.touches[i].x;
        m_ref.y[i] = f.touches[i].y;
    }

    // The hand as the shaders see it. A finger keeps its slot while it is
    // down; a lifted one fades out of its slot in tick().
    h.count = n;
    if (n > 0) {
        h.cx = cx;
        h.cy = cy;
        h.spread = spread;
        h.pressure = p;
        m_idle = 0;
    }
    for (auto &slot : h.fingers)
        if (slot.alive >= 1 && std::none_of(f.touches.begin(), f.touches.begin() + n,
                                            [&](const Touch &t) { return t.id == slot.id; }))
            slot.alive = 0.999f; // lifted: starts to fade
    for (int i = 0; i < n; ++i) {
        const Touch &t = f.touches[i];
        auto it = std::find_if(h.fingers.begin(), h.fingers.end(), [&](const Hand::Finger &s) { return s.id == t.id; });
        if (it == h.fingers.end())
            it = std::min_element(h.fingers.begin(), h.fingers.end(),
                                  [](const Hand::Finger &a, const Hand::Finger &b) { return a.alive < b.alive; });
        *it = {t.x, t.y, clamp01(t.pressure), 1.0f, t.id};
    }
}

void HandMorph::tick(double dt, bool autopilot) {
    dt = std::clamp(dt, 0.0, 0.25);
    Hand &h = m_hand;
    for (auto &slot : h.fingers)
        if (slot.alive < 1)
            slot.alive = std::max(0.0f, slot.alive - float(dt / 0.6));
    h.chaos *= float(std::exp(-dt / 0.35));
    if (h.count == 0) {
        h.motion *= float(std::exp(-dt / 0.3));
        m_idle += dt;
    }
    if (m_speedTarget >= 0)
        look.speed += (m_speedTarget - look.speed) * float(1.0 - std::exp(-dt / 0.3));
    // Left alone for a while, the world wanders on by itself, slowly, so a
    // set never sits on one still picture; a hand takes over at once.
    if (autopilot && m_idle > 4.0) {
        const float ramp = float(std::min(1.0, (m_idle - 4.0) / 4.0));
        look.world += float(dt) * 0.012f * ramp;
        look.seed += float(dt) * 0.02f * ramp;
        look.twist += float(dt) * 0.03f * ramp;
        look.hue += float(dt) * 0.004f * ramp;
    }
    look = tidy(look);
}

QJsonObject toJson(const Look &l) {
    auto r = [](float v) { return std::round(double(v) * 10000.0) / 10000.0; };
    QJsonObject o;
    o[QStringLiteral("world")] = r(l.world);
    o[QStringLiteral("fold")] = r(l.fold);
    o[QStringLiteral("symmetry")] = r(l.symmetry);
    o[QStringLiteral("warp")] = r(l.warp);
    o[QStringLiteral("twist")] = r(l.twist);
    o[QStringLiteral("zoom")] = r(l.zoom);
    o[QStringLiteral("yaw")] = r(l.yaw);
    o[QStringLiteral("pitch")] = r(l.pitch);
    o[QStringLiteral("speed")] = r(l.speed);
    o[QStringLiteral("trails")] = r(l.trails);
    o[QStringLiteral("hue")] = r(l.hue);
    o[QStringLiteral("seed")] = r(l.seed);
    return o;
}

Look fromJson(const QJsonObject &o, Look l) {
    auto get = [&](const char *name, float &v) {
        const QJsonValue j = o.value(QLatin1String(name));
        if (j.isDouble() && std::isfinite(j.toDouble()))
            v = float(j.toDouble());
    };
    get("world", l.world);
    get("fold", l.fold);
    get("symmetry", l.symmetry);
    get("warp", l.warp);
    get("twist", l.twist);
    get("zoom", l.zoom);
    get("yaw", l.yaw);
    get("pitch", l.pitch);
    get("speed", l.speed);
    get("trails", l.trails);
    get("hue", l.hue);
    get("seed", l.seed);
    return tidy(l);
}

// --- the page ------------------------------------------------------------------

namespace {
const char *const kWorldNames[] = {"TUNNEL", "MANDELBOX", "CRYSTAL", "CITY", "LATTICE"};
const char *const kQualityNames[] = {"DRAFT", "NORMAL", "FINE", "FULL"};
enum Spot : int { SpotWorld = 0, SpotAuto = 10, SpotMarkers, SpotQuality, SpotWindow, SpotFull, SpotJump, SpotHome };
} // namespace

int VisualPage::worldKey(int key) {
    switch (key) {
    case Qt::Key_A: return 0;
    case Qt::Key_S: return 1;
    case Qt::Key_D: return 2;
    case Qt::Key_F: return 3;
    case Qt::Key_G: return 4;
    default: return -1;
    }
}

void VisualPage::frame(const Frame &f, ControlContext &ctx) {
    m_morph.frame(f, ctx.aspect());
    m_state.settings.look = m_morph.look;
    m_state.hand = m_morph.hand();
}

void VisualPage::tick(double dt) {
    m_morph.tick(dt, m_state.settings.autopilot);
    m_state.settings.look = m_morph.look;
    m_state.hand = m_morph.hand();
}

void VisualPage::leave(ControlContext &ctx) {
    // No sound to end; the fingers that were down just stop pulling.
    Frame none;
    none.t = ctx.now();
    m_morph.frame(none, ctx.aspect());
    m_state.hand = m_morph.hand();
}

void VisualPage::ask(ControlContext &ctx, const QString &what) { ctx.notice(what); }

bool VisualPage::key(const KeyEvent &k, ControlContext &ctx) {
    if (!k.down)
        return worldKey(k.key) >= 0 || k.key == Qt::Key_Space;
    Look &l = m_morph.look;
    Settings &s = m_state.settings;
    const bool shift = k.modifiers & Qt::ShiftModifier;
    bool used = true;
    if (const int w = worldKey(k.key); w >= 0) {
        l.world = float(w);
        ask(ctx, QStringLiteral("World: %1").arg(QLatin1String(kWorldNames[w])));
    } else {
        switch (k.key) {
        case Qt::Key_Left: l.world -= shift ? 0.1f : 1.0f; break;
        case Qt::Key_Right: l.world += shift ? 0.1f : 1.0f; break;
        case Qt::Key_Up: l.fold += 0.1f; break;
        case Qt::Key_Down: l.fold -= 0.1f; break;
        case Qt::Key_BracketLeft: l.twist -= kPi / 8; break;
        case Qt::Key_BracketRight: l.twist += kPi / 8; break;
        case Qt::Key_K:
            // The kaleidoscope's steps: off, 3, 4, 6, 8, 12-fold.
            l.symmetry = l.symmetry < 0.05f ? 0.2f : l.symmetry >= 0.95f ? 0.0f : std::min(1.0f, l.symmetry + 0.2f);
            break;
        case Qt::Key_W: l.warp = l.warp >= 0.95f ? 0.0f : std::min(1.0f, l.warp + 0.25f); break;
        case Qt::Key_T: l.trails = l.trails >= 0.95f ? 0.0f : std::min(1.0f, l.trails + 0.25f); break;
        case Qt::Key_H: l.hue += 1.0f / 12.0f; break;
        case Qt::Key_Z: l.zoom = l.zoom >= 0.95f ? -1.0f : std::min(1.0f, l.zoom + 0.5f); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: l.speed += 0.1f; break;
        case Qt::Key_Minus: l.speed -= 0.1f; break;
        case Qt::Key_Space:
            // A jump: another seed, far from this one: a new variation of
            // the same world.
            if (!k.repeat) {
                l.seed += 7.31f + float(m_jumps++ % 5) * 3.17f;
                ask(ctx, QStringLiteral("Another world"));
            }
            break;
        case Qt::Key_Backspace:
            l = Look{l.world};
            ask(ctx, QStringLiteral("The world back home"));
            break;
        case Qt::Key_P:
            s.autopilot = !s.autopilot;
            ask(ctx, s.autopilot ? QStringLiteral("Autopilot on: it wanders when left alone")
                                 : QStringLiteral("Autopilot off: it stays where the hand left it"));
            break;
        case Qt::Key_M: s.markers = !s.markers; break;
        case Qt::Key_Q:
            s.quality = (s.quality + 1) % 4;
            ask(ctx, QStringLiteral("Quality: %1").arg(QLatin1String(kQualityNames[s.quality])));
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter: ++(shift ? m_state.fullScreenAsks : m_state.windowAsks); break;
        default: used = false;
        }
    }
    l = tidy(l);
    s.look = l;
    return used;
}

void VisualPage::paint(QPainter &p, const PaintContext &pc) {
    // The picture is drawn under the page by the window (Visualizer.qml);
    // the page draws only the frame, its legend and the band of controls.
    m_spots.clear();
    const Settings &s = m_state.settings;
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(pc.color, 2.0));
    p.drawRect(pc.pad);
    const int w = int(std::floor(s.look.world + 0.5f)) % int(kWorlds);
    const QString legend = QStringLiteral("VISUAL · %1").arg(QLatin1String(kWorldNames[w]));
    const QFont f = console::font(13, true);
    p.setFont(f);
    const QRectF lr(pc.pad.left() + 16, pc.pad.top() - 9, console::textWidth(f, legend) + 16, 18);
    p.fillRect(lr, QColor(0, 0, 0));
    p.setPen(pc.color);
    console::drawText(p, lr, Qt::AlignCenter, legend);

    if (s.markers) {
        for (const auto &fg : m_state.hand.fingers)
            if (fg.alive > 0) {
                Touch t;
                t.x = fg.x;
                t.y = fg.y;
                t.size = 0.8f + fg.pressure;
                p.setOpacity(fg.alive);
                console::finger(p, pc, t, pc.color);
            }
        p.setOpacity(1);
    }

    QStringList worlds;
    for (const char *n : kWorldNames)
        worlds << QLatin1String(n);
    console::chips(p, pc, console::bandRow(pc, 0, 0, 0.62), worlds, w, &m_spots, SpotWorld);
    console::chip(p, pc, console::bandRow(pc, 0, 0.64, 0.81), QStringLiteral("ANOTHER ␣"), false, &m_spots, SpotJump);
    console::chip(p, pc, console::bandRow(pc, 0, 0.83, 1), QStringLiteral("HOME ⌫"), false, &m_spots, SpotHome);
    console::chip(p, pc, console::bandRow(pc, 1, 0, 0.19), QStringLiteral("AUTOPILOT P"), s.autopilot, &m_spots, SpotAuto);
    console::chip(p, pc, console::bandRow(pc, 1, 0.21, 0.4), QStringLiteral("FINGERS M"), s.markers, &m_spots, SpotMarkers);
    console::chip(p, pc, console::bandRow(pc, 1, 0.42, 0.6),
                  QStringLiteral("%1 Q").arg(QLatin1String(kQualityNames[s.quality])), false, &m_spots, SpotQuality);
    console::chip(p, pc, console::bandRow(pc, 1, 0.62, 0.81), QStringLiteral("WINDOW ↵"), false, &m_spots, SpotWindow);
    console::chip(p, pc, console::bandRow(pc, 1, 0.83, 1), QStringLiteral("FULL SCREEN"), false, &m_spots, SpotFull);
}

bool VisualPage::click(QPointF at, ControlContext &ctx) {
    const int id = m_spots.at(at);
    if (id < 0)
        return false;
    auto press = [&](int key) {
        KeyEvent k;
        k.key = key;
        this->key(k, ctx);
    };
    if (id >= SpotWorld && id < SpotWorld + int(kWorlds)) {
        static const int keys[] = {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G};
        press(keys[id - SpotWorld]);
        return true;
    }
    switch (id) {
    case SpotAuto: press(Qt::Key_P); break;
    case SpotMarkers: press(Qt::Key_M); break;
    case SpotQuality: press(Qt::Key_Q); break;
    case SpotWindow: press(Qt::Key_Return); break;
    case SpotFull: ++m_state.fullScreenAsks; break;
    case SpotJump: press(Qt::Key_Space); break;
    case SpotHome: press(Qt::Key_Backspace); break;
    default: return false;
    }
    return true;
}

QString VisualPage::help() const {
    return QStringLiteral(
        "Everything playing\tdraws itself: the kick punches the camera, the snare flashes a ring through the world, "
        "hats sparkle, the bass bends space, each chord paints it, the lead is a streak at its pitch, arp notes "
        "burst into sparks, the atmosphere is the fog, a build speeds it up and a drop throws it into another "
        "world, the voice is the ring at the centre\n"
        "1 finger\tsteer: across turns the camera, up and down tilts it; press harder to fly faster\n"
        "2 fingers\ttwist to roll the world; spread or pinch to zoom\n"
        "3 fingers\tslide sideways to travel between worlds, morphing on the way; up and down folds them; twist "
        "for the kaleidoscope\n"
        "4 fingers\tspread to warp space; twist to turn the colours\n"
        "5 fingers\ttwist the whole hand to dial endless variations; spread for longer trails; up and down for speed\n"
        "Two hands\tthe left steers, the right travels and folds; spread both to zoom, twist both to roll\n"
        "Every finger\tis a whirlpool in the picture, as strong as it presses; a quick flick glitches it\n"
        "A S D F G\tthe worlds: tunnel, mandelbox, crystal, city, lattice\n"
        "Left / Right\tthe next world (Shift: a little way toward it)\n"
        "Up / Down\tmore or less folded\n"
        "[ ]\troll · K kaleidoscope · W warp · T trails · Z zoom · H colours · + − speed\n"
        "Space\tanother variation of this world · Backspace the world back home\n"
        "P\tautopilot: left alone it wanders on its own · M the fingers drawn over it · Q quality\n"
        "Return\tthe picture in a window of its own; Shift+Return puts it full screen (there F11 or a "
        "double-click does too, and Esc leaves it). The fingers keep playing this window's page\n"
        "Ctrl+Shift+R (or Shift+click Record)\trecord the picture and the sound to a video (needs ffmpeg)\n"
        "Lifted fingers\tleave the world as they shaped it: go back to any page and play, the picture keeps it");
}

QString VisualPage::status() const {
    const Hand &h = m_state.hand;
    if (h.count == 0)
        return {};
    static const char *const what[] = {"",       "steering",           "rolling · zooming",
                                       "travelling · folding", "warping · colouring", "dialling variations"};
    return QStringLiteral("%1 fingers: %2").arg(h.count).arg(QLatin1String(h.count <= 5 ? what[h.count] : "two hands"));
}

void VisualPage::startOver(ControlContext &) {
    m_morph = HandMorph();
    m_state.settings = Settings();
    m_state.hand = Hand();
}

void VisualPage::save(QJsonObject &out) const {
    const Settings &s = m_state.settings;
    out[QStringLiteral("look")] = toJson(s.look);
    out[QStringLiteral("autopilot")] = s.autopilot;
    out[QStringLiteral("quality")] = s.quality;
    out[QStringLiteral("markers")] = s.markers;
}

void VisualPage::load(const QJsonObject &in, ControlContext &) {
    Settings &s = m_state.settings;
    s.look = fromJson(in.value(QStringLiteral("look")).toObject(), Look());
    m_morph.look = s.look;
    if (in.value(QStringLiteral("autopilot")).isBool())
        s.autopilot = in.value(QStringLiteral("autopilot")).toBool();
    if (in.value(QStringLiteral("markers")).isBool())
        s.markers = in.value(QStringLiteral("markers")).toBool();
    if (in.value(QStringLiteral("quality")).isDouble())
        s.quality = std::clamp(in.value(QStringLiteral("quality")).toInt(), 0, 3);
}

float VisualPage::tutorialProbe(const QString &name) const {
    if (name == QLatin1String("world"))
        return m_state.settings.look.world;
    if (name == QLatin1String("fingers"))
        return float(m_state.hand.count);
    return std::nanf("");
}

PartSpec makeVisual() {
    PartSpec s;
    s.name = QStringLiteral("Visual");
    s.blurb = QStringLiteral("the show, shaped by hand");
    s.color = QColor("#E4E8FF");
    s.control = std::make_unique<VisualPage>();
    s.followsChords = false;
    return s;
}

}

namespace tf::visual {

void Drive::step(const Signals &s, const Look &l, double dt) {
    dt = std::clamp(dt, 0.0, 0.25);
    // Faster with the tempo, the hand's speed, a build; the kick shoves.
    const double pace = (0.5 + 2.8 * double(l.speed)) * std::clamp(double(s.bpm) / 124.0, 0.5, 2.0) *
                        (1.0 + 1.8 * double(s.build));
    travel += dt * (pace + 2.5 * double(s.kick));
    // The echoes turn with the roll the hand gave the world and with how
    // fast it is twisting now.
    const float twistSpeed = dt > 0 ? float((l.twist - lastTwist) / dt) : 0.0f;
    lastTwist = l.twist;
    roll = 0.004f * std::sin(l.twist) + std::clamp(twistSpeed * 0.01f, -0.04f, 0.04f);
    if (drops >= 0 && s.drops > drops)
        worldShiftTo += 1.0f;
    drops = s.drops;
    if (chordChanges >= 0 && s.chordChanges > chordChanges)
        seedShiftTo += 0.35f;
    chordChanges = s.chordChanges;
    worldShift += (worldShiftTo - worldShift) * float(1.0 - std::exp(-dt / 0.35));
    seedShift += (seedShiftTo - seedShift) * float(1.0 - std::exp(-dt / 0.6));
    // Kept small: only where it is between two worlds matters.
    if (worldShiftTo > kWorlds * 4) {
        worldShiftTo -= kWorlds * 4;
        worldShift -= kWorlds * 4;
    }
    // A float in the shader keeps a millimetre's precision only so far out:
    // back by a multiple of the mandelbox's, crystals' and city's repeats
    // (the tunnel's twist and the lattice may jump once, every half hour).
    if (travel > 6300)
        travel -= 6300;
}

int marchSteps(int quality) {
    static const int steps[] = {48, 80, 110, 150};
    return steps[std::clamp(quality, 0, 3)];
}

float sceneScale(int quality) {
    static const float scale[] = {0.33f, 0.5f, 0.7f, 1.0f};
    return scale[std::clamp(quality, 0, 3)];
}

std::vector<Uniform> uniforms(const Signals &s, const Settings &set, const Hand &h, const Drive &d, float width,
                              float height, float crt) {
    const Look &l = set.look;
    float world = std::fmod(l.world + d.worldShift, kWorlds);
    if (world < 0)
        world += kWorlds;
    // Seconds, kept short enough for a float to stay precise.
    const float time = float(std::fmod(s.time, 3600.0));
    std::vector<Uniform> u = {
        {"res", {width, height, time, float(std::fmod(s.beat, 1024.0))}},
        {"drums", {s.kick, s.snare, s.hat, s.perc}},
        {"drums2", {s.sub, float(s.snares % 1000), float(s.kicks % 1000), s.master}},
        {"bass", {s.bass, s.bassPitch, s.bassFilter, s.bassEnv}},
        {"chord", {s.chordHue, s.chordFlash, s.chords, float(s.chordTones)}},
        {"lead", {s.lead, s.leadPitch, s.leadBright, s.leadBend}},
        {"arp", {s.arp, s.arpPitch, s.arpFlash, s.arpRate}},
        {"atmos", {s.atmos, s.swell, s.halo, s.atmosTone}},
        {"fx", {s.build, s.drop, s.sweep, s.wash}},
        {"vocal", {s.vocal, s.vocalPitch, s.clarity, s.vocalLoops}},
        {"levels0", {s.level[Drums], s.level[Bass], s.level[Chords], s.level[Lead]}},
        {"levels1", {s.level[Arp], s.level[Atmos], s.level[Fx], s.level[Vocal]}},
        {"morph0", {world, l.fold, l.symmetry, l.seed + d.seedShift}},
        {"morph1", {l.yaw, l.pitch, l.twist, l.zoom}},
        {"morph2", {float(d.travel), l.warp, l.hue, h.chaos}},
        {"morph3", {float(marchSteps(set.quality)), clamp01(h.pressure * float(h.count > 0)), h.pressure, float(h.count)}},
        {"roll", {d.roll, l.trails, set.markers ? 1.0f : 0.0f, crt}},
    };
    static const char *const fingers[] = {"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9"};
    for (int i = 0; i < 10; ++i) {
        const Hand::Finger &f = h.fingers[size_t(i)];
        u.push_back({fingers[i], {f.x, f.y, f.pressure, f.alive}});
    }
    static const char *const notes[] = {"n0", "n1", "n2", "n3"};
    for (int i = 0; i < kNotes; ++i) {
        const NoteEvent &n = s.notes[i];
        u.push_back({notes[i], {float(std::min(s.time - n.t, 100.0)), n.pitch, n.vel, float(n.kind)}});
    }
    return u;
}

}
