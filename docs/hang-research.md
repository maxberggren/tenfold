# Hang / handpan acoustics: hard numbers for a physical synthesis model

Compiled 2026-11-23 from the published literature. Bracketed tags such as [R07] refer to the source list at the bottom.

How each value is marked:
- **pub**: stated in the source.
- **fig**: read off a published figure by me, so about ±3 dB or ±5 Hz.
- **calc**: my arithmetic on published numbers.
- **inf**: my inference or estimate. Nothing has been published on it.

Caveat on the literature: only a handful of real measurement studies exist. These are the Rossing/Morrison/Hansen holography and intensity work (2001–2009), Wessel/Morrison/Rossing (2008), Alon's York MSc thesis (2015, four handpans from three makers), and PANArt's own construction paper (Rohner & Schärer 2007). Nothing published measures finger contact time on a Hang, true per-mode Q of an isolated field, or the gu diameter of a PANArt Hang. Those values below are labelled **inf**.

---

## 0. Numbers for the model (summary table)

| Quantity | Value to use | Range / notes | Tag | Source |
|---|---|---|---|---|
| Shell material | Gas-nitrided low-carbon steel (DC04); E≈205 GPa, ρ≈7850 kg/m³, ν≈0.3 | Nitrided at 570–585 °C, 145 h, NH₃ 2.8 bar (patent example). The ISMA paper says "600 °C, a number of hours". Handpans also use AISI 430 stainless or "Ember steel". | pub (E, ρ, ν: inf) | [PAT], [RS07], [AY] |
| Shell thickness h | **0.93 mm** | 0.91–0.95 mm on the Hang; 0.75–1.25 mm allowed in the patent; handpan blanks 0.9–1.0 mm, some 1.2 mm | pub | [RS07], [PAT], [AY] |
| Overall diameter | **52–53 cm** | Ding shell 50 cm; prototype 60 cm, reduced to 50 cm for lap play; handpans 53–56 cm, blank tuning ring 530 mm | pub | [RS07], [PAT], [WIKI], [AY] |
| Overall height | **24 cm** | Each handpan blank shell is 130 mm high | pub | [PAT], [WIKI], [AY] |
| Shell join | Glued with hybrid-polymer sealant at a folded flange about 1 cm wide; brass ring over the seam (2nd gen.). Rigid in practice. | | pub | [RS07], [WIKI] |
| Cavity volume V | **~25–28 L** | Two spherical caps, a=26.5 cm, h=11–13 cm, give 26–31 L before subtracting the inward-pressed fields | calc/inf | — |
| Gu (port) | **ø 82 mm, neck 19 mm** for modern handpan blanks | Hang gu is "the size of a hand" and "subtly oval" (Integral Hang); no published mm value | pub | [AY], [PAT], [WIKI] |
| Helmholtz f_H (free, not on lap) | **85 Hz** | Integral Hang ~85 Hz ("near F2"); handpan ~85 Hz measured vs 86.8 Hz calculated; rises to ~90 Hz when the struck field is damped | pub | [WMR08], [SON] |
| Helmholtz f_H (on lap, design target) | **D2 = 73.4 Hz** | Knee spacing tunes it, roughly 60–98 Hz across sources | pub | [RS07], [SOMA], [IDM] |
| Ding (Integral Hang, D3) partials | **148, 295, 445 Hz** (1 : 1.993 : 3.007), next mode **1344 Hz** (9.08×) | | pub | [WMR08] |
| Ding (low-voice F3) | 174, 348, 520 Hz | | pub | [R07] (decoded fig. labels), [MR07] |
| Ding (high-voice G3) | 189, 390, 593 Hz; (2,1)a/b at 1418 / 1543 Hz (7.50× / 8.16×) | Octave +54 c, fifth +78 c against an exact 2:3, so this ding is not tightly tuned | pub/calc | [R07], [MR07] |
| Tone-field partial ratios | **1 : 2 : 3** (fundamental, octave, compound fifth) | Measured deviations are mostly within ±5 c and all within ±13 c (sample listed in §2) | pub/calc | [R07], [ALON] |
| Higher field modes | Next modes at about **7.5–9 × f1** ("around the third octave"), inharmonic, often in pairs 50–150 c apart | | pub | [RS07], [R07], [SOMA] |
| Gu "neck bell" | (0,1) **D5 = 587 Hz**, (1,1)a **F5 = 698 Hz**, (1,1)b **F#5 = 740 Hz** | Integral Hang | pub | [RS07], [WIKI] |
| Octave mode shape | One nodal line along the **minor** axis; the two lobes sit at the ends of the **long** axis | | pub | [ALON] fig 2.15, [R07] |
| Fifth mode shape | One nodal line along the **major** axis; the lobes sit at the ends of the short axis | | pub | [ALON] fig 2.15 |
| Field long axis orientation | **Radial** (points at the ding) | Schematic only; steelpan outer notes behave the same way | fig/inf | [WMR08] fig 2–4, [RHH96] |
| Field aspect ratio (minor/major) | **~0.75–0.8** | Steelpan 4:5 "readily produces" octave:fifth = 2:3; models use b/a 0.74–0.8 | pub | [SOMA], [SON] |
| Field size scaling | L_major ≈ 11 cm × (f1 / 220 Hz)^(−2/3) | The exponent is the steelpan law L = K f^(−2/3); the 11 cm at A3 is my scaling from the schematic | pub (exponent) / inf (constant) | [RHH96] |
| T60 of field partials | **f1: 2–4 s, octave: 2–5 s, fifth: 1.5–4 s**; mean of all 2.9 s | Low notes (B2–B3) mean 3.2 s, mid (C4–E4) 3.0 s, high (F4–C5) 2.5 s; maximum 5.9 s | pub | [ALON] Tab 4.2, App F |
| Partial onset (bloom) | f1 peaks **<23 ms**; octave peaks at **~90 ms**; fifth peaks at **0.2–0.5 s** | Measured on F#3 field. Upper partials are fed by nonlinear energy transfer. | pub | [ALON] Tab 4.4, [SON] |
| Relative levels after strike (dimple centre) | **Octave −3 dB** (range −24 … +21), **fifth −11 dB** (range −24 … +13) re f1 | Low notes have weaker octaves; at ≥D4 the octave is often ≥ f1 | pub/calc | [ALON] Tab 4.1, App E |
| Strike-position gain | Striking the "O" spot raises the octave **+5 dB**; striking the "P" spot raises the fifth **+7 dB** (re centre strike) | | pub | [ALON] §4.3 |
| Beating | **3–9 Hz** AM on partials shared with other fields | Comes from coupled, slightly mistuned fields; almost gone when the other fields are damped | pub | [ALON] Tab 4.3 |
| Sympathetic coupling | Coupled partials of other fields are **+13 … +22 dB** above the isolated-field spectrum; total energy **+4 … +10 dB** | Near 0 dB on a weakly coupled instrument | pub | [ALON] Tab 4.5–4.7 |
| Nonlinearity onset | Vibration amplitude ≈ **h/25 ≈ 0.04 mm** | Measured on a steelpan with 1 mm steel | pub (steelpan) | [MTT12] |
| Amplitude pitch shift | Softening: up to **~ −50 c** at large amplitude; a struck note starts flat and glides up | Steelpan data, not Hang | pub (steelpan) | [MTT12], [ACH] via [ALON] |
| Contact time | **finger ≈ 1–4 ms** (inf); steelpan stick **1–2 ms** (pub) | Longer contact damps the note | pub/inf | [RHH96], [WMR08] |
| Radiation | f1 and octave behave as **monopoles**; the fifth (3rd mode) as a **dipole** | At the octave the radiating region spreads over the whole instrument | pub | [MR07], [R04] |
| Cavity air mode ("forbidden note") | ≈ **465 Hz for a 53 cm shell**, scaling as 1/D (56 cm → A4, 50 cm → B4, 47 cm → C5) | | pub (maker) | [SOMA] |

