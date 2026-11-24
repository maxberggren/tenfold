# The tromba marina

The Lead page has a **Tromba marina** (baroque), simulated as it is built
(`src/trombamarina.h`) and played as a melody instrument (`src/lead.cpp`,
the `Sound::TrombaMarina` block in `renderVoice`).

It is one long gut string, bowed near the top, with the player's thumb
resting lightly on one of its nodes. So every note is a natural harmonic
of the open string. The string runs over a bridge whose free foot knocks
on the soundboard every period, which makes the trumpet's bray its name
comes from. Every voice of the line, melody and harmony, is a string of
its own.

## Playing it

- **Where you play:** the neck picks the note, and the note picks the
  harmonic. The open string is tuned to the key's root two octaves under
  the neck's (G1 to F♯2). Each note is played on the harmonic of that
  string nearest it in pitch: on A minor, A3 is the 4th, E4 the 6th, A4
  the 8th, C5 the 10th, A5 the 16th.
  - The string is retuned so the harmonic lands exactly on the key's
    note. This is the one way it is not a historical instrument, which
    can only play its string's harmonics. Without it, the lead could not
    follow the key.
    - Where the harmonics stand close (from the 8th up), the retune is a
      few cents to a semitone.
    - On the low ones it is more: in A minor, G3 and B3 are 2 semitones
      off the 4th and 5th harmonics.
    - Up to the 14th harmonic, the worst case is 3 semitones: the open
      string on F♯2, with B♭3 on its 3rd harmonic.
    - Above it, the notes past the neck's top are played on the 14th of a
      string retuned up: C6 in A minor by 5.3 semitones, and higher notes
      by more.
  - From the 14th harmonic up, the thumb stays on the 14th and the string
    goes up. In the model the 15th and 16th don't speak cleanly: their
    neighbouring modes have nearly a node under the thumb too (see the
    open points).
