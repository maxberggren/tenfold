#pragma once

// What the Sampler page and its engine share: a whole song kept in memory
// (and its stems, once they are separated), the beat grid that says where
// in it every beat falls, the stretcher that plays any stretch of it at
// the studio's tempo without changing its pitch, and the layout that puts
// the song on the pad as chops.
//
// The page loads a song on a thread of its own and publishes it in the
// Bank; the engine, on the audio thread, only ever reads what is published.
// A song is never changed once it is there (stems are added to it, each
// whole, by one pointer), and never freed while a voice plays it.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "tuning.h"

namespace tf::sampler {

constexpr int kSounds = 4; // songs kept at once, as handles 1..kSounds
enum Stem : int { Mix = 0, Drums, Bass, Vocals, Other, kStems };
inline const char *stemName(int s) {
    static const char *names[] = {"MIX", "DRUMS", "BASS", "VOCALS", "OTHER"};
    return names[std::clamp(s, 0, int(kStems) - 1)];
}
// The stems' files, as the separator writes them (the mix has none).
inline const char *stemFile(int s) {
    static const char *names[] = {"", "drums", "bass", "vocals", "other"};
    return names[std::clamp(s, 0, int(kStems) - 1)];
}

constexpr int kVoices = 12;
constexpr int kDecim = 4;    // the stretcher looks for its splices in the song at a quarter of the rate
constexpr int kHotHop = 64;  // samples per entry of the transient map (1.3 ms)
constexpr int kPeakHop = 256; // samples per entry of the drawn waveform
constexpr double kMaxSeconds = 15 * 60; // a longer file is cut here

// One stem of a song (or the song itself): the audio, and what is worked
// out from it once, when it is loaded, so the audio thread never has to.
struct Pcm {
    int64_t frames = 0;
    std::vector<int16_t> data;  // interleaved stereo
    std::vector<float> low;     // mono, the mean of every kDecim samples
    std::vector<uint8_t> hot;   // per kHotHop samples: a transient begins here (or just has)
    std::vector<int8_t> peaks;  // per kPeakHop samples: the lowest and highest sample, as bytes