---

## 1. Construction

### Shell and material
- Two hemispherical shells, the ding (top) shell and the gu (bottom) shell, are deep-drawn with a press from sheet. They are then gas-nitrided in ammonia "for a number of hours at a temperature of 600 °C" [RS07]. PANArt's patent gives an example: DC04 sheet, 0.9 mm thick, 80 cm blank, formed over a 600 mm former, then nitrided in NH₃ at 2.8 bar, 570–585 °C, for 145 h. It allows sheet from 0.75 to 1.25 mm, "usually 0.9 or 1 mm". The finished Hang is about 53 cm in diameter and about 24 cm high [PAT].
- Uniform thickness on the ding shell is **0.91–0.95 mm** [RS07]. Handpan blanks (Ayasa: Ember steel, AISI 430, nitrided DC04) are **0.9–1.0 mm**. Their tuning ring is 530 mm, overall diameter 552–560 mm, flange 11–13 mm, and each shell is 130 mm high [AY]. Some makers use 1.2 mm.
- Other steps from [RS07]:
  - The surface is brushed with brass.
  - Domes are stamped over 3–5 cm plastic balls.
  - The shell is annealed at 400 °C for 15 min, which raises tensile strength by about 10%.
  - Shells are tempered at 150–180 °C to test stability, then retuned and re-tempered.
  - The ding shell is clamped at its 1 cm flange during tuning.
- **Join.** The two shells are "joined with a hybrid-polymer sealant", then a brass protection ring goes on the flange [RS07]. The patent describes the edge as "folded up and inward", then joined [PAT]. A handpan patent also glues the shells "over their entire circumference" [HPPAT]. **Model:** treat the rim as a stiff, lossy clamped ring (inf).
- Material properties PANArt wanted: high Young's modulus, low internal absorption, a ceramic-like surface layer, and high internal compression [RS07].