- **How hard:** the press is the bow's weight. It moves from 0.4 of the
  most the bow can use there (Schelleng's limit) to 0.85 of it, and more
  on the high harmonics, which need it. Leaning in, the bow also moves
  faster (0.11 to 0.32 m/s) and nearer the nut:
  - **light:** a soft, nasal flageolet. The bridge rocks but its foot
    doesn't reach the board;
  - **firmer:** past a threshold, the free foot comes down on the board
    every period, and the note brays like a trumpet;
  - **all the way:** the share passes Schelleng's limit, and the string
    sticks too long and scratches.
- **How high:** higher on the pad bows nearer the nut: brighter, and it
  needs more weight.
- **A flatter finger:** sets the guidon closer, from 20 µm down to 12 µm,
  for more bray. Players adjust it for the timbre they want.
- **Holding:** the bow is drawn by the envelope. It turns round at the end
  of its stroke (2.6 s), passing through rest in about 10 ms. A slower
  turn lingers near rest, where the string can catch in several slips a
  period and stay there. A note struck again while it sounds turns the
  bow too.
  - The tromba marina's bridge is asymmetric, so the up-bow and down-bow
    knock differently. This is a real difference between the two
    directions.
- **Legato:** a second finger slides the thumb to the new note's node
  over about 35 ms, without a new bow. The bow's place, a share of the
  loop the thumb is heading for, moves at once by a few millimetres; the
  critique measured that as inaudible. The mode the thumb has left dies
  under it, and the string takes up the new harmonic, down to a divisor
  too (the 12th to the 6th).
  - The Lead's glide moves the open string's pitch with it. A leap from
    the 12th to the 6th sweeps the open string an octave up and back over
    about 100 ms, which a real string couldn't do (see the open points).
  - About one slide in five, nearly all at presses up to 0.5, ends in
    several slips a period. The level dips 12 dB on average (34 dB at
    worst) within 200 ms of a slide (critique round 3).
- **The attack:** the bow's weight comes on ahead of its speed, as a
  player sets the bow before drawing it.
- **Lifting:** the bow leaves the string and the hand stills it in about
  0.8 s.
- **No vibrato:** a harmonic's pitch is the string's.
- **Bends:** the bend wheel and a twist retune the open string. On the
  real instrument they couldn't happen at all.

## The model

Everything is in SI units, integrated sample by sample. The string and
the bridge run at four times the rate (192 kHz), and come back down
through two halfband filters (15 and 31 taps). The board runs at the
rate.
- At twice the rate the knock spanned only 4–8 samples. Pitch under a
  firm bow was off by 6–14 cents, and the regime changed in some cells
  (critique round 2).
- At four times, the pitch is within 1.6 cents of eight times (5.6 at
  988 Hz).
- It is not converged in everything: in 4 of 42 cells the regime differs
  (784 and 988 Hz at a press of 0.3 are clean at 4× and catch in 5 slips
  a period at 8×), and 8–16 kHz differs by up to 31 dB. See the open
  points.
Sources are in `docs/tromba-marina-research.md`. **inf** marks a value I
estimated where nothing is published.

| Part | What it is | Constant | Set from |
|---|---|---|---|
| String | Gut, 1.6 m nut to bridge, 1.6 mm thick, ρ = 1300 kg/m³. Its tension follows from the open pitch (c = 2Lf, T = μc², about 113 N at C2), and so does its impedance Z = μc | `kLength`, `kDiameter`, `kGutRho` | Length: [MET]/[GAC] soundboxes of 1.0–1.3 m plus the neck, **inf**. Diameter: [BHP16]'s 1.30 mm replica string, a little thicker for a low tuning, **inf**. Density: [FR98] |
| Waveguide | Velocity waves in six delay lines: nut–bow, bow–thumb and thumb–bridge, each both ways. Lengths are fractional (cubic Hermite reads), set so the round trip is the open string's period less the delay that the loss filter and the bridge's give add at the note, and so the thumb sits at 1/n of the string as the note hears it | — | [MSW83] (the travelling-wave string) |
| Losses | A one-pole lowpass at the bridge, T60 = 6 s at the open fundamental and 0.5 s at 3 kHz | `kT60Low`, `kT60High`, `kLossHz` | **inf**: a gut string on rigid ends, from [FR98]'s range. No tromba-marina measurement |
| Stiffness | Left out. Gut with E ≈ 5 GPa gives B ≈ 5·10⁻⁵, so the 16th mode is 12 cents sharp of 16·f0. Under a bow the Helmholtz motion locks the partials harmonic, and the string is retuned onto the note anyway | — | [FR98] |
| Thumb | A point damper of R = 2Z at the node (wave-digital resistor): it lets 2Z/(R + 2Z) through and reflects the rest inverted. Every mode moving there loses half its energy per pass, the most a point damper can take, and only the harmonic family n, 2n … passes untouched. It reaches up to the 14th harmonic | `kThumbR = 2`, `kMaxHarmonic = 14` | [Pak05], [PG05] (the finger as a mirror; no force at a node). The value is **inf**. With R = Z, the mode left behind by a slide survived under the bow (critique round 1) |
| Thumb slide | A new harmonic moves the thumb and retunes the open string together, exponentially over 35 ms | `kThumbSlideS` | **inf**. Easing the bow during the slide was tried and removed: it doubled the slides that ended in multiple slips (critique round 3) |
| Hand | Once the bow leaves, the string's loss rises to T60 = 0.8 s | `kHandT60` | **inf** |
| Bow | A point contact solved where the string's impedance line meets rosin's friction curve μ = μd + (μs − μd)·v0/(v0 + Δv), with hysteresis (stay stuck while possible; stay sliding while a sliding solution exists). μs = 0.8, μd = 0.3, v0 = 0.1 m/s | `kMuS`, `kMuD`, `kV0` | [SW00], [MSW83], [WG04] |
| Rosin's grain | The normal force fluctuates ±3 % while sliding (white noise) | `kGrain` | **inf** |
| Player | Speed 0.11–0.32 m/s with the press (and ±15 % with the velocity). β from the nut, as a share of the nut–thumb loop: 0.27 light to 0.18 firm, ±15 % with the height (0.34 was too near a third of the loop, where light notes caught in multiple slips after the Lead's attack or a turn). Force from a least share (0.4 of F_max, rising 0.03 a harmonic above the 6th, to 0.75 at most) to 0.85 of F_max, then quadratically past a press of 0.88 to beyond F_max | `bowFor` | [Sch73] limits; [PG05] the bow in the upper part, near the nut. The shares are **inf**, set from regime maps of the model (Helmholtz, knocking, multiple slips, scratch over notes × press × direction) |
| Bridge | One degree of freedom, the free foot's height y. Effective mass 0.75 g, rocking resonance 1 kHz, Q 20. The string sits 1.2 times the foot span above the pivot, so u = 1.2·y and the string's force acts on y through the same lever. Integrated with an energy-conserving scheme: Störmer–Verlet, the spring averaged, the damping and the string's impedance centred, and the contact as a discrete gradient of its potential, solved by Newton for its unique root. Critique round 1 found the earlier backward-Euler step lost energy and wasn't converged. Round 2 measured the new one conserving energy to 10⁻¹¹ in double, with Hunt–Crossley's restitution and Q 20.08. On the string, the string's load r²Z (0.66 kg/s at 55 Hz) dominates the shoe's own damping, so the effective rocking Q is about 4–5 | `kBridgeMass`, `kBridgeHz`, `kBridgeQ`, `kLever` | [PG05]: a mass–spring system driven by the string, rocking about the loaded foot. Every number is **inf**: chosen so that a firm bow's foot excursion is a few times the gap and a light one's under it |
| Gap | The free foot stands 20 µm off the board | `kGap` | [PG05]: "around 20 micrometers" (the guidon) |
| Contact | Unilateral Hunt–Crossley: potential K·δ^2.5/2.5 for δ = −g − y > 0, its force K·δ^1.5, and loss λ·K·δ^1.5·ẏ (coefficient from the current step). K = 1.06·10⁹ N/m^1.5, λ = 1.5 s/m. The discrete gradient makes the contact conservative, so the scheme is stable however hard it is driven | `kContactK`, `kContactAlpha`, `kContactLambda` | [HC75], [BTC15]. K and λ are **inf**: bone on a hardwood plate, with contact rings of a few kHz |
| Board | A strip of spruce 1.35 m long, tapering from 0.20 m (at the bridge) to 0.07 m (at the neck), 3 mm thick. E_L = 11 GPa, E_T = 0.75 GPa, G_LT = 0.7 GPa, ν_LT = 0.37, ρ = 430 kg/m³. Each cross-section is in its clamped beam's first mode across the width, and Galerkin leaves a beam along the grain (D_x·w), a twisting tension (2H·∫φ'²/w) and a foundation from bending across it (D_y·λ⁴/w³). 96 finite-difference points, both ends clamped, solved by Jacobi when the instrument is built. Modes from 151 Hz, about 40–80 Hz apart, crowded into the wide end | `kBoard*`, `kE*`, `kSpruceRho` | Wood: [HAI79], [WH]. Shape: [BHP16], [WIKI], [MET]. Width and thickness **inf** |
| Board loss | Q = 12 on every mode | `kBoardQ` | **inf**: below wood's material Q of 100–140 [HAI79], as built plates (and a board loaded by the box and its air) are. Lowered from 25 after round 1 found neighbouring notes 16 dB apart on isolated modes |
| Radiation | Pressure at 1 m on two microphones (by the board's lower and upper halves) is ρ0/(2π r) times the volume acceleration. Each mode's volume is ∫ψ·w·∫φ dx, weighted by each element's distance. Above 700 Hz the board's own modes stop. The infinite plate's answer carries the bridge's force there, less what the modes already give: a residual-mass term (1/ρh − Σψ·vol) through a 2nd-order highpass, so the response settles on the infinite plate's F/ρh instead of counting it twice. This is how the knocks are heard | `kModesTop`, `kAirRho` | The hang's and the gong's method; F/ρh for a point-driven infinite plate |
| Board drive | Upward force into the board: the bridge's spring and damper through the loaded foot, less the free foot's knock | — | Newton's third law on the 1-DOF bridge |
| Level on the page | The pressure times 0.5, and times (f/440 Hz)^0.35 across the neck, so a line is one instrument (the board's modes and the harmonic's weaker drive of the bridge would otherwise make it ±10 dB) | `kTrombaLevel` (lead.cpp) | — |

## What it does (measured by `tests/trombamarina_tests.cpp`)

- **Harmonic selection:** bowed on the 3rd, 4th, 6th, 8th and 12th
  harmonics, it sounds n·f0 within 8 cents. The open string's other modes
  stand at least 35 dB under the note.
- **Slides:** under a steady bow, at presses of 0.25 and 0.55 and in both
  bow directions, the thumb moved to another node lands within 10 cents
  of the new harmonic 0.3–0.7 s after the move:
  - to a neighbouring node, up or down (G4→A4, C5→D5, D5→E5, A4→G4,
    E5→C5), with the old note at least 20 dB under the new;
  - down to a divisor (E5→E4, the 12th to the 6th; A4→A3, the 8th to the
    4th).
- **The Lead's attack and turn:** bowed with the Lead's own envelope
  (rising toward 1.2 with an 80 ms time constant, the weight on first, a
  turn with a 10 ms time constant). The grid is ten notes G3–G5, presses
  of 0.1, 0.3, 0.5 and 0.7, both directions, 0.3 s after the attack and
  after the turn. At least nine cells in ten are back to one slip a
  period (0.9–1.15); the test measures 13 of 160 off.
- **Helmholtz motion:** between the limits the bow lets go of the string
  once a period of the note. It slides for β of the period, within 35 %,
  at β = 0.12, 0.2 and 0.3.
- **Scratch:** at 2.2·F_max it no longer lets go once a period, and the
  harmonics' margin over the noise between them falls by more than 10 dB.
- **The buzz's threshold:** at a fixed speed and place (0.25 m/s
  down-bow, β 0.3), the string stays in its Helmholtz motion at every
  force tested. At 0.08 and 0.15 of F_max the bridge is silent; at 0.5
  and 0.85 it knocks every period. So it is the force that closes the
  gap, not a change of regime. At β 0.2 both bow directions knock at
  0.6 of F_max.
  - Through the player, from A4 to E5, a light press (0.1) never knocks, and a
    press of 0.6 knocks every period and is at least 10 dB louder in
    2–8 kHz.
  - On the Lead, A4 at a press of 0.7 is 8 dB brighter (2–8 kHz against
    the band below) than at 0.12.
- **Stability:** 20 s of 20 N at 2 m/s, turning, leaping across every
  harmonic and bending, with NaN and infinities thrown in. It stays finite
  under 200 Pa, and once the bow leaves it sleeps within 4 s.
- **Cost:** a knocking string costs under 1.5 % of real time (0.8–0.9 %
  on the development machine). A released string goes on costing that
  until it sleeps, 0.8–1.3 s later. The worst case, eight voices of four
  strings, is about 0.28 of a core. A run with three harmony strings
  keeps p99 load under 0.2.

## Open points

- **The two bow directions differ.** The knock starts at a lower press on
  one direction than the other: through the Lead, often one direction
  knocks from a press of 0.2–0.3 and the other only from 0.5. At a press
  of 0.5, critique round 2 measured the down-bow 20 dB brighter on
  average (−14…+40 dB); at 0.7, 8 dB brighter (0…20 dB). The shoe is asymmetric, so some difference is real,
  but how much is unmeasured. There is no static preload and no vertical
  polarisation.
- **The knocks' brightness is uneven** from note to note (the 2–8 kHz band
  against the band below ranges about 30 dB over the knocking cells), set
  by how fast the foot arrives.
