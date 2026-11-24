# The daxophone

The Lead page's **Daxophone** (free improv) is Hans Reichel's instrument
simulated as it is built (`src/daxophone.h`) and played as a lead
(`src/lead.cpp`): a thin maple tongue clamped in a block, bowed on its free
end, a wooden dax pressed on it and moved along it to set how much of it
sounds, heard through the contact microphones in the block. Every voice of
the line, melody and harmony, is a daxophone of its own.

The survey behind it is `docs/daxophone-research.md`; **inf** below marks a
value I estimated where nothing is published. Nothing here has been
checked against a recording of a real daxophone: no measured spectrum of
one is published, and none was at hand.

## Playing it

- **The finger is the dax.** The pitch is wherever the finger is: it glides
  between the degrees as freely as the dax slides, landing between two
  cells sings between them, and a slide is one continuous glide. With the
  line following the chord (§) it rests in the key, as every other lead
  does.
- **Pressure is the bow's weight,** set against the window in which this
  tongue sings each note (a player's arm learns it):
  - the lightest touch bows too lightly: the bow skates and the tongue
    barely speaks (30 dB and more under the note);
  - an ordinary press sings, in tune, at every note from F2 to C7 and
    every touch (from C♯2 to E2, low on the pad, it can fail to speak);
  - harder is louder and rougher;
  - the hardest goes past the window's roof: the hair holds on too long.
    From a fresh attack the note is lost, the tone falling 2.5 to 6
    semitones under it, rough around A3–E4, clean from A4 up; at A2 it
    still sings (see the regimes below). Pressed up to it gradually, a note can
    stay sung, flat and rough;
  - on the way up, gradually, a light bow can catch an upper mode instead
    of the note: A3 pressed slowly through the lighter half whistles on
    its sixth harmonic, about 20 dB under the sung note.
- **How high:** high on the pad bows on the very end, where every mode
  swings most under the hair; lower, an eighth of the bowed part in along
  the edge, a few decibels rounder (most on low notes).
- **A flatter finger** presses the dax more lightly (from 19 N down to 8):
  it rocks more easily, which flattens the bowed note (the ear brings it
  back) and brings its harmonics up, and its cardboard loses more.
- **Holding:** the bow turns at the end of its stroke (four seconds of a
  double-bass bow). A note played again while one sounds changes the bow.
  Detached notes slide in briefly from the last (the dax never leaves the
  tongue).
- **Lifting:** the bow lifts and the tongue rings out on the dax for a
  fifth of a second or so.
- **Vibrato:** the hand rocking the dax, late and gentle; the finger's own
  wiggle is a wider one.

## The model

Everything is in SI units, the tongue's modes integrated exactly sample by
sample, the friction solved implicitly at the bow every sample.

| Part | What it is | Set from |
|---|---|---|
| Tongue | Sugar maple, 330 × 30 × 5 mm: E_L = 12.6 GPa, ρ = 705 kg/m³, G_LT = 0.79 GPa (0.063 E_L). Open, its fundamental is 31 Hz (34 with its outline) | [WIKI] dimensions; [FPL] maple |
| Outline | Width 1.0, 0.85, 1.15, 0.8, 1.1, 0.9, 0.7 × 30 mm at 0, 0.15, 0.35, 0.55, 0.75, 0.9 and 1 of the length; its middle off the clamp's centre line by 0, 0.05, −0.08, 0.12, −0.05, 0.1, 0.15 × 30 mm there (**inf**: a waisted, lobed outline of Reichel's kind; none is published in numbers) | [WIKI]: sawn to a free outline |
| Modes | Euler–Bernoulli bending on Hermite elements and Saint-Venant twisting on linear ones, coupled through the outline's mass off the axis; solved (Cholesky and Jacobi) for 64 dax positions when the Lead starts (about 0.1 s). Checked: 1 : 6.267 : 17.55 : 34.39 and f₁ = 0.1615 h/ℓ² √(E/ρ) within 3 %, twisting (2n − 1)c_t/4ℓ within 3 % | [BLE] |
| Two parts | In front of the dax, the bowed part: a cantilever held at the dax against swinging and twisting, and sprung against rocking by 70 EI/L (the dax's cardboard patch, **inf**) plus the part behind (its static stiffness, 4 EI/d). Behind the dax: its own modes, clamped in the block, held at the dax by the same contact, driven by the moment and torque the bowed part puts on the dax; at the clamp, its modes plus, held still, what the rest give. Its 12 families are followed from where it is longest (the dax near the tip) toward the clamp, so every one of its modes under 4 kHz (at most 11) is simulated at every dax position. One way only: the part behind doesn't act back (see the open points) | calc |
| The dax rocking | Its curved face holds at most the press times half its patch (6 mm): 8 N (lightly) to 40 N (firmly), 0.05–0.24 N·m (**inf**); an ordinary touch presses at 0.35 of the way (19 N). What it would hold beyond that flattens off (a tanh), and what it fails to hold works back on the bowed part's modes. A nonlinear support. Under an ordinary bow, against a dax no bow could rock, without the ear (E3–A5): pressed as lightly as a flat finger presses it, the note falls 66–149 c and its harmonics rise 6–22 dB; at an ordinary press, 28–89 c and 1–10 dB | [PW-D]: the yodel, the heavier dax; calc |
| Glides | The modes are followed by their shapes from one dax position to the next (64 positions; a twisting and a bending mode that cross between two of them keep their own motion), their frequencies and weights interpolated, the step's coefficients blended across each 32-sample block. Each mode's state is its swing and its speed over its frequency, so a blended step is a rotation that only shrinks; a retuned mode keeps its action (energy over frequency), carried a sample at a time | [LL] |
| Bow | Rosin's friction a hyperbola in sliding speed, μ = 0.35 + 0.45 · 0.1/(0.1 + \|v\|), 0.8 gripping; its grip varies 4 % from grain to grain, and sliding, by up to 25 % as fast as the slide (the hair's strands catching: the bow's noise, each catch let go at once, so the noise has nothing below a few kilohertz and leaves the friction's mean, and so the period, alone) (**inf**). The bow lands with its weight in 2 ms, drawn up to speed in 30 ms. At 0.3 of the half-width across the end; along it, from the very end to an eighth of the bowed part in | [SW], [GAL], [GUE] |
| Bow force | A table of the window (N, at 0.2 m/s) in which the tongue sings each note at every touch the Lead gives, every three semitones from C2 to C7, from the model's own scan below: what a player's arm learns. An ordinary press bows at the window's middle, or where it is narrow a fifth over its floor; lighter, in proportion (to the power 1.3); harder, slowly then steeply, to 2.5 times the roof at the hardest | calc |
| Bow speed | 0.15–0.25 m/s, faster for a firmer landing; turns every 4 s (**inf**) | |
| The player's ear | The dax goes where the bowed part's fundamental is the note, corrected by what the player has learned of the tongue: for each of 64 places along it, how far the dax goes past where the mode alone would put it (a bowed tone sits flat of its mode, more as the dax rocks). The learning: once the bow is drawn, the fundamental's zero crossings are timed to a fraction of a sample, and while they are within three semitones of the note the place is nudged over 60 ms until they are on it, by as much as four semitones. The Lead's tongues share one player's learning. A place played for the first time settles in about a third of a second (within 40 c from 160 ms, within 5 c from 0.2–0.3 s); one played before, within 25 c from 160 ms (a note's first tenth of a second stands up to 30 c sharp, before the rocking's flattening has built up). The learning moves the correction read back where the note lies by the whole step, wherever it lies between two places | calc |
| Losses | Wood: 0.008 along the grain, 0.02 twisting (**inf** within [ON], [OBA]). The dax's hold: what it holds of the rocking loses through the cardboard at 0.25 (more for a lighter press, **inf**), what passes behind it at 0.1. Behind the dax, the block and its stand take 0.06. Air: 1 s⁻¹ | |
| Pickup | The clamp's moment: two piezo discs either side of the middle, wired in parallel, so a twist cancels. Then the block: rolled off under 45 Hz, a broad lift (Q 1.4) at 2.8 kHz, little over 8 kHz (**inf**) | [WIKI], [PW-B] |
| Level | The clamp hears what passes the dax, which jumps wherever a mode behind it meets the note or a strong harmonic. The Lead evens three quarters of that out: the tongue measures what reaches the clamp against the bowed part's energy over 80 ms (before that, 0.69 of the part behind's response at the fundamental, what is heard on average), never lifting by more than 25 dB. Evened, an ordinary note is within ±3 dB from E2 to C7 | calc |