    // Fills in everything but `data`.
    void prepare() {
        frames = int64_t(data.size() / 2);
        const float k = 1.0f / (2.0f * 32768.0f);
        low.assign(size_t(frames / kDecim + 1), 0.0f);
        for (int64_t i = 0; i < frames / kDecim; ++i) {
            float s = 0;
            for (int j = 0; j < kDecim; ++j)
                s += float(data[size_t(2 * (i * kDecim + j))]) + float(data[size_t(2 * (i * kDecim + j) + 1)]);
            low[size_t(i)] = s * k / kDecim;
        }
        peaks.assign(size_t(2 * (frames / kPeakHop + 1)), 0);
        for (int64_t i = 0; i * kPeakHop < frames; ++i) {
            int lo = 0, hi = 0;
            const int64_t end = std::min(frames, (i + 1) * kPeakHop);
            for (int64_t n = i * kPeakHop; n < end; ++n) {
                const int v = (int(data[size_t(2 * n)]) + int(data[size_t(2 * n + 1)])) / 2;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            peaks[size_t(2 * i)] = int8_t(std::clamp(lo / 256, -128, 127));
            peaks[size_t(2 * i + 1)] = int8_t(std::clamp(hi / 256, -128, 127));
        }
        // Transients: where a sound begins. Two ways of hearing one: the
        // highs (the difference of neighbouring samples, where an attack
        // shows first and a bass note doesn't) rising well above what
        // they were over the last 30 ms, or the whole level doing so over
        // a longer stretch, which catches a soft kick with no click in it
        // and is too slow to be fooled by a low note's own wave. The
        // stretcher never repeats, skips or blurs what is marked here.
        const int64_t hops = frames / kHotHop;
        std::vector<float> high(size_t(hops + 1), 0.0f), all(size_t(hops + 1), 0.0f);
        for (int64_t h = 0; h < hops; ++h) {
            float sd = 0, sx = 0;
            for (int64_t n = std::max<int64_t>(1, h * kHotHop); n < (h + 1) * kHotHop; ++n) {
                const float x = (float(data[size_t(2 * n)]) + float(data[size_t(2 * n + 1)])) * k;
                const float x1 = (float(data[size_t(2 * n - 2)]) + float(data[size_t(2 * n - 1)])) * k;
                sd += (x - x1) * (x - x1);
                sx += x * x;
            }
            high[size_t(h)] = sd;
            all[size_t(h)] = sx;
        }
        hot.assign(size_t(hops + 1), 0);
        // now: the hops just heard; before: the stretch they are compared
        // with; floor: under this (about −60 dB) it is the room, not a drum.
        auto mark = [&](const std::vector<float> &e, int now, int before, float rise, float floor) {
            double a = 0, b = 0;
            int run = 0;
            for (int64_t h = 0; h < hops; ++h) {
                a += e[size_t(h)];
                if (h >= now) {
                    a -= e[size_t(h - now)];
                    b += e[size_t(h - now)];
                }
                if (h >= now + before)
                    b -= e[size_t(h - now - before)];
                if (h < now)
                    continue;
                const bool rising = a > rise * (b / before * now) + double(floor) * now;
                // The first hop of a rise is where the sound begins.
                if (rising && run == 0)
                    for (int64_t m = std::max<int64_t>(0, h - 1 - now / 4); m <= std::min(hops, h + 3); ++m)
                        hot[size_t(m)] = 1;
                run = rising ? run + 1 : 0;
            }
        };
        mark(high, 4, 24, 5.0f, 4e-8f * kHotHop);
        mark(all, 12, 48, 3.0f, 1e-6f * kHotHop);
    }

    // Whether a transient lies between two sample positions.
    bool hotBetween(double a, double b) const {
        if (hot.empty())
            return false;
        const int64_t h0 = std::max<int64_t>(0, int64_t(std::floor(std::min(a, b) / kHotHop)));
        const int64_t h1 = std::min<int64_t>(int64_t(hot.size()) - 1, int64_t(std::floor(std::max(a, b) / kHotHop)));
        for (int64_t h = h0; h <= h1; ++h)
            if (hot[size_t(h)])
                return true;
        return false;
    }

    // The sound at a position between samples: a cubic through the four
    // around it, silence outside the file.
    void read(double pos, float &l, float &r) const {
        const double fl = std::floor(pos);
        const int64_t i = int64_t(fl);
        const float t = float(pos - fl);
        constexpr float k = 1.0f / 32768.0f;
        if (i >= 1 && i + 2 < frames) {
            const int16_t *p = data.data() + 2 * (i - 1);
            if (t == 0.0f) {
                l = float(p[2]) * k;
                r = float(p[3]) * k;
                return;
            }
            const float t2 = t * t, t3 = t2 * t;
            const float w0 = -0.5f * t3 + t2 - 0.5f * t, w1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
            const float w2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t, w3 = 0.5f * t3 - 0.5f * t2;
            l = (w0 * float(p[0]) + w1 * float(p[2]) + w2 * float(p[4]) + w3 * float(p[6])) * k;
            r = (w0 * float(p[1]) + w1 * float(p[3]) + w2 * float(p[5]) + w3 * float(p[7])) * k;
            return;
        }
        auto at = [&](int64_t n, int ch) { return n >= 0 && n < frames ? float(data[size_t(2 * n + ch)]) * k : 0.0f; };
        l = at(i, 0) + (at(i + 1, 0) - at(i, 0)) * t;
        r = at(i, 1) + (at(i + 1, 1) - at(i, 1)) * t;
    }
};

// A song as the engine plays it. Positions in it are counted in beats of
// its own grid: beat b is at sample at(b), so a voice that moves through
// the beats at the studio's tempo plays the song in time, whatever tempo
// it was made at and however that tempo wandered.
struct Sound {
    double rate = 48000;
    int64_t frames = 0;
    // Each stem is published whole, once, by its pointer; the mix is there
    // from the start. Shared, because a song given a new grid (its tempo
    // doubled or halved) is a new Sound over the same audio.
    std::shared_ptr<const Pcm> own[kStems];
    std::atomic<const Pcm *> pcm[kStems] = {};
    std::vector<double> beats; // the sample every beat falls on, at least two
    bool matched = true;       // a beat was found: it plays at the studio's tempo (else as it is)
    double bpm = 120;
    int keyRoot = 0;           // C = 0
    bool keyMinor = false;
    bool keyKnown = false;     // sure enough of the key to move it to the studio's
    double cents = 0;          // how far its tuning is from A = 440
    mutable std::atomic<int> uses{0}; // voices playing it (the engine counts)

