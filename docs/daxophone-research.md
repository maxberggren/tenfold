# The daxophone: what is known, in numbers for a model

Compiled 2026-11-24 from the literature on the instrument, on wooden
beams, and on bowing. Bracketed tags such as [WIKI] point to the source
list at the bottom.

Each value is marked by where it comes from:
- **pub**: stated in the source.
- **calc**: my arithmetic on published numbers or textbook formulas.
- **inf**: my inference or estimate, with nothing published on it.

**Caveat.** No acoustic measurement of the daxophone itself has been
published that I could find: no spectra, no decay times, no measured mode
shapes of a tongue, no bow forces. What is published is how it is built
(Reichel's own notes, as builders quote them, and the encyclopaedias) and
how its parts behave in general: cantilevers, wood, bowed bars, rosin,
piezo pickups. The model is built from those, and every number that only
the instrument itself could settle is marked **inf**. The model's
behaviour checked against them is in `docs/daxophone.md`.

---

## 0. Numbers for the model (summary table)

| Quantity | Value | Tag | Source |
|---|---|---|---|
| Inventor, date | Hans Reichel (1949–2011), Wuppertal; developed in the 1980s; first on record on *The Dawn of Dachsman* (1987) | pub | [WIKI] |
| Name | From German *Dachs*, badger, for its animal sounds; described as comical, often human | pub | [WIKI] |
| Tongue | About 330 mm long, 30 mm wide, 5 mm thick | pub | [WIKI] |
| Tongue (builders' range) | 12–15 in long (300–380 mm), 1–2.5 in wide (25–64 mm), 3–6 mm thick | pub | [PW-S] |
| Tongue outline | Sawn with a coping, scroll or band saw to a free outline; the outline and the wood change the voice | pub | [WIKI] |
| Woods | Denser woods (ebony, oak) sound mellower than light ones; maple and cocobolo used | pub | [WIKI], [PW-D] |
| Mounting | Tongue clamped in a wooden block, on a tripod (Reichel's at 39°) or clamped to a table; it must not wobble | pub | [WIKI], [PW-B] |
| Pickup | Contact microphones in a cavity of the block, glued under a thin top; usually two piezo discs wired in parallel; "the sound comes from the tongue, not the soundbox"; amplified, a murmury low undertone | pub | [WIKI], [PW-B] |
| Soundbox (student model) | About 5 × 2 × 1.5 in (127 × 51 × 38 mm), top 1/8–1/4 in thick | pub | [PW-B] |
| Dax | A hardwood block about 150 mm long and 50 mm tall, curved on both edges; one edge fretted, the other smooth and covered with cardboard (mellower, protects the tongue); finger holes | pub | [WIKI] |
| How the dax is used | Rocked rather than slid along the tongue; pressed flat or on its side edge, which changes the pitch (Reichel's "yodel"); a heavier dax helps intonation (Reichel); the fretted edge gives "trumpet-like" tones | pub | [PW-D] |
| Bow | The free end is bowed, usually with a double-bass bow; the timbre depends on where it is bowed and where the dax stops the tongue | pub | [WIKI] |
| Wood (sugar maple, 12 % moisture) | E_L ≈ 12.6 GPa; specific gravity 0.63 (ρ ≈ 700 kg/m³); G_LT/E_L = 0.063, G_LR/E_L = 0.111 | pub | [FPL] |
| Wood loss factor along the grain | tan δ ≈ 0.006–0.01 for hardwoods in the audio range, roughly independent of frequency | pub (range) / inf (value used, 0.008) | [ON], [OBA] |
| Wood loss factor in shear (twisting) | About twice the bending one | inf (0.02 used) | [OBA] |
| Rosin's friction | Static about 0.8; sliding falling to about 0.3–0.4 by a few tenths of a m/s; fitted as a hyperbola in sliding speed | pub (shape, ranges) | [SW], [GAL] |
| Bow force window | A minimum and a maximum for every bowed note; the minimum falls faster with bowing distance than the maximum | pub (string) | [SCH] |

## 1. The tongue as a cantilever

A board 5 mm thick and 30 mm wide clamped at one end is, for its
bending, an Euler–Bernoulli cantilever: the shear and rotary inertia
Timoshenko adds are a few per cent at the fifth mode for this
thickness-to-length ratio (h/ℓ ≈ 0.015–0.06) [BLE].

- **Frequencies (calc).** f_n = (β_nℓ)²/(2πℓ²) · √(EI/ρA), with
  β_nℓ = 1.8751, 4.6941, 7.8548, 10.9955 [BLE]. For a rectangle
  EI/ρA = E h²/(12ρ), so f_1 = 0.1615 · h/ℓ² · √(E/ρ). The reference
  tongue (5 mm, 330 mm, maple) is 31 Hz open; half of it sounding is four
  times that, 125 Hz, a fifth of it 25 times, 780 Hz. The ratios are
  1 : 6.27 : 17.55 : 34.39.
- **Twisting (calc).** A thin rectangle twists as a Saint-Venant rod,
  J = w h³/3 · (1 − 0.63 h/w), I_p = w³h/12, so c_t = √(GJ/ρI_p) ≈
  2(h/w)√(G/ρ) and f_n = (2n − 1) c_t/(4ℓ). The reference tongue's first
  twisting mode is about 280 Hz open, nine times its first bending one;
  twisting goes as 1/ℓ and bending as 1/ℓ², so as the dax shortens the
  tongue the twisting modes fall back toward the note, to about twice it
  at a fifth of the length.
- **The outline (inf, from [WIKI]).** A tongue of varying width keeps
  its bending modes' frequencies only to the extent that its width varies
  slowly: where it narrows, it is lighter and more flexible together, and
  the upper modes, whose half-waves span a narrow or a wide stretch,
  move off the straight board's ratios. Where the outline's middle stands
  off the clamp's centre line, a swing of that stretch also twists the
  tongue: swinging and twisting couple through the mass off the axis.
  Reichel's free outlines (lobes, waists, horns) do both. This is the
  likeliest physical reason for his remark that the shape changes the
  voice.

## 2. The dax

The dax is pressed onto the tongue across its whole width and slid (or
rocked) along it. What the tongue sees there:

- **A support across the width (calc from [WIKI], [PW-D]).** The dax
  pins the tongue's swing and, lying across the whole width, its twist.
- **A hold on its rocking that is not rigid (inf).** The dax's edge is
  curved and covered with cardboard: pressed flat and firmly it bears
  over a patch of a centimetre or so, and the patch's give resists the
  tongue's rocking over it. A rough number: a patch 12 mm long, cardboard
  with a modulus of tens of MPa and a fraction of a millimetre thick,
  across 30 mm, gives a rocking stiffness of several hundred N·m/rad, of
  order 50–100 times the tongue's own EI/L. Pressed on its edge, the patch
  shrinks and the hold with it: the dax becomes a pin.
- **Only as much as its press can hold (calc).** Its curved face rolls
  when the moment it holds exceeds the press times half the patch. At
  8–40 N over a 6 mm half-patch that is 0.05–0.24 N·m; a bowed tongue's
  moment at the dax is of order 0.1 N·m. So a firmly pressed dax mostly
  holds, a lightly pressed one rocks every cycle. This is a nonlinear
  hold, and a strong one: it softens the support at the peaks of the
  swing and passes on a clipped moment.
- **A pinned versus a held dax (calc, from my finite-element runs).** A
  cantilever with a pin part-way along is one continuous beam: the part
  behind the pin and the part in front rock together through it. Its
  fundamental rises as the pin moves out, to a maximum near three
  quarters of the length, then falls; past that, the mode the tip swings
  most in hands over from one family to the next, a jump of the bowed
  pitch at each hand-over. This is the "yodel" of a dax on its edge
  [PW-D], and why Reichel found a heavier dax intonates better (its
  inertia to rocking holds the tongue more firmly at audio frequencies;
  the model's dax has no inertia, so this is not modelled).
  A firmly pressed, flat dax separates the two parts: the sounding part
  in front is a cantilever clamped (stiffly sprung) at the dax, and its
  fundamental rises smoothly as 1/ℓ² across the whole travel.
- **Damping (inf).** The cardboard is lossy (a loss factor of 0.2–0.3 is
  typical of paperboard); a lighter press lets it give and lose more.
- **The part behind the dax (calc).** It is clamped in the block and
  held at the dax, so it has its own modes, of a beam clamped at one end
  and sprung at the other. Its frequencies go as 1/d², d the distance
  from the clamp to the dax: they fall as the dax moves out, while the
  note rises. It is all the block hears of the sounding part: the moment
  and torque that part puts on the dax reach the clamp through it.

## 3. The pickup

- **Contact microphones (pub [WIKI], [PW-B]).** Piezo discs glued to the
  inside of the block's top, two in parallel. A piezo disc measures its
  own strain, which in a block clamping a cantilever follows the moment
  the clamp holds; the clamp's shear and twist strain the block less.
  Two discs either side of the middle, wired in parallel, add their
  bending strain and cancel their twisting strain: the pickup is the
  clamp's moment, one voice.
- **What the block adds (inf).** A small hardwood box 127 × 51 mm with a
  3–6 mm top has its first plate modes at several kilohertz; the block on
  its stand rocks at tens of hertz. Into a high-impedance input the
  piezo's own roll-off is a few hertz. So: a bottom rolled off by the
  stand, a broad lift around the box top's first modes near three
  kilohertz, little over eight.
- **Not radiated sound (pub [PW-B]).** The block hears the clamp, not
  the air: the tongue's own radiation (a thin strip, a dipole, weak under
  its coincidence frequency) does not reach it.

## 4. Bowing a tongue

- **Stick and slip (pub [SCH], [GAL], [INA]).** The bow drags the tongue
  while the hair grips it, and it slips back when the tongue's pull
  exceeds rosin's grip; the friction's fall with sliding speed is what
  sustains the motion. For a string, every mode is a harmonic and the
  motion is Helmholtz's corner. A bar's modes are not harmonic, and the
  motion locks to its fundamental: the hair grips only near the peak of
  each swing, and the harmonics come from the friction's jumps, filtered
  by the bar's own response between its modes [INA], [ESS].
- **A window of force (pub [SCH]).** Too light a bow and the hair can't
  hold the fundamental: it slips through it and an upper mode (or
  nothing) sounds, a whistle. Too heavy and it holds on past the
  period's end, and the tone falls under the note, aperiodic and raucous.
  The window's floor rises with the stiffness of what is bowed, so a high
  note (a short tongue) needs a firmer bow.
- **The attack (pub [GUE]).** A bowed note speaks cleanly when the bow
  lands with its weight before it is drawn: the hair grips and drags the
  string before its first slip.
- **Rosin (pub [SW]).** Rosin's friction depends on its temperature at
  the contact, not only on the sliding speed; a speed-only curve
  (the hyperbola) is the usual approximation and misses some hysteresis.
- **A sliding stop (pub [LL]).** A mode retuned slowly by a moving
  constraint keeps its action, energy over frequency: its swing grows as
  the dax moves out and the mode rises.

## 5. What is not known

- The force and speed daxophone players bow with: here, the window's
  middle at the speed of an ordinary double-bass stroke (**inf**).
- How hard the dax is pressed and how large its patch is (**inf**,
  section 2).
- The tongue's loss in the clamp and the block (**inf**).
- Any spectrum of a daxophone: the model's tone could only be checked
  against the physics, and by ear against recordings, never against a
  measured one.

## Sources

- [WIKI] "Daxophone", Wikipedia (revision of October 2026).
- [PW-D] "The mystery of the acoustic cantilever: building a dax for your
  daxophone", Popular Woodworking editors' blog.
- [PW-B] "How to build a daxophone soundbox", Popular Woodworking
  editors' blog.
- [PW-S] Popular Woodworking, "Introducing the daxophone" (tongue
  dimensions as quoted by builders).
- [FPL] Forest Products Laboratory, *Wood Handbook: Wood as an
  Engineering Material*, USDA FPL-GTR-190 (2010), ch. 5 (elastic
  constants and ratios for sugar maple).
- [ON] T. Ono and M. Norimoto, "Anisotropy of dynamic Young's modulus
  and internal friction in wood", Jpn. J. Appl. Phys. 24 (1985).
- [OBA] E. Obataya, T. Ono and M. Norimoto, "Vibrational properties of
  wood along the grain", J. Mater. Sci. 35 (2000).
- [BLE] R. D. Blevins, *Formulas for Natural Frequency and Mode Shape*
  (Van Nostrand, 1979), beams and rods.
- [SCH] J. C. Schelleng, "The bowed string and the player", J. Acoust.
  Soc. Am. 53 (1973).
- [SW] J. H. Smith and J. Woodhouse, "The tribology of rosin", J. Mech.
  Phys. Solids 48 (2000).
- [GAL] P. M. Galluzzo, *On the playability of stringed instruments*,
  PhD thesis, University of Cambridge (2004).
- [INA] O. Inácio, *Numerical and experimental study of the nonlinear
  dynamics of bowed musical instruments* (modal method; bowed bars), PhD
  thesis, ISVR, University of Southampton.
- [ESS] G. Essl, *Physical wave propagation modeling for real-time
  synthesis of natural sounds* (banded waveguides; bowed bars), PhD
  thesis, Princeton (2002).
- [GUE] K. Guettler, "On the creation of the Helmholtz motion in bowed
  strings", Acta Acustica united with Acustica 88 (2002).
- [LL] L. D. Landau and E. M. Lifshitz, *Mechanics*, §49 (adiabatic
  invariants).
