# Udu (clay pot drum) acoustics: hard numbers for a physical synthesis model

Compiled 2026-11-23 from the published literature, as the survey the udu kit (`src/udu.h`) is built from; `docs/udu.md` maps the model's constants onto it. Bracketed tags such as [AHG13] refer to the source list at the bottom.

How each value is marked:
- **pub**: stated in the source.
- **fig**: read off a published figure by me, so about ±10–20% on widths and ±3 dB on levels.
- **calc**: my arithmetic on published numbers. The scripts are reproduced in the text where it matters.
- **inf**: my inference or estimate. Nothing has been published on it.

Caveat on the literature: only two measurement papers on udus exist.
- **[AHG13]** Anderson, Hilton & Giorgini, JASA 2013. This is an LP/Giorgini *Udu Utar*, measured in an anechoic chamber, with an equivalent-circuit model and scanning laser vibrometry of six shell modes.
- **[VA22]** Vieira & Aguiar, Physics Education 2022. This is an unnamed udu of the traditional shape, with three measured Helmholtz "beats" and a spectrogram of the glide.

A Brazilian *moringa* (a small udu-like pot) gives the only published Helmholtz **Q** [AF21]. I found **no** peer-reviewed acoustic measurement of a ghatam, no measured loss factor for low-fired terracotta, and no measured palm contact time on any hand drum. Those values below are labelled **inf**.

**Naming trap.** In [AHG13] the *"side hole"* is the opening at the end of the Utar's horizontal **neck**, and the *"top hole"* is the hole in the **cavity wall**. This note uses traditional-udu terms throughout:
- **mouth/neck**: the opening at the end of the neck.
- **body hole**: the hole in the cavity wall. On the traditional instrument this is the "side hole".

So Utar "SOTC" (side open, top closed) means *mouth only*, and "SCTO" means *body hole only*.

---

## 0. Numbers for the model (summary table)