### Ding and tone fields
- Integral Hang (2006–2008): ding D3 plus 7 fields. Free Integral Hang (2010): the same layout without the brass ring [WIKI], [WMR08].
- Earlier models: high-voice ding G3 with 8 fields (pentatonic: C4 D4 E4 G4 A4 C5 D5 E5); low-voice ding F3 with 8 fields (Akebono: B♭3 C4 C#4 F4 F#4 B♭4 C5 C#5) [WMR08], [R07].
- Field shape: "Eight elliptical note areas" [R07, fig. 3 caption; decoded from the PDF]. Each field has an elliptical dome (dimple) in the middle. In [RS07]'s words, the dome "led to a strong fundamental with two harmonic partials, an octave and a fifth. The higher modes came to lie around the third octave due to the stiffening effect of the dome". The notes have "soft edge conditions and were embedded in the stiff concave shell" [RS07].
- **Orientation (fig/inf).** The integral-Hang layout drawing [WMR08 fig. 4] shows the A3, A4 and D4 ellipses with their long axis pointing at the ding, so the long axis is radial. E4 and F4 are tilted toward the centre too. This matches the steelpan outer ring [RHH96], where the mode "with the nodal line parallel to the circumference supplies the second harmonic". That is the radial-long-axis case. Inner-ring notes of a steelpan are the opposite way round.
- **Sizes.** No millimetre dimensions are published for Hang fields. Scaling the schematic [WMR08 fig. 4] to a 52 cm outline gives (inf, ±30%):
  - ding ≈ 8–9 cm
  - A3 ≈ 8 × 11 cm
  - D4 ≈ 8 × 9 cm
  - A4 ≈ 5.5 × 8 cm
- Use the steelpan law L_major = K·f1^(−2/3) [RHH96] to scale between notes. It is consistent with the schematic: A3 to A4 gives 2^(−2/3) = 0.63.
- Ding domes are stamped over 3–5 cm balls, so the dimple diameter is about 3–5 cm (inf from the [RS07] process).
- Aspect ratio: a steelpan length/width of 5:4 "readily produces" octave:fifth = 2:3 [SOMA]. FE models use b/a = 0.74–0.8 [SON].

### Gu and cavity
- The gu is a large hole in the bottom shell that "acts as the neck of the Helmholtz resonator" [MR07].
  - Hang: "the size of a hand" [PAT], "subtly oval" on the Integral Hang [WIKI].
  - Modern handpan blanks: **ø 82 mm, port neck 19 mm** [AY].
  - The gu rim is itself hammered into a tuned "neck ring bell" (see §2).
- Cavity volume (calc/inf): two caps with a = 0.265 m and h = 0.12–0.13 m give 28–31 L. The inward fields reduce that to **≈ 25 L**. With r = 41 mm, L = 19 mm and end correction 1.5–1.7 r, f_H = c/2π·√(A/(V·L_eff)) gives **84–88 Hz at 25 L** and 77–81 Hz at 30 L. This matches the measured 85 Hz (calc).

---

## 2. Tuning

### Ratios and mode shapes
- Every field and the ding: "three tuned partials with frequencies in the ratios of 1:2:3" [R07], [MR07]. Those three modes are:
  - (0,1): the whole field moves in phase.
  - (1,1)a and (1,1)b: one nodal line bisects the field. The two lines are orthogonal, so these are the two members of the dipole pair, deliberately split by 3:2 [R07].
- The [R07] fig. 3 caption adds "except in the highest notes". On the highest fields the fifth is not tuned.
- Which axis goes with which partial: Alon fig. 2.15 [ALON] and the steelpan rule [RHH96].
  - **Octave:** the nodal line lies along the minor axis, and the antinodes ("O" spots) are at both ends of the major axis.
  - **Fifth:** the nodal line lies along the major axis, and the antinodes ("P" spots) are at both ends of the minor axis.
  - Expected for an ellipse, since the mode varying along the long dimension is the lower one.
- How PANArt gets the ratio: "Shaping a hyperbolic area around the dome leads to a frequency ratio of 2:3 of the (1,0) and (0,1) modes (octave and fifth)". The tuner then sets curvature and lowers or raises the fundamental to hit 1:2:3 [RS07].

### Measured frequencies

Holography uses sinusoidal drive with a small magnet on the field, which slightly mass-loads it.

| Instrument / field | f1 | f2 | f3 | higher | Octave dev. | Fifth dev. | Source |
|---|---|---|---|---|---|---|---|
| Integral Hang, ding D3 | 148 | 295 | 445 | 1344 | −6 c | +4 c | [WMR08] |
| Low-voice Hang, ding F3 | 174 | 348 | 520 | — | 0 c | −7 c | [R07] |
| Low-voice Hang, F#4 field | 368 | 738 | 1104 | — | +5 c | 0 c | [R07] |
| High-voice Hang, ding G3 | 189 | 390 | 593 | 1418, 1543 | +54 c | +78 c | [R07], [MR07] |
| Handpan Inst.4 A4 (post-tuning) | 442.7 | 882.7 | 1324 | 2206 (4.98×) | −5 c | −5 c | [ALON] |
| Handpan Inst.4 F4 / Bb4 / C4 | 351.4 / 467.2 / 262.3 | 700.7 / 936.4 / 525.1 | 1051 / 1403 / 787.5 | | −5 / +4 / +2 c | −5 / +2 / +1 c | [ALON] |
| Handpan Inst.3 G4 / A4 / E4 | 393.9 / 442.8 / 332 | 786.5 / 883.2 / 662.3 | 1178 / 1325 / 990 | | −3 / −5 / −4 c | −5 / −4 / −11 c | [ALON] |
| Handpan Inst.1 F#3 / A3 / B3 | 186.9 / 221.8 / 248.8 | 374.6 / 440.2 / 499 | 556.9 / 663.2 / 744.1 | | +4 / −13 / +5 c | −12 / −6 / −5 c | [ALON] |

(Deviations are my calc. Alon's analysis was zero-padded to a 1.3 Hz bin, which is about ±5 c at 440 Hz, so the instrument tuning is better than the analysis can resolve.)

Notes:
- **Tuning tolerance in cents:** no peer-reviewed figure exists. The measured data above sit mostly within ±5 c of an exact 1:2:3 and all within ±13 c (calc). Maker and retail sources give "±2 c needs attention" and "5–10 c acceptable" [SSB]. They are not authoritative.
- **Pitch drift:** notes drift sharp as stresses relax, so PANArt re-tunes after the shells are joined and again two weeks later, through the gu [RS07], [SOMA].
- **Higher partials:**
  - G3 ding (2,1)a/b at 7.50× and 8.16× f1, about 147 c apart [R07].
  - Integral ding 4th mode at 9.08× f1 [WMR08].
  - Maker statement: the 4th and 5th modes fall "somewhere around the third octave of the fundamental", spaced "50–100 cts apart" [SOMA].
  - Alon sometimes finds a 4×f1 peak (double octave) as one of the four strongest peaks, e.g. D3: 4×, −11.6 dB re f1. It comes from the coupled D4 field's octave, not from the D3 field itself [ALON].
- **Typical scale.** Integral Hang (2008): **D3 | A3 B♭3 C4 D4 E4 F4 A4** [WIKI], [WMR08]. This is the modern "D Kurd" minus G4; the 9-note D Kurd D3 | A3 B♭3 C4 D4 E4 F4 G4 A4 is the handpan standard [SELA, retail listing]. PANArt's "basic tonal structure" for all 2nd-generation Hangs is [RS07]:
  - Helmholtz D2 → ding D3 (its octave)
  - lowest field A3 (fifth of the ding)
  - D4 and A4 (octaves of D3 and A3)
  - the remaining four notes are free
- **Gu neck bell** (Integral Hang): fundamental lowered to **D5**, (1,1)a to **F5**, (1,1)b to **F#5** [RS07]. Wikipedia lists the Integral Hang's two gu-rim notes as F and F# [WIKI].

---

## 3. Helmholtz resonance

- **Measured values:**
  - Integral Hang: a small peak at **85 Hz**, "the frequency of the cavity resonance (near F2)", visible in every mallet-tap spectrum [WMR08].
  - Handpan, C4 field struck: about **85 Hz** measured vs **86.8 Hz** calculated (c = 343 m/s, end correction δ = 0.85). It shifts to about **90 Hz** when the mallet rests on the field [SON].
  - Alon quotes "a spectral peak at approximately 85 Hz" for the Hang [ALON].
- **Design target on the lap: D2 (73.4 Hz).** "We began to build in a kind of an acoustical cathedral, a space of sound based on the air resonance of the HANG played on the lap. This Helmholtz resonance (D2) became the fundamental of a new generation of HANG. The D2 received its octave D3 in the ding" [RS07].
  - So the free-air 85 Hz falls to about 73 Hz with the knees closing round the gu: about −2.5 semitones, consistent (calc).
  - Range quoted: "roughly 60 Hz and 85 Hz" by leg width or partial gu closure [SOMA]; about 65–98 Hz [IDM, retail].
- **Formula used:** f_H = (c/2π)·√(S/(V·L)) [ALON eq. 2.7], with L the effective neck length including end corrections. Alon notes that the inward-curved gu makes S and L ill-defined, so the formula is only approximate [ALON]. Knee spacing changes "the acoustical 'length' of the neck"; a wooden collar (the DUM) changes "its diameter and neck length" [MR07].
- **Coupling to the shell:**
  - The ding "couples strongly to the cavity (Helmholtz) resonance of the body" [R07], [MR07]. Rossing 2004 even calls the central note "formed by the Helmholtz (cavity) resonance" [R04].
  - No published coupled-mode (split) frequencies exist for the Hang.
  - Steelpan analogue: interaction between a bass pan's cavity resonance and some notes "can cause a split in the lowest frequency peak" [ALON, citing Rossing 2000].
  - Evidence of shell compliance: damping the struck field moved f_H from 85 to 90 Hz (+1 semitone) [SON]. The wall compliance adds effective volume (inf).
  - Since f_H ≈ f_ding/2 by design, the coupling is not a linear coincidence of frequencies.
- **Model (inf):**
  - One air mass-spring, driven by the net volume velocity of the ding's (0,1) mode and of every field's monopole (0,1) mode, with a weaker share from the octave modes.
  - Its pressure acts back on all shell modes.
  - Strength set so that a ding strike leaves the 85 Hz peak about **40–50 dB below the ding fundamental** (fig, light mallet tap [WMR08 fig. 9]). Palm or "bass" strokes raise it a lot; no published level.
  - Air-mode Q of roughly 10–30, typical of open-port resonators (inf).
- **Playing the gu.** Techniques in [R07] fig. 2 (decoded):
  - "Exciting the bass": a palm or flat-hand stroke on the ding, or a palm over the gu.
  - "Softly striking the DING".
  - "Changing the size of the GU" with the hand or knees.
  - "DUM in the GU".
  Handpan players excite the port with the palm ("Helmholtz activation"), and some makers tune the port lip to the scale [ALON]. Covering the gu raises the effective neck inertance and lowers f_H. Fully sealing it removes the resonance (inf; consistent with [SOMA], [IDM]).
- **Higher cavity modes.** The first non-Helmholtz air mode of the lens-shaped cavity sits at about **465 ± 4 Hz in a 53 cm shell**, scaling as 1/D: 56 cm → A4, 50 cm → B4, 47 cm → C5. Makers avoid placing note partials there [SOMA]. Alon also cites a "problematic frequency" tied to the cavity diameter [ALON].

---

## 4. Sympathetic resonance between fields

- **Holography, low-voice Hang (F3 ding)** [R07], [MR07]. Driving at the ding's partials:
  - **At 348 Hz** (ding octave) the F4 field (fundamental) is "strongly driven" and F#4 weakly.
  - **At 520 Hz** (ding fifth) the C4 field's (1,1)a (octave 523 Hz) and the C5 field's (0,1) "show appreciable response".
  - Driving the F#4 field at its three partials (368 / 738 / 1104 Hz) also drives the C#5 field's (1,1)a mode (C#6 ≈ 1109 Hz is C#5's octave).
  - Raising the drive amplitude couples more notes: at least 3 extra fields at 348 Hz, high vs low amplitude [ALON on R07]. **The coupling is amplitude-dependent, so it is nonlinear.**
- **High-voice Hang:** D4's octave at **604 Hz** and the D5 field's fundamental, also 604 Hz, vibrate together. Phase maps were used to study this [R07], [MR07].
- **Physics Today 2009:** driving the C5 field at 524 Hz makes C4 (its octave mode) and the F3 ding (its fifth mode) vibrate with double lobes. C#5 and A#4 show small motion, "most significant for high-amplitude excitation" [PT09].
- **Quantified (Alon, handpans; undamped vs other fields covered with magnetic pads, rubber-tipped striker at the dimple):**
  - Inst. 1 D4 field: six extra peaks between 115 Hz and 1 kHz, all partials of other fields on that pan:
    - B2: +21 dB
    - F#3: +15 dB
    - E4: +16 dB
    - F#4: +15 dB
    - B4: +13 dB
    - F#5: +22 dB
  - Inst. 3 D4 field:
    - D3 (ding): +20 dB
    - A4: +11 dB
    - E5: +13 dB
  - Total-energy ratio undamped/damped for D4 [ALON Tab 4.7]:
    - Inst. 1: +10 dB
    - Inst. 3: +5.7 dB
    - Inst. 4: +4.3 dB
    - Inst. 2: ≈0 dB, i.e. weak coupling. Coupling strength is a maker/instrument property.
  - Marshall-pair-like pairs, where one field's octave equals another field's fundamental (F#3↔F#4, B2↔B3, D3↔D4), are strongly linked. The D3 field's third-strongest partial became 4×f1 (the D4 field's octave) when undamped. With the other fields damped, D3 measured 152.2 / 298.1 / 449.2 Hz (1:2:3) [ALON].
- **Steelpan analogue for the mechanism** [MTT12]: the octave of G3 and the fundamental of G4 form two **global** modes at 390.0 and 397.8 Hz (2% apart, about 34 c). In one, the G4 pattern is in phase with G3's octave pattern; in the other, out of phase. The same happens at 4f1: 789.5 / 799.3 Hz. Coupling across fields is quadratic (1:2:2 and 1:2:2:4 internal resonance) and activates at **0.04 mm = h/25** vibration amplitude. Achong found that coupling is non-reciprocal: striking A excites B differently from striking B [ALON on Achong].
- **Model (inf):**
  - Couple field partials whose frequencies match within about 10 Hz through a linear spring (gives splitting and beating) plus a quadratic term (gives growth with velocity).
  - Target levels: a coupled partial on a neighbouring field reaches −10 to −25 dB re the struck field's f1 (from the +13 … +22 dB increments over a damped background). Total level gain from the network ≤ +10 dB.

---

## 5. Strike

- **Contact time.**
  - Not measured for fingers on Hang or handpan.
  - Steelpan stick on a tenor pan: **1–2 ms** [RHH96].
  - Wessel et al. note that "the longer the contact time the greater the damping effect". Beginners fail to "pull away quickly", which damps the note; damping is controlled by contact time and by the palm [WMR08].
  - **Inferred finger contact: 1–4 ms** for a good "rapid and gentle" stroke, longer for palm and flat-hand strokes (5–20 ms, inf). This is a lumped fingertip mass of 5–20 g on a pad stiffness of 3–10 kN/m, giving π√(m/k) ≈ 2–6 ms, shortened by the rebound technique.
  - Spectral effect (calc): a half-sine force of duration τ has its first spectral null at 1.5/τ. τ = 2 ms puts it at 750 Hz, τ = 4 ms at 375 Hz.
  - The steelpan literature finds stick impacts close to half-sine force pulses [ALON citing Achong]; Hertz contact gives sin^1.5.
  - **Model note (inf):** much of the octave and fifth energy at the dimple centre comes from nonlinear transfer, not directly from the force spectrum (see the onset delays in §6).
- **Strike position** [ALON] (F#3 field, Inst. 1, initial-magnitude change re a dimple-centre "D" strike):
  - **"D" (dimple apex):** the nodal-line crossing of both the octave and the fifth. It favours the fundamental, yet is "still considered the best impact point for maximum excitation of all the vibrational modes" [ALON citing Achong].
  - **"O" (ends of the long axis):** octave **+5 dB** (damped field: +2 dB, octave T60 +0.3 s).
  - **"P" (ends of the short axis):** fifth **+7 dB** (damped field: fifth T60 +0.3 s).
  - The fundamental's decay curve is nearly identical for all three positions.
  - Undamped, the order of the strongest partials changed with position; damped, it stayed f1 > octave > fifth > double octave regardless of position.
  - Striking the field boundary brings out "shoulder tones". Striking between fields gives a short, percussive, non-ringing sound [ALON].
- **Harmonic isolation:** damping one O spot while striking the P spot leaves the fifth and suppresses the octave, and vice versa. Pressing the damping finger gives "a slight pitch bend" [ALON].
- **Velocity dependence** [WMR08]:
  - ppp: "but two audibly significant components in an octave relationship".
  - fff: "considerably more sinusoidal tracks", higher components "inharmonic with respect to the fundamental", "significant amplitude modulations due to beating", and the loud note "rings longer than the soft one".
- **Steelpan analogues:**
  - Harder impacts add components rather than scaling the spectrum [ALON].
  - Pitch glide: the first mode starts below its set frequency and rises during the attack of a strong stroke (soft-spring behaviour, Achong).
  - Forced-response peak moved from 197.6 to 192 Hz (−50 c) at high drive [MTT12].
  - Maker view: the quadratic coupling coefficient is proportional to the dome's rise-to-thickness ratio, and a flat plate has none [SOMA].
- **Model (inf):**
  - Brightness: let quadratic coupling feed the octave from f1 (1:2) and the fifth from f1+octave (1:2:3 combination). Scale the coupling so the octave reaches its peak at about 90 ms at mf.
  - Pitch: a downward pitch offset of f1 proportional to amplitude, a few cents at mf and up to tens of cents at fff, relaxing upward as the note decays.
- **Flams and rolls:** each stroke "finds the HANG in a different state", so successive strokes differ in timbre. Wessel modelled this with resonant filters whose state is not reset between strokes [WMR08]. **Model:** never reset resonator state on retrigger.

---

## 6. Decay, attack, beating, radiation

**T60 of the three strongest partials, D-spot strike, other fields free** [ALON Tab 4.2]. T60 here is the time to fall 60 dB below the *loudest* partial's initial level, so weaker partials are biased short.

| Group | Mean T60 | SD | Min | Max |
|---|---|---|---|---|
| Inst. 1 / 2 (same maker) | 3.3 s | 1.1–1.2 | 1.6–1.7 | 5.9 |
| Inst. 3 | 2.8 s | 0.7 | 1.4 | 4.0 |
| Inst. 4 | 2.1 s | 0.5 | 0.9 | 3.4 |
| Low B2–B3 | 3.2 s | 0.9 | 1.6 | 5.1 |
| Mid C4–E4 | 3.0 s | 1.2 | 1.2 | 5.9 |
| High F4–C5 | 2.5 s | 0.9 | 0.9 | 4.2 |

Per partial (calc: Appendix F values mapped back to partials via Appendix E). Values are f1 / octave / fifth, in seconds:
- **Inst. 1:**
  - B2 ding: 2.8 / 2.1 / –
  - F#3: 2.5 / 3.2 / 4.7
  - A3: 2.7 / 2.5 / 4.1
  - B3: 4.0 / 5.1 / (8.9, unreliable)
  - C#4: 3.0 / 4.0 / –
  - D4: 2.6 / 5.9 / –
  - E4: 2.8 / 4.6 / –
  - F#4: 4.0 / 3.8 / –
- **Inst. 3 (post-tuning):**
  - D3 ding: 2.1 / 2.9 / –
  - A3: 2.2 / 2.4 / 4.3
  - A#3: 1.9 / 3.6 / 4.9
  - D4: 1.7 / 1.7 / 1.9
  - E4: 1.9 / 4.7 / 3.3
  - G4: 2.8 / 3.6 / 2.6
  - A4: 3.0 / 3.7 / 2.3
- **Inst. 4 (post-tuning):**
  - G3 ding: 1.9 / 2.1 / 2.5
  - C4: 2.0 / 2.8 / 2.6
  - D4: 1.5 / 2.9 / 2.9
  - F4: 1.8 / 2.9 / 1.8
  - A4: 2.0 / 2.5 / 1.7
  - Bb4: 1.9 / 2.3 / 1.3
  - C5: 2.3 / 1.8 / –

Takeaways:
- On a playing instrument, **octave and fifth decay about as slowly as f1, often more slowly**. Coupled partners keep re-feeding energy. With the other fields damped, the octave and especially the 2×-octave decay much faster [ALON §4.3–4.4].
- The A3 synthesis check [ALON Fig 5.2] gave measured T60 2.7 s (f1 223 Hz), 4.1 s (664 Hz) and 2.5 s (914 Hz).
- Wessel: the loud note rings longer [WMR08]. Audible duration of signature modes is "4–6 s" [ALON].
- **Model (inf):** isolated-field partial T60 about 2–3 s for f1, 1.5–2.5 s for the octave, 1–2 s for the fifth. Let the coupling network produce the longer apparent octave and fifth decays.

**Attack and onset** [ALON Tab 4.4] (F#3 field, ±23 ms frame resolution). Time for each partial to reach its spectrogram peak:

| Partial | Undamped | Damped |
|---|---|---|
| f1 (186 Hz) | 23 ms | 23 ms |
| octave (374 Hz) | 92 ms | 92 ms |
| fifth (556 Hz) | 510 ms | 372 ms |
| 2×octave (748 Hz) | 209 ms | 92 ms |

- The broadband transient lasts about 4–10 ms [ALON §4.2.1]. The excitation signal recorded between fields (everything damped, gu covered) was cropped at 10 ms [ALON §5.1.2].
- Higher partials "bloom". The coupled G4 fifth has "a noticeably longer attack" when driven through coupling [SON].
- Rossing 2004 gives a gong analogue: energy cascades from low to high modes after a delay [R04 via ALON].

**Beating and doublets** [ALON Tab 4.3] (Inst. 1, undamped):

| Field | Partial | Beat rate |
|---|---|---|
| A3 | 662 Hz (fifth) | 3.3 Hz |
| A3 | 438 Hz (octave) | 8.6 Hz |
| B3 | 248 Hz (f1) | 3.9 Hz |
| F#3 | 373 Hz (octave) | 3.1 Hz |
| F#4 | 374 Hz (f1) | 3.3 Hz |

- Beating depth is "drastically reduced" with the other fields damped, so the cause is **coupling between mistuned fields**, not intrinsic doublets.
- Alon re-synthesised it with a second oscillator 3.3 Hz above the 662 Hz partial (665.3 Hz).
- The Hang's own (1,1)a and (1,1)b are deliberately not degenerate (2:3). No intrinsic doublet splitting has been measured for Hang fields.
- Steelpan global doublets sit 7.8–9.8 Hz apart near 400 and 800 Hz [MTT12]. The steelpan [MTT12] and Hang [R07] mode pictures support a 2–10 Hz split for coupled pairs.
- Alon also saw two-slope decay on f1 (one fast and one slow component near 186.9 Hz), which suggests a close mode pair [ALON §4.2.2].

**Radiation:**
- Active intensity 8 cm above the Hang for the D4, E4 and A4 fields [MR07], [R07]:
  - **Fundamental:** monopole, peak directly over the struck field.
  - **Second resonance (octave):** monopole-like, but the largest intensity is "centered over the instrument and distributed over a large portion of the instrument". The coupled ding and other fields radiate it.
  - **Third resonance (fifth):** dipole.
- Reactive intensity circulates at all three frequencies, "an exchange of energy between the front and back of the instrument" [MR07].
- Above E4, "considerable sound is radiated laterally" at the 2nd and 3rd modes [R04].
- Gu-side radiation adds the Helmholtz monopole.
- **Model (inf):** sum f1 and the octave as monopole sources; give the fifth a dipole pattern (cos θ). If stereo is needed, give it a weak near-field cancellation, about −3 to −6 dB relative to the monopole sum at the listener.

---

## 7. Published spectra and partial levels

**(a) Alon, handpans, rubber-tipped striker at dimple centre.** Values are levels of the first EDR frame after the transient (~4–10 ms, 46 ms frame), microphone 50 cm above the ding. They are relative to f1, in dB (calc from App. E / Tab 4.1); "–" means the partial was not among the 4 strongest peaks.

| Note | Octave re f1 | Fifth re f1 | Source table |
|---|---|---|---|
| Inst.1 B2 (ding) | −2.6 | – | 6.2 |
| Inst.1 F#3 | −10.8 | −6.6 | 6.2 |
| Inst.1 A3 | −13.4 | −2.2 | 6.2 |
| Inst.1 B3 | −3.2 | −11.2 | 6.2 |
| Inst.1 C#4 | −16.6 | – | 6.2 |
| Inst.1 D4 | +7.7 | −18.7 | 6.2 |
| Inst.1 E4 | −1.9 | – | 6.2 |
| Inst.1 F#4 | +8.9 | – | 6.2 |
| Inst.2 C3 (ding) | −15.0 | −11.7 | 6.3 |
| Inst.2 G3 | −7.1 | – (4f −1.3) | 6.3 |
| Inst.2 Ab3 / B3 | −23.6 / −10.0 | −24.2 / −24.3 | 6.3 |
| Inst.2 C4 / D4 / Eb4 / G4 | −3.4 / +1.1 / 0.0 / +2.7 | – / −14.3 / – / −11.9 | 6.3 |
| Inst.3 pre D3 (ding) | −6.5 | −22.1 | 4.1 |
| Inst.3 pre A3 / A#3 | −0.3 / −22.4 | −7.3 / −20.0 | 4.1 |
| Inst.3 pre D4 / E4 / F4 | +1.8 / −1.7 / +0.5 | −15.3 / −13.4 / −16.6 | 4.1 |
| Inst.3 pre G4 / A4 | −9.5 / −3.7 | −11.3 / −9.6 | 4.1 |
| Inst.4 post G3 (ding) | −4.5 | +0.5 | 6.6 |
| Inst.4 post C4 / D4 / E4 | +5.8 / +12.7 / +4.5 | −5.8 / +12.7 / −13.2 | 6.6 |
| Inst.4 post F4 / A4 / Bb4 / C5 | +9.3 / +0.6 / −4.3 / −12.1 | −8.5 / −14.6 / −10.2 / – | 6.6 |

Medians across this sample (calc): **octave ≈ −3 dB, fifth ≈ −11 dB re f1**. Low notes (≤B3) lean toward a weak octave (−3 to −15 dB). Notes ≥C4 often have octave ≥ f1, probably reinforced by octave-pair coupling and radiation, since the mic is over the centre. The spread is ±10 dB between instruments, so these are instrument-specific voicings, not universal constants.

**(b) Wessel et al., Integral Hang, light rubber-mallet tap** (fig, from [WMR08] fig. 9; dB re f1):

| Note | Octave | Fifth | Other |
|---|---|---|---|
| D3 ding | ≈ −8 | ≈ −45 | 85 Hz cavity ≈ −45 |
| A3 | ≈ −17 | ≈ −35 | |
| B♭3 | ≈ −22 | ≈ −29 | |
| C4 | ≈ +1 | ≈ −20 | |
| D4 | ≈ −30 | ≈ −90 (barely visible) | |
| E4 | ≈ −9 | ≈ −22 | |
| F4 | ≈ −18 | ≈ −34 | |
| A4 | ≈ −17 | ≈ −53 | 4×f1 visible at ≈ −65 |

A soft tap is close to linear, so (b) shows how weak the fifth is without nonlinear transfer. Compare (a), which is a harder strike.

**(c) Over time:** see §6. f1 peaks first (<23 ms), the octave at about 90 ms, the fifth at 0.2–0.5 s. Then all decay with T60 of about 2–5 s. Undamped octaves and fifths often outlast f1.

---

## Source list

- **[R07]** Rossing, T.D., Morrison, A., Hansen, U., Rohner, F., Schärer, S. (2007). "Acoustics of the HANG: a hand-played steel instrument." *Proc. ISMA 2007*, Barcelona. https://www.hangblog.org/panart/2-S2-4-IsmaRossing.pdf (landing page https://www.hangblog.org/?p=27). Figure labels (174/348/520 Hz, 368/738/1104 Hz, fig. 3 caption) were decoded from a garbled font in the PDF.
- **[MR07]** Morrison, A., Rossing, T.D. (2007). "Modes of vibration and sound radiation from the HANG." *Archives of Acoustics* 32(3), 551–560. https://acoustics.ippt.pan.pl/index.php/aa/article/download/709/627
- **[RS07]** Rohner, F., Schärer, S. (2007). "History, development and tuning of the HANG." *Proc. ISMA 2007*, Barcelona. https://www.hangblog.org/panart/Paper-Hang-2007.pdf (page https://www.hangblog.org/?p=108)
- **[WMR08]** Wessel, D., Morrison, A., Rossing, T.D. (2008). "Sound of the HANG." *Proc. Meetings on Acoustics* 4, 035002, doi:10.1121/1.3068630. https://cnmat.berkeley.edu/sites/default/files/2008_sound_of_the_hang.pdf ; lay summary https://acoustics.org/pressroom/httpdocs/155th/wessel.htm
- **[PT09]** Morrison, A., Rossing, T.D. (2009). "The extraordinary sound of the hang." *Physics Today* 62(3), 66–67, doi:10.1063/1.3099586. https://aip.brightspotcdn.com/PTO.v62.i3.66_1.online.pdf
- **[R04]** Rossing, T.D., Yoo, J., Morrison, A. (2004). "Acoustics of percussion instruments: An update." *Acoust. Sci. & Tech.* 25(6), 406–412. https://www.jstage.jst.go.jp/article/ast/25/6/25_6_406/_article
- **[ALON]** Alon, E. (2015). *Analysis and Synthesis of the Handpan Sound*. MSc by Research, University of York. https://etheses.whiterose.ac.uk/12260/1/EyalMSc.pdf
- **[RHH96]** Rossing, T.D., Hampton, D.S., Hansen, U.J. (1996). "Music from oil drums: the acoustics of the steel pan." *Physics Today* 49(3), 24–29. https://aip.brightspotcdn.com/PTO.v49.i3.24_1.online.pdf
- **[MTT12]** Monteil, M., Touzé, C., Thomas, O. (2012). "Complicated dynamics exhibited by thin shells displaying numerous internal resonances: application to the steelpan." *Proc. ICSV19*, Vilnius. https://perso.ensta-paris.fr/~touze/PDF/ICSV2012Monteil.pdf . Full study: Monteil, Thomas, Touzé (2015), "Identification of mode couplings in nonlinear vibrations of the steelpan", *Applied Acoustics* 89, 1–15, https://hal.archives-ouvertes.fr/hal-01084240
- **[ACH]** Achong, A. (1996–2003), steelpan nonlinear mode-localised oscillator papers (J. Sound Vib.) and "The theory of the steelpan", *Proc. ICSTS 2000*. Cited here only via [ALON] and [RS07]; not read directly.
- **[PAT]** Rohner, F., Schärer, S. (PANArt). US 8,552,279 B2, "Method for production of a metallic-sounding musical instrument." https://patents.google.com/patent/US8552279B2/en
- **[HPPAT]** US 11,727,900 B2, "Handpan." https://patents.google.com/patent/US11727900B2/en
- **[AY]** Ayasa Instruments, shell specifications (Ember steel / AISI 430 / DC04). https://ayasainstruments.com/ember-steel
- **[SOMA]** Soma Sound Sculptures, "Scientifics" (maker's technical page, not peer-reviewed). https://somasoundsculptures.com/en/instruments/scientifics
- **[SON]** Sonores Acoustics, "Sound of the handpan: analysis and synthesis" (project page, not peer-reviewed). https://www.sonoresacoustics.be/portfolio/handpan
- **[WIKI]** "Hang (instrument)", Wikipedia. https://en.wikipedia.org/wiki/Hang_(instrument)
- **[IDM]** Instruments du Monde, handpan guides (retail, low weight). https://instruments-du-monde.com/en-us/blogs/handpan/handpan-how-it-works
- **[SSB]** Sound Sculpture, "Handpan tuning: how does it work?" (retail, low weight). https://sound-sculpture.de/en/blogs/handpan/handpan-tuning
- **[SELA]** e.g. Sela Harmony D Kurd listing (D3 A3 Bb3 C4 D4 E4 F4 G4 A4). https://www.musiciansfriend.com/drums-percussion/sela-harmony-handpan-d-kurd-stainless-steel-with-bag-d3-a3-bb3-c4-d4-e4-f4-g4-a4/l73823000000000
- Not obtained: Morrison, A. (2005/2006), PhD dissertation, Northern Illinois Univ. (restricted; https://huskiecommons.lib.niu.edu/allgraduate-thesesdissertations/970). Rossing et al. (2001), "The HANG: a hand-played steel drum", JASA 110, 2649 (abstract only). Alon & Murphy, DAFx-15 conference version of [ALON].
