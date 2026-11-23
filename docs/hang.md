# The hang

The drums page's last kit is not a kit. The whole pad becomes a hang: one
instrument of steel and air, simulated as it is built (`src/hang.h`) and
drawn as it is played (`src/drums.cpp`, `paintHang`).

- The ding is in the middle, with eight tone fields around it.
- The lowest field is nearest the player, and the notes go up the scale
  zig-zagging right and left round the ding.
- Past the shell's rim, round the corners, the hand is under the instrument
  at the gu.

## Playing it

- **Where you tap:** tap a field, or the ding, wherever on it you like.
  - The middle of a field is on the nodal lines of its octave and its
    fifth, so it sounds nearly pure.
  - Off the middle along the field's long axis (the one pointing at the
    ding) wakes the octave. Off it across that axis wakes the fifth.
  - The steel between the fields (the shoulder) gives a dry knock with the
    nearest fields ringing.
- **How hard:** pressure sets how fast the fingertip arrives. Harder is
  louder, brighter and shorter in contact. A hard strike also starts a
  little flat and blooms into its octave, then its fifth.
- **The gu:** a tap past the rim is a palm slapped across the gu. You get
  the cavity's low boom and the gu lip's small bell.
- **The rest of the page:** the drums page's rolls, grooves and loops work
  as on any kit, and a groove on the hang is a melody across the fields.
  - A field's "tune" (a click and a sideways drag) moves it to another
    field's note.
  - Twist sets how long the whole instrument rings.
- **Tuning:** the hang follows the key. The ding is on the root near D3,
  and the fields run from the fifth up eight degrees of the scale. In D
  minor that is the classic hang: D3 | A3 Bb3 C4 D4 E4 F4 G4 A4.

## The model

Everything is in SI units, integrated sample by sample. Each mode is a
damped oscillator, `u' = d·u − c·x + f·dt²/m` and `x' = x + u'`, with `d`
and `c` set so its poles are exactly the mode's frequency and decay.
Everything that couples is a force between the oscillators.

