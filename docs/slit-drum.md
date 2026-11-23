# The slit drum

The drums page's last kit is a slit drum: a wooden tongue drum, the whole
pad one instrument of wood and air. It is simulated as it is built
(`src/slitdrum.h`) and drawn as it is played (`src/drums.cpp`, `paintSlit`).

- The pad is the box seen from above. Four H-shaped cuts in its top leave
  eight tongues, two to each H, pointing at each other across the H's
  crossbar.
- Each H pairs a low tongue with a high one, so the four H's are near
  enough the same length. The lowest note is nearest the player on the
  left, and the scale runs up the left-hand tongues to the far side. It
  carries on from the far right-hand tongue back down the right to the
  highest note, nearest the player on the right.
- Anywhere on the box but off the tongues is a knock on the box.

## Playing it

- **Where you tap along a tongue:**
  - The tip moves in every bending mode: the fullest note.
  - About three quarters of the way out is the second mode's node: the
    purest note.
  - Near the root the first mode hardly moves and the stiff upper ones
    speak: a thinner, brighter knock.
- **Where you tap across a tongue:** off its centre line the tongue twists
  as well as bends. Its first twisting mode adds a clangy, inharmonic
  partial three or four times above the note.
- **How hard:** pressure sets how fast the mallet arrives. Harder is louder,
  and the ball's Hertzian contact is shorter and so brighter.
- **The mallet:** a tongue's "tone" (a click and a twist) chooses it: a
  fingertip, a yarn mallet, a rubber superball (the default) or a hard
  wooden one.
- **The box:** a knock off the tongues sounds the box. On a strip of top
  between the H's you hear the strip; at the ends, behind a row of roots,
  you hear that tongue's upper modes; at the edge, the wall under it. The
  box's air breathes through the slits after each.
- **The rest of the page:** the drums page's rolls, grooves and loops work
  as on any kit. A groove is a melody across the tongues. A tongue's
  "tune" moves it to another tongue's note, and its length sets how long
  the wood rings.
- **Tuning:** the drum follows the key. Its eight tongues are on eight
  degrees of the scale up from the root near G3. In G major it is the box
  as cut: G3 A3 B3 C4 D4 E4 F#4 G4.

## The model

Everything is in SI units, integrated sample by sample. Each mode is a
damped oscillator, `u' = d·u − c·x + f·dt²/m` and `x' = x + u'`, with `d`
and `c` set so its poles are exactly the mode's frequency and decay.
Everything that couples is a force between the oscillators. Sources are
in `docs/slit-drum-research.md`; the tags (pub, calc, inf) say how
firm each number is.