| Quantity | Value to use | Range / notes | Tag | Source |
|---|---|---|---|---|
| Cavity volume V | **9–10 L** | Utar 9.25 L; traditional-shape udu 10.2 L (both water-filled). Moringa 1.55 L. | pub | [AHG13], [VA22], [AF21] |
| Body diameter × height | **~28–30 cm × 33–40 cm** | Igbo pot 38 × 29 cm (H × D); KU udu 34.5 × 28 × 29.5 cm; "just under a foot across"; LP Claytone #2/#3/#4: 13×9″, 15×10″, 17×11″ | pub | [NMAA], [SPEN], [BALT], [THO] |
| Largest internal dimension | **~30 cm** (Utar) | Cavity length from neck junction to body hole is 22.9 cm | pub | [AHG13] |
| Wall thickness | **6–12 mm** | Utar 6 mm at the hole exits; udu body-hole "length" (= wall) 12 mm; moringa 7 mm; kimkim 6.4 mm upper / 9.5 mm lower | pub | [AHG13], [VA22], [AF21], [BALT] |
| Mouth (neck) | **inner r ≈ 2.2 cm, neck L ≈ 6.3 cm** (traditional shape) | Utar neck 14.6 cm long, flared r = 2.9 → 4.7 cm, then an orifice of r = 3.5 cm; traditional "neck ~2 in, opening ~1 in" | pub | [VA22], [AHG13], [BALT] |
| Body hole | **r = 2.5–3.0 cm in a 6–12 mm wall** | Utar r = 2.5 cm (t = 6 mm); udu r = 3.0 cm (t = 12 mm); moringa d = 39 mm (t = 7 mm). It is a short orifice, not a tube. | pub | [AHG13], [VA22], [AF21] |
| f, mouth only (body hole covered) | **68–73 Hz** | 68 Hz [VA22]; 73 Hz [AHG13] (D2) | pub | [VA22], [AHG13] |
| f, body hole only (mouth covered) | **113–116 Hz** | 116 Hz [VA22]; 113 Hz [AHG13] (A2) | pub | [VA22], [AHG13] |
| f, both open | **136–137 Hz** | 136 Hz [VA22]; 137 Hz [AHG13] (C#3) | pub | [VA22], [AHG13] |
| Two-opening rule | **f₁₂² = f₁² + f₂²**, equivalently conductances add: f = (c/2π)·√(ΣSᵢ/L′ᵢ / V) | Predicts 134.5 Hz for both instruments vs 136/137 measured (1–2% low) | pub | [VA22], [AHG13], [LHG19] |
| End corrections (each end) | unflanged **0.6133a**; flanged **0.82–0.85a**; thin orifice in a wall: Rayleigh conductance **K = 2a**, i.e. **L′ = πa/2 ≈ 1.57a** total | [VA22] used L′ = L + 1.4R (neck) and L + 1.7R (wall hole), giving 68/113/132 Hz vs 68/116/136 measured. The simple formula overestimates acoustic pots by ~7% [CDL02]. | pub | [EMSM26], [BS20], [VA22], [CDL02] |
| Glide (slap body hole, hold, release) | **mouth-only → both: +1 octave** (1090–1200 c) | 68→136 Hz = 1200 c; 73→137 Hz = 1090 c. Always rises. | pub/calc | [VA22], [AHG13] |
| Glide (mouth covered → open while striking body hole) | **113→137 Hz = +333 c** (Utar); 116→136 = +275 c | | calc | [AHG13], [VA22] |
| Glide duration (hand release) | **≤70 ms** apparent in the spectrogram (STFT-smeared); physical estimate **10–30 ms** for most of the rise | Low tone held ~140 ms before release in [VA22] fig. 5 | fig/inf | [VA22] |
| Helmholtz Q (small amplitude) | **Q ≈ 50–60** (mouth only, viscous-dominated); **Q ≈ 45–50** both open (radiation-dominated) | Moringa measured **Q = 57** at 138 Hz; my loss budget predicts 60 for the moringa (good check) | pub/calc | [AF21], calc §2.6 |
| Helmholtz Q (forte slap) | **Q ≈ 15–25 early in the decay**, rising as the amplitude falls | Orifice jet (nonlinear) loss at u ≈ 3 m/s | inf | §2.6 |
| T60 of the bass | **0.8 s** both open (Q 48 @ 136 Hz); **~0.9 s** moringa; mouth only up to ~1.9 s small-signal; **0.3–0.5 s** for a hard slap or with the hand near the opening | | calc/inf | §2.6, §3 |
| Hand hovering at height h over a hole of radius a | Added length **ΔL ≈ (a²/2h)·ln(b/a)** (b = palm radius ≈ 6 cm) | For a = 3 cm: ΔL ≈ 31 / 16 / 6 / 3 cm at h = 1 / 2 / 5 / 10 mm, giving f(both) = 83 / 92 / 106 / 116 Hz vs 134 Hz open | inf (radial-flow inertance) | §2.5 |
| Water added | **+1 L raises f by ~3–4%, +2 L by ~9–11%** | Utar: body-only 113 → 116.5 → 123.5 Hz; both 137 → 143.5 → 151.5 Hz | pub | [AHG13] |
| Shell ("clay") modes, Utar | **556, 970, 1108, 1378, 1470, 1644 Hz** | Lip of the neck end: 556 + 1108 + 1644 (1 : 1.99 : 2.96, "cowbell"). Near the body hole: 970 + 1898 (1 : 1.96). | pub | [AHG13] |
| Shell loss factor η (fired clay) | **0.01–0.02** (Q 50–100) for low-fired porous udu clay | Porous earthenware Q⁻¹ ≈ (100–250)×10⁻⁴; porcelain bells calc η ≈ 0.006–0.013; building brick ≈ 0.01. Lap contact adds much more. | pub (units unverified)/calc/inf | [DAV], [BAR25], [GG] |
| T60 of clay tones | **0.2–0.4 s at ~550 Hz, 0.07–0.15 s at ~1.6 kHz** (free); much shorter on the lap | From η = 0.01–0.02. [AHG13] says only "often ring for some time". | inf | §4 |
| Young's modulus E | **8–15 GPa** for a low-fired (650–900 °C) udu | 9.3 GPa at 1100 °C with 30% porosity; 15–68 GPa for five illitic clays after 1100 °C; ~8 GPa below 800 °C | pub/inf | [JCS15], [HUL20], [OPF] |
| Density ρ | **1850–1950 kg/m³** | 1.84–2.27 g/cm³ after 1100 °C; database value 1600–2100 | pub | [HUL20], [SPOT] |
| Poisson ν | **0.15** (0.1–0.2) | Only a database value (0.1) exists | inf | [SPOT] |
| Shell ring frequency | **~2.5–3 kHz** (R = 13 cm, E = 10 GPa) | c_L = √(E/ρ(1−ν²)) ≈ 2300 m/s. The measured modes (0.5–1.6 kHz) are below it, so they are bending-dominated. | calc | §4 |
| First non-Helmholtz cavity mode | **~600–900 Hz** | Rigid sphere of 9.25 L: ka = 2.08 gives 873 Hz; 30 cm long axis gives c/2L ≈ 570 Hz; acoustic pot measured 297 → 1260 Hz (4.2×) | calc/pub | [CDL02] |
| Radiation, Helmholtz mode | Two **in-phase** monopoles; body hole **~+9 dB** louder than the mouth (volume-velocity ratio M_mouth/M_hole ≈ 2.8) | kd ≈ 0.6, so the sum is omnidirectional within ~0.5 dB. A pot at resonance is measured omnidirectional. | calc/pub | §5, [CDL02] |
| Palm pressure pulse into cavity | **~40–200 Pa peak** (palm pad intrudes 1–5 mm into a 3 cm-radius hole); up to ~1 kPa for hard slaps | Handclap cavity gauge pressure is 0.1–5 kPa and scales ~u² | calc/inf, pub (clap) | [FU25], §6 |
| Palm contact / pulse duration | **~4–10 ms** rise-and-fall of the pressure pulse | Stick–head contact 3–6 ms and 5–8 ms (pub). Handclap collision-to-jet is ~10 ms, and its sound decays in ~10 ms (pub). | inf | [WAG06], [DAHL], [FU25] |
| Effective palm mass | **~0.4 kg** (hand ≈ 0.6% of body mass); more with arm follow-through | | inf | — |

---

## 1. Vessel geometry

### 1.1 The two measured udus

| | Udu Utar [AHG13] | Traditional-shape udu [VA22] | Moringa [AF21] |
|---|---|---|---|
| Cavity volume | **9.25 L** (water fill) | **10.2 L** (water fill) | **1.55 L** |
| Neck | 14.6 cm long, horizontal. Four segments of lengths [4.1, 3.1, 3.6, 2.0] cm and radii [4.7, 4.0, 3.3, 2.9] cm, flaring *toward* the opening, then "abruptly constricts" to an orifice of r = 3.5 cm (S = 0.0038 m²) | L = 6.3 cm, R = 2.2 cm; L′ = 9.4 cm (L + 1.4R) | L = 73 mm, d = 33 mm |
| Body hole | r = 2.5 cm (S = 0.0020 m²) in a t = 6 mm wall | R = 3.0 cm, L = 1.2 cm (wall); L′ = 6.3 cm (L + 1.7R) | d = 39 mm, wall 7 mm |
| Other | Cavity length from neck junction to body-hole centre 22.9 cm; effective cavity area 0.041 m²; largest internal dimension ~30 cm; c = 340 m/s, ρ = 1.01 kg/m³ (Provo, altitude) | c = 340 m/s | — |

All values are **pub**. The neck-to-cavity junction in [AHG13] is modelled as a Karal discontinuity mass M = 8ρ/(3π² r_N4 H(b/a)) with H = 0.7.

### 1.2 Variants and catalogue sizes
- **Traditional Igbo udu.** A water pot with a narrow flared neck and a circular hole "midway on the side" [NMAA]. It is globular. Made by Igbo women from clay and sand, open-fired and unglazed [BALT]. Recorded sizes:
  - Smithsonian NMAfA Igbo pot: 38 × 29 cm (H × D) [NMAA].
  - Spencer Museum udu: 34.5 × 28 × 29.5 cm [SPEN].
  - A hobbyist description gives the body as "just under a foot across", the neck "about 2 inches tall", and both the mouth and the side hole "about 1 inch" in diameter [BALT]. The 1-inch mouth is smaller than the 4.4 cm neck measured in [VA22]. Treat [BALT] as low reliability.
  - The Ibibio name is *abang mbre* ("pot for playing"); the Yoruba is *ikoko-ilu* [BALT].
- **Kimkim / akimkim** (the double-chamber relative). Upper body ~4″ diameter, waist ~2″, lower body ~6″. Wall **1/4″ (6.4 mm) upper, 3/8″ (9.5 mm) lower**. Both openings 1.5″ (38 mm) [BALT]. The Met's 1989 kimkim measures 40 × 20.3 × 20.3 cm [MET].
- **Giorgini / Udu Inc. / LP.** The Met's Giorgini udu is 30.6 cm long, with a large-end diameter of 17.4 cm and a small-end diameter of 10.8 cm [MET]. Giorgini studied with Abbas Ahuwan (Zaria). The design of the Utar took "2–4 years" by ear [AHG13]. The LP line includes Claytone #1–#4, Udongo II, Utar, Hadgini, Tambuta and Mbwata [THO]. Sizes:
  - Claytone #2: 13″ H × 9″ D [THO].
  - Claytone #3: 15″ × 10″ (381 × 254 mm) [THO].
  - Claytone #4: 17″ × 11″ [THO].
  - Utar: per retailers, the "lowest pitch of all the Udus". Its acoustic numbers are those in §1.1.
  - **Mbwata**: two chambers, "large round chamber: deep, dry bass" and "small flat chamber: bright, sharp, wet tones". It has one top hole and one side hole, plus a mic port in each chamber [ART]. Model it as two coupled Helmholtz cavities (inf).
  - Udu Inc.'s own site (udu.com) no longer serves product pages.
- **Ghatam (South India), for comparison.**
  - Height **~13″ (33 cm)** [GHU], [GHA-S].
  - "Largest diameter" given as 32.5–36″ [GHU]. That is impossible as a diameter for a 13″-high pot, so it is almost certainly the **circumference**, giving d ≈ 26–29 cm (inf).
  - Mass **5–8 kg** fired, from a ~16 kg green pot [GHU].
  - Clay is mixed with brass/copper and some iron filings [WGH]. Manamadurai pots are "at least twice as thick as" cooking pots [GHA-S].
  - Wall thickness from mass: 5–8 kg over ~0.2–0.25 m² at ρ = 1900 gives **t ≈ 10–20 mm** (calc).
  - The mouth is narrow with an outward-slanting ridge, and no diameter is published (I would estimate 7–11 cm, inf).
  - Pitch "from a low B up to a high A chromatically" [WGH]. That is ambiguous: probably the struck ("sruti") tone, octave unstated.
  - Pitch is tuned down a quarter- to half-tone with plasticine or water [GHU].
  - The player modulates the bass by pressing the **mouth against the belly** [WGH].
- **Moringa (Brazil).** "Similar to the Nigerian udu, most probably inspired by that". 1.55 L, played on the body hole [AF21].

### 1.3 Position of the body hole
On traditional pots the hole is "midway on the side" [NMAA], roughly at the equator, about 90° from the neck axis. On the Utar the body hole is on the top of the cavity, 22.9 cm from the neck junction [AHG13]. For a lumped model the position does not matter (V ≫ neck volumes, λ ≫ 30 cm). It only matters for which shell modes the palm excites and for higher cavity modes.

---

## 2. Helmholtz bass

### 2.1 Measured frequencies

| Configuration (traditional naming) | [AHG13] Utar measured | [AHG13] circuit model | [VA22] udu measured | [VA22] lumped calc |
|---|---|---|---|---|
| Mouth only (body hole closed) | **73 Hz** (also 74 Hz for the other stroke) | 75.8 Hz | **68 Hz** | 68 Hz |
| Body hole only (mouth closed) | **113 Hz** | 113.8 Hz | **116 Hz** | 113 Hz |
| Both open | **137 Hz** | 141.2–141.3 Hz | **136 Hz** | 132 Hz |

- [AHG13] says the circuit model is accurate to within 3.8% (mean 2.9%). A preliminary simpler circuit was off by 9.3%.
- [AHG13]'s *naive* dual-Helmholtz estimate for the Utar was off by ~100%: it gave 93 / 243 / 268 Hz. The cause is the flared, segmented neck and the orifice impedances.
- [VA22]'s naive formula works to a few percent on a simple-necked udu.
- My lumped check of the Utar (calc): body hole r = 2.5 cm, t = 6 mm, 0.85a at both ends gives **114.2 Hz** (meas. 113). An effective mouth neck of L = 14.6 cm, r = 3.4 cm, 0.85a at both ends gives 75.8 Hz (meas. 73). Both together give **137.1 Hz** (meas. 137). The 3.4 cm effective radius is my fit (inf).

### 2.2 Two openings: conductances add
- Lossless lumped model: f = (1/2π)·√((1/M₁ + 1/M₂)/C), with C = V/ρc² and Mᵢ = ρL′ᵢ/Sᵢ. This is the same as the ocarina rule f ∝ √(ΣSᵢ/L′ᵢ / V).
- So **f_both² = f₁² + f₂²** ("already known to Helmholtz and Rayleigh") [VA22]. As a two-mass/one-spring analogy, f_both depends on the *reduced mass*. Releasing a clamped mass therefore always *raises* the pitch [VA22].
- Measured checks: [VA22] √(68² + 116²) = 134.5 vs 136 Hz (~1%). [AHG13] √(73² + 113²) = 134.5 vs 137 Hz (1.8%).
- The ratio a = M₁/M₂ sets the intervals: f₂/f₁ = √a and f₁₂/f₁ = √(a+1) [AHG13] (fig. 6):
  - a = 1.25 gives "a second and a fifth".
  - a = 1.78 gives "a fourth and a sixth".
  - The Utar sits near "a fifth and a seventh" (D2–A2–C#3).
- A multi-neck resonator has one non-zero lossless resonance, and leaks in thin walls raise f strongly because of their very low inertance [LHG19]. For the model, **an imperfect palm seal is a high-conductance leak.** A maker's note agrees: "cover the hole completely. Otherwise the sound will be short and die out quickly" [VALT].
- Interaction between neighbouring holes: in ocarinas a second opened hole gives a flatter interval than the first. That is just √ΣK. The holes on an udu are >20 cm apart, so mutual end-correction interaction is negligible (inf).

### 2.3 End corrections (per end, radius a)
- **Unflanged pipe end:** Levine–Schwinger **0.6133a** (low-ka limit) [EMSM26].
- **Flanged (baffled) end:** **0.8216a** (exact low-ka), commonly **0.85a**. [VA22] used L + 1.4R (≈0.6 + 0.8) for the neck and L + 1.7R (2 × 0.85) for the wall hole.
- **Thin circular orifice (t → 0):** Rayleigh conductance **K = 2a** [BS20], i.e. L′ = S/K = πa/2 = 1.571a in total (0.785a per side). For finite t, L′ ≈ t + ~1.6a. [BS20] shows the inviscid "Rayleigh decomposition" (t + two end corrections) is exponentially accurate for t/a ≳ 1. A log-type correction is needed when t/a ≪ 1, which applies to the udu's body hole (t/a ≈ 0.2–0.4).
- **Inner end into a cavity (Ingard 1953):** for an orifice of radius a feeding a cavity of radius R, the inner correction shrinks roughly as 0.85a(1 − 1.25 a/R) for a/R < 0.4. This is commonly quoted, but **I could not verify the exact form from a fetched source** (tagged inf). For udu a/R ≈ 0.2, giving −25%, about −0.5 cm on one side.
- **Curved wall:** the outer surface of a ~13 cm radius pot around a 2.5–3 cm hole is nearly flat (a/R ≈ 0.2). Use the flanged value outside. The curvature reduces the outer correction by a few percent at most (inf).
- **Empirical bias:** on 8 terracotta "acoustic pots" the classical formula overestimated f by **7% on average** (systematic), which [CDL02] attributes to an undervalued neck correction. Alster's formula gave <3% error. Enclosing a pot in a board lowered f by 1.3–2.8%.
- **Orifice resistance and mass used by [AHG13]** (Morse & Ingard; Pierce; Kinsler):
  - Orifice resistance R = ln(4r/t)·√(2ρωμ)/(4S); orifice mass M = ρr/4 (sic).
  - Flanged radiation resistance R = ρc(kr)²/(2S); flanged radiation mass M = 8ρr/(3πS).

### 2.4 Covering an opening fully
- Closing a hole sends its inertance to infinity, so its branch drops out [AHG13].
- Covering the **mouth** drops f from 137 to 113 Hz (−333 c). Covering the **body hole** drops it to 73 Hz (−1090 c) [AHG13]. The body hole carries the larger conductance, so covering it gives the big drop.
- The [VA22] instrument gives the same picture: −275 c and −1200 c.
- Playing note: "The side hole [neck end, Utar] is typically more responsive … with a much stronger response obtained" [AHG13].

### 2.5 Partial covering, shading, and a hovering hand (no published measurements; physics)
- [AHG13] describes the continuous control: "as the player raises one side of the palm on the top hole … the pitch increases to the tone nearly an octave higher … the player is dynamically changing the end correction". The spectrogram (fig. 3) shows a smooth 73 ↔ 137 Hz sweep over ~2 s cycles, struck every ~0.3 s.
- **Palm parallel to the hole at height h** (inf, radial-flow inertance between two plates): the gap adds M_gap = (ρ/2πh)·ln(b/a). In added length that is **ΔL = (a²/2h)·ln(b/a)**, valid for h ≲ a/2; it goes to 0 (the free end correction) as h ≳ a. With b ≈ 6 cm (palm), my numbers (calc) are:

| h | ΔL (a = 3 cm) | f(both open), [VA22] geometry |
|---|---|---|
| 1 mm | 31 cm | 83 Hz |
| 2 mm | 16 cm | 92 Hz |
| 5 mm | 6.2 cm | 106 Hz |
| 10 mm | 3.1 cm | 116 Hz |
| 20 mm | 1.6 cm | 123 Hz |
| ∞ | 0 | 134 Hz |

- The mapping is steep at small h: most of the pitch rise happens in the first ~5 mm of lift. **This explains the fast "bloop" glide.**
- **Tilted palm ("raising one side") / partial occlusion:** model it as a crescent-shaped aperture. For a thin aperture the Rayleigh conductance scales with *linear size*, not area. For a compact aperture K ≈ 2√(S_open/π), so K ∝ √(open fraction). A slit-like crescent keeps relatively more conductance (inf). In a model, use K(φ) = K_open·φ^0.5…0.7 plus a series gap inertance.
- **Finger shading / half-holing** in ocarinas works the same way; no cents-per-mm data found.

### 2.6 Q and damping of the Helmholtz mode
- **Measured:** moringa, mouth only (hand left on the body hole): **f = 138 Hz, Q = 57** [AF21] (decay fitted to a slap). Its peak input impedance is 6 × 10⁶ Pa·s·m⁻³ [AF21].
- **Loss budget** (calc; R_rad = ρck²/8π·|U_tot|² free-field monopole; neck viscous R = (ρ/S)√(2νω)(L/a + 2) after Ingard; cavity-wall thermal η = (γ−1)δ_t A/2V):

| Case | f (calc) | Q_rad (free / half-space) | Q_visc | Q_therm | **Q_total** | T60 |
|---|---|---|---|---|---|---|
| Moringa, neck only | 130 (meas. 138) | 598 / 299 | 79 | 458 | **60** (meas. **57**) | 1.0 s |
| [VA22] mouth only | 68.7 | 617 / 308 | 73 | 623 | **59** | 1.9 s |
| [VA22] body hole only | 114.5 | 133 / 67 | 129 | 805 | **61** | 1.2 s |
| [VA22] both open | 133.6 | 84 / 42 | 127 | 869 | **48** | 0.8 s |
| Utar both (my lumped fit) | 137.1 | 86 / 43 | 129 | 852 | **49** | 0.8 s |

- **Conclusions** (calc):
  - The deep mouth-only mode is **viscous-loss limited** (long narrow neck) and radiates weakly.
  - The body hole and the both-open mode are **radiation-limited**.
  - Thermal loss at the cavity walls is minor (Q ≈ 450–900).
  - On the lap or near the body, radiation is closer to the half-space value, so Q_both ≈ 30–40.
- **Nonlinear orifice loss at playing amplitude** (inf; Ingard–Ising-type jet resistance r ≈ ρu₀ per unit area): Q_nl ≈ (3π/8)·ωL′/u₀. For the [VA22] body hole:
  - Cavity pressure 50 Pa: u₀ = 0.8 m/s, Q_nl ≈ 80.
  - 200 Pa: u₀ = 3.1 m/s, Q_nl ≈ 20.
  - 1 kPa: Q_nl ≈ 4.
  - So a **hard slap decays fast at first, then rings out at Q ≈ 50**. **Make the bass damping amplitude-dependent.**
- **Figure-derived Q** (fig, lower bounds limited by spectral resolution):
  - [VA22] fig. 4: −3 dB widths ≈ 2.5 / 3 / 4 Hz for 68 / 116 / 135 Hz, giving Q ≳ 25 / 39 / 34.
  - [AHG13] fig. 2a: the "SCTO" peak (body-hole-only tone, striking hand left on the neck end) is broad (Q ≈ 6–7). The SOTO and TOSO peaks have a sharp tip on a broad base; the broad base is the transient and glide.
- The usual textbook rule Q = 2π√(V(L′/S)³) [KIN] is radiation-only. It gives 74–448 for handclap cavities [FU25]. For the udu body hole it is ~70–150, too high on its own.

---

## 3. Playing technique and its sound

### 3.1 Strokes (from [AHG13], [VA22], [AF21], [VALT], [BALT])
1. **Body-hole slap, hand held** ("mouth only"): deep 68–73 Hz. This is the "voice of the ancestors" bass [AHG13].
2. **Body-hole slap and quick release** (the signature "bloop / whoop / doong", "like a droplet falling on water" [VA22]): starts at the mouth-only pitch and **slides up ~1 octave** to the both-open pitch. "When the hand strikes a hole and immediately releases, the tone quickly slides up nearly an octave before dissipating" [AHG13].
3. **Mouth slap and release with the body hole open:** gives the both-open 136–137 Hz tone directly. A mouth slap and release with the body hole *closed* gives no audible mouth-only tone [AHG13].
4. **"Swoop":** cover one hole, strike the other with the palm fully covering it, and release immediately. It sounds "similar to a dripping faucet" [AHG13].
5. **"Wah" / dynamic end correction:** the other hand opens and closes the second opening while one hand keeps striking. The pitch moves continuously between 73 and 137 Hz [AHG13] (fig. 3). Partly covering the struck hole while striking gives "very low tones" [AHG13].
6. **Clay tones:** "tapping with the pads and inner joints of one's fingers", knuckles, nails. Conga and tabla technique transfers [AHG13].
   - Striking the **neck-end lip** gives a "cowbell" tone of 556 Hz plus near-harmonics.
   - Striking **near the body hole** gives a 970 Hz-dominated tone [AHG13].
7. **Water in the pot** raises the bass pitch (see §0) and "seems to improve the sound projection in some Udu" [AHG13].
8. **Support:** the lap "tends to dampen the vibration of the clay". Straw rings and foam-covered stands give "less damping and longer duration" [AHG13].

### 3.2 Glide numbers
- **Range:** +1090 c (Utar, 73 → 137 Hz) and +1200 c (udu, 68 → 136 Hz). For mouth closed → open, +275 to +333 c (calc).
- **Direction:** always **upward** on release, because adding conductance lowers the reduced mass [VA22]. It goes downward only when a hand closes over an opening.
- **Timescale** ([VA22] fig. 5, fig):
  - The low band (~60–95 Hz, centred ~75 Hz) is held from 0 to ~0.14 s.
  - The band then moves to ~140 Hz between **t ≈ 0.14 and 0.21 s (~70 ms)**.
  - The 140 Hz band is still strong at 0.4 s (end of plot).
  - The STFT bins look ~5 Hz wide, which implies a window ≥ 0.1–0.2 s, so the true transition is faster than shown.
- **Physical estimate** (inf): from the h-to-f table in §2.5, ~70% of the rise (83 → 116 Hz) happens as the palm lifts from h = 1 to 10 mm. At a hand lift-off speed of 0.3–1 m/s that takes **10–30 ms**. The last ~2 semitones follow more slowly as the hand moves beyond ~2 cm (tens of ms).
- **Model:** drive h(t) = h₀ + v·t (v ≈ 0.5 m/s) after a hold time of 0–150 ms, and map it through K(h).

### 3.3 Spectrogram and spectrum descriptions
- [AHG13] fig. 3: a smooth arch from ~70 to 135 Hz and back over ~6 s, with discrete strikes visible as vertical bands ~0.3 s apart.
- [AHG13] fig. 2: anechoic, 1 m, averaged over 3–4 strokes (fig):

| Stroke | Peak SPL |
|---|---|
| Both open (strike neck end or body hole) | ~62–63 dB |
| Body hole only | ~60 dB, broad |
| Mouth only, struck at the neck end | ~44 dB (narrow, weakest) |
| Mouth only, struck at the body hole | ~56 dB |

- The deep mode is **radiated 6–18 dB weaker** than the both-open tone. That is partly the f² radiation efficiency and partly the large neck inertance (calc).
- **Above the bass**, the moringa resonator "does not have resonances that are harmonically related to the main resonance … in the first few multiples" [AF21]. Any harmonics come from excitation nonlinearity and from shell modes.

### 3.4 Durations
- **Bass:** T60 ≈ 0.8–1.0 s (calc from Q 48–57). A tone that is still visible 0.2 s after release in [VA22] is consistent with this. For hard or hand-damped strokes use 0.3–0.5 s (inf).
- **Clay tones:** no published T60. Estimated at 0.2–0.4 s at 556 Hz and 0.07–0.15 s at 1.6 kHz on a stand, and much less on the lap (inf, §4.3).

---

## 4. Clay shell modes and material

### 4.1 Utar shell modes (scanning laser vibrometer, shaker at the neck-end lip; 3.2 kHz band) [AHG13]

| f (Hz) | Note | Where it is strongest | Comment |
|---|---|---|---|
| **556** | C#5 | Lip of the neck end ("cowbell") | Strongly excited at the lip antinode, weak at the body-hole positions |
| **970** | ~B5 | Near the body hole (strongest just "west" of it) | Has a partial at **1898 Hz** (×1.96) |
| **1108** | C#6 | Lip | ×1.99 of 556 |
| **1378** | F6 | — | |
| **1470** | F#6 | — | |
| **1644** | G#6 | Lip | ×2.96 of 556 |

- "Nearly harmonic … slightly flat relative to perfect harmonic ratios" [AHG13]. The designer got these ratios by ear, without a model.
- Mode shapes (fig. 8) are only given as images.

### 4.2 Comparison: plain clay pots (untuned)
- **Flowerpot "bells"**, 56 pots from 11.4 to 40.4 cm diameter [HIB]. A 15.2 cm pot has partials at **610.5, 1659.5 (×2.72), 2988 (×4.89), 3069, 3180.5, 4094.5, 4801, 5584.5, 6194, 8437.5 Hz**. These are **inharmonic**, and the strike pitch is the lowest partial.
- [HIB]'s regression is f = 20575·d^−2.296·t^0.8654 (d = diameter, t = rim thickness, "metres"). The stated units do not reproduce 610 Hz with a plausible rim, so use only the exponents: **f ∝ t·d^−2.3**, as for bells.
- A thin-ring n = 2 estimate, f = (t/2πR²)√(E/12ρ)·n(n²−1)/√(n²+1), with R = 7.6 cm, t = 8 mm, E = 10–15 GPa, gives **390–480 Hz** vs the 610 Hz measured (calc). That is the right order, and it supports E ~ 10–20 GPa.
- **Acoustic pot** (terracotta), measured: Helmholtz 297 Hz, next pot modes **1260, 1870, 2350 Hz** [CDL02].
- **Ghatam:** no published modal data. Pots are "tapped many thousands of times to even out thickness", because "the thickness of the pot must be even to produce a quality tone" [GHU].
- The udu shell modes sit at **~4–12 × f_Helmholtz**, so the two are well separated. Coupling through the cavity is weak except where a shell mode meets a cavity or neck mode (inf).

### 4.3 Material properties

| Property | Value | Notes | Tag | Source |
|---|---|---|---|---|
| Firing temperature of traditional open/bonfire firing | Peaks **675–900 °C**, reached in ~12–22 min; 94–295 °C differences within one firing | Cameroon ethnography and experiments | pub | [OPF] |
| E after 1100 °C firing | 15, 17, 30, 39, 68 GPa (five illitic clays) | Bulk density 1.84–2.27 g/cm³ | pub | [HUL20] |
| E during heating | ~8 GPa up to 800 °C | | pub | [HUL20] |
| E, porous body fired at 1100 °C | **9.3 GPa**, porosity ~30% | Dehydroxylation drop: 7.8 GPa (400 °C) → 4.3 GPa (700 °C) | pub | [JCS15] |
| **E to use for a udu** | **8–15 GPa** | Low-fired, porous, sand-tempered. LP/Giorgini commercial udus are kiln-fired and probably harder (E 15–30 GPa, inf). | inf | — |
| ρ | **1850–1950 kg/m³** | | pub/inf | [HUL20], [SPOT] |
| ν | 0.1–0.2 | Database value 0.1 | pub (weak) | [SPOT] |
| Loss factor η | **0.01–0.02** low-fired; 0.005–0.01 vitrified | See the bullets below | mixed | [DAV], [BAR25], [GG] |
| Maker's view | "high temperature durable ceramics get very hard and less resonant" | Anecdotal | pub (anecdote) | [VALT] |

- **Evidence behind the loss factor η:**
  - Davis (British Ceramic RA, IOA) lists internal friction values for "porous earthenware ~100–250, vitreous earthenware ~80–150, electrical porcelain ~30–100, bone china ~25". I saw these only in a search-index extract and could not open the paper. I read them as Q⁻¹ × 10⁴, i.e. η = 0.01–0.025 for porous earthenware, but **the units are unverified**.
  - Porcelain bells (FA 2025): release time 82 ms at 2.09 kHz and 135 ms at 2.79 kHz. Taking that release time as roughly T60 gives Q ≈ 78–171, η ≈ 0.006–0.013 (calc). That includes support and radiation loss.
  - Building brick in vibro-acoustic practice: η ≈ 0.01 [GG].
  - [AHG13] calls the clay walls of high stiffness, "thus a high quality factor".
- **Ring frequency:** f_r = c_L/2πR, with c_L = √(E/ρ(1−ν²)) ≈ 1640 / 2320 / 3280 m/s for E = 5 / 10 / 20 GPa. For R = 13 cm that gives f_r ≈ **2.0 / 2.8 / 4.0 kHz** (calc). The measured 0.5–1.6 kHz modes are below f_r, so they are bending/inextensional-type modes, stiffened by the closed, doubly curved form.
- **T60 of shell modes** (inf): T60 = 6.91/(πfη).

| f | η = 0.01 | η = 0.02 |
|---|---|---|
| 556 Hz | 0.40 s | 0.20 s |
| 970 Hz | 0.23 s | — |
| 1644 Hz | 0.13 s | 0.07 s |

  - On the lap, divide these by 2–5 (inf, from [AHG13]'s qualitative statement).
- **Water absorption:** unglazed terracotta is porous (the ghatam is tuned down by wetting it [GHU]). Wetting adds mass and lowers f, and probably raises η (inf).

### 4.4 Higher air modes inside the vessel
- **First non-Helmholtz cavity mode** (calc):
  - Rigid sphere, first (1,1) mode at ka = 2.0816. For 9.25 L, a = 13.0 cm, giving **873 Hz**. For 10.2 L, a = 13.5 cm, giving 845 Hz.
  - The elongated Utar (30 cm internal) has a longitudinal mode near c/2L ≈ **570 Hz**.
  - Measured pot analogue: 297 → 1260 Hz [CDL02].
  - These modes have a pressure node at the cavity centre and only couple to the openings to the extent that the holes are off-node. They can colour the slap transient and the clay tones in the 0.5–1 kHz range (inf).
- **Neck pipe modes** (calc):
  - Utar neck (14.6 cm plus ~2 × 0.6a corrections ≈ 19–20 cm, open into the large cavity at one end and to air at the other) behaves like an open–open pipe, giving **~850–900 Hz**.
  - The traditional 6.3 cm neck gives ~1.8 kHz.
- **Moringa:** "does not have resonances that are harmonically related to the main resonance" [AF21].

---

## 5. Radiation
- **Helmholtz mode:** both openings are driven by the same cavity pressure, so Uᵢ = p/(jωMᵢ). They are **in phase** (both outward on compression), forming two monopoles, and the ratio is U_hole/U_mouth = M_mouth/M_hole.
  - [VA22] geometry: **2.77, i.e. +8.9 dB** for the body hole (calc).
  - Utar (my fit): 0.69 : 0.31 = 2.3, i.e. +7 dB.
  - Spacing d ≈ 0.25–0.3 m and k = 2.5 m⁻¹ give kd ≈ 0.6–0.75. With unequal in-phase sources the far-field ripple is **< 0.5 dB**, so the source is omnidirectional (calc).
  - ka of each opening ≈ 0.06–0.08, so the radiation resistance is ∝ k²S²: **flanged ρc k²/2π**, **unflanged ρc k²/4π** [KIN], [AHG13].
  - A terracotta pot at resonance radiates omnidirectionally ("an omnidirectional secondary source placed on a wall"), with a useful effect only within ~30 cm when passively excited [CDL02].
- **Absolute level estimate** (calc, free-field monopole p = ρωU_tot/4πr). For the [VA22] geometry at 136 Hz:

| Cavity pressure amplitude | U_tot | SPL at 1 m |
|---|---|---|
| 50 Pa | 3.0 L/s | 79 dB rms |
| 200 Pa | 11.9 L/s | 91 dB rms |
| 1 kPa | 60 L/s | 105 dB |

- **Shell modes (0.5–2 kHz):** kR ≈ 1.2–5, so they radiate as directional multipoles. Strike location selects the modes (antinode vs node) [AHG13]. A breathing component also drives the cavity and leaks out of the holes (inf).
- The player's body, lap and hands sit within λ/4 of the openings. This raises the radiation mass slightly (lowers f by a few %) and changes the low-frequency directivity (inf; compare [CDL02], where enclosure lowered f by 1.3–2.8%).

---

## 6. Palm and finger contact, and the pressure pulse
- **No published hand-contact durations exist for hand drums** (conga, djembe, cajón, udu). The nearest data:
  - **Drumstick–head contact 3–6 ms** (KTH thesis, snare) [WAG06].
  - **5–8 ms** for a mf stroke at the centre of a tom or snare (review) [DAHL].
  - A brief, high-amplitude force pulse "can be achieved with the bare hands" [DAHL].
- **Handclap (closest analogue for a cupped-palm air pulse)** [FU25], high-speed video plus cavity pressure sensor:
  - The collision compresses the hand cavity for ~10 ms (t = −9.5 to 0 ms) before the jet, with no sound until the jet forms.
  - The sound then **decays within ~10 ms**.
  - **Cavity gauge pressure 0.1–5 kPa**, scaling ≈ u_clap² (fitted exponent 1.6–2), for u_clap = 0–4 m/s. It is ~100× the radiated pressure.
  - Soft-tissue deformation of the outlet lowers Q a lot. Faster claps give lower Q.
  - The model's palm soft-tissue thickness is 6 cm.
  - Finger-groove pipe mode: 1320 ± 113 Hz for L = 6.44 ± 0.55 cm.
- **Hand-struck pipe:** keeping "the palm firmly seated against the rim throughout the impact" is needed for a clean closed-end resonance. Their 1.5 m pipe transient lasts ~0.4 s [EMSM26].
- **Udu slap pulse model** (calc/inf):
  - The palm pad intrudes x into a hole of area S (r = 3 cm), giving ΔV = S·x and Δp = ρc²ΔV/V.
  - For V = 10.2 L: x = 1 / 3 / 5 mm gives **39 / 117 / 196 Pa**.
  - Pulse shape: raised-cosine or half-sine, **4–10 ms** long (inf). Choose 3–5 ms for a crisp "pop" and up to 10 ms for a soft palm.
  - Spectrum: a 5 ms half-sine has its first null near 300 Hz. It excites the 70–140 Hz bass efficiently, but the shell modes and the 0.6–0.9 kHz cavity modes only weakly. This matches the bass slap having little "clay" (inf).
- **Fingertip and knuckle strokes on the clay** (inf): contact ~1–3 ms. This is shorter because the finger's effective mass is ~0.01–0.05 kg on a stiff contact, and it reaches the 0.5–2 kHz shell modes. Striking the lip vs near the body hole selects the 556 Hz vs 970 Hz families [AHG13].
- **Effective masses** (inf): whole hand ≈ 0.6% of body mass ≈ 0.4–0.5 kg (standard anthropometric tables, not fetched). Effective palm pad stiffness k ≈ π²m/T² ≈ 1.6 × 10⁵ N/m for m = 0.4 kg and T = 5 ms. The air spring of the cavity seen through the hole, ρc²S²/V ≈ 200 N/m, is negligible in comparison, so the hand's motion is not slowed by the air.
- **Hold and release:** in [VA22] fig. 5 the hand stays on the hole ~0.14 s. The releasing palm then acts as the time-varying gap inertance of §2.5.

---

## 7. Suggested lumped model (summary, inf)
1. **Cavity:** C = V/ρc², with V = 10 L (8–12 L range).
2. **Two branches (mouth M₁, body hole M₂)**, each with:
   - M = ρL′/S + ρ·ln(b/a)/(2πh(t)) (palm gap);
   - R = R_rad(k) + R_visc(ω) + R_nl(|u|);
   - a hard closure when h → 0.
   - Defaults: M₁ = ρ·0.094/(π·0.022²), M₂ = ρ·0.063/(π·0.030²). These give 68 / 115 / 134 Hz.
3. **Exciter:** a volume-displacement source on the struck branch, ΔV(t) = S·x(t), with x as a half-sine of 4–10 ms and 1–5 mm peak (equivalently a cavity pressure pulse of 40–200 Pa). Then hold for 0–150 ms, then lift the palm at ~0.5 m/s.
4. **Output:** the sum of branch volume velocities, differentiated (monopole far field). Optionally pan the mouth and hole separately (hole ≈ +9 dB).
5. **Shell:** a modal bank for the Utar: 556, 970, 1108, 1378, 1470, 1644, 1898 Hz with η ≈ 0.01–0.02, plus a weak breathing coupling into the cavity pressure. Excite it by finger strikes at 1–3 ms contact, and weakly by the palm slap.
6. **Extra damping:** η_shell ×2–5 and Q_bass ×0.6–0.8 when the udu is on the lap.

---

## 8. What is inferred or missing
- Ghatam: no acoustic measurements found at all, so the geometry and Helmholtz estimate are inf.
  - My Helmholtz estimate: mouth d = 7–11 cm, V = 8–11 L, giving **120–190 Hz**, lowered toward ~60–100 Hz when the mouth is pressed toward the belly.
- Loss factor of low-fired terracotta: no clean measured value. The Davis table's units are unverified.
- Palm contact time and slap pressure in an udu: unmeasured. The values above come from clap and stick studies plus simple physics.
- Exact Ingard inner-end correction formula: quoted from memory, not verified.
- Partial-hole (shading) conductance vs open fraction: no data. The √φ law is my approximation.
- Udu Inc. product specs (volumes and pitches per model): the official site is gone. Only LP retailer heights and diameters survive.

---

## Sources

1. **[AHG13]** B. E. Anderson, C. B. Hilton, F. Giorgini, "Equivalent circuit modeling and vibrometry measurements of the Nigerian-origin Udu Utar drum", J. Acoust. Soc. Am. 133(3), 1718–1726 (2013), doi:10.1121/1.4789892. PDF: https://physics.byu.edu/docs/publication/1551
2. **[VA22]** L. P. Vieira, C. E. Aguiar, "The sound of the udu", Physics Education 57, 045028 (2022), doi:10.1088/1361-6552/ac69a1. https://arxiv.org/abs/2204.11610 (figures from the arXiv source)
3. **[AF21]** A. Almeida, L. Fuks, "Redesigning the Helmholtz Resonator with Reeds and the Vocal Folds", DAGA 2021 Wien, pp. 459–462. https://pub.dega-akustik.de/DAGA_2021/data/articles/000569.pdf
4. **[CDL02]** A. P. O. Carvalho, V. Desarnaulds, Y. Loerincik, "Acoustic behavior of ceramic pots used in Middle Age worship spaces – a laboratory analysis", ICSV9 (2002). https://paginas.fe.up.pt/~carvalho/icsv9.pdf
5. **[LHG19]** F. Langfeldt, H. Hoppen, W. Gleine, "Resonance frequencies and sound absorption of Helmholtz resonators with multiple necks", Applied Acoustics 145, 314–319 (2019), doi:10.1016/j.apacoust.2018.10.021 (abstract via https://www.bohrium.com/paper-details/resonance-frequencies-and-sound-absorption-of-helmholtz-resonators-with-multiple-necks/812832977946935299-526)
6. **[BS20]** R. Brandão, O. Schnitzer, "Acoustic impedance of a cylindrical orifice", J. Fluid Mech. (2020), doi:10.1017/jfm.2020.187. https://arxiv.org/abs/2001.04904
7. **[EMSM26]** F. Echegorri, M. Monteiro, C. Stari, A. C. Martí, "Multiple harmonics from a hand-struck tube: a simple and precise measurement of the speed of sound", arXiv:2609.31131 (2026). Uses the Levine–Schwinger 0.6133a correction. https://arxiv.org/html/2609.31131v1
8. **[FU25]** Y. Fu, A. Kiyama, G. Liu, L. Zhang, S. Jung, "Revealing the sound, flow excitation, and collision dynamics of human handclaps", Phys. Rev. Research 7, 013259 (2025). https://journals.aps.org/prresearch/pdf/10.1103/PhysRevResearch.7.013259
9. **[HUL20]** T. Húlan et al., "Young's Modulus of Different Illitic Clays during Heating and Cooling Stage of Firing", Materials 13(21), 4968 (2020). https://pmc.ncbi.nlm.nih.gov/articles/PMC7663826/
10. **[JCS15]** J. Ceram. Soc. Japan 123(1441), JCSJ-P15027 (2015): Young's modulus of a clay body during firing; 9.3 GPa after 1100 °C at ~30% porosity (via search-index abstract). https://www.jstage.jst.go.jp/article/jcersj2/123/1441/123_JCSJ-P15027/_article
11. **[HIB]** W. A. Hibbert, "Ceramic bells" (flowerpot acoustics, 56 pots), The Sound of Bells (2025). https://www.hibberts.co.uk/ceramic-bells/
12. **[BAR25]** A. M. Barbancho, I. Barbancho, L. J. Tardón, C. Traginer, "What do bells made from lunar regoliths sound like?", Forum Acusticum 2025, doi:10.61782/fa.2025.0969. https://dael.euracoustics.org/confs/fa2025/data/articles/000969.pdf
13. **[DAV]** W. R. Davis, "Internal friction in ceramics", Institute of Acoustics proceedings. Seen only as a search-index extract; the file is now 404. https://www.ioa.org.uk/system/files/publications/WR%20DAVIS%20INTERNAL%20FRICTION%20IN%20CERAMICS.pdf
14. **[GG]** C. L. S. Gilford, B. M. Gibbs, "The role of internal damping of materials in scale-model measurements of sound transmission", IOA. η ≈ 0.01 typical for building materials (via search-index extract). https://www.ioa.org.uk/system/files/publications/CLS%20GILFORD%20BM%20GIBBS%20THE%20ROLE%20OF%20INTERNAL%20DAMPING%20OF%20MATERIALS%20IN%20SCALE-MODEL%20MEASUREMENTS%20OF%20SOUND%20TRANSMISSION.pdf
15. **[SPOT]** SpotMaterial, "Earthenware (terracotta)" database entry (ρ 1600–2100 kg/m³, E 15–25 GPa, ν 0.1; provenance unclear). https://spotmaterial.com/material/ceramic/earthenware-terracotta/
16. **[OPF]** O. Gosselain (1992) open-firing ethnography and related experiments (peaks 675–900 °C; 94–295 °C spread within one firing), summarised at https://www.academia.edu/7407048 and https://epub.ub.uni-muenchen.de/93702/1/Budka_D%27Ercole_exarc.net_2022.pdf
17. **[WAG06]** A. Wagner, "Analysis of drumbeats – interaction between drummer, drumstick and instrument", MSc thesis, KTH (2006). https://www.speech.kth.se/publications/masterprojects/2006/AndreasWagner.pdf
18. **[DAHL]** S. Dahl, review of striking movements and contact (5–8 ms stick contact). https://vbn.aau.dk/ws/files/65339564/Dahl_J_AST_review_submi.pdf ; see also S. Dahl & E. Altenmüller, Acoustics'08 Paris summary, https://acoustics.org/pressroom/httpdocs/155th/dahl.htm
19. **[NMAA]** Smithsonian National Museum of African Art, Igbo pot with side hole, 38 × 29 cm (record via search index). https://music.si.edu/node/789
20. **[SPEN]** Spencer Museum of Art (Univ. Kansas), "udu (pot-drum)", 34.5 × 28 × 29.5 cm. https://spencerart.ku.edu/art/collections-online/object/41910
21. **[MET]** The Metropolitan Museum of Art: Giorgini udu, obj. 503261 (30.6 cm long, 17.4 / 10.8 cm end diameters), https://www.metmuseum.org/art/collection/search/503261 ; kimkim, obj. 503370, https://www.metmuseum.org/art/collection/search/503370 (both via search-index extract; the site rate-limited direct fetch)
22. **[BALT]** Baltimore Recorders, "Udu" (traditional udu and kimkim dimensions, names). https://www.baltimorerecorders.org/html/instruments/udu_sem.html
23. **[THO]** Thomann, LP 1400-C2 Udu Claytone #2 page (also lists C3/C4 sizes). https://www.thomann.co.uk/lp_1400_c2_udu_drum_claytone_2.htm
24. **[ART]** Art Drum, "LP Udu Mbwata". https://www.artdrum.com/UDU_MBWATA.HTM
25. **[VALT]** V. Kinbom (Valter Percussion), ALUDU info sheet (maker's playing and material notes). https://cdn.abicart.com/shop/29272/art67/170659667-c6f6f3-ALUDU_INFO_SHEET_A4.pdf
26. **[GHU]** Ghatam Udupa, "Ghatam" (making, size, weight, tuning). https://www.ghatamudupa.com/ghatam
27. **[WGH]** Wikipedia, "Ghatam". https://en.wikipedia.org/wiki/Ghatam
28. **[GHA-S]** Manamadurai maker reports (8 kg fired, thickness): https://ruralindiaonline.org/articles/when-meenakshi-beats-a-pot-3000-times ; seller spec (13″ H, "32.5″ largest diameter", 4.3 kg): https://www.bajaao.com/products/ultimate-guru-concert-grade-manamadurai-ghatam-instructional-dvd-set
29. **[KIN]** L. E. Kinsler, A. R. Frey, A. B. Coppens, J. V. Sanders, *Fundamentals of Acoustics*, 4th ed., §10.8–10.9 (Helmholtz resonator, radiation impedance, Q = 2π√(V(L′/S)³)). Textbook; cited by [AHG13] and [VA22].
30. **[ING53]** U. Ingard, "On the theory and design of acoustic resonators", J. Acoust. Soc. Am. 25(6), 1037–1061 (1953). Cited for the orifice end and resistance corrections; I did not fetch the original.
