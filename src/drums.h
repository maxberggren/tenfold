#pragma once

// The drum machine behind the Drums page: sixteen synthesised drums in fifteen
// kits, a small room of their own, and the grooves a held hand plays. The
// page (drums.cpp) turns fingers into hits; this header is everything that
// makes sound or decides rhythm, so the tests can reach it directly.
//
// Every sound is built from oscillators, noise and filters at audio rate:
//
//   kick    a sine falling from several times its note onto it (the punch),
//           a short band of noise for the beater, a soft clipper for weight;
//           tuned to the key.
//   808     the kick's long cousin: held, it sustains and glides between
//           scale degrees, the trap bass.
//   snare   two tuned body modes with a pitch drop, and a band of noise
//           whose length is the snare's snap.
//   clap    four bursts of band-passed noise ten milliseconds apart, then a
//           tail that is different in each ear.
//   hats    six square waves at the classic inharmonic ratios (band-limited,
//           so nothing folds back), band- and high-passed, with some noise
//           for crispness. A closed hat chokes an open one.
//   toms    pitched sines with a drop, tuned to the root, third and fifth.
//   ride    seven inharmonic partials (more of the low ones toward the bell)
//           over a wash of high noise.
//   crash   twelve squares, two detuned clusters for width, through a
//           low-pass that closes as it decays.
//   and a rim, a shaker, a conga (a woodblock tok in techno, a tambourine
//   in breaks), a cowbell tuned to the fifth, and a laser zap.
//
// The five kits past the dance floor bring drums of their own:
//
//   boom bap  everything through an SP-1200's 26 kHz and twelve bits and a
//             dark low-pass; the clap is a finger snap, the zap a baby
//             scratch (a tone whose speed swings forward and back like a
//             record).
//   jungle    a tight break snare on the key's root, its ghosts on the
//             second and mostly wires, kick and snare chopping each other
//             off like slices of one break; tight high toms, a piccolo rim,
//             a splash; the hand drum a timbale (a skin with a metal
//             shell's ring), the zap a dub siren.
//   linn      the 80s: a snare (and clap) whose room holds flat until a
//             gate chops it to silence, the LM-1's sidestick, a cabasa, a
//             tambourine with its own part, and a Simmons syndrum falling
//             all the way down.
//   cr-78     the rhythm box: a round kick, ringing noise for hats and
//             "metal beat", a guiro of scraped clicks, claves in key.
//   brushes   jazz: a felt kick, a brush slap and a swish (noise swelling
//             slowly through a moving band-pass), a dark ride whose bell
//             is its own pad, tuned, and vibes on the hand drum.
//
// In the CR-78 and brushes kits the 808 is an upright bass: a plucked
// string's harmonics an octave up, walking the chord of the moment.
//
// The five after them leave the drum kit behind for other music's
// percussion, and nearly every pad is an instrument of its own:
//
//   amapiano  the log drum on the 808 pad: a deep sine an octave under
//             where it was, saturated so its harmonics carry, with a
//             resonant "bark" that sweeps down after each attack, sliding
//             from note to note in its 3-3-2 riff and leaping an octave
//             and a fifth, over a clean sub that stays under 80 Hz
//             wherever the riff leaps; the whole low end is its own, the
//             kick high-passed to a light punch above it; around it a woody rim
//             off the backbeat, a vocal chop, private-school clicks and
//             muted toms, a two-layer shaker swung as the lead
//             percussion (a layer in each ear, the hats opposite each
//             other around them), a marimba, and a filtered Rhodes stab of
//             the chord playing.
//   tabla     the bols of a tabla: the bayan's ge (its pitch rising as the
//             heel presses, the gumki, further the further up the pad it
//             is struck) on the kick, the dayan's na, tun, te and tak as
//             modes of a harmonic membrane (no two strokes alike: each a
//             few cents off, the ring finger damping more or less, the
//             syahi's second mode stronger or weaker), the ka a flat,
//             papery slap, a tabla tarang on the toms, ghungroo and the
//             chimta's jingles as little bells, manjira, a temple bell, a
//             sitar, and a tanpura's drone: four strings plucked Pa-Sa-Sa-Sa, each
//             ringing for seconds while the jawari's band sweeps its
//             harmonics. The 808 is the bayan held: up the pad presses the
//             heel, bending it.
//   gamelan   bronze in the gamelan's own tuning, pelog over a minor
//             third and slendro over a major one, on the song's root:
//             saron, bonang, kenong, kempul, ketuk and kempyang, each paired
//             with a twin a few hertz off so it beats (the ombak), the
//             slenthem's long low bar, the great gong ageng around 50 Hz
//             ringing for ten seconds and more, the kecrek's loose plates
//             (never the same twice), the ceng-ceng clashing in both ears,
//             an angklung that shakes as long as it is held, and the
//             jegogan on the 808 pad. Grooving, the bonang and the saron
//             interlock in a kotekan, one line from two players.
//   chiptune  a Game Boy's sound chip: its wave channel's four-bit
//             triangle (each step of the staircase band-limited) swept for
//             the kick and toms, pulses that change duty frame by frame
//             and wobble in the frame-rate vibrato, an arpeggio a frame per
//             note, and the noise channel's shift register clocked through
//             the chip's own divisor table, so its low settings step and
//             grind (its seven-bit mode the metallic hat), all through the
//             chip's DACs and output capacitor: channels pop on and low
//             pulses droop.
//   djembe    a West African ensemble, spread as it stands: the djembe's
//             bass, tone and slap (the slap cracking, a snap of skin and
//             the high modes the hand's edge drives; busy, the lead
//             djembe's call of flams and slap triplets), the three dunun
//             (dununba, sangban, kenkeni) struck with a stick, open or
//             pressed, the dunun's iron bells (six inharmonic modes, which
//             of them speak decided by where the rod lands, now and then a
//             buzzing bounce), every stroke a little different, a talking
//             drum squeezed as it speaks, kese and shekere rattles (kept
//             under the bells and the slaps, which lead), a
//             scraped karinyan, the sesse's buzzing tin, a krin slit drum,
//             a kora's plucked string, and a balafon with its gourds'
//             buzzing membranes on the 808 pad.

//
// The last kit is not a kit: the whole pad becomes a hang (hang.h), one
// instrument of steel and air simulated as it is built, its ding on the
// key's root and its eight fields up the key's scale, played where and as
// hard as the fingers land.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

#include "dsp.h"
#include "electro.h"
#include "hang.h"
#include "part.h"