    int count() const { return int(beats.size()); }
    // The grid carries on past both ends at the tempo it had there.
    double at(double beat) const {
        const int n = count();
        const int i = std::clamp(int(std::floor(beat)), 0, n - 2);
        return beats[size_t(i)] + (beat - i) * (beats[size_t(i + 1)] - beats[size_t(i)]);
    }
    double perBeat(double beat) const {
        const int i = std::clamp(int(std::floor(beat)), 0, count() - 2);
        return beats[size_t(i + 1)] - beats[size_t(i)];
    }
    double beatOf(double sample) const {
        const int n = count();
        int i = int(std::upper_bound(beats.begin(), beats.end(), sample) - beats.begin()) - 1;
        i = std::clamp(i, 0, n - 2);
        return i + (sample - beats[size_t(i)]) / (beats[size_t(i + 1)] - beats[size_t(i)]);
    }
    // The key's notes, as the tonic of the major key that has them (a
    // minor key and its relative major are the same notes).
    int signature() const { return (keyRoot + (keyMinor ? 3 : 0)) % 12; }
};

// The same for the studio's key: every scale family's notes sit closest
// to some major key's.
inline int signatureOf(const Tuning &t) {
    const ScaleFamily &f = family(t.familyIndex());
    const int rotation = f.brightness[size_t(std::clamp(t.brightnessIndex(), 0, f.modeCount - 1))];
    // The parent scale's own tonic, then from it to that major key: the
    // harmonic minor is a minor key (its relative major a minor third up),
    // the In scale's notes are those of the major key a minor sixth up.
    static const int toMajor[] = {0, 0, 3, 8, 0};
    const int parent = t.root() - int(std::lround(f.cents[size_t(rotation)] / 100.0));
    const int fam = std::clamp(t.familyIndex(), 0, 4);
    return (((parent + toMajor[fam]) % 12) + 12) % 12;
}

// Semitones that bring a song into the studio's key by the shortest way,
// up to a tritone down or just under one up.
inline int keyShift(const Tuning &studio, const Sound &s) {
    if (!s.keyKnown)
        return 0;
    return ((signatureOf(studio) - s.signature() + 6 + 120) % 12) - 6;
}

// How far the song's tempo is folded to sit near the studio's: halved or
// doubled when that is closer than its own, so a drum and bass break at
// 170 sits on a 90 beat as two of its beats to each of the studio's, at
// nearly its own speed, rather than slowed to half. songBpm × fold is the
// tempo it is heard at; 1 / fold of its beats go by in one of the studio's.
inline double foldFor(double songBpm, double studioBpm) {
    double fold = 1;
    if (!(songBpm > 0) || !(studioBpm > 0))
        return fold;
    while (songBpm * fold > studioBpm * 1.42)
        fold *= 0.5;
    while (songBpm * fold < studioBpm / 1.42)
        fold *= 2;
    return fold;
}

// Where the page publishes its songs for the engine.
struct Bank {
    std::atomic<const Sound *> sounds[kSounds + 1] = {};
    std::atomic<uint32_t> epoch{0}; // counts the engine's renders, so the page knows when a song it took away is let go of
};

// Plays a song at any speed without changing its pitch (or at any pitch
// without changing its speed), in the time domain: a playhead reads the
// song as it is, and every 50 ms or so, when it has fallen behind or run
// ahead of where it should be, it jumps to a place nearby that sounds like
// where it is, cross-fading over a few milliseconds. Most of what is heard
// is the song untouched. Three things keep it from the usual faults of the
// method:
//
//   * a jump never repeats, skips or blurs a transient (Pcm::hot), so no
//     drum is doubled, dropped or softened;
//   * just before a transient it makes one more small jump, aimed so that
//     the transient itself comes out exactly on time: the drums of a
//     stretched song stay on the beat to a millisecond or two, and the
//     hit covers the splice;
//   * it jumps only between places of the same loudness, so the dying
//     ring of a drum is never played twice (which the ear hears as a
//     second, softer hit).
//
// At the song's own tempo and pitch there is nothing to correct, and the
// song plays sample for sample.
class Stretcher {
public:
    static constexpr int kSequence = 2400;  // output samples between jumps (50 ms)
    static constexpr int kOverlap = 432;    // their cross-fade (9 ms)
    static constexpr int kSeek = 336;       // how far either side of the ideal place one may land (7 ms)
    static constexpr int kNearOverlap = 240;  // a shorter fade (5 ms), to fit before the hit
    static constexpr int kNearSeek = 96;      // and a closer aim (2 ms): the hit's timing matters more
    static constexpr int kGuard = 48;       // clear of a transient by this much (1 ms)
    static constexpr int kRetry = 96;       // a jump put off is tried again this soon
    static constexpr int kLeast = 36;       // a correction smaller than this (0.75 ms) is not worth a splice
    static constexpr int kMost = 1920;      // behind or ahead by this (40 ms), it jumps whatever is there

    void start(const Pcm *pcm, double pos) {
        m_pcm = pcm;
        m_head = pos;
        m_fade = 0;
        m_since = m_wait = 0;
        m_jumps = 0;
    }
    // A loop going round: to `pos`, over a short cross-fade.
    void jump(double pos, int overlap) {
        m_old = m_head;
        m_head = pos;
        m_fade = m_fadeLen = std::max(1, overlap);
        m_since = 0;
    }
    double head() const { return m_head; }
    int jumps() const { return m_jumps; }

