# The cuíca

The drums page's kit after the hang is not a kit either. The whole pad becomes
a cuíca, the Brazilian friction drum: a damp cloth drawn along a bamboo stick
tied to the middle of a goatskin head, simulated as it is built
(`src/cuica.h`) and drawn as it is played (`src/drums.cpp`, `paintCuica`).

## Playing it

The pad shows the instrument twice:

- **On the left, the shell from the side.** It is open at the left. The
  stick stands in it from the knot in the head at the right.
  - A finger here is the hand with the damp cloth. Where it is along the
    stick is where the cloth is.
  - Drawing it along the stick, back and forth, is the stroke. How fast it
    moves is how fast the cloth is drawn, and how hard it presses is how
    hard the cloth grips.
  - Faster and firmer is louder. Too slow and it creaks. A finger that rests
    without moving makes no sound, because the cloth only speaks while it
    slides.
- **On the right, the head from outside,** with the knot in its middle.
  - A finger here is the thumb of the other hand, pressing the head.
  - Nearer the knot and harder is higher, up about an octave (a little
    more with fast strokes). The
    drawn head is the middle of the real one, close up: the note climbs
    steeply in the last centimetre or two to the knot, so that part gets
    most of the drawing.
  - Out towards the rim it hardly bends the note, and lifting the finger
    leaves the open note.
  - Sliding it in and out while the other hand rubs gives the cuíca's laugh.
- **Keys, rolls, grooves and loops.**
  - Each key plays a stroke: oom, ka, kee, tik, the laugh up, the fall,
    rise, wail and more. Consecutive strokes push and pull in turn, as an
    arm does.
  - Holding a key rolls its stroke, and a groove plays the cuíca's samba
    part.
  - What the fingers play goes into a loop with Space, like any part, and
    plays back as the cuíca whatever kit the page is on later.
- **Tuning:** the cuíca has no key. Its open note is wherever the shell's
  air and the stick-loaded head put it (about 190 Hz), and the thumb is its
  pitch.

## The model

Everything is in SI units, integrated sample by sample. Each mode is a
damped oscillator, `u' = d·u − c·x + f·dt²/m` and `x' = x + u'`, with `d`
and `c` set so its poles are exactly the mode's frequency and decay. The
membrane modes' spring term follows the tension sample by sample.
Everything that couples is a force between them.

