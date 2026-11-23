# Cuíca acoustics: hard numbers for a physical synthesis model

Compiled 2026-11-23 from the published literature, makers' specs and an
analysis of freely licensed recordings. `docs/cuica.md` says which of these
numbers each constant of the model (`src/cuica.h`) answers to.

How each value is marked:

- **[M]** measured and published (paper or thesis).
- **[S]** maker or retailer spec.
- **[Q]** qualitative or practitioner statement, with no measurement behind it.
- **[E]** an estimate or derivation from the physics, with its inputs shown.
- **[A]** an analysis of freely licensed recordings (rough; see §5.2).

**The literature is thin.** There are only four directly relevant technical
items: Wheeler 2002 (a JASA abstract with no numbers), Statsenko,
Chatziioannou & Kausel 2015 (ESPI/FEM of a 15 cm Meinl cuíca, the only real
modal data), Onofrei, Willemsen & Serafin 2021 (a DAFx finite-difference
friction drum, its parameters tuned by ear) and Batista 2016 (a teaching
thesis, low quality). Nothing published gives the cuíca's pitch range in Hz,
stroke speeds, grip force, sound level or wet-cloth friction. The pitch and
spectrum figures below come from pitch-tracking three CC-licensed clips
(§5.2): indicative, not authoritative.

---

## 0. Numbers for the model (summary table)

| Quantity | Value to use | Range / notes | Tag | Source |
|---|---|---|---|---|
| Head diameter | **9"** (a = 114 mm) | 6"–10"; 9" standard, 9–10" in the big samba schools | S | S9 |
| Shell length | **25 cm**, open far end | 15–31.5 cm across makers; 8"×23, 8"×26.7, 8–10"×30 | S | S9 |
| Shell quarter wave | **≈ 250 Hz** for 9"×25 cm | 227–436 Hz across sizes; lands on the open tone | E | §1.2, S12 |
| Head | goatskin **0.3 mm, 1260 kg/m³ → σ ≈ 0.38 kg/m²** | 0.25–0.45 kg/m²; tabla 0.2–0.3 | M/E | S1, S7 |
| Head tension | **≈ 4500–6000 N/m** | FEM fit 4470 N/m; bodhrán 470–1360 N/m (looser) | M (fitted) | S1, S8 |
| Skin in-plane stiffness | **E ≈ 330 MPa damp** (680 dry) → E·h ≈ 1e5 N/m | | M | S8 |
| Stick | bamboo **≈ 23–25 cm**, 4–8 mm, **3–8 g** with the knot | shell length less 1" (Remo) | S/E | S9, S10 |
| Bamboo | ρ 600–800 kg/m³, E 5–25 GPa → c ≈ 4.6–6 km/s | first compression mode 5–10 kHz: a rigid mass at the pitch | M/E | §3 |
| Stick mass / skin mass | **0.2–0.8** | lowers the fundamental 4–20 semitones: what sets the pitch | E | §3 |
| Open tone | **≈ 140–300 Hz** | Pearl clip 236–297 Hz; others 133–155 Hz and 105–117 Hz | A | §5.2 |
| Glide within a stroke | **+2 to +3.6 semitones in 90–165 ms** (open), +1–2 st (low) | | A | §5.2 |
| Pressed tones | **460–540 Hz**, peaks to **643 Hz** | span ≈ 1.4 octaves; players: "an octave or more", "up to two" | A/Q | §5.2, S5 |
| Pressing | nearer the stick and harder is higher; untouched is open | | Q | §4 |
| Spectrum, open | H2 −4…−23, H3 −16…−36, H4 −27…−41 dB; little above 1.5 kHz | | A | §5.2 |
| Spectrum, low "growl" | H1–H3 within 4 dB, energy to 3–3.7 kHz, fixed peaks near **1.15–1.2 and 1.45–1.6 kHz** | | A | §5.2 |
| Spectrum, pressed | 500 Hz: H2 −14, H3 −23…−30; 642 Hz nearly sinusoidal | | A | §5.2 |
| Onsets | 10–90 % rise **10–45 ms** (≤15 fastest; 70–100 ms swelling) | | A | §5.2 |
| Strokes | push/pull pairs ≈ 50 ms apart, ≈ 4 pairs/s | | A | §4 |
| Drive | sawtooth-like stick–slip, as a bowed string | | M (qual.) | S2 |
| Friction | μs 0.8, μd 0.3, Stribeck 0.1 m/s; N 0–20 N | tuned by ear; wet fabric 1.5–7× grippier than dry | S/E | S3, §6 |
| Cloth speed, grip | ≈ 0.2–1 m/s, 2–15 N | | E | §4 |

---

## Key sources

