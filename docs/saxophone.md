# The saxophones

The Lead page has two saxophones, each simulated as it is built
(`src/saxophone.h`) and played as a melody instrument (`src/lead.cpp`, the
`Sound::Sax` block):

- **Tenor sax** (R&B): the lead of R&B, soul and rock'n'roll.
- **Alto sax** (smooth jazz): a smaller horn a fourth higher, brighter.

The tenor sounds a fourth under the neck, the alto on it (see
[The tenor against the alto](#the-tenor-against-the-alto)).

Every voice of the line, melody and harmony, is a horn of its own, so the
harmony rows make a section. The survey behind every number is in
`docs/saxophone-research.md`. Citations in brackets refer to it, and
**est.** marks a value I estimated where nothing is published.

## How it differs from the Lead's Reed

`Reed`, the Accordion, is the free reeds of an accordion. Each reed is a
brass tongue swinging through a slot, made as a saw and a pulse locked to
one phase (`ReedOsc`), with no air column. The tongue's own stiffness sets
its pitch. Its "clarinet" register is the accordion's name for one reed,
not a clarinet's tube, and since a saw and a pulse have every harmonic,
`Reed` is not clarinet-like.

The saxophone is the opposite case:

- The reed has no pitch of its own worth speaking of. It is a valve, shut
  by the breath, that feeds a bore.
- The bore's resonances set the note, the keys set the bore, and the bore
  is a cone. Its full harmonic series comes from its shape, not from the
  reed.
- On a cylinder the same reed would sound the odd harmonics, as a
  clarinet's does. `theConeHasTheWholeHarmonicSeries` builds that cylinder
  and checks it.
- Nothing in `Reed` changed.

## Playing them

- **Where you play:** the neck picks the note. Low on the neck is the
  horn's own first register; up the neck the octave key opens.
  - Above the palm keys a smaller horn stands in for the altissimo (see
    Open points).
  - Below the low B♭ a larger horn plays, the same one scaled up.
- **Pressure is the breath.** A light touch is a subtone: the breath just
  over the reed's threshold, and the reed held nearly shut by the lip, so
  the closing pressure, and with it every pressure in the horn, is lower.
  A hard press is a beating, bright edge. From subtone to edge the horn
  gains more than 10 dB, and its spectrum widens.
- **The finger's height is the embouchure.** Low is much lip and a dropped
  jaw: the reed cushioned onto the lay, airy. High is a firm lip, the reed
  standing open: brighter.
- **A flatter finger growls:** the player hums through the horn. A press
  at the very top growls a little too.
- **Notes are tongued.** A fresh note starts with the tongue on the reed
  for its first 15 ms while the breath rises, then lets go over 3–8 ms
  (quicker when harder). A detached note on a horn still sounding is the
  tongue coming onto the reed over 2 ms, holding the slit shut for about
  12 ms (the "t"), and coming off the same way. The tongue closes the slit;
  it does not damp the reed's swing, and never resets it.
- **Legato slurs.** The keys move under the breath over 8 ms, and every
  resonance slides from where it was to where the new fingering puts it.
  While they move, the horn is rendered four samples at a time, so nothing
  steps at a block's edge. Across the octave key the note moves to another
  resonance of the tube, and the air swinging at the note is carried into
  that resonance's slot, so the slur lands on the note.
- **Bends.** A slide, the bend wheel, a twist, the scoop into a note and
  the vibrato are all the lip: an equivalent volume at the mouthpiece,
  bending the note mode onto the pitch. Each resonance moves by its own
  compliance as long as it is a resonance that can carry it: Q at least 3
  and its compliance at least six times the lip's change, handed over
  gradually down to three times. Broader ones move with the note. Every resonance stays within a factor of 1.25 of
  where it was. The keys change only on whole semitones (equal-tempered
  from A440), as fingers do. So a bend of more than half a semitone
  re-fingers: it changes the key, as a player smearing between keys does,
  rather than staying on one key and costing the tone as a real deep lip
  bend would. A bend of up to 45 cents either way keeps the level within
  3 dB (tested). The vibrato is a
  jaw vibrato of about 5.2 Hz (alto 5.4) and up to ±0.2 semitone
  [APFZ14, TD00].
- **The player's ear.** The player hears each note against the pitch wanted
  and lips it in over a few hundredths of a second. The player also
  remembers, for each note:
  - its lip correction, as the ear last heard it while locked on the note
    for a tenth of a second, kept when the note ends (slurred away from or
    lifted), never more than 100 cents;
  - its breath (a note that will not speak is blown a little harder);
  - a cracked note: a note heard more than a semitone off, or barely in
    the sound, is caught. In the upper register a note that has fallen is
    given more support; any other wrong regime is blown less hard. The
    player then plays that note that way, and forgets it again over some
    seconds.

## The tenor against the alto

Until this change the tenor was the alto scaled up by 2^(5/12) in every
length, with its reed's resonance scaled down. Its dimensionless dynamics
were the alto's, and played on the same neck cell it was the alto a
fourth down, in the same place: the two sounded alike. Now its parts come
from a tenor's own measurements where there are any (section 7 of
`docs/saxophone-research.md`):

| Part | Alto | Tenor | Source of the tenor's |
|---|---|---|---|
| Cone half angle | 0.028 rad (1.60°) | 0.0265 rad (1.52°) | [CSW09]; 1.55° in the CT [JG21] |
| Radius at the mouthpiece | 5.5 mm | 6.7 mm | the CT [JG21 Fig. 3], digitised |
| Missing cone | 0.196 m, 6.2 ml | 0.253 m, 11.9 ml | from the two above |
| Mouthpiece (model cylinder) | 30 × 6 mm radius, 3.4 ml | 32 × 8.5 mm radius, 7.3 ml | radius: the shank of 41 tenor mouthpieces [MPW, sec.]; length **est.** |
| Tone hole b/a | 0.6 **est.** | 0.54 | [JG21], measured on a tenor |
| Lattice cutoff (model) | 1.2–1.5 kHz | 0.7–1.0 kHz | against 760 ± 250 Hz measured [CSW09] |
| Reed | 672 Hz, Q 1 [CVGD21] | 514 Hz, Q 1 **est.** | the alto's over (80/70)², the reeds' lengths [Thomann, sec.] |
| Spectrum lifted at the break | — | +4 dB **est.** around 618 Hz | where [BL88] found a tenor's break (an alto's at 837 Hz) |
| Breath noise, subtone | 1 | 3 **est.** | nothing measured |