- **Harmonics 15 and 16 don't speak cleanly in the model.** The open
  string's neighbouring modes are nearly at a node under the thumb too,
  so a point damper takes almost nothing from them, and the bow keeps
  them going. The real instrument reaches the 16th, perhaps through the
  thumb's width or a firmer touch. Here the thumb stops at the 14th.
  The 13th and 14th, and the notes above them on a string retuned up,
  are the weakest. Lighter than a press of 0.45, the bow caught them in
  several slips a period, some an octave or a twelfth off, so the player
  never bows them lighter than that. They are never a soft flageolet.
- **Light notes sometimes catch in multiple slips** through the Lead's
  attack and turn, at 3–21 slips a period: a rough, noisy note, some
  sounding an octave or a twelfth off, quieter by up to 27 dB at a turn.
  - Over G3–C6, at all presses and both directions, my map has 41 of 504
    cells off. The test's grid has 13 of 160: C4 and C5 at 0.1; G4, A4,
    B4 and C5 at 0.3.
  - Critique round 3, measuring differently (and before the 13th and up
    were held to a press of 0.45), found 61 of 420, spread over C4–C6 in
    both directions.
  - About 9 % of clean cells leave the Helmholtz motion at a turn.
  - The Helmholtz window of a harmonic bowed near the nut is narrow, and
    a player would find the spot; this player doesn't search. None of
    `bowFor`'s knobs removed it (round 3 tried the force's timing, the
    least share, the speed's ramp, the string's loss and β).