| # | Citation | URL | What it gives |
|---|---|---|---|
| S1 | T. Statsenko, V. Chatziioannou, W. Kausel (2015), "Interferometric studies of the Brazilian cuíca", Proc. Third Vienna Talk on Music Acoustics, pp. 153–157 | https://viennatalk2015.mdw.ac.at/proceedings/ViennaTalk2015_submission_29.pdf | Membrane material, thickness, FEM parameters, ODS frequencies |
| S2 | P. A. Wheeler (2002), "An acoustic study of the Brazilian cuica", J. Acoust. Soc. Am. 112(5), 2266 (abstract), doi:10.1121/1.4779049 | https://pubs.aip.org/asa/jasa/article/112/5_Supplement/2266/548495 | Sawtooth excitation; body contributes little (gourd cuíca) |
| S3 | M. G. Onofrei, S. Willemsen, S. Serafin (2021), "Real-time implementation of a friction drum inspired instrument using finite difference schemes", Proc. DAFx20in21, pp. 168–175 | https://dafx.de/paper-archive/2021/proceedings/papers/DAFx20in21_paper_32.pdf | Bowed-membrane + tube FDTD model and parameter table |
| S4 | M. G. Onofrei (2021), Aalborg Univ. MSc thesis "Real-time implementation of bowed instruments using static and dynamic friction models" | https://projekter.aau.dk/projekter/files/410480603/Real_Time_Implementation_Of_Bowed_Instruments.pdf | Same friction parameters |
| S5 | L. G. Clarindo Nunes, M. M. Wanderley, F. Lopes (2026), "Hypercuíca: augmenting and performing with an Afro-Brazilian friction drum", NIME 2026 | https://nime.org/proceedings/2026/nime2026_81.pdf | Construction and technique; "octave or more" range; TAKTO 8"×30 cm spec |
| S6 | J. L. de Paula Batista (2016), "Uma proposta de ensino de acústica a partir da análise dos timbres de instrumentos musicais do samba", MSc (prof.), UFU | https://repositorio.ufu.br/bitstream/123456789/18882/1/PropostaEnsinoAcustica.pdf | Tuner reading of the open low note; crude harmonic fit |
| S7 | T. D. Rossing (1992), "Acoustics of drums", Physics Today 45(3), 40–47 | https://aip.brightspotcdn.com/PTO.v45.i3.40_1.online.pdf | Air loading, timpani ratios, tabla areal density, amplitude-dependent tension |
| S8 | G.-Y. Li, A. L. Gower, M. Destrade, S.-H. Yun (2022), "Non-destructive mapping of stress, strain and stiffness of thin elastically deformed materials", arXiv:2201.01847 | https://arxiv.org/abs/2201.01847 | In-situ goat-skin (bodhrán) thickness, stress and E, dry vs damp |
| S9 | Kalango (German samba retailer), cuíca product guide and product pages (2025–26) | https://www.kalango.com/en/product-guide/cuica | Maker specs for Gope, Izzo, Contemporânea, King and Timbra; stick and head notes |
| S10 | Remo, part HK-5910-CU "Cuica friction rod, 10"" | https://remo.com/partnumber/hk-5910-cu | Synthetic-head stick system and trimming rule |
| S11 | S. BenZvi (2016), PHY 103 lecture "Drums", Univ. Rochester (cites B. Hopkin, *Musical Instrument Design*, ch. 7) | https://www.pas.rochester.edu/~sybenzvi/courses/phy103/2016f/phy103_drums.pdf | Ideal-membrane ratios; coffee-can cuíca recipe |
| S12 | Levine & Schwinger (1948), "On the radiation of sound from an unflanged circular pipe", Phys. Rev. 73, 383 (via Wikipedia "End correction" and arXiv:0811.3625) | https://en.wikipedia.org/wiki/End_correction | End correction 0.6133a unflanged, about 0.82–0.85a flanged |

---

## 1. Body (shell)

### 1.1 Maker and retailer dimensions

Nominal size is the head diameter in inches. Length is the shell height unless noted.