Chimneys and bell are the alto's proportions on both: nothing measured
says otherwise (the bells' mouths, ~150 and ~120 mm, scale with the
horns' lengths).

The reed. The one reed measured in playing, 672 Hz with Q 1.13
[MGDC16], is a tenor's. Colinot et al. model the alto with it, and so was
this model's alto tuned. An alto's reed is about 70 mm long and a
tenor's 80, so as a cantilever the alto's rests (80/70)² ≈ 1.31 times
higher. One of the two has to be estimated, and keeping the alto as
tuned makes the tenor's 514 Hz. With the measured 672 Hz on the tenor,
its lowest three notes start an octave up for 0.1–0.2 s and settle flat.

The break. Measured, a saxophone's spectrum rises to its break and falls
steeply above it [BL88]: on a recorded tenor's low notes the strongest
harmonics lie at 415–730 Hz, the honk. This model's reed excitation falls
too fast to make that rise (see "Darker than measured"), so the tenor's
radiated sound is lifted 4 dB around 618 Hz (a peaking section three
octaves wide). It is a gentle tilt: played hard, B♭2 and C3 have their
6th and 5th harmonics strongest, but on most low notes and on every
shared note at a mezzo forte the fundamental still leads, and the energy
above 1 kHz moves by about 1 dB. A larger lift evens the range out less
and costs the low notes' bends more (below). The alto is not lifted: it
is the alto the Lead had.

**Where each plays.** A tenor sounds A♭2 to E♭5 (E5 with the high F♯ key),
an alto D♭3 to A♭5, a fourth higher. On the Lead a cell is the same
fingering, the same written note, on both: the alto plays where the
neck is, the tenor a fourth under it (`Sound::saxOct`). So each plays
across its own range. Their register breaks (written C♯5 to D5) fall on the
same cells, concert B3 to C4 on the tenor and E4 to F4 on the alto. Their
altissimo, the smaller horn above the palm keys, also starts on the same
cell: concert E5 on the tenor, A5 on the alto. An octave would have put the
tenor's solo register far under the alto's. In the Weimar Jazz Database
the middle half of the tenor solos' notes lie from B3 to A♭4, the alto's
D4 to B♭4, a few semitones apart [WJD]. So the pitch makes some of the
difference on the same cell, but most of it has to be the sound.

**The honk.** A low tenor note (under concert F♯3) pressed nearly to the
top (from 0.85) is blown harder through a wider opening and growled into
(`Sound::saxHonk`), as an R&B tenor honks: rough, and within 3 dB of a
forte's energy above 1 kHz. Its growl replaces a top press's own rather
than adding to it, which would swamp the reed and muffle the note. Under
that press a forte low note is clean. The alto doesn't honk.

**What tells them apart on the same note.** The same concert notes, D3 to
C5, blown alike (breath 0.75, lip 0.6), on master's tenor (before) and
this one (after):

| | Before | After |
|---|---|---|
| Tenor's energy above 1 kHz against the alto's (median) | −3.1 dB | −8.5 dB |
| …darker than the alto's on | 6 of 7 notes | 7 of 7 |
| The spectrum's −20 dB edge, tenor over alto (median) | 0.83 | 0.60 |
| Harmonic profiles (first 12) apart, rms (median) | 5.6 dB | 10.8 dB |
| Spectral centroid, tenor over alto (median) | 0.83 | 0.82 |
| Subtone hiss over the harmonics, tenor over alto (D3, E3, A3, B3) | — | +4.5 to +7.5 dB |

Benade and Lutgen's break is at 618 Hz on a tenor and 837 Hz on an alto,
a ratio of 0.74. Here the edge's ratio is 0.60: the same direction, but
further, since the alto's lattice cuts off higher than a measured alto's
(1.2–1.5 kHz against 0.93–1.14). The centroid barely moved, because both
horns' energy lies mostly in their fundamental and second harmonic. The
difference is everything above about 600 Hz: the tenor falls off past
its break, while the alto stays even out to its 7th harmonic and beyond. Both horns are darker than recordings above 1 kHz.
`theTenorIsNotAnAlto` asserts the first four rows and bounds them both
ways, so it fails if the horns converge again (master's tenor fails three
of its four: energy above 1 kHz −3.1 dB, edge 0.83, profiles 5.6 dB apart)
or if the tenor goes dull. `theTenorBreathesMoreInSubtone`,
`theLowestNotesSettle` and `eachSaxPlaysInItsOwnRange` check the hiss,
the low notes' starts, the fourth and the honk. `theTenorIsAsBrightAsARealOne`
is an expected failure: on a recorded tenor's low notes the energy above
1 kHz is 6–9 dB under the total, on this one about 20 dB under, 11–14 dB
less.

## The model

### The horn

The horn is built once, when the Lead's engine starts (0.2 s for both
kinds), from transmission lines:

1. A conical bore of spherical waves with wall losses.
2. A cylindrical mouthpiece closed by the reed.
3. Keefe's open tone holes and the register vents.
4. The bell's radiation.

The bell's length for the low B♭ comes first. Each tone hole is then placed
in turn, up from the bell, where its note is in tune, as a maker does.
Every fingering's input impedance is computed, and each peak becomes a
parallel-resonance mode (its frequency, Q and peak), up to 24 of them below
6 kHz. Above them, one broad resonance carries the bore's average
impedance.

The horn is played as those modes, driven by the reed's flow. The bore's
response to this sample's flow is one known number plus a fixed share of
the flow (impulse invariance), so the flow is the root of a quadratic each
sample (Guillemain, Kergomard & Voinier).

| Part | What it is | Set from |
|---|---|---|
| Cone | Alto: half angle 0.028 rad, missing tip x1 = 0.196 m, so the radius at the mouthpiece is 5.5 mm. Tenor: half angle 0.0265 rad (1.52°), 6.7 mm at the mouthpiece, so x1 = 0.25 m | [PCKG20] 0.028 rad; [KGSVDGK19] x1 0.196 m; tenor 1.52° [CSW09], 1.55° and 6.7 mm at the neck's input in Jensch & Grothe's CT of a tenor [JG21 Fig. 3, digitised] |
| Mouthpiece | A cylinder closed by the reed: alto 30 mm long, 6 mm in radius (3.4 ml, 0.55 of its missing cone); tenor 32 mm long, 8.5 mm in radius (7.3 ml, 0.61 of its missing cone), its radius the mean shank of 41 tenor mouthpieces (16.98 mm across; forum measurements, secondary). Real mouthpieces fill about their whole missing cone (tenor ~11–12 ml in play, alto ~8–9, **est.** from water fills); a tenor's that size here sends its lowest notes into a hollow, near-sine regime. Half the length of Colinot's 60 mm model mouthpiece. **est.**, chosen so the bore keeps a measured alto's inharmonicity (below) | [CVGD21] the cylinder model. Shortened: with a perfectly harmonic bore (f₂/f₁ = 2.00) the low notes jumped to the octave at a forte; this gives 2.05 and 3.14 on the low D♯, against the measured 2.08 and 3.12 |
| Wall losses | Benade's 2.96 × 10⁻⁵·√f/r per metre, per 12 cm piece by its mean 1/r, times √(f/90 Hz) above 90 Hz (the tenor's rise from 67 Hz, its lowest note's ratio to the alto's) | [FR98] the smooth-tube loss; the rise **est.**, fitted so the alto's low D♯ has Q 34, 33, 34, 29 for its first four modes against the measured 34, 35, 28.5, 37 [CVGD21] |
| Tone holes | Radius 0.6 of the bore's where the hole is (tenor 0.54), chimney half its radius; Keefe's shunt (inner end correction by Dalmont's polynomial, outer 0.61 b, radiation 0.25 (kb)², the chimney's own wall loss) and series length −0.28 b(b/a)². Each placed so its fingering's first resonance is on its note | alto's b/a **est.**, the tenor's measured, 0.54 ± 0.08 [JG21]; hole radius 7 mm at the top to 19 mm at the bottom (alto). Cutoff where the peaks fall under a quarter of the strongest: alto 1.2–1.5 kHz, tenor 0.7–1.0 kHz, against 0.93–1.14 kHz [PCKG20] and 760 ± 250 Hz [CSW09] |
| Register vents | Radius 1.5 mm (alto), 6 mm chimney; the body vent at the second mode's node of written F4's tube (serving written D5–G♯5), the neck vent at written C♯5's (A5–E6). The resonances below a vented note's are played at 0.6 of their computed peaks: as computed, the neck vent barely spoils the short tubes' first mode, and the top notes locked onto it at a forte | ~2 mm across, 6 mm long [CSW09]; positions and the 0.6 **est.** (not published) |
| Bell | Radiation of an unflanged mouth 1.25 times the cone's end radius (Levine & Schwinger's low-ka terms, kept bounded) | **est.** |
| Measured check | Alto low D♯, model against Colinot: 185 Hz (equal-tempered) against 190, the next two at 2.05 and 3.14 times it against 2.08 and 3.12; Q 34 / 33 / 34 / 29 against 34 / 35 / 28.5 / 37; peaks 7.7 / 9.3 / 8.3 / 5.3 against 10 / 13.3 / 9.9 / 7.7 × Z_c (23–31 % low). Tenor's lowest note 16 MPa·s/m³ against 11 with the reed's compliance in parallel | [CVGD21 Table 2], [CSW11 Fig. 8] |