| Part | What it is | Set from |
|---|---|---|
| Shell | Steel 0.93 mm thick, 53 cm across. Each mode's modal mass comes from the steel and the field's area: 0.297 of the plate for the dome (∫φ²), ¼ for lobed modes. | Rohner & Schärer 2007 |
| Fields | Ellipses with the long axis pointing at the ding. Width is 0.76 × length, and length goes as f^−2/3 (steelpan law), with A3 at 11 cm. | steelpan scaling; aspect gives fifth:octave = 3:2 |
| Partials | Fundamental, octave and compound fifth at 1 : 2 : 3, with this instrument's own mistunings (f1 ±2 c, octave ±5 c, fifth ±7 c). An overtone that shares a frequency with another field's fundamental is placed 14 c from it, never more than 10 c from its own note. Three untuned modes sit at 7.5–9 × f1 and radiate as dipoles (∝ (f / 2.5 kHz)²). | measured hangs: most partials within ±5 c, all within ±13 c; next modes near the third octave (ding 9.08×) |
| Decay | T60 for each field's partials runs from 3.2 s (low) to 2.5 s (high). The octave is as long as the fundamental, the fifth 0.85 ×, and the ding 4.5 s. | Alon 2015: mean 2.9 s, low 3.2 s, high 2.5 s |
| Gu | An air plug of mass ρSL_eff on the cavity's spring ρc²/V: 25 L, an 82 mm port and a 19 mm neck (85 Hz). It is sized for each key so the coupled boom sounds at 85 × ding / 148 Hz. Q is 25. | Wessel et al. 2008, 85 Hz; handpan blanks |
| Cavity coupling | Pressure is −ρc²/V · ΔV. Each mode feels A·p and makes ΔV by A·x: the dome's A = 0.463 × area, the octave's net 0.13 × area (it radiates as a monopole), the fifth's almost none (a dipole). The shell's give pulls the gu below the rigid-walled formula, so the gu is sized for that. The fundamentals are pre-compensated for the gu's pull so they sound in tune. | Morrison & Rossing: monopole octave, dipole fifth; f_H 85 → 90 Hz when a field is damped |
| Gu lip | A bell at 4.0, 4.757 and 5.04 × the ding (D5, F5, F#5 over D3). | Integral Hang |
| Sympathy | Partials of two fields within 35 c are coupled pairwise by κ = 0.3 % of √(K_a·K_b). The overtone sits 14 c above the fundamental it shares, where a hard strike's sag in pitch carries the fundamental away from it, and never more than 10 c from its own note. Kept apart like this, they trade only part of their energy, at a few hertz. A field's fundamental also drives the fundamental of the field an octave above it (ding → D4, A3 → A4) through U = η·xa²·xb with η = 0.05·k_b/h, below where it would grow a ghost of the note an octave down. This is the cross-note 1 : 2 internal resonance, and it grows with how hard the field is struck. A weak shared shoulder motion leaks a little of everything everywhere. | Alon: coupled partials +13…+22 dB, the D3 ding's 4× becoming the D4 field's octave, beating 3–9 Hz from coupled mistuned fields. MTT12: the cross-note coupling is quadratic and starts at h/25. |
| Dimple | U = β·x1²·x2 + γ·x1·x2·x3 per field, with β = 0.05 and γ = 0.3 of k1/h, plus a softening cubic in x1. A full-strength strike starts about a quarter-tone flat, glides back within 0.4 s, and blooms into its octave, then its fifth. The volume a dome sweeps also has a second-order term, A·x²/(2·depth) with a 4 mm depth, which radiates harmonics above the fifth. | Alon: f1 peaks before 23 ms, the octave about 90 ms, the fifth 0.2–0.5 s. Steelpans: nonlinearity starts at h/25, and a stick's softening reaches −50 c (a fingertip's less). |
| Hand | A 3 g fingertip with a Hertzian pulp, F = K·δ^1.5 with K = 3e5. It is solved against the surface's motion and pulled back by the hand. Pressed past 0.25 mm the pulp bottoms out: the bone's far stiffer spring (3e6) adds to it. That makes the click of a hard stroke, so brightness grows steeply with velocity and soft strokes stay fundamental plus octave. Contact runs from about 3 ms soft to 1 ms hard, and arrival speed from 0.02 to 1.3 m/s, swinging a field from h/25 to h/5. The palm on the gu is 40 g. | contact 1–4 ms (finger, inferred), 1–2 ms (steelpan stick); soft strokes are two partials (Alon) |
| Strike position | Mode shapes: the dome; the octave with lobes at the ends of the long axis (0.45, against a fingertip's wide patch) plus a 0.1 share of the dome; the fifth with lobes across. The rim is not a node, and every mode runs out a little into the shoulder. Octave radiation rises with frequency, so low fields have the weaker octaves. | Alon: octave spot +5 dB, fifth spot +7 dB; low notes weaker octaves |
| Sound | Two microphones 45 cm out either side of the player, in front. Each hears the volume acceleration of every mode and of the gu's plug, with dipoles taking the sign of the lobe facing that microphone. Each field reaches each microphone after its own time of flight, so two fields ringing at one frequency don't cancel at both ears. The force under a fingertip also radiates as from an infinite plate (F/ρh, above 1 kHz), and the skin's rasp on the steel adds air above 5 kHz (force² × noise). | Junger & Feit (point-force radiation) |

The model's cost does not grow with the number of fingers. The body is
about 80 modes stepped once per sample (well under 1 % of a core). A strike
only adds a contact for the milliseconds it touches, and the body sleeps
once it has rung 90 dB down.

## How it was checked

A reviewer was briefed to be hard to impress. Over seven rounds it rendered
strikes and measured their spectra, partials, envelopes, beating, stereo and
cost against the published numbers, until it signed off. It accepted four
deviations, which stand:

- **Sympathy:** total sympathetic energy is +1…+3 dB over an isolated field,
  against +4…+10 dB measured. The coupled partials gain +5…+8 dB, and the
  ding's 4× gains +14 dB. Stronger coupling made shared partials trade all
  their energy and pump, so the beating was kept right instead.
- **Tuning:** D4 and A4 sit about 5 c flat. They give way to the overtones
  they share (the ding's octave, A3's octave), as a maker would.
- **Fifth spot:** striking across a field gains its fifth a median of +12 dB
  over a centre strike, against the published +7.
- **Octave spots:** the two ends of a field's long axis differ by several
  dB. The curvature's drive of the octave adds to the strike at one end and
  works against it at the other.

The reviewer left one item to watch rather than fix. At full strength the
lowest field (A3 in D minor) turns octave-dominant for a few tenths of a
second, at the strong end of the published range. If it sounds as if it
jumps up, lower β and γ on the two lowest fields.

`docs/hang-research.md` holds the literature survey these numbers come
from, with its sources. `tests/hang_tests.cpp` checks the model against
those numbers.
