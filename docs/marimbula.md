# The marimbula

The bass page's sixteenth sound is not a recipe. It is a marimbula, the
Caribbean box lamellophone that plays the bass in Cuban son and changüí and
in Jamaican mento, simulated as it is built (`src/marimbula.h`) and played
by the bass lane (`src/bass.h`, `renderMarimbula`). `docs/marimbula-research.md`
holds the survey the numbers come from.

## Playing it

Choose it with → from P six times (its chip says P→6), or click its chip.

- **Every finger plucks its own tongue.** The pad's columns are the key's
  notes, as on every bass sound; each note has a tongue of its own on the
  box. A tongue rings while its finger is down, and a second finger plucks
  another tongue while the first rings on under it.
- **Lifting:** the finger comes back down on the tongue and stills it, in
  about a tenth of a second. That is how a line is articulated.
- **Pressing into a held note:** lays the finger on the tongue early, so it
  is choked sooner the harder you press.
- **MUTE** (the band's first switch): every note is stilled as soon as it
  is plucked, a dead thump.
- **How hard:** pressure sets how far the finger pushes the tongue down
  before it slips off (a long, soft bass tongue goes further). Pressed hard
  (past about two thirds of full pressure), or accented, the finger slaps:
  it lands on the tongue at speed. Slapped as hard as a hand can, the
  tongue's tip clacks on the panel.
- **Height:** the finger's soft pad (low) rolls off the tongue slowly and
  wakes only the fundamental; the nail (high) flicks off and wakes the
  overtones. A flat finger plays with the pad.
- **Twist (SPOT):** where along the tongue the finger meets it, from the tip
  (round) to a third of the way in toward the bridge (thinner, the overtones
  louder against the note).
- **The click:** the other hand knocks the box. Low on the pad it is the
  palm flat beside the tongues (the box's air and lowest modes, a bass
  drum); high, the knuckles near the top edge (dry, upper modes).
- **The rail works as for every sound.**
  - ROLL plucks the held note on the pattern's steps and stills it after
    the gate.
  - OCT plucks the tongue an octave up.
  - BEND bends the tongue, as a player pressing its tail behind the bridge
    would.
  - FOLLOW plucks the chord's root.
  - FILTER and GRIT work on the box's sound.
- **The box is built for the key:** its hole is cut so the box's air sounds
  just under the key's root (within 45–72 Hz, what real boxes and holes
  give), as on an instrument made for its tuning. It lifts the low notes,
  but a small box still radiates them less: in keys E to A the root is
  7–10 dB under the loudest note of its scale.

## The model

Everything is in SI units, integrated sample by sample. Each mode is a
damped oscillator stepped exactly for its frequency and decay, as in the
hang and the gongs. Everything that couples is a force between them.