### The reed and the player

| Part | What it is | Set from |
|---|---|---|
| Reed | A mass–spring with the lip's damping, x'' + (ω_r/Q)x' + ω_r²x = −ω_r²(γ − p), opening h = 1 + x, shut against the lay (inelastic). First register: ω_r = 0.8–0.9 × 672 Hz, Q 0.5. Second register: 0.9 × 672 Hz, Q 1.5 (the pinched embouchure). Never under 1.9 times the note (an inward-striking valve drives only below its resonance). The tenor's 514 Hz **est.** (see [The tenor against the alto](#the-tenor-against-the-alto)) | [MGDC16] 672 Hz, Q 1.13; [CVGD21] ω_r = 4224 rad/s, q_r = 1; [WB74]. Q is lower here: at Q ≈ 1 this model's low notes jumped to the octave |
| Flow | Bernoulli, u = ζ·h·√(γ − p), with the lay's curve and the lip's cushion closing the slit over the last of the swing, h → h²/(h + ε), ε = 0.02 (edge) to 0.32 (subtone) | ζ = 0.8 at an ordinary embouchure (0.6 [CVGD21], 0.65 [KGSVDGK19]); ε **est.** [DGO03, AvW04 the smooth closure] |
| Embouchure | ζ × (0.85 + 0.35·lip) × √(opening); the closing pressure P_M = 3.8 kPa (alto), 4.3 (tenor), times the opening (0.5 to 1.1, with the touch) | P_M = K_a·H0, 4.8 MPa/m × 0.8 mm [MGDC16]; the opening **est.** |
| Breath | γ from just over the note's linear threshold (with the reed's dynamics) to 0.9, the envelope's rise behind the tongue (30 ms, 12 under a hard press) | [FS96] 1.6–4.4 kPa ≈ 0.4–1 × P_M; [CVGD21] thresholds 0.3–0.45 |
| Second register | The breath held at least at 0.6 × P_M (support) | [CVGD21] second register stable 0.66–0.79 |
| Tongue | Closes the slit (on over 2 ms, held 12 ms, or 15 at a fresh note), lets go over 3–8 ms (harder is quicker); the reed's swing untouched | **est.** |
| Vocal tract | Two resonances in series with the bore, 200 Hz (peak 0.3 Z_c, Q 4) and 800 Hz (0.6 Z_c, Q 5); not radiated | [CSW11] 150–250 Hz ≤ 3 MPa·s/m³, 500–1100 Hz 0.4–10 MPa·s/m³ |
| Lip (bends) | An equivalent volume ΔC at the mouthpiece in parallel with every mode, each mode at ω·√(C/(C+ΔC)) with C = Q/(Aω); solved so the note mode, with the reed's own susceptance, sounds where wanted | the reed's equivalent volume [CSW11] 3.2 ml (tenor) |
| Intonation | All modes scaled alike so the fingered note sounds in tune with the reed on it; and the ear (the reed's swing heard against the note, two 25 Hz poles), drawing it in with a 50 ms time constant, remembered per note | **est.** (the player's) |
| Growl | The mouth pressure modulated by a glottal pulse (open 62 % of its period) at a voice's pitch, 0.41 of the note folded into 85–190 Hz, ±1.2 % jitter, depth up to 0.45 × 0.69 | descriptions only (no measurements found) |
| Breath noise | The flow times (1 + σ·n), n turbulence band-passed at 2 kHz; σ 0.02 (edge) to 0.1 (subtone), more with a flatter finger | **est.** |
| Radiation | From the modes' states: each mode a complex weight, fitted by least squares (relative, column-normalised, ridge 10⁻³, on 220 frequencies up to past the last mode kept) so the modes together radiate as the horn's transfer from the reed's flow to a metre off, H = T·Z, computed from the same lines (each opening a monopole). Above 3.5 kHz, where that transfer's phase turns fast with the paths to the bell and holes, only its level is matched: the flow through two high-passes at 3 kHz. The fit is 3.5 dB rms off the transfer under 1 kHz in the horn's range and about 4.5 dB from 1 to 3.5 kHz, single harmonics up to 23 dB (alto) and 30–33 dB (tenor); and since the modes the reed drives stand higher between their peaks than the horn's own impedance, the radiated sound sits 2–8 dB under the horn's transfer there | [B76] the transformation; harmonics between the modes radiate as the horn does, not only as its resonances do |

