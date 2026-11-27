#pragma once

// What the sound plays through, speakers or headphones, read from an audio
// graph as PipeWire describes it (the Vocal page keeps its monitor muted on
// speakers: see StudioView::Output). Kept apart from the PipeWire code so
// the tests can hand it graphs as real machines report them.
//
// From the sink the stream is linked to (or the default sink, before the
// link is made):
//
//   a card's sink      its device's active route for the sink's
//                      card.profile.device: port.type "speaker" is
//                      speakers, "headphones" or "headset" headphones
//                      (ALSA's jack detection switches the route, so a
//                      plug pulled moves it back); else the device's
//                      form factor (Bluetooth: "headphone", "headset",
//                      "speaker");
//   a filter or loop   (a sink of a node.link-group: Asahi's speaker DSP,
//                      an equaliser, a loopback) whatever its output
//                      stream plays into, followed a few hops;
//   else its names     "speaker" or "headphone"/"headset" in its name,
//                      description or icon.
//
// Anything else (HDMI, a line out, an interface) is Unknown: nothing is
// muted for it.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "part.h"

namespace tf::outputkind {

using Props = std::map<std::string, std::string>;

struct Route {
    bool output = true;
    int device = -1; // the card.profile.device it is for
    std::string name;
    Props info; // port.type, …
};

struct Graph {
    std::map<uint32_t, Props> nodes;
    std::map<uint32_t, Props> devices;
    std::map<uint32_t, std::vector<Route>> routes; // a device's active routes
    std::vector<std::pair<uint32_t, uint32_t>> links; // output node → input node
    std::string defaultSink; // node.name
};

inline std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline std::string get(const Props &p, const char *key) {
    const auto it = p.find(key);
    return it == p.end() ? std::string() : it->second;
}

inline int ofWords(const std::string &text) {
    const std::string s = lower(text);
    if (s.find("headphone") != std::string::npos || s.find("headset") != std::string::npos)
        return StudioView::OutputHeadphones;
    if (s.find("speaker") != std::string::npos)
        return StudioView::OutputSpeakers;
    return StudioView::OutputUnknown;
}

inline int ofPort(const Route &r) {
    const std::string type = lower(get(r.info, "port.type"));
    if (type == "speaker")
        return StudioView::OutputSpeakers;
    if (type == "headphones" || type == "headset" || type == "handset" || type == "handsfree")
        return StudioView::OutputHeadphones;
    if (!type.empty() && type != "unknown")
        return StudioView::OutputUnknown; // hdmi, spdif, line…
    return ofWords(r.name);
}

inline int ofFormFactor(const std::string &f) {
    const std::string s = lower(f);
    if (s == "headphone" || s == "headset" || s == "hands-free" || s == "handset")
        return StudioView::OutputHeadphones;
    if (s == "speaker")
        return StudioView::OutputSpeakers;
    return StudioView::OutputUnknown;
}

inline int sinkKind(const Graph &g, uint32_t id, int depth) {
    const auto n = g.nodes.find(id);
    if (n == g.nodes.end() || depth > 4)
        return StudioView::OutputUnknown;
    const Props &p = n->second;
    // A card's own sink: the route it plays through.
    auto number = [](const std::string &s) {
        char *end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);
        return end && end != s.c_str() ? v : -1L;
    };
    if (const long d = number(get(p, "device.id")); d >= 0) {
        const long want = number(get(p, "card.profile.device"));
        const auto rs = g.routes.find(uint32_t(d));
        if (rs != g.routes.end() && want >= 0) {
            for (const Route &r : rs->second)
                if (r.output && r.device == want)
                    return ofPort(r);
        }
        const auto dp = g.devices.find(uint32_t(d));
        if (dp != g.devices.end())
            if (const int k = ofFormFactor(get(dp->second, "device.form-factor")); k != StudioView::OutputUnknown)
                return k;
    }
    // A filter's or a loopback's sink: where its other end plays.
    const std::string group = get(p, "node.link-group");
    if (!group.empty()) {
        int found = StudioView::OutputUnknown;
        for (const auto &[oid, op] : g.nodes) {
            if (oid == id || get(op, "node.link-group") != group || get(op, "media.class").rfind("Stream/Output", 0) != 0)
                continue;
            std::vector<uint32_t> next;
            for (const auto &[from, to] : g.links)
                if (from == oid)
                    next.push_back(to);
            if (next.empty()) { // not linked yet: where it asks to go
                const std::string target = get(op, "target.object");
                for (const auto &[tid, tp] : g.nodes)
                    if (!target.empty() && get(tp, "node.name") == target)
                        next.push_back(tid);
            }
            for (uint32_t to : next) {
                const int k = sinkKind(g, to, depth + 1);
                if (k == StudioView::OutputSpeakers)
                    return k;
                if (k == StudioView::OutputHeadphones)
                    found = k;
            }
        }
        if (found != StudioView::OutputUnknown)
            return found;
    }
    return ofWords(get(p, "node.name") + " " + get(p, "node.description") + " " + get(p, "device.icon-name"));
}

// What a stream node plays through: speakers if any sink it reaches is.
inline int classify(const Graph &g, uint32_t stream) {
    std::vector<uint32_t> sinks;
    for (const auto &[from, to] : g.links)
        if (from == stream && std::find(sinks.begin(), sinks.end(), to) == sinks.end())
            sinks.push_back(to);
    if (sinks.empty() && !g.defaultSink.empty())
        for (const auto &[id, p] : g.nodes)
            if (get(p, "node.name") == g.defaultSink)
                sinks.push_back(id);
    int kind = StudioView::OutputUnknown;
    for (uint32_t s : sinks) {
        const int k = sinkKind(g, s, 0);
        if (k == StudioView::OutputSpeakers)
            return k;
        if (k == StudioView::OutputHeadphones)
            kind = k;
    }
    return kind;
}

}