namespace tf::drums {

// The pads, as laid out on the trackpad: four rows from the player (low
// sounds nearest) to the far edge (cymbals), four across. pad = row * 4 + col.
enum Pad : int {
    Kick, Snare, Clap, Hat,         // row 0: the beat
    Sub, Rim, Shaker, Open,         // row 1: 808, rim, shaker, open hat
    TomLo, TomMid, TomHi, Perc,     // row 2: toms and a hand drum
    Crash, Ride, Bell, Zap,         // row 3: cymbals, cowbell, zap
    PadCount
};

// The first five are the dance floor's; the second five reach past it: a
// dusty sampler's boom bap, jungle's chopped breaks, an 80s LinnDrum with its
// gated snare, the Roland CR-78 rhythm box, and a jazz kit played with
// brushes; then five from the world's other music: amapiano, tabla,
// gamelan, a Game Boy's chiptune and a djembe ensemble; and the hang, the
// whole pad one instrument. Songs save a kit by
// its name (kitName), and a clip's hits carry the index, so new kits go at
// the end and no kit is ever renamed.
enum Kit : int {
    House, Techno, Trap, Breaks, Garage, BoomBap, Jungle, Linn, Cr78, Brushes,
    Amapiano, Tabla, Gamelan, Chip, Djembe, Hang, KitCount
};

// The hang's ding, eight fields and gu are pads of their own: the ones
// nothing else is special about (no 808 to hold, no hat to choke, no kick
// to duck the studio), so the page's rolls, grooves and loops treat them
// as any drum. Field 0 is the ding, 1–8 the fields lowest to highest.
inline constexpr int kHangPads[hang::kFields + 1] = {Snare, Clap, Rim, Shaker, TomLo, TomMid, TomHi, Perc, Crash, Ride};
inline int hangPad(int field) { return kHangPads[std::clamp(field, 0, hang::kFields)]; }
// The field (or hang::kGu) a pad strikes on the hang; the pads the hang
// doesn't use land on the nearest sense of themselves.
inline int hangField(int pad) {
    for (int f = 0; f <= hang::kFields; ++f)
        if (kHangPads[f] == pad)
            return f;
    switch (pad) {
    case Kick: return 0;
    case Sub: return hang::kGu;
    case Hat: return 4;
    case Open: return 6;
    case Bell: return 7;
    default: return 8;
    }
}
inline bool isHangGu(int pad) { return hangField(pad) == hang::kGu; }

inline const char *padName(int pad) {
    static const char *names[PadCount] = {"kick", "snare", "clap", "hat", "808",   "rim",  "shaker", "open",
                                          "tom lo", "tom mid", "tom hi", "conga", "crash", "ride", "bell", "zap"};
    return names[std::clamp(pad, 0, PadCount - 1)];
}

// Some pads are another instrument in some kits: where a drum has no place
// in a style, the pad plays the nearest one that does.
inline const char *padName(int pad, int kit) {
    // The world kits are other instruments on nearly every pad: a stroke of
    // the tabla (a bol), a piece of the gamelan, a drum or rattle of the
    // djembe's ensemble; amapiano keeps the kit's kick, hats and cymbals.
    pad = std::clamp(pad, 0, PadCount - 1);
    if (kit == Hang) {
        static const char *names[hang::kFields + 1] = {"ding",   "field 1", "field 2", "field 3", "field 4",
                                                       "field 5", "field 6", "field 7", "field 8", "gu"};
        return names[hangField(pad)];
    }
    if (kit == Tabla) {
        static const char *names[PadCount] = {"ge",        "na",     "ka",        "te",      "bayan",   "tak",
                                              "ghungroo",  "chimta", "tarang lo", "tarang",  "tarang hi", "tun",
                                              "tanpura",   "manjira", "ghanti",   "sitar"};
        return names[pad];
    }
    if (kit == Gamelan) {
        static const char *names[PadCount] = {"kendhang", "tak",   "ceng",     "kecrek",   "jegogan", "ketuk",
                                              "slenthem", "kenong", "saron lo", "saron",    "saron hi", "bonang",
                                              "gong",     "kempyang", "kempul", "angklung"};
        return names[pad];
    }
    if (kit == Djembe) {
        static const char *names[PadCount] = {"bass",    "slap",    "tone",    "kese",  "balafon", "bell hi",
                                              "shekere", "karinyan", "dununba", "sangban", "kenkeni", "talking",
                                              "sesse",   "bell",    "krin",    "kora"};
        return names[pad];
    }
    if (kit == Amapiano) {
        static const char *names[PadCount] = {"kick",   "woodrim", "vox",    "hat",     "log drum", "click",
                                              "shaker", "open",    "tom lo", "tom mid", "tom hi",   "wood",
                                              "crash",  "ride",    "marimba", "keys"};
        return names[pad];
    }
    switch (pad) {
    case Perc:
        switch (kit) {
        case Techno: return "tok";
        case Breaks:
        case Linn: return "tamb";
        case Trap:
        case Cr78: return "bongo";
        case Jungle: return "timbale";
        case Brushes: return "vibes";
        case Chip: return "blip";
        default: return "conga";
        }
    case Clap: return kit == BoomBap ? "snap" : kit == Cr78 ? "guiro" : kit == Brushes ? "swish" : "clap";
    case Snare: return kit == Brushes ? "brush" : "snare";
    case Sub:
        switch (kit) {
        case Brushes:
        case Cr78: return "bass";
        case Chip: return "pulse";
        default: return "808";
        }
    case Rim: return kit == Brushes ? "x-stick" : "rim";
    case Shaker: return kit == Cr78 ? "maracas" : kit == Linn ? "cabasa" : "shaker";
    case Open: return kit == Cr78 ? "metal" : "open";
    case TomLo: return kit == Cr78 ? "conga lo" : "tom lo";
    case TomMid: return kit == Cr78 ? "conga" : "tom mid";
    case TomHi: return kit == Cr78 ? "conga hi" : "tom hi";
    case Ride: return kit == Chip ? "metal" : "ride";
    case Bell:
        switch (kit) {
        case Brushes: return "ride bell";
        case Cr78:
        case Linn: return "cowbell";
        case Chip: return "arp";
        default: return "bell";
        }
    case Zap:
        switch (kit) {
        case BoomBap: return "scratch";
        case Linn: return "simmons";
        case Cr78: return "clave";
        case Brushes: return "block";
        case Jungle: return "siren";
        case Chip: return "laser";
        default: return "zap";
        }
    default: return padName(pad);
    }
}

inline const char *kitName(int kit) {
    static const char *names[KitCount] = {"house",    "techno", "trap",    "breaks",   "garage",
                                          "boom bap", "jungle", "linn",    "cr-78",    "brushes",
                                          "amapiano", "tabla",  "gamelan", "chiptune", "djembe",
                                          "hang"};
    return names[std::clamp(kit, 0, KitCount - 1)];
}

// The gamelan plays in its own tuning, never the piano's: pelog when the
// song's scale has a minor third, slendro when it has a major one, both
// counted from the song's root, so changing the song's mode changes the
// gamelan's laras (and the status line says which).
inline bool gamelanPelog(const Tuning &t) { return t.degreeSemis(2) - t.degreeSemis(0) < 3.5; }

// What a world kit is, said after its name in the status line: the tabla's
// rhythm, the gamelan's tuning, the chip, the djembe's rhythm.
inline const char *kitCharacter(int kit, bool pelog) {
    switch (kit) {
    case Amapiano: return "log drum";
    case Tabla: return "kaharwa";
    case Gamelan: return pelog ? "pelog" : "slendro";
    case Chip: return "dmg";
    case Djembe: return "kuku";
    case Hang: return "steel and air";
    default: return "";
    }
}

// Each kit's feel, in MPC terms: 50% is straight, 66% a full triplet
// shuffle. The brushes' own parts are written in triplets; this swings what
// they roll in sixteenths.
inline float kitSwing(int kit) {
    static const float swing[KitCount] = {0.54f, 0.5f, 0.5f, 0.56f, 0.62f, 0.6f, 0.52f, 0.5f, 0.5f, 0.66f, 0.6f, 0.54f, 0.5f, 0.5f, 0.56f, 0.54f};
    return swing[std::clamp(kit, 0, KitCount - 1)];
}

// Each kit's output level, in dB: a kit is voiced to sound like its genre,
// and these bring every kit's groove to about the same loudness so changing
// kit never jumps the mix. Every hit carries its kit, so a loop keeps its
// level too.
inline float kitTrim(int kit) {
    static const float db[KitCount] = {0.0f, -2.2f, 2.6f, 0.0f, 2.4f, 2.8f, 3.4f, -0.4f, 2.2f, 2.7f, 0.0f, 1.6f, 3.0f, 5.2f, 1.5f, 0.0f};
    static const auto gain = [] {
        std::array<float, KitCount> g{};
        for (int k = 0; k < KitCount; ++k)
            g[size_t(k)] = std::exp2(db[k] / 6.0206f);
        return g;
    }();
    return gain[size_t(std::clamp(kit, 0, KitCount - 1))];
}

// Roughly how long each drum rings in each kit (its main decay's time
// constant, in seconds), for drawing.
inline float displayDecay(int pad, int kit) {
    kit = std::clamp(kit, 0, KitCount - 1);
    static const float table[PadCount][KitCount] = {
        // house, techno, trap, breaks, garage, boom bap, jungle, linn, cr-78, brushes, amapiano, tabla, gamelan, chiptune, djembe, hang
        {0.15f, 0.21f, 0.16f, 0.09f, 0.11f, 0.12f, 0.085f, 0.19f, 0.09f, 0.2f, 0.24f, 0.5f, 0.22f, 0.12f, 0.22f, 2.0f}, // kick
        {0.085f, 0.06f, 0.07f, 0.13f, 0.07f, 0.1f, 0.075f, 0.2f, 0.06f, 0.12f, 0.03f, 0.4f, 0.08f, 0.09f, 0.08f, 2.0f}, // snare
        {0.11f, 0.08f, 0.09f, 0.14f, 0.1f, 0.05f, 0.08f, 0.2f, 0.12f, 0.25f, 0.15f, 0.014f, 0.25f, 0.08f, 0.16f, 2.0f}, // clap
        {0.034f, 0.028f, 0.018f, 0.045f, 0.03f, 0.04f, 0.022f, 0.04f, 0.025f, 0.05f, 0.03f, 0.025f, 0.1f, 0.025f, 0.02f, 2.0f}, // hat
        {1.0f, 1.2f, 2.6f, 0.8f, 1.1f, 0.9f, 1.6f, 0.7f, 0.5f, 0.45f, 0.8f, 0.7f, 1.4f, 1.0f, 0.45f, 2.0f}, // 808
        {0.011f, 0.011f, 0.011f, 0.011f, 0.011f, 0.011f, 0.011f, 0.011f, 0.011f, 0.02f, 0.01f, 0.07f, 0.07f, 0.015f, 0.35f, 2.0f}, // rim
        {0.045f, 0.045f, 0.03f, 0.045f, 0.045f, 0.045f, 0.035f, 0.045f, 0.03f, 0.06f, 0.04f, 0.06f, 2.2f, 0.04f, 0.06f, 2.0f}, // shaker
        {0.24f, 0.2f, 0.17f, 0.34f, 0.22f, 0.22f, 0.2f, 0.3f, 0.12f, 0.4f, 0.26f, 0.3f, 2.4f, 0.2f, 0.1f, 2.0f}, // open
        {0.23f, 0.23f, 0.35f, 0.17f, 0.23f, 0.17f, 0.13f, 0.36f, 0.12f, 0.3f, 0.13f, 0.4f, 1.1f, 0.15f, 0.32f, 2.0f}, // tom lo
        {0.2f, 0.2f, 0.3f, 0.15f, 0.2f, 0.15f, 0.11f, 0.32f, 0.1f, 0.26f, 0.13f, 0.4f, 1.1f, 0.15f, 0.32f, 2.0f}, // tom mid
        {0.2f, 0.2f, 0.3f, 0.15f, 0.2f, 0.15f, 0.11f, 0.32f, 0.09f, 0.26f, 0.13f, 0.4f, 1.1f, 0.15f, 0.32f, 2.0f}, // tom hi
        {0.13f, 0.055f, 0.07f, 0.11f, 0.13f, 0.13f, 0.2f, 0.13f, 0.06f, 0.7f, 0.09f, 0.7f, 0.6f, 0.06f, 0.28f, 2.0f}, // hand drum
        {1.1f, 1.1f, 0.8f, 1.3f, 1.1f, 0.7f, 0.85f, 1.25f, 0.6f, 1.35f, 1.0f, 1.7f, 2.2f, 0.6f, 0.3f, 2.0f}, // crash
        {0.8f, 0.8f, 0.56f, 0.8f, 0.8f, 0.6f, 0.8f, 0.9f, 0.4f, 1.4f, 0.8f, 1.0f, 0.25f, 0.3f, 0.6f, 2.0f}, // ride
        {0.15f, 0.09f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.12f, 0.12f, 1.2f, 0.55f, 1.1f, 1.1f, 0.35f, 0.22f, 2.0f}, // bell
        {0.13f, 0.13f, 0.13f, 0.13f, 0.13f, 0.12f, 0.35f, 0.33f, 0.03f, 0.13f, 0.9f, 0.6f, 0.4f, 0.15f, 1.1f, 2.0f}, // zap
    };
    return table[std::clamp(pad, 0, PadCount - 1)][kit];
}

// Pads whose pitch is a scale degree, so they follow the key.
inline bool tonal(int pad) {
    return pad == Kick || pad == Sub || pad == TomLo || pad == TomMid || pad == TomHi || pad == Perc || pad == Bell ||
           pad == Zap;
}
// On the hang every field is: a degree more is the next field up (the gu
// has no pitch to move).
inline bool tonal(int pad, int kit) { return kit == Hang ? !isHangGu(pad) : tonal(pad); }

// --- the command protocol ---------------------------------------------------
//
// A hit: type Hit, flagged Quantize when a hand played it (so a captured
// loop pulls it to the grid) and not when the page generated it on the grid
// already (a roll, a groove), NoRecord for an audition. Every hit carries
// its whole sound, the kit and the pad's shaping included, so a loop sounds
// the way it was played whatever the page is set to later.
//   i[0] pad   i[1] scale degrees (tonal pads, the pad's tuning included)
//   i[2] kit + 1 (0: the engine's current kit)
//   f[0] velocity 0..1   f[1], f[2] where on the pad it was struck (0..1)
//   f[3] semitones (the other pads, tuning included)
//   f[4] decay, as a power of two   f[5] for the 808: seconds to hold before
//   it lets go (0: a hit)   f[6] tone −1..1
// The 808 played by a finger is held instead: On (the same fields), Move
// (i[1] the degree it glides to, f[0] the tabla's bayan pressed in by the
// heel, 0..5 semitones), Off.
// Params never go into clips:
//   ParamKit: i[1] the page's kit (the room's size, and hits that name none).
//   ParamEdit: i[1] pad, f[0..2] tune, decay, tone, mirrored for the view.
//   ParamMute: i[1] clip slot, i[2] pad, i[3] 1 to silence that pad in that
//   slot's loop, 0 to bring it back.
constexpr uint16_t Hit = Command::User;
enum ParamKind : int { ParamKit = 0, ParamEdit = 1, ParamMute = 2 };

struct Edit {
    float tune = 0, decay = 0, tone = 0;
    bool any() const { return std::fabs(tune) > 0.01f || std::fabs(decay) > 0.01f || std::fabs(tone) > 0.01f; }
};

// What one hit asks of a voice.
struct Strike {
    int pad = Kick;
    float vel = 0.8f;
    float sx = 0.5f, sy = 0.5f;
    int degrees = 0;
    float semis = 0;
    float decay = 0;
    float tone = 0;
    int kit = -1;   // −1: the engine's
    float hold = 0; // 808 only
};

// The octave (as Harmony::midi counts them) that puts the key's root
// nearest a target frequency: the kick sits near 52 Hz whatever the key.
inline int octaveNear(const Harmony &h, float hz) {
    int best = 0;
    float err = 1e9f;
    for (int o = -2; o <= 5; ++o) {
        const float e = std::fabs(std::log2(midiToHz(h.midi(0, o)) / hz));
        if (e < err) {
            err = e;
            best = o;
        }
    }
    return best;
}

inline float degreeHz(const Harmony &h, int degree, float nearHz) {
    return midiToHz(h.midi(degree, octaveNear(h, nearHz)));
}

// The kick sits on the key's root when the root falls within a few
// semitones of where a kick lives, else on its fifth: every key gets a kick
// between about 43 and 62 Hz that still belongs to it.
inline float kickHz(const Harmony &h, int degrees, float nearHz) {
    const float root = degreeHz(h, 0, nearHz);
    const bool fits = root >= nearHz * 0.83f && root <= nearHz * 1.2f;
    const int base = fits ? 0 : h.tuning.degreesPerOctave() == 7 ? 4 : 3;
    const float hz = midiToHz(h.midi(base, octaveNear(h, nearHz)));
    const float near = hz > nearHz * 1.41f ? hz * 0.5f : hz < nearHz * 0.71f ? hz * 2.0f : hz;
    return near * std::exp2(float(h.tuning.degreeSemis(base + degrees) - h.tuning.degreeSemis(base)) / 12.0f);
}

// A note of the gamelan, in its own tuning over the song's root: slendro's
// five nearly equal steps, or pelog's seven uneven ones (as measured on
// Javanese gamelan, in cents), with octaves a little wide, the way a
// gamelan's are tuned. Degrees count the gamelan's own notes, so the pads'
// root, third and fifth become its first, third and fifth keys.
inline float gamelanHz(const Harmony &h, int degree, float nearHz) {
    static const float slendro[5] = {0.0f, 231.0f, 474.0f, 717.0f, 955.0f};
    static const float pelog[7] = {0.0f, 120.0f, 270.0f, 540.0f, 670.0f, 785.0f, 950.0f};
    const bool p = gamelanPelog(h.tuning);
    const int n = p ? 7 : 5;
    const int octave = degree >= 0 ? degree / n : -((n - 1 - degree) / n);
    const int i = degree - octave * n;
    const float cents = (p ? pelog[i] : slendro[i]) + 1212.0f * float(octave);
    return degreeHz(h, 0, nearHz) * std::exp2(cents / 1200.0f);
}

// The gamelan's notes by its own names, never the piano's letters (which
// it doesn't play): pelog's seven, ji, ro, lu, pat, ma, nem and pi, written
// 1 to 7 in its notation; slendro's five, which skip 4 and 7: 1 ji, 2 ro,
// 3 lu, 5 ma and 6 nem. Degrees count the gamelan's notes, as gamelanHz.
inline const char *gamelanNoteName(int degree, bool pelog) {
    static const char *pelogNames[7] = {"1 ji", "2 ro", "3 lu", "4 pat", "5 ma", "6 nem", "7 pi"};
    static const char *slendroNames[5] = {"1 ji", "2 ro", "3 lu", "5 ma", "6 nem"};
    const int n = pelog ? 7 : 5;
    const int i = ((degree % n) + n) % n;
    return pelog ? pelogNames[i] : slendroNames[i];
}

// The gamelan's note a pad is struck on (before any tuning of the pad), as
// startGamelan plays it, or −1 for the kendhang and the ceng-ceng, which
// have none.
inline int gamelanPadDegree(int pad) {
    switch (pad) {
    case TomMid:
    case Perc:
    case Bell: return 2;
    case TomHi: return 4;
    case Rim: return 1;
    case Kick:
    case Snare:
    case Clap:
    case Hat: return -1;
    default: return 0;
    }
}

// The hang's tuning in a key: the ding on the root near D3, the fields from
// the fifth above it up the scale, eight degrees, as the classic hang's
// D | A Bb C D E F G A is in D minor.
inline void hangTuning(const Harmony &h, float *hz) {
    const int oct = octaveNear(h, 150.0f);
    const int per = h.tuning.degreesPerOctave();
    int first = 1;
    while (first < per && h.tuning.degreeSemis(first) < 6.5)
        ++first;
    hz[0] = midiToHz(h.midi(0, oct));
    for (int f = 1; f < hang::kFields; ++f)
        hz[f] = midiToHz(h.midi(first + f - 1, oct));
}

// --- grooves ----------------------------------------------------------------
//
// A held pad repeats. One finger alone rolls at a rate; fingers that land
// together become a groove, each pad playing its part in the kit's style
// (four on the floor, a backbeat, offbeat hats, euclidean percussion). A
// bar is 96 ticks, so sixteenths (6), triplets (8, 4) and thirty-seconds
// (3) all fall on it.

constexpr int kTicks = 96;

struct Step {
    uint8_t tick = 0;
    float vel = 1;
    int8_t degree = 0; // tuned percussion walks the scale
};

struct Pattern {
    Step steps[96];
    int count = 0;
    // Written in triplets, with each beat's last triplet on the straight
    // eighth: the page's swing puts it late, onto the triplet at 66%.
    bool triplet = false;
    void add(int tick, float vel, int degree = 0) {
        if (count < 96)
            steps[count++] = {uint8_t(((tick % kTicks) + kTicks) % kTicks), vel, int8_t(degree)};
    }
    void sort() {
        std::sort(steps, steps + count, [](const Step &a, const Step &b) { return a.tick < b.tick; });
    }
};

// The roll rates, slowest first, in ticks per step.
constexpr int kRates = 7;
inline int rateTicks(int r) {
    static const int ticks[kRates] = {24, 12, 8, 6, 4, 3, 2};
    return ticks[std::clamp(r, 0, kRates - 1)];
}
inline const char *rateName(int r) {
    static const char *names[kRates] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
    return names[std::clamp(r, 0, kRates - 1)];
}
// Where a held pad's roll starts: hats and shakers run in sixteenths, the
// kick in quarters, most things in eighths.
inline int defaultRate(int pad) {
    switch (pad) {
    case Kick:
    case Crash: return 0;
    case Hat:
    case Shaker:
    case Perc:
    case Zap: return 3;
    default: return 1;
    }
}

// Even spread of k onsets over n steps (Bjorklund's result, by the bucket
// method), rotated r steps later.
inline bool euclid(int k, int n, int r, int step) {
    if (n <= 0 || k <= 0)
        return false;
    k = std::min(k, n);
    const int s = (((step - r) % n) + n) % n;
    return ((s * k) % n) < k;
}

// A roll: every step at the rate, the beat a touch stronger than the rest,
// and on average as loud as the hit that began it.
inline Pattern rollPattern(int rate) {
    Pattern p;
    const int step = rateTicks(rate);
    for (int t = 0; t < kTicks; t += step)
        p.add(t, t % 24 == 0 ? 1.04f : t % 12 == 0 ? 1.0f : 0.96f);
    return p;
}

constexpr int kDensities = 5;
inline const char *densityName(int d) {
    static const char *names[kDensities] = {"sparse", "groove", "busy", "roll", "roll x2"};
    return names[std::clamp(d, 0, kDensities - 1)];
}

namespace detail {

// Sixteen steps: X accent, x normal, o ghost, . rest.
inline void addSixteenths(Pattern &p, const char *s, int shift) {
    for (int i = 0; i < 16 && s[i]; ++i) {
        const char c = s[i];
        if (c == '.')
            continue;
        p.add((i + shift) * 6, c == 'X' ? 1.0f : c == 'x' ? 0.72f : 0.42f);
    }
}

// The kit's parts for the pads that have one, at densities 0..2. The
// brushes' are written in triplets (tripletPart, below), so their rows here
// are empty.
inline const char *part(int pad, int kit, int density) {
    const int d = std::clamp(density, 0, 2);
    struct Row {
        const char *p[3];
    };
    static constexpr Row none = {{nullptr, nullptr, nullptr}};
    // Kick.
    static const Row kick[KitCount] = {
        {{"X.......X.......", "X...X...X...X...", "X...X...X...X..x"}},
        {{"X...X...X...X...", "X...X...X...X...", "X...X...X...X.x."}},
        {{"X...............", "X......X..X.....", "X..x...X..X...x."}},
        {{"X.........X.....", "X.X.......XX....", "X.X.......XX..x."}},
        {{"X...............", "X.........X.....", "X......x..X....."}},
        // Boom bap: the kick lays back, a pickup after each of its beats.
        {{"X.........X.....", "X..x......X..x..", "X..x...x.xX..x.."}},
        // Jungle: the two-step, then the break's doubled kicks.
        {{"X.........X.....", "X.........X..x..", "X.X.....x.X..x.."}},
        // Linn: the 80s pop beat, on one and three.
        {{"X.......X.......", "X.......X.X.....", "X.....x.X.X...x."}},
        // CR-78: the rhythm box's pop preset, a kick tucked before three.
        {{"X.......X.......", "X......xX.......", "X..x...xX.x...x."}},
        none,
        // Amapiano: soft kicks on the beat, the log drum doing the rest.
        {{"X.......X.......", "X...X...X...X...", "X...X...X...X..x"}},
        // Tabla: the bayan's ge in kaharwa (dha ge na ti na ka dhi na).
        {{"X...........X...", "X.x.........X...", "X.x.......o.X.x."}},
        // Gamelan: the kendhang's dhung, a pickup after each half.
        {{"X.......X.......", "X..x....X..x....", "X..x..x.X..x.x.."}},
        // Chiptune: a game's march.
        {{"X.......X.......", "X.....X.X.......", "X.....X.X.X...x."}},
        // Djembe: the lead's bass strokes, on one and before three.
        {{"X.......X.......", "X.....X.X.......", "X.....X.X.....x."}},
    };
    static const Row snare[KitCount] = {
        {{"............X...", "....X.......X...", "....X.......X.oo"}},
        {{"............X...", "....X.......X...", "....X..o....X.o."}},
        {{"........X.......", "........X.......", "........X.....o."}},
        {{"....X.......X...", "....X..o.o..X..o", "o...X..o.o.oX..o"}},
        {{"............X...", "....X.......X...", "....X.......X..o"}},
        {{"....X.......X...", "....X....o..X...", "....X..o.o..X..o"}},
        // Jungle: ghosts all through the break (and a rush, below).
        {{"....X.......X...", "....X..o.o..X.o.", "o.o.X.oo.o.oX.o."}},
        {{"....X.......X...", "....X.......X...", "....X.......X.oX"}},
        {{"....X.......X...", "....X.......X...", "....X..o....X.o."}},
        none,
        // Amapiano: no backbeat. A soft woody rim answers it, off the beat
        // after two and four, with ghosts as it gets busier.
        {{"......x.......x.", "...o..x....o..x.", "...o..x.o..o.ox."}},
        // Tabla: the dayan's na, on dha and na.
        {{"X.......X.......", "X...x...x.....x.", "X...x.o.x...o.x."}},
        // Gamelan: the kendhang's tak, off the beat.
        {{"....X.......X...", "....X..x....X..x", "..x.X..x..x.X.xx"}},
        {{"....X.......X...", "....X.......X...", "....X.......X.xx"}},
        // Djembe: the accompaniment's slaps, in pairs after the beat.
        {{"....X.X.....X.X.", "....X.XX....X.XX", "..o.X.XXo.o.X.XX"}},
    };
    static const Row clap[KitCount] = {
        {{"............X...", "....X.......X...", "....X..o....X..o"}},
        {{"............X...", "....X.......X...", "....X.......X.o."}},
        {{"........X.......", "........X.......", "........X.....x."}},
        {{"....X...........", "....X.......X...", "....X.......X.o."}},
        {{"............X...", "....X.......X...", "....X.......X.x."}},
        {{"....X.......X...", "....X.......X...", "....X.......X.x."}},
        {{"....X.......X...", "....X.......X...", "....X.......X..x"}},
        {{"....X.......X...", "....X.......X...", "....X.......X.X."}},
        // CR-78: the guiro's long-short-short scrape.
        {{"X.......X.......", "X.x...x.X.x...x.", "X.xxX.x.X.xxX.x."}},
        none,
        // Amapiano: a vocal chop, never on the backbeat.
        {{"..........X.....", "..........X...x.", "..x.......X.x.x."}},
        // Tabla: the ka, the bayan slapped flat.
        {{"..........x.....", "..........x.....", "..o.......x....."}},
        // Gamelan: the ceng-ceng clashing between the beats.
        {{"..............X.", "..X...X...X...X.", "x.X.x.X.x.X.x.XX"}},
        {{"............X...", "....X.......X...", "....X.......X.x."}},
        // Djembe: the tones before the slaps.
        {{"..X.......X.....", "..XX......XX....", "o.XX..o.o.XX..o."}},
    };
    static const Row hat[KitCount] = {
        {{"..X...X...X...X.", "o.X.o.X.o.X.o.X.", "oxXooxXooxXooxXx"}},
        {{"..X...X...X...X.", "xoXoxoXoxoXoxoXo", "xxXxxxXxxxXxxxXx"}},
        {{"X.x.X.x.X.x.X.x.", "", ""}}, // trap hats are built below
        {{"X.x.X.x.X.x.X.x.", "X.xoX.x.X.xoX.x.", "XoxoXoxoXoxoXoxo"}},
        {{"..X...X...X...X.", "o.Xoo.X.o.Xoo.X.", "oxXooxXxoxXooxXx"}},
        {{"X.x.X.x.X.x.X.x.", "X.x.X.xoX.x.X.xo", "XoxoXoxxXoxoXoxx"}},
        // Jungle: rolling sixteenths that lean on the offbeat.
        {{"x.x.x.x.x.x.x.x.", "x.xox.xox.xox.xo", "xoxxxoxxxoxxxoxx"}},
        {{"X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.xx", "XxXxXxXxXxXxXxXx"}},
        // CR-78: sixteenths with the box's accent on the offbeat eighths.
        {{"x.x.x.x.x.x.x.x.", "x.X.x.X.x.X.x.X.", "xoXoxoXoxoXoxoXo"}},
        none,
        {{"..X...X...X...X.", "x.X.x.X.x.X.x.X.", "xoXoxoXoxoXoxoXx"}},
        // Tabla: te, the dayan's closed stroke, and tirakita at the busiest.
        {{"......x.........", "......x.......x.", "....oox.....oox."}},
        // Gamelan: the kecrek, kicked in the dalang's figures, a pair of
        // strokes pushing into each half of the bar.
        {{"..x...x...x...x.", "x.x.x.xXx.x.x.xX", "xxXxxxXxxxXxxxXx"}},
        {{"X.x.X.x.X.x.X.x.", "X.xxX.x.X.xxX.x.", "XxxxXxxxXxxxXxxx"}},
        // Djembe: the kese shaken on the eighths, the beat leaning in, and
        // never so busy it hides the bells.
        {{"X...x...X...x...", "X.x.x.x.X.x.x.x.", "X.xxX.x.X.xxX.x."}},
    };
    static const Row open[KitCount] = {
        {{"..............X.", "..X...X...X...X.", "..X...X...X...XX"}},
        {{"..............X.", "..X...X...X...X.", "..X...X...X.x.X."}},
        {{"..............X.", "......X.......X.", "......X...X...X."}},
        {{"..........X.....", "..........X.....", "..X.......X...X."}},
        {{"..X.............", "..X.......X.....", "..X...X...X...X."}},
        {{"..............X.", "......X.......X.", "..X.......X...X."}},
        {{"..X...........X.", "......X.......X.", "..X...X...X...X."}},
        {{"..............X.", "......X.......X.", "..X...X...X...X."}},
        {{"..............X.", "......X.......X.", "..X...X...X...X."}},
        none,
        {{"..............X.", "..X.......X.....", "..X...X...X...X."}},
        // Tabla: the chimta's jingles on the offbeats.
        {{"..............X.", "..X.......X.....", "..X...X...X...X."}},
        {{"..............X.", "......X.......X.", "..X...X...X...X."}},
        {{"..............X.", "......X.......X.", "..X...X...X...X."}},
        // Djembe: the karinyan scraped on the offbeats.
        {{"..x...x...x...x.", "x.x.x.x.x.x.x.x.", "x.xxx.xxx.xxx.xx"}},
    };
    static const Row shaker = {{"..X...X...X...X.", "oxXxoxXxoxXxoxXx", "xxXxxxXxxxXxxXXX"}};
    static const Row ride = {{"X...X...X...X...", "X.x.X.x.X.x.X.x.", "X.xxX.xxX.xxX.xx"}};
    static const Row rideTechno = {{"..X...X...X...X.", "x.X.x.X.x.X.x.X.", "xxXxxxXxxxXxxxXx"}};
    static const Row crash = {{"X...............", "X...............", "X.......X......."}};
    // Amapiano's shakers lead: swung sixteenths all through, accented.
    static const Row shakerAmapiano = {{"x.X.x.X.x.X.x.X.", "oxXxoxXxoxXxoxXx", "xxXxxxXxxXXxxxXX"}};
    // The djembe's shekere sweeps on the offbeats, leaving the sixteenths
    // to the bells and the slaps.
    static const Row shakerDjembe = {{"......X.......X.", "..x...X...x...X.", "..x.x.X...x.x.X."}};
    // The djembe's iron bell, the dunun's timeline.
    static const Row rideDjembe = {{"X...X...X...X...", "X.x.X.xxX.x.X.xx", "X.xxX.xxX.xxX.xx"}};
    // Rims that are other instruments: the gamelan's ketuk between the
    // kenong's strokes, amapiano's private-school clicks running into the
    // bar, the djembe's high bell.
    static const Row rimGamelan = {{"..x.......x.....", "..x...x...x...x.", "..x.x.x...x.x.x."}};
    static const Row rimAmapiano = {{"..............x.", "..x.....x.x...x.", "..x..x..x.x.x.xx"}};
    static const Row rimDjembe = {{"x...x...x...x...", "x.x.x...x.x.x...", "x.xxx.x.x.xxx.x."}};
    // The CR-78's rim plays the son clave, three-two.
    static const Row rimCr78 = {{"X..X..X.........", "X..X..X...X.X...", "X..X..X...X.X..x"}};
    // The Linn's tambourine: the backbeat, then the 80s' shaken sixteenths.
    static const Row tambLinn = {{"....X.......X...", "..x.X.x...x.X.x.", "oxoxXxoxoxoxXxox"}};
    static const Row sub[KitCount] = {
        {{"X...............", "..X...X...X...X.", "..X.x.X...X.x.X."}},
        {{"..X...X...X...X.", ".xX..xX..xX..xX.", ".xxx.xxx.xxx.xxx"}},
        {{"X...............", "X......X..X.....", "X..x...X..X...x."}},
        {{"X.........X.....", "X.X.......XX....", "X.X.......XX..x."}},
        {{"X...............", "X.........X..x..", "X......x..X..x.."}},
        {{"X.........X.....", "X..x......X..x..", "X..x...x.xX..x.."}},
        // Jungle: long sub notes under the break.
        {{"X...............", "X.........X.....", "X.....x...X....."}},
        // Linn: the synthwave bass's driving eighths.
        {{"X.......X.......", "X.X.X.X.X.X.X.X.", "X.XxX.XxX.XxX.Xx"}},
        {{"X.......X.......", "X......xX.......", "X..x...xX.x...x."}},
        none,
        none, // amapiano's log drum plays a riff of its own (below)
        // Tabla: the bayan held under the ge.
        {{"X...............", "X.........x.....", "X.....x...x....."}},
        none, // the jegogan plays slow notes of its own (below)
        none, // and the chiptune's bass jumps octaves (below)
        none, // the balafon plays a melody of its own (below)
    };
    if (kit == Hang)
        return nullptr; // the hang's grooves are melodies (groovePattern)
    switch (pad) {
    case Kick: return kick[kit].p[d];
    case Snare: return snare[kit].p[d];
    case Clap: return clap[kit].p[d];
    case Hat: return hat[kit].p[d];
    case Open: return open[kit].p[d];
    case Shaker: return kit == Amapiano ? shakerAmapiano.p[d] : kit == Djembe ? shakerDjembe.p[d] : shaker.p[d];
    case Ride: return kit == Techno ? rideTechno.p[d] : kit == Djembe ? rideDjembe.p[d] : ride.p[d];
    case Crash: return crash.p[d];
    case Sub: return sub[kit].p[d];
    case Rim:
        switch (kit) {
        case Cr78: return rimCr78.p[d];
        case Gamelan: return rimGamelan.p[d];
        case Amapiano: return rimAmapiano.p[d];
        case Djembe: return rimDjembe.p[d];
        default: return nullptr;
        }
    case Perc: return kit == Linn ? tambLinn.p[d] : nullptr;
    default: return nullptr;
    }
}

// The tick of the i-th of twelve triplet eighths: each beat's first on the
// beat, its second a third of the way through (eight ticks), its last on
// the straight eighth (twelve), where swing moves it (a full triplet at
// 66%, straight at 50%).
inline int tripletTick(int i) {
    static const int within[3] = {0, 8, 12};
    return (i / 3) * 24 + within[i % 3];
}

// Twelve steps of triplet eighths: X accent, x normal, o ghost, . rest.
inline void addTriplets(Pattern &p, const char *s, int shift) {
    p.triplet = true;
    for (int i = 0; i < 12 && s[i]; ++i) {
        const char c = s[i];
        if (c == '.')
            continue;
        p.add(tripletTick(i) + shift * 6, c == 'X' ? 1.0f : c == 'x' ? 0.72f : 0.42f);
    }
}

// The brushes' parts, in triplets because jazz swings its eighths: the
// ride's spang-a-lang, the hat's foot on two and four, the brush slapping
// the backbeat while the other hand swishes round the head, the kick
// feathered under it all.
// A part with a tune: sixteen steps of rhythm (X accent, x normal, o
// ghost, . rest) and under each step its scale degree, at densities 0..2.
struct Riff {
    const char *rhythm[3];
    const char *degree[3];
};

inline const Riff *riff(int pad, int kit) {
    // Amapiano's log drum: three, three and two, sliding between its notes,
    // and leaping the way it does, from the root up an octave and on to
    // the fifth above that (degrees past 9 are letters: a is 10, b 11),
    // each slide reaching the note before the next one starts.
    static const Riff log = {{"X..x..x.........", "X..x..x.X..x..x.", "X..x..xxX..x.xx."},
                             {"0..7..4.........", "0..7..b.4..7..2.", "0..7..b94..7.b7."}};
    // Its keys: stabs of the chord off the beat.
    static const Riff keys = {{"...x......x.....", "...x..x...x..x..", "x..x..x...x..xx."},
                              {"...0......0.....", "...0..0...0..0..", "0..0..0...0..00."}};
    static const Riff jegogan = {{"X...............", "X.......x.......", "X...x...x...x..."},
                                 {"0...............", "0.......4.......", "0...2...4...2..."}};
    // The slenthem plays the balungan, the gamelan's skeleton melody, a
    // note a beat; the kenong marks its halves.
    static const Riff balungan = {{"X.......x.......", "X...x...x...x...", "X.x.x.x.X.x.x.x."},
                                  {"0.......2.......", "0...2...1...4...", "0.2.1.0.4.2.1.0."}};
    static const Riff kenong = {{"........X.......", "....x.......X...", "X...x...X...x..."},
                                {"........0.......", "....2.......0...", "0...2...4...0..."}};
    // The kotekan: one fast line split between two players, so that
    // neither plays it and together they never miss a note. The bonang
    // takes the polos (on the beat's half) and the saron the sangsih (in
    // between), both in the same octave: sparse, an eighth each; then a
    // figure of three notes turning against the bar's four, each player
    // every other sixteenth; at its busiest a line that climbs and falls
    // back. Played alone each is a broken pattern; held together they
    // lock into one.
    static const Riff polos = {{"X...x...X...x...", "X.x.x.x.X.x.x.x.", "X.x.x.x.X.x.x.x."},
                               {"0...1...2...1...", "0.2.1.0.2.1.3.3.", "0.2.2.2.4.2.4.4."}};
    static const Riff sangsih = {{"..x...x...x...x.", ".x.x.x.x.x.x.x.x", ".x.x.x.x.x.x.x.x"},
                                 {"..1...2...0...2.", ".1.0.2.1.0.2.2.1", ".1.3.1.3.3.3.5.2"}};
    static const Riff chip = {{"X...x...X...x...", "X.x.X.x.X.x.X.x.", "X.xxX.xxX.xxX.xx"},
                              {"0...7...0...7...", "0.7.0.7.0.7.0.7.", "0.770.770.774.77"}};
    // The djembe's ensemble: the three dunun interlocking (the dununba's
    // open strokes, the sangban answering, the kenkeni on the offbeat
    // eighths), and the balafon's ostinato over them.
    static const Riff dununba = {{"X.......X.......", "X.....x.X.......", "X.....x.X..x..x."},
                                 {"0.......0.......", "0.....0.0.......", "0.....0.0..0..0."}};
    static const Riff sangban = {{"....X.......X...", "..x.X.....x.X...", "..x.X.x...x.X.x."},
                                 {"....0.......0...", "..0.0.....0.0...", "..0.0.0...0.0.0."}};
    static const Riff kenkeni = {{"..x...x...x...x.", "..x...x...x...x.", "..xx..x...xx..x."},
                                 {"..0...0...0...0.", "..0...0...0...0.", "..00..0...00..0."}};
    static const Riff balafon = {{"X.....x.X.......", "X.x.x..xX.x.x..x", "X.xxx.xxX.xxx.xx"},
                                 {"0.....4.0.......", "0.2.4..20.4.2..1", "0.245.420.425.21"}};
    if (pad == Sub)
        switch (kit) {
        case Amapiano: return &log;
        case Gamelan: return &jegogan;
        case Chip: return &chip;
        case Djembe: return &balafon;
        default: return nullptr;
        }
    if (kit == Amapiano && pad == Zap)
        return &keys;
    if (kit == Gamelan && pad == Shaker)
        return &balungan;
    if (kit == Gamelan && pad == Open)
        return &kenong;
    if (kit == Gamelan && pad == Perc)
        return &polos;
    if (kit == Gamelan && pad == TomMid)
        return &sangsih;
    if (kit == Djembe)
        switch (pad) {
        case TomLo: return &dununba;
        case TomMid: return &sangban;
        case TomHi: return &kenkeni;
        default: return nullptr;
        }
    return nullptr;
}

inline const char *tripletPart(int pad, int density) {
    const int d = std::clamp(density, 0, 2);
    static const char *const parts[7][3] = {
        {"X.....o.....", "o..o..o..o..", "X..o..o.xo.."}, // kick: feathered quarters, a bomb
        {"...X.....X..", "...X..o..X.o", "..oX.o.o.X.o"}, // the brush's slap
        {"X.....X.....", "X..X..X..X..", "X.oX.oX.oX.o"}, // the swish
        {"...X.....X..", "...X.....X..", "...X..o..X.o"}, // hat, the foot
        {"......X.....", "...X.....X..", "...X..x..X.x"}, // open
        {"X..X..X..X..", "X..X.xX..X.x", "X.xX.xX.xX.x"}, // ride
        {".........X..", "...X.....X..", "...X..o..X.o"}, // cross-stick
    };
    switch (pad) {
    case Kick: return parts[0][d];
    case Snare: return parts[1][d];
    case Clap: return parts[2][d];
    case Hat: return parts[3][d];
    case Open: return parts[4][d];
    case Ride: return parts[5][d];
    case Rim: return parts[6][d];
    default: return nullptr;
    }
}

}

// The part a pad plays in a groove: its kit's pattern at a density (0
// sparse, 1 the groove, 2 busy, 3 a sixteenth roll, 4 thirty-seconds),
// shifted by a number of sixteenths. Pads without a written part play
// euclidean rhythms, each starting somewhere else so they interlock, and
// the tuned ones walk the chord tones of the scale.
inline Pattern groovePattern(int pad, int kit, int density, int shift) {
    Pattern p;
    kit = std::clamp(kit, 0, KitCount - 1);
    density = std::clamp(density, 0, kDensities - 1);
    if (density >= 3) {
        // Rolls: sixteenths, or thirty-seconds; trap hats and brushes roll
        // in triplets.
        const bool triplets = (kit == Trap && (pad == Hat || pad == Shaker)) || kit == Brushes;
        const int rate = density == 3 ? (triplets ? 4 : 3) : 5;
        return rollPattern(rate);
    }
    if (pad == Hat && kit == Trap) {
        // Eighths, with the trap programmer's signature: a triplet burst at
        // the end of the bar, and at the busiest a thirty-second stutter
        // before the snare too.
        for (int t = 0; t < kTicks; t += 12)
            if (density > 0 || t < 72)
                p.add(t + shift * 6, t % 24 == 0 ? 1.0f : 0.7f);
        if (density == 0) {
            for (int t = 72; t < kTicks; t += 12)
                p.add(t + shift * 6, 0.7f);
        } else {
            for (int t = 80; t < kTicks; t += 4)
                if (t % 12 != 0)
                    p.add(t + shift * 6, 0.55f + 0.1f * float(t - 80) / 4.0f);
            if (density == 2)
                for (int t = 39; t < 48; t += 3)
                    p.add(t + shift * 6, 0.5f);
        }
        p.sort();
        return p;
    }
    if (kit == Brushes && pad == Sub) {
        // The bass walks the chord of the moment (the upright counts these
        // from its root): half notes on the root and fifth, then root,
        // third, fifth and the step below the root leading into the next
        // bar, then root, fifth, seventh and the step above it.
        static const int walks[3][4] = {{0, 4, 0, 4}, {0, 2, 4, -1}, {0, 4, 6, 1}};
        for (int b = 0; b < 4; ++b)
            if (density > 0 || b % 2 == 0)
                p.add(b * 24 + shift * 6, b == 0 ? 1.0f : 0.8f, walks[density][b]);
        p.sort();
        return p;
    }
    // The parts that are melodies as much as rhythms, each step a scale
    // degree: amapiano's log drum in three, three and two, leaping an
    // octave and more, and its keys' stabs; the jegogan's slow notes,
    // the slenthem's balungan and the kenong marking the gamelan's phrase,
    // the bonang and the saron interlocking in a kotekan;
    // the chiptune bass bouncing between root and octave; the djembe's
    // three dunun interlocking and the balafon's ostinato.
    if (const detail::Riff *riff = detail::riff(pad, kit)) {
        const char *rhythm = riff->rhythm[density], *degree = riff->degree[density];
        for (int i = 0; i < 16; ++i)
            if (rhythm[i] != '.')
                p.add((i + shift) * 6, rhythm[i] == 'X' ? 1.0f : rhythm[i] == 'x' ? 0.75f : 0.45f,
                      degree[i] >= 'a' ? 10 + degree[i] - 'a' : degree[i] - '0');
        p.sort();
        return p;
    }
    if (kit == Djembe && pad == Snare && density == 2) {
        // Busy, the slap pad is the lead djembe's, the soloist over the
        // accompaniment: no longer a timekeeping pattern but a call (played
        // straight, as a lead plays across the kuku's lilt). Flams
        // (the other hand a tick ahead, softly) mark one and three, a
        // triplet of slaps turns beat two around, and the bar ends in an
        // echauffement, slap triplets swelling into the next one.
        auto flam = [&](int t, float vel) {
            p.add(t - 1 + shift * 6, 0.42f);
            p.add(t + shift * 6, vel);
        };
        flam(0, 1.0f);
        p.add(12 + shift * 6, 0.72f);
        for (int t = 36; t < 48; t += 4)
            p.add(t + shift * 6, 0.62f + 0.12f * float(t - 36) / 4.0f);
        flam(48, 1.0f);
        p.add(60 + shift * 6, 0.72f);
        for (int t = 72; t < 92; t += 4)
            p.add(t + shift * 6, 0.5f + 0.45f * float(t - 72) / 16.0f);
        p.sort();
        return p;
    }
    if (kit == Brushes)
        if (const char *s = detail::tripletPart(pad, density)) {
            detail::addTriplets(p, s, shift);
            p.sort();
            return p;
        }
    if (const char *s = detail::part(pad, kit, density)) {
        detail::addSixteenths(p, s, shift);
        // Jungle's busiest snare rushes into the bar line in thirty-seconds,
        // louder as it goes: the edit a junglist cuts into the break.
        if (kit == Jungle && pad == Snare && density == 2)
            for (int t = 87; t < kTicks; t += 3)
                p.add(t + shift * 6, 0.45f + 0.15f * float(t - 87) / 3.0f);
        p.sort();
        return p;
    }
    // Euclidean: three, five or seven onsets in sixteen.
    static const int pulses[3] = {3, 5, 7};
    int rot = 0;
    switch (pad) {
    case Rim: rot = 4; break;
    case TomLo: rot = 0; break;
    case TomMid: rot = 3; break;
    case TomHi: rot = 6; break;
    case Perc: rot = 2; break;
    case Bell: rot = 5; break;
    case Zap: rot = 7; break;
    default: break;
    }
    // The hang's fields interlock round the instrument, the ding on the one.
    if (kit == Hang)
        rot = (hangField(pad) * 5) % 16;
    // A melody of chord tones for the tuned ones: root, fifth, third, octave.
    // The brushes' run on the triplet grid, twelve to the bar, so they
    // swing with the rest of the kit.
    static const int walk[4] = {0, 4, 2, 7};
    const bool triplets = kit == Brushes;
    const int steps = triplets ? 12 : 16;
    p.triplet = triplets;
    int n = 0;
    for (int s = 0; s < steps; ++s)
        if (euclid(pulses[density], steps, rot + shift, s)) {
            const bool tuned = kit == Hang ? !isHangGu(pad) : pad == Perc || pad == Bell || pad == Zap;
            p.add(triplets ? detail::tripletTick(s) : s * 6, n == 0 ? 1.0f : 0.75f, tuned ? walk[n % 4] : 0);
            ++n;
        }
    return p;
}

// --- the voices -------------------------------------------------------------

// A sine by rotation: two multiplies a sample, for the ride's partials.
// A partial can also bloom: swell in over a time of its own while it
// fades, the way a gong's upper partials and a jawari's buzz come in after
// the strike.
struct Partial {
    float y1 = 0, y2 = 0, c = 0, amp = 0, env = 0, mul = 1;
    float rise = 0, riseMul = 0;
    void set(float hz, float rate, float a, float tau) {
        const float w = kTwoPi * std::min(hz, rate * 0.45f) / rate;
        c = 2.0f * std::cos(w);
        y1 = std::sin(w);
        y2 = 0;
        amp = a;
        env = 1;
        mul = std::exp(-1.0f / (tau * rate));
        rise = 0;
        riseMul = 0;
    }
    void bloom(float seconds, float rate) {
        rise = seconds > 0 ? 1.0f : 0.0f;
        riseMul = seconds > 0 ? std::exp(-1.0f / (seconds * rate)) : 0.0f;
    }
    inline float step() {
        const float y = c * y1 - y2;
        y2 = y1;
        y1 = y;
        env *= mul;
        rise *= riseMul;
        return y2 * amp * env * (1.0f - rise);
    }
};

inline float decayMul(float seconds, float rate) { return std::exp(-1.0f / (std::max(seconds, 0.0005f) * rate)); }

// The Game Boy's sound, one hit's worth: a tone (a pulse channel's pulse,
// or the wave channel's triangle in four bits) and the noise channel's
// shift register, each fading on its own and heard, as the chip plays it,
// in sixteen steps of volume (each step eased over a millisecond, so the
// steps are heard as a staircase fade and never as clicks). A pitch sweep
// either glides (the kick's and the toms' fall) or moves in steps of the
// chip's 128 Hz sweep clock (the laser). Everything a tracker changes per
// frame changes sixty times a second: an arpeggio's note, the pulse's duty
// stepping through a sequence, and a vibrato that comes in after a delay.
//
// Nothing here folds back: the triangle's staircase is a jump at every
// step, and each jump is smoothed over the samples either side of it (a
// band-limited step); the pulse's edges likewise. The noise is the
// register's bit as the chip clocks it, at the clock its divisor and shift
// give (524288 Hz over the divisor, over two to the shift plus one), each
// sample the average of the bits that played within it: at the high clocks
// a hiss, at the low ones the chip's gritty, audibly stepped noise.
//
// Then the chip's output stage. Each channel's DAC turns its four bits into
// a voltage from −1 (level 0) to +1 (level 15), so a channel that is on but
// quiet sits at −1, and a fading note's middle drifts down there with it;
// the capacitor on the way out (the charge factor 0.999958 a cycle of the
// 4 MHz clock, a high-pass near 30 Hz) takes the offset back out. That is
// the Game Boy's sound as much as its waves: a channel switching on pops
// (its DAC jumping down and the capacitor letting it go), a long, low
// pulse droops between its edges, and every fading hit carries a soft
// thump as its middle sinks. The DACs here sit half as far down as the
// chip's (kDacRest): two channels switched on at once would otherwise pop
// twice as loud as the drum they start.
struct ChipVoice {
    enum Tone { None, Pulse, Triangle };
    Tone tone = None;
    float rate = 48000;
    float duty = 0.5f;
    float hz = 0, hzTo = 0, sweep = 0, sweepMul = 1, held = 0; // the pitch: hzTo + (hz − hzTo) · sweep
    float tick = 0, tickInc = 0;                                // the sweep clock, when it steps
    float ph = 0;
    float toneVol = 0, toneMul = 1, toneOut = 0;
    float noiseVol = 0, noiseMul = 1, noiseOut = 0;
    bool shortNoise = false; // the seven-bit mode: a metallic buzz, not a hiss
    uint32_t lfsr = 0x7fff;
    float clockPh = 0, clockInc = 0, bit = 1;
    int divisor = 0, shift = 0, shiftTo = 0; // the noise register's clock, and where it steps to
    float shiftEvery = 0, shiftCount = 0;    // samples between steps of the shift
    float arp[3] = {1, 1, 1};
    int arpNote = 0;
    // The frame: sixty a second, each one a tracker row's worth of change.
    float framePh = 0, frameInc = 0;
    int frame = 0;
    float duties[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    int dutyCount = 0, dutyFrames = 1, dutyLoop = 0; // 0: none; past the end, back to dutyLoop
    int dutyAt = 0;
    float vibDepth = 0, vibFrames = 8, vibDelay = 0, vib = 1;
    float ease = 1;
    static constexpr float kDacRest = 0.5f; // where a switched-on, silent channel's DAC sits, below the middle
    float cap = 0, charge = 1; // the output capacitor: its charge, and what it keeps of it a sample
    bool noiseDac = false;      // whether the noise channel's DAC has been switched on

    // What the output capacitor keeps of its charge each sample at this
    // rate: 0.999958 for each of the 4194304 cycles of the chip's clock.
    static float chargeFactor(float rate) { return std::pow(0.999958f, 4194304.0f / rate); }

    void reset(float rate_) {
        *this = ChipVoice();
        rate = rate_;
        ease = 1.0f - decayMul(0.001f, rate);
        frameInc = 60.0f / rate;
        charge = chargeFactor(rate);
    }
    // The chip's volume register: a level rounded to one of sixteen steps.
    static float steps(float v) { return std::floor(clampf(v, 0.0f, 1.0f) * 15.0f + 0.5f) * (1.0f / 15.0f); }

    // The noise channel's clock from its divisor (0..7) and shift (0..13).
    static float noiseClock(int divisor, int shift) {
        return 524288.0f / (divisor == 0 ? 0.5f : float(divisor)) / std::exp2(float(shift + 1));
    }
    void setNoise(int divisor_, int shift_) {
        divisor = std::clamp(divisor_, 0, 7);
        shift = std::clamp(shift_, 0, 13);
        clockInc = noiseClock(divisor, shift) / rate;
    }

    // One frame of the sequencer: the arpeggio's next note, the duty's next
    // step, the vibrato's next value.
    void nextFrame() {
        ++frame;
        arpNote = (arpNote + 1) % 3;
        if (dutyCount > 0 && frame % dutyFrames == 0) {
            if (++dutyAt >= dutyCount)
                dutyAt = dutyLoop;
            duty = duties[dutyAt];
        }
        if (vibDepth > 0 && float(frame) >= vibDelay)
            vib = 1.0f + vibDepth * std::sin(kTwoPi * (float(frame) - vibDelay) / vibFrames);
    }

    // The wave channel's triangle: thirty-two four-bit samples, up and
    // down, at 32 times the note.
    static int waveLevel(int i) { return i < 16 ? i : 31 - i; }

    inline float step() {
        sweep *= sweepMul;
        float s = sweep;
        if (tickInc > 0) {
            tick += tickInc;
            if (tick >= 1.0f) {
                tick -= 1.0f;
                held = sweep;
            }
            s = held;
        }
        framePh += frameInc;
        if (framePh >= 1.0f) {
            framePh -= 1.0f;
            nextFrame();
        }
        float f = (hzTo + (hz - hzTo) * s) * vib;
        if (arp[1] != 1.0f || arp[2] != 1.0f)
            f *= arp[arpNote];
        float v = 0;
        if (tone != None) {
            const float inc = std::min(f / rate, 0.45f);
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            float w;
            if (tone == Pulse) {
                // Up for the duty, down for the rest, each edge smoothed
                // so it doesn't fold back: 0 to 2, as the DAC sees the
                // level, its middle left in for the capacitor.
                float t2 = ph - duty;
                if (t2 < 0)
                    t2 += 1.0f;
                w = (ph < duty ? 2.0f : 0.0f) + polyBlep(ph, inc) - polyBlep(t2, inc);
            } else {
                // The staircase, and the step just taken (or about to be)
                // smoothed by half its height on either side.
                const float u = ph * 32.0f, du = inc * 32.0f;
                const int i = std::min(int(u), 31);
                const float fu = u - float(i);
                const int now = waveLevel(i);
                w = float(now) * (2.0f / 15.0f);
                if (du < 0.5f) {
                    if (fu < du)
                        w += 0.5f * float(now - waveLevel((i + 31) & 31)) * (2.0f / 15.0f) * polyBlep(fu, du);
                    else if (fu > 1.0f - du)
                        w += 0.5f * float(waveLevel((i + 1) & 31) - now) * (2.0f / 15.0f) * polyBlep(fu, du);
                }
            }
            toneVol *= toneMul;
            toneOut += (steps(toneVol) - toneOut) * ease;
            // The DAC: from below the middle at level 0 up to the note's top.
            v += w * toneOut - kDacRest;
        }
        if (noiseVol > 0 && clockInc > 0) {
            if (shiftEvery > 0 && shift < shiftTo && (shiftCount += 1.0f) >= shiftEvery) {
                shiftCount = 0;
                setNoise(divisor, shift + 1);
            }
            // The bit as it plays through this sample: each time the clock
            // ticks, fifteen bits shift, the first two XORed back in at the
            // top (and at bit six too in the short mode).
            float left = 1.0f, sum = 0.0f;
            for (int guard = 0; guard < 64; ++guard) {
                const float toNext = (1.0f - clockPh) / clockInc;
                if (toNext >= left) {
                    sum += bit * left;
                    clockPh += clockInc * left;
                    break;
                }
                sum += bit * toNext;
                left -= toNext;
                clockPh = 0;
                const uint32_t b = (lfsr ^ (lfsr >> 1)) & 1u;
                lfsr = (lfsr >> 1) | (b << 14);
                if (shortNoise)
                    lfsr = (lfsr & ~0x40u) | (b << 6);
                bit = (lfsr & 1u) ? -1.0f : 1.0f;
            }
            noiseVol *= noiseMul;
            noiseOut += (steps(noiseVol) - noiseOut) * ease;
            v += (sum + 1.0f) * noiseOut - kDacRest;
            noiseDac = true;
        } else if (noiseDac) {
            v -= kDacRest; // switched on and silent: its DAC still sits down there
        }
        // The capacitor on the way out.
        const float out = v - cap;
        cap = v - out * charge;
        return out;
    }
};

// Everything a voice needs from the engine when it starts.
struct Setup {
    float rate = 48000;
    int kit = House; // when the strike names none
    const Harmony *harmony = nullptr;
};

struct Voice {
    static constexpr int kOwnVoice = -1;
    bool active = false;
    int pad = Kick;
    int kit = House;
    int note = 0;    // the degree it was struck on, for the pads that have one
    int delay = 0;   // samples into the block before it starts
    uint32_t serial = 0;
    float t = 0;     // samples since it started
    float gain = 0, gl = 0, gr = 0, send = 0;
    float fade = 1, fadeMul = 1;
    float level = 0;
    float rate = 48000;

