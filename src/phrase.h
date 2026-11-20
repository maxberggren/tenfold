#pragma once

// The tap-then-hold loop gesture, shared by every page that wants it: tap a
// rhythm, land on the next beat and stay, and the taps become a loop. The
// rules come from playing it on a real Apple trackpad:
//
//   * a touch shorter than 40 ms is a graze, not part of a rhythm;
//   * taps closer than 60 ms are one moment (a flam, two fingers together);
//   * the hold closes the phrase when it lands about one of the phrase's own
//     intervals after the last tap (±30%: a finger that lands and stays is
//     rarely as tight as a tap), or two of them after an even pulse;
//   * only a hold in the same group (a row, a zone: the page decides) closes;
//   * a phrase is forgotten after a pause no one would hear as part of it.
//
// Pressure and contact size play no part: light players read near zero.

#include <algorithm>
#include <cmath>
#include <vector>

namespace tf {

class Phrase {
public:
    struct Tap {
        double t;
        int group;
    };

    // A finished tap that landed at t and lasted length seconds.
    void tap(double t, double length, int group) {
        if (length < 0.04)
            return;
        m_taps.push_back({t, group});
        while (m_taps.size() > 64 || (m_taps.size() > 1 && t - m_taps.front().t > 8.0))
            m_taps.erase(m_taps.begin());
    }

    // A finger that landed at t in group is still down: does it close the
    // phrase? If so, start is when the phrase began (the loop runs from
    // start to t).
    bool closes(double t, int group, double *start) const {
        if (m_taps.size() < 2)
            return false;
        bool sameGroup = false;
        for (const Tap &p : m_taps)
            sameGroup |= p.group == group;
        if (!sameGroup)
            return false;
        const double gap = t - m_taps.back().t;
        if (gap < 0.1 || gap > 2.2)
            return false;
        std::vector<double> iois;
        for (size_t i = 1; i < m_taps.size(); ++i) {
            const double d = m_taps[i].t - m_taps[i - 1].t;
            if (d >= 0.06)
                iois.push_back(d);
        }
        if (iois.empty())
            return false;
        double mean = 0, var = 0;
        for (double v : iois)
            mean += v;
        mean /= double(iois.size());
        for (double v : iois)
            var += (v - mean) * (v - mean);
        const bool even = iois.size() >= 2 && std::sqrt(var / double(iois.size())) < 0.15 * mean;
        bool ok = false;
        for (double ioi : iois) {
            ok |= std::fabs(gap - ioi) < 0.3 * ioi;
            ok |= even && std::fabs(gap - 2 * ioi) < 0.15 * ioi;
        }
        if (ok && start)
            *start = m_taps.front().t;
        return ok;
    }

    // Forgets the phrase after a long enough pause.
    void expire(double now) {
        if (m_taps.empty())
            return;
        double maxGap = 1.3;
        for (size_t i = 1; i < m_taps.size(); ++i)
            maxGap = std::max(maxGap, (m_taps[i].t - m_taps[i - 1].t) * 2.3);
        if (now - m_taps.back().t > std::min(maxGap, 2.4) + 0.3)
            m_taps.clear();
    }

    void clear() { m_taps.clear(); }
    bool empty() const { return m_taps.empty(); }
    const std::vector<Tap> &taps() const { return m_taps; }
    // When the next beat of the phrase would land, for drawing a target.
    double next() const {
        if (m_taps.size() < 2)
            return 0;
        return m_taps.back().t + (m_taps.back().t - m_taps[m_taps.size() - 2].t);
    }

private:
    std::vector<Tap> m_taps;
};

}