| Part | What it is | Set from |
|---|---|---|
| Wood | African padauk: ρ 745 kg/m³, E_L 13.1 GPa, E_R 1.4 GPa, G 1.1 GPa (G/E_L 0.084). | Wood Database (pub); G/E_L 0.08–0.10 for padauk, whose anisotropy is lower than most hardwoods' (Brémaud 2010; inf from the Wood Handbook's ratios) |
| Top and box | A 13 mm top on 12 mm walls. The box is 41.6 × 27.2 × 20 cm outside and holds 17 litres. Its length is what the longest H needs, with a 30 mm land behind every root. | Makers use 12–20 mm tops; the Schlagwerk 8-tongue box is 60 × 20 × 18 cm |
| Tongues | 40 mm wide and 132–195 mm long, each cut to the length that sounds its note. Six rounds of finite elements, f ∝ L⁻², settle the lengths. | f1 = 0.1615·h·√(E/ρ)/L² (pub, F&R), with the soft root below |
| A tongue's modes | One beam, clamped at the end wall, run through a finite-element model (20 Timoshenko elements, shear-deformable with rotary inertia) once. The beam is the land behind the root (two tongue widths of top, for the plate it shares) and then the tongue. Four bending modes and three twisting ones (linear torsion elements, Saint-Venant's J for a 40 × 13 mm strip). The root is not a clamp: the land gives, taking 28–39 % of the first mode's strain and lowering it about 20 %. Each mode's modal mass is from the wood. | Euler–Bernoulli clamped-free 1 : 6.267 : 17.55 : 34.39; torsion f_Tm = m·c_T/4L; a soft root (κ 5–30) lowers f1 6–26 % (calc) |
| Shear | The elements carry shear and rotary inertia, so each bending mode comes out below Euler–Bernoulli's by its wavenumber against the thickness: about 2 % for the first, 10–25 % for the fourth. The same model solved without shear gives the share of each mode's stiffness that shear takes, and that share is damped as shear. | E/G ≈ 12 in wood against 2.6 in steel; glockenspiel partials "closer together than predicted" (pub, F&R) |
| Ratios as built | B2/B1 5.2–5.8, B3/B1 12.4–14.3, B4/B1 20–25. The first twist sits at 3.0–4.1 × B1, between the first two bending modes, where 3.10·(L/w)·√(G/E) puts it. B2 is never near a harmonic. A piece of wood's own scatter moves each upper ratio by up to ±1.5 %. | Kalimba tines 5.6–5.9 (pub, Chapman 2006); wood's first torsion 1.5–4 × f1, well below B2, against 3.8–7.7 × in steel (calc) |
| Wood's loss | Bending along the grain: η_L 0.005. Shear and twisting: η_G 0.02. The land and the glued joint: 0.012 of each mode's land share. Radiation from the tongue's face: ρω²A²/2πc. Loss is constant with frequency, so T60 ∝ 1/f. | Rosewood marimba bars η 0.0036–0.0068, flat with frequency (pub, Hill 2011); padauk "exceptionally low" (pub); shear 1.4–2.6 × along-grain (pub, Carlier 2018) |
| Decay as heard | First modes ring T60 1.25 s (G3) to 0.52 s (G4) on their own; with the box's air, 0.8 s at G3 and about 0.6 s at 300 Hz. Upper modes die in tens of milliseconds, twists faster than the bending mode below them. | Teponaztli T60 0.39–2.0 s, η_eff 0.007–0.021 (pub, TEP19); inferred default 0.6 s at 300 Hz |
| The box's air | A closed box: a uniform compression on ρc²/V plus nine standing waves (437–1383 Hz), each on ρc²/(VΛ). Every bending mode, panel and slit couples to each wave by ∫φψ over its surface. That gives the coupling to every tongue and the sympathy between them. | Rectangular cavity modes (pub, F&R) |
| The slits | Each H's kerf, 2 mm wide, is a plug of air: the top's thickness plus a narrow slit's end correction either side, (w/π)(1.5 − γ − ln(kw/4)). A short tip-to-tip slot counts too. Together, on the box's volume, they make a Helmholtz resonator at 241 Hz (rigid walls). Its Q of 3.6 comes from radiation (2π√(V(L/S)³)) and the kerf's viscous boundary layers. The panels' give pulls it lower, to two broad peaks near 225 and 255 Hz. Radiation alone gives a slit this long a Q of about 4, so the survey's viscous-only 7–20 is too high. | Kerf ports ≈ 150–350 Hz (calc/inf); Helmholtz's radiation Q 2π√(V(L/S)³) (pub); the Nigerian slit gong's cavity tuned near its lips (pub, Cannaday 2012) |
| What the box does to a tongue | A tongue pushes on the air and the slits breathe it out, as a bass-reflex cabinet does. Against the same tongue with no box air (20–120 ms), the tongues near the box's resonance gain 4–7 dB (G3 to C4), the rest 1–2 dB, and they ring a little shorter for what they radiate. No tongue gets quieter. The slits' air takes about 5 ms to answer, so near the resonance a note blooms: its first 5 ms are 3–8 dB under its level at 20 ms, and the two beat as they meet. Each tongue is tuned against everything that gives way at its frequency (slits, strips, panels, waves, the other tongues) so it lands on its note on the closed box. | Teponaztli: closing the cavity moves f0 < 0.6 Hz, gives louder, shorter tones (pub); the tongue nearest a box mode rings longest (pub, Thompson 2015); slit-gong beating is interference between lip and cavity (pub, Skubal 2006) |
| The top between the cuts | Three strips of top between the H's, 18 mm wide, run wall to wall: each is a clamped–clamped beam (4.730²/2π·√(EI/ρA)/L², then 2.76 × and 5.40 ×, a little under for shear), so 350–370, 950–990 and 1810–1850 Hz. Each board is a few percent off the next. Their glued ends take the joint's loss (0.012) on the share of each mode's strain within a land's length of the wall. The strips beside the front and back walls are glued down along them and hardly move. | Clamped–clamped beam (pub) |
| Bottom and walls | Seven modes of the bottom and the four walls, by Warburton's clamped orthotropic plate (H = ν·D_y + 2·D_k), 470–1120 Hz. Each board is its own piece of wood, so front and back and the two ends are a few percent apart. Loss η 0.025 for wood with glued joints. Every panel and strip moves the air by its net volume and its overlap with each standing wave. | The teponaztli body radiates faintly at 700–1000 Hz (pub) |
| The top at the roots | The land behind each root and the strips beside it are one plate. Across the grain, the 31 mm between a land's centre line and a strip's is a plate spring of 12·D_y/d³ per metre of land, weighed by both modes' shapes along the land. As a spring, ½k(w_land − w_strip)²: pressing a land down pulls the strip beside it down too. Each tongue's first mode couples to its neighbouring strips by about 1.5 % of their stiffness. Coupling only: each part's own stiffness already holds its share.