- **Low harmonics rattle more:** at a firm press, the 4th and 5th
  harmonics (A3–D4) knock two or three times a period. A harmonic n sends
  about 1/n of a pressed note's energy to the bridge [PG05], so the low
  ones drive it hardest. That is the period doubling of [PG05], though
  never as a clean regime of its own.
- **Pitch moves with the bow** by a few cents: Helmholtz flattening under
  a heavy bow, and a little sharpening while the foot touches.
- **The Lead's glide retunes the open string.** Each step of a legato
  glide is played on the new note's harmonic, so a leap across harmonics
  that are not neighbours sweeps the open string's tension. A real player
  would slide the thumb alone.
- **The regime depends a little on the sample rate.** At 4× the string's
  rate, a few cells (about 1 in 10) are in a different regime than at 8×.
  The numerical damping of 4× flatters the clean counts.
- **Memory:** each voice's four strings hold 384 KiB of delay lines
  (3 MiB for the Lead at 48 kHz), allocated whatever the sound. They are
  cleared at a note-on only if something was written since they were
  last cleared.
- **The bridge's numbers are estimates.** Its mass, rocking stiffness,
  lever and contact are set so the threshold lands in the press's range.
  No measurement of them was found.
- **The box's air is not modelled.** The long cavity's pipe modes
  (c/2L ≈ 120 Hz and up) and any rose are missing, as are the back boards
  and the second and higher cross-width board modes. Above 700 Hz the
  board is an infinite plate.
- **The bridge and the board don't interact.** The bridge's knock pushes
  the board, but the board's motion doesn't move the contact.
- **Gut's stiffness is left out** (see the table).
- **Only one polarisation.** The bowed, horizontal one rocks the bridge.
  [PG05] measured vertical motion too.
- **Rosin is the hyperbolic friction law, not a thermal model.**
- **No spectrum of a real tromba marina with numbers** was found to check
  against, and the full [WSBD20] paper couldn't be read here.