### The bow's window

The model's scan: at each of the four touches the Lead gives (the dax
pressed at 0 and 0.35, the bow on the very end and an eighth in, 0.3
across), at 0.2 m/s, the ear on; the force from 0.02 N up in steps of
1.15×. **O** the note within 15 cents, its tone clear and within 20 dB of
the loudest, at all four touches; **.** anything else (silence, an upper
mode, a broken tone, or a touch where it fails).

```
  65 Hz  ................OOOOOOO......................   0.19–0.43 N
  78 Hz  ................O............................   0.19
 110 Hz  ....................OOOOO....................   0.33–0.57
 220 Hz  .......................OOOOOOOO..............   0.50–1.32
 440 Hz  ............................OOOOOO...........   1.00–2.01
 622 Hz  ..............................OO.OOO.........   2.01–2.66
 880 Hz  ................................OOO.OO.......   1.75–2.32
1047 Hz  .................................OOOOO.......   2.01–3.52
1760 Hz  ....................................O........   3.06
2093 Hz  .....................................OOO.....   3.52–4.66
```

The floor rises with the note, as Schelleng's does with a shorter string.
The roof is irregular, and at two notes (D♯2, A6) a single step of the
scan sings at all four touches: the window narrows where a mode behind
the dax or the tongue's own meets the note's harmonics. At D♯2 that is
too narrow to rely on: with the Lead's 2 ms landing, no force sings it at
all four touches (the playable range is F2 to C7 at every touch).

