# tenfold

![tenfold mid-performance: ten fingers on the pad, each turning its own knob, three loops spinning and a landscape of spectra underneath](docs/screenshot.png)

tenfold turns the trackpad into an electronic instrument you play with all
ten fingers: drums, bass, chords, lead and arpeggios, in key and in time.
Every finger that lands plays at once. Every finger that stays sustains its
note and takes a knob, in the order it arrived. A rhythm you tap becomes a
loop the moment you land on the next beat and hold, and the first loop sets
the tempo. Every kick pumps the rest of the mix.

All of it is synthesized from scratch, with no samples: band-limited saws in
up to seven-voice unison through a resonant ladder filter, and a drum kit
built from sines, noise and square-wave metal. It runs at 48 kHz in
128-frame PipeWire callbacks (2.7 ms).

## Playing

Space hands the trackpad to tenfold: the pointer stops and the pad becomes the
instrument. Esc gives it back. tenfold also gives it back whenever its window
loses focus. Reading the pad needs root, so the first Space asks for your
password once, the same way [padtune](https://github.com/maxberggren/padtune)
does.

The pad has five rows, each an instrument:

| Row (bottom to top) | Tap | Hold |
| --- | --- | --- |
| **Drums** | The column picks the piece: kick, snare, clap, hat, open hat, perc. The kick is tuned to the key. | Rolls the piece on the sixteenths; press harder for thirty-seconds. |
| **Bass** | A short bass note | A sustained bass note |
| **Chords** | A stab: the seventh chord on that degree of the scale | A pad; slide sideways to walk the progression |
| **Lead** | A pluck | A sustained supersaw lead |
| **Arp** | A short burst | The chord on that degree, arpeggiated in time |

Above the drum row, left to right is the scale (A minor by default), one note
per column over three octaves, so every finger lands on a note in key. A
finger that's still plays exactly in tune; a quick slide glides.

### The grammar

| Gesture | What happens |
| --- | --- |
| **Hold** | The finger's note keeps sounding and the finger takes the next free knob (below). Slide it up or down for the knob's first value, twist it for the second. Knobs stay where you leave them. |
| **Tap a rhythm, then land on the next beat and stay** | The taps become a loop, with each note's length. The hold has to land on the beat (one interval after the last tap, or two for an even pulse). The first loop sets the tempo, rounded to a whole BPM. Loops pull most of the way onto the sixteenth grid and swing as much as the Glide knob says. |
| **While holding a loop** | Slide sideways to transpose it in the scale, twist right for rolls, left to thin it out, press harder to accent. Its height sets how wet it is. Taps with other fingers are added to it; held fingers are recorded as notes and knob sweeps that replay every bar. |
| **Press** (click the pad) | Latches what you're holding: held notes keep sounding with the sound they had, a held loop is pinned. Fingers that latched a chord keep their knobs while they stay down. |
| **Tap once and hold on a latched chord** | Picks up that layer (its lettered box): slide sideways to move it through the scale, up or down for its filter, twist for width, press to swell it. |
| **Swipe** | Three fingers flicked sideways move the key a fifth; up or down, the octave. Four fingers sideways change the scale family. |
| **Palm** | Damps everything. Palm and press clears the landscape. |

### The ten knobs

| # | Knob | Slide | Twist |
| --- | --- | --- | --- |
| 1 | Filter | cutoff | resonance |
| 2 | Env | filter envelope amount | decay: pluck ↔ pad |
| 3 | Width | unison detune and stereo spread | sub oscillator |
| 4 | Drive | drive (4× oversampled) | wavefold |
| 5 | Pump | sidechain depth | arpeggio rate |
| 6 | Echo | tape echo amount | time, synced |
| 7 | Space | reverb | shimmer |
| 8 | Glide | portamento | swing |
| 9 | Wobble | tempo-synced filter wobble depth | rate |
| 10 | Gravity | pull to the scale | mode, from Lydian to Locrian |

### Keys

| Key | Action |
| --- | --- |
| Space | Take the trackpad / give it back |
| Esc | Give the trackpad back |
| ← → | Key down or up a fifth |
| ↑ ↓ | Scale family: diatonic, pentatonic, harmonic minor, In, overtones |
| [ ] | Octave down or up |
| J | Just intonation (default) or equal temperament |
| Backspace | Dissolve the newest layer |
| Shift+Backspace | Dissolve everything |
| 0 | Knobs back to where they started |
| R | Record to `~/Music/tenfold/` (24-bit WAV) |
| F | Full screen |
| ? | All of this, in the window |
| Q | Quit |

## The sound

- **Synth voices** are band-limited (PolyBLEP) saws, two voices for bass,
  seven for leads, five per chord tone, detuned and spread across the
  stereo field, with a square sub an octave down. They run through a
  four-pole zero-delay-feedback ladder filter with a soft-clipped input, its
  own envelope, and a wobble locked to the tempo.
- **Chords** are the diatonic sevenths of the current scale, each voiced as
  close as it can be to the last, so progressions move by small steps.
- **Drums:** the kick is a sine falling four to one onto the key's root
  (45–65 Hz), with a 2 ms click; snare is two tuned sines and band-passed
  noise; clap is three bursts and a tail; hats are the classic six-square
  metal cluster under bright noise, closed choking open; perc is a tuned tom
  with a rim click.
- **The mix:** every kick ducks the bass, the music and the effect returns
  (the pump). Bass stays dry and mono; each drum has its own send; the kick
  sends nothing. Drive is 4× oversampled with antiderivative anti-aliasing.
  On the master: bass summed to mono under 120 Hz and compressed under
  150 Hz, a glue compressor, and a lookahead limiter at −1 dBFS.
- **Timing** comes from the kernel: the helper stamps every touch on the
  monotonic clock the audio engine runs on, and loop notes start at their
  exact sample inside each audio block.

## Without a trackpad

Every part of the instrument can be driven from a script, which is how it is
tested and how the demo is made:

```bash
tenfold demo demo.wav --json        # render the built-in demo track
tenfold render my.tf out.wav        # render your own performance
tenfold bench                       # dense loops, latched chords, ten held notes, every knob up
tenfold screenshot shot.png --at 50 # the window, mid-performance
```

`tenfold demo-script` prints the demo's script, and `src/offline.h` documents
the script language (`down`, `move`, `glide`, `up`, `tap`, `click`, `key`).

## Building

```bash
bin/build      # qmake6 + make into build/tenfold
bin/test       # the gesture grammar, the tuning, and the engine's stability
bin/install    # package and install, so it shows up in the launcher
```

Needs Qt 6 (base, declarative), PipeWire and polkit. Built with `-O3
-ffast-math` for the CPU it's built on. On an M2, the worst-case bench uses
about 4% of each audio callback on average and under 20% at its peak.
