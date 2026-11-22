# tenfold

![tenfold's eight pages mid-performance: drums, bass, chords, lead, arp, atmosphere, effects and the Song page](docs/screenshot.png)

tenfold turns the trackpad into an electronic music studio you play with all
ten fingers. Every part of a track has its own page and its own instrument,
designed for what that part needs: a pad grid with note-repeat for drums, a
303-style bass with slides and a filter you ride, chords that voice-lead
themselves, an expressive lead with glides, vibrato and harmonies, an
arpeggiator, a bed of pads and choirs, a transitions desk of risers and
drops, and a Song page that turns your loops into a track. Everything
plays in one key and on one clock, and every kick pumps the mix.

All of it is synthesized from scratch, with no samples, at 48 kHz in
128-frame PipeWire callbacks (2.7 ms).

## Install

On any Linux with PipeWire, x86_64 or arm64:

```bash
curl -fsSL https://maxberggren.github.io/tenfold/install.sh | sh
```

The script picks the way that suits the system, and checks every download
against its checksum:

- **Arch** and its kin (Omarchy, Arch Linux ARM on an Asahi Mac): the
  release's pacman package.
- **NixOS**: the flake in this repository, through `nix profile`. In a
  NixOS configuration, add `packages.${system}.default` from it instead
  (`flake.nix` shows how).
- **Everything else** (Ubuntu 23.04+, Debian 12+, Fedora, openSUSE, Mint,
  Pop!_OS…; on arm64 Ubuntu 24.04+ or Debian 13+): a bundle with its own
  Qt in `~/.local/lib/tenfold`, with a launcher entry. If PipeWire's library or polkit is missing, the system's
  package manager adds it.

`sh -s -- --uninstall` after the same command takes it away again.
[Read the script](site/install.sh) first if you like, or take a package
from [the releases](https://github.com/maxberggren/tenfold/releases).

Reading the trackpad's raw fingers takes root, so the first time tenfold
takes the pad in a run it asks for your password (through polkit) and reads
the pad with a small helper. To skip the password, let the person at the
computer read touchpads (only touchpads, not keyboards) with the udev rule
in `extras/`:

```bash
sudo install -Dm644 extras/70-tenfold.rules /etc/udev/rules.d/70-tenfold.rules
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=input --action=change
```

### macOS

[Download tenfold-macos.dmg](https://github.com/maxberggren/tenfold/releases/latest/download/tenfold-macos.dmg),
open it and drag tenfold onto Applications. It runs on Apple silicon and
Intel Macs with macOS 12 or later.

- Taking the pad needs no permission: the pointer stops and the trackpad's
  clicks, scrolls and pinches go to the music until Esc gives it back.
- The Mac's own three- and four-finger swipes still switch spaces. Turn them
  off under System Settings › Trackpad › More Gestures while you play, or
  allow tenfold under Privacy & Security › Accessibility and it holds them
  back while the pad is taken.
- The Vocal page asks once for the microphone.
- tenfold's Ctrl shortcuts are ⌘ on the Mac, and the buttons say so.

Windows needs a port of its own (the trackpad and the audio are read in its
own way), so it isn't there yet; see [How it's built](#how-its-built) for
where a port goes.

## Playing

Click **Take pad** to hand the trackpad to tenfold (Esc gives it back), then
pick a page with its number:

| Key | Page | What the fingers do |
|---|---|---|
| 1 | Drums | 16 pads in ten kits, from house and trap to boom bap, jungle, an 80s Linn, a CR-78 and jazz brushes. Tap to hit, hold to roll (slide up for faster), land several fingers for a groove, click to keep it. A thumb on the bottom strip (or B / N) is note-repeat. |
| 2 | Bass | Notes of the key across the pad; height opens the sound, a second finger slides. The rail has octave, roll, wobble, filter, grit and bend. Ten sounds from sub to acid to 808, a Moog-style ladder bass and an upright. |
| 3 | Chords | Columns are the key's chords, rows their colour (power, sus, triad, add9, seventh). Hold to play, slide to recolour; the top rail gates them in rhythm. The others follow the chord you play. Ten sounds from pad, trance and organ to Rhodes keys, a string machine, dub-techno stabs and funk guitar. |
| 4 | Lead | A two-octave neck: glide, wiggle for vibrato, twist to bend, a second finger for legato, the left column for harmonies. Ten sounds from anthem to hoover, a vintage mono synth and a breathy flute. |
| 5 | Arp | Hold notes on the left; the pads on the right set rate, rhythm, range, tone, groove and echo. Click to latch. Ten sounds from trance pluck and acid to marimba, harp, Rhodes and organ. |
| 6 | Atmos | Five layers (drone, pad, choir, glass, air), one per finger; click to latch, F freezes, S swells. Ten scenes on Q–P, from dawn and cathedral to neon (synthwave) and dub (dub techno); L picks the style hand-laid layers play in: classic, neon, dub or tape (lo-fi). |
| 7 | FX | Risers that all peak on the next phrase line, impacts, sub drops, sweeps, throws, in ten sets from EDM and analog to dub, lo-fi, trap, cinema and ambient. Click to drop now. |
| 8 | Vocal | Your microphone, heard at once in the headphones. Each finger in the middle is your voice again as a harmony in the key (−8ve to +8ve, the note named in each lane; height the throat, your own on the middle line); touch a word on the tape and it plays again; the left edge is a filter, the right freeze and stutter, the top echo, reverb, radio and crush; the bottom edge is for resting a thumb; click for tape stop. Hard tune, prism, choir, vocoder and robot on ← →. Space loops your last phrase as audio, cut in the breaths; Shift+Space layers it. |
| 0 | Arrange | Every part's clips, scenes on the bar, a ten-finger mixer, a tension fader for builds and drops. I writes a whole track from your loops. |
| 9 | Visual | Everything playing, drawn as a raymarched world you fly through: a city whose towers are the parts, a tunnel, gem crystals, a Menger sponge, a gyroid lattice. One finger steers, two roll and scale the world, three travel between worlds, four turn space inside out, five dial endless variations, two hands do it all at once. See [The visualizer](#the-visualizer). |

`?` shows the guide for the page you're on. Letter keys play most pages too.

**New here?** Ctrl+T (or the Tutorial button) clears the studio and walks you from silence to a
finished, recorded track: a beat, chords, a bassline that follows them, a
melody, an arp, an atmosphere, a build and its drop looped for the track,
your own voice with a choir of it and a sung loop (V lends a demo singer if
there's no microphone), then the mix, a song of your own and the whole written track, heard through
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

**The Song page (0).** A song is a row of sections (intro, groove, build,
drop, breakdown, outro) in the band under the lanes. I writes one from your
loops and plays it; Space records one as you play here (clips, mutes, the
moves, the fader), and while a song plays Space punches in, replacing the bars
you play over. P plays it from the top, Ctrl+P pauses. The moves work live on
whatever plays: B builds to the end of the phrase with your FX loop leading
in, Return drops (everything back on the ONE; the payoff of a build or a
breakdown), N breaks down, X echoes out. Pick a section (← → while stopped,
or tap it in the band) and the lanes show what plays in it: tap a loop to
play it there or take the part out, D doubles the section, Delete removes it,
Shift+O takes each edit back.

**Editing loops on the Song page.** Hold a clip (a second finger, or the mouse
button) or right-click it to pick it; right-click opens a menu of what can be
done to it, and the same actions show as buttons under the song while
something is picked. D doubles a loop into a free slot, Shift+← → (or [ ])
move it, Delete clears it. Pick a second loop of the same part (hold or
right-click it too) and M layers it into the first, Shift+M chains it after.
Ctrl+Z takes back each edit.

**The same keys on every page.** Letters are each page's own (pads, notes,
chords, scenes); the rest means the same everywhere, and every one of them
also has a button in the band under the pad, so the mouse alone can play:

| Key | Does |
|---|---|
| ← → | the sound: kit, preset, scene, FX set |
| ↑ ↓ | octave or register (Drums: the loop longer or shorter, any time; FX: riser length) |
| Shift ← → | the rhythm: swing, gate, arp rate, FX drop style |
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
metronome, Ctrl+P pause and play (every loop stops at once; again, the same
loops play on together from a downbeat, and a song on the Song page from
the bar it stopped in), Ctrl+R record to `~/Music/tenfold` (Ctrl+Shift+R, or
Shift+click Record: with the Visual page's picture, as an `.mp4`), Ctrl+Q quit.

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

## The visualizer

Page 9 turns everything playing into a picture: one hue for the chord at a
low light, and each part's own mark in its page's colour. The kick shoves
the world in and flares its edges with a shockwave, the snare breaks space
into slabs, hats glint and streak down the city's road, the bass lifts the
skyline and swells space, the lead is a ribbon of light at its pitch, arp
notes are orbs that light the walls as you pass, the atmosphere is the fog,
a build whitens and shakes it and a drop flashes into the next world, the
voice is a filament down the middle. With the drums alone it is near black.

| Key | Does |
|---|---|
| A S D F G | the worlds: city, tunnel, crystal, sponge, lattice (← → step, Shift: a little way) |
| ↑ ↓ · [ ] | more or less folded · roll |
| K · W · T · Z · H · + − | kaleidoscope · warp · trails · zoom · colours · speed |
| Space · Backspace | another variation · the world back home |
| P · M · Q · C | autopilot (wanders when left alone; a drop moves it on) · the fingers drawn · quality · CRT glass |
| Return · Shift Return | the picture in a window of its own · that window full screen (on the other screen, if there is one) |

In the second window F11 or a double-click goes full screen and Esc comes
back. The trackpad keeps playing the page the main window shows, whichever
tenfold window has the focus, and keys typed in the second window go to
that page too: put the picture on a projector, play on pages 1–8, and come
to page 9 when the hands should shape the picture. What they shape stays
when they lift.

Ctrl+Shift+R records the picture with the sound: it is drawn a second time,
offscreen at 1280×720 (`TENFOLD_VIDEO_SIZE=1920x1080` for more), and piped
to `ffmpeg` at 30 frames a second, each frame placed by the WAV's own sample
count so the two cannot drift; at stop the two are muxed into an `.mp4`
beside the `.wav`. Without `ffmpeg` (`TENFOLD_FFMPEG` names one) the window
says so and records the sound alone. The audio thread never waits on any of
it.

Offline, `tenfold visualize` draws a script as `render` plays it, to a video
with its sound or to a still:

```bash
tenfold visualize docs/visual/live-set-visual.tf demo.mp4 --from 98 --to 143.5
tenfold visualize docs/parts/risers/build-and-drop.tf drop.png --at 32.75
```

The shaders are `src/visual/*.frag`, baked into the `.qsb` files beside them
by `bin/shaders` (Qt's `qsb` from qt6-shadertools when it is there, else
`tools/qsbmini`, which bakes OpenGL and Vulkan but not Metal; the macOS
bundle is baked with `qsb`).

## Without a trackpad

Every page can be driven from a script, which is how it is tested:

```bash
tenfold render docs/parts/drums/house_hands.tf out.wav --json
tenfold screenshot shot.png docs/parts/lead/anthem.tf --page 4 --at 10
tenfold bench
tenfold check song.tenfold
tenfold visualize docs/visual/live-set-visual.tf out.mp4 --from 98 --to 120
```

`src/offline.h` documents the script language (`page`, `down`, `move`,
`glide`, `up`, `tap`, `click`, `key`, `tempo`), and `docs/parts/` has demos
for every page.

## How it's built

`docs/PARTS.md` describes the studio: one clock, one key, a mixer channel
and eight clip slots per part, and the contract a page implements. Each page
is one file in `src/`.

Everything tenfold needs from the system is in `src/platform.h`: the
trackpad read raw, the sound card, the file dialogs and the clock. The
studio, the pages and the window only see that header. Linux implements it
in `src/linux/` (evdev and a pkexec helper for the pad, PipeWire for sound,
xdg-desktop-portal for dialogs), macOS in `src/macos/` (MultitouchSupport
for the pad, CoreAudio, AppKit's panels), and a Windows port is a folder
beside them plus a scope in `tenfold.pro`. `src/contacts.h` turns any
pad's raw contacts into the size and pressure the pages play, learned per
player. The tests never touch the platform, so they run unchanged on a
port.

```bash
bin/build      # qmake6 + make into build/tenfold (TENFOLD_PORTABLE=1: for any CPU)
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

Needs Qt 6.8 or newer (base, declarative, wayland), PipeWire and polkit.
Noto Sans Mono is compiled in (`src/fonts`, under the OFL).

Every push to master builds and tests tenfold for x86_64 and arm64
(`.github/workflows/build.yml`): the pacman package on Arch, the bundle with
Qt's own build on the oldest system it supports (`bin/bundle` makes it),
which is then
installed and run on Ubuntu, Debian, Fedora and openSUSE, the Nix
flake, and on macOS the tests and `tenfold.app` in a disk image
(`bin/bundle-macos` makes it). The Mac app is signed with a Developer ID
and notarized when the repository has the keys; `bin/set-signing-secrets`
hands them to GitHub. A version tag publishes a release with the packages, the bundles and
their checksums:

```bash
git tag v0.2.0 && git push origin v0.2.0
```

The install page and its script live in `site/` and are published to
GitHub Pages by `.github/workflows/site.yml`.
