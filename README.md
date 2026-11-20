# tenfold

![tenfold's eight pages mid-performance: drums, bass, chords, lead, arp, atmosphere, effects and the arrange page](docs/screenshot.png)

tenfold turns the trackpad into an electronic music studio you play with all
ten fingers. Every part of a track has its own page and its own instrument,
designed for what that part needs: a pad grid with note-repeat for drums, a
303-style bass with slides and a filter you ride, chords that voice-lead
themselves, an expressive lead with glides, vibrato and harmonies, an
arpeggiator, a bed of pads and choirs, a transitions desk of risers and
drops, and an arrange page that turns your loops into a track. Everything
plays in one key and on one clock, and every kick pumps the mix.

All of it is synthesized from scratch, with no samples, at 48 kHz in
128-frame PipeWire callbacks (2.7 ms).

## Playing

Click **Take pad** to hand the trackpad to tenfold (Esc gives it back), then
pick a page with its number:

| Key | Page | What the fingers do |
|---|---|---|
| 1 | Drums | 16 pads in five kits. Tap to hit, hold to roll (slide up for faster), land several fingers for a groove, click to keep it. A thumb on the bottom strip (or B / N) is note-repeat. |
| 2 | Bass | Notes of the key across the pad; height opens the sound, a second finger slides. The rail has octave, roll, wobble, filter, grit and bend. Eight sounds from sub to acid to 808. |
| 3 | Chords | Columns are the key's chords, rows their colour (power, sus, triad, add9, seventh). Hold to play, slide to recolour; the top rail gates them in rhythm. The others follow the chord you play. |
| 4 | Lead | A two-octave neck: glide, wiggle for vibrato, twist to bend, a second finger for legato, the left column for harmonies. Eight sounds from anthem to hoover. |
| 5 | Arp | Hold notes on the left; the pads on the right set rate, rhythm, range, tone, groove and echo. Click to latch. |
| 6 | Atmos | Five layers (drone, pad, choir, glass, air), one per finger; click to latch, F freezes, S swells. Scenes on Q–I. |
| 7 | FX | Risers that all peak on the next phrase line, impacts, sub drops, sweeps, throws. Click to drop now. |
| 0 | Arrange | Every part's clips, scenes on the bar, a ten-finger mixer, a tension fader for builds and drops. I writes a whole track from your loops. |

`?` shows the guide for the page you're on. Letter keys play most pages too.

**Loops.** Play something, then press **Space**. With nothing else playing,
press it where your phrase would start again: that loop sets the tempo. Over
something playing, Space finds the bars you repeated and loops them from the
bar line, leaving out whatever you tried before. F1–F8 launch a page's eight
slots and Backspace stops it.

**Layers.** Shift+Space puts what you just played on top of the part's playing
loop, as one loop. On Arrange, pick two loops of a part and press M to layer
them, Shift+M to chain them.

**Changed your mind?** Ctrl+Z takes back the newest loop on any page, and
what was playing before comes back. Ctrl+N twice starts over.

**Everywhere:** Ctrl+↑/↓ tempo, Ctrl+←/→ key, Ctrl+[ ] mode, Ctrl+M
metronome, Ctrl+R record to `~/Music/tenfold`, Ctrl+Q quit.

## Without a trackpad

Every page can be driven from a script, which is how it is tested:

```bash
tenfold render docs/parts/drums/house_hands.tf out.wav --json
tenfold screenshot shot.png docs/parts/lead/anthem.tf --page 4 --at 10
tenfold bench
```

`src/offline.h` documents the script language (`page`, `down`, `move`,
`glide`, `up`, `tap`, `click`, `key`, `tempo`), and `docs/parts/` has demos
for every page.

## How it's built

`docs/PARTS.md` describes the studio: one clock, one key, a mixer channel
and eight clip slots per part, and the contract a page implements. Each page
is one file in `src/`.

```bash
bin/build      # qmake6 + make into build/tenfold
bin/test       # every page's tests and the studio's
bin/install    # package and install, so it shows up in the launcher
```

Needs Qt 6 (base, declarative), PipeWire and polkit.