### The regimes, measured

From a fresh attack, the dax at an ordinary press, the bow at 0.3 along,
the ear on (the noise is measured between the harmonics of the tone
actually sounding, on its own pitch; the same on gcc and clang where a
single figure is given):

| | the lightest (1/20 of ordinary) | ordinary | the hardest (2.5 × the roof) |
|---|---|---|---|
| 110 Hz | skating, 34 dB under | in tune, noise 66 dB under the harmonics | still sung, rougher (noise 40 dB under) |
| 220 Hz | skating, 36 dB under | in tune, noise 64–68 dB under | lost: a tone 2.5 semitones down, rough (noise 28–33 dB under) |
| 330 Hz | skating | in tune | lost: 3 semitones down, rough or nearly clean (noise 30–41 dB under) |
| 440 Hz | skating | in tune | lost: a clean tone 3–5 semitones down |
| 660 Hz | skating | in tune | lost: about 6 semitones down (noise 40 dB under) |
| 880 Hz | skating | in tune | a clean tone 2.7 semitones flat |
| 1320 Hz | skating | in tune | a clean tone 3.6 semitones flat |

Where a broken note lands is the same from one build to another; whether
what is left is rough or clean, near A3–E4, turns on the attack's last
detail and differs between compilers' rounding.

### Where its harmonics come from