| Part | What it is | Set from |
|---|---|---|
| Head | Goatskin, σ = 0.38 kg/m² (0.3 mm at 1260 kg/m³), on a 9" (a = 114 mm) shell, at **4500 N/m** (15 MPa in the skin). 21 Bessel modes, J_m(j_mn·r/a)·cos mθ for m = 0–5, each with modal mass σ∫φ². The skin's own loss is Q = 35. | Statsenko 2015: 0.3 mm goatskin, FEM fit 4470 N/m; 9" the standard size |
| Stretch | ΔT = Y/(2S)·∫\|∇w\|², with Y = E·h/(1−ν) = 1.65e5 N/m (damp goatskin, 330 MPa). Every mode's spring scales with T(t)/T₀. At this tension it moves the note by hundredths of a semitone. | Li 2022 (in-situ goatskin E, damp); Rossing 1992 (ΔT ∝ amplitude²) |
| Stick | Bamboo, 27.5 cm (the shell less an inch) by 5.6 mm, 750 kg/m³, 18 GPa: **5.1 g**, a third of the skin's mass. Free–free along its length: a rigid motion and its first compression mode (8.9 kHz; the next, at 18 kHz, is past what the step holds cleanly). | Gope 23 cm sticks; Remo: shell length less 1"; bamboo E and ρ; stick 3–8 g |
| Knot | A spring of 1.5e6 N/m (cord knotted through soaked skin, dried hard) and 3 N·s/m between the stick's end and the skin over a 6 mm patch, 1.4 mm off the middle. The patch averages each mode as 2J₁(kρ)/(kρ) (Graf). It is solved implicitly against both sides' give. | Statsenko: drive patch 5 mm across, 1.4 mm off centre |
| Shell's air | A 30 cm cylinder of the head's radius, open at the far end: a plane-wave waveguide each way, Z₀ = ρc/S. The open end reflects inverted through a low-pass (\|R\| ≈ e^{−(ka)²/2}), with the 0.6133a end correction in the length. The player's forearm is in the bore and takes half the reflection. The head pushes Z₀·U into the shell and feels the returning wave. | Levine & Schwinger 1948; makers' 9"×30 cm |
| Front air | The head's outer face radiates into open air: the radiation mass ρ·0.61a/S in parallel with ρc/S (a high-pass of the volume velocity at c/0.61a). | unflanged piston |
| What that makes | The head with the stick's mass on it (its own (0,1) is 364 Hz in vacuo) and the shell's quarter wave (232 Hz), with the forearm damping the shell, give one dominant resonance at about **190 Hz**. The open stroke sounds at 190–200 Hz. The stick-loaded (0,2) sits near 470 Hz. | open tone 140–300 Hz measured |
| Cloth | 0.5 g of wet cloth and fingertip skin, held by the fingertips in shear: 0.2 N/mm plus 0.1 N/mm per newton of pinch, softer through a wet cloth than a bare finger pad (a finger pad's tangential stiffness grows with its load), and 15 N·s/m (pulp in shear is lossy). Pulp in shear creeps: a held load relaxes with a 50 ms time constant, so a hand resting on the stick doesn't sit on the edge of breaking away. The fingertips sit on an arm that follows the player's motion at about 40 Hz and gives under the cloth's pull (0.3 kg), at no more than 3 m/s. | finger pad 1–3 N/mm (Pataky 2005) |
| Grip | Up to 20 N. N = 20 N × (0.03 + 0.9·v·(1 + 0.5·(pinch − 0.5)) + growl), with v the arm's speed in m/s. A deliberately firm pinch (over 0.8) bears down with up to 0.7 more whatever the speed, fading out as the draw of the last 0.1 s passes 0.4 m/s: slow and firm, the cloth holds the stick most of each cycle, the low grunt (about 150 Hz). The player squeezes in step with the draw, as a bow arm leans in as it speeds up. Keeping the load in proportion to the speed keeps a stick–slip note's period, so the note holds whether the stroke is quick or slow, speeding up or slowing into the turn. The page's pressure is the pinch: it leans on how much squeeze per unit of speed. A firm one holds the stick longer each cycle at any speed, which makes the note a little lower and rougher; a firm press with a slow draw is the growl. | pinch 2–15 N (estimated); DAFx 0–20 N |
| Friction | μ(v) = μd + (μs − μd)/(1 + \|v\|/v₀), with μs 0.9, μd 0.45 and v₀ 0.5 m/s: a wet cloth's friction falls over the whole range a cuíca is drawn at, so fast strokes keep speaking. It is solved each sample against the contact's admittance (the stick's modes and the cloth): stick while the force needed is under μs·N, otherwise slip on the curve (a quadratic), with hysteresis. The contact ages, as in rate-and-state friction: a cloth that has sat still on the stick grips up to 0.1 harder in μs, so μs stays at or under 1.0 (measured ageing is a few hundredths per decade of hold time) (water films thin, fibres settle into the bamboo). Sliding wears that off over about 4 mm of travel. It heals in 0.15 s while sliding, and in 30 ms while the arm is all but still at a turn. While the cloth slides, the wet fibres catching waver the load by 25 % in a band round 1.2 kHz. The cloth's own hiss is noise ∝ slip speed × load, 1.5–6 kHz. | DAFx 2021 friction drum (μs 0.8, μd 0.3); wet fabric 1.5–7× grippier than dry; ageing: Dieterich, Rice & Ruina |
| Thumb | Exponential pulp, F = A(e^{δ/δ₀} − 1) with A = 0.3 N and δ₀ = 0.35 mm, so its stiffness (F + A)/δ₀ grows with force: 4 N/mm at 1 N, 58 N/mm at 20 N. Hunt–Crossley loss λ = 0.05 s/m. It is solved implicitly against the skin's admittance at its patch, 8 mm in radius, with the modes averaged over it. The patch's middle comes no nearer the knot than 11.4 mm (r 0.1), the pad of the thumb against the cord's knot. On top of the knot, the thumb-stiffened fundamental and the stick-loaded (0,2) would fight for the cloth. A 30 g bone is pushed by the hand (up to 20 N), eased in over 35 ms and lifted over about 70 ms. It slides at no more than 0.5 m/s. | fingertip stiffness grows with load (Serina 1997, Pawluk & Howe 1999); thumb 5–20 N |
| Sound | Two microphones in front hear the volume acceleration of the head's front face. They also hear the air leaving the open end, which arrives round the shell 0.3 ms later at the left ear and 0.8 ms later at the right, low-passed (diffraction). The cloth's hiss is added. | small-source radiation, ρ/4π·dQ/dt |

Pressing beside the knot, the pulp's stiffness adds to the spring the
stick's mass rides on. The note climbs smoothly from about 190 Hz to about
365 Hz at a hard press with a medium stroke, and a little higher with fast
strokes. That is about an octave, the 9–10 semitones the recorded pressed
passages sit above their open tone, and more. Further out the press holds a
patch of skin the stick hardly moves, so it bends the note less, and near
the rim not at all.

A stroke starts on a settled contact, from rest or from a turn, and the
sliding wears its extra grip off. The squeeze keeps pace with the speed,
so a stroke doesn't sag as the arm slows into the turn. A pad's or key's
stroke also starts squeezed: once the cloth first slips out of the turn,
the player bears down up to 1.4 times as hard per unit of speed over a few
tens of milliseconds, then eases into the glide over about 80 ms. (The
extra squeeze waits for that first slip, so the cloth breaks away at the
ordinary load and speaks promptly.)
The note starts on the flat side and climbs 2–3 semitones onto the head's,
then holds, as the recorded strokes do (+2 to +3.6 semitones on the Pearl
clip). A finger on the page is its own player: its pressure is its
squeeze, and its strokes hold the head's note.

A known limit: quick 55 ms flicks of 4–7 cm ease down by half a semitone
to a semitone over their loud part. Slower strokes, and small flicks, are
level or rising. This is a property of a single-mode stick–slip oscillator
under a draw that slows before it has settled. Every route tried against it
moved it the wrong way or broke something else: the load-to-speed ratio,
contact ageing, the fingertip shear's stiffness and loss, a smaller gap
between static and kinetic friction, the Stribeck speed, holding the squeeze
through the turn, and easing in.

A firmer pinch holds the stick longer each cycle, which lowers the note. The one practitioner remark, "a tighter grip raises the
pitch", goes the other way.

The thumb's patch stops 11.4 mm from the knot. On the knot, the
thumb-stiffened fundamental meets the stick-loaded (0,2) near 470–500 Hz
in an avoided crossing: the note jumps about 4–5 semitones and flips
between the two. Neither spreading the pulp's stiffness over its patch nor
growing the patch with force (to 16 mm) removes that in the linear
model.

One hand and one thumb cost a few tenths of a percent of a core. The body
is 21 modes, 2 stick modes and a short waveguide, stepped once per sample.
Each hand is one friction solve over the stick's 2 modes, and each thumb
one contact over the head's 21 modes. At most 3 hands and 3 thumbs: the
player's, a loop's and a stroke's. The body sleeps once nothing touches it
and it has rung 90 dB down.

## How it was checked

`tests/cuica_tests.cpp` checks the model against the numbers in
`docs/cuica-research.md`:

- the open note and how far the thumb bends it;
- that nearer the knot and harder is higher, smoothly both ways, with the
  open note back after a lift;
- that a stroke glides up and then holds, and the page's quick push and
  pull hold the head's note;
- that the pressed note stays put across stroke speeds;
- that faster is louder, and the first stroke is as loud as the rest;
- that it speaks across most of the speeds and pinches it is played with;
- that the cloth sticks and slips once per period;
- that the fundamental leads the spectrum, and nothing fizzes near the top;
- no ghost notes and no runaway energy;
- the page, loops and cost.

A reviewer was briefed to be hard to impress. Over eleven rounds it built
its own harnesses, rendered strokes through the model and through the app,
and measured the following against the recordings and the literature:

- pitch against speed, pinch and thumb, with playability maps;
- stroke contours and onsets;
- turn transients, spectra and formants;
- ghost notes, stereo and cost.

It signed off on the model as it stands. It accepted these, which stand:

- **Quick flicks:** they ease down, as described above.
- **The top of the range:** it stops about 12 semitones over the open note,
  with the thumb beside the knot rather than on it.
- **Short pressed strokes:** "ka" and "mid" start 2 semitones under their
  held note, the opening squeeze scooping them.
- **The growl:** it speaks in a narrow band (a pinch of 0.9 or more, drawn
  at about 0.2–0.3 m/s). It lacks the recordings' formant near
  1.15–1.2 kHz.
- **The cloth's hiss:** it is added at the microphones rather than
  radiated by the instrument.

`docs/cuica-research.md` holds the literature survey these numbers come
from, with its sources.