    // n frames into l and r. `nominal` is where in the song the first of
    // them should come from, `speed` how many of the song's samples go by
    // per frame (the tempo), `rate` how fast the playhead reads them (the
    // pitch: 1 as recorded).
    void render(float *l, float *r, int n, double nominal, double speed, double rate) {
        if (!m_pcm) {
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
            return;
        }
        if (m_wait > 0)
            m_wait -= n;
        if (m_fade == 0 && (speed != rate || std::fabs(nominal - m_head) >= kLeast))
            consider(nominal, speed, rate, n);
        for (int i = 0; i < n; ++i) {
            float a, b;
            m_pcm->read(m_head, a, b);
            if (m_fade > 0) {
                float oa, ob;
                m_pcm->read(m_old, oa, ob);
                // Equal gain: the two are the same sound, lined up.
                const float w = float(m_fade) / float(m_fadeLen);
                a += (oa - a) * w;
                b += (ob - b) * w;
                m_old += rate;
                --m_fade;
            }
            l[i] = a;
            r[i] = b;
            m_head += rate;
        }
        m_since += n;
    }

private:
    // Where the next transient begins at or after `from`, within `reach`
    // samples, or −1; inside is set when `from` is in one already.
    double nextHot(double from, double reach, bool &inside) const {
        const std::vector<uint8_t> &hot = m_pcm->hot;
        const int64_t n = int64_t(hot.size());
        const int64_t h0 = int64_t(std::floor(from / kHotHop));
        inside = h0 >= 0 && h0 < n && hot[size_t(h0)];
        const int64_t h1 = std::min(n - 1, int64_t(std::floor((from + reach) / kHotHop)));
        for (int64_t h = std::max<int64_t>(1, h0 + 1); h <= h1; ++h)
            if (hot[size_t(h)] && !hot[size_t(h - 1)])
                return double(h * kHotHop);
        return -1;
    }
    // Where the last transient before `from` ends, within `reach`, or −1.
    double lastHot(double from, double reach) const {
        const std::vector<uint8_t> &hot = m_pcm->hot;
        const int64_t n = int64_t(hot.size());
        const int64_t h0 = std::min(n - 1, int64_t(std::floor(from / kHotHop)));
        const int64_t h1 = std::max<int64_t>(0, int64_t(std::floor((from - reach) / kHotHop)));
        for (int64_t h = h0; h >= h1; --h)
            if (hot[size_t(h)])
                return double((h + 1) * kHotHop);
        return -1;
    }