### Keeping it in real time

- **Cost.** A horn costs about 0.2 % of a core held or under vibrato
  (0.21 %), 0.33 % changing note five times a second: 27 complex one-poles
  with their radiation, the reed and a quadratic per sample, and the
  coefficients once a block (every four samples while the keys move). A
  four-voice section stays within the studio's budget (tested; that test,
  like the other parts', fails when the machine is loaded by other work).
- **No allocation, no tables built on the audio thread.** The designs are
  built in `LeadEngine::init`.

## How it was checked

`tests/saxophone_tests.cpp`:

- **The bore against a measured alto:** Colinot's modal fit (frequencies
  within 9 %, Q within ×0.7–1.4, peaks within ×0.6–1.5), and the tenor's
  first peak against Chen's.
- **The cone's whole harmonic series:** the second resonance near twice
  the first and the third near three times, against a cylinder built the
  same way, whose second resonance is three times its first. Played, the
  second harmonic stands within 10 dB of the first.
- **The tone-hole lattice's cutoff** against Petersen's and Chen's.
- **The reed's threshold** (by bisection on a steady breath) against the
  linear theory with the reed's dynamics. The horn speaks 0.05–0.11
  above the theory's single-mode estimate; the test allows 0.12.
- **Beating:** at a hard breath (0.75), shut for x1/(L+x1) of the period,
  the idealised saxophone's Helmholtz-motion analogue, within ×0.5–2. The
  shut share grows with the breath (0.2 to 0.4 on the alto's low notes),
  so the analogue holds only near that breath.
- **Intonation:** within 10 cents across each horn's own range at an
  ordinary breath and embouchure (within 15 a little below it), and in
  register, once the ear has drawn it in. Above the palm keys (written F6
  and up, every semitone to s 50, at an ordinary and at a hard touch with
  a firm lip) within 50.
  Slurred across the break (from the neck's E4: the alto's E4, the tenor's
  B3) up and back down:
  every note slurred to, and the note held at the end, within 30 cents.
- **Bends and level:** every note bent ±15, ±30 and ±45 cents within 3 dB
  of the note unbent (the tenor's within 4: its low notes' strength lies in
  the upper harmonics around their break, from which a bend up on the lip
  alone pulls the reed's support; its third note loses 3.8 dB at +45); every note from 8 semitones under the low B♭ to 40
  over it within 10 dB of the range's median.
- **A slide and a tongue on the page:** a finger slid between two notes of
  one register swells less than 8 dB; notes re-tongued on a sounding horn
  put no edge in the line sharper than three times a held note's.
- **Breath and embouchure:** on the horn, subtone against edge, more than
  10 dB and a centroid at least 1.5 times higher. On the page, louder and
  brighter with the press.
- **Legato:** within a register, no change of note puts a sharper edge
  (second difference) into the line than the held notes' beating does,
  and the line never stops. Across the break, within six times.
- **Growl:** sidebands between the harmonics, 6 dB more of them.
- **The tenor is not an alto:** see the next section.
- **Stability:** six seconds of random pushes, about 90 of them (pitch,
  breath past closing, embouchure, growl, tonguing), always finite, under
  40 Pa, and silent once the breath stops.
- **Budget:** one horn under 1 % of a core; a four-voice section through
  the studio within its time.
- **Sanitizers:** run once under `-fsanitize=address,undefined`.

## Open points

These were left open after the critique rounds:

- **Altissimo is not the real one.** A saxophonist's altissimo is a
  cross-fingered tube's third to fifth resonance held by the vocal tract
  tuned onto the note [CSW08, CSW11]. In this model those regimes would
  not hold with the tract in series (it pulled the pitch, and the reed
  jumped to other regimes). Above the palm keys a smaller horn plays: the
  same horn scaled down, on its second-register fingering. Those notes are
  in tune within 50 cents and their timbre is a sopranino's, not an
  altissimo's. Likewise, below the low B♭ a larger horn plays.
- **One reed is measured, for two horns.** The alto plays the tenor's
  measured reed (672 Hz), as Colinot et al.'s does; the tenor's is
  estimated from it by the reeds' lengths (514 Hz).
- **The tenor's break is lifted, not grown.** Its 4 dB around 618 Hz is a
  filter on what it radiates, standing in for an excitation this model
  can't make (see "Darker than measured"). It costs its low notes' bends
  up to 4 dB (against 3 for the alto) and lifts its low register over its
  throat notes (written A4 to C♯5), which sit 8–10 dB under the range's
  median. The alto's break isn't lifted.
- **The tenor's breath noise is an estimate.** Nothing is measured. It is
  three times the alto's in the subtone term, so a low subtone's hiss is
  4.5–7.5 dB over the alto's.
- **The honk is played, not built.** It is a little more breath, a wider
  opening and the growl on a low note pressed nearly to the top, as an R&B
  tenor plays. It roughens the note without brightening it: in this model a
  low note blown harder grows only a little brighter.
- **Where the tenor is still not a tenor:**
  - In subtone on D4 and E4 it has 4–6 dB less hiss than the alto, and it
    speaks faster. The hiss is tested on D3 to B3 only.
  - Its lowest note, A♭2, starts about 20 cents flat for up to 0.4 s;
    master's was in tune by 0.3 s. `theLowestNotesSettle` allows 25 cents
    from 0.3 s.
  - Its bends are allowed 4 dB, 0.2 dB over its worst case.
  - Its reed, 514 Hz, is about where master's scaled one was (503 Hz), so
    the reed adds little contrast. The cantilever estimate ignores the
    lip's loading.
  - Its altissimo wasn't listened to, and the fourth down wasn't checked
    against the other parts in Arrange.
- **The reed is heavier damped than measured.** It has Q 0.5 in the first
  register against the measured 1.13 with the lip [MGDC16]. With the
  measured value, this model's low notes jumped to the octave at a
  forte. The truncation of the bore's resonances, the single-mode
  mouthpiece and the absent pumping flow of the reed are the likely
  reasons.
- **The reed's swept volume is folded into the mouthpiece, not
  simulated.** The flow the reed sweeps as it swings was simulated at
  first. With the mouthpiece reduced by its volume (Benade's rule), it
  flattened the upper register by hundreds of cents near the reed's
  resonance. It is left out, and the cavity is sized for the bore's
  inharmonicity instead.
- **The threshold is above the single-mode theory** by 0.05–0.11 of P_M.
- **Darker than measured.** A forte tenor low B♭ (written; concert A♭2)
  has its spectral centroid at 170–220 Hz, against about 1.2 kHz in Li et
  al.'s measurement at 107 dB: 5–6 times low. A forte tenor concert B♭3
  is at about 420 Hz, an alto A4 at about 800. Two causes:
  - The mouthpiece pressure itself is fundamental-heavy: on a forte tenor
    A3 the second harmonic is about 13 dB under the first. The heavily
    damped reed (Q 0.5, its resonance held to at least 1.9 times the note)
    is the cause. With the measured Q (1.13), 12–13 of 87 notes played in
    the wrong regime; with a reed resonance of 1.5–2.5 kHz, 16–51 squealed
    (critic's probes). Fixing it is structural: the reed's swept flow, a
    properly sized mouthpiece, ζ nearer 0.6.
  - The bore modes are fitted peak by peak (frequency, Q, peak), not as
    complex residues to the whole impedance. Between the peaks the modes
    the reed drives stand 6–10 dB higher than the horn's own impedance, so
    the radiation, fitted to the horn's, comes out 2–8 dB low there.
    Fitting the modes with complex residues to the impedance on the same
    grid (checking passivity) would make the two consistent and, by the
    critic's estimate, roughly double the centroid.
- **The forte barely brightens with more breath.** Once the reed beats,
  more breath changes the level by about 1 dB and the centroid little. The
  dynamics come mostly from the embouchure: the opening the lip allows,
  hence the closing pressure, and the lip on the reed.
- **The dynamic range** from subtone to edge is about 15 dB (a forte tenor
  low B♭ at 1.45 Pa against 0.27 in subtone), where players manage 20–30.
- **Intonation is a global rescale.** Each note's bore is rescaled whole by
  34–171 cents in the first register, and the second register's note
  modes are 62–152 cents sharp of the passive bore's, for the reed's pull
  and the holes' placement. Placing the holes against the played
  frequency (with the reed on) would leave only a few cents to rescale.
  Without the ear, notes sit up to about 70 cents off (the ear settles at
  −35 to −72 cents); a note's attack starts up to 40–55 cents off for
  about 100 ms until the ear has drawn it in (less once it has learnt
  that note).
- **Low notes are slow to speak** from a fresh attack: 265–525 ms to 90 %
  of their level near the low B♭.
- **The vibrato is mostly pitch:** 0.2–3.8 dB of level, and on the lowest
  notes it swings ±31 to ±43 cents for the ±20 asked. A saxophonist's jaw
  vibrato moves the tone more.
- **The level is a sawtooth across the break:** the first register falls
  8–9 dB from its third note to its top, then steps up 8.5 dB (alto) and
  8 dB (tenor) into the second register, whose breath is supported and
  whose reed is less damped.
- **The larger horn's notes far below the low B♭** settle up to 50–72
  cents off at 0.7 s (alto s −12 +72, s −10 +59; tenor s −9 +48, s −6 −50).
- **Unlearnt notes land sharp.** A slur onto a note the ear's memory hasn't
  learnt lands 23–64 cents sharp and takes 50–80 ms to come within 20;
  once learnt, 5–15 ms. Trills and runs of notes under 100 ms never teach
  it. Fresh attacks swoop about 80 cents: −30 to −60 at 30–50 ms, then
  sharp at 70–100 ms (+11 to +31 once learnt, +24 to +54 before). A cheap
  fix: play each note once when the horn is built and seed the memory.
- **Instant octave leaps** (no glide) from s 10–15 to s 22–27 land on the
  wrong regime in about 1 % of cases (5 of 432 alto, 1 of 432 tenor). The
  page always glides, so this is the API's only.
- **The support a fallen note is given** takes about 35 s to fade from its
  limit.
- **The growl** puts sidebands 32–37 dB under the note between the
  harmonics. The voice itself is not heard through the horn.
- **The ear and the memory are a player's.** The intonation within
  10 cents, the octave catches and the found breath are control loops
  standing for the player, not acoustics. A note's first 100–300 ms can
  be up to a few tens of cents off, as a player's attack can.
- **Slurs across the break** land on their notes, but the regime changing
  under the reed gives a short burst, up to six times the reed's own edge.
  A real horn blips there too, but its size against a real one's is
  unmeasured.
- **Radiation.** Every opening is a monopole at one distance, with no
  interference between the bell and the holes and no directivity.