| Part | What it is | Set from |
|---|---|---|
| Tongue stock | Spring steel (E 200 GPa, ρ 7850, G 79 GPa), 28 × 0.75 mm, every tongue cut from the same spring; only its length differs. A note's length comes out of the beam: 11.3 cm for F1, 9.1 cm for C2, 7.2 cm for G2. | Thompson: gramophone spring 1–1¼ in × a little under 1/32 in; survey: 11.4 cm for F1 at 0.7 mm |
| Tongue modes | An Euler–Bernoulli beam, free at the tip, its root on two springs: the span of steel back to the clamping bar (2.2 cm) resists it turning (K_r = 4EI/a), and the bridge gives under it (k_t = 3·10⁵ N/m). Six bending modes are solved from the four boundary conditions when the instrument is built (a table over length, read between). The first overtone is 6.24× at F1, 6.04× at G2, 5.92× at C3 and 5.54× at G3; the second 17.0× down to 13.0×. | Cantilever 1 : 6.267 : 17.55 : 34.39 (both springs stiff, checked); measured tines 5.3–6.8 and 11.7–14 (Chapman 2006, 2008; McNeil & Mitran 2008); k_t set so the bridge's give brings the ratios toward them, as the survey's FE does at k·a³/EI = 100–300 |
| Twisting mode | A thin strip's first torsion mode, J = wh³/3·(1 − 0.63h/w), a quarter wave on the tongue: 7.0× the note at C2. Woken by a finger landing off the tongue's middle (a random few millimetres each pluck); it rolls the clamp board, not pushes it. | survey: 7.5× at 65 Hz |
| Tongue losses | A Q of 150 for every mode (the clamp losing much the same share of energy each cycle), plus 2.5 s⁻¹ at every frequency (the clamp's rubbing). Away from the box's resonances a C2 note's T60 is about 1.6 s; on them, much shorter. | survey estimate for a marimbula: T60 1–3 s (EST); a kalimba tine's tail 4–5 s (Chapman 2006 figure) |
| Clamp board | The tongues are bolted to a hardwood board along the bridge, 44% of the panel's width: a rigid bar. Each tongue's root moves with the bar where it sits (up and down, tipping along the tongue, rolling end to end, from each panel mode's average and least-squares slope along it). The board, bars and resting tongues (0.4 kg) load the front's modes by what each moves the bar: its first mode drops from 117 to 93 Hz. | survey: front (1,1) 80–130 Hz for glued edges; Thompson: clamp boards bolted to the front |
| Bridge coupling | A beam on a moving base, exactly: the board's motion accelerates the tongue's root, and the tongue's modes push the board back with their root force, moment and torque (stiffness form, plus the share of the finger's force the modes leave out). Inertial one way and elastic the other is what the beam's Lagrangian gives (with every loss off, the energy stays bounded and never grows, at 22–192 kHz); statically the finger's force reaches the panel whole. Modes above a fifth of the sample rate take no part. | Ludwigsen 2008: the box radiates the tines' fundamentals through the bridge |
| Tuning | A maker slides each tongue until it sounds in tune on its box. Where a tongue is heard is the root of its first mode's equation with the box, ω² = ω₀²/(1 + ω₀²G(ω)/M), G the box's complex receptance at the board (Sherman–Morrison over the panels and the air, losses included), followed by Newton in complex frequency from the tongue's own, so it stays on the tongue's branch. A secant on the tongue's length makes that land on the note. Measured from −2.8 to +3.8 cents (−5.2 at worst) every semitone from E1 to E4, with 72–99% of the note's energy at its pitch, at 44.1, 48 and 96 kHz alike, except the wolves (below). | — |
| Box | 58 × 38 × 20 cm inside (44 L). Front and back are ¼-inch (6.35 mm) birch plywood, orthotropic (12.7 / 4.8 GPa, 680 kg/m³), simply supported with the glued edges' stiffening (×1.3 on frequency). Fourteen front modes (93 Hz to 1.3 kHz) and four back ones. Panel Q 18 (plywood's loss, the joints, the floor). | Thompson: 58–64 × 37–47 × 19–28 cm, ¼-inch plywood; Finnish plywood handbook; Kičák: cajón front (0,0) 131.5 Hz |
| Air | The cavity's pressure is ρc²/V times the change in its volume. Every panel mode with net volume (odd × odd, area 4ab/π²mn) feels it and makes it, and so does the plug of air in the hole (mass ρ·S·L, L the panel plus 0.85 r each side). The hole is cut per key, so that the box is *heard* where wanted: the lowest root of the box's own equation (1 + κ·Σ aᵢ²/(Kᵢ − ω²Mᵢ) = 0), 0.74–0.82 of the rigid formula as the panels give. A hole 5.5 cm across for 45 Hz, 8.2 cm across for 55 Hz, 15.7 cm across for 72 Hz. Hole Q 8 (viscous loss, radiation, the player's legs either side of it). | Kičák: cajón Helmholtz 63–94 Hz, measured 0.85 × the rigid formula; survey: marimbula f_H 50–80 Hz, Q 5–20 (EST); semicircular holes ~11–14 cm equivalent |
| Finger: pluck | A still tongue starts from its static bent shape under the finger (its modes and the box's). The finger pushes it most of the way to the panel when pressed hard, but a fingertip pushes with 1–7 N at most, so a short, stiff tongue goes down less. Then the force falls as the pad slides off the tip: cos(πr/2), easing off and gone with a kink at the edge, over 4.5 ms off the soft pad down to 0.4 ms off the nail (a quarter quicker pressed hard). A tongue already moving is caught: the finger lands on it, its flesh a dashpot (3 N·s/m) that soaks up the swing while its force presses in over 5 ms, and then slips off as from rest, so a re-pluck sets the tongue rather than adding to it, without a click (its highs within 4 dB of a fresh pluck's). | harp slip ≈ 3 ms (Chadefaux 2012), guitar 0.5 ms; survey: push 3–15 mm with 1–9 N, slip 0.5–3 ms (EST) |
| Finger: slap | A 10 g fingertip with a Hertzian pulp (3·10⁵ N/m^1.5, stiffer toward the nail) landing at up to 0.9 m/s. It is solved against the tongue's motion and pushed by the hand (up to three times a pluck's force) toward driving the tip onto the panel, and then slips off as a pluck. | hang fingertip model; harp release velocities 0.2–5 m/s |
| Tip on the panel | 9 mm under the tip at rest. Past that, a Hertzian steel-on-wood contact (4·10⁷ N/m^1.5, Hunt–Crossley loss) pushes the tip back and the panel in. Hard slaps reach it (the clack); a pluck holds the tip at most 95% of the way down, and a bend never swings it further than that. | — |
| Finger damping | A dashpot of 1.6 N·s/m at the finger's spot on the tongue, coming on over 6 ms. | — |
| Knock | A Hertzian hand on the front panel, solved against the panel's motion, between the palm (0.2 kg, 2·10⁵ N/m^1.5, beside the tongues) and the knuckles (10 g, bone on wood, 1.5·10⁷, near the top edge), landing at 0.3–1.9 m/s. | Kičák: palm near the middle gives the bass of the plate and the air; fingers near the edge the bright stroke |
| Sound | A microphone a metre in front hears ρ/4πr times the volume acceleration of the plug and the panels' net volume (both monopoles; the back's from behind). Panel modes without net volume radiate a little (a tenth, rising toward coincidence at about 3 kHz). Each tongue is a dipole: its swept volume times ωd/c, with d its width. Hard contacts on the panel radiate their force as from an infinite plate (ρ₀F/2πrρh) above 1.4 kHz, where the modes stop: the knuckles and the tip's clack. (The bridge's own force does not: through the 0.4 kg board it is gone long before that.) A pluck's highs are the tongue's own upper modes, its dipoles. | Fletcher & Rossing ch. 7; survey: a bare tongue ≥ 40 dB under the box at the fundamental |
| Contacts | The slap's pulp, the tip on the panel and the knock are each solved implicitly with the step the masses they push are about to take (the force bisected against how far its own push moves them apart), so a stiff contact is as stiff at any rate and cannot ring itself up. | — |