    // The playhead isn't where it should be: jump, if this is the moment
    // and there is a good place to land.
    void consider(double nominal, double speed, double rate, int block) {
        const double drift = nominal - m_head;
        const bool force = std::fabs(drift) >= kMost;
        bool inside = false;
        // Far enough ahead to see a transient coming in time to splice
        // before it, and far enough back to know what a jump would repeat.
        const double ahead = (kOverlap + kSequence) * rate;
        const double hit = nextHot(m_head, ahead, inside);
        if (inside && !force)
            return; // the transient plays through untouched

        // Just before a transient: the last chance to put it on time.
        const double nearSpan = kNearOverlap * rate + kGuard;
        // Where to land so that the transient comes out on time, given how
        // the playhead goes on drifting until it gets there. The moment
        // for it is the last at which both the place the playhead leaves
        // and the place it lands are clear of the transient.
        const double aim = hit >= 0 && speed > 1e-9 ? hit - (hit - nominal) * rate / speed : nominal;
        const double lead = hit - std::max(m_head, aim);
        const bool near = hit >= 0 && lead <= nearSpan + 4.0 * block * rate && lead >= nearSpan;
        // The further from its own tempo, the sooner the next jump: each
        // corrects about 6 ms, never more often than every 20. With a
        // transient coming, the jump before it does the correcting: one
        // here would only be in its way.
        const double off = std::fabs(speed / std::max(1e-9, rate) - 1.0);
        const int sequence = int(std::clamp(288.0 / std::max(off, 1e-9), 960.0, double(kSequence)));
        if (!force && !near) {
            // (A jump put off waits a moment before it is tried again; the
            // one before a transient never waits: its moment is short.)
            if (m_since < sequence || m_wait > 0)
                return;
            if (hit >= 0 && lead < (kOverlap + kNearOverlap + 6.0 * block) * rate + 3.0 * kGuard)
                return;
        }
        const int overlap = near ? kNearOverlap : kOverlap;
        const double span = overlap * rate;
        // The old playhead fades out over the next `span` of the song: it
        // must be done before the transient.
        if (!force && hit >= 0 && m_head + span + kGuard > hit)
            return;
        // Where to land: usually where the song should be now.
        const double ideal = near ? aim : nominal;
        double lo = -1e18, hi = 1e18;
        if (!force) {
            const double back = lastHot(m_head, kMost + kSeek + 2.0 * kHotHop);
            if (back >= 0)
                lo = back + kGuard;      // nothing of the last transient again
            if (hit >= 0)
                hi = hit - span - kGuard; // and the fade-in over before the next
            if (lo > hi)
                return retry();
        }
        const int seek = near ? kNearSeek : kSeek;
        double from = std::max(ideal - seek, lo), to = std::min(ideal + seek, hi);
        if (from > to)
            from = to = std::clamp(ideal, lo, hi); // not allowed there: as near as may be

        const std::vector<float> &low = m_pcm->low;
        const int64_t n = int64_t(low.size());
        const int64_t a0 = int64_t(std::llround(m_head / kDecim));
        const int w = overlap / kDecim;
        double best = -1e30, bestAt = std::clamp(ideal, from, to);
        bool found = false;
        if (a0 >= 0 && a0 + w < n) {
            const float *x = low.data() + a0;
            float xx = 1e-9f;
            for (int i = 0; i < w; ++i)
                xx += x[i] * x[i];
            const float quiet = 1e-7f * float(w); // −70 dB: silence matches silence
            const int64_t b0 = int64_t(std::ceil(from / kDecim)), b1 = int64_t(std::floor(to / kDecim));
            for (int64_t b = std::min(b0, b1); b <= std::max(b0, b1); ++b) {
                if (b < 0 || b + w >= n)
                    continue;
                const float *y = low.data() + b;
                float xy = 0, yy = 1e-9f;
                for (int i = 0; i < w; ++i) {
                    xy += x[i] * y[i];
                    yy += y[i] * y[i];
                }
                // The same loudness (within 3 dB), or it is no place to
                // land: a jump back up a dying ring is heard as a hit.
                // (Before a transient the hit's timing comes first: 6 dB.)
                const float within = near ? 4.0f : 2.0f;
                if (!force && !(xx < quiet && yy < quiet) && (yy > within * xx || xx > within * yy))
                    continue;
                // How alike the two are, a little in favour of the ideal
                // place: silence and noise, where nothing matches, land on it.
                const double score = 2.0 * double(xy) / double(xx + yy) - 2e-4 * std::fabs(double(b * kDecim) - ideal);
                if (score > best) {
                    best = score;
                    bestAt = double(b * kDecim);
                    found = true;
                }
            }
        } else {
            found = true; // off the end of the song: silence, land anywhere
        }
        if (!found)
            return retry();
        // To the sample: the quarter-rate search is good to four.
        if (best > -1e29) {
            const int64_t a = int64_t(std::llround(m_head)), c = int64_t(std::llround(bestAt));
            constexpr int kFine = 192;
            if (a >= 0 && a + kFine < m_pcm->frames && c - 3 >= 0 && c + 3 + kFine < m_pcm->frames) {
                const int16_t *d = m_pcm->data.data();
                float fineBest = -1e30f;
                for (int s = -3; s <= 3; ++s) {
                    float xy = 0, yy = 1.0f;
                    for (int i = 0; i < kFine; ++i) {
                        const float x = float(d[2 * (a + i)]), y = float(d[2 * (c + s + i)]);
                        xy += x * y;
                        yy += y * y;
                    }
                    const float score = xy / std::sqrt(yy);
                    if (score > fineBest) {
                        fineBest = score;
                        bestAt = double(c + s);
                    }
                }
            }
        }
        // Worth it only if it brings the playhead closer to where it
        // should be by more than a splice costs.
        if (std::fabs(ideal - bestAt) > std::fabs(ideal - m_head) - kLeast)
            return retry();
        m_old = m_head;
        m_head = bestAt;
        m_fade = m_fadeLen = overlap;
        m_since = 0;
        ++m_jumps;
    }
    // Put off: asked again in a couple of milliseconds.
    void retry() { m_wait = kRetry; }

