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
| 1 | Drums | 16 pads in ten kits, from house and trap to boom bap, jungle, an 80s Linn, a CR-78 and jazz brushes. Tap to hit, hold to roll (slide up for faster), land several fingers for a groove, click to keep it. A thumb on the bottom strip (or B / N) is note-repeat. |
| 2 | Bass | Notes of the key across the pad; height opens the sound, a second finger slides. The rail has octave, roll, wobble, filter, grit and bend. Ten sounds from sub to acid to 808, a Moog-style ladder bass and an upright. |
| 3 | Chords | Columns are the key's chords, rows their colour (power, sus, triad, add9, seventh). Hold to play, slide to recolour; the top rail gates them in rhythm. The others follow the chord you play. |
| 4 | Lead | A two-octave neck: glide, wiggle for vibrato, twist to bend, a second finger for legato, the left column for harmonies. Ten sounds from anthem to hoover, a vintage mono synth and a breathy flute. |
| 5 | Arp | Hold notes on the left; the pads on the right set rate, rhythm, range, tone, groove and echo. Click to latch. |
| 6 | Atmos | Five layers (drone, pad, choir, glass, air), one per finger; click to latch, F freezes, S swells. Scenes on Q–I. |
| 7 | FX | Risers that all peak on the next phrase line, impacts, sub drops, sweeps, throws. Click to drop now. |
| 8 | Vocal | Your microphone, heard at once in the headphones. Each finger in the middle is your voice again as a harmony in the key (−8ve to +8ve, height the throat, giant to elf); touch a word on the tape and it plays again; the edges are filter, freeze and stutter, echo and reverb throws, radio and crush; click for tape stop. Hard tune, prism, choir, vocoder and robot on ← →. Space loops your last phrase as audio, cut in the breaths; Shift+Space layers it. |
| 0 | Arrange | Every part's clips, scenes on the bar, a ten-finger mixer, a tension fader for builds and drops. I writes a whole track from your loops. |

`?` shows the guide for the page you're on. Letter keys play most pages too.

**New here?** Ctrl+T (or the Tutorial button) clears the studio and walks you from silence to a
finished, recorded track: a beat, chords, a bassline that follows them, a
melody, an arp, an atmosphere, a build and its drop looped for the track,
then the mix, a song of your own and the whole written track, heard through
to its echo and saved, every feature met when the song needs it. Each step lights
where the fingers go and moves on by itself once you've done it;
Ctrl+Return skips ahead, Ctrl+Shift+Return goes back, Esc leaves.

**Loops.** Play something, then press **Space**. With nothing else playing,
press it where your phrase would start again: that loop sets the tempo. Over
something playing, Space finds the bars you repeated and loops them from the
bar line, leaving out whatever you tried before. F1–F8 launch a page's eight
slots and Backspace stops it. Right after Space, ↑ and ↓ make the loop longer or shorter (1, 2,
4, 8 or 16 bars, ending where it ended); once the arrows rest for three
seconds it stays. On Drums, which have no octave, ↑ and ↓ do it at any
time, like the Loop shorter / Loop longer buttons. With nothing else playing, a loop sets the tempo only when
you played a pattern or pressed Space in its flow; after a wait, the tempo
stays.

**Layers.** Over a page's own playing loop, Space puts what you just played on
top of it, as one loop. Shift+Space makes a new loop in its place instead.
The Vocal page has it the other way round: Space makes a new vocal loop,
Shift+Space layers the singing onto the one playing.
A layer is as long as fits the loop (one, two, four… bars over a longer one,
never longer than it; on Atmos, the whole loop), and lands without stopping
the loop. Each layer plays on its own: its own voice on Bass and Lead, its
own chords on Chords (a different chord steps the other back), its own
pattern on Arp, so layers sound together instead of cutting each other off.

**Editing loops on Arrange.** Hold a clip (a second finger, or the mouse
button) or right-click it to pick it; right-click opens a menu of what can be
done to it, and the same actions show as buttons under the song while
something is picked. D doubles a loop into a free slot, Shift+← → (or [ ])
move it, Delete clears it. Pick a second loop of the same part (hold or
right-click it too) and M layers it into the first, Shift+M chains it after.
Ctrl+Z takes back each edit. A song section is picked the same way: D doubles
it, M joins it with the next, Shift+← → move it, Delete removes it.

**The same keys on every page.** Letters are each page's own (pads, notes,
chords, scenes); the rest means the same everywhere, and every one of them
also has a button in the band under the pad, so the mouse alone can play:

| Key | Does |
|---|---|
| ← → | the sound: kit, preset, scene, drop style |
| ↑ ↓ | octave or register (Drums: the loop longer or shorter, any time; FX: riser length) |
| Shift ← → | the rhythm: swing, gate, arp rate |
| - + | tone: darker, brighter |
| Return | hold or latch (FX: drop; Drums: keep the roll) |
| Shift Return | overdub |
| § (the key left of 1) | follow the Chords page |
| Delete | let go, or sounds back |
| F1–F8 · Shift F1–F8 · Backspace | launch a slot · clear it · stop |

Older keys ([ ] \ = and each page's letters) still work too. The clip
slots under the pad launch or stop with a click.

**Changed your mind?** Ctrl+Z takes back the newest change to the page's
loops (a loop, a layer, a pass played into one), up to 16 back, and what
was playing before comes back. On the Song page it takes back the newest
change anywhere. Ctrl+Shift+Z brings back what Ctrl+Z took back. Ctrl+N twice starts over: every loop
cleared, the mix set back, nothing left muted.

**Everywhere:** Ctrl+↑/↓ tempo, Ctrl+←/→ key, Ctrl+[ ] mode, Ctrl+M
metronome, Ctrl+R record to `~/Music/tenfold`, Ctrl+Q quit.

**Songs.** Ctrl+S saves the song to a `.tenfold` file to pick up where you
left off: tempo, key and mode, every part's loops (layers too, and the Vocal
page's recorded singing), which ones play, the mix, each page's sounds and settings, and the Song page's song and
scenes. Ctrl+Shift+S saves it under a new name, Ctrl+O opens one (the
buttons beside the pad do the same). Quitting keeps the song in
`~/.local/state/tenfold/last.tenfold`, and the next start offers it back:
Ctrl+Shift+O. A song file is JSON you can read; `tenfold check song.tenfold`
says whether one opens and what is in it.

| Key | Does |
|---|---|
| Ctrl S · Ctrl Shift S | save the song · save it as |
| Ctrl O | open a song |
| Ctrl Shift O | the song as it was when tenfold last quit |

## Without a trackpad

Every page can be driven from a script, which is how it is tested:

```bash
tenfold render docs/parts/drums/house_hands.tf out.wav --json
tenfold screenshot shot.png docs/parts/lead/anthem.tf --page 4 --at 10
tenfold bench
tenfold check song.tenfold
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

`bin/test` runs the tests in parallel on every core, the slowest first, and
shows only what failed. One suite or one test runs on its own, with QtTest's
output:

```bash
bin/test DrumsTests                                     # one suite
bin/test DrumsTests::kicksPlayedAlongArentLayeredTwice  # one test
bin/test -h                                             # more
```

Needs Qt 6 (base, declarative), PipeWire and polkit.