The friction force is rich: in the stick-slip its harmonics stand as high
as its fundamental. The bowed tongue passes few of them on: at the dax,
its second harmonic is about 15 dB under its fundamental at every note
from 220 to 523 Hz, the fundamental's own resonance lifting it over the
others (the hair grips only near the peak of the tongue's speed, 15–20 % of the
period, as on a bowed bar, whose upper modes don't fall on the harmonics).
What reaches the clamp is then shaped by the part behind the dax: where
its first bending mode sits on the note, from about E4 to G4 on this
tongue, the clamp hears the fundamental lifted and the harmonics 8–15 dB
less, and those notes are the purest on the neck. That is where, coupled
both ways, a wolf would be (see the open points). The harmonics come up
with:

- **the dax rocking** (above), the more the lighter it is pressed;
- **the bow's force**: twice the ordinary is more than 6 dB richer than
  half the middle, on average over the neck;
- **where it is bowed**: the very end a few decibels rougher than an
  eighth in (5 dB at A2, and the other way round at E5);
- **the modes behind the dax and in front**, lifting the harmonics they
  meet.

At an ordinary press and force (the bow half way along), harmonics 2–20
against the fundamental, in dB:

| A2 | E3 | A3 | E4 | G4 | A4 | C5 | E5 | A5 | E6 |
|---|---|---|---|---|---|---|---|---|---|
| −13 | −7 | +1 | −19 | −26 | −13 | −6 | −10 | −19 | −18 |

## Keeping it in real time

- **A tongue** is 16 modes in front of the dax and 12 behind, a friction
  solve and the dax's rocking: about 0.47 % of a core with the project's
  flags while the dax moves, 0.28 % while it holds still.
- **The dax still** (its moves under a hundredth of a cent, the ear's
  nudges, adding up first), its coefficients are not worked out again or
  blended.
- **Sleep.** A voice ends once its bow is off and it has rung 70 dB down.
- **Build cost.** The 64 dax positions' modes are solved once, when the
  Lead starts, in about 0.1–0.15 s.
- **Guards** (numerical, not physics):
  - the dax's rocking works only on the bowed part's modes under 7 kHz,
    tapering from 4 kHz, always (an explicit push on a mode swinging more
    than a radian a sample isn't stable);
  - its coupling is scaled back if it would take more than 0.9 of the
    tongue's stiffness or exceed what an explicit step holds, which
    played notes never reach;
  - a dax thrown more than a table step in one block doesn't rock for
    that block;
  - families fade out over the last stretch to Nyquist.

## How it was checked

`tests/daxophone_tests.cpp`:

- the straight tongue against Euler–Bernoulli and Saint-Venant;
- every mode of the part behind the dax under 4 kHz followed, at all 64
  positions;
- the outline moving the upper modes and coupling twist into swing;
- the dax setting the pitch, rising all the way from under 40 Hz to past
  1.5 kHz, the bowed tone within a semitone of the mode without the ear;
- the ear bringing every note from 110 to 1320 Hz within 8 cents, and
  never swinging past a note at its onset (as above);
- every semitone from A2 to E6 singing its own note at every touch the
  Lead gives (the scan behind the window covers F2 to C7);
- an octave's glide up and back and a semitone's vibrato without a click
  (and the measure hearing a step of 0.3 of the swing);
- the regimes: skating 25 dB under at a twentieth of the force, in tune
  and clean (noise 40 dB under) at an ordinary one, more than 6 dB
  rougher from a light bow to one just under the roof, the note lost at
  the hardest at A3, E4 and E5 (more than two semitones down), the floor
  rising with the note;
- the dax rocking, at both presses, as above;
- where it is bowed: the end rougher than further in;
- the dax's damping: 40 dB down within a second, quicker for a lighter
  press;
- ten seconds of abuse: finite, never holding more energy than the bow
  could have given it (the whole bound carried up or down with the swing
  as the dax retunes it, energy over frequency), and quiet after;
- on the page: the finger as the dax, the key only with §, a slide's
  continuous rise, pressure as the bow (in tune, rougher and louder);
- the budget: a tongue under 1 % (0.47 % measured on a quiet core), and a
  line with three harmony voices through the studio.

## The review

Critiqued by a reviewer briefed as a bowed-string acoustician who knows
Reichel's recordings. Round 1 (not signed off) found, and this version
fixed or answered:

- the first block of a fresh voice 17 dB too loud (the level read before
  it was worked out): fixed;
- every detached note scooping in (the ear starting over each note): the
  ear now remembers, and is quicker;
- the regimes advertised but not happening (a whistle that was silence, a
  croak that was a clean flat tone): the force now follows the measured
  window, and the regimes are described as measured;
- the claim that the dax's rocking made the harmonics at a firm press: it
  doesn't; it does at a light one (measured above), and the docs say so;
- loose tests (the croak passing on any clean tone, the corner test on
  harmonics 50–70 dB down, a mono check true by construction): replaced;
- the cost (0.4 %, not the 0.25 % claimed) and its waste (coefficients
  blended with the dax still, `pow` per mode): fixed.

Round 2 (not signed off) found, and this version fixed or answered:

- **a bug:** the part behind the dax's families were chosen where it is a
  centimetre long, so most were twisting modes and its bending modes went
  unsimulated over most of the travel. They are now followed from the far
  end, and a test checks every one under 4 kHz. This changed the tone
  (A4 from −22 to −13 dB of harmonics) and the level evening;
- **dead and flat notes** at touches the force window wasn't scanned at:
  the window is now the forces at which all four of the Lead's touches
  sing, and a test plays every semitone from A2 to E6 at each;
- **the ear swinging past** a note at its onset (−60 then +77 c): it now
  listens once the bow is drawn, more slowly, and learns each place on
  the tongue;
- **the explanation of the pure middle** (an antiresonance, which isn't
  there at those notes): corrected above;
- **the shear at the dax**, which is not "about 1 N" (below);
- **the regimes, the rocking's numbers, the CPU and the guards** as
  documented: re-measured and corrected; and stale comments.

Round 3 (signed off once these landed) found, and this version fixed:

- the abuse test's energy bound only ever growing (it reached infinity, so
  it could not fail): it now follows the swing down as well as up;
- the croak test and the regimes table measuring noise against the
  nominal note's harmonics, which read a clean flat tone as broken: both
  now measure on the tone actually sounding, and the table says which
  notes break and which fall flat;
- the shear at the dax quoted from the old window: re-measured (below);
- the claim that every note sings at every touch: true from F2 to C7, not
  from C♯2 to E2 low on the pad;
- the richer-with-force test crossing the window's roof: it now compares
  inside the window;
- E5's harmonics (−10 dB, not −16), and stale comments.

Left open:

- **The pure middle.** E4–G4 have their harmonics 18–25 dB down, and the
  brightness varies by about 30 dB across the neck. One-way coupling
  through the dax turns each place where a mode behind it meets the note
  into the purest note, where a real tongue would more likely wolf. If
  Reichel's E4–G4 show their second to fifth harmonics within 15 dB of the
  fundamental, the one-way coupling is the cause.
- **One way through the dax.** The part behind the dax doesn't act back
  on the bowed part: no wolf (pitch pulled or beating where one of its
  modes meets the note), no yodel from it. Coupled both ways (the review's
  whole-tongue model confirms it), the modes veer about 2.2 semitones apart
  near 385 Hz, with further crossings near 830 Hz and 1.4 kHz, all in the
  Lead's range: a playable lead would need the ear to cope with wolves
  there.
- **The hardest press,** from a fresh attack, loses the note from A3 up,
  the tone falling 2.5 to 6 semitones under it, rough around A3–E4 and
  clean from A4 up, rather than a croak; at A2 it still sings (it
  period-doubles at about 1.5 × the roof).
- **The dax pressing from one side.** It presses down only; it can't pull
  the tongue up. Under a flat finger the shear at the dax is 0.5–0.9 of
  the press over most of the neck, exceeds it at D♯5–E5 and from A♯6, and
  under the hardest bow reaches the press at F♯5. A real dax would lift
  there and slap back (a buzz); the model keeps the contact closed.
- **The dax has no inertia.** It is held by springs, not a 100–200 g block
  in a hand whose inertia to rocking is about as stiff as the cardboard at
  220 Hz, so Reichel's "a heavier dax intonates better" is not modelled.
- **The bow's noise is quiet.** The hair's noise has nothing below a few
  kilohertz (lower, it wandered the pitch), and the tongue passes little
  of it: between the harmonics of an ordinary note it stands 57–80 dB
  under them, where Reichel's recordings have audible bow noise (by the
  reviewer's recollection).
- **No real daxophone has been measured.** Every comparison with the
  instrument is by recollection; the numbers above are predictions a
  recording could check.
- **The cost:** about 0.47 % of a core per tongue while the dax moves
  (vibrato included), 0.28 % held still.