On the page, the lane adds the engineer's EQ. A small box radiates its low
notes about 12 dB an octave less than its high ones, up to its panel's
first mode. Two shelves lift what is under 115 Hz by as much, down to 36 Hz,
and a high-pass at 30 Hz takes the rumble. Then come the level, the FILTER
finger, GRIT, the kick's pump and the lane's knee.

### Cost

The box (18 panel modes and the plug) rings once, whatever plays it. Each
sounding tongue adds seven modes and its coupling to the front's fourteen
(about 150 multiply-adds a sample). Measured by the reviewer (best case,
on a loaded machine): six tongues ringing 185 ns a sample (0.9% of a
core). A slap's contact, solved by bisection while it touches, costs more:
six tongues slapped every few milliseconds with knocks reach 2.6%, which
no line played on the page comes near. A tongue sleeps once rung out, and
the box once everything has. A new note's tongue (the secant over the box's
equation) costs 4 µs on average and 10 µs at worst when its note lands, a
bend 2–13 µs, and a new key's box (its hole solved from the box's
equation) 21–43 µs, once.

## How it was checked

`tests/marimbula_tests.cpp` checks the model against the numbers above:

- the beam's limits;
- the tongues' ratios and lengths;
- the tuning on the box;
- the overtone in the attack;
- the Helmholtz formula and where the box rings;
- that the box carries the fundamental (box path against the tongue's own
  dipole, and against a tongue on a rigid block);
