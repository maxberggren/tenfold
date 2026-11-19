#pragma once

#include <string>

// Headless ways to play tenfold, for checking it, demoing it, and for agents:
//
//   tenfold render <script.tf> <out.wav> [--json]
//       plays a scripted performance through the real gesture layer and
//       engine, faster than real time, and reports levels and CPU load.
//   tenfold demo <out.wav> [--json]
//       renders the built-in demo performance.
//   tenfold bench [--json]
//       the worst case: ten held voices, frozen layers, a dozen dense loops
//       and every effect up, timed per audio callback.
//
// The script language, one event per line (times in seconds, positions 0..1
// with y up, twist in degrees):
//
//   down  T ID X Y [p=0.6] [size=1] [twist=0]
//   move  T ID X Y [p=…] [size=…] [twist=…]           jump there
//   glide T1 T2 ID X Y [p=…] [size=…] [twist=…]       move there smoothly
//   up    T ID
//   tap   T X Y [p=0.7] [len=0.06]                    a quick down and up
//   click T                                            press the pad
//   key   T dissolve|dissolve-all|root+|root-|family+|family-|just|reset
//   end   T
namespace tf {

struct RenderStats {
    double seconds = 0, renderSeconds = 0;
    double peakDb = -120, rmsDb = -120;
    double meanLoad = 0, maxLoad = 0, p99Load = 0;
    int strikes = 0, loopsMade = 0, freezes = 0, maxVoices = 0, maxLoops = 0;
    std::string error;
};

RenderStats renderScript(const std::string &script, const std::string &wavPath);

class Gesture;

// Steps a script into a gesture layer one trackpad frame at a time; used by
// the renderer and by the screenshot command.
class ScriptPlayer {
public:
    ScriptPlayer();
    ~ScriptPlayer();
    bool load(const std::string &script, std::string *error);
    double end() const;
    // Applies everything due by time t and sends the gesture one frame.
    void tick(double t, Gesture &gesture);

    struct Impl;

private:
    Impl *d;
};
RenderStats runBench(double seconds);
std::string statsJson(const RenderStats &s);
std::string statsText(const RenderStats &s);
const char *demoScript();

}