    const Pcm *m_pcm = nullptr;
    double m_head = 0, m_old = 0;
    int m_fade = 0, m_fadeLen = 1;
    int m_since = 0, m_wait = 0;
    int m_jumps = 0;
};

// --- the layout -----------------------------------------------------------------

// What the analysis says about each beat of a song, kept by the page (the
// engine needs only the grid).
struct Marks {
    int downbeat = 0;             // beats[downbeat + 4k] are the bar lines
    std::vector<float> strength;  // per beat: how strong a sound lands on it
    std::vector<float> sub;       // 4 per beat: the same at the beat and its three sixteenths
    std::vector<double> attack;   // per beat: the sample where that sound begins
    std::vector<float> change;    // per bar: how much the music changes at its bar line
};

// How the song is cut. Auto cuts where something lands: every bar line,
// and every beat with a sound starting on it, so a held chord stays whole
// and a drum beat falls into its hits.
enum Size : int { Auto = 0, Half, Beat, Two, Bar, TwoBars, kSizes };
inline const char *sizeName(int s) {
    static const char *names[] = {"AUTO", "1/2 BEAT", "BEAT", "2 BEATS", "BAR", "2 BARS"};
    return names[std::clamp(s, 0, int(kSizes) - 1)];
}
constexpr int kRows = 5; // rows of the song on the pad at once

struct Chop {
    double start = 0, end = 0; // in beats of the song's grid, the start at its attack
    int at = 0;      // where it begins, in half beats of the grid (may be before the song)
    int length = 0;  // half beats
    int row = 0;     // of the whole song, kRows to a page
    float x0 = 0, x1 = 0; // across its row, 0..1
    // What a trim of it is remembered under: this very cut of the song.
    int64_t key() const { return (int64_t(at) + (1 << 20)) * 256 + length; }
};

struct Layout {
    int size = Auto;
    int origin = 0;    // the beat at the first row's left edge
    int rowBeats = 16;
    int rows = 1;
    std::vector<Chop> chops;   // in order
    std::vector<int> rowFirst; // each row's first chop, and one past the last
    int pages() const { return std::max(1, (rows + kRows - 1) / kRows); }
    // The chop under a place in a row, or −1.
    int find(int row, float x) const {
        if (row < 0 || row >= rows)
            return -1;
        for (int i = rowFirst[size_t(row)]; i < rowFirst[size_t(row + 1)]; ++i)
            if (x >= chops[size_t(i)].x0 && x < chops[size_t(i)].x1)
                return i;
        return -1;
    }
};

// Lays a song out in rows of whole bars (four to a row; more for longer
// chops, two for half beats, so a row never holds more chops than fingers
// can tell apart). The rows start where the song's sections do, as far as
// the analysis heard them: of the ways to group the bars into rows, the
// one whose rows begin on the most change. barShift moves the bar lines
// by so many beats, for a song whose one was heard in the wrong place.
inline Layout layOut(const Sound &s, const Marks &m, int size, int barShift = 0) {
    Layout out;
    out.size = std::clamp(size, 0, int(kSizes) - 1);
    static const int rowBeatsFor[kSizes] = {16, 8, 16, 16, 32, 64};
    static const int halvesFor[kSizes] = {0, 1, 2, 4, 8, 16};
    out.rowBeats = rowBeatsFor[out.size];
    const int n = s.count();
    if (n < 2 || s.frames <= 0)
        return out;
    const int down = (((m.downbeat + barShift) % 4) + 4) % 4;
    // The rows' phase against the bars: where the changes are.
    const int rowBars = out.rowBeats / 4;
    int phase = 0;
    double most = -1;
    for (int o = 0; o < rowBars; ++o) {
        double sum = 0;
        for (size_t k = size_t(o); k < m.change.size(); k += size_t(rowBars))
            sum += m.change[k];
        // The bar-line shift moves the bars the changes were counted in;
        // near enough for choosing among a few phases.
        if (sum > most + 1e-9) {
            most = sum;
            phase = o;
        }
    }
    // The last row start at or before the song's first sample. (A sliver
    // of the song before a beat, under a quarter of one, is that beat's
    // own lead-in, not a bar to give a row to.)
    constexpr double kSliver = 0.25;
    const double first = s.beatOf(0), last = s.beatOf(double(s.frames));
    int origin = down + 4 * phase;
    while (origin > first + kSliver)
        origin -= out.rowBeats;
    while (origin + out.rowBeats <= first + kSliver)
        origin += out.rowBeats;
    out.origin = origin;
    out.rows = std::max(1, int(std::ceil((last - kSliver - origin) / out.rowBeats)));

    // Where a sound lands on a beat, Auto cuts.
    float threshold = 0;
    if (out.size == Auto && !m.strength.empty()) {
        std::vector<float> heard;
        for (float v : m.strength)
            if (v > 0.02f)
                heard.push_back(v);
        if (!heard.empty()) {
            std::nth_element(heard.begin(), heard.begin() + long(heard.size() / 2), heard.end());
            threshold = std::max(0.12f, 0.55f * heard[heard.size() / 2]);
        } else {
            threshold = 1e9f;
        }
    }
    auto lands = [&](int beat) {
        return beat >= 0 && beat < int(m.strength.size()) && m.strength[size_t(beat)] >= threshold;
    };
    // Where the cut for a place on the grid really goes: at the foot of
    // the sound that lands there, a little before the beat.
    auto cut = [&](int half) {
        const double nominal = half * 0.5;
        if (half % 2 == 0) {
            const int beat = half / 2;
            if (beat >= 0 && beat < int(m.attack.size())) {
                const double a = s.beatOf(m.attack[size_t(beat)]);
                if (a <= nominal + 0.05 && a >= nominal - 0.2)
                    return a;
            }
            return nominal;
        }
        // Between the beats the analysis marks no attacks: the transient
        // map says where one begins, if one does within 20 ms.
        const Pcm *pcm = s.pcm[Mix].load(std::memory_order_acquire);
        if (!pcm || pcm->hot.empty())
            return nominal;
        const double centre = s.at(nominal);
        const int64_t h0 = int64_t(centre / kHotHop), reach = int64_t(0.02 * s.rate / kHotHop);
        for (int64_t d = 0; d <= reach; ++d)
            for (int64_t h : {h0 - d, h0 + d}) {
                if (h < 1 || h >= int64_t(pcm->hot.size()) || !pcm->hot[size_t(h)] || pcm->hot[size_t(h - 1)])
                    continue;
                return s.beatOf(double(h * kHotHop));
            }
        return nominal;
    };

    const int rowHalves = out.rowBeats * 2;
    for (int row = 0; row < out.rows; ++row) {
        out.rowFirst.push_back(int(out.chops.size()));
        const int h0 = (origin + row * out.rowBeats) * 2;
        int at = h0;
        while (at < h0 + rowHalves) {
            int len = halvesFor[out.size];
            if (out.size == Auto) {
                // To the next beat a sound lands on, or the bar line.
                len = 2;
                while ((at + len - h0) % 8 != 0 && !lands((at + len) / 2))
                    len += 2;
            }
            len = std::min(len, h0 + rowHalves - at);
            Chop c;
            c.at = at;
            c.length = len;
            c.row = row;
            c.x0 = float(at - h0) / float(rowHalves);
            c.x1 = float(at + len - h0) / float(rowHalves);
            // Only what holds some of the song.
            if ((at + len) * 0.5 > first + kSliver && at * 0.5 < last - kSliver)
                out.chops.push_back(c);
            at += len;
        }
    }
    out.rowFirst.push_back(int(out.chops.size()));
    for (size_t i = 0; i < out.chops.size(); ++i) {
        Chop &c = out.chops[i];
        c.start = std::max(cut(c.at), first);
        // It ends where the next begins (at that one's attack, so none of
        // it is in this one), or at its own bar's end.
        const bool joined = i + 1 < out.chops.size() && out.chops[i + 1].at == c.at + c.length;
        c.end = joined ? std::max(cut(c.at + c.length), first) : std::min((c.at + c.length) * 0.5, last);
        if (c.end < c.start + 0.02)
            c.end = c.start + 0.02;
    }
    return out;
}

// --- files ----------------------------------------------------------------------

// Reads a WAV (16- or 24-bit PCM or 32-bit float, any number of channels,
// the first two kept, one doubled) as interleaved 16-bit stereo, with its
// rate. False when it isn't one of those.
inline bool readWavStereo(const std::string &path, std::vector<int16_t> &out, int &rate, double maxSeconds = kMaxSeconds) {
    out.clear();
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    auto u32 = [](const uint8_t *b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; };
    uint8_t head[12];
    if (std::fread(head, 1, 12, f) != 12 || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4)) {
        std::fclose(f);
        return false;
    }
    int format = 0, channels = 0, bits = 0;
    rate = 0;
    bool ok = false;
    uint8_t ch[8];
    while (std::fread(ch, 1, 8, f) == 8) {
        const uint32_t size = u32(ch + 4);
        if (!std::memcmp(ch, "fmt ", 4) && size >= 16 && size <= 4096) {
            std::vector<uint8_t> body(size);
            if (std::fread(body.data(), 1, size, f) != size)
                break;
            format = body[0] | body[1] << 8;
            channels = body[2] | body[3] << 8;
            rate = int(u32(body.data() + 4));
            bits = body[14] | body[15] << 8;
            if (format == 0xFFFE && size >= 26)
                format = body[24] | body[25] << 8;
            if (size & 1)
                std::fgetc(f);
        } else if (!std::memcmp(ch, "data", 4)) {
            if (channels <= 0 || rate <= 0 || !((format == 1 && (bits == 16 || bits == 24)) || (format == 3 && bits == 32)))
                break;
            const size_t bytes = size_t(bits / 8), step = bytes * size_t(channels);
            // A streamed file says its data is as long as can be: read to the end.
            size_t frames = std::min(size_t(size) / step, size_t(maxSeconds * rate));
            out.resize(frames * 2);
            std::vector<uint8_t> buf(step * 4096);
            size_t done = 0;
            while (done < frames) {
                const size_t want = std::min<size_t>(4096, frames - done);
                const size_t got = std::fread(buf.data(), step, want, f);
                for (size_t i = 0; i < got; ++i)
                    for (int c = 0; c < 2; ++c) {
                        const uint8_t *b = buf.data() + i * step + size_t(std::min(c, channels - 1)) * bytes;
                        int v = 0;
                        if (format == 3) {
                            float x;
                            std::memcpy(&x, b, 4);
                            v = int(std::lrint(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
                        } else if (bits == 16) {
                            v = int16_t(uint16_t(b[0] | b[1] << 8));
                        } else {
                            v = int32_t(uint32_t(b[0]) << 8 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 24) >> 16;
                        }
                        out[(done + i) * 2 + size_t(c)] = int16_t(v);
                    }
                done += got;
                if (got < want)
                    break;
            }
            out.resize(done * 2);
            ok = done > 0;
            break;
        } else {
            if (std::fseek(f, long(size + (size & 1)), SEEK_CUR) != 0)
                break;
        }
    }
    std::fclose(f);
    return ok;
}

// Writes interleaved 16-bit stereo as a WAV (the mix handed to the stem
// separator, a test's song).
inline bool writeWavStereo(const std::string &path, const int16_t *data, size_t frames, int rate) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    auto put32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto put16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = uint32_t(frames * 4);
    std::fwrite("RIFF", 1, 4, f);
    put32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(16);
    put16(1);
    put16(2);
    put32(uint32_t(rate));
    put32(uint32_t(rate) * 4);
    put16(4);
    put16(16);
    std::fwrite("data", 1, 4, f);
    put32(bytes);
    const bool ok = std::fwrite(data, 4, frames, f) == frames;
    return std::fclose(f) == 0 && ok;
}

// 16-bit stereo from one rate to another, a line between samples: for a
// WAV at another rate when there is no ffmpeg to do it properly.
inline std::vector<int16_t> resampleStereo(const std::vector<int16_t> &in, int from, int to) {
    if (from == to || in.empty())
        return in;
    const size_t frames = in.size() / 2;
    const double step = double(from) / to;
    std::vector<int16_t> out(size_t(double(frames) / step) * 2);
    for (size_t i = 0; i < out.size() / 2; ++i) {
        const double x = double(i) * step;
        const size_t a = std::min(size_t(x), frames - 1), b = std::min(a + 1, frames - 1);
        const double u = x - double(a);
        for (int c = 0; c < 2; ++c)
            out[2 * i + size_t(c)] = int16_t(std::lrint(in[2 * a + size_t(c)] * (1 - u) + in[2 * b + size_t(c)] * u));
    }
    return out;
}

// A song's own folder in the cache (TENFOLD_SAMPLER_CACHE, or the user's
// cache folder): what was fetched for it, and its stems in stems/ once
// they are made. `source` is the file's path, or the link.
std::string cacheFolder(const std::string &source);

// What the engine publishes for the page (PartView::data).
namespace pub {
enum : int {
    Voices = 0, // how many sound
    Level,      // the part's level, 0..1
    Low,        // the level of its lows (the kick), middle (the snare) and highs (the hats)
    Mid,
    High,
    First = 8,  // then kVoices of these:
    Handle = 0, // the song (0: the voice is free)
    StemOf,
    Start,      // of the pass playing, in the song's beats
    End,
    Pos,        // where it is
    Flags,      // 1 held, 2 from a loop
    Per = 6,
};
}

// The page's own command types and what an On carries.
namespace cmd {
enum : int {
    // On: i[0] the chop (what Space's phrase finder tells apart), i[1] the
    // song's handle | stem << 8 | flags << 16, i[2] and i[3] where it
    // starts and ends, as samples of the song (not beats of its grid, so a
    // loop keeps its sound when the grid is doubled or halved), f[0]
    // level, f[3] semitones up or down.
    FlagCut = 1,      // it cuts off what was let go and still rings
    FlagKeyMatch = 2, // it is moved to the studio's key
    // Another round of a chop a finger holds: the page sends one each time
    // the chop goes round, so Space hears the repeats as playing (one press
    // held for bars is a pattern, not one long note) and a loop holds them.
    // A voice already playing it goes on as it is; none, and it starts.
    FlagRound = 4,
    // Off: i[0] = 1 ends it at once instead of at the chop's end.
    // Move: i[2], i[3] the start and end from the next time round.
    // Param: i[0] which.
    ParamTone = 0,    // f[0] −1 dark .. 1 thin
    ParamLoopStopped = 1, // the part's loop stopped: what it left going round stops
};
}

}