- the two-way coupling;
- the finger's gestures and the knock;
- the page's gestures, saving and reopening;
- stability at 44.1, 48 and 96 kHz;
- the cost.

Three critique rounds by a reviewer briefed to be hard to impress measured
the model against the survey's numbers. The third signed off with the
open points below.

- **Round 1 (not yet).** The beam, the coupling's energy and the rate
  independence held. Fourteen defects were found, among them:
  - tuning off by up to a semitone near the box's modes;
  - nothing above 2.5 kHz;
  - a re-pluck that added to the tongue's swing;
  - bass notes ringing 5–8 s;
  - a clack that almost never sounded;
  - the clamp board's mass not moving the panel;
  - a constant "heard" factor for the box's air;
  - high notes 30 dB too loud.

  All were fixed: the rigid clamp board, the air solved from the box's
  equation, the fingertip's force limit, the catch, the constant loss, six
  bending modes and the contacts held stable.
- **Round 2 (not yet).** Tuning still used a first-order correction and
  missed notes by up to two semitones near modes; a hard pluck could start
  with the tip through the panel; contacts behaved differently at different
  rates; and the bridge's plate radiation did nothing. Fixed by the
  secant-and-Newton tuning, the pluck's clamp, the implicit contacts, and
  removing that path.
- **Round 3 (signed off with open points).** All tuning outside the wolves
  is within −2.8…+3.8 cents. No pluck touches the panel in 4104 cases a
  rate. Contacts agree across 44.1–96 kHz within 1–3 dB. It remained to
  make the catch click-free (done: the finger now lands as a dashpot) and
  to give the wolves', the roots' and the costs' real numbers here (done).

The reviewer's verdict on realism: played from E1 to F♯2 and A2 to G3, it
passes for a crude marimbula.

## Open points

- **Wolves.** A note from about 3% below to 15% above one of the box's
  resonances (a front mode, or the box's air, as they ring coupled) falls
  in the gap the tongue and the box open between them: no length of tongue
  sounds there. Its note is short, much of its energy is at the box's
  frequency, and it is heard well off. Measured from E1 to E4:
  - G♯2 sounds 106–144 cents flat, in every key;
  - G2 is 14–80 cents flat, G♯3 21–58 cents flat, and C4 36–55 cents sharp;
  - A3 has only 11–25% of its energy at its note;
  - in keys C, C♯ and D, whose boxes' air pushes a coupled panel mode up, C♯3
    is 22–59 cents sharp.

  That is five or six of thirty-seven semitones on any one box. A bend
  passing through a wolf stalls and then jumps about 17 cents. Kalimbas
  show the same coupling as the fast first stage of their decay, and real
  marimbulas are tuned loosely around such notes ("the ear accepting a
  vague low pitch", Thompson).
- **The register above G3 is quiet** (beyond Thompson's range, reachable
  with OCT), and the bass tongues' second overtone sits at about 17× the
  note where measured tines show 11.7–14×.
- **The palm's knock is loud:** at full force, about 10 dB over a
  mid-strength pluck (Thompson: the box drumming is a voice of its own).
- **The clamp board's mass is added to each panel mode alone.** The full
  mass matrix (its cross terms) would move the front's modes to about
  90/220/235/375 Hz from 93/210/262/462, and the wolves with them.
- **Nothing is measured on a marimbula itself.** The tongues' Q, the box's
  Q, the hole's size and the pluck's slip are estimates, set from kalimba,
  cajón and harp measurements.
- **The tongues are uniform strips.** Real ones keep the spring's curve,
  have rough ends and may be filed. Thompson's instruments are tuned
  loosely and not tempered; this one is tuned to the key.
- **Only the front and back panels are modelled.** The thicker sides, top
  and bottom, and the cavity's own standing waves (above 280 Hz), are left
  out. They would colour the knuckle knock.
- **No incidental rattle.** Loose clamps or tongue tails touching the box
  are plausible but undocumented, and deliberate buzzers are not
  traditional.