| Maker / model | Head Ø | Shell length | Material | Mass | Source / tag |
|---|---|---|---|---|---|
| Contemporânea Light | 6" | 15 cm | brushed aluminium | – | Thomann, Kalango [S] |
| Gope | 6" | 17 cm | aluminium, welded seam | – | Kalango A371726 [S] |
| Izzo "pocket" | 6" | – | stainless (inox) | – | Kalango [S] |
| Meinl (S1 test object) | 15 cm (≈6") | – | aluminium, ribbed tube | – | S1 [M, diameter only] |
| Contemporânea C-CU01 | 8" | 23 cm (25 cm total height incl. hoop) | polished thick-walled inox | 2.0 kg net | Kalango, Thomann [S] |
| King "vazada" (open-sided) | 8" | 25 cm | aluminium with cut-out shell | – | Kalango [S] |
| LP LP3408 | 8" | 10.5" (26.7 cm) | brass | – | lpmusic.com product text via search [S] |
| Izzo A321108 | 8" | 30 cm | inox | 1.98 kg | Kalango [S] |
| Timbra | 8" | 30 cm (31 cm total) | inox | 2.2 kg | Kalango [S] |
| TAKTO | 8 cm(sic) × 30 cm | 30 cm | stainless steel, "open body" | – | S5 [S]; "8 cm" is presumably 8" |
| Stay Music 6342ST/8223ST | 8" | 30 cm | stainless | 3.0 kg packed | bax-shop [S] |
| Gope | 9" | (≈30 cm) | welded aluminium | – | Kalango [S] |
| King | 9.5" | 30 cm | aluminium (vazada) or inox | – | Kalango [S] |
| Contemporânea C-CU02 | 10" | 28 cm total height | polished inox, 8 lugs | 2.3 kg | Kalango [S] |
| Gope A371710 | 10" | "30 cm" nominal, 31.5 cm real overall height | brushed inox, 10 zinc-steel rods 26.5 cm long | 3.0 kg | Kalango [S] |
| Izzo | 10" | 30 cm | inox | – | Kalango, Chamberlain [S] |
| Remo (synthetic) | 8" | 5" (12.7 cm) | acousticon/plastic | – | gear4music listing [S] |

Notes:

- Size range is 6" to 10", with 9" called the standard. Big Rio and São Paulo baterias use 9–10". I found no evidence of an 11" production cuíca. The Kalango guide says "In den großen Sambaschulen … werden 9"–10" Cuicas gespielt" [S/Q, S9].
- Per Kalango, a larger head is louder and can be tuned lower, so it has more usable notes. 6" is too quiet for a bateria. Shell material matters little; head thickness, stick thickness and technique matter far more [Q, S9].
- Kalango's 8" Izzo note says the 8" has "the same length as the larger models and is therefore very similarly loud" [Q]. This hints that the tube length, not the head diameter, sets loudness.
- Folk cuícas use a gourd, wood or fibreglass body [Q, S1]. Wheeler found the gourd body "contributes little to the sound" [M-qualitative, S2].
- Open end: the far end is open, usually plain cut, sometimes with a rolled edge. "Vazada" models also have large windows cut in the shell; King says this "eliminates the resonant chamber" for a "brighter, more projected" sound [S/Q].
- Wall thickness was not published for any maker. Inox shells are probably about 0.6–1.0 mm (thick-walled per Contemporânea), aluminium about 1–1.5 mm [E, unverified].

### 1.2 Derived air-column numbers [E]

Model the shell as a tube closed at the head end (the membrane is much stiffer and heavier than air) and open at the far end. Unflanged end correction Δ = 0.6133a (Levine & Schwinger 1948, S12). Odd quarter-wave resonances f_n = (2n−1)·c / 4(L+Δ), with c = 343 m/s. a = nominal radius, so the true internal radius is slightly smaller.

| Size | a (mm) | L_eff (cm) | f1 | f3 | f5 | 1st duct cross-mode (m=1) | 1st axisymmetric cross-mode |
|---|---|---|---|---|---|---|---|
| 6"×15 | 76 | 19.7 | 436 | 1308 | 2179 | 1319 | 2745 |
| 6"×17 | 76 | 21.7 | 396 | 1187 | 1978 | 1319 | 2745 |
| 8"×23 | 102 | 29.2 | 293 | 880 | 1467 | 989 | 2059 |
| 8"×26.7 (LP) | 102 | 32.9 | 261 | 782 | 1303 | 989 | 2059 |
| 8"×30 | 102 | 36.2 | 237 | 710 | 1183 | 989 | 2059 |
| 9"×30 | 114 | 37.0 | 232 | 695 | 1158 | 879 | 1830 |
| 10"×28 | 127 | 35.8 | 240 | 719 | 1198 | 791 | 1647 |
| 10"×30 | 127 | 37.8 | 227 | 681 | 1135 | 791 | 1647 |

Implications:

- The 1D tube model holds for axisymmetric excitation up to the axisymmetric cutoff (1.6–2.7 kHz). The centre stick drives mostly axisymmetric motion. Off-centre attachment, which S1 observed, also couples the m=1 duct mode (0.8–1.3 kHz).
- The first quarter-wave resonance (≈230–300 Hz for 8–10" × 23–30 cm) falls right on the measured open-tone pitch range (§5). That makes membrane-tube coupling or mode splitting plausible, as in kettledrums.
- Wheeler's "body contributes little" result (gourd) and the vazada design argue the coupling is weak. This should be tested in the model, for example by comparing with and without the tube.

The DAFx model (S3) used a 1D tube with L = 0.4 m, A = 0.0707 m² (a = 0.15 m), and a *detuned* wave speed of 30 m/s instead of 343 m/s "to produce the desired drum sound". Its radiation boundary is ∂ζ/∂x = −α1 ∂ζ/∂t − α2 ζ with α1 = 0.008 s/m and α2 = 4.348 1/m. So their tube is a tuned-by-ear filter, not a physical resonator [S3, tuned].

### 1.3 Radiation of a single-headed open drum

Rossing (S7) classes tom-toms and congas as "a single membrane open to the air on both sides" [M-qualitative]. For the cuíca, the outer face of the head and the open tube mouth both radiate. At low frequency they are roughly in antiphase (a dipole-like cancellation, like a frame drum with a tube between the faces). Near and above the quarter-wave resonance the mouth dominates [E, standard physics].

BenZvi's slides (S11) contrast "tube drums", where a "tube has strong, narrow air resonance peak; drumhead resonance is likely far above the tube resonance", with frame drums: "no enclosure … front and back of drumhead out of phase, causing cancellation of low-frequency overtones" [Q, teaching level].

---

## 2. Head (membrane)

### 2.1 Material

- **Natural:** goat skin is standard. Brazilian makers mount it on aluminium or wood rings (e.g. "Jorginho goat skin – wood rim", Gope "skin in an aluminium ring") [S, S9].
  - The Meinl 15 cm cuíca has a goat-skin head, "approximately 0.3 mm thick" [M, S1].
  - LP sells an LP3408 8" goatskin replacement head [S].
  - Calf skin is used on some drums generally. I found no cuíca-specific calf data.
- **Synthetic:** Kalango says plastic heads have the stick "fixed with a screw thread" and are easy to change. "Volume equals that of a thick natural head", but "can never really replace natural skin" [Q, S9].
  - Remo's system is a 10" friction rod plus hex nut, washer and stick. The stick is trimmed to 1" shorter than the shell length [S, S10].

### 2.2 Mechanical numbers

| Quantity | Value | Source / tag |
|---|---|---|
| Goat-skin thickness, cuíca | ≈0.3 mm | S1 [M] |
| Goat-skin thickness, bodhrán (18") | 360 ± 30 µm | S8 [M] |
| Goat-skin E, dry (in situ, small strain) | ≈680 MPa | S8 [M] |
| Goat-skin E, moisturised | ≈330 MPa | S8 [M] |
| In-plane stress, bodhrán dry → damp | 3.79 MPa → 1.31 MPa (radial stretch 0.28% → 0.20%) | S8 [M] |
| ⇒ tension per unit length, bodhrán | T = σh ≈ 1360 N/m dry, ≈470 N/m damp | [E from S8] |
| Bodhrán fundamental, dry → hydrated | 84 Hz → 36 Hz | S8 [M] |
| FEM-fitted cuíca membrane (Meinl 15 cm) | pre-tension 4470 N/m, E = 50 MPa, ρ = 1260 kg/m³, ν = 0.3 | S1 [fitted to ESPI, not directly measured; E=50 MPa is far below S8's 330–680 MPa, so only T, ρ, h matter] |
| ⇒ wave speed and ideal f01 for that FEM head | c = √(T/ρh) = 109 m/s; f01 = 2.405c/2πa = 555 Hz (a = 75 mm, no stick, no air) | [E] |
| Unloaded tabla membrane areal density | 0.02–0.03 g/cm² (0.2–0.3 kg/m²) | S7 [M] |
| Cuíca goat-skin areal density | 0.3 mm × 1260 kg/m³ ≈ 0.38 kg/m², plausible range 0.25–0.45 kg/m² | [E] |
| Mylar (PET) density | 1.39–1.40 g/cm³ | DuPont Teijin / TAP Plastics datasheets [S] |
| Mylar tensile modulus | ≈3.4–5.1 GPa (MD/TD, grade-dependent; Type A 50–125 µm: 3.6–4.1 GPa) | Mylar datasheets via search [S] |
| Mylar drumhead in a patent example | 0.241 mm, 1400 kg/m³, E 3.46 GPa, ν 0.3 | US patent via search [S] |
| DAFx friction-drum membrane | ρ = 1400 kg/m³, H = 7 mm (sic; unphysical, effectively a tuning knob), c = 15–150 m/s, σ0 = 0–6 s⁻¹, σ1 = 0–0.0026 m²/s | S3 [tuned] |

Cross-check that the FEM head is effectively ideal [E]. Ideal ratios × 555 Hz give (1,1) 885, (0,2) 1274, (4,1) 1751 and (2,2) 1943 Hz. S1's simulated shapes were (1,1)-like at 883 Hz, (0,2) at 1360 Hz, (4,1) at 1755 Hz and (2,2) at 1975 Hz. So the FEM is close to an ideal membrane with f01 ≈ 555 Hz. ESPI on the real drum showed (1,1)-like ODS at 820 Hz and 872 Hz. Since the stick sits on the (1,1) nodal line, that implies an unloaded f01 near 515–545 Hz.

### 2.3 Moisture

Wetting goat skin drops tension about 3× and E about 2× (S8). Cuiqueiros wet the cloth, and water reaches the head around the knot. Kalango warns that touching the skin from inside with the damp cloth "too often" softens it around the knot and can tear it [Q, S9]. For the model, head tension should be a slowly drifting parameter.

### 2.4 Amplitude-dependent tension

Rossing (S7): "the increase in average tension ΔT is proportional to the square of the displacement amplitude". A bass drum struck with 6 mm initial amplitude shifts up "about 10%" [M]. This is relevant to pitch rising with bowing force (§5.3).

---

## 3. Stick (haste, vareta, gambito, palito)

| Quantity | Value | Source / tag |
|---|---|---|
| Material | bamboo (usual); "wooden stick" also said | S1, S5, Kalango [Q/S] |
| Length, Gope replacement | ≈23 cm | Kalango A371790 [S] |
| Listed weight, Gope set of 2 with string | 0.05 kg (shipping weight, so an upper bound) | Kalango [S] |
| Length rule, Remo synthetic | shell length − 1" (≈20–30 cm for 23–31 cm shells) | S10 [S] |
| Homemade guides | ≈30 cm bamboo rod; coffee-can version uses a 1/8" (3.2 mm) bamboo skewer | vocepergunta.com; Hopkin via S11 [Q] |
| Diameter | not published. Kalango: "thin sticks respond more easily but break faster; thick sticks are hard to control, usually you have to sand them down" | S9 [Q] |
| Estimated diameter | 4–8 mm (sanded culm splint, often not round) | [E] |
| Estimated mass, 23 cm stick | d 4 mm → 1.7–2.3 g; 5 mm → 2.7–3.6 g; 6 mm → 3.9–5.2 g; 8 mm → 6.9–9.2 g (ρ 600–800). With knot, string and the thickened skin around it, the effective centre mass is ≈3–8 g | [E] |
| Attachment, natural head | stick placed in the centre of a soaked, soft skin and a knot is tied, so the skin and string form the binding; dries "at least overnight" (no heat or sun, or it gets brittle) | Kalango blog [Q/S] |
| Attachment offset | S1: the stick is "not connected to the exact center", plus a slight tilt, which causes asymmetric ODS. Their FEM drive patch was 5 mm Ø, offset 1.4 mm from centre | S1 [M] |
| Bamboo density | ≈500–650 kg/m³ inner wall (Moso); 700–800 kg/m³ typical whole culm | PMC4233722; par.nsf.gov/10100580 [M] |
| Bamboo longitudinal E | 5–25 GPa whole culm (bending/compression; Guadua ≈17 GPa); cell wall ≈20–33 GPa; vascular bundles 9–45 GPa, rising toward the outer wall | core.ac.uk 83235737, 78053785; Forests 2024 [M] |
| ⇒ longitudinal sound speed | √(E/ρ) ≈ 2.8–6 km/s; ≈4.6 km/s for 15 GPa / 700 kg/m³. An outer-wall splint is likely 5–6 km/s | [E] |
| ⇒ first longitudinal mode, 23 cm stick | free-free ≈10 kHz; fixed-free ≈5 kHz. So the stick is a rigid mass on the membrane below about 3 kHz | [E] |
| ⇒ flexural modes, d = 5 mm | cantilever from head ≈60 Hz; free-free ≈390 Hz. Bending is not driven by axial rubbing, but tilt or off-centre attachment can couple it (S1) | [E] |

**Stick mass versus membrane mass [E].** This is the single most important number for the pitch.

- Membrane mass: 6" (a ≈ 75 mm) ≈ 7 g; 8" (a ≈ 95 mm) ≈ 11 g; 10" (a ≈ 120 mm) ≈ 18 g, at σ = 0.40 kg/m².
- Mass ratio μ = m_stick / M_membrane is therefore ≈0.2–0.8.

I solved the axisymmetric membrane with a rigid central disc (radius b = 0.03–0.05a, mass ratio μ, fixed rim). The frequency equation is μ·k·w(b) = 2b·w′-term in J0/Y0. Results:

| μ | f01 / f01(no stick) | semitones | (0,2)/(0,1) | (0,3)/(0,1) |
|---|---|---|---|---|
| 0 | 1.00 | 0 | 2.30 | 3.61 |
| 0.1 | 0.79–0.81 | −3.7 to −4.2 | 1.87–1.94 | 3.40–3.44 |
| 0.25 | 0.58–0.61 | −8.7 to −9.6 | 2.28 | 4.47–4.56 |
| 0.5 | 0.43–0.46 | −13.6 to −14.8 | 2.91–2.97 | 5.9–6.1 |
| 1.0 | 0.31–0.33 | −19 to −20 | 3.9–4.0 | 8.1–8.5 |

So the head plus stick behaves as a mass on a membrane spring. The fundamental drops 1–1.5 octaves and the axisymmetric overtones become strongly inharmonic relative to it.

Example: S1's FEM head has f01 = 555 Hz. With μ ≈ 0.5–0.7 it would sit at ≈200–250 Hz, which matches the measured open tones in §5.

The static stiffness of the membrane seen by the stick is K ≈ 2πT / ln(a/b), about 5–9 kN/m for T = 3000–4500 N/m, a/b ≈ 25 [E].

---

## 4. Playing technique

- **Cloth.** A small damp cloth is held in one hand and pinched around the stick (thumb, index and middle fingers, per BYU). It is rubbed along the stick with push and pull strokes [Q; BYU percussion page https://percussion.byu.edu/cuica; S5; S1].
  - Experienced players often use a wet grosgrain ribbon instead of a rag [Q, S5].
  - Kalango: the cloth should be "slightly rough, grippy, and durable" [Q].
- **Grip.** "A tighter grip on the rod raises the pitch" [Q, BYU]. This is consistent with normal force raising the friction force, which raises static deflection and average tension (§2.4) and the stick-slip regime.
- **Other hand (pitch).** A finger or thumb presses the head from outside, near the centre where the stick is attached. "The closer the finger is to the centre, the higher the pitch" [Q, vocepergunta.com; S1: "pressing against the center of the drum"]. More pressure gives a higher pitch [Q, S5, BYU, Wikipedia]. If the hand does not touch, you get the open tone [Q, S1].
- **Range.** "Distinct melodic lines spanning an octave or more" [Q, S5]. "Up to two octaves" [Q, BYU]. No measurement found.
- **Holding.** Seated, the drum lies horizontally on a knee; standing, it is held at chest height on a strap [Q, S1].
- **Stroke speeds, lengths, rates.** I found no published measurements.
  - DAFx model input range: bow velocity v_B = 0–0.2 m/s; normal force f_N = 0–20 N, with 12 N and 0.1 m/s used as a demo point [S3, tuned for a Sensel controller, not measured on players].
  - Estimate: stroke length 2–10 cm along the stick in 50–200 ms, so cloth sliding speed ≈0.2–1 m/s. Pinch normal force ≈2–15 N [E].
  - In the Commons clip Cuica01.ogg the stroke pattern repeats every ≈255 ms, with paired onsets ≈50 ms apart (push/pull doublets). That is ≈4 stroke pairs/s, consistent with samba 16ths around 120 bpm [A].
  - In the Pearl clip, separate notes start every 0.3–0.4 s [A].

**Physics estimate of finger pressing [E].** I give two mechanisms. Both scale strongly with how close the press is to the stick, which matches "closer = higher".

1. **Shortened spring for the stick mass.** A press at radius r_f effectively clamps the membrane there, so the stick mass sees K′ ≈ 2πT / ln(r_f/b) instead of 2πT / ln(a/b). In the mass-dominated limit f ∝ √K. With a = 100 mm and b = 4 mm:
   - r_f = 30 mm: ×1.27 (+4 st)
   - r_f = 15 mm: ×1.56 (+7.7 st)
   - r_f = 10 mm: ×1.88 (+11 st)
   - r_f = 6 mm: ×2.8 (+18 st)

   A full-ring clamp of an *unloaded* membrane at inner radius b/a = 0.02, 0.05, 0.1, 0.2 and 0.3 raises f01 by +3.2, +4.2, +5.6, +8.0 and +10.5 st (computed). A single fingertip is weaker than a full ring, so these are upper bounds for the constraint effect alone.
2. **Indentation stretches the skin.** In S8 the head's working strain is only ≈0.28%, and E·h ≈ 245 kN/m is about 80–180× T. An extra radial strain of 0.3–0.5%, from a 2–3 mm indentation over ≈20–30 mm, would roughly double or triple T, giving +6 to +9 st on its own.

Combined, an octave or more is easily reachable, consistent with S5 and BYU.

---

## 5. Measured sound

### 5.1 Published

- **Wheeler 2002 (S2), gourd folk cuíca** [M, qualitative abstract only]:
  - Rubbing produces a "primitive saw-toothed excitation", like a bowed violin string.
  - The membrane modifies and radiates it.
  - The body contributes little.
  - Pitch changes by pressure on the membrane.
  - The paper reported waveforms, spectra and time envelopes, but the abstract gives no numbers.
- **Statsenko et al. 2015 (S1)** [M]:
  - Shaker-driven stick, 200–2000 Hz, low amplitude, so these are not playing conditions.
  - ODS images at 220, 820, 872 (highly asymmetric), 942, 1360 [(0,2)-like], 1752 [(4,1)-like] and 1971 Hz [(2,2)-like].
  - LDV amplitudes at three points: 0.08–0.2 µm at 260 Hz and 0.2–0.28 µm at 750 Hz (ESPI vs LDV agree within ≈35 nm).
  - The (1,1)-family ODS become "highly asymmetric with increasing excitation force" because of the off-centre stick and tilt.
  - Their description: "high pitched sound recalling an animal voice with large dynamic range".
- **Batista 2016 (S6)**, UFU, unknown cuíca, open "low" note:
  - A tuner read it as C4, written as "269 Hz" (C4 is 261.6 Hz, so the note name is more trustworthy than the Hz figure) [M, weak].
  - The thesis's Audacity peaks at 53.8, 118.4 and 183.0 Hz and the fitted 58.8 / 114.4 / 180.3 Hz are inconsistent with that note. They look like analysis artefacts (too-short window). **Do not use.**
- I found no published f0 ranges, glide rates, onset times, SPL or formant studies for the cuíca.

### 5.2 Analysis of CC-licensed recordings [A]

Method: three Wikimedia Commons files, resampled to 11.025 kHz; YIN pitch (1024-sample frames, 23 ms hop, aperiodicity < 0.25); harmonic levels by Goertzel on 80 ms Hann frames; 5 ms RMS envelopes. Instrument size and tuning are unknown.

**(a) "Cuica.Cuica.wav", Pearl Drums, CC BY-SA 4.0, 7.7 s, 48 kHz.** https://commons.wikimedia.org/wiki/File:Cuica.Cuica.wav

- **Open tones:**
  - f0 starts at 236–251 Hz and glides up to 285–297 Hz within each stroke.
  - Glide per stroke is +2.0 to +3.6 semitones in 90–165 ms, about 15–25 st/s, then the pitch levels off around 288–296 Hz.
  - Measured examples: 236→284 Hz over 160 ms; 251→282 Hz over 93 ms; 240→292 Hz over 163 ms; 247→290 Hz over 116 ms (twice).
- **Pressed tones:**
  - 460–540 Hz for long passages (median ≈500 Hz, so about +9 to +10 st above the open tone).
  - Peaks of 584→643 Hz (+1.6 st in 70 ms, then falling back to 605 Hz).
  - The total span seen is ≈236–643 Hz (about 17 st, 1.4 octaves).
- **Spectrum (harmonic levels re strongest):**
  - Open tone at 288 Hz: H2 −4, H3 −16, H4 −27, H5 −27, H6–H8 ≈ −50 to −56 dB.
  - Open tone at 296 Hz, a softer moment: H2 −23, H3 −36, H4 −41 dB.
  - Pressed 494–515 Hz: H2 −14, H3 −23 to −30, H4 −30 dB.
  - Pressed 642 Hz: nearly sinusoidal (H2 −52 dB).
  - So this cuíca is fundamental-dominated, a hooting "oo" quality with little energy above ≈1.5 kHz.
- **Onsets:** 10–90% RMS rise mostly 10–45 ms. The fastest are ≤15 ms; 70–100 ms when the stroke swells.

**(b) "Cuica01.ogg" and "Cuica02.ogg", CC BY-SA 3.0, unattributed, 22.05 kHz.** https://commons.wikimedia.org/wiki/File:Cuica01.ogg and https://commons.wikimedia.org/wiki/File:Cuica02.ogg

- **Pitch:**
  - Low grunting tones at f0 ≈ 133–155 Hz (Cuica01 median 142 Hz, interquartile range 138–145 Hz).
  - Within-stroke rises of ≈1–2 st (133→146 Hz).
  - Cuica02 also has a lower note at 105–117 Hz with a weak fundamental (H1 −18 dB below H2/H3) and some 150–192 Hz notes.
- **Spectrum:** much richer than (a).
  - At 145–147 Hz, H1, H2 and H3 are within 4 dB; H4–H5 ≈ −11 dB; energy stays above −45 dB out to H20–H25 (≈3–3.7 kHz).
  - Local spectral peaks that stay at the same frequency when f0 changes (formant-like) sit near **1.15–1.2 kHz** and **1.45–1.6 kHz**:
    - At 145 Hz: H8 (1.17 kHz) is −19/−20 dB versus −27/−37 dB for H6–H7.
    - At 110 Hz: H7 (0.77 kHz) and H11 (1.21 kHz) are local peaks.
  - This is the classic "ronco" (growl).
- **Attack:** not resolved (continuous sound, see stroke-rate notes in §4).

### 5.3 Interpretation for the model [E]

- **Source.** Stick-slip at the cloth gives a sawtooth-like velocity drive (S2). DAFx reproduces Helmholtz-like motion and a hysteresis loop with an elasto-plastic model (S3).
  - The f0 is set mainly by the stick-mass-on-membrane resonance (§3), lowered further by air loading. S7 says air loading "lowers the low-frequency modes" and that a baffled-piston model gives a "reasonably good estimate".
  - A rough per-side added mass is 0.85ρa ≈ 0.1 kg/m², against σ ≈ 0.4 kg/m², so roughly −1.5 to −3 st.
  - Predicted open tone for an 8" head (T 3000–4500 N/m, μ 0.3–0.6): ≈160–250 Hz. For 10": 120–200 Hz. This fits the measured 140–300 Hz.
- **Formants and the vocal quality.**
  - Higher axisymmetric membrane modes, (0,2) at ≈2–3× and (0,3) at ≈4.5–6× the loaded f01, act as fixed resonances shaping a harmonic source. That is a source-filter structure, as in the voice.
  - Tube quarter-wave resonances (230–300, 700–900, 1150–1470 Hz) add more. The measured fixed peaks near 1.15–1.2 and 1.45–1.6 kHz are consistent with (0,3)-type membrane modes or f5 of a 25–30 cm tube. Not resolved.
  - Pressing near the centre moves both f0 and these resonances, giving vowel-like timbre changes.
  - The "laughing" figure is the fast push/pull doublets (≈50 ms apart) plus upward glides of 2–4 st per stroke.
- **Why the pitch rises during a stroke (hypotheses).** Each has some support; none is measured.
  1. Rising friction force increases static deflection, which raises tension (amplitude-dependent tension per S7).
  2. The pressing hand tightens.
  3. In a relaxation-type stick-slip, period shortens with bow velocity or force.
- **Levels.** No SPL data found. Qualitative: "large dynamic range" (S1); bigger head and thicker skin are louder (S9).

---

## 6. Friction physics

- **No published wet-cotton-on-bamboo friction coefficients** were found.
- **Model values in use** (S3, S4, taken from violin and stiff-string literature and tuned by ear):

| Parameter | Value |
|---|---|
| µ_C (Coulomb) | 0.3 |
| µ_S (static) | 0.8 |
| Stribeck velocity v_S | 0.1 m/s |
| Bristle stiffness s0 | 10⁵ N/m |
| Bristle damping s1 | 0.001·√s0 kg/s |
| Viscous s2 | 4 kg/s (thesis demo used 0.4 kg/s) |
| Noise | s3 = 0–0.04·f_N |
| Breakaway z_ba | 0.7·F_C/s0 |
| Normal force f_N | 0–20 N |
| Bow velocity v_B | 0–0.2 m/s |

  The Stribeck curve form is μ(v) = μ_C + (μ_S − μ_C)·exp(−(v/v_S)²) (S3 Eq. 14; S4 Eq. 3.6). The elasto-plastic model is from Dupont et al. 2002, as applied by Serafin, Avanzini & Rocchesso 2003 and Willemsen, Bilbao & Serafin 2019.
- **Proxies:**
  - Wood on wet wood μ_s ≈ 0.2; cotton on cotton ≈ 0.3 (EngineeringToolbox, low quality) [S].
  - Wet fabric on skin is 1.5–7× more frictional than dry, "more than twofold" when fully wet (textile-skin tribology reviews, e.g. PMC2607440, Tribol. Lett.) [M, different pair].
  - Water-saturated wood shows equal or higher μ than dry wood in some studies and lower in others (Murase, via search) [M, mixed].
  - Water-lubricated wood-ceramic on steel: μ ≈ 0.1–0.16 [M, irrelevant pair].
- **Physical argument [E].** Wet cotton on bamboo acts like wet skin on a fabric, a soft and wet contact with real-contact-area growth. Static μ is plausibly 0.5–1.0 and dynamic 0.2–0.5, with a falling μ(v) needed for stick-slip. Players' preference for a rough, grippy cloth or ribbon and the need to keep it wet suggest the water raises the static-to-dynamic contrast. A dry cloth slides without speaking. This is a measurement gap worth filling with a simple sled test.
- **Friction-drum literature:**
  - Balfour 1907 "The friction-drum" (J. R. Anthropol. Inst. 37, 67–92) is organological.
  - Akay 2002 "Acoustics of friction" (JASA 111, 1525) is a general review cited by S1.
  - Serafin 2004 PhD "The sound of friction" (Stanford).
  - Avanzini, Serafin & Rocchesso 2005 (IEEE TSAP 13(5)), friction models for sound.
  - Smith & Woodhouse 2000 "Tribology of rosin" (JMPS 48), the hysteresis loop.
  - Galante 2015 (USP MSc, history of Central-African friction drums in Brazil) and Bicolor 2016 (UFRJ MSc, bibliography of cuíca information) are cultural or historical, not acoustic (cited in S5).
  - Rossing's *Science of Percussion Instruments* friction-drum section could not be accessed. Lion's roar, rommelpot, zambomba and putipù have no measurement papers found.

---

## 7. Membrane modes

- **Ideal circular membrane, ratios to f01** (S11, Bessel zeros): (1,1) 1.594, (2,1) 2.136, (0,2) 2.296, (3,1) 2.653, (1,2) 2.918, (4,1) 3.156, (2,2) 3.501, (0,3) 3.600. Zeros: j01 = 2.4048, j11 = 3.8317, j21 = 5.1356, j02 = 5.5201, j31 = 6.3802, j03 = 8.6537 (computed) [exact].
  - f01 = 0.766·√(T/σ)/D (S11).
- **Kettledrum with air loading and a kettle** (S7) [M], relative to (1,1): (2,1) 1.50, (3,1) ≈1.98–2.00, (4,1) 2.44, (5,1) 2.90. The ideal values are 1.34, 1.66, 1.83 for (2,1), (3,1), (1,2).
  - Air loading "lowers the low-frequency modes" and is "mainly responsible" for the harmonicity.
  - Enclosed-air stiffness raises the axisymmetric (0,1), (0,2), (0,3) modes. Smaller volume raises (0,n) and lowers (m,1).
  - (0,1) in a baffle is a monopole and damps fast.
- **Central mass load.** Tabla and mridanga: the loaded patch has ≈10× the areal density of the unloaded skin (0.02–0.03 g/cm²). Patch mass is 9–15 g (tabla) and 29 g (mridanga, 0.8 g/cm², 3 mm thick) (S7) [M]. A heavy centre load pulls modes toward harmonic ratios; the cuíca stick is a much more concentrated load (§3).
  - The effective mass of the (0,n) modes at the centre is M·J1(j0n)² = 0.269, 0.116, 0.074, 0.054 × M (optomechanics membrane literature via search) [exact]. So a stick of only ~0.27M already halves the (0,1) modal stiffness-to-mass ratio.
- **Pressing or constraint.**
  - Tabla: resting the palm on the unloaded rim region constrains it and changes the sound quality (S7) [Q].
  - Dùndún talking drum: "the more the pressure, the higher the frequency" (Durojaye et al. 2021, Front. Commun. 6:652690). No Hz or semitone numbers given [Q].
  - See §4 for computed ring-clamp and indentation estimates.
- **Bending stiffness of skin** raises higher modes slightly. For goat skin with h = 0.3 mm and E ≈ 0.3–0.7 GPa, D = Eh³/12(1−ν²) ≈ 0.75–1.8 mN·m, small against T at a ≈ 0.1 m below ≈2 kHz [E].

---

## 8. Air column summary

- Open-ended cylinder closed by the head: f_n ≈ (2n−1)c / 4(L + 0.6133a) [S12; Levine & Schwinger 1948; flanged ≈ 0.82–0.85a]. The table in §1.2 gives 227–436 Hz fundamentals across sizes.
- The membrane is not a rigid end. Membrane (0,1)+stick and tube f1 are close for 8–10" × 23–30 cm shells, so expect two split coupled modes rather than one, as with kettledrum head–air coupling (S7) and DAFx's rigid membrane-tube connection (S3).
- Validity: 1D below the first axisymmetric duct cutoff (1.6–2.7 kHz). The m=1 cutoff is 0.8–1.3 kHz.
- Radiation from the open end: unflanged pipe, low-frequency radiation resistance ∝ (ka)²/4 [standard].

---

## 9. Gaps and what to measure

1. **f0 versus press position and force** on a known cuíca. The best quick option is pitch-tracking BRID solo cuíca tracks (Maia et al. 2018, AES LAC, Zenodo record 14051323; the class list needs confirming) or recording one.
2. **Stick mass and diameter.** Weigh a Gope/Izzo gambito (≈23 cm).
3. **Head tension.** Measure from the struck (1,1) frequency with the stick fixed: it sits on a nodal line, so f11 ≈ 1.594·f01(unloaded) gives T.
4. **Wet-cotton-on-bamboo μ_s, μ_d and μ(v)** with a sled test.
5. **SPL** at 1 m. Not published.