    // Oscillators, envelopes and filters, used as each drum needs them.
    float ph[4] = {};
    float hz[4] = {};
    float env[4] = {}, mul[4] = {};
    float k[8] = {};
    float holdSamples = 0;
    SquareOsc sq[12];
    float sqInc[12] = {};
    Partial partial[12];
    Svf f1, f2, f3, f4, f5, f6, f7;
    Rng rng, rng2;
    // Boom bap's sampler: every drum leaves through twelve bits and a dark
    // low-pass, in each ear.
    bool dusty = false;
    Svf dustL, dustR;
    float holdPhase = 0, holdInc = 0, heldL = 0, heldR = 0; // its 26 kHz sample-and-hold
    // A second envelope, and a gate's swell, for the drums that need more.
    float env2 = 0, env2Mul = 1, swell = 0;

    // The instruments of the world kits that are neither drum nor cymbal.
    // The modal ones (the tabla's strokes, the gamelan's bronze, wood,
    // bells, skins struck with a stick) are `modes` partials over a strike
    // of band-passed noise, swelling in over `soft` samples when the mallet
    // is padded; the angklung's are struck again at each shake of the frame.
    int modes = 0;
    float strike = 0, soft = 0;
    // A second burst of noise in a band of its own (f2), for a skin that
    // cracks as well as rings: the djembe's slap.
    float snap = 0, snapEnv = 0, snapMul = 1;
    float shakePh = 0, shakeInc = 0, shakeEnv = 0, shakeMul = 1, shakeLen = 0;
    // A rattle: a modal instrument that strikes itself again and again, a
    // little apart and a little softer each time: bells on a string, the
    // jingles on fire tongs, plates lying loose on one another, a rod
    // scraped along a notched tube. Each strike rings one bell (its
    // `rattleModes` partials, from `rattleFirst`), in turn or at random,
    // or with no bells every partial; an arch swells the strikes up and
    // back down instead of letting them fall.
    int rattleLeft = 0, rattleTotal = 0, rattleBells = 0, rattleModes = 1, rattleFirst = 0, rattleBell = 0;
    float rattleNext = 0, rattleGap = 0, rattleJitter = 0, rattleAccel = 1, rattleLevel = 1, rattleFall = 1;
    bool rattleRandom = false, rattleArch = false;
    // The instruments that sound for as long as they are played: a roll on
    // the angklung keeps it shaking, a roll on the tanpura keeps its strings
    // being plucked. Each step of the roll extends the voice already
    // sounding (Engine::startVoice) instead of starting another.
    bool sustains = false;
    float sustainFor = 0; // samples each step extends it by
    float aux[8] = {};    // the instruments with more state than the rest
    // The rest each have a voice of their own, played by renderNew.
    enum Special { NoSpecial, Jawari, Tanpura, Ge, Slap, Ceng, Vox, Keys, Shakers, Talking };
    Special special = NoSpecial;
    bool chipOn = false;
    ChipVoice chip;

    // One partial of a modal instrument; anything above 16 kHz would only
    // fizz, so it stays silent.
    void mode(int q, float f, float amp, float tau, float bloom = 0) {
        partial[q].set(f, rate, f < 16000.0f ? amp : 0.0f, std::max(tau, 0.002f));
        partial[q].bloom(bloom, rate);
        modes = std::max(modes, q + 1);
    }
    void strikeNoise(float f, float q, float amp, float tau) {
        f1.set(std::min(f, rate * 0.4f), q, rate);
        strike = amp;
        env[3] = 1;
        mul[3] = decayMul(tau, rate);
    }
    // A rattle of `count` strikes `gap` seconds apart (plus up to `jitter`
    // more), each `fall` as loud as the last; its bells silent until struck.
    void rattle(int count, float gap, float jitter, float fall, int bells, int perBell, int first = 0) {
        rattleLeft = rattleTotal = count;
        rattleGap = gap * rate;
        rattleJitter = jitter * rate;
        rattleFall = fall;
        rattleLevel = 1;
        rattleBells = bells;
        rattleModes = perBell;
        rattleFirst = first;
        rattleBell = 0;
        rattleNext = 0;
        rattleAccel = 1;
        rattleRandom = rattleArch = false;
        for (int q = first; q < modes; ++q)
            partial[q].env = 0;
    }
    // One of the strikes: its bell (or every partial) rung, and the strike's
    // noise again.
    void rattleStrike() {
        const int i = rattleTotal - rattleLeft;
        float level = rattleLevel;
        if (rattleArch)
            level = std::sin(kPi * (float(i) + 0.5f) / float(rattleTotal));
        level *= 0.75f + 0.25f * rng.unit();
        if (rattleBells > 0) {
            const int b = rattleRandom ? int(rng.unit() * float(rattleBells)) % rattleBells : rattleBell++ % rattleBells;
            for (int m = 0; m < rattleModes; ++m)
                partial[rattleFirst + b * rattleModes + m].env = level;
        } else {
            for (int q = rattleFirst; q < modes; ++q)
                partial[q].env = std::max(partial[q].env, level);
        }
        env[3] = std::max(env[3], level);
        rattleLevel *= rattleFall;
        --rattleLeft;
        rattleNext += rattleGap + rattleJitter * rng.unit();
        rattleGap *= rattleAccel;
    }

    // A roll's next step reaches an instrument that sounds as long as it is
    // played: it goes on sounding, `sustainFor` from now.
    void extend() {
        if (special == Tanpura)
            aux[4] = t + sustainFor; // plucks go on until then
        else
            shakeLen = t + sustainFor;
        fade = 1;
    }

