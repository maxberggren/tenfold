# The marimbula: what is known

This is the survey behind `src/marimbula.h`. `docs/marimbula.md` maps each of
the model's constants to the numbers here.

Every number below is tagged:

- **[PUB]**: published, with the source named.
- **[RETAIL]**: a maker's or retailer's specification.
- **[DERIVED]**: calculated from published inputs, which are stated.
- **[EST]**: an estimate with no source.

**The main gap: no acoustic measurement of a marimbula exists** in the
literature that could be reached. There are no spectra, decay times,
Helmholtz frequencies or hole sizes for one. Its acoustics are assembled
from three bodies of work:

- the physics of kalimba and mbira tines;
- the physics of the cajón, a plywood box with a round hole;
- beam, plate and radiation theory.

## 1. The instrument

The marimbula is the Caribbean descendant of the African mbira. It plays the
bass in Cuban son and changüí, in Dominican and Puerto Rican music, and in
Jamaican mento.

The one organological study with measurements is Thompson (1975). He
measured instruments from Puerto Rico, the Dominican Republic and Curaçao.

### The box

- **Size:** "approximately the size and the shape of a small suitcase". The
  measured boxes are 58–64 cm wide, 37–47 cm high and 19–28 cm deep
  [PUB, Thompson].
- **Retail boxes:** Schlagwerk's is 60 × 40 × 20 cm and Meinl's about
  60 × 41 × 18 cm [RETAIL].
- **Volume:** 35–65 L inside, typically 40–45 L [DERIVED, subtracting walls
  of 12 mm for sides, top and bottom and 6.35 mm for front and back].
- **Wood:** "plywood of approximately quarter-inch thickness" (6.35 mm)
  front and back, with thicker sides, top and bottom [PUB, Thompson].
  Historically the boxes were packing crates.

### The sound hole and the tongues' mounting

- The tongues are mounted vertically on the front.
- The sound hole is above them, "often of semicircular form", curve up. The
  tongues "extend vertically over the straight lower edge of the hole"
  [PUB, Thompson].
- No hole size is published. From the photographs, about 15–20 cm wide,
  equivalent to a round hole of 11–14 cm [EST].
- The tongues are held in one of two ways [PUB, Thompson]:
  - the African way: two bridges (rods or dowels) with a third bar pressing
    the tongues against them;
  - the clamp board: the tongues "tightly clasped between two narrow
    boards" bolted to the front.
- Either way, a tongue is tuned by sliding it, which changes its free
  length.

### The tongues

- **Material:** "the universally preferred material … is steel cut from the
  springs of discarded wind-up phonographs … from one inch to 1¼ inch wide
  and a little less than 1/32 inch thick" [PUB, Thompson]. That is
  25–32 × ~0.75 mm.
- **Alternatives:** clock springs ("too narrow, too thin, and too flexible
  for the deep pitches"), rum-cask hoops, steel strapping and ground-down
  saw blades.
- **Count:** three or four on Haitian and Dominican instruments, and "ten
  or more" on Cuban and Puerto Rican ones. Thompson's documented sets have
  4, 7, 7 and 10 tongues [PUB, Thompson].
- **Pitch:** the lowest documented note is F1 (43.7 Hz) and the highest
  A♭4 (415 Hz). Most tongues sit between 44 and 200 Hz [PUB, Thompson,
  converted to scientific pitch].
- **Tuning:** not tempered: "the ear accepting a vague low pitch in place of
  an absent neighbor" [PUB, Thompson].

### Playing

- The player sits on the box and leans down to the tongues. "Instead of the
  thumbs, the player's index and middle fingers, held slightly curved, are
  used in depressing the tongues" [PUB, Thompson].
- The tongues are pushed down "at one end and letting them fly back"
  [PUB, Ewbank 1846, quoted by Thompson].
- The other hand, and the thumbs, drum on the front and sides. "The
  rhythmic elaborations of a marimbulero's left hand on the variously
  resonant sections of the instrument's body are often more highly praised
  than the ostinato basses" [PUB, Thompson].

### Buzz

- **Thompson mentions no buzzers.** African likembe and mbira use rings,
  bottle caps or shells.
- Some modern rumba boxes have rattles [informal, Hopkin], and one modern
  maker lines the face to prevent buzz.
- **Deliberate buzzers are not a traditional marimbula feature.** Incidental
  rattle from loose clamps or the curved tails of the tongues is plausible
  but undocumented.

## 2. The tongue

### A cantilever

A tongue's modes follow f_n = (β_nL)²/(2πL²)·√(EI/ρA) [textbook: Fletcher &
Rossing ch. 2; Rayleigh 1896]:

- β_nL = 1.8751, 4.6941, 7.8548, 10.9955.
- The ratios are 1 : 6.267 : 17.548 : 34.386.
- For steel (E = 200 GPa, ρ = 7850 kg/m³), f₁ = 815·h/L².

With 0.75 mm gramophone spring, a bass tongue is 6–12 cm long: 11.4 cm for
F1 and 7.6 cm for G2 [DERIVED].

### Measured tines are not ideal cantilevers

| Source | First overtone | Second overtone |
|---|---|---|
| Chapman 2006 (kalimba, 304–581 Hz) [PUB] | 5.30–5.86 | 11.7 (304 Hz note) |
| Chapman 2008 [PUB] | 5.9–6.8 | — |
| McNeil & Mitran 2008 (mbira) [PUB] | ≈ 5 | ≈ 14 |

- Chapman (2012) shows the ratios "depend uniquely upon where the bridge
  supports and subdivides the tine" [PUB, abstract].
- A root that can turn (a compliant clamp) **raises** the first ratio
  [DERIVED, FE]:

  | k_r·L/EI | f₂/f₁ |
  |---|---|
  | 100 | 6.27 |
  | 10 | 6.52 |
  | 3 | 7.61 |

- A bridge that gives under the root (translational compliance)
  **lowers** it to 5.85–5.92 at k·a³/EI = 100–300 [DERIVED, FE]. That is a
  plausible reading of Chapman's measured 5.3–5.9.
- Ludwigsen (2023) recommends modelling the bridge as a complex impedance
  [PUB, abstract].

### Torsion

- The first twisting mode of a thin strip is f ≈ (1/4L)·2(h/w)·√(G/ρ). For
  a 25 × 0.75 mm tongue at 65 Hz that is ≈ 490 Hz (7.5 × f₁) [DERIVED].
- It is woken by an off-centre finger and radiates poorly. Through the
  bridge it makes a moment, not a net force.

### Decay

- **No tine T60 is published.** Chapman 2006's waveform figure shows a
  two-stage decay on a 304 Hz kalimba note [DERIVED from his figure]:
  - about −12 dB in the first 0.25 s;
  - then a tail with T60 ≈ 4–5 s (Q ≈ 600).
- The overtone "dies away rapidly" [PUB, Chapman 2008].
- The box's structural modes match the "quickly decaying spectral components
  of the attack" [PUB, Ludwigsen 2008].
- The steel itself loses almost nothing (η ~ 10⁻⁴). The mounting sets the
  decay [EST].
- For a marimbula bass tongue the survey estimated a fundamental T60 of
  1–3 s and 0.1–0.4 s for the first overtone [EST].

### Forces

- Pushing a 0.75 mm tongue's tip 5 mm down takes about 3 N [DERIVED].
- The bridge carries the transverse force. The bar behind it carries the
  moment, as a couple across 1–3 cm [DERIVED].
- A tongue's modal mass is 3–15 g, against about 0.25 kg of panel modal
  mass [DERIVED].

### Radiation

- A bare tongue is an unbaffled dipole with ka ≈ 0.01–0.08. That is 40 dB
  or more less efficient than the panel or the hole [DERIVED].
- Measurements at a kalimba's bridge "show support for the effective
  radiation of sound at the fundamental frequencies of the tines" through
  the box [PUB, Ludwigsen 2008].

## 3. The box

### Helmholtz

f_H = (c/2π)·√(S/(V·L_eff)), with L_eff the wall's thickness plus about
1.7 times the hole's radius.

Kičák (2009) measured a 34.2 L plywood cajón with a 3 mm front [PUB]:

- **The air:** the Helmholtz peak rose from **63 to 94 Hz** as the hole
  grew from 74 to 160 mm.
- **The front's modes:** (0,0) at 131.5 Hz, (2,0) at 303 Hz and (1,2) at
  373 Hz.
- **Against the formula:** the measured Helmholtz is **0.85 times** the
  rigid-wall formula [DERIVED]. The compliant front pulls it down.

For marimbula boxes of 35–65 L with 8–12 cm holes, that gives **50–80 Hz,
typically 60–70 Hz**: in the instrument's own bass range [DERIVED]. Its Q is
unpublished; for a plywood box with a large hole it is likely 5–20 [EST].

### Plywood

The Handbook of Finnish Plywood gives birch [PUB]:

- **Density:** 680 kg/m³.
- **Bending modulus:** 12.7 GPa along the face grain and 4.8 GPa across it
  for 6.5 mm 5-ply. The shear modulus is 620 MPa.
- **Loss:** a loss factor of 0.01–0.03 (Q 30–100) is the usual order for
  wood panels [EST].

### The front panel

For a 0.61 × 0.40 m front of 6.4 mm birch, the (1,1) mode is ≈ 64 Hz
simply supported and ≈ 137 Hz clamped. With real glued and nailed edges it
is 80–130 Hz, just above the Helmholtz mode, as in a cajón [DERIVED].

## 4. The pluck

- No lamellophone pluck has been measured.
- The harp is the nearest analogue (Chadefaux et al. 2012) [PUB]:
  - a sticking phase of about 250 ms and a slip of about 3 ms;
  - about 4 mm of displacement at release (0.5–7 mm).
- Guitar slips take about 0.5 ms.
- For a marimbula the estimate is a 3–15 mm push and a 0.5–3 ms slip [EST].

## 5. The knock

On a cajón (Kičák 2009) [PUB]:

- A palm near the middle gives a bass of the plate's (0,0) mode and the
  Helmholtz peak.
- Fingers at the top edge give a bright, snare-like stroke, partly from the
  cajón's snares.

No slap spectrum is published; "slap = 1–3 kHz" is lore. The survey's
estimates [EST]:

- **Palm:** 55–140 Hz, lasting 100–250 ms.
- **Knuckles near an edge:** 300 Hz–3 kHz, lasting 30–100 ms.

## Sources

- Thompson, D. (1975). A new world mbira: the Caribbean marimbula.
  *African Music* 5(4): 140–148. doi:10.21504/amj.v5i4.1621
- Chapman, D. M. F. (2006). Investigating the sound of an African thumb
  piano (kalimba). *Canadian Acoustics* 34(3): 26–27.
- Chapman, D. M. F. (2008). Characterizing the sound of an African thumb
  piano (kalimba). Acoustics'08, paper 5aMUf1.
- Chapman, D. M. F. (2012). The tones of the kalimba (African thumb piano).
  *JASA* 131(1): 945–950. doi:10.1121/1.3651090
- McNeil, L. E. & Mitran, S. (2008). Vibrational frequencies and tuning of
  the African mbira. *JASA* 123(2): 1169–1178. doi:10.1121/1.2828063
- Ludwigsen, D. O. (2004, 2008, 2011, 2023). Kalimba acoustics, resonator
  box, tine video and boundary-condition abstracts. *JASA* 116: 2593;
  124: 2510; 130: 2509; 153: A227.
- Pehmoeller & Ludwigsen (2015). Acoustic and structural resonance
  characteristics of the cajon. *JASA* 138: 1935.
- Kičák, P. (2009). Frequency and dynamics analysis of bass tone of cajon
  box drum. Proc. 34th Int. Acoustical Conf., High Tatras.
- Handbook of Finnish Plywood. Finnish Forest Industries Federation.
- Chadefaux, D., Le Carrou, J.-L., Fabre, B. & Daudet, L. (2012).
  Experimentally based description of harp plucking. *JASA* 131(1):
  844–855. doi:10.1121/1.3651246
- Fletcher, N. H. & Rossing, T. D. (1998). *The Physics of Musical
  Instruments*, 2nd ed. Springer.
- Hopkin, B. Rumba box; Impedance. barthopkin.com (informal).