A tongue whose note lands on a strip's first mode (350–370 Hz; the key retunes the tongues, not the box) trades energy with it: a wolf, as on any instrument with a coupled resonance. It is at most about 9 dB down in its second 25 ms, then rings on. A knock on a land sounds that tongue's modes there: its first mode barely (0.02 of the tip's), its upper ones well. | Plate theory; E_R/E_L ≈ 0.1 (pub, Wood Handbook) |
| Mallet | A Hertzian contact, F = K·δ^1.5, with Hunt–Crossley loss, solved against the wood's own motion every sample. Four presets, interpolated: fingertip (8 g, K 3e5, its bone at 1.2 mm), yarn (14 g, 5e5), rubber superball (13 g, 1.2e6) and hard wood (10 g, 6e7). The hand comes on at up to 3 m/s, lifts as the ball lands (at least 0.1 m/s) and holds it loosely (100 N/m and a damper, a finger 400 N/m), so a ball can bounce back for a second touch; it is let go once it has been off the wood 3 ms. The last touch ends 3–4.5 ms after the first for a fingertip, yarn or rubber ball, and 0.3–0.45 ms for hard wood, sooner the harder it is played. Each shape is averaged over a contact patch a few millimetres wide. | Superball E 3–10 MPa, R 12.5 mm, so K 0.6–2·10⁶ and 1.5–3.4 ms on a rigid target (calc, Hertz); mallet speeds 0.5–3.5 m/s (fig, Bork); strike-spectrum peak 0.85/τ (pub) |
| Strike position | The finite-element mode shapes at the point struck. The second mode's node, at 0.75–0.77 of the length (the land moves it in from a clamp's 0.783), cuts that mode 15 dB or more. Near the root B1 is about 10 dB down and B2 relatively up. Off the centre line the tongue twists, and the twist is heard. | B2 node 0.783 L; B1 at 0.10 of the tip at 0.25 L (calc) |
| Sound | Two microphones 45 cm out either side of the player, in front. Each hears the volume acceleration of every tongue's face, slit, strip and panel. Lobed modes radiate as dipoles, their pressure growing as kd (d the distance between lobes) until the lobes are half a wave apart; how well a lobed mode radiates against a monopole (0.5 for a tongue's bending lobes and twist, 0.3 for a panel's) is a choice, and it sets how clacky a hard mallet at the tip is (B4 about +12 dB over B1 on the lowest tongue). Each tongue reaches each microphone after its own time of flight. The modes kept carry the strike itself: nothing is added while the mallet presses. | Fletcher & Rossing (monopole and dipole radiation) |

The model's cost does not grow with the number of mallets. The body is 85
oscillators stepped once per sample, about two thirds of a percent of a core, laid
out so every sum runs over contiguous memory. A strike adds a contact only
for the milliseconds it touches, and the body sleeps once it has rung
90 dB down.

## What is inferred

Nobody has published modal measurements of a modern wooden tongue drum:
its overtone ratios, per-tongue T60, its box's Helmholtz frequency or a
mallet's contact time on it. These come from the physics, checked against
the nearest measured instruments: teponaztli, kalimba tines and marimba
bars. Those inferred values are:

- the land's effective width
- the strips' and walls' scatter, and the panel dipoles' efficiency
- padauk's loss factor and shear modulus
- the joint loss
- the fingertip's parameters

## How it was checked

A reviewer was briefed to be hard to impress. Over four rounds it rendered
strikes and knocks and measured their spectra, partials, contact forces,
decays, the box's gain and bloom, sympathy, stereo and cost against the
published numbers. It also rebuilt the tongues in an independent
Timoshenko model: the second bending modes agree within 0.2 %, the fourth
within about 2 %. It signed off at 0.62 % of a core for single hits and
0.91 % for ten mallets (the hang costs about 0.5 %). On the way it found
and saw fixed:

- a wrong-signed land–strip coupling
- a box modelled as one plate rather than strips of top
- an unjustified shared-motion bus and an unjustified retract force
- a post-hoc shear correction in place of real Timoshenko elements
- dipoles weighted as (kd)², not kd
- a strike-radiation term wrong next to a support
- a retune on every strike that cost a millisecond when pads had different
  lengths

It accepted these deviations, which stand:

- **The wolf:** a tongue that lands on a strip's first mode (349, 360 and
  371 Hz) loses up to about 8 dB in its first 50 ms and rings quieter.
  In G major that is F#4: its fundamental is 9.4 dB under E4, with T60
  0.49 s. It can't be designed out while the key moves the tongues, and
  a test bounds it in every key.
- **The bloom:** near the box's resonance a note's first 5 ms are
  3–9 dB under its level at 20–25 ms, as the slits' air (Q 3.6:
  radiation 4.3, viscous 24) answers.
- **Level across the scale:** the box lifts G3–C4 by 4–7 dB, and at the
  default strike the fundamental falls from −6.4 dB on A3 to about
  −19…−22 dB on G4 and F#4.
- **Timbre at the extremes:** the default superball strike is round and
  nearly pure (B2 −34 to −66 dB under the fundamental). A hard mallet at
  the tip is clacky (B4 about +12 dB over it), set by the dipole
  efficiencies, which are choices.
- **Estimates:** G = 1.1 GPa, the land as a strip twice the tongue's
  width, the cross-grain coupling 12·D_y/d³ (about 1.5 % per tongue), the
  joint loss and the fingertip are estimates, documented above.
- **Decay per tongue:** a pad's length takes effect from its next strike
  and rescales that tongue's ring, a note already sounding included.

`tests/slitdrum_tests.cpp` checks the model against these numbers.