    // The world kits' instruments, set up for a hit. True when this pad in
    // this kit is one of them; pan and send are the voice's.
    bool startNew(const Strike &s, const Harmony &h, float decayScale, float tone, float ratio, float edge, float &pan) {
        const int degrees = s.degrees;
        auto dm = [&](float seconds) { return decayMul(seconds * decayScale, rate); };
        if (kit == Chip)
            return pad != Sub && startChip(s, h, decayScale, tone, ratio, edge, pan);
        if (kit == Tabla)
            return startTabla(s, h, decayScale, tone, ratio, edge, pan);
        if (kit == Gamelan)
            return startGamelan(s, h, decayScale, tone, ratio, edge, pan);
        if (kit == Djembe)
            return startDjembe(s, h, decayScale, tone, ratio, edge, pan);
        if (kit != Amapiano)
            return false;
        switch (pad) {
        case Snare: {
            // The woody rim: a stick laid on a wooden shell's rim and
            // struck softly, a hollow knock with hardly any ring; harder
            // toward the far edge.
            const float c = 1.0f + 0.6f * std::max(0.0f, edge);
            mode(0, 520.0f * ratio, 1.0f, 0.03f * decayScale);
            mode(1, 1340.0f * ratio, 0.6f * c, 0.02f * decayScale);
            mode(2, 2610.0f * ratio, 0.35f * c * std::exp2(tone), 0.012f * decayScale);
            mode(3, 3900.0f * ratio, 0.2f * c * std::exp2(tone), 0.008f * decayScale);
            strikeNoise(3000.0f, 0.9f, 1.2f * c, 0.0015f);
            gain = 0.2f;
            pan = -0.4f; // answering the clicks on the right
            send = 0.35f;
            return true;
        }
        case Rim: {
            // The private-school click: a short, high, dry tick of wood.
            mode(0, 2350.0f * ratio, 1.0f, 0.01f * decayScale);
            mode(1, 3900.0f * ratio, 0.6f * std::exp2(tone), 0.007f * decayScale);
            mode(2, 6100.0f * ratio, 0.3f * std::exp2(tone), 0.005f * decayScale);
            strikeNoise(6000.0f, 1.0f, 1.3f, 0.0008f);
            gain = 0.12f;
            pan = 0.5f + 0.3f * (s.sx - 0.5f);
            send = 0.25f;
            return true;
        }
        case TomLo:
        case TomMid:
        case TomHi: {
            // Muted toms, the private-school fill: a skin damped by a cloth,
            // its modes gone in a tenth of a second, the thud of the stick.
            const int deg = pad == TomLo ? 0 : pad == TomMid ? 2 : 4;
            float f = degreeHz(h, deg + degrees, 140.0f);
            if (pad != TomLo && f < degreeHz(h, degrees, 140.0f))
                f *= 2.0f;
            const float b = std::exp2(tone);
            mode(0, f, 1.0f, 0.13f * decayScale);
            mode(1, f * 1.59f, 0.45f * b, 0.06f * decayScale);
            mode(2, f * 2.14f, 0.3f * b, 0.04f * decayScale);
            mode(3, f * 2.65f, 0.15f * b, 0.025f * decayScale);
            strikeNoise(900.0f, 0.7f, 1.0f, 0.006f);
            soft = 0.001f * rate;
            gain = 0.27f;
            pan = pad == TomLo ? -0.35f : pad == TomMid ? 0.0f : 0.35f;
            send = 0.25f;
            return true;
        }
        case Perc: {
            // A wood block in key: a hollow log's tok, its modes far
            // apart, gone in a tenth of a second.
            const float f = degreeHz(h, 4 + degrees, 700.0f);
            mode(0, f, 1.0f, 0.1f * decayScale);
            mode(1, f * 2.57f, 0.35f * std::exp2(tone), 0.045f * decayScale);
            mode(2, f * 4.15f, 0.15f * std::exp2(tone), 0.025f * decayScale);
            strikeNoise(3000.0f, 1.0f, 0.8f, 0.0015f);
            gain = 0.36f;
            pan = -0.35f + 0.3f * (s.sx - 0.5f);
            send = 0.3f;
            return true;
        }
        case Bell: {
            // A marimba: a rosewood bar cut so its second mode rings two
            // octaves up and its third near the tenth harmonic, struck
            // with a yarn mallet, its tube resonator holding the note.
            const float f = degreeHz(h, 4 + degrees, 520.0f);
            const float b = std::exp2(tone);
            mode(0, f, 1.0f, 0.55f * decayScale * (1.0f + 0.4f * std::max(0.0f, edge)));
            mode(1, f * 3.93f, 0.3f * b, 0.12f * decayScale);
            mode(2, f * 9.8f, 0.06f * b, 0.03f * decayScale);
            soft = 0.002f * rate;
            strikeNoise(1200.0f, 0.7f, 0.3f, 0.004f);
            gain = 0.27f;
            pan = 0.3f + 0.3f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Shaker: {
            // The shaker stack amapiano is led by: a tight, bright shaker
            // and a grainier one a few milliseconds behind it, its beads
            // heard one by one.
            special = Shakers;
            k[0] = (0.004f + 0.006f * std::max(0.0f, edge)) * rate; // the swell
            k[1] = 0.007f * rate;                                    // the second shaker, behind
            k[2] = 0.25f;                                            // its beads, per sample
            k[3] = 0;                                                // no gourd
            env[0] = 1;
            mul[0] = dm(0.03f * (1.0f + 0.3f * tone));
            env[1] = 1;
            mul[1] = dm(0.05f * (1.0f + 0.3f * tone));
            f1.set(std::min(8500.0f * ratio * std::exp2(0.3f * tone), rate * 0.42f), 1.0f, rate);
            f2.set(std::min(5500.0f * ratio, rate * 0.42f), 1.2f, rate);
            f4.set(3000.0f, 0.7f, rate);
            f5.set(std::min(13000.0f, rate * 0.4f), 0.6f, rate);
            k[5] = 0.9f; // the two shakers apart, one in each ear
            f6.set(3000.0f, 0.7f, rate);
            f7.set(std::min(13000.0f, rate * 0.4f), 0.6f, rate);
            gain = 0.08f;
            pan = 0.1f * (s.sx - 0.5f);
            send = 0.15f;
            return true;
        }
        case Clap: {
            // The vocal chop: a voice, cut from a sample, saying "ay"
            // (struck at the far edge, "oh") on the key's root, its vowel
            // moving as it speaks and the chop shutting it off.
            special = Vox;
            hz[0] = degreeHz(h, 0, 250.0f) * ratio;
            const bool oh = edge > 0.3f;
            // The two vowels' formants: from, then to.
            aux[0] = oh ? 650.0f : 800.0f, aux[1] = oh ? 1000.0f : 1250.0f, aux[2] = 2600.0f;
            aux[3] = oh ? 420.0f : 450.0f, aux[4] = oh ? 800.0f : 2000.0f, aux[5] = oh ? 2500.0f : 2750.0f;
            aux[6] = std::exp2(0.2f * tone); // the throat, shorter or longer
            k[0] = 0.006f * rate;               // the attack
            k[1] = 0.15f * decayScale * rate;   // where the chop cuts it
            env[0] = 1;
            mul[0] = decayMul(0.6f, rate);      // while it speaks
            mul[1] = decayMul(0.02f, rate);     // once cut
            env[2] = 1;
            mul[2] = decayMul(0.06f, rate);     // the vowel's move
            f2.set(std::min(5500.0f, rate * 0.4f), 0.7f, rate);
            gain = 0.45f;
            pan = 0.1f;
            send = 0.4f;
            return true;
        }
        case Zap: {
            // Keys: a Rhodes stab of the chord playing, its notes spread
            // across the stereo, each a tine's sine with the bark of its
            // modulation at the attack, through a low-pass that keeps it
            // in the background, the way amapiano filters its piano.
            special = Keys;
            const int n = std::clamp(h.chordCount, 1, 4);
            float last = 0;
            for (int i = 0; i < 4; ++i) {
                if (i >= n) {
                    hz[i] = 0;
                    continue;
                }
                float f = degreeHz(h, h.chordTones[i] + degrees, i ? last * 1.2f : 260.0f);
                while (i && f <= last * 1.01f)
                    f *= 2.0f;
                hz[i] = f;
                last = f;
                ph[i] = 0;
                aux[i] = 0;
            }
            k[4] = float(n);
            k[0] = 0.003f * rate;                                         // the attack
            k[1] = (0.2f + 0.25f * std::max(0.0f, edge)) * decayScale * rate; // the stab lets go
            k[2] = 1.6f * std::exp2(tone);                                // the bark
            env[0] = 1;
            mul[0] = decayMul(0.9f, rate);
            mul[1] = decayMul(0.08f, rate);
            env[2] = 1;
            mul[2] = decayMul(0.06f, rate);
            f2.set(std::min(1800.0f * std::exp2(tone), rate * 0.4f), 0.9f, rate);
            f3.set(std::min(1800.0f * std::exp2(tone), rate * 0.4f), 0.9f, rate);
            gain = 0.36f;
            send = 0.3f;
            return true;
        }
        default: return false;
        }
    }

    // The tabla's bols, and its kit's other instruments.
    bool startTabla(const Strike &s, const Harmony &h, float decayScale, float tone, float ratio, float edge,
                    float &pan) {
        const int degrees = s.degrees;
        // The dayan is tuned to the key's root, Sa, near 280 Hz; its black
        // syahi makes the skin's modes nearly harmonic (1, 2, 3, 4, 5, each
        // a hair sharp), which is why a tabla rings in tune. Each stroke
        // excites them differently.
        const float sa = degreeHz(h, 0, 280.0f) * ratio;
        // A hand never strikes the dayan twice alike, and a fast bol shows
        // it: each stroke lands a few cents off (the skin under the hand),
        // the ring finger resting more or less firmly on it (how much of
        // the fundamental it damps), the syahi's second mode speaking more
        // or less, and the ring a little longer or shorter.
        const float lean = std::exp2(7.0f * rng2.bipolar() / 1200.0f);
        const float damp = 0.45f + 1.1f * rng2.unit();
        const float syahi = 0.75f + 0.5f * rng2.unit();
        const float ring = 0.85f + 0.3f * rng2.unit();
        const float slap = 0.8f + 0.4f * rng2.unit();
        switch (pad) {
        case Kick: {
            // Ge: the bayan struck open with the fingers while the heel of
            // the hand presses into the skin, so the pitch rises after the
            // stroke (the gumki), by two semitones struck near the pad's
            // near edge and five at its far one. The bayan's syahi sits off
            // centre, so its modes are only roughly harmonic.
            special = Ge;
            hz[0] = kickHz(h, degrees, 85.0f) * ratio;
            k[0] = 2.0f + 3.0f * clampf((s.sy - 0.1f) / 0.8f, 0.0f, 1.0f); // the gumki, semitones
            k[1] = decayMul(0.06f, rate);                                   // how fast the heel presses
            k[2] = std::exp2(tone);
            env[0] = 1;
            mul[0] = decayMul(0.5f * decayScale, rate);
            env[1] = 1;
            mul[1] = decayMul(0.15f * decayScale, rate);
            env[2] = 1; // the thump of the fingers
            mul[2] = decayMul(0.012f, rate);
            k[3] = 1; // the gumki still to come, falling to 0 as the heel presses
            f1.set(220.0f, 0.7f, rate);
            gain = 0.55f;
            pan = -0.15f;
            send = 0.15f;
            return true;
        }
        case Clap:
            // Ka: the bayan slapped flat with the palm and left there: a
            // papery crack and the flat slap of the hand, no note in it.
            special = Slap;
            env[0] = 1;
            mul[0] = decayMul(0.006f * decayScale, rate);
            env[1] = 1;
            mul[1] = decayMul(0.014f * decayScale, rate);
            f1.set(std::min(2600.0f * std::exp2(0.5f * tone) * ratio, rate * 0.4f), 0.7f, rate);
            f2.set(700.0f * ratio, 0.6f, rate);
            f3.set(std::min(9000.0f, rate * 0.4f), 0.6f, rate);
            gain = 0.28f;
            pan = -0.2f;
            send = 0.2f;
            return true;
        case Snare: {
            // Na: the rim struck while a finger rests on the skin, so the
            // fundamental is damped and the harmonics ring.
            const float r = 1.0f + 0.4f * std::max(0.0f, edge);
            static const float ratios[6] = {1.0f, 2.0f, 3.0f, 4.02f, 5.04f, 6.1f};
            static const float amps[6] = {0.2f, 1.0f, 0.6f, 0.4f, 0.25f, 0.12f};
            static const float taus[6] = {0.06f, 0.42f, 0.3f, 0.2f, 0.14f, 0.08f};
            const float hand[6] = {damp, syahi, 1.0f, 1.0f, 1.0f, 1.0f};
            for (int q = 0; q < 6; ++q)
                mode(q, sa * lean * ratios[q], amps[q] * hand[q] * (q > 2 ? std::exp2(tone) : 1.0f),
                     taus[q] * decayScale * r * (q ? ring : 2.0f - damp));
            strikeNoise(3500.0f, 0.9f, 1.2f * slap, 0.003f);
            gain = 0.15f;
            pan = 0.2f;
            send = 0.3f;
            return true;
        }
        case Hat: {
            // Te: the dayan struck flat and held there, all damped.
            static const float amps[3] = {0.5f, 0.6f, 0.4f};
            static const float taus[3] = {0.02f, 0.018f, 0.012f};
            for (int q = 0; q < 3; ++q)
                mode(q, sa * lean * float(q + 1), amps[q] * (q == 1 ? syahi : 1.0f),
                     taus[q] * ring * decayScale * (1.0f + std::max(0.0f, edge)));
            strikeNoise(2500.0f * std::exp2(0.5f * tone) * lean, 0.8f, 1.5f * slap, 0.006f);
            gain = 0.12f;
            pan = 0.25f + 0.25f * (s.sx - 0.5f);
            send = 0.15f;
            return true;
        }
        case Rim: {
            // Tak: a sharp, closed crack off the rim.
            static const float ratios[4] = {2.0f, 3.0f, 4.02f, 5.04f};
            static const float amps[4] = {1.0f, 0.6f, 0.5f, 0.3f};
            static const float taus[4] = {0.07f, 0.05f, 0.04f, 0.03f};
            for (int q = 0; q < 4; ++q)
                mode(q, sa * lean * ratios[q], amps[q] * (q == 0 ? syahi : 1.0f), taus[q] * ring * decayScale);
            strikeNoise(4500.0f * std::exp2(0.5f * tone), 1.0f, 1.6f * slap, 0.002f);
            gain = 0.14f;
            pan = 0.15f;
            send = 0.3f;
            return true;
        }
        case TomLo:
        case TomMid:
        case TomHi: {
            // A tabla tarang: a row of dayans tuned to the scale, played
            // with na, its fundamental let ring so the note is heard.
            const int deg = pad == TomLo ? 0 : pad == TomMid ? 2 : 4;
            float f = degreeHz(h, deg + degrees, 280.0f);
            if (pad != TomLo && f < degreeHz(h, degrees, 280.0f))
                f *= 2.0f;
            static const float amps[5] = {0.5f, 1.0f, 0.5f, 0.3f, 0.18f};
            static const float taus[5] = {0.25f, 0.4f, 0.28f, 0.18f, 0.12f};
            static const float ratios[5] = {1.0f, 2.0f, 3.0f, 4.02f, 5.04f};
            // The tarang's fundamental is let ring, so the finger damps it
            // only a little.
            const float hand[5] = {0.9f + 0.2f * damp, 0.8f + 0.2f * syahi, 1.0f, 1.0f, 1.0f};
            for (int q = 0; q < 5; ++q)
                mode(q, f * lean * ratios[q], amps[q] * hand[q] * (q > 1 ? std::exp2(tone) : 1.0f),
                     taus[q] * ring * decayScale);
            strikeNoise(3200.0f, 0.9f, 0.9f * slap, 0.003f);
            gain = 0.13f;
            pan = pad == TomLo ? -0.3f : pad == TomMid ? 0.0f : 0.3f;
            send = 0.3f;
            return true;
        }
        case Perc: {
            // Tun: the centre struck with a finger that lifts at once, so
            // the fundamental rings out, round and long.
            const float f = degreeHz(h, degrees, 280.0f);
            static const float amps[4] = {1.0f, 0.3f, 0.12f, 0.06f};
            static const float taus[4] = {0.55f, 0.25f, 0.15f, 0.1f};
            for (int q = 0; q < 4; ++q)
                mode(q, f * lean * float(q + 1), amps[q] * (q == 1 ? syahi : 1.0f) * (q ? std::exp2(tone) : 1.0f),
                     taus[q] * ring * decayScale);
            strikeNoise(1500.0f, 0.8f, 0.6f * slap, 0.004f);
            gain = 0.19f;
            pan = -0.3f + 0.3f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Ride: {
            // Manjira: two small, thick cymbals struck together, each mode
            // doubled a few cents apart so the pair beats. Struck at the far
            // edge they are held together: a short chik.
            const float base = 1750.0f * ratio * std::exp2(0.25f * tone);
            const float r = edge > 0.3f ? 0.12f : 1.0f;
            static const float ratios[7] = {1.0f, 1.0045f, 1.58f, 2.31f, 2.318f, 3.44f, 4.9f};
            static const float amps[7] = {1.0f, 0.8f, 0.5f, 0.55f, 0.4f, 0.3f, 0.2f};
            static const float taus[7] = {1.0f, 1.0f, 0.6f, 0.55f, 0.55f, 0.35f, 0.25f};
            for (int q = 0; q < 7; ++q)
                mode(q, base * ratios[q], amps[q], taus[q] * decayScale * r);
            strikeNoise(7000.0f, 0.8f, 0.5f, 0.003f);
            gain = 0.045f;
            pan = 0.3f;
            send = 0.15f;
            return true;
        }
        case Bell: {
            // Ghanti: a temple bell, its partials a bell's (the hum an
            // octave down, a minor third, a fifth, the nominal an octave
            // up), the hum doubled so it slowly wavers.
            const float f = degreeHz(h, 4 + degrees, 700.0f);
            static const float ratios[7] = {0.5f, 0.5025f, 1.0f, 1.19f, 1.5f, 2.0f, 2.66f};
            static const float amps[7] = {0.4f, 0.3f, 1.0f, 0.4f, 0.3f, 0.55f, 0.25f};
            static const float taus[7] = {1.1f, 1.1f, 0.95f, 0.8f, 0.7f, 0.6f, 0.4f};
            for (int q = 0; q < 7; ++q)
                mode(q, f * ratios[q], amps[q] * (q > 4 ? std::exp2(tone) : 1.0f),
                     taus[q] * decayScale * (1.0f + 0.3f * edge));
            strikeNoise(3000.0f, 1.0f, 0.6f, 0.004f);
            gain = 0.085f;
            pan = 0.3f + 0.2f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Shaker: {
            // Ghungroo: a string of little brass bells on a dancer's ankle,
            // ringing one after another. Each is a closed bell with a pellet
            // inside: two inharmonic modes, its own pitch, a short ring.
            for (int b = 0; b < 6; ++b) {
                const float f = (3300.0f + 2300.0f * rng.unit()) * ratio * std::exp2(0.3f * tone);
                mode(2 * b, f, 1.0f, (0.12f + 0.06f * rng.unit()) * decayScale);
                mode(2 * b + 1, f * 2.37f, 0.45f, 0.05f * decayScale);
            }
            rattle(6, 0.003f, 0.011f, 0.8f, 6, 2);
            strikeNoise(7000.0f, 1.0f, 0.5f, 0.0015f);
            gain = 0.05f;
            pan = 0.3f + 0.3f * (s.sx - 0.5f);
            send = 0.2f;
            return true;
        }
        case Open: {
            // Chimta: fire tongs hung with pairs of brass jingles, clashed
            // open so the jingles chatter against each other for a moment,
            // over the steel of the tongs ringing. Each jingle pair is two
            // modes of a little cymbal; they rattle at random.
            mode(0, 880.0f * ratio, 0.35f, 0.45f * decayScale);
            mode(1, 2410.0f * ratio, 0.2f, 0.25f * decayScale);
            for (int b = 0; b < 5; ++b) {
                const float f = (2300.0f + 1900.0f * rng.unit()) * ratio * std::exp2(0.3f * tone);
                mode(2 + 2 * b, f, 1.0f, 0.09f * decayScale);
                mode(3 + 2 * b, f * 1.48f, 0.6f, 0.06f * decayScale);
            }
            rattle(edge > 0.3f ? 16 : 10, 0.014f, 0.016f, 0.86f, 5, 2, 2);
            rattleRandom = true;
            strikeNoise(8000.0f, 1.0f, 0.4f, 0.002f);
            gain = 0.07f;
            pan = 0.22f + 0.25f * (s.sx - 0.5f);
            send = 0.15f;
            return true;
        }
        case Crash: {
            // The tanpura: four strings plucked in turn, Pa, Sa, Sa and the
            // low Sa (Ma for Pa, struck at the far edge), each left to ring
            // for seconds. On a tanpura the strings lie on a broad, curved
            // bridge with a thread under them (the jawari): as a string
            // swings, its contact point slides along the bridge and a band
            // of its harmonics sweeps slowly through the tone, from high
            // after the pluck down toward the low ones, wavering as it
            // goes. Held, it goes on through its cycle as long as it is
            // played.
            special = Tanpura;
            const float sa = degreeHz(h, 0, 130.0f) * ratio;
            hz[0] = sa * (edge > 0.3f ? 2.0f / 3.0f : 0.75f); // Pa (or Ma) below Sa
            hz[1] = sa;
            hz[2] = sa + 0.15f; // the two Sa strings a little apart, so they shimmer
            hz[3] = sa * 0.5f;
            for (int i = 0; i < 4; ++i) {
                ph[i] = rng.unit();
                env[i] = 0;
                mul[i] = decayMul((i == 3 ? 2.2f : 1.7f) * decayScale, rate);
                aux[i] = -1; // seconds since its pluck, as samples; −1: not yet plucked
            }
            k[0] = 0;                // the next pluck, in samples
            k[1] = 0.55f * rate;     // between plucks
            k[2] = 0;                // which string is next
            k[3] = std::exp2(0.5f * tone);
            k[4] = 0.012f * rate;    // a pluck's attack
            k[5] = 1.0f - decayMul(0.004f, rate);
            aux[4] = 3.5f * k[1];    // plucks a whole cycle, then rings out
            aux[5] = 0;              // the band's coefficients, refreshed every few samples
            sustains = true;
            sustainFor = 3.5f * k[1];
            f2.set(std::min(7000.0f, rate * 0.4f), 0.6f, rate);
            f7.set(40.0f, 0.7f, rate);
            gain = 0.08f;
            pan = -0.25f;
            send = 0.35f;
            return true;
        }
        case Zap: {
            // The sitar: a band-limited sawtooth through a resonant
            // band-pass that falls fast after the pluck, "dzing" (the
            // jawari's buzz), and struck at the far edge it bends up to the
            // next note (a meend). Its sympathetic strings, an octave and a
            // twelfth up, ring in after the pluck.
            special = Jawari;
            hz[0] = degreeHz(h, degrees, 330.0f);
            env[0] = 1;
            mul[0] = decayMul(0.7f * decayScale, rate);
            env[1] = 1;
            mul[1] = decayMul(0.22f, rate);
            k[0] = 0.0015f * rate; // the pluck's attack
            k[1] = 2.5f;           // the band's lowest harmonic
            k[2] = 10.0f;          // and how far above it the pluck starts it
            k[3] = 0.0f;
            k[4] = std::exp2(0.5f * tone);
            k[5] = 1.0f; // the meend's ratio
            if (edge > 0.25f)
                k[5] = degreeHz(h, degrees + 1, 330.0f) / hz[0];
            k[6] = 0.14f * rate; // when it bends
            k[7] = 0.12f * rate; // over how long
            ph[2] = 0.25f;
            f2.set(std::min(6500.0f, rate * 0.4f), 0.6f, rate);
            partial[0].set(hz[0] * 2.0f, rate, 0.12f, 1.0f * decayScale);
            partial[0].bloom(0.15f, rate);
            partial[1].set(hz[0] * 3.0f, rate, 0.08f, 0.8f * decayScale);
            partial[1].bloom(0.2f, rate);
            partial[2].set(hz[0] * 4.0f, rate, 0.0f, 0.8f * decayScale);
            gain = 0.12f;
            pan = -0.15f + 0.4f * (s.sx - 0.5f);
            send = 0.3f;
            return true;
        }
        default: return false;
        }
    }

    // The gamelan: bronze in its own tuning (gamelanHz).
    bool startGamelan(const Strike &s, const Harmony &h, float decayScale, float tone, float ratio, float edge,
                      float &pan) {
        const int degrees = s.degrees;
        const float b = std::exp2(tone);
        switch (pad) {
        case TomLo:
        case TomMid:
        case TomHi: {
            // Saron: thick bronze bars struck with a horn mallet. A free
            // bar's modes sit at 1, 2.76 and 5.4; each note is played on two
            // instruments tuned a few hertz apart, so the pair beats (the
            // ombak, the gamelan's shimmer).
            const int deg = pad == TomLo ? 0 : pad == TomMid ? 2 : 4;
            float f = gamelanHz(h, deg + degrees, 520.0f);
            if (pad != TomLo && f < gamelanHz(h, degrees, 520.0f))
                f *= 2.0f;
            // The horn mallet is hard, so the bar's upper modes ring for a
            // moment too, each doubled on the twin and beating faster the
            // higher it is (the ombak shimmers up there as well), over the
            // bright tick of the horn on bronze.
            mode(0, f, 1.0f, 1.0f * decayScale);
            mode(1, f + 6.5f, 0.85f, 1.0f * decayScale);
            mode(2, f * 2.76f, 0.35f * b, 0.3f * decayScale);
            mode(3, f * 2.76f + 9.0f, 0.25f * b, 0.3f * decayScale);
            mode(4, f * 5.4f, 0.16f * b, 0.1f * decayScale);
            mode(5, f * 5.4f + 13.0f, 0.13f * b, 0.1f * decayScale);
            mode(6, f * 8.93f, 0.12f * b, 0.045f * decayScale);
            mode(7, f * 8.93f + 21.0f, 0.1f * b, 0.045f * decayScale);
            mode(8, f * 13.3f, 0.08f * b, 0.025f * decayScale);
            mode(9, f * 13.3f + 31.0f, 0.06f * b, 0.025f * decayScale);
            strikeNoise(7500.0f, 0.8f, 0.9f + 0.5f * std::max(0.0f, edge), 0.0025f);
            gain = 0.11f;
            pan = pad == TomLo ? -0.3f : pad == TomMid ? 0.0f : 0.3f;
            send = 0.3f;
            return true;
        }
        case Perc: {
            // Bonang: a kettle gong on a frame, struck with a padded stick:
            // a round fundamental (paired, so it beats) and a few dim
            // partials above.
            const float f = gamelanHz(h, 2 + degrees, 520.0f);
            // The kettle's rim rings high and briefly, its twin beating
            // against it there too.
            mode(0, f, 1.0f, 0.7f * decayScale);
            mode(1, f + 5.0f, 0.7f, 0.7f * decayScale);
            mode(2, f * 2.94f, 0.25f * b, 0.25f * decayScale);
            mode(3, f * 4.1f, 0.14f * b, 0.12f * decayScale);
            mode(4, f * 4.1f + 11.0f, 0.1f * b, 0.12f * decayScale);
            mode(5, f * 7.3f, 0.08f * b, 0.05f * decayScale);
            mode(6, f * 7.3f + 17.0f, 0.06f * b, 0.05f * decayScale);
            mode(7, f * 11.6f, 0.05f * b, 0.025f * decayScale);
            soft = 0.0015f * rate;
            strikeNoise(4500.0f, 0.8f, 0.45f, 0.003f);
            gain = 0.13f;
            pan = -0.35f + 0.3f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Open: {
            // Kenong: the big kettle gong lying on cords in its wooden nest,
            // struck with a padded stick to mark the phrase, left to ring,
            // its twin beating slowly under it.
            const float f = gamelanHz(h, degrees, 440.0f) * ratio;
            mode(0, f, 1.0f, 2.4f * decayScale);
            mode(1, f + 1.6f, 0.85f, 2.4f * decayScale);
            mode(2, f * 1.52f, 0.12f * b, 0.6f * decayScale, 0.04f);
            mode(3, f * 2.08f, 0.3f * b, 1.0f * decayScale, 0.05f);
            mode(4, f * 3.1f, 0.12f * b, 0.4f * decayScale, 0.08f);
            soft = 0.008f * rate;
            strikeNoise(500.0f, 0.7f, 0.3f, 0.008f);
            gain = 0.1f;
            pan = 0.2f + 0.2f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Rim: {
            // Ketuk: a small kettle gong lying flat, struck and damped at
            // once by the stick left on it: a dead "tuk" with a pitch in it.
            const float f = gamelanHz(h, 1, 330.0f) * ratio;
            const float r = 1.0f + 0.8f * std::max(0.0f, edge);
            mode(0, f, 1.0f, 0.07f * decayScale * r);
            mode(1, f + 3.0f, 0.8f, 0.07f * decayScale * r);
            mode(2, f * 2.9f, 0.2f * b, 0.03f * decayScale);
            strikeNoise(700.0f, 0.7f, 0.8f, 0.006f);
            gain = 0.16f;
            pan = -0.2f;
            send = 0.3f;
            return true;
        }
        case Shaker: {
            // Slenthem: the lowest of the bars, thin bronze hung over a tube
            // that holds its fundamental, struck with a padded disc: a long,
            // dark, beating note, the balungan an octave under the sarons.
            const float f = gamelanHz(h, degrees, 230.0f) * ratio;
            mode(0, f, 1.0f, 2.2f * decayScale);
            mode(1, f + 2.0f, 0.85f, 2.2f * decayScale);
            mode(2, f * 2.76f, 0.12f * b, 0.5f * decayScale);
            mode(3, f * 2.76f + 3.0f, 0.08f * b, 0.5f * decayScale);
            soft = 0.008f * rate;
            strikeNoise(400.0f, 0.7f, 0.2f, 0.008f);
            gain = 0.09f;
            pan = -0.3f;
            send = 0.3f;
            return true;
        }
        case Hat: {
            // Kecrek: iron plates hung loose on the puppet box, kicked by the
            // dalang's foot: they clatter on each other, a few strikes in a
            // few milliseconds, each plate's inharmonic modes damped by the
            // others lying on it. Loose iron never lands twice alike: each
            // kick sets the plates against one another differently, so
            // every plate's pitch moves by a few percent from one kick to
            // the next and its modes stop keeping to fixed ratios, and two
            // to four of them (more at the far edge) clatter in no fixed
            // order; the iron's top is a hiss of its own.
            static const float ratios[4] = {1.0f, 1.63f, 2.53f, 4.1f};
            static const float plate[3] = {0.9f, 1.0f, 1.13f};
            const float base = 2300.0f * ratio * std::exp2(0.3f * tone);
            for (int p = 0; p < 3; ++p) {
                const float pf = base * plate[p] * (1.0f + 0.035f * rng.bipolar());
                for (int m = 0; m < 4; ++m)
                    mode(4 * p + m, pf * ratios[m] * (m ? 1.0f + 0.02f * rng.bipolar() : 1.0f),
                         (0.6f + 0.4f * rng.unit()) / (1.0f + 0.5f * float(m)), (0.11f - 0.02f * float(m)) * decayScale);
            }
            rattle(2 + int(rng.unit() * 2.99f) + (edge > 0.3f ? 2 : 0), 0.003f, 0.006f, 0.75f, 3, 4);
            rattleRandom = true;
            strikeNoise(9000.0f, 0.6f, 2.5f, 0.014f);
            gain = 0.08f;
            pan = 0.3f + 0.25f * (s.sx - 0.5f);
            send = 0.2f;
            return true;
        }
        case Bell:
        case Crash: {
            // Kempul and the great gong, the gong ageng: hanging gongs. The
            // fundamental is doubled a fraction of a hertz off, so it swells
            // and ebbs (the great gong breathes, slower than once a second);
            // the partials above bloom in after the soft mallet lands, the
            // higher the later. The gong ageng sits in the key's root
            // around 50 Hz and rings for ten seconds and more; a laptop
            // hears it through its second and third partials.
            const bool gong = pad == Crash;
            if (gong) {
                const float f = gamelanHz(h, 0, 50.0f) * ratio;
                const float t0 = 2.2f * decayScale;
                static const float ratios[10] = {1.0f, 1.0f, 2.02f, 2.02f, 2.93f, 4.08f, 5.2f, 6.4f, 7.7f, 9.1f};
                static const float twin[10] = {0.0f, 0.7f, 0.0f, 1.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                static const float amps[10] = {1.0f, 0.9f, 0.5f, 0.45f, 0.4f, 0.3f, 0.22f, 0.14f, 0.09f, 0.06f};
                static const float taus[10] = {1.0f, 1.0f, 0.85f, 0.85f, 0.65f, 0.45f, 0.36f, 0.27f, 0.2f, 0.16f};
                static const float blooms[10] = {0.0f, 0.0f, 0.12f, 0.12f, 0.25f, 0.35f, 0.45f, 0.5f, 0.55f, 0.6f};
                for (int q = 0; q < 10; ++q)
                    mode(q, f * ratios[q] + twin[q], amps[q] * (q > 1 ? b : 1.0f), t0 * taus[q], blooms[q]);
                soft = 0.03f * rate;
                strikeNoise(120.0f, 0.7f, 0.5f, 0.03f);
                gain = 0.08f;
                pan = -0.1f;
                send = 0.25f;
                return true;
            }
            const float f = gamelanHz(h, 2 + degrees, 220.0f);
            const float t0 = 1.0f * decayScale;
            mode(0, f, 1.0f, t0);
            mode(1, f + 2.2f, 0.9f, t0);
            mode(2, f * 2.01f, 0.35f * b, t0 * 0.8f, 0.05f);
            mode(3, f * 2.94f, 0.3f * b, t0 * 0.7f, 0.08f);
            mode(4, f * 4.08f, 0.22f * b, t0 * 0.55f, 0.11f);
            mode(5, f * 5.22f, 0.12f * b, t0 * 0.45f, 0.14f);
            soft = 0.006f * rate;
            strikeNoise(600.0f, 0.7f, 0.35f, 0.006f);
            gain = 0.12f;
            pan = 0.25f + 0.3f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Ride: {
            // Kempyang: a small kettle gong muted by the player's hand,
            // keeping time, a short bright "tuk" on the root.
            const float f = gamelanHz(h, 0, 1100.0f) * ratio;
            const float r = 1.0f + std::max(0.0f, edge);
            mode(0, f, 1.0f, 0.22f * decayScale * r);
            mode(1, f + 7.0f, 0.8f, 0.22f * decayScale * r);
            mode(2, f * 2.7f, 0.3f * b, 0.08f * decayScale);
            mode(3, f * 2.7f + 15.0f, 0.22f * b, 0.08f * decayScale);
            mode(4, f * 4.2f, 0.18f * b, 0.04f * decayScale);
            mode(5, f * 6.6f, 0.1f * b, 0.02f * decayScale);
            strikeNoise(8000.0f, 1.0f, 0.8f, 0.002f);
            gain = 0.06f;
            pan = 0.3f;
            send = 0.2f;
            return true;
        }
        case Zap: {
            // Angklung: bamboo tubes tuned an octave apart in a frame,
            // shaken so they knock again and again, eleven times a second,
            // for as long as the shake lasts: half a second for a tap, and
            // as long as the pad is held (a roll keeps it shaking).
            const float f = gamelanHz(h, degrees, 620.0f);
            mode(0, f, 1.0f, 0.5f * decayScale);
            mode(1, f * 2.0f, 0.6f, 0.4f * decayScale);
            mode(2, f * 3.0f, 0.15f * b, 0.2f * decayScale);
            mode(3, f * 4.0f, 0.12f * b, 0.15f * decayScale);
            mode(4, f * 5.6f, 0.1f, 0.05f * decayScale);
            strikeNoise(2500.0f, 0.9f, 0.4f, 0.004f);
            shakeInc = (11.0f + 3.0f * std::max(0.0f, edge)) / rate;
            shakePh = 0.999f;
            shakeEnv = 0;
            shakeMul = decayMul(0.035f, rate);
            shakeLen = 0.5f * decayScale * rate;
            sustains = true;
            sustainFor = 0.6f * rate;
            gain = 0.15f;
            pan = -0.15f + 0.4f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Clap: {
            // Ceng-ceng: little cymbals clashed against a set on a frame,
            // two players interlocking, one in each ear: left, right, left,
            // right in a breath, then each set's own sizzle. k: 0..3 the
            // clashes, in samples.
            static const float ratios[6] = {1.0f, 1.41f, 1.73f, 2.13f, 2.59f, 3.07f};
            const float base = 2200.0f * ratio * std::exp2(0.3f * tone);
            for (int i = 0; i < 6; ++i) {
                sq[i].phase = rng.unit();
                sq[i + 6].phase = rng.unit();
                sqInc[i] = std::min(base * ratios[i] / rate, 0.45f);
                sqInc[i + 6] = std::min(base * 1.19f * ratios[i] / rate, 0.45f);
            }
            k[0] = 0;
            k[1] = 0.009f * rate;
            k[2] = 0.021f * rate;
            k[3] = 0.031f * rate;
            env[0] = env[1] = 0; // each side's clash
            mul[0] = mul[1] = decayMul(0.035f, rate);
            env[2] = 0; // each side's sizzle
            env2 = 0;
            mul[2] = decayMul(0.25f * decayScale, rate);
            env2Mul = decayMul(0.27f * decayScale, rate);
            f1.set(2500.0f, 0.7f, rate);
            f2.set(2900.0f, 0.7f, rate);
            f3.set(std::min(12000.0f, rate * 0.4f), 0.6f, rate);
            f4.set(std::min(11000.0f, rate * 0.4f), 0.6f, rate);
            special = Ceng;
            gain = 0.26f;
            send = 0.3f;
            return true;
        }
        default: return false;
        }
    }

    // The djembe's ensemble, placed as it stands when it plays: the lead
    // djembe on the right and the accompanying djembe's tones on the
    // left, the three dunun across the back from the dununba on the left to
    // the kenkeni on the right, each iron bell on its own drum.
    bool startDjembe(const Strike &s, const Harmony &h, float decayScale, float tone, float ratio, float edge,
                     float &pan) {
        const int degrees = s.degrees;
        auto dm = [&](float seconds) { return decayMul(seconds * decayScale, rate); };
        const float b = std::exp2(tone);
        // No hand and no stick strikes twice alike, and a fast pattern
        // shows it: each stroke lands a few cents off (where on the skin,
        // how tight it is under the hand), its upper modes a fifth more or
        // less against the fundamental, its ring a fifth longer or shorter,
        // and its level a little up or down. The skins and the bells take
        // the same touch, drawn afresh for every stroke.
        const float lean = std::exp2(6.0f * rng2.bipolar() / 1200.0f);
        const float balance = 0.8f + 0.4f * rng2.unit();
        const float ring = 0.8f + 0.4f * rng2.unit();
        const float hit = 0.85f + 0.3f * rng2.unit();
        switch (pad) {
        case Kick: {
            // Bass: the palm flat in the middle of the djembe's goatskin,
            // and what answers is the air in its goblet, a Helmholtz
            // resonance near 75 Hz, with a little of the skin's own mode
            // and the slap of the palm.
            const float f = kickHz(h, degrees, 75.0f) * ratio * lean;
            mode(0, f, 1.0f, 0.22f * ring * decayScale);
            mode(1, f * 2.03f, 0.18f * b * balance, 0.07f * ring * decayScale);
            mode(2, f * 4.6f, 0.1f * b * balance, 0.03f * ring * decayScale);
            soft = 0.004f * rate;
            strikeNoise(350.0f, 0.6f, 1.4f, 0.012f);
            gain = 0.62f * hit;
            pan = 0.2f; // the lead's
            send = 0.1f;
            return true;
        }
        case Snare: {
            // Slap: the fingers whipped onto the skin's edge and off, so the
            // high modes ring and the fundamental hardly sounds: "pa". What
            // makes it cut through a whole ensemble is the crack: the skin
            // snapping under the fingertips, a burst of noise between 2.5
            // and 6 kHz gone in a few milliseconds, and the skin's high,
            // inharmonic modes that only the hand's edge drives, a hiss of
            // metal-bright ring above 6 kHz that dies within a few
            // hundredths. The lead's slaps, from the right; each hand a
            // little apart from the other.
            const float f = 360.0f * ratio * lean;
            static const float ratios[7] = {1.0f, 1.62f, 2.25f, 2.9f, 3.55f, 4.4f, 5.3f};
            static const float amps[7] = {0.25f, 0.8f, 1.0f, 0.7f, 0.5f, 0.35f, 0.2f};
            static const float taus[7] = {0.05f, 0.09f, 0.07f, 0.05f, 0.04f, 0.03f, 0.02f};
            for (int q = 0; q < 7; ++q)
                mode(q, f * ratios[q], amps[q] * (q > 2 ? b * balance : 1.0f), taus[q] * ring * decayScale);
            // The edge's modes come in over a fraction of a millisecond each,
            // a little apart, as the hand rolls onto the rim: struck all at
            // once they would line up into a click.
            static const float high[5] = {11.8f, 14.6f, 17.3f, 21.1f, 25.7f};
            for (int q = 0; q < 5; ++q)
                mode(7 + q, f * high[q] * (1.0f + 0.015f * rng2.bipolar()),
                     (0.7f - 0.09f * float(q)) * b * (0.6f + 0.8f * rng2.unit()), (0.03f - 0.004f * float(q)) * ring,
                     0.0003f + 0.0005f * rng2.unit());
            strikeNoise(std::min(8000.0f * std::exp2(0.3f * tone), rate * 0.4f), 0.8f, 2.4f, 0.007f);
            snap = 3.2f;
            snapEnv = 1;
            snapMul = decayMul(0.003f + 0.005f * rng2.unit(), rate);
            f2.set(std::min(3900.0f * std::exp2(0.3f * tone) * (0.9f + 0.2f * rng2.unit()), rate * 0.4f), 0.75f, rate);
            gain = 0.14f * hit;
            pan = 0.62f + 0.1f * rng2.bipolar();
            send = 0.3f;
            return true;
        }
        case Clap: {
            // Tone: the flat fingers on the skin near the edge, lifted at
            // once: the skin's own note, open and round, in the key. The
            // accompanying djembe's, from the left.
            const float f = degreeHz(h, 0, 330.0f) * ratio * lean;
            static const float ratios[5] = {1.0f, 1.52f, 1.98f, 2.44f, 2.9f};
            static const float amps[5] = {1.0f, 0.35f, 0.45f, 0.2f, 0.12f};
            static const float taus[5] = {0.16f, 0.08f, 0.07f, 0.05f, 0.04f};
            for (int q = 0; q < 5; ++q)
                mode(q, f * ratios[q], amps[q] * (q ? b * balance : 1.0f), taus[q] * ring * decayScale);
            strikeNoise(1400.0f, 0.8f, 0.9f, 0.003f);
            snap = 0.9f;
            snapEnv = 1;
            snapMul = decayMul(0.003f, rate);
            f2.set(std::min(4200.0f, rate * 0.4f), 0.8f, rate);
            gain = 0.17f * hit;
            pan = -0.55f + 0.08f * rng2.bipolar();
            send = 0.3f;
            return true;
        }
        case TomLo:
        case TomMid:
        case TomHi: {
            // The dunun: barrel drums with cowhide on both ends, laid on
            // their side and struck with a stick, the dununba lowest, the
            // sangban, the kenkeni highest. Open, the skin rings; struck at
            // the far edge, the stick stays pressed into it, a closed stroke.
            const float near = pad == TomLo ? 78.0f : pad == TomMid ? 125.0f : 185.0f;
            const float f = degreeHz(h, degrees, near) * lean;
            const float m = (edge > 0.3f ? 0.18f : 1.0f) * ring;
            mode(0, f, 1.0f, 0.32f * m * decayScale);
            mode(1, f * 1.59f, 0.4f * b * balance, 0.14f * m * decayScale);
            mode(2, f * 2.14f, 0.3f * b * balance, 0.09f * m * decayScale);
            mode(3, f * 2.65f, 0.15f * b * balance, 0.05f * m * decayScale);
            strikeNoise(2600.0f, 1.0f, 1.6f, 0.0025f);
            gain = (pad == TomLo ? 0.3f : pad == TomMid ? 0.26f : 0.21f) * hit;
            pan = pad == TomLo ? -0.55f : pad == TomMid ? 0.0f : 0.55f;
            send = 0.25f;
            return true;
        }
        case Rim:
        case Ride: {
            // The dunun's iron bells, struck with an iron rod: the
            // kenkeni's high one on the rim pad, the dununba's lower one,
            // the timeline, on the ride, each where its drum is. Iron rings
            // in modes nowhere near harmonic: the lowest as a pair a few
            // hertz apart (the bell is not quite round), then five more up
            // to seven and more times it. Where the rod lands on the bell
            // decides which of them speak, and it never lands twice in the
            // same place; now and then it bounces, a buzzing double strike.
            // Struck at the far edge, a hand mutes it.
            const bool high = pad == Rim;
            const float f = (high ? 1480.0f : 780.0f) * ratio * std::exp2(0.25f * tone) * lean;
            const float m = (edge > 0.3f ? 0.25f : 1.0f) * ring;
            static const float hiRatios[5] = {2.32f, 2.71f, 3.93f, 5.1f, 6.4f};
            static const float loRatios[5] = {2.43f, 3.18f, 4.47f, 5.92f, 7.61f};
            const float *ratios = high ? hiRatios : loRatios;
            const float fundTau = (high ? 0.35f : 0.6f) * m * decayScale;
            mode(0, f, 1.0f, fundTau);
            mode(1, f * 1.006f, 0.5f, fundTau);
            const float contact = 0.1f + 0.8f * rng2.unit();
            for (int q = 0; q < 5; ++q) {
                const float where = 0.25f + 0.75f * std::fabs(std::sin(kPi * contact * (1.0f + 0.9f * float(q))));
                mode(2 + q, f * ratios[q], (high ? 0.6f : 0.55f - 0.06f * float(q)) * where * balance,
                     (0.16f - 0.022f * float(q)) * m * decayScale);
            }
            if (rng2.unit() < 0.2f)
                rattle(3, 0.006f, 0.004f, 0.35f, 0, 1, 2);
            strikeNoise(std::min(7500.0f, rate * 0.4f), 0.9f, high ? 2.4f : 1.8f, 0.0015f);
            // Panned out to its drum, the bell is louder in that ear: the
            // dununba's is trimmed to keep under the bass.
            gain = (high ? 0.1f : 0.068f) * hit;
            pan = high ? 0.65f : -0.65f;
            send = 0.2f;
            return true;
        }
        case Bell: {
            // Krin: a slit log drum, two tongues of one hollowed trunk, the
            // left a note of the scale and the right a third above it.
            const float f = degreeHz(h, 4 + degrees + (s.sx > 0.55f ? 2 : 0), 300.0f);
            mode(0, f, 1.0f, 0.22f * decayScale);
            mode(1, f * 2.95f, 0.3f * b, 0.06f * decayScale);
            mode(2, f * 5.2f, 0.08f * b, 0.02f * decayScale);
            soft = 0.0015f * rate;
            strikeNoise(1800.0f, 0.8f, 0.7f, 0.003f);
            gain = 0.24f;
            pan = -0.3f + 0.3f * (s.sx - 0.5f);
            send = 0.3f;
            return true;
        }
        case Zap: {
            // Kora: a string plucked with a fingernail, its harmonics shaped
            // by where it was plucked (the comb of a pluck near the bridge),
            // the higher fading sooner and a hair sharp, as a real string's.
            const float f = degreeHz(h, degrees, 440.0f);
            for (int n = 1; n <= 9; ++n) {
                const float a = std::fabs(std::sin(float(n) * kPi * 0.12f)) / std::pow(float(n), 0.9f);
                mode(n - 1, f * float(n) * (1.0f + 0.0003f * float(n * n)), a * (n > 2 ? b : 1.0f),
                     1.1f * decayScale / (1.0f + 0.35f * float(n - 1)));
            }
            strikeNoise(4500.0f, 1.0f, 0.4f, 0.002f);
            gain = 0.225f;
            pan = 0.2f + 0.4f * (s.sx - 0.5f);
            send = 0.35f;
            return true;
        }
        case Open: {
            // Karinyan: a notched iron tube scraped with a rod, so a train
            // of clicks, each ringing the tube's high modes, quickening and
            // swelling then fading over the stroke (longer struck at the far
            // edge).
            static const float ratios[6] = {1.0f, 1.43f, 1.96f, 2.51f, 3.17f, 3.9f};
            for (int q = 0; q < 6; ++q)
                mode(q, 2700.0f * ratios[q] * ratio * std::exp2(0.3f * tone), 1.0f / (1.0f + 0.3f * float(q)),
                     0.025f * decayScale);
            rattle(edge > 0.3f ? 18 : 12, 0.009f, 0.002f, 1.0f, 0, 1);
            rattleAccel = 0.95f;
            rattleArch = true;
            strikeNoise(5000.0f, 1.0f, 0.6f, 0.002f);
            gain = 0.085f;
            pan = 0.35f + 0.25f * (s.sx - 0.5f);
            send = 0.2f;
            return true;
        }
        case Crash: {
            // Sesse: the tin plate with wire rings round its rim that the
            // lead player hangs on the djembe: struck, the rings buzz and
            // chatter against the tin for most of a second.
            for (int r = 0; r < 4; ++r) {
                const float f = (2400.0f + 4100.0f * rng.unit()) * ratio * std::exp2(0.3f * tone);
                mode(3 * r, f, 1.0f, 0.08f * decayScale);
                mode(3 * r + 1, f * 1.57f, 0.6f, 0.06f * decayScale);
                mode(3 * r + 2, f * 2.21f, 0.35f, 0.04f * decayScale);
            }
            rattle(edge > 0.3f ? 60 : 40, 0.004f, 0.005f, 0.94f, 4, 3);
            rattleRandom = true;
            strikeNoise(7500.0f, 0.8f, 0.5f, 0.004f);
            gain = 0.07f;
            pan = -0.25f;
            send = 0.2f;
            return true;
        }
        case Perc: {
            // The talking drum: an hourglass drum whose skins are tied
            // together by cords, squeezed under the arm as it is struck with
            // a hooked stick, so its note rises as it speaks and falls back
            // below where it began (struck further up the pad, further).
            special = Talking;
            hz[0] = degreeHz(h, 4 + degrees, 260.0f);
            k[0] = 3.0f + 4.0f * std::max(0.0f, edge); // the squeeze, semitones
            k[1] = decayMul(0.025f, rate);             // squeezing
            k[2] = decayMul(0.12f, rate);              // letting go
            k[3] = 0.07f * rate;                       // when it lets go
            k[4] = b;
            aux[0] = 1;  // the squeeze still to come
            aux[1] = 1;  // the release still to come
            env[0] = 1;
            mul[0] = dm(0.28f);
            env[1] = 1;
            mul[1] = dm(0.12f);
            env[2] = 1; // the stick
            mul[2] = decayMul(0.003f, rate);
            f1.set(2200.0f, 1.0f, rate);
            gain = 0.28f;
            pan = -0.35f + 0.3f * (s.sx - 0.5f);
            send = 0.3f;
            return true;
        }
        case Hat:
        case Shaker: {
            // Kese, a little basket of seeds shaken tight; shekere, a gourd
            // in a net of beads, swept, with the thump of the gourd itself.
            const bool kese = pad == Hat;
            special = Shakers;
            k[0] = (kese ? 0.0015f : 0.006f + 0.008f * std::max(0.0f, edge)) * rate;
            k[1] = (kese ? 0.004f : 0.012f) * rate;
            k[2] = kese ? 0.08f : 0.15f;
            k[3] = kese ? 0.0f : 0.6f;
            k[5] = 0; // both in one place, not spread like amapiano's
            env[0] = 1;
            mul[0] = dm((kese ? 0.02f : 0.06f) * (1.0f + 0.3f * tone) * (1.0f + 0.6f * std::max(0.0f, edge)));
            env[1] = 1;
            mul[1] = dm((kese ? 0.03f : 0.08f) * (1.0f + 0.3f * tone));
            env[2] = 1;
            mul[2] = decayMul(0.04f, rate);
            f1.set(std::min((kese ? 6500.0f : 5000.0f) * ratio * std::exp2(0.3f * tone), rate * 0.42f), kese ? 1.0f : 0.8f,
                   rate);
            f2.set(std::min((kese ? 4000.0f : 3500.0f) * ratio, rate * 0.42f), 1.2f, rate);
            f3.set(260.0f * ratio, 1.2f, rate);
            f4.set(kese ? 2500.0f : 1800.0f, 0.7f, rate);
            f5.set(std::min(13000.0f, rate * 0.4f), 0.6f, rate);
            // Under the bells and the slaps, which carry the timeline: the
            // rattles keep the pulse but never cover them.
            gain = kese ? 0.06f : 0.055f;
            pan = kese ? 0.7f + 0.2f * (s.sx - 0.5f) : -0.7f + 0.2f * (s.sx - 0.5f);
            send = 0.15f;
            return true;
        }
        default: return false;
        }
    }

    // The chiptune kit: every drum is the Game Boy's chip. pan is the
    // voice's; the chip's channels are only left, right or both, so the
    // drums sit in three places, panned as hard as the chip pans them.
    bool startChip(const Strike &s, const Harmony &h, float decayScale, float tone, float ratio, float edge, float &pan) {
        const int degrees = s.degrees;
        auto dm = [&](float seconds) { return decayMul(seconds * decayScale, rate); };
        chip.reset(rate);
        chipOn = true;
        f1.set(30.0f, 0.7f, rate);                            // the chip's output capacitor
        f2.set(std::min(11000.0f, rate * 0.4f), 0.6f, rate); // and the speaker's top
        // The noise channel at a divisor and shift (tone moves the shift a
        // step either way), fading over tau.
        auto noise = [&](int divisor, int shift, float tau, bool metallic = false) {
            chip.noiseVol = 1;
            chip.noiseMul = dm(tau);
            chip.shortNoise = metallic;
            chip.setNoise(divisor, shift - int(std::lround(tone)));
            chip.shiftTo = chip.shift;
        };
        auto tone_ = [&](ChipVoice::Tone t, float from, float to, float sweepTau, float tau, float duty = 0.5f) {
            chip.tone = t;
            chip.hz = from;
            chip.hzTo = to;
            chip.sweep = 1;
            chip.sweepMul = decayMul(sweepTau, rate);
            chip.toneVol = 1;
            chip.toneMul = dm(tau);
            chip.duty = duty;
            chip.duties[0] = duty;
        };
        // A duty sequence, a step every few frames, looping back to `loop`
        // past its end (or holding its last step).
        auto duties = [&](std::initializer_list<float> d, int frames, int loop = -1) {
            int n = 0;
            for (float x : d)
                if (n < 4)
                    chip.duties[n++] = x;
            chip.dutyCount = n;
            chip.dutyFrames = std::max(1, frames);
            chip.dutyLoop = loop >= 0 ? loop : n - 1;
            chip.duty = chip.duties[0];
        };
        constexpr float hard = 0.85f;
        pan = 0;
        send = 0.08f;
        switch (pad) {
        case Kick: {
            // The wave channel's triangle dropping from five times its note
            // in a few hundredths of a second, and a tick of noise on top.
            const float f = kickHz(h, degrees, 55.0f);
            tone_(ChipVoice::Triangle, f * 5.0f * std::exp2(0.5f * tone), f, 0.018f, 0.13f);
            noise(0, 1, 0.006f);
            chip.noiseVol = 0.5f;
            gain = 0.5f;
            break;
        }
        case Snare:
            // Noise over a square falling an octave and more, its duty
            // narrowing frame by frame; the noise's clock steps down as it
            // fades, the tracker snare's crunch.
            noise(1, 1, 0.09f * (1.0f + 0.3f * edge));
            chip.shiftTo = chip.shift + 4;
            chip.shiftEvery = 0.012f * rate;
            tone_(ChipVoice::Pulse, 420.0f * ratio, 160.0f * ratio, 0.03f, 0.05f);
            duties({0.5f, 0.25f, 0.125f}, 1);
            chip.toneVol = 0.6f;
            gain = 0.2f;
            break;
        case Clap:
            // Two bursts of noise (the second on the first's fade, at a
            // lower clock so it grinds), as a tracker writes a clap.
            noise(1, 2, 0.07f);
            k[0] = 0.012f * rate;
            gain = 0.2f;
            pan = -hard;
            break;
        case Hat:
            // The tracker's hat: the noise channel's seven-bit mode at its
            // fastest clocks, a short metallic tick whose register repeats
            // every 127 bits, so it rings at a pitch with its harmonics
            // over it rather than hissing; struck at the far edge, the long
            // mode's plain hiss.
            if (edge > 0.3f) {
                noise(0, 0, 0.025f * (0.8f + 0.4f * s.vel) * (1.0f + 0.9f * edge));
            } else {
                noise(0, 1, 0.03f * (0.8f + 0.4f * s.vel), true);
                chip.noiseVol = 0.85f;
            }
            f1.set(6000.0f, 0.7f, rate);
            gain = 0.11f;
            pan = hard;
            break;
        case Open:
            noise(0, 1, 0.2f * (1.0f + 0.4f * std::max(0.0f, edge)));
            f1.set(5000.0f, 0.7f, rate);
            gain = 0.08f;
            pan = hard;
            break;
        case Shaker:
            noise(0, 2, 0.04f);
            chip.noiseVol = 0.4f;
            k[1] = 0.01f * rate; // its swell, the volume register stepping up
            f1.set(4000.0f, 0.7f, rate);
            gain = 0.12f;
            pan = hard;
            break;
        case Rim:
            tone_(ChipVoice::Pulse, 1600.0f * ratio, 1600.0f * ratio, 1.0f, 0.015f, 0.125f);
            gain = 0.13f;
            pan = -hard;
            break;
        case Ride:
            // The noise channel's short mode: its register cycles every 127
            // steps, so the hiss turns into a metallic buzz.
            noise(1, 3, 0.3f * (1.0f + 0.5f * std::max(0.0f, edge)), true);
            f1.set(2500.0f, 0.7f, rate);
            gain = 0.07f;
            pan = hard;
            break;
        case Crash:
            // Hiss whose clock steps down the chip's table as it fades,
            // darkening into the low settings' grinding steps.
            noise(0, 1, 0.6f);
            chip.shiftTo = 9;
            chip.shiftEvery = 0.075f * decayScale * rate;
            f1.set(200.0f, 0.7f, rate);
            gain = 0.1f;
            break;
        case TomLo:
        case TomMid:
        case TomHi: {
            const int deg = pad == TomLo ? 0 : pad == TomMid ? 2 : 4;
            float f = degreeHz(h, deg + degrees, 125.0f);
            if (pad != TomLo && f < degreeHz(h, degrees, 125.0f))
                f *= 2.0f;
            tone_(ChipVoice::Triangle, f * 2.2f, f, 0.04f, 0.15f);
            gain = 0.3f;
            pan = pad == TomLo ? -hard : pad == TomMid ? 0.0f : hard;
            break;
        }
        case Perc: {
            // A blip: a thin pulse a frame up an octave, then on the note,
            // its duty opening a step a frame.
            const float f = degreeHz(h, 4 + degrees, 520.0f);
            tone_(ChipVoice::Pulse, f * 2.0f, f, 0.004f, 0.07f * (1.0f + 0.3f * tone), 0.125f);
            duties({0.125f, 0.25f, 0.5f}, 1);
            chip.tickInc = 128.0f / rate;
            gain = 0.11f;
            pan = -hard;
            break;
        }
        case Bell: {
            // The arpeggio: root, third and fifth of the scale a frame
            // each, the chord as one voice of a four-voice chip plays it,
            // its duty turning every other frame.
            const float f = degreeHz(h, 4 + degrees, 660.0f);
            tone_(ChipVoice::Pulse, f, f, 1.0f, 0.35f, 0.5f);
            for (int i = 0; i < 3; ++i)
                chip.arp[i] = degreeHz(h, 4 + degrees + 2 * i, 660.0f) / f;
            for (int i = 1; i < 3; ++i)
                if (chip.arp[i] < chip.arp[i - 1])
                    chip.arp[i] *= 2.0f;
            if (edge > 0.3f)
                duties({0.125f}, 1);
            else
                duties({0.5f, 0.25f, 0.125f, 0.25f}, 2, 0);
            gain = 0.08f;
            pan = hard;
            break;
        }
        case Zap: {
            // The laser: a pulse swept down by the chip's sweep unit, in
            // its steps, landing on the note, its duty cycling a frame a
            // step; struck at the far edge it wobbles in the frame-rate
            // vibrato as it lands.
            const float f = degreeHz(h, degrees, 330.0f);
            tone_(ChipVoice::Pulse, f * (4.0f + 3.0f * std::max(0.0f, edge)), f, 0.05f, 0.16f * (1.0f + 0.3f * tone),
                  0.125f);
            duties({0.125f, 0.25f, 0.5f, 0.25f}, 1, 0);
            if (edge > 0.25f) {
                chip.vibDepth = 0.03f;
                chip.vibFrames = 6;
                chip.vibDelay = 6;
            }
            chip.tickInc = 128.0f / rate;
            f2.set(std::min(8000.0f, rate * 0.4f), 0.6f, rate);
            gain = 0.1f;
            pan = -hard;
            break;
        }
        default: chipOn = false; return false;
        }
        return true;
    }

    // One sample of the world kits' instruments; the wide ones put their
    // right ear in sr and say so in stereo.
    inline float renderNew(float &sr, bool &stereo, float inv) {
        if (modes) {
            if (rattleLeft > 0 && t >= rattleNext)
                rattleStrike();
            float v = 0;
            for (int q = 0; q < modes; ++q)
                v += partial[q].step();
            if (shakeInc > 0) {
                // Each shake knocks the tubes again: they ring afresh (a
                // little less or more each time, as a hand shakes), and
                // the strike's noise and the tone's swell start over.
                shakePh += shakeInc;
                if (shakePh >= 1.0f) {
                    shakePh -= 1.0f;
                    if (t < shakeLen) {
                        shakeEnv = 1;
                        env[3] = 1;
                        const float knock = 0.7f + 0.3f * rng.unit();
                        for (int q = 0; q < modes; ++q)
                            partial[q].env = std::max(partial[q].env, knock);
                    }
                }
                shakeEnv *= shakeMul;
                v *= 0.15f + 0.85f * shakeEnv;
            }
            env[3] *= mul[3];
            v += f1.step(rng.bipolar()).band * env[3] * strike;
            if (snap > 0) {
                snapEnv *= snapMul;
                v += f2.step(rng.bipolar()).band * snapEnv * snap;
            }
            if (soft > 0 && t < soft)
                v *= t / soft;
            return v;
        }
        if (chipOn) {
            if (pad == Clap && k[0] > 0 && t >= k[0]) {
                // The second burst, three steps down the clock: it grinds.
                chip.noiseVol = 0.85f;
                chip.setNoise(chip.divisor, chip.shift + 3);
                chip.shiftTo = chip.shift;
                k[0] = 0;
            }
            if (pad == Shaker && t < k[1])
                chip.noiseVol = 0.4f + 0.6f * t / k[1];
            return f2.step(f1.step(chip.step()).high).low;
        }
        switch (special) {
        case Jawari: {
            env[0] *= mul[0];
            env[1] *= mul[1];
            const float a = std::min(1.0f, t / k[0]);
            // The meend: the pitch bends up to the next note, eased.
            float f = hz[0];
            if (k[5] != 1.0f && t > k[6]) {
                const float u = std::min(1.0f, (t - k[6]) / k[7]);
                f *= 1.0f + (k[5] - 1.0f) * u * u * (3.0f - 2.0f * u);
            }
            const float inc = f * inv;
            ph[0] += inc;
            ph[0] -= std::floor(ph[0]);
            const float saw = 2.0f * ph[0] - 1.0f - polyBlep(ph[0], inc);
            if ((int(t) & 15) == 0) {
                // The band: from high after the pluck down to its lowest
                // harmonic.
                const float c = f * (k[1] + k[2] * env[1]) * k[4];
                f3.set(std::min(c, rate * 0.4f), 3.0f, rate);
            }
            const float buzz = f3.step(saw).band * 1.3f;
            const float body = 0.45f * std::sin(kTwoPi * ph[0]);
            const float sym = partial[0].step() + partial[1].step() + partial[2].step();
            return f2.step(body + buzz).low * a * env[0] + sym;
        }
        case Tanpura: {
            // The next string of the cycle, while the cycle goes on.
            if (t >= k[0] && k[0] < aux[4]) {
                const int i = int(k[2]) & 3;
                aux[i] = 0;
                k[2] += 1.0f;
                k[0] += k[1];
            }
            // Every few samples, each string's band moves: down from about
            // its twenty-fourth harmonic toward its fifth over the seconds
            // after its pluck, and wavering as it goes, each string out of
            // step with the others.
            const bool refresh = (int(t) & 31) == 0;
            Svf *band[4] = {&f3, &f4, &f5, &f6};
            float out = 0;
            for (int i = 0; i < 4; ++i) {
                if (aux[i] < 0)
                    continue;
                if (aux[i] < k[4])
                    env[i] += (1.0f - env[i]) * k[5];
                else
                    env[i] *= mul[i];
                aux[i] += 1.0f;
                const float inc = hz[i] * inv;
                ph[i] += inc;
                ph[i] -= std::floor(ph[i]);
                const float saw = 2.0f * ph[i] - 1.0f - polyBlep(ph[i], inc);
                if (refresh) {
                    const float since = aux[i] * inv;
                    const float harm = 5.0f + 19.0f * std::exp(-since / 2.0f) +
                                       3.0f * (0.5f - 0.5f * std::cos(kTwoPi * (0.23f * since + 0.25f * float(i))));
                    band[i]->set(std::min(hz[i] * harm * k[3], rate * 0.4f), 6.0f, rate);
                }
                out += (0.35f * std::sin(kTwoPi * ph[i]) + 0.4f * band[i]->step(saw).band + 0.05f * saw) * env[i];
            }
            return f7.step(f2.step(out).low).high;
        }
        case Ge: {
            // The heel presses: the pitch rises by the gumki, eased in.
            k[3] *= k[1];
            const float f = hz[0] * std::exp2(k[0] * (1.0f - k[3]) * (1.0f / 12.0f));
            ph[0] += f * inv;
            ph[1] += f * 1.98f * inv;
            ph[2] += f * 2.95f * inv;
            for (int i = 0; i < 3; ++i)
                ph[i] -= std::floor(ph[i]);
            env[0] *= mul[0];
            env[1] *= mul[1];
            env[2] *= mul[2];
            const float body = std::sin(kTwoPi * ph[0]) * env[0] +
                               (0.25f * std::sin(kTwoPi * ph[1]) + 0.12f * std::sin(kTwoPi * ph[2])) * env[1] * k[2];
            const float thump = f1.step(rng.bipolar()).band * env[2] * 1.5f;
            return fastTanh(1.3f * body) * 1.15f + thump;
        }
        case Slap: {
            env[0] *= mul[0];
            env[1] *= mul[1];
            const float a = std::min(1.0f, t / (0.0004f * rate));
            const float n0 = rng.bipolar();
            return f3.step(f1.step(n0).band * env[0] * 1.8f + f2.step(n0).band * env[1] * 2.4f).low * a;
        }
        case Ceng: {
            // The clashes, left and right in turn, each setting off its
            // side's sizzle.
            for (int b = 0; b < 4; ++b)
                if (t >= k[b] && t < k[b] + 1.0f) {
                    if (b & 1) {
                        env[1] = 1.0f - 0.15f * float(b);
                        env2 = std::max(env2, 0.35f);
                    } else {
                        env[0] = 1.0f - 0.15f * float(b);
                        env[2] = std::max(env[2], 0.35f);
                    }
                }
            env[0] *= mul[0];
            env[1] *= mul[1];
            env[2] *= mul[2];
            env2 *= env2Mul;
            float ml = 0, mr = 0;
            for (int q = 0; q < 6; ++q) {
                ml += sq[q].step(sqInc[q]);
                mr += sq[q + 6].step(sqInc[q + 6]);
            }
            const float a = std::min(1.0f, t / (0.0008f * rate));
            const float nl = rng.bipolar(), nr = rng2.bipolar();
            sr = f4.step(f2.step(mr * (0.8f / 6.0f) + nr * 0.35f).high).low * (env[1] + env2) * a;
            stereo = true;
            return f3.step(f1.step(ml * (0.8f / 6.0f) + nl * 0.35f).high).low * (env[0] + env[2]) * a;
        }
        case Vox: {
            const float a = std::min(1.0f, t / k[0]);
            env[0] *= t < k[1] ? mul[0] : mul[1];
            env[2] *= mul[2];
            // The voice falls a little onto its note as it speaks.
            const float f = hz[0] * (1.0f + 0.06f * env[2]);
            const float inc = f * inv;
            ph[0] += inc;
            ph[0] -= std::floor(ph[0]);
            const float src = 2.0f * ph[0] - 1.0f - polyBlep(ph[0], inc) + 0.15f * rng.bipolar();
            if ((int(t) & 15) == 0) {
                // The formants, moving from the first vowel to the second.
                const float m = env[2];
                f3.set((aux[3] + (aux[0] - aux[3]) * m) * aux[6], 6.0f, rate);
                f4.set((aux[4] + (aux[1] - aux[4]) * m) * aux[6], 10.0f, rate);
                f5.set(std::min((aux[5] + (aux[2] - aux[5]) * m) * aux[6], rate * 0.4f), 12.0f, rate);
            }
            const float v = f3.step(src).band * (1.0f / 6.0f) + f4.step(src).band * (0.6f / 10.0f) +
                            f5.step(src).band * (0.35f / 12.0f);
            return f2.step(v).low * a * env[0] * 2.5f;
        }
        case Keys: {
            const float a = std::min(1.0f, t / k[0]);
            env[0] *= t < k[1] ? mul[0] : mul[1];
            env[2] *= mul[2];
            const float index = k[2] * (0.2f + 0.8f * env[2]);
            const int n = int(k[4]);
            float vl = 0, vr = 0;
            for (int i = 0; i < n; ++i) {
                ph[i] += hz[i] * inv;
                ph[i] -= std::floor(ph[i]);
                const float w = kTwoPi * ph[i];
                const float v = std::sin(w + index * std::sin(w));
                const float p = n > 1 ? float(i) / float(n - 1) : 0.5f;
                vl += v * (1.0f - 0.8f * p);
                vr += v * (0.2f + 0.8f * p);
            }
            const float e = a * env[0] / std::sqrt(float(n));
            sr = f3.step(vr * e).low;
            stereo = true;
            return f2.step(vl * e).low;
        }
        case Shakers: {
            // Two shakers, the second a moment behind with its beads heard
            // one by one, and for the shekere the gourd's thump.
            const float s0 = t < k[0] ? (t / k[0]) * (t / k[0]) : 1.0f;
            if (t >= k[0])
                env[0] *= mul[0];
            float e1 = 0;
            if (t >= k[1]) {
                const float since = t - k[1];
                if (since >= k[0])
                    env[1] *= mul[1];
                e1 = (since < k[0] ? since / k[0] : 1.0f) * env[1];
            }
            const float n0 = rng.bipolar();
            const float bead = rng2.unit() < k[2] ? rng2.bipolar() * 2.5f : 0.0f;
            const float tight = f1.step(n0).band * s0 * env[0], grainy = f2.step(bead).band * e1 * 1.2f;
            if (k[5] > 0) {
                // Amapiano's two shakers are two hands: the tight one in
                // the left ear and the grainy one, behind it, in the right,
                // each with a little of the other, so the stack is wide.
                const float bleed = 1.0f - k[5]; // how much of each shaker the other ear hears
                sr = f7.step(f6.step(grainy + bleed * tight).high).low * 2.4f;
                stereo = true;
                return f5.step(f4.step(tight + bleed * grainy).high).low * 2.4f;
            }
            float v = f5.step(f4.step(tight + grainy).high).low * 2.0f;
            if (k[3] > 0) {
                env[2] *= mul[2];
                v += f3.step(rng.bipolar()).band * env[2] * k[3] * 2.0f;
            }
            return v;
        }
        case Talking: {
            // Squeezed, the note rises; let go, it falls back past where it
            // began.
            aux[0] *= k[1];
            if (t >= k[3])
                aux[1] *= k[2];
            const float semis = k[0] * (1.0f - aux[0]) - (k[0] + 2.0f) * (1.0f - aux[1]);
            const float f = hz[0] * std::exp2(semis * (1.0f / 12.0f));
            ph[0] += f * inv;
            ph[1] += f * 1.58f * inv;
            ph[0] -= std::floor(ph[0]);
            ph[1] -= std::floor(ph[1]);
            env[0] *= mul[0];
            env[1] *= mul[1];
            env[2] *= mul[2];
            const float body = std::sin(kTwoPi * ph[0]) * env[0] + 0.3f * k[4] * std::sin(kTwoPi * ph[1]) * env[1];
            return fastTanh(1.2f * body) + f1.step(rng.bipolar()).band * env[2] * 1.6f;
        }
        default: return 0.0f;
        }
    }

    // Fades the voice out over a few milliseconds: a choke, or a steal.
    void release(float seconds) {
        fadeMul = std::min(fadeMul, decayMul(seconds, rate));
    }

    void start(const Strike &s, const Setup &su, float rate_, int delaySamples, uint32_t seed) {
        rate = rate_;
        active = true;
        pad = s.pad;
        note = s.degrees;
        kit = std::clamp(s.kit >= 0 ? s.kit : su.kit, 0, KitCount - 1);
        delay = delaySamples;
        t = 0;
        fade = 1;
        fadeMul = 1;
        level = 0;
        rng = Rng(seed * 2654435761u + 1u);
        rng2 = Rng(seed * 40503u + 7u);
        for (float &p : ph)
            p = 0;
        for (int i = 0; i < 4; ++i)
            env[i] = 0, mul[i] = 1;
        f1.reset();
        f2.reset();
        f3.reset();
        f4.reset();
        f5.reset();
        f6.reset();
        f7.reset();
        dusty = kit == BoomBap;
        if (dusty) {
            dustL.reset();
            dustR.reset();
            dustL.set(8500.0f, 0.6f, rate_);
            dustR.set(8500.0f, 0.6f, rate_);
            holdPhase = 1.0f;
            holdInc = 26040.0f / rate_;
            heldL = heldR = 0;
        }
        const Harmony &h = *su.harmony;
        const float vel = clampf(s.vel, 0.02f, 1.0f);
        // Louder hits are louder by more than their velocity, the way a
        // drum responds, but a ghost note still speaks: about 24 dB from
        // the lightest touch to the hardest.
        const float dyn = 0.05f + 0.95f * std::pow(vel, 1.7f);
        const float decayScale = std::exp2(clampf(s.decay, -3.0f, 3.0f));
        const float tone = clampf(s.tone, -1.0f, 1.0f);
        const float ratio = std::exp2(s.semis / 12.0f);
        const int degrees = s.degrees;
        const float edge = clampf((s.sy - 0.5f) * 2.0f, -1.0f, 1.0f); // −1 near edge … +1 far edge
        float pan = 0;
        send = 0;
        auto dm = [&](float seconds) { return decayMul(seconds * decayScale, rate); };
        modes = 0;
        strike = 0;
        snap = 0;
        soft = 0;
        shakeInc = 0;
        rattleLeft = 0;
        sustains = false;
        special = NoSpecial;
        chipOn = false;

        if (startNew(s, h, decayScale, tone, ratio, edge, pan)) {
            place(dyn, pan);
            return;
        }
        switch (pad) {
        case Kick: {
            // k: 0 pitch amount, 1 fast pitch amount, 2 drive, 3 click, 4 drive norm
            // The CR-78's and the felt jazz kick barely sweep: a round
            // boom, no punch; boom bap's knocks through its drive. The
            // CR-78's sits higher, a little driven and with a soft knock at
            // a kilohertz, so a laptop's speakers still hear it.
            static const float amt[KitCount] = {5.5f, 7.0f, 4.0f, 4.5f, 5.0f, 3.5f, 6.0f, 4.0f, 1.2f, 1.6f, 4.5f, -0.22f, 0.6f, 6.0f, 5.0f};
            static const float fast[KitCount] = {8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 10.0f, 8.0f, 1.5f, 2.0f, 4.0f, 0.8f, 3.0f, 8.0f, 9.0f};
            static const float tauP[KitCount] = {0.030f, 0.034f, 0.028f, 0.022f, 0.025f,
                                                 0.028f, 0.018f, 0.040f, 0.020f, 0.030f, 0.035f, 0.09f, 0.05f, 0.02f, 0.03f};
            static const float hold[KitCount] = {0.025f, 0.04f, 0.02f, 0.01f, 0.015f, 0.012f, 0.008f, 0.03f, 0.0f, 0.02f, 0.03f, 0.0f, 0.0f, 0.0f, 0.05f};
            static const float tauA[KitCount] = {0.15f, 0.21f, 0.16f, 0.09f, 0.11f, 0.12f, 0.085f, 0.19f, 0.09f, 0.2f, 0.16f, 0.3f, 0.22f, 0.12f, 0.24f};
            static const float drive[KitCount] = {1.8f, 3.6f, 1.5f, 2.4f, 2.0f, 3.2f, 2.6f, 2.2f, 1.5f, 1.2f, 1.3f, 1.4f, 1.3f, 1.0f, 4.5f};
            static const float click[KitCount] = {0.45f, 0.4f, 0.5f, 0.7f, 0.5f, 0.85f, 0.8f, 0.6f, 0.15f, 0.25f, 0.2f, 0.35f, 0.5f, 0.3f, 0.9f};
            static const float clickHz[KitCount] = {3500.0f, 2600.0f, 4200.0f, 2200.0f, 3200.0f,
                                                    1500.0f, 4000.0f, 2600.0f, 1000.0f, 700.0f, 1200.0f, 500.0f, 900.0f, 3000.0f, 3800.0f};
            static const float nearHz[KitCount] = {52.0f, 52.0f, 47.0f, 52.0f, 52.0f, 50.0f, 55.0f, 54.0f, 68.0f, 56.0f, 50.0f, 82.0f, 72.0f, 55.0f, 50.0f};
            hz[0] = kickHz(h, degrees, nearHz[kit]);
            k[0] = amt[kit];
            k[1] = fast[kit];
            mul[0] = decayMul(tauP[kit], rate);
            mul[1] = decayMul(0.0035f, rate);
            env[0] = env[1] = 1;
            holdSamples = hold[kit] * decayScale * rate;
            env[2] = 1;
            mul[2] = dm(tauA[kit]);
            k[2] = drive[kit] * std::exp2(tone * 1.2f) * (0.8f + 0.3f * vel);
            k[4] = 1.0f / fastTanh(k[2]);
            k[3] = click[kit] * (1.0f + 0.6f * tone) * (1.0f + 0.4f * std::max(0.0f, edge)) * (0.5f + 0.6f * vel);
            env[3] = 1;
            mul[3] = decayMul(0.0025f, rate);
            f1.set(clickHz[kit], 0.8f, rate);
            // Amapiano gives the bottom octave to the log drum: its kick is
            // high-passed above the log drum's root, a short punch of the
            // sweep and the beater with no boom of its own under the riff.
            if (kit == Amapiano) {
                f2.set(115.0f, 0.6f, rate);
                f3.set(115.0f, 0.6f, rate);
            }
            // Every kit's kick peaks alike, whatever its drive and sweep.
            static const float level[KitCount] = {1.0f, 1.15f, 0.92f, 1.08f, 1.0f, 1.0f, 0.75f, 1.05f, 1.2f, 1.25f, 1.6f, 1.1f, 1.15f, 1.0f, 1.0f};
            gain = 0.48f * level[kit];
            send = 0.02f;
            break;
        }
        case Sub:
            // The 808 has a voice of its own (Sub808): it glides between notes.
            active = false;
            return;
        case Snare: {
            // k: 0 body level, 1 noise level, 2 click level, 3 the noise's
            // rise in samples, 4 the gated room's level, 5 when its gate
            // shuts (samples), 6 its swell per sample, 7 the gate's fall.
            // Jungle's is the ringing break snare, tuned high; the CR-78's
            // a thin tick of noise; the brush has hardly any body, and its
            // bristles spread the hit over a few milliseconds. The
            // kendhang's tak is the small head slapped: a high crack.
            static const float base[KitCount] = {190.0f, 200.0f, 225.0f, 172.0f, 212.0f,
                                                 185.0f, 245.0f, 200.0f, 270.0f, 180.0f, 330.0f, 200.0f, 420.0f, 200.0f, 210.0f};
            static const float tauB[KitCount] = {0.05f, 0.04f, 0.035f, 0.07f, 0.045f, 0.06f, 0.035f, 0.05f, 0.03f, 0.04f, 0.025f, 0.04f, 0.03f, 0.04f, 0.05f};
            static const float tauN[KitCount] = {0.085f, 0.06f, 0.07f, 0.13f, 0.07f, 0.1f, 0.075f, 0.04f, 0.06f, 0.12f, 0.05f, 0.06f, 0.02f, 0.06f, 0.11f};
            static const float hp[KitCount] = {1300.0f, 2200.0f, 2800.0f, 900.0f, 1900.0f,
                                               900.0f, 1800.0f, 1200.0f, 2500.0f, 1500.0f, 2500.0f, 1500.0f, 2600.0f, 1500.0f, 1500.0f};
            static const float bp[KitCount] = {4800.0f, 5200.0f, 6500.0f, 3600.0f, 5600.0f,
                                               3000.0f, 6000.0f, 4500.0f, 7000.0f, 4200.0f, 6800.0f, 4200.0f, 6500.0f, 4200.0f, 5000.0f};
            static const float noise[KitCount] = {0.85f, 0.75f, 0.95f, 0.8f, 0.9f, 0.85f, 0.9f, 0.8f, 0.9f, 1.0f, 0.8f, 0.9f, 0.5f, 0.9f, 0.95f};
            static const float body[KitCount] = {0.65f, 0.55f, 0.45f, 0.85f, 0.5f, 0.8f, 0.75f, 0.6f, 0.35f, 0.15f, 0.35f, 0.5f, 0.9f, 0.5f, 0.6f};
            static const float crack[KitCount] = {0.2f, 0.2f, 0.35f, 0.2f, 0.2f, 0.25f, 0.35f, 0.2f, 0.15f, 0.04f, 0.4f, 0.2f, 0.5f, 0.2f, 0.3f};
            static const float rise[KitCount] = {0.0004f, 0.0004f, 0.0004f, 0.0004f, 0.0004f,
                                                 0.0004f, 0.0004f, 0.0004f, 0.0004f, 0.004f, 0.0004f, 0.0004f, 0.0004f, 0.0004f, 0.0004f};
            static const float sends[KitCount] = {0.3f, 0.3f, 0.3f, 0.5f, 0.3f, 0.25f, 0.45f, 0.0f, 0.2f, 0.35f, 0.35f, 0.3f, 0.3f, 0.0f, 0.25f};
            // Jungle's ringing body is tuned to the key's root, so a break
            // played busy rings in key; its ghosts are struck nearer the
            // rim and ring on the scale's second, a step above.
            const float tuned = kit == Jungle ? degreeHz(h, vel > 0.5f ? 0 : 1, base[kit])
                                              : base[kit];
            hz[0] = tuned * ratio * (1.0f + 0.04f * (s.sx - 0.5f)) * (1.0f + 0.06f * std::max(0.0f, edge));
            hz[1] = hz[0] * 1.62f;
            env[0] = 1; // body
            mul[0] = dm(tauB[kit]);
            env[1] = 1; // pitch drop
            mul[1] = decayMul(0.012f, rate);
            env[2] = 1; // noise
            mul[2] = dm(tauN[kit] * (0.85f + 0.3f * vel));
            env[3] = 1; // the crack
            mul[3] = decayMul(0.0012f, rate);
            k[0] = body[kit] * std::exp2(-tone * 0.6f) * (1.0f - 0.35f * std::max(0.0f, edge));
            if (kit == Jungle) // a ghost is mostly the wires, so a busy break doesn't drone
                k[0] *= vel * vel;
            k[1] = noise[kit] * std::exp2(tone * 0.7f) * (1.0f + 0.25f * std::max(0.0f, edge));
            k[2] = crack[kit] * (0.6f + 0.6f * vel);
            k[3] = rise[kit] * rate;
            f1.set(hp[kit] * ratio * (1.0f + 0.5f * std::max(0.0f, edge)), 0.7f, rate);
            f2.set(std::min(bp[kit] * ratio, rate * 0.4f), 1.1f, rate);
            f3.set(kit == Brushes ? 8000.0f : 11000.0f, 0.7f, rate);
            if (kit == Linn) {
                // The 80s: the snare's big room swells, holds nearly flat,
                // and is cut off by a noise gate a fifth of a second on, in
                // a few milliseconds. A pair of noises, one each side,
                // stands for the room's reflections, filtered to the
                // snare's colour. The room is all its own: none goes to the
                // kit's, which would blur the cut.
                k[4] = 3.4f * (1.0f + 0.3f * tone); // the band-pass passes 0.45 at its peak
                k[5] = 0.17f * decayScale * rate;
                k[6] = 1.0f - decayMul(0.01f, rate);
                k[7] = decayMul(0.004f, rate);
                swell = 0;
                env2 = 1;
                env2Mul = dm(1.5f);
                f4.set(1700.0f * ratio, 0.45f, rate);
                f5.set(1700.0f * ratio, 0.45f, rate);
                f6.set(9000.0f, 0.6f, rate);
                f7.set(9000.0f, 0.6f, rate);
            }
            gain = 0.45f;
            send = sends[kit];
            break;
        }
        case Clap: {
            if (kit == Cr78) {
                // The guiro: a stick scraped along a gourd's ridges, so a
                // train of clicks (each a ring of band-passed noise) that
                // quickens as the stroke goes, swelling and fading over it.
                // k: 0 samples between ridges, 1 the stroke's length, 2
                // the next ridge.
                k[0] = 0.0095f * rate * std::exp2(-0.3f * tone);
                k[1] = (0.08f + 0.06f * vel) * decayScale * rate * (1.0f + 0.4f * std::max(0.0f, edge));
                k[2] = 0;
                env[0] = 0;
                mul[0] = decayMul(0.0012f, rate);
                f1.set(std::min(3300.0f * ratio, rate * 0.4f), 2.5f, rate);
                f2.set(1200.0f, 0.7f, rate);
                gain = 0.12f;
                pan = 0.25f;
                send = 0.25f;
                break;
            }
            if (kit == Brushes) {
                // The swish: the brush swept round the head. Noise that
                // swells over most of a tenth of a second and fades as
                // slowly, through a band that climbs as the wires speed up.
                // k: 0 the swell, in samples.
                k[0] = (0.07f + 0.04f * std::max(0.0f, edge)) * rate;
                env[0] = 1;
                mul[0] = dm(0.16f * (1.0f + 0.3f * tone));
                k[1] = 2200.0f * ratio * std::exp2(0.4f * tone);
                f1.set(k[1], 0.9f, rate);
                f2.set(700.0f, 0.7f, rate);
                f3.set(9000.0f, 0.6f, rate);
                gain = 0.22f;
                pan = -0.12f + 0.25f * (s.sx - 0.5f);
                send = 0.3f;
                break;
            }
            // The boom bap snap is a clap whose bursts crowd into a few
            // milliseconds, pitched up, with almost no tail; the Linn's
            // tail holds, then a gate shuts it.
            static const float centre[KitCount] = {1200.0f, 1000.0f, 1500.0f, 1100.0f, 1300.0f,
                                                   2300.0f, 1250.0f, 1150.0f, 1200.0f, 1200.0f, 1350.0f, 1200.0f, 1200.0f, 1200.0f, 1500.0f};
            static const float tail[KitCount] = {0.11f, 0.08f, 0.09f, 0.14f, 0.1f, 0.05f, 0.08f, 0.6f, 0.1f, 0.1f, 0.16f, 0.1f, 0.1f, 0.1f, 0.07f};
            static const float spread[KitCount] = {1.0f, 0.9f, 0.8f, 1.15f, 1.0f, 0.25f, 0.85f, 1.1f, 1.0f, 1.0f, 1.2f, 1.0f, 1.0f, 1.0f, 0.7f};
            k[6] = kit == Linn ? 0.2f * decayScale * rate : 1e9f; // the gate
            k[7] = decayMul(0.01f, rate);
            // Burst times in samples.
            k[0] = 0;
            k[1] = 0.0085f * spread[kit] * rate;
            k[2] = k[1] + 0.0095f * spread[kit] * rate;
            k[3] = k[2] + 0.011f * spread[kit] * rate;
            k[4] = decayMul(0.0021f, rate); // one burst's fall, per sample
            env[0] = 0;                     // the burst envelope
            env[1] = 1;                     // the tail
            mul[1] = dm(tail[kit] * (1.0f + 0.25f * tone));
            const float c = centre[kit] * ratio * std::exp2(0.35f * edge);
            f1.set(c, 1.4f, rate);
            f2.set(c * 1.04f, 1.4f, rate);
            f3.set(2500.0f * ratio, 0.7f, rate);
            f4.set(2500.0f * ratio, 0.7f, rate);
            f5.set(std::min(12000.0f, rate * 0.4f), 0.6f, rate);
            f6.set(std::min(12000.0f, rate * 0.4f), 0.6f, rate);
            k[5] = 0.35f + 0.25f * tone; // sizzle
            gain = kit == BoomBap ? 0.6f : 0.7f;
            send = kit == BoomBap ? 0.2f : 0.45f;
            break;
        }
        case Rim: {
            // The brushes' cross-stick lays the stick across the rim: more
            // of the shell's wooden tock, longer, less ring. Jungle's is a
            // high piccolo rimshot that rings. The Linn's is the LM-1's
            // sidestick: a short, low, woody knock with hardly any ring.
            const bool cross = kit == Brushes, piccolo = kit == Jungle, side = kit == Linn;
            hz[0] = (cross ? 560.0f : piccolo ? 660.0f : side ? 400.0f : 480.0f) * ratio;
            hz[1] = (cross ? 1450.0f : piccolo ? 2350.0f : side ? 1150.0f : 1720.0f) * ratio * (1.0f + 0.05f * edge);
            env[0] = env[1] = env[2] = 1;
            mul[0] = dm(cross ? 0.02f : side ? 0.008f : 0.011f);
            mul[1] = dm(piccolo ? 0.018f : side ? 0.004f : 0.007f);
            mul[2] = decayMul(0.0012f, rate);
            k[0] = (cross ? 0.9f : side ? 1.1f : 0.55f) - 0.2f * tone;
            k[1] = (cross ? 0.45f : side ? 0.3f : 0.8f) + 0.3f * tone;
            f1.set(side ? 180.0f : 320.0f, 0.7f, rate);
            f2.set(3500.0f, 1.2f, rate);
            gain = 0.25f;
            pan = -0.2f;
            send = 0.35f;
            break;
        }
        case Shaker: {
            const float attack = (0.004f + 0.012f * (0.5f + 0.5f * edge)) * rate;
            k[0] = attack;
            env[0] = 1;
            // The CR-78's maracas are short and bright; the brushes' shaker
            // a softer, longer shush.
            static const float tau[KitCount] = {0.045f, 0.045f, 0.03f, 0.045f, 0.045f,
                                                0.045f, 0.035f, 0.045f, 0.03f, 0.06f, 0.05f, 0.06f, 0.04f, 0.04f, 0.035f};
            static const float centre[KitCount] = {7200.0f, 7200.0f, 7200.0f, 7200.0f, 7200.0f,
                                                   6500.0f, 7600.0f, 7200.0f, 8200.0f, 5600.0f, 7800.0f, 8500.0f, 6000.0f, 7200.0f, 8200.0f};
            mul[0] = dm(tau[kit] * (1.0f + 0.3f * tone));
            f1.set(std::min(centre[kit] * ratio * std::exp2(0.4f * tone), rate * 0.42f), 0.9f, rate);
            f2.set(4500.0f * ratio, 0.7f, rate);
            f3.set(std::min(13000.0f, rate * 0.4f), 0.6f, rate);
            gain = 0.17f;
            pan = 0.3f + 0.3f * (s.sx - 0.5f);
            send = 0.15f;
            if (kit == Linn) {
                // The cabasa: steel beads scraped round the drum, "chk-a".
                // A sharp, bright scrape, then the beads rolling back,
                // softer, a moment later. k: 1 when they roll back.
                k[0] = 0.0015f * rate;
                mul[0] = dm(0.04f * (1.0f + 0.3f * tone));
                k[1] = (0.075f + 0.03f * std::max(0.0f, edge)) * rate;
                env2 = 0.55f;
                env2Mul = dm(0.05f);
                f1.set(std::min(9800.0f * ratio * std::exp2(0.4f * tone), rate * 0.42f), 0.7f, rate);
                f2.set(6500.0f * ratio, 0.7f, rate);
                gain = 0.2f;
            }
            break;
        }
        case Hat:
        case Open: {
            static const float ratios[6] = {1.0f, 1.483f, 1.800f, 2.546f, 2.630f, 3.897f};
            // The Linn's sampled hats are mostly noise; the CR-78's are
            // noise ringing through a narrow band, its "metal beat" (the
            // open pad) the squares ringing high and short; the jazz foot
            // hat is dark and loose.
            static const float tune[KitCount] = {1.0f, 1.12f, 1.25f, 0.92f, 1.05f, 0.9f, 1.15f, 1.3f, 1.45f, 0.82f, 1.1f, 1.0f, 1.35f, 1.0f, 1.2f};
            static const float noise[KitCount] = {0.45f, 0.3f, 0.75f, 0.55f, 0.5f, 0.65f, 0.6f, 0.9f, 1.1f, 0.7f, 0.5f, 0.5f, 0.4f, 0.5f, 0.8f};
            static const float closedTau[KitCount] = {0.034f, 0.028f, 0.018f, 0.045f, 0.03f,
                                                      0.04f, 0.022f, 0.04f, 0.025f, 0.05f, 0.03f, 0.03f, 0.03f, 0.03f, 0.02f};
            static const float openTau[KitCount] = {0.24f, 0.2f, 0.17f, 0.34f, 0.22f, 0.22f, 0.2f, 0.3f, 0.12f, 0.4f, 0.26f, 0.25f, 0.35f, 0.25f, 0.2f};
            static const float bright[KitCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.85f, 1.05f, 1.0f, 1.0f, 0.75f, 1.0f, 1.0f, 0.9f, 1.0f, 1.05f};
            static const float ring[KitCount] = {1.1f, 1.1f, 1.1f, 1.1f, 1.1f, 1.1f, 1.1f, 1.0f, 4.0f, 1.1f, 1.1f, 1.1f, 2.5f, 1.1f, 1.1f};
            const float base = 205.3f * 1.7f * tune[kit] * ratio * std::exp2(0.25f * tone);
            for (int i = 0; i < 6; ++i) {
                sq[i].phase = rng.unit();
                sqInc[i] = std::min(base * ratios[i] / rate, 0.45f);
            }
            k[0] = noise[kit] * (1.0f + 0.4f * tone);
            const bool open = pad == Open;
            // Struck toward the far edge, a closed hat loosens and an open one
            // washes longer.
            const float tau = open ? openTau[kit] * (1.0f + 0.4f * std::max(0.0f, edge))
                                   : closedTau[kit] * (0.8f + 0.4f * vel) * (1.0f + 0.9f * std::max(0.0f, edge));
            env[0] = 1;
            mul[0] = dm(tau);
            const float b = bright[kit] * std::exp2(0.3f * tone);
            f1.set(std::min(9000.0f * b, rate * 0.42f), kit == Cr78 && open ? 6.0f : ring[kit], rate);
            f2.set(6800.0f * b, 0.7f, rate);
            f4.set(6000.0f * b, 0.7f, rate);
            f3.set(std::min(13500.0f, rate * 0.42f), 0.7f, rate);
            // Brighter and looser kits are trimmed to sit as far under the kick.
            // The CR-78's ringing band is trimmed hardest, its metal more.
            static const float level[KitCount] = {1.0f, 1.0f, 0.7f, 0.8f, 1.0f, 0.9f, 0.85f, 0.6f, 0.22f, 0.55f, 0.9f, 0.8f, 0.5f, 0.8f, 0.75f};
            gain = (open ? 0.19f : 0.25f) * level[kit] * (kit == Cr78 && open ? 0.8f : 1.0f);
            pan = (open ? 0.22f : 0.15f) + 0.25f * (s.sx - 0.5f);
            // Amapiano's hats sit opposite each other, the closed one left
            // and the open one right, each hit a little off the last, so
            // the top of the groove is wide around the shakers.
            if (kit == Amapiano)
                pan = (open ? 0.5f : -0.45f) + 0.15f * rng2.bipolar();
            send = open ? 0.12f : 0.08f;
            break;
        }
        case TomLo:
        case TomMid:
        case TomHi: {
            // The Linn's are the 80s' big toms, falling far; jungle's are
            // a break's tight high toms, snapping down with a slap of skin;
            // the CR-78's congas sit higher and barely bend; brushes on a
            // jazz kit's open toms leave a long hiss of wire on the skin.
            static const float decay[KitCount] = {0.2f, 0.2f, 0.3f, 0.15f, 0.2f, 0.15f, 0.11f, 0.32f, 0.1f, 0.26f, 0.22f, 0.2f, 0.2f, 0.2f, 0.35f};
            static const float drop[KitCount] = {0.75f, 0.75f, 0.75f, 0.55f, 0.75f, 0.55f, 0.9f, 1.1f, 0.15f, 0.3f, 0.6f, 0.5f, 0.5f, 0.5f, 0.5f};
            static const float dropTau[KitCount] = {0.045f, 0.045f, 0.045f, 0.045f, 0.045f,
                                                    0.045f, 0.03f, 0.09f, 0.03f, 0.045f, 0.04f, 0.045f, 0.045f, 0.045f, 0.06f};
            static const float skin[KitCount] = {0.12f, 0.12f, 0.12f, 0.35f, 0.12f, 0.35f, 0.45f, 0.2f, 0.03f, 0.45f, 0.15f, 0.12f, 0.12f, 0.12f, 0.25f};
            static const float skinTau[KitCount] = {0.008f, 0.008f, 0.008f, 0.03f, 0.008f,
                                                    0.03f, 0.015f, 0.02f, 0.004f, 0.05f, 0.01f, 0.008f, 0.008f, 0.008f, 0.02f};
            const int deg = pad == TomLo ? 0 : pad == TomMid ? 2 : 4;
            const float near = kit == Cr78 ? 200.0f : kit == Jungle ? 160.0f : 125.0f;
            hz[0] = degreeHz(h, deg + degrees, near);
            if (pad != TomLo && hz[0] < degreeHz(h, degrees, near))
                hz[0] *= 2.0f;
            env[0] = 1; // amp
            mul[0] = dm(decay[kit] * (pad == TomLo ? 1.15f : 1.0f));
            env[1] = 1; // pitch
            mul[1] = decayMul(dropTau[kit], rate);
            k[0] = drop[kit] * (1.0f + 0.3f * tone) * (1.0f + 0.3f * vel);
            env[2] = 1; // skin noise
            mul[2] = decayMul(skinTau[kit], rate);
            k[1] = skin[kit] * (1.0f + 0.5f * std::max(0.0f, edge));
            k[2] = 1.6f + 1.2f * std::max(0.0f, tone);
            k[3] = 1.0f / fastTanh(k[2]);
            f1.set(hz[0] * 6.0f, 0.9f, rate);
            gain = 0.26f;
            pan = pad == TomLo ? -0.3f : pad == TomMid ? 0.0f : 0.3f;
            send = 0.25f;
            break;
        }
        case Perc: {
            static const float near[KitCount] = {360.0f, 360.0f, 520.0f, 360.0f, 360.0f,
                                                 360.0f, 560.0f, 360.0f, 480.0f, 520.0f, 700.0f, 300.0f, 520.0f, 520.0f, 180.0f};
            hz[0] = degreeHz(h, 4 + degrees, near[kit]);
            const float slap = clampf((s.sy - 0.45f) * 2.5f, 0.0f, 1.0f);
            env[0] = 1;
            env[1] = 1;
            env[2] = 1;
            if (kit == Brushes) {
                // Vibes: a struck bar's fundamental, its fourth partial two
                // octaves up (a vibraphone bar is cut so it rings there) and
                // a brief tenth for the mallet, all under the motor's
                // tremolo.
                const float t0 = 0.9f * decayScale * (1.0f + 0.4f * std::max(0.0f, edge));
                partial[0].set(hz[0], rate, 1.0f, t0);
                partial[1].set(hz[0] * 3.98f, rate, 0.28f * (1.0f + 0.5f * tone), t0 * 0.3f);
                partial[2].set(hz[0] * 9.85f, rate, 0.1f * (1.0f + tone) * vel, 0.04f);
                k[0] = 5.4f / rate; // the tremolo's rate, per sample
                k[1] = 0.0015f * rate; // the mallet's soft landing
                gain = 0.2f;
                pan = 0.3f + 0.3f * (s.sx - 0.5f);
                send = 0.4f;
                break;
            }
            if (kit == Linn) {
                // The Linn's tambourine is the breaks one, brighter.
                static const float ratios[6] = {1.0f, 1.37f, 1.73f, 2.21f, 2.67f, 3.11f};
                for (int i = 0; i < 6; ++i) {
                    sq[i].phase = rng.unit();
                    sqInc[i] = std::min(1650.0f * ratios[i] * ratio / rate, 0.45f);
                }
                mul[0] = dm(0.13f * (1.0f + 0.4f * tone));
                f1.set(9000.0f, 1.0f, rate);
                f2.set(6000.0f, 0.7f, rate);
            } else if (kit == Techno) {
                // A woodblock tok: frequency modulation that dies in a few
                // milliseconds, leaving a short hollow tone.
                mul[0] = dm(0.055f);
                mul[1] = decayMul(0.008f, rate);
                k[0] = 2.6f + 1.5f * tone;
                k[1] = 1.41f;
            } else if (kit == Breaks) {
                // A tambourine: jingles over a short wash.
                static const float ratios[6] = {1.0f, 1.37f, 1.73f, 2.21f, 2.67f, 3.11f};
                for (int i = 0; i < 6; ++i) {
                    sq[i].phase = rng.unit();
                    sqInc[i] = std::min(1450.0f * ratios[i] * ratio / rate, 0.45f);
                }
                mul[0] = dm(0.11f * (1.0f + 0.4f * tone));
                f1.set(9500.0f, 1.0f, rate);
                f2.set(5500.0f, 0.7f, rate);
            } else {
                // Conga or bongo: open tone at the centre, a slap at the
                // edge. Jungle's timbale is a bongo whose metal shell rings
                // on above it (two inharmonic partials); the CR-78's bongo
                // is a short, soft blip.
                static const float tau[KitCount] = {0.13f, 0.13f, 0.07f, 0.13f, 0.13f,
                                                    0.13f, 0.2f, 0.13f, 0.06f, 0.13f, 0.13f, 0.13f, 0.13f, 0.13f, 0.22f};
                mul[0] = dm(tau[kit] * (1.0f - 0.6f * slap) * (1.0f + 0.3f * tone));
                mul[1] = decayMul(0.006f, rate);
                k[0] = kit == Cr78 ? 0.1f : 0.3f;
                mul[2] = decayMul(0.012f, rate);
                k[2] = (kit == Cr78 ? 0.3f : 1.0f) * (0.15f + 0.7f * slap);
                f1.set(kit == Jungle ? 3800.0f : 2400.0f, 1.3f, rate);
                if (kit == Jungle) {
                    partial[0].set(hz[0] * 2.37f, rate, 0.3f, 0.25f * decayScale);
                    partial[1].set(hz[0] * 3.61f, rate, 0.18f, 0.16f * decayScale);
                }
            }
            gain = 0.21f;
            pan = -0.35f + 0.3f * (s.sx - 0.5f);
            send = 0.35f;
            break;
        }
        case Crash: {
            static const float ratios[6] = {1.0f, 1.342f, 1.693f, 2.131f, 2.788f, 3.414f};
            // The jazz crash is a big dark one that washes on; the CR-78's
            // a thin, short splash; jungle's a bright, quick one.
            static const float tune[KitCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.95f, 1.25f, 1.1f, 1.3f, 0.8f, 1.05f, 1.0f, 1.0f, 1.0f, 1.1f};
            static const float wash[KitCount] = {1.1f, 1.1f, 0.8f, 1.3f, 1.1f, 0.7f, 0.85f, 1.25f, 0.6f, 1.35f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f};
            static const float shine[KitCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, 1.0f, 1.0f, 1.0f, 0.7f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
            const float base = 410.0f * tune[kit] * ratio * std::exp2(0.2f * tone);
            for (int i = 0; i < 6; ++i) {
                sq[i].phase = rng.unit();
                sq[i + 6].phase = rng.unit();
                sqInc[i] = std::min(base * ratios[i] / rate, 0.45f);
                sqInc[i + 6] = std::min(base * 1.013f * ratios[i] / rate, 0.45f);
            }
            env[0] = 1; // the hit
            mul[0] = decayMul(0.05f, rate);
            env[1] = 1; // the wash
            mul[1] = dm(wash[kit]);
            env[2] = 1; // brightness
            mul[2] = dm(0.45f);
            k[0] = 1.0f;
            k[1] = shine[kit];
            f1.set(4200.0f, 0.7f, rate);
            f2.set(4200.0f, 0.7f, rate);
            f5.set(3600.0f, 0.7f, rate);
            f6.set(3600.0f, 0.7f, rate);
            gain = 0.36f;
            pan = -0.25f;
            send = 0.12f;
            break;
        }
        case Ride: {
            // The bow: a ping of inharmonic partials up where a ride's
            // stick sound lives, over a wash. Toward the far edge, the bell:
            // the partials drop and ring longer and purer.
            static const float ratios[7] = {1.0f, 1.34f, 1.71f, 2.15f, 2.62f, 3.27f, 4.08f};
            static const float amps[7] = {0.7f, 1.0f, 0.85f, 0.7f, 0.55f, 0.45f, 0.3f};
            static const float taus[7] = {1.1f, 0.95f, 0.8f, 0.65f, 0.55f, 0.45f, 0.35f};
            // The jazz ride is big and dark, rings long, and its bell is
            // always in it a little; the CR-78's cymbal is mostly hiss.
            static const float pitch[KitCount] = {1250.0f, 1500.0f, 1250.0f, 1250.0f, 1250.0f,
                                                  1150.0f, 1350.0f, 1300.0f, 1600.0f, 950.0f, 1300.0f, 1250.0f, 1250.0f, 1250.0f, 1400.0f};
            static const float ring[KitCount] = {1.0f, 1.0f, 0.7f, 1.0f, 1.0f, 0.75f, 1.0f, 1.1f, 0.5f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.8f};
            static const float hiss[KitCount] = {0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.9f, 0.3f, 0.5f, 0.55f, 0.55f, 0.55f, 0.6f};
            const float bell = clampf(edge + (kit == Brushes ? 0.2f : 0.0f), 0.0f, 1.0f);
            const float base = pitch[kit] * ratio * std::exp2(0.25f * tone) * (1.0f - 0.5f * bell);
            // No two strokes of a real ride ring quite alike: each hit's
            // partials wander a little, so a run of eighths shimmers
            // instead of droning on one chord.
            for (int i = 0; i < 7; ++i) {
                const float shape = i < 3 ? 1.0f + 1.2f * bell : 1.0f - 0.5f * bell;
                const float wander = 1.0f + 0.018f * rng.bipolar();
                partial[i].set(base * ratios[i] * wander, rate, amps[i] * shape / 6.0f,
                               taus[i] * (0.65f + 1.0f * bell) * decayScale * ring[kit]);
            }
            env[0] = 1; // wash
            mul[0] = dm(0.5f);
            env[1] = 1; // stick
            mul[1] = decayMul(0.0015f, rate);
            k[0] = hiss[kit] * (1.0f - 0.6f * bell) * (1.0f + 0.5f * tone);
            f1.set(6500.0f, 0.7f, rate);
            f2.set(5000.0f, 1.2f, rate);
            f3.set(std::min(14500.0f, rate * 0.4f), 0.6f, rate); // air in the wash, not fizz
            gain = 0.17f;
            pan = 0.3f;
            send = 0.1f;
            break;
        }
        case Bell: {
            if (kit == Brushes) {
                // The ride's bell, struck on its own and tuned: one partial
                // on the note, the cup's inharmonic others fading faster
                // above it, a tick of the stick and a breath of the bow's
                // wash.
                static const float ratios[5] = {1.0f, 1.52f, 2.24f, 2.93f, 4.12f};
                static const float amps[5] = {1.0f, 0.42f, 0.38f, 0.24f, 0.14f};
                static const float taus[5] = {1.1f, 0.8f, 0.6f, 0.45f, 0.3f};
                hz[0] = degreeHz(h, 4 + degrees, 760.0f);
                const float t = decayScale * (1.0f + 0.3f * edge);
                for (int i = 0; i < 5; ++i)
                    partial[i].set(hz[0] * ratios[i], rate, amps[i] * (i ? 1.0f + 0.5f * tone : 1.0f), taus[i] * t);
                env[0] = 1; // wash
                mul[0] = dm(0.35f);
                env[1] = 1; // stick
                mul[1] = decayMul(0.0015f, rate);
                f1.set(6500.0f, 0.7f, rate);
                f2.set(5000.0f, 1.2f, rate);
                f3.set(std::min(14500.0f, rate * 0.4f), 0.6f, rate);
                gain = 0.11f;
                pan = 0.32f + 0.2f * (s.sx - 0.5f);
                send = 0.12f;
                break;
            }
            // The 808's two squares, here a perfect fifth apart so the bell
            // rings in key.
            hz[0] = degreeHz(h, 4 + degrees, kit == Cr78 ? 800.0f : kit == Jungle ? 900.0f : 680.0f);
            sq[0].phase = 0;
            sq[1].phase = 0.25f;
            sqInc[0] = std::min(hz[0] / rate, 0.45f);
            sqInc[1] = std::min(hz[0] * 1.4983f / rate, 0.45f);
            env[0] = 1;
            mul[0] = decayMul(0.01f, rate);
            env[1] = 1;
            mul[1] = dm((kit == Techno ? 0.09f : kit == Linn || kit == Cr78 ? 0.12f : 0.15f) * (1.0f + 0.3f * edge));
            f1.set(hz[0] * 2.2f * std::exp2(0.4f * tone), 1.4f, rate);
            f2.set(380.0f, 0.7f, rate);
            gain = 0.09f;
            pan = 0.25f + 0.3f * (s.sx - 0.5f);
            send = 0.3f;
            break;
        }
        case Zap: {
            pan = -0.15f + 0.4f * (s.sx - 0.5f);
            send = 0.3f;
            env[0] = 1;
            env[1] = 1;
            if (kit == BoomBap) {
                // A baby scratch: the record pushed forward and pulled back
                // once. Its speed swings as a sine through zero; the sound's
                // pitch and loudness follow the speed, and a band-pass
                // follows the pitch, so it says "wicka". k: 0 the note at
                // full speed, 1 the hand's motion, cycles per sample.
                k[0] = degreeHz(h, degrees, 330.0f);
                k[1] = (4.6f + 1.2f * std::max(0.0f, edge)) / rate;
                mul[1] = dm(0.16f);
                f1.set(1200.0f, 0.9f, rate);
                f2.set(250.0f, 0.7f, rate);
                gain = 0.42f;
                send = 0.15f;
                break;
            }
            if (kit == Brushes) {
                // A temple block in key: the techno tok's frequency
                // modulation, rounder and a touch longer.
                hz[0] = degreeHz(h, degrees, 620.0f);
                mul[0] = decayMul(0.006f, rate);
                mul[1] = dm(0.07f);
                k[0] = 1.8f + 1.0f * tone;
                k[1] = 1.0f;
                k[2] = 2.76f;
                gain = 0.2f;
                pan = 0.4f;
                break;
            }
            if (kit == Jungle) {
                // A dub siren, the sound system's: a squared-off tone that
                // whoops up an octave and back down, on the note. k: 0 the
                // whoop's rate, cycles per sample.
                hz[0] = degreeHz(h, degrees, 440.0f);
                k[0] = (2.2f + 1.2f * std::max(0.0f, edge)) / rate;
                mul[1] = dm(0.35f * (1.0f + 0.4f * tone));
                f1.set(2400.0f * std::exp2(tone), 0.7f, rate);
                gain = 0.17f;
                send = 0.45f;
                break;
            }
            // The Linn kit's is a Simmons syndrum: a sine that starts three
            // times higher and falls all the way down as it fades, "pyoo".
            // The CR-78's is a clave, a pure high tone gone in a few
            // milliseconds.
            hz[0] = degreeHz(h, degrees, kit == Linn ? 150.0f : kit == Cr78 ? 2100.0f : 220.0f);
            if (kit == Linn) {
                k[0] = 3.0f + 1.0f * std::max(0.0f, edge);
                mul[0] = dm(0.28f * (1.0f + 0.4f * tone));
                mul[1] = dm(0.33f);
                k[1] = 0.12f;
                gain = 0.3f;
                send = 0.35f;
            } else if (kit == Cr78) {
                k[0] = 0.04f;
                mul[0] = decayMul(0.002f, rate);
                mul[1] = dm(0.03f * (1.0f + 0.3f * tone));
                k[1] = 0.0f;
                gain = 0.2f;
                send = 0.3f;
            } else {
                k[0] = 10.0f + 16.0f * (0.5f + 0.5f * edge); // sweep depth
                mul[0] = decayMul(0.03f * (1.0f + 0.4f * tone), rate);
                mul[1] = dm(0.13f);
                k[1] = 0.6f + 0.4f * tone;
                gain = 0.2f;
            }
            break;
        }
        default:
            active = false;
            return;
        }
        place(dyn, pan);
    }

    // The hit's loudness, its kit's level and where it sits in the stereo.
    void place(float dyn, float pan) {
        gain *= dyn * kitTrim(kit);
        pan = clampf(pan, -0.9f, 0.9f);
        gl = std::sqrt(0.5f * (1.0f - pan));
        gr = std::sqrt(0.5f * (1.0f + pan));
    }

    // Adds n samples into l, r and the room's send.
    void render(float *l, float *r, float *room, int n) {
        int from = 0;
        if (delay > 0) {
            if (delay >= n) {
                delay -= n;
                return;
            }
            from = delay;
            delay = 0;
        }
        const float inv = 1.0f / rate;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            float sl = 0, sr = 0; // stereo where the drum is wide, else sl only
            bool stereo = false;
            // The new kits' instruments play themselves (renderNew).
            switch (modes || chipOn || special != NoSpecial ? kOwnVoice : pad) {
            case kOwnVoice:
                sl = renderNew(sr, stereo, inv);
                break;
            case Kick: {
                env[0] *= mul[0];
                env[1] *= mul[1];
                const float f = hz[0] * (1.0f + k[0] * env[0] + k[1] * env[1]);
                ph[0] += f * inv;
                ph[0] -= std::floor(ph[0]);
                if (t >= holdSamples)
                    env[2] *= mul[2];
                const float body = fastTanh(k[2] * std::sin(kTwoPi * ph[0]) * env[2]) * k[4];
                env[3] *= mul[3];
                const float click = f1.step(rng.bipolar()).band * env[3] * k[3] * 2.5f;
                sl = body + click;
                if (kit == Amapiano)
                    sl = f3.step(f2.step(sl).high).high;
                break;
            }
            case Snare: {
                env[1] *= mul[1];
                const float pitch = 1.0f + 0.5f * env[1];
                ph[0] += hz[0] * pitch * inv;
                ph[1] += hz[1] * pitch * inv;
                ph[0] -= std::floor(ph[0]);
                ph[1] -= std::floor(ph[1]);
                env[0] *= mul[0];
                const float body = (std::sin(kTwoPi * ph[0]) + 0.55f * std::sin(kTwoPi * ph[1])) * env[0] * k[0];
                const float n0 = rng.bipolar();
                const float nz = f3.step(f1.step(n0).high * 0.7f + f2.step(n0).band * 1.3f).low;
                env[2] *= mul[2];
                const float attack = std::min(1.0f, t / k[3]);
                env[3] *= mul[3];
                // Tape-like rounding of the noise's tallest peaks: the snare
                // stays as loud but no longer spikes past the kick.
                sl = fastTanh(1.2f * (body + nz * env[2] * attack * k[1] + n0 * env[3] * k[2])) * (1.0f / 1.2f);
                if (kit == Linn) {
                    // The gated room: it swells, holds while it slowly
                    // fades, and the gate shuts on everything, the snare's
                    // own tail too, so it chops to silence.
                    swell = t < k[5] ? swell + (1.0f - swell) * k[6] : swell * k[7];
                    env2 *= env2Mul;
                    const float e = swell * env2 * k[4];
                    const float cut = t < k[5] ? 1.0f : swell;
                    const float rl = f6.step(f4.step(rng2.bipolar()).band).low;
                    const float rr = f7.step(f5.step(rng2.bipolar()).band).low;
                    sl *= cut;
                    sr = sl + rr * e;
                    sl += rl * e;
                    stereo = true;
                }
                break;
            }
            case Clap: {
                if (kit == Cr78) {
                    // The guiro: a ridge struck, then the next a little
                    // sooner, until the stroke ends.
                    if (t >= k[2] && t < k[1]) {
                        env[0] = 1;
                        k[2] += k[0];
                        k[0] *= 0.985f;
                    }
                    env[0] *= mul[0];
                    const float w = t / k[1];
                    const float shape = w < 1.0f ? 0.3f + 2.8f * w * (1.0f - w) : 0.3f;
                    sl = f2.step(f1.step(rng.bipolar() * env[0]).band).high * shape * 3.0f;
                    break;
                }
                if (kit == Brushes) {
                    // The swish: a slow swell, a slow fade, and every few
                    // samples the band moves up with the stroke.
                    const float a = t < k[0] ? t / k[0] : 1.0f;
                    if (t >= k[0])
                        env[0] *= mul[0];
                    if ((int(t) & 15) == 0)
                        f1.set(k[1] * (1.0f + 0.8f * a) * (0.8f + 0.2f * env[0]), 0.9f, rate);
                    const float n0 = rng.bipolar();
                    sl = f3.step(f2.step(f1.step(n0).band).high).low * a * a * env[0] * 2.2f;
                    break;
                }
                float burst = 0;
                for (int b = 0; b < 4; ++b) {
                    const float since = t - k[b];
                    if (since >= 0 && since < 0.009f * rate)
                        burst = std::max(burst, std::pow(k[4], since));
                }
                const bool tailing = t > k[3];
                if (tailing)
                    env[1] *= t < k[6] ? mul[1] : k[7]; // a gate shuts the Linn's
                const float nl = rng.bipolar(), nr = rng2.bipolar();
                const float bl = f1.step(nl).band * 2.2f + f3.step(nl).high * k[5];
                const float br = f2.step(nr).band * 2.2f + f4.step(nr).high * k[5];
                const float tail = tailing ? env[1] * 0.55f : 0.0f;
                // A clap is a crowd of transients: rounding the tallest
                // keeps it loud without spiking past the kick.
                // Then a low-pass at 12 kHz: the saturated noise would
                // otherwise fizz up to Nyquist.
                sl = f5.step(fastTanh(1.6f * (bl * burst + bl * tail)) * 0.8f).low;
                sr = f6.step(fastTanh(1.6f * (bl * burst + br * tail)) * 0.8f).low;
                stereo = true;
                break;
            }
            case Rim: {
                ph[0] += hz[0] * inv;
                ph[1] += hz[1] * inv;
                ph[0] -= std::floor(ph[0]);
                ph[1] -= std::floor(ph[1]);
                env[0] *= mul[0];
                env[1] *= mul[1];
                env[2] *= mul[2];
                float v = std::sin(kTwoPi * ph[0]) * env[0] * k[0] + std::sin(kTwoPi * ph[1]) * env[1] * k[1];
                v = f1.step(v).high + f2.step(rng.bipolar()).band * env[2] * 1.5f;
                sl = fastTanh(1.6f * v);
                break;
            }
            case Shaker: {
                const float a = t < k[0] ? (t / k[0]) * (t / k[0]) : 1.0f;
                if (t >= k[0])
                    env[0] *= mul[0];
                float e = a * env[0];
                if (kit == Linn && t >= k[1]) { // the cabasa's beads rolling back
                    env2 *= env2Mul;
                    e += env2 * std::min(1.0f, (t - k[1]) / (0.004f * rate));
                }
                const float n0 = rng.bipolar();
                sl = f3.step(f2.step(f1.step(n0).band).high).low * e * 2.0f;
                break;
            }
            case Hat:
            case Open: {
                float m = 0;
                for (int q = 0; q < 6; ++q)
                    m += sq[q].step(sqInc[q]);
                m = m * (1.0f / 6.0f) + rng.bipolar() * k[0];
                // Two high-pass stages: the cluster's low partials would ring
                // as a clang, and a hat is all shimmer.
                const float v = f3.step(f4.step(f2.step(f1.step(m).band + 0.4f * m).high).high).low;
                env[0] *= mul[0];
                const float attack = std::min(1.0f, t / (0.0006f * rate));
                sl = v * env[0] * attack * 2.2f;
                break;
            }
            case TomLo:
            case TomMid:
            case TomHi: {
                env[1] *= mul[1];
                ph[0] += hz[0] * (1.0f + k[0] * env[1]) * inv;
                ph[0] -= std::floor(ph[0]);
                env[0] *= mul[0];
                env[2] *= mul[2];
                const float body = fastTanh(k[2] * std::sin(kTwoPi * ph[0]) * env[0]) * k[3];
                sl = body + f1.step(rng.bipolar()).band * env[2] * k[1] * 2.0f;
                break;
            }
            case Perc: {
                env[0] *= mul[0];
                if (kit == Brushes) {
                    // The vibes' motor turns a disc over each bar's
                    // resonator: the tone pulses a few times a second.
                    ph[0] += k[0];
                    ph[0] -= std::floor(ph[0]);
                    const float trem = 1.0f - 0.3f * (0.5f + 0.5f * std::sin(kTwoPi * ph[0]));
                    const float a = std::min(1.0f, t / k[1]);
                    sl = (partial[0].step() + partial[1].step() + partial[2].step()) * trem * a;
                } else if (kit == Techno) {
                    env[1] *= mul[1];
                    ph[1] += hz[0] * k[1] * inv;
                    ph[1] -= std::floor(ph[1]);
                    ph[0] += hz[0] * inv;
                    ph[0] -= std::floor(ph[0]);
                    sl = std::sin(kTwoPi * ph[0] + k[0] * env[1] * std::sin(kTwoPi * ph[1])) * env[0];
                } else if (kit == Breaks || kit == Linn) {
                    float m = 0;
                    for (int q = 0; q < 6; ++q)
                        m += sq[q].step(sqInc[q]);
                    m = m * (1.0f / 6.0f) + 0.5f * rng.bipolar();
                    // Jingles: the wash flutters as the discs settle.
                    const float flutter = 0.65f + 0.35f * std::sin(kTwoPi * 31.0f * t * inv);
                    sl = f2.step(f1.step(m).band).high * env[0] * flutter * 2.4f;
                } else {
                    env[1] *= mul[1];
                    env[2] *= mul[2];
                    ph[0] += hz[0] * (1.0f + k[0] * env[1]) * inv;
                    ph[0] -= std::floor(ph[0]);
                    sl = std::sin(kTwoPi * ph[0]) * env[0] + f1.step(rng.bipolar()).band * env[2] * k[2] * 2.0f;
                    if (kit == Jungle) // the timbale's shell
                        sl += partial[0].step() + partial[1].step();
                }
                break;
            }
            case Crash: {
                float ml = 0, mr = 0;
                for (int q = 0; q < 6; ++q) {
                    ml += sq[q].step(sqInc[q]);
                    mr += sq[q + 6].step(sqInc[q + 6]);
                }
                env[0] *= mul[0];
                env[1] *= mul[1];
                env[2] *= mul[2];
                if ((int(t) & 15) == 0) {
                    const float hz2 = std::min((6000.0f + 7000.0f * env[2]) * k[1], rate * 0.4f);
                    f3.set(hz2, 0.6f, rate);
                    f4.set(hz2, 0.6f, rate);
                }
                const float e = (0.55f * env[0] + 0.45f * env[1]) * std::min(1.0f, t / (0.0015f * rate));
                const float nl = rng.bipolar(), nr = rng2.bipolar();
                sl = f3.step(f5.step(f1.step(ml * 0.12f + nl * 0.55f).high).high).low * e;
                sr = f4.step(f6.step(f2.step(mr * 0.12f + nr * 0.55f).high).high).low * e;
                stereo = true;
                break;
            }
            case Ride: {
                float v = 0;
                for (int q = 0; q < 7; ++q)
                    v += partial[q].step();
                env[0] *= mul[0];
                env[1] *= mul[1];
                const float n0 = rng.bipolar();
                v += f3.step(f1.step(n0).high).low * env[0] * k[0] + f2.step(n0).band * env[1] * 0.6f;
                sl = v;
                break;
            }
            case Bell: {
                if (kit == Brushes) {
                    float v = 0;
                    for (int q = 0; q < 5; ++q)
                        v += partial[q].step();
                    env[0] *= mul[0];
                    env[1] *= mul[1];
                    const float n0 = rng.bipolar();
                    sl = v + f3.step(f1.step(n0).high).low * env[0] * 0.15f + f2.step(n0).band * env[1] * 0.8f;
                    break;
                }
                const float m = sq[0].step(sqInc[0]) + sq[1].step(sqInc[1]);
                env[0] *= mul[0];
                env[1] *= mul[1];
                const float e = 0.6f * env[0] + 0.4f * env[1];
                sl = f2.step(f1.step(m).band).high * e * 1.4f;
                break;
            }
            case Zap: {
                env[0] *= mul[0];
                env[1] *= mul[1];
                if (kit == BoomBap) {
                    // The record's speed, forward then back; the tone is a
                    // squared-off sine (a sample's mids) with some of the
                    // vinyl's hiss, both as loud as the record is fast.
                    ph[1] += k[1];
                    const float v = std::sin(kTwoPi * std::min(ph[1], 1.0f));
                    const float speed = std::fabs(v);
                    ph[0] += k[0] * (0.12f + 1.4f * speed) * inv;
                    ph[0] -= std::floor(ph[0]);
                    if ((int(t) & 15) == 0)
                        f1.set(500.0f + 2600.0f * speed, 0.9f, rate);
                    const float tone = fastTanh(2.4f * std::sin(kTwoPi * ph[0])) + 0.35f * rng.bipolar();
                    // One push and pull, then the hand lets the record stop.
                    const float done = ph[1] < 1.0f ? 1.0f : 0.0f;
                    sl = f2.step(f1.step(tone * speed * done).band).high * env[1] * 2.0f;
                    break;
                }
                if (kit == Jungle) {
                    ph[1] += k[0];
                    const float up = 0.5f - 0.5f * std::cos(kTwoPi * ph[1]);
                    ph[0] += hz[0] * (1.0f + up) * inv;
                    ph[0] -= std::floor(ph[0]);
                    const float a = std::min(1.0f, t / (0.004f * rate));
                    sl = f1.step(fastTanh(2.5f * std::sin(kTwoPi * ph[0]))).low * env[1] * a;
                    break;
                }
                if (kit == Brushes) {
                    ph[0] += hz[0] * inv;
                    ph[1] += hz[0] * k[2] * inv;
                    ph[0] -= std::floor(ph[0]);
                    ph[1] -= std::floor(ph[1]);
                    sl = std::sin(kTwoPi * ph[0] + k[0] * env[0] * std::sin(kTwoPi * ph[1])) * env[1];
                    break;
                }
                ph[0] += hz[0] * (1.0f + k[0] * env[0]) * inv;
                ph[0] -= std::floor(ph[0]);
                sl = std::sin(kTwoPi * ph[0] + k[1] * env[0] * std::sin(kTwoPi * 2.0f * ph[0])) * env[1];
                break;
            }
            default:
                break;
            }
            fade *= fadeMul;
            const float g = gain * fade;
            sl *= g;
            sr = stereo ? sr * g : sl;
            if (dusty) {
                // The sampler, after the level so it hears the drum as it
                // sits in the mix: a little drive, then the SP-1200's 26 kHz
                // and twelve bits. With no filter before it, the hold folds
                // the highs back down as a metallic grit, and the tails turn
                // gritty as they fall toward the last steps; the output
                // filter's dark top follows.
                auto crush = [](float x) {
                    return std::floor(fastTanh(1.2f * x) * (2048.0f / 1.2f) + 0.5f) * (1.0f / 2048.0f);
                };
                holdPhase += holdInc;
                if (holdPhase >= 1.0f) {
                    holdPhase -= 1.0f;
                    heldL = crush(sl);
                    heldR = stereo ? crush(sr) : heldL;
                }
                sl = dustL.step(heldL).low;
                sr = stereo ? dustR.step(heldR).low : sl;
            }
            l[i] += sl * gl * 1.4142f;
            r[i] += sr * gr * 1.4142f;
            room[i] += (sl + sr) * 0.5f * send;
            peak = std::max(peak, std::fabs(sl));
            t += 1.0f;
        }
        level = std::max(level * 0.8f, peak);
        if (fade < 1e-3f || (t > 0.03f * rate && level < 1e-4f))
            active = false;
    }
};

// The 808: one voice, a sine with a little punch and drive, that sustains
// while held and glides to each new degree, the way a trap bass slides.
struct Sub808 {
    bool active = false, held = false, attacking = false;
    int id = -1;
    int delay = 0;
    float degree = 0, targetDegree = 0;
    float hz = 55, phase = 0;
    float env = 0, punch = 0, click = 0;
    float vel = 0.8f, autoOff = -1; // samples until a groove's 808 lets go
    float glide = 0;
    float level = 0;
    float rate = 48000;
    int kit = House;
    Edit edit;
    Svf clickBp, lp;
    // The log drum's, the bayan's, the jegogan's and the balafon's: two more
    // oscillators and a brightness that fades after the attack; the log
    // drum's bark and the balafon's buzz; the bayan's bend (the heel
    // pressing, in semitones), where it is and where it is going.
    float phase2 = 0, phase3 = 0, bright = 0;
    Svf bark, buzz, buzzLp;
    float bend = 0, bendTarget = 0;
    float since = 1e9f; // samples since the last note began, for the log drum's slides
    float cap = 0;      // the chip's output capacitor, for its pulse (ChipVoice)
    float subPh[2] = {}; // the log drum's sub, an octave and two under its note
    Rng rng{0x808u};

    // holdSeconds > 0: a hit that lets go by itself; 0: held until off().
    void start(int fingerId, int deg, float velocity, float holdSeconds, int kit_, const Edit &e, int delaySamples,
               float rate_) {
        rate = rate_;
        // A new note while the last still sounds slides into it; the
        // upright's is plucked afresh, from where the last left the string.
        // The jegogan's and the balafon's bars are struck afresh too, never
        // slid. The log drum's slide takes longer the further apart its
        // notes are played: a riff's quick notes slip into each other, a
        // long one moans across.
        const bool slide =
            active && env > 0.05f && kit_ != Brushes && kit_ != Cr78 && kit_ != Gamelan && kit_ != Djembe;
        id = fingerId;
        held = true;
        autoOff = holdSeconds > 0 ? holdSeconds * rate : -1;
        delay = delaySamples;
        targetDegree = float(deg);
        if (!slide)
            degree = targetDegree;
        glide = !slide ? 0.0f : kit_ == Amapiano ? clampf(0.35f * since / rate, 0.025f, 0.15f) : 0.06f;
        since = 0;
        // The bayan played by a groove presses its heel in by itself, a
        // couple of semitones; held by a finger, the finger presses it.
        bendTarget = kit_ == Tabla && holdSeconds > 0 ? 2.0f : 0.0f;
        if (!slide)
            bend = 0;
        vel = clampf(velocity, 0.05f, 1.0f);
        kit = kit_;
        edit = e;
        if (!active)
            phase = phase2 = phase3 = cap = subPh[0] = subPh[1] = 0;
        if (!slide)
            bright = 1.0f;
        active = true;
        attacking = true;
        punch = slide ? 0.25f : 1.0f;
        click = slide ? 0.0f : 1.0f;
        // The 808's click is up high; the upright's thump, the finger, low;
        // the log drum's knock in the middle, the bayan's palm lower still,
        // the jegogan's mallet bright.
        const float clickHz = kit == Brushes || kit == Cr78 ? 320.0f
                              : kit == Amapiano         ? 1200.0f
                              : kit == Tabla            ? 250.0f
                              : kit == Gamelan          ? 3000.0f
                              : kit == Djembe           ? 2500.0f
                                                        : 2000.0f;
        clickBp.set(clickHz, 0.8f, rate);
        lp.set(std::min(5000.0f, rate * 0.4f), 0.6f, rate);
        buzz.set(1800.0f, 1.2f, rate);
        buzzLp.set(std::min(6000.0f, rate * 0.4f), 0.6f, rate);
    }
    void off(int fingerId) {
        if (fingerId == id)
            held = false;
    }

    void render(float *l, float *r, int n, const Harmony &h) {
        if (!active)
            return;
        int from = 0;
        if (delay > 0) {
            if (delay >= n) {
                delay -= n;
                return;
            }
            from = delay;
            delay = 0;
        }
        since += float(n - from);
        // The pitch is worked out every block from the key, so a held 808
        // follows a key change.
        if (glide > 0)
            degree += (targetDegree - degree) * (1.0f - std::exp(-float(n - from) / (glide * rate)));
        else
            degree = targetDegree;
        const float lo = std::floor(degree);
        const float frac = degree - lo;
        const int deg = int(lo) + int(std::lround(edit.tune));
        const bool upright = kit == Brushes || kit == Cr78;
        if (upright) {
            // The upright plays the chord, not the key: its degrees count
            // from the chord's root, an octave up where a laptop can hear
            // its fundamental.
            const int oct = octaveNear(h, 100.0f);
            const float m0 = float(h.midi(chordDegree(h, deg), oct)), m1 = float(h.midi(chordDegree(h, deg + 1), oct));
            hz = midiToHz(m0 + (m1 - m0) * frac);
            renderUpright(l, r, from, n);
            return;
        }
        // The log drum and the bayan sit higher than an 808, where their
        // tone carries; the jegogan's bar higher still; the chip's pulse
        // where its harmonics do the work.
        float near = kit == Trap ? 46.0f : 50.0f;
        switch (kit) {
        case Amapiano: near = 55.0f; break;
        case Tabla: near = 85.0f; break;
        case Gamelan: near = 120.0f; break;
        case Chip: near = 55.0f; break;
        case Djembe: near = 175.0f; break;
        default: break;
        }
        const int oct = octaveNear(h, near);
        const float m0 = float(h.midi(deg, oct)), m1 = float(h.midi(deg + 1, oct));
        hz = midiToHz(m0 + (m1 - m0) * frac);
        switch (kit) {
        case Amapiano: renderLog(l, r, from, n); return;
        case Tabla: renderBayan(l, r, from, n); return;
        case Gamelan:
            // The jegogan is in the gamelan's tuning, as the rest of it.
            hz = gamelanHz(h, deg, near);
            renderBar(l, r, from, n);
            return;
        case Chip: renderPulse(l, r, from, n); return;
        case Djembe: renderBalafon(l, r, from, n); return;
        default: break;
        }
        // Jungle's is a clean, long sub; the CR-78's and the brushes' a
        // round bass that fades like a plucked string, barely driven.
        static const float sustainTau[KitCount] = {1.0f, 1.2f, 2.6f, 0.8f, 1.1f, 0.9f, 1.6f, 0.7f, 0.5f, 0.45f, 0.35f, 0.7f, 1.4f, 1.0f, 1.3f};
        static const float drives[KitCount] = {1.6f, 2.4f, 2.2f, 1.8f, 1.7f, 2.6f, 1.2f, 2.0f, 1.1f, 1.0f, 1.5f, 1.1f, 1.0f, 1.0f, 3.4f};
        static const float punchAmt[KitCount] = {1.4f, 1.4f, 1.4f, 1.4f, 1.4f, 1.4f, 1.0f, 1.4f, 0.5f, 0.3f, 1.0f, 0.0f, 0.0f, 0.0f, 1.6f};
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float sustain = decayMul(sustainTau[kit] * decayScale, rate);
        const float release = decayMul(0.07f, rate);
        const float drive = drives[kit] * std::exp2(edit.tone * 1.2f);
        const float norm = 1.0f / fastTanh(drive);
        const float punchMul = decayMul(0.04f, rate), clickMul = decayMul(0.003f, rate);
        const float attack = 1.0f - decayMul(0.0015f, rate);
        const float trim = kitTrim(kit);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            if (autoOff > 0) {
                autoOff -= 1.0f;
                if (autoOff <= 0)
                    held = false;
            }
            if (attacking) {
                env += (1.0f - env) * attack;
                attacking = env < 0.995f;
            } else {
                env *= held ? sustain : release;
            }
            punch *= punchMul;
            click *= clickMul;
            phase += hz * (1.0f + punchAmt[kit] * punch) / rate;
            phase -= std::floor(phase);
            const float s = fastTanh(drive * std::sin(kTwoPi * phase) * env) * norm;
            const float c = clickBp.step(rng.bipolar()).band * click * 0.25f;
            const float y = (s + c) * vel * 0.47f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!held && !attacking && env < 1e-4f)
            active = false;
    }

    // A walking bass's degree, counted from the chord's root: 2, 4 and 6
    // are the chord's own third, fifth and seventh (so a sus or an altered
    // chord is followed), anything else a step of the scale from its root,
    // so −1 is the step below the root that leads back to it.
    static int chordDegree(const Harmony &h, int d) {
        if ((d == 2 || d == 4 || d == 6) && d / 2 < std::clamp(h.chordCount, 1, 4))
            return h.chordTones[d / 2];
        return h.chordRoot + d;
    }

    // What every one of the 808 pad's voices does each sample with its
    // envelope: a groove's note lets go by itself; the attack rises fast,
    // then the level falls slowly while held and faster once let go.
    inline void envelope(float attack, float sustain, float release) {
        if (autoOff > 0) {
            autoOff -= 1.0f;
            if (autoOff <= 0)
                held = false;
        }
        if (attacking) {
            env += (1.0f - env) * attack;
            attacking = env < 0.995f;
        } else {
            env *= held ? sustain : release;
        }
    }

    // Amapiano's log drum: a deep sine (around 55 Hz, an octave under
    // where it would carry by itself) that phase-modulates itself and is
    // driven into a soft clipper, so its harmonics carry it on a laptop's
    // speakers; it drops into its note from an octave up in a couple of
    // hundredths of a second, and each attack sets off its bark, a
    // resonant band of the driven tone sweeping from 900 Hz down to 400 as
    // it fades, the knock that makes it a log drum. Held, its tail rings
    // most of a second; the tone (twisted up) drives it harder.
    //
    // Under it all a clean sub, because on a club's system the log drum is
    // the loudest low end in the record, and its riff leaps up out of the
    // bottom octave: the note itself and the sines an octave and two under
    // it, each weighted by how near it is to 55 Hz on a window an octave
    // either side (cos² of the distance in octaves, so the weights always
    // add up to one). Whatever note it plays and wherever it slides, the
    // sub stays between about 40 and 80 Hz and moves smoothly, while the
    // bark above it is left exactly as it was.
    static constexpr float kLogSub = 0.95f;
    void renderLog(float *l, float *r, int from, int n) {
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float sustain = decayMul(0.8f * decayScale, rate), release = decayMul(0.3f * decayScale, rate);
        const float punchMul = decayMul(0.02f, rate), brightMul = decayMul(0.06f, rate),
                    clickMul = decayMul(0.004f, rate);
        const float attack = 1.0f - decayMul(0.001f, rate);
        const float tone = clampf(edit.tone, -1.0f, 1.0f);
        const float index = 1.6f * std::exp2(tone);
        const float drive = 2.4f * std::exp2(1.2f * std::max(0.0f, tone));
        const float norm = 1.0f / fastTanh(drive);
        const float trim = kitTrim(kit);
        auto weight = [](float hzOf) {
            const float x = std::log2(hzOf / 55.0f);
            const float c = std::cos(0.5f * kPi * x);
            return std::fabs(x) < 1.0f ? c * c : 0.0f;
        };
        const float w0 = weight(hz), w1 = weight(0.5f * hz), w2 = weight(0.25f * hz);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            envelope(attack, sustain, release);
            punch *= punchMul;
            bright *= brightMul;
            click *= clickMul;
            subPh[0] += 0.5f * hz * (1.0f + punch) / rate;
            subPh[0] -= std::floor(subPh[0]);
            subPh[1] += 0.25f * hz * (1.0f + punch) / rate;
            subPh[1] -= std::floor(subPh[1]);
            phase += hz * (1.0f + punch) / rate;
            phase -= std::floor(phase);
            const float w = kTwoPi * phase;
            const float sine = std::sin(w + index * (0.35f + 0.65f * bright) * std::sin(w));
            const float driven = fastTanh(drive * sine * env) * norm;
            if (((i - from) & 15) == 0)
                bark.set(420.0f + 480.0f * bright, 4.0f, rate);
            // The bark is rung by the tone squared off (rich in the
            // harmonics up where it sits) and the stick's knock.
            const float edge = fastTanh(6.0f * sine) * env + clickBp.step(rng.bipolar()).band * click * 2.0f;
            const float knock = bark.step(edge).band * 0.25f;
            const float sub = (w0 * std::sin(w) + w1 * std::sin(kTwoPi * subPh[0]) + w2 * std::sin(kTwoPi * subPh[1])) * env;
            const float y = (driven + knock * (0.15f + 0.85f * bright) + kLogSub * sub) * vel * 0.42f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!held && !attacking && env < 1e-4f)
            active = false;
    }

    // The bayan held: a skin whose pitch rises as the heel presses into it,
    // from a little under the note onto it, its second and third modes
    // fading first, over the thump of the palm. It dies like a drum even
    // held; sliding between degrees is the heel sliding across the skin,
    // and the finger moving up the pad presses the heel in, bending it up
    // to five semitones (the gumki).
    void renderBayan(float *l, float *r, int from, int n) {
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float sustain = decayMul(0.7f * decayScale, rate), release = decayMul(0.25f, rate);
        const float punchMul = decayMul(0.1f, rate), brightMul = decayMul(0.15f, rate),
                    clickMul = decayMul(0.01f, rate);
        const float attack = 1.0f - decayMul(0.0015f, rate);
        const float tone = std::exp2(clampf(edit.tone, -1.0f, 1.0f));
        const float trim = kitTrim(kit);
        // The heel's bend eases toward where the finger (or a groove) puts
        // it, as fast as a hand presses.
        bend += (bendTarget - bend) * (1.0f - std::exp(-float(n - from) / (0.05f * rate)));
        const float bent = hz * std::exp2(bend / 12.0f);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            envelope(attack, sustain, release);
            punch *= punchMul;
            bright *= brightMul;
            click *= clickMul;
            phase += bent * (1.0f - 0.1f * punch) / rate;
            phase -= std::floor(phase);
            const float s1 = std::sin(kTwoPi * phase), c1 = std::cos(kTwoPi * phase);
            const float s2 = 2.0f * s1 * c1, s3 = s1 * (3.0f - 4.0f * s1 * s1);
            const float b = std::min(1.0f, (0.2f + 0.8f * bright) * tone);
            const float body = s1 + b * (0.35f * s2 + 0.15f * s3);
            const float thump = clickBp.step(rng.bipolar()).band * click * 1.2f;
            const float y = (fastTanh(1.2f * body * env) + thump) * vel * 0.42f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!held && !attacking && env < 1e-4f)
            active = false;
    }

    // The jegogan: the gamelan's deepest bar, struck with a padded mallet.
    // Its fundamental is paired with a twin four hertz off (the ombak), its
    // second mode at 2.76 fades sooner, and it rings on after the note
    // ends, as a bar does until the player damps it with the next.
    void renderBar(float *l, float *r, int from, int n) {
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float ring = decayMul(0.8f * decayScale, rate);
        const float brightMul = decayMul(0.35f, rate), clickMul = decayMul(0.002f, rate);
        const float attack = 1.0f - decayMul(0.003f, rate);
        const float tone = std::exp2(clampf(edit.tone, -1.0f, 1.0f));
        const float trim = kitTrim(kit);
        const float inc1 = hz / rate, inc2 = (hz + 4.0f) / rate, inc3 = std::min(hz * 2.76f / rate, 0.45f);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            envelope(attack, ring, ring);
            bright *= brightMul;
            click *= clickMul;
            phase += inc1;
            phase -= std::floor(phase);
            phase2 += inc2;
            phase2 -= std::floor(phase2);
            phase3 += inc3;
            phase3 -= std::floor(phase3);
            const float bar = std::sin(kTwoPi * phase) + 0.8f * std::sin(kTwoPi * phase2) +
                              0.35f * tone * bright * std::sin(kTwoPi * phase3);
            const float mallet = clickBp.step(rng.bipolar()).band * click * 0.3f;
            const float y = (bar * env + mallet) * vel * 0.17f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!attacking && env < 1e-4f)
            active = false;
    }

    // The balafon: a hardwood bar over a gourd, struck with a rubber
    // mallet. The bar's modes sit at 1 and nearly 4; the gourd under it
    // holds the fundamental; and a membrane over a hole in the gourd (a
    // spider's egg sac once, cigarette paper now) buzzes against it while
    // the note is loud, a rasp riding on the tone: the bar's sound,
    // clipped hard where the membrane slaps, band-passed.
    void renderBalafon(float *l, float *r, int from, int n) {
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float ring = decayMul(0.45f * decayScale, rate);
        const float brightMul = decayMul(0.08f, rate), clickMul = decayMul(0.003f, rate);
        const float attack = 1.0f - decayMul(0.002f, rate);
        const float tone = std::exp2(clampf(edit.tone, -1.0f, 1.0f));
        const float trim = kitTrim(kit);
        const float inc1 = hz / rate, inc3 = std::min(hz * 3.9f / rate, 0.45f);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            envelope(attack, ring, ring);
            bright *= brightMul;
            click *= clickMul;
            phase += inc1;
            phase -= std::floor(phase);
            phase3 += inc3;
            phase3 -= std::floor(phase3);
            const float bar = (std::sin(kTwoPi * phase) + 0.4f * tone * bright * std::sin(kTwoPi * phase3)) * env;
            const float slap = clampf(bar * 5.0f, -1.0f, 1.0f) + 0.3f * std::fabs(bar) * rng.bipolar();
            const float rasp = buzzLp.step(buzz.step(slap).band).low * std::min(1.0f, env * 3.0f);
            const float mallet = clickBp.step(rng.bipolar()).band * click * 0.4f;
            const float y = (bar + rasp * 0.28f * tone + mallet) * vel * 0.3f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!attacking && env < 1e-4f)
            active = false;
    }

    // The chiptune's bass: a pulse channel, band-limited, its level in the
    // chip's sixteen steps, gliding between notes as a tracker's
    // portamento does. Twisted up its duty holds at an eighth, down at a
    // half; in between it sweeps, a step every four frames through an
    // eighth, a quarter, a half and back (the phasing pulse of a tracker's
    // bass), and held past a quarter of a second the note wobbles in the
    // frame-rate vibrato, its pitch changing sixty times a second.
    void renderPulse(float *l, float *r, int from, int n) {
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float sustain = decayMul(1.0f * decayScale, rate), release = decayMul(0.03f, rate);
        const float attack = 1.0f - decayMul(0.0005f, rate);
        const float ease = 1.0f - decayMul(0.001f, rate);
        static const float sweep[4] = {0.125f, 0.25f, 0.5f, 0.25f};
        const float charge = ChipVoice::chargeFactor(rate);
        const int fixed = edit.tone > 0.33f ? 1 : edit.tone < -0.33f ? -1 : 0;
        const float trim = kitTrim(kit);
        float peak = 0;
        for (int i = from; i < n; ++i) {
            const int frame = int((since - float(n - i)) * 60.0f / rate);
            const float duty = fixed > 0 ? 0.125f : fixed < 0 ? 0.5f : sweep[(frame / 4) & 3];
            const float vib = frame > 15 ? 1.0f + 0.012f * std::sin(kTwoPi * float(frame - 15) / 8.0f) : 1.0f;
            const float inc = std::min(hz * vib / rate, 0.45f);
            envelope(attack, sustain, release);
            bright += (ChipVoice::steps(env) - bright) * ease;
            phase += inc;
            phase -= std::floor(phase);
            float t2 = phase - duty;
            if (t2 < 0)
                t2 += 1.0f;
            // Through the chip's DAC and its output capacitor, as the
            // drums' channels are (ChipVoice): the note pops as the
            // channel comes on, and a low note's pulse droops toward the
            // middle between its edges.
            const float w = (phase < duty ? 2.0f : 0.0f) + polyBlep(phase, inc) - polyBlep(t2, inc);
            const float dac = w * bright - ChipVoice::kDacRest;
            const float out = dac - cap;
            cap = dac - out * charge;
            const float y = lp.step(out).low * vel * 0.2f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!held && !attacking && env < 1e-4f && bright < 1e-4f)
            active = false;
    }

    // The upright: a plucked string's fundamental with its second, third
    // and fourth harmonics, which fade faster than it (the pluck's
    // brightness), and a thump of the finger on the string; the note itself
    // dies away like a string even while held, and a groove's lets go
    // sooner.
    void renderUpright(float *l, float *r, int from, int n) {
        const float decayScale = std::exp2(clampf(edit.decay, -3.0f, 3.0f));
        const float sustain = decayMul((kit == Cr78 ? 0.45f : 0.6f) * decayScale, rate);
        const float release = decayMul(0.25f, rate); // the string rings on past a groove's note
        const float brightMul = decayMul(0.09f, rate), thumpMul = decayMul(0.012f, rate);
        const float attack = 1.0f - decayMul(0.0012f, rate);
        const float trim = kitTrim(kit);
        const float tone = std::exp2(edit.tone);
        const float inc = hz / rate;
        float peak = 0;
        for (int i = from; i < n; ++i) {
            if (autoOff > 0) {
                autoOff -= 1.0f;
                if (autoOff <= 0)
                    held = false;
            }
            if (attacking) {
                env += (1.0f - env) * attack;
                attacking = env < 0.995f;
            } else {
                env *= held ? sustain : release;
            }
            punch *= brightMul;
            click *= thumpMul;
            phase += inc;
            phase -= std::floor(phase);
            // The harmonics from the fundamental's sine and cosine, by the
            // double- and triple-angle formulas: two trig calls a sample.
            const float s1 = std::sin(kTwoPi * phase), c1 = std::cos(kTwoPi * phase);
            const float s2 = 2.0f * s1 * c1, c2 = 1.0f - 2.0f * s1 * s1;
            const float s3 = s1 * (3.0f - 4.0f * s1 * s1), s4 = 2.0f * s2 * c2;
            const float b = std::min(1.0f, (0.35f + 0.65f * punch) * tone);
            const float body = s1 + b * (0.55f * s2 + 0.32f * s3 + 0.16f * s4);
            const float thump = clickBp.step(rng.bipolar()).band * click * 1.2f;
            const float y = (fastTanh(1.1f * body * env) + thump) * vel * 0.32f * trim;
            l[i] += y;
            r[i] += y;
            peak = std::max(peak, std::fabs(y));
        }
        level = std::max(level * 0.8f, peak);
        if (!held && !attacking && env < 1e-4f)
            active = false;
    }
};

// A small room of the drums' own, so a snare or a clap gets air without the
// kick and the 808 smearing the studio's big reverb: four delay lines mixed
// by a Householder matrix, damped, after two allpasses that diffuse the
// input. Nothing below 200 Hz goes in.
struct Room {
    Delay lines[4];
    uint32_t len[4] = {};
    float g = 0.5f, mix = 0.25f;
    OnePoleLp damp[4];
    Allpass diff[2];
    Svf hp;
    float rate = 48000;

    void init(float r) {
        rate = r;
        static const float ms[4] = {23.3f, 31.7f, 41.1f, 53.9f};
        for (int i = 0; i < 4; ++i) {
            len[i] = uint32_t(ms[i] * 0.001f * r);
            lines[i].init(size_t(len[i]) + 8);
            damp[i].set(6000.0f, r);
        }
        diff[0].init(uint32_t(0.0047f * r), 0.6f);
        diff[1].init(uint32_t(0.0113f * r), 0.6f);
        hp.set(200.0f, 0.7f, r);
        set(0.45f, 0.25f);
    }
    // seconds of decay to −60 dB; mix is the wet level.
    void set(float t60, float wet) {
        const float meanLen = 0.0375f * rate;
        g = std::pow(10.0f, -3.0f * meanLen / (t60 * rate));
        mix = wet;
    }
    inline void process(float in, float &outL, float &outR) {
        float x = diff[1].step(diff[0].step(hp.step(in).high));
        float y[4];
        for (int i = 0; i < 4; ++i)
            y[i] = damp[i].step(lines[i].tap(len[i]));
        const float sum = 0.5f * (y[0] + y[1] + y[2] + y[3]);
        for (int i = 0; i < 4; ++i)
            lines[i].push((y[i] - sum) * g + x * (i & 1 ? -0.5f : 0.5f));
        outL = (y[0] + y[2]) * mix;
        outR = (y[1] + y[3]) * mix;
    }
};

// --- the engine -------------------------------------------------------------

class Engine : public PartEngine {
public:
    static constexpr int kVoices = 48;
    static constexpr int kPending = 256;
    // A clip from a song file can say any degree: far past where a drum
    // could be tuned, the scale's arithmetic would overflow.
    static constexpr int kMaxDegrees = 48;

    void init(float rate) override {
        m_rate = rate;
        m_room.init(rate);
        m_hang.init(rate);
        applyKit();
    }

    void command(const Command &c, AudioContext &ctx) override {
        const bool replay = c.id >= (1 << 24) || c.id < 0;
        int delay = 0;
        if (c.t > 0)
            delay = int(std::clamp((c.t - ctx.now()) * double(m_rate), 0.0, double(m_rate)));
        switch (c.type) {
        case Hit:
        case Command::On: {
            const int pad = std::clamp(c.i[0], 0, PadCount - 1);
            const int64_t at = int64_t(m_sample) + delay;
            if (replay && (m_muted[(c.id >> 24) & 7] >> pad & 1u))
                return; // this pad's lane is silenced in the loop that sent it
            if (replay) {
                // A clip that was overdubbed while a hand (or a held
                // groove) played the same pad can bring back what was just
                // played live: the live hit wins.
                const int64_t since = at - m_lastLive[pad];
                if (since > -int64_t(0.005f * m_rate) && since < int64_t(0.06f * m_rate))
                    return;
            } else {
                m_lastLive[pad] = at;
            }
            Pending p;
            p.wait = delay;
            p.cmd = c;
            p.replay = replay;
            push(p, ctx);
            break;
        }
        case Command::Off:
        case Command::Move: {
            Pending p;
            p.wait = delay;
            p.cmd = c;
            push(p, ctx);
            break;
        }
        case Command::Param:
            if (c.i[0] == ParamKit) {
                m_kit = std::clamp(c.i[1], 0, KitCount - 1);
                applyKit();
            } else if (c.i[0] == ParamEdit) {
                const int pad = std::clamp(c.i[1], 0, PadCount - 1);
                m_edit[pad] = {c.f[0], c.f[1], c.f[2]};
            } else if (c.i[0] == ParamMute && c.i[1] >= 0 && c.i[1] < kClipSlots) {
                const uint32_t bit = 1u << std::clamp(c.i[2], 0, PadCount - 1);
                m_muted[c.i[1]] = c.i[3] ? m_muted[c.i[1]] | bit : m_muted[c.i[1]] & ~bit;
            }
            break;
        default:
            break;
        }
    }

    void render(float *l, float *r, int n, AudioContext &ctx) override {
        // What falls due in this block starts now, at its sample.
        int keep = 0;
        for (int i = 0; i < m_pendingCount; ++i) {
            Pending &p = m_pending[i];
            if (p.wait < n)
                fire(p, p.wait, ctx);
            else {
                p.wait -= n;
                m_pending[keep++] = p;
            }
        }
        m_pendingCount = keep;

        float room[kBlock] = {};
        for (auto &v : m_voices)
            if (v.active)
                v.render(l, r, room, n);
        m_sub.render(l, r, n, ctx.harmony());
        if (m_hang.active()) {
            float hz[hang::kFields];
            hangTuning(ctx.harmony(), hz);
            m_hang.tune(hz);
            float hl[kBlock] = {}, hr[kBlock] = {};
            m_hang.render(hl, hr, n);
            for (int i = 0; i < n; ++i) {
                l[i] += hl[i];
                r[i] += hr[i];
                room[i] += kHangRoom * (hl[i] + hr[i]);
            }
        }
        // The trim sits the kit where the studio's master expects a part,
        // with room for the bass and the chords: a full-velocity kick peaks
        // around −10 dBFS before the bus glue.
        constexpr float trim = 0.45f;
        for (int i = 0; i < n; ++i) {
            float rl, rr;
            m_room.process(room[i], rl, rr);
            l[i] = (l[i] + rl) * trim;
            r[i] = (r[i] + rr) * trim;
        }

        // The kick ducks the studio: the studio reads pumps before it
        // renders a block, so a kick due in the next block says so now.
        for (int i = 0; i < m_pendingCount; ++i) {
            Pending &p = m_pending[i];
            if (p.pumped || p.wait >= kBlock)
                continue;
            if (const float s = pumpOf(p.cmd); s > 0) {
                ctx.pump(p.wait, s);
                p.pumped = true;
            }
        }

        const float fall = std::exp(-float(n) / (0.14f * m_rate));
        for (int p = 0; p < PadCount; ++p) {
            m_flash[p] *= fall;
            m_flashReplay[p] *= fall;
        }
        m_sample += uint64_t(n);
    }

    void silence(bool hard) override {
        if (hard)
            m_hang.silence();
        else
            m_hang.mute(0.04f);
        for (auto &v : m_voices) {
            if (hard)
                v.active = false;
            else if (v.active)
                v.release(0.03f);
        }
        m_sub.held = false;
        if (hard)
            m_sub.active = false;
        m_pendingCount = 0;
    }

    // data[0..15] how hard each pad was just hit (live), [16..31] the same
    // for hits a clip played, [32] the 808's level, [33] its degree, [34]
    // the kit, [35] voices sounding, [36] how loud the hang rings, [40 + 3 * pad ..] each pad's edit
    // (tune, decay, tone) as the engine has it.
    void publish(float *data) override {
        for (int p = 0; p < PadCount; ++p) {
            data[p] = m_flash[p];
            data[16 + p] = m_flashReplay[p];
        }
        data[32] = m_sub.active ? m_sub.level : 0.0f;
        data[33] = m_sub.degree;
        data[34] = float(m_kit);
        data[35] = float(voices());
        data[36] = m_hang.level();
        for (int p = 0; p < PadCount; ++p) {
            data[40 + 3 * p] = m_edit[p].tune;
            data[41 + 3 * p] = m_edit[p].decay;
            data[42 + 3 * p] = m_edit[p].tone;
        }
    }

    // A hit played along with the loop while layering is a double, not a
    // new part of the groove.
    bool layersDropDoubles() const override { return true; }

    int voices() const override {
        int n = (m_sub.active ? 1 : 0) + (m_hang.active() ? 1 : 0);
        for (auto &v : m_voices)
            n += v.active;
        return n;
    }

    // For tests: start a hit at once, as if a command had come.
    void strike(const Strike &s, const Harmony &h) {
        if ((s.kit >= 0 ? s.kit : m_kit) == Hang) {
            strikeHang(s, h, 0);
            return;
        }
        if (s.pad == Sub) {
            m_sub.start(0, s.degrees, s.vel, std::max(0.05f, s.hold), s.kit >= 0 ? s.kit : m_kit,
                        Edit{0, s.decay, s.tone}, 0, m_rate);
            return;
        }
        Setup su{m_rate, m_kit, &h};
        startVoice(s, su, 0);
    }
    int kit() const { return m_kit; }
    const hang::Body &hangBody() const { return m_hang; }
    hang::Body &hangBody() { return m_hang; }

private:
    struct Pending {
        int wait = 0;
        Command cmd;
        bool replay = false, pumped = false;
    };

    void push(const Pending &p, AudioContext &ctx) {
        if (m_pendingCount >= kPending)
            return;
        Pending q = p;
        // Something due in this very block ducks the studio from here.
        if (q.wait < kBlock)
            if (const float s = pumpOf(q.cmd); s > 0) {
                ctx.pump(q.wait, s);
                q.pumped = true;
            }
        m_pending[m_pendingCount++] = q;
    }

    float pumpOf(const Command &c) const {
        if (c.type != Hit && c.type != Command::On)
            return 0;
        if ((c.i[2] > 0 ? c.i[2] - 1 : m_kit) == Hang)
            return 0; // the hang rings under the mix, it doesn't push it
        if (c.i[0] == Kick)
            return clampf(0.35f + 0.75f * c.f[0], 0.0f, 1.0f);
        if (c.i[0] == Sub)
            return clampf(0.25f + 0.5f * c.f[0], 0.0f, 0.8f);
        return 0;
    }

    void fire(const Pending &p, int offset, AudioContext &ctx) {
        const Command &c = p.cmd;
        const int pad = std::clamp(c.i[0], 0, PadCount - 1);
        switch (c.type) {
        case Hit:
        case Command::On: {
            Strike s;
            s.pad = pad;
            s.degrees = std::clamp(c.i[1], -kMaxDegrees, kMaxDegrees);
            s.vel = c.f[0];
            s.sx = c.f[1];
            s.sy = c.f[2];
            s.semis = c.f[3];
            s.decay = c.f[4];
            s.tone = c.f[6];
            s.kit = c.i[2] > 0 ? std::clamp(c.i[2] - 1, 0, KitCount - 1) : m_kit;
            if (s.kit == Hang) {
                strikeHang(s, ctx.harmony(), offset);
            } else if (pad == Sub) {
                // A hit on the 808 (a groove's) lets go by itself; an On is
                // held until its Off.
                const float hold = c.type == Hit ? std::max(0.05f, c.f[5]) : 0.0f;
                m_sub.start(c.id, s.degrees, s.vel, hold, s.kit, Edit{0, s.decay, s.tone}, offset, m_rate);
            } else {
                Setup su{m_rate, m_kit, &ctx.harmony()};
                startVoice(s, su, offset);
            }
            float &flash = p.replay ? m_flashReplay[pad] : m_flash[pad];
            flash = std::max(flash, 0.3f + 0.7f * clampf(s.vel, 0, 1));
            break;
        }
        case Command::Move:
            if (m_sub.active && m_sub.id == c.id) {
                m_sub.targetDegree = float(std::clamp(c.i[1], -kMaxDegrees, kMaxDegrees));
                m_sub.glide = 0.05f;
                // The bayan's heel, pressed in by the finger moving up.
                m_sub.bendTarget = clampf(c.f[0], 0.0f, 5.0f);
            }
            break;
        case Command::Off:
            m_sub.off(c.id);
            break;
        default:
            break;
        }
    }

    static bool worldKit(int kit) { return kit == Tabla || kit == Gamelan || kit == Djembe; }

    // How many of one drum may ring at once: a kick or a closed hat is one
    // drum, struck again; a crash can bloom over itself a little. Where the
    // hats are other instruments, they ring over themselves like the rest.
    static int polyphony(int pad, int kit) {
        switch (pad) {
        case Kick: return 1;
        case Hat:
        case Open: return worldKit(kit) ? 2 : 1;
        case Crash:
        case Ride: return 3;
        default: return 2;
        }
    }

    void startVoice(const Strike &s, const Setup &su, int offset) {
        // Jungle's kick and snare are chopped from one break: each new hit
        // of either cuts the last one's tail short, the way the next slice
        // of an Amen cuts in, so a busy pattern plays as a chopped break
        // rather than drums ringing over each other.
        const int kit = s.kit >= 0 ? s.kit : su.kit;
        // The angklung and the tanpura sound for as long as they are
        // played: a roll's next step (the same note) keeps the one already
        // sounding going rather than starting another over it.
        for (auto &v : m_voices)
            if (v.active && v.sustains && v.pad == s.pad && v.kit == kit && v.note == s.degrees &&
                v.fadeMul >= 1.0f) {
                v.extend();
                return;
            }
        if (kit == Jungle && (s.pad == Kick || s.pad == Snare))
            for (auto &v : m_voices)
                if (v.active && v.kit == Jungle && (v.pad == Kick || v.pad == Snare))
                    v.release(0.006f);
        // The gamelan's sarons and bonang are damped by the player's other
        // hand as the next note is struck, so a fast kotekan stays two
        // clear lines instead of a wash of ringing bronze.
        if (kit == Gamelan && (s.pad == TomLo || s.pad == TomMid || s.pad == TomHi || s.pad == Perc))
            for (auto &v : m_voices)
                if (v.active && v.kit == Gamelan && v.pad == s.pad)
                    v.release(0.07f);
        // A closed hat chokes the open one; in the kits where those pads
        // are other instruments (the te and the chimta, the kecrek and the
        // kenong, the kese and the karinyan), neither stops the other.
        if (s.pad == Hat && !worldKit(kit))
            for (auto &v : m_voices)
                if (v.active && v.pad == Open && !worldKit(v.kit))
                    v.release(0.012f);
        // Past the pad's polyphony, its oldest voice fades out fast.
        int count = 0;
        Voice *oldest = nullptr;
        for (auto &v : m_voices)
            if (v.active && v.pad == s.pad && v.fadeMul >= 1.0f) {
                ++count;
                if (!oldest || v.serial < oldest->serial)
                    oldest = &v;
            }
        if (oldest && count >= polyphony(s.pad, kit))
            oldest->release(s.pad == Kick ? 0.004f : 0.008f);
        Voice *slot = nullptr;
        for (auto &v : m_voices)
            if (!v.active) {
                slot = &v;
                break;
            }
        if (!slot) {
            // Everything is busy: take the quietest.
            slot = &m_voices[0];
            for (auto &v : m_voices)
                if (v.level < slot->level)
                    slot = &v;
        }
        slot->start(s, su, m_rate, offset, ++m_serial);
        slot->serial = m_serial;
    }

    // A hit on the hang: which field (a degree more is the next one up),
    // where on it, how hard, the finger's hardness, and how long the whole
    // instrument rings.
    void strikeHang(const Strike &s, const Harmony &h, int offset) {
        float hz[hang::kFields];
        hangTuning(h, hz);
        m_hang.tune(hz);
        m_hang.setDecay(s.decay);
        int field = hangField(s.pad);
        if (field != hang::kGu)
            field = std::clamp(field + s.degrees, 0, hang::kFields - 1);
        // The page sends where in the field it landed, ±kSpotReach of its
        // rim across 0..1.
        const float u = (clampf(s.sx, 0, 1) - 0.5f) * 2.0f * hang::kSpotReach;
        const float w = (clampf(s.sy, 0, 1) - 0.5f) * 2.0f * hang::kSpotReach;
        m_hang.strike(field, s.vel, u, w, clampf(s.tone, -1, 1), offset);
    }

    void applyKit() {
        // Boom bap and the CR-78 are dry; jungle's breaks and the jazz kit
        // keep their room.
        static const float t60[KitCount] = {0.55f, 0.45f, 0.4f, 0.9f, 0.5f, 0.35f, 0.7f, 0.8f, 0.4f, 0.75f, 0.6f, 0.9f, 1.3f, 0.25f, 0.6f, 1.1f};
        static const float wet[KitCount] = {0.22f, 0.18f, 0.14f, 0.32f, 0.22f, 0.14f, 0.28f, 0.3f, 0.12f, 0.26f, 0.22f, 0.26f, 0.3f, 0.06f, 0.18f, 0.2f};
        m_room.set(t60[m_kit], wet[m_kit]);
    }

    float m_rate = 48000;
    int m_kit = House;
    Edit m_edit[PadCount];
    Voice m_voices[kVoices];
    Sub808 m_sub;
    hang::Body m_hang;
    // How much of the hang the room hears: a hang is played in one.
    static constexpr float kHangRoom = 0.35f;
    Room m_room;
    Pending m_pending[kPending];
    int m_pendingCount = 0;
    uint64_t m_sample = 0;
    int64_t m_lastLive[PadCount] = {INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2,
                                     INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2,
                                     INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2,
                                     INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2, INT64_MIN / 2};
    uint32_t m_serial = 0;
    uint32_t m_muted[kClipSlots] = {}; // per clip slot, the pads silenced in its loop
    float m_flash[PadCount] = {}, m_flashReplay[PadCount] = {};
};

}
