#pragma once

#include <string>

// Headless ways to play tenfold, for checking it and for agents:
//
//   tenfold render <script.tf> <out.wav> [--json]
//       plays a scripted performance through the real session (every page's
//       controls) and studio, faster than real time, and reports levels,
//       CPU load and what the clips hold.
//   tenfold bench [--json]
//       a dense case: a loop on every part, ten fingers held, timed per
//       audio callback.
//
// The script language, one event per line (times in seconds, positions 0..1
// with y up, twist in degrees; # starts a comment):
//
//   page  T N                                         show page N (1–8, 0 arrange, 9 visual,
//                                                     or by its name: sampler)
//   drop  T FILE                                      a sound file dragged onto the window (the
//                                                     Sampler takes it and is shown), loaded
//                                                     before the script goes on
//   down  T ID X Y [p=0.5] [size=1] [twist=0]
//   move  T ID X Y [p=…] [size=…] [twist=…]           jump there
//   glide T1 T2 ID X Y [p=…] [size=…] [twist=…]       move there smoothly
//   up    T ID
//   tap   T X Y [p=0.5] [len=0.08]                    a quick down and up
//   click T [len=0.1]                                 press the pad down
//   key   T NAME [down|up]                            a key, as Qt names it: A, F1, Up,
//                                                     Ctrl+Up, Shift+Left…; without
//                                                     down or up, a press and release
//   mouse T X Y down|up|move                          the mouse on the pad: pressed, it is
//                                                     one more finger; move alone hovers
//   tempo T BPM
//   voice T DUR [hz=220] [to=HZ] [level=0.3] [vowel=a|e|i|o|u] [vib=0.25]
//                                                     a sung word into the microphone
//                                                     (a glide to `to`, vibrato of vib
//                                                     semitones when held; 0 sings straight)
//   room  T [gain=1] [ms=12] [hz=2500]                from T the microphone hears the mix
//                                                     too, through a room (a resonance
//                                                     at hz) a trip of ms later: speakers,
//                                                     for feedback; gain=0 ends it
//   output T speakers|headphones|unknown              what the sound plays through, as
//                                                     the audio server would say
//   mic   T FILE.wav [gain=1]                         a recording into the microphone from T
//                                                     (with either, the script has a mic;
//                                                     without, it has none)
//   end   T
namespace tf {

struct RenderStats {
    double seconds = 0, renderSeconds = 0;
    double peakDb = -120, rmsDb = -120;
    double meanLoad = 0, maxLoad = 0, p99Load = 0;
    int maxVoices = 0, clips = 0;
    double bpm = 0;
    std::string clipsText; // per part: the slots that hold something
    std::string error;
};

RenderStats renderScript(const std::string &script, const std::string &wavPath);
RenderStats runBench(double seconds);
std::string statsJson(const RenderStats &s);
std::string statsText(const RenderStats &s);

class Session;

// Steps a script into a session one trackpad frame at a time; used by the
// renderer and the screenshot command.
class ScriptPlayer {
public:
    ScriptPlayer();
    ~ScriptPlayer();
    bool load(const std::string &script, std::string *error);
    double end() const;
    // Applies everything due by time t and sends the session one frame.
    void tick(double t, Session &session);

    struct Impl;

private:
    Impl *d;
};

// The clock a session uses offline: script time.
double scriptClock();
void setScriptClock(double t);

}
